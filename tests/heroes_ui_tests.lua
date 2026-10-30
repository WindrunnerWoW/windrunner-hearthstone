-- Heroes tab tests (reward track). Called from lua_tests.lua with the shared helpers:
--     dofile("tests/heroes_ui_tests.lua")({ H = H, check = check, T = packHelpers, reset = resetAddon, drain = drain,
--                                           ROOT = ROOT })
-- Lua 5.0 style, like the addon.

return function(ctx)
    local H, check, E = ctx.H, ctx.check, HsHeroes
    local F = dofile(ctx.ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local HU = H.HeroesUI
    local CM = HsCollection
    local T = ctx.T()
    local savedDb = H.db
    local savedTextures = HS_HeroTextures

    local function resetUI()
        UI.frame, UI.preview, UI.hover = nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons = {}, {}, {}
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited = nil, nil, false
        HU.panel, HU.cells, HU.class, HU.confirmId = nil, {}, nil, nil
        H.MicroButton.frame, H.MicroButton.icon, H.MicroButton.border = nil, nil, nil
    end
    local function flush() ctx.drain(1) end
    local function lastSent(op)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end
    local function tab3()
        return getglobal("HearthstoneFrameTab" .. UI.HEROES_TAB)
    end
    local function shownCells()
        local n = 0
        for i = 1, table.getn(HU.cells) do
            if HU.cells[i].button:IsShown() then n = n + 1 end
        end
        return n
    end

    ctx.reset()
    F.install({ class = { "Mage", "MAGE" }, realm = "Realm A" })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()
    HS_HeroTextures = { generated = true, count = 2, set = { HERO_08c = 0.7578125, HERO_08b = 0.7578125 } }

    T.welcome("solo,chunk,packs,decks,wallet,heroes")
    flush()
    T.coll(1)
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=;sel=")
    flush()

    -- nothing unlocked, no credit: the tab is hidden
    H.Slash("collection")
    local main = UI.frame
    check(main and main:IsShown() and HU.panel ~= nil, "the window and the heroes panel exist")
    check(tab3() and not tab3():IsShown(), "no unlock and no credit: the Heroes tab is hidden")
    check(not HU.panel:IsShown(), "the Heroes panel is hidden on the collection tab")
    UI.SetBottomTab(UI.HEROES_TAB)
    check(UI.state.bottom == 1, "the hidden tab cannot be selected")

    -- a credit arrives: the tab appears, with the banner
    T.frame("HERO_CHANGED", "0", "why=legend1")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=1;own=;sel=")
    check(tab3():IsShown(), "a credit shows the Heroes tab")
    UI.SetBottomTab(UI.HEROES_TAB)
    check(UI.state.bottom == 3 and HU.panel:IsShown() and not main.sidePanel:IsShown(), "the Heroes tab: its panel, no deck panel")
    check(UI.WindowWidth() == UI.WIDTH_BOOK and main:GetWidth() == UI.WIDTH_BOOK, "the window is as narrow as on the Packs tab")
    check(main.title:GetText() == "", "no title text, the parchment has its ribbon: " .. tostring(main.title:GetText()))
    check(HU.class == "MAGE", "opens on the player's class")
    check(HU.panel.banner:GetText() == "Legend #1: choose a portrait to unlock (1)", "banner: " .. tostring(HU.panel.banner:GetText()))
    check(table.getn(HU.cells) == HU.PER_PAGE and HU.PER_ROW == 4, "eight cells, four to a row")

    -- the Mage page: 10 portraits, the default selected, the rest unlockable
    check(shownCells() == 8 and HU.PageCount() == 2, "eight portraits on page 1 of 2 for Mage: " .. shownCells())
    check(HU.cells[1].name:GetText() == "Jaina Proudmoore" and HU.cells[1].pick:IsShown(), "the default comes first and is selected")
    HU.ChangePage(1)
    check(HU.page == 2 and shownCells() == 2, "page 2 holds the last two portraits: " .. shownCells())
    HU.ChangePage(1)
    check(HU.page == 2, "no page past the last")
    HU.ChangePage(-1)
    check(HU.page == 1 and shownCells() == 8, "back to page 1")
    check(HU.cells[2].name:GetText() == "Fire Mage Jaina" and not HU.cells[2].pick:IsShown(), "an alternative follows")
    check(string.find(HU.cells[2].status:GetText(), "Click to unlock", 1, true) ~= nil
        and string.find(HU.cells[2].status:GetText(), "art not installed", 1, true) == nil, "installed + credit: click to unlock")
    check(string.find(HU.cells[5].status:GetText(), "art not installed", 1, true) ~= nil, "a portrait without its texture says so")

    -- a class with 7 portraits shows 7 cells
    F.fire(UI.classTabs[9].button, "OnClick", {})
    check(HU.class == "WARRIOR" and shownCells() == 7, "Warrior shows 7 portraits: " .. shownCells())
    F.fire(UI.classTabs[3].button, "OnClick", {})
    check(HU.class == "MAGE", "back to Mage")

    -- unlock: the click opens the confirm box, nothing is sent until Unlock
    H.queue = {}
    F.fire(HU.cells[2].button, "OnClick", {})
    check(HU.panel.confirm:IsShown() and HU.confirmId == "HERO_08c" and table.getn(T.queuedOps("HERO_UNLOCK")) == 0, "confirm box, nothing sent")
    check(string.find(HU.panel.confirm.text:GetText(), "Unlock Fire Mage Jaina?", 1, true) ~= nil, "confirm text")
    F.fire(HU.panel.confirm.no, "OnClick", {})
    check(not HU.panel.confirm:IsShown() and table.getn(T.queuedOps("HERO_UNLOCK")) == 0, "Cancel sends nothing")
    F.fire(HU.cells[2].button, "OnClick", {})
    F.fire(HU.panel.confirm.yes, "OnClick", {})
    local uq = T.queuedOps("HERO_UNLOCK")
    check(table.getn(uq) == 1 and string.find(uq[1].payload, ";id=HERO_08c", 1, true) ~= nil and not HU.panel.confirm:IsShown(),
        "Unlock sends HERO_UNLOCK")
    flush()
    T.frame("HEROES", lastSent("HERO_UNLOCK").reqId, "credits=0;own=HERO_08c;sel=")
    check(HU.panel.message:GetText() == "Unlocked Fire Mage Jaina.", "message: " .. tostring(HU.panel.message:GetText()))
    check(string.find(HU.cells[2].status:GetText(), "Click to select", 1, true) ~= nil, "now owned")
    check(string.find(HU.cells[3].status:GetText(), "Locked", 1, true) ~= nil, "others locked without a credit")
    check(string.find(HU.panel.banner:GetText(), "Reach Legend #1", 1, true) ~= nil, "no credit: the hint banner")

    -- a locked click without a credit sends nothing
    H.queue = {}
    F.fire(HU.cells[3].button, "OnClick", {})
    check(table.getn(T.queuedOps("HERO_UNLOCK")) == 0 and not HU.panel.confirm:IsShown(), "a locked click sends nothing")
    check(HU.panel.message:GetText() == E.LOCKED_TEXT, "and says why: " .. tostring(HU.panel.message:GetText()))

    -- select the owned alternative
    H.queue = {}
    F.fire(HU.cells[2].button, "OnClick", {})
    local sq = T.queuedOps("HERO_SELECT")
    check(table.getn(sq) == 1 and sq[1].payload == "class=MAGE;id=HERO_08c", "select sends HERO_SELECT")
    flush()
    T.frame("HEROES", lastSent("HERO_SELECT").reqId, "credits=0;own=HERO_08c;sel=MAGE.HERO_08c")
    check(HU.cells[2].pick:IsShown() and not HU.cells[1].pick:IsShown()
        and string.find(HU.cells[2].status:GetText(), "Selected", 1, true) ~= nil, "the choice is marked")
    local strip = HU.cells[2].hero.strips[1]
    local tex = strip.GetTexture and strip:GetTexture()
    check(tex == "Interface\\Hearthstone\\Textures\\Heroes\\HERO_08c", "the alternative's texture is drawn: " .. tostring(tex))
    F.fire(HU.cells[1].button, "OnClick", {})
    local sq2 = T.queuedOps("HERO_SELECT")
    check(sq2[table.getn(sq2)].payload == "class=MAGE;id=default", "back to the default sends 'default'")
    flush()
    T.frame("HEROES", lastSent("HERO_SELECT").reqId, "credits=0;own=HERO_08c;sel=")

    -- the other tabs keep working; the tab goes when everything is gone
    UI.SetBottomTab(1)
    check(UI.state.bottom == 1 and not HU.panel:IsShown() and main.sidePanel:IsShown() and main:GetWidth() == UI.WIDTH,
        "back on the collection tab")
    UI.SetBottomTab(UI.HEROES_TAB)
    T.frame("HERO_CHANGED", "0", "why=gm")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=;sel=")
    check(UI.state.bottom == 1 and not tab3():IsShown(), "a GM revoke hides the tab and returns to the collection")

    -- /hs heroes opens the tab when it is visible
    T.frame("HERO_CHANGED", "0", "why=gm")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=HERO_08c;sel=")
    main:Hide()
    H.Slash("heroes")
    check(main:IsShown() and UI.state.bottom == 3 and HU.panel:IsShown(), "/hs heroes opens the Heroes tab")

    -- ---- the call sites draw the selected portrait (own art only)
    T.frame("HERO_CHANGED", "0", "why=gm")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=HERO_08c;sel=MAGE.HERO_08c")
    local probe = HU.cells[1].hero.strips[1]
    local DU = H.DecksUI
    check(DU.SetTileArt(probe, "MAGE") == "crop" and probe:GetTexture() == "Interface\\Hearthstone\\Textures\\Heroes\\HERO_08c",
        "deck tile: the selected portrait, cropped")
    check(DU.SetTileArt(probe, "WARRIOR") ~= "crop" or probe:GetTexture() ~= "Interface\\Hearthstone\\Textures\\Heroes\\HERO_08c",
        "deck tile: another class keeps its own art")
    H.ArenaUI.SetHeroArt(probe and HU.cells[1].hero, "MAGE", E.PortraitFor("MAGE"))
    check(HU.cells[1].hero.strips[1]:GetTexture() == "Interface\\Hearthstone\\Textures\\Heroes\\HERO_08c", "arena hero art: the portrait")
    H.ArenaUI.SetHeroArt(HU.cells[1].hero, "MAGE")
    check(HU.cells[1].hero.strips[1]:GetTexture() ~= "Interface\\Hearthstone\\Textures\\Heroes\\HERO_08c", "arena hero art without a portrait id: the default")
    -- the match: only the own hero, only a class default
    check(E.ArtFor("HERO_08", true) == "HERO_08c" and E.ArtFor("HERO_08", false) == "HERO_08", "match: own hero only")
    local hero = { archW = 100, archH = 150, bleed = 0, slices = { { tex = probe, x1 = 0, y1 = 0, x2 = 100, y2 = 150 } } }
    H.MatchUI.SetHeroArt(hero, "HERO_08c")
    check(probe:GetTexture() == "Interface\\Hearthstone\\Textures\\Heroes\\HERO_08c", "MatchUI.SetHeroArt draws an alternative texture")

    H.db = savedDb
    HS_HeroTextures = savedTextures
    E.Reset()
end
