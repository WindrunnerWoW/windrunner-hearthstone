-- Warsong Battlegrounds model tests. Called from lua_tests.lua with the shared helpers:
--     dofile("tests/bg_tests.lua")({ H = H, check = check, reset = resetAddon, drain = drain, T = packHelpers() })
-- Exercises the WRBG codec (BgProtocol.lua) and the client state machine (BgState.lua) without a WoW UI.
-- Lua 5.0 style, like the addon.

return function(ctx)
    local H, check = ctx.H, ctx.check
    local P = HsProtocol
    local BP = HsBgProtocol
    local BG = HsBg

    -- ------------------------------------------------------------- payload builders (mirror the server encoder)
    local function minion(id, g, atk, hp, kw, uid)
        return id .. "." .. (g and "1" or "0") .. "." .. atk .. "." .. hp .. "." .. kw .. "." .. uid
    end
    local function list(items)
        return table.concat(items, ",")
    end
    -- 8 seats: health.tier.alive.elim.opp.heroId.armor.questId.rewardId.questId2.rewardId2 (seat 2 wears
    -- heroId/questId when given)
    local function lobby(enemyHealth, heroId, questId, rewardId)
        local out = {}
        for i = 1, 8 do
            local hp = i == 5 and enemyHealth or 40
            local alive = hp > 0 and "1" or "0"
            local hero = (i == 2 and heroId) or ""
            local armor = (i == 2 and heroId) and 5 or 0
            local quest = (i == 2 and questId) or ""
            local reward = (i == 2 and rewardId) or ""
            table.insert(out, hp .. "." .. 1 .. "." .. alive .. "." .. (hp > 0 and 0 or 3) .. "." .. (-1)
                .. "." .. hero .. "." .. armor .. "." .. quest .. "." .. reward .. "..")
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
            { "te", list({ "8", "16" }) },   -- the other faction's own set
            { "rf", "3" },                    -- my next Refresh cost
            { "pl", lobby(40) },
            { "win", "-1" },
            { "hc", "" }, { "hero", "" }, { "arm", "0" }, { "mhp", "40" }, { "rh", "0" }, { "pw", "" }, { "pwc", "0" },
            { "pwx", "0" }, { "pwu", "0" }, { "pwt", "n" }, { "snd", "0" }, { "gift", "" }, { "whl", "" }, { "nm", "|||||||" },
            { "qo", "" }, { "q", "" }, { "qp", "0" }, { "qr", "0" }, { "rw", "" },
            { "qa", "0" }, { "qb", "0" }, { "rt", "0" },
            { "pws", "0" }, { "pwh", "" }, { "pwe", "" }, { "sec", "" },
            { "q2", "" }, { "qp2", "0" }, { "qr2", "0" }, { "rw2", "" },
            { "qa2", "0" }, { "qb2", "0" }, { "rt2", "0" },
            { "nd", "" },
        }
        local map = {}
        if mut then
            for k, v in pairs(mut) do
                map[k] = v
            end
        end
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
        -- Keys the test named but the base payload does not carry (optional keys such as svh / svb) go at the end.
        for k, v in pairs(map) do
            if k ~= "ofz" and k ~= "oft" and v ~= false then
                table.insert(out, { k, v })
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
    local function combatPayload(mut)
        local kv = {
            { "rd", "2" }, { "ms", "0" }, { "opp", "5" }, { "w", "0" }, { "dm", "8" }, { "fs", "0" },
            { "to", "0" }, { "stl", "0" },
            { "s0", minion("BG_CFM_315", false, 1, 1, 0, 1) },
            { "s1", minion("BG19_010", false, 2, 3, 0, 8) },
            { "v0", minion("BG_CFM_315", false, 1, 1, 0, 1) },
            { "v1", "" },
            { "tr", "0" },
            { "cd", list({ "BG19_010" }) },
            -- type.side.actor.target.amount.amount2.card.golden.position.keywords
            { "ev", list({ "0.0.1.8.0.0.-1.0.0.0", "3.1.1.8.0.0.0.0.1.0" }) },
        }
        local map = mut or {}
        local out = {}
        for i = 1, table.getn(kv) do
            if map[kv[i][1]] ~= nil then
                if map[kv[i][1]] ~= false then
                    table.insert(out, { kv[i][1], map[kv[i][1]] })
                end
                map[kv[i][1]] = nil
            else
                table.insert(out, kv[i])
            end
        end
        return P.KvEncode(out)
    end
    local function trimmedCombatPayload()
        return combatPayload({ tr = "1", ev = false, cd = false })
    end

    -- ------------------------------------------------------------- the tests
    local function testEncoding()
        local cases = {
            { { type = "buy", index = 2 }, "act=buy;i=2" },
            { { type = "sell", zone = "hand", index = 0 }, "act=sell;z=h;i=0" },
            { { type = "sell", zone = "board", index = 6 }, "act=sell;z=b;i=6" },
            { { type = "play", index = 1, slot = 3 }, "act=play;i=1;s=3" },
            { { type = "play", index = 1, slot = 3, uid = 105 }, "act=play;i=1;s=3;u=105" },
            { { type = "move", index = 0, slot = 2 }, "act=move;f=0;t=2" },
            { { type = "refresh" }, "act=refresh" },
            { { type = "upgrade" }, "act=upgrade" },
            { { type = "end" }, "act=end" },
            { { type = "freeze", flag = true }, "act=freeze;on=1" },
            { { type = "freeze", flag = false }, "act=freeze;on=0" },
            { { type = "discover", index = 1 }, "act=discover;i=1" },
            { { type = "hero", index = 2 }, "act=hero;i=2" },
            { { type = "quest", index = 1 }, "act=quest;i=1" },
            { { type = "power" }, "act=power" },
            { { type = "power", uid = 105 }, "act=power;u=105" },
            { { type = "power", offer = 0 }, "act=power;o=0" },
            { { type = "power", uid = 105, uid2 = 106 }, "act=power;u=105;u2=106" },
            { { type = "send", index = 1, seat = 6 }, "act=send;i=1;to=6" },
            { { type = "peek", seat = 2 }, "act=peek;to=2" },
        }
        for i = 1, table.getn(cases) do
            local got = BP.EncodeAction(cases[i][1])
            check(got == cases[i][2], "encode " .. cases[i][2] .. " -> " .. tostring(got))
            local kv = got and P.KvDecode(got)
            check(kv ~= nil and type(kv.act) == "string", "round trip " .. cases[i][2])
        end
        check(BP.EncodeAction({ type = "nonsense" }) == nil, "unknown action -> nil")
    end

    local function testParseMinions()
        local m = BP.ParseMinions("BG19_010.0.2.3.0.101,BG21_003.1.10.6.64.102", 7)
        check(m and table.getn(m) == 2, "two minions")
        check(m[1].id == "BG19_010" and m[1].golden == false and m[1].atk == 2 and m[1].hp == 3 and m[1].kw == 0
            and m[1].uid == 101, "normal minion")
        check(m[2].golden == true and m[2].atk == 10 and m[2].kw == 64, "golden minion with a keyword")
        check(table.getn(BP.ParseMinions("", 7)) == 0, "empty list")
        check(BP.ParseMinions("BG19_010.0.2.3.0", 7) == nil, "short field count rejected")
        check(BP.ParseMinions("BG19_010.2.2.3.0.1", 7) == nil, "bad golden flag rejected")
        check(BP.ParseMinions("bad id.0.2.3.0.1", 7) == nil, "bad id rejected")
        check(BP.ParseMinions("a,,b", 7) == nil, "empty item rejected")
        check(BP.ParseMinions("BG19_010.0.2.3.0.101,BG19_010.0.2.3.0.102", 1) == nil, "too many items")
    end

    local function testParseState()
        local m, why = BP.ParseState(statePayload())
        check(m, "state parses: " .. tostring(why))
        check(m.me == 0 and m.rd == 3 and m.ph == "s" and m.al == true and m.en == false, "state header")
        check(m.gold == 7 and m.tier == 2 and m.upgrade == 5 and m.health == 34 and m.cap == true, "numbers")
        check(table.getn(m.offers) == 2 and m.offers[1].id == "BG19_010", "offers")
        check(table.getn(m.hand) == 1 and table.getn(m.board) == 2 and m.board[2].golden, "hand / board")
        check(table.getn(m.tribes) == 3 and m.tribes[3] == 4, "tribes")
        check(table.getn(m.enemyTribes) == 2 and m.enemyTribes[1] == 8, "enemy tribes")
        check(m.refreshCost == 3, "refresh cost")
        check(table.getn(m.lobby) == 8 and m.lobby[1].health == 40 and m.lobby[5].health == 40, "lobby")
        check(m.winner == -1, "winner")
        -- ofz marks the Frozen offers one by one
        check(not m.offers[1].frozen and not m.offers[2].frozen, "no offer is frozen")
        check(BP.ParseState(statePayload({ ofz = false })) == nil and BP.ParseState(statePayload({ ofz = "0" })) == nil,
            "ofz is required, one digit per offer")
        local mo = BP.ParseState(statePayload({ ofz = "01" }))
        check(mo and not mo.frozen and not mo.offers[1].frozen and mo.offers[2].frozen, "ofz freezes only the second offer")
        local mb = BP.ParseState(statePayload({ fz = "1", ofz = "10" }))
        check(mb.frozen and mb.offers[1].frozen and not mb.offers[2].frozen, "ofz wins over fz per offer")

        local bad = {
            { me = "9" }, { ph = "x" }, { al = "2" }, { g = "abc" }, { t = "7" }, { hp = "abc" },
            { of = "BG19_010" }, { tb = "1,zz" }, { te = "1,zz" }, { rf = "x" }, { pl = "40" }, { win = "9" },
        }
        for i = 1, table.getn(bad) do
            local key = next(bad[i])
            local ok, reason = BP.ParseState(statePayload(bad[i]))
            check(ok == nil, "bad " .. key .. " rejected (" .. tostring(reason) .. ")")
        end
        -- a defeated enemy seat in the lobby
        local m2 = BP.ParseState(statePayload({ pl = lobby(0) }))
        check(m2 and m2.lobby[5].alive == false and m2.lobby[5].elim == 3, "eliminated seat")

        -- A seat's hero and armor, the seat names, the hero pick
        local m3 = BP.ParseState(statePayload({ pl = lobby(40, "TB_BaconShop_HERO_34"), nm = "|a|b|c|d|e|f|g" }))
        check(m3 and m3.lobby[2].hero == "TB_BaconShop_HERO_34" and m3.lobby[2].armor == 5, "seat hero and armor")
        check(table.getn(m3.names) == 8 and m3.names[1] == "" and m3.names[2] == "a", "seat names split on |")
        local m4 = BP.ParseState(statePayload({ ph = "p", hc = "TB_BaconShop_HERO_34,TB_BaconShop_HERO_57" }))
        check(m4 and m4.ph == "p" and table.getn(m4.hc) == 2 and m4.hc[1] == "TB_BaconShop_HERO_34", "hero pick state")
        local m5 = BP.ParseState(statePayload({ hero = "TB_BaconShop_HERO_34", arm = "3", pw = "TB_BaconShop_HP_035",
            pwc = "1", pwu = "1", pwt = "f", snd = "1", gift = "BG19_010,BG21_003" }))
        check(m5 and m5.hero == "TB_BaconShop_HERO_34" and m5.armor == 3 and m5.power == "TB_BaconShop_HP_035",
            "my hero, armor and power")
        -- `nd`: where a card helps an ally (uid -> { { seat, reason }, ... })
        local mn = BP.ParseState(statePayload({ nd = "103.1TR,103.2U,101.1T" }))
        check(mn and table.getn(mn.need[103]) == 2, "nd groups by uid")
        check(mn.need[103][1].seat == 1 and mn.need[103][1].reason == "TR", "nd reasons are kept")
        check(mn.need[103][2].seat == 2 and mn.need[103][2].reason == "U", "nd second ally")
        check(mn.need[101][1].seat == 1 and mn.need[101][1].reason == "T", "nd second card")
        check(BP.ParseState(statePayload({ nd = "103.8T" })) == nil, "a bad nd seat is rejected")
        check(BP.ParseState(statePayload({ nd = "103.1" })) == nil, "nd without a reason is rejected")
        check(BP.ParseState(statePayload({ nd = "x.1T" })) == nil, "a bad nd uid is rejected")
        -- rk: the server's live placing per seat (optional)
        local rkm = BP.ParseState(statePayload({ rk = "2,1,3,4,5,6,7,8" }))
        check(rkm and rkm.rank[1] == 2 and rkm.rank[2] == 1, "rk parsed per seat")
        check(BP.ParseState(statePayload({ rk = "1,2,3" })) == nil, "a short rk is rejected")
        check(BP.ParseState(statePayload({ rk = "1,2,3,4,5,6,7,9" })) == nil, "an rk placing over 8 is rejected")
        -- svh / svb: the sell price of each hand / board card (optional keys)
        local sv = BP.ParseState(statePayload({ svh = "3", svb = "1,6" }))
        check(sv and sv.hand[1].sell == 3 and sv.board[1].sell == 1 and sv.board[2].sell == 6, "sell prices per card")
        check(BP.ParseState(statePayload({ svh = "1,2" })) == nil, "a sell list longer than the hand is rejected")
        check(BP.ParseState(statePayload({ svb = "x" })) == nil, "a bad sell price is rejected")
        check(BG.SellValue({ id = "BG_CFM_315" }) == 1, "a minion sells for 1 by default")
        check(BG.SellValue({ id = "BG20_GEM" }) == 0, "a spell sells for 0")
        check(BG.SellValue({ id = "BGS_049", sell = 3 }) == 3, "the wire price wins")
        check(m5.powerCost == 1 and m5.powerUsable == true and m5.powerTarget == "f" and m5.sendUsed == true,
            "power and send flags")
        check(table.getn(m5.gifts) == 2 and m5.gifts[2] == "BG21_003", "gifts")
        local mw = BP.ParseState(statePayload({ whl = "35" }))
        check(mw and table.getn(mw.wheel) == 2 and mw.wheel[1] == 3 and mw.wheel[2] == 5, "wheel spins")
        local mw0 = BP.ParseState(statePayload({}))
        check(mw0 and table.getn(mw0.wheel) == 0, "no wheel spins")
        check(BP.ParseState(statePayload({ whl = "6" })) == nil, "a wheel outcome above 5 is refused")
        check(BP.ParseState(statePayload({ ph = "p", hc = "bad id" })) == nil, "bad hero choice rejected")
        check(BP.ParseState(statePayload({ pwt = "x" })) == nil, "bad power target rejected")
        check(BP.ParseState(statePayload({ pwt = "s" })).powerTarget == "s", "the seat power target")
        check(BP.ParseState(statePayload({ pwt = "m" })).powerTarget == "m", "the two-minion power target")
        check(BP.ParseState(statePayload({ nm = "only|seven|names|here|and|no|more" })) == nil, "short nm rejected")
        local m6 = BP.ParseState(statePayload({ nm = "|" .. string.rep("x", 70) .. "|b|c|d|e|f|g" }))
        check(m6 and m6.names[2] == string.rep("x", 64), "a too long seat name is cut, not refused")

        -- A hero-power choice instead of minions (opw), and the two kinds cannot be open together
        local mp = BP.ParseState(statePayload({ opw = "BG20_HERO_283p_t1,BG22_HERO_000p_t4" }))
        check(mp and table.getn(mp.powerOptions) == 2 and mp.powerOptions[2] == "BG22_HERO_000p_t4", "power choice")
        check(table.getn(mp.options) == 0, "no minion options beside the powers")
        check(BP.ParseState(statePayload({ opw = "bad id" })) == nil, "bad power option rejected")
        check(BP.ParseState(statePayload({ op = minion("BG19_010", false, 2, 3, 0, 101),
            opw = "BG20_HERO_283p_t1" })) == nil, "two kinds of options rejected")

        -- The quest offer, my quest with its progress and goal, my reward and a seat's quest/reward
        local m7 = BP.ParseState(statePayload({ qo = "BG24_Quest_112:BG24_Reward_107:0:0:0|BG24_Quest_114:BG24_Reward_109:0:0:0"
            .. "|BG24_Quest_120:BG24_Reward_111:0:0:0" }))
        check(m7 and table.getn(m7.questOffer) == 3, "three quest offers")
        check(m7.questOffer[1].quest == "BG24_Quest_112" and m7.questOffer[1].reward == "BG24_Reward_107", "first pair")
        check(m7.questOffer[3].quest == "BG24_Quest_120" and m7.questOffer[3].reward == "BG24_Reward_111", "third pair")
        check(table.getn(BP.ParseState(statePayload()).questOffer) == 0, "no offer by default")
        local m8 = BP.ParseState(statePayload({ q = "BG24_Quest_112", qp = "4", qr = "10" }))
        check(m8 and m8.quest == "BG24_Quest_112" and m8.questProgress == 4 and m8.questRequired == 10
            and m8.reward == "", "my quest and progress")
        local m9 = BP.ParseState(statePayload({ q = "BG24_Quest_112", qp = "10", qr = "10", rw = "BG24_Reward_107" }))
        check(m9 and m9.reward == "BG24_Reward_107", "my active reward")
        check(BP.ParseState(statePayload({ qo = "BG24_Quest_112" })) == nil, "a pair without a reward is rejected")
        check(BP.ParseState(statePayload({ qo = "BG24_Quest_112:BG24_Reward_107:0:0:0|bad id:BG24_Reward_109" })) == nil,
            "a bad quest id in the offer is rejected")
        check(BP.ParseState(statePayload({ q = "bad id" })) == nil, "a bad taken quest id is rejected")
        check(BP.ParseState(statePayload({ qp = "x" })) == nil, "a bad progress is rejected")
        check(BP.ParseState(statePayload({ qr = "-1" })) == nil, "a negative goal is rejected")
        local m10 = BP.ParseState(statePayload({ pl = lobby(40, "TB_BaconShop_HERO_34", "BG24_Quest_112",
            "BG24_Reward_107") }))
        check(m10 and m10.lobby[2].quest == "BG24_Quest_112" and m10.lobby[2].reward == "BG24_Reward_107",
            "a seat's quest and reward")
        check(m10.lobby[1].quest == "" and m10.lobby[1].reward == "", "an empty seat quest")

        -- the types a pair names: tribe bits after the two ids (Unlikely Duo's two, The Friends' one)
        local m11 = BP.ParseState(statePayload({ qo = "BG24_Quest_151:BG24_Reward_134:1:16:32"
            .. "|BG24_Quest_112:BG24_Reward_107:0:0:0|BG24_Quest_114:BG24_Reward_109:0:0:0" }))
        check(m11 and m11.questOffer[1].typeA == 1 and m11.questOffer[1].typeB == 16
            and m11.questOffer[1].rewardType == 32, "a pair's types")
        check(m11.questOffer[3].typeA == 0 and m11.questOffer[3].rewardType == 0, "a pair with 0 types")
        check(BP.ParseState(statePayload({ qo = "BG24_Quest_112:BG24_Reward_107" })) == nil, "a pair without types is rejected")
        check(BP.ParseState(statePayload({ qo = "BG24_Quest_151:BG24_Reward_134:1:16" })) == nil,
            "a pair with two of three types is rejected")
        check(BP.ParseState(statePayload({ qo = "BG24_Quest_151:BG24_Reward_134:1:x:0" })) == nil,
            "a bad type is rejected")
        local m12 = BP.ParseState(statePayload({ q = "BG24_Quest_151", qa = "1", qb = "16", rt = "32" }))
        check(m12 and m12.questTypeA == 1 and m12.questTypeB == 16 and m12.rewardType == 32, "my quest's types")
        local m13 = BP.ParseState(statePayload())
        check(m13.questTypeA == 0 and m13.questTypeB == 0 and m13.rewardType == 0, "no types by default")
        check(BP.ParseState(statePayload({ qa = "-1" })) == nil, "a bad quest type is rejected")
    end

    local function testParseAlly()
        local payload = P.KvEncode({
            { "se", "1" }, { "nm", "Ally" }, { "hero", "TB_BaconShop_HERO_34" }, { "t", "4" }, { "g", "8" },
            { "hp", "30" }, { "arm", "2" }, { "bd", minion("BG19_010", false, 2, 3, 0, 201) }, { "hd", "" },
            { "pw", "TB_BaconShop_HP_035" }, { "pwc", "1" }, { "pwx", "0" },
        })
        local a, why = BP.ParseAlly(payload)
        check(a, "ally view parses: " .. tostring(why))
        check(a.seat == 1 and a.name == "Ally" and a.hero == "TB_BaconShop_HERO_34", "ally header")
        check(a.tier == 4 and a.gold == 8 and a.health == 30 and a.armor == 2, "ally numbers")
        check(table.getn(a.board) == 1 and a.board[1].uid == 201 and table.getn(a.hand) == 0, "ally board and hand")
        check(a.power == "TB_BaconShop_HP_035" and a.powerCost == 1, "ally power")
        check(BP.ParseAlly("junk") == nil, "junk ally view rejected")
    end

    local function testParseCombat()
        local c, why = BP.ParseCombat(combatPayload())
        check(c, "combat parses: " .. tostring(why))
        check(c.round == 2 and c.opponent == 5 and c.winner == 0 and c.damage == 8 and c.first == 0
            and c.timedOut == false and c.trimmed == false, "combat header")
        check(table.getn(c.start[1]) == 1 and table.getn(c.start[2]) == 1, "start boards")
        check(table.getn(c.survivors[1]) == 1 and table.getn(c.survivors[2]) == 0, "survivors")
        check(table.getn(c.cards) == 1 and c.cards[1] == "BG19_010", "card table")
        check(table.getn(c.events) == 2, "events")
        check(c.events[1].type == 0 and c.events[1].actor == 1 and c.events[1].target == 8 and c.events[1].card == nil,
            "attack event")
        check(c.events[2].type == 3 and c.events[2].card == "BG19_010" and c.events[2].position == 1, "death event")
        local t, why2 = BP.ParseCombat(trimmedCombatPayload())
        check(t and t.trimmed == true and table.getn(t.events) == 0 and table.getn(t.cards) == 0, "trimmed log")
        check(BP.ParseCombat("rd=2;ms=0;opp=5;w=0;dm=8") == nil, "incomplete combat rejected")
        check(BP.ParseCombat("junk") == nil, "junk combat rejected")
    end

    local function testParseEnd()
        local payload = P.KvEncode({
            { "win", "1" }, { "myf", "0" }, { "won", "0" }, { "pf", "3" }, { "po", "5" }, { "al", "0" },
            { "rd", "12" }, { "tm", "0" }, { "rc", "0" }, { "rp", "0" }, { "pl", list({ "8", "7", "6", "5", "4", "3", "2", "1" }) },
        })
        local e, why = BP.ParseEnd(payload)
        check(e, "end parses: " .. tostring(why))
        check(e.winner == 1 and e.myFaction == 0 and e.won == false and e.placement == 3 and e.overall == 5
            and e.alive == false and e.round == 12, "end fields")
        check(table.getn(e.order) == 8 and e.order[1] == 8 and e.order[5] == 4, "placement order")
        check(e.coins == 0 and e.packs == 0, "no payout")
        check(BP.ParseEnd((string.gsub(payload, "rc=0;", ""))) == nil, "rc is required")
        local paid = BP.ParseEnd(P.KvEncode({
            { "win", "1" }, { "myf", "0" }, { "won", "1" }, { "pf", "1" }, { "po", "1" }, { "al", "1" },
            { "rd", "9" }, { "tm", "0" }, { "rc", "15" }, { "rp", "2" },
            { "pl", list({ "1", "2", "3", "4", "5", "6", "7", "8" }) },
        }))
        check(paid and paid.coins == 15 and paid.packs == 2, "the payout is read")
        check(BP.ParseEnd("win=1") == nil, "short end rejected")
    end

    -- Feeds one complete frame through the addon router (as the client would; sender nil skips the name check).
    local function feed(op, reqId, payload)
        local frames = P.ChunkPayload(op, reqId, payload)
        for i = 1, table.getn(frames) do
            H.OnAddonMessage("WRBG", frames[i], "GUILD", nil)
        end
    end
    local function lastSent(op)
        local l = ctx.T and ctx.T.sentOps(op) or {}
        return l[table.getn(l)]
    end

    local function testFlow()
        ctx.reset()
        check(BG.state == "none", "starts with no game")
        BG.Get()
        ctx.drain(1)
        local get = lastSent("BG_GET")
        check(get ~= nil, "BG_GET sent")
        check(BG.state == "loading", "loading after the request")

        -- the start push (reqId 0) opens the game
        feed("BG_STATE", "0", statePayload())
        check(BG.state == "ready" and BG.model.gold == 7 and BG.IsBusy() == false, "start push ready")
        check(BG.model.me == 0 and BG.MyFaction() == 0, "human seat")

        -- an action is queued and answered with a state carrying its reqId
        local reqId = BG.Buy(0)
        check(reqId ~= nil and BG.IsBusy(), "buy in flight")
        ctx.drain(1)
        -- a second action is refused while the first is unconfirmed
        local second, why = BG.Buy(1)
        check(second == nil and why == "busy", "one action at a time")
        local act = lastSent("BG_ACT")
        check(act and act.payload == "act=buy;i=0", "BG_ACT payload: " .. tostring(act and act.payload))
        feed("BG_STATE", act.reqId, statePayload({ g = "4" }))
        check(BG.model.gold == 4 and BG.IsBusy() == false, "reply applied and the action confirmed")

        -- a fight is pushed before the next state; the UI takes it once
        feed("BG_COMBAT", "0", combatPayload())
        local c = BG.TakeCombat()
        check(c and c.round == 2 and BG.TakeCombat() == nil, "combat taken once")

        -- the end: BG_STATE ph=o, then BG_END
        feed("BG_STATE", "0", statePayload({ ph = "o", al = "0" }))
        check(BG.Over() and BG.state == "ended" and BG.model.al == false, "game over")
        feed("BG_END", "0", P.KvEncode({ { "win", "1" }, { "myf", "0" }, { "won", "0" }, { "pf", "4" },
            { "po", "8" }, { "al", "0" }, { "rd", "12" }, { "tm", "0" }, { "rc", "0" }, { "rp", "0" },
            { "pl", list({ "8", "7", "6", "5", "4", "3", "2", "1" }) } }))
        check(BG.endResult and BG.endResult.overall == 8, "end result")

        -- BG_CLOSE folds the window and forgets the game
        feed("BG_CLOSE", "0", "")
        check(BG.state == "none" and BG.model.gold == 0 and BG.endResult == nil, "closed")
    end

    local function testErrors()
        ctx.reset()
        -- NotReady (no game) is quiet, not an error line
        local reqId = BG.Get()
        ctx.drain(1)
        feed("ERR", reqId, P.KvEncode({ { "code", "NotReady" }, { "msg", "No Battlegrounds game is open." } }))
        check(BG.state == "none" and BG.message == nil, "NotReady is quiet")

        -- a refused action shows its reason and clears the op
        feed("BG_STATE", "0", statePayload())
        local rid = BG.Buy(0)
        ctx.drain(1)
        local act = lastSent("BG_ACT")
        check(act and rid, "action sent")
        feed("ERR", act.reqId, P.KvEncode({ { "code", "Refused" }, { "msg", "no" }, { "why", "NotEnoughGold" } }))
        check(BG.IsBusy() == false and BG.message == BG.WHY_TEXT.NotEnoughGold, "refusal text: " .. tostring(BG.message))
    end

    local function testTimeout()
        ctx.reset()
        feed("BG_STATE", "0", statePayload())
        check(BG.state == "ready", "ready for the timeout test")
        BG.EndTurn()
        ctx.drain(1)
        check(BG.IsBusy(), "end turn in flight")
        local before = table.getn(ctx.T and ctx.T.sentOps("BG_GET") or {})
        ctx.drain(11)                                    -- past BG.TIMEOUT: the action is never resent
        check(BG.IsBusy() == false, "op cleared after the timeout")
        check(BG.message ~= nil, "the player is told the answer was lost")
        check(table.getn(ctx.T and ctx.T.sentOps("BG_ACT") or {}) == 1, "the action is not resent")
        ctx.drain(3)                                     -- past BG.RETRY_GET: a BG_GET follows
        check(table.getn(ctx.T and ctx.T.sentOps("BG_GET") or {}) > before, "the board is refetched after a lost action")
    end

    -- A server with the mode off never answers BG_GET: "loading" gives up quietly after BG.GET_TRIES lost requests.
    local function testSilentServer()
        ctx.reset()
        BG.Get()
        check(BG.state == "loading", "loading while the first BG_GET is out")
        for i = 1, 400 do
            BG.Tick(0.1)
        end
        check(BG.state == "none", "a silent server leaves no game: " .. BG.state)
        check(BG.message == nil and BG.errorText == nil, "and no error line")
        check(table.getn(ctx.T.sentOps("BG_GET")) == BG.GET_TRIES, "one BG_GET per try: "
            .. table.getn(ctx.T.sentOps("BG_GET")))

        -- a reply that finally comes still opens the game
        ctx.reset()
        BG.Get()
        for i = 1, 150 do
            BG.Tick(0.1)
        end
        check(BG.state == "loading", "still loading after one lost request")
        feed("BG_STATE", "0", statePayload())
        check(BG.state == "ready", "a late state opens the game")
        for i = 1, 400 do
            BG.Tick(0.1)
        end
        check(BG.state == "ready", "and the earlier requests do not close it")
    end

    -- A Get asked for while one is out is sent once the state arrives.
    local function testRefetch()
        ctx.reset()
        local reqId = BG.Get()
        BG.Get()
        ctx.drain(1)
        check(table.getn(ctx.T.sentOps("BG_GET")) == 1, "one BG_GET while one is out")
        feed("BG_STATE", reqId, statePayload())
        ctx.drain(1)
        check(table.getn(ctx.T.sentOps("BG_GET")) == 2, "the second Get follows the state")
    end

    -- ------------------------------------------------------------------ run
    check(BP.PREFIX == "WRBG", "WRBG prefix")
    local function testCardLabels()
        local db = HS_BgCardDB
        check(type(db) == "table" and db.count > 0, "BgCardDB loaded")
        local card = BG.Card("BG19_010")
        check(card and card.name == "Sewer Rat" and card.tier == 2, "card lookup")
        check(BG.CardName("BG19_010") == "Sewer Rat", "card name")
        check(BG.CardName("nope") == "nope", "unknown id falls back")
        local kws = BG.PlainKeywords(1 + 2 + 64)
        check(table.getn(kws) == 3 and kws[1] == "Taunt" and kws[2] == "Divine Shield" and kws[3] == "Mega-Windfury",
            "keyword bit set: " .. table.concat(kws, "/"))
        check(table.getn(BG.PlainKeywords(0)) == 0, "no keywords")
        check(BG.TribeLabel(16) == "Mech", "tribe label")
        local names = BG.CardKeywords("BG20_104")
        check(table.getn(names) == 1 and names[1] == "Windfury", "card keyword label")
        check(table.getn(BG.CardKeywords("BG20_104_G")) == 1 and BG.CardKeywords("BG20_104_G")[1] == "Windfury",
            "golden card keyword label")
        check(BG.FactionOf(0) == 0 and BG.FactionOf(7) == 1, "factions by seat")
        check(BG.FactionName(0) == "Alliance" and BG.RefusalText("BoardFull") == BG.WHY_TEXT.BoardFull, "texts")
    end

    local function testSeatLabels()
        ctx.reset()
        feed("BG_STATE", "0", statePayload({ pl = lobby(0, "TB_BaconShop_HERO_34"), nm = "|Tess|Bob|x|y|z|w|v" }))
        check(BG.SeatLabel(0) == "You", "own seat")
        check(BG.SeatLabel(2) == "Ally 3", "ally label: " .. BG.SeatLabel(2))
        check(BG.SeatLabel(5) == "Foe 2", "foe label: " .. BG.SeatLabel(5))
        check(BG.SeatIsEnemy(5) and not BG.SeatIsEnemy(2), "enemy seats")
        check(BG.SeatState(0) == "tier 1", "seat state")
        check(BG.SeatState(4) == "eliminated (round 3)", "eliminated seat state")
        -- Names, hero and armor
        check(BG.SeatName(1) == "Tess", "seat name: " .. BG.SeatName(1))
        check(BG.SeatName(0) == "You", "the human's empty name falls back to the seat label")
        check(BG.SeatHero(1) == "TB_BaconShop_HERO_34" and BG.SeatArmor(1) == 5, "seat hero and armor")
        check(string.find(BG.SeatTitle(1), "Tess", 1, true) ~= nil, "seat title carries the name")
        -- Live standings: survivors by health, then the eliminated by their round; ties by seat.
        check(BG.LivePlacement(0) == 1 and BG.LivePlacement(4) == 8, "live placing: leader and the eliminated seat")
        BG.model.lobby[7].health = 5
        check(BG.LivePlacement(6) == 7 and BG.LivePlacement(4) == 8, "a low-health survivor still ranks above the out seat")
        BG.model.lobby[5].elim = 12
        check(BG.LivePlacement(4) == 8, "a late elimination (round 12) still ranks below every survivor")
        -- two eliminated seats: the one that went out later ranks higher
        BG.model.lobby[6].alive, BG.model.lobby[6].health, BG.model.lobby[6].elim = false, -3, 5
        check(BG.LivePlacement(4) == 7 and BG.LivePlacement(5) == 8, "the later elimination ranks above the earlier one")
        -- the server's own count (rk) wins over the local one
        BG.model.rank = { 3, 1, 2, 4, 5, 6, 7, 8 }
        check(BG.LivePlacement(0) == 3 and BG.LivePlacement(1) == 1, "the server's rk wins")
        BG.model.rank = nil
        check(BG.PlaceLabel(1) == "1st" and BG.PlaceLabel(2) == "2nd" and BG.PlaceLabel(3) == "3rd"
            and BG.PlaceLabel(11) == "11th" and BG.PlaceLabel(21) == "21st", "placing labels")
    end

    -- The hero pick, the picked hero with armor and a power, the send and the gift line.
    local function test15C()
        ctx.reset()
        feed("BG_STATE", "0", statePayload({ ph = "p", hc = "TB_BaconShop_HERO_34,TB_BaconShop_HERO_57" }))
        check(BG.Picking() and BG.state == "ready", "the pick phase keeps the game ready")
        check(not BG.CanSend(1), "no sending during the pick")
        check(BG.ChooseHero(1) ~= nil, "a hero can be picked")
        ctx.drain(1)
        local hp = lastSent("BG_ACT")
        check(hp.payload == "act=hero;i=1", "hero pick payload")
        feed("BG_STATE", hp.reqId, statePayload({ ph = "s", hero = "TB_BaconShop_HERO_34", arm = "3", mhp = "40",
            pw = "TB_BaconShop_HP_010", pwc = "2", pwu = "1", pwt = "f" }))
        check(BG.MyHero() == "TB_BaconShop_HERO_34" and BG.MyArmor() == 3 and BG.MyMaxHealth() == 40, "my hero")
        check(BG.PowerUsable() and BG.PowerCost() == 2 and BG.PowerTarget() == "f", "my power")
        check(BG.CanSend(1) and not BG.CanSend(0) and not BG.CanSend(5), "only a living ally can be sent to")
        check(BG.HeroName("TB_BaconShop_HERO_34") == "Patchwerk", "hero name from CardDB")
        check(BG.PowerName("TB_BaconShop_HP_010") == "Boon of Light", "power name from CardDB")

        -- send a hand minion to an ally
        BG.Send(0, 1)
        ctx.drain(1)
        local snd = lastSent("BG_ACT")
        check(snd.payload == "act=send;i=0;to=1", "send payload")
        feed("BG_STATE", snd.reqId, statePayload({ snd = "1", gift = "BG19_010" }))
        check(BG.SendUsed() and not BG.CanSend(2), "one send a turn")
        check(BG.GiftMessage() == "An ally sent you " .. BG.CardName("BG19_010") .. ".", "gift line: "
            .. tostring(BG.GiftMessage()))
        check(BG.message ~= nil, "the window shows the gift line")

        -- the hero power with a target
        BG.UsePower(104, nil)
        ctx.drain(1)
        local pw = lastSent("BG_ACT")
        check(pw.payload == "act=power;u=104", "power payload")
        feed("BG_STATE", pw.reqId, statePayload())
        check(BG.message == nil, "the state clears the message")
    end

    -- The quest pick, the taken quest with progress and goal, the reward and the completion line.
    local function test15D()
        ctx.reset()
        feed("BG_STATE", "0", statePayload({ ph = "q",
            qo = "BG24_Quest_112:BG24_Reward_107:0:0:0|BG24_Quest_114:BG24_Reward_109:0:0:0|BG24_Quest_120:BG24_Reward_111:0:0:0" }))
        check(BG.PickingQuest() and BG.state == "ready", "the quest pick keeps the game ready")
        check(not BG.CanSend(1), "no sending during the quest pick")
        check(table.getn(BG.QuestOffers()) == 3, "three offers")
        check(BG.QuestOffer(1).quest == "BG24_Quest_114" and BG.QuestOffer(1).reward == "BG24_Reward_109",
            "an offer by its choice index")
        check(BG.QuestName("BG24_Quest_112") == "Track the Footprints", "quest name from CardDB")
        check(BG.QuestNumber("BG24_Quest_112") == 10, "quest goal number from CardDB")
        check(BG.RewardName("BG24_Reward_107") == "Snicker Snacks", "reward name from CardDB")
        check(BG.ChooseQuest(1) ~= nil, "a quest can be picked")
        ctx.drain(1)
        local pick = lastSent("BG_ACT")
        check(pick.payload == "act=quest;i=1", "quest pick payload")

        -- the pick is answered with my taken quest at progress 0
        feed("BG_STATE", pick.reqId, statePayload({ q = "BG24_Quest_114", qp = "0", qr = "14" }))
        check(BG.MyQuest() == "BG24_Quest_114" and BG.QuestProgress() == 0 and BG.QuestRequired() == 14,
            "my quest, progress and goal")
        check(BG.MyReward() == "" and not BG.QuestDone(), "no reward and not done yet")
        check(BG.QuestCompleteMessage() == nil, "no completion line before the goal")

        -- the seat's quest and reward are on the lobby
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "3", qr = "14",
            pl = lobby(40, "TB_BaconShop_HERO_34", "BG24_Quest_112", "BG24_Reward_107") }))
        check(BG.SeatQuest(1) == "BG24_Quest_112" and BG.SeatReward(1) == "BG24_Reward_107",
            "a seat's quest and reward")
        check(BG.SeatQuest(0) == "" and BG.SeatReward(0) == "", "the human's own seat carries none yet")

        -- the goal is reached: the reward is active and a chat line is printed once
        local before = table.getn(ctx.printed())
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "14", qr = "14", rw = "BG24_Reward_109" }))
        check(BG.QuestDone() and BG.MyReward() == "BG24_Reward_109", "done with the reward active")
        check(BG.message ~= nil and string.find(BG.message, "Quest complete", 1, true) ~= nil,
            "the completion line: " .. tostring(BG.message))
        check(table.getn(ctx.printed()) == before + 1, "the completion is printed as a chat line")
        local printed = ctx.printed()
        check(string.find(printed[table.getn(printed)], "Reward: ", 1, true) ~= nil
            or string.find(printed[table.getn(printed)], "reward is active", 1, true) ~= nil, "the line names the reward")

        -- the transition fires once: later states keep the quest done without another line
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "15", qr = "14", rw = "BG24_Reward_109" }))
        check(table.getn(ctx.printed()) == before + 1, "the completion prints once")

        -- a completion and an ally's gift in the same state: both reach the chat
        ctx.reset()
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "3", qr = "14" }))
        local n0 = table.getn(ctx.printed())
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_114", qp = "14", qr = "14", rw = "BG24_Reward_109",
            gift = "BG19_010" }))
        check(table.getn(ctx.printed()) == n0 + 2, "the completion and the gift are both printed")

        -- the texts name the drawn types
        check(BG.QuestText("BG24_Quest_151", 1, 16) == "|cffffd100Quest:|r Play 5 Beasts or Mechs.",
            "Unlikely Duo names its types: " .. BG.QuestText("BG24_Quest_151", 1, 16))
        check(string.find(BG.QuestText("BG24_Quest_151", 0, 0), "one of two types", 1, true) ~= nil,
            "unknown types keep the generic wording")
        check(BG.RewardText("BG24_Reward_134", 32) == "At the start of your turn, get 2 random Murlocs.",
            "The Friends names its type: " .. BG.RewardText("BG24_Reward_134", 32))
        check(BG.RewardText("BG24_Reward_107", 32) == BG.Reward("BG24_Reward_107").text, "other rewards are unchanged")
        feed("BG_STATE", "0", statePayload({ q = "BG24_Quest_151", qp = "1", qr = "5", qa = "2", qb = "4" }))
        check(BG.MyQuestText() == "|cffffd100Quest:|r Play 5 Demons or Dragons.", "my quest text with its types")
    end

    testEncoding()
    testParseMinions()
    testParseState()
    testParseAlly()
    testParseCombat()
    testParseEnd()
    testCardLabels()
    testFlow()
    testErrors()
    testTimeout()
    testSilentServer()
    testRefetch()
    testSeatLabels()
    test15C()
    test15D()
end
