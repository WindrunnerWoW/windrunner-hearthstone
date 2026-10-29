-- Hearthstone entry point: reuse the world-map micro button and its existing anchors.
-- The button takes over the map button only once the account finished the
-- introduction's card quest ("Too Many Rules" / "No Time to Play"; Hearthstone.IntroUnlocked). Until then the map
-- button is hidden and the micro buttons anchored to it move into its place (owner decision: no map button at all;
-- the map stays on its key); /hs and the key binding work anyway. Its icon is the Hearthstone card back
-- (Textures\UI\MicroIcon, made by tools/art/ui_icons.py and copied by the deploy script), with a stock icon as
-- fallback when that texture is missing.
-- Lua 5.0 / WoW 1.12: script handlers use this / event / arg1 globals.

Hearthstone = Hearthstone or {}
local H = Hearthstone
H.MicroButton = H.MicroButton or {}
local MB = H.MicroButton

MB.ICON = "Interface\\AddOns\\Hearthstone\\Textures\\UI\\MicroIcon"
MB.ICON_RIGHT = 0.671875     -- the card's width on the 128x128 texture (tools/art/ui_icons.py prints it)
MB.ICON_FALLBACK = "Interface\\Icons\\INV_Misc_Ticket_Tarot_Stack_01"
MB.iconPath = nil            -- the icon in use (MB.ICON or the fallback)
MB.frame = nil

function MB.UpdateState()
    local b = MB.frame
    if not b then
        return
    end
    local collection = H.CollectionUI and H.CollectionUI.frame
    if collection and collection:IsVisible() then
        b:SetButtonState("PUSHED", 1)
    else
        b:SetButtonState("NORMAL")
    end
end

function MB.RefreshTooltip()
    local b = MB.frame
    if not b then
        return
    end
    b.tooltipText = "Hearthstone"
    if MicroButtonTooltipText then
        b.tooltipText = MicroButtonTooltipText(b.tooltipText, "HEARTHSTONE_TOGGLE")
    end
    b.newbieText = "Open your card collection, decks and packs."
end

function MB.OnEnter()
    if not MB.frame or not GameTooltip then
        return
    end
    MB.RefreshTooltip()
    GameTooltip:SetOwner(MB.frame, "ANCHOR_RIGHT")
    GameTooltip:SetText(MB.frame.tooltipText)
    GameTooltip:AddLine("Click: open collection", 1, 1, 1)
    if HsPacks and HsPacks.state == "ready" then
        GameTooltip:AddLine("Unopened packs: " .. HsPacks.stock, 1, 0.82, 0)
    end
    GameTooltip:Show()
end

local function placeIcon(texture, offset, shade)
    texture:ClearAllPoints()
    texture:SetWidth(18)
    texture:SetHeight(25)
    texture:SetPoint("TOP", MB.frame, "TOP", 0, offset)
    if MB.iconPath == MB.ICON then
        texture:SetTexCoord(0, MB.ICON_RIGHT, 0, 1)
    else
        texture:SetTexCoord(0.05, 0.95, 0.05, 0.95)
    end
    texture:SetVertexColor(shade, shade, shade)
end

-- Returns nil when the default micro menu is unavailable. Slash commands and the
-- Hearthstone binding remain usable; never create an extra minimap button.
function MB.Create()
    local b = WorldMapMicroButton
    if not b then
        return nil
    end
    if MB.frame == b then
        return b
    end
    MB.ShowMap()
    MB.frame = b

    -- The character button supplies a blank native micro-menu frame. The button's
    -- own icon textures automatically switch for mouse-down and disabled states.
    local border = b:CreateTexture(nil, "BACKGROUND")
    border:SetTexture("Interface\\Buttons\\UI-MicroButtonCharacter-Up")
    border:SetAllPoints(b)
    MB.border = border
    -- 1.12 SetTexture answers nil for a file it cannot load (the card back not deployed): use the stock icon.
    local probe = b:CreateTexture(nil, "BACKGROUND")
    MB.iconPath = probe:SetTexture(MB.ICON) and MB.ICON or MB.ICON_FALLBACK
    probe:SetTexture(nil)
    probe:Hide()
    b:SetNormalTexture(MB.iconPath)
    b:SetPushedTexture(MB.iconPath)
    b:SetDisabledTexture(MB.iconPath)
    placeIcon(b:GetNormalTexture(), -28, 1)
    placeIcon(b:GetPushedTexture(), -29, 0.5)
    placeIcon(b:GetDisabledTexture(), -28, 0.4)
    MB.icon = b:GetNormalTexture()
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    b:RegisterEvent("UPDATE_BINDINGS")
    b:SetScript("OnClick", function()
        if H.ToggleCollection then
            H.ToggleCollection()
        end
    end)
    b:SetScript("OnEnter", MB.OnEnter)
    b:SetScript("OnLeave", function()
        if GameTooltip then
            GameTooltip:Hide()
        end
    end)
    b:SetScript("OnEvent", function()
        if event == "UPDATE_BINDINGS" then
            MB.RefreshTooltip()
        end
    end)

    -- Blizzard updates every micro button when other panels open or close. Run
    -- its normal update first, then correct this button's world-map-based state.
    if UpdateMicroButtons then
        local original = UpdateMicroButtons
        UpdateMicroButtons = function()
            original()
            MB.UpdateState()
        end
    end
    MB.RefreshTooltip()
    MB.UpdateState()
    return b
end

-- Before the introduction: hide the map button; every micro button anchored to it takes its anchor, so no gap stays.
function MB.HideMap()
    local b = WorldMapMicroButton
    if not b or MB.frame or MB.mapHidden or not b.GetPoint then
        return
    end
    local point, rel, relPoint, x, y = b:GetPoint(1)
    if not point then
        return
    end
    local moved = {}
    for name, f in pairs(_G) do
        if type(name) == "string" and f ~= b and type(f) == "table" and type(f.GetPoint) == "function"
            and string.find(name, "MicroButton$") then
            local p, r, rp, fx, fy = f:GetPoint(1)
            if r == b or r == "WorldMapMicroButton" then
                table.insert(moved, { frame = f, point = p, relPoint = rp, x = fx, y = fy })
                f:ClearAllPoints()
                f:SetPoint(point, rel, relPoint, x, y)
            end
        end
    end
    MB.mapHidden = moved
    b:Hide()
end

-- Gives the map button its place back (right before it becomes the Hearthstone button).
function MB.ShowMap()
    local b = WorldMapMicroButton
    local moved = MB.mapHidden
    if not b or not moved then
        return
    end
    for _, m in ipairs(moved) do
        m.frame:ClearAllPoints()
        m.frame:SetPoint(m.point, b, m.relPoint, m.x, m.y)
    end
    MB.mapHidden = nil
    b:Show()
end

-- Takes over the map button once the introduction is done (never gives it back during a session: the hooks stay);
-- before that the map button is hidden.
function MB.Refresh()
    if not MB.frame and not (H.IntroUnlocked and H.IntroUnlocked()) then
        MB.HideMap()
        return
    end
    MB.ShowMap()
    MB.Create()
    MB.RefreshTooltip()
    MB.UpdateState()
end
