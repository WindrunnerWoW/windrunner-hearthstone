-- Hearthstone addon: the collection window.
--
-- ONE window in the style of the original Hearthstone collection (merged view). On the left the
-- book (the owner's book background): the class name on its title plate, the class tabs along its top edge,
-- 4 x 2 cards per page with the "x1" / "x2" banner under every owned card, unowned cards dimmed, the page
-- number and the page arrows at the bottom of the page, and a bar below the book with the mana gems, the
-- search box and the rarity / ownership filters. On the right the deck panel (DecksUI.lua): the saved decks
-- as tiles ("My Decks", New Deck, "n/20 Decks"); a click on a tile turns the panel into that deck's card
-- list (with a short slide / fade, no second view) while the book switches to the deck class tab + Neutral
-- and shows only cards a deck of that class can hold: left click adds a copy, right click removes one.
-- The bottom tabs are Collection | Packs (PacksUI.lua, which hides the deck panel). There is no queue,
-- ranked or trading UI. The header strip shows the Hearthstone coins and the dust; while a crafting
-- NPC is open (HsWallet.craftOpen) a click on a card opens the craft panel (CraftUI.lua).
--
-- Textures: the owner's art pieces (art/out/Collection, tools/art/slice_collection.py) are listed in
-- HS_CollectionTextures (CollectionTextures.lua, written by tools/deploy/deploy-addon.ps1). Every piece has
-- a native fallback: without them the window keeps today's look (dark pages with the parchment of the mail
-- stationery, spellbook style class tabs, UIPanelButtons) at the same positions. /hs bookart off (saved in
-- HearthstoneDB.bookArt) shows the plain dark pages instead of the book art.
--
-- Layout (pixels at scale 1, origin = top left of the window): a 24 px header strip ("Hearthstone"), a 38 px band with the class tabs, the
-- 1024 x 704 book below it, a 58 px bar below the book, the 256 x 800 deck panel right of the book. The
-- window is 1280 x 846 (1024 wide on the Packs tab) and is scaled to the screen by UI.FitWindow.
--
-- Textures: the client does not load a texture side above 512 (it draws solid green), so the book, the bar
-- and the deck panel are tiles <name>_c<col>r<row> of at most 512 (UI.TILE_SIZE, tools/art/slice_collection.py);
-- the book art is transparent outside the book, so the window has a plain dark background texture (UI.BG_COLOR; the world would show
-- around the book). Z-order, bottom to top: the class tabs (UI.tabsFrame), then the book art (the page overlaps
-- the lower part of the tabs, like the original), then the cards and controls. A selected tab is raised.
--
-- Filtering, sorting and paging live in CollectionModel.lua; the card picture (with a text fallback for
-- missing textures) is CardWidget.lua. The 8 card slots are created once and reused.
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local CM = HsCollection
local O = HsOwnership
H.CollectionUI = H.CollectionUI or {}
local UI = H.CollectionUI

-- layout
UI.HEADER_H = 34               -- strip above the class tabs: the window title and the close button, clear of the stone frame
UI.FOOT_MARGIN = 12            -- dark margin below the bar and the deck panel, so nothing touches the bottom of the frame
UI.TAB_BAND = 38               -- band above the book (class tabs)
UI.BOOK_W, UI.BOOK_H = 1024, 704
UI.BAR_H = 58                  -- bar below the book (mana gems, search, filters)
UI.SIDE_W = 256                -- deck panel
UI.WIDTH_BOOK = UI.BOOK_W
UI.WIDTH = UI.BOOK_W + UI.SIDE_W                          -- 1280
UI.HEIGHT = UI.HEADER_H + UI.TAB_BAND + UI.BOOK_H + UI.BAR_H + UI.FOOT_MARGIN   -- 846 (the deck panel is 800 tall, below the header)
UI.CARD_W = 172
UI.CARD_H = math.floor(UI.CARD_W * 388 / 256 + 0.5)       -- 261 (CardWidget keeps the 256 x 388 aspect)
UI.SLOT_W, UI.SLOT_H = UI.CARD_W, UI.CARD_H
UI.COL_GAP, UI.ROW_GAP = 14, 14
UI.GRID_X, UI.GRID_Y = 235, 112                           -- first card, relative to the book
UI.PAGES_TOP = -(UI.HEADER_H + UI.TAB_BAND)               -- top of the book in window coordinates
UI.BAR_TOP = UI.PAGES_TOP - UI.BOOK_H
UI.BAR_MID = UI.BAR_TOP - math.floor(UI.BAR_H / 2)
UI.PAGE_X1, UI.PAGE_Y1, UI.PAGE_X2, UI.PAGE_Y2 = 194, 18, 1006, 697   -- the parchment inside the book image
UI.PAGE_CX = 600                                          -- centre of the page
UI.PLATE_Y = 67                                           -- centre of the title plate inside the book
UI.TAB_W = 56
UI.TAB_X0, UI.TAB_PITCH = 196, 62
-- Class tabs: they stick out above the top edge of the page (the page starts 18 px below the book's top, 56 px
-- below the window's top) and their lower part is hidden behind the book art. The selected tab is raised.
UI.TAB_Y = -(UI.HEADER_H + 14)    -- top of a tab (14 px below the band's top): 42 px show above the page edge, 14 are hidden
UI.TAB_Y_UP = -(UI.HEADER_H + 6)  -- top of the selected tab: 8 px higher (6 px of it are still behind the page)
UI.FOOT_Y = 668                -- centre of the page arrows, the page number and the card counts, from the book's top
UI.EDGE_ARROW_W, UI.EDGE_ARROW_H, UI.EDGE_ARROW_Y = 64, 32, 0   -- the hover arrow in the page margins: 2:1 like PageArrow.blp (512 x 256), so the art keeps its aspect ratio
UI.TABS_OVERHANG = 8          -- gap between the window and the enlarged card
UI.BOTTOM_OVERHANG = 34        -- bottom tabs stick out below the window
UI.BTAB_W, UI.BTAB_H, UI.BTAB_X0, UI.BTAB_PITCH = 150, 38, 30, 156   -- the bottom tabs (Collection, Packs)
UI.BTAB_Y, UI.BTAB_Y_UP = UI.FOOT_MARGIN - 1, UI.FOOT_MARGIN - 1    -- px the tab top is above the window's bottom edge (the same place relative to the bar as before the margin): the selected tab keeps its place
UI.BG_COLOR = { 0.08, 0.035, 0.03, 1 }   -- the window's own background (dark red brown), behind the book art
-- the stone silver frame around the window: the dialog border of the client, edge only, tinted light grey like the
-- window of the companion recruiter addon
UI.FRAME_EDGE = "Interface\\DialogFrame\\UI-DialogBox-Border"
UI.FRAME_TINT = { 0.84, 0.84, 0.86, 1 }
-- Where the frame sits, in px beyond the window's edge per side: 0 = on the window's edge, a bigger number moves
-- that side outwards, a negative one inwards. The dark background always follows the frame (it ends at the
-- outer edge of the metal, UI.FRAME_METAL px inside the frame's rectangle), so nothing dark sticks out around it.
-- The window itself (its size, its art) does not change with these.
UI.FRAME_PAD = { left = 0, top = 0, right = 0, bottom = 0 }
UI.FRAME_METAL = 5
UI.CLOSE_X, UI.CLOSE_Y = -11, -6       -- the close button's top right corner from the window's top right corner (px; y negative = down)
-- the plus on the 7 gem ("7+"): the small badge of the sheet keeps its size and place, a bigger white plus is drawn on it
UI.PLUS_W, UI.PLUS_H = 18, 17
UI.PLUS_X, UI.PLUS_Y = 5, 3            -- how far the badge hangs out of the gem's top right corner
UI.PLUS_ARM, UI.PLUS_THICK = 6, 2     -- length and thickness of the drawn plus (px)
UI.BANNER_W, UI.BANNER_H = 52, 39
UI.LIMIT_CONTENT_W = 556       -- the plate's bar inside its 512 x 256 texture, in units of the visible card rect: the bar is the card's own frame (about 203 of the 226 px rect), not the rect with its rim
UI.LIMIT_BAR_BOTTOM = 137      -- the plate's bar bottom edge inside the texture, from its top
UI.LIMIT_FOOT_GAP = 4          -- the bar ends this far above the card's bottom edge
UI.BANNER_OVERLAP = 8         -- the x1 / x2 banner starts this far above the card's bottom edge, under the card
-- The same banner without its text (ui_icons.py --only banner), for the counts the sheet has no banner for: the
-- count is written on it as text. Same canvas and plate as the sheet's banners.
UI.BANNER_BLANK = "Interface\\AddOns\\Hearthstone\\Textures\\UI\\BannerBlank"
UI.BANNER_BLANK_RIGHT, UI.BANNER_BLANK_BOTTOM = 0.9375, 0.703125
UI.BANNER_FONT = "Fonts\\FRIZQT__.TTF"
UI.BANNER_INK = { 0.2, 0.12, 0.05 }
-- The count on the blank banner ("x3", "x12", "x345") is drawn with the numerals of the "x2" banner's typeface: one atlas
-- of the digits and the x (ui_icons.py --only digits writes the table below). The font above is only the fallback for
-- a client without the atlas. Boxes are pixels { x0, y0, x1, y1 } of the 512 x 64 atlas; the digits are 34 px high
-- with their baseline at row 40, the x sits on the same baseline.
UI.BANNER_DIGITS = "Interface\\AddOns\\Hearthstone\\Textures\\UI\\BannerDigits"
UI.GLYPH_ATLAS_W, UI.GLYPH_ATLAS_H = 512, 64
UI.GLYPH_DIGIT_H, UI.GLYPH_BASELINE, UI.GLYPH_GAP = 34, 40, 2
UI.GLYPHS = {
    ["0"] = { 4, 6, 28, 40 },
    ["1"] = { 40, 6, 55, 40 },
    ["2"] = { 68, 6, 91, 40 },
    ["3"] = { 100, 6, 124, 40 },
    ["4"] = { 132, 6, 155, 40 },
    ["5"] = { 164, 6, 188, 40 },
    ["6"] = { 196, 6, 220, 40 },
    ["7"] = { 229, 6, 250, 40 },
    ["8"] = { 260, 6, 283, 40 },
    ["9"] = { 292, 6, 316, 40 },
    ["x"] = { 323, 12, 348, 37 },
}
-- Where the text sits on the banner's 60 x 45 plate (the "x2" of the sheet): centred on x = 29.5, baseline at y = 26.4,
-- digits 15.5 high and at most 25.5 wide (longer counts shrink to fit), all in plate px.
UI.PLATE_W = 60
UI.TEXT_CX, UI.TEXT_BASE, UI.TEXT_H, UI.TEXT_MAXW = 29.5, 26.4, 15.5, 25.5
-- UI.GLOW_PAD is only the fallback for a glow piece that has no entry in UI.GLOW_LINE (none today): the pieces draw
-- a thin line about 15 (left, top), 22 (right) and 18 (bottom) px inside their edge
UI.GLOW_PAD = { left = 15, top = 15, right = 22, bottom = 18 }
-- TO MOVE THE HOVER GLOW: change UI.GLOW_ADJUST. Per piece (glow_blue = the hover glow, glow_gold = a card in the deck
-- being edited) and side, in px: positive moves that side of the glow outwards (away from the card), negative inwards.
UI.GLOW_ADJUST = {
    glow_blue = { left = -8, top = -12, right = -4, bottom = -4 },
    glow_gold = { left = -8, top = -12, right = -4, bottom = -4 },
}
-- Where the glow line really is inside each 200 x 300 glow piece (pixel centres of the line, measured on the
-- pieces: left / right in x, top / bottom in y) and how far its centre sits outside the visible card edge ('gap',
-- about half the line's width, so the card covers the inner half and the line is flush with the card). The piece
-- is stretched over the card, so UI.GlowRect places the line itself, not the piece's edge.
UI.GLOW_LINE = {
    glow_blue = { w = 200, h = 300, left = 18, right = 175, top = 19, bottom = 280.5, gap = 1.5 },
    glow_gold = { w = 200, h = 300, left = 21, right = 176.5, top = 18, bottom = 286, gap = 3 },
}
-- The dust in the header strip: the bottle (Textures\UI\DustBottle, tools/art/ui_icons.py; the stock dust icon
-- when it is not deployed) and the number. The coins are not shown (they are items in the bags).
UI.WALLET_X, UI.WALLET_W = 28, 90
UI.WALLET_DY = 20                -- the dust sits this far below the middle of the header strip
UI.DUST_ICON = "Interface\\Icons\\INV_Enchant_DustDream"
UI.DUST_BOTTLE = "Interface\\AddOns\\Hearthstone\\Textures\\UI\\DustBottle"
UI.BOTTLE_RIGHT = 0.609375     -- width / height of the bottle on its 256 x 256 texture (ui_icons.py prints it)
UI.BOTTLE_H = 26               -- height of the bottle in the header strip
UI.PREVIEW_W = 272
UI.PREVIEW_H = 480
UI.CLEAR_LEFT = 0              -- the default position is centred (a number keeps it right of that many px, e.g. 230 for the player / target frames)
local SLOT_W, SLOT_H = UI.SLOT_W, UI.SLOT_H
local COL_GAP, ROW_GAP = UI.COL_GAP, UI.ROW_GAP
local CARD_W = UI.CARD_W
local WHITE = "Interface\\Buttons\\WHITE8X8"

-- Native fallback of the book pages: the parchment of the mail stationery (checked to exist in the owner's
-- client MPQs) over the plain dark page; /hs bookart off goes back to the plain dark page.
UI.BOOK_ART = {
    page = "Interface\\Stationery\\StationeryTest1",
}
UI.PAGE_INK = { 0.25, 0.14, 0.04 }    -- dark brown text over parchment (gold text is unreadable there)
UI.BOOK_TILES = { "book_c0r0", "book_c1r0", "book_c0r1", "book_c1r1", "book_c0r2", "book_c1r2" }
UI.BAR_TILES = { "bar_main_c0r0", "bar_main_c1r0" }
-- texture size of one tile (the last row / column of a piece may be smaller): the slicer's "tile" / "split"
UI.PACKS_BG_TILES = { "packs_bg_c0r0", "packs_bg_c1r0", "packs_bg_c0r1", "packs_bg_c1r1", "packs_bg_c0r2", "packs_bg_c1r2",
    "packs_bg_c0r3", "packs_bg_c1r3" }      -- the Packs page: 1024 x 812 below the header (PacksUI.lua)
UI.HEROES_BG_TILES = { "heroes_bg_c0r0", "heroes_bg_c1r0", "heroes_bg_c0r1", "heroes_bg_c1r1", "heroes_bg_c0r2", "heroes_bg_c1r2" }    -- the Heroes page: the parchment with its own "Heroes" ribbon (HeroesUI.lua)
UI.TILE_SIZE = { book = { 512, 256 }, bar_main = { 512, 64 }, deck_panel = { 256, 256 }, packs_bg = { 512, 256 }, heroes_bg = { 512, 256 } }
UI.TAB_PIECES = {                      -- class tab key -> collection piece
    DRUID = "tab_druid", HUNTER = "tab_hunter", MAGE = "tab_mage", PALADIN = "tab_paladin", PRIEST = "tab_priest",
    ROGUE = "tab_rogue", SHAMAN = "tab_shaman", WARLOCK = "tab_warlock", WARRIOR = "tab_warrior", NEUTRAL = "tab_neutral",
}

UI.HEROES_DROP = 19                    -- the Heroes book sits this far below the collection book, so the class tabs show whole
UI.HERO_TAB_KEYS = { "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR" }   -- the class tabs of the Heroes tab
UI.BOTTOM_TABS = { "Collection", "Packs", "Heroes" }
UI.TITLES = { "Card Catalog", "Card Packs", "Hero Portraits" }
UI.HEROES_TAB = 3                      -- hidden until the account has an unlock or an unspent credit (HsHeroes.TabVisible)
UI.UNAVAILABLE = {
    nil,
    "The pack window could not be loaded. Reinstall the Hearthstone addon.",   -- only without PacksUI.lua
    "The hero portraits could not be loaded. Reinstall the Hearthstone addon.", -- only without HeroesUI.lua
}

UI.frame = nil
UI.slots = {}
UI.classTabs = {}
UI.manaButtons = {}
UI.hover = nil
UI.layoutMode = nil              -- "normal", "packs", "heroes" or "edit:<CLASS>" (see UI.ApplyLayout)
UI.state = {
    bottom = 1,                   -- 1 Collection, 2 Packs
    savedClass = nil,             -- the class tab of the browsing mode while the edit mode shows the deck class
    filters = CM.NewFilters(),
    page = 1,
    results = {},
    pageIds = {},
    catalog = nil,
    catalogFor = nil,
    inited = false,
}

-- ----------------------------------------------------------- collection pieces
-- The piece 'name' of HS_CollectionTextures (path, w, h, right, bottom), or nil.
function UI.Piece(name)
    -- the icy Done / Choose plates (ButtonTextures.lua) replace the sheet's own btn_done
    local own = type(HS_ButtonTextures) == "table" and HS_ButtonTextures[name]
    if type(own) == "table" and type(own.path) == "string" and type(own.right) == "number" and type(own.bottom) == "number" then
        return own
    end
    local t = HS_CollectionTextures
    if type(t) ~= "table" or type(t.pieces) ~= "table" then
        return nil
    end
    local p = t.pieces[name]
    if type(p) == "table" and type(p.path) == "string" and type(p.right) == "number" and type(p.bottom) == "number" then
        return p
    end
    return nil
end

-- Our own glow behind a Done / Choose plate: a soft halo (btn_glow) layered under the plate, drawn at twice the
-- plate's size and pulsing slowly. Nothing happens without the art.
function UI.ButtonGlow(button)
    local g = UI.Piece("btn_glow")
    if not g or not button.CreateTexture then
        return nil
    end
    if button.glow then
        return button.glow   -- one halo and one pulse per button, however often this is called
    end
    local t = button:CreateTexture(nil, "BACKGROUND")
    t:SetTexture(g.path)
    t:SetTexCoord(0, g.right, 0, g.bottom)
    t:SetBlendMode("ADD")
    t:SetPoint("CENTER", button, "CENTER", 0, 0)
    t:SetWidth(button:GetWidth() * 2)
    t:SetHeight(button:GetHeight() * 2)
    local pulse = CreateFrame("Frame", nil, button)
    pulse:SetScript("OnUpdate", function()
        t:SetAlpha(0.55 + 0.25 * math.sin(GetTime() * 2.4))
    end)
    button.glow = t
    return t
end

-- Puts the piece on a texture region. Returns the piece, or nil when it is not deployed.
function UI.SetPiece(tex, name)
    local p = UI.Piece(name)
    if p and tex then
        tex:SetTexture(p.path)
        tex:SetTexCoord(0, p.right, 0, p.bottom)
    end
    return p
end

-- The rectangle for glow piece 'name' so that its line runs along the visible card rectangle (vl, vt, vr, vb as
-- in CardWidget.VisibleRect, measured from the slot's top left): returns left, top, right, bottom as offsets
-- from the slot's top left corner (y counts down). The piece is stretched over the rectangle, so the pads
-- depend on the size of the card and are not the same on every side.
function UI.GlowRect(name, vl, vt, vr, vb)
    local g = UI.GLOW_LINE[name]
    if not g then
        local p = UI.GLOW_PAD
        return vl - p.left, vt - p.top, vr + p.right, vb + p.bottom
    end
    local a = UI.GLOW_ADJUST[name] or { left = 0, top = 0, right = 0, bottom = 0 }
    local tl, tt, tr, tb = vl - g.gap - a.left, vt - g.gap - a.top, vr + g.gap + a.right, vb + g.gap + a.bottom
    local sx = (tr - tl) / (g.right - g.left)
    local sy = (tb - tt) / (g.bottom - g.top)
    local l = tl - g.left * sx
    local t = tt - g.top * sy
    return l, t, l + g.w * sx, t + g.h * sy
end

-- Sets glow piece 'name' on texture 'tex' and anchors it around the visible card of 'slot' (UI.GlowRect).
function UI.PlaceGlow(tex, name, slot, vl, vt, vr, vb)
    if not UI.SetPiece(tex, name) then
        return false
    end
    local l, t, r, b = UI.GlowRect(name, vl, vt, vr, vb)
    tex:SetPoint("TOPLEFT", slot, "TOPLEFT", l, -t)
    tex:SetPoint("BOTTOMRIGHT", slot, "TOPLEFT", r, -b)
    return true
end

-- Are all the pieces of 'names' deployed?
function UI.TilesDeployed(names)
    for i = 1, table.getn(names) do
        if not UI.Piece(names[i]) then
            return false
        end
    end
    return true
end

-- Is the book art deployed (all its tiles)?
function UI.BookTextured()
    return UI.TilesDeployed(UI.BOOK_TILES)
end

-- One texture per tile of a piece that is cut into tiles (names <base>_c<col>r<row>, tile size UI.TILE_SIZE[base];
-- the client does not load a texture side above 512), laid out from x, y (the top left corner, in the parent's
-- coordinates, y counts up like in SetPoint). Returns the textures, or nil when a tile is not deployed.
function UI.PlaceTiles(parent, base, names, x, y, layer)
    if not UI.TilesDeployed(names) then
        return nil
    end
    local size = UI.TILE_SIZE[base]
    local out = {}
    for i = 1, table.getn(names) do
        local p = UI.Piece(names[i])
        local _, _, c, r = string.find(names[i], "_c(%d+)r(%d+)$")
        local tex = parent:CreateTexture(nil, layer or "BACKGROUND")
        tex:SetTexture(p.path)
        tex:SetTexCoord(0, p.right, 0, p.bottom)
        tex:SetWidth(p.w)
        tex:SetHeight(p.h)
        tex:SetPoint("TOPLEFT", parent, "TOPLEFT", x + tonumber(c) * size[1], y - tonumber(r) * size[2])
        out[i] = tex
    end
    return out
end

-- ------------------------------------------------------------- logic
-- (frame free: these work without a window; they refresh it when it exists)
function UI.EnsureCatalog()
    local s = UI.state
    local db = CM.GetDB()
    if db == nil then
        s.catalog = nil
        s.catalogFor = nil
        return nil
    end
    if s.catalogFor ~= db then
        s.catalog = CM.NewCatalog(db)
        s.catalogFor = db
    end
    return s.catalog
end

-- Number of catalog cards the player owns (0 while ownership is not ready).
function UI.OwnedCount()
    local cat = UI.state.catalog
    if not cat then
        return 0
    end
    return O.CountOwned(cat.ids)
end

-- Text for the note (status) line at the bottom of the page: (text, isProblem).
function UI.NoteText()
    if not CM.GetDB() then
        return "Card data (CardDB.lua) is missing - reinstall the Hearthstone addon.", true
    end
    if H.state and H.state.datasetStatus == "mismatch" then
        return "Card data does not match the server - update the addon or the server.", true
    end
    local st = O.state
    if st == "ready" then
        local cat = UI.state.catalog
        return "Collection: " .. UI.OwnedCount() .. " of " .. (cat and cat.total or 0) .. " cards", false
    elseif st == "loading" then
        return "Loading collection...", false
    elseif st == "error" then
        return "Collection unavailable", true
    end
    return "Catalog view (not connected)", false
end

-- Message shown over the empty book pages, or nil.
function UI.EmptyText()
    if not CM.GetDB() then
        return "The card database could not be loaded.\nReinstall the Hearthstone addon."
    end
    if table.getn(UI.state.results) == 0 then
        if O.IsReady() and UI.state.filters.own == "OWNED" and UI.OwnedCount() == 0 then
            return "You do not own any cards yet."
        end
        return "No cards match your filters."
    end
    return nil
end

-- What the window opens on: "ALL" (all cards, no class tab selected) or "PLAYER" (the tab saved last time, else the
-- tab of the character's class).
UI.START_TAB = "ALL"

function UI.InitState()
    local s = UI.state
    local key = "ALL"
    if UI.START_TAB == "PLAYER" then
        key = H.db and H.db.tab
        if not key or key == "ALL" then
            local token = nil
            if UnitClass then
                local _, t = UnitClass("player")
                token = t
            end
            key = CM.TabForClassToken(token) or "NEUTRAL"
        end
    end
    s.filters.class = key
    s.inited = true
end

-- The filters the query uses: a search looks through every class (the class tab is ignored while a search
-- text is set; in the edit mode the deck class filter still limits it to cards the deck can hold).
function UI.QueryFilters()
    local f = UI.state.filters
    if f.class ~= "ALL" and CM.Trim(f.search) ~= "" then
        return { class = "ALL", mana = f.mana, rarity = f.rarity, search = f.search, own = f.own, deckClass = f.deckClass }
    end
    return f
end

-- Recomputes the result list (call after a filter changed) and shows the first page.
function UI.Requery(keepPage)
    local s = UI.state
    if not s.inited then
        UI.InitState()
    end
    local owned = nil
    if O.IsReady() then
        owned = O.owned
    end
    s.results = CM.Query(UI.EnsureCatalog(), UI.QueryFilters(), owned)
    if not keepPage then
        s.page = 1
    end
    UI.RefreshAll()
end

function UI.PageCount()
    return CM.PageCount(table.getn(UI.state.results), CM.PER_PAGE)
end

function UI.SetClass(key)
    if not CM.TabIndex(key) then
        return
    end
    if UI.IsEditView() then
        -- the edit view offers the deck class and Neutral only, and is not remembered
        local dr = HsDecks.draft
        if key ~= dr.class and key ~= "NEUTRAL" and key ~= "ALL" then
            return
        end
        UI.state.filters.class = key
        UI.Requery()
        return
    end
    UI.state.filters.class = key
    if H.db then
        H.db.tab = key
    end
    UI.Requery()
end

function UI.ToggleMana(n)
    local m = UI.state.filters.mana
    if m[n] then
        m[n] = nil
    else
        m[n] = true
    end
    UI.Requery()
end

-- The glowing "all" gem: no mana filter.
function UI.ClearMana()
    UI.state.filters.mana = {}
    UI.Requery()
end

function UI.SetRarity(key)
    if not CM.RarityIndex(key) then
        return
    end
    UI.state.filters.rarity = key
    UI.Requery()
end

-- Ownership filter. Owned / Not owned need the collection; without it the filter stays All.
function UI.SetOwnFilter(key)
    if not CM.OwnIndex(key) then
        return
    end
    if key ~= "ALL" and not O.IsReady() then
        key = "ALL"
    end
    UI.state.filters.own = key
    UI.Requery()
    if H.CraftUI and H.CraftUI.RefreshSide then
        H.CraftUI.RefreshSide()       -- the Owned / Missing checkboxes of the craft panel
    end
end

-- Ownership state or data changed (called from Hearthstone.lua): falls back to All while
-- ownership is not ready and redraws the open window, keeping the page where possible.
function UI.OnOwnershipChanged()
    local s = UI.state
    if not O.IsReady() and s.filters.own ~= "ALL" then
        s.filters.own = "ALL"
    end
    if UI.frame and UI.frame:IsShown() then
        UI.Requery(true)
    end
    if H.CraftUI and H.CraftUI.Refresh then
        H.CraftUI.Refresh()           -- the owned copies of the card in the craft panel
    end
end

function H.OnOwnershipChanged()
    UI.OnOwnershipChanged()
end

function UI.SetSearch(text)
    text = tostring(text or "")
    if UI.state.filters.search == text then
        return
    end
    UI.state.filters.search = text
    UI.Requery()
end

function UI.ResetFilters()
    CM.ResetFilters(UI.state.filters)
    UI.Requery()
end

function UI.ChangePage(delta)
    local s = UI.state
    local page = CM.ClampPage(s.page + delta, UI.PageCount())
    if page ~= s.page then
        s.page = page
        UI.RefreshPage()
    end
end

-- Mouse wheel over the book: up = previous page, down = next page.
function UI.OnWheel(delta)
    if not UI.BookShown() then
        return
    end
    delta = tonumber(delta) or 0
    if delta > 0 then
        UI.ChangePage(-1)
    elseif delta < 0 then
        UI.ChangePage(1)
    end
end

-- Shows tab 'id' as the selected bottom tab. The native tabs (without the art) go through the client's
-- PanelTemplates_SetTab; the art tabs (buttons with the sheet's plate) are drawn here: the selected one is
-- bright and raised, the others dimmer. PanelTemplates_SetTab must not run for them (it wants the template's
-- Left / Middle / Right textures).
function UI.HeroesTabVisible()
    return HsHeroes ~= nil and HsHeroes.TabVisible()
end

function UI.StyleBottomTabs(id)
    local f = UI.frame
    if not f then
        return
    end
    local heroTab = getglobal("HearthstoneFrameTab" .. UI.HEROES_TAB)
    if heroTab then
        if UI.HeroesTabVisible() then
            heroTab:Show()
        else
            heroTab:Hide()
        end
    end
    if f.nativeTabs then
        if PanelTemplates_SetTab then
            PanelTemplates_SetTab(f, id)
        end
        return
    end
    f.selectedTab = id
    for i = 1, table.getn(UI.BOTTOM_TABS) do
        local b = getglobal("HearthstoneFrameTab" .. i)
        if b and b.label then
            local on = (i == id)
            b:ClearAllPoints()
            b:SetPoint("TOPLEFT", f, "BOTTOMLEFT", UI.BTAB_X0 + (i - 1) * UI.BTAB_PITCH, on and UI.BTAB_Y_UP or UI.BTAB_Y)
            local nt = b:GetNormalTexture()
            if nt then
                if on then
                    nt:SetVertexColor(1, 1, 1)
                else
                    nt:SetVertexColor(0.62, 0.58, 0.55)
                end
            end
            -- both tabs use the ink colour: a paler text on the dimmed plate of the other tab was unreadable
            b.label:SetTextColor(UI.PAGE_INK[1], UI.PAGE_INK[2], UI.PAGE_INK[3])
            b.selected = on
        end
    end
end

function UI.SetBottomTab(id)
    local s = UI.state
    if id == UI.HEROES_TAB and not UI.HeroesTabVisible() then
        id = 1                    -- the Heroes tab is hidden until there is something in it
    end
    s.bottom = id
    local f = UI.frame
    if not f then
        return
    end
    UI.StyleBottomTabs(id)
    UI.ShowBookTiles()
    UI.ClearHover()
    UI.HidePreview()
    if H.PacksUI and H.PacksUI.ClearHover then
        H.PacksUI.ClearHover()
    end
    if H.DecksUI and H.DecksUI.ClearHover then
        H.DecksUI.ClearHover()
    end
    if H.HeroesUI and H.HeroesUI.ClearHover then
        H.HeroesUI.ClearHover()
    end
    if id == 1 and f.sidePanel and H.DecksUI then
        H.DecksUI.OnTabShown()
    end
    UI.Update()
    if id == 2 and f.packs and H.PacksUI then
        H.PacksUI.OnTabShown()
    end
    if id == UI.HEROES_TAB and f.heroes and H.HeroesUI then
        H.HeroesUI.OnTabShown()
    end
end

-- ------------------------------------------------------------- edit view
-- The collection tab while a deck draft exists: the book shows the deck class + Neutral, the deck panel
-- shows the deck's card list.
function UI.IsEditView()
    return UI.state.bottom == 1 and H.DecksUI ~= nil and HsDecks ~= nil and HsDecks.draft ~= nil
        and UI.frame ~= nil and UI.frame.sidePanel ~= nil
end

-- Is the card book on screen (the collection tab)?
function UI.BookShown()
    return UI.state.bottom == 1
end

-- Width of the window as it is now: the Packs and Heroes tabs have no deck panel.
function UI.WindowWidth()
    if UI.state.bottom == 2 or UI.state.bottom == UI.HEROES_TAB then
        return UI.WIDTH_BOOK
    end
    return UI.WIDTH
end

-- Shows the class tabs whose key is in 'keys' (all when nil), from the left of the band.
function UI.LayoutClassTabs(keys)
    local pos = 0
    for i = 1, table.getn(UI.classTabs) do
        local t = UI.classTabs[i]
        local show = true
        if keys then
            show = false
            for k = 1, table.getn(keys) do
                if t.key == keys[k] then
                    show = true
                end
            end
        end
        t.shown = show
        if show then
            pos = pos + 1
            t.x = UI.TAB_X0 + (pos - 1) * UI.TAB_PITCH
            UI.PlaceTab(t, false)
            t.button:Show()
        else
            t.button:Hide()
        end
    end
end

-- The selected tab is raised (it sticks out higher above the page; the others sit lower, partly behind the book).
function UI.PlaceTab(t, selected)
    local main = UI.frame
    if not main or not t.x then
        return
    end
    t.button:ClearAllPoints()
    t.button:SetPoint("TOPLEFT", main, "TOPLEFT", t.x, selected and UI.TAB_Y_UP or UI.TAB_Y)
end

-- Scale that fits the window (full width, also on the Packs tab) on the screen; keeps the top left corner.
function UI.FitWindow()
    local main = UI.frame
    if not main then
        return
    end
    local screenW, screenH = 1024, 768
    if GetScreenWidth then
        screenW, screenH = GetScreenWidth(), GetScreenHeight()
    end
    local scale = CM.FitScale(screenW, screenH, UI.WIDTH + UI.TABS_OVERHANG, UI.HEIGHT + UI.BOTTOM_OVERHANG)
    local old = main:GetScale() or 1
    if scale ~= old then
        local l, t = main:GetLeft(), main:GetTop()
        main:SetScale(scale)
        if l and t then
            main:ClearAllPoints()
            main:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", l * old / scale, t * old / scale)
        end
    end
    UI.KeepTabsOnScreen()
end

-- Keeps the whole window (also the part that the current tab hides) on the screen.
function UI.KeepTabsOnScreen()
    local main = UI.frame
    if not main or not UIParent or not UIParent.GetWidth then
        return
    end
    local l, t = main:GetLeft(), main:GetTop()
    local scale = main:GetScale() or 1
    local screen = UIParent:GetWidth()
    if not l or not t or not screen or screen <= 0 then
        return
    end
    local limit = screen / scale - UI.WindowWidth()
    if l > limit then
        if limit < 0 then
            limit = 0
        end
        main:ClearAllPoints()
        main:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", limit, t)
    end
end

-- Switches between the browsing mode, the deck edit mode and the Packs tab (window width, class tabs, class
-- filter). Returns true when the mode changed (the results must then be recomputed). 'force' re-applies the
-- layout without touching the scale (UI.OnShow sets the scale itself).
function UI.ApplyLayout(force)
    local main = UI.frame
    if not main then
        return false
    end
    local s = UI.state
    local edit = UI.IsEditView()
    local mode = "normal"
    if s.bottom == 2 then
        mode = "packs"
    elseif s.bottom == UI.HEROES_TAB then
        mode = "heroes"
    elseif edit then
        mode = "edit:" .. HsDecks.draft.class
    end
    if mode == UI.layoutMode and not force then
        return false
    end
    local changed = mode ~= UI.layoutMode
    UI.layoutMode = mode
    -- a tab with another window width (Packs is narrower) keeps the window's centre where it was
    local oldW, newW = main:GetWidth(), UI.WindowWidth()
    main:SetWidth(newW)
    if oldW and oldW ~= newW and main:IsShown() then
        local l, t = main:GetLeft(), main:GetTop()
        if l and t then
            main:ClearAllPoints()
            main:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", l + (oldW - newW) / 2, t)
        end
    end
    if edit then
        local class = HsDecks.draft.class
        UI.LayoutClassTabs({ class, "NEUTRAL" })
        if s.savedClass == nil then
            s.savedClass = s.filters.class
        end
        -- a new edit view opens on the deck class tab
        if changed or (s.filters.class ~= class and s.filters.class ~= "NEUTRAL" and s.filters.class ~= "ALL") then
            s.filters.class = class
        end
        s.filters.deckClass = class
    else
        UI.LayoutClassTabs(mode == "heroes" and UI.HERO_TAB_KEYS or nil)
        s.filters.deckClass = nil
        if s.savedClass ~= nil then
            s.filters.class = s.savedClass
            s.savedClass = nil
        end
    end
    if not force then
        UI.FitWindow()
    end
    return changed
end

-- Redraws the window after a tab or deck change, switching the layout when needed.
function UI.Update()
    if UI.ApplyLayout() then
        UI.ClearHover()
        UI.HidePreview()
        UI.Requery()
    else
        UI.RefreshAll()
    end
end

-- ----------------------------------------------------------- refreshing
-- The class name on the title plate ("Neutral", "Druid"), "Search" while a search text is set.
function UI.TitleText()
    local s = UI.state
    if s.bottom >= 2 then
        return UI.TITLES[s.bottom]
    end
    if CM.Trim(s.filters.search) ~= "" then
        return "Search"
    end
    return CM.TabLabel(s.filters.class)
end

-- Puts the dust bottle into a texture, height pixels high (the stock dust icon when the bottle is not deployed).
-- Returns true for the bottle.
function UI.SetBottle(tex, height)
    if tex:SetTexture(UI.DUST_BOTTLE) then
        tex:SetTexCoord(0, UI.BOTTLE_RIGHT, 0, 1)
        tex:SetWidth(math.floor(height * UI.BOTTLE_RIGHT + 0.5))
        tex:SetHeight(height)
        return true
    end
    tex:SetTexture(UI.DUST_ICON)
    tex:SetTexCoord(0, 1, 0, 1)
    tex:SetWidth(height)
    tex:SetHeight(height)
    return false
end

-- The dust in the header strip (shown once the wallet is known; not on the Packs tab, and not in crafting
-- mode, where the craft panel shows it on its bottle).
function UI.WalletText()
    local W = HsWallet
    if not W or W.state ~= "ready" then
        return nil
    end
    return tostring(W.dust)
end

function UI.RefreshWallet()
    local f = UI.frame
    if not f or not f.wallet then
        return
    end
    local dust = UI.WalletText()
    if dust and not UI.IsCraftMode() and UI.state.bottom == 1 then
        f.wallet.dustText:SetText(dust)
        f.wallet:Show()
    else
        f.wallet:Hide()
    end
end

-- Crafting mode (a crafting NPC is open) on the collection tab outside the deck editor.
function UI.IsCraftMode()
    return HsWallet ~= nil and HsWallet.craftOpen and UI.state.bottom == 1 and not UI.IsEditView()
end

function UI.RefreshHeader()
    local f = UI.frame
    if not f then
        return
    end
    local s = UI.state
    f.title:SetText(s.bottom == UI.HEROES_TAB and "" or UI.TitleText())    -- the Heroes parchment has its own "Heroes" ribbon
    UI.RefreshWallet()
    if s.bottom ~= 1 then
        f.note:SetText("")
        return
    end
    if UI.IsEditView() then
        if HsWallet and HsWallet.craftOpen then
            f.note:SetText("Left click adds a copy, right click removes one. Finish the deck to craft.")
        else
            f.note:SetText("Left click adds a copy, right click removes one.")
        end
        return
    end
    local text, problem = UI.NoteText()
    if problem then
        f.note:SetText("|cffff5555" .. text .. "|r")
    else
        f.note:SetText(text)
    end
end

function UI.RefreshTabs()
    local key = UI.state.filters.class
    if UI.state.bottom == UI.HEROES_TAB and H.HeroesUI then
        key = H.HeroesUI.class                      -- the Heroes tab: the tab of the class whose portraits are shown
    end
    for i = 1, table.getn(UI.classTabs) do
        local t = UI.classTabs[i]
        local on = (t.key == key)
        if on then
            t.button:SetChecked(1)
        else
            t.button:SetChecked(nil)
        end
        if t.shown then
            UI.PlaceTab(t, on)
        end
        if t.textured then
            local nt = t.button:GetNormalTexture()
            if nt then
                if on then
                    nt:SetVertexColor(1, 1, 1)
                else
                    nt:SetVertexColor(0.78, 0.78, 0.78)
                end
            end
        end
    end
end

function UI.RefreshFilters()
    local fl = UI.state.filters
    for n = 0, CM.MANA_MAX do
        local b = UI.manaButtons[n]
        if b then
            local label = tostring(n)
            if n == CM.MANA_MAX then
                label = label .. "+"
            end
            local on = fl.mana[n] and true or false
            if on then
                b:LockHighlight()
            else
                b:UnlockHighlight()
            end
            if b.textured then
                local nt = b:GetNormalTexture()
                if nt then
                    if on then
                        nt:SetVertexColor(1, 1, 1)
                    else
                        nt:SetVertexColor(0.62, 0.62, 0.62)
                    end
                end
            elseif on then
                b:SetText("|cff33ff33" .. label .. "|r")
            else
                b:SetText(label)
            end
        end
    end
    local all = UI.manaAll
    if all then
        local none = (CM.ManaCount(fl) == 0)
        if all.textured then
            local nt = all:GetNormalTexture()
            if nt then
                if none then
                    nt:SetVertexColor(1, 1, 1)
                else
                    nt:SetVertexColor(0.62, 0.62, 0.62)
                end
            end
        end
        if none then
            all:LockHighlight()
        else
            all:UnlockHighlight()
        end
    end
    local f = UI.frame
    if f and f.rarityDropDown and UIDropDownMenu_SetText then
        local i = CM.RarityIndex(fl.rarity) or 1
        UIDropDownMenu_SetText(CM.RARITIES[i].label, f.rarityDropDown)
    end
    if f and f.ownDropDown and UIDropDownMenu_SetText then
        local i = CM.OwnIndex(fl.own) or 1
        UIDropDownMenu_SetText(CM.OWN_FILTERS[i].label, f.ownDropDown)
    end
end

-- Border / glow of a card slot: 'hover' (blue) and 'inDeck' (gold, a copy is in the deck being edited).
function UI.SetSlotLook(slot, hover, inDeck)
    if slot.glowBlue then
        if hover then
            slot.glowBlue:Show()
        else
            slot.glowBlue:Hide()
        end
    end
    if slot.glowGold then
        if inDeck then
            slot.glowGold:Show()
        else
            slot.glowGold:Hide()
        end
    end
    if slot.glowBlue then
        slot:SetBackdropBorderColor(1, 0.82, 0, 0)       -- the glow replaces the border
    elseif hover then
        slot:SetBackdropBorderColor(1, 0.82, 0, 1)
    elseif inDeck then
        slot:SetBackdropBorderColor(0.4, 0.6, 1, 1)
    else
        slot:SetBackdropBorderColor(1, 0.82, 0, 0)
    end
end

-- Where the glyphs of str ("x3", "x12") go on a banner whose plate is f frame px per plate px: a list, one entry per
-- glyph, { box, left, bottom, w, h } in frame px from the banner's top left (bottom = the glyph's bottom edge, down from the
-- top), or nil for a character without a glyph. The whole text is centred on the plate and shrunk when it is too wide.
function UI.GlyphLayout(str, f)
    local n = string.len(str)
    local unit = UI.TEXT_H * f / UI.GLYPH_DIGIT_H          -- frame px per atlas px
    local boxes, total = {}, UI.GLYPH_GAP * (n - 1)
    for i = 1, n do
        local box = UI.GLYPHS[string.sub(str, i, i)]
        if not box then
            return nil
        end
        boxes[i] = box
        total = total + (box[3] - box[1])
    end
    local fit = 1
    local maxw = UI.TEXT_MAXW * f / unit                   -- in atlas px
    if total > maxw then
        fit = maxw / total
    end
    local u = unit * fit
    local left = UI.TEXT_CX * f - total * u / 2
    local base = UI.TEXT_BASE * f
    local out = {}
    for i = 1, n do
        local box = boxes[i]
        local w, h = (box[3] - box[1]) * u, (box[4] - box[2]) * u
        out[i] = { box = box, left = left, bottom = base - (UI.GLYPH_BASELINE - box[4]) * u, w = w, h = h }
        left = left + w + UI.GLYPH_GAP * u
    end
    return out
end

-- Hides the glyphs a banner has drawn.
function UI.HideGlyphs(banner)
    local glyphs = banner and banner.hsGlyphs
    if glyphs then
        for i = 1, table.getn(glyphs) do
            glyphs[i]:Hide()
        end
    end
end

-- Draws str on the banner with the glyph atlas (one texture per glyph, made when needed). False when the atlas is not
-- there (then the caller writes the text).
function UI.PaintGlyphs(banner, str, scale)
    local layout = UI.GlyphLayout(str, UI.BANNER_W * scale / UI.PLATE_W)
    if not layout then
        return false
    end
    local glyphs = banner.hsGlyphs
    if not glyphs then
        glyphs = {}
        banner.hsGlyphs = glyphs
    end
    local n = table.getn(layout)
    for i = 1, n do
        local g = glyphs[i]
        if not g then
            g = banner:CreateTexture(nil, "OVERLAY")
            glyphs[i] = g
        end
        if not g:SetTexture(UI.BANNER_DIGITS) then
            UI.HideGlyphs(banner)
            return false
        end
        local e = layout[i]
        g:SetTexCoord(e.box[1] / UI.GLYPH_ATLAS_W, e.box[3] / UI.GLYPH_ATLAS_W, e.box[2] / UI.GLYPH_ATLAS_H, e.box[4] / UI.GLYPH_ATLAS_H)
        g:SetWidth(e.w)
        g:SetHeight(e.h)
        g:ClearAllPoints()
        g:SetPoint("BOTTOMLEFT", banner, "TOPLEFT", e.left, -e.bottom)
        g:Show()
    end
    for i = n + 1, table.getn(glyphs) do
        glyphs[i]:Hide()
    end
    return true
end

-- Paints the ownership banner for num copies on banner (a frame with the texture tex and the text fontstring): the
-- sheet's "x1" / "x2" art, or the blank banner with the count on it (glyphs, else text). scale: the banner's size against the
-- book's 52 px wide one (for the font). Returns true when the banner shows, false when there is no banner art
-- (the caller shows a text badge then).
function UI.PaintBanner(banner, tex, text, num, scale)
    if not banner or not num or num < 1 then
        return false
    end
    if num == 1 or num == 2 then
        UI.SetPiece(tex, "banner_x" .. num)
        text:Hide()
        UI.HideGlyphs(banner)
        banner:Show()
        return true
    end
    if tex:SetTexture(UI.BANNER_BLANK) then
        -- x3, x4 ...: the blank banner with the count on it, in the numerals of the "x2" banner
        tex:SetTexCoord(0, UI.BANNER_BLANK_RIGHT, 0, UI.BANNER_BLANK_BOTTOM)
        if UI.PaintGlyphs(banner, "x" .. num, scale or 1) then
            text:Hide()
        else
            text:SetFont(UI.BANNER_FONT, math.floor((num > 9 and 13 or 17) * (scale or 1) + 0.5))
            text:SetText("x" .. num)
            text:SetTextColor(UI.BANNER_INK[1], UI.BANNER_INK[2], UI.BANNER_INK[3])
            text:Show()
        end
        banner:Show()
        return true
    end
    return false
end

-- The ownership banner under a card: "x1" / "x2" art, or the text badge for other counts / without art.
function UI.SetOwnedMark(slot, badge)
    if not badge then
        slot.badge:Hide()
        if slot.banner then
            slot.banner:Hide()
        end
        return
    end
    slot.badgeText:SetText(badge)
    local _, _, num = string.find(badge, "(%d+)")
    num = tonumber(num)
    if UI.PaintBanner(slot.banner, slot.bannerTex, slot.bannerText, num) then
        slot.badge:Hide()
    else
        if slot.banner then
            slot.banner:Hide()
        end
        slot.badge:Show()
    end
end

function UI.RefreshPage()
    local s = UI.state
    local f = UI.frame
    local total = table.getn(s.results)
    local pages = CM.PageCount(total, CM.PER_PAGE)
    s.page = CM.ClampPage(s.page, pages)
    s.pageIds = CM.PageSlice(s.results, s.page, CM.PER_PAGE)
    if not f then
        return
    end
    local db = CM.GetDB()
    local ready = O.IsReady()
    local edit = UI.IsEditView()
    for i = 1, CM.PER_PAGE do
        local slot = UI.slots[i]
        local id = s.pageIds[i]
        if slot then
            slot.id = id
            if id then
                H.CardWidget.SetCard(slot.widget, id)
                local card = db and db.cards[id]
                if card and CM.IsQuestReward(card) then
                    slot.marker:Show()
                else
                    slot.marker:Hide()
                end
                -- ownership: banner on owned cards, unowned cards dimmed (only when known); in the
                -- edit view also the copies in the deck, and cards whose owned copies are all used
                local badge = O.BadgeText(id)
                local dimmed = ready and badge == nil
                local inDeck = false
                local capped = nil
                if edit then
                    local used
                    inDeck, used, capped = H.DecksUI.SlotInfo(id)
                    if used then
                        dimmed = true
                    end
                end
                H.CardWidget.SetDimmed(slot.widget, dimmed)
                slot.deckBadge:Hide()
                UI.SetOwnedMark(slot, badge)
                -- "Deck Limit: n" over the foot of the card (and in place of the owned banner under it); below the
                -- limit nothing counts up
                slot.limit:Hide()
                if capped then
                    if UI.SetPiece(slot.limit.tex, capped == 1 and "deck_limit_1" or "deck_limit") then
                        slot.limit:Show()
                        slot.badge:Hide()
                        if slot.banner then
                            slot.banner:Hide()
                        end
                    else
                        -- without the plate art: a plain badge
                        slot.deckBadgeText:SetText("|cff33ff33" .. capped .. "/" .. capped .. "|r")
                        slot.deckBadge:Show()
                    end
                end
                UI.SetSlotLook(slot, UI.hover == i, inDeck)
                slot:Show()
            else
                UI.SetSlotLook(slot, false, false)
                slot:Hide()
            end
        end
    end
    f.pageText:SetText("Page " .. s.page .. " / " .. pages)
    if s.page <= 1 then
        f.prevButton:Disable()
    else
        f.prevButton:Enable()
    end
    if s.page >= pages then
        f.nextButton:Disable()
    else
        f.nextButton:Enable()
    end
    local empty = UI.EmptyText()
    if empty then
        f.emptyText:SetText(empty)
        f.emptyText:Show()
    else
        f.emptyText:Hide()
    end
    -- keep the enlarged card in step with the slot under the mouse
    if UI.hover and s.pageIds[UI.hover] then
        UI.ShowPreview(s.pageIds[UI.hover])
    else
        UI.hover = nil
        UI.HidePreview()
    end
end

function UI.RefreshAll()
    local f = UI.frame
    if not f then
        UI.RefreshPage()
        return
    end
    local collection = (UI.state.bottom == 1)
    local packs = (UI.state.bottom == 2 and f.packs ~= nil and H.PacksUI ~= nil)
    local heroes = (UI.state.bottom == UI.HEROES_TAB and f.heroes ~= nil and H.HeroesUI ~= nil)
    UI.RefreshHeader()
    if f.packs then
        if packs then
            f.packs:Show()
        else
            f.packs:Hide()
        end
    end
    if f.heroes then
        if heroes then
            f.heroes:Show()
        else
            f.heroes:Hide()
        end
    end
    if f.sidePanel then
        if collection then
            f.sidePanel:Show()
        else
            f.sidePanel:Hide()
        end
    end
    if collection then
        f.book:Show()
        f.unavailable:Hide()
        UI.RefreshTabs()
        UI.RefreshFilters()
        UI.RefreshPage()
        if f.sidePanel and H.DecksUI then
            H.DecksUI.Refresh()
        end
    elseif packs then
        f.book:Hide()
        f.unavailable:Hide()
        H.PacksUI.Refresh()
    elseif heroes then
        f.book:Hide()
        f.unavailable:Hide()
        H.HeroesUI.Refresh()
    else
        f.book:Hide()
        f.unavailable:Show()
        f.unavailableTitle:SetText(UI.TITLES[UI.state.bottom] or "")
        f.unavailableText:SetText(UI.UNAVAILABLE[UI.state.bottom] or "")
    end
end

-- --------------------------------------------------------- enlarged card
-- The large card of a hover (book, deck list, packs) is bare. true brings back the dark plate with the caption under
-- it (name, type, owned copies, numbers, "Quest reward only").
UI.PREVIEW_CAPTION = false

function UI.HidePreview()
    local pv = UI.preview
    if pv then
        pv.frame:Hide()
    end
end

function UI.PlacePreview()
    local pv, main = UI.preview, UI.frame
    if not pv or not main then
        return
    end
    local uiScale = 1
    if UIParent and UIParent.GetEffectiveScale then
        uiScale = UIParent:GetEffectiveScale() or 1
    end
    local screenW, screenH = 1024, 768
    if GetScreenWidth then
        screenW, screenH = GetScreenWidth(), GetScreenHeight()
    end
    pv.frame:SetScale(CM.FitScale(screenW, screenH, UI.PREVIEW_W, UI.PREVIEW_H))
    local es = main:GetEffectiveScale() or 1
    local left, right = main:GetLeft(), main:GetRight()
    local leftPx, rightPx = nil, nil
    if left and right then
        leftPx, rightPx = left * es, right * es
    end
    local needed = (UI.PREVIEW_W * pv.frame:GetScale() + UI.TABS_OVERHANG) * es
    local side = CM.PreviewSide(leftPx, rightPx, screenW * uiScale, needed)
    pv.frame:ClearAllPoints()
    if side == "right" then
        pv.frame:SetPoint("LEFT", main, "RIGHT", UI.TABS_OVERHANG, 0)
    else
        pv.frame:SetPoint("RIGHT", main, "LEFT", -6, 0)
    end
end

function UI.ShowPreview(id)
    local pv = UI.preview
    if not pv or not id then
        return
    end
    local db = CM.GetDB()
    local card = db and db.cards[id]
    H.CardWidget.SetCard(pv.widget, id)
    if not UI.PREVIEW_CAPTION then
        -- just the large card, no plate and no caption (like the match window's hovers; the owned copies are on
        -- the card's banner and in the craft panel)
        pv.caption:SetText("")
        pv.frame:SetBackdropColor(0, 0, 0, 0)
        pv.frame:SetBackdropBorderColor(0.75, 0.6, 0.3, 0)
        pv.widget.frame:ClearAllPoints()
        pv.widget.frame:SetPoint("CENTER", pv.frame, "CENTER", 0, 0)
    else
        local lines = CM.Caption(id, card, CM.HasTexture(id))
        local own = O.CaptionLine(id)
        if own then
            table.insert(lines, 3, own)
        end
        pv.caption:SetText(table.concat(lines, "\n"))
        pv.frame:SetBackdropColor(0, 0, 0, 0.9)
        pv.frame:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        pv.widget.frame:ClearAllPoints()
        pv.widget.frame:SetPoint("TOP", pv.frame, "TOP", 0, -8)
    end
    UI.PlacePreview()
    pv.frame:Show()
end

-- Forgets the hovered card (hiding a frame under the cursor may not fire OnLeave).
function UI.ClearHover()
    UI.hover = nil
    for i = 1, table.getn(UI.slots) do
        UI.SetSlotLook(UI.slots[i], false, false)
    end
end

function UI.OnSlotEnter(index)
    local slot = UI.slots[index]
    if not slot or not slot.id then
        return
    end
    UI.hover = index
    UI.SetSlotLook(slot, true, UI.IsEditView() and HsDecks.InDraft(slot.id) > 0)
    UI.ShowPreview(slot.id)
end

-- A click on a card: in the edit view it adds (left) or removes (right) a copy; in crafting mode it
-- opens the craft panel.
function UI.OnSlotClick(index, button)
    local slot = UI.slots[index]
    if not slot or not slot.id then
        return
    end
    if UI.IsEditView() then
        H.DecksUI.OnBookClick(slot.id, button)
    elseif UI.IsCraftMode() and H.CraftUI and H.CraftUI.Show then
        H.CraftUI.Show(slot.id)
    end
end

-- The wallet or the crafting access changed (Hearthstone.lua).
function UI.OnWalletChanged()
    if not UI.frame then
        return
    end
    UI.RefreshHeader()
    -- the crafting access opened or closed: the side panel shows the craft panel or the deck list
    if UI.frame.sidePanel and UI.state.bottom == 1 and H.DecksUI and H.DecksUI.Refresh then
        H.DecksUI.Refresh()
    end
    if H.CraftUI and H.CraftUI.Refresh then
        H.CraftUI.Refresh()
    end
end

function H.OnWalletChanged()
    UI.OnWalletChanged()
end

-- A crafting NPC was opened: the collection tab of the window, in crafting mode.
function UI.ShowCraft()
    if not UI.EnsureBuilt() then
        return false
    end
    if UI.state.bottom ~= 1 then
        UI.SetBottomTab(1)
    end
    UI.frame:Show()
    UI.RefreshAll()
    return true
end

function H.ShowCraft()
    return UI.ShowCraft()
end

function UI.OnSlotLeave(index)
    local slot = UI.slots[index]
    if slot then
        UI.SetSlotLook(slot, false, UI.IsEditView() and slot.id ~= nil and HsDecks.InDraft(slot.id) > 0)
    end
    if UI.hover == index then
        UI.hover = nil
    end
    UI.HidePreview()
end

-- --------------------------------------------------------------- tooltips
local function tip(owner, anchor, title, l1, l2)
    if not GameTooltip then
        return
    end
    GameTooltip:SetOwner(owner, anchor)
    GameTooltip:SetText(title)
    if l1 then
        GameTooltip:AddLine(l1, 1, 1, 1)
    end
    if l2 then
        GameTooltip:AddLine(l2, 0.6, 0.6, 0.6)
    end
    GameTooltip:Show()
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

function UI.OnClassTabEnter(index)
    local t = UI.classTabs[index]
    if not t then
        return
    end
    local cat = UI.state.catalog
    local n = cat and cat.counts[t.key] or 0
    if UI.state.bottom == UI.HEROES_TAB then
        tip(t.button, "ANCHOR_BOTTOM", t.label, "Hero portraits")
        return
    end
    tip(t.button, "ANCHOR_BOTTOM", t.label, n .. " cards")
end

function UI.OnManaEnter(n)
    local b = UI.manaButtons[n]
    if not b then
        return
    end
    local title = "Mana cost " .. n
    if n == CM.MANA_MAX then
        title = "Mana cost 7 or more"
    end
    tip(b, "ANCHOR_TOP", title, "Click to toggle. Select several to combine them.")
end

function UI.OnManaAllEnter()
    if UI.manaAll then
        tip(UI.manaAll, "ANCHOR_TOP", "All mana costs", "Click to clear the mana filter.")
    end
end

-- ------------------------------------------------------------------ build
-- A page of the fallback look: dark backdrop with the border of the tooltips.
local function makePage(parent, x, y, w, h)
    local p = CreateFrame("Frame", nil, parent)
    p:SetWidth(w)
    p:SetHeight(h)
    p:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    p:SetBackdrop({
        bgFile = WHITE,
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    p:SetBackdropColor(0.10, 0.08, 0.05, 0.92)
    p:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    return p
end

-- Book art on or off. It is on unless the player turned it off with /hs bookart off
-- (HearthstoneDB.bookArt == false).
function UI.BookArtEnabled()
    return not (H.db and H.db.bookArt == false)
end

-- The tiles of the book image and of the bar under it: on the collection tab only. The Packs and Heroes pages lie
-- over them and leave 6 px free on each side (PacksUI PU.BG_INSET), where their edge would show past the frame.
function UI.ShowBookTiles()
    local f = UI.frame
    if not f then
        return
    end
    local book = UI.BookShown()
    local sets = { { f.bookTiles, book and UI.BookArtEnabled() }, { f.barTiles, book } }
    for k = 1, table.getn(sets) do
        local tiles, show = sets[k][1], sets[k][2]
        if tiles then
            for i = 1, table.getn(tiles) do
                if show then
                    tiles[i]:Show()
                else
                    tiles[i]:Hide()
                end
            end
        end
    end
end

-- Shows the book image (when deployed) or the plain / parchment page of the fallback look, and sets the
-- ink of the texts that sit on the page (dark brown on parchment, gold on the plain dark page).
function UI.ApplyBookArt()
    local f = UI.frame
    if not f or not f.art then
        return
    end
    local on = UI.BookArtEnabled()
    local tiles = on and f.bookTiles ~= nil
    UI.ShowBookTiles()
    if f.plain then
        if tiles then
            f.plain:Hide()
        else
            f.plain:Show()
        end
        if f.plain.art then
            if on and not tiles then
                f.plain.art:Show()
            else
                f.plain.art:Hide()
            end
        end
    end
    local ink = on
    UI.ink = ink
    local texts = { f.emptyText, f.title, f.note, f.pageText }
    for i = 1, table.getn(texts) do
        local t = texts[i]
        if t then
            if ink then
                t:SetTextColor(UI.PAGE_INK[1], UI.PAGE_INK[2], UI.PAGE_INK[3])
                -- the font's black drop shadow over dark ink on parchment reads as doubled text
                t:SetShadowColor(0, 0, 0, 0)
            else
                t:SetTextColor(1, 0.82, 0)
                t:SetShadowColor(0, 0, 0, 1)
            end
        end
    end
end

-- /hs bookart [on|off]: returns the message to print.
function UI.SetBookArt(mode)
    mode = string.lower(tostring(mode or ""))
    if not H.db then
        return "Settings are not loaded yet."
    end
    if mode == "off" then
        H.db.bookArt = false
    elseif mode == "on" then
        H.db.bookArt = nil
    else
        return "book pages: " .. (UI.BookArtEnabled() and "book art" or "plain") .. " (/hs bookart on | off)"
    end
    UI.ApplyBookArt()
    return "book pages: " .. (UI.BookArtEnabled() and "book art" or "plain")
end

-- Mouse wheel pages the book. 'solid' frames also take the mouse so the wheel reaches them
-- (never the book group: it covers the title bar and must not swallow the drag).
local function wheel(frame, solid)
    if solid then
        frame:EnableMouse(true)
    end
    frame:EnableMouseWheel(true)
    frame:SetScript("OnMouseWheel", function()
        UI.OnWheel(arg1)
    end)
end

local function makeSlot(index, parent, col, row)
    local slot = CreateFrame("Button", "HearthstoneCardSlot" .. index, parent)
    slot:SetWidth(SLOT_W)
    slot:SetHeight(SLOT_H)
    slot:SetPoint("TOPLEFT", parent, "TOPLEFT", UI.GRID_X + col * (SLOT_W + COL_GAP),
        UI.PAGES_TOP - (UI.GRID_Y + row * (SLOT_H + ROW_GAP)))
    slot:EnableMouse(true)
    slot:SetBackdrop({
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    slot:SetBackdropBorderColor(1, 0.82, 0, 0)
    wheel(slot, true)

    -- the visible card inside the slot: the render has a transparent margin (CardWidget.VISIBLE), so everything
    -- that sits on the card's edge is placed from this rectangle, not from the slot's edge
    local vl, vt, vr, vb = H.CardWidget.VisibleRect(CARD_W)
    slot.vis = { left = vl, top = vt, right = vr, bottom = vb }
    -- only the visible card takes the mouse (hover, click, preview), not the transparent margin of the render
    if slot.SetHitRectInsets then
        slot:SetHitRectInsets(vl, SLOT_W - vr, vt, SLOT_H - vb)
    end

    -- glows around the card (behind it, their line flush with the card's edge): blue while the mouse is over
    -- it, gold while a copy is in the deck being edited
    local gb = slot:CreateTexture(nil, "BACKGROUND")
    if UI.PlaceGlow(gb, "glow_blue", slot, vl, vt, vr, vb) then
        gb:Hide()
        slot.glowBlue = gb
    end
    local gg = slot:CreateTexture(nil, "BACKGROUND")
    if UI.PlaceGlow(gg, "glow_gold", slot, vl, vt, vr, vb) then
        gg:Hide()
        slot.glowGold = gg
    end

    local widget = H.CardWidget.Create(slot, "HearthstoneCardSlot" .. index .. "Card", CARD_W, false)
    widget.frame:SetPoint("CENTER", slot, "CENTER", 0, 0)
    slot.widget = widget

    -- "quest reward only" marker (yellow "!" in the corner; the hover caption says it in words)
    local marker = CreateFrame("Frame", nil, slot)
    marker:SetFrameLevel(slot:GetFrameLevel() + 4)
    marker:SetWidth(18)
    marker:SetHeight(18)
    marker:SetPoint("TOPRIGHT", slot, "TOPLEFT", vr - 4, -vt - 2)
    local mt = marker:CreateTexture(nil, "OVERLAY")
    mt:SetTexture("Interface\\GossipFrame\\AvailableQuestIcon")
    mt:SetAllPoints(marker)
    marker:Hide()
    slot.marker = marker

    -- quantity badge ("x3") of the fallback look and of counts the banners do not have, over the bottom edge
    local badge = CreateFrame("Frame", nil, slot)
    badge:SetFrameLevel(slot:GetFrameLevel() + 4)
    badge:SetWidth(40)
    badge:SetHeight(22)
    badge:SetPoint("TOP", slot, "TOPLEFT", (vl + vr) / 2, -vb + 11)
    badge:SetBackdrop({
        bgFile = WHITE,
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    badge:SetBackdropColor(0, 0, 0, 0.85)
    badge:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    local bt = badge:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    bt:SetPoint("CENTER", badge, "CENTER", 0, 0)
    badge:Hide()
    slot.badge = badge
    slot.badgeText = bt

    -- the "x1" / "x2" banner of the book art: centred under the visible card, its top tucked UI.BANNER_OVERLAP px
    -- behind the card's bottom edge (the card widget draws over it), like the original game
    if UI.Piece("banner_x1") and UI.Piece("banner_x2") then
        local banner = CreateFrame("Frame", nil, slot)
        banner:SetFrameLevel(slot:GetFrameLevel())
        banner:SetWidth(UI.BANNER_W)
        banner:SetHeight(UI.BANNER_H)
        banner:SetPoint("TOPLEFT", slot, "TOPLEFT", (vl + vr) / 2 - UI.BANNER_W / 2, -vb + UI.BANNER_OVERLAP)
        local bg = banner:CreateTexture(nil, "ARTWORK")
        bg:SetAllPoints(banner)
        -- the count of the blank banner (x3 and up), a little above the middle where the art's "x2" sits
        local bannerText = banner:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        bannerText:SetFont(UI.BANNER_FONT, 17)
        bannerText:SetTextColor(UI.BANNER_INK[1], UI.BANNER_INK[2], UI.BANNER_INK[3])
        bannerText:SetShadowColor(0, 0, 0, 0)
        bannerText:SetPoint("CENTER", banner, "CENTER", 0, 3)
        bannerText:Hide()
        banner:Hide()
        slot.banner = banner
        slot.bannerTex = bg
        slot.bannerText = bannerText
    end

    -- "Deck Limit: 2" plate of the art, laid over the foot of the card with its keyhole hanging below the card's edge
    -- (the plate's bar bottom sits UI.LIMIT_BAR_BOTTOM canvas px down in the 512 x 256 texture)
    local limit = CreateFrame("Frame", nil, slot)
    limit:SetFrameLevel(slot:GetFrameLevel() + 5)
    local ls = (vr - vl) / UI.LIMIT_CONTENT_W
    limit:SetWidth(512 * ls)
    limit:SetHeight(256 * ls)
    limit:SetPoint("TOPLEFT", slot, "TOPLEFT", (vl + vr) / 2 - 256 * ls,
        -(vb - UI.LIMIT_FOOT_GAP - UI.LIMIT_BAR_BOTTOM * ls))
    local lt = limit:CreateTexture(nil, "ARTWORK")
    lt:SetAllPoints(limit)
    limit.tex = lt
    limit:Hide()
    slot.limit = limit

    -- copies in the deck being edited ("1/2"), over the top edge of the card
    local deckBadge = CreateFrame("Frame", nil, slot)
    deckBadge:SetFrameLevel(slot:GetFrameLevel() + 4)
    deckBadge:SetWidth(44)
    deckBadge:SetHeight(22)
    deckBadge:SetPoint("TOP", slot, "TOPLEFT", (vl + vr) / 2, -vt - 1)
    deckBadge:SetBackdrop({
        bgFile = WHITE,
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    deckBadge:SetBackdropColor(0, 0, 0, 0.85)
    deckBadge:SetBackdropBorderColor(0.4, 0.6, 1, 1)
    local dbt = deckBadge:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    dbt:SetPoint("CENTER", deckBadge, "CENTER", 0, 0)
    deckBadge:Hide()
    slot.deckBadge = deckBadge
    slot.deckBadgeText = dbt

    local idx = index
    -- 1.12: arg1 is the mouse button ("LeftButton" / "RightButton")
    slot:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    slot:SetScript("OnClick", function()
        UI.OnSlotClick(idx, arg1)
    end)
    slot:SetScript("OnEnter", function()
        UI.OnSlotEnter(idx)
    end)
    slot:SetScript("OnLeave", function()
        UI.OnSlotLeave(idx)
    end)
    return slot
end

-- A class tab along the top edge of the book. With the art: the gold framed icon of the sheet (a checked
-- tab glows and sits lower); without: the spellbook style tab with the class icon.
local function makeClassTab(index, def, parent)
    local b = CreateFrame("CheckButton", "HearthstoneClassTab" .. index, parent)
    local pieceName = UI.TAB_PIECES[def.key]
    local piece = pieceName and UI.Piece(pieceName)
    local textured = false
    if piece then
        b:SetWidth(UI.TAB_W)
        b:SetHeight(UI.TAB_W)
        b:SetNormalTexture(piece.path)
        local nt = b:GetNormalTexture()
        nt:SetTexCoord(0, piece.right, 0, piece.bottom)
        b:SetHighlightTexture(piece.path, "ADD")
        local ht = b:GetHighlightTexture()
        ht:SetTexCoord(0, piece.right, 0, piece.bottom)
        ht:SetAlpha(0.3)
        b:SetCheckedTexture(piece.path)
        local ct = b:GetCheckedTexture()
        ct:SetTexCoord(0, piece.right, 0, piece.bottom)
        ct:SetBlendMode("ADD")
        ct:SetAlpha(0.35)
        textured = true
    else
        b:SetWidth(32)
        b:SetHeight(32)
        local bg = b:CreateTexture(nil, "BACKGROUND")
        bg:SetTexture("Interface\\Spellbook\\SpellBook-SkillLineTab")
        bg:SetWidth(64)
        bg:SetHeight(64)
        bg:SetPoint("TOPLEFT", b, "TOPLEFT", -3, 11)
        if def.coords then
            b:SetNormalTexture(CM.ICON_ATLAS)
            b:GetNormalTexture():SetTexCoord(def.coords[1], def.coords[2], def.coords[3], def.coords[4])
        else
            b:SetNormalTexture(def.icon)
        end
        b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        b:SetCheckedTexture("Interface\\Buttons\\CheckButtonHilight")
        b:GetCheckedTexture():SetBlendMode("ADD")
    end
    local key = def.key
    local idx = index
    b:SetScript("OnClick", function()
        hideTip()
        if UI.state.bottom == UI.HEROES_TAB and H.HeroesUI then
            H.HeroesUI.SetClass(key)
        elseif UI.state.filters.class == key then
            UI.SetClass("ALL")       -- the active tab again: no class filter, all cards
        else
            UI.SetClass(key)
        end
    end)
    b:SetScript("OnEnter", function()
        UI.OnClassTabEnter(idx)
    end)
    b:SetScript("OnLeave", hideTip)
    return b, textured
end

local function makeSearchBox(parent)
    local eb = CreateFrame("EditBox", "HearthstoneSearchBox", parent, "InputBoxTemplate")
    local x0 = 460
    local art = parent:CreateTexture(nil, "BACKGROUND")
    if UI.SetPiece(art, "search_box") then
        art:SetWidth(212)
        art:SetHeight(UI.BAR_H - 4)
        art:SetPoint("LEFT", parent, "TOPLEFT", x0, UI.BAR_MID)
        -- the art draws the field: hide the template's own pieces and sit inside the art (right of the magnifier)
        local parts = { "Left", "Middle", "Right" }
        for i = 1, table.getn(parts) do
            local r = getglobal("HearthstoneSearchBox" .. parts[i])
            if r then
                r:Hide()
            end
        end
        eb:SetWidth(158)
        eb:SetHeight(22)
        eb:SetPoint("LEFT", parent, "TOPLEFT", x0 + 36, UI.BAR_MID)
    else
        eb:SetWidth(180)
        eb:SetHeight(22)
        eb:SetPoint("LEFT", parent, "TOPLEFT", x0 + 6, UI.BAR_MID)
    end
    eb:SetAutoFocus(false)
    eb:SetMaxLetters(40)
    eb:SetTextInsets(6, 4, 0, 0)
    local hint = eb:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    hint:SetPoint("LEFT", eb, "LEFT", 6, 0)
    hint:SetText("Search")
    local function updateHint()
        if eb:GetText() == "" and not UI.searchFocused then
            hint:Show()
        else
            hint:Hide()
        end
    end
    eb:SetScript("OnTextChanged", function()
        updateHint()
        if not UI.settingSearch then
            UI.SetSearch(eb:GetText())
        end
    end)
    eb:SetScript("OnEditFocusGained", function()
        UI.searchFocused = true
        updateHint()
        eb:HighlightText()
    end)
    eb:SetScript("OnEditFocusLost", function()
        UI.searchFocused = false
        updateHint()
        eb:HighlightText(0, 0)
    end)
    eb:SetScript("OnEnterPressed", function()
        eb:ClearFocus()
    end)
    eb:SetScript("OnEscapePressed", function()
        eb:ClearFocus()
    end)
    UI.searchHint = updateHint
    return eb
end

local function initRarityMenu()
    local fl = UI.state.filters
    for i = 1, table.getn(CM.RARITIES) do
        local r = CM.RARITIES[i]
        local info = {}
        info.text = r.label
        info.value = r.key
        info.arg1 = r.key
        info.func = UI.OnRarityPicked
        if fl.rarity == r.key then
            info.checked = 1
        end
        UIDropDownMenu_AddButton(info)
    end
end

-- Dropdown item handler. 1.12 calls func(button.arg1, button.arg2) with 'this' = the item.
function UI.OnRarityPicked(key)
    if type(key) ~= "string" and this then
        key = this.value
    end
    UI.SetRarity(key)
end

local function makeDropDown(parent)
    local dd = CreateFrame("Frame", "HearthstoneRarityDropDown", parent, "UIDropDownMenuTemplate")
    dd:SetPoint("LEFT", parent, "TOPLEFT", 664, UI.BAR_MID - 2)
    UIDropDownMenu_Initialize(dd, initRarityMenu)
    UIDropDownMenu_SetWidth(84, dd)
    UIDropDownMenu_SetText(CM.RARITIES[1].label, dd)
    return dd
end

-- Ownership filter menu. Owned / Not owned are greyed out until the collection is loaded
-- (the client runs this again every time the menu opens).
local function initOwnMenu()
    local fl = UI.state.filters
    local ready = O.IsReady()
    for i = 1, table.getn(CM.OWN_FILTERS) do
        local o = CM.OWN_FILTERS[i]
        local info = {}
        info.text = o.label
        info.value = o.key
        info.arg1 = o.key
        info.func = UI.OnOwnPicked
        if fl.own == o.key then
            info.checked = 1
        end
        if o.key ~= "ALL" and not ready then
            info.disabled = 1
        end
        UIDropDownMenu_AddButton(info)
    end
end

function UI.OnOwnPicked(key)
    if type(key) ~= "string" and this then
        key = this.value
    end
    UI.SetOwnFilter(key)
end

local function makeOwnDropDown(parent)
    local dd = CreateFrame("Frame", "HearthstoneOwnedDropDown", parent, "UIDropDownMenuTemplate")
    dd:SetPoint("LEFT", parent, "TOPLEFT", 790, UI.BAR_MID - 2)
    UIDropDownMenu_Initialize(dd, initOwnMenu)
    UIDropDownMenu_SetWidth(78, dd)
    UIDropDownMenu_SetText(CM.OWN_FILTERS[1].label, dd)
    return dd
end

local function makePagingButton(name, parent, kind)
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(32)
    b:SetHeight(32)
    b:SetNormalTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Up")
    b:SetPushedTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Down")
    b:SetDisabledTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. kind .. "-Disabled")
    b:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
    return b
end

-- One mana gem: the gem of the sheet with the number baked in, or a UIPanelButton with the number as text.
local function makeManaButton(name, parent, x, n, label)
    local piece = UI.Piece(n == "all" and "mana_all" or ("mana_" .. n))
    local b
    if piece then
        b = CreateFrame("Button", name, parent)
        b:SetWidth(piece.w)
        b:SetHeight(piece.h)
        b:SetNormalTexture(piece.path)
        b:GetNormalTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
        b:SetHighlightTexture(piece.path, "ADD")
        local ht = b:GetHighlightTexture()
        ht:SetTexCoord(0, piece.right, 0, piece.bottom)
        ht:SetAlpha(0.35)
        b:SetPoint("CENTER", parent, "TOPLEFT", x, UI.BAR_MID)
        b.textured = true
    else
        b = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
        b:SetWidth(26)
        b:SetHeight(22)
        b:SetPoint("CENTER", parent, "TOPLEFT", x, UI.BAR_MID)
        b:SetText(label)
    end
    return b
end

-- A button drawn with the blank plate of the sheet (btn_blank: the gold framed parchment button without its
-- text) and the label on it in dark ink. Returns the button; b.label is the label, b.plate the plate piece.
-- Without the plate it is a UIPanelButton with the label as text. Dim it with UI.SetButtonLook.
function UI.MakeButton(name, parent, width, height, text, level)
    local piece = UI.Piece("btn_blank")
    local b
    if piece then
        b = CreateFrame("Button", name, parent)
        b:SetWidth(width)
        b:SetHeight(height)
        if level then
            b:SetFrameLevel(level)
        end
        b:SetNormalTexture(piece.path)
        b:GetNormalTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
        b:SetHighlightTexture(piece.path, "ADD")
        local ht = b:GetHighlightTexture()
        ht:SetTexCoord(0, piece.right, 0, piece.bottom)
        ht:SetAlpha(0.3)
        local label = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        label:SetPoint("CENTER", b, "CENTER", 0, 1)
        label:SetText(text)
        label:SetTextColor(UI.PAGE_INK[1], UI.PAGE_INK[2], UI.PAGE_INK[3])
        label:SetShadowColor(0, 0, 0, 0)
        b.label = label
        b.plate = piece
    else
        b = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
        b:SetWidth(width)
        b:SetHeight(22)
        b:SetText(text)
        if level then
            b:SetFrameLevel(level)
        end
    end
    return b
end

-- Enables / disables a button of UI.MakeButton and dims its plate and label when it is disabled.
function UI.SetButtonEnabled(b, enabled)
    if enabled then
        b:Enable()
    else
        b:Disable()
    end
    if b.label then
        local nt = b:GetNormalTexture()
        if nt then
            if enabled then
                nt:SetVertexColor(1, 1, 1)
            else
                nt:SetVertexColor(0.5, 0.5, 0.5)
            end
        end
        if enabled then
            b.label:SetTextColor(UI.PAGE_INK[1], UI.PAGE_INK[2], UI.PAGE_INK[3])
        else
            b.label:SetTextColor(0.42, 0.38, 0.33)
        end
    end
end

function UI.Build()
    if UI.frame then
        return UI.frame
    end
    if not CreateFrame or not UIParent then
        return nil
    end
    local W, Ht = UI.WIDTH, UI.HEIGHT
    UI.slots = {}
    UI.classTabs = {}
    UI.manaButtons = {}
    UI.manaAll = nil
    local textured = UI.BookTextured()

    local main = CreateFrame("Frame", "HearthstoneFrame", UIParent)
    main:SetWidth(W)
    main:SetHeight(Ht)
    main:SetPoint("CENTER", UIParent, "CENTER", 0, 0)    -- UI.OnShow places it (saved or default position)
    main:SetFrameStrata("DIALOG")
    main:SetToplevel(true)
    main:SetMovable(true)
    main:EnableMouse(true)
    main:SetClampedToScreen(true)
    if not textured then
        -- the fallback look keeps the dialog frame; with the book art the window has the dark background texture
        -- below instead (the book is transparent outside its silhouette)
        main:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
            tile = true, tileSize = 32, edgeSize = 32,
            insets = { left = 11, right = 12, top = 12, bottom = 11 },
        })
    end
    if textured then
        -- the book is transparent outside its silhouette: a dark background of its own keeps the window from
        -- looking cut out of the world (it follows the window's width, so the Packs tab has no gap on the right)
        local bg = main:CreateTexture(nil, "BACKGROUND")
        bg:SetTexture(WHITE)
        bg:SetVertexColor(UI.BG_COLOR[1], UI.BG_COLOR[2], UI.BG_COLOR[3], UI.BG_COLOR[4])
        local fp = UI.FRAME_PAD
        local m = UI.FRAME_METAL
        bg:SetPoint("TOPLEFT", main, "TOPLEFT", -fp.left + m, fp.top - m)
        bg:SetPoint("BOTTOMRIGHT", main, "BOTTOMRIGHT", fp.right - m, -fp.bottom + m)
        main.bg = bg
        -- the frame: above the book, below the tabs, the close button and the preview
        local edge = CreateFrame("Frame", nil, main)
        edge:SetFrameLevel(main:GetFrameLevel() + 12)
        edge:SetPoint("TOPLEFT", main, "TOPLEFT", -fp.left, fp.top)
        edge:SetPoint("BOTTOMRIGHT", main, "BOTTOMRIGHT", fp.right, -fp.bottom)
        edge:SetBackdrop({
            edgeFile = UI.FRAME_EDGE,
            tile = true, tileSize = 32, edgeSize = 32,
            insets = { left = 0, right = 0, top = 0, bottom = 0 },
        })
        edge:SetBackdropBorderColor(UI.FRAME_TINT[1], UI.FRAME_TINT[2], UI.FRAME_TINT[3], UI.FRAME_TINT[4])
        main.frameEdge = edge
    end
    wheel(main)
    main:Hide()

    local L = main:GetFrameLevel()

    -- dragging by the band above the book (the class tabs sit on top of it) and by the top of the book
    local drag = CreateFrame("Frame", nil, main)
    drag:SetFrameLevel(L + 1)
    drag:SetHeight(UI.HEADER_H + UI.TAB_BAND + 14)
    drag:SetPoint("TOPLEFT", main, "TOPLEFT", 0, 0)
    drag:SetWidth(UI.BOOK_W)
    drag:EnableMouse(true)
    drag:SetScript("OnMouseDown", function()
        if arg1 == "LeftButton" then
            main:StartMoving()
        end
    end)
    drag:SetScript("OnMouseUp", function()
        main:StopMovingOrSizing()
        UI.SavePosition()
    end)
    main.dragArea = drag

    -- the class tabs live on a frame BELOW the book art: the page overlaps their lower part
    local tabsFrame = CreateFrame("Frame", nil, main)
    tabsFrame:SetFrameLevel(L + 2)
    tabsFrame:SetAllPoints(main)
    main.tabsFrame = tabsFrame
    UI.tabsFrame = tabsFrame

    -- ---- the art: the book image, or the plain / parchment pages of the fallback look
    local art = CreateFrame("Frame", nil, main)
    art:SetFrameLevel(L + 5)
    art:SetAllPoints(main)
    main.art = art
    if textured then
        main.bookTiles = UI.PlaceTiles(art, "book", UI.BOOK_TILES, 0, UI.PAGES_TOP, "BACKGROUND")
        -- the bar below the book is part of the art (UI.ShowBookTiles hides both on the Packs and Heroes tabs)
        main.barTiles = UI.PlaceTiles(art, "bar_main", UI.BAR_TILES, 0, UI.BAR_TOP, "BACKGROUND")
        if main.barTiles then
            main.bar = main.barTiles[1]
        end
    end
    local plain = makePage(art, UI.PAGE_X1, UI.PAGES_TOP - UI.PAGE_Y1, UI.PAGE_X2 - UI.PAGE_X1, UI.PAGE_Y2 - UI.PAGE_Y1)
    local pageArt = plain:CreateTexture(nil, "BACKGROUND")
    pageArt:SetTexture(UI.BOOK_ART.page)
    pageArt:SetPoint("TOPLEFT", plain, "TOPLEFT", 4, -4)
    pageArt:SetPoint("BOTTOMRIGHT", plain, "BOTTOMRIGHT", -4, 4)
    pageArt:Hide()
    plain.art = pageArt
    local spine = plain:CreateTexture(nil, "BORDER")
    spine:SetTexture(WHITE)
    spine:SetVertexColor(0.16, 0.09, 0.04, 1)
    spine:SetWidth(UI.PAGE_X1 - 20)
    spine:SetHeight(UI.BOOK_H - 36)
    spine:SetPoint("TOPRIGHT", plain, "TOPLEFT", -6, 0)
    plain:Hide()
    main.plain = plain

    -- the title (class name on the plate), the status line: above the art, also on the Packs tab
    local head = CreateFrame("Frame", nil, main)
    head:SetFrameLevel(L + 6)
    head:SetAllPoints(main)
    local title = head:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("CENTER", head, "TOPLEFT", UI.PAGE_CX - 6, UI.PAGES_TOP - UI.PLATE_Y)
    main.title = title
    -- the window title in the strip above the class tabs
    local header = head:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    header:SetPoint("CENTER", head, "TOPLEFT", UI.PAGE_CX, -UI.HEADER_H / 2)
    header:SetText("Hearthstone")
    main.header = header
    -- The crafting dust (the bottle and the number, no tooltip) at the left of the header strip
    local wallet = CreateFrame("Frame", nil, head)
    wallet:SetFrameLevel(L + 7)
    wallet:SetWidth(UI.WALLET_W)
    wallet:SetHeight(UI.HEADER_H - 8)
    wallet:SetPoint("LEFT", head, "TOPLEFT", UI.WALLET_X, -UI.HEADER_H / 2 - UI.WALLET_DY)
    local dustIcon = wallet:CreateTexture(nil, "ARTWORK")
    UI.SetBottle(dustIcon, UI.BOTTLE_H)
    dustIcon:SetPoint("LEFT", wallet, "LEFT", 0, 0)
    local dustText = wallet:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
    dustText:SetPoint("LEFT", dustIcon, "RIGHT", 6, 0)
    wallet.dustIcon = dustIcon
    wallet.dustText = dustText
    wallet:Hide()
    main.wallet = wallet
    local note = head:CreateFontString(nil, "ARTWORK", "GameFontDisableSmall")
    note:SetPoint("LEFT", head, "TOPLEFT", UI.GRID_X, UI.PAGES_TOP - UI.FOOT_Y)
    note:SetJustifyH("LEFT")
    note:SetWidth(300)
    main.note = note

    -- ---- the collection: cards, class tabs, page arrows, the bar with the filters
    local book = CreateFrame("Frame", nil, main)
    book:SetFrameLevel(L + 6)
    book:SetAllPoints(main)
    wheel(book)
    main.book = book

    -- bar below the book: the art tiles above (they are part of the art), or a flat dark bar
    if not main.barTiles then
        local barTex = book:CreateTexture(nil, "BACKGROUND")
        barTex:SetTexture(WHITE)
        barTex:SetVertexColor(0.16, 0.05, 0.04, 0.95)
        barTex:SetWidth(UI.BOOK_W - 40)
        barTex:SetHeight(UI.BAR_H - 18)
        barTex:SetPoint("TOPLEFT", book, "TOPLEFT", 20, UI.BAR_TOP - 9)
        main.bar = barTex
    end

    -- mana gems: "all" and 0..7+
    UI.manaAll = makeManaButton("HearthstoneManaAll", book, 49, "all", "All")
    UI.manaAll:SetScript("OnClick", function()
        UI.ClearMana()
    end)
    UI.manaAll:SetScript("OnEnter", function()
        UI.OnManaAllEnter()
    end)
    UI.manaAll:SetScript("OnLeave", hideTip)
    for n = 0, CM.MANA_MAX do
        local label = tostring(n)
        if n == CM.MANA_MAX then
            label = label .. "+"
        end
        local b = makeManaButton("HearthstoneMana" .. n, book, 92 + n * 42, n, label)
        if n == CM.MANA_MAX and b.textured then
            -- the "+" of "7+": a small plus gem on the corner of the 7
            local plus = b:CreateTexture(nil, "OVERLAY")
            if UI.SetPiece(plus, "mana_plus") then
                plus:SetWidth(UI.PLUS_W)
                plus:SetHeight(UI.PLUS_H)
                plus:SetPoint("TOPRIGHT", b, "TOPRIGHT", UI.PLUS_X, UI.PLUS_Y)
                -- a bigger plus on the badge (the badge's own plus is tiny): two white bars on a frame above it
                local pf = CreateFrame("Frame", nil, b)
                pf:SetFrameLevel(b:GetFrameLevel() + 2)
                pf:SetWidth(UI.PLUS_W)
                pf:SetHeight(UI.PLUS_H)
                pf:SetPoint("TOPRIGHT", b, "TOPRIGHT", UI.PLUS_X, UI.PLUS_Y)
                local bars = { { UI.PLUS_ARM, UI.PLUS_THICK }, { UI.PLUS_THICK, UI.PLUS_ARM } }
                for k = 1, 2 do
                    local bar = pf:CreateTexture(nil, "OVERLAY")
                    bar:SetTexture(WHITE)
                    bar:SetVertexColor(0.97, 0.97, 0.92, 1)
                    bar:SetWidth(bars[k][1])
                    bar:SetHeight(bars[k][2])
                    bar:SetPoint("CENTER", pf, "CENTER", 0, 0)
                end
                b.plusMark = pf
            end
        end
        local mana = n
        b:SetScript("OnClick", function()
            UI.ToggleMana(mana)
        end)
        b:SetScript("OnEnter", function()
            UI.OnManaEnter(mana)
        end)
        b:SetScript("OnLeave", hideTip)
        UI.manaButtons[n] = b
    end

    main.searchBox = makeSearchBox(book)
    main.rarityDropDown = makeDropDown(book)
    main.ownDropDown = makeOwnDropDown(book)

    local reset = CreateFrame("Button", "HearthstoneResetButton", book, "UIPanelButtonTemplate")
    reset:SetWidth(54)
    reset:SetHeight(22)
    reset:SetPoint("LEFT", book, "TOPLEFT", 926, UI.BAR_MID)
    reset:SetText("Reset")
    reset:SetScript("OnClick", function()
        UI.SettingSearchText("")
        UI.ResetFilters()
    end)
    main.resetButton = reset

    for i = 1, CM.PER_PAGE do
        local col = (i - 1) - math.floor((i - 1) / 4) * 4     -- 0..3 within the row of four
        local row = math.floor((i - 1) / 4)
        UI.slots[i] = makeSlot(i, book, col, row)
    end

    -- the message lives on its own frame above the cards (a FontString of book would draw under them)
    local emptyFrame = CreateFrame("Frame", nil, book)
    emptyFrame:SetFrameLevel(book:GetFrameLevel() + 12)
    emptyFrame:SetWidth(500)
    emptyFrame:SetHeight(80)
    emptyFrame:SetPoint("CENTER", book, "TOPLEFT", UI.PAGE_CX, UI.PAGES_TOP - 380)
    local empty = emptyFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    empty:SetPoint("CENTER", emptyFrame, "CENTER", 0, 0)
    empty:SetWidth(480)
    empty:Hide()
    main.emptyText = empty

    -- page number and arrows at the bottom of the page
    local pageY = UI.PAGES_TOP - UI.FOOT_Y
    local prev = makePagingButton("HearthstonePrevPageButton", book, "PrevPage")
    prev:SetPoint("CENTER", book, "TOPLEFT", UI.PAGE_CX - 62, pageY)
    prev:SetScript("OnClick", function()
        UI.ChangePage(-1)
    end)
    main.prevButton = prev

    local nxt = makePagingButton("HearthstoneNextPageButton", book, "NextPage")
    nxt:SetPoint("CENTER", book, "TOPLEFT", UI.PAGE_CX + 62, pageY)
    nxt:SetScript("OnClick", function()
        UI.ChangePage(1)
    end)
    main.nextButton = nxt

    -- the margins of the parchment turn the page too: a click flips it, and the golden arrow shows on hover
    -- (left margin = back, right margin = forward; no arrow when there is no page that way)
    main.edgeZones = {}
    local function makeEdgeZone(dir, x1, x2)
        local z = CreateFrame("Button", nil, book)
        z:SetFrameLevel(book:GetFrameLevel() + 8)
        z:SetWidth(x2 - x1)
        z:SetHeight(UI.PAGE_Y2 - UI.PAGE_Y1)
        z:SetPoint("TOPLEFT", book, "TOPLEFT", x1, UI.PAGES_TOP - UI.PAGE_Y1)
        z:EnableMouseWheel(true)
        z:SetScript("OnMouseWheel", function()
            UI.OnWheel(arg1)
        end)
        local arrow = z:CreateTexture(nil, "OVERLAY")
        arrow:SetWidth(UI.EDGE_ARROW_W)
        arrow:SetHeight(UI.EDGE_ARROW_H)
        arrow:SetPoint("CENTER", z, "CENTER", 0, UI.EDGE_ARROW_Y)
        if not UI.SetPiece(arrow, "page_arrow") then
            arrow:SetTexture("Interface\\Buttons\\UI-SpellbookIcon-" .. (dir > 0 and "NextPage" or "PrevPage") .. "-Up")
        elseif dir < 0 then
            arrow:SetTexCoord(1, 0, 0, 1)
        end
        arrow:Hide()
        z.arrow = arrow
        z.dir = dir
        z.Refresh = function(self)
            local target = (UI.state and UI.state.page or 1) + self.dir
            if self.hover and target >= 1 and target <= UI.PageCount() then
                self.arrow:Show()
            else
                self.arrow:Hide()
            end
        end
        z:SetScript("OnEnter", function()
            this.hover = true
            this:Refresh()
        end)
        z:SetScript("OnLeave", function()
            this.hover = false
            this:Refresh()
        end)
        z:SetScript("OnClick", function()
            UI.ChangePage(this.dir)
            this:Refresh()
        end)
        z:SetScript("OnHide", function()
            this.hover = false
            this.arrow:Hide()
        end)
        main.edgeZones[dir] = z
        return z
    end
    makeEdgeZone(-1, UI.PAGE_X1 - 2, UI.GRID_X - 2)
    makeEdgeZone(1, UI.GRID_X + 4 * UI.CARD_W + 3 * UI.COL_GAP + 2, UI.PAGE_X2 + 2)

    local pageText = book:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    pageText:SetPoint("CENTER", book, "TOPLEFT", UI.PAGE_CX, pageY)
    pageText:SetWidth(90)
    main.pageText = pageText

    -- class tabs along the top edge (All is not a tab any more: the tab of a class, or Neutral)
    local n = 0
    for i = 1, table.getn(CM.CLASS_TABS) do
        local def = CM.CLASS_TABS[i]
        if def.key ~= "ALL" then
            n = n + 1
            local b, tex = makeClassTab(n, def, tabsFrame)
            UI.classTabs[n] = { key = def.key, label = def.label, button = b, textured = tex }
        end
    end
    UI.LayoutClassTabsFor(main)

    -- ---- a tab without its window: honest "not yet" panel
    local unavail = makePage(main, UI.PAGE_X1, UI.PAGES_TOP - UI.PAGE_Y1, UI.PAGE_X2 - UI.PAGE_X1, UI.PAGE_Y2 - UI.PAGE_Y1)
    unavail:SetFrameLevel(L + 6)
    local uTitle = unavail:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    uTitle:SetPoint("CENTER", unavail, "CENTER", 0, 30)
    local uText = unavail:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
    uText:SetPoint("CENTER", unavail, "CENTER", 0, -6)
    uText:SetWidth(480)
    unavail:Hide()
    main.unavailable = unavail
    main.unavailableTitle = uTitle
    main.unavailableText = uText

    -- ---- packs (PacksUI.lua): the panel is the whole window below the header (above the art, below the close button)
    UI.panelX = 0
    UI.panelY = -UI.HEADER_H
    if H.PacksUI and H.PacksUI.Build then
        main.packs = H.PacksUI.Build(main, UI.panelX, UI.panelY, L + 6)
    end

    -- ---- hero portraits (HeroesUI.lua): the same page area as the Packs tab
    if H.HeroesUI and H.HeroesUI.Build then
        main.heroes = H.HeroesUI.Build(main, UI.panelX, UI.PAGES_TOP - UI.HEROES_DROP, L + 6)   -- the size of the book, under the class tabs
    end

    -- ---- decks (DecksUI.lua): the deck panel right of the book, the class picker over the book
    if H.DecksUI and H.DecksUI.BuildPanel then
        main.sidePanel = H.DecksUI.BuildPanel(main, UI.BOOK_W, -UI.HEADER_H)
        if H.DecksUI.BuildPicker then
            main.picker = H.DecksUI.BuildPicker(main, UI.PAGE_CX, UI.PAGES_TOP - 380, book:GetFrameLevel() + 30)
        end
    end

    -- ---- the craft panel (CraftUI.lua): over the book, above the cards
    if H.CraftUI and H.CraftUI.Build then
        main.craft = H.CraftUI.Build(main, UI.PAGE_CX, UI.PAGES_TOP - 360, book:GetFrameLevel() + 32)
    end

    -- ---- close button (top right corner of the window, in the header strip)
    local close = CreateFrame("Button", "HearthstoneFrameCloseButton", main, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", main, "TOPRIGHT", UI.CLOSE_X, UI.CLOSE_Y)
    close:SetFrameLevel(book:GetFrameLevel() + 40)
    close:SetScript("OnClick", function()
        main:Hide()
    end)

    -- ---- bottom tabs (Collection, Packs): the plate of the sheet with dark ink on it, or the native tab buttons
    main.nativeTabs = UI.Piece("btn_blank") == nil
    for i = 1, table.getn(UI.BOTTOM_TABS) do
        local t
        if main.nativeTabs then
            t = CreateFrame("Button", "HearthstoneFrameTab" .. i, main, "CharacterFrameTabButtonTemplate")
            t:SetText(UI.BOTTOM_TABS[i])
            if i == 1 then
                t:SetPoint("TOPLEFT", main, "BOTTOMLEFT", 20, 7)
            else
                t:SetPoint("LEFT", getglobal("HearthstoneFrameTab" .. (i - 1)), "RIGHT", -16, 0)
            end
        else
            t = UI.MakeButton("HearthstoneFrameTab" .. i, main, UI.BTAB_W, UI.BTAB_H, UI.BOTTOM_TABS[i], book:GetFrameLevel() + 10)
            t:SetPoint("TOPLEFT", main, "BOTTOMLEFT", UI.BTAB_X0 + (i - 1) * UI.BTAB_PITCH, UI.BTAB_Y)
        end
        t:SetID(i)
        local id = i
        t:SetScript("OnClick", function()
            UI.SetBottomTab(id)
        end)
    end
    if main.nativeTabs then
        if PanelTemplates_SetNumTabs then
            PanelTemplates_SetNumTabs(main, table.getn(UI.BOTTOM_TABS))
        end
    else
        main.numTabs = table.getn(UI.BOTTOM_TABS)
    end

    -- ---- window behaviour
    main:SetScript("OnShow", function()
        UI.OnShow()
        if H.MicroButton then
            H.MicroButton.UpdateState()
        end
    end)
    main:SetScript("OnHide", function()
        if H.MicroButton then
            H.MicroButton.UpdateState()
        end
        UI.ClearHover()
        if H.PacksUI and H.PacksUI.ClearHover then
            H.PacksUI.ClearHover()
        end
        if H.DecksUI then
            if H.DecksUI.ClearHover then
                H.DecksUI.ClearHover()
            end
            if H.DecksUI.OnWindowHidden then
                H.DecksUI.OnWindowHidden()
            end
        end
        UI.HidePreview()
        hideTip()
        if H.CraftUI and H.CraftUI.Hide then
            H.CraftUI.Hide()
        end
        -- closing the window ends crafting (the server keeps the access until the character walks away, but the
        -- window opens as the plain collection again; the next click on the NPC opens crafting anew)
        if HsWallet and HsWallet.craftOpen then
            HsWallet.SetAccess(false)
            if H.NotifyWallet then
                H.NotifyWallet()
            end
        end
        if PlaySound then
            PlaySound("igCharacterInfoClose")
        end
    end)
    if UISpecialFrames then
        local have = false
        for i = 1, table.getn(UISpecialFrames) do
            if UISpecialFrames[i] == "HearthstoneFrame" then
                have = true
            end
        end
        if not have then
            table.insert(UISpecialFrames, "HearthstoneFrame")
        end
    end

    UI.BuildPreview()
    UI.frame = main
    UI.ApplyBookArt()
    return main
end

-- Sets the class tabs to their first positions while the window is built (before UI.frame is set).
function UI.LayoutClassTabsFor(main)
    for i = 1, table.getn(UI.classTabs) do
        local t = UI.classTabs[i]
        t.shown = true
        t.x = UI.TAB_X0 + (i - 1) * UI.TAB_PITCH
        t.button:ClearAllPoints()
        t.button:SetPoint("TOPLEFT", main, "TOPLEFT", t.x, UI.TAB_Y)
    end
end

-- The enlarged card, shown beside the window while the mouse is over a card.
function UI.BuildPreview()
    if UI.preview then
        return UI.preview
    end
    local f = CreateFrame("Frame", "HearthstoneCardPreview", UIParent)
    f:SetWidth(UI.PREVIEW_W)
    f:SetHeight(UI.PREVIEW_H)
    f:SetFrameStrata("TOOLTIP")
    f:SetClampedToScreen(true)
    f:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    f:SetBackdropColor(0, 0, 0, 0.9)
    f:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    local widget = H.CardWidget.Create(f, "HearthstoneCardPreviewCard", 256, true)
    widget.frame:SetPoint("TOP", f, "TOP", 0, -8)
    local caption = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    caption:SetPoint("TOPLEFT", f, "TOPLEFT", 12, -404)
    caption:SetWidth(UI.PREVIEW_W - 24)
    caption:SetJustifyH("LEFT")
    f:Hide()
    UI.preview = { frame = f, widget = widget, caption = caption }
    return UI.preview
end

-- Sets the search box text without triggering another query.
function UI.SettingSearchText(text)
    local f = UI.frame
    if not f then
        return
    end
    UI.settingSearch = true
    f.searchBox:SetText(text)
    UI.settingSearch = false
    if UI.searchHint then
        UI.searchHint()
    end
end

-- Saves / restores the window position (left and top edge in the window's own scale units).
function UI.SavePosition()
    local main = UI.frame
    if not main or not H.db then
        return
    end
    local l, t = main:GetLeft(), main:GetTop()
    if l and t then
        H.db.pos = { left = l, top = t, scale = main:GetScale() }
    end
end

-- The saved corner is converted when the window now has another scale (other screen).
-- Returns true when a saved position was applied; the default position never overrides one.
function UI.RestorePosition()
    local main = UI.frame
    local pos = H.db and H.db.pos
    if main and pos then
        local f = 1
        local scale = main:GetScale()
        if type(pos.scale) == "number" and pos.scale > 0 and scale and scale > 0 then
            f = pos.scale / scale
        end
        main:ClearAllPoints()
        main:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", pos.left * f, pos.top * f)
        return true
    end
    return false
end

-- Where the window opens until the player has moved it: centred on the screen, but
-- right of the player and target frames (UI.CLEAR_LEFT) as far as the screen allows. The corner is
-- worked out for the window as it is now (scale) including the bottom tabs, so everything stays
-- on the screen. Returns left, top in screen units.
function UI.DefaultCorner()
    local main = UI.frame
    local screenW, screenH = 1024, 768
    if GetScreenWidth then
        screenW, screenH = GetScreenWidth(), GetScreenHeight()
    end
    local scale = (main and main:GetScale()) or 1
    local w = (UI.WindowWidth() + UI.TABS_OVERHANG) * scale
    local h = (UI.HEIGHT + UI.BOTTOM_OVERHANG) * scale
    return CM.DefaultPosition(screenW, screenH, w, h, UI.CLEAR_LEFT)
end

function UI.PlaceDefault()
    local main = UI.frame
    if not main then
        return
    end
    local left, top = UI.DefaultCorner()
    local scale = main:GetScale() or 1
    main:ClearAllPoints()
    main:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left / scale, top / scale)
end

-- /hs center: forgets the saved position and puts the window in the middle of the screen.
function UI.CenterWindow()
    if H.db then
        H.db.pos = nil
    end
    if UI.frame then
        UI.PlaceDefault()
        UI.KeepTabsOnScreen()
    end
    return "collection window centred"
end

function UI.OnShow()
    local main = UI.frame
    if not main then
        return
    end
    -- retry a failed deck load, pick the layout of the current mode, then shrink on small screens
    -- (for example 800x600) so that the whole window fits
    if UI.state.bottom == 1 and main.sidePanel and H.DecksUI then
        H.DecksUI.OnTabShown()
    end
    if H.DecksUI and H.DecksUI.OnWindowShown then
        H.DecksUI.OnWindowShown()
    end
    UI.ApplyLayout(true)
    local screenW, screenH = 1024, 768
    if GetScreenWidth then
        screenW, screenH = GetScreenWidth(), GetScreenHeight()
    end
    main:SetScale(CM.FitScale(screenW, screenH, UI.WIDTH + UI.TABS_OVERHANG, UI.HEIGHT + UI.BOTTOM_OVERHANG))
    if not UI.RestorePosition() then
        UI.PlaceDefault()
    end
    UI.ApplyBookArt()
    UI.KeepTabsOnScreen()
    if PlaySound then
        PlaySound("igCharacterInfoOpen")
    end
    UI.StyleBottomTabs(UI.state.bottom)
    UI.Requery(true)
    if UI.state.bottom == 2 and main.packs and H.PacksUI then
        H.PacksUI.OnTabShown()
    end
    if UI.state.bottom == UI.HEROES_TAB and main.heroes and H.HeroesUI then
        H.HeroesUI.OnTabShown()
    end
end

-- Builds the window on first use. A failure is reported once instead of raising.
function UI.EnsureBuilt()
    if UI.frame then
        return true
    end
    if not CreateFrame then
        return false
    end
    local ok, err = pcall(UI.Build)
    if not ok or not UI.frame then
        -- a partial build may have left a named frame behind: hide it so it cannot pop up later
        UI.frame = nil
        UI.preview = nil
        local stale = getglobal and getglobal("HearthstoneFrame")
        if stale then
            stale:Hide()
        end
        if H.PrintError then
            H.PrintError("Could not build the collection window: " .. tostring(err))
        end
        return false
    end
    return true
end

function UI.Show()
    if UI.EnsureBuilt() then
        UI.frame:Show()
        return true
    end
    return false
end

function UI.Hide()
    if UI.frame then
        UI.frame:Hide()
    end
end

function UI.Toggle()
    if not UI.EnsureBuilt() then
        return false
    end
    if UI.frame:IsShown() then
        UI.frame:Hide()
    else
        UI.frame:Show()
    end
    return true
end

-- The single entry point used by the micro button, the key binding and /hs collection.
function H.ToggleCollection()
    return UI.Toggle()
end

function Hearthstone_ToggleCollection()
    return UI.Toggle()
end

-- Saved variables were (re)loaded: apply the stored tab and replace the map micro button.
function H.OnSavedLoaded()
    UI.state.inited = false
    if H.MicroButton and H.MicroButton.Refresh then
        H.MicroButton.Refresh()
    end
end
