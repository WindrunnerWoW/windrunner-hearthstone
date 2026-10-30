-- Hearthstone addon: the Warsong Battlegrounds window and shop.
--
-- The main window of the mode, opened by the server's first BG_STATE push (and again after a /reload by the BG_GET of
-- HsBg.Restore). It owns the driver frame (the frame clock that ticks HsBg and the combat playback) and orchestrates
-- the panels: the lobby (BgLobbyUI.lua), the fight playback (BgCombatUI.lua) and the result screen (BgEndUI.lua).
--
--   tavern      the offers of the round (click to Buy; tooltips from BgCardDB) with Gold, tier and the
--               Upgrade / Refresh / Freeze / End Turn buttons on the right
--   board       the warband (click to pick up, click a slot to drop, drag to reorder, drag onto Bob to sell)
--   hand        the bought cards (click to pick up, click a board slot to play; a drop on a friendly minion also sends
--               that minion as the Battlecry / Magnetic target; drag onto an ally cell in the lobby column, or pick
--               one up and click the ally, to send it)
--   hero        my hero in the bottom arch with health and armor, the hero power button beside it and the
--               hero-pick panel while the pick phase is open
--   discover    the three-option choice of a triple and the engine's Discover, when one is open (a minion or a
--               hero power)
--   lobby       the eight seats by faction with health, tier and eliminations
--   message     the last line: a refusal, a lost answer, or the mode hint
--
-- Art: the window wears the tavern board (HS_BoardTextures "bg_board" and the other bg_* / minion pieces of
-- tools/art/slice_board.py); a tile is the minion oval with its portrait (HS_BgTextures), the Taunt frame, the Divine
-- Shield and the attack / health gems. Every piece has a plain fallback (a tinted tile with name, stats, tier and
-- keywords, backdrop buttons) for a client without the board art.
-- Everything shown comes from BgState.lua (HsBg). Lua 5.0 / WoW 1.12: handlers get no arguments and use this / arg1.

Hearthstone = Hearthstone or {}
local H = Hearthstone
H.BgUI = H.BgUI or {}
HsBgUI = H.BgUI                     -- the global the panel modules (combat, lobby, end) reach for
local UI = H.BgUI
local BG = HsBg
local BP = HsBgProtocol
local V = HsBgVis

UI.W, UI.H = 1240, 632
UI.TILE_W, UI.TILE_H = 72, 88         -- a minion of the tavern and of my board (small enough for the tier shield and the Taunt frame to stay inside the board)
UI.OFFER_W, UI.OFFER_H = UI.TILE_W, UI.TILE_H
UI.PICK_W, UI.PICK_H = 82, 100        -- a Discover choice
UI.HAND_W, UI.HAND_H = 96, 146        -- a hand card at rest: the whole card, as in the match window (the 256x388 aspect)
UI.HAND_POP_W = 150                   -- the hovered hand card grows to this width, from its bottom edge
UI.HAND_OVERLAP = 0.42                -- how much of its width a hand card is covered by the next one
UI.HAND_SPAN = 348                    -- the widest the resting hand gets (centred under my hero, short of the gold pill)
UI.HAND_TILT = 12                     -- degrees the outermost hand cards lean outwards (MatchUI's fan default)
UI.HAND_STRIPS, UI.HAND_GLOW_STRIPS = 12, 16   -- strips of a tilted card / of its halo (CardWidget.PlaceStrips)
UI.HAND_ARC = 14                      -- the outer cards of the hand stand this many pixels lower than the middle one
-- The full-window overlays (discover, quest pick, combat, end, hero pick) stand this far above the window: over a full
-- hand (10 bands, its pop and its numbers) and under nothing. Each adds its own order on top (HsBgUI.OVERLAY_BASE).
UI.OVERLAY_BASE = 180
UI.HAND_BAND = 8                     -- frame levels per hand card (the card, its shield layer and its numbers)
UI.HAND_POP_LEVEL = 90                -- the hovered card is lifted this far
UI.ROW_GAP = 18                      -- between the minions of the tavern and my board (the gems reach 10 px beyond a tile)
UI.NUMBER_FONT = "Fonts\\FRIZQT__.TTF"
UI.WHITE = "Interface\\Buttons\\WHITE8X8"
UI.EDGE = "Interface\\Tooltips\\UI-Tooltip-Border"
UI.ART_PATH = "Interface\\Hearthstone\\Textures\\BgMinions\\"
UI.BG_PATH = "Interface\\Hearthstone\\Textures\\Bg\\"
UI.HERO_PATH = "Interface\\Hearthstone\\Textures\\BgHeroes\\"
UI.POWER_PATH = "Interface\\Hearthstone\\Textures\\BgHeroes\\Powers\\"
UI.QUEST_PATH = "Interface\\Hearthstone\\Textures\\BgQuests\\"   -- quest and reward portraits
UI.SECRET_ICON = "Interface\\Icons\\INV_Misc_QuestionMark"   -- a Secret on my hero portrait
UI.PIPS = 6                          -- tavern tier pips (texture Textures\Bg\tier_pip), without the tier shields
UI.MAX_TIER = 7                      -- the tier shields bg_tier_1..7 (tools/art/cut_checker.py + slice_board.py)
UI.TIER_SHIELD_W = 46                -- Bob's tier shield at the lower left of his arch
UI.OFFER_SHIELD_W = 30               -- the small tier shield on top of a tavern offer
UI.END_GUARD = 0.6                   -- End Turn ignores clicks this long after the fight's Continue (same slot)

UI.POWER_RING_PAD = 4               -- the hero_power_ring piece reaches this far beyond the power button
UI.POWER_ART_INSET = 4              -- the round power portrait sits this far inside the button, under the ring
UI.HERO_W = 118                     -- the golden hero arch (its height follows the arch's aspect)
UI.COIN = 18                        -- a gold coin of the gold bar
UI.COIN_STEP = 20                   -- coins side by side, not overlapping
UI.MAX_COINS = 10
UI.TOAST_TIME = 2.6                 -- seconds a refusal / notice stays in the middle of the board
UI.KW_ICON = 15                     -- the keyword badges at the bottom of a minion
UI.KW_ICONS = {                     -- standard 1.12 icons (MatchUI uses Ability_Poisons too)
    Poisonous = "Interface\\Icons\\Ability_Poisons", Windfury = "Interface\\Icons\\Spell_Nature_Windfury",
    ["Mega-Windfury"] = "Interface\\Icons\\Spell_Nature_Windfury", Reborn = "Interface\\Icons\\Spell_Holy_Resurrection",
    Magnetic = "Interface\\Icons\\Trade_Engineering", Venomous = "Interface\\Icons\\Ability_Poisons",
}

-- layout (window pixels from the top left corner)
-- (the board texture is the parchment between x 217..1022 and y 200..440; the red band above it holds Bob and the
-- shields, the blue band below it my hero in its stone arch (556..669 x 444..557), the hand on both sides of it, and the
-- stone strip at the bottom edge the gold pill (788..850 x 597..617) with the coins to its right)
UI.RECT = {
    lobby = { 8, 40, 204, 520 },
    lobbyArt = { 151, 98, 50, 464 },     -- the portrait column on the board's left scroll
    bobArt = { 565, 68, 96, 112 },       -- the tavern arch's opening (the tier-up light)
    bobHero = { 555, 54 },               -- top left of Bob's golden arch over the tavern arch (the fight's opponent too)
    tavern = { 230, 222, 780, 88 },
    board = { 230, 322, 780, 88 },
    hand = { 230, 440, 780, 146 },        -- the hand: left of my hero (handLeft) and right of my power (handRight)
    handLeft = { 236, 545 },             -- x range of the left part
    handRight = { 752, 1016 },           -- x range of the right part (pushed right by the quest / reward icons)
    toast = { 620, 318, 520 },           -- centre x, y and the widest line of the notices over the board
    goldPill = { 814, 607 },             -- centre of the board's gold pill (789..840 x 600..616) at the bottom edge
    coins = { 847, 599 },                -- the first coin (top left), just right of the pill on the stone strip
    bob = { 563, 74, 100 },              -- x, y, width of the tavern arch
    myHero = { 553, 430 },               -- top left of my hero's golden arch, over the stone arch of the board
    myPower = { 688, 470, 56, 56 },      -- x, y, w, h: the hero power ring right of the arch
    myQuest = { 754, 480, 44 },          -- x, y, size: my quest icon with its progress, right of the power
    myReward = { 802, 480, 44 },         -- x, y, size: my reward icon once earned
    endTurn = { 960, 279, 114, 56 },     -- the stone slot at the right edge
    -- x of each shield, the shared top y and size; the Upgrade shield has its own size and y (upgradeW/H/Y)
    shields = { upgrade = 462, refresh = 692, freeze = 756, y = 96, w = 52, h = 73,
        upgradeW = 62, upgradeH = 83, upgradeY = 96 },
}

local TILE_COLOR = {
    BEAST = { 0.26, 0.20, 0.09 }, DEMON = { 0.24, 0.10, 0.26 }, DRAGON = { 0.26, 0.11, 0.11 },
    ELEMENTAL = { 0.12, 0.19, 0.26 }, MECHANICAL = { 0.18, 0.21, 0.23 }, MURLOC = { 0.10, 0.20, 0.24 },
    NAGA = { 0.09, 0.19, 0.21 }, PIRATE = { 0.21, 0.14, 0.07 }, QUILBOAR = { 0.21, 0.17, 0.09 },
    NEUTRAL = { 0.16, 0.13, 0.09 },
}
local KEYWORD_ABBR = {
    Taunt = "T", ["Divine Shield"] = "D", Poisonous = "P", Venomous = "V", Windfury = "W", Reborn = "R", Magnetic = "M",
    ["Mega-Windfury"] = "MW",
}

-- The HS_BoardTextures entry of a piece, or nil when the board art is not installed (BoardTextures.lua).
local function piece(name)
    local t = HS_BoardTextures
    if type(t) ~= "table" or type(t.pieces) ~= "table" then
        return nil
    end
    local p = t.pieces[name]
    if type(p) ~= "table" or type(p.path) ~= "string" or p.path == "" then
        return nil
    end
    return p
end
UI.Piece = piece

-- Puts a piece into a texture; false (texture untouched) when it is not installed.
local function setPiece(tex, name)
    local p = piece(name)
    if not p then
        return false
    end
    tex:SetTexture(p.path)
    tex:SetTexCoord(0, type(p.right) == "number" and p.right or 1, 0, type(p.bottom) == "number" and p.bottom or 1)
    tex:SetVertexColor(1, 1, 1)
    return true
end
UI.SetPiece = setPiece

UI.frame = nil
UI.driver = nil
UI.rows = {}              -- slot tables per zone: offers / board / hand / options
UI.drag = nil             -- { kind, index, id } while a tile is dragged
UI.sel = nil              -- { zone, index } picked up with a click
UI.ghost = nil
UI.powerArm = nil         -- a target power is armed for "f" (a friendly minion) or "o" (a tavern offer)
UI.powerArmRound = nil    -- the round it was armed in (a new round disarms it)
UI.powerFirst = nil       -- "m" (Vol'jin): the uid of the first chosen minion

local function fs(parent, font, justify)
    local s = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormal")
    s:SetJustifyH(justify or "LEFT")
    return s
end

local function tint(t, entry)
    local c = nil
    if entry then
        local card = BG.Card(entry.id)
        local race = card and card.tribes and card.tribes[1]
        c = race and TILE_COLOR[race] or TILE_COLOR.NEUTRAL
    end
    c = c or TILE_COLOR.NEUTRAL
    t:SetBackdropColor(c[1], c[2], c[3], 0.9)
end

-- Outlined text (the number font) for the gems and the shields' cost.
local function outline(parent, size, justify)
    local t = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    t:SetFont(UI.NUMBER_FONT, size, "OUTLINE")
    t:SetJustifyH(justify or "CENTER")
    return t
end
UI.Outline = outline

-- Fits a piece over the minion oval of a tile: the piece's "inner" anchor (the opening of the Taunt frame, the oval of
-- the Divine Shield) goes onto the oval of the minion_oval piece grown by `grow` pixels.
local function fitOverOval(tile, tex, name, grow)
    local p = piece(name)
    if not p then
        return
    end
    local hole = type(p.anchors) == "table" and p.anchors.inner or { left = 0, top = 0, right = 1, bottom = 1 }
    local mo = piece("minion_oval")
    local oval = mo and type(mo.anchors) == "table" and mo.anchors.oval or { left = 0.09, top = 0.074, right = 0.9, bottom = 0.926 }
    local w, h = tile.tw, tile.th
    local tl, tt = oval.left * w - grow, oval.top * h - grow
    local tr, tb = oval.right * w + grow, oval.bottom * h + grow
    local qw = (tr - tl) / (hole.right - hole.left)
    local qh = (tb - tt) / (hole.bottom - hole.top)
    tex:ClearAllPoints()
    tex:SetPoint("TOPLEFT", tile, "TOPLEFT", tl - hole.left * qw, -(tt - hole.top * qh))
    tex:SetWidth(qw)
    tex:SetHeight(qh)
    return tl - hole.left * qw, tt - hole.top * qh, qw, qh
end

-- Puts the tier shield of `tier` (1..UI.MAX_TIER, its stars) into a texture `w` wide (the height follows the shield's
-- aspect; tier 7 is a little taller). False when the shields are not installed.
function UI.SetTierShield(tex, tier, w)
    tier = math.max(1, math.min(UI.MAX_TIER, tier or 1))
    local p = piece("bg_tier_" .. tier)
    if not p or not setPiece(tex, "bg_tier_" .. tier) then
        return false
    end
    tex:SetWidth(w)
    tex:SetHeight(w * (p.h or 1) / (p.w or 1))
    return true
end

-- The visible body of a card render (256x388) and how far the hand glow reaches beyond it (the MatchUI defaults).
UI.CARD_BODY = { left = 10, top = 30, right = 236, bottom = 347 }
UI.CARD_GLOW_PAD = { x = 6, top = 0.1, bottom = 7 }

-- The halo of a hand card (glow_green / glow_yellow of the board pieces): a texture behind the card, over its body grown
-- by the pad (scaled to the card's width). nil when the piece is not installed.
function UI.MakeCardGlow(b, w, name, crop)
    local p = piece(name)
    if not p then
        return nil
    end
    local k, pk, v, pad = w / 256, w / 96, UI.CARD_BODY, UI.CARD_GLOW_PAD
    local tex = b:CreateTexture(nil, "BACKGROUND")
    setPiece(tex, name)
    tex:SetBlendMode("ADD")
    tex:SetPoint("TOPLEFT", b, "TOPLEFT", v.left * k - pad.x * pk, -(v.top * k - pad.top * pk))
    if crop then
        tex:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", v.right * k + pad.x * pk - w, 0)     -- ends with the visible strip
    else
        tex:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", v.right * k + pad.x * pk, -(v.bottom * k + pad.bottom * pk))
    end
    tex:Hide()
    return { tex = tex, frame = tex, piece = name }
end

-- A Battlegrounds minion tile (a Button so it takes clicks; the caller sets the scripts). With the board art it is the
-- minion oval + portrait + Taunt frame / Divine Shield + gems; without it the plain tinted tile.
function UI.CreateTile(parent, name, w, h, crop)
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(w)
    b:SetHeight(h)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")     -- the right click cancels a pick-up / an armed power
    b.tw, b.th = w, h
    b.crop = crop and true or false         -- a resting hand card: only its top strip is drawn
    b.cardH = crop and UI.HAND_H or h
    b.artMode = piece("minion_oval") ~= nil
    local overlay = b                       -- the frame the numbers sit on (above the Divine Shield in art mode)
    if b.artMode then
        -- the selection outline only: no fill, and the border stays clear until a tile is picked up
        b:SetBackdrop({ edgeFile = UI.EDGE, edgeSize = 10, insets = { left = 2, right = 2, top = 2, bottom = 2 } })
        b.bg = b:CreateTexture(nil, "BORDER")         -- the glow lies under it, in BACKGROUND
        b.bg:SetAllPoints(b)
        setPiece(b.bg, "minion_oval")
        -- the portrait fills the minion_oval piece's "oval" anchor (the pre-cut oval with its soft edge, like
        -- MatchUI's SetMinionArt); the golden ring lies on the same oval
        local mo = piece("minion_oval")
        local oval = mo and type(mo.anchors) == "table" and mo.anchors.oval or { left = 0.09, top = 0.074, right = 0.9, bottom = 0.926 }
        local ol, ot = math.floor(oval.left * w + 0.5), math.floor(oval.top * h + 0.5)
        local orr, ob = math.floor(oval.right * w + 0.5), math.floor(oval.bottom * h + 0.5)
        b.art = b:CreateTexture(nil, "ARTWORK")
        b.art:SetPoint("TOPLEFT", b, "TOPLEFT", ol, -ot)
        b.art:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", orr, -ob)
        b.art:Hide()
        b.ring = b:CreateTexture(nil, "OVERLAY")
        b.ring:SetPoint("TOPLEFT", b, "TOPLEFT", ol, -ot)
        b.ring:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", orr, -ob)
        b.ring:Hide()
        if piece("taunt_frame") then
            b.taunt = b:CreateTexture(nil, "OVERLAY")
            setPiece(b.taunt, "taunt_frame")
            fitOverOval(b, b.taunt, "taunt_frame", 0)
            b.taunt:Hide()
        end
        local shieldLayer = CreateFrame("Frame", nil, b)
        b.shieldLayer = shieldLayer
        shieldLayer:SetAllPoints(b)
        shieldLayer:SetFrameLevel(b:GetFrameLevel() + 2)
        if piece("divine_shield") then
            b.shield = shieldLayer:CreateTexture(nil, "OVERLAY")
            setPiece(b.shield, "divine_shield")
            fitOverOval(b, b.shield, "divine_shield", w * 0.18)
            b.shield:Hide()
        end
        -- the ice ring of a frozen tavern offer
        if piece("frozen_ice") then
            b.ice = shieldLayer:CreateTexture(nil, "ARTWORK")
            setPiece(b.ice, "frozen_ice")
            fitOverOval(b, b.ice, "frozen_ice", 2)
            b.ice:Hide()
        end
        overlay = CreateFrame("Frame", nil, b)
        overlay:SetAllPoints(b)
        overlay:SetFrameLevel(b:GetFrameLevel() + 6)
        local gem = w * 0.44
        b.gemA = overlay:CreateTexture(nil, "BACKGROUND")
        b.gemA:SetWidth(gem)
        b.gemA:SetHeight(gem)
        b.gemA:SetPoint("CENTER", b, "BOTTOMLEFT", w * 0.10, h * 0.09)
        setPiece(b.gemA, "gem_attack")
        b.gemH = overlay:CreateTexture(nil, "BACKGROUND")
        b.gemH:SetWidth(gem)
        b.gemH:SetHeight(gem)
        b.gemH:SetPoint("CENTER", b, "BOTTOMRIGHT", -w * 0.10, h * 0.09)
        setPiece(b.gemH, "gem_health")
        -- the tier pips of a tavern offer above the oval, the keyword badges at its bottom
        b.pips = {}
        for i = 1, UI.PIPS do
            local pip = overlay:CreateTexture(nil, "OVERLAY")
            pip:SetTexture(UI.BG_PATH .. "tier_pip")
            pip:SetWidth(10)
            pip:SetHeight(10)
            pip:Hide()
            b.pips[i] = pip
        end
        -- with the art: the tier shield on top of the oval instead of the pips
        if piece("bg_tier_1") then
            b.tierShield = overlay:CreateTexture(nil, "OVERLAY")
            b.tierShield:Hide()
        end
        b.badges = {}
        for i = 1, 4 do
            local bd = overlay:CreateTexture(nil, "OVERLAY")
            bd:SetWidth(UI.KW_ICON)
            bd:SetHeight(UI.KW_ICON)
            bd:Hide()
            b.badges[i] = bd
        end
    else
        b:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 10,
            insets = { left = 2, right = 2, top = 2, bottom = 2 } })
        b:SetBackdropBorderColor(0.35, 0.30, 0.20, 1)
        b.art = b:CreateTexture(nil, "ARTWORK")
        b.art:SetWidth(w)
        b.art:SetHeight(w)
        b.art:SetPoint("TOP", b, "TOP", 0, -14)
        b.art:Hide()
        -- the gold ring a golden minion wears, drawn over its (normal twin's) portrait
        b.ring = b:CreateTexture(nil, "OVERLAY")
        b.ring:SetWidth(w)
        b.ring:SetHeight(w)
        b.ring:SetPoint("TOP", b, "TOP", 0, -14)
        b.ring:Hide()
    end
    -- the glow around the oval: gold on hover and for a picked-up minion, green for a playable hand card
    -- (behind the tile: the silhouette of the oval, of the Taunt frame while the minion has Taunt)
    if b.artMode then
        b.glow = V.MakeGlow(b, "glow_minion_oval", w, h, 0)
        if b.taunt and piece("glow_taunt") then
            local l, t, tw, th = fitOverOval(b, b.taunt, "taunt_frame", 0)
            b.tauntGlow = V.MakeGlow(b, "glow_taunt", tw, th, 0, l, t)
        end
    else
        b.glow = V.MakeGlow(b, "oval", w, h, 0)
    end
    -- a hand card is the whole card (UI.FillCardTile): its picture, and the green / gold halo around the card body
    if b.artMode then
        b.cardTex = b:CreateTexture(nil, "ARTWORK")
        b.cardTex:SetAllPoints(b)
        b.cardTex:Hide()
        b.cardGlows = { green = UI.MakeCardGlow(b, w, "glow_green", crop), gold = UI.MakeCardGlow(b, w, "glow_yellow", crop) }
    end
    b.tier = fs(overlay, "GameFontNormalSmall", "LEFT")
    b.tier:SetPoint("TOPLEFT", b, "TOPLEFT", 4, -3)
    b.golden = fs(overlay, "GameFontNormalSmall", "RIGHT")
    b.golden:SetPoint("TOPRIGHT", b, "TOPRIGHT", -4, -3)
    b.golden:SetText("|cffffd100G|r")
    b.golden:Hide()
    b.name = fs(overlay, "GameFontNormalSmall", "CENTER")
    b.name:SetPoint("TOP", b, "TOP", 0, -16)
    b.name:SetWidth(w - 8)
    b.name:SetHeight(26)
    b.kw = fs(overlay, "GameFontNormalSmall", "CENTER")
    b.kw:SetPoint("BOTTOM", b, "BOTTOM", 0, b.artMode and 4 or 20)
    b.kw:SetWidth(b.artMode and (w * 0.3) or (w - 6))
    b.kw:SetHeight(14)
    if b.artMode then
        b.atk = outline(overlay, math.floor(w * 0.21), "CENTER")
        b.atk:SetPoint("CENTER", b.gemA, "CENTER", w * 0.02, -h * 0.05)
        b.hp = outline(overlay, math.floor(w * 0.21), "CENTER")
        b.hp:SetPoint("CENTER", b.gemH, "CENTER", 0, -h * 0.02)
        b.atk:SetWidth(w)
        b.hp:SetWidth(w)
    else
        b.atk = fs(overlay, "GameFontNormalLarge", "LEFT")
        b.atk:SetPoint("BOTTOMLEFT", b, "BOTTOMLEFT", 6, 4)
        b.hp = fs(overlay, "GameFontNormalLarge", "RIGHT")
        b.hp:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -6, 4)
    end
    -- Silas Darkmoon's ticket badge in the top-right corner of the offer (art mode or plain)
    b.ticket = b:CreateTexture(nil, "OVERLAY")
    if not setPiece(b.ticket, "bg_coin") then
        b.ticket:SetTexture(UI.BG_PATH .. "coin")
    end
    b.ticket:SetWidth(18)
    b.ticket:SetHeight(18)
    b.ticket:SetPoint("TOPRIGHT", b, "TOPRIGHT", -2, -2)
    b.ticket:Hide()
    -- the child frames that must rise with the tile when it is lifted above the others (a lunge; 1.12 does not raise
    -- the children of a frame with it)
    b.subs = {}
    if b.shieldLayer then
        table.insert(b.subs, b.shieldLayer)
    end
    if overlay ~= b then
        table.insert(b.subs, overlay)
    end
    b.float = fs(overlay, "GameFontNormalLarge", "CENTER")
    b.float:SetPoint("CENTER", b, "CENTER", 0, 0)
    b.float:Hide()
    b:SetScript("OnEnter", function() UI.OnTileEnter(this) end)
    b:SetScript("OnLeave", function() UI.OnTileLeave(this) end)
    return b
end

-- The card a minion shows: the golden twin's own text and stats when the minion is a golden one.
function UI.EntryCard(entry)
    local card = BG.Card(entry.id)
    if entry.golden and card and card.golden then
        return BG.Card(card.golden) or card
    end
    return card
end

-- Shows a minion on a tile (or empties it when entry is nil). mode: "offer" | "board" | "hand" | "combat" | "empty".
function UI.FillTile(t, entry, mode)
    t.entry = entry
    t.mode = mode
    if t.artMode then
        return UI.FillArtTile(t, entry, mode)
    end
    if not entry then
        t.name:SetText("")
        t.atk:SetText("")
        t.hp:SetText("")
        t.tier:SetText("")
        t.kw:SetText("")
        t.golden:Hide()
        t.art:Hide()
        t.ring:Hide()
        t:SetBackdropColor(0.08, 0.07, 0.05, 0.5)
        t:SetBackdropBorderColor(0.25, 0.22, 0.16, 1)
        if t.ticket then
            t.ticket:Hide()
        end
        return
    end
    local card = UI.EntryCard(entry)
    local name = card and card.name or entry.id
    local spell = card ~= nil and card.spell == 1
    t.name:SetText((entry.golden and "|cffffd100" or "|cffffffff") .. name .. "|r")
    if spell then
        t.atk:SetText("")
        t.hp:SetText("")
        t.tier:SetText("|cffb8b8b8Spell|r")
        t.kw:SetText(entry.temp and "|cffb8b8b8until next turn|r" or "")
    else
        t.atk:SetText("|cffffd100" .. entry.atk .. "|r")
        t.hp:SetText("|cffff5555" .. entry.hp .. "|r")
        t.tier:SetText(card and ("T" .. card.tier) or "")
        local names = BG.PlainKeywords(entry.kw or 0)
        local abbr = {}
        for i = 1, table.getn(names) do
            table.insert(abbr, KEYWORD_ABBR[names[i]] or names[i])
        end
        t.kw:SetText("|cffffd100" .. table.concat(abbr, " ") .. "|r")
    end
    tint(t, entry)
    if HS_BgTextures and HS_BgTextures.set and HS_BgTextures.set[entry.id] then
        t.art:SetTexture(UI.ART_PATH .. entry.id)
        t.art:Show()
    else
        t.art:Hide()
    end
    if entry.golden and t.art:IsShown() then
        t.ring:SetTexture(UI.BG_PATH .. "golden_ring")
        t.ring:Show()
        t.golden:Hide()                 -- the ring says it
    else
        t.ring:Hide()
        if entry.golden then            -- no portrait: a small gold marker stands in for the ring
            t.golden:Show()
        else
            t.golden:Hide()
        end
    end
    t.name:ClearAllPoints()
    if t.art:IsShown() then
        t.name:SetPoint("TOP", t, "TOP", 0, -2)
    else
        t.name:SetPoint("TOP", t, "TOP", 0, -16)
    end
    if mode == "offer" then
        t:SetBackdropBorderColor(0.55, 0.45, 0.20, 1)
    elseif mode == "empty" then
        t:SetBackdropBorderColor(0.20, 0.35, 0.20, 1)
    else
        t:SetBackdropBorderColor(0.35, 0.30, 0.20, 1)
    end
    -- A Darkmoon Ticket on the offer (Silas Darkmoon)
    if t.ticket then
        if mode == "offer" and entry.ticket then
            t.ticket:Show()
        else
            t.ticket:Hide()
        end
    end
end

-- The mechanic badge of a card from its text: a skull for a Deathrattle, a lightning bolt for a trigger (as in the game).
local TRIGGER_WORDS = { "Avenge", "Start of Combat", "Whenever", "After ", "At the end", "At the start" }
function UI.MechanicOf(card)
    local text = card and card.text or ""
    if string.find(text, "Deathrattle", 1, true) then
        return "kw_deathrattle"
    end
    for i = 1, table.getn(TRIGGER_WORDS) do
        if string.find(text, TRIGGER_WORDS[i], 1, true) then
            return "kw_continuous"
        end
    end
    return nil
end

-- the centers of the printed attack / health digits in the 256x388 render (patch_renders.GEM_BOXES)
UI.GEM_A_X, UI.GEM_A_Y = 44.5, 319
UI.GEM_H_X, UI.GEM_H_Y = 212.5, 320.5

-- The look of the numbers on a tile: the card's (small gems over the printed ones, the keyword line above the card) or
-- the oval's (the sizes of CreateTile).
function UI.SetCardLook(t, card)
    local w, h = t.tw, t.cardH
    local gem = card and w * 0.24 or w * 0.44
    t.gemA:SetWidth(gem)
    t.gemA:SetHeight(gem)
    t.gemH:SetWidth(gem)
    t.gemH:SetHeight(gem)
    t.gemA:ClearAllPoints()
    t.gemH:ClearAllPoints()
    if card then
        -- the attack piece's circle sits below-right of its texture center (the sword sticks out), so shift the texture
        t.gemA:SetPoint("CENTER", t, "TOPLEFT", w * UI.GEM_A_X / 256 - gem * 0.04, -h * UI.GEM_A_Y / 388 + gem * 0.11)
        t.gemH:SetPoint("CENTER", t, "TOPLEFT", w * UI.GEM_H_X / 256, -h * UI.GEM_H_Y / 388)
    else
        t.gemA:SetPoint("CENTER", t, "BOTTOMLEFT", w * 0.10, h * 0.09)
        t.gemH:SetPoint("CENTER", t, "BOTTOMRIGHT", -w * 0.10, h * 0.09)
    end
    local size = math.floor(w * (card and 0.15 or 0.21))
    t.atk:SetFont(UI.NUMBER_FONT, size, "OUTLINE")
    t.hp:SetFont(UI.NUMBER_FONT, size, "OUTLINE")
    t.atk:ClearAllPoints()
    t.hp:ClearAllPoints()
    if card then
        t.atk:SetPoint("CENTER", t.gemA, "CENTER", gem * 0.04, -gem * 0.11 - 1)
        t.hp:SetPoint("CENTER", t.gemH, "CENTER", 0, -1)
    else
        t.atk:SetPoint("CENTER", t.gemA, "CENTER", w * 0.02, -h * 0.05)
        t.hp:SetPoint("CENTER", t.gemH, "CENTER", 0, -h * 0.02)
    end
    -- wider than the gem, so a two-digit number stays on one line
    t.atk:SetWidth(w)
    t.hp:SetWidth(w)
    t.kw:ClearAllPoints()
    if card then
        t.kw:SetPoint("BOTTOM", t, "TOP", 0, 2)
        t.kw:SetWidth(w + 40)
    else
        t.kw:SetPoint("BOTTOM", t, "BOTTOM", 0, 4)
        t.kw:SetWidth(w * 0.3)
    end
end

-- A hand card: the whole card (its render), a tier shield over the cost gem the render prints (a Battlegrounds minion
-- costs Gold, not mana; its tier is what the card shows), and a gem over a printed number that is not the current one.
function UI.FillCardTile(t, entry, card, cid)
    t.cardMode = true
    t.bg:SetAlpha(0)
    t.art:Hide()
    t.ring:Hide()
    t.name:Hide()
    t.cardTex:SetTexture(V.CARD_PATH .. cid)
    t.cardPath = V.CARD_PATH .. cid
    UI.CropCard(t)
    t.cardTex:Show()
    UI.SetCardLook(t, true)
    t.kw:SetText((entry.temp and not t.crop) and "|cffb8b8b8until next turn|r" or "")
    local tier = card and card.tier or 0
    if tier > 0 and t.tierShield and UI.SetTierShield(t.tierShield, tier, t.tw * 0.3) then
        t.tierShield:ClearAllPoints()
        t.tierShield:SetPoint("CENTER", t, "TOPLEFT", t.tw * 0.14, -t.cardH * 0.16)
        t.tierShield:Show()
    end
    t.gemA:Hide()
    t.gemH:Hide()
    t.atk:SetText("")
    t.hp:SetText("")
    if card and card.spell ~= 1 and card.atk and not t.crop then      -- the numbers are at the card's foot: only while it shows whole
        if entry.atk ~= card.atk then
            t.gemA:Show()
            t.atk:SetText((entry.atk > card.atk and "|cff4cff4c" or "|cffff5555") .. entry.atk .. "|r")
        end
        if entry.hp ~= card.hp then
            t.gemH:Show()
            t.hp:SetText((entry.hp > card.hp and "|cff4cff4c" or "|cffff5555") .. entry.hp .. "|r")
        end
    end
    t.art:SetVertexColor(1, 1, 1)
    if t.restX then
        UI.TiltCard(t)
    end
end

-- The part of the card's render a card tile shows: all of it, or the top strip of a resting hand card.
function UI.CropCard(t)
    local bottom = V.CARD_H / 512
    if t.crop then
        bottom = bottom * math.min(1, t.th / t.cardH)
    end
    t.cardTex:SetTexCoord(0, 1, 0, bottom)
end

-- The board-art version of FillTile: the oval, the portrait (the name stands in while there is none), Taunt frame and
-- Divine Shield from the keywords, white outlined numbers on the gems (green above the printed ones), the tier pips of a
-- tavern offer, the keyword and mechanic badges, the ice of a frozen offer. The border stays clear (UI.TileGlow marks).
function UI.FillArtTile(t, entry, mode)
    t:SetBackdropBorderColor(0, 0, 0, 0)
    if t.taunt then
        t.taunt:Hide()
    end
    if t.shield then
        t.shield:Hide()
    end
    if t.ice then
        t.ice:Hide()
    end
    for i = 1, table.getn(t.pips) do
        t.pips[i]:Hide()
    end
    if t.tierShield then
        t.tierShield:Hide()
    end
    for i = 1, table.getn(t.badges) do
        t.badges[i]:Hide()
    end
    t.golden:Hide()
    t.tier:SetText("")
    t.cardMode = false
    if t.cardTex then
        t.cardTex:Hide()
        UI.SetCardLook(t, false)
        UI.SetCardGlow(t, nil)      -- (also clears the strips of a tilted card: UI.TiltCard)
    end
    if t.popped and not entry then
        UI.HandUnpop(t)
    end
    if not entry then
        t.bg:SetAlpha(0)                    -- an empty slot is only a place to drop on (it glows while it is the target)
        t.name:SetText("")
        t.atk:SetText("")
        t.hp:SetText("")
        t.kw:SetText("")
        t.art:Hide()
        t.ring:Hide()
        t.gemA:Hide()
        t.gemH:Hide()
        if t.ticket then
            t.ticket:Hide()
        end
        return
    end
    t.bg:SetAlpha(1)
    t.gemA:Show()
    t.gemH:Show()
    local card = UI.EntryCard(entry)
    local name = card and card.name or entry.id
    local cid = (mode == "hand" and t.cardTex and not t.noCard) and V.CardArtId(entry) or nil
    if cid then
        return UI.FillCardTile(t, entry, card, cid)
    end
    if card and card.spell == 1 then
        -- A spell shows its name and its "until next turn" mark, with no gems.
        t.gemA:Hide()
        t.gemH:Hide()
        t.atk:SetText("")
        t.hp:SetText("")
        t.name:SetText((entry.golden and "|cffffd100" or "|cffffffff") .. name .. "|r")
        t.name:ClearAllPoints()
        t.name:SetPoint("CENTER", t, "CENTER", 0, 4)
        t.name:SetWidth(t.tw * 0.7)
        t.kw:SetText(entry.temp and "|cffb8b8b8until next turn|r" or "")
        if HS_BgTextures and HS_BgTextures.set and HS_BgTextures.set[entry.id] then
            t.art:SetTexture(UI.ART_PATH .. entry.id)       -- the spell's own picture (Blood Gem, Banana, Gold Coin)
            t.art:SetVertexColor(1, 1, 1)
            t.art:Show()
            t.name:Hide()
        else
            t.art:Hide()
            t.name:Show()
        end
        t.ring:Hide()
        if entry.golden then
            t.golden:Show()
        end
        return
    end
    t.name:SetText((entry.golden and "|cffffd100" or "|cffffffff") .. name .. "|r")
    local atkC = (card and card.atk and entry.atk > card.atk) and "|cff4cff4c" or "|cffffffff"
    local hpC = (card and card.hp and entry.hp > card.hp) and "|cff4cff4c" or "|cffffffff"
    if card and card.hp and entry.hp < card.hp and mode == "combat" then
        hpC = "|cffff5555"
    end
    t.atk:SetText(atkC .. entry.atk .. "|r")
    t.hp:SetText(hpC .. entry.hp .. "|r")
    -- a tavern offer shows its tier: the tier shield on top of the oval, or pips above it without the art
    local tier = card and card.tier or 0
    if mode == "offer" and tier > 0 and t.tierShield and UI.SetTierShield(t.tierShield, tier, UI.OFFER_SHIELD_W) then
        t.tierShield:ClearAllPoints()
        t.tierShield:SetPoint("CENTER", t, "TOP", 0, -3)
        t.tierShield:Show()
    elseif mode == "offer" and tier > 0 then
        local step = 9
        local x0 = (t.tw - (tier * step - 1)) / 2
        for i = 1, tier do
            local pip = t.pips[i]
            if pip then
                pip:ClearAllPoints()
                pip:SetPoint("TOPLEFT", t, "TOPLEFT", x0 + (i - 1) * step - 1, 2)
                pip:Show()
            end
        end
    end
    local names = BG.PlainKeywords(entry.kw or 0)
    local abbr = {}
    local icons = {}
    for i = 1, table.getn(names) do
        if names[i] == "Taunt" and t.taunt then
            t.taunt:Show()
        elseif names[i] == "Divine Shield" and t.shield then
            t.shield:Show()
        elseif UI.KW_ICONS[names[i]] then
            table.insert(icons, { icon = UI.KW_ICONS[names[i]] })
        else
            table.insert(abbr, KEYWORD_ABBR[names[i]] or names[i])
        end
    end
    local mech = UI.MechanicOf(card)
    if mech and piece(mech) then
        table.insert(icons, 1, { piece = mech })
    end
    local n = table.getn(icons)
    if n > table.getn(t.badges) then
        n = table.getn(t.badges)
    end
    local size = UI.KW_ICON
    local x0 = (t.tw - (n * (size + 1) - 1)) / 2
    for i = 1, n do
        local bd = t.badges[i]
        if icons[i].piece then
            setPiece(bd, icons[i].piece)
        else
            bd:SetTexture(icons[i].icon)
            bd:SetTexCoord(0.08, 0.92, 0.08, 0.92)
            bd:SetVertexColor(1, 1, 1)
        end
        bd:ClearAllPoints()
        bd:SetPoint("TOPLEFT", t, "BOTTOMLEFT", x0 + (i - 1) * (size + 1), size - 2)
        bd:Show()
    end
    t.kw:SetText(table.getn(abbr) > 0 and ("|cffffd100" .. table.concat(abbr, " ") .. "|r") or "")
    if HS_BgTextures and HS_BgTextures.set and HS_BgTextures.set[entry.id] then
        t.art:SetTexture(UI.ART_PATH .. entry.id)
        t.art:Show()
        t.name:Hide()                   -- the card names it
    else
        t.art:Hide()
        t.name:Show()
    end
    t.name:ClearAllPoints()
    t.name:SetPoint("CENTER", t, "CENTER", 0, 4)
    t.name:SetWidth(t.tw * 0.6)
    -- a golden minion's frame is gold instead of the normal stone one (no second ring on top of it)
    t.ring:Hide()
    if entry.golden then
        t.bg:SetVertexColor(1, 0.8, 0.32)
        if not t.art:IsShown() then
            t.golden:Show()
        end
    else
        t.bg:SetVertexColor(1, 1, 1)
    end
    -- a frozen offer wears the ice ring and a cold tint
    if mode == "offer" and entry.frozen then
        if t.ice then
            t.ice:Show()
        end
        t.art:SetVertexColor(0.7, 0.85, 1)
    else
        t.art:SetVertexColor(1, 1, 1)
    end
    -- A Darkmoon Ticket on the offer (Silas Darkmoon)
    if t.ticket then
        if mode == "offer" and entry.ticket then
            t.ticket:Show()
        else
            t.ticket:Hide()
        end
    end
end

-- The tile's glow: its resting colour (t.restGlow: a picked-up minion gold, a playable hand card green, a gift gold),
-- or a light gold while the cursor is on it.
-- The glow a tile shows now: the Taunt frame's silhouette while the Taunt frame is up, else the oval's.
function UI.GlowOf(t)
    if t.tauntGlow and t.taunt and t.taunt:IsShown() then
        return t.tauntGlow, t.glow
    end
    return t.glow, t.tauntGlow
end

-- Colours a tile's glow (nil: off), hiding the other shape.
function UI.SetTileGlow(t, color, alpha)
    local g, other = UI.GlowOf(t)
    if other then
        V.SetGlow(other, nil)
    end
    V.SetGlow(g, color, alpha)
end

-- The halo of a hand card: green (playable) or gold (picked up, a gift); without the glow pieces the oval's glow.
function UI.SetCardGlow(t, color)
    for _, g in pairs(t.cardGlows or {}) do
        V.SetGlow(g, nil)
    end
    local g = t.cardGlows and (color == "green" or color == "gold") and t.cardGlows[color]
    if g then
        UI.SetTileGlow(t, nil)
        V.SetGlow(g, "white", 1)
    elseif t.cardMode then
        UI.SetTileGlow(t, color, 1)
    end
    UI.TiltCard(t)
end

-- Frame levels of a tile and of the frames it keeps over its picture, from `base`.
function UI.LevelTile(t, base)
    if t.popped then
        base = base + UI.HAND_POP_LEVEL
    end
    t:SetFrameLevel(base)
    for i = 1, table.getn(t.subs or {}) do
        t.subs[i]:SetFrameLevel(base + (t.subs[i] == t.shieldLayer and 2 or 6))
    end
end

-- Leans a hand card as the fan wants (t.restAngle, radians clockwise; upright while it is hovered): the card picture and
-- the halo are drawn as strips (CardWidget.PlaceStrips, the 1.12 client cannot rotate a texture), and the tier shield and
-- the changed-number gems at the card's corners follow it (CardWidget.TiltPoint).
function UI.TiltCard(t)
    local W = H.CardWidget
    if not t.cardTex or not W or not W.PlaceStrips then
        return
    end
    local angle = 0
    if t.cardMode and not t.popped and t.cardPath then
        angle = t.restAngle or 0
    end
    local tw, th = t.tw, t.th
    local k = tw / 256
    local src = W.TILT_SRC
    local px, py = (src.left + src.right) / 2 * k, (src.top + src.bottom) / 2 * k      -- the card turns around its middle
    t.cardStrips = t.cardStrips or {}
    if angle ~= 0 then
        local bottom = V.CARD_H / 512
        local rect = { l = src.left * k, t = src.top * k, r = src.right * k, b = src.bottom * k }
        t.cardTex:Hide()
        W.PlaceStrips(t.cardStrips, t, "ARTWORK", t.cardPath, rect, px, py,
            { a = px / tw, b = 1 / tw, c = py / th * bottom, d = bottom / th }, angle, UI.HAND_STRIPS)
    else
        W.HideStrips(t.cardStrips)
        if t.cardMode then
            t.cardTex:Show()
        end
    end
    -- the halo: its texture while upright, strips while tilted
    local v, pad, pk = UI.CARD_BODY, UI.CARD_GLOW_PAD, tw / 96
    t.glowStrips = t.glowStrips or {}
    for name, g in pairs(t.cardGlows or {}) do
        local strips = t.glowStrips[name]
        if not strips then
            strips = {}
            t.glowStrips[name] = strips
        end
        local p = piece(g.piece)
        if angle ~= 0 and g.color and p then
            local rect = { l = v.left * k - pad.x * pk, t = v.top * k - pad.top * pk,
                r = v.right * k + pad.x * pk, b = v.bottom * k + pad.bottom * pk }
            local right = type(p.right) == "number" and p.right or 1
            local bottom = type(p.bottom) == "number" and p.bottom or 1
            local rw, rh = rect.r - rect.l, rect.b - rect.t
            g.tex:Hide()
            W.PlaceStrips(strips, t, "BACKGROUND", p.path, rect, px, py,
                { a = right * (px - rect.l) / rw, b = right / rw, c = bottom * (py - rect.t) / rh, d = bottom / rh },
                angle, UI.HAND_GLOW_STRIPS, function(tex) tex:SetBlendMode("ADD") end)
        else
            W.HideStrips(strips)
            if g.color then
                g.tex:Show()
            end
        end
    end
    if t.cardMode then
        local function put(frame, x, y)
            x, y = W.TiltPoint(x, y, angle)
            frame:ClearAllPoints()
            frame:SetPoint("CENTER", t, "CENTER", x, y)
        end
        if t.tierShield and t.tierShield:IsShown() then
            put(t.tierShield, -tw * 0.36, th * 0.34)
        end
        -- the printed digits (render coordinates) turn around the card's middle (px, py), as the picture does
        local function putAt(frame, rx, ry)
            local x, y = W.TiltPoint(rx * k - px, py - ry * k, angle)
            frame:ClearAllPoints()
            frame:SetPoint("CENTER", t, "CENTER", px - tw / 2 + x, th / 2 - py + y)
        end
        putAt(t.gemA, UI.GEM_A_X, UI.GEM_A_Y)
        putAt(t.gemH, UI.GEM_H_X, UI.GEM_H_Y)
    end
end

-- A hand card goes to its place: at rest (t.restX / t.restY from the hand row's top left, the fan) or, while the
-- cursor is on it, grown from its bottom edge (the tile's own scale; the offsets of a scaled frame are in its units).
function UI.HandPlace(t)
    local row = UI.handRow
    t:ClearAllPoints()
    if t.popped then
        local s = UI.HAND_POP_W / t.tw
        local left = row.rx + t.restX + t.tw / 2 - UI.HAND_POP_W / 2
        if left < 4 then
            left = 4
        elseif left > UI.W - UI.HAND_POP_W - 4 then
            left = UI.W - UI.HAND_POP_W - 4
        end
        t:SetScale(s)
        t:SetPoint("BOTTOMLEFT", row, "TOPLEFT", (left - row.rx) / s, -(t.restY + t.th) / s)
        -- the mouse area stays the resting card's (as the slot frame does in the match window): the grown card does
        -- not hold the cursor over its neighbours, so moving onto the next card's place hovers that card
        if t.SetHitRectInsets then
            local rl = row.rx + t.restX
            -- as in the match window the card to the right stays in front for the mouse: the hit area ends where the next
            -- card's resting place begins, so moving right hovers that card at once (the grown card is only drawn above it)
            local rr = rl + t.tw
            for i = 1, table.getn(UI.hand or {}) do
                local o = UI.hand[i]
                if o ~= t and o.restX and o.entry and o:IsShown() and o.restX > t.restX and row.rx + o.restX < rr then
                    rr = row.rx + o.restX
                end
            end
            t:SetHitRectInsets(math.max(0, (rl - left) / s), math.max(0, (left + UI.HAND_POP_W - rr) / s),
                t.th - t.th / s, 0)
        end
    else
        t:SetScale(1)
        t:SetPoint("TOPLEFT", row, "TOPLEFT", t.restX, -t.restY)
        if t.SetHitRectInsets then
            t:SetHitRectInsets(0, 0, 0, 0)
        end
    end
    UI.TiltCard(t)
end

function UI.HandPop(t)
    if t.popped or UI.drag then
        return
    end
    t.popped = true
    UI.LevelTile(t, t.band)
    UI.HandPlace(t)
end

function UI.HandUnpop(t)
    if not t.popped then
        return
    end
    t.popped = nil
    UI.LevelTile(t, t.band)
    UI.HandPlace(t)
end

function UI.HandUnpopAll()
    for i = 1, table.getn(UI.hand or {}) do
        UI.HandUnpop(UI.hand[i])
    end
end

-- True while a spell of the hand is picked up (by a click or a drag): the board shows targets, not play slots.
function UI.SpellPicked(m)
    local d = UI.drag
    local zone, index = nil, nil
    if UI.sel and UI.sel.zone == "hand" then
        zone, index = "hand", UI.sel.index
    elseif d and d.kind == "hand" then
        zone, index = "hand", d.index
    end
    local entry = zone and m and m.hand and m.hand[index + 1]
    return entry ~= nil and BG.IsSpell(entry)
end

-- The picked-up spell (clicked or dragged) that must be cast on a friendly minion, with its hand index; else nil.
function UI.TargetSpell(m)
    local d = UI.drag
    local index = nil
    if UI.sel and UI.sel.zone == "hand" then
        index = UI.sel.index
    elseif d and d.kind == "hand" then
        index = d.index
    end
    local entry = index and m and m.hand and m.hand[index + 1]
    if entry and BG.IsSpell(entry) and BG.SpellNeedsTarget(entry) then
        return entry, index
    end
    return nil
end

function UI.TileGlow(t)
    if not t or not t.glow then
        return
    end
    if not t.entry then
        if t.previewGlow or (t.hovered and (UI.sel or UI.drag) and not UI.SpellPicked(BG.model)) then
            UI.SetTileGlow(t, "gold", 0.6)        -- the empty slot a picked-up minion would go to
        else
            UI.SetTileGlow(t, nil)
        end
        return
    end
    if t.cardMode then
        UI.SetCardGlow(t, t.restGlow)              -- a hand card has its halo only; the hover raises the card instead
        return
    end
    if t.magnetGlow then
        UI.SetTileGlow(t, "ice", 1)               -- the Mech a dragged Magnetic minion would merge into
    elseif t.restGlow then
        UI.SetTileGlow(t, t.restGlow, 1)
    elseif t.hovered then
        UI.SetTileGlow(t, "gold", 0.8)
    else
        UI.SetTileGlow(t, nil)
    end
end

-- ------------------------------------------------------------------ hover
-- The allies a hovered card would help (the wire `nd` entry of its uid), or nil.
local function NeedsOf(t)
    local m = BG.model
    if not m or not m.need or not t.entry then
        return nil
    end
    return m.need[t.entry.uid]
end

-- The lobby glow of a hovered card: mark its ally cells while the cursor is on the card.
local function SetNeedSeats(needs)
    UI.needSeats = needs
    if H.BgLobbyUI and H.BgLobbyUI.SetNeedSeats then
        H.BgLobbyUI.SetNeedSeats(needs)
    end
end

-- A minion under the cursor: its glow and its large card beside it (the tooltip of a card without card art).
function UI.OnTileEnter(t)
    UI.cardTile, UI.cardEntry = nil, nil
    local needs = NeedsOf(t)
    SetNeedSeats(needs)
    t.hovered = true
    UI.TileGlow(t)
    if not t.entry or UI.drag then
        return
    end
    if t.cardMode then
        if t.restX then
            UI.HandPop(t)                -- the card itself grows; no second card beside it (a Discover card has no rest place)
        end
        if needs then
            V.ShowNeedsOnly(t, UI.frame, needs)       -- the ally plate still shows
            UI.cardTile, UI.cardEntry = t, t.entry
        end
        return
    end
    if V.ShowCard(t.entry, t, UI.frame, needs) then
        UI.cardTile, UI.cardEntry = t, t.entry       -- HoverCardTick takes it down if the tile changes under it
        return
    end
    if not GameTooltip then
        return
    end
    local e = t.entry
    local card = UI.EntryCard(e)
    GameTooltip:SetOwner(t, "ANCHOR_RIGHT")
    GameTooltip:SetText((e.golden and "Golden " or "") .. BG.CardName(card and card.id or e.id))
    local tribes = BG.CardTribesText(e.id)
    if card and card.spell == 1 then
        GameTooltip:AddLine("Spell", 0.7, 0.7, 0.7)
    elseif tribes ~= "" then
        GameTooltip:AddLine(tribes .. (card and (" - tier " .. card.tier) or ""), 0.7, 0.7, 0.7)
    elseif card then
        GameTooltip:AddLine("tier " .. card.tier, 0.7, 0.7, 0.7)
    end
    if card and card.text and card.text ~= "" then
        GameTooltip:AddLine(card.text, 1, 1, 1)
    end
    if not (card and card.spell == 1) then
        GameTooltip:AddLine(e.atk .. " / " .. e.hp, 1, 0.82, 0.2)
        local names = BG.PlainKeywords(e.kw or 0)
        if table.getn(names) > 0 then
            GameTooltip:AddLine(table.concat(names, ", "), 1, 0.82, 0.2)
        end
    end
    if e.temp then
        GameTooltip:AddLine("Leaves your hand at the start of your next turn.", 1, 0.82, 0.2)
    end
    if (t.mode == "hand" or t.mode == "board") and not BG.IsSpell(e) then
        GameTooltip:AddLine("Sells for " .. BG.SellValue(e) .. " Gold.", 0.75, 1, 0.6)
    end
    local needLine = BG.NeedLine(needs)
    if needLine then
        GameTooltip:AddLine(needLine, 0.4, 1, 0.5)
    end
    GameTooltip:AddLine(UI.TileHint(t.mode, e), 0.6, 0.6, 0.6)
    GameTooltip:Show()
end

-- Every frame: the large card stays only while its tile is shown, under the cursor and holding the same entry. A tile
-- emptied or hidden under the cursor (a sale, a new state) gets no OnLeave in 1.12; a new entry shows its own card.
function UI.HoverCardTick()
    local t = UI.cardTile
    if not t then
        return
    end
    if not t:IsVisible() or not t.entry or UI.drag or not UI.CursorOnTile(t) then
        UI.OnTileLeave(t)
    elseif t.entry ~= UI.cardEntry then
        UI.OnTileEnter(t)
    end
end

function UI.OnTileLeave(t)
    UI.cardTile, UI.cardEntry = nil, nil
    SetNeedSeats(nil)
    if t then
        t.hovered = nil
        UI.HandUnpop(t)
        UI.TileGlow(t)
    end
    V.HideCard()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

function UI.TileHint(mode, entry)
    if mode == "offer" then
        local cost = (BG.model and BG.model.buyCost) or 3
        return "Click to buy (" .. cost .. " Gold)."
    elseif mode == "hand" then
        if entry and BG.IsSpell(entry) then
            if BG.SellableSpell(entry) then
                return "Click, then a friendly minion (or a board slot) to cast it. Drag onto Bob to throw it away."
            end
            return "Click, then a friendly minion (or a board slot) to cast it. Spells are not sold."
        end
        return "Click, then a board slot to play it, or an ally to send it. Drag onto Bob to sell ("
            .. BG.SellValue(entry) .. " Gold)."
    elseif mode == "board" then
        return "Click, then a slot to move. Drag onto Bob to sell (" .. BG.SellValue(entry) .. " Gold)."
    elseif mode == "option" then
        return entry and BG.IsSpell(entry) and "Click to choose this spell." or "Click to choose it."
    end
    return ""
end

-- ------------------------------------------------------------------ build
local function makeButton(parent, name, w, h, label, onClick)
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(w)
    b:SetHeight(h)
    b:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 } })
    b:SetBackdropColor(0.12, 0.10, 0.07, 0.95)
    b:SetBackdropBorderColor(0.45, 0.38, 0.24, 1)
    b.text = fs(b, "GameFontNormal", "CENTER")
    b.text:SetPoint("CENTER", b, "CENTER", 0, 0)
    b.text:SetWidth(w - 6)
    if label then
        b.text:SetText(label)
    end
    b:SetScript("OnClick", onClick)
    b.hoverGlow = V.MakeGlow(b, "round", w, h, 0)
    V.Hover(b, UI.ButtonEnter, UI.ButtonLeave)
    return b
end

-- A button of the board art: the picture of `pieceName` (a purple shield with its gold coin, or the End Turn stone)
-- at w x h, `.text` kept (the full label, hidden), `.cost` on the shield's coin and `.caption` under it. Without the
-- piece the plain backdrop button of makeButton (full label, no cost / caption).
local function makeArtButton(parent, name, pieceName, w, h, label, onClick, captionBelow)
    if not piece(pieceName) then
        return makeButton(parent, name, captionBelow and 100 or w, captionBelow and 26 or h, label, onClick)
    end
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(w)
    b:SetHeight(h)
    b.tex = b:CreateTexture(nil, "ARTWORK")
    b.tex:SetAllPoints(b)
    b.texHome = true                     -- V.Hover sinks it a pixel while the button is held down
    setPiece(b.tex, pieceName)
    V.AddHighlight(b, b.tex, pieceName, 0.4)
    -- the green glow of a button that can be used now (UI.SetRestGlow), gold on hover
    b.hoverGlow = V.MakeGlow(b, pieceName == "end_turn_button" and "glow_end_turn" or ("glow_" .. pieceName), w, h, 0)
    b.text = fs(b, "GameFontNormal", "CENTER")
    b.text:SetPoint("CENTER", b, "CENTER", 0, 0)
    b.text:SetWidth(w)
    b.text:SetText(label or "")
    b.text:Hide()                        -- the picture carries the label (End Turn) or .cost / .caption do
    if captionBelow then
        b.cost = outline(b, math.floor(h * 0.17), "CENTER")
        b.cost:SetPoint("CENTER", b, "TOP", 0, -h * 0.13)
        b.caption = outline(b, 11, "CENTER")
        b.caption:SetPoint("TOP", b, "BOTTOM", 0, 1)
        b.caption:SetWidth(w + 30)
    end
    b:SetScript("OnClick", onClick)
    V.Hover(b, UI.ButtonEnter, UI.ButtonLeave)
    return b
end

-- The tooltip of a side button (b.tip = { title, line, line2 } set by Refresh).
function UI.ButtonEnter(b)
    local tip = b.tip
    if not tip or not GameTooltip then
        return
    end
    GameTooltip:SetOwner(b, "ANCHOR_BOTTOM")
    GameTooltip:SetText(tip[1])
    if tip[2] then
        GameTooltip:AddLine(tip[2], 1, 1, 1)
    end
    if tip[3] then
        GameTooltip:AddLine(tip[3], 1, 0.82, 0.2)
    end
    GameTooltip:Show()
end

function UI.ButtonLeave(b)
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- Sets what a side button shows: the full label (kept on `.text`), and on a shield the cost and the caption.
function UI.SetButton(b, label, cost, caption)
    b.text:SetText(label)
    if b.cost then
        b.cost:SetText(cost and ("|cffffd100" .. cost .. "|r") or "")
    end
    if b.caption then
        b.caption:SetText(caption and ("|cffffffff" .. caption .. "|r") or "")
    end
end

-- Dims a button (End Turn waiting): the picture's colour, or the plain button's fill.
function UI.DimButton(b, on)
    if b.tex then
        if on then
            b.tex:SetVertexColor(0.5, 0.5, 0.5)
        else
            b.tex:SetVertexColor(1, 1, 1)
        end
    elseif on then
        b:SetBackdropColor(0.08, 0.08, 0.08, 0.9)
    else
        b:SetBackdropColor(0.14, 0.24, 0.12, 0.95)
    end
end

local function makeRow(parent, name, prefix, rect)
    local f = CreateFrame("Frame", name, parent)
    f:SetWidth(rect[3])
    f:SetHeight(rect[4])
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", rect[1], -rect[2])
    f.tiles = {}
    f.prefix = prefix
    f.rx, f.ry = rect[1], rect[2]          -- its place in the window (the fx find the tiles by it)
    return f
end

-- A Hide / Show plate of the discover overlay: the art piece with its word (btn_hide, btn_show) and the pulsing halo, or a
-- plain button when the art is not installed.
UI.PLATE_W, UI.PLATE_H = 130, 76
function UI.MakeDiscoverPlate(name, pieceName, word, hide)
    local D = UI.discover
    local CU = H.CollectionUI
    local piece = CU and CU.Piece and CU.Piece(pieceName)
    local b
    if piece then
        b = CreateFrame("Button", name, D)
        b:SetWidth(UI.PLATE_W)
        b:SetHeight(UI.PLATE_H)
        b:SetNormalTexture(piece.path)
        b:GetNormalTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
        b:SetHighlightTexture(piece.path, "ADD")
        local ht = b:GetHighlightTexture()
        ht:SetTexCoord(0, piece.right, 0, piece.bottom)
        ht:SetAlpha(0.35)
        if b.SetHitRectInsets then
            b:SetHitRectInsets(20, 20, 12, 12)
        end
        if CU.Piece("btn_glow") and CU.ButtonGlow then
            CU.ButtonGlow(b)
        end
    else
        b = CreateFrame("Button", name, D, "UIPanelButtonTemplate")
        b:SetWidth(UI.PLATE_W * 0.7)
        b:SetHeight(24)
        b:SetText(word)
    end
    b:SetFrameLevel(D:GetFrameLevel() + 6)
    b:SetScript("OnClick", function()
        UI.discoverHidden = hide and true or nil
        UI.Refresh()          -- lays the options out again (Show) or takes them away (Hide)
    end)
    b:ClearAllPoints()
    b:SetPoint("BOTTOMLEFT", D, "BOTTOMLEFT", 46, 36)
    b:Hide()
    return b
end

-- Shows the discover overlay as asked: its options, or only the Show plate (the dim goes, the board stays blocked).
function UI.ApplyDiscoverHidden()
    local D = UI.discover
    if not D or not D.hideButton then
        return
    end
    if UI.discoverHidden then
        D:SetBackdropColor(0, 0, 0, 0)
        D.text:Hide()
        if D.banner then
            D.banner:Hide()
        end
        for _, list in ipairs({ D.tiles, D.cards, D.powerCards }) do
            for i = 1, table.getn(list) do
                list[i]:Hide()
            end
        end
        D.hideButton:Hide()
        D.showButton:Show()
    else
        D:SetBackdropColor(0, 0, 0, 0.62)
        D.hideButton:Show()
        D.showButton:Hide()
    end
end

function UI.Create()
    if UI.frame then
        return UI.frame
    end
    local f = CreateFrame("Frame", "HearthstoneBgFrame", UIParent)
    f:SetWidth(UI.W)
    f:SetHeight(UI.H)
    local saved = type(HearthstoneDB) == "table" and HearthstoneDB.bg
    if saved and saved.x and saved.y then
        f:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", saved.x, saved.y)
    else
        f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    end
    f:SetFrameStrata("DIALOG")
    f:SetMovable(true)
    f:SetClampedToScreen(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() this:StartMoving() end)
    f:SetScript("OnDragStop", function()
        this:StopMovingOrSizing()
        -- Remember where the window was left (HearthstoneDB.bg).
        if type(HearthstoneDB) == "table" and this:GetLeft() and this:GetTop() then
            HearthstoneDB.bg = HearthstoneDB.bg or {}
            HearthstoneDB.bg.x = this:GetLeft()
            HearthstoneDB.bg.y = this:GetTop()
        end
    end)
    -- Escape cancels an armed hero power or a picked-up tile. Not EnableKeyboard: in 1.12 a frame with the keyboard
    -- takes every key (no movement, chat or action bars). Instead the window is in UISpecialFrames while something is
    -- armed (UI.SyncEscape); Escape hides it, and OnHide shows it again and cancels.
    f:SetScript("OnHide", function() UI.OnHide() end)
    -- the board is six 512 px tiles (the client draws nothing above 512): 3 columns, 2 rows over the window
    f.boardTiles = {}
    for r = 0, 1 do
        for c = 0, 2 do
            local tile = f:CreateTexture(nil, "BACKGROUND")
            tile:SetWidth(UI.W / 3)
            tile:SetHeight(UI.H / 2)
            tile:SetPoint("TOPLEFT", f, "TOPLEFT", c * UI.W / 3, -r * UI.H / 2)
            if setPiece(tile, "bg_board_c" .. c .. "r" .. r) then
                f.artMode = true
            else
                tile:Hide()
            end
            table.insert(f.boardTiles, tile)
        end
    end
    if not f.artMode then
        f:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 16,
            insets = { left = 4, right = 4, top = 4, bottom = 4 } })
        f:SetBackdropColor(0.05, 0.05, 0.06, 0.96)
        f:SetBackdropBorderColor(0.5, 0.4, 0.2, 1)
    end
    UI.frame = f
    V.Init(f)                            -- the fx layer (floating numbers, flashes) over the window

    -- the dialog border of the other Hearthstone windows, drawn over the board art's edge
    f.frameEdge = CreateFrame("Frame", nil, f)
    f.frameEdge:SetFrameLevel(f:GetFrameLevel() + 60)
    f.frameEdge:SetPoint("TOPLEFT", f, "TOPLEFT", -11, 11)
    f.frameEdge:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", 11, -11)
    f.frameEdge:SetBackdrop({
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32, insets = { left = 0, right = 0, top = 0, bottom = 0 },
    })
    f.frameEdge:EnableMouse(false)

    -- the close button at the top right: surrender (asks first), the game ends for good; once the game is over it
    -- only closes (nothing to surrender)
    f.close = CreateFrame("Button", "HearthstoneBgClose", f, "UIPanelCloseButton")
    f.close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -2, -2)
    f.close:SetFrameLevel(f:GetFrameLevel() + 70)
    f.close:SetScript("OnClick", function() UI.AskSurrender() end)
    f.close:SetScript("OnEnter", function()
        if GameTooltip then
            GameTooltip:SetOwner(this, "ANCHOR_LEFT")
            if BG.Over() then
                GameTooltip:SetText("Close")
                GameTooltip:AddLine("The game is over; closes the window.", 1, 1, 1)
            else
                GameTooltip:SetText("Surrender")
                GameTooltip:AddLine("Leaves the game; it counts as a loss.", 1, 1, 1)
            end
            GameTooltip:Show()
        end
    end)
    f.close:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)

    f.title = fs(f, "GameFontNormalLarge", "CENTER")
    f.title:SetPoint("TOP", f, "TOP", 0, f.artMode and -9 or -12)
    f.title:SetWidth(UI.W - 40)
    f.title:SetText("|cffffd100" .. BP.NAME .. "|r")

    -- lobby panel (BgLobbyUI.lua)
    if HsBgLobbyUI then
        HsBgLobbyUI.Create(f, f.artMode and UI.RECT.lobbyArt or UI.RECT.lobby)
    end

    UI.tavern = makeRow(f, "HearthstoneBgTavern", "HearthstoneBgOffer", UI.RECT.tavern)
    UI.tavern.zone = "offer"
    UI.offers = {}

    UI.boardRow = makeRow(f, "HearthstoneBgBoard", "HearthstoneBgBoardSlot", UI.RECT.board)
    UI.boardRow.zone = "board"
    UI.board = {}

    UI.handRow = makeRow(f, "HearthstoneBgHand", "HearthstoneBgHandTile", UI.RECT.hand)
    UI.handRow.zone = "hand"
    UI.hand = {}
    UI.tavern.centered = f.artMode and true or false
    UI.boardRow.centered = UI.tavern.centered
    UI.handRow.centered = UI.tavern.centered

    -- Bob's arch (top centre): his name, the tavern tier and its pips
    local R = UI.RECT
    f.bob = fs(f, "GameFontNormalSmall", "CENTER")
    f.bob:SetPoint("TOPLEFT", f, "TOPLEFT", R.bob[1], -R.bob[2])
    f.bob:SetWidth(R.bob[3])
    f.bob:SetText("|cffffffffBob's Tavern|r")
    -- Bob himself (tools/art/bg_heroes.py): the arch-shaped portrait fills the arch, the tier sits under it
    local hasBob = false
    local heroes = HS_BgTextures and HS_BgTextures.heroes
    if f.artMode and type(heroes) == "table" then
        for i = 1, table.getn(heroes) do
            if heroes[i] == "bob" then
                hasBob = true
            end
        end
    end
    if hasBob then
        -- Bob in the same golden arch as the heroes (the fight puts the opponent in his place)
        f.bobHero = V.MakeHero(f, "HearthstoneBgBob", UI.HERO_W)
        V.Raise(f.bobHero.frame, 7)
        f.bobHero.frame:SetPoint("TOPLEFT", f, "TOPLEFT", R.bobHero[1], -R.bobHero[2])
        V.SetHero(f.bobHero, "bob", nil, nil)
        f.bobArt = f.bobHero.frame
        V.Hover(f.bobArt, function(b) UI.BobEnter(b) end, function() if GameTooltip then GameTooltip:Hide() end end)
        f.bobArt.hoverGlow = f.bobHero.glow
        f.bob:Hide()
    end
    local tierY = hasBob and (R.bobHero[2] + f.bobHero.h + 2) or (R.bob[2] + 34)
    local pipY = hasBob and (R.bobHero[2] + f.bobHero.h + 3) or (R.bob[2] + 58)
    f.tier = fs(f, "GameFontNormal", "CENTER")
    f.tier:SetPoint("TOPLEFT", f, "TOPLEFT", R.bob[1], -tierY)
    f.tier:SetWidth(R.bob[3])
    if hasBob then
        f.tier:SetFont(UI.NUMBER_FONT, 12, "OUTLINE")
        f.tier:SetTextColor(1, 1, 1)
    end
    if hasBob then
        f.tier:Hide()                        -- Bob's tooltip names the tier; the pips under his arch show it
        f.tierHidden = true
    end
    -- the tavern tier as its shield at the lower left of Bob's arch (the pips stay as the fallback without the art)
    if hasBob and piece("bg_tier_1") then
        f.tierFrame = CreateFrame("Frame", nil, f)
        f.tierFrame:SetFrameLevel(f.bobHero.frame:GetFrameLevel() + 6)      -- over the arch and its gems
        f.tierFrame:SetWidth(UI.TIER_SHIELD_W)
        f.tierFrame:SetHeight(UI.TIER_SHIELD_W)
        f.tierFrame:SetPoint("CENTER", f, "TOPLEFT", R.bobHero[1] + 4, -(R.bobHero[2] + f.bobHero.h - 24))
        f.tierFrame:EnableMouse(true)
        f.tierFrame:SetScript("OnEnter", function()
            if GameTooltip and BG.model then
                GameTooltip:SetOwner(this, "ANCHOR_LEFT")
                GameTooltip:SetText("Tavern Tier " .. (BG.model.tier or 1))
                GameTooltip:AddLine("Bob offers minions of this tier and below.", 1, 1, 1)
                GameTooltip:Show()
            end
        end)
        f.tierFrame:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)
        f.tierShield = f.tierFrame:CreateTexture(nil, "OVERLAY")
        f.tierShield:SetPoint("CENTER", f.tierFrame, "CENTER", 0, 0)
    end
    f.pips = {}
    local pipX = R.bob[1] + (R.bob[3] - (UI.PIPS * 14 - 2)) / 2
    for i = 1, UI.PIPS do
        local pip = f:CreateTexture(nil, "OVERLAY")
        pip:SetTexture(UI.BG_PATH .. "tier_pip")
        pip:SetWidth(12)
        pip:SetHeight(12)
        pip:SetPoint("TOPLEFT", f, "TOPLEFT", pipX + (i - 1) * 14, -pipY)
        f.pips[i] = pip
        if f.tierFrame then
            pip:Hide()                       -- the shield shows the tier
        end
    end

    -- the shields (Upgrade, Refresh, Freeze) beside the arch, End Turn in the stone slot on the right
    local sh = R.shields
    f.upgrade = makeArtButton(f, "HearthstoneBgUpgrade", "bg_upgrade", sh.upgradeW or sh.w, sh.upgradeH or sh.h, "Upgrade", function() UI.SideAction("upgrade") end, true)
    f.refresh = makeArtButton(f, "HearthstoneBgRefresh", "bg_refresh", sh.w, sh.h, "Refresh", function() UI.SideAction("refresh") end, true)
    f.freeze = makeArtButton(f, "HearthstoneBgFreeze", "bg_freeze", sh.w, sh.h, "Freeze", function() UI.SideAction("freeze") end, true)
    f.endTurn = makeArtButton(f, "HearthstoneBgEndTurn", "end_turn_button", R.endTurn[3], R.endTurn[4], "End Turn", function() UI.SideAction("end") end)
    if f.upgrade.tex then
        f.upgrade:SetPoint("TOPLEFT", f, "TOPLEFT", sh.upgrade, -(sh.upgradeY or sh.y))
        f.refresh:SetPoint("TOPLEFT", f, "TOPLEFT", sh.refresh, -sh.y)
        f.freeze:SetPoint("TOPLEFT", f, "TOPLEFT", sh.freeze, -sh.y)
    else
        local x = 1060
        f.upgrade:SetPoint("TOPLEFT", f, "TOPLEFT", x, -130)
        f.refresh:SetPoint("TOPLEFT", f, "TOPLEFT", x, -160)
        f.freeze:SetPoint("TOPLEFT", f, "TOPLEFT", x, -190)
    end
    if f.endTurn.tex then
        f.endTurn:SetPoint("TOPLEFT", f, "TOPLEFT", R.endTurn[1], -R.endTurn[2])
    else
        f.endTurn:SetWidth(100)
        f.endTurn:SetHeight(34)
        f.endTurn:SetPoint("TOPLEFT", f, "TOPLEFT", 1060, -226)
    end

    -- Gold: "<gold>/<this turn's gold>" on the board's gold pill at the bottom edge and a row of coins right of it
    -- (spent coins dark), like the game's gold bar
    local GP = R.goldPill
    f.gold = outline(f, 14, "CENTER")
    f.gold:SetWidth(60)
    if f.artMode then
        f.gold:SetPoint("CENTER", f, "TOPLEFT", GP[1], -GP[2])
    else
        f.gold:SetPoint("CENTER", f, "TOPLEFT", 1090, -62)
    end
    f.goldHit = CreateFrame("Frame", nil, f)            -- the tooltip of the gold bar
    f.goldHit:SetPoint("TOPLEFT", f, "TOPLEFT", GP[1] - 34, -(GP[2] - 13))
    f.goldHit:SetWidth(68 + UI.MAX_COINS * UI.COIN_STEP + 8)
    f.goldHit:SetHeight(26)
    f.goldHit:EnableMouse(true)
    f.goldHit:SetScript("OnEnter", function()
        if GameTooltip and BG.model then
            GameTooltip:SetOwner(this, "ANCHOR_TOP")
            GameTooltip:SetText("Gold: " .. (BG.model.gold or 0))
            GameTooltip:AddLine("A minion costs 3, a refresh 1. Selling gives 1.", 1, 1, 1)
            GameTooltip:Show()
        end
    end)
    f.goldHit:SetScript("OnLeave", function() if GameTooltip then GameTooltip:Hide() end end)
    f.coins = {}
    local coinPiece = piece("bg_coin") ~= nil
    for i = 1, UI.MAX_COINS do
        local c = f:CreateTexture(nil, "OVERLAY")
        if coinPiece then
            setPiece(c, "bg_coin")
        else
            c:SetTexture(UI.BG_PATH .. "coin")
        end
        c:SetWidth(UI.COIN)
        c:SetHeight(UI.COIN)
        if f.artMode then
            c:SetPoint("TOPLEFT", f, "TOPLEFT", R.coins[1] + (i - 1) * UI.COIN_STEP, -R.coins[2])
        else
            c:SetPoint("TOPLEFT", f, "TOPLEFT", 1060 + (i - 1) * 12, -80)
        end
        c:Hide()
        f.coins[i] = c
    end
    f.coin = f.coins[1]

    -- My hero in its golden arch over the board's stone arch, with the health gem and the armor shield (BgVisuals)
    f.hero = V.MakeHero(f, "HearthstoneBgMyHero", UI.HERO_W)
    V.Raise(f.hero.frame, 7)                 -- above the rows (with its gems: 1.12 does not raise the children)
    f.heroActor = { frame = f.hero.frame, x = R.myHero[1], y = R.myHero[2], w = f.hero.w, h = f.hero.h }
    f.hero.frame:SetPoint("TOPLEFT", f, "TOPLEFT", R.myHero[1], -R.myHero[2])
    f.hero.frame:Hide()
    V.Hover(f.hero.frame, function(b) UI.HeroEnter(b) end, function() if GameTooltip then GameTooltip:Hide() end end)
    f.hero.frame.hoverGlow = f.hero.glow
    f.hero.frame.hoverColor = "gold"
    f.power = CreateFrame("Button", "HearthstoneBgPower", f)
    f.power:SetWidth(R.myPower[3])
    f.power:SetHeight(R.myPower[4])
    f.power:SetPoint("TOPLEFT", f, "TOPLEFT", R.myPower[1], -R.myPower[2])
    f.power:SetFrameLevel(f:GetFrameLevel() + 8)
    f.power:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 } })
    f.power:SetBackdropColor(0.10, 0.09, 0.07, 0.95)
    f.power:SetBackdropBorderColor(0.45, 0.38, 0.24, 1)
    -- the round ring of the board art (hero_power_ring) over the round portrait; without it the plain backdrop
    f.power.ring = f.power:CreateTexture(nil, "OVERLAY")
    f.power.ring:SetPoint("TOPLEFT", f.power, "TOPLEFT", -UI.POWER_RING_PAD, UI.POWER_RING_PAD)
    f.power.ring:SetPoint("BOTTOMRIGHT", f.power, "BOTTOMRIGHT", UI.POWER_RING_PAD, -UI.POWER_RING_PAD)
    f.power.hasRing = setPiece(f.power.ring, "hero_power_ring")
    f.power.ring:Hide()
    f.power.art = f.power:CreateTexture(nil, "ARTWORK")
    local inset = f.power.hasRing and UI.POWER_ART_INSET or 0
    f.power.art:SetPoint("TOPLEFT", f.power, "TOPLEFT", inset, -inset)
    f.power.art:SetPoint("BOTTOMRIGHT", f.power, "BOTTOMRIGHT", -inset, inset)
    f.power.art:Hide()
    -- the light of the hovered power over its portrait
    f.power.hl = f.power:CreateTexture(nil, "OVERLAY")
    f.power.hl:SetAllPoints(f.power.art)
    f.power.hl:SetTexture(UI.WHITE)
    f.power.hl:SetBlendMode("ADD")
    f.power.hl:SetVertexColor(1, 0.9, 0.6)
    f.power.hl:SetAlpha(0.18)
    f.power.hl:Hide()
    f.power.text = fs(f.power, "GameFontNormalSmall", "CENTER")
    f.power.text:SetPoint("CENTER", f.power, "CENTER", 0, 2)
    f.power.text:SetWidth(R.myPower[3] - 4)
    f.power.text:SetHeight(R.myPower[4] - 6)
    -- the green glow of a usable power (gold while armed or hovered)
    f.power.hoverGlow = V.MakeGlow(f.power, "glow_power_ring", R.myPower[3] + 2 * UI.POWER_RING_PAD,
        R.myPower[4] + 2 * UI.POWER_RING_PAD, 0, -UI.POWER_RING_PAD, -UI.POWER_RING_PAD)
    -- the cost on the board art's gold coin, sitting on the top edge of the ring (above the glow)
    f.power.top = CreateFrame("Frame", nil, f.power)
    f.power.top:SetAllPoints(f.power)
    f.power.top:SetFrameLevel(f.power:GetFrameLevel() + 3)
    f.power.coin = f.power.top:CreateTexture(nil, "OVERLAY")
    f.power.coin:SetWidth(24)
    f.power.coin:SetHeight(24)
    f.power.coin:SetPoint("CENTER", f.power, "TOP", 0, 2)
    f.power.coinOk = setPiece(f.power.coin, "bg_coin")
    f.power.coin:Hide()
    f.power.cost = outline(f.power.top, 12, "CENTER")
    f.power.cost:SetPoint("CENTER", f.power.coin, "CENTER", 0, 0)
    -- the small counter some powers carry (Captain Eudora's digs left)
    f.power.count = outline(f.power.top, 10, "CENTER")
    f.power.count:SetPoint("BOTTOMRIGHT", f.power, "BOTTOMRIGHT", 3, -2)
    f.power.count:Hide()
    f.power:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    f.power:SetScript("OnClick", function()
        if arg1 == "RightButton" then
            UI.PowerCancel()
        else
            UI.PowerClick()
        end
    end)
    V.Hover(f.power, function(b) UI.PowerEnter(b) end, function() UI.PowerLeave() end)
    f.power:Hide()

    -- My quest icon (with its progress) and my reward icon, round portraits in the bronze ring right of the power
    local function roundIcon(name, rect)
        local b = CreateFrame("Button", name, f)
        b:SetWidth(rect[3])
        b:SetHeight(rect[3])
        b:SetPoint("TOPLEFT", f, "TOPLEFT", rect[1], -rect[2])
        b:SetFrameLevel(f:GetFrameLevel() + 8)
        b.art = b:CreateTexture(nil, "ARTWORK")
        b.art:SetPoint("TOPLEFT", b, "TOPLEFT", 3, -3)
        b.art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -3, 3)
        b.art:Hide()
        b.ring = b:CreateTexture(nil, "OVERLAY")
        b.ring:SetPoint("TOPLEFT", b, "TOPLEFT", -3, 3)
        b.ring:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", 3, -3)
        if not setPiece(b.ring, "hero_power_ring") then
            b.ring:Hide()
            b:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 8,
                insets = { left = 2, right = 2, top = 2, bottom = 2 } })
            b:SetBackdropColor(0.10, 0.09, 0.07, 0.95)
            b:SetBackdropBorderColor(0.45, 0.38, 0.24, 1)
        end
        b.hoverGlow = V.MakeGlow(b, "glow_power_ring", rect[3] + 6, rect[3] + 6, 0, -3, -3)
        b.name = fs(b, "GameFontNormalSmall", "CENTER")
        b.name:SetPoint("CENTER", b, "CENTER", 0, 0)
        b.name:SetWidth(rect[3] - 4)
        b.name:SetHeight(rect[3] - 8)
        b.name:SetJustifyV("MIDDLE")
        b.top = CreateFrame("Frame", nil, b)
        b.top:SetAllPoints(b)
        b.top:SetFrameLevel(b:GetFrameLevel() + 3)
        b:Hide()
        return b
    end
    f.myQuest = roundIcon("HearthstoneBgQuest", R.myQuest)
    f.myQuest.progress = outline(f.myQuest.top, 12, "CENTER")
    f.myQuest.progress:SetPoint("CENTER", f.myQuest, "BOTTOM", 0, 1)
    V.Hover(f.myQuest, function(b) UI.QuestEnter(b) end, function() if GameTooltip then GameTooltip:Hide() end end)
    f.myReward = roundIcon("HearthstoneBgReward", R.myReward)
    V.Hover(f.myReward, function(b) UI.RewardEnter(b) end, function() if GameTooltip then GameTooltip:Hide() end end)
    -- My Secrets, small icons along the bottom of my hero's portrait (hover: the card render)
    f.secrets = {}
    for i = 1, 5 do
        local b = CreateFrame("Button", "HearthstoneBgSecret" .. i, f.hero.frame)
        b:SetWidth(18)
        b:SetHeight(18)
        b:SetPoint("BOTTOMLEFT", f.hero.frame, "BOTTOMLEFT", 6 + (i - 1) * 19, 4)
        b:SetFrameLevel(f.hero.frame:GetFrameLevel() + 3)
        b.art = b:CreateTexture(nil, "ARTWORK")
        b.art:SetAllPoints(b)
        b.art:SetTexture(UI.SECRET_ICON)
        b.art:Hide()
        b.ring = b:CreateTexture(nil, "OVERLAY")
        b.ring:SetPoint("TOPLEFT", b, "TOPLEFT", -2, 2)
        b.ring:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", 2, -2)
        if not setPiece(b.ring, "hero_power_ring") then
            b.ring:Hide()
            b:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 6,
                insets = { left = 1, right = 1, top = 1, bottom = 1 } })
            b:SetBackdropColor(0.10, 0.09, 0.07, 0.95)
            b:SetBackdropBorderColor(0.45, 0.38, 0.24, 1)
        end
        b:Hide()
        V.Hover(b, function(btn) UI.SecretEnter(btn) end, function()
            V.HideCard()
            if GameTooltip then
                GameTooltip:Hide()
            end
        end)
        f.secrets[i] = b
    end

    -- the notices in the middle of the board: a refusal or a lost answer fades after UI.TOAST_TIME, a hint (a picked-up
    -- card, an armed power) stays while it is true. f.message holds the text.
    f.toast = CreateFrame("Frame", "HearthstoneBgToast", f)
    f.toast:SetFrameLevel(f:GetFrameLevel() + 40)
    f.toast:SetWidth(R.toast[3])
    f.toast:SetHeight(30)
    f.toast:SetPoint("CENTER", f, "TOPLEFT", R.toast[1], -R.toast[2])
    f.toast:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f.toast:SetBackdropColor(0, 0, 0, 0.78)
    f.toast:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    f.toast:EnableMouse(false)
    f.message = fs(f.toast, "GameFontHighlight", "CENTER")
    f.message:SetPoint("CENTER", f.toast, "CENTER", 0, 0)
    f.message:SetWidth(R.toast[3] - 20)
    f.message:SetText("")
    f.toast:Hide()

    -- discover overlay: the whole board dims and the options stand in the middle as large cards (the tiles without the
    -- card art)
    UI.discover = CreateFrame("Frame", "HearthstoneBgDiscover", f)
    UI.discover:SetPoint("TOPLEFT", f, "TOPLEFT", 0, 0)
    UI.discover:SetWidth(UI.W)
    UI.discover:SetHeight(UI.H)
    UI.discover:SetFrameLevel(f:GetFrameLevel() + UI.OVERLAY_BASE + 20)
    UI.discover:EnableMouse(true)
    UI.discover:SetBackdrop({ bgFile = UI.WHITE })
    UI.discover:SetBackdropColor(0, 0, 0, 0.62)
    UI.discover.text = fs(UI.discover, "GameFontNormalLarge", "CENTER")
    UI.discover.text:SetPoint("TOP", UI.discover, "TOP", 0, -150)
    UI.discover.text:SetWidth(760)
    UI.discover.text:SetText("|cffffd100Choose a minion|r")
    -- the hero power choice wears the Choose One scroll of the match chooser (MatchUI makeChooser; its words are part of
    -- the art, 28% of the window wide, the art filling about a sixth of its height)
    local chooseBanner = H.CollectionUI and H.CollectionUI.Piece and H.CollectionUI.Piece("choose_banner")
    if chooseBanner then
        UI.discover.banner = UI.discover:CreateTexture(nil, "ARTWORK")
        UI.discover.banner:SetTexture(chooseBanner.path)
        UI.discover.banner:SetTexCoord(0, chooseBanner.right, 0, chooseBanner.bottom)
        UI.discover.banner:SetWidth(UI.W * 0.28)
        UI.discover.banner:SetHeight(UI.W * 0.28 / 2)
        UI.discover.banner:SetPoint("TOP", UI.discover, "TOP", 0, -UI.H * 0.09)
        UI.discover.banner:Hide()
    end
    UI.discover.tiles = {}
    UI.discover.cards = {}
    UI.discover.powerCards = {}
    -- Hide takes the options away and keeps the board blocked (the frame still takes the clicks); Show brings them back,
    -- like the Choose One chooser of the match (MatchUI makeChooser): the same icy plates in the same corner
    UI.discover.hideButton = UI.MakeDiscoverPlate("HearthstoneBgDiscoverHide", "btn_hide", "Hide", true)
    UI.discover.showButton = UI.MakeDiscoverPlate("HearthstoneBgDiscoverShow", "btn_show", "Show", nil)
    UI.discover:Hide()

    -- hero pick overlay and quest pick overlay
    if HsBgHeroPickUI then
        HsBgHeroPickUI.Create(f)
    end
    if HsBgQuestPickUI then
        HsBgQuestPickUI.Create(f)
    end

    -- combat + end overlays (their modules)
    if HsBgCombatUI then
        HsBgCombatUI.Create(f)
    end
    if HsBgEndUI then
        HsBgEndUI.Create(f)
    end

    UI.hiding = true
    f:Hide()
    UI.hiding = false
    return f
end

-- ------------------------------------------------------------------ interactions
function UI.SideAction(kind)
    if kind == "end" then
        if (UI.endGuard or 0) > 0 then
            return                       -- the click that closed the fight result (Continue sits in this slot)
        end
        BG.EndTurn()
    elseif kind == "refresh" then
        BG.Refresh()
    elseif kind == "upgrade" then
        BG.Upgrade()
    elseif kind == "freeze" then
        BG.Freeze(not (BG.model and BG.model.frozen))
    end
end

-- Escape: cancel an armed hero power, then a picked-up tile, then close an ally view.
function UI.Escape()
    if UI.powerArm then
        UI.PowerCancel()
    elseif UI.sel then
        UI.sel = nil
        UI.Refresh()
    elseif BG.peekSeat then
        BG.ClosePeek()
        UI.Refresh()
    end
end

-- The window is listed in UISpecialFrames only while Escape has something to cancel, so otherwise Escape opens the
-- game menu as usual.
function UI.SyncEscape()
    if not UISpecialFrames then
        return
    end
    local want = UI.frame ~= nil and UI.frame:IsShown() and (UI.powerArm ~= nil or UI.sel ~= nil or BG.peekSeat ~= nil)
    local at = nil
    for i = 1, table.getn(UISpecialFrames) do
        if UISpecialFrames[i] == "HearthstoneBgFrame" then
            at = i
            break
        end
    end
    if want and not at then
        table.insert(UISpecialFrames, "HearthstoneBgFrame")
    elseif at and not want then
        table.remove(UISpecialFrames, at)
    end
end

-- Surrender asks first; accepting sends the server's leave command (.hs bg leave), which drops the game. A game that
-- is over is left at once: the result is final and the payout already paid, so there is nothing to confirm.
function UI.AskSurrender()
    if BG.state == "none" or BG.state == "loading" then
        return
    end
    if BG.Over() then
        UI.Surrender()
        return
    end
    if StaticPopupDialogs and not StaticPopupDialogs["HEARTHSTONE_BG_SURRENDER"] then
        StaticPopupDialogs["HEARTHSTONE_BG_SURRENDER"] = {
            text = "Surrender and leave the " .. BP.NAME .. "?\n\nThe game ends now and cannot be resumed.",
            button1 = "Surrender",
            button2 = CANCEL or "Cancel",
            OnAccept = function() UI.Surrender() end,
            showAlert = 1,
            timeout = 0,
            whileDead = 1,
            hideOnEscape = 1,
        }
    end
    if StaticPopup_Show then
        StaticPopup_Show("HEARTHSTONE_BG_SURRENDER")
    end
end

function UI.Surrender()
    if SendChatMessage then
        SendChatMessage(".hs bg leave", "SAY")
    end
end

-- The window was hidden. By this file (UI.hiding, or no game open): nothing more. By Escape (UISpecialFrames): an
-- open game keeps its window, so it comes back and Escape only cancels.
function UI.OnHide()
    if UI.hiding or UI.reshowing then
        return
    end
    if BG.state == "none" or BG.state == "loading" then
        UI.SyncEscape()
        return
    end
    UI.reshowing = true
    UI.frame:Show()
    UI.reshowing = false
    UI.Escape()
    UI.SyncEscape()
end

-- True when Textures\BgHeroes\Powers\<id>.blp is installed (HS_BgTextures.powers).
function UI.PowerArtInstalled(id)
    if not id or id == "" then
        return false
    end
    local list = HS_BgTextures and HS_BgTextures.powers
    if type(list) ~= "table" then
        return false
    end
    for i = 1, table.getn(list) do
        if list[i] == id then
            return true
        end
    end
    return false
end

-- True when `id` is one of the installed keys in `list` (HS_BgTextures.quests / .rewards).
function UI.Listed(list, id)
    if not id or id == "" or type(list) ~= "table" then
        return false
    end
    for i = 1, table.getn(list) do
        if list[i] == id then
            return true
        end
    end
    return false
end

-- The quest and reward round portraits (Textures\BgQuests).
function UI.QuestArtInstalled(id)
    return UI.Listed(HS_BgTextures and HS_BgTextures.quests, id)
end

function UI.RewardArtInstalled(id)
    return UI.Listed(HS_BgTextures and HS_BgTextures.rewards, id)
end

-- The hero power button. A target power arms a click on the board / a tavern offer.
function UI.PowerClick()
    local m = BG.model
    if not m or m.power == "" or m.power == nil then
        return
    end
    if not m.powerUsable then
        -- pwu=0 is either a used power or too little gold
        BG.message = (m.gold or 0) < (m.powerCost or 0) and BG.WHY_TEXT.NotEnoughGold or BG.WHY_TEXT.PowerUsed
        UI.Refresh()
        return
    end
    local target = BG.PowerTarget()
    if target == "n" then
        BG.UsePower(0, nil)
    else
        UI.powerArm = target
        UI.powerArmRound = m.rd
        UI.powerFirst = nil
        BG.message = nil
        UI.Refresh()
    end
end

function UI.PowerCancel()
    if UI.powerArm then
        UI.powerArm = nil
        UI.powerFirst = nil
        BG.message = "Hero power cancelled."
        UI.Refresh()
    end
end

-- The hero power under the cursor: its card beside the ring (like the match's hero power), else its tooltip.
function UI.PowerEnter(b)
    local m = BG.model or {}
    local pw = BG.HeroPower(m.power)
    local list = HS_BgTextures and HS_BgTextures.powerCards
    if m.power and UI.Listed(list, m.power) then
        local line = nil
        if not m.powerUsable then
            line = "|cffff6650Not usable now|r"
        elseif BG.PowerTarget() == "f" then
            line = "Click, then a friendly minion"
        elseif BG.PowerTarget() == "o" then
            line = "Click, then a tavern minion"
        elseif BG.PowerTarget() == "s" then
            line = "Click, then a player in the lobby"
        elseif BG.PowerTarget() == "m" then
            line = "Click, then two minions"
        end
        -- The held offer / Tier minions show beside the power card too.
        local held, heldTurns = BG.HeldOffer()
        local extra = ""
        if held ~= "" then
            extra = "Held: " .. BG.CardName(held) .. " (" .. heldTurns .. " turns)"
        end
        for _, e in ipairs(BG.Expedition()) do
            extra = extra .. (extra ~= "" and "  " or "") .. "T" .. e.tier .. ": " .. BG.CardName(e.id)
        end
        if extra ~= "" then
            line = line and (line .. "  " .. extra) or extra
        end
        -- the real number a growing power gives now ("Now: +4/+2" for For the Horde!)
        local now = BG.PowerNowLine(m.power, m.rd)
        if now then
            line = (line and (line .. "\n") or "") .. "|cff80ff80" .. now .. "|r"
        end
        if V.ShowTexCard(UI.HERO_PATH .. "PowerCards\\" .. m.power, b, UI.frame, line) then
            return
        end
    end
    if not GameTooltip then
        return
    end
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    GameTooltip:SetText(pw and pw.name or "Hero power")
    if pw and pw.text and pw.text ~= "" then
        GameTooltip:AddLine(pw.text, 1, 1, 1)
    end
    local now = BG.PowerNowLine(m.power, m.rd)
    if now then
        GameTooltip:AddLine(now, 0.5, 1, 0.5)
    end
    -- The skipped turns, Maiev's held offer and Faelin's Tier minions.
    if BG.SkipTurn() then
        GameTooltip:AddLine("This turn is skipped.", 0.8, 0.8, 0.8)
    end
    local held, heldTurns = BG.HeldOffer()
    if held ~= "" then
        GameTooltip:AddLine("Held: " .. BG.CardName(held) .. " (" .. heldTurns .. " turns)", 0.6, 0.9, 1)
    end
    for _, e in ipairs(BG.Expedition()) do
        GameTooltip:AddLine("At tier " .. e.tier .. ": " .. BG.CardName(e.id), 0.6, 0.9, 1)
    end
    GameTooltip:AddLine((m.powerCost or 0) .. " Gold", 1, 0.82, 0.2)
    if not m.powerUsable then
        GameTooltip:AddLine("Not usable now.", 1, 0.4, 0.3)
    elseif BG.PowerTarget() == "f" then
        GameTooltip:AddLine("Click, then a friendly minion.", 0.8, 0.8, 0.8)
    elseif BG.PowerTarget() == "o" then
        GameTooltip:AddLine("Click, then a tavern minion.", 0.8, 0.8, 0.8)
    elseif BG.PowerTarget() == "s" then
        GameTooltip:AddLine("Click, then a player in the lobby.", 0.8, 0.8, 0.8)
    elseif BG.PowerTarget() == "m" then
        GameTooltip:AddLine("Click, then two minions (board or tavern).", 0.8, 0.8, 0.8)
    else
        GameTooltip:AddLine("Click to use.", 0.8, 0.8, 0.8)
    end
    GameTooltip:Show()
end

function UI.PowerLeave()
    V.HideCard()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- My quest icon's tooltip: the quest's text and its progress, and that the reward is active once it is done.
function UI.QuestEnter(b)
    if not GameTooltip then
        return
    end
    local m = BG.model or {}
    local q = m.quest or ""
    local q2 = m.quest2 or ""
    if q == "" and q2 == "" then
        return
    end
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    if q ~= "" then
        GameTooltip:SetText(BG.QuestName(q))
        local questText = BG.MyQuestText()
        if questText ~= "" then
            GameTooltip:AddLine(questText, 1, 1, 1)
        end
        GameTooltip:AddLine("Progress " .. BG.QuestProgress() .. "/" .. BG.QuestRequired(), 1, 0.82, 0.2)
        if BG.QuestDone(m) then
            GameTooltip:AddLine("Complete: your reward is active.", 0.5, 1, 0.5)
        end
    else
        GameTooltip:SetText("Second quest")
    end
    -- Sire Denathrius's second quest.
    if q2 ~= "" then
        GameTooltip:AddLine("Second quest: " .. BG.QuestName(q2), 1, 0.82, 0.2)
        local text2 = BG.MyQuestText2()
        if text2 ~= "" then
            GameTooltip:AddLine(text2, 1, 1, 1)
        end
        GameTooltip:AddLine("Progress " .. BG.QuestProgress2() .. "/" .. BG.QuestRequired2(), 1, 0.82, 0.2)
        if BG.QuestDone2(m) then
            GameTooltip:AddLine("Complete: your second reward is active.", 0.5, 1, 0.5)
        end
    end
    GameTooltip:Show()
end

-- My reward icon's tooltip: the reward's name and text.
function UI.RewardEnter(b)
    if not GameTooltip then
        return
    end
    local m = BG.model or {}
    if (not m.reward or m.reward == "") and (not m.reward2 or m.reward2 == "") then
        return
    end
    local rw = (m.reward ~= nil and m.reward ~= "") and m.reward or m.reward2
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    GameTooltip:SetText(BG.RewardName(rw))
    local rewardText = rw == m.reward and BG.MyRewardText() or BG.MyRewardText2()
    if rewardText ~= "" then
        GameTooltip:AddLine(rewardText, 1, 1, 1)
    end
    -- Sire Denathrius's second reward.
    if m.reward2 and m.reward2 ~= "" and rw ~= m.reward2 then
        GameTooltip:AddLine("Second reward: " .. BG.RewardName(m.reward2), 1, 0.82, 0.2)
        local text2 = BG.MyRewardText2()
        if text2 ~= "" then
            GameTooltip:AddLine(text2, 1, 1, 1)
        end
    end
    GameTooltip:Show()
end

-- Vol'jin: the first click marks a minion, the second (another one) uses the power; the same one twice unmarks it.
function UI.PowerPickMinion(uid)
    if not uid then
        BG.message = BG.WHY_TEXT.BadTarget
    elseif UI.powerFirst == nil then
        UI.powerFirst = uid
        BG.message = nil
    elseif UI.powerFirst == uid then
        UI.powerFirst = nil
    else
        BG.UsePower(UI.powerFirst, nil, uid)
        UI.powerArm = nil
        UI.powerFirst = nil
    end
    UI.Refresh()
end

function UI.OfferClick(i)
    if UI.powerArm == "m" then
        local o = BG.model and BG.model.offers[i + 1]
        UI.PowerPickMinion(o and o.uid)
        return
    end
    if UI.powerArm == "o" then
        BG.UsePower(0, i)
        UI.powerArm = nil
        UI.Refresh()
        return
    end
    BG.Buy(i)
end

function UI.HandClick(i)
    if UI.sel and UI.sel.zone == "hand" and UI.sel.index == i then
        UI.sel = nil
    else
        UI.sel = { zone = "hand", index = i }
    end
    UI.Refresh()
end

-- The cast of a spell on the board tile `index` (nil: no target): a magic flash on the target, a spell sound.
function UI.CastFx(index)
    local t = index and UI.board[index + 1]
    if t and t.wx and UI.frame and UI.frame.artMode then
        V.Pulse(t.wx, t.wy, t.tw, t.th, 0.6, 0.8, 1, 0.5, "glow_oval", 0.5)
    end
    V.Sound("igAbilityIconDrop")
end

-- Casts the hand spell `handIndex` on the board minion `over` (0-based; nil: no target). While the last action is
-- still out (BG.Act would refuse it as busy) the cast waits in UI.castQueue by the card's uid instead of being lost,
-- so a played spell never stays in the hand; UI.CastQueueTick sends it once the answer is in.
function UI.CastSpell(handIndex, over)
    local m = BG.model
    local spell = m and m.hand[handIndex + 1]
    if not spell then
        return
    end
    local t = over and m.board[over + 1]
    local target = t and t.uid or 0
    if BG.IsBusy() and spell.uid and spell.uid ~= 0 then
        UI.castQueue = UI.castQueue or {}
        for i = 1, table.getn(UI.castQueue) do
            if UI.castQueue[i].uid == spell.uid then
                return                                  -- already waiting
            end
        end
        table.insert(UI.castQueue, { uid = spell.uid, target = target })
        return
    end
    if BG.Cast(handIndex, target) then
        UI.CastFx(target ~= 0 and over or nil)
    end
end

-- Every frame: the first waiting cast goes out once no action is out and the board is current. Its card and target
-- are found again by uid (an earlier cast moves the hand); a card or target that is gone drops the cast.
function UI.CastQueueTick()
    local q = UI.castQueue
    if not q or table.getn(q) == 0 then
        return
    end
    local m = BG.model
    if not (m and m.ph == "s" and BG.state == "ready") then
        UI.castQueue = nil                              -- the shop turn is over: nothing waits into the next one
        return
    end
    if BG.IsBusy() or BG.getIn ~= nil or BG.getReqId ~= nil then
        return
    end
    local c = table.remove(q, 1)
    for i = 1, table.getn(m.hand) do
        if m.hand[i].uid == c.uid then
            local over = nil
            if c.target ~= 0 then
                for j = 1, table.getn(m.board) do
                    if m.board[j].uid == c.target then
                        over = j - 1
                    end
                end
                if not over then
                    return
                end
            end
            UI.CastSpell(i - 1, over)
            return
        end
    end
end

-- A click on a board slot (index = the slot; slot == board size means "at the end").
function UI.BoardClick(index)
    local bd = BG.model and BG.model.board or {}
    if UI.powerArm == "m" then
        UI.PowerPickMinion(bd[index + 1] and bd[index + 1].uid)
        return
    end
    if UI.powerArm == "f" then
        local t = bd[index + 1]
        if t then
            BG.UsePower(t.uid, nil)
            UI.powerArm = nil
        else
            BG.message = BG.WHY_TEXT.BadTarget
        end
        UI.Refresh()
        return
    end
    if UI.sel and UI.sel.zone == "hand" then
        local target = bd[index + 1] and bd[index + 1].uid or 0
        local entry = BG.model and BG.model.hand[UI.sel.index + 1]
        if entry and BG.IsSpell(entry) then
            if BG.SpellNeedsTarget(entry) and not BG.ValidSpellTarget(entry, bd[index + 1]) then
                BG.message = BG.WHY_TEXT.BadTarget          -- the spell stays picked up: choose a target
                UI.Refresh()
                return
            end
            UI.CastSpell(UI.sel.index, target ~= 0 and index or nil)
            UI.sel = nil
        elseif BG.BattlecryRule(entry) then
            -- a targeted Battlecry: a click on a fitting minion plays the card there aimed at it; elsewhere the card
            -- waits for its target (the board's fitting minions glow) when there is one to choose
            local over = bd[index + 1]
            if UI.sel.choosing then
                if over and BG.ValidBattlecryTarget(entry, over) then
                    BG.Play(UI.sel.index, UI.sel.slot, over.uid)
                    UI.sel = nil
                else
                    BG.message = BG.WHY_TEXT.BadTarget          -- the card stays picked up: choose a target
                end
            elseif over and BG.ValidBattlecryTarget(entry, over) then
                BG.Play(UI.sel.index, index, over.uid)
                UI.sel = nil
            elseif BG.HasBattlecryTarget(entry, bd) then
                UI.sel = { zone = "hand", index = UI.sel.index, choosing = true, slot = index }
            else
                BG.Play(UI.sel.index, index, 0)
                UI.sel = nil
            end
        else
            if BG.IsMagnetic(entry) then
                target = BG.MagnetUid(entry, bd[index + 1])
            end
            BG.Play(UI.sel.index, index, target)
            UI.sel = nil
        end
    elseif UI.sel and UI.sel.zone == "board" then
        -- a move takes the final index: the empty end slot means the last minion's place
        local last = table.getn(bd) - 1
        if index > last then
            index = last
        end
        BG.Move(UI.sel.index, index)
        UI.sel = nil
    elseif index <= table.getn(bd) then
        UI.sel = { zone = "board", index = index }
    end
    UI.Refresh()
end

function UI.OptionClick(i)
    BG.Discover(i)
end

function UI.TileClick(kind, index)
    if kind == "offer" then
        UI.OfferClick(index)
    elseif kind == "hand" then
        UI.HandClick(index)
    elseif kind == "board" then
        UI.BoardClick(index)
    elseif kind == "option" then
        UI.OptionClick(index)
    end
end

-- Right click cancels an armed hero power, else a picked-up card (selling is a drag onto Bob, UI.DropAt).
function UI.TileRightClick(kind, index)
    if UI.powerArm then
        UI.PowerCancel()
        return
    end
    if UI.sel then
        UI.sel = nil
        UI.Refresh()
    end
end

-- Bob's portrait (the plain "Bob's Tavern" label without the art): the drop target that sells.
function UI.BobFrame()
    local f = UI.frame
    return f and (f.bobArt or f.bob)
end

-- A board minion or a hand minion can be sold; a spell is cast, not sold (a Wild Banana is thrown away for 0 Gold).
local function sellable(d)
    return d and (d.kind == "board" or d.kind == "hand") and (not BG.IsSpell(d.entry) or BG.SellableSpell(d.entry))
end

-- While a sellable card is carried, Bob glows under the cursor and the carried card shows what it pays.
function UI.BobDragGlow()
    if UI.dragGhost and UI.dragGhost.float then
        UI.dragGhost.float:Hide()
    end
    local bob = UI.frame and UI.frame.bobArt
    if not bob then
        return
    end
    local lit = sellable(UI.drag) and UI.CursorIn(bob)
    if lit and not UI.bobLit then
        V.HoverOn(bob)
    elseif UI.bobLit and not lit then
        V.HoverOff(bob)
    end
    UI.bobLit = lit or nil
    if lit and UI.dragGhost and UI.dragGhost.float then
        local v = BG.SellValue(UI.drag.entry)
        UI.dragGhost.float:SetFont(V.NUMBER_FONT, 16, "OUTLINE")
        UI.dragGhost.float:SetText("|cffffd100" .. (v > 0 and ("Sell " .. v .. " Gold") or "Throw away") .. "|r")
        UI.dragGhost.float:Show()
    end
end

-- ------------------------------------------------------------------ drag
-- The cursor in UIParent coordinates, or nil. Shared with the lobby's drop test (BgLobbyUI L.CellAt).
function UI.CursorPos()
    if not GetCursorPosition then
        return nil
    end
    local scale = UIParent and UIParent:GetEffectiveScale() or 1
    local x, y = GetCursorPosition()
    return x / scale, y / scale
end
local cursorPos = UI.CursorPos

-- True when the cursor is over `frame`.
function UI.CursorIn(frame)
    local x, y = cursorPos()
    if not x or not frame or not frame.GetLeft then
        return false
    end
    local l, r, b, t = frame:GetLeft(), frame:GetRight(), frame:GetBottom(), frame:GetTop()
    if not l or not r or not b or not t then
        return false
    end
    return x >= l and x <= r and y >= b and y <= t
end
local inFrame = UI.CursorIn

-- True when the cursor is on a tile's mouse area. A popped hand card is scaled up and its frame rect is in its own
-- scaled units, while its mouse area stays the resting card's (UI.HandPlace): test that place, in the row's units.
function UI.CursorOnTile(t)
    local row = UI.handRow
    if not (t.popped and row and t.restX) then
        return UI.CursorIn(t)
    end
    local x, y = cursorPos()
    local l, top = row:GetLeft(), row:GetTop()
    if not x or not l or not top then
        return false
    end
    local k = UIParent:GetEffectiveScale() / row:GetEffectiveScale()
    x, y = x * k, y * k
    l, top = l + t.restX, top - t.restY
    return x >= l and x <= l + t.tw and y <= top and y >= top - t.th
end

-- The board slot the cursor is over (0..size), or nil when it is not over the board row.
local function boardSlotAt()
    local x, y = cursorPos()
    if not x or not UI.boardRow then
        return nil
    end
    if not inFrame(UI.boardRow) then
        return nil
    end
    local bd = BG.model and BG.model.board or {}
    local n = table.getn(bd)
    local left = UI.boardRow:GetLeft()
    for i = 1, n do
        local tile = UI.board[i]
        if tile then
            local l = tile:GetLeft()
            if l and x < l + UI.TILE_W / 2 then
                return i - 1
            end
        end
    end
    return n
end

-- The board minion (0-based index) the cursor is over, or nil (also nil off the board row: a minion's column below
-- or above the board is not the minion).
local function boardTileAt()
    local x = cursorPos()
    if not x or not UI.boardRow or not inFrame(UI.boardRow) then
        return nil
    end
    local n = table.getn(BG.model and BG.model.board or {})
    for i = 1, n do
        local tile = UI.board[i]
        local l = tile and tile:GetLeft()
        if l and x >= l and x <= l + UI.TILE_W then
            return i - 1
        end
    end
    return nil
end

-- The centre of a frame in UIParent coordinates, or nil.
local function uiCenter(frame)
    local cx, cy = frame:GetCenter()
    if not cx then
        return nil
    end
    local s = frame.GetEffectiveScale and frame:GetEffectiveScale()
    local p = UIParent and UIParent.GetEffectiveScale and UIParent:GetEffectiveScale()
    if s and p and p ~= 0 then
        return cx * s / p, cy * s / p
    end
    return cx, cy
end

-- Every frame: while a spell that needs a target is picked up (clicked or dragged), the arrow of the match window runs
-- from its card to the cursor and the minions it can be cast on glow, brighter under the cursor.
function UI.UpdateTargeting()
    local m = BG.model
    local entry, index = UI.TargetSpell(m)
    local src = entry and UI.hand and UI.hand[index + 1]
    local x, y = cursorPos()
    if not (src and UI.frame and UI.frame:IsShown() and x) then
        V.ArrowHide()
        return
    end
    local cx, cy = uiCenter(src)
    if not cx then
        V.ArrowHide()
        return
    end
    local bd = m.board or {}
    local over = boardTileAt()
    local hit = over and bd[over + 1] or nil
    V.ArrowShow(cx, cy, x, y, hit ~= nil and not BG.ValidSpellTarget(entry, hit))
    for i = 1, table.getn(bd) do
        local t = UI.board[i]
        if t and BG.ValidSpellTarget(entry, bd[i]) then
            UI.SetTileGlow(t, "green", over == i - 1 and 1 or 0.6)
        end
    end
end

function UI.StartDrag(kind, index)
    local entry = nil
    if kind == "offer" then
        entry = BG.model and BG.model.offers[index + 1]
    elseif kind == "hand" then
        entry = BG.model and BG.model.hand[index + 1]
    elseif kind == "board" then
        entry = BG.model and BG.model.board[index + 1]
    end
    if not entry then
        return
    end
    UI.HandUnpopAll()
    UI.drag = { kind = kind, index = index, id = entry.id, entry = entry }
    UI.OnTileLeave(UI.cardTile)             -- the large card of the hovered tile does not ride along
    if kind == "hand" and BG.IsSpell(entry) and BG.SpellNeedsTarget(entry) then
        -- a targeted spell is not carried: its card stays in the hand, dimmed, and the arrow leaves it
        UI.dragDim = UI.hand[index + 1]
        if UI.dragDim then
            UI.dragDim:SetAlpha(0.35)
        end
        UI.dragGhost = nil
        UI.Refresh()
        return
    end
    if kind == "hand" and UI.frame and UI.frame.artMode then
        -- a hand card is dragged as the card
        if not UI.cardGhost then
            UI.cardGhost = UI.CreateTile(UI.frame, "HearthstoneBgCardGhost", UI.HAND_W, UI.HAND_H)
            UI.cardGhost:EnableMouse(false)
            UI.LevelTile(UI.cardGhost, UI.frame:GetFrameLevel() + 100)
        end
        UI.dragGhost = UI.cardGhost
    else
        if not UI.ghost then
            UI.ghost = UI.CreateTile(UI.frame, "HearthstoneBgGhost", UI.TILE_W, UI.TILE_H)
            UI.ghost:EnableMouse(false)
            UI.ghost.noCard = true
            UI.LevelTile(UI.ghost, UI.frame:GetFrameLevel() + 100)     -- over Bob and his arch while a minion is sold
        end
        UI.dragGhost = UI.ghost
    end
    UI.FillTile(UI.dragGhost, entry, kind)
    UI.dragGhost:Show()
end

function UI.UpdateGhost()
    local g = UI.dragGhost
    if not UI.drag or not g then
        return
    end
    local x, y = cursorPos()
    if x then
        g:ClearAllPoints()
        g:SetPoint("CENTER", UIParent, "BOTTOMLEFT", x, y)
    end
    UI.BobDragGlow()
end

function UI.StopDrag()
    local d = UI.drag
    UI.drag = nil
    UI.BobDragGlow()
    if UI.dragDim then
        UI.dragDim:SetAlpha(1)
        UI.dragDim = nil
    end
    V.ArrowHide()
    if UI.ghost then
        UI.ghost:Hide()
    end
    if UI.cardGhost then
        UI.cardGhost:Hide()
    end
    if d then
        UI.DropAt(d)
    end
    UI.Refresh()
    UI.RehoverHand()
end

-- A drag whose end was lost: the drag is dropped without a drop (the cursor is no longer where it was released).
function UI.CancelDrag()
    local d = UI.drag
    UI.drag = nil
    UI.BobDragGlow()
    if UI.dragDim then
        UI.dragDim:SetAlpha(1)
        UI.dragDim = nil
    end
    V.ArrowHide()
    if UI.ghost then
        UI.ghost:Hide()
    end
    if UI.cardGhost then
        UI.cardGhost:Hide()
    end
    UI.Refresh()
    UI.RehoverHand()
end

-- OnEnter is skipped while a drag runs (no pop), and the cursor may still be on a card when it ends: raise that card.
function UI.RehoverHand()
    for i = 1, table.getn(UI.hand or {}) do
        local t = UI.hand[i]
        if t and t.entry and t:IsVisible() and not t.popped and UI.CursorIn(t) then
            UI.OnTileEnter(t)
            return
        end
    end
end

function UI.DropAt(d)
    -- A board or hand minion dropped on Bob is sold
    if (d.kind == "board" or d.kind == "hand") and UI.CursorIn(UI.BobFrame()) then
        if not sellable(d) then
            BG.message = "A spell is cast, not sold."
            return false
        end
        BG.Sell(d.kind, d.index)
        if UI.sel and UI.sel.zone == d.kind and UI.sel.index == d.index then
            UI.sel = nil
        end
        return true
    end
    -- A spell is cast only when dropped over the board (a release anywhere else, as a slipped click, does nothing).
    if d.kind == "hand" then
        local entry = BG.model and BG.model.hand[d.index + 1]
        if entry and BG.IsSpell(entry) then
            if boardSlotAt() == nil then
                return false
            end
            local over = boardTileAt()
            local bd = BG.model and BG.model.board or {}
            local target = over and bd[over + 1] and bd[over + 1].uid or 0
            if BG.SpellNeedsTarget(entry) then
                -- a targeted spell is cast only on a minion it fits; anywhere else the drag just ends
                if target ~= 0 and BG.ValidSpellTarget(entry, bd[over + 1]) then
                    UI.CastSpell(d.index, over)
                elseif target ~= 0 then
                    BG.message = BG.WHY_TEXT.BadTarget
                end
                return true
            end
            UI.CastSpell(d.index, target ~= 0 and over or nil)
            return true
        end
    end
    -- A hand card dropped on a living ally cell in the lobby column is sent to that ally
    if d.kind == "hand" and HsBgLobbyUI and HsBgLobbyUI.CellAt then
        local seat = HsBgLobbyUI.CellAt()
        if seat and BG.CanSend(seat) then
            BG.Send(d.index, seat)
            return true
        end
    end
    local slot = boardSlotAt()
    if slot == nil then
        return false                       -- dropped outside the board: nothing happens
    end
    local bd = BG.model and BG.model.board or {}
    if d.kind == "hand" then
        -- slot is an insertion point; the target is the minion under the cursor, if any
        local over = boardTileAt()
        local target = over and bd[over + 1] and bd[over + 1].uid or 0
        local entry = BG.model and BG.model.hand[d.index + 1]
        if entry and BG.IsMagnetic(entry) then
            target = UI.magnetUid or BG.MagnetUid(entry, over and bd[over + 1])   -- a non-Mech under the cursor is no target
        end
        if BG.BattlecryRule(entry) and not (over and BG.ValidBattlecryTarget(entry, bd[over + 1]))
            and BG.HasBattlecryTarget(entry, bd) then
            -- dropped where no fitting minion is: the card waits for its target (see UI.BoardClick)
            UI.sel = { zone = "hand", index = d.index, choosing = true, slot = slot }
            return true
        end
        BG.Play(d.index, slot, target)
        return true
    elseif d.kind == "board" then
        -- the place the preview showed (UI.MoveSlot); a drop on its own place moves nothing
        local to = UI.MoveSlot(d)
        if to and to ~= d.index then
            BG.Move(d.index, to)
        end
        return true
    elseif d.kind == "offer" then
        BG.Buy(d.index)                    -- a bought card lands in the hand; play it from there
        return true
    end
    return false
end

-- ------------------------------------------------------------------ layout
-- Where the first tile of a centred row starts so that `count` tiles stand in the middle of it.
local function rowStart(row, count, w)
    local span = count * w + (count - 1) * UI.ROW_GAP
    local start = (row:GetWidth() - span) / 2
    if start < 0 then
        start = 0
    end
    return start
end

-- xs (optional): each tile's x from the row's left edge (the hand's two parts); else the row is centred / packed.
-- centerCount (optional): centre only the first tiles (my board's minions; the empty slot to play into stands after them).
local function placeRow(row, tiles, count, w, h, xs, centerCount, ys, angles)
    local n = table.getn(tiles)
    for i = 1, count do
        local t = tiles[i]
        if not t then
            t = UI.CreateTile(row, row.prefix .. i, w, h)
            t.zone = row.zone
            local index = i - 1
            t:SetScript("OnClick", function()
                if arg1 == "RightButton" then
                    UI.TileRightClick(this.zone, this.index)
                else
                    UI.TileClick(this.zone, this.index)
                end
            end)
            t:RegisterForDrag("LeftButton")
            t:SetScript("OnDragStart", function() UI.StartDrag(this.zone, this.index) end)
            t:SetScript("OnDragStop", function() UI.StopDrag() end)
            tiles[i] = t
        end
        t.index = i - 1
        local x = xs and xs[i] or ((row.centered and rowStart(row, centerCount or count, w) or 0) + (i - 1) * (w + UI.ROW_GAP))
        local y = ys and ys[i] or 0
        local hh = h
        if row.zone == "hand" and t.cardTex then
            t.restX, t.restY, t.band = x, y, row:GetFrameLevel() + 1 + UI.HAND_BAND * (i - 1)
            t.restAngle = angles and angles[i] or 0
            UI.LevelTile(t, t.band)
            if t.cardMode then
                UI.CropCard(t)
            end
        end
        t:SetWidth(w)
        t:SetHeight(hh)
        if row.zone == "hand" and t.cardTex then
            UI.HandPlace(t)
        else
            t:ClearAllPoints()
            t:SetPoint("TOPLEFT", row, "TOPLEFT", x, -y)
        end
        t.wx, t.wy = (row.rx or 0) + x + w / 2, (row.ry or 0) + y + hh / 2   -- its centre in the window
        t:Show()
    end
    for i = count + 1, n do
        tiles[i]:Hide()
    end
end

-- The hand's places (x from the hand row's left edge): my hero stands in the middle of the bottom band, so the cards
-- fill the part left of it (towards the hero) first, then the part right of my power (and quest / reward icons). A
-- part that is too narrow lets its cards overlap.
function UI.HandXs(n, m)
    local R = UI.RECT
    if not UI.frame or not UI.frame.artMode then
        return nil
    end
    local w, gap = UI.HAND_W, -math.floor(UI.HAND_W * UI.HAND_OVERLAP)
    local l0, l1 = R.handLeft[1], R.handLeft[2]
    local nl, nr = n, 0                      -- the whole hand stands left of my hero, squeezed when it is long
    local r0, r1 = R.handRight[1], R.handRight[2]
    local xs = {}
    local function part(k, a, b, fromRight, first)
        if k <= 0 then
            return
        end
        local step = w + gap
        if k > 1 and k * w + (k - 1) * gap > b - a then
            step = (b - a - w) / (k - 1)
        end
        for i = 1, k do
            local x
            if fromRight then
                x = b - w - (k - i) * step
            else
                x = a + (i - 1) * step
            end
            xs[first + i - 1] = x - R.hand[1]
        end
    end
    part(nl, l0, l1, true, 1)
    part(nr, r0, r1, false, nl + 1)
    -- the fan (as in the match window): the middle of the hand stands highest and upright, the outer cards lower and
    -- leaning outwards
    local ys, angles = {}, {}
    for i = 1, n do
        local t = n > 1 and (i - (n + 1) / 2) / ((n - 1) / 2) or 0
        ys[i] = math.floor(UI.HAND_ARC * t * t + 0.5)
        angles[i] = math.rad(UI.HAND_TILT * t)
    end
    return xs, ys, angles
end

-- The insertion point (0..board size) while a minion of the hand is dragged over the board, else nil. A spell and a
-- targeted Battlecry are dropped ON a minion, so the row stays put for them. A Magnetic minion also returns the
-- board index (0-based) of the Mech it would merge into (the one under the cursor, else the one right of the gap),
-- and its gap is that Mech's left side.
function UI.PreviewSlot()
    local d = UI.drag
    local m = BG.model
    if not (d and d.kind == "hand" and not UI.dragDim and m and m.ph == "s" and table.getn(m.board) < 7) then
        return nil
    end
    local entry = m.hand[d.index + 1]
    if not entry or BG.IsSpell(entry) or BG.BattlecryRule(entry) then
        return nil
    end
    local slot = boardSlotAt()
    if slot == nil or not BG.IsMagnetic(entry) then
        return slot
    end
    local over = boardTileAt()
    local to = nil
    if over and BG.MagnetUid(entry, m.board[over + 1]) ~= 0 then
        to = over
    elseif BG.MagnetUid(entry, m.board[slot + 1]) ~= 0 then
        to = slot
    end
    if to then
        return to, to
    end
    return slot
end

-- The final place (0-based, as Shop::Move takes it) of a board minion `d` dragged over the board row, else nil: the
-- number of the other minions whose middle is left of the cursor. `d` defaults to the running drag.
function UI.MoveSlot(d)
    d = d or UI.drag
    local m = BG.model
    if not (d and d.kind == "board" and m and m.ph == "s" and m.board[d.index + 1]) then
        return nil
    end
    local x = cursorPos()
    if not x or not UI.boardRow or not inFrame(UI.boardRow) then
        return nil
    end
    local slot = 0
    for i = 1, table.getn(m.board) do
        local t = UI.board[i]
        local l = t and i ~= d.index + 1 and t:GetLeft()
        if l and x >= l + UI.TILE_W / 2 then
            slot = slot + 1
        end
    end
    return slot
end

-- Puts the board row's tiles on their places (dt nil: at once, else they glide there). With a preview slot the minions
-- from that slot on stand one place to the right, the row is centred round the gap and the empty tile (which glows)
-- is the gap; else the minions are centred and the empty slot to play into stands after them. A board minion dragged
-- over the row (UI.MoveSlot) is the same preview: its own tile is hidden (the ghost carries it), the others close up
-- round the gap at its new place, and the empty tile (on a full board, none: the gap stays empty) is the gap.
function UI.PlaceBoard(dt)
    local row = UI.boardRow
    if not row or not row.GetWidth then
        return
    end
    local nm = table.getn(BG.model and BG.model.board or {})
    local mv = UI.MoveSlot()
    local from = mv and UI.drag.index
    local slot, to = nil, nil
    if not mv then
        slot, to = UI.PreviewSlot()
    end
    local w, step = UI.TILE_W, UI.TILE_W + UI.ROW_GAP
    local pull = to and UI.ROW_GAP or 0         -- the Mech to merge into stands against the gap
    local start = 0
    if row.centered then
        start = rowStart(row, (mv and nm) or (slot and nm + 1) or math.max(1, nm), w) + pull / 2
    end
    for i = 1, nm < 7 and nm + 1 or nm do
        local t = UI.board[i]
        if t then
            local v = i - 1
            if mv then
                if i == nm + 1 or i == from + 1 then
                    v = mv
                else
                    local o = i - 1 > from and i - 2 or i - 1     -- its place among the other minions
                    v = o < mv and o or o + 1
                end
            elseif slot then
                if i == nm + 1 then
                    v = slot
                elseif v >= slot then
                    v = i
                end
            end
            local hide = mv ~= nil and i == from + 1
            if (t.moveHidden or false) ~= hide then
                t.moveHidden = hide
                t:SetAlpha(hide and 0 or 1)
            end
            local tx = start + v * step - (slot and v > slot and pull or 0)
            local x = t.bx
            if not x or not dt then
                x = tx
            else
                x = x + (tx - x) * math.min(1, dt * 16)
                if math.abs(tx - x) < 0.5 then
                    x = tx
                end
            end
            if x ~= t.bx then
                t.bx = x
                t:ClearAllPoints()
                t:SetPoint("TOPLEFT", row, "TOPLEFT", x, 0)
                t.wx = (row.rx or 0) + x + w / 2
            end
        end
    end
    local bd = BG.model and BG.model.board or {}
    UI.magnetUid = to and bd[to + 1] and bd[to + 1].uid or nil
    for i = 1, nm do
        local t = UI.board[i]
        if t and (t.magnetGlow or false) ~= (to == i - 1) then
            t.magnetGlow = to == i - 1
            UI.TileGlow(t)
        end
    end
    local gap = nm < 7 and UI.board[nm + 1] or nil
    local lit = slot ~= nil or mv ~= nil
    if gap and (gap.previewGlow or false) ~= lit then
        gap.previewGlow = lit
        UI.TileGlow(gap)
    end
end

function UI.Layout()
    local m = BG.model or BP.EmptyState()
    placeRow(UI.tavern, UI.offers, table.getn(m.offers), UI.OFFER_W, UI.OFFER_H)
    local boardCount = table.getn(m.board)
    if boardCount < 7 then
        boardCount = boardCount + 1          -- one empty slot to play into
    end
    placeRow(UI.boardRow, UI.board, boardCount, UI.TILE_W, UI.TILE_H, nil, math.max(1, table.getn(m.board)))
    for i = 1, table.getn(UI.board) do
        UI.board[i].bx = nil                 -- placeRow put them at their resting places
    end
    UI.PlaceBoard()
    local hxs, hys, has = UI.HandXs(table.getn(m.hand), m)
    placeRow(UI.handRow, UI.hand, table.getn(m.hand), UI.HAND_W, UI.HAND_H, hxs, nil, hys, has)
    local opts = table.getn(m.options)
    for i = 1, opts do
        local t = UI.discover.tiles[i]
        if not t then
            t = UI.CreateTile(UI.discover, "HearthstoneBgOption" .. i, UI.PICK_W, UI.PICK_H)
            local index = i - 1
            t.zone = "option"
            t:SetScript("OnClick", function() UI.OptionClick(this.index) end)
            UI.discover.tiles[i] = t
        end
        t.index = i - 1
        t:ClearAllPoints()
        t:SetPoint("TOPLEFT", UI.discover, "TOPLEFT",
            (UI.discover:GetWidth() - opts * UI.PICK_W - (opts - 1) * UI.ROW_GAP) / 2 + (i - 1) * (UI.PICK_W + UI.ROW_GAP), -260)
        t:Show()
    end
    for i = opts + 1, table.getn(UI.discover.tiles) do
        UI.discover.tiles[i]:Hide()
    end
    if table.getn(m.powerOptions or {}) > 0 then
        UI.LayoutDiscoverPowers(m)
    else
        UI.LayoutDiscoverCards(m)
    end
end

-- The hero-power choice as cards (the power renders in Textures\BgHeroes\PowerCards), like the minion
-- options; a power whose card is not installed falls back to its name and text on a plate. The tiles stay hidden.
function UI.LayoutDiscoverPowers(m)
    local D = UI.discover
    local ids = m.powerOptions or {}
    local w = UI.DISCOVER_CARD_W
    local h = math.floor(w * 388 / 256 + 0.5)
    local gap = 26
    local count = table.getn(ids)
    for i = 1, count do
        local c = D.powerCards[i]
        if not c then
            c = CreateFrame("Button", "HearthstoneBgDiscoverPower" .. i, D)
            c:SetWidth(w)
            c:SetHeight(h)
            c:SetFrameLevel(D:GetFrameLevel() + 3)
            c.tex = c:CreateTexture(nil, "ARTWORK")
            c.tex:SetAllPoints(c)
            c.tex:SetTexCoord(0, 1, 0, 388 / 512)
            c.name = fs(c, "GameFontNormal", "CENTER")
            -- a power without its card render wears the blank card (Textures\Bg\reward_card, as the quest rewards do):
            -- the name on the gold banner (y 186-216 of 256 wide), the text in the parchment box from y 256
            local k = w / 256
            c.name:SetPoint("TOPLEFT", c, "TOPLEFT", 58 * k, -187 * k)
            c.name:SetWidth(140 * k)
            c.name:SetHeight(30 * k)
            c.name:SetJustifyV("MIDDLE")
            c.text = fs(c, "GameFontNormalSmall", "CENTER")
            c.text:SetPoint("TOPLEFT", c, "TOPLEFT", 52 * k, -258 * k)
            c.text:SetWidth(152 * k)
            c.text:SetHeight(94 * k)
            c.text:SetJustifyV("TOP")
            if c.text.SetShadowOffset then
                c.text:SetShadowOffset(0, 0)
            end
            c.hl = c:CreateTexture(nil, "OVERLAY")
            c.hl:SetAllPoints(c)
            c.hl:SetTexCoord(0, 1, 0, 388 / 512)
            c.hl:SetBlendMode("ADD")
            c.hl:SetAlpha(0.3)
            c.hl:Hide()
            c:SetScript("OnClick", function() UI.OptionClick(this.index) end)
            V.Hover(c)
            D.powerCards[i] = c
        end
        c.index = i - 1
        local id = ids[i]
        if UI.Listed(HS_BgTextures and HS_BgTextures.powerCards, id) then
            c.tex:SetTexture(UI.HERO_PATH .. "PowerCards\\" .. id)
            c.hl:SetTexture(UI.HERO_PATH .. "PowerCards\\" .. id)
            c.tex:Show()
            c.name:SetText("")
            c.text:SetText("")
        else
            c.tex:SetTexture(UI.BG_PATH .. "reward_card")
            c.hl:SetTexture(UI.BG_PATH .. "reward_card")
            c.tex:Show()
            c.name:SetText("|cffffffff" .. BG.PowerName(id) .. "|r")
            local p = BG.HeroPower(id)
            -- the data breaks its lines for a real card; on the drawn one the text wraps to its box
            local body = string.gsub(p and p.text or "", "\n", " ")
            c.text:SetText("|cff2b1a0a" .. body .. "|r")
        end
        c:ClearAllPoints()
        c:SetPoint("TOPLEFT", D, "TOPLEFT", (UI.W - count * w - (count - 1) * gap) / 2 + (i - 1) * (w + gap), -180)
        c:Show()
    end
    for i = count + 1, table.getn(D.powerCards) do
        D.powerCards[i]:Hide()
    end
    for i = 1, table.getn(D.cards) do
        D.cards[i]:Hide()
    end
    for i = 1, table.getn(D.tiles) do
        D.tiles[i]:Hide()
    end
end

-- The Discover options as large cards when every option's card art is installed (the tiles stay hidden then).
UI.DISCOVER_CARD_W = 168
function UI.LayoutDiscoverCards(m)
    local D = UI.discover
    local opts = table.getn(m.options)
    local all = opts > 0
    for i = 1, opts do
        if not V.CardArtId(m.options[i]) then
            all = false
        end
    end
    local w = UI.DISCOVER_CARD_W
    local h = math.floor(w * 388 / 256 + 0.5)
    local gap = 26
    for i = 1, opts do
        local c = D.cards[i]
        if all then
            if not c then
                c = CreateFrame("Button", "HearthstoneBgDiscoverCard" .. i, D)
                c:SetWidth(w)
                c:SetHeight(h)
                c:SetFrameLevel(D:GetFrameLevel() + 3)
                c.tex = c:CreateTexture(nil, "ARTWORK")
                c.tex:SetAllPoints(c)
                c.tex:SetTexCoord(0, 1, 0, 388 / 512)
                c.hl = c:CreateTexture(nil, "OVERLAY")
                c.hl:SetAllPoints(c)
                c.hl:SetTexCoord(0, 1, 0, 388 / 512)
                c.hl:SetBlendMode("ADD")
                c.hl:SetAlpha(0.3)
                c.hl:Hide()
                c.line = fs(c, "GameFontHighlight", "CENTER")
                c.line:SetPoint("TOP", c, "BOTTOM", 0, 2)
                c.line:SetWidth(w + 30)
                c:SetScript("OnClick", function() UI.OptionClick(this.index) end)
                -- the allies this option would help (the wire `nd` entry of its uid): the plate above the card and the lobby glow
                V.Hover(c, function(b)
                    local mm = BG.model
                    local opt = mm and mm.options and mm.options[b.index + 1]
                    local needs = opt and mm.need and mm.need[opt.uid]
                    SetNeedSeats(needs)
                    if needs then
                        V.ShowNeedsOnly(b, UI.frame, needs)
                    end
                end, function()
                    SetNeedSeats(nil)
                    V.HideCard()
                end)
                D.cards[i] = c
            end
            c.index = i - 1
            local id = V.CardArtId(m.options[i])
            c.tex:SetTexture(V.CARD_PATH .. id)
            c.hl:SetTexture(V.CARD_PATH .. id)
            c.line:SetText(V.CardLine(m.options[i]) or "")
            c:ClearAllPoints()
            c:SetPoint("TOPLEFT", D, "TOPLEFT", (UI.W - opts * w - (opts - 1) * gap) / 2 + (i - 1) * (w + gap), -180)
            c:Show()
            D.tiles[i]:Hide()
        elseif c then
            c:Hide()
        end
    end
    for i = opts + 1, table.getn(D.cards) do
        D.cards[i]:Hide()
    end
    for i = 1, table.getn(D.powerCards or {}) do
        D.powerCards[i]:Hide()
    end
end

-- ------------------------------------------------------------------ refresh
-- My hero in its arch (portrait, health gem, armor shield) and the hero power ring beside it.
function UI.RefreshHero()
    local f = UI.frame
    if not f then
        return
    end
    local m = BG.model or BP.EmptyState()
    local hero = m.hero or ""
    if hero == "" and BG.Picking() then
        f.hero.frame:Hide()
    else
        V.SetHero(f.hero, hero, m.health, m.armor, m.maxHealth, not m.al)
        f.hero.frame:Show()
    end
    local power = m.power or ""
    if power == "" then
        f.power:Hide()
        return
    end
    f.power:Show()
    if UI.PowerArtInstalled(power) then
        f.power.art:SetTexture(UI.POWER_PATH .. power)
        f.power.art:Show()
        f.power.text:SetText("")
    else
        f.power.art:Hide()
        f.power.text:SetText("|cffffd100" .. BG.PowerName(power) .. "|r")
    end
    local cost = m.powerCost or 0
    f.power.cost:SetText(cost > 0 and ("|cffffd100" .. cost .. "|r") or "")
    local counter = m.powerCounter or 0
    if counter > 0 then
        f.power.count:SetText("|cffffd100" .. counter .. "|r")
        f.power.count:Show()
    else
        f.power.count:Hide()
    end
    if f.power.coinOk and cost > 0 then
        f.power.coin:Show()
    else
        f.power.coin:Hide()
    end
    -- dimmed while the power cannot be used now
    local v = m.powerUsable and 1 or 0.4
    f.power.art:SetVertexColor(v, v, v)
    if f.power.hasRing then
        f.power.ring:SetVertexColor(math.max(v, 0.6), math.max(v, 0.6), math.max(v, 0.6))
        f.power.ring:Show()
        f.power:SetBackdropColor(0, 0, 0, 0)
        f.power:SetBackdropBorderColor(0, 0, 0, 0)
    elseif UI.powerArm then
        f.power:SetBackdropBorderColor(1, 0.85, 0.2, 1)
    elseif m.powerUsable then
        f.power:SetBackdropBorderColor(0.45, 0.38, 0.24, 1)
    else
        f.power:SetBackdropBorderColor(0.3, 0.28, 0.22, 1)
    end
    -- the glow: gold while a target power is armed, green while it can be used in the shop
    local shop = m.ph == "s" and not m.en and not BG.Picking() and not BG.PickingQuest()
    if UI.powerArm then
        V.SetRestGlow(f.power, "gold")
    elseif m.powerUsable and shop then
        V.SetRestGlow(f.power, "green")
    else
        V.SetRestGlow(f.power, nil)
    end
end

-- My hero's tooltip: the hero's name, health and armor.
function UI.HeroEnter(b)
    local m = BG.model
    if not GameTooltip or not m or (m.hero or "") == "" then
        return
    end
    GameTooltip:SetOwner(b, "ANCHOR_TOP")
    GameTooltip:SetText(BG.HeroName(m.hero))
    GameTooltip:AddLine("Health " .. m.health .. "/" .. (m.maxHealth or m.health)
        .. ((m.armor or 0) > 0 and ("   Armor " .. m.armor) or ""), 1, 1, 1)
    GameTooltip:Show()
end

-- Bob's tooltip: the tavern's tier and what the next one costs.
function UI.BobEnter(b)
    local m = BG.model
    if not GameTooltip or not m then
        return
    end
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    GameTooltip:SetText("Bob's Tavern - Tier " .. m.tier)
    if m.upgrade and m.upgrade >= 0 then
        GameTooltip:AddLine("Upgrade to tier " .. (m.tier + 1) .. ": " .. m.upgrade .. " Gold", 1, 0.82, 0.2)
    else
        GameTooltip:AddLine("The highest tier.", 1, 1, 1)
    end
    GameTooltip:Show()
end

-- My quest icon (a round portrait with "<progress>/<required>", or a text tile without art) and the reward icon
-- once the quest is done. Both are hidden while no quest is taken / no reward is active.
function UI.RefreshQuests()
    local f = UI.frame
    if not f then
        return
    end
    local m = BG.model or BP.EmptyState()
    local q = (m.quest ~= nil and m.quest ~= "") and m.quest or (m.quest2 or "")
    local second = (m.quest == nil or m.quest == "") and (m.quest2 or "") ~= ""
    if q == "" then
        f.myQuest:Hide()
    else
        f.myQuest:Show()
        if UI.QuestArtInstalled(q) then
            f.myQuest.art:SetTexture(UI.QUEST_PATH .. q)
            f.myQuest.art:SetVertexColor(1, 1, 1)
            f.myQuest.art:Show()
            f.myQuest.name:SetText("")
        else
            f.myQuest.art:Hide()
            f.myQuest.name:SetText("|cffffd100" .. BG.QuestName(q) .. "|r")
        end
        local progress = second and BG.QuestProgress2() or BG.QuestProgress()
        local required = second and BG.QuestRequired2() or BG.QuestRequired()
        f.myQuest.progress:SetText("|cffffffff" .. progress .. "/" .. required .. "|r")
        V.SetRestGlow(f.myQuest, (second and BG.QuestDone2(m) or BG.QuestDone(m)) and "gold" or nil)
    end
    local rw = (m.reward ~= nil and m.reward ~= "") and m.reward or (m.reward2 or "")
    if rw == "" then
        f.myReward:Hide()
    else
        f.myReward:Show()
        if UI.RewardArtInstalled(rw) then
            f.myReward.art:SetTexture(UI.QUEST_PATH .. rw)
            f.myReward.art:SetVertexColor(1, 1, 1)
            f.myReward.art:Show()
            f.myReward.name:SetText("")
        else
            f.myReward.art:Hide()
            f.myReward.name:SetText("|cffffd100" .. BG.RewardName(rw) .. "|r")
        end
    end
end

-- A Secret icon on my hero portrait shows its card render on hover.
function UI.SecretEnter(b)
    local id = b and b.secretId
    if not id then
        return
    end
    if V.ShowCard({ id = id }, b, UI.frame) then
        return
    end
    if not GameTooltip then
        return
    end
    local card = BG.Card(id)
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    GameTooltip:SetText(card and card.name or id)
    if card and card.text and card.text ~= "" then
        GameTooltip:AddLine(card.text, 1, 1, 1)
    end
    GameTooltip:Show()
end

-- The Secrets in play, in play order, as small icons along the bottom of my hero portrait.
function UI.RefreshSecrets()
    local f = UI.frame
    if not f or not f.secrets then
        return
    end
    local m = BG.model or BP.EmptyState()
    for i = 1, table.getn(f.secrets) do
        local b = f.secrets[i]
        local id = m.secrets and m.secrets[i]
        if id then
            b.secretId = id
            b.art:Show()
            b:Show()
        else
            b.secretId = nil
            b:Hide()
        end
    end
end

-- The gold of this turn: the most gold seen since the round began (the game's "<gold>/<max>"; selling can go above).
function UI.TurnGold(m)
    if UI.goldRound ~= m.rd then
        UI.goldRound = m.rd
        UI.goldMax = m.gold
    elseif m.gold > (UI.goldMax or 0) then
        UI.goldMax = m.gold
    end
    return UI.goldMax or m.gold
end

function UI.RefreshGold(m)
    local f = UI.frame
    local max = UI.TurnGold(m)
    f.gold:SetText("|cffffffff" .. m.gold .. "/" .. max .. "|r")
    local shown = max
    if m.gold > shown then
        shown = m.gold
    end
    for i = 1, table.getn(f.coins) do
        local c = f.coins[i]
        if i <= shown then
            if i <= m.gold then
                c:SetVertexColor(1, 1, 1)
            else
                c:SetVertexColor(0.3, 0.27, 0.22)
            end
            c:Show()
        else
            c:Hide()
        end
    end
end

-- A notice in the middle of the board. sticky: a hint that stays until it is no longer true (Refresh clears it).
function UI.Toast(text, sticky)
    local f = UI.frame
    if not f then
        return
    end
    if not text or text == "" then
        f.message:SetText("")
        f.toast:Hide()
        UI.toastText, UI.toastAt = nil, nil
        return
    end
    f.message:SetText(text)
    local w = f.message:GetStringWidth() + 40
    if w < 200 then
        w = 200
    end
    if w > UI.RECT.toast[3] then
        w = UI.RECT.toast[3]
    end
    f.toast:SetWidth(w)
    f.toast:SetAlpha(1)
    f.toast:Show()
    if UI.toastText ~= text then
        UI.toastText = text
        UI.toastAt = (not sticky) and (BG.clock or 0) or nil
    elseif sticky then
        UI.toastAt = nil
    end
end

-- Fades a passing notice out once it has been read.
function UI.ToastTick()
    local f = UI.frame
    if not f or not UI.toastAt or not f.toast:IsShown() then
        return
    end
    local age = (BG.clock or 0) - UI.toastAt
    if age > UI.TOAST_TIME + 0.4 then
        f.toast:Hide()
        UI.toastAt = nil
        if BG.message == UI.toastText then
            BG.message = nil
        end
    elseif age > UI.TOAST_TIME then
        f.toast:SetAlpha(1 - (age - UI.TOAST_TIME) / 0.4)
    end
end

-- Shows or hides the shop's own pieces: the tavern, the warband, the hand, Bob and the shop buttons. The pick panels
-- and the fight (which draws both warbands and the opponent in Bob's arch) hide them.
function UI.ShowShop(on)
    local f = UI.frame
    local list = { UI.tavern, UI.boardRow, UI.handRow, f.upgrade, f.refresh, f.freeze, f.endTurn }
    if not f.tierHidden then
        table.insert(list, f.tier)
    end
    for i = 1, table.getn(list) do
        if on then
            list[i]:Show()
        else
            list[i]:Hide()
        end
    end
    for i = 1, table.getn(f.pips) do
        if on and not f.tierFrame then
            f.pips[i]:Show()
        else
            f.pips[i]:Hide()
        end
    end
    if f.tierFrame then
        if on then
            f.tierFrame:Show()
        else
            f.tierFrame:Hide()
        end
    end
    local bob = { f.bobArt }
    for i = 1, 1 do
        if bob[i] then
            if on then
                bob[i]:Show()
            else
                bob[i]:Hide()
            end
        end
    end
end

-- ------------------------------------------------------------------ ally view (a peek)
-- The read-only overlay of an ally's field: its hero, tier, gold, board and hand. Clicks on its board send the picked-up
-- hand card to the ally (the normal send path); right click / Escape closes it.
function UI.AllyView()
    if UI.allyView or not UI.frame then
        return UI.allyView
    end
    local f = CreateFrame("Frame", "HearthstoneBgAllyView", UI.frame)
    f:SetWidth(650)
    f:SetHeight(286)
    f:SetPoint("TOP", UI.frame, "TOP", 0, -70)
    f:SetFrameStrata("DIALOG")
    f:SetFrameLevel(UI.frame:GetFrameLevel() + 20)
    f:SetBackdrop({ bgFile = UI.WHITE, edgeFile = UI.EDGE, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f:SetBackdropColor(0.05, 0.05, 0.04, 0.94)
    f:SetBackdropBorderColor(0.35, 0.8, 0.45, 1)
    f:EnableMouse(true)
    f.portrait = f:CreateTexture(nil, "ARTWORK")
    f.portrait:SetWidth(42)
    f.portrait:SetHeight(42)
    f.portrait:SetPoint("TOPLEFT", f, "TOPLEFT", 12, -10)
    f.title = fs(f, "GameFontNormal", "LEFT")
    f.title:SetPoint("TOPLEFT", f, "TOPLEFT", 62, -10)
    f.title:SetWidth(460)
    f.stats = fs(f, "GameFontNormalSmall", "LEFT")
    f.stats:SetPoint("TOPLEFT", f, "TOPLEFT", 62, -30)
    f.stats:SetWidth(460)
    f.close = makeButton(f, "HearthstoneBgAllyClose", 28, 20, "X", function()
        BG.ClosePeek()
        UI.Refresh()
    end)
    f.close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -10, -10)
    f.boardLabel = fs(f, "GameFontNormalSmall", "LEFT")
    f.boardLabel:SetPoint("TOPLEFT", f, "TOPLEFT", 12, -54)
    f.boardLabel:SetText("|cffffd100Warband|r")
    f.handLabel = fs(f, "GameFontNormalSmall", "LEFT")
    f.handLabel:SetPoint("TOPLEFT", f, "TOPLEFT", 12, -150)
    f.handLabel:SetText("|cffffd100Hand|r")
    -- the whole board row is the send target: a picked-up hand card dropped here goes to the ally
    f.sendButton = CreateFrame("Button", nil, f)
    f.sendButton:SetWidth(620)
    f.sendButton:SetHeight(92)
    f.sendButton:SetPoint("TOP", f, "TOP", 0, -62)
    f.sendButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    f.sendButton:SetScript("OnClick", function()
        local a = BG.ally
        if arg1 == "RightButton" then
            BG.ClosePeek()
        elseif a and UI.sel and UI.sel.zone == "hand" and BG.CanSend(a.seat) then
            BG.Send(UI.sel.index, a.seat)
            UI.sel = nil
            BG.message = "Sent to " .. BG.SeatName(a.seat) .. "."
        else
            BG.message = "Pick up one of your hand cards first, then click the ally's board to send it."
        end
        UI.Refresh()
    end)
    f.hint = fs(f, "GameFontNormalSmall", "CENTER")
    f.hint:SetPoint("BOTTOM", f, "BOTTOM", 0, 8)
    f.hint:SetWidth(620)
    f.hint:SetText("Pick up a hand card, then click this board to send it. Right click or Escape closes.")
    f.board = {}
    for i = 1, 7 do
        local t = UI.CreateTile(f, "HearthstoneBgAllyBoard" .. i, 64, 78)
        t:EnableMouse(false)
        t:SetPoint("TOPLEFT", f, "TOPLEFT", 12 + (i - 1) * 68, -68)
        f.board[i] = t
    end
    f.hand = {}
    for i = 1, 10 do
        local t = UI.CreateTile(f, "HearthstoneBgAllyHand" .. i, 56, 84)
        t:EnableMouse(false)
        t:SetPoint("TOPLEFT", f, "TOPLEFT", 12 + (i - 1) * 60, -164)
        f.hand[i] = t
    end
    f:Hide()
    UI.allyView = f
    return f
end

function UI.RefreshAllyView(m)
    local f = UI.AllyView()
    if not f then
        return
    end
    local a = BG.ally
    if BG.peekSeat == nil or not a or a.seat ~= BG.peekSeat or (m and m.ph ~= "s") then
        f:Hide()
        return
    end
    f.title:SetText("|cff4cff4cViewing " .. (a.name ~= "" and a.name or BG.SeatLabel(a.seat)) .. "|r")
    f.stats:SetText("Tier " .. a.tier .. "    " .. a.gold .. " Gold    " .. a.health .. " health"
        .. ((a.armor or 0) > 0 and ("+" .. a.armor) or "") .. (a.power ~= "" and ("    " .. BG.PowerName(a.power)) or ""))
    local hero = a.hero or ""
    if hero ~= "" and HsBgLobbyUI and HsBgLobbyUI.HeroInstalled and HsBgLobbyUI.HeroInstalled(hero) then
        f.portrait:SetTexture(HsBgLobbyUI.HERO_PATH .. hero)
        f.portrait:SetVertexColor(1, 1, 1)
    else
        f.portrait:SetTexture("Interface\\Buttons\\WHITE8X8")
        f.portrait:SetVertexColor(0.22, 0.30, 0.42)
    end
    for i = 1, 7 do
        UI.FillTile(f.board[i], a.board[i], a.board[i] and "board" or "empty")
    end
    for i = 1, 10 do
        local t = f.hand[i]
        if a.hand[i] then
            UI.FillTile(t, a.hand[i], "hand")
            t:Show()
        else
            UI.FillTile(t, nil, "empty")
            t:Hide()
        end
    end
    f:Show()
end

function UI.Refresh()
    -- "loading" (a BG_GET is out) shows nothing: a server with the mode off never answers, and the window cannot be closed.
    if BG.state == "none" or BG.state == "loading" then
        if HsBgCombatUI then
            HsBgCombatUI.Hide()
        end
        if UI.frame then
            UI.frame:Hide()
        end
        V.HideCard()
        if UI.allyView then
            UI.allyView:Hide()
        end
        UI.powerArm, UI.sel = nil, nil      -- nothing armed carries into the next game
        UI.SyncEscape()
        return
    end
    local f = UI.Create()
    if not f then
        return
    end
    if not f:IsShown() then
        f:Show()
    end
    if HsBgCombatUI and HsBgCombatUI.IsPlaying() then
        UI.ShowShop(false)                   -- the fight keeps the board as it was until it is over
        UI.discover:Hide()
        if UI.allyView then
            UI.allyView:Hide()
        end
        return
    end
    local m = BG.model or BP.EmptyState()
    UI.shownModel = m                        -- what the window shows (the fight reads the health before it from here)
    if UI.sel then       -- a new state can shrink the zone the selection points into
        local count = table.getn(UI.sel.zone == "hand" and m.hand or m.board)
        if UI.sel.index >= count then
            UI.sel = nil
        end
    end
    -- an armed power ends with the shop turn it was armed in, or when the state says it cannot be used now
    if UI.powerArm and (BG.Picking() or m.ph ~= "s" or m.en or not m.powerUsable
        or BG.PowerTarget() ~= UI.powerArm or m.rd ~= UI.powerArmRound) then
        UI.powerArm = nil
    end
    if not UI.powerArm then
        UI.powerFirst = nil
    end
    UI.Layout()
    -- While a pick is open the shop stays hidden (the pick panels show the choices)
    local fighting = false
    UI.ShowShop(not (BG.Picking() or BG.PickingQuest()))
    local shopTurn = m.ph == "s" and not m.en and not BG.Picking() and not BG.PickingQuest()
    for i = 1, table.getn(m.offers) do
        UI.FillTile(UI.offers[i], m.offers[i], "offer")
        UI.offers[i].restGlow = (UI.powerArm == "o" or UI.powerArm == "m") and "green" or nil
        UI.TileGlow(UI.offers[i])
    end
    local bd = m.board
    for i = 1, table.getn(UI.board) do
        local t = UI.board[i]
        if i <= table.getn(bd) then
            UI.FillTile(t, bd[i], "board")
        else
            UI.FillTile(t, nil, "empty")
        end
        if UI.sel and UI.sel.zone == "board" and UI.sel.index == i - 1 then
            t.restGlow = "gold"
            if not t.artMode then
                t:SetBackdropBorderColor(1, 0.85, 0.2, 1)
            end
        elseif (UI.powerArm == "f" or UI.powerArm == "m") and bd[i] then
            t.restGlow = "green"
        elseif bd[i] and UI.TargetSpell(m) and BG.ValidSpellTarget(UI.TargetSpell(m), bd[i]) then
            t.restGlow = "green"                -- a targeted spell is picked up: the minions it can be cast on glow
        elseif bd[i] and UI.sel and UI.sel.choosing and BG.ValidBattlecryTarget(m.hand[UI.sel.index + 1], bd[i]) then
            t.restGlow = "green"                -- a Battlecry waits for its target: the minions it can aim at glow
        else
            t.restGlow = nil
        end
        UI.TileGlow(t)
    end
    -- the hand: green while a card can be played (a free board slot in the shop turn), gold when picked up or just sent
    local canPlay = shopTurn and table.getn(bd) < 7
    local giftLit = table.getn(m.gifts) > 0 and BG.giftAt and (BG.clock or 0) - BG.giftAt < 3
    for i = 1, table.getn(m.hand) do
        local t = UI.hand[i]
        UI.FillTile(t, m.hand[i], "hand")
        local glow = (BG.IsSpell(m.hand[i]) and shopTurn) and "green" or (canPlay and "green" or nil)
        if UI.sel and UI.sel.zone == "hand" and UI.sel.index == i - 1 then
            glow = "gold"
            if not t.artMode then
                t:SetBackdropBorderColor(1, 0.85, 0.2, 1)
            end
        end
        if giftLit then
            for g = 1, table.getn(m.gifts) do
                if m.hand[i].id == m.gifts[g] then
                    glow = "gold"
                    if not t.artMode then
                        t:SetBackdropBorderColor(1, 0.85, 0.2, 1)
                    end
                end
            end
        end
        t.restGlow = glow
        UI.TileGlow(t)
        -- an open ally view marks the hand cards that would help that ally (the wire `nd` reasons)
        if BG.peekSeat then
            local needs = m.need and m.need[m.hand[i].uid]
            for j = 1, table.getn(needs or {}) do
                if needs[j].seat == BG.peekSeat then
                    t.kw:SetText((t.kw:GetText() or "") .. "  |cff4cff4c" .. needs[j].reason .. "|r")
                end
            end
        end
    end
    for i = 1, table.getn(m.options) do
        UI.FillTile(UI.discover.tiles[i], m.options[i], "option")
    end
    if UI.discover.banner then
        UI.discover.banner:Hide()
    end
    UI.discover.text:Show()
    if table.getn(m.options) > 0 then
        local first = m.options[1]
        local secret = first and string.sub(first.id or "", 1, 16) == "TB_Bacon_Secrets"
        UI.discover.text:SetText(secret and "|cffffd100Choose a Secret|r"
            or ((first and BG.IsSpell(first)) and "|cffffd100Choose a spell|r" or "|cffffd100Choose a minion|r"))
    elseif table.getn(m.powerOptions or {}) > 0 then
        UI.discover.text:SetText("|cffffd100Choose a hero power|r")
        if UI.discover.banner then
            UI.discover.banner:Show()
            UI.discover.text:Hide()
        end
    end
    UI.RefreshGold(m)
    f.title:SetText("|cffffd100" .. BP.NAME .. "|r" .. ((m.rd or 0) > 0 and ("  |cffb8b8b8- Round " .. m.rd .. "|r") or ""))
    f.tier:SetText("Tier " .. m.tier)
    if f.tierShield then
        UI.SetTierShield(f.tierShield, m.tier, UI.TIER_SHIELD_W)
    end
    for i = 1, table.getn(f.pips) do
        if i <= m.tier then
            f.pips[i]:SetVertexColor(1, 1, 1)
        else
            f.pips[i]:SetVertexColor(0.35, 0.30, 0.22)
        end
    end
    local gold = m.gold or 0
    if m.upgrade < 0 then
        UI.SetButton(f.upgrade, "Upgrade (max)", "-", "Max tier")
        f.upgrade.tip = { "Tavern tier " .. m.tier, "The tavern is at its highest tier." }
    else
        UI.SetButton(f.upgrade, "Upgrade (" .. m.upgrade .. ")", m.upgrade, "Upgrade")
        f.upgrade.tip = { "Upgrade Tavern", "Raise the tavern to tier " .. (m.tier + 1) .. ": stronger minions are offered.",
            m.upgrade .. " Gold" }
    end
    local refreshCost = m.refreshCost or 1
    UI.SetButton(f.refresh, "Refresh", refreshCost, "Refresh")
    f.refresh.tip = { "Refresh", "Get a new set of minions.",
        refreshCost == 0 and "Free (your next Refresh is discounted)" or (refreshCost .. " Gold") }
    UI.SetButton(f.freeze, m.frozen and "Unfreeze" or "Freeze", 0, m.frozen and "Unfreeze" or "Freeze")
    f.freeze.tip = m.frozen and { "Unfreeze", "The tavern's minions are no longer kept." }
        or { "Freeze", "Keep the tavern's minions for your next turn." }
    UI.SetButton(f.endTurn, m.en and "Waiting..." or "End Turn")
    f.endTurn.tip = m.en and { "Waiting", "The other players are still shopping." } or nil
    UI.DimButton(f.endTurn, m.en)
    -- green glows: what can be used now (the game marks the affordable shop buttons the same way)
    V.SetRestGlow(f.upgrade, (shopTurn and m.upgrade >= 0 and gold >= m.upgrade) and "green" or nil)
    V.SetRestGlow(f.refresh, (shopTurn and gold >= 1) and "green" or nil)
    V.SetRestGlow(f.freeze, (shopTurn and table.getn(m.offers) > 0) and (m.frozen and "ice" or "green") or nil)
    V.SetRestGlow(f.endTurn, (shopTurn and gold == 0) and "green" or nil)
    f.endTurn.noHover = m.en and true or nil
    UI.RefreshHero()
    UI.RefreshQuests()
    UI.RefreshSecrets()
    -- the notice: a hint while something is picked up / armed (stays), a message of the state (fades)
    local hint = nil
    if BG.Picking() or BG.PickingQuest() then
        hint = nil                           -- the pick panels say what to do
    elseif UI.powerArm then
        hint = (UI.powerArm == "o" and "Click a tavern minion for the hero power, or right click to cancel."
            or UI.powerArm == "s" and "Click a player in the lobby for the hero power, or right click to cancel."
            or UI.powerArm == "m" and (UI.powerFirst and "Click the second minion, or right click to cancel."
                or "Click a minion of the board or the tavern, then a second one, or right click to cancel.")
            or "Click a friendly minion for the hero power, or right click to cancel.")
    elseif m.options and table.getn(m.options) > 0 then
        hint = "Discover: choose one minion."
    elseif BG.SkipTurn() then
        hint = "Your turn is skipped."   -- A. F. Kay / Ambassador Faelin
    elseif UI.sel then
        local picked = UI.sel.zone == "hand" and m.hand[UI.sel.index + 1]
        if picked and BG.IsSpell(picked) and BG.SpellNeedsTarget(picked) then
            hint = "Choose a friendly minion to cast it on, or right click to cancel."
        elseif picked and UI.sel.choosing and BG.BattlecryRule(picked) then
            hint = "Choose a friendly " .. BG.BattlecryRule(picked).label .. " for the Battlecry, or right click to cancel."
        elseif picked and not BG.IsSpell(picked) and BG.IsMagnetic(picked) then
            hint = "Magnetic: play it directly left of a Mech (or on one) to merge, or elsewhere as a minion."
        elseif picked and BG.IsSpell(picked) then
            hint = "Now click a board slot to cast it, or right click to cancel."
        else
            hint = (UI.sel.zone == "hand" and "Now click a board slot to play it, or an ally to send it."
                or "Now click a slot to move it.")
        end
    end
    local msg = BG.message or ""
    if hint then
        UI.Toast(msg ~= "" and (msg .. "  " .. hint) or hint, true)
    elseif msg ~= "" then
        UI.Toast(msg, false)
    elseif UI.toastAt == nil then
        UI.Toast(nil)
    end
    if BG.Picking() then
        f.message:SetText("Choose your hero to begin.")
    elseif BG.PickingQuest() then
        f.message:SetText("Choose a quest and its reward.")
    end
    if HsBgLobbyUI then
        HsBgLobbyUI.Refresh()
    end
    UI.RefreshAllyView(m)
    if (table.getn(m.options) > 0 or table.getn(m.powerOptions or {}) > 0)
        and not BG.Picking() and not BG.PickingQuest() and not fighting then
        -- a new choice (other options) opens shown; Hide stays only for the choice it was clicked on
        local key = ""
        for i = 1, table.getn(m.options) do
            key = key .. (m.options[i].uid or 0) .. (m.options[i].id or "") .. ","
        end
        for i = 1, table.getn(m.powerOptions or {}) do
            key = key .. m.powerOptions[i] .. ","
        end
        if key ~= UI.discoverKey then
            UI.discoverKey = key
            UI.discoverHidden = nil
        end
        UI.discover:Show()
        UI.ApplyDiscoverHidden()
    else
        UI.discoverKey, UI.discoverHidden = nil, nil
        UI.discover:Hide()
        UI.ApplyDiscoverHidden()
    end
    if HsBgHeroPickUI then
        HsBgHeroPickUI.Refresh()
    end
    if HsBgQuestPickUI then
        HsBgQuestPickUI.Refresh()
    end
    if HsBgEndUI then
        HsBgEndUI.Refresh()
    end
    if not BG.Picking() and not BG.PickingQuest() then
        UI.ShopFx(m)
    end
    UI.SyncEscape()
end

-- Small feedback on what changed since the last drawn state of the same round: the gold spent or gained floats up from
-- the gold pill, a new tavern tier lights Bob's arch, a fresh set of offers flashes in, a bought card glows in the hand.
function UI.ShopFx(m)
    local last = UI.fxLast
    local offers = {}
    for i = 1, table.getn(m.offers) do
        offers[i] = m.offers[i].uid or i
    end
    local board = {}
    for i = 1, table.getn(m.board) do
        board[i] = { uid = m.board[i].uid or i, a = m.board[i].atk, h = m.board[i].hp }
    end
    UI.fxLast = { rd = m.rd, gold = m.gold, tier = m.tier, offers = offers, hand = table.getn(m.hand), board = board }
    if last and last.rd ~= m.rd then
        UI.RestFx(m)
    end
    if not last or last.rd ~= m.rd or not UI.frame or not UI.frame.artMode then
        return
    end
    local R = UI.RECT
    if m.gold ~= last.gold then
        local d = m.gold - last.gold
        V.Float(R.goldPill[1], R.goldPill[2] - 16, (d > 0 and "+" or "") .. d, 1, 0.85, 0.2, 0.9, 18)
        V.Sound("LOOTWINDOWCOINSOUND")
    end
    if m.tier > last.tier then
        V.Pulse(R.bobArt[1] + R.bobArt[3] / 2, R.bobArt[2] + R.bobArt[4] / 2, R.bobArt[3] + 20, R.bobArt[4] + 20,
            1, 0.85, 0.3, 0.9, "glow_arch", 0.5)
        V.Float(R.bobArt[1] + R.bobArt[3] / 2, R.bobArt[2] + R.bobArt[4] + 20, "Tier " .. m.tier, 1, 0.85, 0.2, 1.5, 22)
        V.Sound("LEVELUPSOUND")
    end
    -- a new set of offers (Refresh, a new turn): none of the old ones is left
    local fresh = table.getn(offers) > 0
    for i = 1, table.getn(offers) do
        for j = 1, table.getn(last.offers) do
            if offers[i] == last.offers[j] then
                fresh = false
            end
        end
    end
    if fresh then
        for i = 1, table.getn(m.offers) do
            local t = UI.offers[i]
            if t and t.wx then
                V.Pulse(t.wx, t.wy, t.tw, t.th, 0.8, 0.8, 1, 0.4, "glow_oval", 0.4)
            end
        end
    end
    if table.getn(m.hand) > last.hand then
        local t = UI.hand[table.getn(m.hand)]
        if t and t.wx then
            V.Pulse(t.wx, t.wy, t.tw, t.th, 1, 0.85, 0.3, 0.5, "glow_oval", 0.5)
        end
    end
    -- a board minion whose stats went up (a Blood Gem played by an effect): a green buff glow
    if last.board then
        for i = 1, table.getn(board) do
            local now = board[i]
            local before = nil
            for j = 1, table.getn(last.board) do
                if last.board[j].uid == now.uid then
                    before = last.board[j]
                end
            end
            local t = UI.board[i]
            if before and t and t.wx and (now.a > before.a or now.h > before.h) then
                V.Pulse(t.wx, t.wy, t.tw, t.th, 0.3, 1, 0.4, 0.55, "glow_oval", 0.45)
            end
        end
    end
end

-- The round from which resting heals nothing (HsBgGame.h kRestHealEndRound). The state after a rest is the next round.
UI.REST_HEAL_END_ROUND = 20

-- My faction is the larger one and I sat the last round out (the others take turns fighting): a notice and the health
-- I healed rising from my hero.
function UI.RestFx(m)
    local me = m.lobby and m.lobby[m.me + 1]
    if not me or not me.alive or me.opp ~= BP.RESTED then
        return
    end
    local healed = m.restHealed or 0
    if healed > 0 then
        UI.Toast("No opponent for you this round: you rested and healed " .. healed .. " health.")
    elseif (m.rd or 0) - 1 >= UI.REST_HEAL_END_ROUND then
        UI.Toast("No opponent for you this round: you rested (from round " .. UI.REST_HEAL_END_ROUND
            .. " resting no longer heals).")
    else
        UI.Toast("No opponent for you this round: you rested (already at full health).")
    end
    local a = UI.frame and UI.frame.heroActor
    if a and healed > 0 then
        local cx, cy = V.Center(a)
        V.Pulse(cx, cy, a.w * 0.9, a.h * 0.7, 0.3, 1, 0.4, 0.8, nil, 0.4)
        V.Float(cx, cy - 10, "+" .. healed, 0.3, 1, 0.4, 1.4, 34)
    end
end

function UI.Show()
    UI.Create()
    UI.frame:Show()
    UI.Refresh()
end

function UI.Hide()
    if UI.frame then
        UI.hiding = true
        UI.frame:Hide()
        UI.hiding = false
        UI.SyncEscape()
    end
end

function UI.IsShown()
    return UI.frame ~= nil and UI.frame:IsShown()
end

-- The combat playback finished: the shop is current again.
function UI.OnCombatDone()
    UI.endGuard = UI.END_GUARD
    UI.Refresh()
end

-- ------------------------------------------------------------------ Wheel of Yogg-Saron
-- Yogg-tastic Tasties: the server rolls the outcome at the start of the turn and sends it (`whl`); the window spins the
-- wheel picture (Textures\Bg\yogg_wheel) under a fixed red pointer, with a purple halo, and lets it stop on the icon of
-- that outcome. There is no texture rotation in 1.12: the spin turns the texture coordinates of the square instead.
UI.WHEEL_SIZE = 250
UI.WHEEL_SPIN, UI.WHEEL_FADE = 3.4, 0.4
-- the outcome names (HsBgRewards WheelOutcomeName order) and where their icon sits on the picture (degrees clockwise from the top)
UI.WHEEL_NAME = { [0] = "Mysterybox", "Hand of Fate", "Curse of Flesh", "Mindflayer Goggles", "Devouring Hunger", "Rod of Roasting" }
UI.WHEEL_ANGLE = { [0] = 18, 193, 260, 322, 82, 142 }
-- what each outcome does (HsBgRewardsShop WheelOutcome), shown on a card beside the wheel once it has stopped
UI.WHEEL_TEXT = { [0] = "Your Hero Power triggers an extra time.",
    "|cffffd100Discover|r a Darkmoon Prize.",
    "Give your minions +3/+3, then shuffle their stats.",
    "Steal all minions in Bob's Tavern, then |cffffd100Refresh|r it.",
    "Consume all minions in Bob's Tavern: their stats go to random friendly minions. Then |cffffd100Refresh|r.",
    "Give random friendly minions +4/+4, a few times over." }
UI.WHEEL_CARD_W = 168

function UI.WheelFrame()
    if UI.wheel then
        return UI.wheel
    end
    local f = UI.frame
    local w = CreateFrame("Frame", "HearthstoneBgWheel", f)
    w:SetAllPoints(f)
    w:SetFrameLevel(f:GetFrameLevel() + UI.OVERLAY_BASE + 40)
    w:EnableMouse(true)
    w:SetBackdrop({ bgFile = UI.WHITE })
    w:SetBackdropColor(0, 0, 0, 0.55)
    local size = UI.WHEEL_SIZE
    w.glow = w:CreateTexture(nil, "BORDER")
    w.glow:SetTexture(UI.BG_PATH .. "yogg_glow")
    w.glow:SetBlendMode("ADD")
    w.glow:SetWidth(size * 1.5)
    w.glow:SetHeight(size * 1.5)
    w.glow:SetPoint("CENTER", w, "CENTER", 0, 0)
    w.disc = w:CreateTexture(nil, "ARTWORK")
    w.disc:SetTexture(UI.BG_PATH .. "yogg_wheel")
    w.disc:SetWidth(size)
    w.disc:SetHeight(size)
    w.disc:SetPoint("CENTER", w, "CENTER", 0, 0)
    -- the pointer: 1 : 2 canvas, its tip a little over the rim at the top of the wheel
    w.pointer = w:CreateTexture(nil, "OVERLAY")
    w.pointer:SetTexture(UI.BG_PATH .. "yogg_pointer")
    w.pointer:SetWidth(size * 0.3)
    w.pointer:SetHeight(size * 0.6)
    w.pointer:SetPoint("BOTTOM", w.disc, "TOP", 0, -size * 0.1)
    w.title = fs(w, "GameFontNormalLarge", "CENTER")
    w.title:SetPoint("BOTTOM", w.disc, "TOP", 0, size * 0.55)
    w.title:SetText("|cffffd100Wheel of Yogg-Saron|r")
    w.result = fs(w, "GameFontNormal", "CENTER")
    w.result:SetPoint("TOP", w.disc, "BOTTOM", 0, -18)
    -- the outcome as a card to the right of the wheel (the blank reward card: name on the banner, text in the box)
    local cw = UI.WHEEL_CARD_W
    local k = cw / 256
    w.card = CreateFrame("Frame", nil, w)
    w.card:SetWidth(cw)
    w.card:SetHeight(math.floor(cw * 388 / 256 + 0.5))
    w.card:SetPoint("LEFT", w.disc, "RIGHT", 40, 0)
    w.card:SetFrameLevel(w:GetFrameLevel() + 2)
    w.card.tex = w.card:CreateTexture(nil, "ARTWORK")
    w.card.tex:SetAllPoints(w.card)
    w.card.tex:SetTexture(UI.BG_PATH .. "reward_card")
    w.card.tex:SetTexCoord(0, 1, 0, 388 / 512)
    w.card.name = fs(w.card, "GameFontNormal", "CENTER")
    w.card.name:SetPoint("TOPLEFT", w.card, "TOPLEFT", 58 * k, -187 * k)
    w.card.name:SetWidth(140 * k)
    w.card.name:SetHeight(30 * k)
    w.card.name:SetJustifyV("MIDDLE")
    w.card.text = fs(w.card, "GameFontNormalSmall", "CENTER")
    w.card.text:SetPoint("TOPLEFT", w.card, "TOPLEFT", 52 * k, -258 * k)
    w.card.text:SetWidth(152 * k)
    w.card.text:SetHeight(94 * k)
    w.card.text:SetJustifyV("TOP")
    if w.card.text.SetShadowOffset then
        w.card.text:SetShadowOffset(0, 0)
    end
    w.card:Hide()
    w:SetScript("OnMouseUp", function()
        UI.WheelSkip()
    end)
    w:Hide()
    UI.wheel = w
    return w
end

-- Turns the wheel picture clockwise by `deg` (corners UL, LL, UR, LR; samples outside the picture are transparent).
function UI.WheelTurn(deg)
    local w = UI.wheel
    local r = math.rad(deg)
    local c, s = math.cos(r), math.sin(r)
    local function uv(u, v)
        return 0.5 + c * u + s * v, 0.5 - s * u + c * v
    end
    local u1, v1 = uv(-0.5, -0.5)
    local u2, v2 = uv(-0.5, 0.5)
    local u3, v3 = uv(0.5, -0.5)
    local u4, v4 = uv(0.5, 0.5)
    w.disc:SetTexCoord(u1, v1, u2, v2, u3, v3, u4, v4)
end

function UI.WheelStart(outcome)
    local w = UI.WheelFrame()
    local spins = 4
    local stop = (360 - UI.WHEEL_ANGLE[outcome]) + 360 * spins    -- the outcome's icon ends under the pointer at the top
    UI.spin = { outcome = outcome, t = 0, from = 0, to = stop }
    w.result:SetText("")
    w.card:Hide()
    w:SetAlpha(1)
    UI.WheelTurn(0)
    w:Show()
end

-- Click: the wheel stops where it is going to stop; a click on the stopped wheel and its card closes it.
function UI.WheelSkip()
    local sp = UI.spin
    if not sp then
        return
    end
    if sp.t < UI.WHEEL_SPIN then
        sp.t = UI.WHEEL_SPIN
    elseif not sp.closeAt then
        sp.closeAt = sp.t
    end
end

function UI.WheelTick(dt)
    local sp = UI.spin
    if not sp then
        -- the next outcome waits until the window shows and the fight is over
        if BG.wheelQueue and table.getn(BG.wheelQueue) > 0 and UI.frame and UI.frame:IsShown()
            and not (HsBgCombatUI and HsBgCombatUI.IsPlaying()) then
            local outcome = BG.TakeWheel()
            if outcome and UI.WHEEL_NAME[outcome] then
                UI.WheelStart(outcome)
            end
        end
        return
    end
    local w = UI.wheel
    if not (UI.frame and UI.frame:IsShown()) then
        UI.spin = nil
        w:Hide()
        return
    end
    sp.t = sp.t + (dt or 0)
    if sp.t < UI.WHEEL_SPIN then
        local u = sp.t / UI.WHEEL_SPIN
        local e = 1 - (1 - u) * (1 - u) * (1 - u)        -- fast at first, settling on the outcome
        UI.WheelTurn(sp.from + (sp.to - sp.from) * e)
        w.glow:SetAlpha(0.65 + 0.2 * math.sin(sp.t * 6))
    else
        UI.WheelTurn(sp.to)
        w.glow:SetAlpha(0.9)
        if not w.card:IsShown() then
            w.card.name:SetText("|cffffffff" .. UI.WHEEL_NAME[sp.outcome] .. "|r")
            w.card.text:SetText("|cff2b1a0a" .. UI.WHEEL_TEXT[sp.outcome] .. "|r")
            w.card:Show()
            w.result:SetText("|cffaaaaaaClick to continue|r")
        end
        -- the card stays until it is clicked away, then the whole wheel fades
        if sp.closeAt then
            local over = sp.t - sp.closeAt
            if over >= UI.WHEEL_FADE then
                UI.spin = nil
                w:Hide()
            else
                w:SetAlpha(1 - over / UI.WHEEL_FADE)
            end
        end
    end
end

-- ------------------------------------------------------------------ driver (the frame clock)
-- The whole frame: the network clock (BG.Tick), the fight playback and the window refresh. The driver calls only this.
function UI.Tick(dt)
    dt = dt or 0
    BG.Tick(dt)
    V.Tick(dt)
    if UI.endGuard then
        UI.endGuard = UI.endGuard - dt
        if UI.endGuard <= 0 then
            UI.endGuard = nil
        end
    end
    -- The fight is played only once the window exists (Refresh built it); until then it stays in HsBg.combat. It starts
    -- before the next state is drawn: the server sends the round's next state right behind the fight, and the window
    -- (the lobby's health, my hero) must keep showing the board as it was until the fight is over (UI.OnCombatDone).
    if BG.combat and UI.frame and UI.frame:IsShown() and HsBgCombatUI and BG.state ~= "none" and BG.state ~= "loading" then
        HsBgCombatUI.Play(BG.TakeCombat())
    end
    if HsBgCombatUI then
        HsBgCombatUI.Tick(dt)
    end
    UI.WheelTick(dt)
    if UI.drag then
        -- OnDragStop never comes if the dragged tile was hidden or re-laid out under the cursor (a refresh mid-drag);
        -- a drag left set blocks every hover (OnTileEnter / HandPop return early), so the released button ends it
        if IsMouseButtonDown and not IsMouseButtonDown("LeftButton") then
            UI.CancelDrag()
        else
            UI.UpdateGhost()
        end
    end
    if UI.frame and UI.frame:IsShown() then
        UI.PlaceBoard(dt)
    end
    UI.UpdateTargeting()
    UI.CastQueueTick()
    UI.HoverCardTick()
    UI.ToastTick()
    if BG.dirty and not (HsBgCombatUI and HsBgCombatUI.IsPlaying() and BG.state ~= "none" and BG.state ~= "loading") then
        BG.dirty = nil
        UI.Refresh()
    end
end

if CreateFrame then
    UI.driver = CreateFrame("Frame", "HearthstoneBgDriver", UIParent)
    -- No events: the restore after a /reload is HsBg.Restore, called from H.OnWelcome once the handshake is done (the
    -- server also drops the game on a map change, so a zone change has nothing to restore).
    UI.driver:SetScript("OnUpdate", function(s, dt)
        UI.Tick(dt or arg1)
    end)
end
