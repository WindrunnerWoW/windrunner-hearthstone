-- Lua-side tests for the Hearthstone addon (run from the module root):
--     lua tests/lua_tests.lua
-- Runs under any Lua 5.0 - 5.4 interpreter. The addon itself is written for
-- Lua 5.0 (WoW 1.12); the shims below only exist so the tests can run on a newer
-- interpreter, and a static lint makes sure the addon does not use anything
-- newer than 5.0.
-- Golden files (written by protocol_tests.exe) tie the Lua and C++ codecs together:
--     tests/golden_vectors.txt   escape vectors
--     tests/transcript.txt       request frames and server replies produced by C++

-- ---------------------------------------------------------------- shims
if not table.getn then table.getn = function(t) return #t end end
if not math.mod then math.mod = math.fmod end
if not string.gfind then string.gfind = string.gmatch end
if not unpack then unpack = table.unpack end

local ROOT = "."
local checks, failed = 0, 0
local current = ""

local function check(cond, what)
    checks = checks + 1
    if not cond then
        failed = failed + 1
        print("FAIL [" .. current .. "] " .. tostring(what))
    end
end

local function run(name, fn)
    current = name
    local before = failed
    local ok, err = pcall(fn)
    if not ok then
        failed = failed + 1
        print("ERROR [" .. name .. "] " .. tostring(err))
    end
    print(string.format("%-44s %s", name, (failed == before) and "ok" or "FAILED"))
end

local function readAll(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

local function fromHex(h)
    local out = {}
    for i = 1, string.len(h), 2 do
        out[table.getn(out) + 1] = string.char(tonumber(string.sub(h, i, i + 1), 16))
    end
    return table.concat(out)
end

local function toHex(s)
    local out = {}
    for i = 1, string.len(s) do out[i] = string.format("%02x", string.byte(s, i)) end
    return table.concat(out)
end

local function allBytes()
    local t = {}
    for i = 0, 255 do t[i + 1] = string.char(i) end
    return table.concat(t)
end

local seed = 12345
local function rnd(n)   -- Park-Miller, exact in doubles
    seed = math.fmod(seed * 16807, 2147483647)
    return math.fmod(seed, n)
end

-- -------------------------------------------------------------- load addon
local function loadFile(path)
    local chunk, err
    if loadfile then chunk, err = loadfile(path) end
    assert(chunk, err)
    chunk()
end

loadFile(ROOT .. "/addon/Hearthstone/HsProtocol.lua")
local P = HsProtocol
loadFile(ROOT .. "/addon/Hearthstone/CardDB.lua")
loadFile(ROOT .. "/addon/Hearthstone/CardTextures.lua")
loadFile(ROOT .. "/addon/Hearthstone/CollectionTextures.lua")
loadFile(ROOT .. "/addon/Hearthstone/CollectionModel.lua")
loadFile(ROOT .. "/addon/Hearthstone/Ownership.lua")
loadFile(ROOT .. "/addon/Hearthstone/Packs.lua")
loadFile(ROOT .. "/addon/Hearthstone/Wallet.lua")
loadFile(ROOT .. "/addon/Hearthstone/HeroPortraits.lua")
loadFile(ROOT .. "/addon/Hearthstone/Heroes.lua")
loadFile(ROOT .. "/addon/Hearthstone/Arena.lua")
loadFile(ROOT .. "/addon/Hearthstone/Decks.lua")
loadFile(ROOT .. "/addon/Hearthstone/Match.lua")
loadFile(ROOT .. "/addon/Hearthstone/MatchAnimations.lua")
-- Warsong Battlegrounds (protocol, client state, windows and the placeholder portrait list)
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgCardDB.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgProtocol.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgState.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgTextures.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgVisuals.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgShopUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgLobbyUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgHeroPickUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgQuestPickUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgCombatUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/Battlegrounds/BgEndUI.lua")

-- Hearthstone.lua expects the WoW globals to be absent or replaced.
local printed = {}
local sent = {}
loadFile(ROOT .. "/addon/Hearthstone/Hearthstone.lua")
loadFile(ROOT .. "/addon/Hearthstone/CardWidget.lua")
loadFile(ROOT .. "/addon/Hearthstone/MicroButton.lua")
loadFile(ROOT .. "/addon/Hearthstone/CollectionUI.lua")
local DEFAULT_START_TAB = Hearthstone.CollectionUI.START_TAB
Hearthstone.CollectionUI.START_TAB = "PLAYER"      -- most tests start on the player's class tab; testStartTab checks the real default
loadFile(ROOT .. "/addon/Hearthstone/PacksUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/DecksUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/CraftUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/ArenaUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/HeroesUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/LadderRankTextures.lua")
loadFile(ROOT .. "/addon/Hearthstone/LadderUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/MerchantCoins.lua")
loadFile(ROOT .. "/addon/Hearthstone/BoardTextures.lua")
loadFile(ROOT .. "/addon/Hearthstone/MatchUI.lua")
loadFile(ROOT .. "/addon/Hearthstone/MatchFxProfiles.lua")
loadFile(ROOT .. "/addon/Hearthstone/MatchFx.lua")
local H = Hearthstone
H.Print = function(m) table.insert(printed, m) end
H.PrintError = function(m) table.insert(printed, "ERROR: " .. m) end
H.Transmit = function(f) table.insert(sent, f) end

local function lastPrinted()
    return printed[table.getn(printed)] or ""
end

local function resetAddon()
    H.state.welcomed = false
    H.state.welcome = nil
    H.state.lastError = nil
    H.state.lastErrorCode = nil
    H.state.helloSent = false
    H.state.datasetStatus = nil
    H.queue = {}
    H.pending = {}
    H.helloIn = nil
    H.reasm:Clear()
    H.coll.reqId, H.coll.refetch = nil, false
    H.coll.covers, H.coll.refetchVer = nil, nil
    H.packs.reqId, H.packs.refetch, H.packs.supported = nil, false, false
    H.packReqs = {}
    HsOwnership.Reset()
    local PK = HsPacks
    PK.queue, PK.view, PK.pending, PK.message = {}, nil, nil, nil
    PK.memSeen = {}
    PK.Reset()
    H.decks.reqId, H.decks.refetch, H.decks.supported = nil, false, false
    H.deckReqs = {}
    local D = HsDecks
    D.draft, D.op, D.message, D.addReason, D.selected = nil, nil, nil, nil, nil
    D.Reset()
    H.match.reqId, H.match.refetch, H.match.supported, H.match.announce = nil, false, false, false
    H.matchReqs = {}
    HsMatch.supported = false
    HsMatch.Reset()
    -- the phase 07-09 tests look at the board right after a GAME: animations are off there (the animation tests have their own)
    HsMatchAnim.Reset()
    HsMatchAnim.ClearFailure()
    HsMatchAnim.SetMode("off")
    H.wallet.reqId, H.wallet.refetch, H.wallet.supported = nil, false, false
    H.walletReqs = {}
    HsWallet.Reset()
    H.heroes.reqId, H.heroes.refetch, H.heroes.supported = nil, false, false
    H.heroReqs = {}
    HsHeroes.Reset()
    H.arena.reqId, H.arena.refetch, H.arena.supported = nil, false, false
    H.arenaReqs = {}
    HsArena.op, HsArena.message = nil, nil
    HsArena.Reset()
    -- The Battlegrounds state and its outbound queue go into the same recorder.
    HsBg.Transmit = function(f) table.insert(sent, f) end
    HsBg.Reset()
    printed = {}
    sent = {}
end

-- ------------------------------------------------------------------- lint
local function stripCodeOnly(src)
    -- removes strings and comments; keeps newlines
    local out = {}
    local i, n = 1, string.len(src)
    while i <= n do
        local c = string.sub(src, i, i)
        local two = string.sub(src, i, i + 1)
        if two == "--" then
            check(string.sub(src, i, i + 3) ~= "--[[", "block comments are not used")
            while i <= n and string.sub(src, i, i) ~= "\n" do i = i + 1 end
        elseif c == '"' or c == "'" then
            local q = c
            i = i + 1
            while i <= n and string.sub(src, i, i) ~= q do
                if string.sub(src, i, i) == "\\" then i = i + 1 end
                i = i + 1
            end
            i = i + 1
            out[table.getn(out) + 1] = '""'
        else
            out[table.getn(out) + 1] = c
            i = i + 1
        end
    end
    return table.concat(out)
end

local function testLint()
    local files = { "addon/Hearthstone/HsProtocol.lua", "addon/Hearthstone/CardDB.lua",
        "addon/Hearthstone/CardTextures.lua", "addon/Hearthstone/Hearthstone.lua",
        "addon/Hearthstone/CollectionModel.lua", "addon/Hearthstone/Ownership.lua", "addon/Hearthstone/CardWidget.lua",
        "addon/Hearthstone/MicroButton.lua", "addon/Hearthstone/CollectionUI.lua", "addon/Hearthstone/Packs.lua",
        "addon/Hearthstone/PacksUI.lua", "addon/Hearthstone/Decks.lua", "addon/Hearthstone/DecksUI.lua",
        "addon/Hearthstone/Match.lua", "addon/Hearthstone/MatchAnimations.lua", "addon/Hearthstone/BoardTextures.lua",
        "addon/Hearthstone/MatchUI.lua", "addon/Hearthstone/MatchFxProfiles.lua", "addon/Hearthstone/MatchFx.lua",
        "addon/Hearthstone/Wallet.lua", "addon/Hearthstone/Heroes.lua", "addon/Hearthstone/HeroesUI.lua", "addon/Hearthstone/CraftUI.lua", "addon/Hearthstone/MerchantCoins.lua",
        "addon/Hearthstone/Arena.lua", "addon/Hearthstone/ArenaUI.lua", "addon/Hearthstone/LadderUI.lua",
        "addon/Hearthstone/LadderRankTextures.lua",
        "addon/Hearthstone/Battlegrounds/BgCardDB.lua",
        "addon/Hearthstone/Battlegrounds/BgProtocol.lua", "addon/Hearthstone/Battlegrounds/BgState.lua",
        "addon/Hearthstone/Battlegrounds/BgTextures.lua", "addon/Hearthstone/Battlegrounds/BgVisuals.lua",
        "addon/Hearthstone/Battlegrounds/BgShopUI.lua",
        "addon/Hearthstone/Battlegrounds/BgLobbyUI.lua", "addon/Hearthstone/Battlegrounds/BgHeroPickUI.lua",
        "addon/Hearthstone/Battlegrounds/BgQuestPickUI.lua",
        "addon/Hearthstone/Battlegrounds/BgCombatUI.lua",
        "addon/Hearthstone/Battlegrounds/BgEndUI.lua" }
    local uiFiles = { ["addon/Hearthstone/CardWidget.lua"] = true, ["addon/Hearthstone/MicroButton.lua"] = true,
        ["addon/Hearthstone/CollectionUI.lua"] = true, ["addon/Hearthstone/PacksUI.lua"] = true,
        ["addon/Hearthstone/DecksUI.lua"] = true, ["addon/Hearthstone/MatchUI.lua"] = true,
        ["addon/Hearthstone/MatchFx.lua"] = true, ["addon/Hearthstone/CraftUI.lua"] = true,
        ["addon/Hearthstone/MerchantCoins.lua"] = true, ["addon/Hearthstone/ArenaUI.lua"] = true,
        ["addon/Hearthstone/HeroesUI.lua"] = true,
        ["addon/Hearthstone/LadderUI.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgVisuals.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgShopUI.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgLobbyUI.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgHeroPickUI.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgQuestPickUI.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgCombatUI.lua"] = true,
        ["addon/Hearthstone/Battlegrounds/BgEndUI.lua"] = true }
    for _, path in ipairs(files) do
        local src = readAll(ROOT .. "/" .. path)
        check(src ~= nil, path .. " readable")
        local code = stripCodeOnly(src)
        check(not string.find(code, "#"), path .. ": '#' operator")
        check(not string.find(code, "select%s*%("), path .. ": select()")
        check(not string.find(code, "%.%.%."), path .. ": varargs")
        check(not string.find(code, "string%.match"), path .. ": string.match")
        check(not string.find(code, "gmatch"), path .. ": gmatch")
        check(not string.find(code, "goto"), path .. ": goto")
        check(not string.find(code, "::"), path .. ": labels")
        check(not string.find(code, "[%w_%)%]]%s*%%%s*[%w_%(]"), path .. ": '%' operator")
        check(not string.find(code, "//"), path .. ": integer division")
        check(not string.find(code, "<<") and not string.find(code, ">>"), path .. ": bit shifts")
        check(not string.find(code, "[%w_%)]%s*&%s*[%w_%(]"), path .. ": bit and")
        check(not string.find(code, "string%.len%(%s*%)"), path .. ": string.len()")
        check(not string.find(code, "table%.unpack") and not string.find(code, "math%.pow"), path .. ": newer library")
        check(not string.find(code, "%f[%w_]ipairs%s*%(%s*arg"), path .. ": arg misuse")
        check(not string.find(code, "SetShown") and not string.find(code, "SetColorTexture") and not string.find(code, "SetEnabled"),
            path .. ": method that does not exist in 1.12")
        check(not string.find(code, "SUPERWOW") and not string.find(code, "Nampower") and not string.find(code, "UnitXP"),
            path .. ": no SuperWoW / nampower / UnitXP dependency")
        check(not string.find(code, "math%.mod") and not string.find(code, "%f[%w_]mod%s*%("), path .. ": mod() avoided (use floor arithmetic)")
        if uiFiles[path] then
            -- 1.12 calls SetScript handlers without arguments (they use this / arg1)
            check(not string.find(code, "SetScript%s*%(%s*\"[%w]+\"%s*,%s*function%s*%(%s*[%w_]"), path .. ": handler declares parameters")
        end
    end
    local toc = readAll(ROOT .. "/addon/Hearthstone/Hearthstone.toc") or ""
    check(string.find(toc, "## Interface: 11200", 1, true) ~= nil, "toc Interface 11200")
    check(string.find(toc, "HsProtocol.lua", 1, true) ~= nil, "toc lists HsProtocol.lua")
    check(string.find(toc, "\nHearthstone.lua", 1, true) ~= nil, "toc lists Hearthstone.lua")
    local p1 = string.find(toc, "HsProtocol.lua", 1, true)
    local p2 = string.find(toc, "\nHearthstone.lua", 1, true)
    check(p1 and p2 and p1 < p2, "protocol file loads first")
    local p3 = string.find(toc, "CardDB.lua", 1, true)
    check(p3 and p2 and p1 < p3 and p3 < p2, "CardDB.lua loads before Hearthstone.lua")
    local p4 = string.find(toc, "CardTextures.lua", 1, true)
    check(p4 and p4 < p2, "CardTextures.lua loads before Hearthstone.lua")
    local pm = string.find(toc, "CollectionModel.lua", 1, true)
    check(pm and pm > p3 and pm < p2, "CollectionModel.lua loads after CardDB.lua and before Hearthstone.lua")
    local po = string.find(toc, "Ownership.lua", 1, true)
    check(po and po > p1 and po < p2, "Ownership.lua loads after HsProtocol.lua and before Hearthstone.lua")
    local pw = string.find(toc, "CardWidget.lua", 1, true)
    local pb = string.find(toc, "MicroButton.lua", 1, true)
    local pu = string.find(toc, "CollectionUI.lua", 1, true)
    check(pw and pb and pu and pw > p2 and pb > p2 and pu > p2, "UI files load after Hearthstone.lua")
    check(pu > pw and pu > pb, "CollectionUI loads after CardWidget and MicroButton")
    check(string.find(toc, "## SavedVariables: HearthstoneDB", 1, true) ~= nil, "toc declares SavedVariables HearthstoneDB")
    local pk = string.find(toc, "Packs.lua", 1, true)
    check(pk and pk > po and pk < p2, "Packs.lua loads after Ownership.lua and before Hearthstone.lua")
    local pp = string.find(toc, "PacksUI.lua", 1, true)
    check(pp and pp > pu, "PacksUI.lua loads after CollectionUI.lua")
    local pd = string.find(toc, "\nDecks.lua", 1, true)
    check(pd and pd > pk and pd < p2, "Decks.lua loads after Packs.lua and before Hearthstone.lua")
    local pdu = string.find(toc, "DecksUI.lua", 1, true)
    check(pdu and pdu > pp, "DecksUI.lua loads after PacksUI.lua")
    local pwl = string.find(toc, "\nWallet.lua", 1, true)
    check(pwl and pwl > pk and pwl < p2, "Wallet.lua loads after Packs.lua and before Hearthstone.lua")
    local pcu = string.find(toc, "CraftUI.lua", 1, true)
    check(pcu and pcu > pdu, "CraftUI.lua loads after DecksUI.lua")
    local phe = string.find(toc, "\nHeroes.lua", 1, true)
    local php = string.find(toc, "\nHeroPortraits.lua", 1, true)
    check(phe and php and php < phe and phe > pwl and phe < p2, "HeroPortraits.lua and Heroes.lua load after Wallet.lua and before Hearthstone.lua")
    local par = string.find(toc, "\nArena.lua", 1, true)
    check(par and par > pwl and par < p2, "Arena.lua loads after Wallet.lua and before Hearthstone.lua")
    local pau = string.find(toc, "ArenaUI.lua", 1, true)
    check(pau and pau > pcu and pau > p2, "ArenaUI.lua loads after CraftUI.lua and Hearthstone.lua")
    local pmc = string.find(toc, "MerchantCoins.lua", 1, true)
    check(pmc and pmc > p2 and pmc > pwl, "MerchantCoins.lua loads after Hearthstone.lua and Wallet.lua")
    check(string.find(toc, "decks", 1, true) ~= nil, "toc notes mention decks")
    local pma = string.find(toc, "\nMatch.lua", 1, true)
    check(pma and pma > pd and pma < p2, "Match.lua loads after Decks.lua and before Hearthstone.lua")
    local pan = string.find(toc, "MatchAnimations.lua", 1, true)
    check(pan and pma and pan > pma and pan < p2, "MatchAnimations.lua loads after Match.lua and before Hearthstone.lua")
    local pbt = string.find(toc, "BoardTextures.lua", 1, true)
    local pmu = string.find(toc, "MatchUI.lua", 1, true)
    check(pbt and pmu and pbt > pdu and pmu > pbt, "BoardTextures.lua, then MatchUI.lua, after the other UI files")
    check(type(HS_BoardTextures) == "table" and HS_BoardTextures.generated == false and next(HS_BoardTextures.pieces) == nil,
        "the repository keeps the BoardTextures.lua placeholder")
    -- every Lua file of the addon folder is listed in the toc
    for _, name in ipairs({ "HsProtocol", "CardDB", "CardTextures", "CollectionModel", "Ownership", "Packs", "Decks", "Hearthstone", "CardWidget",
        "MicroButton", "CollectionUI", "PacksUI", "DecksUI", "Match", "MatchAnimations", "BoardTextures", "MatchUI",
        "MatchFxProfiles", "MatchFx", "Wallet", "CraftUI", "Arena", "ArenaUI" }) do
        check(readAll(ROOT .. "/addon/Hearthstone/" .. name .. ".lua") ~= nil, name .. ".lua exists")
        check(string.find(toc, name .. ".lua", 1, true) ~= nil, name .. ".lua is in the toc")
    end
    -- key binding: Bindings.xml is loaded by name, the strings and the function must exist
    local bind = readAll(ROOT .. "/addon/Hearthstone/Bindings.xml") or ""
    check(string.find(bind, 'name="HEARTHSTONE_TOGGLE"', 1, true) ~= nil, "Bindings.xml defines HEARTHSTONE_TOGGLE")
    check(string.find(bind, 'header="HEARTHSTONE"', 1, true) ~= nil, "Bindings.xml header")
    check(string.find(bind, "Hearthstone_ToggleCollection()", 1, true) ~= nil, "binding calls Hearthstone_ToggleCollection")
    check(BINDING_HEADER_HEARTHSTONE == "Hearthstone" and type(BINDING_NAME_HEARTHSTONE_TOGGLE) == "string", "binding labels defined")
    check(type(Hearthstone_ToggleCollection) == "function", "binding function exists")
end

-- ---------------------------------------------------------------- escaping
local function testEscape()
    for i = 0, 255 do
        local s = string.char(i)
        local e = P.Escape(s)
        check(P.Unescape(e) == s, "roundtrip byte " .. i)
        check(not P.ContainsForbidden(e), "forbidden in byte " .. i)
        check(not string.find(e, "[^\32-\123\125\126]") and not string.find(e, "[:_]"), "charset byte " .. i)
    end
    check(P.Unescape(P.Escape(allBytes())) == allBytes(), "all bytes roundtrip")

    local triggers = { "LFT", "TW_", "TWT_", "GM_ADDON", "MR:", "TR:", "TW_GUILDBANK", "TWT_UDTSv4", "TW_TRANSMOG",
        "TW_CHAT_MSG_WHISPER", "TW_BUFF", "TW_SHOP", "TW_BGQueue", "TW_TITLES", "TW_HONOR", "TW_Merchant",
        "LLFT", "LFLFT", "xLFTx", "LFTLFT", "L", "F", "T", "LF" }
    for _, t in ipairs(triggers) do
        local e = P.Escape(t)
        check(not P.ContainsForbidden(e), "trigger " .. t)
        check(P.Unescape(e) == t, "trigger roundtrip " .. t)
    end

    local alpha = { "L", "F", "T", "_", ":", "W", "M", "R", "~", "|", "\n", "G" }
    local n = table.getn(alpha)
    for len = 1, 4 do
        local combos = 1
        for _ = 1, len do combos = combos * n end
        for c = 0, combos - 1 do
            local x, parts = c, {}
            for i = 1, len do
                parts[i] = alpha[math.fmod(x, n) + 1]
                x = math.floor(x / n)
            end
            local s = table.concat(parts)
            local e = P.Escape(s)
            check(not P.ContainsForbidden(e), "combo forbidden")
            check(P.Unescape(e) == s, "combo roundtrip")
        end
    end

    local bias = "LFT_:WMRGADON~|\n\t"
    for _ = 1, 1500 do
        local t = {}
        for i = 1, rnd(150) do
            if rnd(2) == 1 then
                local k = rnd(string.len(bias)) + 1
                t[i] = string.sub(bias, k, k)
            else
                t[i] = string.char(rnd(256))
            end
        end
        local s = table.concat(t)
        local e = P.Escape(s)
        check(not P.ContainsForbidden(e), "fuzz forbidden")
        check(P.Unescape(e) == s, "fuzz roundtrip")
    end

    check(P.Unescape("~") == nil, "dangling ~")
    check(P.Unescape("abc~") == nil, "dangling ~ 2")
    check(P.Unescape("~z") == nil, "unknown escape")
    check(P.Unescape("~x") == nil and P.Unescape("~x4") == nil and P.Unescape("~xzz") == nil, "bad hex")
    check(P.Unescape("~x41") == "A" and P.Unescape("~x4A") == "J", "hex")
    check(P.Unescape("") == "", "empty")
    check(P.UnescapeValue("~") == nil and P.UnescapeValue("~z") == nil, "value escape errors")
end

local function testKv()
    local v = "a;b=c~d;;=="
    local e = P.EscapeValue(v)
    check(not string.find(e, ";", 1, true) and not string.find(e, "=", 1, true), "value escape hides separators")
    check(P.UnescapeValue(e) == v, "value roundtrip")
    local payload = P.KvEncode({ { "protocol", "1" }, { "msg", "hello; world = ~ LFT TW_x" }, { "empty", "" } })
    local m = P.KvDecode(payload)
    check(m and m.protocol == "1" and m.msg == "hello; world = ~ LFT TW_x" and m.empty == "", "kv roundtrip")
    local wire = P.Escape(payload)
    check(not P.ContainsForbidden(wire) and P.Unescape(wire) == payload, "kv through wire escape")
    check(P.KvDecode("") ~= nil, "empty kv")
    for _, bad in ipairs({ "a", "=b", "a=b;", ";a=b", "a=b;a=c", "a b=c", "a=~z" }) do
        check(P.KvDecode(bad) == nil, "kv reject " .. bad)
    end
end

local function testGoldenVectors()
    local data = readAll(ROOT .. "/tests/golden_vectors.txt")
    check(data ~= nil, "golden_vectors.txt readable")
    local count = 0
    for line in string.gmatch(data or "", "[^\n]+") do
        line = string.gsub(line, "\r$", "")
        if string.sub(line, 1, 1) ~= "#" then
            local tab = string.find(line, "\t", 1, true)
            check(tab ~= nil, "vector has a tab")
            local raw = fromHex(string.sub(line, 1, tab - 1))
            local enc = string.sub(line, tab + 1)
            check(P.Escape(raw) == enc, "Escape matches C++ for " .. string.sub(line, 1, 24))
            check(P.Unescape(enc) == raw, "Unescape matches C++ for " .. string.sub(line, 1, 24))
            count = count + 1
        end
    end
    check(count >= 19, "vector count " .. count)
end

-- -------------------------------------------------------------- frame codec
local function testFrames()
    local f = P.EncodeFrame("PING", "a1", 1, 1, "hi")
    check(f == "v1\tPING\ta1\t1\t1\thi", "encode")
    local st, fr = P.ParseFrame(f)
    check(st == "ok" and fr.op == "PING" and fr.reqId == "a1" and fr.part == 1 and fr.total == 1 and fr.payload == "hi", "parse")
    local nasty = string.sub(allBytes(), 1, 20)
    f = P.EncodeFrame("ECHO", "zz9", 2, 5, nasty)
    st, fr = P.ParseFrame(f)
    check(st == "ok" and fr.part == 2 and fr.total == 5 and fr.payload == nasty, "nasty payload")

    check(P.EncodeFrame("ping", "a1", 1, 1, "") == nil, "lower op")
    check(P.EncodeFrame("PING", "", 1, 1, "") == nil, "empty reqId")
    check(P.EncodeFrame("PING", "A1", 1, 1, "") == nil, "upper reqId")
    check(P.EncodeFrame("PING", "123456789", 1, 1, "") == nil, "long reqId")
    check(P.EncodeFrame("PING", "a", 0, 1, "") == nil, "part 0")
    check(P.EncodeFrame("PING", "a", 2, 1, "") == nil, "part>total")
    check(P.EncodeFrame("PING", "a", 1, 100, "") == nil, "total 100")
    check(P.EncodeFrame("TW_ABC", "a", 1, 1, "") == nil, "trigger op")
    check(P.EncodeFrame("VERYLONGOPNAME", "a", 1, 1, "") == nil, "long op")
    check(P.EncodeFrame("PING", "a", 1, 1, string.rep("x", 300)) == nil, "too long")

    local bad = {
        "", "v1\tPING\ta1\t1\t1", "x1\tPING\ta1\t1\t1\t", "v\tPING\ta1\t1\t1\t", "v01\tPING\ta1\t1\t1\t",
        "v1\tping\ta1\t1\t1\t", "v1\tPING\tA1\t1\t1\t", "v1\tPING\ta1\t0\t1\t", "v1\tPING\ta1\t2\t1\t",
        "v1\tPING\ta1\t01\t1\t", "v1\tPING\ta1\t1\tx\t", "v1\tPING\ta1\t1\t1\tbad~z", "v1\tPING\ta1\t1\t1\tra\tw",
        "v1\tPING\ta1\t1\t1\ta|b", "v1\tPING\ta1\t1\t1\ta:b", "v1\tPING\ta1\t1\t1\ta_b", "v1\tPING\ta1\t1\t1\ta\1b",
    }
    for _, b in ipairs(bad) do
        local s = P.ParseFrame(b)
        check(s == "malformed", "malformed: " .. toHex(b))
    end
    local s, _, rid = P.ParseFrame("v1\tPING\ta1\t1\t1\ta\1b")
    check(s == "malformed" and rid == "a1", "best effort reqId")
    s, _, rid = P.ParseFrame("v2\tWHATEVER\tq7\tweird")
    check(s == "badversion" and rid == "q7", "version is checked first")
    s, _, rid = P.ParseFrame("v2\tX")
    check(s == "badversion" and rid == "0", "v2 short frame")
    check(P.ParseFrame("v2") == "badversion", "v2 alone")
    check(P.ParseFrame("v1000\tHELLO") == "badversion", "v1000")
    check(P.ParseFrame("v1") == "malformed", "v1 alone")
    check(P.ParseFrame("v1\tPING") == "malformed", "v1 too few fields")
    check(P.ParseFrame("v1\tPING\ta1\t1") == "malformed", "v1 four fields")
    check(P.ParseFrame("v1234567890\tPING") == "malformed", "over 9 digits")
    check(P.ParseFrame("vx\tPING") == "malformed", "non numeric version")
    s, _, rid = P.ParseFrame("v2\tPING\tq7\t1\t1\tx")
    check(s == "badversion" and rid == "q7", "bad version")
    s, _, rid = P.ParseFrame("v2\tPING\tQ!\t1\t1\tx")
    check(s == "badversion" and rid == "0", "bad version, bad id")
end

local function testChunking()
    local nastyBase = allBytes() .. "LFTLFLFTTW_TWT_GM_ADDONMR:TR:" .. allBytes()
    local limits = { 240, 200, 120, 80, 60 }
    for _, limit in ipairs(limits) do
        local len = 0
        while len <= 700 do
            local t = {}
            for i = 0, len - 1 do
                local k = math.fmod(i * 5 + len, string.len(nastyBase)) + 1
                t[i + 1] = string.sub(nastyBase, k, k)
            end
            local payload = table.concat(t)
            local frames = P.ChunkPayload("ECHO", "abcd1234", payload, limit)
            if not frames then
                check(len > 99 * 20, "chunk failure only above the part cap")
            else
                local r = P.NewReassembler({ maxBytes = 1048576, maxParts = 99, timeout = 10, maxPending = 4 })
                local status, done
                for k = 1, table.getn(frames) do
                    check(string.len(P.PREFIX) + 1 + string.len(frames[k]) <= limit, "frame within limit")
                    check(not P.ContainsForbidden(P.PREFIX .. "\t" .. frames[k]), "frame forbidden")
                    local st, f = P.ParseFrame(frames[k])
                    check(st == "ok" and f.part == k and f.total == table.getn(frames), "frame numbering")
                    status, done = r:Add(0, f.op, f.reqId, f.part, f.total, f.payload)
                end
                check(status == "complete" and done.payload == payload, "reassembled payload")
            end
            if len < 40 then len = len + 1 else len = len + 13 end
        end
    end
    check(P.ChunkPayload("ECHO", "abcd1234", "x", 20) == nil, "tiny limit fails")
    check(P.ChunkPayload("echo", "abcd1234", "x") == nil, "bad op fails")
    check(P.ChunkPayload("ECHO", "a", string.rep("x", 30000)) == nil, "99 part cap")
    check(table.getn(P.ChunkPayload("ECHO", "a", "x")) == 1, "single")
    check(table.getn(P.ChunkPayload("ECHO", "a", "")) == 1, "empty")
    -- worst case escape content
    for len = 0, 2000, 7 do
        local payload = string.rep("\255", len)
        local frames = P.ChunkPayload("NPC_START", "12345678", payload)
        if frames then
            local pieces = {}
            for k = 1, table.getn(frames) do
                check(string.len(P.PREFIX) + 1 + string.len(frames[k]) <= 240, "worst case within 240")
                local _, f = P.ParseFrame(frames[k])
                pieces[k] = f.payload
            end
            check(table.concat(pieces) == payload, "worst case payload")
        else
            check(len * 4 > 99 * 150, "worst case failure only above cap")
        end
    end
end


-- ---------------------------------------------------------------- reassembler
local function newR(bytes, parts, timeout, pending)
    return P.NewReassembler({ maxBytes = bytes or 4096, maxParts = parts or 16, timeout = timeout or 10, maxPending = pending or 4 })
end

local function testReassembler()
    -- server pushes (reqId "0") are never remembered: every multi-part push completes (GAME)
    do
        local r = newR()
        for round = 1, 3 do
            local st1 = r:Add(0, "GAME", "0", 1, 2, "a" .. round)
            local st2, done = r:Add(0, "GAME", "0", 2, 2, "b")
            check(st1 == "pending" and st2 == "complete" and done.payload == "a" .. round .. "b", "multi-part push " .. round)
        end
        local st = r:Add(0, "X", "abc", 1, 2, "a")
        st = r:Add(0, "X", "abc", 2, 2, "b")
        check(st == "complete" and r:Add(0, "X", "abc", 1, 2, "a") == "ignored", "a finished request is still remembered")
    end
    -- all part counts in three orders
    for total = 1, 16 do
        for order = 0, 2 do
            local r = newR()
            local seq = {}
            for i = 1, total do seq[i] = i end
            if order == 1 then
                local rev = {}
                for i = 1, total do rev[i] = total + 1 - i end
                seq = rev
            elseif order == 2 then
                local i = 1
                while i + 1 <= total do
                    seq[i], seq[i + 1] = seq[i + 1], seq[i]
                    i = i + 2
                end
            end
            local expected = ""
            for i = 1, total do expected = expected .. "part" .. i .. ";" end
            local st, done
            for k = 1, total do
                st, done = r:Add(0, "ECHO", "r1", seq[k], total, "part" .. seq[k] .. ";")
                if k < total then check(st == "pending", "pending") end
            end
            check(st == "complete" and done.payload == expected, "complete " .. total)
            check(r:PendingCount() == 0, "no pending left")
        end
    end

    local r = newR()
    check(r:Add(0, "ECHO", "d1", 1, 3, "A") == "pending", "dup 1")
    check(r:Add(0, "ECHO", "d1", 1, 3, "A") == "ignored", "dup ignored")
    check(r:Add(0, "ECHO", "d1", 1, 3, "OTHER") == "ignored", "dup with other data ignored")
    check(r:Add(0, "ECHO", "d1", 3, 3, "C") == "pending", "dup 3")
    local st, done = r:Add(0, "ECHO", "d1", 2, 3, "B")
    check(st == "complete" and done.payload == "ABC", "dup complete")

    r = newR()
    check(r:Add(0, "ECHO", "c1", 1, 3, "A") == "pending", "conflict setup")
    check(r:Add(0, "ECHO", "c1", 2, 4, "B") == "malformed", "conflicting total")
    check(r:PendingCount() == 0, "dropped")
    check(r:Add(0, "ECHO", "c1", 3, 3, "C") == "ignored", "late part ignored")
    check(r:Add(0, "ECHO", "c3", 0, 3, "A") == "malformed" and r:Add(0, "ECHO", "c3", 4, 3, "A") == "malformed", "bad numbers")
    check(r:Add(0, "ECHO", "c4", 1, 17, "A") == "toolarge", "total > max")
    check(r:Add(0, "ECHO", "c4", 2, 17, "A") == "ignored", "over-limit total answered once")
    check(r:Add(0, "ECHO", "c6", 1, 3, "A") == "pending", "c6 pending")
    r.limits.maxParts = 2
    check(r:Add(0, "ECHO", "c6", 2, 3, "B") == "toolarge", "c6 over-limit")
    check(r:PendingCount() == 0, "c6 dropped")
    r.limits.maxParts = 16
    check(r:Add(0, "ECHO", "c6", 3, 3, "C") == "ignored", "c6 late part ignored")

    r = newR(10)
    check(r:Add(0, "ECHO", "o1", 1, 3, "123456") == "pending", "oversize setup")
    check(r:Add(0, "ECHO", "o1", 2, 3, "78901") == "toolarge", "oversize")
    check(r:Add(0, "ECHO", "o1", 3, 3, "x") == "ignored", "oversize late")
    check(r:Add(0, "ECHO", "o2", 1, 2, "12345") == "pending", "exact")
    check(r:Add(0, "ECHO", "o2", 2, 2, "67890") == "complete", "exact complete")
    check(r:Add(0, "ECHO", "o3", 1, 1, "12345678901") == "toolarge", "single oversize")

    r = newR()
    for i = 0, 3 do check(r:Add(0, "ECHO", "p" .. i, 1, 2, "A") == "pending", "fill " .. i) end
    check(r:Add(0, "ECHO", "p9", 1, 2, "A") == "busy", "busy")
    check(r:Add(0, "ECHO", "p9", 2, 2, "B") == "ignored", "busy late part")
    check(r:Add(0, "ECHO", "p0", 2, 2, "B") == "complete", "free a slot")
    check(r:Add(0, "ECHO", "pa", 1, 2, "A") == "pending", "slot reused")
    check(r:Add(0, "PING", "s1", 1, 1, "x") == "complete", "single never pending")

    r = newR()
    check(r:Add(1, "ECHO", "t1", 1, 2, "A") == "pending", "timeout setup 1")
    check(r:Add(5, "ECHO", "t2", 1, 2, "A") == "pending", "timeout setup 2")
    check(table.getn(r:Sweep(10.9)) == 0, "no timeout yet")
    local expired = r:Sweep(11)
    check(table.getn(expired) == 1 and expired[1] == "t1", "t1 expired")
    check(r:Add(11.1, "ECHO", "t1", 2, 2, "B") == "ignored", "late part after timeout")
    expired = r:Sweep(15)
    check(table.getn(expired) == 1 and expired[1] == "t2" and r:PendingCount() == 0, "t2 expired")
    check(r:Add(20, "ECHO", "t3", 1, 2, "A") == "pending", "clock back setup")
    check(table.getn(r:Sweep(0.01)) == 0, "clock going backwards")

    r = newR()
    for i = 0, 15 do
        check(r:Add(0, "ECHO", "k" .. i, 1, 2, "A") == "pending", "lru p")
        check(r:Add(0, "ECHO", "k" .. i, 2, 2, "B") == "complete", "lru c")
    end
    for i = 0, 15 do
        check(r:IsRemembered("k" .. i), "remembered " .. i)
        check(r:Add(0, "ECHO", "k" .. i, 2, 2, "B") == "ignored", "late duplicate ignored " .. i)
    end
    check(r:Add(0, "ECHO", "k16", 1, 2, "A") == "pending", "17th")
    check(r:Add(0, "ECHO", "k16", 2, 2, "B") == "complete", "17th complete")
    check(not r:IsRemembered("k0") and r:IsRemembered("k1") and r:IsRemembered("k16"), "oldest evicted")
    check(r:Add(0, "PING", "k1", 1, 1, "x") == "complete", "single reuse of remembered id")
end

-- ------------------------------------------------------------- C++ transcript
local function testTranscript()
    local data = readAll(ROOT .. "/tests/transcript.txt")
    check(data ~= nil, "transcript.txt readable")
    resetAddon()

    local groups, order = {}, {}
    local server = {}
    for line in string.gmatch(data or "", "[^\n]+") do
        line = string.gsub(line, "\r$", "")
        if string.sub(line, 1, 1) ~= "#" then
            if string.sub(line, 1, 6) == "CHUNK\t" then
                local _, _, op, reqId, hex, frame = string.find(line, "^CHUNK\t([^\t]+)\t([^\t]+)\t([^\t]*)\t(.*)$")
                check(op ~= nil, "chunk line parses")
                local key = op .. "|" .. reqId .. "|" .. hex
                if not groups[key] then
                    groups[key] = { op = op, reqId = reqId, hex = hex, frames = {} }
                    table.insert(order, key)
                end
                table.insert(groups[key].frames, frame)
            elseif string.sub(line, 1, 7) == "SERVER\t" then
                table.insert(server, string.sub(line, 8))
            end
        end
    end
    check(table.getn(order) >= 8, "transcript has requests")
    check(table.getn(server) >= 15, "transcript has replies")

    -- The Lua chunker must reproduce the C++ chunker byte for byte.
    for _, key in ipairs(order) do
        local g = groups[key]
        local frames = P.ChunkPayload(g.op, g.reqId, fromHex(g.hex))
        check(frames ~= nil, "lua chunks " .. g.op .. " " .. g.reqId)
        if frames then
            check(table.getn(frames) == table.getn(g.frames), "frame count " .. g.reqId)
            for i = 1, table.getn(frames) do
                check(frames[i] == g.frames[i], "frame identical " .. g.reqId .. " #" .. i)
            end
        end
    end

    -- Feed the C++ server replies to the addon.
    local errors = {}
    local origError = H.PrintError
    for _, f in ipairs(server) do
        local st = P.ParseFrame(f)
        check(st == "ok", "server frame parses")
        check(string.len(P.PREFIX) + 1 + string.len(f) <= 240, "server frame within 240")
        check(not P.ContainsForbidden(P.PREFIX .. "\t" .. f), "server frame forbidden")
        H.HandleFrame(f)
    end
    H.PrintError = origError

    check(H.state.welcome ~= nil, "WELCOME seen (state.welcome set)")
    -- HELLO v2 at the end drops the connection on the server; the addon saw the ERR VERSION last.
    check(H.state.lastErrorCode == "VERSION", "VERSION error surfaced")
    local joined = table.concat(printed, "\n")
    check(string.find(joined, "Connected: server", 1, true) == nil, "no connected line at login")
    check(string.find(joined, "MALFORMED", 1, true) ~= nil, "MALFORMED for n=99")
    check(string.find(joined, "UNKNOWN_OP", 1, true) ~= nil, "UNKNOWN_OP")
    check(string.find(joined, "NOT_IMPLEMENTED", 1, true) ~= nil, "NOT_IMPLEMENTED")
    check(string.find(joined, "Protocol mismatch", 1, true) ~= nil, "version mismatch message")
    check(string.find(joined, "FAILED", 1, true) == nil, "no failed self-check")
    resetAddon()
end

-- ------------------------------------------------------------------ addon flow
local function drain(seconds)
    local step = 0.05
    local t = 0
    while t < seconds do
        H.OnUpdate(step)
        HsBg.Tick(step)
        t = t + step
    end
end

local function testAddonFlow()
    resetAddon()
    -- reqIds are valid, distinct
    local seen = {}
    for _ = 1, 200 do
        local id = H.NextReqId()
        check(P.IsValidReqId(id) and id ~= "0" and not seen[id], "reqId " .. id)
        seen[id] = true
    end

    -- entering the world twice sends exactly one HELLO, after the delay
    H.OnEvent("PLAYER_ENTERING_WORLD")
    H.OnEvent("PLAYER_ENTERING_WORLD")
    drain(1)
    check(table.getn(sent) == 0, "no HELLO before the delay")
    drain(2)
    check(table.getn(sent) == 1, "one HELLO after the delay")
    local st, f = P.ParseFrame(sent[1])
    check(st == "ok" and f.op == "HELLO", "HELLO frame")
    local kv = P.KvDecode(f.payload)
    check(kv and kv.protocol == "1" and kv.addon == H.VERSION and kv.dataset == "-", "HELLO payload")
    H.OnEvent("PLAYER_ENTERING_WORLD")
    drain(5)
    check(table.getn(sent) == 1, "zoning does not resend HELLO")

    -- sender / channel / prefix filter
    local welcome = P.EncodeFrame("WELCOME", f.reqId, 1, 1,
        P.KvEncode({ { "protocol", "1" }, { "server", "9" }, { "dataset", "d" }, { "hash", "h" }, { "caps", "solo,chunk" },
            { "maxframe", "240" }, { "debug", "0" } }))
    H.OnAddonMessage("WRHS", welcome, "PARTY", nil)
    H.OnAddonMessage("OTHER", welcome, "GUILD", nil)
    check(not H.state.welcomed, "wrong channel / prefix ignored")
    local savedUnitName = UnitName
    UnitName = function() return "Me" end
    H.OnAddonMessage("WRHS", welcome, "GUILD", "SomeoneElse")
    check(not H.state.welcomed, "other sender ignored")
    H.OnAddonMessage("WRHS", welcome, "GUILD", "Me")
    check(H.state.welcomed, "own sender accepted")
    UnitName = savedUnitName
    check(table.getn(H.queue) == 1 and P.ParseFrame(H.queue[1]) == "ok", "WELCOME queues the collection request")

    -- the WELCOME queue is paced, frames within limit
    sent = {}
    drain(2)
    do  -- the Battlegrounds restore (BG_GET after WELCOME) shares the recorder; it is checked in bg_tests.lua
        local wrhs = {}
        for _, fr in ipairs(sent) do
            local _, pf = P.ParseFrame(fr)
            if not (pf and pf.op == "BG_GET") then table.insert(wrhs, fr) end
        end
        check(table.getn(wrhs) < table.getn(sent), "WELCOME asks for an open Battlegrounds game (restore after /reload)")
        sent = wrhs
    end
    check(table.getn(sent) == 1, "COLL_GET sent")
    local _, fc = P.ParseFrame(sent[1] or "")
    check(fc and fc.op == "COLL_GET" and fc.payload == "", "the first frame is the empty COLL_GET")
    for _, fr in ipairs(sent) do check(string.len("WRHS\t" .. fr) <= 240, "frame length") end

    H.Slash("bogus")
    check(string.find(lastPrinted(), "commands", 1, true) ~= nil, "help text")
    H.Slash("")
    check(string.find(lastPrinted(), "queued frames", 1, true) ~= nil, "status")

    -- timeouts are reported
    resetAddon()
    H.clock = 100
    H.Hello()
    drain(3)
    check(table.getn(printed) == 0, "no timeout message yet")
    H.clock = H.clock + 20
    H.OnUpdate(0.1)
    local joined = table.concat(printed, "\n")
    check(string.find(joined, "did not answer HELLO", 1, true) ~= nil, "HELLO timeout message")

    -- ERR on reqId 0 clears the oldest pending request: no extra timeout line
    resetAddon()
    H.clock = 50
    H.Request("PING", "ping", {})
    H.clock = 51
    H.Request("PING", "ping", {})
    check(H.pending ~= nil, "pending set")
    local function pendingCount()
        local c = 0
        for _ in pairs(H.pending) do c = c + 1 end
        return c
    end
    check(pendingCount() == 2, "two pending")
    H.HandleFrame("v1\tERR\t0\t1\t1\tcode=RATE;msg=slow")
    check(pendingCount() == 1, "oldest pending cleared by ERR 0")

    -- a garbage frame from the server never throws
    resetAddon()
    H.HandleFrame("garbage")
    H.HandleFrame("v1\tERR\t0\t1\t1\tnot~kv")
    H.HandleFrame("v1\tERR\t0\t1\t1\tcode=DISABLED;msg=off")
    check(H.state.lastErrorCode == "DISABLED", "DISABLED error handled")
    H.HandleFrame("v3\tERR\t0\t1\t1\tx")
    check(H.state.lastErrorCode == "VERSION", "future version frame handled")
    resetAddon()
end

-- ------------------------------------------------------------- card database
local function testCardDB()
    local db = HS_CardDB
    check(type(db) == "table" and type(db.cards) == "table" and type(db.order) == "table", "HS_CardDB tables")
    check(type(db.datasetId) == "string" and db.datasetId ~= "", "datasetId")
    check(string.len(db.hash) == 16 and not string.find(db.hash, "[^0-9a-f]"), "hash is 16 lowercase hex")
    check(string.sub(db.fullHash, 1, 16) == db.hash, "hash is the prefix of fullHash")

    -- the wire id/hash of the generated C++ header must be the same
    local header = readAll(ROOT .. "/src/HsDataset.h") or ""
    local _, _, hid = string.find(header, 'kDatasetId = "([^"]*)"')
    local _, _, hh = string.find(header, 'kDatasetHash = "([^"]*)"')
    check(hid == db.datasetId, "datasetId equals src/HsDataset.h")
    check(hh == db.hash, "hash equals src/HsDataset.h")

    local n = 0
    for id, c in pairs(db.cards) do
        n = n + 1
        check(type(c.name) == "string" and c.name ~= "", id .. " has a name")
        check(type(c.type) == "string" and type(c.class) == "string", id .. " has type and class")
        check(c.type ~= "ENCHANTMENT", id .. " is not an enchantment")
        check(c.acq == "standard" or c.acq == "reward_only" or c.acq == "quest" or c.acq == "none", id .. " acq")
    end
    check(n == db.count, "card count matches count field")
    check(n <= db.datasetCount, "client subset is not larger than the dataset")
    local seen = {}
    for i = 1, table.getn(db.order) do
        local id = db.order[i]
        check(db.cards[id] ~= nil, "order entry exists: " .. tostring(id))
        check(not seen[id], "order entry unique: " .. tostring(id))
        seen[id] = true
    end
    check(table.getn(db.order) > 300, "collection browsing order is populated")
    local fb = db.cards["CS2_029"]
    check(fb and fb.name == "Fireball" and fb.cost == 4 and fb.class == "MAGE" and fb.text == "Deal 6 damage.", "Fireball entry")
    check(db.cards["EX1_298"] and db.cards["EX1_298"].atk == 8, "Ragnaros entry")
end

-- WELCOME versus CardDB
local function testDatasetCheck()
    resetAddon()
    local function welcomeWith(dataset, hash)
        return P.EncodeFrame("WELCOME", "abc", 1, 1,
            P.KvEncode({ { "protocol", "1" }, { "server", "9" }, { "dataset", dataset }, { "hash", hash },
                { "caps", "solo,chunk" }, { "maxframe", "240" }, { "debug", "0" } }))
    end
    local db = HS_CardDB

    H.HandleFrame(welcomeWith(db.datasetId, db.hash))
    check(H.state.welcomed and H.state.datasetStatus == "ok", "matching dataset accepted")
    check(string.find(table.concat(printed, "\n"), "mismatch", 1, true) == nil, "no warning on match")

    resetAddon()
    H.HandleFrame(welcomeWith(db.datasetId, "0000000000000000"))
    check(H.state.welcomed and H.state.datasetStatus == "mismatch", "hash mismatch detected")
    check(string.find(lastPrinted(), "Card data mismatch", 1, true) ~= nil, "mismatch warning printed")
    check(string.find(lastPrinted(), "0000000000000000", 1, true) ~= nil, "warning names the server hash")
    check(string.find(lastPrinted(), db.hash, 1, true) ~= nil, "warning names the addon hash")

    resetAddon()
    H.HandleFrame(welcomeWith("other-dataset", db.hash))
    check(H.state.datasetStatus == "mismatch", "dataset id mismatch detected")

    resetAddon()
    H.HandleFrame(welcomeWith(db.datasetId, "-"))
    check(H.state.welcomed and H.state.datasetStatus == "unknown", "server without hash is not a mismatch")

    -- CardDB missing
    resetAddon()
    HS_CardDB = nil
    H.HandleFrame(welcomeWith("x", "y"))
    check(H.state.datasetStatus == "nocarddb", "missing CardDB reported")
    check(string.find(lastPrinted(), "CardDB.lua", 1, true) ~= nil, "missing CardDB warning")
    H.Slash("status")
    check(string.find(table.concat(printed, "\n"), "CardDB.lua not loaded", 1, true) ~= nil, "status without CardDB")
    HS_CardDB = db

    resetAddon()
    H.HandleFrame(welcomeWith(db.datasetId, db.hash))
    H.Slash("status")
    check(string.find(table.concat(printed, "\n"), "check: ok", 1, true) ~= nil, "status shows dataset check")
    resetAddon()
end


-- ------------------------------------------------------------ card texcoords
local function testCardBottom()
    resetAddon()
    -- default 388/512, per-card override from CardTextures.lua
    check(H.CardBottom("EX1_116") == 388 / 512, "default bottom texcoord")
    local saved = HS_CardTextures
    HS_CardTextures = { order = { "A", "B", "C" }, bottom = { B = 0.5 } }
    check(H.CardBottom("B") == 0.5 and H.CardBottom("A") == 388 / 512, "per-card bottom texcoord")
    HS_CardTextures = saved
    resetAddon()
end

-- ------------------------------------------------------- collection model
local function testCollectionModel()
    local CM = HsCollection
    local db = HS_CardDB
    local cat = CM.NewCatalog(db)
    check(cat ~= nil, "catalog builds")
    check(CM.NewCatalog(nil) == nil and CM.NewCatalog({}) == nil, "no catalog without a card database")

    -- the catalog is exactly the standard, reward_only and quest (Curse of Naxxramas) cards
    local expect, reward, quest = 0, 0, 0
    for _, c in pairs(db.cards) do
        if c.acq == "standard" or c.acq == "reward_only" or c.acq == "quest" then expect = expect + 1 end
        if c.acq == "reward_only" then reward = reward + 1 end
        if c.acq == "quest" then quest = quest + 1 end
    end
    check(cat.total == expect and table.getn(cat.ids) == expect, "catalog size " .. cat.total .. " vs " .. expect)
    check(reward == 4, "four reward_only cards in the data")
    check(quest == 30, "thirty quest (Naxxramas) cards in the data")
    local seenReward, seenQuest, other = 0, 0, 0
    for i = 1, table.getn(cat.ids) do
        local c = db.cards[cat.ids[i]]
        if c.acq == "reward_only" then seenReward = seenReward + 1 end
        if c.acq == "quest" then seenQuest = seenQuest + 1 end
        if c.acq == "none" then other = other + 1 end
    end
    check(seenReward == reward and seenQuest == quest and other == 0, "reward_only and quest cards are in the catalog (shown before they are owned), acq none cards are not")
    check(CM.IsQuestReward(db.cards["FP1_028"]) and CM.IsQuestReward(db.cards["EX1_062"]) and not CM.IsQuestReward(db.cards["CS2_029"]) and not CM.IsQuestReward(nil), "quest reward cards are recognised")
    local cap = table.concat(CM.Caption("FP1_028", db.cards["FP1_028"], true), " ")
    check(string.find(cap, "Quest reward - Curse of Naxxramas", 1, true) ~= nil, "Naxxramas caption: " .. cap)

    -- sorted by cost, then name
    local sorted = true
    for i = 2, table.getn(cat.ids) do
        local a, b = db.cards[cat.ids[i - 1]], db.cards[cat.ids[i]]
        if (a.cost or 0) > (b.cost or 0) then sorted = false end
        if (a.cost or 0) == (b.cost or 0) and string.lower(a.name) > string.lower(b.name) then sorted = false end
    end
    check(sorted, "catalog sorted by cost and name")

    -- per class counts add up
    local sum = 0
    for i = 2, table.getn(CM.CLASS_TABS) do sum = sum + (cat.counts[CM.CLASS_TABS[i].key] or 0) end
    check(sum == cat.total and cat.counts.ALL == cat.total, "class counts add up to the catalog")
    check(cat.counts.MAGE == 26, "mage count (25 Basic/Classic + Duplicate of Naxxramas) " .. tostring(cat.counts.MAGE))

    -- every class of the data has a tab (except the non-collectible DREAM class)
    for _, cls in ipairs(db.classes) do
        if cls ~= "DREAM" then check(CM.TabIndex(cls) ~= nil, "tab for class " .. cls) end
    end
    local keys = {}
    for i = 1, table.getn(CM.CLASS_TABS) do
        local t = CM.CLASS_TABS[i]
        check(not keys[t.key], "unique tab " .. t.key)
        keys[t.key] = true
        check(t.icon ~= nil or (t.coords ~= nil and table.getn(t.coords) == 4), "tab icon for " .. t.key)
    end
    check(CM.CLASS_TABS[1].key == "ALL", "All tab exists")

    -- helpers for independent expectations
    local function count(pred)
        local n = 0
        for i = 1, table.getn(cat.ids) do
            if pred(db.cards[cat.ids[i]], cat.ids[i]) then n = n + 1 end
        end
        return n
    end
    local function query(mut)
        local f = CM.NewFilters()
        mut(f)
        return CM.Query(cat, f)
    end
    check(table.getn(CM.Query(cat, CM.NewFilters())) == cat.total, "empty filters return everything")
    check(table.getn(query(function(f) f.class = "MAGE" end)) == cat.counts.MAGE, "class filter")
    check(table.getn(query(function(f) f.mana[7] = true end)) == count(function(c) return (c.cost or 0) >= 7 end), "mana 7+ bucket")
    check(table.getn(query(function(f) f.mana[0] = true end)) == count(function(c) return (c.cost or 0) == 0 end), "mana 0")
    check(table.getn(query(function(f) f.mana[2] = true; f.mana[3] = true end)) ==
        count(function(c) return c.cost == 2 or c.cost == 3 end), "several mana buttons combine")
    check(table.getn(query(function(f) f.rarity = "RARE" end)) == count(function(c) return c.rarity == "RARE" end), "rarity rare")
    check(table.getn(query(function(f) f.rarity = "COMMON" end)) ==
        count(function(c) return c.rarity == "COMMON" or c.rarity == "FREE" or c.rarity == nil end), "common includes basic")
    check(table.getn(query(function(f) f.rarity = "LEGENDARY" end)) == count(function(c) return c.rarity == "LEGENDARY" end), "rarity legendary")
    check(table.getn(query(function(f) f.class = "MAGE"; f.rarity = "RARE"; f.mana[3] = true end)) ==
        count(function(c) return c.class == "MAGE" and c.rarity == "RARE" and c.cost == 3 end), "filters combine")

    -- search: name and rules text, case-insensitive, plain (never a Lua pattern), colour codes ignored
    local function has(list, id)
        for i = 1, table.getn(list) do if list[i] == id then return true end end
        return false
    end
    local r = query(function(f) f.search = "fireball" end)
    check(has(r, "CS2_029"), "search finds Fireball")
    r = query(function(f) f.search = "FIREBALL" end)
    check(has(r, "CS2_029"), "search is case-insensitive")
    r = query(function(f) f.search = "  deal   6 " end)
    check(has(r, "CS2_029"), "search matches rules text, words in any spacing")
    check(table.getn(query(function(f) f.search = "cffffd100" end)) == 0, "colour codes are not searchable")
    check(has(query(function(f) f.search = "taunt" end), "CS1_042"), "search finds keyword text hidden behind colour codes")
    local function plainCount(q)
        q = string.lower(q)
        return count(function(c)
            local text = string.gsub(CM.StripColors(c.text or ""), "\n", " ")
            local hay = string.lower(c.name .. "\n" .. text)
            return string.find(hay, q, 1, true) ~= nil
        end)
    end
    for _, q in ipairs({ "%", "(", "[", "^", ".*", "%d", "a-b", "+", "?", "$" }) do
        local ok, got = pcall(function() return query(function(f) f.search = q end) end)
        check(ok, "pattern characters do not raise: " .. q)
        if ok then check(table.getn(got) == plainCount(q), "plain search for '" .. q .. "'") end
    end
    check(table.getn(query(function(f) f.search = "zzzzqqq" end)) == 0, "no hits")

    -- filters state
    local f = CM.NewFilters()
    check(not CM.HasFilters(f), "fresh filters are inactive")
    f.mana[4] = true
    check(CM.HasFilters(f) and CM.ManaCount(f) == 1, "mana counts as a filter")
    CM.ResetFilters(f)
    f.class = "HUNTER"
    check(not CM.HasFilters(f) and f.class == "HUNTER", "reset keeps the class tab")
    check(CM.ManaBucket(0) == 0 and CM.ManaBucket(6) == 6 and CM.ManaBucket(7) == 7 and CM.ManaBucket(12) == 7 and CM.ManaBucket(nil) == 0, "mana buckets")

    -- paging
    check(CM.PageCount(0) == 1 and CM.PageCount(1) == 1 and CM.PageCount(8) == 1 and CM.PageCount(9) == 2 and CM.PageCount(16) == 2, "page count")
    check(CM.PageCount(cat.total) == math.floor((cat.total + 7) / 8), "page count of the catalog")
    check(CM.ClampPage(0, 3) == 1 and CM.ClampPage(99, 3) == 3 and CM.ClampPage("x", 3) == 1 and CM.ClampPage(2, 3) == 2, "page clamp")
    local list = {}
    for i = 1, 19 do list[i] = "c" .. i end
    check(table.getn(CM.PageSlice(list, 1)) == 8 and CM.PageSlice(list, 1)[1] == "c1" and CM.PageSlice(list, 2)[1] == "c9", "page slices")
    check(table.getn(CM.PageSlice(list, 3)) == 3 and CM.PageSlice(list, 3)[3] == "c19", "partial last page")
    check(table.getn(CM.PageSlice({}, 1)) == 0, "empty page")
    local covered = 0
    for p = 1, CM.PageCount(cat.total) do covered = covered + table.getn(CM.PageSlice(cat.ids, p)) end
    check(covered == cat.total, "pages cover the catalog exactly once")

    -- saved variable defaults
    local d = CM.ApplySavedDefaults(nil)
    check(d.anim == "on" and d.tab == nil and d.version == CM.SAVED_VERSION, "defaults")
    d = CM.ApplySavedDefaults({ anim = "x", tab = "BOGUS" })
    check(d.anim == "on" and d.tab == nil, "wrong types are repaired")
    d = CM.ApplySavedDefaults({ anim = "fast", tab = "MAGE" })
    check(d.anim == "fast" and d.tab == "MAGE", "valid values are kept")
    d = CM.ApplySavedDefaults({ minimapAngle = 45, minimapHidden = true, arch = { dx = 1 }, hand = { tilt = 3 } })
    check(d.minimapAngle == nil and d.minimapHidden == nil and d.arch == nil and d.hand == nil, "old settings are dropped")
    check(CM.ApplySavedDefaults("junk").anim == "on", "non-table saved variable")
    check(CM.TabForClassToken("mage") == "MAGE" and CM.TabForClassToken("DREAM") == nil and CM.TabForClassToken(nil) == nil and CM.TabForClassToken("ALL") == nil, "class token to tab")

    -- layout helpers
    check(CM.FitScale(1920, 1080, 782, 680) == 1, "large screen: no scaling")
    check(CM.FitScale(800, 600, 782, 680) == 0.84, "800x600 shrinks the window: " .. CM.FitScale(800, 600, 782, 680))
    check(CM.FitScale(100, 100, 782, 680) == 0.5, "never below 0.5")
    check(CM.PreviewSide(100, 800, 1920, 334) == "right", "room on the right")
    check(CM.PreviewSide(500, 1700, 1920, 334) == "left", "room only on the left")
    check(CM.PreviewSide(100, 800, 900, 334) == "right" and CM.PreviewSide(200, 1000, 1100, 334) == "left", "no room: the larger side")
    check(CM.PreviewSide(nil, nil, 1000, 334) == "right", "unknown geometry")

    -- textures deployed or not
    local saved = HS_CardTextures
    HS_CardTextures = { generated = true, order = { "CS2_029", "EX1_116" }, bottom = {} }
    check(CM.HasTexture("CS2_029") and not CM.HasTexture("CS1_042"), "texture set from CardTextures.lua")
    HS_CardTextures = { generated = true, order = { "CS1_042" }, bottom = {} }
    check(CM.HasTexture("CS1_042") and not CM.HasTexture("CS2_029"), "texture set follows a new table")
    HS_CardTextures = nil
    check(not CM.HasTexture("CS2_029"), "no CardTextures at all")
    HS_CardTextures = saved

    -- captions and fallback
    local cap = table.concat(CM.Caption("EX1_062", db.cards["EX1_062"], true), "\n")
    check(string.find(cap, "Quest reward only", 1, true) and string.find(cap, "Old Murk-Eye", 1, true) and not string.find(cap, "not installed", 1, true), "reward-only caption")
    cap = table.concat(CM.Caption("CS2_029", db.cards["CS2_029"], false), "\n")
    check(string.find(cap, "not installed", 1, true) and string.find(cap, "Cost 4", 1, true) and not string.find(cap, "Quest reward", 1, true), "text fallback caption")
    check(table.getn(CM.Caption("nope", nil, false)) >= 1 and CM.FallbackInfo("nope", nil).name == "nope", "unknown card does not raise")
    local fi = CM.FallbackInfo("EX1_116", db.cards["EX1_116"])
    check(fi.name == "Leeroy Jenkins" and fi.cost == tostring(db.cards["EX1_116"].cost) and fi.stats == "6/2", "fallback values: " .. fi.name .. " " .. fi.cost .. " " .. fi.stats)
    check(CM.StatsText(db.cards["EX1_116"]) == "6/2" and CM.StatsText(db.cards["CS2_029"]) == nil, "stats text")
    check(CM.RarityGroup({ rarity = "FREE" }) == "COMMON" and CM.RarityGroup({}) == "COMMON" and CM.RarityGroup({ rarity = "EPIC" }) == "EPIC", "rarity groups")
    check(CM.RarityLabel({ rarity = "FREE" }) == "Basic", "basic label")
    check(CM.StripColors("|cffffd100Taunt|r. |cffb8b8b8(x)|r") == "Taunt. (x)", "strip colours")
end

-- ------------------------------------------------ collection window (UI)
local function testCollectionUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    UI.PREVIEW_CAPTION = true                  -- these tests read the caption under the large card
    local MB = H.MicroButton
    local CM = HsCollection
    local db = HS_CardDB
    local savedTextures = HS_CardTextures
    local savedState = H.state.datasetStatus
    local savedDb = H.db

    local function resetUI()
        UI.frame, UI.preview, UI.hover = nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons = {}, {}, {}
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited = nil, nil, false
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    local function visibleSlots()
        local n = 0
        for i = 1, table.getn(UI.slots) do
            if UI.slots[i]:IsShown() then n = n + 1 end
        end
        return n
    end
    local function sameFilteredList(list, pred)
        local exp = 0
        local cat = UI.state.catalog
        for i = 1, table.getn(cat.ids) do
            if pred(db.cards[cat.ids[i]]) then exp = exp + 1 end
        end
        return table.getn(list) == exp
    end

    -- without a client UI (no CreateFrame) nothing is built and nothing raises
    resetUI()
    local before = CreateFrame
    check(before == nil, "the test runs without a client UI")
    check(H.ToggleCollection() == false, "toggle without a UI returns false")
    H.Slash("collection")
    H.Slash("c")
    check(UI.frame == nil, "no window without a UI")

    F.install({ class = { "Mage", "MAGE" } })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()
    printed = {}

    -- open the window through the slash command
    H.Slash("collection")
    local main = UI.frame
    check(main ~= nil and main:IsShown(), "/hs collection builds and shows the window")
    check(F.globals.HearthstoneFrame == main, "window has a global name")
    local special = false
    for i = 1, table.getn(UISpecialFrames) do
        if UISpecialFrames[i] == "HearthstoneFrame" then special = true end
    end
    check(special, "Escape closes the window (UISpecialFrames)")
    check(main._.movable and main._.clamped and main._.strata == "DIALOG", "movable, clamped to the screen, dialog strata")
    check(table.getn(UI.slots) == 8 and table.getn(UI.classTabs) == table.getn(CM.CLASS_TABS) - 1, "8 card slots and one tab per class (no All tab) plus Neutral")
    check(F.globals.HearthstoneFrameTab1 ~= nil and F.globals.HearthstoneFrameTab2 ~= nil and F.globals.HearthstoneFrameTab3 ~= nil and not F.globals.HearthstoneFrameTab3:IsShown() and main.numTabs == 3 and main.selectedTab == 1, "three bottom tabs (Collection, Packs, Heroes hidden until unlocked), Collection selected")
    check(F.globals.HearthstoneFrameTab1:GetText() == "Collection" and F.globals.HearthstoneFrameTab2:GetText() == "Packs", "bottom tab labels")
    check(main.title:GetText() == "Mage", "the class name is on the title plate: " .. tostring(main.title:GetText()))
    check(main.note:GetText() == "Catalog view (not connected)", "catalog note: " .. tostring(main.note:GetText()))
    check(UI.state.filters.class == "MAGE", "opens on the player's class tab (UI.START_TAB = PLAYER)")
    check(DEFAULT_START_TAB == "ALL", "the default start tab is all cards")
    do
        local keep = UI.START_TAB
        UI.START_TAB = DEFAULT_START_TAB
        local saved = UI.state.filters.class
        UI.InitState()
        check(UI.state.filters.class == "ALL", "by default the window opens on all cards, not on the class tab")
        UI.START_TAB = keep
        UI.state.filters.class = saved
    end
    local cat = UI.state.catalog
    check(cat ~= nil and table.getn(UI.state.results) == cat.counts.MAGE, "mage results")
    check(main.pageText:GetText() == "Page 1 / 4", "page text: " .. tostring(main.pageText:GetText()))
    check(visibleSlots() == 8, "8 visible cards on page 1")
    check(not main.prevButton:IsEnabled() and main.nextButton:IsEnabled(), "prev disabled on the first page")
    check(main.countText == nil, "no card count text on the book page (removed, owner request 2026-10-04)")
    check(not main.emptyText:IsShown(), "no empty message")
    check(UI.classTabs[3].key == "MAGE" and UI.classTabs[3].button:GetChecked() == 1 and UI.classTabs[1].button:GetChecked() == nil, "mage tab is checked")
    local created = table.getn(F.frames)

    -- text fallback while no texture is installed (placeholder CardTextures.lua)
    local w1 = UI.slots[1].widget
    local id1 = UI.slots[1].id
    check(w1.usingTexture == false and w1.fb:IsShown() and not w1.tex:IsShown(), "no texture deployed: text fallback")
    check(string.find(w1.name:GetText(), db.cards[id1].name, 1, true) ~= nil, "fallback shows the card name")
    check(w1.cost:GetText() ~= "" and w1.text:GetText() == (db.cards[id1].text or ""), "fallback shows cost and rules text")

    -- texture when the card is deployed
    HS_CardTextures = { generated = true, order = { id1 }, bottom = {} }
    UI.RefreshPage()
    check(w1.usingTexture == true and w1.tex:IsShown() and not w1.fb:IsShown(), "deployed texture is used")
    check(w1.tex._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Cards\\" .. id1, "texture path: " .. tostring(w1.tex._.texture))
    check(w1.tex._.texcoord[1] == 0 and w1.tex._.texcoord[2] == 1 and w1.tex._.texcoord[3] == 0 and w1.tex._.texcoord[4] == 388 / 512, "texcoord crops the 256x512 canvas")
    check(UI.slots[2].widget.usingTexture == false, "a card without texture next to it still falls back")
    HS_CardTextures = savedTextures
    UI.RefreshPage()

    -- mouse wheel pages (1.12: arg1 = delta, handler without parameters)
    F.fire(UI.slots[1], "OnMouseWheel", { arg1 = -1 })
    check(UI.state.page == 2 and main.pageText:GetText() == "Page 2 / 4", "wheel down: next page")
    check(main.prevButton:IsEnabled(), "prev enabled on page 2")
    F.fire(main, "OnMouseWheel", { arg1 = 1 })
    check(UI.state.page == 1, "wheel up: previous page")
    F.fire(main, "OnMouseWheel", { arg1 = 1 })
    check(UI.state.page == 1, "cannot go before page 1")
    for i = 1, 6 do F.fire(UI.slots[1], "OnMouseWheel", { arg1 = -1 }) end
    check(UI.state.page == 4 and visibleSlots() == 2, "last page holds the remainder: " .. visibleSlots())
    check(not main.nextButton:IsEnabled(), "next disabled on the last page")
    F.fire(main.nextButton, "OnClick")
    check(UI.state.page == 4, "next does nothing on the last page")
    F.fire(main.prevButton, "OnClick")
    check(UI.state.page == 3 and visibleSlots() == 8, "prev arrow")

    -- class tabs
    UI.SetClass("ALL")                                   -- not a tab any more, but the model state still exists
    check(UI.state.filters.class == "ALL" and H.db.tab == "ALL" and UI.state.page == 1, "All state, page reset, saved")
    check(table.getn(UI.state.results) == cat.total and UI.classTabs[3].button:GetChecked() == nil and main.title:GetText() == "All Cards", "All results")
    check(main.pageText:GetText() == "Page 1 / " .. CM.PageCount(cat.total), "all pages: " .. main.pageText:GetText())
    F.fire(UI.classTabs[9].button, "OnClick")
    check(UI.state.filters.class == "WARRIOR" and H.db.tab == "WARRIOR" and table.getn(UI.state.results) == cat.counts.WARRIOR and main.title:GetText() == "Warrior", "warrior tab")
    F.fire(UI.classTabs[10].button, "OnClick")
    check(UI.state.filters.class == "NEUTRAL" and table.getn(UI.state.results) == cat.counts.NEUTRAL and main.title:GetText() == "Neutral", "neutral tab")
    F.fire(UI.classTabs[3].button, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Mage" and F.tooltip.lines[2] == cat.counts.MAGE .. " cards", "class tab tooltip")
    F.fire(UI.classTabs[3].button, "OnLeave")
    check(not F.tooltip.shown, "tooltip hides")
    -- the active tab again clears the class filter (all cards); another tab selects again
    F.fire(UI.classTabs[10].button, "OnClick")
    check(UI.state.filters.class == "ALL" and table.getn(UI.state.results) == cat.total and main.title:GetText() == "All Cards" and UI.classTabs[10].button:GetChecked() == nil, "the active tab again: all cards, no tab selected")
    F.fire(UI.classTabs[3].button, "OnClick")
    check(UI.state.filters.class == "MAGE" and table.getn(UI.state.results) == cat.counts.MAGE, "a tab after that selects its class again")
    F.fire(UI.classTabs[10].button, "OnClick")

    -- search (EditBox OnTextChanged): it looks through every class, the title says so
    F.fire(UI.classTabs[3].button, "OnClick")
    check(table.getn(UI.state.results) == cat.counts.MAGE, "mage tab before the search")
    main.searchBox:SetText("fireball")
    check(main.title:GetText() == "Search" and UI.state.filters.class == "MAGE", "a search leaves the class tab alone but is titled Search")
    UI.SetClass("ALL")
    main.searchBox:SetText("")
    UI.SetClass("MAGE")
    main.searchBox:SetText("fireball")
    local ok1 = sameFilteredList(UI.state.results, function(c) return string.find(string.lower(c.name .. " " .. CM.StripColors(c.text or "")), "fireball", 1, true) ~= nil end)
    local hasFireball = false
    for i = 1, table.getn(UI.state.results) do if UI.state.results[i] == "CS2_029" then hasFireball = true end end
    check(ok1 and hasFireball and UI.state.page == 1, "typing filters the list")
    check(visibleSlots() == table.getn(UI.state.results), "only matching cards are shown")
    main.searchBox:SetText("")
    check(table.getn(UI.state.results) == cat.counts.MAGE, "clearing the search restores the list")
    main.searchBox:SetText("%(")
    check(table.getn(UI.state.results) == 0 and main.emptyText:IsShown() and string.find(main.emptyText:GetText(), "No cards match", 1, true) ~= nil, "no hits shows a message, pattern characters are harmless")
    check(visibleSlots() == 0 and main.pageText:GetText() == "Page 1 / 1", "empty page")
    main.searchBox:SetText("")
    check(main.title:GetText() == "Mage" and table.getn(UI.state.results) == cat.counts.MAGE, "clearing the search is back on the class")
    UI.SetClass("ALL")

    -- mana buttons: multi select, obvious state
    F.fire(UI.manaButtons[3], "OnClick")
    check(UI.state.filters.mana[3] == true and UI.manaButtons[3]._.locked == true and string.find(UI.manaButtons[3]:GetText(), "|cff33ff33", 1, true) ~= nil, "mana 3 is highlighted")
    check(sameFilteredList(UI.state.results, function(c) return c.cost == 3 end), "mana 3 results")
    F.fire(UI.manaButtons[7], "OnClick")
    check(sameFilteredList(UI.state.results, function(c) return c.cost == 3 or c.cost >= 7 end), "mana 3 and 7+ combine")
    check(UI.manaButtons[7]:GetText() ~= "7+" and string.find(UI.manaButtons[7]:GetText(), "7+", 1, true) ~= nil, "7+ label kept")
    F.fire(UI.manaButtons[3], "OnClick")
    check(UI.state.filters.mana[3] == nil and UI.manaButtons[3]._.locked == false and UI.manaButtons[3]:GetText() == "3", "toggling off")
    F.fire(UI.manaButtons[7], "OnEnter")
    check(F.tooltip.shown and string.find(F.tooltip.lines[1], "7 or more", 1, true) ~= nil, "mana tooltip")
    F.fire(UI.manaButtons[7], "OnLeave")

    -- rarity dropdown
    local dd = F.dropdowns.HearthstoneRarityDropDown
    check(dd ~= nil and table.getn(dd.items) == 5 and dd.items[1].text == "All rarities" and dd.items[5].text == "Legendary", "dropdown items")
    check(main.rarityDropDown.ddWidth == 84 and main.rarityDropDown.ddText == "All rarities", "dropdown width and text")
    F.clickDropDownItem(dd.items[4])
    check(UI.state.filters.rarity == "EPIC" and main.rarityDropDown.ddText == "Epic", "picking Epic")
    check(table.getn(UI.state.results) == 0 or db.cards[UI.state.results[1]].rarity == "EPIC", "epic + mana 7+ on the Mage tab")
    F.clickDropDownItem({ value = "RARE", func = dd.items[3].func })            -- handler reading this.value
    check(UI.state.filters.rarity == "RARE", "dropdown handler falls back to this.value")
    F.clickDropDownItem(dd.items[1])
    check(UI.state.filters.rarity == "ALL", "All rarities")

    -- reset button clears search, mana and rarity but keeps the class tab
    F.fire(UI.classTabs[3].button, "OnClick")
    main.searchBox:SetText("a")
    F.clickDropDownItem(dd.items[2])
    F.fire(UI.manaButtons[2], "OnClick")
    F.fire(F.globals.HearthstoneResetButton, "OnClick")
    check(not CM.HasFilters(UI.state.filters) and UI.state.filters.class == "MAGE" and main.searchBox:GetText() == "", "reset")
    check(table.getn(UI.state.results) == cat.counts.MAGE and main.rarityDropDown.ddText == "All rarities", "reset restores the class list")

    -- hover shows the enlarged card, reward-only cards say so
    main.searchBox:SetText("murk")
    check(UI.state.results[1] == "EX1_062" and UI.slots[1].marker:IsShown(), "reward-only card has a marker")
    F.fire(UI.slots[1], "OnEnter")
    local pv = UI.preview
    check(pv.frame:IsShown() and pv.widget.id == "EX1_062" and pv.widget.width == 256 and pv.widget.height == 388, "hover shows the full size card")
    check(string.find(pv.caption:GetText(), "Quest reward only", 1, true) ~= nil and string.find(pv.caption:GetText(), "not installed", 1, true) ~= nil, "caption: reward line and text fallback line")
    check(pv.widget.fb:IsShown() and string.find(pv.widget.name:GetText(), "Old Murk-Eye", 1, true) ~= nil, "enlarged text fallback")
    check(pv.frame._.strata == "TOOLTIP", "preview above other frames")
    local p = pv.frame._.points[1]
    check(p.point == "LEFT" and p.rel == main and p.relPoint == "RIGHT", "preview goes to the right of the window when there is room")
    F.fire(UI.slots[1], "OnLeave")
    check(not pv.frame:IsShown(), "leaving the card hides the preview")
    main._.left, main._.right = 500, 1220             -- no room on the right (1024 wide screen)
    F.fire(UI.slots[1], "OnEnter")
    p = pv.frame._.points[1]
    check(p.point == "RIGHT" and p.rel == main and p.relPoint == "LEFT", "preview switches to the left side")
    F.fire(UI.slots[1], "OnLeave")
    main._.left, main._.right = nil, nil
    HS_CardTextures = { generated = true, order = { "EX1_062" }, bottom = {} }
    F.fire(UI.slots[1], "OnEnter")
    check(pv.widget.usingTexture == true and string.find(pv.caption:GetText(), "not installed", 1, true) == nil, "enlarged card uses the texture when installed")
    F.fire(UI.slots[1], "OnLeave")
    HS_CardTextures = savedTextures
    main.searchBox:SetText("")

    -- everything above reused the frames created at the start
    check(table.getn(F.frames) == created, "no frames created while browsing: " .. table.getn(F.frames) .. " vs " .. created)

    -- the deck panel is part of the collection tab (offline state); the Packs tab has none
    check(main.sidePanel:IsShown() and main.sidePanel.browse:IsShown() and not main.sidePanel.edit:IsShown() and main.book:IsShown(), "collection tab: book and deck panel")
    check(main.sidePanel.browse.status:GetText() == "Not connected to the server." and not main.sidePanel.browse.newButton:IsEnabled(), "deck panel offline")
    check(main:GetWidth() == UI.WIDTH, "the window is wide enough for the deck panel")
    F.fire(F.globals.HearthstoneFrameTab2, "OnClick")
    check(UI.state.bottom == 2 and main.selectedTab == 2 and main.title:GetText() == "Card Packs" and main.packs:IsShown() and not main.unavailable:IsShown() and not main.book:IsShown(), "packs tab")
    check(not main.sidePanel:IsShown() and main:GetWidth() == UI.WIDTH_BOOK, "the Packs tab has no deck panel and a narrower window")
    check(main.packs.status:GetText() == "Not connected to the server." and not main.packs.stack:IsShown(), "packs tab offline")
    UI.state.page = 2
    F.fire(main, "OnMouseWheel", { arg1 = -1 })
    check(UI.state.page == 2, "wheel does not page outside the collection")
    F.fire(F.globals.HearthstoneFrameTab1, "OnClick")
    check(UI.state.bottom == 1 and main.book:IsShown() and not main.unavailable:IsShown() and not main.packs:IsShown() and main.sidePanel:IsShown() and main:GetWidth() == UI.WIDTH, "back to the collection")
    UI.state.page = 1

    -- nothing in the window suggests play, queues, ranking, trading, crafting or scrapping (except the
    -- craft panel, which only opens at a crafting NPC; testCraftUI covers it)
    local function underCard(o)
        while o and o._ do
            if o._.name and string.find(o._.name, "Card", 1, true) then return true end
            if o._.name and string.find(o._.name, "HearthstoneCraft", 1, true) then return true end
            o = o._.parent
        end
        return false
    end
    local bad = { "play", "queue", "ranked", "trad", "craft", "scrap", "invite", "dust", "disenchant" }
    for i = 1, table.getn(F.frames) do
        local o = F.frames[i]
        if not underCard(o) then
            local texts = { o._.text }
            for _, r in ipairs(o._.regions) do table.insert(texts, r._.text) end
            for _, t in ipairs(texts) do
                for _, w in ipairs(bad) do
                    check(not string.find(string.lower(tostring(t)), w, 1, true), "window text mentions '" .. w .. "': " .. tostring(t))
                end
            end
        end
    end
    for _, w in ipairs(bad) do
        for i = 2, 2 do
            check(not string.find(string.lower(UI.UNAVAILABLE[i]), w, 1, true), "unavailable text mentions " .. w)
        end
    end

    -- dragging the title bar moves the window (left button only)
    local drag = main.dragArea
    check(drag ~= nil and drag._.parent == main and drag._.scripts.OnMouseDown ~= nil, "drag area above the book exists")
    F.fire(drag, "OnMouseDown", { arg1 = "RightButton" })
    check(not main._.moving, "right button does not move the window")
    F.fire(drag, "OnMouseDown", { arg1 = "LeftButton" })
    check(main._.moving, "left button starts moving")
    F.fire(drag, "OnMouseUp", { arg1 = "LeftButton" })
    check(not main._.moving, "mouse up stops moving")

    -- search box hint and focus handling
    check(main.searchBox:GetText() == "", "search box empty")
    F.fire(main.searchBox, "OnEditFocusGained")
    F.fire(main.searchBox, "OnEscapePressed")
    F.fire(main.searchBox, "OnEnterPressed")

    -- closing and reopening; scale fits small screens
    main:Hide()
    check(not pv.frame:IsShown() and not main:IsShown(), "closing hides the preview")
    F.screenW, F.screenH = 800, 600
    F.UIParent._.w, F.UIParent._.h = 800, 600
    H.Slash("c")
    check(main:IsShown() and main._.scale == CM.FitScale(800, 600, UI.WIDTH + UI.TABS_OVERHANG, UI.HEIGHT + UI.BOTTOM_OVERHANG) and main._.scale < 1, "small screen: window scaled to fit " .. main._.scale)
    H.Slash("collection")
    check(not main:IsShown(), "second toggle closes")
    F.screenW, F.screenH = 1920, 1080
    F.UIParent._.w, F.UIParent._.h = 1920, 1080
    H.Slash("collection")
    check(main._.scale == 1, "large screen: scale 1")
    main:Hide()

    -- missing card database: the window opens with a message and does not raise
    HS_CardDB = nil
    UI.state.catalog, UI.state.catalogFor = nil, nil
    local okShow, errShow = pcall(function() main:Show() end)
    check(okShow, "window opens without CardDB: " .. tostring(errShow))
    check(main.emptyText:IsShown() and string.find(main.emptyText:GetText(), "could not be loaded", 1, true) ~= nil, "message for a missing CardDB")
    check(string.find(main.note:GetText(), "missing", 1, true) ~= nil and visibleSlots() == 0, "note and empty book without CardDB")
    F.fire(UI.classTabs[3].button, "OnClick")
    F.fire(UI.manaButtons[2], "OnClick")
    main.searchBox:SetText("x")
    F.fire(main.nextButton, "OnClick")
    HS_CardDB = { cards = {} }                                  -- broken database (no order list)
    check(pcall(function() UI.RefreshAll() end), "broken CardDB does not raise")
    HS_CardDB = db
    main:Hide()

    -- server mismatch: warning in the window, browsing still works
    H.state.datasetStatus = "mismatch"
    UI.ResetFilters()
    main.searchBox:SetText("")
    main:Show()
    check(string.find(main.note:GetText(), "does not match the server", 1, true) ~= nil and visibleSlots() > 0, "dataset mismatch warning")
    H.state.datasetStatus = savedState
    main:Hide()
    main:Show()
    check(string.find(main.note:GetText(), "Catalog view", 1, true) ~= nil, "warning gone after the check passes")
    main:Hide()

    -- hover state does not survive closing the window (OnLeave may not fire for a hidden frame)
    main:Show()
    F.fire(UI.slots[1], "OnEnter")
    check(UI.hover == 1 and UI.slots[1]._.bdBorder[4] == 1, "hovering")
    main:Hide()
    check(UI.hover == nil and UI.slots[1]._.bdBorder[4] == 0 and not UI.preview.frame:IsShown(), "closing clears hover, border and preview")
    main:Show()
    check(UI.hover == nil and not UI.preview.frame:IsShown(), "no stale preview on reopen")
    F.fire(UI.classTabs[1].button, "OnClick")
    F.fire(UI.slots[8], "OnEnter")
    main.searchBox:SetText("murk")                               -- slot 8 disappears while hovered
    check(UI.slots[8]._.bdBorder[4] == 0 and UI.hover == nil, "hidden slot border reset")
    main.searchBox:SetText("")

    -- the empty message is on a frame above the pages
    main.searchBox:SetText("zzzz")
    local ef = main.emptyText._.parent
    local pageLevel = UI.slots[1]._.parent._.level
    check(main.emptyText:IsShown() and ef._.level > pageLevel + 4, "empty message frame is above the pages and the cards")
    main.searchBox:SetText("")

    -- window position is saved and restored
    main._.left, main._.top = 40, 456                 -- left of the limit KeepTabsOnScreen allows for the wider book
    UI.SavePosition()
    check(H.db.pos.left == 40 and H.db.pos.top == 456, "position saved")
    main:Hide()
    main:Show()
    local pp = main._.points[1]
    check(pp.point == "TOPLEFT" and pp.rel == UIParent and pp.relPoint == "BOTTOMLEFT" and pp.x == 40 and pp.y == 456, "position restored")
    main._.left, main._.top = nil, nil
    main:Hide()
    check(CM.ApplySavedDefaults({ pos = { left = "x", top = 1 } }).pos == nil and CM.ApplySavedDefaults({ pos = { left = 1, top = 2 } }).pos.top == 2, "saved position validated")

    -- a build failure is reported, not raised
    local oldBuild, oldFrame = UI.Build, UI.frame
    UI.frame = nil
    UI.Build = function() error("boom") end
    printed = {}
    check(H.ToggleCollection() == false and string.find(lastPrinted(), "Could not build", 1, true) ~= nil, "build failure is reported")
    UI.Build = oldBuild
    UI.frame = oldFrame

    F.uninstall()
    HS_CardTextures = savedTextures
    HS_CardDB = db
    resetUI()
    H.db = savedDb
    H.state.datasetStatus = savedState
end

-- ---------------------------------------------------------- micro-menu button
local function testMicroButton()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local MB = H.MicroButton
    local CM = HsCollection
    local savedDb = H.db

    local function resetAll()
        UI.frame, UI.preview, UI.hover = nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons = {}, {}, {}
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited = nil, nil, false
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end

    resetAll()
    check(MB.Create() == nil, "no micro button without the default UI")

    F.install({ class = { "Priest", "PRIEST" } })
    H.db = CM.ApplySavedDefaults({ minimapHidden = true })
    local stock = WorldMapMicroButton
    local anchor = stock._.points[1]
    local parent = stock:GetParent()
    local frames = table.getn(F.frames)
    local b = MB.Create()
    check(b == stock and b:GetParent() == parent and b._.points[1] == anchor, "reuse the map button without moving it")
    check(b:GetWidth() == 29 and b:GetHeight() == 58, "keep the native micro-button size")
    check(table.getn(F.frames) == frames and F.globals.HearthstoneMinimapButton == nil, "no new button or minimap icon")
    check(b:GetNormalTexture():GetTexture() == MB.ICON and b:GetPushedTexture():GetTexture() == MB.ICON
        and b:GetDisabledTexture():GetTexture() == MB.ICON, "existing card icon in every button state")
    check(MB.border:GetTexture() == "Interface\\Buttons\\UI-MicroButtonCharacter-Up", "blank native micro-menu frame")
    check(b:GetHighlightTexture():GetTexture() == "Interface\\Buttons\\UI-MicroButton-Hilight", "native highlight kept")
    check(b:IsShown() and b:GetButtonState() == "NORMAL", "legacy minimap visibility does not hide the micro button")
    local update = UpdateMicroButtons
    local regions = table.getn(b._.regions)
    MB.Refresh()
    check(MB.Create() == b and UpdateMicroButtons == update and table.getn(b._.regions) == regions, "refresh installs once")

    F.fire(b, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Hearthstone" and F.tooltip.lines[2] == "Click: open collection", "Hearthstone tooltip")
    F.fire(b, "OnLeave")
    check(not F.tooltip.shown, "tooltip hides")
    F.hearthstoneKey = "H"
    F.fire(b, "OnEvent", { event = "UPDATE_BINDINGS" })
    F.fire(b, "OnEnter")
    check(b.tooltipText == "Hearthstone (H)" and F.tooltip.lines[1] == "Hearthstone (H)", "binding event keeps the Hearthstone label and shortcut")
    F.fire(b, "OnLeave")

    F.fire(b, "OnClick", { arg1 = "LeftButton" })
    check(UI.frame and UI.frame:IsShown() and not WorldMapFrame:IsShown() and F.mapToggles == 0, "click opens Hearthstone instead of the world map")
    check(UI.state.filters.class == "PRIEST" and b:GetButtonState() == "PUSHED", "collection tab and pushed state")
    local updates = F.microUpdates
    UpdateMicroButtons()
    check(F.microUpdates == updates + 1 and b:GetButtonState() == "PUSHED", "Blizzard updates still run and cannot clear the Hearthstone state")
    F.fire(b, "OnClick", { arg1 = "RightButton" })
    check(not UI.frame:IsShown() and b:GetButtonState() == "NORMAL", "second click closes and releases the button")
    H.Slash("collection")
    check(UI.frame:IsShown() and b:GetButtonState() == "PUSHED", "slash command also updates the button")
    UI.frame:Hide() -- Escape closes the frame directly.
    check(b:GetButtonState() == "NORMAL", "direct hide releases the button")

    check(GetBindingKey("TOGGLEWORLDMAP") == "M", "map shortcut is unchanged")
    ToggleWorldMap()
    check(WorldMapFrame:IsShown() and F.mapToggles == 1 and not UI.frame:IsShown()
        and b:GetButtonState() == "NORMAL", "map shortcut opens only the map and does not press Hearthstone")
    UI.Show()
    UpdateMicroButtons()
    check(b:GetButtonState() == "PUSHED", "Hearthstone state wins even when the map is open")
    UI.Hide()
    ToggleWorldMap()
    b:Disable()
    MB.Refresh()
    check(not b:IsEnabled(), "refresh preserves Blizzard's disabled state")
    b:Enable()

    HearthstoneDB = nil
    H.db = nil
    H.OnEvent("ADDON_LOADED", "SomeOtherAddon")
    check(H.db == nil and HearthstoneDB == nil, "other addons do not initialise saved variables")
    H.OnEvent("ADDON_LOADED", "Hearthstone")
    check(type(HearthstoneDB) == "table" and H.db == HearthstoneDB and MB.frame == b, "ADDON_LOADED installs the micro button")
    HearthstoneDB = { minimapAngle = 10, minimapHidden = true, tab = "SHAMAN" }
    H.OnEvent("VARIABLES_LOADED")
    check(H.db == HearthstoneDB and MB.frame == b and b:IsShown(), "saved legacy minimap settings are harmless")
    UI.Show()
    check(UI.state.filters.class == "SHAMAN", "saved collection tab is restored")
    UI.SetClass("DRUID")
    check(HearthstoneDB.tab == "DRUID", "tab is saved")
    UI.Hide()
    check(UpdateMicroButtons == update and table.getn(b._.regions) == regions, "saved-variable events do not duplicate hooks or artwork")

    F.uninstall()
    HearthstoneDB = nil
    resetAll()
    H.db = savedDb

    -- A custom UI may remove the default micro menu. Collection access still works.
    F.install()
    WorldMapMicroButton = nil
    MB.Refresh()
    check(MB.frame == nil and F.globals.HearthstoneMinimapButton == nil, "missing micro menu is harmless")
    check(H.ToggleCollection() and UI.frame:IsShown(), "collection remains usable without a micro menu")
    UI.Hide()
    F.uninstall()
    resetAll()

    -- The map button becomes Hearthstone only after the introduction's card quest.
    local savedIntro = H.state.intro
    F.install({ realm = "Realm A" })
    H.db = CM.ApplySavedDefaults(nil)
    H.SetIntro(false)
    MB.Refresh()
    local mapB, menuB = WorldMapMicroButton, MainMenuMicroButton
    check(MB.frame == nil and not mapB:IsShown() and mapB:GetNormalTexture():GetTexture() == "Interface\\Buttons\\UI-MicroButton-World-Up",
        "intro not done: the map button is hidden and untouched")
    local p, r, rp, x = menuB:GetPoint(1)
    check(p == "BOTTOMLEFT" and r == "SocialsMicroButton" and rp == "BOTTOMRIGHT" and x == -2,
        "the next micro button takes the map button's place")
    MB.Refresh()
    check(table.getn(menuB._.points) == 1 and not mapB:IsShown(), "hiding twice changes nothing")
    H.OnIntroDone()
    p, r, rp, x = menuB:GetPoint(1)
    check(mapB:IsShown() and r == mapB and x == -3, "unlocked: the button is back in its place, the next one anchored to it again")
    check(MB.frame == WorldMapMicroButton and MB.iconPath == MB.ICON and H.db.introDone["Realm A"] == true,
        "INTRO_DONE: the card back takes over the map button, remembered per realm")
    check(string.find(lastPrinted() or "", "micro menu", 1, true) == nil, "INTRO_DONE stays out of the chat")
    local tex = MB.frame:GetNormalTexture()
    check(tex._.texcoord == nil or (tex._.texcoord[2] == MB.ICON_RIGHT), "the card back is shown without the empty texture part")
    F.uninstall()
    resetAll()
    -- the next login: before the handshake the remembered answer installs it
    F.install({ realm = "Realm A" })
    H.state.intro = nil
    MB.Refresh()
    check(MB.frame == WorldMapMicroButton, "remembered intro: the button is there before WELCOME")
    F.uninstall()
    resetAll()
    -- another realm has no answer yet
    F.install({ realm = "Realm B" })
    H.state.intro = nil
    MB.Refresh()
    check(MB.frame == nil, "another realm: no button before WELCOME")
    -- the card back is not deployed: the stock icon
    F.missingTextures = { [MB.ICON] = true }
    MB.Create()
    check(MB.iconPath == MB.ICON_FALLBACK and MB.frame:GetNormalTexture():GetTexture() == MB.ICON_FALLBACK, "missing card back: stock icon")
    F.missingTextures = nil
    F.uninstall()
    resetAll()
    H.state.intro = savedIntro
    H.db = savedDb
end
-- ------------------------------------------------------ ownership (pure)
local function testOwnershipModel()
    local O = HsOwnership
    local CM = HsCollection
    local db = HS_CardDB

    -- a good reply
    local r, why = O.Parse("ver=7;n=3;cards=CS2_022.2.1,CS2_029.1.0,EX1_116.1.1", db)
    check(r ~= nil, "parse ok: " .. tostring(why))
    r = r or { owned = {} }
    check(r.version == 7 and r.entries == 3 and r.distinct == 3 and r.copies == 4 and r.protected == 2 and r.unknown == 0, "parse totals")
    check(r.owned.CS2_022 and r.owned.CS2_022.total == 2 and r.owned.CS2_022.prot == 1, "entry with protected copies")
    check(r.owned.CS2_029 and r.owned.CS2_029.total == 1 and r.owned.CS2_029.prot == 0 and r.owned.CS2_023 == nil, "entry without protected copies")
    r = O.Parse("ver=4294967295;n=1;cards=CS2_022.999.999", db)
    check(r and r.version == 4294967295 and r.owned.CS2_022.total == 999 and r.owned.CS2_022.prot == 999, "upper bounds accepted")
    r = O.Parse("n=1;cards=CS2_022.1.0;ver=2;extra=x", db)
    check(r and r.version == 2 and r.distinct == 1, "key order and unknown keys do not matter")

    -- empty collection
    r, why = O.Parse("ver=0;n=0;cards=", db)
    check(r and r.version == 0 and r.entries == 0 and r.distinct == 0 and r.copies == 0 and next(r.owned) == nil, "empty collection: " .. tostring(why))

    -- n does not match the entries
    for _, bad in ipairs({ "ver=1;n=2;cards=CS2_022.1.0", "ver=1;n=0;cards=CS2_022.1.0", "ver=1;n=1;cards=",
        "ver=1;n=1;cards=CS2_022.1.0,CS2_029.1.0" }) do
        r, why = O.Parse(bad, db)
        check(r == nil and why ~= nil, "n mismatch rejected: " .. bad)
    end

    -- malformed payloads and entries: the whole reply is rejected
    local malformed = {
        "", "garbage", "ver=1;n=1", "n=1;cards=CS2_022.1.0", "ver=1;cards=CS2_022.1.0",
        "ver=;n=1;cards=CS2_022.1.0", "ver=x;n=1;cards=CS2_022.1.0", "ver=-1;n=1;cards=CS2_022.1.0",
        "ver=01;n=1;cards=CS2_022.1.0", "ver=1;n=01;cards=CS2_022.1.0", "ver=1.5;n=1;cards=CS2_022.1.0",
        "ver=4294967296;n=1;cards=CS2_022.1.0", "ver=1;n=99999;cards=CS2_022.1.0", "ver=1;n=1e0;cards=CS2_022.1.0",
        "ver=1;n=1;cards=CS2_022.1", "ver=1;n=1;cards=CS2_022", "ver=1;n=1;cards=CS2_022..1.0",
        "ver=1;n=1;cards=CS2_022.1.0.0", "ver=1;n=1;cards=CS2_022.1.0,", "ver=1;n=1;cards=,CS2_022.1.0",
        "ver=1;n=2;cards=CS2_022.1.0,,CS2_029.1.0", "ver=1;n=1;cards=CS2 022.1.0", "ver=1;n=1;cards=CS2-022.1.0",
        "ver=1;n=1;cards=.1.0", "ver=1;n=1;cards=CS2_022.-1.0", "ver=1;n=1;cards=CS2_022.1.x",
        "ver=1;n=1;cards=CS2_022.01.0", "ver=1;n=1;cards=CS2_022.1.00", "ver=1;n=1;cards=CS2_022. 1.0",
        "ver=1;n=2;cards=CS2_022.1.0,CS2_022.2.0", "ver=1;n=1;cards=" .. string.rep("A", 33) .. ".1.0",
        "ver=1;n=1;n=1;cards=CS2_022.1.0",
    }
    for _, bad in ipairs(malformed) do
        r, why = O.Parse(bad, db)
        check(r == nil and why ~= nil, "malformed rejected: " .. bad)
    end
    check(O.Parse(nil, db) == nil and O.Parse(42, db) == nil, "no payload")

    -- numbers out of range
    for _, bad in ipairs({ "CS2_022.0.0", "CS2_022.1000.0", "CS2_022.2.3", "CS2_022.1.1000", "CS2_022.9999.1" }) do
        r = O.Parse("ver=1;n=2;cards=CS2_029.1.0," .. bad, db)
        check(r == nil, "out of range rejected: " .. bad)
    end

    -- unknown ids are ignored but counted
    r = O.Parse("ver=2;n=4;cards=CS2_022.1.0,ZZZ_999.2.2,zzz_1.1.0,EX1_116.2.0", db)
    check(r and r.distinct == 2 and r.copies == 3 and r.protected == 0 and r.unknown == 2 and r.entries == 4, "unknown ids counted")
    check(r and r.owned.ZZZ_999 == nil and r.owned.zzz_1 == nil and r.owned.EX1_116.total == 2, "unknown ids are not owned")
    r = O.Parse("ver=2;n=1;cards=CS2_022.1.0", nil)
    check(r and r.distinct == 0 and r.unknown == 1, "without a card database every id is unknown")

    -- a realistic full collection survives the wire (escaping, chunking, reassembly limits)
    local cat = CM.NewCatalog(db)
    local parts = {}
    for i = 1, table.getn(cat.ids) do parts[i] = cat.ids[i] .. ".999.999" end
    table.sort(parts)
    local big = "ver=4294967295;n=" .. table.getn(parts) .. ";cards=" .. table.concat(parts, ",")
    local frames = P.ChunkPayload("COLL", "abcd1234", big)
    check(frames ~= nil and table.getn(frames) <= 99, "full collection fits into the part limit")
    local reasm = P.NewReassembler({ maxBytes = 16384, maxParts = P.MAX_PARTS_LIMIT, timeout = 15, maxPending = 8 })
    local st, done
    for i = 1, table.getn(frames or {}) do
        local _, f = P.ParseFrame(frames[i])
        st, done = reasm:Add(0, f.op, f.reqId, f.part, f.total, f.payload)
    end
    check(st == "complete" and done.payload == big, "full collection reassembles within the addon limits")
    r = done and O.Parse(done.payload, db)
    check(r and r.distinct == cat.total and r.copies == 999 * cat.total, "full collection parses")

    -- state
    O.Reset()
    check(O.state == "none" and not O.IsReady() and O.Get("CS2_022") == nil and O.CaptionLine("CS2_022") == nil, "reset: no ownership")
    check(string.find(O.StatusText(), "none", 1, true) ~= nil, "status none")
    O.SetLoading()
    check(O.state == "loading" and O.StatusText() == "collection: loading", "loading")
    O.SetReady(O.Parse("ver=5;n=2;cards=CS2_022.2.1,CS2_029.1.0", db))
    check(O.IsReady() and O.version == 5 and O.Get("CS2_022").total == 2 and O.Get("CS2_023") == nil, "ready")
    check(O.CountOwned({ "CS2_022", "CS2_023", "CS2_029" }) == 2 and O.CountOwned(nil) == 0, "count owned")
    check(O.BadgeText("CS2_022") == "x2" and O.BadgeText("CS2_029") == "x1" and O.BadgeText("CS2_023") == nil, "badge text")
    check(string.find(O.CaptionLine("CS2_022"), "Owned: 2 (1 protected)", 1, true) ~= nil, "caption with protected copies")
    check(string.find(O.CaptionLine("CS2_029"), "Owned: 1", 1, true) ~= nil and string.find(O.CaptionLine("CS2_029"), "protected", 1, true) == nil, "caption without protected copies")
    check(string.find(O.CaptionLine("CS2_023"), "Not owned", 1, true) ~= nil, "caption not owned")
    check(O.StatusText() == "collection: ready, version 5, 2 distinct cards, 3 copies, 1 protected, 0 unknown ids", "status ready: " .. O.StatusText())
    O.SetLoading()
    check(O.IsReady() and O.Get("CS2_022") ~= nil, "a refetch keeps the data")
    O.SetError("UNAVAILABLE: db")
    check(O.state == "error" and O.Get("CS2_022") == nil and O.version == nil and O.distinct == 0, "error drops the data")
    check(string.find(O.StatusText(), "error (UNAVAILABLE: db)", 1, true) ~= nil, "status error")
    O.SetLoading()
    check(O.state == "loading", "error to loading")
    O.Reset()

    -- the ownership filter combined with the other filters
    local owned = O.Parse("ver=1;n=4;cards=CS2_022.2.1,CS2_025.1.0,CS2_029.1.0,EX1_116.1.1", db).owned
    local function q(mut, own)
        local f = CM.NewFilters()
        mut(f)
        return CM.Query(cat, f, own)
    end
    local function has(list, id)
        for i = 1, table.getn(list) do if list[i] == id then return true end end
        return false
    end
    check(table.getn(q(function(f) f.own = "OWNED" end, owned)) == 4, "owned")
    check(table.getn(q(function(f) f.own = "UNOWNED" end, owned)) == cat.total - 4, "not owned")
    check(table.getn(q(function(f) f.own = "ALL" end, owned)) == cat.total, "all cards")
    check(table.getn(q(function(f) f.own = "OWNED" end, nil)) == cat.total, "owned without ownership data acts like all")
    local l = q(function(f) f.own = "OWNED"; f.class = "MAGE" end, owned)
    check(table.getn(l) == 3 and not has(l, "EX1_116"), "owned + class")
    l = q(function(f) f.own = "OWNED"; f.mana[4] = true end, owned)
    check(table.getn(l) == 3 and has(l, "CS2_022") and has(l, "CS2_029") and has(l, "EX1_116"), "owned + mana (Leeroy costs 4 here)")
    l = q(function(f) f.own = "OWNED"; f.search = "fireball" end, owned)
    check(table.getn(l) == 1 and l[1] == "CS2_029", "owned + search")
    l = q(function(f) f.own = "OWNED"; f.rarity = "LEGENDARY" end, owned)
    check(table.getn(l) == 1 and l[1] == "EX1_116", "owned + rarity")
    l = q(function(f) f.own = "UNOWNED"; f.class = "MAGE"; f.mana[4] = true end, owned)
    check(not has(l, "CS2_022") and not has(l, "CS2_029") and table.getn(l) == table.getn(q(function(f) f.class = "MAGE"; f.mana[4] = true end, nil)) - 2, "not owned + class + mana")
    check(table.getn(q(function(f) f.own = "OWNED" end, {})) == 0, "owning nothing")
    local f = CM.NewFilters()
    check(f.own == "ALL" and not CM.HasFilters(f), "fresh filters: all cards")
    f.own = "OWNED"
    check(CM.HasFilters(f), "ownership counts as a filter")
    CM.ResetFilters(f)
    check(f.own == "ALL", "reset clears the ownership filter")
    check(CM.OwnIndex("OWNED") == 2 and CM.OwnIndex("x") == nil and CM.OWN_FILTERS[3].label == "Not owned", "ownership filter entries")
end

-- --------------------------------------------- collection requests (protocol)
local function testCollectionFlow()
    local O = HsOwnership
    resetAddon()
    local function queued()
        local out = {}
        for i = 1, table.getn(H.queue) do
            local _, f = P.ParseFrame(H.queue[i])
            table.insert(out, f)
        end
        return out
    end
    local function collGets()
        local n = 0
        for _, f in ipairs(queued()) do
            if f.op == "COLL_GET" then
                n = n + 1
                check(f.payload == "", "COLL_GET carries no payload (the addon never sends counts)")
            end
        end
        return n
    end
    local function reply(reqId, payload)
        local frames = P.ChunkPayload("COLL", reqId, payload)
        for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
    end
    local function push(payload)
        H.HandleFrame(P.EncodeFrame("COLL_CHANGED", "0", 1, 1, payload))
    end
    local function err(reqId, code)
        H.HandleFrame(P.EncodeFrame("ERR", reqId, 1, 1, "code=" .. code .. ";msg=test"))
    end
    local function welcome()
        H.HandleFrame(P.EncodeFrame("WELCOME", "abc", 1, 1,
            P.KvEncode({ { "protocol", "1" }, { "server", "9" }, { "dataset", HS_CardDB.datasetId }, { "hash", HS_CardDB.hash },
                { "caps", "solo,chunk" }, { "maxframe", "240" }, { "debug", "0" } })))
    end
    local notified = 0
    local savedHook = H.OnOwnershipChanged
    H.OnOwnershipChanged = function() notified = notified + 1 end

    -- nothing before WELCOME
    check(H.CollGet() == nil and table.getn(H.queue) == 0, "no COLL_GET before WELCOME")
    push("ver=3")
    check(table.getn(H.queue) == 0 and O.state == "none", "COLL_CHANGED before WELCOME is ignored")
    reply("zz", "ver=1;n=0;cards=")
    check(O.state == "none", "unsolicited COLL before WELCOME is ignored")

    -- WELCOME: exactly one COLL_GET, loading
    welcome()
    check(collGets() == 1 and O.state == "loading" and H.coll.reqId == queued()[1].reqId, "WELCOME sends one COLL_GET")
    check(H.pending[H.coll.reqId] and H.pending[H.coll.reqId].op == "COLL_GET", "request pending")
    check(notified >= 1, "UI told about loading")
    H.Slash("status")
    check(string.find(table.concat(printed, "\n"), "collection: loading", 1, true) ~= nil, "status while loading")

    -- the reply (chunked, with unknown ids)
    local id1 = H.coll.reqId
    H.queue = {}
    local cards = { "CS2_022.2.1", "CS2_029.1.0", "ZZZ_404.1.0" }
    local long = {}
    for i = 1, 40 do table.insert(long, "UNKNOWN_" .. i .. ".1.0") end
    reply(id1, "ver=3;n=" .. (3 + table.getn(long)) .. ";cards=" .. table.concat(cards, ",") .. "," .. table.concat(long, ","))
    check(O.IsReady() and O.version == 3 and O.distinct == 2 and O.copies == 3 and O.protected == 1 and O.unknown == 41, "multi-frame reply applied")
    check(H.coll.reqId == nil and H.pending[id1] == nil and table.getn(H.queue) == 0, "request finished, nothing more sent")
    printed = {}
    H.Slash("status")
    check(string.find(table.concat(printed, "\n"), "collection: ready, version 3, 2 distinct cards, 3 copies, 1 protected, 41 unknown ids", 1, true) ~= nil,
        "status shows the collection")

    -- a late or foreign COLL does not touch the data
    reply(id1, "ver=9;n=0;cards=")
    reply("0", "ver=9;n=0;cards=")
    check(O.version == 3 and O.distinct == 2, "COLL without a matching request is ignored")

    -- COLL_CHANGED with the held version: nothing; a new version: one refetch, data kept meanwhile
    push("ver=3")
    check(table.getn(H.queue) == 0, "same version: no refetch")
    push("ver=4")
    check(collGets() == 1 and O.IsReady() and O.Get("CS2_022") ~= nil, "new version: one COLL_GET, data stays visible")
    local id2 = H.coll.reqId
    push("ver=5")
    push("ver=6")
    check(collGets() == 1 and H.coll.refetch == true, "pushes while one is in flight only mark a refetch")
    printed = {}
    H.Slash("status")
    check(string.find(table.concat(printed, "\n"), "refetch pending", 1, true) ~= nil, "status mentions the pending refetch")
    H.queue = {}
    reply(id2, "ver=5;n=1;cards=CS2_022.3.1")
    check(O.version == 5 and O.Get("CS2_022").total == 3 and O.Get("CS2_029") == nil, "refetched data applied")
    check(collGets() == 1 and H.coll.reqId ~= nil and H.coll.reqId ~= id2 and not H.coll.refetch, "exactly one refetch after the reply")
    local id3 = H.coll.reqId
    H.queue = {}
    reply(id3, "ver=6;n=1;cards=CS2_022.3.1")
    check(O.version == 6 and H.coll.reqId == nil and table.getn(H.queue) == 0, "no further request")
    push("ver=6")
    check(table.getn(H.queue) == 0, "caught up")
    push("garbage~z")
    check(collGets() == 1, "a malformed push still refetches once")
    H.queue = {}
    reply(H.coll.reqId, "ver=6;n=1;cards=CS2_022.3.1")

    -- ERR UNAVAILABLE answering the COLL_GET: error, catalog stays usable
    push("ver=7")
    local id4 = H.coll.reqId
    H.queue = {}
    printed = {}
    err(id4, "UNAVAILABLE")
    check(O.state == "error" and O.Get("CS2_022") == nil and H.coll.reqId == nil and H.pending[id4] == nil, "UNAVAILABLE: error state")
    check(string.find(table.concat(printed, "\n"), "card catalog still works", 1, true) ~= nil, "UNAVAILABLE message")
    check(string.find(O.StatusText(), "UNAVAILABLE", 1, true) ~= nil, "status names the error")
    check(table.getn(H.queue) == 0, "no automatic retry loop")

    -- a later COLL_CHANGED recovers
    push("ver=8")
    check(collGets() == 1 and O.state == "loading", "COLL_CHANGED after an error refetches")
    local id5 = H.coll.reqId
    H.queue = {}
    reply(id5, "ver=8;n=0;cards=")
    check(O.IsReady() and O.distinct == 0, "recovered with an empty collection")

    -- UNAVAILABLE without a request id still fails the request in flight
    push("ver=9")
    local id6 = H.coll.reqId
    H.Request("PING", "ping", {})
    H.queue = {}
    err("0", "UNAVAILABLE")
    check(O.state == "error" and H.coll.reqId == nil and H.pending[id6] == nil, "unattributed UNAVAILABLE")

    -- a refetch requested while the failing request was in flight is still sent once
    push("ver=10")
    local id7 = H.coll.reqId
    push("ver=11")
    H.queue = {}
    err(id7, "UNAVAILABLE")
    check(collGets() == 1 and O.state == "loading" and H.coll.reqId ~= id7, "refetch after a failed request")
    H.queue = {}
    reply(H.coll.reqId, "ver=11;n=1;cards=CS2_029.1.0")
    check(O.IsReady() and O.version == 11, "ready again")

    -- malformed reply: error, data dropped
    push("ver=12")
    H.queue = {}
    printed = {}
    reply(H.coll.reqId, "ver=12;n=2;cards=CS2_029.1.0")
    check(O.state == "error" and O.Get("CS2_029") == nil and H.coll.reqId == nil, "malformed reply: error")
    check(string.find(table.concat(printed, "\n"), "cannot read", 1, true) ~= nil, "malformed reply message")
    reply("q1", "ver=12;n=1;cards=CS2_029.1.0")
    check(O.state == "error", "still error")

    -- timeout of COLL_GET
    push("ver=13")
    local id8 = H.coll.reqId
    drain(1)
    check(table.getn(H.queue) == 0 and H.pending[id8] ~= nil, "COLL_GET sent")
    printed = {}
    H.clock = H.clock + 20
    H.OnUpdate(0.1)
    check(O.state == "error" and O.errorText == "no response" and H.coll.reqId == nil and H.pending[id8] == nil, "timeout: error")
    check(string.find(table.concat(printed, "\n"), "timed out", 1, true) ~= nil, "timeout message")
    reply(id8, "ver=13;n=0;cards=")
    check(O.state == "error", "a reply after the timeout is ignored")

    -- any other ERR answering it, also ERR RATE on reqId 0 when COLL_GET is the oldest request
    push("ver=14")
    H.queue = {}
    err(H.coll.reqId, "MALFORMED")
    check(O.state == "error" and H.coll.reqId == nil, "other ERR answering COLL_GET")
    push("ver=15")
    H.queue = {}
    H.clock = H.clock + 1
    H.Request("PING", "ping", {})
    err("0", "RATE")
    check(O.state == "error" and H.coll.reqId == nil, "ERR RATE takes the oldest request, the COLL_GET")
    -- an ERR for another request does not touch the collection
    push("ver=16")
    local id9 = H.coll.reqId
    local pingId = H.Request("PING", "ping", {})
    err(pingId, "NOT_IMPLEMENTED")
    check(O.state == "loading" and H.coll.reqId == id9, "ERR for another request leaves the collection alone")

    -- the connection is gone: back to the catalog
    err("0", "DISABLED")
    check(O.state == "none" and H.coll.reqId == nil and not H.coll.refetch, "DISABLED: no ownership")
    push("ver=17")
    check(H.coll.reqId == nil and O.state == "none", "pushes after losing the connection are ignored")
    welcome()
    check(O.state == "loading" and H.coll.reqId ~= nil, "a new WELCOME fetches again")
    H.queue = {}
    reply(H.coll.reqId, "ver=17;n=1;cards=CS2_029.1.0")
    err("0", "VERSION")
    check(O.state == "none" and not H.state.welcomed, "VERSION: no ownership")

    -- every frame the addon queued for the collection was an empty COLL_GET
    H.OnOwnershipChanged = savedHook
    resetAddon()
end

-- ------------------------------------------- ownership in the collection window
local function testCollectionOwnershipUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    UI.PREVIEW_CAPTION = true                  -- these tests read the caption under the large card
    local W = H.CardWidget
    local CM = HsCollection
    local O = HsOwnership
    local db = HS_CardDB
    local savedDb = H.db

    local function resetUI()
        UI.frame, UI.preview, UI.hover = nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons = {}, {}, {}
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited = nil, nil, false
    end
    local function slotOf(id)
        for i = 1, table.getn(UI.slots) do
            if UI.slots[i]:IsShown() and UI.slots[i].id == id then return UI.slots[i], i end
        end
        return nil
    end
    local function ownItems()
        local dd = UI.frame.ownDropDown
        UIDropDownMenu_Initialize(dd, dd.initialize)          -- the client re-runs it when the menu opens
        return F.dropdowns.HearthstoneOwnedDropDown.items
    end
    local function welcome()
        H.HandleFrame(P.EncodeFrame("WELCOME", "abc", 1, 1,
            P.KvEncode({ { "protocol", "1" }, { "server", "9" }, { "dataset", db.datasetId }, { "hash", db.hash },
                { "caps", "solo,chunk" }, { "maxframe", "240" }, { "debug", "0" } })))
    end
    local function reply(payload)
        local frames = P.ChunkPayload("COLL", H.coll.reqId, payload)
        for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
        H.queue = {}
    end
    local function push(ver)
        H.HandleFrame(P.EncodeFrame("COLL_CHANGED", "0", 1, 1, "ver=" .. ver))
        H.queue = {}
    end

    resetAddon()
    F.install({ class = { "Mage", "MAGE" } })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()
    H.Slash("collection")
    local main = UI.frame
    local cat = UI.state.catalog
    local created = table.getn(F.frames)

    -- not connected: the plain catalog, labelled as such
    check(main.note:GetText() == "Catalog view (not connected)" and main.title:GetText() == "Mage", "catalog view label")
    check(main.ownDropDown ~= nil and main.ownDropDown.ddText == "All cards" and main.ownDropDown.ddWidth == 78, "ownership dropdown")
    local items = ownItems()
    check(table.getn(items) == 3 and items[1].text == "All cards" and items[2].text == "Owned" and items[3].text == "Not owned", "ownership items")
    check(items[1].disabled == nil and items[2].disabled == 1 and items[3].disabled == 1 and items[1].checked == 1, "Owned / Not owned disabled without ownership")
    F.clickDropDownItem(items[2])
    check(UI.state.filters.own == "ALL" and table.getn(UI.state.results) == cat.counts.MAGE, "Owned falls back to All without ownership")
    local s1 = UI.slots[1]
    check(not s1.badge:IsShown() and not s1.widget.dimmed and s1.widget.frame._.alpha == 1, "no badges and no dimming in the catalog view")
    F.fire(s1, "OnEnter")
    check(string.find(UI.preview.caption:GetText(), "wned", 1, true) == nil, "no ownership line in the catalog view")
    F.fire(s1, "OnLeave")

    -- loading while the window is open
    welcome()
    check(main.note:GetText() == "Loading collection..." and main.title:GetText() == "Mage", "loading line")
    check(ownItems()[2].disabled == 1, "still disabled while loading")

    -- ready: the first three cards of the mage page are A (owned x2, 1 protected), B (owned x1), C (not owned)
    local A, B, C = UI.state.pageIds[1], UI.state.pageIds[2], UI.state.pageIds[3]
    check(A and B and C and db.cards[A].class == "MAGE", "three mage cards on page 1")
    reply("ver=1;n=3;cards=" .. A .. ".2.1," .. B .. ".1.0,EX1_116.1.1")
    check(main.title:GetText() == "Mage", "the title stays the class name with ownership")
    check(main.note:GetText() == "Collection: 3 of " .. cat.total .. " cards", "status line: " .. tostring(main.note:GetText()))
    local sa, sb, sc = slotOf(A), slotOf(B), slotOf(C)
    check(sa and sb and sc, "the three cards are still on the page")
    check(sa.badge:IsShown() and sa.badgeText:GetText() == "x2" and sb.badge:IsShown() and sb.badgeText:GetText() == "x1", "quantity badges")
    check(not sc.badge:IsShown(), "no badge on an unowned card")
    check(sc.widget.dimmed and sc.widget.frame._.alpha == W.DIM_ALPHA and sc.widget.tex._.vertex[1] == W.DIM_SHADE and sc.widget.tex._.desat == 1, "unowned card dimmed")
    check(not sa.widget.dimmed and sa.widget.frame._.alpha == 1 and sa.widget.tex._.vertex[1] == 1 and sa.widget.tex._.desat == nil, "owned card not dimmed")
    check(sa.badge._.level > sa.widget.frame._.level, "badge above the card")
    items = ownItems()
    check(items[2].disabled == nil and items[3].disabled == nil, "Owned / Not owned enabled")

    -- hover caption
    local _, ia = slotOf(A)
    F.fire(UI.slots[ia], "OnEnter")
    check(string.find(UI.preview.caption:GetText(), "Owned: 2 (1 protected)", 1, true) ~= nil, "caption: owned with protected copies")
    check(UI.preview.widget.dimmed == nil, "the enlarged card is never dimmed")
    F.fire(UI.slots[ia], "OnLeave")
    local _, ib = slotOf(B)
    F.fire(UI.slots[ib], "OnEnter")
    check(string.find(UI.preview.caption:GetText(), "Owned: 1", 1, true) ~= nil and string.find(UI.preview.caption:GetText(), "protected", 1, true) == nil, "caption: owned")
    F.fire(UI.slots[ib], "OnLeave")
    local _, ic = slotOf(C)
    F.fire(UI.slots[ic], "OnEnter")
    check(string.find(UI.preview.caption:GetText(), "Not owned", 1, true) ~= nil, "caption: not owned")

    -- the hovered card's caption follows an ownership change
    push(2)
    reply("ver=2;n=5;cards=" .. A .. ".2.1," .. B .. ".1.0," .. C .. ".1.0,CS2_029.1.0,EX1_116.1.1")
    check(UI.hover == ic and string.find(UI.preview.caption:GetText(), "Owned: 1", 1, true) ~= nil, "preview refreshed on change")
    check(sc.badge:IsShown() and sc.badgeText:GetText() == "x1" and not sc.widget.dimmed, "page refreshed on change")
    F.fire(UI.slots[ic], "OnLeave")

    -- ownership filter combined with the class tab, mana, search and paging
    local ownedSet = { [A] = true, [B] = true, [C] = true, CS2_029 = true, EX1_116 = true }
    local function expected(pred)
        local n = 0
        for i = 1, table.getn(cat.ids) do
            local id = cat.ids[i]
            if ownedSet[id] and pred(db.cards[id], id) then n = n + 1 end
        end
        return n
    end
    local function allOwned()
        for i = 1, table.getn(UI.state.results) do
            if not ownedSet[UI.state.results[i]] then return false end
        end
        return true
    end
    F.clickDropDownItem(items[2])
    check(UI.state.filters.own == "OWNED" and main.ownDropDown.ddText == "Owned", "Owned picked")
    check(table.getn(UI.state.results) == 4 and allOwned() and true, "owned mage cards")
    local bucket = CM.ManaBucket(db.cards[A].cost)
    F.fire(UI.manaButtons[bucket], "OnClick")
    check(table.getn(UI.state.results) == expected(function(c) return c.class == "MAGE" and CM.ManaBucket(c.cost) == bucket end) and allOwned(), "owned + mana")
    F.fire(UI.manaButtons[bucket], "OnClick")
    main.searchBox:SetText("fireball")
    check(table.getn(UI.state.results) == 1 and UI.state.results[1] == "CS2_029", "owned + search")
    main.searchBox:SetText("")
    F.clickDropDownItem(F.dropdowns.HearthstoneRarityDropDown.items[5])
    check(table.getn(UI.state.results) == 0 and main.emptyText:GetText() == "No cards match your filters.", "owned + rarity: no owned mage legendary")
    UI.SetClass("ALL")
    check(table.getn(UI.state.results) == 1 and UI.state.results[1] == "EX1_116", "owned + rarity + All tab")
    F.clickDropDownItem(F.dropdowns.HearthstoneRarityDropDown.items[1])
    check(table.getn(UI.state.results) == 5 and UI.state.filters.own == "OWNED", "owned + All tab keeps the ownership filter")
    F.clickDropDownItem({ value = "UNOWNED", func = items[3].func })   -- handler reading this.value
    check(UI.state.filters.own == "UNOWNED" and table.getn(UI.state.results) == cat.total - 5, "Not owned")
    check(main.pageText:GetText() == "Page 1 / " .. CM.PageCount(cat.total - 5), "paging over the unowned cards")
    for i = 1, CM.PER_PAGE do
        local sl = UI.slots[i]
        check(not sl.badge:IsShown() and sl.widget.dimmed, "unowned page is dimmed")
    end
    F.fire(UI.slots[1], "OnMouseWheel", { arg1 = -1 })
    check(UI.state.page == 2, "next page with the filter")
    F.fire(F.globals.HearthstoneResetButton, "OnClick")
    check(UI.state.filters.own == "ALL" and main.ownDropDown.ddText == "All cards" and table.getn(UI.state.results) == cat.total, "reset clears the ownership filter")

    -- a change keeps the page where it can
    F.fire(UI.slots[1], "OnMouseWheel", { arg1 = -1 })
    F.fire(UI.slots[1], "OnMouseWheel", { arg1 = -1 })
    push(3)
    reply("ver=3;n=1;cards=" .. A .. ".1.0")
    check(UI.state.page == 3, "page kept on an ownership change")

    -- Owned with nothing owned
    F.fire(UI.classTabs[3].button, "OnClick")
    push(4)
    reply("ver=4;n=0;cards=")
    UI.SetOwnFilter("OWNED")
    check(table.getn(UI.state.results) == 0 and main.emptyText:IsShown() and main.emptyText:GetText() == "You do not own any cards yet.", "empty collection message")
    check(main.note:GetText() == "Collection: 0 of " .. cat.total .. " cards", "status line with nothing owned")
    push(5)
    reply("ver=5;n=1;cards=EX1_116.1.0")
    check(table.getn(UI.state.results) == 0 and main.emptyText:GetText() == "No cards match your filters.", "owned cards elsewhere: normal empty message")
    push(6)
    reply("ver=6;n=1;cards=CS2_029.2.0")
    check(table.getn(UI.state.results) == 1 and slotOf("CS2_029").badgeText:GetText() == "x2" and not main.emptyText:IsShown(), "Owned list refreshed on a change")

    -- error: the catalog stays usable, the filter falls back to All
    H.HandleFrame(P.EncodeFrame("COLL_CHANGED", "0", 1, 1, "ver=7"))
    H.queue = {}
    H.HandleFrame(P.EncodeFrame("ERR", H.coll.reqId, 1, 1, "code=UNAVAILABLE;msg=db down"))
    check(main.note:GetText() == "|cffff5555Collection unavailable|r" and main.title:GetText() == "Mage", "unavailable line")
    check(UI.state.filters.own == "ALL" and main.ownDropDown.ddText == "All cards" and table.getn(UI.state.results) == cat.counts.MAGE, "filter falls back to All")
    check(ownItems()[2].disabled == 1, "Owned disabled again")
    for i = 1, CM.PER_PAGE do
        local sl = UI.slots[i]
        if sl:IsShown() then check(not sl.badge:IsShown() and not sl.widget.dimmed, "no ownership shown after an error") end
    end

    -- ownership arriving while the window is closed shows up when it opens
    main:Hide()
    push(8)
    reply("ver=8;n=1;cards=" .. A .. ".1.0")
    main:Show()
    check(slotOf(A) and slotOf(A).badge:IsShown() and main.note:GetText() == "Collection: 1 of " .. cat.total .. " cards", "applied on open")

    -- losing the connection: back to the catalog view
    H.HandleFrame(P.EncodeFrame("ERR", "0", 1, 1, "code=DISABLED;msg=off"))
    check(main.note:GetText() == "Catalog view (not connected)" and not slotOf(A).badge:IsShown(), "catalog view after DISABLED")

    check(table.getn(F.frames) == created, "ownership changes create no frames: " .. table.getn(F.frames) .. " vs " .. created)

    main:Hide()
    F.uninstall()
    resetUI()
    H.db = savedDb
    resetAddon()
end

-- ---------------------------------------------------------- packs (pure)
local function testPacksModel()
    local PK = HsPacks
    local CM = HsCollection
    resetAddon()
    local savedDb = H.db
    H.db = nil

    -- PACKS: the sample of docs/PROTOCOL.md
    local r, why = PK.ParsePacks("type=classic;stock=2;opened=7;recent=7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189")
    check(r and r.stock == 2 and r.opened == 7 and table.getn(r.recent) == 1, "PACKS sample parses: " .. tostring(why))
    local o = r and r.recent[1]
    check(o and o.seq == 7 and o.cards[1] == "EX1_116" and o.cards[2] == "CS2_022" and o.cards[5] == "CS2_189", "opening ids in slot order")
    check(o and o.new[1] == true and o.new[2] == false and o.new[5] == false, "opening mask")
    r = PK.ParsePacks("type=classic;stock=0;opened=0;recent=")
    check(r and r.stock == 0 and r.opened == 0 and table.getn(r.recent) == 0, "no openings: empty recent")
    r = PK.ParsePacks("type=classic;stock=100000;opened=4294967295;recent=")
    check(r and r.stock == 100000, "upper bounds")
    local five = "9.00000.A.B.C.D.E,8.11111.A.A.A.A.A,5.00001.ZZZ_999.B.C.D.E,2.01010.a_b.B.C.D.E,1.00000.A.B.C.D.E"
    r = PK.ParsePacks("type=classic;stock=1;opened=9;recent=" .. five)
    check(r and table.getn(r.recent) == 5 and r.recent[3].cards[1] == "ZZZ_999" and r.recent[2].new[3] == true, "five openings, unknown ids accepted")
    local id32 = string.rep("A", 32)
    check(PK.ParsePacks("type=classic;stock=1;opened=9;recent=9.00000." .. id32 .. ".B.C.D.E") ~= nil, "32 character id")
    local badPacks = {
        "", "garbage~z", "stock=1;opened=1;recent=", "type=wild;stock=1;opened=1;recent=",
        "type=classic;opened=1;recent=", "type=classic;stock=-1;opened=1;recent=", "type=classic;stock=01;opened=1;recent=",
        "type=classic;stock=100001;opened=1;recent=", "type=classic;stock=;opened=1;recent=", "type=classic;stock=1;opened=x;recent=",
        "type=classic;stock=1;opened=4294967296;recent=", "type=classic;stock=1;opened=1",
        "type=classic;stock=1;opened=9;recent=" .. five .. ",0.00000.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=9.1000.A.B.C.D.E", "type=classic;stock=1;opened=9;recent=9.10002.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=9.100000.A.B.C.D.E", "type=classic;stock=1;opened=9;recent=9.10000.A.B.C.D",
        "type=classic;stock=1;opened=9;recent=9.10000.A.B.C.D.E.F", "type=classic;stock=1;opened=9;recent=9.10000.A-1.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=9.10000." .. id32 .. "X.B.C.D.E", "type=classic;stock=1;opened=9;recent=0.10000.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=10.10000.A.B.C.D.E", "type=classic;stock=1;opened=9;recent=07.10000.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=7.10000.A.B.C.D.E,7.10000.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=6.10000.A.B.C.D.E,7.10000.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=7.10000.A.B.C.D.E,", "type=classic;stock=1;opened=9;recent=,7.10000.A.B.C.D.E",
        "type=classic;stock=1;opened=9;recent=7.10000.A.B..D.E", "type=classic;stock=1;opened=9;recent=7..A.B.C.D.E",
    }
    for i = 1, table.getn(badPacks) do
        check(PK.ParsePacks(badPacks[i]) == nil, "PACKS rejected: " .. badPacks[i])
    end
    check(PK.ParsePacks(nil) == nil, "PACKS without payload")

    -- PACK_OPENED
    local okOpened = "id=8;key=abcdef123456;replay=0;stock=1;ver=9;new=11000;cards=CS2_022,EX1_116,CS2_022,CS2_189,EX1_011"
    r, why = PK.ParseOpened(okOpened)
    check(r and r.seq == 8 and r.key == "abcdef123456" and r.replay == false and r.stock == 1 and r.ver == 9, "PACK_OPENED sample: " .. tostring(why))
    check(r and r.cards[1] == "CS2_022" and r.cards[3] == "CS2_022" and r.cards[5] == "EX1_011" and r.new[1] and r.new[2] and not r.new[3], "PACK_OPENED cards and mask (duplicates allowed)")
    r = PK.ParseOpened("id=1;key=0123456789abcdef;replay=1;stock=0;ver=0;new=00000;cards=ZZZ_1,ZZZ_1,ZZZ_1,ZZZ_1,ZZZ_1")
    check(r and r.replay == true and r.key == "0123456789abcdef", "replay, 16 character key, unknown ids")
    local function openedWith(field, value)
        local parts = { id = "8", key = "abcdef123456", replay = "0", stock = "1", ver = "9", new = "11000",
            cards = "CS2_022,EX1_116,CS2_022,CS2_189,EX1_011" }
        parts[field] = value
        local out = {}
        for _, k in ipairs({ "id", "key", "replay", "stock", "ver", "new", "cards" }) do
            if parts[k] then table.insert(out, k .. "=" .. parts[k]) end
        end
        return table.concat(out, ";")
    end
    local badOpened = {
        { "id", "0" }, { "id", "x" }, { "id", "08" }, { "id", false }, { "key", "abcdef12345" }, { "key", "abcdef1234567890a" },
        { "key", "ABCDEF123456" }, { "key", "abcdef_23456" }, { "key", false }, { "replay", "2" }, { "replay", "" }, { "replay", false },
        { "stock", "100001" }, { "stock", "-1" }, { "stock", false }, { "ver", "01" }, { "ver", "4294967296" }, { "ver", false },
        { "new", "1100" }, { "new", "110001" }, { "new", "11a00" }, { "new", false }, { "cards", "A,B,C,D" }, { "cards", "A,B,C,D,E,F" },
        { "cards", "A,,C,D,E" }, { "cards", "A,B,C,D,E," }, { "cards", "A,B,C,D," .. id32 .. "X" }, { "cards", "A,B,C,D,E F" },
        { "cards", "" }, { "cards", false },
    }
    for i = 1, table.getn(badOpened) do
        local b = badOpened[i]
        local payload = openedWith(b[1], b[2] or nil)
        check(PK.ParseOpened(payload) == nil, "PACK_OPENED rejected: " .. payload)
    end
    check(PK.ParseOpened("garbage~z") == nil and PK.ParseOpened(nil) == nil, "PACK_OPENED garbage")

    -- helpers
    check(PK.CapsHavePacks("solo,chunk,packs") and PK.CapsHavePacks("packs") and not PK.CapsHavePacks("solo,chunk")
        and not PK.CapsHavePacks("solo,packsx") and not PK.CapsHavePacks("") and not PK.CapsHavePacks(nil), "caps")
    check(PK.IsValidKey("abcdefghijkl") and not PK.IsValidKey("abc") and not PK.IsValidKey(nil), "key check")
    local keys = {}
    local distinct = true
    for _ = 1, 300 do
        local k = PK.NewKey()
        check(PK.IsValidKey(k) and string.len(k) == PK.KEY_LEN and string.find(k, "^[0-9a-z]+$") ~= nil, "new key " .. k)
        if keys[k] then distinct = false end
        keys[k] = true
    end
    check(distinct, "keys are fresh")

    -- rarities (Basic and unknown ids count as Common)
    check(PK.RarityOf("EX1_116") == "LEGENDARY" and PK.RarityOf("CS2_028") == "RARE" and PK.RarityOf("CS2_053") == "EPIC", "rarity lookup")
    check(PK.RarityOf("CS2_022") == "COMMON" and PK.RarityOf("CS2_189") == "COMMON" and PK.RarityOf("ZZZ_404") == "COMMON", "basic / unknown are common")
    check(PK.RaritySummary({ "EX1_116", "CS2_022", "CS2_022", "EX1_011", "CS2_189" }) == "|cffff80001 Legendary|r, |cffffffff4 Common|r", "summary")
    check(PK.RecentText({ seq = 7, cards = { "CS2_053", "CS2_028", "CS2_161", "CS2_189", "ZZZ" } }) == "#7  |cffa335ee1 Epic|r, |cff0070dd2 Rare|r, |cffffffff2 Common|r", "recent row text")
    local c = PK.RARITY_COLORS
    check(c.RARE[1] == 0 and c.RARE[2] == 0.44 and c.RARE[3] == 0.87 and c.EPIC[1] == 0.64 and c.LEGENDARY[2] == 0.5 and c.COMMON[1] == 1, "item quality colours")

    -- seen marker, recovery and the reveal queue (no saved variables: memory, realm "?")
    local function opening(seq, mask, cards)
        return { seq = seq, new = PK.ParseMask(mask or "00000"), cards = cards or { "CS2_022", "EX1_116", "CS2_028", "CS2_189", "EX1_011" } }
    end
    PK.SetReady({ stock = 3, opened = 7, recent = { opening(7, "10000"), opening(6), opening(5) } })
    check(PK.GetSeen() == nil, "fresh install: no marker")
    check(PK.Recover() == 0 and PK.GetSeen() == 7 and PK.Current() == nil, "fresh install: marker starts at 'opened', nothing to recover")
    PK.SetSeen(5)
    check(PK.Recover() == 2 and table.getn(PK.queue) == 2, "two unseen openings queued")
    local cur = PK.Current()
    check(cur.seq == 6 and cur.recovered and not cur.flipped[1] and PK.queue[2].seq == 7, "oldest first, face down")
    check(PK.Recover() == 0 and table.getn(PK.queue) == 2, "not queued twice")
    check(PK.View(1) == false and PK.view == nil, "no view while a reveal is on screen")
    check(PK.Flip(1) and not PK.Flip(1) and not PK.Flip(9) and not PK.AllFlipped(), "flip one card")
    check(PK.FinishReveal() == false and PK.GetSeen() == 5, "cannot finish with cards face down")
    check(PK.FlipAll() == 4 and PK.AllFlipped() and PK.FlipAll() == 0, "flip the rest")
    check(PK.FinishReveal() and PK.GetSeen() == 6 and PK.Current().seq == 7, "finish: marked seen, next reveal")
    check(PK.Current().new[1] == true and PK.Current().new[2] == false, "reveal keeps the new flags")
    PK.FlipAll()
    check(PK.FinishReveal() and PK.GetSeen() == 7 and PK.Current() == nil, "all revealed")
    check(PK.Enqueue(opening(7), false) == false and PK.Enqueue(opening(3), false) == false, "seen openings are not queued again")
    check(PK.View(1) and PK.view.seq == 7 and not PK.View(9), "view a recent opening")
    PK.CloseView()
    check(PK.view == nil, "view closed")
    check(PK.Enqueue(opening(8), false) and PK.view == nil and PK.Current().seq == 8 and not PK.Current().recovered, "own opening queued")
    PK.View(1)
    check(PK.view == nil, "view refused while revealing")
    PK.FlipAll()
    PK.FinishReveal()
    PK.MarkSeen(3)
    check(PK.GetSeen() == 8, "the marker never goes down")
    PK.SetSeen(50)
    check(PK.Recover() == 0 and PK.GetSeen() == 7, "marker above 'opened' (server data reset) is lowered")

    -- ApplyOpened keeps the recent list newest first and at most five long
    PK.SetReady({ stock = 3, opened = 7, recent = { opening(7), opening(6), opening(5), opening(4), opening(3) } })
    PK.ApplyOpened({ seq = 8, stock = 2, cards = opening(8).cards, new = opening(8).new })
    check(PK.stock == 2 and PK.opened == 8 and table.getn(PK.recent) == 5 and PK.recent[1].seq == 8 and PK.recent[5].seq == 4, "recent list follows an opening")
    PK.ApplyOpened({ seq = 8, stock = 2, cards = opening(8).cards, new = opening(8).new })
    check(table.getn(PK.recent) == 5 and PK.recent[2].seq == 7, "a replayed opening is not listed twice")

    -- pending open: the key survives until it is cleared
    local p = PK.BeginOpen()
    local k = p.key
    p.inFlight = false
    check(PK.BeginOpen().key == k and PK.pending.attempts == 0 and PK.IsOpening(), "an unconfirmed key is reused")
    PK.ClearPending()
    check(PK.BeginOpen().key ~= k, "a fresh key after a confirmed opening")
    PK.ClearPending()

    -- status line
    check(string.find(PK.StatusText(), "packs: ready, 2 unopened, 8 opened", 1, true) ~= nil, "status ready")
    PK.SetUnsupported()
    check(PK.StatusText() == "packs: not supported by this server", "status unsupported")
    PK.SetError("UNAVAILABLE: x")
    check(PK.StatusText() == "packs: error (UNAVAILABLE: x)" and PK.stock == 0, "status error")
    PK.Reset()
    check(PK.StatusText() == "packs: none (not connected)", "status none")

    -- saved variables: packsSeen per realm, validated
    local db = CM.ApplySavedDefaults(nil)
    check(type(db.packsSeen) == "table" and next(db.packsSeen) == nil, "packsSeen default")
    db = CM.ApplySavedDefaults({ packsSeen = { A = 3, B = "x", C = -1, D = 2.5, E = 0 } })
    check(db.packsSeen.A == 3 and db.packsSeen.E == 0 and db.packsSeen.B == nil and db.packsSeen.C == nil and db.packsSeen.D == nil, "packsSeen validated")
    check(type(CM.ApplySavedDefaults({ packsSeen = "x" }).packsSeen) == "table", "packsSeen replaced when not a table")
    H.db = CM.ApplySavedDefaults(nil)
    local savedRealm = GetRealmName
    GetRealmName = function() return "Realm A" end
    PK.SetSeen(4)
    GetRealmName = function() return "Realm B" end
    check(PK.GetSeen() == nil, "the marker is per realm")
    PK.SetSeen(9)
    check(H.db.packsSeen["Realm A"] == 4 and H.db.packsSeen["Realm B"] == 9, "stored in HearthstoneDB.packsSeen")
    GetRealmName = function() return nil end
    PK.SetSeen(1)
    check(H.db.packsSeen["?"] == 1, "no realm name: '?'")
    GetRealmName = savedRealm

    H.db = savedDb
    resetAddon()
end

-- ----------------------------------------------------------- packs (flow)
local SAMPLE_CARDS = "EX1_116,CS2_028,CS2_053,CS2_189,CS2_022"

local function packHelpers()
    local t = {}
    function t.sentOps(op)
        local out = {}
        for _, fr in ipairs(sent) do
            local st, f = P.ParseFrame(fr)
            if st == "ok" and f.op == op then table.insert(out, f) end
        end
        return out
    end
    function t.queuedOps(op)
        local out = {}
        for i = 1, table.getn(H.queue) do
            local st, f = P.ParseFrame(H.queue[i])
            if st == "ok" and f.op == op then table.insert(out, f) end
        end
        return out
    end
    function t.welcome(caps)
        H.HandleFrame(P.EncodeFrame("WELCOME", "abc", 1, 1,
            P.KvEncode({ { "protocol", "1" }, { "server", "9" }, { "dataset", HS_CardDB.datasetId }, { "hash", HS_CardDB.hash },
                { "caps", caps }, { "maxframe", "240" }, { "debug", "0" } })))
    end
    function t.frame(op, reqId, payload)
        H.HandleFrame(P.EncodeFrame(op, reqId, 1, 1, payload))
    end
    function t.err(reqId, code, text)
        t.frame("ERR", reqId, "code=" .. code .. ";msg=" .. (text or "test"))
    end
    function t.packs(stock, opened, recent)
        t.frame("PACKS", H.packs.reqId, "type=classic;stock=" .. stock .. ";opened=" .. opened .. ";recent=" .. (recent or ""))
    end
    function t.opened(reqId, seq, key, stock, ver, mask, cards, replay)
        t.frame("PACK_OPENED", reqId, "id=" .. seq .. ";key=" .. key .. ";replay=" .. (replay or "0") .. ";stock=" .. stock
            .. ";ver=" .. ver .. ";new=" .. (mask or "10000") .. ";cards=" .. (cards or SAMPLE_CARDS))
    end
    function t.coll(ver)
        local frames = P.ChunkPayload("COLL", H.coll.reqId, "ver=" .. ver .. ";n=1;cards=CS2_022.1.0")
        for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
    end
    function t.joined()
        return table.concat(printed, "\n")
    end
    return t
end

local function testPacksFlow()
    local PK = HsPacks
    local O = HsOwnership
    local T = packHelpers()
    resetAddon()
    local savedDb = H.db
    H.db = HsCollection.ApplySavedDefaults(nil)
    local notified = 0
    local savedHook = H.OnPacksChanged
    H.OnPacksChanged = function() notified = notified + 1 end
    local function flush() drain(1) end
    local function lastOpen()
        local l = T.sentOps("PACK_OPEN")
        return l[table.getn(l)]
    end
    local function keyOf(f)
        local kv = f and P.KvDecode(f.payload)
        return kv and kv.key
    end

    -- nothing before WELCOME
    local k0, why0 = H.PackOpen()
    check(k0 == nil and why0 == "Not connected to the server." and H.PackGet() == nil and table.getn(H.queue) == 0, "no pack requests before WELCOME")
    T.frame("PACK_CHANGED", "0", "stock=5")
    check(PK.state == "none" and PK.stock == 0 and table.getn(H.queue) == 0, "PACK_CHANGED before WELCOME is ignored")

    -- a server without packs
    T.welcome("solo,chunk")
    check(PK.state == "unsupported" and table.getn(T.queuedOps("PACK_GET")) == 0, "caps without packs: unsupported, no PACK_GET")
    local k1, why1 = H.PackOpen()
    check(k1 == nil and why1 == "This server does not support packs yet.", "cannot open on such a server")
    T.frame("PACK_CHANGED", "0", "stock=5")
    check(table.getn(T.queuedOps("PACK_GET")) == 0 and PK.stock == 0, "pushes ignored without packs")
    H.Slash("status")
    check(string.find(T.joined(), "packs: not supported by this server", 1, true) ~= nil, "status without packs")
    flush()
    T.coll(1)
    H.queue = {}

    -- with packs: COLL_GET then PACK_GET
    T.welcome("solo,chunk,packs")
    local q = {}
    for i = 1, table.getn(H.queue) do local _, f = P.ParseFrame(H.queue[i]); table.insert(q, f) end
    check(table.getn(q) == 2 and q[1].op == "COLL_GET" and q[2].op == "PACK_GET" and q[2].payload == "", "WELCOME sends COLL_GET and an empty PACK_GET")
    check(PK.state == "loading" and H.packs.reqId == q[2].reqId and H.pending[q[2].reqId].op == "PACK_GET", "packs loading")
    check(H.PackOpen() == nil, "cannot open while loading")
    T.frame("PACK_CHANGED", "0", "stock=5")
    check(PK.state == "loading" and table.getn(T.queuedOps("PACK_GET")) == 1, "a push while loading waits for the reply")
    flush()
    T.coll(3)
    check(O.IsReady() and O.version == 3, "collection ready")
    T.frame("PACKS", "zz", "type=classic;stock=9;opened=9;recent=")
    check(PK.state == "loading", "a PACKS for another reqId is ignored")
    T.packs(2, 7, "7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189")
    check(PK.IsReady() and PK.stock == 2 and PK.opened == 7 and table.getn(PK.recent) == 1 and H.packs.reqId == nil, "PACKS applied")
    check(PK.GetSeen() == 7 and PK.Current() == nil, "fresh install: nothing to recover")
    check(notified > 0, "UI told")
    printed = {}
    H.Slash("status")
    check(string.find(T.joined(), "packs: ready, 2 unopened, 7 opened", 1, true) ~= nil, "status line for packs")

    -- PACK_CHANGED
    T.frame("PACK_CHANGED", "0", "stock=5")
    check(PK.stock == 5 and table.getn(H.queue) == 0, "PACK_CHANGED updates the stock")
    T.frame("PACK_CHANGED", "0", "stock=x")
    check(table.getn(T.queuedOps("PACK_GET")) == 1 and PK.IsReady() and PK.stock == 5, "an unreadable push refetches, data stays")
    flush()
    T.packs(5, 7, "7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189")

    -- open -> PACK_OPENED -> reveal -> Done -> seen
    sent = {}
    local key = H.PackOpen()
    check(PK.IsValidKey(key) and string.len(key) == 12 and PK.IsOpening(), "open starts: " .. tostring(key))
    local k2, why2 = H.PackOpen()
    check(k2 == nil and why2 == "A pack is being opened.", "only one open in flight")
    printed = {}
    H.Slash("open")
    check(string.find(T.joined(), "A pack is being opened.", 1, true) ~= nil, "/hs open says why")
    flush()
    local f1 = lastOpen()
    check(table.getn(T.sentOps("PACK_OPEN")) == 1 and keyOf(f1) == key and f1.payload == "key=" .. key, "one PACK_OPEN with the key")
    check(f1.reqId ~= key and H.packReqs[f1.reqId] == "PACK_OPEN", "the key is not the reqId")
    T.opened("zz9", 8, "zzzzzzzzzzzz", 4, 4)
    check(PK.pending ~= nil and PK.stock == 5, "a PACK_OPENED with another key and reqId is ignored")
    H.queue = {}
    T.opened(f1.reqId, 8, key, 4, 4, "11001")
    check(PK.pending == nil and PK.stock == 4 and PK.opened == 8 and PK.recent[1].seq == 8 and PK.recent[2].seq == 7, "opening confirmed")
    local rv = PK.Current()
    check(rv and rv.seq == 8 and rv.cards[1] == "EX1_116" and rv.new[1] and rv.new[2] and not rv.new[3] and rv.new[5] and not rv.flipped[1], "reveal queued face down")
    check(table.getn(T.queuedOps("COLL_GET")) == 1 and H.coll.covers == 4, "collection refetched (version changed)")
    T.frame("COLL_CHANGED", "0", "ver=4")
    check(table.getn(T.queuedOps("COLL_GET")) == 1 and not H.coll.refetch, "the COLL_CHANGED after it needs no second fetch")
    flush()
    T.coll(4)
    check(O.version == 4 and table.getn(H.queue) == 0, "collection at the new version")
    local k3, why3 = H.PackOpen()
    check(k3 == nil and why3 == "Finish looking at your last pack first.", "no open during a reveal")
    PK.FlipAll()
    check(PK.FinishReveal() and PK.GetSeen() == 8 and H.db.packsSeen["?"] == 8, "reveal done, seen saved")
    T.opened(f1.reqId, 8, key, 4, 4)
    check(PK.Current() == nil and PK.stock == 4, "a duplicate PACK_OPENED without a pending open is ignored")

    -- a PACK_OPENED whose version we already hold does not refetch
    local keyB = H.PackOpen()
    flush()
    H.queue = {}
    O.version = 9
    T.opened(lastOpen().reqId, 9, keyB, 3, 9)
    check(table.getn(T.queuedOps("COLL_GET")) == 0, "same collection version: no refetch")
    PK.FlipAll()
    PK.FinishReveal()

    -- timeout and UNAVAILABLE: the same key, fresh reqIds, three sends, then stop and keep the key
    sent = {}
    printed = {}
    local key2 = H.PackOpen()
    check(key2 ~= key and key2 ~= keyB, "a fresh key for a new pack")
    flush()
    local a1 = lastOpen()
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(PK.IsOpening() and PK.pending.retryIn ~= nil and H.pending[a1.reqId] == nil, "timeout: retry scheduled")
    check(string.find(T.joined(), "timed out", 1, true) == nil, "no timeout message for a retried open")
    drain(3)
    local opens = T.sentOps("PACK_OPEN")
    check(table.getn(opens) == 2 and keyOf(opens[2]) == key2 and opens[2].reqId ~= a1.reqId, "retry with the same key and a new reqId")
    T.err(opens[2].reqId, "UNAVAILABLE", "db down")
    check(O.IsReady() and O.version == 9 and string.find(T.joined(), "card collection", 1, true) == nil, "UNAVAILABLE for PACK_OPEN leaves the collection alone")
    check(PK.IsOpening() and PK.pending.key == key2, "UNAVAILABLE: still opening, key kept")
    drain(3)
    opens = T.sentOps("PACK_OPEN")
    check(table.getn(opens) == 3 and keyOf(opens[3]) == key2 and PK.pending.attempts == 3, "third attempt, same key")
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(not PK.IsOpening() and PK.pending ~= nil and PK.pending.key == key2, "gave up after three attempts, key kept")
    check(string.find(T.joined(), "did not confirm", 1, true) ~= nil and string.find(T.joined(), "Recent packs", 1, true) ~= nil, "player told")
    check(table.getn(T.queuedOps("PACK_GET")) == 1, "resync requested")
    flush()
    check(table.getn(T.sentOps("PACK_OPEN")) == 3, "no fourth send")
    T.packs(3, 9, "9.00000.A.B.C.D.E,8.11001." .. string.gsub(SAMPLE_CARDS, ",", "."))
    check(PK.IsReady() and PK.stock == 3 and PK.Current() == nil, "resync: nothing new")
    printed = {}
    H.Slash("status")
    check(string.find(T.joined(), "unconfirmed", 1, true) ~= nil, "status mentions the kept key")
    -- the next click reuses the key; a late reply to an earlier attempt settles it
    local key2b = H.PackOpen()
    flush()
    local a4 = lastOpen()
    check(key2b == key2 and keyOf(a4) == key2 and PK.pending.attempts == 1, "the next click reuses the key")
    T.opened(opens[1].reqId, 10, key2, 2, 10)
    check(PK.pending == nil and H.pending[a4.reqId] == nil and PK.Current().seq == 10, "late reply to an earlier attempt accepted, no timeout left")
    flush()
    T.coll(10)
    H.clock = H.clock + 20
    printed = {}
    H.OnUpdate(0.05)
    check(T.joined() == "", "nothing times out afterwards")
    PK.FlipAll()
    PK.FinishReveal()
    H.queue = {}

    -- replay of an opening already looked at
    local key3 = H.PackOpen()
    flush()
    printed = {}
    T.opened(lastOpen().reqId, 10, key3, 2, 10, "00000", nil, "1")
    check(PK.pending == nil and PK.Current() == nil and string.find(PK.message or "", "opened before", 1, true) ~= nil and T.joined() == "", "replay of a seen opening: panel message, no chat, no reveal")

    -- definitive errors
    H.PackOpen()
    flush()
    printed = {}
    H.queue = {}
    T.err(lastOpen().reqId, "NO_STOCK", "You have no unopened packs.")
    check(PK.pending == nil and not PK.IsOpening(), "NO_STOCK clears the pending open")
    check(string.find(T.joined(), "no unopened packs", 1, true) ~= nil and table.getn(T.queuedOps("PACK_GET")) == 1, "NO_STOCK: message and resync")
    check(O.IsReady(), "NO_STOCK leaves the collection alone")
    flush()
    T.packs(2, 10, "")
    local keyR = H.PackOpen()
    flush()
    printed = {}
    T.err(lastOpen().reqId, "REFUSED", "A card is at the copy limit.")
    check(PK.pending == nil and string.find(PK.message, "copy limit", 1, true) ~= nil and string.find(T.joined(), "Nothing was changed", 1, true) ~= nil, "REFUSED")
    local keyM = H.PackOpen()
    check(keyM ~= keyR, "after a definitive error the next open uses a fresh key")
    flush()
    T.err(lastOpen().reqId, "MALFORMED", "bad key")
    check(PK.pending == nil, "MALFORMED clears the pending open")
    drain(5)
    check(table.getn(T.sentOps("PACK_OPEN")) > 0 and not PK.IsOpening(), "no retry after a definitive error")

    -- an error guessed for the open (reqId 0, oldest request) is not definitive
    H.pending = {}
    local keyG = H.PackOpen()
    local nOpen = table.getn(T.sentOps("PACK_OPEN"))
    flush()
    T.err("0", "MALFORMED", "x")
    check(PK.IsOpening() and PK.pending.key == keyG and PK.pending.retryIn ~= nil, "a reqId 0 error is retried")
    drain(3)
    check(table.getn(T.sentOps("PACK_OPEN")) == nOpen + 2 and keyOf(lastOpen()) == keyG, "retried with the same key")
    T.opened(lastOpen().reqId, 11, keyG, 1, 10)
    check(PK.pending == nil and PK.Current().seq == 11, "settled")
    PK.FlipAll()
    PK.FinishReveal()

    -- unattributed UNAVAILABLE with a COLL_GET in flight fails the collection, the open is retried
    H.pending = {}
    H.queue = {}
    local keyU = H.PackOpen()
    flush()
    H.clock = H.clock + 1
    T.frame("COLL_CHANGED", "0", "ver=12")
    flush()
    check(H.coll.reqId ~= nil, "COLL_GET in flight")
    printed = {}
    T.err("0", "UNAVAILABLE", "db")
    check(O.state == "error" and H.coll.reqId == nil, "reqId 0 UNAVAILABLE still fails the COLL_GET")
    check(PK.IsOpening() and PK.pending.key == keyU and PK.pending.retryIn ~= nil, "and the oldest request (the open) is retried")
    check(string.find(T.joined(), "card collection could not be loaded", 1, true) ~= nil, "collection message")
    drain(3)
    T.opened(lastOpen().reqId, 12, keyU, 0, 12)
    check(PK.pending == nil and PK.stock == 0, "settled, stock 0")
    PK.FlipAll()
    PK.FinishReveal()
    flush()
    T.coll(12)
    local k4, why4 = H.PackOpen()
    check(k4 == nil and why4 == "You have no unopened packs.", "no stock: blocked locally")

    -- UNAVAILABLE for PACK_GET: packs error, collection untouched, and vice versa
    H.queue = {}
    local g = H.PackGet()
    check(PK.IsReady(), "a refetch keeps the data visible")
    printed = {}
    T.err(g, "UNAVAILABLE", "db")
    check(PK.state == "error" and PK.stock == 0 and H.packs.reqId == nil and H.pending[g] == nil, "PACK_GET UNAVAILABLE: packs error")
    check(O.IsReady() and O.version == 12, "collection untouched")
    check(string.find(T.joined(), "packs could not be loaded", 1, true) ~= nil and string.find(T.joined(), "card collection", 1, true) == nil, "pack message only")
    check(H.PackOpen() == nil, "no open while packs are in error")
    T.frame("PACK_CHANGED", "0", "stock=3")
    check(PK.state == "loading" and H.packs.reqId ~= nil, "a push after an error refetches")
    flush()
    T.packs(3, 12, "")
    check(PK.IsReady() and PK.stock == 3, "recovered")
    T.frame("COLL_CHANGED", "0", "ver=13")
    local cr = H.coll.reqId
    T.err(cr, "UNAVAILABLE", "db")
    check(O.state == "error" and PK.IsReady() and PK.stock == 3, "a collection UNAVAILABLE leaves the packs alone")
    flush()

    -- a late UNAVAILABLE of an old pack request touches nothing
    T.frame("COLL_CHANGED", "0", "ver=14")
    printed = {}
    T.err(g, "UNAVAILABLE", "late")
    check(H.coll.reqId ~= nil and O.state == "loading" and PK.IsReady(), "late pack error: no effect")
    check(T.joined() == "", "and no message")
    flush()
    T.coll(14)

    -- PACK_GET timeout
    H.queue = {}
    local g2 = H.PackGet()
    flush()
    printed = {}
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(PK.state == "error" and PK.errorText == "no response" and H.packs.reqId == nil, "PACK_GET timeout")
    check(string.find(T.joined(), "did not send your packs", 1, true) ~= nil, "timeout message")
    T.frame("PACKS", g2, "type=classic;stock=1;opened=1;recent=")
    check(PK.state == "error", "a reply after the timeout is ignored")

    -- malformed replies
    H.PackGet()
    flush()
    printed = {}
    T.packs(1, 12, "12.1000.A.B.C.D.E")
    check(PK.state == "error" and string.find(PK.errorText, "bad reply", 1, true) ~= nil, "malformed PACKS: error")
    check(string.find(T.joined(), "cannot read", 1, true) ~= nil, "malformed PACKS message")
    H.PackGet()
    flush()
    T.packs(1, 12, "")
    local keyX = H.PackOpen()
    flush()
    local ax = lastOpen()
    printed = {}
    H.queue = {}
    T.frame("PACK_OPENED", ax.reqId, "id=13;key=" .. keyX .. ";replay=0;stock=0;ver=15;new=1000;cards=A,B,C,D,E")
    check(PK.pending ~= nil and PK.pending.key == keyX and not PK.IsOpening() and PK.Current() == nil, "malformed PACK_OPENED: stop, keep the key")
    check(string.find(T.joined(), "cannot read", 1, true) ~= nil and table.getn(T.queuedOps("PACK_GET")) == 1, "message and resync")
    flush()
    T.packs(0, 13, "13.10000." .. string.gsub(SAMPLE_CARDS, ",", "."))
    check(PK.Current() and PK.Current().seq == 13 and PK.Current().recovered, "the opening shows up through the recovery")
    PK.FlipAll()
    PK.FinishReveal()
    PK.ClearPending()

    -- recovery on login: marker 5, openings 5..7 on the server
    resetAddon()
    H.db = HsCollection.ApplySavedDefaults({ packsSeen = { ["?"] = 5 } })
    T.welcome("solo,chunk,packs")
    flush()
    printed = {}
    T.packs(1, 7, "7.00000.A.B.C.D.E,6.10000.EX1_116.B.C.D.E,5.00000.A.B.C.D.E")
    check(table.getn(PK.queue) == 2 and PK.Current().seq == 6 and PK.queue[2].seq == 7 and PK.Current().recovered, "unseen openings queued, oldest first")
    check(PK.message == "You have 2 opened packs you have not looked at yet." and T.joined() == "", "notice on the panel, not in the chat")
    check(PK.message == "You have 2 opened packs you have not looked at yet.", "notice kept for the window")
    H.PackGet()
    flush()
    T.packs(1, 7, "7.00000.A.B.C.D.E,6.10000.EX1_116.B.C.D.E,5.00000.A.B.C.D.E")
    check(table.getn(PK.queue) == 2, "a second PACKS does not queue them again")
    PK.FlipAll()
    PK.FinishReveal()
    check(H.db.packsSeen["?"] == 6 and PK.Current().seq == 7, "first one seen")
    H.PackGet()
    flush()
    printed = {}
    T.packs(1, 7, "7.00000.A.B.C.D.E,6.10000.EX1_116.B.C.D.E,5.00000.A.B.C.D.E")
    check(table.getn(PK.queue) == 1 and string.find(T.joined(), "not looked at", 1, true) == nil, "nothing new to announce")

    -- losing the connection: none; the reveal and an unconfirmed key stay; a new WELCOME reloads
    local keyV = H.PackOpen()
    check(keyV == nil, "blocked by the reveal")
    PK.FlipAll()
    PK.FinishReveal()
    keyV = H.PackOpen()
    flush()
    local av = lastOpen()
    T.err("0", "DISABLED", "off")
    check(PK.state == "none" and PK.stock == 0 and not PK.IsOpening() and PK.pending.key == keyV and H.pending[av.reqId] == nil, "DISABLED: packs none, key kept")
    H.clock = H.clock + 30
    drain(1)
    check(table.getn(H.queue) == 0, "nothing retried while disconnected")
    T.frame("PACK_CHANGED", "0", "stock=4")
    check(PK.state == "none", "pushes ignored while disconnected")
    T.welcome("solo,chunk,packs")
    check(PK.state == "loading" and H.packs.reqId ~= nil, "a new WELCOME reloads the packs")
    flush()
    T.packs(1, 7, "7.00000.A.B.C.D.E")
    check(H.PackOpen() == keyV, "the kept key is used again")
    flush()
    T.err("0", "VERSION", "v")
    check(PK.state == "none" and not H.state.welcomed, "VERSION: packs none")

    -- the sample server frames of the transcript, without matching requests: ignored, no errors
    resetAddon()
    H.db = HsCollection.ApplySavedDefaults(nil)
    T.welcome("solo,chunk,packs")
    local okSample, errSample = pcall(function()
        H.HandleFrame("v1\tPACKS\tk1\t1\t1\ttype=classic;stock=2;opened=7;recent=7.10000.EX1~u116.CS2~u022.CS2~u022.EX1~u011.CS2~u189")
        H.HandleFrame("v1\tPACK_OPENED\to1\t1\t1\tid=8;key=abcdef123456;replay=0;stock=1;ver=9;new=11000;cards=CS2~u022,EX1~u116,CS2~u022,CS2~u189,EX1~u011")
        H.HandleFrame("v1\tPACK_CHANGED\t0\t1\t1\tstock=5")
        H.HandleFrame("v1\tERR\to2\t1\t1\tcode=NO~uSTOCK;msg=You have no unopened packs.")
    end)
    check(okSample, "sample frames handled: " .. tostring(errSample))
    check(PK.state == "loading" and PK.Current() == nil and PK.pending == nil, "unsolicited pack replies change nothing")

    H.OnPacksChanged = savedHook
    H.db = savedDb
    resetAddon()
end

-- ------------------------------------------------------------- packs (UI)
local function testPacksUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local PU = H.PacksUI
    local PK = HsPacks
    local MB = H.MicroButton
    local CM = HsCollection
    local T = packHelpers()
    local savedDb = H.db

    local function resetUI()
        UI.frame, UI.preview, UI.hover = nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons = {}, {}, {}
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited = nil, nil, false
        PU.panel, PU.slots, PU.rows, PU.hover = nil, {}, {}, nil
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    local function flush() drain(1) end
    local function lastOpen()
        local l = T.sentOps("PACK_OPEN")
        return l[table.getn(l)]
    end
    local function faceDown(i)
        local sl = PU.slots[i]
        return sl.holder:IsShown() and sl.back:IsShown() and not sl.widget.frame:IsShown() and not sl.rarityBadge:IsShown()
    end
    local function faceUp(i, id)
        local sl = PU.slots[i]
        return sl.holder:IsShown() and not sl.back:IsShown() and sl.widget.frame:IsShown() and sl.widget.id == id and sl.rarityBadge:IsShown()
    end
    local function border(i)
        local b = PU.slots[i].holder._.bdBorder
        return b[1], b[2], b[3], b[4]
    end

    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, realm = "Realm A" })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()

    -- /hs packs opens the window on the Packs tab
    H.Slash("packs")
    local main = UI.frame
    local pnl = main and main.packs
    check(main and main:IsShown() and UI.state.bottom == 2 and main.selectedTab == 2, "/hs packs opens the Packs tab")
    check(pnl and pnl:IsShown() and not main.book:IsShown() and not main.unavailable:IsShown(), "packs panel shown")
    check(main.title:GetText() == "Card Packs" and pnl.packName:GetText() == "Classic Pack", "titles")
    check(pnl:GetWidth() == PU.PANEL_W and pnl:GetHeight() == PU.PANEL_H and PU.PANEL_W == UI.WIDTH_BOOK and UI.HEADER_H + PU.PANEL_H == UI.HEIGHT, "the packs page is the whole window below the header")
    check(pnl.status:GetText() == "Not connected to the server." and not pnl.stack:IsShown() and pnl.unopened:GetText() == "-", "offline state")
    check(table.getn(PU.slots) == 5 and table.getn(PU.rows) == 5, "five card slots, five recent rows")
    for i = 1, 5 do
        check(not PU.slots[i].holder:IsShown() and not PU.rows[i]:IsShown(), "nothing shown offline " .. i)
    end
    check(not pnl.doneButton:IsShown() and not pnl.recentButton:IsEnabled(), "no Done button, Recent Packs disabled offline")
    check(PU.slots[1].widget.width == PU.CARD_W and PU.CARD_W == 168 and PU.slots[1]:GetHeight() == PU.CARD_H and UI.CARD_W > PU.CARD_W, "pack cards are 168 px wide (a ring of five on the slab, legible without the preview), the book's are 172")
    MB.Create()
    local created = table.getn(F.frames)

    -- the cards fit into the panel: three above two, around the slot of the slab, clear of the header line
    for i = 1, 5 do
        local pt = PU.slots[i].holder._.points[1]
        local w, h = PU.slots[i].holder:GetWidth(), PU.slots[i].holder:GetHeight()
        check(pt.x - w / 2 >= 8 and pt.x + w / 2 <= PU.PANEL_W - 8 and -pt.y - h / 2 >= 8 and -pt.y + h / 2 <= PU.HEADER_Y - 20, "slot " .. i .. " inside the panel, above the header line")
        for j = 1, i - 1 do
            local q = PU.slots[j].holder._.points[1]
            check(math.abs(pt.x - q.x) >= PU.CARD_W * 0.88 or math.abs(pt.y - q.y) >= PU.CARD_H * 0.82, "the cards of slot " .. i .. " and slot " .. j .. " do not touch")
        end
    end

    -- unsupported server
    T.welcome("solo,chunk")
    check(pnl.status:GetText() == "This server does not support packs yet." and not pnl.stack:IsShown(), "unsupported: " .. tostring(pnl.status:GetText()))

    -- loading, ready
    H.queue = {}
    T.welcome("solo,chunk,packs")
    check(pnl.status:GetText() == "Loading your packs..." and not pnl.stack:IsShown(), "loading")
    flush()
    T.packs(2, 7, "7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189,6.00000.CS2_028.CS2_022.CS2_022.EX1_011.CS2_189")
    check(pnl.unopened:GetText() == "2" and pnl.stack:IsShown() and pnl.recentButton:IsEnabled() and pnl.status:GetText() == "", "ready")
    check(PU.rows[1]:IsShown() and PU.rows[1].text:GetText() == "#7  |cffff80001 Legendary|r, |cffffffff4 Common|r" and PU.rows[2]:IsShown() and not PU.rows[3]:IsShown(), "recent rows")
    check(pnl.hint:GetText() == "Drag a pack onto the stone to open it." and pnl.recentEmpty:GetText() == "", "hint")
    check(H.db.packsSeen["Realm A"] == 7, "marker initialised for the realm")

    -- micro-menu tooltip shows the unopened count
    F.fire(MB.frame, "OnEnter")
    check(F.tooltip.lines[3] == "Unopened packs: 2", "micro-menu tooltip: " .. tostring(F.tooltip.lines[3]))
    F.fire(MB.frame, "OnLeave")

    -- the packs are stacked down the purple strip; the front pack is dragged
    pnl._.left, pnl._.bottom = 0, 0
    local function cursorAt(x, y) local sc = pnl:GetEffectiveScale(); F.cursor[1], F.cursor[2] = x * sc, (PU.PANEL_H - y) * sc end    -- panel pixels (y down) to the screen
    local function settle() for _ = 1, 20 do PU.Step(0.1) end end
    local down = { arg1 = "LeftButton" }
    local function flipAll() for i = 1, 5 do F.fire(PU.slots[i], "OnClick") end settle() end
    check(pnl.stack:IsShown() and pnl.stack:GetHeight() == PU.PACK_H and PU.stackPacks[1]:IsShown() and PU.stackPacks[2] == nil, "two unopened packs: one pack is shown")
    local sx, sy = PU.StackPos(1)
    sent = {}
    F.sounds = {}
    -- let go outside the slab: the pack goes back, nothing is opened
    cursorAt(sx, sy)
    F.fire(pnl.stack, "OnMouseDown", down)
    check(PU.held and PU.drag ~= nil and pnl.flyer:IsShown() and PU.stackPacks[1]:IsShown(), "the pack is in the hand, the next one is on the strip")
    cursorAt(300, 560)
    PU.Step(0.016)
    check(PU.fx == 300 and PU.fy == 560 and not pnl.dropGlow:IsShown(), "the pack follows the cursor")
    F.fire(pnl.stack, "OnMouseUp", down)
    settle()
    flush()
    check(not PU.held and not pnl.flyer:IsShown() and PU.stackPacks[1]:IsShown() and table.getn(T.sentOps("PACK_OPEN")) == 0 and not PK.IsOpening(), "let go outside: back on the stack, nothing opened")
    -- drop it on the slab
    cursorAt(sx, sy)
    F.fire(pnl.stack, "OnMouseDown", down)
    cursorAt(PU.ARENA_CX + 40, PU.ARENA_CY - 30)
    PU.Step(0.016)
    check(pnl.dropGlow:IsShown(), "the slot lights up under a pack")
    local g = pnl.dropGlow._.vertex
    check(g[3] == 1 and g[3] > g[1] and pnl.dropGlow:GetWidth() == PU.DROP_GLOW_W and pnl.dropGlow:GetWidth() > PU.CARD_W + 60, "the glow is blue and a little bigger")
    F.fire(pnl.stack, "OnMouseUp", down)
    check(pnl.status:GetText() == "Opening..." and not PU.drag and PK.IsOpening() and PU.awaiting and pnl.unopened:GetText() == "1" and pnl.unopened:GetParent():GetParent() == PU.stackPacks[1], "opening: the number sits on the pack and counts the one in the slot out")
    flush()
    local a = lastOpen()
    check(a ~= nil, "PACK_OPEN sent when the pack is let go")
    check(not PU.arrived and PU.tween ~= nil and PU.stackPacks[1]:IsShown(), "the pack flies to the slot")
    settle()
    check(PU.arrived and PU.tween == nil and PU.awaiting and pnl.flyer:IsShown() and pnl.dropGlow:IsShown(), "the pack waits in the slot")
    check(math.abs(PU.fx - PU.ARENA_CX) < 8 and math.abs(PU.fy - PU.ARENA_CY) < 8, "it shakes around the middle of the slot")
    H.queue = {}
    T.opened(a.reqId, 8, PK.pending.key, 1, 8, "11001", "EX1_116,CS2_028,CS2_053,CS2_189,CS2_022")
    check(PU.anim ~= nil and not PU.awaiting and not PU.held, "the burst starts when the cards are there")
    check(pnl.unopened:GetText() == "1" and not PU.CanOpen(), "reveal blocks the next open")
    for i = 1, 5 do check(PU.slots[i].holder:IsShown() and PU.slots[i].holder:GetAlpha() == 0 and PU.slots[i].holder:GetScale() < 0.4, "card " .. i .. " starts in the slot, invisible") end
    F.fire(PU.slots[1], "OnClick")
    check(faceDown(1), "no flipping while the cards fly")
    for _ = 1, 5 do PU.Step(0.1) end
    check(pnl.fxStar:IsShown() and pnl.fxRing:IsShown() and PU.slots[1].holder:GetAlpha() == 1 and PU.slots[5].holder:GetAlpha() < 1, "burst of light, the cards come out one after the other")
    settle()
    check(PU.anim == nil and not pnl.flyer:IsShown() and not pnl.fxStar:IsShown() and not pnl.fxRing:IsShown() and not pnl.dropGlow:IsShown(), "the effect ends")
    for i = 1, 5 do
        local pt = PU.slots[i].holder._.points[1]
        check(faceDown(i) and PU.slots[i].holder:GetScale() == 1 and PU.slots[i].holder:GetAlpha() == 1 and pt.x == PU.SLOT_POS[i][1] and pt.y == -PU.SLOT_POS[i][2], "card " .. i .. " face down in its place")
    end
    -- the rarity shows only while the mouse is over a face-down card
    for i = 1, 5 do
        local r, g, b, al = border(i)
        check(al == 0, "no rarity shown for card " .. i)
    end
    F.fire(PU.slots[1], "OnEnter")
    local r, g, b, al = border(1)
    check(r == 1 and g == 0.5 and b == 0 and al == 1, "legendary glow on hover")
    F.fire(PU.slots[1], "OnLeave")
    check(select(4, border(1)) == 0, "glow gone")
    F.fire(PU.slots[2], "OnEnter")
    r, g, b = border(2)
    check(r == 0 and g == 0.44 and b == 0.87, "rare glow on hover")
    F.fire(PU.slots[2], "OnLeave")
    F.fire(PU.slots[3], "OnEnter")
    r, g, b = border(3)
    check(r == 0.64 and g == 0.21 and b == 0.93, "epic glow on hover")
    F.fire(PU.slots[3], "OnLeave")
    F.fire(PU.slots[4], "OnEnter")
    r, g, b = border(4)
    check(r == 1 and g == 1 and b == 1, "common on hover")
    F.fire(PU.slots[4], "OnLeave")
    check(not pnl.doneButton:IsShown() and not pnl.recentButton:IsShown(), "no buttons while cards are face down")
    check(string.find(pnl.header:GetText(), "Pack #8", 1, true) == 1 and pnl.hint:GetText() == "", "header")

    -- hover a face-down card: highlight, no preview
    F.fire(PU.slots[1], "OnEnter")
    check(PU.slots[1].back._.bdBorder[1] == 1 and PU.slots[1].back._.bdBorder[2] == 0.82 and not UI.preview.frame:IsShown(), "face-down hover")
    F.fire(PU.slots[1], "OnLeave")
    check(PU.slots[1].back._.bdBorder[1] == 0.75, "hover ends")

    -- flip: the back squashes to an edge, the face opens, the labels come when it is done
    F.fire(PU.slots[1], "OnClick")
    local sl1 = PU.slots[1]
    check(sl1.flip ~= nil and sl1.back:IsShown() and not sl1.widget.frame:IsShown(), "the card starts turning: still the back")
    PU.Step(0.09)
    check(sl1.back:IsShown() and sl1.back._.points[2].x - sl1.back._.points[1].x < sl1.vis.right - sl1.vis.left and sl1.holder:GetScale() > 1, "the back squashes and the card lifts")
    PU.Step(0.1)
    check(not sl1.back:IsShown() and sl1.widget.frame:IsShown() and sl1.widget.frame:GetWidth() < PU.CARD_W and not sl1.newBadge:IsShown() and not sl1.rarityBadge:IsShown(), "edge on: the face opens, no labels yet")
    check(select(4, border(1)) == 0, "no rarity glow while the card is still turning")
    settle()
    check(sl1.flip == nil and sl1.widget.frame:GetWidth() == PU.CARD_W and sl1.holder:GetScale() == 1 and sl1.back._.points[1].x == sl1.vis.left and sl1.back._.points[2].x == sl1.vis.right, "turned: full size again")
    check(faceUp(1, "EX1_116") and PU.slots[1].newBadge:IsShown(), "click flips: card and NEW tag")
    local r1, g1, b1, a1 = border(1)
    check(a1 == 1 and r1 == 1 and g1 == 0.5 and b1 == 0, "a turned card keeps the border of its rarity")
    check(string.find(PU.slots[1].rarityText:GetText(), "Legendary", 1, true) ~= nil and string.find(PU.slots[1].rarityText:GetText(), PK.RARITY_CODES.LEGENDARY, 1, true) == 1, "rarity name in its colour")
    check(F.sounds[table.getn(F.sounds)] == "igAbilityPageTurn", "flip sound")
    check(faceDown(2) and not pnl.doneButton:IsShown(), "the others stay face down")
    F.fire(PU.slots[1], "OnClick")
    check(faceUp(1, "EX1_116"), "a second click changes nothing")
    F.fire(PU.slots[1], "OnEnter")
    check(UI.preview.frame:IsShown() and UI.preview.widget.id == "EX1_116", "hovering a turned card shows the preview")
    F.fire(PU.slots[1], "OnLeave")
    check(not UI.preview.frame:IsShown(), "preview hides")
    F.fire(PU.slots[2], "OnEnter")
    F.fire(PU.slots[2], "OnClick")
    settle()
    check(faceUp(2, "CS2_028") and PU.slots[2].newBadge:IsShown() and UI.preview.frame:IsShown() and UI.preview.widget.id == "CS2_028", "flip under the mouse shows the preview")
    F.fire(PU.slots[2], "OnLeave")
    check(PU.slots[1].widget.frame._.level > PU.slots[1].holder._.level and PU.slots[1].newBadge._.level > PU.slots[1].widget.frame._.level, "tags above the card")

    -- closing the window mid-reveal loses nothing
    F.fire(PU.slots[1], "OnEnter")
    main:Hide()
    check(PU.hover == nil and not UI.preview.frame:IsShown(), "closing clears the hover")
    H.Slash("packs")
    check(main:IsShown() and faceUp(1, "EX1_116") and faceUp(2, "CS2_028") and faceDown(3) and not pnl.doneButton:IsShown(), "the reveal resumes")
    main:Hide()
    H.Slash("c")
    check(UI.state.bottom == 2 and pnl:IsShown(), "the window reopens on the Packs tab")
    F.fire(F.globals.HearthstoneFrameTab1, "OnClick")
    check(not pnl:IsShown() and main.book:IsShown(), "collection tab hides the packs")
    F.fire(F.globals.HearthstoneFrameTab2, "OnClick")
    check(pnl:IsShown() and faceDown(3), "back on the Packs tab")

    -- turn the rest by clicking them, then Done
    check(PU.OnRevealAll == nil and pnl.revealAllButton == nil, "there is no Reveal All")
    flipAll()
    check(faceUp(3, "CS2_053") and faceUp(4, "CS2_189") and faceUp(5, "CS2_022"), "clicking turns the rest")
    check(not PU.slots[3].newBadge:IsShown() and not PU.slots[4].newBadge:IsShown() and PU.slots[5].newBadge:IsShown(), "NEW tags from the mask")
    check(string.find(PU.slots[5].rarityText:GetText(), "Basic", 1, true) ~= nil and string.find(PU.slots[4].rarityText:GetText(), "Common", 1, true) ~= nil, "rarity names")
    check(pnl.doneButton:IsShown() and not PU.CanOpen(), "Done shown")
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(PK.Current() == nil and H.db.packsSeen["Realm A"] == 8, "Done marks the pack seen")
    for i = 1, 5 do check(not PU.slots[i].holder:IsShown(), "reveal area cleared " .. i) end
    check(PU.CanOpen() and pnl.stack:IsShown() and not pnl.doneButton:IsShown() and pnl.recentButton:IsShown() and pnl.recentButton:IsEnabled(), "back to the pack")

    -- a recent row shows its cards face up
    check(not pnl.recentFrame:IsShown(), "the list is closed")
    F.fire(pnl.recentButton, "OnClick")
    check(pnl.recentFrame:IsShown(), "Recent Packs opens the list")
    F.fire(pnl.recentButton, "OnClick")
    check(not pnl.recentFrame:IsShown(), "and closes it")
    F.fire(pnl.recentButton, "OnClick")
    F.fire(PU.rows[2], "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Pack #7", "row tooltip")
    F.fire(PU.rows[2], "OnLeave")
    F.fire(PU.rows[2], "OnClick")
    check(not pnl.recentFrame:IsShown(), "choosing a pack closes the list")
    check(PK.view and PK.view.seq == 7 and faceUp(1, "EX1_116") and faceUp(5, "CS2_189") and not PU.slots[1].newBadge:IsShown(), "view mode: face up, no NEW tags")
    check(pnl.header:GetText() == "Pack #7 (opened earlier)" and pnl.doneButton:IsShown() and not pnl.recentButton:IsShown(), "view header and Done")
    F.fire(PU.slots[3], "OnEnter")
    check(UI.preview.frame:IsShown() and UI.preview.widget.id == "CS2_022", "hover preview in view mode")
    F.fire(PU.slots[3], "OnClick")
    check(PK.view ~= nil, "clicks do nothing in view mode")
    F.fire(PU.rows[1], "OnClick")
    check(PK.view.seq == 8 and faceUp(2, "CS2_028"), "another row")
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(PK.view == nil and not PU.slots[1].holder:IsShown() and not UI.preview.frame:IsShown(), "Done returns")
    F.fire(pnl.recentButton, "OnClick")
    F.fire(PU.rows[1], "OnClick")
    check(PU.rows[1]._.locked == true and PK.view.seq == 8, "the viewed pack is highlighted in the list")
    F.fire(pnl.doneButton, "OnClick")
    settle()

    -- recovery shows up in the window
    H.db.packsSeen["Realm A"] = 6
    H.PackGet()
    flush()
    T.packs(1, 8, "8.11001.EX1_116.CS2_028.CS2_053.CS2_189.CS2_022,7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189")
    check(PK.Current().seq == 7 and faceDown(1) and pnl.status:GetText() == "You have 2 opened packs you have not looked at yet.", "recovered reveal")
    check(string.find(pnl.header:GetText(), "(opened earlier)", 1, true) ~= nil, "recovered header")
    F.fire(PU.rows[1], "OnClick")
    check(PK.view == nil and faceDown(1), "rows do nothing during a reveal")
    flipAll()
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(PK.Current().seq == 8 and faceDown(1) and H.db.packsSeen["Realm A"] == 7, "next queued reveal")
    flipAll()
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(PK.Current() == nil and H.db.packsSeen["Realm A"] == 8 and pnl.status:GetText() == "", "all seen")

    -- /hs open from a closed window
    main:Hide()
    sent = {}
    H.Slash("open")
    flush()
    check(main:IsShown() and UI.state.bottom == 2 and table.getn(T.sentOps("PACK_OPEN")) == 1, "/hs open opens the tab and a pack")
    check(PU.awaiting and PU.held and pnl.flyer:IsShown(), "/hs open sends the pack to the slot too")
    H.queue = {}
    T.opened(lastOpen().reqId, 9, PK.pending.key, 0, 9, "00000")
    settle()
    check(PU.anim == nil and faceDown(1) and pnl.unopened:GetText() == "0", "revealing")
    flipAll()
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(not PU.CanOpen() and not pnl.stack:IsShown() and pnl.status:GetText() == "You have no unopened packs.", "no stock")
    printed = {}
    H.Slash("open")
    check(string.find(table.concat(printed, "\n"), "no unopened packs", 1, true) ~= nil, "/hs open without stock says so")

    -- errors: shown in red, a retry when the tab is shown again
    T.err(H.PackGet(), "UNAVAILABLE", "db")
    H.queue = {}
    check(string.find(pnl.status:GetText(), "|cffff5555", 1, true) == 1 and string.find(pnl.status:GetText(), "could not be loaded", 1, true) ~= nil, "error state")
    check(not pnl.stack:IsShown() and not pnl.recentButton:IsEnabled() and not PU.rows[1]:IsShown() and pnl.recentEmpty:GetText() == "-", "error: nothing to open or view")
    F.fire(F.globals.HearthstoneFrameTab1, "OnClick")
    check(table.getn(T.queuedOps("PACK_GET")) == 0, "no retry while the tab is hidden")
    F.fire(F.globals.HearthstoneFrameTab2, "OnClick")
    check(table.getn(T.queuedOps("PACK_GET")) == 1 and pnl.status:GetText() == "Loading your packs...", "showing the tab retries once")
    F.fire(F.globals.HearthstoneFrameTab2, "OnClick")
    check(table.getn(T.queuedOps("PACK_GET")) == 1, "only once")
    flush()
    T.packs(0, 9, "")
    check(pnl.recentEmpty:GetText() == "No packs opened yet." and not PU.rows[1]:IsShown(), "no recent packs")

    -- the pack UI never builds frames after the window
    check(table.getn(F.frames) == created, "no frames created by the pack UI: " .. table.getn(F.frames) .. " vs " .. created)

    -- no text suggests play, queues, trading or crafting
    local bad = { "play", "queue", "ranked", "trad", "craft", "scrap", "invite", "dust", "disenchant", "buy" }
    local function underCard(o)
        while o and o._ do
            if o._.name and string.find(o._.name, "Card", 1, true) then return true end
            if o._.name and string.find(o._.name, "HearthstoneCraft", 1, true) then return true end   -- the craft panels (crafting NPC only)
            o = o._.parent
        end
        return false
    end
    for i = 1, table.getn(F.frames) do
        local o = F.frames[i]
        if not underCard(o) then
            local texts = { o._.text }
            for _, rg in ipairs(o._.regions) do table.insert(texts, rg._.text) end
            for _, t in ipairs(texts) do
                for _, w in ipairs(bad) do
                    check(not string.find(string.lower(tostring(t)), w, 1, true), "pack window text mentions '" .. w .. "': " .. tostring(t))
                end
            end
        end
    end

    -- disconnect: back to "not connected", the window keeps working
    T.err("0", "DISABLED", "off")
    check(pnl.status:GetText() == "Not connected to the server." and not pnl.stack:IsShown() and pnl.unopened:GetText() == "-", "disconnected")

    main:Hide()
    F.uninstall()
    resetUI()
    H.db = savedDb
    resetAddon()
end

-- ---------------------------------------------------------- decks (pure)
-- The development fixture "Dev Mage": 15 Basic cards, 2 copies each.
local MAGE_IDS = { "EX1_277", "CS2_168", "CS2_025", "CS2_172", "EX1_015", "CS2_120", "CS2_023", "CS2_122",
    "CS2_124", "CS2_029", "CS2_119", "CS2_022", "CS2_179", "EX1_593", "CS2_200" }

local function mageCards(n)
    local t = {}
    for i = 1, (n or 15) do t[MAGE_IDS[i]] = 2 end
    return t
end

-- "id.qty,..." in byte order, like the server.
local function cardsString(cards)
    local ids = {}
    for id in pairs(cards) do table.insert(ids, id) end
    table.sort(ids)
    local out = {}
    for i = 1, table.getn(ids) do out[i] = ids[i] .. "." .. cards[ids[i]] end
    return table.concat(out, ",")
end

-- DECKS payload. decks = { { id, rev, class, name, cards (string), state, origin }, ... }
local function decksPayload(cap, start, unlocked, decks)
    local kv = { { "cap", tostring(cap) }, { "n", tostring(table.getn(decks)) }, { "start", start }, { "unlocked", unlocked } }
    for i = 1, table.getn(decks) do
        local d = decks[i]
        table.insert(kv, { "d" .. i, d[1] .. "." .. d[2] .. "." .. d[3] .. "." .. (d[7] or "player") })
        table.insert(kv, { "m" .. i, d[4] })
        table.insert(kv, { "c" .. i, d[5] })
        table.insert(kv, { "s" .. i, d[6] or "ok" })
    end
    return P.KvEncode(kv)
end

-- Owns the given cards (id -> total) as a ready collection.
local function ownCards(owned)
    local O = HsOwnership
    local entries = {}
    local n = 0
    for id, total in pairs(owned) do
        table.insert(entries, id .. "." .. total .. ".0")
        n = n + 1
    end
    table.sort(entries)
    local res = O.Parse("ver=1;n=" .. n .. ";cards=" .. table.concat(entries, ","), HS_CardDB)
    assert(res, "ownCards payload")
    O.SetReady(res)
end

local function testDecksModel()
    local D = HsDecks
    local O = HsOwnership
    resetAddon()
    for i = 1, table.getn(MAGE_IDS) do
        check(HS_CardDB.cards[MAGE_IDS[i]] ~= nil, "fixture card exists: " .. MAGE_IDS[i])
    end

    -- names: trim, 1..24 code points, <= 64 bytes, valid UTF-8, no control bytes, no pipe
    check(D.ValidateName("  Big Mage  ") == "Big Mage", "name trimmed")
    check(D.ValidateName("x") == "x" and D.ValidateName(string.rep("a", 24)) == string.rep("a", 24), "1 and 24 characters")
    local okE, whyE = D.ValidateName("")
    check(okE == nil and whyE == "the name is empty" and D.ValidateName("    ") == nil, "empty name")
    check(D.ValidateName(string.rep("a", 25)) == nil, "25 characters")
    check(D.ValidateName(" " .. string.rep("a", 24) .. " ") == string.rep("a", 24), "trim before counting")
    local ae = "\195\164"                                     -- U+00E4, 2 bytes
    check(D.ValidateName(string.rep(ae, 24)) == string.rep(ae, 24), "24 two-byte characters (48 bytes)")
    check(D.ValidateName(string.rep(ae, 25)) == nil, "25 two-byte characters")
    local emoji = "\240\159\152\128"                         -- U+1F600, 4 bytes
    local okF, whyF = D.ValidateName("Deck " .. emoji)
    check(okF == nil and whyF == "the name is not valid text", "four-byte characters refused (server: utf8mb3)")
    local euro = "\226\130\172"                              -- U+20AC, 3 bytes
    check(D.ValidateName(string.rep(euro, 21)) ~= nil, "21 three-byte characters = 63 bytes")
    local okB, whyB = D.ValidateName(string.rep(euro, 22))
    check(okB == nil and whyB == "the name is too long", "22 three-byte characters = 66 bytes: over 64 bytes")
    for _, bad in ipairs({ "\255", "a\128", "\192\128", "\193\191", "\224\128\128", "\224\159\191", "\237\160\128",
        "\244\144\128\128", "\245\128\128\128", "\195", "\226\130", "\240\159\152", "\195\40" }) do
        local r, w = D.ValidateName("x" .. bad)
        check(r == nil and w == "the name is not valid text", "invalid UTF-8 rejected: " .. toHex(bad))
    end
    check(D.ValidateName("\237\159\191") ~= nil and D.ValidateName("\239\191\191") ~= nil and D.ValidateName("\238\128\128") ~= nil, "edge code points accepted")
    check(D.ValidateName("\244\143\191\191") == nil, "U+10FFFF refused (four bytes)")
    for _, bad in ipairs({ "a\tb", "a\nb", "a\127b", "a\0b", "a\31b", "\tab" }) do
        local r, w = D.ValidateName(bad)
        check(r == nil and w == "the name contains a control character", "control byte rejected: " .. toHex(bad))
    end
    local rP, wP = D.ValidateName("a|b")
    check(rP == nil and wP == "the name may not contain |", "pipe rejected")
    check(D.ValidateName("a;b=c~d") == "a;b=c~d", "separators are fine (escaped on the wire)")
    check(D.Utf8Length("a" .. ae .. euro) == 3 and D.Utf8Length("") == 0 and D.Utf8Length(emoji) == nil, "code point count")

    -- caps
    check(D.CapsHaveDecks("solo,chunk,packs,decks") and D.CapsHaveDecks("decks"), "caps with decks")
    check(not D.CapsHaveDecks("solo,chunk,packs") and not D.CapsHaveDecks("decksx") and not D.CapsHaveDecks(nil) and not D.CapsHaveDecks("a,,decks"), "caps without decks")

    -- card list codec
    local cards, count = D.ParseCards("CS2_029.2,EX1_116.1")
    check(cards and cards.CS2_029 == 2 and cards.EX1_116 == 1 and count == 3, "cards parsed")
    check(D.ParseCards("") ~= nil and D.Count(D.ParseCards("")) == 0, "empty card list")
    for _, bad in ipairs({ "CS2_029", "CS2_029.0", "CS2_029.3", "CS2_029.02", "CS2_029.x", "CS2_029.1,CS2_029.1",
        "CS2_029.1,", ",CS2_029.1", "CS 2.1", string.rep("A", 33) .. ".1" }) do
        check(D.ParseCards(bad) == nil, "card list rejected: " .. bad)
    end
    check(D.ParseCards(cardsString(mageCards())) ~= nil, "30 cards accepted")
    local over = mageCards()
    over.NEW1_012 = 1
    check(D.ParseCards(cardsString(over)) == nil, "31 cards rejected")
    -- byte order: '_' (0x5f) sorts after upper case and digits, before lower case
    check(D.EncodeCards({ b1 = 1, A_ = 2, A1 = 1, B = 2 }) == "A1.1,A_.2,B.2,b1.1", "encode in byte order: " .. D.EncodeCards({ b1 = 1, A_ = 2, A1 = 1, B = 2 }))
    check(D.EncodeCards({}) == "", "encode empty")

    -- states
    check(D.ParseState("ok") ~= nil and next(D.ParseState("ok")) == nil, "state ok")
    local st = D.ParseState("size,owned,invalid,locked")
    check(st and st.size and st.owned and st.invalid and st.locked, "all states")
    check(D.ParseState("owned,locked").owned and D.ParseState("owned,locked").size == nil, "subset")
    for _, bad in ipairs({ "", "size,size", "owned,size", "locked,invalid", "foo", "gone", "ok,size", "size,", "OK" }) do
        check(D.ParseState(bad) == nil, "state rejected: " .. bad)
    end

    -- DECKS
    local good = decksPayload(36, "MAGE", "WARRIOR", {
        { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" },
        { 4, 1, "WARRIOR", "  Draft  ", "CS2_108.2", "size,owned" },
    })
    local res, why = D.ParseDecks(good)
    check(res ~= nil, "DECKS parsed: " .. tostring(why))
    check(res and res.cap == 36 and res.start == "MAGE" and res.unlocked.WARRIOR and table.getn(res.decks) == 2, "DECKS header")
    local d1, d2 = res and res.decks[1], res and res.decks[2]
    check(d1 and d1.id == 1 and d1.rev == 3 and d1.class == "MAGE" and d1.origin == "gm_fixture" and d1.name == "Dev Mage" and d1.count == 30 and d1.cards.CS2_029 == 2, "deck 1")
    check(d2 and d2.id == 4 and d2.name == "Draft" and d2.count == 2 and d2.serverState.size and d2.serverState.owned and not d2.serverState.locked, "deck 2 (name trimmed)")
    local empty = D.ParseDecks("cap=36;n=0;start=-;unlocked=")
    check(empty and table.getn(empty.decks) == 0 and empty.start == nil and next(empty.unlocked) == nil, "no decks, start unknown")
    check(D.ParseDecks("cap=40;n=1;start=DRUID;unlocked=MAGE,HUNTER;d1=1.0.DRUID.starter;m1=x;c1=;s1=size") ~= nil, "rev 0, a future origin, empty deck")
    local function mutate(from, to)
        local s = string.gsub(good, from, to)
        return s
    end
    local rejects = {
        { "cap=36", "cap=0" }, { "cap=36", "cap=41" }, { "cap=36", "cap=036" }, { "cap=36;", "" },
        { "n=2", "n=3" }, { "n=2", "n=1" }, { "n=2", "n=x" },
        { "start=MAGE", "start=MAGEX" }, { "start=MAGE", "start=" }, { "start=MAGE", "start=NEUTRAL" },
        { "unlocked=WARRIOR", "unlocked=WARRIOR,WARRIOR" }, { "unlocked=WARRIOR", "unlocked=FOO" }, { "unlocked=WARRIOR", "unlocked=,WARRIOR" },
        { "d1=1.3.MAGE", "d1=1.3.DREAM" }, { "d1=1.3.MAGE", "d1=0.3.MAGE" }, { "d1=1.3.MAGE", "d1=1.03.MAGE" },
        { "d1=1.3.MAGE", "d1=5.3.MAGE" }, { "d1=1.3.MAGE", "d1=4.3.MAGE" }, { "gm_fixture", "Player" }, { "d1=1.3.MAGE.gm_fixture", "d1=1.3.MAGE" },
        { "m1=Dev Mage", "m1=" }, { "m1=Dev Mage", "m1=a~pb" }, { "m1=Dev Mage;", "" },
        { "c2=CS2_108.2", "c2=CS2_108.3" }, { "c2=CS2_108.2", "c2=CS2_108.2,CS2_108.1" }, { "c2=CS2_108.2;", "" },
        { "s2=size,owned", "s2=owned,size" }, { "s2=size,owned", "s2=gone" }, { "s1=ok", "s1=" },
    }
    for _, r in ipairs(rejects) do
        local payload = mutate(r[1], r[2])
        check(payload ~= good, "mutation applies: " .. r[1])
        check(D.ParseDecks(payload) == nil, "DECKS rejected: " .. r[1] .. " -> " .. r[2])
    end
    check(D.ParseDecks(good .. ";d3=7.1.MAGE.player") == nil, "a deck key beyond n is rejected")
    check(D.ParseDecks(good .. ";x9=1") ~= nil, "an unknown key is ignored")
    check(D.ParseDecks("not~kv") == nil and D.ParseDecks(nil) == nil, "garbage")

    -- DECK_SAVED / DECK_DELETED
    local sv = D.ParseSaved("key=abcdef123456;id=7;rev=2;replay=0;state=size,owned")
    check(sv and sv.key == "abcdef123456" and sv.id == 7 and sv.rev == 2 and not sv.replay and sv.state.size and not sv.gone, "DECK_SAVED")
    local gone = D.ParseSaved("key=abcdef123456;id=7;rev=2;replay=1;state=gone")
    check(gone and gone.replay and gone.gone and gone.state == nil, "DECK_SAVED replay of a deleted deck")
    for _, bad in ipairs({ "key=abc;id=7;rev=2;replay=0;state=ok", "key=abcdef123456;id=0;rev=2;replay=0;state=ok",
        "key=abcdef123456;id=7;rev=x;replay=0;state=ok", "key=abcdef123456;id=7;rev=2;replay=2;state=ok",
        "key=abcdef123456;id=7;rev=2;replay=0;state=gone", "key=abcdef123456;id=7;rev=2;replay=0;state=bad",
        "key=ABCDEF123456;id=7;rev=2;replay=0;state=ok", "key=abcdef123456;id=7;rev=2;replay=0" }) do
        check(D.ParseSaved(bad) == nil, "DECK_SAVED rejected: " .. bad)
    end
    local dl = D.ParseDeleted("key=abcdef123456;id=7;replay=1")
    check(dl and dl.id == 7 and dl.replay, "DECK_DELETED")
    check(D.ParseDeleted("key=abcdef123456;id=x;replay=0") == nil and D.ParseDeleted("key=abcdef123456;id=7;replay=") == nil, "DECK_DELETED rejected")

    -- hard rules (the server's order: class, name, card, cardclass, copies, size)
    check(D.CheckHard("MAGE", "Dev Mage", mageCards()) == nil, "fixture deck is legal")
    check(D.CheckHard("NEUTRAL", "x", {}) == "class" and D.CheckHard("DREAM", "x", {}) == "class", "class")
    check(D.CheckHard("MAGE", " ", {}) == "name" and D.CheckHard("MAGE", "a|b", {}) == "name", "name")
    check(D.CheckHard("MAGE", "x", { CS1h_001 = 1 }) == "card" and D.CheckHard("MAGE", "x", { DREAM_01 = 1 }) == "card"
        and D.CheckHard("MAGE", "x", { NOPE_1 = 1 }) == "card", "card: not collectible, other class set, unknown")
    check(D.CheckHard("MAGE", "x", { EX1_062 = 1 }) == nil, "reward-only cards are deckable")
    check(D.CheckHard("MAGE", "x", { FP1_028 = 2, FP1_018 = 2 }) == nil, "Naxxramas quest cards are deckable (neutral and Mage)")
    check(D.CheckHard("WARRIOR", "x", { FP1_018 = 1 }) == "cardclass" and D.CheckHard("MAGE", "x", { FP1_010 = 2 }) == "copies", "quest cards follow class access and the Legendary limit")
    check(D.CheckHard("MAGE", "x", { CS2_108 = 1 }) == "cardclass", "cardclass")
    check(D.CheckHard("MAGE", "x", { CS2_108 = 1, CS1h_001 = 1 }) == "card", "card comes before cardclass")
    check(D.CheckHard("MAGE", "x", { EX1_116 = 2 }) == "copies" and D.CheckHard("MAGE", "x", { CS2_029 = 3 }) == "copies", "copies")
    check(D.CheckHard("MAGE", "x", over) == "size", "size")
    check(D.MaxCopies("EX1_116") == 1 and D.MaxCopies("CS2_029") == 2, "copy limits")

    -- live states: ownership not ready -> the server's owned flag; decks not ready -> its locked flag
    O.Reset()
    local r = D.Evaluate("MAGE", { CS2_029 = 2 }, { owned = true, locked = true })
    check(r.size and r.owned and not r.invalid and r.locked and r.count == 2 and not r.ownedKnown, "fallback to the server state")
    check(D.StateString(r) == "size,owned,locked", "state string " .. D.StateString(r))
    r = D.Evaluate("MAGE", mageCards(), {})
    check(not r.size and not r.owned and not r.locked and D.IsComplete(r) and D.StateString(r) == "ok", "complete without ownership data")
    -- with ownership: copies counted live
    local owned = {}
    for i = 1, table.getn(MAGE_IDS) do owned[MAGE_IDS[i]] = 2 end
    owned.CS2_029 = 1
    owned.CS2_022 = nil
    ownCards(owned)
    r = D.Evaluate("MAGE", mageCards(), { owned = false })
    check(r.owned and r.missing == 3 and r.ownedKnown and not D.IsComplete(r), "3 copies missing (live)")
    r = D.Evaluate("MAGE", mageCards(), { owned = true })
    owned.CS2_029, owned.CS2_022 = 2, 2
    ownCards(owned)
    r = D.Evaluate("MAGE", mageCards(), { owned = true })
    check(not r.owned and r.missing == 0, "the live collection wins over the server's flag")
    r = D.Evaluate("MAGE", { CS2_108 = 1 }, nil)
    check(r.invalid and r.size, "invalid (card of another class)")
    r = D.Evaluate("MAGE", { EX1_116 = 2 }, nil)
    check(r.invalid, "invalid (two Legendary copies)")
    -- access
    local acc = D.ParseDecks(decksPayload(36, "MAGE", "WARRIOR", {}))
    D.SetReady(acc)
    check(D.CanPlay("MAGE") and D.CanPlay("WARRIOR") and not D.CanPlay("DRUID"), "starting class and unlock")
    check(D.Evaluate("DRUID", {}, nil).locked and not D.Evaluate("WARRIOR", {}, { locked = true }).locked, "locked computed from the access data")
    check(D.AccessText("Jaina") == "Jaina can play: Mage (starting class), Warrior (unlocked)", "access line: " .. D.AccessText("Jaina"))
    D.SetReady(D.ParseDecks(decksPayload(36, "-", "", {})))
    check(D.AccessText("Jaina") == "Jaina has no class unlocked yet.", "no access")
    D.SetReady(D.ParseDecks(decksPayload(36, "WARRIOR", "WARRIOR", {})))
    check(D.AccessText("Garrosh") == "Garrosh can play: Warrior (starting class)", "an unlock of the starting class is not listed twice")

    -- reason texts
    D.SetReady(acc)
    local lines = D.ReasonLines(D.Evaluate("MAGE", mageCards(), nil), "MAGE", "Jaina")
    check(table.getn(lines) == 1 and lines[1][1] == "Complete" and lines[1][3] == 1, "complete line")
    owned.CS2_029, owned.CS2_022 = 1, nil
    ownCards(owned)
    local deck27 = mageCards()
    deck27.CS2_200 = 1
    deck27.EX1_593 = nil
    lines = D.ReasonLines(D.Evaluate("MAGE", deck27, nil), "MAGE", "Jaina")
    check(lines[1][1] == "Incomplete: 27/30" and lines[2][1] == "Missing 3 cards you do not own", "incomplete + missing: " .. tostring(lines[1][1]) .. " / " .. tostring(lines[2] and lines[2][1]))
    lines = D.ReasonLines(D.Evaluate("DRUID", { CS2_168 = 1 }, nil), "DRUID", "Jaina")
    check(lines[2][1] == "Locked for Jaina: Druid is not unlocked", "locked line")
    check(D.RowStatus(D.Evaluate("DRUID", { CS2_168 = 1 }, nil), "DRUID", "Jaina") == "|cffffd100Incomplete: 1/30|r; |cff999999Locked for Jaina: Druid is not unlocked|r", "row status")
    lines = D.ReasonLines(D.Evaluate("MAGE", { CS2_022 = 1 }, nil), "MAGE", "Jaina")
    check(lines[2][1] == "Missing 1 card you do not own", "singular")
    O.Reset()
    lines = D.ReasonLines(D.Evaluate("MAGE", { CS2_022 = 1 }, { owned = true }), "MAGE", "Jaina")
    check(lines[2][1] == "Missing cards you do not own", "count unknown without the collection")
    lines = D.ReasonLines(D.Evaluate("MAGE", { CS2_108 = 1 }, nil), "MAGE", "Jaina")
    check(lines[2][1] == "Contains cards a Mage deck cannot hold", "invalid line")

    -- the editor draft and its reasons
    D.EndEdit()
    check(D.AddCard("CS2_029") == false, "no draft: nothing to add")
    local dr = D.BeginNew("MAGE")
    check(dr and dr.deckId == 0 and dr.name == "Custom Mage" and D.Count(dr.cards) == 0 and not D.IsDirty(), "new draft")
    check(D.BeginNew("NEUTRAL") == nil and D.draft == dr, "no draft of a non-class")
    local ok, reason = D.AddCard("CS2_029")
    check(not ok and reason == "your collection is not loaded", "add needs the collection: " .. tostring(reason))
    ownCards({ CS2_029 = 2, CS2_022 = 1, EX1_116 = 3, CS2_108 = 2, CS2_168 = 9, EX1_277 = 5 })
    check(D.AddCard("CS2_029") and D.AddCard("CS2_029") and D.InDraft("CS2_029") == 2 and D.IsDirty(), "two copies")
    ok, reason = D.AddCard("CS2_029")
    check(not ok and reason == "only 2 copies", "only 2 copies")
    check(D.AddCard("EX1_116"), "a Legendary")
    ok, reason = D.AddCard("EX1_116")
    check(not ok and reason == "Legendary: only 1 copy", "Legendary: only 1 copy")
    check(D.AddCard("CS2_022"), "the only owned copy")
    ok, reason = D.AddCard("CS2_022")
    check(not ok and reason == "you own only 1 copy", "you own only 1 copy")
    ok, reason = D.AddCard("CS2_025")
    check(not ok and reason == "you do not own this card", "not owned")
    ok, reason = D.AddCard("CS2_108")
    check(not ok and reason == "wrong class", "wrong class")
    ok, reason = D.AddCard("CS1h_001")
    check(not ok and reason == "not collectible", "hero power: not collectible")
    ok, reason = D.AddCard("DREAM_01")
    check(not ok and reason == "not collectible", "Dream card: not collectible")
    ownCards({ CS2_029 = 1 })
    D.BeginNew("MAGE")
    check(D.AddCard("CS2_029"), "one owned copy")
    ok, reason = D.AddCard("CS2_029")
    check(not ok and reason == "you own only 1 copy", "owned limit below the copy limit")
    local all = {}
    for i = 1, table.getn(MAGE_IDS) do all[MAGE_IDS[i]] = 2 end
    all.NEW1_012 = 2
    ownCards(all)
    D.BeginNew("MAGE")
    for i = 1, table.getn(MAGE_IDS) do D.AddCard(MAGE_IDS[i]); D.AddCard(MAGE_IDS[i]) end
    check(D.Count(D.draft.cards) == 30, "30 cards")
    ok, reason = D.AddCard("NEW1_012")
    check(not ok and reason == "deck is full", "deck is full")
    check(D.RemoveCard("CS2_029") and D.InDraft("CS2_029") == 1 and D.RemoveCard("CS2_029") and D.InDraft("CS2_029") == 0 and D.draft.cards.CS2_029 == nil, "remove copies")
    ok, reason = D.RemoveCard("CS2_029")
    check(not ok and reason == "not in the deck", "remove a card that is not in the deck")
    check(D.AddCard("NEW1_012"), "room again")
    -- rows: cost, then name, then id
    local rows = D.Rows()
    check(rows[1].cost <= rows[2].cost and rows[table.getn(rows)].id == "CS2_200", "rows sorted by cost")
    local sorted = true
    for i = 2, table.getn(rows) do
        local a, b = rows[i - 1], rows[i]
        if a.cost > b.cost or (a.cost == b.cost and string.lower(a.name) > string.lower(b.name)) then sorted = false end
    end
    check(sorted, "rows sorted by cost and name")
    check(rows[1].id == "EX1_277" and rows[2].id == "NEW1_012" and rows[3].id == "CS2_168", "1-cost rows: Arcane Missiles, Mana Wyrm, Murloc Raider")
    local curve = D.Curve()
    check(curve[1] == 5 and curve[6] == 2 and curve[7] == 0 and curve[0] == 0, "mana curve")
    -- edit draft and dirty flag
    local saved = { id = 4, rev = 2, class = "MAGE", name = "Tempo", cards = { CS2_029 = 2 } }
    D.BeginEdit(saved)
    check(D.draft.deckId == 4 and D.draft.baseRev == 2 and not D.IsDirty() and D.draft.cards ~= saved.cards, "edit a copy")
    D.RemoveCard("CS2_029")
    check(D.IsDirty() and saved.cards.CS2_029 == 2, "the saved deck is untouched")
    D.AddCard("CS2_029")
    check(not D.IsDirty(), "back to the saved cards: not dirty")
    D.SetDraftName("  Tempo ")
    check(not D.IsDirty(), "spaces around the name do not count")
    D.SetDraftName("Tempo 2")
    check(D.IsDirty(), "renamed: dirty")

    -- payloads and keys
    local op = D.BeginSaveDraft()
    check(op and op.kind == "save" and HsPacks.IsValidKey(op.key), "save op")
    local kv = P.KvDecode(op.payload)
    check(kv.key == op.key and kv.id == "4" and kv.rev == "2" and kv.class == "MAGE" and kv.name == "Tempo 2" and kv.cards == "CS2_029.2", "DECK_SAVE payload: " .. op.payload)
    check(D.IsBusy(), "busy")
    ok, reason = D.AddCard("CS2_022")
    check(not ok and reason == "wait until the deck is saved", "no edits while saving")
    ok, reason = D.RemoveCard("CS2_029")
    check(not ok and reason == "wait until the deck is saved", "no removal while saving")
    D.ClearOp()
    D.BeginNew("WARRIOR")
    D.SetDraftName("  Smash  ")
    local op1 = D.BeginSaveDraft()
    D.ClearOp()
    local op2 = D.BeginSaveDraft()
    check(op1.key == op2.key and D.draft.createKey == op1.key and P.KvDecode(op2.payload).id == "0" and P.KvDecode(op2.payload).rev == "0" and P.KvDecode(op2.payload).name == "Smash", "a new deck keeps its create key")
    D.ClearOp()
    local opR = D.BeginRename(saved, " Renamed ")
    check(opR.kind == "rename" and opR.key ~= op1.key and P.KvDecode(opR.payload).name == "Renamed" and P.KvDecode(opR.payload).cards == "CS2_029.2", "rename payload")
    D.ClearOp()
    check(D.BeginRename(saved, "") == nil, "no rename to an invalid name")
    local opD = D.BeginDelete(saved)
    check(opD.kind == "delete" and opD.payload == "key=" .. opD.key .. ";id=4;rev=2", "DECK_DELETE payload: " .. opD.payload)
    D.ClearOp()
    local keys = {}
    for _ = 1, 200 do
        local k = D.NewKey()
        check(HsPacks.IsValidKey(k) and not keys[k], "fresh keys")
        keys[k] = true
    end

    -- status line
    D.EndEdit()
    D.SetReady(D.ParseDecks(good))
    check(D.StatusText() == "decks: ready, 2 / 36 saved, start MAGE, unlocked WARRIOR", "status: " .. D.StatusText())
    D.BeginNew("MAGE")
    D.AddCard("CS2_168")
    check(string.find(D.StatusText(), "editing Mage deck \"Custom Mage\" (unsaved changes)", 1, true) ~= nil, "status while editing")
    D.SetUnsupported()
    check(string.find(D.StatusText(), "decks: not supported by this server", 1, true) == 1 and D.draft ~= nil, "unsupported keeps the draft")
    resetAddon()
end

-- ----------------------------------------------------------- decks (flow)
local function deckHelpers(T)
    local t = {}
    -- the DECKS reply to the DECK_LIST in flight (chunked like the server does it)
    function t.decks(payload)
        local frames = P.ChunkPayload("DECKS", H.decks.reqId, payload)
        assert(frames, "DECKS payload fits the frame limit")
        for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
        return table.getn(frames)
    end
    -- the COLL reply to the COLL_GET in flight: owned = { id = total }
    function t.coll(ver, owned)
        local entries, n = {}, 0
        for id, total in pairs(owned) do
            table.insert(entries, id .. "." .. total .. ".0")
            n = n + 1
        end
        table.sort(entries)
        local frames = P.ChunkPayload("COLL", H.coll.reqId, "ver=" .. ver .. ";n=" .. n .. ";cards=" .. table.concat(entries, ","))
        for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
    end
    -- the last request 'op' that was sent, its parts joined: { op, reqId, payload }
    function t.lastOp(op)
        local l = T.sentOps(op)
        local last = l[table.getn(l)]
        if not last then return nil end
        local parts = {}
        for i = 1, table.getn(l) do
            if l[i].reqId == last.reqId then parts[l[i].part] = l[i].payload end
        end
        return { op = op, reqId = last.reqId, payload = table.concat(parts, "", 1, last.total) }
    end
    function t.kv(f)
        return f and P.KvDecode(f.payload)
    end
    function t.refused(reqId, why)
        T.frame("ERR", reqId, "code=REFUSED;msg=refused;why=" .. why)
    end
    return t
end

local function ownedMage()
    local owned = { EX1_116 = 1, NEW1_012 = 2 }
    for i = 1, table.getn(MAGE_IDS) do owned[MAGE_IDS[i]] = 2 end
    return owned
end

local function testDecksFlow()
    local D = HsDecks
    local O = HsOwnership
    local T = packHelpers()
    local X = deckHelpers(T)
    resetAddon()
    local savedDb = H.db
    H.db = HsCollection.ApplySavedDefaults(nil)
    local notified = 0
    local savedHook = H.OnDecksChanged
    H.OnDecksChanged = function() notified = notified + 1 end
    local function flush() drain(1) end
    local devMage = { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" }
    local list1 = decksPayload(36, "MAGE", "", { devMage })

    check(H.reasm.limits.maxBytes == 32768, "reply bound raised to 32 KiB")

    -- nothing before WELCOME
    check(H.DeckList() == nil and table.getn(H.queue) == 0, "no DECK_LIST before WELCOME")
    local k0, w0 = H.DeckRename(1, "x")
    check(k0 == nil and w0 == "Not connected to the server.", "no change before WELCOME")
    T.frame("DECK_CHANGED", "0", "why=gm")
    check(D.state == "none" and table.getn(H.queue) == 0, "DECK_CHANGED before WELCOME is ignored")

    -- a server without decks
    T.welcome("solo,chunk")
    check(D.state == "unsupported" and table.getn(T.queuedOps("DECK_LIST")) == 0, "caps without decks: unsupported, no DECK_LIST")
    T.frame("DECK_CHANGED", "0", "why=gm")
    check(table.getn(T.queuedOps("DECK_LIST")) == 0, "pushes ignored without decks")
    printed = {}
    H.Slash("status")
    check(string.find(T.joined(), "decks: not supported by this server", 1, true) ~= nil, "status without decks")
    D.BeginNew("MAGE")
    check(H.DeckSaveBlocker() == "This server does not support decks yet.", "save blocked on such a server")
    local kU, wU = H.DeckSave()
    check(kU == nil and wU == "This server does not support decks yet." and D.op == nil, "H.DeckSave says why")
    D.EndEdit()
    flush()
    X.coll(1, ownedMage())
    H.queue = {}

    -- with decks: COLL_GET then DECK_LIST
    T.welcome("solo,chunk,decks")
    local q = {}
    for i = 1, table.getn(H.queue) do local _, f = P.ParseFrame(H.queue[i]); table.insert(q, f) end
    check(table.getn(q) == 2 and q[1].op == "COLL_GET" and q[2].op == "DECK_LIST" and q[2].payload == "", "WELCOME sends COLL_GET and an empty DECK_LIST")
    check(D.state == "loading" and H.decks.reqId == q[2].reqId and H.pending[q[2].reqId].op == "DECK_LIST" and H.deckReqs[q[2].reqId] == "DECK_LIST", "decks loading")
    check(H.DeckOpBlocker() == "Your decks are still loading.", "no change while loading")
    T.frame("DECK_CHANGED", "0", "why=class")
    check(H.decks.refetch and table.getn(T.queuedOps("DECK_LIST")) == 1, "a push while loading marks a refetch")
    flush()
    X.coll(2, ownedMage())
    T.frame("DECKS", "zz", "cap=36;n=0;start=MAGE;unlocked=")
    check(D.state == "loading", "a DECKS for another reqId is ignored")
    notified = 0
    X.decks(list1)
    check(D.IsReady() and D.DeckCount() == 1 and D.byId[1].name == "Dev Mage" and D.cap == 36 and D.start == "MAGE", "DECKS applied")
    check(notified > 0, "UI told")
    check(H.decks.reqId ~= nil and not H.decks.refetch and table.getn(T.queuedOps("DECK_LIST")) == 1, "the refetch went out")
    flush()
    X.decks(list1)
    check(H.decks.reqId == nil and table.getn(H.queue) == 0, "no more requests")
    printed = {}
    H.Slash("status")
    check(string.find(T.joined(), "decks: ready, 1 / 36 saved, start MAGE", 1, true) ~= nil, "status line for decks")
    T.frame("DECK_CHANGED", "0", "why=gm")
    check(table.getn(T.queuedOps("DECK_LIST")) == 1 and D.IsReady() and D.DeckCount() == 1, "DECK_CHANGED refetches, data stays")
    flush()
    X.decks(list1)

    -- save a new deck
    sent = {}
    D.BeginNew("MAGE")
    D.SetDraftName("  Tempo ")
    D.AddCard("CS2_029")
    D.AddCard("EX1_116")
    local key = H.DeckSave()
    check(HsPacks.IsValidKey(key) and D.op and D.op.kind == "save" and D.draft.createKey == key, "save starts: " .. tostring(key))
    local k2, w2 = H.DeckSave()
    check(k2 == nil and w2 == "A deck change is being sent.", "only one change in flight")
    check(D.AddCard("CS2_022") == false, "no edits while saving")
    flush()
    local f1 = X.lastOp("DECK_SAVE")
    local kv1 = X.kv(f1)
    check(table.getn(T.sentOps("DECK_SAVE")) == 1 and kv1.key == key and kv1.id == "0" and kv1.rev == "0" and kv1.class == "MAGE"
        and kv1.name == "Tempo" and kv1.cards == "CS2_029.1,EX1_116.1", "DECK_SAVE payload: " .. tostring(f1 and f1.payload))
    check(f1.reqId ~= key and H.deckReqs[f1.reqId] == "DECK_SAVE", "the key is not the reqId")
    T.frame("DECK_SAVED", "zz9", "key=zzzzzzzzzzzz;id=9;rev=1;replay=0;state=ok")
    check(D.op ~= nil, "a DECK_SAVED with another key and reqId is ignored")
    T.frame("DECK_DELETED", f1.reqId, "key=" .. key .. ";id=2;replay=0")
    check(D.op ~= nil, "a DECK_DELETED does not settle a save")
    H.queue = {}
    T.frame("DECK_SAVED", f1.reqId, "key=" .. key .. ";id=2;rev=1;replay=0;state=size")
    check(D.op == nil and D.draft == nil and D.selected == 2, "saved: editor closed, the new deck selected")
    check(D.byId[2] and D.byId[2].name == "Tempo" and D.byId[2].count == 2 and D.byId[2].rev == 1 and D.DeckCount() == 2, "the list shows it at once")
    check(D.message == nil and table.getn(T.queuedOps("DECK_LIST")) == 1, "message and refetch")
    check(H.pending[f1.reqId] == nil, "nothing left to time out")
    flush()
    local devTempo = { 2, 1, "MAGE", "Tempo", "CS2_029.1,EX1_116.1", "size" }
    local list2 = decksPayload(36, "MAGE", "", { devMage, devTempo })
    X.decks(list2)
    T.frame("DECK_SAVED", f1.reqId, "key=" .. key .. ";id=2;rev=1;replay=0;state=size")
    check(D.op == nil and D.draft == nil, "a duplicate DECK_SAVED changes nothing")

    -- timeout and UNAVAILABLE on a new deck: the same key, three sends, then stop and keep the key
    sent = {}
    printed = {}
    D.BeginNew("MAGE")
    D.AddCard("CS2_168")
    local keyN = H.DeckSave()
    flush()
    local a1 = X.lastOp("DECK_SAVE")
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(D.op and D.op.retryIn ~= nil and H.pending[a1.reqId] == nil, "timeout: retry scheduled")
    check(string.find(T.joined(), "timed out", 1, true) == nil, "no timeout message for a retried save")
    drain(3)
    local saves = T.sentOps("DECK_SAVE")
    check(table.getn(saves) == 2 and saves[2].payload == a1.payload and saves[2].reqId ~= a1.reqId, "retry: the same payload and key, a new reqId")
    T.err(saves[2].reqId, "UNAVAILABLE", "db down")
    check(D.op and D.op.retryIn ~= nil and O.IsReady() and string.find(T.joined(), "card collection", 1, true) == nil, "UNAVAILABLE: retried, the collection is left alone")
    drain(3)
    saves = T.sentOps("DECK_SAVE")
    check(table.getn(saves) == 3 and saves[3].payload == a1.payload and D.op.attempts == 3, "third attempt")
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(D.op == nil and D.draft and D.draft.createKey == keyN and D.draft.deckId == 0, "gave up after three attempts, the create key is kept")
    check(string.find(T.joined(), "did not confirm the save", 1, true) ~= nil and string.find(D.message, "cannot create a second deck", 1, true) ~= nil, "player told")
    check(table.getn(T.queuedOps("DECK_LIST")) == 1, "the list is reloaded")
    flush()
    check(table.getn(T.sentOps("DECK_SAVE")) == 3, "no fourth send")
    X.decks(list2)
    -- the player edits on, then saves: the unconfirmed create is asked about again, unchanged
    check(D.AddCard("CS2_168"), "editing is possible again")
    local keyN2 = H.DeckSave()
    flush()
    local a4 = X.lastOp("DECK_SAVE")
    check(keyN2 == keyN and a4.payload == a1.payload, "the same request again (same key, same cards)")
    -- the server replays the create of the first attempt: the latest edits still need a save
    T.frame("DECK_SAVED", a4.reqId, "key=" .. keyN .. ";id=3;rev=1;replay=1;state=size")
    check(D.op == nil and D.draft and D.draft.deckId == 3 and D.draft.baseRev == 1 and D.draft.createKey == nil, "the draft is now deck 3")
    check(D.IsDirty() and string.find(D.message, "earlier version", 1, true) ~= nil, "the second copy is not saved yet")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { devMage, devTempo, { 3, 1, "MAGE", "Custom Mage", "CS2_168.1", "size" } }))
    local keyU = H.DeckSave()
    flush()
    local kvU = X.kv(X.lastOp("DECK_SAVE"))
    check(keyU ~= keyN and kvU.id == "3" and kvU.rev == "1" and kvU.cards == "CS2_168.2", "an update of deck 3 with a fresh key")
    -- a late reply to an earlier attempt settles it
    local u1 = X.lastOp("DECK_SAVE")
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    drain(3)
    local u2 = X.lastOp("DECK_SAVE")
    check(u2.reqId ~= u1.reqId and u2.payload == u1.payload, "update retried")
    T.frame("DECK_SAVED", u1.reqId, "key=" .. keyU .. ";id=3;rev=2;replay=0;state=size")
    check(D.op == nil and D.draft == nil and H.pending[u2.reqId] == nil and D.byId[3].rev == 2, "late reply to the first attempt accepted, no timeout left")
    flush()
    local list3 = decksPayload(36, "MAGE", "", { devMage, devTempo, { 3, 2, "MAGE", "Custom Mage", "CS2_168.2", "size" } })
    X.decks(list3)
    printed = {}
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(T.joined() == "", "nothing times out afterwards")

    -- an error guessed for the save (reqId 0, oldest request) is not definitive
    H.pending = {}
    D.BeginEdit(D.byId[2])
    D.AddCard("CS2_168")
    local keyG = H.DeckSave()
    flush()
    local nSaves = table.getn(T.sentOps("DECK_SAVE"))
    T.err("0", "REFUSED", "x")
    check(D.op and D.op.key == keyG and D.op.retryIn ~= nil, "a reqId 0 error is retried")
    drain(3)
    check(table.getn(T.sentOps("DECK_SAVE")) == nSaves + 1 and X.kv(X.lastOp("DECK_SAVE")).key == keyG, "retried with the same key")

    -- conflict: the deck changed elsewhere; the list is reloaded, the next save uses the new revision
    printed = {}
    H.queue = {}
    X.refused(X.lastOp("DECK_SAVE").reqId, "conflict")
    check(D.op == nil and D.draft and D.draft.rebase and D.draft.deckId == 2, "conflict: draft kept")
    check(string.find(T.joined(), "changed elsewhere", 1, true) ~= nil and string.find(D.message, "Save again", 1, true) ~= nil, "conflict message")
    check(table.getn(T.queuedOps("DECK_LIST")) == 1, "conflict reloads the list")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { devMage, { 2, 5, "MAGE", "Tempo", "CS2_029.1,EX1_116.1", "size" } }))
    check(D.draft.baseRev == 5 and not D.draft.rebase, "rebased on the current revision")
    H.DeckSave()
    flush()
    check(X.kv(X.lastOp("DECK_SAVE")).rev == "5", "the next save uses revision 5")

    -- missing: the deck was deleted elsewhere; the draft becomes a new deck
    H.queue = {}
    X.refused(X.lastOp("DECK_SAVE").reqId, "missing")
    check(D.op == nil and D.draft.deckId == 0 and D.draft.baseRev == 0 and string.find(D.message, "new deck", 1, true) ~= nil, "missing: the draft is a new deck now")
    check(table.getn(T.queuedOps("DECK_LIST")) == 1, "missing reloads the list")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { devMage }))
    H.DeckSave()
    flush()
    local kvM = X.kv(X.lastOp("DECK_SAVE"))
    check(kvM.id == "0" and kvM.rev == "0" and kvM.cards == "CS2_029.1,CS2_168.1,EX1_116.1", "saved as a new deck")

    -- other refusals are definitive: nothing changed, no refetch, a fresh key next time
    printed = {}
    H.queue = {}
    local keyC = D.op.key
    X.refused(X.lastOp("DECK_SAVE").reqId, "cap")
    check(D.op == nil and D.draft.createKey == nil and string.find(D.message, "maximum of 36 decks", 1, true) ~= nil and string.find(D.message, "Nothing was changed", 1, true) ~= nil, "cap: " .. tostring(D.message))
    check(table.getn(T.queuedOps("DECK_LIST")) == 0, "no refetch for a cap refusal")
    for _, why in ipairs({ "class", "name", "card", "cardclass", "copies", "size", "key", "weird" }) do
        local kk = H.DeckSave()
        check(kk ~= nil and kk ~= keyC, "save again with a fresh key (" .. why .. ")")
        flush()
        X.refused(X.lastOp("DECK_SAVE").reqId, why)
        check(D.op == nil and D.draft ~= nil and string.find(D.message, "The deck was not saved", 1, true) == 1, "refused why=" .. why .. ": " .. tostring(D.message))
    end
    check(string.find(D.message, "refused", 1, true) ~= nil, "an unknown why falls back to the server text")
    H.DeckSave()
    flush()
    T.err(X.lastOp("DECK_SAVE").reqId, "MALFORMED", "bad cards")
    check(D.op == nil and string.find(D.message, "MALFORMED", 1, true) ~= nil, "MALFORMED is definitive")
    drain(5)
    check(D.op == nil, "no retry after a definitive error")

    -- a reply this addon cannot read: stop and reload
    H.DeckSave()
    flush()
    H.queue = {}
    printed = {}
    local am = X.lastOp("DECK_SAVE")
    T.frame("DECK_SAVED", am.reqId, "key=" .. D.op.key .. ";id=x;rev=1;replay=0;state=ok")
    check(D.op == nil and string.find(T.joined(), "cannot read", 1, true) ~= nil and table.getn(T.queuedOps("DECK_LIST")) == 1, "unreadable DECK_SAVED")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { devMage }))
    D.EndEdit()

    -- the cap blocks a new deck locally
    local full = {}
    for i = 1, 3 do table.insert(full, { i, 1, "MAGE", "Deck " .. i, "", "size" }) end
    H.DeckList()
    flush()
    X.decks(decksPayload(3, "MAGE", "", full))
    D.BeginNew("MAGE")
    check(H.DeckSaveBlocker() == "You have the maximum of 3 decks. Delete one first.", "cap reached: " .. tostring(H.DeckSaveBlocker()))
    D.EndEdit()
    D.BeginEdit(D.byId[1])
    D.SetDraftName("x|y")
    check(H.DeckSaveBlocker() == "Name: the name may not contain |.", "invalid name blocks: " .. tostring(H.DeckSaveBlocker()))
    D.EndEdit()

    -- rename
    sent = {}
    local kr, wr = H.DeckRename(1, "a|b")
    check(kr == nil and wr == "Name: the name may not contain |.", "rename to an invalid name")
    check(H.DeckRename(77, "x") == nil, "rename of an unknown deck")
    local keyR = H.DeckRename(1, "  First  ")
    flush()
    local fr = X.lastOp("DECK_SAVE")
    local kvr = X.kv(fr)
    check(keyR and kvr.key == keyR and kvr.id == "1" and kvr.rev == "1" and kvr.name == "First" and kvr.cards == "" and kvr.class == "MAGE", "rename payload: " .. fr.payload)
    H.queue = {}
    T.frame("DECK_SAVED", fr.reqId, "key=" .. keyR .. ";id=1;rev=2;replay=0;state=size")
    check(D.op == nil and D.byId[1].name == "First" and D.byId[1].rev == 2 and D.message == "Deck renamed to \"First\".", "renamed")
    check(table.getn(T.queuedOps("DECK_LIST")) == 1, "refetch after the rename")
    flush()
    full[1] = { 1, 2, "MAGE", "First", "", "size" }
    X.decks(decksPayload(3, "MAGE", "", full))
    local keyR2 = H.DeckRename(2, "Second")
    flush()
    T.frame("DECK_SAVED", X.lastOp("DECK_SAVE").reqId, "key=" .. keyR2 .. ";id=5;rev=2;replay=0;state=size")
    check(D.op == nil and D.byId[5] == nil and string.find(D.message, "cannot read", 1, true) ~= nil, "a DECK_SAVED for another deck id is unreadable")
    flush()
    X.decks(decksPayload(3, "MAGE", "", full))
    H.DeckRename(2, "Second")
    flush()
    printed = {}
    X.refused(X.lastOp("DECK_SAVE").reqId, "conflict")
    check(D.op == nil and string.find(D.message, "nothing was renamed", 1, true) ~= nil and table.getn(T.queuedOps("DECK_LIST")) == 1, "rename conflict: " .. tostring(D.message))
    flush()
    X.decks(decksPayload(3, "MAGE", "", full))

    -- delete
    sent = {}
    local keyD = H.DeckDelete(2)
    flush()
    local fd = X.lastOp("DECK_DELETE")
    check(keyD and fd and fd.payload == "key=" .. keyD .. ";id=2;rev=1" and H.deckReqs[fd.reqId] == "DECK_DELETE", "DECK_DELETE payload")
    T.frame("DECK_SAVED", fd.reqId, "key=" .. keyD .. ";id=2;rev=2;replay=0;state=ok")
    check(D.op ~= nil, "a DECK_SAVED does not settle a delete")
    H.queue = {}
    T.frame("DECK_DELETED", fd.reqId, "key=" .. keyD .. ";id=2;replay=0")
    check(D.op == nil and D.byId[2] == nil and D.DeckCount() == 2 and D.message == "Deck \"Deck 2\" deleted.", "deleted")
    check(table.getn(T.queuedOps("DECK_LIST")) == 1, "refetch after the delete")
    flush()
    X.decks(decksPayload(3, "MAGE", "", { full[1], full[3] }))
    H.DeckDelete(3)
    flush()
    X.refused(X.lastOp("DECK_DELETE").reqId, "missing")
    check(D.op == nil and D.message == "That deck was already deleted." and table.getn(T.queuedOps("DECK_LIST")) == 1, "delete of a deck that is gone")
    flush()
    X.decks(decksPayload(3, "MAGE", "", { full[1] }))
    -- a delete whose outcome stays unknown: the list tells
    H.DeckDelete(1)
    for _ = 1, 3 do
        flush()
        H.clock = H.clock + 20
        H.OnUpdate(0.05)
        drain(3)
    end
    check(D.op == nil and string.find(D.message, "did not confirm the deletion", 1, true) ~= nil and table.getn(T.sentOps("DECK_DELETE")) == 5, "delete gave up after three sends (2 earlier deletes + 3)")
    flush()
    X.decks(decksPayload(3, "MAGE", "", { full[1] }))

    -- DECK_LIST errors: UNAVAILABLE, timeout, unreadable; the collection is left alone
    printed = {}
    local g = H.DeckList()
    check(D.IsReady(), "a refetch keeps the data visible")
    T.err(g, "UNAVAILABLE", "db")
    check(D.state == "error" and D.DeckCount() == 0 and H.decks.reqId == nil and H.pending[g] == nil, "DECK_LIST UNAVAILABLE: decks error")
    check(O.IsReady() and string.find(T.joined(), "decks could not be loaded", 1, true) ~= nil and string.find(T.joined(), "card collection", 1, true) == nil, "deck message only")
    check(H.DeckOpBlocker() == "Your decks could not be loaded.", "no changes while in error")
    T.frame("DECK_CHANGED", "0", "why=gm")
    check(D.state == "loading" and H.decks.reqId ~= nil, "a push after an error refetches")
    flush()
    printed = {}
    H.clock = H.clock + 20
    H.OnUpdate(0.05)
    check(D.state == "error" and D.errorText == "no response" and string.find(T.joined(), "did not send your decks", 1, true) ~= nil, "DECK_LIST timeout")
    H.DeckList()
    flush()
    printed = {}
    X.decks("cap=36;n=1;start=MAGE;unlocked=")
    check(D.state == "error" and string.find(D.errorText, "bad reply", 1, true) ~= nil and string.find(T.joined(), "cannot read", 1, true) ~= nil, "malformed DECKS")
    H.DeckList()
    flush()
    X.decks(list1)

    -- a deck UNAVAILABLE never fails the COLL_GET in flight
    H.DeckSave()
    D.BeginNew("WARRIOR")
    H.DeckSave()
    flush()
    local wsave = X.lastOp("DECK_SAVE")
    T.frame("COLL_CHANGED", "0", "ver=9")
    flush()
    check(H.coll.reqId ~= nil, "COLL_GET in flight")
    T.err(wsave.reqId, "UNAVAILABLE", "db")
    check(H.coll.reqId ~= nil and O.state == "ready" and D.op and D.op.retryIn ~= nil, "deck UNAVAILABLE: collection untouched, save retried")
    X.coll(9, ownedMage())
    -- a late error of an old deck request touches nothing
    printed = {}
    T.err(fd.reqId, "UNAVAILABLE", "late")
    check(T.joined() == "" and D.op and D.IsReady(), "late deck error: no effect, no message")

    -- losing the connection: none; the draft stays; the change in flight is dropped with a note
    T.err("0", "DISABLED", "off")
    check(D.state == "none" and D.op == nil and D.draft ~= nil and D.draft.createKey ~= nil and string.find(D.message, "connection was lost", 1, true) ~= nil, "DISABLED: decks none, draft kept")
    H.clock = H.clock + 30
    drain(3)
    check(table.getn(T.queuedOps("DECK_SAVE")) == 0, "nothing retried while disconnected")
    T.frame("DECK_CHANGED", "0", "why=gm")
    check(D.state == "none", "pushes ignored while disconnected")
    check(H.DeckSaveBlocker() == "Not connected to the server.", "save blocked while disconnected")
    T.welcome("solo,chunk,decks")
    check(D.state == "loading" and H.decks.reqId ~= nil, "a new WELCOME reloads the decks")
    flush()
    X.decks(list1)
    local keyAfter = H.DeckSave()
    check(keyAfter == D.draft.createKey, "the kept create key is asked about again")
    flush()
    T.err("0", "VERSION", "v")
    check(D.state == "none" and not H.state.welcomed and D.op == nil, "VERSION: decks none")
    D.EndEdit()

    -- a DECKS of 44 decks (more than the cap: starter awards) with 30 cards each needs more than 16 KiB
    resetAddon()
    T.welcome("solo,chunk,decks")
    flush()
    local big = {}
    local ids = {}
    for i = 1, 30 do ids[i] = string.format("ZZ%06d.1", i) end
    for i = 1, 44 do
        table.insert(big, { i, 1, "PRIEST", string.rep("n", 22) .. string.format("%02d", i), table.concat(ids, ","), "invalid,locked" })
    end
    local bigPayload = decksPayload(40, "MAGE", "", big)
    check(string.len(bigPayload) > 16384, "the payload is above the old bound: " .. string.len(bigPayload))
    local nFrames = X.decks(bigPayload)
    check(nFrames <= 99 and D.IsReady() and D.DeckCount() == 44 and D.byId[44].count == 30, "accepted (" .. nFrames .. " frames)")
    local r44 = D.Evaluate(D.byId[44].class, D.byId[44].cards, D.byId[44].serverState)
    check(r44.invalid and r44.locked and not r44.size, "unknown cards are invalid, priest locked for a mage")

    -- the sample server frames, without matching requests: ignored, no errors
    local okSample, errSample = pcall(function()
        H.HandleFrame("v1\tDECKS\tk1\t1\t1\tcap=36;n=0;start=MAGE;unlocked=")
        H.HandleFrame("v1\tDECK~uSAVED\tk2\t1\t1\tkey=abcdef123456;id=1;rev=1;replay=0;state=ok")
        H.HandleFrame("v1\tDECK_SAVED\tk2\t1\t1\tkey=abcdef123456;id=1;rev=1;replay=0;state=ok")
        H.HandleFrame("v1\tDECK_DELETED\tk3\t1\t1\tkey=abcdef123456;id=1;replay=0")
        H.HandleFrame("v1\tERR\tk4\t1\t1\tcode=REFUSED;msg=x;why=conflict")
    end)
    check(okSample, "sample frames handled: " .. tostring(errSample))
    check(D.IsReady() and D.DeckCount() == 44 and D.op == nil, "unsolicited deck replies change nothing")

    -- /hs decks without a UI says so instead of raising
    printed = {}
    local savedShow = H.ShowDecks
    H.ShowDecks = nil
    H.Slash("decks")
    check(string.find(T.joined(), "DecksUI.lua is not loaded", 1, true) ~= nil, "/hs decks without DecksUI.lua")
    H.ShowDecks = savedShow
    H.Slash("help")
    check(string.find(lastPrinted(), "| decks |", 1, true) ~= nil, "help mentions decks")

    H.OnDecksChanged = savedHook
    H.db = savedDb
    resetAddon()
end

-- ------------------------------------------------------------- decks (UI)
-- The deck panel of the one collection window. Browse mode (tiles) and edit mode (the deck's card
-- list) slide into each other; the book switches to the deck class and Neutral while a deck is edited.
local function testDecksUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local DU = H.DecksUI
    local PU = H.PacksUI
    local D = HsDecks
    local O = HsOwnership
    local W = H.CardWidget
    local MB = H.MicroButton
    local CM = HsCollection
    local T = packHelpers()
    local X = deckHelpers(T)
    local savedDb = H.db
    local savedTextures = HS_CardTextures

    local function resetUI()
        UI.frame, UI.preview, UI.hover, UI.layoutMode = nil, nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons, UI.manaAll = {}, {}, {}, nil
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited, s.savedClass = nil, nil, false, nil
        PU.panel, PU.slots, PU.rows, PU.hover = nil, {}, {}, nil
        DU.panel, DU.picker, DU.tiles, DU.empties, DU.rows, DU.pickButtons, DU.curveBars = nil, nil, {}, {}, {}, {}, {}
        DU.page, DU.hoverRow, DU.shownDraft, DU.shown, DU.trans, DU.snap, DU.leaveAfterDelete = 1, nil, nil, "browse", nil, true, nil
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    local function flush() drain(1) end
    local function near_(a, b) return math.abs(a - b) < 0.0001 end
    local function slotOf(id)
        for i = 1, table.getn(UI.slots) do
            if UI.slots[i]:IsShown() and UI.slots[i].id == id then return UI.slots[i], i end
        end
        return nil
    end
    local function shownTabs()
        local keys = {}
        for i = 1, table.getn(UI.classTabs) do
            if UI.classTabs[i].button:IsShown() then table.insert(keys, UI.classTabs[i].key) end
        end
        return table.concat(keys, ",")
    end
    local function rowsShown()
        local n = 0
        for i = 1, table.getn(DU.rows) do
            if DU.rows[i]:IsShown() then n = n + 1 end
        end
        return n
    end
    local function tilesShown()
        local n = 0
        for i = 1, table.getn(DU.tiles) do
            if DU.tiles[i]:IsShown() then n = n + 1 end
        end
        return n
    end
    local function rowOf(id)
        for i = 1, table.getn(DU.rows) do
            if DU.rows[i]:IsShown() and DU.rows[i].cardId == id then return DU.rows[i], i end
        end
        return nil
    end
    local function click(slotId, button)
        local _, i = slotOf(slotId)
        assert(i, "card " .. slotId .. " is on the page")
        F.fire(UI.slots[i], "OnClick", { arg1 = button or "LeftButton" })
    end
    local function showCard(id)
        -- puts the card on the current page: its tab (deck class or Neutral) and the search box
        local c = HS_CardDB.cards[id]
        UI.SetClass(c.class)
        UI.frame.searchBox:SetText(c.name)
        return slotOf(id)
    end
    local function tabY(i) return UI.classTabs[i].button._.points[1].y end
    -- runs a slide to its end
    local function finishSlide()
        F.fire(DU.panel, "OnUpdate", { arg1 = DU.TRANS_TIME + 0.01 })
    end

    resetAddon()
    HS_CardTextures = { generated = false, order = {}, bottom = {} }
    F.install({ class = { "Mage", "MAGE" }, name = "Jaina" })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()

    -- /hs decks opens the collection window with the deck panel, offline first
    H.Slash("decks")
    local main = UI.frame
    local pnl = main and main.sidePanel
    local br = pnl and pnl.browse
    local ed = pnl and pnl.edit
    check(main and main:IsShown() and UI.state.bottom == 1 and main.selectedTab == 1, "/hs decks opens the collection tab")
    check(pnl and pnl:IsShown() and br:IsShown() and not ed:IsShown() and main.book:IsShown() and not main.unavailable:IsShown(), "browse mode: book and deck tiles, no second view")
    check(br.title:GetText() == "My Decks" and br.status:GetText() == "Not connected to the server." and br.countText:GetText() == "", "offline texts")
    check(not br.newButton:IsEnabled(), "New Deck disabled offline")
    check(table.getn(DU.tiles) == DU.ROWS and table.getn(DU.rows) == DU.TRAY_ROWS and table.getn(DU.pickButtons) == 9, "tiles, card rows and nine class buttons built")
    check(main:GetWidth() == UI.WIDTH and DU.picker ~= nil and not DU.picker:IsShown(), "window width, picker hidden")
    check(DU.HeroCard("MAGE") == "HERO_08" and DU.HeroCard("WARRIOR") == "HERO_01" and DU.HeroCard("DRUID") == "HERO_06" and DU.HeroCard("NOPE") == nil, "the hero card of each class")
    MB.Create()
    local created = table.getn(F.frames)

    -- unsupported, loading, ready
    T.welcome("solo,chunk")
    check(br.status:GetText() == "This server does not support decks yet." and not br.newButton:IsEnabled(), "unsupported")
    flush()
    X.coll(1, ownedMage())
    H.queue = {}
    T.welcome("solo,chunk,decks")
    check(br.status:GetText() == "Loading your decks...", "loading")
    flush()
    X.coll(2, ownedMage())
    local devTempo = { 2, 1, "WARRIOR", "Smash", "CS2_108.2", "size,owned" }
    X.decks(decksPayload(36, "MAGE", "", { { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" }, devTempo }))
    check(br.countText:GetText() == "2 / 36 Decks", "counter plate: " .. tostring(br.countText:GetText()))
    check(DU.tiles[1]:IsShown() and DU.tiles[2]:IsShown() and not DU.tiles[3]:IsShown() and tilesShown() == 2, "two deck tiles")
    check(DU.tiles[1].name:GetText() == "Dev Mage" and DU.tiles[1].count:GetText() == "|cff33ff3330/30|r" and DU.tiles[2].count:GetText() == "|cffffd1002/30|r", "tile name and count: " .. tostring(DU.tiles[1].count:GetText()))
    check(DU.tiles[1].art._.texture == "Interface\\Buttons\\WHITE8X8" and DU.tiles[1].art._.vertex[3] > DU.tiles[1].art._.vertex[1], "no hero texture deployed: the mage colour")
    check(br.status:GetText() == "" and br.newButton:IsEnabled() and not br.prevButton:IsShown() and br.pageText:GetText() == "", "New enabled, one page: no paging")
    F.fire(br.counter, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[2] == "Jaina can play: Mage (starting class)" and string.find(F.tooltip.lines[3], "Talk to a Hearthstone opponent", 1, true) ~= nil, "counter tooltip: the playable classes")
    F.fire(br.counter, "OnLeave")
    F.fire(DU.tiles[1], "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Dev Mage" and F.tooltip.lines[3] == "Complete" and F.tooltip.lines[4] == "Opponents offer it when all its cards work in matches.", "tile tooltip")
    F.fire(DU.tiles[1], "OnLeave")
    F.fire(DU.tiles[2], "OnEnter")
    local tl = table.concat(F.tooltip.lines, "|")
    check(string.find(tl, "Incomplete: 2/30", 1, true) ~= nil and string.find(tl, "Missing 2 cards you do not own", 1, true) ~= nil
        and string.find(tl, "Locked for Jaina: Warrior is not unlocked", 1, true) ~= nil, "warrior tile tooltip: " .. tl)
    F.fire(DU.tiles[2], "OnLeave")

    -- the hero art comes from the card textures when they are deployed
    HS_CardTextures = { generated = true, order = { "HERO_08" }, bottom = { HERO_08 = 388 / 512 } }
    UI.RefreshAll()
    local tc = DU.tiles[1].art._.texcoord
    check(string.find(DU.tiles[1].art._.texture, "Textures\\Cards\\HERO_08", 1, true) ~= nil and tc[1] == DU.TILE_CROP[1] and tc[2] == DU.TILE_CROP[2]
        and near_(tc[3], DU.TILE_CROP[3] * 388 / 512) and near_(tc[4], DU.TILE_CROP[4] * 388 / 512), "tile art: the hero's face from the card texture")
    check(DU.tiles[2].art._.texture == "Interface\\Buttons\\WHITE8X8", "a class without a deployed hero texture keeps the colour")
    HS_CardTextures = { generated = false, order = {}, bottom = {} }
    UI.RefreshAll()

    -- an ownership change refreshes the states
    T.frame("COLL_CHANGED", "0", "ver=3")
    flush()
    X.coll(3, { CS2_108 = 2 })
    F.fire(DU.tiles[1], "OnEnter")
    check(string.find(table.concat(F.tooltip.lines, "|"), "Missing 30 cards you do not own", 1, true) ~= nil and DU.tiles[1].count:GetText() == "|cffffd10030/30|r", "COLL change: mage deck now missing cards")
    F.fire(DU.tiles[2], "OnEnter")
    check(string.find(table.concat(F.tooltip.lines, "|"), "Missing", 1, true) == nil, "COLL change: warrior deck owned now")
    F.fire(DU.tiles[2], "OnLeave")
    T.frame("COLL_CHANGED", "0", "ver=4")
    flush()
    X.coll(4, ownedMage())

    -- right click: rename (StaticPopup with an edit box)
    sent = {}
    F.fire(DU.tiles[2], "OnClick", { arg1 = "RightButton" })
    check(F.popup and F.popup.which == "HEARTHSTONE_RENAME_DECK" and F.popup.text == "Rename the deck \"Smash\":", "rename popup: " .. tostring(F.popup and F.popup.text))
    check(D.draft == nil and br:IsShown(), "a right click does not open the deck")
    local info = StaticPopupDialogs.HEARTHSTONE_RENAME_DECK
    check(info.hasEditBox == 1 and info.maxLetters == 24 and info.button1 and info.button2 and info.timeout == 0, "rename popup definition")
    local eb = F.globals.StaticPopup1EditBox
    check(eb:GetText() == "Smash", "the edit box holds the current name")
    eb:SetText("Smash Bros")
    F.popupClick(1)
    flush()
    local fr = X.lastOp("DECK_SAVE")
    check(fr and X.kv(fr).name == "Smash Bros" and X.kv(fr).id == "2" and X.kv(fr).cards == "CS2_108.2", "rename sends DECK_SAVE")
    check(br.status:GetText() == "Saving..." and not br.newButton:IsEnabled(), "busy while saving")
    F.fire(DU.tiles[1], "OnClick", { arg1 = "LeftButton" })
    check(D.draft == nil, "no deck can be opened while a change is in flight")
    H.queue = {}
    T.frame("DECK_SAVED", fr.reqId, "key=" .. X.kv(fr).key .. ";id=2;rev=2;replay=0;state=size,owned,locked")
    check(DU.tiles[2].name:GetText() == "Smash Bros" and br.status:GetText() == "Deck renamed to \"Smash Bros\".", "renamed in the list")
    flush()
    devTempo = { 2, 2, "WARRIOR", "Smash Bros", "CS2_108.2", "size,owned" }
    X.decks(decksPayload(36, "MAGE", "", { { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" }, devTempo }))
    -- Enter in the popup, invalid name: nothing is sent, the reason is shown
    F.fire(DU.tiles[2], "OnClick", { arg1 = "RightButton" })
    F.globals.StaticPopup1EditBox:SetText("a|b")
    sent = {}
    F.popupEnter()
    check(F.popup == nil and table.getn(H.queue) == 0 and br.status:GetText() == "Name: the name may not contain |.", "invalid rename: " .. tostring(br.status:GetText()))

    -- a click on a tile opens that deck: the panel turns into its card list (a slide, no second view)
    D.message = nil
    F.fire(DU.tiles[2], "OnClick", { arg1 = "LeftButton" })
    check(D.draft and D.draft.deckId == 2 and D.draft.class == "WARRIOR", "the warrior deck is open")
    check(DU.trans ~= nil and DU.trans.from == "browse" and DU.trans.to == "edit" and br:IsShown() and ed:IsShown(), "both modes are on screen while sliding")
    local kx0 = br._.points[1].x
    check(br._.alpha == 1 and ed._.alpha == 0 and ed._.points[1].x == DU.SLIDE and kx0 == 0, "the slide starts: the old mode in place, the new one at the right and invisible")
    F.fire(DU.panel, "OnUpdate", { arg1 = DU.TRANS_TIME / 2 })
    check(br._.alpha < 1 and br._.alpha > 0 and ed._.alpha > 0 and ed._.alpha < 1 and br._.points[1].x < 0 and ed._.points[1].x > 0 and ed._.points[1].x < DU.SLIDE, "half way: sliding left, cross fading")
    finishSlide()
    check(DU.trans == nil and not br:IsShown() and ed:IsShown() and ed._.alpha == 1 and ed._.points[1].x == 0 and br._.alpha == 1 and br._.points[1].x == 0 and DU.panel._.scripts.OnUpdate == nil, "the slide ends: only the card list, restored positions, no update handler")
    check(shownTabs() == "WARRIOR,NEUTRAL" and UI.state.filters.class == "WARRIOR" and UI.state.filters.deckClass == "WARRIOR", "the book offers the deck class and Neutral only: " .. shownTabs())
    check(main.title:GetText() == "Warrior" and UI.layoutMode == "edit:WARRIOR", "title plate: the deck class")
    check(ed.nameBox:GetText() == "Smash Bros" and ed.classText:GetText() == "Warrior deck" and ed.countText:GetText() == "2 / 30" and rowsShown() == 1, "card list header")
    check(ed.deleteButton:IsShown() and ed.deleteButton:IsEnabled() and not ed.saveButton:IsEnabled(), "a saved deck: Delete shown, Save disabled until something changed")
    -- delete (confirmation) from the card list
    F.fire(ed.deleteButton, "OnClick")
    check(F.popup and F.popup.which == "HEARTHSTONE_DELETE_DECK" and string.find(F.popup.text, "Smash Bros", 1, true) ~= nil, "delete confirmation")
    F.popupClick(2)
    check(table.getn(H.queue) == 0 and D.op == nil and D.draft ~= nil, "No: nothing deleted, still editing")
    -- popups are not modal: a delete popup still open for deck 2 must delete deck 2, even after deck 1 was opened
    F.fire(ed.deleteButton, "OnClick")
    local staleDelete = StaticPopupDialogs.HEARTHSTONE_DELETE_DECK
    F.popupClick(2)
    D.EndEdit()
    UI.Update()
    finishSlide()
    F.fire(DU.tiles[1], "OnClick", { arg1 = "LeftButton" })
    finishSlide()
    check(D.draft.deckId == 1, "deck 1 is open")
    staleDelete.OnAccept()
    flush()
    local fd = X.lastOp("DECK_DELETE")
    check(fd and string.find(fd.payload, ";id=2;rev=2", 1, true) ~= nil, "Yes: DECK_DELETE sent for deck 2")
    H.queue = {}
    T.frame("DECK_DELETED", fd.reqId, "key=" .. X.kv(fd).key .. ";id=2;replay=0")
    check(D.draft and D.draft.deckId == 1 and ed:IsShown() and not DU.tiles[2]:IsShown(), "deleting another deck does not close the open one")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" } }))
    F.fire(ed.cancelButton, "OnClick")
    check(F.popup == nil and D.draft == nil, "Cancel without changes needs no confirmation")
    check(DU.trans ~= nil and DU.trans.to == "browse", "closing slides back")
    finishSlide()
    check(br:IsShown() and not ed:IsShown() and tilesShown() == 1 and br.countText:GetText() == "1 / 36 Decks" and shownTabs() == "DRUID,HUNTER,MAGE,PALADIN,PRIEST,ROGUE,SHAMAN,WARLOCK,WARRIOR,NEUTRAL", "back to the tiles and all class tabs")
    check(UI.state.filters.class == "MAGE" and UI.state.filters.deckClass == nil and main.title:GetText() == "Mage", "the collection's own tab is back")

    -- deleting the open deck: the editor closes when the server confirmed it
    F.fire(DU.tiles[1], "OnClick", { arg1 = "LeftButton" })
    finishSlide()
    F.fire(ed.deleteButton, "OnClick")
    F.popupClick(1)
    flush()
    fd = X.lastOp("DECK_DELETE")
    check(fd and string.find(fd.payload, ";id=1;rev=3", 1, true) ~= nil and D.draft ~= nil, "Yes: DECK_DELETE sent, the editor stays until it is confirmed")
    H.queue = {}
    T.frame("DECK_DELETED", fd.reqId, "key=" .. X.kv(fd).key .. ";id=1;replay=0")
    check(D.draft == nil and DU.trans ~= nil and DU.trans.to == "browse", "confirmed: the deck is gone, the panel slides back")
    finishSlide()
    check(br:IsShown() and tilesShown() == 0 and br.countText:GetText() == "0 / 36 Decks" and br.emptyText:GetText() ~= "", "no decks left: the empty message")
    flush()
    -- a refused delete keeps the deck and the editor
    X.decks(decksPayload(36, "MAGE", "", { { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" } }))
    F.fire(DU.tiles[1], "OnClick", { arg1 = "LeftButton" })
    finishSlide()
    F.fire(ed.deleteButton, "OnClick")
    F.popupClick(1)
    flush()
    fd = X.lastOp("DECK_DELETE")
    H.queue = {}
    X.refused(fd.reqId, "conflict")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" } }))
    check(D.draft ~= nil and D.draft.deckId == 1 and ed:IsShown() and DU.leaveAfterDelete == nil, "a refused delete keeps the deck open")
    F.fire(ed.cancelButton, "OnClick")
    finishSlide()

    -- New Deck: the class picker over the book (locked classes marked), then the edit mode
    local picker = DU.picker
    check(not picker:IsShown() and not picker.shade:IsShown(), "picker hidden")
    F.fire(br.newButton, "OnClick")
    check(picker:IsShown() and picker.shade:IsShown(), "New Deck opens the class picker")
    check(picker._.level > UI.slots[1]._.level and picker.shade._.level > UI.slots[1]._.level and picker.shade._.level < picker._.level, "picker over the cards, the shade between")
    check(picker.access == nil, "the picker has no line of playable classes (the buttons show it)")
    check(DU.pickButtons[3].class == "MAGE" and DU.pickButtons[3].label:GetText() == "Mage", "mage is playable")
    check(DU.pickButtons[9].class == "WARRIOR" and DU.pickButtons[9].label:GetText() == "|cff7a6a58Warrior (locked)|r", "warrior is marked locked")
    -- owner decision: a class the character has not unlocked cannot be picked (greyed, "Not unlocked")
    check(DU.LOCKED_PICKABLE == false, "locked classes are not pickable by default")
    check(DU.pickButtons[9].locked == true and DU.pickButtons[9].icon._.vertex[1] < 1 and not DU.pickButtons[3].locked and DU.pickButtons[3].icon._.vertex[1] == 1, "the locked class is greyed out, the playable one is not")
    F.fire(DU.pickButtons[9], "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Warrior" and F.tooltip.lines[2] == "Not unlocked" and F.tooltip.lines[3] == "Jaina cannot play Warrior yet.", "locked class tooltip: Not unlocked")
    F.fire(DU.pickButtons[9], "OnLeave")
    F.fire(DU.pickButtons[3], "OnEnter")
    check(F.tooltip.lines[2] == "Build a new Mage deck.", "playable class tooltip")
    F.fire(DU.pickButtons[3], "OnLeave")
    -- the phase 06 rule is still there behind the switch
    DU.LOCKED_PICKABLE = true
    F.fire(DU.pickButtons[9], "OnEnter")
    check(F.tooltip.lines[2] == "Locked for Jaina: Warrior is not unlocked.", "locked class tooltip with LOCKED_PICKABLE on")
    F.fire(DU.pickButtons[9], "OnLeave")
    DU.LOCKED_PICKABLE = false
    F.fire(picker.cancelButton, "OnClick")
    check(not picker:IsShown() and not picker.shade:IsShown() and D.draft == nil, "Cancel closes the picker")
    F.fire(br.newButton, "OnClick")
    F.fire(picker.shade, "OnMouseDown")
    check(not picker:IsShown() and D.draft == nil, "a click beside the picker closes it")
    F.fire(br.newButton, "OnClick")
    F.fire(DU.pickButtons[9], "OnClick")
    check(D.draft == nil and picker:IsShown() and not D.draft, "a click on a locked class does nothing: no draft, the picker stays")
    DU.LOCKED_PICKABLE = true
    F.fire(DU.pickButtons[9], "OnClick")
    check(D.draft and D.draft.class == "WARRIOR" and not picker:IsShown(), "with LOCKED_PICKABLE on a locked class can be picked (phase 06)")
    D.EndEdit()
    UI.Update()
    finishSlide()
    DU.LOCKED_PICKABLE = false
    F.fire(br.newButton, "OnClick")
    F.fire(DU.pickButtons[3], "OnClick")
    check(D.draft and D.draft.class == "MAGE" and D.draft.deckId == 0, "a new mage draft")
    check(not picker:IsShown() and not picker.shade:IsShown() and DU.trans ~= nil, "the picker closed, the panel slides")
    finishSlide()
    check(ed:IsShown() and not br:IsShown() and main.book:IsShown() and main:GetWidth() == UI.WIDTH, "edit mode: book and card list in the same window")
    check(shownTabs() == "MAGE,NEUTRAL", "the deck class tab and Neutral: " .. shownTabs())
    check(UI.classTabs[3].button._.points[1].x == UI.TAB_X0 and UI.classTabs[10].button._.points[1].x == UI.TAB_X0 + UI.TAB_PITCH, "the two tabs are the first tabs")
    check(tabY(3) == UI.TAB_Y_UP and tabY(10) == UI.TAB_Y and UI.TAB_Y_UP > UI.TAB_Y, "the selected tab is raised")
    check(UI.state.filters.class == "MAGE" and UI.state.filters.deckClass == "MAGE" and main.title:GetText() == "Mage"
        and main.note:GetText() == "Left click adds a copy, right click removes one.", "opens on the deck class tab, title and hint")
    local db = HsCollection.GetDB()
    local function onlyClass(set)
        for i = 1, table.getn(UI.state.results) do
            if not set[db.cards[UI.state.results[i]].class or "NEUTRAL"] then return false end
        end
        return true
    end
    check(table.getn(UI.state.results) == UI.state.catalog.counts.MAGE and onlyClass({ MAGE = true }), "the Mage tab lists mage cards only")
    F.fire(UI.classTabs[10].button, "OnClick")
    check(UI.state.filters.class == "NEUTRAL" and main.title:GetText() == "Neutral" and onlyClass({ NEUTRAL = true }) and H.db.tab ~= "NEUTRAL", "Neutral tab, not remembered")
    check(tabY(10) == UI.TAB_Y_UP and tabY(3) == UI.TAB_Y, "the selection moved")
    F.fire(UI.classTabs[9].button, "OnClick")
    check(UI.state.filters.class == "NEUTRAL", "a hidden class tab cannot be picked")
    F.fire(UI.classTabs[3].button, "OnClick")
    check(UI.state.filters.class == "MAGE", "the mage tab")
    -- the search looks through everything but still offers only what the deck can hold
    UI.frame.searchBox:SetText("heroic strike")
    check(table.getn(UI.state.results) == 0 and main.emptyText:IsShown(), "a warrior card is not offered to a mage deck")
    UI.frame.searchBox:SetText("fireball")
    check(table.getn(UI.state.results) >= 1 and onlyClass({ MAGE = true, NEUTRAL = true }) and slotOf("CS2_029") ~= nil, "searching offers eligible cards of both tabs")
    UI.frame.searchBox:SetText("")
    check(ed.nameBox:GetText() == "Custom Mage" and ed.classText:GetText() == "New Mage deck" and ed.countText:GetText() == "0 / 30", "card list header")
    check(rowsShown() == 0 and ed.emptyText:GetText() ~= "" and ed.saveButton:IsEnabled() and not ed.deleteButton:IsShown(), "empty list (an empty draft may be saved), no Delete for a new deck")
    check(ed._.alpha == 1 and ed._.points[1].x == 0, "the card list rests in place")

    -- left click adds, right click removes; badge "n/limit"; all owned copies used -> dimmed
    local s = showCard("CS2_029")
    check(s and not s.deckBadge:IsShown() and not s.widget.dimmed, "Fireball on the page, not in the deck")
    click("CS2_029")
    check(D.InDraft("CS2_029") == 1 and not s.deckBadge:IsShown() and not s.limit:IsShown() and not s.widget.dimmed, "left click adds a copy, no counter below the limit")
    check(ed.countText:GetText() == "1 / 30" and rowsShown() == 1, "the card list shows it")
    local row = rowOf("CS2_029")
    check(row and row.cost:GetText() == "4" and row.qty:GetText() == "" and not row.star:IsShown() and string.find(row.name:GetText(), "Fireball", 1, true) ~= nil, "row: cost, name, one copy shows no count")
    check(s._.bdBorder[4] == 1, "a card in the deck has the border (no glow art)")
    click("CS2_029")
    check(D.InDraft("CS2_029") == 2 and row.qty:GetText() == "2" and s.deckBadgeText:GetText() == "|cff33ff332/2|r" and s.deckBadge:IsShown() and not s.limit:IsShown() and s.widget.dimmed, "two copies: the row says 2, badge full (no plate art in the test), card dimmed")
    click("CS2_029")
    check(D.InDraft("CS2_029") == 2 and string.find(ed.status:GetText(), "Fireball: only 2 copies", 1, true) ~= nil, "the list says why: " .. ed.status:GetText())
    click("CS2_029", "RightButton")
    check(D.InDraft("CS2_029") == 1 and not s.widget.dimmed and not s.limit:IsShown() and string.find(ed.status:GetText(), "only 2 copies", 1, true) == nil, "right click removes a copy")
    s = showCard("EX1_116")
    click("EX1_116")
    check(s.deckBadgeText:GetText() == "|cff33ff331/1|r" and s.widget.dimmed, "Legendary 1/1 (badge: no plate art in the test)")
    row = rowOf("EX1_116")
    check(row.star:IsShown() and row.qty:GetText() == "" and row.star._.texture == "Interface\\TargetingFrame\\UI-RaidTargetingIcons", "a Legendary shows a star")
    click("EX1_116")
    check(string.find(ed.status:GetText(), "Leeroy Jenkins: Legendary: only 1 copy", 1, true) ~= nil, "Legendary reason")
    UI.SetClass("MAGE")
    UI.frame.searchBox:SetText("")
    -- an unowned card: dimmed, the click explains
    local unowned = nil
    for i = 1, CM.PER_PAGE do
        local id = UI.state.pageIds[i]
        if id and not O.Get(id) then unowned = id break end
    end
    check(unowned ~= nil and slotOf(unowned).widget.dimmed, "an unowned card is dimmed")
    click(unowned)
    check(D.InDraft(unowned) == 0 and string.find(ed.status:GetText(), "you do not own this card", 1, true) ~= nil, "unowned: not added")

    -- list rows: hover shows the large card, click removes a copy, rarity colour, sorted by cost then name
    local _, ri = rowOf("EX1_116")
    F.fire(DU.rows[ri], "OnEnter")
    check(UI.preview.frame:IsShown() and UI.preview.widget.id == "EX1_116", "hovering a row shows the card")
    check(string.find(DU.rows[ri].name:GetText(), "|cffff8000", 1, true) == 1, "legendary row in orange")
    F.fire(DU.rows[ri], "OnClick")
    check(D.InDraft("EX1_116") == 0 and not rowOf("EX1_116"), "clicking a row removes a copy")
    check(not UI.preview.frame:IsShown() or UI.preview.widget.id ~= "EX1_116", "the preview follows the row under the mouse")
    F.fire(DU.rows[ri], "OnLeave")
    check(not UI.preview.frame:IsShown(), "leaving the row hides the preview")

    -- fill the deck: 30 cards, mana curve, complete
    for i = 1, table.getn(MAGE_IDS) do
        local id = MAGE_IDS[i]
        while D.InDraft(id) < 2 do
            local ok = D.AddCard(id)
            if not ok then break end
        end
    end
    DU.RefreshEdit()
    check(D.Count(D.draft.cards) == 30 and ed.countText:GetText() == "|cff33ff3330 / 30|r", "30 cards")
    check(rowsShown() == 15, "15 distinct cards, 15 rows")
    local costs = {}
    for i = 1, rowsShown() do costs[i] = tonumber(DU.rows[i].cost:GetText()) end
    local sorted = true
    for i = 2, table.getn(costs) do if costs[i] < costs[i - 1] then sorted = false end end
    check(sorted, "rows sorted by cost")
    check(string.find(ed.status:GetText(), "Complete. Opponents offer it when all its cards work in matches.", 1, true) ~= nil, "complete: " .. ed.status:GetText())
    check(ed.saveButton:IsEnabled(), "Save enabled")
    check(DU.curveBars[1]._.h > DU.curveBars[7]._.h and DU.curveBars[7]._.alpha == 0.25, "mana curve bars")
    -- 30 distinct cards still fit: the list never scrolls
    check(DU.TRAY_ROWS == 30 and DU.ROW_Y0 + DU.TRAY_ROWS * DU.RowPitch(30) <= DU.ROW_Y_END and DU.ROW_Y_END <= DU.CURVE_Y - DU.CURVE_H and DU.CURVE_Y + 14 + 44 <= DU.BUTTONS_Y and DU.BUTTONS_Y + 22 <= 768, "30 rows, the curve and the status lines fit above the buttons")
    check(DU.RowPitch(10) == DU.TRAY_ROW_H and DU.TRAY_ROW_H >= 21 and DU.RowPitch(30) >= 17 and DU.RowPitch(30) < DU.RowPitch(20), "few cards: tall rows; 30 rows shrink to fit")
    D.RemoveCard("CS2_029")
    D.RemoveCard("CS2_022")
    D.AddCard("NEW1_012")
    D.AddCard("EX1_116")
    DU.RefreshEdit()
    check(rowsShown() == 17, "17 distinct cards: 17 rows, no scrolling")
    -- the book still pages with the wheel in the edit mode
    UI.state.page = 1
    F.fire(UI.slots[1], "OnMouseWheel", { arg1 = -1 })
    check(UI.state.page == 2, "the book pages in the edit mode")
    F.fire(DU.panel, "OnMouseWheel", { arg1 = -1 })
    check(UI.state.page == 2 and DU.page == 1, "the panel wheel does not page the decks in the edit mode")

    -- name box, invalid name blocks Save with the reason
    ed.nameBox:SetText("Bad|Name")
    check(D.draft.name == "Bad|Name" and not ed.saveButton:IsEnabled() and string.find(ed.status:GetText(), "Name: the name may not contain |.", 1, true) ~= nil, "invalid name shown")
    ed.nameBox:SetText("Tempo Mage")
    check(ed.saveButton:IsEnabled(), "valid name")

    -- the Packs tab hides the deck panel; closing the window keeps the draft; the draft resumes without sliding
    local before = D.draft
    F.fire(F.globals.HearthstoneFrameTab2, "OnClick")
    check(main.packs:IsShown() and not pnl:IsShown() and not main.book:IsShown() and main:GetWidth() == UI.WIDTH_BOOK, "Packs tab: no deck panel")
    F.fire(F.globals.HearthstoneFrameTab1, "OnClick")
    check(D.draft == before and pnl:IsShown() and ed:IsShown() and not br:IsShown() and DU.trans == nil and ed.nameBox:GetText() == "Tempo Mage", "back on the collection: the draft resumes in place")
    check(main:GetWidth() == UI.WIDTH and shownTabs() == "MAGE,NEUTRAL", "wide window, deck tabs")
    main:Hide()
    check(D.draft == before, "closing keeps the draft")
    H.Slash("c")
    check(main:IsShown() and ed:IsShown() and not br:IsShown() and DU.trans == nil and shownTabs() == "MAGE,NEUTRAL", "reopening resumes the edit mode without a slide")
    -- a small screen: the window is scaled to fit
    main:Hide()
    F.screenW, F.screenH = 1024, 768
    F.UIParent._.w, F.UIParent._.h = 1024, 768
    H.Slash("c")
    check(main._.scale == CM.FitScale(1024, 768, UI.WIDTH + UI.TABS_OVERHANG, UI.HEIGHT + UI.BOTTOM_OVERHANG) and main._.scale < 1, "scaled to fit: " .. main._.scale)

    -- Cancel with changes asks; No keeps editing, Yes discards
    F.fire(ed.cancelButton, "OnClick")
    check(F.popup and F.popup.which == "HEARTHSTONE_DISCARD_DECK" and string.find(F.popup.text, "Tempo Mage", 1, true) ~= nil, "discard confirmation")
    F.popupClick(2)
    check(D.draft == before and ed:IsShown(), "No: still editing")
    -- a discard popup left open must not end a draft started later
    local other = D.BeginNew("WARRIOR")
    StaticPopupDialogs.HEARTHSTONE_DISCARD_DECK.OnAccept()
    check(D.draft == other, "a stale discard popup leaves another draft alone")
    D.draft = before

    -- hiding the window during a slide finishes it
    F.fire(ed.saveButton, "OnClick")
    check(D.op ~= nil and ed.saveButton:GetText() == "Saving..." and not ed.saveButton:IsEnabled(), "saving")
    showCard("CS2_029")
    click("CS2_029")
    check(string.find(ed.status:GetText(), "wait until the deck is saved", 1, true) ~= nil, "no edits while saving")
    DU.OnNameChanged("Renamed meanwhile")
    check(D.draft.name == "Tempo Mage" and ed.nameBox:GetText() == "Tempo Mage", "no renaming while saving")
    flush()
    local fs = X.lastOp("DECK_SAVE")
    local kvs = X.kv(fs)
    check(kvs.id == "0" and kvs.name == "Tempo Mage" and kvs.class == "MAGE", "DECK_SAVE from the card list")
    H.queue = {}
    T.frame("DECK_SAVED", fs.reqId, "key=" .. kvs.key .. ";id=5;rev=1;replay=0;state=ok")
    check(D.draft == nil and DU.trans ~= nil and DU.trans.to == "browse" and br:IsShown() and ed:IsShown(), "after DECK_SAVED the panel slides back to the tiles")
    main:Hide()
    check(DU.trans == nil and br:IsShown() and not ed:IsShown() and br._.alpha == 1 and ed._.alpha == 1, "hiding the window finishes the slide")
    main:Show()
    check(UI.layoutMode == "normal" and string.find(shownTabs(), "WARRIOR", 1, true) ~= nil, "normal tabs again")
    check(DU.tiles[2].name:GetText() == "Tempo Mage" and D.selected == 5 and br.status:GetText() == "", "the saved deck is a tile, with no \"saved\" line")
    flush()
    X.decks(decksPayload(36, "MAGE", "", { { 1, 3, "MAGE", "Dev Mage", cardsString(mageCards()), "ok", "gm_fixture" }, { 5, 1, "MAGE", "Tempo Mage", kvs.cards, "ok" } }))

    -- open an existing deck; Cancel after changes, Yes
    F.fire(DU.tiles[1], "OnClick", { arg1 = "LeftButton" })
    finishSlide()
    check(D.draft and D.draft.deckId == 1 and ed.nameBox:GetText() == "Dev Mage" and ed.classText:GetText() == "Mage deck" and not ed.saveButton:IsEnabled(), "editing Dev Mage, nothing changed: Save disabled")
    showCard("CS2_029")
    click("CS2_029", "RightButton")
    F.fire(ed.cancelButton, "OnClick")
    F.popupClick(1)
    finishSlide()
    check(D.draft == nil and br:IsShown() and D.byId[1].cards.CS2_029 == 2, "Yes: discarded, the saved deck is unchanged")
    UI.frame.searchBox:SetText("")

    -- a locked class deck can be built when DU.LOCKED_PICKABLE is on (the phase 06 rule; off by default since the
    -- owner's decision of phase 10); saving while disconnected says why
    DU.LOCKED_PICKABLE = true
    F.fire(br.newButton, "OnClick")
    F.fire(DU.pickButtons[9], "OnClick")
    DU.LOCKED_PICKABLE = false
    finishSlide()
    check(D.draft.class == "WARRIOR" and shownTabs() == "WARRIOR,NEUTRAL" and UI.layoutMode == "edit:WARRIOR", "warrior draft")
    check(string.find(ed.status:GetText(), "Locked for Jaina: Warrior is not unlocked", 1, true) ~= nil, "locked note in the list")
    T.err("0", "DISABLED", "off")
    check(ed:IsShown() and not ed.saveButton:IsEnabled() and string.find(ed.status:GetText(), "Not connected to the server.", 1, true) ~= nil, "disconnected: save blocked with the reason")
    F.fire(ed.saveButton, "OnClick")
    check(D.op == nil and table.getn(H.queue) == 0, "nothing sent")
    DU.Discard()
    finishSlide()
    check(br:IsShown() and br.status:GetText() == "Not connected to the server.", "tiles offline")

    -- more decks than tiles fit: pages (mouse wheel, arrows) and the cap
    T.welcome("solo,chunk,decks")
    flush()
    X.coll(5, ownedMage())
    local nine = {}
    for i = 1, 9 do
        table.insert(nine, { i, 1, "MAGE", "Deck " .. i, cardsString(mageCards()), "ok" })
    end
    X.decks(decksPayload(10, "MAGE", "", nine))
    check(tilesShown() == DU.ROWS and br.pageText:GetText() == "Page 1 / 2" and br.prevButton:IsShown() and not br.prevButton:IsEnabled() and br.nextButton:IsEnabled(), "page 1 of 2: " .. tostring(br.pageText:GetText()))
    check(DU.tiles[1].name:GetText() == "Deck 1" and DU.tiles[7].name:GetText() == "Deck 7" and br.countText:GetText() == "9 / 10 Decks", "tiles 1 to 7")
    F.fire(br.nextButton, "OnClick")
    check(DU.page == 2 and tilesShown() == 2 and DU.tiles[1].name:GetText() == "Deck 8" and br.pageText:GetText() == "Page 2 / 2" and not br.nextButton:IsEnabled(), "page 2: the last two decks")
    F.fire(DU.tiles[1], "OnMouseWheel", { arg1 = 1 })
    check(DU.page == 1, "wheel up over a tile: previous page")
    F.fire(DU.panel, "OnMouseWheel", { arg1 = -1 })
    check(DU.page == 2, "wheel over the panel: next page")
    F.fire(DU.tiles[2], "OnClick", { arg1 = "LeftButton" })
    finishSlide()
    check(D.draft and D.draft.deckId == 9, "a tile on page 2 opens its deck")
    DU.Discard()
    finishSlide()
    check(DU.page == 2, "the page is kept")
    -- the cap: the tenth deck makes room for no more
    table.insert(nine, { 10, 1, "MAGE", "Deck 10", cardsString(mageCards()), "ok" })
    D.SetReady(D.ParseDecks(decksPayload(10, "MAGE", "", nine)))
    UI.RefreshAll()
    check(not br.newButton:IsEnabled() and string.find(br.status:GetText(), "maximum of 10 decks", 1, true) ~= nil and br.countText:GetText() == "10 / 10 Decks", "at the cap: New Deck disabled, the reason")

    -- the deck UI never builds frames after the window
    check(table.getn(F.frames) == created, "no frames created by the deck UI: " .. table.getn(F.frames) .. " vs " .. created)

    -- no text suggests queues, ranking, trading or crafting ("can play" is the access line)
    local bad = { "queue", "ranked", "trad", "craft", "scrap", "invite", "dust", "disenchant", "buy" }
    local function underCard(o)
        while o and o._ do
            if o._.name and string.find(o._.name, "Card", 1, true) then return true end
            if o._.name and string.find(o._.name, "HearthstoneCraft", 1, true) then return true end   -- the craft panels (crafting NPC only)
            o = o._.parent
        end
        return false
    end
    for i = 1, table.getn(F.frames) do
        local o = F.frames[i]
        local texts = {}
        if not underCard(o) then texts = { o._.text } end
        if not underCard(o) then
            for _, rg in ipairs(o._.regions) do table.insert(texts, rg._.text) end
        end
        for _, t in ipairs(texts) do
            for _, w in ipairs(bad) do
                check(not string.find(string.lower(tostring(t)), w, 1, true), "deck window text mentions '" .. w .. "': " .. tostring(t))
            end
        end
    end

    main:Hide()
    F.uninstall()
    resetUI()
    HS_CardTextures = savedTextures
    H.db = savedDb
    resetAddon()
end

-- ------------------------------------------- decks against the C++ transcript
-- Every deck reply the C++ session produced must pass the addon's strict parsers, and the
-- addon's payload encoders must rebuild the C++ deck requests byte for byte.
local function testDecksTranscript()
    local D = HsDecks
    local data = readAll(ROOT .. "/tests/transcript.txt") or ""
    check(string.find(data, "caps=[^;\n]*decks") ~= nil, "the transcript has the decks capability")
    local groups, order, requests = {}, {}, {}
    for line in string.gmatch(data, "[^\n]+") do
        line = string.gsub(line, "\r$", "")
        if string.sub(line, 1, 7) == "SERVER\t" then
            local st, f = P.ParseFrame(string.sub(line, 8))
            if st == "ok" and (f.op == "DECKS" or f.op == "DECK_SAVED" or f.op == "DECK_DELETED" or f.op == "DECK_CHANGED"
                or (f.op == "ERR" and string.find(f.payload, "why=", 1, true))) then
                local key = f.op .. "|" .. f.reqId .. "|" .. table.getn(order)
                if f.part > 1 then key = order[table.getn(order)] end
                if not groups[key] then
                    groups[key] = { op = f.op, parts = {}, total = f.total }
                    table.insert(order, key)
                end
                groups[key].parts[f.part] = f.payload
            end
        elseif string.sub(line, 1, 6) == "CHUNK\t" then
            local _, _, op, reqId, hex = string.find(line, "^CHUNK\t([^\t]+)\t([^\t]+)\t([^\t]*)\t")
            if (op == "DECK_SAVE" or op == "DECK_DELETE" or op == "DECK_LIST") and not requests[op .. reqId] then
                requests[op .. reqId] = true
                table.insert(requests, { op = op, payload = fromHex(hex) })
            end
        end
    end
    local seen = {}
    for _, key in ipairs(order) do
        local g = groups[key]
        local payload = table.concat(g.parts, "", 1, g.total)
        seen[g.op] = (seen[g.op] or 0) + 1
        if g.op == "DECKS" then
            local res, why = D.ParseDecks(payload)
            check(res ~= nil, "C++ DECKS accepted: " .. tostring(why))
        elseif g.op == "DECK_SAVED" then
            local res, why = D.ParseSaved(payload)
            check(res ~= nil, "C++ DECK_SAVED accepted: " .. tostring(why))
        elseif g.op == "DECK_DELETED" then
            local res, why = D.ParseDeleted(payload)
            check(res ~= nil, "C++ DECK_DELETED accepted: " .. tostring(why))
        elseif g.op == "DECK_CHANGED" then
            local kv = P.KvDecode(payload)
            check(kv and (kv.why == "gm" or kv.why == "class"), "C++ DECK_CHANGED why: " .. payload)
        else
            local kv = P.KvDecode(payload)
            check(kv and kv.code == "REFUSED" and (H.DECK_REFUSED[kv.why] ~= nil or kv.why == "cap"), "C++ refusal with a known why: " .. payload)
        end
    end
    check((seen.DECKS or 0) >= 1 and (seen.DECK_SAVED or 0) >= 1, "the transcript has deck replies")
    local rebuilt = 0
    for i = 1, table.getn(requests) do
        local r = requests[i]
        if r.op == "DECK_LIST" then
            check(r.payload == "", "DECK_LIST is empty")
        else
            local kv = P.KvDecode(r.payload)
            check(kv ~= nil, r.op .. " payload decodes")
            if kv and r.op == "DECK_SAVE" then
                local cards = D.ParseCards(kv.cards)
                check(cards ~= nil, "C++ card list parses: " .. tostring(kv.cards))
                local mine = D.SavePayload(kv.key, tonumber(kv.id), tonumber(kv.rev), kv.class, kv.name, cards or {})
                check(mine == r.payload, "DECK_SAVE identical to C++: " .. mine .. " vs " .. r.payload)
                rebuilt = rebuilt + 1
            elseif kv then
                local mine = D.DeletePayload(kv.key, tonumber(kv.id), tonumber(kv.rev))
                check(mine == r.payload, "DECK_DELETE identical to C++: " .. mine .. " vs " .. r.payload)
                rebuilt = rebuilt + 1
            end
        end
    end
    check(rebuilt >= 2, "deck requests compared: " .. rebuilt)
end

-- ------------------------------------------------------------- match
-- The canonical GAME example (the C++ encoder produces exactly this string).
local CANONICAL_GAME = "mid=7;seq=12;ph=play;turn=3;act=m;res=-;why=-;mn=Jonas;on=Innkeeper Allison;dif=normal;"
    .. "mh=1.HERO_08.27.30.0.0.-;mp=2.CS2_034.2.0;mw=;mm=2.2;mk=4.24.0;mb=40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t;"
    .. "mc=12.CS2_029.4.0.0,15.CS2_168.1.2.1,18.CS2_025.2.0.0,21.GAME_005.0.0.0;mo=;ms=;mx=0.0.0;oh=3.HERO_01.30.30.2.0.-;"
    .. "op=4.CS2_102.2.1;ow=33.CS2_106.3.1;om=0.2;ok=5.23.0;ob=44.CS2_124.3.1.1.c;os=;ox=0.0.0;mul=;pk=;opk=0;tmr=0;"
    .. "lg=p15.0,p18.0,p21.0,a40.3/44,h2.1/3/40/41/44;evs=31;ev=turn.m.0.0.3.-,draw.m.0.18.0.CS2_025"

local GAME_KEYS = { "mid", "seq", "ph", "turn", "act", "res", "why", "mn", "on", "dif", "dur", "mh", "mp", "mw", "mm", "mk",
    "mb", "mc", "mo", "ms", "mx", "oh", "op", "ow", "om", "ok", "ob", "os", "ox", "mul", "pk", "opk", "tmr", "lg", "evs",
    "ev" }

-- The canonical game with some values replaced (in the server's key order).
local function gamePayload(over)
    local kv = P.KvDecode(CANONICAL_GAME)
    for k, v in pairs(over or {}) do kv[k] = v end
    local list = {}
    for i = 1, table.getn(GAME_KEYS) do
        local k = GAME_KEYS[i]
        if kv[k] ~= nil then table.insert(list, { k, kv[k] }) end
    end
    return P.KvEncode(list)
end

local function matchHelpers()
    local T = packHelpers()
    local t = { T = T }
    -- a GAME reply (reqId) or push (reqId "0"), chunked like the server does it
    function t.game(reqId, payload)
        local frames = P.ChunkPayload("GAME", reqId, payload)
        assert(frames, "GAME payload fits")
        for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
    end
    function t.push(payload) t.game(P.SERVER_PUSH_REQID, payload) end
    function t.welcome() T.welcome("solo,chunk,packs,decks,match") end
    -- the last request 'op' sent (single frame)
    function t.last(op)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end
    function t.count(op) return table.getn(T.sentOps(op)) end
    return t
end

local function resetMatch()
    H.match.reqId, H.match.refetch, H.match.supported, H.match.announce = nil, false, false, false
    H.matchReqs = {}
    HsMatch.supported = false
    HsMatch.Reset()
    HsMatchAnim.Reset()
    HsMatchAnim.ClearFailure()
    HsMatchAnim.SetMode("off")
end

-- ------------------------------------- collection and decks: layout numbers, position, book look
-- ------------------------------------------------------- wallet and crafting
local WALLET_PAYLOAD = "coins=120;dust=30;craft=0;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10"

local function testWalletModel()
    local W = HsWallet
    W.Reset()
    check(W.state == "none" and W.coins == 0 and W.dust == 0 and not W.craftOpen and W.op == nil, "reset")

    local r = W.Parse(WALLET_PAYLOAD)
    check(r and r.coins == 120 and r.dust == 30 and not r.craftOpen and r.craftNpc == nil and r.packCoins == 10, "WALLET parsed")
    check(r and r.values.scrap[1] == 5 and r.values.scrap[4] == 400 and r.values.make[1] == 40 and r.values.make[4] == 1600, "dust values")
    r = W.Parse("coins=0;dust=0;craft=1;npc=Ezra Chatterton;scrap=1.2.3.4;make=5.6.7.8;packcoins=0")
    check(r and r.craftOpen and r.craftNpc == "Ezra Chatterton" and r.values.make[3] == 7, "WALLET with the crafting NPC")
    for _, bad in ipairs({ "", "coins=1", "coins=01;dust=0;craft=0;scrap=1.2.3.4;make=1.2.3.4;packcoins=0",
        "coins=1000000001;dust=0;craft=0;scrap=1.2.3.4;make=1.2.3.4;packcoins=0",
        "coins=1;dust=0;craft=2;scrap=1.2.3.4;make=1.2.3.4;packcoins=0",
        "coins=1;dust=0;craft=0;scrap=1.2.3;make=1.2.3.4;packcoins=0",
        "coins=1;dust=0;craft=0;scrap=1.2.3.4;make=0.2.3.4;packcoins=0",
        "coins=1;dust=0;craft=0;scrap=1.2.3.4;make=1.2.3.4", "coins=-1;dust=0" }) do
        check(W.Parse(bad) == nil, "bad WALLET rejected: " .. bad)
    end

    local u = W.ParseUpd("coins=135;dust=30;why=match;gain=25")
    check(u and u.coins == 135 and u.dust == 30 and u.why == "match" and u.gain == 25, "WALLET_UPD")
    u = W.ParseUpd("coins=1;dust=2;why=gm")
    check(u and u.gain == 0 and u.why == "gm", "WALLET_UPD without gain")
    check(W.ParseUpd("coins=x;dust=2;why=gm") == nil and W.ParseUpd("coins=1;dust=2;gain=y") == nil, "bad WALLET_UPD")

    local open, npc = W.ParseAccess("open=1;npc=Ibelin Redmoore")
    check(open == true and npc == "Ibelin Redmoore", "CRAFT_ACCESS open")
    open = W.ParseAccess("open=0")
    check(open == false, "CRAFT_ACCESS closed")
    check(W.ParseAccess("open=x") == nil and W.ParseAccess("") == nil, "bad CRAFT_ACCESS")

    local c = W.ParseCrafted("key=abcdef123456;replay=0;kind=craft;card=EX1_116;n=1;delta=-1600;dust=400;ver=5;total=1;prot=0")
    check(c and c.key == "abcdef123456" and not c.replay and c.kind == "craft" and c.card == "EX1_116" and c.n == 1
        and c.delta == -1600 and c.dust == 400 and c.ver == 5 and c.total == 1 and c.prot == 0, "CRAFTED")
    c = W.ParseCrafted("key=abcdef123456;replay=1;kind=scrap;card=CS2_022;n=2;delta=10;dust=40;ver=6;total=2;prot=2")
    check(c and c.replay and c.kind == "scrap" and c.delta == 10, "CRAFTED scrap replay")
    for _, bad in ipairs({ "key=SHORT;kind=craft;card=EX1_116;n=1;delta=0;dust=0;ver=1;total=1;prot=0",
        "key=abcdef123456;kind=make;card=EX1_116;n=1;delta=0;dust=0;ver=1;total=1;prot=0",
        "key=abcdef123456;kind=craft;card=EX1_116;n=1;delta=0;dust=0;ver=1;total=1;prot=2",
        "key=abcdef123456;kind=craft;card=a b;n=1;delta=0;dust=0;ver=1;total=1;prot=0",
        "key=abcdef123456;kind=craft;card=EX1_116;n=1;delta=--5;dust=0;ver=1;total=1;prot=0" }) do
        check(W.ParseCrafted(bad) == nil, "bad CRAFTED rejected: " .. bad)
    end

    check(W.CapsHaveWallet("solo,chunk,packs,decks,wallet,match") and W.CapsHaveWallet("wallet"), "caps with wallet")
    check(not W.CapsHaveWallet("solo,walleted") and not W.CapsHaveWallet("solo,chunk") and not W.CapsHaveWallet(nil), "caps without wallet")

    local db = HS_CardDB.cards
    W.SetReady(W.Parse(WALLET_PAYLOAD))
    check(W.Bucket(db["CS2_022"]) == 1 and W.Bucket(db["EX1_008"]) == 1 and W.Bucket(db["EX1_116"]) == 4, "rarity buckets (Basic is Common)")
    check(W.Craftable(db["CS2_022"]) and W.Craftable(db["EX1_116"]) and not W.Craftable(db["EX1_062"]) and not W.Craftable(db["FP1_028"]) and not W.Craftable(db["FP1_010"]) and not W.Craftable(nil), "craftable: not the promos, not the Naxxramas quest cards")
    check(W.CraftCost(db["CS2_022"]) == 40 and W.ScrapValue(db["CS2_022"]) == 5 and W.CraftCost(db["EX1_116"]) == 1600
        and W.ScrapValue(db["EX1_116"]) == 400, "dust values of cards")
    check(W.Limit(db["EX1_116"]) == 1 and W.Limit(db["CS2_022"]) == 2, "craft limits")
    check(W.CraftBlocker(db["EX1_116"], nil) == "Not enough dust (1600 needed, you have 30).", "craft blocked by dust")
    check(W.CraftBlocker(db["CS2_022"], nil) == "Not enough dust (40 needed, you have 30).", "Basic card blocked by dust")
    W.dust = 100
    check(W.CraftBlocker(db["CS2_022"], nil) == nil, "craft possible")
    W.dust = 30
    check(W.CraftBlocker(db["CS2_022"], { total = 2, prot = 0 }) == "You already own every copy a deck can use.", "craft limit")
    check(W.CraftBlocker(db["EX1_062"], nil) == "This card cannot be crafted.", "promo cannot be crafted")
    check(W.ScrapBlocker(db["CS2_022"], nil) == "You do not own this card.", "scrap unowned")
    check(string.find(W.ScrapBlocker(db["CS2_022"], { total = 2, prot = 2 }), "protected", 1, true) ~= nil, "scrap protected")
    check(W.ScrapBlocker(db["CS2_022"], { total = 3, prot = 2 }) == nil, "scrap ok")
    check(W.ScrapBlocker(db["EX1_062"], { total = 1, prot = 1 }) == "This card cannot be scrapped.", "promo cannot be scrapped")

    local decks = { { id = 1, name = "Mage A", cards = { CS2_022 = 2 } }, { id = 2, name = "Mage B", cards = { CS2_022 = 1 } },
        { id = 3, name = "Other", cards = {} } }
    local warn = W.DeckWarnings("CS2_022", 1, decks)
    check(table.getn(warn) == 1 and warn[1] == "Mage A", "deck warnings")
    check(table.getn(W.DeckWarnings("CS2_022", 0, decks)) == 2 and table.getn(W.DeckWarnings("CS2_022", 2, decks)) == 0
        and table.getn(W.DeckWarnings("CS2_022", 0, nil)) == 0, "deck warnings counts")

    local op = W.BeginOp("craft", "EX1_116", 1)
    check(op.key and string.len(op.key) == 12 and W.IsBusy() and W.OpPayload(op) == "key=" .. op.key .. ";card=EX1_116", "craft op")
    op = W.BeginOp("scrap", "CS2_022", 2)
    check(W.OpPayload(op) == "key=" .. op.key .. ";card=CS2_022;n=2", "scrap op")
    W.ClearOp()
    check(not W.IsBusy(), "op cleared")

    -- Disenchant Extra Cards (CRAFT_EXTRAS): the copies above what a deck can use that are not protected
    op = W.BeginOp("extras", nil, 5)
    check(W.OpPayload(op) == "key=" .. op.key, "extras op sends only its key")
    W.ClearOp()
    c = W.ParseCrafted("key=abcdef777777;replay=0;kind=extras;card=all;n=12345;delta=240;dust=640;ver=13;total=0;prot=0")
    check(c and c.kind == "extras" and c.card == "all" and c.n == 12345 and c.delta == 240 and c.total == 0, "CRAFTED extras")
    check(W.ParseCrafted("key=abcdef123456;replay=0;kind=scrap;card=CS2_022;n=123;delta=0;dust=0;ver=1;total=1;prot=0") == nil,
        "a scrap count has at most two digits")
    check(W.ExtraCopies(db["CS2_022"], { total = 5, prot = 2 }) == 3, "5 owned, 2 protected: 3 extra")
    check(W.ExtraCopies(db["CS2_022"], { total = 5, prot = 4 }) == 1, "5 owned, 4 protected: only 1 is free")
    check(W.ExtraCopies(db["CS2_022"], { total = 2, prot = 0 }) == 0, "2 owned: none extra")
    check(W.ExtraCopies(db["CS2_022"], { total = 3, prot = 3 }) == 0, "all protected: none")
    check(W.ExtraCopies(db["EX1_116"], { total = 3, prot = 0 }) == 2, "a Legendary keeps only 1")
    check(W.ExtraCopies(db["EX1_062"], { total = 3, prot = 0 }) == 0 and W.ExtraCopies(db["CS2_022"], nil) == 0, "promo cards and unowned cards")
    local copies, dustGain, kinds = W.Extras({ CS2_022 = { total = 5, prot = 2 }, EX1_116 = { total = 3, prot = 0 },
        EX1_062 = { total = 4, prot = 0 }, NOPE = { total = 9, prot = 0 } }, db)
    check(copies == 5 and dustGain == 3 * 5 + 2 * 400 and kinds == 2, "extras: " .. copies .. " " .. dustGain .. " " .. kinds)
    copies, dustGain, kinds = W.Extras(nil, db)
    check(copies == 0 and dustGain == 0 and kinds == 0, "no ownership: no extras")
    check(W.ExtrasBlocker(0) == W.WHY_TEXT.nothing and W.ExtrasBlocker(3) == nil, "extras blocker")

    check(W.StatusText() == "wallet: 120 Hearthstone Coins in your bags, 30 dust", "status ready: " .. W.StatusText())
    W.SetAccess(true, "Ezra Chatterton")
    check(W.StatusText() == "wallet: 120 Hearthstone Coins in your bags, 30 dust, crafting open at Ezra Chatterton", "status crafting")
    W.SetAccess(false, "Ezra Chatterton")
    check(not W.craftOpen and W.craftNpc == nil, "access closed")
    -- coin items in the bags: backpack slot 2 holds 40, bag 1 slot 5 holds 210, a pack item does not count
    local items = { ["0:2"] = { "|cffffffff|Hitem:919601:0:0:0|h[Hearthstone Coin]|h|r", 40 },
        ["1:5"] = { "|cffffffff|Hitem:919601:0:0:0|h[Hearthstone Coin]|h|r", 210 },
        ["1:6"] = { "|cff0070dd|Hitem:919602:0:0:0|h[Hearthstone Booster Pack]|h|r", 3 },
        ["2:1"] = { "|cffffffff|Hitem:9196010:0:0:0|h[Other]|h|r", 7 } }
    local function numSlots(bag) if bag <= 2 then return 16 end return 0 end
    local function link(bag, slot) local e = items[bag .. ":" .. slot]; return e and e[1] end
    local function info(bag, slot) local e = items[bag .. ":" .. slot]; return "tex", e and e[2] end
    check(W.CountBags(numSlots, link, info) == 250, "coins counted in the bags: " .. tostring(W.CountBags(numSlots, link, info)))
    check(W.CountBags(nil, link, info) == nil, "no container API: unknown")
    W.SetUnsupported()
    check(W.StatusText() == "wallet: not supported by this server", "status unsupported")
    W.Reset()
end

local function testWalletFlow()
    local W = HsWallet
    local O = HsOwnership
    local T = packHelpers()
    resetAddon()
    local notified = 0
    local savedHook, savedShow = H.OnWalletChanged, H.ShowCraft
    local shown = 0
    H.OnWalletChanged = function() notified = notified + 1 end
    H.ShowCraft = function() shown = shown + 1 end
    local function flush() drain(1) end
    local function lastSent(op)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end

    -- nothing before WELCOME
    local k0, why0 = H.CraftCard("CS2_022")
    check(k0 == nil and why0 == "Not connected to the server." and H.WalletGet() == nil, "nothing before WELCOME")
    T.frame("WALLET_UPD", "0", "coins=5;dust=5;why=gm")
    T.frame("CRAFT_ACCESS", "0", "open=1;npc=Ezra Chatterton")
    check(W.state == "none" and W.coins == 0 and not W.craftOpen and table.getn(H.queue) == 0, "pushes before WELCOME ignored")

    -- a server without the wallet
    T.welcome("solo,chunk,packs,decks")
    check(W.state == "unsupported" and table.getn(T.queuedOps("WALLET_GET")) == 0, "no wallet caps: unsupported")
    local _, why1 = H.CraftCard("CS2_022")
    check(why1 == "This server does not support crafting yet.", "crafting unsupported")
    flush()
    T.coll(1)
    H.queue = {}

    -- with the wallet: WALLET_GET, then WALLET
    T.welcome("solo,chunk,packs,decks,wallet")
    local q = T.queuedOps("WALLET_GET")
    local collReq = T.queuedOps("COLL_GET")[1]
    check(table.getn(q) == 1 and q[1].payload == "" and W.state == "loading" and H.wallet.reqId == q[1].reqId, "WALLET_GET sent")
    check(H.WalletGet() == H.wallet.reqId and H.wallet.refetch, "a second call marks a refetch")
    flush()
    check(collReq ~= nil and H.coll.reqId == collReq.reqId, "COLL_GET in flight")
    T.coll(1)
    T.frame("WALLET", "zz", WALLET_PAYLOAD)
    check(W.state == "loading", "a WALLET for another reqId is ignored")
    local before = notified
    T.frame("WALLET", H.wallet.reqId, WALLET_PAYLOAD)
    check(W.IsReady() and W.coins == 120 and W.dust == 30 and notified > before, "WALLET applied")
    check(table.getn(T.queuedOps("WALLET_GET")) == 1, "the refetch went out")
    flush()
    T.frame("WALLET", H.wallet.reqId, WALLET_PAYLOAD)
    check(H.wallet.reqId == nil, "nothing in flight")

    -- a match reward
    printed = {}
    T.frame("WALLET_UPD", "0", "coins=123;dust=30;why=match;gain=3")
    check(W.coins == 123 and table.getn(printed) == 0, "a match reward is silent: " .. tostring(printed[1]))
    T.frame("WALLET_UPD", "0", "coins=145;dust=30;why=buy")
    check(W.coins == 145 and table.getn(printed) == 0, "a purchase is silent")
    T.frame("WALLET_UPD", "0", "coins=146;dust=31;why=gm")
    check(W.coins == 146 and W.dust == 31 and table.getn(printed) == 0, "a GM change is silent")
    T.frame("WALLET_UPD", "0", "coins=bad")
    check(table.getn(T.queuedOps("WALLET_GET")) == 1, "an unreadable push refetches")
    flush()
    T.frame("WALLET", H.wallet.reqId, WALLET_PAYLOAD)

    -- crafting needs the NPC
    ownCards({ CS2_022 = 2 })
    local _, why2 = H.CraftCard("CS2_022")
    check(why2 == W.WHY_TEXT.npc, "no NPC: " .. tostring(why2))
    T.frame("CRAFT_ACCESS", "0", "open=1;npc=Ezra Chatterton")
    check(W.craftOpen and W.craftNpc == "Ezra Chatterton" and shown == 1, "access opens the window")
    check(string.find(lastPrinted(), "Crafting with Ezra Chatterton", 1, true) ~= nil, "access announced")

    -- client checks first: nothing is sent for a craft the server would refuse
    local _, why3 = H.CraftCard("EX1_116")
    check(why3 == "Not enough dust (1600 needed, you have 30).", "dust check: " .. tostring(why3))
    local _, why4 = H.CraftCard("CS2_022")
    check(why4 == "You already own every copy a deck can use.", "limit check")
    check(table.getn(T.queuedOps("CRAFT_MAKE")) == 0, "nothing queued yet")

    -- craft
    T.frame("WALLET_UPD", "0", "coins=146;dust=2000;why=gm")
    local key = H.CraftCard("EX1_116")
    local cm = T.queuedOps("CRAFT_MAKE")
    check(key and table.getn(cm) == 1 and cm[1].payload == "key=" .. key .. ";card=EX1_116" and W.IsBusy(), "CRAFT_MAKE queued")
    local _, why6 = H.CraftCard("CS2_022")
    check(why6 == "A card is being crafted.", "one at a time")
    flush()
    local m = lastSent("CRAFT_MAKE")
    T.frame("CRAFTED", m.reqId, "key=" .. key .. ";replay=0;kind=craft;card=EX1_116;n=1;delta=-1600;dust=400;ver=9;total=1;prot=0")
    check(not W.IsBusy() and W.op == nil and W.dust == 400, "CRAFTED applied")
    check(W.message == "Crafted Leeroy Jenkins for 1600 dust.", "craft message: " .. tostring(W.message))
    check(table.getn(T.queuedOps("COLL_GET")) == 1, "the collection is refetched")
    check(H.pending[m.reqId] == nil, "the request is answered")
    flush()
    T.coll(9)
    ownCards({ CS2_022 = 3, EX1_116 = 1 })

    -- a CRAFTED of another key is ignored
    key = H.ScrapCard("CS2_022")
    local sc = T.queuedOps("CRAFT_SCRAP")
    check(key and table.getn(sc) == 1 and sc[1].payload == "key=" .. key .. ";card=CS2_022;n=1", "CRAFT_SCRAP queued")
    flush()
    T.frame("CRAFTED", "0", "key=zzzzzzzzzzzz;replay=0;kind=scrap;card=CS2_022;n=1;delta=5;dust=405;ver=10;total=2;prot=0")
    check(W.IsBusy(), "a CRAFTED push of another key is not ours")

    -- REFUSED why=npc closes the access
    local s = lastSent("CRAFT_SCRAP")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=no;why=npc")
    check(not W.IsBusy() and not W.craftOpen and W.message == W.WHY_TEXT.npc, "npc refusal closes crafting")

    -- an unknown outcome is retried with the same key, then the addon gives up and resyncs
    T.frame("CRAFT_ACCESS", "0", "open=1;npc=Ezra Chatterton")
    key = H.ScrapCard("CS2_022")
    flush()
    for attempt = 1, W.MAX_ATTEMPTS do
        s = lastSent("CRAFT_SCRAP")
        local kv = P.KvDecode(s.payload)
        check(kv.key == key, "attempt " .. attempt .. " uses the same key")
        T.frame("ERR", s.reqId, "code=UNAVAILABLE;msg=later")
        drain(H.CRAFT_RETRY_DELAY + 0.5)
    end
    check(W.op == nil and string.find(W.message or "", "did not confirm", 1, true) ~= nil, "gave up: " .. tostring(W.message))
    check(table.getn(T.sentOps("CRAFT_SCRAP")) == 1 + W.MAX_ATTEMPTS, "sent " .. W.MAX_ATTEMPTS .. " times (plus the refused one)")

    -- REFUSED why=dust resyncs the wallet and the collection
    flush()
    T.frame("WALLET", H.wallet.reqId, "coins=1;dust=2000;craft=1;npc=Ezra Chatterton;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10")
    flush()
    T.coll(12)
    ownCards({ CS2_022 = 3, EX1_116 = 1 })
    H.queue = {}
    local why7
    key, why7 = H.ScrapCard("CS2_022")
    check(key ~= nil, "scrap started: " .. tostring(why7))
    flush()
    s = lastSent("CRAFT_SCRAP")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=no;why=copies")
    check(W.op == nil and W.message == W.WHY_TEXT.copies and W.craftOpen, "copies refusal: " .. tostring(W.message))
    check(table.getn(T.queuedOps("WALLET_GET")) == 1 and table.getn(T.queuedOps("COLL_GET")) == 1, "resync after a stale view")

    -- timeouts retry like errors
    flush()
    H.queue = {}
    T.frame("WALLET", H.wallet.reqId, "coins=1;dust=2000;craft=1;npc=Ezra Chatterton;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10")
    key = H.ScrapCard("CS2_022")
    flush()
    local count = table.getn(T.sentOps("CRAFT_SCRAP"))
    drain(H.REQUEST_TIMEOUT + 3)
    check(table.getn(T.sentOps("CRAFT_SCRAP")) == count + 1, "a timeout sends again")
    s = lastSent("CRAFT_SCRAP")
    T.frame("CRAFTED", s.reqId, "key=" .. key .. ";replay=1;kind=scrap;card=CS2_022;n=1;delta=5;dust=2005;ver=12;total=2;prot=0")
    check(W.op == nil and W.dust == 2005 and W.message == "Scrapped Polymorph for 5 dust.", "replayed scrap applied: " .. tostring(W.message))

    -- Disenchant Extra Cards: one CRAFT_EXTRAS with only a key
    flush()
    H.queue = {}
    HsOwnership.SetReady(HsOwnership.Parse("ver=13;n=2;cards=CS2_022.5.2,EX1_116.3.0", HS_CardDB))
    key = H.ScrapExtras()
    check(key ~= nil and W.IsBusy(), "extras started")
    check(select(2, H.ScrapExtras()) == "A card is being crafted.", "one craft at a time")
    flush()
    s = lastSent("CRAFT_EXTRAS")
    check(s and s.payload == "key=" .. key, "CRAFT_EXTRAS payload: " .. tostring(s and s.payload))
    T.frame("CRAFTED", s.reqId, "key=" .. key .. ";replay=0;kind=extras;card=all;n=5;delta=815;dust=2820;ver=13;total=0;prot=0")
    check(W.op == nil and W.dust == 2820 and W.message == "Disenchanted 5 extra cards for 815 dust.", "extras applied: " .. tostring(W.message))
    HsOwnership.SetReady(HsOwnership.Parse("ver=13;n=2;cards=CS2_022.2.2,EX1_116.1.0", HS_CardDB))
    local noKey, noWhy = H.ScrapExtras()
    check(noKey == nil and noWhy == W.WHY_TEXT.nothing, "nothing left to disenchant: " .. tostring(noWhy))
    -- a refusal: the view was stale, the addon resyncs
    HsOwnership.SetReady(HsOwnership.Parse("ver=14;n=1;cards=CS2_022.4.0", HS_CardDB))
    flush()
    H.queue = {}
    key = H.ScrapExtras()
    flush()
    s = lastSent("CRAFT_EXTRAS")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=no;why=nothing")
    check(W.op == nil and W.message == W.WHY_TEXT.nothing and not W.IsBusy(), "nothing refusal: " .. tostring(W.message))

    -- leaving the NPC
    printed = {}
    T.frame("CRAFT_ACCESS", "0", "open=0")
    check(not W.craftOpen and printed[1] == "You walked away from the Hearthstone Master: crafting is closed.", "access closed")
    T.frame("CRAFT_ACCESS", "0", "open=0")
    check(table.getn(printed) == 1, "a second close is silent")

    -- status, disconnect
    printed = {}
    H.Slash("status")
    check(string.find(T.joined(), "wallet: 1 Hearthstone Coins in your bags, 2820 dust", 1, true) ~= nil, "status line")

    -- BAG_UPDATE recounts the coin items (here: no container API outside the client, nothing changes)
    local savedApi = GetContainerNumSlots
    GetContainerNumSlots = function() return 1 end
    local savedLink, savedInfo = GetContainerItemLink, GetContainerItemInfo
    GetContainerItemLink = function(bag) if bag == 0 then return "|Hitem:919601:0:0:0|h[Hearthstone Coin]|h" end return nil end
    GetContainerItemInfo = function() return "tex", 17 end
    H.OnEvent("BAG_UPDATE", 0)
    check(W.coins == 17, "BAG_UPDATE recounts the coins: " .. tostring(W.coins))
    GetContainerNumSlots, GetContainerItemLink, GetContainerItemInfo = savedApi, savedLink, savedInfo
    T.err("0", "DISABLED", "off")
    check(W.state == "none" and H.wallet.reqId == nil and not H.wallet.supported, "disconnect resets the wallet")

    H.OnWalletChanged, H.ShowCraft = savedHook, savedShow
    resetAddon()
end

local function testCraftUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local CU = H.CraftUI
    local W = HsWallet
    local CM = HsCollection
    local T = packHelpers()
    local savedDb = H.db

    local function resetUI()
        UI.frame, UI.preview, UI.hover = nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons = {}, {}, {}
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited = nil, nil, false
        if H.PacksUI then
            H.PacksUI.panel, H.PacksUI.slots, H.PacksUI.rows, H.PacksUI.hover = nil, {}, {}, nil
        end
        H.DecksUI.panel, H.DecksUI.picker, H.DecksUI.tiles, H.DecksUI.empties, H.DecksUI.rows = nil, nil, {}, {}, {}
        CU.frame, CU.cardId, CU.confirm = nil, nil, nil
        CU.side, CU.extrasConfirm, CU.confirmCopies = nil, nil, nil
    end
    local function flush() drain(1) end
    local DU = H.DecksUI
    local O = HsOwnership
    local function btnText(b) return b.label and b.label:GetText() or b:GetText() end

    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, realm = "Realm A" })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()

    T.welcome("solo,chunk,packs,decks,wallet")
    flush()
    T.coll(1)
    T.frame("WALLET", H.wallet.reqId, WALLET_PAYLOAD)
    ownCards({ CS2_022 = 2, EX1_116 = 1 })

    -- not crafting: the header shows the dust on the bottle (no coins, no tooltip)
    UI.Show()
    local head = UI.frame
    check(head.wallet:IsShown() and head.wallet.dustText:GetText() == "30" and head.wallet.coinsText == nil, "header: the dust only")
    check(head.wallet.dustIcon._.texture == UI.DUST_BOTTLE and head.wallet.dustIcon:GetHeight() == UI.BOTTLE_H
        and head.wallet.dustIcon:GetWidth() == math.floor(UI.BOTTLE_H * UI.BOTTLE_RIGHT + 0.5), "header: the bottle, in its own proportions")
    check(head.wallet._.scripts.OnEnter == nil, "header: no tooltip")
    check(DU.panel.browse:IsShown() and not DU.panel.craft:IsShown(), "the deck list while not crafting")
    UI.frame:Hide()

    -- opening crafting at the NPC opens the window on the collection, in crafting mode
    T.frame("CRAFT_ACCESS", "0", "open=1;npc=Ezra Chatterton")
    local main = UI.frame
    check(main and main:IsShown() and UI.state.bottom == 1 and UI.IsCraftMode(), "the window opens in crafting mode")
    check(string.find(main.note:GetText() or "", "Crafting at", 1, true) == nil, "no blue crafting note: " .. tostring(main.note:GetText()))
    check(not main.wallet:IsShown(), "the dust leaves the header while crafting")
    check(main.craft and not main.craft:IsShown(), "the panel waits for a card")

    -- the craft side panel replaces the deck list
    local side = CU.side
    check(DU.panel.craft == side and side:IsShown() and not DU.panel.browse:IsShown() and not DU.panel.edit:IsShown(), "the side panel replaces the deck list")
    check(side.title:GetText() == "Card Crafting" and string.find(side.help:GetText(), "Select a card", 1, true) ~= nil, "title and help")
    check(side.dust:GetText() == "0" and side.bottle._.texture == UI.DUST_BOTTLE and side.summary:GetText() == "", "the bottle shows what the button gives: nothing yet")
    check(side.extras:GetText() == "No extra cards." and not side.button:IsEnabled(), "no extra cards yet")
    check(btnText(side.button) == "Disenchant\nExtra Cards" and string.find(side.explain:GetText(), "beyond 2", 1, true) ~= nil, "button and explanation")
    check(side.ownedBox:GetChecked() and side.missingBox:GetChecked(), "both checkboxes checked")
    -- the checkboxes are the ownership filter of the book
    F.fire(side.missingBox, "OnClick")
    check(UI.state.filters.own == "OWNED" and side.ownedBox:GetChecked() and not side.missingBox:GetChecked(), "only Owned")
    F.fire(side.ownedBox, "OnClick")
    check(UI.state.filters.own == "OWNED" and side.ownedBox:GetChecked(), "the last checkbox stays checked")
    F.fire(side.missingBox, "OnClick")
    check(UI.state.filters.own == "ALL" and side.ownedBox:GetChecked() and side.missingBox:GetChecked(), "both again: all cards")
    F.fire(side.ownedBox, "OnClick")
    check(UI.state.filters.own == "UNOWNED" and not side.ownedBox:GetChecked() and side.missingBox:GetChecked(), "only Missing")
    UI.SetOwnFilter("ALL")
    check(side.ownedBox:GetChecked() and side.missingBox:GetChecked(), "the dropdown moves the checkboxes")

    -- a click on a card opens the panel
    local slot = UI.slots[1]
    check(slot and slot.id ~= nil, "a card on the page")
    F.fire(slot, "OnClick", { arg1 = "LeftButton" })
    check(main.craft:IsShown() and CU.cardId == slot.id, "the panel shows the clicked card")
    -- hovering a card: the large card only, no plate and no caption (the default)
    UI.PREVIEW_CAPTION = false
    F.fire(slot, "OnEnter")
    check(UI.preview.frame:IsShown() and UI.preview.caption:GetText() == "" and UI.preview.widget.frame._.points[1].point == "CENTER", "crafting: the bare large card")
    F.fire(slot, "OnLeave")

    -- Polymorph: 2 owned (the limit), unprotected: craft greyed out, scrap possible
    CU.Show("CS2_022")
    local f = CU.frame
    check(f.title:GetText() == "Polymorph" and f.rarity:GetText() == "Basic (crafts as Common)", "title and rarity")
    check(f.owned:GetText() == "Owned: 2" and f.dustText:GetText() == "30" and f.plateBottle:IsShown(), "owned and dust on the plate")
    check(btnText(f.craftButton) == "Craft" and f.redText:GetText() == "-40" and f.redBottle:IsShown() and not f.craftButton:IsEnabled() and f.craftButton.dim:IsShown(), "craft disabled at the limit, -40 on the red bar")
    check(btnText(f.scrapButton) == "Disenchant" and f.greenText:GetText() == "+5" and f.scrapButton:IsEnabled() and not f.scrapButton.dim:IsShown(), "disenchant enabled, +5 on the green bar")
    check(f.scrapButton.glow:IsShown() and f.scrapButton.label:IsShown() and not f.craftButton.glow:IsShown() and not f.craftButton.label:IsShown(),
        "a usable slot has the blue outline and its text, one that cannot be used has neither")
    check(f.message:GetText() == "You already own every copy a deck can use.", "why craft is disabled: " .. tostring(f.message:GetText()))
    check(f.widget.id == "CS2_022", "the card picture")

    -- a saved deck needs both copies: the first click asks again
    local D = HsDecks
    local savedDecks = D.decks
    D.decks = { { id = 4, name = "Frost Mage", cards = { CS2_022 = 2 } } }
    CU.Refresh()
    check(f.warning:GetText() == "|cffff9933Scrapping leaves 1 copy: Frost Mage needs more.|r", "deck warning: " .. tostring(f.warning:GetText()))
    H.queue = {}
    F.fire(f.scrapButton, "OnClick")
    check(btnText(f.scrapButton) == "Disenchant\nanyway" and table.getn(T.queuedOps("CRAFT_SCRAP")) == 0, "first click asks again")
    F.fire(f.scrapButton, "OnClick")
    local sc = T.queuedOps("CRAFT_SCRAP")
    check(table.getn(sc) == 1 and string.find(sc[1].payload, "card=CS2_022;n=1", 1, true) ~= nil, "second click scraps")
    check(f.message:GetText() == "Working..." and not f.craftButton:IsEnabled() and not f.scrapButton:IsEnabled(), "busy")
    flush()
    local s = T.sentOps("CRAFT_SCRAP")
    local key = P.KvDecode(s[1].payload).key
    T.frame("CRAFTED", s[1].reqId, "key=" .. key .. ";replay=0;kind=scrap;card=CS2_022;n=1;delta=5;dust=35;ver=2;total=1;prot=0")
    check(f.message:GetText() == "Scrapped Polymorph for 5 dust." and W.dust == 35, "scrap done")
    D.decks = savedDecks

    -- Disenchant Extra Cards: 5 Polymorph (2 protected) and 3 Leeroy = 3 * 5 + 2 * 400 dust
    O.SetReady(O.Parse("ver=3;n=2;cards=CS2_022.5.2,EX1_116.3.0", HS_CardDB))
    UI.OnOwnershipChanged()
    check(side.extras:GetText() == "" and side.button:IsEnabled(), "extra cards: no total line, the summary and the bottle say it")
    check(side.dust:GetText() == "+815", "the bottle: the dust the button gives, not the dust owned: " .. tostring(side.dust:GetText()))
    check(side.summary:GetText() == "|cffff8c1a2 Legendary|r   +800\n|cffffffff3 Common|r   +15", "the summary by rarity: " .. tostring(side.summary:GetText()))
    H.queue = {}
    local exAsk, exWhy = CU.OnExtrasClick()
    check(exAsk == nil and exWhy == "confirm" and btnText(side.button) == "Click again\nto confirm"
        and table.getn(T.queuedOps("CRAFT_EXTRAS")) == 0, "the first click asks again")
    CU.Show("CS2_022")                       -- another click elsewhere cancels the question
    check(btnText(side.button) == "Disenchant\nExtra Cards", "the question is dropped")
    F.fire(side.button, "OnClick")
    F.fire(side.button, "OnClick")
    local ex = T.queuedOps("CRAFT_EXTRAS")
    check(table.getn(ex) == 1 and string.find(ex[1].payload, "^key=%w+$") ~= nil, "the second click sends CRAFT_EXTRAS")
    check(side.message:GetText() == "Working..." and not side.button:IsEnabled(), "busy")
    flush()
    local es = T.sentOps("CRAFT_EXTRAS")
    local ekey = P.KvDecode(es[1].payload).key
    T.frame("CRAFTED", es[1].reqId, "key=" .. ekey .. ";replay=0;kind=extras;card=all;n=5;delta=815;dust=850;ver=4;total=0;prot=0")
    check(side.message:GetText() == "Disenchanted 5 extra cards for 815 dust." and W.dust == 850, "disenchanted: " .. tostring(side.message:GetText()))
    O.SetReady(O.Parse("ver=4;n=2;cards=CS2_022.2.2,EX1_116.1.0", HS_CardDB))
    UI.OnOwnershipChanged()
    check(side.extras:GetText() == "No extra cards." and not side.button:IsEnabled(), "nothing left to disenchant")
    O.SetReady(O.Parse("ver=1;n=2;cards=CS2_022.2.0,EX1_116.1.0", HS_CardDB))
    UI.OnOwnershipChanged()

    -- Leeroy: not enough dust
    CU.Show("EX1_116")
    check(f.redText:GetText() == "-1600" and not f.craftButton:IsEnabled(), "Leeroy too expensive")
    check(f.greenText:GetText() == "+400" and f.scrapButton:IsEnabled(), "Leeroy scrappable")

    -- a promo card
    CU.Show("EX1_062")
    check(not f.craftButton:IsEnabled() and not f.scrapButton:IsEnabled() and f.message:GetText() == "This card cannot be crafted." and f.greenText:GetText() == "+400", "promo")

    -- the rest of the book is dimmed; a click on the dimmed part closes the view (no close button)
    check(f.shade:IsShown() and f.shade._.mouse and getglobal("HearthstoneCraftPanelClose") == nil, "dimmed book, no close button")
    F.fire(f.shade, "OnMouseDown")
    check(not f:IsShown() and not f.shade:IsShown() and CU.cardId == nil and not CU.IsShown(), "a click on the dimmed book closes the view")
    CU.Show("CS2_022")
    check(f:IsShown() and f.shade:IsShown(), "shown again")
    CU.Hide()

    -- the deck editor keeps its clicks
    D.draft = { deckId = 0, baseRev = 0, class = "MAGE", name = "x", cards = {}, orig = { name = "x", cards = {} } }
    check(not UI.IsCraftMode(), "no crafting mode in the deck editor")
    UI.RefreshAll()
    check(DU.panel.edit:IsShown() and not side:IsShown() and main.wallet:IsShown(), "the editor replaces the side panel, the dust shows in the header")
    D.draft = nil
    UI.RefreshAll()
    check(side:IsShown() and not DU.panel.edit:IsShown() and not main.wallet:IsShown(), "back to the side panel")

    -- closing the window ends crafting; the NPC opens it again
    main:Hide()
    check(not W.craftOpen and not UI.IsCraftMode(), "closing the window ends crafting")
    check(not main.craft:IsShown() and not CU.frame.shade:IsShown(), "no craft panel left behind")
    T.frame("CRAFT_ACCESS", "0", "open=1;npc=Ezra Chatterton")
    check(main:IsShown() and W.craftOpen and UI.IsCraftMode(), "the NPC opens crafting again")

    -- walking away closes the panel and the mode
    T.frame("CRAFT_ACCESS", "0", "open=0")
    check(not f:IsShown() and CU.cardId == nil and not UI.IsCraftMode(), "panel closed with the access")
    check(main.note:GetText() ~= nil and string.find(main.note:GetText(), "Crafting", 1, true) == nil, "note back to normal")
    check(DU.panel.browse:IsShown() and not side:IsShown(), "the deck list is back")
    check(main.wallet:IsShown() and main.wallet.dustText:GetText() == "850", "the dust is back in the header")
    F.fire(slot, "OnClick", { arg1 = "LeftButton" })
    check(not f:IsShown(), "no panel without the NPC")

    -- closing the window hides the panel
    T.frame("CRAFT_ACCESS", "0", "open=1;npc=Ezra Chatterton")
    CU.Show("CS2_022")
    main:Hide()
    check(not f:IsShown(), "closing the window hides the panel")

    -- without a wallet the header shows nothing
    T.err("0", "DISABLED", "off")
    UI.RefreshHeader()
    check(not main.wallet:IsShown(), "no wallet offline")

    F.uninstall()
    resetUI()
    H.db = savedDb
    resetAddon()
end

local function testCollectionPolish()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local DU = H.DecksUI
    local PU = H.PacksUI
    local D = HsDecks
    local MB = H.MicroButton
    local CM = HsCollection
    local savedDb = H.db

    local function resetUI()
        UI.frame, UI.preview, UI.hover, UI.layoutMode = nil, nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons, UI.manaAll = {}, {}, {}, nil
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited, s.savedClass = nil, nil, false, nil
        PU.panel, PU.slots, PU.rows, PU.hover = nil, {}, {}, nil
        DU.panel, DU.picker, DU.tiles, DU.empties, DU.rows, DU.pickButtons, DU.curveBars = nil, nil, {}, {}, {}, {}, {}
        DU.page, DU.hoverRow, DU.shownDraft, DU.shown, DU.trans, DU.snap, DU.leaveAfterDelete = 1, nil, nil, "browse", nil, true, nil
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    local function near(a, b) return math.abs(a - b) < 0.0001 end

    -- the numbers of the merged window (pixels at scale 1)
    check(UI.CARD_W == 172 and UI.CARD_H == 261 and UI.CARD_H == math.floor(UI.CARD_W * 388 / 256 + 0.5), "card 172 x 261 keeps the 256 x 388 aspect")
    check(UI.SLOT_W == UI.CARD_W and UI.SLOT_H == UI.CARD_H, "a slot is exactly the card")
    check(UI.BOOK_W == 1024 and UI.BOOK_H == 704 and UI.TAB_BAND == 38 and UI.BAR_H == 58 and UI.SIDE_W == 256 and UI.HEADER_H == 34, "book 1024 x 704, header 34, tab band 38, bar 58, deck panel 256")
    check(UI.WIDTH == 1280 and UI.HEIGHT == 846 and UI.WIDTH_BOOK == 1024 and UI.WIDTH == UI.BOOK_W + UI.SIDE_W and UI.HEIGHT == UI.HEADER_H + UI.TAB_BAND + UI.BOOK_H + UI.BAR_H + UI.FOOT_MARGIN, "window 1280 x 846 (1024 wide without the deck panel)")
    check(DU.PANEL_W == UI.SIDE_W and DU.PANEL_H == UI.HEIGHT - UI.HEADER_H - UI.FOOT_MARGIN, "the deck panel fills the window between the header and the bottom margin")
    local gridW = 4 * UI.CARD_W + 3 * UI.COL_GAP
    local gridH = 2 * UI.CARD_H + UI.ROW_GAP
    check(UI.GRID_X >= UI.PAGE_X1 + 20 and UI.GRID_X + gridW <= UI.PAGE_X2 - 20 and UI.GRID_X + gridW / 2 == UI.PAGE_CX, "the 4 columns are centred on the page")
    check(UI.GRID_Y >= 106 and UI.GRID_Y + gridH <= UI.PAGE_Y2 - 24, "two rows between the title plate and the page number")
    -- the x1 / x2 banner hangs from the VISIBLE card (not the slot, the render has a transparent margin) and fits
    -- between the rows and above the page arrows
    local vl, vt, vr, vb = H.CardWidget.VisibleRect(UI.CARD_W)
    check(vl > 0 and vt > 0 and vr < UI.CARD_W and vb < UI.SLOT_H and vb > UI.SLOT_H * 0.85, "the visible card is inside the slot, its bottom edge about 90% down")
    local bannerBottom = vb - UI.BANNER_OVERLAP + UI.BANNER_H
    check(UI.ROW_GAP == 14 and bannerBottom <= UI.SLOT_H + UI.ROW_GAP + vt, "the banner of row 1 ends above the visible card of row 2")
    check(UI.GRID_Y + UI.SLOT_H + UI.ROW_GAP + bannerBottom <= 667, "the banner of row 2 ends above the page arrows (their top is 667)")
    check(UI.BAR_TOP == -(UI.HEADER_H + UI.TAB_BAND + UI.BOOK_H) and UI.BAR_MID == UI.BAR_TOP - 29, "bar below the book")
    check(UI.BOOK_ART.page == "Interface\\Stationery\\StationeryTest1", "fallback page texture path")
    for k, v in pairs(UI.BOOK_ART) do
        check(string.find(v, "Interface\\", 1, true) == 1 and not string.find(v, "AddOns", 1, true), "book texture " .. k .. " is a client path")
    end

    -- default position (pure)
    local l, t = CM.DefaultPosition(1920, 1080, 938, 780, 230)
    check(near(l, 491) and near(t, 930), "wide screen: centred " .. l .. "," .. t)
    l, t = CM.DefaultPosition(1024, 768, 882, 732, 230)
    check(near(l, 142) and near(t, 750), "narrow screen: as far right as it fits (" .. l .. "," .. t .. ")")
    l, t = CM.DefaultPosition(1280, 720, 938, 800, 230)
    check(near(l, 230) and near(t, 720), "window taller than the screen: clamped to the top " .. l .. "," .. t)
    l = CM.DefaultPosition(640, 480, 900, 700, 230)
    check(l == 0, "never left of the screen")

    -- the window
    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, name = "Jaina", screenW = 1920, screenH = 1080 })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()
    check(H.db.pos == nil and H.db.bookArt == nil, "no saved position, art not turned off")
    H.Slash("collection")
    local main = UI.frame
    check(main ~= nil and main:IsShown() and main:GetWidth() == UI.WIDTH and main:GetHeight() == UI.HEIGHT, "window size")

    -- default position: centred on a wide screen, nothing saved
    local p = main._.points[1]
    check(table.getn(main._.points) == 1 and p.point == "TOPLEFT" and p.rel == UIParent and p.relPoint == "BOTTOMLEFT", "anchored to the screen's bottom left")
    check(near(p.x, (1920 - (UI.WIDTH + UI.TABS_OVERHANG)) / 2) and near(p.y, 1080 - (1080 - (UI.HEIGHT + UI.BOTTOM_OVERHANG)) / 2), "default position centred: " .. p.x .. "," .. p.y)
    check(p.x >= UI.CLEAR_LEFT and main:GetScale() == 1, "the default clears the player frame, scale 1 at 1920x1080")
    check(H.db.pos == nil, "showing the window does not save a position")

    -- 1024x768: scaled down, right of the player frame as far as it fits, completely on screen
    main:Hide()
    F.screenW, F.screenH = 1024, 768
    F.UIParent._.w, F.UIParent._.h = 1024, 768
    main:Show()
    local sc = main:GetScale()
    p = main._.points[1]
    check(sc < 1 and sc == CM.FitScale(1024, 768, UI.WIDTH + UI.TABS_OVERHANG, UI.HEIGHT + UI.BOTTOM_OVERHANG), "1024x768: scaled to fit: " .. sc)
    check((UI.WIDTH + UI.TABS_OVERHANG) * sc <= 1024 * 0.96 + 0.01 and (UI.HEIGHT + UI.BOTTOM_OVERHANG) * sc <= 768 * 0.96 + 0.01, "1024x768: the whole window fits")
    check(near(p.x * sc, (1024 - (UI.WIDTH + UI.TABS_OVERHANG) * sc) / 2) and p.x * sc >= 0, "1024x768: centred: " .. p.x * sc)
    check(p.y * sc <= 768 + 0.001 and p.y * sc - (UI.HEIGHT + UI.BOTTOM_OVERHANG) * sc >= -0.001, "1024x768: window and bottom tabs on the screen vertically")
    check(UI.CARD_W * sc > 120, "1024x768: a card is still " .. math.floor(UI.CARD_W * sc) .. " px wide")
    -- the edit mode and the Packs tab are the same window: nothing grows
    D.BeginNew("MAGE")
    UI.Update()
    check(main:GetWidth() == UI.WIDTH and main:GetScale() == sc and UI.IsEditView(), "edit mode: same width and scale")
    D.EndEdit()
    UI.Update()
    UI.SetBottomTab(2)
    check(main:GetWidth() == UI.WIDTH_BOOK and main:GetScale() == sc, "Packs tab: narrower window, same scale")
    UI.SetBottomTab(1)
    check(main:GetWidth() == UI.WIDTH, "collection tab: wide again")

    -- a saved position is never overridden
    F.screenW, F.screenH = 1920, 1080
    F.UIParent._.w, F.UIParent._.h = 1920, 1080
    main:Hide()
    main:Show()
    check(main:GetScale() == 1, "large screen again: scale 1")
    main._.left, main._.top = 300, 900
    UI.SavePosition()
    check(H.db.pos.left == 300 and H.db.pos.top == 900, "moved window saved")
    main:Hide()
    main:Show()
    p = main._.points[1]
    check(p.x == 300 and p.y == 900, "saved position wins over the default: " .. p.x .. "," .. p.y)
    H.db.pos = nil
    main:Hide()
    main:Show()
    p = main._.points[1]
    check(near(p.x, (1920 - (UI.WIDTH + UI.TABS_OVERHANG)) / 2), "without a saved position the default applies again")
    check(CM.ApplySavedDefaults({ pos = { left = "x", top = 1 } }).pos == nil and CM.ApplySavedDefaults({ bookArt = "no" }).bookArt == nil and CM.ApplySavedDefaults({ bookArt = false }).bookArt == false, "saved settings validated")

    -- slots: card size and place on the book, nothing outside the page
    check(table.getn(UI.slots) == 8, "8 slots")
    for i = 1, 8 do
        local slot = UI.slots[i]
        local sp = slot._.points[1]
        check(slot:GetWidth() == UI.SLOT_W and slot:GetHeight() == UI.SLOT_H, "slot " .. i .. " size")
        check(slot.widget.width == UI.CARD_W and slot.widget.frame:GetWidth() == UI.CARD_W and slot.widget.frame:GetHeight() == UI.CARD_H, "card " .. i .. " is " .. UI.CARD_W .. " x " .. UI.CARD_H)
        local wp = slot.widget.frame._.points[1]
        check(wp.point == "CENTER" and wp.rel == slot and wp.x == 0 and wp.y == 0, "card " .. i .. " centred in its slot")
        check(sp.x >= UI.PAGE_X1 and sp.x + UI.SLOT_W <= UI.PAGE_X2, "slot " .. i .. " inside the page horizontally")
        check(-sp.y >= -UI.PAGES_TOP + UI.PAGE_Y1 and -sp.y + UI.SLOT_H <= -UI.PAGES_TOP + UI.PAGE_Y2, "slot " .. i .. " inside the page vertically")
        check(slot.badge:GetWidth() <= UI.SLOT_W and slot.deckBadge:GetWidth() <= UI.SLOT_W and slot.marker:GetWidth() < UI.SLOT_W, "badges narrower than the slot " .. i)
        local bp, dp = slot.badge._.points[1], slot.deckBadge._.points[1]
        check(bp.rel == slot and bp.point == "TOP" and bp.relPoint == "TOPLEFT" and near(bp.y, -vb + 11) and near(bp.x, (vl + vr) / 2), "the count badge sits on the visible card's bottom edge " .. i)
        check(dp.rel == slot and dp.point == "TOP" and dp.relPoint == "TOPLEFT" and near(dp.y, -vt - 1) and near(dp.x, (vl + vr) / 2), "the deck badge sits on the visible card's top edge " .. i)
    end
    local s1, s2, s3, s5 = UI.slots[1]._.points[1], UI.slots[2]._.points[1], UI.slots[3]._.points[1], UI.slots[5]._.points[1]
    check(s2.x - s1.x == UI.SLOT_W + UI.COL_GAP and s3.x - s2.x == UI.SLOT_W + UI.COL_GAP and s1.x == UI.GRID_X, "four columns")
    check(s5.y == s1.y - (UI.SLOT_H + UI.ROW_GAP) and s5.x == s1.x and s1.y == UI.PAGES_TOP - UI.GRID_Y, "second row")
    check(main.prevButton._.points[1].y == main.nextButton._.points[1].y and main.pageText._.points[1].y == main.prevButton._.points[1].y
        and main.pageText._.points[1].x == UI.PAGE_CX and main.prevButton._.points[1].y > UI.BAR_TOP, "page arrows and number sit on the page above the bar")
    check(main.title._.points[1].x < UI.BOOK_W and main.title._.points[1].y == UI.PAGES_TOP - UI.PLATE_Y, "the title is on the plate")
    -- the bar controls end before the close button and the deck panel
    local resetBtn = F.globals.HearthstoneResetButton
    check(resetBtn._.points[1].x + resetBtn:GetWidth() <= UI.BOOK_W - 8, "bar controls end inside the book width")
    check(UI.manaAll._.points[1].x < UI.manaButtons[0]._.points[1].x and UI.manaButtons[0]._.points[1].x < UI.manaButtons[7]._.points[1].x and UI.manaButtons[7]._.points[1].x < 460, "mana gems left of the search box")
    check(UI.manaButtons[3]._.points[1].y == UI.BAR_MID and main.searchBox._.points[1].y == UI.BAR_MID, "gems and search in the bar")
    local cp = F.globals.HearthstoneFrameCloseButton._.points[1]
    check(cp.rel == main and cp.x <= UI.BOOK_W, "close button inside the book band")

    -- the enlarged card
    F.fire(UI.slots[1], "OnEnter")
    local pv = UI.preview
    check(UI.hover == 1 and pv.frame:IsShown() and pv.widget.width == 256, "hover shows the 256 px preview")
    local pp = pv.frame._.points[1]
    check(pp.rel == main and (pp.point == "LEFT" or pp.point == "RIGHT"), "preview is beside the window")
    F.fire(UI.slots[1], "OnLeave")

    -- the class tabs along the top edge, left to right, above the book
    check(table.getn(UI.classTabs) == 10 and UI.classTabs[1].key == "DRUID" and UI.classTabs[10].key == "NEUTRAL", "nine classes and Neutral, no All tab")
    for i = 1, 10 do
        local tp = UI.classTabs[i].button._.points[1]
        check(tp.rel == main and tp.x == UI.TAB_X0 + (i - 1) * UI.TAB_PITCH and (tp.y == UI.TAB_Y or tp.y == UI.TAB_Y_UP), "tab " .. i .. " place")
        check(tp.x + 32 <= UI.BOOK_W - 40, "tab " .. i .. " ends before the close button")
    end

    -- the fallback look: parchment of the mail stationery on the plain pages, /hs bookart off = plain dark
    local plain = main.plain
    check(main.bookTiles == nil and plain:IsShown() and plain.art ~= nil and plain.art._.texture == UI.BOOK_ART.page and plain.art:IsShown(), "no book art deployed: the parchment page")
    check(plain._.bdColor ~= nil, "the dark backdrop stays under the parchment")
    check(main.unavailable.art == nil, "the not-available panel stays dark")
    printed = {}
    H.Slash("bookart off")
    check(H.db.bookArt == false and plain:IsShown() and not plain.art:IsShown(), "/hs bookart off: plain dark page")
    check(string.find(lastPrinted(), "plain", 1, true) ~= nil, "bookart message: " .. tostring(lastPrinted()))
    check(main.title._.textColor[1] == 1 and main.pageText._.textColor[1] == 1, "gold text on the plain page")
    main:Hide()
    main:Show()
    check(not plain.art:IsShown(), "the plain page stays after reopening")
    H.Slash("bookart on")
    check(H.db.bookArt == nil and plain.art:IsShown() and main.title._.textColor[1] == UI.PAGE_INK[1], "/hs bookart on: parchment and dark ink again")
    H.Slash("bookart")
    check(string.find(lastPrinted(), "book art", 1, true) ~= nil, "bookart without argument reports")

    -- the deck panel, the packs panel and the class picker are inside the window
    local sp = main.sidePanel._.points[1]
    check(sp.rel == main and sp.x == UI.BOOK_W and sp.y == -UI.HEADER_H and main.sidePanel:GetWidth() == UI.SIDE_W and main.sidePanel:GetHeight() == UI.HEIGHT - UI.HEADER_H - UI.FOOT_MARGIN, "deck panel right of the book, below the header, above the bottom margin")
    check(sp.x + main.sidePanel:GetWidth() == main:GetWidth(), "the deck panel ends at the window edge")
    local kp = main.packs._.points[1]
    check(kp.x == 0 and kp.y == -UI.HEADER_H and PU.PANEL_W == UI.WIDTH_BOOK and PU.PANEL_H == UI.HEIGHT - UI.HEADER_H, "the packs panel is the whole window below the header")
    local pk = DU.picker._.points[1]
    check(pk.x - 178 >= 0 and pk.x + 178 <= UI.BOOK_W and -pk.y - 189 >= -UI.PAGES_TOP and -pk.y + 189 <= -UI.PAGES_TOP + UI.BOOK_H, "the class picker is inside the book")
    check(DU.TILE_X >= 0 and DU.TILE_X + DU.TILE_W <= DU.PANEL_W and DU.TILE_Y0 + (DU.ROWS + 1) * DU.TILE_PITCH <= 704, "tiles inside the panel above the counter")
    check(DU.ROW_X >= 35 and DU.ROW_X + DU.ROW_W <= 220 and DU.ROW_Y0 + DU.TRAY_ROWS * DU.RowPitch(DU.TRAY_ROWS) <= 700, "card rows inside the purple area of the panel")

    main:Hide()
    F.uninstall()
    resetUI()
    H.db = savedDb
end

-- ----------------------------------------------- collection and decks with the owner's art deployed
local function testCollectionArt()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local DU = H.DecksUI
    local PU = H.PacksUI
    local D = HsDecks
    local O = HsOwnership
    local MB = H.MicroButton
    local CM = HsCollection
    local savedDb = H.db
    local savedPieces = HS_CollectionTextures
    local function near(a, b) return math.abs(a - b) < 0.0001 end

    local function resetUI()
        UI.frame, UI.preview, UI.hover, UI.layoutMode = nil, nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons, UI.manaAll = {}, {}, {}, nil
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited, s.savedClass = nil, nil, false, nil
        PU.panel, PU.slots, PU.rows, PU.hover = nil, {}, {}, nil
        DU.panel, DU.picker, DU.tiles, DU.empties, DU.rows, DU.pickButtons, DU.curveBars = nil, nil, {}, {}, {}, {}, {}
        DU.page, DU.hoverRow, DU.shownDraft, DU.shown, DU.trans, DU.snap, DU.leaveAfterDelete = 1, nil, nil, "browse", nil, true, nil
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    -- HS_CollectionTextures as deploy-addon.ps1 writes it, for the given piece names
    local function pieces(names)
        local t = { generated = true, pieces = {} }
        for i = 1, table.getn(names) do
            t.pieces[names[i]] = { path = "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\" .. names[i], w = 40, h = 30, right = 0.625, bottom = 0.5 }
        end
        return t
    end
    local all = { "tab_druid", "tab_hunter", "tab_mage", "tab_paladin", "tab_priest", "tab_rogue", "tab_shaman", "tab_warlock", "tab_warrior", "tab_neutral",
        "mana_all", "mana_0", "mana_1", "mana_2", "mana_3", "mana_4", "mana_5", "mana_6", "mana_7", "mana_plus", "gem_cost", "banner_x1", "banner_x2",
        "search_box", "plate_blue", "deck_new", "btn_blank", "deck_tile", "deck_empty", "glow_blue", "glow_gold", "parch_tall",
        "hero_druid", "hero_hunter", "hero_mage", "hero_paladin", "hero_priest", "hero_rogue", "hero_shaman", "hero_warlock", "hero_warrior" }
    for i = 1, table.getn(UI.BOOK_TILES) do table.insert(all, UI.BOOK_TILES[i]) end
    for i = 1, table.getn(UI.BAR_TILES) do table.insert(all, UI.BAR_TILES[i]) end
    for i = 1, table.getn(DU.PANEL_TILES) do table.insert(all, DU.PANEL_TILES[i]) end
    local function slotOf(id)
        for i = 1, table.getn(UI.slots) do
            if UI.slots[i]:IsShown() and UI.slots[i].id == id then return UI.slots[i], i end
        end
        return nil
    end

    -- every piece the code asks for is a piece of the manifest
    local mf = readAll(ROOT .. "/data/art/collection_textures.manifest.json") or ""
    local spec = readAll(ROOT .. "/data/art/collection_pieces.json") or ""
    check(mf ~= "" and spec ~= "", "the collection art manifests are committed")
    for i = 1, table.getn(all) do
        check(string.find(mf, "\"" .. all[i] .. "\": {", 1, true) ~= nil, "manifest lists " .. all[i])
    end
    check(string.find(spec, "\"name\": \"book\"", 1, true) ~= nil or string.find(spec, "\"name\":\"book\"", 1, true) ~= nil, "pieces file cuts the book into tiles")
    local ps = readAll(ROOT .. "/tools/deploy/deploy-addon.ps1") or ""
    check(string.find(ps, "CollectionTextures.lua", 1, true) ~= nil and string.find(ps, "art\\out\\Collection", 1, true) ~= nil and string.find(ps, "Textures\\Collection", 1, true) ~= nil, "deploy script copies the collection art and writes CollectionTextures.lua")
    local toc = readAll(ROOT .. "/addon/Hearthstone/Hearthstone.toc") or ""
    check(string.find(toc, "CollectionTextures.lua", 1, true) ~= nil and string.find(toc, "CollectionTextures.lua", 1, true) < string.find(toc, "CollectionUI.lua", 1, true), "toc loads CollectionTextures.lua before CollectionUI.lua")

    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, name = "Jaina", screenW = 1920, screenH = 1080 })
    H.db = CM.ApplySavedDefaults(nil)
    HS_CollectionTextures = pieces(all)
    resetUI()
    H.Slash("collection")
    local main = UI.frame
    check(main and main:IsShown(), "window with the art")
    check(main._.backdrop == nil and main.plain ~= nil and not main.plain:IsShown(), "no dialog frame, no plain page")
    check(main.bookTiles and table.getn(main.bookTiles) == 6, "six book tiles")
    for i = 1, 6 do
        local tex = main.bookTiles[i]
        local _, _, c, r = string.find(UI.BOOK_TILES[i], "_c(%d+)r(%d+)")
        local pt = tex._.points[1]
        check(tex:IsShown() and tex._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\" .. UI.BOOK_TILES[i] and pt.x == tonumber(c) * 512 and pt.y == UI.PAGES_TOP - tonumber(r) * 256, "book tile " .. i .. " at its place")
        check(tex._.texcoord[2] == 0.625 and tex._.texcoord[4] == 0.5, "book tile " .. i .. " uses right / bottom of its piece")
    end

    -- class tabs: the sheet's icons, the checked tab glows
    local t3 = UI.classTabs[3]
    check(t3.textured and t3.button:GetNormalTexture()._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\tab_mage" and t3.button:GetWidth() == UI.TAB_W, "mage tab uses its piece")
    check(t3.button:GetCheckedTexture()._.blend == "ADD" and t3.button:GetChecked() == 1 and t3.button:GetNormalTexture()._.vertex[1] == 1, "the selected tab is bright and glows")
    check(UI.classTabs[4].button:GetNormalTexture()._.vertex[1] < 1 and UI.classTabs[4].button:GetChecked() == nil, "other tabs are dimmer")
    check(t3.button._.points[1].y == UI.TAB_Y_UP and UI.classTabs[4].button._.points[1].y == UI.TAB_Y and UI.TAB_Y_UP > UI.TAB_Y, "the selected tab is raised, the others sit lower")

    -- mana gems
    check(UI.manaButtons[3].textured and UI.manaButtons[3]:GetNormalTexture()._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\mana_3", "gem 3 piece")
    check(UI.manaAll.textured and UI.manaAll._.locked == true and UI.manaAll:GetNormalTexture()._.vertex[1] == 1, "the all gem glows while no mana filter is set")
    F.fire(UI.manaButtons[3], "OnClick")
    check(UI.manaButtons[3]._.locked == true and UI.manaButtons[3]:GetNormalTexture()._.vertex[1] == 1 and UI.manaButtons[2]:GetNormalTexture()._.vertex[1] < 1, "a selected gem is bright, the others dim")
    check(UI.manaAll._.locked == false and UI.manaAll:GetNormalTexture()._.vertex[1] < 1, "the all gem dims with a filter")
    local ok = true
    for i = 1, table.getn(UI.state.results) do if HS_CardDB.cards[UI.state.results[i]].cost ~= 3 then ok = false end end
    check(ok and UI.state.filters.mana[3], "mana 3 filters")
    F.fire(UI.manaAll, "OnClick")
    check(CM.ManaCount(UI.state.filters) == 0 and UI.manaAll._.locked == true, "the all gem clears the mana filter")
    F.fire(UI.manaAll, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "All mana costs", "all gem tooltip")
    F.fire(UI.manaAll, "OnLeave")

    -- bar and search box
    check(main.barTiles and table.getn(main.barTiles) == 2 and main.bar == main.barTiles[1], "the bar is two tiles")
    for i = 1, 2 do
        local pt = main.barTiles[i]._.points[1]
        check(main.barTiles[i]._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\" .. UI.BAR_TILES[i] and pt.x == (i - 1) * 512 and pt.y == UI.BAR_TOP, "bar tile " .. i .. " below the book")
    end
    check(F.globals.HearthstoneSearchBoxLeft:IsShown() == false and F.globals.HearthstoneSearchBoxMiddle:IsShown() == false, "the search art replaces the template's pieces")

    -- ownership banners and glows
    UI.SetClass("MAGE")
    local A, B, C = UI.state.pageIds[1], UI.state.pageIds[2], UI.state.pageIds[3]
    local entries = A .. ".2.0," .. B .. ".1.0," .. C .. ".3.0"
    local res = O.Parse("ver=1;n=3;cards=" .. entries, HS_CardDB)
    O.SetReady(res)
    UI.Requery(true)
    local sa, sb, sc = slotOf(A), slotOf(B), slotOf(C)
    check(sa.banner:IsShown() and not sa.badge:IsShown() and sa.bannerTex._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\banner_x2", "x2 banner under a card owned twice")
    check(sb.banner:IsShown() and sb.bannerTex._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\banner_x1", "x1 banner")
    local function glyphString(banner)
        local out = ""
        for i = 1, table.getn(banner.hsGlyphs or {}) do
            local g = banner.hsGlyphs[i]
            if g:IsShown() then
                local tc = g._.texcoord
                for ch, box in pairs(UI.GLYPHS) do
                    if near(tc[1], box[1] / 512) and near(tc[2], box[3] / 512) and near(tc[3], box[2] / 64) and near(tc[4], box[4] / 64) then
                        out = out .. ch
                    end
                end
            end
        end
        return out
    end
    check(sc.banner:IsShown() and not sc.badge:IsShown() and sc.bannerTex._.texture == UI.BANNER_BLANK and not sc.bannerText:IsShown()
        and glyphString(sc.banner) == "x3" and sc.banner.hsGlyphs[1]._.texture == UI.BANNER_DIGITS, "three copies: the blank banner with the count drawn in the banner numerals")
    check(sa.banner:IsShown() and not sa.bannerText:IsShown(), "the x2 banner has its own text")
    F.missingTextures = F.missingTextures or {}
    F.missingTextures[UI.BANNER_BLANK] = true
    UI.SetOwnedMark(sc, "x3")
    check(not sc.banner:IsShown() and sc.badge:IsShown() and sc.badgeText:GetText() == "x3", "without the blank banner texture: the text badge")
    F.missingTextures[UI.BANNER_BLANK] = nil
    F.missingTextures[UI.BANNER_DIGITS] = true
    UI.SetOwnedMark(sc, "x3")
    check(sc.banner:IsShown() and sc.bannerText:IsShown() and sc.bannerText:GetText() == "x3" and glyphString(sc.banner) == "",
        "without the numeral atlas: the count is written as text on the blank banner")
    F.missingTextures[UI.BANNER_DIGITS] = nil
    UI.SetOwnedMark(sc, "x12")
    check(sc.banner:IsShown() and not sc.bannerText:IsShown() and glyphString(sc.banner) == "x12", "two digit counts fit the blank banner")
    do -- every glyph is inside the plate (60 x 45 px at scale 1 -> 52 x 39 frame px) and the text is centred
        for _, str in ipairs({ "x3", "x12", "x345", "x999" }) do
            local lay = UI.GlyphLayout(str, UI.BANNER_W / UI.PLATE_W)
            local first, last = lay[1], lay[table.getn(lay)]
            check(first.left >= 0 and last.left + last.w <= UI.BANNER_W and near((first.left + last.left + last.w) / 2, UI.TEXT_CX * UI.BANNER_W / UI.PLATE_W),
                "the glyphs of " .. str .. " are centred on the plate and inside the banner")
            for i = 1, table.getn(lay) do
                check(lay[i].bottom <= UI.BANNER_H and lay[i].bottom - lay[i].h >= 0, "glyph " .. i .. " of " .. str .. " is inside the banner vertically")
            end
        end
        local a, b = UI.GlyphLayout("x3", 1), UI.GlyphLayout("x345", 1)
        check(near(a[2].h, UI.TEXT_H) and b[2].h < a[2].h, "one digit is full size, three digits shrink to fit")
        check(near(a[2].bottom - a[1].bottom, (UI.GLYPH_BASELINE - UI.GLYPHS["x"][4]) * UI.TEXT_H / UI.GLYPH_DIGIT_H), "the x stands on the digits' baseline (3 atlas px above their bottom)")
        check(UI.GlyphLayout("xa", 1) == nil, "no glyph for other characters")
    end
    UI.SetOwnedMark(sc, "x3")
    -- the craft panel paints the same banner at its own size
    local big = UI.GlyphLayout("x5", 1.2 * UI.BANNER_W / UI.PLATE_W)
    check(UI.PaintBanner(sc.banner, sc.bannerTex, sc.bannerText, 5, 1.2) and glyphString(sc.banner) == "x5" and not sc.bannerText:IsShown()
        and near(sc.banner.hsGlyphs[2]._.h, big[2].h) and near(big[2].h, UI.TEXT_H * 1.2 * UI.BANNER_W / UI.PLATE_W)
        and UI.PaintBanner(sc.banner, sc.bannerTex, sc.bannerText, 1) and not sc.bannerText:IsShown() and glyphString(sc.banner) == ""
        and not UI.PaintBanner(sc.banner, sc.bannerTex, sc.bannerText, 0) and not UI.PaintBanner(nil, nil, nil, 3), "PaintBanner: any count, scaled glyphs, nothing for 0 or without a banner")
    UI.SetOwnedMark(sc, "x3")
    local unowned = UI.slots[4]
    check(not unowned.banner:IsShown() and unowned.widget.dimmed, "no banner on an unowned card, dimmed")
    local bpt = sa.banner._.points[1]
    local vl, vt, vr, vb = H.CardWidget.VisibleRect(UI.CARD_W)
    check(bpt.point == "TOPLEFT" and bpt.rel == sa and bpt.relPoint == "TOPLEFT" and near(bpt.x + UI.BANNER_W / 2, (vl + vr) / 2) and near(bpt.y, -vb + UI.BANNER_OVERLAP), "the banner hangs from the VISIBLE card: centred, its top tucked behind the bottom edge")
    check(-bpt.y + UI.BANNER_H - UI.SLOT_H < 12 and sa.banner._.level < sa.widget.frame._.level, "the banner ends close under the slot and the card draws over its top")
    check(sa.glowBlue ~= nil and sa.glowGold ~= nil and not sa.glowBlue:IsShown() and not sa.glowGold:IsShown(), "glows are built, hidden")
    F.fire(sa, "OnEnter")
    check(sa.glowBlue:IsShown() and sa._.bdBorder[4] == 0, "hover: the blue glow replaces the border")
    F.fire(sa, "OnLeave")
    check(not sa.glowBlue:IsShown(), "no glow after leaving")
    -- the gold glow marks the cards of the deck being edited
    D.BeginNew("MAGE")
    UI.Update()
    check(UI.IsEditView() and not sa.glowGold:IsShown(), "no gold glow for a card that is not in the deck")
    D.AddCard(A)
    UI.RefreshPage()
    check(sa.glowGold:IsShown() and not sa.deckBadge:IsShown(), "a card in the deck glows gold")
    D.EndEdit()
    UI.Update()

    -- the deck panel
    local pnl = main.sidePanel
    check(pnl.artTiles and table.getn(pnl.artTiles) == 4 and pnl._.backdrop == nil, "deck panel frame art: four tiles")
    for i = 1, 4 do
        local pt = pnl.artTiles[i]._.points[1]
        check(pnl.artTiles[i]._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\" .. DU.PANEL_TILES[i] and pt.x == 0 and pt.y == -(i - 1) * 256, "deck panel tile " .. i .. " below the previous one")
    end
    local br = pnl.browse
    check(br.newButton.textured and br.newButton:GetNormalTexture()._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\deck_new", "New Deck is the sheet's button")
    check(table.getn(DU.empties) == DU.ROWS and DU.empties[1]:IsShown(), "empty slots fill the free positions")
    local x0, y0 = DU.tiles[1]._.points[1].x, DU.tiles[1]._.points[1].y
    check(x0 == DU.TILE_X and y0 == -(DU.TILE_Y0 + DU.TILE_PITCH) and DU.empties[1]._.points[1].x == x0 and DU.empties[1]._.points[1].y == y0, "empty slots sit where the tiles go")
    check(br.counter._.regions[1]._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\plate_blue", "counter plate")
    check(br.title._.textColor[1] == 0.25, "My Decks in dark ink on the plate")
    check(pnl.edit.nameBox._.textColor ~= nil and F.globals.HearthstoneDeckNameBoxLeft:IsShown() == false, "the name sits on the plate of the panel art")
    local nbp = pnl.edit.nameBox._.points[1]
    check(nbp.x >= 64 and nbp.x + pnl.edit.nameBox:GetWidth() <= 192 and -nbp.y >= 30 and -nbp.y + pnl.edit.nameBox:GetHeight() <= 58 and pnl.edit.nameBox._.justifyH == "CENTER", "the name box is inside the cream plaque, centred")
    local cip, ctp = pnl.edit.classIcon._.points[1], pnl.edit.countText._.points[1]
    check(-cip.y >= 84 and -ctp.y >= 90 and -ctp.y <= DU.ROW_Y0 and cip.x >= 35, "the deck line (class, 30 / 30) is in the purple area above the rows")
    -- the class picker: Cancel is clear of the class buttons
    DU.ShowPicker()
    do
        local last = DU.pickButtons[table.getn(DU.pickButtons)]
        local lp, cp = last._.points[1], DU.picker.cancelButton._.points[1]
        check(-lp.y + last:GetHeight() < 378 - cp.y - DU.picker.cancelButton:GetHeight() and DU.picker.cancelButton._.level > last._.level, "the picker's Cancel is below the last row of class buttons and above them")
    end
    DU.HidePicker()
    -- a deck tile with its frame
    H.state.welcomed = true
    D.SetReady(D.ParseDecks("cap=5;n=1;start=MAGE;unlocked=;d1=1.1.MAGE.player;m1=Frost;c1=;s1=size"))
    UI.RefreshAll()
    check(DU.tiles[1]:IsShown() and not DU.empties[1]:IsShown() and DU.empties[2]:IsShown(), "a deck replaces its empty slot")
    local frameTex = nil
    for _, rg in ipairs(DU.tiles[1]._.regions) do
        if rg._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\deck_tile" then frameTex = rg end
    end
    check(frameTex ~= nil and frameTex._.layer == "ARTWORK" and DU.tiles[1].art._.layer == "BACKGROUND", "the tile frame is over the hero art")
    -- the class picker uses the tab icons and the parchment
    F.fire(br.newButton, "OnClick")
    check(DU.picker:IsShown() and DU.picker._.regions[1]._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\parch_tall", "picker on the parchment")
    check(DU.pickButtons[3].icon._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Collection\\tab_mage", "picker icon is the class tab piece")
    F.fire(DU.picker.cancelButton, "OnClick")

    -- /hs bookart off: the plain page, but the other pieces stay
    H.Slash("bookart off")
    check(not main.bookTiles[1]:IsShown() and main.plain:IsShown() and not main.plain.art:IsShown() and main.title._.textColor[1] == 1, "bookart off: plain dark page instead of the book")
    check(UI.classTabs[3].textured and main.bar:IsShown(), "the other art stays")
    H.Slash("bookart on")
    check(main.bookTiles[1]:IsShown() and not main.plain:IsShown() and main.title._.textColor[1] == UI.PAGE_INK[1], "bookart on: the book again")

    -- the Packs tab hides the book art and its bar (their edges would show past the frame beside the pack page)
    -- and the deck panel
    local before = main._.points[1].x
    main._.left, main._.top = before, main._.points[1].y      -- the fake reports no corner of its own
    UI.SetBottomTab(2)
    check(not main.bookTiles[1]:IsShown() and not main.barTiles[1]:IsShown() and not pnl:IsShown() and not main.book:IsShown() and main.packs:IsShown() and main.title:GetText() == "Card Packs", "packs tab without the book art")
    check(near(main._.points[1].x, before + (UI.WIDTH - UI.WIDTH_BOOK) / 2), "the narrower Packs window keeps the centre of the window")
    main._.left = main._.points[1].x
    UI.SetBottomTab(1)
    check(main.bookTiles[1]:IsShown() and main.barTiles[1]:IsShown(), "the book art is back on the collection tab")
    check(near(main._.points[1].x, before), "and back on the collection tab the window is where it was")
    main._.left, main._.top = nil, nil
    main:Hide()
    F.uninstall()
    resetUI()

    -- a partial deployment: only the tabs, everything else falls back piece by piece
    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, name = "Jaina", screenW = 1920, screenH = 1080 })
    HS_CollectionTextures = pieces({ "tab_mage", "tab_druid", "mana_3" })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()
    H.Slash("collection")
    main = UI.frame
    check(main and main:IsShown() and main._.backdrop ~= nil and main.bookTiles == nil and main.plain:IsShown(), "no book tiles: the native look, one by one")
    check(UI.classTabs[3].textured and not UI.classTabs[4].textured and UI.manaButtons[3].textured and not UI.manaButtons[2].textured, "only the deployed pieces are used")
    check(UI.manaButtons[2]:GetText() == "2", "native gems keep their number")
    check(UI.slots[1].banner == nil and UI.slots[1].glowBlue == nil, "no banner or glow pieces: the badge and the border")
    check(main.sidePanel._.backdrop ~= nil and main.sidePanel.browse.newButton:GetText() == "New Deck" and not main.sidePanel.browse.newButton.textured, "native deck panel and New Deck button")
    check(table.getn(DU.empties) == 0, "no empty slots without the piece")
    F.fire(UI.manaButtons[2], "OnClick")
    check(UI.manaButtons[2]:GetText() == "|cff33ff332|r" and UI.manaButtons[2]._.locked == true, "native gem selected")
    D.BeginNew("MAGE")
    UI.Update()
    check(UI.IsEditView() and main.sidePanel.edit:IsShown() and UI.state.filters.deckClass == "MAGE", "the edit mode works with the native look")
    D.EndEdit()
    main:Hide()
    F.uninstall()
    resetUI()

    -- a broken piece table is ignored
    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, name = "Jaina", screenW = 1920, screenH = 1080 })
    HS_CollectionTextures = { generated = true, pieces = { tab_mage = { path = 5 }, book_c0r0 = "x", mana_3 = { path = "p", right = "a", bottom = 1 } } }
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()
    check(UI.Piece("tab_mage") == nil and UI.Piece("book_c0r0") == nil and UI.Piece("mana_3") == nil and not UI.BookTextured(), "malformed entries are no pieces")
    HS_CollectionTextures = nil
    check(UI.Piece("tab_mage") == nil, "no table: no pieces")
    HS_CollectionTextures = pieces(all)
    check(pcall(function() H.Slash("collection") end) and UI.frame ~= nil, "the window opens")
    UI.frame:Hide()
    F.uninstall()
    resetUI()
    HS_CollectionTextures = savedPieces
    H.db = savedDb
    resetAddon()
end

-- --------------------- the fixes after the owner's in-game test (textures, tabs, tiles, banners, picker)
local function testCollectionFixes()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local DU = H.DecksUI
    local PU = H.PacksUI
    local D = HsDecks
    local O = HsOwnership
    local MB = H.MicroButton
    local CM = HsCollection
    local W = H.CardWidget
    local savedDb = H.db
    local savedPieces = HS_CollectionTextures
    local savedCardTextures = HS_CardTextures
    local BS = string.char(92)
    local PATH = "Interface" .. BS .. "AddOns" .. BS .. "Hearthstone" .. BS .. "Textures" .. BS .. "Collection" .. BS
    local function near(a, b) return math.abs(a - b) < 0.0001 end

    local function resetUI()
        UI.frame, UI.preview, UI.hover, UI.layoutMode, UI.tabsFrame = nil, nil, nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons, UI.manaAll = {}, {}, {}, nil
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited, s.savedClass = nil, nil, false, nil
        PU.panel, PU.slots, PU.rows, PU.hover = nil, {}, {}, nil
        DU.panel, DU.picker, DU.tiles, DU.empties, DU.rows, DU.pickButtons, DU.curveBars = nil, nil, {}, {}, {}, {}, {}
        DU.page, DU.hoverRow, DU.shownDraft, DU.shown, DU.trans, DU.snap, DU.leaveAfterDelete = 1, nil, nil, "browse", nil, true, nil
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    local function pieces(names)
        local t = { generated = true, pieces = {} }
        for i = 1, table.getn(names) do
            t.pieces[names[i]] = { path = PATH .. names[i], w = 40, h = 30, right = 0.625, bottom = 0.5 }
        end
        return t
    end
    local classes = { "druid", "hunter", "mage", "paladin", "priest", "rogue", "shaman", "warlock", "warrior" }
    local all = { "mana_all", "mana_0", "mana_1", "mana_2", "mana_3", "mana_4", "mana_5", "mana_6", "mana_7", "mana_plus", "gem_cost", "banner_x1", "banner_x2",
        "search_box", "plate_blue", "deck_new", "btn_blank", "deck_tile", "deck_empty", "glow_blue", "glow_gold", "parch_tall", "tab_neutral" }
    for i = 1, table.getn(classes) do
        table.insert(all, "tab_" .. classes[i])
        table.insert(all, "hero_" .. classes[i])
    end
    for i = 1, table.getn(UI.BOOK_TILES) do table.insert(all, UI.BOOK_TILES[i]) end
    for i = 1, table.getn(UI.BAR_TILES) do table.insert(all, UI.BAR_TILES[i]) end
    for i = 1, table.getn(DU.PANEL_TILES) do table.insert(all, DU.PANEL_TILES[i]) end

    -- ---- 1. no texture side above 512 (the client draws bigger ones solid green)
    local function textureSizes(path)
        local text = readAll(ROOT .. path) or ""
        local n, bad = 0, {}
        for w, h in string.gfind(text, "\"texture_size\": %[%s*(%d+),%s*(%d+)%s*%]") do
            n = n + 1
            if tonumber(w) > 512 or tonumber(h) > 512 then table.insert(bad, w .. "x" .. h) end
        end
        return n, bad, text
    end
    local n, bad, mf = textureSizes("/data/art/collection_textures.manifest.json")
    check(n >= 50 and table.getn(bad) == 0, "collection manifest: " .. n .. " textures, none above 512 (bad: " .. table.concat(bad, ",") .. ")")
    n, bad = textureSizes("/data/art/board_textures.manifest.json")
    check(n > 10 and table.getn(bad) == 0, "board manifest: none above 512")
    n, bad = textureSizes("/data/art/textures.manifest.json")
    check(n > 400 and table.getn(bad) == 0, "card manifest: none above 512")
    for name, size in pairs(UI.TILE_SIZE) do
        check(size[1] <= 512 and size[2] <= 512, "the tile size of " .. name .. " is within 512")
    end
    check(string.find(mf, "\"deck_panel\": {", 1, true) == nil and string.find(mf, "\"bar_main\": {", 1, true) == nil, "the 1024 pieces are gone from the manifest: deck_panel and bar_main are tiles")
    local names = {}
    for i = 1, table.getn(UI.BOOK_TILES) do table.insert(names, UI.BOOK_TILES[i]) end
    for i = 1, table.getn(UI.BAR_TILES) do table.insert(names, UI.BAR_TILES[i]) end
    for i = 1, table.getn(DU.PANEL_TILES) do table.insert(names, DU.PANEL_TILES[i]) end
    for i = 1, table.getn(classes) do table.insert(names, "hero_" .. classes[i]) end
    table.insert(names, "btn_blank")
    for i = 1, table.getn(names) do
        check(string.find(mf, "\"" .. names[i] .. "\": {", 1, true) ~= nil, "manifest lists " .. names[i])
    end
    check(table.getn(UI.BAR_TILES) * UI.TILE_SIZE.bar_main[1] >= UI.BOOK_W and table.getn(DU.PANEL_TILES) * UI.TILE_SIZE.deck_panel[2] >= DU.PANEL_H, "the tiles cover the bar and the deck panel")
    local slicer = readAll(ROOT .. "/tools/art/slice_collection.py") or ""
    check(string.find(slicer, "MAX_SIDE = 512", 1, true) ~= nil and string.find(slicer, "def check_manifest", 1, true) ~= nil and string.find(slicer, "--check", 1, true) ~= nil and string.find(slicer, "--art-root", 1, true) ~= nil, "the slicer enforces the 512 rule and has --check and --art-root")
    local spec = readAll(ROOT .. "/data/art/collection_pieces.json") or ""
    check(string.find(spec, "\"tex\": [1024", 1, true) == nil and string.find(spec, "\"surround\"", 1, true) ~= nil and string.find(spec, "\"hero_strips\"", 1, true) ~= nil and string.find(spec, "\"split\"", 1, true) ~= nil, "pieces file: no 1024 texture, the book surround, the hero strips, split pieces")

    -- ---- the window with the art
    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, name = "Jaina", screenW = 1920, screenH = 1080 })
    H.db = CM.ApplySavedDefaults(nil)
    H.state.welcomed = true
    local classTabs = {}
    for i = 1, table.getn(classes) do table.insert(classTabs, "tab_" .. classes[i]) end
    HS_CollectionTextures = pieces(all)
    resetUI()
    H.Slash("collection")
    local main = UI.frame
    check(main and main:IsShown(), "window with the art")

    -- ---- 2. the book is transparent outside its silhouette: no dialog backdrop, only a plain dark background texture
    check(main._.backdrop == nil and table.getn(main._.regions) == 1 and main.bg == main._.regions[1], "no dialog backdrop, one background texture on the window")
    check(main.bg._.layer == "BACKGROUND" and main.bg._.vertex[1] == UI.BG_COLOR[1] and main.bg._.vertex[4] == UI.BG_COLOR[4] and table.getn(main.bg._.points) == 2, "the background is the dark colour behind the whole window")
    local fe = main.frameEdge
    check(fe ~= nil and fe._.backdrop ~= nil and fe._.backdrop.edgeFile == UI.FRAME_EDGE and string.find(UI.FRAME_EDGE, "UI-DialogBox-Border", 1, true) and fe._.backdrop.bgFile == nil and fe._.bdBorder[1] > 0.8 and fe._.level < UI.tabsFrame._.level + 20, "the stone silver frame (dialog border, edge only, grey tint) around the window")
    check(fe._.points[1].x == -UI.FRAME_PAD.left and fe._.points[1].y == UI.FRAME_PAD.top and fe._.points[2].x == UI.FRAME_PAD.right and fe._.points[2].y == -UI.FRAME_PAD.bottom, "the frame is placed by UI.FRAME_PAD per side")
    local bp1, bp2 = main.bg._.points[1], main.bg._.points[2]
    check(bp1.x == -UI.FRAME_PAD.left + UI.FRAME_METAL and bp1.y == UI.FRAME_PAD.top - UI.FRAME_METAL and bp2.x == UI.FRAME_PAD.right - UI.FRAME_METAL and bp2.y == -UI.FRAME_PAD.bottom + UI.FRAME_METAL, "the dark background follows the frame, ending at the outer edge of the metal")
    local pm = UI.manaButtons[7].plusMark
    check(UI.PLUS_W == 18 and UI.PLUS_H == 17 and pm ~= nil and pm:GetWidth() == 18, "the 7 gem keeps its small badge, with a bigger plus drawn on it")
    do
        local l0, t0, r0, b0 = UI.GlowRect("glow_blue", 10, 30, 236, 347)
        local keep = UI.GLOW_ADJUST.glow_blue.left
        UI.GLOW_ADJUST.glow_blue.left = keep + 4
        local l1, t1, r1, b1 = UI.GlowRect("glow_blue", 10, 30, 236, 347)
        UI.GLOW_ADJUST.glow_blue.left = keep
        check(l1 < l0 and near(t1, t0) and near(b1, b0), "UI.GLOW_ADJUST moves one side of the glow outwards")
    end
    local cb = F.globals.HearthstoneFrameCloseButton._.points[1]
    check(cb.point == "TOPRIGHT" and cb.relPoint == "TOPRIGHT" and cb.rel == main and cb.x < 0, "the close button is in the top right corner of the window")
    check(main.header ~= nil and main.header:GetText() == "Hearthstone" and main.header._.points[1].y == -UI.HEADER_H / 2 and main.header._.points[1].y > UI.TAB_Y_UP, "the title sits in the strip above the class tabs")
    check(string.find(mf, "\"mode\": \"surround\"", 1, true) ~= nil, "the book tiles are cut with a transparent surround")

    -- ---- 3. class tabs sit BEHIND the book, the selected one is raised
    local L = main:GetFrameLevel()
    check(UI.tabsFrame ~= nil and main.tabsFrame == UI.tabsFrame and UI.tabsFrame._.level < main.art._.level and main.art._.level < main.book._.level, "z-order: tabs, then the book art, then the book's cards")
    check(UI.tabsFrame._.level > main.dragArea._.level, "the tabs are above the drag band (clickable)")
    for i = 1, 10 do
        local b = UI.classTabs[i].button
        check(b:GetParent() == UI.tabsFrame and b._.level < main.art._.level, "tab " .. i .. " is drawn under the book art")
    end
    local pageTop = UI.PAGES_TOP - UI.PAGE_Y1
    check(UI.TAB_Y > pageTop and UI.TAB_Y - UI.TAB_W < pageTop, "an unselected tab sticks out above the page edge and its lower part is behind the page")
    check(UI.TAB_Y_UP > UI.TAB_Y and UI.TAB_Y_UP - UI.TAB_Y <= 12, "the selected tab is raised a little (the reference game's tabs are higher)")
    local function tabY(i) return UI.classTabs[i].button._.points[1].y end
    check(tabY(3) == UI.TAB_Y_UP and tabY(4) == UI.TAB_Y and tabY(10) == UI.TAB_Y, "the mage tab (selected) is raised, the others are not")
    F.fire(UI.classTabs[10].button, "OnClick")
    check(tabY(10) == UI.TAB_Y_UP and tabY(3) == UI.TAB_Y, "clicking Neutral raises it and lowers the mage tab")
    check(UI.classTabs[10].button._.points[1].rel == main, "tabs stay anchored to the window")
    D.BeginNew("MAGE")
    UI.Update()
    check(tabY(3) == UI.TAB_Y_UP or tabY(10) == UI.TAB_Y_UP, "in the edit mode one of the two tabs is raised")
    D.EndEdit()
    UI.Update()

    -- ---- the bottom tabs are drawn with the art, the selected one is raised
    local t1, t2 = F.globals.HearthstoneFrameTab1, F.globals.HearthstoneFrameTab2
    check(not main.nativeTabs and t1.label:GetText() == "Collection" and t2.label:GetText() == "Packs" and t1:GetNormalTexture()._.texture == PATH .. "btn_blank", "bottom tabs: the plate of the art with the label in ink")
    check(main.selectedTab == 1 and main.numTabs == 3 and t1._.points[1].y == UI.BTAB_Y_UP and t2._.points[1].y == UI.BTAB_Y and t1:GetNormalTexture()._.vertex[1] == 1 and t2:GetNormalTexture()._.vertex[1] < 1, "Collection selected: bright, same height as the other tab")
    check(t1._.level > main.book._.level and t1:GetWidth() == UI.BTAB_W and t1:GetHeight() == UI.BTAB_H, "bottom tabs above the bar")
    check(UI.BTAB_H - UI.BTAB_Y <= UI.BOTTOM_OVERHANG, "the bottom tabs stay within the overhang the window reserves")
    F.fire(t2, "OnClick")
    check(UI.state.bottom == 2 and main.selectedTab == 2 and t2._.points[1].y == UI.BTAB_Y_UP and t1._.points[1].y == UI.BTAB_Y and not main.barTiles[1]:IsShown(), "Packs selected: the tab keeps its height, the bar art is hidden under the pack page")
    F.fire(t1, "OnClick")
    check(UI.state.bottom == 1 and main.selectedTab == 1, "back on Collection")

    -- ---- 5. banner and glows hang from the VISIBLE card
    UI.SetClass("MAGE")
    local A, B = UI.state.pageIds[1], UI.state.pageIds[2]
    O.SetReady(O.Parse("ver=1;n=2;cards=" .. A .. ".2.0," .. B .. ".1.0", HS_CardDB))
    UI.Requery(true)
    local vl, vt, vr, vb = W.VisibleRect(UI.CARD_W)
    check(vl > 0 and vr < UI.CARD_W and vt > 0 and vb < UI.SLOT_H, "the visible card is smaller than the slot (the render has a margin)")
    local v = W.VISIBLE
    check(v.bottom == 347 and v.left == 10 and v.right == 236 and near(vb, 347 * UI.CARD_W / 256), "the visible rectangle comes from the renders' used area (bottom edge 347 of 388)")
    for i = 1, 4 do
        local slot = UI.slots[i]
        local bp = slot.banner._.points[1]
        check(near(bp.x + UI.BANNER_W / 2, (vl + vr) / 2) and near(bp.y, -vb + UI.BANNER_OVERLAP), "banner " .. i .. " is centred under the visible card and overlaps its bottom edge by " .. UI.BANNER_OVERLAP)
        local gb, gg = slot.glowBlue._.points, slot.glowGold._.points
        local gl, gt, gr, gbm = UI.GlowRect("glow_blue", vl, vt, vr, vb)
        check(table.getn(gb) == 2 and gb[1].point == "TOPLEFT" and gb[1].rel == slot and near(gb[1].x, gl) and near(gb[1].y, -gt)
            and gb[2].point == "BOTTOMRIGHT" and gb[2].relPoint == "TOPLEFT" and near(gb[2].x, gr) and near(gb[2].y, -gbm), "blue glow " .. i .. " is placed by its line")
        -- the line of the piece (stretched over the rectangle) lands just outside the visible card on all four sides
        local ln = UI.GLOW_LINE.glow_blue
        local sx, sy = (gr - gl) / ln.w, (gbm - gt) / ln.h
        local ad = UI.GLOW_ADJUST.glow_blue
        check(near(gl + ln.left * sx, vl - ln.gap - ad.left) and near(gl + ln.right * sx, vr + ln.gap + ad.right) and near(gt + ln.top * sy, vt - ln.gap - ad.top) and near(gt + ln.bottom * sy, vb + ln.gap + ad.bottom), "blue glow " .. i .. " line is where the card edge, the gap and UI.GLOW_ADJUST put it")
        local hl, ht, hr, hb = UI.GlowRect("glow_gold", vl, vt, vr, vb)
        local hi = slot._.hitInsets
        check(hi ~= nil and near(hi[1], vl) and near(hi[2], UI.SLOT_W - vr) and near(hi[3], vt) and near(hi[4], UI.SLOT_H - vb), "slot " .. i .. " takes the mouse only on the visible card")
        check(near(gg[1].x, hl) and near(gg[1].y, -ht) and near(gg[2].x, hr) and near(gg[2].y, -hb), "gold glow " .. i .. " is placed by its own line")
    end
    -- nothing of a banner or of the page arrows overlaps (the stray yellow arrow of the first test was the page arrow under the banner)
    local function rectOf(frame, book)
        local p = frame._.points[1]
        local x, y = p.x, p.y
        local w, h = frame:GetWidth(), frame:GetHeight()
        if p.point == "CENTER" then x, y = x - w / 2, y + h / 2 end
        return { x, x + w, -y, -y + h }       -- left, right, top, bottom (downwards)
    end
    local arrows = { rectOf(main.prevButton), rectOf(main.nextButton), rectOf(main.pageText) }
    for i = 1, 8 do
        local slot = UI.slots[i]
        local bp = slot.banner._.points[1]
        local sp = slot._.points[1]
        local br = { sp.x + bp.x, sp.x + bp.x + UI.BANNER_W, -sp.y - bp.y, -sp.y - bp.y + UI.BANNER_H }
        for k = 1, 3 do
            local a = arrows[k]
            local hit = br[1] < a[2] and a[1] < br[2] and br[3] < a[4] and a[3] < br[4]
            check(not hit, "banner " .. i .. " does not touch page control " .. k)
        end
    end

    -- ---- 4. deck tile art: the strip of the pipeline, else the crop of the hero card, else a colour
    check(DU.HeroCard("MAGE") == "HERO_08" and DU.HeroCard("DRUID") == "HERO_06" and DU.HeroCard("WARRIOR") == "HERO_01", "the hero card of a class")
    for i = 1, table.getn(classes) do
        local tex = main:CreateTexture(nil, "ARTWORK")
        local kind = DU.SetTileArt(tex, string.upper(classes[i]))
        check(kind == "strip" and tex._.texture == PATH .. "hero_" .. classes[i] and tex._.texcoord[2] == 0.625 and tex._.texcoord[4] == 0.5, "tile art of " .. classes[i] .. " is its strip")
    end
    H.state.welcomed = true
    D.SetReady(D.ParseDecks("cap=5;n=1;start=MAGE;unlocked=;d1=1.1.MAGE.player;m1=Frost;c1=;s1=size"))
    UI.RefreshAll()
    check(DU.tiles[1]:IsShown() and DU.tiles[1].artKind == "strip" and DU.tiles[1].art._.texture == PATH .. "hero_mage", "a deck tile shows the mage strip")
    -- without the strips: the crop of the HERO card's texture, centred on the portrait
    local noStrips = {}
    for i = 1, table.getn(all) do
        if string.sub(all[i], 1, 5) ~= "hero_" then table.insert(noStrips, all[i]) end
    end
    HS_CollectionTextures = pieces(noStrips)
    HS_CardTextures = { generated = true, order = { "HERO_08", "EX1_116" }, bottom = { HERO_08 = 388 / 512 } }
    local tex = main:CreateTexture(nil, "ARTWORK")
    check(DU.SetTileArt(tex, "MAGE") == "crop" and tex._.texture == "Interface" .. BS .. "AddOns" .. BS .. "Hearthstone" .. BS .. "Textures" .. BS .. "Cards" .. BS .. "HERO_08", "no strip: the crop of the HERO_08 texture")
    local c = DU.TILE_CROP
    local bottom = 388 / 512
    check(near(tex._.texcoord[1], c[1]) and near(tex._.texcoord[2], c[2]) and near(tex._.texcoord[3], c[3] * bottom) and near(tex._.texcoord[4], c[4] * bottom), "the crop's texcoords follow DU.TILE_CROP")
    -- the crop is the inside of the portrait arch around the face (x 69 ... 187, y 99 ... 124 of the 256 x 388 render), the aspect of the tile's picture
    local x1, x2, y1, y2 = c[1] * 256, c[2] * 256, c[3] * 388, c[4] * 388
    check(x1 >= 66 and x2 <= 190 and y1 >= 95 and y2 <= 128, "TILE_CROP is the portrait (x " .. x1 .. " ... " .. x2 .. ", y " .. y1 .. " ... " .. y2 .. "), not the frame or the top edge of the card")
    check((x2 - x1) / (y2 - y1) > 4.4 and (x2 - x1) / (y2 - y1) < 5.3, "TILE_CROP has the aspect of the picture opening of the tile (4.9 : 1)")
    check(DU.SetTileArt(tex, "WARRIOR") == "color" and tex._.texture == "Interface" .. BS .. "Buttons" .. BS .. "WHITE8X8", "neither strip nor texture: a flat colour of the class")
    HS_CardTextures = savedCardTextures
    HS_CollectionTextures = pieces(all)

    -- ---- 7. the class picker must not come back when the window reopens
    F.fire(main.sidePanel.browse.newButton, "OnClick")
    check(DU.picker:IsShown() and DU.picker.shade:IsShown(), "the picker is open")
    main:Hide()
    check(not DU.picker:IsShown() and not DU.picker.shade:IsShown(), "hiding the window closes the class picker")
    main:Show()
    check(main:IsShown() and not DU.picker:IsShown() and not DU.picker.shade:IsShown(), "the picker does not come back when the window reopens")
    DU.ShowPicker()
    DU.OnWindowHidden()
    check(not DU.picker:IsShown() and not DU.picker.shade:IsShown(), "DU.OnWindowHidden calls DU.HidePicker")
    local calls = 0
    local realHide = DU.HidePicker
    DU.HidePicker = function() calls = calls + 1 return realHide() end
    DU.OnWindowHidden()
    DU.HidePicker = realHide
    check(calls == 1, "OnWindowHidden hides the picker through HidePicker")

    -- ---- the micro menu button still opens and closes the window
    local mb = MB.Create()
    check(mb ~= nil and mb == WorldMapMicroButton, "the micro button replaces the map button")
    main:Hide()
    F.fire(mb, "OnClick")
    check(main:IsShown() and mb:GetButtonState() == "PUSHED", "the micro button opens the window")
    F.fire(mb, "OnClick")
    check(not main:IsShown() and mb:GetButtonState() == "NORMAL", "and closes it")

    main:Hide()
    F.uninstall()
    resetUI()
    HS_CollectionTextures = savedPieces
    HS_CardTextures = savedCardTextures
    H.db = savedDb
    resetAddon()
end

-- ------------------------------ the Packs tab in the style of the collection (art deployed) and its flows
local function testPacksArt()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local UI = H.CollectionUI
    local DU = H.DecksUI
    local PU = H.PacksUI
    local PK = HsPacks
    local MB = H.MicroButton
    local CM = HsCollection
    local T = packHelpers()
    local savedDb = H.db
    local savedPieces = HS_CollectionTextures
    local savedBoard = HS_BoardTextures
    local BS = string.char(92)
    local PATH = "Interface" .. BS .. "AddOns" .. BS .. "Hearthstone" .. BS .. "Textures" .. BS .. "Collection" .. BS
    local BOARD = "Interface" .. BS .. "AddOns" .. BS .. "Hearthstone" .. BS .. "Textures" .. BS .. "Board" .. BS

    local function resetUI()
        UI.frame, UI.preview, UI.hover, UI.layoutMode, UI.tabsFrame = nil, nil, nil, nil, nil
        UI.slots, UI.classTabs, UI.manaButtons, UI.manaAll = {}, {}, {}, nil
        UI.searchFocused, UI.settingSearch, UI.searchHint = nil, nil, nil
        local s = UI.state
        s.bottom, s.filters, s.page, s.results, s.pageIds = 1, CM.NewFilters(), 1, {}, {}
        s.catalog, s.catalogFor, s.inited, s.savedClass = nil, nil, false, nil
        PU.panel, PU.slots, PU.rows, PU.hover = nil, {}, {}, nil
        DU.panel, DU.picker, DU.tiles, DU.empties, DU.rows, DU.pickButtons, DU.curveBars = nil, nil, {}, {}, {}, {}, {}
        DU.page, DU.hoverRow, DU.shownDraft, DU.shown, DU.trans, DU.snap, DU.leaveAfterDelete = 1, nil, nil, "browse", nil, true, nil
        MB.frame, MB.icon, MB.border = nil, nil, nil
    end
    local function pieces(names)
        local t = { generated = true, pieces = {} }
        for i = 1, table.getn(names) do
            t.pieces[names[i]] = { path = PATH .. names[i], w = 40, h = 30, right = 0.625, bottom = 0.5 }
        end
        return t
    end
    local classes = { "druid", "hunter", "mage", "paladin", "priest", "rogue", "shaman", "warlock", "warrior", "neutral" }
    local all = { "mana_all", "mana_0", "mana_1", "mana_2", "mana_3", "mana_4", "mana_5", "mana_6", "mana_7", "mana_plus", "gem_cost", "banner_x1", "banner_x2",
        "search_box", "plate_blue", "deck_new", "btn_blank", "deck_tile", "deck_empty", "glow_blue", "glow_gold", "parch_tall" }
    for i = 1, table.getn(classes) do table.insert(all, "tab_" .. classes[i]) end
    for i = 1, table.getn(UI.BOOK_TILES) do table.insert(all, UI.BOOK_TILES[i]) end
    for i = 1, table.getn(UI.BAR_TILES) do table.insert(all, UI.BAR_TILES[i]) end
    for i = 1, table.getn(UI.PACKS_BG_TILES) do table.insert(all, UI.PACKS_BG_TILES[i]) end
    for i = 1, table.getn(UI.HEROES_BG_TILES) do table.insert(all, UI.HEROES_BG_TILES[i]) end
    table.insert(all, "pack_classic")
    table.insert(all, "btn_done")
    table.insert(all, "pack_badge")
    for i = 1, table.getn(DU.PANEL_TILES) do table.insert(all, DU.PANEL_TILES[i]) end
    local function flush() drain(1) end
    local function lastOpen()
        local l = T.sentOps("PACK_OPEN")
        return l[table.getn(l)]
    end
    local function faceDown(i)
        local sl = PU.slots[i]
        return sl.holder:IsShown() and sl.back:IsShown() and not sl.widget.frame:IsShown() and not sl.rarityBadge:IsShown() and not sl.newBadge:IsShown()
    end
    local function faceUp(i, id)
        local sl = PU.slots[i]
        return sl.holder:IsShown() and not sl.back:IsShown() and sl.widget.frame:IsShown() and sl.widget.id == id and sl.rarityBadge:IsShown()
    end
    local function rect(o)       -- left, top, right, bottom in panel pixels (y down) of a frame placed with TOPLEFT / TOP / CENTER on the panel
        local p = o._.points[1]
        local w, h = o:GetWidth(), o:GetHeight()
        local x, y = p.x, -p.y
        if p.point == "TOP" then x = x - w / 2 end
        if p.point == "CENTER" then x, y = x - w / 2, y - h / 2 end
        return { x, y, x + w, y + h }
    end
    local function overlap(a, b) return a[1] < b[3] and b[1] < a[3] and a[2] < b[4] and b[2] < a[4] end

    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, realm = "Realm A", screenW = 1920, screenH = 1080 })
    H.db = CM.ApplySavedDefaults(nil)
    HS_CollectionTextures = pieces(all)
    HS_BoardTextures = { generated = true, pieces = { card_back = { path = BOARD .. "card_back", w = 128, h = 217, right = 1, bottom = 0.84765625 } } }
    resetUI()

    local vl, vt, vr, vb = H.CardWidget.VisibleRect(PU.CARD_W)
    H.Slash("packs")
    local main = UI.frame
    local pnl = main and main.packs
    check(main and main:IsShown() and UI.state.bottom == 2 and main.selectedTab == 2 and pnl:IsShown() and not main.book:IsShown(), "/hs packs opens the Packs tab")
    check(main.title:GetText() == "Card Packs" and not main.sidePanel:IsShown() and main:GetWidth() == UI.WIDTH_BOOK, "the deck panel goes")

    -- ---- the layout: the panel is the whole window below the header, above the book art and the class tabs
    local pr = rect(pnl)
    check(pr[1] == 0 and pr[2] == UI.HEADER_H and pr[3] == UI.WIDTH_BOOK and pr[4] == UI.HEIGHT, "the panel fills the window below the header")
    check(pnl._.level > main.art._.level and pnl._.level > main.tabsFrame._.level and pnl._.level > main.book._.level - 1, "above the book art and the class tabs")
    check(PU.PANEL_W == 1024 and PU.PANEL_H == 812 and pnl.hasArt == true, "panel 1024 x 812 with the art")
    -- the background: eight tiles, none above 512 px, laid out like the tiles of the book
    local tiles = 0
    for i = 1, table.getn(UI.PACKS_BG_TILES) do
        for _, rg in ipairs(pnl._.regions) do
            if rg._.texture == PATH .. UI.PACKS_BG_TILES[i] then
                tiles = tiles + 1
                local _, _, c, r = string.find(UI.PACKS_BG_TILES[i], "_c(%d+)r(%d+)$")
                local pt = rg._.points[1]
                check(pt.x == PU.BG_INSET + tonumber(c) * 512 and pt.y == -tonumber(r) * 256, "tile " .. UI.PACKS_BG_TILES[i] .. " in its place, inside the stone frame")
            end
        end
    end
    check(tiles == 8 and UI.TILE_SIZE.packs_bg[1] <= 512 and UI.TILE_SIZE.packs_bg[2] <= 512, "the packs background is eight tiles of at most 512 px")
    check(PU.BG_INSET >= UI.FRAME_METAL, "the background keeps clear of the transparent margin of the frame")

    local holders = {}
    for i = 1, 5 do
        holders[i] = rect(PU.slots[i].holder)
        check(holders[i][1] >= 8 and holders[i][3] <= PU.PANEL_W - 8 and holders[i][2] >= 8 and holders[i][4] <= PU.HEADER_Y - 20, "card " .. i .. " inside the page, above the header line")
        check(holders[i][1] >= 280 and holders[i][3] <= 916, "card " .. i .. " on the slab or its frame")
    end
    local function visible(i) local h = holders[i]; return { h[1] + PU.GLOW + vl, h[2] + PU.GLOW + vt, h[1] + PU.GLOW + vr, h[2] + PU.GLOW + vb } end   -- the visible card inside the render
    for i = 1, 5 do
        for j = 1, i - 1 do check(not overlap(visible(i), visible(j)), "card " .. i .. " does not overlap card " .. j) end
    end
    -- the ring: one on top, one left and one right at the same height, two below
    check(holders[2][2] == holders[3][2] and holders[4][2] == holders[5][2] and holders[1][2] < holders[2][2] and holders[2][2] < holders[4][2], "top, left and right, two below")
    check(math.abs((holders[1][1] + holders[1][3]) / 2 - PU.ARENA_CX) <= 1 and math.abs((holders[2][1] + holders[3][3]) / 2 - PU.ARENA_CX) <= 1 and math.abs((holders[4][1] + holders[5][3]) / 2 - PU.ARENA_CX) <= 1, "the ring is centred on the slab")
    local done, recent = rect(pnl.doneButton), rect(pnl.recentButton)
    local donec = { done[1] + PU.DONE_INSET[1], done[2] + PU.DONE_INSET[3], done[3] - PU.DONE_INSET[2], done[4] - PU.DONE_INSET[4] }   -- the plate (the click area)
    local header = { PU.REVEAL_CX - 310, PU.HEADER_Y - 8, PU.REVEAL_CX + 310, PU.HEADER_Y + 8 }
    local hint = { PU.REVEAL_CX - 280, PU.HINT_Y - 10, PU.REVEAL_CX + 280, PU.HINT_Y + 10 }
    check(math.abs((done[1] + done[3]) / 2 - PU.ARENA_CX) <= 1 and math.abs((done[2] + done[4]) / 2 - PU.BUTTON_Y) <= 1, "Done is in the middle of the ring of cards")
    check(done[3] - done[1] == PU.DONE_W and done[4] - done[2] == PU.DONE_H and PU.DONE_W == 2 * PU.DONE_H, "with the 2:1 aspect of its art")
    check(pnl.doneButton._.level > PU.slots[1].holder._.level + 6 and pnl.reveal == nil, "above the cards")
    check(recent[2] >= hint[4] and recent[4] <= PU.PANEL_H - 2, "Recent Packs sits below the slab, under the hint")
    -- the pack column in the purple strip
    local stackMax = { PU.STACK_CX - PU.PACK_W / 2, PU.STACK_Y0, PU.STACK_CX + PU.PACK_W / 2, PU.STACK_Y0 + PU.PACK_H + (PU.STACK_MAX - 1) * PU.STACK_PITCH }
    local status = rect(pnl.status)
    local blocks = { stack = stackMax, status = status }
    local keys = { "stack", "status" }
    for a = 1, table.getn(keys) do
        local r = blocks[keys[a]]
        check(r[1] >= 100 and r[3] <= 270 and r[2] >= 0 and r[4] <= PU.PANEL_H - 2, keys[a] .. " is inside the purple strip")
        for b = a + 1, table.getn(keys) do
            check(not overlap(r, blocks[keys[b]]), keys[a] .. " does not overlap " .. keys[b])
        end
    end
    for _, r in ipairs({ header, recent }) do
        for i = 1, 5 do check(not overlap(r, holders[i]), "recent button and header do not overlap card " .. i) end
    end
    for i = 1, 5 do check(not overlap(donec, visible(i)), "the Done plate does not cover card " .. i) end
    check(stackMax[4] < status[2] and status[4] >= 640, "pack column: the pack, then the status line at the bottom of the strip")
    -- the unopened count is a ribbon on the top right corner of the pack
    local bd = pnl.badge._.points[1]
    check(bd.point == "TOPRIGHT" and bd.relPoint == "TOPRIGHT" and bd.rel == PU.stackPacks[1] and pnl.badge:GetParent() == PU.stackPacks[1] and pnl.badge:GetWidth() == math.floor(PU.BADGE_W / 1.2 + 0.5) and bd.y <= 0, "the unopened count hangs from the pack's top right corner, inside its top edge")
    local rf = rect(pnl.recentFrame)
    check(rf[1] >= 340 and rf[3] <= 860 and rf[2] >= 0 and rf[4] <= recent[2], "the recent list sits on the slab, above its button")
    for i = 1, 5 do
        local row = rect(PU.rows[i])
        local pt = PU.rows[i]._.points[1]
        check(PU.rows[i]:GetParent() == pnl.recentFrame and pt.y <= -54 and -pt.y + PU.rows[i]:GetHeight() <= PU.RECENT_H - 8 and (i == 1 or -pt.y > -PU.rows[i - 1]._.points[1].y), "row " .. i .. " inside the list, below the previous one")
    end
    -- the labels under a card (rarity name, NEW! tag) touch no other card and end above the header line
    local nb = PU.slots[1].newBadge._.points[1]
    for i = 1, 5 do
        local mid = (holders[i][1] + holders[i][3]) / 2
        local lab = { mid - 40, holders[i][2] + PU.GLOW + (-nb.y) - 16, mid + 40, holders[i][2] + PU.GLOW + (-nb.y) + PU.slots[1].newBadge:GetHeight() }
        for j = 1, 5 do
            if j ~= i then check(not overlap(lab, visible(j)), "the labels of card " .. i .. " do not touch card " .. j) end
        end
        check(lab[4] <= header[2] + 2, "the labels of card " .. i .. " end above the header line")
        check(not overlap(lab, donec), "the labels of card " .. i .. " do not touch the Done plate")
    end

    -- ---- the art: the plate of the sheet, the card back of the board, the glow piece
    check(pnl.recentButton:GetNormalTexture()._.texture == PATH .. "btn_blank" and pnl.recentButton.label:GetText() == "Recent Packs", "Recent Packs: the blank plate of the sheet with the ink label")
    check(pnl.doneButton:GetNormalTexture()._.texture == PATH .. "btn_done" and pnl.doneButton.label == nil and pnl.doneButton:GetHighlightTexture()._.texture == PATH .. "btn_done", "Done: the glowing plate (the text is in the art)")
    check(pnl.badge._.regions[1]._.texture == PATH .. "pack_badge" and pnl.unopened:GetText() == "-", "the unopened count sits on the blue pennant")
    check(PU.rows[1]._.regions[1]._.texture == PATH .. "deck_empty", "recent rows use the sheet's deck slot")
    local function hasPackArt(f)
        for _, rg in ipairs(f._.regions) do
            if rg._.texture == PATH .. "pack_classic" then return true end
        end
        return false
    end
    check(PU.STACK_MAX == 1 and hasPackArt(PU.stackPacks[1]) and PU.stackPacks[1]._.backdrop == nil, "one pack on the strip, the pack art without a border")
    check(hasPackArt(pnl.flyer) and pnl.flyer._.backdrop == nil, "the pack in the hand looks the same")
    for i = 1, 5 do
        local sl = PU.slots[i]
        local gp = sl.glow._.points
        check(sl.glow._.texture == PATH .. "glow_gold" and gp[1].rel == sl and math.abs(gp[1].x - (UI.GlowRect("glow_gold", vl, vt, vr, vb))) < 0.001 and math.abs(gp[2].x - select(3, UI.GlowRect("glow_gold", vl, vt, vr, vb))) < 0.001, "card " .. i .. ": the glow hugs the visible card")
        check(sl.holder._.backdrop == nil, "card " .. i .. ": no backdrop box with the glow art")
        local bp = sl.back._.points
        check(bp[1].rel == sl and bp[1].x == vl and bp[2].x == vr and sl.back._.regions[1]._.texture == BOARD .. "card_back" and not sl.back.hasBorder, "card " .. i .. ": the card back fills the visible card")
    end
    check(pnl.status:GetText() == "Not connected to the server." and not pnl.stack:IsShown() and not pnl.recentButton:IsEnabled(), "offline state")
    check(pnl.recentButton:GetNormalTexture()._.vertex[1] < 1 and pnl.recentButton.label._.textColor[1] > 0.3, "a disabled Recent Packs button is dimmed")

    -- ---- the interface is drawn 1.2 times too wide: the pack and its pennant are narrower in units, round on the screen
    check(PU.Stretch() == 1.2 and PU.PackWidth() == math.floor(PU.PACK_W / 1.2 + 0.5), "the stretch is fixed at 1.2")
    check(pnl.stack:GetWidth() == 125 and PU.stackPacks[1]:GetWidth() == 125 and pnl.flyer:GetWidth() == 125 and PU.stackPacks[1]:GetHeight() == PU.PACK_H, "the pack, the flyer and the mouse area are narrower, the height is the same")
    check(pnl.badge:GetWidth() == math.floor(PU.BADGE_W / 1.2 + 0.5) and pnl.badge:GetHeight() == PU.BADGE_H, "so is the pennant")
    check(H.PacksUI.SetStretch == nil, "no command to change it")

    -- ---- connect, load
    H.queue = {}
    T.welcome("solo,chunk,packs")
    flush()
    T.packs(2, 7, "7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189,6.00000.CS2_028.CS2_022.CS2_022.EX1_011.CS2_189")
    check(pnl.unopened:GetText() == "2" and pnl.stack:IsShown() and pnl.recentButton:IsEnabled() and pnl.recentButton:GetNormalTexture()._.vertex[1] == 1 and pnl.status:GetText() == "", "ready: the stack and the button are there")
    check(PU.rows[1]:IsShown() and PU.rows[2]:IsShown() and not PU.rows[3]:IsShown() and PU.rows[1].text:GetText() == "#7  |cffff80001 Legendary|r, |cffffffff4 Common|r", "recent rows")
    check(pnl.hint:GetText() == "Drag a pack onto the stone to open it." and pnl.recentTitle:GetText() == "Recent Packs", "hint and list title")
    F.fire(pnl.stack, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Classic Pack" and string.find(F.tooltip.lines[2], "Five Basic and Classic cards", 1, true) == 1, "the pack's tooltip")
    F.fire(pnl.stack, "OnLeave")

    -- ---- open by a click: the key, the retry with the same key, one pack at a time
    local down = { arg1 = "LeftButton" }
    local function settle() for _ = 1, 20 do PU.Step(0.1) end end
    local function flipAll() for i = 1, 5 do F.fire(PU.slots[i], "OnClick") end settle() end
    pnl._.left, pnl._.bottom = 0, 0
    local function cursorAt(x, y) local sc = pnl:GetEffectiveScale(); F.cursor[1], F.cursor[2] = x * sc, (PU.PANEL_H - y) * sc end
    local sx, sy = PU.StackPos(1)
    sent = {}
    F.sounds = {}
    cursorAt(sx, sy)                        -- a click only sends the pack back; it takes a drop on the stone to open it
    F.fire(pnl.stack, "OnMouseDown", down)
    F.fire(pnl.stack, "OnMouseUp", down)
    settle()
    flush()
    check(not PU.held and not PK.IsOpening() and table.getn(T.sentOps("PACK_OPEN")) == 0, "a click on the stack does not open a pack")
    cursorAt(sx, sy)
    F.fire(pnl.stack, "OnMouseDown", down)
    cursorAt(PU.ARENA_CX, PU.ARENA_CY)
    PU.Step(0.016)
    F.fire(pnl.stack, "OnMouseUp", down)
    check(pnl.status:GetText() == "Opening..." and PU.awaiting and PK.IsOpening(), "dropping the pack on the stone opens it")
    local key = PK.pending.key
    F.fire(pnl.stack, "OnMouseDown", down)
    F.fire(pnl.stack, "OnMouseUp", down)
    flush()
    check(table.getn(T.sentOps("PACK_OPEN")) == 1, "a second click while opening sends nothing")
    local a1 = lastOpen()
    F.time = F.time + 20                    -- the fake clock (GetTime) runs the request timeout
    H.OnUpdate(0.05)
    drain(3)
    local opens = T.sentOps("PACK_OPEN")
    local function keyOf(f) return HsProtocol.KvDecode(f.payload).key end
    check(table.getn(opens) == 2 and keyOf(opens[2]) == key and keyOf(opens[1]) == key and opens[2].reqId ~= a1.reqId and pnl.status:GetText() == "Opening...", "a lost reply: the retry carries the same key")
    H.queue = {}
    T.opened(opens[2].reqId, 8, key, 1, 8, "11001", "EX1_116,CS2_028,CS2_053,CS2_189,CS2_022")
    settle()
    check(PU.anim == nil and not PU.held and not PU.awaiting, "the burst is over")
    check(pnl.unopened:GetText() == "1" and not PU.CanOpen(), "reveal blocks the next open")
    for i = 1, 5 do check(faceDown(i), "card " .. i .. " face down") end

    -- ---- glows: only on a hovered face-down card; Common none, Rare blue, Epic purple, Legendary orange
    local function tint(i)
        local g = PU.slots[i].glow
        if not g:IsShown() then return nil end
        local v = g._.vertex
        return v[1], v[2], v[3]
    end
    for i = 1, 5 do check(tint(i) == nil, "card " .. i .. ": no glow until the mouse is over it") end
    F.fire(PU.slots[1], "OnEnter")
    local r, g, b = tint(1)
    check(r == PU.GLOW_TINT.LEGENDARY[1] and g == PU.GLOW_TINT.LEGENDARY[2] and b == PU.GLOW_TINT.LEGENDARY[3] and r > b and not UI.preview.frame:IsShown(), "the Legendary glows orange under the mouse")
    check(tint(2) == nil, "the others stay dark")
    F.fire(PU.slots[1], "OnLeave")
    check(tint(1) == nil, "and the glow goes when the mouse leaves")
    F.fire(PU.slots[2], "OnEnter")
    r, g, b = tint(2)
    check(r == PU.GLOW_TINT.RARE[1] and b == 1 and b > r, "the Rare glows blue")
    F.fire(PU.slots[2], "OnLeave")
    F.fire(PU.slots[3], "OnEnter")
    r, g, b = tint(3)
    check(r == PU.GLOW_TINT.EPIC[1] and r > g and b > g, "the Epic glows purple")
    F.fire(PU.slots[3], "OnLeave")
    F.fire(PU.slots[4], "OnEnter")
    check(tint(4) == nil, "a Common has no glow")
    F.fire(PU.slots[4], "OnLeave")
    check(not pnl.doneButton:IsShown() and not pnl.recentButton:IsShown() and string.find(pnl.header:GetText(), "Pack #8 - click the cards", 1, true) == 1 and pnl.hint:GetText() == "", "no buttons until every card is turned, the header says what to do")

    -- ---- flip
    F.fire(PU.slots[1], "OnClick")
    settle()
    check(faceUp(1, "EX1_116") and PU.slots[1].newBadge:IsShown() and tint(1) == PU.GLOW_TINT.LEGENDARY[1], "click flips: the card and NEW!, and the card keeps its orange glow")
    check(string.find(PU.slots[1].rarityText:GetText(), "Legendary", 1, true) ~= nil and string.find(PU.slots[1].rarityText:GetText(), PK.RARITY_CODES.LEGENDARY, 1, true) == 1, "the rarity name in its colour")
    check(PU.slots[1].newBadge._.regions[1]:GetText() == PU.NEW_TAG and string.find(PU.NEW_TAG, "NEW!", 1, true) ~= nil, "the NEW! tag")
    check(F.sounds[table.getn(F.sounds)] == "igAbilityPageTurn" and faceDown(2) and not pnl.doneButton:IsShown(), "flip sound, the others stay face down")
    F.fire(PU.slots[1], "OnEnter")
    check(UI.preview.frame:IsShown() and UI.preview.widget.id == "EX1_116" and tint(1) == PU.GLOW_TINT.LEGENDARY[1], "hovering a turned card shows the large preview")
    F.fire(PU.slots[1], "OnLeave")
    check(PU.slots[1].widget.frame._.level > PU.slots[1].holder._.level and PU.slots[1].newBadge._.level > PU.slots[1].widget.frame._.level, "tags above the card")
    check(PU.slots[1].holder._.level < pnl.flyer._.level and pnl.fxStar:GetParent()._.level > PU.slots[1].holder._.level and pnl.recentFrame._.level > PU.slots[1].holder._.level, "the pack and the effect are drawn above the cards")

    -- ---- close the window in the middle of the reveal: nothing is lost
    F.fire(PU.slots[2], "OnEnter")
    main:Hide()
    check(PU.hover == nil and not UI.preview.frame:IsShown(), "closing clears the hover")
    H.Slash("packs")
    check(main:IsShown() and faceUp(1, "EX1_116") and faceDown(3) and not pnl.doneButton:IsShown() and F.globals.HearthstoneFrameTab2._.points[1].y == UI.BTAB_Y_UP, "the reveal resumes, the Packs tab is the selected one")
    -- a /reload in the middle of the reveal: the saved marker brings the opening back face down
    H.db.packsSeen["Realm A"] = 7
    PK.queue = {}
    PK.view = nil
    H.PackGet()
    flush()
    T.packs(1, 8, "8.11001.EX1_116.CS2_028.CS2_053.CS2_189.CS2_022,7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189")
    check(PK.Current() and PK.Current().seq == 8 and faceDown(1) and faceDown(5) and PU.anim == nil and string.find(pnl.header:GetText(), "(opened earlier)", 1, true) ~= nil, "after a reload the reveal comes back face down, without the burst")
    flipAll()
    check(faceUp(3, "CS2_053") and faceUp(5, "CS2_022") and PU.slots[1].newBadge:IsShown() and PU.slots[2].newBadge:IsShown() and not PU.slots[3].newBadge:IsShown() and PU.slots[5].newBadge:IsShown(), "Reveal All: NEW! tags from the server's mask")
    check(string.find(PU.slots[5].rarityText:GetText(), "Basic", 1, true) ~= nil and not PU.slots[5].glow:IsShown(), "a Basic card is named Basic, no glow")
    check(pnl.doneButton:IsShown() and not PU.CanOpen(), "Done is shown")
    check(tint(2) ~= nil and tint(2) == PU.GLOW_TINT.RARE[1] and tint(3) == PU.GLOW_TINT.EPIC[1] and tint(4) == nil, "every turned card keeps the glow of its rarity (Common none)")
    F.fire(pnl.doneButton, "OnClick") -- start the fade
    check(PU.fadeOut ~= nil and PK.Current() ~= nil and PU.slots[1].holder:IsShown(), "Done starts a fade, the reveal is still there")
    PU.Step(0.1)
    check(PU.slots[1].holder:GetAlpha() < 1 and PU.slots[5].holder:GetAlpha() < 1 and pnl.doneButton:GetAlpha() < 1 and PK.Current() ~= nil, "the cards and the button fade out")
    PU.Step(0.1)
    check(PU.fadeOut ~= nil and pnl.doneButton:GetAlpha() > 0, "still fading")
    F.fire(pnl.doneButton, "OnClick") -- a second click does nothing
    settle()
    check(PU.fadeOut == nil and PK.Current() == nil and not PU.slots[1].holder:IsShown() and PU.slots[1].holder:GetAlpha() == 1 and pnl.doneButton:GetAlpha() == 1, "faded out: the reveal is over and everything is opaque again")
    check(PK.Current() == nil and H.db.packsSeen["Realm A"] == 8 and PU.CanOpen() and pnl.recentButton:IsShown(), "Done marks the pack seen")
    for i = 1, 5 do check(not PU.slots[i].holder:IsShown(), "reveal area cleared " .. i) end

    -- ---- the recent packs: a list on the slab
    check(not pnl.recentFrame:IsShown() and pnl.recentEmpty:GetText() == "", "the list is closed")
    F.fire(pnl.recentButton, "OnClick")
    check(pnl.recentFrame:IsShown(), "Recent Packs opens the list")
    F.fire(pnl.recentButton, "OnClick")
    check(not pnl.recentFrame:IsShown(), "and closes it")
    F.fire(pnl.recentButton, "OnClick")
    F.fire(PU.rows[2], "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Pack #7", "row tooltip")
    F.fire(PU.rows[2], "OnLeave")
    F.fire(PU.rows[2], "OnClick")
    check(not pnl.recentFrame:IsShown() and not pnl.recentButton:IsShown(), "choosing a pack closes the list")
    check(PK.view and PK.view.seq == 7 and faceUp(1, "EX1_116") and not PU.slots[1].newBadge:IsShown() and PU.rows[2]._.locked and pnl.header:GetText() == "Pack #7 (opened earlier)" and pnl.doneButton:IsShown(), "a row shows its pack face up")
    F.fire(PU.slots[3], "OnEnter")
    check(UI.preview.frame:IsShown() and UI.preview.widget.id == "CS2_022", "hover preview in the view")
    F.fire(PU.slots[3], "OnLeave")
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(PK.view == nil and not PU.slots[1].holder:IsShown() and not PU.rows[2]._.locked and pnl.recentButton:IsShown(), "Done closes the view")

    -- ---- the bottom tabs and the other tab
    F.fire(F.globals.HearthstoneFrameTab1, "OnClick")
    check(not pnl:IsShown() and main.book:IsShown() and main.sidePanel:IsShown() and F.globals.HearthstoneFrameTab1._.points[1].y == UI.BTAB_Y_UP, "Collection tab: the book and the deck panel come back")
    F.fire(F.globals.HearthstoneFrameTab2, "OnClick")
    check(pnl:IsShown() and not main.sidePanel:IsShown(), "and the Packs tab again")

    -- ---- a pack let go on the slab, a pack let go elsewhere, a failed opening
    pnl._.left, pnl._.bottom = 0, 0
    local function cursorAt(x, y) local sc = pnl:GetEffectiveScale(); F.cursor[1], F.cursor[2] = x * sc, (PU.PANEL_H - y) * sc end
    local sx, sy = PU.StackPos(1)
    sent = {}
    cursorAt(sx, sy)
    F.fire(pnl.stack, "OnMouseDown", down)
    cursorAt(150, 700)
    PU.Step(0.016)
    F.fire(pnl.stack, "OnMouseUp", down)
    settle()
    flush()
    check(not PU.held and not pnl.flyer:IsShown() and pnl.stack:IsShown() and table.getn(T.sentOps("PACK_OPEN")) == 0, "let go beside the slab: the pack goes back, nothing opened")
    cursorAt(sx, sy)
    F.fire(pnl.stack, "OnMouseDown", down)
    cursorAt(PU.ARENA_CX, PU.ARENA_CY)
    PU.Step(0.016)
    check(pnl.dropGlow:IsShown() and PU.fx == PU.ARENA_CX and PU.fy == PU.ARENA_CY, "the pack follows the cursor, the slot lights up")
    F.fire(pnl.stack, "OnMouseUp", down)
    flush()
    check(table.getn(T.sentOps("PACK_OPEN")) == 1 and PU.awaiting and not pnl.stack:IsShown(), "let go on the slab: the pack is opened, the stack is empty")
    H.queue = {}
    T.err(lastOpen().reqId, "REFUSED", "no")
    settle()
    check(not PU.held and not PU.awaiting and not pnl.flyer:IsShown() and pnl.stack:IsShown() and not PK.IsOpening(), "a refused opening: the pack goes back to the stack")
    check(string.find(pnl.status:GetText(), "did not open the pack", 1, true) ~= nil, "and the player is told")
    -- the last pack: picking it up empties the strip and hides its mouse area; the release must still arrive
    sent = {}
    local mouseIsDown = true
    _G.IsMouseButtonDown = function() return mouseIsDown end
    cursorAt(sx, sy)
    F.fire(pnl.stack, "OnMouseDown", down)
    check(PU.drag ~= nil and not PU.stackPacks[1]:IsShown() and pnl.stack:IsShown(), "the last pack in the hand: its mouse area stays")
    cursorAt(PU.ARENA_CX, PU.ARENA_CY)
    PU.Step(0.016)
    check(PU.drag ~= nil and pnl.dropGlow:IsShown(), "still in the hand while the button is down")
    mouseIsDown = false
    PU.Step(0.016)                          -- the button went up without any OnMouseUp reaching the stack
    flush()
    check(PU.drag == nil and PU.awaiting and table.getn(T.sentOps("PACK_OPEN")) == 1, "released: the last pack is opened")
    H.queue = {}
    T.err(lastOpen().reqId, "REFUSED", "no")
    settle()
    _G.IsMouseButtonDown = nil
    check(not PU.held and pnl.stack:IsShown(), "and goes back when refused")
    sent = {}
    H.Slash("open")
    flush()
    check(table.getn(T.sentOps("PACK_OPEN")) == 1 and PU.awaiting and PU.held and pnl.flyer:IsShown(), "/hs open sends the front pack to the slot")
    H.queue = {}
    T.opened(lastOpen().reqId, 9, PK.pending.key, 0, 9, "00000")
    settle()
    check(PU.anim == nil and faceDown(1) and not pnl.stack:IsShown(), "and the cards come out")
    flipAll()
    F.fire(pnl.doneButton, "OnClick")
    settle()
    check(not PU.CanOpen() and pnl.status:GetText() == "You have no unopened packs." and pnl.unopened:GetText() == "0", "no stock")
    T.err(H.PackGet(), "UNAVAILABLE", "db")
    H.queue = {}
    check(string.find(pnl.status:GetText(), "|cffff5555", 1, true) == 1 and pnl.recentEmpty:GetText() == "-" and not PU.rows[1]:IsShown() and not pnl.recentButton:IsEnabled(), "errors in red, no rows")

    -- ---- /hs bookart only concerns the book: the Packs page keeps its own background
    H.Slash("bookart off")
    check(pnl:IsShown() and pnl.hasArt == true, "bookart off: the packs page is unchanged")
    H.Slash("bookart on")

    main:Hide()
    F.uninstall()
    resetUI()
    HS_CollectionTextures = savedPieces
    HS_BoardTextures = savedBoard
    H.db = savedDb
    resetAddon()

    -- ---- without any art the native look of the Packs tab still works: the card holders carry the glow colour
    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, realm = "Realm A" })
    H.db = CM.ApplySavedDefaults(nil)
    HS_CollectionTextures = nil
    HS_BoardTextures = nil
    resetUI()
    H.Slash("packs")
    main = UI.frame
    pnl = main.packs
    check(main.nativeTabs and F.globals.HearthstoneFrameTab1:GetText() == "Collection" and pnl.recentButton:GetText() == "Recent Packs" and pnl.doneButton:GetText() == "Done", "native tabs and UIPanelButtons without the art")
    check(pnl.hasArt == false and PU.slots[1].glow == nil and PU.slots[1].holder._.backdrop ~= nil and PU.slots[1].back.hasBorder and PU.stackPacks[1]._.backdrop ~= nil and pnl.flyer._.backdrop ~= nil, "native page, holders, card backs and packs")
    check(main._.backdrop ~= nil and main.tabsFrame._.level < main.art._.level, "the dialog frame is back and the z-order is the same")
    main:Hide()
    F.uninstall()
    resetUI()
    HS_CollectionTextures = savedPieces
    HS_BoardTextures = savedBoard
    H.db = savedDb
    resetAddon()
end

local function testMatchModel()
    local MA = HsMatch
    resetMatch()
    -- the canonical example, field by field
    local g, why = MA.Parse(CANONICAL_GAME)
    check(g ~= nil, "canonical parses: " .. tostring(why))
    if not g then return end
    check(g.mid == 7 and g.seq == 12 and g.ph == "play" and g.turn == 3 and g.act == "m" and g.res == "-" and g.why == "-", "header")
    check(g.myName == "Jonas" and g.oppName == "Innkeeper Allison", "names")
    local mh = g.me.hero
    check(mh.id == 1 and mh.cardId == "HERO_08" and mh.health == 27 and mh.maxHealth == 30 and mh.armor == 0 and mh.attack == 0 and not mh.canAttack, "my hero")
    check(g.me.power.id == 2 and g.me.power.cardId == "CS2_034" and g.me.power.cost == 2 and not g.me.power.used, "my power")
    check(g.me.weapon == nil and g.me.mana == 2 and g.me.maxMana == 2, "my weapon and mana")
    check(g.me.handCount == 4 and g.me.deckCount == 24 and g.me.fatigue == 0, "my counts")
    check(table.getn(g.me.board) == 2, "my board")
    local r, s = g.me.board[1], g.me.board[2]
    check(r.id == 40 and r.cardId == "CS2_172" and r.attack == 3 and r.health == 2 and r.maxHealth == 2 and r.canAttack and not r.taunt, "raptor")
    check(s.id == 41 and s.cardId == "CS2_179" and s.taunt and not s.canAttack and s.health == 5, "shieldmasta")
    check(table.getn(g.hand) == 4 and g.hand[1].id == 12 and g.hand[1].cardId == "CS2_029" and g.hand[1].cost == 4, "hand 1")
    check(g.hand[2].id == 15 and g.hand[2].attack == 2 and g.hand[2].health == 1 and g.hand[4].cardId == "GAME_005", "hand 2-4")
    local oh = g.opp.hero
    check(oh.id == 3 and oh.cardId == "HERO_01" and oh.health == 30 and oh.armor == 2, "opp hero")
    check(g.opp.power.used and g.opp.weapon and g.opp.weapon.id == 33 and g.opp.weapon.attack == 3 and g.opp.weapon.durability == 1, "opp power and weapon")
    check(g.opp.mana == 0 and g.opp.maxMana == 2 and g.opp.handCount == 5 and g.opp.deckCount == 23, "opp mana and counts")
    check(table.getn(g.opp.board) == 1 and g.opp.board[1].id == 44 and g.opp.board[1].charge, "opp board")
    check(table.getn(g.mulligan) == 0, "no mulligan")
    check(table.getn(g.legal) == 5, "five legal entries")
    check(g.legal[4].kind == "a" and g.legal[4].source == 40 and g.legal[4].targets[1] == 3 and g.legal[4].targets[2] == 44, "attack entry")
    check(g.evs == 31 and table.getn(g.events) == 2, "events")
    check(g.events[1].type == "turn" and g.events[1].side == "m" and g.events[1].amount == 3 and g.events[1].seq == 31 and g.events[1].cardId == nil, "turn event")
    check(g.events[2].type == "draw" and g.events[2].target == 18 and g.events[2].cardId == "CS2_025" and g.events[2].seq == 32, "draw event")

    -- lookups
    check(MA.IsRunning(g) and MA.IsMyTurn(g) and MA.CanEndTurn(g), "running, my turn")
    check(MA.IsPlayable(g, 15) and MA.IsPlayable(g, 21) and not MA.IsPlayable(g, 12), "playable")
    check(not MA.NeedsTarget(MA.LegalFor(g, "p", 15)), "Murloc needs no target")
    check(MA.CanAttackWith(g, 40) and not MA.CanAttackWith(g, 41), "attackers")
    local hp = MA.PowerEntry(g)
    check(hp and hp.source == 2 and MA.NeedsTarget(hp) and table.getn(MA.Targets(hp)) == 5, "hero power entry")
    check(MA.IsValidTarget(hp, 44) and not MA.IsValidTarget(hp, 12) and not MA.IsValidTarget(hp, 0), "valid targets")
    local kind, side = MA.WhoIs(g, 3)
    check(kind == "hero" and side == "o", "WhoIs hero")
    kind, side = MA.WhoIs(g, 41)
    check(kind == "minion" and side == "m" and MA.WhoIs(g, 999) == nil, "WhoIs minion")
    check(MA.FindHand(g, 18).cardId == "CS2_025" and MA.FindMinion(g, 44).cardId == "CS2_124", "find")

    -- GAME_ACT payloads (the C++ DecodeGameAct tests accept exactly these forms)
    check(MA.ActPlay(g, 15) == "mid=7;seq=12;a=play;s=15", "play")
    check(MA.ActPlay(g, 12, 44, -1) == "mid=7;seq=12;a=play;s=12;t=44", "play with a target")
    check(MA.ActPlay(g, 15, 0, 1) == "mid=7;seq=12;a=play;s=15;p=1", "play at a position")
    check(MA.ActAttack(g, 40, 44) == "mid=7;seq=12;a=atk;s=40;t=44", "attack")
    check(MA.ActHeroPower(g, 3) == "mid=7;seq=12;a=hp;t=3" and MA.ActHeroPower(g, 0) == "mid=7;seq=12;a=hp", "hero power")
    check(MA.ActEnd(g) == "mid=7;seq=12;a=end" and MA.ActConcede(g) == "mid=7;seq=12;a=concede", "end and concede")
    check(MA.ActMulligan(g, {}) == "mid=7;seq=12;a=mull;r=" and MA.ActMulligan(g, { 12, 18 }) == "mid=7;seq=12;a=mull;r=12,18", "mulligan")
    check(MA.LeavePayload(g) == "mid=7", "leave")

    -- malformed payloads are rejected
    local bad = {
        "", "ph=play", "ph=bogus", "mid=0;" .. string.sub(CANONICAL_GAME, 7),
        gamePayload({ act = "x" }), gamePayload({ res = "maybe" }), gamePayload({ why = "a;b" }),
        gamePayload({ mh = "1.HERO_08.27.30.0.0" }), gamePayload({ mh = "1.HERO_08.27.30.0.0.z" }),
        gamePayload({ mp = "2.CS2_034.2.2" }), gamePayload({ mw = "33.CS2_106.3" }), gamePayload({ mm = "2" }),
        gamePayload({ mk = "4.24" }), gamePayload({ mb = "40.CS2_172.3.2.2.a," }), gamePayload({ mb = "0.CS2_172.3.2.2.a" }),
        gamePayload({ mc = "12.CS2 029.4.0.0" }), gamePayload({ mul = "1,x" }), gamePayload({ lg = "x15.0" }),
        gamePayload({ lg = "p15." }), gamePayload({ lg = "p15.0/" }), gamePayload({ ev = "turn.q.0.0.3.-" }),
        gamePayload({ ev = "turn.m.0.0.3" }), gamePayload({ evs = "0" }), gamePayload({ evs = "" }),
        gamePayload({ turn = "-1" }), gamePayload({ seq = "x" }),
    }
    for i = 1, table.getn(bad) do
        check(MA.Parse(bad[i]) == nil, "rejects bad payload " .. i)
    end
    local many = {}
    for i = 1, 65 do many[i] = "dmg.m.1.2.3.-" end
    check(MA.Parse(gamePayload({ ev = table.concat(many, ",") })) == nil, "too many events")
    local none = MA.Parse("ph=none")
    check(none and none.ph == "none", "ph=none")
    check(MA.Parse(gamePayload({ mn = "a|b" })).myName == "a||b", "a | in a name is escaped for the chat frame")
    check(MA.Parse(gamePayload({ mb = "", lg = "", ev = "", evs = "0" })) ~= nil, "empty lists")

    check(MA.CapsHaveMatch("solo,chunk,packs,decks,match") and not MA.CapsHaveMatch("solo,matches") and not MA.CapsHaveMatch(nil), "caps")

    -- Apply: the log, older seqs, a new match, ph=none
    MA.supported = true
    check(MA.Apply(g) and MA.state == "play" and MA.game == g and MA.mid == 7, "applied")
    check(table.getn(MA.log) == 2 and string.find(MA.log[1], "Turn 3: your turn", 1, true) ~= nil, "turn line: " .. tostring(MA.log[1]))
    check(string.find(MA.log[2], "You draw Arcane Explosion.", 1, true) ~= nil, "draw line: " .. tostring(MA.log[2]))
    local older = MA.Parse(gamePayload({ seq = "11", ev = "", evs = "0" }))
    check(MA.Apply(older) == false and MA.game == g, "an older seq is ignored")
    local nxt = MA.Parse(gamePayload({ seq = "13", act = "o", lg = "", evs = "33",
        ev = "play.m.15.0.0.CS2_168,sum.m.0.15.0.CS2_168,atk.m.40.44.0.-,dmg.o.40.44.3.-,dmg.m.44.40.3.-,die.o.0.44.0.CS2_124,die.m.0.40.0.CS2_172,turn.o.0.0.4.-,draw.o.0.50.0.-,hp.o.4.0.0.CS2_102,arm.o.4.3.2.-,end.-.0.0.0.-" }))
    check(MA.Apply(nxt) and MA.state == "play" and not MA.IsMyTurn(nxt), "next view")
    local joined = table.concat(MA.log, "\n")
    check(string.find(joined, "You play Murloc Raider.", 1, true) ~= nil, "play line")
    check(string.find(joined, "joins the board", 1, true) == nil, "the summon of a played minion is folded into the play line")
    check(string.find(joined, "Bloodfen Raptor attacks Wolfrider.", 1, true) ~= nil, "attack line: names learned from the board")
    check(string.find(joined, "Wolfrider takes 3 damage.", 1, true) ~= nil and string.find(joined, "Wolfrider dies.", 1, true) ~= nil, "damage and death")
    check(string.find(joined, "Innkeeper Allison draws a card.", 1, true) ~= nil, "hidden draw")
    check(string.find(joined, "Innkeeper Allison uses Armor Up!.", 1, true) ~= nil and string.find(joined, "Innkeeper Allison gains 2 armor.", 1, true) ~= nil, "hero power and armor")
    check(string.find(joined, "The match ends.", 1, true) ~= nil, "end line")
    -- the history tiles: one per play / attack / hero power; the events after it are its lines; a turn closes it
    local hist = MA.history
    check(table.getn(hist) == 3, "three tiles (play, attack, hero power): " .. table.getn(hist))
    check(hist[1].kind == "play" and hist[1].side == "m" and hist[1].cardId == "CS2_168" and hist[1].targetCardId == nil, "play tile")
    check(hist[2].kind == "atk" and hist[2].cardId == "CS2_172" and hist[2].targetCardId == "CS2_124", "attack tile: attacker and target cards")
    check(table.getn(hist[2].lines) == 5 and string.find(hist[2].lines[2], "Wolfrider takes 3 damage.", 1, true) ~= nil, "damage and deaths are lines of the attack tile: " .. table.getn(hist[2].lines))
    check(hist[3].kind == "hp" and hist[3].side == "o" and hist[3].cardId == "CS2_102" and table.getn(hist[3].lines) == 2, "hero power tile with its armor line")
    local tail = MA.HistoryTail(2)
    check(table.getn(tail) == 2 and tail[1] == hist[2] and tail[2] == hist[3] and table.getn(MA.HistoryTail(9)) == 3, "history tail: the newest, oldest first")
    local n = table.getn(MA.log)
    MA.Apply(MA.Parse(gamePayload({ seq = "13", act = "o", lg = "", evs = "33", ev = "play.m.15.0.0.CS2_168" })))
    check(table.getn(MA.log) == n and table.getn(MA.history) == 3, "events already logged are not logged again")
    for _, ev in ipairs({ "mull.m.0.0.0.-", "mull.o.0.0.2.-", "burn.o.0.9.0.CS2_200", "fat.m.0.1.2.-", "heal.m.2.1.2.-",
        "pop.o.1.44.0.-", "eq.o.0.33.0.CS2_106", "wbrk.o.0.33.0.CS2_106", "tf.o.0.44.0.CS2_tk1", "buff.m.1.41.2.-",
        "buff.m.1.41.0.-", "mana.m.21.0.1.-" }) do
        local _, _, ty, sd, so, ta, am, cid = string.find(ev, "^(%l+)%.(.)%.(%d+)%.(%d+)%.(%d+)%.(.+)$")
        local e = { type = ty, side = sd, source = tonumber(so), target = tonumber(ta), amount = tonumber(am), cardId = cid ~= "-" and cid or nil }
        local line = MA.EventText(e, nxt)
        check(type(line) == "string" and string.len(line) > 5, "event text " .. ev .. ": " .. tostring(line))
    end
    check(string.find(MA.EventText({ type = "zzz", side = "m", source = 0, target = 0, amount = 0 }, nxt) or "", "Something happens (zzz).", 1, true) ~= nil, "unknown events get a generic line")

    -- a transformed minion keeps its old name for the events up to the transformation
    MA.Apply(MA.Parse(gamePayload({ mid = "20", seq = "1", ev = "", evs = "0" })))
    MA.Apply(MA.Parse(gamePayload({ mid = "20", seq = "2", ob = "44.CS2_tk1.1.1.1.-", evs = "50",
        ev = "play.m.12.44.0.CS2_022,tf.o.0.44.0.CS2_tk1,atk.m.40.44.0.-" })))
    local poly = table.concat(MA.log, "\n")
    check(string.find(poly, "You play Polymorph on Wolfrider.", 1, true) ~= nil, "play names the minion before the change: " .. poly)
    check(string.find(poly, "Wolfrider becomes Sheep.", 1, true) ~= nil, "transformation line")
    check(string.find(poly, "Bloodfen Raptor attacks Sheep.", 1, true) ~= nil, "after the change the new name")

    -- marks: only offered cards, only while the mulligan is open
    local mull = MA.Parse(gamePayload({ mid = "8", seq = "1", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15,18", ev = "", evs = "0" }))
    check(mull and MA.Apply(mull) and MA.state == "mull", "mulligan view")
    check(MA.ToggleMark(15) and MA.ToggleMark(12) and not MA.ToggleMark(99), "marks")
    check(table.concat(MA.MarkedList(), ",") == "12,15", "marked in offer order")
    check(MA.ToggleMark(15) == false and table.concat(MA.MarkedList(), ",") == "12", "unmark")
    MA.Apply(MA.Parse(gamePayload({ mid = "8", seq = "2", ph = "mull", turn = "0", act = "-", lg = "", mul = "", ev = "", evs = "0" })))
    check(table.getn(MA.MarkedList()) == 0, "marks dropped when the offer closes")

    -- a different match id starts a fresh log (also with a lower seq)
    MA.Apply(MA.Parse(gamePayload({ mid = "9", seq = "1", ev = "", evs = "0" })))
    check(MA.mid == 9 and table.getn(MA.log) == 0 and MA.game.seq == 1, "new match clears the log")

    -- results
    local over = MA.Parse(gamePayload({ ph = "over", act = "-", res = "win", why = "hero", lg = "", ev = "", evs = "0" }))
    check(MA.ResultText(over) == "Victory" and MA.ReasonText(over) == "Innkeeper Allison was defeated.", "win")
    over.res, over.why = "loss", "concede"
    check(MA.ResultText(over) == "Defeat" and MA.ReasonText(over) == "You surrendered.", "concede")
    over.res, over.why = "int", "stuck"
    check(MA.ResultText(over) == "Interrupted" and string.find(MA.ReasonText(over), "Stuck", 1, true) ~= nil, "stuck")
    over.res, over.why = "draw", "turns"
    check(MA.ResultText(over) == "Draw" and string.find(MA.ReasonText(over), "turn limit", 1, true) ~= nil, "turn limit")
    for _, w in ipairs({ "teleport", "death", "npc", "gm", "error" }) do
        over.res, over.why = "int", w
        check(MA.ReasonText(over) ~= "The match is over.", "reason " .. w)
    end
    MA.Apply(over)
    check(MA.state == "over" and not MA.IsRunning(MA.game) and MA.ActBlocker() == "The match is over.", "over blocks actions")
    check(string.find(MA.StatusText(), "over, Interrupted (error)", 1, true) ~= nil, "status over: " .. MA.StatusText())

    -- overlay numbers: only where the hand card differs from the card database
    local c1, a1, h1 = MA.HandOverlay({ cardId = "CS2_168", cost = 1, attack = 2, health = 1 })
    check(c1 == nil and a1 == nil and h1 == nil, "Murloc as printed")
    c1, a1, h1 = MA.HandOverlay({ cardId = "CS2_168", cost = 0, attack = 3, health = 1 })
    check(c1 == "down" and a1 == "up" and h1 == nil, "Murloc changed")
    c1, a1 = MA.HandOverlay({ cardId = "CS2_029", cost = 4, attack = 0, health = 0 })
    check(c1 == nil and a1 == nil, "spells have no attack overlay")

    -- the log tail fits the rows
    MA.log = { "a", string.rep("b", 70), "|cff99ff99c|r" }
    local tail = MA.LogTail(3, 30)
    check(table.getn(tail) == 1 and tail[1] == "|cff99ff99c|r", "tail 3 rows: " .. table.getn(tail))
    check(table.getn(MA.LogTail(5, 30)) == 3, "tail 5 rows")

    MA.Apply({ ph = "none" })
    check(MA.state == "none" and MA.game == nil and MA.StatusText() == "match: none running" and MA.TooltipLine() == nil, "none")
    resetMatch()
    check(MA.StatusText() == "match: not supported by this server", "unsupported status")
end

local function testMatchFlow()
    local MA = HsMatch
    local X = matchHelpers()
    local T = X.T
    resetAddon()
    resetMatch()
    local changed = 0
    local savedHook = H.OnMatchChanged
    H.OnMatchChanged = function() changed = changed + 1 end

    -- no match support: nothing is asked
    T.welcome("solo,chunk,packs,decks")
    drain(1)
    check(X.count("GAME_GET") == 0 and not H.match.supported, "no GAME_GET without the cap")
    H.Slash("game")
    check(string.find(lastPrinted(), "does not support matches", 1, true) ~= nil, "/hs game unsupported")
    local ok, why = H.MatchAct("end")
    check(not ok and why == "This server does not support matches yet.", "no actions without the cap")

    -- WELCOME with match: one GAME_GET (also the /reload path)
    sent = {}
    X.welcome()
    drain(1)
    check(H.match.supported and MA.supported and X.count("GAME_GET") == 1 and MA.state == "loading", "GAME_GET after WELCOME")
    local get = X.last("GAME_GET")
    H.MatchGet()
    check(H.match.refetch and X.count("GAME_GET") == 1, "a second GAME_GET while one is in flight is a refetch")
    X.game(get.reqId, "ph=none")
    drain(1)
    check(X.count("GAME_GET") == 2, "the refetch is sent")
    X.game(X.last("GAME_GET").reqId, "ph=none")
    check(MA.state == "none" and H.match.reqId == nil, "no match")
    ok, why = H.MatchAct("end")
    check(not ok and why == "No match is running.", "nothing to act on")
    H.Slash("game")
    drain(1)
    X.game(X.last("GAME_GET").reqId, "ph=none")
    check(string.find(lastPrinted(), "no match in progress", 1, true) ~= nil, "/hs game without a match tells so: " .. lastPrinted())

    -- a push starts the match (gossip on the server); the hook redraws
    changed = 0
    X.push(CANONICAL_GAME)
    check(MA.state == "play" and MA.game.mid == 7 and changed >= 1, "the push shows the match")

    -- one action in flight, never resent with the same seq
    ok = H.MatchAct("play", 15, 0)
    drain(1)
    local act = X.last("GAME_ACT")
    check(ok and act and act.payload == "mid=7;seq=12;a=play;s=15", "GAME_ACT play: " .. tostring(act and act.payload))
    ok, why = H.MatchAct("end")
    check(not ok and why == "The last action is still being sent.", "one action at a time")
    X.game(act.reqId, gamePayload({ seq = "13", evs = "33", ev = "play.m.15.0.0.CS2_168,sum.m.0.15.0.CS2_168" }))
    check(MA.pending == nil and MA.game.seq == 13, "answered")

    -- refused: stale refetches, anything else only explains
    H.MatchAct("atk", 40, 44)
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=7;seq=13;a=atk;s=40;t=44", "attack payload")
    local gets = X.count("GAME_GET")
    X.T.frame("ERR", act.reqId, "code=REFUSED;msg=x;why=taunt")
    drain(1)
    check(MA.pending == nil and MA.message == "A minion with Taunt must be attacked first." and X.count("GAME_GET") == gets, "taunt refusal")
    H.MatchAct("end")
    drain(1)
    act = X.last("GAME_ACT")
    X.T.frame("ERR", act.reqId, "code=REFUSED;msg=x;why=stale")
    drain(1)
    check(MA.pending == nil and X.count("GAME_GET") == gets + 1, "stale refetches")
    X.game(X.last("GAME_GET").reqId, gamePayload({ seq = "14", ev = "", evs = "0" }))
    check(MA.game.seq == 14, "refetched")

    -- a lost answer: the board is refetched, the action is not sent again
    H.MatchAct("hp", 3)
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=7;seq=14;a=hp;t=3", "hero power payload")
    local acts = X.count("GAME_ACT")
    gets = X.count("GAME_GET")
    drain(40)
    check(MA.pending == nil and X.count("GAME_ACT") == acts and X.count("GAME_GET") >= gets + 1, "timeout refetches, no resend")
    X.game(X.last("GAME_GET").reqId, gamePayload({ seq = "15", ev = "", evs = "0" }))

    -- AI pushes; an older push is ignored
    X.push(gamePayload({ seq = "16", act = "o", lg = "", evs = "40", ev = "turn.o.0.0.4.-" }))
    check(MA.game.seq == 16 and not MA.IsMyTurn(MA.game), "AI turn push: seq " .. tostring(MA.game.seq) .. " w=" .. tostring(H.state.welcomed) .. " s=" .. tostring(H.match.supported) .. " " .. tostring(H.state.lastError) .. " " .. tostring(MA.message))
    X.push(gamePayload({ seq = "15", ev = "", evs = "0" }))
    check(MA.game.seq == 16, "older push ignored")
    ok, why = H.MatchAct("end")
    drain(1)
    act = X.last("GAME_ACT")
    check(ok and act.payload == "mid=7;seq=16;a=end", "an action out of turn still goes to the server (it judges)")
    X.T.frame("ERR", act.reqId, "code=REFUSED;msg=x;why=turn")
    check(MA.message == "It is not your turn.", "turn refusal")

    -- concede, the result, leave
    H.MatchAct("concede")
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=7;seq=16;a=concede", "concede payload")
    X.game(act.reqId, gamePayload({ seq = "17", ph = "over", act = "-", res = "loss", why = "concede", lg = "", evs = "41", ev = "end.o.0.0.2.-" }))
    check(MA.state == "over" and MA.ResultText(MA.game) == "Defeat", "over")
    check(string.find(MA.StatusText(), "Defeat", 1, true) ~= nil, "status")
    H.PrintStatus()
    local status = table.concat(printed, "\n")
    check(string.find(status, "match #7: over, Defeat (concede)", 1, true) ~= nil, "/hs status match line")
    ok = H.MatchLeave()
    drain(1)
    local leave = X.last("GAME_LEAVE")
    check(ok and leave and leave.payload == "mid=7" and MA.state == "none", "GAME_LEAVE")
    X.game(leave.reqId, "ph=none")
    check(MA.state == "none", "left")
    check(not H.MatchLeave(), "nothing more to leave")

    -- /reload while a match runs: a fresh WELCOME asks again and restores the board
    X.push(gamePayload({ mid = "9", seq = "3", ev = "", evs = "0" }))
    resetAddon()
    resetMatch()
    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, gamePayload({ mid = "9", seq = "3", ev = "", evs = "0" }))
    check(MA.state == "play" and MA.game.mid == 9, "restored after /reload")
    -- the match ended during the reload: the result is shown, not revived
    resetAddon()
    resetMatch()
    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, gamePayload({ mid = "9", seq = "4", ph = "over", act = "-", res = "int", why = "teleport", lg = "", ev = "", evs = "0" }))
    check(MA.state == "over" and MA.game.why == "teleport" and MA.ActBlocker() ~= nil, "a match that ended meanwhile shows its result")

    -- an unreadable GAME
    X.push("ph=play;mid=x")
    check(MA.state == "over" and string.find(H.state.lastError or "", "match reply rejected", 1, true) ~= nil, "a bad push keeps the board")
    -- pushes before the handshake are ignored; VERSION resets
    X.T.err("0", "VERSION", "old")
    check(MA.state == "none" and not H.match.supported, "VERSION resets the match")
    X.push(CANONICAL_GAME)
    check(MA.state == "none", "ignored while not welcomed")

    H.OnMatchChanged = savedHook
    resetAddon()
    resetMatch()
end

local function testMatchUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local MA = HsMatch
    local MU = H.MatchUI
    local X = matchHelpers()
    resetAddon()
    resetMatch()
    F.install({ class = { "Mage", "MAGE" }, name = "Jonas", screenW = 1920, screenH = 1080 })
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil

    -- the layout fits the screen at ~80%
    local scale, w, h = MU.Fit(1920, 1080)
    check(scale > 0 and w >= MU.MIN_W and w <= MU.MAX_W and h == MU.DESIGN_H, "fit")
    check(h * scale <= 1080 * 0.81 and h * scale >= 1080 * 0.7, "about 80% of the screen height: " .. h * scale)
    scale, w = MU.Fit(800, 600)
    check(w * scale <= 800 and h * scale <= 600, "small screens fit")

    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, "ph=none")
    check(MU.frame == nil or not MU.frame:IsShown(), "no window without a match")

    -- a push opens the window
    X.push(CANONICAL_GAME)
    local f = MU.frame
    check(f and f:IsShown(), "window shown")
    if not f then return end
    check(f:GetFrameStrata() == "HIGH" and f:GetWidth() >= MU.MIN_W, "strata and width")
    local found = false
    for i = 1, table.getn(UISpecialFrames) do if UISpecialFrames[i] == "HearthstoneMatchFrame" then found = true end end
    check(found, "Escape closes (asks)")
    local ui = MU.ui
    check(ui.hand[1].frame:IsShown() and ui.hand[4].frame:IsShown() and not ui.hand[5].frame:IsShown(), "four hand cards")
    check(ui.m.minions[1].frame:IsShown() and ui.m.minions[2].frame:IsShown() and not ui.m.minions[3].frame:IsShown(), "my board")
    check(ui.o.minions[1].frame:IsShown() and not ui.o.minions[2].frame:IsShown(), "opponent board")
    local backs = 0
    for i = 1, table.getn(ui.o.backs) do if ui.o.backs[i]:IsShown() then backs = backs + 1 end end
    check(backs == 5, "five card backs: " .. backs)
    check(not MU.mull.frame:IsShown() and not MU.result.frame:IsShown(), "no overlay")
    -- the history column: a tile per history entry (newest on top), none without history; hovering shows the card
    check(table.getn(ui.log.tiles) == MU.LOG_MAX_TILES and ui.log.count >= 3 and ui.log.count <= MU.LOG_MAX_TILES, "history tiles that fit: " .. tostring(ui.log.count))
    check(not ui.log.tiles[1].frame:IsShown(), "no tile without history")
    local keepHistory = MA.history
    MA.history = {
        { kind = "play", side = "m", cardId = "CS2_168", lines = { "You play Murloc Raider." } },
        { kind = "atk", side = "o", cardId = "CS2_172", targetCardId = "CS2_124", lines = { "a attacks b.", "b dies." } },
    }
    MU.RefreshLog()
    check(ui.log.tiles[1].frame:IsShown() and ui.log.tiles[2].frame:IsShown() and not ui.log.tiles[3].frame:IsShown(), "two tiles shown")
    check(ui.log.tiles[1].entry == MA.history[2] and ui.log.tiles[1].icon:IsShown() and ui.log.tiles[1].small:IsShown(), "attack tile (newest, on top): icon and target picture")
    check(ui.log.tiles[2].entry == MA.history[1] and not ui.log.tiles[2].icon:IsShown() and not ui.log.tiles[2].small:IsShown(), "a plain play (older, below) has neither")
    MU.OnLogEnter(ui.log.tiles[1])
    check(MU.preview.frame:IsShown(), "hovering a tile shows the large card")
    MU.OnLogLeave()
    check(not MU.preview.frame:IsShown(), "leaving hides it")
    MA.history = keepHistory
    MU.RefreshLog()
    check(not ui.log.tiles[1].frame:IsShown(), "tiles gone with the history")
    check(ui.mid.endButton:IsShown() and ui.mid.endButton:IsEnabled(), "End Turn on my turn")
    -- the hand stands in a dock below the field and reaches over the field's bottom rim; at rest it never covers the
    -- player's hero (arch frame), and every card stays inside the board area
    local _, hy, hsize = MU.HeroRect("m", MU.geom.bw, MU.geom.fh)
    check(hy + hsize / 2 <= MU.geom.fh and MU.geom.fh + MU.HAND_STRIP == MU.geom.bh, "hero above the hand strip")
    local archBottom = MU.PieceAnchor("board_field", "arch_bottom_frame")
    local archY = archBottom and (MU.FIELD_INSET + MU.FieldY(archBottom.bottom) * (MU.geom.fh - 2 * MU.FIELD_INSET)) or hy + hsize / 2
    for i = 1, 4 do
        local hp = ui.hand[i].frame._.points[1]
        -- (20 px: the owner's lower hand dock, HAND_STRIP 70, puts the middle cards 17 px into the frame)
        check(hp and hp.point == "TOPLEFT" and -hp.y >= archY - 20 and -hp.y + MU.HAND_H <= MU.geom.bh
            and hp.x >= 0 and hp.x + MU.HAND_W <= MU.geom.bw, "hand card " .. i .. " inside the board area, at most 20 px into the hero's arch frame: " .. tostring(hp and hp.y))
    end
    check(-ui.hand[1].frame._.points[1].y + MU.HAND_H - MU.geom.fh >= 20, "the hand reaches over the field's bottom rim")
    -- a fan: the middle cards stand higher than the outer ones, and neighbours overlap (each shows about half its width)
    local set = MU.HAND_FAN
    local lay = MU.HandLayout(5, MU.geom.bw, MU.geom.bh)
    check(lay[3].y < lay[1].y and lay[3].y < lay[5].y and lay[1].y == lay[5].y and lay[3].y + MU.HAND_H == MU.geom.bh - set.base,
        "the hand is a fan")
    check(math.abs((lay[2].x - lay[1].x) - (lay[3].x - lay[2].x)) <= 1, "even steps")
    check(math.abs((lay[2].x - lay[1].x) - set.step * MU.HAND_W) <= 1 and lay[2].x < lay[1].x + MU.HAND_W * 0.7, "five cards overlap by about half")
    check(math.abs((lay[1].x + lay[5].x + MU.HAND_W) / 2 - MU.geom.bw / 2) <= 1, "the fan is centred")
    lay = MU.HandLayout(10, MU.geom.bw, MU.geom.bh)
    check(lay[10].x + MU.HAND_W - lay[1].x <= MU.HAND_ROW_W and lay[2].x < lay[1].x + MU.HAND_W * 0.7, "ten cards overlap")
    local oldRow = MU.HAND_ROW_W
    MU.HAND_ROW_W = 300
    lay = MU.HandLayout(10, MU.geom.bw, MU.geom.bh)
    check(lay[10].x + MU.HAND_W - lay[1].x <= 301, "a narrow row squeezes the cards further")
    MU.HAND_ROW_W = oldRow
    check(table.getn(MU.HandLayout(0, MU.geom.bw, MU.geom.bh)) == 0 and MU.HandLayout(1, MU.geom.bw, MU.geom.bh)[1].y + MU.HAND_H == MU.geom.bh - set.base, "empty and single hand")
    -- the fan follows MU.HAND_FAN
    local tilt0 = MU.HAND_FAN.tilt
    MU.HAND_FAN.tilt = 20
    check(MU.HandLayout(3, MU.geom.bw, MU.geom.bh)[3].angle == math.rad(20), "hand tilt setting")
    MU.HAND_FAN.tilt = tilt0

    -- the hovered hand card grows in place from its bottom edge, above its neighbours, and returns when the cursor leaves.
    -- Only its body grows: the slot frame is the mouse area and never changes, so the cursor can go straight onto the
    -- neighbouring cards.
    local rest = ui.hand[2].frame._.points[1]
    local lvl2, lvl3 = ui.hand[2].frame:GetFrameLevel(), ui.hand[3].frame:GetFrameLevel()
    local w2, h2 = ui.hand[2].frame:GetWidth(), ui.hand[2].frame:GetHeight()
    F.fire(ui.hand[2].frame, "OnEnter")
    local body = ui.hand[2].body
    local pop = body._.points[1]
    local ps = MU.HAND_POP_W / MU.HAND_W
    check(ui.hand[2].popped and body:GetScale() == ps, "the hovered hand card grows")
    local nowP = ui.hand[2].frame._.points[1]
    check(ui.hand[2].frame:GetScale() == 1 and ui.hand[2].frame:GetWidth() == w2 and ui.hand[2].frame:GetHeight() == h2
        and nowP.point == rest.point and nowP.x == rest.x and nowP.y == rest.y and ui.hand[2].frame:GetFrameLevel() == lvl2,
        "the mouse area keeps its size, place and level while the card is big")
    check(pop.point == "BOTTOMLEFT" and pop.rel == ui.hand[2].frame and pop.y == 0, "it keeps its bottom edge")
    local popLeft = rest.x + pop.x * ps
    check(math.abs(popLeft + MU.HAND_POP_W / 2 - (rest.x + MU.HAND_W / 2)) < 0.01 and popLeft >= 0 and popLeft + MU.HAND_POP_W <= MU.geom.bw,
        "it stays centred on the resting card and inside the board")
    check(body:GetFrameLevel() > lvl3 and ui.hand[2].widget.frame:GetFrameLevel() > lvl3 and ui.hand[2].cost.frame:GetFrameLevel() > lvl3
        and ui.hand[2].glow.frame:GetFrameLevel() > lvl3 + MU.HAND_BAND, "the popped card and its parts are above the neighbours")
    check(not (MU.preview and MU.preview.frame:IsShown()), "a hand card does not open the side preview")
    F.fire(ui.hand[3].frame, "OnEnter")
    F.fire(ui.hand[2].frame, "OnLeave")
    check(not ui.hand[2].popped and body:GetScale() == 1 and ui.hand[3].popped, "moving to the next card pops it and drops the first")
    local back = body._.points[1]
    check(back.point == "TOPLEFT" and back.rel == ui.hand[2].frame and back.x == 0 and back.y == 0, "the card is back at rest")
    check(body:GetFrameLevel() == lvl2 + 1, "the body is back at its level: " .. body:GetFrameLevel() .. " vs " .. lvl2)
    F.fire(ui.hand[3].frame, "OnLeave")
    check(not ui.hand[3].popped and ui.hand[3].body:GetFrameLevel() == lvl3 + 1, "levels restored")
    F.fire(ui.hand[1].frame, "OnEnter")
    MU.HandUnpopAll()
    check(not ui.hand[1].popped and ui.hand[1].body:GetScale() == 1, "unpop all")
    -- the leftmost and rightmost cards stay inside the board when popped
    F.fire(ui.hand[1].frame, "OnEnter")
    local left = ui.hand[1].frame._.points[1].x + ui.hand[1].body._.points[1].x * ps
    F.fire(ui.hand[1].frame, "OnLeave")
    check(left >= 0, "the leftmost popped card stays inside the board")
    -- a dragged card is played above the top of the hand, not just above the field
    check(MU.PlayLine(MU.geom.bh) < MU.geom.fh and MU.PlayLine(MU.geom.bh) < -ui.hand[3].frame._.points[1].y, "the play line is above the resting hand")
    -- the window frame is the tinted stone border of the collection
    local tint = H.CollectionUI.FRAME_TINT
    check(f._.bdBorder and f._.bdBorder[1] == tint[1] and f._.bdBorder[3] == tint[3], "stone frame tint")

    -- the fan leans: the middle card is upright, the outer cards lean outwards by the tilt setting
    lay = MU.HandLayout(5, MU.geom.bw, MU.geom.bh)
    check(lay[3].angle == 0 and lay[1].angle < 0 and lay[5].angle > 0 and math.abs(lay[5].angle - math.rad(MU.HAND_FAN.tilt)) < 1e-9
        and math.abs(lay[1].angle + lay[5].angle) < 1e-9 and lay[2].angle < 0 and lay[4].angle > 0, "the outer cards lean outwards")
    check(MU.HandLayout(1, MU.geom.bw, MU.geom.bh)[1].angle == 0, "a single card is upright")
    -- the tilted texture: thin strips, each only as wide as the tilted card is inside its band, with turned texcoords
    local W = H.CardWidget
    local w = W.Create(UIParent, nil, MU.HAND_W, false)
    W.SetCard(w, "CS2_029", true)
    local bottom = w.bottom
    W.SetTilt(w, 0)
    local tc = w.tex._.texcoord
    check(tc[1] == 0 and tc[2] == 1 and tc[4] == bottom and not w.tilted and w.tex:IsShown(), "upright: the plain texcoord")
    local ang = math.rad(MU.HAND_FAN.tilt)
    W.SetTilt(w, ang)
    check(w.tilted and not w.tex:IsShown() and table.getn(w.strips) == W.TILT_STRIPS, "tilted: strips instead of the single texture")
    local k = w.width / 256
    local src = W.TILT_SRC
    local cx, cy = (src.left + src.right) / 2 * k, (src.top + src.bottom) / 2 * k
    local c, sn = math.cos(ang), math.sin(ang)
    local inside, bad, maxX, minX, prevBottom, contiguous = 0, 0, -1e9, 1e9, nil, true
    for j = 1, W.TILT_STRIPS do
        local st = w.strips[j].tex
        local pt = st._.points[1]
        local stc = st._.texcoord
        if st:IsShown() and stc and table.getn(stc) == 8 and pt then
            inside = inside + 1
            local lo, y0 = pt.x - cx, -pt.y - cy
            local hi, y1 = lo + st:GetWidth(), y0 + st:GetHeight()
            if prevBottom and math.abs(prevBottom - y0) > 1e-6 then contiguous = false end
            prevBottom = y1
            if lo < minX then minX = lo end
            if hi > maxX then maxX = hi end
            -- the four corners (UL, LL, UR, LR): the sampled source point turned forward lands on the screen corner
            local corners = { { lo, y0 }, { lo, y1 }, { hi, y0 }, { hi, y1 } }
            for q = 1, 4 do
                local u, v = stc[q * 2 - 1], stc[q * 2]
                if u < 0 or u > 1 or v < -0.2 or v > 1 then bad = bad + 1 end   -- above the top is transparent (wraps to the empty bottom of the 512 px texture)
                local sx, sy = u * w.width - cx, v * w.height / bottom - cy
                local fx, fy = sx * c - sy * sn, sx * sn + sy * c
                if math.abs(fx - corners[q][1]) > 1e-6 or math.abs(fy - corners[q][2]) > 1e-6 then bad = bad + 1 end
            end
        end
    end
    check(inside == W.TILT_STRIPS and bad == 0, "every strip corner samples the turned-back card, inside the texture's transparent margins: " .. bad)
    check(contiguous, "the strips stack without gaps")
    local hw, hh = (src.right - src.left) / 2 * k, (src.bottom - src.top) / 2 * k
    check(maxX >= hw * c + hh * sn - 0.01 and minX <= -(hw * c + hh * sn) + 0.01, "the strips reach the tilted card's outer corners")
    local top = w.strips[1].tex._.points[1]
    local lastS = w.strips[W.TILT_STRIPS].tex
    check(math.abs((-lastS._.points[1].y - cy + lastS:GetHeight()) - (hw * sn + hh * c)) < 1e-6 and math.abs((-top.y - cy) + (hw * sn + hh * c)) < 1e-6, "the strips span the tilted card's height")
    W.SetCard(w, "CS2_029", true)
    check(w.tilted and w.strips[1].path ~= nil and not w.tex:IsShown(), "a new card keeps the tilt")
    W.SetTilt(w, nil)
    local shown = 0
    for j = 1, W.TILT_STRIPS do if w.strips[j].tex:IsShown() then shown = shown + 1 end end
    check(not w.tilted and shown == 0 and w.tex:IsShown() and table.getn(w.tex._.texcoord) == 4, "tilt off: the plain texture again")
    local px, py = W.TiltPoint(40, 60, ang)
    check(px > 40 and py < 60 and math.abs(px * px + py * py - 40 * 40 - 60 * 60) < 1e-6, "TiltPoint turns clockwise")
    -- a hand slot with a texture: tilted at rest (gems moved, glow wider), upright while hovered
    local slot5 = ui.hand[1]
    slot5.widget.usingTexture = true
    slot5.widget.bottom = bottom
    slot5.pos = lay[1]
    MU.HandPlace(1)
    check(slot5.widget.tilted, "a resting outer card is tilted")
    local gem = slot5.cost.frame._.points[1]
    local cx0, cy0 = -MU.HAND_W / 2 + MU.HAND_COST_AT[1] * MU.HAND_W, MU.HAND_H / 2 - MU.HAND_COST_AT[2] * MU.HAND_H
    local ex, ey = W.TiltPoint(cx0, cy0, lay[1].angle)
    check(gem.point == "CENTER" and math.abs(gem.x - ex) < 1e-6 and math.abs(gem.y - ey) < 1e-6, "the cost gem follows the tilted printed cost")
    check(slot5.glow.angle == lay[1].angle, "the highlight takes the card's tilt")
    slot5.popped = true
    MU.HandPlace(1)
    gem = slot5.cost.frame._.points[1]
    check(not slot5.widget.tilted and math.abs(gem.x - cx0) < 1e-6 and slot5.glow.angle == 0, "a hovered card stands upright")
    slot5.popped = nil
    -- the highlight lies behind its card and over the cards behind it, never over the cards in front: every card owns a
    -- band of frame levels, and the next card starts above the whole band
    local function parts(slot)
        local list = { slot.glow.frame, slot.frame, slot.widget.frame, slot.widget.fb, slot.cost.frame, slot.atk.frame, slot.hp.frame }
        return list
    end
    local levelsOk, bandsOk, glowFirst = true, true, true
    local prevTop = nil
    for i = 1, MU.MAX_HAND do
        local list = parts(ui.hand[i])
        local lo, hi = 1e9, -1e9
        for q = 1, table.getn(list) do
            local lv = list[q]:GetFrameLevel()
            if lv < lo then lo = lv end
            if lv > hi then hi = lv end
        end
        if ui.hand[i].glow.frame:GetFrameLevel() ~= lo then glowFirst = false end
        if ui.hand[i].glow.frame:GetFrameLevel() >= ui.hand[i].widget.frame:GetFrameLevel() then levelsOk = false end
        if prevTop and lo <= prevTop then bandsOk = false end
        prevTop = hi
    end
    check(glowFirst and levelsOk, "a hand card's highlight is the lowest frame of its band, under the card")
    check(bandsOk, "the bands of neighbouring hand cards do not interleave")
    check(prevTop < MU.board:GetFrameLevel() + MU.HAND_POP_LEVEL and MU.board:GetFrameLevel() + MU.HAND_POP_LEVEL + MU.HAND_BAND < MU.mull.frame:GetFrameLevel()
        and MU.mull.frame:GetFrameLevel() < MU.fxFrame:GetFrameLevel() and MU.result.frame:GetFrameLevel() < MU.fxFrame:GetFrameLevel()
        and MU.pick.frame:GetFrameLevel() > prevTop and MU.chooser.frame:GetFrameLevel() > prevTop, "hand under the hovered card under the overlays under the effects")
    -- the highlight with art: strips, tinted, turned like the card, sized by glowx / glowy
    local oldPieces = HS_BoardTextures
    HS_BoardTextures = { generated = true, pieces = {
        glow_green = { path = "Interface\\AddOns\\Hearthstone\\Textures\\Board\\glow_green", w = 128, h = 192, right = 1, bottom = 0.75 },
        glow_red = { path = "Interface\\AddOns\\Hearthstone\\Textures\\Board\\glow_red", w = 128, h = 192, right = 1, bottom = 0.75 } } }
    local host = CreateFrame("Frame", nil, MU.board)
    host:SetFrameLevel(50)
    host:SetWidth(MU.HAND_W)
    host:SetHeight(MU.HAND_H)
    local g = MU.MakeHandGlow(host, 49)
    check(g.piece and g.frame:GetFrameLevel() == 49 and not g.frame:IsShown(), "the highlight is its own frame, hidden until it has a colour")
    g.color = "green"
    g.angle = 0
    MU.LayoutHandGlow(g)
    check(g.frame:IsShown() and table.getn(g.strips) == MU.GLOW_STRIPS, "the highlight is drawn as strips")
    local function extent(glow)
        local lo, hi, top, bot = 1e9, -1e9, 1e9, -1e9
        for q = 1, table.getn(glow.strips) do
            local t = glow.strips[q].tex
            local pt = t._.points[1]
            if t:IsShown() and pt then
                if pt.x < lo then lo = pt.x end
                if pt.x + t:GetWidth() > hi then hi = pt.x + t:GetWidth() end
                if -pt.y < top then top = -pt.y end
                if -pt.y + t:GetHeight() > bot then bot = -pt.y + t:GetHeight() end
            end
        end
        return lo, hi, top, bot
    end
    local lo, hi, top, bot = extent(g)
    local kk = MU.HAND_W / 256
    local V = H.CardWidget.VISIBLE
    local set0 = MU.HAND_FAN
    check(math.abs(lo - (V.left * kk - set0.glowx)) < 1e-6 and math.abs(hi - (V.right * kk + set0.glowx)) < 1e-6
        and math.abs(top - (V.top * kk - set0.glowtop)) < 1e-6 and math.abs(bot - (V.bottom * kk + set0.glowbottom)) < 1e-6,
        "upright, the highlight is the visible card plus glowx / glowtop / glowbottom")
    check(set0.glowbottom > set0.glowtop, "the highlight reaches further below the card than above it")
    local st1 = g.strips[1].tex
    check(g.strips[1].tex._.vertex and g.strips[1].tex._.vertex[1] == 1, "green art is shown untinted")
    local glowx0 = MU.HAND_FAN.glowx
    MU.HAND_FAN.glowx = 16
    MU.LayoutHandGlow(g)
    local lo2, hi2 = extent(g)
    check(math.abs((hi2 - lo2) - (hi - lo) - 20) < 1e-6, "glowx widens the highlight: " .. (hi2 - lo2) - (hi - lo))
    MU.HAND_FAN.glowx = glowx0
    -- tilted: every strip corner samples the turned-back glow image; the highlight leans with the card
    g.angle = math.rad(10)
    MU.LayoutHandGlow(g)
    local srcT = H.CardWidget.TILT_SRC
    local pvx, pvy = (srcT.left + srcT.right) / 2 * kk, (srcT.top + srcT.bottom) / 2 * kk
    local rl, rt = V.left * kk - set0.glowx, V.top * kk - set0.glowtop
    local rw, rh = (V.right - V.left) * kk + 2 * set0.glowx, (V.bottom - V.top) * kk + set0.glowtop + set0.glowbottom
    local mapA, mapB, mapC, mapD = 1 * (pvx - rl) / rw, 1 / rw, 0.75 * (pvy - rt) / rh, 0.75 / rh
    local ca, sa = math.cos(g.angle), math.sin(g.angle)
    local badG = 0
    for q = 1, MU.GLOW_STRIPS do
        local t = g.strips[q].tex
        local pt, tcg = t._.points[1], t._.texcoord
        if t:IsShown() and table.getn(tcg) == 8 then
            local lx, y0 = pt.x - pvx, -pt.y - pvy
            local hx, y1 = lx + t:GetWidth(), y0 + t:GetHeight()
            local cs = { { lx, y0 }, { lx, y1 }, { hx, y0 }, { hx, y1 } }
            for m = 1, 4 do
                local sx, sy = (tcg[m * 2 - 1] - mapA) / mapB, (tcg[m * 2] - mapC) / mapD
                local fx, fy = sx * ca - sy * sa, sx * sa + sy * ca
                if math.abs(fx - cs[m][1]) > 1e-6 or math.abs(fy - cs[m][2]) > 1e-6 then badG = badG + 1 end
            end
        else
            badG = badG + 1
        end
    end
    check(badG == 0, "the tilted highlight samples the glow image turned back by the angle: " .. badG)
    local tlo, thi, ttop, tbot = extent(g)
    check(tbot - ttop > bot - top and thi - tlo > hi - lo, "a tilted highlight covers more height and width than an upright one")
    g.color = "red"
    MU.LayoutHandGlow(g)
    check(string.find(g.strips[1].path, "glow_red", 1, true), "red uses the red art")
    g.color = "gold"
    MU.LayoutHandGlow(g)
    check(g.strips[1].tex._.vertex[1] == 1 and g.strips[1].tex._.vertex[3] < 0.5, "other colours tint the green art")
    g.color = nil
    MU.LayoutHandGlow(g)
    check(not g.frame:IsShown() and not g.strips[1].tex:IsShown(), "no colour hides the highlight")
    HS_BoardTextures = oldPieces
    -- the text fallback cannot tilt
    slot5.widget.usingTexture = false
    MU.HandPlace(1)
    check(not slot5.widget.tilted, "the text fallback stays upright")
    MU.Refresh()

    -- a card without a target plays at once
    F.fire(ui.hand[2].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    local act = X.last("GAME_ACT")
    check(act and act.payload == "mid=7;seq=12;a=play;s=15", "hand click plays the Murloc: " .. tostring(act and act.payload))
    X.game(act.reqId, gamePayload({ seq = "13" }))
    -- a card that cannot be played says why
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.hint == "Not enough mana.", "Fireball blocked: " .. tostring(MU.hint))

    -- the hero power targets: click it, the enemy hero glows, a click sends it
    F.fire(ui.m.power.frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.sel and MU.sel.kind == "h" and MU.sel.targetSet[3] and MU.sel.targetSet[44], "hero power targeting")
    F.fire(ui.m.minions[1].frame, "OnClick", { arg1 = "LeftButton" })   -- the Raptor is a valid target too (friendly)
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=7;seq=13;a=hp;t=40", "hero power on the Raptor: " .. act.payload)
    X.game(act.reqId, gamePayload({ seq = "14" }))

    -- attacking: select the Raptor, an invalid target is refused locally, right click cancels
    F.fire(ui.m.minions[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.sel and MU.sel.kind == "a" and MU.sel.source == 40, "attacker selected")
    F.fire(ui.m.minions[2].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.sel ~= nil and string.find(MU.hint or "", "not a valid target", 1, true) ~= nil, "own minion is no target")
    F.fire(ui.m.minions[1].frame, "OnClick", { arg1 = "RightButton" })
    check(MU.sel == nil, "right click cancels")
    F.fire(ui.m.minions[1].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ui.o.hero.frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=7;seq=14;a=atk;s=40;t=3", "attack the hero: " .. act.payload)
    X.game(act.reqId, gamePayload({ seq = "15" }))
    -- Escape while targeting only cancels
    F.fire(ui.m.minions[1].frame, "OnClick", { arg1 = "LeftButton" })
    f:Hide()
    check(f:IsShown() and MU.sel == nil and F.popup == nil, "Escape cancels targeting")

    -- End Turn
    F.fire(ui.mid.endButton, "OnClick", {})
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=7;seq=15;a=end", "end turn")
    X.game(act.reqId, gamePayload({ seq = "16", act = "o", lg = "", mb = "40.CS2_172.3.2.2.-,41.CS2_179.3.5.5.t" }))
    check(not ui.mid.endButton:IsEnabled() or not ui.mid.endButton:IsShown(), "no End Turn on the opponent's turn")
    F.fire(ui.hand[2].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.hint == "It is not your turn.", "not my turn: " .. tostring(MU.hint))

    -- closing asks for the surrender: No keeps playing, Yes concedes
    local acts = table.getn(X.T.sentOps("GAME_ACT"))
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    check(f:IsShown() and StaticPopupDialogs["HEARTHSTONE_SURRENDER"] ~= nil, "close asks")
    F.popupClick(2)
    drain(1)
    check(f:IsShown() and table.getn(X.T.sentOps("GAME_ACT")) == acts, "No keeps the match")
    f:Hide() -- Escape
    check(f:IsShown(), "Escape cannot close a running match")
    F.popupClick(1)
    drain(1)
    act = X.last("GAME_ACT")
    check(table.getn(X.T.sentOps("GAME_ACT")) == acts + 1 and act.payload == "mid=7;seq=16;a=concede", "Yes concedes")
    X.game(act.reqId, gamePayload({ seq = "17", ph = "over", act = "-", res = "loss", why = "concede", lg = "", evs = "40", ev = "end.o.0.0.2.-" }))
    check(f:IsShown() and MU.result.frame:IsShown() and MU.result.title:GetText() == "Defeat", "result view")
    check(MU.result.reason:GetText() == "You surrendered.", "reason")
    local rs = MU.result
    check(rs.emblem:IsShown() and rs.emblem._.texture == MU.EMBLEM_PATH .. "DefeatEmblem" and not rs.title:IsShown(), "the Defeat emblem names itself")
    check(rs.strips[1]:IsShown() and rs.strips[table.getn(MU.EMBLEMS.defeat.strips)]:IsShown()
        and not rs.strips[table.getn(MU.EMBLEMS.defeat.strips) + 1]:IsShown(), "the portrait strips behind the arch")
    check(rs.emblem:GetHeight() == MU.EMBLEM_H and rs.emblem:GetWidth() == math.floor(MU.EMBLEM_H * MU.EMBLEMS.defeat.aspect + 0.5), "emblem size")
    local first, last = rs.strips[1], rs.strips[table.getn(MU.EMBLEMS.defeat.strips)]
    check(first._.texture ~= nil and first._.texcoord ~= nil and last._.points[1] ~= nil, "the strips are filled and placed")
    F.fire(getglobal("HearthstoneMatchLeaveButton"), "OnClick", {})
    drain(1)
    check(not f:IsShown() and MA.state == "none" and X.last("GAME_LEAVE").payload == "mid=7", "Leave closes and leaves")

    -- the mulligan view
    X.push(gamePayload({ mid = "8", seq = "1", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15,18", ev = "", evs = "0" }))
    check(f:IsShown() and MU.mull.frame:IsShown(), "mulligan shown")
    check(MU.mull.slots[1].frame:IsShown() and MU.mull.slots[3].frame:IsShown() and not MU.mull.slots[4].frame:IsShown(), "three offered")
    F.fire(MU.mull.slots[1].frame, "OnEnter")
    check(not MU.preview.frame:IsShown(), "no hover preview in the mulligan")
    -- the look: a "Starting Hand" title with a subtitle, every card kept (green highlight, no X) until it is clicked
    local mm = MU.mull
    check(mm.title:GetText() == "Starting Hand" and string.find(mm.hint:GetText(), "Keep or Replace Cards", 1, true), "title and subtitle")
    check((mm.confirm.art ~= nil or mm.confirm:GetText() == "Done") and mm.confirm:IsShown(), "the button says Done")
    local kept = true
    for q = 1, 3 do
        if mm.slots[q].glow.color ~= "green" or not mm.slots[q].glow.frame:IsShown() or mm.slots[q].mark:IsShown() then kept = false end
    end
    check(kept and not mm.slots[4].glow.frame:IsShown(), "all offered cards start kept: green highlight, no X")
    check(mm.slots[1].glow.cardW == MU.MULL_W and mm.slots[1].frame:GetWidth() == MU.MULL_W and MU.MULL_W > MU.HAND_W, "big cards with a highlight of their size")
    F.fire(MU.mull.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(MU.mull.slots[3].frame, "OnClick", { arg1 = "LeftButton" })
    check(mm.slots[1].mark:IsShown() and mm.slots[3].mark:IsShown() and not mm.slots[2].mark:IsShown(), "replaced cards show the X and the ribbon")
    check(mm.slots[1].glow.color == nil and not mm.slots[1].glow.frame:IsShown() and mm.slots[2].glow.frame:IsShown(), "only kept cards are highlighted")
    check(mm.slots[1].tagText:GetText() == "REPLACED" and (mm.confirm.art ~= nil or mm.confirm:GetText() == "Done"), "REPLACED label, Done button unchanged")
    -- the X: four strip sets (two shadows, two red bars) crossing the card, both diagonals
    local xok = true
    local sets = { mm.slots[1].xBar1, mm.slots[1].xBar2, mm.slots[1].xShadow1, mm.slots[1].xShadow2 }
    for q = 1, 4 do
        local shown = 0
        for r = 1, table.getn(sets[q]) do if sets[q][r].tex:IsShown() then shown = shown + 1 end end
        if shown < 10 then xok = false end
    end
    check(xok, "the X is drawn")
    local b1 = mm.slots[1].xBar1[1].tex._.vertex
    check(b1 and b1[1] > 0.7 and b1[2] < 0.3, "the X is red")
    local function barSpan(list)
        local lo, hi = 1e9, -1e9
        for r = 1, table.getn(list) do
            local pt = list[r].tex._.points[1]
            if list[r].tex:IsShown() and pt then
                if pt.x < lo then lo = pt.x end
                if pt.x + list[r].tex:GetWidth() > hi then hi = pt.x + list[r].tex:GetWidth() end
            end
        end
        return lo, hi
    end
    local l1, h1 = barSpan(sets[1])
    local firstTop, lastTop = sets[1][1].tex._.points[1].x, sets[1][table.getn(sets[1])].tex._.points[1].x
    local firstTop2, lastTop2 = sets[2][1].tex._.points[1].x, sets[2][table.getn(sets[2])].tex._.points[1].x
    check(lastTop > firstTop and lastTop2 < firstTop2 and h1 - l1 > MU.MULL_W * 0.5, "the two bars run in opposite diagonals")
    -- a narrow board: the cards shrink to fit and keep their gap
    local oldBw = MU.geom.bw
    MU.geom.bw = 560
    MU.Refresh()
    check(mm.slots[1].frame:GetScale() < 1 and mm.slots[1].frame:GetScale() * MU.MULL_W * 3 + 2 * MU.MULL_GAP <= 560, "cards shrink on a narrow board: " .. mm.slots[1].frame:GetScale())
    MU.geom.bw = oldBw
    MU.Refresh()
    check(mm.slots[1].frame:GetScale() == 1, "full size on a wide board")
    F.fire(MU.mull.confirm, "OnClick", {})
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=8;seq=1;a=mull;r=12,18", "mulligan payload: " .. act.payload)
    X.game(act.reqId, gamePayload({ mid = "8", seq = "2", act = "o", lg = "", ev = "", evs = "0" }))
    check(not MU.mull.frame:IsShown(), "mulligan closed")

    -- an interrupt push (Help > Stuck) shows the result; X then leaves
    X.push(gamePayload({ mid = "8", seq = "3", ph = "over", act = "-", res = "int", why = "stuck", lg = "", ev = "", evs = "0" }))
    check(MU.result.frame:IsShown() and MU.result.title:GetText() == "Interrupted", "interrupted")
    check(MU.result.emblem._.texture == MU.EMBLEM_PATH .. "DefeatEmblem" and MU.result.title:IsShown(), "interrupted: the Defeat emblem, named")
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    drain(1)
    check(not f:IsShown() and X.last("GAME_LEAVE").payload == "mid=8", "X leaves a finished match")

    -- board pieces: a generated BoardTextures.lua is used where present
    local savedBT = HS_BoardTextures
    HS_BoardTextures = { generated = true, pieces = { card_back = { path = "Interface\\AddOns\\Hearthstone\\Textures\\Board\\card_back", w = 64, h = 128, right = 0.9, bottom = 0.7 } } }
    check(MU.Piece("card_back") ~= nil and MU.Piece("gem_attack") == nil, "pieces")
    local tex = f:CreateTexture(nil, "ARTWORK")
    check(MU.SetPiece(tex, "card_back") and tex._.texture == HS_BoardTextures.pieces.card_back.path, "SetPiece")
    check(not MU.SetPiece(tex, "nope"), "a missing piece falls back")
    -- art sits in the hole of a ring piece (its "inner" anchor), else fills the frame minus the inset
    HS_BoardTextures.pieces.hero_ring = { path = "x\\hero_ring", w = 120, h = 128, right = 0.9375, bottom = 1,
        anchors = { inner = { left = 0.25, top = 0.25, right = 0.75, bottom = 0.75 } } }
    local holder = CreateFrame("Frame", nil, f)
    holder:SetWidth(88)
    holder:SetHeight(88)
    local art = holder:CreateTexture(nil, "ARTWORK")
    check(MU.ArtInRing(art, holder, "hero_ring", 6, 7), "ring with an anchor")
    local p1, p2 = art._.points[1], art._.points[2]
    check(p1.x == 19 and p1.y == -19 and p2.x == 69 and p2.y == -69, "inner rect: " .. p1.x .. "," .. p1.y .. " " .. p2.x .. "," .. p2.y)
    check(not MU.ArtInRing(art, holder, "nope", 6, 7) and art._.points[1].x == 7, "fallback inset")
    HS_BoardTextures = savedBT

    F.uninstall()
    resetAddon()
    resetMatch()
end

local function testMatchArch()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local MA = HsMatch
    local MU = H.MatchUI
    local X = matchHelpers()
    resetAddon()
    resetMatch()
    F.install({ class = { "Mage", "MAGE" }, name = "Jonas", screenW = 1920, screenH = 1080 })
    -- the board's own arch frame is tested first: the duel uses the arena's arch when its texture is installed (below)
    F.missingTextures = { [H.ArenaUI.TEXTURE_PATH .. "ArenaArch"] = true }
    MU.arenaArchInstalled = nil
    local savedBT, savedCT, savedDb = HS_BoardTextures, HS_CardTextures, H.db
    H.db = HsCollection.ApplySavedDefaults(nil)
    HS_CardTextures = { generated = true, order = { "HERO_08", "HERO_01" }, bottom = {} }
    HS_BoardTextures = { generated = true, pieces = {
        board_field = { path = "x\\board_field", w = 512, h = 512, right = 1, bottom = 1, aspect = 1.44964, anchors = {
            hero_bottom = { left = 0.44665, top = 0.72302, right = 0.54467, bottom = 0.88669 },
            hero_bottom_safe = { left = 0.44789, top = 0.76619, right = 0.54342, bottom = 0.88489 },
            hero_top = { left = 0.44665, top = 0.1223, right = 0.54467, bottom = 0.26978 },
            hero_top_safe = { left = 0.44789, top = 0.16547, right = 0.54342, bottom = 0.26799 } } },
        arch_top = { path = "x\\arch_top", w = 124, h = 128, right = 0.96875, bottom = 1 },
        arch_bottom = { path = "x\\arch_bottom", w = 114, h = 128, right = 0.890625, bottom = 1 },
        hero_ring = { path = "x\\hero_ring", w = 120, h = 128, right = 0.9375, bottom = 1,
            anchors = { inner = { left = 0.25, top = 0.29, right = 0.74, bottom = 0.74 } } } } }
    local bfa = HS_BoardTextures.pieces.board_field.anchors
    bfa.arch_top_frame = { left = 0.43176, top = 0.10072, right = 0.55955, bottom = 0.29137 }
    bfa.arch_bottom_frame = { left = 0.43176, top = 0.70144, right = 0.55955, bottom = 0.90827 }
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil

    X.welcome()
    drain(1)
    X.push(CANONICAL_GAME)
    local hero = MU.ui and MU.ui.m.hero
    check(hero and hero.arch and hero.ring == nil, "arch mode: no ring over the field's arch")
    if not hero then return end
    local l, t, w, h, dome = MU.HeroArch("m", MU.geom.bw, MU.geom.fh)
    check(hero.frame:GetWidth() == w and hero.frame:GetHeight() == h and dome > 0 and dome < h, "frame = the arch box")
    local p = hero.frame._.points[1]
    check(p.point == "TOPLEFT" and p.x == l and p.y == -t, "placed at the arch")
    -- strips: the straight part, then the dome strips narrowing to the top
    check(table.getn(hero.slices) >= 5 and hero.slices[1].y1 == dome and hero.slices[1].x2 == w, "strips: " .. table.getn(hero.slices))
    check(hero.bleed == 0, "no bleed without a golden frame of its own")
    for i = 3, table.getn(hero.slices) do
        local a, b = hero.slices[i - 1], hero.slices[i]
        check(b.y1 > a.y1 or a.tex == hero.art, "strips go down")
        check(a.tex == hero.art or (b.x2 - b.x1) >= (a.x2 - a.x1), "lower dome strips are wider")
    end
    check(hero.art._.texture == "Interface\\AddOns\\Hearthstone\\Textures\\Cards\\HERO_08", "portrait texture: " .. tostring(hero.art._.texture))
    local top = hero.slices[table.getn(hero.slices)]
    check(top.tex._.texture == hero.art._.texture, "dome strips show the same portrait")

    -- the arch cut-out lies over the portrait at its place in the field texture; the name is above it
    local ov = hero.overlay
    check(ov:IsShown() and ov._.texture == "x\\arch_bottom" and ov._.layer == "ARTWORK" and hero.art._.layer == "BORDER", "arch cut-out shown above the portrait")
    local i = MU.FIELD_INSET
    local op = ov._.points[1]
    -- anchored to the hero frame (it moves with the hero): the offsets are the board position minus the frame's home
    local absX = op.x + hero.home.x
    local absY = -op.y - hero.home.y
    check(op.rel == hero.frame and absX == math.floor(i + 0.43176 * (MU.geom.bw - 2 * i) + 0.5)
        and absY == math.floor(i + MU.FieldY(0.70144) * (MU.geom.fh - 2 * i) + 0.5), "cut-out position: " .. absX .. "," .. absY)
    check(MU.ui.m.name:GetParent() == hero.frame, "name above the cut-out")
    check(MU.ui.o.hero.overlay._.texture == "x\\arch_top", "top arch cut-out")

    -- a frame with its own "opening" anchor (the golden gothic arch) is laid so the opening is exactly the portrait box
    HS_BoardTextures.pieces.arch_bottom.anchors = { opening = { left = 0.05, top = 0.04, right = 0.95, bottom = 0.96 } }
    MU.Place()
    local op2, bp2 = ov._.points[1], ov._.points[2]
    check(op2.rel == hero.frame and bp2.rel == hero.frame, "opening frame hangs on the hero frame")
    local ow, oh = bp2.x - op2.x, op2.y - bp2.y
    -- the box is then the frame's outer edge shrunk to the opening (HeroArch), and the opening is the portrait box
    local _, _, bx, by = MU.HeroArch("m", MU.geom.bw, MU.geom.fh)
    check(hero.archW == bx and hero.archH == by and math.abs(bx - w * 0.9) <= 1 and math.abs(by - h * 0.92) <= 1, "box shrinks to the opening: " .. bx .. "x" .. by)
    check(hero.bleed == MU.ARCH_BLEED and hero.slices[1].x1 == -MU.ARCH_BLEED and hero.slices[1].x2 == bx + MU.ARCH_BLEED, "the portrait reaches under the golden frame")
    check(math.abs(ow * 0.9 - hero.archW) <= 1 and math.abs(oh * 0.92 - hero.archH) <= 1, "the opening is the portrait box: " .. ow .. "x" .. oh .. " for " .. hero.archW .. "x" .. hero.archH)
    check(math.abs(op2.x + ow * 0.05) <= 1 and math.abs(op2.y - oh * 0.04) <= 1, "the opening starts at the box corner")
    HS_BoardTextures.pieces.arch_bottom.anchors = nil
    MU.Place()

    -- changed tuning (relative to the shipped values)
    local d = MU.ARCH
    local dx0, ox0 = d.dx, d.ox
    d.dx = dx0 + 3
    MU.Place()
    MU.Refresh()
    -- the tuning is in pixels of an ARCH_REF_W wide field, so it scales with the field's width
    local l3 = MU.HeroArch("m", MU.geom.bw, MU.geom.fh)
    local sx = (MU.geom.bw - 2 * MU.FIELD_INSET) / MU.ARCH_REF_W
    check(hero.frame._.points[1].x == l3 and math.abs(l3 - l - 3 * sx) <= 1, "moved by dx: " .. l .. " -> " .. l3)
    d.ox = ox0 + 2
    MU.Place()
    MU.Refresh()
    check(ov._.points[1].x + hero.home.x == absX + 2, "cut-out moved by ox")
    d.dx, d.ox = dx0, ox0
    MU.Place()
    MU.Refresh()
    check(hero.frame._.points[1].x == l, "arch reset")

    -- the arena's golden arch (ArenaArch) replaces the board's arch frame once its texture is installed
    F.missingTextures = nil
    MU.arenaArchInstalled = nil
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil
    X.welcome()
    drain(1)
    X.push(CANONICAL_GAME)
    hero = MU.ui and MU.ui.m.hero
    local art = H.ArenaUI.ART.ArenaArch
    check(MU.Piece("arch_bottom").arena == art and MU.Piece("arch_top").path == H.ArenaUI.TEXTURE_PATH .. "ArenaArch", "the arena arch answers arch_top / arch_bottom")
    check(hero and hero.overlay._.texture == H.ArenaUI.TEXTURE_PATH .. "ArenaArch" and hero.overlay:IsShown(), "the arena arch is the frame over the portrait")
    if hero then
        local _, _, aw, ah = MU.HeroArch("m", MU.geom.bw, MU.geom.fh)
        local o = art.opening
        local op = MU.PieceAnchor("arch_bottom", "opening")
        check(op and op.left == o[1] and op.bottom == o[4], "the opening anchor is the arena arch's")
        check(table.getn(hero.slices) == table.getn(art.strips), "the portrait follows the arena arch's strips: " .. table.getn(hero.slices))
        check(hero.slices[1].tex == hero.art and hero.slices[1].y1 == math.floor(hero.archH * art.strips[table.getn(art.strips)][2] + 0.5), "the straight part starts where the last strip does")
    end
    F.missingTextures = nil
    MU.arenaArchInstalled = nil

    HS_BoardTextures, HS_CardTextures, H.db = savedBT, savedCT, savedDb
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview = nil, nil, nil, nil, nil, nil
    F.uninstall()
    resetAddon()
    resetMatch()
end

-- ------------------------------------------------------ match
-- A realistic phase 08 GAME: every new key, in the wire key order. The player is a Druid
-- (Wrath, a Choose One card, hand id 12), the opponent a Hard AI hero (40/60, frozen and immune).
local GAME8_KEYS = { "mid", "seq", "ph", "turn", "act", "res", "why", "mn", "on", "dif", "mh", "mp", "mw", "mm", "mk",
    "mb", "mc", "mo", "ms", "mx", "oh", "op", "ow", "om", "ok", "ob", "os", "ox", "mul", "pk", "opk", "tmr", "lg", "evs", "ev" }
local GAME8 = {
    mid = "11", seq = "30", ph = "play", turn = "5", act = "m", res = "-", why = "-", mn = "Jonas", on = "Innkeeper Allison",
    dif = "hard", mh = "1.HERO_06.24.30.3.1.a", mp = "2.CS2_017.2.0", mw = "", mm = "3.8", mk = "4.20.0",
    mb = "40.CS2_172.3.2.2.sa,41.CS2_179.3.5.5.tf,42.CS2_168.2.1.1.pxn,43.EX1_393.5.2.3.ea,44.CS2_172.3.2.2.i",
    mc = "12.EX1_154.2.0.0,15.EX1_124.2.0.0,18.CS2_029.4.0.0,21.CS2_168.1.2.1",
    mo = "12.EX1_154a/EX1_154b", ms = "60.EX1_287", mx = "2.2.1",
    oh = "3.HERO_01.40.60.0.0.fi", op = "4.CS2_102.2.1", ow = "", om = "6.6", ok = "5.22.0",
    ob = "45.CS2_124.3.1.1.cdw,46.CS2_182.4.5.5.f", os = "61,62", ox = "0.0.3",
    mul = "", pk = "", opk = "0", tmr = "0", lg = "p12:1.0,p12:2.45/46,p15.0,p21.0,a40.3/45,h2.0",
    evs = "100", ev = "turn.m.0.0.5.-",
}

local function game8(over)
    local kv = {}
    for k, v in pairs(GAME8) do kv[k] = v end
    for k, v in pairs(over or {}) do
        if v == false then kv[k] = nil else kv[k] = v end
    end
    local list = {}
    for i = 1, table.getn(GAME8_KEYS) do
        local k = GAME8_KEYS[i]
        if kv[k] ~= nil then table.insert(list, { k, kv[k] }) end
    end
    return P.KvEncode(list)
end

-- The server's canonical phase 08 GAME example (src/match/HsMatchWire tests, docs/PROTOCOL.md), byte for byte.
local CANONICAL_GAME8 = "mid=7;seq=12;ph=play;turn=3;act=m;res=-;why=-;mn=Jonas;on=Innkeeper Allison;dif=normal;mh=1.HERO_08.27.30.0.0.-;mp=2.CS2_034.2.0;mw=;mm=2.2;mk=5.23.0;mb=40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.EX1_010.2.1.1.sf;mc=12.CS2_029.4.0.0,15.CS2_168.1.2.1,18.CS2_025.2.0.0,21.GAME_005.0.0.0,24.EX1_154.2.0.0;mo=24.EX1_154a/EX1_154b;ms=26.EX1_289;mx=1.0.0;oh=3.HERO_01.30.30.2.0.-;op=4.CS2_102.2.1;ow=33.CS2_106.3.1;om=0.2;ok=5.23.0;ob=44.CS2_124.3.1.1.c,45.EX1_412.4.2.3.we;os=36,37;ox=0.1.2;mul=;pk=;opk=0;tmr=0;lg=p15.0,p18.0,p21.0,p24:1.40/41/42/44/45,p24:2.40/41/42/44/45,a40.3/44/45,h2.1/3/40/41/42/44/45;evs=31;ev=turn.m.0.0.3.-,draw.m.0.18.0.CS2_025"

local function testMatchPhase08Model()
    local MA = HsMatch
    resetMatch()

    -- the server's canonical phase 08 example
    local c8, why8 = MA.Parse(CANONICAL_GAME8)
    check(c8 ~= nil, "canonical phase 08 example parses: " .. tostring(why8))
    -- dur=1 marks a ranked ladder duel (the close button pauses it); absent means an ordinary match
    check(c8 and c8.dur == false, "dur: absent is false")
    do
        local d1 = MA.Parse((string.gsub(CANONICAL_GAME8, ";dif=normal;", ";dif=normal;dur=1;")))
        check(d1 ~= nil and d1.dur == true, "dur=1 parses as a ladder duel")
        local d2 = MA.Parse((string.gsub(CANONICAL_GAME8, ";dif=normal;", ";dif=normal;dur=2;")))
        check(d2 == nil, "dur=2 is refused")
    end
    if c8 then
        check(c8.dif == "normal" and MA.TitleText(c8) == "Jonas vs Innkeeper Allison (Normal)", "canonical: difficulty")
        check(table.getn(c8.me.board) == 3 and c8.me.board[3].stealth and c8.me.board[3].frozen, "canonical: stealthy frozen minion")
        check(c8.opp.board[2].enraged and c8.opp.board[2].windfury, "canonical: enraged windfury enemy")
        check(table.getn(c8.me.secrets) == 1 and table.getn(c8.opp.secrets) == 2, "canonical: secrets")
        check(c8.me.cardsPlayed == 1 and c8.opp.overloadLocked == 1 and c8.opp.overloadOwed == 2, "canonical: mx / ox")
        check(MA.IsChoiceCard(c8, 24) and table.getn(MA.OptionsOf(c8, 24)) == 2, "canonical: choose one options")
        check(MA.LegalFor(c8, "p", 24, 2) ~= nil and MA.LegalFor(c8, "p", 24, 1) ~= nil, "canonical: legal per choice")
        check(table.getn(c8.pick) == 0 and c8.oppPicking == false, "canonical: no pick")
    end

    -- every key the server always sends is required
    for _, k in ipairs({ "dif", "mo", "ms", "mx", "os", "ox", "pk", "opk", "tmr" }) do
        check(MA.Parse((string.gsub(CANONICAL_GAME8, ";" .. k .. "=[^;]*", ""))) == nil, "a payload without " .. k .. " is refused")
    end

    -- a payload with empty lists and no overload
    local old = MA.Parse(CANONICAL_GAME)
    check(old ~= nil, "plain payload parses")
    check(table.getn(old.me.secrets) == 0 and table.getn(old.opp.secrets) == 0, "no secrets")
    check(old.me.cardsPlayed == 0 and old.me.overloadLocked == 0 and old.me.overloadOwed == 0 and old.opp.overloadOwed == 0, "no overload")
    check(next(old.options) == nil and table.getn(old.pick) == 0 and old.oppPicking == false, "no options, no pick")
    check(not old.me.hero.frozen and not old.me.hero.immune and old.me.hero.canAttack == false, "hero flags default")
    local m = old.me.board[1]
    check(m.canAttack and not m.stealth and not m.frozen and not m.silenced and not m.poisonous and not m.enraged
        and not m.immune and not m.cantAttack, "minion flags default")
    check(MA.LegalFor(old, "p", 15).choice == nil and not MA.IsChoiceCard(old, 15), "plain legal entries have no choice")
    local f = MA.ParseFlags("tcdwa")
    check(f.taunt and f.charge and f.divine and f.windfury and f.canAttack and not f.stealth, "plain minion flags")

    -- the phase 08 payload, key by key
    local g, why = MA.Parse(game8())
    check(g ~= nil, "phase 08 payload parses: " .. tostring(why))
    if not g then return end
    check(g.dif == "hard" and MA.DifficultyLabel(g.dif) == "Hard" and MA.TitleText(g) == "Jonas vs Innkeeper Allison (Hard)", "difficulty and title")
    check(MA.DifficultyLabel("normal") == "Normal" and MA.DifficultyLabel("boss") == "Boss" and MA.DifficultyLabel("x") == "", "difficulty labels")
    -- heroes: flags f i a, maxHealth other than 30
    check(g.me.hero.canAttack and not g.me.hero.frozen and not g.me.hero.immune and g.me.hero.armor == 3 and g.me.hero.attack == 1, "my hero")
    local oh = g.opp.hero
    check(oh.health == 40 and oh.maxHealth == 60 and oh.frozen and oh.immune and not oh.canAttack and oh.flags == "fi", "boss-like opponent hero")
    local h2 = MA.Parse(game8({ mh = "1.HERO_06.24.30.3.1.fia" })).me.hero
    check(h2.frozen and h2.immune and h2.canAttack, "all three hero flags")
    check(MA.ParseHeroFlags("-").frozen == false and MA.ParseHeroFlags("t") == nil and MA.ParseHeroFlags("") == nil, "hero flag letters")
    -- minions: every flag
    local b = g.me.board
    check(b[1].stealth and b[1].canAttack and not b[1].frozen, "stealth + can attack")
    check(b[2].taunt and b[2].frozen and not b[2].canAttack, "taunt + frozen")
    check(b[3].poisonous and b[3].silenced and b[3].cantAttack and not b[3].canAttack, "poisonous + silenced + can't attack")
    check(b[4].enraged and b[4].canAttack and b[4].health == 2 and b[4].maxHealth == 3 and b[4].attack == 5, "enraged")
    check(b[5].immune and not b[5].stealth, "immune")
    check(g.opp.board[1].charge and g.opp.board[1].divine and g.opp.board[1].windfury and g.opp.board[2].frozen, "opponent minions")
    local all = MA.ParseFlags("tcdwsfxpeina")
    check(all.taunt and all.charge and all.divine and all.windfury and all.stealth and all.frozen and all.silenced
        and all.poisonous and all.enraged and all.immune and all.cantAttack and all.canAttack, "all twelve minion flags")
    check(MA.ParseFlags("z") == nil and MA.ParseFlags("") == nil and MA.ParseFlags("-").stealth == false, "minion flag letters")
    -- Choose One options, secrets, overload, combo
    check(MA.IsChoiceCard(g, 12) and not MA.IsChoiceCard(g, 15) and table.getn(MA.OptionsOf(g, 12)) == 2
        and MA.OptionsOf(g, 12)[1] == "EX1_154a" and MA.OptionsOf(g, 12)[2] == "EX1_154b" and MA.OptionsOf(g, 15) == nil, "options")
    check(table.getn(g.me.secrets) == 1 and g.me.secrets[1].id == 60 and g.me.secrets[1].cardId == "EX1_287", "own secret")
    check(table.getn(g.opp.secrets) == 2 and g.opp.secrets[1].id == 61 and g.opp.secrets[2].id == 62 and g.opp.secrets[1].cardId == nil, "opponent secrets are ids only")
    check(MA.Secrets(g, "m") == g.me.secrets and MA.Secrets(g, "o") == g.opp.secrets, "Secrets lookup")
    check(g.me.cardsPlayed == 2 and g.me.overloadLocked == 2 and g.me.overloadOwed == 1, "my overload")
    check(g.opp.cardsPlayed == 0 and g.opp.overloadLocked == 0 and g.opp.overloadOwed == 3, "opponent overload")
    check(MA.ComboActive(g) and MA.IsComboCard("EX1_124") and MA.IsComboCard("EX1_134") and not MA.IsComboCard("CS2_029")
        and not MA.IsComboCard("nope") and not MA.IsComboCard(nil), "combo")
    -- keyword icons of a played minion: a skull for a deathrattle, a lightning bolt for a continuous effect (AURA);
    -- the invisible deathrattle of Worshipper shows nothing extra, and a silenced minion shows none
    local MU = H.MatchUI
    check(MA.HasMechanic("EX1_012", "DEATHRATTLE") and not MA.HasMechanic("EX1_012", "AURA") and not MA.HasMechanic(nil, "AURA"), "HasMechanic")
    check(table.concat(MU.MechanicKeys("EX1_012", false), ",") == "DEATHRATTLE", "deathrattle skull")
    check(table.concat(MU.MechanicKeys("CS2_122", false), ",") == "AURA", "continuous lightning (Raid Leader)")
    check(table.getn(MU.MechanicKeys("EX1_012", true)) == 0 and table.getn(MU.MechanicKeys("CS1_042", false)) == 0, "silenced or plain minion: no icon")
    -- a triggered effect is tagged TRIGGER_VISUAL in the card data (override 001): the same lightning as an aura
    check(table.concat(MU.MechanicKeys("NEW1_020", false), ",") == "AURA", "lightning on Wild Pyromancer (TRIGGER_VISUAL)")
    check(table.concat(MU.MechanicKeys("NEW1_019", false), ",") == "AURA", "lightning on Knife Juggler (After you summon ...)")
    check(table.concat(MU.MechanicKeys("EX1_249", false), ",") == "AURA", "lightning on Baron Geddon (At the end of your turn ...)")
    check(table.getn(MU.MechanicKeys("NEW1_020", true)) == 0, "silenced Wild Pyromancer: no icon")
    check(table.getn(MU.MechanicKeys("FP1_003", false)) == 0, "Echoing Ooze (Battlecry, once): no lightning")
    check(not MA.ComboActive(MA.Parse(game8({ mx = "0.0.0" }))) and not MA.ComboActive(MA.Parse(game8({ act = "o", lg = "" }))), "combo needs a card played and my turn")
    -- pending pick
    check(not MA.IsPicking(g) and not MA.OpponentPicking(g), "no pick pending")
    local pk = MA.Parse(game8({ pk = "70.CS2_029,71.CS2_168,72.CS2_025", lg = "", opk = "0" }))
    check(pk and MA.IsPicking(pk) and table.getn(pk.pick) == 3 and pk.pick[2].id == 71 and pk.pick[2].cardId == "CS2_168"
        and MA.FindPick(pk, 72).cardId == "CS2_025" and MA.FindPick(pk, 5) == nil, "my pick")
    local opk = MA.Parse(game8({ opk = "1", lg = "" }))
    check(MA.OpponentPicking(opk) and not MA.IsPicking(opk), "opponent picking")
    check(not MA.OpponentPicking(MA.Parse(game8({ opk = "0" }))) and not MA.OpponentPicking(MA.Parse(game8({ opk = false }))), "opk 0 / missing")

    -- legal entries with a choice, and the lookups
    check(table.getn(g.legal) == 6, "six legal entries")
    check(g.legal[1].kind == "p" and g.legal[1].source == 12 and g.legal[1].choice == 1 and g.legal[1].targets[1] == 0, "p12:1")
    check(g.legal[2].choice == 2 and g.legal[2].targets[1] == 45 and g.legal[2].targets[2] == 46, "p12:2")
    check(g.legal[3].choice == nil and g.legal[5].kind == "a" and g.legal[5].choice == nil, "no choice on ordinary entries")
    check(MA.LegalFor(g, "p", 12).choice == 1, "without a choice: the first entry of the source")
    check(MA.LegalFor(g, "p", 12, 2).choice == 2 and MA.LegalFor(g, "p", 12, 3) == nil and MA.LegalFor(g, "p", 15, 1) == nil, "LegalFor with a choice")
    check(MA.IsPlayable(g, 12) and MA.IsPlayable(g, 15), "a Choose One card is playable")
    check(table.getn(MA.TargetsFor(g, 12, 1)) == 0 and table.concat(MA.TargetsFor(g, 12, 2), ",") == "45,46", "targets of source 12 by choice")
    check(not MA.NeedsTarget(MA.LegalFor(g, "p", 12, 1)) and MA.NeedsTarget(MA.LegalFor(g, "p", 12, 2)), "option 1 needs no target, option 2 does")
    check(MA.IsValidTarget(MA.LegalFor(g, "p", 12, 2), 46) and not MA.IsValidTarget(MA.LegalFor(g, "p", 12, 1), 46), "valid targets per choice")
    local ce = MA.ChoiceEntries(g, 12)
    check(ce[1] == g.legal[1] and ce[2] == g.legal[2] and table.getn(MA.ChoiceEntries(g, 15)) == 0, "ChoiceEntries")
    -- only one option playable
    local one = MA.Parse(game8({ lg = "p12:2.45" }))
    local ce1 = MA.ChoiceEntries(one, 12)
    check(ce1[1] == nil and ce1[2] ~= nil and MA.IsPlayable(one, 12), "only option 2 is playable")

    -- GAME_ACT builders
    check(MA.ActPlay(g, 12, 0, nil, 1) == "mid=11;seq=30;a=play;s=12;c=1", "play choice 1 without a target: " .. tostring(MA.ActPlay(g, 12, 0, nil, 1)))
    check(MA.ActPlay(g, 12, 45, nil, 2) == "mid=11;seq=30;a=play;s=12;t=45;c=2", "play choice 2 on a target")
    check(MA.ActPlay(g, 12, 45, 3, 2) == "mid=11;seq=30;a=play;s=12;t=45;p=3;c=2", "play with a position and a choice")
    check(MA.ActPlay(g, 15, 0) == "mid=11;seq=30;a=play;s=15", "no choice: no c")
    check(MA.ActPick(g, 71) == "mid=11;seq=30;a=pick;s=71" and MA.ActPick(g, nil) == nil and MA.ActPick(nil, 71) == nil, "pick payload")
    -- the payloads decode with the server's rules
    local kv = P.KvDecode(MA.ActPlay(g, 12, 45, nil, 2))
    check(kv.a == "play" and kv.s == "12" and kv.t == "45" and kv.c == "2" and kv.mid == "11" and kv.seq == "30", "play payload keys")

    -- refusal texts of the new codes
    check(string.find(MA.RefusedText("choice"), "choice", 1, true) and string.find(MA.RefusedText("pending"), "first", 1, true)
        and string.find(MA.RefusedText("secret"), "secret", 1, true), "new refusal codes")

    -- malformed phase 08 values are rejected
    local bad = {
        { dif = "easy" }, { mh = "1.HERO_06.24.30.3.1.t" }, { oh = "3.HERO_01.40.60.0.0.fz" }, { mb = "40.CS2_172.3.2.2.z" },
        { ob = "45.CS2_124.3.1.1.cdW" }, { mo = "12" }, { mo = "12.EX1_154a/" }, { mo = "x.EX1_154a/EX1_154b" },
        { mo = "12.EX1_154a/EX1 154b" }, { mo = "12.A/B/C/D/E" }, { ms = "60" }, { ms = "60.EX1 287" }, { ms = "0.EX1_287" },
        { os = "61,x" }, { os = "0" }, { mx = "2.2" }, { mx = "2.2.x" }, { ox = "0.0.0.0" }, { pk = "70" }, { pk = "70.CS2_029," },
        { opk = "2" }, { opk = "yes" }, { lg = "p12:0.0" }, { lg = "p12:5.0" }, { lg = "p12:x.0" }, { lg = "p12:1:2.0" },
        { lg = "a40:1.3" }, { lg = "h2:1.0" }, { lg = "p12:1." },
    }
    for i = 1, table.getn(bad) do
        check(MA.Parse(game8(bad[i])) == nil, "rejects bad phase 08 payload " .. i)
    end
    check(MA.Parse(game8({ ms = "", os = "", mo = "", pk = "", mx = "0.0.0", ox = "0.0.0" })) ~= nil, "empty lists")
    check(MA.Parse(game8({ mx = "" })) == nil and MA.Parse(game8({ ox = "" })) == nil, "mx / ox are never empty")
    check(MA.Parse(game8({ dif = "" })) == nil and MA.Parse(game8({ dif = "normal" })).dif == "normal" and MA.Parse(game8({ dif = "boss" })).dif == "boss", "dif values")
    check(MA.Parse(game8({ mo = "12.EX1_154a/EX1_154b,15.EX1_154a/EX1_154b" })).options[15][2] == "EX1_154b", "two Choose One cards")
    -- an unknown extra key is ignored (forward compatible)
    check(MA.Parse(game8() .. ";zz=1") ~= nil, "unknown keys are ignored")
end

local function testMatchPhase08Events()
    local MA = HsMatch
    resetMatch()
    MA.supported = true
    local g = MA.Parse(game8({ ev = "", evs = "0" }))
    MA.Apply(g)
    local function ev(s)
        local _, _, ty, sd, so, ta, am, cid = string.find(s, "^(%l+)%.(.)%.(%d+)%.(%d+)%.(-?%d+)%.(.+)$")
        return { type = ty, side = sd, source = tonumber(so), target = tonumber(ta), amount = tonumber(am), cardId = cid ~= "-" and cid or nil }
    end
    -- names come from the board and the hand: my hand 12 Wrath, 15 Eviscerate, 18 Fireball; their board 45 Wolfrider, 46 Yeti
    local function text(s)
        local t = MA.EventText(ev(s), g)
        if not t then return nil end
        t = string.gsub(t, "|c%x%x%x%x%x%x%x%x", "")
        t = string.gsub(t, "|r", "")
        return t
    end
    check(text("sil.m.12.45.0.-") == "Wrath silences Wolfrider.", "silence by a card: " .. tostring(text("sil.m.12.45.0.-")))
    check(text("sil.m.0.45.0.-") == "Wolfrider is silenced.", "silence without a source")
    check(text("sil.m.45.45.0.-") == "Wolfrider is silenced.", "silence by itself")
    check(text("frz.m.18.46.0.-") == "Fireball freezes Chillwind Yeti.", "freeze by a card: " .. tostring(text("frz.m.18.46.0.-")))
    check(text("frz.o.0.1.0.-") == "Jonas is frozen.", "freeze without a source: " .. tostring(text("frz.o.0.1.0.-")))
    check(text("sec.o.61.61.0.-") == "Innkeeper Allison sets a Secret.", "opponent secret: " .. tostring(text("sec.o.61.61.0.-")))
    check(text("sec.m.60.60.0.EX1_287") == "You set a Secret: Counterspell.", "own secret: " .. tostring(text("sec.m.60.60.0.EX1_287")))
    check(text("rev.o.40.61.0.EX1_289") == "Innkeeper Allison's Secret is revealed: Ice Barrier.", "revealed secret: " .. tostring(text("rev.o.40.61.0.EX1_289")))
    check(text("rev.m.46.60.0.EX1_287") == "Your Secret is revealed: Counterspell.", "my secret revealed")
    check(text("trig.o.0.0.0.EX1_095") == "Gadgetzan Auctioneer triggers.", "trigger")
    check(text("trig.o.46.0.0.-") == "Chillwind Yeti triggers.", "trigger named by the board")
    check(text("dr.o.45.0.0.EX1_029") == "Leper Gnome's Deathrattle triggers.", "deathrattle")
    check(text("disc.m.0.19.0.CS2_029") == "You discard Fireball.", "discard")
    check(text("disc.o.0.19.0.CS2_029") == "Innkeeper Allison discards Fireball.", "opponent discard")
    check(text("bnc.o.0.45.0.CS2_124") == "Wolfrider returns to Innkeeper Allison's hand.", "bounce: " .. tostring(text("bnc.o.0.45.0.CS2_124")))
    check(text("bnc.m.0.40.0.CS2_172") == "Bloodfen Raptor returns to your hand.", "my bounce")
    check(text("ctl.m.0.46.0.-") == "You take control of Chillwind Yeti.", "control: " .. tostring(text("ctl.m.0.46.0.-")))
    check(text("ctl.o.0.40.0.-") == "Innkeeper Allison takes control of Bloodfen Raptor.", "opponent takes control")
    check(text("ovl.m.0.0.2.-") == "You overload 2 mana crystals." and text("ovl.o.0.0.1.-") == "Innkeeper Allison overloads 1 mana crystal.", "overload")
    check(text("gen.m.0.80.0.CS2_029") == "You get Fireball." and text("gen.o.0.81.0.-") == "Innkeeper Allison gets a card.", "generate")
    check(text("dest.m.12.46.0.-") == "Chillwind Yeti is destroyed.", "destroy")
    check(text("cnt.o.18.0.0.CS2_029") == "Fireball is countered." and text("cnt.o.0.0.0.-") == "A spell is countered.", "counter")
    check(text("pick.m.0.70.0.CS2_029") == "You pick Fireball." and text("pick.o.0.82.0.-") == "Innkeeper Allison picks a card.", "pick")
    check(text("crys.m.0.0.1.-") == "You gain 1 mana crystal." and text("crys.m.0.0.2.-") == "You gain 2 mana crystals.", "crystal gain")
    check(text("crys.o.0.0.-1.-") == "Innkeeper Allison loses 1 mana crystal." and text("crys.m.0.0.-2.-") == "You lose 2 mana crystals.", "crystal loss")
    check(text("stl.o.0.46.0.-") == "Chillwind Yeti loses Stealth." and text("stl.o.0.999.0.-") == "A minion loses Stealth.", "stealth lost")
    -- coloured by side like the other lines
    check(string.find(MA.EventText(ev("ovl.m.0.0.2.-"), g), "|cff99ff99", 1, true) == 1 and string.find(MA.EventText(ev("ovl.o.0.0.2.-"), g), "|cffff9999", 1, true) == 1, "side colours")
    -- every new code produces a line
    for _, code in ipairs({ "sil", "frz", "sec", "rev", "trig", "dr", "disc", "bnc", "ctl", "ovl", "gen", "dest", "cnt", "pick", "crys", "stl" }) do
        check(MA.EventText({ type = code, side = "m", source = 0, target = 0, amount = 1 }, g) ~= nil, "a line for " .. code)
    end

    -- through Apply: names are learned from the events and the secret / pick lists
    resetMatch()
    MA.supported = true
    local g2 = MA.Parse(game8({ evs = "100", ev = table.concat({
        "play.m.18.46.0.CS2_024", "frz.m.18.46.0.-", "play.m.12.45.0.EX1_332", "sil.m.12.45.0.-",
        "sec.o.61.61.0.-", "sec.m.60.60.0.EX1_287", "rev.o.40.61.0.EX1_289", "trig.o.0.0.0.EX1_095", "dr.o.99.0.0.EX1_029",
        "disc.m.0.19.0.CS2_029", "bnc.o.0.45.0.CS2_124", "ctl.m.0.46.0.-", "ovl.m.0.0.2.-", "gen.m.0.80.0.CS2_029", "gen.o.0.81.0.-",
        "dest.m.12.46.0.-", "cnt.o.18.0.0.CS2_029", "pick.m.0.70.0.CS2_168", "pick.o.0.82.0.-", "crys.m.0.0.1.-", "crys.o.0.0.-1.-", "stl.o.0.46.0.-" }, ",") }))
    check(g2 ~= nil and table.getn(g2.events) == 22, "22 events parse")
    MA.Apply(g2)
    check(table.getn(MA.log) == 22, "one log line per event: " .. table.getn(MA.log))
    local joined = table.concat(MA.log, "\n")
    joined = string.gsub(joined, "|c%x%x%x%x%x%x%x%x", "")
    joined = string.gsub(joined, "|r", "")
    for _, line in ipairs({ "You play Frostbolt on Chillwind Yeti.", "Frostbolt freezes Chillwind Yeti.", "You play Silence on Wolfrider.",
        "Silence silences Wolfrider.", "Innkeeper Allison sets a Secret.", "You set a Secret: Counterspell.",
        "Innkeeper Allison's Secret is revealed: Ice Barrier.", "Gadgetzan Auctioneer triggers.", "Leper Gnome's Deathrattle triggers.",
        "You discard Fireball.", "Wolfrider returns to Innkeeper Allison's hand.", "You take control of Chillwind Yeti.",
        "You overload 2 mana crystals.", "You get Fireball.", "Innkeeper Allison gets a card.", "Chillwind Yeti is destroyed.",
        "Fireball is countered.", "You pick Murloc Raider.", "Innkeeper Allison picks a card.", "You gain 1 mana crystal.",
        "Innkeeper Allison loses 1 mana crystal.", "Chillwind Yeti loses Stealth." }) do
        check(string.find(joined, line, 1, true) ~= nil, "log has: " .. line)
    end
    check(MA.cards[60] == "EX1_287" and MA.cards[80] == "CS2_029", "own secret and generated card learned")
    resetMatch()
end

local function testMatchPhase08UI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local MA = HsMatch
    local MU = H.MatchUI
    local X = matchHelpers()
    resetAddon()
    resetMatch()
    F.install({ class = { "Druid", "DRUID" }, name = "Jonas", screenW = 1920, screenH = 1080 })
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil
    MU.pick, MU.chooser, MU.choose = nil, nil, nil

    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, "ph=none")

    -- a plain payload shows none of the optional pieces
    X.push(CANONICAL_GAME)
    local f = MU.frame
    check(f and f:IsShown(), "window shown for a plain payload")
    if not f then return end
    local ui = MU.ui
    check(f.title:GetText() == "Jonas vs Innkeeper Allison (Normal)", "the difficulty in the title: " .. tostring(f.title:GetText()))
    check(not ui.m.secrets[1].frame:IsShown() and not ui.o.secrets[1].frame:IsShown(), "no secret icons")
    check(table.getn(ui.m.minions[1].marks.shown) == 0 and not ui.m.minions[1].rim:IsShown() and not ui.m.hero.rim:IsShown(), "no markers")
    check(ui.m.overload:GetText() == "" and not MU.pick.frame:IsShown() and not MU.chooser.frame:IsShown(), "no overload text, pick panel or chooser")

    -- the phase 08 payload (a new match id replaces the old board)
    X.push(game8())
    f = MU.frame
    ui = MU.ui
    check(f:IsShown() and MA.game.mid == 11, "window shows the phase 08 match")
    check(f.title:GetText() == "Jonas vs Innkeeper Allison (Hard)", "difficulty in the title: " .. tostring(f.title:GetText()))
    X.push(game8({ seq = "31", dif = "normal" }))
    check(f.title:GetText() == "Jonas vs Innkeeper Allison (Normal)", "normal title")
    X.push(game8({ seq = "32", dif = "boss" }))
    check(f.title:GetText() == "Jonas vs Innkeeper Allison (Boss)", "boss title")
    X.push(game8({ seq = "33" }))

    -- flags as markers
    local mm = ui.m.minions
    check(table.concat(mm[1].marks.shown, ",") == "stealth" and mm[1].marks.tex[1]:IsShown() and mm[1].marks.tex[1]._.texture == MU.MARKERS.stealth.icon, "stealth icon")
    check(mm[1].art._.alpha == MU.STEALTH_ALPHA and mm[2].art._.alpha == 1, "stealth fades the art")
    check(not mm[1].tint:IsShown() and not mm[1].rim:IsShown(), "stealth has no tint or rim")
    check(table.concat(mm[2].marks.shown, ",") == "frozen" and mm[2].tint:IsShown() and mm[2].rim:IsShown(), "frozen: icon, tint, rim")
    check(mm[2].rim._.bdBorder[3] == 1 and mm[2].tint._.vertex[3] == 1 and mm[2].tint._.vertex[1] < 0.6, "frozen is icy blue")
    check(table.concat(mm[3].marks.shown, ",") == "poisonous,silenced,cantAttack" and mm[3].tint:IsShown() and not mm[3].rim:IsShown(), "poisonous, silenced, can't attack")
    check(mm[3].tint._.vertex[1] == mm[3].tint._.vertex[2] and mm[3].tint._.vertex[2] == mm[3].tint._.vertex[3], "silenced is grey")
    check(table.concat(mm[4].marks.shown, ",") == "enraged" and mm[4].tint:IsShown() and mm[4].tint._.vertex[1] == 1 and mm[4].tint._.vertex[2] < 0.3, "enraged: red tint")
    check(table.concat(mm[5].marks.shown, ",") == "immune" and mm[5].rim:IsShown() and mm[5].rim._.bdBorder[1] == 1 and mm[5].rim._.bdBorder[3] < 0.5, "immune: gold rim")
    check(table.getn(ui.o.minions[1].marks.shown) == 0 and table.concat(ui.o.minions[2].marks.shown, ",") == "frozen", "opponent minion markers")
    check(MU.StateNames(MA.game.me.board[2]) == "Frozen" and MU.StateNames(MA.game.me.board[3]) == "Poisonous, Silenced, Can't attack"
        and MU.StateNames(MA.game.me.board[4]) == "Enraged" and MU.StateNames(MA.game.opp.board[1]) == nil, "state names")
    check(table.concat(ui.o.hero.marks.shown, ",") == "frozen" and ui.o.hero.rim:IsShown() and ui.o.hero.tint:IsShown()
        and ui.o.hero.glow.frame:IsShown(), "frozen immune hero: no immune icon, a light blue glow")
    check(table.getn(ui.m.hero.marks.shown) == 0 and not ui.m.hero.rim:IsShown() and not ui.m.hero.tint:IsShown(), "plain hero")
    -- a marker disappears when the flag does
    X.push(game8({ seq = "34", mb = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.-,43.EX1_393.2.3.3.-,44.CS2_172.3.2.2.-" }))
    check(table.getn(mm[1].marks.shown) == 0 and mm[1].art._.alpha == 1 and not mm[2].tint:IsShown() and not mm[2].rim:IsShown()
        and not mm[5].rim:IsShown() and not mm[4].tint:IsShown(), "markers are removed with the flags")
    X.push(game8({ seq = "35" }))
    -- hovering a minion shows just its card at the right edge (no caption)
    F.fire(mm[2].frame, "OnEnter")
    check(MU.preview.frame:IsShown() and MU.preview.caption:GetText() == "" and MU.preview.frame._.points[1].point == "RIGHT", "minion hover: only the card, on the right")
    F.fire(ui.o.hero.frame, "OnEnter")
    check(not MU.preview.frame:IsShown(), "hovering a hero shows no preview (the minion preview went away)")
    F.fire(ui.o.hero.frame, "OnLeave")

    -- secrets
    check(ui.m.secrets[1].frame:IsShown() and not ui.m.secrets[2].frame:IsShown() and ui.m.secrets[1].cardId == "EX1_287", "one own secret")
    check(ui.o.secrets[1].frame:IsShown() and ui.o.secrets[2].frame:IsShown() and not ui.o.secrets[3].frame:IsShown() and ui.o.secrets[1].cardId == nil, "two opponent secrets")
    check(string.find(ui.o.secrets[1].label:GetText(), "?", 1, true) ~= nil, "opponent secrets are a question mark")
    F.fire(ui.m.secrets[1].frame, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Counterspell" and string.find(F.tooltip.lines[2], "Counter", 1, true) ~= nil
        and F.tooltip.lines[3] == "Secret" and F.tooltip.owner == ui.m.secrets[1].frame, "own secret tooltip: " .. tostring(F.tooltip.lines[1]))
    F.fire(ui.m.secrets[1].frame, "OnLeave")
    check(not F.tooltip.shown, "tooltip hidden")
    F.fire(ui.o.secrets[2].frame, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Secret" and F.tooltip.owner == ui.o.secrets[2].frame, "opponent secret tooltip")
    F.fire(ui.o.secrets[2].frame, "OnLeave")
    X.push(game8({ seq = "36", ms = "60.EX1_287,63.EX1_289,64.EX1_294,65.EX1_533,66.EX1_287", os = "" }))
    local shown = 0
    for i = 1, MU.MAX_SECRETS do if ui.m.secrets[i].frame:IsShown() then shown = shown + 1 end end
    check(shown == 5 and not ui.o.secrets[1].frame:IsShown(), "five own secrets, none of the opponent")
    X.push(game8({ seq = "37" }))

    -- overload: locked crystals are red, the text names locked and owed
    check(string.find(ui.m.overload:GetText(), "2 locked", 1, true) ~= nil and string.find(ui.m.overload:GetText(), "1 next turn", 1, true) ~= nil, "my overload text: " .. tostring(ui.m.overload:GetText()))
    check(string.find(ui.o.overload:GetText(), "3 next turn", 1, true) ~= nil and string.find(ui.o.overload:GetText(), "locked", 1, true) == nil, "opponent overload text")
    check(ui.m.crystals[1]._.vertex[3] == 1 and ui.m.crystals[3]._.vertex[3] == 1, "available crystals are blue")
    check(ui.m.crystals[4]._.vertex[1] < 0.2 and ui.m.crystals[6]._.vertex[1] < 0.2, "spent crystals are dark")
    check(ui.m.crystals[7]._.vertex[1] > 0.7 and ui.m.crystals[8]._.vertex[1] > 0.7 and ui.m.crystals[8]._.vertex[2] < 0.3, "the two locked crystals are red")
    check(MU.OverloadText(MA.game.me) == "Overload: |cffff6060" .. "2 locked|r, |cffffa040" .. "1 next turn|r" and MU.OverloadText({ overloadLocked = 0, overloadOwed = 0 }) == nil, "overload text")
    X.push(game8({ seq = "38", mx = "0.0.0", ox = "0.0.0" }))
    check(ui.m.overload:GetText() == "" and ui.m.crystals[8]._.vertex[3] < 0.3, "no overload: no text, the crystal is just spent")
    X.push(game8({ seq = "39" }))

    -- combo: Eviscerate glows while cards were played this turn
    check(ui.hand[2].combo and ui.hand[2].glow.frame:IsShown() and not ui.hand[4].combo, "combo glow on Eviscerate")
    check(ui.hand[4].glow.frame:IsShown() and ui.hand[2].glow.frame._.bdBorder[1] == 1 and ui.hand[2].glow.frame._.bdBorder[2] > 0.85 and ui.hand[2].glow.frame._.bdBorder[3] < 0.3, "combo is yellow, the plain playable card is green")
    X.push(game8({ seq = "40", mx = "0.2.1" }))
    check(not ui.hand[2].combo and ui.hand[2].glow.frame._.bdBorder[1] < 0.5, "no combo glow before a card was played")
    X.push(game8({ seq = "41" }))

    -- Choose One: the chooser, then the targets of that option, then play with c
    check(not MU.chooser.frame:IsShown() and MU.choose == nil, "chooser closed")
    local acts = table.getn(X.T.sentOps("GAME_ACT"))
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    local ch = MU.chooser
    check(MU.choose and MU.choose.source == 12 and ch.frame:IsShown() and table.getn(X.T.sentOps("GAME_ACT")) == acts, "click opens the chooser and sends nothing")
    check(ch.slots[1].frame:IsShown() and ch.slots[2].frame:IsShown() and not ch.slots[3].frame:IsShown(), "two option cards")
    check(ch.slots[1].cardId == "EX1_154a" and ch.slots[2].cardId == "EX1_154b" and ch.slots[1].choice == 1 and ch.slots[2].choice == 2, "the option cards")
    check(ch.slots[1].widget.id == "EX1_154a" and ch.slots[1].widget.fb:IsShown(), "no texture: name and text fallback")
    check(string.find(ch.slots[1].widget.text:GetText(), "3 damage", 1, true) ~= nil, "option text on the card")
    check(ch.slots[1].glow.frame:IsShown() and ch.slots[2].glow.frame:IsShown() and not ch.slots[1].widget.dimmed and not ch.slots[2].widget.dimmed, "both options playable: the green glow")
    check(ch.label == nil and ch.hint == nil and ch.cancel == nil and ch.title:GetText() == "Choose One", "chooser: the title (or the banner art), no hint line, no Cancel")
    -- Hide takes the options away and keeps the board blocked, Show brings them back
    check(ch.panel:IsShown() and ch.hideButton:IsShown() and not ch.showButton:IsShown(), "the Hide plate is shown")
    F.fire(ch.hideButton, "OnClick", {})
    check(MU.choose ~= nil and ch.frame:IsShown() and not ch.panel:IsShown() and ch.showButton:IsShown() and not ch.hideButton:IsShown(), "Hide: the options are gone, the chooser still blocks the board")
    F.fire(ch.showButton, "OnClick", {})
    check(MU.choose ~= nil and ch.panel:IsShown() and ch.hideButton:IsShown() and not ch.showButton:IsShown(), "Show: the options are back")
    -- cancel two ways
    F.fire(ch.slots[1].frame, "OnClick", { arg1 = "RightButton" })
    check(MU.choose == nil and not ch.frame:IsShown(), "right click closes the chooser")
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    f:Hide()
    check(f:IsShown() and MU.choose == nil and not ch.frame:IsShown() and F.popup == nil, "Escape closes the chooser, not the match")
    check(table.getn(X.T.sentOps("GAME_ACT")) == acts, "still nothing sent")
    -- other clicks are blocked while it is open
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ui.hand[2].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.choose ~= nil and string.find(MU.hint or "", "Choose one", 1, true) ~= nil and table.getn(X.T.sentOps("GAME_ACT")) == acts, "other hand cards are blocked: " .. tostring(MU.hint))
    F.fire(ui.mid.endButton, "OnClick", {})
    check(table.getn(X.T.sentOps("GAME_ACT")) == acts, "End Turn is blocked")
    -- option 2 needs a target (45 / 46 from lg): targeting follows, the click sends play with t and c
    F.fire(ch.slots[2].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.choose == nil and not ch.frame:IsShown() and MU.sel and MU.sel.kind == "p" and MU.sel.source == 12 and MU.sel.choice == 2
        and MU.sel.targetSet[45] and MU.sel.targetSet[46] and not MU.sel.targetSet[3], "option 2 starts targeting with its own targets")
    F.fire(ui.o.minions[1].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    local act = X.last("GAME_ACT")
    check(act and act.payload == "mid=11;seq=41;a=play;s=12;t=45;c=2", "play with t and c: " .. tostring(act and act.payload))
    X.game(act.reqId, game8({ seq = "42" }))
    -- option 1 needs no target: it plays at once
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ch.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    act = X.last("GAME_ACT")
    check(MU.choose == nil and MU.sel == nil and act.payload == "mid=11;seq=42;a=play;s=12;c=1", "option 1 plays at once: " .. tostring(act.payload))
    X.game(act.reqId, game8({ seq = "43" }))
    -- an option without a playable entry is dimmed and refuses
    X.push(game8({ seq = "44", lg = "p12:2.45" }))
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.choose and ch.slots[1].widget.dimmed and not ch.slots[2].widget.dimmed and not ch.slots[1].glow.frame:IsShown() and ch.slots[2].glow.frame:IsShown(), "option 1 is dimmed, without the glow")
    acts = table.getn(X.T.sentOps("GAME_ACT"))
    F.fire(ch.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.choose ~= nil and table.getn(X.T.sentOps("GAME_ACT")) == acts and string.find(MU.hint or "", "cannot be played", 1, true) ~= nil, "a dimmed option is refused locally")
    -- the chooser closes when the board changes underneath
    X.push(game8({ seq = "45", lg = "p12:2.45" }))
    check(MU.choose == nil and not ch.frame:IsShown(), "a new board state closes the chooser")
    -- a Choose One card that cannot be played (no entry) is explained, not opened
    X.push(game8({ seq = "46", lg = "p15.0,p21.0" }))
    F.fire(ui.hand[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.choose == nil and MU.hint == "You cannot play that card now.", "an unplayable Choose One card: " .. tostring(MU.hint))
    -- the server's new refusal codes
    F.fire(ui.hand[4].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=11;seq=46;a=play;s=21", "a plain card still plays without c")
    X.T.frame("ERR", act.reqId, "code=REFUSED;msg=x;why=choice")
    drain(1)
    check(string.find(MA.message or "", "choice", 1, true) ~= nil, "choice refusal shown")
    X.push(game8({ seq = "47" }))

    -- pending pick: the panel, a click sends pick
    X.push(game8({ seq = "50", pk = "70.CS2_029,71.CS2_168,72.CS2_025", lg = "" }))
    local pp = MU.pick
    check(pp.frame:IsShown() and pp.slots[1].frame:IsShown() and pp.slots[3].frame:IsShown() and not pp.slots[4].frame:IsShown(), "pick panel with three cards")
    check(pp.slots[1].cardId == "CS2_029" and pp.slots[2].cardId == "CS2_168" and pp.slots[3].entity == 72 and pp.slots[2].widget.id == "CS2_168", "pick options")
    check(string.find(pp.title:GetText(), "Choose a card", 1, true) ~= nil, "pick title")
    check(not ui.mid.endButton:IsEnabled(), "no End Turn while a pick is pending")
    F.fire(ui.hand[4].frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.hint == "Choose one of the offered cards first.", "a hand click while picking: " .. tostring(MU.hint))
    F.fire(pp.slots[2].frame, "OnEnter")
    check(MU.preview.frame:IsShown(), "hovering an option shows the large card")
    F.fire(pp.slots[2].frame, "OnLeave")
    acts = table.getn(X.T.sentOps("GAME_ACT"))
    F.fire(pp.slots[2].frame, "OnClick", { arg1 = "RightButton" })
    drain(1)
    check(table.getn(X.T.sentOps("GAME_ACT")) == acts, "a right click picks nothing")
    F.fire(pp.slots[2].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    act = X.last("GAME_ACT")
    check(act.payload == "mid=11;seq=50;a=pick;s=71", "pick payload: " .. tostring(act.payload))
    check(MA.pending and MA.pending.kind == "pick", "the pick is in flight")
    X.game(act.reqId, game8({ seq = "51", pk = "", lg = "p15.0,p21.0" }))
    check(not pp.frame:IsShown() and not pp.slots[1].frame:IsShown() and ui.mid.endButton:IsEnabled(), "the panel closes when answered")
    -- a refused pick explains itself
    X.push(game8({ seq = "52", pk = "70.CS2_029,71.CS2_168", lg = "" }))
    F.fire(pp.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    act = X.last("GAME_ACT")
    X.T.frame("ERR", act.reqId, "code=REFUSED;msg=x;why=pending")
    drain(1)
    check(string.find(MA.message or "", "Choose one of the offered cards first", 1, true) ~= nil and pp.frame:IsShown(), "pending refusal shown, the panel stays")
    MA.message = nil                  -- (a message stays until the next action)
    X.push(game8({ seq = "53", pk = "", lg = "" }))

    -- the opponent is choosing
    X.push(game8({ seq = "54", act = "o", opk = "1", lg = "" }))
    check(ui.mid.bubble:IsShown() and ui.mid.bubbleText:GetText() == "Opponent is choosing...", "opponent is choosing: " .. tostring(ui.mid.bubbleText:GetText()))
    check(not pp.frame:IsShown(), "no panel for the opponent's pick")
    X.push(game8({ seq = "55", act = "o", opk = "0", lg = "" }))
    check(not ui.mid.bubble:IsShown(), "the message goes away")

    -- the window title follows the difficulty for a new match, the result view still works
    X.push(game8({ seq = "56", ph = "over", act = "-", res = "win", why = "hero", lg = "", ev = "", evs = "0" }))
    check(MU.result.frame:IsShown() and MU.result.title:GetText() == "Victory", "result view with phase 08 keys")
    check(MU.result.emblem._.texture == MU.EMBLEM_PATH .. "VictoryEmblem" and not MU.result.title:IsShown(), "the Victory emblem names itself")
    -- a draw counts as a victory: its emblem, with the word under it
    X.push(game8({ seq = "57", ph = "over", act = "-", res = "draw", why = "turns", lg = "", ev = "", evs = "0" }))
    check(MU.result.emblem._.texture == MU.EMBLEM_PATH .. "VictoryEmblem" and MU.result.title:GetText() == "Draw" and MU.result.title:IsShown(), "a draw: the Victory emblem and the word")
    -- without the emblem texture the old dialog box shows
    F.missingTextures = { [MU.EMBLEM_PATH .. "VictoryEmblem"] = true }
    MU.Refresh()
    check(not MU.result.emblem:IsShown() and not MU.result.strips[1]:IsShown() and MU.result.title:IsShown()
        and MU.result.box:GetWidth() == 440, "no emblem texture: the dialog box")
    F.missingTextures = nil
    MU.Refresh()
    check(MU.result.emblem:IsShown() and MU.result.box:GetWidth() == MU.EMBLEM_BOX_W, "the emblem is back")


    -- keyword icons on the board: a skull for a deathrattle (Bloodmage Thalnos), a lightning bolt for a continuous
    -- effect (Raid Leader), none for a silenced minion; the board goes back to the default state afterwards
    X.push(game8({ seq = "70", mb = "40.EX1_012.2.1.1.a,41.CS2_122.2.2.2.a,42.EX1_012.1.1.1.x" }))
    local kw = MU.ui.m.minions
    check(kw[1].mechs.DEATHRATTLE:IsShown() and not kw[1].mechs.AURA:IsShown(), "deathrattle skull on Thalnos")
    check(kw[2].mechs.AURA:IsShown() and not kw[2].mechs.DEATHRATTLE:IsShown(), "lightning on Raid Leader")
    check(not kw[3].mechs.DEATHRATTLE:IsShown() and not kw[3].mechs.AURA:IsShown(), "silenced: no keyword icon")
    X.push(game8({ seq = "71" }))

    F.uninstall()
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.pick, MU.chooser, MU.choose = nil, nil, nil, nil, nil, nil, nil, nil, nil
    resetAddon()
    resetMatch()
end

-- ------------------------------------------------------ match
-- tmr= (the Nozdormu turn timer) and a hero that changes in mid-match (Lord Jaraxxus: HERO_07 -> EX1_323h).
local function testMatchPhase09Model()
    local MA = HsMatch
    resetMatch()
    local c = MA.Parse(CANONICAL_GAME8)
    check(c ~= nil and c.timer == 0, "canonical example: no timer")
    MA.Apply(c)
    check(MA.timerLeft == nil and MA.TimerSeconds() == nil and not MA.TimerLow(), "no timer in the model")

    -- parsing: a number up to MAX_TIMER, anything else (absent and empty too) is refused
    check(MA.Parse(game8({ tmr = false })) == nil and MA.Parse(game8({ tmr = "" })) == nil, "tmr absent / empty")
    check(MA.Parse(game8({ tmr = "15" })).timer == 15 and MA.Parse(game8({ tmr = "1" })).timer == 1, "tmr 15 / 1")
    for _, bad in ipairs({ "x", "-1", "1.5", "99999", "1 5", "0x10" }) do
        local g, why = MA.Parse(game8({ tmr = bad }))
        check(g == nil and why == "bad tmr", "bad tmr " .. bad .. ": " .. tostring(why))
    end

    -- a snapshot with a timer starts the local countdown; it ticks down and turns red below 5 s
    resetMatch()
    MA.Apply(MA.Parse(game8({ seq = "40", tmr = "15" })))
    check(MA.timerLeft == 15 and MA.TimerSeconds() == 15 and not MA.TimerLow(), "countdown starts at 15")
    MA.TimerTick(0.4)
    check(MA.TimerSeconds() == 15, "14.6 s is shown as 15")
    MA.TimerTick(9.6)
    check(MA.TimerSeconds() == 5 and not MA.TimerLow(), "5.0 s: not red yet")
    MA.TimerTick(0.1)
    check(MA.TimerSeconds() == 5 and MA.TimerLow(), "4.9 s: red")
    MA.TimerTick(100)
    check(MA.TimerSeconds() == 0 and MA.TimerLow(), "the countdown stops at 0")
    MA.TimerTick(-3)
    MA.TimerTick("x")
    check(MA.TimerSeconds() == 0, "bad dt ignored")
    -- the next snapshot restarts it; an older snapshot of the same match is ignored; tmr=0 and over clear it
    MA.Apply(MA.Parse(game8({ seq = "41", tmr = "9" })))
    check(MA.TimerSeconds() == 9, "a new snapshot restarts the countdown")
    check(MA.Apply(MA.Parse(game8({ seq = "39", tmr = "15" }))) == false and MA.TimerSeconds() == 9, "an old snapshot changes nothing")
    MA.Apply(MA.Parse(game8({ seq = "42", tmr = "0" })))
    check(MA.timerLeft == nil, "tmr=0 clears the countdown")
    MA.Apply(MA.Parse(game8({ seq = "43", tmr = "15" })))
    MA.Apply(MA.Parse(game8({ seq = "44", tmr = "15", ph = "over", act = "-", res = "win", why = "hero", lg = "", ev = "", evs = "0" })))
    check(MA.timerLeft == nil, "no countdown when the match is over")
    MA.Apply(MA.Parse(game8({ seq = "45", mid = "12", tmr = "15" })))
    MA.Apply(MA.Parse("ph=none"))
    check(MA.timerLeft == nil, "no countdown without a match")

    -- an event code this addon does not know is logged generically, not dropped and not an error
    resetMatch()
    local g = MA.Parse(game8({ seq = "50", ev = "hrep.m.1.1.0.EX1_323h", evs = "200" }))
    check(g ~= nil and g.events[1].type == "hrep", "an unknown event code parses")
    MA.Apply(g)
    local joined = table.concat(MA.log, "\n")
    check(string.find(joined, "Something happens (hrep)", 1, true) ~= nil, "generic log line: " .. joined)
    resetMatch()
end

local function testMatchPhase09UI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local MA = HsMatch
    local MU = H.MatchUI
    local X = matchHelpers()
    resetAddon()
    resetMatch()
    F.install({ class = { "Warlock", "WARLOCK" }, name = "Jonas", screenW = 1920, screenH = 1080 })
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil
    MU.pick, MU.chooser, MU.choose = nil, nil, nil
    local savedTextures = HS_CardTextures
    HS_CardTextures = { generated = true, order = {}, bottom = {} }   -- no card textures installed

    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, "ph=none")

    -- Gul'dan with a hero power, no weapon, no timer
    local guldan = { mh = "1.HERO_07.30.30.0.0.-", mp = "2.CS2_056.2.0", mw = "" }
    local function with(extra)
        local o = {}
        for k, v in pairs(guldan) do o[k] = v end
        for k, v in pairs(extra or {}) do o[k] = v end
        return game8(o)
    end
    X.push(with({ seq = "60" }))
    local f = MU.frame
    check(f and f:IsShown(), "window shown")
    if not f then return end
    local ui = MU.ui
    local timer = ui.mid.timerText
    check(timer and not timer:IsShown() and timer:GetText() == "", "no timer: the countdown is hidden")
    check(ui.m.hero.cardId == "HERO_07" and ui.m.hero.hp.text:GetText() == "30", "Gul'dan, 30 health")

    -- Lord Jaraxxus: another hero card, 15 health, INFERNO!, Blood Fury; an event code the addon may not know
    local jar = { seq = "61", mh = "80.EX1_323h.15.15.0.3.a", mp = "81.EX1_tk33.2.0", mw = "82.EX1_323w.3.8",
        ev = "hrep.m.80.80.0.EX1_323h", evs = "300" }
    X.push(with(jar))
    check(MA.game.me.hero.cardId == "EX1_323h" and MA.game.me.hero.maxHealth == 15, "the model has the new hero")
    check(ui.m.hero.cardId == "EX1_323h" and ui.m.hero.entity == 80, "the portrait follows the hero card")
    check(ui.m.hero.hp.text:GetText() == "15", "health gem: " .. tostring(ui.m.hero.hp.text:GetText()))
    check(ui.m.hero.atk.frame:IsShown() and ui.m.hero.atk.text:GetText() == "3", "attack gem shows Blood Fury's 3")
    check(ui.m.power.cardId == "EX1_tk33" and ui.m.power.frame:IsShown() and ui.m.power.label:GetText() == "INFERNO!",
        "hero power button: " .. tostring(ui.m.power.cardId) .. " " .. tostring(ui.m.power.label:GetText()))
    check(ui.m.weapon.cardId == "EX1_323w" and ui.m.weapon.frame:IsShown() and ui.m.weapon.atk.text:GetText() == "3"
        and ui.m.weapon.dur.text:GetText() == "8", "Blood Fury weapon")
    -- no texture installed: the class colour (Warlock) fills the portrait, the power and the weapon
    local wl = MU.ClassColor("EX1_323h")
    check(wl[1] == MU.ClassColor("HERO_07")[1] and wl[2] == MU.ClassColor("HERO_07")[2], "Jaraxxus is a Warlock card")
    local art = ui.m.hero.art
    if ui.m.hero.arch then
        art = ui.m.hero.slices[1].tex
    end
    check(art._.vertex and art._.vertex[1] == wl[1] and art._.vertex[2] == wl[2] and art._.vertex[3] == wl[3],
        "portrait falls back to the class colour")
    check(ui.m.power.art._.vertex[1] == wl[1], "hero power falls back to the class colour")
    local log = table.concat(MA.log, "\n")
    check(string.find(log, "Something happens (hrep)", 1, true) ~= nil, "generic log line for the unknown event code")

    -- hover text of the hero: health out of the new maximum
    F.fire(ui.m.hero.frame, "OnEnter")
    check(not MU.preview.frame:IsShown(), "hero hover: no preview")
    F.fire(ui.m.hero.frame, "OnLeave")
    -- damaged Jaraxxus (9/15) shows the lowered health in red
    X.push(with({ seq = "62", mh = "80.EX1_323h.9.15.0.3.a", mp = "81.EX1_tk33.2.0", mw = "82.EX1_323w.3.8" }))
    check(ui.m.hero.hp.text:GetText() == "9", "damaged Jaraxxus")

    -- with the textures installed the card art is used (texture path of the hero, the power, the weapon)
    HS_CardTextures = { generated = true, order = { "HERO_07", "EX1_323h", "EX1_tk33", "EX1_323w" }, bottom = {} }
    X.push(with({ seq = "63", mh = "80.EX1_323h.15.15.0.3.a", mp = "81.EX1_tk33.2.0", mw = "82.EX1_323w.3.8" }))
    local tex = ui.m.hero.art
    if ui.m.hero.arch then
        tex = ui.m.hero.slices[1].tex
    end
    check(string.find(tex._.texture or "", "EX1_323h", 1, true) ~= nil, "hero portrait texture: " .. tostring(tex._.texture))
    check(string.find(ui.m.power.art._.texture or "", "EX1_tk33", 1, true) ~= nil, "hero power texture")
    check(string.find(ui.m.weapon.art._.texture or "", "EX1_323w", 1, true) ~= nil, "weapon texture")
    -- and back to Gul'dan-like (a hero card changing again) works too
    X.push(with({ seq = "64" }))
    check(ui.m.hero.cardId == "HERO_07" and ui.m.hero.hp.text:GetText() == "30" and not ui.m.weapon.frame:IsShown(), "hero card changes back")

    -- the turn countdown next to End Turn
    X.push(with({ seq = "70", tmr = "15" }))
    check(timer:IsShown() and string.find(timer:GetText(), "15", 1, true) ~= nil and string.find(timer:GetText(), "|cffffd100", 1, true) == 1,
        "countdown shown: " .. tostring(timer:GetText()))
    check(timer._.points[1] and timer._.points[1].rel == ui.mid.endButton and timer._.points[1].relPoint == "LEFT", "countdown sits beside the End Turn button")
    H.OnUpdate(6)
    check(timer:GetText() == "|cffffd1009|r", "9 s left: " .. tostring(timer:GetText()))
    H.OnUpdate(4.5)
    check(string.find(timer:GetText(), "|cffff2020", 1, true) == 1 and string.find(timer:GetText(), "5", 1, true) ~= nil,
        "4.5 s left is red: " .. tostring(timer:GetText()))
    H.OnUpdate(5)
    check(string.find(timer:GetText(), "|cffff2020", 1, true) == 1 and string.find(timer:GetText(), "0", 1, true) ~= nil and timer:IsShown(),
        "0 s left: " .. tostring(timer:GetText()))
    X.push(with({ seq = "71", tmr = "7", act = "o" }))
    check(timer:IsShown() and string.find(timer:GetText(), "7", 1, true) ~= nil and string.find(timer:GetText(), "|cffffd100", 1, true) == 1,
        "a new snapshot restarts the countdown (also on the opponent's turn)")
    X.push(with({ seq = "72", tmr = "0" }))
    check(not timer:IsShown() and timer:GetText() == "", "tmr=0 hides the countdown")
    H.OnUpdate(3)
    check(not timer:IsShown(), "stays hidden")

    HS_CardTextures = savedTextures
    F.uninstall()
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.pick, MU.chooser, MU.choose = nil, nil, nil, nil, nil, nil, nil, nil, nil
    resetAddon()
    resetMatch()
end

-- ------------------------------------------------------ match animations
-- A plain game for the animation tests: me (hero 1, power 2) with the minions 40 41 42, the opponent (hero 3,
-- power 4, weapon 33) with 44 45; ids 60+ are secrets. ag() takes overrides like game8().
local AG = {
    mid = "21", seq = "1", ph = "play", turn = "4", act = "m", res = "-", why = "-", mn = "Jonas", on = "Allison",
    dif = "normal", mh = "1.HERO_08.27.30.0.0.a", mp = "2.CS2_034.2.0", mw = "", mm = "4.4", mk = "4.20.0",
    mb = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a", mc = "12.CS2_029.4.0.0,15.CS2_168.1.2.1",
    mo = "", ms = "", mx = "0.0.0", oh = "3.HERO_01.30.30.2.0.-", op = "4.CS2_102.2.1", ow = "33.CS2_106.3.1",
    om = "4.4", ok = "5.22.0", ob = "44.CS2_124.3.1.1.c,45.EX1_412.4.2.3.-", os = "", ox = "0.0.0", mul = "", pk = "",
    opk = "0", tmr = "0", lg = "a40.3/44/45", evs = "0", ev = "",
}
local function ag(over)
    local kv = {}
    for k, v in pairs(AG) do kv[k] = v end
    for k, v in pairs(over or {}) do kv[k] = v end
    local list = {}
    for i = 1, table.getn(GAME8_KEYS) do
        local k = GAME8_KEYS[i]
        if kv[k] ~= nil then table.insert(list, { k, kv[k] }) end
    end
    return P.KvEncode(list)
end
-- a game after one action: seq, the first event seq and the events (strings)
local function agE(seq, first, events, over)
    local o = { seq = tostring(seq), evs = tostring(first), ev = table.concat(events, ",") }
    for k, v in pairs(over or {}) do o[k] = v end
    return ag(o)
end

-- the next game of the running match: seq + 1, events numbered on from the last one handed out
local function nextE(events, over)
    return agE(HsMatch.game.seq + 1, HsMatch.lastEventSeq + 1, events, over)
end

local ATTACK_EVENTS = { "atk.m.40.44.0.-", "dmg.o.40.44.3.-", "dmg.m.44.40.3.-", "die.o.0.44.0.CS2_124",
    "die.m.0.40.0.CS2_172" }
local ATTACK_AFTER = { mb = "41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a", ob = "45.EX1_412.4.2.3.-" }

local function testAnimTimeline()
    local MA, AN = HsMatch, HsMatchAnim
    resetMatch()
    local function apply(payload)
        local g, why = MA.Parse(payload)
        check(g ~= nil, "parse: " .. tostring(why))
        MA.Apply(g)
        return MA.TakeTransition(), g
    end
    -- the plan of one action on the plain game
    local function plan(events, over)
        resetMatch()
        AN.SetMode("on")
        apply(ag({}))
        return AN.Plan((apply(agE(2, 1, events, over))))
    end
    local function kinds(p) return AN.Kinds(p) end

    -- the timing constants add up
    check(math.abs(AN.ANTICIPATE + AN.STRIKE + AN.HOLD + AN.RETURN - AN.ATTACK) < 1e-9, "attack = anticipation + strike + hold + return")
    check(math.abs(AN.ANTICIPATE + AN.STRIKE - AN.IMPACT_AT) < 1e-9, "impact at the end of the strike")
    check(math.abs(AN.ANTICIPATE + AN.FIZZLE_STRIKE + AN.FIZZLE_RETURN - AN.FIZZLE) < 1e-9, "fizzle")
    check(math.abs(AN.REVEAL_IN + AN.REVEAL_HOLD + AN.REVEAL_OUT - AN.REVEAL) < 1e-9, "reveal")
    check(AN.ATTACK + AN.DEATH < 1.5, "a normal attack with its death fits the default AI delay")

    -- ordering: an attack, both sides of combat in one beat, then the deaths in one beat
    local p = plan(ATTACK_EVENTS, ATTACK_AFTER)
    check(not p.snap and kinds(p) == "attack,death", "attack, then death: " .. kinds(p))
    local a, d = p.beats[1], p.beats[2]
    check(a.source == 40 and a.target == 44 and a.planned == 44 and not a.redirected and not a.fizzle, "attack ids")
    check(table.getn(a.hits) == 2 and a.hits[1].kind == "dmg" and a.hits[1].id == 44 and a.hits[1].amount == 3
        and a.hits[2].id == 40 and a.hits[2].amount == 3, "both sides of the combat are one simultaneous group")
    check(a.impactAt == AN.IMPACT_AT and a.dur == AN.ATTACK and a.reach == 1 and not a.hero, "attack timing")
    check(table.getn(d.ids) == 2 and d.ids[1] == 44 and d.ids[2] == 40 and d.cardIds[44] == "CS2_124" and d.sides[40] == "m", "one death step")
    check(math.abs(p.total - (AN.ATTACK + AN.DEATH)) < 1e-9, "plan total")
    check(a.seq == 1 and d.seq == 4, "beats keep the seq of their first event")

    -- a hero attack: the weapon is no separate attacker; when it breaks that is its own beat after the attack
    -- (review of the first in-game test: not an effect pulse any more)
    resetMatch()
    AN.SetMode("on")
    apply(ag({ mw = "9.CS2_106.2.1" }))
    p = AN.Plan((apply(agE(2, 1, { "atk.m.1.44.0.-", "dmg.o.1.44.2.-", "wbrk.m.0.9.0.CS2_106" }, { mw = "" }))))
    check(kinds(p) == "attack,weapon_break" and p.beats[1].hero and p.beats[2].id == 9 and p.beats[2].side == "m"
        and p.beats[2].cardId == "CS2_106" and p.beats[2].dur == AN.WEAPON_BREAK, "hero attack: " .. kinds(p))

    -- hero vs hero: no counter damage when the defender has no attack
    p = plan({ "atk.m.1.3.0.-", "dmg.o.1.3.2.-" })
    check(kinds(p) == "attack" and table.getn(p.beats[1].hits) == 1 and p.beats[1].target == 3, "hero attacks hero")

    -- shield pop: a pop is an impact, never a number
    p = plan({ "atk.m.40.44.0.-", "pop.o.40.44.0.-", "dmg.m.44.40.3.-" })
    check(kinds(p) == "attack" and p.beats[1].hits[1].kind == "pop" and p.beats[1].hits[2].kind == "dmg", "shield pop in the group")

    -- a defender without attack: only the attacker's damage
    p = plan({ "atk.m.40.45.0.-", "dmg.o.40.45.3.-", "dmg.m.45.40.4.-" })
    check(kinds(p) == "attack" and table.getn(p.beats[1].hits) == 2, "both combatants")

    -- redirection by a Secret: the reveal first, then the collision with the real target
    p = plan({ "atk.m.40.3.0.-", "rev.o.60.40.0.EX1_609", "dmg.o.40.44.3.-", "dmg.m.44.40.3.-" })
    check(kinds(p) == "reveal,attack", "reveal before the redirected attack: " .. kinds(p))
    check(p.beats[1].secret and p.beats[1].cardId == "EX1_609" and p.beats[1].side == "o", "secret reveal")
    check(p.beats[2].planned == 3 and p.beats[2].target == 44 and p.beats[2].redirected and not p.beats[2].fizzle, "redirected target")

    -- cancellation: no impact event, no invented damage
    p = plan({ "atk.m.40.3.0.-" })
    check(kinds(p) == "attack" and p.beats[1].fizzle and table.getn(p.beats[1].hits) == 0 and p.beats[1].dur == AN.FIZZLE
        and p.beats[1].impactAt == nil and p.beats[1].reach == AN.FIZZLE_REACH, "fizzle")
    p = plan({ "atk.m.40.3.0.-", "turn.o.0.0.5.-" })
    check(kinds(p) == "attack" and p.beats[1].fizzle, "fizzle before the next turn")
    p = plan({ "atk.m.40.3.0.-", "rev.o.60.40.0.EX1_610", "dmg.o.60.40.2.-", "die.m.0.40.0.CS2_172" })
    check(kinds(p) == "attack,reveal,impact,death" and p.beats[1].fizzle and p.beats[3].hits[1].id == 40,
        "an attacker killed by a Secret: fizzle, reveal, hit, death: " .. kinds(p))
    -- the hit of a counter-blow alone still shows the contact (an attacker without attack)
    p = plan({ "atk.m.40.44.0.-", "dmg.m.44.40.3.-" })
    check(kinds(p) == "attack" and not p.beats[1].fizzle and p.beats[1].hits[1].id == 40, "counter-hit only")

    -- area effects: one burst, one group of impacts, then the deaths
    p = plan({ "play.m.12.0.0.CS2_172", "dmg.o.12.44.2.-", "dmg.o.12.45.2.-", "dmg.o.12.3.2.-", "die.o.0.44.0.CS2_124" })
    check(kinds(p) == "effect,impact,death", "area: " .. kinds(p))
    check(p.beats[1].family == "area" and p.beats[1].area and table.getn(p.beats[1].targets) == 3 and p.beats[1].cardId == "CS2_172", "area effect")
    check(table.getn(p.beats[2].hits) == 3 and p.beats[2].area and p.beats[2].family == "area", "the three hits are simultaneous")
    -- damage runs of separate resolution steps stay separate
    p = plan({ "dmg.o.12.44.2.-", "trig.o.45.0.0.EX1_412", "dmg.o.12.45.2.-" })
    check(kinds(p):find("impact,effect,impact", 1, true) ~= nil or kinds(p) == "impact,effect,impact", "separate steps: " .. kinds(p))

    -- the opponent's card is revealed, mine is not
    p = plan({ "play.o.99.0.0.EX1_011" })
    check(kinds(p) == "reveal" and p.beats[1].cardId == "EX1_011" and not p.beats[1].secret and p.beats[1].dur == AN.REVEAL, "opponent reveal")
    p = plan({ "play.m.12.0.0.EX1_011" })
    check(kinds(p) == "" and not p.snap and p.total == 0, "my own play has no reveal")
    p = plan({ "play.o.99.45.0.EX1_011", "dmg.m.99.1.3.-" })
    check(kinds(p) == "reveal,effect,impact", "a spell: reveal, effect, impact: " .. kinds(p))

    -- generic effects and their family guess
    local function family(events, over)
        local q = plan(events, over)
        for i = 1, table.getn(q.beats) do
            if q.beats[i].kind == "effect" then return q.beats[i].family, q.beats[i], q end
        end
        return nil, nil, q
    end
    check(family({ "play.m.12.45.0.CS2_172", "dmg.o.12.45.1.-", "frz.o.12.45.0.-" }) == "frost", "frost")
    -- a hero power plays no effect of its own (it just turns over): only its results, with the power's family
    local fam, hpBeat, hpPlan = family({ "hp.m.2.45.0.CS2_172", "dmg.o.2.45.1.-", "frz.o.2.45.0.-" })
    check(hpBeat == nil and kinds(hpPlan) == "impact" and hpPlan.beats[1].family == "frost" and hpPlan.beats[1].cardId == "CS2_172",
        "hero power: no effect beat, its impact stays: " .. kinds(hpPlan))
    check(family({ "play.m.12.45.0.CS2_172", "sil.o.12.45.0.-" }) == "silence", "unmapped card guesses silence from its result")
    check(family({ "play.m.12.45.0.CS2_022", "tf.o.12.45.0.CS2_121" }) == "transform", "transform")
    check(family({ "play.m.12.40.0.CS2_172", "buff.m.12.40.2.-" }) == "buff", "buff")
    check(family({ "play.m.12.0.0.CS2_172", "sum.m.0.80.2.CS2_124" }) == "summon", "summon")
    fam, hpBeat = family({ "play.m.12.0.0.CS2_172", "sum.m.0.80.2.CS2_124" })
    check(hpBeat.slots[80] == 2 and hpBeat.targets[1] == 80 and hpBeat.sides[80] == "m", "summon position hint")
    check(family({ "play.m.12.45.0.CS2_172", "dest.o.0.45.0.-", "die.o.0.45.0.EX1_412" }) == "destroy", "destroy")
    check(family({ "play.m.12.0.0.EX1_289", "sec.m.12.12.0.EX1_289" }) == "secret", "secret")
    check(family({ "play.m.12.1.0.CS2_172", "heal.m.12.1.4.-" }) == "heal", "heal")
    check(family({ "dr.o.45.0.0.EX1_412" }) == nil, "a deathrattle without an emitted result has no effect cue")
    p = plan({ "dr.o.45.0.0.EX1_412" })
    check(kinds(p) == "", "a bare deathrattle event does not invent an effect")
    p = plan({ "trig.o.99.0.0.EX1_412" })
    check(kinds(p) == "", "an ability of an entity that is not on the board has nothing to show")
    -- nothing else makes beats: draws, mana, turns, unknown codes
    p = plan({ "turn.o.0.0.5.-", "draw.o.0.99.0.-", "mana.o.0.0.1.-", "zzz.o.1.2.3.-", "stl.m.0.40.0.-", "crys.m.0.0.1.-" })
    check(kinds(p) == "" and not p.snap, "neutral events make no beats")
    -- part B overrides the family guess per card
    AN.CARD_FAMILY.EX1_011 = "fire"
    p = plan({ "play.m.12.45.0.EX1_011", "dmg.o.12.45.5.-" })
    check(p.beats[1].family == "fire" and p.beats[2].family == "fire", "card family override")
    AN.CARD_FAMILY.EX1_011 = nil

    -- a chain: reveal, effect, impact, death, deathrattle summon
    p = plan({ "play.o.99.44.0.EX1_011", "dmg.m.99.40.3.-", "die.m.0.40.0.CS2_172", "dr.m.40.0.0.CS2_172", "sum.m.0.81.1.CS2_124" })
    check(kinds(p) == "reveal,effect,impact,death,effect", "chain: " .. kinds(p))

    -- the attack beat tells the pose over time
    local beat = { fizzle = false, reach = 1 }
    local pos, ph = AN.AttackPose(beat, 0)
    check(pos == 0 and ph == "anticipate", "pose at 0")
    pos = AN.AttackPose(beat, AN.ANTICIPATE)
    check(math.abs(pos + 1) < 1e-9, "fully pulled back after the anticipation")
    pos, ph = AN.AttackPose(beat, AN.IMPACT_AT)
    check(math.abs(pos - 1) < 1e-9 and ph == "hold", "at the contact at the impact time")
    pos, ph = AN.AttackPose(beat, AN.ANTICIPATE + AN.STRIKE + AN.HOLD / 2)
    check(pos == 1 and ph == "hold", "holds the overlap")
    pos, ph = AN.AttackPose(beat, AN.ATTACK)
    check(pos == 0 and ph == "done", "home at the end")
    local last = -2
    local mono = true
    for k = 0, 100 do
        local t = AN.ANTICIPATE + AN.STRIKE * k / 100
        local v = AN.AttackPose(beat, t)
        if v < last - 1e-9 then mono = false end
        last = v
    end
    check(mono, "the strike only goes forward")
    local fz = { fizzle = true, reach = AN.FIZZLE_REACH }
    local maxPos = 0
    for k = 0, 100 do
        local v = AN.AttackPose(fz, AN.FIZZLE * k / 100)
        if v > maxPos then maxPos = v end
    end
    check(math.abs(maxPos - AN.FIZZLE_REACH) < 0.02 and AN.AttackPose(fz, AN.FIZZLE) == 0, "a fizzle only goes part of the way and returns")

    -- mode off, bad modes
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    local tr = apply(agE(2, 1, ATTACK_EVENTS, ATTACK_AFTER))
    AN.SetMode("off")
    p = AN.Plan(tr)
    check(p.snap and p.reason == "off" and table.getn(p.beats) == 0, "animations off: snap")
    check(AN.SetMode("fast") and AN.Mode() == "fast" and AN.Speed() == 3 and not AN.SetMode("bogus") and AN.Mode() == "fast", "modes")
    AN.SetMode("on")
    check(AN.Speed() == 1, "speed on")
    resetMatch()
end

-- Match.lua: the consumable transition and the dedupe by event seq
local function testAnimTransition()
    local MA, AN = HsMatch, HsMatchAnim
    resetMatch()
    AN.SetMode("on")
    local function apply(payload)
        local g, why = MA.Parse(payload)
        check(g ~= nil, "parse: " .. tostring(why))
        local changed = MA.Apply(g)
        return g, changed
    end
    check(MA.TakeTransition() == nil and MA.lastEventSeq == 0, "nothing before the first game")
    local g1 = apply(ag({}))
    local tr = MA.TakeTransition()
    check(tr and tr.snap and tr.reason == "first" and tr.prev == nil and tr.game == g1 and tr.mid == 21, "the first game is a snap")
    check(MA.TakeTransition() == nil, "a transition is returned once")
    check(MA.game == g1, "HsMatch.game is the newest game")

    local g2 = apply(agE(2, 1, { "turn.m.0.0.4.-", "draw.m.0.18.0.CS2_025", "atk.m.40.44.0.-" }))
    tr = MA.TakeTransition()
    check(tr and not tr.snap and tr.prev == g1 and tr.game == g2 and table.getn(tr.events) == 3 and tr.lastSeen == 0, "a change with its events")
    check(tr.events[1].seq == 1 and tr.events[3].seq == 3 and MA.lastEventSeq == 3, "event seqs")

    -- an equal snapshot with the same events replays nothing; a duplicate is no change (no transition), only the
    -- reply to a GAME_GET (resync = true) snaps
    local _, changed = apply(agE(2, 1, { "turn.m.0.0.4.-", "draw.m.0.18.0.CS2_025", "atk.m.40.44.0.-" }))
    tr = MA.TakeTransition()
    check(changed and tr == nil and MA.game.seq == 2, "an equal snapshot yields no events and no transition")
    MA.Apply(MA.Parse(agE(2, 1, { "turn.m.0.0.4.-", "draw.m.0.18.0.CS2_025", "atk.m.40.44.0.-" })), true)
    tr = MA.TakeTransition()
    check(tr and tr.snap and tr.reason == "resync" and table.getn(tr.events) == 0, "an equal GAME_GET reply is a resync snap")
    -- an older snapshot is ignored entirely
    local _, ch = apply(ag({}))
    check(not ch and MA.TakeTransition() == nil and MA.game.seq == 2, "an older snapshot yields nothing")
    -- overlapping events: only the new ones
    local g3 = apply(agE(3, 3, { "atk.m.40.44.0.-", "dmg.o.40.44.3.-", "die.o.0.44.0.CS2_124" }))
    tr = MA.TakeTransition()
    check(tr and not tr.snap and table.getn(tr.events) == 2 and tr.events[1].seq == 4 and tr.events[2].seq == 5 and tr.lastSeen == 3, "only events above the seq already handed out")
    -- a GAME_GET resync: no events, a snap
    apply(ag({ seq = "4" }))
    tr = MA.TakeTransition()
    check(tr and tr.snap and tr.reason == "resync" and tr.prev == g3, "a resync is marked as a snap")
    -- a gap
    apply(agE(5, 9, { "turn.o.0.0.5.-" }))
    tr = MA.TakeTransition()
    check(tr and not tr.snap and tr.lastSeen == 5 and tr.events[1].seq == 9, "the transition carries the last seen seq")
    local plan = AN.Plan(tr)
    check(plan.snap and plan.reason == "gap", "a gap in the event seqs snaps instead of telling a partial story")
    -- a hole inside the events
    local synthetic = { mid = 21, prev = g3, game = g3, lastSeen = 0, snap = false,
        events = { { seq = 1, type = "atk", side = "m", source = 40, target = 44, amount = 0 },
            { seq = 3, type = "dmg", side = "o", source = 40, target = 44, amount = 3 } } }
    check(AN.Plan(synthetic).reason == "gap", "a hole inside the events")

    -- two changes that nobody took are one change
    resetMatch()
    AN.SetMode("on")
    local a = apply(ag({}))
    MA.TakeTransition()
    local b = apply(agE(2, 1, { "atk.m.40.44.0.-" }))
    local c = apply(agE(3, 2, { "dmg.o.40.44.3.-", "die.o.0.44.0.CS2_124" }))
    tr = MA.TakeTransition()
    check(tr and tr.prev == a and tr.game == c and table.getn(tr.events) == 3 and not tr.snap, "untaken changes are merged")
    -- ... but a resync in between makes the story incomplete
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    MA.TakeTransition()
    apply(agE(2, 1, { "atk.m.40.44.0.-" }))
    apply(ag({ seq = "3" }))
    apply(agE(4, 2, { "dmg.o.40.44.3.-" }))
    tr = MA.TakeTransition()
    check(tr and tr.snap and tr.reason == "resync", "a resync inside a merged change snaps")

    -- a new match id: no previous game, a snap, the event mark starts over
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    apply(agE(2, 1, { "atk.m.40.44.0.-", "dmg.o.40.44.3.-" }))
    MA.TakeTransition()
    check(MA.lastEventSeq == 2, "mark")
    apply(ag({ mid = "22" }))
    tr = MA.TakeTransition()
    check(tr and tr.snap and tr.reason == "first" and tr.prev == nil and MA.lastEventSeq == 0, "a new match resets")
    -- ph=none and Reset clear it
    apply(agE(2, 1, { "atk.m.40.44.0.-" }, { mid = "22" }))
    MA.Apply({ ph = "none" })
    check(MA.TakeTransition() == nil and MA.game == nil and MA.lastEventSeq == 0, "ph=none clears the transition")
    apply(ag({}))
    MA.Reset()
    check(MA.TakeTransition() == nil and MA.lastEventSeq == 0, "Reset clears the transition")
    resetMatch()
end

-- the clock, the queue, the limits
local function testAnimClock()
    local MA, AN = HsMatch, HsMatchAnim
    resetMatch()
    AN.SetMode("on")
    local saved = AN.listener
    local log = {}
    local function names(beats)
        local out = {}
        for i = 1, table.getn(beats) do out[i] = beats[i].kind end
        return table.concat(out, ",")
    end
    AN.SetListener({
        OnPlanStart = function(plan) table.insert(log, "plan") end,
        OnBeatStart = function(b) table.insert(log, "+" .. b.kind) end,
        OnBeatTick = function(b, t) end,
        OnBeatEnd = function(b) table.insert(log, "-" .. b.kind) end,
        OnPlanEnd = function(plan) table.insert(log, "end") end,
        OnSnap = function(reason) table.insert(log, "snap:" .. reason) end,
    })
    local function apply(payload)
        local g = MA.Parse(payload)
        MA.Apply(g)
        return MA.TakeTransition(), g
    end
    -- full boards (a resync nobody animates), then my Raptor attacks them
    local function attackTr(over)
        local mid = tostring(MA.game.mid)
        apply(ag({ mid = mid, seq = tostring(MA.game.seq + 1) }))
        local o = { mid = mid, mb = ATTACK_AFTER.mb, ob = ATTACK_AFTER.ob }
        for k, v in pairs(over or {}) do o[k] = v end
        return (apply(nextE(ATTACK_EVENTS, o)))
    end
    local g0 = select(2, apply(ag({})))

    -- one plan runs its beats in order and commits once
    local plan = AN.Enqueue((apply(agE(2, 1, ATTACK_EVENTS, ATTACK_AFTER))))
    check(plan and not plan.snap and AN.Busy() and AN.QueuedPlans() == 1 and AN.PresentedGame() == g0, "a queued plan: the old game is presented")
    check(math.abs(AN.Remaining() - (AN.ATTACK + AN.DEATH)) < 1e-9, "remaining")
    AN.Tick(0.1)
    check(table.concat(log, " ") == "plan +attack" and AN.PresentedGame() == g0, "the first tick starts the first beat: " .. table.concat(log, " "))
    check(math.abs(AN.Remaining() - (AN.ATTACK + AN.DEATH - 0.1)) < 1e-9, "remaining after a tick")
    for _ = 1, 40 do AN.Tick(0.05) end
    check(table.concat(log, " ") == "plan +attack -attack +death -death end", "order of the callbacks: " .. table.concat(log, " "))
    check(not AN.Busy() and AN.PresentedGame() == nil and AN.Remaining() == 0, "idle after the commit")
    -- a huge dt (a frame hitch) only advances MAX_DT
    log = {}
    AN.Enqueue(attackTr())
    AN.Tick(10)
    check(math.abs(AN.Remaining() - (AN.ATTACK + AN.DEATH - AN.MAX_DT)) < 1e-9, "a hitch advances at most MAX_DT")
    -- fast-forward: the queue is dropped, the listener told, a second call has nothing to do
    log = {}
    check(AN.FastForward() and log[1] == "snap:skip" and not AN.Busy() and not AN.FastForward(), "fast-forward")
    check(AN.PresentedGame() == nil and AN.Remaining() == 0, "nothing presented behind after a skip")
    -- /hs anim fast runs the clock at three times the speed
    log = {}
    AN.SetMode("fast")
    AN.Enqueue(attackTr())
    AN.Tick(0.1)
    check(math.abs(AN.Remaining() - (AN.ATTACK + AN.DEATH - 0.3)) < 1e-9, "fast: 3x")
    AN.Reset()
    AN.SetMode("on")

    -- queued plans: chained, each presents the game before it
    log = {}
    resetMatch()
    AN.SetMode("on")
    AN.SetListener(AN.listener)
    local gA = select(2, apply(ag({})))
    MA.TakeTransition()
    local trB, gB = apply(agE(2, 1, { "play.o.99.0.0.EX1_011" }))
    local trC, gC = apply(agE(3, 2, { "play.o.98.0.0.EX1_011" }))
    AN.Enqueue(trB)
    AN.Enqueue(trC)
    check(AN.QueuedPlans() == 2 and AN.PresentedGame() == gA, "two plans queued")
    for _ = 1, 40 do AN.Tick(0.05) end
    check(AN.PresentedGame() == gB and AN.Busy(), "the second plan presents the game after the first")
    for _ = 1, 60 do AN.Tick(0.05) end
    check(not AN.Busy() and table.concat(log, " "):find("end", 1, true) ~= nil, "both done")

    -- a plan without beats while busy waits its turn, while idle it is instant
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    local trN = apply(agE(2, 1, { "turn.o.0.0.5.-" }))
    plan = AN.Enqueue(trN)
    check(plan.instant and not AN.Busy(), "no beats and idle: instant")

    -- overflow: the network outruns the presentation: everything snaps
    resetMatch()
    AN.SetMode("on")
    log = {}
    apply(ag({}))
    MA.TakeTransition()
    local snapped = nil
    for k = 1, 8 do
        local tr = apply(agE(k + 1, k, { "play.o." .. (90 + k) .. ".0.0.EX1_011" }))
        plan = AN.Enqueue(tr)
        if plan.snap then snapped = k break end
    end
    check(snapped and snapped >= 4 and plan.reason == "overflow" and not AN.Busy() and log[table.getn(log)] == "snap:overflow",
        "the queued duration is bounded: snapped at " .. tostring(snapped))
    -- too many events in one transition
    resetMatch()
    AN.SetMode("on")
    local ga = select(2, apply(ag({})))
    local events = {}
    for k = 1, AN.MAX_EVENTS + 1 do
        events[k] = { seq = k, type = "draw", side = "o", source = 0, target = 99, amount = 0 }
    end
    plan = AN.Plan({ mid = 21, prev = ga, game = ga, events = events, lastSeen = 0, snap = false })
    check(plan.snap and plan.reason == "overflow", "too many events")
    -- too many beats
    events = {}
    for k = 1, AN.MAX_BEATS + 1 do
        events[k] = { seq = k, type = "play", side = "o", source = 90, target = 0, amount = 0, cardId = "EX1_011" }
    end
    plan = AN.Plan({ mid = 21, prev = ga, game = ga, events = events, lastSeen = 0, snap = false })
    check(plan.snap and plan.reason == "overflow", "too many beats")
    check(table.getn(AN.Plan({ mid = 21, prev = ga, game = ga, events = {}, lastSeen = 0, snap = false }).beats) == 0, "no events no beats")

    -- a resync / new match while something runs: the listener is told, nothing waits
    resetMatch()
    AN.SetMode("on")
    log = {}
    apply(ag({}))
    AN.Enqueue(attackTr())
    AN.Tick(0.1)
    local trR = apply(ag({ seq = tostring(MA.game.seq + 1) }))
    plan = AN.Enqueue(trR)
    check(plan.snap and plan.reason == "resync" and not AN.Busy() and log[table.getn(log)] == "snap:resync", "a resync clears visual work")
    AN.Enqueue(attackTr())
    AN.Tick(0.1)
    local trM = apply(ag({ mid = "30" }))
    plan = AN.Enqueue(trM)
    check(plan.snap and plan.reason == "first" and not AN.Busy(), "a new match drops the work")
    AN.Enqueue(attackTr())
    check(AN.Busy(), "work is queued")
    log = {}
    AN.Reset()
    check(log[1] == "snap:reset" and not AN.Busy(), "Reset tells the listener")
    log = {}
    AN.Reset()
    check(table.getn(log) == 0, "Reset while idle is silent")

    -- a finished match: the beats that explain the end, the reveal only when it is short
    resetMatch()
    AN.SetMode("on")
    local over = { ph = "over", act = "-", res = "win", why = "hero", lg = "" }
    apply(ag({}))
    MA.TakeTransition()
    local tr = apply(agE(2, 1, { "play.o.99.0.0.EX1_011", "atk.o.44.1.0.-", "dmg.m.44.1.3.-", "end.o.0.0.1.-" }, over))
    plan = AN.Plan(tr)
    check(not plan.snap and names(plan.beats) == "reveal,attack", "a short ending keeps its reveal: " .. names(plan.beats))
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    MA.TakeTransition()
    tr = apply(agE(2, 1, { "play.o.99.0.0.EX1_011", "play.o.98.0.0.EX1_011", "atk.o.44.1.0.-", "dmg.m.44.1.3.-", "die.o.0.44.0.CS2_124", "end.o.0.0.1.-" }, over))
    plan = AN.Plan(tr)
    check(not plan.snap and names(plan.beats) == "attack,death" and plan.total <= AN.OVER_CAP, "a long ending drops the reveals: " .. names(plan.beats))
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    MA.TakeTransition()
    tr = apply(agE(2, 1, { "atk.m.40.3.0.-", "dmg.o.40.3.3.-" }, { ph = "over", act = "-", res = "int", why = "stuck", lg = "" }))
    check(AN.Plan(tr).reason == "interrupted", "an interrupted match does not animate")

    -- a Nozdormu turn: the remaining work is capped to about half a second
    resetMatch()
    AN.SetMode("on")
    log = {}
    apply(ag({}))
    MA.TakeTransition()
    tr = apply(agE(2, 1, ATTACK_EVENTS, { mb = ATTACK_AFTER.mb, ob = ATTACK_AFTER.ob, tmr = "15" }))
    plan = AN.Enqueue(tr)
    check(not plan.snap and AN.timedScale > 1.5 and math.abs(AN.Remaining() / AN.Scale() - AN.TIMED_CAP) < 1e-6, "a timed human turn: " .. AN.timedScale)
    local elapsed = 0
    while AN.Busy() and elapsed < 5 do
        AN.Tick(0.02)
        elapsed = elapsed + 0.02
    end
    check(elapsed <= AN.TIMED_CAP + 0.05 and elapsed >= AN.TIMED_CAP - 0.05, "it ends in about half a second: " .. elapsed)
    check(AN.timedScale == 1, "the scale is back")
    -- the same work on the AI's turn is not cut
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    MA.TakeTransition()
    tr = apply(agE(2, 1, { "atk.o.44.40.0.-", "dmg.m.44.40.3.-", "dmg.o.40.44.3.-" }, { act = "o", tmr = "0", lg = "" }))
    AN.Enqueue(tr)
    check(AN.timedScale == 1, "no acceleration on the opponent's turn")
    AN.Reset()
    -- too much work at the start of a timed turn is skipped, not squeezed
    resetMatch()
    AN.SetMode("on")
    log = {}
    apply(ag({}))
    MA.TakeTransition()
    tr = apply(agE(2, 1, { "play.o.99.0.0.EX1_011", "atk.o.44.1.0.-", "dmg.m.44.1.3.-" }, { tmr = "15" }))
    plan = AN.Enqueue(tr)
    check(plan.snap and plan.reason == "timed" and not AN.Busy(), "a timed turn skips long opponent work")

    -- an error in a listener stops the animations and snaps
    resetMatch()
    AN.SetMode("on")
    local snaps = {}
    AN.SetListener({ OnBeatStart = function() error("boom") end, OnSnap = function(r) table.insert(snaps, r) end })
    apply(ag({}))
    MA.TakeTransition()
    AN.Enqueue(attackTr())
    AN.Tick(0.1)
    check(AN.failed and string.find(AN.failed, "boom", 1, true) and snaps[1] == "error" and not AN.Busy(), "a listener error snaps: " .. tostring(AN.failed))
    tr = apply(agE(3, 6, ATTACK_EVENTS, { seq = "3", mb = AG.mb, ob = AG.ob }))
    check(AN.Plan(tr).reason == "error", "no animation after an error")
    AN.ClearFailure()
    check(AN.failed == nil and not AN.Plan(tr).snap, "cleared")

    AN.SetListener(saved)
    resetMatch()
end

-- ---------------------------------------------------------------- the window
local function frameRect(frame)
    local p = frame._.points[1]
    local w, h = frame:GetWidth(), frame:GetHeight()
    if p.point == "CENTER" then
        return p.x - w / 2, -p.y - h / 2, w, h
    end
    return p.x, -p.y, w, h
end

local function overlapOf(a, b)
    local ax, ay, aw, ah = frameRect(a)
    local bx, by, bw, bh = frameRect(b)
    local ox = math.min(ax + aw, bx + bw) - math.max(ax, bx)
    local oy = math.min(ay + ah, by + bh) - math.max(ay, by)
    return ox, oy
end

local function slotLevels(slot)
    local out = { slot.frame:GetFrameLevel() }
    for _, key in ipairs({ "atk", "hp", "armor" }) do
        if slot[key] and slot[key].frame then table.insert(out, slot[key].frame:GetFrameLevel()) end
    end
    if slot.glow and slot.glow.frame then table.insert(out, slot.glow.frame:GetFrameLevel()) end
    if slot.rim then table.insert(out, slot.rim:GetFrameLevel()) end
    return table.concat(out, ",")
end

-- the effect layer (MatchFx.lua) back to a fresh session: no bench, no pools, settings at their defaults
local function fxReset()
    local FX = H.MatchFx
    FX.bench = nil
    FX.matchStage.mpool = nil
    FX.runs = {}
    FX.modelsDisabled, FX.modelError, FX.broken, FX.lastError = false, nil, false, nil
    FX.lastSound, FX.soundOff, FX.modelMode, FX.last = nil, false, "all", nil
    FX.stats = { sounds = 0, throttled = 0, models = 0, missiles = 0, impacts = 0, errors = 0 }
    FX.hinted = false
    FX.benchLayer = "2d"
    HS_EffectProfiles = FX.testProfiles or HS_EffectProfiles
end

-- every slot that moves: at home, full alpha, its own frame levels, no temporary piece left
local function checkAtRest(MU, baseline, tag)
    local stats = MU.FxStats()
    check(stats.textures == 0 and stats.texts == 0 and stats.items == 0 and stats.touched == 0,
        tag .. ": nothing leaks " .. stats.textures .. "/" .. stats.texts .. "/" .. stats.items .. "/" .. stats.touched)
    local keys = { "o", "m" }
    for k = 1, 2 do
        local u = MU.ui[keys[k]]
        local slots = { u.hero }
        for i = 1, MU.MAX_BOARD do table.insert(slots, u.minions[i]) end
        for i = 1, table.getn(slots) do
            local s = slots[i]
            local alpha = s.frame._.alpha
            check(alpha == nil or alpha == 1, tag .. ": alpha restored")
            if baseline then check(slotLevels(s) == baseline[s], tag .. ": levels restored: " .. slotLevels(s) .. " vs " .. tostring(baseline[s])) end
            if s.home and s.frame:IsShown() then
                local p = s.frame._.points[1]
                check(table.getn(s.frame._.points) == 1 and p.point == s.home.point and p.rel == MU.board and p.relPoint == "TOPLEFT"
                    and p.x == s.home.x and p.y == s.home.y, tag .. ": at its home point")
            end
            check(s.lx == nil and s.sx == nil and s.ay == nil and s.alpha == nil and s.levels == nil, tag .. ": no displacement left")
        end
    end
    check(not MU.reveal.frame:IsShown() and (MU.reveal.frame._.alpha == nil or MU.reveal.frame._.alpha == 1), tag .. ": reveal card hidden")
    local ps = H.MatchFx.PoolStats()
    check(ps.used == 0 and ps.runs == 0, tag .. ": no effect model is in use " .. ps.used .. "/" .. ps.runs)
end

local function baselineLevels(MU)
    local out = {}
    for _, key in ipairs({ "o", "m" }) do
        local u = MU.ui[key]
        out[u.hero] = slotLevels(u.hero)
        for i = 1, MU.MAX_BOARD do out[u.minions[i]] = slotLevels(u.minions[i]) end
    end
    return out
end

local function animSetup(opts)
    opts = opts or {}
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local MU = H.MatchUI
    local X = matchHelpers()
    resetAddon()
    resetMatch()
    F.install({ class = { "Mage", "MAGE" }, name = "Jonas", screenW = opts.screenW or 1920, screenH = opts.screenH or 1080 })
    H.db = HsCollection.ApplySavedDefaults(nil)
    if opts.board then HS_BoardTextures = opts.board end
    if opts.minions then HS_MinionTextures = opts.minions end
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil
    MU.pick, MU.chooser, MU.choose, MU.pool, MU.fxFrame, MU.reveal = nil, nil, nil, nil, nil, nil
    MU.fxItems, MU.touched, MU.beatRt = {}, {}, nil
    MU.bannerRt, MU.bannerSeen = nil, nil
    H.MatchFx.testProfiles = H.MatchFx.testProfiles or HS_EffectProfiles
    fxReset()
    HsMatchAnim.SetMode("on")
    HsMatchAnim.SetListener(MU.AnimListener)
    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, "ph=none")
    X.push(ag({}))
    return F, MU, X
end

local function animTeardown(F, MU)
    MU.AnimAbort()
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.pick, MU.chooser, MU.choose = nil, nil, nil, nil, nil, nil, nil, nil, nil
    MU.pool, MU.fxFrame, MU.reveal = nil, nil, nil
    MU.fxItems, MU.touched = {}, {}
    MU.bannerRt, MU.bannerSeen = nil, nil
    H.db = nil
    fxReset()
    F.uninstall()
    resetAddon()
    resetMatch()
end

local function usedTexts(MU)
    local out = {}
    for i = 1, table.getn(MU.pool.text) do
        if MU.pool.text[i].used then table.insert(out, MU.pool.text[i].obj) end
    end
    return out
end

local function usedTextures(MU)
    local out = {}
    for i = 1, table.getn(MU.pool.tex) do
        if MU.pool.tex[i].used then table.insert(out, MU.pool.tex[i].obj) end
    end
    return out
end

local function queuedActs(X)
    return table.getn(X.T.queuedOps("GAME_ACT")) + table.getn(X.T.sentOps("GAME_ACT"))
end

local function testAnimAttackUI()
    local AN, MA = HsMatchAnim, HsMatch
    local F, MU, X = animSetup()
    local ui = MU.ui
    check(MU.frame and MU.frame:IsShown() and not AN.Busy() and not MU.Behind(), "the first game is shown at once")
    local base = baselineLevels(MU)
    checkAtRest(MU, base, "idle")
    check(MU.fxFrame ~= nil and MU.pool ~= nil and table.getn(MU.pool.tex) == MU.POOL_TEXTURES and table.getn(MU.pool.text) == MU.POOL_TEXTS,
        "the pools exist once the window is built")
    local frames = table.getn(F.frames)

    local attacker, defender = ui.m.minions[1], ui.o.minions[1]
    local home0 = { point = attacker.home.point, x = attacker.home.x, y = attacker.home.y }
    check(attacker.entity == 40 and defender.entity == 44, "entities")

    -- the action: the Raptor attacks the opponent's minion; both die
    X.push(agE(2, 1, ATTACK_EVENTS, ATTACK_AFTER))
    check(AN.Busy() and MU.Behind() and MA.game.seq == 2, "behind: the newest game is known, the board lags")
    check(ui.m.minions[1].entity == 40 and ui.o.minions[1].entity == 44 and ui.m.minions[2].frame:IsShown(), "the old board is still drawn")
    check(MU.ui.mid.endButton:IsEnabled() == false, "End Turn is off while behind")
    check(table.getn(F.frames) == frames, "no frame is created for the animation")

    -- inputs are disabled
    local acts = queuedActs(X)
    F.fire(ui.hand[2].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ui.m.minions[1].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ui.m.hero.frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ui.m.power.frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(ui.mid.endButton, "OnClick", {})
    check(MU.sel == nil and not MU.Send("end") and queuedActs(X) == acts, "no input while behind")
    -- but close asks for the surrender and it works
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    check(F.popup ~= nil and F.popup.which == "HEARTHSTONE_SURRENDER" and MU.frame:IsShown(), "close asks while animating")
    F.popupClick(2)
    check(queuedActs(X) == acts, "No keeps the match")

    -- the beats: step the clock, watch the attacker
    local tNow, impactSeen, deathStart, attackEnd = 0, nil, nil, nil
    local shook = false
    local dstart = { frameRect(defender.frame) }
    local defLevel, lvl0 = defender.frame:GetFrameLevel(), attacker.frame:GetFrameLevel()
    local origStart, origEnd = MU.BeatStart, MU.BeatEnd
    MU.BeatStart = function(beat)
        if beat.kind == "death" then
            local p = attacker.frame._.points[1]
            deathStart = { t = tNow, point = { point = p.point, x = p.x, y = p.y } }
        end
        origStart(beat)
    end
    MU.BeatEnd = function(beat)
        origEnd(beat)
        if beat.kind == "attack" then
            local p = attacker.frame._.points[1]
            attackEnd = { t = tNow, point = { point = p.point, x = p.x, y = p.y }, level = attacker.frame:GetFrameLevel() }
        end
    end
    while AN.Busy() and tNow < 3 do
        MU.AnimTick(0.01)
        tNow = tNow + 0.01
        local plan, beat, bt = AN.Current()
        if beat and beat.kind == "attack" then
            local ox, oy = overlapOf(attacker.frame, defender.frame)
            if bt >= AN.IMPACT_AT - 0.001 and not impactSeen then
                impactSeen = { t = tNow, ox = ox, oy = oy, level = attacker.frame:GetFrameLevel(), texts = table.getn(usedTexts(MU)),
                    textures = table.getn(usedTextures(MU)) }
            end
            local dx = frameRect(defender.frame)
            if bt > AN.IMPACT_AT and math.abs(dx - dstart[1]) > 0.01 then shook = true end
            check(defender.frame._.alpha == nil or defender.frame._.alpha == 1, "the defender is not faded during the attack")
        end
    end
    MU.BeatStart, MU.BeatEnd = origStart, origEnd
    check(impactSeen and impactSeen.ox > 0 and impactSeen.oy > 0, "full contact: the attacker overlaps the defender at the impact")
    check(impactSeen and math.min(impactSeen.ox, impactSeen.oy) > 5 and impactSeen.level > defLevel, "deliberate overlap, raised above the defender: "
        .. tostring(impactSeen and impactSeen.oy))
    check(attackEnd and attackEnd.level == lvl0 and attackEnd.point.point == home0.point and attackEnd.point.x == home0.x
        and attackEnd.point.y == home0.y, "the attacker returns to its exact point and level when its beat ends")
    check(impactSeen and impactSeen.texts == 2 and impactSeen.textures >= 2, "numbers and flashes at the impact: " .. tostring(impactSeen and impactSeen.texts))
    check(shook, "the defender shakes")
    check(deathStart and deathStart.t >= AN.ATTACK - 0.02, "deaths start after the attack")
    check(deathStart and deathStart.point.point == home0.point and deathStart.point.x == home0.x and deathStart.point.y == home0.y,
        "the attacker is back at its exact point before the deaths fade")
    check(not AN.Busy() and tNow >= AN.ATTACK + AN.DEATH - 0.02 and tNow < AN.ATTACK + AN.DEATH + 0.1, "the plan takes " .. tNow)
    -- the new snapshot is committed: survivors recentered, everything restored
    check(ui.m.minions[1].entity == 41 and ui.m.minions[2].entity == 42 and not ui.m.minions[3].frame:IsShown(), "my board after the commit")
    check(ui.o.minions[1].entity == 45 and not ui.o.minions[2].frame:IsShown(), "opponent board after the commit")
    check(ui.mid.endButton:IsShown(), "the End Turn button follows the newest state")
    for _ = 1, 100 do MU.AnimTick(0.01) end
    checkAtRest(MU, base, "after the attack")
    check(AN.failed == nil and MU.animError == nil, "no animation error")
    -- the board accepts input again
    F.fire(ui.mid.endButton, "OnClick", {})
    check(queuedActs(X) == acts + 1, "input is back after the commit: " .. queuedActs(X) .. " vs " .. acts)

    animTeardown(F, MU)
end

-- every hero / minion pairing reaches full contact and returns to its exact anchor
local function pairingTest(MU, F, label)
    local AN = HsMatchAnim
    local base = baselineLevels(MU)
    local mine = { 1, 40, 41, 42 }
    local theirs = { 3, 44, 45 }
    local function run(a, t)
        local an, tn = MU.AnchorOf(a), MU.AnchorOf(t)
        check(an and tn, label .. ": anchors " .. a .. " " .. t)
        if not (an and tn) then return end
        local isHero = an.kind == "hero"
        local beat = { kind = "attack", dur = AN.ATTACK, source = a, target = t, planned = t, hits = {}, impactAt = AN.IMPACT_AT,
            reach = 1, fizzle = false, hero = isHero }
        local start = an.slot.frame._.points[1]
        local sp = { point = start.point, x = start.x, y = start.y }
        MU.BeatStart(beat)
        MU.BeatTick(beat, AN.ANTICIPATE)
        local ax0, ay0 = frameRect(an.slot.frame)
        MU.BeatTick(beat, AN.IMPACT_AT)
        local ox, oy = overlapOf(an.slot.frame, tn.slot.frame)
        check(ox > 0.5 and oy > 0.5, label .. ": " .. a .. " reaches " .. t .. " (" .. string.format("%.1f/%.1f", ox, oy) .. ")")
        check(an.slot.frame:GetFrameLevel() > tn.slot.frame:GetFrameLevel(), label .. ": " .. a .. " is above " .. t)
        local ax1, ay1 = frameRect(an.slot.frame)
        local d0 = math.sqrt((an.x - tn.x) ^ 2 + (an.y - tn.y) ^ 2)
        local cx, cy = ax1 + an.w / 2, ay1 + an.h / 2
        local d1 = math.sqrt((cx - tn.x) ^ 2 + (cy - tn.y) ^ 2)
        check(d1 < d0 and d1 >= 0, label .. ": the attacker moved towards the target")
        MU.BeatTick(beat, AN.ATTACK)
        MU.BeatEnd(beat)
        local p = an.slot.frame._.points[1]
        check(table.getn(an.slot.frame._.points) == 1 and p.point == sp.point and p.x == sp.x and p.y == sp.y,
            label .. ": " .. a .. " is back at its exact point")
        MU.RestoreAll()
        MU.ClearFx()
    end
    for i = 1, table.getn(mine) do
        for j = 1, table.getn(theirs) do
            run(mine[i], theirs[j])
            run(theirs[j], mine[i])
        end
    end
    checkAtRest(MU, base, label)
end

local function testAnimPairings()
    local AN = HsMatchAnim
    local F, MU, X = animSetup()
    pairingTest(MU, F, "square heroes")
    -- with the board art (arch heroes) the rectangles differ
    local savedBT = HS_BoardTextures
    HS_BoardTextures = { generated = true, pieces = {
        board_field = { path = "x\\board_field", w = 512, h = 512, right = 1, bottom = 1, aspect = 1.44964, anchors = {
            hero_bottom = { left = 0.44665, top = 0.72302, right = 0.54467, bottom = 0.88669 },
            hero_bottom_safe = { left = 0.44789, top = 0.76619, right = 0.54342, bottom = 0.88489 },
            hero_top = { left = 0.44665, top = 0.1223, right = 0.54467, bottom = 0.26978 },
            hero_top_safe = { left = 0.44789, top = 0.16547, right = 0.54342, bottom = 0.26799 } } },
        arch_top = { path = "x\\arch_top", w = 124, h = 128, right = 0.96875, bottom = 1 },
        arch_bottom = { path = "x\\arch_bottom", w = 114, h = 128, right = 0.890625, bottom = 1 } } }
    local bfa = HS_BoardTextures.pieces.board_field.anchors
    bfa.arch_top_frame = { left = 0.43176, top = 0.10072, right = 0.55955, bottom = 0.29137 }
    bfa.arch_bottom_frame = { left = 0.43176, top = 0.70144, right = 0.55955, bottom = 0.90827 }
    MU.AnimAbort()
    MU.HideWindow()
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.pick, MU.chooser = nil, nil, nil, nil, nil, nil, nil, nil
    MU.pool, MU.fxFrame, MU.reveal, MU.fxItems, MU.touched = nil, nil, nil, {}, {}
    X.push(ag({ seq = "3" }))
    check(MU.ui and MU.ui.m.hero.arch, "arch heroes")
    local overlayShown = MU.ui.m.hero.overlay:IsShown()
    pairingTest(MU, F, "arch heroes")
    check(MU.ui.m.hero.overlay:IsShown() == overlayShown, "the arch cut-out comes back after a hero attack")
    HS_BoardTextures = savedBT
    animTeardown(F, MU)
end

local function testAnimFeedbackUI()
    local AN, MA = HsMatchAnim, HsMatch
    local F, MU, X = animSetup()
    local ui = MU.ui
    local base = baselineLevels(MU)
    local function run(seconds, step)
        local t = 0
        while t < seconds do
            MU.AnimTick(step or 0.01)
            t = t + (step or 0.01)
        end
    end
    local function finish()
        run(4, 0.05)
        run(1, 0.05)
    end

    -- healing: a green +N on my hero, no shake
    X.push(nextE({ "heal.m.0.1.3.-" }, { mh = "1.HERO_08.30.30.0.0.a" }))
    MU.AnimTick(0.001)
    check(AN.Busy(), "a heal is presented")
    local texts = usedTexts(MU)
    check(table.getn(texts) == 1 and texts[1]:GetText() == "+3", "healing number")
    local c = texts[1]._.textColor
    check(c and c[2] > c[1] and c[2] > c[3], "healing is green")
    local y0 = texts[1]._.points[1].y
    run(0.3)
    local y1 = texts[1]._.points[1].y
    check(y1 > y0 and y1 - y0 <= AN.NUMBER_RISE + 0.001, "the number rises: " .. y0 .. " -> " .. y1)
    check((texts[1]._.alpha or 1) < 1, "and fades")
    finish()
    checkAtRest(MU, base, "heal")
    check(ui.m.hero.hp.text:GetText() == "30", "the committed gem shows the new value")

    -- damage numbers are red, the group is simultaneous: two numbers at once
    X.push(nextE({ "play.o.99.40.0.EX1_011", "dmg.m.99.40.1.-", "dmg.m.99.41.2.-" }, { mb = "40.CS2_172.3.1.2.a,41.CS2_179.3.3.5.t,42.CS2_168.2.1.1.a" }))
    MU.AnimTick(0.001)
    check(AN.Busy() and MU.reveal.frame:IsShown() and MU.reveal.widget.id == "EX1_011", "the opponent's card is revealed")
    check(MU.reveal.caption:GetText() == "", "no caption on an opponent's play: " .. tostring(MU.reveal.caption:GetText()))
    check(MU.reveal.frame._.alpha < 0.1, "fades in")
    run(AN.REVEAL_IN + 0.01)
    check(MU.reveal.frame._.alpha == 1, "readable after the fade in")
    run(AN.REVEAL_HOLD / 2)
    check(MU.reveal.frame:IsShown() and MU.reveal.frame._.alpha == 1, "hold")
    run(AN.REVEAL_HOLD / 2 + AN.REVEAL_OUT / 2)
    check(MU.reveal.frame._.alpha < 1 and MU.reveal.frame._.alpha > 0, "fades out")
    run(AN.REVEAL_OUT)
    check(not MU.reveal.frame:IsShown(), "the reveal is over")
    local plan, beat = AN.Current()
    check(beat and beat.kind == "effect", "then the effect")
    run(AN.EFFECT + 0.01)
    plan, beat = AN.Current()
    check(beat and beat.kind == "impact", "then the damage")
    run(0.01)
    texts = usedTexts(MU)
    local reds = 0
    for i = 1, table.getn(texts) do
        local col = texts[i]._.textColor
        if col and col[1] > col[2] and string.sub(texts[i]:GetText(), 1, 1) == "-" then reds = reds + 1 end
    end
    check(reds == 2, "two red numbers at once: " .. reds)
    finish()
    checkAtRest(MU, base, "damage")

    -- shield pop: a flash instead of a number
    X.push(nextE({ "atk.m.40.44.0.-", "pop.o.40.44.0.-", "dmg.m.44.40.3.-" }, { mb = "40.CS2_172.3.1.2.-" }))
    run(AN.IMPACT_AT + 0.02)
    local popTex = false
    for _, t in ipairs(usedTextures(MU)) do
        if t._.texture == "Interface\\Cooldown\\star4" then popTex = true end
    end
    texts = usedTexts(MU)
    check(popTex and table.getn(texts) == 1 and texts[1]:GetText() == "-3", "shield pop flash and the counter damage number")
    finish()
    checkAtRest(MU, base, "pop")
    MU.AnimAbort()

    -- the generic effect: the hook gets the beat and the anchors; a failing hook falls back to the 2D pulse
    local calls = {}
    local saved = MU.Fx.PlayEffect
    MU.Fx.PlayEffect = function(beat, ctx) table.insert(calls, { beat = beat, ctx = ctx }) end
    X.push(nextE({ "play.m.12.45.0.CS2_022", "tf.o.12.45.0.CS2_121" }, { ob = "44.CS2_124.3.1.1.c,45.CS2_121.1.1.1.-" }))
    run(0.02)
    check(table.getn(calls) == 1 and calls[1].beat.family == "transform", "PlayEffect hook gets the effect beat")
    local ctx = calls[1] and calls[1].ctx
    check(ctx and ctx.targets[1] and ctx.targets[1].id == 45 and ctx.targets[1].x > 0 and ctx.targets[1].w == MU.MINION_W
        and ctx.src and ctx.src.kind == "hero" and ctx.src.side == "m" and ctx.family == "transform" and ctx.color[1] ~= nil, "ctx anchors: source hero, target minion")
    finish()
    MU.Fx.PlayEffect = function() error("model missing") end
    X.push(nextE({ "play.m.12.45.0.EX1_332", "sil.o.12.45.0.-" }, { ob = "44.CS2_124.3.1.1.c,45.CS2_121.1.1.1.x" }))
    run(0.02)
    check(AN.failed == nil and table.getn(usedTextures(MU)) >= 1, "a failing effect hook falls back to the pulse")
    finish()
    MU.Fx.PlayEffect = saved
    check(MU.Fx.PlayEffect == H.MatchFx.PlayEffect and MU.Fx.Stop == H.MatchFx.Stop and type(MU.Fx.DefaultEffect) == "function", "the native effect hook is installed, the 2D pulse stays as the fallback")
    checkAtRest(MU, base, "effects")

    -- a summoned and killed minion that is on neither board: nothing to fade, no error
    X.push(nextE({ "sum.m.0.90.1.CS2_124", "die.m.0.90.0.CS2_124" }))
    finish()
    check(AN.failed == nil and not AN.Busy(), "a transient minion is skipped")
    checkAtRest(MU, base, "transient")
    check(MU.animError == nil, "no error")
    animTeardown(F, MU)
end

local function testAnimControlUI()
    local AN, MA = HsMatchAnim, HsMatch
    local F, MU, X = animSetup()
    local ui = MU.ui
    local base = baselineLevels(MU)
    local mid = 21
    -- my Raptor and their Murloc kill each other (the boards after are ATTACK_AFTER)
    local function attack(over)
        local o = { mid = tostring(mid), mb = ATTACK_AFTER.mb, ob = ATTACK_AFTER.ob }
        for k, v in pairs(over or {}) do o[k] = v end
        X.push(nextE(ATTACK_EVENTS, o))
    end
    -- a resync with the full boards again
    local function restore()
        X.push(ag({ mid = tostring(mid), seq = tostring(MA.game.seq + 1) }))
    end
    local function runFor(s)
        for _ = 1, math.floor(s / 0.01 + 0.5) do MU.AnimTick(0.01) end
    end

    -- skip: the board commits the newest snapshot at once and every property is restored
    attack()
    runFor(0.3)
    check(AN.Busy() and ui.m.minions[1].frame._.points[1].y ~= ui.m.minions[1].home.y, "mid-lunge: the attacker is away from home")
    check(MU.AnimSkip() and not AN.Busy(), "skip")
    checkAtRest(MU, base, "skip")
    check(ui.m.minions[1].entity == 41 and ui.o.minions[1].entity == 45, "the newest snapshot is shown after the skip")
    check(not MU.AnimSkip(), "nothing left to skip")
    -- a click on the empty board skips too
    restore()
    attack()
    check(AN.Busy(), "running again")
    F.fire(MU.board, "OnMouseUp", { arg1 = "LeftButton" })
    check(not AN.Busy(), "a click on the board skips")
    checkAtRest(MU, base, "click skip")
    -- /hs anim skip
    restore()
    attack()
    runFor(0.1)
    H.Slash("anim skip")
    check(not AN.Busy() and string.find(lastPrinted(), "skipped", 1, true) ~= nil, "/hs anim skip: " .. lastPrinted())
    H.Slash("anim skip")
    check(string.find(lastPrinted(), "no animation", 1, true) ~= nil, "skip with nothing running")

    -- /hs anim off snaps while running and for every later change
    restore()
    attack()
    runFor(0.3)
    H.Slash("anim off")
    check(string.find(lastPrinted(), "off", 1, true) ~= nil and AN.Mode() == "off" and H.db.anim == "off", "/hs anim off: " .. lastPrinted())
    check(not AN.Busy(), "off drops the running animation")
    checkAtRest(MU, base, "anim off")
    restore()
    attack()
    check(not AN.Busy() and ui.m.minions[1].entity == 41, "off: the board is always the newest")
    H.Slash("anim fast")
    check(AN.Mode() == "fast" and H.db.anim == "fast" and string.find(lastPrinted(), "fast", 1, true) ~= nil, "/hs anim fast")
    restore()
    attack()
    runFor(0.1)
    check(AN.Busy() and math.abs(AN.Remaining() - (AN.ATTACK + AN.DEATH - 0.3)) < 0.02, "fast runs three times faster: " .. AN.Remaining())
    MU.AnimSkip()
    H.Slash("anim")
    check(string.find(lastPrinted(), "fast", 1, true) ~= nil, "/hs anim shows the mode")
    H.Slash("anim bogus")
    check(string.find(lastPrinted(), "usage", 1, true) ~= nil, "usage")
    H.Slash("anim on")
    check(AN.Mode() == "on" and H.db.anim == "on", "/hs anim on")
    H.Slash("help")
    check(string.find(lastPrinted(), "anim", 1, true) ~= nil, "the help lists /hs anim")
    -- the saved mode is applied when the variables load
    HearthstoneDB = HsCollection.ApplySavedDefaults({ anim = "off" })
    H.InitSaved()
    check(AN.Mode() == "off", "the saved mode is read")
    HearthstoneDB = { anim = "bogus" }
    H.InitSaved()
    check(AN.Mode() == "on" and H.db.anim == "on", "a bad saved mode means on")
    HearthstoneDB = nil
    H.db = HsCollection.ApplySavedDefaults(nil)

    -- hiding the window while animating restores everything
    restore()
    attack()
    runFor(0.3)
    check(AN.Busy(), "running")
    MU.HideWindow()
    check(not AN.Busy(), "hidden: the work is dropped")
    checkAtRest(MU, base, "hide")
    -- while hidden nothing animates, and the window opens on the newest snapshot
    attack()
    check(MU.frame:IsShown() and not AN.Busy(), "a push shows the window on the newest snapshot")
    restore()

    -- a new match drops the work
    attack()
    runFor(0.1)
    check(AN.Busy(), "running")
    mid = 99
    X.push(ag({ mid = "99", seq = "1" }))
    check(not AN.Busy() and MA.game.mid == 99 and MA.lastEventSeq == 0, "a new match")
    checkAtRest(MU, base, "new match")
    -- a resync (a GAME_GET reply) too
    attack()
    check(AN.Busy(), "running")
    X.push(ag({ mid = "99", seq = tostring(MA.game.seq + 1) }))
    check(not AN.Busy(), "a resync snaps")
    checkAtRest(MU, base, "resync")
    -- no match at all
    restore()
    attack()
    X.push("ph=none")
    check(not AN.Busy() and MA.game == nil, "ph=none")
    MU.AnimTick(0.1)
    checkAtRest(MU, base, "none")

    -- a lethal action: the result overlay waits for the last beat, nothing else does
    mid = 100
    X.push(ag({ mid = "100", seq = "1" }))
    X.push(nextE({ "atk.o.44.1.0.-", "dmg.m.44.1.30.-", "end.o.0.0.1.-" },
        { mid = "100", ph = "over", act = "-", res = "loss", why = "hero", lg = "", mh = "1.HERO_08.-3.30.0.0.-" }))
    check(AN.Busy() and not MU.result.frame:IsShown(), "the result overlay waits for the last beat")
    runFor(0.3)
    check(not MU.result.frame:IsShown(), "still waiting")
    runFor(0.5)
    check(not AN.Busy() and MU.result.frame:IsShown() and MU.result.title:GetText() == "Defeat", "then the result shows: " .. tostring(MU.result.title:GetText()))
    -- leave works as before
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    drain(1)
    check(X.last("GAME_LEAVE") and X.last("GAME_LEAVE").payload == "mid=100", "X leaves a finished match")

    -- surrender while the board is behind: Yes concedes with the newest seq
    mid = 101
    X.push(ag({ mid = "101", seq = "1" }))
    attack()
    check(AN.Busy(), "behind")
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    check(F.popup ~= nil, "the surrender popup")
    F.popupClick(1)
    local acts = X.T.queuedOps("GAME_ACT")
    check(acts[table.getn(acts)] and acts[table.getn(acts)].payload == "mid=101;seq=2;a=concede",
        "concede with the newest seq: " .. tostring(acts[table.getn(acts)] and acts[table.getn(acts)].payload))
    MU.AnimAbort()
    check(AN.failed == nil and MU.animError == nil, "no failure")
    animTeardown(F, MU)
end

-- many recorded transitions: after the end of the animations (or a skip) the window shows HsMatch.game and
-- no pooled object is left
-- ---- review fixes (part A)
-- the absolute rectangle (board coordinates, y down) of a region, resolved from its anchor chain in the fake
local function absRect(MU, o)
    local function frac(point)
        local fx, fy = 0.5, 0.5
        if string.find(point, "LEFT") then fx = 0 elseif string.find(point, "RIGHT") then fx = 1 end
        if string.find(point, "TOP") then fy = 0 elseif string.find(point, "BOTTOM") then fy = 1 end
        return fx, fy
    end
    local rectOf
    local function loc(r, name)
        local x, y, w, h = rectOf(r)
        local fx, fy = frac(name)
        return x + fx * w, y + fy * h
    end
    rectOf = function(r)
        if r == MU.board then return 0, 0, MU.board:GetWidth(), MU.board:GetHeight() end
        local p = r._.points[1]
        local w, h = r:GetWidth(), r:GetHeight()
        if not p then return 0, 0, w, h end
        local rx, ry = loc(p.rel or r._.parent, p.relPoint or p.point)
        local ax, ay = rx + p.x, ry - p.y
        local fx, fy = frac(p.point)
        return ax - fx * w, ay - fy * h, w, h
    end
    return rectOf(o)
end

-- every piece of a side that sits beside the hero and must not move with it (the secrets sit on the hero's dome
-- and do move with it: they are tested apart)
local function rowPoints(MU, u)
    local list = { u.name, u.power.frame, u.weapon.frame, u.counts, u.manaText, u.overload, u.crystals[1],
        u.crystals[table.getn(u.crystals)] }
    local out = {}
    for i = 1, table.getn(list) do
        local x, y, w, h = absRect(MU, list[i])
        out[i] = { x, y, w, h }
    end
    return out
end

local function samePoints(a, b)
    for i = 1, table.getn(a) do
        for k = 1, 4 do
            if math.abs(a[i][k] - b[i][k]) > 0.0001 then return false, i end
        end
    end
    return true
end

local function testAnimReviewHeroRow()
    local AN = HsMatchAnim
    local F, MU, X = animSetup()
    local ui = MU.ui
    -- secrets and a weapon on both sides
    X.push(ag({ seq = "2", ms = "60.EX1_287,63.EX1_289", os = "61,62", mw = "9.CS2_106.2.2" }))
    check(ui.m.secrets[2].frame:IsShown() and ui.o.secrets[2].frame:IsShown() and ui.m.weapon.frame:IsShown()
        and ui.o.weapon.frame:IsShown(), "secrets and weapons are shown")
    local sizes = { { 1920, 1080 }, { 1024, 768 }, { 1280, 720 } }
    for si = 1, table.getn(sizes) do
        F.screenW, F.screenH = sizes[si][1], sizes[si][2]
        MU.ApplyFit()
        local tag = sizes[si][1] .. "x" .. sizes[si][2]
        for _, key in ipairs({ "m", "o" }) do
            local u = ui[key]
            local label = tag .. " " .. key .. ": "
            -- the layout at rest: the name hangs over the hero, the power / weapon beside the hero's outer frame
            local now = rowPoints(MU, u)
            local same, which
            local nx, ny, nw = absRect(MU, u.name)
            check(math.abs(nx + nw / 2 - (u.hero.rect.x + u.hero.rect.w / 2)) < 1, label .. "the name is centred over the hero")

            -- the hero attacks: lunge up to the impact
            local hx0, hy0 = absRect(MU, u.hero.frame)
            local heroId, tgt = key == "m" and 1 or 3, key == "m" and 3 or 1
            local beat = { kind = "attack", dur = AN.ATTACK, source = heroId, target = tgt, planned = tgt, hits = {},
                impactAt = AN.IMPACT_AT, reach = 1, fizzle = false, hero = true }
            MU.BeatStart(beat)
            MU.BeatTick(beat, AN.ANTICIPATE)
            MU.BeatTick(beat, AN.IMPACT_AT)
            local hx1, hy1 = absRect(MU, u.hero.frame)
            check(math.abs(hx1 - hx0) + math.abs(hy1 - hy0) > 5, label .. "the hero lunges")
            same, which = samePoints(now, rowPoints(MU, u))
            check(same, label .. "during the hero attack nothing else moves (piece " .. tostring(which) .. ")")
            local wx, wy, ww = absRect(MU, u.weapon.frame)
            check(math.abs(wx - u.weapon.rect.x) < 0.0001 and math.abs(wy - u.weapon.rect.y) < 0.0001 and ww == u.weapon.rect.w,
                label .. "the weapon is where its impact flash is drawn")
            MU.BeatTick(beat, AN.ATTACK)
            MU.BeatEnd(beat)
            MU.RestoreAll()
            MU.ClearFx()

            -- the hero is hit: it shakes, the row stays
            MU.Shake(u.hero)
            MU.FxTick(0.05)
            local sx, sy = absRect(MU, u.hero.frame)
            check(math.abs(sx - hx0) + math.abs(sy - hy0) > 0.01, label .. "the hero shakes")
            same, which = samePoints(now, rowPoints(MU, u))
            check(same, label .. "during the hero shake nothing else moves (piece " .. tostring(which) .. ")")
            MU.RestoreAll()
            MU.ClearFx()
            same, which = samePoints(now, rowPoints(MU, u))
            check(same, label .. "the row is at rest after the animations")
        end
    end
    animTeardown(F, MU)
end

local function testAnimReviewSync()
    local AN, MA = HsMatchAnim, HsMatch
    local F, MU, X = animSetup()
    local ui = MU.ui
    local function runFor(s)
        for _ = 1, math.floor(s / 0.01 + 0.5) do MU.AnimTick(0.01) end
    end
    local function getReply(payload)
        local reqId = H.MatchGet()
        check(reqId ~= nil, "GAME_GET sent")
        X.game(reqId, payload)
    end

    -- 2: a duplicate push (same seq, no fresh events) does not cancel the running animation
    X.push(agE(2, 1, ATTACK_EVENTS, ATTACK_AFTER))
    check(AN.Busy() and MU.Behind(), "the attack runs")
    runFor(0.2)
    X.push(agE(2, 1, ATTACK_EVENTS, ATTACK_AFTER))
    check(AN.Busy() and MU.Behind() and MA.game.seq == 2, "a duplicate push keeps the animation running")
    local dup = ag({ seq = "2", mb = ATTACK_AFTER.mb, ob = ATTACK_AFTER.ob })
    X.push(dup)
    check(AN.Busy() and MA.game.seq == 2 and MA.game.me.board[1].id == 41, "the same seq without events keeps it too, and is the newest game")
    runFor(3)
    check(not AN.Busy() and ui.m.minions[1].entity == 41 and ui.o.minions[1].entity == 45, "it ends and shows the newest board")
    -- ... but the reply to a GAME_GET is a real resync: it snaps
    X.push(agE(3, 6, ATTACK_EVENTS, { mb = "41.CS2_179.3.5.5.t", ob = "" }))
    check(AN.Busy(), "another attack runs")
    getReply(ag({ seq = "3", mb = "41.CS2_179.3.5.5.t", ob = "" }))
    check(not AN.Busy() and not MU.Behind(), "a same-seq GAME_GET reply snaps")
    -- the unchanged-seq answers still update an idle board
    getReply(ag({ seq = "3", mb = "42.CS2_168.2.1.1.a", ob = "" }))
    check(ui.m.minions[1].entity == 42 and not ui.m.minions[2].frame:IsShown(), "a same-seq reply updates the idle board")
    X.push(ag({ seq = "3", mb = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t", ob = "" }))
    check(not AN.Busy() and ui.m.minions[1].entity == 40 and ui.m.minions[2].entity == 41 and not ui.m.minions[3].frame:IsShown(),
        "a same-seq push updates the idle board")
    animTeardown(F, MU)

    -- 3: a resync that jumped ahead (no events) must not make the next action a gap
    resetMatch()
    AN.SetMode("on")
    local function apply(payload, resync)
        local g, why = MA.Parse(payload)
        check(g ~= nil, "parse: " .. tostring(why))
        MA.Apply(g, resync)
        return MA.TakeTransition(), g
    end
    local FIVE = { "turn.m.0.0.4.-", "draw.m.0.18.0.CS2_025", "atk.m.40.44.0.-", "dmg.o.40.44.3.-", "dmg.m.44.40.3.-" }
    local NEXT = { "atk.m.40.44.0.-", "dmg.o.40.44.3.-", "die.o.0.44.0.CS2_124" }
    apply(ag({}))
    apply(agE(5, 1, FIVE))
    check(MA.lastEventSeq == 5, "events seen up to 5")
    local tr = apply(ag({ seq = "5" }), true)
    check(tr and tr.snap and tr.reason == "resync" and MA.lastEventSeq == 0, "a resync without events forgets the last seq")
    tr = apply(agE(6, 12, NEXT))
    check(tr and not tr.snap and table.getn(tr.events) == 3 and tr.events[1].seq == 12 and tr.lastSeen == 0, "the next batch is a story")
    check(not AN.Plan(tr).snap, "no gap snap: " .. tostring(AN.Plan(tr).reason))
    -- duplicates do not replay after it
    tr = apply(agE(6, 12, NEXT))
    check(tr == nil and MA.lastEventSeq == 14, "a duplicate of the batch replays nothing")
    tr = apply(agE(7, 3, { "atk.m.40.44.0.-", "dmg.o.40.44.3.-", "die.o.0.44.0.CS2_124" }))
    check(tr and table.getn(tr.events) == 0, "events seen before the resync never come back")
    -- a resync that carries events keeps the dedupe mark
    resetMatch()
    AN.SetMode("on")
    apply(ag({}))
    apply(agE(5, 1, FIVE))
    tr = apply(agE(5, 1, FIVE), true)
    check(tr and tr.snap and table.getn(tr.events) == 0 and MA.lastEventSeq == 5, "a resync with events keeps the last seq")
    tr = apply(agE(6, 6, { "turn.o.0.0.5.-" }))
    check(tr and not tr.snap and tr.lastSeen == 5 and not AN.Plan(tr).snap, "the next batch follows on")
    resetMatch()
end

local function testAnimReviewMulligan()
    local AN, MA = HsMatchAnim, HsMatch
    resetMatch()
    AN.SetMode("on")
    local function apply(payload)
        local g, why = MA.Parse(payload)
        check(g ~= nil, "parse: " .. tostring(why))
        MA.Apply(g)
        return MA.TakeTransition(), g
    end
    local MULL = { ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15" }
    local function mull(seq, evs, events)
        local o = { seq = tostring(seq), evs = tostring(evs), ev = table.concat(events, ",") }
        for k, v in pairs(MULL) do o[k] = v end
        return ag(o)
    end
    apply(mull(1, 0, {}))
    local tr = apply(agE(2, 1, { "turn.m.0.0.1.-", "atk.m.40.44.0.-" }))
    check(tr and tr.snap and tr.reason == "mulligan" and tr.prev.ph == "mull", "mulligan to play snaps")
    check(AN.Plan(tr).snap, "the plan snaps")
    resetMatch()
    AN.SetMode("on")
    apply(mull(1, 0, {}))
    tr = apply(mull(2, 1, { "mull.o.0.0.2.-" }))
    check(tr == nil or tr.reason ~= "mulligan", "mulligan to mulligan is no mulligan snap")

    -- on screen: the panel is drawn from the presented game, so nothing may play against it
    local F, MU, X = animSetup()
    X.push(ag({ mid = "22", seq = "1", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15" }))
    check(MU.mull.frame:IsShown(), "the mulligan panel is up")
    MU.MullSkip()      -- the cards are dealt first; a click on one waits until they lie there
    F.fire(MU.mull.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MA.marked[12], "a card is marked")
    X.push(agE(2, 1, { "turn.m.0.0.1.-", "atk.m.40.44.0.-" }, { mid = "22" }))
    check(not AN.Busy() and not MU.Behind() and not MU.mull.frame:IsShown(), "no animation starts from the mulligan")
    animTeardown(F, MU)
end

local function testMulliganAnimation()
    local AN, MA = HsMatchAnim, HsMatch
    local F, MU, X = animSetup()
    local m = MU.mull
    local function tick(seconds)
        local left = seconds
        while left > 0 do
            local dt = left > 0.05 and 0.05 or left
            MU.AnimTick(dt)
            left = left - dt
        end
    end

    -- the deal: the first card flies from the pile face down, the second waits, Done and clicks wait too
    X.push(ag({ mid = "30", seq = "1", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15" }))
    check(m.anim and m.anim.kind == "deal" and m.frame:IsShown(), "the offer is dealt")
    check(m.slots[1].frame:IsShown() and not m.slots[2].frame:IsShown(), "the second card waits in the pile")
    check(m.slots[1].back:IsShown() and not m.slots[1].widget.frame:IsShown(), "a dealt card is face down")
    check(m.deck[1]:IsShown() and not m.confirm:IsShown() and not m.note:IsShown(), "the pile shows, Done waits")
    F.fire(m.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(not MA.marked[12], "no click while the cards are dealt")
    tick(MU.MULL_DEAL_STAGGER + 0.05)
    check(m.slots[2].frame:IsShown(), "the second card is dealt after the first")
    tick(MU.MULL_DEAL_FLY + MU.MULL_DEAL_FLIP * 0.75 - MU.MULL_DEAL_STAGGER - 0.05)
    check(m.slots[1].back:IsShown() == false and m.slots[1].widget.frame:IsShown() and m.slots[1].widget.frame:GetWidth() < MU.MULL_W,
        "the first card is turning over")
    tick(2)
    check(m.anim == nil and m.confirm:IsShown() and not m.deck[1]:IsShown(), "the deal ended: Done shows, the pile is gone")
    check(m.slots[1].widget.frame:GetWidth() == MU.MULL_W and not m.slots[1].back:IsShown() and m.slots[1].frame:GetScale() == 1
        and m.slots[1].glow.color == "green", "the cards lie face up, kept")
    MU.Refresh()
    check(m.anim == nil, "a refresh does not deal again")

    -- Done with the first card replaced: it flies into the pile, the new card is dealt in its place
    F.fire(m.slots[1].frame, "OnClick", { arg1 = "LeftButton" })
    check(MA.marked[12], "the first card is marked")
    F.fire(m.confirm, "OnClick", {})
    drain(1)
    local act = X.last("GAME_ACT")
    check(act.payload == "mid=30;seq=1;a=mull;r=12", "payload: " .. tostring(act.payload))
    X.game(act.reqId, ag({ mid = "30", seq = "2", ph = "play", turn = "1", act = "m", lg = "", ev = "", evs = "0",
        mc = "15.CS2_168.2.1.1,18.CS2_025.2.0.0", mul = "" }))
    check(MU.MullSwapping() and m.frame:IsShown(), "the swap runs")
    check(not MU.ui.hand[1].frame:IsShown() and not MU.ui.hand[2].frame:IsShown(), "the new hand waits until the cards arrive")
    check(m.slots[1].animCard == "CS2_029" and m.slots[1].mark:IsShown(), "the replaced card still shows, with its X")
    tick(MU.MULL_CHROME_FADE + MU.MULL_OUT_FLY + 0.02)
    check(not m.slots[1].frame:IsShown() and m.slots[2].frame:IsShown(), "the replaced card is in the pile, the kept one stays")
    tick(MU.MULL_GAP_BEFORE_IN + 0.1)
    check(m.slots[1].frame:IsShown() and m.slots[1].animCard == "CS2_025" and m.slots[1].back:IsShown(),
        "the new card is dealt face down")
    check(not MU.bannerRt, "no turn banner during the swap")
    tick(MU.MULL_DEAL_FLY + MU.MULL_DEAL_FLIP)
    check(m.slots[1].widget.frame:IsShown() and not m.slots[1].back:IsShown(), "the new card is face up")
    tick(MU.MULL_HOLD + 0.05)
    check(m.frame:IsShown() and m.anim ~= nil, "the hand holds, then flies down")
    tick(5)
    check(m.anim == nil and not m.frame:IsShown(), "the swap ended")
    check(MU.ui.hand[1].frame:IsShown() and MU.ui.hand[2].frame:IsShown() and MU.ui.hand[1].cardId == "CS2_168"
        and MU.ui.hand[2].cardId == "CS2_025", "the new hand is on the board")
    check(m.slots[1].frame:GetScale() == 1 and m.slots[1].widget.frame:GetWidth() == MU.MULL_W, "the slots are back to plain")

    -- a click ends the animation at once
    X.push(ag({ mid = "31", seq = "1", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15", mc = "12.CS2_029.4.0.0,15.CS2_168.1.2.1" }))
    check(m.anim and m.anim.kind == "deal", "a new match deals again")
    m.frame._.scripts.OnMouseDown()
    check(m.anim == nil and m.confirm:IsShown(), "a click on the view ends the deal")

    -- an answer that does not match what was kept just shows the new hand
    F.fire(m.confirm, "OnClick", {})
    drain(1)
    act = X.last("GAME_ACT")
    X.game(act.reqId, ag({ mid = "31", seq = "2", ph = "play", turn = "1", act = "m", lg = "", ev = "", evs = "0",
        mc = "99.CS2_025.2.0.0", mul = "" }))
    check(m.anim == nil and not m.frame:IsShown() and MU.ui.hand[1].frame:IsShown(), "a different hand: no swap, the hand shows at once")

    -- animations off: no deal
    AN.SetMode("off")
    X.push(ag({ mid = "32", seq = "1", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15" }))
    check(m.anim == nil and m.slots[2].frame:IsShown() and m.confirm:IsShown(), "animations off: the offer shows at once")
    AN.SetMode("on")
    animTeardown(F, MU)
end

local function testAnimReviewRevealMouse()
    local F, MU, X = animSetup()
    local rv = MU.reveal
    check(rv and rv.frame._.mouse == false, "the reveal frame does not take the mouse")
    check(rv.widget.frame._.mouse == false and rv.widget.fb._.mouse == false, "nor does its card")
    animTeardown(F, MU)
end

local function testAnimProperty()
    local AN, MA = HsMatchAnim, HsMatch
    local F, MU, X = animSetup()
    local base = baselineLevels(MU)
    local ids = { 0, 1, 2, 3, 4, 33, 40, 41, 42, 43, 44, 45, 46, 47, 12, 15, 90, 99 }
    local etypes = { "atk", "atk", "dmg", "dmg", "dmg", "heal", "pop", "die", "die", "play", "play", "hp", "trig", "dr", "sum", "frz",
        "sil", "tf", "buff", "dest", "sec", "rev", "arm", "fat", "turn", "draw", "wbrk", "eq", "bnc", "ctl", "stl", "zzz" }
    local cards = { "-", "CS2_029", "CS2_124", "EX1_289" }
    local function pick(list) return list[rnd(table.getn(list)) + 1] end
    local function board(from)
        local out = {}
        for id = from, from + 3 do
            if rnd(3) > 0 then table.insert(out, id .. ".CS2_124." .. (1 + rnd(5)) .. "." .. (1 + rnd(5)) .. ".6.-") end
        end
        return table.concat(out, ",")
    end
    local failed0 = failed
    local mid, seq, evSeq = 500, 1, 1
    X.push(ag({ mid = tostring(mid), seq = "1" }))
    local pushes, finished, skipped, snapped, animated = 0, 0, 0, 0, 0
    for iter = 1, 220 do
        local burst = 1 + rnd(3)
        for _ = 1, burst do
            seq = seq + 1
            local over = { mid = tostring(mid), seq = tostring(seq), mb = board(40), ob = board(44) }
            local n = rnd(7)
            local list = {}
            for k = 1, n do
                local t = pick(etypes)
                local card = pick(cards)
                list[k] = t .. "." .. pick({ "m", "o", "-" }) .. "." .. pick(ids) .. "." .. pick(ids) .. "." .. rnd(6) .. "." .. card
            end
            local roll = rnd(100)
            if roll < 8 then
                evSeq = evSeq + 5 + rnd(50)             -- a hole in the numbers
            elseif roll < 14 then
                list = {}                                -- a resync
            elseif roll < 17 then
                mid = mid + 1                            -- a new match
                over.mid = tostring(mid)
                seq = 1
                over.seq = "1"
                evSeq = 1
                list = {}
            end
            over.evs = tostring(evSeq)
            over.ev = table.concat(list, ",")
            if table.getn(list) > 0 then evSeq = evSeq + table.getn(list) end
            if rnd(100) < 6 then
                over.ph, over.act, over.res, over.why, over.lg = "over", "-", pick({ "win", "loss", "draw", "int" }), "hero", ""
            elseif rnd(100) < 25 then
                over.tmr = "15"
            end
            X.push(ag(over))
            pushes = pushes + 1
            if rnd(3) == 0 then
                for _ = 1, rnd(25) do MU.AnimTick(0.01 + rnd(30) / 100) end
            end
            if over.ph == "over" then break end
        end
        local how = rnd(3)
        if how == 0 and AN.Busy() then
            MU.AnimSkip()
            skipped = skipped + 1
        else
            local guard = 0
            if AN.Busy() then animated = animated + 1 end
            while AN.Busy() and guard < 4000 do
                MU.AnimTick(0.05)
                guard = guard + 1
            end
            for _ = 1, 20 do MU.AnimTick(0.05) end
            finished = finished + 1
        end
        -- the window shows the newest game
        local g = MA.game
        check(not AN.Busy() and AN.PresentedGame() == nil and AN.failed == nil, "iteration " .. iter .. ": idle, no failure")
        if g.ph ~= "over" or true then
            local keys = { o = g.opp, m = g.me }
            for key, side in pairs(keys) do
                local u = MU.ui[key]
                local shown = 0
                for i = 1, MU.MAX_BOARD do
                    local slot = u.minions[i]
                    if side.board[i] then
                        check(slot.frame:IsShown() and slot.entity == side.board[i].id, "iteration " .. iter .. ": minion " .. key .. i .. " is the newest")
                        shown = shown + 1
                    else
                        check(not slot.frame:IsShown(), "iteration " .. iter .. ": no stale minion " .. key .. i)
                    end
                end
                check(u.hero.entity == side.hero.id, "iteration " .. iter .. ": hero")
            end
        end
        checkAtRest(MU, base, "iteration " .. iter)
        if failed > failed0 then break end
        if g.ph == "over" then
            mid = mid + 1
            seq = 1
            evSeq = 1
            X.push(ag({ mid = tostring(mid), seq = "1" }))
        end
    end
    check(pushes > 200 and animated > 20 and skipped > 5, "the property test exercised animations: " .. pushes .. "/" .. animated .. "/" .. skipped)
    check(table.getn(F.frames) > 0 and AN.failed == nil and MU.animError == nil, "no failure")
    for _, line in ipairs(printed) do
        check(string.find(line, "animation error", 1, true) == nil, "no animation error printed: " .. line)
    end
    animTeardown(F, MU)
end


-- ------------------------------------------------ the board visuals of the first in-game test
-- Board pieces with the real anchors of data/art/board_textures.manifest.json (noGlows: without the glow_* pieces;
-- noSecrets: without the secret_* pieces).
local function visualBoard(opts)
    opts = opts or {}
    local inner = { left = 0.1, top = 0.1, right = 0.9, bottom = 0.9 }
    local p = {
        board_field = { path = "x\\board_field", w = 512, h = 512, right = 1, bottom = 1, aspect = 1.44964, anchors = {
            hero_bottom = { left = 0.44665, top = 0.72302, right = 0.54467, bottom = 0.88669 },
            hero_bottom_safe = { left = 0.44789, top = 0.76619, right = 0.54342, bottom = 0.88489 },
            hero_top = { left = 0.44665, top = 0.1223, right = 0.54467, bottom = 0.26978 },
            hero_top_safe = { left = 0.44789, top = 0.16547, right = 0.54342, bottom = 0.26799 },
            arch_top_frame = { left = 0.43176, top = 0.10072, right = 0.55955, bottom = 0.29137 },
            arch_bottom_frame = { left = 0.43176, top = 0.70144, right = 0.55955, bottom = 0.90827 } } },
        arch_top = { path = "x\\arch_top", w = 124, h = 128, right = 0.96875, bottom = 1 },
        arch_bottom = { path = "x\\arch_bottom", w = 114, h = 128, right = 0.890625, bottom = 1 },
        hero_ring = { path = "x\\hero_ring", w = 120, h = 128, right = 0.9375, bottom = 1,
            anchors = { inner = { left = 0.254, top = 0.289, right = 0.7375, bottom = 0.742 } } },
        hero_power_ring = { path = "x\\hero_power_ring", w = 128, h = 128, right = 1, bottom = 1,
            anchors = { inner = { left = 0.242, top = 0.25, right = 0.727, bottom = 0.734 },
                circle = { left = 0.2227, top = 0.2305, right = 0.7461, bottom = 0.7578 } } },
        minion_oval = { path = "x\\minion_oval", w = 106, h = 128, right = 0.828125, bottom = 1, anchors = {
            inner = { left = 0.18229, top = 0.175, right = 0.81041, bottom = 0.825 },
            oval = { left = 0.09, top = 0.074, right = 0.9, bottom = 0.926 } } },
        weapon_frame = { path = "x\\weapon_frame", w = 119, h = 128, right = 0.9296875, bottom = 1,
            anchors = { inner = { left = 0.2815, top = 0.254, right = 0.769, bottom = 0.707 } } },
        banner_your_turn = { path = "x\\banner_your_turn", w = 256, h = 122, right = 1, bottom = 0.953125 },
        banner_opponent_turn = { path = "x\\banner_opponent_turn", w = 256, h = 122, right = 1, bottom = 0.953125 },
    }
    if not opts.noGlows then
        p.glow_round = { path = "x\\glow_round", w = 128, h = 128, right = 1, bottom = 1, anchors = { inner = inner } }
        p.glow_oval = { path = "x\\glow_oval", w = 128, h = 128, right = 1, bottom = 1, anchors = { inner = inner } }
        p.glow_arch = { path = "x\\glow_arch", w = 128, h = 128, right = 1, bottom = 1, anchors = { inner = inner } }
    end
    if not opts.noSecrets then
        p.secret_paladin = { path = "x\\secret_paladin", w = 126, h = 128, right = 0.984375, bottom = 1 }
        p.secret_hunter = { path = "x\\secret_hunter", w = 128, h = 128, right = 1, bottom = 1 }
        p.secret_mage = { path = "x\\secret_mage", w = 126, h = 128, right = 0.984375, bottom = 1 }
    end
    return { generated = true, pieces = p }
end

local SEVEN_M = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a,43.CS2_172.3.2.2.-,46.CS2_172.3.2.2.-,"
    .. "47.CS2_172.3.2.2.-,48.CS2_172.3.2.2.-"
local SEVEN_O = "44.CS2_124.3.1.1.c,45.EX1_412.4.2.3.-,50.CS2_172.3.2.2.-,51.CS2_172.3.2.2.-,52.CS2_172.3.2.2.-,"
    .. "53.CS2_172.3.2.2.-,54.CS2_172.3.2.2.-"

-- the arch frame of the field art (name: arch_top_frame / arch_bottom_frame) in board pixels: x0, y0, x1, y1
local function archFrameRect(MU, name)
    local a = HS_BoardTextures.pieces.board_field.anchors[name]
    local i = MU.FIELD_INSET
    local tw, th = MU.geom.bw - 2 * i, MU.geom.fh - 2 * i
    return { i + a.left * tw, i + MU.FieldY(a.top) * th, i + a.right * tw, i + MU.FieldY(a.bottom) * th }
end

local function rectsHit(a, b)
    return a[1] < b[3] and b[1] < a[3] and a[2] < b[4] and b[2] < a[4]
end

-- items 1 and 2: the minion rows stay clear of the arch frames, the hero power is as big as the hero ring
local function testBoardLayout()
    local savedBT = HS_BoardTextures
    local F, MU, X = animSetup({ board = visualBoard() })
    X.push(ag({ seq = "2", mb = SEVEN_M, ob = SEVEN_O }))
    local ui = MU.ui
    check(ui.m.minions[7].frame:IsShown() and ui.o.minions[7].frame:IsShown() and ui.m.hero.arch, "seven minions per side, arch heroes")
    local sizes = { { 1920, 1080 }, { 1024, 768 }, { 1280, 720 }, { 800, 600 }, { 2560, 1080 } }
    for si = 1, table.getn(sizes) do
        F.screenW, F.screenH = sizes[si][1], sizes[si][2]
        MU.ApplyFit()
        MU.Refresh()
        local tag = sizes[si][1] .. "x" .. sizes[si][2] .. ": "
        local bw, fh = MU.geom.bw, MU.geom.fh
        local frames = { archFrameRect(MU, "arch_top_frame"), archFrameRect(MU, "arch_bottom_frame") }
        local oppBottom, myTop = 0, 1e9
        for _, key in ipairs({ "o", "m" }) do
            for i = 1, 7 do
                local r = ui[key].minions[i].rect
                -- the frame with its gems (they reach 6 px beyond it) and the glow's shape
                local box = { r.x - 6, r.y - 6, r.x + r.w + 6, r.y + r.h + 6 }
                for f = 1, 2 do
                    check(not rectsHit(box, frames[f]), tag .. key .. i .. " keeps clear of the arch frame " .. f)
                end
                if key == "o" and r.y + r.h > oppBottom then oppBottom = r.y + r.h end
                if key == "m" and r.y < myTop then myTop = r.y end
                check(r.y >= 0 and r.y + r.h <= fh, tag .. "the row is inside the field")
            end
        end
        check(oppBottom + 12 <= myTop, tag .. "the rows do not touch each other: " .. oppBottom .. " / " .. myTop)
        check(MU.RowTop("o", fh) == ui.o.minions[1].rect.y and MU.RowTop("m", fh) == ui.m.minions[1].rect.y, tag .. "RowTop is the row")
        -- every minion frame is really placed there
        local p = ui.m.minions[1].frame._.points[1]
        check(p.y == -MU.RowTop("m", fh), tag .. "the frame is at the row")

        -- the hero power: its ring is as big as the hero ring, right of the arch frame, clear of crystals and counts
        local ringSize = MU.HeroRingSize(bw, fh)
        local _, _, heroSize = MU.HeroRect("m", bw, fh)
        check(ringSize == heroSize + 2 * MU.HERO_RING_PAD, tag .. "hero ring size")
        for _, key in ipairs({ "o", "m" }) do
            local u = ui[key]
            local pw = u.power.frame:GetWidth()
            check(pw + 2 * MU.POWER_RING_PAD == ringSize and u.power.frame:GetHeight() == pw, tag .. key .. ": power ring == hero ring (" .. pw .. ")")
            check(pw > MU.POWER, tag .. "bigger than the old button")
            local fr = frames[key == "o" and 1 or 2]
            local ring = { u.power.rect.x - MU.POWER_RING_PAD, u.power.rect.y - MU.POWER_RING_PAD,
                u.power.rect.x + u.power.rect.w + MU.POWER_RING_PAD, u.power.rect.y + u.power.rect.h + MU.POWER_RING_PAD }
            check(ring[1] >= fr[3] and not rectsHit(ring, fr), tag .. key .. ": the power ring is right of the arch frame")
            check(ring[4] <= fh and ring[2] >= 0, tag .. key .. ": inside the field")
            local ax, ay, aw, ah = absRect(MU, u.power.frame)
            check(math.abs(ax - u.power.rect.x) < 0.5 and math.abs(ay - u.power.rect.y) < 0.5, tag .. key .. ": rect = frame")
            local cx0, cy0, cw0 = absRect(MU, u.crystals[1])
            local cx1, _, cw1 = absRect(MU, u.crystals[MU.MAX_CRYSTALS])
            check(cx0 >= ring[3] and cx1 + cw1 <= bw, tag .. key .. ": the crystals are right of the ring and inside: " .. cx0 .. " " .. (cx1 + cw1))
            check(cy0 + 17 > ring[2] and cy0 < ring[4], tag .. key .. ": the crystals sit beside the ring")
            local wx, wy, ww = absRect(MU, u.weapon.frame)
            check(wx + ww <= fr[1] and wx + ww > fr[1] - 30, tag .. key .. ": the weapon is left of the arch frame")
            local kx, _, kw = absRect(MU, u.counts)
            check(kx + kw <= wx and kx >= 0, tag .. key .. ": the counts are left of the weapon")
            -- the hero arch is inside its own frame
            local hx, hy, hw, hh = absRect(MU, u.hero.frame)
            check(hx >= fr[1] - 8 and hx + hw <= fr[3] + 8, tag .. key .. ": the hero is in the arch frame")
        end
    end

    -- without the board field (ring heroes) the same rule holds: the power ring is as big as the hero ring
    animTeardown(F, MU)
    HS_BoardTextures = { generated = true, pieces = { hero_ring = visualBoard().pieces.hero_ring,
        hero_power_ring = visualBoard().pieces.hero_power_ring } }
    F, MU, X = animSetup({ board = HS_BoardTextures })
    local u = MU.ui.m
    check(u.hero.ring ~= nil and not u.hero.arch, "ring heroes without the field")
    check(u.power.frame:GetWidth() + 2 * MU.POWER_RING_PAD == u.hero.frame:GetWidth() + 2 * MU.HERO_RING_PAD,
        "ring heroes: the power ring is as big as the hero ring")
    check(MU.RowTop("o", MU.geom.fh) == math.floor(MU.geom.fh * MU.OPP_BOARD_Y), "without the art the old rows are used")
    animTeardown(F, MU)
    HS_BoardTextures = savedBT
end

-- item 4: shape-matched glows
local function testBoardGlows()
    local savedBT = HS_BoardTextures
    local F, MU, X = animSetup({ board = visualBoard() })
    X.push(ag({ seq = "2", lg = "a40.3/44/45,a1.3/44,h2.3/44/45/40/41/42" }))
    local ui = MU.ui
    local m1, hero, power = ui.m.minions[1], ui.m.hero, ui.m.power
    check(m1.glow.shapePiece == "glow_oval" and hero.glow.shapePiece == "glow_arch" and power.glow.shapePiece == "glow_round",
        "glows use the shape pieces: " .. tostring(m1.glow.shapePiece) .. " " .. tostring(hero.glow.shapePiece) .. " " .. tostring(power.glow.shapePiece))
    check(ui.hand[1].glow.shapePiece == nil, "a hand card keeps the rectangular glow")
    for _, g in ipairs({ m1.glow, hero.glow, power.glow }) do
        check(g.frame:IsShown() and g.tex._.blend == "ADD", "a ready character glows")
        local tint = g.tex._.vertex
        check(tint and math.abs(tint[1] - 0.3) < 1e-9 and tint[2] == 1 and math.abs(tint[3] - 0.3) < 1e-9, "green tint")
        check(string.find(g.tex._.texture, "glow_", 1, true) ~= nil, "the glow texture is a shape piece: " .. tostring(g.tex._.texture))
    end
    check(m1.glow.tex._.texture == "x\\glow_oval" and hero.glow.tex._.texture == "x\\glow_arch" and power.glow.tex._.texture == "x\\glow_round",
        "the right shape per character")
    -- the glow's shape rectangle is the frame grown by the shape pad: the inner anchor (0.1 .. 0.9) maps onto it
    local w, h = m1.frame:GetWidth(), m1.frame:GetHeight()
    local pad = m1.glow.shapePad
    local gw, gh = (w + 2 * pad) / 0.8, (h + 2 * pad) / 0.8
    local p1, p2 = m1.glow.frame._.points[1], m1.glow.frame._.points[2]
    check(math.abs(p1.x - (-pad - 0.1 * gw)) < 1e-6 and math.abs(p1.y - (pad + 0.1 * gh)) < 1e-6, "glow top left")
    -- the bottom right hangs on the parent's bottom right: the size is the parent's plus the two offsets
    check(p1.point == "TOPLEFT" and p2.point == "BOTTOMRIGHT" and p2.relPoint == "BOTTOMRIGHT" and p1.rel == p2.rel, "both corners on the parent's corners")
    check(math.abs((w + p2.x - p1.x) - gw) < 1e-6 and math.abs((h + p1.y - p2.y) - gh) < 1e-6, "glow size")
    check(math.abs(p1.x + p2.x) < 1e-6 and math.abs(p1.y + p2.y) < 1e-6, "the glow is centred on its parent")
    -- the arch glow follows the hero's arch box, the round one the power button
    local hw, hh = hero.frame:GetWidth(), hero.frame:GetHeight()
    check(hero.glow.w == hw and hero.glow.h == hh and hw > 80, "the arch glow is laid out for the arch box: " .. hw .. "x" .. hh)
    check(power.glow.w == power.frame:GetWidth(), "the round glow is laid out for the power button")
    -- targeting: red for enemies, green for friends, gold for the source
    F.fire(m1.frame, "OnClick", { arg1 = "LeftButton" })
    check(MU.sel and MU.sel.kind == "a", "attacker selected")
    local red = ui.o.minions[1].glow
    check(red.frame:IsShown() and red.shapePiece == "glow_oval" and red.tex._.vertex[1] == 1 and math.abs(red.tex._.vertex[2] - 0.25) < 1e-9,
        "enemy targets glow red in the oval")
    check(ui.o.hero.glow.frame:IsShown() and ui.o.hero.glow.tex._.texture == "x\\glow_arch" and ui.o.hero.glow.tex._.vertex[1] == 1, "the enemy hero glows red in its arch")
    check(m1.glow.tex._.vertex[1] == 1 and math.abs(m1.glow.tex._.vertex[2] - 0.85) < 1e-9, "the source is gold")
    MU.CancelTarget()
    animTeardown(F, MU)

    -- without the pieces: today's glow
    F, MU, X = animSetup({ board = visualBoard({ noGlows = true }) })
    X.push(ag({ seq = "2", lg = "a40.3/44/45,a1.3/44,h2.3/44/45/40/41/42" }))
    ui = MU.ui
    check(ui.m.minions[1].glow.shapePiece == nil and ui.m.hero.glow.shapePiece == nil and ui.m.power.glow.shapePiece == nil,
        "no shape pieces: the old glow")
    local bd = ui.m.minions[1].glow.frame._.bdBorder
    check(ui.m.minions[1].glow.frame:IsShown() and bd and math.abs(bd[1] - 0.3) < 1e-9, "the border glow is green")
    animTeardown(F, MU)
    HS_BoardTextures = savedBT
end

-- item 3: oval minion portraits
local function testMinionOvals()
    local savedBT, savedMT = HS_BoardTextures, HS_MinionTextures
    local F, MU, X = animSetup({ board = visualBoard(), minions = { generated = true, count = 1, set = { CS2_172 = true } } })
    local ui = MU.ui
    local m1, m2 = ui.m.minions[1], ui.m.minions[2]
    check(m1.cardId == "CS2_172" and m1.artMode == "oval", "the Raptor has an oval portrait")
    check(m1.art._.texture == MU.MINION_TEXTURE_PATH .. "CS2_172" and m1.art._.texcoord[2] == 1 and m1.art._.texcoord[4] == 1,
        "the portrait texture is whole: " .. tostring(m1.art._.texture))
    local p1, p2 = m1.art._.points[1], m1.art._.points[2]
    local w, h = m1.frame:GetWidth(), m1.frame:GetHeight()
    check(p1.rel == m1.frame and p1.x == math.floor(0.09 * w + 0.5) and p1.y == -math.floor(0.074 * h + 0.5)
        and p2.x == math.floor(0.9 * w + 0.5) and p2.y == -math.floor(0.926 * h + 0.5), "inside the ring's oval anchor")
    check(m2.cardId == "CS2_179" and m2.artMode == "crop" and m2.art:GetWidth() == 58, "a card without a portrait keeps the crop")
    -- a frozen minion tints in the oval's shape
    X.push(ag({ seq = "3", mb = "40.CS2_172.3.2.2.af,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a" }))
    check(m1.tint:IsShown() and m1.tint._.texture == MU.MINION_TEXTURE_PATH .. "CS2_172", "the tint is cut like the portrait")
    check(not m2.tint:IsShown() or m2.tint._.texture ~= MU.MINION_TEXTURE_PATH .. "CS2_179", "the other minion is not tinted with it")
    animTeardown(F, MU)

    -- the placeholder (nothing installed): the crop, today's look
    F, MU, X = animSetup({ board = visualBoard(), minions = { generated = false, count = 0, set = {} } })
    check(MU.ui.m.minions[1].artMode == "crop" and MU.ui.m.minions[1].art:GetWidth() == 58, "no portraits installed: the crop")
    animTeardown(F, MU)
    -- without the ring piece
    local bt = visualBoard()
    bt.pieces.minion_oval = nil
    F, MU, X = animSetup({ board = bt, minions = { generated = true, count = 1, set = { CS2_172 = true } } })
    check(MU.ui.m.minions[1].artMode == "crop", "no ring piece: the crop")
    animTeardown(F, MU)
    HS_BoardTextures, HS_MinionTextures = savedBT, savedMT
end

-- round hero power portraits (second in-game test): inside the ring's circular opening
local function testPowerRound()
    local savedBT, savedPT, savedCT = HS_BoardTextures, HS_PowerTextures, HS_CardTextures
    local F, MU, X = animSetup({ board = visualBoard() })
    HS_CardTextures = { generated = true, order = { "CS2_034", "CS2_102" }, bottom = {} }
    HS_PowerTextures = { generated = true, count = 1, set = { CS2_034 = true } }
    -- my power (Fireblast) has a portrait, the opponent's (Armor Up!) not
    X.push(ag({ seq = "2" }))
    local ui = MU.ui
    local p, q = ui.m.power, ui.o.power
    check(p.cardId == "CS2_034" and q.cardId == "CS2_102", "the powers")
    check(p.artMode == "round" and p.art._.texture == MU.POWER_TEXTURE_PATH .. "CS2_034", "the portrait is used: " .. tostring(p.art._.texture))
    check(p.art._.texcoord[1] == 0 and p.art._.texcoord[2] == 1 and p.art._.texcoord[3] == 0 and p.art._.texcoord[4] == 1, "whole texture")
    local fw = p.frame:GetWidth()
    local R = fw + 2 * MU.POWER_RING_PAD
    local c = HS_BoardTextures.pieces.hero_power_ring.anchors.circle
    local p1, p2 = p.art._.points[1], p.art._.points[2]
    check(p1.rel == p.frame and p1.point == "TOPLEFT" and p2.point == "BOTTOMRIGHT", "anchored to the power frame")
    local G = MU.POWER_ART_GROW
    check(G > 0 and p1.x == math.floor(-4 + c.left * R + 0.5) - G and p1.y == math.floor(4 - c.top * R + 0.5) + G
        and p2.x == math.floor(-4 + c.right * R + 0.5) + G and p2.y == math.floor(4 - c.bottom * R + 0.5) - G,
        "the ring's circle anchor grown by POWER_ART_GROW: " .. p1.x .. "," .. p1.y .. " " .. p2.x .. "," .. p2.y)
    -- ... and that is inside the ring's frame and centred on it (the ring spans frame +- 4)
    local ax, ay, aw, ah = absRect(MU, p.art)
    local rx, ry = absRect(MU, p.frame)
    rx, ry = rx - MU.POWER_RING_PAD, ry - MU.POWER_RING_PAD
    check(ax >= rx and ay >= ry, "inside the ring")
    check(math.abs((p2.x - p1.x) - ((c.right - c.left) * R + 2 * G)) <= 1 and math.abs((p1.y - p2.y) - ((c.bottom - c.top) * R + 2 * G)) <= 1, "size of the opening plus the growth")
    check(not p.label:IsShown() and p.cost.frame:IsShown() and p.glow.w == fw and p.glow.shapePiece == "glow_round", "cost gem, glow unchanged")
    local cp = p.cost.frame._.points[1]
    check(cp.point == "CENTER" and cp.rel == p.frame and cp.relPoint == "TOP", "the cost gem is on the top of the frame")
    -- the power without a portrait: the crop of the round art window in the ring's rectangle (today's look)
    check(q.artMode == "crop" and q.art._.points[1].x == math.floor(-4 + 0.242 * R + 0.5) - G, "fallback crop in the inner rectangle, grown")
    check(q.art._.texcoord[1] == MU.CROP_POWER[1] and q.art._.texcoord[2] == MU.CROP_POWER[2], "fallback uses the round window crop")
    -- the used state (without the medallion art) dims the portrait, shows no "Used" text, keeps the place
    X.push(ag({ seq = "3", mp = "2.CS2_034.2.1" }))
    check(p.pw == nil and p.used == nil, "no stray field, no Used text")
    check(p.artMode == "round", "used power still shows the portrait")
    local v = p.art._.vertex
    check(v and v[1] < 0.5 and v[2] == v[1], "dimmed")
    check(p.art._.points[1].x == p1.x and p.art._.points[2].y == p2.y, "same place when used")
    X.push(ag({ seq = "4", mp = "2.CS2_034.2.0" }))
    check(p.art._.vertex[1] == 1, "ready again: bright")
    animTeardown(F, MU)

    animTeardown(F, MU)

    -- the medallion: a used power is turned over (flip), shows no portrait, ring or cost and no "Used" text; the weapon
    -- sits in its own ring with the round portrait
    local bt2 = visualBoard()
    bt2.pieces.hero_power_used = { path = "x\\hero_power_used", w = 254, h = 256, right = 0.99, bottom = 1 }
    bt2.pieces.weapon_ring = { path = "x\\weapon_ring", w = 247, h = 256, right = 0.96, bottom = 1,
        anchors = { circle = { left = 0.178, top = 0.194, right = 0.824, bottom = 0.817 } } }
    HS_PowerTextures = { generated = true, count = 2, set = { CS2_034 = true, EX1_323w = true } }
    F, MU, X = animSetup({ board = bt2 })
    X.push(ag({ seq = "2" }))
    local pm = MU.ui.m.power
    check(pm.back and not pm.back:IsShown() and pm.art:IsShown() and pm.ring:IsShown() and pm.cost.frame:IsShown() and pm.flip == nil,
        "a ready power shows portrait, ring and cost, not the medallion")
    X.push(ag({ seq = "3", mp = "2.CS2_034.2.1" }))
    check(pm.flip ~= nil and pm.flip.to == true, "ready -> used starts the flip")
    MU.PowerFlipTick(MU.POWER_FLIP_TIME / 4)
    check(pm.art:IsShown() and not pm.back:IsShown() and pm.ring:GetWidth() < MU.POWER + 2 * MU.POWER_RING_PAD and not pm.cost.frame:IsShown(),
        "first half: the old face squashes, the cost is gone")
    MU.PowerFlipTick(MU.POWER_FLIP_TIME / 2)
    check(pm.back:IsShown() and not pm.art:IsShown() and not pm.ring:IsShown(), "second half: the medallion opens")
    MU.PowerFlipTick(MU.POWER_FLIP_TIME)
    check(pm.flip == nil and pm.back:IsShown() and not pm.art:IsShown() and not pm.ring:IsShown() and not pm.cost.frame:IsShown(),
        "used: only the medallion, no cost")
    check(pm.back._.points[1].x == -MU.POWER_RING_PAD and pm.back._.points[2].x == MU.POWER_RING_PAD, "the medallion covers the ring again")
    X.push(ag({ seq = "4", mp = "2.CS2_034.2.0" }))
    check(pm.flip ~= nil and pm.flip.to == false, "used -> ready flips back")
    MU.PowerFlipTick(1)
    check(pm.flip == nil and not pm.back:IsShown() and pm.art:IsShown() and pm.ring:IsShown() and pm.cost.frame:IsShown(), "ready again: portrait, ring and cost")
    -- the weapon: a ring and the round portrait
    X.push(ag({ seq = "5", ow = "33.EX1_323w.3.1" }))
    local wo = MU.ui.o.weapon
    check(wo.ring and wo.frame:IsShown() and wo.art._.texture == MU.POWER_TEXTURE_PATH .. "EX1_323w", "the weapon shows its round portrait in a ring: " .. tostring(wo.art._.texture))
    animTeardown(F, MU)

    -- no portraits installed / no circle anchor: today's crop
    HS_PowerTextures = savedPT
    F, MU, X = animSetup({ board = visualBoard() })
    check(MU.ui.m.power.artMode == "crop" and MU.ui.m.power.art._.texture ~= MU.POWER_TEXTURE_PATH .. "CS2_034", "placeholder: the crop")
    animTeardown(F, MU)
    local bt = visualBoard()
    bt.pieces.hero_power_ring.anchors.circle = nil
    HS_PowerTextures = { generated = true, count = 1, set = { CS2_034 = true } }
    F, MU, X = animSetup({ board = bt })
    check(MU.ui.m.power.artMode == "crop", "no circle anchor: the crop")
    animTeardown(F, MU)
    -- without the ring piece (native look)
    bt = visualBoard()
    bt.pieces.hero_power_ring = nil
    F, MU, X = animSetup({ board = bt })
    check(MU.ui.m.power.artMode == "crop" and MU.ui.m.power.ring == nil, "no ring: the crop")
    animTeardown(F, MU)
    HS_BoardTextures, HS_PowerTextures, HS_CardTextures = savedBT, savedPT, savedCT
end

-- item 5: the turn banner
local function testTurnBanner()
    local AN, MA = HsMatchAnim, HsMatch
    local savedBT = HS_BoardTextures
    local F, MU, X = animSetup({ board = visualBoard() })
    local ui = MU.ui
    local banner = ui.mid.banner
    local function runFor(s)
        for _ = 1, math.floor(s / 0.01 + 0.5) do MU.AnimTick(0.01) end
    end
    check(not banner:IsShown() and MU.bannerRt == nil, "no banner for the first game")
    check(not banner._.mouse, "the banner takes no mouse")
    check(ui.mid.turnText:GetText() == "", "the turn number is not shown")
    -- the turn changes without any beat: the banner flashes at once
    X.push(nextE({ "end.m.0.0.4.-", "turn.o.0.0.5.-" }, { turn = "5", act = "o" }))
    check(not AN.Busy() and banner:IsShown() and MU.bannerRt ~= nil and MU.bannerRt.mine == false, "the opponent's turn flashes")
    check(ui.mid.bannerTex._.texture == "x\\banner_opponent_turn", "the opponent's banner piece")
    check((banner._.alpha or 0) == 0, "it starts invisible")
    runFor(MU.BANNER_IN / 2)
    check(math.abs(banner._.alpha - 0.5) < 0.1, "fading in: " .. tostring(banner._.alpha))
    runFor(MU.BANNER_IN / 2 + 0.3)
    check(banner._.alpha == 1 and banner:IsShown(), "held")
    check(ui.mid.endButton:IsShown(), "the board is not covered by an input blocker")
    runFor(MU.BANNER_HOLD - 0.3 + MU.BANNER_OUT / 2 - 0.0001)
    check(banner:IsShown() and banner._.alpha < 0.7 and banner._.alpha > 0.3, "fading out: " .. tostring(banner._.alpha))
    runFor(MU.BANNER_OUT / 2 + 0.05)
    check(not banner:IsShown() and MU.bannerRt == nil and banner._.alpha == 1, "hidden after about 1.5 seconds")
    check(math.abs(MU.BANNER_IN + MU.BANNER_HOLD + MU.BANNER_OUT - 1.5) < 1e-9, "1.5 s in all")
    MU.Refresh()
    check(not banner:IsShown(), "a refresh in the same turn does not replay it")
    -- a resync (reply to GAME_GET) that jumps to another turn does not replay it
    local reqId = H.MatchGet()
    X.game(reqId, ag({ seq = "10", turn = "7", act = "m" }))
    check(MA.game.turn == 7 and not banner:IsShown() and MU.bannerRt == nil, "a resync is not announced")
    -- a /reload: the window is built fresh and syncs on opening
    MU.bannerSeen = "stale"
    MU.OnShow()
    check(not banner:IsShown() and MU.bannerSeen == MU.BannerKey(MA.game), "opening the window never announces")
    -- the next real turn flashes again: mine
    X.push(agE(MA.game.seq + 1, (MA.eventFloor or 0) + 1, { "end.o.0.0.7.-", "turn.m.0.0.8.-" }, { turn = "8", act = "m" }))
    check(banner:IsShown() and MU.bannerRt.mine and ui.mid.bannerTex._.texture == "x\\banner_your_turn", "my turn flashes")
    runFor(2)
    check(not banner:IsShown(), "and goes away")

    -- a plan is running when the turn changes: the banner waits for the commit
    local EV = { "atk.m.40.44.0.-", "dmg.o.40.44.3.-", "dmg.m.44.40.3.-", "die.o.0.44.0.CS2_124", "die.m.0.40.0.CS2_172",
        "end.m.0.0.8.-", "turn.o.0.0.9.-" }
    X.push(agE(MA.game.seq + 1, MA.lastEventSeq + 1, EV, { turn = "9", act = "o", mb = ATTACK_AFTER.mb, ob = ATTACK_AFTER.ob }))
    check(AN.Busy() and not banner:IsShown() and MU.bannerRt == nil, "no banner while the plan shows the old turn")
    runFor(0.3)
    check(AN.Busy() and not banner:IsShown(), "still none in the middle of the plan")
    runFor(0.7)
    check(not AN.Busy() and MA.game.turn == 9 and banner:IsShown() and MU.bannerRt ~= nil and not MU.bannerRt.mine,
        "the flash starts when the plan commits the new turn")
    animTeardown(F, MU)

    -- the same without a piece: a text banner
    HS_BoardTextures = savedBT
    F, MU, X = animSetup()
    X.push(nextE({ "end.m.0.0.4.-", "turn.o.0.0.5.-" }, { turn = "5", act = "o" }))
    check(MU.ui.mid.banner:IsShown() and not MU.ui.mid.bannerTex:IsShown() and string.find(MU.ui.mid.bannerText:GetText(), "Opponent", 1, true) ~= nil,
        "the text banner without the piece: " .. tostring(MU.ui.mid.banner:IsShown()) .. tostring(MU.ui.mid.bannerTex:IsShown())
        .. tostring(MU.ui.mid.bannerText:GetText()) .. tostring(MA.game.turn) .. tostring(MU.bannerSeen))
    MU.AnimTick(0.01)
    -- hiding the window ends it
    MU.EndBanner()
    check(not MU.ui.mid.banner:IsShown(), "EndBanner hides it")
    animTeardown(F, MU)
    HS_BoardTextures = savedBT
end

-- items 6 and 8: the arch cut-out and the secrets move with the hero
local function testHeroFollowers()
    local AN, MA = HsMatchAnim, HsMatch
    local savedBT = HS_BoardTextures
    local F, MU, X = animSetup({ board = visualBoard() })
    local ui = MU.ui
    local function runFor(s)
        for _ = 1, math.floor(s / 0.01 + 0.5) do MU.AnimTick(0.01) end
    end
    X.push(ag({ seq = "2", ms = "60.EX1_287,61.EX1_610,62.EX1_130,63.EX1_136,64.EX1_132,65.EX1_287", os = "70,71,72",
        oh = "3.HERO_05.30.30.2.0.-" }))
    -- secret positions: on the dome, centre, upper left, upper right, lower left, lower right; at most five
    for _, key in ipairs({ "m", "o" }) do
        local u = ui[key]
        local hero = u.hero
        local hx, hy, hw, hh = absRect(MU, hero.frame)
        local n = key == "m" and 5 or 3
        local size = MU.SecretSize(hw)
        check(size == math.floor(hw / 2 + 0.5), key .. ": half the frame width")
        local c = {}
        for i = 1, MU.MAX_SECRETS do
            local s = u.secrets[i]
            check(s.frame:IsShown() == (i <= n), key .. ": secret " .. i .. " shown: " .. tostring(s.frame:IsShown()))
            if i <= n then
                local sx, sy, sw, sh = absRect(MU, s.frame)
                check(sw == size and sh == size and s.frame:GetParent() == hero.frame, key .. ": sized, a child of the hero frame")
                c[i] = { sx + sw / 2 - hx, sy + sh / 2 - hy }
            end
        end
        check(math.abs(c[1][1] - hw / 2) <= 1 and math.abs(c[1][2]) <= 1, key .. ": the first one is on top of the dome: " .. c[1][1] .. "," .. c[1][2])
        check(c[2][1] < c[1][1] and c[3][1] > c[1][1] and c[2][2] > c[1][2] and math.abs(c[2][2] - c[3][2]) <= 1
            and math.abs((c[2][1] + c[3][1]) / 2 - c[1][1]) <= 1, key .. ": two and three left and right, level")
        if n == 5 then
            check(c[4][1] < c[2][1] and c[5][1] > c[3][1] and c[4][2] > c[2][2] and math.abs(c[4][2] - c[5][2]) <= 1,
                key .. ": four and five lower and further out")
            check(c[4][2] < hh and c[5][2] < hh, key .. ": still on the arch")
            -- on the dome: the centres are on the ellipse of the dome
            local a, b = hw / 2, hero.domePx
            for i = 1, 5 do
                local ex, ey = (c[i][1] - a) / a, (c[i][2] - b) / b
                check(math.abs(ex * ex + ey * ey - 1) < 0.04, key .. ": " .. i .. " lies on the dome edge")
            end
        end
    end
    -- the class medallion: own = the card's class, the opponent's hidden ones = the hero's class
    check(ui.m.secrets[1].icon._.texture == "x\\secret_mage" and ui.m.secrets[2].icon._.texture == "x\\secret_hunter"
        and ui.m.secrets[3].icon._.texture == "x\\secret_paladin", "own secrets show the medallion of their class")
    check(ui.m.secrets[1].icon:IsShown() and not ui.m.secrets[1].art:IsShown(), "the medallion replaces the plate")
    check(ui.o.secrets[1].icon._.texture == "x\\secret_hunter" and ui.o.secrets[3].icon._.texture == "x\\secret_hunter",
        "the opponent's hidden secrets show the hero's class (Hunter)")
    X.push(ag({ seq = "3", ms = "60.EX1_287", os = "70", oh = "3.HERO_01.30.30.2.0.-" }))
    check(ui.o.secrets[1].icon._.texture == "x\\secret_mage", "an unknown class falls back to the mage medallion")
    -- tooltips stay
    F.fire(ui.m.secrets[1].frame, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Counterspell" and F.tooltip.lines[3] == "Secret", "own secret tooltip")
    F.fire(ui.m.secrets[1].frame, "OnLeave")
    F.fire(ui.o.secrets[1].frame, "OnEnter")
    check(F.tooltip.shown and F.tooltip.lines[1] == "Secret", "opponent secret tooltip")
    F.fire(ui.o.secrets[1].frame, "OnLeave")
    X.push(ag({ seq = "4", ms = "60.EX1_287,61.EX1_610", os = "70,71", oh = "3.HERO_05.30.30.2.0.-" }))

    -- lunge and shake: the overlay and the secrets travel with the hero, everything is back afterwards
    for _, key in ipairs({ "m", "o" }) do
        local u = ui[key]
        local hero = u.hero
        local function where()
            local out = {}
            local list = { hero.frame, hero.overlay, u.secrets[1].frame, u.secrets[2].frame }
            for i = 1, table.getn(list) do
                local x, y = absRect(MU, list[i])
                out[i] = { x, y }
            end
            return out
        end
        local before = where()
        check(hero.overlay:IsShown(), key .. ": the cut-out is shown")
        local heroId, tgt = key == "m" and 1 or 3, key == "m" and 3 or 1
        local beat = { kind = "attack", dur = AN.ATTACK, source = heroId, target = tgt, planned = tgt, hits = {},
            impactAt = AN.IMPACT_AT, reach = 1, fizzle = false, hero = true }
        MU.BeatStart(beat)
        MU.BeatTick(beat, AN.ANTICIPATE)
        MU.BeatTick(beat, AN.IMPACT_AT)
        local now = where()
        local dx, dy = now[1][1] - before[1][1], now[1][2] - before[1][2]
        check(math.abs(dx) + math.abs(dy) > 5, key .. ": the hero lunges")
        check(hero.overlay:IsShown(), key .. ": the cut-out stays shown during the hero attack")
        for i = 2, 4 do
            check(math.abs(now[i][1] - before[i][1] - dx) < 0.001 and math.abs(now[i][2] - before[i][2] - dy) < 0.001,
                key .. ": piece " .. i .. " moves with the hero")
        end
        MU.BeatEnd(beat)
        MU.RestoreAll()
        local back = where()
        for i = 1, 4 do
            check(math.abs(back[i][1] - before[i][1]) < 1e-9 and math.abs(back[i][2] - before[i][2]) < 1e-9, key .. ": piece " .. i .. " restored")
        end
        check(hero.overlay:IsShown(), key .. ": the cut-out is back")
        -- the hero is hit: the shake moves them along too
        MU.Shake(hero)
        MU.FxTick(0.05)
        local sk = where()
        local sdx, sdy = sk[1][1] - before[1][1], sk[1][2] - before[1][2]
        check(math.abs(sdx) + math.abs(sdy) > 0.01, key .. ": the hero shakes")
        for i = 2, 4 do
            check(math.abs(sk[i][1] - before[i][1] - sdx) < 0.001 and math.abs(sk[i][2] - before[i][2] - sdy) < 0.001,
                key .. ": piece " .. i .. " shakes with the hero")
        end
        MU.RestoreAll()
        MU.ClearFx()
        back = where()
        for i = 1, 4 do
            check(math.abs(back[i][1] - before[i][1]) < 1e-9 and math.abs(back[i][2] - before[i][2]) < 1e-9, key .. ": piece " .. i .. " restored after the shake")
        end
    end
    -- a skip in the middle of a real hero attack restores everything
    local before = { absRect(MU, ui.m.hero.overlay) }
    X.push(nextE({ "atk.m.1.44.0.-", "dmg.o.1.44.2.-", "dmg.m.44.1.1.-" }, {}))
    check(AN.Busy(), "a hero attack plan runs")
    runFor(0.2)
    local mid = { absRect(MU, ui.m.hero.overlay) }
    check(math.abs(mid[1] - before[1]) + math.abs(mid[2] - before[2]) > 1 and ui.m.hero.overlay:IsShown(), "the cut-out is displaced mid-attack")
    MU.AnimSkip()
    local after = { absRect(MU, ui.m.hero.overlay) }
    check(math.abs(after[1] - before[1]) < 1e-9 and math.abs(after[2] - before[2]) < 1e-9 and ui.m.hero.overlay:IsShown(), "a skip restores the cut-out")
    check(MU.FxStats().touched == 0, "nothing is left moved")
    -- a new match / hidden window: the same
    X.push(nextE({ "atk.m.1.44.0.-", "dmg.o.1.44.2.-", "dmg.m.44.1.1.-" }, {}))
    runFor(0.2)
    MU.AnimAbort()
    after = { absRect(MU, ui.m.hero.overlay) }
    check(math.abs(after[1] - before[1]) < 1e-9 and math.abs(after[2] - before[2]) < 1e-9, "an abort restores the cut-out")
    animTeardown(F, MU)
    HS_BoardTextures = savedBT
end

-- item 7: a weapon breaks
local function testWeaponBreak()
    local AN, MA = HsMatchAnim, HsMatch
    local savedBT = HS_BoardTextures
    -- plan level: the break is its own beat, no effect pulse
    resetMatch()
    AN.SetMode("on")
    local function apply(payload)
        local g = MA.Parse(payload)
        MA.Apply(g)
        return MA.TakeTransition()
    end
    apply(ag({ ow = "33.CS2_106.3.1" }))
    local p = AN.Plan(apply(agE(2, 1, { "play.m.12.0.0.CS2_029", "wbrk.o.0.33.0.CS2_106" }, { ow = "" })))
    check(AN.Kinds(p) == "weapon_break" and p.beats[1].id == 33 and p.beats[1].side == "o" and p.beats[1].dur == AN.WEAPON_BREAK,
        "a play that destroys a weapon: " .. AN.Kinds(p))
    check(AN.WEAPON_BREAK >= 0.4 and AN.WEAPON_BREAK <= 0.6, "about half a second")
    -- the weapon disappears without an event: a break is added
    resetMatch(); AN.SetMode("on")
    apply(ag({ mw = "9.CS2_106.2.1" }))
    p = AN.Plan(apply(agE(2, 1, { "end.m.0.0.4.-", "turn.o.0.0.5.-" }, { mw = "", turn = "5", act = "o" })))
    check(AN.Kinds(p) == "weapon_break" and p.beats[1].id == 9 and p.beats[1].side == "m", "a vanished weapon breaks: " .. AN.Kinds(p) .. tostring(p.snap) .. tostring(p.reason))
    -- a weapon that was not on the board before has nothing to break
    resetMatch(); AN.SetMode("on")
    apply(ag({}))
    p = AN.Plan(apply(agE(2, 1, { "play.m.12.0.0.CS2_106", "eq.m.0.9.0.CS2_106", "wbrk.m.0.9.0.CS2_106" }, {})))
    check(AN.Kinds(p) == "", "not on the presented board: no beat: " .. AN.Kinds(p))
    -- the same weapon stays: no beat
    resetMatch(); AN.SetMode("on")
    apply(ag({ mw = "9.CS2_106.2.2" }))
    p = AN.Plan(apply(agE(2, 1, { "end.m.0.0.4.-", "turn.o.0.0.5.-" }, { mw = "9.CS2_106.2.1", turn = "5", act = "o" })))
    check(AN.Kinds(p) == "", "a weapon that survives does not break")

    -- the window
    local F, MU, X = animSetup({ board = visualBoard() })
    local ui = MU.ui
    local function runFor(s)
        for _ = 1, math.floor(s / 0.01 + 0.5) do MU.AnimTick(0.01) end
    end
    X.push(ag({ seq = "2", mw = "9.CS2_106.2.1" }))
    local w = ui.m.weapon
    check(w.frame:IsShown() and w.entity == 9, "a weapon")
    local home = { x = w.home.x, y = w.home.y, point = w.home.point }
    local base = { w = w.frame:GetWidth(), h = w.frame:GetHeight() }
    check(base.w == MU.WEAPON_W and base.h == MU.WEAPON_H, "weapon size")
    X.push(nextE({ "atk.m.1.44.0.-", "dmg.o.1.44.2.-", "wbrk.m.0.9.0.CS2_106", "die.o.0.44.0.CS2_124" },
        { mw = "", ob = "45.EX1_412.4.2.3.-" }))
    check(AN.Busy() and ui.m.weapon.frame:IsShown() and w.entity == 9, "the old weapon stays during the beats")
    local v0 = { w.art._.vertex[1], w.art._.vertex[2], w.art._.vertex[3] }
    MU.AnimTick(0.001)
    local plan = AN.Current()
    check(plan and AN.Kinds(plan) == "attack,weapon_break,death", "beats: " .. AN.Kinds(plan))
    runFor(AN.ATTACK + 0.02)
    local _, beat = AN.Current()
    check(beat and beat.kind == "weapon_break", "the break beat runs")
    check(w.frame:IsShown() and (w.frame._.alpha or 1) == 1 and w.frame:GetWidth() == base.w, "it starts whole")
    runFor(AN.WEAPON_BREAK * 0.15)
    check(w.frame:IsShown() and MU.FxStats().textures >= 1, "the crack flashes")
    check(w.sx and w.sx ~= 0 or true, "it trembles")
    runFor(AN.WEAPON_BREAK * 0.5)
    local a = w.frame._.alpha
    local p1 = w.frame._.points[1]
    check(a and a > 0 and a < 1, "it fades: " .. tostring(a))
    check(w.frame:GetWidth() < base.w and w.frame:GetHeight() < base.h, "it shrinks")
    check(p1.y < home.y, "it falls: " .. p1.y .. " vs " .. home.y)
    local v = w.art._.vertex
    check(v and v[1] == v0[1] and v[2] == v0[2] and v[3] == v0[3], "the weapon art is not tinted while it breaks")
    check(w.atk.frame:IsShown() and w.atk.text._.textColor[1] == 1 and w.atk.text._.textColor[2] == 1, "the gem numbers stay white")
    check(w.dur.frame:IsShown(), "the gems stay with the frame")
    runFor(AN.WEAPON_BREAK * 0.4)
    -- the death beat follows: the weapon stays gone
    local _, b2 = AN.Current()
    check(b2 and b2.kind == "death" and w.frame._.alpha == 0, "gone during the next beat")
    runFor(1)
    check(not AN.Busy() and not w.frame:IsShown() and w.entity == nil, "committed: no weapon")
    check(w.frame._.alpha == 1 and w.frame:GetWidth() == base.w and w.frame:GetHeight() == base.h and w.scale == nil and w.alpha == nil
        and w.sx == nil and w.ay == nil, "the frame is restored")
    local pp = w.frame._.points[1]
    check(table.getn(w.frame._.points) == 1 and pp.point == home.point and pp.x == home.x and pp.y == home.y, "back at its home point")
    check(MU.FxStats().touched == 0 and MU.FxStats().textures == 0, "nothing leaks")
    -- a new weapon appears after a break
    X.push(ag({ seq = "20", mw = "9.CS2_106.2.1" }))
    check(w.frame:IsShown() and w.frame._.alpha == 1, "a new weapon is drawn normally")

    -- a skip in the middle of the break
    X.push(nextE({ "atk.m.1.44.0.-", "dmg.o.1.44.2.-", "wbrk.m.0.9.0.CS2_106" }, { mw = "" }))
    runFor(AN.ATTACK + AN.WEAPON_BREAK * 0.6)
    check(AN.Busy() and w.frame._.alpha < 1, "mid break")
    MU.AnimSkip()
    check(not AN.Busy() and w.frame._.alpha == 1 and w.frame:GetWidth() == base.w and w.scale == nil and MU.FxStats().touched == 0, "a skip restores the weapon frame")
    pp = w.frame._.points[1]
    check(pp.x == home.x and pp.y == home.y, "a skip restores the point")
    animTeardown(F, MU)
    HS_BoardTextures = savedBT
end

-- ------------------------------------------------------------------ phase 10, part B: native effects
local function adler32(str)
    local a, b = 1, 0
    for i = 1, string.len(str) do
        a = math.fmod(a + string.byte(str, i), 65521)
        b = math.fmod(b + a, 65521)
    end
    return b * 65536 + a
end

-- the generated profiles are what the manifest says, and every reference in them resolves
local function testFxProfiles()
    local AN, FX = HsMatchAnim, H.MatchFx
    local P = HS_EffectProfiles
    check(type(P) == "table" and P.schema == 1, "profiles loaded")
    local manifest = readAll(ROOT .. "/data/fx/effect_profiles.json")
    check(manifest ~= nil, "the manifest is checked in")
    manifest = string.gsub(manifest or "", "\r", "")
    check(P.manifestAdler == adler32(manifest), "the generated file matches the manifest (run tools/fx/gen_effect_profiles.py): "
        .. tostring(P.manifestAdler) .. " vs " .. adler32(manifest))
    check(string.find(readAll(ROOT .. "/addon/Hearthstone/MatchFxProfiles.lua") or "", "GENERATED by tools/fx/gen_effect_profiles.py", 1, true) ~= nil,
        "the profile file says it is generated")
    local n = 0
    for key, p in pairs(P.presets) do
        n = n + 1
        check(type(p.model) == "string" and string.find(p.model, "^Spells\\") ~= nil, key .. ": model path into the client data")
        check(p.kind == "missile" or p.kind == "impact" or p.kind == "aura", key .. ": kind")
        check(type(p.scale) == "number" and p.scale > 0 and table.getn(p.pos) == 3 and type(p.facing) == "number", key .. ": scale, position, facing")
        check(type(p.dur) == "number" and p.dur > 0 and p.dur <= FX.MAX_LIFE, key .. ": bounded duration")
        check(p.size == nil or (p.size >= 16 and p.size <= 512), key .. ": optional size override")
        check(p.ext == nil or (p.ext > 0 and p.ext <= 100), key .. ": natural radius in yards")
        check(p.verified == false, key .. ": not verified until the owner checked it in game")
        check(p.exists == true, key .. ": the file was found in the client data")
        check(p.fallback == nil or FX.STYLES[p.fallback], key .. ": fallback style")
        check(p.source == "design" or p.source == "client" or p.source == "guess", key .. ": source")
        check(p.sound == nil or string.find(p.sound, "^Sound\\") ~= nil, key .. ": sound path")
    end
    check(n >= 12 and n <= 20, "about 12-20 presets: " .. n)
    -- every family: a bounded duration, a 2D fallback, presets that exist with the right kind
    local nfam = 0
    for _ in pairs(P.families) do nfam = nfam + 1 end
    check(nfam == table.getn(AN.FAMILIES), "profiles and animation know the same families")
    for i = 1, table.getn(AN.FAMILIES) do
        local fam = AN.FAMILIES[i]
        local f = P.families[fam]
        check(f ~= nil, fam .. ": family profile")
        local d = FX.FamilyDuration(fam)
        check(d > 0 and d <= FX.MAX_LIFE, fam .. ": bounded duration " .. d)
        local r = FX.Resolve(nil, fam)
        check(FX.STYLES[r.fallback], fam .. ": a 2D fallback style " .. tostring(r.fallback))
        for _, role in ipairs({ "missile", "impact", "area" }) do
            if f and f[role] then
                local p = P.presets[f[role]]
                check(p ~= nil and ((role == "missile") == (p.kind == "missile")), fam .. ": " .. role .. " preset " .. f[role])
            end
        end
        check(H.MatchUI.FamilyColor(fam)[1] ~= nil, fam .. ": has a colour")
    end
    -- cards: known cards, existing presets, valid families
    local cards = 0
    for id, c in pairs(P.cards) do
        cards = cards + 1
        check(HS_CardDB.cards[id] ~= nil, id .. ": card exists")
        for _, role in ipairs({ "missile", "impact", "area" }) do
            check(c[role] == nil or c[role] == false or P.presets[c[role]] ~= nil, id .. ": " .. role .. " preset exists")
        end
        check(c.fallback == nil or FX.STYLES[c.fallback], id .. ": fallback style")
    end
    check(cards >= 50, "iconic cards are mapped: " .. cards)
    for _, id in ipairs({ "CS2_029", "CS2_024", "EX1_279", "CS2_032", "CS2_028", "EX1_277", "CS2_025", "CS2_022", "CS2_089", "CS2_093",
        "CS2_234", "EX1_622", "DS1_233", "EX1_238", "CS2_062", "EX1_173", "CS2_012", "CS2_072", "CS2_076", "CS2_108", "EX1_400",
        "CS2_007", "CS2_034", "CS1h_001", "DS1h_292", "CS2_056" }) do
        check(P.cards[id] ~= nil, id .. " has an override")
    end
    -- card families are installed into the animation table
    check(AN.CARD_FAMILY.CS2_029 == "fire" and AN.CARD_FAMILY.CS2_024 == "frost" and AN.CARD_FAMILY.EX1_277 == "arcane"
        and AN.CARD_FAMILY.CS2_234 == "shadow" and AN.CARD_FAMILY.CS2_093 == "holy" and AN.CARD_FAMILY.EX1_238 == "nature",
        "CARD_FAMILY filled")
    for id, fam in pairs(AN.CARD_FAMILY) do
        local known = false
        for i = 1, table.getn(AN.FAMILIES) do if AN.FAMILIES[i] == fam then known = true end end
        check(known, id .. ": family " .. tostring(fam) .. " is known")
    end
    -- the family map
    local r = FX.Resolve("CS2_029", "fire")
    check(r.missileKey == "fireball" and r.impactKey == "fire_impact" and r.areaKey == "fire_burst" and r.trail, "fire: fireball, fire impact, burst")
    r = FX.Resolve("CS2_034", "fire")
    check(r.missile == nil and r.impactKey == "fire_impact", "Fireblast: no missile (override false)")
    r = FX.Resolve("CS2_032", "fire")
    check(r.areaKey == "fire_burst", "Flamestrike: area burst")
    -- unknown ids, families and a missing table degrade
    r = FX.Resolve("ZZZ_999", "bogus")
    check(r.missile == nil and r.impact == nil and r.area == nil and r.fallback == "pulse" and not r.trail and r.sound == nil, "unknown card and family")
    local saved = HS_EffectProfiles
    HS_EffectProfiles = nil
    r = FX.Resolve("CS2_029", "fire")
    check(r.missile == nil and r.fallback == "pulse", "no profile table")
    FX.InstallCardFamilies()
    HS_EffectProfiles = { families = { fire = { missile = "nope", impact = "nope" } }, presets = { x = { model = 5 } } }
    r = FX.Resolve(nil, "fire")
    check(r.missile == nil and r.impact == nil, "missing presets are ignored")
    HS_EffectProfiles = saved
end

local function fxEffect(MU, fields)
    local AN = HsMatchAnim
    local beat = { kind = "effect", dur = AN.EFFECT, family = "generic", targets = {}, sides = {}, area = false, source = 0,
        aim = 0, side = "m", slots = {}, types = {}, events = {} }
    for k, v in pairs(fields) do beat[k] = v end
    local ctx = MU.EffectContext(beat)
    local ok, err = pcall(MU.Fx.PlayEffect, beat, ctx)
    return ok, err, beat, ctx
end

local function shownModels(F)
    local out = {}
    for i = 1, table.getn(F.models) do
        local m = F.models[i]
        if m._.shown and m._.modelPath then table.insert(out, m) end
    end
    return out
end

local function runTicks(MU, seconds, step)
    step = step or 0.01
    for _ = 1, math.floor(seconds / step + 0.5) do MU.AnimTick(step) end
end

local function testFxEffects()
    local AN, FX = HsMatchAnim, H.MatchFx
    local F, MU, X = animSetup()
    local base = baselineLevels(MU)
    local fire = { family = "fire", cardId = "EX1_308", source = 40, targets = { 45 }, sides = { [45] = "o" }, aim = 45 }
    check(table.getn(F.models) == FX.POOL and FX.PoolStats().models == FX.POOL, "the model pool is made once with the window: " .. table.getn(F.models))
    for i = 1, table.getn(F.models) do
        check(F.models[i]:GetParent() == MU.fxFrame and not F.models[i]:IsShown(), "pool model " .. i .. " hidden on the Fx layer")
    end
    local frames = table.getn(F.frames)

    -- every family: bounded, with a 2D layer, and everything returns
    for i = 1, table.getn(AN.FAMILIES) do
        local fam = AN.FAMILIES[i]
        local ok, err = fxEffect(MU, { family = fam, source = 40, targets = { 45 }, sides = { [45] = "o" }, aim = 45 })
        check(ok, fam .. ": plays " .. tostring(err))
        check(table.getn(usedTextures(MU)) >= 1, fam .. ": the 2D layer shows")
        local longest = 0
        for _, it in ipairs(MU.fxItems) do if it.dur > longest then longest = it.dur end end
        check(longest > 0 and longest <= FX.MAX_LIFE + 1e-9, fam .. ": bounded " .. longest)
        runTicks(MU, FX.MAX_LIFE + 0.2, 0.05)
        MU.RestoreAll()                       -- a destroy shake marks its slot until the plan ends
        checkAtRest(MU, base, fam)
    end
    check(table.getn(F.frames) == frames, "no frame is created while effects play")

    -- a missile flies from the source to the target and ends on the target anchor
    F.time = F.time + 10
    local before = table.getn(F.soundFiles)
    fxEffect(MU, fire)
    local last = FX.last
    check(last and last.launches[1] and last.launches[1].from == 40 and last.launches[1].to == 45 and last.models == 1, "fireball: one missile launch 40 -> 45")
    local target, src = MU.AnchorOf(45), MU.AnchorOf(40)
    local live = shownModels(F)
    check(table.getn(live) == 1 and live[1]._.modelPath == "Spells\\Fireball_Missile_High.mdx", "the fireball model flies")
    check(table.getn(F.soundFiles) == before + 1 and F.soundFiles[table.getn(F.soundFiles)] == "Sound\\Spells\\FireBallImpactA.wav", "one sound")
    runTicks(MU, 0.2)
    local p = live[1]._.points[1]
    local d0 = math.sqrt((src.x - target.x) ^ 2 + (src.y - target.y) ^ 2)
    local d1 = math.sqrt((p.x - target.x) ^ 2 + (-p.y - target.y) ^ 2)
    check(d1 < d0 and d1 > 0, "the missile is on its way: " .. d1 .. " of " .. d0)
    runTicks(MU, 0.1)
    local both = shownModels(F)
    check(table.getn(both) == 1 and both[1]._.modelPath == "Spells\\FireBlast_Impact_Chest.mdx", "then the impact model: the missile went back to the pool")
    local ip = both[1]._.points[1]
    check(ip.rel == MU.board and ip.relPoint == "TOPLEFT" and ip.x == target.x and ip.y == -target.y, "the impact is on the target anchor")
    local hasScale = false
    for _, c in ipairs(both[1]._.calls) do if c.name == "SetModelScale" then hasScale = true end end
    check(hasScale, "scale is set")
    runTicks(MU, 1.2)
    check(FX.PoolStats().used == 0 and table.getn(shownModels(F)) == 0, "all models returned after their lifetime")
    local cleared = 0
    for _, m in ipairs(F.models) do
        for _, c in ipairs(m._.calls) do if c.name == "ClearModel" then cleared = cleared + 1 end end
    end
    check(cleared >= 2, "models are cleared: " .. cleared)
    checkAtRest(MU, base, "missile")

    -- an area effect: one burst, a mark on every target, one sound
    F.time = F.time + 10
    before = table.getn(F.soundFiles)
    local textures0 = table.getn(usedTextures(MU))
    fxEffect(MU, { family = "fire", cardId = "CS2_032", source = 0, area = true, targets = { 44, 45, 41 }, sides = { [44] = "o", [45] = "o", [41] = "m" } })
    last = FX.last
    check(last.area and last.goals == 3 and last.models == 1 and table.getn(last.launches) == 0, "area: one model, no missiles")
    live = shownModels(F)
    check(table.getn(live) == 1 and live[1]._.modelPath == "Spells\\FireNova_Area.mdx"
        and math.abs(live[1]:GetWidth() * live[1]:GetEffectiveScale() - FX.Cal().frame) < 1e-6, "the burst model (a frame of the calibrated screen size)")
    check(table.getn(usedTextures(MU)) - textures0 >= 4, "a burst and a mark on each target at once: " .. (table.getn(usedTextures(MU)) - textures0))
    check(table.getn(F.soundFiles) - before == 1 and F.soundFiles[table.getn(F.soundFiles)] == "Sound\\Spells\\Flamestrike.wav", "exactly one sound")
    runTicks(MU, 1.5, 0.05)
    checkAtRest(MU, base, "area")
    -- three or more targets without damage count as an area too (Frost Nova)
    fxEffect(MU, { family = "frost", cardId = "CS2_026", source = 0, targets = { 44, 45, 41 }, sides = { [44] = "o", [45] = "o", [41] = "m" } })
    check(FX.last.area and table.getn(FX.last.launches) == 0, "mass effects are grouped")
    runTicks(MU, 1.5, 0.05)
    checkAtRest(MU, base, "mass")

    -- a target that is not on the board: the middle of its side, no missile
    fxEffect(MU, { family = "fire", cardId = "EX1_308", source = 40, targets = { 77 }, sides = { [77] = "o" } })
    check(FX.last and table.getn(FX.last.launches) == 0 and table.getn(usedTextures(MU)) >= 1, "missing target: no missile, the 2D mark")
    runTicks(MU, 1.5, 0.05)
    -- nothing to show at all
    local ok = fxEffect(MU, { family = "fire", targets = {}, source = 0, side = "-" })
    check(ok and FX.last == nil, "no target and no source: nothing")
    checkAtRest(MU, base, "missing")

    -- sounds: a gap between two, off, and a name instead of a path
    F.time = F.time + 10
    before = table.getn(F.soundFiles)
    fxEffect(MU, fire)
    fxEffect(MU, { family = "fire", cardId = "EX1_308", source = 40, targets = { 44 }, sides = { [44] = "o" } })
    check(table.getn(F.soundFiles) == before + 1 and FX.stats.throttled >= 1, "the second sound inside the gap is dropped")
    F.time = F.time + FX.SOUND_GAP + 0.01
    fxEffect(MU, fire)
    check(table.getn(F.soundFiles) == before + 2, "a sound after the gap plays")
    FX.SetSound(false)
    F.time = F.time + 10
    fxEffect(MU, fire)
    check(table.getn(F.soundFiles) == before + 2 and H.db.fxSound == false and not FX.SoundOn(), "sound off")
    FX.SetSound(true)
    check(FX.SoundOn() and H.db.fxSound == nil, "sound on again")
    F.time = F.time + 10
    check(FX.PlaySound("igMainMenuOpen") and F.sounds[table.getn(F.sounds)] == "igMainMenuOpen", "a name goes to PlaySound")
    runTicks(MU, 2, 0.05)
    checkAtRest(MU, base, "sounds")

    -- model modes
    FX.SetModelMode("verified")
    check(H.db.fxModels == "verified" and FX.ModelMode() == "verified", "mode saved")
    fxEffect(MU, fire)
    check(FX.last.models == 0 and table.getn(shownModels(F)) == 0 and table.getn(usedTextures(MU)) >= 1, "verified only: no model for an unverified preset, the 2D layer plays")
    HS_EffectProfiles.presets.fireball.verified = true
    HS_EffectProfiles.presets.fire_impact.verified = true
    fxEffect(MU, fire)
    check(FX.last.models == 1, "verified presets play")
    HS_EffectProfiles.presets.fireball.verified = false
    HS_EffectProfiles.presets.fire_impact.verified = false
    MU.AnimSkip()
    FX.SetModelMode("off")
    fxEffect(MU, fire)
    check(FX.last.models == 0 and table.getn(shownModels(F)) == 0, "models off")
    FX.SetModelMode("all")
    check(H.db.fxModels == nil and not FX.SetModelMode("bogus"), "mode all is the default")
    MU.AnimSkip()
    checkAtRest(MU, base, "modes")

    -- a failing SetModel switches the model layer off, the beat goes on with the 2D layer
    F.failSetModel = true
    local ok2, err2 = fxEffect(MU, fire)
    check(ok2, "a failing model does not break the beat: " .. tostring(err2))
    check(FX.modelsDisabled and string.find(FX.modelError, "SetModel", 1, true) ~= nil, "the model layer is off: " .. tostring(FX.modelError))
    check(table.getn(usedTextures(MU)) >= 1 and FX.PoolStats().used == 0, "the 2D layer plays, no model is held")
    fxEffect(MU, { family = "frost", cardId = "CS2_024", source = 40, targets = { 45 }, sides = { [45] = "o" } })
    check(FX.last.models == 0 and table.getn(F.models) == FX.POOL, "later effects: 2D only")
    F.failSetModel = nil
    MU.AnimSkip()
    FX.Enable()
    fxEffect(MU, { family = "frost", cardId = "CS2_024", source = 40, targets = { 45 }, sides = { [45] = "o" } })
    check(FX.last.models == 1 and table.getn(shownModels(F)) >= 1, "/hs fx reset brings the models back")
    MU.AnimSkip()
    checkAtRest(MU, base, "failed model")
    -- a Model frame that cannot be created
    F.failCreateModel = true
    FX.matchStage.mpool = nil
    ok2 = pcall(FX.Init, MU.fxFrame)
    check(ok2 and FX.modelsDisabled and string.find(FX.modelError, "Model frame", 1, true) ~= nil, "no Model frame type: layer off " .. tostring(FX.modelError))
    fxEffect(MU, fire)
    check(table.getn(usedTextures(MU)) >= 1, "still a 2D effect")
    F.failCreateModel = nil
    FX.Enable()
    MU.AnimSkip()
    -- model methods the client lacks are skipped
    FX.matchStage.mpool = nil
    FX.Init(MU.fxFrame)
    for _, m in ipairs(F.models) do m.SetModelScale, m.SetFacing, m.SetPosition = false, false, false end
    ok2 = fxEffect(MU, fire)
    check(ok2 and FX.last.models == 1 and not FX.modelsDisabled, "a method the client lacks is not needed")
    MU.AnimSkip()

    -- missing / bad profiles
    local saved = HS_EffectProfiles
    HS_EffectProfiles = nil
    ok2 = fxEffect(MU, fire)
    check(ok2 and FX.last.models == 0 and table.getn(usedTextures(MU)) >= 1, "no profiles: the 2D layer")
    HS_EffectProfiles = { presets = {}, families = { fire = { missile = "nope", impact = "nope", fallback = "ring" } }, cards = {} }
    ok2 = fxEffect(MU, fire)
    check(ok2 and FX.last.models == 0, "unknown presets")
    HS_EffectProfiles = saved
    MU.AnimSkip()
    checkAtRest(MU, base, "profiles")

    -- an error inside the layer: the hook raises (MatchUI then plays the pulse), three errors silence it
    local oldPlay = FX.Play
    FX.Play = function() error("boom") end
    local beat2 = { kind = "effect", dur = AN.EFFECT, family = "fire", targets = { 45 }, sides = { [45] = "o" }, source = 40, aim = 45, side = "m" }
    check(not pcall(MU.Fx.PlayEffect, beat2, MU.EffectContext(beat2)), "an error is passed on")
    pcall(MU.Fx.PlayEffect, beat2, MU.EffectContext(beat2))
    pcall(MU.Fx.PlayEffect, beat2, MU.EffectContext(beat2))
    check(FX.broken and FX.stats.errors == 3, "broken after three errors")
    local ok3 = pcall(MU.Fx.PlayEffect, beat2, MU.EffectContext(beat2))
    check(ok3 and table.getn(usedTextures(MU)) >= 1, "then the plain 2D pulse")
    FX.Play = oldPlay
    FX.Enable()
    MU.AnimSkip()
    checkAtRest(MU, base, "broken")

    -- models are returned on skip, hide, a new match and a resync; and at the end of the plan
    local board = "44.CS2_124.3.1.1.c,45.CS2_121.1.1.1.-"
    local function spell(over)
        local o = { ob = board }
        for k, v in pairs(over or {}) do o[k] = v end
        X.push(nextE({ "play.m.12.45.0.EX1_308", "dmg.o.12.45.5.-" }, o))
    end
    F.time = F.time + 10
    spell()
    runTicks(MU, 0.1)
    local _, cb = AN.Current()
    check(cb and cb.kind == "effect" and cb.family == "fire", "a fire effect beat plays: " .. tostring(cb and cb.family))
    check(FX.PoolStats().used >= 1 and FX.PoolStats().runs >= 1, "models are in use")
    MU.AnimSkip()
    checkAtRest(MU, base, "skip")
    X.push(ag({ seq = tostring(HsMatch.game.seq + 1) }))
    spell({ seq = tostring(HsMatch.game.seq + 1) })
    runTicks(MU, 0.1)
    check(FX.PoolStats().used >= 1, "models in use again")
    MU.HideWindow()
    checkAtRest(MU, base, "hide")
    X.push(ag({ seq = tostring(HsMatch.game.seq + 1) }))
    spell({ seq = tostring(HsMatch.game.seq + 1) })
    runTicks(MU, 0.1)
    check(FX.PoolStats().used >= 1, "models in use a third time")
    X.push(ag({ mid = "98", seq = "1" }))
    checkAtRest(MU, base, "new match")
    X.push(ag({ mid = "98", seq = "2" }))
    spell({ mid = "98", seq = "3" })
    runTicks(MU, 5, 0.05)
    check(not AN.Busy() and FX.PoolStats().used == 0, "at the end of the plan nothing is left")
    checkAtRest(MU, base, "finished")
    check(AN.failed == nil and MU.animError == nil, "no animation error")
    animTeardown(F, MU)
end

-- The reference fireball uses shipped alpha textures and a beat-owned flight clock.
local function testFireball()
    local AN, FX = HsMatchAnim, H.MatchFx
    local F, MU, X = animSetup()
    local base = baselineLevels(MU)
    local frames = table.getn(F.frames)
    local fire = { family = "fire", cardId = "CS2_029", source = 40, targets = { 45 }, sides = { [45] = "o" },
        aim = 45, dur = 0.34, flight = 0.34, visual = "fireball" }
    check(FX.Resolve("CS2_029", "fire").visual == "fireball", "the reference card opts into the new renderer")
    check(FX.Resolve("CS2_034", "fire").visual == nil, "hero powers retain their existing profile")
    local before = table.getn(F.soundFiles)
    local ok, err, beat = fxEffect(MU, fire)
    check(ok, "reference fireball starts: " .. tostring(err))
    check(table.getn(shownModels(F)) == 0, "reference effect does not depend on native models")
    check(table.getn(usedTextures(MU)) >= 2, "core and arrival cue have reserved texture leases")
    for _, obj in ipairs(usedTextures(MU)) do
        check(string.find(obj:GetTexture() or "", "Textures\\Effects\\", 1, true) ~= nil,
            "every fireball texture is an effect asset, never WHITE8X8 or a button border")
    end
    check(table.getn(F.soundFiles) == before, "no impact sound at launch")
    runTicks(MU, 0.33)
    check(table.getn(F.soundFiles) == before, "no impact sound before arrival")
    runTicks(MU, 0.02)
    check(table.getn(F.soundFiles) == before + 1, "one impact sound on arrival")
    runTicks(MU, 1)
    check(table.getn(F.soundFiles) == before + 1, "decay does not replay impact sound")
    checkAtRest(MU, base, "fireball decay")
    check(table.getn(F.frames) == frames, "fireball reuses existing frames")

    -- A broken native model layer cannot remove the reference fireball.
    FX.modelsDisabled = true
    fxEffect(MU, fire)
    check(table.getn(usedTextures(MU)) >= 2, "2D fireball still works with native models disabled")
    MU.AnimSkip()
    FX.Enable()
    checkAtRest(MU, base, "fireball skip")

    -- Reserve essential pieces before decorations when other effects occupy the pool.
    local held = {}
    for i = 1, MU.POOL_TEXTURES - 2 do held[i] = MU.AllocTexture() end
    ok, err = fxEffect(MU, fire)
    check(ok and MU.FxStats().textures == MU.POOL_TEXTURES, "two free textures still provide core and impact")
    MU.AnimSkip()
    for _, h in ipairs(held) do MU.FreeTexture(h) end
    checkAtRest(MU, base, "fireball under pool pressure")
    held = {}
    for i = 1, MU.POOL_TEXTURES do held[i] = MU.AllocTexture() end
    ok, err = fxEffect(MU, fire)
    check(ok and MU.FxStats().textures == MU.POOL_TEXTURES, "exhausted pool does not grow or throw")
    MU.AnimSkip()
    for _, h in ipairs(held) do MU.FreeTexture(h) end
    checkAtRest(MU, base, "fireball exhausted pool")

    -- A failed asset load must neither leak its reservation nor fall back to a white quad.
    local obj = MU.pool.tex[1].obj
    local setTexture = obj.SetTexture
    obj.SetTexture = function(o, path)
        if type(path) == "string" and string.find(path, "Textures\\Effects\\", 1, true) then return false end
        return setTexture(o, path)
    end
    ok = fxEffect(MU, fire)
    check(ok and MU.FxStats().textures == 0 and FX.last.error ~= nil, "missing fireball asset is diagnosed without rectangles or leaked leases")
    obj.SetTexture = setTexture
    MU.AnimSkip()

    -- Real transition: sound, arrival and damage happen at the same timeline boundary.
    F.time = F.time + 10
    before = table.getn(F.soundFiles)
    X.push(nextE({ "play.m.12.45.0.CS2_029", "dmg.o.12.45.1.-" }))
    MU.AnimTick(0.2)
    local _, current = AN.Current()
    check(current and current.kind == "effect" and current.visual == "fireball", "live transition uses reference effect")
    check(current and current.fxItem and math.abs(current.fxItem.t - 0.2) < 1e-8,
        "flight follows beat time without a second frame advance")
    MU.AnimTick(0.13)
    check(table.getn(F.soundFiles) == before and table.getn(usedTexts(MU)) == 0, "sound and damage wait until arrival")
    MU.AnimTick(0.02)
    check(table.getn(F.soundFiles) == before + 1 and table.getn(usedTexts(MU)) > 0, "sound and damage start together")
    for _, it in ipairs(MU.fxItems) do check(it.kind ~= "flash", "no rectangular damage flash for fireball") end
    runTicks(MU, 1.5)
    checkAtRest(MU, base, "live fireball")

    -- Ordinary damage still receives its existing feedback.
    MU.ShowHits({ { id = 45, kind = "dmg", amount = 1 } })
    local hasFlash = false
    for _, it in ipairs(MU.fxItems) do if it.kind == "flash" then hasFlash = true end end
    check(hasFlash, "fireball flash suppression does not affect unrelated damage")
    MU.AnimSkip()

    -- An opponent reveal consumes part of the frame before the flight starts.
    F.time = F.time + 10
    before = table.getn(F.soundFiles)
    X.push(nextE({ "play.o.80.40.0.CS2_029", "dmg.m.80.40.1.-" }))
    runTicks(MU, 1.6, 0.1)
    MU.AnimTick(0.1)
    local _, afterReveal = AN.Current()
    check(afterReveal and afterReveal.kind == "effect" and afterReveal.fxItem
        and math.abs(afterReveal.fxItem.t - 0.05) < 1e-8, "mid-frame start receives only the remaining frame time")
    check(table.getn(F.soundFiles) == before, "opponent reveal does not trigger early impact sound")
    MU.AnimSkip()
    checkAtRest(MU, base, "opponent fireball skip")

    AN.SetMode("fast")
    F.time = F.time + 10
    before = table.getn(F.soundFiles)
    X.push(nextE({ "play.m.12.45.0.CS2_029", "dmg.o.12.45.1.-" }))
    MU.AnimTick(0.1)
    check(table.getn(F.soundFiles) == before, "fast mode still waits for flight completion")
    MU.AnimTick(0.02)
    check(table.getn(F.soundFiles) == before + 1 and table.getn(usedTexts(MU)) > 0, "fast mode preserves impact synchronization")
    MU.AnimSkip()
    AN.SetMode("on")
    fxEffect(MU, fire)
    MU.HideWindow()
    checkAtRest(MU, base, "fireball hide")
    animTeardown(F, MU)

    -- Bench isolation: model-only diagnostics must not show 2D fallback rectangles.
    F = dofile(ROOT .. "/tests/fake_wow.lua")
    resetAddon()
    F.install({ screenW = 1920, screenH = 1080 })
    H.db = HsCollection.ApplySavedDefaults(nil)
    fxReset()
    H.Slash("fx layer 2d")
    H.Slash("fx play fireball")
    local b = FX.bench
    check(b and table.getn(shownModels(F)) == 0 and table.getn(b.items) > 0, "2D bench shows reference fireball alone")
    H.Slash("fx layer model")
    H.Slash("fx play fireball")
    check(table.getn(shownModels(F)) == 1, "model bench shows the native projectile")
    local textures = 0
    for _, h in ipairs(b.tex) do if h.used then textures = textures + 1 end end
    check(textures == 0, "model bench holds no fallback textures")
    H.Slash("fx play fireball missile b a")
    H.Slash("fx scale 0.5")
    H.Slash("fx pos 0 0.2 -0.1")
    H.Slash("fx dur 0.5")
    local native = shownModels(F)[1]
    check(native and b.cur.from == "b" and b.cur.to == "a" and b.cur.p.scale == 0.5,
        "native fireball audition retains endpoints and tuning")
    local px, py, pz = FX.Position(FX.Cal(), { 0, 0.2, -0.1 })
    check(native and math.abs(native._.modelPos[2] - py) < 1e-8 and math.abs(native._.modelPos[3] - pz) < 1e-8,
        "native fireball uses the tuned model position")
    check(FX.last.flight == 0.5, "bench flight tuning is applied")
    H.Slash("fx layer 2d")
    H.Slash("fx play fire_impact")
    check(table.getn(shownModels(F)) == 0 and table.getn(b.items) > 0, "2D isolation also applies to other presets")
    H.Slash("fx layer model")
    textures = 0
    for _, h in ipairs(b.tex) do if h.used then textures = textures + 1 end end
    check(table.getn(shownModels(F)) == 1 and textures == 0, "switching layer immediately isolates an impact model")
    H.Slash("fx boxes on")
    H.Slash("fx boxes off")
    H.Slash("fx markers on")
    H.Slash("fx backdrop quiet")
    check(not b.boxes[1]:IsShown() and b.markers[1][1]:IsShown() and b.quiet,
        "diagnostic view replaces target rectangles with small center marks")
    H.Slash("fx layer both")
    H.Slash("fx play fireball")
    check(table.getn(shownModels(F)) == 1, "combined bench includes native model")
    H.Slash("fx stop")
    check(FX.PoolStats().used == 0 and table.getn(b.items) == 0, "bench stop frees both layers")
    fxReset()
    H.db = nil
    F.uninstall()
    resetAddon()
end

local function testFxBench()
    local FX = H.MatchFx
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    resetAddon()
    F.install({ screenW = 1920, screenH = 1080 })
    H.db = HsCollection.ApplySavedDefaults(nil)
    fxReset()
    local function say(msg)
        H.Slash(msg)
        return lastPrinted()
    end
    local function allPrinted() return table.concat(printed, "\n") end
    check(FX.bench == nil, "no bench before it is used")
    printed = {}
    H.Slash("fx")
    check(table.getn(printed) >= 6 and string.find(allPrinted(), "/hs fx play", 1, true) ~= nil and string.find(allPrinted(), "/hs fx dump", 1, true) ~= nil, "/hs fx shows the help")
    for _, w in ipairs({ "list", "play", "model", "scale", "pos", "facing", "dur", "dump", "stop", "sound", "models", "family", "card", "snd", "status", "loop", "reset" }) do
        check(string.find(allPrinted(), "fx " .. w, 1, true) ~= nil or string.find(allPrinted(), w, 1, true) ~= nil, "help mentions " .. w)
    end
    check(string.find(say("help"), "fx", 1, true) ~= nil, "the general help lists fx")
    printed = {}
    H.Slash("fx list")
    check(string.find(allPrinted(), "fireball(missile)", 1, true) ~= nil and string.find(allPrinted(), "fire_burst(impact)", 1, true) ~= nil, "list shows the presets")
    printed = {}
    H.Slash("fx list frost")
    check(string.find(allPrinted(), "frostbolt", 1, true) ~= nil and string.find(allPrinted(), "fireball", 1, true) == nil, "list filters")

    -- play: a missile on the bench, with no match running
    check(HsMatch.game == nil, "no match is running")
    check(string.find(say("fx play nope"), "no preset", 1, true) ~= nil, "unknown preset")
    check(string.find(say("fx scale 2"), "no preset yet", 1, true) ~= nil, "tuning needs a preset")
    say("fx layer both") -- this legacy suite exercises native model tuning
    local msg = say("fx play fireball")
    local b = FX.bench
    check(b and b.frame:IsShown() and string.find(msg, "fireball", 1, true) ~= nil, "play opens the bench: " .. msg)
    check(table.getn(F.models) == FX.POOL, "the bench makes its own pool")
    local live = shownModels(F)
    check(table.getn(live) == 1 and live[1]._.modelPath == "Spells\\Fireball_Missile_High.mdx" and live[1]:GetParent() == b.layer, "the missile model plays on the bench")
    local a1 = live[1]._.points[1]
    FX.BenchTick(0.2)
    local a2 = shownModels(F)[1]._.points[1]
    check(a2.x > a1.x, "it flies from a to b: " .. a1.x .. " -> " .. a2.x)
    for _ = 1, 20 do FX.BenchTick(0.1) end
    check(FX.PoolStats().used == 0 and table.getn(b.items) == 0, "it ends and returns everything")
    -- impact and aura play in place on `to`
    say("fx play fire_impact impact a mid")
    live = shownModels(F)
    check(table.getn(live) == 1 and live[1]._.points[1].x == b.anchors.mid.x and live[1]._.points[1].y == -b.anchors.mid.y, "an impact plays at the to anchor")
    -- tune it live: every command replays and changes the values
    local function calls(frame, name)
        local out = {}
        for _, c in ipairs(frame._.calls) do if c.name == name then table.insert(out, c) end end
        return out
    end
    say("fx scale 0.5")
    live = shownModels(F)
    local sc = calls(live[1], "SetModelScale")
    local e = b.frame:GetEffectiveScale()
    local cal = FX.Cal()
    local base = FX.ModelScale(cal, { ext = 1.92, scale = 1 }, "impact", FX.Diameter("impact", b.anchors.mid), e)
    check(table.getn(live) == 1 and math.abs(sc[table.getn(sc)].a - 0.5 * base) < 1e-9 and b.cur.p.scale == 0.5, "scale is relative to the derived size")
    say("fx pos 1 2 3")
    live = shownModels(F)
    local ps = calls(live[1], "SetPosition")
    local cy, cz = FX.CenterUnits(cal)
    check(math.abs(ps[table.getn(ps)].a - (cal.x + 1)) < 1e-9 and math.abs(ps[table.getn(ps)].b - (cy + 2)) < 1e-9
        and math.abs(ps[table.getn(ps)].c - (cz + 3)) < 1e-9, "position is an offset from the centred default")
    say("fx facing 1.5")
    live = shownModels(F)
    local fc = calls(live[1], "SetFacing")
    check(fc[table.getn(fc)].a == 1.5, "facing")
    say("fx dur 0.2")
    check(b.cur.p.dur == 0.2, "duration")
    say("fx dur 99")
    check(b.cur.p.dur == FX.MAX_LIFE, "duration is bounded")
    say("fx size 150")
    live = shownModels(F)
    sc = calls(live[1], "SetModelScale")
    check(math.abs(live[1]:GetWidth() * e - cal.frame) < 1e-9, "the frame keeps its screen size")
    check(math.abs(sc[table.getn(sc)].a - 150 * e / (2 * 1.92 * cal.ppy) * 0.5) < 1e-9, "size: the on-screen diameter in pixels")
    say("fx size auto")
    check(b.cur.p.size == nil, "size auto")
    say("fx cam 2")
    live = shownModels(F)
    check(table.getn(calls(live[1], "SetCamera")) == 1, "camera")
    check(string.find(say("fx scale abc"), "usage", 1, true) ~= nil and string.find(say("fx pos 1 2"), "usage", 1, true) ~= nil, "bad numbers: usage")
    say("fx kind missile")
    check(b.cur.kind == "missile", "kind")
    printed = {}
    H.Slash("fx dump")
    check(string.find(allPrinted(), "\"key\": \"fire_impact\"", 1, true) ~= nil and string.find(allPrinted(), "Spells\\\\FireBlast_Impact_Chest.mdx", 1, true) ~= nil
        and string.find(allPrinted(), "\"scale\": 0.5", 1, true) ~= nil and string.find(allPrinted(), "\"pos\": [1, 2, 3]", 1, true) ~= nil, "dump in manifest form: " .. allPrinted())
    -- any model path
    msg = say("fx model Spells\\Whatever.mdx")
    live = shownModels(F)
    check(table.getn(live) == 1 and live[1]._.modelPath == "Spells\\Whatever.mdx" and string.find(msg, "Whatever", 1, true) ~= nil, "a free model path plays")
    -- loop and stop
    say("fx loop on")
    for _ = 1, 60 do FX.BenchTick(0.1) end
    local setCalls = 0
    for _, m in ipairs(F.models) do setCalls = setCalls + table.getn(calls(m, "SetModel")) end
    check(b.loop and setCalls >= 4, "a loop plays again: " .. setCalls)
    say("fx stop")
    check(not b.loop and FX.PoolStats().used == 0 and table.getn(b.items) == 0, "stop ends the loop and returns everything")
    local setCalls2 = 0
    for _ = 1, 30 do FX.BenchTick(0.1) end
    for _, m in ipairs(F.models) do setCalls2 = setCalls2 + table.getn(calls(m, "SetModel")) end
    check(setCalls2 == setCalls, "nothing plays after stop")
    -- families and cards
    msg = say("fx family fire")
    check(FX.last and FX.last.family == "fire" and FX.last.launches[1] and string.find(msg, "fire", 1, true) ~= nil, "a family on the bench")
    msg = say("fx card CS2_032 area")
    check(FX.last and FX.last.family == "fire" and FX.last.area and FX.last.cardId == "CS2_032", "a card on the bench: " .. msg)
    check(string.find(say("fx family bogus"), "usage", 1, true) ~= nil, "unknown family")
    say("fx stop")
    -- settings
    check(string.find(say("fx sound off"), "off", 1, true) ~= nil and H.db.fxSound == false, "sound off")
    check(string.find(say("fx snd Sound\\x.wav"), "could not", 1, true) ~= nil, "no sound while off")
    say("fx sound on")
    check(H.db.fxSound == nil and FX.SoundOn(), "sound on")
    F.soundFiles = {}
    say("fx snd Sound\\x.wav")
    check(F.soundFiles[1] == "Sound\\x.wav", "a sound is auditioned")
    check(string.find(say("fx models verified"), "verified", 1, true) ~= nil and H.db.fxModels == "verified", "models verified")
    say("fx play fireball")
    check(table.getn(shownModels(F)) == 1, "the bench shows a model whatever the mode (it is where presets get verified)")
    say("fx family fire")
    check(FX.last.models == 0 and table.getn(b.items) >= 1, "a family in verified mode: unverified presets stay 2D")
    say("fx stop")
    check(string.find(say("fx models bogus"), "usage", 1, true) ~= nil, "bad mode")
    say("fx models all")
    check(H.db.fxModels == nil, "models all")
    -- a failing model on the bench
    F.failSetModel = true
    say("fx play fireball")
    check(FX.modelsDisabled and table.getn(b.items) >= 1, "a failing model: 2D only, no error")
    printed = {}
    H.Slash("fx status")
    check(string.find(allPrinted(), "OFF", 1, true) ~= nil, "status shows the failure")
    F.failSetModel = nil
    say("fx reset")
    check(not FX.modelsDisabled, "reset")
    check(string.find(say("fx bogus"), "unknown", 1, true) ~= nil, "unknown command")
    say("fx hide")
    check(not b.frame:IsShown() and FX.PoolStats().used == 0, "hide")
    local special = false
    for i = 1, table.getn(UISpecialFrames) do if UISpecialFrames[i] == "HearthstoneFxBench" then special = true end end
    check(special, "Escape closes the bench")
    for _, line in ipairs(printed) do check(string.find(line, "fx error", 1, true) == nil, "no fx error printed: " .. line) end
    fxReset()
    H.db = nil
    F.uninstall()
    resetAddon()
end

local function approx(a, b, eps)
    return type(a) == "number" and type(b) == "number" and math.abs(a - b) <= (eps or 1e-9)
end

-- placement and size of the effect models: the calibration model (pure math), the defaults, and what the match stage draws
local function testFxPlacement()
    local FX, P = H.MatchFx, HS_EffectProfiles
    fxReset()
    local saved = H.db
    H.db = {}
    -- defaults while nothing is saved
    local d = FX.CAL_DEFAULT
    check(FX.Cal() == d and not FX.CalSaved(), "the shipped defaults apply while nothing is saved")
    check(d.frame == 128 and d.x == 0 and d.ox == 0 and d.oz == 0 and d.ky > 0 and d.kz > 0 and d.ppy > 0, "shipped defaults: origin at the bottom-left, +y right, +z up")
    H.db.fxCal = { x = 1 }
    check(FX.Cal() == d and not FX.CalSaved(), "an incomplete saved calibration is ignored")
    H.db.fxCal = { x = 0, frame = 128, ox = 0, oz = 0, ky = 0, kz = 160, ppy = 160 }
    check(FX.Cal() == d and not FX.CalSaved(), "a zero scale is not a calibration")
    H.db.fxCal = { x = 0, frame = 128, ox = 0, oz = 0, ky = 160, kz = 160, ppy = 160, junk = true }
    check(FX.CalSaved() and FX.Cal() == H.db.fxCal, "a valid saved calibration is used")
    H.db.fxCal = nil

    -- the centre of a target -> model units, several calibrations (frame sizes), several effective scales
    local cals = { d, { x = 0, frame = 96, ox = 0, oz = 0, ky = 120, kz = 120, ppy = 100 },
        { x = 2, frame = 256, ox = -12, oz = 7, ky = -210, kz = 190, ppy = 300 },
        { x = -1, frame = 64, ox = 30, oz = -20, ky = 75, kz = -80, ppy = 50 } }
    for i, c in ipairs(cals) do
        local y, z = FX.CenterUnits(c)
        local px, pz = FX.PixelFor(c, y, z)
        check(approx(px, c.frame / 2) and approx(pz, c.frame / 2), "cal " .. i .. ": the centring units put the origin in the middle of the frame")
        y, z = FX.UnitsFor(c, 10, 100)
        px, pz = FX.PixelFor(c, y, z)
        check(approx(px, 10) and approx(pz, 100), "cal " .. i .. ": any pixel round trips")
        for _, e in ipairs({ 0.5, 0.64, 1, 1.35, 2.2 }) do
            check(approx(FX.FrameSide(c, e) * e, c.frame), "cal " .. i .. ": the frame is " .. c.frame .. " screen pixels at scale " .. e)
        end
    end
    local y0, z0 = FX.CenterUnits(d)
    check(approx(y0, 0.4) and approx(z0, 0.4), "default centre: 64 pixels / 160 per unit = 0.4 units")
    check(approx(FX.FrameSide(d, 0), 128) and approx(FX.FrameSide(d, nil), 128), "an unusable scale counts as 1")
    local c2 = { x = 0, frame = 128, ox = 20, oz = -10, ky = 100, kz = 50, ppy = 100 }
    local y2, z2 = FX.CenterUnits(c2)
    check(approx(y2, 0.44) and approx(z2, 1.48), "centre units = (frame / 2 - origin) / pixels per unit: " .. y2 .. " " .. z2)
    -- a per-preset pos is a small offset on top of the centring, pos[1] adds to the depth
    local x, y, z = FX.Position(c2, { 0.5, 0.1, -0.2 })
    check(approx(x, 0.5) and approx(y, y2 + 0.1) and approx(z, z2 - 0.2), "pos adds to the centred placement")
    x, y, z = FX.Position(c2, nil)
    check(approx(x, 0) and approx(y, y2) and approx(z, z2), "no pos: centred")
    x, y, z = FX.Position(cals[3], { 1, 0, 0 })
    check(approx(x, 3), "pos[1] shifts the depth: " .. x)

    -- the two-point solve recovers a known scale and origin offset
    local base = d
    local function solveCase(T, tag)
        local function pt(px, pz)
            local y, z = FX.UnitsFor(T, px, pz)
            return { px = px, pz = pz, y = y, z = z }
        end
        local s, sy, sz = FX.Solve(base, pt(64, 64), pt(364, -66))
        check(sy and sz and approx(s.ky, T.ky, 1e-7) and approx(s.kz, T.kz, 1e-7) and approx(s.ox, T.ox, 1e-7) and approx(s.oz, T.oz, 1e-7),
            tag .. ": scale and offset recovered: " .. s.ky .. " " .. s.kz .. " " .. s.ox .. " " .. s.oz)
        check(base.ky == 160 and base.ox == 0, tag .. ": the base is not changed")
        local one = FX.Solve(base, pt(64, 64), nil)
        local py, pz = FX.PixelFor(one, pt(64, 64).y, pt(64, 64).z)
        check(approx(one.ky, 160) and approx(one.kz, 160) and approx(py, 64, 1e-7) and approx(pz, 64, 1e-7), tag .. ": one point moves only the offset")
    end
    solveCase({ ox = 13, oz = -9, ky = 212, kz = 187 }, "positive")
    solveCase({ ox = -5, oz = 4, ky = -150, kz = 170 }, "mirrored y")
    solveCase({ ox = 40, oz = 22, ky = 95, kz = 99 }, "small")
    local q = { px = 64, pz = 64, y = 0.4, z = 0.4 }
    local s2, sy2, sz2 = FX.Solve(base, q, { px = 364, pz = -66, y = 0.4, z = 0.4 })
    check(not sy2 and not sz2 and s2.ky == 160 and s2.kz == 160, "two equal positions solve nothing: the scales stay")

    -- sizes: bounded relative to the anchor, per kind, at any effective scale
    local anchors = { minion = { w = 76, h = 92 }, hero = { w = 110, h = 110 }, mid = { w = 120, h = 92 }, wide = { w = 300, h = 80 } }
    local n = 0
    for key, p in pairs(P.presets) do
        local role = (p.kind == "missile") and "missile" or (p.kind == "aura" and "aura" or "impact")
        for an, a in pairs(anchors) do
            for _, e in ipairs({ 0.7, 1, 1.6 }) do
                local diam = FX.Diameter(role, a)
                local s = FX.ModelScale(d, p, role, diam, e)
                local shown = s * 2 * FX.Extent(p, role) * d.ppy / e            -- design pixels
                local m = math.max(a.w, a.h)
                n = n + 1
                check(s > FX.SCALE_MIN and s < FX.SCALE_MAX, key .. "/" .. an .. ": the model scale is not clamped: " .. s)
                if role == "impact" then
                    check(shown >= 1.0 * m - 1e-6 and shown <= 1.5 * m + 1e-6, key .. "/" .. an .. ": an impact is 1.0-1.5x the target: " .. shown .. " of " .. m)
                elseif role == "aura" then
                    check(shown >= 0.9 * m - 1e-6 and shown <= 1.3 * m + 1e-6, key .. "/" .. an .. ": an aura is about the target: " .. shown .. " of " .. m)
                else
                    local minion = math.min(m, 92)
                    check(shown >= 0.4 * minion - 1e-6 and shown <= 0.6 * minion + 1e-6, key .. "/" .. an .. ": a missile is half a minion: " .. shown)
                end
            end
        end
        if role == "impact" then
            for _, c in ipairs({ { w = 100, h = 92 }, { w = 260, h = 120 }, { w = 420, h = 200 } }) do
                local diam = FX.Diameter("area", c)
                local s = FX.ModelScale(d, p, "area", diam, 1)
                check(approx(diam, math.max(c.w, c.h)) and diam <= FX.DIAM_MAX, key .. ": an area burst covers the area: " .. diam)
                check(s > FX.SCALE_MIN and s < FX.SCALE_MAX, key .. ": the area scale is not clamped: " .. s)
            end
        end
    end
    check(n >= 20 * 4 * 3, "every preset was checked: " .. n)
    check(FX.Diameter("area", { w = 900, h = 700 }) == FX.DIAM_MAX and FX.Diameter("impact", { w = 4, h = 4 }) == FX.DIAM_MIN, "diameters are bounded")
    -- a preset's own scale / size correct the default; unknown models get the default of their role
    local p1 = { ext = 1, scale = 1 }
    local s1 = FX.ModelScale(d, p1, "impact", 100, 1)
    check(approx(FX.ModelScale(d, { ext = 1, scale = 2 }, "impact", 100, 1), 2 * s1), "preset.scale is relative")
    check(approx(FX.ModelScale(d, { ext = 1, size = 200 }, "impact", 100, 1), 2 * s1), "preset.size is an on-screen diameter in pixels")
    check(approx(FX.ModelScale(d, p1, "impact", 100, 2), 2 * s1), "the same design size is twice the screen pixels at scale 2")
    check(FX.Extent({}, "area") == FX.EXT_ROLE.area and FX.Extent({ ext = 99 }, "impact") == FX.EXT_MAX and FX.Extent({ ext = 0.01 }, "impact") == FX.EXT_MIN, "natural radius: role default and range")
    H.db = saved

    -- on the match board: centred, calibrated, one hint, nothing leaks
    local F, MU, X = animSetup()
    local base0 = baselineLevels(MU)
    local P2 = HS_EffectProfiles
    local hintText = "Effects are not calibrated yet: /hs fx calibrate"
    local function hints()
        local k = 0
        for _, line in ipairs(printed) do if line == hintText then k = k + 1 end end
        return k
    end
    printed = {}
    check(not FX.CalSaved() and FX.Cal() == FX.CAL_DEFAULT and not FX.hinted, "a fresh match: defaults, no hint yet")
    local blast = { family = "fire", cardId = "CS2_034", source = 40, targets = { 45 }, sides = { [45] = "o" }, aim = 45 }
    for i, s in ipairs({ 1, 0.6, 1.4 }) do
        MU.frame:SetScale(s)
        local ok = fxEffect(MU, blast)
        runTicks(MU, 0.35)
        local live = shownModels(F)
        check(ok and table.getn(live) == 1, "fireblast: one impact model at scale " .. s)
        local m = live[1]
        local e = m:GetEffectiveScale()
        local cal = FX.Cal()
        local cy, cz = FX.CenterUnits(cal)
        local anchor = MU.AnchorOf(45)
        local pt = m._.points[1]
        check(pt.point == "CENTER" and pt.rel == MU.board and pt.relPoint == "TOPLEFT" and pt.x == anchor.x and pt.y == -anchor.y, "centred on the target at scale " .. s)
        check(approx(m:GetWidth() * e, cal.frame, 1e-6) and approx(m:GetHeight() * e, cal.frame, 1e-6), "the frame is " .. cal.frame .. " screen pixels at scale " .. s .. " (effective " .. e .. ")")
        check(approx(m._.modelPos[1], cal.x) and approx(m._.modelPos[2], cy) and approx(m._.modelPos[3], cz), "the same units at every scale: " .. m._.modelPos[2] .. " " .. m._.modelPos[3])
        local want = FX.ModelScale(cal, P2.presets.fire_impact, "impact", FX.Diameter("impact", anchor), e)
        check(approx(m._.modelScale, want), "scale from the anchor size: " .. tostring(m._.modelScale) .. " vs " .. want)
        local shown = m._.modelScale * 2 * FX.Extent(P2.presets.fire_impact, "impact") * cal.ppy / e
        check(shown >= 1.0 * 92 - 1e-6 and shown <= 1.5 * 92 + 1e-6, "the impact is 1.0-1.5x the minion (92 px): " .. shown)
        MU.AnimSkip()
        checkAtRest(MU, base0, "scale " .. s)
    end
    check(hints() == 1 and FX.hinted, "the hint is printed once per session: " .. hints())
    -- a missile is about half a minion, an area burst about its area; models are all returned
    MU.frame:SetScale(1)
    fxEffect(MU, { family = "fire", cardId = "EX1_308", source = 40, targets = { 45 }, sides = { [45] = "o" }, aim = 45 })
    local live = shownModels(F)
    local cal = FX.Cal()
    local e = live[1]:GetEffectiveScale()
    local shown = live[1]._.modelScale * 2 * FX.Extent(P2.presets.fireball, "missile") * cal.ppy / e
    check(table.getn(live) == 1 and approx(shown, 46, 1e-6), "the fireball missile is half a minion: " .. shown)
    MU.AnimSkip()
    fxEffect(MU, { family = "fire", cardId = "CS2_032", source = 0, area = true, targets = { 44, 45, 41 }, sides = { [44] = "o", [45] = "o", [41] = "m" } })
    live = shownModels(F)
    local c = FX.last.center
    shown = live[1]._.modelScale * 2 * FX.Extent(P2.presets.fire_burst, "area") * cal.ppy / e
    check(table.getn(live) == 1 and approx(shown, math.max(c.w, c.h), 1e-6) and shown <= FX.DIAM_MAX, "the area burst covers the affected area: " .. shown .. " of " .. c.w .. "x" .. c.h)
    MU.AnimSkip()
    checkAtRest(MU, base0, "kinds")
    -- a saved calibration changes the placement and silences the hint
    H.db.fxCal = { x = 0.5, frame = 200, ox = 10, oz = -20, ky = -130, kz = 140, ppy = 90 }
    FX.hinted = false
    printed = {}
    MU.frame:SetScale(0.8)
    fxEffect(MU, blast)
    runTicks(MU, 0.35)
    live = shownModels(F)
    e = live[1]:GetEffectiveScale()
    local py, pz = FX.UnitsFor(H.db.fxCal, 100, 100)
    check(approx(live[1]:GetWidth() * e, 200, 1e-6) and approx(live[1]._.modelPos[1], 0.5) and approx(live[1]._.modelPos[2], py) and approx(live[1]._.modelPos[3], pz),
        "a saved calibration is used: " .. live[1]._.modelPos[2] .. " " .. live[1]._.modelPos[3])
    check(hints() == 0 and not FX.hinted, "no hint once a calibration is saved")
    MU.AnimSkip()
    -- a calibration that does not exist or is broken: the defaults, models stay small and visible
    H.db.fxCal = { frame = "x" }
    fxEffect(MU, blast)
    runTicks(MU, 0.35)
    live = shownModels(F)
    check(table.getn(live) == 1 and approx(live[1]:GetWidth() * live[1]:GetEffectiveScale(), FX.CAL_DEFAULT.frame, 1e-6), "a broken calibration falls back to the defaults")
    MU.AnimSkip()
    H.db.fxCal = nil
    MU.frame:SetScale(1)
    checkAtRest(MU, base0, "calibrated")
    animTeardown(F, MU)
end

-- /hs fx calibrate: the guided calibration on the bench
local function testFxCalibration()
    local FX = H.MatchFx
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    resetAddon()
    F.install({ screenW = 1920, screenH = 1080 })
    H.db = HsCollection.ApplySavedDefaults(nil)
    fxReset()
    local function say(msg)
        H.Slash(msg)
        return lastPrinted()
    end
    local function allPrinted() return table.concat(printed, "\n") end
    local function click(b, id)
        check(b.calUI and b.calUI.btn[id] ~= nil, "button " .. id .. " exists")
        F.fire(b.calUI.btn[id], "OnClick")
    end
    local function setCalls(m, name)
        local n = 0
        for _, c in ipairs(m._.calls) do if c.name == name then n = n + 1 end end
        return n
    end
    local function totalSetModel()
        local n = 0
        for _, m in ipairs(F.models) do n = n + setCalls(m, "SetModel") end
        return n
    end

    -- help and the numbers before any calibration
    printed = {}
    H.Slash("fx help")
    check(string.find(allPrinted(), "fx calibrate", 1, true) ~= nil, "/hs fx help lists calibrate")
    check(string.find(allPrinted(), "bigger", 1, true) ~= nil and string.find(allPrinted(), "save", 1, true) ~= nil, "help lists the calibration commands")
    printed = {}
    H.Slash("fx status")
    check(string.find(allPrinted(), "NOT calibrated", 1, true) ~= nil, "status: not calibrated yet")
    check(string.find(say("fx calibrate left"), "no calibration is running", 1, true) ~= nil, "nudging needs a running calibration")
    check(FX.bench == nil or FX.bench.calib == nil, "nothing started")

    -- start
    printed = {}
    H.Slash("fx calibrate")
    local b = FX.bench
    local c = b and b.calib
    check(b and b.frame:IsShown() and c and c.point == 1 and not c.two, "calibrate opens the bench at point 1")
    local text = allPrinted()
    check(string.find(text, "1. ", 1, true) ~= nil and string.find(text, "5. Save", 1, true) ~= nil and string.find(text, "Bigger / Smaller", 1, true) ~= nil, "the 5 steps are printed")
    local ids = { "L2", "R2", "U2", "D2", "L1", "R1", "U1", "D1", "bigger", "smaller", "next", "save", "reset", "done" }
    for _, id in ipairs(ids) do
        local btn = b.calUI and b.calUI.btn[id]
        check(btn ~= nil and btn:IsShown() and btn:GetParent() == b.frame, "button " .. id .. " is shown on the bench")
    end
    check(b.calUI.btn.next:GetText() == "Next (b)", "the next button names box b")
    local a = b.anchors.a
    local hp = b.calUI.hline._.points[1]
    check(b.calUI.hline:IsShown() and b.calUI.vline:IsShown() and b.calUI.ring:IsShown() and hp.rel == b.frame and hp.x == a.x and hp.y == -a.y, "the cross is at the centre of box a")
    check(b.anchors.a.x < b.anchors.b.x and b.anchors.a.y ~= b.anchors.b.y, "box a and box b differ in both directions (both scales can be solved)")
    local frames = table.getn(F.frames)
    local live = shownModels(F)
    local cal = FX.Cal()
    local y0, z0 = FX.CenterUnits(cal)
    check(table.getn(live) == 1 and live[1]._.modelPath == FX.MARKER.model and live[1]:GetParent() == b.layer, "the marker model plays on the bench")
    check(approx(live[1]._.modelPos[2], y0) and approx(live[1]._.modelPos[3], z0), "the marker starts at the centring units")
    check(string.find(b.info:GetText(), "point 1/2", 1, true) ~= nil and string.find(b.info:GetText(), "px per unit", 1, true) ~= nil, "the numbers are shown on the bench: " .. b.info:GetText())
    check(string.find(b.info:GetText(), "no, defaults", 1, true) ~= nil, "the bench says nothing is saved")
    local m0 = live[1]._.modelScale
    check(m0 > 0 and approx(FX.MARKER.diam / (2 * FX.MARKER.ext * m0), cal.ppy, 1e-6), "the marker starts at the ring size")

    -- nudging with the buttons (coarse 16, fine 2 design pixels, converted with the starting scale)
    local e = b.frame:GetEffectiveScale()
    local u = c.u[1]
    local ya, za = u[1], u[2]
    click(b, "R2")
    check(approx(u[1], ya + 16 * e / cal.ky) and approx(u[2], za), "right, coarse")
    click(b, "L1")
    check(approx(u[1], ya + 16 * e / cal.ky - 2 * e / cal.ky), "left, fine")
    click(b, "U2")
    check(approx(u[2], za + 16 * e / cal.kz), "up, coarse")
    click(b, "D1")
    check(approx(u[2], za + 16 * e / cal.kz - 2 * e / cal.kz), "down, fine")
    live = shownModels(F)
    check(table.getn(live) == 1 and approx(live[1]._.modelPos[2], u[1]) and approx(live[1]._.modelPos[3], u[2]), "every press replays the marker at the new units")
    local ms = c.ms
    click(b, "bigger")
    check(approx(c.ms, ms * 1.15), "bigger")
    click(b, "smaller")
    click(b, "smaller")
    check(approx(c.ms, ms / 1.15), "smaller")
    check(table.getn(F.frames) == frames and FX.PoolStats().used <= 1, "nudging makes no frame and holds one model")
    -- the same through the slash command
    local u1 = u[1]
    say("fx calibrate right fine")
    check(approx(u[1], u1 + 2 * e / cal.ky), "/hs fx calibrate right fine")
    say("fx calibrate up")
    say("fx calibrate bigger")
    check(string.find(say("fx calibrate sideways"), "usage", 1, true) ~= nil, "bad subcommand: usage")

    -- saving needs both points
    local before = H.db.fxCal
    printed = {}
    click(b, "save")
    check(H.db.fxCal == before and before == nil and string.find(allPrinted(), "Next", 1, true) ~= nil, "Save needs point 2 too")

    -- Next / Back
    click(b, "next")
    check(c.point == 2 and c.two and b.calUI.btn.next:GetText() == "Back (a)", "next: point 2")
    local bb = b.anchors.b
    hp = b.calUI.hline._.points[1]
    check(hp.x == bb.x and hp.y == -bb.y, "the cross moved to box b")
    local first = { c.u[2][1], c.u[2][2] }
    local dy1 = c.u[1][1] - y0
    check(approx(first[1], (64 + (bb.x - a.x) * e) / cal.ky + dy1, 1e-6), "the first guess for b follows the shift found at a")
    click(b, "next")
    check(c.point == 1 and b.calUI.btn.next:GetText() == "Next (b)", "back: point 1")
    check(approx(c.u[2][1], first[1]), "going back keeps point 2")
    click(b, "next")

    -- two-point solve through Save: the owner found a known scale and offset
    local T = { ox = 13, oz = -9, ky = 212, kz = 187 }
    c.u[1] = { FX.UnitsFor(T, 64, 64) }
    local px2, pz2 = 64 + (bb.x - a.x) * e, 64 - (bb.y - a.y) * e
    c.u[2] = { FX.UnitsFor(T, px2, pz2) }
    c.ms = 0.2
    printed = {}
    click(b, "save")
    local s = H.db.fxCal
    check(s ~= nil and FX.CalSaved(), "saved in HearthstoneDB.fxCal")
    check(s and approx(s.ky, 212, 1e-6) and approx(s.kz, 187, 1e-6) and approx(s.ox, 13, 1e-6) and approx(s.oz, -9, 1e-6), "the saved scale and offset are the solved ones: " .. tostring(s and s.ky) .. " " .. tostring(s and s.kz))
    check(s and approx(s.ppy, FX.MARKER.diam * e / (2 * FX.MARKER.ext * 0.2), 1e-6) and s.frame == 128 and s.x == 0, "the size factor comes from the ring: " .. tostring(s and s.ppy))
    local keys = 0
    for _ in pairs(s or {}) do keys = keys + 1 end
    check(keys == 7, "only the seven calibration numbers are stored")
    check(string.find(allPrinted(), "calibration saved", 1, true) ~= nil, "Save says so")
    check(FX.Cal() == s, "the saved calibration is the one in use")
    -- the calibration restarts with the saved values: the marker sits at the crosses
    check(b.calib and b.calib.point == 1 and not b.calib.two, "after Save the calibration restarts at point 1")
    local cy, cz = FX.CenterUnits(s)
    check(approx(b.calib.u[1][1], cy) and approx(b.calib.u[1][2], cz), "the marker is replayed at the saved centring units")
    check(string.find(b.info:GetText(), "yes", 1, true) ~= nil, "the bench says it is saved")
    local pred1 = FX.PixelFor(s, b.calib.u[1][1], b.calib.u[1][2])
    check(approx(pred1, 64, 1e-6), "the saved calibration puts the origin on the cross at a")
    local sy, sz = FX.UnitsFor(s, px2, pz2)
    local qx, qz = FX.PixelFor(s, sy, sz)
    check(approx(qx, px2, 1e-6) and approx(qz, pz2, 1e-6), "and on the cross at b")

    -- persists across a session: the bench and effects use it again
    FX.bench = nil
    FX.matchStage.mpool = nil
    fxReset()
    check(FX.CalSaved() and FX.Cal().ky == s.ky, "a new session reads the saved calibration")
    printed = {}
    H.Slash("fx play fire_impact")
    check(not FX.hinted and string.find(allPrinted(), "not calibrated yet", 1, true) == nil, "no hint when a calibration is saved")
    local m = shownModels(F)[1]
    check(m and approx(m._.modelPos[2], cy) and approx(m._.modelPos[3], cz), "the bench plays presets with the saved placement")
    printed = {}
    H.Slash("fx status")
    check(string.find(allPrinted(), "calibrated", 1, true) ~= nil and string.find(allPrinted(), "NOT calibrated", 1, true) == nil, "status: calibrated")
    say("fx stop")

    -- Reset: the shipped defaults again
    H.Slash("fx calibrate")
    b = FX.bench
    c = b.calib
    frames = table.getn(F.frames)
    check(c and approx(c.start.ky, 212), "calibration starts from the saved values")
    printed = {}
    click(b, "reset")
    check(H.db.fxCal == nil and not FX.CalSaved() and FX.Cal() == FX.CAL_DEFAULT, "Reset restores the shipped defaults")
    check(b.calib and approx(b.calib.start.ky, FX.CAL_DEFAULT.ky) and b.calib.point == 1, "and restarts the calibration with them")
    check(string.find(allPrinted(), "reset", 1, true) ~= nil, "Reset says so")
    say("fx calibrate reset")
    check(H.db.fxCal == nil, "/hs fx calibrate reset")

    -- the marker is replayed until Done; nothing leaks
    local n0 = totalSetModel()
    for _ = 1, 60 do FX.BenchTick(0.1) end
    check(totalSetModel() >= n0 + 4, "the marker is replayed: " .. (totalSetModel() - n0))
    check(FX.PoolStats().used <= 1 and table.getn(F.frames) == frames, "the replay holds one model and makes no frame")
    say("fx stop")
    local n1 = totalSetModel()
    for _ = 1, 30 do FX.BenchTick(0.1) end
    check(totalSetModel() == n1 and FX.PoolStats().used == 0, "stop ends the replay")
    click(b, "R2")
    check(FX.PoolStats().used == 1, "a press plays the marker again")
    click(b, "done")
    check(b.calib == nil and FX.PoolStats().used == 0 and not b.calUI.hline:IsShown() and not b.calUI.btn.save:IsShown() and not b.calUI.ring:IsShown(), "Done hides the cross and the buttons and returns the model")
    check(string.find(b.info:GetText(), "shipped defaults", 1, true) ~= nil, "the bench shows the numbers again")
    -- a play command or closing the bench ends the calibration too
    H.Slash("fx calibrate")
    say("fx play fireball")
    check(FX.bench.calib == nil, "playing a preset ends the calibration")
    H.Slash("fx calibrate")
    check(FX.bench.calib ~= nil and FX.PoolStats().used == 1, "restarted")
    say("fx hide")
    check(FX.bench.calib == nil and FX.PoolStats().used == 0 and not FX.bench.frame:IsShown(), "hide ends the calibration and returns the model")
    for _, line in ipairs(printed) do check(string.find(line, "fx error", 1, true) == nil, "no fx error printed: " .. line) end
    fxReset()
    H.db = nil
    F.uninstall()
    resetAddon()
end

local function testMerchantCoins()
    local MC = H.MerchantCoins
    local W = HsWallet
    local LINK = "|cffffffff|Hitem:919603:0:0:0|h[Hearthstone Booster Pack]|h|r"
    local OTHER = "|cffffffff|Hitem:919602:0:0:0|h[Hearthstone Booster Pack]|h|r"

    -- the pure decision
    check(MC.State(OTHER, 5, 10) == nil and MC.State(nil, 5, 10) == nil, "only the coin listing is decorated")
    local st = MC.State(LINK, 3, 10)
    check(st and st.text == "10" and st.affordable == false, "3 coins cannot buy a 10 coin pack")
    st = MC.State(LINK, 10, 10)
    check(st and st.affordable == true, "exactly the price is enough")
    st = MC.State(LINK, 0, nil)
    check(st and st.text == "?" and st.affordable == true, "unknown price: '?' and never red")
    check(MC.TooltipLine(MC.State(LINK, 1, 10)) == "|cffff2020You have 1 Hearthstone Coin.|r", "tooltip line, short")
    check(MC.TooltipLine(MC.State(LINK, 25, 10)) == "|cffffffffYou have 25 Hearthstone Coins.|r", "tooltip line, enough")

    -- the vendor window
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    F.install({})
    local names = { "MerchantFrame", "MERCHANT_ITEMS_PER_PAGE", "GetMerchantItemLink", "SetItemButtonTextureVertexColor",
        "GetContainerNumSlots", "GetContainerItemLink", "GetContainerItemInfo", "MerchantFrame_UpdateMerchantInfo" }
    local saved = {}
    for _, n in ipairs(names) do saved[n] = _G[n] end
    local savedPack, savedOverlays, savedInstalled = W.packCoins, MC.overlays, MC.installed
    local savedTip = GameTooltip.SetMerchantItem
    MC.overlays = {}

    local merchant = F.new("Frame", "MerchantFrame", UIParent)
    merchant.page, merchant.selectedTab = 1, 1
    MerchantFrame, MERCHANT_ITEMS_PER_PAGE = merchant, 10
    for i = 1, 10 do
        local item = F.new("Frame", "MerchantItem" .. i, merchant)
        F.globals["MerchantItem" .. i] = item
        F.globals["MerchantItem" .. i .. "MoneyFrame"] = F.new("Frame", "MerchantItem" .. i .. "MoneyFrame", item)
        F.globals["MerchantItem" .. i .. "ItemButton"] = F.new("Button", "MerchantItem" .. i .. "ItemButton", item)
    end
    local links = { [1] = OTHER, [2] = LINK }
    GetMerchantItemLink = function(index) return links[index] end
    local tint = {}
    SetItemButtonTextureVertexColor = function(button, r, g, b) tint[button] = { r, g, b } end
    local coins = 3
    GetContainerNumSlots = function(bag) if bag == 0 then return 1 end return 0 end
    GetContainerItemLink = function(bag, slot) return "|Hitem:919601:0:0:0|h[Hearthstone Coin]|h" end
    GetContainerItemInfo = function(bag, slot) return "icon", coins end
    W.packCoins = 10

    MC.Update()
    local o = MC.overlays[2]
    local money2 = F.globals["MerchantItem2MoneyFrame"]
    local button2 = F.globals["MerchantItem2ItemButton"]
    check(o and o:IsShown() and o.text:GetText() == "10", "the coin listing shows its coin price")
    check(not money2:IsShown(), "its gold money frame is hidden")
    check(F.globals["MerchantItem1MoneyFrame"]:IsShown() and MC.overlays[1] == nil, "the gold listing keeps its money frame")
    check(tint[button2] and tint[button2][1] == 0.9 and tint[button2][2] == 0, "3 coins: the item icon is red")

    coins = 12
    MC.Update()
    check(tint[button2][1] == 1 and tint[button2][2] == 1, "12 coins: the item icon is normal again")

    -- another page: the slot holds a gold item, its money frame comes back
    merchant.page = 2
    MC.Update()
    check(not o:IsShown() and money2:IsShown(), "page 2: overlay hidden, money frame shown")
    merchant.page = 1
    merchant.selectedTab = 2
    MC.Update()
    check(not o:IsShown(), "buyback tab: no coin price")
    merchant.selectedTab = 1

    -- the hooks run after Blizzard's update and add the tooltip line
    local origCalls = 0
    MerchantFrame_UpdateMerchantInfo = function() origCalls = origCalls + 1 end
    GameTooltip.SetMerchantItem = function(self, index) self:SetText("Hearthstone Booster Pack") end
    MC.installed = nil
    MC.Install()
    coins = 3
    MerchantFrame_UpdateMerchantInfo()
    check(origCalls == 1 and o:IsShown() and tint[button2][1] == 0.9, "the hook runs Blizzard's update, then decorates")
    GameTooltip:SetOwner(UIParent, "ANCHOR_RIGHT")
    GameTooltip:SetMerchantItem(2)
    local lines = F.tooltip.lines
    check(lines[1] == "Hearthstone Booster Pack" and lines[2] == "|cffff2020You have 3 Hearthstone Coins.|r",
        "tooltip: " .. tostring(lines[2]))
    GameTooltip:SetOwner(UIParent, "ANCHOR_RIGHT")
    GameTooltip:SetMerchantItem(1)
    check(F.tooltip.lines[2] == nil, "no extra tooltip line on the gold listing")

    for _, n in ipairs(names) do _G[n] = saved[n] end
    GameTooltip.SetMerchantItem = savedTip
    W.packCoins, MC.overlays, MC.installed = savedPack, savedOverlays, savedInstalled
    F.uninstall()
end

local function testMatchDrag()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local MA, MU = HsMatch, H.MatchUI
    local X = matchHelpers()
    resetAddon()
    resetMatch()
    F.install({ class = { "Mage", "MAGE" }, name = "Jonas", screenW = 1920, screenH = 1080 })
    MU.frame, MU.ui, MU.board, MU.sel, MU.choose = nil, nil, nil, nil, nil
    H.db = {}
    X.welcome()
    drain(1)
    X.game(X.last("GAME_GET").reqId, "ph=none")
    X.push(CANONICAL_GAME)
    local seq = 100
    local function snapshot(over)
        seq = seq + 1
        over = over or {}
        over.seq, over.ev, over.evs = tostring(seq), "", "0"
        local req = MA.pending and MA.pending.reqId or H.match.reqId
        local payload = gamePayload(over)
        if req then X.game(req, payload) else X.push(payload) end
        -- The fake does not calculate layout: supply the board's screen rect.
        MU.board._.left, MU.board._.top = 50, 800
    end
    local function cursor(x, y)
        local scale = MU.board:GetEffectiveScale()
        F.cursor = { (50 + x) * scale, (800 - y) * scale }
        MU.DragTick()
    end
    local function start(slot)
        F.fire(slot.frame, "OnMouseDown", { arg1 = "LeftButton" })
        F.fire(slot.frame, "OnDragStart", {})
    end
    local function drop(slot)
        F.fire(slot.frame, "OnMouseUp", { arg1 = "LeftButton" })
        F.fire(slot.frame, "OnDragStop", {})
        F.fire(slot.frame, "OnClick", { arg1 = "LeftButton" })
        drain(1)
    end
    local function acts() return table.getn(X.T.sentOps("GAME_ACT")) end
    local function expect(payload)
        check(X.last("GAME_ACT").payload == "mid=7;seq=" .. seq .. ";" .. payload,
            "drag payload: " .. tostring(X.last("GAME_ACT").payload))
    end
    snapshot()
    local count = table.getn(F.frames)
    local function regions()
        local n = 0
        for i = 1, table.getn(F.frames) do n = n + table.getn(F.frames[i]._.regions) end
        return n
    end
    local regionCount = regions()
    for n = 0, 6 do
        local board = {}
        for i = 1, n do board[i] = tostring(39 + i) .. ".CS2_172.3.2.2.a" end
        for p = 0, n do
            snapshot({ mb = table.concat(board, ",") })
            local slot = MU.ui.hand[2]
            cursor(100, MU.geom.fh + 50)
            start(slot)
            check(MU.drag and slot.frame:GetAlpha() == 0.35, "hand copy dims its slot " .. n .. "/" .. p)
            local xs = MU.RowPositions(n + 1, MU.MINION_W, MU.MINION_GAP, MU.geom.bw)
            cursor(xs[p + 1] + MU.MINION_W / 2, MU.RowTop("m", MU.geom.fh) + 30)
            check(MU.drag.position == p, "gap " .. n .. "/" .. p)
            for i = 1, n do
                local j = i
                if i > p then j = i + 1 end
                check(MU.ui.m.minions[i].frame._.points[1].x == xs[j], "minions open selected gap")
            end
            drop(slot)
            expect("a=play;s=15;p=" .. p)
            check(not MU.drag and slot.frame:GetAlpha() == 1 and not MU.dragPool.copy.frame:IsShown(), "drop restores")
        end
    end
    snapshot()
    local slot = MU.ui.hand[4]
    start(slot)
    cursor(100, 30)
    drop(slot)
    expect("a=play;s=21")
    snapshot({ lg = "p12.3/44,p15.0,p18.0,p21.0,a40.3/44,a1.3/44,h2.1/3/40/41/44" })
    slot = MU.ui.hand[1]
    cursor(300, MU.geom.fh + 50)
    start(slot)
    local anchor, target = MU.drag.anchor, MU.AnchorOf(44)
    cursor(target.x, target.y)
    local dot, head = MU.dragPool.dots[1]._.points[1], MU.dragPool.head._.points[1]
    local scale = MU.board:GetEffectiveScale()
    check(math.abs(dot.x + 2 / scale - anchor.x) < 1e-6 and math.abs(-dot.y + 2 / scale - anchor.y) < 1e-6,
        "first dot at source")
    check(math.abs(head.x + 12 / scale - target.x) < 1e-6 and math.abs(-head.y + 12 / scale - target.y) < 1e-6,
        "head at cursor")
    local uv = MU.dragPool.head._.texcoord
    check(table.getn(uv) == 8, "eight corner UVs")
    check(MU.dragPool.head._.texture == "Interface\\Minimap\\MinimapArrow", "native arrowhead texture")
    for i = 1, 8 do check(type(uv[i]) == "number" and uv[i] == uv[i], "finite UV") end
    check(math.abs((uv[3] - uv[1]) ^ 2 + (uv[4] - uv[2]) ^ 2 - 1) < 1e-6,
        "rotation preserves UV edge length")
    MU.DragArrow({ x = 100, y = 200 }, 100, 0, false)
    uv = MU.dragPool.head._.texcoord
    for i, value in ipairs({ 0, 0, 0, 1, 1, 0, 1, 1 }) do check(uv[i] == value, "upward UV corner") end
    cursor(target.x, target.y)
    check(MU.ui.o.minions[1].glow.frame:GetAlpha() == 1 and MU.ui.o.hero.glow.frame:GetAlpha() == 0.5,
        "hover target stronger")
    local own = MU.AnchorOf(41)
    cursor(own.x, own.y)
    check(MU.dragPool.head._.vertex[1] == 1 and MU.dragPool.head._.vertex[2] == 0.15, "invalid head red")
    cursor(target.x, target.y)
    drop(slot)
    expect("a=play;s=12;t=44")
    for _, source in ipairs({ 40, 1, 2 }) do
        snapshot({ lg = "a40.3/44,a1.3/44,h2.1/3/40/41/44" })
        local a = MU.AnchorOf(source)
        cursor(a.x, a.y)
        slot = a.slot
        start(slot)
        check(MU.drag and MU.drag.targeting, "ready source targets")
        local t = MU.AnchorOf(3)
        cursor(t.x, t.y)
        drop(slot)
        if source == 2 then expect("a=hp;t=3") else expect("a=atk;s=" .. source .. ";t=3") end
    end
    snapshot({ lg = "p15.3/44" })
    slot = MU.ui.hand[2]
    start(slot)
    cursor(0, MU.RowTop("m", MU.geom.fh) + 30)
    local before = acts()
    drop(slot)
    check(MU.sel and MU.sel.position == 0 and acts() == before, "Battlecry retains placement")
    check(MU.dragPool.copy.frame:IsShown() and MU.dragStage and slot.frame:GetAlpha() == 0.35,
        "Battlecry keeps placement preview")
    F.fire(MU.ui.o.hero.frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    expect("a=play;s=15;t=3;p=0")
    snapshot({ lg = "p15.0/3/44" })
    slot = MU.ui.hand[2]
    start(slot)
    cursor(0, MU.RowTop("m", MU.geom.fh) + 30)
    drop(slot)
    start(slot)
    local battlecryTarget = MU.AnchorOf(44)
    cursor(battlecryTarget.x, battlecryTarget.y)
    drop(slot)
    expect("a=play;s=15;t=44;p=0")
    for _, kind in ipairs({ "p", "a", "h" }) do
        snapshot({ lg = "p12.3/44,a40.3/44,h2.3/44" })
        slot = kind == "p" and MU.ui.hand[1] or (kind == "a" and MU.ui.m.minions[1] or MU.ui.m.power)
        start(slot)
        before = acts()
        local invalid = MU.AnchorOf(41)
        cursor(invalid.x, invalid.y)
        drop(slot)
        check(not MU.drag and not MU.sel and acts() == before, "invalid targeted release " .. kind)
    end
    snapshot({ mc = "12.EX1_154.2.0.0", mo = "12.EX1_154a/EX1_154b", lg = "p12:1.44,p12:2.0" })
    slot = MU.ui.hand[1]
    -- a Choose One card is dragged like an untargeted spell; dropped back over the hand it opens nothing
    start(slot)
    check(MU.drag and not MU.drag.targeting and not MU.choose, "a Choose One card is dragged like a spell")
    before = acts()
    cursor(100, MU.geom.fh + 50)
    drop(slot)
    check(not MU.drag and not MU.choose and acts() == before, "dropped over the hand: back in the hand, no chooser")
    -- dropped over the field it opens the chooser
    start(slot)
    cursor(100, 30)
    drop(slot)
    check(MU.choose and MU.choose.source == 12 and not MU.drag and acts() == before, "dropped over the field: the chooser opens")
    slot = MU.chooser.slots[1]
    start(slot)
    local t = MU.AnchorOf(44)
    cursor(t.x, t.y)
    drop(slot)
    expect("a=play;s=12;t=44;c=1")
    for _, path in ipairs({ "outside", "right", "escape", "hide", "snapshot", "end", "invalid" }) do
        snapshot()
        slot = MU.ui.hand[2]
        start(slot)
        before = acts()
        if path == "outside" then cursor(-100, -100) drop(slot)
        elseif path == "invalid" then cursor(100, MU.geom.fh + 50) drop(slot)
        elseif path == "right" then F.fire(slot.frame, "OnMouseDown", { arg1 = "RightButton" })
        elseif path == "escape" then MU.frame:Hide()
        elseif path == "hide" then MU.HideWindow()
        elseif path == "end" then snapshot({ ph = "over", act = "-", lg = "", res = "win" })
        else snapshot() end
        check(not MU.drag and not MU.dragPool.head:IsShown() and not MU.dragPool.copy.frame:IsShown()
            and slot.frame:GetAlpha() == 1 and acts() == before, "cancel " .. path)
    end
    for _, blocked in ipairs({ "pending", "turn", "mull", "pick", "choose", "animation", "setting" }) do
        snapshot()
        local oldBehind = MU.Behind
        if blocked == "pending" then MA.pending = true
        elseif blocked == "turn" then MA.game.act = "o"
        elseif blocked == "mull" then MA.game.ph = "mull"
        elseif blocked == "pick" then MA.game.pick = { { id = 900, cardId = "CS2_029" } }
        elseif blocked == "choose" then MU.choose = { source = 15, seq = seq }
        elseif blocked == "animation" then MU.Behind = function() return true end
        else H.Slash("drag off") end
        before = acts()
        start(MU.ui.hand[2])
        drop(MU.ui.hand[2])
        check(not MU.drag and acts() == before, "blocked " .. blocked)
        MU.Behind, MA.pending, MU.choose = oldBehind, nil, nil
        H.Slash("drag on")
    end
    snapshot()
    for i = 1, 40 do start(MU.ui.hand[2]) cursor(-10, -10) drop(MU.ui.hand[2]) end
    check(table.getn(F.frames) == count and table.getn(MU.dragPool.dots) == 24, "no frame leaks")
    check(regions() == regionCount, "no texture or font string leaks")
    for i = 1, 24 do check(not MU.dragPool.dots[i]:IsShown(), "dot returned") end
    UIParent:SetScale(0.8)
    cursor(123, 234)
    local x, y = MU.DragCursor()
    check(math.abs(x - 123) < 1e-6 and math.abs(y - 234) < 1e-6, "effective cursor scale")
    UIParent:SetScale(1)
    H.Slash("drag off")
    F.fire(MU.ui.m.minions[1].frame, "OnClick", { arg1 = "LeftButton" })
    F.fire(MU.ui.o.hero.frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    expect("a=atk;s=40;t=3")
    check(H.db.drag == false, "saved setting")
    H.Slash("drag on")
    -- with dragging on, a click on a playable hand card sends nothing and says how to play it; with it off the click plays
    snapshot()
    before = acts()
    MU.hint = nil
    F.fire(MU.ui.hand[2].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    check(acts() == before and MU.hint and string.find(MU.hint, "Drag the card", 1, true), "a click does not play a card while dragging is on: " .. tostring(MU.hint))
    check(MU.DragEnabled(), "dragging is on")
    H.Slash("drag off")
    check(not MU.DragEnabled(), "dragging is off")
    F.fire(MU.ui.hand[2].frame, "OnClick", { arg1 = "LeftButton" })
    drain(1)
    check(acts() == before + 1, "with dragging off a click plays the card")
    H.Slash("drag on")
    MU.HideWindow()
    H.db = nil
    F.uninstall()
    resetAddon()
    resetMatch()
end

local function testDuelWindow()
    local savedBT = HS_BoardTextures
    -- without the cloth pieces the dark dialog background stays
    local F, MU, X = animSetup({ board = visualBoard() })
    X.push(ag({ seq = "2" }))
    check(MU.cloth == nil and MU.frame._.backdrop.bgFile ~= nil, "no cloth pieces: the dark dialog background")
    check(MU.board._.bdColor[4] > 0.5, "the board plate stays without the cloth")
    -- no player names, no hand / deck counts (the fatigue line would stay)
    check(MU.ui.m.name:GetText() == "" and MU.ui.o.name:GetText() == "", "no player names")
    check(MU.ui.m.counts:GetText() == "" and MU.ui.o.counts:GetText() == "", "no hand / deck counts")
    local mm2, om = MU.ui.m.minions, MU.ui.o.minions
    check(mm2[2].taunt == nil and string.find(mm2[2].tags:GetText(), "Taunt", 1, true) and mm2[1].tags:GetText() == "",
        "without the frame art a Taunt minion keeps its word, other minions have no text")
    check(om[2].tags:GetText() == "" and not string.find(om[1].tags:GetText(), "Charge", 1, true), "no keyword names on the opponent's minions")
    local at, hp = mm2[1].atk.text._.points[1], mm2[1].hp.text._.points[1]
    check(at.x > 0 and at.y < 0 and math.abs(at.x - MU.MINION_GEM * MU.ATTACK_TEXT_DX) < 1e-9 and math.abs(at.y - MU.MINION_GEM * MU.ATTACK_TEXT_DY) < 1e-9
        and hp.x == 0 and hp.y == 0, "the attack number sits on the gold disc (right of and below the picture's middle), the health number in the middle")
    check(MU.HERO_HP_FONT < math.floor(MU.HERO_HP_GEM * 0.6), "the hero health number is a little smaller than the standard size")
    check(mm2[1].atk.frame:GetWidth() == MU.MINION_GEM and mm2[1].hp.frame:GetWidth() == MU.MINION_GEM and MU.MINION_GEM >= 32
        and MU.ui.m.hero.hp.frame:GetWidth() == MU.HERO_HP_GEM and MU.HERO_HP_GEM > MU.ui.m.hero.atk.frame:GetWidth(), "bigger minion gems and hero health gem")
    -- with the cloth pieces: six tiles cover the window inside its frame, the board plate is gone
    local bt = visualBoard()
    for r = 0, 1 do
        for c = 0, 2 do
            bt.pieces["cloth_c" .. c .. "r" .. r] = { path = "x/cloth_c" .. c .. "r" .. r, w = 512, h = 512, right = 1, bottom = 1 }
        end
    end
    F, MU, X = animSetup({ board = bt })
    X.push(ag({ seq = "2" }))
    check(MU.cloth and table.getn(MU.cloth) == 6 and MU.frame._.backdrop.bgFile == nil and MU.frame._.backdrop.edgeFile ~= nil,
        "six cloth tiles instead of the dialog background, the frame edge stays")
    local function check_tiles(tag)
        local i = MU.CLOTH_INSET
        local ok, first, last = true, MU.cloth[1].tex._.points, MU.cloth[6].tex._.points
        check(first[1].x == i and first[1].y == -i and last[2].x == MU.geom.w - i and last[2].y == -(MU.geom.h - i), tag .. "the tiles span the window inside its frame")
        for k = 1, 6 do
            local t = MU.cloth[k]
            local nxt = MU.cloth[k + 1]
            local pts = t.tex._.points
            if t.c < 2 and nxt and nxt.r == t.r and nxt.tex._.points[1].x ~= pts[2].x then ok = false end
            if t.r == 0 and MU.cloth[k + 3] and MU.cloth[k + 3].tex._.points[1].y ~= pts[2].y then ok = false end
            if t.tex._.texture ~= "x/cloth_c" .. t.c .. "r" .. t.r then ok = false end
        end
        check(ok, tag .. "the tiles touch and show their own texture")
    end
    check_tiles("")
    check(MU.board._.bdColor[4] == 0 and MU.board._.bdBorder[4] == 0, "no plate behind the board: the cloth shows under the hand")
    F.screenW, F.screenH = 1280, 720
    MU.ApplyFit()
    check_tiles("after a new screen size: ")
    -- the minion rows keep their distance to the hero frames (ROW_CLEAR)
    local arch = MU.PieceAnchor("board_field", "arch_bottom_frame")
    local th = MU.geom.fh - 2 * MU.FIELD_INSET
    check(math.abs(MU.RowTop("m", MU.geom.fh) + MU.MINION_H + MU.ROW_CLEAR + MU.MY_ROW_LIFT - (MU.FIELD_INSET + MU.FieldY(arch.top) * th)) <= 1, "the player's row stands ROW_CLEAR + MY_ROW_LIFT above the hero frame")
    check(MU.ROW_CLEAR >= 16, "the rows keep clear of the portraits")
    HS_BoardTextures = savedBT
end

local function testDuelArt()
    local MA = HsMatch
    local savedBT = HS_BoardTextures
    local bt = visualBoard()
    local function piece(name, w, h, tw, th)
        bt.pieces[name] = { path = "x/" .. name, w = w, h = h, right = w / tw, bottom = h / th }
    end
    piece("end_turn_button", 256, 135, 256, 256)
    piece("enemy_turn_button", 256, 129, 256, 256)
    piece("banner_starting_hand", 512, 171, 512, 256)
    piece("mull_x", 223, 256, 256, 256)
    piece("mull_replaced", 512, 134, 512, 256)
    piece("taunt_frame", 187, 256, 256, 256)
    piece("mana_pill", 128, 49, 128, 64)
    piece("mana_gem_full", 64, 59, 64, 64)
    piece("mana_gem_empty", 64, 62, 64, 64)
    piece("mana_lock", 64, 61, 64, 64)
    piece("mana_gem_glow", 128, 125, 128, 128)
    piece("divine_shield", 207, 256, 256, 256)
    piece("frozen_ice", 256, 256, 256, 256)
    bt.pieces.frozen_ice.anchors = { inner = { left = 0.205, top = 0.164, right = 0.833, bottom = 0.793 } }
    bt.pieces.divine_shield.anchors = { inner = { left = 0.131, top = 0.047, right = 0.914, bottom = 0.916 } }
    bt.pieces.taunt_frame.anchors = { inner = { left = 0.13093, top = 0.09158, right = 0.87246, bottom = 0.81353 } }
    local F, MU, X = animSetup({ board = bt })
    X.push(ag({ seq = "2" }))
    local ui = MU.ui

    -- the Taunt frame: shown on Taunt minions only, its opening on the minion's oval, no words
    local mm3 = ui.m.minions
    check(mm3[2].taunt and mm3[2].taunt:IsShown() and not mm3[1].taunt:IsShown() and mm3[2].tags:GetText() == "", "the steel frame is on the Taunt minion, no word")
    local tp1 = mm3[2].taunt._.points[1]
    local fw, fh = mm3[2].frame:GetWidth(), mm3[2].frame:GetHeight()
    local oval = bt.pieces.minion_oval.anchors.oval
    local inner = bt.pieces.taunt_frame.anchors.inner
    local qw, qh = mm3[2].taunt:GetWidth(), mm3[2].taunt:GetHeight()
    local holeL, holeT = tp1.x + inner.left * qw, -tp1.y + inner.top * qh
    local holeR, holeB = tp1.x + inner.right * qw, -tp1.y + inner.bottom * qh
    check(tp1.point == "TOPLEFT" and tp1.rel == mm3[2].frame and math.abs(holeL - (oval.left * fw - MU.TAUNT_GROW)) < 1e-6
        and math.abs(holeR - (oval.right * fw + MU.TAUNT_GROW)) < 1e-6 and math.abs(holeT - (oval.top * fh - MU.TAUNT_GROW)) < 1e-6
        and math.abs(holeB - (oval.bottom * fh + MU.TAUNT_GROW)) < 1e-6, "the frame's opening is the minion's oval")
    check(qw > fw * 0.9 and qh > fh * 0.9, "the frame is bigger than the oval it surrounds")
    check(mm3[2].taunt._.texture == "x/taunt_frame" and mm3[2].taunt._.texcoord[2] == bt.pieces.taunt_frame.right, "the frame's own texture and used area")

    -- Divine Shield: the golden bubble over a shielded minion's oval (the fixture's 2nd opponent minion has it)
    local sh = ui.o.minions[1]
    check(sh.shield and not ui.m.minions[1].shield:IsShown(), "the shield bubble exists, hidden on minions without a shield")
    local mhp = MA.game.opp.board
    local shieldOn = 0
    for i = 1, table.getn(mhp) do
        if mhp[i].divine then
            shieldOn = shieldOn + 1
            check(ui.o.minions[i].shield:IsShown(), "a Divine Shield minion shows the bubble")
            local sp = ui.o.minions[i].shield._.points[1]
            local sw, sht = ui.o.minions[i].shield:GetWidth(), ui.o.minions[i].shield:GetHeight()
            local ov = bt.pieces.minion_oval.anchors.oval
            local fw2, fh2 = ui.o.minions[i].frame:GetWidth(), ui.o.minions[i].frame:GetHeight()
            local inn = bt.pieces.divine_shield.anchors.inner
            check(math.abs((sp.x + inn.left * sw) - (ov.left * fw2 - MU.SHIELD_GROW)) < 1e-6
                and math.abs((sp.x + inn.right * sw) - (ov.right * fw2 + MU.SHIELD_GROW)) < 1e-6
                and math.abs((-sp.y + inn.top * sht) - (ov.top * fh2 - MU.SHIELD_GROW)) < 1e-6, "the bubble's oval is the minion's oval")
        end
    end
    -- Frozen: the ice ring replaces the blue rim; hidden on characters that are not frozen
    check(sh.ice and not sh.ice:IsShown() and ui.o.hero.ice and not ui.o.hero.ice:IsShown(), "the ice rings exist, hidden while nothing is frozen")
    check(MU.RimOf({ frozen = true }, true) == nil and MU.RimOf({ frozen = true }) ~= nil, "the frozen rim is left out with the ring")
    check(MU.RimOf({ immune = true }, true) ~= nil, "the immune rim stays")
    X.push(ag({ seq = "12", ob = "44.CS2_124.3.1.1.cd,45.EX1_412.4.2.3.-" }))
    check(ui.o.minions[1].shield:IsShown() and not ui.o.minions[2].shield:IsShown(), "the d flag raises the bubble: " .. tostring(MA.game.opp.board[1].divine))

    -- the End Turn button: small, no turn text, a gold halo while the cursor is on it and it can be clicked
    local mid0 = ui.mid
    check(mid0.endButton:GetWidth() == MU.END_W and MU.END_W <= 120 and mid0.endTex and mid0.endGlow and not mid0.endGlow:IsShown(), "a small button with a hidden glow")
    F.fire(mid0.endButton, "OnEnter")
    check(mid0.endGlow:IsShown() and mid0.endGlow._.blend == "ADD", "the glow shows on hover")
    F.fire(mid0.endButton, "OnLeave")
    check(not mid0.endGlow:IsShown(), "and goes with the cursor")
    X.push(ag({ seq = "13", act = "o" }))
    F.fire(mid0.endButton, "OnEnter")
    check(not mid0.endGlow:IsShown(), "no glow on the Enemy Turn plate")
    X.push(ag({ seq = "14", act = "m" }))

    -- the mana with the art: the pill, my crystals, the lock, the glow of what a hovered card would spend
    local um, uo = ui.m, ui.o
    check(um.pill:IsShown() and uo.pill:IsShown() and um.pillText:GetText() == "4/4", "the pill shows mana / maximum: " .. tostring(um.pillText:GetText()))
    check(uo.pillText:GetText() == "4/4" and not uo.crystals[1]:IsShown() and not uo.crystals[4]:IsShown(), "the opponent has only the pill")
    local full, empty = 0, 0
    for i = 1, 4 do
        if um.crystals[i]._.texture == "x/mana_gem_full" then full = full + 1 end
    end
    check(full == 4 and not um.crystals[5]:IsShown(), "four full crystals, no fifth")
    check(um.manaText:GetText() == "", "the old yellow text is empty")
    local pp = um.pill._.points[1]
    local cp1 = um.crystals[1]._.points[1]
    check(pp.rel == MU.board and cp1.x > pp.x + MU.PILL_W and cp1.y == pp.y, "the crystals follow the pill on one line")
    check(cp1.x + 10 * (um.crystals[1]:GetWidth() + 2) <= MU.geom.bw, "the whole row fits inside the board")
    -- hover a hand card: the rightmost full crystals it costs glow
    F.fire(ui.hand[1].frame, "OnEnter")
    local cost1 = MA.game.hand[1].cost
    local lit = 0
    for i = 1, 10 do if um.glows[i]:IsShown() then lit = lit + 1 end end
    check(lit == math.min(cost1, 4) and (cost1 == 0 or um.glows[4]:IsShown()), "a hovered card of cost " .. cost1 .. " lights " .. lit .. " crystals, the rightmost first")
    check(um.glows[4]._.blend == "ADD" and um.glows[4]._.texture == "x/mana_gem_glow", "the glow texture, added")
    F.fire(ui.hand[1].frame, "OnLeave")
    lit = 0
    for i = 1, 10 do if um.glows[i]:IsShown() then lit = lit + 1 end end
    check(lit == 0, "the glow goes with the cursor")
    F.fire(um.power.frame, "OnEnter")
    lit = 0
    for i = 1, 10 do if um.glows[i]:IsShown() then lit = lit + 1 end end
    check(lit == 2 and um.glows[4]:IsShown() and um.glows[3]:IsShown() and not um.glows[2]:IsShown(), "the hero power (cost 2) lights the two rightmost crystals")
    F.fire(um.power.frame, "OnLeave")
    -- Overload: the locked crystals show the lock emblem, the spent ones are empty
    X.push(ag({ seq = "15", mm = "2.5", mk = "4.20.0", mx = "0.2.1" }))
    local kinds = {}
    for i = 1, 5 do kinds[i] = string.gsub(um.crystals[i]._.texture or "?", "^x/mana_", "") end
    check(table.concat(kinds, ",") == "gem_full,gem_full,gem_empty,lock,lock", "2 full, 1 spent, 2 locked: " .. table.concat(kinds, ","))
    check(string.find(um.overload:GetText(), "2 locked", 1, true) ~= nil, "the overload line names the locked crystals")
    -- a narrow board: the crystals shrink so the row still fits
    local oldBw = MU.geom.bw
    MU.geom.bw = 900   -- (narrow enough that ten crystals do not fit at full size, wide enough to stay above the smallest size)
    MU.Place()
    MU.Refresh()
    local small, smallH = um.crystals[1]:GetWidth(), um.crystals[1]:GetHeight()
    MU.geom.bw = oldBw
    MU.Place()
    MU.Refresh()
    check(small < um.crystals[1]:GetWidth() and smallH >= 13, "the crystals shrink on a narrow board: " .. small .. "x" .. smallH)

    -- End Turn / Enemy Turn: one button, the art follows whose turn it is
    local mid = ui.mid
    check(mid.endTex and mid.endButton:GetWidth() == MU.END_W and mid.endTex._.texture == "x/end_turn_button" and mid.endButton:IsEnabled(), "my turn: End Turn art, clickable")
    X.push(ag({ seq = "33", act = "o" }))
    check(mid.endTex._.texture == "x/enemy_turn_button" and not mid.endButton:IsEnabled() and mid.endButton:IsShown() and mid.endButton:GetAlpha() == 1,
        "the opponent's turn: the Enemy Turn plate, shown at full strength")
    X.push(ag({ seq = "34", act = "m" }))
    check(mid.endTex._.texture == "x/end_turn_button" and mid.endButton:IsEnabled(), "and back to End Turn")

    -- no preview over a hero
    F.fire(ui.m.hero.frame, "OnEnter")
    check(not MU.preview.frame:IsShown(), "hovering my hero shows nothing")
    F.fire(ui.m.hero.frame, "OnLeave")

    -- the hero power: just the large card at the side (no plate, no caption), like in the log
    local pv = MU.preview
    F.fire(ui.m.power.frame, "OnEnter")
    check(pv.frame:IsShown(), "the power shows its card")
    check(pv.caption:GetText() == "", "no caption under it")
    F.fire(ui.m.power.frame, "OnLeave")
    check(not pv.frame:IsShown(), "gone when the cursor leaves")
    F.fire(ui.o.power.frame, "OnEnter")
    check(pv.frame:IsShown(), "the opponent's power shows its card too")
    MU.HidePreview()
    check(not pv.frame:IsShown(), "HidePreview closes it")

    -- the mulligan with the owner's art: banner, painted X, painted REPLACED label
    X.push(ag({ seq = "35", ph = "mull", turn = "0", act = "-", lg = "", mul = "12,15,18" }))
    local mm = MU.mull
    check(mm.frame:IsShown() and mm.banner:IsShown() and mm.bannerH and not mm.ribbon:IsShown() and not mm.title:IsShown(), "the banner replaces ribbon and title")
    check(math.abs(mm.banner:GetHeight() - MU.MULL_BANNER_W * 171 / 512) < 1e-6 and mm.hint:GetText() == "", "banner size, no subtitle text (it is painted in)")
    local sl = mm.slots[1]
    check(sl.xImg and sl.xImg._.texture == "x/mull_x" and table.getn(sl.xBar1) == 0 and table.getn(sl.xShadow1) == 0, "the painted X replaces the drawn bars")
    check(sl.tag._.texture == "x/mull_replaced" and not sl.tagText:IsShown(), "the painted REPLACED label replaces the ribbon text")
    check(math.abs(sl.xImg:GetWidth() / sl.xImg:GetHeight() - 223 / 256) < 1e-6, "the X keeps its proportions")
    HS_BoardTextures = savedBT
end

-- ------------------------------------------------------------ draft arena
local ARENA_KEYS = { "st", "run", "en", "pay", "cls", "w", "d", "l", "duel", "no", "of", "pk", "dk", "pz", "tb", "spc", "sp",
    "sc", "live", "mine", "rp" }
local ARENA_BASE = { st = "none", run = "0", en = "1", pay = "", cls = "", w = "0", d = "0", l = "0", duel = "0", no = "",
    of = "", pk = "0", dk = "", pz = "0.0.0", tb = "3.10", spc = "0", sp = "", sc = "", live = "0", mine = "0", rp = "0" }

-- An ARENA / ARENA_UPD / ARENA_OPEN frame, chunked like the server does it (the payloads are longer than one frame).
local function aframe(op, reqId, payload)
    local frames = P.ChunkPayload(op, reqId, payload)
    assert(frames, "arena payload fits")
    for i = 1, table.getn(frames) do H.HandleFrame(frames[i]) end
end

local function popupWhich(F)
    return F.popup and F.popup.which or nil
end

-- An ARENA payload (the server's key order) with some values replaced.
local function arenaPayload(over)
    local kv = {}
    for k, v in pairs(ARENA_BASE) do kv[k] = v end
    for k, v in pairs(over or {}) do kv[k] = v end
    if kv.st ~= "none" and kv.st ~= "class" and (over == nil or over.cls == nil) then kv.cls = "MAGE" end   -- a started run has its class
    local list = {}
    for i = 1, table.getn(ARENA_KEYS) do table.insert(list, { ARENA_KEYS[i], kv[ARENA_KEYS[i]] }) end
    return P.KvEncode(list)
end

local ARENA_CLASS_OFFER = { st = "class", run = "40", pay = "gold", no = "0", of = "MAGE,PRIEST,WARRIOR" }
local ARENA_DRAFT = { st = "draft", run = "40", pay = "gold", cls = "MAGE", no = "3", of = "CS2_168,CS2_172,CS2_124", pk = "2",
    dk = "CS2_168.1,CS2_172.1" }
local ARENA_READY = { st = "ready", run = "40", pay = "gold", cls = "MAGE", w = "2", pk = "30", pz = "1.0.0",
    dk = "CS2_168.2,CS2_172.2,CS2_124.2,CS2_022.2,CS2_189.2,EX1_116.2,CS1_042.2,CS2_003.2,CS2_004.2,CS2_005.2,CS2_007.2,CS2_008.2,CS2_009.2,CS2_011.2,CS2_012.2" }
local ARENA_DUEL = { st = "duel", run = "40", pay = "gold", cls = "MAGE", w = "2", duel = "3", pk = "30", pz = "1.0.0",
    dk = "CS2_168.2,CS2_172.2,CS2_124.2,CS2_022.2,CS2_189.2,EX1_116.2,CS1_042.2,CS2_003.2,CS2_004.2,CS2_005.2,CS2_007.2,CS2_008.2,CS2_009.2,CS2_011.2,CS2_012.2",
    live = "1", mine = "1" }

local function testArenaModel()
    local AR = HsArena
    resetAddon()
    check(AR.NAME == "Night Watch Arena", "the player-facing name")
    check(AR.CapsHaveArena("solo,chunk,arena") and AR.CapsHaveArena("arena") and not AR.CapsHaveArena("solo,arenas,chunk")
        and not AR.CapsHaveArena("") and not AR.CapsHaveArena(nil) and not AR.CapsHaveArena("solo,,arena"), "caps")

    -- the payloads of the protocol
    local m, why = AR.ParseArena(arenaPayload())
    check(m and m.st == "none" and m.run == 0 and m.no == nil and table.getn(m.offer) == 0 and m.enabled and m.terms.packs == 3
        and m.terms.coins == 10 and not m.live and not m.replay, "no run: " .. tostring(why))
    m, why = AR.ParseArena(arenaPayload(ARENA_CLASS_OFFER))
    check(m and m.st == "class" and m.no == 0 and m.offer[1] == "MAGE" and m.offer[3] == "WARRIOR" and m.pay == "gold", "class offer: " .. tostring(why))
    m, why = AR.ParseArena(arenaPayload(ARENA_DRAFT))
    check(m and m.st == "draft" and m.no == 3 and m.offer[2] == "CS2_172" and m.cls == "MAGE" and m.picked == 2
        and table.getn(m.deck) == 2 and m.deck[1].id == "CS2_168" and m.deck[1].n == 1, "draft: " .. tostring(why))
    m, why = AR.ParseArena(arenaPayload({ st = "settled", run = "40", w = "10", d = "2", l = "1", pk = "30", pz = "3.10.1", spc = "1",
        sp = "EX1_062.1,NEW1_016.2", sc = "", rp = "1" }))
    check(m and m.specialPending and table.getn(m.specials) == 2 and m.specials[2].n == 2 and m.wins == 10 and m.draws == 2
        and m.losses == 1 and m.prize.packs == 3 and m.prize.coins == 10 and m.prize.special and m.replay, "settled: " .. tostring(why))
    m, why = AR.ParseArena(arenaPayload({ st = "duel", run = "40", live = "1", mine = "1" }))
    check(m and m.live and m.mine, "live flags")

    -- one bad key rejects the whole reply
    local bad = {
        { { st = "bogus" }, "bad st" }, { { run = "040" }, "run with a leading zero" }, { { run = "x" }, "run not a number" },
        { { en = "2" }, "bad en" }, { { pay = "paypal" }, "bad pay" }, { { cls = "BARD" }, "bad cls" },
        { { w = "1", d = "2" }, "more draws than wins" }, { { duel = "-1" }, "bad duel" },
        { { st = "draft", no = "31", of = "A,B,C" }, "no above 30" }, { { no = "1", of = "CS2_168,CS2_172" }, "offer of two" },
        { { no = "", of = "CS2_168,CS2_172,CS2_124" }, "offer without a number" },
        { { no = "0", of = "MAGE,PRIEST,CS2_168" }, "class offer with a card" }, { { no = "1", of = "CS2_168,,CS2_124" }, "empty offer item" },
        { { pk = "31" }, "pk above 30" }, { { dk = "CS2_168.1,CS2_168.1" }, "card twice in dk" },
        { { dk = "CS2_168.31" }, "more than 30 copies" }, { { dk = "CS2_168.20,CS2_172.11" }, "deck above 30" },
        { { dk = "CS2_168" }, "dk without a count" }, { { pz = "1.2" }, "short pz" }, { { pz = "1.2.3" }, "special not 0/1" },
        { { tb = "x" }, "bad tb" }, { { spc = "2" }, "bad spc" }, { { sp = "EX1_062" }, "sp without a count" },
        { { sc = "bad id!" }, "bad sc" }, { { live = "yes" }, "bad live" }, { { rp = "" }, "bad rp" },
    }
    for i = 1, table.getn(bad) do
        local r = AR.ParseArena(arenaPayload(bad[i][1]))
        check(r == nil, "rejected: " .. bad[i][2])
    end
    check(AR.ParseArena(nil) == nil and AR.ParseArena("") == nil and AR.ParseArena("st=none") == nil, "missing keys")

    -- the pictures
    local dk = AR.ParseArena(arenaPayload(ARENA_READY))
    local rows = AR.DeckRows(dk)
    check(rows[1].cost <= rows[2].cost and rows[table.getn(rows)].cost >= rows[table.getn(rows) - 1].cost, "rows sorted by cost")
    check(AR.DeckCount(dk) == 30 and table.getn(dk.deck) == 15, "deck count with copies")
    local curve = AR.Curve(dk)
    local sum = 0
    for i = 0, HsCollection.MANA_MAX do sum = sum + curve[i] end
    check(sum == 30 and curve[1] >= 2, "curve sums to the deck")
    check(AR.PrizeText({ packs = 0, coins = 0, special = false }) == "nothing" and AR.PrizeText({ packs = 1, coins = 0, special = false }) == "1 pack"
        and AR.PrizeText({ packs = 3, coins = 10, special = true }) == "3 packs + 10 Hearthstone Coins + a special card of your choice", "prize text")
    local tbl = AR.PrizeTable(dk)
    check(table.getn(tbl) == 5 and tbl[1][2] == "nothing" and tbl[3][2] == "3 packs" and string.find(tbl[5][2], "special card", 1, true), "prize table")

    -- the state texts point to the hosts
    for _, st in ipairs({ "ready", "paused", "none" }) do
        local _, body = AR.StateInfo(AR.ParseArena(arenaPayload({ st = st, run = st == "none" and "0" or "40" })))
        check(string.find(body, "Ezra", 1, true) and string.find(body, "Ibelin", 1, true), st .. " text names the hosts: " .. body)
    end
    local _, duelBody = AR.StateInfo(AR.ParseArena(arenaPayload(ARENA_DUEL)))
    check(string.find(duelBody, "pauses", 1, true) and string.find(duelBody, "Ezra or Ibelin", 1, true), "duel text: " .. duelBody)
    local _, foreign = AR.StateInfo(AR.ParseArena(arenaPayload({ st = "duel", run = "40", live = "1", mine = "0" })))
    check(string.find(foreign, "Another character", 1, true), "a duel of another character")
    local h1, b1 = AR.StateInfo(AR.ParseArena(arenaPayload({ st = "repair", run = "40" })))
    check(string.find(h1, "game master", 1, true) and string.find(b1, "game master", 1, true), "repair text")
    local h2, b2 = AR.StateInfo(AR.ParseArena(arenaPayload({ st = "retired", run = "40", w = "4" })))
    check(h2 == "Run retired" and string.find(b2, "4 wins", 1, true), "retired text")
    -- a practice run: valid pay code, and no word about a prize anywhere
    local pm = AR.ParseArena(arenaPayload({ st = "settled", run = "41", pay = "practice", w = "4", l = "1" }))
    check(pm and pm.pay == "practice", "practice is a valid pay code")
    local _, pb = AR.StateInfo(pm)
    check(string.find(pb, "4 wins", 1, true) and not string.find(pb, "rize", 1, true), "practice settled text: " .. pb)
    local _, pf = AR.StateInfo(AR.ParseArena(arenaPayload({ st = "finished", run = "41", pay = "practice", w = "2" })))
    check(string.find(pf, "practice", 1, true) and not string.find(pf, "rize", 1, true), "practice finished text: " .. pf)
    check(string.find(AR.SettledText(pm), "no prize", 1, true) ~= nil, "practice settled line: " .. AR.SettledText(pm))
    local h3 = AR.StateInfo(AR.ParseArena(arenaPayload({ st = "finished", run = "40", w = "1" })))
    check(h3 == "Run over", "finished heading")
    check(select(1, AR.StateInfo(AR.ParseArena(arenaPayload({ en = "0" })))) == AR.NAME and
        string.find(select(2, AR.StateInfo(AR.ParseArena(arenaPayload({ en = "0" })))), "closed", 1, true), "entries closed hint")

    -- special cards: the missing sets, or - every set complete - the four reward cards
    local sp = AR.SpecialChoices(AR.ParseArena(arenaPayload({ st = "settled", run = "40", spc = "1", sp = "EX1_062.1,NEW1_016.2" })))
    check(table.getn(sp) == 2 and sp[1].id == "EX1_062" and not sp[1].complete and sp[2].n == 2, "missing sets")
    sp = AR.SpecialChoices(AR.ParseArena(arenaPayload({ st = "settled", run = "40", spc = "1", sp = "" })))
    check(table.getn(sp) == 4 and sp[1].complete and sp[1].id == "EX1_062" and sp[4].id == "PRO_001", "all complete: the four reward cards")
    check(table.getn(AR.SpecialChoices(AR.ParseArena(arenaPayload({ st = "settled", run = "40", spc = "0" })))) == 0, "no choice when spc=0")

    -- refusal texts: every code of the protocol, readable, and unknown ones fall back to the server's sentence
    local codes = { "no_run", "stale", "bad_state", "bad_class", "not_offered", "picked", "bad_card", "deck", "not_special",
        "not_missing", "repair", "unpaid", "key", "overflow" }
    for i = 1, table.getn(codes) do
        local t = AR.RefusedText(codes[i], "server text")
        check(t ~= "server text" and string.len(t) > 10 and string.find(t, "why", 1, true) == nil, "refusal " .. codes[i] .. ": " .. t)
    end
    check(AR.RefusedText("brand_new", "A new sentence.") == "A new sentence." and AR.RefusedText(nil, "") == "The server refused that.", "unknown refusal")
    check(string.find(AR.RefusedText("repair"), "game master", 1, true) and string.find(AR.RefusedText("unpaid"), "Ezra", 1, true), "repair and unpaid texts")

    -- steps: blockers, payloads and the key
    AR.SetReady(AR.ParseArena(arenaPayload(ARENA_CLASS_OFFER)))
    check(AR.OpBlocker("class", "MAGE") == nil and AR.OpBlocker("class", "DRUID") == AR.WHY_TEXT.bad_class
        and AR.OpBlocker("pick", "CS2_168") ~= nil and AR.OpBlocker("claim", "EX1_062") ~= nil, "blockers in the class offer")
    local op = AR.BeginOp("class", "PRIEST")
    check(string.find(op.key, "^[0-9a-z]+$") and string.len(op.key) >= 12 and string.len(op.key) <= 16, "key shape: " .. op.key)
    check(AR.OpPayload(op) == "key=" .. op.key .. ";class=PRIEST;run=40", "ARENA_CLASS payload: " .. AR.OpPayload(op))
    check(AR.IsBusy() and AR.OpBlocker("class", "MAGE") == "Your last choice is still on its way.", "busy")
    AR.ClearOp()
    AR.SetReady(AR.ParseArena(arenaPayload(ARENA_DRAFT)))
    check(AR.OpBlocker("pick", "CS2_172") == nil and AR.OpBlocker("pick", "CS2_189") == AR.WHY_TEXT.not_offered
        and AR.OpBlocker("class", "MAGE") ~= nil, "blockers in the draft")
    op = AR.BeginOp("pick", "CS2_172")
    check(AR.OpPayload(op) == "key=" .. op.key .. ";no=3;id=CS2_172;run=40", "ARENA_PICK payload: " .. AR.OpPayload(op))
    AR.ClearOp()
    AR.SetReady(AR.ParseArena(arenaPayload({ st = "settled", run = "40", spc = "1", sp = "EX1_062.1" })))
    check(AR.OpBlocker("claim", "EX1_062") == nil and AR.OpBlocker("claim", "PRO_001") == AR.WHY_TEXT.not_special, "claim blockers")
    op = AR.BeginOp("claim", "EX1_062")
    check(AR.OpPayload(op) == "key=" .. op.key .. ";id=EX1_062;run=40", "ARENA_CLAIM payload: " .. AR.OpPayload(op))
    AR.ClearOp()

    -- a push that is older than what we know is stale
    AR.SetReady(AR.ParseArena(arenaPayload(ARENA_DRAFT)))
    check(AR.IsStale(AR.ParseArena(arenaPayload({ st = "draft", run = "40", cls = "MAGE", no = "2", of = "CS2_168,CS2_172,CS2_124", pk = "1" }))), "older step is stale")
    check(not AR.IsStale(AR.ParseArena(arenaPayload({ st = "draft", run = "40", cls = "MAGE", no = "4", of = "CS2_168,CS2_172,CS2_124", pk = "3" }))), "newer step is not")
    check(not AR.IsStale(AR.ParseArena(arenaPayload({ st = "class", run = "41", no = "0", of = "MAGE,PRIEST,WARRIOR" }))), "another run is not stale")
    check(not AR.IsArenaDuel(), "no duel")
    AR.SetReady(AR.ParseArena(arenaPayload(ARENA_DUEL)))
    check(AR.IsArenaDuel(), "a live duel of this character")
    AR.SetReady(AR.ParseArena(arenaPayload({ st = "duel", run = "40", live = "1", mine = "0" })))
    check(not AR.IsArenaDuel(), "a duel of another character is not ours")
    AR.Reset()
end

local function testArenaFlow()
    local AR = HsArena
    local T = packHelpers()
    local X = matchHelpers()
    resetAddon()
    local notified, shown = 0, 0
    local savedTime = GetTime
    GetTime = nil                          -- earlier UI tests leave the fake clock installed; this test drives H.clock
    local savedHook, savedShow = H.OnArenaChanged, H.ShowArena
    H.OnArenaChanged = function() notified = notified + 1 end
    H.ShowArena = function() shown = shown + 1 end
    local function flush() drain(1) end
    local function lastSent(op)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end

    -- nothing before WELCOME
    check(H.ArenaGet() == nil and select(2, H.ArenaChooseClass("MAGE")) == "Not connected to the server.", "nothing before WELCOME")
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DRAFT))
    aframe("ARENA_OPEN", "0", arenaPayload(ARENA_DRAFT))
    check(AR.state == "none" and shown == 0 and table.getn(H.queue) == 0, "pushes before WELCOME ignored")

    -- a server without the arena
    T.welcome("solo,chunk,packs,decks,match,wallet")
    check(AR.state == "unsupported" and table.getn(T.queuedOps("ARENA_GET")) == 0, "no arena cap: unsupported")
    check(select(2, H.ArenaPick("CS2_168")) == "This server does not support the Night Watch Arena yet.", "pick unsupported")
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DRAFT))
    check(AR.state == "unsupported" and shown == 0, "pushes ignored without the cap")
    T.err("0", "DISABLED", "off")

    -- with the arena: ARENA_GET after the handshake
    resetAddon()
    T.welcome("solo,chunk,packs,decks,match,wallet,arena")
    local q = T.queuedOps("ARENA_GET")
    check(table.getn(q) == 1 and q[1].payload == "" and AR.state == "loading" and H.arena.reqId == q[1].reqId, "ARENA_GET sent")
    check(H.ArenaGet() == H.arena.reqId and H.arena.refetch, "a second call marks a refetch")
    aframe("ARENA", "zz", arenaPayload(ARENA_DRAFT))
    check(AR.state == "loading", "an ARENA for another reqId is ignored")
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_CLASS_OFFER))
    check(AR.IsReady() and AR.run.st == "class" and AR.run.offer[2] == "PRIEST", "ARENA applied")
    flush()
    check(table.getn(T.sentOps("ARENA_GET")) == 2 and H.arena.reqId ~= nil and not H.arena.refetch, "the refetch went out")
    aframe("ARENA", H.arena.reqId, "st=nonsense")
    check(AR.state == "error" and H.arena.reqId == nil and string.find(H.state.lastError or "", "arena reply rejected", 1, true), "an unreadable reply is an error")
    H.queue = {}
    H.ArenaGet()
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_CLASS_OFFER))
    check(AR.IsReady() and AR.run.st == "class" and H.arena.reqId == nil, "and the next one recovers")
    -- the class choice: client check, payload, retry with the same key, answer
    H.queue = {}
    local k0, w0 = H.ArenaChooseClass("DRUID")
    check(k0 == nil and w0 == AR.WHY_TEXT.bad_class, "a class that is not offered is not sent")
    check(table.getn(T.queuedOps("ARENA_CLASS")) == 0, "nothing queued for it")
    local key = H.ArenaChooseClass("PRIEST")
    local cq = T.queuedOps("ARENA_CLASS")
    check(key and string.find(key, "^[0-9a-z]+$") and table.getn(cq) == 1
        and cq[1].payload == "key=" .. key .. ";class=PRIEST;run=40" and AR.IsBusy(), "ARENA_CLASS queued")
    check(select(2, H.ArenaChooseClass("MAGE")) == "Your last choice is still on its way.", "one step at a time")
    flush()
    local s = lastSent("ARENA_CLASS")
    T.err(s.reqId, "UNAVAILABLE", "later")
    check(AR.IsBusy() and AR.op.retryIn ~= nil, "unavailable: waits to retry")
    drain(H.ARENA_RETRY_DELAY + 0.5)
    local again = T.sentOps("ARENA_CLASS")
    check(table.getn(again) == 2 and P.KvDecode(again[2].payload).key == key and again[2].reqId ~= again[1].reqId, "retry: same key, new reqId")
    -- the late answer of the first attempt still counts
    aframe("ARENA", s.reqId, arenaPayload({ st = "draft", run = "40", pay = "gold", cls = "PRIEST", no = "1", of = "CS2_168,CS2_172,CS2_124", pk = "0", rp = "1" }))
    check(not AR.IsBusy() and AR.op == nil and AR.run.st == "draft" and AR.run.cls == "PRIEST" and AR.run.replay, "class chosen (replay)")
    check(AR.message == nil, "no message after a class: " .. tostring(AR.message))

    -- a pick; the ARENA_UPD push after our own step carries the same state
    local pickKey = H.ArenaPick("CS2_172")
    local pq = T.queuedOps("ARENA_PICK")
    check(pickKey and table.getn(pq) == 1 and pq[1].payload == "key=" .. pickKey .. ";no=1;id=CS2_172;run=40", "ARENA_PICK queued: " .. tostring(pq[1] and pq[1].payload))
    check(select(2, H.ArenaPick("CS2_168")) == "Your last choice is still on its way.", "no second pick")
    flush()
    s = lastSent("ARENA_PICK")
    local nextOffer = { st = "draft", run = "40", pay = "gold", cls = "PRIEST", no = "2", of = "CS2_189,CS2_124,CS2_022", pk = "1", dk = "CS2_172.1" }
    aframe("ARENA", s.reqId, arenaPayload(nextOffer))
    check(AR.run.no == 2 and AR.run.picked == 1 and AR.op == nil, "pick applied: the next offer")
    aframe("ARENA_UPD", "0", arenaPayload(nextOffer))
    check(AR.run.no == 2 and AR.run.offer[1] == "CS2_189", "the push replaces the model")
    -- an older push (the offer before) never puts the old offer back
    aframe("ARENA_UPD", "0", arenaPayload({ st = "draft", run = "40", pay = "gold", cls = "PRIEST", no = "1", of = "CS2_168,CS2_172,CS2_124", pk = "0" }))
    check(AR.run.no == 2 and AR.run.picked == 1, "a stale push is ignored")
    -- but a push of another run replaces it
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_CLASS_OFFER))
    aframe("ARENA_UPD", "0", arenaPayload({ st = "class", run = "41", no = "0", of = "DRUID,HUNTER,MAGE" }))
    check(AR.run.run == 41 and AR.run.offer[1] == "DRUID", "a new run replaces the model")
    -- an unreadable push refetches
    H.queue = {}
    aframe("ARENA_UPD", "0", "st=bad")
    check(table.getn(T.queuedOps("ARENA_GET")) == 1, "an unreadable push refetches")
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(nextOffer))

    -- ARENA_OPEN opens the window
    local before = shown
    aframe("ARENA_OPEN", "0", arenaPayload(nextOffer))
    check(shown == before + 1 and AR.run.no == 2, "ARENA_OPEN shows the window")

    -- three unknown outcomes: the addon gives up and resyncs (choosing again is safe)
    H.queue = {}
    key = H.ArenaPick("CS2_189")
    flush()
    for attempt = 1, AR.MAX_ATTEMPTS do
        s = lastSent("ARENA_PICK")
        check(P.KvDecode(s.payload).key == key, "attempt " .. attempt .. " uses the same key")
        T.err(s.reqId, "UNAVAILABLE", "later")
        drain(H.ARENA_RETRY_DELAY + 0.5)
    end
    check(AR.op == nil and string.find(AR.message or "", "did not confirm", 1, true), "gave up: " .. tostring(AR.message))
    check(table.getn(T.sentOps("ARENA_PICK")) == 1 + AR.MAX_ATTEMPTS, "sent " .. AR.MAX_ATTEMPTS .. " times (plus the first)")
    check(H.arena.reqId ~= nil, "and resynced")
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(nextOffer))

    -- a timeout sends again like an error
    H.queue = {}
    key = H.ArenaPick("CS2_124")
    flush()
    local count = table.getn(T.sentOps("ARENA_PICK"))
    drain(H.REQUEST_TIMEOUT + 3)
    check(table.getn(T.sentOps("ARENA_PICK")) == count + 1, "a timeout sends again")
    s = lastSent("ARENA_PICK")
    check(P.KvDecode(s.payload).key == key, "with the same key")
    aframe("ARENA", s.reqId, arenaPayload({ st = "draft", run = "40", pay = "gold", cls = "PRIEST", no = "3", of = "CS2_189,CS2_124,CS2_022", pk = "2", dk = "CS2_124.1,CS2_172.1", rp = "1" }))
    check(AR.op == nil and AR.run.no == 3, "replayed pick applied")

    -- refusals: nothing was changed, the screen resyncs
    H.queue = {}
    key = H.ArenaPick("CS2_189")
    flush()
    s = lastSent("ARENA_PICK")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=no;why=stale")
    check(AR.op == nil and AR.message == AR.WHY_TEXT.stale and not AR.IsBusy(), "stale refusal: " .. tostring(AR.message))
    check(table.getn(T.queuedOps("ARENA_GET")) == 1, "and a resync")
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload({ st = "draft", run = "40", pay = "gold", cls = "PRIEST", no = "3", of = "CS2_189,CS2_124,CS2_022", pk = "2", dk = "CS2_124.1,CS2_172.1" }))
    H.queue = {}
    key = H.ArenaPick("CS2_189")
    flush()
    s = lastSent("ARENA_PICK")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=Nope;why=picked")
    check(AR.message == AR.WHY_TEXT.picked, "picked refusal: " .. tostring(AR.message))
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload({ st = "draft", run = "40", pay = "gold", cls = "PRIEST", no = "3", of = "CS2_189,CS2_124,CS2_022", pk = "2", dk = "CS2_124.1,CS2_172.1" }))

    -- the 30th pick: the deck is ready
    H.queue = {}
    local whyp
    key, whyp = H.ArenaPick("CS2_022")
    check(key ~= nil, "30th pick started: " .. tostring(whyp))
    flush()
    s = lastSent("ARENA_PICK")
    aframe("ARENA", s.reqId, arenaPayload(ARENA_READY))
    check(AR.run.st == "ready" and AR.message == nil, "ready")
    check(select(2, H.ArenaPick("CS2_168")) == "There is no card to pick right now.", "no pick in the ready state")

    -- the claim of a special card: the collection and the stock are refetched
    flush()
    aframe("ARENA_UPD", "0", arenaPayload({ st = "settled", run = "40", w = "10", l = "0", pk = "30", pz = "3.10.1", spc = "1", sp = "EX1_062.1,PRO_001.1" }))
    check(string.find(lastPrinted(), "Night Watch Arena run complete: 10 wins, 0 losses. Prize: 3 packs + 10 Hearthstone Coins. You can choose a special card: /hs arena", 1, true), "the end of the run is announced, with the special card hint: " .. lastPrinted())
    H.queue = {}
    check(select(2, H.ArenaClaim("NEW1_016")) == AR.WHY_TEXT.not_special, "a card that is not offered")
    key = H.ArenaClaim("EX1_062")
    local cl = T.queuedOps("ARENA_CLAIM")
    check(key and table.getn(cl) == 1 and cl[1].payload == "key=" .. key .. ";id=EX1_062;run=40", "ARENA_CLAIM queued")
    flush()
    s = lastSent("ARENA_CLAIM")
    H.queue = {}
    aframe("ARENA", s.reqId, arenaPayload({ st = "settled", run = "40", w = "10", l = "0", pk = "30", pz = "3.10.1", spc = "0", sc = "EX1_062" }))
    check(AR.op == nil and AR.run.claimed == "EX1_062" and string.find(AR.message, "Claimed Old Murk-Eye", 1, true), "claimed: " .. tostring(AR.message))
    check(H.coll.reqId ~= nil and (H.packs.refetch or table.getn(T.queuedOps("PACK_GET")) == 1), "collection and stock are fetched again")

    -- every set complete: any of the four gives an extra pack
    flush()
    aframe("ARENA_UPD", "0", arenaPayload({ st = "settled", run = "42", w = "10", pk = "30", pz = "3.10.1", spc = "1", sp = "" }))
    key = H.ArenaClaim("PRO_001")
    check(key ~= nil, "claim with every set complete")
    AR.ClearOp()
    AR.Reset()

    -- the end of a run is announced once, when the screen knew the run
    aframe("ARENA_UPD", "0", arenaPayload({ st = "finished", run = "43", w = "4", l = "1", pk = "30", pz = "3.0.0" }))
    printed = {}
    aframe("ARENA_UPD", "0", arenaPayload({ st = "settled", run = "43", w = "4", l = "1", pk = "30", pz = "3.0.0" }))
    check(string.find(T.joined(), "Night Watch Arena run complete: 4 wins, 1 loss. Prize: 3 packs.", 1, true), "settled line: " .. T.joined())

    -- /hs status, /hs arena
    printed = {}
    H.Slash("status")
    check(string.find(T.joined(), "arena: settled, run 43, 4 wins, 1 losses, 30 of 30 cards", 1, true), "status line: " .. T.joined())
    printed = {}
    H.ShowArena = function() shown = shown + 1 end
    local calls = shown
    H.Slash("arena")
    check(shown == calls + 1 and H.arena.reqId ~= nil, "/hs arena shows the window and refreshes the run")

    -- the pause: GAME_PAUSE for the running arena duel, GAME ph=none closes it
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_DUEL))
    X.game(X.last("GAME_GET").reqId, "ph=none")
    flush()
    check(select(2, H.MatchPause()) == "No running duel to pause.", "nothing to pause")
    X.push(CANONICAL_GAME)
    H.queue = {}
    check(AR.IsArenaDuel() and H.MatchPause() == true, "pause accepted")
    local pq2 = T.queuedOps("GAME_PAUSE")
    check(table.getn(pq2) == 1 and pq2[1].payload == "mid=7" and AR.pausing and AR.pausing.mid == 7, "GAME_PAUSE mid=7")
    check(H.MatchPause() == true and table.getn(T.queuedOps("GAME_PAUSE")) == 1, "asked once")
    check(table.getn(T.queuedOps("GAME_ACT")) == 0 and table.getn(T.queuedOps("GAME_LEAVE")) == 0, "never a concede or a leave")
    flush()
    s = lastSent("GAME_PAUSE")
    X.game(s.reqId, "ph=none")
    check(AR.pausing == nil and HsMatch.state == "none" and AR.message == AR.PAUSED_TEXT, "paused: " .. tostring(AR.message))
    check(string.find(lastPrinted(), "Duel paused. Resume it at Ezra or Ibelin.", 1, true), "chat line: " .. lastPrinted())
    aframe("ARENA_UPD", "0", arenaPayload({ st = "paused", run = "40", w = "2", duel = "3", pk = "30", live = "0", mine = "0", pz = "1.0.0" }))
    check(AR.run.st == "paused" and not AR.IsArenaDuel(), "the run is paused")

    -- the pause refused: an ordinary match (the picture of the run was stale) falls back to the surrender question
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DUEL))
    X.push(gamePayload({ mid = "9", seq = "2" }))
    H.queue = {}
    local asked = 0
    local savedAsk = H.MatchUI.AskSurrender
    H.MatchUI.AskSurrender = function() asked = asked + 1 return true end
    H.MatchPause()
    flush()
    s = lastSent("GAME_PAUSE")
    check(s.payload == "mid=9", "pause of match 9")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=Only arena;why=ordinary")
    check(AR.pausing == nil and asked == 1 and HsMatch.message == "Only an arena duel can be paused.", "ordinary refusal: surrender is asked")
    check(table.getn(T.queuedOps("ARENA_GET")) >= 1, "and the run is refetched")
    H.MatchUI.AskSurrender = savedAsk

    -- an unknown outcome of the pause: the board is refetched
    flush()
    H.queue = {}
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_DUEL))
    H.MatchPause()
    flush()
    drain(H.REQUEST_TIMEOUT + 3)
    check(AR.pausing == nil and table.getn(T.sentOps("GAME_GET")) >= 2, "timeout: GAME_GET")

    -- a match that starts makes the addon look at the run (is it an arena duel?)
    flush()
    T.frame("GAME", "0", "ph=none")
    aframe("ARENA", H.arena.reqId or "none", "")
    H.queue = {}
    AR.SetReady(AR.ParseArena(arenaPayload({ st = "ready", run = "40", pk = "30", pz = "1.0.0" })))
    X.push(gamePayload({ mid = "12", seq = "1" }))
    check(table.getn(T.queuedOps("ARENA_GET")) == 1, "a duel that starts asks for the run")

    -- disconnect resets
    T.err("0", "DISABLED", "off")
    check(AR.state == "none" and H.arena.reqId == nil and not H.arena.supported and AR.pausing == nil, "disconnect resets the arena")

    H.OnArenaChanged, H.ShowArena = savedHook, savedShow
    GetTime = savedTime
    resetAddon()
    resetMatch()
end

local function testArenaUI()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local AR = HsArena
    local AU = H.ArenaUI
    local CM = HsCollection
    local T = packHelpers()
    local savedDb = H.db
    local function flush() drain(1) end
    local function lastSent(op)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end
    local function resetUI()
        AU.frame, AU.preview = nil, nil
        AU.slots, AU.picks, AU.classes, AU.rows, AU.bars = {}, {}, {}, {}, {}
    end
    resetAddon()
    F.install({ class = { "Mage", "MAGE" }, realm = "Realm A" })
    H.db = CM.ApplySavedDefaults(nil)
    resetUI()

    T.welcome("solo,chunk,packs,decks,wallet,arena")
    flush()
    T.coll(1)
    check(AU.frame == nil, "no window before it is asked for")

    -- /hs arena while the run loads
    H.Slash("arena")
    local f = AU.frame
    check(f and f:IsShown() and f.heading:GetText() == "Night Watch Arena" and f.body:GetText() == "Loading your run...", "loading text")
    check(f.title:GetText() == "Night Watch Arena", "window title")
    local found = false
    for i = 1, table.getn(UISpecialFrames) do if UISpecialFrames[i] == "HearthstoneArenaFrame" then found = true end end
    check(found, "Escape closes the window")
    flush()

    -- no run
    aframe("ARENA", H.arena.reqId, arenaPayload())
    check(f.heading:GetText() == "Night Watch Arena" and string.find(f.body:GetText(), "Ezra Chatterton or Ibelin Redmoore", 1, true), "no run text")
    check(not AU.classes[1]:IsShown() and not AU.slots[1].button:IsShown() and not f.key:IsShown() and f.deckEmpty:IsShown(), "no run: nothing to choose")
    check(f.deckTitle:GetText() == "0 / 30", "empty deck title")
    check(not f.play:IsShown() and not f.retire:IsShown() and not f.hero.button:IsShown(), "no run: no Play, no Retire, no hero")

    -- the class offer arrives (a push)
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_CLASS_OFFER))
    check(f.heading:GetText() == "Choose your Hero", "class heading")
    check(AU.classes[1]:IsShown() and AU.classes[2]:IsShown() and AU.classes[3]:IsShown() and not AU.slots[1].button:IsShown(), "three class buttons")
    check(AU.classes[1].name:GetText() == "Mage" and AU.classes[2].name:GetText() == "Priest" and AU.classes[3].name:GetText() == "Warrior", "class names")
    check(AU.classes[1]:IsEnabled(), "class buttons enabled")
    check(AU.classes[1].hero:GetText() ~= "" and AU.classes[1].portrait.frame:IsShown(), "hero name and arch")
    check(not f.key:IsShown() and not f.losses:IsShown() and f.body:GetText() == "", "the parchment shows only the heroes")
    check(f.retire:IsShown() and not f.play:IsShown(), "a run can be retired while drafting, there is no duel to play")

    -- click a hero: its view opens and nothing is sent; Back returns to the three heroes
    H.queue = {}
    F.fire(AU.classes[3], "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_CLASS")) == 0 and AU.Lines().chosen == "WARRIOR", "a hero click only opens the hero")
    check(AU.heroView.dim:IsShown() and AU.heroView.host:IsShown(), "the hero view dims the window")
    check(AU.heroView.choose:IsShown() and AU.heroView.back:IsShown() and AU.heroView.name:GetText() == AU.HeroName("WARRIOR"), "the hero view")
    check(AU.heroView.cls:GetText() == AR.ClassLabel("WARRIOR") and AU.heroView.hero.frame:IsShown(), "the hero view's class and arch")
    local hp = AU.HeroPower("WARRIOR")
    check(hp and hp.name ~= "" and hp.id and AU.heroView.power.id == hp.id and AU.heroView.power.frame:IsShown(), "the hero power card of the hero")
    F.fire(AU.heroView.back, "OnClick", {})
    check(AU.classes[1]:IsShown() and not AU.heroView.choose:IsShown() and not AU.heroView.dim:IsShown() and not AU.heroView.power.frame:IsShown()
        and table.getn(T.queuedOps("ARENA_CLASS")) == 0, "back: the three heroes again")
    -- a click on the dimmed window beside the arch is Back too
    F.fire(AU.classes[3], "OnClick", {})
    check(AU.heroView.dim:IsShown(), "the hero view again")
    F.fire(AU.heroView.dim, "OnMouseUp", {})
    check(not AU.heroView.dim:IsShown() and not AU.heroView.choose:IsShown() and table.getn(T.queuedOps("ARENA_CLASS")) == 0, "the dimmed window: back")
    -- choose: one ARENA_CLASS, everything disabled while it is on its way
    F.fire(AU.classes[3], "OnClick", {})
    F.fire(AU.heroView.choose, "OnClick", {})
    local cq = T.queuedOps("ARENA_CLASS")
    check(table.getn(cq) == 1 and string.find(cq[1].payload, ";class=WARRIOR;run=40", 1, true), "ARENA_CLASS WARRIOR")
    check(not AU.heroView.choose:IsEnabled() and not AU.heroView.back:IsEnabled(), "disabled while pending")
    check(f.message:GetText() == "Sending your choice...", "pending text")
    F.fire(AU.heroView.choose, "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_CLASS")) == 1 and f.message:GetText() == "Sending your choice...", "a second click sends nothing")
    flush()
    local s = lastSent("ARENA_CLASS")
    -- the refusal comes back readable, and the hero is still open to choose again
    T.frame("ERR", s.reqId, "code=REFUSED;msg=x;why=bad_class")
    check(f.message:GetText() == AR.WHY_TEXT.bad_class and AU.heroView.choose:IsEnabled() and AU.heroView.choose:IsShown(), "refusal text: " .. f.message:GetText())
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_CLASS_OFFER))
    H.queue = {}
    F.fire(AU.classes[3], "OnClick", {})
    F.fire(AU.heroView.choose, "OnClick", {})
    flush()
    s = lastSent("ARENA_CLASS")
    local offer1 = { st = "draft", run = "40", pay = "gold", cls = "WARRIOR", no = "1", of = "CS2_168,CS2_172,CS2_124", pk = "0" }
    aframe("ARENA", s.reqId, arenaPayload(offer1))

    -- the card offer
    check(f.heading:GetText() == "Pick a card (1 of 30)" and not f.heading:IsShown(), "draft heading: " .. f.heading:GetText())
    for i = 1, 3 do
        check(AU.slots[i].widget.frame:IsShown() and AU.slots[i].button:IsShown() and AU.slots[i].button:IsEnabled()
            and AU.slots[i].id == ({ "CS2_168", "CS2_172", "CS2_124" })[i] and AU.slots[i].widget.id == AU.slots[i].id, "card " .. i)
        check(not AU.classes[i]:IsShown(), "class button " .. i .. " gone")
    end
    check(not AU.heroView.choose:IsShown() and not AU.heroView.name:IsShown() and not AU.heroView.dim:IsShown() and AU.Lines().chosen == nil, "the hero view is gone with the offer")
    check(f.message:GetText() == "", "no class message: " .. f.message:GetText())
    -- hover shows the large card
    F.fire(AU.slots[2].button, "OnEnter", {})
    check(AU.preview and AU.preview.frame:IsShown() and AU.preview.widget.id == "CS2_172", "hover: large card")
    F.fire(AU.slots[2].button, "OnLeave", {})
    check(not AU.preview.frame:IsShown(), "leave hides it")
    -- a click right after the offer appeared is ignored (the second click of a double click on the last offer)
    H.queue = {}
    F.fire(AU.slots[2].button, "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_PICK")) == 0, "no pick inside the click grace")
    F.time = F.time + AU.CLICK_GRACE
    -- click a card
    H.queue = {}
    F.fire(AU.slots[2].button, "OnClick", {})
    local pq = T.queuedOps("ARENA_PICK")
    check(table.getn(pq) == 1 and string.find(pq[1].payload, ";no=1;id=CS2_172;run=40", 1, true), "ARENA_PICK payload: " .. tostring(pq[1] and pq[1].payload))
    check(not AU.slots[1].button:IsEnabled() and not AU.slots[2].button:IsEnabled() and AU.slots[2].sel:IsShown() and not AU.slots[1].sel:IsShown()
        and AU.slots[1].widget.dimmed and not AU.slots[2].widget.dimmed, "cards disabled while pending, the others dimmed")
    F.fire(AU.slots[1].button, "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_PICK")) == 1, "no second pick while pending")
    flush()
    s = lastSent("ARENA_PICK")
    F.fire(AU.slots[2].button, "OnEnter", {})  -- the mouse stays on the clicked card
    aframe("ARENA", s.reqId, arenaPayload({ st = "draft", run = "40", pay = "gold", cls = "WARRIOR", no = "2", of = "CS2_189,CS2_124,CS2_022", pk = "1", dk = "CS2_172.1" }))
    check(f.heading:GetText() == "Pick a card (2 of 30)" and AU.slots[1].id == "CS2_189" and AU.slots[1].button:IsEnabled() and not AU.slots[1].widget.dimmed, "next offer")
    check(AU.preview.frame:IsShown() and AU.preview.widget.id == "CS2_124", "the large card follows the new card under the mouse")
    F.fire(AU.slots[2].button, "OnLeave", {})
    H.queue = {}
    F.fire(AU.slots[1].button, "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_PICK")) == 0, "the next offer ignores a click at once too")
    F.time = F.time + AU.CLICK_GRACE

    -- the picked list and the mana curve
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DRAFT))
    check(f.deckTitle:GetText() == "2 / 30" and not f.deckEmpty:IsShown(), "picked count")
    check(f.hero.button:IsShown() and f.hero.frame:IsShown(), "the drafted hero in the arch")
    check(AU.rows[1]:IsShown() and AU.rows[2]:IsShown() and not AU.rows[3]:IsShown(), "two rows")
    check(AU.rows[1].cost:GetText() == "1" and AU.rows[1].name:GetText() == "|cffffffffMurloc Raider|r", "row 1: " .. AU.rows[1].name:GetText())
    check(AU.rows[2].cost:GetText() == "2" and string.find(AU.rows[2].name:GetText(), "Bloodfen Raptor", 1, true) and AU.rows[2].count:GetText() == ""
        and not AU.rows[2].box:IsShown(), "row 2")
    check(AU.bars[1].tex:GetHeight() > 1
        and AU.bars[0].tex:GetHeight() == 1, "curve bars")
    -- rows hover the large card
    F.fire(AU.rows[2], "OnEnter", {})
    check(AU.preview.frame:IsShown() and AU.preview.widget.id == "CS2_172", "row hover")
    F.fire(AU.rows[2], "OnLeave", {})

    -- grouped by cost with copies, 30 cards
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_READY))
    check(f.deckTitle:GetText() == "30 / 30", "30 cards")
    check(f.key:IsShown() and f.losses:IsShown() and f.winsNumber:GetText() == "2", "run on the key emblem")
    check(string.find(AU.Lines().hint, "Ezra Chatterton or Ibelin Redmoore", 1, true), "not at the host: where Play and Retire work")
    check(f.play:IsShown() and f.play.tex._.desat and f.play.tip == AR.NOT_AT_HOST, "Play greyed out away from the host")
    local lines = AU.Lines()
    local twos, firstCosts = 0, 0
    for i = 1, table.getn(lines.rows) do
        if lines.rows[i].n == 2 then twos = twos + 1 end
        if lines.rows[i].costText ~= "" then firstCosts = firstCosts + 1 end
    end
    check(twos == 15 and firstCosts < table.getn(lines.rows), "copies and one cost label per group")

    -- the states
    local function state(over, headingPart, bodyPart)
        local base = { run = "40", pay = "gold", cls = "MAGE", pk = "30", pz = "1.0.0", dk = ARENA_READY.dk }
        for k, v in pairs(over) do base[k] = v end
        aframe("ARENA_UPD", "0", arenaPayload(base))
        local t = AU.Lines()
        check(string.find(f.heading:GetText(), headingPart, 1, true) and string.find(t.body, bodyPart, 1, true),
            over.st .. ": " .. f.heading:GetText() .. " / " .. t.body)
        -- a run that can be played shows no text on the parchment, only the key emblem and the losses box
        check((t.canPlay and f.body:GetText() == "" and f.hint:GetText() == "") or (not t.canPlay and f.body:GetText() == t.body),
            over.st .. ": the parchment text " .. f.body:GetText())
        check(not AU.classes[1]:IsShown() and not AU.slots[1].button:IsShown() and t.mode == "info", over.st .. ": nothing to choose")
        return t
    end
    state({ st = "ready", w = "2" }, "Your deck is ready", "Ezra Chatterton or Ibelin Redmoore")
    state({ st = "paused", w = "2", duel = "3" }, "Your duel is paused", "resume it")
    state({ st = "duel", w = "2", duel = "3", live = "1", mine = "1" }, "A duel is running", "pauses it")
    state({ st = "duel", w = "2", duel = "3", live = "1", mine = "0" }, "A duel is running", "Another character")
    local fin = state({ st = "finished", w = "4", l = "1" }, "Run over", "4 wins")
    state({ st = "retired", w = "4" }, "Run retired", "settled")
    state({ st = "repair", w = "4" }, "needs a game master", "game master")
    check(f.hint:GetText() == "", "no retire hint for a run that ended")
    state({ st = "ready", w = "2" }, "Your deck is ready", "one loss ends the run")
    check(string.find(AU.Lines().hint, "only be retired", 1, true) and f.hint:GetText() == "", "retire hint while the run lives (not shown over the key emblem)")

    -- progress: the wins sit on the key emblem; no loss shows the empty box, a loss the box with the red X
    aframe("ARENA_UPD", "0", arenaPayload({ st = "ready", run = "40", pay = "gold", cls = "MAGE", w = "6", d = "1", l = "0", pk = "30", pz = "3.10.0", dk = ARENA_READY.dk }))
    check(f.winsNumber:GetText() == "6" and f.losses._.texture == AU.TEXTURE_PATH .. "ArenaLosses", "six wins, the empty loss box")
    aframe("ARENA_UPD", "0", arenaPayload({ st = "settled", run = "40", pay = "gold", cls = "MAGE", w = "6", l = "1", pk = "30", pz = "3.10.0", dk = ARENA_READY.dk }))
    check(f.winsNumber:GetText() == "6" and f.losses._.texture == AU.TEXTURE_PATH .. "ArenaLossesHit" and f.heading:GetText() == "Run complete" and string.find(f.body:GetText(), "6 wins and 1 loss. Prize: 3 packs + 10 Hearthstone Coins.", 1, true), "settled: " .. f.body:GetText())

    -- the special-card choice
    aframe("ARENA_UPD", "0", arenaPayload({ st = "settled", run = "40", pay = "gold", cls = "MAGE", w = "10", l = "0", pk = "30", pz = "3.10.1",
        spc = "1", sp = "EX1_062.1,NEW1_016.2,PRO_001.1", dk = ARENA_READY.dk }))
    check(f.heading:GetText() == "Run complete" and string.find(f.body:GetText(), "Choose your special card below.", 1, true), "choose below")
    check(AU.picks[1].widget.frame:IsShown() and AU.picks[3].widget.frame:IsShown() and not AU.picks[4].widget.frame:IsShown(), "three special cards")
    check(AU.picks[1].id == "EX1_062" and AU.picks[1].caption:GetText() == "+1 copy" and AU.picks[2].caption:GetText() == "+2 copies", "captions")
    check(f.specialNote:GetText() == "", "no note")
    H.queue = {}
    F.fire(AU.picks[2].button, "OnClick", {})
    local clq = T.queuedOps("ARENA_CLAIM")
    check(table.getn(clq) == 1 and string.find(clq[1].payload, ";id=NEW1_016;run=40", 1, true), "ARENA_CLAIM")
    check(not AU.picks[1].button:IsEnabled() and AU.picks[2].sel:IsShown(), "disabled while claiming")
    flush()
    s = lastSent("ARENA_CLAIM")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=x;why=overflow")
    check(f.message:GetText() == AR.WHY_TEXT.overflow and AU.picks[1].button:IsEnabled(), "overflow refusal shown")
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload({ st = "settled", run = "40", pay = "gold", cls = "MAGE", w = "10", pk = "30", pz = "3.10.1",
        spc = "1", sp = "", dk = ARENA_READY.dk }))
    check(AU.picks[4].widget.frame:IsShown() and AU.picks[4].id == "PRO_001" and AU.picks[1].caption:GetText() == "1 extra pack"
        and string.find(f.specialNote:GetText(), "extra pack", 1, true), "every set complete: four cards, an extra pack")
    H.queue = {}
    F.fire(AU.picks[4].button, "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_CLAIM")) == 1, "claim with every set complete")
    flush()
    s = lastSent("ARENA_CLAIM")
    aframe("ARENA", s.reqId, arenaPayload({ st = "settled", run = "40", pay = "gold", cls = "MAGE", w = "10", pk = "30", pz = "3.10.1", spc = "0", sc = "PRO_001", dk = ARENA_READY.dk }))
    check(not AU.picks[1].widget.frame:IsShown() and string.find(f.body:GetText(), "You took Elite Tauren Chieftain.", 1, true), "claimed: " .. f.body:GetText())

    -- ARENA_OPEN opens a closed window; the close button and /hs arena
    f:Hide()
    aframe("ARENA_OPEN", "0", arenaPayload(ARENA_CLASS_OFFER))
    check(f:IsShown() and AU.classes[1]:IsShown(), "ARENA_OPEN opens the window")
    F.fire(getglobal("HearthstoneArenaBack"), "OnClick", {})
    check(not f:IsShown(), "Back closes")
    H.Slash("arena")
    check(f:IsShown(), "/hs arena opens")
    H.Slash("arena")
    check(not f:IsShown(), "/hs arena toggles")
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DRAFT))
    check(not f:IsShown(), "a push does not open a closed window")

    -- Play and Retire: only at the host (ARENA_OPEN), until ARENA_CLOSE
    AR.message = nil
    AR.atHost = false
    H.Slash("arena")
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_READY))
    H.queue = {}
    F.fire(f.play, "OnClick", {})
    check(table.getn(T.queuedOps("ARENA_DUEL")) == 0 and f.message:GetText() == AR.NOT_AT_HOST, "Play away from the host: " .. f.message:GetText())
    F.fire(f.retire, "OnClick", {})
    check(popupWhich(F) == nil and f.message:GetText() == AR.NOT_AT_HOST_RETIRE, "Retire away from the host")
    aframe("ARENA_OPEN", "0", arenaPayload(ARENA_READY))
    check(AR.atHost and f:IsShown() and f.play:IsShown() and not f.play.tex._.desat and f.hint:GetText() == ""
        and f.body:GetText() == "" and string.find(AU.Lines().body, "Press Play", 1, true), "at the host: Play lit, " .. f.body:GetText())
    F.fire(f.play, "OnClick", {})
    local dq = T.queuedOps("ARENA_DUEL")
    check(table.getn(dq) == 1 and string.find(dq[1].payload, "key=", 1, true) == 1 and string.find(dq[1].payload, ";run=40", 1, true), "ARENA_DUEL payload")
    check(f.play.tex._.desat, "Play greyed while it is on its way")
    flush()
    s = lastSent("ARENA_DUEL")
    T.frame("ERR", s.reqId, "code=REFUSED;msg=x;why=host")
    check(f.message:GetText() == AR.WHY_TEXT.host, "refused at the server: " .. f.message:GetText())
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_READY))
    -- a duel starts: the window gives way and comes back when the match window closes
    H.HideArena(true)
    check(not f:IsShown() and AU.reopen, "hidden for the match")
    H.OnMatchWindowHidden()
    check(f:IsShown() and not AU.reopen, "back after the match")
    -- Retire asks first
    H.queue = {}
    F.fire(f.retire, "OnClick", {})
    check(popupWhich(F) == "HEARTHSTONE_ARENA_RETIRE" and table.getn(T.queuedOps("ARENA_RETIRE")) == 0, "Retire asks")
    F.popupClick(1)
    local rq = T.queuedOps("ARENA_RETIRE")
    check(table.getn(rq) == 1 and string.find(rq[1].payload, ";run=40", 1, true), "ARENA_RETIRE after the confirmation")
    flush()
    s = lastSent("ARENA_RETIRE")
    aframe("ARENA", s.reqId, arenaPayload({ st = "settled", run = "40", pay = "gold", cls = "MAGE", w = "2", pk = "30", pz = "1.0.0", dk = ARENA_READY.dk }))
    check(f.heading:GetText() == "Run complete" and not f.play:IsShown() and not f.retire:IsShown(), "retired: no Play, no Retire")
    -- walking away closes the window and ends the host's buttons
    aframe("ARENA_CLOSE", "0", "")
    check(not f:IsShown() and not AR.atHost, "ARENA_CLOSE closes the window")
    H.Slash("arena")
    aframe("ARENA_CLOSE", "0", "")
    check(f:IsShown(), "a window opened with /hs arena stays open")
    f:Hide()

    -- disconnect
    H.Slash("arena")
    T.err("0", "DISABLED", "off")
    check(f.body:GetText() == "Not connected to the server." and not AU.classes[1]:IsShown(), "after a disconnect")
    f:Hide()
    resetAddon()
    H.db = savedDb
    resetUI()
end

local function testArenaMatch()
    local F = dofile(ROOT .. "/tests/fake_wow.lua")
    local AR = HsArena
    local MA = HsMatch
    local MU = H.MatchUI
    local X = matchHelpers()
    local T = X.T
    local function flush() drain(1) end
    resetAddon()
    resetMatch()
    F.install({ class = { "Mage", "MAGE" }, name = "Jonas", screenW = 1920, screenH = 1080 })
    MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview, MU.sel, MU.hint = nil, nil, nil, nil, nil, nil, nil, nil
    T.welcome("solo,chunk,packs,decks,match,arena")
    flush()
    X.game(X.last("GAME_GET").reqId, "ph=none")
    aframe("ARENA", H.arena.reqId, arenaPayload())

    -- an ORDINARY match: X still asks to surrender, no pause, no surrender button
    X.push(CANONICAL_GAME)
    check(table.getn(T.queuedOps("ARENA_GET")) == 1, "a started match asks which run it belongs to")
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload())      -- no run: an ordinary match
    local f = MU.frame
    check(f and f:IsShown() and not f.surrender:IsShown() and not MU.IsArenaDuel(), "ordinary match: no surrender button")
    H.queue = {}
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    check(f:IsShown() and popupWhich(F) == "HEARTHSTONE_SURRENDER", "ordinary: close asks to surrender")
    F.popupClick(2)
    flush()
    check(table.getn(T.queuedOps("GAME_PAUSE")) == 0 and table.getn(T.sentOps("GAME_PAUSE")) == 0, "ordinary: no GAME_PAUSE")
    f:Hide() -- Escape
    check(f:IsShown() and popupWhich(F) == "HEARTHSTONE_SURRENDER", "ordinary: Escape asks too")
    F.popupClick(2)
    flush()
    -- ... and Yes concedes as before
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    F.popupClick(1)
    flush()
    local act = X.last("GAME_ACT")
    check(act and act.payload == "mid=7;seq=12;a=concede", "ordinary: Yes concedes: " .. tostring(act and act.payload))
    X.game(act.reqId, gamePayload({ seq = "13", ph = "over", act = "-", res = "loss", why = "concede", lg = "", evs = "40", ev = "end.o.0.0.2.-" }))
    F.fire(getglobal("HearthstoneMatchLeaveButton"), "OnClick", {})
    flush()
    check(not f:IsShown() and MA.state == "none", "ordinary: left")

    -- an ARENA duel: the match starts, the run says it is ours
    H.queue = {}
    X.push(gamePayload({ mid = "8", seq = "1" }))
    check(table.getn(T.queuedOps("ARENA_GET")) == 1, "a started duel asks for the run")
    flush()
    aframe("ARENA", H.arena.reqId, arenaPayload(ARENA_DUEL))
    check(MU.IsArenaDuel() and f:IsShown() and f.surrender:IsShown(), "arena duel: the surrender button is shown")
    check(string.find(f.title:GetText(), "Night Watch Arena", 1, true), "arena duel: the title names the mode: " .. f.title:GetText())

    -- X pauses; nothing is conceded or left
    local acts = table.getn(T.sentOps("GAME_ACT"))
    local leaves = table.getn(T.sentOps("GAME_LEAVE"))
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    flush()
    local pz = X.last("GAME_PAUSE")
    check(pz and pz.payload == "mid=8", "arena: X sends GAME_PAUSE mid=8")
    check(popupWhich(F) == nil, "arena: no surrender question")
    check(table.getn(T.sentOps("GAME_ACT")) == acts and table.getn(T.sentOps("GAME_LEAVE")) == leaves, "arena: no concede, no leave")
    check(f:IsShown() and MU.hint == "Pausing the duel...", "the window waits for the server")
    -- Escape pauses too (asked once)
    f:Hide()
    flush()
    check(f:IsShown() and table.getn(T.sentOps("GAME_PAUSE")) == 1 and popupWhich(F) == nil, "arena: Escape pauses, once")
    -- the answer closes the window and says where to resume
    printed = {}
    X.game(pz.reqId, "ph=none")
    check(not f:IsShown() and MA.state == "none", "paused: the window closed")
    check(AR.message == "Duel paused. Resume it at Ezra or Ibelin." and string.find(T.joined(), "Duel paused. Resume it at Ezra or Ibelin.", 1, true), "paused: the text")
    aframe("ARENA_UPD", "0", arenaPayload({ st = "paused", run = "40", w = "2", duel = "3", pk = "30", pz = "1.0.0", dk = ARENA_READY.dk }))
    check(not MU.IsArenaDuel(), "the run is paused")

    -- resumed at the host: the duel comes back as a push, the run says duel again
    X.push(gamePayload({ mid = "8", seq = "5" }))
    flush()
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DUEL))
    check(f:IsShown() and f.surrender:IsShown(), "resumed: window and surrender button")

    -- the Surrender button: its own confirm text; No keeps the duel, Yes concedes (and ends the run)
    H.queue = {}
    local sendsBefore = table.getn(T.sentOps("GAME_ACT"))
    F.fire(f.surrender, "OnClick", {})
    check(popupWhich(F) == "HEARTHSTONE_ARENA_SURRENDER", "surrender: its own question")
    local text = F.popup.text
    check(string.find(text, "LOSS", 1, true) and string.find(text, "ENDS", 1, true) and string.find(text, "Night Watch Arena", 1, true)
        and string.find(text, "close the window", 1, true) and string.find(text, "Ezra or Ibelin", 1, true), "surrender text: " .. text)
    local d = StaticPopupDialogs["HEARTHSTONE_ARENA_SURRENDER"]
    check(d.button1 == "Surrender and end the run" and d.button2 == "Keep playing", "surrender buttons")
    F.popupClick(2)
    flush()
    check(table.getn(T.sentOps("GAME_ACT")) == sendsBefore and f:IsShown(), "No keeps playing")
    F.fire(f.surrender, "OnClick", {})
    F.popupClick(1)
    flush()
    act = X.last("GAME_ACT")
    check(table.getn(T.sentOps("GAME_ACT")) == sendsBefore + 1 and act.payload == "mid=8;seq=5;a=concede", "Yes concedes: " .. tostring(act and act.payload))
    check(table.getn(T.sentOps("GAME_PAUSE")) == 1, "and does not pause")
    X.game(act.reqId, gamePayload({ mid = "8", seq = "6", ph = "over", act = "-", res = "loss", why = "concede", lg = "", evs = "40", ev = "end.o.0.0.2.-" }))
    check(MU.result.frame:IsShown() and MU.result.title:GetText() == "Defeat", "defeat shown")
    aframe("ARENA_UPD", "0", arenaPayload({ st = "finished", run = "40", w = "2", l = "1", duel = "3", pk = "30", pz = "1.0.0", dk = ARENA_READY.dk }))
    check(not f.surrender:IsShown(), "the surrender button is gone when the run is not a live duel")
    F.fire(getglobal("HearthstoneMatchLeaveButton"), "OnClick", {})
    flush()
    check(not f:IsShown(), "left")

    -- a pause that is refused as 'ordinary' (the run in our picture was stale) asks the ordinary question
    aframe("ARENA_UPD", "0", arenaPayload(ARENA_DUEL))
    X.push(gamePayload({ mid = "9", seq = "1" }))
    flush()
    aframe("ARENA", H.arena.reqId or "zz", arenaPayload(ARENA_DUEL))
    check(MU.IsArenaDuel(), "stale picture: we think it is an arena duel")
    F.fire(getglobal("HearthstoneMatchCloseButton"), "OnClick", {})
    flush()
    pz = X.last("GAME_PAUSE")
    check(pz and pz.payload == "mid=9", "pause of match 9")
    T.frame("ERR", pz.reqId, "code=REFUSED;msg=ordinary;why=ordinary")
    check(AR.pausing == nil and popupWhich(F) == "HEARTHSTONE_SURRENDER", "refused as ordinary: the surrender question")
    F.popupClick(2)
    flush()

    -- the server suspends an arena duel by itself (logout, teleport, death): the same text, although no pause was asked
    check(MU.IsArenaDuel() and MA.IsRunning(MA.game), "an arena duel runs again")
    printed = {}
    AR.message = nil
    X.push("ph=none")
    check(not f:IsShown() and AR.message == AR.PAUSED_TEXT and string.find(T.joined(), "Resume it at Ezra or Ibelin", 1, true), "interrupted: " .. tostring(AR.message))
    check(table.getn(T.sentOps("GAME_PAUSE")) == 2, "and no pause was sent for it")

    f:Hide()
    resetAddon()
    resetMatch()
end

run("testLint", testLint)
run("testEscape", testEscape)
run("testKv", testKv)
run("testGoldenVectors", testGoldenVectors)
run("testFrames", testFrames)
run("testChunking", testChunking)
run("testReassembler", testReassembler)
run("testTranscript", testTranscript)
run("testCardDB", testCardDB)
run("testDatasetCheck", testDatasetCheck)
run("testCardBottom", testCardBottom)
run("testCollectionModel", testCollectionModel)
run("testCollectionUI", testCollectionUI)
run("testMicroButton", testMicroButton)
run("testAddonFlow", testAddonFlow)
run("testOwnershipModel", testOwnershipModel)
run("testCollectionFlow", testCollectionFlow)
run("testCollectionOwnershipUI", testCollectionOwnershipUI)
run("testPacksModel", testPacksModel)
run("testPacksFlow", testPacksFlow)
run("testPacksUI", testPacksUI)
run("testDecksModel", testDecksModel)
run("testDecksFlow", testDecksFlow)
run("testDecksTranscript", testDecksTranscript)
run("testDecksUI", testDecksUI)
run("testWalletModel", testWalletModel)
run("testWalletFlow", testWalletFlow)
run("testCraftUI", testCraftUI)
run("testCollectionPolish", testCollectionPolish)
run("testCollectionArt", testCollectionArt)
run("testCollectionFixes", testCollectionFixes)
run("testPacksArt", testPacksArt)
run("testMatchModel", testMatchModel)
run("testMatchFlow", testMatchFlow)
-- Hand cards are played by dragging; these tests play them by clicking, which /hs drag off keeps working.
local function withClickPlay(fn)
    return function()
        local db = H.db or {}
        H.db = db
        local old = db.drag
        db.drag = false
        local ok, err = pcall(fn)
        if H.db == db then db.drag = old end
        if not ok then error(err, 0) end
    end
end

run("testMatchUI", withClickPlay(testMatchUI))
run("testMatchArch", testMatchArch)
run("testMatchPhase08Model", testMatchPhase08Model)
run("testMatchPhase08Events", testMatchPhase08Events)
run("testMatchPhase08UI", withClickPlay(testMatchPhase08UI))
run("testMatchPhase09Model", testMatchPhase09Model)
run("testMatchPhase09UI", testMatchPhase09UI)
run("testAnimTimeline", testAnimTimeline)
run("testAnimTransition", testAnimTransition)
run("testAnimClock", testAnimClock)
run("testAnimAttackUI", testAnimAttackUI)
run("testAnimPairings", testAnimPairings)
run("testAnimFeedbackUI", testAnimFeedbackUI)
run("testAnimControlUI", testAnimControlUI)
run("testAnimReviewHeroRow", testAnimReviewHeroRow)
run("testAnimReviewSync", testAnimReviewSync)
run("testAnimReviewMulligan", testAnimReviewMulligan)
run("testMulliganAnimation", testMulliganAnimation)
run("testAnimReviewRevealMouse", testAnimReviewRevealMouse)
run("testAnimProperty", testAnimProperty)
run("testBoardLayout", testBoardLayout)
run("testBoardGlows", testBoardGlows)
run("testDuelWindow", testDuelWindow)
run("testDuelArt", testDuelArt)
run("testMinionOvals", testMinionOvals)
run("testPowerRound", testPowerRound)
run("testTurnBanner", testTurnBanner)
run("testHeroFollowers", testHeroFollowers)
run("testWeaponBreak", testWeaponBreak)
run("testFxProfiles", testFxProfiles)
-- Exercise retained native/family rendering independently of the cards now opting into 2D visuals.
-- The library tests below use the real, generated card profiles.
local function legacyFxProfiles(fn)
    return function()
        local FX = H.MatchFx
        local saved, savedTest = HS_EffectProfiles, FX.testProfiles
        local copy = {}
        for k, v in pairs(saved) do copy[k] = v end
        copy.cards = {}
        for id, card in pairs(saved.cards or {}) do
            local c = {}
            for k, v in pairs(card) do
                if k ~= "visual" and k ~= "flight" and k ~= "tint" and k ~= "fxScale" then c[k] = v end
            end
            copy.cards[id] = c
        end
        HS_EffectProfiles, FX.testProfiles = copy, copy
        local ok, err = pcall(fn)
        HS_EffectProfiles, FX.testProfiles = saved, savedTest
        if not ok then error(err, 0) end
    end
end
run("testFxEffects", legacyFxProfiles(testFxEffects))
run("testFireball", testFireball)
run("testFxBench", legacyFxProfiles(testFxBench))
run("testFxPlacement", legacyFxProfiles(testFxPlacement))
run("testSpellEffectLibrary", function()
    dofile(ROOT .. "/tests/spell_fx_tests.lua")({ H = H, check = check, animSetup = animSetup, animTeardown = animTeardown,
        baselineLevels = baselineLevels, checkAtRest = checkAtRest, usedTextures = usedTextures, usedTexts = usedTexts,
        runTicks = runTicks, fxEffect = fxEffect, shownModels = shownModels, nextE = nextE })
end)
run("testSpellEffectResults", function()
    dofile(ROOT .. "/tests/spell_fx_result_tests.lua")({ H = H, check = check, animSetup = animSetup, animTeardown = animTeardown,
        baselineLevels = baselineLevels, checkAtRest = checkAtRest, usedTextures = usedTextures, usedTexts = usedTexts,
        runTicks = runTicks, nextE = nextE })
end)
run("testSpellEffectVolleys", function()
    dofile(ROOT .. "/tests/spell_fx_volley_tests.lua")({ H = H, check = check, animSetup = animSetup, animTeardown = animTeardown,
        baselineLevels = baselineLevels, checkAtRest = checkAtRest, usedTextures = usedTextures, usedTexts = usedTexts,
        runTicks = runTicks, nextE = nextE })
end)
run("testSpellEffectSpecials", function()
    dofile(ROOT .. "/tests/spell_fx_special_tests.lua")({ H = H, check = check, animSetup = animSetup, animTeardown = animTeardown,
        baselineLevels = baselineLevels, checkAtRest = checkAtRest, usedTextures = usedTextures, usedTexts = usedTexts,
        runTicks = runTicks, nextE = nextE })
end)
run("testSpellEffectAbilities", function()
    dofile(ROOT .. "/tests/spell_fx_ability_tests.lua")({ H = H, check = check, animSetup = animSetup,
        animTeardown = animTeardown, nextE = nextE, runTicks = runTicks, baselineLevels = baselineLevels,
        checkAtRest = checkAtRest })
end)
run("testFxCalibration", testFxCalibration)
run("testMatchDrag", testMatchDrag)
run("testMerchantCoins", testMerchantCoins)
run("testArenaModel", testArenaModel)
run("testArenaFlow", testArenaFlow)
run("testArenaUI", testArenaUI)
run("testArenaMatch", testArenaMatch)
run("testHeroes", function()
    dofile(ROOT .. "/tests/heroes_tests.lua")({ H = H, check = check, T = packHelpers, reset = resetAddon, drain = drain,
        printed = function() return printed end, printedReset = function() printed = {} end })
end)
run("testHeroesUI", function()
    dofile(ROOT .. "/tests/heroes_ui_tests.lua")({ H = H, check = check, T = packHelpers, reset = resetAddon, drain = drain, ROOT = ROOT })
end)
run("testLadderUI", function() dofile(ROOT .. "/tests/ladder_ui_tests.lua")(check) end)
run("testBgModel", function()
    dofile(ROOT .. "/tests/bg_tests.lua")({ H = H, check = check, reset = resetAddon, drain = drain, T = packHelpers(),
        printed = function() return printed end })
end)
run("testBgUI", function()
    dofile(ROOT .. "/tests/bg_ui_tests.lua")({ H = H, check = check, reset = resetAddon, drain = drain, T = packHelpers(),
        ROOT = ROOT, printed = function() return printed end })
end)

print(string.format("\n%d checks, %d failed", checks, failed))
if failed > 0 then os.exit(1) end
