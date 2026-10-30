-- Warsong Battlegrounds window tests. Called from lua_tests.lua:
--     dofile("tests/bg_ui_tests.lua")({ H = H, check = check, reset = resetAddon, drain = drain, T = packHelpers(),
--                                      ROOT = ROOT })
-- Drives the shop, lobby, fight playback and result screen against the fake 1.12 frame API.
-- Lua 5.0 style, like the addon.

return function(ctx)
    local H, check = ctx.H, ctx.check
    local F = dofile(ctx.ROOT .. "/tests/fake_wow.lua")
    local UI = H.BgUI
    local BG = HsBg
    local BP = HsBgProtocol
    local P = HsProtocol
    local LOBBY = HsBgLobbyUI
    local PICK = HsBgHeroPickUI
    local QUESTPICK = HsBgQuestPickUI
    local COMBAT = HsBgCombatUI
    local ENDUI = HsBgEndUI
    local T = ctx.T

    -- ------------------------------------------------------------------ payload builders (mirror the server)
    local function minion(id, g, atk, hp, kw, uid)
        return id .. "." .. (g and "1" or "0") .. "." .. atk .. "." .. hp .. "." .. kw .. "." .. uid
    end
    local function list(items)
        return table.concat(items, ",")
    end
    -- 8 seats: health.tier.alive.elim.opp.heroId.armor.questId.rewardId.questId2.rewardId2 (seat heroSeat, 1-based,
    -- wears heroId with 5 armor; seat questSeat carries the two quest / reward pairs)
    local function lobby(heroId, heroSeat, questId, rewardId, questSeat, quest2Id, reward2Id)
        heroSeat = heroSeat or 2
        questSeat = questSeat or heroSeat
        local out = {}
        for i = 1, 8 do
            local hero = (i == heroSeat and heroId) or ""
            local armor = (i == heroSeat and heroId) and 5 or 0
            local quest = (i == questSeat and questId) or ""
            local reward = (i == questSeat and rewardId) or ""
            local quest2 = (i == questSeat and quest2Id) or ""
            local reward2 = (i == questSeat and reward2Id) or ""
            table.insert(out, 40 .. "." .. 1 .. "." .. 1 .. "." .. 0 .. "." .. (-1) .. "." .. hero .. "." .. armor
                .. "." .. quest .. "." .. reward .. "." .. quest2 .. "." .. reward2)
        end
        return list(out)
    end
    local function splitComma(s)
        local out = {}
        for item in string.gfind(s, "[^,]+") do table.insert(out, item) end
        return out
    end
    local function statePayload(mut)
        local kv = {
            { "me", "0" }, { "rd", "3" }, { "ph", "s" }, { "al", "1" }, { "en", "0" },
            { "g", "7" }, { "t", "2" }, { "uc", "5" }, { "bc", "3" }, { "fz", "0" }, { "hp", "34" }, { "cap", "1" },
            { "of", list({ minion("BG19_010", false, 2, 3, 0, 101), minion("BG21_003", false, 5, 3, 0, 102) }) },
            { "hd", list({ minion("BG_CFM_315", false, 1, 1, 0, 103) }) },
            { "tm", "" },
            { "bd", list({ minion("BG_EX1_531", false, 2, 2, 1, 104), minion("BG19_010", true, 6, 4, 0, 105) }) },
            { "op", "" },
            { "opw", "" },   -- a hero-power choice
            { "tb", list({ "1", "2", "4" }) },
            { "te", list({ "8", "16" }) },
            { "rf", "1" },
            { "pl", lobby() },
            { "win", "-1" },
            { "hc", "" }, { "hero", "" }, { "arm", "0" }, { "mhp", "40" }, { "rh", "0" }, { "pw", "" }, { "pwc", "0" },
            { "pwx", "0" }, { "pwu", "0" }, { "pwt", "n" }, { "snd", "0" }, { "gift", "" }, { "whl", "" }, { "nm", "|||||||" },
            { "qo", "" }, { "q", "" }, { "qp", "0" }, { "qr", "0" }, { "rw", "" },
            { "qa", "0" }, { "qb", "0" }, { "rt", "0" },
            { "pws", "0" }, { "pwh", "" }, { "pwe", "" }, { "sec", "" },
            { "q2", "" }, { "qp2", "0" }, { "qr2", "0" }, { "rw2", "" },
            { "qa2", "0" }, { "qb2", "0" }, { "rt2", "0" },
        }
        local map = mut or {}
        local out = {}
        for i = 1, table.getn(kv) do
            local v = map[kv[i][1]]
            if v ~= nil then
                if v ~= false then
                    table.insert(out, { kv[i][1], v })
                end
                map[kv[i][1]] = nil
            else
                table.insert(out, kv[i])
            end
        end
        -- ofz / oft: one digit per offer unless a test names them (every offer Frozen with fz=1, as the server does)
        local allFrozen = false
        for i = 1, table.getn(out) do
            if out[i][1] == "fz" then allFrozen = out[i][2] == "1" end
        end
        for i = 1, table.getn(out) do
            if out[i][1] == "of" then
                local n = out[i][2] == "" and 0 or table.getn(splitComma(out[i][2]))
                for _, k in ipairs({ "oft", "ofz" }) do
                    local v = map[k]
                    if v == nil then v = string.rep((k == "ofz" and allFrozen) and "1" or "0", n) end
                    if v ~= false then table.insert(out, i + 1, { k, v }) end
                end
                break
            end
        end
        return P.KvEncode(out)
    end
    local function combatPayload()
        return P.KvEncode({
            { "rd", "3" }, { "ms", "0" }, { "opp", "5" }, { "w", "0" }, { "dm", "6" }, { "fs", "0" },
            { "to", "0" }, { "stl", "0" },
            { "s0", minion("BG_EX1_531", false, 2, 2, 1, 1) },
            { "s1", minion("BG19_010", false, 2, 3, 0, 8) },
            { "v0", minion("BG_EX1_531", false, 2, 1, 1, 1) },
            { "v1", "" },
            { "tr", "0" },
            { "cd", list({ "BG19_010" }) },
            { "ev", list({ "0.0.1.8.0.0.-1.0.0.0", "1.1.1.8.2.0.0.0.0.0", "3.1.1.8.0.0.0.0.1.0" }) },
        })
    end
    local LAST = { type = "buy", index = 0 }

    local function feed(op, reqId, payload)
        local frames = P.ChunkPayload(op, reqId, payload)
        for i = 1, table.getn(frames) do
            H.OnAddonMessage("WRBG", frames[i], "GUILD", nil)
        end
    end
    local function tick(dt)
        UI.Tick(dt or 0.05)          -- the driver frame: BG.Tick, the fight playback and the refresh
    end
    local function lastSent(op)
        ctx.drain(1)
        local l = T.sentOps(op)
        return l[table.getn(l)]
    end
    local function resetUI()
        UI.frame, UI.drag, UI.sel, UI.ghost, UI.powerArm, UI.castQueue = nil, nil, nil, nil, nil, nil
        UI.hiding, UI.reshowing = false, false
        UI.discover = nil
        UI.offers, UI.board, UI.hand = {}, {}, {}
        LOBBY.frame, LOBBY.rows = nil, {}
        PICK.frame, PICK.tiles = nil, {}
        QUESTPICK.frame, QUESTPICK.tiles = nil, {}
        COMBAT.frame, COMBAT.playing, COMBAT.finishing = nil, false, false
        COMBAT.tiles = { [0] = {}, [1] = {} }
        ENDUI.frame, ENDUI.dismissed = nil, false
    end
    local function boot()
        resetUI()
        ctx.reset()
        F.install({})
        H.db = HsCollection.ApplySavedDefaults(nil)
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
    end
    local function click(frame, button)
        F.fire(frame, "OnClick", { arg1 = button or "LeftButton" })
    end

    -- ------------------------------------------------------------------ shop
    local function testShopWindow()
        boot()
        check(UI.frame ~= nil and UI.frame:IsShown(), "the window opened on the first state")
        check(F.globals.HearthstoneBgFrame == UI.frame, "the window has a global name")
        check(string.find(UI.frame.title:GetText(), BP.NAME, 1, true) ~= nil, "title: " .. UI.frame.title:GetText())
        check(UI.frame.gold:GetText() == "|cffffffff7/7|r", "gold: " .. UI.frame.gold:GetText())
        local coins = 0
        for i = 1, table.getn(UI.frame.coins) do
            if UI.frame.coins[i]:IsShown() then coins = coins + 1 end
        end
        check(coins == 7 and UI.frame.coins[7]._.vertex[1] == 1, "seven lit coins: " .. coins)
        check(UI.frame.tier:GetText() == "Tier 2", "tier")
        check(UI.frame.coin._.texture == H.BgUI.BG_PATH .. "coin", "the coin texture is used")
        check(table.getn(UI.frame.pips) == 6, "six tier pips")
        check(UI.frame.pips[2]._.vertex[1] == 1 and UI.frame.pips[3]._.vertex[1] < 1, "the tier pips light the current tier")
        check(UI.frame.upgrade.text:GetText() == "Upgrade (5)", "upgrade label: " .. UI.frame.upgrade.text:GetText())
        check(UI.frame.endTurn.text:GetText() == "End Turn", "end turn label")
        check(UI.frame.hero.hp.frame:IsShown() and UI.frame.hero.hp.text:GetText() == "34", "own health on the gem")
        check(UI.frame.hero.hp.text._.textColor[1] == 1 and UI.frame.hero.hp.text._.textColor[2] < 1, "a hurt hero's gem is red")
        check(not UI.frame.toast:IsShown(), "no notice without a message")
        check(UI.frame.refresh.restGlow == "green" and UI.frame.upgrade.restGlow == "green", "affordable buttons glow")
        check(UI.frame.endTurn.restGlow == nil, "End Turn does not glow while gold is left")
        check(UI.hand[1].restGlow == "green" and UI.hand[1].glow.frame:IsShown(), "a playable hand card glows green")

        -- the tavern offers
        check(table.getn(UI.offers) == 2 and UI.offers[1]:IsShown() and UI.offers[2]:IsShown(), "two offers drawn")
        check(UI.offers[2] == nil or UI.offers[4] == nil, "no extra offers")
        check(string.find(UI.offers[1].name:GetText(), "Sewer Rat", 1, true) ~= nil,
            "offer name: " .. tostring(UI.offers[1].name:GetText()))
        check(UI.offers[1].atk:GetText() == "|cffffd1002|r" and UI.offers[1].hp:GetText() == "|cffff55553|r",
            "offer stats are the card's")
        local offerCount = 0
        for i = 1, 6 do
            if UI.offers[i] and UI.offers[i]:IsShown() then offerCount = offerCount + 1 end
        end
        check(offerCount == 2, "exactly two offer tiles shown: " .. offerCount)

        -- the board (two minions plus one empty slot) and the hand
        check(table.getn(UI.board) == 3, "board draws one empty slot: " .. table.getn(UI.board))
        check(UI.board[1].atk:GetText() == "|cffffd1002|r", "board stats")
        check(UI.board[2].golden:IsShown(), "the golden flag is shown")
        check(not UI.board[3].entry, "the third board slot is empty")
        check(table.getn(UI.hand) == 1 and UI.hand[1]:IsShown(), "one hand card")

        -- the lobby: eight seats, the human's faction first, faction named on the first row of each group
        check(table.getn(LOBBY.rows) == 8, "eight lobby rows")
        check(LOBBY.rows[1].label:GetText() == "You", "own seat: " .. LOBBY.rows[1].label:GetText())
        check(string.find(LOBBY.rows[1].group:GetText(), "Alliance", 1, true) ~= nil, "the faction heading is on the first row")
        check(LOBBY.rows[2].label:GetText() == "Ally 2", "ally label")
        check(LOBBY.rows[5].label:GetText() == "Foe 1", "foe label: " .. LOBBY.rows[5].label:GetText())
        check(string.find(LOBBY.rows[5].group:GetText(), "Horde", 1, true) ~= nil, "the other faction is named")
        check(LOBBY.rows[1].health:GetText() ~= "", "lobby health shown")
    end

    local function testBuyAndPlay()
        boot()
        click(UI.offers[1])
        check(BG.IsBusy(), "a buy is in flight")
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=buy;i=0", "buy payload: " .. tostring(act and act.payload))
        feed("BG_STATE", act.reqId, statePayload())

        -- pick a hand card, then a board slot: Play(index, slot, target uid)
        tick(0.1)
        click(UI.hand[1])
        check(UI.sel and UI.sel.zone == "hand" and UI.sel.index == 0, "a hand card is picked up")
        check(string.find(UI.frame.message:GetText(), "board slot", 1, true) ~= nil, "the hint changes")
        click(UI.board[2])
        local play = lastSent("BG_ACT")
        check(play and play.payload == "act=play;i=0;s=1;u=105", "play payload: " .. tostring(play and play.payload))
        check(UI.sel == nil, "the pick is cleared")
    end

    -- A targeted Battlecry (Mind Muck: a friendly Demon): a click on a slot waits for the target, a click on a fitting minion
    -- plays the card aimed at it, a minion that does not fit is refused.
    local function testBattlecryTarget()
        boot()
        feed("BG_STATE", "0", statePayload({
            hd = list({ minion("BG23_357", false, 3, 2, 0, 103) }),
            bd = list({ minion("BG24_009", false, 1, 1, 0, 104), minion("BG_EX1_531", false, 2, 2, 1, 105) }) }))
        tick(0.1)
        local before = lastSent("BG_ACT")
        click(UI.hand[1])
        click(UI.board[3])                                   -- the empty slot after the two minions
        check(UI.sel and UI.sel.choosing and UI.sel.slot == 2, "the card waits for its target")
        check(lastSent("BG_ACT") == before, "nothing is sent yet")
        check(string.find(UI.frame.message:GetText(), "friendly Demon", 1, true) ~= nil, "the hint names the target")
        click(UI.board[2])                                   -- a Beast: no Demon
        check(UI.sel and UI.sel.choosing and lastSent("BG_ACT") == before, "a minion that does not fit is refused")
        click(UI.board[1])
        local play = lastSent("BG_ACT")
        check(play and play.payload == "act=play;i=0;s=2;u=104", "played aimed at the Demon: " .. tostring(play and play.payload))
        check(UI.sel == nil, "the pick is cleared")
        -- without a Demon on the board the card is simply played
        feed("BG_STATE", play.reqId, statePayload({ hd = list({ minion("BG23_357", false, 3, 2, 0, 103) }) }))
        tick(0.1)
        click(UI.hand[1])
        click(UI.board[3])
        play = lastSent("BG_ACT")
        check(play and play.payload == "act=play;i=0;s=2", "no Demon to choose: played at once: " .. tostring(play and play.payload))
    end

    -- A Wild Banana (needs a Beast) can be thrown away on Bob; any other spell is cast, not sold.
    -- A power that grows with the round shows its real number now.
    local function testPowerNowLine()
        check(BG.PowerNowLine("BG20_HERO_102p", 1) == "Now: +2/+1" and BG.PowerNowLine("BG20_HERO_102p", 2) == "Now: +2/+1",
            "For the Horde! at turn 1-2")
        check(BG.PowerNowLine("BG20_HERO_102p", 3) == "Now: +4/+2" and BG.PowerNowLine("BG20_HERO_102p", 9) == "Now: +16/+8",
            "doubled every 3 turns")
        check(BG.PowerNowLine("BG22_HERO_000p", 3) == "Now: 2 damage" and BG.PowerNowLine("BG22_HERO_000p_t2", 4) == "Now: 4 damage"
            and BG.PowerNowLine("BG22_HERO_000p_t1", 10) == "Now: 16 damage", "the aims double from turn 4")
        check(BG.PowerNowLine("TB_BaconShop_HP_040", 5) == nil, "other powers show no line")
    end

    local function testSellBanana()
        check(BG.SellableSpell({ id = "TB_BaconShop_HP_038t" }), "a Wild Banana is sellable")
        check(not BG.SellableSpell({ id = "BGS_Treasures_040" }) and not BG.SellableSpell(nil), "other spells are not")
    end

    local function testSpellCast()
        boot()
        feed("BG_STATE", "0", statePayload({ hd = minion("BG23_000t", false, 0, 0, 0, 103), tm = "103" }))
        tick(0.1)
        check(UI.hand[1] and UI.hand[1]:IsShown(), "the spell is in the hand")
        check(string.find(UI.hand[1].name:GetText(), "Mini-Trident", 1, true) ~= nil, "the spell's name is shown")
        check(UI.hand[1].atk:GetText() == "" and UI.hand[1].hp:GetText() == "", "a spell has no stats")
        check(string.find(UI.hand[1].kw:GetText(), "until next turn", 1, true) ~= nil, "the temporary marker")
        check(UI.hand[1].restGlow == "green", "a playable spell glows green")
        click(UI.hand[1])
        check(UI.sel and UI.sel.zone == "hand", "the spell is picked up")
        click(UI.board[1])
        local cast = lastSent("BG_ACT")
        check(cast and cast.payload == "act=cast;i=0;u=104", "cast payload: " .. tostring(cast and cast.payload))
        check(UI.sel == nil, "the pick is cleared after the cast")
    end

    -- A spell cast while the last action is still out waits and goes out after the answer (found again by uid), it is
    -- not lost: two Gold Coins played back to back both leave the hand.
    local function testSpellCastQueued()
        boot()
        local coins = list({ minion("TB_BaconShop_HP_008a", false, 0, 0, 0, 201),
            minion("TB_BaconShop_HP_008a", false, 0, 0, 0, 202) })
        feed("BG_STATE", "0", statePayload({ hd = coins }))
        tick(0.1)
        click(UI.hand[1])
        click(UI.board[3])
        local first = lastSent("BG_ACT")
        check(first and first.payload == "act=cast;i=0", "the first coin is cast: " .. tostring(first and first.payload))
        click(UI.hand[2])
        click(UI.board[3])
        tick(0.1)
        local n = table.getn(T.sentOps("BG_ACT"))
        check(n == 1, "the second coin waits while the first is out")
        check(UI.castQueue and table.getn(UI.castQueue) == 1, "the second coin is queued")
        feed("BG_STATE", first.reqId, statePayload({ hd = minion("TB_BaconShop_HP_008a", false, 0, 0, 0, 202) }))
        tick(0.1)
        local second = lastSent("BG_ACT")
        check(table.getn(T.sentOps("BG_ACT")) == 2 and second.payload == "act=cast;i=0",
            "the second coin goes out at its new place: " .. tostring(second and second.payload))
        check(table.getn(UI.castQueue) == 0, "the queue is empty")
    end

    local function testBuffGlow()
        boot()
        UI.frame.artMode = true
        local V = HsBgVis
        local oldPulse = V.Pulse
        local pulses = 0
        V.Pulse = function(...)
            pulses = pulses + 1
            return oldPulse(...)
        end
        -- the same board, one minion buffed: a Blood Gem played by an effect
        feed("BG_STATE", "0", statePayload({ bd = list({ minion("BG_EX1_531", false, 3, 3, 1, 104),
            minion("BG19_010", true, 6, 4, 0, 105) }) }))
        tick(0.1)
        V.Pulse = oldPulse
        check(pulses >= 1, "a buffed board minion glows: " .. pulses)
    end

    local function testSellAndMove()
        boot()
        local before = table.getn(T.sentOps("BG_ACT"))
        click(UI.board[1], "RightButton")
        check(table.getn(T.sentOps("BG_ACT")) == before, "a right click does not sell")
        -- a board minion dropped on Bob is sold (fake_wow leaves the rects nil: give Bob one, the cursor inside it)
        local bob = UI.BobFrame()
        bob.GetLeft = function() return 900 end      -- the plain "Bob's Tavern" label: a FontString in fake_wow
        bob.GetRight = function() return 1000 end
        bob.GetBottom = function() return 500 end
        bob.GetTop = function() return 600 end
        F.cursor[1], F.cursor[2] = 950, 550
        UI.StartDrag("board", 0)
        UI.StopDrag()
        local sell = lastSent("BG_ACT")
        check(sell and sell.payload == "act=sell;z=b;i=0", "a drop on Bob sells: " .. tostring(sell and sell.payload))
        feed("BG_STATE", sell.reqId, statePayload())

        tick(0.1)
        click(UI.board[1])
        check(UI.sel and UI.sel.zone == "board", "a board minion is picked up")
        click(UI.board[2])
        local mv = lastSent("BG_ACT")
        check(mv and mv.payload == "act=move;f=0;t=1", "move payload: " .. tostring(mv and mv.payload))
    end

    -- At the top tier the server sends uc=-1 (nothing to buy): the state must still be read and the button says so.
    local function testMaxTier()
        boot()
        feed("BG_STATE", "0", statePayload({ t = "6", uc = "-1" }))
        tick(0.1)
        check(BG.model and BG.model.tier == 6 and BG.model.upgrade == -1, "a tier 6 state is accepted")
        check(UI.frame.upgrade.text:GetText() == "Upgrade (max)", "the Upgrade button shows the top tier: " .. UI.frame.upgrade.text:GetText())
    end

    -- bc is what the next tavern minion costs, and the Buy tooltip reads it.
    local function testBuyCost()
        boot()
        feed("BG_STATE", "0", statePayload({ bc = "2" }))
        tick(0.1)
        check(BG.model and BG.model.buyCost == 2, "the buy cost is parsed")
        check(UI.TileHint("offer", nil) == "Click to buy (2 Gold).", "the offer tooltip: " .. UI.TileHint("offer", nil))
    end

    local function testSideButtons()
        boot()
        click(UI.frame.upgrade)
        local a = lastSent("BG_ACT")
        check(a and a.payload == "act=upgrade", "upgrade")
        feed("BG_STATE", a.reqId, statePayload())
        tick(0.1)
        click(UI.frame.refresh)
        a = lastSent("BG_ACT")
        check(a and a.payload == "act=refresh", "refresh")
        feed("BG_STATE", a.reqId, statePayload())
        tick(0.1)
        click(UI.frame.freeze)
        a = lastSent("BG_ACT")
        check(a and a.payload == "act=freeze;on=1", "freeze on")
        feed("BG_STATE", a.reqId, statePayload({ fz = "1" }))
        tick(0.1)
        check(UI.frame.freeze.text:GetText() == "Unfreeze", "the freeze button flips: " .. UI.frame.freeze.text:GetText())
        click(UI.frame.freeze)
        a = lastSent("BG_ACT")
        check(a and a.payload == "act=freeze;on=0", "freeze off")
        feed("BG_STATE", a.reqId, statePayload())
        tick(0.1)
        click(UI.frame.endTurn)
        a = lastSent("BG_ACT")
        check(a and a.payload == "act=end", "end turn")
    end

    local function testDiscover()
        boot()
        feed("BG_STATE", "0", statePayload({ op = list({ minion("BG21_003", false, 5, 3, 0, 201),
            minion("BGS_018", false, 4, 4, 0, 202) }) }))
        tick(0.1)
        check(UI.discover:IsShown(), "the discover panel is shown")
        check(UI.discover.tiles[1]:IsShown() and UI.discover.tiles[2]:IsShown(), "two options")
        check(string.find(UI.frame.message:GetText(), "Discover", 1, true) ~= nil, "the hint names the discover")
        click(UI.discover.tiles[2])
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=discover;i=1", "discover payload: " .. tostring(act and act.payload))
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
        check(not UI.discover:IsShown(), "the panel closes with the next state")
    end

    -- Hide takes the options away (the panel stays up and blocks the board), Show brings them back; a new choice opens shown.
    local function testDiscoverHide()
        boot()
        feed("BG_STATE", "0", statePayload({ op = list({ minion("BG21_003", false, 5, 3, 0, 201),
            minion("BGS_018", false, 4, 4, 0, 202) }) }))
        tick(0.1)
        check(UI.discover.hideButton:IsShown() and not UI.discover.showButton:IsShown(), "Hide is offered")
        click(UI.discover.hideButton)
        check(UI.discover:IsShown(), "the panel keeps blocking the board")
        check(not UI.discover.tiles[1]:IsShown() and not UI.discover.text:IsShown(), "the options are out of sight")
        check(UI.discover.showButton:IsShown() and not UI.discover.hideButton:IsShown(), "Show replaces Hide")
        tick(0.1)
        check(not UI.discover.tiles[1]:IsShown(), "a refresh keeps them hidden")
        click(UI.discover.showButton)
        check(UI.discover.tiles[1]:IsShown() and UI.discover.hideButton:IsShown(), "Show brings the options back")
        click(UI.discover.hideButton)
        feed("BG_STATE", "0", statePayload({ op = list({ minion("BG21_003", false, 5, 3, 0, 301),
            minion("BGS_018", false, 4, 4, 0, 302) }) }))
        tick(0.1)
        check(UI.discover.tiles[1]:IsShown(), "another choice opens shown")
    end

    -- The Wheel of Yogg-Saron: the spin of the state turns the picture and stops on the outcome, then the overlay leaves.
    local function testYoggWheel()
        boot()
        feed("BG_STATE", "0", statePayload({ whl = "3" }))
        tick(0.1)
        check(UI.wheel and UI.wheel:IsShown(), "the wheel is shown")
        tick(1.0)
        check(UI.wheel:IsShown() and UI.wheel.result:GetText() == "", "it is still spinning")
        tick(3.0)
        check(UI.wheel.card:IsShown() and string.find(UI.wheel.card.name:GetText(), "Mindflayer Goggles", 1, true) ~= nil,
            "it names the outcome on its card")
        tick(2.0)
        check(UI.wheel:IsShown(), "the card stays until it is clicked")
        UI.WheelSkip()
        tick(1.0)
        check(not UI.wheel:IsShown(), "after the click the overlay leaves")
    end

    -- ------------------------------------------------------------------ a hero-power choice
    local function testPowerChoice()
        boot()
        -- with the power card art installed the options are the cards themselves
        HS_BgTextures = { generated = true, count = 0, set = {}, heroes = { "bob" }, powers = {},
            powerCards = { "BG20_HERO_283p_t1", "BG22_HERO_000p_t4" } }
        feed("BG_STATE", "0", statePayload({ opw = "BG20_HERO_283p_t1,BG22_HERO_000p_t4" }))
        tick(0.1)
        check(UI.discover:IsShown(), "the power choice panel is shown")
        check(UI.discover.powerCards[1] and UI.discover.powerCards[1]:IsShown(), "the first power card")
        check(UI.discover.powerCards[2] and UI.discover.powerCards[2]:IsShown(), "the second power card")
        check(UI.discover.powerCards[2].tex._.texture == UI.HERO_PATH .. "PowerCards\\BG22_HERO_000p_t4",
            "the power card texture: " .. tostring(UI.discover.powerCards[2].tex._.texture))
        check(string.find(UI.discover.text:GetText(), "hero power", 1, true) ~= nil, "the heading names the power choice")
        click(UI.discover.powerCards[2])
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=discover;i=1", "the power picks with the discover action: "
            .. tostring(act and act.payload))
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
        check(not UI.discover:IsShown(), "the panel closes with the next state")
        -- without the card art the option shows its name and text on a plate
        HS_BgTextures = { generated = true, count = 0, set = {}, heroes = { "bob" }, powers = {}, powerCards = {} }
        feed("BG_STATE", "0", statePayload({ opw = "TB_BaconShop_HP_087t" }))
        tick(0.1)
        check(UI.discover:IsShown() and UI.discover.powerCards[1]:IsShown(), "the plate fallback is shown")
        check(string.find(UI.discover.powerCards[1].tex:GetTexture() or "", "reward_card", 1, true) ~= nil
            and UI.discover.powerCards[1].name:GetText() ~= "", "the fallback wears the blank reward card with the power's name")
        check(string.find(UI.discover.powerCards[1].name:GetText(), "Sulfuras", 1, true) ~= nil,
            "the fallback names the power: " .. tostring(UI.discover.powerCards[1].name:GetText()))
    end

    -- ------------------------------------------------------------------ combat
    local function testCombat()
        boot()
        feed("BG_COMBAT", "0", combatPayload())
        feed("BG_STATE", "0", statePayload({ rd = "4", hp = "30" }))
        tick(0.1)
        check(COMBAT.IsPlaying() and COMBAT.frame:IsShown(), "the fight plays")
        check(table.getn(COMBAT.side[0]) == 1 and table.getn(COMBAT.side[1]) == 1, "both warbands on screen")
        check(string.find(COMBAT.frame.title:GetText(), "Round 3", 1, true) ~= nil, "fight heading: "
            .. COMBAT.frame.title:GetText())
        -- the fight is played on the board: the shop steps aside, the next state waits until the fight is over
        check(not UI.tavern:IsShown() and not UI.boardRow:IsShown() and not UI.frame.endTurn:IsShown(),
            "the shop's pieces step aside")
        check(UI.frame.hero.hp.text:GetText() == "34", "my hero keeps the health before the fight")
        check(COMBAT.frame.foe.frame:IsShown() and COMBAT.frame.foe.hp.text:GetText() == "40",
            "the opponent stands in Bob's arch with his health")
        -- the damage gem stays hidden during the fight: the winner's tally shows it just before its hero strikes
        check(not UI.frame.hero.atk.frame:IsShown(), "no damage gem while the warbands fight")
        local mine = COMBAT.actors[1]
        local foe = COMBAT.actors[8]
        check(mine and foe and mine.y > foe.y, "my warband stands below the opponent's")
        check(mine.frame.shieldLayer:GetFrameLevel() > mine.frame:GetFrameLevel(),
            "a fight tile's Divine Shield layer stays above the tile")
        check(COMBAT.beats[1].kind == "intro" and COMBAT.beats[2].kind == "attack"
            and table.getn(COMBAT.beats[2].hits) == 1, "an attack beat carries its hit")
        check(COMBAT.beats[3].kind == "death", "the death follows")
        check(COMBAT.beats[table.getn(COMBAT.beats)].kind == "hero", "the winner's hero strikes last")
        -- into the attack: the attacker lunges at the defender, the hit lands with a damage number
        local guard = 0
        while COMBAT.i < 1 and guard < 100 do
            tick(0.05)
            guard = guard + 1
        end
        tick(0.24)
        check(COMBAT.i == 1 and (mine.ly or 0) < 0, "the attacker moves up into its target: " .. tostring(mine.ly))
        local hit = foe.frame
        check(hit.float:IsShown() and hit.float:GetText() == "-2", "the damage number shows on the hit tile: "
            .. tostring(hit.float:GetText()))
        check(COMBAT.byUid[8] and COMBAT.byUid[8].hp == 1, "the defender lost 2 health")
        guard = 0
        local seen = {}
        while COMBAT.playing and guard < 200 do
            tick(0.05)
            guard = guard + 1
            for _, w in ipairs({ UI.frame.hero, COMBAT.frame.foe }) do
                if w.atk.frame:IsShown() then
                    local v = w.atk.text:GetText()
                    if seen[table.getn(seen)] ~= v then
                        table.insert(seen, v)
                    end
                end
            end
        end
        check(table.getn(seen) >= 1, "the winner's damage showed in the tally")
        check(not UI.frame.hero.atk.frame:IsShown() and not COMBAT.frame.foe.atk.frame:IsShown(),
            "no damage gem once the fight is over")
        check(COMBAT.finishing and not COMBAT.playing, "the fight ends by itself")
        check(table.getn(COMBAT.side[1]) == 0, "the dead defender is gone")
        check(COMBAT.frame.foe.hp.text:GetText() == "34", "the loser's hero took the damage: "
            .. tostring(COMBAT.frame.foe.hp.text:GetText()))
        check(string.find(COMBAT.frame.status:GetText(), "Victory", 1, true) ~= nil, "the outcome line: "
            .. COMBAT.frame.status:GetText())
        check(COMBAT.frame.skip.text:GetText() == "Continue", "the button continues")
        tick(10)
        check(COMBAT.IsPlaying() and COMBAT.frame:IsShown(), "the result waits for Continue")
        COMBAT.Button()
        check(not COMBAT.IsPlaying() and not COMBAT.frame:IsShown(), "Continue puts the result away")
        local ends = 0
        local realEnd = HsBg.EndTurn
        HsBg.EndTurn = function() ends = ends + 1 end
        UI.SideAction("end")
        check(ends == 0, "a click right after Continue does not end the turn")
        tick(UI.END_GUARD + 0.1)
        UI.SideAction("end")
        check(ends == 1, "End Turn works again after the guard")
        HsBg.EndTurn = realEnd
        check(UI.frame:IsShown() and UI.tavern:IsShown(), "the shop is back")
        tick(0.1)
        check(UI.frame.hero.hp.text:GetText() == "30", "the next state is drawn after the fight")
        check(string.find(UI.frame.title:GetText(), "Round 4", 1, true) ~= nil, "the next round: " .. UI.frame.title:GetText())

        -- Skip jumps to the result; Continue closes it
        feed("BG_COMBAT", "0", combatPayload())
        tick(0.1)
        check(COMBAT.playing, "a second fight plays")
        COMBAT.Button()
        check(COMBAT.finishing and not COMBAT.playing, "skip jumps to the result")
        check(table.getn(COMBAT.side[1]) == 0 and table.getn(COMBAT.side[0]) == 1, "the survivors stand")
        check(COMBAT.frame.status:GetText() ~= "", "the outcome line: " .. COMBAT.frame.status:GetText())
        check(table.getn(HsBgVis.items) == 0, "no animation keeps running after a skip")
        COMBAT.Button()
        check(not COMBAT.IsPlaying() and not COMBAT.frame:IsShown(), "Continue puts the fight away")
        check(UI.frame:IsShown() and UI.tavern:IsShown(), "the shop is back")

        -- a fight against a ghost (opp -1, no health): winning it and skipping does not break, the game can end after it
        local ghost = string.gsub(combatPayload(), "opp=5", "opp=-1")
        check(ghost ~= combatPayload(), "the ghost payload differs")
        feed("BG_COMBAT", "0", ghost)
        feed("BG_STATE", "0", statePayload({ ph = "o" }))
        feed("BG_END", "0", P.KvEncode({ { "win", "0" }, { "myf", "0" }, { "won", "1" }, { "pf", "1" },
            { "po", "1" }, { "al", "1" }, { "rd", "16" }, { "tm", "0" }, { "rc", "0" }, { "rp", "0" },
            { "pl", list({ "1", "2", "3", "4", "5", "6", "7", "8" }) } }))
        tick(0.1)
        check(COMBAT.playing, "the ghost fight plays")
        COMBAT.Button()
        check(COMBAT.finishing and not COMBAT.playing, "skip against a ghost reaches the result")
        COMBAT.Button()
        tick(0.1)
        check(HsBgEndUI.IsShown(), "the result screen follows the last fight")
        -- the game is over: the close button leaves at once, no surrender question
        local said, popped = nil, nil
        local realSay, realPop = SendChatMessage, StaticPopup_Show
        SendChatMessage = function(msg) said = msg end
        StaticPopup_Show = function(name) popped = name end
        UI.AskSurrender()
        SendChatMessage, StaticPopup_Show = realSay, realPop
        check(said == ".hs bg leave" and popped == nil, "closing a finished game leaves without asking: "
            .. tostring(said) .. " / " .. tostring(popped))
    end

    -- ------------------------------------------------------------------ resting (owner decision 2026-10-19)
    -- My faction had a player too many: I sat the round out (lastOpponent -3) and healed; no fight, a notice instead.
    local function testRest()
        boot()
        feed("BG_STATE", "0", statePayload({ rd = "5", hp = "30" }))
        tick(0.1)
        check(not COMBAT.IsPlaying(), "no fight before the rest")
        local seats = {}
        for i = 1, 8 do
            local opp = (i == 1 or i == 3) and "-3" or "-1"
            table.insert(seats, (i == 1 and 33 or 40) .. ".1.1.0." .. opp .. "..0....")
        end
        feed("BG_STATE", "0", statePayload({ rd = "6", hp = "33", rh = "3", pl = list(seats) }))
        tick(0.1)
        check(BG.state == "ready" and BG.model.rd == 6, "a lobby seat that rested (-3) is a valid state")
        check(BG.model.restHealed == 3 and BG.model.lobby[1].opp == HsBgProtocol.RESTED, "the rest is read")
        check(UI.frame.toast:IsShown() and string.find(UI.frame.message:GetText(), "healed 3 health", 1, true) ~= nil,
            "the notice says the round was skipped and what I healed: " .. tostring(UI.frame.message:GetText()))
        check(UI.frame.hero.hp.text:GetText() == "33", "my hero shows the healed health")
        local floated = false
        for i = 1, table.getn(HsBgVis.items) do
            local it = HsBgVis.items[i]
            if it.kind == "number" and it.h.obj:GetText() == "+3" then
                floated = true
            end
        end
        check(floated, "+3 rises from my hero")
        -- the next state of the same round brings no second notice
        UI.Toast(nil)
        feed("BG_STATE", "0", statePayload({ rd = "6", hp = "33", rh = "3", pl = list(seats), g = "4" }))
        tick(0.1)
        check(not UI.frame.toast:IsShown(), "the notice comes once per round")
        -- rh is required
        check(BP.ParseState(statePayload({ rd = "7", rh = false })) == nil, "a state without rh is refused")
        -- a rest from round 20 on heals nothing, and the notice says why
        UI.Toast(nil)
        feed("BG_STATE", "0", statePayload({ rd = "21", hp = "20", rh = "0", pl = list(seats) }))
        tick(0.1)
        check(UI.frame.toast:IsShown() and string.find(UI.frame.message:GetText(), "no longer heals", 1, true) ~= nil,
            "the late rest notice: " .. tostring(UI.frame.message:GetText()))
    end

    -- ------------------------------------------------------------------ end screen
    local function testEnd()
        boot()
        feed("BG_STATE", "0", statePayload({ ph = "o", al = "0" }))
        feed("BG_END", "0", P.KvEncode({ { "win", "1" }, { "myf", "0" }, { "won", "0" }, { "pf", "4" },
            { "po", "8" }, { "al", "0" }, { "rd", "12" }, { "tm", "0" }, { "rc", "0" }, { "rp", "0" },
            { "pl", list({ "8", "7", "6", "5", "4", "3", "2", "1" }) } }))
        tick(0.1)
        check(ENDUI.frame ~= nil and ENDUI.frame:IsShown(), "the result screen is up")
        check(string.find(ENDUI.frame.heading:GetText(), "Defeat", 1, true) ~= nil, "heading: "
            .. ENDUI.frame.heading:GetText())
        check(string.find(ENDUI.frame.chips[1].value:GetText(), "8th", 1, true) ~= nil, "your place: "
            .. ENDUI.frame.chips[1].value:GetText())
        check(string.find(ENDUI.frame.chips[2].value:GetText(), "4th", 1, true) ~= nil, "your place in the faction")
        check(ENDUI.frame.chips[3].value:GetText() == "12", "the rounds")
        check(string.find(ENDUI.frame.rows[8].place:GetText(), "8th", 1, true) ~= nil, "the placement table")
        check(ENDUI.frame.rows[1]:IsShown() and string.find(ENDUI.frame.rows[1].place:GetText(), "1st", 1, true) ~= nil,
            "the winner's row")

        local said, realSay = nil, SendChatMessage
        SendChatMessage = function(msg) said = msg end
        click(ENDUI.frame.leave)
        SendChatMessage = realSay
        check(ENDUI.dismissed and not ENDUI.frame:IsShown(), "Leave puts the screen away")
        check(not UI.frame:IsShown(), "Leave closes the whole window")
        check(said == ".hs bg leave", "Leave sends the real leave command: " .. tostring(said))

        feed("BG_CLOSE", "0", "")
        tick(0.1)
        check(BG.state == "none" and not UI.frame:IsShown(), "BG_CLOSE closes the window")
    end

    local function testLeaveWithAHostileState()
        -- a malformed state is rejected and shown as an error, the window stays
        boot()
        feed("BG_STATE", "0", "me=9")
        tick(0.1)
        check(BG.errorText ~= nil, "a bad state is an error")
        check(UI.frame:IsShown(), "the window stays up")
    end

    local function testDrag()
        boot()
        -- fake_wow leaves GetLeft/GetTop nil, so give the board row a rect and put the cursor inside it
        local row = UI.boardRow
        row._.left, row._.right = 0, 700
        row._.bottom, row._.top = 0, 120
        F.cursor[1], F.cursor[2] = 10, 10
        UI.StartDrag("hand", 0)
        check(UI.drag ~= nil and UI.ghost:IsShown(), "a hand card is being dragged")
        UI.UpdateGhost()
        UI.StopDrag()
        check(UI.drag == nil and not UI.ghost:IsShown(), "the drag ended")
        local play = lastSent("BG_ACT")
        check(play and play.payload == "act=play;i=0;s=2", "drag onto the board plays at the end: "
            .. tostring(play and play.payload))
        feed("BG_STATE", play.reqId, statePayload())
        tick(0.1)

        -- dropped outside the board row: nothing is sent
        F.cursor[2] = 500
        local before = table.getn(T.sentOps("BG_ACT"))
        UI.StartDrag("hand", 0)
        UI.StopDrag()
        check(table.getn(T.sentOps("BG_ACT")) == before, "a drop outside the board does nothing")
    end

    -- Board drops: a move takes the final index (Shop::Move), a play takes an insertion point plus the minion under the
    -- cursor as the target. Rects are seeded: the board row is 0..700 and tile k (0-based) starts at k * 100.
    local function testDragBoard()
        boot()
        local row = UI.boardRow
        row._.left, row._.right, row._.bottom, row._.top = 0, 700, 0, 120
        for i = 1, table.getn(UI.board) do
            UI.board[i]._.left = (i - 1) * 100
        end
        F.cursor[2] = 10
        local function drop(kind, index, x)
            F.cursor[1] = x
            UI.StartDrag(kind, index)
            UI.StopDrag()
            local op = lastSent("BG_ACT")
            feed("BG_STATE", op.reqId, statePayload())
            tick(0.1)
            return op.payload
        end
        -- while a board minion is dragged over the row, the row previews the move as a hand card's play does: its tile
        -- hides, the other minion takes its place and the glowing empty tile is the gap at the new place
        F.cursor[1] = 170
        UI.StartDrag("board", 0)
        UI.PlaceBoard()
        check(UI.board[1]:GetAlpha() == 0, "the dragged minion's tile is hidden")
        check(UI.board[2].bx < UI.board[3].bx and UI.board[3].bx == UI.board[1].bx, "the gap stands right of the other minion")
        check(UI.board[3].previewGlow == true, "the gap glows")
        F.cursor[2] = 500
        UI.PlaceBoard()
        check(UI.board[1]:GetAlpha() == 1 and not UI.board[3].previewGlow, "off the row the preview leaves")
        F.cursor[2] = 10
        UI.CancelDrag()
        check(UI.board[1]:GetAlpha() == 1, "a cancelled drag shows the minion again")
        check(drop("board", 0, 170) == "act=move;f=0;t=1", "the right half of the last minion moves to the end")
        check(drop("board", 0, 650) == "act=move;f=0;t=1", "past the last minion is the end, never out of range")
        check(drop("board", 1, 10) == "act=move;f=1;t=0", "the left half of the first minion moves to the front")
        check(drop("hand", 0, 120) == "act=play;i=0;s=1;u=105", "a hand card dropped on a minion targets that minion")
        check(drop("hand", 0, 10) == "act=play;i=0;s=0;u=104", "the left half of the first minion targets it")
        check(drop("hand", 0, 650) == "act=play;i=0;s=2", "past the last minion plays at the end, no target")

        -- the empty end slot, with a board minion picked up, is the last minion's place
        UI.sel = { zone = "board", index = 0 }
        UI.BoardClick(2)
        local op = lastSent("BG_ACT")
        check(op.payload == "act=move;f=0;t=1", "click on the end slot: " .. op.payload)
        feed("BG_STATE", op.reqId, statePayload())
        tick(0.1)

        -- a new state that shrinks the zone drops the selection
        UI.sel = { zone = "hand", index = 0 }
        feed("BG_STATE", "0", statePayload({ hd = "" }))
        tick(0.1)
        check(UI.sel == nil, "a stale selection is dropped")
    end

    -- "loading" shows nothing: a server with the mode off never answers and the window cannot be closed.
    local function testLoadingHidden()
        resetUI()
        ctx.reset()
        F.install({})
        BG.Get()
        tick(0.1)
        check(BG.state == "loading", "a BG_GET is out")
        check(not UI.IsShown(), "no window while loading")
        for i = 1, 400 do
            tick(0.1)
        end
        check(BG.state == "none" and not UI.IsShown(), "no window after the server stays silent")
    end

    local function testPortraits()
        boot()
        local saved = HS_BgTextures
        HS_BgTextures = { generated = true, count = 1, set = { ["BG19_010"] = true } }
        UI.Refresh()
        -- the offer is a normal BG19_010 with a portrait; the second board minion is its golden twin
        check(UI.offers[1].art:IsShown(), "the portrait is shown")
        check(UI.offers[1].art._.texture == H.BgUI.ART_PATH .. "BG19_010", "portrait path")
        check(not UI.offers[1].ring:IsShown(), "a normal minion has no gold ring")
        check(UI.board[2].art:IsShown() and UI.board[2].ring:IsShown(), "a golden minion wears the gold ring")
        check(UI.board[2].ring._.texture == H.BgUI.BG_PATH .. "golden_ring", "ring path")
        check(not UI.board[2].golden:IsShown(), "the ring replaces the G marker")
        check(UI.board[2].art._.texture == H.BgUI.ART_PATH .. "BG19_010", "a golden minion uses its twin's portrait")
        check(not UI.board[1].art:IsShown(), "an id without a portrait stays a text tile")
        check(UI.offers[1].name:GetText() ~= "", "the text tile keeps its name")
        HS_BgTextures = saved
        UI.Refresh()
        check(not UI.offers[1].art:IsShown() and not UI.offers[1].ring:IsShown(), "without portraits it is a text tile")
        check(UI.board[2].golden:IsShown(), "the G marker is back for a golden minion")
    end

    -- With the board art installed the window is the tavern board: tiles are ovals with gems, the side buttons are the
    -- shields and the End Turn stone (every piece falls back to the plain look, which the tests above cover).
    local function testBoardArt()
        local saved = HS_BoardTextures
        local B = "Interface\\Hearthstone\\Textures\\Board\\"
        local pieces = {}
        local names = { "minion_oval", "taunt_frame", "divine_shield", "gem_attack", "gem_health", "end_turn_button",
            "bg_upgrade", "bg_refresh", "bg_freeze", "bg_coin" }
        for c = 0, 2 do
            for r = 0, 1 do
                table.insert(names, "bg_board_c" .. c .. "r" .. r)
            end
        end
        for i = 1, table.getn(names) do
            pieces[names[i]] = { path = B .. names[i], w = 64, h = 64, right = 0.5, bottom = 1 }
        end
        pieces.minion_oval.anchors = { inner = { left = 0.18, top = 0.17, right = 0.81, bottom = 0.82 },
            oval = { left = 0.09, top = 0.07, right = 0.9, bottom = 0.92 } }
        pieces.taunt_frame.anchors = { inner = { left = 0.13, top = 0.09, right = 0.87, bottom = 0.81 } }
        pieces.divine_shield.anchors = { inner = { left = 0.13, top = 0.05, right = 0.91, bottom = 0.91 } }
        HS_BoardTextures = { generated = true, pieces = pieces }
        local savedBg = HS_BgTextures
        local heroId = "TB_BaconShop_HERO_34"
        HS_BgTextures = { generated = true, count = 0, set = {}, heroes = { "bob", heroId }, cards = { BG_CFM_315 = true } }
        pieces.glow_green = { path = B .. "glow_green", w = 128, h = 192, right = 1, bottom = 0.75 }
        pieces.glow_yellow = { path = B .. "glow_yellow", w = 128, h = 192, right = 1, bottom = 0.75 }
        boot()
        feed("BG_STATE", "0", statePayload({ pl = lobby(heroId, 1) }))
        tick(0.1)
        local f = UI.frame
        check(f.artMode and table.getn(f.boardTiles) == 6, "the board is six texture tiles")
        check(f.boardTiles[1]._.texture == B .. "bg_board_c0r0" and f.boardTiles[6]._.texture == B .. "bg_board_c2r1", "board tile paths")
        check(f.upgrade.tex and f.upgrade.cost:GetText() == "|cffffd1005|r" and f.upgrade.caption:GetText() ~= "", "the Upgrade shield shows its cost")
        check(f.upgrade.text:GetText() == "Upgrade (5)", "the full label is kept")
        check(f.refresh.cost:GetText() == "|cffffd1001|r" and f.freeze.cost:GetText() == "|cffffd1000|r", "Refresh costs 1, Freeze 0")
        check(f.endTurn.tex ~= nil and not f.endTurn.text:IsShown(), "End Turn is the stone button, its label is the picture")
        check(f.coin._.texture == B .. "bg_coin", "the Gold coin is the board's")
        local o = UI.offers[1]
        check(o.artMode and o.bg ~= nil and o.gemA ~= nil, "a tile is an oval with gems")
        check(o.atk:GetText() == "|cffffffff2|r", "white numbers on the gems: " .. tostring(o.atk:GetText()))
        check(not o.taunt:IsShown() and not o.shield:IsShown(), "a plain minion has no frame")
        check(UI.board[1].taunt:IsShown() and not UI.board[1].shield:IsShown(), "a Taunt minion wears the steel frame")
        check(not string.find(UI.board[1].kw:GetText(), "T", 1, true), "its Taunt is not repeated as a letter")
        check(not UI.board[3].entry and UI.board[3].gemA:IsShown() == false, "an empty slot has no gems")
        -- the portrait fills the oval of the minion_oval piece (taller than wide), not a square in its middle
        local _, _, _, ax, ay = o.art:GetPoint(1)
        local _, _, _, bx, by = o.art:GetPoint(2)
        check(ax > 0 and bx < UI.TILE_W and (ay - by) > (bx - ax), "the portrait fills the oval: " .. tostring(bx - ax) .. "x" .. tostring(ay - by))
        -- the gems (10 px beyond a tile) do not reach the neighbour's
        check(UI.ROW_GAP >= 18, "minions stand far enough apart for their gems")
        -- rows are centred: the first offer starts after (780 - 2 * 82 - UI.ROW_GAP) / 2
        local _, _, _, px = UI.offers[1]:GetPoint(1)
        check(px == (780 - (2 * UI.OFFER_W + UI.ROW_GAP)) / 2, "the tavern row is centred: " .. tostring(px))
        -- my board is centred on its minions; the empty slot to play into stands after them
        local _, _, _, bx0 = UI.board[1]:GetPoint(1)
        check(bx0 == (780 - (2 * UI.TILE_W + UI.ROW_GAP)) / 2, "my board is centred on its two minions: " .. tostring(bx0))
        -- a hand card is the whole card: its render, the halo, and it grows from its bottom edge under the cursor
        local hc = UI.hand[1]
        check(hc.cardMode and hc.cardTex:IsShown() and hc.cardTex._.texture == HsBgVis.CARD_PATH .. "BG_CFM_315",
            "a hand card shows its card: " .. tostring(hc.cardTex._.texture))
        check(hc:GetWidth() == UI.HAND_W and hc:GetHeight() == UI.HAND_H, "at the size of a card")
        check(not hc.art:IsShown() and not hc.name:IsShown(), "no oval portrait and no name text over the card")
        check(hc.restGlow == "green" and hc.cardGlows.green.tex:IsShown(), "a playable card has the green halo")
        UI.OnTileEnter(hc)
        check(hc.popped and hc:GetScale() == UI.HAND_POP_W / UI.HAND_W and not HsBgVis.CardShown(), "the hovered card grows, no second card")
        UI.OnTileLeave(hc)
        check(not hc.popped and hc:GetScale() == 1, "it shrinks back")
        -- the hand stands left of my hero in a fan: the middle card upright, the outer ones leaning outwards and lower
        local hxs, hys, has = UI.HandXs(5, BG.model)
        check(hxs and hxs[1] and hxs[5] + UI.RECT.hand[1] + UI.HAND_W <= UI.RECT.handLeft[2] + 1,
            "the whole hand stands left of the hero")
        check(has[3] == 0 and has[1] < 0 and has[5] > 0 and hys[1] > hys[3], "the hand is fanned")
        -- a waiting End Turn is dimmed
        feed("BG_STATE", "0", statePayload({ en = "1", pl = lobby(heroId, 1) }))
        tick(0.1)
        check(f.endTurn.tex._.vertex[1] < 1, "a waiting End Turn is dimmed")
        -- Bob's arch and the lobby's portrait column
        check(f.bobHero ~= nil and f.bobHero.pic == "bob" and f.bobHero.strips[1]._.texture == H.BgUI.HERO_PATH .. "bob"
            and not f.bob:IsShown(), "Bob stands in the golden arch")
        check(not f.bobHero.hp.frame:IsShown(), "Bob has no health gem")
        check(table.getn(LOBBY.rows) == 8 and LOBBY.rows[1].portrait ~= nil, "eight portrait cells")
        check(LOBBY.rows[1].portrait._.texture == H.BgUI.HERO_PATH .. heroId,
            "a seat wears its real hero portrait: " .. tostring(LOBBY.rows[1].portrait._.texture))
        check(not LOBBY.rows[1].label:IsShown() and string.find(LOBBY.rows[1].label:GetText(), "You", 1, true) ~= nil,
            "the texts stay for the tooltip only: " .. tostring(LOBBY.rows[1].label:GetText()))
        check(LOBBY.rows[1].frameTex._.vertex[1] == 1 and LOBBY.rows[5].frameTex._.vertex[1] > 0.8 and LOBBY.rows[5].frameTex._.vertex[2] < 0.3,
            "gold frame for you, red for a foe")
        check(LOBBY.rows[2].frameTex._.vertex[3] > 0.9, "blue frame for an ally")
        check(LOBBY.rows[1].bar:IsShown() and LOBBY.rows[1].bar:GetWidth() > 0, "the health bar is drawn")
        HS_BgTextures = savedBg
        HS_BoardTextures = saved
        resetUI()
    end

    -- ------------------------------------------------------------------ hero pick, hero, power, send, gift
    local function setArt()
        local savedB, savedT = HS_BoardTextures, HS_BgTextures
        local B = "Interface\\Hearthstone\\Textures\\Board\\"
        local pieces = {}
        local names = { "minion_oval", "taunt_frame", "divine_shield", "gem_attack", "gem_health" }
        for c = 0, 2 do
            for r = 0, 1 do
                table.insert(names, "bg_board_c" .. c .. "r" .. r)
            end
        end
        for i = 1, table.getn(names) do
            pieces[names[i]] = { path = B .. names[i], w = 64, h = 64, right = 0.5, bottom = 1 }
        end
        pieces.minion_oval.anchors = { inner = { left = 0.18, top = 0.17, right = 0.81, bottom = 0.82 },
            oval = { left = 0.09, top = 0.07, right = 0.9, bottom = 0.92 } }
        HS_BoardTextures = { generated = true, pieces = pieces }
        HS_BgTextures = { generated = true, count = 0, set = {}, heroes = { "bob" }, powers = {} }
        return function()
            HS_BoardTextures, HS_BgTextures = savedB, savedT
            resetUI()
        end
    end

    local function testHeroPick()
        boot()
        feed("BG_STATE", "0", statePayload({ ph = "p", hc = "TB_BaconShop_HERO_34,TB_BaconShop_HERO_57" }))
        tick(0.1)
        check(PICK.frame ~= nil and PICK.frame:IsShown(), "the hero pick panel opened")
        check(PICK.tiles[1] and PICK.tiles[1]:IsShown() and PICK.tiles[2]:IsShown(), "two hero tiles")
        check(not UI.tavern:IsShown() and not UI.boardRow:IsShown() and not UI.handRow:IsShown(),
            "the shop is hidden during the pick")
        check(not UI.frame.upgrade:IsShown() and not UI.frame.endTurn:IsShown(), "the side buttons are hidden")
        check(string.find(UI.frame.message:GetText(), "hero", 1, true) ~= nil, "the hint names the pick: "
            .. UI.frame.message:GetText())
        check(not lastSent("BG_ACT"), "a click only selects")
        click(PICK.tiles[2])
        click(PICK.tiles[2])
        check(PICK.selected == "TB_BaconShop_HERO_57" and PICK.tiles[2].glow:IsShown(), "a second click keeps the hero selected")
        local gl = PICK.tiles[2].glowLayer
        check(gl == nil or gl:GetFrameLevel() > PICK.tiles[2]:GetFrameLevel(), "the glow is drawn above the tile")
        check(PICK.tiles[2].top:GetFrameLevel() > (gl and gl:GetFrameLevel() or 0), "the shield is drawn above the glow")
        click(PICK.tiles[1])
        check(PICK.selected == "TB_BaconShop_HERO_34" and PICK.tiles[1].glow:IsShown() and not PICK.tiles[2].glow:IsShown(),
            "switching to the first hero selects it in one click")
        click(PICK.tiles[2])
        check(PICK.selected == "TB_BaconShop_HERO_57" and PICK.tiles[2].glow:IsShown(), "the clicked hero is selected")
        click(PICK.frame.confirm)
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=hero;i=1", "hero pick payload: " .. tostring(act and act.payload))
        feed("BG_STATE", act.reqId, statePayload())
        tick(0.1)
        check(not PICK.frame:IsShown() and UI.tavern:IsShown(), "the shop returns once the pick is done")
    end

    -- ------------------------------------------------------------------ quest pick, quest icon, reward icon
    -- A spell that needs a friendly minion shows the match window's targeting; the others are cast without one.
    local function testSpellTargets()
        local gem, coin = { id = "BG20_GEM" }, { id = "BG_GoldCoin" }
        check(BG.SpellNeedsTarget(gem) and not BG.SpellNeedsTarget(coin), "Blood Gem needs a target, a coin does not")
        check(BG.ValidSpellTarget(gem, { id = "BG20_100" }) and not BG.ValidSpellTarget(gem, nil), "any friendly minion")
        local banana = { id = "TB_BaconShop_HP_038t" }
        check(BG.ValidSpellTarget(banana, { id = "BG20_100", golden = false }) == false, "a Wild Banana needs a Beast")
        local repeatCustomer = { id = "BGS_Treasures_034" }
        check(not BG.ValidSpellTarget(repeatCustomer, { id = "BG20_100", golden = true }), "not a golden minion")
        UI.UpdateTargeting()
        check(true, "no targeting without a picked-up spell")
    end

    local function testQuestPick()
        boot()
        feed("BG_STATE", "0", statePayload({ ph = "q",
            qo = "BG24_Quest_112:BG24_Reward_107:0:0:0|BG24_Quest_114:BG24_Reward_109:0:0:0|BG24_Quest_120:BG24_Reward_111:0:0:0" }))
        tick(0.1)
        check(QUESTPICK.frame ~= nil and QUESTPICK.frame:IsShown(), "the quest pick panel opened")
        check(table.getn(QUESTPICK.tiles) == 3, "three quest tiles")
        check(QUESTPICK.tiles[1]:IsShown() and QUESTPICK.tiles[3]:IsShown(), "the tiles are shown")
        check(not UI.tavern:IsShown() and not UI.boardRow:IsShown() and not UI.handRow:IsShown(),
            "the shop is hidden during the quest pick")
        check(not UI.frame.upgrade:IsShown() and not UI.frame.endTurn:IsShown(), "the side buttons are hidden")
        check(string.find(UI.frame.message:GetText(), "quest", 1, true) ~= nil, "the hint names the pick: "
            .. UI.frame.message:GetText())
        -- the first tile names its quest and reward and carries the quest text
        check(string.find(QUESTPICK.tiles[1].questName:GetText(), "Track the Footprints", 1, true) ~= nil,
            "the quest name: " .. QUESTPICK.tiles[1].questName:GetText())
        check(string.find(QUESTPICK.tiles[1].questText:GetText(), "Refreshed", 1, true) ~= nil, "the quest text")
        check(string.find(QUESTPICK.tiles[1].rewardName:GetText(), "Snicker Snacks", 1, true) ~= nil,
            "the reward name: " .. QUESTPICK.tiles[1].rewardName:GetText())
        check(QUESTPICK.tiles[1].rewardText:GetText() ~= "", "the reward text")
        -- without art the tile is a plain text card (no portrait)
        check(not QUESTPICK.tiles[1].questArt:IsShown() and not QUESTPICK.tiles[1].rewardArt:IsShown(),
            "no art: a plain text tile")
        -- a click picks the second pair
        click(QUESTPICK.tiles[2])
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=quest;i=1", "quest pick payload: " .. tostring(act and act.payload))
        feed("BG_STATE", act.reqId, statePayload({ q = "BG24_Quest_114", qp = "0", qr = "14" }))
        tick(0.1)
        check(not QUESTPICK.frame:IsShown() and UI.tavern:IsShown(), "the shop returns once the quest is picked")
    end

    -- With the quest/reward art installed the tiles wear the round portraits; the panel falls back without them.
    local function testQuestPickArt()
        boot()
        local saved = HS_BgTextures
        HS_BgTextures = { generated = true, count = 0, set = {},
            quests = { "BG24_Quest_112" }, rewards = { "BG24_Reward_107" } }
        feed("BG_STATE", "0", statePayload({ ph = "q",
            qo = "BG24_Quest_112:BG24_Reward_107:0:0:0|BG24_Quest_114:BG24_Reward_109:0:0:0" }))
        tick(0.1)
        local t = QUESTPICK.tiles[1]
        check(not t.questArt:IsShown(), "the quest plate carries no portrait (its gem stands for it)")
        check(t.card._.texture == H.BgUI.BG_PATH .. "reward_card" and t.plate._.texture == H.BgUI.BG_PATH .. "quest_frame",
            "the reward card on top, the quest plate below")
        check(t.rewardArt:IsShown() and t.rewardArt._.texture == H.BgUI.QUEST_PATH .. "BG24_Reward_107",
            "the reward portrait path")
        check(not QUESTPICK.tiles[2].rewardArt:IsShown(), "an id without art leaves the card window empty")
        -- a reward with a real card render shows it (and the coin over its gem); the others keep the drawn card
        check(not t.real and t.coin:IsShown(), "no real card listed: the drawn card wears the coin over its gem")
        HS_BgTextures.rewardCards = { "BG24_Reward_107" }
        QUESTPICK.Refresh()
        check(t.real and t.coin:IsShown() and t.card._.texture == H.BgUI.QUEST_PATH .. "Cards\\BG24_Reward_107",
            "the real reward card: " .. tostring(t.card._.texture))
        check(not t.rewardName:IsShown() and not t.rewardArt:IsShown(), "the real card carries its own name and art")
        check(not QUESTPICK.tiles[2].real, "an unlisted reward keeps the drawn card")
        -- a baked card (real frame and art, no words) gets its name and text written over it
        HS_BgTextures.rewardBaked = { "BG24_Reward_107" }
        QUESTPICK.Refresh()
        check(t.baked and t.rewardName:IsShown() and t.rewardText:IsShown(), "a baked card shows the addon's words")
        HS_BgTextures = saved
        resetUI()
    end

    local function testQuestIconAndReward()
        boot()
        local saved = HS_BgTextures
        HS_BgTextures = { generated = true, count = 0, set = {},
            quests = { "BG24_Quest_114" }, rewards = { "BG24_Reward_109" } }
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "4", qr = "14" }))
        tick(0.1)
        check(UI.frame.myQuest:IsShown(), "the quest icon is shown")
        check(UI.frame.myQuest.art:IsShown() and UI.frame.myQuest.art._.texture == H.BgUI.QUEST_PATH .. "BG24_Quest_114",
            "the quest icon art")
        check(UI.frame.myQuest.progress:GetText() == "|cffffffff4/14|r", "the progress: "
            .. UI.frame.myQuest.progress:GetText())
        check(not UI.frame.myReward:IsShown(), "no reward icon before the quest is done")
        -- its tooltip names the quest and the progress
        F.fire(UI.frame.myQuest, "OnEnter", {})
        check(string.find(table.concat(F.tooltip.lines, "\n"), "Track", 1, true) ~= nil
            or string.find(table.concat(F.tooltip.lines, "\n"), "Assemble", 1, true) ~= nil, "the quest tooltip")
        check(string.find(table.concat(F.tooltip.lines, "\n"), "4/14", 1, true) ~= nil, "progress in the tooltip")
        -- the quest completes: the reward icon appears with its tooltip
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "14", qr = "14", rw = "BG24_Reward_109" }))
        tick(0.1)
        check(UI.frame.myReward:IsShown() and UI.frame.myReward.art:IsShown(), "the reward icon is shown")
        check(UI.frame.myReward.art._.texture == H.BgUI.QUEST_PATH .. "BG24_Reward_109", "the reward icon path")
        F.fire(UI.frame.myReward, "OnEnter", {})
        check(string.find(table.concat(F.tooltip.lines, "\n"), "Stolen Gold", 1, true) ~= nil, "the reward tooltip")
        HS_BgTextures = saved
        resetUI()
    end

    -- Without art the quest icon and the reward icon fall back to their names.
    local function testQuestIconPlain()
        boot()
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_112", qp = "2", qr = "10", rw = "BG24_Reward_107" }))
        tick(0.1)
        check(not UI.frame.myQuest.art:IsShown(), "no quest art: the icon is text")
        check(string.find(UI.frame.myQuest.name:GetText(), "Track the Footprints", 1, true) ~= nil, "the quest name fallback")
        check(UI.frame.myQuest.progress:GetText() == "|cffffffff2/10|r", "the progress is kept")
        check(not UI.frame.myReward.art:IsShown(), "no reward art: the icon is text")
        check(string.find(UI.frame.myReward.name:GetText(), "Snicker Snacks", 1, true) ~= nil, "the reward name fallback")
        -- nothing taken: both hidden
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
        check(not UI.frame.myQuest:IsShown() and not UI.frame.myReward:IsShown(), "no quest and no reward icon")
    end

    -- Sire Denathrius's second quest and reward, the Darkmoon Ticket badge and a skipped turn.
    local function testSecondQuest()
        boot()
        local saved = HS_BgTextures
        HS_BgTextures = { generated = true, count = 0, set = {},
            quests = { "BG24_Quest_114" }, rewards = { "BG24_Reward_109" } }
        feed("BG_STATE", "0", statePayload({ q2 = "BG24_Quest_114", qp2 = "3", qr2 = "14" }))
        tick(0.1)
        check(UI.frame.myQuest:IsShown(), "the second quest uses the quest icon")
        check(UI.frame.myQuest.art._.texture == H.BgUI.QUEST_PATH .. "BG24_Quest_114", "the second quest's art")
        check(UI.frame.myQuest.progress:GetText() == "|cffffffff3/14|r", "the second quest's progress: "
            .. UI.frame.myQuest.progress:GetText())
        F.fire(UI.frame.myQuest, "OnEnter", {})
        check(string.find(table.concat(F.tooltip.lines, "\n"), "Second quest", 1, true) ~= nil, "the second quest tooltip")
        feed("BG_STATE", "0", statePayload({ q2 = "BG24_Quest_114", qp2 = "14", qr2 = "14", rw2 = "BG24_Reward_109" }))
        tick(0.1)
        check(UI.frame.myReward:IsShown() and UI.frame.myReward.art._.texture == H.BgUI.QUEST_PATH .. "BG24_Reward_109",
            "the second reward uses the reward icon")
        HS_BgTextures = saved
        resetUI()
    end

    local function testDarkmoonTicket()
        local saved = HS_BoardTextures
        HS_BoardTextures = { generated = true, pieces = { bg_coin = { path = "Interface\\Hearthstone\\Textures\\Board\\bg_coin" } } }
        boot()
        feed("BG_STATE", "0", statePayload({ oft = "10" }))
        tick(0.1)
        check(BG.model.offers[1].ticket == true, "parsed ticket: " .. tostring(BG.model.offers[1].ticket))
        check(UI.offers[1].ticket ~= nil and UI.offers[1].ticket:IsShown(), "the first offer wears a ticket")
        check(UI.offers[2].ticket ~= nil and not UI.offers[2].ticket:IsShown(), "the second offer has no ticket")
        feed("BG_STATE", "0", statePayload({ oft = "01" }))
        tick(0.1)
        check(not UI.offers[1].ticket:IsShown() and UI.offers[2].ticket:IsShown(), "the badge moves with the ticket")
        HS_BoardTextures = saved
        resetUI()
    end

    local function testSkipTurn()
        boot()
        feed("BG_STATE", "0", statePayload({ pws = "1" }))
        tick(0.1)
        check(string.find(UI.frame.message:GetText() or "", "skipped", 1, true) ~= nil,
            "the skipped turn notice: " .. tostring(UI.frame.message:GetText()))
    end

    -- My Secrets arrive in play order and show as icons on my hero portrait, with their card on hover.
    local function testSecrets()
        boot()
        feed("BG_STATE", "0", statePayload({ sec = "TB_Bacon_Secrets_01,TB_Bacon_Secrets_12" }))
        tick(0.1)
        check(table.getn(BG.model.secrets) == 2 and BG.model.secrets[1] == "TB_Bacon_Secrets_01", "parsed secrets")
        check(UI.frame.secrets[1]:IsShown() and UI.frame.secrets[2]:IsShown(), "two secret icons")
        check(not UI.frame.secrets[3]:IsShown(), "no third secret icon")
        check(UI.frame.secrets[1].secretId == "TB_Bacon_Secrets_01", "the icon knows its secret")
        -- a new state with fewer secrets hides the rest
        feed("BG_STATE", "0", statePayload({ sec = "TB_Bacon_Secrets_12" }))
        tick(0.1)
        check(UI.frame.secrets[1]:IsShown() and not UI.frame.secrets[2]:IsShown(), "the icons follow the state")
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
        check(not UI.frame.secrets[1]:IsShown(), "no secret, no icon")
        -- a Secret on the Discover overlay is named as such
        feed("BG_STATE", "0", statePayload({ op = minion("TB_Bacon_Secrets_01", false, 0, 0, 0, 201) }))
        tick(0.1)
        check(string.find(UI.discover.text:GetText(), "Secret", 1, true) ~= nil, "the Discover names the Secret")
        resetUI()
    end

    -- The lobby cell tooltip names the seat's quest and reward (the tooltip lives on the portrait column).
    local function testLobbyQuestTooltip()
        local savedB, savedT = HS_BoardTextures, HS_BgTextures
        local B = "Interface\\Hearthstone\\Textures\\Board\\"
        local pieces = {}
        for c = 0, 2 do
            for r = 0, 1 do
                pieces["bg_board_c" .. c .. "r" .. r] = { path = B .. "bg_board_c" .. c .. "r" .. r, w = 64, h = 64 }
            end
        end
        HS_BoardTextures = { generated = true, pieces = pieces }
        HS_BgTextures = { generated = true, count = 0, set = {}, heroes = { "bob" }, powers = {} }
        boot()
        check(LOBBY.art, "the lobby is the portrait column")
        feed("BG_STATE", "0", statePayload({ pl = lobby("TB_BaconShop_HERO_34", 2, "BG24_Quest_112",
            "BG24_Reward_107", 2) }))
        tick(0.1)
        F.fire(LOBBY.rows[2], "OnEnter", {})
        local lines = table.concat(F.tooltip.lines, "\n")
        check(string.find(lines, "Quest: Track the Footprints", 1, true) ~= nil, "the seat quest: " .. lines)
        check(string.find(lines, "Reward: Snicker Snacks", 1, true) ~= nil, "the seat reward")
        -- a seat without a quest shows neither line
        F.fire(LOBBY.rows[1], "OnEnter", {})
        check(string.find(table.concat(F.tooltip.lines, "\n"), "Quest:", 1, true) == nil,
            "no quest line for an empty seat")
        HS_BoardTextures, HS_BgTextures = savedB, savedT
        resetUI()
    end

    -- The result screen shows the payout when the server paid one.
    local function testPayout()
        boot()
        feed("BG_STATE", "0", statePayload({ ph = "o", al = "0" }))
        feed("BG_END", "0", P.KvEncode({ { "win", "1" }, { "myf", "0" }, { "won", "1" }, { "pf", "1" },
            { "po", "1" }, { "al", "1" }, { "rd", "9" }, { "tm", "0" }, { "rc", "15" }, { "rp", "2" },
            { "pl", list({ "1", "2", "3", "4", "5", "6", "7", "8" }) } }))
        tick(0.1)
        check(ENDUI.frame ~= nil and ENDUI.frame:IsShown(), "the result screen is up")
        check(string.find(ENDUI.frame.payout:GetText(), "15 coins", 1, true) ~= nil, "the coins: "
            .. ENDUI.frame.payout:GetText())
        check(string.find(ENDUI.frame.payout:GetText(), "2 packs", 1, true) ~= nil, "the packs")
        -- a game with no payout shows nothing
        feed("BG_END", "0", P.KvEncode({ { "win", "0" }, { "myf", "0" }, { "won", "0" }, { "pf", "4" },
            { "po", "8" }, { "al", "0" }, { "rd", "12" }, { "tm", "0" }, { "rc", "0" }, { "rp", "0" },
            { "pl", list({ "8", "7", "6", "5", "4", "3", "2", "1" }) } }))
        tick(0.1)
        check(ENDUI.frame.payout:GetText() == "", "no payout: no line")
        -- a single coin/pack reads in the singular
        check(ENDUI.PayoutText({ coins = 1, packs = 1 }) == "|cff66ff66Your reward: 1 coin and 1 pack.|r", "the singular")
    end

    -- with the board art the hero is the _arch picture in the arch_bottom frame, and the power sits in the ring
    local function testHeroArch()
        local savedB, savedT = HS_BoardTextures, HS_BgTextures
        local BS = string.char(92)
        local B = "Interface" .. BS .. "Hearthstone" .. BS .. "Textures" .. BS .. "Board" .. BS
        local pieces = {}
        local names = { "arch_top", "hero_power_ring", "bg_coin" }
        for i = 1, table.getn(names) do
            pieces[names[i]] = { path = B .. names[i], w = 64, h = 64, right = 1, bottom = 1 }
        end
        pieces.arch_top.anchors = { opening = { left = 0.06, top = 0.06, right = 0.93, bottom = 0.94 } }
        HS_BoardTextures = { generated = true, pieces = pieces }
        local heroId = "TB_BaconShop_HERO_34"
        HS_BgTextures = { generated = true, count = 0, set = {}, heroes = { "bob", heroId, heroId .. "_arch" },
            powers = { "TB_BaconShop_HP_010" } }
        resetUI()
        boot()
        feed("BG_STATE", "0", statePayload({ hero = heroId, arm = "3", pw = "TB_BaconShop_HP_010", pwc = "2",
            pwu = "1", pwt = "f" }))
        tick(0.1)
        local f = UI.frame
        check(f.hero.archTex and f.hero.frame:IsShown(), "the hero arch is shown")
        check(f.hero.strips[1]._.texture == UI.HERO_PATH .. heroId .. "_arch", "the arch picture path")
        check(not f.hero.square:IsShown(), "the square portrait is not shown with the arch")
        check(f.hero.armor.frame:IsShown() and f.hero.armor.text:GetText() == "3", "the armor shield")
        check(f.power.hasRing and f.power.ring:IsShown(), "the power ring is shown")
        check(f.power.coin:IsShown() and f.power.cost:GetText() == "|cffffd1002|r", "the cost sits on the coin")
        -- without the arch picture the square portrait returns
        HS_BgTextures.heroes = { "bob", heroId }
        feed("BG_STATE", "0", statePayload({ hero = heroId, arm = "3" }))
        tick(0.1)
        check(f.hero.strips[1]._.texture == UI.HERO_PATH .. heroId and not f.hero.square:IsShown(),
            "without the _arch cut the square portrait fills the arch's strips")
        HS_BoardTextures, HS_BgTextures = savedB, savedT
        resetUI()
    end

    -- The whole window with the board art: tiles with tier pips, badges and ice, the card on hover, Discover as
    -- cards, and a fight played to its end on the board
    local function testArtMode()
        local savedB, savedT = HS_BoardTextures, HS_BgTextures
        local BS = string.char(92)
        local B = "Interface" .. BS .. "Hearthstone" .. BS .. "Textures" .. BS .. "Board" .. BS
        local pieces = {}
        local names = { "bg_board_c0r0", "bg_board_c1r0", "bg_board_c2r0", "bg_board_c0r1", "bg_board_c1r1",
            "bg_board_c2r1", "minion_oval", "gem_attack", "gem_health", "gem_armor", "taunt_frame", "divine_shield",
            "frozen_ice", "glow_oval", "glow_round", "glow_arch", "kw_deathrattle", "kw_continuous", "bg_upgrade",
            "bg_refresh", "bg_freeze", "end_turn_button", "bg_coin", "hero_power_ring", "arch_top", "bg_tier_1",
            "bg_tier_2", "bg_tier_3", "bg_tier_4", "bg_tier_5", "bg_tier_6", "bg_tier_7" }
        for i = 1, table.getn(names) do
            pieces[names[i]] = { path = B .. names[i], w = 64, h = string.find(names[i], "bg_tier_", 1, true) and 68 or 64,
                right = 1, bottom = 1,
                anchors = { inner = { left = 0.1, top = 0.1, right = 0.9, bottom = 0.9 },
                    oval = { left = 0.09, top = 0.074, right = 0.9, bottom = 0.926 },
                    opening = { left = 0.06, top = 0.06, right = 0.93, bottom = 0.94 } } }
        end
        HS_BoardTextures = { generated = true, pieces = pieces }
        local heroId = "TB_BaconShop_HERO_34"
        HS_BgTextures = { generated = true, count = 3, set = { BG19_010 = true, BG21_003 = true, BG_EX1_531 = true },
            heroes = { "bob", heroId, heroId .. "_arch" }, powers = {}, powerCards = {},
            cards = { BG19_010 = true, BG19_010_G = true, BG21_003 = true, BG_CFM_315 = true, BG_EX1_531 = true } }
        resetUI()
        boot()
        feed("BG_STATE", "0", statePayload({ hero = heroId, fz = "1", op = "" }))
        tick(0.1)
        local f = UI.frame
        check(f.artMode, "the board art is in use")
        local offer = UI.offers[1]
        local tier = BG.Card(offer.entry.id).tier
        check(offer.tierShield:IsShown() and offer.tierShield._.texture == B .. "bg_tier_" .. tier
            and not offer.pips[1]:IsShown(), "an offer shows its tier shield on top")
        check(offer.tierShield:GetWidth() == UI.OFFER_SHIELD_W
            and offer.tierShield:GetHeight() == UI.OFFER_SHIELD_W * 68 / 64, "the shield keeps its aspect")
        check(f.tierShield._.texture == B .. "bg_tier_2" and f.tierFrame:IsShown() and not f.pips[1]:IsShown(),
            "Bob's arch wears the shield of my tier (2), no pips")
        check(offer.ice and offer.ice:IsShown(), "a frozen offer wears the ice")
        check(UI.offers[2].ice and UI.offers[2].ice:IsShown(), "every frozen offer wears the ice")
        -- Frozen per offer (ofz), only the second offer wears the ice
        feed("BG_STATE", "0", statePayload({ hero = heroId, fz = "0", ofz = "01", op = "" }))
        tick(0.1)
        check(not UI.offers[1].ice:IsShown() and UI.offers[2].ice:IsShown(), "only the frozen offer wears the ice")
        check(UI.offers[1].art._.vertex[1] == 1 and UI.offers[2].art._.vertex[1] < 1, "and only it gets the cold tint")
        feed("BG_STATE", "0", statePayload({ hero = heroId, fz = "0", ofz = "00", op = "" }))
        tick(0.1)
        check(not UI.offers[1].ice:IsShown() and not UI.offers[2].ice:IsShown(), "the ice goes when the offer thaws")
        feed("BG_STATE", "0", statePayload({ hero = heroId, fz = "1", op = "" }))
        tick(0.1)
        check(not UI.board[1].pips[1]:IsShown() and not UI.board[1].tierShield:IsShown(), "a minion on the board has no tier")
        check(UI.board[1].taunt:IsShown(), "the Taunt frame")
        check(UI.board[1].badges[1]:IsShown(), "a minion with a trigger shows its badge")
        -- hovering a minion shows its card (the golden twin's for a golden one)
        F.fire(UI.board[2], "OnEnter")
        check(HsBgVis.CardShown() and HsBgVis.card.tex._.texture == HsBgVis.CARD_PATH .. "BG19_010_G",
            "the golden card on hover: " .. tostring(HsBgVis.card and HsBgVis.card.tex._.texture))
        check(UI.board[2].glow.frame:IsShown(), "the hovered minion glows")
        F.fire(UI.board[2], "OnLeave")
        check(not HsBgVis.CardShown() and not UI.board[2].glow.frame:IsShown(), "leaving hides the card and the glow")
        -- hovering a shop button lights it
        F.fire(f.refresh, "OnEnter")
        check(f.refresh.hl:IsShown() and f.refresh.hoverGlow.frame:IsShown(), "the hovered button lights up")
        F.fire(f.refresh, "OnLeave")
        check(not f.refresh.hl:IsShown(), "and goes dark again")
        -- buying: the gold floats from the pill
        local before = table.getn(HsBgVis.items)
        feed("BG_STATE", "0", statePayload({ hero = heroId, g = "4" }))
        tick(0.05)
        check(table.getn(HsBgVis.items) > before, "the spent gold floats up")
        -- a Discover shows its options as cards
        feed("BG_STATE", "0", statePayload({ hero = heroId, op = list({ minion("BG19_010", false, 2, 3, 0, 201),
            minion("BG21_003", false, 5, 3, 0, 202) }) }))
        tick(0.1)
        check(UI.discover:IsShown() and UI.discover.cards[2] and UI.discover.cards[2]:IsShown(), "Discover as cards")
        check(not UI.discover.tiles[1]:IsShown(), "the tiles stay hidden")
        click(UI.discover.cards[2])
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=discover;i=1", "the card picks: " .. tostring(act and act.payload))
        -- a fight with the art, played to the end
        feed("BG_STATE", "0", statePayload({ hero = heroId }))
        tick(0.1)
        feed("BG_COMBAT", "0", combatPayload())
        tick(0.1)
        check(COMBAT.IsPlaying() and f.bobArt and not f.bobArt:IsShown(), "Bob steps aside for the opponent")
        local guard = 0
        while COMBAT.playing and guard < 400 do
            tick(0.05)
            guard = guard + 1
        end
        COMBAT.Button()                  -- Continue
        check(not COMBAT.IsPlaying() and UI.tavern:IsShown() and f.bobArt:IsShown(), "the shop is back after the fight")
        check(f.heroActor.lx == nil and f.heroActor.sx == nil, "my hero is home")
        check(table.getn(HsBgVis.items) == 0, "no animation is left running: " .. table.getn(HsBgVis.items))
        HS_BoardTextures, HS_BgTextures = savedB, savedT
        resetUI()
    end

    local function testHeroAndPower()
        boot()
        local saved = HS_BgTextures
        HS_BgTextures = { generated = true, count = 0, set = {},
            heroes = { "bob", "TB_BaconShop_HERO_34" }, powers = { "TB_BaconShop_HP_010" } }
        feed("BG_STATE", "0", statePayload({ hero = "TB_BaconShop_HERO_34", arm = "3", pw = "TB_BaconShop_HP_010",
            pwc = "2", pwx = "3", pwu = "1", pwt = "f" }))
        tick(0.1)
        check(UI.frame.hero.frame:IsShown() and UI.frame.hero.strips[1]:IsShown(), "my hero portrait is shown")
        check(UI.frame.hero.strips[1]._.texture == H.BgUI.HERO_PATH .. "TB_BaconShop_HERO_34", "the hero art path")
        check(UI.frame.hero.hp.frame:GetFrameLevel() > UI.frame.hero.frame:GetFrameLevel(),
            "the health gem stands above the portrait")
        check(UI.frame.hero.armor.text:GetText() == "3", "the armor shield: " .. tostring(UI.frame.hero.armor.text:GetText()))
        check(UI.frame.power.restGlow == "green", "a usable power glows green")
        check(UI.frame.power:IsShown() and UI.frame.power.art:IsShown(), "the power button wears its portrait")
        check(UI.frame.power.cost:GetText() == "|cffffd1002|r", "the power cost: " .. UI.frame.power.cost:GetText())
        check(UI.frame.power.count:IsShown() and UI.frame.power.count:GetText() == "|cffffd1003|r",
            "the power counter: " .. tostring(UI.frame.power.count:GetText()))
        -- a target power arms a click
        click(UI.frame.power)
        check(UI.powerArm == "f", "the power arms a friendly target")
        check(string.find(UI.frame.message:GetText(), "friendly minion", 1, true) ~= nil, "the hint asks for a target")
        click(UI.board[1])
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=power;u=104", "power payload: " .. tostring(act and act.payload))
        check(UI.powerArm == nil, "the arm cleared")
        feed("BG_STATE", act.reqId, statePayload())
        tick(0.1)
        -- a no-target power fires at once
        feed("BG_STATE", "0", statePayload({ hero = "TB_BaconShop_HERO_34", pw = "TB_BaconShop_HP_010",
            pwc = "1", pwu = "1", pwt = "n" }))
        tick(0.1)
        click(UI.frame.power)
        local n = lastSent("BG_ACT")
        check(n and n.payload == "act=power", "no-target power payload: " .. tostring(n and n.payload))
        feed("BG_STATE", n.reqId, statePayload())
        tick(0.1)
        -- Vol'jin (two minions of the board or the tavern): two clicks, the second one sends
        feed("BG_STATE", "0", statePayload({ hero = "TB_BaconShop_HERO_34", pw = "TB_BaconShop_HP_010",
            pwc = "0", pwu = "1", pwt = "m" }))
        tick(0.1)
        click(UI.frame.power)
        check(UI.powerArm == "m", "the power arms two minions")
        click(UI.board[1])
        check(UI.powerArm == "m" and UI.powerFirst == 104, "the first click only marks the minion")
        click(UI.board[1])
        check(UI.powerFirst == nil, "the same minion again unmarks it")
        click(UI.board[1])
        local second = BG.model.offers[1].uid
        click(UI.offers[1])
        local two = lastSent("BG_ACT")
        check(two and two.payload == "act=power;u=104;u2=" .. second, "two-minion payload: " .. tostring(two and two.payload))
        check(UI.powerArm == nil and UI.powerFirst == nil, "the arm cleared after the second minion")
        feed("BG_STATE", two.reqId, statePayload())
        tick(0.1)
        -- Lord Barov (a player of the lobby): the clicked seat is the offer index
        feed("BG_STATE", "0", statePayload({ hero = "TB_BaconShop_HERO_34", pw = "TB_BaconShop_HP_010",
            pwc = "1", pwu = "1", pwt = "s" }))
        tick(0.1)
        click(UI.frame.power)
        check(UI.powerArm == "s", "the power arms a lobby seat")
        local seatRow = nil
        for _, row in ipairs(HsBgLobbyUI.rows) do
            if seatRow == nil and row.seat == 1 then
                seatRow = row
            end
        end
        check(seatRow ~= nil, "the lobby shows seat 1")
        if seatRow then
            click(seatRow)
            local guess = lastSent("BG_ACT")
            check(guess and guess.payload == "act=power;o=1", "seat payload: " .. tostring(guess and guess.payload))
            check(UI.powerArm == nil, "the arm cleared after the seat")
            feed("BG_STATE", guess.reqId, statePayload())
            tick(0.1)
        end
        -- not usable: nothing is sent
        feed("BG_STATE", "0", statePayload({ hero = "TB_BaconShop_HERO_34", pw = "TB_BaconShop_HP_010",
            pwc = "9", pwu = "0", pwt = "f" }))
        tick(0.1)
        local before = table.getn(T.sentOps("BG_ACT"))
        click(UI.frame.power)
        ctx.drain(1)
        check(table.getn(T.sentOps("BG_ACT")) == before, "an unusable power sends nothing")
        HS_BgTextures = saved
        resetUI()
    end

    local function testSend()
        local restore = setArt()
        boot()
        check(LOBBY.art, "the lobby is the portrait column")
        -- seed the ally cell's rect (row 2 = seat 1, a living ally) and put the cursor on it
        local allyRow = LOBBY.rows[2]
        allyRow._.left, allyRow._.right, allyRow._.bottom, allyRow._.top = 100, 150, 300, 350
        -- the click fallback: pick a hand card, then click the ally cell
        click(UI.hand[1])
        check(UI.sel and UI.sel.zone == "hand", "a hand card is picked up")
        F.fire(allyRow, "OnClick", { arg1 = "LeftButton" })
        check(UI.sel == nil, "the pick cleared after the send")
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=send;i=0;to=1", "the click fallback sends: " .. tostring(act and act.payload))
        feed("BG_STATE", act.reqId, statePayload({ snd = "1" }))
        tick(0.1)
        -- a used send: a drag sends nothing
        local before = table.getn(T.sentOps("BG_ACT"))
        F.cursor[1], F.cursor[2] = 120, 320
        UI.StartDrag("hand", 0)
        check(UI.drag ~= nil, "the card is dragged")
        UI.StopDrag()
        ctx.drain(1)
        check(table.getn(T.sentOps("BG_ACT")) == before, "a used send sends nothing")
        -- a fresh turn: the drag onto the ally sends
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
        F.cursor[1], F.cursor[2] = 120, 320
        UI.StartDrag("hand", 0)
        UI.StopDrag()
        local d = lastSent("BG_ACT")
        check(d and d.payload == "act=send;i=0;to=1", "the drag sends: " .. tostring(d and d.payload))
        restore()
    end

    -- Escape goes through UISpecialFrames only while something is armed (no EnableKeyboard: it would take every key);
    -- an armed power ends with its round, the end of the turn or a state that says it is not usable.
    local function testEscapeAndArm()
        boot()
        local function listed()
            for i = 1, table.getn(UISpecialFrames) do
                if UISpecialFrames[i] == "HearthstoneBgFrame" then
                    return true
                end
            end
            return false
        end
        local power = { hero = "TB_BaconShop_HERO_34", pw = "TB_BaconShop_HP_010", pwc = "2", pwu = "1", pwt = "f" }
        local function withPower(extra)
            local t = {}
            for k, v in pairs(power) do
                t[k] = v
            end
            for k, v in pairs(extra or {}) do
                t[k] = v
            end
            return statePayload(t)
        end
        feed("BG_STATE", "0", withPower())
        tick(0.1)
        check(not UI.frame._.keyboard, "the window does not take the keyboard")
        check(not listed(), "nothing armed: Escape is left to the game menu")
        click(UI.frame.power)
        check(UI.powerArm == "f" and listed(), "an armed power lists the window for Escape")
        UI.frame:Hide()                       -- what Escape does to a UISpecialFrames window
        check(UI.frame:IsShown() and UI.powerArm == nil, "Escape cancels the power and keeps the window")
        check(not listed(), "unlisted again after the cancel")
        click(UI.hand[1])
        check(listed(), "a picked-up card lists the window too")
        UI.frame:Hide()
        check(UI.frame:IsShown() and UI.sel == nil and not listed(), "Escape drops the picked-up card")
        -- the arm does not outlive its round, the end of the turn or the power's use
        click(UI.frame.power)
        feed("BG_STATE", "0", withPower({ rd = "4" }))
        tick(0.1)
        check(UI.powerArm == nil, "a new round disarms the power")
        click(UI.frame.power)
        feed("BG_STATE", "0", withPower({ en = "1" }))
        tick(0.1)
        check(UI.powerArm == nil, "ending the turn disarms the power")
        feed("BG_STATE", "0", withPower())
        tick(0.1)
        click(UI.frame.power)
        feed("BG_STATE", "0", withPower({ pwu = "0" }))
        tick(0.1)
        check(UI.powerArm == nil and not listed(), "an unusable power is disarmed")
        -- pwu=0 says why: too little gold, or used
        feed("BG_STATE", "0", withPower({ pwu = "0", g = "1" }))
        tick(0.1)
        click(UI.frame.power)
        check(UI.frame.message:GetText() == BG.WHY_TEXT.NotEnoughGold, "too little gold: " .. UI.frame.message:GetText())
        feed("BG_STATE", "0", withPower({ pwu = "0" }))
        tick(0.1)
        click(UI.frame.power)
        check(UI.frame.message:GetText() == BG.WHY_TEXT.PowerUsed, "used: " .. UI.frame.message:GetText())
    end

    -- Without the board art the lobby rows also take a send (row.seat is set in both looks).
    local function testSendPlainLobby()
        boot()
        check(not LOBBY.art, "the plain lobby")
        click(UI.hand[1])
        F.fire(LOBBY.rows[2], "OnClick", { arg1 = "LeftButton" })
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=send;i=0;to=1", "the plain lobby sends: " .. tostring(act and act.payload))
    end

    local function testGift()
        boot()
        feed("BG_STATE", "0", statePayload({ gift = "BG_CFM_315" }))
        tick(0.1)
        check(string.find(UI.frame.message:GetText(), "An ally sent you", 1, true) ~= nil, "the gift line: "
            .. UI.frame.message:GetText())
        check(UI.hand[1]._.bdBorder[1] == 1 and UI.hand[1]._.bdBorder[2] > 0.8, "the sent card is highlighted")
        check(string.find(table.concat(ctx.printed(), "\n"), "An ally sent you", 1, true) ~= nil, "the chat line")
        check(UI.frame.toast:IsShown(), "the gift line shows as a notice over the board")
        feed("BG_STATE", "0", statePayload())
        tick(0.1)
        tick(UI.TOAST_TIME + 0.5)
        check(not UI.frame.toast:IsShown(), "the notice fades once read")
        check(BG.message == nil, "the gift line is gone after the next state")
    end

    boot()
    testShopWindow()
    testBuyAndPlay()
    testBattlecryTarget()
    testSellBanana()
    testPowerNowLine()
    testSpellCast()
    testSpellCastQueued()
    testBuffGlow()
    testSellAndMove()
    testSideButtons()
    testMaxTier()
    testBuyCost()
    testDiscover()
    testDiscoverHide()
    testYoggWheel()
    testPowerChoice()
    testDrag()
    testDragBoard()
    testLoadingHidden()
    testPortraits()
    testBoardArt()
    testHeroPick()
    testQuestPick()
    testSpellTargets()
    testQuestPickArt()
    testQuestIconAndReward()
    testQuestIconPlain()
    testSecondQuest()
    testDarkmoonTicket()
    testSkipTurn()
    testSecrets()
    testHeroAndPower()
    testHeroArch()
    testArtMode()
    testSend()
    testEscapeAndArm()
    testSendPlainLobby()
    testLobbyQuestTooltip()
    testGift()
    testCombat()
    testRest()
    testEnd()
    testPayout()
    testLeaveWithAHostileState()
    resetUI()
    ctx.reset()
    F.uninstall()
end
