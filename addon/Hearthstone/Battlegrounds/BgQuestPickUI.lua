-- Hearthstone addon: the Warsong Battlegrounds quest-pick panel.
--
-- Shown by BgShopUI.lua while the state is in the quest-pick phase (BG.PickingQuest, `ph=q`, at the start of turn 4):
-- the three quest + reward pairs the server offered (`qo`): the reward as a card on top, the quest's text (with its goal)
-- on a quest plate below it with a green glow, as in the game. The reward is its real card (Textures\BgQuests\Cards,
-- HS_BgTextures.rewardCards) or, for the few HearthstoneJSON has no render of, a drawn card. A click sends
-- BG_ACT act=quest;i=<choice>; the shop behind stays hidden until the pick is done. Escape is left to the window
-- (UI.SyncEscape): nothing is armed here, exactly like the hero pick. Reads HsBg.model only. Lua 5.0 / WoW 1.12.

Hearthstone = Hearthstone or {}
local H = Hearthstone
HsBgQuestPickUI = HsBgQuestPickUI or {}
local P = HsBgQuestPickUI
local BG = HsBg

H.BgUI = H.BgUI or {}
local UI = H.BgUI

P.CELL_W, P.CELL_H = 200, 394
P.GAP = 26
P.MAX = 3
P.frame = nil
P.tiles = {}

local function fs(parent, font, justify)
    local s = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormal")
    s:SetJustifyH(justify or "LEFT")
    return s
end

function P.Create(parent)
    if P.frame or not parent then
        return P.frame
    end
    local f = CreateFrame("Frame", "HearthstoneBgQuestPick", parent)
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", 230, -44)
    f:SetWidth(780)
    f:SetHeight(520)
    f:SetFrameLevel(parent:GetFrameLevel() + (HsBgUI and HsBgUI.OVERLAY_BASE or 0) + 25)
    f:EnableMouse(true)
    P.frame = f
    f.title = fs(f, "GameFontNormalLarge", "CENTER")
    f.title:SetPoint("TOP", f, "TOP", 0, -14)
    f.title:SetWidth(740)
    f.title:SetText("|cffffd100Choose a quest|r")
    f.hint = fs(f, "GameFontNormalSmall", "CENTER")
    f.hint:SetPoint("TOP", f, "TOP", 0, -42)
    f.hint:SetWidth(740)
    f.hint:SetText("Each quest counts up. When it is done its reward is active for the rest of the game.")
    f:Hide()
    return f
end

function P.OnEnter(t)
    if not GameTooltip then
        return
    end
    GameTooltip:SetOwner(t, "ANCHOR_RIGHT")
    GameTooltip:SetText(BG.QuestName(t.quest))
    local questText = BG.QuestText(t.quest, t.typeA, t.typeB)
    if questText ~= "" then
        GameTooltip:AddLine(questText, 1, 1, 1)
    end
    GameTooltip:AddLine("Reward: " .. BG.RewardName(t.reward), 1, 0.82, 0.2)
    local rewardText = BG.RewardText(t.reward, t.rewardType)
    if rewardText ~= "" then
        GameTooltip:AddLine(rewardText, 1, 1, 1)
    end
    GameTooltip:AddLine("Click to take this quest.", 0.8, 0.8, 0.8)
    GameTooltip:Show()
end

-- One choice: the reward as a card on top (Textures\Bg\reward_card: its portrait shows through the round window, the
-- name on the banner, the text in the parchment box), the quest below on the quest plate (Textures\Bg\quest_frame).
-- Both textures are drawn into the top-left of a power-of-two canvas, hence the texcoord bottoms.
local CARD_W, CARD_H = 190, 288
local PLATE_W, PLATE_H = 176, 120
local CARD_K = CARD_W / 256
local PLATE_TOP = CARD_H - 14
local GLOW_S = PLATE_W / 448            -- quest_glow: the plate is 448 px wide at (32, 32) of its texture

-- The text without colour codes (the card data carries its own; the plate and the drawn card are light parchment).
local function plain(text)
    text = string.gsub(text or "", "|c%x%x%x%x%x%x%x%x", "")
    text = string.gsub(text, "|r", "")
    -- the card data breaks its lines for the real card; on the drawn one the text wraps to its box
    text = string.gsub(text, "\n", " ")
    text = string.gsub(text, "%s+", " ")
    return text
end

-- Where the reward's name and text stand: on the drawn card's banner and text box, or on a baked card's (the blank card
-- scaled to the footprint of a real render, with its art but no words: bg_quests.py `baked`).
local function placeRewardText(t, baked)
    local cx = (P.CELL_W - CARD_W) / 2
    t.rewardName:ClearAllPoints()
    t.rewardText:ClearAllPoints()
    if baked then
        t.rewardName:SetPoint("TOPLEFT", t, "TOPLEFT", cx + 40 * CARD_K, -184 * CARD_K)
        t.rewardName:SetWidth(176 * CARD_K)
        t.rewardName:SetHeight(28 * CARD_K)
        t.rewardText:SetPoint("TOPLEFT", t, "TOPLEFT", cx + 48 * CARD_K, -238 * CARD_K)
        t.rewardText:SetWidth(160 * CARD_K)
        t.rewardText:SetHeight(92 * CARD_K)
    else
        -- the blank card (the reward_card texture): the gold banner at y 186-216, the arched parchment box from y 256
        t.rewardName:SetPoint("TOPLEFT", t, "TOPLEFT", cx + 58 * CARD_K, -187 * CARD_K)
        t.rewardName:SetWidth(140 * CARD_K)
        t.rewardName:SetHeight(30 * CARD_K)
        t.rewardText:SetPoint("TOPLEFT", t, "TOPLEFT", cx + 52 * CARD_K, -258 * CARD_K)
        t.rewardText:SetWidth(152 * CARD_K)
        t.rewardText:SetHeight(94 * CARD_K)
    end
end

-- Writes `text` in the largest font size (max down to min) that fits the string's box (its width wraps, its height is the
-- limit), so a long reward text is never cut off ("... Stealth un...") and a short one reads big.
local FONT = "Fonts\\FRIZQT__.TTF"
local function fitText(fsr, text, maxSize, minSize, flags)
    if not (fsr.SetFont and fsr.GetStringHeight) then
        fsr:SetText(text)
        return
    end
    local limit = fsr:GetHeight()
    local size = maxSize
    while true do
        fsr:SetFont(FONT, size, flags)
        fsr:SetText(text)
        if size <= minSize or fsr:GetStringHeight() <= limit then
            break
        end
        size = size - 1
    end
end

-- The coin over the mana gem: the gem of a real render is at (43, 66), the one of the blank card at (32, 38).
local function placeCoin(t, real)
    local cx = (P.CELL_W - CARD_W) / 2
    t.coin:ClearAllPoints()
    t.coin:SetWidth(58 * CARD_K)
    t.coin:SetHeight(58 * CARD_K)
    t.coin:SetPoint("CENTER", t, "TOPLEFT", cx + (real and 43 or 32) * CARD_K, -(real and 66 or 38) * CARD_K)
end

-- The name on the banner follows the banner's curve, as the name of a real card does: the ribbon sags in the middle and
-- its ends rise, so each letter is its own string on a shallow arc (there is no text rotation in 1.12, a letter only
-- moves up or down). `cy` is the banner's middle line, `half` half the width the name may take, `depth` how much higher
-- the ends sit, negative: lower, as the blank card's ribbon arches (all in 256-wide card pixels). Without the string measures (a plain client) the name stays one string.
local letterPool = {}     -- tile -> its letter strings
local function curveTitle(t, text, cy, half, depth)
    letterPool[t] = letterPool[t] or {}
    local letters = letterPool[t]
    for _, l in pairs(letters) do      -- (the pool has holes at the spaces: no table.getn)
        l:Hide()
    end
    local probe = t.rewardName
    if not (probe.SetFont and probe.GetStringWidth) or string.len(text) > 28 then
        return false
    end
    local cx = (P.CELL_W - CARD_W) / 2
    local limit = 2 * half * CARD_K
    local size = 14
    local widths, total
    while true do
        widths, total = {}, 0
        for i = 1, string.len(text) do
            local ch = string.sub(text, i, i)
            local w
            if ch == " " then
                w = size * 0.32
            else
                probe:SetFont(FONT, size, "OUTLINE")
                probe:SetText(ch)
                w = probe:GetStringWidth()
            end
            widths[i] = w
            total = total + w
        end
        if total <= limit or size <= 8 then
            break
        end
        size = size - 1
    end
    local x = -total / 2
    for i = 1, string.len(text) do
        local ch = string.sub(text, i, i)
        local mid = x + widths[i] / 2
        x = x + widths[i]
        if ch ~= " " then
            local l = letters[i]
            if not l then
                l = fs(t, "GameFontNormalSmall", "CENTER")
                letters[i] = l
            end
            l:SetFont(FONT, size, "OUTLINE")
            l:SetText("|cffffffff" .. ch .. "|r")
            local u = mid / (limit / 2)
            l:ClearAllPoints()
            l:SetPoint("CENTER", t, "TOPLEFT", cx + 128 * CARD_K + mid, -(cy * CARD_K) + depth * CARD_K * u * u)
            l:Show()
        end
    end
    probe:SetFont(FONT, size, "OUTLINE")
    probe:SetText("|cffffffff" .. text .. "|r")
    if probe.SetAlpha then
        probe:SetAlpha(0)          -- the one-piece name stays for the tests and tooltips, the letters are what is seen
    end
    return true
end

-- Dark text on parchment: no shadow under it.
local function noShadow(text)
    if text.SetShadowOffset then
        text:SetShadowOffset(0, 0)
    end
end

local function makeTile(f, i)
    local b = CreateFrame("Button", "HearthstoneBgQuestTile" .. i, f)
    b:SetWidth(P.CELL_W)
    b:SetHeight(P.CELL_H)
    local cx = (P.CELL_W - CARD_W) / 2
    b.card = b:CreateTexture(nil, "ARTWORK")
    b.card:SetTexture(UI.BG_PATH .. "reward_card")
    b.card:SetWidth(CARD_W)
    b.card:SetHeight(CARD_H)
    b.card:SetPoint("TOPLEFT", b, "TOPLEFT", cx, 0)
    b.card:SetTexCoord(0, 1, 0, 388 / 512)
    -- the round reward portrait sits over the dark stone window of the blank card (a real card brings its own art)
    b.rewardArt = b:CreateTexture(nil, "OVERLAY")
    b.rewardArt:SetWidth(112 * CARD_K)
    b.rewardArt:SetHeight(112 * CARD_K)
    b.rewardArt:SetPoint("CENTER", b, "TOPLEFT", cx + 128 * CARD_K, -112 * CARD_K)
    b.rewardArt:Hide()
    -- a card prints a mana gem where a reward costs nothing: the game's gold coin covers it
    b.coin = b:CreateTexture(nil, "OVERLAY")
    if not (UI.SetPiece and UI.SetPiece(b.coin, "bg_coin")) then
        b.coin:SetTexture(UI.BG_PATH .. "coin")
    end
    b.coin:SetWidth(58 * CARD_K)
    b.coin:SetHeight(58 * CARD_K)
    b.coin:SetPoint("CENTER", b, "TOPLEFT", cx + 43 * CARD_K, -66 * CARD_K)
    b.coin:Hide()
    b.rewardName = fs(b, "GameFontNormalSmall", "CENTER")
    b.rewardName:SetPoint("TOPLEFT", b, "TOPLEFT", cx + 27 * CARD_K, -217 * CARD_K)
    b.rewardName:SetWidth(202 * CARD_K)
    b.rewardName:SetHeight(28 * CARD_K)
    b.rewardName:SetJustifyV("MIDDLE")
    b.rewardText = fs(b, "GameFontNormalSmall", "CENTER")
    b.rewardText:SetPoint("TOPLEFT", b, "TOPLEFT", cx + 33 * CARD_K, -268 * CARD_K)
    b.rewardText:SetWidth(190 * CARD_K)
    b.rewardText:SetHeight(92 * CARD_K)
    b.rewardText:SetJustifyV("TOP")
    noShadow(b.rewardText)
    local px = (P.CELL_W - PLATE_W) / 2
    b.glow = b:CreateTexture(nil, "BACKGROUND")
    b.glow:SetTexture(UI.BG_PATH .. "quest_glow")
    b.glow:SetBlendMode("ADD")
    b.glow:SetWidth(512 * GLOW_S)
    b.glow:SetHeight(368 * GLOW_S)
    b.glow:SetPoint("TOPLEFT", b, "TOPLEFT", px - 32 * GLOW_S, -(PLATE_TOP - 32 * GLOW_S))
    b.glow:SetTexCoord(0, 1, 0, 368 / 512)
    b.plate = b:CreateTexture(nil, "ARTWORK")
    b.plate:SetTexture(UI.BG_PATH .. "quest_frame")
    b.plate:SetWidth(PLATE_W)
    b.plate:SetHeight(PLATE_H)
    b.plate:SetPoint("TOPLEFT", b, "TOPLEFT", px, -PLATE_TOP)
    b.plate:SetTexCoord(0, 1, 0, 348 / 512)
    -- the quest's own portrait is not drawn (the plate's exclamation gem stands for it); questArt stays for the tests
    b.questArt = b:CreateTexture(nil, "OVERLAY")
    b.questArt:SetWidth(1)
    b.questArt:SetHeight(1)
    b.questArt:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
    b.questArt:Hide()
    b.questName = fs(b, "GameFontNormalSmall", "CENTER")    -- never shown: the plate carries the "Quest: ..." line only
    b.questName:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
    b.questName:Hide()
    b.questText = fs(b, "GameFontNormal", "CENTER")
    b.questText:SetPoint("TOPLEFT", b, "TOPLEFT", px + PLATE_W * 0.15, -(PLATE_TOP + PLATE_H * 0.34))
    b.questText:SetWidth(PLATE_W * 0.70)
    b.questText:SetHeight(PLATE_H * 0.46)
    b.questText:SetJustifyV("MIDDLE")
    noShadow(b.questText)
    b:RegisterForClicks("LeftButtonUp")
    b:SetScript("OnClick", function() BG.ChooseQuest(this.index) end)
    b:SetScript("OnEnter", function() P.OnEnter(this) end)
    b:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)
    return b
end

function P.Refresh()
    if not P.frame then
        return
    end
    local m = BG.model
    local offers = BG.QuestOffers()
    if not m or m.ph ~= "q" or table.getn(offers) == 0 then
        P.frame:Hide()
        return
    end
    P.frame:Show()
    local n = table.getn(offers)
    local span = n * P.CELL_W + (n - 1) * P.GAP
    local start = (P.frame:GetWidth() - span) / 2
    if start < 0 then
        start = 0
    end
    for i = 1, n do
        local t = P.tiles[i]
        if not t then
            t = makeTile(P.frame, i)
            P.tiles[i] = t
        end
        t.index = i - 1
        t.quest = offers[i].quest
        t.reward = offers[i].reward
        t.typeA, t.typeB, t.rewardType = offers[i].typeA or 0, offers[i].typeB or 0, offers[i].rewardType or 0
        t:ClearAllPoints()
        t:SetPoint("TOPLEFT", P.frame, "TOPLEFT", start + (i - 1) * (P.CELL_W + P.GAP), -70)
        t.real = UI.Listed and UI.Listed(HS_BgTextures and HS_BgTextures.rewardCards, t.reward) and true or false
        if t.real then
            t.card:SetTexture(UI.QUEST_PATH .. "Cards\\" .. t.reward)
            placeCoin(t, true)
            t.coin:Show()
            t.rewardArt:Hide()
            t.baked = UI.Listed(HS_BgTextures.rewardBaked, t.reward) and true or false
            placeRewardText(t, true)
            if t.baked then
                t.rewardName:Show()
                t.rewardText:Show()
            else
                t.rewardName:Hide()
                t.rewardText:Hide()
            end
        else
            t.card:SetTexture(UI.BG_PATH .. "reward_card")
            t.baked = false
            placeRewardText(t, false)
            placeCoin(t, false)
            t.coin:Show()
            t.rewardName:Show()
            t.rewardText:Show()
            if UI.RewardArtInstalled and UI.RewardArtInstalled(t.reward) then
                t.rewardArt:SetTexture(UI.QUEST_PATH .. t.reward)
                t.rewardArt:SetVertexColor(1, 1, 1)
                t.rewardArt:Show()
            else
                t.rewardArt:Hide()
            end
        end
        t.questName:SetText(plain(BG.QuestName(t.quest)))
        t.questText:SetText("|cff3a2410" .. plain(BG.QuestText(t.quest, t.typeA, t.typeB)) .. "|r")
        t.rewardName:SetJustifyV("MIDDLE")
        t.rewardText:SetJustifyV("MIDDLE")
        local name = plain(BG.RewardName(t.reward))
        if t.rewardName.SetAlpha then
            t.rewardName:SetAlpha(1)
        end
        fitText(t.rewardName, "|cffffffff" .. name .. "|r", 14, 8, "OUTLINE")
        local ok = false
        if t.baked then
            ok = curveTitle(t, name, 197, 78, -6)
        elseif not t.real then
            ok = curveTitle(t, name, 203, 70, -5)
        end
        if not ok then
            for _, l in pairs(letterPool[t] or {}) do
                l:Hide()
            end
        end
        fitText(t.rewardText, "|cff1e1006" .. plain(BG.RewardText(t.reward, t.rewardType)) .. "|r", 13, 7)
        t:Show()
    end
    for i = n + 1, table.getn(P.tiles) do
        P.tiles[i]:Hide()
    end
end

function P.Hide()
    if P.frame then
        P.frame:Hide()
    end
end

function P.IsShown()
    return P.frame ~= nil and P.frame:IsShown()
end
