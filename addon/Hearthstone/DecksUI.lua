-- Hearthstone addon: the deck panel of the collection window.
--
-- There is no separate Decks view any more: the deck panel sits right of the book in the one
-- collection window (CollectionUI.lua) and has two modes that slide / fade into each other:
--
--   Browse  "My Decks": the account's saved decks as tiles (the hero art of the deck's class and the name; the
--           first tile is always New Deck), at most DU.ROWS tiles per page (mouse wheel / arrows), the rest of
--           the page filled with empty slots, and a plate "n / cap Decks". A left click on a tile edits that
--           deck, a right click renames it (a StaticPopup with an edit box), New Deck opens the class picker
--           over the book (nine classes; a class the character has not unlocked is shown greyed out, marked
--           "(locked)", with the tooltip "Not unlocked", and a click on it does nothing: DU.LOCKED_PICKABLE is
--           false), the plate's tooltip says which classes the character can play.
--   Edit    the deck's card list: the name (editable, on the title plate), class, "n / 30", one row per card
--           sorted by cost then name (cost gem, name in its rarity colour, a strip of the card's art, "2" or a
--           star for a Legendary; red when the account owns fewer copies), a mana curve, the reasons the draft
--           is incomplete and Save / Cancel (/ Delete for a saved deck). A click on a row removes one copy and
--           hovering shows the card. The book shows only the deck class tab and Neutral and only cards the deck
--           can hold: a left click on a card adds a copy, a right click removes one.
--
-- The draft lives in Decks.lua (HsDecks.draft), so closing the window keeps it: the panel shows the editor
-- again until it is saved or discarded. Saving goes through Hearthstone.lua (H.DeckSave), which retries
-- with the same key when the outcome is unknown. All frames are created once in DU.BuildPanel; the
-- StaticPopupDialogs are registered on first use.
--
-- The art is the owner's collection art (CollectionUI.Piece); without it every part has its native look.
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local D = HsDecks
local CM = HsCollection
local O = HsOwnership
H.DecksUI = H.DecksUI or {}
local DU = H.DecksUI

-- layout (pixels at scale 1, relative to the panel's top left corner)
DU.PANEL_W, DU.PANEL_H = 256, 800
DU.ROWS = 7                       -- deck tiles per page (New Deck is always the first tile)
DU.TILE_W, DU.TILE_H, DU.TILE_PITCH = 190, 64, 68
DU.STAR_SIZE = 26                 -- the default deck's star, on the upper edge of its tile
DU.TILE_X, DU.TILE_Y0 = 33, 92
DU.TRAY_ROWS = 30                 -- card rows of the edit mode (a deck has 30 cards at most: no scrolling)
DU.TRAY_ROW_H = 25                -- pitch of a card row; it shrinks (DU.RowPitch) when more cards than fit are listed
DU.ROW_X, DU.ROW_W, DU.ROW_Y0 = 38, 180, 116   -- inside the purple area of the panel art (x 35 ... 220, y 84 ... 768)
DU.ROW_Y_END = 646                -- the card rows end here at the latest
DU.CURVE_Y = 676                  -- baseline of the mana curve bars
DU.CURVE_H = 24
DU.BUTTONS_Y = 738                -- top of the Save / Cancel / Delete buttons
DU.TRANS_TIME = 0.25              -- seconds of the slide / fade between the two modes
DU.SLIDE = 40                     -- pixels the modes slide
-- Can a class the character has not unlocked be picked for a new deck? No: the picker greys it out
-- ("Not unlocked"). true allows decks of locked classes.
DU.LOCKED_PICKABLE = false
-- crops of the card texture (u1, u2, v1, v2 as fractions of the 256 x 388 image): the hero's portrait on a tile
-- (the fallback of the strips of the art pipeline: x 69 ... 187, y 99 ... 124 of the HERO_xx render is the inside of
-- the portrait arch around the face, with the aspect 4.7 : 1 of the tile's picture), a card's art on a row
DU.TILE_CROP = { 0.27, 0.73, 0.255, 0.32 }
DU.ROW_CROP = { 0.30, 0.70, 0.225, 0.335 }
DU.COST_FONT = 14                -- size of the mana cost on the gem of a row
DU.QTY_W = 20                    -- width of the dark box with the number of copies / the star at the right end of a row
DU.ROW_FONT = 11                  -- size of the card name in a row (the small template font is 10)
DU.PANEL_TILES = { "deck_panel_c0r0", "deck_panel_c0r1", "deck_panel_c0r2", "deck_panel_c0r3" }   -- the panel art (texture sides <= 512)
local WHITE = "Interface\\Buttons\\WHITE8X8"
local TOOLTIP_EDGE = "Interface\\Tooltips\\UI-Tooltip-Border"
local HILITE = "Interface\\QuestFrame\\UI-QuestTitleHighlight"
local LADDER_STAR = "Interface\\Hearthstone\\Textures\\UI\\LadderStar_full"
local STAR ="Interface\\TargetingFrame\\UI-RaidTargetingIcons"      -- the first icon of the sheet is the yellow star
local INK = { 0.25, 0.14, 0.04 }
DU.HINT = "Decks are shared by all characters of your account. Talk to a Hearthstone opponent to play a complete deck."

-- fallback colours of an art strip without a card texture
local CLASS_COLORS = {
    DRUID = { 0.40, 0.28, 0.10 }, HUNTER = { 0.18, 0.34, 0.14 }, MAGE = { 0.14, 0.24, 0.44 },
    PALADIN = { 0.44, 0.34, 0.16 }, PRIEST = { 0.40, 0.40, 0.40 }, ROGUE = { 0.34, 0.34, 0.14 },
    SHAMAN = { 0.10, 0.24, 0.42 }, WARLOCK = { 0.32, 0.14, 0.38 }, WARRIOR = { 0.42, 0.14, 0.14 },
    NEUTRAL = { 0.28, 0.23, 0.16 },
}

DU.panel = nil
DU.picker = nil
DU.tiles = {}
DU.empties = {}
DU.rows = {}
DU.pickButtons = {}
DU.curveBars = {}
DU.page = 1
DU.hoverRow = nil
DU.shown = "browse"               -- the mode on screen ("browse" or "edit"; the target while sliding)
DU.trans = nil                    -- a running slide: { from, to, t }
DU.shownDraft = nil
DU.heroIds = nil

-- ------------------------------------------------------------- helpers
local function charName()
    if UnitName then
        local n = UnitName("player")
        if type(n) == "string" and n ~= "" then
            return n
        end
    end
    return "This character"
end
DU.CharName = charName

local function collectionUI()
    return H.CollectionUI
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- Puts the class icon of the character creation atlas on a texture.
function DU.SetClassIcon(tex, class)
    local i = CM.TabIndex(class)
    local def = i and CM.CLASS_TABS[i]
    if def and def.coords then
        tex:SetTexture(CM.ICON_ATLAS)
        tex:SetTexCoord(def.coords[1], def.coords[2], def.coords[3], def.coords[4])
    else
        tex:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
        tex:SetTexCoord(0, 1, 0, 1)
    end
end

function DU.IsEditing()
    return D.draft ~= nil
end

-- The id of the hero card of a class (HERO_01 ... HERO_09, the card textures carry their art), or nil.
function DU.HeroCard(class)
    local db = CM.GetDB()
    if not db then
        return nil
    end
    if DU.heroIds == nil or DU.heroFor ~= db then
        DU.heroIds = {}
        DU.heroFor = db
        local ids = {}
        for id, c in pairs(db.cards) do
            if c.type == "HERO" and c.coll == 1 and c.class then
                table.insert(ids, id)
            end
        end
        table.sort(ids)
        for i = 1, table.getn(ids) do
            local c = db.cards[ids[i]]
            if DU.heroIds[c.class] == nil then
                DU.heroIds[c.class] = ids[i]
            end
        end
    end
    return DU.heroIds[class]
end

-- Crops the card texture of cardId into tex (crop: u1, u2, v1, v2 of the card image). Without the texture the
-- area is a flat colour of the class. Returns true when the texture is used.
function DU.SetArt(tex, cardId, crop, class)
    if cardId and CM.HasTexture(cardId) then
        local bottom = 388 / 512
        if H.CardBottom then
            bottom = H.CardBottom(cardId)
        end
        tex:SetTexture((H.CARD_TEXTURE_PATH or "Interface\\Hearthstone\\Textures\\Cards\\") .. cardId)
        tex:SetTexCoord(crop[1], crop[2], crop[3] * bottom, crop[4] * bottom)
        tex:SetVertexColor(1, 1, 1)
        return true
    end
    local db = CM.GetDB()
    local c = db and cardId and db.cards[cardId]
    local col = CLASS_COLORS[class or (c and c.class) or "NEUTRAL"] or CLASS_COLORS.NEUTRAL
    tex:SetTexture(WHITE)
    tex:SetTexCoord(0, 1, 0, 1)
    tex:SetVertexColor(col[1], col[2], col[3])
    return false
end

-- The picture of a deck tile (the hero of the deck's class): the strip of the hero's portrait that the art pipeline
-- makes (piece hero_<class>, tools/art/slice_collection.py, centred on the face), else the crop DU.TILE_CROP of
-- the hero card's texture, else a flat colour of the class. Returns "strip", "crop" or "color".
function DU.SetTileArt(tex, class)
    local UI = collectionUI()
    -- a selected alternative portrait replaces the pre-cut strip: the crop of its own texture
    local E = HsHeroes
    local alt = E and class and E.PortraitFor(class)
    if alt and alt ~= E.Default(class) then
        local path, bottom = E.Texture(alt)
        if path then
            tex:SetTexture(path)
            tex:SetTexCoord(DU.TILE_CROP[1], DU.TILE_CROP[2], DU.TILE_CROP[3] * bottom, DU.TILE_CROP[4] * bottom)
            tex:SetVertexColor(1, 1, 1)
            return "crop"
        end
    end
    local piece = class and UI and UI.Piece("hero_" .. string.lower(class))
    if piece then
        tex:SetTexture(piece.path)
        tex:SetTexCoord(0, piece.right, 0, piece.bottom)
        tex:SetVertexColor(1, 1, 1)
        return "strip"
    end
    if DU.SetArt(tex, DU.HeroCard(class), DU.TILE_CROP, class) then
        return "crop"
    end
    return "color"
end

-- Is the deck panel's art deployed (all its tiles)?
function DU.PanelTextured()
    local UI = collectionUI()
    return UI ~= nil and UI.TilesDeployed(DU.PANEL_TILES)
end

-- Is the collection tab on screen (the deck panel is part of it)?
local function tabShown()
    local UI = collectionUI()
    return UI and UI.frame and UI.frame:IsShown() and UI.state.bottom == 1
end

-- Redraws the window (switches between browse and edit mode when needed).
local function update()
    local UI = collectionUI()
    if UI and UI.Update then
        UI.Update()
    end
end

function H.OnDecksChanged()
    -- a deck deleted from the editor and confirmed by the server closes the editor before the window is laid out
    if DU.CheckDeleted then
        DU.CheckDeleted()
    end
    if tabShown() then
        update()
    end
end

-- ------------------------------------------------------------ list logic
function DU.PageCount()
    return CM.PageCount(D.DeckCount(), DU.ROWS)
end

-- Status line under the tiles: (text, isProblem).
function DU.ListStatus()
    if not H.state or not H.state.welcomed then
        return "Not connected to the server.", false
    end
    local s = D.state
    if s == "unsupported" then
        return "This server does not support decks yet.", false
    elseif s == "loading" or s == "none" then
        return "Loading your decks...", false
    elseif s == "error" then
        return "Your decks could not be loaded (" .. tostring(D.errorText or "?") .. ").", true
    end
    local op = D.op
    if op then
        if op.kind == "delete" then
            return "Deleting...", false
        end
        return "Saving...", false
    end
    if D.message then
        return D.message, false
    end
    if D.DeckCount() >= D.cap then
        return "You have the maximum of " .. D.cap .. " decks. Delete one to make room.", false
    end
    return "", false
end

function DU.EmptyText()
    if D.state == "ready" and D.DeckCount() == 0 then
        return "You have no decks yet. Click New Deck to build one."
    end
    return ""
end

-- Live state of a saved deck.
function DU.DeckState(deck)
    return D.Evaluate(deck.class, deck.cards, deck.serverState)
end

-- --------------------------------------------------------- browse refresh
function DU.RefreshList()
    local f = DU.panel
    if not f then
        return
    end
    local ready = D.state == "ready"
    local b = f.browse
    if ready then
        b.countText:SetText(D.DeckCount() .. " / " .. D.cap .. " Decks")
    else
        b.countText:SetText("")
    end
    local pages = DU.PageCount()
    DU.page = CM.ClampPage(DU.page, pages)
    local first = (DU.page - 1) * DU.ROWS
    local defaultId = D.DefaultId()
    for i = 1, DU.ROWS do
        local tile = DU.tiles[i]
        local deck = ready and D.decks[first + i]
        local empty = DU.empties[i]
        if tile then
            tile.deckId = deck and deck.id
            if deck then
                local r = DU.DeckState(deck)
                tile.artKind = DU.SetTileArt(tile.art, deck.class)
                tile.name:SetText(deck.name)
                if deck.id == defaultId then
                    tile.star:Show()
                else
                    tile.star:Hide()
                end
                local color = "|cffffd100"
                if D.IsComplete(r) then
                    color = "|cff33ff33"
                end
                tile.count:SetText(color .. r.count .. "/" .. D.DECK_SIZE .. "|r")
                tile:Show()
                if empty then
                    empty:Hide()
                end
            else
                tile:Hide()
                if empty then
                    empty:Show()
                end
            end
        end
    end
    b.emptyText:SetText(DU.EmptyText())
    b.countText:ClearAllPoints()
    if D.DeckCount() > DU.ROWS then
        b.countText:SetPoint("CENTER", b.counter, "CENTER", 0, 9)
        b.pageText:SetText("Page " .. DU.page .. " / " .. pages)
        b.prevButton:Show()
        b.nextButton:Show()
        if DU.page <= 1 then b.prevButton:Disable() else b.prevButton:Enable() end
        if DU.page >= pages then b.nextButton:Disable() else b.nextButton:Enable() end
    else
        -- alone on the plate: centred on the art of the plate (its drawn part is 1 px above the frame's middle)
        b.countText:SetPoint("CENTER", b.counter, "CENTER", 0, 1)
        b.pageText:SetText("")
        b.prevButton:Hide()
        b.nextButton:Hide()
    end
    local text, problem = DU.ListStatus()
    if problem then
        b.status:SetText("|cffff5555" .. text .. "|r")
    else
        b.status:SetText(text)
    end
    local blocked = "not loaded"
    if H.DeckOpBlocker then
        blocked = H.DeckOpBlocker()
    end
    local canNew = blocked == nil and D.DeckCount() < D.cap
    if canNew then
        b.newButton:Enable()
    else
        b.newButton:Disable()
    end
    if b.newButton.textured then
        local nt = b.newButton:GetNormalTexture()
        if nt then
            if canNew then
                nt:SetVertexColor(1, 1, 1)
            else
                nt:SetVertexColor(0.5, 0.5, 0.5)
            end
        end
    end
    if not ready or blocked ~= nil or D.draft then
        DU.HidePicker()
    end
end

-- ------------------------------------------------------------ edit logic
-- The book slot of card 'id' while editing: whether the deck holds a copy, whether every owned copy is in the deck
-- (the slot is then dimmed) and the deck limit when the deck holds that many (1 or 2, else nil).
function DU.SlotInfo(id)
    local n = D.InDraft(id)
    local ready = O.IsReady()
    local e = O.Get(id)
    local owned = (e and e.total) or 0
    local used = ready and owned > 0 and n >= owned
    if n <= 0 then
        return false, used, nil
    end
    -- a card at the deck's limit (1 or 2 copies): the book shows the "Deck Limit" plate; no counter below that
    local limit = D.MaxCopies(id)
    return true, used, (n >= limit) and limit or nil
end

-- Lines under the card rows: list of coloured strings.
function DU.TrayLines()
    local dr = D.draft
    local lines = {}
    if not dr then
        return lines
    end
    if D.addReason then
        table.insert(lines, "|cffffd100" .. D.addReason .. "|r")
    end
    if D.op then
        table.insert(lines, "Saving...")
    elseif D.message then
        table.insert(lines, D.message)
    end
    local r = D.Evaluate(dr.class, dr.cards, nil)
    local reasons = D.ReasonLines(r, dr.class, charName())
    for i = 1, table.getn(reasons) do
        local l = reasons[i]
        local text = l[1]
        if text == "Complete" then
            text = "Complete. Opponents offer it when all its cards work in matches."
        end
        table.insert(lines, "|cff" .. DU.Hex(l[2]) .. DU.Hex(l[3]) .. DU.Hex(l[4]) .. text .. "|r")
    end
    if not D.op then
        local why = H.DeckSaveBlocker and H.DeckSaveBlocker()
        if why and (D.IsDirty() or dr.deckId == 0) then
            table.insert(lines, "|cffff5555" .. why .. "|r")
        end
    end
    return lines
end

function DU.Hex(v)
    local n = math.floor(v * 255 + 0.5)
    local digits = "0123456789abcdef"
    local hi = math.floor(n / 16)
    local lo = n - hi * 16
    return string.sub(digits, hi + 1, hi + 1) .. string.sub(digits, lo + 1, lo + 1)
end

-- Texts of a card row: cost, name in its rarity colour (red when not owned enough), the count ("2", empty for a
-- single copy) and whether the star of a Legendary is shown instead.
function DU.RowTexts(row)
    local ready = O.IsReady()
    local e = O.Get(row.id)
    local owned = (e and e.total) or 0
    local short = ready and owned < row.qty
    local cost = tostring(row.card and row.card.cost or "?")
    local name
    if short then
        name = "|cffff5555" .. row.name .. "|r"
    else
        name = CM.RarityColor(row.card) .. row.name .. "|r"
    end
    local legendary = row.card and row.card.rarity == "LEGENDARY"
    local qty = ""
    if row.qty > 1 then
        qty = tostring(row.qty)
    end
    if short then
        qty = "|cffff5555" .. (row.qty > 1 and tostring(row.qty) or "!") .. "|r"
    end
    return cost, name, qty, legendary and not short, short
end

-- --------------------------------------------------------- edit refresh
function DU.SetNameText(text)
    local f = DU.panel
    if not f then
        return
    end
    DU.settingName = true
    f.edit.nameBox:SetText(text)
    DU.settingName = false
end

-- Pitch of the card rows for 'n' rows: DU.TRAY_ROW_H, less when the rows would not end above the mana curve.
function DU.RowPitch(n)
    if n < 1 then
        n = 1
    end
    local p = math.floor((DU.ROW_Y_END - DU.ROW_Y0) / n)
    if p > DU.TRAY_ROW_H then
        p = DU.TRAY_ROW_H
    end
    return p
end

function DU.LayoutRows(n)
    local pitch = DU.RowPitch(n)
    for i = 1, DU.TRAY_ROWS do
        local b = DU.rows[i]
        if b then
            b:ClearAllPoints()
            b:SetPoint("TOPLEFT", b:GetParent(), "TOPLEFT", DU.ROW_X, -(DU.ROW_Y0 + (i - 1) * pitch))
            b:SetHeight(pitch - 1)
        end
    end
    return pitch
end

function DU.RefreshEdit()
    local f = DU.panel
    local dr = D.draft
    if not f or not dr then
        return
    end
    local t = f.edit
    if DU.shownDraft ~= dr then
        -- another draft than last time: fresh name box
        DU.shownDraft = dr
        DU.SetNameText(dr.name)
    end
    DU.SetClassIcon(t.classIcon, dr.class)
    if dr.deckId == 0 then
        t.classText:SetText("New " .. D.ClassLabel(dr.class) .. " deck")
    else
        t.classText:SetText(D.ClassLabel(dr.class) .. " deck")
    end
    local count = D.Count(dr.cards)
    if count == D.DECK_SIZE then
        t.countText:SetText("|cff33ff33" .. count .. " / " .. D.DECK_SIZE .. "|r")
    else
        t.countText:SetText(count .. " / " .. D.DECK_SIZE)
    end

    local rows = D.Rows(dr)
    DU.LayoutRows(table.getn(rows))
    for i = 1, DU.TRAY_ROWS do
        local b = DU.rows[i]
        local row = rows[i]
        if b then
            b.cardId = row and row.id
            if row then
                local cost, name, qty, star = DU.RowTexts(row)
                b.cost:SetText(cost)
                b.name:SetText(name)
                b.qty:SetText(qty)
                if star then
                    b.star:Show()
                else
                    b.star:Hide()
                end
                if star or qty ~= "" then
                    b.qtyBox:Show()
                else
                    b.qtyBox:Hide()
                end
                DU.SetArt(b.art, row.id, DU.ROW_CROP)
                b:Show()
            else
                b:Hide()
            end
        end
    end
    if table.getn(rows) == 0 then
        t.emptyText:SetText("Left click cards in the book to add them.")
    else
        t.emptyText:SetText("")
    end

    local curve = D.Curve(dr)
    local top = 1
    for i = 0, CM.MANA_MAX do
        if curve[i] > top then top = curve[i] end
    end
    for i = 0, CM.MANA_MAX do
        local bar = DU.curveBars[i]
        if bar then
            bar:SetHeight(1 + math.floor(curve[i] * (DU.CURVE_H - 1) / top))
            if curve[i] > 0 then bar:SetAlpha(1) else bar:SetAlpha(0.25) end
        end
    end

    t.status:SetText(table.concat(DU.TrayLines(), "\n"))
    local why = H.DeckSaveBlocker and H.DeckSaveBlocker()
    if why == nil and (D.IsDirty() or dr.deckId == 0) then
        t.saveButton:Enable()
    else
        t.saveButton:Disable()
    end
    if D.op then
        t.saveButton:SetText("Saving...")
    else
        t.saveButton:SetText("Save")
    end
    -- Delete: only for a deck that is saved
    local deck = dr.deckId ~= 0 and D.byId and D.byId[dr.deckId]
    if deck then
        t.deleteButton:Show()
        if H.DeckOpBlocker and H.DeckOpBlocker() == nil then
            t.deleteButton:Enable()
        else
            t.deleteButton:Disable()
        end
    else
        t.deleteButton:Hide()
    end
    -- keep the enlarged card in step with the row under the mouse
    if DU.hoverRow then
        local b = DU.rows[DU.hoverRow]
        local UI = collectionUI()
        if b and b.cardId and b:IsShown() then
            UI.ShowPreview(b.cardId)
        else
            DU.hoverRow = nil
            UI.HidePreview()
        end
    end
end

-- A deck that was deleted from the edit mode: leave the editor once the server confirmed it (or drop the
-- request when it was refused).
function DU.CheckDeleted()
    local id = DU.leaveAfterDelete
    if not id or D.op then
        return
    end
    DU.leaveAfterDelete = nil
    if D.byId and D.byId[id] == nil and D.draft and D.draft.deckId == id then
        D.EndEdit()
        DU.hoverRow = nil
        local UI = collectionUI()
        if UI then
            UI.HidePreview()
        end
    end
end

-- ------------------------------------------------------ slide / fade between the modes
-- The mode the panel should show: the deck being edited, the craft panel while a crafting NPC is open
-- (CraftUI.lua) or the list of decks.
function DU.WantMode()
    if D.draft then
        return "edit"
    end
    local UI = collectionUI()
    if UI and UI.IsCraftMode and UI.IsCraftMode() and DU.panel and DU.panel.craft then
        return "craft"
    end
    return "browse"
end

local function place(frame, x, alpha)
    frame:ClearAllPoints()
    frame:SetPoint("TOPLEFT", DU.panel, "TOPLEFT", x, 0)
    frame:SetAlpha(alpha)
end

-- All containers in their resting place: the mode 'mode' is visible, the others hidden.
function DU.Snap(mode)
    local p = DU.panel
    if not p then
        return
    end
    local modes = { "browse", "edit", "craft" }
    for i = 1, table.getn(modes) do
        local c = p[modes[i]]
        if c then
            place(c, 0, 1)
            if modes[i] == mode then
                c:Show()
            else
                c:Hide()
            end
        end
    end
    DU.shown = mode
end

function DU.FinishTransition()
    local tr = DU.trans
    local p = DU.panel
    DU.trans = nil
    if p then
        p:SetScript("OnUpdate", nil)
        DU.Snap(tr and tr.to or DU.shown)
    end
end

-- The two containers at progress k (0 .. 1) of the slide: the old one slides out and fades, the new one
-- slides in from the other side (to the left when a deck is opened, back to the right when it is closed).
function DU.ApplyTransition(tr, k)
    local p = DU.panel
    local ease = k * k * (3 - 2 * k)
    local s = -1                       -- opening a deck: the browse mode leaves to the left
    if tr.to == "browse" then
        s = 1
    end
    place(p[tr.from], s * ease * DU.SLIDE, 1 - ease)
    place(p[tr.to], -s * (1 - ease) * DU.SLIDE, ease)
end

function DU.OnUpdate(elapsed)
    local tr = DU.trans
    if not tr then
        return
    end
    tr.t = tr.t + (tonumber(elapsed) or 0)
    if tr.t >= DU.TRANS_TIME then
        DU.FinishTransition()
    else
        DU.ApplyTransition(tr, tr.t / DU.TRANS_TIME)
    end
end

-- Shows the mode 'mode'. 'animate' slides from the mode on screen when the window is shown.
function DU.SetMode(mode, animate)
    local p = DU.panel
    if not p then
        return
    end
    if DU.trans then
        if DU.trans.to == mode then
            return                     -- already sliding there (another redraw of the same change)
        end
        DU.FinishTransition()
    end
    if mode == DU.shown then
        return
    end
    local UI = collectionUI()
    -- the slide is for the deck editor; the craft panel replaces the list at once
    local slides = mode ~= "craft" and DU.shown ~= "craft"
    if animate and slides and UI and UI.frame and UI.frame:IsShown() then
        DU.trans = { from = DU.shown, to = mode, t = 0 }
        DU.shown = mode
        p.browse:Show()
        p.edit:Show()
        DU.ApplyTransition(DU.trans, 0)
        p:SetScript("OnUpdate", function()
            DU.OnUpdate(arg1)
        end)
    else
        DU.Snap(mode)
    end
end

-- The window was opened: no slide, the panel shows the mode of the moment.
function DU.OnWindowShown()
    DU.snap = true
end

function DU.OnWindowHidden()
    if DU.trans then
        DU.FinishTransition()
    end
    -- a class picker left open must not come back over the book when the window reopens
    DU.HidePicker()
end

-- Called from CollectionUI.RefreshAll while the collection tab is shown.
function DU.Refresh()
    if not DU.panel then
        return
    end
    DU.CheckDeleted()
    local animate = not DU.snap
    DU.snap = false
    DU.SetMode(DU.WantMode(), animate)
    if D.draft then
        DU.RefreshEdit()
    end
    DU.RefreshList()
    if DU.shown == "craft" and H.CraftUI and H.CraftUI.RefreshSide then
        H.CraftUI.RefreshSide()
    end
end

-- The collection tab became visible (tab click, window opened): retry a failed load once.
function DU.OnTabShown()
    if D.state == "error" and H.decks and not H.decks.reqId and H.DeckList then
        H.DeckList()
    end
end

-- Forgets the hovered row (hiding a frame under the cursor may not fire OnLeave).
function DU.ClearHover()
    DU.hoverRow = nil
end

-- ------------------------------------------------------------- actions
local function opBlocked()
    return D.op ~= nil or D.state ~= "ready"
end

-- A click on a deck tile: edit that deck.
function DU.OnTileClick(i, button)
    local tile = DU.tiles[i]
    if not tile or not tile.deckId then
        return
    end
    local deck = D.byId[tile.deckId]
    if not deck then
        return
    end
    if button == "RightButton" then
        DU.StartRename(deck)
        return
    end
    if button == "MiddleButton" then
        -- the default deck for the ladder: no server call, so it works even while a save is in flight
        D.ToggleDefault(deck.id)
        update()
        return
    end
    if opBlocked() then
        return
    end
    hideTip()
    D.BeginEdit(deck)
    D.message = nil
    DU.HidePicker()
    update()
end

function DU.OnTileEnter(i)
    local tile = DU.tiles[i]
    local deck = tile and tile.deckId and D.byId[tile.deckId]
    if not deck or not GameTooltip then
        return
    end
    GameTooltip:SetOwner(tile, "ANCHOR_LEFT")
    GameTooltip:SetText(deck.name)
    GameTooltip:AddLine(D.ClassLabel(deck.class) .. ", " .. deck.count .. "/" .. D.DECK_SIZE .. " cards", 1, 1, 1)
    local r = DU.DeckState(deck)
    local lines = D.ReasonLines(r, deck.class, charName())
    for k = 1, table.getn(lines) do
        GameTooltip:AddLine(lines[k][1], lines[k][2], lines[k][3], lines[k][4])
    end
    if D.IsComplete(r) then
        GameTooltip:AddLine("Opponents offer it when all its cards work in matches.", 0.6, 0.6, 0.6)
    end
    GameTooltip:AddLine("Left click: edit. Right click: rename. Middle click: default for ladder matches.", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end

function DU.OnCounterEnter()
    if not GameTooltip or not DU.panel then
        return
    end
    GameTooltip:SetOwner(DU.panel.browse.counter, "ANCHOR_LEFT")
    GameTooltip:SetText("Decks")
    if D.state == "ready" then
        GameTooltip:AddLine(D.AccessText(charName()), 1, 1, 1)
    end
    GameTooltip:AddLine(DU.HINT, 0.6, 0.6, 0.6)
    GameTooltip:Show()
end

function DU.ChangePage(delta)
    local page = CM.ClampPage(DU.page + delta, DU.PageCount())
    if page ~= DU.page then
        DU.page = page
        DU.RefreshList()
    end
end

function DU.OnListWheel(delta)
    if DU.shown ~= "browse" then
        return
    end
    delta = tonumber(delta) or 0
    if delta > 0 then
        DU.ChangePage(-1)
    elseif delta < 0 then
        DU.ChangePage(1)
    end
end

function DU.OnNewClick()
    local why = H.DeckOpBlocker and H.DeckOpBlocker()
    if why then
        D.message = why
        DU.RefreshList()
        return
    end
    DU.ShowPicker()
end

function DU.OnPickClass(class)
    if not D.IsClass(class) then
        return
    end
    if not DU.LOCKED_PICKABLE and not D.CanPlay(class) then
        return                         -- not unlocked: the button is greyed out and does nothing
    end
    DU.HidePicker()
    hideTip()
    D.BeginNew(class)
    D.message = nil
    update()
end

-- Registers the three popups (StaticPopupDialogs exists only in the client).
local function popupEditBox()
    local f = this
    for _ = 1, 3 do
        if not f or not f.GetName then
            return nil
        end
        local name = f:GetName()
        if name then
            if string.sub(name, -7) == "EditBox" then
                return f
            end
            local eb = getglobal(name .. "EditBox")
            if eb then
                return eb
            end
        end
        f = f:GetParent()
    end
    return nil
end

local function popupText()
    local eb = popupEditBox()
    if eb then
        return eb:GetText()
    end
    return nil
end

local function closePopup()
    local f = this
    if f and f.GetParent and f:GetParent() then
        f:GetParent():Hide()
    end
end

function DU.EnsurePopups()
    if not StaticPopupDialogs then
        return false
    end
    if StaticPopupDialogs["HEARTHSTONE_RENAME_DECK"] then
        return true
    end
    StaticPopupDialogs["HEARTHSTONE_RENAME_DECK"] = {
        text = "Rename the deck \"%s\":",
        button1 = ACCEPT or "Accept",
        button2 = CANCEL or "Cancel",
        hasEditBox = 1,
        maxLetters = D.NAME_MAX_CHARS,
        OnShow = function()
            local eb = popupEditBox()
            if eb then
                eb:SetText(DU.popupName or "")
                eb:HighlightText()
            end
        end,
        OnAccept = function()
            DU.AcceptRename(popupText())
        end,
        EditBoxOnEnterPressed = function()
            DU.AcceptRename(popupText())
            closePopup()
        end,
        EditBoxOnEscapePressed = function()
            closePopup()
        end,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
    StaticPopupDialogs["HEARTHSTONE_DELETE_DECK"] = {
        text = "Delete the deck \"%s\"?\nThis cannot be undone.",
        button1 = YES or "Yes",
        button2 = NO or "No",
        OnAccept = function()
            DU.AcceptDelete()
        end,
        showAlert = 1,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
    StaticPopupDialogs["HEARTHSTONE_DISCARD_DECK"] = {
        text = "Discard your changes to the deck \"%s\"?",
        button1 = YES or "Yes",
        button2 = NO or "No",
        OnAccept = function()
            DU.AcceptDiscard()
        end,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
    return true
end

local function showPopup(which, arg)
    if DU.EnsurePopups() and StaticPopup_Show then
        return StaticPopup_Show(which, arg)
    end
    return nil
end

-- Right click on a tile: rename that deck (a save of the same cards under the new name).
function DU.StartRename(deck)
    if not deck then
        return
    end
    local why = H.DeckOpBlocker and H.DeckOpBlocker()
    if why then
        D.message = why
        DU.RefreshList()
        return
    end
    -- each popup remembers its own deck: popups are not modal, and a stale one must never act on
    -- the deck another popup was opened for
    DU.renameDeck = deck.id
    DU.popupName = deck.name
    local dlg = showPopup("HEARTHSTONE_RENAME_DECK", deck.name)
    local eb = dlg and dlg.GetName and getglobal(dlg:GetName() .. "EditBox")
    if eb then
        eb:SetText(deck.name)
        eb:HighlightText()
    end
end

function DU.AcceptRename(text)
    local deck = DU.renameDeck and D.byId[DU.renameDeck]
    DU.renameDeck = nil
    if not deck or text == nil then
        return
    end
    if D.TrimName(text) == deck.name then
        return                        -- unchanged
    end
    local key, why = H.DeckRename(deck.id, text)
    if not key then
        D.message = why
        H.PrintError(why)
    end
    DU.RefreshList()
end

-- The Delete button of the edit mode (a saved deck only).
function DU.OnDeleteClick()
    local dr = D.draft
    local deck = dr and dr.deckId ~= 0 and D.byId[dr.deckId]
    if not deck then
        return
    end
    DU.deleteDeck = deck.id
    showPopup("HEARTHSTONE_DELETE_DECK", deck.name)
end

function DU.AcceptDelete()
    local deck = DU.deleteDeck and D.byId[DU.deleteDeck]
    DU.deleteDeck = nil
    if not deck then
        return
    end
    local key, why = H.DeckDelete(deck.id)
    if not key then
        D.message = why
        H.PrintError(why)
    else
        DU.leaveAfterDelete = deck.id        -- the editor closes when the server confirmed it
    end
    update()
end

-- Ends editing without saving (after the confirmation when there were changes).
function DU.Discard()
    if not D.draft then
        return
    end
    D.EndEdit()
    D.message = nil
    DU.hoverRow = nil
    local UI = collectionUI()
    if UI then
        UI.HidePreview()
    end
    update()
end

-- Yes on the discard popup: only for the draft it was opened for (a stale popup must not end a
-- draft started later).
function DU.AcceptDiscard()
    local dr = DU.discardDraft
    DU.discardDraft = nil
    if dr and dr == D.draft then
        DU.Discard()
    end
end

function DU.OnCancelClick()
    local dr = D.draft
    if not dr then
        return
    end
    if D.IsDirty() then
        if StaticPopupDialogs and StaticPopup_Show then
            DU.discardDraft = dr
            showPopup("HEARTHSTONE_DISCARD_DECK", D.TrimName(dr.name))
            return
        end
    end
    DU.Discard()
end

function DU.OnSaveClick()
    local key, why = nil, "Hearthstone.lua is not loaded."
    if H.DeckSave then
        key, why = H.DeckSave()
    end
    D.addReason = nil
    if not key then
        D.message = why
    end
    update()
end

-- After an edit of the draft: the book badges and the card rows.
local function afterEdit()
    local UI = collectionUI()
    if UI and UI.RefreshPage then
        UI.RefreshPage()
    end
    DU.RefreshEdit()
end

-- A click on a card of the book while editing: left adds a copy, right removes one.
function DU.OnBookClick(id, button)
    if not D.draft or not id then
        return
    end
    local ok, why
    if button == "RightButton" then
        ok, why = D.RemoveCard(id)
    else
        ok, why = D.AddCard(id)
    end
    if ok then
        D.addReason = nil
    else
        local c = D.GetCard(id)
        D.addReason = tostring(c and c.name or id) .. ": " .. tostring(why)
    end
    afterEdit()
end

-- A click on a card row of the list removes one copy.
function DU.OnRowClick(i)
    local b = DU.rows[i]
    if not b or not b.cardId then
        return
    end
    local ok, why = D.RemoveCard(b.cardId)
    if ok then
        D.addReason = nil
    else
        D.addReason = why
    end
    afterEdit()
end

function DU.OnRowEnter(i)
    local b = DU.rows[i]
    if not b or not b.cardId then
        return
    end
    DU.hoverRow = i
    collectionUI().ShowPreview(b.cardId)
end

function DU.OnRowLeave(i)
    if DU.hoverRow == i then
        DU.hoverRow = nil
    end
    collectionUI().HidePreview()
end

function DU.OnNameChanged(text)
    if DU.settingName or not D.draft then
        return
    end
    if D.op then
        -- like adding and removing cards, renaming waits until the save in flight is answered
        DU.SetNameText(D.draft.name)
        return
    end
    D.SetDraftName(text)
    DU.RefreshEdit()
end

-- ------------------------------------------------------------ class picker
function DU.ShowPicker()
    local p = DU.picker
    if not p then
        return
    end
    for i = 1, table.getn(DU.pickButtons) do
        local b = DU.pickButtons[i]
        local label = D.ClassLabel(b.class)
        if D.CanPlay(b.class) then
            b.label:SetText(label)
            b.icon:SetVertexColor(1, 1, 1)
        else
            b.label:SetText("|cff7a6a58" .. label .. " (locked)|r")
            b.icon:SetVertexColor(0.45, 0.45, 0.45)
        end
        b.locked = not D.CanPlay(b.class) and not DU.LOCKED_PICKABLE
    end
    p.shade:Show()
    p:Show()
end

function DU.HidePicker()
    local p = DU.picker
    if p then
        p:Hide()
        p.shade:Hide()
    end
end

function DU.OnPickEnter(i)
    local b = DU.pickButtons[i]
    if not b or not GameTooltip then
        return
    end
    local label = D.ClassLabel(b.class)
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    GameTooltip:SetText(label)
    if D.CanPlay(b.class) then
        GameTooltip:AddLine("Build a new " .. label .. " deck.", 1, 1, 1)
    elseif DU.LOCKED_PICKABLE then
        GameTooltip:AddLine("Locked for " .. charName() .. ": " .. label .. " is not unlocked.", 1, 0.33, 0.33)
        GameTooltip:AddLine("You can still build it; a character with " .. label .. " unlocked can use it.", 1, 1, 1)
    else
        GameTooltip:AddLine("Not unlocked", 1, 0.33, 0.33)
        GameTooltip:AddLine(charName() .. " cannot play " .. label .. " yet.", 1, 1, 1)
    end
    GameTooltip:Show()
end

-- -------------------------------------------------------------- build
local function newText(parent, font, justify, layer)
    local fs = parent:CreateFontString(nil, layer or "ARTWORK", font)
    fs:SetJustifyH(justify or "LEFT")
    return fs
end

local function button(name, parent, width, text)
    local b = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
    b:SetWidth(width)
    b:SetHeight(22)
    b:SetText(text)
    return b
end

local function pagingButton(name, parent, kind)
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(22)
    b:SetHeight(22)
    b:SetNormalTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Up")
    b:SetPushedTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Down")
    b:SetDisabledTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Disabled")
    b:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
    return b
end

local function panelBackdrop(f)
    f:SetBackdrop({
        bgFile = WHITE, edgeFile = TOOLTIP_EDGE,
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    f:SetBackdropColor(0.10, 0.08, 0.05, 0.92)
    f:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
end

local function tilePos(i)
    return DU.TILE_X, -(DU.TILE_Y0 + i * DU.TILE_PITCH)        -- i = 0 is the New Deck tile
end

-- A deck tile: the hero art in the opening of the frame piece, the name and the card count over it.
local function makeTile(parent, i)
    local b = CreateFrame("Button", "HearthstoneDeckTile" .. i, parent)
    b:SetWidth(DU.TILE_W)
    b:SetHeight(DU.TILE_H)
    local x, y = tilePos(i)
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp", "MiddleButtonUp")
    b:SetHighlightTexture(HILITE, "ADD")
    b:EnableMouseWheel(true)
    local art = b:CreateTexture(nil, "BACKGROUND")
    local framed = collectionUI().Piece("deck_tile")
    if framed then
        art:SetPoint("TOPLEFT", b, "TOPLEFT", 19, -16)
        art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -19, 17)
    else
        b:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
        b:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        art:SetPoint("TOPLEFT", b, "TOPLEFT", 4, -4)
        art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -4, 4)
    end
    b.art = art
    local shade = b:CreateTexture(nil, "BORDER")
    shade:SetTexture(WHITE)
    shade:SetVertexColor(0, 0, 0, 0.5)
    shade:SetHeight(14)
    shade:SetPoint("BOTTOMLEFT", art, "BOTTOMLEFT", 0, 0)
    shade:SetPoint("BOTTOMRIGHT", art, "BOTTOMRIGHT", 0, 0)
    if framed then
        local fr = b:CreateTexture(nil, "ARTWORK")
        collectionUI().SetPiece(fr, "deck_tile")
        fr:SetAllPoints(b)
    end
    local name = newText(b, "GameFontHighlightSmall", "LEFT", "OVERLAY")
    name:SetPoint("BOTTOMLEFT", art, "BOTTOMLEFT", 4, 1)
    name:SetWidth(DU.TILE_W - 90)
    name:SetHeight(12)
    b.name = name
    local count = newText(b, "GameFontNormalSmall", "RIGHT", "OVERLAY")
    count:SetPoint("BOTTOMRIGHT", art, "BOTTOMRIGHT", -4, 1)
    count:SetWidth(50)
    b.count = count
    -- the ladder star marks the default deck, on the upper edge of the tile (the middle click sets it)
    local star = b:CreateTexture(nil, "OVERLAY")
    star:SetWidth(DU.STAR_SIZE)
    star:SetHeight(DU.STAR_SIZE)
    star:SetPoint("CENTER", b, "TOPRIGHT", -30, -8)
    star:SetTexture(LADDER_STAR)
    star:Hide()
    b.star = star
    local idx = i
    b:SetScript("OnClick", function()
        DU.OnTileClick(idx, arg1)
    end)
    b:SetScript("OnEnter", function()
        DU.OnTileEnter(idx)
    end)
    b:SetScript("OnLeave", hideTip)
    b:SetScript("OnMouseWheel", function()
        DU.OnListWheel(arg1)
    end)
    b:Hide()
    return b
end

-- The browse mode container.
local function buildBrowse(p, UI)
    local b = CreateFrame("Frame", "HearthstoneDecksBrowse", p)
    b:SetWidth(DU.PANEL_W)
    b:SetHeight(DU.PANEL_H)
    b:SetPoint("TOPLEFT", p, "TOPLEFT", 0, 0)

    local title = newText(b, "GameFontNormal", "CENTER")
    title:SetPoint("CENTER", b, "TOPLEFT", DU.PANEL_W / 2, -43)
    title:SetText("My Decks")
    if DU.PanelTextured() then
        title:SetTextColor(INK[1], INK[2], INK[3])
        title:SetShadowColor(0, 0, 0, 0)    -- the font's black shadow over dark ink reads as doubled text
    end
    b.title = title

    -- New Deck: the button of the sheet (its text is baked in), or a UIPanelButton
    local nx, ny = tilePos(0)
    local nb
    if UI.Piece("deck_new") then
        nb = CreateFrame("Button", "HearthstoneDeckNewButton", b)
        nb:SetWidth(DU.TILE_W)
        nb:SetHeight(DU.TILE_H)
        local piece = UI.Piece("deck_new")
        nb:SetNormalTexture(piece.path)
        nb:GetNormalTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
        nb:SetHighlightTexture(piece.path, "ADD")
        local ht = nb:GetHighlightTexture()
        ht:SetTexCoord(0, piece.right, 0, piece.bottom)
        ht:SetAlpha(0.3)
        nb.textured = true
    else
        nb = button("HearthstoneDeckNewButton", b, DU.TILE_W, "New Deck")
        nb:SetHeight(32)
    end
    nb:SetPoint("TOPLEFT", b, "TOPLEFT", nx, ny)
    nb:SetScript("OnClick", function()
        hideTip()
        DU.OnNewClick()
    end)
    nb:EnableMouseWheel(true)
    nb:SetScript("OnMouseWheel", function()
        DU.OnListWheel(arg1)
    end)
    b.newButton = nb

    DU.tiles = {}
    DU.empties = {}
    for i = 1, DU.ROWS do
        local x, y = tilePos(i)
        local e = b:CreateTexture(nil, "BACKGROUND")
        if UI.SetPiece(e, "deck_empty") then
            e:SetWidth(DU.TILE_W)
            e:SetHeight(DU.TILE_H - 2)
            e:SetPoint("TOPLEFT", b, "TOPLEFT", x, y)
            DU.empties[i] = e
        end
        DU.tiles[i] = makeTile(b, i)
    end

    local empty = newText(b, "GameFontNormal", "CENTER")
    empty:SetPoint("CENTER", b, "TOPLEFT", DU.PANEL_W / 2, -300)
    empty:SetWidth(170)
    if DU.PanelTextured() then
        empty:SetTextColor(0.8, 0.72, 0.55)
    end
    b.emptyText = empty

    local status = newText(b, "GameFontNormalSmall")
    status:SetPoint("TOPLEFT", b, "TOPLEFT", 26, -(DU.TILE_Y0 + (DU.ROWS + 1) * DU.TILE_PITCH + 2))
    status:SetWidth(204)
    status:SetHeight(46)
    status:SetJustifyV("TOP")
    b.status = status

    -- the counter plate "n / cap Decks" with the page arrows
    local counter = CreateFrame("Button", nil, b)
    counter:SetWidth(200)
    counter:SetHeight(48)
    counter:SetPoint("TOPLEFT", b, "TOPLEFT", 28, -704)
    local plate = counter:CreateTexture(nil, "BACKGROUND")
    if UI.SetPiece(plate, "plate_blue") then
        plate:SetAllPoints(counter)
    else
        counter:SetBackdrop({
            bgFile = WHITE, edgeFile = TOOLTIP_EDGE,
            tile = true, tileSize = 16, edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 },
        })
        counter:SetBackdropColor(0.12, 0.14, 0.26, 0.95)
        counter:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    end
    counter:SetScript("OnEnter", function()
        DU.OnCounterEnter()
    end)
    counter:SetScript("OnLeave", hideTip)
    b.counter = counter
    local count = newText(counter, "GameFontNormal", "CENTER", "OVERLAY")
    count:SetPoint("CENTER", counter, "CENTER", 0, 1)
    count:SetWidth(120)
    b.countText = count
    local pageText = newText(counter, "GameFontDisableSmall", "CENTER", "OVERLAY")
    pageText:SetPoint("CENTER", counter, "CENTER", 0, -4)
    pageText:SetWidth(120)
    b.pageText = pageText
    local prev = pagingButton("HearthstoneDeckPrevPage", counter, "PrevPage")
    prev:SetPoint("LEFT", counter, "LEFT", 20, 0)
    prev:SetScript("OnClick", function()
        DU.ChangePage(-1)
    end)
    b.prevButton = prev
    local nxt = pagingButton("HearthstoneDeckNextPage", counter, "NextPage")
    nxt:SetPoint("RIGHT", counter, "RIGHT", -20, 0)
    nxt:SetScript("OnClick", function()
        DU.ChangePage(1)
    end)
    b.nextButton = nxt
    return b
end

-- One card row of the edit mode: cost gem, name, a strip of the card's art, the copies.
local function makeRow(parent, i, UI)
    local b = CreateFrame("Button", "HearthstoneDeckRow" .. i, parent)
    b:SetWidth(DU.ROW_W)
    b:SetHeight(DU.TRAY_ROW_H - 1)
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", DU.ROW_X, -(DU.ROW_Y0 + (i - 1) * DU.RowPitch(DU.TRAY_ROWS)))
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    b:SetHighlightTexture(HILITE, "ADD")
    local bg = b:CreateTexture(nil, "BACKGROUND")
    bg:SetTexture(WHITE)
    bg:SetVertexColor(0.07, 0.05, 0.12, 0.75)
    bg:SetAllPoints(b)
    local art = b:CreateTexture(nil, "BORDER")
    art:SetWidth(56)
    art:SetPoint("TOPRIGHT", b, "TOPRIGHT", -1, -1)
    art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -1, 1)
    b.art = art
    local gem = b:CreateTexture(nil, "ARTWORK")
    if UI.SetPiece(gem, "gem_cost") then
        gem:SetWidth(22)
        gem:SetHeight(21)
        gem:SetPoint("LEFT", b, "LEFT", -1, 0)
    else
        gem:SetTexture(WHITE)
        gem:SetVertexColor(0.15, 0.35, 0.75)
        gem:SetWidth(18)
        gem:SetHeight(16)
        gem:SetPoint("LEFT", b, "LEFT", 1, 0)
    end
    local cost = newText(b, "GameFontNormalSmall", "CENTER", "OVERLAY")
    cost:SetPoint("CENTER", gem, "CENTER", 0, 0)
    cost:SetWidth(22)
    -- white with an outline on the blue gem (the gold small font was hard to read)
    if cost.SetFont then
        cost:SetFont("Fonts\\FRIZQT__.TTF", DU.COST_FONT, "OUTLINE")
    end
    cost:SetTextColor(1, 1, 1)
    b.cost = cost
    local name = newText(b, "GameFontHighlightSmall", "LEFT", "OVERLAY")
    name:SetPoint("LEFT", b, "LEFT", 25, 0)
    name:SetWidth(96)
    if name.SetFont then
        name:SetFont("Fonts\\FRIZQT__.TTF", DU.ROW_FONT)
    end
    name:SetHeight(12)
    b.name = name
    -- the number of copies (or the star of a legendary) in a dark box on the right end of the art, like the original
    local box = b:CreateTexture(nil, "ARTWORK")
    box:SetTexture(WHITE)
    box:SetVertexColor(0, 0, 0, 0.7)
    box:SetWidth(DU.QTY_W)
    box:SetPoint("TOPRIGHT", b, "TOPRIGHT", -1, -1)
    box:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -1, 1)
    box:Hide()
    b.qtyBox = box
    local qty = newText(b, "GameFontNormal", "CENTER", "OVERLAY")
    qty:SetPoint("CENTER", box, "CENTER", 0, 0)
    qty:SetWidth(DU.QTY_W)
    b.qty = qty
    local star = b:CreateTexture(nil, "OVERLAY")
    star:SetTexture(STAR)
    star:SetTexCoord(0, 0.25, 0, 0.25)
    star:SetWidth(15)
    star:SetHeight(15)
    star:SetPoint("CENTER", box, "CENTER", 0, 0)
    star:Hide()
    b.star = star
    local idx = i
    b:SetScript("OnClick", function()
        DU.OnRowClick(idx)
    end)
    b:SetScript("OnEnter", function()
        DU.OnRowEnter(idx)
    end)
    b:SetScript("OnLeave", function()
        DU.OnRowLeave(idx)
    end)
    b:Hide()
    return b
end

-- The edit mode container.
local function buildEdit(p, UI)
    local t = CreateFrame("Frame", "HearthstoneDecksEdit", p)
    t:SetWidth(DU.PANEL_W)
    t:SetHeight(DU.PANEL_H)
    t:SetPoint("TOPLEFT", p, "TOPLEFT", 0, 0)

    -- the name sits on the title plate of the panel art, or at the top of the plain panel
    local nameBox = CreateFrame("EditBox", "HearthstoneDeckNameBox", t, "InputBoxTemplate")
    if DU.PanelTextured() then
        local parts = { "Left", "Middle", "Right" }
        for i = 1, table.getn(parts) do
            local r = getglobal("HearthstoneDeckNameBox" .. parts[i])
            if r then
                r:Hide()
            end
        end
        -- the cream plaque of the art spans x 64 ... 192, y 30 ... 58 (the dark screws sit left and right of it)
        nameBox:SetWidth(124)
        nameBox:SetHeight(20)
        nameBox:SetPoint("TOPLEFT", t, "TOPLEFT", 66, -34)
        nameBox:SetJustifyH("CENTER")
        nameBox:SetTextColor(INK[1], INK[2], INK[3])
        nameBox:SetShadowColor(0, 0, 0, 0)
    else
        nameBox:SetWidth(DU.PANEL_W - 40)
        nameBox:SetHeight(20)
        nameBox:SetPoint("TOPLEFT", t, "TOPLEFT", 20, -26)
    end
    nameBox:SetAutoFocus(false)
    nameBox:SetMaxLetters(D.NAME_MAX_CHARS)
    nameBox:SetTextInsets(2, 2, 0, 0)
    nameBox:SetScript("OnTextChanged", function()
        DU.OnNameChanged(this:GetText())
    end)
    nameBox:SetScript("OnEnterPressed", function()
        this:ClearFocus()
    end)
    nameBox:SetScript("OnEscapePressed", function()
        this:ClearFocus()
    end)
    t.nameBox = nameBox

    local classIcon = t:CreateTexture(nil, "ARTWORK")
    -- the line "Rogue deck 30 / 30" sits in the purple area, above the card rows
    classIcon:SetWidth(20)
    classIcon:SetHeight(20)
    classIcon:SetPoint("TOPLEFT", t, "TOPLEFT", DU.ROW_X, -92)
    t.classIcon = classIcon
    local classText = newText(t, "GameFontNormal")
    classText:SetPoint("LEFT", classIcon, "RIGHT", 5, 0)
    classText:SetWidth(100)
    t.classText = classText
    local countText = newText(t, "GameFontNormalLarge", "RIGHT")
    countText:SetPoint("RIGHT", t, "TOPLEFT", DU.ROW_X + DU.ROW_W - 2, -102)
    countText:SetWidth(60)
    t.countText = countText

    DU.rows = {}
    for i = 1, DU.TRAY_ROWS do
        DU.rows[i] = makeRow(t, i, UI)
    end
    local empty = newText(t, "GameFontNormal", "CENTER")
    empty:SetPoint("TOP", t, "TOPLEFT", DU.PANEL_W / 2, -150)
    empty:SetWidth(170)
    if DU.PanelTextured() then
        empty:SetTextColor(0.8, 0.72, 0.55)
    end
    t.emptyText = empty

    -- mana curve: one bar per cost 0..7+
    DU.curveBars = {}
    local barsBottom = DU.CURVE_Y
    for i = 0, CM.MANA_MAX do
        local bar = t:CreateTexture(nil, "ARTWORK")
        bar:SetTexture(WHITE)
        bar:SetVertexColor(0.85, 0.7, 0.35)
        bar:SetWidth(14)
        bar:SetHeight(1)
        bar:SetPoint("BOTTOMLEFT", t, "TOPLEFT", DU.ROW_X + 4 + i * 22, -barsBottom)
        DU.curveBars[i] = bar
        local label = newText(t, "GameFontDisableSmall", "CENTER")
        label:SetPoint("TOP", t, "TOPLEFT", DU.ROW_X + 11 + i * 22, -barsBottom - 1)
        label:SetWidth(22)
        if i == CM.MANA_MAX then
            label:SetText(i .. "+")
        else
            label:SetText(tostring(i))
        end
    end

    local status = newText(t, "GameFontHighlightSmall")
    status:SetPoint("TOPLEFT", t, "TOPLEFT", DU.ROW_X, -(barsBottom + 14))
    status:SetWidth(DU.ROW_W)
    status:SetHeight(44)
    status:SetJustifyV("TOP")
    t.status = status

    local by = -DU.BUTTONS_Y
    local save = button("HearthstoneDeckSaveButton", t, 56, "Save")
    save:SetPoint("TOPLEFT", t, "TOPLEFT", DU.ROW_X, by)
    save:SetScript("OnClick", function()
        DU.OnSaveClick()
    end)
    t.saveButton = save
    local cancel = button("HearthstoneDeckCancelButton", t, 56, CANCEL or "Cancel")
    cancel:SetPoint("TOPLEFT", t, "TOPLEFT", DU.ROW_X + 62, by)
    cancel:SetScript("OnClick", function()
        DU.OnCancelClick()
    end)
    t.cancelButton = cancel
    local del = button("HearthstoneDeckDeleteButton", t, 56, "Delete")
    del:SetPoint("TOPLEFT", t, "TOPLEFT", DU.ROW_X + 124, by)
    del:SetScript("OnClick", function()
        hideTip()
        DU.OnDeleteClick()
    end)
    t.deleteButton = del
    return t
end

-- Builds the deck panel into the collection window (called once from CollectionUI's UI.Build).
function DU.BuildPanel(main, x, y)
    local UI = collectionUI()
    DU.rows, DU.tiles, DU.empties = {}, {}, {}
    local p = CreateFrame("Frame", "HearthstoneDecksPanel", main)
    p:SetWidth(DU.PANEL_W)
    p:SetHeight(DU.PANEL_H)
    p:SetPoint("TOPLEFT", main, "TOPLEFT", x, y)
    -- the panel art is four tiles of at most 256 x 256 (the client does not load a texture side above 512)
    p.artTiles = UI.PlaceTiles(p, "deck_panel", DU.PANEL_TILES, 0, 0, "BACKGROUND")
    if not p.artTiles then
        panelBackdrop(p)
    end
    p:EnableMouse(true)
    p:EnableMouseWheel(true)
    p:SetScript("OnMouseWheel", function()
        DU.OnListWheel(arg1)
    end)
    p:Hide()
    DU.panel = p
    p.browse = buildBrowse(p, UI)
    p.edit = buildEdit(p, UI)
    p.edit:Hide()
    -- the craft panel of a crafting NPC (CraftUI.lua) replaces the list while it is open
    if H.CraftUI and H.CraftUI.BuildSide then
        p.craft = H.CraftUI.BuildSide(p, UI)
        p.craft:Hide()
    end
    DU.shown = "browse"
    DU.trans = nil
    DU.snap = true
    return p
end

-- The class picker: a parchment over the book with the nine classes (a dim shade swallows clicks on the book
-- and closes the picker when clicked).
function DU.BuildPicker(main, cx, cy, level)
    local UI = collectionUI()
    DU.pickButtons = {}
    local shade = CreateFrame("Frame", "HearthstoneDeckClassPickerShade", main)
    shade:SetWidth(UI.BOOK_W)
    shade:SetHeight(UI.BOOK_H)
    shade:SetPoint("TOPLEFT", main, "TOPLEFT", 0, UI.PAGES_TOP)
    shade:SetFrameLevel(level - 1)
    shade:EnableMouse(true)
    shade:SetScript("OnMouseDown", function()
        DU.HidePicker()
    end)
    shade:Hide()
    local p = CreateFrame("Frame", "HearthstoneDeckClassPicker", main)
    p:SetWidth(356)
    p:SetHeight(378)
    p:SetPoint("CENTER", main, "TOPLEFT", cx, cy)
    p:SetFrameLevel(level)
    p:EnableMouse(true)
    local bg = p:CreateTexture(nil, "BACKGROUND")
    local textured = UI.SetPiece(bg, "parch_tall")
    local ink = textured ~= nil
    if textured then
        bg:SetWidth(textured.w)
        bg:SetHeight(textured.h)
        bg:SetPoint("TOPLEFT", p, "TOPLEFT", 0, 0)
    else
        p:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
            tile = true, tileSize = 32, edgeSize = 32,
            insets = { left = 11, right = 12, top = 12, bottom = 11 },
        })
    end
    local title = newText(p, "GameFontNormal", "CENTER")
    title:SetPoint("TOP", p, "TOP", 0, -34)
    title:SetText("Choose the class of the new deck")
    if ink then
        title:SetTextColor(INK[1], INK[2], INK[3])
        title:SetShadowColor(0, 0, 0, 0)
    end
    for i = 1, table.getn(D.CLASSES) do
        local class = D.CLASSES[i]
        local col = (i - 1) - math.floor((i - 1) / 3) * 3
        local rowN = math.floor((i - 1) / 3)
        local b = CreateFrame("Button", "HearthstoneDeckPick" .. i, p)
        b:SetWidth(98)
        b:SetHeight(76)
        b:SetPoint("TOPLEFT", p, "TOPLEFT", 31 + col * 100, -76 - rowN * 80)
        b:RegisterForClicks("LeftButtonUp")
        b:SetHighlightTexture(HILITE, "ADD")
        local icon = b:CreateTexture(nil, "ARTWORK")
        local piece = UI.TAB_PIECES[class] and UI.Piece(UI.TAB_PIECES[class])
        if piece then
            icon:SetTexture(piece.path)
            icon:SetTexCoord(0, piece.right, 0, piece.bottom)
            icon:SetWidth(piece.w)
            icon:SetHeight(piece.h)
        else
            DU.SetClassIcon(icon, class)
            icon:SetWidth(36)
            icon:SetHeight(36)
        end
        icon:SetPoint("TOP", b, "TOP", 0, -2)
        local label = newText(b, "GameFontHighlightSmall", "CENTER")
        label:SetPoint("BOTTOM", b, "BOTTOM", 0, 4)
        label:SetWidth(96)
        label:SetText(D.ClassLabel(class))
        if ink then
            label:SetTextColor(INK[1], INK[2], INK[3])
            label:SetShadowColor(0, 0, 0, 0)
        end
        b.class = class
        b.icon = icon
        b.label = label
        local idx = i
        b:SetScript("OnClick", function()
            DU.OnPickClass(DU.pickButtons[idx].class)
        end)
        b:SetScript("OnEnter", function()
            DU.OnPickEnter(idx)
        end)
        b:SetScript("OnLeave", hideTip)
        DU.pickButtons[i] = b
    end
    local cancel = button("HearthstoneDeckPickCancel", p, 96, CANCEL or "Cancel")
    -- below the third row of class buttons (they end 312 px from the top; the plate is 378 px tall), above them
    cancel:SetPoint("BOTTOM", p, "BOTTOM", 0, 38)
    cancel:SetFrameLevel(p:GetFrameLevel() + 5)
    cancel:SetScript("OnClick", function()
        DU.HidePicker()
    end)
    p.cancelButton = cancel
    p.shade = shade
    p:Hide()
    DU.picker = p
    return p
end

-- /hs decks: the collection window with the deck panel.
function H.ShowDecks()
    local UI = collectionUI()
    if not UI or not UI.EnsureBuilt or not UI.EnsureBuilt() then
        return false
    end
    UI.state.bottom = 1
    if not UI.frame:IsShown() then
        UI.frame:Show()
    end
    UI.SetBottomTab(1)
    return true
end
