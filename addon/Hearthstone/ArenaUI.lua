-- Hearthstone addon: the draft arena window.
--
-- A window of its own (not a tab of the collection book, which is built around the owned cards): /hs arena opens it,
-- and so does the server push ARENA_OPEN when the player chooses "Continue drafting" / "Look at my deck" at Ezra /
-- Ibelin or starts a run. It is the arena screen of the original game, drawn on the owner's background
-- (Textures\UI\ArenaBg_*, tools/art/arena_ui.py):
--
--   the top plate        the name of the mode
--   the parchment        a heading on a plate, then one of
--                          classes   "Choose your Hero": three heroes in golden arches (portrait, hero name, class);
--                                    a click opens the hero as in the original: the window dims, the arch grows large
--                                    in the middle with the glowing Done plate over its foot (sends ARENA_CLASS), the
--                                    hero power card stands on the right (the large card of the duel's power hover)
--                                    and Back (a click beside the arch does the same) returns to the three heroes
--                          cards     "Choose a Card": three cards (CardWidget, hover shows the large card); a click
--                                    sends ARENA_PICK
--                          specials  the special card choice of a ten-win run (the cards of the missing sets or, when
--                                    every set is complete, the four reward cards for one extra pack); ARENA_CLAIM
--                          info      the state of the run: its text, the wins (on the key emblem) and the losses box,
--                                    the one loss that ends the run
--                        and the message line at its foot
--   bottom left          the drafted hero in the background's golden arch, the Retire plate under it
--   bottom middle        the mana curve in the background's eight bar slots (0 .. 7+)
--   bottom right         the Play button over the background's round seal (next duel, or resume the paused one)
--   right                the picked cards (gem, name, art strip, copies; sorted by cost), "n / 30" under them and
--                        the Back button (closes the window; Escape does too)
--
-- Play and Retire only work at a host: the server sends ARENA_OPEN from the host's gossip (AR.atHost) and ARENA_CLOSE
-- when the player walks away (the window closes then). Elsewhere they are greyed out and say where to go; the server
-- checks the host again. Retire asks first. Picks can be made anywhere.
--
-- Everything shown comes from Arena.lua (HsArena); AU.Lines() computes the texts without a frame (the tests use it).
-- Positions are pixels of the background image (AU.ART.bg: srcW x srcH), scaled by AU.K to the window.
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local AR = HsArena
local CM = HsCollection
H.ArenaUI = H.ArenaUI or {}
local AU = H.ArenaUI

AU.TEXTURE_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\UI\\"
AU.ART = {   -- printed by tools/art/arena_ui.py
    bg = { cols = 3, rows = 2, w = 1536, h = 829, tile = 512, srcW = 1672, srcH = 902 },
    ArenaPlay = { right = 0.99609, bottom = 1.00000, aspect = 0.99654 },
    ArenaRetire = { right = 1.00000, bottom = 0.64453, aspect = 3.10233 },
    ArenaKey = { right = 0.96875, bottom = 1.00000, aspect = 1.93689 },
    ArenaLosses = { right = 1.00000, bottom = 0.72266, aspect = 2.77143 },
    ArenaLossesHit = { right = 1.00000, bottom = 0.72266, aspect = 2.77143 },
    ArenaPlate = { right = 1.00000, bottom = 0.60547, aspect = 3.30858 },
    ArenaArch = {
        right = 0.89844, bottom = 1.00000, aspect = 0.90000,
        frame = { 266, 608, 473, 838 },   -- the arch in background source pixels (left, top, right, bottom)
        opening = { 0.08696, 0.10870, 0.87440, 0.92609 },
        strips = {
            { 0.2822, -0.0160, 0.7178, 0.0160 },
            { 0.2577, 0.0160, 0.7423, 0.0319 },
            { 0.2270, 0.0319, 0.7669, 0.0479 },
            { 0.2086, 0.0479, 0.7914, 0.0638 },
            { 0.1840, 0.0638, 0.8160, 0.0798 },
            { 0.1595, 0.0798, 0.8344, 0.0957 },
            { 0.1411, 0.0957, 0.8528, 0.1117 },
            { 0.1227, 0.1117, 0.8712, 0.1277 },
            { 0.1043, 0.1277, 0.8896, 0.1436 },
            { 0.0920, 0.1436, 0.9080, 0.1596 },
            { 0.0736, 0.1596, 0.9202, 0.1755 },
            { 0.0613, 0.1755, 0.9325, 0.1915 },
            { 0.0491, 0.1915, 0.9448, 0.2074 },
            { 0.0368, 0.2074, 0.9571, 0.2234 },
            { 0.0245, 0.2234, 0.9693, 0.2394 },
            { 0.0123, 0.2394, 0.9755, 0.2553 },
            { 0.0061, 0.2553, 0.9877, 0.2713 },
            { -0.0061, 0.2713, 0.9939, 0.2872 },
            { -0.0123, 0.2872, 1.0061, 0.3032 },
            { -0.0184, 0.3032, 1.0123, 0.3191 },
            { -0.0184, 0.3191, 1.0184, 1.0160 },
        },
    },
}

-- layout: the window is the background at AU.K; every rectangle below is in background pixels (x1, y1, x2, y2)
AU.W = 1104
AU.K = AU.W / AU.ART.bg.srcW
AU.H = math.floor(AU.ART.bg.srcH * AU.K + 0.5)
AU.TITLE = { 545, 10, 810, 42 }           -- the empty top plate
AU.PARCH = { 300, 104, 1060, 548 }        -- inside the parchment's border
AU.BANNER_Y = 112                         -- the (hidden) heading text: its top
AU.CONTENT_Y = 196                        -- the choices start here
AU.CARD_W, AU.CARD_GAP = 212, 28          -- a card of the offer (large enough to read the card text)
AU.SPECIAL_W, AU.SPECIAL_GAP = 140, 24    -- a card of the special choice (four fit in a row)
AU.SPECIAL_Y = 256                        -- under the text of the settled run
AU.HERO_H, AU.HERO_GAP = 220, 52          -- a golden arch of the class choice; its name and class under it
AU.VIEW = { cx = 836, y = 96, h = 540 }   -- the arch of the chosen hero (background pixels: centre x, top, height)
AU.VIEW_POWER = { 1300, 510, 300 }        -- the hero power card beside it: centre x, centre y and width
AU.VIEW_DONE = { 836, 600 }               -- the centre of the Done plate, over the foot of the arch
AU.VIEW_DONE_SIZE = { 160, 80 }           -- the Done plate in window pixels (as the duel's mulligan and result)
AU.VIEW_NAME_Y = 696                      -- the hero's name under the plate, its class under that
AU.VIEW_BACK = { 836, 788, 150 }          -- centre x, centre y and width of the Back plate: under the name and class
AU.VIEW_DIM = 0.7                         -- the black over the window while a hero is open (the original blurs it)
AU.MESSAGE = { 320, 522, 1040, 548 }
AU.ARCH = AU.ART.ArenaArch.frame          -- the drafted hero (the background's own arch)
AU.RETIRE = { 376, 860, 160 }             -- centre x, centre y and width of the Retire plate
AU.BARS = { x = 563, pitch = 29.45, w = 22, top = 640, bottom = 791 }
AU.PLAY = { 962, 719, 232 }               -- centre x, centre y, diameter of the Play button
AU.DECK = { 1186, 74, 1390, 838 }         -- the purple list
AU.COUNT = { 1182, 856, 1298, 894 }       -- the small purple plate under it: "n / 30"
AU.BACK = { 1382, 875, 148 }              -- centre x, centre y and width of the Back plate
AU.KEY_MEDAL = { 0.501, 0.686 }           -- the centre of the medallion in the emblem (share of its width, height)
AU.RUN_BARE = { key = 520, loss = 220, font = 40 }      -- the run on its own (a run that can be played): centred, large
AU.RUN_TEXT = { key = 300, loss = 130, font = 26 }      -- under the text of the run (a finished or running one)
AU.KEY_DIM = 0.8                          -- the key emblem is dimmed a little (a share of its colour)
AU.INK = { 0.27, 0.15, 0.06 }             -- text on parchment
AU.CLICK_GRACE = 0.5              -- seconds a new card offer ignores clicks (a double click must not pick from it)
-- the portrait window of a hero card (256 x 388 render): the board's arch (MatchUI.lua MU.ARCH cx, cy, zoom),
-- so the portrait looks the same in the match and in the picker; zoom is the window's height as a share of the image
AU.HERO_CROP = { cx = 0.5, cy = 0.315, zoom = 0.255 }
local WHITE = "Interface\\Buttons\\WHITE8X8"
local DIALOG_BG = "Interface\\DialogFrame\\UI-DialogBox-Background"
local HEAD_FONT = "Fonts\\MORPHEUS.TTF"
local BODY_FONT = "Fonts\\FRIZQT__.TTF"

AU.frame = nil
AU.slots = {}                     -- the three cards of the offer
AU.picks = {}                     -- the four cards of the special choice
AU.classes = {}                   -- the three hero buttons
AU.chosen = nil                   -- the class clicked in the hero choice (its view is open until Choose or Back)
AU.heroView = nil                 -- the view of that hero: its arch, name, class, hero power, Choose and Back
AU.offerKey = nil                 -- the card offer on screen ("run:no") and when it appeared (GetTime)
AU.offerShownAt = nil
AU.hover = nil                    -- the offer / special card under the mouse: { kind, index }
AU.rows = {}
AU.bars = {}
AU.reopen = false                 -- the window was open when a duel started: it comes back after the match

-- ------------------------------------------------------------- texts
local function plural(n, one, many)
    if n == 1 then
        return n .. " " .. one
    end
    return n .. " " .. many
end

-- 1..5: the line of the prize table that the wins on screen fall into.
local function tierOf(wins)
    if wins <= 0 then
        return 1
    elseif wins <= 2 then
        return 2
    elseif wins <= 5 then
        return 3
    elseif wins < AR.WIN_TARGET then
        return 4
    end
    return 5
end

-- The name of the hero of a class (the hero card of the collection data), or the class name.
function AU.HeroName(class)
    local DU = H.DecksUI
    local id = DU and DU.HeroCard and DU.HeroCard(class)
    local db = CM.GetDB and CM.GetDB()
    local c = id and db and db.cards[id]
    return (c and c.name) or AR.ClassLabel(class)
end

-- The hero power of a class's hero card: { id (the power card), name, text (its own "Hero Power" line first), cost },
-- or nil.
function AU.HeroPower(class)
    local DU = H.DecksUI
    local id = DU and DU.HeroCard and DU.HeroCard(class)
    local db = CM.GetDB and CM.GetDB()
    local hero = id and db and db.cards[id]
    local p = hero and hero.power and db.cards[hero.power]
    if not p then
        return nil
    end
    return { id = hero.power, name = p.name or "", text = p.text or "", cost = p.cost or 0 }
end

-- Everything the window shows, without a frame: { mode ("info" | "classes" | "cards" | "specials"), heading, body,
-- offer (ids or class names), labels (their names), heroes (the hero names of a class offer), chosen (the class of the
-- hero view, with chosenName and power = { id, name, text, cost } from AU.HeroPower), choices (special cards:
-- { id, n, complete, caption }), specialNote, message, busy, hasRun, progress, wins, losses, prizeLine, tableLines,
-- tier, hint, deckTitle, rows, curve, curveMax, canPlay, playEnabled, playTip, canRetire, retireEnabled, retireTip,
-- hero (the drafted class or "") }.
function AU.Lines()
    local t = { mode = "info", offer = {}, labels = {}, heroes = {}, choices = {}, rows = {}, tableLines = {}, tier = 0,
        wins = 0, losses = 0, hasRun = false, progress = "", prizeLine = "", hint = "", specialNote = "", curveMax = 0,
        hero = "" }
    local busy = AR.IsBusy()
    t.busy = busy
    local m = AR.run
    if AR.state ~= "ready" then
        t.heading = AR.NAME
        if AR.state == "loading" then
            t.body = "Loading your run..."
        elseif AR.state == "unsupported" then
            t.body = "This server does not support the " .. AR.NAME .. " yet."
        elseif AR.state == "error" then
            t.body = "Your run could not be loaded (" .. tostring(AR.errorText or "?") .. "). Open this window again to retry."
        else
            t.body = "Not connected to the server."
        end
        m = AR.Empty()
    else
        t.heading, t.body = AR.StateInfo(m)
        if m.st == "class" and m.no == 0 and table.getn(m.offer) == AR.OFFER_SIZE then
            t.mode = "classes"
            t.heading = "Choose your Hero"
        elseif m.st == "draft" and m.no ~= nil and m.no >= 1 and table.getn(m.offer) == AR.OFFER_SIZE then
            t.mode = "cards"
        elseif m.st == "settled" and m.specialPending then
            t.mode = "specials"
        end
        -- at the host the buttons do what the text would send the player away for
        if AR.atHost and m.st == "ready" then
            t.body = "Press Play to start your next duel. Win " .. AR.WIN_TARGET .. " duels for the full prize; one loss ends the run."
        elseif AR.atHost and m.st == "paused" then
            t.body = "Press Play to resume your duel where you left it."
        end
    end
    if t.mode == "classes" then
        for i = 1, AR.OFFER_SIZE do
            t.offer[i] = m.offer[i]
            t.labels[i] = AR.ClassLabel(m.offer[i])
            t.heroes[i] = AU.HeroName(m.offer[i])
            if AU.chosen == m.offer[i] then
                t.chosen = AU.chosen
                t.chosenName = t.heroes[i]
                t.power = AU.HeroPower(AU.chosen)
            end
        end
    elseif t.mode == "cards" then
        for i = 1, AR.OFFER_SIZE do
            t.offer[i] = m.offer[i]
            t.labels[i] = AR.CardName(m.offer[i])
        end
    elseif t.mode == "specials" then
        t.choices = AR.SpecialChoices(m)
        for i = 1, table.getn(t.choices) do
            local c = t.choices[i]
            if c.complete then
                c.caption = "1 extra pack"
            else
                c.caption = "+" .. plural(c.n, "copy", "copies")
            end
        end
        if table.getn(m.specials) == 0 then
            t.specialNote = "Every special card set is complete, so any card you choose gives you one extra pack instead."
        end
    end
    if busy then
        t.message = "Sending your choice..."
    else
        t.message = AR.message or ""
    end
    -- the run
    if m.run > 0 then
        t.hasRun = true
        t.hero = m.cls or ""
        t.wins, t.losses = m.wins, m.losses
        t.progress = "Wins " .. m.wins .. " / " .. AR.WIN_TARGET
        if m.draws > 0 then
            t.progress = t.progress .. " (" .. plural(m.draws, "draw", "draws") .. " count as wins)"
        end
        t.progress = t.progress .. "      Losses " .. m.losses .. " / 1 (a loss ends the run)"
        local ended = m.st == "finished" or m.st == "retired" or m.st == "settled"
        t.prizeLine = (ended and "Prize: " or "If the run ended now: ") .. AR.PrizeText(m.prize)
        local lines = AR.PrizeTable(m)
        for i = 1, table.getn(lines) do
            t.tableLines[i] = lines[i][1] .. ": " .. lines[i][2]
        end
        t.tier = tierOf(m.wins)
        if AR.CanRetire(m) and not AR.atHost then
            t.hint = "Play and Retire work at " .. AR.HOSTS .. ". A run can only be retired there; it pays the prize above."
        end
    end
    -- the buttons
    t.canPlay = AR.state == "ready" and AR.CanPlay(m)
    t.playEnabled = t.canPlay and AR.atHost and not busy
    if t.canPlay and not AR.atHost then
        t.playTip = AR.NOT_AT_HOST
    elseif m.st == "paused" then
        t.playTip = "Resume your paused duel."
    else
        t.playTip = "Start your next duel."
    end
    t.canRetire = AR.state == "ready" and AR.CanRetire(m)
    t.retireEnabled = t.canRetire and AR.atHost and not busy
    if t.canRetire and not AR.atHost then
        t.retireTip = AR.NOT_AT_HOST_RETIRE
    else
        t.retireTip = "End this run now and take the prize for " .. plural(m.wins, "win", "wins") .. "."
    end
    -- the picked cards
    t.deckTitle = AR.DeckCount(m) .. " / " .. AR.PICKS
    local rows = AR.DeckRows(m)
    local lastCost = nil
    for i = 1, table.getn(rows) do
        local r = rows[i]
        local code = HsPacks.RARITY_CODES[r.rarity] or "|cffffffff"
        local row = { id = r.id, cost = r.cost, n = r.n, rarity = r.rarity }
        row.costText = (r.cost ~= lastCost) and tostring(r.cost) or ""
        row.nameText = code .. r.name .. "|r"
        row.countText = r.n > 1 and tostring(r.n) or ""
        lastCost = r.cost
        t.rows[i] = row
    end
    t.curve = AR.Curve(m)
    for i = 0, CM.MANA_MAX do
        if t.curve[i] > t.curveMax then
            t.curveMax = t.curve[i]
        end
    end
    return t
end

-- ------------------------------------------------------------- helpers
local function S(v)
    return math.floor(v * AU.K + 0.5)
end

-- Puts region r on the background rectangle (x1, y1, x2, y2) of the window f.
local function place(r, f, x1, y1, x2, y2)
    r:ClearAllPoints()
    r:SetPoint("TOPLEFT", f, "TOPLEFT", S(x1), -S(y1))
    r:SetWidth(S(x2) - S(x1))
    r:SetHeight(S(y2) - S(y1))
end

-- Puts region r centred on the background point (cx, cy) with the size w x h (background pixels).
local function placeCentered(r, f, cx, cy, w, h)
    place(r, f, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
end

local function setFont(fs, file, size, flags)
    if fs.SetFont then
        fs:SetFont(file, size, flags)
    end
end

local function newText(parent, font, justify, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", font or "GameFontNormal")
    fs:SetJustifyH(justify or "LEFT")
    return fs
end

-- Text written on the parchment: dark ink, no shadow.
local function inkText(parent, file, size, justify)
    local fs = newText(parent, "GameFontNormal", justify)
    setFont(fs, file, size)
    fs:SetTextColor(AU.INK[1], AU.INK[2], AU.INK[3])
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    return fs
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

local function showTip(owner, text)
    if GameTooltip and text and text ~= "" then
        GameTooltip:SetOwner(owner, "ANCHOR_TOP")
        GameTooltip:SetText(text, 1, 1, 1, 1, 1)
        GameTooltip:Show()
    end
end

-- A texture of AU.ART on region tex. False when the file is not installed (1.12 SetTexture answers nil).
local function setArt(tex, name)
    local a = AU.ART[name]
    if not tex:SetTexture(AU.TEXTURE_PATH .. name) then
        return false
    end
    tex:SetTexCoord(0, a.right, 0, a.bottom)
    return true
end

local function setDimmed(tex, dimmed)
    if tex.SetDesaturated then
        tex:SetDesaturated(dimmed and 1 or nil)
    end
    if dimmed then
        tex:SetVertexColor(0.55, 0.55, 0.55)
    else
        tex:SetVertexColor(1, 1, 1)
    end
end

-- A button dims while its answer is on the way: the Done plate (its art is its normal texture) fades, the plates of
-- AU.ART grey out.
local function dimButton(b, dimmed)
    if b.tex then
        setDimmed(b.tex, dimmed)
    else
        b:SetAlpha(dimmed and 0.5 or 1)
    end
end

-- ------------------------------------------------------------ large card
-- The hovered card, large: over the deck list for a card of the parchment, left of the list for a row of it.
function AU.BuildPreview()
    if AU.preview then
        return AU.preview
    end
    local f = CreateFrame("Frame", "HearthstoneArenaPreview", UIParent)
    f:SetWidth(262)
    f:SetHeight(396)
    f:SetFrameStrata("TOOLTIP")
    f:SetClampedToScreen(true)
    local widget = H.CardWidget.Create(f, "HearthstoneArenaPreviewCard", 256, true)
    widget.frame:SetPoint("CENTER", f, "CENTER", 0, 0)
    f:Hide()
    AU.preview = { frame = f, widget = widget }
    return AU.preview
end

function AU.PlacePreview(kind)
    local pv, main = AU.preview, AU.frame
    if not pv or not main then
        return
    end
    pv.frame:ClearAllPoints()
    pv.frame:SetScale(main:GetScale() or 1)
    if kind == "Row" then
        pv.frame:SetPoint("RIGHT", main, "TOPLEFT", S(AU.DECK[1]) - 6, -S((AU.DECK[2] + AU.DECK[4]) / 2))
    else
        pv.frame:SetPoint("CENTER", main, "TOPLEFT", S((AU.DECK[1] + AU.DECK[3]) / 2), -S((AU.DECK[2] + AU.DECK[4]) / 2))
    end
end

function AU.ShowPreview(id, kind)
    if not id then
        return
    end
    local pv = AU.BuildPreview()
    H.CardWidget.SetCard(pv.widget, id)
    AU.PlacePreview(kind)
    pv.frame:Show()
end

function AU.HidePreview()
    if AU.preview then
        AU.preview.frame:Hide()
    end
end

-- ----------------------------------------------------------- the clicks
-- The step could not be sent: the window says why.
local function failed(why)
    AR.message = why
    AU.Refresh()
end

-- A click on a hero opens its view; nothing is sent until Choose.
function AU.OnClassClick(index)
    local id = AU.classes[index] and AU.classes[index].value
    if not id then
        return nil
    end
    AU.chosen = id
    AU.Refresh()
    return true
end

function AU.OnChooseClick()
    local id = AU.chosen
    if not id then
        return nil
    end
    local key, why = H.ArenaChooseClass(id)
    if not key then
        failed(why)
    end
    return key, why
end

-- Back from the hero view: the three heroes again (a refusal text goes with it).
function AU.OnBackClick()
    AU.chosen = nil
    AR.message = nil
    AU.Refresh()
end

function AU.OnCardClick(index)
    local id = AU.slots[index] and AU.slots[index].id
    if not id then
        return nil
    end
    -- the next offer can replace the cards between the two clicks of a double click (or under a click that was on
    -- its way): a card the player has not seen yet is never picked
    if AU.offerShownAt and GetTime and GetTime() - AU.offerShownAt < AU.CLICK_GRACE then
        return nil
    end
    local key, why = H.ArenaPick(id)
    if not key then
        failed(why)
    end
    return key, why
end

-- Places the special cards centred under the text: n cards take the middle places (one card is in the middle, two
-- sit side by side in the middle, and so on). The four picks are all placed; the ones beyond n are hidden anyway.
function AU.PlaceSpecials(n)
    local sw, sgap = AU.SPECIAL_W, AU.SPECIAL_GAP
    local count = math.max(1, math.min(n, table.getn(AU.picks)))
    local x0 = AU.specialCenter - (count * sw + (count - 1) * sgap) / 2
    for i = 1, table.getn(AU.picks) do
        AU.picks[i].widget.frame:SetPoint("TOPLEFT", AU.specialFrame, "TOPLEFT",
            S(x0 + (i - 1) * (sw + sgap)), -S(AU.SPECIAL_Y))
    end
end

function AU.OnSpecialClick(index)
    local id = AU.picks[index] and AU.picks[index].id
    if not id then
        return nil
    end
    local key, why = H.ArenaClaim(id)
    if not key then
        failed(why)
    end
    return key, why
end

function AU.OnPlayClick()
    local key, why = H.ArenaDuel()
    if not key then
        failed(why)
    end
    return key, why
end

-- Retire asks first (the run ends for good); a click that cannot work says why at once.
function AU.OnRetireClick()
    local why = H.ArenaBlocker() or AR.OpBlocker("retire", nil)
    if why then
        failed(why)
        return nil, why
    end
    AU.EnsurePopup()
    if StaticPopup_Show then
        StaticPopup_Show("HEARTHSTONE_ARENA_RETIRE", tostring(AR.run.wins), AR.run.wins == 1 and "win" or "wins")
    end
    return true
end

function AU.AcceptRetire()
    local key, why = H.ArenaRetire()
    if not key then
        failed(why)
    end
    return key, why
end

function AU.EnsurePopup()
    if not StaticPopupDialogs or StaticPopupDialogs["HEARTHSTONE_ARENA_RETIRE"] then
        return
    end
    StaticPopupDialogs["HEARTHSTONE_ARENA_RETIRE"] = {
        text = "Retire from the " .. AR.NAME .. "?\n\nYour run ends now with %s %s and pays the prize for that. "
            .. "The entry is not refunded.",
        button1 = "Retire",
        button2 = CANCEL or "Cancel",
        OnAccept = function()
            AU.AcceptRetire()
        end,
        showAlert = 1,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
end

-- ------------------------------------------------------------- refresh
local function setEnabled(b, enabled)
    if enabled then
        b:Enable()
    else
        b:Disable()
    end
end

local function showIf(r, on)
    if on then
        r:Show()
    else
        r:Hide()
    end
end

-- Places the run's pieces: the key emblem (the wins on its medallion), the losses box under it and the hint.
-- bare: the run is all there is on the parchment, so it is large and centred in it; else it sits under the text.
function AU.LayoutRun(f, bare)
    local L = bare and AU.RUN_BARE or AU.RUN_TEXT
    local pcx = (AU.PARCH[1] + AU.PARCH[3]) / 2
    local kh = L.key / AU.ART.ArenaKey.aspect
    local lh = L.loss / AU.ART.ArenaLosses.aspect
    local gap = bare and 18 or 14
    local ky = AU.CONTENT_Y + 34
    if bare then
        ky = (AU.PARCH[2] + AU.PARCH[4]) / 2 - (kh + gap + lh) / 2 + 6
    end
    place(f.key, f, pcx - L.key / 2, ky, pcx + L.key / 2, ky + kh)
    local mx, my = pcx + (AU.KEY_MEDAL[1] - 0.5) * L.key, ky + AU.KEY_MEDAL[2] * kh
    setFont(f.winsNumber, HEAD_FONT, L.font, "OUTLINE")
    place(f.winsNumber, f, mx - L.font * 1.2, my - L.font * 0.65, mx + L.font * 1.2, my + L.font * 0.65)
    local ty = ky + kh + gap
    place(f.losses, f, pcx - L.loss / 2, ty, pcx + L.loss / 2, ty + lh)
    place(f.hint, f, AU.PARCH[1] + 30, ty + lh + 8, AU.PARCH[3] - 30, AU.MESSAGE[2])
    f.runBare = bare
end

-- Puts the picture of the run into the frames.
function AU.Refresh()
    local f = AU.frame
    if not f or not f:IsShown() then
        return
    end
    local t = AU.Lines()
    local info = t.mode == "info"
    f.heading:SetText(t.heading)
    -- a run that can be played shows only the key emblem and the losses box (no text of its own)
    local bare = info and t.hasRun and t.canPlay
    f.body:SetText(((info and not bare) or t.mode == "specials") and t.body or "")
    if f.runBare ~= bare then
        AU.LayoutRun(f, bare)
    end
    f.message:SetText(t.message)
    if AR.state == "ready" and t.message ~= "" and not t.busy then
        f.message:SetTextColor(0.6, 0.1, 0.05)
    else
        f.message:SetTextColor(AU.INK[1], AU.INK[2], AU.INK[3])
    end
    local sending = t.busy
    local pending = AR.op and AR.op.arg
    -- the hero buttons (they stay under the dimmed window while a hero is open)
    if not t.chosen then
        AU.chosen = nil
    end
    for i = 1, AR.OFFER_SIZE do
        local b = AU.classes[i]
        if t.mode == "classes" then
            b.value = t.offer[i]
            b.name:SetText(t.labels[i])
            b.hero:SetText(t.heroes[i])
            AU.SetHeroArt(b.portrait, t.offer[i], HsHeroes and HsHeroes.PortraitFor(t.offer[i]))
            b:Show()
            setEnabled(b, not sending)
            if sending and pending == t.offer[i] then
                b.pick:Show()
            else
                b.pick:Hide()
            end
        else
            b.value = nil
            b:Hide()
        end
    end
    -- the hero view: the dimmed window, the arch, name and class of the chosen hero, its hero power card, Done and Back
    local hv = AU.heroView
    local viewing = t.chosen ~= nil
    local power = viewing and t.power
    if viewing then
        AU.SetHeroArt(hv.hero, t.chosen, HsHeroes and HsHeroes.PortraitFor(t.chosen))
        hv.name:SetText(t.chosenName)
        hv.cls:SetText(AR.ClassLabel(t.chosen))
        setEnabled(hv.choose, not sending)
        setEnabled(hv.back, not sending)
        dimButton(hv.choose, sending)
    else
        AU.HideHero(hv.hero)
    end
    H.CardWidget.SetCard(hv.power, power and power.id or nil)
    showIf(hv.dim, viewing)
    showIf(hv.host, viewing)
    showIf(hv.name, viewing)
    showIf(hv.cls, viewing)
    showIf(hv.choose, viewing)
    showIf(hv.back, viewing)
    showIf(hv.power.frame, power)
    -- the cards of the offer (a new offer starts the click grace)
    local offerKey = nil
    if t.mode == "cards" then
        offerKey = tostring(AR.run.run) .. ":" .. tostring(AR.run.no)
    end
    if offerKey ~= AU.offerKey then
        AU.offerKey = offerKey
        AU.offerShownAt = offerKey and GetTime and GetTime() or nil
    end
    for i = 1, AR.OFFER_SIZE do
        local s = AU.slots[i]
        if t.mode == "cards" then
            s.id = t.offer[i]
            H.CardWidget.SetCard(s.widget, s.id)
            H.CardWidget.SetDimmed(s.widget, sending and pending ~= s.id)
            s.widget.frame:Show()
            s.button:Show()
            setEnabled(s.button, not sending)
            if sending and pending == s.id then
                s.sel:Show()
            else
                s.sel:Hide()
            end
        else
            s.id = nil
            s.widget.frame:Hide()
            s.button:Hide()
        end
    end
    -- the special cards (centred for however many are left)
    if t.mode == "specials" then
        AU.PlaceSpecials(table.getn(t.choices))
    end
    for i = 1, table.getn(AU.picks) do
        local s = AU.picks[i]
        local c = t.choices[i]
        if t.mode == "specials" and c then
            s.id = c.id
            H.CardWidget.SetCard(s.widget, c.id)
            H.CardWidget.SetDimmed(s.widget, sending and pending ~= c.id)
            s.widget.frame:Show()
            s.button:Show()
            setEnabled(s.button, not sending)
            s.caption:SetText(c.caption)
            s.caption:Show()
            if sending and pending == c.id then
                s.sel:Show()
            else
                s.sel:Hide()
            end
        else
            s.id = nil
            s.widget.frame:Hide()
            s.button:Hide()
            s.caption:Hide()
        end
    end
    -- the large card follows the card under the mouse (the offer changes under a mouse that does not move)
    if AU.hover and AU.preview and AU.preview.frame:IsShown() then
        local id = AU.CardOf(AU.hover.kind, AU.hover.index)
        if id then
            AU.ShowPreview(id, AU.hover.kind)
        else
            AU.HidePreview()
        end
    end
    f.specialNote:SetText(t.mode == "specials" and t.specialNote or "")
    -- progress (on the parchment while there is nothing to choose)
    if t.hasRun and info then
        f.winsNumber:SetText(tostring(t.wins))
        f.winsNumber:Show()
        f.key:Show()
        if t.losses > 0 and f.lossesHitInstalled then
            setArt(f.losses, "ArenaLossesHit")
        else
            setArt(f.losses, "ArenaLosses")
        end
        f.losses:Show()
    else
        f.winsNumber:Hide()
        f.key:Hide()
        f.losses:Hide()
    end
    f.hint:SetText(info and not bare and t.hint or "")
    -- the drafted hero
    if t.hero ~= "" then
        AU.SetHeroArt(f.hero, t.hero, HsHeroes and HsHeroes.PortraitFor(t.hero))
        f.hero.class = t.hero
        f.hero.button:Show()
    else
        AU.HideHero(f.hero)
        f.hero.class = nil
        f.hero.button:Hide()
    end
    -- Play and Retire
    f.play.tip = t.playTip
    if t.canPlay then
        setDimmed(f.play.tex, not t.playEnabled)
        f.play:Show()
    else
        f.play:Hide()
    end
    f.retire.tip = t.retireTip
    if t.canRetire then
        setDimmed(f.retire.tex, not t.retireEnabled)
        f.retire:Show()
    else
        f.retire:Hide()
    end
    -- the picked list and the mana curve
    f.deckTitle:SetText(t.deckTitle)
    for i = 1, table.getn(AU.rows) do
        local r, row = AU.rows[i], t.rows[i]
        if row then
            r.id = row.id
            r.cost:SetText(tostring(row.cost))
            r.name:SetText(row.nameText)
            r.count:SetText(row.countText)
            if row.rarity == "LEGENDARY" then
                r.star:Show()
            else
                r.star:Hide()
            end
            if row.countText ~= "" or row.rarity == "LEGENDARY" then
                r.box:Show()
            else
                r.box:Hide()
            end
            AU.SetRowArt(r.art, row.id)
            r:Show()
        else
            r.id = nil
            r:Hide()
        end
    end
    if table.getn(t.rows) == 0 then
        f.deckEmpty:Show()
    else
        f.deckEmpty:Hide()
    end
    local b = AU.BARS
    for i = 0, CM.MANA_MAX do
        local bar = AU.bars[i]
        local n = t.curve[i]
        if n > 0 then
            bar.tex:SetHeight(1 + math.floor(n * (S(b.bottom - b.top) - 1) / t.curveMax))
            bar.tex:SetAlpha(1)
            bar.glow:SetAlpha(0.6)
        else
            bar.tex:SetHeight(1)
            bar.tex:SetAlpha(0)
            bar.glow:SetAlpha(0)
        end
    end
end

-- A hero portrait (the strips of an arch, AU.MakeHero) for a class: the hero card's art window, or the class colour
-- without the card texture. portraitId (optional) is an alternative portrait of the class (HsHeroes): its
-- texture replaces the class default's; one whose texture is not installed shows the class colour.
function AU.SetHeroArt(hero, class, portraitId)
    local DU = H.DecksUI
    local defaultId = DU and DU.HeroCard and DU.HeroCard(class)
    local cardId = defaultId
    local altPath, altBottom
    if portraitId and portraitId ~= defaultId then
        cardId = portraitId
        if HsHeroes then
            altPath, altBottom = HsHeroes.Texture(portraitId)
        end
    end
    local a = AU.ART.ArenaArch
    local strips = a.strips
    local x0, y0, x1, y1 = strips[1][1], strips[1][2], strips[1][3], strips[1][4]
    for i = 2, table.getn(strips) do
        local st = strips[i]
        if st[1] < x0 then x0 = st[1] end
        if st[2] < y0 then y0 = st[2] end
        if st[3] > x1 then x1 = st[3] end
        if st[4] > y1 then y1 = st[4] end
    end
    local has = cardId and (altPath ~= nil or (cardId == defaultId and CM.HasTexture(cardId)))
    local u1, u2, v1, v2 = 0, 1, 0, 1
    if has then
        local c = AU.HERO_CROP
        local bottom = 388 / 512
        if altBottom then
            bottom = altBottom
        elseif H.CardBottom then
            bottom = H.CardBottom(cardId)
        end
        local o = a.opening
        local boxAspect = (x1 - x0) * (o[3] - o[1]) * a.aspect / ((y1 - y0) * (o[4] - o[2]))
        -- the picture has the box's aspect (as MatchUI's SetHeroArt), so the face is not squeezed
        local vh = c.zoom
        local uw = vh * 388 * boxAspect / 256
        u1, u2 = c.cx - uw / 2, c.cx + uw / 2
        v1, v2 = (c.cy - vh / 2) * bottom, (c.cy + vh / 2) * bottom
    end
    local col = { 0.3, 0.3, 0.35 }
    if H.MatchUI and H.MatchUI.ClassColor and defaultId then
        col = H.MatchUI.ClassColor(defaultId) or col
    end
    local path = altPath or (H.CARD_TEXTURE_PATH or "Interface\\AddOns\\Hearthstone\\Textures\\Cards\\") .. tostring(cardId)
    for i = 1, table.getn(hero.strips) do
        local tex = hero.strips[i]
        local st = strips[i]
        if has then
            tex:SetTexture(path)
            tex:SetTexCoord(u1 + (u2 - u1) * (st[1] - x0) / (x1 - x0), u1 + (u2 - u1) * (st[3] - x0) / (x1 - x0),
                v1 + (v2 - v1) * (st[2] - y0) / (y1 - y0), v1 + (v2 - v1) * (st[4] - y0) / (y1 - y0))
            tex:SetVertexColor(1, 1, 1)
        else
            tex:SetTexture(WHITE)
            tex:SetTexCoord(0, 1, 0, 1)
            tex:SetVertexColor(col[1], col[2], col[3])
        end
        tex:Show()
    end
    hero.frame:Show()
end

function AU.HideHero(hero)
    for i = 1, table.getn(hero.strips) do
        hero.strips[i]:Hide()
    end
    hero.frame:Hide()
end

-- The art strip of a row of the picked list (DecksUI's crop of the card), or the class colour.
function AU.SetRowArt(tex, cardId)
    local DU = H.DecksUI
    if DU and DU.SetArt then
        DU.SetArt(tex, cardId, DU.ROW_CROP)
    else
        tex:SetTexture(WHITE)
        tex:SetVertexColor(0.2, 0.15, 0.3)
    end
end

-- ------------------------------------------------------------- the frame
local function makeCard(parent, kind, index, width)
    local w = H.CardWidget.Create(parent, "HearthstoneArena" .. kind .. index, width, false)
    local b = CreateFrame("Button", "HearthstoneArena" .. kind .. "Button" .. index, parent)
    -- the button covers the visible card only: the hover and selection highlights stay on the card, not its margin
    local vl, vt, vr, vb = H.CardWidget.VisibleRect(width)
    b:SetPoint("TOPLEFT", w.frame, "TOPLEFT", vl, -vt)
    b:SetPoint("BOTTOMRIGHT", w.frame, "TOPLEFT", vr, -vb)
    b:SetFrameLevel(w.frame:GetFrameLevel() + 2)
    -- the collection's glows around the visible card: blue on hover, gold for the pick on its way to the server
    local UI = H.CollectionUI
    local blue = w.frame:CreateTexture(nil, "BACKGROUND")
    local gold = w.frame:CreateTexture(nil, "BACKGROUND")
    local glows = UI and UI.PlaceGlow and UI.PlaceGlow(blue, "glow_blue", w.frame, vl, vt, vr, vb)
        and UI.PlaceGlow(gold, "glow_gold", w.frame, vl, vt, vr, vb)
    local hover, sel
    if glows then
        blue:Hide()
        gold:Hide()
        hover, sel = blue, gold
    else
        -- the glow pieces are not deployed: a plain highlight on the visible card instead
        b:SetHighlightTexture(WHITE, "ADD")
        b:GetHighlightTexture():SetAlpha(0.12)
        sel = b:CreateTexture(nil, "OVERLAY")
        sel:SetTexture(WHITE)
        sel:SetVertexColor(1, 0.82, 0.2)
        sel:SetAlpha(0.25)
        sel:SetAllPoints(b)
        sel:Hide()
    end
    b:SetScript("OnEnter", function()
        AU.hover = { kind = kind, index = index }
        if hover then
            hover:Show()
        end
        AU.ShowPreview(AU.CardOf(kind, index), kind)
    end)
    b:SetScript("OnLeave", function()
        AU.hover = nil
        if hover then
            hover:Hide()
        end
        AU.HidePreview()
    end)
    return { widget = w, button = b, sel = sel }
end

-- The card id of slot 'index' of the offer ("Offer") or of the special choice ("Special").
function AU.CardOf(kind, index)
    local list = kind == "Offer" and AU.slots or AU.picks
    return list[index] and list[index].id
end

-- A hero portrait in a golden arch of height h (background pixels) at the background point (x, y) (top left), on
-- frame f, drawn on 'host' (a frame whose level decides what is on top): { frame (the arch texture), strips }.
function AU.MakeHero(host, f, x, y, h)
    local a = AU.ART.ArenaArch
    local w = h * a.aspect
    local hero = { strips = {} }
    local o = a.opening
    local ax, ay = x + w * o[1], y + h * o[2]
    local aw, ah = w * (o[3] - o[1]), h * (o[4] - o[2])
    for i = 1, table.getn(a.strips) do
        local st = a.strips[i]
        local tex = host:CreateTexture(nil, "BORDER")
        tex:SetTexture(WHITE)
        tex:SetPoint("TOPLEFT", f, "TOPLEFT", S(ax + aw * st[1]), -S(ay + ah * st[2]))
        tex:SetPoint("BOTTOMRIGHT", f, "TOPLEFT", S(ax + aw * st[3]), -S(ay + ah * st[4]))
        tex:Hide()
        hero.strips[i] = tex
    end
    local frame = host:CreateTexture(nil, "ARTWORK")
    if not setArt(frame, "ArenaArch") then
        frame:SetTexture(nil)
    end
    place(frame, f, x, y, x + w, y + h)
    frame:Hide()
    hero.frame = frame
    return hero
end

-- An arch-shaped additive glow over the golden arch at (x, y), h high: a tint in the arch's opening (the strips of
-- MakeHero, so it follows the round top) and a brighter copy of the golden frame. 'host' makes the textures in the
-- given layer; they are returned for hiding and showing.
function AU.ArchGlow(host, f, layer, x, y, h, r, g, b, openAlpha, frameAlpha)
    local a = AU.ART.ArenaArch
    local w = h * a.aspect
    local o = a.opening
    local ax, ay = x + w * o[1], y + h * o[2]
    local aw, ah = w * (o[3] - o[1]), h * (o[4] - o[2])
    local parts = {}
    for i = 1, table.getn(a.strips) do
        local st = a.strips[i]
        local tex = host:CreateTexture(nil, layer)
        tex:SetTexture(WHITE)
        tex:SetVertexColor(r, g, b)
        tex:SetBlendMode("ADD")
        tex:SetAlpha(openAlpha)
        tex:SetPoint("TOPLEFT", f, "TOPLEFT", S(ax + aw * st[1]), -S(ay + ah * st[2]))
        tex:SetPoint("BOTTOMRIGHT", f, "TOPLEFT", S(ax + aw * st[3]), -S(ay + ah * st[4]))
        table.insert(parts, tex)
    end
    local frame = host:CreateTexture(nil, layer)
    if setArt(frame, "ArenaArch") then
        frame:SetBlendMode("ADD")
        frame:SetVertexColor(r, g, b)
        frame:SetAlpha(frameAlpha)
        place(frame, f, x, y, x + w, y + h)
        table.insert(parts, frame)
    else
        frame:SetTexture(nil)
    end
    return parts
end

-- A button with an AU.ART texture, centred on the background point (cx, cy), w wide (its height from the art):
-- the texture, a brighter copy on hover, the tooltip b.tip. 'parent': the frame it is a child of (default f).
local function artButton(name, f, art, cx, cy, w, onClick, parent)
    local a = AU.ART[art]
    local h = w / a.aspect
    local b = CreateFrame("Button", name, parent or f)
    placeCentered(b, f, cx, cy, w, h)
    b.tex = b:CreateTexture(nil, "ARTWORK")
    b.tex:SetAllPoints(b)
    b.installed = setArt(b.tex, art)
    local hl = b:CreateTexture(nil, "HIGHLIGHT")
    hl:SetAllPoints(b)
    if b.installed then
        setArt(hl, art)
        hl:SetBlendMode("ADD")
        hl:SetAlpha(0.3)
    else
        b.tex:SetTexture(WHITE)
        b.tex:SetVertexColor(0.3, 0.25, 0.15)
        hl:SetTexture(WHITE)
        hl:SetAlpha(0.12)
    end
    b:SetScript("OnClick", onClick)
    b:SetScript("OnEnter", function()
        showTip(b, b.tip)
    end)
    b:SetScript("OnLeave", hideTip)
    return b
end

-- The background: AU.ART.bg tiles (the dialog background when they are not installed).
local function buildBackground(f)
    local bg = AU.ART.bg
    local scale = bg.srcW / bg.w      -- background source pixels per texture pixel
    local ok = true
    f.tiles = {}
    for r = 0, bg.rows - 1 do
        for c = 0, bg.cols - 1 do
            local x, y = c * bg.tile, r * bg.tile
            local tw, th = math.min(bg.tile, bg.w - x), math.min(bg.tile, bg.h - y)
            local tex = f:CreateTexture(nil, "BACKGROUND")
            if tex:SetTexture(AU.TEXTURE_PATH .. "ArenaBg_c" .. c .. "r" .. r) then
                tex:SetTexCoord(0, tw / bg.tile, 0, th / bg.tile)
            else
                ok = false
            end
            place(tex, f, x * scale, y * scale, (x + tw) * scale, (y + th) * scale)
            table.insert(f.tiles, tex)
        end
    end
    if not ok then
        for i = 1, table.getn(f.tiles) do
            f.tiles[i]:SetTexture(DIALOG_BG)
            f.tiles[i]:SetTexCoord(0, 1, 0, 1)
        end
        -- the 1.12 client only sees texture files that existed when it started: /reload does not find new ones
        if H.Print then
            H.Print("The " .. AR.NAME .. " art is not loaded. If you just deployed it, restart WoW (a /reload is not enough).")
        end
    end
    f.textured = ok
end

function AU.Build()
    if AU.frame then
        return AU.frame
    end
    local f = CreateFrame("Frame", "HearthstoneArenaFrame", UIParent)
    f:SetWidth(AU.W)
    f:SetHeight(AU.H)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    f:SetFrameStrata("HIGH")
    f:SetToplevel(true)
    f:EnableMouse(true)
    f:SetClampedToScreen(true)
    local screenW = GetScreenWidth and GetScreenWidth() or 1024
    local screenH = GetScreenHeight and GetScreenHeight() or 768
    f:SetScale(CM.FitScale(screenW, screenH, AU.W, AU.H))
    f:Hide()
    AU.frame = f
    buildBackground(f)
    -- the frame of the addon's windows (the collection book's): the tinted dialog edge, above the background art.
    -- The texture is painted inwards from the frame's rectangle, and its visible stroke is about the outer half of
    -- the edge: the rectangle is grown by EDGE_OUT so the stroke sits just outside the window.
    local UI = H.CollectionUI
    local tint = UI and UI.FRAME_TINT or { 0.84, 0.84, 0.86, 1 }
    local EDGE, EDGE_OUT = 32, 11
    f.frameEdge = CreateFrame("Frame", nil, f)
    f.frameEdge:SetFrameLevel(f:GetFrameLevel() + 12)
    f.frameEdge:SetPoint("TOPLEFT", f, "TOPLEFT", -EDGE_OUT, EDGE_OUT)
    f.frameEdge:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", EDGE_OUT, -EDGE_OUT)
    f.frameEdge:SetBackdrop({
        edgeFile = UI and UI.FRAME_EDGE or "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = EDGE,
        insets = { left = 0, right = 0, top = 0, bottom = 0 },
    })
    f.frameEdge:SetBackdropBorderColor(tint[1], tint[2], tint[3], tint[4])

    -- the top plate: the name of the mode (light gold with an outline: the plate is dark)
    f.title = newText(f, "GameFontNormalLarge", "CENTER")
    setFont(f.title, HEAD_FONT, 16)    -- the plate is light parchment: dark ink, no outline
    f.title:SetTextColor(AU.INK[1], AU.INK[2], AU.INK[3])
    place(f.title, f, AU.TITLE[1], AU.TITLE[2], AU.TITLE[3], AU.TITLE[4])
    f.title:SetText(AR.NAME)

    local px1, py1, px2, py2 = AU.PARCH[1], AU.PARCH[2], AU.PARCH[3], AU.PARCH[4]
    local pcx = (px1 + px2) / 2
    -- the heading ("Pick a card (n of 30)", "Choose your Hero", ...) is not drawn: the count is on the bottom plate.
    -- The text object stays so the state of the window can still be read from it.
    f.heading = inkText(f, HEAD_FONT, 13, "CENTER")
    place(f.heading, f, px1 + 30, AU.BANNER_Y, px2 - 30, AU.BANNER_Y + 30)
    f.heading:Hide()
    f.body = inkText(f, BODY_FONT, 12, "CENTER")
    place(f.body, f, px1 + 30, AU.CONTENT_Y - 6, px2 - 30, AU.CONTENT_Y + 50)
    f.body:SetJustifyV("TOP")

    -- the hero choice: three golden arches with the hero's name and class under them
    local hw = AU.HERO_H * AU.ART.ArenaArch.aspect
    local hx0 = pcx - (3 * hw + 2 * AU.HERO_GAP) / 2
    AU.classes = {}
    for i = 1, AR.OFFER_SIZE do
        AU.classes[i] = AU.MakeClassButton(f, i, hx0 + (i - 1) * (hw + AU.HERO_GAP), AU.CONTENT_Y)
    end
    -- the hero view (a click on an arch), over everything: the dimmed window (a click on it is Back), then a frame
    -- above the dimming with the big arch, the hero's name and class, the hero power card and the Done and Back plates
    local vw = AU.VIEW
    local hv = {}
    hv.dim = CreateFrame("Frame", nil, f)
    hv.dim:SetAllPoints(f)
    hv.dim:SetFrameLevel(f:GetFrameLevel() + 10)
    hv.dim:EnableMouse(true)
    local shade = hv.dim:CreateTexture(nil, "BACKGROUND")
    shade:SetTexture(WHITE)
    shade:SetVertexColor(0, 0, 0, AU.VIEW_DIM)
    shade:SetAllPoints(hv.dim)
    hv.dim:SetScript("OnMouseUp", function()
        if not AR.IsBusy() then
            AU.OnBackClick()
        end
    end)
    hv.dim:Hide()
    hv.host = CreateFrame("Frame", nil, f)
    hv.host:SetAllPoints(f)
    hv.host:SetFrameLevel(f:GetFrameLevel() + 11)
    hv.host:Hide()
    local archW = vw.h * AU.ART.ArenaArch.aspect
    hv.hero = AU.MakeHero(hv.host, f, vw.cx - archW / 2, vw.y, vw.h)
    hv.name = newText(hv.host, "GameFontNormalLarge", "CENTER")
    setFont(hv.name, HEAD_FONT, 22, "OUTLINE")
    hv.name:SetTextColor(1, 0.9, 0.55)
    place(hv.name, f, vw.cx - 240, AU.VIEW_NAME_Y, vw.cx + 240, AU.VIEW_NAME_Y + 30)
    hv.cls = newText(hv.host, "GameFontNormal", "CENTER")
    setFont(hv.cls, BODY_FONT, 13, "OUTLINE")
    hv.cls:SetTextColor(0.9, 0.9, 0.9)
    place(hv.cls, f, vw.cx - 240, AU.VIEW_NAME_Y + 32, vw.cx + 240, AU.VIEW_NAME_Y + 54)
    -- the hero power: the large card, as the duel shows it when the power is hovered
    local pw = AU.VIEW_POWER
    hv.power = H.CardWidget.Create(hv.host, "HearthstoneArenaHeroPower", S(pw[3]), true)
    hv.power.frame:SetPoint("CENTER", f, "TOPLEFT", S(pw[1]), -S(pw[2]))
    hv.power.frame:Hide()
    -- Choose: the icy blue plate (its text is part of the art); without the art
    -- the gold plate with a label
    local UI = H.CollectionUI
    local donePiece = UI and UI.Piece and UI.Piece("btn_choose")
    local choose = function()
        AU.OnChooseClick()
    end
    if donePiece then
        hv.choose = CreateFrame("Button", "HearthstoneArenaChoose", hv.host)
        hv.choose:SetWidth(AU.VIEW_DONE_SIZE[1])
        hv.choose:SetHeight(AU.VIEW_DONE_SIZE[2])
        hv.choose:SetPoint("CENTER", f, "TOPLEFT", S(AU.VIEW_DONE[1]), -S(AU.VIEW_DONE[2]))
        hv.choose:SetNormalTexture(donePiece.path)
        hv.choose:GetNormalTexture():SetTexCoord(0, donePiece.right, 0, donePiece.bottom)
        hv.choose:SetHighlightTexture(donePiece.path, "ADD")
        local ht = hv.choose:GetHighlightTexture()
        ht:SetTexCoord(0, donePiece.right, 0, donePiece.bottom)
        ht:SetAlpha(0.35)
        if hv.choose.SetHitRectInsets then
            hv.choose:SetHitRectInsets(20, 20, 12, 12)
        end
        hv.choose:SetScript("OnClick", choose)
        hv.choose.art = donePiece
        UI.ButtonGlow(hv.choose)
    else
        hv.choose = artButton("HearthstoneArenaChoose", f, "ArenaPlate", AU.VIEW_DONE[1], AU.VIEW_DONE[2], 180, choose, hv.host)
        hv.choose.label = inkText(hv.choose, HEAD_FONT, 14, "CENTER")
        hv.choose.label:SetAllPoints(hv.choose)
        hv.choose.label:SetText("Choose")
    end
    hv.choose:SetFrameLevel(hv.host:GetFrameLevel() + 3)
    hv.back = artButton("HearthstoneArenaHeroBack", f, "ArenaPlate", AU.VIEW_BACK[1], AU.VIEW_BACK[2], AU.VIEW_BACK[3], function()
        AU.OnBackClick()
    end, hv.host)
    hv.back:SetFrameLevel(hv.host:GetFrameLevel() + 3)
    hv.back.tip = "Choose another hero."
    hv.back.label = inkText(hv.back, HEAD_FONT, 13, "CENTER")
    hv.back.label:SetAllPoints(hv.back)
    hv.back.label:SetText("Back")
    hv.parts = { hv.name, hv.cls, hv.choose, hv.back }
    for i = 1, table.getn(hv.parts) do
        hv.parts[i]:Hide()
    end
    AU.heroView = hv
    -- the card offer
    local cw, ch = AU.CARD_W, AU.CARD_W * 388 / 256
    local cx0 = pcx - (3 * cw + 2 * AU.CARD_GAP) / 2
    AU.slots = {}
    for i = 1, AR.OFFER_SIZE do
        local s = makeCard(f, "Offer", i, S(cw))
        s.widget.frame:SetPoint("TOPLEFT", f, "TOPLEFT", S(cx0 + (i - 1) * (cw + AU.CARD_GAP)), -S(AU.CONTENT_Y))
        -- Lua 5.0: the loop variable is shared by every closure of the loop (it would be 3 for every card)
        local idx = i
        s.button:SetScript("OnClick", function()
            AU.OnCardClick(idx)
        end)
        s.widget.frame:Hide()
        s.button:Hide()
        AU.slots[i] = s
    end
    local sw, sgap = AU.SPECIAL_W, AU.SPECIAL_GAP
    local sx0 = pcx - (4 * sw + 3 * sgap) / 2
    AU.picks = {}
    for i = 1, 4 do
        local s = makeCard(f, "Special", i, S(sw))
        s.widget.frame:SetPoint("TOPLEFT", f, "TOPLEFT", S(sx0 + (i - 1) * (sw + sgap)), -S(AU.SPECIAL_Y))
        local idx = i
        s.button:SetScript("OnClick", function()
            AU.OnSpecialClick(idx)
        end)
        s.caption = inkText(f, BODY_FONT, 12, "CENTER")
        s.caption:SetPoint("TOP", s.widget.frame, "BOTTOM", 0, -2)
        s.caption:SetWidth(S(sw))
        s.widget.frame:Hide()
        s.button:Hide()
        s.caption:Hide()
        AU.picks[i] = s
    end
    AU.specialCenter, AU.specialFrame = pcx, f
    f.specialNote = inkText(f, BODY_FONT, 11, "CENTER")
    place(f.specialNote, f, px1 + 30, AU.SPECIAL_Y + sw * 388 / 256 + 22, px2 - 30, AU.MESSAGE[2])
    f.specialNote:SetJustifyV("TOP")

    -- the run on the parchment (background pixels from the top): its text, the key emblem with the wins on its
    -- medallion, the "Losses:" label with its box (the red X once the run is lost) and the hint, down to the message line
    f.body:SetHeight(S(36))
    f.key = f:CreateTexture(nil, "ARTWORK")
    if not setArt(f.key, "ArenaKey") then
        f.key:SetTexture(nil)
    end
    f.key:SetVertexColor(AU.KEY_DIM, AU.KEY_DIM, AU.KEY_DIM)
    f.key:Hide()
    f.winsNumber = newText(f, "GameFontNormalLarge", "CENTER")
    f.winsNumber:SetTextColor(1, 1, 1)
    f.losses = f:CreateTexture(nil, "ARTWORK")
    f.lossesHitInstalled = f.losses:SetTexture(AU.TEXTURE_PATH .. "ArenaLossesHit") and true or false
    if not setArt(f.losses, "ArenaLosses") then
        f.losses:SetTexture(nil)
    end
    f.losses:Hide()
    f.hint = inkText(f, BODY_FONT, 9, "CENTER")
    f.hint:SetJustifyV("TOP")
    AU.LayoutRun(f, false)

    f.message = inkText(f, BODY_FONT, 11, "CENTER")
    place(f.message, f, AU.MESSAGE[1], AU.MESSAGE[2], AU.MESSAGE[3], AU.MESSAGE[4])
    f.message:SetJustifyV("TOP")

    -- bottom left: the drafted hero in the background's arch, the Retire plate under it
    local heroHost = CreateFrame("Frame", nil, f)
    heroHost:SetAllPoints(f)
    f.hero = AU.MakeHero(heroHost, f, AU.ARCH[1], AU.ARCH[2], AU.ARCH[4] - AU.ARCH[2])
    f.hero.button = CreateFrame("Button", "HearthstoneArenaHero", f)
    place(f.hero.button, f, AU.ARCH[1], AU.ARCH[2], AU.ARCH[3], AU.ARCH[4])
    f.hero.button:SetScript("OnEnter", function()
        if f.hero.class then
            showTip(f.hero.button, AU.HeroName(f.hero.class) .. " (" .. AR.ClassLabel(f.hero.class) .. ")")
        end
    end)
    f.hero.button:SetScript("OnLeave", hideTip)
    f.hero.button:Hide()
    f.retire = artButton("HearthstoneArenaRetire", f, "ArenaRetire", AU.RETIRE[1], AU.RETIRE[2], AU.RETIRE[3], function()
        AU.OnRetireClick()
    end)
    if not f.retire.installed then
        f.retire.label = newText(f.retire, "GameFontNormal", "CENTER")
        f.retire.label:SetAllPoints(f.retire)
        f.retire.label:SetText("Retire")
    end
    f.retire:Hide()

    -- bottom middle: the mana curve in the background's bar slots
    local bb = AU.BARS
    AU.bars = {}
    for i = 0, CM.MANA_MAX do
        local x = bb.x + i * bb.pitch
        local tex = f:CreateTexture(nil, "ARTWORK")
        if tex:SetTexture(AU.TEXTURE_PATH .. "ArenaLava") then
            tex:SetTexCoord(0, 1, 0, 1)      -- the whole lava bar, stretched to the height of the count
        else
            tex:SetTexture(WHITE)
            tex:SetVertexColor(1, 0.55, 0.12)
            if tex.SetGradient then
                tex:SetGradient("VERTICAL", 1, 0.78, 0.25, 1, 0.42, 0.05)
            end
        end
        tex:SetWidth(S(bb.w))
        tex:SetHeight(1)
        tex:SetPoint("BOTTOMLEFT", f, "TOPLEFT", S(x), -S(bb.bottom))
        local glow = f:CreateTexture(nil, "OVERLAY")
        glow:SetTexture(WHITE)
        glow:SetVertexColor(1, 0.75, 0.3)
        glow:SetBlendMode("ADD")
        glow:SetPoint("TOPLEFT", tex, "TOPLEFT", 0, 0)
        glow:SetPoint("BOTTOMRIGHT", tex, "TOPRIGHT", 0, -2)
        glow:SetAlpha(0)
        AU.bars[i] = { tex = tex, glow = glow }
    end

    -- bottom right: Play over the round seal
    f.play = artButton("HearthstoneArenaPlay", f, "ArenaPlay", AU.PLAY[1], AU.PLAY[2], AU.PLAY[3], function()
        AU.OnPlayClick()
    end)
    if not f.play.installed then
        f.play.label = newText(f.play, "GameFontNormalLarge", "CENTER")
        f.play.label:SetAllPoints(f.play)
        f.play.label:SetText("Play")
    end
    f.play:Hide()

    -- right: the picked list, "n / 30" and Back
    local dx1, dy1, dx2, dy2 = AU.DECK[1], AU.DECK[2], AU.DECK[3], AU.DECK[4]
    f.deckEmpty = newText(f, "GameFontDisableSmall", "CENTER")
    place(f.deckEmpty, f, dx1 + 8, dy1 + 10, dx2 - 8, dy1 + 60)
    f.deckEmpty:SetText("No cards picked yet.")
    AU.rows = {}
    local pitch = (dy2 - dy1) / AR.PICKS
    for i = 1, AR.PICKS do
        AU.rows[i] = AU.MakeRow(f, i, dx1, dy1 + (i - 1) * pitch, dx2 - dx1, pitch - 1)
    end
    f.deckTitle = newText(f, "GameFontHighlight", "CENTER")
    place(f.deckTitle, f, AU.COUNT[1], AU.COUNT[2], AU.COUNT[3], AU.COUNT[4])
    f.back = artButton("HearthstoneArenaBack", f, "ArenaPlate", AU.BACK[1], AU.BACK[2], AU.BACK[3], function()
        f:Hide()
    end)
    f.back.label = inkText(f.back, HEAD_FONT, 13, "CENTER")
    f.back.label:SetAllPoints(f.back)
    f.back.label:SetText("Back")

    f:SetScript("OnShow", function()
        if PlaySound then
            PlaySound("igCharacterInfoOpen")
        end
        AU.Refresh()
    end)
    f:SetScript("OnHide", function()
        AU.hover = nil
        AU.chosen = nil
        AU.HidePreview()
        hideTip()
        if StaticPopup_Hide then
            StaticPopup_Hide("HEARTHSTONE_ARENA_RETIRE")
        end
        if PlaySound then
            PlaySound("igCharacterInfoClose")
        end
    end)
    if UISpecialFrames then
        local have = false
        for i = 1, table.getn(UISpecialFrames) do
            if UISpecialFrames[i] == "HearthstoneArenaFrame" then
                have = true
            end
        end
        if not have then
            table.insert(UISpecialFrames, "HearthstoneArenaFrame")
        end
    end
    return f
end

-- A hero button of the class choice at the background point (x, y) (top left): the portrait in a golden arch, the
-- hero's name and the class under it.
function AU.MakeClassButton(parent, index, x, y)
    local h = AU.HERO_H
    local w = h * AU.ART.ArenaArch.aspect
    local b = CreateFrame("Button", "HearthstoneArenaClass" .. index, parent)
    place(b, parent, x, y, x + w, y + h + 56)
    b.portrait = AU.MakeHero(b, parent, x, y, h)
    AU.ArchGlow(b, parent, "HIGHLIGHT", x, y, h, 1, 0.9, 0.6, 0.12, 0.55)     -- hover: the arch, not its square
    b.hero = inkText(b, HEAD_FONT, 13, "CENTER")
    place(b.hero, parent, x - 30, y + h + 4, x + w + 30, y + h + 28)
    b.name = inkText(b, BODY_FONT, 9, "CENTER")
    place(b.name, parent, x - 30, y + h + 28, x + w + 30, y + h + 46)
    -- the pick on its way to the server: the same arch glow in gold, in a frame of its own to show and hide
    b.pick = CreateFrame("Frame", nil, b)
    b.pick:SetAllPoints(b)
    AU.ArchGlow(b.pick, parent, "OVERLAY", x, y, h, 1, 0.82, 0.2, 0.22, 0.7)
    b.pick:Hide()
    b:SetScript("OnClick", function()
        AU.OnClassClick(index)
    end)
    b:Hide()
    return b
end

-- A row of the picked list at the background point (x, y), w x h: the cost gem, the name in its rarity colour, the
-- card's art strip on the right and the copies (or the star of a Legendary) in a dark box at its end.
function AU.MakeRow(parent, index, x, y, w, h)
    local r = CreateFrame("Button", "HearthstoneArenaRow" .. index, parent)
    place(r, parent, x, y, x + w, y + h)
    r:SetHighlightTexture(WHITE, "ADD")
    r:GetHighlightTexture():SetAlpha(0.12)
    local rh = S(y + h) - S(y)
    local bg = r:CreateTexture(nil, "BACKGROUND")
    bg:SetTexture(WHITE)
    bg:SetVertexColor(0.05, 0.03, 0.09, 0.85)
    bg:SetAllPoints(r)
    r.art = r:CreateTexture(nil, "BORDER")
    r.art:SetWidth(S(w * 0.45))
    r.art:SetPoint("TOPRIGHT", r, "TOPRIGHT", 0, 0)
    r.art:SetPoint("BOTTOMRIGHT", r, "BOTTOMRIGHT", 0, 0)
    local shade = r:CreateTexture(nil, "ARTWORK")
    shade:SetTexture(WHITE)
    shade:SetVertexColor(0.05, 0.03, 0.09)
    if shade.SetGradientAlpha then
        shade:SetGradientAlpha("HORIZONTAL", 0.05, 0.03, 0.09, 1, 0.05, 0.03, 0.09, 0)
    end
    shade:SetWidth(S(w * 0.2))
    shade:SetPoint("TOPLEFT", r.art, "TOPLEFT", 0, 0)
    shade:SetPoint("BOTTOMLEFT", r.art, "BOTTOMLEFT", 0, 0)
    local gem = r:CreateTexture(nil, "ARTWORK")
    local UI = H.CollectionUI
    if UI and UI.SetPiece and UI.SetPiece(gem, "gem_cost") then
        gem:SetWidth(rh + 2)
        gem:SetHeight(rh + 1)
        gem:SetPoint("LEFT", r, "LEFT", -1, 0)
    else
        gem:SetTexture(WHITE)
        gem:SetVertexColor(0.15, 0.35, 0.75)
        gem:SetWidth(rh - 2)
        gem:SetHeight(rh - 2)
        gem:SetPoint("LEFT", r, "LEFT", 1, 0)
    end
    r.cost = newText(r, "GameFontNormalSmall", "CENTER")
    setFont(r.cost, BODY_FONT, 11, "OUTLINE")
    r.cost:SetTextColor(1, 1, 1)
    r.cost:SetPoint("CENTER", gem, "CENTER", 0, 0)
    r.cost:SetWidth(rh + 2)
    r.name = newText(r, "GameFontHighlightSmall", "LEFT")
    setFont(r.name, BODY_FONT, 9, "OUTLINE")
    r.name:SetPoint("LEFT", r, "LEFT", rh + 3, 0)
    r.name:SetWidth(S(w) - rh - 20)
    r.name:SetHeight(rh)
    r.box = r:CreateTexture(nil, "ARTWORK")
    r.box:SetTexture(WHITE)
    r.box:SetVertexColor(0, 0, 0, 0.75)
    r.box:SetWidth(14)
    r.box:SetPoint("TOPRIGHT", r, "TOPRIGHT", 0, 0)
    r.box:SetPoint("BOTTOMRIGHT", r, "BOTTOMRIGHT", 0, 0)
    r.box:Hide()
    r.count = newText(r, "GameFontNormalSmall", "CENTER")
    r.count:SetPoint("CENTER", r.box, "CENTER", 0, 0)
    r.count:SetWidth(14)
    r.star = r:CreateTexture(nil, "OVERLAY")
    r.star:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcons")
    r.star:SetTexCoord(0, 0.25, 0, 0.25)
    r.star:SetWidth(11)
    r.star:SetHeight(11)
    r.star:SetPoint("CENTER", r.box, "CENTER", 0, 0)
    r.star:Hide()
    r:SetScript("OnEnter", function()
        AU.ShowPreview(AU.rows[index] and AU.rows[index].id, "Row")
    end)
    r:SetScript("OnLeave", function()
        AU.HidePreview()
    end)
    r:Hide()
    return r
end

-- Builds the window on first use. A failure is reported once instead of raising.
function AU.EnsureBuilt()
    if AU.frame then
        return true
    end
    if not CreateFrame or not UIParent then
        return false
    end
    local ok, err = pcall(AU.Build)
    if not ok or not AU.frame then
        local stale = getglobal and getglobal("HearthstoneArenaFrame")
        if stale then
            stale:Hide()
        end
        AU.frame = nil
        if H.PrintError then
            H.PrintError("Could not build the " .. AR.NAME .. " window: " .. tostring(err))
        end
        return false
    end
    return true
end

-- Opens the window (/hs arena, ARENA_OPEN). Returns true when it is shown.
function AU.Show()
    if not AU.EnsureBuilt() then
        return false
    end
    if AU.frame:IsShown() then
        AU.Refresh()
    else
        AU.frame:Show()               -- OnShow refreshes
    end
    return true
end

function AU.Hide()
    if AU.frame and AU.frame:IsShown() then
        AU.frame:Hide()
    end
end

function AU.IsShown()
    return AU.frame ~= nil and AU.frame:IsShown()
end

function H.ShowArena()
    return AU.Show()
end

-- Closes the window. forMatch: a match starts; an open window comes back when the match window closes (at the host).
function H.HideArena(forMatch)
    if forMatch then
        AU.reopen = AU.IsShown()
    end
    AU.Hide()
end

-- The match window closed (MatchUI.lua): the arena window that a duel replaced comes back while the player is still
-- at the host.
function H.OnMatchWindowHidden()
    if not AU.reopen then
        return
    end
    AU.reopen = false
    if AR.atHost then
        AU.Show()
    end
end

-- The arena state changed (Hearthstone.lua): an open window is redrawn; a closed one stays closed.
function H.OnArenaChanged()
    AU.Refresh()
end
