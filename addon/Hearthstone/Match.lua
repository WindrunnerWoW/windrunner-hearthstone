-- Hearthstone addon: the match.
--
-- Pure logic, no WoW frames and no network: the strict parser of the server's GAME payload, the
-- match state, lookups over the legal actions (lg), the payloads of GAME_ACT / GAME_LEAVE, the one
-- action in flight, the mulligan marks and the event log texts. Hearthstone.lua sends GAME_GET /
-- GAME_ACT / GAME_LEAVE and feeds the replies and pushes into this file; MatchUI.lua only reads it.
-- The server (the rules engine) is the judge of every action: the legal list is only used to show
-- what can be clicked, and nothing here can change the match by itself.
--
--   M.state      "none" (no match), "loading" (the first GAME_GET is on its way), "mull", "play",
--                "over" or "error" (the GAME reply could not be read and no match is known)
--   M.supported  true when the WELCOME caps list "match"
--   M.game       the last GAME, or nil:
--                { mid, seq, ph ("mull"|"play"|"over"), turn, act ("m"|"o"|"-"), res ("-"|"win"|
--                  "loss"|"draw"|"int"), why, myName, oppName,
--                  me / opp = { hero = { id, cardId, health, maxHealth, armor, attack, flags,
--                  canAttack }, power = { id, cardId, cost, used }, weapon = nil | { id, cardId,
--                  attack, durability }, mana, maxMana, handCount, deckCount, fatigue, board =
--                  { { id, cardId, attack, health, maxHealth, taunt, charge, divine, windfury,
--                  canAttack, flags, stealth, frozen, silenced, poisonous, enraged, immune,
--                  cantAttack } } },
--                  hand = { { id, cardId, cost, attack, health } }, mulligan = { id },
--                  legal = { { kind ("p"|"a"|"h"), source, choice (1|2|nil), targets = { id, ... } } },
--                  evs, events = { { seq, type, side ("m"|"o"|"-"), source, target, amount,
--                  cardId } } }
--                dif ("normal"|"hard"|"boss"), hero.frozen / hero.immune, me / opp .secrets = { { id,
--                cardId (own only) } }, .cardsPlayed, .overloadLocked, .overloadOwed, options =
--                { [handId] = { optionCardId, ... } } (Choose One), pick = { { id, cardId } } (the
--                viewer's pending pick), oppPicking (true while the opponent picks)
--                Timer (tmr: seconds left of the Nozdormu turn timer when the snapshot was
--                built, rounded up; 0 = no timer)
--   M.timerLeft  the local countdown (seconds, fractions allowed) of the last snapshot's timer, or nil
--                without a timer; M.TimerTick(dt) lowers it, M.TimerSeconds() / M.TimerLow() read it
--   M.transition the last change of the game that the animation layer has not taken yet, or nil:
--                { mid, prev (the game before, nil for the first game of a match), game (the new one = M.game),
--                  events (the events of the new game with a seq above M.lastEventSeq, oldest first, deduplicated),
--                  lastSeen (the newest event seq handed out before), snap (true: nothing to animate), reason
--                  ("first" | "resync" when snap) }. M.TakeTransition() returns it once and clears it. Two
--                  changes that were not taken in between are merged (prev of the first, all events).
--   M.lastEventSeq the newest event seq of this match that was handed out in a transition (0 = none, or unknown
--                after a resync snapshot without events: the next batch is not checked for a gap)
--   M.eventFloor events with a seq up to this one were seen before such a resync and are never handed out again
--   M.pending    the one GAME_ACT that is not answered yet, or nil: { kind, mid, seq, payload, reqId }
--   M.marked     the offered cards the player marked for replacement: { [entityId] = true }
--   M.log        the event log lines of this match (coloured text), oldest first
--   M.history    the history tiles of this match, oldest first: { kind, side, cardId, targetCardId, lines } (see track)
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsMatch = HsMatch or {}
local M = HsMatch
local P = HsProtocol

M.MAX_LIST = 400              -- items of any list in a GAME payload
M.MAX_TIMER = 3600             -- the largest tmr accepted (the server sends 15)
M.TIMER_LOW = 5                -- the countdown is shown in red below this many seconds
M.MAX_EVENTS = 64            -- the server sends at most 40 events
M.MAX_TRANSITION_EVENTS = 160 -- events of a merged transition kept (the newest ones)
M.MAX_LOG = 200               -- log lines kept
M.MAX_HISTORY = 40            -- history tiles kept
M.MAX_TILE_LINES = 10         -- log lines kept per history tile
M.MAX_ID_LEN = 32             -- card ids, VARCHAR(32)
M.MAX_NUM = 4294967295
M.KINDS = { "play", "atk", "hp", "end", "concede", "mull", "pick" }
M.MAX_SECRETS = 10            -- secrets per side in a GAME payload (the rules allow 5)
M.MAX_OPTIONS = 4             -- Choose One options of a hand card
M.DIFFICULTIES = { normal = "Normal", hard = "Hard", boss = "Boss" }

local PHASES = { none = true, mull = true, play = true, over = true }
local ACTS = { m = true, o = true, ["-"] = true }
local RESULTS = { ["-"] = true, win = true, loss = true, draw = true, int = true }

-- ---------------------------------------------------------------- helpers
-- Decimal of at most 10 digits (optionally negative), or nil.
local function parseInt(s, signed)
    if type(s) ~= "string" or string.len(s) > 11 then
        return nil
    end
    if signed then
        if not string.find(s, "^-?%d+$") then
            return nil
        end
    elseif not string.find(s, "^%d+$") or string.len(s) > 10 then
        return nil
    end
    return tonumber(s)
end

local function validId(id)
    return type(id) == "string" and string.find(id, "^[%w_]+$") ~= nil and string.len(id) <= M.MAX_ID_LEN
end

-- Splits on a plain separator; "" gives an empty list, an empty item stays in the list.
local function split(s, sep, maxItems)
    local out = {}
    if s == "" then
        return out
    end
    local pos = 1
    while true do
        local i = string.find(s, sep, pos, true)
        if i then
            table.insert(out, string.sub(s, pos, i - 1))
        else
            table.insert(out, string.sub(s, pos))
        end
        if table.getn(out) > maxItems then
            return nil
        end
        if not i then
            return out
        end
        pos = i + string.len(sep)
    end
end

local function cleanName(s)
    return (string.gsub(s or "", "|", "||"))
end

-- --------------------------------------------------------------- parsing
-- Minion flags: "-" or letters of t c d w s f x p e i n a (taunt, charge, divine shield, windfury, stealth,
-- frozen, silenced, poisonous, enraged, immune, can't attack, can attack now) -> table of booleans, or nil.
local MINION_FLAGS = { t = "taunt", c = "charge", d = "divine", w = "windfury", s = "stealth", f = "frozen",
    x = "silenced", p = "poisonous", e = "enraged", i = "immune", n = "cantAttack", a = "canAttack" }
function M.ParseFlags(s)
    if type(s) ~= "string" or s == "" then
        return nil
    end
    local f = { taunt = false, charge = false, divine = false, windfury = false, stealth = false, frozen = false,
        silenced = false, poisonous = false, enraged = false, immune = false, cantAttack = false, canAttack = false }
    if s == "-" then
        return f
    end
    if not string.find(s, "^[tcdwsfxpeina]+$") then
        return nil
    end
    for letter, field in pairs(MINION_FLAGS) do
        f[field] = string.find(s, letter, 1, true) ~= nil
    end
    return f
end

-- Hero flags: "-" or letters of f i a (frozen, immune, can attack) -> { frozen, immune, canAttack }, or nil.
function M.ParseHeroFlags(s)
    if type(s) ~= "string" or s == "" then
        return nil
    end
    local f = { frozen = false, immune = false, canAttack = false }
    if s == "-" then
        return f
    end
    if not string.find(s, "^[fia]+$") then
        return nil
    end
    f.frozen = string.find(s, "f", 1, true) ~= nil
    f.immune = string.find(s, "i", 1, true) ~= nil
    f.canAttack = string.find(s, "a", 1, true) ~= nil
    return f
end

-- <id>.<cardId>.<health>.<maxHealth>.<armor>.<attack>.<flags>
local function parseHero(s)
    local p = split(s, ".", 8)
    if not p or table.getn(p) ~= 7 then
        return nil
    end
    local id, health, maxHealth, armor, attack = parseInt(p[1]), parseInt(p[3], true), parseInt(p[4], true),
        parseInt(p[5]), parseInt(p[6])
    local flags = M.ParseHeroFlags(p[7])
    if not id or id < 1 or not validId(p[2]) or not health or not maxHealth or not armor or not attack or not flags then
        return nil
    end
    return { id = id, cardId = p[2], health = health, maxHealth = maxHealth, armor = armor, attack = attack,
        flags = p[7], canAttack = flags.canAttack, frozen = flags.frozen, immune = flags.immune }
end

-- <id>.<cardId>.<cost>.<used 0|1>
local function parsePower(s)
    local p = split(s, ".", 8)
    if not p or table.getn(p) ~= 4 then
        return nil
    end
    local id, cost = parseInt(p[1]), parseInt(p[3])
    if not id or id < 1 or not validId(p[2]) or not cost or (p[4] ~= "0" and p[4] ~= "1") then
        return nil
    end
    return { id = id, cardId = p[2], cost = cost, used = p[4] == "1" }
end

-- "" (no weapon, returns false) or <id>.<cardId>.<attack>.<durability>
local function parseWeapon(s)
    if s == "" then
        return false
    end
    local p = split(s, ".", 8)
    if not p or table.getn(p) ~= 4 then
        return nil
    end
    local id, attack, durability = parseInt(p[1]), parseInt(p[3]), parseInt(p[4])
    if not id or id < 1 or not validId(p[2]) or not attack or not durability then
        return nil
    end
    return { id = id, cardId = p[2], attack = attack, durability = durability }
end

-- "<a>.<b>" -> a, b
local function parsePair(s)
    local p = split(s, ".", 4)
    if not p or table.getn(p) ~= 2 then
        return nil
    end
    local a, b = parseInt(p[1]), parseInt(p[2])
    if not a or not b then
        return nil
    end
    return a, b
end

-- <handCount>.<deckCount>.<fatigue>
local function parseCounts(s)
    local p = split(s, ".", 4)
    if not p or table.getn(p) ~= 3 then
        return nil
    end
    local h, d, f = parseInt(p[1]), parseInt(p[2]), parseInt(p[3])
    if not h or not d or not f then
        return nil
    end
    return h, d, f
end

-- <id>.<cardId>.<attack>.<health>.<maxHealth>.<flags>
local function parseMinion(s)
    local p = split(s, ".", 8)
    if not p or table.getn(p) ~= 6 then
        return nil
    end
    local id, attack, health, maxHealth = parseInt(p[1]), parseInt(p[3], true), parseInt(p[4], true), parseInt(p[5], true)
    local f = M.ParseFlags(p[6])
    if not id or id < 1 or not validId(p[2]) or not attack or not health or not maxHealth or not f then
        return nil
    end
    return { id = id, cardId = p[2], attack = attack, health = health, maxHealth = maxHealth, taunt = f.taunt,
        charge = f.charge, divine = f.divine, windfury = f.windfury, canAttack = f.canAttack, flags = p[6],
        stealth = f.stealth, frozen = f.frozen, silenced = f.silenced, poisonous = f.poisonous,
        enraged = f.enraged, immune = f.immune, cantAttack = f.cantAttack }
end

-- <id>.<cardId>.<cost>.<attack>.<health>
local function parseHandCard(s)
    local p = split(s, ".", 8)
    if not p or table.getn(p) ~= 5 then
        return nil
    end
    local id, cost, attack, health = parseInt(p[1]), parseInt(p[3], true), parseInt(p[4], true), parseInt(p[5], true)
    if not id or id < 1 or not validId(p[2]) or not cost or not attack or not health then
        return nil
    end
    return { id = id, cardId = p[2], cost = cost, attack = attack, health = health }
end

-- <k><source>[:<choice>].<t1>/<t2>/...   k = p (play), a (attack), h (hero power); target 0 = no target;
-- choice 1..MAX_OPTIONS only on a play entry of a Choose One card (one entry per playable option)
local function parseLegal(s)
    local _, _, k, rest = string.find(s, "^([pah])(.+)$")
    if not k then
        return nil
    end
    local p = split(rest, ".", 4)
    if not p or table.getn(p) ~= 2 then
        return nil
    end
    local head = split(p[1], ":", 4)
    if not head or table.getn(head) < 1 or table.getn(head) > 2 then
        return nil
    end
    local source = parseInt(head[1])
    local choice = nil
    if table.getn(head) == 2 then
        choice = parseInt(head[2])
        if not choice or choice < 1 or choice > M.MAX_OPTIONS or k ~= "p" then
            return nil
        end
    end
    local list = split(p[2], "/", M.MAX_LIST)
    if not source or source < 1 or not list or table.getn(list) < 1 then
        return nil
    end
    local targets = {}
    for i = 1, table.getn(list) do
        local t = parseInt(list[i])
        if not t then
            return nil
        end
        table.insert(targets, t)
    end
    return { kind = k, source = source, choice = choice, targets = targets }
end

-- <type>.<side m|o|->.<source>.<target>.<amount>.<cardId or ->
local function parseEvent(s)
    local p = split(s, ".", 8)
    if not p or table.getn(p) ~= 6 then
        return nil
    end
    local source, target, amount = parseInt(p[3]), parseInt(p[4]), parseInt(p[5], true)
    if not string.find(p[1], "^%l+$") or not ACTS[p[2]] or not source or not target or not amount then
        return nil
    end
    local cardId = nil
    if p[6] ~= "-" then
        if not validId(p[6]) then
            return nil
        end
        cardId = p[6]
    end
    return { type = p[1], side = p[2], source = source, target = target, amount = amount, cardId = cardId }
end

-- Parses a list value with 'one' for each item; nil key or "" gives an empty list.
local function parseList(value, one)
    local out = {}
    if value == nil or value == "" then
        return out
    end
    local items = split(value, ",", M.MAX_LIST)
    if not items then
        return nil
    end
    for i = 1, table.getn(items) do
        local v = one(items[i])
        if v == nil then
            return nil
        end
        table.insert(out, v)
    end
    return out
end

-- <id>.<cardId> (the viewer's own secret or a pending pick option)
local function parseIdCard(s)
    local p = split(s, ".", 4)
    if not p or table.getn(p) ~= 2 then
        return nil
    end
    local id = parseInt(p[1])
    if not id or id < 1 or not validId(p[2]) then
        return nil
    end
    return { id = id, cardId = p[2] }
end

-- <id> (an opponent secret: the card is unknown)
local function parseIdOnly(s)
    local id = parseInt(s)
    if not id or id < 1 then
        return nil
    end
    return { id = id }
end

-- <handId>.<option1>/<option2>[/...]
local function parseOptions(s)
    local p = split(s, ".", 4)
    if not p or table.getn(p) ~= 2 then
        return nil
    end
    local handId = parseInt(p[1])
    local list = split(p[2], "/", M.MAX_OPTIONS)
    if not handId or handId < 1 or not list or table.getn(list) < 1 then
        return nil
    end
    for i = 1, table.getn(list) do
        if not validId(list[i]) then
            return nil
        end
    end
    return { handId = handId, options = list }
end

-- <cardsPlayedThisTurn>.<overloadLocked>.<overloadOwed>
local function parseOverload(s)
    local p = split(s, ".", 4)
    if not p or table.getn(p) ~= 3 then
        return nil
    end
    local played, locked, owed = parseInt(p[1]), parseInt(p[2]), parseInt(p[3])
    if not played or not locked or not owed then
        return nil
    end
    return played, locked, owed
end

local function parseSide(kv, pre, post)
    local hero = parseHero(kv[pre .. "h"] or "")
    if not hero then
        return nil, "bad hero " .. pre .. "h"
    end
    local power = parsePower(kv[pre .. "p"] or "")
    if not power then
        return nil, "bad hero power " .. pre .. "p"
    end
    local weapon = parseWeapon(kv[pre .. "w"] or "")
    if weapon == nil then
        return nil, "bad weapon " .. pre .. "w"
    end
    if weapon == false then
        weapon = nil
    end
    local mana, maxMana = parsePair(kv[pre .. "m"] or "")
    if not mana then
        return nil, "bad mana " .. pre .. "m"
    end
    local handCount, deckCount, fatigue = parseCounts(kv[pre .. "k"] or "")
    if not handCount then
        return nil, "bad counts " .. pre .. "k"
    end
    local board = parseList(kv[pre .. "b"], parseMinion)
    if not board then
        return nil, "bad board " .. pre .. "b"
    end
    local secretsKey = pre .. "s"
    local secrets
    if kv[secretsKey] ~= nil then
        secrets = parseList(kv[secretsKey], pre == "m" and parseIdCard or parseIdOnly)
    end
    if not secrets then
        return nil, "bad secrets " .. secretsKey
    end
    local played, locked, owed = parseOverload(kv[pre .. "x"] or "")
    if not played then
        return nil, "bad overload " .. pre .. "x"
    end
    return { hero = hero, power = power, weapon = weapon, mana = mana, maxMana = maxMana, handCount = handCount,
        deckCount = deckCount, fatigue = fatigue, board = board, secrets = secrets, cardsPlayed = played,
        overloadLocked = locked, overloadOwed = owed }
end

-- Parses a GAME payload. Returns the game table or nil and a reason. "ph=none" gives { ph = "none" }.
function M.Parse(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = P.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    if not PHASES[kv.ph or ""] then
        return nil, "bad ph"
    end
    if kv.ph == "none" then
        return { ph = "none" }
    end
    local mid, seq, turn = parseInt(kv.mid), parseInt(kv.seq), parseInt(kv.turn)
    if not mid or mid < 1 then
        return nil, "bad mid"
    end
    if not seq then
        return nil, "bad seq"
    end
    if not turn then
        return nil, "bad turn"
    end
    if not ACTS[kv.act or ""] then
        return nil, "bad act"
    end
    if not RESULTS[kv.res or ""] then
        return nil, "bad res"
    end
    if type(kv.why) ~= "string" or not string.find(kv.why, "^[%a-]+$") then
        return nil, "bad why"
    end
    local g = { mid = mid, seq = seq, ph = kv.ph, turn = turn, act = kv.act, res = kv.res, why = kv.why,
        myName = cleanName(kv.mn), oppName = cleanName(kv.on), dif = kv.dif }
    if not M.DIFFICULTIES[kv.dif or ""] then
        return nil, "bad dif"
    end
    -- dur=1 marks a ranked ladder duel (the window's close button pauses it, the server resumes it at a chair)
    g.dur = false
    if kv.dur ~= nil and kv.dur ~= "" then
        if kv.dur ~= "1" then
            return nil, "bad dur"
        end
        g.dur = true
    end
    local why
    g.me, why = parseSide(kv, "m")
    if not g.me then
        return nil, why
    end
    g.opp, why = parseSide(kv, "o")
    if not g.opp then
        return nil, why
    end
    g.hand = parseList(kv.mc, parseHandCard)
    if not g.hand then
        return nil, "bad hand mc"
    end
    local opts = kv.mo ~= nil and parseList(kv.mo, parseOptions)
    if not opts then
        return nil, "bad options mo"
    end
    g.options = {}                    -- hand card id -> { option card id, ... }
    for i = 1, table.getn(opts) do
        g.options[opts[i].handId] = opts[i].options
    end
    g.pick = kv.pk ~= nil and parseList(kv.pk, parseIdCard)
    if not g.pick then
        return nil, "bad pick pk"
    end
    if kv.opk ~= "0" and kv.opk ~= "1" then
        return nil, "bad opk"
    end
    g.oppPicking = kv.opk == "1"
    g.timer = parseInt(kv.tmr)
    if not g.timer or g.timer > M.MAX_TIMER then
        return nil, "bad tmr"
    end
    g.mulligan = parseList(kv.mul, function(s)
        return parseInt(s)
    end)
    if not g.mulligan then
        return nil, "bad mulligan mul"
    end
    g.legal = parseList(kv.lg, parseLegal)
    if not g.legal then
        return nil, "bad legal actions lg"
    end
    g.events = parseList(kv.ev, parseEvent)
    if not g.events or table.getn(g.events) > M.MAX_EVENTS then
        return nil, "bad events ev"
    end
    g.evs = 0
    if table.getn(g.events) > 0 then
        local evs = parseInt(kv.evs)
        if not evs or evs < 1 then
            return nil, "bad evs"
        end
        g.evs = evs
        for i = 1, table.getn(g.events) do
            g.events[i].seq = evs + i - 1
        end
    elseif kv.evs ~= nil and kv.evs ~= "" and parseInt(kv.evs) == nil then
        return nil, "bad evs"
    end
    return g
end

-- True when the WELCOME caps list ("solo,chunk,packs,decks,match") contains "match".
function M.CapsHaveMatch(caps)
    if type(caps) ~= "string" then
        return false
    end
    local items = split(caps, ",", 64)
    if not items then
        return false
    end
    for i = 1, table.getn(items) do
        if items[i] == "match" then
            return true
        end
    end
    return false
end

-- ----------------------------------------------------------------- lookups
function M.IsRunning(g)
    return g ~= nil and (g.ph == "mull" or g.ph == "play")
end

-- The human's turn in a running match (an action other than the mulligan can be sent).
function M.IsMyTurn(g)
    return g ~= nil and g.ph == "play" and g.act == "m"
end

function M.CanEndTurn(g)
    return M.IsMyTurn(g)
end

-- The legal entry (kind "p", "a" or "h", source entity id), or nil. choice (optional, a Choose One
-- option 1 or 2) selects that option's entry; without it the first entry of the source is returned.
function M.LegalFor(g, kind, source, choice)
    if not g then
        return nil
    end
    for i = 1, table.getn(g.legal) do
        local e = g.legal[i]
        if e.kind == kind and e.source == source and (choice == nil or e.choice == choice) then
            return e
        end
    end
    return nil
end

-- The Choose One options of a hand card (a list of option card ids), or nil for an ordinary card.
function M.OptionsOf(g, handId)
    if g and g.options then
        return g.options[handId]
    end
    return nil
end

function M.IsChoiceCard(g, handId)
    return M.OptionsOf(g, handId) ~= nil
end

-- The play entries of a hand card by choice: { [1] = entry or nil, [2] = ... } over the card's options.
function M.ChoiceEntries(g, handId)
    local out = {}
    local opts = M.OptionsOf(g, handId)
    if not opts then
        return out
    end
    for i = 1, table.getn(opts) do
        out[i] = M.LegalFor(g, "p", handId, i)
    end
    return out
end

-- Targets (other than 0) of the play entry of source with the choice, ascending; empty when the
-- entry is missing or needs no target.
function M.TargetsFor(g, source, choice)
    return M.Targets(M.LegalFor(g, "p", source, choice))
end

-- True when the viewer must answer a pending pick before anything else.
function M.IsPicking(g)
    return g ~= nil and g.ph == "play" and g.pick ~= nil and table.getn(g.pick) > 0
end

-- True while the opponent has a pending pick (its options are hidden).
function M.OpponentPicking(g)
    return g ~= nil and g.ph == "play" and g.oppPicking == true
end

-- A pick option by entity id, or nil.
function M.FindPick(g, id)
    if g and g.pick then
        for i = 1, table.getn(g.pick) do
            if g.pick[i].id == id then
                return g.pick[i]
            end
        end
    end
    return nil
end

-- The card ids of the player's own secrets, from the view: { { id, cardId }, ... }.
function M.Secrets(g, which)
    if not g then
        return {}
    end
    local side = g.opp
    if which == "m" then
        side = g.me
    end
    return side.secrets or {}
end

-- Combo is active: it is my turn and I already played a card this turn.
function M.ComboActive(g)
    return M.IsMyTurn(g) and g.me.cardsPlayed > 0
end

-- True when the card database lists the mechanic (COMBO, DEATHRATTLE, AURA, ...) for the card.
function M.HasMechanic(cardId, mech)
    local db = HS_CardDB
    local c = nil
    if type(db) == "table" and type(db.cards) == "table" then
        c = db.cards[cardId or ""]
    end
    if c and type(c.mech) == "table" then
        for i = 1, table.getn(c.mech) do
            if c.mech[i] == mech then
                return true
            end
        end
    end
    return false
end

-- True when the card database lists the COMBO mechanic for the card.
function M.IsComboCard(cardId)
    return M.HasMechanic(cardId, "COMBO")
end

-- "Normal", "Hard", "Boss" for the difficulty of the opponent, or "" when the server sent none.
function M.DifficultyLabel(dif)
    return M.DIFFICULTIES[dif or ""] or ""
end

-- The window title: "<me> vs <opponent> (Hard)".
function M.TitleText(g)
    if not g then
        return "Hearthstone"
    end
    local text = g.myName .. " vs " .. g.oppName
    local d = M.DifficultyLabel(g.dif)
    if d ~= "" then
        text = text .. " (" .. d .. ")"
    end
    return text
end

-- The targets of an entry other than 0 (no target), ascending as the server sent them.
function M.Targets(entry)
    local out = {}
    if entry then
        for i = 1, table.getn(entry.targets) do
            if entry.targets[i] ~= 0 then
                table.insert(out, entry.targets[i])
            end
        end
    end
    return out
end

-- True when playing / using the entry needs the player to pick a target.
function M.NeedsTarget(entry)
    return table.getn(M.Targets(entry)) > 0
end

function M.IsValidTarget(entry, id)
    if not entry or id == 0 then
        return false
    end
    for i = 1, table.getn(entry.targets) do
        if entry.targets[i] == id then
            return true
        end
    end
    return false
end

function M.IsPlayable(g, handId)
    return M.LegalFor(g, "p", handId) ~= nil
end

function M.CanAttackWith(g, id)
    return M.LegalFor(g, "a", id) ~= nil
end

-- The hero power entry (its source is the hero power's entity id), or nil.
function M.PowerEntry(g)
    if not g then
        return nil
    end
    for i = 1, table.getn(g.legal) do
        if g.legal[i].kind == "h" then
            return g.legal[i]
        end
    end
    return nil
end

-- A hand card of the viewer by entity id, and its index.
function M.FindHand(g, id)
    if g then
        for i = 1, table.getn(g.hand) do
            if g.hand[i].id == id then
                return g.hand[i], i
            end
        end
    end
    return nil
end

-- A minion on either board by entity id: minion, "m" or "o", index.
function M.FindMinion(g, id)
    if g then
        for i = 1, table.getn(g.me.board) do
            if g.me.board[i].id == id then
                return g.me.board[i], "m", i
            end
        end
        for i = 1, table.getn(g.opp.board) do
            if g.opp.board[i].id == id then
                return g.opp.board[i], "o", i
            end
        end
    end
    return nil
end

-- Which character an entity id is: "hero", "minion" or nil, and the side "m" / "o".
function M.WhoIs(g, id)
    if not g then
        return nil
    end
    if g.me.hero.id == id then
        return "hero", "m"
    end
    if g.opp.hero.id == id then
        return "hero", "o"
    end
    local mn, side = M.FindMinion(g, id)
    if mn then
        return "minion", side
    end
    return nil
end

-- ------------------------------------------------------------ GAME_ACT payloads
local function actBase(g, a)
    return { { "mid", tostring(g.mid) }, { "seq", tostring(g.seq) }, { "a", a } }
end

-- Mulligan: the offered card ids to replace (may be empty), in the order given.
function M.ActMulligan(g, ids)
    if not g then
        return nil
    end
    local list = actBase(g, "mull")
    local strs = {}
    for i = 1, table.getn(ids or {}) do
        strs[i] = tostring(ids[i])
    end
    table.insert(list, { "r", table.concat(strs, ",") })
    return P.KvEncode(list)
end

-- Play a hand card. target 0 / nil: none; position nil / -1: rightmost (the server default); choice
-- nil, or 1 / 2 for the option of a Choose One card (key c, after s, t and p).
function M.ActPlay(g, source, target, position, choice)
    if not g then
        return nil
    end
    local list = actBase(g, "play")
    table.insert(list, { "s", tostring(source) })
    if target and target ~= 0 then
        table.insert(list, { "t", tostring(target) })
    end
    if position and position >= 0 then
        table.insert(list, { "p", tostring(position) })
    end
    if choice and choice > 0 then
        table.insert(list, { "c", tostring(choice) })
    end
    return P.KvEncode(list)
end

-- Answers a pending pick with the entity id of one of the options (pk).
function M.ActPick(g, optionId)
    if not g or not optionId then
        return nil
    end
    local list = actBase(g, "pick")
    table.insert(list, { "s", tostring(optionId) })
    return P.KvEncode(list)
end

function M.ActAttack(g, source, target)
    if not g then
        return nil
    end
    local list = actBase(g, "atk")
    table.insert(list, { "s", tostring(source) })
    table.insert(list, { "t", tostring(target) })
    return P.KvEncode(list)
end

function M.ActHeroPower(g, target)
    if not g then
        return nil
    end
    local list = actBase(g, "hp")
    if target and target ~= 0 then
        table.insert(list, { "t", tostring(target) })
    end
    return P.KvEncode(list)
end

function M.ActEnd(g)
    if not g then
        return nil
    end
    return P.KvEncode(actBase(g, "end"))
end

function M.ActConcede(g)
    if not g then
        return nil
    end
    return P.KvEncode(actBase(g, "concede"))
end

-- GAME_LEAVE payload.
function M.LeavePayload(g)
    if not g or not g.mid then
        return nil
    end
    return P.KvEncode({ { "mid", tostring(g.mid) } })
end

-- ------------------------------------------------------------------ texts
local REFUSED = {
    over = "The match is already over.",
    phase = "That is not possible in this phase of the match.",
    turn = "It is not your turn.",
    source = "That card or character cannot be used.",
    mana = "Not enough mana.",
    target = "That is not a valid target.",
    board = "Your board is full.",
    attack = "That minion cannot attack now.",
    taunt = "A minion with Taunt must be attacked first.",
    used = "You already used your hero power this turn.",
    requirement = "That card cannot be played now (its requirement is not met).",
    mulligan = "That mulligan choice is not valid.",
    position = "That board position is not valid.",
    unsupported = "That card is not supported yet.",
    stale = "The board changed; it was refreshed.",
    match = "That match is not running any more.",
    active = "The match is still running.",
    ordinary = "Only an arena duel can be paused.",
    choice = "That card needs a valid choice of its options.",
    pending = "Choose one of the offered cards first.",
    secret = "You cannot play that secret (you already have it, or have too many).",
}

-- Text for ERR REFUSED why=<code> of a match request.
function M.RefusedText(why, text)
    local t = REFUSED[why or ""]
    if t then
        return t
    end
    if text and text ~= "" then
        return text
    end
    return "The server refused that."
end

function M.ResultText(g)
    if not g then
        return ""
    end
    if g.res == "win" then
        return "Victory"
    elseif g.res == "loss" then
        return "Defeat"
    elseif g.res == "draw" then
        return "Draw"
    elseif g.res == "int" then
        return "Interrupted"
    end
    return ""
end

-- The readable reason of a finished match (from the viewer's point of view).
function M.ReasonText(g)
    if not g or g.ph ~= "over" then
        return ""
    end
    local why, res = g.why, g.res
    local opp = g.oppName
    if opp == nil or opp == "" then
        opp = "Your opponent"
    end
    if why == "hero" then
        if res == "win" then
            return opp .. " was defeated."
        elseif res == "loss" then
            return "Your hero was defeated."
        end
        return "Both heroes were defeated."
    elseif why == "concede" then
        if res == "win" then
            return opp .. " surrendered."
        end
        return "You surrendered."
    elseif why == "turns" then
        return "The match reached the turn limit."
    elseif why == "stuck" then
        return "You used Help > Stuck, which ends the match."
    elseif why == "teleport" then
        return "You left the area (teleport or zone change)."
    elseif why == "death" then
        return "Your character died."
    elseif why == "npc" then
        return opp .. " is gone (removed or killed)."
    elseif why == "gm" then
        return "A game master ended the match."
    elseif why == "error" then
        return "The match ended because of a server error."
    end
    return "The match is over."
end

-- Name of a card id from the card database (the id itself when unknown).
function M.CardName(cardId)
    if cardId == nil then
        return nil
    end
    local db = HS_CardDB
    local c = nil
    if type(db) == "table" and type(db.cards) == "table" then
        c = db.cards[cardId]
    end
    if c and c.name then
        return c.name
    end
    return cardId
end

-- Rules text of a card from the card database (with its colour codes), or nil for an unknown id.
function M.CardText(cardId)
    local db = HS_CardDB
    local c = nil
    if type(db) == "table" and type(db.cards) == "table" then
        c = db.cards[cardId or ""]
    end
    if c and c.text and c.text ~= "" then
        return c.text
    end
    return nil
end

-- Base values of a card from the card database: cost, attack, health (durability for weapons),
-- type, or nil for an unknown id.
function M.CardBase(cardId)
    local db = HS_CardDB
    local c = nil
    if type(db) == "table" and type(db.cards) == "table" then
        c = db.cards[cardId]
    end
    if not c then
        return nil
    end
    local health = c.hp
    if c.type == "WEAPON" then
        health = c.dur
    end
    return { cost = c.cost, attack = c.atk, health = health, type = c.type, name = c.name }
end

-- Which numbers of a hand card differ from the card database: costDiff, attackDiff, healthDiff
-- each nil (shown as on the card image), "up" or "down" (more / less than the printed value).
function M.HandOverlay(card)
    local base = M.CardBase(card.cardId)
    if not base then
        return nil, nil, nil
    end
    local function diff(now, was)
        if was == nil or now == was then
            return nil
        end
        if now > was then
            return "up"
        end
        return "down"
    end
    local cost, attack, health = diff(card.cost, base.cost), nil, nil
    if base.type == "MINION" or base.type == "WEAPON" then
        attack = diff(card.attack, base.attack)
        health = diff(card.health, base.health)
    end
    return cost, attack, health
end

-- ---------------------------------------------------------------- state
local function clearMatchData()
    M.log = {}
    M.history = {}
    M.historyOpen = nil
    M.logSeq = 0
    M.cards = {}
    M.heroNames = {}
    M.marked = {}
    M.pending = nil
    M.message = nil
    M.timerLeft = nil
    M.transition = nil
    M.lastEventSeq = 0
    M.eventFloor = 0
end

-- Not connected (or no match support): nothing is known.
function M.Reset()
    M.state = "none"
    M.game = nil
    M.mid = nil
    M.errorText = nil
    clearMatchData()
end

-- A GAME_GET is on its way. A refetch while a match is known keeps the board on screen.
function M.SetLoading()
    if M.game == nil then
        M.state = "loading"
        M.errorText = nil
    end
end

-- The GAME could not be read, or the request failed. A known match stays on screen.
function M.SetError(text)
    if M.game == nil then
        M.state = "error"
        M.errorText = text
    else
        M.message = text
    end
end

local function stateOf(g)
    if g.ph == "mull" or g.ph == "play" or g.ph == "over" then
        return g.ph
    end
    return "none"
end

-- Notes which card an entity id is (the board and hand of the view, cards named in events). Entities
-- stay known after they left the board, so the log can still name a minion that died.
local function learn(g)
    M.heroNames[g.me.hero.id] = g.myName
    M.heroNames[g.opp.hero.id] = g.oppName
    M.cards[g.me.hero.id] = g.me.hero.cardId
    M.cards[g.opp.hero.id] = g.opp.hero.cardId
    local sides = { g.me, g.opp }
    for s = 1, 2 do
        M.cards[sides[s].power.id] = sides[s].power.cardId
        if sides[s].weapon then
            M.cards[sides[s].weapon.id] = sides[s].weapon.cardId
        end
        for i = 1, table.getn(sides[s].board) do
            M.cards[sides[s].board[i].id] = sides[s].board[i].cardId
        end
    end
    for i = 1, table.getn(g.hand) do
        M.cards[g.hand[i].id] = g.hand[i].cardId
    end
    for i = 1, table.getn(g.me.secrets) do
        M.cards[g.me.secrets[i].id] = g.me.secrets[i].cardId
    end
    for i = 1, table.getn(g.pick) do
        M.cards[g.pick[i].id] = g.pick[i].cardId
    end
end

-- The name of an entity for the log: the player's name for a hero, else the card name. names (optional)
-- maps entity ids to the card they were at the time of the event (a transformed minion keeps its old name
-- for the events before the transformation).
local function label(id, g, names)
    if id == 0 then
        return nil
    end
    if M.heroNames[id] and M.heroNames[id] ~= "" then
        return M.heroNames[id]
    end
    local cardId = (names and names[id]) or M.cards[id]
    if cardId then
        return M.CardName(cardId)
    end
    return nil
end

local function who(side, g)
    if side == "m" then
        return "You"
    elseif side == "o" then
        if g.oppName ~= "" then
            return g.oppName
        end
        return "Your opponent"
    end
    return "Someone"
end

-- "You" and "the opponent" agree with their verbs: "You play", "Allison plays".
local function verb(side, you, other)
    if side == "m" then
        return you
    end
    return other
end

-- "Your" / "Allison's" for a possessive in front of a noun.
local function possessive(side, g)
    if side == "m" then
        return "Your"
    elseif side == "o" then
        if g.oppName ~= "" then
            return g.oppName .. "'s"
        end
        return "Your opponent's"
    end
    return "A"
end

local SIDE_COLOR = { m = "|cff99ff99", o = "|cffff9999" }

-- Event types whose cardId names the target entity / the source entity (learned for the log).
local TARGET_NAMED = { draw = true, burn = true, sum = true, eq = true, tf = true, gen = true, bnc = true,
    disc = true, pick = true, sec = true, rev = true }
local SOURCE_NAMED = { play = true, hp = true, trig = true, dr = true, cnt = true }

-- One log line (coloured) for an event, or nil when there is nothing to show. 'g' is the game the
-- event came with; names (optional) as for label().
function M.EventText(e, g, names)
    local t = e.type
    local text = nil
    local name = M.CardName(e.cardId)
    local target = label(e.target, g, names)
    local source = label(e.source, g, names)
    if t == "mull" then
        if e.amount == 0 then
            text = who(e.side, g) .. " " .. verb(e.side, "keep", "keeps") .. " every card."
        else
            text = who(e.side, g) .. " " .. verb(e.side, "replace", "replaces") .. " " .. e.amount
                .. " card" .. (e.amount == 1 and "" or "s") .. "."
        end
    elseif t == "turn" then
        if e.side == "m" then
            text = "|cffffd100Turn " .. e.amount .. ": your turn.|r"
        else
            text = "|cffffd100Turn " .. e.amount .. ": " .. who(e.side, g) .. (e.side == "o" and "'s" or "") .. " turn.|r"
        end
    elseif t == "draw" then
        if name then
            text = who(e.side, g) .. " " .. verb(e.side, "draw", "draws") .. " " .. name .. "."
        else
            text = who(e.side, g) .. " " .. verb(e.side, "draw", "draws") .. " a card."
        end
    elseif t == "burn" then
        text = who(e.side, g) .. " " .. verb(e.side, "draw", "draws") .. " " .. (name or "a card")
            .. " but the hand is full: it is destroyed."
    elseif t == "fat" then
        text = (target or who(e.side, g)) .. " takes " .. e.amount .. " fatigue damage."
    elseif t == "play" then
        text = who(e.side, g) .. " " .. verb(e.side, "play", "plays") .. " " .. (name or source or "a card")
        if e.target ~= 0 and target then
            text = text .. " on " .. target
        end
        text = text .. "."
    elseif t == "sum" then
        text = (name or target or "A minion") .. " joins the board."
    elseif t == "atk" then
        text = (source or "A character") .. " attacks " .. (target or "a character") .. "."
    elseif t == "dmg" then
        text = (target or "A character") .. " takes " .. e.amount .. " damage."
    elseif t == "heal" then
        text = (target or "A character") .. " is healed for " .. e.amount .. "."
    elseif t == "arm" then
        text = who(e.side, g) .. " " .. verb(e.side, "gain", "gains") .. " " .. e.amount .. " armor."
    elseif t == "pop" then
        text = (target or "A minion") .. " loses its Divine Shield."
    elseif t == "die" then
        text = (target or "A minion") .. " dies."
    elseif t == "eq" then
        text = who(e.side, g) .. " " .. verb(e.side, "equip", "equips") .. " " .. (name or target or "a weapon") .. "."
    elseif t == "wbrk" then
        text = (target or "A weapon") .. " breaks."
    elseif t == "tf" then
        text = (target or "A minion") .. " becomes " .. (name or "something else") .. "."
    elseif t == "buff" then
        if e.amount > 0 then
            text = (target or "A minion") .. " gets +" .. e.amount .. " attack."
        elseif e.amount < 0 then
            text = (target or "A minion") .. " gets " .. e.amount .. " attack."
        else
            text = (target or "A minion") .. " is changed."
        end
    elseif t == "hp" then
        text = who(e.side, g) .. " " .. verb(e.side, "use", "uses") .. " " .. (name or source or "the hero power")
        if e.target ~= 0 and target then
            text = text .. " on " .. target
        end
        text = text .. "."
    elseif t == "mana" then
        text = who(e.side, g) .. " " .. verb(e.side, "gain", "gains") .. " " .. e.amount .. " mana this turn."
    elseif t == "sil" or t == "frz" then
        -- "Frostbolt freezes Chillwind Yeti." when the source is known and is another entity
        local actor = nil
        if e.source ~= 0 and e.source ~= e.target then
            actor = source
        end
        if t == "sil" then
            if actor then
                text = actor .. " silences " .. (target or "a minion") .. "."
            else
                text = (target or "A minion") .. " is silenced."
            end
        elseif actor then
            text = actor .. " freezes " .. (target or "a character") .. "."
        else
            text = (target or "A character") .. " is frozen."
        end
    elseif t == "sec" then
        text = who(e.side, g) .. " " .. verb(e.side, "set", "sets") .. " a Secret"
        if name then
            text = text .. ": " .. name
        end
        text = text .. "."
    elseif t == "rev" then
        text = possessive(e.side, g) .. " Secret is revealed"
        if name then
            text = text .. ": " .. name
        end
        text = text .. "."
    elseif t == "trig" then
        text = (name or source or "An ability") .. " triggers."
    elseif t == "dr" then
        text = (name or source or "A minion") .. "'s Deathrattle triggers."
    elseif t == "disc" then
        text = who(e.side, g) .. " " .. verb(e.side, "discard", "discards") .. " " .. (name or "a card") .. "."
    elseif t == "bnc" then
        local hand = "your hand"
        if e.side == "o" then
            hand = who(e.side, g) .. "'s hand"
        elseif e.side ~= "m" then
            hand = "a hand"
        end
        text = (name or target or "A minion") .. " returns to " .. hand .. "."
    elseif t == "ctl" then
        text = who(e.side, g) .. " " .. verb(e.side, "take", "takes") .. " control of " .. (name or target or "a minion") .. "."
    elseif t == "ovl" then
        text = who(e.side, g) .. " " .. verb(e.side, "overload", "overloads") .. " " .. e.amount .. " mana crystal"
            .. (e.amount == 1 and "" or "s") .. "."
    elseif t == "gen" then
        if name then
            text = who(e.side, g) .. " " .. verb(e.side, "get", "gets") .. " " .. name .. "."
        else
            text = who(e.side, g) .. " " .. verb(e.side, "get", "gets") .. " a card."
        end
    elseif t == "dest" then
        text = (target or name or "A character") .. " is destroyed."
    elseif t == "cnt" then
        text = (name or source or "A spell") .. " is countered."
    elseif t == "pick" then
        text = who(e.side, g) .. " " .. verb(e.side, "pick", "picks") .. " " .. (name or "a card") .. "."
    elseif t == "crys" then
        if e.amount < 0 then
            text = who(e.side, g) .. " " .. verb(e.side, "lose", "loses") .. " " .. (0 - e.amount) .. " mana crystal"
                .. (e.amount == -1 and "" or "s") .. "."
        else
            text = who(e.side, g) .. " " .. verb(e.side, "gain", "gains") .. " " .. e.amount .. " mana crystal"
                .. (e.amount == 1 and "" or "s") .. "."
        end
    elseif t == "stl" then
        text = (target or "A minion") .. " loses Stealth."
    elseif t == "end" then
        text = "The match ends."
    else
        -- an event code this addon does not know (a newer server): a generic line instead of nothing
        text = "Something happens (" .. tostring(t) .. ")"
        local named = name or source or target
        if named then
            text = text .. ": " .. named
        end
        text = text .. "."
    end
    if not text then
        return nil
    end
    if t == "turn" then
        return text
    end
    local c = SIDE_COLOR[e.side]
    if c then
        return c .. text .. "|r"
    end
    return text
end

-- The history tiles (the column of card pictures beside the board, like Hearthstone's own log). A tile starts at
-- an event that something does: a card played, a hero power used, an attack, a trigger / deathrattle, a secret
-- revealed. The log lines of the events that follow (damage, deaths, draws, ...) belong to that tile and are its hover
-- text, until the next tile or the next turn starts. Events of no tile (the turn change, the draws and the mana of a
-- turn's start) stay in M.log only.
local TILE_START = { play = true, hp = true, atk = true, trig = true, dr = true, rev = true }
local TILE_ENDS = { turn = true, ["end"] = true }

local function entityCard(id, names)
    if id == nil or id == 0 then
        return nil
    end
    return (names and names[id]) or M.cards[id]
end

-- Adds the log line of event e to the history: a new tile for a starting event, else a line of the open tile.
local function track(e, line, names)
    local t = e.type
    if TILE_ENDS[t] then
        M.historyOpen = nil
        return
    end
    if TILE_START[t] then
        local cardId = nil
        if t == "atk" or t == "trig" or t == "dr" then
            cardId = e.cardId or entityCard(e.source, names)
        else
            cardId = e.cardId
        end
        if not cardId then
            M.historyOpen = nil
            return
        end
        local tile = { kind = t, side = e.side, cardId = cardId, lines = { line } }
        if e.target ~= 0 and (t == "play" or t == "hp" or t == "atk") then
            tile.targetCardId = entityCard(e.target, names)
        end
        table.insert(M.history, tile)
        M.historyOpen = tile
        while table.getn(M.history) > M.MAX_HISTORY do
            table.remove(M.history, 1)
        end
        return
    end
    local open = M.historyOpen
    if open and table.getn(open.lines) < M.MAX_TILE_LINES then
        table.insert(open.lines, line)
    end
end

-- The newest n tiles that fit, oldest first (the column fills from the top, the newest tile is the lowest).
function M.HistoryTail(n)
    local out = {}
    local first = table.getn(M.history) - n + 1
    if first < 1 then
        first = 1
    end
    for i = first, table.getn(M.history) do
        table.insert(out, M.history[i])
    end
    return out
end

-- Learns the cards the events name, then logs the events not logged before. before: what M.cards knew
-- before this GAME; the log names each entity as it was at its event. Returns the number of lines added.
local function addEvents(g, before)
    local added = 0
    local prev = nil
    local names = {}
    for id, cardId in pairs(before or {}) do
        names[id] = cardId
    end
    -- names first: a minion that is summoned and killed within one batch is gone from the view
    for i = 1, table.getn(g.events) do
        local e = g.events[i]
        if e.cardId then
            if SOURCE_NAMED[e.type] then
                M.cards[e.source] = e.cardId
            elseif TARGET_NAMED[e.type] then
                M.cards[e.target] = e.cardId
            end
        end
    end
    for i = 1, table.getn(g.events) do
        local e = g.events[i]
        if e.seq > M.logSeq then
            M.logSeq = e.seq
            local skip = false
            -- a minion that is played enters the board right after: one line is enough
            if e.type == "sum" and prev and prev.type == "play" and prev.cardId == e.cardId and prev.side == e.side then
                skip = true
            end
            if not skip then
                local line = M.EventText(e, g, names)
                if line then
                    table.insert(M.log, line)
                    track(e, line, names)
                    added = added + 1
                end
            end
        end
        -- from here on the entity is this card (a summon, a draw, a transformation)
        if e.cardId and TARGET_NAMED[e.type] then
            names[e.target] = e.cardId
        elseif e.cardId and SOURCE_NAMED[e.type] then
            names[e.source] = e.cardId
        end
        prev = e
    end
    while table.getn(M.log) > M.MAX_LOG do
        table.remove(M.log, 1)
    end
    return added
end

-- The transition of a GAME that is applied now (see M.transition): prev is the game it replaces (nil when
-- the match is new to this client). Events are deduplicated by their seq: only those above the newest
-- seq handed out before are new, so an equal or older GAME replays nothing.
-- resync = the reply to a GAME_GET (the whole board, no story): it snaps even when nothing changed. A GAME of
-- the seq already shown without fresh events (a duplicate push) is not a change: it records no transition, so a
-- running animation goes on (the pending transition, if any, just points at the newer object).
local function makeTransition(prev, g, resync)
    local last = M.lastEventSeq or 0
    local floor = M.eventFloor or 0
    local fresh = {}
    for i = 1, table.getn(g.events) do
        if g.events[i].seq > last and g.events[i].seq > floor then
            table.insert(fresh, g.events[i])
            M.lastEventSeq = g.events[i].seq
        end
    end
    if prev and prev.seq == g.seq and table.getn(fresh) == 0 and not resync then
        local pending = M.transition
        if pending and pending.mid == g.mid then
            pending.game = g
            return pending
        end
        return nil
    end
    local tr = { mid = g.mid, prev = prev, game = g, events = fresh, lastSeen = last, snap = false }
    local old = M.transition
    if old and old.mid == g.mid then
        -- the previous change was not taken yet: one change from its start to this game
        local merged = {}
        for i = 1, table.getn(old.events) do
            table.insert(merged, old.events[i])
        end
        for i = 1, table.getn(fresh) do
            table.insert(merged, fresh[i])
        end
        while table.getn(merged) > M.MAX_TRANSITION_EVENTS do
            table.remove(merged, 1)
        end
        tr.prev = old.prev
        tr.events = merged
        tr.lastSeen = old.lastSeen
        if old.snap then
            tr.snap = true
            tr.reason = old.reason
        end
    end
    if not tr.snap then
        if tr.prev == nil then
            tr.snap = true
            tr.reason = "first"
        elseif tr.prev.ph == "mull" and g.ph ~= "mull" then
            tr.snap = true
            tr.reason = "mulligan"    -- the mulligan panel is drawn from marks that are pruned by now
        elseif table.getn(fresh) == 0 then
            tr.snap = true
            tr.reason = "resync"      -- no new events (GAME_GET, or nothing new): the whole board, no story
        end
    end
    return tr
end

-- Takes a parsed GAME. A GAME of an older seq of the same match is ignored (returns false); a
-- GAME with ph=none forgets the match. Returns true when the view changed. M.game is always the newest
-- server truth; the change is recorded in M.transition for the animation layer. resync = true for
-- the reply to a GAME_GET.
function M.Apply(g, resync)
    if g.ph == "none" then
        M.game = nil
        M.mid = nil
        M.state = "none"
        M.errorText = nil
        clearMatchData()
        return true
    end
    if M.game and M.mid == g.mid and g.seq < M.game.seq then
        return false
    end
    local prev = nil
    if M.game and M.mid == g.mid then
        prev = M.game
    end
    if M.mid ~= g.mid then
        clearMatchData()
        M.mid = g.mid
    end
    M.errorText = nil
    local before = {}
    for id, cardId in pairs(M.cards) do
        before[id] = cardId
    end
    learn(g)
    addEvents(g, before)
    M.transition = makeTransition(prev, g, resync)
    if resync and table.getn(g.events) == 0 then
        -- a jump ahead without its events: what was seen is no reference for the next batch's gap check
        if (M.lastEventSeq or 0) > (M.eventFloor or 0) then
            M.eventFloor = M.lastEventSeq
        end
        M.lastEventSeq = 0
    end
    -- the mulligan marks only make sense while the offer is open
    local still = {}
    for i = 1, table.getn(g.mulligan) do
        if M.marked[g.mulligan[i]] then
            still[g.mulligan[i]] = true
        end
    end
    M.marked = still
    if g.ph == "over" then
        M.pending = nil
    end
    M.game = g
    M.state = stateOf(g)
    -- Every snapshot restarts the local countdown from the server's value
    if g.ph == "play" and g.timer and g.timer > 0 then
        M.timerLeft = g.timer
    else
        M.timerLeft = nil
    end
    return true
end

-- The change of M.game since the last call (see M.transition), or nil. Returned once.
function M.TakeTransition()
    local tr = M.transition
    M.transition = nil
    return tr
end

-- ---------------------------------------------------------- turn timer
-- Lowers the local countdown by dt seconds (called from the addon's OnUpdate). The server ends the turn
-- itself; at 0 the countdown just waits for the next snapshot.
function M.TimerTick(dt)
    if M.timerLeft and type(dt) == "number" and dt > 0 then
        M.timerLeft = M.timerLeft - dt
        if M.timerLeft < 0 then
            M.timerLeft = 0
        end
    end
end

-- Whole seconds left (rounded up), or nil when there is no timer.
function M.TimerSeconds()
    if M.timerLeft == nil then
        return nil
    end
    return math.ceil(M.timerLeft)
end

-- True when the countdown is below M.TIMER_LOW seconds (shown in red).
function M.TimerLow()
    return M.timerLeft ~= nil and M.timerLeft < M.TIMER_LOW
end

-- ---------------------------------------------------------- mulligan marks
-- Offered card ids of the pending mulligan, or an empty list.
function M.Offered()
    if M.game and M.game.ph == "mull" then
        return M.game.mulligan
    end
    return {}
end

-- Marks / unmarks an offered card for replacement. Returns true when it is marked now.
function M.ToggleMark(id)
    local offered = M.Offered()
    for i = 1, table.getn(offered) do
        if offered[i] == id then
            if M.marked[id] then
                M.marked[id] = nil
                return false
            end
            M.marked[id] = true
            return true
        end
    end
    return false
end

-- The marked cards in offer order.
function M.MarkedList()
    local out = {}
    local offered = M.Offered()
    for i = 1, table.getn(offered) do
        if M.marked[offered[i]] then
            table.insert(out, offered[i])
        end
    end
    return out
end

-- ----------------------------------------------------------- action in flight
-- Why no action can be sent right now, or nil.
function M.ActBlocker()
    if not M.game then
        return "No match is running."
    end
    if M.game.ph == "over" then
        return "The match is over."
    end
    if M.pending then
        return "The last action is still being sent."
    end
    return nil
end

-- Records the action in flight (kind: mull, play, atk, hp, end or concede).
function M.BeginAct(kind, payload)
    M.pending = { kind = kind, payload = payload, mid = M.game and M.game.mid, seq = M.game and M.game.seq }
    M.message = nil
    return M.pending
end

function M.ClearAct()
    M.pending = nil
end

-- --------------------------------------------------------------- log view
-- The last log lines that fit: at most maxRows rows, a line counting as ceil(visible length /
-- charsPerRow) rows. Returns a list oldest first.
function M.LogTail(maxRows, charsPerRow)
    local out = {}
    local rows = 0
    for i = table.getn(M.log), 1, -1 do
        local plain = string.gsub(string.gsub(M.log[i], "|c%x%x%x%x%x%x%x%x", ""), "|r", "")
        local n = math.floor((string.len(plain) + charsPerRow - 1) / charsPerRow)
        if n < 1 then
            n = 1
        end
        if rows + n > maxRows then
            break
        end
        rows = rows + n
        table.insert(out, 1, M.log[i])
    end
    return out
end

-- ----------------------------------------------------------------- status
-- One line for /hs status.
function M.StatusText()
    if not M.supported then
        return "match: not supported by this server"
    end
    local s = M.state
    local g = M.game
    if s == "loading" then
        return "match: loading"
    elseif s == "error" then
        return "match: error (" .. tostring(M.errorText or "?") .. ")"
    elseif s == "none" or not g then
        return "match: none running"
    end
    local text = "match #" .. g.mid .. ": "
    if s == "over" then
        text = text .. "over, " .. M.ResultText(g) .. " (" .. g.why .. ")"
    elseif s == "mull" then
        text = text .. "mulligan"
    else
        local who = "opponent's turn"
        if g.act == "m" then
            who = "your turn"
        end
        text = text .. "turn " .. g.turn .. ", " .. who
    end
    text = text .. ", " .. g.me.hero.health .. " vs " .. g.opp.hero.health .. " health, seq " .. g.seq
    if M.pending then
        text = text .. ", action in flight (" .. M.pending.kind .. ")"
    end
    return text
end

-- A line for the minimap tooltip, or nil when no match is running.
function M.TooltipLine()
    local g = M.game
    if not M.IsRunning(g) then
        return nil
    end
    if g.ph == "mull" then
        return "Match: mulligan"
    end
    if g.act == "m" then
        return "Match: your turn " .. g.turn
    end
    return "Match: opponent's turn " .. g.turn
end

if M.state == nil then
    M.supported = false
    M.Reset()
end
