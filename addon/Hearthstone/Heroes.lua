-- Hearthstone addon: alternative hero portraits (reward track).
--
-- Pure logic, no WoW frames and no network: the account's portrait state, the parsers of HEROES, the catalog helpers
-- over HeroPortraits.lua (generated), which portrait to draw for a class, the client-side checks that keep a locked
-- click from sending anything, and the one HERO_SELECT / HERO_UNLOCK in flight. Hearthstone.lua sends the requests and
-- feeds the replies in; HeroesUI.lua and the portrait call sites only read it. The server decides everything.
--
--   E.state     "none" (not connected), "unsupported" (no "heroes" in WELCOME caps), "loading", "ready" or "error"
--   E.credits   unspent Legend #1 credits
--   E.own       { [portraitId] = true }: the unlocked alternatives (the class defaults are always usable)
--   E.sel       { [CLASS] = portraitId }: the classes whose choice is not the default
--   E.op        the select / unlock that is not confirmed yet, or nil:
--               { kind ("select" or "unlock"), class, id, key (unlock), reqId, attempts, inFlight }
--   E.message   the last result line of the Heroes tab, or nil
--
-- Cosmetic only: the portrait art changes, never the hero's name, hero power or rules. Texture files of the
-- alternatives are listed in HS_HeroTextures (generated into CardTextures.lua); a portrait without its texture falls
-- back to the class default.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsHeroes = HsHeroes or {}
local E = HsHeroes

E.MAX_ATTEMPTS = 3            -- sends of one unlock (same key) before the addon gives up and resyncs
E.MAX_CREDITS = 99999
E.TEXTURE_DIR = "Interface\\Hearthstone\\Textures\\Heroes\\"
E.CLASSES = { "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR" }

-- REFUSED why=<code> -> the tab's line.
E.WHY_TEXT = {
    class = "That portrait does not belong to this class.",
    unknown = "That portrait does not exist.",
    locked = "You have not unlocked that portrait yet.",
    nocredit = "Reach Legend #1 to unlock a portrait.",
    owned = "You already unlocked that portrait.",
}
E.LOCKED_TEXT = "Reach Legend #1 to unlock a portrait."

-- ---------------------------------------------------------------- catalog
local function catalog()
    return HearthstoneHeroPortraits
end

-- The class default portrait id ("HERO_08" for MAGE), or nil.
function E.Default(class)
    local list = catalog() and catalog()[class]
    return list and list[1] and list[1].id or nil
end

-- Finds a portrait in the catalog: returns class, entry (or nil).
function E.Find(id)
    local cat = catalog()
    if type(cat) ~= "table" or type(id) ~= "string" then
        return nil
    end
    for ci = 1, table.getn(E.CLASSES) do
        local class = E.CLASSES[ci]
        local list = cat[class]
        if list then
            for i = 1, table.getn(list) do
                if list[i].id == id then
                    return class, list[i]
                end
            end
        end
    end
    return nil
end

-- The class whose default portrait is this card id, or nil ("HERO_08" -> "MAGE"; Jaraxxus and bosses -> nil).
function E.ClassOfDefault(cardId)
    local class, entry = E.Find(cardId)
    if class and entry.default then
        return class
    end
    return nil
end

-- The portraits of a class, the default first: { { id, name, default, owned, installed }, ... }.
function E.List(class)
    local list = catalog() and catalog()[class]
    local out = {}
    if not list then
        return out
    end
    for i = 1, table.getn(list) do
        local p = list[i]
        out[i] = { id = p.id, name = p.name, default = p.default, owned = p.default or E.own[p.id] == true,
            installed = E.Installed(p.id) }
    end
    return out
end

-- ---------------------------------------------------------------- textures
local function textureSet()
    local t = HS_HeroTextures
    return type(t) == "table" and t.set or nil
end

-- True when the portrait can be drawn: a class default (Cards\HERO_0x, always installed) or an alternative whose
-- texture is listed in HS_HeroTextures.
function E.Installed(id)
    local class, entry = E.Find(id)
    if not class then
        return false
    end
    if entry.default then
        return true
    end
    local set = textureSet()
    return set ~= nil and set[id] ~= nil
end

-- Path and bottom texcoord of an installed alternative (SetTexCoord(0, 1, 0, bottom)), or nil for a default or a
-- missing texture (the defaults go through the normal card art).
function E.Texture(id)
    local class, entry = E.Find(id)
    local set = textureSet()
    if not class or entry.default or not set or set[id] == nil then
        return nil
    end
    return E.TEXTURE_DIR .. id, set[id]
end

-- ----------------------------------------------------------------- choice
-- The portrait id to draw for a class: the selected one when it is owned, belongs to the class and its texture is
-- installed, else the class default (nil for an unknown class).
function E.PortraitFor(class)
    local default = E.Default(class)
    if not default then
        return nil
    end
    local id = E.state == "ready" and E.sel[class] or nil
    if id and id ~= default and E.own[id] and E.Installed(id) then
        local c = E.Find(id)
        if c == class then
            return id
        end
    end
    return default
end

-- The card id to draw for a hero card. Substitutes only for the player's own hero (mine) and only when cardId is a
-- class default: Jaraxxus, Naxxramas bosses and every opponent keep their own art.
function E.ArtFor(cardId, mine)
    if not mine then
        return cardId
    end
    local class = E.ClassOfDefault(cardId)
    if not class then
        return cardId
    end
    return E.PortraitFor(class)
end

-- Is the Heroes tab visible: at least one unlock or an unspent credit.
function E.TabVisible()
    if E.state ~= "ready" then
        return false
    end
    if E.credits > 0 then
        return true
    end
    for _ in pairs(E.own) do
        return true
    end
    return false
end

-- Why a portrait cannot be selected now (a short line), or nil. Defaults are always allowed.
function E.SelectBlocker(class, id)
    if E.state ~= "ready" then
        return "The portraits are not loaded."
    end
    local c, entry = E.Find(id)
    if not c then
        return E.WHY_TEXT.unknown
    end
    if c ~= class then
        return E.WHY_TEXT.class
    end
    if not entry.default and not E.own[id] then
        return E.LOCKED_TEXT
    end
    return nil
end

-- Why a portrait cannot be unlocked now, or nil.
function E.UnlockBlocker(id)
    if E.state ~= "ready" then
        return "The portraits are not loaded."
    end
    local c, entry = E.Find(id)
    if not c or entry.default then
        return E.WHY_TEXT.unknown
    end
    if E.own[id] then
        return E.WHY_TEXT.owned
    end
    if E.credits < 1 then
        return E.LOCKED_TEXT
    end
    return nil
end

-- ---------------------------------------------------------------- parsing
local function parseCredits(s)
    if type(s) ~= "string" or not string.find(s, "^%d+$") or string.len(s) > 5 then
        return nil
    end
    if string.len(s) > 1 and string.sub(s, 1, 1) == "0" then
        return nil
    end
    return tonumber(s)
end

local function validId(s)
    return type(s) == "string" and string.len(s) >= 1 and string.len(s) <= 32 and string.find(s, "^[%w_]+$") ~= nil
end

local function splitCommas(s)
    local out = {}
    if s == "" then
        return out
    end
    local pos = 1
    while true do
        local comma = string.find(s, ",", pos, true)
        out[table.getn(out) + 1] = comma and string.sub(s, pos, comma - 1) or string.sub(s, pos)
        if not comma then
            return out
        end
        pos = comma + 1
    end
end

-- HEROES: credits=<n>;own=<id,...>;sel=<CLASS>.<id>,...
function E.Parse(payload)
    local kv = type(payload) == "string" and HsProtocol.KvDecode(payload)
    if not kv then
        return nil, "malformed payload"
    end
    local credits = parseCredits(kv.credits)
    if credits == nil then
        return nil, "bad credits"
    end
    if type(kv.own) ~= "string" or type(kv.sel) ~= "string" then
        return nil, "missing lists"
    end
    local own, sel = {}, {}
    local ids = splitCommas(kv.own)
    for i = 1, table.getn(ids) do
        if not validId(ids[i]) then
            return nil, "bad portrait id"
        end
        own[ids[i]] = true
    end
    local pairsList = splitCommas(kv.sel)
    for i = 1, table.getn(pairsList) do
        local _, _, class, id = string.find(pairsList[i], "^(%u+)%.([%w_]+)$")
        if not class or not validId(id) then
            return nil, "bad choice"
        end
        sel[class] = id
    end
    return { credits = credits, own = own, sel = sel }
end

-- HERO_CHANGED push: why=legend1|gm. Returns the reason ("" when unreadable; the addon refetches either way).
function E.ParseChanged(payload)
    local kv = type(payload) == "string" and HsProtocol.KvDecode(payload)
    return kv and kv.why or ""
end

-- True when the WELCOME caps list contains "heroes".
function E.CapsHaveHeroes(caps)
    if type(caps) ~= "string" then
        return false
    end
    local items = splitCommas(caps)
    for i = 1, table.getn(items) do
        if items[i] == "heroes" then
            return true
        end
    end
    return false
end

-- ------------------------------------------------------------------ state
function E.Reset()
    E.state = "none"
    E.errorText = nil
    E.credits = 0
    E.own = {}
    E.sel = {}
    E.op = nil
    E.message = nil
end

function E.SetUnsupported()
    E.Reset()
    E.state = "unsupported"
end

function E.SetLoading()
    if E.state ~= "ready" then
        E.state = "loading"
        E.errorText = nil
    end
end

function E.SetReady(res)
    E.state = "ready"
    E.errorText = nil
    E.credits, E.own, E.sel = res.credits, res.own, res.sel
end

function E.SetError(text)
    E.state = "error"
    E.errorText = text
end

function E.IsReady()
    return E.state == "ready"
end

-- ------------------------------------------------------------------- ops
-- Starts a select or unlock. An unlock gets a fresh idempotency key (HsPacks.NewKey rules).
function E.BeginOp(kind, class, id)
    E.op = { kind = kind, class = class, id = id, reqId = nil, attempts = 0, inFlight = true }
    if kind == "unlock" then
        E.op.key = HsPacks.NewKey()
    end
    E.message = nil
    return E.op
end

function E.ClearOp()
    E.op = nil
end

function E.IsBusy()
    return E.op ~= nil and E.op.inFlight
end

-- HERO_SELECT / HERO_UNLOCK payload of the op.
function E.OpPayload(op)
    if op.kind == "unlock" then
        return HsProtocol.KvEncode({ { "key", op.key }, { "id", op.id } })
    end
    local id = op.id
    if id == E.Default(op.class) then
        id = "default"
    end
    return HsProtocol.KvEncode({ { "class", op.class }, { "id", id } })
end

E.OPS = { select = "HERO_SELECT", unlock = "HERO_UNLOCK" }

-- One line for /hs status.
function E.StatusText()
    local s = E.state
    if s == "ready" then
        local n = 0
        for _ in pairs(E.own) do
            n = n + 1
        end
        return "heroes: " .. n .. " portrait" .. (n == 1 and "" or "s") .. " unlocked, " .. E.credits
            .. " credit" .. (E.credits == 1 and "" or "s")
    elseif s == "loading" then
        return "heroes: loading"
    elseif s == "error" then
        return "heroes: error (" .. tostring(E.errorText or "?") .. ")"
    elseif s == "unsupported" then
        return "heroes: not supported by this server"
    end
    return "heroes: none (not connected)"
end

if E.state == nil then
    E.Reset()
end
