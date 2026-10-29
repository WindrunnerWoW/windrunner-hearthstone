-- Hearthstone addon: Warsong Battlegrounds visuals shared by the shop and the fight (UI polish).
--
-- The pieces BgShopUI.lua and BgCombatUI.lua both draw, made like the match window's (MatchUI.lua):
--   hero    the golden arch of the matches (ArenaUI's ArenaArch) with the hero's picture in strips across its opening, the
--           health gem at the opening's bottom right and the steel armor shield above it, an arch glow; without the arch
--           art the square portrait in its frame
--   gem     a number on a gem piece (gem_attack / gem_health / gem_armor), a plate without the piece
--   glow    the white soft-glow pieces (glow_oval / glow_round / glow_arch) tinted and ADD blended around a frame
--   hover   an additive copy of a button's picture and / or a glow, shown while the cursor is on it
--   card    the large card of a hovered minion (Textures\BgMinions\Cards, HS_BgTextures.cards) beside it, with its
--           current numbers under it when they differ from the printed ones; GameTooltip without the card art
--   fx      timed items over the window (window coordinates: x right, y down from its top left): floating numbers,
--           flashes, pulses, shakes and moves of "actors" (frames with a home position)
-- Lua 5.0 / WoW 1.12: handlers get no arguments and use this / arg1.

Hearthstone = Hearthstone or {}
local H = Hearthstone
HsBgVis = HsBgVis or {}
local V = HsBgVis

V.WHITE = "Interface\\Buttons\\WHITE8X8"
V.EDGE = "Interface\\Tooltips\\UI-Tooltip-Border"
V.NUMBER_FONT = "Fonts\\FRIZQT__.TTF"
V.CARD_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\BgMinions\\Cards\\"
V.HERO_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\BgHeroes\\"
V.CARD_W, V.CARD_H = 256, 388           -- the card part of the 256x512 card texture
V.CARD_SCALE = 0.72                     -- the hover card is shown at this share of its 256x388
V.HP_GEM, V.HP_FONT = 38, 20            -- the hero's health gem (MatchUI HERO_HP_GEM / HERO_HP_FONT)
V.ATK_GEM, V.ATK_FONT = 44, 22             -- the hero's attack gem (the damage it would deal in a combat)
V.ARMOR_W, V.ARMOR_H, V.ARMOR_FONT = 43, 50, 23   -- the steel armor shield (MatchUI HERO_ARMOR_*)
V.MAX_FX = 64

V.GLOW_COLORS = { green = { 0.3, 1, 0.3 }, red = { 1, 0.25, 0.25 }, gold = { 1, 0.82, 0.25 },
    blue = { 0.35, 0.65, 1 }, white = { 1, 1, 1 }, ice = { 0.55, 0.85, 1 } }
local GEM_FALLBACK = { attack = { 0.75, 0.6, 0.15 }, health = { 0.7, 0.1, 0.1 }, armor = { 0.5, 0.52, 0.58 },
    mana = { 0.15, 0.35, 0.8 } }

V.items = {}                            -- running fx items
V.speed = 1                             -- the fx clock runs this many times faster (a long fight is sped up)
V.pool = nil                            -- { tex = { {obj, used} }, text = { {obj, used} } } on V.stage
V.stage = nil                           -- the frame the fx coordinates are relative to (the shop window)
V.layer = nil                           -- the frame the pooled regions sit on (above the board)

local function easeOut(u)
    return 1 - (1 - u) * (1 - u)
end
V.EaseOut = easeOut

function V.EaseInOut(u)
    if u < 0.5 then
        return 2 * u * u
    end
    return 1 - 2 * (1 - u) * (1 - u)
end

-- ------------------------------------------------------------------ pieces
-- A board piece (MatchUI.Piece: arch_bottom / arch_top answer with the arena's golden arch), or nil.
function V.Piece(name)
    local MU = H.MatchUI
    if MU and MU.Piece then
        return MU.Piece(name)
    end
    local t = HS_BoardTextures
    local p = type(t) == "table" and type(t.pieces) == "table" and t.pieces[name]
    if type(p) == "table" and type(p.path) == "string" and p.path ~= "" then
        return p
    end
    return nil
end

-- Puts a piece into a texture; false (texture untouched) when it is not installed.
function V.SetPiece(tex, name)
    local p = V.Piece(name)
    if not p then
        return false
    end
    tex:SetTexture(p.path)
    tex:SetTexCoord(0, type(p.right) == "number" and p.right or 1, 0, type(p.bottom) == "number" and p.bottom or 1)
    tex:SetVertexColor(1, 1, 1)
    return true
end

local function outline(parent, size)
    local t = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    t:SetFont(V.NUMBER_FONT, size, "OUTLINE")
    t:SetJustifyH("CENTER")
    return t
end
V.Outline = outline

-- ------------------------------------------------------------------ gems
-- A number gem of w x h. kind: attack, health, armor or mana.
function V.MakeGem(parent, kind, w, h, size)
    local f = CreateFrame("Frame", nil, parent)
    f:SetWidth(w)
    f:SetHeight(h)
    f:SetFrameLevel(parent:GetFrameLevel() + 2)
    local g = { frame = f, kind = kind }
    g.tex = f:CreateTexture(nil, "ARTWORK")
    g.tex:SetAllPoints(f)
    if not V.SetPiece(g.tex, "gem_" .. kind) then
        g.tex:Hide()
        local c = GEM_FALLBACK[kind] or GEM_FALLBACK.mana
        f:SetBackdrop({ bgFile = V.WHITE, edgeFile = V.EDGE, edgeSize = 10,
            insets = { left = 2, right = 2, top = 2, bottom = 2 } })
        f:SetBackdropColor(c[1], c[2], c[3], 1)
        f:SetBackdropBorderColor(0.85, 0.7, 0.35, 1)
    end
    g.text = outline(f, size or math.floor(h * 0.55))
    g.text:SetPoint("CENTER", f, "CENTER", 0, -1)
    f:Hide()
    return g
end

-- Shows value on the gem: tint "up" (green), "down" (red) or nil (white); nil hides it.
function V.SetGem(g, value, tint)
    if value == nil then
        g.frame:Hide()
        return
    end
    g.text:SetText(tostring(value))
    if tint == "up" then
        g.text:SetTextColor(0.35, 1, 0.35)
    elseif tint == "down" then
        g.text:SetTextColor(1, 0.35, 0.35)
    else
        g.text:SetTextColor(1, 1, 1)
    end
    g.frame:Show()
end

-- ------------------------------------------------------------------ glows
-- A glow behind parent: a texture in the parent's BACKGROUND layer, so the parent's own art (in the higher layers)
-- covers its middle and only the halo around the outline shows. name: a glow piece ("oval" means "glow_oval"); the
-- silhouette glows of tools/art/slice_board.py (glow_bg_refresh, glow_end_turn, glow_minion_oval, glow_power_ring,
-- glow_taunt) follow their piece's own outline, the procedural ones (glow_oval, glow_round, glow_arch) a plain shape.
-- The piece's "inner" anchor is laid onto the rectangle (l, t, w, h) from the parent's top left (l, t default 0),
-- grown by pad pixels on every side. Without the piece (or any glow piece) a coloured tooltip border frame.
function V.MakeGlow(parent, name, w, h, pad, l, t)
    pad = pad or 0
    l, t = l or 0, t or 0
    if not string.find(name, "^glow_") then
        name = "glow_" .. name
    end
    local p = V.Piece(name)
    if not p then
        name = "glow_oval"
        p = V.Piece(name)
    end
    local g = { parent = parent, name = name }
    if p then
        local tex = parent:CreateTexture(nil, "BACKGROUND")
        V.SetPiece(tex, name)
        tex:SetBlendMode("ADD")
        local inner = type(p.anchors) == "table" and p.anchors.inner or nil
        if type(inner) ~= "table" then
            inner = { left = 0.1, top = 0.1, right = 0.9, bottom = 0.9 }
        end
        local gw = (w + 2 * pad) / (inner.right - inner.left)
        local gh = (h + 2 * pad) / (inner.bottom - inner.top)
        tex:SetPoint("TOPLEFT", parent, "TOPLEFT", l - pad - inner.left * gw, -(t - pad - inner.top * gh))
        tex:SetWidth(gw)
        tex:SetHeight(gh)
        tex:Hide()
        g.tex, g.frame = tex, tex
        return g
    end
    local f = CreateFrame("Frame", nil, parent)
    f:SetFrameLevel(parent:GetFrameLevel() + 1)
    f:EnableMouse(false)
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", l - pad - 4, -(t - pad - 4))
    f:SetWidth(w + 2 * pad + 8)
    f:SetHeight(h + 2 * pad + 8)
    f:SetBackdrop({ edgeFile = V.EDGE, edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
    f:Hide()
    g.frame = f
    return g
end

-- Shows the glow in a colour of V.GLOW_COLORS (nil hides it), alpha 0..1 (default 1).
function V.SetGlow(g, color, alpha)
    if not g then
        return
    end
    if not color then
        g.frame:Hide()
        g.color = nil
        return
    end
    local c = V.GLOW_COLORS[color] or V.GLOW_COLORS.gold
    if g.tex then
        g.tex:SetVertexColor(c[1], c[2], c[3])
        g.tex:SetAlpha(alpha or 1)
    else
        g.frame:SetBackdropBorderColor(c[1], c[2], c[3], alpha or 1)
    end
    g.color = color
    g.frame:Show()
end

-- ------------------------------------------------------------------ hover
-- An additive copy of the button's picture `src` (a texture region with piece `name`) over it, shown on hover.
function V.AddHighlight(b, src, name, alpha)
    local t = b:CreateTexture(nil, "OVERLAY")
    t:SetAllPoints(src)
    if not V.SetPiece(t, name) then
        t:SetTexture(V.WHITE)
    end
    t:SetBlendMode("ADD")
    t:SetAlpha(alpha or 0.35)
    t:Hide()
    b.hl = t
    return t
end

-- Gives b a hover look: its highlight (b.hl) and / or hover glow (b.hoverGlow, colour b.hoverColor) show while the
-- cursor is on it, and the picture (b.tex) sinks a pixel while the button is held down. enter / leave (optional) still
-- run with the button (tooltips, previews).
function V.Hover(b, enter, leave)
    b:SetScript("OnEnter", function()
        V.HoverOn(this)
        if enter then
            enter(this)
        end
    end)
    b:SetScript("OnLeave", function()
        V.HoverOff(this)
        if leave then
            leave(this)
        end
    end)
    b:SetScript("OnMouseDown", function()
        if this.tex and this.texHome then
            this.tex:ClearAllPoints()
            this.tex:SetPoint("TOPLEFT", this, "TOPLEFT", 1, -1)
            this.tex:SetPoint("BOTTOMRIGHT", this, "BOTTOMRIGHT", 1, -1)
        end
    end)
    b:SetScript("OnMouseUp", function()
        if this.tex and this.texHome then
            this.tex:ClearAllPoints()
            this.tex:SetAllPoints(this)
        end
    end)
end

function V.HoverOn(b)
    b.hovered = true
    if b.hl and not b.noHover then
        b.hl:Show()
    end
    if b.hoverGlow and not b.noHover then
        V.SetGlow(b.hoverGlow, b.hoverColor or "gold")
    end
end

function V.HoverOff(b)
    b.hovered = nil
    if b.hl then
        b.hl:Hide()
    end
    if b.hoverGlow then
        if b.restGlow then
            V.SetGlow(b.hoverGlow, b.restGlow)
        else
            V.SetGlow(b.hoverGlow, nil)
        end
    end
end

-- The glow a button keeps while the cursor is not on it (nil: none); the hover look still wins while hovered.
function V.SetRestGlow(b, color)
    b.restGlow = color
    if b.hoverGlow and not b.hovered then
        V.SetGlow(b.hoverGlow, color)
    end
end

-- ------------------------------------------------------------------ the hero arch
local function archArt()
    local AU = H.ArenaUI
    local a = AU and AU.ART and AU.ART.ArenaArch
    if a and AU.TEXTURE_PATH then
        return a, AU.TEXTURE_PATH
    end
    return nil
end

local function heroInstalled(id)
    return HsBgLobbyUI and HsBgLobbyUI.HeroInstalled and HsBgLobbyUI.HeroInstalled(id) or false
end

-- A hero of w pixels width (the height follows the arch): a Button (h.frame) holding the portrait strips, the golden
-- frame, the gems on a layer above it and an arch glow. Place it with h.frame:SetPoint; h.open is the portrait's box
-- (left, top, right, bottom from the frame's top left).
function V.MakeHero(parent, name, w)
    local arch, path = archArt()
    local hh = arch and math.floor(w / arch.aspect + 0.5) or w
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(w)
    b:SetHeight(hh)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local h = { frame = b, w = w, h = hh, strips = {} }
    if arch then
        local o = arch.opening
        h.open = { l = w * o[1], t = hh * o[2], r = w * o[3], b = hh * o[4] }
        local aw, ah = h.open.r - h.open.l, h.open.b - h.open.t
        local s = arch.strips
        local bb = { s[1][1], s[1][2], s[1][3], s[1][4] }
        for k = 2, table.getn(s) do
            local st = s[k]
            if st[1] < bb[1] then bb[1] = st[1] end
            if st[2] < bb[2] then bb[2] = st[2] end
            if st[3] > bb[3] then bb[3] = st[3] end
            if st[4] > bb[4] then bb[4] = st[4] end
        end
        h.bounds = bb
        h.arch = arch
        for k = 1, table.getn(s) do
            local st = s[k]
            local tex = b:CreateTexture(nil, "BORDER")
            tex:SetTexture(V.WHITE)
            tex:SetPoint("TOPLEFT", b, "TOPLEFT", h.open.l + aw * st[1], -(h.open.t + ah * st[2]))
            tex:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", h.open.l + aw * st[3], -(h.open.t + ah * st[4]))
            tex:Hide()
            h.strips[k] = tex
        end
        h.archTex = b:CreateTexture(nil, "ARTWORK")
        h.archTex:SetTexture(path .. "ArenaArch")
        h.archTex:SetTexCoord(0, arch.right, 0, arch.bottom)
        h.archTex:SetAllPoints(b)
    else
        h.open = { l = 4, t = 4, r = w - 4, b = hh - 4 }
        h.ring = b:CreateTexture(nil, "ARTWORK")
        h.ring:SetTexture("Interface\\AddOns\\Hearthstone\\Textures\\Bg\\hero_frame")
        h.ring:SetVertexColor(1, 0.82, 0.3)
        h.ring:SetAllPoints(b)
    end
    h.square = b:CreateTexture(nil, "BORDER")      -- the square portrait: no arch art, or no _arch picture of the hero
    h.square:SetPoint("TOPLEFT", b, "TOPLEFT", h.open.l, -h.open.t)
    h.square:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", h.open.r, -h.open.b)
    h.square:Hide()
    -- a red flash over the portrait when it takes damage (fx), kept hidden
    h.flash = b:CreateTexture(nil, "OVERLAY")
    h.flash:SetTexture(V.WHITE)
    h.flash:SetBlendMode("ADD")
    h.flash:SetPoint("TOPLEFT", b, "TOPLEFT", h.open.l, -h.open.t)
    h.flash:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", h.open.r, -h.open.b)
    h.flash:Hide()
    h.glow = V.MakeGlow(b, "arch", w, hh, 2)
    h.top = CreateFrame("Frame", nil, b)            -- the gems stand above the glow
    h.top:SetAllPoints(b)
    h.top:SetFrameLevel(b:GetFrameLevel() + 4)
    h.hp = V.MakeGem(h.top, "health", V.HP_GEM, V.HP_GEM, V.HP_FONT)
    h.hp.frame:SetPoint("CENTER", b, "TOPLEFT", h.open.r - 2, -(h.open.b - 8))
    h.armor = V.MakeGem(h.top, "armor", V.ARMOR_W, V.ARMOR_H, V.ARMOR_FONT)
    h.armor.frame:SetFrameLevel(h.hp.frame:GetFrameLevel() + 2)
    h.armor.frame:SetPoint("CENTER", b, "TOPLEFT", h.open.r - 2, -(h.open.b - 43))
    -- the damage the hero would deal in a combat (tavern tier + the tiers of its surviving minions): the sword gem on the
    -- hero's lower left, as in the game; hidden outside a combat
    h.atk = V.MakeGem(h.top, "attack", V.ATK_GEM, V.ATK_GEM, V.ATK_FONT)
    h.atk.frame:SetPoint("CENTER", b, "TOPLEFT", h.open.l + 4, -(h.open.b - 6))
    h.atk.text:ClearAllPoints()
    h.atk.text:SetPoint("CENTER", h.atk.frame, "CENTER", V.ATK_GEM * 0.04, -V.ATK_GEM * 0.11)   -- the sword sticks out of the gem
    b.subs = { h.top, h.hp.frame, h.armor.frame, h.atk.frame }   -- they rise with the hero (V.Raise)
    h.name = h.top:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    h.name:SetPoint("CENTER", b, "TOPLEFT", (h.open.l + h.open.r) / 2, -(h.open.t + h.open.b) / 2)
    h.name:SetWidth(h.open.r - h.open.l - 4)
    h.name:SetJustifyH("CENTER")
    return h
end

-- Shows a hero: heroId ("" = unknown), its health and armor (nil hides a gem), maxHealth (a hurt hero's number is
-- red), dim (a dead hero is faded), atk (the damage it would deal, a green number; nil hides the gem).
function V.SetHero(h, heroId, health, armor, maxHealth, dim, atk)
    heroId = heroId or ""
    h.heroId = heroId
    for k = 1, table.getn(h.strips) do
        h.strips[k]:Hide()
    end
    h.square:Hide()
    h.name:SetText("")
    -- the picture across the arch's strips: the _arch cut made for it, else the square portrait (never a rectangle
    -- over the arch, its corners would stand out above the round top)
    local pic = nil
    if heroId ~= "" and heroInstalled(heroId .. "_arch") then
        pic = heroId .. "_arch"
    elseif heroId ~= "" and heroInstalled(heroId) then
        pic = heroId
    end
    h.pic = pic
    if pic and h.arch then
        local b = h.bounds
        local s = h.arch.strips
        for k = 1, table.getn(h.strips) do
            local st = s[k]
            local tex = h.strips[k]
            tex:SetTexture(V.HERO_PATH .. pic)
            tex:SetTexCoord((st[1] - b[1]) / (b[3] - b[1]), (st[3] - b[1]) / (b[3] - b[1]),
                (st[2] - b[2]) / (b[4] - b[2]), (st[4] - b[2]) / (b[4] - b[2]))
            tex:SetVertexColor(1, 1, 1)
            tex:Show()
        end
    elseif pic then
        h.square:SetTexture(V.HERO_PATH .. pic)
        h.square:SetVertexColor(1, 1, 1)
        h.square:Show()
    elseif heroId ~= "" then
        h.name:SetText("|cffffd100" .. (heroId == "bob" and "Bob" or (HsBg and HsBg.HeroName and HsBg.HeroName(heroId)
            or heroId)) .. "|r")
    end
    if health then
        V.SetGem(h.hp, health, (maxHealth and health < maxHealth) and "down" or nil)
    else
        V.SetGem(h.hp, nil)
    end
    if armor and armor > 0 then
        V.SetGem(h.armor, armor)
    else
        V.SetGem(h.armor, nil)
    end
    V.SetGem(h.atk, atk, "up")
    h.frame:SetAlpha(dim and 0.5 or 1)
end

-- ------------------------------------------------------------------ the targeting arrow
-- The dotted arc and arrowhead of the match window's spell targeting (MatchUI.DragArrow), drawn over everything in UIParent
-- coordinates (y up): from (ax, ay) to the cursor (x, y); the head is green, red when `invalid` (over a wrong target).
local arrow = nil
local function arrowPool()
    if arrow then
        return arrow
    end
    local f = CreateFrame("Frame", "HearthstoneBgArrow", UIParent)
    f:SetFrameStrata("TOOLTIP")
    f:SetWidth(1)
    f:SetHeight(1)
    f:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", 0, 0)
    f:EnableMouse(false)
    arrow = { frame = f, dots = {} }
    for i = 1, 24 do
        local dot = f:CreateTexture(nil, "OVERLAY")
        dot:SetTexture(V.WHITE)
        dot:SetVertexColor(1, 0.8, 0.2)
        dot:SetWidth(4)
        dot:SetHeight(4)
        dot:Hide()
        arrow.dots[i] = dot
    end
    arrow.head = f:CreateTexture(nil, "OVERLAY")
    arrow.head:SetTexture("Interface\\Minimap\\MinimapArrow")
    arrow.head:SetWidth(24)
    arrow.head:SetHeight(24)
    arrow.head:Hide()
    return arrow
end

function V.ArrowHide()
    if not arrow then
        return
    end
    for i = 1, table.getn(arrow.dots) do
        arrow.dots[i]:Hide()
    end
    arrow.head:Hide()
end

function V.ArrowShow(ax, ay, x, y, invalid)
    local pool = arrowPool()
    local dx, dy = x - ax, -(y - ay)                  -- y down, like the match board
    local length = math.sqrt(dx * dx + dy * dy)
    local count = math.min(24, math.max(16, math.floor(length / 18) + 1))
    local bow = math.min(38, length * 0.12)
    for i = 1, 24 do
        local dot = pool.dots[i]
        if i <= count then
            local t = (i - 1) / count
            dot:ClearAllPoints()
            dot:SetPoint("CENTER", UIParent, "BOTTOMLEFT", ax + dx * t, ay - (dy * t - 4 * bow * t * (1 - t)))
            dot:Show()
        else
            dot:Hide()
        end
    end
    local head = pool.head
    head:ClearAllPoints()
    head:SetPoint("CENTER", UIParent, "BOTTOMLEFT", x, y)
    -- MinimapArrow points up: rotate the texture coordinates towards the end of the arc (corners UL, LL, UR, LR; 1.12)
    local tx, ty = dx, dy + 4 * bow
    local len = math.sqrt(tx * tx + ty * ty)
    local c, s = 1, 0
    if len > 0 then
        c, s = -ty / len, tx / len
    end
    local function uv(u, v)
        return 0.5 + c * u + s * v, 0.5 - s * u + c * v
    end
    local u1, v1 = uv(-0.5, -0.5)
    local u2, v2 = uv(-0.5, 0.5)
    local u3, v3 = uv(0.5, -0.5)
    local u4, v4 = uv(0.5, 0.5)
    head:SetTexCoord(u1, v1, u2, v2, u3, v3, u4, v4)
    if invalid then
        head:SetVertexColor(1, 0.15, 0.15)
    else
        head:SetVertexColor(0.3, 1, 0.3)
    end
    head:Show()
end

-- ------------------------------------------------------------------ the large card
local function cardInstalled(id)
    local t = HS_BgTextures and HS_BgTextures.cards
    return id ~= nil and type(t) == "table" and t[id] == true
end
V.CardInstalled = cardInstalled

-- The card texture id for a minion entry: its golden twin's card when it is golden and that is installed.
function V.CardArtId(entry)
    if not entry then
        return nil
    end
    local card = HsBg and HsBg.Card and HsBg.Card(entry.id)
    if entry.golden and card and card.golden and cardInstalled(card.golden) then
        return card.golden
    end
    if cardInstalled(entry.id) then
        return entry.id
    end
    return nil
end

local function cardFrame()
    if V.card then
        return V.card
    end
    local f = CreateFrame("Frame", "HearthstoneBgCard", UIParent)
    f:SetWidth(V.CARD_W)
    f:SetHeight(V.CARD_H + 40)
    f:SetFrameStrata("TOOLTIP")
    f:SetClampedToScreen(true)
    f:EnableMouse(false)
    f.tex = f:CreateTexture(nil, "ARTWORK")
    f.tex:SetWidth(V.CARD_W)
    f.tex:SetHeight(V.CARD_H)
    f.tex:SetPoint("TOP", f, "TOP", 0, 0)
    f.tex:SetTexCoord(0, 1, 0, V.CARD_H / 512)
    -- the tavern tier over the mana gem the render prints (a Battlegrounds minion costs Gold, its tier is what matters)
    f.tier = f:CreateTexture(nil, "OVERLAY")
    f.tier:Hide()
    -- the minion's current attack / health over the gems the render prints (patch_renders.GEM_BOXES centers)
    f.gemA = f:CreateTexture(nil, "OVERLAY")
    f.gemH = f:CreateTexture(nil, "OVERLAY")
    V.SetPiece(f.gemA, "gem_attack")
    V.SetPiece(f.gemH, "gem_health")
    f.atk = f:CreateFontString(nil, "OVERLAY")
    f.hp = f:CreateFontString(nil, "OVERLAY")
    local gem = V.CARD_W * 0.24
    local spots = { { f.gemA, f.atk, 44.5, 319 }, { f.gemH, f.hp, 212.5, 320.5 } }
    for i = 1, 2 do
        local g, fs, x, y = spots[i][1], spots[i][2], spots[i][3], spots[i][4]
        g:SetWidth(gem)
        g:SetHeight(gem)
        -- the attack piece's circle sits below-right of its texture center (the sword sticks out), so shift the texture
        local dx, dy = 0, 0
        if i == 1 then
            dx, dy = -gem * 0.04, gem * 0.11
        end
        g:SetPoint("CENTER", f, "TOPLEFT", x + dx, -y + dy)
        g:Hide()
        fs:SetFont(V.NUMBER_FONT, math.floor(V.CARD_W * 0.15), "OUTLINE")
        fs:SetPoint("CENTER", g, "CENTER", -dx, -dy - 1)
        -- wider than the gem, so a two-digit number stays on one line instead of wrapping at the gem's width
        fs:SetWidth(V.CARD_W * 0.6)
        if fs.SetNonSpaceWrap then
            fs:SetNonSpaceWrap(false)
        end
        fs:SetJustifyH("CENTER")
    end
    -- the minion's numbers now (and its keywords) under the card when they differ from the printed ones
    f.plate = CreateFrame("Frame", nil, f)
    f.plate:SetPoint("TOP", f, "TOP", 0, -(V.CARD_H - 30))
    f.plate:SetWidth(V.CARD_W - 40)
    f.plate:SetHeight(34)
    f.plate:SetBackdrop({ bgFile = V.WHITE, edgeFile = V.EDGE, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f.plate:SetBackdropColor(0, 0, 0, 0.85)
    f.plate:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    f.line = f.plate:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    f.line:SetPoint("CENTER", f.plate, "CENTER", 0, 0)
    f.line:SetWidth(V.CARD_W - 50)
    f.line:SetJustifyH("CENTER")
    -- the allies the card would help, as portraits with a reason mark (a second plate under the first)
    f.needs = CreateFrame("Frame", nil, f)
    f.needs:SetPoint("TOP", f.plate, "BOTTOM", 0, -4)
    f.needs:SetWidth(V.CARD_W - 40)
    f.needs:SetHeight(76)
    f.needs:SetBackdrop({ bgFile = V.WHITE, edgeFile = V.EDGE, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f.needs:SetBackdropColor(0, 0, 0, 0.85)
    f.needs:SetBackdropBorderColor(0.3, 0.65, 0.4, 1)
    f.np = {}
    local lobbyPath = (HsBgLobbyUI and HsBgLobbyUI.BG_PATH) or "Interface\\AddOns\\Hearthstone\\Textures\\Bg\\"
    local heroPath = (HsBgLobbyUI and HsBgLobbyUI.HERO_PATH) or "Interface\\AddOns\\Hearthstone\\Textures\\BgHeroes\\"
    for i = 1, 3 do
        local p = CreateFrame("Frame", nil, f.needs)
        p:SetWidth(62)
        p:SetHeight(66)
        p:SetPoint("LEFT", f.needs, "LEFT", 15 + (i - 1) * 62, 0)
        p.portrait = p:CreateTexture(nil, "ARTWORK")
        p.portrait:SetWidth(58)
        p.portrait:SetHeight(58)
        p.portrait:SetPoint("CENTER", p, "CENTER", 0, 4)
        p.frameTex = p:CreateTexture(nil, "OVERLAY")
        p.frameTex:SetTexture(lobbyPath .. "hero_frame")
        p.frameTex:SetWidth(66)
        p.frameTex:SetHeight(66)
        p.frameTex:SetPoint("CENTER", p.portrait, "CENTER", 0, 0)
        p.mark = p:CreateFontString(nil, "OVERLAY")
        p.mark:SetFont("Fonts\\FRIZQT__.TTF", 11, "OUTLINE")
        p.mark:SetPoint("BOTTOM", p, "BOTTOM", 0, -4)
        p:Hide()
        f.np[i] = p
    end
    f.needs:Hide()
    f.heroPath = heroPath
    f:Hide()
    V.card = f
    return f
end

-- The line under the card: the keywords an effect granted (the numbers show on the card's gems), else nil.
function V.CardLine(entry)
    local BG = HsBg
    local card = BG and BG.Card and BG.Card(entry.id)
    if card and entry.golden and card.golden then
        card = BG.Card(card.golden) or card
    end
    if card and card.spell == 1 then
        return nil                     -- a spell has no stats to compare
    end
    local parts = {}
    local names = BG and BG.PlainKeywords and BG.PlainKeywords(entry.kw or 0) or {}
    -- only the keywords the card does not print itself (the ones an effect granted)
    local printed = {}
    local own = card and card.kw
    if type(own) == "table" then
        for i = 1, table.getn(own) do
            printed[own[i] == "MEGA_WINDFURY" and "Mega-Windfury" or BG.KeywordLabel(own[i])] = true
        end
    end
    local granted = {}
    for i = 1, table.getn(names) do
        if not printed[names[i]] then
            table.insert(granted, names[i])
        end
    end
    if table.getn(granted) > 0 then
        table.insert(parts, "|cffffd100" .. table.concat(granted, ", ") .. "|r")
    end
    if table.getn(parts) == 0 then
        return nil
    end
    return table.concat(parts, "  ")
end

-- The second plate: up to three ally portraits, each with a small reason mark (T gold, R blue, U green).
function V.ShowNeeds(needs)
    local f = cardFrame()
    if not needs or table.getn(needs) == 0 then
        f.needs:Hide()
        return false
    end
    for i = 1, 3 do
        local p = f.np[i]
        local n = needs[i]
        if n then
            local hero = HsBg and HsBg.SeatHero and HsBg.SeatHero(n.seat) or ""
            if hero ~= "" and HsBgLobbyUI and HsBgLobbyUI.HeroInstalled and HsBgLobbyUI.HeroInstalled(hero) then
                p.portrait:SetTexture(f.heroPath .. hero)
                p.portrait:SetVertexColor(1, 1, 1)
            else
                p.portrait:SetTexture("Interface\\Buttons\\WHITE8X8")
                p.portrait:SetVertexColor(0.22, 0.28, 0.40)
            end
            local reason = n.reason or ""
            -- a triple completes on this send: the gold ring; otherwise the ally faction's blue frame
            if string.find(reason, "T", 1, true) then
                p.frameTex:SetVertexColor(1, 0.82, 0.3)
            else
                p.frameTex:SetVertexColor(0.35, 0.62, 1)
            end
            p.mark:SetText(
                (string.find(reason, "T", 1, true) and "|cffffd100T|r" or "") ..
                (string.find(reason, "R", 1, true) and "|cff66ccffR|r" or "") ..
                (string.find(reason, "U", 1, true) and "|cff4cff4cU|r" or ""))
            p:Show()
        else
            p:Hide()
        end
    end
    f.needs:Show()
    return true
end

-- Shows a card texture (256x512, the card in its top 388 rows) beside `anchor` (on the side towards the screen's
-- middle), scaled with `scaleFrame`, with `line` (optional) on a plate under it, `tier` (optional) as a tier shield
-- over the mana gem and `needs` (optional) as the ally-need portraits under that.
function V.ShowTexCard(path, anchor, scaleFrame, line, tier, needs, stats)
    if not path or not anchor then
        return false
    end
    local f = cardFrame()
    f.tex:SetTexture(path)
    f.tier:Hide()
    -- stats = { atk, hp, baseAtk, baseHp }: green above the printed number, red below it
    local function number(g, fs, now, base)
        if stats and now then
            local c = (base and now > base) and "|cff4cff4c" or ((base and now < base) and "|cffff5555" or "|cffffffff")
            g:Show()
            fs:SetText(c .. now .. "|r")
        else
            g:Hide()
            fs:SetText("")
        end
    end
    number(f.gemA, f.atk, stats and stats.atk, stats and stats.baseAtk)
    number(f.gemH, f.hp, stats and stats.hp, stats and stats.baseHp)
    if tier and tier > 0 and V.Piece("bg_tier_" .. tier) and V.SetPiece(f.tier, "bg_tier_" .. tier) then
        local tp = V.Piece("bg_tier_" .. tier)
        local w = V.CARD_W * 0.25
        f.tier:SetWidth(w)
        f.tier:SetHeight(w * (tp.h or 1) / (tp.w or 1))
        f.tier:ClearAllPoints()
        f.tier:SetPoint("CENTER", f, "TOPLEFT", V.CARD_W * 0.14 + 5, -V.CARD_H * 0.17)
        f.tier:Show()
    end
    if line then
        f.line:SetText(line)
        f.plate:Show()
    else
        f.plate:Hide()
    end
    f.tex:Show()
    f.needs:ClearAllPoints()
    f.needs:SetPoint("TOP", f.plate, "BOTTOM", 0, -4)
    V.ShowNeeds(needs)
    f:SetScale((scaleFrame and scaleFrame:GetScale() or 1) * V.CARD_SCALE)
    f:ClearAllPoints()
    local x = anchor.GetCenter and anchor:GetCenter()
    local mid = UIParent and UIParent.GetCenter and UIParent:GetCenter()
    if x and mid and x > mid then
        f:SetPoint("RIGHT", anchor, "LEFT", -8, 0)
    else
        f:SetPoint("LEFT", anchor, "RIGHT", 8, 0)
    end
    f:Show()
    return true
end

-- The large card of a minion entry (its golden card for a golden one) with its current numbers. False when the card
-- art is not installed (the caller shows its tooltip instead).
function V.ShowCard(entry, anchor, scaleFrame, needs)
    local id = V.CardArtId(entry)
    if not id then
        return false
    end
    local card = HsBg and HsBg.Card and HsBg.Card(entry.id)
    local stats = nil
    if card and card.spell ~= 1 and entry.atk and entry.hp then
        local printed = (entry.golden and card.golden and HsBg.Card(card.golden)) or card      -- as V.CardLine
        stats = { atk = entry.atk, hp = entry.hp, baseAtk = printed.atk, baseHp = printed.hp }
    end
    return V.ShowTexCard(V.CARD_PATH .. id, anchor, scaleFrame, V.CardLine(entry), card and card.tier, needs, stats)
end

-- Only the ally plate, above `anchor` (a hand card that already grew: no second card beside it). False without needs.
function V.ShowNeedsOnly(anchor, scaleFrame, needs)
    if not anchor or not needs or table.getn(needs) == 0 then
        return false
    end
    local f = cardFrame()
    f.tex:Hide()
    f.tier:Hide()
    f.gemA:Hide()
    f.gemH:Hide()
    f.atk:SetText("")
    f.hp:SetText("")
    f.plate:Hide()
    V.ShowNeeds(needs)
    f:SetScale((scaleFrame and scaleFrame:GetScale() or 1) * V.CARD_SCALE)
    f:ClearAllPoints()
    f:SetPoint("BOTTOM", anchor, "TOP", 0, 0)
    f.needs:ClearAllPoints()
    f.needs:SetPoint("BOTTOM", anchor, "TOP", 0, 6)
    f:Show()
    return true
end

function V.HideCard()
    if V.card then
        V.card:Hide()
    end
end

function V.CardShown()
    return V.card ~= nil and V.card:IsShown()
end

-- ------------------------------------------------------------------ fx
-- The fx layer over the stage (the shop window): pooled ADD textures and outlined numbers. Built once.
function V.Init(stage)
    if V.stage == stage and V.layer then
        return
    end
    V.stage = stage
    local f = CreateFrame("Frame", nil, stage)
    f:SetAllPoints(stage)
    f:SetFrameLevel(stage:GetFrameLevel() + 45)
    f:EnableMouse(false)
    V.layer = f
    V.pool = { tex = {}, text = {} }
    for i = 1, 32 do
        local t = f:CreateTexture(nil, "OVERLAY")
        t:SetTexture(V.WHITE)
        t:SetBlendMode("ADD")
        t:Hide()
        V.pool.tex[i] = { obj = t, used = false }
    end
    for i = 1, 20 do
        local s = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        s:SetFont(V.NUMBER_FONT, 24, "OUTLINE")
        s:SetJustifyH("CENTER")
        s:Hide()
        V.pool.text[i] = { obj = s, used = false }
    end
    V.items = {}
end

local function alloc(list)
    if not list then
        return nil
    end
    for i = 1, table.getn(list) do
        if not list[i].used then
            list[i].used = true
            return list[i]
        end
    end
    return nil
end

local function freeTex(h)
    if not h or not h.used then
        return
    end
    h.used = false
    h.obj:Hide()
    h.obj:ClearAllPoints()
    h.obj:SetTexture(V.WHITE)
    h.obj:SetTexCoord(0, 1, 0, 1)
    h.obj:SetVertexColor(1, 1, 1, 1)
    h.obj:SetBlendMode("ADD")
    h.obj:SetAlpha(1)
end

local function freeText(h)
    if not h or not h.used then
        return
    end
    h.used = false
    h.obj:Hide()
    h.obj:ClearAllPoints()
    h.obj:SetText("")
    h.obj:SetAlpha(1)
end

-- Centres a pooled region on (x, y) of the stage, w x h.
local function placeRect(obj, x, y, w, h)
    obj:ClearAllPoints()
    obj:SetWidth(w)
    obj:SetHeight(h)
    obj:SetPoint("CENTER", V.stage, "TOPLEFT", x, -y)
end

-- Runs a timed item: { dur, update(item, u), finish(item) }. finish always runs (also on V.Clear).
function V.Add(item)
    if table.getn(V.items) >= V.MAX_FX then
        if item.finish then
            item.finish(item)
        end
        return nil
    end
    item.t = 0
    table.insert(V.items, item)
    if item.update then
        item.update(item, 0)
    end
    return item
end

function V.Tick(dt)
    dt = (dt or 0) * (V.speed or 1)
    local list = V.items
    for i = table.getn(list), 1, -1 do
        local it = list[i]
        if it then
            it.t = it.t + dt
            if it.t >= it.dur then
                table.remove(list, i)
                if it.finish then
                    it.finish(it)
                end
            elseif it.update then
                it.update(it, it.t / it.dur)
            end
        end
    end
end

-- Ends every running item now.
function V.Clear()
    local list = V.items
    V.items = {}
    for i = table.getn(list), 1, -1 do
        if list[i].finish then
            list[i].finish(list[i])
        end
    end
end

function V.Busy()
    return table.getn(V.items) > 0
end

-- A number that rises and fades over (x, y): damage red, healing / buffs green, gold yellow.
function V.Float(x, y, text, r, g, b, dur, size)
    local h = alloc(V.pool and V.pool.text)
    if not h then
        return nil
    end
    h.obj:SetFont(V.NUMBER_FONT, size or 24, "OUTLINE")
    h.obj:SetText(text)
    h.obj:SetTextColor(r, g, b)
    h.obj:Show()
    return V.Add({ dur = dur or 0.9, h = h, kind = "number",
        update = function(it, u)
            it.h.obj:ClearAllPoints()
            it.h.obj:SetPoint("CENTER", V.stage, "TOPLEFT", x, -(y - 26 * easeOut(u)))
            local a = 1
            if u > 0.55 then
                a = 1 - (u - 0.55) / 0.45
            end
            it.h.obj:SetAlpha(a)
        end,
        finish = function(it)
            freeText(it.h)
        end })
end

-- A flash of colour over a rectangle (centre x, y, w x h) that fades out; tex (optional): a texture path for its shape.
function V.Flash(x, y, w, h, r, g, b, alpha, dur, tex)
    local p = alloc(V.pool and V.pool.tex)
    if not p then
        return nil
    end
    if tex then
        p.obj:SetTexture(tex)
    end
    placeRect(p.obj, x, y, w, h)
    p.obj:SetVertexColor(r, g, b, alpha or 0.6)
    p.obj:Show()
    return V.Add({ dur = dur or 0.28, p = p, kind = "flash",
        update = function(it, u)
            it.p.obj:SetAlpha(math.max(0, 1 - u))
        end,
        finish = function(it)
            freeTex(it.p)
        end })
end

-- A glow that grows and fades over (x, y): the piece `name` (a glow or the star burst of a lost Divine Shield).
function V.Pulse(x, y, w, h, r, g, b, dur, name, grow)
    local p = alloc(V.pool and V.pool.tex)
    if not p then
        return nil
    end
    if name and string.find(name, "\\", 1, true) then
        p.obj:SetTexture(name)
    elseif not (name and V.SetPiece(p.obj, name)) then
        p.obj:SetTexture("Interface\\Cooldown\\star4")
    end
    p.obj:SetBlendMode("ADD")
    p.obj:SetVertexColor(r, g, b, 1)
    p.obj:Show()
    grow = grow or 0.6
    return V.Add({ dur = dur or 0.35, p = p, kind = "pulse",
        update = function(it, u)
            local s = 0.7 + grow * easeOut(u)
            placeRect(it.p.obj, x, y, w * s, h * s)
            it.p.obj:SetAlpha(math.max(0, 1 - u))
        end,
        finish = function(it)
            freeTex(it.p)
        end })
end

-- ------------------------------------------------------------------ actors
-- An actor is a frame that has a home on the stage: { frame, x, y (home top left, window pixels), w, h }, plus the
-- temporary offsets lx / ly (lunge), sx / sy (shake), ay (sink) and alpha. V.Place puts it where it is now.
-- True when the actor's frame now belongs to another actor (a pooled tile handed on): its old items leave it alone.
local function stale(a)
    return a.frame.actor ~= nil and a.frame.actor ~= a
end
V.Stale = stale

function V.Place(a)
    if not a or not a.frame or not V.stage or stale(a) then
        return
    end
    a.frame:ClearAllPoints()
    a.frame:SetPoint("TOPLEFT", V.stage, "TOPLEFT", a.x + (a.lx or 0) + (a.sx or 0),
        -(a.y + (a.ly or 0) + (a.sy or 0) + (a.ay or 0)))
    if a.alpha then
        a.frame:SetAlpha(a.alpha)
    end
end

function V.Center(a)
    return a.x + (a.lx or 0) + a.w / 2, a.y + (a.ly or 0) + a.h / 2
end

-- A short shake of an actor around its home.
function V.Shake(a, px, dur)
    if not a then
        return nil
    end
    px = px or 4
    return V.Add({ dur = dur or 0.24, a = a, kind = "shake",
        update = function(it, u)
            local amp = px * (1 - u)
            it.a.sx = amp * math.sin(u * 30)
            it.a.sy = amp * 0.5 * math.cos(u * 38)
            V.Place(it.a)
        end,
        finish = function(it)
            it.a.sx, it.a.sy = nil, nil
            V.Place(it.a)
        end })
end

-- Moves an actor's home from where it is to (x, y) over dur seconds (the board closing a gap, a new minion's place).
function V.Slide(a, x, y, dur)
    if not a then
        return nil
    end
    if a.slide then
        a.slide.dead = true
    end
    local x0, y0 = a.x, a.y
    if math.abs(x0 - x) < 0.5 and math.abs(y0 - y) < 0.5 then
        a.x, a.y = x, y
        V.Place(a)
        return nil
    end
    local item = { dur = dur or 0.2, a = a, kind = "slide",
        update = function(it, u)
            if it.dead then
                return
            end
            local e = V.EaseInOut(u)
            it.a.x = x0 + (x - x0) * e
            it.a.y = y0 + (y - y0) * e
            V.Place(it.a)
        end,
        finish = function(it)
            if it.dead then
                return
            end
            it.a.x, it.a.y = x, y
            it.a.slide = nil
            V.Place(it.a)
        end }
    a.slide = item
    return V.Add(item)
end

-- Fades an actor in (from 0) or out (to 0) over dur; out also sinks it a few pixels. done(actor) runs at the end.
function V.Fade(a, out, dur, done)
    if not a then
        return nil
    end
    return V.Add({ dur = dur or 0.3, a = a, kind = "fade",
        update = function(it, u)
            if out then
                it.a.alpha = 1 - u
                it.a.ay = 10 * u
            else
                it.a.alpha = u
            end
            V.Place(it.a)
        end,
        finish = function(it)
            if stale(it.a) then
                return
            end
            it.a.alpha = out and 0 or 1
            it.a.ay = nil
            V.Place(it.a)
            if out then
                it.a.frame:Hide()
            end
            it.a.alpha = nil
            it.a.frame:SetAlpha(1)
            if done then
                done(it.a)
            end
        end })
end

-- Lifts a frame and the child frames listed in frame.subs by delta levels (1.12 leaves the children where they were);
-- returns the levels to give back to V.Lower.
function V.Raise(frame, delta)
    local saved = { { frame, frame:GetFrameLevel() } }
    for i = 1, table.getn(frame.subs or {}) do
        table.insert(saved, { frame.subs[i], frame.subs[i]:GetFrameLevel() })
    end
    for i = 1, table.getn(saved) do
        saved[i][1]:SetFrameLevel(saved[i][2] + delta)
    end
    return saved
end

function V.Lower(saved)
    for i = 1, table.getn(saved or {}) do
        saved[i][1]:SetFrameLevel(saved[i][2])
    end
end

-- Plays a sound of the client by name (PlaySound) or by path (PlaySoundFile); silent where neither exists.
function V.Sound(name)
    if not name then
        return
    end
    if string.find(name, "\\", 1, true) then
        if PlaySoundFile then
            PlaySoundFile(name)
        end
    elseif PlaySound then
        PlaySound(name)
    end
end
