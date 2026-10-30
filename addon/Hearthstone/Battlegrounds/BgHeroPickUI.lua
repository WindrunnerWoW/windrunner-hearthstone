-- Hearthstone addon: the Warsong Battlegrounds hero-pick panel.
--
-- Shown by BgShopUI.lua while the state is in the pick phase (BG.Picking, `ph=p`), laid out like the Hearthstone
-- Battlegrounds "Choose a Hero" screen: a ribbon title, the minion types of this game, the heroes the server offered
-- (`hc`) as arch portraits in a gold frame with their armor in a shield and their name below, and a Confirm button.
-- A click on a hero selects it (hero power in a tooltip); Confirm sends BG_ACT act=hero;i=<choice>. The shop behind
-- stays hidden until the pick is done. The arch is the normal Hearthstone one (the arena's golden UI\ArenaArch, ArenaUI.lua
-- AU.ART.ArenaArch) with the hero art shown through its strips; without it a tile falls back to the square portrait or,
-- without any hero art, to its tinted backdrop with the name and power text.
-- The other art is the Hearthstone matches' own: the choose_banner scroll (CollectionUI.Piece, the match chooser's
-- ribbon, its words are in the art), the icy btn_choose plate as the Confirm button and the steel gem_armor shield
-- (MatchUI.SetPiece). Each has a drawn fallback when it is not installed.
-- Reads HsBg.model only. Lua 5.0 / WoW 1.12.

Hearthstone = Hearthstone or {}
local H = Hearthstone
HsBgHeroPickUI = HsBgHeroPickUI or {}
local P = HsBgHeroPickUI
local BG = HsBg

H.BgUI = H.BgUI or {}
local UI = H.BgUI

P.CELL_W, P.CELL_H = 162, 180           -- the arch (ArenaArch aspect 0.9)
P.NAME_H = 56                           -- the name and hero power lines under it
P.GAP = 36
P.MAX = 4
P.ARMOR_SIZE = 26                       -- the armor number in the shield (it fills the shield like the match's gem)
P.CARD_W, P.CARD_H = 256, 388           -- the hero power card of the hover (the card part of its 256x512 texture)
P.CARD_SCALE = 0.7                      -- the hover card is shown at this fraction of its 256x388
P.CARD_PATH = "Interface\\Hearthstone\\Textures\\BgHeroes\\PowerCards\\"
P.frame = nil
P.tiles = {}
P.selected = nil                        -- the hero id picked up (Confirm sends it)

local TOP = 190                         -- the tile row's distance from the panel's top

local function fs(parent, font, justify)
    local s = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormal")
    s:SetJustifyH(justify or "LEFT")
    return s
end

-- "Minion types this game: Beasts, Demons, ..." from the lobby's tribes (BgProtocol TRIBES).
local function tribesLine(m)
    local names = {}
    local all = HsBgProtocol and HsBgProtocol.TRIBES or {}
    for i = 1, table.getn(m.tribes or {}) do
        local n = all[m.tribes[i]]
        if n then
            table.insert(names, n == "Quilboar" and n or (n .. "s"))
        end
    end
    if table.getn(names) == 0 then
        return ""
    end
    local text = "Minion types this game: " .. table.concat(names, ", ")
    -- The other faction rolls its own set too.
    local enemyNames = {}
    for i = 1, table.getn(m.enemyTribes or {}) do
        local n = all[m.enemyTribes[i]]
        if n then
            table.insert(enemyNames, n == "Quilboar" and n or (n .. "s"))
        end
    end
    if table.getn(enemyNames) > 0 then
        text = text .. "\nThe other faction plays: " .. table.concat(enemyNames, ", ")
    end
    return text
end

local function archArt()
    local AU = H.ArenaUI
    return AU and AU.ART and AU.ART.ArenaArch, AU and AU.TEXTURE_PATH
end

local function collectionPiece(name)
    local UIc = H.CollectionUI
    return UIc and UIc.Piece and UIc.Piece(name)
end

local function plate(b)
    b:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 10,
        insets = { left = 2, right = 2, top = 2, bottom = 2 } })
    b:SetBackdropColor(0.16, 0.16, 0.18, 0.95)
    b:SetBackdropBorderColor(0.75, 0.75, 0.8, 1)
end

function P.Create(parent)
    if P.frame or not parent then
        return P.frame
    end
    local f = CreateFrame("Frame", "HearthstoneBgPick", parent)
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, 0)       -- a dimming layer over the whole window
    f:SetWidth(parent:GetWidth())
    f:SetHeight(parent:GetHeight())
    f:SetFrameLevel(parent:GetFrameLevel() + (HsBgUI and HsBgUI.OVERLAY_BASE or 0) + 50)
    f:EnableMouse(true)
    f:SetBackdrop({ bgFile = UI.WHITE })
    f:SetBackdropColor(0, 0, 0, 0.85)
    P.frame = f
    -- the title: the match chooser's scroll (its words are part of the art); without it a drawn gold plate
    local banner = collectionPiece("choose_banner")
    f.ribbon = CreateFrame("Frame", nil, f)
    f.ribbon:SetPoint("TOP", f, "TOP", 0, -12)
    f.title = fs(f.ribbon, "GameFontNormalLarge", "CENTER")
    f.title:SetPoint("CENTER", f.ribbon, "CENTER", 0, 0)
    f.title:SetWidth(380)
    f.title:SetText("|cffffffffChoose a Hero|r")
    if banner then
        f.ribbon:SetWidth(340)
        f.ribbon:SetHeight(170)
        f.ribbon:ClearAllPoints()
        f.ribbon:SetPoint("TOP", f, "TOP", 0, 6)
        f.ribbon.tex = f.ribbon:CreateTexture(nil, "ARTWORK")
        f.ribbon.tex:SetTexture(banner.path)
        f.ribbon.tex:SetTexCoord(0, banner.right, 0, banner.bottom)
        f.ribbon.tex:SetAllPoints(f.ribbon)
        f.title:Hide()
    else
        f.ribbon:SetWidth(460)
        f.ribbon:SetHeight(58)
        f.ribbon:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 } })
        f.ribbon:SetBackdropColor(0.62, 0.45, 0.16, 1)
        f.ribbon:SetBackdropBorderColor(0.95, 0.8, 0.4, 1)
    end
    f.hint = fs(f, "GameFontNormalSmall", "CENTER")
    f.hint:SetPoint("TOP", f, "TOP", 0, -140)
    f.hint:SetWidth(700)
    f.confirm = CreateFrame("Button", "HearthstoneBgPickConfirm", f)
    f.confirm:SetWidth(120)
    f.confirm:SetHeight(46)
    f.confirm:SetPoint("BOTTOM", f, "BOTTOM", 0, 70)
    f.confirm:SetFrameLevel(f:GetFrameLevel() + 5)
    f.confirm.text = fs(f.confirm, "GameFontNormal", "CENTER")
    f.confirm.text:SetPoint("CENTER", f.confirm, "CENTER", 0, 0)
    f.confirm.text:SetText("Confirm")
    local plateArt = collectionPiece("btn_choose")             -- the icy Choose plate of the matches
    if plateArt then
        f.confirm.art = f.confirm:CreateTexture(nil, "BACKGROUND")
        f.confirm.art:SetTexture(plateArt.path)
        f.confirm.art:SetTexCoord(0, plateArt.right, 0, plateArt.bottom)
        f.confirm.art:SetWidth(170)
        f.confirm.art:SetHeight(85)
        f.confirm.art:SetPoint("CENTER", f.confirm, "CENTER", 0, 0)
        f.confirm.text:Hide()
    else
        plate(f.confirm)
    end
    f.confirm:RegisterForClicks("LeftButtonUp")
    f.confirm:SetScript("OnClick", function() P.Confirm() end)
    f:Hide()
    return f
end

-- True when Textures\BgHeroes\PowerCards\<id>.blp is installed (HS_BgTextures.powerCards).
local function powerCardInstalled(id)
    local list = HS_BgTextures and HS_BgTextures.powerCards
    if not id or type(list) ~= "table" then
        return false
    end
    for i = 1, table.getn(list) do
        if list[i] == id then
            return true
        end
    end
    return false
end

-- The hovered hero's power as the large card (like the hero power of a normal match), beside the tile.
local function powerCard()
    if not P.card then
        local f = CreateFrame("Frame", "HearthstoneBgPickPowerCard", UIParent)
        f:SetWidth(P.CARD_W)
        f:SetHeight(P.CARD_H)
        f:SetFrameStrata("TOOLTIP")
        f:SetClampedToScreen(true)
        f.tex = f:CreateTexture(nil, "ARTWORK")
        f.tex:SetAllPoints(f)
        f.tex:SetTexCoord(0, 1, 0, P.CARD_H / 512)
        f:Hide()
        P.card = f
    end
    return P.card
end

local function onEnter(t)
    if not t.hero then
        return
    end
    local hero = BG.Hero(t.hero)
    local power = hero and BG.HeroPower(hero.power) or nil
    if power and powerCardInstalled(hero.power) then
        local f = powerCard()
        f.tex:SetTexture(P.CARD_PATH .. hero.power)
        f:SetScale((P.frame and P.frame:GetScale() or 1) * P.CARD_SCALE)
        f:ClearAllPoints()
        local x, mid = t:GetCenter(), UIParent:GetCenter()
        if x and mid and x > mid then
            f:SetPoint("RIGHT", t, "LEFT", -6, 0)
        else
            f:SetPoint("LEFT", t, "RIGHT", 6, 0)
        end
        f:Show()
        return
    end
    if not GameTooltip then
        return
    end
    GameTooltip:SetOwner(t, "ANCHOR_RIGHT")
    GameTooltip:SetText(BG.HeroName(t.hero))
    if hero and hero.health then
        GameTooltip:AddLine(hero.health .. " Health" .. (hero.armor and (", armor tier " .. hero.armor) or ""),
            1, 1, 1)
    end
    if power then
        local cost = (power.cost and power.cost > 0) and (" (" .. power.cost .. ")") or ""
        GameTooltip:AddLine("|cffffd100" .. power.name .. cost .. "|r")
        if power.text and power.text ~= "" then
            GameTooltip:AddLine(power.text, 1, 1, 1)
        end
    end
    GameTooltip:Show()
end

local function onLeave()
    if P.card then
        P.card:Hide()
    end
    if GameTooltip then
        GameTooltip:Hide()
    end
end

local function makeTile(f, i)
    local b = CreateFrame("Button", "HearthstoneBgPickTile" .. i, f)
    b:SetWidth(P.CELL_W)
    b:SetHeight(P.CELL_H + P.NAME_H)
    b.back = b:CreateTexture(nil, "BACKGROUND")        -- the plain tile without any hero art
    b.back:SetTexture(UI.WHITE)
    b.back:SetVertexColor(0.10, 0.09, 0.07)
    b.back:SetWidth(P.CELL_W - 8)
    b.back:SetHeight(P.CELL_H - 8)
    b.back:SetPoint("TOP", b, "TOP", 0, -4)
    b.portrait = b:CreateTexture(nil, "ARTWORK")       -- the square portrait (no arch art)
    b.portrait:SetWidth(P.CELL_W - 20)
    b.portrait:SetHeight(P.CELL_W - 20)
    b.portrait:SetPoint("TOP", b, "TOP", 0, -10)
    b.portrait:Hide()
    b.frameTex = b:CreateTexture(nil, "OVERLAY")
    b.frameTex:SetTexture(UI.BG_PATH .. "hero_frame")
    b.frameTex:SetWidth(P.CELL_W - 8)
    b.frameTex:SetHeight(P.CELL_W - 8)
    b.frameTex:SetPoint("TOP", b, "TOP", 0, -4)
    b.frameTex:Hide()
    -- the arch: the hero art in strips (ArenaUI MakeHero) under the golden frame, a bright copy of it for the selection
    b.strips = {}
    local arch, path = archArt()
    if arch then
        local o = arch.opening
        local ax, ay = P.CELL_W * o[1], P.CELL_H * o[2]
        local aw, ah = P.CELL_W * (o[3] - o[1]), P.CELL_H * (o[4] - o[2])
        for k = 1, table.getn(arch.strips) do
            local st = arch.strips[k]
            local tex = b:CreateTexture(nil, "BORDER")
            tex:SetTexture(UI.WHITE)
            tex:SetPoint("TOPLEFT", b, "TOPLEFT", ax + aw * st[1], -(ay + ah * st[2]))
            tex:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", ax + aw * st[3], -(ay + ah * st[4]))
            tex:Hide()
            b.strips[k] = tex
        end
        b.arch = b:CreateTexture(nil, "ARTWORK")
        b.arch:SetTexture(path .. "ArenaArch")
        b.arch:SetTexCoord(0, arch.right, 0, arch.bottom)
        b.arch:SetWidth(P.CELL_W)
        b.arch:SetHeight(P.CELL_H)
        b.arch:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
        b.arch:Hide()
        -- the glow has its own frame above the tile: inside one layer the 1.12 draw order is whatever was shown last,
        -- so a re-shown arch would cover a glow of the same layer (Refresh runs on every update)
        b.glowLayer = CreateFrame("Frame", nil, b)
        b.glowLayer:SetAllPoints(b)
        b.glow = b.glowLayer:CreateTexture(nil, "ARTWORK")
        b.glow:SetTexture(path .. "ArenaArch")
        b.glow:SetTexCoord(0, arch.right, 0, arch.bottom)
        b.glow:SetBlendMode("ADD")
        b.glow:SetVertexColor(0.35, 0.65, 1)
        b.glow:SetWidth(P.CELL_W)
        b.glow:SetHeight(P.CELL_H)
        b.glow:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
        b.glow:Hide()
    else
        b.glow = b:CreateTexture(nil, "OVERLAY")
        b.glow:SetTexture(UI.BG_PATH .. "hero_glow")
        b.glow:SetWidth(P.CELL_W + 20)
        b.glow:SetHeight(P.CELL_W + 20)
        b.glow:SetPoint("CENTER", b, "TOP", 0, -P.CELL_W / 2)
        b.glow:SetVertexColor(0.35, 0.65, 1)
        b.glow:Hide()
    end
    -- the armor shield: the matches' steel gem_armor (MatchUI.SetPiece), else a drawn plate
    b.top = CreateFrame("Frame", nil, b)                  -- above the glow, so the additive glow never washes the shield out
    b.top:SetAllPoints(b)
    b.shield = b.top:CreateTexture(nil, "OVERLAY")
    local MU = H.MatchUI
    if not (MU and MU.SetPiece and MU.SetPiece(b.shield, "gem_armor")) then
        b.shield:SetTexture(UI.WHITE)
        b.shield:SetVertexColor(0.45, 0.46, 0.52)
    end
    b.shield:SetWidth(46)
    b.shield:SetHeight(54)
    b.shield:SetPoint("CENTER", b, "TOPRIGHT", -2, -(P.CELL_H - 62))
    b.armor = fs(b.top, "GameFontNormalLarge", "CENTER")
    b.armor:SetFont(UI.NUMBER_FONT or "Fonts\\FRIZQT__.TTF", P.ARMOR_SIZE, "OUTLINE")
    b.armor:SetPoint("CENTER", b.shield, "CENTER", 0, 1)
    b.name = fs(b, "GameFontNormal", "CENTER")
    b.name:SetPoint("TOP", b, "TOP", 0, -(P.CELL_H + 10))
    b.name:SetWidth(P.CELL_W + P.GAP - 6)
    b.name:SetHeight(18)
    b:RegisterForClicks("LeftButtonUp")
    b:SetScript("OnClick", function() P.Select(this.hero) end)
    b:SetScript("OnEnter", function() onEnter(this) end)
    b:SetScript("OnLeave", onLeave)
    return b
end

-- Picks a hero up (the glow marks it); Confirm then sends the choice.
function P.Select(hero)
    if not P.frame or not hero then
        return
    end
    P.selected = hero
    P.Refresh()
end

-- The Confirm button: sends the picked hero's index among the offered heroes.
function P.Confirm()
    local m = BG.model
    if not m or not P.selected then
        if Hearthstone and Hearthstone.Print then Hearthstone.Print("Select a hero first, then press Choose.") end
        return nil, "no hero selected"
    end
    for i = 1, table.getn(m.hc) do
        if m.hc[i] == P.selected then
            return BG.ChooseHero(i - 1)
        end
    end
    return nil, "not offered"
end

function P.Refresh()
    if not P.frame then
        return
    end
    local m = BG.model
    if not m or m.ph ~= "p" or table.getn(m.hc) == 0 then
        P.selected = nil
        P.frame:Hide()
        return
    end
    P.frame:Show()
    P.frame.confirm:SetFrameLevel(P.frame:GetFrameLevel() + 5)   -- above the dimming layer and its tiles
    local n = table.getn(m.hc)
    local found = false
    for i = 1, n do
        if m.hc[i] == P.selected then
            found = true
        end
    end
    if not found then
        P.selected = nil
    end
    P.frame.hint:SetText("|cffffffff" .. tribesLine(m) .. "|r")
    local span = n * P.CELL_W + (n - 1) * P.GAP
    local start = (P.frame:GetWidth() - span) / 2
    if start < 0 then
        start = 0
    end
    local lobby = HsBgLobbyUI
    for i = 1, n do
        local t = P.tiles[i]
        if not t then
            t = makeTile(P.frame, i)
            P.tiles[i] = t
        end
        t:SetFrameLevel(P.frame:GetFrameLevel() + 2)
        if t.glowLayer then
            t.glowLayer:SetFrameLevel(t:GetFrameLevel() + 1)
        end
        t.top:SetFrameLevel(t:GetFrameLevel() + 2)
        t.index = i - 1
        t.hero = m.hc[i]
        t:ClearAllPoints()
        t:SetPoint("TOPLEFT", P.frame, "TOPLEFT", start + (i - 1) * (P.CELL_W + P.GAP), -TOP)
        local hid = m.hc[i]
        t.back:Hide()
        t.portrait:Hide()
        t.frameTex:Hide()
        if t.arch then
            t.arch:Hide()
        end
        for k = 1, table.getn(t.strips) do
            t.strips[k]:Hide()
        end
        local archId = hid .. "_arch"
        if t.arch and lobby and lobby.HeroInstalled(archId) then
            local arch = archArt()
            local st0 = arch.strips[1]
            local x0, y0, x1, y1 = st0[1], st0[2], st0[3], st0[4]
            for k = 2, table.getn(arch.strips) do
                local st = arch.strips[k]
                if st[1] < x0 then x0 = st[1] end
                if st[2] < y0 then y0 = st[2] end
                if st[3] > x1 then x1 = st[3] end
                if st[4] > y1 then y1 = st[4] end
            end
            for k = 1, table.getn(t.strips) do
                local st = arch.strips[k]
                local tex = t.strips[k]
                tex:SetTexture(UI.HERO_PATH .. archId)
                tex:SetTexCoord((st[1] - x0) / (x1 - x0), (st[3] - x0) / (x1 - x0),
                    (st[2] - y0) / (y1 - y0), (st[4] - y0) / (y1 - y0))
                tex:SetVertexColor(1, 1, 1)
                tex:Show()
            end
            t.arch:Show()
        elseif lobby and lobby.HeroInstalled(hid) then          -- no arch art: the square portrait in its ring
            t.portrait:SetTexture(UI.HERO_PATH .. hid)
            t.portrait:Show()
            t.frameTex:SetVertexColor(1, 0.82, 0.3)
            t.frameTex:Show()
        else
            t.back:Show()
        end
        local hero = BG.Hero(hid)
        local power = hero and BG.HeroPower(hero.power) or nil
        if hero and hero.armor and hero.armor > 0 then
            t.shield:Show()
            t.armor:SetText("|cffffffff" .. hero.armor .. "|r")
        else
            t.shield:Hide()
            t.armor:SetText("")
        end
        t.name:SetText("|cffffffff" .. BG.HeroName(hid) .. "|r")
        if hid == P.selected then
            t.glow:Show()
        else
            t.glow:Hide()
        end
        t:Show()
    end
    for i = n + 1, table.getn(P.tiles) do
        P.tiles[i]:Hide()
    end
    local confirm = P.frame.confirm
    if P.selected then
        confirm:EnableMouse(true)
        confirm:SetAlpha(1)
        if confirm.art then
            confirm.art:SetVertexColor(1, 1, 1)
        else
            confirm.text:SetText("|cffffffffConfirm|r")
        end
    else
        confirm:EnableMouse(true)
        confirm:SetAlpha(0.8)
        if confirm.art then
            confirm.art:SetVertexColor(0.55, 0.55, 0.6)
        else
            confirm.text:SetText("|cffaaaaaaConfirm|r")
        end
    end
end

function P.Hide()
    P.selected = nil
    if P.card then
        P.card:Hide()
    end
    if P.frame then
        P.frame:Hide()
    end
end

function P.IsShown()
    return P.frame ~= nil and P.frame:IsShown()
end
