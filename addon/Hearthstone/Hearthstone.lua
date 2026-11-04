-- Hearthstone addon: server connection, handshake, collection, pack, deck, match, wallet and arena
-- requests and diagnostics.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.
-- All protocol logic lives in HsProtocol.lua; this file owns the connection state,
-- the outbound queue, the collection requests (COLL_GET / COLL / COLL_CHANGED; the ownership data
-- itself lives in Ownership.lua), the pack requests (PACK_GET / PACK_OPEN and their replies; the
-- pack state lives in Packs.lua), the deck requests (DECK_LIST / DECK_SAVE / DECK_DELETE and their
-- replies, DECK_CHANGED; the deck state, rules and the editor draft live in Decks.lua), the match
-- requests (GAME_GET / GAME_ACT / GAME_LEAVE, the GAME replies and pushes; the match state lives in
-- Match.lua), the wallet and craft requests (Wallet.lua), the draft arena requests (ARENA_GET / ARENA_CLASS /
-- ARENA_PICK / ARENA_CLAIM / ARENA_DUEL / ARENA_RETIRE, GAME_PAUSE and the pushes ARENA_UPD / ARENA_OPEN / ARENA_CLOSE;
-- the arena state lives in Arena.lua),
-- the slash commands and the WoW event plumbing. The WoW
-- specific parts are guarded so tests/lua_tests.lua can drive everything from a
-- plain Lua interpreter with H.Transmit / H.Print replaced.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local P = HsProtocol
local O = HsOwnership
local PK = HsPacks
local D = HsDecks
local MA = HsMatch
local W = HsWallet
local AR = HsArena
local E = HsHeroes

H.VERSION = "0.1.0"           -- addon version (sent in HELLO)
H.PROTOCOL = 1
H.SEND_INTERVAL = 0.12        -- seconds between outbound frames (server allows ~10/s)
H.REQUEST_TIMEOUT = 10        -- seconds until an unanswered request is reported
H.HELLO_DELAY = 2             -- seconds after entering the world before HELLO
H.PACK_RETRY_DELAY = 2        -- seconds before PACK_OPEN is sent again (same key) after a failure
H.DECK_RETRY_DELAY = 2        -- seconds before DECK_SAVE / DECK_DELETE is sent again (same key)
H.CRAFT_RETRY_DELAY = 2       -- seconds before CRAFT_MAKE / CRAFT_SCRAP is sent again (same key)
H.ARENA_RETRY_DELAY = 2       -- seconds before ARENA_CLASS / ARENA_PICK / ARENA_CLAIM is sent again (same key)

-- key binding labels (Bindings.xml)
BINDING_HEADER_HEARTHSTONE = "Hearthstone"
BINDING_NAME_HEARTHSTONE_TOGGLE = "Toggle Card Collection"

H.state = {
    welcomed = false,
    welcome = nil,            -- decoded WELCOME key/values
    lastError = nil,          -- text of the last ERR / local failure
    lastErrorCode = nil,
    helloSent = false,
    helloReqId = nil,
    datasetStatus = nil,      -- "ok", "mismatch", "nocarddb" or "unknown" (see H.CheckDataset)
}
H.queue = {}                  -- outbound frames, oldest first
H.pending = {}                -- reqId -> { op, sentAt }
H.sinceSend = 1
H.clock = 0                   -- fallback clock when GetTime() does not exist
H.helloIn = nil               -- countdown to the automatic HELLO
H.reqCounter = 0
H.coll = { reqId = nil, refetch = false }   -- the one COLL_GET in flight; refetch = COLL_CHANGED came meanwhile
-- covers: the COLL_GET in flight was sent after the server reached this collection version, so a
-- COLL_CHANGED up to it needs no refetch; refetchVer: the same for the refetch that is due.
H.packs = { reqId = nil, refetch = false, supported = false }  -- the one PACK_GET in flight; supported = caps has "packs"
H.packReqs = {}               -- reqId -> "PACK_GET" / "PACK_OPEN" for every pack request of this session
H.decks = { reqId = nil, refetch = false, supported = false }  -- the one DECK_LIST in flight; supported = caps has "decks"
H.deckReqs = {}               -- reqId -> "DECK_LIST" / "DECK_SAVE" / "DECK_DELETE" for every deck request of this session
-- the one GAME_GET in flight; supported = caps has "match"; announce = a /hs game asked, so a reply
-- without a match is told to the player
H.match = { reqId = nil, refetch = false, supported = false, announce = false }
H.matchReqs = {}              -- reqId -> "GAME_GET" / "GAME_ACT" / "GAME_LEAVE" for every match request of this session
-- The one WALLET_GET in flight; supported = caps has "wallet"
H.wallet = { reqId = nil, refetch = false, supported = false }
H.walletReqs = {}             -- reqId -> "WALLET_GET" / "craft" / "scrap" for every wallet request of this session
-- The one ARENA_GET in flight; supported = caps has "arena"
H.arena = { reqId = nil, refetch = false, supported = false }
H.arenaReqs = {}              -- reqId -> "ARENA_GET" / "class" / "pick" / "claim" for every arena request of this session
-- The one HERO_GET in flight; supported = caps has "heroes"
H.heroes = { reqId = nil, refetch = false, supported = false }
H.heroReqs = {}               -- reqId -> "HERO_GET" / "HERO_SELECT" / "HERO_UNLOCK" for every hero request of this session

-- 32 KiB: the largest DECKS reply (29 decks of 30 distinct cards with long non-ASCII names,
-- about 14 KB) fits with room to spare
H.reasm = P.NewReassembler({ maxBytes = 32768, maxParts = P.MAX_PARTS_LIMIT, timeout = 15, maxPending = 8 })

-- ------------------------------------------------------------------- output
local function sanitize(s)
    return (string.gsub(tostring(s), "|", "||"))
end

function H.Print(msg)
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cff33ccff[Hearthstone]|r " .. sanitize(msg))
    end
end

function H.PrintError(msg)
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cff33ccff[Hearthstone]|r |cffff5555" .. sanitize(msg) .. "|r")
    end
end

local function now()
    if GetTime then
        return GetTime()
    end
    return H.clock
end

-- ---------------------------------------------------------------- transport
-- The server only understands the GUILD distribution for addon traffic.
function H.Transmit(frame)
    if SendAddonMessage then
        SendAddonMessage(P.PREFIX, frame, "GUILD")
    end
end

local BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz"

function H.NextReqId()
    if H.reqCounter == 0 then
        -- random start so that a /reload does not reuse the ids the server still remembers
        if math.randomseed then
            local seed = 0
            if GetTime then seed = seed + math.floor(GetTime() * 1000) end
            if time then seed = seed + time() end
            math.randomseed(seed)
        end
        H.reqCounter = math.random(1, 1000000)
    end
    H.reqCounter = H.reqCounter + 1
    local n = H.reqCounter
    local s = ""
    while n > 0 do
        local d = n - math.floor(n / 36) * 36
        s = string.sub(BASE36, d + 1, d + 1) .. s
        n = math.floor(n / 36)
    end
    return s
end

-- Chunks and queues a request. Returns the reqId or nil (after printing why).
function H.Request(op, payload, meta)
    local reqId = H.NextReqId()
    local frames = P.ChunkPayload(op, reqId, payload)
    if not frames then
        H.PrintError("Request " .. op .. " does not fit into the protocol limits.")
        return nil
    end
    for i = 1, table.getn(frames) do
        table.insert(H.queue, frames[i])
    end
    meta = meta or {}
    meta.op = op
    meta.sentAt = now()
    H.pending[reqId] = meta
    return reqId
end

function H.Hello()
    H.state.helloSent = true
    local payload = P.KvEncode({
        { "protocol", tostring(H.PROTOCOL) },
        { "addon", H.VERSION },
        { "dataset", "-" },
        { "ladderui", "1" },
    })
    local reqId = H.Request("HELLO", payload, {})
    H.state.helloReqId = reqId
    return reqId
end

-- ------------------------------------------------------------- collection
-- Tells the UI (CollectionUI.lua) that the ownership state or data changed.
function H.NotifyOwnership()
    if H.OnOwnershipChanged then
        H.OnOwnershipChanged()
    end
end

-- Asks for the account collection. Only after WELCOME, and only one request at a time: a call
-- while one is in flight marks a refetch that is sent when it finished. atLeastVer (optional) is
-- a collection version the server is known to have reached already (COLL_CHANGED, PACK_OPENED):
-- a request in flight that was sent after that point already covers it.
function H.CollGet(atLeastVer)
    if not H.state.welcomed then
        return nil
    end
    if H.coll.reqId then
        if atLeastVer == nil or H.coll.covers == nil or atLeastVer > H.coll.covers then
            H.coll.refetch = true
            if atLeastVer ~= nil and (H.coll.refetchVer == nil or atLeastVer > H.coll.refetchVer) then
                H.coll.refetchVer = atLeastVer
            end
        end
        return H.coll.reqId
    end
    local reqId = H.Request("COLL_GET", "", {})
    if not reqId then
        O.SetError("request failed")
        H.NotifyOwnership()
        return nil
    end
    H.coll.reqId = reqId
    H.coll.covers = atLeastVer
    O.SetLoading()
    H.NotifyOwnership()
    return reqId
end

-- The COLL_GET in flight got its answer (reply, error or timeout): send the refetch if one is due.
local function collFinished()
    H.coll.reqId = nil
    H.coll.covers = nil
    if H.coll.refetch then
        local ver = H.coll.refetchVer
        H.coll.refetch = false
        H.coll.refetchVer = nil
        H.CollGet(ver)
    end
end

-- Not connected any more (VERSION / DISABLED): back to the plain catalog.
function H.ResetCollection()
    H.coll.reqId = nil
    H.coll.refetch = false
    H.coll.covers = nil
    H.coll.refetchVer = nil
    O.Reset()
    H.NotifyOwnership()
end

function H.OnColl(msg)
    if msg.reqId ~= H.coll.reqId then
        return                        -- unsolicited or late (after a timeout): ignored
    end
    local res, why = O.Parse(msg.payload, HS_CardDB)
    if res then
        O.SetReady(res)
    else
        O.SetError("bad reply: " .. tostring(why))
        H.state.lastError = "collection reply rejected (" .. tostring(why) .. ")"
        H.PrintError("The server sent a collection this addon cannot read. The card catalog still works.")
    end
    collFinished()
    H.NotifyOwnership()
end

-- Server push (reqId 0): the collection changed. Refetch unless we already hold that version.
function H.OnCollChanged(msg)
    if not H.state.welcomed then
        return
    end
    local kv = P.KvDecode(msg.payload)
    local ver = kv and tonumber(kv.ver)
    if ver ~= nil and O.state == "ready" and ver == O.version then
        return
    end
    H.CollGet(ver)
end

-- An ERR / timeout answered the COLL_GET in flight.
function H.OnCollFailed(text)
    if H.coll.reqId then
        H.pending[H.coll.reqId] = nil     -- no "timed out" line later
    end
    O.SetError(text)
    collFinished()
    H.NotifyOwnership()
end

-- ------------------------------------------------------------------ packs
-- Tells the UI (PacksUI.lua) that the pack state, the pending opening or the reveal queue changed.
function H.NotifyPacks()
    if H.OnPacksChanged then
        H.OnPacksChanged()
    end
end

-- Asks for the pack stock and the latest openings. Only after WELCOME with "packs" in caps, and
-- only one request at a time (a call while one is in flight marks a refetch).
function H.PackGet()
    if not H.state.welcomed or not H.packs.supported then
        return nil
    end
    if H.packs.reqId then
        H.packs.refetch = true
        return H.packs.reqId
    end
    local reqId = H.Request("PACK_GET", "", {})
    if not reqId then
        PK.SetError("request failed")
        H.NotifyPacks()
        return nil
    end
    H.packReqs[reqId] = "PACK_GET"
    H.packs.reqId = reqId
    PK.SetLoading()
    H.NotifyPacks()
    return reqId
end

local function packsFinished()
    H.packs.reqId = nil
    if H.packs.refetch then
        H.packs.refetch = false
        H.PackGet()
    end
end

-- Not connected any more (VERSION / DISABLED), or the server has no packs. The reveal queue and
-- an unconfirmed key stay (see Packs.lua).
function H.ResetPacks(unsupported)
    local p = PK.pending
    if p and p.reqId then
        H.pending[p.reqId] = nil
    end
    H.packs.reqId = nil
    H.packs.refetch = false
    H.packs.supported = false
    if unsupported then
        PK.SetUnsupported()
    else
        PK.Reset()
    end
    if p then
        p.inFlight = false
        p.retryIn = nil
        p.reqId = nil
    end
    H.NotifyPacks()
end

function H.OnPacks(msg)
    if msg.reqId ~= H.packs.reqId then
        return                        -- unsolicited or late (after a timeout): ignored
    end
    local res, why = PK.ParsePacks(msg.payload)
    if res then
        PK.SetReady(res)
        local added = PK.Recover()
        if added > 0 then
            if added == 1 then
                PK.message = "You have an opened pack you have not looked at yet."
            else
                PK.message = "You have " .. added .. " opened packs you have not looked at yet."
            end
        end
    else
        PK.SetError("bad reply: " .. tostring(why))
        H.state.lastError = "pack reply rejected (" .. tostring(why) .. ")"
        H.PrintError("The server sent pack data this addon cannot read.")
    end
    packsFinished()
    H.NotifyPacks()
end

-- An ERR / timeout answered the PACK_GET in flight.
function H.OnPackGetFailed(text)
    if H.packs.reqId then
        H.pending[H.packs.reqId] = nil
    end
    PK.SetError(text)
    packsFinished()
    H.NotifyPacks()
end

-- Server push (reqId 0): the stock changed outside PACK_OPEN (for example a GM grant).
function H.OnPackChanged(msg)
    if not H.state.welcomed or not H.packs.supported then
        return
    end
    local kv = P.KvDecode(msg.payload)
    local stock = nil
    if kv and type(kv.stock) == "string" and string.find(kv.stock, "^%d+$") and string.len(kv.stock) <= 6 then
        stock = tonumber(kv.stock)
    end
    if PK.state == "ready" and stock ~= nil and stock <= PK.MAX_STOCK then
        PK.stock = stock
        H.NotifyPacks()
    elseif PK.state ~= "loading" then
        H.PackGet()                   -- error state or an unreadable push: fetch the truth
    end
end

-- Why the Open button / "/hs open" cannot open a pack right now, or nil.
function H.PackOpenBlocker()
    if not H.state.welcomed then
        return "Not connected to the server."
    end
    local s = PK.state
    if s == "unsupported" then
        return "This server does not support packs yet."
    elseif s == "loading" then
        return "Your packs are still loading."
    elseif s ~= "ready" then
        return "Your packs could not be loaded."
    end
    if PK.IsOpening() then
        return "A pack is being opened."
    end
    if PK.Current() then
        return "Finish looking at your last pack first."
    end
    if PK.stock <= 0 then
        return "You have no unopened packs."
    end
    return nil
end

-- Sends PACK_OPEN for the pending opening (a fresh reqId every time, always the same key).
function H.SendPackOpen()
    local p = PK.pending
    if not p then
        return nil
    end
    p.retryIn = nil
    if not H.state.welcomed then
        p.inFlight = false
        return nil
    end
    local reqId = H.Request("PACK_OPEN", P.KvEncode({ { "key", p.key } }), {})
    if not reqId then
        p.inFlight = false
        return nil
    end
    p.attempts = (p.attempts or 0) + 1
    p.reqId = reqId
    p.reqIds[reqId] = true
    H.packReqs[reqId] = "PACK_OPEN"
    return reqId
end

-- Opens one pack. Returns the idempotency key, or nil and the reason it cannot.
function H.PackOpen()
    local why = H.PackOpenBlocker()
    if why then
        return nil, why
    end
    local p = PK.BeginOpen()
    PK.message = nil
    if not H.SendPackOpen() then
        PK.message = "The request could not be sent."
        H.NotifyPacks()
        return nil, PK.message
    end
    if PlaySound then
        PlaySound("igMainMenuOptionCheckBoxOn")
    end
    H.NotifyPacks()
    return p.key
end

-- The attempt in flight failed. Definitive errors (answered to this very reqId) end the opening:
-- nothing changed on the server. Anything else leaves the outcome unknown, so PACK_OPEN is sent
-- again with the same key, up to PK.MAX_ATTEMPTS sends; then the addon stops and keeps the key,
-- so the next click on Open asks about the same opening again.
function H.OnPackOpenFailed(code, text, direct)
    local p = PK.pending
    if not p then
        return
    end
    if p.reqId then
        H.pending[p.reqId] = nil
    end
    p.reqId = nil
    if direct and (code == "NO_STOCK" or code == "REFUSED" or code == "MALFORMED") then
        PK.ClearPending()
        if code == "NO_STOCK" then
            PK.message = "You have no unopened packs."
            H.PrintError(PK.message)
            H.PackGet()               -- our count was stale: resync
        elseif code == "REFUSED" then
            local why = "refused."
            if text and text ~= "" then
                why = text
            end
            PK.message = "The server did not open the pack: " .. why .. " Nothing was changed."
            H.PrintError(PK.message)
        else
            PK.message = "The server rejected the request (MALFORMED). Nothing was changed."
            H.PrintError(PK.message)
        end
        H.NotifyPacks()
        return
    end
    if (p.attempts or 0) < PK.MAX_ATTEMPTS and H.state.welcomed then
        p.retryIn = H.PACK_RETRY_DELAY
        return
    end
    p.inFlight = false
    p.retryIn = nil
    PK.message = "The server did not confirm the pack opening. If the pack was opened, it shows up under Recent packs; opening again is safe."
    H.state.lastError = "PACK_OPEN not confirmed (" .. tostring(code) .. ")"
    H.PrintError(PK.message)
    H.PackGet()
    H.NotifyPacks()
end

function H.OnPackOpened(msg)
    local p = PK.pending
    if not p then
        return                        -- unsolicited: ignored
    end
    local res, why = PK.ParseOpened(msg.payload)
    local byReq = p.reqIds[msg.reqId] == true
    if res and res.key ~= p.key then
        if not byReq then
            return                    -- another key: not ours
        end
        res, why = nil, "key does not match"
    end
    if not res then
        if not byReq then
            return
        end
        -- an answer to our request that we cannot read: stop, keep the key, resync
        if p.reqId then
            H.pending[p.reqId] = nil
        end
        p.reqId = nil
        p.inFlight = false
        p.retryIn = nil
        H.state.lastError = "pack reply rejected (" .. tostring(why) .. ")"
        PK.message = "The server sent a pack reply this addon cannot read."
        H.PrintError(PK.message)
        H.PackGet()
        H.NotifyPacks()
        return
    end
    -- confirmed (possibly the reply to an earlier attempt while a later one is in flight)
    if p.reqId then
        H.pending[p.reqId] = nil
    end
    PK.ClearPending()
    PK.ApplyOpened(res)
    if not O.IsReady() or res.ver ~= O.version then
        H.CollGet(res.ver)
    end
    if PK.Enqueue(res, false) then
        PK.message = nil
    else
        PK.message = "That pack (#" .. res.seq .. ") was opened before; it is under Recent packs."
    end
    H.NotifyPacks()
end

-- A failed PACK_OPEN is sent again after H.PACK_RETRY_DELAY (called from H.OnUpdate).
local function packRetryTick(elapsed)
    local p = PK.pending
    if p and p.inFlight and p.retryIn then
        p.retryIn = p.retryIn - elapsed
        if p.retryIn <= 0 then
            p.retryIn = nil
            if not H.SendPackOpen() then
                H.OnPackOpenFailed("SEND", "", false)
            end
        end
    end
end

-- ------------------------------------------------------------------ decks
-- Tells the UI (DecksUI.lua) that the deck state, the draft or the change in flight changed.
function H.NotifyDecks()
    if H.OnDecksChanged then
        H.OnDecksChanged()
    end
end

-- Asks for the deck list. Only after WELCOME with "decks" in caps, and only one request at a time
-- (a call while one is in flight marks a refetch).
function H.DeckList()
    if not H.state.welcomed or not H.decks.supported then
        return nil
    end
    if H.decks.reqId then
        H.decks.refetch = true
        return H.decks.reqId
    end
    local reqId = H.Request("DECK_LIST", "", {})
    if not reqId then
        D.SetError("request failed")
        H.NotifyDecks()
        return nil
    end
    H.deckReqs[reqId] = "DECK_LIST"
    H.decks.reqId = reqId
    D.SetLoading()
    H.NotifyDecks()
    return reqId
end

local function deckListFinished()
    H.decks.reqId = nil
    if H.decks.refetch then
        H.decks.refetch = false
        H.DeckList()
    end
end

-- Not connected any more (VERSION / DISABLED), or the server has no decks. The draft stays in
-- memory. A change in flight is given up: a new deck keeps its key on the draft (asking again is
-- safe), an update would at worst meet a "conflict" and a delete a "missing".
function H.ResetDecks(unsupported)
    local op = D.op
    if op and op.reqId then
        H.pending[op.reqId] = nil
    end
    H.decks.reqId = nil
    H.decks.refetch = false
    H.decks.supported = false
    if unsupported then
        D.SetUnsupported()
    else
        D.Reset()
    end
    if op then
        D.ClearOp()
        D.message = "The connection was lost before the server confirmed the change. Check your decks when you are connected again."
    end
    H.NotifyDecks()
end

function H.OnDecks(msg)
    if msg.reqId ~= H.decks.reqId then
        return                        -- unsolicited or late (after a timeout): ignored
    end
    local res, why = D.ParseDecks(msg.payload)
    if res then
        D.SetReady(res)
    else
        D.SetError("bad reply: " .. tostring(why))
        H.state.lastError = "deck reply rejected (" .. tostring(why) .. ")"
        H.PrintError("The server sent deck data this addon cannot read.")
    end
    deckListFinished()
    H.NotifyDecks()
end

-- An ERR / timeout answered the DECK_LIST in flight.
function H.OnDeckListFailed(text)
    if H.decks.reqId then
        H.pending[H.decks.reqId] = nil
    end
    D.SetError(text)
    deckListFinished()
    H.NotifyDecks()
end

-- Server push (reqId 0): decks or class access changed outside this client (GM command).
function H.OnDeckChanged(msg)
    if not H.state.welcomed or not H.decks.supported then
        return
    end
    H.DeckList()
end

-- Why no deck can be saved, renamed or deleted right now, or nil.
function H.DeckOpBlocker()
    if not H.state.welcomed then
        return "Not connected to the server."
    end
    local s = D.state
    if s == "unsupported" then
        return "This server does not support decks yet."
    elseif s == "loading" or s == "none" then
        return "Your decks are still loading."
    elseif s ~= "ready" then
        return "Your decks could not be loaded."
    end
    if D.op then
        return "A deck change is being sent."
    end
    return nil
end

-- Why the draft cannot be saved right now, or nil.
function H.DeckSaveBlocker()
    local dr = D.draft
    if not dr then
        return "No deck is being edited."
    end
    local why = H.DeckOpBlocker()
    if why then
        return why
    end
    local _, nameWhy = D.ValidateName(dr.name)
    if nameWhy then
        return "Name: " .. nameWhy .. "."
    end
    if dr.deckId == 0 and D.DeckCount() >= D.cap then
        return "You have the maximum of " .. D.cap .. " decks. Delete one first."
    end
    local hard = D.CheckHard(dr.class, dr.name, dr.cards)
    if hard then
        return H.DeckRefusedText(hard)
    end
    return nil
end

-- Sends the change in flight (a fresh reqId every time, always the same key and payload).
function H.SendDeckOp()
    local op = D.op
    if not op then
        return nil
    end
    op.retryIn = nil
    if not H.state.welcomed then
        return nil
    end
    local wire = "DECK_SAVE"
    if op.kind == "delete" then
        wire = "DECK_DELETE"
    end
    local reqId = H.Request(wire, op.payload, {})
    if not reqId then
        return nil
    end
    op.attempts = (op.attempts or 0) + 1
    op.reqId = reqId
    op.reqIds[reqId] = true
    H.deckReqs[reqId] = wire
    return reqId
end

local function startDeckOp(op)
    if not op then
        return nil, "The request could not be built."
    end
    D.message = nil
    if not H.SendDeckOp() then
        D.ClearOp()
        D.message = "The request could not be sent."
        H.NotifyDecks()
        return nil, D.message
    end
    H.NotifyDecks()
    return op.key
end

-- Saves the draft. Returns the idempotency key, or nil and the reason it cannot.
function H.DeckSave()
    local why = H.DeckSaveBlocker()
    if why then
        return nil, why
    end
    return startDeckOp(D.BeginSaveDraft())
end

-- Renames a saved deck (a save of the same cards). Returns the key, or nil and why not.
function H.DeckRename(deckId, name)
    local why = H.DeckOpBlocker()
    if why then
        return nil, why
    end
    local deck = D.byId[deckId]
    if not deck then
        return nil, "That deck no longer exists."
    end
    local _, nameWhy = D.ValidateName(name)
    if nameWhy then
        return nil, "Name: " .. nameWhy .. "."
    end
    return startDeckOp(D.BeginRename(deck, name))
end

function H.DeckDelete(deckId)
    local why = H.DeckOpBlocker()
    if why then
        return nil, why
    end
    local deck = D.byId[deckId]
    if not deck then
        return nil, "That deck no longer exists."
    end
    return startDeckOp(D.BeginDelete(deck))
end

-- Text for ERR REFUSED why=<code> of a deck request (and for the local hard-rule check).
H.DECK_REFUSED = {
    class = "The deck class is not valid or cannot be changed.",
    name = "The deck name is not valid (1 to 24 characters, no | character).",
    card = "The deck holds a card that cannot be put into decks.",
    cardclass = "The deck holds a card of another class.",
    copies = "Too many copies of a card (2 at most, 1 of a Legendary).",
    size = "A deck holds at most 30 cards.",
    missing = "This deck no longer exists (it was deleted elsewhere).",
    conflict = "This deck was changed elsewhere (another character or a GM).",
    key = "The server refused the request key.",
}

function H.DeckRefusedText(why, text)
    if why == "cap" then
        return "You have the maximum of " .. tostring(D.cap) .. " decks. Delete one first."
    end
    local t = H.DECK_REFUSED[why or ""]
    if t then
        return t
    end
    if text and text ~= "" then
        return text
    end
    return "The server refused the change."
end

-- The change in flight failed. Definitive errors (REFUSED / MALFORMED answered to this very
-- reqId) end it: nothing changed on the server. "conflict" and "missing" also reload the list.
-- Anything else leaves the outcome unknown, so the same request (same key) is sent again, up to
-- D.MAX_ATTEMPTS sends; then the addon stops, reloads the list and tells the player.
function H.OnDeckOpFailed(code, text, why, direct)
    local op = D.op
    if not op then
        return
    end
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    op.reqId = nil
    local dr = op.draft
    if dr and dr ~= D.draft then
        dr = nil                      -- the draft was discarded meanwhile
    end
    if direct and (code == "REFUSED" or code == "MALFORMED") then
        D.ClearOp()
        -- nothing was created: the next save may use a fresh key. A MALFORMED answer to a retry
        -- says nothing about an earlier attempt, which may have committed: keep its key then.
        if dr and dr.deckId == 0 and (code == "REFUSED" or (op.attempts or 0) <= 1) then
            D.ClearCreate(dr)
        end
        local verb = "saved"
        if op.kind == "rename" then
            verb = "renamed"
        elseif op.kind == "delete" then
            verb = "deleted"
        end
        if code == "MALFORMED" then
            D.message = "The server rejected the request (MALFORMED). Nothing was changed."
        elseif why == "conflict" or why == "missing" then
            if dr and why == "conflict" then
                dr.rebase = true      -- the next DECKS gives the current revision
                D.message = H.DECK_REFUSED.conflict .. " Your decks were reloaded. Save again to replace it with your version, or Cancel to keep the other one."
            elseif dr then
                dr.deckId, dr.baseRev, dr.rebase = 0, 0, false
                D.ClearCreate(dr)
                dr.orig = { name = "", cards = {} }
                D.message = H.DECK_REFUSED.missing .. " Save again to create it as a new deck."
            elseif op.kind == "delete" and why == "missing" then
                D.message = "That deck was already deleted."
            else
                D.message = H.DeckRefusedText(why, text) .. " Your decks were reloaded; nothing was " .. verb .. "."
            end
            H.DeckList()
        else
            D.message = "The deck was not " .. verb .. ": " .. H.DeckRefusedText(why, text) .. " Nothing was changed."
        end
        H.PrintError(D.message)
        H.NotifyDecks()
        return
    end
    if (op.attempts or 0) < D.MAX_ATTEMPTS and H.state.welcomed then
        op.retryIn = H.DECK_RETRY_DELAY
        return
    end
    D.ClearOp()
    if op.kind == "delete" then
        D.message = "The server did not confirm the deletion. Your decks were reloaded; check whether the deck is gone."
    elseif dr and dr.deckId == 0 then
        D.message = "The server did not confirm the save. Saving again is safe: it cannot create a second deck."
    else
        D.message = "The server did not confirm the change. Your decks were reloaded; check the deck and save again if needed."
        -- the update may have committed: follow the reloaded revision, so saving again is not
        -- refused as a conflict with the player's own change
        if op.kind == "save" and dr and dr == D.draft then
            dr.rebase = true
        end
    end
    H.state.lastError = "DECK_" .. string.upper(op.kind == "delete" and "delete" or "save") .. " not confirmed (" .. tostring(code) .. ")"
    H.PrintError(D.message)
    H.DeckList()
    H.NotifyDecks()
end

-- An answer to the change in flight that this addon cannot read: stop and reload the list.
local function deckReplyUnreadable(op, why)
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    D.ClearOp()
    H.state.lastError = "deck reply rejected (" .. tostring(why) .. ")"
    D.message = "The server sent a deck reply this addon cannot read. Your decks were reloaded."
    H.PrintError(D.message)
    H.DeckList()
    H.NotifyDecks()
end

function H.OnDeckSaved(msg)
    local op = D.op
    if not op or op.kind == "delete" then
        return                        -- unsolicited: ignored
    end
    local res, why = D.ParseSaved(msg.payload)
    local byReq = op.reqIds[msg.reqId] == true
    if res and res.key ~= op.key then
        if not byReq then
            return                    -- another key: not ours
        end
        res, why = nil, "key does not match"
    end
    if res and op.deckId ~= 0 and res.id ~= op.deckId then
        res, why = nil, "deck id does not match"
    end
    if not res then
        if byReq then
            deckReplyUnreadable(op, why)
        end
        return
    end
    -- confirmed (possibly the reply to an earlier attempt while a later one is in flight)
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    D.ClearOp()
    D.ApplySaved(op, res)
    local dr = op.draft
    if dr and dr == D.draft then
        D.ClearCreate(dr)
        if res.gone then
            dr.deckId, dr.baseRev, dr.rebase = 0, 0, false
            dr.orig = { name = "", cards = {} }
            D.message = "This deck was deleted meanwhile. Save again to create it as a new deck."
        else
            dr.deckId = res.id
            dr.baseRev = res.rev
            dr.rebase = false
            dr.orig = { name = op.name, cards = D.CopyCards(op.cards) }
            if D.IsDirty(dr) then
                -- a replay of an earlier save of this new deck: the latest edits still need a save
                D.message = "An earlier version of this deck was saved. Save again to store your latest changes."
            else
                D.EndEdit()
                D.selected = res.id
                D.message = nil          -- no "saved" line: the deck's tile in the list is the confirmation
            end
        end
    elseif op.kind == "rename" then
        D.message = "Deck renamed to \"" .. op.name .. "\"."
    else
        D.message = nil
    end
    H.DeckList()
    H.NotifyDecks()
end

function H.OnDeckDeleted(msg)
    local op = D.op
    if not op or op.kind ~= "delete" then
        return
    end
    local res, why = D.ParseDeleted(msg.payload)
    local byReq = op.reqIds[msg.reqId] == true
    if res and (res.key ~= op.key or res.id ~= op.deckId) then
        if not byReq then
            return
        end
        res, why = nil, "key or id does not match"
    end
    if not res then
        if byReq then
            deckReplyUnreadable(op, why)
        end
        return
    end
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    D.ClearOp()
    D.ApplyDeleted(res.id)
    D.message = "Deck \"" .. tostring(op.name) .. "\" deleted."
    H.DeckList()
    H.NotifyDecks()
end

-- A failed DECK_SAVE / DECK_DELETE is sent again after H.DECK_RETRY_DELAY (called from H.OnUpdate).
local function deckRetryTick(elapsed)
    local op = D.op
    if op and op.retryIn then
        op.retryIn = op.retryIn - elapsed
        if op.retryIn <= 0 then
            op.retryIn = nil
            if not H.SendDeckOp() then
                H.OnDeckOpFailed("SEND", "", nil, false)
            end
        end
    end
end

-- ------------------------------------------------------------------ match
-- Tells the UI (MatchUI.lua) that the match state changed.
function H.NotifyMatch()
    if H.OnMatchChanged then
        H.OnMatchChanged()
    end
end

-- Asks for the match (a resync: no events, the whole board). Only after WELCOME with "match" in
-- caps, and only one request at a time (a call while one is in flight marks a refetch).
function H.MatchGet()
    if not H.state.welcomed or not H.match.supported then
        return nil
    end
    if H.match.reqId then
        H.match.refetch = true
        return H.match.reqId
    end
    local reqId = H.Request("GAME_GET", "", {})
    if not reqId then
        MA.SetError("request failed")
        H.NotifyMatch()
        return nil
    end
    H.matchReqs[reqId] = "GAME_GET"
    H.match.reqId = reqId
    MA.SetLoading()
    H.NotifyMatch()
    return reqId
end

local function matchGetFinished()
    H.match.reqId = nil
    if H.match.refetch then
        H.match.refetch = false
        H.MatchGet()
    end
end

-- Not connected any more (VERSION / DISABLED), or the server has no matches: nothing is known.
function H.ResetMatch()
    local p = MA.pending
    if p and p.reqId then
        H.pending[p.reqId] = nil
    end
    if H.match.reqId then
        H.pending[H.match.reqId] = nil
    end
    if AR.pausing and AR.pausing.reqId then
        H.pending[AR.pausing.reqId] = nil
    end
    AR.pausing = nil
    H.match.reqId = nil
    H.match.refetch = false
    H.match.announce = false
    H.match.supported = false
    MA.supported = false
    MA.Reset()
    H.NotifyMatch()
end

-- Why no match action can be sent right now, or nil.
function H.MatchActBlocker()
    if not H.state.welcomed then
        return "Not connected to the server."
    end
    if not H.match.supported then
        return "This server does not support matches yet."
    end
    return MA.ActBlocker()
end

-- Sends one action of the match (kind: mull, play, atk, hp, end, concede or pick; a, b, c, d as the
-- Match.lua builders take them: play = source, target, position, Choose One choice; pick = option id). One action is in flight at a time and it is never sent again with
-- the same seq: if the answer is lost or refused as stale, the board is refetched. Returns true, or
-- nil and the reason.
function H.MatchAct(kind, a, b, c, d)
    local why = H.MatchActBlocker()
    if why then
        return nil, why
    end
    local g = MA.game
    local payload = nil
    if kind == "mull" then
        payload = MA.ActMulligan(g, a)
    elseif kind == "play" then
        payload = MA.ActPlay(g, a, b, c, d)
    elseif kind == "atk" then
        payload = MA.ActAttack(g, a, b)
    elseif kind == "hp" then
        payload = MA.ActHeroPower(g, a)
    elseif kind == "end" then
        payload = MA.ActEnd(g)
    elseif kind == "concede" then
        payload = MA.ActConcede(g)
    elseif kind == "pick" then
        payload = MA.ActPick(g, a)
    end
    if not payload then
        return nil, "That action is not known."
    end
    local p = MA.BeginAct(kind, payload)
    local reqId = H.Request("GAME_ACT", payload, {})
    if not reqId then
        MA.ClearAct()
        return nil, "The request could not be sent."
    end
    p.reqId = reqId
    H.matchReqs[reqId] = "GAME_ACT"
    H.NotifyMatch()
    return true
end

-- Leaves a finished match (GAME_LEAVE): the board is forgotten at once, the server drops the
-- finished match. A refusal (the match is still running) brings it back through a refetch.
function H.MatchLeave()
    local g = MA.game
    if not g or g.ph ~= "over" then
        return nil, "No finished match to leave."
    end
    local payload = MA.LeavePayload(g)
    MA.Apply({ ph = "none" })
    if H.state.welcomed and H.match.supported then
        local reqId = H.Request("GAME_LEAVE", payload, {})
        if reqId then
            H.matchReqs[reqId] = "GAME_LEAVE"
        end
    end
    H.NotifyMatch()
    return true
end

-- Pauses a running ARENA duel (GAME_PAUSE): the server saves it and answers GAME ph=none, which closes the window
-- (the run is paused and resumes at the host). Closing the duel window of an arena duel does this, never a surrender.
-- Returns true, or nil and the reason.
function H.MatchPause()
    local g = MA.game
    if not H.state.welcomed or not H.match.supported then
        return nil, "Not connected to the server."
    end
    if not MA.IsRunning(g) then
        return nil, "No running duel to pause."
    end
    if AR.pausing then
        return true                   -- asked already: the answer is on its way
    end
    local reqId = H.Request("GAME_PAUSE", MA.LeavePayload(g), {})
    if not reqId then
        return nil, "The request could not be sent."
    end
    AR.pausing = { mid = g.mid, reqId = reqId }
    H.matchReqs[reqId] = "GAME_PAUSE"
    H.NotifyMatch()
    return true
end

-- GAME: the reply to GAME_GET / GAME_ACT / GAME_LEAVE / GAME_PAUSE, or a push (reqId 0: the match started, the
-- AI acted or the match was interrupted).
function H.OnGame(msg)
    if not H.state.welcomed or not H.match.supported then
        return
    end
    local push = msg.reqId == P.SERVER_PUSH_REQID
    if not push and H.matchReqs[msg.reqId] == nil then
        return                        -- not ours: ignored
    end
    local p = MA.pending
    local forAct = p ~= nil and p.reqId == msg.reqId
    local g, why = MA.Parse(msg.payload)
    if not g then
        H.state.lastError = "match reply rejected (" .. tostring(why) .. ")"
        if forAct then
            MA.ClearAct()
        end
        if AR.pausing and AR.pausing.reqId == msg.reqId then
            AR.pausing = nil
            H.MatchGet()              -- the pause may have been applied: fetch the truth
        end
        MA.SetError("bad reply: " .. tostring(why))
        if msg.reqId == H.match.reqId then
            matchGetFinished()
        end
        H.PrintError("The server sent a match update this addon cannot read.")
        if forAct then
            H.MatchGet()              -- the action may have been applied: fetch the truth
        end
        H.NotifyMatch()
        return
    end
    if forAct then
        MA.ClearAct()
    end
    -- a duel that starts: the run says whether it is an arena duel (the close button pauses it instead of surrendering)
    local started = MA.IsRunning(g) and (MA.game == nil or MA.game.mid ~= g.mid)
    local wasArena = AR.IsArenaDuel() and MA.IsRunning(MA.game)
    local wasLadder = MA.game ~= nil and MA.game.dur and MA.IsRunning(MA.game)   -- dur=1, a ranked ladder duel
    MA.Apply(g, not push and H.matchReqs[msg.reqId] == "GAME_GET")
    if started and H.arena.supported and not AR.IsArenaDuel() then
        H.ArenaGet()
    end
    if started and H.LadderUI then H.LadderUI.Hide() end
    if started and H.HideArena then
        H.HideArena(true)             -- the match window takes over (the arena window comes back after an arena duel)
    end
    -- our pause was applied, or the server suspended an arena duel (logout, teleport, death ...): the run is paused
    local pausing = AR.pausing
    if pausing and (msg.reqId == pausing.reqId or g.ph == "none") then
        AR.pausing = nil
    end
    if g.ph == "none" and wasLadder then
        H.Print("Duel paused. Resume it at a ladder chair in Thunder Bluff or Darnassus.")
    elseif g.ph == "none" and (pausing or wasArena) then
        AR.message = AR.PAUSED_TEXT
        H.Print(AR.PAUSED_TEXT)
    end
    if msg.reqId == H.match.reqId then
        matchGetFinished()
    end
    if H.match.announce and not push then
        H.match.announce = false
        if g.ph == "none" then
            H.Print("You have no match in progress. Talk to a Hearthstone opponent to start one.")
        end
    end
    H.NotifyMatch()
end

-- An ERR / timeout answered the GAME_PAUSE in flight. A refusal that names the match is definitive (nothing changed;
-- "ordinary" means the run in our picture was stale: this is no arena duel, so the close button asks to surrender as
-- for any match); anything else leaves the outcome unknown, so the board is refetched.
function H.OnMatchPauseFailed(code, text, why, direct)
    local p = AR.pausing
    if not p then
        return
    end
    if p.reqId then
        H.pending[p.reqId] = nil
    end
    AR.pausing = nil
    if direct and code == "REFUSED" then
        MA.message = MA.RefusedText(why, text)
        if why == "ordinary" then
            H.ArenaGet()
            if H.MatchUI and H.MatchUI.AskSurrender then
                H.MatchUI.AskSurrender()
            end
        else
            H.MatchGet()
        end
    else
        MA.message = "The server did not confirm the pause. The board was refreshed."
        H.state.lastError = "GAME_PAUSE not confirmed (" .. tostring(code) .. ")"
        H.MatchGet()
    end
    H.PrintError("The duel was not paused: " .. MA.message)
    H.NotifyMatch()
end

-- An ERR / timeout answered the GAME_GET in flight.
function H.OnMatchGetFailed(text)
    if H.match.reqId then
        H.pending[H.match.reqId] = nil
    end
    H.match.announce = false
    MA.SetError(text)
    matchGetFinished()
    H.NotifyMatch()
end

-- The GAME_ACT in flight failed. A refusal answered to this very reqId is definitive: nothing
-- changed on the server, and only a stale / unknown match is refetched. Anything else leaves the
-- outcome unknown (the action may have been applied), so the board is refetched.
function H.OnMatchActFailed(code, text, why, direct)
    local p = MA.pending
    if not p then
        return
    end
    if p.reqId then
        H.pending[p.reqId] = nil
    end
    MA.ClearAct()
    if direct and code == "REFUSED" and why then
        MA.message = MA.RefusedText(why, text)
        if why == "stale" or why == "match" or why == "over" then
            H.MatchGet()
        end
    elseif direct and code == "MALFORMED" then
        MA.message = "The server rejected that action (MALFORMED)."
    elseif direct and code == "RATE" then
        MA.message = "Too many actions at once, try again."
    else
        MA.message = "The server did not confirm the action. The board was refreshed."
        H.state.lastError = "GAME_ACT not confirmed (" .. tostring(code) .. ")"
        H.MatchGet()
    end
    H.NotifyMatch()
end

-- ------------------------------------------------------------ wallet
-- Tells the UI (CollectionUI.lua, CraftUI.lua, MerchantCoins.lua) that the coins, the dust, the crafting access or
-- the craft in flight changed.
function H.NotifyWallet()
    if H.OnWalletChanged then
        H.OnWalletChanged()
    end
    if H.MerchantCoins and H.MerchantCoins.Update then
        H.MerchantCoins.Update()
    end
end

-- Asks for the coins, the dust, the dust values and the crafting access. Only after WELCOME with "wallet" in
-- caps, and only one request at a time (a call while one is in flight marks a refetch).
function H.WalletGet()
    if not H.state.welcomed or not H.wallet.supported then
        return nil
    end
    if H.wallet.reqId then
        H.wallet.refetch = true
        return H.wallet.reqId
    end
    local reqId = H.Request("WALLET_GET", "", {})
    if not reqId then
        W.SetError("request failed")
        H.NotifyWallet()
        return nil
    end
    H.walletReqs[reqId] = "WALLET_GET"
    H.wallet.reqId = reqId
    W.SetLoading()
    H.NotifyWallet()
    return reqId
end

local function walletFinished()
    H.wallet.reqId = nil
    if H.wallet.refetch then
        H.wallet.refetch = false
        H.WalletGet()
    end
end

-- Not connected any more (VERSION / DISABLED), or the server has no wallet.
function H.ResetWallet(unsupported)
    local op = W.op
    if op and op.reqId then
        H.pending[op.reqId] = nil
    end
    H.wallet.reqId = nil
    H.wallet.refetch = false
    H.wallet.supported = false
    if unsupported then
        W.SetUnsupported()
    else
        W.Reset()
    end
    H.NotifyWallet()
end

function H.OnWallet(msg)
    if msg.reqId ~= H.wallet.reqId then
        return                        -- unsolicited or late (after a timeout): ignored
    end
    local res, why = W.Parse(msg.payload)
    if res then
        W.SetReady(res)
    else
        W.SetError("bad reply: " .. tostring(why))
        H.state.lastError = "wallet reply rejected (" .. tostring(why) .. ")"
        H.PrintError("The server sent coins and dust this addon cannot read.")
    end
    walletFinished()
    H.NotifyWallet()
end

-- An ERR / timeout answered the WALLET_GET in flight.
function H.OnWalletGetFailed(text)
    if H.wallet.reqId then
        H.pending[H.wallet.reqId] = nil
    end
    W.SetError(text)
    walletFinished()
    H.NotifyWallet()
end

-- Server push (reqId 0): coins or dust changed (a match reward, a purchase at a vendor, a GM).
function H.OnWalletUpd(msg)
    if not H.state.welcomed or not H.wallet.supported then
        return
    end
    local u = W.ParseUpd(msg.payload)
    if not u or W.state ~= "ready" then
        if W.state ~= "loading" then
            H.WalletGet()             -- an unreadable push or no wallet yet: fetch the truth
        end
        return
    end
    W.ApplyBalances(u.coins, u.dust)
    H.NotifyWallet()
end

-- BAG_UPDATE: the coin items in the bags changed (bought a pack, mail, bank, trade): recount them.
function H.OnBagUpdate()
    if W.state ~= "ready" then
        return
    end
    local n = W.CountBags(GetContainerNumSlots, GetContainerItemLink, GetContainerItemInfo)
    if n ~= nil and n ~= W.coins then
        W.coins = n
        H.NotifyWallet()
    end
end

-- Server push (reqId 0): a crafting NPC was opened (the collection opens in crafting mode) or left.
function H.OnCraftAccess(msg)
    if not H.state.welcomed or not H.wallet.supported then
        return
    end
    local open, npc = W.ParseAccess(msg.payload)
    if open == nil then
        return
    end
    W.SetAccess(open, npc)
    if open and H.ShowCraft then
        H.ShowCraft()
    end
    H.NotifyWallet()
end

-- Why no craft or scrap can be sent at all right now, or nil.
function H.CraftBlocker()
    if not H.state.welcomed then
        return "Not connected to the server."
    end
    if W.state == "unsupported" then
        return "This server does not support crafting yet."
    elseif W.state == "loading" then
        return "Your coins and dust are still loading."
    elseif W.state ~= "ready" then
        return "Your coins and dust could not be loaded."
    end
    if not W.craftOpen then
        return W.WHY_TEXT.npc
    end
    if W.IsBusy() then
        return "A card is being crafted."
    end
    return nil
end

-- The request op of a craft op kind.
local CRAFT_OPS = { craft = "CRAFT_MAKE", scrap = "CRAFT_SCRAP", extras = "CRAFT_EXTRAS" }

-- Sends CRAFT_MAKE / CRAFT_SCRAP / CRAFT_EXTRAS for the op in flight (a fresh reqId every time, always the same key).
function H.SendCraftOp()
    local op = W.op
    if not op then
        return nil
    end
    op.retryIn = nil
    if not H.state.welcomed then
        op.inFlight = false
        return nil
    end
    local reqId = H.Request(CRAFT_OPS[op.kind], W.OpPayload(op), {})
    if not reqId then
        op.inFlight = false
        return nil
    end
    op.attempts = (op.attempts or 0) + 1
    op.reqId = reqId
    op.reqIds[reqId] = true
    H.walletReqs[reqId] = op.kind
    return reqId
end

local function startCraftOp(kind, id, n)
    local why = H.CraftBlocker()
    if why then
        return nil, why
    end
    local card = HS_CardDB and HS_CardDB.cards and HS_CardDB.cards[id]
    if kind == "craft" then
        why = W.CraftBlocker(card, O.Get(id))
    else
        why = W.ScrapBlocker(card, O.Get(id))
    end
    if why then
        return nil, why
    end
    local op = W.BeginOp(kind, id, n)
    if not H.SendCraftOp() then
        W.ClearOp()
        W.message = "The request could not be sent."
        H.NotifyWallet()
        return nil, W.message
    end
    H.NotifyWallet()
    return op.key
end

-- Crafts one copy of a card at the open crafting NPC. Returns the key, or nil and why it cannot.
function H.CraftCard(id)
    return startCraftOp("craft", id, 1)
end

-- Scraps n (default 1) unprotected copies of a card at the open crafting NPC.
function H.ScrapCard(id, n)
    return startCraftOp("scrap", id, n or 1)
end

-- Scraps every extra copy of the collection (the copies above what a deck can use, never a protected one) at the
-- open crafting NPC. Returns the key, or nil and why it cannot.
function H.ScrapExtras()
    local why = H.CraftBlocker()
    if why then
        return nil, why
    end
    local copies = W.Extras(O.IsReady() and O.owned or nil, HS_CardDB and HS_CardDB.cards)
    why = W.ExtrasBlocker(copies)
    if why then
        return nil, why
    end
    local op = W.BeginOp("extras", nil, copies)
    if not H.SendCraftOp() then
        W.ClearOp()
        W.message = "The request could not be sent."
        H.NotifyWallet()
        return nil, W.message
    end
    H.NotifyWallet()
    return op.key
end

local function cardName(id)
    local card = HS_CardDB and HS_CardDB.cards and HS_CardDB.cards[id]
    return card and card.name or tostring(id)
end

function H.OnCrafted(msg)
    local op = W.op
    if not op then
        return                        -- unsolicited: ignored
    end
    local res, why = W.ParseCrafted(msg.payload)
    local byReq = op.reqIds[msg.reqId] == true
    if res and res.key ~= op.key then
        if not byReq then
            return                    -- another key: not ours
        end
        res, why = nil, "key does not match"
    end
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    if not res then
        if not byReq then
            return
        end
        W.ClearOp()
        H.state.lastError = "craft reply rejected (" .. tostring(why) .. ")"
        W.message = "The server sent a reply this addon cannot read."
        H.PrintError(W.message)
        H.WalletGet()
        H.CollGet()
        H.NotifyWallet()
        return
    end
    W.ClearOp()
    W.ApplyBalances(nil, res.dust)
    if res.kind == "extras" then
        W.message = "Disenchanted " .. res.n .. " extra card" .. (res.n == 1 and "" or "s") .. " for " .. res.delta .. " dust."
    elseif res.kind == "craft" then
        W.message = "Crafted " .. cardName(res.card) .. " for " .. (-res.delta) .. " dust."
    else
        W.message = "Scrapped " .. (res.n > 1 and (res.n .. "x ") or "") .. cardName(res.card) .. " for " .. res.delta .. " dust."
    end
    if PlaySound then
        PlaySound(res.kind == "craft" and "LEVELUPSOUND" or "igMainMenuOptionCheckBoxOn")
    end
    if not O.IsReady() or res.ver ~= O.version then
        H.CollGet(res.ver)
    end
    H.NotifyWallet()
end

-- The craft / scrap in flight failed. Definitive errors (answered to this very reqId) end it: nothing
-- changed on the server. Anything else leaves the outcome unknown, so it is sent again with the same key, up
-- to W.MAX_ATTEMPTS sends; then the addon stops and resyncs.
function H.OnCraftFailed(code, text, why, direct)
    local op = W.op
    if not op then
        return
    end
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    op.reqId = nil
    if direct and (code == "REFUSED" or code == "MALFORMED") then
        W.ClearOp()
        if code == "REFUSED" then
            W.message = (why and W.WHY_TEXT[why]) or "The server refused: " .. tostring(text)
            if why == "npc" then
                W.SetAccess(false)
            elseif why == "dust" or why == "copies" or why == "limit" or why == "nothing" then
                H.WalletGet()         -- our view was stale: resync
                H.CollGet()
            end
        else
            W.message = "The server rejected the request (MALFORMED). Nothing was changed."
        end
        H.NotifyWallet()
        return
    end
    if (op.attempts or 0) < W.MAX_ATTEMPTS and H.state.welcomed then
        op.retryIn = H.CRAFT_RETRY_DELAY
        return
    end
    W.ClearOp()
    W.message = "The server did not confirm. Your collection and dust were refreshed; trying again is safe."
    H.state.lastError = "craft not confirmed (" .. tostring(code) .. ")"
    H.PrintError(W.message)
    H.WalletGet()
    H.CollGet()
    H.NotifyWallet()
end

-- A failed craft / scrap is sent again after H.CRAFT_RETRY_DELAY (called from H.OnUpdate).
local function craftRetryTick(elapsed)
    local op = W.op
    if op and op.inFlight and op.retryIn then
        op.retryIn = op.retryIn - elapsed
        if op.retryIn <= 0 then
            op.retryIn = nil
            if not H.SendCraftOp() then
                H.OnCraftFailed("SEND", "", nil, false)
            end
        end
    end
end

-- ------------------------------------------------------- hero portraits
-- Tells the UI (HeroesUI.lua, the portrait call sites) that the portraits, the credits or the op in flight changed.
function H.NotifyHeroes()
    if H.OnHeroesChanged then
        H.OnHeroesChanged()
    end
end

-- Asks for the credits, the unlocked portraits and the choices. Only after WELCOME with "heroes" in caps, and only one
-- request at a time (a call while one is in flight marks a refetch).
function H.HeroGet()
    if not H.state.welcomed or not H.heroes.supported then
        return nil
    end
    if H.heroes.reqId then
        H.heroes.refetch = true
        return H.heroes.reqId
    end
    local reqId = H.Request("HERO_GET", "", {})
    if not reqId then
        E.SetError("request failed")
        H.NotifyHeroes()
        return nil
    end
    H.heroReqs[reqId] = "HERO_GET"
    H.heroes.reqId = reqId
    E.SetLoading()
    H.NotifyHeroes()
    return reqId
end

local function heroesFinished()
    H.heroes.reqId = nil
    if H.heroes.refetch then
        H.heroes.refetch = false
        H.HeroGet()
    end
end

-- Not connected any more (VERSION / DISABLED), or the server has no hero portraits.
function H.ResetHeroes(unsupported)
    local op = E.op
    if op and op.reqId then
        H.pending[op.reqId] = nil
    end
    if H.heroes.reqId then
        H.pending[H.heroes.reqId] = nil
    end
    H.heroes.reqId = nil
    H.heroes.refetch = false
    H.heroes.supported = false
    if unsupported then
        E.SetUnsupported()
    else
        E.Reset()
    end
    H.NotifyHeroes()
end

-- Why no select / unlock can be sent right now, or nil.
function H.HeroBlocker()
    if not H.state.welcomed then
        return "Not connected to the server."
    elseif E.state == "unsupported" then
        return "This server does not have alternative hero portraits yet."
    elseif E.state ~= "ready" then
        return "The portraits are not loaded."
    elseif E.IsBusy() then
        return "A portrait change is in progress."
    end
    return nil
end

-- Sends HERO_SELECT / HERO_UNLOCK for the op in flight (a fresh reqId every time, always the same key).
function H.SendHeroOp()
    local op = E.op
    if not op then
        return nil
    end
    if not H.state.welcomed then
        op.inFlight = false
        return nil
    end
    local name = E.OPS[op.kind]
    local reqId = H.Request(name, E.OpPayload(op), {})
    if not reqId then
        E.ClearOp()
        return nil
    end
    op.attempts = op.attempts + 1
    op.reqId = reqId
    H.heroReqs[reqId] = name
    return reqId
end

local function startHeroOp(kind, class, id, blocker)
    local why = H.HeroBlocker() or blocker
    if why then
        return nil, why
    end
    E.BeginOp(kind, class, id)
    local reqId = H.SendHeroOp()
    H.NotifyHeroes()
    if not reqId then
        return nil, "The request could not be sent."
    end
    return reqId
end

-- Chooses the portrait of a class (id may be the class default). Nothing is sent for a locked portrait.
-- Returns the reqId, or nil and the reason.
function H.HeroSelect(class, id)
    return startHeroOp("select", class, id, E.SelectBlocker(class, id))
end

-- Spends a credit on a locked portrait. Returns the reqId, or nil and the reason.
function H.HeroUnlock(id)
    return startHeroOp("unlock", (E.Find(id)), id, E.UnlockBlocker(id))
end

function H.OnHeroes(msg)
    local op = E.op
    local byOp = op ~= nil and op.reqId ~= nil and msg.reqId == op.reqId
    local byGet = msg.reqId == H.heroes.reqId
    if not byOp and not byGet then
        return                        -- unsolicited or late (after a timeout): ignored
    end
    local res, why = E.Parse(msg.payload)
    if byOp then
        H.pending[op.reqId] = nil
        E.ClearOp()
    end
    if res then
        E.SetReady(res)
        if byOp then
            if op.kind == "unlock" then
                local _, entry = E.Find(op.id)
                E.message = "Unlocked " .. (entry and entry.name or op.id) .. "."
            else
                E.message = nil
            end
        end
    else
        H.state.lastError = "heroes reply rejected (" .. tostring(why) .. ")"
        if byGet then
            E.SetError("bad reply: " .. tostring(why))
        end
        H.PrintError("The server sent hero portraits this addon cannot read.")
        if byOp then
            H.HeroGet()
        end
    end
    if byGet then
        heroesFinished()
    end
    H.NotifyHeroes()
end

-- An ERR / timeout answered the HERO_GET in flight.
function H.OnHeroGetFailed(text)
    if H.heroes.reqId then
        H.pending[H.heroes.reqId] = nil
    end
    E.SetError(text)
    heroesFinished()
    H.NotifyHeroes()
end

-- An ERR / timeout answered the select / unlock in flight. direct: the error carries this op's reqId. A refusal is
-- definitive; anything else is sent again (same key, so it cannot be applied twice) up to E.MAX_ATTEMPTS times.
function H.OnHeroOpFailed(code, text, why, direct)
    local op = E.op
    if not op then
        return
    end
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    op.reqId = nil
    if direct and (code == "REFUSED" or code == "MALFORMED") then
        E.ClearOp()
        if code == "REFUSED" then
            E.message = (why and E.WHY_TEXT[why]) or "The server refused: " .. tostring(text)
            H.HeroGet()               -- our view was stale: resync
        else
            E.message = "The server rejected the request (MALFORMED). Nothing was changed."
        end
        H.NotifyHeroes()
        return
    end
    if op.attempts < E.MAX_ATTEMPTS and H.state.welcomed then
        if H.SendHeroOp() then
            return
        end
    end
    E.ClearOp()
    E.message = "The server did not confirm. Your portraits were refreshed; trying again is safe."
    H.state.lastError = "hero change not confirmed (" .. tostring(code) .. ")"
    H.PrintError(E.message)
    H.HeroGet()
    H.NotifyHeroes()
end

-- Server push (reqId 0): a credit was earned (Legend #1) or a GM changed something.
function H.OnHeroChanged(msg)
    if not H.state.welcomed or not H.heroes.supported then
        return
    end
    if E.ParseChanged(msg.payload) == "legend1" then
        H.Print("Legend #1! You earned a hero portrait credit. Open the Heroes tab to choose a portrait.")
    end
    H.HeroGet()
end

-- ------------------------------------------------------------ draft arena
-- Tells the UI (ArenaUI.lua, MatchUI.lua) that the arena run, the step in flight or the pause changed.
function H.NotifyArena()
    if H.OnArenaChanged then
        H.OnArenaChanged()
    end
    if H.MatchUI and H.MatchUI.RefreshSurrender then
        H.MatchUI.RefreshSurrender()
    end
end

-- Asks for the account's run. Only after WELCOME with "arena" in caps, and only one request at a time (a call while
-- one is in flight marks a refetch).
function H.ArenaGet()
    if not H.state.welcomed or not H.arena.supported then
        return nil
    end
    if H.arena.reqId then
        H.arena.refetch = true
        return H.arena.reqId
    end
    local reqId = H.Request("ARENA_GET", "", {})
    if not reqId then
        AR.SetError("request failed")
        H.NotifyArena()
        return nil
    end
    H.arenaReqs[reqId] = "ARENA_GET"
    H.arena.reqId = reqId
    AR.SetLoading()
    H.NotifyArena()
    return reqId
end

local function arenaFinished()
    H.arena.reqId = nil
    if H.arena.refetch then
        H.arena.refetch = false
        H.ArenaGet()
    end
end

-- Not connected any more (VERSION / DISABLED), or the server has no arena. An unconfirmed step is forgotten: the same
-- step again is a replay on the server anyway.
function H.ResetArena(unsupported)
    local op = AR.op
    if op and op.reqId then
        H.pending[op.reqId] = nil
    end
    AR.ClearOp()
    H.arena.reqId = nil
    H.arena.refetch = false
    H.arena.supported = false
    if unsupported then
        AR.SetUnsupported()
    else
        AR.Reset()
    end
    H.NotifyArena()
end

-- ARENA: the reply to ARENA_GET or to the step in flight (the model is replaced, never merged).
function H.OnArena(msg)
    if not H.state.welcomed or not H.arena.supported then
        return
    end
    local op = AR.op
    local byOp = op ~= nil and op.reqIds[msg.reqId] == true
    local byGet = msg.reqId == H.arena.reqId
    if not byOp and not byGet then
        return                        -- unsolicited or late (after a timeout): ignored
    end
    local res, why = AR.ParseArena(msg.payload)
    if not res then
        H.state.lastError = "arena reply rejected (" .. tostring(why) .. ")"
        if byOp then
            -- an answer to our request that we cannot read: stop, resync
            if op.reqId then
                H.pending[op.reqId] = nil
            end
            AR.ClearOp()
            AR.message = "The server sent a reply this addon cannot read."
            H.PrintError(AR.message)
            H.ArenaGet()
        else
            AR.SetError("bad reply: " .. tostring(why))
            H.PrintError("The server sent " .. AR.NAME .. " data this addon cannot read.")
            arenaFinished()
        end
        H.NotifyArena()
        return
    end
    if byOp then
        if op.reqId then
            H.pending[op.reqId] = nil
        end
        AR.ClearOp()
        AR.message = AR.DoneText(op, res)
        if PlaySound then
            PlaySound("igMainMenuOptionCheckBoxOn")
        end
    end
    AR.SetReady(res)
    if byGet then
        arenaFinished()
    end
    if byOp and op.kind == "claim" then
        H.CollGet()                   -- the special card or the extra pack is in the collection / the stock now
        H.PackGet()
    end
    if byOp and op.kind == "retire" then
        H.Print(AR.message or AR.SettledText(res))
        H.PackGet()                   -- the prize packs are in the stock now
    end
    H.NotifyArena()
end

-- Server push (reqId 0): ARENA_UPD (the run changed; also after our own steps, with the state of the reply) or
-- ARENA_OPEN (the player chose "Continue drafting" at the host: show the window).
function H.OnArenaPush(msg, open)
    if not H.state.welcomed or not H.arena.supported then
        return
    end
    local res = AR.ParseArena(msg.payload)
    if not res then
        if AR.state ~= "loading" then
            H.ArenaGet()              -- an unreadable push: fetch the truth
        end
        return
    end
    local prev = AR.run
    local wasReady = AR.state == "ready"
    if open or not AR.IsStale(res) then
        AR.SetReady(res)
        if wasReady and prev.run == res.run and prev.st ~= "settled" and res.st == "settled" then
            H.Print(AR.SettledText(res))
            H.PackGet()               -- the prize packs are in the stock now
        end
    end
    if open then
        AR.atHost = true              -- opened by a host: Play and Retire work until ARENA_CLOSE
    end
    H.NotifyArena()
    if open and H.ShowArena then
        H.ShowArena()
    end
end

-- Server push ARENA_CLOSE (reqId 0): the player walked away from the host that opened the window.
function H.OnArenaClose()
    if not AR.atHost then
        return
    end
    AR.atHost = false
    if H.HideArena then
        H.HideArena()
    end
    H.NotifyArena()
end

-- Why no step can be sent right now, or nil.
function H.ArenaBlocker()
    if not H.state.welcomed then
        return "Not connected to the server."
    end
    local s = AR.state
    if s == "unsupported" then
        return "This server does not support the " .. AR.NAME .. " yet."
    elseif s == "loading" then
        return "Your run is still loading."
    elseif s ~= "ready" then
        return "Your run could not be loaded."
    end
    return nil
end

-- Sends ARENA_CLASS / ARENA_PICK / ARENA_CLAIM for the step in flight (a fresh reqId every time, always the same key).
function H.SendArenaOp()
    local op = AR.op
    if not op then
        return nil
    end
    op.retryIn = nil
    if not H.state.welcomed then
        op.inFlight = false
        return nil
    end
    local reqId = H.Request(AR.OPS[op.kind], AR.OpPayload(op), {})
    if not reqId then
        op.inFlight = false
        return nil
    end
    op.attempts = (op.attempts or 0) + 1
    op.reqId = reqId
    op.reqIds[reqId] = true
    H.arenaReqs[reqId] = op.kind
    return reqId
end

local function startArenaOp(kind, arg)
    local why = H.ArenaBlocker()
    if why then
        return nil, why
    end
    why = AR.OpBlocker(kind, arg)
    if why then
        return nil, why
    end
    local op = AR.BeginOp(kind, arg)
    if not H.SendArenaOp() then
        AR.ClearOp()
        AR.message = "The request could not be sent."
        H.NotifyArena()
        return nil, AR.message
    end
    H.NotifyArena()
    return op.key
end

-- Chooses one of the three offered classes. Returns the idempotency key, or nil and why it cannot.
function H.ArenaChooseClass(class)
    return startArenaOp("class", class)
end

-- Picks one card of the open offer.
function H.ArenaPick(id)
    return startArenaOp("pick", id)
end

-- Claims the special card of a ten-win run.
function H.ArenaClaim(id)
    return startArenaOp("claim", id)
end

-- The Play button (only at a host): starts the next duel of a ready run or resumes the paused one.
function H.ArenaDuel()
    return startArenaOp("duel", nil)
end

-- The Retire button (only at a host, after the confirmation): ends the run and settles its prize.
function H.ArenaRetire()
    return startArenaOp("retire", nil)
end

-- The ARENA_GET in flight failed (an ERR or a timeout).
function H.OnArenaGetFailed(text)
    if H.arena.reqId then
        H.pending[H.arena.reqId] = nil
    end
    AR.SetError(text)
    arenaFinished()
    H.NotifyArena()
end

-- The step in flight failed. Definitive errors (answered to this very reqId) end it: nothing changed on the server.
-- Anything else leaves the outcome unknown, so it is sent again with the same key, up to AR.MAX_ATTEMPTS sends; then
-- the addon stops and resyncs (a step again later is a replay or a new step, never a double one).
function H.OnArenaOpFailed(code, text, why, direct)
    local op = AR.op
    if not op then
        return
    end
    if op.reqId then
        H.pending[op.reqId] = nil
    end
    op.reqId = nil
    if direct and (code == "REFUSED" or code == "MALFORMED") then
        AR.ClearOp()
        if code == "REFUSED" then
            AR.message = AR.RefusedText(why, text)
        else
            AR.message = "The server rejected the request (MALFORMED). Nothing was changed."
        end
        H.PrintError(AR.message)
        H.ArenaGet()                  -- our view was stale or wrong: resync
        H.NotifyArena()
        return
    end
    if (op.attempts or 0) < AR.MAX_ATTEMPTS and H.state.welcomed then
        op.retryIn = H.ARENA_RETRY_DELAY
        return
    end
    AR.ClearOp()
    AR.message = "The server did not confirm your choice. Your run was refreshed; choosing again is safe."
    H.state.lastError = "arena step not confirmed (" .. tostring(code) .. ")"
    H.PrintError(AR.message)
    H.ArenaGet()
    H.NotifyArena()
end

-- A failed step is sent again after H.ARENA_RETRY_DELAY (called from H.OnUpdate).
local function arenaRetryTick(elapsed)
    local op = AR.op
    if op and op.inFlight and op.retryIn then
        op.retryIn = op.retryIn - elapsed
        if op.retryIn <= 0 then
            op.retryIn = nil
            if not H.SendArenaOp() then
                H.OnArenaOpFailed("SEND", "", nil, false)
            end
        end
    end
end

-- ------------------------------------------------------------------ receive
function H.OnAddonMessage(prefix, message, channel, sender)
    -- Warsong Battlegrounds: the WRBG namespace, routed to BgState.lua. The WRHS path below is unchanged.
    if prefix == (HsBgProtocol and HsBgProtocol.PREFIX or "WRBG") and channel == "GUILD" then
        if HsBg and HsBg.OnAddonMessage then
            HsBg.OnAddonMessage(message, sender)
        end
        return
    end
    if prefix ~= P.PREFIX or channel ~= "GUILD" then
        return
    end
    -- The server speaks as the player itself; guild mates' lines carry their own name.
    local me = nil
    if UnitName then
        me = UnitName("player")
    end
    if me ~= nil and sender ~= me then
        return
    end
    if type(message) ~= "string" then
        return
    end
    H.HandleFrame(message)
end

function H.HandleFrame(frame)
    local status, f = P.ParseFrame(frame)
    if status == "badversion" then
        H.state.lastErrorCode = "VERSION"
        H.state.lastError = "the server uses a different protocol version"
        H.PrintError("The server speaks a different protocol version. Update the Hearthstone addon or the server.")
        return
    end
    if status ~= "ok" then
        H.state.lastError = "malformed frame from server"
        return
    end
    local result, done = H.reasm:Add(now(), f.op, f.reqId, f.part, f.total, f.payload)
    if result == "complete" then
        H.Dispatch(done)
    elseif result == "malformed" or result == "toolarge" or result == "busy" then
        H.state.lastError = "dropped a reply (" .. result .. ")"
    end
end

function H.Dispatch(msg)
    local meta = H.pending[msg.reqId]
    if meta then
        H.pending[msg.reqId] = nil
    end

    if H.LadderUI and msg.op == "ERR" and H.LadderUI.OnError(msg) then
        return
    elseif H.LadderUI and (msg.op == "LADDER" or msg.op == "LADDER_OPEN") then
        H.LadderUI.OnMessage(msg, msg.op == "LADDER_OPEN")
    elseif H.LadderUI and msg.op == "LADDER_CLOSE" then
        H.LadderUI.request = nil
        H.LadderUI.Hide()
    elseif msg.op == "WELCOME" then
        H.OnWelcome(msg)
    elseif msg.op == "ERR" then
        H.OnError(msg)
    elseif msg.op == "COLL" then
        H.OnColl(msg)
    elseif msg.op == "COLL_CHANGED" then
        H.OnCollChanged(msg)
    elseif msg.op == "PACKS" then
        H.OnPacks(msg)
    elseif msg.op == "PACK_OPENED" then
        H.OnPackOpened(msg)
    elseif msg.op == "PACK_CHANGED" then
        H.OnPackChanged(msg)
    elseif msg.op == "DECKS" then
        H.OnDecks(msg)
    elseif msg.op == "DECK_SAVED" then
        H.OnDeckSaved(msg)
    elseif msg.op == "DECK_DELETED" then
        H.OnDeckDeleted(msg)
    elseif msg.op == "DECK_CHANGED" then
        H.OnDeckChanged(msg)
    elseif msg.op == "GAME" then
        H.OnGame(msg)
    elseif msg.op == "HEROES" then
        H.OnHeroes(msg)
    elseif msg.op == "HERO_CHANGED" then
        H.OnHeroChanged(msg)
    elseif msg.op == "WALLET" then
        H.OnWallet(msg)
    elseif msg.op == "WALLET_UPD" then
        H.OnWalletUpd(msg)
    elseif msg.op == "CRAFT_ACCESS" then
        H.OnCraftAccess(msg)
    elseif msg.op == "CRAFTED" then
        H.OnCrafted(msg)
    elseif msg.op == "ARENA" then
        H.OnArena(msg)
    elseif msg.op == "ARENA_UPD" then
        H.OnArenaPush(msg, false)
    elseif msg.op == "ARENA_OPEN" then
        H.OnArenaPush(msg, true)
    elseif msg.op == "ARENA_CLOSE" then
        H.OnArenaClose()
    elseif msg.op == "INTRO_DONE" then
        H.OnIntroDone()
    end
end

-- ------------------------------------------------------- introduction
local function realmKey()
    local r = GetRealmName and GetRealmName()
    if type(r) ~= "string" or r == "" then
        r = "?"
    end
    return r
end

-- Has the account finished the introduction's card quest? WELCOME intro=, before the
-- handshake the remembered answer of this realm. Gates the micro menu button (MicroButton.lua).
function H.IntroUnlocked()
    if H.state.intro ~= nil then
        return H.state.intro
    end
    local db = H.db
    return type(db) == "table" and type(db.introDone) == "table" and db.introDone[realmKey()] == true
end

function H.SetIntro(done)
    H.state.intro = done and true or false
    local db = H.db
    if type(db) == "table" then
        if type(db.introDone) ~= "table" then
            db.introDone = {}
        end
        db.introDone[realmKey()] = done and true or nil
    end
    if H.MicroButton and H.MicroButton.Refresh then
        H.MicroButton.Refresh()      -- unlocked: the Hearthstone button; not yet: the map button hidden
    end
end

-- INTRO_DONE push: the card quest was just turned in.
function H.OnIntroDone()
    H.SetIntro(true)
end

function H.OnWelcome(msg)
    local kv = P.KvDecode(msg.payload)
    if not kv or kv.protocol ~= tostring(H.PROTOCOL) then
        H.state.welcomed = false
        H.state.lastErrorCode = "VERSION"
        H.state.lastError = "unexpected WELCOME"
        H.PrintError("The server answered with an incompatible handshake. Update the Hearthstone addon or the server.")
        return
    end
    H.state.welcomed = true
    H.state.welcome = kv
    H.SetIntro(kv.intro == "1")
    H.state.lastError = nil
    H.state.lastErrorCode = nil
    H.CheckDataset(kv)
    H.CollGet()
    if PK.CapsHavePacks(kv.caps) then
        H.packs.supported = true
        H.PackGet()
    else
        H.ResetPacks(true)
    end
    if D.CapsHaveDecks(kv.caps) then
        H.decks.supported = true
        H.DeckList()
    else
        H.ResetDecks(true)
    end
    if MA.CapsHaveMatch(kv.caps) then
        H.match.supported = true
        MA.supported = true
        H.MatchGet()                  -- restores the match window after /reload
    else
        H.ResetMatch(true)
    end
    if W.CapsHaveWallet(kv.caps) then
        H.wallet.supported = true
        H.WalletGet()
    else
        H.ResetWallet(true)
    end
    if AR.CapsHaveArena(kv.caps) then
        H.arena.supported = true
        H.ArenaGet()
    else
        H.ResetArena(true)
    end
    if E.CapsHaveHeroes(kv.caps) then
        H.heroes.supported = true
        H.HeroGet()
    else
        H.ResetHeroes(true)
    end
    if HsBg and HsBg.Restore then
        HsBg.Restore()                -- Battlegrounds: restores an open game after /reload (no reply = no game)
    end
end

-- Compares the card database shipped with the addon (CardDB.lua) with the dataset the
-- server announced in WELCOME. Sets state.datasetStatus to "ok", "mismatch", "nocarddb" or
-- "unknown" (server sent no hash) and warns once per handshake on a problem.
function H.CheckDataset(kv)
    local db = HS_CardDB
    local s = H.state
    if type(db) ~= "table" then
        s.datasetStatus = "nocarddb"
        H.PrintError("Card database CardDB.lua is missing or failed to load. Reinstall the Hearthstone addon.")
        return
    end
    if kv.hash == nil or kv.hash == "-" then
        s.datasetStatus = "unknown"
        return
    end
    if kv.hash == db.hash and (kv.dataset == nil or kv.dataset == db.datasetId) then
        s.datasetStatus = "ok"
        return
    end
    s.datasetStatus = "mismatch"
    H.PrintError("Card data mismatch: the server uses dataset " .. (kv.dataset or "?") .. " (" .. kv.hash
        .. ") but this addon has " .. tostring(db.datasetId) .. " (" .. tostring(db.hash)
        .. "). Update the Hearthstone addon or the server so that both use the same card data.")
end

function H.OnError(msg)
    local kv = P.KvDecode(msg.payload) or {}
    local code = kv.code or "?"
    local text = kv.msg or ""
    H.state.lastErrorCode = code
    H.state.lastError = code .. ": " .. text
    local answered = msg.reqId
    if msg.reqId == P.SERVER_PUSH_REQID then
        -- The server could not attribute this error to a request: it answers the
        -- oldest one. Forget it so no extra "timed out" line follows.
        local oldestId, oldestAt = nil, nil
        for id, meta in pairs(H.pending) do
            if oldestAt == nil or meta.sentAt < oldestAt then
                oldestId, oldestAt = id, meta.sentAt
            end
        end
        if oldestId then
            H.pending[oldestId] = nil
        end
        answered = oldestId
    end
    local gone = (code == "VERSION" or code == "DISABLED")
    if gone then
        -- the connection is gone: back to the plain catalog and no packs, no refetch
        H.state.welcomed = false
        H.ResetCollection()
        H.ResetPacks(false)
        H.ResetDecks(false)
        H.ResetMatch(false)
        H.ResetWallet(false)
        H.ResetArena(false)
        H.ResetHeroes(false)
    end
    -- UNAVAILABLE is sent for the collection, the packs and the decks. One carrying a pack or
    -- deck request's reqId (also a late one) belongs to those; any other one, also an
    -- unattributed one (reqId 0), fails the COLL_GET in flight.
    local packReq = H.packReqs[msg.reqId] ~= nil
    local deckReq = H.deckReqs[msg.reqId] ~= nil
    local matchReq = H.matchReqs[msg.reqId] ~= nil
    local walletReq = H.walletReqs[msg.reqId] ~= nil
    local arenaReq = H.arenaReqs[msg.reqId] ~= nil
    local heroReq = H.heroReqs[msg.reqId] ~= nil
    local collFailed = false
    if H.coll.reqId and (answered == H.coll.reqId or (code == "UNAVAILABLE" and not packReq and not deckReq and not matchReq
        and not walletReq and not arenaReq and not heroReq)) then
        collFailed = true
        H.OnCollFailed(code .. ": " .. text)
    end
    -- the pack request this error answers (for reqId 0: the oldest request, a guess)
    local packHandled = false
    if not gone and answered ~= nil then
        local p = PK.pending
        if H.packs.reqId and answered == H.packs.reqId then
            packHandled = true
            H.OnPackGetFailed(code .. ": " .. text)
            H.PrintError("Your packs could not be loaded (" .. code .. ").")
        elseif p and p.reqId and answered == p.reqId then
            packHandled = true
            -- only an error carrying this reqId is definitive; a guessed one is retried
            H.OnPackOpenFailed(code, text, answered == msg.reqId)
        end
    end
    -- the deck request this error answers (for reqId 0 again the oldest request, a guess)
    local deckHandled = false
    if not gone and answered ~= nil then
        local op = D.op
        if H.decks.reqId and answered == H.decks.reqId then
            deckHandled = true
            H.OnDeckListFailed(code .. ": " .. text)
            H.PrintError("Your decks could not be loaded (" .. code .. ").")
        elseif op and op.reqId and answered == op.reqId then
            deckHandled = true
            -- only an error carrying this reqId is definitive; a guessed one is retried
            H.OnDeckOpFailed(code, text, kv.why, answered == msg.reqId)
        end
    end
    -- the match request this error answers (for reqId 0 again the oldest request, a guess)
    local matchHandled = false
    if not gone and answered ~= nil then
        if H.match.reqId and answered == H.match.reqId then
            matchHandled = true
            H.OnMatchGetFailed(code .. ": " .. text)
            H.PrintError("Your match could not be loaded (" .. code .. ").")
        elseif MA.pending and MA.pending.reqId and answered == MA.pending.reqId then
            matchHandled = true
            -- only an error carrying this reqId is definitive; a guessed one refetches the board
            H.OnMatchActFailed(code, text, kv.why, answered == msg.reqId)
        end
    end
    -- the wallet request this error answers (for reqId 0 again the oldest request, a guess)
    local walletHandled = false
    if not gone and answered ~= nil then
        if H.wallet.reqId and answered == H.wallet.reqId then
            walletHandled = true
            H.OnWalletGetFailed(code .. ": " .. text)
        elseif W.op and W.op.reqId and answered == W.op.reqId then
            walletHandled = true
            -- only an error carrying this reqId is definitive; a guessed one is retried
            H.OnCraftFailed(code, text, kv.why, answered == msg.reqId)
        end
    end
    -- the arena request this error answers (for reqId 0 again the oldest request, a guess)
    local arenaHandled = false
    if not gone and answered ~= nil then
        local op = AR.op
        if H.arena.reqId and answered == H.arena.reqId then
            arenaHandled = true
            H.OnArenaGetFailed(code .. ": " .. text)
            H.PrintError("Your " .. AR.NAME .. " run could not be loaded (" .. code .. ").")
        elseif op and op.reqId and answered == op.reqId then
            arenaHandled = true
            -- only an error carrying this reqId is definitive; a guessed one is retried
            H.OnArenaOpFailed(code, text, kv.why, answered == msg.reqId)
        elseif AR.pausing and AR.pausing.reqId and answered == AR.pausing.reqId then
            arenaHandled = true
            H.OnMatchPauseFailed(code, text, kv.why, answered == msg.reqId)
        end
    end
    -- the hero request this error answers (for reqId 0 again the oldest request, a guess)
    local heroHandled = false
    if not gone and answered ~= nil then
        if H.heroes.reqId and answered == H.heroes.reqId then
            heroHandled = true
            H.OnHeroGetFailed(code .. ": " .. text)
        elseif E.op and E.op.reqId and answered == E.op.reqId then
            heroHandled = true
            -- only an error carrying this reqId is definitive; a guessed one is retried
            H.OnHeroOpFailed(code, text, kv.why, answered == msg.reqId)
        end
    end
    if code == "VERSION" then
        H.PrintError("Protocol mismatch. " .. text)
    elseif code == "DISABLED" then
        H.PrintError("Hearthstone is disabled on this server.")
    elseif walletReq or (walletHandled and not collFailed) then
        -- the craft panel shows the outcome (a late error of an earlier attempt is only recorded in lastError)
    elseif heroReq or (heroHandled and not collFailed) then
        -- the Heroes tab shows the outcome (a late error of an earlier attempt is only recorded in lastError)
    elseif arenaReq or (arenaHandled and not collFailed) then
        -- the arena code above told the player what they need to know (a late error of an earlier attempt is only
        -- recorded in lastError)
    elseif packReq or deckReq or matchReq or ((packHandled or deckHandled or matchHandled) and not collFailed) then
        -- the pack / deck / match code above told the player what they need to know (a late error
        -- of an earlier attempt is only recorded in lastError)
    elseif (code == "UNAVAILABLE" and not packHandled and not deckHandled and not matchHandled and not walletHandled
        and not arenaHandled and not heroHandled)
        or collFailed then
        H.PrintError("Your card collection could not be loaded (" .. code .. "). The card catalog still works.")
    elseif code == "RATE" then
        H.PrintError("Server: too many requests, slow down.")
    else
        H.PrintError("Server error " .. code .. ": " .. text)
    end
end

-- ------------------------------------------------------------------- timers
function H.OnUpdate(elapsed)
    elapsed = elapsed or 0
    H.clock = H.clock + elapsed

    if H.helloIn then
        H.helloIn = H.helloIn - elapsed
        if H.helloIn <= 0 then
            H.helloIn = nil
            H.Hello()
        end
    end

    packRetryTick(elapsed)
    deckRetryTick(elapsed)
    craftRetryTick(elapsed)
    arenaRetryTick(elapsed)

    -- The local countdown of the Nozdormu turn timer (HsMatch.timerLeft) and its widget
    if MA and MA.timerLeft then
        MA.TimerTick(elapsed)
        if H.MatchUI and H.MatchUI.RefreshTimer then
            H.MatchUI.RefreshTimer()
        end
    end

    -- The match animations run on the frame clock, independent of the network
    if H.MatchUI and H.MatchUI.AnimTick then
        H.MatchUI.AnimTick(elapsed)
    end
    if H.MatchUI and H.MatchUI.DragTick then
        H.MatchUI.DragTick(elapsed)
    end

    H.sinceSend = H.sinceSend + elapsed
    if table.getn(H.queue) > 0 and H.sinceSend >= H.SEND_INTERVAL then
        H.sinceSend = 0
        H.Transmit(table.remove(H.queue, 1))
    end

    local t = now()
    local timedOut = {}
    if table.getn(H.queue) == 0 then
        for reqId, meta in pairs(H.pending) do
            if t - meta.sentAt >= H.REQUEST_TIMEOUT then
                table.insert(timedOut, reqId)
            end
        end
    end
    for i = 1, table.getn(timedOut) do
        local meta = H.pending[timedOut[i]]
        H.pending[timedOut[i]] = nil
        H.state.lastError = "no response to " .. meta.op
        if meta.op == "HELLO" then
            H.state.welcomed = false
            H.PrintError("The server did not answer HELLO. Is mod-hearthstone installed and enabled, and AddonChannel = 1?")
        elseif meta.op == "COLL_GET" and timedOut[i] == H.coll.reqId then
            H.OnCollFailed("no response")
            H.PrintError("The server did not send your card collection (timed out). The card catalog still works.")
        elseif meta.op == "PACK_GET" and timedOut[i] == H.packs.reqId then
            H.OnPackGetFailed("no response")
            H.PrintError("The server did not send your packs (timed out).")
        elseif meta.op == "PACK_OPEN" then
            -- the outcome is unknown: retried with the same key (H.OnPackOpenFailed)
            if PK.pending and PK.pending.reqId == timedOut[i] then
                H.OnPackOpenFailed("TIMEOUT", "no response", false)
            end
        elseif meta.op == "DECK_LIST" then
            if timedOut[i] == H.decks.reqId then
                H.OnDeckListFailed("no response")
                H.PrintError("The server did not send your decks (timed out).")
            end
        elseif meta.op == "DECK_SAVE" or meta.op == "DECK_DELETE" then
            -- the outcome is unknown: retried with the same key (H.OnDeckOpFailed)
            if D.op and D.op.reqId == timedOut[i] then
                H.OnDeckOpFailed("TIMEOUT", "no response", nil, false)
            end
        elseif meta.op == "GAME_GET" then
            if timedOut[i] == H.match.reqId then
                H.OnMatchGetFailed("no response")
                H.PrintError("The server did not send your match (timed out).")
            end
        elseif meta.op == "GAME_ACT" then
            -- the outcome is unknown: the action is not sent again, the board is refetched
            if MA.pending and MA.pending.reqId == timedOut[i] then
                H.OnMatchActFailed("TIMEOUT", "no response", nil, false)
            end
        elseif meta.op == "WALLET_GET" then
            if timedOut[i] == H.wallet.reqId then
                H.OnWalletGetFailed("no response")
                H.PrintError("The server did not send your coins and dust (timed out).")
            end
        elseif meta.op == "CRAFT_MAKE" or meta.op == "CRAFT_SCRAP" or meta.op == "CRAFT_EXTRAS" then
            -- the outcome is unknown: retried with the same key (H.OnCraftFailed)
            if W.op and W.op.reqId == timedOut[i] then
                H.OnCraftFailed("TIMEOUT", "no response", nil, false)
            end
        elseif meta.op == "HERO_GET" then
            if timedOut[i] == H.heroes.reqId then
                H.OnHeroGetFailed("no response")
                H.PrintError("The server did not send your hero portraits (timed out).")
            end
        elseif meta.op == "HERO_SELECT" or meta.op == "HERO_UNLOCK" then
            -- the outcome is unknown: sent again with the same key (H.OnHeroOpFailed)
            if E.op and E.op.reqId == timedOut[i] then
                H.OnHeroOpFailed("TIMEOUT", "no response", nil, false)
            end
        elseif meta.op == "ARENA_GET" then
            if timedOut[i] == H.arena.reqId then
                H.OnArenaGetFailed("no response")
                H.PrintError("The server did not send your " .. AR.NAME .. " run (timed out).")
            end
        elseif meta.op == "ARENA_CLASS" or meta.op == "ARENA_PICK" or meta.op == "ARENA_CLAIM" then
            -- the outcome is unknown: retried with the same key (H.OnArenaOpFailed)
            if AR.op and AR.op.reqId == timedOut[i] then
                H.OnArenaOpFailed("TIMEOUT", "no response", nil, false)
            end
        elseif meta.op == "GAME_PAUSE" then
            -- the outcome is unknown: the board is refetched (H.OnMatchPauseFailed)
            if AR.pausing and AR.pausing.reqId == timedOut[i] then
                H.OnMatchPauseFailed("TIMEOUT", "no response", nil, false)
            end
        else
            H.PrintError("No response to " .. meta.op .. " (timed out).")
        end
    end

    local expired = H.reasm:Sweep(t)
    if table.getn(expired) > 0 then
        H.state.lastError = "reply timed out before all parts arrived"
    end
end

-- ------------------------------------------------------------ saved variables
-- HearthstoneDB (UI settings, last class tab). Initialised on ADDON_LOADED and again on
-- VARIABLES_LOADED: whichever comes after the client filled the global wins, and the defaults
-- are re-applied to the loaded table.
function H.InitSaved()
    HearthstoneDB = HsCollection.ApplySavedDefaults(HearthstoneDB)
    H.db = HearthstoneDB
    if H.db.drag == nil then H.db.drag = true end
    if HsMatchAnim then
        HsMatchAnim.SetMode(H.db.anim)        -- /hs anim on|off|fast (default on)
    end
    if H.OnSavedLoaded then
        H.OnSavedLoaded()
    end
end

-- ------------------------------------------------------------------- events
function H.OnEvent(event, a1, a2, a3, a4)
    if event == "ADDON_LOADED" then
        if a1 == "Hearthstone" then
            H.InitSaved()
        end
    elseif event == "VARIABLES_LOADED" then
        H.InitSaved()
    elseif event == "PLAYER_ENTERING_WORLD" then
        -- fires on every loading screen; the handshake is once per UI load (login or /reload)
        if not H.state.helloSent and not H.helloIn then
            H.helloIn = H.HELLO_DELAY
        end
    elseif event == "CHAT_MSG_ADDON" then
        H.OnAddonMessage(a1, a2, a3, a4)
    elseif event == "BAG_UPDATE" then
        H.OnBagUpdate()
        if H.MerchantCoins and H.MerchantCoins.Update then
            H.MerchantCoins.Update()  -- the coin price in an open vendor window (works without a wallet too)
        end
    end
end

-- ------------------------------------------------------------------- slash
function H.PrintStatus()
    local s = H.state
    H.Print("addon " .. H.VERSION .. ", protocol v" .. H.PROTOCOL)
    if s.welcomed and s.welcome then
        local w = s.welcome
        H.Print("connected: server " .. (w.server or "?") .. ", dataset " .. (w.dataset or "-") .. " ("
            .. (w.hash or "-") .. "), caps " .. (w.caps or "-") .. ", maxframe " .. (w.maxframe or "?"))
    elseif s.helloSent then
        H.Print("not connected")
    else
        H.Print("not connected (handshake not sent yet)")
    end
    if type(HS_CardDB) == "table" then
        H.Print("card data " .. tostring(HS_CardDB.datasetId) .. " (" .. tostring(HS_CardDB.hash) .. "), "
            .. tostring(HS_CardDB.count) .. " cards, check: " .. (s.datasetStatus or "pending"))
    else
        H.Print("card data: CardDB.lua not loaded")
    end
    local coll = O.StatusText()
    if H.coll.refetch then
        coll = coll .. ", refetch pending"
    end
    H.Print(coll)
    local packs = PK.StatusText()
    if H.packs.refetch then
        packs = packs .. ", refetch pending"
    end
    H.Print(packs)
    local decks = D.StatusText()
    if H.decks.refetch then
        decks = decks .. ", refetch pending"
    end
    H.Print(decks)
    local match = MA.StatusText()
    if H.match.refetch then
        match = match .. ", refetch pending"
    end
    H.Print(match)
    H.Print(W.StatusText())
    H.Print(AR.StatusText())
    H.Print(E.StatusText())
    if s.lastError then
        H.Print("last error: " .. s.lastError)
    end
    H.Print("queued frames: " .. table.getn(H.queue))
end

function H.Slash(msg)
    msg = msg or ""
    local _, _, cmd, rest = string.find(msg, "^%s*(%S*)%s*(.-)%s*$")
    cmd = string.lower(cmd or "")
    if cmd == "" or cmd == "status" then
        H.PrintStatus()
    elseif cmd == "collection" or cmd == "c" then
        -- collection window, implemented in CollectionUI.lua (works offline)
        if H.ToggleCollection then
            H.ToggleCollection()
        else
            H.PrintError("CollectionUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "packs" or cmd == "open" then
        -- Packs tab of the collection window (PacksUI.lua); "open" also opens one pack
        if H.ShowPacks then
            H.ShowPacks()
        end
        if cmd == "open" then
            local key, why
            if H.PacksUI and H.PacksUI.OpenNow then
                key, why = H.PacksUI.OpenNow()
            else
                key, why = H.PackOpen()
            end
            if not key then
                H.PrintError(why)
            end
        elseif not H.ShowPacks then
            H.PrintError("PacksUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "decks" or cmd == "d" then
        -- Decks tab of the collection window (DecksUI.lua)
        if H.ShowDecks then
            H.ShowDecks()
        else
            H.PrintError("DecksUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "center" then
        -- forget the saved window position, centre the collection window (CollectionUI.lua)
        if H.CollectionUI and H.CollectionUI.CenterWindow then
            H.Print(H.CollectionUI.CenterWindow())
        else
            H.PrintError("CollectionUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "bookart" then
        -- parchment pages of the collection book on / off (CollectionUI.lua; off if a texture shows green)
        if H.CollectionUI and H.CollectionUI.SetBookArt then
            H.Print(H.CollectionUI.SetBookArt(rest))
        else
            H.PrintError("CollectionUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "drag" then
        H.Print(H.MatchUI.DragCommand(rest))
    elseif cmd == "anim" then
        -- match animations on / off / fast, or skip the running ones (MatchUI.lua)
        if H.MatchUI and H.MatchUI.AnimCommand then
            H.Print(H.MatchUI.AnimCommand(rest))
        else
            H.PrintError("MatchUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "fx" then
        -- native spell effect bench and settings (MatchFx.lua)
        if H.MatchFx and H.MatchFx.Command then
            H.MatchFx.Command(rest)
        else
            H.PrintError("MatchFx.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "game" or cmd == "match" then
        -- reopens the match window (MatchUI.lua) and asks the server for the board again
        if not H.state.welcomed then
            H.PrintError("Not connected to the server.")
        elseif not H.match.supported then
            H.PrintError("This server does not support matches yet.")
        else
            if H.ShowMatch then
                H.ShowMatch()
            end
            H.match.announce = true
            H.MatchGet()
        end
    elseif cmd == "arena" then
        -- the Night Watch Arena window (ArenaUI.lua) toggles; the run is fetched again when it opens
        if not H.state.welcomed then
            H.PrintError("Not connected to the server.")
        elseif not H.arena.supported then
            H.PrintError("This server does not support the " .. AR.NAME .. " yet.")
        elseif H.ArenaUI and H.ArenaUI.IsShown and H.ArenaUI.IsShown() then
            H.ArenaUI.Hide()
        elseif H.ShowArena then
            H.ShowArena()
            H.ArenaGet()
        else
            H.PrintError("ArenaUI.lua is not loaded. Reinstall the Hearthstone addon.")
        end
    elseif cmd == "heroes" then
        -- the Heroes tab of the collection window (HeroesUI.lua); without it the state is printed
        if not H.state.welcomed then
            H.PrintError("Not connected to the server.")
        elseif not H.heroes.supported then
            H.PrintError("This server does not have alternative hero portraits yet.")
        elseif H.ShowHeroes then
            H.ShowHeroes()
            H.HeroGet()
        else
            H.HeroGet()
            H.Print(E.StatusText())
        end
    else
        H.Print("commands: /hs collection (or /hs c) | decks | packs | open | game | arena | heroes | drag on|off | anim on|off|fast|skip | fx [help] | bookart [on|off] | center | status")
    end
end

-- ----------------------------------------------------------- WoW plumbing
if CreateFrame then
    local frame = CreateFrame("Frame", "HearthstoneEventFrame")
    frame:RegisterEvent("ADDON_LOADED")
    frame:RegisterEvent("VARIABLES_LOADED")
    frame:RegisterEvent("PLAYER_ENTERING_WORLD")
    frame:RegisterEvent("CHAT_MSG_ADDON")
    frame:RegisterEvent("BAG_UPDATE")
    -- 1.12 passes event data in the globals this/event/arg1..arg4; later clients
    -- pass them as parameters. Accept both.
    frame:SetScript("OnEvent", function(s, e, p1, p2, p3, p4)
        if event then
            H.OnEvent(event, arg1, arg2, arg3, arg4)
        else
            H.OnEvent(e, p1, p2, p3, p4)
        end
    end)
    frame:SetScript("OnUpdate", function(s, dt)
        H.OnUpdate(dt or arg1)
    end)

    SLASH_HEARTHSTONE1 = "/hs"
    SlashCmdList["HEARTHSTONE"] = function(msg)
        H.Slash(msg)
    end
end
