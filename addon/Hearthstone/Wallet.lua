-- Hearthstone addon: Hearthstone coins, crafting dust and crafting (economy).
--
-- Pure logic, no WoW frames and no network: the wallet state, the parsers of WALLET / WALLET_UPD /
-- CRAFT_ACCESS / CRAFTED, the dust values of a card, the client-side checks that grey out the craft and scrap
-- buttons, the deck warning for a scrap and the one craft / scrap in flight. Hearthstone.lua sends the
-- requests and feeds the replies in; CollectionUI.lua and CraftUI.lua only read it. The server decides
-- everything: these checks only spare the player a request the server would refuse anyway.
--
--   W.state      "none" (not connected), "unsupported" (no "wallet" in WELCOME caps), "loading", "ready" or "error"
--   W.coins      Hearthstone Coin items in the character's bags (W.COIN_ITEM; earned in matches, a Hearthstone
--                Master's vendor window sells booster packs for them). The server sends the count, the addon
--                recounts the bags on BAG_UPDATE (W.CountBags).
--   W.dust       crafting dust (account-wide; from scrapping, crafts cards at a crafting NPC)
--   W.values     { scrap = { c, r, e, l }, make = { c, r, e, l } } dust per rarity (Basic uses Common)
--   W.packCoins  coins one booster pack costs at a vendor
--   W.craftOpen  a crafting NPC is open (CRAFT_ACCESS / WALLET craft=1), W.craftNpc its name
--   W.op         the craft / scrap that is not confirmed yet, or nil:
--                { kind ("craft", "scrap" or "extras": every extra copy of the collection), key, card, n, reqId, reqIds,
--                  attempts, retryIn, inFlight }
--   W.message    the last result line of the craft panel, or nil
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsWallet = HsWallet or {}
local W = HsWallet

W.MAX_BALANCE = 1000000000
W.COIN_ITEM = 919601          -- Hearthstone Coin (world migration 0006)
W.MAX_ATTEMPTS = 3            -- sends of one craft / scrap (same key) before the addon gives up and resyncs
W.RARITY_BUCKET = { FREE = 1, COMMON = 1, RARE = 2, EPIC = 3, LEGENDARY = 4 }
W.RARITY_LABEL = { "Common", "Rare", "Epic", "Legendary" }
W.DEFAULT_VALUES = { scrap = { 5, 20, 100, 400 }, make = { 40, 100, 400, 1600 } }

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

-- "5.20.100.400" -> { 5, 20, 100, 400 } (each 1..1000000), or nil.
local function parseValues(s)
    if type(s) ~= "string" then
        return nil
    end
    local _, _, a, b, c, d = string.find(s, "^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
    local out = { parseNumber(a, 7), parseNumber(b, 7), parseNumber(c, 7), parseNumber(d, 7) }
    for i = 1, 4 do
        if out[i] == nil or out[i] < 1 or out[i] > 1000000 then
            return nil
        end
    end
    return out
end

local function parseBalance(s)
    local n = parseNumber(s, 10)
    if n == nil or n > W.MAX_BALANCE then
        return nil
    end
    return n
end

-- WALLET: coins=;dust=;craft=0|1[;npc=];scrap=c.r.e.l;make=c.r.e.l;packcoins=
function W.Parse(payload)
    local kv = type(payload) == "string" and HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local res = {}
    res.coins = parseBalance(kv.coins)
    res.dust = parseBalance(kv.dust)
    if res.coins == nil or res.dust == nil then
        return nil, "bad balance"
    end
    if kv.craft ~= "0" and kv.craft ~= "1" then
        return nil, "bad craft"
    end
    res.craftOpen = kv.craft == "1"
    res.craftNpc = res.craftOpen and kv.npc or nil
    local scrap, make = parseValues(kv.scrap), parseValues(kv.make)
    if not scrap or not make then
        return nil, "bad dust values"
    end
    res.values = { scrap = scrap, make = make }
    res.packCoins = parseBalance(kv.packcoins)
    if res.packCoins == nil then
        return nil, "bad price"
    end
    return res
end

-- WALLET_UPD push: coins=;dust=;why=[;gain=]
function W.ParseUpd(payload)
    local kv = type(payload) == "string" and HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local coins, dust = parseBalance(kv.coins), parseBalance(kv.dust)
    if coins == nil or dust == nil then
        return nil, "bad balance"
    end
    local gain = 0
    if kv.gain ~= nil then
        gain = parseBalance(kv.gain)
        if gain == nil then
            return nil, "bad gain"
        end
    end
    return { coins = coins, dust = dust, why = kv.why or "", gain = gain }
end

-- CRAFT_ACCESS push: open=0|1[;npc=]. Returns open (true / false) and the name, or nil.
function W.ParseAccess(payload)
    local kv = type(payload) == "string" and HsProtocol.KvDecode(payload)
    if not kv or (kv.open ~= "0" and kv.open ~= "1") then
        return nil
    end
    return kv.open == "1", kv.npc
end

-- CRAFTED: key=;replay=0|1;kind=craft|scrap|extras;card=;n=;delta=;dust=;ver=;total=;prot=
-- (kind=extras: card=all, n = the copies scrapped over all cards, total=0, prot=0)
function W.ParseCrafted(payload)
    local kv = type(payload) == "string" and HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    if type(kv.key) ~= "string" or not string.find(kv.key, "^[0-9a-z]+$") or string.len(kv.key) < 12
        or string.len(kv.key) > 16 then
        return nil, "bad key"
    end
    if kv.kind ~= "craft" and kv.kind ~= "scrap" and kv.kind ~= "extras" then
        return nil, "bad kind"
    end
    if type(kv.card) ~= "string" or not string.find(kv.card, "^[%w_]+$") or string.len(kv.card) > 32 then
        return nil, "bad card"
    end
    local res = { key = kv.key, replay = kv.replay == "1", kind = kv.kind, card = kv.card }
    res.n = parseNumber(kv.n, kv.kind == "extras" and 5 or 2)     -- an extras op scraps the copies of every card
    res.dust = parseBalance(kv.dust)
    res.ver = parseNumber(kv.ver, 10)
    res.total = parseNumber(kv.total, 3)
    res.prot = parseNumber(kv.prot, 3)
    local delta = kv.delta
    if type(delta) == "string" and string.sub(delta, 1, 1) == "-" then
        res.delta = parseNumber(string.sub(delta, 2), 9)
        if res.delta then
            res.delta = -res.delta
        end
    else
        res.delta = parseNumber(delta, 9)
    end
    if res.n == nil or res.dust == nil or res.ver == nil or res.total == nil or res.prot == nil or res.delta == nil
        or res.prot > res.total then
        return nil, "bad numbers"
    end
    return res
end

-- True when the WELCOME caps list contains "wallet".
function W.CapsHaveWallet(caps)
    if type(caps) ~= "string" then
        return false
    end
    local pos = 1
    while true do
        local comma = string.find(caps, ",", pos, true)
        local item = comma and string.sub(caps, pos, comma - 1) or string.sub(caps, pos)
        if item == "wallet" then
            return true
        end
        if not comma then
            return false
        end
        pos = comma + 1
    end
end

-- ------------------------------------------------------------------ state
function W.Reset()
    W.state = "none"
    W.errorText = nil
    W.coins, W.dust = 0, 0
    W.values = { scrap = { 5, 20, 100, 400 }, make = { 40, 100, 400, 1600 } }
    W.packCoins = 0
    W.craftOpen, W.craftNpc = false, nil
    W.op = nil
    W.message = nil
end

function W.SetUnsupported()
    W.Reset()
    W.state = "unsupported"
end

function W.SetLoading()
    if W.state ~= "ready" then
        W.state = "loading"
        W.errorText = nil
    end
end

function W.SetReady(res)
    W.state = "ready"
    W.errorText = nil
    W.coins, W.dust = res.coins, res.dust
    W.values = res.values
    W.packCoins = res.packCoins
    W.craftOpen, W.craftNpc = res.craftOpen, res.craftNpc
end

function W.SetError(text)
    W.state = "error"
    W.errorText = text
end

function W.IsReady()
    return W.state == "ready"
end

-- Hearthstone Coin items in the bags (backpack and the four bags), or nil without the container API. link and
-- info are GetContainerItemLink / GetContainerItemInfo (passed in so the tests can fake them).
function W.CountBags(numSlots, link, info)
    if type(numSlots) ~= "function" or type(link) ~= "function" or type(info) ~= "function" then
        return nil
    end
    local key = "item:" .. W.COIN_ITEM .. ":"
    local n = 0
    for bag = 0, 4 do
        local slots = numSlots(bag) or 0
        for slot = 1, slots do
            local l = link(bag, slot)
            if l and string.find(l, key, 1, true) then
                local _, count = info(bag, slot)
                n = n + (count or 1)
            end
        end
    end
    return n
end

-- A WALLET_UPD push or a CRAFTED reply moved the balances.
function W.ApplyBalances(coins, dust)
    if coins ~= nil then
        W.coins = coins
    end
    if dust ~= nil then
        W.dust = dust
    end
end

function W.SetAccess(open, npc)
    W.craftOpen = open and true or false
    if W.craftOpen then
        W.craftNpc = npc
    else
        W.craftNpc = nil
    end
end

-- ------------------------------------------------------------ card values
-- 1 (Common, also Basic) .. 4 (Legendary), or nil for a card without a rarity.
function W.Bucket(card)
    return card and W.RARITY_BUCKET[card.rarity]
end

-- Can the card be crafted and scrapped at all: collectible, acquisition standard, a rarity (the server's rule).
-- Promo (reward_only) and Naxxramas (quest) cards never can.
function W.Craftable(card)
    return card ~= nil and card.coll == 1 and card.acq == "standard" and W.Bucket(card) ~= nil
end

function W.CraftCost(card)
    local b = W.Bucket(card)
    return b and W.values.make[b]
end

function W.ScrapValue(card)
    local b = W.Bucket(card)
    return b and W.values.scrap[b]
end

-- The copies crafting fills: 2, or 1 for a Legendary.
function W.Limit(card)
    if card and card.rarity == "LEGENDARY" then
        return 1
    end
    return 2
end

-- Why the card cannot be crafted now (a short line for the panel), or nil. owned = { total, prot } or nil.
function W.CraftBlocker(card, owned)
    if not W.Craftable(card) then
        return "This card cannot be crafted."
    end
    local total = owned and owned.total or 0
    if total >= W.Limit(card) then
        return "You already own every copy a deck can use."
    end
    if W.dust < W.CraftCost(card) then
        return "Not enough dust (" .. W.CraftCost(card) .. " needed, you have " .. W.dust .. ")."
    end
    return nil
end

-- Why one copy cannot be scrapped now, or nil.
function W.ScrapBlocker(card, owned)
    if not W.Craftable(card) then
        return "This card cannot be scrapped."
    end
    local total = owned and owned.total or 0
    local prot = owned and owned.prot or 0
    if total == 0 then
        return "You do not own this card."
    end
    if total - prot < 1 then
        return "Your copies are protected (starter and quest cards cannot be scrapped)."
    end
    return nil
end

-- The copies of a card that "Disenchant Extra Cards" scraps: the copies above what a deck can use (the limit: 2, or 1
-- for a Legendary) that are not protected (starter and quest copies never are). owned = { total, prot } or nil.
function W.ExtraCopies(card, owned)
    if not owned or not W.Craftable(card) then
        return 0
    end
    local over = owned.total - W.Limit(card)
    local free = owned.total - owned.prot
    if over < 1 or free < 1 then
        return 0
    end
    if over < free then
        return over
    end
    return free
end

-- What "Disenchant Extra Cards" would scrap: copies, the dust they give, the number of different cards and the same
-- by rarity ({ [bucket 1..4] = { copies, dust } }, only the rarities that have extra copies).
-- owned is HsOwnership.owned (id -> { total, prot }), cards HS_CardDB.cards.
function W.Extras(owned, cards)
    local copies, dust, kinds, per = 0, 0, 0, {}
    if type(owned) ~= "table" or type(cards) ~= "table" then
        return copies, dust, kinds, per
    end
    for id, entry in pairs(owned) do
        local card = cards[id]
        local n = W.ExtraCopies(card, entry)
        if n > 0 then
            local gain = n * W.ScrapValue(card)
            copies = copies + n
            kinds = kinds + 1
            dust = dust + gain
            local b = W.Bucket(card)
            per[b] = per[b] or { 0, 0 }
            per[b][1] = per[b][1] + n
            per[b][2] = per[b][2] + gain
        end
    end
    return copies, dust, kinds, per
end

-- Why "Disenchant Extra Cards" cannot be sent now (a short line), or nil. copies: W.Extras' first result.
function W.ExtrasBlocker(copies)
    if copies < 1 then
        return W.WHY_TEXT.nothing
    end
    return nil
end

-- The saved decks that need more copies of id than remain after scrapping (the scrap warning): a list of names.
-- decks is HsDecks.decks ({ name, cards = { [id] = qty } }).
function W.DeckWarnings(id, remaining, decks)
    local out = {}
    if type(decks) ~= "table" then
        return out
    end
    for i = 1, table.getn(decks) do
        local d = decks[i]
        local need = d.cards and d.cards[id] or 0
        if need > remaining then
            table.insert(out, d.name or ("deck " .. tostring(d.id)))
        end
    end
    return out
end

-- ------------------------------------------------------------- craft ops
-- Starts a craft or scrap with a fresh idempotency key (HsPacks.NewKey rules). Returns the op.
function W.BeginOp(kind, card, n)
    W.op = { kind = kind, key = HsPacks.NewKey(), card = card, n = n or 1, reqIds = {}, attempts = 0, inFlight = true }
    W.message = nil
    return W.op
end

function W.ClearOp()
    W.op = nil
end

function W.IsBusy()
    return W.op ~= nil and W.op.inFlight
end

-- CRAFT_MAKE / CRAFT_SCRAP payload of the op.
function W.OpPayload(op)
    if op.kind == "extras" then
        return HsProtocol.KvEncode({ { "key", op.key } })
    end
    local kv = { { "key", op.key }, { "card", op.card } }
    if op.kind == "scrap" then
        table.insert(kv, { "n", tostring(op.n) })
    end
    return HsProtocol.KvEncode(kv)
end

-- REFUSED why=<code> -> the panel's line.
W.WHY_TEXT = {
    npc = "Talk to a Hearthstone Master to craft and scrap cards.",
    dust = "You do not have enough dust.",
    limit = "You already own every copy a deck can use.",
    copies = "You have no copy of this card that can be scrapped.",
    card = "This card cannot be crafted or scrapped.",
    count = "You can scrap 1 to 99 copies at a time.",
    nothing = "You have no extra cards to disenchant.",
}

-- One line for /hs status.
function W.StatusText()
    local s = W.state
    if s == "ready" then
        local text = "wallet: " .. W.coins .. " Hearthstone Coins in your bags, " .. W.dust .. " dust"
        if W.craftOpen then
            text = text .. ", crafting open at " .. tostring(W.craftNpc or "?")
        end
        return text
    elseif s == "loading" then
        return "wallet: loading"
    elseif s == "error" then
        return "wallet: error (" .. tostring(W.errorText or "?") .. ")"
    elseif s == "unsupported" then
        return "wallet: not supported by this server"
    end
    return "wallet: none (not connected)"
end

if W.state == nil then
    W.Reset()
end
