-- Hearthstone addon: the Warsong Battlegrounds result screen.
--
-- Covers the window when the game is over (BG_END). Left: the Victory / Defeat emblem of the match window
-- (MatchUI.lua MU.ShowEmblem) with the player's hero in its arch, the faction result, the payout and the Done plate of the
-- match window (it closes the whole window, see E.Leave). Right: the
-- stats (placing, placing in the faction, rounds) over the table of all eight seats by their final placing: hero,
-- name, tavern tier, health and armor, standing or the round they went out in. The game itself ends with .hs bg leave.
-- Without the emblem texture the heading is plain text.
-- Reads HsBg.endResult / HsBg.model; built by BgShopUI.lua. Lua 5.0 / WoW 1.12.

Hearthstone = Hearthstone or {}
HsBgEndUI = HsBgEndUI or {}
local E = HsBgEndUI
local BG = HsBg
local H = Hearthstone

E.frame = nil
E.dismissed = false          -- Leave was pressed: the screen stays away until the next game

local WHITE = "Interface\\Buttons\\WHITE8X8"
local NUMBER_FONT = "Fonts\\FRIZQT__.TTF"
local HERO_PATH = "Interface\\Hearthstone\\Textures\\BgHeroes\\"

E.EMBLEM_H = 340             -- height of the emblem on screen
E.LEFT_X, E.LEFT_W = 70, 460 -- the emblem block (x from the window's left, width)
E.RIGHT_X, E.RIGHT_W = 600, 590
E.ROW_H = 47
E.PLACE_COLORS = { { 1, 0.82, 0.1 }, { 0.82, 0.84, 0.88 }, { 0.8, 0.52, 0.28 } }   -- gold, silver, bronze
E.FACTION_COLORS = { [0] = { 0.25, 0.5, 1 }, [1] = { 0.9, 0.2, 0.2 } }            -- Alliance, Horde

local function fs(parent, font, justify)
    local s = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormal")
    s:SetJustifyH(justify or "LEFT")
    return s
end

local function place(n)
    local suffix = "th"
    local d = n - math.floor(n / 10) * 10
    local h = n - math.floor(n / 100) * 100
    if d == 1 and h ~= 11 then
        suffix = "st"
    elseif d == 2 and h ~= 12 then
        suffix = "nd"
    elseif d == 3 and h ~= 13 then
        suffix = "rd"
    end
    return n .. suffix
end

-- True when Textures\BgHeroes\<key>.blp is installed (HS_BgTextures.heroes lists the keys).
local function heroInstalled(key)
    local list = HS_BgTextures and HS_BgTextures.heroes
    if not key or key == "" or type(list) ~= "table" then
        return false
    end
    for i = 1, table.getn(list) do
        if list[i] == key then
            return true
        end
    end
    return false
end

local function tint(tex, c, a)
    tex:SetTexture(WHITE)
    tex:SetVertexColor(c[1], c[2], c[3], a or 1)
end

-- One stat block: a big value over a small label.
local function makeChip(parent, x, w, label)
    local c = CreateFrame("Frame", nil, parent)
    c:SetWidth(w)
    c:SetHeight(58)
    c:SetPoint("TOPLEFT", parent, "TOPLEFT", x, 0)
    c.bg = c:CreateTexture(nil, "BACKGROUND")
    c.bg:SetAllPoints(c)
    tint(c.bg, { 0.16, 0.12, 0.07 }, 0.85)
    c.line = c:CreateTexture(nil, "BORDER")
    c.line:SetPoint("BOTTOMLEFT", c, "BOTTOMLEFT", 0, 0)
    c.line:SetPoint("BOTTOMRIGHT", c, "BOTTOMRIGHT", 0, 0)
    c.line:SetHeight(2)
    tint(c.line, { 0.75, 0.6, 0.25 }, 0.9)
    c.value = fs(c, "GameFontNormalLarge", "CENTER")
    c.value:SetFont(NUMBER_FONT, 22, "OUTLINE")
    c.value:SetPoint("TOP", c, "TOP", 0, -8)
    c.label = fs(c, "GameFontHighlightSmall", "CENTER")
    c.label:SetPoint("BOTTOM", c, "BOTTOM", 0, 8)
    c.label:SetText("|cffb8a888" .. label .. "|r")
    return c
end

-- One row of the table: place, hero picture, name and hero, tier, health, standing.
local function makeRow(parent, y)
    local r = CreateFrame("Frame", nil, parent)
    r:SetWidth(E.RIGHT_W)
    r:SetHeight(E.ROW_H - 3)
    r:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, -y)
    r.bg = r:CreateTexture(nil, "BACKGROUND")
    r.bg:SetAllPoints(r)
    r.bar = r:CreateTexture(nil, "BORDER")          -- the faction colour on the left edge
    r.bar:SetPoint("TOPLEFT", r, "TOPLEFT", 0, 0)
    r.bar:SetPoint("BOTTOMLEFT", r, "BOTTOMLEFT", 0, 0)
    r.bar:SetWidth(4)
    r.place = fs(r, "GameFontNormal", "CENTER")
    r.place:SetFont(NUMBER_FONT, 17, "OUTLINE")
    r.place:SetPoint("LEFT", r, "LEFT", 6, 0)
    r.place:SetWidth(46)
    r.icon = r:CreateTexture(nil, "ARTWORK")
    r.icon:SetWidth(34)
    r.icon:SetHeight(34)
    r.icon:SetPoint("LEFT", r, "LEFT", 58, 0)
    r.name = fs(r, "GameFontNormal", "LEFT")
    r.name:SetPoint("TOPLEFT", r, "TOPLEFT", 102, -6)
    r.name:SetWidth(210)
    r.hero = fs(r, "GameFontHighlightSmall", "LEFT")
    r.hero:SetPoint("BOTTOMLEFT", r, "BOTTOMLEFT", 102, 6)
    r.hero:SetWidth(210)
    r.tier = fs(r, "GameFontHighlightSmall", "CENTER")
    r.tier:SetPoint("LEFT", r, "LEFT", 318, 0)
    r.tier:SetWidth(62)
    r.health = fs(r, "GameFontHighlightSmall", "CENTER")
    r.health:SetPoint("LEFT", r, "LEFT", 384, 0)
    r.health:SetWidth(90)
    r.state = fs(r, "GameFontHighlightSmall", "RIGHT")
    r.state:SetPoint("RIGHT", r, "RIGHT", -10, 0)
    r.state:SetWidth(104)
    return r
end

function E.Create(parent)
    if E.frame or not parent then
        return E.frame
    end
    local MU = H.MatchUI
    local f = CreateFrame("Frame", "HearthstoneBgEnd", parent)
    f:SetAllPoints(parent)
    f:SetFrameLevel(parent:GetFrameLevel() + (HsBgUI and HsBgUI.OVERLAY_BASE or 0) + 40)
    f:EnableMouse(true)
    f:SetBackdrop({ bgFile = WHITE, tile = true, tileSize = 16, insets = { left = 0, right = 0, top = 0, bottom = 0 } })
    f:SetBackdropColor(0, 0, 0, 0.8)
    E.frame = f

    -- the left block: the emblem, what it says, the payout, Leave
    local box = CreateFrame("Frame", nil, f)
    box:SetWidth(E.LEFT_W)
    box:SetHeight(E.EMBLEM_H + 190)
    box:SetPoint("TOPLEFT", f, "TOPLEFT", E.LEFT_X, -36)
    f.box = box
    f.emblem = MU and MU.CreateEmblem and MU.CreateEmblem(box) or nil
    f.heading = fs(box, "GameFontNormalLarge", "CENTER")      -- the title of a draw, and of a result without the emblem
    f.heading:SetFont(NUMBER_FONT, 34, "OUTLINE")
    f.heading:SetWidth(E.LEFT_W)
    f.result = fs(box, "GameFontHighlight", "CENTER")
    f.result:SetWidth(E.LEFT_W - 40)
    f.result:SetHeight(40)
    f.result:SetJustifyV("TOP")
    f.payout = fs(box, "GameFontHighlight", "CENTER")
    f.payout:SetWidth(E.LEFT_W - 40)
    f.payout:SetText("")
    if MU and MU.DoneButton then
        f.leave = MU.DoneButton("HearthstoneBgLeave", box)
    else
        f.leave = CreateFrame("Button", "HearthstoneBgLeave", box)
        f.leave:SetWidth(160)
        f.leave:SetHeight(34)
        f.leave:SetBackdrop({ bgFile = WHITE, edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            edgeSize = 8, insets = { left = 2, right = 2, top = 2, bottom = 2 } })
        f.leave:SetBackdropColor(0.14, 0.12, 0.08, 0.95)
        f.leave:SetBackdropBorderColor(0.5, 0.42, 0.24, 1)
        f.leave.text = fs(f.leave, "GameFontNormal", "CENTER")
        f.leave.text:SetPoint("CENTER", f.leave, "CENTER", 0, 0)
        f.leave.text:SetText("Done")
    end
    f.leave:SetScript("OnClick", function() E.Leave() end)

    -- the right block: the headline, three stats, the table
    local panel = CreateFrame("Frame", nil, f)
    panel:SetWidth(E.RIGHT_W)
    panel:SetHeight(520)
    panel:SetPoint("TOPLEFT", f, "TOPLEFT", E.RIGHT_X, -44)
    f.panel = panel
    f.headline = fs(panel, "GameFontNormalLarge", "LEFT")
    f.headline:SetPoint("TOPLEFT", panel, "TOPLEFT", 0, 0)
    f.headline:SetWidth(E.RIGHT_W)
    f.chips = {
        makeChip(panel, 0, 190, "PLACING"),
        makeChip(panel, 200, 190, "IN YOUR FACTION"),
        makeChip(panel, 400, 190, "ROUNDS"),
    }
    for i = 1, 3 do
        f.chips[i]:SetPoint("TOPLEFT", panel, "TOPLEFT", (i - 1) * 200, -30)
    end
    f.rows = {}
    for p = 1, 8 do
        f.rows[p] = makeRow(panel, 102 + (p - 1) * E.ROW_H)
    end
    f:Hide()
    return f
end

-- Leave really leaves: it sends the server's leave command (.hs bg leave, which drops the finished game), puts the screen
-- away and closes the whole window.
function E.Leave()
    E.dismissed = true
    if E.frame then
        E.frame:Hide()
    end
    if HsBgUI and HsBgUI.Surrender then
        HsBgUI.Surrender()
    end
    if HsBgUI and HsBgUI.Hide then
        HsBgUI.Hide()
    end
end

-- The payout line ("12 coins and 1 pack"), or "" when the result carries none (rc=0, rp=0).
function E.PayoutText(e)
    if not e then
        return ""
    end
    local coins, packs = e.coins or 0, e.packs or 0
    local parts = {}
    if coins > 0 then
        table.insert(parts, coins .. (coins == 1 and " coin" or " coins"))
    end
    if packs > 0 then
        table.insert(parts, packs .. (packs == 1 and " pack" or " packs"))
    end
    if table.getn(parts) == 0 then
        return ""
    end
    return "|cff66ff66Your reward: " .. table.concat(parts, " and ") .. ".|r"
end

-- The picture behind the emblem's arch: the round hero portrait, its middle (the arch is taller than wide), else a flat
-- colour. `aspect` is the arch box's width / height.
local function heroPic(heroId, aspect)
    if not heroInstalled(heroId) then
        return { color = { 0.22, 0.19, 0.15 } }
    end
    local w = 0.62
    local vh = w / aspect
    if vh > 0.92 then
        vh = 0.92
    end
    return { path = HERO_PATH .. heroId, u1 = 0.5 - w / 2, u2 = 0.5 + w / 2, v1 = 0.5 - vh / 2, v2 = 0.5 + vh / 2 }
end

local function fillRow(row, seat, p, me)
    local s = BG.Seat(seat)
    local mine = seat == me
    local fc = E.FACTION_COLORS[BG.FactionOf(seat)] or E.FACTION_COLORS[0]
    tint(row.bg, mine and { 0.34, 0.25, 0.07 } or { 0.11, 0.09, 0.07 }, mine and 0.9 or 0.8)
    tint(row.bar, fc, 0.95)
    local pc = E.PLACE_COLORS[p] or { 0.75, 0.72, 0.66 }
    row.place:SetText(place(p))
    row.place:SetTextColor(pc[1], pc[2], pc[3])
    local heroId = s and s.hero or ""
    if heroInstalled(heroId) then
        row.icon:SetTexture(HERO_PATH .. heroId)
        row.icon:SetVertexColor(1, 1, 1, s and s.alive and 1 or 0.55)
    else
        row.icon:SetTexture(WHITE)
        row.icon:SetVertexColor(0.25, 0.22, 0.18, 1)
    end
    local name = BG.SeatName(seat)
    row.name:SetText((mine and "|cffffd100" or "|cffffffff") .. name .. (mine and " (you)" or "") .. "|r")
    row.hero:SetText(heroId ~= "" and ("|cffb8a888" .. BG.HeroName(heroId) .. "|r") or "")
    if s then
        row.tier:SetText("|cffd8c8a0Tier " .. s.tier .. "|r")
        if s.alive then
            row.health:SetText("|cff66dd66" .. s.health .. " health|r" .. (s.armor > 0 and ("  |cffb0c4de+" .. s.armor .. "|r") or ""))
            row.state:SetText("|cff66ff66Survived|r")
        else
            row.health:SetText("")
            row.state:SetText("|cffff7766Out in round " .. s.elim .. "|r")
        end
    else
        row.tier:SetText("")
        row.health:SetText("")
        row.state:SetText("")
    end
end

function E.Refresh()
    if not E.frame then
        return
    end
    -- While the human's fight plays, the result waits behind it.
    if HsBgCombatUI and HsBgCombatUI.IsPlaying() then
        E.frame:Hide()
        return
    end
    if BG.state ~= "ended" then
        E.dismissed = false
        E.frame:Hide()
        return
    end
    if E.dismissed then
        E.frame:Hide()
        return
    end
    local e = BG.endResult
    if not e then
        E.frame:Hide()
        return
    end
    local f = E.frame
    local MU = H.MatchUI
    local m = BG.model
    local me = m and m.me or 0
    local draw = e.winner == -1
    local won = e.won and not draw
    local ownHero = BG.Seat(me) and BG.Seat(me).hero or ""
    if ownHero == "" and m then
        ownHero = m.hero or ""
    end

    -- the left block
    local emblem = nil
    if f.emblem and MU and MU.ShowEmblem then
        local kind = (won or draw) and "victory" or "defeat"
        local def = MU.EMBLEMS and MU.EMBLEMS[kind]
        emblem = def and MU.ShowEmblem(f.emblem, kind, heroPic(ownHero, MU.EmblemBoxAspect(def)), E.LEFT_W, E.EMBLEM_H) or nil
    end
    if f.emblem and not emblem then
        MU.HideEmblem(f.emblem)
    end
    local y = emblem and (E.EMBLEM_H - 4) or 0
    f.heading:ClearAllPoints()
    f.result:ClearAllPoints()
    f.payout:ClearAllPoints()
    f.leave:ClearAllPoints()
    if draw then
        f.heading:SetText("|cffd9d9d9A draw|r")
    elseif won then
        f.heading:SetText("|cff66ff66Victory!|r")
    else
        f.heading:SetText("|cffff5555Defeat|r")
    end
    if emblem and not draw then
        f.heading:Hide()                      -- the emblem says it
    else
        f.heading:SetPoint("TOP", f.box, "TOP", 0, -y - 2)
        f.heading:Show()
        y = y + 44
    end
    local mine = BG.FactionName(e.myFaction)
    local other = BG.FactionName(1 - e.myFaction)
    f.result:SetPoint("TOP", f.box, "TOP", 0, -y - 4)
    f.result:SetText(draw and "Neither faction won: the match was a draw."
        or ((e.winner == e.myFaction) and ("Your faction, the " .. mine .. ", won the match.")
            or ("Your faction, the " .. mine .. ", lost the match to the " .. other .. ".")))
    f.payout:SetPoint("TOP", f.box, "TOP", 0, -y - 34)
    f.payout:SetText(E.PayoutText(e))
    f.leave:SetPoint("TOP", f.box, "TOP", 0, -y - 52)

    -- the right block
    f.headline:SetText("|cffffd100Final standings|r" .. (e.timedOut and "  |cffb8a888(round limit reached)|r" or ""))
    f.chips[1].value:SetText(place(e.overall) .. " |cffb8a888of 8|r")
    f.chips[2].value:SetText(place(e.placement) .. " |cffb8a888of 4|r")
    f.chips[3].value:SetText(tostring(e.round))
    local c1 = E.PLACE_COLORS[e.overall]
    if c1 then
        f.chips[1].value:SetTextColor(c1[1], c1[2], c1[3])
    else
        f.chips[1].value:SetTextColor(1, 1, 1)
    end
    local byPlace = {}
    for seat = 0, 7 do
        local p = e.order[seat + 1]
        if p then
            byPlace[p] = seat
        end
    end
    for p = 1, 8 do
        local seat = byPlace[p]
        if seat ~= nil then
            fillRow(f.rows[p], seat, p, me)
            f.rows[p]:Show()
        else
            f.rows[p]:Hide()
        end
    end
    f:Show()
end

function E.IsShown()
    return E.frame ~= nil and E.frame:IsShown()
end
