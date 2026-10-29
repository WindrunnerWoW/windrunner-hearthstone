-- Hearthstone addon: account card ownership.
--
-- Pure logic, no WoW frames and no network: the ownership state and the parser of the server's
-- COLL reply. Hearthstone.lua sends COLL_GET and feeds COLL / COLL_CHANGED / ERR into this file;
-- CollectionUI.lua only reads it. The server is the only source of counts: the addon never sends
-- a count anywhere, so nothing here can assert ownership.
--
--   O.state      "none" (not connected: catalog only), "loading", "ready" or "error"
--   O.owned      id -> { total = n, prot = p }  (filled only while ready; ids unknown to
--                HS_CardDB are left out and counted in O.unknown)
--   O.version    collection version of the last good COLL reply
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsOwnership = HsOwnership or {}
local O = HsOwnership

O.MAX_TOTAL = 999            -- copies of one card (server bound)
O.MAX_ENTRIES = 5000         -- entries in one COLL reply (far above the dataset size)
O.MAX_VERSION = 4294967295   -- INT UNSIGNED on the server
O.MAX_ID_LEN = 32            -- VARCHAR(32) on the server

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

-- Parses a COLL payload: ver=<n>;n=<entries>;cards=<id>.<total>.<protected>,...
-- db is HS_CardDB (or nil). Returns a result table, or nil and a reason; one bad entry rejects
-- the whole reply. Result: { version, owned, entries, distinct, copies, protected, unknown }.
function O.Parse(payload, db)
    if type(payload) ~= "string" then
        return nil, "no payload"
    end
    local kv = HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local version = parseNumber(kv.ver, 10)
    if version == nil or version > O.MAX_VERSION then
        return nil, "bad ver"
    end
    local n = parseNumber(kv.n, 5)
    if n == nil or n > O.MAX_ENTRIES then
        return nil, "bad n"
    end
    local cards = kv.cards
    if cards == nil then
        return nil, "missing cards"
    end
    local known = nil
    if type(db) == "table" and type(db.cards) == "table" then
        known = db.cards
    end
    local res = { version = version, owned = {}, entries = 0, distinct = 0, copies = 0, protected = 0, unknown = 0 }
    local seen = {}
    if cards ~= "" then
        local pos = 1
        while true do
            local comma = string.find(cards, ",", pos, true)
            local entry
            if comma then
                entry = string.sub(cards, pos, comma - 1)
            else
                entry = string.sub(cards, pos)
            end
            res.entries = res.entries + 1
            if res.entries > n then
                return nil, "more entries than n"
            end
            local _, _, id, t, p = string.find(entry, "^([%w_]+)%.(%d+)%.(%d+)$")
            if not id or string.len(id) > O.MAX_ID_LEN then
                return nil, "malformed entry " .. res.entries
            end
            local total = parseNumber(t, 3)
            local prot = parseNumber(p, 3)
            if total == nil or prot == nil or total < 1 or total > O.MAX_TOTAL or prot > total then
                return nil, "count out of range for " .. id
            end
            if seen[id] then
                return nil, "duplicate entry " .. id
            end
            seen[id] = true
            if known and known[id] then
                res.owned[id] = { total = total, prot = prot }
                res.distinct = res.distinct + 1
                res.copies = res.copies + total
                res.protected = res.protected + prot
            else
                res.unknown = res.unknown + 1
            end
            if not comma then
                break
            end
            pos = comma + 1
        end
    end
    if res.entries ~= n then
        return nil, "n=" .. n .. " but " .. res.entries .. " entries"
    end
    return res
end

-- ------------------------------------------------------------------ state
local function clearData()
    O.owned = {}
    O.version = nil
    O.distinct, O.copies, O.protected, O.unknown = 0, 0, 0, 0
end

-- Not connected: the browser is the plain catalog.
function O.Reset()
    O.state = "none"
    O.errorText = nil
    clearData()
end

-- A request is on its way. A refetch while ready keeps the current data on screen.
function O.SetLoading()
    if O.state ~= "ready" then
        O.state = "loading"
        O.errorText = nil
        clearData()
    end
end

function O.SetReady(res)
    O.state = "ready"
    O.errorText = nil
    O.owned = res.owned
    O.version = res.version
    O.distinct, O.copies, O.protected, O.unknown = res.distinct, res.copies, res.protected, res.unknown
end

-- The collection could not be loaded; old counts are dropped so the UI never shows stale data.
function O.SetError(text)
    O.state = "error"
    O.errorText = text
    clearData()
end

function O.IsReady()
    return O.state == "ready"
end

-- The entry { total, prot } of an owned card, or nil (also nil while not ready).
function O.Get(id)
    if O.state ~= "ready" or id == nil then
        return nil
    end
    return O.owned[id]
end

-- How many of the ids are owned.
function O.CountOwned(ids)
    local n = 0
    if O.state ~= "ready" or type(ids) ~= "table" then
        return 0
    end
    for i = 1, table.getn(ids) do
        if O.owned[ids[i]] then
            n = n + 1
        end
    end
    return n
end

-- Quantity badge text ("x2"), or nil when the card is not owned or ownership is not ready.
function O.BadgeText(id)
    local e = O.Get(id)
    if e then
        return "x" .. e.total
    end
    return nil
end

-- Line for the hover caption, or nil while ownership is not ready.
function O.CaptionLine(id)
    if O.state ~= "ready" then
        return nil
    end
    local e = O.owned[id]
    if not e then
        return "|cff999999Not owned|r"
    end
    local text = "Owned: " .. e.total
    if e.prot > 0 then
        text = text .. " (" .. e.prot .. " protected)"
    end
    return "|cff33ff33" .. text .. "|r"
end

-- One line for /hs status.
function O.StatusText()
    local s = O.state
    if s == "ready" then
        return "collection: ready, version " .. tostring(O.version) .. ", " .. O.distinct .. " distinct cards, "
            .. O.copies .. " copies, " .. O.protected .. " protected, " .. O.unknown .. " unknown ids"
    elseif s == "loading" then
        return "collection: loading"
    elseif s == "error" then
        return "collection: error (" .. tostring(O.errorText or "?") .. ")"
    end
    return "collection: none (not connected, catalog only)"
end

if O.state == nil then
    O.Reset()
end
