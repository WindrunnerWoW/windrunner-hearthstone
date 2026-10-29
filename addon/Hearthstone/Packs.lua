-- Hearthstone addon: booster packs.
--
-- Pure logic, no WoW frames and no network: the pack state, the strict parsers of the server's
-- PACKS and PACK_OPENED replies, the idempotency key of PACK_OPEN, the one pending opening and
-- the reveal queue. Hearthstone.lua sends PACK_GET / PACK_OPEN and feeds the replies into this
-- file; PacksUI.lua only reads it and flips cards. The server rolls every card and owns the
-- stock: nothing here can open a pack or grant a card by itself.
--
--   PK.state     "none" (not connected), "unsupported" (no "packs" in WELCOME caps), "loading",
--                "ready" or "error"
--   PK.stock     unopened packs, PK.opened packs ever opened (seq of the newest opening)
--   PK.recent    newest first, at most 5: { seq, cards = { 5 ids }, new = { 5 booleans } }
--   PK.pending   the one opening that is not confirmed yet, or nil:
--                { key, attempts, reqId, reqIds = { [reqId] = true }, inFlight, retryIn }
--                The key survives failed attempts (see Hearthstone.lua), so a retry can never
--                open a second pack.
--   PK.queue     reveals, the first one is on screen: { seq, cards, new, flipped, recovered }
--   PK.view      an opening of the recent list shown face up, or nil
--
-- The highest opening the player finished revealing is kept per realm in
-- HearthstoneDB.packsSeen, so an opening whose reveal was cut short (/reload, disconnect) is
-- shown again the next time PACKS arrives.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsPacks = HsPacks or {}
local PK = HsPacks

PK.CARDS = 5                 -- cards per pack
PK.MAX_RECENT = 5            -- openings in PACKS.recent
PK.MAX_STOCK = 100000        -- unopened packs (server bound)
PK.MAX_SEQ = 4294967295      -- INT UNSIGNED on the server (opening numbers, collection version)
PK.MAX_ID_LEN = 32           -- VARCHAR(32) on the server
PK.KEY_LEN = 12              -- length of the keys this addon makes (the server takes 12..16)
PK.KEY_MIN, PK.KEY_MAX = 12, 16
PK.MAX_ATTEMPTS = 3          -- PACK_OPEN sends per click before the addon gives up
PK.TYPE = "classic"

-- WoW item quality colours (r, g, b) and the chat colour codes used in texts.
PK.RARITY_COLORS = {
    COMMON = { 1, 1, 1 },
    RARE = { 0, 0.44, 0.87 },
    EPIC = { 0.64, 0.21, 0.93 },
    LEGENDARY = { 1, 0.5, 0 },
}
PK.RARITY_CODES = { COMMON = "|cffffffff", RARE = "|cff0070dd", EPIC = "|cffa335ee", LEGENDARY = "|cffff8000" }
PK.RARITY_NAMES = { COMMON = "Common", RARE = "Rare", EPIC = "Epic", LEGENDARY = "Legendary" }
PK.RARITY_ORDER = { "LEGENDARY", "EPIC", "RARE", "COMMON" }

PK.memSeen = PK.memSeen or {}   -- packsSeen while there are no saved variables (tests)
PK.keyCounter = PK.keyCounter or 0

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
    return type(id) == "string" and string.find(id, "^[%w_]+$") ~= nil and string.len(id) <= PK.MAX_ID_LEN
end

-- "10010" -> { true, false, false, true, false }, or nil unless exactly five 0/1 digits.
function PK.ParseMask(s)
    if type(s) ~= "string" or string.len(s) ~= PK.CARDS or not string.find(s, "^[01]+$") then
        return nil
    end
    local t = {}
    for i = 1, PK.CARDS do
        t[i] = string.sub(s, i, i) == "1"
    end
    return t
end

function PK.IsValidKey(key)
    return type(key) == "string" and string.len(key) >= PK.KEY_MIN and string.len(key) <= PK.KEY_MAX
        and string.find(key, "^[0-9a-z]+$") ~= nil
end

-- Splits "a,b,c" (no empty items allowed). Returns a list or nil.
local function splitList(s)
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
        if table.getn(out) > 64 then
            return nil
        end
        if not comma then
            return out
        end
        pos = comma + 1
    end
end

-- One opening of PACKS.recent: <seq>.<mask>.<id1>.<id2>.<id3>.<id4>.<id5>
local function parseOpening(s)
    local _, _, seqS, maskS, a, b, c, d, e =
        string.find(s, "^(%d+)%.([01]+)%.([%w_]+)%.([%w_]+)%.([%w_]+)%.([%w_]+)%.([%w_]+)$")
    if not seqS then
        return nil
    end
    local seq = parseNumber(seqS, 10)
    local new = PK.ParseMask(maskS)
    if seq == nil or seq < 1 or seq > PK.MAX_SEQ or new == nil then
        return nil
    end
    local cards = { a, b, c, d, e }
    for i = 1, PK.CARDS do
        if not validId(cards[i]) then
            return nil
        end
    end
    return { seq = seq, cards = cards, new = new }
end

-- Parses a PACKS payload: type=classic;stock=<n>;opened=<m>;recent=<opening>,...
-- Returns { stock, opened, recent } or nil and a reason; one bad opening rejects the reply.
function PK.ParsePacks(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    if kv.type ~= PK.TYPE then
        return nil, "bad type"
    end
    local stock = parseNumber(kv.stock, 6)
    if stock == nil or stock > PK.MAX_STOCK then
        return nil, "bad stock"
    end
    local opened = parseNumber(kv.opened, 10)
    if opened == nil or opened > PK.MAX_SEQ then
        return nil, "bad opened"
    end
    if kv.recent == nil then
        return nil, "missing recent"
    end
    local recent = {}
    if kv.recent ~= "" then
        local items = splitList(kv.recent)
        if not items or table.getn(items) > PK.MAX_RECENT then
            return nil, "bad recent list"
        end
        for i = 1, table.getn(items) do
            local o = parseOpening(items[i])
            if not o then
                return nil, "malformed opening " .. i
            end
            -- newest first: strictly decreasing, none above the opened counter
            if o.seq > opened or (i > 1 and o.seq >= recent[i - 1].seq) then
                return nil, "opening " .. i .. " out of order"
            end
            recent[i] = o
        end
    end
    return { stock = stock, opened = opened, recent = recent }
end

-- Parses a PACK_OPENED payload:
-- id=<seq>;key=<key>;replay=0|1;stock=<n>;ver=<version>;new=<mask>;cards=<id>,<id>,<id>,<id>,<id>
-- Returns { seq, key, replay, stock, ver, new, cards } or nil and a reason.
function PK.ParseOpened(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local seq = parseNumber(kv.id, 10)
    if seq == nil or seq < 1 or seq > PK.MAX_SEQ then
        return nil, "bad id"
    end
    if not PK.IsValidKey(kv.key) then
        return nil, "bad key"
    end
    if kv.replay ~= "0" and kv.replay ~= "1" then
        return nil, "bad replay"
    end
    local stock = parseNumber(kv.stock, 6)
    if stock == nil or stock > PK.MAX_STOCK then
        return nil, "bad stock"
    end
    local ver = parseNumber(kv.ver, 10)
    if ver == nil or ver > PK.MAX_SEQ then
        return nil, "bad ver"
    end
    local new = PK.ParseMask(kv.new)
    if not new then
        return nil, "bad new mask"
    end
    local cards = nil
    if type(kv.cards) == "string" then
        cards = splitList(kv.cards)
    end
    if not cards or table.getn(cards) ~= PK.CARDS then
        return nil, "not five cards"
    end
    for i = 1, PK.CARDS do
        if not validId(cards[i]) then
            return nil, "bad card id " .. i
        end
    end
    return { seq = seq, key = kv.key, replay = kv.replay == "1", stock = stock, ver = ver, new = new, cards = cards }
end

-- True when the WELCOME caps list ("solo,chunk,packs") contains "packs".
function PK.CapsHavePacks(caps)
    if type(caps) ~= "string" then
        return false
    end
    local items = splitList(caps)
    if not items then
        return false
    end
    for i = 1, table.getn(items) do
        if items[i] == "packs" then
            return true
        end
    end
    return false
end

-- ------------------------------------------------------------ idempotency key
local BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz"
local SIX = 2176782336       -- 36^6

local function base36(n, width)
    local s = ""
    for _ = 1, width do
        local d = n - math.floor(n / 36) * 36
        s = string.sub(BASE36, d + 1, d + 1) .. s
        n = math.floor(n / 36)
    end
    return s
end

local function wrap(n, m)
    n = math.floor(n)
    return n - math.floor(n / m) * m
end

-- A fresh 12 character key [0-9a-z]: six characters of the wall clock (seconds) and six mixed
-- from math.random, the millisecond clock and a per-session counter. It only has to be unique
-- per account; the server keeps every key it has seen.
function PK.NewKey()
    PK.keyCounter = PK.keyCounter + 1
    local wall = 0
    if time then
        wall = time() or 0
    elseif os and os.time then
        wall = os.time()
    end
    local ms = 0
    if GetTime then
        ms = math.floor((GetTime() or 0) * 1000)
    end
    local r = math.random(0, 46655) * 46656 + wrap(ms + PK.keyCounter * 7919 + math.random(0, 46655), 46656)
    return base36(wrap(wall, SIX), 6) .. base36(wrap(r, SIX), 6)
end

-- ------------------------------------------------------------------ state
local function clearData()
    PK.stock = 0
    PK.opened = 0
    PK.recent = {}
end

-- Not connected. The reveal queue stays (those cards are already in the collection) and an
-- unconfirmed key is kept, so the next click after a reconnect reuses it.
function PK.Reset()
    PK.state = "none"
    PK.errorText = nil
    clearData()
    if PK.pending then
        PK.pending.inFlight = false
        PK.pending.retryIn = nil
        PK.pending.reqId = nil
    end
end

function PK.SetUnsupported()
    PK.state = "unsupported"
    PK.errorText = nil
    clearData()
end

-- A PACK_GET is on its way. A refetch while ready keeps the current data on screen.
function PK.SetLoading()
    if PK.state ~= "ready" then
        PK.state = "loading"
        PK.errorText = nil
        clearData()
    end
end

function PK.SetReady(res)
    PK.state = "ready"
    PK.errorText = nil
    PK.stock = res.stock
    PK.opened = res.opened
    PK.recent = res.recent
end

function PK.SetError(text)
    PK.state = "error"
    PK.errorText = text
    clearData()
end

function PK.IsReady()
    return PK.state == "ready"
end

-- A confirmed opening (PACK_OPENED): stock, counter and the recent list follow it.
function PK.ApplyOpened(res)
    PK.stock = res.stock
    if res.seq > (PK.opened or 0) then
        PK.opened = res.seq
    end
    local list = { { seq = res.seq, cards = res.cards, new = res.new } }
    for i = 1, table.getn(PK.recent or {}) do
        local o = PK.recent[i]
        if o.seq ~= res.seq and table.getn(list) < PK.MAX_RECENT then
            table.insert(list, o)
        end
    end
    table.sort(list, function(a, b) return a.seq > b.seq end)
    PK.recent = list
end

-- ---------------------------------------------------------- pending open
function PK.IsOpening()
    return PK.pending ~= nil and PK.pending.inFlight == true
end

-- Starts (or restarts) the opening: an unconfirmed key from an earlier click is reused.
function PK.BeginOpen()
    local p = PK.pending
    if not p then
        p = { key = PK.NewKey(), reqIds = {} }
        PK.pending = p
    end
    p.attempts = 0
    p.inFlight = true
    p.retryIn = nil
    p.reqId = nil
    return p
end

function PK.ClearPending()
    PK.pending = nil
end

-- ------------------------------------------------------------ seen marker
local function realm()
    local r = nil
    if GetRealmName then
        r = GetRealmName()
    end
    if type(r) ~= "string" or r == "" then
        r = "?"
    end
    return r
end

local function seenStore()
    local db = Hearthstone and Hearthstone.db
    if type(db) == "table" then
        if type(db.packsSeen) ~= "table" then
            db.packsSeen = {}
        end
        return db.packsSeen
    end
    return PK.memSeen
end

-- Highest opening the player finished revealing on this realm, or nil (fresh install).
function PK.GetSeen()
    local v = seenStore()[realm()]
    if type(v) == "number" then
        return v
    end
    return nil
end

function PK.SetSeen(n)
    seenStore()[realm()] = n
end

-- Raises the marker (never lowers it).
function PK.MarkSeen(seq)
    local cur = PK.GetSeen()
    if cur == nil or seq > cur then
        PK.SetSeen(seq)
    end
end

-- ----------------------------------------------------------------- reveal
function PK.Current()
    return PK.queue[1]
end

function PK.IsQueued(seq)
    for i = 1, table.getn(PK.queue) do
        if PK.queue[i].seq == seq then
            return true
        end
    end
    return false
end

-- Adds a reveal (face down) unless that opening is queued already or was revealed before.
-- Returns true when it was added.
function PK.Enqueue(opening, recovered)
    if PK.IsQueued(opening.seq) then
        return false
    end
    local seen = PK.GetSeen()
    if seen ~= nil and opening.seq <= seen then
        return false
    end
    local r = { seq = opening.seq, cards = {}, new = {}, flipped = {}, recovered = recovered and true or false }
    for i = 1, PK.CARDS do
        r.cards[i] = opening.cards[i]
        r.new[i] = opening.new[i] and true or false
        r.flipped[i] = false
    end
    table.insert(PK.queue, r)
    table.sort(PK.queue, function(a, b) return a.seq < b.seq end)
    PK.view = nil
    return true
end

-- PACKS arrived: queues the recent openings the player has not finished looking at (oldest
-- first). On a fresh install (no marker for this realm) the marker starts at 'opened', so
-- there is nothing to recover. A marker above 'opened' (the server's data was reset) is lowered.
-- Returns the number of reveals added.
function PK.Recover()
    local seen = PK.GetSeen()
    if seen == nil or seen > PK.opened then
        PK.SetSeen(PK.opened)
        return 0
    end
    local added = 0
    for i = table.getn(PK.recent), 1, -1 do
        local o = PK.recent[i]
        if o.seq > seen and PK.Enqueue(o, true) then
            added = added + 1
        end
    end
    return added
end

function PK.Flip(i)
    local r = PK.Current()
    if not r or not r.cards[i] or r.flipped[i] then
        return false
    end
    r.flipped[i] = true
    return true
end

-- Flips every card still face down. Returns how many were flipped.
function PK.FlipAll()
    local r = PK.Current()
    local n = 0
    if not r then
        return 0
    end
    for i = 1, PK.CARDS do
        if not r.flipped[i] then
            r.flipped[i] = true
            n = n + 1
        end
    end
    return n
end

function PK.AllFlipped()
    local r = PK.Current()
    if not r then
        return false
    end
    for i = 1, PK.CARDS do
        if not r.flipped[i] then
            return false
        end
    end
    return true
end

-- Ends the reveal on screen (all cards must be face up), marks it seen and moves on to the
-- next queued one. Returns true when a reveal was finished.
function PK.FinishReveal()
    local r = PK.Current()
    if not r or not PK.AllFlipped() then
        return false
    end
    PK.MarkSeen(r.seq)
    table.remove(PK.queue, 1)
    return true
end

-- Shows opening 'index' of the recent list face up (not while a reveal is on screen).
function PK.View(index)
    if PK.Current() then
        return false
    end
    local o = PK.recent[index]
    if not o then
        return false
    end
    PK.view = o
    return true
end

function PK.CloseView()
    PK.view = nil
end

-- -------------------------------------------------------------- rarities
-- "COMMON", "RARE", "EPIC" or "LEGENDARY" of a card id; Basic and unknown ids are Common.
function PK.RarityOf(id)
    local db = HS_CardDB
    local card = nil
    if type(db) == "table" and type(db.cards) == "table" then
        card = db.cards[id]
    end
    local r = card and card.rarity
    if r == "RARE" or r == "EPIC" or r == "LEGENDARY" then
        return r
    end
    return "COMMON"
end

-- "1 Legendary, 1 Rare, 3 Common" (coloured, rarest first) for five card ids.
function PK.RaritySummary(cards)
    local counts = {}
    for i = 1, table.getn(cards) do
        local r = PK.RarityOf(cards[i])
        counts[r] = (counts[r] or 0) + 1
    end
    local parts = {}
    for i = 1, table.getn(PK.RARITY_ORDER) do
        local r = PK.RARITY_ORDER[i]
        if counts[r] then
            table.insert(parts, PK.RARITY_CODES[r] .. counts[r] .. " " .. PK.RARITY_NAMES[r] .. "|r")
        end
    end
    return table.concat(parts, ", ")
end

-- A row of the recent list: "#7  1 Legendary, 1 Rare, 3 Common".
function PK.RecentText(opening)
    return "#" .. opening.seq .. "  " .. PK.RaritySummary(opening.cards)
end

-- One line for /hs status.
function PK.StatusText()
    local s = PK.state
    local text
    if s == "ready" then
        text = "packs: ready, " .. PK.stock .. " unopened, " .. PK.opened .. " opened"
    elseif s == "loading" then
        text = "packs: loading"
    elseif s == "error" then
        text = "packs: error (" .. tostring(PK.errorText or "?") .. ")"
    elseif s == "unsupported" then
        text = "packs: not supported by this server"
    else
        text = "packs: none (not connected)"
    end
    local p = PK.pending
    if p and p.inFlight then
        text = text .. ", opening (attempt " .. (p.attempts or 0) .. " of " .. PK.MAX_ATTEMPTS .. ")"
    elseif p then
        text = text .. ", an unconfirmed opening will be retried with the same key"
    end
    if table.getn(PK.queue) > 0 then
        text = text .. ", " .. table.getn(PK.queue) .. " to reveal"
    end
    return text
end

if PK.state == nil then
    PK.queue = {}
    PK.view = nil
    PK.pending = nil
    PK.message = nil
    PK.Reset()
end
