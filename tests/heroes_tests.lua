-- Hero portrait model and routing tests (reward track). Called from lua_tests.lua:
--     dofile("tests/heroes_tests.lua")({ H = H, check = check, T = packHelpers, reset = resetAddon, drain = drain,
--                                        printed = function() return printed end })
-- Lua 5.0 style, like the addon.

return function(ctx)
    local H, check, E = ctx.H, ctx.check, HsHeroes
    local T = ctx.T()

    local function installTextures(set)
        HS_HeroTextures = { generated = true, count = 0, set = set }
    end

    -- ---------------------------------------------------------------- model
    local savedTextures = HS_HeroTextures
    installTextures({})
    E.Reset()
    check(E.state == "none" and E.credits == 0 and E.op == nil and not E.TabVisible(), "reset")

    check(E.Default("MAGE") == "HERO_08" and E.Default("WARRIOR") == "HERO_01" and E.Default("NOPE") == nil, "class defaults")
    local class, entry = E.Find("HERO_08c")
    check(class == "MAGE" and entry.name == "Fire Mage Jaina" and not entry.default, "Find an alternative")
    check(E.Find("EX1_323h") == nil and E.Find(nil) == nil, "Find: Jaraxxus and nil are not in the catalog")
    check(E.ClassOfDefault("HERO_08") == "MAGE" and E.ClassOfDefault("HERO_08c") == nil and E.ClassOfDefault("EX1_323h") == nil,
        "only class defaults map to a class")
    local total = 0
    for i = 1, table.getn(E.CLASSES) do
        local list = E.List(E.CLASSES[i])
        check(table.getn(list) >= 7 and list[1].default and list[1].owned, "class " .. E.CLASSES[i] .. " lists its default first")
        total = total + table.getn(list)
    end
    check(total == 80, "the catalog has 80 portraits: " .. total)

    local r = E.Parse("credits=1;own=HERO_08c,HERO_06c;sel=MAGE.HERO_08c")
    check(r and r.credits == 1 and r.own.HERO_08c and r.own.HERO_06c and r.sel.MAGE == "HERO_08c", "HEROES parsed")
    r = E.Parse("credits=0;own=;sel=")
    check(r and r.credits == 0 and next(r.own) == nil and next(r.sel) == nil, "empty HEROES parsed")
    for _, bad in ipairs({ "", "credits=1", "credits=01;own=;sel=", "credits=-1;own=;sel=", "credits=100000;own=;sel=",
        "credits=1;own=a b;sel=", "credits=1;own=,HERO_08c;sel=", "credits=1;own=;sel=mage.HERO_08c",
        "credits=1;own=;sel=MAGE", "credits=1;own=;sel=MAGE.", "credits=x;own=;sel=" }) do
        check(E.Parse(bad) == nil, "bad HEROES rejected: " .. bad)
    end
    check(E.CapsHaveHeroes("solo,heroes,wallet") and E.CapsHaveHeroes("heroes") and not E.CapsHaveHeroes("solo,hero")
        and not E.CapsHaveHeroes(nil), "caps")

    -- PortraitFor / ArtFor: the choice counts only when ready, owned, of the class and its texture is installed
    E.SetReady({ credits = 0, own = { HERO_08c = true }, sel = { MAGE = "HERO_08c" } })
    check(E.PortraitFor("MAGE") == "HERO_08", "no texture installed: the default shows")
    installTextures({ HERO_08c = 0.75 })
    check(E.PortraitFor("MAGE") == "HERO_08c", "installed and owned: the choice shows")
    check(E.PortraitFor("DRUID") == "HERO_06" and E.PortraitFor("NOPE") == nil, "other classes keep the default")
    E.SetReady({ credits = 0, own = {}, sel = { MAGE = "HERO_08c" } })
    check(E.PortraitFor("MAGE") == "HERO_08", "a choice that is not owned is ignored")
    E.SetReady({ credits = 0, own = { HERO_06c = true }, sel = { MAGE = "HERO_06c" } })
    installTextures({ HERO_06c = 0.75 })
    check(E.PortraitFor("MAGE") == "HERO_08", "a choice of another class is ignored")
    E.SetReady({ credits = 0, own = { HERO_08c = true }, sel = { MAGE = "HERO_08c" } })
    installTextures({ HERO_08c = 0.75 })
    check(E.ArtFor("HERO_08", true) == "HERO_08c", "own hero: the portrait replaces the class default")
    check(E.ArtFor("HERO_08", false) == "HERO_08", "an opponent keeps the default")
    check(E.ArtFor("EX1_323h", true) == "EX1_323h" and E.ArtFor("HERO_08c", true) == "HERO_08c", "Jaraxxus and non-defaults never substituted")
    check(E.ArtFor("HERO_01", true) == "HERO_01", "a class without a choice keeps the default")
    local path, bottom = E.Texture("HERO_08c")
    check(path == "Interface\\AddOns\\Hearthstone\\Textures\\Heroes\\HERO_08c" and bottom == 0.75, "texture path and texcoord")
    check(E.Texture("HERO_08") == nil and E.Texture("HERO_08b") == nil, "no texture for a default or a missing file")
    E.state = "loading"
    check(E.PortraitFor("MAGE") == "HERO_08", "not ready: defaults")
    E.SetReady({ credits = 0, own = { HERO_08c = true }, sel = { MAGE = "HERO_08c" } })

    -- tab visibility and the client checks
    check(E.TabVisible(), "an unlock shows the tab")
    E.SetReady({ credits = 0, own = {}, sel = {} })
    check(not E.TabVisible(), "nothing to show: the tab is hidden")
    E.SetReady({ credits = 1, own = {}, sel = {} })
    check(E.TabVisible(), "a credit shows the tab")
    check(E.SelectBlocker("MAGE", "HERO_08") == nil, "the default is always selectable")
    check(E.SelectBlocker("MAGE", "HERO_08c") == E.LOCKED_TEXT, "a locked portrait cannot be selected")
    check(E.SelectBlocker("MAGE", "HERO_06c") == E.WHY_TEXT.class and E.SelectBlocker("MAGE", "nope") == E.WHY_TEXT.unknown,
        "wrong class and unknown id")
    check(E.UnlockBlocker("HERO_08c") == nil, "a credit unlocks a locked portrait")
    check(E.UnlockBlocker("HERO_08") == E.WHY_TEXT.unknown and E.UnlockBlocker("nope") == E.WHY_TEXT.unknown, "defaults and unknown ids")
    E.SetReady({ credits = 0, own = { HERO_08c = true }, sel = {} })
    check(E.UnlockBlocker("HERO_08c") == E.WHY_TEXT.owned and E.UnlockBlocker("HERO_08b") == E.LOCKED_TEXT, "owned, no credit")

    E.Reset()
    local op = E.BeginOp("unlock", "MAGE", "HERO_08c")
    check(string.len(op.key) == 12 and string.find(op.key, "^[0-9a-z]+$") and E.IsBusy(), "unlock op gets a key")
    check(E.OpPayload(op) == "key=" .. op.key .. ";id=HERO_08c", "HERO_UNLOCK payload")
    op = E.BeginOp("select", "MAGE", "HERO_08")
    check(op.key == nil and E.OpPayload(op) == "class=MAGE;id=default", "selecting the default sends 'default'")
    op = E.BeginOp("select", "MAGE", "HERO_08c")
    check(E.OpPayload(op) == "class=MAGE;id=HERO_08c", "HERO_SELECT payload")
    E.Reset()

    -- ----------------------------------------------------------------- flow
    ctx.reset()
    local notified = 0
    local savedHook = H.OnHeroesChanged
    H.OnHeroesChanged = function() notified = notified + 1 end
    local function flush() ctx.drain(1) end
    local function lastSent(op)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end
    local function lastPrinted()
        local p = ctx.printed()
        return p[table.getn(p)]
    end

    -- nothing before WELCOME
    local k0, why0 = H.HeroSelect("MAGE", "HERO_08")
    check(k0 == nil and why0 == "Not connected to the server." and H.HeroGet() == nil, "nothing before WELCOME")
    T.frame("HERO_CHANGED", "0", "why=gm")
    check(E.state == "none" and table.getn(H.queue) == 0, "a push before WELCOME is ignored")

    -- a server without heroes
    T.welcome("solo,chunk,packs,decks,wallet")
    check(E.state == "unsupported" and table.getn(T.queuedOps("HERO_GET")) == 0, "no heroes caps: unsupported")
    local _, why1 = H.HeroSelect("MAGE", "HERO_08")
    check(why1 == "This server does not have alternative hero portraits yet.", "unsupported: " .. tostring(why1))
    flush()
    H.queue = {}

    -- with heroes: HERO_GET, then HEROES
    T.welcome("solo,chunk,packs,decks,wallet,heroes")
    local q = T.queuedOps("HERO_GET")
    check(table.getn(q) == 1 and q[1].payload == "" and E.state == "loading" and H.heroes.reqId == q[1].reqId, "HERO_GET sent")
    check(H.HeroGet() == H.heroes.reqId and H.heroes.refetch, "a second call marks a refetch")
    local _, why2 = H.HeroSelect("MAGE", "HERO_08")
    check(why2 == "The portraits are not loaded.", "not loaded yet: " .. tostring(why2))
    flush()
    local getId = H.heroes.reqId
    T.frame("HEROES", "zz", "credits=1;own=;sel=")
    check(E.state == "loading", "a HEROES for another reqId is ignored")
    local before = notified
    T.frame("HEROES", getId, "credits=1;own=HERO_08c;sel=")
    check(E.IsReady() and E.credits == 1 and E.own.HERO_08c and notified > before, "HEROES applied")
    check(table.getn(T.queuedOps("HERO_GET")) == 1, "the refetch went out")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=1;own=HERO_08c;sel=")
    check(H.heroes.reqId == nil, "nothing in flight")
    check(E.TabVisible(), "the tab shows")

    -- a locked click sends nothing
    H.queue = {}
    local k1, why3 = H.HeroSelect("MAGE", "HERO_08b")
    check(k1 == nil and why3 == E.LOCKED_TEXT and table.getn(T.queuedOps("HERO_SELECT")) == 0 and not E.IsBusy(),
        "a locked portrait sends nothing: " .. tostring(why3))
    local k2, why4 = H.HeroUnlock("HERO_08")
    check(k2 == nil and why4 == E.WHY_TEXT.unknown and table.getn(T.queuedOps("HERO_UNLOCK")) == 0, "a default cannot be unlocked")

    -- select an owned portrait
    local sel = H.HeroSelect("MAGE", "HERO_08c")
    local qs = T.queuedOps("HERO_SELECT")
    check(sel and table.getn(qs) == 1 and qs[1].payload == "class=MAGE;id=HERO_08c" and E.IsBusy(), "HERO_SELECT queued")
    local _, why5 = H.HeroSelect("MAGE", "HERO_08")
    local _, why6 = H.HeroUnlock("HERO_08b")
    check(why5 == "A portrait change is in progress." and why6 == why5 and table.getn(T.queuedOps("HERO_SELECT")) == 1,
        "one request at a time")
    flush()
    T.frame("HEROES", "zz", "credits=1;own=HERO_08c;sel=MAGE.HERO_08c")
    check(E.IsBusy() and E.sel.MAGE == nil, "a reply for another reqId is ignored")
    T.frame("HEROES", lastSent("HERO_SELECT").reqId, "credits=1;own=HERO_08c;sel=MAGE.HERO_08c")
    check(not E.IsBusy() and E.op == nil and E.sel.MAGE == "HERO_08c", "HERO_SELECT confirmed")
    check(H.pending[lastSent("HERO_SELECT").reqId] == nil, "the request is answered")

    -- unlock with the credit
    H.queue = {}
    local ku = H.HeroUnlock("HERO_08b")
    local qu = T.queuedOps("HERO_UNLOCK")
    local key = E.op and E.op.key
    check(ku and table.getn(qu) == 1 and key and qu[1].payload == "key=" .. key .. ";id=HERO_08b", "HERO_UNLOCK queued")
    flush()
    -- an unreadable outcome (timeout): sent again with the same key
    H.OnHeroOpFailed("TIMEOUT", "no response", nil, false)
    local qu2 = T.queuedOps("HERO_UNLOCK")
    check(table.getn(qu2) == 1 and qu2[1].payload == "key=" .. key .. ";id=HERO_08b" and E.op.attempts == 2, "a timeout sends the same key again")
    flush()
    T.frame("HEROES", lastSent("HERO_UNLOCK").reqId, "credits=0;own=HERO_08b,HERO_08c;sel=MAGE.HERO_08c")
    check(E.op == nil and E.credits == 0 and E.own.HERO_08b and E.message == "Unlocked Khadgar.", "unlock confirmed: " .. tostring(E.message))

    -- a refusal is definitive
    H.queue = {}
    E.SetReady({ credits = 1, own = {}, sel = {} })
    H.HeroUnlock("HERO_08a")
    flush()
    T.err(lastSent("HERO_UNLOCK").reqId, "REFUSED", "no credit")
    check(E.op == nil, "a refusal ends the op")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=1;own=;sel=")
    H.queue = {}
    H.HeroUnlock("HERO_08a")
    flush()
    T.frame("ERR", lastSent("HERO_UNLOCK").reqId, "code=REFUSED;msg=nocredit;why=nocredit")
    check(E.op == nil and E.message == E.WHY_TEXT.nocredit and table.getn(T.queuedOps("HERO_GET")) >= 1, "refusal text, then a resync")
    check(not string.find(lastPrinted() or "", "collection could not be loaded", 1, true), "no collection error for a hero request")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=;sel=")

    -- give up after E.MAX_ATTEMPTS
    H.queue = {}
    E.SetReady({ credits = 1, own = {}, sel = {} })
    H.HeroUnlock("HERO_08a")
    for _ = 1, E.MAX_ATTEMPTS do
        flush()
        H.OnHeroOpFailed("TIMEOUT", "no response", nil, false)
    end
    check(E.op == nil and table.getn(T.sentOps("HERO_UNLOCK")) >= E.MAX_ATTEMPTS, "gave up after the attempts")
    check(string.find(E.message or "", "did not confirm", 1, true) ~= nil and table.getn(T.queuedOps("HERO_GET")) >= 1, "resync after giving up")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=1;own=;sel=")

    -- pushes
    H.queue = {}
    T.frame("HERO_CHANGED", "0", "why=gm")
    check(table.getn(T.queuedOps("HERO_GET")) == 1, "HERO_CHANGED refetches")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=2;own=;sel=")
    T.frame("HERO_CHANGED", "0", "why=legend1")
    check(string.find(lastPrinted() or "", "Legend #1", 1, true) ~= nil and table.getn(T.queuedOps("HERO_GET")) == 1,
        "Legend #1 push tells the player and refetches")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=2;own=;sel=")

    -- HERO_GET failures
    T.frame("HERO_CHANGED", "0", "why=gm")
    flush()
    T.err(H.heroes.reqId, "UNAVAILABLE", "tables")
    check(H.heroes.reqId == nil and E.state == "error", "HERO_GET failure recorded")
    check(not string.find(lastPrinted() or "", "collection could not be loaded", 1, true), "UNAVAILABLE of HERO_GET is not a collection error")
    H.queue = {}
    check(H.HeroGet() ~= nil, "HERO_GET can be retried")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=;sel=")
    check(E.IsReady(), "ready again")

    -- an unreadable reply to a HERO_GET is an error, not a state change
    T.frame("HERO_CHANGED", "0", "why=gm")
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=x;own=;sel=")
    check(E.state == "error", "unreadable HEROES: error")
    H.queue = {}
    H.HeroGet()
    flush()
    T.frame("HEROES", H.heroes.reqId, "credits=0;own=;sel=")

    -- /hs heroes without the UI prints the state
    ctx.printedReset()
    H.Slash("heroes")
    -- with HeroesUI.lua loaded the tab opens; nothing unlocked yet, so the player is told how to get a portrait
    check(lastPrinted() == E.LOCKED_TEXT or string.find(lastPrinted() or "", "heroes: 0 portraits unlocked, 0 credits", 1, true) ~= nil,
        "/hs heroes: " .. tostring(lastPrinted()))

    -- VERSION error: back to nothing
    T.frame("ERR", "0", "code=DISABLED;msg=off")
    check(E.state == "none" and not H.heroes.supported, "DISABLED resets the portraits")

    H.OnHeroesChanged = savedHook
    HS_HeroTextures = savedTextures
    E.Reset()
end
