-- Hearthstone addon: wire protocol v1 (framing, escaping, chunking, reassembly).
--
-- Lua 5.0 (WoW 1.12): no '#', no select(), no string.match/gmatch, no '%' operator,
-- no '...' varargs. No WoW API is used here so tests/lua_tests.lua can load this
-- file in a plain interpreter. It mirrors src/protocol/HsProtocol.cpp and
-- HsReassembler.cpp; tests/golden_vectors.txt keeps both implementations in step.
-- See docs/PROTOCOL.md.

HsProtocol = {}
local P = HsProtocol

P.PREFIX = "WRHS"
P.VERSION = 1
P.MAX_WIRE = 240          -- prefix + TAB + frame must not exceed this
P.MAX_PARTS_LIMIT = 99
P.MAX_REQID_LEN = 8
P.MAX_OP_LEN = 12
P.SERVER_PUSH_REQID = "0"

local HEX = "0123456789abcdef"
local TWO = {
    ["~"] = "~~", ["|"] = "~p", ["\n"] = "~n", ["\r"] = "~r",
    ["\t"] = "~t", [":"] = "~c", ["_"] = "~u",
}
local UNTWO = {
    ["~"] = "~", ["p"] = "|", ["n"] = "\n", ["r"] = "\r",
    ["t"] = "\t", ["c"] = ":", ["u"] = "_", ["l"] = "L",
}

local function hex2(b)
    local hi = math.floor(b / 16)
    local lo = b - hi * 16
    return string.sub(HEX, hi + 1, hi + 1) .. string.sub(HEX, lo + 1, lo + 1)
end

-- ---------------------------------------------------------------- escaping
-- Encoded text never contains | LF CR TAB NUL : _  , nothing outside printable
-- ASCII, and never the substring "LFT".
--   ~~ '~'  ~p '|'  ~n LF  ~r CR  ~t TAB  ~c ':'  ~u '_'
--   ~l 'L' (only when the next byte is 'F')   ~xHH other control / >= 0x80 bytes
function P.EscapeStep(raw, i)
    local c = string.sub(raw, i, i)
    local two = TWO[c]
    if two then
        return two
    end
    if c == "L" and string.sub(raw, i + 1, i + 1) == "F" then
        return "~l"
    end
    local b = string.byte(c)
    if b < 32 or b == 127 or b >= 128 then
        return "~x" .. hex2(b)
    end
    return c
end

function P.Escape(raw)
    local out = {}
    local n = string.len(raw)
    for i = 1, n do
        out[i] = P.EscapeStep(raw, i)
    end
    return table.concat(out)
end

-- Returns the raw string, or nil on a dangling '~' / unknown escape.
function P.Unescape(enc)
    local out = {}
    local count = 0
    local i = 1
    local n = string.len(enc)
    while i <= n do
        local c = string.sub(enc, i, i)
        if c ~= "~" then
            count = count + 1
            out[count] = c
            i = i + 1
        else
            i = i + 1
            if i > n then
                return nil
            end
            local e = string.sub(enc, i, i)
            if e == "x" then
                local h = string.sub(enc, i + 1, i + 2)
                if string.len(h) ~= 2 or not string.find(h, "^%x%x$") then
                    return nil
                end
                count = count + 1
                out[count] = string.char(tonumber(h, 16))
                i = i + 3
            else
                local r = UNTWO[e]
                if not r then
                    return nil
                end
                count = count + 1
                out[count] = r
                i = i + 1
            end
        end
    end
    return table.concat(out)
end

-- Value escape for key=value;key=value payloads:  ~~ '~'  ~s ';'  ~q '='
function P.EscapeValue(raw)
    local s = string.gsub(raw, "~", "~~")
    s = string.gsub(s, ";", "~s")
    s = string.gsub(s, "=", "~q")
    return s
end

function P.UnescapeValue(enc)
    local out = {}
    local count = 0
    local i = 1
    local n = string.len(enc)
    while i <= n do
        local c = string.sub(enc, i, i)
        if c ~= "~" then
            count = count + 1
            out[count] = c
            i = i + 1
        else
            i = i + 1
            if i > n then
                return nil
            end
            local e = string.sub(enc, i, i)
            local r = nil
            if e == "~" then r = "~" elseif e == "s" then r = ";" elseif e == "q" then r = "=" end
            if not r then
                return nil
            end
            count = count + 1
            out[count] = r
            i = i + 1
        end
    end
    return table.concat(out)
end

-- True if s contains | LF CR NUL or a substring the server core reacts to.
function P.ContainsForbidden(s)
    local bad = { "|", "\n", "\r", "\0", "LFT", "TW_", "TWT_", "GM_ADDON", "MR:", "TR:" }
    for i = 1, table.getn(bad) do
        if string.find(s, bad[i], 1, true) then
            return true
        end
    end
    return false
end

-- ------------------------------------------------------------- key=value
-- list = { {"key", "value"}, ... } (ordered)
function P.KvEncode(list)
    local out = {}
    for i = 1, table.getn(list) do
        out[i] = list[i][1] .. "=" .. P.EscapeValue(list[i][2])
    end
    return table.concat(out, ";")
end

-- Returns a table key -> value, or nil when malformed.
function P.KvDecode(payload)
    local result = {}
    if payload == "" then
        return result
    end
    local pos = 1
    while true do
        local semi = string.find(payload, ";", pos, true)
        local seg
        if semi then
            seg = string.sub(payload, pos, semi - 1)
        else
            seg = string.sub(payload, pos)
        end
        local eq = string.find(seg, "=", 1, true)
        if seg == "" or not eq or eq == 1 then
            return nil
        end
        local key = string.sub(seg, 1, eq - 1)
        if not string.find(key, "^[%w_]+$") then
            return nil
        end
        local value = P.UnescapeValue(string.sub(seg, eq + 1))
        if value == nil or result[key] ~= nil then
            return nil
        end
        result[key] = value
        if not semi then
            break
        end
        pos = semi + 1
    end
    return result
end

-- ------------------------------------------------------------------ frames
-- frame = v1 TAB OP TAB reqId TAB part TAB total TAB payload

function P.IsValidOp(op)
    return type(op) == "string" and string.len(op) >= 1 and string.len(op) <= P.MAX_OP_LEN
        and string.find(op, "^[A-Z][A-Z0-9_]*$") ~= nil
end

function P.IsValidReqId(id)
    return type(id) == "string" and string.len(id) >= 1 and string.len(id) <= P.MAX_REQID_LEN
        and string.find(id, "^[0-9a-z]+$") ~= nil
end

-- Canonical decimal 1..3 digits without leading zero, else nil.
local function parseSmall(s, maxDigits)
    if string.len(s) < 1 or string.len(s) > (maxDigits or 3) or not string.find(s, "^%d+$") then
        return nil
    end
    if string.len(s) > 1 and string.sub(s, 1, 1) == "0" then
        return nil
    end
    return tonumber(s)
end

-- Parses a frame (prefix already removed). Returns status, frame, reqId where
-- status is "ok", "malformed" or "badversion"; frame is only set for "ok";
-- reqId is a best effort id for error replies.
function P.ParseFrame(frame)
    local fields = {}
    local count = 0
    local pos = 1
    for i = 1, 5 do
        local tab = string.find(frame, "\t", pos, true)
        if not tab then
            break
        end
        count = count + 1
        fields[count] = string.sub(frame, pos, tab - 1)
        pos = tab + 1
    end
    -- fields[1..count] ended with a TAB; the remainder is the next field
    count = count + 1
    fields[count] = string.sub(frame, pos)

    local reqId = P.SERVER_PUSH_REQID
    if count >= 3 and P.IsValidReqId(fields[3]) then
        reqId = fields[3]
    end

    -- Version comes first: "v" + digits (up to 9). Another number is a version
    -- error whatever follows.
    if string.len(fields[1]) < 2 or string.sub(fields[1], 1, 1) ~= "v" then
        return "malformed", nil, reqId
    end
    local version = parseSmall(string.sub(fields[1], 2), 9)
    if not version then
        return "malformed", nil, reqId
    end
    if version ~= P.VERSION then
        return "badversion", nil, reqId
    end
    if count < 6 then
        return "malformed", nil, reqId
    end

    if not P.IsValidOp(fields[2]) or not P.IsValidReqId(fields[3]) then
        return "malformed", nil, reqId
    end
    local part = parseSmall(fields[4])
    local total = parseSmall(fields[5])
    if not part or not total or part < 1 or total < 1 or part > total then
        return "malformed", nil, reqId
    end
    -- payload may only hold characters Escape() can emit
    if string.find(fields[6], "[^\32-\123\125\126]") or string.find(fields[6], "[:_]") then
        return "malformed", nil, reqId
    end
    local payload = P.Unescape(fields[6])
    if payload == nil then
        return "malformed", nil, reqId
    end
    return "ok", { op = fields[2], reqId = fields[3], part = part, total = total, payload = payload }, reqId
end

-- Returns the frame string, or nil if invalid / too long / forbidden.
function P.EncodeFrame(op, reqId, part, total, payload, maxWire)
    maxWire = maxWire or P.MAX_WIRE
    if not P.IsValidOp(op) or not P.IsValidReqId(reqId) then
        return nil
    end
    if total < 1 or total > P.MAX_PARTS_LIMIT or part < 1 or part > total then
        return nil
    end
    local frame = "v" .. P.VERSION .. "\t" .. op .. "\t" .. reqId .. "\t" .. part .. "\t" .. total
        .. "\t" .. P.Escape(payload)
    if string.len(P.PREFIX) + 1 + string.len(frame) > maxWire then
        return nil
    end
    if P.ContainsForbidden(frame) then
        return nil
    end
    return frame
end

-- Splits payload into frames <= maxWire. Returns a table of frames or nil.
function P.ChunkPayload(op, reqId, payload, maxWire)
    maxWire = maxWire or P.MAX_WIRE
    if not P.IsValidOp(op) or not P.IsValidReqId(reqId) then
        return nil
    end
    local header = string.len(P.PREFIX) + 1 + 3 + string.len(op) + 1 + string.len(reqId) + 1 + 3 + 3
    if maxWire < header + 4 then
        return nil
    end
    local budget = maxWire - header
    local n = string.len(payload)
    local chunks = {}
    local nchunks = 0
    local i = 1
    repeat
        local start = i
        local used = 0
        while i <= n do
            local cost = string.len(P.EscapeStep(payload, i))
            if used + cost > budget then
                break
            end
            used = used + cost
            i = i + 1
        end
        nchunks = nchunks + 1
        chunks[nchunks] = string.sub(payload, start, i - 1)
        if nchunks > P.MAX_PARTS_LIMIT then
            return nil
        end
    until i > n

    local frames = {}
    for k = 1, nchunks do
        local f = P.EncodeFrame(op, reqId, k, nchunks, chunks[k], maxWire)
        if not f then
            return nil
        end
        frames[k] = f
    end
    return frames
end

-- ------------------------------------------------------------- reassembler
-- Bounded reassembly of multi-part replies. Time values are seconds.
-- limits = { maxBytes, maxParts, timeout, maxPending }
local COMPLETED_CAPACITY = 16

function P.NewReassembler(limits)
    local r = { limits = limits, pending = {}, npending = 0, completed = {} }

    -- Server pushes all carry reqId "0" (P.SERVER_PUSH_REQID): one of them finishing must not make the
    -- next multi-part push look like a duplicate, so pushes are never remembered.
    local function remember(reqId)
        if reqId == P.SERVER_PUSH_REQID then
            return
        end
        for i = 1, table.getn(r.completed) do
            if r.completed[i] == reqId then
                table.remove(r.completed, i)
                break
            end
        end
        table.insert(r.completed, reqId)
        while table.getn(r.completed) > COMPLETED_CAPACITY do
            table.remove(r.completed, 1)
        end
    end

    function r:IsRemembered(reqId)
        for i = 1, table.getn(self.completed) do
            if self.completed[i] == reqId then
                return true
            end
        end
        return false
    end

    function r:PendingCount()
        return self.npending
    end

    function r:Clear()
        self.pending = {}
        self.npending = 0
        self.completed = {}
    end

    -- Returns status, completed. status: "complete" "pending" "ignored"
    -- "malformed" "toolarge" "busy"; completed = { op, reqId, payload }.
    function r:Add(now, op, reqId, part, total, payload)
        local lim = self.limits
        if total < 1 or part < 1 or part > total then
            return "malformed"
        end
        -- one error per reqId: an already finished / rejected request stays quiet
        if total > 1 and self:IsRemembered(reqId) then
            return "ignored"
        end
        if total > lim.maxParts then
            if self.pending[reqId] then
                self.pending[reqId] = nil
                self.npending = self.npending - 1
            end
            remember(reqId)
            return "toolarge"
        end
        if total == 1 then
            if string.len(payload) > lim.maxBytes then
                return "toolarge"
            end
            return "complete", { op = op, reqId = reqId, payload = payload }
        end
        -- Pushes share reqId "0" and are never resent: the first part of a new push (or one of another op or size)
        -- drops an unfinished one, so a lost part cannot splice two pushes together.
        if reqId == P.SERVER_PUSH_REQID then
            local old = self.pending[reqId]
            if old and (part == 1 or old.total ~= total or old.op ~= op) then
                self.pending[reqId] = nil
                self.npending = self.npending - 1
            end
        end
        local p = self.pending[reqId]
        if not p then
            if self.npending >= lim.maxPending then
                remember(reqId)
                return "busy"
            end
            p = { op = op, total = total, received = 0, bytes = 0, started = now, parts = {}, have = {} }
            self.pending[reqId] = p
            self.npending = self.npending + 1
        end
        if p.total ~= total or p.op ~= op then
            self.pending[reqId] = nil
            self.npending = self.npending - 1
            remember(reqId)
            return "malformed"
        end
        if p.have[part] then
            return "ignored"
        end
        if p.bytes + string.len(payload) > lim.maxBytes then
            self.pending[reqId] = nil
            self.npending = self.npending - 1
            remember(reqId)
            return "toolarge"
        end
        p.parts[part] = payload
        p.have[part] = true
        p.bytes = p.bytes + string.len(payload)
        p.received = p.received + 1
        if p.received < p.total then
            return "pending"
        end
        local pieces = {}
        for i = 1, p.total do
            pieces[i] = p.parts[i]
        end
        self.pending[reqId] = nil
        self.npending = self.npending - 1
        remember(reqId)
        return "complete", { op = p.op, reqId = reqId, payload = table.concat(pieces) }
    end

    -- Drops requests older than limits.timeout; returns a list of their reqIds.
    function r:Sweep(now)
        local expired = {}
        for reqId, p in pairs(self.pending) do
            if now >= p.started and now - p.started >= self.limits.timeout then
                table.insert(expired, reqId)
            end
        end
        for i = 1, table.getn(expired) do
            self.pending[expired[i]] = nil
            self.npending = self.npending - 1
            remember(expired[i])
        end
        return expired
    end

    return r
end
