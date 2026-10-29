// Effect tests (Quilboar and Blood Gems). Included by bg_effects_tests.cpp after the helpers.

namespace
{
    uint32_t GemInHand(Lobby& L, int p = 0) { return InHand(L, p, "BG20_GEM"); }

    int GemsInHand(Lobby& L, int p = 0)
    {
        int n = 0;
        for (Minion const& m : L.S(p).hand.Items())
            if (m.card == Cd("BG20_GEM"))
                ++n;
        return n;
    }

    ActionResult CastGem(Lobby& L, int p, uint32_t target)
    {
        uint32_t const gem = GemInHand(L, p);
        return L.shop.Cast(p, L.S(p).hand.IndexOfUid(gem), target);
    }

    void TestQuilboarRegistry()
    {
        EffectRegistry const& reg = EffectRegistry::Global();
        for (char const* id : { "BG20_GEM", "BG20_100", "BG20_101", "BG20_102", "BG20_103", "BG20_104", "BG20_105",
                                "BG20_106", "BG20_201", "BG20_202", "BG20_203", "BG20_204", "BG20_207", "BG20_301",
                                "BG20_302", "BG20_303", "BG21_037", "BG23_018" })
            CHECK(reg.Find(Cd(id)) != nullptr);
        EffectDef const* const agamaggan = reg.Find(Cd("BG20_205"));
        CHECK(agamaggan && agamaggan->bloodGemAttack == 1 && agamaggan->bloodGemAttackGolden == 2);
    }

    void TestQuilboarBloodGem()
    {
        {
            Lobby L(11, 2);
            uint32_t const target = OnBoard(L, 0, "BG20_100");   // 3/1, no Blood Gem hook
            CHECK(CastGem(L, 0, target) == ActionResult::Ok);
            Minion const* m = Find(L, 0, target);
            CHECK(m && m->attack == 4 && m->health == 2 && m->bloodGemAttack == 1 && m->bloodGemHealth == 1);
            L.shop.StartTurn(0, 2);
            m = Find(L, 0, target);
            CHECK(m && m->attack == 4 && m->health == 2);   // a Blood Gem is permanent
        }
        {
            Lobby L(12, 2);
            OnBoard(L, 0, "BG20_205");                           // Agamaggan +1/+1
            uint32_t const target = OnBoard(L, 0, "BG20_100");
            CHECK(CastGem(L, 0, target) == ActionResult::Ok);
            Minion const* m = Find(L, 0, target);
            CHECK(m && m->attack == 5 && m->health == 3 && m->bloodGemAttack == 2);
        }
        {
            Lobby L(13, 2);
            OnBoard(L, 0, "BG20_205", true);                     // golden Agamaggan +2/+2
            uint32_t const target = OnBoard(L, 0, "BG20_100");
            CHECK(CastGem(L, 0, target) == ActionResult::Ok);
            Minion const* m = Find(L, 0, target);
            CHECK(m && m->attack == 6 && m->health == 4);
        }
    }

    void TestQuilboarGemSources()
    {
        {
            Lobby L(14, 2);
            PlayCard(L, 0, "BG20_100");                          // Razorfen Geomancer: get a Blood Gem
            CHECK(GemsInHand(L) == 1);
        }
        {
            Lobby L(15, 2);
            PlayCard(L, 0, "BG20_100", true);                    // golden: two
            CHECK(GemsInHand(L) == 2);
        }
        {
            Lobby L(16, 2);
            uint32_t const relaxer = OnBoard(L, 0, "BG20_301"); // Sun-Bacon Relaxer: 2 gems when sold
            CHECK(L.shop.Sell(0, Zone::Board, L.S(0).board.IndexOfUid(relaxer)) == ActionResult::Ok);
            CHECK(GemsInHand(L) == 2);
        }
        {
            Lobby L(17, 2);
            OnBoard(L, 0, "BG20_203");                           // Prophet of the Boar
            PlayCard(L, 0, "BG20_103");                          // playing a Quilboar gives a gem
            CHECK(GemsInHand(L) == 1);
            PlayCard(L, 0, "BG20_301");                          // another Quilboar
            CHECK(GemsInHand(L) == 2);
        }
    }

    void TestQuilboarToughTusk()
    {
        Lobby L(18, 2);
        uint32_t const tusk = OnBoard(L, 0, "BG20_102");
        CHECK(CastGem(L, 0, tusk) == ActionResult::Ok);
        Minion const* m = Find(L, 0, tusk);
        CHECK(m && m->Has(kKwDivineShield) && m->tempKeywords == kKwDivineShield);
        L.shop.StartTurn(0, 2);
        m = Find(L, 0, tusk);
        CHECK(m && !m->Has(kKwDivineShield));
    }

    void TestQuilboarBristlebackBrute()
    {
        Lobby L(19, 2);
        uint32_t const brute = OnBoard(L, 0, "BG20_103");       // 3/3
        CHECK(CastGem(L, 0, brute) == ActionResult::Ok);
        Minion const* m = Find(L, 0, brute);
        CHECK(m && m->attack == 7 && m->health == 7);           // +1/+1 gem and an extra +3/+3
        CHECK(CastGem(L, 0, brute) == ActionResult::Ok);
        m = Find(L, 0, brute);
        CHECK(m && m->attack == 8 && m->health == 8);           // only the gem
        L.shop.StartTurn(0, 2);
        CHECK(CastGem(L, 0, brute) == ActionResult::Ok);        // a new turn: the first gem is extra again
        m = Find(L, 0, brute);
        CHECK(m && m->attack == 12 && m->health == 12);
    }

    void TestQuilboarGroundshaker()
    {
        Lobby L(20, 2);
        uint32_t const shaker = OnBoard(L, 0, "BG20_106");
        uint32_t const other = OnBoard(L, 0, "BG23_353_Gt");    // an inert body
        int const attack = Find(L, 0, other)->attack;
        CHECK(CastGem(L, 0, shaker) == ActionResult::Ok);
        CHECK(Find(L, 0, other)->attack == attack + 2 && Find(L, 0, other)->tempAttack == 2);
        L.shop.StartTurn(0, 2);
        CHECK(Find(L, 0, other)->attack == attack);             // until next turn
    }

    void TestQuilboarDynamicDuo()
    {
        Lobby L(21, 2);
        uint32_t const duo = OnBoard(L, 0, "BG20_207");         // 5/6 Taunt
        uint32_t const other = OnBoard(L, 0, "BG20_103");
        CHECK(CastGem(L, 0, other) == ActionResult::Ok);
        Minion const* m = Find(L, 0, duo);
        CHECK(m && m->attack == 6 && m->health == 7);
        // A gem on the Duo itself is not "another Quilboar".
        CHECK(CastGem(L, 0, duo) == ActionResult::Ok);
        m = Find(L, 0, duo);
        CHECK(m && m->attack == 7 && m->health == 8);
    }

    void TestQuilboarAggem()
    {
        Lobby L(22, 2);
        uint32_t const aggem = OnBoard(L, 0, "BG20_302");       // 3/5
        uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");    // 2/2 Beast
        uint32_t const dragon = OnBoard(L, 0, "BGS_034");       // 2/3 Dragon
        L.shop.Settle(0);
        CHECK(CastGem(L, 0, aggem) == ActionResult::Ok);
        CHECK(Find(L, 0, aggem)->attack == 6 && Find(L, 0, aggem)->health == 7);   // gem +2/+1 on it too
        CHECK(Find(L, 0, beast)->attack == 4 && Find(L, 0, beast)->health == 3);    // 2/2 token +2/+1
        CHECK(Find(L, 0, dragon)->attack == 4 && Find(L, 0, dragon)->health == 2);  // Bronze Warden 2/1 +2/+1
    }

    void TestQuilboarEndOfTurn()
    {
        {
            Lobby L(23, 2);
            uint32_t const left = OnBoard(L, 0, "BG20_100");
            OnBoard(L, 0, "BG20_201");                           // Bannerboar
            uint32_t const right = OnBoard(L, 0, "BG20_101");
            CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
            CHECK(Find(L, 0, left)->attack == 4 && Find(L, 0, right)->attack == 3);
            CHECK(Find(L, 0, left)->health == 2 && Find(L, 0, right)->health == 5);
        }
        {
            Lobby L(24, 2);
            uint32_t const left = OnBoard(L, 0, "BG20_100");
            uint32_t const charlga = OnBoard(L, 0, "BG20_303");
            uint32_t const right = OnBoard(L, 0, "BG20_101");
            CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
            CHECK(Find(L, 0, left)->attack == 4 && Find(L, 0, right)->attack == 3);
            CHECK(Find(L, 0, charlga)->attack == 4 && Find(L, 0, charlga)->health == 4);   // "your other minions"
        }
    }

    void TestQuilboarDarkgaze()
    {
        Lobby L(25, 2);
        uint32_t const elder = OnBoard(L, 0, "BG23_018");
        std::vector<uint32_t> boars = { elder };
        boars.push_back(OnBoard(L, 0, "BG20_100"));
        boars.push_back(OnBoard(L, 0, "BG20_101"));
        boars.push_back(OnBoard(L, 0, "BG20_104"));
        SetGold(L, 0, 10);
        for (int i = 0; i < 5; ++i)                             // 5 Gold: one 5-Gold trigger
            CHECK(L.shop.Refresh(0) == ActionResult::Ok);
        for (uint32_t uid : boars)
        {
            Minion const* m = Find(L, 0, uid);
            CHECK(m && m->bloodGemAttack == 1 && m->bloodGemHealth == 1);
        }
    }

    void TestQuilboarNecrolyte()
    {
        Lobby L(26, 2);
        uint32_t const left = OnBoard(L, 0, "BG20_100");
        uint32_t const target = OnBoard(L, 0, "BG20_101");
        uint32_t const right = OnBoard(L, 0, "BG20_104");
        CHECK(CastGem(L, 0, left) == ActionResult::Ok);
        CHECK(CastGem(L, 0, left) == ActionResult::Ok);
        CHECK(CastGem(L, 0, right) == ActionResult::Ok);
        PlayCard(L, 0, "BG20_202", false, -1, target);           // two gems, then the neighbours' gems
        Minion const* t = Find(L, 0, target);
        CHECK(t && t->bloodGemAttack == 5 && t->bloodGemHealth == 5);   // 2 + the 2 and 1 stolen
        CHECK(Find(L, 0, left)->bloodGemAttack == 0 && Find(L, 0, left)->attack == 3);
        CHECK(Find(L, 0, right)->bloodGemAttack == 0 && Find(L, 0, right)->attack == 3);
    }

    // Review fix: the neighbour keeps Attack >= 0 and Health >= 1 when its gem stats exceed its stats.
    void TestQuilboarNecrolyteClamp()
    {
        Lobby L(126, 2);
        uint32_t const left = OnBoard(L, 0, "BG20_100");   // 3/1
        uint32_t const target = OnBoard(L, 0, "BG20_101");
        Minion* l = Find(L, 0, left);
        CHECK(l != nullptr);
        if (!l)
            return;
        l->bloodGemAttack = 5;   // as after a stat-setting effect: more gem stats than stats
        l->bloodGemHealth = 5;
        PlayCard(L, 0, "BG20_202", false, -1, target);
        l = Find(L, 0, left);
        CHECK(l && l->attack == 0 && l->health == 1 && l->maxHealth == 1);
        CHECK(l && l->bloodGemAttack == 0 && l->bloodGemHealth == 0);
        Minion const* t = Find(L, 0, target);
        CHECK(t && t->bloodGemAttack == 5 && t->bloodGemHealth == 2);   // 2 + the 3 Attack actually removed
    }

    void TestQuilboarFrenzy()
    {
        {
            // Damage to ANOTHER friendly minion is no Frenzy trigger (Body hits first and takes 1; Roadboar is never hit).
            CombatResult const r = Fight({ Body(5, 10), Mn("BG20_101") }, { Body(1, 5) }, 1);
            int gems = 0;
            for (PersistentChange const& c : r.persistent)
                if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                    ++gems;
            CHECK(gems == 0);
            CombatResult const k = Fight({ Body(5, 10), Mn("BG20_204") }, { Body(1, 5) }, 1);
            for (CombatEvent const& e : Events(k, EventType::Buff))
                CHECK((e.keywords & kKwDivineShield) == 0);
        }
        {
            CombatResult const r = Fight({ Mn("BG20_101") }, { Body(1, 100) }, 1);   // Roadboar
            int gems = 0;
            for (PersistentChange const& c : r.persistent)
                if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                    ++gems;
            CHECK(gems == 1);
            CombatResult const g = Fight({ Mn("BG20_101", true) }, { Body(1, 100) }, 1);
            gems = 0;
            for (PersistentChange const& c : g.persistent)
                if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                    ++gems;
            CHECK(gems == 2);
        }
        {
            // Bristleback Knight: the shield pops, the next hit is survived and the Frenzy returns the shield.
            CombatResult const r = Fight({ Mn("BG20_204") }, { Body(1, 13) }, 1);
            bool gained = false;
            for (CombatEvent const& e : Events(r, EventType::Buff))
                if ((e.keywords & kKwDivineShield) != 0)
                    gained = true;
            CHECK(gained);
        }
    }

    void TestQuilboarBonkerAndThorncaller()
    {
        {
            CombatResult const r = Fight({ Mn("BG20_104") }, { Body(0, 5) }, 1);   // Windfury: two attacks, one round
            int gems = 0;
            for (PersistentChange const& c : r.persistent)
                if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                    ++gems;
            CHECK(gems == 2);
        }
        {
            CombatResult const g = Fight({ Mn("BG20_105") }, { Body(20, 20) }, 1);   // Thorncaller
            int gems = 0;
            for (PersistentChange const& c : g.persistent)
                if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                    ++gems;
            CHECK(gems == 1);
            CombatResult const gg = Fight({ Mn("BG20_105", true) }, { Body(20, 20) }, 1);
            gems = 0;
            for (PersistentChange const& c : gg.persistent)
                if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                    ++gems;
            CHECK(gems == 2);
        }
    }

    void TestQuilboarGemsplitter()
    {
        for (int g = 0; g < 2; ++g)
        {
            int ran = 0;
            for (uint32_t seed = 1; seed <= 12; ++seed)
            {
                CombatResult const r = Fight({ With(Mn("BG21_037", g != 0), 1, 100),
                                               With(Mn("BG21_022"), 1, 100) }, { Body(1, 6) }, seed);
                bool popped = false;
                for (CombatEvent const& e : Events(r, EventType::ShieldPop))
                    if (e.target == 2)
                        popped = true;
                if (!popped)
                    continue;
                ++ran;
                int gems = 0;
                for (PersistentChange const& c : r.persistent)
                    if (c.kind == kPersistAddToHand && c.card == Cd("BG20_GEM"))
                        ++gems;
                CHECK(gems == (g != 0 ? 2 : 1));
            }
            CHECK(ran > 0);
        }
    }

    void RunQuilboarTests()
    {
        RUN(TestQuilboarRegistry);
        RUN(TestQuilboarBloodGem);
        RUN(TestQuilboarGemSources);
        RUN(TestQuilboarToughTusk);
        RUN(TestQuilboarBristlebackBrute);
        RUN(TestQuilboarGroundshaker);
        RUN(TestQuilboarDynamicDuo);
        RUN(TestQuilboarAggem);
        RUN(TestQuilboarEndOfTurn);
        RUN(TestQuilboarDarkgaze);
        RUN(TestQuilboarNecrolyte);
        RUN(TestQuilboarNecrolyteClamp);
        RUN(TestQuilboarFrenzy);
        RUN(TestQuilboarBonkerAndThorncaller);
        RUN(TestQuilboarGemsplitter);
    }
}
