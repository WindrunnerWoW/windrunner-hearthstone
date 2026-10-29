-- Hearthstone addon: the Packs tab of the collection window.
--
-- Built into the window of CollectionUI.lua (bottom tab "Packs") as one full page below the window header: the
-- owner's "carved fantasy" pack opening background (packs_bg tiles, 1024 x 812) with a purple strip on the left and
-- a round stone slab with the empty card slot in the middle, like the pack opening of the original game.
--
--   left     one pack on the purple strip (a blue ribbon on its corner says how many are unopened). Drag
--            it onto the stone and let go: it moves to the card slot and the pack is opened
--            (H.PackOpen, the server rolls the cards). A click, or letting go anywhere else, sends the pack back.
--   middle   while the server answers the pack waits in the slot (it shakes); then it bursts in light and the five
--            cards fly out of the slot into a ring: one on top, one left, one right, two below. A card starts face
--            down; only while the mouse is over a face down card its rarity shows as a glow around it (Common none,
--            Rare blue, Epic purple, Legendary orange; Basic counts as Common), so the rarity is not given away up
--            front. A click turns the card over (it squashes to an edge and opens again): the card keeps the glow of
--            its rarity from then on, with its rarity name and a NEW! tag for a card the account did not own yet;
--            hovering a turned card shows the large preview of the collection. Once every card is turned, the Done
--            button (a glowing blue plate) appears in the middle of the ring; the cards and the button fade out when it
--            is clicked.
--   buttons  Done in the middle of the ring; under the slab "Recent Packs" while no cards are on screen, which lists
--            the last five openings (click one to see its cards face up again, Done closes that view).
--
-- Everything the reveal needs lives in Packs.lua, so closing the window loses nothing: the cards are already in
-- the collection (the server granted them), and the reveal is back where it was when the tab is shown again (without
-- the burst). After a /reload an unfinished reveal comes back through HearthstoneDB.packsSeen (see Packs.lua).
-- All frames are created once in PU.Build. The animations are 2D (textures, no model), driven by PU.Step from the
-- OnUpdate of the panel; they are cosmetic: PU.CancelAnim puts everything in its final place.
--
-- Without the art (CollectionUI.Piece) every part keeps a native look: a dark page, the book icon as the pack,
-- UIPanelButtons, the rarity colour as the border of a card holder. A pack texture of its own ("pack_classic" in
-- HS_CollectionTextures) replaces the card back used as the pack.
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...', no '%'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local PK = HsPacks
local CM = HsCollection
H.PacksUI = H.PacksUI or {}
local PU = H.PacksUI

-- layout (pixels at scale 1, relative to the panel's top left corner; the panel is the whole window below the header)
PU.PANEL_W, PU.PANEL_H = 1024, 812
PU.ARENA_CX, PU.ARENA_CY = 598, 351   -- centre of the round slab and of the card slot in it
PU.DROP_W, PU.DROP_H = 330, 360       -- a pack let go inside this rectangle (around the centre) is opened
PU.DROP_GLOW_W, PU.DROP_GLOW_H = 252, 368   -- the blue glow around the slot while a pack is over it / waits in it
PU.DROP_GLOW_DY = 11                  -- it reaches further down than up, to fill the whole hole of the slot
PU.DROP_GLOW_TINT = { 0.3, 0.6, 1 }
PU.CARD_W = 168                       -- a pack card (the collection's are 172 wide): big enough to read without the preview
PU.CARD_H = math.floor(168 * 388 / 256 + 0.5)
PU.GLOW = 4                           -- the holder is this much bigger than the card on every side
PU.ICON = "Interface\\Icons\\INV_Misc_Book_09"
PU.PACK_NAME = "Classic Pack"
local HOLDER_W, HOLDER_H = PU.CARD_W + 2 * PU.GLOW, PU.CARD_H + 2 * PU.GLOW
-- centres of the five cards, a ring around the card slot: top, left, right, bottom left, bottom right. The side cards
-- reach a little over the edge of the slab onto the frame, like the original; the visible cards (148 x 208) do not touch.
PU.SLOT_POS = {
    { 598, 186 },
    { 408, 292 }, { 788, 292 },
    { 519, 540 }, { 677, 540 },
}
PU.REVEAL_CX = PU.ARENA_CX
PU.HEADER_Y = 694                     -- centre of the line "Pack #n ..." under the lower cards
PU.HINT_Y = 660                      -- centre of the hint under the empty slab (above Recent Packs)
PU.BUTTON_Y = 372                     -- centre of Done: in the middle of the ring of cards, between the two rows of labels
PU.DONE_W, PU.DONE_H = 160, 80        -- the Done art (the plate and its glow, 2:1); the plate itself is about 82 % of it
PU.DONE_INSET = { 20, 20, 12, 12 }    -- the click area is the plate: left, right, top, bottom px of the art left out
PU.FADE_TIME = 0.35                   -- the cards and Done fade out when Done is clicked
PU.RECENT_BUTTON_Y = 696              -- centre of Recent Packs (below the slab, no cards on screen)
PU.BUTTON_W, PU.BUTTON_H = 170, 44
PU.STACK_CX, PU.STACK_Y0 = 177, 255   -- centre of the pack column and top of the first pack of the stack
PU.PACK_W, PU.PACK_H = 150, 192
PU.STACK_PITCH = 52                   -- each pack of the stack is this much lower than the one behind it
PU.STACK_MAX = 1                      -- packs drawn in the stack: one
PU.STATUS_Y = 628                     -- top of the status line at the bottom of the strip
PU.BADGE_W, PU.BADGE_H = 44, 56       -- the unopened count: a blue pennant hanging from the top right of the pack
PU.BADGE_DX, PU.BADGE_DY = -12, -9    -- its top right corner from the pack's top right corner (y negative = below it)
PU.BG_INSET = 6                       -- the background stays this far from the left, right and bottom edge of the window,
                                      -- clear of the transparent outer margin of the stone frame (CollectionUI UI.FRAME_METAL)
PU.RECENT_W, PU.RECENT_H, PU.RECENT_ROW_W, PU.RECENT_PITCH = 440, 270, 400, 38
-- tint of the white glow line per rarity (a Common card has none)
PU.GLOW_TINT = {
    RARE = { 0.25, 0.6, 1 },
    EPIC = { 0.8, 0.35, 1 },
    LEGENDARY = { 1, 0.6, 0.1 },
}
PU.NEW_TAG = "|cff1c9a1cNEW!|r"
-- animation (seconds)
PU.FLY_TIME = 0.2                     -- pack from the cursor to the slot / back to the stack
PU.CARD_START, PU.CARD_STEP, PU.CARD_TIME = 0.2, 0.1, 0.45
PU.BURST_TIME = 0.6
PU.FLIP_TIME = 0.36                   -- a card turns over: it squashes to an edge and opens again
PU.CLICK_MOVE = 8                     -- px the cursor may move for a press to count as a click
local WHITE = "Interface\\Buttons\\WHITE8X8"
local TOOLTIP_EDGE = "Interface\\Tooltips\\UI-Tooltip-Border"
local HILITE = "Interface\\QuestFrame\\UI-QuestTitleHighlight"
local STAR = "Interface\\Cooldown\\star4"
local RING = "Interface\\Buttons\\UI-ActionButton-Border"

PU.panel = nil
PU.slots = {}
PU.rows = {}
PU.stackPacks = {}
PU.hover = nil
PU.held = false         -- a pack is out of the stack (dragged, flying, waiting in the slot)
PU.drag = nil           -- { dx, dy, sx, sy, moved } while the mouse button is down on the stack
PU.awaiting = false     -- PACK_OPEN is on its way for the pack in the slot
PU.arrived = false      -- the pack has reached the slot
PU.tween = nil          -- the flight of the pack
PU.anim = nil           -- { t } while the burst and the cards flying out play
PU.clock = 0
PU.recentOpen = false
PU.fx, PU.fy = 0, 0     -- where the pack is

local function ui()
    return H.CollectionUI
end

-- A piece of the board art (Textures\Board, BoardTextures.lua), or nil. The card back of the match is the pack's.
local function boardPiece(name)
    local t = HS_BoardTextures
    if type(t) ~= "table" or type(t.pieces) ~= "table" then
        return nil
    end
    local p = t.pieces[name]
    if type(p) == "table" and type(p.path) == "string" and type(p.right) == "number" and type(p.bottom) == "number" then
        return p
    end
    return nil
end

local function clamp01(v)
    if v < 0 then
        return 0
    elseif v > 1 then
        return 1
    end
    return v
end

local function easeOut(u)
    local k = 1 - u
    return 1 - k * k * k
end

local function lerp(a, b, u)
    return a + (b - a) * u
end

-- Puts a frame's centre at (cx, cy) of the panel (y counts down) at 'scale'.
local function place(frame, cx, cy, scale)
    scale = scale or 1
    frame:SetScale(scale)
    frame:ClearAllPoints()
    frame:SetPoint("CENTER", PU.panel, "TOPLEFT", cx / scale, -cy / scale)
end

-- ------------------------------------------------------------- logic
-- What the reveal area shows: "reveal" (PK.Current()), "view" (a recent opening) or "idle".
function PU.Mode()
    local r = PK.Current()
    if r then
        return "reveal", r
    end
    if PK.view then
        return "view", PK.view
    end
    return "idle", nil
end

-- Card i of the reveal area: id, face up, new tag.
function PU.SlotInfo(i)
    local mode, o = PU.Mode()
    if mode == "reveal" then
        return o.cards[i], o.flipped[i] == true, o.flipped[i] == true and o.new[i] == true
    elseif mode == "view" then
        return o.cards[i], true, false
    end
    return nil, false, false
end

-- Status line under the unopened count: (text, isProblem).
function PU.StatusText()
    if not H.state or not H.state.welcomed then
        return "Not connected to the server.", false
    end
    local s = PK.state
    if s == "unsupported" then
        return "This server does not support packs yet.", false
    elseif s == "loading" or s == "none" then
        return "Loading your packs...", false
    elseif s == "error" then
        return "Your packs could not be loaded (" .. tostring(PK.errorText or "?") .. ").", true
    end
    if PK.IsOpening() then
        return "Opening...", false
    end
    if PK.message then
        return PK.message, false
    end
    if PK.stock <= 0 then
        return "You have no unopened packs.", false
    end
    return "", false
end

function PU.UnopenedText()
    if PK.state == "ready" then
        return "Unopened: " .. PK.stock
    end
    return "Unopened: -"
end

-- The number on the pack: the unopened packs, minus the one that is in the hand or waiting for the server.
function PU.CountText()
    if PK.state ~= "ready" then
        return "-"
    end
    local n = PK.stock
    if PU.held and not PK.Current() then
        n = n - 1
    end
    if n < 0 then
        n = 0
    end
    return tostring(n)
end

-- Line under the cards.
function PU.HeaderText()
    local mode, o = PU.Mode()
    if mode == "reveal" then
        local text = "Pack #" .. o.seq
        if o.recovered then
            text = text .. " (opened earlier)"
        end
        if not PK.AllFlipped() then
            text = text .. " - click the cards to turn them over"
        end
        return text
    elseif mode == "view" then
        return "Pack #" .. o.seq .. " (opened earlier)"
    end
    return ""
end

-- Text under the empty slab.
function PU.HintText()
    if PU.Mode() ~= "idle" then
        return ""
    end
    if PK.state == "ready" and PK.stock > 0 then
        return "Drag a pack onto the stone to open it."
    end
    return ""
end

function PU.CanOpen()
    return H.PackOpenBlocker ~= nil and H.PackOpenBlocker() == nil
end

-- The name of a rarity in its colour (Basic cards have the rarity FREE and count as Common).
function PU.RarityText(card)
    return CM.RarityColor(card) .. CM.RarityLabel(card) .. "|r"
end

-- Packs drawn in the stack: the unopened ones, minus the one that is out (dragged, flying, waiting for the server).
function PU.ShownPacks()
    local n = 0
    if PK.state == "ready" then
        n = PK.stock
        if PU.held and not PK.Current() then
            n = n - 1
        end
    end
    if n < 0 then
        n = 0
    end
    if n > PU.STACK_MAX then
        n = PU.STACK_MAX
    end
    return n
end

-- Centre of pack i of the stack.
function PU.StackPos(i)
    return PU.STACK_CX, PU.STACK_Y0 + PU.PACK_H / 2 + (i - 1) * PU.STACK_PITCH
end

-- The client draws the interface about 1.2 times wider than its units say (the owner's setup), so everything round
-- turns into an ellipse. The pack and its pennant are drawn that much narrower in interface units, so that they keep
-- their own aspect on the screen. The rest of the window (cards, background) is not corrected.
PU.STRETCH = 1.2

function PU.Stretch()
    return PU.STRETCH
end

-- The packs are drawn PU.PACK_W / stretch wide in interface units, so that they have their own aspect on the screen.
function PU.PackWidth()
    return math.floor(PU.PACK_W / PU.Stretch() + 0.5)
end

function PU.ApplyStretch()
    local f = PU.panel
    if not f then
        return
    end
    local w = PU.PackWidth()
    for i = 1, table.getn(PU.stackPacks) do
        PU.stackPacks[i]:SetWidth(w)
    end
    f.stack:SetWidth(w)
    f.flyer:SetWidth(w)
    f.badge:SetWidth(math.floor(PU.BADGE_W / PU.Stretch() + 0.5))
end

-- Where the cursor is on the panel (y counts down), or nil.
function PU.CursorPos()
    local p = PU.panel
    if not p or not GetCursorPosition or not p.GetLeft then
        return nil, nil
    end
    local l, b = p:GetLeft(), p:GetBottom()
    if not l or not b then
        return nil, nil
    end
    local x, y = GetCursorPosition()
    local s = p:GetEffectiveScale()
    if not s or s <= 0 then
        return nil, nil
    end
    return x / s - l, PU.PANEL_H - (y / s - b)
end

function PU.InDropZone(x, y)
    if not x then
        return false
    end
    return math.abs(x - PU.ARENA_CX) <= PU.DROP_W / 2 and math.abs(y - PU.ARENA_CY) <= PU.DROP_H / 2
end

-- ---------------------------------------------------------- refreshing
local function isShown()
    return PU.panel ~= nil and PU.panel:IsVisible()
end

local function sound(name)
    if PlaySound then
        PlaySound(name)
    end
end

local function hidePreview()
    if H.CollectionUI and H.CollectionUI.HidePreview then
        H.CollectionUI.HidePreview()
    end
end

local function showPreview(id)
    if H.CollectionUI and H.CollectionUI.ShowPreview then
        H.CollectionUI.ShowPreview(id)
    end
end

-- The glow of a card's rarity (none for Common): a face-down card shows it only while the mouse is over it, so the
-- rarity is not given away; a card that is turned over (and has finished turning) keeps it. The border of the plain
-- card back turns gold on hover.
function PU.SetBackLook(slot, hover)
    local down = hover and not slot.faceUp and slot.id ~= nil
    local lit = down or (slot.faceUp and not slot.flip and slot.id ~= nil)
    if slot.glow then
        if lit and slot.tint then
            slot.glow:SetVertexColor(slot.tint[1], slot.tint[2], slot.tint[3])
            slot.glow:SetAlpha(1)
            slot.glow:Show()
        else
            slot.glow:Hide()
        end
    elseif slot.rc then
        if lit then
            slot.holder:SetBackdropBorderColor(slot.rc[1], slot.rc[2], slot.rc[3], 1)
            slot.holder:SetBackdropColor(slot.rc[1], slot.rc[2], slot.rc[3], 0.3)
        else
            slot.holder:SetBackdropBorderColor(0, 0, 0, 0)
            slot.holder:SetBackdropColor(0, 0, 0, 0)
        end
    end
    if slot.back.hasBorder then
        if down then
            slot.back:SetBackdropBorderColor(1, 0.82, 0, 1)
        else
            slot.back:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        end
    end
end

function PU.RefreshSlot(i)
    local slot = PU.slots[i]
    if not slot then
        return
    end
    local id, faceUp, isNew = PU.SlotInfo(i)
    slot.id = id
    slot.faceUp = faceUp
    -- the cards stay hidden while the pack is on its way or waits for the server: the burst shows them
    if PU.awaiting and not PU.anim then
        slot.holder:Hide()
        return
    end
    if not id then
        PU.EndFlip(slot)
        slot.holder:Hide()
        return
    end
    local flipping = slot.flip ~= nil
    -- while a card turns the back is shown until the card is edge on, then the face
    local showFace = faceUp and not (flipping and slot.flip.t < PU.FLIP_TIME / 2)
    local card = nil
    local db = CM.GetDB()
    if db then
        card = db.cards[id]
    end
    local rarity = PK.RarityOf(id)
    slot.tint = PU.GLOW_TINT[rarity]
    slot.rc = PK.RARITY_COLORS[rarity]
    if showFace then
        slot.back:Hide()
        H.CardWidget.SetCard(slot.widget, id)
        slot.widget.frame:Show()
        slot.rarityText:SetText(PU.RarityText(card))
        if flipping then
            slot.rarityBadge:Hide()
            slot.newBadge:Hide()
        else
            slot.rarityBadge:Show()
            if isNew then
                slot.newBadge:Show()
            else
                slot.newBadge:Hide()
            end
        end
    else
        slot.widget.frame:Hide()
        slot.back:Show()
        slot.rarityBadge:Hide()
        slot.newBadge:Hide()
    end
    PU.SetBackLook(slot, PU.hover == i)
    slot.holder:Show()
end

function PU.RefreshRows()
    local recent = PK.recent or {}
    for i = 1, PK.MAX_RECENT do
        local row = PU.rows[i]
        local o = recent[i]
        if row then
            if o and PK.state == "ready" then
                row.text:SetText(PK.RecentText(o))
                if PK.view and PK.view.seq == o.seq then
                    row:LockHighlight()
                else
                    row:UnlockHighlight()
                end
                row:Show()
            else
                row:Hide()
            end
        end
    end
    local f = PU.panel
    if PK.state == "ready" and table.getn(recent) == 0 then
        f.recentEmpty:SetText("No packs opened yet.")
    elseif PK.state ~= "ready" then
        f.recentEmpty:SetText("-")
    else
        f.recentEmpty:SetText("")
    end
end

local function setEnabled(b, enabled)
    if ui() and ui().SetButtonEnabled then
        ui().SetButtonEnabled(b, enabled)
    elseif enabled then
        b:Enable()
    else
        b:Disable()
    end
end

-- The stack: pack i of the n drawn ones, the front one (the last) is the one that is dragged.
function PU.RefreshStack()
    local f = PU.panel
    local n = PU.ShownPacks()
    for i = 1, PU.STACK_MAX do
        local sp = PU.stackPacks[i]
        if sp then
            if i <= n then
                sp:Show()
            else
                sp:Hide()
            end
        end
    end
    local stack = f.stack
    if n > 0 or PU.drag then
        stack:SetHeight(PU.PACK_H + (n - 1) * PU.STACK_PITCH)
        stack:Show()
    else
        stack:Hide()
    end
end

-- The pack in the slot / on its way: what is shown of it (flyer) and the glow of the slot.
function PU.RefreshFlyer()
    local f = PU.panel
    local out = PU.held or PU.anim ~= nil
    if out and not (PU.anim and PU.anim.t >= 0.35) then
        f.flyer:Show()
    else
        f.flyer:Hide()
    end
    local over = false
    if PU.drag then
        local x, y = PU.CursorPos()
        over = PU.InDropZone(x, y)
    end
    if over or (PU.awaiting and PU.arrived) then
        f.dropGlow:Show()
    else
        f.dropGlow:Hide()
    end
end

function PU.Refresh()
    local f = PU.panel
    if not f then
        return
    end
    -- the pack in the slot: burst when the cards are there, back to the stack when the opening failed
    if PU.awaiting and PU.arrived and not PU.anim then
        if PK.Current() then
            PU.StartBurst()
        elseif not PK.IsOpening() then
            PU.awaiting = false
            PU.ReturnPack()
        end
    end
    local mode = PU.Mode()
    if mode ~= "idle" then
        PU.recentOpen = false
    end
    f.unopened:SetText(PU.CountText())
    local text, problem = PU.StatusText()
    if problem then
        f.status:SetText("|cffff5555" .. text .. "|r")
    else
        f.status:SetText(text)
    end
    PU.RefreshStack()
    PU.RefreshFlyer()
    PU.RefreshRows()

    for i = 1, PK.CARDS do
        PU.RefreshSlot(i)
    end
    f.header:SetText(PU.HeaderText())
    f.hint:SetText(PU.HintText())
    if mode == "reveal" and not PK.AllFlipped() then
        f.doneButton:Hide()                 -- the cards are turned by clicking them
        f.recentButton:Hide()
    elseif mode ~= "idle" then
        f.doneButton:Show()
        f.recentButton:Hide()
    else
        f.doneButton:Hide()
        f.recentButton:Show()
        setEnabled(f.recentButton, PK.state == "ready")
    end
    if PU.recentOpen and mode == "idle" and PK.state == "ready" then
        f.recentFrame:Show()
    else
        PU.recentOpen = false
        f.recentFrame:Hide()
    end
    -- keep the large preview in step with the card under the mouse
    local slot = PU.hover and PU.slots[PU.hover]
    if slot and slot.id and slot.faceUp then
        showPreview(slot.id)
    elseif PU.hover then
        hidePreview()
        if not (slot and slot.id) then
            PU.hover = nil
        end
    end
end

-- The Packs tab became visible (tab click, window opened on it): retry a failed load once.
function PU.OnTabShown()
    if PK.state == "error" and H.packs and not H.packs.reqId and H.PackGet then
        H.PackGet()
    end
    PU.ApplyStretch()
    PU.Refresh()
end

-- Forgets the hovered card (hiding a frame under the cursor may not fire OnLeave) and ends what is moving: the
-- window is closing or another tab is shown.
function PU.ClearHover()
    PU.hover = nil
    for i = 1, table.getn(PU.slots) do
        PU.SetBackLook(PU.slots[i], false)
    end
    PU.CancelAnim()
end

function H.OnPacksChanged()
    if isShown() then
        PU.Refresh()
    end
end

-- ----------------------------------------------------------- animation
-- Cards in their final places, full size.
local function placeCards()
    for i = 1, PK.CARDS do
        local slot = PU.slots[i]
        local pos = PU.SLOT_POS[i]
        if slot then
            place(slot.holder, pos[1], pos[2], 1)
            slot.holder:SetAlpha(1)
        end
    end
end

local function showFx(fx, size, alpha)
    fx:SetWidth(size)
    fx:SetHeight(size)
    fx:SetAlpha(clamp01(alpha))
    if alpha > 0 then
        fx:Show()
    else
        fx:Hide()
    end
end

local function hideFx()
    local f = PU.panel
    if not f then
        return
    end
    f.fxStar:Hide()
    f.fxRing:Hide()
    if f.fxSparkle then
        f.fxSparkle:Hide()
    end
end

-- The pack flies from where it is to (tx, ty); 'done' runs on arrival.
function PU.Fly(tx, ty, dur, done)
    PU.tween = { fx = PU.fx, fy = PU.fy, tx = tx, ty = ty, t = 0, dur = dur, done = done }
end

local function setFlyer(x, y, scale, alpha)
    local f = PU.panel
    PU.fx, PU.fy = x, y
    place(f.flyer, x, y, scale or 1)
    f.flyer:SetAlpha(alpha or 1)
end

-- Back to the stack (the opening failed or the pack was let go outside the slab).
function PU.ReturnPack()
    PU.arrived = false
    local n = PU.ShownPacks() + 1
    if n > PU.STACK_MAX then
        n = PU.STACK_MAX
    end
    local x, y = PU.StackPos(n)
    PU.Fly(x, y, PU.FLY_TIME, function()
        PU.held = false
        PU.Refresh()
    end)
    PU.Refresh()
end

-- The pack reached the slot: ask for the opening (already sent) and wait.
local function arrivedAtSlot()
    PU.arrived = true
    PU.Refresh()
end

-- The burst of light in the slot, then the cards fly out of it.
function PU.StartBurst()
    PU.held = false
    PU.awaiting = false
    PU.arrived = false
    PU.anim = { t = 0 }
    PU.tween = nil
    sound("igQuestListComplete")
    PU.ApplyAnim(0)
end

-- One frame of the burst at time t (also the first frame at t = 0).
function PU.ApplyAnim(t)
    local f = PU.panel
    local cx, cy = PU.ARENA_CX, PU.ARENA_CY
    -- the pack swells and fades into the light
    if t < 0.35 then
        local u = t / 0.35
        setFlyer(cx, cy, 1 + 0.3 * u, 1 - u)
        f.flyer:Show()
    else
        f.flyer:Hide()
    end
    -- light: a star flash, a ring that runs out and a sparkle
    local b = PU.BURST_TIME
    if t < b then
        local u = t / b
        showFx(f.fxStar, lerp(80, 440, easeOut(u)), (1 - u) * (1 - u) * 1.4)
        local ur = clamp01((t - 0.05) / b)
        showFx(f.fxRing, lerp(120, 560, easeOut(ur)), (1 - ur) * 0.9)
        if f.fxSparkle then
            showFx(f.fxSparkle, lerp(120, 380, easeOut(u)), (1 - u) * 1.2)
        end
    else
        hideFx()
    end
    -- the cards fly out of the slot to their places
    for i = 1, PK.CARDS do
        local slot = PU.slots[i]
        local pos = PU.SLOT_POS[i]
        local start = PU.CARD_START + (i - 1) * PU.CARD_STEP
        local u = clamp01((t - start) / PU.CARD_TIME)
        local e = easeOut(u)
        if slot then
            place(slot.holder, lerp(cx, pos[1], e), lerp(cy, pos[2], e), 0.35 + 0.65 * e)
            slot.holder:SetAlpha(clamp01(u * 3))
        end
    end
end

local function animLength()
    return PU.CARD_START + (PK.CARDS - 1) * PU.CARD_STEP + PU.CARD_TIME
end

-- Ends the burst: everything in its final place.
function PU.FinishAnim()
    PU.anim = nil
    hideFx()
    placeCards()
    if PU.panel then
        PU.panel.flyer:SetAlpha(1)
    end
end

-- Puts everything in its final place at once: the window closes, another tab is shown. A pack that waits for the
-- server stays in the slot; a pack in the hand goes back to the stack.
function PU.CancelAnim()
    if PU.fadeOut then
        PU.CompleteDone()
    end
    if PU.anim then
        PU.FinishAnim()
    end
    for i = 1, table.getn(PU.slots) do
        PU.EndFlip(PU.slots[i])
    end
    PU.tween = nil
    PU.drag = nil
    if PU.awaiting then
        PU.arrived = true
        if PU.panel then
            setFlyer(PU.ARENA_CX, PU.ARENA_CY, 1, 1)
        end
    else
        PU.held = false
    end
    if PU.panel then
        PU.panel.dropGlow:Hide()
    end
end

-- A card turns over: the back squashes to an edge (first half), the face opens from the edge (second half) and the
-- card lifts a little. slot.flip = { t }.
function PU.StartFlip(i, delay)
    local slot = PU.slots[i]
    if slot then
        slot.flip = { t = -(delay or 0) }
    end
end

-- Back to the plain card: full width, normal size, labels.
function PU.EndFlip(slot)
    if not slot.flip then
        return
    end
    slot.flip = nil
    local vl, vt, vr, vb = slot.vis.left, slot.vis.top, slot.vis.right, slot.vis.bottom
    slot.back:ClearAllPoints()
    slot.back:SetPoint("TOPLEFT", slot, "TOPLEFT", vl, -vt)
    slot.back:SetPoint("BOTTOMRIGHT", slot, "TOPLEFT", vr, -vb)
    slot.widget.frame:SetWidth(PU.CARD_W)
    local pos = PU.SLOT_POS[slot.index]
    if pos and not PU.anim then
        place(slot.holder, pos[1], pos[2], 1)
    end
end

function PU.ApplyFlip(slot)
    local fl = slot.flip
    local half = PU.FLIP_TIME / 2
    local t = fl.t
    if t < half then
        local f = 1 - clamp01(t / half)
        if f < 0.03 then
            f = 0.03
        end
        local vl, vt, vr, vb = slot.vis.left, slot.vis.top, slot.vis.right, slot.vis.bottom
        local mid, arm = (vl + vr) / 2, (vr - vl) / 2 * f
        slot.back:ClearAllPoints()
        slot.back:SetPoint("TOPLEFT", slot, "TOPLEFT", mid - arm, -vt)
        slot.back:SetPoint("BOTTOMRIGHT", slot, "TOPLEFT", mid + arm, -vb)
    else
        local f = easeOut(clamp01((t - half) / half))
        if f < 0.03 then
            f = 0.03
        end
        slot.widget.frame:SetWidth(PU.CARD_W * f)
    end
    local pos = PU.SLOT_POS[slot.index]
    if pos and not PU.anim then
        place(slot.holder, pos[1], pos[2], 1 + 0.1 * math.sin(3.14159 * clamp01(t / PU.FLIP_TIME)))
    end
end

-- One step of everything that moves (OnUpdate of the panel).
function PU.Step(dt)
    local f = PU.panel
    if not f or not dt then
        return
    end
    if dt > 0.1 then
        dt = 0.1
    end
    PU.clock = PU.clock + dt
    local d = PU.drag
    if d and IsMouseButtonDown and not IsMouseButtonDown("LeftButton") then
        PU.OnStackUp()
        d = PU.drag
    end
    if d then
        local x, y = PU.CursorPos()
        if x then
            if not d.moved and (math.abs(x - d.sx) > PU.CLICK_MOVE or math.abs(y - d.sy) > PU.CLICK_MOVE) then
                d.moved = true
            end
            setFlyer(x + d.dx, y + d.dy, 1, 1)
            local over = PU.InDropZone(x, y)
            if over then
                f.dropGlow:Show()
            else
                f.dropGlow:Hide()
            end
        end
    end
    local tw = PU.tween
    if tw then
        tw.t = tw.t + dt
        local u = clamp01(tw.t / tw.dur)
        local e = easeOut(u)
        setFlyer(lerp(tw.fx, tw.tx, e), lerp(tw.fy, tw.ty, e), 1, 1)
        if u >= 1 then
            PU.tween = nil
            if tw.done then
                tw.done()
            end
        end
    elseif PU.awaiting and PU.arrived and not PU.anim then
        -- the pack shakes in the slot while the server answers
        local amp = 2 + 1.5 * math.sin(PU.clock * 5)
        setFlyer(PU.ARENA_CX + amp * math.sin(PU.clock * 42), PU.ARENA_CY + 0.5 * amp * math.sin(PU.clock * 37), 1, 1)
        f.dropGlow:SetAlpha(0.6 + 0.4 * math.sin(PU.clock * 7))
    end
    if PU.anim then
        PU.anim.t = PU.anim.t + dt
        if PU.anim.t >= animLength() then
            PU.FinishAnim()
            PU.Refresh()
        else
            PU.ApplyAnim(PU.anim.t)
        end
    end
    local fo = PU.fadeOut
    if fo then
        fo.t = fo.t + dt
        if fo.t >= PU.FADE_TIME then
            PU.CompleteDone()
            return
        end
        local a = 1 - fo.t / PU.FADE_TIME
        for i = 1, table.getn(PU.slots) do
            PU.slots[i].holder:SetAlpha(a)
        end
        f.doneButton:SetAlpha(a)
    end
    local changed = false
    for i = 1, table.getn(PU.slots) do
        local slot = PU.slots[i]
        local fl = slot.flip
        if fl then
            local half = PU.FLIP_TIME / 2
            local before = fl.t
            fl.t = fl.t + dt
            if fl.t >= PU.FLIP_TIME then
                PU.EndFlip(slot)
                changed = true
            else
                PU.ApplyFlip(slot)
                if before < half and fl.t >= half then
                    changed = true          -- edge on: the face takes over
                end
            end
        end
    end
    if changed then
        PU.Refresh()
    end
end

-- ------------------------------------------------------------ actions
-- The mouse button went down on the stack: the front pack is in the hand.
function PU.OnStackDown()
    if PU.drag or PU.tween or PU.anim or PU.held then
        return
    end
    local why = nil
    if H.PackOpenBlocker then
        why = H.PackOpenBlocker()
    end
    if why then
        PK.message = why
        PU.Refresh()
        return
    end
    PK.message = nil
    local n = PU.ShownPacks()
    if n < 1 then
        return
    end
    local sx, sy = PU.StackPos(n)
    local x, y = PU.CursorPos()
    local d = { sx = x or sx, sy = y or sy, dx = 0, dy = 0, moved = false }
    if x then
        d.dx, d.dy = sx - x, sy - y
    end
    PU.drag = d
    PU.held = true
    setFlyer(sx, sy, 1, 1)
    PU.Refresh()
end

-- The mouse button went up: a drop on the slab opens the pack, anywhere else (a click included) it goes back.
function PU.OnStackUp()
    local d = PU.drag
    if not d then
        return
    end
    PU.drag = nil
    local x, y = PU.CursorPos()
    if PU.InDropZone(x, y) then
        PU.StartOpen()
    else
        PU.ReturnPack()
    end
end

-- The pack flies to the slot and the opening is requested at once (the answer may come before it lands).
function PU.StartOpen()
    local key, why = nil, nil
    if H.PackOpen then
        key, why = H.PackOpen()
    end
    if not key then
        if why then
            PK.message = why
        end
        PU.awaiting = false
        PU.ReturnPack()
        return
    end
    PU.awaiting = true
    PU.arrived = false
    PU.Fly(PU.ARENA_CX, PU.ARENA_CY, PU.FLY_TIME, arrivedAtSlot)
    PU.Refresh()
end

-- /hs open: the front pack of the stack flies to the slot and is opened, as if it had been clicked. Returns the key of
-- the opening, or nil and the reason it cannot be opened.
function PU.OpenNow()
    local why = nil
    if H.PackOpenBlocker then
        why = H.PackOpenBlocker()
    end
    if why then
        return nil, why
    end
    if not PU.panel or PU.drag or PU.tween or PU.anim or PU.held then
        return H.PackOpen()
    end
    local sx, sy = PU.StackPos(math.max(PU.ShownPacks(), 1))
    PU.held = true
    setFlyer(sx, sy, 1, 1)
    PU.StartOpen()
    if not PU.awaiting then
        return nil, PK.message
    end
    return PK.pending and PK.pending.key
end

function PU.OnSlotClick(i)
    if PU.Mode() ~= "reveal" or PU.anim then
        return
    end
    if PK.Flip(i) then
        sound("igAbilityPageTurn")
        PU.StartFlip(i, 0)
        PU.Refresh()
    end
end

-- Done: the cards and the button fade out (PU.Step), then the reveal ends (PU.CompleteDone).
function PU.OnDone()
    if PU.anim or PU.fadeOut then
        return
    end
    local mode = PU.Mode()
    if mode == "reveal" and not PK.AllFlipped() then
        return
    end
    if mode == "idle" then
        return
    end
    hidePreview()
    PU.hover = nil
    PU.fadeOut = { t = 0 }
end

-- The end of Done: the opening is seen, the cards are gone, everything is opaque again.
function PU.CompleteDone()
    PU.fadeOut = nil
    local f = PU.panel
    for i = 1, table.getn(PU.slots) do
        PU.slots[i].holder:SetAlpha(1)
    end
    if f then
        f.doneButton:SetAlpha(1)
    end
    local mode = PU.Mode()
    if mode == "reveal" then
        if not PK.FinishReveal() then
            return
        end
        PK.message = nil
    elseif mode == "view" then
        PK.CloseView()
    end
    PU.ClearHover()
    hidePreview()
    PU.Refresh()
end

function PU.OnRecentButton()
    PU.recentOpen = not PU.recentOpen
    PU.Refresh()
end

function PU.OnRecentClick(i)
    if PK.View(i) then
        PU.recentOpen = false
        PU.ClearHover()
        hidePreview()
        PU.Refresh()
    end
end

function PU.OnSlotEnter(i)
    local slot = PU.slots[i]
    if not slot or not slot.id or PU.anim then
        return
    end
    PU.hover = i
    if slot.faceUp then
        showPreview(slot.id)
    else
        PU.SetBackLook(slot, true)
    end
end

function PU.OnSlotLeave(i)
    local slot = PU.slots[i]
    if slot then
        PU.SetBackLook(slot, false)
    end
    if PU.hover == i then
        PU.hover = nil
    end
    hidePreview()
end

function PU.OnRowEnter(i)
    local row = PU.rows[i]
    if not row or not GameTooltip then
        return
    end
    GameTooltip:SetOwner(row, "ANCHOR_RIGHT")
    GameTooltip:SetText("Pack #" .. ((PK.recent[i] and PK.recent[i].seq) or "?"))
    GameTooltip:AddLine("Click to see these five cards again.", 1, 1, 1)
    GameTooltip:Show()
end

function PU.OnPackEnter()
    if not GameTooltip or not PU.panel then
        return
    end
    GameTooltip:SetOwner(PU.panel.stack, "ANCHOR_RIGHT")
    GameTooltip:SetText(PU.PACK_NAME)
    GameTooltip:AddLine("Five Basic and Classic cards, at least one Rare or better.", 1, 1, 1)
    GameTooltip:AddLine(PU.UnopenedText(), 0.6, 0.8, 1)
    GameTooltip:AddLine("Drag it onto the stone to open it.", 0.8, 0.8, 0.8)
    GameTooltip:Show()
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- -------------------------------------------------------------- build
-- A label under a card (the rarity name, the NEW! tag): a frame with a text, drawn over the card.
local function badge(parent, width, height, font)
    local b = CreateFrame("Frame", nil, parent)
    b:SetFrameLevel(parent:GetFrameLevel() + 6)
    b:SetWidth(width)
    b:SetHeight(height)
    local t = b:CreateFontString(nil, "OVERLAY", font)
    t:SetPoint("CENTER", b, "CENTER", 0, 0)
    b:Hide()
    return b, t
end

-- The look of a pack on frame f: the pack texture, else the card back of the board art in a gold border, else the book
-- icon in a gold border.
local function paintPack(f)
    local UI = ui()
    local tex = f:CreateTexture(nil, "ARTWORK")
    if UI.Piece("pack_classic") then
        UI.SetPiece(tex, "pack_classic")
        tex:SetAllPoints(f)
        return
    end
    local cb = boardPiece("card_back")
    f:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 16, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
    f:SetBackdropBorderColor(0.85, 0.68, 0.3, 1)
    if cb then
        tex:SetTexture(cb.path)
        tex:SetTexCoord(0, cb.right, 0, cb.bottom)
        tex:SetPoint("TOPLEFT", f, "TOPLEFT", 3, -3)
        tex:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -3, 3)
        tex:SetVertexColor(0.75, 0.85, 1)
    else
        tex:SetTexture(PU.ICON)
        tex:SetWidth(PU.PACK_H - 50)
        tex:SetHeight(PU.PACK_H - 50)
        tex:SetPoint("CENTER", f, "CENTER", 0, 0)
    end
end

-- One card holder: the holder (with the native rarity border), the glow, the card button with the card back, the card
-- and the labels under it.
local function makeSlot(panel, i)
    local UI = ui()
    local pos = PU.SLOT_POS[i]
    local holder = CreateFrame("Frame", nil, panel)
    holder:SetWidth(HOLDER_W)
    holder:SetHeight(HOLDER_H)
    place(holder, pos[1], pos[2], 1)
    holder:Hide()

    local slot = CreateFrame("Button", "HearthstonePackSlot" .. i, holder)
    slot:SetWidth(PU.CARD_W)
    slot:SetHeight(PU.CARD_H)
    slot:SetPoint("CENTER", holder, "CENTER", 0, 0)
    slot:EnableMouse(true)
    slot:RegisterForClicks("LeftButtonUp")

    -- the visible card inside the render (CardWidget.VISIBLE), as in the collection
    local vl, vt, vr, vb = H.CardWidget.VisibleRect(PU.CARD_W)
    slot.vis = { left = vl, top = vt, right = vr, bottom = vb }
    if slot.SetHitRectInsets then
        slot:SetHitRectInsets(vl, PU.CARD_W - vr, vt, PU.CARD_H - vb)
    end

    -- the glow of the rarity hugs the visible card (white line art tinted per rarity); it only shows while the mouse is
    -- over a face-down card. Without the art the holder carries the colour with its border and backdrop.
    local glow = nil
    if UI.Piece("glow_gold") then
        glow = holder:CreateTexture(nil, "BACKGROUND")
        UI.PlaceGlow(glow, "glow_gold", slot, vl, vt, vr, vb)
        glow:Hide()
    else
        holder:SetBackdrop({
            bgFile = WHITE, edgeFile = TOOLTIP_EDGE,
            tile = true, tileSize = 16, edgeSize = 12,
            insets = { left = 2, right = 2, top = 2, bottom = 2 },
        })
        holder:SetBackdropBorderColor(0, 0, 0, 0)
        holder:SetBackdropColor(0, 0, 0, 0)
    end

    local widget = H.CardWidget.Create(slot, "HearthstonePackSlot" .. i .. "Card", PU.CARD_W, false)
    widget.frame:SetPoint("CENTER", slot, "CENTER", 0, 0)
    widget.frame:Hide()

    -- card back over the visible card: the card back of the board art, or a dark frame with the pack emblem
    local back = CreateFrame("Frame", nil, slot)
    back:SetPoint("TOPLEFT", slot, "TOPLEFT", vl, -vt)
    back:SetPoint("BOTTOMRIGHT", slot, "TOPLEFT", vr, -vb)
    local cb = boardPiece("card_back")
    if cb then
        local tex = back:CreateTexture(nil, "ARTWORK")
        tex:SetTexture(cb.path)
        tex:SetTexCoord(0, cb.right, 0, cb.bottom)
        tex:SetAllPoints(back)
        back.hasBorder = false
    else
        back:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile = TOOLTIP_EDGE,
            tile = true, tileSize = 32, edgeSize = 16,
            insets = { left = 4, right = 4, top = 4, bottom = 4 },
        })
        back:SetBackdropColor(1, 1, 1, 1)
        back:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        back.hasBorder = true
        local inner = CreateFrame("Frame", nil, back)
        inner:SetPoint("TOPLEFT", back, "TOPLEFT", 12, -12)
        inner:SetPoint("BOTTOMRIGHT", back, "BOTTOMRIGHT", -12, 12)
        inner:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
        inner:SetBackdropBorderColor(0.55, 0.42, 0.2, 0.8)
        local emblem = back:CreateTexture(nil, "ARTWORK")
        emblem:SetTexture(PU.ICON)
        emblem:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        emblem:SetWidth(56)
        emblem:SetHeight(56)
        emblem:SetPoint("CENTER", back, "CENTER", 0, 0)
        emblem:SetVertexColor(0.85, 0.75, 0.55)
    end

    -- under the card: its rarity name, then the NEW! tag (labels on the stone, no plates)
    local rarityBadge, rarityText = badge(slot, 120, 14, "GameFontNormalSmall")
    rarityBadge:SetPoint("TOP", slot, "TOPLEFT", (vl + vr) / 2, -vb - 3)
    local newBadge, newLabel = badge(slot, 80, 20, "GameFontNormalLarge")
    newBadge:SetPoint("TOP", slot, "TOPLEFT", (vl + vr) / 2, -vb - 19)
    newLabel:SetText(PU.NEW_TAG)

    local idx = i
    slot:SetScript("OnClick", function()
        PU.OnSlotClick(idx)
    end)
    slot:SetScript("OnEnter", function()
        PU.OnSlotEnter(idx)
    end)
    slot:SetScript("OnLeave", function()
        PU.OnSlotLeave(idx)
    end)

    slot.index = i
    slot.holder = holder
    slot.glow = glow
    slot.widget = widget
    slot.back = back
    slot.newBadge = newBadge
    slot.rarityBadge = rarityBadge
    slot.rarityText = rarityText
    return slot
end

-- A row of the recent list, on the overlay.
local function makeRow(parent, i)
    local UI = ui()
    local row = CreateFrame("Button", "HearthstonePackRecent" .. i, parent)
    row:SetWidth(PU.RECENT_ROW_W)
    row:SetHeight(34)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", (PU.RECENT_W - PU.RECENT_ROW_W) / 2, -(54 + (i - 1) * PU.RECENT_PITCH))
    row:RegisterForClicks("LeftButtonUp")
    -- the row of the sheet's empty deck slot (dark purple with a gold line), or a dark plate
    local bg = row:CreateTexture(nil, "BACKGROUND")
    if UI.SetPiece(bg, "deck_empty") then
        bg:SetAllPoints(row)
    else
        bg:SetTexture(WHITE)
        bg:SetVertexColor(0.1, 0.08, 0.16, 0.85)
        bg:SetAllPoints(row)
    end
    row:SetHighlightTexture(HILITE, "ADD")
    local t = row:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
    t:SetPoint("LEFT", row, "LEFT", 18, 0)
    t:SetWidth(PU.RECENT_ROW_W - 36)
    t:SetJustifyH("LEFT")
    row.text = t
    local idx = i
    row:SetScript("OnClick", function()
        PU.OnRecentClick(idx)
    end)
    row:SetScript("OnEnter", function()
        PU.OnRowEnter(idx)
    end)
    row:SetScript("OnLeave", hideTip)
    row:Hide()
    return row
end

-- The number of unopened packs on the pack, like the original: a blue pennant (pack_badge, else a blue box) hanging
-- from the top right of the pack, with the count in white. It belongs to the pack frame, so it goes with it.
local function makeBadge(pack)
    local UI = ui()
    local b = CreateFrame("Frame", nil, pack)
    b:SetFrameLevel(pack:GetFrameLevel() + 2)
    b:SetWidth(PU.BADGE_W)
    b:SetHeight(PU.BADGE_H)
    b:SetPoint("TOPRIGHT", pack, "TOPRIGHT", PU.BADGE_DX, PU.BADGE_DY)
    local bg = b:CreateTexture(nil, "BACKGROUND")
    if UI.SetPiece(bg, "pack_badge") then
        bg:SetAllPoints(b)
    else
        b:SetBackdrop({
            bgFile = WHITE, edgeFile = TOOLTIP_EDGE,
            tile = true, tileSize = 16, edgeSize = 10,
            insets = { left = 3, right = 3, top = 3, bottom = 3 },
        })
        b:SetBackdropColor(0.12, 0.2, 0.55, 0.95)
        b:SetBackdropBorderColor(0.85, 0.68, 0.3, 1)
    end
    local t = b:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    t:SetPoint("CENTER", b, "CENTER", 0, 4)        -- the pennant's body is above its point
    return b, t
end

-- The background: the packs_bg tiles (1012 x 806, PU.BG_INSET from the left, right and bottom edge), or a plain dark page.
local function makeBackground(panel)
    local UI = ui()
    local names = UI.PACKS_BG_TILES
    if names and UI.PlaceTiles(panel, "packs_bg", names, PU.BG_INSET, 0, "BACKGROUND") then
        return true
    end
    local bg = panel:CreateTexture(nil, "BACKGROUND")
    bg:SetTexture(WHITE)
    bg:SetVertexColor(0.12, 0.09, 0.07, 1)
    bg:SetPoint("TOPLEFT", panel, "TOPLEFT", PU.BG_INSET, 0)
    bg:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -PU.BG_INSET, PU.BG_INSET)
    return false
end

-- Builds the panel into the collection window (called once from CollectionUI's UI.Build): the panel is the whole
-- window below the header, 'level' is its frame level (above the book art).
function PU.Build(main, x, y, level)
    local UI = ui()
    PU.slots = {}
    PU.rows = {}
    PU.stackPacks = {}
    PU.hover = nil
    PU.drag, PU.tween, PU.anim = nil, nil, nil
    PU.held, PU.awaiting, PU.arrived, PU.recentOpen = false, false, false, false
    local p = CreateFrame("Frame", "HearthstonePacksPanel", main)
    if level then
        p:SetFrameLevel(level)
    end
    level = p:GetFrameLevel()
    p:SetWidth(PU.PANEL_W)
    p:SetHeight(PU.PANEL_H)
    p:SetPoint("TOPLEFT", main, "TOPLEFT", x, y)
    p:EnableMouse(true)
    p:Hide()
    PU.panel = p
    p.hasArt = makeBackground(p)

    -- ---- the pack column (the purple strip)
    local name = p:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    name:SetPoint("CENTER", p, "TOPLEFT", PU.STACK_CX, -62)
    name:SetText(PU.PACK_NAME)
    p.packName = name

    for i = 1, PU.STACK_MAX do
        local sp = CreateFrame("Frame", nil, p)
        sp:SetFrameLevel(level + 1 + i)
        sp:SetWidth(PU.PACK_W)
        sp:SetHeight(PU.PACK_H)
        local cx, cy = PU.StackPos(i)
        place(sp, cx, cy, 1)
        paintPack(sp)
        sp:Hide()
        PU.stackPacks[i] = sp
    end
    -- the mouse area over the whole stack: press = take the front pack, release = drop it
    local stack = CreateFrame("Button", nil, p)
    stack:SetFrameLevel(level + 12)
    stack:SetWidth(PU.PACK_W)
    stack:SetHeight(PU.PACK_H)
    stack:SetPoint("TOP", p, "TOPLEFT", PU.STACK_CX, -PU.STACK_Y0)
    stack:EnableMouse(true)
    stack:SetScript("OnMouseDown", function()
        if arg1 == "LeftButton" then
            hideTip()
            PU.OnStackDown()
        end
    end)
    stack:SetScript("OnMouseUp", function()
        if arg1 == "LeftButton" then
            PU.OnStackUp()
        end
    end)
    stack:SetScript("OnEnter", function()
        if not PU.drag and not PU.held then
            PU.OnPackEnter()
        end
    end)
    stack:SetScript("OnLeave", hideTip)
    stack:Hide()
    p.stack = stack

    local badge, unopened = makeBadge(PU.stackPacks[1])
    p.badge = badge
    p.unopened = unopened

    local status = p:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    status:SetPoint("TOP", p, "TOPLEFT", PU.STACK_CX, -PU.STATUS_Y)
    status:SetWidth(124)
    status:SetHeight(58)
    status:SetJustifyH("CENTER")
    status:SetJustifyV("BOTTOM")
    p.status = status

    -- ---- the slab: the glow of the card slot (a pack is over it / waits in it), the reveal area
    local dropGlow = p:CreateTexture(nil, "ARTWORK")
    dropGlow:SetTexture(RING)
    dropGlow:SetBlendMode("ADD")
    dropGlow:SetVertexColor(PU.DROP_GLOW_TINT[1], PU.DROP_GLOW_TINT[2], PU.DROP_GLOW_TINT[3])
    dropGlow:SetWidth(PU.DROP_GLOW_W)
    dropGlow:SetHeight(PU.DROP_GLOW_H)
    dropGlow:SetPoint("CENTER", p, "TOPLEFT", PU.ARENA_CX, -(PU.ARENA_CY + PU.DROP_GLOW_DY))
    dropGlow:Hide()
    p.dropGlow = dropGlow

    local header = p:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    header:SetPoint("CENTER", p, "TOPLEFT", PU.REVEAL_CX, -PU.HEADER_Y)
    header:SetWidth(620)
    p.header = header
    local hint = p:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    hint:SetPoint("CENTER", p, "TOPLEFT", PU.REVEAL_CX, -PU.HINT_Y)
    hint:SetWidth(560)
    p.hint = hint
    for i = 1, PK.CARDS do
        PU.slots[i] = makeSlot(p, i)
    end

    -- Done: the glowing blue plate (the text is part of the art), above the cards; without the art a plain button
    local done
    local donePiece = UI.Piece("btn_done")
    if donePiece then
        done = CreateFrame("Button", "HearthstonePackDoneButton", p)
        done:SetWidth(PU.DONE_W)
        done:SetHeight(PU.DONE_H)
        done:SetNormalTexture(donePiece.path)
        done:GetNormalTexture():SetTexCoord(0, donePiece.right, 0, donePiece.bottom)
        done:SetHighlightTexture(donePiece.path, "ADD")
        local ht = done:GetHighlightTexture()
        ht:SetTexCoord(0, donePiece.right, 0, donePiece.bottom)
        ht:SetAlpha(0.35)
        if done.SetHitRectInsets then
            done:SetHitRectInsets(PU.DONE_INSET[1], PU.DONE_INSET[2], PU.DONE_INSET[3], PU.DONE_INSET[4])
        end
        done.art = donePiece
        UI.ButtonGlow(done)
    else
        done = UI.MakeButton("HearthstonePackDoneButton", p, PU.BUTTON_W, PU.BUTTON_H, "Done")
    end
    done:SetFrameLevel(level + 20)
    done:SetPoint("CENTER", p, "TOPLEFT", PU.REVEAL_CX, -PU.BUTTON_Y)
    done:SetScript("OnClick", function()
        PU.OnDone()
    end)
    done:Hide()
    p.doneButton = done
    local recentButton = UI.MakeButton("HearthstonePackRecentButton", p, PU.BUTTON_W, PU.BUTTON_H, "Recent Packs")
    recentButton:SetPoint("CENTER", p, "TOPLEFT", PU.REVEAL_CX, -PU.RECENT_BUTTON_Y)
    recentButton:SetScript("OnClick", function()
        PU.OnRecentButton()
    end)
    p.recentButton = recentButton

    -- ---- the recent packs: an overlay on the slab
    local rf = CreateFrame("Frame", nil, p)
    rf:SetFrameLevel(level + 25)
    rf:SetWidth(PU.RECENT_W)
    rf:SetHeight(PU.RECENT_H)
    rf:SetPoint("CENTER", p, "TOPLEFT", PU.ARENA_CX, -PU.ARENA_CY)
    rf:EnableMouse(true)
    rf:SetBackdrop({
        bgFile = WHITE, edgeFile = TOOLTIP_EDGE,
        tile = true, tileSize = 16, edgeSize = 14,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    rf:SetBackdropColor(0.07, 0.05, 0.1, 0.95)
    rf:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    local recentTitle = rf:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    recentTitle:SetPoint("TOP", rf, "TOP", 0, -18)
    recentTitle:SetText("Recent Packs")
    local recentEmpty = rf:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    recentEmpty:SetPoint("TOP", rf, "TOP", 0, -72)
    rf:Hide()
    p.recentFrame = rf
    p.recentTitle = recentTitle
    p.recentEmpty = recentEmpty
    for i = 1, PK.MAX_RECENT do
        PU.rows[i] = makeRow(rf, i)
    end

    -- ---- the pack that is out of the stack (dragged, flying, waiting in the slot) and the burst of light
    local flyer = CreateFrame("Frame", nil, p)
    flyer:SetFrameLevel(level + 40)
    flyer:SetWidth(PU.PACK_W)
    flyer:SetHeight(PU.PACK_H)
    paintPack(flyer)
    flyer:Hide()
    p.flyer = flyer

    local fx = CreateFrame("Frame", nil, p)
    fx:SetFrameLevel(level + 30)
    fx:SetAllPoints(p)
    local function fxTexture(path, r, g, b)
        local t = fx:CreateTexture(nil, "OVERLAY")
        t:SetTexture(path)
        t:SetBlendMode("ADD")
        t:SetVertexColor(r, g, b)
        t:SetPoint("CENTER", p, "TOPLEFT", PU.ARENA_CX, -PU.ARENA_CY)
        t:SetWidth(100)
        t:SetHeight(100)
        t:Hide()
        return t
    end
    p.fxStar = fxTexture(STAR, 1, 0.9, 0.6)
    p.fxRing = fxTexture(RING, 0.9, 0.75, 1)
    local sp = boardPiece("sparkle_gold")
    if sp then
        p.fxSparkle = fxTexture(sp.path, 1, 0.9, 0.6)
        p.fxSparkle:SetTexCoord(0, sp.right, 0, sp.bottom)
    end

    PU.ApplyStretch()
    p:SetScript("OnUpdate", function()
        PU.Step(arg1)
    end)
    p:SetScript("OnHide", function()
        PU.CancelAnim()
    end)
    return p
end

-- /hs packs, /hs open: the collection window on the Packs tab.
function H.ShowPacks()
    local UI = H.CollectionUI
    if not UI or not UI.EnsureBuilt or not UI.EnsureBuilt() then
        return false
    end
    UI.state.bottom = 2
    if not UI.frame:IsShown() then
        UI.frame:Show()
    end
    UI.SetBottomTab(2)
    return true
end
