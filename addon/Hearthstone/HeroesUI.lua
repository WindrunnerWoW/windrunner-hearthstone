-- Hearthstone addon: the Heroes tab of the collection window (reward track).
--
-- Built into the window of CollectionUI.lua (bottom tab "Heroes", hidden until the account has an unlock or an unspent
-- Legend #1 credit) as one page below the window header, on the Heroes parchment. A row of class
-- buttons chooses the class; under it the portraits of that class stand in golden arches (ArenaUI's MakeHero /
-- SetHeroArt): the default first, then the alternatives, locked ones dimmed.
--
--   click an owned portrait     selects it (HERO_SELECT; the default is always owned)
--   click a locked portrait     with a credit: a confirm box, then HERO_UNLOCK; without one: "Reach Legend #1 ..."
--
-- Cosmetic only: the hero power, health and rules of the class never change. A portrait whose texture is not installed
-- shows the class colour with "art not installed"; it can still be selected (the default shows in play until the
-- texture is installed). The model is Heroes.lua, the requests are H.HeroSelect / H.HeroUnlock in Hearthstone.lua.
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...', no '%'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local E = HsHeroes
H.HeroesUI = H.HeroesUI or {}
local HU = H.HeroesUI

HU.PANEL_W, HU.PANEL_H = 1024, 704      -- the size of the collection book, under the class tabs
HU.PER_ROW, HU.ROWS = 4, 2
HU.PER_PAGE = HU.PER_ROW * HU.ROWS    -- portraits per page; a class with more (up to 10) gets a second page
HU.CELL_H = 180                       -- height of an arch in screen pixels
HU.GRID_X, HU.GRID_Y, HU.ROW_PITCH = 72, 134, 236
HU.GRID_W = 868                       -- the parchment between the stone edges
HU.PAGER_Y = 668                      -- the page buttons and the page number (the collection's FOOT_Y)
HU.EDGE_ARROW_W, HU.EDGE_ARROW_H = 64, 32   -- the hover arrow in the page margins (the collection's size)
HU.page = 1
HU.CLASS_LABELS = { DRUID = "Druid", HUNTER = "Hunter", MAGE = "Mage", PALADIN = "Paladin", PRIEST = "Priest",
    ROGUE = "Rogue", SHAMAN = "Shaman", WARLOCK = "Warlock", WARRIOR = "Warrior" }
HU.class = nil                        -- the class whose portraits are shown
HU.confirmId = nil                    -- the locked portrait the confirm box asks about

local WHITE = "Interface\\Buttons\\WHITE8X8"

local function ui()
    return H.CollectionUI
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- The class of the player, as a key of the catalog (MAGE ...), or nil.
local function playerClass()
    if UnitClass then
        local _, token = UnitClass("player")
        if token and E.Default(token) then
            return token
        end
    end
    return nil
end

-- ----------------------------------------------------------- model helpers
-- The portrait of the class that is in use (the selected one, else the default).
function HU.CurrentId(class)
    return E.sel[class] or E.Default(class)
end

-- "selected", "owned", "locked" or "unlockable" (locked, and a credit is waiting) for a list entry of E.List.
function HU.CellState(class, p)
    if p.id == HU.CurrentId(class) then
        return "selected"
    elseif p.owned then
        return "owned"
    elseif E.credits > 0 then
        return "unlockable"
    end
    return "locked"
end

HU.STATE_TEXT = {
    selected = "|cff0b5a12Selected|r",
    owned = "|cff2a1608Click to select|r",
    unlockable = "|cff7a1a00Click to unlock|r",
    locked = "|cff4a3a2aLocked|r",
}

function HU.BannerText()
    if E.credits > 0 then
        return "Legend #1: choose a portrait to unlock (" .. E.credits .. ")"
    end
    return "Choose the portrait of your heroes. Reach Legend #1 to unlock more."
end

-- ----------------------------------------------------------------- frames
local function makeBackground(panel)
    local UI = ui()
    local PU = H.PacksUI
    local inset = PU and PU.BG_INSET or 0
    if UI.HEROES_BG_TILES and UI.PlaceTiles and UI.PlaceTiles(panel, "heroes_bg", UI.HEROES_BG_TILES, inset, 0, "BACKGROUND") then
        return true
    end
    local bg = panel:CreateTexture(nil, "BACKGROUND")
    bg:SetTexture(WHITE)
    bg:SetVertexColor(0.12, 0.09, 0.07, 1)
    bg:SetPoint("TOPLEFT", panel, "TOPLEFT", inset, 0)
    bg:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -inset, inset)
    return false
end

local function showCellTip(cell)
    local p = cell.entry
    if not p or not GameTooltip then
        return
    end
    GameTooltip:SetOwner(cell.button, "ANCHOR_RIGHT")
    GameTooltip:SetText(p.name or p.id)
    local state = HU.CellState(HU.class, p)
    if state == "locked" then
        GameTooltip:AddLine(E.LOCKED_TEXT, 1, 0.82, 0.2, 1)
    elseif state == "unlockable" then
        GameTooltip:AddLine("Click to unlock this portrait with your Legend #1 credit.", 1, 0.82, 0.2, 1)
    elseif state == "owned" then
        GameTooltip:AddLine("Click to use this portrait.", 1, 1, 1, 1)
    end
    if not p.installed then
        GameTooltip:AddLine("Art not installed.", 1, 0.3, 0.3, 1)
    end
    GameTooltip:Show()
end

local function makeCell(panel, level, index)
    local AU = H.ArenaUI
    local row = math.floor((index - 1) / HU.PER_ROW)
    local col = index - 1 - row * HU.PER_ROW
    local cell = { index = index }
    local archH = HU.CELL_H / AU.K                       -- ArenaUI works in pixels of its background
    local archW = HU.CELL_H * AU.ART.ArenaArch.aspect
    local pitch = HU.GRID_W / HU.PER_ROW
    local x = HU.GRID_X + col * pitch + (pitch - archW) / 2
    local y = HU.GRID_Y + row * HU.ROW_PITCH
    local b = CreateFrame("Button", "HearthstoneHeroCell" .. index, panel)
    b:SetFrameLevel(level + 2)
    b:SetWidth(archW)
    b:SetHeight(HU.CELL_H)
    b:SetPoint("TOPLEFT", panel, "TOPLEFT", x, -y)
    cell.button = b
    cell.hero = AU.MakeHero(b, panel, x / AU.K, y / AU.K, archH)
    -- the hover and the portrait in use glow the golden frame only: a tint over the portrait (the strips overlap
    -- where they bleed under the frame, and the additive tints stack there into a bright spot)
    AU.ArchGlow(b, panel, "HIGHLIGHT", x / AU.K, y / AU.K, archH, 1, 0.9, 0.6, 0, 0.55)
    cell.pick = CreateFrame("Frame", nil, b)              -- the portrait in use: a gold glow of the arch
    cell.pick:SetAllPoints(b)
    AU.ArchGlow(cell.pick, panel, "OVERLAY", x / AU.K, y / AU.K, archH, 1, 0.82, 0.2, 0, 0.7)
    cell.pick:Hide()
    cell.name = panel:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    cell.name:SetPoint("TOP", b, "BOTTOM", 0, -6)
    cell.name:SetWidth(archW + 24)
    cell.name:SetTextColor(0.16, 0.08, 0.02)
    cell.name:SetShadowColor(0, 0, 0, 0)               -- the font's black shadow doubles dark ink on parchment
    cell.status = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    cell.status:SetPoint("TOP", cell.name, "BOTTOM", 0, -2)
    cell.status:SetWidth(archW + 24)
    cell.status:SetTextColor(0.16, 0.08, 0.02)
    cell.status:SetShadowColor(0, 0, 0, 0)
    b:SetScript("OnClick", function()
        HU.OnCellClick(index)
    end)
    b:SetScript("OnEnter", function()
        showCellTip(cell)
    end)
    b:SetScript("OnLeave", hideTip)
    b:Hide()
    return cell
end

-- The confirm box over the page: "Unlock <name>?" with Unlock and Cancel.
local function makeConfirm(panel, level)
    local UI = ui()
    local c = CreateFrame("Frame", "HearthstoneHeroConfirm", panel)
    c:SetFrameLevel(level + 20)
    c:SetWidth(420)
    c:SetHeight(150)
    c:SetPoint("CENTER", panel, "CENTER", 0, 40)
    c:EnableMouse(true)
    c:SetBackdrop({ bgFile = WHITE, edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = false, edgeSize = 14,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    c:SetBackdropColor(0.05, 0.04, 0.03, 0.96)
    c:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    c.text = c:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    c.text:SetPoint("TOP", c, "TOP", 0, -24)
    c.text:SetWidth(380)
    c.yes = UI.MakeButton("HearthstoneHeroConfirmYes", c, 130, 30, "Unlock", level + 22)
    c.yes:SetPoint("BOTTOMLEFT", c, "BOTTOMLEFT", 50, 18)
    c.yes:SetScript("OnClick", function()
        HU.OnConfirm(true)
    end)
    c.no = UI.MakeButton("HearthstoneHeroConfirmNo", c, 130, 30, "Cancel", level + 22)
    c.no:SetPoint("BOTTOMRIGHT", c, "BOTTOMRIGHT", -50, 18)
    c.no:SetScript("OnClick", function()
        HU.OnConfirm(false)
    end)
    c:Hide()
    return c
end

-- Page turning as in the collection book: the margins of the parchment turn the page (a click, and the golden arrow
-- shows on hover), spellbook buttons either side of "Page 1 / 2" under the grid, and the mouse wheel.
function HU.makePager(p, level)
    local UI = ui()
    p:EnableMouseWheel(true)
    p:SetScript("OnMouseWheel", function()
        HU.ChangePage(arg1 > 0 and -1 or 1)
    end)
    local function pageButton(kind, dir)
        local b = CreateFrame("Button", nil, p)
        b:SetFrameLevel(level + 3)
        b:SetWidth(32)
        b:SetHeight(32)
        b:SetNormalTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Up")
        b:SetPushedTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Down")
        b:SetDisabledTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Disabled")
        b:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
        b:SetPoint("CENTER", p, "TOPLEFT", HU.PANEL_W / 2 + dir * 62, -HU.PAGER_Y)
        b:SetScript("OnClick", function()
            HU.ChangePage(dir)
        end)
        return b
    end
    p.prevPage = pageButton("PrevPage", -1)
    p.nextPage = pageButton("NextPage", 1)
    p.pageText = p:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    p.pageText:SetPoint("CENTER", p, "TOPLEFT", HU.PANEL_W / 2, -HU.PAGER_Y)
    p.pageText:SetWidth(90)
    p.pageText:SetTextColor(0.16, 0.08, 0.02)
    p.pageText:SetShadowColor(0, 0, 0, 0)

    p.edgeZones = {}
    local function makeEdgeZone(dir, x1, x2)
        local z = CreateFrame("Button", nil, p)
        z:SetFrameLevel(level + 3)
        z:SetWidth(x2 - x1)
        z:SetHeight(HU.PANEL_H - 80)
        z:SetPoint("TOPLEFT", p, "TOPLEFT", x1, -40)
        z:EnableMouseWheel(true)
        z:SetScript("OnMouseWheel", function()
            HU.ChangePage(arg1 > 0 and -1 or 1)
        end)
        local arrow = z:CreateTexture(nil, "OVERLAY")
        arrow:SetWidth(HU.EDGE_ARROW_W)
        arrow:SetHeight(HU.EDGE_ARROW_H)
        arrow:SetPoint("CENTER", z, "CENTER", 0, 0)
        if not UI.SetPiece(arrow, "page_arrow") then
            arrow:SetTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. (dir > 0 and "NextPage" or "PrevPage") .. "-Up")
        elseif dir < 0 then
            arrow:SetTexCoord(1, 0, 0, 1)
        end
        arrow:Hide()
        z.arrow = arrow
        z.dir = dir
        z.Refresh = function(self)
            local target = HU.page + self.dir
            if self.hover and target >= 1 and target <= HU.PageCount() then
                self.arrow:Show()
            else
                self.arrow:Hide()
            end
        end
        z:SetScript("OnEnter", function()
            this.hover = true
            this:Refresh()
        end)
        z:SetScript("OnLeave", function()
            this.hover = false
            this:Refresh()
        end)
        z:SetScript("OnClick", function()
            HU.ChangePage(this.dir)
            this:Refresh()
        end)
        z:SetScript("OnHide", function()
            this.hover = false
            this.arrow:Hide()
        end)
        p.edgeZones[dir] = z
        return z
    end
    makeEdgeZone(-1, 30, 96)
    makeEdgeZone(1, 928, 986)
end

function HU.PageCount()
    local n = table.getn(E.List(HU.class or "MAGE"))
    return math.max(1, math.ceil(n / HU.PER_PAGE))
end

function HU.ChangePage(delta)
    local page = HU.page + delta
    if page < 1 or page > HU.PageCount() then
        return
    end
    HU.page = page
    HU.confirmId = nil
    hideTip()
    HU.Refresh()
end

-- Builds the panel into the collection window (called once from CollectionUI's UI.Build): the panel is the whole
-- window below the header, 'level' is its frame level (above the book art).
function HU.Build(main, x, y, level)
    local UI = ui()
    local p = CreateFrame("Frame", "HearthstoneHeroesPanel", main)
    if level then
        p:SetFrameLevel(level)
    end
    level = p:GetFrameLevel()
    p:SetWidth(HU.PANEL_W)
    p:SetHeight(HU.PANEL_H)
    p:SetPoint("TOPLEFT", main, "TOPLEFT", x, y)
    p:EnableMouse(true)
    p:Hide()
    HU.panel = p
    p.hasArt = makeBackground(p)

    p.banner = p:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    p.banner:SetPoint("CENTER", p, "TOPLEFT", HU.PANEL_W / 2, -104)     -- below the ribbon of the parchment
    p.banner:SetWidth(900)
    p.banner:SetTextColor(0.25, 0.14, 0.05)
    p.banner:SetShadowColor(0, 0, 0, 0)
    p.message = p:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    p.message:SetPoint("CENTER", p, "TOPLEFT", HU.PANEL_W / 2, -120)
    p.message:SetWidth(900)
    p.message:SetTextColor(0.55, 0.1, 0.05)
    p.message:SetShadowColor(0, 0, 0, 0)

    HU.cells = {}
    local AU = H.ArenaUI
    if AU and AU.MakeHero and AU.ART and AU.ART.ArenaArch and AU.K then
        for i = 1, HU.PER_PAGE do
            HU.cells[i] = makeCell(p, level, i)
        end
    end
    HU.makePager(p, level)
    p.confirm = makeConfirm(p, level)
    return p
end

-- ---------------------------------------------------------------- refresh
function HU.SetClass(class)
    HU.class = class
    HU.page = 1
    HU.confirmId = nil
    hideTip()
    HU.Refresh()
end

local function dimCell(cell, dim)
    local v = dim and 0.4 or 1
    for i = 1, table.getn(cell.hero.strips) do
        cell.hero.strips[i]:SetVertexColor(v, v, v)
    end
    if cell.hero.frame.SetVertexColor then
        cell.hero.frame:SetVertexColor(v, v, v)
    end
end

function HU.Refresh()
    local p = HU.panel
    if not p then
        return
    end
    if not HU.class or not E.Default(HU.class) then
        HU.class = playerClass() or "MAGE"
    end
    p.banner:SetText(HU.BannerText())
    p.message:SetText(E.message or "")
    local AU = H.ArenaUI
    local list = E.List(HU.class)
    local pages = HU.PageCount()
    if HU.page > pages then
        HU.page = pages
    end
    local first = (HU.page - 1) * HU.PER_PAGE
    if p.pageText then
        p.pageText:SetText("Page " .. HU.page .. " / " .. pages)
        if HU.page <= 1 then p.prevPage:Disable() else p.prevPage:Enable() end
        if HU.page >= pages then p.nextPage:Disable() else p.nextPage:Enable() end
        p.edgeZones[-1]:Refresh()
        p.edgeZones[1]:Refresh()
    end
    for i = 1, table.getn(HU.cells) do
        local cell = HU.cells[i]
        local entry = list[first + i]
        cell.entry = entry
        if entry then
            local state = HU.CellState(HU.class, entry)
            AU.SetHeroArt(cell.hero, HU.class, entry.id)
            dimCell(cell, state == "locked" or state == "unlockable")
            if state == "selected" then
                cell.pick:Show()
            else
                cell.pick:Hide()
            end
            cell.name:SetText(entry.name or entry.id)
            local status = HU.STATE_TEXT[state]
            if not entry.installed then
                status = status .. "  |cffff5555art not installed|r"
            end
            cell.status:SetText(status)
            cell.button:Show()
            cell.name:Show()
            cell.status:Show()
        else
            AU.HideHero(cell.hero)
            cell.pick:Hide()
            cell.button:Hide()
            cell.name:Hide()
            cell.status:Hide()
        end
    end
    local UI = ui()
    if UI and UI.RefreshTabs and UI.state and UI.state.bottom == UI.HEROES_TAB then
        UI.RefreshTabs()
    end
    local c = p.confirm
    if HU.confirmId and E.UnlockBlocker(HU.confirmId) == nil then
        local _, entry = E.Find(HU.confirmId)
        c.text:SetText("Unlock " .. (entry and entry.name or HU.confirmId) .. "?\nThis spends one Legend #1 credit.")
        c:Show()
    else
        HU.confirmId = nil
        c:Hide()
    end
end

function HU.OnTabShown()
    HU.class = nil
    HU.page = 1
    HU.confirmId = nil
    if H.HeroGet and E.state ~= "ready" then
        H.HeroGet()
    end
    HU.Refresh()
end

function HU.ClearHover()
    hideTip()
    HU.confirmId = nil
    if HU.panel and HU.panel.confirm then
        HU.panel.confirm:Hide()
    end
end

-- ------------------------------------------------------------------ clicks
function HU.OnCellClick(index)
    local cell = HU.cells[index]
    local entry = cell and cell.entry
    if not entry or E.IsBusy() then
        return
    end
    hideTip()
    local state = HU.CellState(HU.class, entry)
    if state == "selected" then
        return
    elseif state == "owned" then
        local _, why = H.HeroSelect(HU.class, entry.id)
        if why then
            E.message = why
        end
    elseif state == "unlockable" then
        HU.confirmId = entry.id
    else
        E.message = E.LOCKED_TEXT                       -- a locked click sends nothing
    end
    HU.Refresh()
end

function HU.OnConfirm(yes)
    local id = HU.confirmId
    HU.confirmId = nil
    if yes and id then
        local _, why = H.HeroUnlock(id)
        if why then
            E.message = why
        end
    end
    HU.Refresh()
end

-- ------------------------------------------------------------------- hooks
-- Opens the collection window on the Heroes tab (/hs heroes).
function H.ShowHeroes()
    local UI = ui()
    if not UI or not UI.EnsureBuilt() then
        return false
    end
    if not UI.HeroesTabVisible() then
        H.Print(E.LOCKED_TEXT)
        return false
    end
    UI.SetBottomTab(UI.HEROES_TAB)
    UI.frame:Show()
    UI.RefreshAll()
    return true
end

-- Heroes.lua state changed (a reply, a push, the op in flight): the tab appears or goes, the page redraws.
function H.OnHeroesChanged()
    local UI = ui()
    if not UI or not UI.frame then
        return
    end
    if UI.state.bottom == UI.HEROES_TAB and not UI.HeroesTabVisible() then
        UI.SetBottomTab(1)
        return
    end
    UI.StyleBottomTabs(UI.state.bottom)
    if UI.state.bottom == UI.HEROES_TAB and HU.panel and HU.panel:IsShown() then
        HU.Refresh()
    elseif UI.state.bottom == 1 and UI.frame:IsShown() and UI.frame.sidePanel and H.DecksUI then
        H.DecksUI.Refresh()               -- the deck tiles show the selected portraits
    end
end
