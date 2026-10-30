-- Hearthstone addon: the craft panel of the collection window (economy).
--
-- While a crafting NPC is open (a Hearthstone Master's "Craft and scrap cards", HsWallet.craftOpen), a click
-- on a card in the collection book opens this panel over the book: the card, its owned copies and the crystal
-- status panel (Textures\UI\CraftPanel, the owner's art): the left slot disenchants one copy ("Disenchant", the
-- green bar shows "+N" dust), the right slot crafts it ("Craft", the red bar shows "-N"), the plate below shows
-- the dust. Scrapping a copy that a saved deck needs asks once more (the deck becomes unplayable until the
-- copy is back). The panel has no frame or close button: the book behind it is dimmed by a shade, and a click on
-- the shade (anywhere that is not the card or the panel) closes the view. The panel only sends requests through
-- Hearthstone.CraftCard / Hearthstone.ScrapCard; the server checks the NPC, the dust and the protected copies.
--
-- CU.Lines(id) computes everything the panel shows without a frame (the tests use it).
--
-- Session 14: while a crafting NPC is open the deck list on the right of the collection window is replaced by the
-- craft side panel (CU.BuildSide, a third mode of the deck panel, see DecksUI.lua): the explanation, the Owned /
-- Missing checkboxes (they drive the ownership filter of the book), the "Disenchant Extra Cards" button with the dust
-- bottle (CRAFT_EXTRAS: every copy above what a deck can use, never a protected one; the first click asks again) and
-- the dust number on the bottle. CU.SideLines() computes its texts without a frame.
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local O = HsOwnership
local W = HsWallet
H.CraftUI = H.CraftUI or {}
local CU = H.CraftUI

CU.W, CU.H = 450, 680
CU.CARD_W = 210
CU.CARD_TOP = 52
CU.BANNER_W, CU.BANNER_H = 64, 48      -- the ownership banner under the card (the book's is 52 x 39 under a 172 px card)
CU.BANNER_OVERLAP = 10                 -- its top is tucked this far behind the card's bottom edge
-- The crystal status panel: its image (1774 x 887 in the source, 512 x 256 as texture, the whole image) and the
-- rectangles the addon lays over it, in source pixels { left, top, right, bottom }, measured by hand on the art.
CU.PANEL_PATH = "Interface\\Hearthstone\\Textures\\UI\\CraftPanel"
CU.PANEL_W = 430
CU.PANEL_SRC_W, CU.PANEL_SRC_H = 1774, 887
CU.PANEL_H = math.floor(CU.PANEL_W * CU.PANEL_SRC_H / CU.PANEL_SRC_W + 0.5)
CU.RECTS = {
    scrap = { 218, 250, 787, 556 },       -- the grey slot with the burst, with its gold frame: Disenchant
    craft = { 1046, 250, 1615, 556 },     -- the dark slot: Craft (the same 569 x 306 as the left one, centred on the slot)
    green = { 255, 612, 748, 681 },       -- "+N" dust
    red = { 1082, 608, 1559, 682 },       -- "-N" dust
    plate = { 620, 770, 1240, 832 },      -- the dust of the player
}
-- The blue outline of a slot that can be used and the dimming of one that cannot (ui_icons.py --only glow): both are
-- one 512 x 256 texture with the arched slot shape inset GLOW_INSET px on every side of a shape of 464 x 208 px.
CU.GLOW_PATH = "Interface\\Hearthstone\\Textures\\UI\\SlotGlow"
CU.SHADE_PATH = "Interface\\Hearthstone\\Textures\\UI\\SlotShade"
CU.GLOW_INSET, CU.GLOW_SHAPE_W, CU.GLOW_SHAPE_H = 24, 464, 208
CU.GLOW_COLOR = { 0.35, 0.75, 1 }
CU.SHADE_ALPHA = 0.5          -- how much the book is dimmed while the panel is open
CU.GREEN = { 0.8, 1, 0.65 }
CU.RED = { 1, 0.7, 0.68 }
CU.BAR_BOTTLE_H = 26
CU.TEXT_SIZE = 14               -- the numbers and labels of the panel
CU.frame = nil
CU.cardId = nil
CU.confirm = nil              -- the card whose scrap needs a second click (a deck would lose a copy)
CU.side = nil                 -- the side panel (a container of the deck panel)
CU.extrasConfirm = nil        -- true after the first click on Disenchant Extra Cards
CU.confirmCopies = nil        -- the copies that first click was for (another number asks again)
CU.HELP = "Disenchant extra cards to create dust, then use it to craft the cards you want!\n\nSelect a card to start crafting."
CU.EXPLAIN = "Disenchants every copy beyond 2 (beyond 1 for Legendary cards). Starter and quest cards are never disenchanted."
CU.BOTTLE_H = 90              -- the bottle of the button
CU.BTN_W, CU.BTN_H = 190, 56
CU.RARITY_COLOR = { "ffffff", "5c9cff", "c880ff", "ff8c1a" }   -- the summary lines: Common, Rare, Epic, Legendary
CU.INK = { 0.25, 0.14, 0.04 }

local function card(id)
    return HS_CardDB and HS_CardDB.cards and HS_CardDB.cards[id]
end

local function rarityLabel(c)
    local b = W.Bucket(c)
    if not b then
        return ""
    end
    if c.rarity == "FREE" then
        return "Basic (crafts as Common)"
    end
    return W.RARITY_LABEL[b]
end

-- Everything the panel shows for card id: { title, rarity, owned, dust (the number on the plate), craftLabel,
-- craftAmount ("-40"), craftOk, craftHint, scrapLabel, scrapAmount ("+5"), scrapOk, scrapHint, warning, message }.
function CU.Lines(id)
    local c = card(id)
    local owned = O.Get(id)
    local t = {}
    t.title = c and c.name or tostring(id)
    t.rarity = rarityLabel(c)
    if owned then
        t.owned = "Owned: " .. owned.total
        if owned.prot > 0 then
            t.owned = t.owned .. " (" .. owned.prot .. " protected)"
        end
    else
        t.owned = "Not owned"
    end
    t.dust = W.IsReady() and tostring(W.dust) or ""
    local cost, value = W.CraftCost(c), W.ScrapValue(c)
    t.craftLabel = "Craft"
    t.craftAmount = cost and ("-" .. cost) or ""
    t.scrapLabel = "Disenchant"
    t.scrapAmount = value and ("+" .. value) or ""
    local blocker = H.CraftBlocker and H.CraftBlocker()
    t.craftHint = blocker or W.CraftBlocker(c, owned)
    t.scrapHint = blocker or W.ScrapBlocker(c, owned)
    t.craftOk = t.craftHint == nil
    t.scrapOk = t.scrapHint == nil
    -- A saved deck that needs more copies than remain after one scrap
    if t.scrapOk then
        local left = (owned and owned.total or 0) - 1
        local decks = W.DeckWarnings(id, left, HsDecks and HsDecks.decks)
        if table.getn(decks) > 0 then
            t.warning = "Scrapping leaves " .. left .. (left == 1 and " copy" or " copies") .. ": "
                .. table.concat(decks, ", ") .. (table.getn(decks) == 1 and " needs" or " need") .. " more."
            if CU.confirm == id then
                t.scrapLabel = "Disenchant\nanyway"
            end
        end
    end
    if W.IsBusy() then
        t.message = "Working..."
    elseif W.message then
        t.message = W.message
    elseif not t.craftOk then
        -- a disabled button shows no tooltip in 1.12: say why on the message line (the craft side first)
        t.message = t.craftHint
        if not t.scrapOk and t.scrapHint ~= t.craftHint and (owned and owned.total or 0) > 0 then
            t.message = t.scrapHint
        end
    elseif not t.scrapOk and owned then
        t.message = t.scrapHint
    end
    return t
end

-- ------------------------------------------------------------- actions
function CU.OnCraft()
    local id = CU.cardId
    if not id then
        return nil
    end
    CU.confirm = nil
    local key, why = H.CraftCard(id)
    if not key then
        W.message = why
    end
    CU.Refresh()
    return key, why
end

function CU.OnScrap()
    local id = CU.cardId
    if not id then
        return nil
    end
    local t = CU.Lines(id)
    if t.scrapOk and t.warning and CU.confirm ~= id then
        CU.confirm = id               -- first click: ask again
        CU.Refresh()
        return nil, "confirm"
    end
    CU.confirm = nil
    local key, why = H.ScrapCard(id, 1)
    if not key then
        W.message = why
    end
    CU.Refresh()
    return key, why
end

-- ------------------------------------------------------------- the frame
function CU.Show(id)
    CU.cardId = id
    CU.confirm = nil
    CU.extrasConfirm = nil
    W.message = nil
    if CU.frame then
        CU.frame.shade:Show()
        CU.frame:Show()
    end
    CU.Refresh()
end

function CU.Hide()
    CU.cardId = nil
    CU.confirm = nil
    if CU.frame then
        CU.frame:Hide()
        CU.frame.shade:Hide()
    end
end

function CU.IsShown()
    return CU.frame ~= nil and CU.frame:IsShown() and CU.cardId ~= nil
end

local function setButton(b, text, enabled)
    if b.label then
        b.label:SetText(text)
    else
        b:SetText(text)
    end
    if H.CollectionUI and H.CollectionUI.SetButtonEnabled then
        H.CollectionUI.SetButtonEnabled(b, enabled)
    elseif enabled then
        b:Enable()
    else
        b:Disable()
    end
end

-- A slot of the panel: the label on it and the dimmed look when the action is not possible.
local function setSlot(b, text, enabled)
    b.label:SetText(text)
    if enabled then
        b:Enable()
        b.label:Show()
        b.label:SetTextColor(1, 1, 1)
        b.dim:Hide()
        if b.glowOk then
            b.glow:Show()
        elseif b.SetBackdropBorderColor then
            b:SetBackdropBorderColor(CU.GLOW_COLOR[1], CU.GLOW_COLOR[2], CU.GLOW_COLOR[3], 1)
        end
    else
        -- nothing to do here: no text, a dimmed slot, no outline
        b:Disable()
        b.label:Hide()
        b.dim:Show()
        b.glow:Hide()
        if not b.glowOk and b.SetBackdropBorderColor then
            b:SetBackdropBorderColor(0.4, 0.4, 0.4, 1)
        end
    end
end

-- The number on a bar of the panel ("+20", "-100") with the bottle after it; nothing when the card has no value.
local function setAmount(fs, bottle, text)
    fs:SetText(text)
    if text ~= "" then
        bottle:Show()
    else
        bottle:Hide()
    end
end

function CU.Refresh()
    CU.RefreshSide()
    local f = CU.frame
    if not f then
        return
    end
    -- the access closed (the player walked away): the panel goes with it
    if CU.cardId and not W.craftOpen then
        CU.Hide()
        return
    end
    if not CU.cardId or not f:IsShown() then
        return
    end
    local t = CU.Lines(CU.cardId)
    H.CardWidget.SetCard(f.widget, CU.cardId)
    f.title:SetText(t.title)
    f.rarity:SetText(t.rarity)
    local num = O.Get(CU.cardId)
    num = num and num.total or 0
    local UIc = H.CollectionUI
    local banner = num > 0 and UIc and UIc.PaintBanner and UIc.Piece("banner_x1") and UIc.Piece("banner_x2")
        and UIc.PaintBanner(f.banner, f.bannerTex, f.bannerText, num, CU.BANNER_W / 52)
    if banner then
        f.owned:SetText("")
    else
        f.banner:Hide()
        f.owned:SetText(t.owned)
    end
    setSlot(f.craftButton, t.craftLabel, t.craftOk)
    setSlot(f.scrapButton, t.scrapLabel, t.scrapOk)
    f.craftButton.hint = t.craftHint
    f.scrapButton.hint = t.scrapHint
    setAmount(f.greenText, f.greenBottle, t.scrapAmount)
    setAmount(f.redText, f.redBottle, t.craftAmount)
    f.dustText:SetText(t.dust)
    if t.dust ~= "" then
        f.plateBottle:Show()
    else
        f.plateBottle:Hide()
    end
    if t.warning then
        f.warning:SetText("|cffff9933" .. t.warning .. "|r")
    else
        f.warning:SetText("")
    end
    f.message:SetText(t.message or "")
end

local function hintTip(b)
    if not GameTooltip or not b.hint then
        return
    end
    GameTooltip:SetOwner(b, "ANCHOR_TOP")
    GameTooltip:SetText(b.hint, 1, 0.82, 0, 1, 1)
    GameTooltip:Show()
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- Builds the panel over the book (centre cx, cy relative to main's top left), above the cards (level).
function CU.Build(main, cx, cy, level)
    if CU.frame then
        return CU.frame
    end
    local f = CreateFrame("Frame", "HearthstoneCraftPanel", main)
    f:SetWidth(CU.W)
    f:SetHeight(CU.H)
    f:SetPoint("CENTER", main, "TOPLEFT", cx, cy)
    f:SetFrameLevel(level)
    -- no backdrop and no close button: the rest of the book is dimmed by a shade that closes the view when it is
    -- clicked (like the class picker's), and the empty parts of this frame let the click through to it
    local UIc = H.CollectionUI
    local shade = CreateFrame("Frame", "HearthstoneCraftShade", main)
    shade:SetWidth(UIc and UIc.BOOK_W or CU.W)
    shade:SetHeight(UIc and UIc.BOOK_H or CU.H)
    shade:SetPoint("TOPLEFT", main, "TOPLEFT", 0, UIc and UIc.PAGES_TOP or 0)
    shade:SetFrameLevel(level - 1)
    shade:EnableMouse(true)
    local dim = shade:CreateTexture(nil, "BACKGROUND")
    dim:SetAllPoints(shade)
    dim:SetTexture(0, 0, 0, CU.SHADE_ALPHA)
    shade:SetScript("OnMouseDown", function() CU.Hide() end)
    shade:Hide()
    f.shade = shade

    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOP", f, "TOP", 0, -14)
    title:SetWidth(CU.W - 50)
    f.title = title
    local rarity = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    rarity:SetPoint("TOP", title, "BOTTOM", 0, -3)
    f.rarity = rarity

    local widget = H.CardWidget.Create(f, "HearthstoneCraftPanelCard", CU.CARD_W, false)
    widget.frame:SetPoint("TOP", f, "TOP", 0, -CU.CARD_TOP)
    f.widget = widget
    local cardH = math.floor(CU.CARD_W * 388 / 256 + 0.5)
    local vl, vt, vr, vb = 0, 0, CU.CARD_W, cardH
    if H.CardWidget.VisibleRect then
        vl, vt, vr, vb = H.CardWidget.VisibleRect(CU.CARD_W)
    end

    -- the copies the player owns: the collection's "x1" / "x2" / "x3" banner hanging under the card (CollectionUI.lua
    -- UI.PaintBanner); a plain "Owned: N" line takes its place when the banner art is not deployed
    local bannerTop = CU.CARD_TOP + vb - CU.BANNER_OVERLAP
    local banner = CreateFrame("Frame", nil, f)
    banner:SetFrameLevel(level)
    banner:SetWidth(CU.BANNER_W)
    banner:SetHeight(CU.BANNER_H)
    banner:SetPoint("TOP", f, "TOP", 0, -bannerTop)
    local bannerTex = banner:CreateTexture(nil, "ARTWORK")
    bannerTex:SetAllPoints(banner)
    local bannerText = banner:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    bannerText:SetShadowColor(0, 0, 0, 0)
    bannerText:SetPoint("CENTER", banner, "CENTER", 0, 3)
    bannerText:Hide()
    banner:Hide()
    f.banner, f.bannerTex, f.bannerText = banner, bannerTex, bannerText
    local owned = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    owned:SetPoint("TOP", f, "TOP", 0, -bannerTop - 8)
    f.owned = owned
    -- the crystal status panel under the card
    local panel = CreateFrame("Frame", "HearthstoneCraftPanelArt", f)
    panel:SetWidth(CU.PANEL_W)
    panel:SetHeight(CU.PANEL_H)
    panel:SetPoint("TOP", f, "TOP", 0, -bannerTop - CU.BANNER_H - 4)
    panel:SetFrameLevel(level + 1)
    f.panel = panel
    local art = panel:CreateTexture(nil, "ARTWORK")
    art:SetAllPoints(panel)
    local textured = art:SetTexture(CU.PANEL_PATH) and true or false
    f.art = art
    local UI = H.CollectionUI

    -- a rectangle of CU.RECTS (source pixels) as a child area of the panel
    local function place(region, r)
        local sx, sy = CU.PANEL_W / CU.PANEL_SRC_W, CU.PANEL_H / CU.PANEL_SRC_H
        region:ClearAllPoints()
        region:SetPoint("TOPLEFT", panel, "TOPLEFT", r[1] * sx, -r[2] * sy)
        region:SetWidth((r[3] - r[1]) * sx)
        region:SetHeight((r[4] - r[2]) * sy)
    end
    local function bottleIn(parent, height)
        local t = parent:CreateTexture(nil, "OVERLAY")
        if UI and UI.SetBottle then
            UI.SetBottle(t, height)
        end
        return t
    end

    -- the two slots are buttons over the art: the left one disenchants, the right one crafts
    local function slot(name, rect, level2)
        local b = CreateFrame("Button", name, panel)
        place(b, rect)
        b:SetFrameLevel(level2)
        if not textured then
            b:SetBackdrop({
                bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
                edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
                tile = true, tileSize = 16, edgeSize = 12,
                insets = { left = 3, right = 3, top = 3, bottom = 3 },
            })
        end
        -- the glow and the shade are the slot's arched shape, a little bigger than the button (the line and the
        -- glow lie outside the shape)
        local function shaped(layer, path)
            local t = b:CreateTexture(nil, layer)
            local padX = (rect[3] - rect[1]) * CU.PANEL_W / CU.PANEL_SRC_W * CU.GLOW_INSET / CU.GLOW_SHAPE_W
            local padY = (rect[4] - rect[2]) * CU.PANEL_H / CU.PANEL_SRC_H * CU.GLOW_INSET / CU.GLOW_SHAPE_H
            t:SetPoint("TOPLEFT", b, "TOPLEFT", -padX, padY)
            t:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", padX, -padY)
            return t, t:SetTexture(path)
        end
        local glow, glowOk = shaped("OVERLAY", CU.GLOW_PATH)
        glow:SetVertexColor(CU.GLOW_COLOR[1], CU.GLOW_COLOR[2], CU.GLOW_COLOR[3])
        glow:SetAlpha(0.8)
        glow:Hide()
        b.glow = glow
        b.glowOk = glowOk and true or false
        local dim, dimOk = shaped("OVERLAY", CU.SHADE_PATH)
        if not dimOk then
            dim:SetTexture(0, 0, 0, CU.SHADE_ALPHA)
        end
        dim:Hide()
        b.dim = dim
        local label = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        label:SetFont(CU.NUMBER_FONT, CU.TEXT_SIZE, "OUTLINE")
        label:SetPoint("CENTER", b, "CENTER", 0, 0)
        label:SetWidth(120)
        label:SetJustifyH("CENTER")
        b.label = label
        return b
    end
    local scrapButton = slot("HearthstoneScrapButton", CU.RECTS.scrap, level + 3)
    local craftButton = slot("HearthstoneCraftButton", CU.RECTS.craft, level + 3)
    scrapButton:SetScript("OnClick", function() CU.OnScrap() end)
    craftButton:SetScript("OnClick", function() CU.OnCraft() end)
    -- a usable slot's outline brightens under the mouse
    local function enter(b)
        b.glow:SetAlpha(1)
        hintTip(b)
    end
    local function leave(b)
        b.glow:SetAlpha(0.8)
        hideTip()
    end
    scrapButton:SetScript("OnEnter", function() enter(scrapButton) end)
    craftButton:SetScript("OnEnter", function() enter(craftButton) end)
    scrapButton:SetScript("OnLeave", function() leave(scrapButton) end)
    craftButton:SetScript("OnLeave", function() leave(craftButton) end)
    -- a disabled button gets no OnEnter in 1.12: its hint is the message line below instead
    f.craftButton = craftButton
    f.scrapButton = scrapButton

    -- the bars: "+N" on the green one, "-N" on the red one, each followed by the bottle; the dust on the plate
    local function amount(name, rect, color, size)
        local holder = CreateFrame("Frame", name, panel)
        place(holder, rect)
        holder:SetFrameLevel(level + 2)
        local fs = holder:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        fs:SetFont(CU.NUMBER_FONT, CU.TEXT_SIZE, "OUTLINE")
        fs:SetTextColor(color[1], color[2], color[3])
        fs:SetPoint("CENTER", holder, "CENTER", -6, 0)
        local bottle = bottleIn(holder, size)
        bottle:SetPoint("LEFT", fs, "RIGHT", 3, 0)
        return fs, bottle
    end
    f.greenText, f.greenBottle = amount("HearthstoneCraftGain", CU.RECTS.green, CU.GREEN, CU.BAR_BOTTLE_H)
    f.redText, f.redBottle = amount("HearthstoneCraftCost", CU.RECTS.red, CU.RED, CU.BAR_BOTTLE_H)
    f.dustText, f.plateBottle = amount("HearthstoneCraftDust", CU.RECTS.plate, { 1, 1, 1 }, CU.BAR_BOTTLE_H)

    local warning = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    warning:SetPoint("TOP", panel, "BOTTOM", 0, -8)
    warning:SetWidth(CU.W - 30)
    f.warning = warning
    local message = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    message:SetPoint("BOTTOM", f, "BOTTOM", 0, 14)
    message:SetWidth(CU.W - 30)
    f.message = message

    f:Hide()
    CU.frame = f
    return f
end

-- ------------------------------------------------------------ the side panel
-- Everything the side panel shows: { title, help, extras (the line above the button), explain, copies, dust, kinds,
-- buttonLabel, buttonOk, dustText, message, ownedChecked, missingChecked, checksOk }.
function CU.SideLines()
    local UI = H.CollectionUI
    local t = {}
    t.title = "Card Crafting"
    t.help = CU.HELP
    t.explain = CU.EXPLAIN
    local ready = O.IsReady()
    local per
    t.copies, t.dust, t.kinds, per = W.Extras(ready and O.owned or nil, HS_CardDB and HS_CardDB.cards)
    -- the summary above the button: one line per rarity that has extra copies, the rarest first
    local lines = {}
    for b = 4, 1, -1 do
        if per[b] then
            local c = CU.RARITY_COLOR[b]
            table.insert(lines, string.format("|cff%s%d %s|r   +%d", c, per[b][1], W.RARITY_LABEL[b], per[b][2]))
        end
    end
    t.summary = table.concat(lines, "\n")
    if t.copies > 0 then
        t.extras = ""                 -- the summary above and the bottle say it
    elseif ready then
        t.extras = "No extra cards."
    else
        t.extras = ""
    end
    if CU.extrasConfirm and CU.confirmCopies ~= t.copies then
        CU.extrasConfirm = nil        -- the collection changed since the first click: ask again
    end
    local blocker = H.CraftBlocker and H.CraftBlocker()
    t.buttonOk = blocker == nil and t.copies > 0
    if CU.extrasConfirm and t.buttonOk then
        t.buttonLabel = "Click again\nto confirm"
    else
        t.buttonLabel = "Disenchant\nExtra Cards"
    end
    -- the bottle shows what the button gives, not what the player has
    t.dustText = ready and (t.dust > 0 and ("+" .. t.dust) or "0") or ""
    if W.IsBusy() then
        t.message = "Working..."
    elseif W.message then
        t.message = W.message
    elseif blocker and t.copies > 0 then
        t.message = blocker
    end
    local own = UI and UI.state and UI.state.filters and UI.state.filters.own or "ALL"
    t.ownedChecked = own ~= "UNOWNED"
    t.missingChecked = own ~= "OWNED"
    t.checksOk = ready
    return t
end

-- A click on a checkbox of the side panel. which: "owned" or "missing". The checkboxes are the ownership filter of the
-- book in two parts: both = all cards, only Owned, only Missing (the dropdown's "Not owned"); the last one stays checked.
function CU.OnCheckClick(which)
    local UI = H.CollectionUI
    if not UI or not O.IsReady() then
        CU.RefreshSide()
        return nil
    end
    local own = UI.state.filters.own
    local owned, missing = own ~= "UNOWNED", own ~= "OWNED"
    if which == "owned" then
        owned = not owned
    else
        missing = not missing
    end
    if not owned and not missing then
        CU.RefreshSide()              -- the last checkbox cannot be unchecked: the click is undone
        return nil
    end
    local key = "ALL"
    if owned and not missing then
        key = "OWNED"
    elseif missing and not owned then
        key = "UNOWNED"
    end
    UI.SetOwnFilter(key)
    CU.RefreshSide()
    return key
end

-- The Disenchant Extra Cards button: the first click asks again (it cannot be undone), the second sends CRAFT_EXTRAS.
function CU.OnExtrasClick()
    local t = CU.SideLines()
    if not t.buttonOk then
        return nil, t.message
    end
    if not CU.extrasConfirm then
        CU.extrasConfirm = true
        CU.confirmCopies = t.copies
        W.message = nil
        CU.Refresh()
        return nil, "confirm"
    end
    CU.extrasConfirm = nil
    W.message = nil
    local key, why = H.ScrapExtras()
    if not key then
        W.message = why
    end
    CU.Refresh()
    return key, why
end

function CU.RefreshSide()
    local s = CU.side
    if not s or not s:IsShown() then
        return
    end
    local t = CU.SideLines()
    s.help:SetText(t.help)
    s.extras:SetText(t.extras)
    s.summary:SetText(t.summary)
    s.explain:SetText(t.explain)
    s.message:SetText(t.message or "")
    setButton(s.button, t.buttonLabel, t.buttonOk)
    s.dust:SetText(t.dustText)
    if string.len(t.dustText) > 5 then
        s.dust:SetFont(CU.NUMBER_FONT, 11, "OUTLINE")
    else
        s.dust:SetFont(CU.NUMBER_FONT, 15, "OUTLINE")
    end
    s.ownedBox:SetChecked(t.ownedChecked and 1 or nil)
    s.missingBox:SetChecked(t.missingChecked and 1 or nil)
    for _, box in ipairs({ s.ownedBox, s.missingBox }) do
        if t.checksOk then
            box:Enable()
        else
            box:Disable()
        end
    end
end

-- Builds the side panel as a container of the deck panel p (the same size, over the panel art; DecksUI.lua shows it
-- instead of the deck list while a crafting NPC is open). The purple area of the art is x 35 ... 220, y 84 ... 768.
CU.NUMBER_FONT = "Fonts\\FRIZQT__.TTF"
CU.LIGHT = { 0.8, 0.72, 0.55 }

function CU.BuildSide(p, UI)
    if CU.side then
        CU.side = nil                 -- a rebuilt window builds its own
    end
    local DU = H.DecksUI
    local w, h = DU and DU.PANEL_W or 256, DU and DU.PANEL_H or 800
    local textured = DU and DU.PanelTextured and DU.PanelTextured()
    local s = CreateFrame("Frame", "HearthstoneCraftSide", p)
    s:SetWidth(w)
    s:SetHeight(h)
    s:SetPoint("TOPLEFT", p, "TOPLEFT", 0, 0)
    local function text(font, y, width, color)
        local fs = s:CreateFontString(nil, "OVERLAY", font)
        fs:SetPoint("TOP", s, "TOPLEFT", w / 2, -y)
        fs:SetWidth(width)
        fs:SetJustifyH("CENTER")
        if color then
            fs:SetTextColor(color[1], color[2], color[3])
        end
        return fs
    end

    -- the title on the plate of the panel art
    s.title = text("GameFontNormal", 36, 160)
    s.title:SetText("Card Crafting")
    if textured then
        s.title:SetTextColor(CU.INK[1], CU.INK[2], CU.INK[3])
        s.title:SetShadowColor(0, 0, 0, 0)
    end
    s.help = text("GameFontNormal", 94, 176, CU.LIGHT)
    s.help:SetText(CU.HELP)

    -- Owned / Missing: the ownership filter of the book
    s.heading = text("GameFontNormal", 248, 176)
    s.heading:SetText("Show cards")
    local function check(name, y, label, which)
        local b = CreateFrame("CheckButton", name, s, "UICheckButtonTemplate")
        b:SetWidth(26)
        b:SetHeight(26)
        b:SetPoint("TOPLEFT", s, "TOPLEFT", 74, -y)
        local fs = getglobal(name .. "Text")
        if fs then
            fs:SetText(label)
            fs:SetTextColor(1, 1, 1)
        end
        b:SetScript("OnClick", function()
            CU.OnCheckClick(which)
        end)
        return b
    end
    s.ownedBox = check("HearthstoneCraftOwned", 272, "Owned", "owned")
    s.missingBox = check("HearthstoneCraftMissing", 304, "Missing", "missing")

    -- what the button does
    s.message = text("GameFontHighlightSmall", 420, 176)
    s.summary = s:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    s.summary:SetPoint("BOTTOM", s, "TOPLEFT", w / 2, -528)
    s.summary:SetWidth(176)
    s.summary:SetJustifyH("CENTER")
    s.extras = text("GameFontNormal", 538, 176)
    s.explain = text("GameFontNormalSmall", 574, 176, CU.LIGHT)

    -- the button with the dust bottle (and the number on it)
    local level = s:GetFrameLevel() + 2
    local mk = UI and UI.MakeButton
    local b
    if mk then
        b = mk("HearthstoneCraftExtrasButton", s, CU.BTN_W, CU.BTN_H, "Disenchant\nExtra Cards", level)
        if b.label then
            b.label:ClearAllPoints()
            b.label:SetPoint("CENTER", b, "LEFT", 70, 1)
            b.label:SetWidth(124)
            b.label:SetJustifyH("CENTER")
        end
    else
        b = CreateFrame("Button", "HearthstoneCraftExtrasButton", s, "UIPanelButtonTemplate")
        b:SetWidth(CU.BTN_W)
        b:SetHeight(24)
    end
    b:SetPoint("TOPLEFT", s, "TOPLEFT", DU and DU.TILE_X or 33, -700)
    b:SetScript("OnClick", function()
        CU.OnExtrasClick()
    end)
    local bottle = b:CreateTexture(nil, "OVERLAY")
    if UI and UI.SetBottle then
        UI.SetBottle(bottle, CU.BOTTLE_H)
    end
    bottle:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", 8, -4)
    local dust = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    dust:SetFont(CU.NUMBER_FONT, 15, "OUTLINE")
    dust:SetTextColor(1, 1, 1)
    dust:SetPoint("CENTER", bottle, "CENTER", 0, -14)
    s.button = b
    s.bottle = bottle
    s.dust = dust

    s:Hide()
    CU.side = s
    return s
end
