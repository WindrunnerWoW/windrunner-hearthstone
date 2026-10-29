-- Hearthstone addon: Warsong Battlegrounds wire codec.
--
-- The WRBG payloads on top of the HsProtocol codec: the frame, the escaping and the chunking are HsProtocol's (the
-- server uses the same codec for both prefixes; only the chat prefix differs). This is the addon mirror of
-- src/battlegrounds/HsBgWire.cpp. Pure Lua, no WoW API, so the tests load it in a plain interpreter.
--
-- CLIENT -> SERVER
--   BG_GET                 empty: resend the state (and the result once the game is over)
--   BG_ACT   act=<name>;    one action, keys by act (see BP.EncodeAction)
-- SERVER -> CLIENT
--   BG_STATE   the human's view; BG_COMBAT  the human's fight with its event log (pushed before the next BG_STATE);
--   BG_ALLY    the answer to a peek (an ally's field); BG_END  the result; BG_CLOSE  the game ended on the server;
--   ERR  a refusal.
--
-- `nd` (optional) names where my hand cards, board minions and affordable tavern offers would help an ally:
-- `<uid>.<seat><reasons>` entries joined by ',', best first, at most three allies per card. The reasons are T
-- (a triple), R (the ally's tribe) and U (an upgrade); empty outside the shop phase and once `snd=1`.
-- `rk` (optional) is the server's live placing of each seat (eight numbers, 1 best).
-- `svh` / `svb` (optional) are what each hand card / board minion sells for (one number per entry; a spell is 0).
--
-- `ph` may be `p` (the hero pick, before round 1). The state then carries the four choices (`hc`) and, once a
-- hero is picked, the hero id, armor, max health and the hero power (`pw`/`pwc`/`pwu`/`pwt`), the send flag (`snd`),
-- the cards allies sent (`gift`), the Wheel of Yogg-Saron outcomes spun by the action (`whl`, one digit 0-5 each, oldest
-- first, absent = none) and the eight seat names (`nm`, `|`-separated, the human's own empty). `pwx` is the small counter on the power button (Captain Eudora's digs left). Every `pl`
-- seat also carries its hero id and armor.
--
-- `ph` may be `q` (the quest pick, at the start of turn 4, after the hero pick). The state then carries the three
-- quest + reward pairs offered (`qo`, "questId:rewardId" pairs joined by '|'), my taken quest with its progress and its
-- goal (`q`, `qp`, `qr`) and my active reward (`rw`, empty until the quest is done). Every `pl` seat also carries its
-- quest and reward id (either may be empty). The minion types a pair names ride as tribe bits: each `qo` pair may end
-- in ":typeA:typeB:rewardType" (Unlikely Duo's two, The Friends Along the Way's one, 0 = none), and `qa`, `qb`, `rt`
-- are the ones of my taken quest and reward.
--
-- `oft` marks the Darkmoon Tickets (one digit per offer, like `ofz`); `pws` says this turn is skipped;
-- `pwh` is Maiev's held offer ("id.turns") and `pwe` Faelin's held Tier minions ("id.tier" joined by ',').
-- Denathrius's second quest and reward are `q2`, `qp2`, `qr2`, `rw2`, `qa2`, `qb2`, `rt2`; every `pl` seat carries
-- two more fields (its second quest and reward id).
--
-- `sec` lists my Secrets in play order (card ids joined by ','); they show as icons on my hero portrait.
--
-- A minion is "id.g.atk.hp.kw.uid" (g = golden 0/1, kw = the keyword bit set including auras), lists join with ','; an
-- empty list is the empty string. Card ids are the stable ids of the card table (BgCardDB.lua), never table indexes.
-- A spell is the same entry with atk=hp=kw=0; its BgCardDB entry has `spell=1`. `tm` lists the uids of the
-- temporary Spellcraft cards in the hand (they leave at the next StartTurn).

HsBgProtocol = {}
local BP = HsBgProtocol
local P = HsProtocol

BP.PREFIX = "WRBG"
BP.MAX_WIRE = P.MAX_WIRE
BP.NAME = "Warsong Battlegrounds"     -- the player-facing name of the mode (change it here only)

BP.OPS = { get = "BG_GET", act = "BG_ACT" }
BP.PUSH = { state = "BG_STATE", combat = "BG_COMBAT", endOfGame = "BG_END", close = "BG_CLOSE", err = "ERR",
    ally = "BG_ALLY" }
BP.RESTED = -3      -- a lobby seat's lastOpponent when it rested last round (its faction had a player too many)

-- The tribes of the lobby (HsBgTypes.h Tribe bit values) -> a player-facing name.
BP.TRIBES = {
    [1] = "Beast", [2] = "Demon", [4] = "Dragon", [8] = "Elemental", [16] = "Mech",
    [32] = "Murloc", [64] = "Naga", [128] = "Pirate", [256] = "Quilboar", [512] = "All",
}
-- The keyword bit set (HsBgTypes.h Keyword): value, short name.
BP.KEYWORDS = {
    { 1, "Taunt" }, { 2, "Divine Shield" }, { 4, "Poisonous" }, { 8, "Windfury" }, { 16, "Reborn" },
    { 32, "Magnetic" }, { 64, "Mega-Windfury" }, { 128, "Stealth" }, { 256, "Venomous" },
}
BP.KEYWORD_MAX = 511          -- every bit of BP.KEYWORDS set
-- The combat event types (HsBgCombat.h EventType), by number.
BP.EVENTS = { [0] = "attack", [1] = "damage", [2] = "shield", [3] = "death", [4] = "summon", [5] = "reborn",
    [6] = "buff", [7] = "trigger" }

-- --------------------------------------------------------------------------- parsing helpers
-- Canonical decimal of at most maxDigits digits (optional leading '-' when min < 0), in [min, max], or nil.
local function parseInt(s, min, max, maxDigits)
    if type(s) ~= "string" or s == "" or string.len(s) > maxDigits + 1 then
        return nil
    end
    local neg = false
    local body = s
    if string.sub(s, 1, 1) == "-" then
        neg = true
        body = string.sub(s, 2)
    end
    if body == "" or string.len(body) > maxDigits or not string.find(body, "^%d+$") then
        return nil
    end
    if string.len(body) > 1 and string.sub(body, 1, 1) == "0" then
        return nil
    end
    local n = tonumber(body)
    if neg then n = -n end
    if n < min or n > max then
        return nil
    end
    return n
end

local function parseFlag(s)
    if s == "0" then
        return false
    elseif s == "1" then
        return true
    end
    return nil
end

-- Splits "a,b,c" (no empty items, at most maxItems) -> list, or nil.
local function splitList(s, maxItems)
    if type(s) ~= "string" then
        return nil
    end
    if s == "" then
        return {}
    end
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

-- "3,1,0" -> { { value }, ... } with exactly `count` numbers in [0, 999], or nil when the key is absent.
-- false when the list is malformed.
local function parseSellList(s, count)
    if s == nil then
        return nil
    end
    local items = splitList(s, count + 1)
    if not items or table.getn(items) ~= count then
        return false
    end
    local out = {}
    for i = 1, count do
        local v = parseInt(items[i], 0, 999, 3)
        if v == nil then
            return false
        end
        out[i] = v
    end
    return out
end

local function validId(id)
    return type(id) == "string" and string.len(id) >= 1 and string.len(id) <= 32
        and string.find(id, "^[%w_]+$") ~= nil
end

-- "" -> "" (the field is unset), a valid id -> itself, anything else -> nil (malformed).
local function optId(s)
    if s == nil or s == "" then
        return ""
    end
    if validId(s) then
        return s
    end
    return nil
end

-- "questId:rewardId[:typeA:typeB:rewardType]|..." -> { { quest, reward, typeA, typeB, rewardType }, ... }, or nil.
-- "" -> {} (no offer pending). At most three pairs, each with two valid ids of 1..32 bytes; the types are tribe bits
-- (0 = none).
local function parseQuestOffer(s)
    if type(s) ~= "string" then
        return nil
    end
    local out = {}
    if s == "" then
        return out
    end
    local pos = 1
    while true do
        local bar = string.find(s, "|", pos, true)
        local item
        if bar then
            item = string.sub(s, pos, bar - 1)
        else
            item = string.sub(s, pos)
        end
        local _, _, qid, rid, a, b, r = string.find(item, "^([%w_]+):([%w_]+):(%d+):(%d+):(%d+)$")
        if not qid or not validId(qid) or not validId(rid) then
            return nil
        end
        local ta, tb, rt = parseInt(a, 0, 512, 3), parseInt(b, 0, 512, 3), parseInt(r, 0, 512, 3)
        if ta == nil or tb == nil or rt == nil then
            return nil
        end
        table.insert(out, { quest = qid, reward = rid, typeA = ta, typeB = tb, rewardType = rt })
        if table.getn(out) > 3 then
            return nil
        end
        if not bar then
            return out
        end
        pos = bar + 1
    end
end

-- Splits "a|b|c" into a list of up to maxItems parts (empty parts allowed: a seat name may be ""); nil when the
-- count is wrong. The seat names use '|' because a name may contain a comma. A part over 64 bytes is cut, not
-- refused: one long NPC name must not make the whole state unreadable.
local function splitPipe(s, count)
    if type(s) ~= "string" then
        return nil
    end
    local out = {}
    local pos = 1
    while true do
        local bar = string.find(s, "|", pos, true)
        local item
        if bar then
            item = string.sub(s, pos, bar - 1)
        else
            item = string.sub(s, pos)
        end
        if string.len(item) > 64 then
            item = string.sub(item, 1, 64)
        end
        table.insert(out, item)
        if not bar then
            break
        end
        pos = bar + 1
    end
    if count and table.getn(out) ~= count then
        return nil
    end
    return out
end

-- "id.g.atk.hp.kw.uid,id2..." -> { { id, golden, atk, hp, kw, uid }, ... }, or nil. "" -> {}.
-- maxItems bounds the list length (7 board, 10 hand, 15 offers).
function BP.ParseMinions(s, maxItems)
    local items = splitList(s, maxItems or 16)
    if not items then
        return nil
    end
    local out = {}
    for i = 1, table.getn(items) do
        local _, _, id, g, atk, hp, kw, uid = string.find(items[i], "^([%w_]+)%.([01])%.(%-?%d+)%.(%-?%d+)%.(%d+)%.(%d+)$")
        if not id or not validId(id) then
            return nil
        end
        local a = parseInt(atk, -9999, 99999, 5)
        local h = parseInt(hp, -9999, 99999, 5)
        local k = parseInt(kw, 0, BP.KEYWORD_MAX, 3)
        local u = parseInt(uid, 0, 999999999, 9)
        if a == nil or h == nil or k == nil or u == nil then
            return nil
        end
        table.insert(out, { id = id, golden = g == "1", atk = a, hp = h, kw = k, uid = u })
    end
    return out
end

-- --------------------------------------------------------------------------- BG_STATE
function BP.EmptyState()
    return { me = 0, rd = 1, ph = "s", al = true, en = false, gold = 0, tier = 1, upgrade = 0, buyCost = 3, frozen = false,
        health = 40, cap = true, offers = {}, hand = {}, board = {}, options = {}, powerOptions = {}, tribes = {},
        enemyTribes = {}, refreshCost = 1, lobby = {},
        winner = -1,
        hc = {}, hero = "", armor = 0, maxHealth = 40, restHealed = 0, power = "", powerCost = 0, powerUsable = false,
        powerCounter = 0,
        powerTarget = "n", sendUsed = false, gifts = {}, wheel = {}, names = {}, tempSpells = {},
        -- Where my hand cards and tavern offers would help an ally (uid -> { { seat, reason }, ... })
        need = {},
        skipTurn = false, heldOffer = "", heldTurns = 0, expedition = {},
        -- My Secrets, in play order
        secrets = {},
        questOffer = {}, quest = "", questProgress = 0, questRequired = 0, reward = "",
        questTypeA = 0, questTypeB = 0, rewardType = 0,
        quest2 = "", questProgress2 = 0, questRequired2 = 0, reward2 = "",
        questTypeA2 = 0, questTypeB2 = 0, rewardType2 = 0 }
end

function BP.ParseState(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = P.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local m = BP.EmptyState()
    local me = parseInt(kv.me, 0, 7, 1)
    local rd = parseInt(kv.rd, 0, 999, 3)
    if me == nil then
        return nil, "bad me"
    end
    if rd == nil then
        return nil, "bad rd"
    end
    m.me, m.rd = me, rd
    if kv.ph ~= "s" and kv.ph ~= "o" and kv.ph ~= "p" and kv.ph ~= "q" then
        return nil, "bad ph"
    end
    m.ph = kv.ph
    m.al = parseFlag(kv.al)
    m.en = parseFlag(kv.en)
    if m.al == nil or m.en == nil then
        return nil, "bad flags"
    end
    m.gold = parseInt(kv.g, 0, 999, 3)
    m.tier = parseInt(kv.t, 1, 6, 1)
    m.upgrade = parseInt(kv.uc, -1, 999, 3)       -- -1: the tavern is at its top tier, nothing to buy
    m.buyCost = parseInt(kv.bc, 0, 99, 2)  -- Friends and Family Discount (Millhouse too)
    m.health = parseInt(kv.hp, -99, 9999, 4)
    if m.gold == nil or m.tier == nil or m.upgrade == nil or m.health == nil then
        return nil, "bad numbers"
    end
    m.frozen = parseFlag(kv.fz)
    m.cap = parseFlag(kv.cap)
    if m.frozen == nil or m.cap == nil then
        return nil, "bad flags"
    end
    m.offers = BP.ParseMinions(kv.of, 15)
    m.hand = BP.ParseMinions(kv.hd, 10)
    m.board = BP.ParseMinions(kv.bd, 7)
    m.options = BP.ParseMinions(kv.op, 5)
    if not m.offers or not m.hand or not m.board or not m.options then
        return nil, "bad minions"
    end
    -- What each hand / board card sells for (optional; an old server sends no key).
    local svh = parseSellList(kv.svh, table.getn(m.hand))
    local svb = parseSellList(kv.svb, table.getn(m.board))
    if svh == false or svb == false then
        return nil, "bad sell prices"
    end
    for i = 1, table.getn(m.hand) do
        m.hand[i].sell = svh and svh[i] or nil
    end
    for i = 1, table.getn(m.board) do
        m.board[i].sell = svb and svb[i] or nil
    end
    -- Where my cards would help: "<uid>.<seat><TRU>,..." (optional; an old server sends no key).
    local needs = splitList(kv.nd or "", 64)
    if not needs then
        return nil, "bad nd"
    end
    for i = 1, table.getn(needs) do
        local _, _, uid, seat, reasons = string.find(needs[i], "^(%d+)%.([0-7])([TRU]*)$")
        local u = parseInt(uid, 0, 999999999, 9)
        local s = parseInt(seat, 0, 7, 1)
        if u == nil or s == nil or reasons == "" then
            return nil, "bad nd"
        end
        local list = m.need[u] or {}
        table.insert(list, { seat = s, reason = reasons })
        m.need[u] = list
    end
    -- A hero-power choice instead of minions: the power ids in opw, comma separated.
    local powerOptions = splitList(kv.opw, 8)
    if not powerOptions then
        return nil, "bad opw"
    end
    for i = 1, table.getn(powerOptions) do
        if not validId(powerOptions[i]) then
            return nil, "bad power option"
        end
        table.insert(m.powerOptions, powerOptions[i])
    end
    if table.getn(m.options) > 0 and table.getn(m.powerOptions) > 0 then
        return nil, "two kinds of options"
    end
    -- ofz marks the Frozen offers and oft Silas Darkmoon's tickets, one digit per offer.
    if type(kv.ofz) ~= "string" or string.len(kv.ofz) ~= table.getn(m.offers) then
        return nil, "bad ofz"
    end
    if type(kv.oft) ~= "string" or string.len(kv.oft) ~= table.getn(m.offers) then
        return nil, "bad oft"
    end
    for i = 1, table.getn(m.offers) do
        m.offers[i].frozen = string.sub(kv.ofz, i, i) == "1"
        m.offers[i].ticket = string.sub(kv.oft, i, i) == "1"
    end
    -- The temporary Spellcraft cards of the hand.
    local temp = splitList(kv.tm, 10)
    if not temp then
        return nil, "bad tm"
    end
    for i = 1, table.getn(temp) do
        local u = parseInt(temp[i], 0, 999999999, 9)
        if u == nil then
            return nil, "bad tm"
        end
        table.insert(m.tempSpells, u)
        for j = 1, table.getn(m.hand) do
            if m.hand[j].uid == u then
                m.hand[j].temp = true
            end
        end
    end
    local tribes = splitList(kv.tb, 9)
    if not tribes then
        return nil, "bad tb"
    end
    for i = 1, table.getn(tribes) do
        local t = parseInt(tribes[i], 1, 512, 3)
        if t == nil then
            return nil, "bad tribe"
        end
        table.insert(m.tribes, t)
    end
    -- The other faction's own set and my next Refresh cost.
    local enemy = kv.te ~= nil and splitList(kv.te, 9)
    if not enemy then
        return nil, "bad te"
    end
    for i = 1, table.getn(enemy) do
        local t = parseInt(enemy[i], 1, 512, 3)
        if t == nil then
            return nil, "bad enemy tribe"
        end
        table.insert(m.enemyTribes, t)
    end
    m.refreshCost = parseInt(kv.rf, 0, 99, 2)
    if m.refreshCost == nil then
        return nil, "bad refresh cost"
    end
    -- The hero pick, the picked hero with its armor and power, the send flag and gifts.
    local hc = splitList(kv.hc, 8)
    if not hc then
        return nil, "bad hc"
    end
    for i = 1, table.getn(hc) do
        if not validId(hc[i]) then
            return nil, "bad hero choice"
        end
        table.insert(m.hc, hc[i])
    end
    m.hero = kv.hero
    if m.hero == nil or (m.hero ~= "" and not validId(m.hero)) then
        return nil, "bad hero"
    end
    m.power = kv.pw
    if m.power == nil or (m.power ~= "" and not validId(m.power)) then
        return nil, "bad power"
    end
    m.armor = parseInt(kv.arm, 0, 999, 3)
    m.maxHealth = parseInt(kv.mhp, -99, 9999, 4)
    m.restHealed = parseInt(kv.rh, 0, 999, 3)     -- the health I healed resting last round
    m.powerCost = parseInt(kv.pwc, -99, 999, 3)
    m.powerCounter = parseInt(kv.pwx, -99, 999, 3)
    m.powerUsable = parseFlag(kv.pwu)
    -- "Now: ..." line of a power whose effect changes (optional; an older server does not send it)
    m.powerNow = nil
    if kv.pwn ~= nil and kv.pwn ~= "" and string.find(kv.pwn, "^Now: [%w%+/ ]+$") and string.len(kv.pwn) <= 40 then
        m.powerNow = kv.pwn
    end
    local tgt = kv.pwt
    if m.armor == nil or m.maxHealth == nil or m.restHealed == nil or m.powerCost == nil or m.powerCounter == nil
        or m.powerUsable == nil
        or (tgt ~= "n" and tgt ~= "f" and tgt ~= "o" and tgt ~= "s" and tgt ~= "m") then
        return nil, "bad hero numbers"
    end
    m.powerTarget = tgt
    m.sendUsed = parseFlag(kv.snd)
    if m.sendUsed == nil then
        return nil, "bad snd"
    end
    -- A skipped turn, Maiev's held offer and Faelin's held Tier minions.
    m.skipTurn = parseFlag(kv.pws)
    if m.skipTurn == nil then
        return nil, "bad pws"
    end
    if kv.pwh == nil then
        return nil, "bad pwh"
    end
    if kv.pwh ~= "" then
        local a, b, id, turns = string.find(kv.pwh, "^([%w_]+)%.(%-?%d+)$")
        local t = parseInt(turns, 0, 99, 2)
        if a == nil or not validId(id) or t == nil then
            return nil, "bad pwh"
        end
        m.heldOffer, m.heldTurns = id, t
    end
    local expedition = splitList(kv.pwe, 3)
    if not expedition then
        return nil, "bad pwe"
    end
    for i = 1, table.getn(expedition) do
        local a, b, id, tier = string.find(expedition[i], "^([%w_]+)%.(%-?%d+)$")
        local t = parseInt(tier, 1, 6, 1)
        if a == nil or not validId(id) or t == nil then
            return nil, "bad pwe"
        end
        table.insert(m.expedition, { id = id, tier = t })
    end
    -- My Secrets in play order (card ids, comma separated).
    local secrets = splitList(kv.sec, 5)
    if not secrets then
        return nil, "bad sec"
    end
    for i = 1, table.getn(secrets) do
        if not validId(secrets[i]) then
            return nil, "bad secret"
        end
        table.insert(m.secrets, secrets[i])
    end
    local gifts = splitList(kv.gift, 8)
    if not gifts then
        return nil, "bad gift"
    end
    for i = 1, table.getn(gifts) do
        if not validId(gifts[i]) then
            return nil, "bad gift card"
        end
        table.insert(m.gifts, gifts[i])
    end
    -- The Wheel of Yogg-Saron spins of this action: one digit (the outcome 0-5) each, oldest first; the key may be absent.
    local wheel = kv.whl or ""
    if string.len(wheel) > 8 or string.find(wheel, "[^0-5]") then
        return nil, "bad whl"
    end
    for i = 1, string.len(wheel) do
        table.insert(m.wheel, tonumber(string.sub(wheel, i, i)))
    end

    -- The quest offer, my taken quest with its progress and goal, and my active reward.
    local offer = parseQuestOffer(kv.qo)
    if not offer then
        return nil, "bad qo"
    end
    m.questOffer = offer
    m.quest = (kv.q ~= nil and optId(kv.q) or nil)
    m.reward = (kv.rw ~= nil and optId(kv.rw) or nil)
    m.questProgress = parseInt(kv.qp, 0, 9999, 4)
    m.questRequired = parseInt(kv.qr, 0, 9999, 4)
    m.questTypeA = parseInt(kv.qa, 0, 512, 3)
    m.questTypeB = parseInt(kv.qb, 0, 512, 3)
    m.rewardType = parseInt(kv.rt, 0, 512, 3)
    -- Sire Denathrius's second quest and reward.
    m.quest2 = (kv.q2 ~= nil and optId(kv.q2) or nil)
    m.reward2 = (kv.rw2 ~= nil and optId(kv.rw2) or nil)
    m.questProgress2 = parseInt(kv.qp2, 0, 9999, 4)
    m.questRequired2 = parseInt(kv.qr2, 0, 9999, 4)
    m.questTypeA2 = parseInt(kv.qa2, 0, 512, 3)
    m.questTypeB2 = parseInt(kv.qb2, 0, 512, 3)
    m.rewardType2 = parseInt(kv.rt2, 0, 512, 3)
    if m.quest == nil or m.reward == nil or m.questProgress == nil or m.questRequired == nil
        or m.questTypeA == nil or m.questTypeB == nil or m.rewardType == nil
        or m.quest2 == nil or m.reward2 == nil or m.questProgress2 == nil or m.questRequired2 == nil
        or m.questTypeA2 == nil or m.questTypeB2 == nil or m.rewardType2 == nil then
        return nil, "bad quest"
    end

    local seats = splitList(kv.pl, 8)
    if not seats or table.getn(seats) ~= 8 then
        return nil, "bad pl"
    end
    for i = 1, 8 do
        -- health.tier.alive.eliminationRound.lastOpponent.heroId.armor.questId.rewardId.questId2.rewardId2
        -- (the four quest fields may be empty); lastOpponent is a seat, BP.RESTED when the seat rested last round,
        -- -2 before the first fight
        local _, _, hp, tier, alive, elim, opp, hid, arm, qid, rid, q2id, r2id =
            string.find(seats[i], "^(%-?%d+)%.(%d+)%.([01])%.(%d+)%.(%-?%d+)%.([%w_]*)%.(%-?%d+)%.([%w_]*)%.([%w_]*)%.([%w_]*)%.([%w_]*)$")
        local h = parseInt(hp, -99, 9999, 4)
        local tr = parseInt(tier, 1, 6, 1)
        local el = parseInt(elim, 0, 999, 3)
        local o = parseInt(opp, BP.RESTED, 7, 2)
        local ar = parseInt(arm, 0, 9999, 4)
        local sq = optId(qid)
        local sr = optId(rid)
        local sq2 = optId(q2id)
        local sr2 = optId(r2id)
        if h == nil or tr == nil or el == nil or o == nil or ar == nil or (hid ~= "" and not validId(hid))
            or sq == nil or sr == nil or sq2 == nil or sr2 == nil then
            return nil, "bad seat"
        end
        table.insert(m.lobby, { health = h, tier = tr, alive = alive == "1", elim = el, opp = o,
            hero = hid, armor = ar, quest = sq, reward = sr, quest2 = sq2, reward2 = sr2 })
    end
    local names = splitPipe(kv.nm, 8)
    if not names then
        return nil, "bad nm"
    end
    -- The server's live placing per seat (optional; an old server sends none and the addon works it out).
    if kv.rk ~= nil then
        local rk = splitList(kv.rk, 9)
        if not rk or table.getn(rk) ~= 8 then
            return nil, "bad rk"
        end
        m.rank = {}
        for i = 1, 8 do
            local r = parseInt(rk[i], 1, 8, 1)
            if r == nil then
                return nil, "bad rk"
            end
            m.rank[i] = r
        end
    end
    m.names = names
    m.winner = parseInt(kv.win, -1, 1, 2)
    if m.winner == nil then
        return nil, "bad win"
    end
    return m
end

-- --------------------------------------------------------------------------- BG_COMBAT
-- The event log references cards by an index into `cd` (resolved to the id here); `tr` = 1 means the log was too long
-- for one reply and is left out (the start boards and the survivors are always there).
function BP.ParseCombat(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = P.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local c = { round = 0, ms = 0, opponent = -1, winner = -1, damage = 0, first = 0, timedOut = false, stalled = false,
        start = { {}, {} }, survivors = { {}, {} }, trimmed = false, cards = {}, events = {} }
    local rd = parseInt(kv.rd, 0, 999, 3)
    local ms = parseInt(kv.ms, 0, 1, 1)
    local opp = parseInt(kv.opp, -1, 7, 2)
    local w = parseInt(kv.w, -1, 1, 2)
    local dm = parseInt(kv.dm, 0, 999, 3)
    local fs = parseInt(kv.fs, 0, 1, 1)
    if rd == nil or ms == nil or opp == nil or w == nil or dm == nil or fs == nil then
        return nil, "bad header"
    end
    c.round, c.ms, c.opponent, c.winner, c.damage, c.first = rd, ms, opp, w, dm, fs
    c.timedOut = parseFlag(kv.to)
    c.stalled = parseFlag(kv.stl)
    if c.timedOut == nil or c.stalled == nil then
        return nil, "bad flags"
    end
    c.start[1] = BP.ParseMinions(kv.s0, 7)
    c.start[2] = BP.ParseMinions(kv.s1, 7)
    c.survivors[1] = BP.ParseMinions(kv.v0, 7)
    c.survivors[2] = BP.ParseMinions(kv.v1, 7)
    if not c.start[1] or not c.start[2] or not c.survivors[1] or not c.survivors[2] then
        return nil, "bad boards"
    end
    local tr = parseFlag(kv.tr)
    if tr == nil then
        return nil, "bad tr"
    end
    c.trimmed = tr
    if tr then
        return c
    end
    local cards = splitList(kv.cd, 64)
    if not cards then
        return nil, "bad cd"
    end
    for i = 1, table.getn(cards) do
        if not validId(cards[i]) then
            return nil, "bad card"
        end
        c.cards[i] = cards[i]
    end
    local evs = splitList(kv.ev, 512)
    if not evs then
        return nil, "bad ev"
    end
    for i = 1, table.getn(evs) do
        local e = evs[i]
        local _, _, ty, side, actor, target, amount, amount2, card, golden, position, kw =
            string.find(e, "^(%d+)%.(%d+)%.(%d+)%.(%d+)%.(%-?%d+)%.(%-?%d+)%.(%-?%d+)%.([01])%.(%d+)%.(%d+)$")
        local tyN = parseInt(ty, 0, 7, 1)
        local sideN = parseInt(side, 0, 1, 1)
        local actorN = parseInt(actor, 0, 999999999, 9)
        local targetN = parseInt(target, 0, 999999999, 9)
        local amountN = parseInt(amount, -9999, 9999, 5)
        local amount2N = parseInt(amount2, -9999, 9999, 5)
        local cardN = parseInt(card, -1, 63, 2)
        local posN = parseInt(position, 0, 7, 1)
        local kwN = parseInt(kw, 0, BP.KEYWORD_MAX, 3)
        if tyN == nil or sideN == nil or actorN == nil or targetN == nil or amountN == nil or amount2N == nil
            or cardN == nil or posN == nil or kwN == nil then
            return nil, "bad event"
        end
        local id = nil
        if cardN >= 0 then
            id = c.cards[cardN + 1]
            if not id then
                return nil, "bad event card"
            end
        end
        table.insert(c.events, { type = tyN, side = sideN, actor = actorN, target = targetN, amount = amountN,
            amount2 = amount2N, card = id, golden = golden == "1", position = posN, kw = kwN })
    end
    return c
end

-- --------------------------------------------------------------------------- BG_END
function BP.ParseEnd(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = P.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local e = { winner = -1, myFaction = 0, won = false, placement = 0, overall = 0, alive = false, round = 1,
        timedOut = false, coins = 0, packs = 0, order = {} }
    local win = parseInt(kv.win, -1, 1, 2)
    local myf = parseInt(kv.myf, 0, 1, 1)
    local pf = parseInt(kv.pf, 1, 4, 1)
    local po = parseInt(kv.po, 1, 8, 1)
    local rd = parseInt(kv.rd, 0, 999, 3)
    -- The payout (0 until the payout step).
    local rc = parseInt(kv.rc, 0, 999999, 6)
    local rp = parseInt(kv.rp, 0, 9999, 4)
    if win == nil or myf == nil or pf == nil or po == nil or rd == nil or rc == nil or rp == nil then
        return nil, "bad numbers"
    end
    e.winner, e.myFaction, e.placement, e.overall, e.round = win, myf, pf, po, rd
    e.coins, e.packs = rc, rp
    e.won = parseFlag(kv.won)
    e.alive = parseFlag(kv.al)
    e.timedOut = parseFlag(kv.tm)
    if e.won == nil or e.alive == nil or e.timedOut == nil then
        return nil, "bad flags"
    end
    local order = splitList(kv.pl, 8)
    if not order or table.getn(order) ~= 8 then
        return nil, "bad pl"
    end
    for i = 1, 8 do
        local v = parseInt(order[i], 1, 8, 1)
        if v == nil then
            return nil, "bad placement"
        end
        e.order[i] = v
    end
    return e
end

-- --------------------------------------------------------------------------- BG_ALLY
-- An ally's field (a peek): the seat, name, hero, tier, gold, health, armor, board, hand and hero power state.
function BP.ParseAlly(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = P.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local a = { seat = -1, name = "", hero = "", power = "", tier = 1, gold = 0, health = 0, armor = 0,
        powerCost = 0, powerCounter = 0, board = {}, hand = {} }
    local se = parseInt(kv.se, 0, 7, 1)
    local t = parseInt(kv.t, 1, 6, 1)
    local g = parseInt(kv.g, 0, 999, 3)
    local hp = parseInt(kv.hp, -99, 9999, 4)
    local arm = parseInt(kv.arm, 0, 9999, 4)
    local pwc = parseInt(kv.pwc, -99, 999, 3)
    local pwx = parseInt(kv.pwx, -99, 999, 3)
    if se == nil or t == nil or g == nil or hp == nil or arm == nil or pwc == nil or pwx == nil then
        return nil, "bad numbers"
    end
    local hero = kv.hero or ""
    local power = kv.pw or ""
    if (hero ~= "" and not validId(hero)) or (power ~= "" and not validId(power)) then
        return nil, "bad hero"
    end
    a.seat, a.tier, a.gold, a.health, a.armor = se, t, g, hp, arm
    a.hero, a.power, a.powerCost, a.powerCounter = hero, power, pwc, pwx
    a.name = kv.nm or ""
    a.board = BP.ParseMinions(kv.bd, 7)
    a.hand = BP.ParseMinions(kv.hd, 10)
    if not a.board or not a.hand then
        return nil, "bad minions"
    end
    return a
end

-- ERR payload -> { code, msg, why } (never nil: a missing key reads as "").
function BP.ParseError(payload)
    local kv = type(payload) == "string" and P.KvDecode(payload) or nil
    kv = kv or {}
    return { code = kv.code or "", msg = kv.msg or "", why = kv.why or "" }
end

-- --------------------------------------------------------------------------- actions
-- a.type is "buy" | "sell" | "play" | "cast" | "move" | "refresh" | "freeze" | "upgrade" | "discover" | "end" |
-- "hero" | "quest" | "power" | "send".
-- Fields by type: buy index = offer; sell zone ("board"|"hand") + index; play index = hand slot, slot = board
-- position, uid = target (0 none); cast index = hand slot, uid = spell target (0 none); move index = from, slot = to;
-- freeze flag; discover index = option;
-- hero index = choice; quest index = choice; power uid = friendly target / offer = tavern offer index;
-- send index = hand slot, seat = ally seat.
function BP.EncodeAction(a)
    local kv = {}
    local t = a.type
    if t == "buy" then
        table.insert(kv, { "act", "buy" })
        table.insert(kv, { "i", tostring(a.index) })
    elseif t == "sell" then
        table.insert(kv, { "act", "sell" })
        table.insert(kv, { "z", a.zone == "hand" and "h" or "b" })
        table.insert(kv, { "i", tostring(a.index) })
    elseif t == "play" then
        table.insert(kv, { "act", "play" })
        table.insert(kv, { "i", tostring(a.index) })
        table.insert(kv, { "s", tostring(a.slot) })
        if a.uid and a.uid ~= 0 then
            table.insert(kv, { "u", tostring(a.uid) })
        end
    elseif t == "cast" then
        table.insert(kv, { "act", "cast" })
        table.insert(kv, { "i", tostring(a.index) })
        if a.uid and a.uid ~= 0 then
            table.insert(kv, { "u", tostring(a.uid) })
        end
    elseif t == "move" then
        table.insert(kv, { "act", "move" })
        table.insert(kv, { "f", tostring(a.index) })
        table.insert(kv, { "t", tostring(a.slot) })
    elseif t == "freeze" then
        table.insert(kv, { "act", "freeze" })
        table.insert(kv, { "on", a.flag and "1" or "0" })
    elseif t == "discover" then
        table.insert(kv, { "act", "discover" })
        table.insert(kv, { "i", tostring(a.index) })
    elseif t == "hero" then
        table.insert(kv, { "act", "hero" })
        table.insert(kv, { "i", tostring(a.index) })
    elseif t == "quest" then
        table.insert(kv, { "act", "quest" })
        table.insert(kv, { "i", tostring(a.index) })
    elseif t == "power" then
        table.insert(kv, { "act", "power" })
        if a.uid and a.uid ~= 0 then
            table.insert(kv, { "u", tostring(a.uid) })
        end
        if a.uid2 and a.uid2 ~= 0 then
            table.insert(kv, { "u2", tostring(a.uid2) })   -- Vol'jin's second minion
        end
        if a.offer ~= nil then
            table.insert(kv, { "o", tostring(a.offer) })   -- a tavern offer index; Lord Barov's seat
        end
    elseif t == "send" then
        table.insert(kv, { "act", "send" })
        table.insert(kv, { "i", tostring(a.index) })
        table.insert(kv, { "to", tostring(a.seat) })
    elseif t == "peek" then
        table.insert(kv, { "act", "peek" })
        table.insert(kv, { "to", tostring(a.seat) })
    elseif t == "refresh" or t == "upgrade" or t == "end" then
        table.insert(kv, { "act", t })
    else
        return nil
    end
    return P.KvEncode(kv)
end
