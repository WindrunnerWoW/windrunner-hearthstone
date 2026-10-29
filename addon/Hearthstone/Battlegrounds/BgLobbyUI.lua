-- Hearthstone addon: the Warsong Battlegrounds lobby panel.
--
-- The eight seats of the game by faction (players 0-3 faction A, 4-7 faction B; HsBgGame.h), the human's faction first.
-- Each row shows the seat label, the health and the state (tier, or the round the player was eliminated in); the
-- human's row and the seat the human fought last are marked. Reads HsBg.model only; built by BgShopUI.lua.
-- With the board art (parent.artMode) the panel is the Hearthstone Battlegrounds opponent column instead: a portrait per
-- seat in a faction coloured frame (gold for you, blue allies, red foes) with a health bar under it, a red glow on the
-- seat the human fought last, a grey portrait once a seat is out; the details are a tooltip. The portrait is the
-- seat's real hero (`pl` hero id), the name is `nm` and the armor shows on the bar; a cell click while a hand card is
-- picked sends that minion (the fallback for the drag). Lua 5.0 / WoW 1.12.

Hearthstone = Hearthstone or {}
local H = Hearthstone
HsBgLobbyUI = HsBgLobbyUI or {}
local L = HsBgLobbyUI
local BG = HsBg

L.ROW_H = 54
L.ROW_GAP = 4
L.CELL = 50                            -- the portrait column: frame size, bar height and the pitch
L.BAR_H = 7
L.PITCH = 57
L.BG_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\Bg\\"
L.HERO_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\BgHeroes\\"
L.FRAME_COLOR = { me = { 1, 0.82, 0.3 }, ally = { 0.35, 0.62, 1 }, foe = { 0.85, 0.22, 0.18 } }
L.GLOW_COLOR = { 1, 0.15, 0.1 }

L.frame = nil
L.rows = {}

local function fs(parent, font, justify)
    local s = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormalSmall")
    s:SetJustifyH(justify or "LEFT")
    return s
end

-- True when Textures\BgHeroes\<key>.blp is installed (HS_BgTextures.heroes lists the keys).
function L.HeroInstalled(key)
    if not key or key == "" then
        return false
    end
    local list = HS_BgTextures and HS_BgTextures.heroes
    if type(list) ~= "table" then
        return false
    end
    for i = 1, table.getn(list) do
        if list[i] == key then
            return true
        end
    end
    return false
end

-- The seat whose lobby cell is under the cursor, or nil (the hit test is BgShopUI's UI.CursorIn).
function L.CellAt()
    local UI = H.BgUI
    if not UI or not UI.CursorIn then
        return nil
    end
    for i = 1, table.getn(L.rows) do
        local row = L.rows[i]
        if row.seat ~= nil and UI.CursorIn(row) then
            return row.seat
        end
    end
    return nil
end

-- A cell click: the fallback for sending (a hand card is picked up, then an ally cell is clicked). With nothing picked
-- up and no power armed, a click on a living ally opens its field (a right click switches to it; a second left click or
-- Escape closes it).
function L.OnCellClick(row, button)
    local UI = H.BgUI
    if not UI or not row.seat then
        return
    end
    if UI.powerArm == "s" then
        -- Lord Barov: the guessed player is the clicked seat (a living one; the server refuses the others)
        BG.UsePower(0, row.seat)
        UI.powerArm = nil
        if UI.Refresh then
            UI.Refresh()
        end
        return
    end
    if UI.sel and UI.sel.zone == "hand" then
        if BG.CanSend(row.seat) then
            BG.Send(UI.sel.index, row.seat)
            UI.sel = nil
            if UI.Refresh then
                UI.Refresh()
            end
        elseif BG.message == nil then
            BG.message = BG.IsLivingAlly(row.seat) and BG.WHY_TEXT.SendUsed or BG.WHY_TEXT.NotAlly
            if UI.Refresh then
                UI.Refresh()
            end
        end
        return
    end
    if BG.IsLivingAlly(row.seat) then
        -- a right click always switches to that ally's field; a left click on the open one closes it
        if BG.peekSeat == row.seat and button ~= "RightButton" then
            BG.ClosePeek()
        else
            BG.Peek(row.seat)
        end
        if UI.Refresh then
            UI.Refresh()
        end
    end
end

-- One cell of the portrait column.
local function makeCell(parent, i, x, y)
    local row = CreateFrame("Button", "HearthstoneBgLobby" .. i, parent)
    row:SetWidth(L.CELL)
    row:SetHeight(L.CELL + L.BAR_H)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", x, -y)
    row.glow = row:CreateTexture(nil, "BACKGROUND")
    row.glow:SetTexture(L.BG_PATH .. "hero_glow")
    row.glow:SetWidth(L.CELL * 1.6)
    row.glow:SetHeight(L.CELL * 1.6)
    row.glow:SetPoint("CENTER", row, "TOP", 0, -L.CELL / 2)
    row.glow:SetVertexColor(L.GLOW_COLOR[1], L.GLOW_COLOR[2], L.GLOW_COLOR[3])
    row.glow:Hide()
    -- the green halo while a hovered hand card would help this ally
    row.needGlow = row:CreateTexture(nil, "BACKGROUND")
    row.needGlow:SetTexture(L.BG_PATH .. "hero_glow")
    row.needGlow:SetWidth(L.CELL * 1.6)
    row.needGlow:SetHeight(L.CELL * 1.6)
    row.needGlow:SetPoint("CENTER", row, "TOP", 0, -L.CELL / 2)
    row.needGlow:SetVertexColor(0.35, 1, 0.45)
    row.needGlow:Hide()
    row.portrait = row:CreateTexture(nil, "ARTWORK")
    row.portrait:SetWidth(L.CELL - 4)
    row.portrait:SetHeight(L.CELL - 4)
    row.portrait:SetPoint("TOPLEFT", row, "TOPLEFT", 2, -2)
    -- the light over the portrait while the cursor is on it
    row.hl = row:CreateTexture(nil, "OVERLAY")
    row.hl:SetTexture("Interface\\Buttons\\WHITE8X8")
    row.hl:SetBlendMode("ADD")
    row.hl:SetVertexColor(1, 0.9, 0.6)
    row.hl:SetAlpha(0.22)
    row.hl:SetAllPoints(row.portrait)
    row.hl:Hide()
    row.unknown = row:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    row.unknown:SetPoint("CENTER", row, "TOP", 0, -L.CELL / 2)
    row.unknown:SetText("|cffffd100?|r")
    row.unknown:Hide()
    row.frameTex = row:CreateTexture(nil, "OVERLAY")
    row.frameTex:SetTexture(L.BG_PATH .. "hero_frame")
    row.frameTex:SetWidth(L.CELL)
    row.frameTex:SetHeight(L.CELL)
    row.frameTex:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0)
    row.barBack = row:CreateTexture(nil, "ARTWORK")
    row.barBack:SetTexture("Interface\\Buttons\\WHITE8X8")
    row.barBack:SetVertexColor(0.08, 0.02, 0.02)
    row.barBack:SetWidth(L.CELL - 6)
    row.barBack:SetHeight(L.BAR_H)
    row.barBack:SetPoint("TOP", row, "TOP", 0, -(L.CELL - 1))
    row.bar = row:CreateTexture(nil, "OVERLAY")
    row.bar:SetTexture("Interface\\Buttons\\WHITE8X8")
    row.bar:SetVertexColor(0.85, 0.12, 0.12)
    row.bar:SetHeight(L.BAR_H - 2)
    row.bar:SetPoint("TOPLEFT", row.barBack, "TOPLEFT", 1, -1)
    row.hpText = row:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    row.hpText:SetFont("Fonts\\FRIZQT__.TTF", 8, "OUTLINE")
    row.hpText:SetPoint("CENTER", row.barBack, "CENTER", 0, 0)
    -- the live standing: a small placing badge at the top right of the portrait (gold for the leader)
    row.rank = row:CreateFontString(nil, "OVERLAY")
    row.rank:SetFont("Fonts\\FRIZQT__.TTF", 9, "OUTLINE")
    row.rank:SetPoint("TOPRIGHT", row, "TOPRIGHT", -2, -2)
    row:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    row:SetScript("OnEnter", function()
        this.hl:Show()
        L.OnCellEnter(this)
    end)
    row:SetScript("OnLeave", function()
        this.hl:Hide()
        if GameTooltip then GameTooltip:Hide() end
    end)
    row:SetScript("OnClick", function() L.OnCellClick(this, arg1) end)
    return row
end

function L.OnCellEnter(row)
    if not GameTooltip or not row.seat then
        return
    end
    GameTooltip:SetOwner(row, "ANCHOR_RIGHT")
    GameTooltip:SetText((row.name or BG.SeatLabel(row.seat)))
    local hero = BG.SeatHero(row.seat)
    if hero ~= "" then
        GameTooltip:AddLine(BG.HeroName(hero), 1, 0.82, 0.2)
    end
    local quest = BG.SeatQuest(row.seat)
    if quest ~= "" then
        GameTooltip:AddLine("Quest: " .. BG.QuestName(quest), 1, 0.82, 0.2)
    end
    local reward = BG.SeatReward(row.seat)
    if reward ~= "" then
        GameTooltip:AddLine("Reward: " .. BG.RewardName(reward), 1, 0.82, 0.2)
    end
    GameTooltip:AddLine(row.health:GetText() .. " health", 1, 1, 1)
    GameTooltip:AddLine(row.state:GetText() or "", 0.8, 0.8, 0.8)
    local rank = BG.LivePlacement(row.seat)
    if rank > 0 then
        local s = BG.model and BG.model.lobby and BG.model.lobby[row.seat + 1]
        GameTooltip:AddLine((s and s.alive and "Standing: " or "Final standing: ") .. BG.PlaceLabel(rank),
            rank == 1 and 1 or 0.85, rank == 1 and 0.82 or 0.85, rank == 1 and 0.3 or 0.85)
    end
    if row.fought then
        GameTooltip:AddLine("You fought them last round.", 1, 0.4, 0.3)
    end
    local s = BG.model and BG.model.lobby and BG.model.lobby[row.seat + 1]
    if s and s.alive and s.opp == HsBgProtocol.RESTED then
        GameTooltip:AddLine("Rested last round (no opponent left for them).", 0.4, 1, 0.5)
    end
    if BG.IsLivingAlly(row.seat) and BG.CanSend(row.seat) then
        GameTooltip:AddLine("Drag a hand card here to send it.", 0.5, 1, 0.5)
    end
    GameTooltip:Show()
end

-- Creates the panel in `parent` at rect = { x, y, w, h } (window pixels from the top left).
function L.Create(parent, rect)
    if L.frame or not parent then
        return L.frame
    end
    local f = CreateFrame("Frame", "HearthstoneBgLobby", parent)
    f:SetWidth(rect[3])
    f:SetHeight(rect[4])
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", rect[1], -rect[2])
    L.frame = f
    L.art = parent.artMode and true or false
    f.header = fs(f, "GameFontNormal", "LEFT")
    f.header:SetPoint("TOPLEFT", f, "TOPLEFT", 2, 0)
    f.header:SetWidth(rect[3] - 4)
    f.header:SetText("|cffffd100The lobby|r")
    if L.art then
        f.header:Hide()
    end
    L.rows = {}
    for i = 1, 8 do
        local row
        if L.art then
            row = makeCell(f, i, 0, (i - 1) * L.PITCH)
        else
            row = CreateFrame("Button", "HearthstoneBgLobby" .. i, f)
            row:SetScript("OnClick", function() L.OnCellClick(this, arg1) end)
        end
        if not L.art then
            row:SetWidth(rect[3] - 4)
            row:SetHeight(L.ROW_H)
            row:SetPoint("TOPLEFT", f, "TOPLEFT", 2, -(18 + (i - 1) * (L.ROW_H + L.ROW_GAP)))
            row:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
                edgeSize = 8, insets = { left = 2, right = 2, top = 2, bottom = 2 } })
            row:SetBackdropColor(0.10, 0.09, 0.07, 0.85)
            row:SetBackdropBorderColor(0.30, 0.26, 0.18, 1)
        end
        -- the texts are the same in both looks; the portrait column keeps them for its tooltip only
        row.label = fs(row, "GameFontNormal", "LEFT")
        row.label:SetPoint("TOPLEFT", row, "TOPLEFT", 6, -4)
        row.label:SetWidth(rect[3] - 70)
        row.health = fs(row, "GameFontNormal", "RIGHT")
        row.health:SetPoint("TOPRIGHT", row, "TOPRIGHT", -6, -4)
        row.health:SetWidth(60)
        row.group = fs(row, "GameFontNormalSmall", "LEFT")
        row.group:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 6, 4)
        row.group:SetWidth(rect[3] - 90)
        row.state = fs(row, "GameFontNormalSmall", "RIGHT")
        row.state:SetPoint("BOTTOMRIGHT", row, "BOTTOMRIGHT", -6, 4)
        row.state:SetWidth(rect[3] - 12)
        if L.art then
            row.label:Hide()
            row.health:Hide()
            row.group:Hide()
            row.state:Hide()
        end
        L.rows[i] = row
    end
    return f
end

-- The live placing badge of a cell (the portrait column); the plain rows carry it in their state line instead.
function L.ShowRank(row, seat)
    if not row.rank then
        return
    end
    local rank = BG.LivePlacement(seat)
    if rank <= 0 then
        row.rank:SetText("")
        return
    end
    local c = rank == 1 and 1 or (rank == 2 and 0.82 or (rank == 3 and 0.72 or 0.7))
    local g = rank == 1 and 0.82 or (rank == 2 and 0.84 or (rank == 3 and 0.48 or 0.7))
    local b = rank == 1 and 0.3 or (rank == 2 and 0.88 or (rank == 3 and 0.24 or 0.7))
    row.rank:SetText(BG.PlaceLabel(rank))
    row.rank:SetTextColor(c, g, b)
end

-- The portrait column's look of one seat: the seat's real hero in a faction frame, the health and armor bar.
function L.RefreshCell(row, seat, s, m)
    row.seat = seat
    row.name = BG.SeatName(seat)
    local hero = s.hero or ""
    if hero ~= "" and L.HeroInstalled(hero) then
        row.portrait:SetTexture(L.HERO_PATH .. hero)
        row.portrait:SetTexCoord(0, 1, 0, 1)
    else
        row.portrait:SetTexture("Interface\\Buttons\\WHITE8X8")
        row.portrait:SetVertexColor(0.18, 0.16, 0.2)
    end
    row.unknown:Hide()
    if hero == "" and seat == m.me then
        row.unknown:Show()   -- my hero is not picked yet
    end
    local c = seat == m.me and L.FRAME_COLOR.me or (BG.SeatIsEnemy(seat) and L.FRAME_COLOR.foe or L.FRAME_COLOR.ally)
    row.frameTex:SetVertexColor(c[1], c[2], c[3])
    local v = s.alive and 1 or 0.35
    if hero ~= "" and L.HeroInstalled(hero) then
        row.portrait:SetVertexColor(v, v, v)
    end
    row.frameTex:SetAlpha(s.alive and 1 or 0.6)
    -- the bar is the health against the hero's full health (Patchwerk 60); the armor is in the number
    local full = BG.SeatMaxHealth(seat)
    local frac = full > 0 and s.health / full or 0
    if frac > 1 then
        frac = 1
    elseif frac < 0 then
        frac = 0
    end
    local w = (L.CELL - 8) * frac
    if w < 1 then
        row.bar:Hide()
    else
        row.bar:SetWidth(w)
        row.bar:Show()
    end
    local armor = s.armor or 0
    row.hpText:SetText(s.alive and ("|cffffffff" .. s.health .. (armor > 0 and ("+" .. armor) or "") .. "|r")
        or "|cffff6666x|r")
    row.fought = s.opp == m.me and seat ~= m.me
    if row.fought then
        row.glow:Show()
    else
        row.glow:Hide()
    end
    L.ShowRank(row, seat)
end

function L.Refresh()
    if not L.frame or not L.frame:IsShown() then
        return
    end
    local m = BG.model
    if not m or type(m.lobby) ~= "table" then
        return
    end
    local mine = BG.MyFaction()
    local order = { mine, 1 - mine }
    local slot = 0
    for o = 1, 2 do
        local faction = order[o]
        local seats = BG.FactionSeats(faction)
        for k = 1, 4 do
            slot = slot + 1
            local seat = seats[k]
            local row = L.rows[slot]
            local s = m.lobby[seat + 1] or { health = 0, tier = 1, alive = false, elim = 0, opp = -2 }
            row.seat = seat                  -- both looks: the cell click and the drop (L.CellAt) need it
            row.name = BG.SeatName(seat)
            row.label:SetText(BG.SeatTitle(seat))
            row.health:SetText((s.alive and "|cff66ff66" or "|cffff5555") .. s.health
                .. ((s.armor or 0) > 0 and ("+" .. s.armor) or "") .. "|r")
            row.state:SetText(BG.SeatState(seat))
            local rank = BG.LivePlacement(seat)
            if rank > 0 then
                row.state:SetText(row.state:GetText() .. "  |cffd8c8a0" .. BG.PlaceLabel(rank) .. "|r")
            end
            L.ShowRank(row, seat)
            if k == 1 then
                row.group:SetText("|cffffd100" .. BG.FactionName(faction)
                    .. (faction == mine and "  (your faction)" or "") .. "|r")
            else
                row.group:SetText("")
            end
            if L.art then
                L.RefreshCell(row, seat, s, m)
            elseif L.Needed(seat) then
                row:SetBackdropColor(0.12, 0.20, 0.12, 0.95)
                row:SetBackdropBorderColor(0.35, 1, 0.45, 1)
            elseif seat == m.me then
                row:SetBackdropColor(0.16, 0.20, 0.12, 0.95)
                row:SetBackdropBorderColor(0.6, 0.8, 0.3, 1)
            elseif s.opp == m.me then
                row:SetBackdropColor(0.22, 0.12, 0.12, 0.95)
                row:SetBackdropBorderColor(0.8, 0.35, 0.3, 1)
            elseif not s.alive then
                row:SetBackdropColor(0.08, 0.08, 0.08, 0.85)
                row:SetBackdropBorderColor(0.25, 0.22, 0.16, 1)
            else
                row:SetBackdropColor(0.10, 0.09, 0.07, 0.85)
                row:SetBackdropBorderColor(0.30, 0.26, 0.18, 1)
            end
        end
    end
end

function L.IsShown()
    return L.frame ~= nil and L.frame:IsShown()
end

-- True when the hovered hand card would help this ally (the hover glow).
function L.Needed(seat)
    local needs = L.needSeats
    if not needs or seat == nil then
        return false
    end
    for i = 1, table.getn(needs) do
        if needs[i].seat == seat then
            return true
        end
    end
    return false
end

-- The green halo of the allies a hovered card would help. `needs` is the wire entry of the card or nil.
function L.SetNeedSeats(needs)
    L.needSeats = needs
    for i = 1, table.getn(L.rows) do
        local row = L.rows[i]
        if row.needGlow then
            if L.Needed(row.seat) then
                row.needGlow:Show()
            else
                row.needGlow:Hide()
            end
        end
    end
    if not L.art and L.Refresh then
        L.Refresh()   -- repaint the plain rows (their border carries the hover)
    end
end
