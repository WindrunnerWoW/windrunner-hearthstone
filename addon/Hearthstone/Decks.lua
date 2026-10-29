-- Hearthstone addon: decks.
--
-- Pure logic, no WoW frames and no network: the deck state, the strict parsers of the server's
-- DECKS, DECK_SAVED and DECK_DELETED replies, the mirror of the server's deck rules (hard rules
-- and draft states), the one deck draft the editor works on, and the payloads and idempotency
-- keys of DECK_SAVE / DECK_DELETE. Hearthstone.lua sends DECK_LIST / DECK_SAVE / DECK_DELETE and
-- feeds the replies into this file; DecksUI.lua only reads it and edits the draft. The server is
-- the judge of every rule: the mirror here only explains things early and never replaces it.
--
--   D.state      "none" (not connected), "unsupported" (no "decks" in WELCOME caps), "loading",
--                "ready" or "error"
--   D.decks      the account's decks ordered by id: { id, rev, class, origin, name, cards =
--                { [cardId] = qty }, count, serverState = { [code] = true } }; D.byId[id] the same
--   D.cap        the account's saved-deck cap; D.start the character's starting class (or nil);
--                D.unlocked = { [CLASS] = true } the account's class unlocks
--   D.draft      the deck the editor works on, or nil: { deckId (0 = new), baseRev, class, name,
--                cards, orig = { name, cards }, createKey, rebase }. It lives in memory only and
--                survives closing the window and switching tabs.
--   D.op         the one save / delete that is not confirmed yet, or nil:
--                { kind ("save", "rename" or "delete"), key, payload, deckId, draft, name, cards,
--                attempts, reqId, reqIds = { [reqId] = true }, retryIn }
--   D.message    the last result to show in the Decks tab (saved, refused, not confirmed, ...)
--
-- Draft states (size, owned, invalid, locked) are computed live from HsOwnership and the access
-- data of the last DECKS reply; the server's s<i> is only used for "owned" while the collection is
-- not loaded.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsDecks = HsDecks or {}
local D = HsDecks
local CM = HsCollection
local O = HsOwnership

D.CLASSES = { "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR" }
D.DECK_SIZE = 30
D.MAX_COPIES = 2              -- ordinary cards
D.MAX_LEGENDARY = 1
D.NAME_MAX_CHARS = 24
D.NAME_MAX_BYTES = 64
D.MAX_CAP = 40                -- sanity bound of DECKS cap= (Hearthstone.Deck.MaxSaved is 1..20)
D.MAX_LIST = 200              -- decks in one DECKS reply (starter awards may exceed the cap)
D.MAX_NUM = 4294967295        -- INT UNSIGNED on the server (deck ids, revisions)
D.MAX_ID_LEN = 32             -- card ids, VARCHAR(32)
D.MAX_ORIGIN_LEN = 16
D.MAX_ATTEMPTS = 3            -- sends of one save / delete before the addon gives up
D.STATE_ORDER = { "size", "owned", "invalid", "locked" }

local IS_CLASS = {}
for i = 1, table.getn(D.CLASSES) do
    IS_CLASS[D.CLASSES[i]] = true
end
local STATE_INDEX = {}
for i = 1, table.getn(D.STATE_ORDER) do
    STATE_INDEX[D.STATE_ORDER[i]] = i
end

-- --------------------------------------------------------------- helpers
function D.IsClass(c)
    return type(c) == "string" and IS_CLASS[c] == true
end

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

-- Splits "a,b,c" (no empty items allowed). Returns a list or nil.
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

-- Byte order comparison (string '<' follows the C locale only by accident).
local function byteLess(a, b)
    local la, lb = string.len(a), string.len(b)
    local n = la
    if lb < n then
        n = lb
    end
    for i = 1, n do
        local x, y = string.byte(a, i), string.byte(b, i)
        if x ~= y then
            return x < y
        end
    end
    return la < lb
end
D.ByteLess = byteLess

local function copyCards(cards)
    local t = {}
    for id, q in pairs(cards or {}) do
        t[id] = q
    end
    return t
end
D.CopyCards = copyCards

local function sameCards(a, b)
    for id, q in pairs(a) do
        if b[id] ~= q then
            return false
        end
    end
    for id, q in pairs(b) do
        if a[id] ~= q then
            return false
        end
    end
    return true
end
D.SameCards = sameCards

-- True when the WELCOME caps list ("solo,chunk,packs,decks") contains "decks".
function D.CapsHaveDecks(caps)
    if type(caps) ~= "string" then
        return false
    end
    local items = splitList(caps, 64)
    if not items then
        return false
    end
    for i = 1, table.getn(items) do
        if items[i] == "decks" then
            return true
        end
    end
    return false
end

-- ------------------------------------------------------------------ names
-- Number of UTF-8 code points of s, or nil when s is not valid UTF-8 (overlong forms and surrogates
-- are invalid). 4-byte sequences (above U+FFFF) are valid UTF-8 but refused, like on the server:
-- its database connection is utf8mb3 and could not store them.
function D.Utf8Length(s)
    local n = string.len(s)
    local i = 1
    local count = 0
    while i <= n do
        local c = string.byte(s, i)
        local need, lo, hi = 0, 128, 191
        if c < 128 then
            need = 0
        elseif c >= 194 and c <= 223 then
            need = 1
        elseif c == 224 then
            need, lo = 2, 160
        elseif (c >= 225 and c <= 236) or c == 238 or c == 239 then
            need = 2
        elseif c == 237 then
            need, hi = 2, 159
        else
            return nil
        end
        if i + need > n then
            return nil
        end
        for k = 1, need do
            local b = string.byte(s, i + k)
            if k == 1 then
                if b < lo or b > hi then
                    return nil
                end
            elseif b < 128 or b > 191 then
                return nil
            end
        end
        i = i + need + 1
        count = count + 1
    end
    return count
end

-- Removes leading and trailing spaces (only ' ', like the server).
function D.TrimName(s)
    s = tostring(s or "")
    local a, b = 1, string.len(s)
    while a <= b and string.sub(s, a, a) == " " do
        a = a + 1
    end
    while b >= a and string.sub(s, b, b) == " " do
        b = b - 1
    end
    return string.sub(s, a, b)
end

-- The name the server would store, or nil and why not: after trimming 1..24 characters (UTF-8
-- code points), at most 64 bytes, valid UTF-8, no control byte (< 0x20, 0x7f) and no '|'.
function D.ValidateName(raw)
    if type(raw) ~= "string" then
        return nil, "no name"
    end
    local name = D.TrimName(raw)
    if name == "" then
        return nil, "the name is empty"
    end
    if string.len(name) > D.NAME_MAX_BYTES then
        return nil, "the name is too long"
    end
    for i = 1, string.len(name) do
        local b = string.byte(name, i)
        if b < 32 or b == 127 then
            return nil, "the name contains a control character"
        end
    end
    if string.find(name, "|", 1, true) then
        return nil, "the name may not contain |"
    end
    local len = D.Utf8Length(name)
    if not len then
        return nil, "the name is not valid text"
    end
    if len > D.NAME_MAX_CHARS then
        return nil, "the name is longer than " .. D.NAME_MAX_CHARS .. " characters"
    end
    return name
end

-- ------------------------------------------------------------------ rules
function D.GetCard(id)
    local db = CM.GetDB()
    if db and id ~= nil then
        return db.cards[id]
    end
    return nil
end

-- Hard card rule of the server for one card in a deck of 'class': nil, "card" (unknown, not
-- collectible or not obtainable) or "cardclass" (neither the deck class nor Neutral).
function D.CardProblem(id, class)
    local c = D.GetCard(id)
    if not c or c.coll ~= 1 or (c.acq ~= "standard" and c.acq ~= "reward_only" and c.acq ~= "quest") then
        return "card"
    end
    local cc = c.class or "NEUTRAL"
    if cc ~= "NEUTRAL" and cc ~= class then
        return "cardclass"
    end
    return nil
end

function D.MaxCopies(id)
    local c = D.GetCard(id)
    if c and c.rarity == "LEGENDARY" then
        return D.MAX_LEGENDARY
    end
    return D.MAX_COPIES
end

function D.Count(cards)
    local n = 0
    for _, q in pairs(cards or {}) do
        n = n + q
    end
    return n
end

-- The first hard rule a save of (class, name, cards) breaks, in the server's order, or nil:
-- "class", "name", "card", "cardclass", "copies" or "size".
function D.CheckHard(class, name, cards)
    if not D.IsClass(class) then
        return "class"
    end
    if not D.ValidateName(name) then
        return "name"
    end
    local ids = {}
    for id in pairs(cards) do
        table.insert(ids, id)
    end
    table.sort(ids, byteLess)
    for i = 1, table.getn(ids) do
        local p = D.CardProblem(ids[i], class)
        if p == "card" then
            return "card"
        end
    end
    for i = 1, table.getn(ids) do
        if D.CardProblem(ids[i], class) == "cardclass" then
            return "cardclass"
        end
    end
    for i = 1, table.getn(ids) do
        local q = cards[ids[i]]
        if q < 1 or q > D.MaxCopies(ids[i]) then
            return "copies"
        end
    end
    if D.Count(cards) > D.DECK_SIZE then
        return "size"
    end
    return nil
end

-- True when the asking character may use decks of 'class' (starting class or an account unlock).
function D.CanPlay(class)
    return class ~= nil and (class == D.start or D.unlocked[class] == true)
end

-- Live state of a deck (or the draft). serverState is the server's s<i> (a set) or nil.
-- Returns { count, missing, size, owned, invalid, locked, ownedKnown }.
function D.Evaluate(class, cards, serverState)
    local r = { count = 0, missing = 0, size = false, owned = false, invalid = false, locked = false }
    local ready = O.IsReady()
    r.ownedKnown = ready
    if not D.IsClass(class) then
        r.invalid = true
    end
    for id, q in pairs(cards or {}) do
        r.count = r.count + q
        if D.CardProblem(id, class) or q < 1 or q > D.MaxCopies(id) then
            r.invalid = true
        end
        if ready then
            local e = O.Get(id)
            local have = (e and e.total) or 0
            if have < q then
                r.missing = r.missing + (q - have)
            end
        end
    end
    if r.count > D.DECK_SIZE then
        r.invalid = true
    end
    r.size = r.count < D.DECK_SIZE
    if ready then
        r.owned = r.missing > 0
    else
        r.owned = serverState ~= nil and serverState.owned == true
    end
    if D.state == "ready" then
        r.locked = not D.CanPlay(class)
    else
        r.locked = serverState ~= nil and serverState.locked == true
    end
    return r
end

-- "ok" or the comma list of the codes present (size,owned,invalid,locked).
function D.StateString(r)
    local out = {}
    for i = 1, table.getn(D.STATE_ORDER) do
        local k = D.STATE_ORDER[i]
        if r[k] then
            table.insert(out, k)
        end
    end
    if table.getn(out) == 0 then
        return "ok"
    end
    return table.concat(out, ",")
end

-- Complete = none of size / owned / invalid (locked is character specific).
function D.IsComplete(r)
    return not r.size and not r.owned and not r.invalid
end

function D.ClassLabel(class)
    return CM.ClassLabel(class or "?")
end

-- Lines that explain a state: list of { text, r, g, b } (complete decks get a green "Complete").
function D.ReasonLines(r, class, charName)
    local lines = {}
    if r.size then
        table.insert(lines, { "Incomplete: " .. r.count .. "/" .. D.DECK_SIZE, 1, 0.82, 0 })
    end
    if r.owned then
        local text
        if r.ownedKnown and r.missing == 1 then
            text = "Missing 1 card you do not own"
        elseif r.ownedKnown then
            text = "Missing " .. r.missing .. " cards you do not own"
        else
            text = "Missing cards you do not own"
        end
        table.insert(lines, { text, 1, 0.33, 0.33 })
    end
    if r.invalid then
        table.insert(lines, { "Contains cards a " .. D.ClassLabel(class) .. " deck cannot hold", 1, 0.33, 0.33 })
    end
    if D.IsComplete(r) then
        table.insert(lines, 1, { "Complete", 0.2, 1, 0.2 })
    end
    if r.locked then
        table.insert(lines, { "Locked for " .. tostring(charName or "this character") .. ": "
            .. D.ClassLabel(class) .. " is not unlocked", 0.6, 0.6, 0.6 })
    end
    return lines
end

local function hex(v)
    local n = math.floor(v * 255 + 0.5)
    local digits = "0123456789abcdef"
    local hi = math.floor(n / 16)
    local lo = n - hi * 16
    return string.sub(digits, hi + 1, hi + 1) .. string.sub(digits, lo + 1, lo + 1)
end

-- One coloured line for a deck list row: the reasons joined with "; ".
function D.RowStatus(r, class, charName)
    local lines = D.ReasonLines(r, class, charName)
    local parts = {}
    for i = 1, table.getn(lines) do
        local l = lines[i]
        table.insert(parts, "|cff" .. hex(l[2]) .. hex(l[3]) .. hex(l[4]) .. l[1] .. "|r")
    end
    return table.concat(parts, "; ")
end

-- ---------------------------------------------------------------- parsing
-- "ok" or a comma list in the order size,owned,invalid,locked -> set, or nil.
function D.ParseState(s)
    if s == "ok" then
        return {}
    end
    if type(s) ~= "string" or s == "" then
        return nil
    end
    local items = splitList(s, 8)
    if not items then
        return nil
    end
    local set = {}
    local last = 0
    for i = 1, table.getn(items) do
        local idx = STATE_INDEX[items[i]]
        if not idx or idx <= last then
            return nil
        end
        last = idx
        set[items[i]] = true
    end
    return set
end

-- "<cardId>.<qty>,..." (may be empty) -> { [id] = qty }, count; or nil and a reason. Every
-- quantity must be 1 or 2 (a stored deck never holds more) and the total at most 30.
function D.ParseCards(s)
    if type(s) ~= "string" then
        return nil, "missing cards"
    end
    local cards = {}
    if s == "" then
        return cards, 0
    end
    local items = splitList(s, D.DECK_SIZE)
    if not items then
        return nil, "bad card list"
    end
    local count = 0
    for i = 1, table.getn(items) do
        local _, _, id, q = string.find(items[i], "^([%w_]+)%.(%d+)$")
        if not id or string.len(id) > D.MAX_ID_LEN then
            return nil, "malformed card entry " .. i
        end
        local qty = parseNumber(q, 1)
        if qty == nil or qty < 1 or qty > D.MAX_COPIES then
            return nil, "bad quantity for " .. id
        end
        if cards[id] then
            return nil, "duplicate card " .. id
        end
        cards[id] = qty
        count = count + qty
    end
    if count > D.DECK_SIZE then
        return nil, "more than " .. D.DECK_SIZE .. " cards"
    end
    return cards, count
end

-- { [id] = qty } -> "<cardId>.<qty>,..." sorted by id in byte order (the server's storage order).
function D.EncodeCards(cards)
    local ids = {}
    for id in pairs(cards or {}) do
        table.insert(ids, id)
    end
    table.sort(ids, byteLess)
    local out = {}
    for i = 1, table.getn(ids) do
        out[i] = ids[i] .. "." .. cards[ids[i]]
    end
    return table.concat(out, ",")
end

local function validOrigin(s)
    return type(s) == "string" and string.len(s) >= 1 and string.len(s) <= D.MAX_ORIGIN_LEN
        and string.find(s, "^[a-z_]+$") ~= nil
end

-- Parses a DECKS payload:
-- cap=<max>;n=<k>;start=<CLASS|->;unlocked=<CLASS,...>;d<i>=<id>.<rev>.<CLASS>.<origin>;m<i>=<name>;
-- c<i>=<cards>;s<i>=<state>  (i = 1..n, ordered by id). Returns { cap, start, unlocked, decks } or
-- nil and a reason; one bad deck rejects the whole reply.
function D.ParseDecks(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local cap = parseNumber(kv.cap, 3)
    if cap == nil or cap < 1 or cap > D.MAX_CAP then
        return nil, "bad cap"
    end
    local n = parseNumber(kv.n, 3)
    if n == nil or n > D.MAX_LIST then
        return nil, "bad n"
    end
    local start = nil
    if kv.start ~= "-" then
        if not D.IsClass(kv.start) then
            return nil, "bad start"
        end
        start = kv.start
    end
    if kv.unlocked == nil then
        return nil, "missing unlocked"
    end
    local unlocked = {}
    if kv.unlocked ~= "" then
        local items = splitList(kv.unlocked, table.getn(D.CLASSES))
        if not items then
            return nil, "bad unlocked list"
        end
        for i = 1, table.getn(items) do
            if not D.IsClass(items[i]) or unlocked[items[i]] then
                return nil, "bad unlocked class"
            end
            unlocked[items[i]] = true
        end
    end
    -- no deck keys beyond n
    for k in pairs(kv) do
        local _, _, idx = string.find(k, "^[dmcs](%d+)$")
        local i = idx and parseNumber(idx, 4)
        if idx and (i == nil or i < 1 or i > n) then
            return nil, "deck key " .. k .. " beyond n"
        end
    end
    local decks = {}
    local lastId = 0
    for i = 1, n do
        local d = kv["d" .. i]
        if type(d) ~= "string" then
            return nil, "missing d" .. i
        end
        local _, _, idS, revS, class, origin = string.find(d, "^(%d+)%.(%d+)%.([A-Z]+)%.([a-z_]+)$")
        if not idS then
            return nil, "malformed d" .. i
        end
        local id = parseNumber(idS, 10)
        local rev = parseNumber(revS, 10)
        if id == nil or id < 1 or id > D.MAX_NUM or rev == nil or rev > D.MAX_NUM then
            return nil, "bad id or rev in d" .. i
        end
        if id <= lastId then
            return nil, "deck " .. i .. " out of order"
        end
        lastId = id
        if not D.IsClass(class) then
            return nil, "bad class in d" .. i
        end
        if not validOrigin(origin) then
            return nil, "bad origin in d" .. i
        end
        local name = D.ValidateName(kv["m" .. i])
        if not name then
            return nil, "bad name m" .. i
        end
        local cards, count = D.ParseCards(kv["c" .. i])
        if not cards then
            return nil, "c" .. i .. ": " .. tostring(count)
        end
        local state = D.ParseState(kv["s" .. i])
        if not state then
            return nil, "bad state s" .. i
        end
        decks[i] = { id = id, rev = rev, class = class, origin = origin, name = name, cards = cards,
            count = count, serverState = state }
    end
    return { cap = cap, start = start, unlocked = unlocked, decks = decks }
end

-- Parses a DECK_SAVED payload: key=<key>;id=<id>;rev=<rev>;replay=0|1;state=<state>
-- (state may be "gone" on a replay of a deck that was deleted since).
function D.ParseSaved(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    if not HsPacks.IsValidKey(kv.key) then
        return nil, "bad key"
    end
    local id = parseNumber(kv.id, 10)
    if id == nil or id < 1 or id > D.MAX_NUM then
        return nil, "bad id"
    end
    local rev = parseNumber(kv.rev, 10)
    if rev == nil or rev > D.MAX_NUM then
        return nil, "bad rev"
    end
    if kv.replay ~= "0" and kv.replay ~= "1" then
        return nil, "bad replay"
    end
    local gone = false
    local state = nil
    if kv.state == "gone" and kv.replay == "1" then
        gone = true
    else
        state = D.ParseState(kv.state)
        if not state then
            return nil, "bad state"
        end
    end
    return { key = kv.key, id = id, rev = rev, replay = kv.replay == "1", gone = gone, state = state }
end

-- Parses a DECK_DELETED payload: key=<key>;id=<id>;replay=0|1
function D.ParseDeleted(payload)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    if not HsPacks.IsValidKey(kv.key) then
        return nil, "bad key"
    end
    local id = parseNumber(kv.id, 10)
    if id == nil or id < 1 or id > D.MAX_NUM then
        return nil, "bad id"
    end
    if kv.replay ~= "0" and kv.replay ~= "1" then
        return nil, "bad replay"
    end
    return { key = kv.key, id = id, replay = kv.replay == "1" }
end

-- ------------------------------------------------------------------ state
local function clearData()
    D.cap = 0
    D.decks = {}
    D.byId = {}
    D.start = nil
    D.unlocked = {}
end

-- Not connected. The draft stays in memory (saving waits for the connection).
function D.Reset()
    D.state = "none"
    D.errorText = nil
    clearData()
end

function D.SetUnsupported()
    D.state = "unsupported"
    D.errorText = nil
    clearData()
end

-- A DECK_LIST is on its way. A refetch while ready keeps the current data on screen.
function D.SetLoading()
    if D.state ~= "ready" then
        D.state = "loading"
        D.errorText = nil
        clearData()
    end
end

function D.SetReady(res)
    D.state = "ready"
    D.errorText = nil
    D.cap = res.cap
    D.start = res.start
    D.unlocked = res.unlocked
    D.decks = res.decks
    D.byId = {}
    for i = 1, table.getn(res.decks) do
        D.byId[res.decks[i].id] = res.decks[i]
    end
    D.SyncDraft()
end

function D.SetError(text)
    D.state = "error"
    D.errorText = text
    clearData()
end

function D.IsReady()
    return D.state == "ready"
end

function D.DeckCount()
    return table.getn(D.decks or {})
end

-- A confirmed save: the list shows it at once (the refetch that follows brings the truth).
function D.ApplySaved(op, res)
    local d = D.byId[res.id]
    if res.gone then
        return
    end
    if not d then
        d = { id = res.id, origin = "player" }
        table.insert(D.decks, d)
        table.sort(D.decks, function(a, b) return a.id < b.id end)
        D.byId[res.id] = d
    end
    d.rev = res.rev
    d.class = op.class
    d.name = op.name
    d.cards = copyCards(op.cards)
    d.count = D.Count(d.cards)
    d.serverState = res.state or {}
end

function D.ApplyDeleted(id)
    if not D.byId[id] then
        return
    end
    D.byId[id] = nil
    for i = 1, table.getn(D.decks) do
        if D.decks[i].id == id then
            table.remove(D.decks, i)
            break
        end
    end
end

-- ----------------------------------------------------------------- default
-- The default deck (middle click in the Decks tab) is a preference of this WoW account, kept in
-- HearthstoneDB.defaultDeck like the other UI settings. The ladder selects it and lists it first.
-- A default that is no longer a saved deck counts as no default.
function D.DefaultId()
    local id = HearthstoneDB and HearthstoneDB.defaultDeck
    if id and D.byId and D.byId[id] then
        return id
    end
    return nil
end

-- Makes deck 'id' the default, or clears the default when it already is. False for an unknown deck.
function D.ToggleDefault(id)
    if not HearthstoneDB or not D.byId or not D.byId[id] then
        return false
    end
    if HearthstoneDB.defaultDeck == id then
        HearthstoneDB.defaultDeck = nil
    else
        HearthstoneDB.defaultDeck = id
    end
    return true
end

-- "<Name> can play: Mage (starting class), Warrior (unlocked)" from the last DECKS reply.
function D.AccessText(charName)
    local parts = {}
    if D.start then
        table.insert(parts, D.ClassLabel(D.start) .. " (starting class)")
    end
    for i = 1, table.getn(D.CLASSES) do
        local c = D.CLASSES[i]
        if D.unlocked[c] and c ~= D.start then
            table.insert(parts, D.ClassLabel(c) .. " (unlocked)")
        end
    end
    local who = tostring(charName or "This character")
    if table.getn(parts) == 0 then
        return who .. " has no class unlocked yet."
    end
    return who .. " can play: " .. table.concat(parts, ", ")
end

-- ------------------------------------------------------------------ draft
function D.DefaultName(class)
    return "Custom " .. D.ClassLabel(class)
end

local function newDraft(deckId, rev, class, name, cards)
    return {
        deckId = deckId, baseRev = rev, class = class, name = name, cards = copyCards(cards),
        orig = { name = name, cards = copyCards(cards) }, createKey = nil, rebase = false,
    }
end

-- Starts a new deck of 'class' (any of the nine classes, also a locked one).
function D.BeginNew(class)
    if not D.IsClass(class) then
        return nil
    end
    D.draft = newDraft(0, 0, class, D.DefaultName(class), {})
    D.addReason = nil
    return D.draft
end

-- Starts editing a copy of a saved deck.
function D.BeginEdit(deck)
    if type(deck) ~= "table" then
        return nil
    end
    D.draft = newDraft(deck.id, deck.rev, deck.class, deck.name, deck.cards)
    D.addReason = nil
    return D.draft
end

function D.EndEdit()
    D.draft = nil
    D.addReason = nil
end

function D.IsEditing()
    return D.draft ~= nil
end

-- True when the draft holds something that is not saved.
function D.IsDirty(draft)
    draft = draft or D.draft
    if not draft then
        return false
    end
    if D.TrimName(draft.name) ~= draft.orig.name then
        return true
    end
    return not sameCards(draft.cards, draft.orig.cards)
end

-- The deck list changed: follow a rebase after a conflict, notice a deck deleted elsewhere.
function D.SyncDraft()
    local dr = D.draft
    if not dr or dr.deckId == 0 then
        return
    end
    local d = D.byId[dr.deckId]
    if not d then
        dr.deckId = 0
        dr.baseRev = 0
        D.ClearCreate(dr)
        dr.rebase = false
        dr.orig = { name = "", cards = {} }
        D.message = "This deck was deleted elsewhere. Save creates it again as a new deck."
    elseif dr.rebase then
        dr.baseRev = d.rev
        dr.rebase = false
    end
end

-- Adds one copy of a card to the draft. Returns true, or false and the reason (shown in the tray).
function D.AddCard(id)
    local dr = D.draft
    if not dr then
        return false, "no deck is being edited"
    end
    if D.op then
        return false, "wait until the deck is saved"
    end
    local p = D.CardProblem(id, dr.class)
    if p == "card" then
        return false, "not collectible"
    elseif p == "cardclass" then
        return false, "wrong class"
    end
    local have = dr.cards[id] or 0
    if D.Count(dr.cards) >= D.DECK_SIZE then
        return false, "deck is full"
    end
    local max = D.MaxCopies(id)
    if have >= max then
        if max == 1 then
            return false, "Legendary: only 1 copy"
        end
        return false, "only " .. max .. " copies"
    end
    if not O.IsReady() then
        return false, "your collection is not loaded"
    end
    local e = O.Get(id)
    local owned = (e and e.total) or 0
    if have >= owned then
        if owned == 0 then
            return false, "you do not own this card"
        elseif owned == 1 then
            return false, "you own only 1 copy"
        end
        return false, "you own only " .. owned .. " copies"
    end
    dr.cards[id] = have + 1
    return true
end

-- Removes one copy. Returns true, or false and the reason.
function D.RemoveCard(id)
    local dr = D.draft
    if not dr then
        return false, "no deck is being edited"
    end
    if D.op then
        return false, "wait until the deck is saved"
    end
    local have = dr.cards[id] or 0
    if have <= 0 then
        return false, "not in the deck"
    end
    if have == 1 then
        dr.cards[id] = nil
    else
        dr.cards[id] = have - 1
    end
    return true
end

function D.SetDraftName(name)
    if D.draft then
        D.draft.name = tostring(name or "")
    end
end

-- Copies of 'id' in the draft (0 without a draft).
function D.InDraft(id)
    local dr = D.draft
    if not dr or id == nil then
        return 0
    end
    return dr.cards[id] or 0
end

-- Tray rows: { id, qty, cost, name, card } sorted by cost, then name, then id.
function D.Rows(draft)
    draft = draft or D.draft
    local rows = {}
    if not draft then
        return rows
    end
    for id, q in pairs(draft.cards) do
        local c = D.GetCard(id)
        table.insert(rows, { id = id, qty = q, card = c, cost = (c and c.cost) or 99, name = (c and c.name) or id })
    end
    table.sort(rows, function(a, b)
        if a.cost ~= b.cost then
            return a.cost < b.cost
        end
        local na, nb = string.lower(a.name), string.lower(b.name)
        if na ~= nb then
            return na < nb
        end
        return byteLess(a.id, b.id)
    end)
    return rows
end

-- Cards per mana cost 0..7 (7 = 7 or more).
function D.Curve(draft)
    draft = draft or D.draft
    local t = {}
    for i = 0, CM.MANA_MAX do
        t[i] = 0
    end
    if draft then
        for id, q in pairs(draft.cards) do
            local c = D.GetCard(id)
            local b = CM.ManaBucket(c and c.cost or 0)
            t[b] = t[b] + q
        end
    end
    return t
end

-- ------------------------------------------------------------ operations
-- A fresh idempotency key (the generator of Packs.lua: 12 characters [0-9a-z]).
function D.NewKey()
    return HsPacks.NewKey()
end

-- DECK_SAVE payload: key=;id=;rev=;class=;name=;cards=
function D.SavePayload(key, deckId, rev, class, name, cards)
    return HsProtocol.KvEncode({
        { "key", key }, { "id", tostring(deckId) }, { "rev", tostring(rev) }, { "class", class },
        { "name", name }, { "cards", D.EncodeCards(cards) },
    })
end

-- DECK_DELETE payload: key=;id=;rev=
function D.DeletePayload(key, deckId, rev)
    return HsProtocol.KvEncode({ { "key", key }, { "id", tostring(deckId) }, { "rev", tostring(rev) } })
end

local function newOp(kind, key, payload)
    D.op = { kind = kind, key = key, payload = payload, attempts = 0, reqIds = {} }
    return D.op
end

-- Forgets the key of a create whose outcome was unknown (it is settled now).
function D.ClearCreate(dr)
    if dr then
        dr.createKey, dr.createName, dr.createCards, dr.createPayload = nil, nil, nil, nil
    end
end

-- The save of the draft. A new deck keeps its key and its exact payload on the draft until the
-- server settles the create: asking again after an unknown outcome resends that very request, so
-- it can never create a second deck, and a replay brings back exactly what the server stored
-- (later edits of the draft then still need a save, as an update of the created deck).
function D.BeginSaveDraft()
    local dr = D.draft
    if not dr then
        return nil
    end
    local name = D.ValidateName(dr.name)
    if not name then
        return nil
    end
    local op
    if dr.deckId == 0 and dr.createKey then
        op = newOp("save", dr.createKey, dr.createPayload)
        op.name = dr.createName
        op.cards = copyCards(dr.createCards)
    else
        local key = D.NewKey()
        local cards = copyCards(dr.cards)
        op = newOp("save", key, D.SavePayload(key, dr.deckId, dr.baseRev, dr.class, name, cards))
        op.name = name
        op.cards = cards
        if dr.deckId == 0 then
            dr.createKey, dr.createName, dr.createCards, dr.createPayload = key, name, copyCards(cards), op.payload
        end
    end
    op.draft = dr
    op.deckId = dr.deckId
    op.class = dr.class
    return op
end

-- Renaming a saved deck: a save of the same cards with the new name.
function D.BeginRename(deck, newName)
    local name = D.ValidateName(newName)
    if not name or not deck then
        return nil
    end
    local key = D.NewKey()
    local op = newOp("rename", key, D.SavePayload(key, deck.id, deck.rev, deck.class, name, deck.cards))
    op.deckId = deck.id
    op.class = deck.class
    op.name = name
    op.cards = copyCards(deck.cards)
    return op
end

function D.BeginDelete(deck)
    if not deck then
        return nil
    end
    local key = D.NewKey()
    local op = newOp("delete", key, D.DeletePayload(key, deck.id, deck.rev))
    op.deckId = deck.id
    op.name = deck.name
    return op
end

function D.ClearOp()
    D.op = nil
end

function D.IsBusy()
    return D.op ~= nil
end

-- One line for /hs status.
function D.StatusText()
    local s = D.state
    local text
    if s == "ready" then
        text = "decks: ready, " .. D.DeckCount() .. " / " .. D.cap .. " saved"
        text = text .. ", start " .. tostring(D.start or "-")
        local un = {}
        for i = 1, table.getn(D.CLASSES) do
            if D.unlocked[D.CLASSES[i]] then
                table.insert(un, D.CLASSES[i])
            end
        end
        if table.getn(un) > 0 then
            text = text .. ", unlocked " .. table.concat(un, ",")
        end
    elseif s == "loading" then
        text = "decks: loading"
    elseif s == "error" then
        text = "decks: error (" .. tostring(D.errorText or "?") .. ")"
    elseif s == "unsupported" then
        text = "decks: not supported by this server"
    else
        text = "decks: none (not connected)"
    end
    local op = D.op
    if op then
        text = text .. ", " .. op.kind .. " in progress (attempt " .. (op.attempts or 0) .. " of " .. D.MAX_ATTEMPTS .. ")"
    end
    if D.draft then
        text = text .. ", editing " .. D.ClassLabel(D.draft.class) .. " deck \"" .. D.TrimName(D.draft.name) .. "\""
        if D.IsDirty() then
            text = text .. " (unsaved changes)"
        end
    end
    return text
end

if D.state == nil then
    D.draft = nil
    D.op = nil
    D.message = nil
    D.addReason = nil
    D.Reset()
end
