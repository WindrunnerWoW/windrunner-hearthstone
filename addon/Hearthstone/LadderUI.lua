-- Ranked opponent selection, Lua 5.0 / WoW 1.12.
-- Structured chair views use WRHS; the portraits and shaped glows reuse ArenaUI.
Hearthstone = Hearthstone or {}
local H, P, AU, CM = Hearthstone, HsProtocol, Hearthstone.ArenaUI, HsCollection
local LU = { W = 1104, H = 644, K = 1104 / 1536, elapsed = 0, page = 1 }
H.LadderUI = LU
local WHITE = "Interface\\Buttons\\WHITE8X8"
local PATH = "Interface\\Hearthstone\\Textures\\UI\\"
-- The badge itself fits in the upper left; stars arch over it as in the owner's references.
LU.BADGE = { cx = 114, y = 82, h = 176, starW = 34, pitch = 33 }

local function number(s, max)
    if type(s) ~= "string" or not string.find(s, "^%d+$") then return nil end
    local n = tonumber(s)
    if not n or n > (max or 4294967295) then return nil end
    return n
end

function LU.Parse(payload)
    local kv = P.KvDecode(payload)
    if not kv or (kv.st ~= "offers" and kv.st ~= "paused" and kv.st ~= "waiting") then return nil end
    local v = { st = kv.st, offers = {}, decks = {}, name = kv.name, class = kv.class, note = kv.note or "" }
    if kv.rank ~= nil then
        v.rank, v.stars = number(kv.rank, 25), number(kv.stars, 5)
        v.wins, v.losses, v.draws = number(kv.wins), number(kv.losses), number(kv.draws)
        if not v.rank or not v.stars or not v.wins or not v.losses or not v.draws then return nil end
        v.maxStars = number(kv.maxstars, 5)
        v.legendPosition = kv.legendpos and number(kv.legendpos) or 0
        if v.maxStars == nil or not v.legendPosition or v.stars > v.maxStars
            or (v.rank == 0 and (v.maxStars ~= 0 or v.stars ~= 0))
            or (v.rank > 0 and v.maxStars == 0) then return nil end
    end
    if v.st == "paused" then
        v.duel = kv.duel
        v.npc = number(kv.npc)
        if not v.duel or string.len(v.duel) > 20 or not string.find(v.duel, "^%d+$") or v.duel == "0"
            or not v.npc or v.npc == 0 or not HsDecks.IsClass(v.class) or not v.name or v.name == "" then return nil end
    elseif v.st == "offers" then
        local n, dn = number(kv.n, 3), number(kv.dn, 100)
        if not v.rank or not v.stars or not v.wins or not v.losses or not v.draws or not n or not dn then return nil end
        local seen = {}
        for i = 1, n do
            local p = "o" .. i
            local o = { id = number(kv[p .. "id"]), name = kv[p .. "name"], class = kv[p .. "class"],
                faction = kv[p .. "faction"], rank = number(kv[p .. "rank"], 25), stars = number(kv[p .. "stars"], 5),
                wins = number(kv[p .. "wins"]), losses = number(kv[p .. "losses"]), draws = number(kv[p .. "draws"]) }
            if not o.id or o.id == 0 or seen[o.id] or not o.name or o.name == "" or not HsDecks.IsClass(o.class)
                or not o.faction or not o.rank or not o.stars or not o.wins or not o.losses or not o.draws then return nil end
            seen[o.id] = true
            table.insert(v.offers, o)
        end
        seen = {}
        for i = 1, dn do
            local p = "d" .. i
            local d = { id = number(kv[p .. "id"]), name = kv[p .. "name"] }
            if not d.id or d.id == 0 or seen[d.id] or not d.name or d.name == "" then return nil end
            seen[d.id] = true
            table.insert(v.decks, d)
        end
    end
    return v
end

function LU.StarPoint(index, count)
    local heights = count == 3 and { 36, 19, 36 } or (count == 5 and { 61, 31, 17, 31, 61 })
    local offset = (index - (count + 1) / 2) * LU.BADGE.pitch
    local y = heights and heights[index] or (19 + math.abs(offset) * 0.45)
    return LU.BADGE.cx + offset - LU.BADGE.starW / 2, y
end

function LU.Rank(rank, stars)
    if rank == 0 then return "Legend" end
    return "Rank " .. rank .. "  -  " .. stars .. (stars == 1 and " star" or " stars")
end

function LU.Stats(o)
    return HsArena.ClassLabel(o.class) .. "  -  " .. o.faction
end

function LU.Deck()
    for i = 1, table.getn(LU.view and LU.view.decks or {}) do
        local d = LU.view.decks[i]
        if d.id == LU.deckId then return d end
    end
    return nil
end

-- The account's default deck (middle click in the Decks tab) when the ladder lists it, or nil.
function LU.DefaultDeckId(decks)
    local id = HearthstoneDB and HearthstoneDB.defaultDeck
    for i = 1, table.getn(decks or {}) do
        if decks[i].id == id then return id end
    end
    return nil
end

-- The deck menu's order: the default first, then the rest as the server listed them.
function LU.MenuDecks()
    local decks = LU.view and LU.view.decks or {}
    local def = LU.DefaultDeckId(decks)
    local out = {}
    for i = 1, table.getn(decks) do
        if decks[i].id == def then table.insert(out, decks[i]) end
    end
    for i = 1, table.getn(decks) do
        if decks[i].id ~= def then table.insert(out, decks[i]) end
    end
    return out
end

local function place(widget, parent, x, y, w, h)
    widget:SetWidth(w * LU.K); widget:SetHeight(h * LU.K)
    widget:SetPoint("TOPLEFT", parent, "TOPLEFT", x * LU.K, -y * LU.K)
end

local function text(parent, x, y, w, h, size, gold)
    local t = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    t:SetFont(gold and "Fonts\\MORPHEUS.TTF" or "Fonts\\FRIZQT__.TTF", size * LU.K, "OUTLINE")
    t:SetTextColor(1, gold and 0.85 or 0.92, gold and 0.5 or 0.82)
    t:SetJustifyH("CENTER"); t:SetJustifyV("MIDDLE")
    place(t, parent, x, y, w, h)
    return t
end

local function enabled(button, on)
    if on then button:Enable() else button:Disable() end
    button:SetAlpha(on and 1 or 0.45)
end

local function visible(region, on)
    if on then region:Show() else region:Hide() end
end

local function plate(parent, name, label, x, y, w, h, click)
    local b = CreateFrame("Button", name, parent)
    place(b, parent, x, y, w, h)
    local a = AU.ART.ArenaPlate
    b.tex = b:CreateTexture(nil, "ARTWORK")
    b.tex:SetAllPoints(b); b.tex:SetTexture(PATH .. "ArenaPlate")
    b.tex:SetTexCoord(0, a.right, 0, a.bottom)
    b.glow = b:CreateTexture(nil, "HIGHLIGHT")
    b.glow:SetAllPoints(b); b.glow:SetTexture(PATH .. "ArenaPlate")
    b.glow:SetTexCoord(0, a.right, 0, a.bottom); b.glow:SetBlendMode("ADD"); b.glow:SetAlpha(0.3)
    b.label = text(b, 8, 0, w - 16, h, 22, true)
    b.label:SetText(label); b:SetScript("OnClick", click)
    return b
end

-- A wide plate that keeps the proportions of the art: the metal ends stay at their natural size and only the
-- uniform middle of the plate is stretched.
local function widePlate(parent, name, label, x, y, w, h, click)
    local b = CreateFrame("Button", name, parent)
    place(b, parent, x, y, w, h)
    local a = AU.ART.ArenaPlate
    local cut = 0.38
    local cap = cut * h * a.aspect
    local parts = { { 0, cut, 0, cap }, { cut, 1 - cut, cap, w - 2 * cap }, { 1 - cut, 1, w - cap, cap } }
    for i = 1, 3 do
        local u1, u2, px, pw = parts[i][1], parts[i][2], parts[i][3], parts[i][4]
        local t = b:CreateTexture(nil, "ARTWORK")
        place(t, b, px, 0, pw, h); t:SetTexture(PATH .. "ArenaPlate"); t:SetTexCoord(u1 * a.right, u2 * a.right, 0, a.bottom)
        local g = b:CreateTexture(nil, "HIGHLIGHT")
        place(g, b, px, 0, pw, h); g:SetTexture(PATH .. "ArenaPlate"); g:SetTexCoord(u1 * a.right, u2 * a.right, 0, a.bottom)
        g:SetBlendMode("ADD"); g:SetAlpha(0.3)
    end
    b.label = text(b, 0, 0, w, h, 17, true)
    b.label:SetText(label); b:SetScript("OnClick", click)
    return b
end

-- ArenaUI pieces are anchored in arena background pixels; convert our coordinates.
local function hero(parent, x, y, h)
    local q = LU.K / AU.K
    return AU.MakeHero(parent, parent, x * q, y * q, h * q)
end

function LU.Send(op, values)
    if LU.request then return end
    local req = H.Request(op, P.KvEncode(values or {}), {})
    if not req then LU.message = "Not connected. Sit down again after reconnecting."
    else
        LU.request, LU.elapsed = req, 0
        LU.message = op == "LADDER_GET" and "Loading..." or "Waiting for the table..."
    end
    LU.Refresh()
end

function LU.Confirm()
    if LU.request or not LU.chosen or not LU.Deck() then return end
    LU.Send("LADDER_PLAY", { { "npc", tostring(LU.chosen.id) }, { "deck", tostring(LU.deckId) } })
end

function LU.PausedAction(op)
    if LU.view and LU.view.st == "paused" then LU.Send(op, {{ "duel", LU.view.duel }}) end
end

function LU.Back()
    if LU.request then return end
    LU.chosen = nil; LU.Refresh()
end

function LU.Hide()
    if LU.frame then LU.frame:Hide() end
end

function LU.OnMessage(msg, open)
    if not open and msg.reqId ~= "0" and msg.reqId ~= LU.request then return end
    local v = LU.Parse(msg.payload)
    if not v then
        LU.request = nil
        LU.message = "The ladder reply could not be read. Sit down again to retry."
        LU.Refresh(); return
    end
    LU.request, LU.elapsed = nil, 0
    LU.view, LU.message = v, nil
    if v.rank ~= nil then LU.standing = v end
    if v.st == "waiting" then
        -- no screen while the opponent walks up: the player watches him arrive at the table
        LU.chosen = nil
        LU.Refresh(); LU.Hide(); return
    end
    local chosen = LU.chosen
    LU.chosen = nil
    for i = 1, table.getn(v.offers) do
        if chosen and v.offers[i].id == chosen.id then LU.chosen = v.offers[i] end
    end
    -- opening the ladder selects the default deck; a refresh keeps the choice while the deck is still listed
    local def = LU.DefaultDeckId(v.decks)
    if def and (open or not LU.Deck()) then LU.deckId = def end
    if not LU.Deck() then LU.deckId = v.decks[1] and v.decks[1].id end
    if open then
        LU.chosen, LU.page = nil, 1
        if H.HideArena then H.HideArena() end
        if H.CollectionUI and H.CollectionUI.Hide then H.CollectionUI.Hide() end
        LU.Build():Show()
    end
    LU.Refresh()
end

function LU.OnError(msg)
    if msg.reqId ~= LU.request then return false end
    local kv = P.KvDecode(msg.payload)
    LU.request, LU.elapsed = nil, 0
    LU.message = kv and kv.msg or "The ladder request failed. Sit down again to retry."
    LU.Refresh(); return true
end

function LU.Refresh()
    local f, v = LU.frame, LU.view
    if not f or not v then return end
    local busy, offers = LU.request ~= nil, v.st == "offers"
    local viewing = offers and LU.chosen ~= nil
    local standing = LU.standing
    f.standing:SetText(standing and ("Your record: " .. standing.wins .. "W / " .. standing.losses
        .. "L / " .. standing.draws .. "D") or "")
    LU.RefreshBadge(standing)
    for i = 1, 3 do
        local b, o = f.opponents[i], v.offers[i]
        b.value = o
        if o then
            AU.SetHeroArt(b.hero, o.class)
            b.name:SetText(o.name); b.stats:SetText(LU.Stats(o)); b:Show(); enabled(b, not busy)
            visible(b.name, not viewing); visible(b.stats, not viewing)
        else b:Hide() end
    end
    visible(f.standing, not viewing)
    visible(f.rankBadge, standing ~= nil and not viewing)
    visible(f.rankPlaque, standing ~= nil and not viewing)
    visible(f.message, not viewing)
    visible(f.back, not viewing)
    if offers and not viewing then f.deck:Show() else f.deck:Hide(); f.menu:Hide() end
    f.deck.label:SetText(LU.Deck() and (LU.Deck().name) or "No playable decks")
    enabled(f.deck, offers and table.getn(v.decks) > 0 and not busy)
    f.message:SetText(LU.message or (offers and (table.getn(v.decks) == 0
        and (v.note ~= "" and v.note or "Build a complete, owned 30-card deck before playing.")
        or "Choose your deck, then choose a rival.")
        or (v.st == "paused" and ("Your duel against " .. v.name .. " is paused.")
        or ((v.name or "Your opponent") .. " is on the way to the table."))))
    if v.st == "paused" then f.resume:Show(); f.surrender:Show()
    else f.resume:Hide(); f.surrender:Hide() end
    enabled(f.resume, not busy); enabled(f.surrender, not busy)
    if not offers then LU.chosen = nil end
    local hv, o = f.heroView, LU.chosen
    if o then
        f.menu:Hide(); hv.dim:Show(); hv.host:Show()
        AU.SetHeroArt(hv.hero, o.class)
        hv.name:SetText(o.name); hv.stats:SetText(LU.Stats(o))
        hv.status:SetText(LU.message or "")
        hv.deck:SetText(LU.Deck() and ("Your deck: " .. LU.Deck().name) or "Select a playable deck first.")
        enabled(hv.done, not busy and LU.Deck() ~= nil); enabled(hv.back, not busy)
    else hv.host:Hide(); hv.dim:Hide() end
    if busy then f.menu:Hide() end
end

function LU.RefreshBadge(standing)
    local b = LU.frame and LU.frame.rankBadge
    if not b or not standing then return end
    local art = HS_LadderRankTextures or { ranks = {} }
    local legend = standing.rank == 0
    local spec = legend and art.legend or art.ranks[standing.rank]
    local name = legend and "LadderLegend" or ("LadderRank_" .. standing.rank)
    local installed = spec and b.icon:SetTexture(PATH .. name)
    b.fallback:Hide(); b.number:Hide()
    if installed then
        local w = LU.BADGE.h * spec.aspect
        b.icon:ClearAllPoints()
        place(b.icon, b, LU.BADGE.cx - w / 2, LU.BADGE.y, w, LU.BADGE.h)
        b.icon:SetTexCoord(0, spec.right, 0, spec.bottom)
        b.icon:Show()
        if legend then
            local pos = standing.legendPosition > 0 and tostring(standing.legendPosition) or "?"
            local size = string.len(pos) <= 3 and 52 or (string.len(pos) == 4 and 40 or 30)
            b.number:SetFont("Fonts\\FRIZQT__.TTF", size * LU.K, "THICKOUTLINE")
            b.number:SetText(pos)
            b.number:Show()
        end
    else
        -- Ranks 16..25 work immediately and pick up their own artwork once provided.
        b.icon:Hide(); b.fallback:Show()
        b.fallback.label:SetText(legend and "Legend" or ("Rank " .. standing.rank))
    end
    b.caption:SetText(legend and "Legend" or "Your Rank")
    for i = 1, 5 do
        local star = b.stars[i]
        if not legend and i <= standing.maxStars then
            local x, y = LU.StarPoint(i, standing.maxStars)
            star:ClearAllPoints()
            place(star, b, x, y, LU.BADGE.starW, LU.BADGE.starW)
            star:SetTexture(PATH .. (i <= standing.stars and "LadderStar_full" or "LadderStar_empty"))
            star:SetTexCoord(0, 1, 0, 1); star:Show()
        else star:Hide() end
    end
end

function LU.DeckMenu()
    local f, v = LU.frame, LU.view
    if not f or not v then return end
    local decks = LU.MenuDecks()
    local def = LU.DefaultDeckId(v.decks)
    local pages = math.max(1, math.ceil(table.getn(decks) / 6))
    LU.page = math.min(LU.page, pages)
    for i = 1, 6 do
        local b, d = f.menu.rows[i], decks[(LU.page - 1) * 6 + i]
        b.value = d
        if d then
            b.label:SetText(d.name); b:Show()
            if d.id == def then b.star:Show() else b.star:Hide() end
            if d.id == LU.deckId then b.label:SetTextColor(1, 0.82, 0.3) else b.label:SetTextColor(0.92, 0.88, 0.78) end
        else b:Hide(); b.star:Hide() end
    end
    f.menu.count:SetText(LU.page .. " / " .. pages)
    enabled(f.menu.prev, LU.page > 1); enabled(f.menu.next, LU.page < pages)
end

function LU.Build()
    LU.RegisterPopups()
    if LU.frame then return LU.frame end
    local f = CreateFrame("Frame", "HearthstoneLadderFrame", UIParent)
    LU.frame = f
    f:SetWidth(LU.W); f:SetHeight(LU.H); f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    f:SetFrameStrata("HIGH"); f:SetToplevel(true); f:EnableMouse(true); f:SetClampedToScreen(true)
    f:SetScale(CM.FitScale(GetScreenWidth(), GetScreenHeight(), LU.W, LU.H)); f:Hide()
    for row = 0, 1 do
        for col = 0, 2 do
            local h = row == 0 and 512 or 384
            local t = f:CreateTexture(nil, "BACKGROUND")
            t:SetTexture(PATH .. "LadderBg_c" .. col .. "r" .. row); t:SetTexCoord(0, 1, 0, h / 512)
            place(t, f, col * 512, row * 512, 512, h)
        end
    end
    -- the frame of the addon's windows (as ArenaUI): the tinted dialog edge, above the background art
    local tint = H.CollectionUI and H.CollectionUI.FRAME_TINT or { 0.84, 0.84, 0.86, 1 }
    f.frameEdge = CreateFrame("Frame", nil, f)
    f.frameEdge:SetFrameLevel(f:GetFrameLevel() + 12)
    f.frameEdge:SetPoint("TOPLEFT", f, "TOPLEFT", -11, 11)
    f.frameEdge:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", 11, -11)
    f.frameEdge:SetBackdrop({
        edgeFile = H.CollectionUI and H.CollectionUI.FRAME_EDGE or "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32, insets = { left = 0, right = 0, top = 0, bottom = 0 },
    })
    f.frameEdge:SetBackdropBorderColor(tint[1], tint[2], tint[3], tint[4])
    f.standing = text(f, 250, 178, 1036, 36, 22, true)
    -- a dark plaque behind the rank so the badge and stars stand out from the stone
    f.rankPlaque = CreateFrame("Frame", nil, f)
    place(f.rankPlaque, f, 10, 10, 208, 300)
    f.rankPlaque:SetBackdrop({ bgFile = WHITE, edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = true,
        tileSize = 8, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f.rankPlaque:SetBackdropColor(0.07, 0.02, 0.02, 0.88)
    f.rankPlaque:SetBackdropBorderColor(0.85, 0.7, 0.35, 1)
    local badge = CreateFrame("Frame", nil, f)
    f.rankBadge = badge
    badge:SetAllPoints(f)
    badge.icon = badge:CreateTexture(nil, "ARTWORK")
    badge.number = text(badge, LU.BADGE.cx - 60, LU.BADGE.y + 46, 120, 80, 52, true)
    badge.number:SetFont("Fonts\\FRIZQT__.TTF", 52 * LU.K, "THICKOUTLINE")
    badge.number:SetTextColor(1, 0.93, 0.63)
    badge.caption = text(badge, LU.BADGE.cx - 80, LU.BADGE.y + LU.BADGE.h + 4, 160, 28, 19, true)
    badge.fallback = plate(badge, nil, "", LU.BADGE.cx - 85, LU.BADGE.y + 70, 170, 58, function() end)
    badge.fallback:Disable()
    badge.stars = {}
    for i = 1, 5 do badge.stars[i] = badge:CreateTexture(nil, "ARTWORK") end
    badge:Hide()
    f.opponents = {}
    for i = 1, 3 do
        local b = CreateFrame("Button", nil, f)
        place(b, f, 285 + (i - 1) * 348, 230, 270, 462)
        b.hero = hero(b, 0, 0, 300)
        AU.ArchGlow(b, b, "HIGHLIGHT", 0, 0, 300 * LU.K / AU.K, 1, 0.9, 0.6, 0.12, 0.55)
        b.name = text(b, -30, 316, 330, 58, 29, true)
        b.stats = text(b, -30, 366, 330, 40, 20, false)
        b:SetScript("OnClick", function()
            if not LU.request then LU.chosen = this.value; LU.Refresh() end
        end)
        f.opponents[i] = b
    end
    f.message = text(f, 200, 714, 1136, 42, 20, false)
    f.deck = widePlate(f, "HearthstoneLadderDeck", "Select your deck", 548, 782, 440, 68, function()
        if f.menu:IsShown() then f.menu:Hide() else LU.DeckMenu(); f.menu:Show() end
    end)
    f.back = plate(f, nil, "Back", 1232, 800, 150, 46, LU.Hide)
    f.back.label:SetFont("Fonts\\MORPHEUS.TTF", 18 * LU.K, "OUTLINE")
    f.resume = plate(f, nil, "Resume", 515, 535, 240, 70, function() LU.PausedAction("LADDER_RES") end)
    f.surrender = plate(f, nil, "Surrender", 785, 535, 240, 70, function()
        LU.surrenderDuel = LU.view and LU.view.duel
        StaticPopup_Show("HEARTHSTONE_LADDER_PAUSED_SURRENDER")
    end)
    -- the deck label sits on the light parchment of the plate: plain, readable, dark ink
    f.deck.label:SetFont("Fonts\\FRIZQT__.TTF", 17 * LU.K)
    f.deck.label:SetTextColor(0.2, 0.1, 0.04)
    f.deck.label:SetPoint("TOPLEFT", f.deck, "TOPLEFT", 66 * LU.K, 0)
    f.deck.label:SetPoint("BOTTOMRIGHT", f.deck, "BOTTOMRIGHT", -66 * LU.K, 0)
    local menu = CreateFrame("Frame", nil, f)
    f.menu = menu
    place(menu, f, 548, 508, 440, 268); menu:SetFrameLevel(f:GetFrameLevel() + 4); menu:EnableMouse(true)
    menu:SetBackdrop({ bgFile = WHITE, edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = true,
        tileSize = 8, edgeSize = 14, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    menu:SetBackdropColor(0.10, 0.04, 0.03, 0.98)
    menu:SetBackdropBorderColor(0.85, 0.7, 0.35, 1)
    menu.rows = {}
    for i = 1, 6 do
        local b = CreateFrame("Button", nil, menu)
        place(b, menu, 10, 10 + (i - 1) * 34, 420, 32)
        local hl = b:CreateTexture(nil, "HIGHLIGHT")
        hl:SetAllPoints(b); hl:SetTexture(WHITE); hl:SetVertexColor(0.8, 0.5, 0.15, 0.3)
        -- the star on the top right of the default deck's row; the name keeps clear of it
        b.star = b:CreateTexture(nil, "ARTWORK")
        place(b.star, b, 392, 2, 24, 24); b.star:SetTexture(PATH .. "LadderStar_full"); b.star:Hide()
        b.label = text(b, 6, 0, 380, 32, 17, false)
        b.label:SetFont("Fonts\\FRIZQT__.TTF", 17 * LU.K)
        b:SetScript("OnClick", function() LU.deckId = this.value.id; menu:Hide(); LU.Refresh() end)
        menu.rows[i] = b
    end
    menu.prev = plate(menu, nil, "<", 14, 222, 70, 34, function() LU.page = LU.page - 1; LU.DeckMenu() end)
    menu.next = plate(menu, nil, ">", 356, 222, 70, 34, function() LU.page = LU.page + 1; LU.DeckMenu() end)
    -- the arrow glyphs sit a touch high on the small plates
    for _, b in ipairs({ menu.prev, menu.next }) do
        b.label:ClearAllPoints(); place(b.label, b, 8, -3, 54, 34)
    end
    menu.count = text(menu, 170, 222, 100, 34, 18, true); menu:Hide()
    local hv = {}
    f.heroView = hv
    hv.dim = CreateFrame("Frame", nil, f)
    hv.dim:SetAllPoints(f); hv.dim:SetFrameLevel(f:GetFrameLevel() + 10); hv.dim:EnableMouse(true)
    local shade = hv.dim:CreateTexture(nil, "BACKGROUND")
    shade:SetAllPoints(hv.dim); shade:SetTexture(WHITE); shade:SetVertexColor(0, 0, 0, AU.VIEW_DIM)
    hv.dim:SetScript("OnMouseUp", LU.Back)
    hv.host = CreateFrame("Frame", nil, f)
    hv.host:SetAllPoints(f); hv.host:SetFrameLevel(f:GetFrameLevel() + 11)
    hv.hero = hero(hv.host, 561, 138, 460)
    hv.name = text(hv.host, 448, 662, 640, 46, 32, true)
    hv.stats = text(hv.host, 438, 710, 660, 80, 20, false)
    hv.deck = text(hv.host, 448, 792, 640, 30, 19, false)
    hv.status = text(hv.host, 170, 530, 330, 150, 22, false)
    hv.done = plate(hv.host, nil, "Choose", 668, 531, 200, 100, LU.Confirm)
    local UI = H.CollectionUI
    local piece = UI and UI.Piece and UI.Piece("btn_choose")
    if piece then
        hv.done.label:SetText(""); hv.done.tex:Hide(); hv.done.glow:Hide()
        hv.done:SetNormalTexture(piece.path)
        hv.done:GetNormalTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
        hv.done:SetHighlightTexture(piece.path, "ADD")
        hv.done:GetHighlightTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
        hv.done:GetHighlightTexture():SetAlpha(0.35)
        UI.ButtonGlow(hv.done)
    end
    hv.back = plate(hv.host, nil, "Back", 678, 833, 180, 50, LU.Back)
    hv.dim:Hide(); hv.host:Hide()
    f:SetScript("OnHide", function()
        LU.chosen, LU.surrenderDuel = nil, nil
        LU.request, LU.elapsed = nil, 0     -- the OnUpdate timeout does not run while hidden
        StaticPopup_Hide("HEARTHSTONE_LADDER_PAUSED_SURRENDER")
        menu:Hide(); hv.dim:Hide(); hv.host:Hide()
    end)
    f:SetScript("OnUpdate", function()
        if LU.request then
            LU.elapsed = LU.elapsed + (arg1 or 0)
            if LU.elapsed > 12 then
                LU.request = nil
                LU.message = "No response. Sit down again to check the table before retrying."
                LU.Refresh()
            end
        elseif LU.view and LU.view.st == "waiting" then
            LU.elapsed = LU.elapsed + (arg1 or 0)
            if LU.elapsed > 3 then LU.Send("LADDER_GET") end
        end
    end)
    if UISpecialFrames then table.insert(UISpecialFrames, "HearthstoneLadderFrame") end
    return f
end

function LU.RegisterPopups()
    StaticPopupDialogs = StaticPopupDialogs or {}
    StaticPopupDialogs["HEARTHSTONE_LADDER_PAUSED_SURRENDER"] = {
        text = "Surrender your paused ladder duel?\n\nThis counts as a loss. Closing the window keeps the duel paused.",
        button1 = "Surrender", button2 = "Cancel", timeout = 0, whileDead = 0, hideOnEscape = 1,
        OnAccept = function()
            if LU.view and LU.view.st == "paused" and LU.view.duel == LU.surrenderDuel then
                LU.PausedAction("LADDER_GIVE")
            end
            LU.surrenderDuel = nil
        end,
        OnCancel = function() LU.surrenderDuel = nil end,
    }
end
