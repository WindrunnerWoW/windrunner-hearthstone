-- Hearthstone addon: collection browser model.
--
-- Pure logic, no WoW frames: catalog building, sorting, filtering (including the ownership
-- filter, which gets the owned-card map from Ownership.lua), searching, paging, saved
-- variable defaults and the text used by the card fallback. The UI files
-- (CardWidget.lua, CollectionUI.lua, MicroButton.lua) only call these functions, and
-- tests/lua_tests.lua tests them without a client.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no string.match/gmatch, no '%', no '...'.

HsCollection = HsCollection or {}
local CM = HsCollection

CM.PER_PAGE = 8              -- 4 columns x 2 rows
CM.MANA_MAX = 7              -- the "7+" button: every card costing 7 or more
CM.SAVED_VERSION = 1

-- Class tab strip (top to bottom). Class icons come from the character creation atlas
-- (256x256, 4x4 icons); the coordinates are the client's own CLASS_ICON_TCOORDS.
CM.ICON_ATLAS = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes"
CM.CLASS_TABS = {
    { key = "ALL", label = "All Cards", icon = "Interface\\Icons\\INV_Misc_Book_09" },
    { key = "DRUID", label = "Druid", coords = { 0.7421875, 0.98828125, 0, 0.25 } },
    { key = "HUNTER", label = "Hunter", coords = { 0, 0.25, 0.25, 0.5 } },
    { key = "MAGE", label = "Mage", coords = { 0.25, 0.49609375, 0, 0.25 } },
    { key = "PALADIN", label = "Paladin", coords = { 0, 0.25, 0.5, 0.75 } },
    { key = "PRIEST", label = "Priest", coords = { 0.49609375, 0.7421875, 0.25, 0.5 } },
    { key = "ROGUE", label = "Rogue", coords = { 0.49609375, 0.7421875, 0, 0.25 } },
    { key = "SHAMAN", label = "Shaman", coords = { 0.25, 0.49609375, 0.25, 0.5 } },
    { key = "WARLOCK", label = "Warlock", coords = { 0.7421875, 0.98828125, 0.25, 0.5 } },
    { key = "WARRIOR", label = "Warrior", coords = { 0, 0.25, 0, 0.25 } },
    { key = "NEUTRAL", label = "Neutral", icon = "Interface\\Icons\\INV_Misc_Gear_01" },
}

-- Rarity filter. Basic ("FREE") cards count as Common.
CM.RARITIES = {
    { key = "ALL", label = "All rarities" },
    { key = "COMMON", label = "Common / Basic", color = "ffffffff" },
    { key = "RARE", label = "Rare", color = "ff0070dd" },
    { key = "EPIC", label = "Epic", color = "ffa335ee" },
    { key = "LEGENDARY", label = "Legendary", color = "ffff8000" },
}

-- Ownership filter. OWNED / UNOWNED need ownership data; without it they act like ALL.
CM.OWN_FILTERS = {
    { key = "ALL", label = "All cards" },
    { key = "OWNED", label = "Owned" },
    { key = "UNOWNED", label = "Not owned" },
}

-- ---------------------------------------------------------------- helpers
function CM.Trim(s)
    local _, _, t = string.find(tostring(s or ""), "^%s*(.-)%s*$")
    return t or ""
end

-- Removes WoW colour codes (|cAARRGGBB ... |r) from card text.
function CM.StripColors(s)
    s = tostring(s or "")
    s = string.gsub(s, "|c%x%x%x%x%x%x%x%x", "")
    s = string.gsub(s, "|r", "")
    return s
end

function CM.TabIndex(key)
    for i = 1, table.getn(CM.CLASS_TABS) do
        if CM.CLASS_TABS[i].key == key then
            return i
        end
    end
    return nil
end

function CM.TabLabel(key)
    local i = CM.TabIndex(key)
    if i then
        return CM.CLASS_TABS[i].label
    end
    return tostring(key)
end

-- Tab key for a WoW class token ("MAGE"), or nil when there is no such class tab.
function CM.TabForClassToken(token)
    if type(token) ~= "string" then
        return nil
    end
    local up = string.upper(token)
    if up == "ALL" then
        return nil
    end
    if CM.TabIndex(up) then
        return up
    end
    return nil
end

function CM.OwnIndex(key)
    for i = 1, table.getn(CM.OWN_FILTERS) do
        if CM.OWN_FILTERS[i].key == key then
            return i
        end
    end
    return nil
end

function CM.RarityIndex(key)
    for i = 1, table.getn(CM.RARITIES) do
        if CM.RARITIES[i].key == key then
            return i
        end
    end
    return nil
end

-- "COMMON", "RARE", "EPIC" or "LEGENDARY"; Basic and cards without rarity are Common.
function CM.RarityGroup(card)
    local r = card and card.rarity
    if r == "RARE" or r == "EPIC" or r == "LEGENDARY" then
        return r
    end
    return "COMMON"
end

function CM.RarityLabel(card)
    local r = card and card.rarity
    if r == "FREE" then
        return "Basic"
    elseif r == "RARE" then
        return "Rare"
    elseif r == "EPIC" then
        return "Epic"
    elseif r == "LEGENDARY" then
        return "Legendary"
    end
    return "Common"
end

-- Colour code prefix ("|cff0070dd") for the rarity of a card.
function CM.RarityColor(card)
    local g = CM.RarityGroup(card)
    local i = CM.RarityIndex(g)
    local c = i and CM.RARITIES[i].color or "ffffffff"
    if card and card.rarity == "FREE" then
        c = "ffd0d0d0"
    end
    return "|c" .. c
end

-- 0..7 (7 stands for 7 or more).
function CM.ManaBucket(cost)
    local c = tonumber(cost) or 0
    if c < 0 then
        c = 0
    end
    if c > CM.MANA_MAX then
        c = CM.MANA_MAX
    end
    return math.floor(c)
end

local function titleCase(s)
    s = string.lower(tostring(s or ""))
    s = string.gsub(s, "_", " ")
    return string.upper(string.sub(s, 1, 1)) .. string.sub(s, 2)
end

function CM.ClassLabel(class)
    local i = CM.TabIndex(class)
    if i then
        return CM.CLASS_TABS[i].label
    end
    return titleCase(class)
end

function CM.TypeLabel(card)
    if not card then
        return "?"
    end
    local t = titleCase(card.type or "?")
    if card.race then
        t = t .. " - " .. titleCase(card.race)
    end
    return t
end

-- "3/2" for minions, "3/2" (attack/durability) for weapons, nil for other cards.
function CM.StatsText(card)
    if card and card.atk ~= nil and card.hp ~= nil then
        return card.atk .. "/" .. card.hp
    end
    if card and card.atk ~= nil and card.dur ~= nil then
        return card.atk .. "/" .. card.dur
    end
    return nil
end

-- ------------------------------------------------------------------ data
-- The card database, or nil when CardDB.lua is missing or unusable.
function CM.GetDB()
    local db = HS_CardDB
    if type(db) == "table" and type(db.cards) == "table" and type(db.order) == "table" then
        return db
    end
    return nil
end

-- Ids whose texture was deployed to the client (HS_CardTextures.order), as a set.
-- Cached per HS_CardTextures table.
function CM.TextureSet()
    local t = HS_CardTextures
    if type(t) ~= "table" or type(t.order) ~= "table" then
        return {}
    end
    if CM.texSetFor ~= t or CM.texSetCount ~= table.getn(t.order) then
        local set = {}
        for i = 1, table.getn(t.order) do
            set[t.order[i]] = true
        end
        CM.texSet = set
        CM.texSetFor = t
        CM.texSetCount = table.getn(t.order)
    end
    return CM.texSet
end

function CM.HasTexture(id)
    return CM.TextureSet()[id] == true
end

-- ---------------------------------------------------------------- catalog
-- Sort order used in the book: mana cost, then name, then id.
local function lessCard(db)
    return function(a, b)
        local ca, cb = db.cards[a], db.cards[b]
        local ka, kb = ca.cost or 0, cb.cost or 0
        if ka ~= kb then
            return ka < kb
        end
        local na, nb = string.lower(ca.name or a), string.lower(cb.name or b)
        if na ~= nb then
            return na < nb
        end
        return a < b
    end
end

function CM.SortIds(ids, db)
    table.sort(ids, lessCard(db))
    return ids
end

-- Builds the browsable catalog from HS_CardDB.order: every collectible card
-- (acq "standard", "reward_only" or "quest"), sorted by cost and name. Cards earned only through quests
-- (the promos and the Curse of Naxxramas set) are listed whether or not they are owned.
-- Returns { db, ids, counts (per tab key), hay (id -> lower case search text), total }.
function CM.NewCatalog(db)
    if type(db) ~= "table" or type(db.cards) ~= "table" or type(db.order) ~= "table" then
        return nil
    end
    local cat = { db = db, ids = {}, counts = { ALL = 0 }, hay = {}, total = 0 }
    local seen = {}
    for i = 1, table.getn(db.order) do
        local id = db.order[i]
        local c = db.cards[id]
        if c and not seen[id] and (c.acq == "standard" or c.acq == "reward_only" or c.acq == "quest") then
            seen[id] = true
            table.insert(cat.ids, id)
            cat.counts.ALL = cat.counts.ALL + 1
            local k = c.class or "NEUTRAL"
            cat.counts[k] = (cat.counts[k] or 0) + 1
            local text = string.gsub(CM.StripColors(c.text or ""), "\n", " ")
            cat.hay[id] = string.lower(tostring(c.name or "") .. "\n" .. text)
        end
    end
    CM.SortIds(cat.ids, db)
    cat.total = table.getn(cat.ids)
    return cat
end

-- ---------------------------------------------------------------- filters
function CM.NewFilters()
    return { class = "ALL", mana = {}, rarity = "ALL", search = "", own = "ALL" }
end

-- Number of selected mana buttons.
function CM.ManaCount(f)
    local n = 0
    for i = 0, CM.MANA_MAX do
        if f.mana[i] then
            n = n + 1
        end
    end
    return n
end

-- True when a search, mana, rarity or ownership filter is set (the class tab does not count).
function CM.HasFilters(f)
    return CM.Trim(f.search) ~= "" or CM.ManaCount(f) > 0 or f.rarity ~= "ALL" or (f.own or "ALL") ~= "ALL"
end

function CM.ResetFilters(f)
    f.mana = {}
    f.rarity = "ALL"
    f.search = ""
    f.own = "ALL"
end

-- Splits a query into lower case words (all must occur in the name or the rules text).
function CM.SearchWords(search)
    local words = {}
    local q = string.lower(CM.Trim(search))
    for w in string.gfind(q, "%S+") do
        table.insert(words, w)
    end
    return words
end

-- Ids of the catalog that pass the filters, in catalog order. owned is the id -> entry map of
-- the owned cards, or nil while ownership is not known (the ownership filter is then ignored).
function CM.Query(cat, f, owned)
    local out = {}
    if not cat then
        return out
    end
    local db = cat.db
    local words = CM.SearchWords(f.search)
    local nWords = table.getn(words)
    local useMana = CM.ManaCount(f) > 0
    local own = "ALL"
    if type(owned) == "table" then
        own = f.own or "ALL"
    end
    for i = 1, table.getn(cat.ids) do
        local id = cat.ids[i]
        local c = db.cards[id]
        local ok = true
        if f.class ~= "ALL" and (c.class or "NEUTRAL") ~= f.class then
            ok = false
        end
        -- deck editing: only cards a deck of that class can hold, also on the All tab
        if ok and f.deckClass and (c.class or "NEUTRAL") ~= "NEUTRAL" and c.class ~= f.deckClass then
            ok = false
        end
        if ok and own == "OWNED" and not owned[id] then
            ok = false
        elseif ok and own == "UNOWNED" and owned[id] then
            ok = false
        end
        if ok and useMana and not f.mana[CM.ManaBucket(c.cost)] then
            ok = false
        end
        if ok and f.rarity ~= "ALL" and CM.RarityGroup(c) ~= f.rarity then
            ok = false
        end
        if ok and nWords > 0 then
            local hay = cat.hay[id] or ""
            for w = 1, nWords do
                -- plain find: the query is never a Lua pattern
                if not string.find(hay, words[w], 1, true) then
                    ok = false
                    break
                end
            end
        end
        if ok then
            table.insert(out, id)
        end
    end
    return out
end

-- ----------------------------------------------------------------- paging
function CM.PageCount(n, per)
    per = per or CM.PER_PAGE
    if n <= 0 then
        return 1
    end
    return math.floor((n + per - 1) / per)
end

function CM.ClampPage(page, pages)
    page = math.floor(tonumber(page) or 1)
    if page > pages then
        page = pages
    end
    if page < 1 then
        page = 1
    end
    return page
end

-- The ids shown on 'page' (1 based).
function CM.PageSlice(list, page, per)
    per = per or CM.PER_PAGE
    local out = {}
    local first = (page - 1) * per
    for i = 1, per do
        local id = list[first + i]
        if id == nil then
            break
        end
        out[i] = id
    end
    return out
end

-- -------------------------------------------------------- saved variables
-- Fills HearthstoneDB with defaults and repairs values of the wrong type.
-- Returns the (same or new) table.
function CM.ApplySavedDefaults(db)
    if type(db) ~= "table" then
        db = {}
    end
    db.version = CM.SAVED_VERSION
    -- left over from the removed minimap button and the /hs arch and /hs hand tuning commands
    db.minimapAngle, db.minimapHidden, db.arch, db.hand = nil, nil, nil, nil
    -- Match animations "on" | "off" | "fast" (/hs anim)
    if db.anim ~= "on" and db.anim ~= "off" and db.anim ~= "fast" then
        db.anim = "on"
    end
    if type(db.pos) ~= "table" or type(db.pos.left) ~= "number" or type(db.pos.top) ~= "number" then
        db.pos = nil
    end
    if type(db.bookArt) ~= "boolean" then
        db.bookArt = nil             -- unset: parchment pages on (/hs bookart off turns them off)
    end
    if type(db.tab) ~= "string" or not CM.TabIndex(db.tab) then
        db.tab = nil               -- unset: the UI picks the player's class tab
    end
    -- introDone[<realm>] = true once the server said the account finished the introduction's card quest:
    -- the micro menu button shows at login before the handshake (Hearthstone.IntroUnlocked)
    if type(db.introDone) ~= "table" then
        db.introDone = {}
    end
    for realm, v in pairs(db.introDone) do
        if type(realm) ~= "string" or v ~= true then
            db.introDone[realm] = nil
        end
    end
    -- packsSeen[<realm>] = highest pack opening the player finished revealing (Packs.lua)
    if type(db.packsSeen) ~= "table" then
        db.packsSeen = {}
    end
    for realm, seq in pairs(db.packsSeen) do
        if type(realm) ~= "string" or type(seq) ~= "number" or seq < 0 or seq ~= math.floor(seq) then
            db.packsSeen[realm] = nil
        end
    end
    return db
end

-- ---------------------------------------------------------- window layout
-- Scale (<= 1, never below 0.5) that makes a w x h window fit a screen.
function CM.FitScale(screenW, screenH, w, h)
    local s = 1
    if w > 0 and screenW and screenW > 0 then
        s = math.min(s, screenW * 0.96 / w)
    end
    if h > 0 and screenH and screenH > 0 then
        s = math.min(s, screenH * 0.96 / h)
    end
    if s < 0.5 then
        s = 0.5
    end
    return math.floor(s * 100) / 100
end

-- Default place of the collection window while no position is saved: top left corner
-- (left, top) in screen units, measured from the bottom left like a TOPLEFT/BOTTOMLEFT anchor.
-- 'w' and 'h' are the on-screen extent of the window including the class tabs and the bottom tabs
-- (already multiplied by the window scale). The window is centred, but kept right of 'clearLeft'
-- (the player and target frames sit in the top left corner of the screen) as far as the screen
-- allows, and always completely on the screen.
function CM.DefaultPosition(screenW, screenH, w, h, clearLeft)
    local left = (screenW - w) / 2
    if clearLeft and left < clearLeft then
        left = clearLeft
    end
    if left > screenW - w then
        left = screenW - w
    end
    if left < 0 then
        left = 0
    end
    local top = screenH - (screenH - h) / 2
    if top > screenH then
        top = screenH
    end
    return left, top
end

-- Which side of the window the enlarged card goes: "right" or "left". All values in
-- screen pixels (edge * effective scale).
function CM.PreviewSide(leftPx, rightPx, screenPx, previewPx)
    if leftPx == nil or rightPx == nil or screenPx == nil then
        return "right"
    end
    local spaceRight = screenPx - rightPx
    local spaceLeft = leftPx
    if spaceRight >= previewPx then
        return "right"
    end
    if spaceLeft >= previewPx then
        return "left"
    end
    if spaceRight >= spaceLeft then
        return "right"
    end
    return "left"
end

-- --------------------------------------------------------- card text lines
-- True for the cards that are earned only through quests: the promos (acq "reward_only") and the Curse of
-- Naxxramas set (acq "quest"). They are not in packs and cannot be crafted (see HsWallet.Craftable).
function CM.IsQuestReward(card)
    return card ~= nil and (card.acq == "reward_only" or card.acq == "quest")
end

-- Lines describing a card for the caption under the enlarged card and for the text
-- fallback when the card image is not installed. hasTexture only adds a hint line.
function CM.Caption(id, card, hasTexture)
    local lines = {}
    if not card then
        table.insert(lines, tostring(id))
        table.insert(lines, "No card data for this id")
        return lines
    end
    table.insert(lines, CM.RarityColor(card) .. tostring(card.name or id) .. "|r")
    local head = CM.RarityLabel(card) .. " " .. CM.TypeLabel(card)
    head = head .. ", " .. CM.ClassLabel(card.class or "NEUTRAL")
    table.insert(lines, head)
    local nums = {}
    if card.cost ~= nil then table.insert(nums, "Cost " .. card.cost) end
    if card.atk ~= nil then table.insert(nums, "Attack " .. card.atk) end
    if card.hp ~= nil then table.insert(nums, "Health " .. card.hp) end
    if card.dur ~= nil then table.insert(nums, "Durability " .. card.dur) end
    if table.getn(nums) > 0 then
        table.insert(lines, table.concat(nums, "   "))
    end
    if card.acq == "reward_only" then
        table.insert(lines, "|cffffd100Quest reward only|r")
    elseif card.acq == "quest" then
        table.insert(lines, "|cffffd100Quest reward - Curse of Naxxramas|r")
    end
    if not hasTexture then
        table.insert(lines, "|cff999999Card image not installed - text preview|r")
    end
    return lines
end

-- Values for the text fallback inside a card slot.
function CM.FallbackInfo(id, card)
    if not card then
        return { cost = "", name = tostring(id), typeLine = "No card data", text = "", stats = "" }
    end
    return {
        cost = tostring(card.cost or ""),
        name = tostring(card.name or id),
        typeLine = CM.RarityLabel(card) .. " " .. CM.TypeLabel(card),
        text = tostring(card.text or ""),
        stats = CM.StatsText(card) or "",
        color = CM.RarityColor(card),
        class = card.class or "NEUTRAL",
    }
end
