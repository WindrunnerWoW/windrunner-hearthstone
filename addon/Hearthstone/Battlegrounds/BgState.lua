-- Hearthstone addon: Warsong Battlegrounds client state.
--
-- Owns the connection of the mode: the outbound queue (the WRBG prefix, drained on the frame clock), the inbound
-- reassembler, the last BG_STATE / BG_COMBAT / BG_END, the one action in flight, and the texts the windows show. No
-- WoW frames here (the tests drive it from a plain interpreter); BgShopUI.lua builds the window and the driver frame
-- and calls HsBg.Tick on the frame clock.
--
--   HsBg.state      "none" (no game) | "loading" (a BG_GET is out) | "ready" | "ended" (the result screen)
--   HsBg.model      the last BG_STATE (BgProtocol.EmptyState() before the first one)
--   HsBg.combat     the human's fight of the last round, until the UI takes it (HsBg.TakeCombat)
--   HsBg.endResult  the last BG_END
--   HsBg.op         the action not confirmed yet, or nil: { action, reqId, inFlight }
--   HsBg.message    the last line for the window, or nil
--   HsBg.dirty      set on every change; the driver refreshes the window when it is set
--
-- The server owns the game: the addon sends actions and ids and replaces the model on every BG_STATE, never merges.
-- Lua 5.0 / WoW 1.12: no '#', no select(), no '...', no '%'.

HsBg = HsBg or {}
local BG = HsBg
local P = HsProtocol
local BP = HsBgProtocol

BG.SEND_INTERVAL = 0.12       -- seconds between outbound frames (the server rate limits WRBG)
BG.TIMEOUT = 10               -- seconds until an unanswered request is handled
BG.RETRY_GET = 2              -- seconds before a lost BG_GET is sent again
BG.GET_TRIES = 3              -- unanswered BG_GETs before "loading" gives up (the server has the mode off)
BG.MAX_QUEUE = 128

-- Rewritten by the tests; in game it goes out on the GUILD channel with the WRBG prefix.
function BG.Transmit(frame)
    if SendAddonMessage then
        SendAddonMessage(BP.PREFIX, frame, "GUILD")
    end
end

local function now()
    -- The frame clock: BG.Tick accumulates the frame delta into BG.clock. Deterministic and immune to a client without
    -- GetTime (the tests drive it the same way).
    return BG.clock or 0
end

-- ------------------------------------------------------------------ lifecycle
function BG.NewReassembler()
    return P.NewReassembler({ maxBytes = 65536, maxParts = P.MAX_PARTS_LIMIT, timeout = 15, maxPending = 8 })
end

function BG.Reset()
    BG.state = "none"
    BG.errorText = nil
    BG.model = BP.EmptyState()
    BG.combat = nil
    BG.endResult = nil
    BG.op = nil
    BG.message = nil
    BG.giftAt = nil
    BG.wheelQueue = {}
    BG.ally = nil
    BG.peekSeat = nil
    BG.getReqId = nil
    BG.getIn = nil
    BG.getTries = 0
    BG.refetch = nil
    BG.queue = {}
    BG.pending = {}
    BG.reasm = BG.reasm or BG.NewReassembler()
    BG.reasm:Clear()
    BG.clock = 0
    BG.sinceSend = 1
    BG.dirty = true
end

function BG.SetError(text)
    BG.state = BG.state == "ready" and "ready" or "none"
    BG.errorText = text
    BG.message = text
    BG.dirty = true
end

function BG.IsReady()
    return BG.state == "ready" or BG.state == "ended"
end

function BG.IsBusy()
    return BG.op ~= nil and BG.op.inFlight == true
end

function BG.Over()
    return BG.state == "ended" or (BG.model and BG.model.ph == "o") or false
end

-- The hero-pick phase is open (before round 1).
function BG.Picking()
    return BG.model ~= nil and BG.model.ph == "p"
end

-- The quest-pick phase is open (at the start of turn 4, after the hero pick).
function BG.PickingQuest()
    return BG.model ~= nil and BG.model.ph == "q"
end

if not BG.state then
    BG.reasm = BG.NewReassembler()
    BG.reqCounter = 0
    BG.Reset()
end

local BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz"

function BG.NextReqId()
    if not BG.reqCounter or BG.reqCounter == 0 then
        if math.randomseed then
            math.randomseed(math.floor(now() * 1000) + 7)
        end
        BG.reqCounter = math.random(1, 1000000)
    end
    BG.reqCounter = BG.reqCounter + 1
    local n = BG.reqCounter
    local s = ""
    while n > 0 do
        local d = n - math.floor(n / 36) * 36
        s = string.sub(BASE36, d + 1, d + 1) .. s
        n = math.floor(n / 36)
    end
    return s
end

-- ------------------------------------------------------------------ outbound
function BG.Request(op, payload, meta)
    local reqId = BG.NextReqId()
    local frames = P.ChunkPayload(op, reqId, payload)
    if not frames then
        return nil
    end
    -- all parts or none: a request cut off in the middle would hold a slot of the server's reassembler
    if table.getn(BG.queue) + table.getn(frames) > BG.MAX_QUEUE then
        return nil
    end
    for i = 1, table.getn(frames) do
        table.insert(BG.queue, frames[i])
    end
    meta = meta or {}
    meta.op = op
    meta.sentAt = now()
    BG.pending[reqId] = meta
    return reqId
end

-- Asks the server for the state. Safe to call at any time; one is in flight at most (a call while one is out marks a
-- refetch). A reply with NotReady means no game is open.
function BG.Get()
    if BG.getReqId then
        BG.refetch = true
        return BG.getReqId
    end
    local reqId = BG.Request(BP.OPS.get, "", { kind = "get" })
    if not reqId then
        BG.SetError("The request did not fit into the protocol limits.")
        return nil
    end
    BG.getReqId = reqId
    if BG.state ~= "ready" and BG.state ~= "ended" then
        BG.state = "loading"
    end
    BG.dirty = true
    return reqId
end

function BG.clearOpIf(reqId)
    if BG.op and BG.op.reqId == reqId then
        BG.op = nil
    end
    if BG.getReqId == reqId then
        BG.getReqId = nil
    end
    BG.pending[reqId] = nil
end

-- Sends one action. Returns the reqId, or nil and a short reason.
function BG.Act(action)
    if BG.op and BG.op.inFlight then
        return nil, "busy"
    end
    if BG.state ~= "ready" then
        return nil, "not ready"
    end
    local payload = BP.EncodeAction(action)
    if not payload then
        return nil, "bad action"
    end
    local reqId = BG.Request(BP.OPS.act, payload, { kind = "act" })
    if not reqId then
        BG.message = "The request did not fit into the protocol limits."
        BG.dirty = true
        return nil, "request"
    end
    BG.op = { action = action, reqId = reqId, inFlight = true }
    BG.dirty = true
    return reqId
end

function BG.Buy(index) return BG.Act({ type = "buy", index = index }) end
-- The hero pick, the hero power and the send. The quest pick.
function BG.ChooseHero(index) return BG.Act({ type = "hero", index = index }) end
function BG.ChooseQuest(index) return BG.Act({ type = "quest", index = index }) end
function BG.UsePower(uid, offer, uid2) return BG.Act({ type = "power", uid = uid or 0, uid2 = uid2 or 0, offer = offer }) end
function BG.Send(handIndex, seat) return BG.Act({ type = "send", index = handIndex, seat = seat }) end
-- View an ally's board and hand (read-only; the server refuses enemies, the dead and the human's own seat).
function BG.Peek(seat)
    if type(seat) ~= "number" then
        return nil
    end
    BG.peekSeat = seat
    if BG.ally and BG.ally.seat ~= seat then
        BG.ally = nil
    end
    if BG.model then
        BG.model.ally = BG.ally
    end
    return BG.Act({ type = "peek", seat = seat })
end

function BG.ClosePeek()
    BG.peekSeat = nil
    BG.ally = nil
    if BG.model then
        BG.model.ally = nil
    end
    BG.dirty = true
end
function BG.Sell(zone, index) return BG.Act({ type = "sell", zone = zone, index = index }) end
function BG.Play(index, slot, uid) return BG.Act({ type = "play", index = index, slot = slot, uid = uid or 0 }) end
-- Cast a spell from the hand (the target is a friendly board minion, or 0).
function BG.Cast(index, uid) return BG.Act({ type = "cast", index = index, uid = uid or 0 }) end
function BG.Move(from, to) return BG.Act({ type = "move", index = from, slot = to }) end
function BG.Refresh() return BG.Act({ type = "refresh" }) end
function BG.Freeze(on) return BG.Act({ type = "freeze", flag = on and true or false }) end
function BG.Upgrade() return BG.Act({ type = "upgrade" }) end
function BG.Discover(index) return BG.Act({ type = "discover", index = index }) end
function BG.EndTurn() return BG.Act({ type = "end" }) end

-- ------------------------------------------------------------------ inbound
-- One WRBG chat message (the frame; the prefix is already stripped by the client). The server speaks as the player.
function BG.OnAddonMessage(message, sender)
    if type(message) ~= "string" then
        return
    end
    local me = nil
    if UnitName then
        me = UnitName("player")
    end
    if me ~= nil and sender ~= nil and sender ~= me then
        return
    end
    local status, f = P.ParseFrame(message)
    if status ~= "ok" then
        return
    end
    local result, done = BG.reasm:Add(now(), f.op, f.reqId, f.part, f.total, f.payload)
    if result == "complete" then
        BG.Dispatch(done)
    end
end

function BG.Dispatch(msg)
    local op = msg.op
    if op == BP.PUSH.state then
        BG.OnState(msg.payload, msg.reqId)
    elseif op == BP.PUSH.combat then
        BG.OnCombat(msg.payload)
    elseif op == BP.PUSH.endOfGame then
        BG.OnEnd(msg.payload)
    elseif op == BP.PUSH.close then
        BG.OnClose()
    elseif op == BP.PUSH.ally then
        BG.OnAlly(msg.payload, msg.reqId)
    elseif op == BP.PUSH.err then
        BG.OnError(msg.payload, msg.reqId)
    end
end

function BG.OnState(payload, reqId)
    local m, why = BP.ParseState(payload)
    if not m then
        BG.SetError("The server sent a Battlegrounds state this addon cannot read (" .. tostring(why) .. ").")
        return
    end
    BG.clearOpIf(reqId)
    local wasDone = BG.QuestDone(BG.model)     -- the completion line is the transition, not every state
    local wasDone2 = BG.QuestDone2(BG.model)   -- the second quest too
    BG.model = m
    BG.model.ally = BG.ally
    BG.state = m.ph == "o" and "ended" or "ready"
    BG.errorText = nil
    local questLine = BG.QuestDone(m) and not wasDone and BG.QuestCompleteMessage(m) or nil
    local quest2Line = BG.QuestDone2(m) and not wasDone2 and BG.QuestCompleteMessage2(m) or nil
    local giftLine = BG.GiftMessage(m)
    BG.message = questLine or quest2Line or giftLine     -- the banner shows one line; chat gets all
    if questLine and Hearthstone and Hearthstone.Print then
        Hearthstone.Print(questLine)
    end
    if quest2Line and Hearthstone and Hearthstone.Print then
        Hearthstone.Print(quest2Line)
    end
    if giftLine and Hearthstone and Hearthstone.Print then
        Hearthstone.Print(giftLine)
    end
    if giftLine then
        BG.giftAt = BG.clock or 0          -- the UI highlights the sent card for a moment
    end
    BG.wheelQueue = BG.wheelQueue or {}
    for i = 1, table.getn(m.wheel or {}) do
        table.insert(BG.wheelQueue, m.wheel[i])   -- the UI spins the Wheel of Yogg-Saron for each, once the fight is over
    end
    BG.getTries = 0
    BG.dirty = true
    if BG.refetch and not BG.getReqId then   -- a Get asked for while one was out
        BG.refetch = nil
        BG.Get()
    end
    -- Keep an open peek fresh: ask again after every state, the server answers with BG_ALLY.
    if BG.peekSeat ~= nil and BG.state == "ready" and not BG.op then
        BG.Peek(BG.peekSeat)
    end
end

function BG.OnAlly(payload, reqId)
    local a, why = BP.ParseAlly(payload)
    if not a then
        BG.SetError("The server sent an ally view this addon cannot read (" .. tostring(why) .. ").")
        return
    end
    BG.clearOpIf(reqId)
    if BG.peekSeat ~= a.seat then
        return   -- the view was closed (or changed) before this answer arrived
    end
    BG.ally = a
    if BG.model then
        BG.model.ally = a
    end
    BG.dirty = true
end

function BG.OnCombat(payload)
    local c = BP.ParseCombat(payload)
    if c then
        BG.combat = c
        BG.dirty = true
    end
end

-- The combat to play, once; nil after (the UI calls it once per fight).
function BG.TakeCombat()
    local c = BG.combat
    BG.combat = nil
    return c
end

-- The next Wheel of Yogg-Saron outcome (0-5) to spin, once; nil when none is waiting.
function BG.TakeWheel()
    if not BG.wheelQueue or table.getn(BG.wheelQueue) == 0 then
        return nil
    end
    return table.remove(BG.wheelQueue, 1)
end

function BG.OnEnd(payload)
    local e = BP.ParseEnd(payload)
    if e then
        BG.endResult = e
        BG.state = "ended"
        BG.dirty = true
    end
end

function BG.OnClose()
    BG.Reset()
    BG.dirty = true
end

function BG.OnError(payload, reqId)
    local e = BP.ParseError(payload)
    if reqId == "0" then
        reqId = nil
    end
    if reqId then
        BG.clearOpIf(reqId)
    else
        if BG.op then
            BG.op = nil
        end
        BG.getReqId = nil
    end
    if e.code == "NotReady" then
        BG.Reset()                        -- no game open: quiet
        return
    end
    BG.message = BG.RefusalText(e.why ~= "" and e.why or e.msg)
    BG.dirty = true
end

-- ------------------------------------------------------------------ clock
-- Advances the outbound queue and the timeouts. Called from the driver frame (BgShopUI.lua) every frame.
function BG.Tick(elapsed)
    elapsed = elapsed or 0
    BG.clock = (BG.clock or 0) + elapsed
    if BG.sinceSend == nil then
        BG.sinceSend = 1
    end
    BG.sinceSend = BG.sinceSend + elapsed
    if table.getn(BG.queue) > 0 and BG.sinceSend >= BG.SEND_INTERVAL then
        BG.sinceSend = 0
        BG.Transmit(table.remove(BG.queue, 1))
    end
    if BG.getIn ~= nil then
        BG.getIn = BG.getIn - elapsed
        if BG.getIn <= 0 then
            BG.getIn = nil
            BG.Get()
        end
    end
    if table.getn(BG.queue) == 0 then
        local t = now()
        local timedOut = {}
        for reqId, meta in pairs(BG.pending) do
            if t - meta.sentAt >= BG.TIMEOUT then
                table.insert(timedOut, reqId)
            end
        end
        for i = 1, table.getn(timedOut) do
            BG.OnTimeout(timedOut[i])
        end
    end
    local expired = BG.reasm:Sweep(now())
    if table.getn(expired) > 0 then
        BG.message = "A reply timed out before all of its parts arrived."
        BG.dirty = true
    end
end

function BG.OnTimeout(reqId)
    local meta = BG.pending[reqId]
    if not meta then
        return
    end
    BG.pending[reqId] = nil
    if meta.kind == "act" then
        -- an action's outcome is unknown: never resent, the board is fetched instead
        if BG.op and BG.op.reqId == reqId then
            BG.op = nil
        end
        BG.message = "The server did not answer; the board was refreshed. Check it before acting again."
        BG.dirty = true
        BG.getIn = BG.RETRY_GET
    elseif meta.kind == "get" then
        if BG.getReqId == reqId then
            BG.getReqId = nil
        end
        BG.getTries = (BG.getTries or 0) + 1
        if BG.getTries >= BG.GET_TRIES and BG.state == "loading" then
            -- nobody answers (the server has the mode off): no game, quietly
            BG.getTries = 0
            BG.state = "none"
            BG.dirty = true
        else
            BG.getIn = BG.RETRY_GET
        end
    end
end

-- ------------------------------------------------------------------ card data
local function card(id)
    return HS_BgCardDB and HS_BgCardDB.cards and HS_BgCardDB.cards[id]
end

function BG.Card(id)
    return card(id)
end

-- What a hero power whose number grows with the round does right now, as a short line ("Now: +4/+2"), or nil for one
-- that does not. The formulas of the server: For the Horde! (ActSaurfang) doubles every 3 turns, Tavish's aim and its
-- aim powers (HsBgCombat) double every 3 turns from turn 4.
function BG.PowerNowLine(id, round)
    round = round or 1
    if id == "BG20_HERO_102p" then
        local mult = math.floor(2 ^ math.min(8, math.floor(round / 3)) + 0.5)
        return "Now: +" .. (2 * mult) .. "/+" .. mult
    end
    if id == "BG22_HERO_000p" or id == "BG22_HERO_000p_t1" or id == "BG22_HERO_000p_t2" or id == "BG22_HERO_000p_t3"
        or id == "BG22_HERO_000p_t4" then
        local d = math.floor(2 * 2 ^ math.min(8, math.floor((math.max(1, round) - 1) / 3)) + 0.5)
        return "Now: " .. d .. " damage"
    end
    -- the powers whose number lives in the game state (Rat King's type, Pyramad, Edwin, C'Thun) come from the server
    local m = BG.model
    if m and m.power == id and m.powerNow then
        return m.powerNow
    end
    return nil
end

-- The spells that can be sold for 0 Gold (the server's Shop::Sell): a Wild Banana needs a Beast, so it would clog the hand.
BG.FREE_SELL = { TB_BaconShop_HP_038t = true }

function BG.SellableSpell(entry)
    return entry ~= nil and BG.FREE_SELL[entry.id] == true
end

-- What selling this card pays: the server's price when the state carries it, else 1 Gold for a minion and 0 for a
-- spell (a Wild Banana is only thrown away).
function BG.SellValue(entry)
    if not entry then
        return 0
    end
    if entry.sell ~= nil then
        return entry.sell
    end
    return BG.IsSpell(entry) and 0 or 1
end

-- A card of the hand that is a spell (BgCardDB `spell=1`).
function BG.IsSpell(entry)
    local c = entry and card(entry.id)
    return c ~= nil and c.spell == 1 or false
end

-- The spells that are cast on a friendly board minion (the server's EffectDef::spellNeedsTarget in HsBgEffects*.cpp, with
-- its tribe / non-golden limits); every other spell is cast without a target. Keep in step with the server.
BG.SPELL_TARGETS = {
    BG20_GEM = {}, BG23_000t = {}, BG23_004t = {}, BG23_006t = {}, BG23_007t = {}, BG23_008t = {}, BG23_011t = {},
    TB_BaconShop_HP_038t = { tribe = "BEAST" },
    BGS_Treasures_009 = {}, BGS_Treasures_018 = {}, BGS_Treasures_026 = {}, BGS_Treasures_015 = {},
    BGS_Treasures_034 = { notGolden = true }, BGS_Treasures_016 = { notGolden = true }, BGS_Treasures_028 = {},
}

-- True when the spell entry must be cast on a friendly board minion.
function BG.SpellNeedsTarget(entry)
    return entry ~= nil and BG.SPELL_TARGETS[entry.id] ~= nil
end

-- True when `minion` (a board entry) fits a target rule ({ tribe =, notGolden = }).
local function ruleAllows(rule, minion)
    if not rule or not minion then
        return false
    end
    if rule.notGolden and minion.golden then
        return false
    end
    if rule.tribe then
        local c = card(minion.id)
        local ok = false
        for i = 1, table.getn(c and c.tribes or {}) do
            if c.tribes[i] == rule.tribe or c.tribes[i] == "ALL" then
                ok = true
            end
        end
        return ok
    end
    return true
end

-- True when `minion` (a board entry) is a legal target of the spell entry (the server's Cast checks).
function BG.ValidSpellTarget(spell, minion)
    return ruleAllows(spell and BG.SPELL_TARGETS[spell.id], minion)
end

-- The minions whose Battlecry is aimed at a friendly board minion (the server's ShopCtx::PickTarget users in
-- HsBgEffects*.cpp; `label` names the target in the hint). Played with a target when one is chosen, else the server
-- picks a fitting minion at random. Keep in step with the server.
BG.BATTLECRY_TARGETS = {
    BGS_038 = { tribe = "DRAGON", label = "Dragon" },        -- Twilight Emissary
    BG21_021 = { tribe = "ELEMENTAL", label = "Elemental" }, -- Smogger
    BG_UNG_073 = { tribe = "MURLOC", label = "Murloc" },     -- Rockpool Hunter
    BG_DAL_077 = { tribe = "MURLOC", label = "Murloc" },     -- Toxfin
    BG_AT_069 = { label = "minion" },                        -- Sparring Partner
    BG_DS1_070 = { tribe = "BEAST", label = "Beast" },       -- Houndmaster
    BG23_190 = { label = "minion" },                         -- Uther the Lightbringer
    BGS_048 = { tribe = "PIRATE", label = "Pirate" },        -- Southsea Strongarm
    BG23_357 = { tribe = "DEMON", label = "Demon" },         -- Mind Muck
}

-- The target rule of a hand minion with a targeted Battlecry, or nil.
function BG.BattlecryRule(entry)
    if entry == nil or BG.IsSpell(entry) then
        return nil
    end
    return BG.BATTLECRY_TARGETS[entry.id]
end

-- True when the hand minion has Magnetic (it attaches to a Mech it is dropped on, else it is played as a minion).
function BG.IsMagnetic(entry)
    local c = entry and card(entry.id)
    for i = 1, table.getn(c and c.kw or {}) do
        if c.kw[i] == "MAGNETIC" then
            return true
        end
    end
    return false
end

-- The uid a Magnetic hand minion is played at: the Mech under the cursor, else 0 (the server then merges it into the Mech
-- to the right of the slot, if there is one).
function BG.MagnetUid(entry, minion)
    if minion and BG.IsMagnetic(entry) and ruleAllows({ tribe = "MECHANICAL" }, minion) then
        return minion.uid
    end
    return 0
end

-- True when `minion` (a board entry) can be the target of the hand minion's Battlecry.
function BG.ValidBattlecryTarget(entry, minion)
    return ruleAllows(BG.BattlecryRule(entry), minion)
end

-- True when some minion of `board` can be the target of the hand minion's Battlecry.
function BG.HasBattlecryTarget(entry, board)
    for i = 1, table.getn(board or {}) do
        if BG.ValidBattlecryTarget(entry, board[i]) then
            return true
        end
    end
    return false
end

function BG.CardName(id)
    local c = card(id)
    return c and c.name or tostring(id)
end

-- The card's own keywords as a list of names ("Taunt", "Divine Shield", ...).
function BG.CardKeywords(id)
    local c = card(id)
    local out = {}
    local kw = c and c.kw
    if type(kw) == "table" then
        for i = 1, table.getn(kw) do
            table.insert(out, kw[i] == "MEGA_WINDFURY" and "Mega-Windfury" or BG.KeywordLabel(kw[i]))
        end
    end
    return out
end

-- The keyword names of a minion's keyword BIT SET (the wire `kw`), in a fixed order. No bit operators in Lua 5.0:
-- a bit is set when floor(kw / bit) is odd.
function BG.PlainKeywords(kw)
    local out = {}
    for i = 1, table.getn(BP.KEYWORDS) do
        local bit = BP.KEYWORDS[i][1]
        local q = math.floor(kw / bit)
        if q - math.floor(q / 2) * 2 == 1 then
            table.insert(out, BP.KEYWORDS[i][2])
        end
    end
    return out
end

function BG.KeywordLabel(name)
    if name == "TAUNT" then return "Taunt" end
    if name == "DIVINE_SHIELD" then return "Divine Shield" end
    if name == "POISONOUS" then return "Poisonous" end
    if name == "VENOMOUS" then return "Venomous" end
    if name == "WINDFURY" then return "Windfury" end
    if name == "REBORN" then return "Reborn" end
    if name == "MAGNETIC" then return "Magnetic" end
    return name
end

function BG.TribeLabel(t)
    return BP.TRIBES[t] or tostring(t)
end

-- The name of a card's tribe (BgCardDB stores HearthstoneJSON race names: BEAST, MECHANICAL, ...).
BG.RACE_NAMES = {
    BEAST = "Beast", DEMON = "Demon", DRAGON = "Dragon", ELEMENTAL = "Elemental", MECHANICAL = "Mech",
    MURLOC = "Murloc", NAGA = "Naga", PIRATE = "Pirate", QUILBOAR = "Quilboar", ALL = "All",
}

function BG.RaceLabel(race)
    return BG.RACE_NAMES[race] or tostring(race)
end

-- The card's tribes as a comma list, from BgCardDB (may be empty for a neutral minion).
function BG.CardTribesText(id)
    local c = card(id)
    local out = {}
    local tribes = c and c.tribes
    if type(tribes) == "table" then
        for i = 1, table.getn(tribes) do
            table.insert(out, BG.RaceLabel(tribes[i]))
        end
    end
    return table.concat(out, ", ")
end

-- ------------------------------------------------------------------ the picture
function BG.FactionOf(seat)
    return math.floor(seat / 4)
end

function BG.MyFaction()
    return BG.FactionOf(BG.model and BG.model.me or 0)
end

function BG.FactionName(faction)
    return faction == 0 and "Alliance" or "Horde"
end

-- The seats of a faction, in seat order: { 0, 1, 2, 3 } or { 4, 5, 6, 7 }.
function BG.FactionSeats(faction)
    local out = {}
    for i = 1, 4 do
        table.insert(out, faction * 4 + i - 1)
    end
    return out
end

function BG.SeatIsEnemy(seat)
    return BG.FactionOf(seat) ~= BG.MyFaction()
end

-- A short label for a seat: "You", "Ally 2", "Foe 3".
function BG.SeatLabel(seat)
    local me = BG.model and BG.model.me or 0
    if seat == me then
        return "You"
    end
    local n = (seat - BG.FactionOf(seat) * 4) + 1
    return (BG.SeatIsEnemy(seat) and "Foe " or "Ally ") .. n
end

-- The words of a need reason string ("TRU"): triple, tribe, upgrade.
local function needWords(reason)
    local out = {}
    if string.find(reason, "T", 1, true) then
        table.insert(out, "triple")
    end
    if string.find(reason, "R", 1, true) then
        table.insert(out, "tribe")
    end
    if string.find(reason, "U", 1, true) then
        table.insert(out, "upgrade")
    end
    return table.concat(out, ", ")
end

-- One line naming the allies a card would help (for the tooltip when there is no card art).
function BG.NeedLine(needs)
    if not needs or table.getn(needs) == 0 then
        return nil
    end
    local out = {}
    for i = 1, table.getn(needs) do
        local words = needWords(needs[i].reason)
        table.insert(out, BG.SeatLabel(needs[i].seat) .. (words ~= "" and (" (" .. words .. ")") or ""))
    end
    return "Helps " .. table.concat(out, "; ")
end

-- "1st", "2nd", ... for a placing.
function BG.PlaceLabel(n)
    local suffix = "th"
    local d = n - math.floor(n / 10) * 10
    local h = n - math.floor(n / 100) * 100
    if d == 1 and h ~= 11 then
        suffix = "st"
    elseif d == 2 and h ~= 12 then
        suffix = "nd"
    elseif d == 3 and h ~= 13 then
        suffix = "rd"
    end
    return n .. suffix
end

-- The live placing of a seat, from the lobby's health and elimination round (the server's OverallPlacement shape:
-- survivors first by health, then the eliminated by how late they went out). The server's `rk` wins when the state
-- carries it; the local count is only for an older server. 0 when the seat is unknown.
function BG.LivePlacement(seat)
    local lobby = BG.model and BG.model.lobby
    if not lobby or seat < 0 or seat > 7 or not lobby[seat + 1] then
        return 0
    end
    if BG.model.rank and BG.model.rank[seat + 1] then
        return BG.model.rank[seat + 1]   -- the server's own count
    end
    local function key(s)
        local e = lobby[s + 1]
        if not e then
            return -1000000
        end
        if e.alive then
            return 1000000000 + e.health * 100 - s
        end
        -- a later elimination is a better placing; within a round the lower health went out first
        return e.elim * 100000 + (e.health + 99) * 100 - s
    end
    local k = key(seat)
    local rank = 1
    for s = 0, 7 do
        if s ~= seat and key(s) > k then
            rank = rank + 1
        end
    end
    return rank
end

-- The lobby clue: the short line under a seat's health.
function BG.SeatState(seat)
    local s = BG.model and BG.model.lobby and BG.model.lobby[seat + 1]
    if not s then
        return ""
    end
    if not s.alive then
        return "eliminated (round " .. s.elim .. ")"
    end
    return "tier " .. s.tier
end

-- ------------------------------------------------------------------ heroes, powers, sending
-- The hero definitions (name, health, armor tier, power) and the hero power text of BgCardDB.lua, keyed by id.
function BG.Hero(id)
    return HS_BgCardDB and HS_BgCardDB.heroes and HS_BgCardDB.heroes[id]
end

function BG.HeroName(id)
    local h = BG.Hero(id)
    return h and h.name or tostring(id)
end

function BG.HeroPower(id)
    return HS_BgCardDB and HS_BgCardDB.powers and HS_BgCardDB.powers[id]
end

function BG.PowerName(id)
    local p = BG.HeroPower(id)
    return p and p.name or tostring(id)
end

-- The lobby seat's record (health, tier, alive, elim, opp, hero, armor), or nil.
function BG.Seat(seat)
    return BG.model and BG.model.lobby and BG.model.lobby[seat + 1]
end

-- A seat's hero id ("" before the pick / when unknown).
function BG.SeatHero(seat)
    local s = BG.Seat(seat)
    return s and s.hero or ""
end

-- A seat's full health: the server's mhp for the human, the hero's card health for the others (40 without a hero).
function BG.SeatMaxHealth(seat)
    if BG.model and seat == BG.model.me then
        return BG.MyMaxHealth()
    end
    local h = BG.Hero(BG.SeatHero(seat))
    return h and tonumber(h.health) or 40
end

function BG.SeatArmor(seat)
    local s = BG.Seat(seat)
    return s and s.armor or 0
end

-- A seat's player name from the wire (nm), falling back to the seat label when it is empty.
function BG.SeatName(seat)
    local names = BG.model and BG.model.names
    local n = names and names[seat + 1]
    if n and n ~= "" then
        return n
    end
    return BG.SeatLabel(seat)
end

-- A seat's title for the lobby and the result screen: the name and, once the pick is done, the hero.
function BG.SeatTitle(seat)
    local name = BG.SeatName(seat)
    local hero = BG.SeatHero(seat)
    local heroName = BG.Hero(hero) and BG.Hero(hero).name or nil
    local armor = BG.SeatArmor(seat)
    if heroName then
        name = heroName .. " - " .. name .. (armor > 0 and (" (" .. armor .. " armor)") or "")   -- hero and seat
        return name
    end
    return name
end

-- My hero id, armor, max health and hero power.
function BG.MyHero()
    return BG.model and BG.model.hero or ""
end

function BG.MyArmor()
    return BG.model and BG.model.armor or 0
end

function BG.MyMaxHealth()
    return BG.model and BG.model.maxHealth or 40
end

function BG.PowerCost()
    return BG.model and BG.model.powerCost or 0
end

function BG.PowerUsable()
    return BG.model and BG.model.powerUsable or false
end

-- "n" none, "f" a friendly minion, "o" a tavern offer; nil when there is no active power.
function BG.PowerTarget()
    return BG.model and BG.model.powerTarget or "n"
end

-- The send of this shop turn is used.
function BG.SendUsed()
    return BG.model and BG.model.sendUsed or false
end

local function handCount()
    return BG.model and table.getn(BG.model.hand) or 0
end

-- An ally of the human's faction (not the human, same side, still alive).
function BG.IsLivingAlly(seat)
    local m = BG.model
    if not m or seat == m.me or BG.SeatIsEnemy(seat) then
        return false
    end
    local s = BG.Seat(seat)
    return s ~= nil and s.alive == true
end

-- A hand minion may be sent to this seat: the shop phase, an unused send, a living ally and a card to send.
function BG.CanSend(seat)
    local m = BG.model
    if not m or m.ph ~= "s" or m.sendUsed then
        return false
    end
    if not BG.IsLivingAlly(seat) then
        return false
    end
    return handCount() > 0
end

-- The line when allies sent the human a minion in the last action, or nil.
function BG.GiftMessage(m)
    m = m or BG.model
    if not m or type(m.gifts) ~= "table" or table.getn(m.gifts) == 0 then
        return nil
    end
    local names = {}
    for i = 1, table.getn(m.gifts) do
        table.insert(names, BG.CardName(m.gifts[i]))
    end
    return "An ally sent you " .. table.concat(names, ", ") .. "."
end

-- ------------------------------------------------------------------ quests and rewards
-- The quest and reward definitions (name, text, the goal number of a quest) of BgCardDB.lua, keyed by id.
function BG.Quest(id)
    return HS_BgCardDB and HS_BgCardDB.quests and HS_BgCardDB.quests[id]
end

function BG.QuestName(id)
    local q = BG.Quest(id)
    return q and q.name or tostring(id)
end

-- A quest's goal number from the card data, or nil (rewards have none).
function BG.QuestNumber(id)
    local q = BG.Quest(id)
    return q and q.number or nil
end

function BG.Reward(id)
    return HS_BgCardDB and HS_BgCardDB.rewards and HS_BgCardDB.rewards[id]
end

function BG.RewardName(id)
    local r = BG.Reward(id)
    return r and r.name or tostring(id)
end

-- A minion type (tribe bit) as a plural for the quest texts: "Beasts", "Mechs", "Naga".
BG.TRIBE_PLURAL = {
    [1] = "Beasts", [2] = "Demons", [4] = "Dragons", [8] = "Elementals", [16] = "Mechs",
    [32] = "Murlocs", [64] = "Naga", [128] = "Pirates", [256] = "Quilboar",
}

local function tribePlural(t)
    return BG.TRIBE_PLURAL[t] or (BG.TribeLabel(t) .. "s")
end

-- The generic wording gen_bg_lua.py puts in place of a type the server draws per game (its FILL table).
local DUO_A = "minions of one of two types"
local DUO_B = "minions of the other"
local FRIENDS = "minions of one of the game's minion types"

local function replacePlain(s, find, repl)
    local a, b = string.find(s, find, 1, true)
    if not a then
        return s
    end
    return string.sub(s, 1, a - 1) .. repl .. string.sub(s, b + 1)
end

-- A quest's text with the minion types it names (Unlikely Duo's two; 0 = not known, the generic wording stays).
function BG.QuestText(id, typeA, typeB)
    local q = BG.Quest(id)
    local text = q and q.text or ""
    if (typeA or 0) ~= 0 and (typeB or 0) ~= 0 then
        text = replacePlain(text, DUO_A, tribePlural(typeA))
        text = replacePlain(text, DUO_B, tribePlural(typeB))
    end
    return text
end

-- A reward's text with the minion type it names (The Friends Along the Way; 0 = the generic wording).
function BG.RewardText(id, rewardType)
    local r = BG.Reward(id)
    local text = r and r.text or ""
    if (rewardType or 0) ~= 0 then
        text = replacePlain(text, FRIENDS, tribePlural(rewardType))
    end
    return text
end

-- My taken quest's and reward's texts, with their types.
function BG.MyQuestText()
    local m = BG.model or {}
    return BG.QuestText(m.quest, m.questTypeA, m.questTypeB)
end

function BG.MyRewardText()
    local m = BG.model or {}
    return BG.RewardText(m.reward, m.rewardType)
end

-- The three offered quest + reward pairs (each { quest, reward }) while a quest offer is pending, else an empty list.
function BG.QuestOffers()
    local m = BG.model
    if m and type(m.questOffer) == "table" then
        return m.questOffer
    end
    return {}
end

-- One offered pair by its 0-based choice index (the index sent with act=quest;i=), or nil.
function BG.QuestOffer(index)
    return BG.QuestOffers()[index + 1]
end

function BG.MyQuest()
    return (BG.model and BG.model.quest) or ""
end

function BG.MyReward()
    return (BG.model and BG.model.reward) or ""
end

function BG.QuestProgress()
    return (BG.model and BG.model.questProgress) or 0
end

function BG.QuestRequired()
    return (BG.model and BG.model.questRequired) or 0
end

-- The taken quest's goal is reached (its reward is active). Not done before a quest is taken.
function BG.QuestDone(m)
    m = m or BG.model
    if not m then
        return false
    end
    local q = m.quest
    if not q or q == "" then
        return false
    end
    local required = m.questRequired or 0
    return required > 0 and (m.questProgress or 0) >= required
end

-- A seat's taken quest and reward id ("" before the pick / when unknown).
function BG.SeatQuest(seat)
    local s = BG.Seat(seat)
    return s and s.quest or ""
end

function BG.SeatReward(seat)
    local s = BG.Seat(seat)
    return s and s.reward or ""
end

-- The line when the taken quest's goal is reached (nil otherwise).
function BG.QuestCompleteMessage(m)
    m = m or BG.model
    if not m or not BG.QuestDone(m) then
        return nil
    end
    local line = "Quest complete: " .. BG.QuestName(m.quest) .. "."
    if m.reward and m.reward ~= "" then
        line = line .. " Your reward is active: " .. BG.RewardName(m.reward) .. "."
    end
    return line
end

function BG.QuestCompleteMessage2(m)
    m = m or BG.model
    if not m or not BG.QuestDone2(m) then
        return nil
    end
    local line = "Quest complete: " .. BG.QuestName(m.quest2) .. "."
    if m.reward2 and m.reward2 ~= "" then
        line = line .. " Your second reward is active: " .. BG.RewardName(m.reward2) .. "."
    end
    return line
end

-- --------------------------------------------------------------- second quest, tickets, held minions
function BG.QuestProgress2()
    return (BG.model and BG.model.questProgress2) or 0
end

function BG.QuestRequired2()
    return (BG.model and BG.model.questRequired2) or 0
end

function BG.QuestDone2(m)
    m = m or BG.model
    if not m then
        return false
    end
    local q = m.quest2
    if not q or q == "" then
        return false
    end
    local required = m.questRequired2 or 0
    return required > 0 and (m.questProgress2 or 0) >= required
end

function BG.MyQuestText2()
    local m = BG.model or {}
    return BG.QuestText(m.quest2, m.questTypeA2, m.questTypeB2)
end

function BG.MyRewardText2()
    local m = BG.model or {}
    return BG.RewardText(m.reward2, m.rewardType2)
end

-- This turn ends at once (A. F. Kay, Ambassador Faelin).
function BG.SkipTurn()
    return BG.model and BG.model.skipTurn or false
end

-- Maiev's held tavern offer: its id and the StartTurns left, or "" / 0.
function BG.HeldOffer()
    local m = BG.model
    return (m and m.heldOffer) or "", (m and m.heldTurns) or 0
end

-- Faelin's Tier minions waiting for their Tier: a list of { id, tier }.
function BG.Expedition()
    local m = BG.model
    return (m and m.expedition) or {}
end

-- ------------------------------------------------------------------ texts
BG.WHY_TEXT = {
    BadPlayer = "That is not your board.",
    Eliminated = "You are out of the game.",
    GameOver = "The game is over.",
    NotShopPhase = "You already ended your turn.",
    DiscoverPending = "Choose one of the discovered minions first.",
    NoDiscover = "There is nothing to choose.",
    BadIndex = "That slot is empty.",
    BadTarget = "That is not a valid target.",
    NotEnoughGold = "Not enough gold.",
    HandFull = "Your hand is full.",
    BoardFull = "Your board is full.",
    MaxTier = "Your tavern is already at the highest tier.",
    NotAlly = "You can only send to a living ally of your faction.",
    SendUsed = "You already sent a minion this turn.",
    NoPower = "Your hero power is passive.",
    PowerUsed = "Your hero power is already used.",
    NotPicking = "The hero pick is over.",
}

function BG.RefusalText(why)
    if not why or why == "" then
        return "The server refused that."
    end
    return BG.WHY_TEXT[why] or "The server refused that (" .. tostring(why) .. ")."
end

-- ------------------------------------------------------------------ startup
-- Sends one BG_GET after the handshake so a /reload restores an open game (a reply of NotReady is quiet). Called from
-- the driver frame's PLAYER_ENTERING_WORLD.
function BG.Restore()
    if Hearthstone and Hearthstone.state and Hearthstone.state.welcomed then
        BG.Get()
    end
end
