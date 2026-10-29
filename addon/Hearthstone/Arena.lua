-- Hearthstone addon: the draft arena.
--
-- Pure logic, no WoW frames and no network: the run state, the strict parser of the server's ARENA payload (the
-- reply to ARENA_GET / ARENA_CLASS / ARENA_PICK / ARENA_CLAIM / ARENA_DUEL / ARENA_RETIRE and the pushes ARENA_UPD /
-- ARENA_OPEN), the idempotency key and the one step in flight (class choice, card pick, special card claim, next duel
-- or retirement), the texts of the states and of the refusals, and what the window shows (picked list, mana curve,
-- prize). Hearthstone.lua sends ARENA_GET / ARENA_CLASS / ARENA_PICK / ARENA_CLAIM / ARENA_DUEL / ARENA_RETIRE /
-- GAME_PAUSE and feeds the replies in; ArenaUI.lua only reads it. The server owns the run:
-- the addon sends ids and numbers, never state, and the model is replaced by every ARENA, never merged.
--
--   AR.NAME      the player-facing name of the mode (a working label: change it here only)
--   AR.state     "none" (not connected), "unsupported" (no "arena" in WELCOME caps), "loading", "ready" or "error"
--   AR.run       the last ARENA (AR.Empty() before the first one):
--                { st ("none"|"class"|"draft"|"ready"|"duel"|"paused"|"finished"|"retired"|"settled"|"repair"), run,
--                  enabled, pay ("", "free", "gold", "coins" or "practice": no prize), cls (the drafted class or ""), wins, draws, losses,
--                  duels, no (the open offer: 0 = classes, 1..30 = cards, nil = none), offer = { three class names or
--                  card ids }, picked (0..30), deck = { { id, n } } (sorted by id), prize = { packs, coins, special },
--                  terms = { packs, coins }, specialPending, specials = { { id, n } }, claimed, live, mine, replay }
--   AR.op        the step that is not confirmed yet, or nil:
--                { kind ("class", "pick", "claim", "duel" or "retire"), key, arg (the class or the card id), no, run, reqId, reqIds =
--                  { [reqId] = true }, attempts, retryIn, inFlight }
--   AR.pausing   the GAME_PAUSE in flight, or nil: { mid, reqId }
--   AR.message   the last result line of the window, or nil
--   AR.atHost    true while the window was opened by a host (ARENA_OPEN) and the player has not walked away from it
--                (ARENA_CLOSE): only then do the Play and Retire buttons work (the server checks it again)
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsArena = HsArena or {}
local AR = HsArena
local CM = HsCollection

AR.NAME = "Night Watch Arena"   -- the player-facing name of the mode (owner decision); the only place it is written
AR.HOSTS = "Ezra Chatterton or Ibelin Redmoore"   -- the two hosts of the mode (module gossip)
AR.HOSTS_SHORT = "Ezra or Ibelin"
AR.PICKS = 30                  -- cards of a deck
AR.WIN_TARGET = 10             -- wins that end a run with the full prize
AR.OFFER_SIZE = 3
AR.MAX_ATTEMPTS = 3            -- sends of one step (same key) before the addon gives up and resyncs
AR.MAX_RUN = 999999999         -- run ids (the server takes 9 digits)
AR.KEY_LEN = 12
AR.CLASSES = { "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR" }
AR.STATES = { none = true, class = true, draft = true, ready = true, duel = true, paused = true, finished = true,
    retired = true, settled = true, repair = true }
AR.PAY = { [""] = true, free = true, gold = true, coins = true, practice = true }
AR.PAUSED_TEXT = "Duel paused. Resume it at " .. AR.HOSTS_SHORT .. "."

local CLASS_SET = {}
for i = 1, table.getn(AR.CLASSES) do
    CLASS_SET[AR.CLASSES[i]] = true
end

-- --------------------------------------------------------------- parsing
-- Canonical decimal (no sign, no leading zeros) of at most maxDigits digits, or nil.
local function parseNumber(s, maxDigits)
    if type(s) ~= "string" or not string.find(s, "^%d+$") or string.len(s) > maxDigits then
        return nil
    end
    if string.len(s) > 1 and string.sub(s, 1, 1) == "0" then
        return nil
    end
    return tonumber(s)
end

local function validId(id)
    return type(id) == "string" and string.find(id, "^[%w_]+$") ~= nil and string.len(id) <= HsPacks.MAX_ID_LEN
end

-- Splits "a,b,c" (no empty items allowed, at most maxItems). Returns a list or nil.
local function splitList(s, maxItems)
    local out = {}
    local pos = 1
    while true do
        local comma = string.find(s, ",", pos, true)
        local item
        if comma then
            item = string.sub(s, pos, comma - 1)
        else
            item = string.sub(s, pos)
        end
        if item == "" then
            return nil
        end
        table.insert(out, item)
        if table.getn(out) > maxItems then
            return nil
        end
        if not comma then
            return out
        end
        pos = comma + 1
    end
end

-- "CS2_022.2,EX1_116.1" -> { { id, n }, ... } in the order sent (each n 1..maxCopies, no id twice), "" -> {}, else nil.
local function parseCounts(s, maxItems, maxCopies)
    if type(s) ~= "string" then
        return nil
    end
    local out = {}
    if s == "" then
        return out
    end
    local items = splitList(s, maxItems)
    if not items then
        return nil
    end
    local seen = {}
    for i = 1, table.getn(items) do
        local _, _, id, nS = string.find(items[i], "^([%w_]+)%.(%d+)$")
        local n = parseNumber(nS, 2)
        if not id or not validId(id) or n == nil or n < 1 or n > maxCopies or seen[id] then
            return nil
        end
        seen[id] = true
        table.insert(out, { id = id, n = n })
    end
    return out
end

local function parseFlag(s)
    if s == "0" then
        return false
    elseif s == "1" then
        return true
    end
    return nil
end

-- The model of "no run": what the window shows before the first ARENA.
function AR.Empty()
    return { st = "none", run = 0, enabled = false, pay = "", cls = "", wins = 0, draws = 0, losses = 0, duels = 0,
        no = nil, offer = {}, picked = 0, deck = {}, prize = { packs = 0, coins = 0, special = false },
        terms = { packs = 0, coins = 0 }, specialPending = false, specials = {}, claimed = "", live = false,
        mine = false, replay = false }
end

-- Parses an ARENA payload (see docs/PROTOCOL.md, "Draft arena"). Returns the model or nil and a reason; one bad
-- key rejects the whole reply.
function AR.ParseArena(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local m = AR.Empty()
    if not AR.STATES[kv.st or ""] then
        return nil, "bad st"
    end
    m.st = kv.st
    m.run = parseNumber(kv.run, 9)
    if m.run == nil then
        return nil, "bad run"
    end
    local flag = parseFlag(kv.en)
    if flag == nil then
        return nil, "bad en"
    end
    m.enabled = flag
    if not AR.PAY[kv.pay or "x"] then
        return nil, "bad pay"
    end
    m.pay = kv.pay
    if kv.cls ~= "" and not CLASS_SET[kv.cls or ""] then
        return nil, "bad cls"
    end
    m.cls = kv.cls
    m.wins, m.draws, m.losses = parseNumber(kv.w, 3), parseNumber(kv.d, 3), parseNumber(kv.l, 3)
    if m.wins == nil or m.draws == nil or m.losses == nil or m.draws > m.wins then
        return nil, "bad record"
    end
    m.duels = parseNumber(kv.duel, 5)
    if m.duels == nil then
        return nil, "bad duel"
    end
    if kv.no == nil then
        return nil, "missing no"
    end
    if kv.no ~= "" then
        m.no = parseNumber(kv.no, 2)
        if m.no == nil or m.no > AR.PICKS then
            return nil, "bad no"
        end
    end
    if type(kv.of) ~= "string" then
        return nil, "missing of"
    end
    if kv.of ~= "" then
        local offer = splitList(kv.of, AR.OFFER_SIZE)
        if m.no == nil or not offer or table.getn(offer) ~= AR.OFFER_SIZE then
            return nil, "bad offer"
        end
        for i = 1, AR.OFFER_SIZE do
            if m.no == 0 then
                if not CLASS_SET[offer[i]] then
                    return nil, "bad class offer"
                end
            elseif not validId(offer[i]) then
                return nil, "bad card offer"
            end
        end
        m.offer = offer
    end
    m.picked = parseNumber(kv.pk, 2)
    if m.picked == nil or m.picked > AR.PICKS then
        return nil, "bad pk"
    end
    m.deck = parseCounts(kv.dk, AR.PICKS, AR.PICKS)
    if not m.deck then
        return nil, "bad dk"
    end
    local total = 0
    for i = 1, table.getn(m.deck) do
        total = total + m.deck[i].n
    end
    if total > AR.PICKS then
        return nil, "deck too large"
    end
    local _, _, pp, pc, ps = string.find(type(kv.pz) == "string" and kv.pz or "", "^(%d+)%.(%d+)%.([01])$")
    local packs, coins = parseNumber(pp, 3), parseNumber(pc, 7)
    if packs == nil or coins == nil then
        return nil, "bad pz"
    end
    m.prize = { packs = packs, coins = coins, special = ps == "1" }
    local _, _, tp, tc = string.find(type(kv.tb) == "string" and kv.tb or "", "^(%d+)%.(%d+)$")
    packs, coins = parseNumber(tp, 3), parseNumber(tc, 7)
    if packs == nil or coins == nil then
        return nil, "bad tb"
    end
    m.terms = { packs = packs, coins = coins }
    flag = parseFlag(kv.spc)
    if flag == nil then
        return nil, "bad spc"
    end
    m.specialPending = flag
    m.specials = parseCounts(kv.sp, 8, 2)
    if not m.specials then
        return nil, "bad sp"
    end
    if kv.sc ~= "" and not validId(kv.sc) then
        return nil, "bad sc"
    end
    m.claimed = kv.sc
    m.live, m.mine, m.replay = parseFlag(kv.live), parseFlag(kv.mine), parseFlag(kv.rp)
    if m.live == nil or m.mine == nil or m.replay == nil then
        return nil, "bad flags"
    end
    return m
end

-- True when the WELCOME caps list ("solo,chunk,packs") contains "arena".
function AR.CapsHaveArena(caps)
    if type(caps) ~= "string" then
        return false
    end
    local items = splitList(caps, 64)
    if not items then
        return false
    end
    for i = 1, table.getn(items) do
        if items[i] == "arena" then
            return true
        end
    end
    return false
end

-- ------------------------------------------------------------------ state
function AR.Reset()
    AR.state = "none"
    AR.errorText = nil
    AR.run = AR.Empty()
    AR.pausing = nil
    AR.atHost = false
    if AR.op then
        AR.op.inFlight = false
        AR.op.retryIn = nil
        AR.op.reqId = nil
    end
end

function AR.SetUnsupported()
    AR.Reset()
    AR.state = "unsupported"
end

-- An ARENA_GET is on its way. A refetch while ready keeps the current data on screen.
function AR.SetLoading()
    if AR.state ~= "ready" then
        AR.state = "loading"
        AR.errorText = nil
    end
end

function AR.SetReady(res)
    AR.state = "ready"
    AR.errorText = nil
    AR.run = res
end

function AR.SetError(text)
    AR.state = "error"
    AR.errorText = text
end

function AR.IsReady()
    return AR.state == "ready"
end

-- How far a run has come inside the draft: the class choice and every pick only ever add to it.
function AR.Step(m)
    return (m.cls ~= "" and 1 or 0) + m.picked
end

-- Is res an older picture of the run we have? An ARENA_UPD for a step of ours arrives after the reply; if the
-- player already made the next step, that push would put the previous offer back.
function AR.IsStale(res)
    local cur = AR.run
    return AR.state == "ready" and res.run ~= 0 and res.run == cur.run and AR.Step(res) < AR.Step(cur)
end

-- True while the run of this account is a duel that this character plays (the match window shows it).
function AR.IsArenaDuel()
    local r = AR.run
    return AR.state == "ready" and r.st == "duel" and r.live and r.mine
end

-- States in which the run can still be retired at the host.
function AR.CanRetire(m)
    return m.st == "class" or m.st == "draft" or m.st == "ready" or m.st == "paused"
end

-- States in which the Play button starts (ready) or resumes (paused) a duel.
function AR.CanPlay(m)
    return m.st == "ready" or m.st == "paused"
end

-- ----------------------------------------------------------- the picture
local function card(id)
    return HS_CardDB and HS_CardDB.cards and HS_CardDB.cards[id]
end

function AR.CardName(id)
    local c = card(id)
    return c and c.name or tostring(id)
end

function AR.ClassLabel(class)
    return CM.ClassLabel(class)
end

-- The picked cards as rows { id, name, cost, n, rarity } sorted by cost, then name, then id.
function AR.DeckRows(m)
    local rows = {}
    for i = 1, table.getn(m.deck) do
        local c = card(m.deck[i].id)
        table.insert(rows, { id = m.deck[i].id, name = c and c.name or m.deck[i].id, cost = c and c.cost or 0,
            n = m.deck[i].n, rarity = c and c.rarity or "COMMON" })
    end
    table.sort(rows, function(a, b)
        if a.cost ~= b.cost then
            return a.cost < b.cost
        end
        if a.name ~= b.name then
            return a.name < b.name
        end
        return a.id < b.id
    end)
    return rows
end

function AR.DeckCount(m)
    local n = 0
    for i = 1, table.getn(m.deck) do
        n = n + m.deck[i].n
    end
    return n
end

-- Cards per mana cost 0..7 (7 = 7 or more).
function AR.Curve(m)
    local t = {}
    for i = 0, CM.MANA_MAX do
        t[i] = 0
    end
    for i = 1, table.getn(m.deck) do
        local c = card(m.deck[i].id)
        local b = CM.ManaBucket(c and c.cost or 0)
        t[b] = t[b] + m.deck[i].n
    end
    return t
end

-- The cards of the special-card choice: the missing sets (sp), or - when spc=1 and sp is empty, every set is complete -
-- the four reward-only cards, so the player can still pick one (it then gives one extra pack). { id, n, complete }.
function AR.SpecialChoices(m)
    local out = {}
    if not m.specialPending then
        return out
    end
    if table.getn(m.specials) > 0 then
        for i = 1, table.getn(m.specials) do
            table.insert(out, { id = m.specials[i].id, n = m.specials[i].n, complete = false })
        end
        return out
    end
    local db = HS_CardDB and HS_CardDB.cards
    if type(db) == "table" then
        local ids = {}
        for id, c in pairs(db) do
            if c.acq == "reward_only" then
                table.insert(ids, id)
            end
        end
        table.sort(ids)
        for i = 1, table.getn(ids) do
            table.insert(out, { id = ids[i], n = 0, complete = true })
        end
    end
    return out
end

local function plural(n, one, many)
    if n == 1 then
        return n .. " " .. one
    end
    return n .. " " .. many
end

-- "3 packs + 100 Hearthstone Coins + a special card of your choice" for { packs, coins, special }.
function AR.PrizeText(p)
    if p.packs == 0 and p.coins == 0 and not p.special then
        return "nothing"
    end
    local parts = {}
    if p.packs > 0 then
        table.insert(parts, plural(p.packs, "pack", "packs"))
    end
    if p.coins > 0 then
        table.insert(parts, p.coins .. " Hearthstone Coins")
    end
    if p.special then
        table.insert(parts, "a special card of your choice")
    end
    return table.concat(parts, " + ")
end

-- The prize table of the run (tb): { { wins, text } }, five lines.
function AR.PrizeTable(m)
    local base, bonus = m.terms.packs, m.terms.coins
    return {
        { "0 wins", "nothing" },
        { "1-2 wins", "1 pack" },
        { "3-5 wins", plural(base, "pack", "packs") },
        { "6-9 wins", AR.PrizeText({ packs = base, coins = bonus, special = false }) },
        { "10 wins", AR.PrizeText({ packs = base, coins = bonus, special = true }) .. " (or 1 more pack once you own them all)" },
    }
end

-- What the window says about the run: heading and a paragraph (colour codes allowed).
function AR.StateInfo(m)
    local st = m.st
    local hosts = AR.HOSTS
    if st == "none" then
        local text = "You have no run. Talk to " .. hosts .. " to enter."
        if not m.enabled then
            text = "New entries are closed right now. " .. text
        end
        return AR.NAME, text
    elseif st == "class" then
        return "Choose your class", "Pick one of the three classes. Every card you draft comes from that class and the neutral cards."
    elseif st == "draft" then
        return "Pick a card (" .. (m.no or m.picked + 1) .. " of " .. AR.PICKS .. ")",
            "Click one of the three cards. Your picks are saved: you can close this window and carry on later."
    elseif st == "ready" then
        return "Your deck is ready",
            "Visit " .. hosts .. " to start your next duel. Win " .. AR.WIN_TARGET .. " duels for the full prize; one loss ends the run."
    elseif st == "duel" then
        if m.mine then
            return "A duel is running", "Closing the duel window pauses it; you can resume at " .. AR.HOSTS_SHORT .. "."
        end
        return "A duel is running", "Another character of your account is playing this duel."
    elseif st == "paused" then
        return "Your duel is paused", "Visit " .. hosts .. " to resume it where you left it."
    elseif st == "finished" or st == "retired" then
        if m.pay == "practice" then
            return st == "retired" and "Run retired" or "Run over",
                "The practice run ended with " .. plural(m.wins, "win", "wins") .. "."
        end
        return st == "retired" and "Run retired" or "Run over",
            "The run ended with " .. plural(m.wins, "win", "wins") .. ". Your prize is being settled."
    elseif st == "settled" then
        local text = "The run ended with " .. plural(m.wins, "win", "wins") .. " and " .. plural(m.losses, "loss", "losses") .. "."
        if m.pay ~= "practice" then
            text = text .. " Prize: " .. AR.PrizeText({ packs = m.prize.packs, coins = m.prize.coins, special = false }) .. "."
        end
        if m.specialPending then
            text = text .. " Choose your special card below."
        elseif m.claimed ~= "" then
            text = text .. " You took " .. AR.CardName(m.claimed) .. "."
        end
        return "Run complete", text
    elseif st == "repair" then
        return "This run needs a game master", "It was saved with other game data, so it cannot be played on. Ask a game master to repair or retire it."
    end
    return AR.NAME, ""
end

-- One chat line when a run is settled ("Night Watch Arena run complete: ...").
function AR.SettledText(m)
    local text = AR.NAME .. " run complete: " .. plural(m.wins, "win", "wins") .. ", " .. plural(m.losses, "loss", "losses")
    if m.pay == "practice" then
        return text .. ". A practice run: no prize."
    end
    text = text .. ". Prize: " .. AR.PrizeText({ packs = m.prize.packs, coins = m.prize.coins, special = false }) .. "."
    if m.specialPending then
        text = text .. " You can choose a special card: /hs arena"
    end
    return text
end

-- ------------------------------------------------------------------ steps
-- REFUSED why=<code> -> the window's line.
AR.WHY_TEXT = {
    no_run = "You have no " .. AR.NAME .. " run.",
    stale = "The run changed meanwhile. The screen was refreshed.",
    bad_state = "That is not possible in the current state of the run.",
    bad_class = "That is not one of the three offered classes.",
    not_offered = "That card is not one of the three offered.",
    picked = "That offer was answered with another card already.",
    bad_card = "That card cannot be drafted.",
    deck = "The finished deck is not legal. Please tell a game master.",
    not_special = "That is not a special card.",
    not_missing = "You already own every copy of that card.",
    repair = "This run needs a game master.",
    unpaid = "The entry fee of this run is not paid yet. Visit " .. AR.HOSTS .. ".",
    key = "That request was used for something else. Try again.",
    overflow = "The grant would go over the copy limit of that card.",
    host = "Talk to " .. AR.HOSTS .. " for that.",
    running = "You are already in a match.",
    paused = "Your run has a paused duel. Resume it.",
    world = "You cannot do that right now.",
}

AR.NOT_AT_HOST = "Visit " .. AR.HOSTS .. " to play your next duel."
AR.NOT_AT_HOST_RETIRE = "Visit " .. AR.HOSTS .. " to retire your run."

function AR.RefusedText(why, text)
    local t = AR.WHY_TEXT[why or ""]
    if t then
        return t
    end
    if text and text ~= "" then
        return text
    end
    return "The server refused that."
end

-- Why the step cannot be sent for the run on screen (a short line), or nil. The server decides anyway.
function AR.OpBlocker(kind, arg)
    local m = AR.run
    if AR.IsBusy() then
        return "Your last choice is still on its way."
    end
    local offered = false
    for i = 1, table.getn(m.offer) do
        if m.offer[i] == arg then
            offered = true
        end
    end
    if kind == "class" then
        if m.st ~= "class" or m.no ~= 0 then
            return "There is no class to choose right now."
        end
        if not offered then
            return AR.WHY_TEXT.bad_class
        end
    elseif kind == "pick" then
        if m.st ~= "draft" or m.no == nil or m.no < 1 then
            return "There is no card to pick right now."
        end
        if not offered then
            return AR.WHY_TEXT.not_offered
        end
    elseif kind == "duel" then
        if not AR.CanPlay(m) then
            return "There is no duel to play right now."
        end
        if not AR.atHost then
            return AR.NOT_AT_HOST
        end
    elseif kind == "retire" then
        if not AR.CanRetire(m) then
            return "There is no run to retire."
        end
        if not AR.atHost then
            return AR.NOT_AT_HOST_RETIRE
        end
        if m.st == "duel" then
            return "Your duel is still running."
        end
    elseif kind == "claim" then
        if m.st ~= "settled" or not m.specialPending then
            return "There is no special card to claim."
        end
        local listed = false
        local choices = AR.SpecialChoices(m)
        for i = 1, table.getn(choices) do
            if choices[i].id == arg then
                listed = true
            end
        end
        if not listed then
            return AR.WHY_TEXT.not_special
        end
    else
        return "That step is not known."
    end
    return nil
end

-- Starts a step with a fresh idempotency key (HsPacks.NewKey rules). Returns the op.
function AR.BeginOp(kind, arg)
    local m = AR.run
    AR.op = { kind = kind, key = HsPacks.NewKey(), arg = arg, no = m.no, run = m.run, reqIds = {}, attempts = 0,
        inFlight = true }
    AR.message = nil
    return AR.op
end

function AR.ClearOp()
    AR.op = nil
end

function AR.IsBusy()
    return AR.op ~= nil and AR.op.inFlight == true
end

-- The request op of a step kind.
AR.OPS = { class = "ARENA_CLASS", pick = "ARENA_PICK", claim = "ARENA_CLAIM", duel = "ARENA_DUEL", retire = "ARENA_RETIRE" }

-- ARENA_CLASS / ARENA_PICK / ARENA_CLAIM / ARENA_DUEL / ARENA_RETIRE payload of the step: key=;class= | no=;id= | id= |
-- key only, then run= (when known).
function AR.OpPayload(op)
    local kv = { { "key", op.key } }
    if op.kind == "class" then
        table.insert(kv, { "class", op.arg })
    elseif op.kind == "pick" then
        table.insert(kv, { "no", tostring(op.no) })
        table.insert(kv, { "id", op.arg })
    elseif op.kind == "claim" then
        table.insert(kv, { "id", op.arg })
    end
    if op.run and op.run > 0 then
        table.insert(kv, { "run", tostring(op.run) })
    end
    return HsProtocol.KvEncode(kv)
end

-- The line under the window after a confirmed step.
function AR.DoneText(op, m)
    if op.kind == "claim" then
        if m.claimed ~= "" then
            return "Claimed " .. AR.CardName(m.claimed) .. ". The cards were added to your collection (or you got one more pack)."
        end
        return "Your special card was claimed."
    elseif op.kind == "class" then
        return nil                    -- the cards of the new offer say it
    elseif op.kind == "duel" then
        return nil                    -- the match window takes over
    elseif op.kind == "retire" then
        return AR.SettledText(m)
    end
    return nil
end

-- One line for /hs status.
function AR.StatusText()
    local s = AR.state
    local text
    if s == "ready" then
        local m = AR.run
        text = "arena: " .. m.st
        if m.run > 0 then
            text = text .. ", run " .. m.run .. ", " .. m.wins .. " wins, " .. m.losses .. " losses, " .. m.picked .. " of "
                .. AR.PICKS .. " cards"
        end
    elseif s == "loading" then
        text = "arena: loading"
    elseif s == "error" then
        text = "arena: error (" .. tostring(AR.errorText or "?") .. ")"
    elseif s == "unsupported" then
        text = "arena: not supported by this server"
    else
        text = "arena: none (not connected)"
    end
    local op = AR.op
    if op and op.inFlight then
        text = text .. ", sending " .. op.kind .. " (attempt " .. (op.attempts or 0) .. " of " .. AR.MAX_ATTEMPTS .. ")"
    end
    return text
end

if AR.state == nil then
    AR.op = nil
    AR.atHost = false
    AR.message = nil
    AR.Reset()
end
