// Tests of the Pirate batch (src/battlegrounds/HsBgEffectsPirate.cpp).
// Included by bg_effects_tests.cpp; uses its helpers (Lobby, PlayCard, OnBoard, Fight, ...).

namespace
{
    void DrSummonPirate(CombatCtx& c, bool golden) { c.Summon(Cd("BGS_061t"), golden); }

    void TestSouthseaCaptain()
    {
        Lobby L(1, 2);
        uint32_t const captain = OnBoard(L, 0, "BG_NEW1_027");   // 3/3
        uint32_t const pirate = OnBoard(L, 0, "BGS_061");        // 3/1
        Minion const* p = Find(L, 0, pirate);
        CHECK(p && p->attack == 4 && p->health == 2);            // the aura adds +1/+1
        Minion const* c = Find(L, 0, captain);
        CHECK(c && c->attack == 3 && c->health == 3);            // not itself

        Lobby G(2, 2);
        OnBoard(G, 0, "BG_NEW1_027", true);                      // golden: +2/+2
        uint32_t const p2 = OnBoard(G, 0, "BGS_061");
        Minion const* g = Find(G, 0, p2);
        CHECK(g && g->attack == 5 && g->health == 3);
    }

    void TestSaltyLooter()
    {
        Lobby L(3, 2);
        uint32_t const salty = OnBoard(L, 0, "BGS_081");         // 5/5
        PlayCard(L, 0, "BGS_061t");                              // the first Pirate summoned
        Minion const* s = Find(L, 0, salty);
        CHECK(s && s->attack == 6 && s->health == 6);
        PlayCard(L, 0, "BGS_061t");
        s = Find(L, 0, salty);
        CHECK(s && s->attack == 7 && s->health == 7);

        Lobby G(4, 2);
        uint32_t const golden = OnBoard(G, 0, "BGS_081", true);  // 10/10, +2/+2 per Pirate
        PlayCard(G, 0, "BGS_061t");
        Minion const* g = Find(G, 0, golden);
        CHECK(g && g->attack == 12 && g->health == 12);

        // A Salty Looter played after the Pirates already counts them.
        Lobby B(5, 2);
        PlayCard(B, 0, "BGS_061t");
        PlayCard(B, 0, "BGS_061t");
        uint32_t const late = PlayCard(B, 0, "BGS_081");
        Minion const* l = Find(B, 0, late);
        CHECK(l && l->attack == 8 && l->health == 8);   // two earlier Pirates and itself

        // A bought Salty Looter keeps its count once, not twice (offer bonus -> counters, not baked in as well).
        Lobby O(8, 2);
        PlayCard(O, 0, "BGS_061t");
        PlayCard(O, 0, "BGS_061t");
        NewOffers(O, 0, { "BGS_081" });
        CHECK(O.S(0).offers[0].attack == 7 && O.S(0).offers[0].health == 7);
        SetGold(O, 0, 10);
        CHECK(O.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(O.S(0).hand.Size() >= 1 && O.S(0).hand[O.S(0).hand.Size() - 1].attack == 7
              && O.S(0).hand[O.S(0).hand.Size() - 1].health == 7);

        // Three copies make a Golden that counts the Pirates once, at the golden rate (no carried-over bonuses).
        Lobby T(7, 2);
        PlayCard(T, 0, "BGS_061t");
        PlayCard(T, 0, "BGS_061t");
        PlayCard(T, 0, "BGS_081");
        PlayCard(T, 0, "BGS_081");
        PlayCard(T, 0, "BGS_081");
        CHECK(T.S(0).board.Empty() || T.S(0).board.Size() <= 2);
        Minion const* tg = nullptr;
        for (Minion const& m : T.S(0).hand.Items())
            if (m.golden && m.card == Cd("BGS_081"))
                tg = &m;
        for (Minion const& m : T.S(0).board.Items())
            if (m.golden && m.card == Cd("BGS_081"))
                tg = &m;
        CHECK(tg && tg->attack == 10 + 2 * 5 && tg->health == 10 + 2 * 5);   // 5 Pirates summoned in all
    }

    void TestFirstMatePip()
    {
        Lobby L(6, 2);
        OnBoard(L, 0, "BG23_192");
        CHECK(L.S(0).hand.Empty());
        OnBoard(L, 0, "BG23_192");                               // two copies already make it Golden
        CHECK(L.S(0).board.Empty());
        CHECK(L.S(0).hand.Size() == 1 && L.S(0).hand[0].golden);
        CHECK(L.S(0).hand[0].card == Cd("BG23_192"));
    }

    void TestFreedealingGambler()
    {
        Lobby L(7, 2);
        OnBoard(L, 0, "BGS_049");
        SetGold(L, 0, 10);
        CHECK(L.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(L.S(0).gold == 13);                                // sells for 3, not 1 + 3

        Lobby G(8, 2);
        OnBoard(G, 0, "BGS_049", true);
        SetGold(G, 0, 10);
        CHECK(G.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(G.S(0).gold == 16);                                // golden: 6
    }

    void TestDeckSwabbie()
    {
        Lobby L(9, 2);
        int const before = L.S(0).upgradeCost;
        PlayCard(L, 0, "BGS_055");
        CHECK(L.S(0).upgradeCost == before - 1);
        Lobby G(10, 2);
        int const beforeG = G.S(0).upgradeCost;
        PlayCard(G, 0, "BGS_055", true);
        CHECK(G.S(0).upgradeCost == beforeG - 2);
    }

    void TestSouthseaStrongarm()
    {
        Lobby L(11, 2);
        uint32_t const pirate = OnBoard(L, 0, "BGS_061");        // 3/1
        L.S(0).piratesBoughtThisTurn = 2;
        PlayCard(L, 0, "BGS_048", false, -1, pirate);            // +1/+1, repeated 1 + 2 times
        Minion const* p = Find(L, 0, pirate);
        CHECK(p && p->attack == 6 && p->health == 4);            // 3/1 + 3/3

        Lobby G(12, 2);
        uint32_t const gp = OnBoard(G, 0, "BGS_061");
        G.S(0).piratesBoughtThisTurn = 1;
        PlayCard(G, 0, "BGS_048", true, -1, gp);                 // golden: +2/+2, twice
        Minion const* q = Find(G, 0, gp);
        CHECK(q && q->attack == 7 && q->health == 5);
    }

    void TestPeggy()
    {
        Lobby L(13, 2);
        uint32_t const peggy = OnBoard(L, 0, "BG21_016");        // 6/5
        uint32_t const pirate = OnBoard(L, 0, "BGS_061");        // 3/1
        CHECK(L.shop.AddToHand(0, Cd("BGS_061t"), false) != 0);
        Minion const* p = Find(L, 0, pirate);
        CHECK(p && p->attack == 4 && p->health == 2);            // another friendly Pirate
        Minion const* self = Find(L, 0, peggy);
        CHECK(self && self->attack == 6 && self->health == 5);   // not Peggy itself
    }

    void TestHoggarr()
    {
        Lobby L(14, 2);
        OnBoard(L, 0, "BGS_072");                                // Cap'n Hoggarr
        SetGold(L, 0, 10);
        NewOffers(L, 0, { "BGS_061" });
        CHECK(L.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(L.S(0).gold == 8);                                 // 10 - 3 + 1

        Lobby G(15, 2);
        OnBoard(G, 0, "BGS_072", true);                          // golden: +2
        SetGold(G, 0, 10);
        NewOffers(G, 0, { "BGS_061" });
        CHECK(G.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(G.S(0).gold == 9);
    }

    void TestGoldgrubber()
    {
        Lobby L(16, 2);
        uint32_t const grub = OnBoard(L, 0, "BGS_066");          // 4/4
        OnBoard(L, 0, "BGS_061", true);                          // a Golden minion
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        Minion const* g = Find(L, 0, grub);
        CHECK(g && g->attack == 6 && g->health == 6);            // +2/+2

        Lobby N(17, 2);
        uint32_t const plain = OnBoard(N, 0, "BGS_066");
        CHECK(N.shop.EndTurn(0) == ActionResult::Ok);
        Minion const* p = Find(N, 0, plain);
        CHECK(p && p->attack == 4 && p->health == 4);            // no Golden minion: nothing
    }

    void TestNosyLooter()
    {
        Lobby L(18, 2);
        OnBoard(L, 0, "BG21_019");
        L.shop.StartTurn(0, 1);                                  // turn 1: two turns not reached
        CHECK(L.S(0).hand.Empty());
        L.shop.StartTurn(0, 2);
        CHECK(L.S(0).hand.Size() == 1 && L.S(0).hand[0].golden);

        Lobby G(19, 2);
        OnBoard(G, 0, "BG21_019", true);                         // golden: every turn
        G.shop.StartTurn(0, 1);
        CHECK(G.S(0).hand.Size() == 1 && G.S(0).hand[0].golden);
    }

    void TestElizaAndRipsnarl()
    {
        // Side 0 has two minions, so it attacks first; Ripsnarl (a Pirate) attacks for 2 + 3.
        CombatResult const r = Fight({ Mn("BGS_056"), Mn("BGS_061") }, { Body(0, 100) }, 1);
        bool ripsnarl = false;
        for (CombatEvent const& e : Events(r, EventType::Damage))
            if (e.side == 1 && e.amount == 5)
                ripsnarl = true;
        CHECK(ripsnarl);

        // Eliza: every friendly Pirate attack gives all friendly minions +2/+1.
        CombatResult const e = Fight({ Mn("BGS_047"), Mn("BGS_061") }, { Body(0, 100) }, 1);
        bool buffed = false;
        for (CombatEvent const& ev : e.events)
            if (ev.type == EventType::Buff && ev.amount == 2 && ev.amount2 == 1)
                buffed = true;
        CHECK(buffed);
    }

    void TestYohOgre()
    {
        // Side 1 has more minions and attacks first; Yo-Ho-Ogre (5 health, Taunt) survives and attacks back.
        CombatResult const r = Fight({ Mn("BGS_060") }, { Body(1, 1), Body(0, 100) }, 1);
        bool counter = false;
        for (CombatEvent const& e : Events(r, EventType::Attack))
            if (e.side == 0)
                counter = true;
        CHECK(counter);
    }

    void TestScallywag()
    {
        CombatResult const r = Fight({ Mn("BGS_061") }, { Body(10, 10) }, 1);
        int const sky = FindCard("BGS_061t");
        uint32_t summoned = 0;
        for (CombatEvent const& e : Events(r, EventType::Summon))
            if (e.card == sky)
                summoned = e.target;
        CHECK(summoned != 0);
        bool attacked = false;
        for (CombatEvent const& e : Events(r, EventType::Attack))
            if (e.actor == summoned)
                attacked = true;
        CHECK(attacked);
    }

    void TestTonyTwoTusk()
    {
        // Four Taunt minions die; the fourth Avenge makes the other friendly Pirate Golden.
        CombatResult const r = Fight({ With(Mn("BG21_031"), 0, 100), With(Mn("BGS_061"), 0, 500),
            Body(0, 1, kKwTaunt), Body(0, 1, kKwTaunt), Body(0, 1, kKwTaunt), Body(0, 1, kKwTaunt) },
            { Body(30, 30) }, 1);
        bool golden = false;
        int index = -1;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistMakeGolden)
            {
                golden = true;
                index = c.inputIndex;
            }
        CHECK(golden);
        CHECK(index == 1);   // the other friendly Pirate (the Sky Pirate), not Tony itself
    }

    // Salty Looter counts the Pirates its owner's side summons in combat (not the opponent's).
    void TestSaltyLooterCombatSummons()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.deathrattle = &DrSummonPirate;
            r.Register("BG19_010", d);   // a test Deathrattle: summon a Pirate
            d = EffectDef();
            d.perPirateSummonedAttack = 1;
            d.perPirateSummonedHealth = 1;
            r.Register("BGS_081", d);
            return r;
        }();
        ManualGame G(11, reg);
        for (int p = 0; p < kPlayers; ++p)
        {
            // even seats: a 1/1 whose Deathrattle summons a Pirate; odd seats: a plain 1/1 (no Pirates on their side)
            G.S(p).board.Push(With(Mn(p % 2 == 0 ? "BG19_010" : "BG_CFM_315", false, G.game.GetShop().NewUid()), 1, 1));
            G.S(p).hand.Push(Mn("BGS_081", false, G.game.GetShop().NewUid()));
        }
        G.EndRound();
        int fought = 0;
        for (int p = 0; p < kPlayers; ++p)
        {
            if (G.game.GetShop().LastOpponentBoard(p) == nullptr)
                continue;
            ++fought;
            int const want = p % 2 == 0 ? 1 : 0;
            CHECK(G.S(p).piratesSummonedThisGame == want);
            Minion const* salty = nullptr;
            for (Minion const& m : G.S(p).hand.Items())
                if (m.card == Cd("BGS_081"))
                    salty = &m;
            CHECK(salty && salty->attack == 5 + want && salty->health == 5 + want);   // the bonus is applied after the fight
        }
        CHECK(fought > 0);
    }

    void RunPirateTests()
    {
        RUN(TestSouthseaCaptain);
        RUN(TestSaltyLooter);
        RUN(TestSaltyLooterCombatSummons);
        RUN(TestFirstMatePip);
        RUN(TestFreedealingGambler);
        RUN(TestDeckSwabbie);
        RUN(TestSouthseaStrongarm);
        RUN(TestPeggy);
        RUN(TestHoggarr);
        RUN(TestGoldgrubber);
        RUN(TestNosyLooter);
        RUN(TestElizaAndRipsnarl);
        RUN(TestYohOgre);
        RUN(TestScallywag);
        RUN(TestTonyTwoTusk);
    }
}
