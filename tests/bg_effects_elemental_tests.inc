// Tests of the Elemental batch (src/battlegrounds/HsBgEffectsElemental.cpp).
// Included by bg_effects_tests.cpp; uses its helpers (Lobby, PlayCard, OnBoard, Fight, ...).

namespace
{
    void TestSmogger()
    {
        Lobby L(1, 2);
        SetTier(L, 0, 3);
        uint32_t const droplet = OnBoard(L, 0, "BGS_115t");
        PlayCard(L, 0, "BG21_021");   // no target: the battlecry picks the friendly Elemental itself
        Minion const* d = Find(L, 0, droplet);
        CHECK(d && d->attack == 5 && d->health == 5);   // 2/2 + 3/3
        Lobby G2(2, 2);
        SetTier(G2, 0, 3);
        uint32_t const droplet2 = OnBoard(G2, 0, "BGS_115t");
        PlayCard(G2, 0, "BG21_021", true);
        Minion const* d2 = Find(G2, 0, droplet2);
        CHECK(d2 && d2->attack == 8 && d2->health == 8);   // golden: 2/2 + 6/6
    }

    void TestSellemental()
    {
        Lobby L(1, 2);
        OnBoard(L, 0, "BGS_115");
        CHECK(L.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(L.S(0).hand.Size() == 1 && CardAt(L.S(0).hand[0].card).id == std::string("BGS_115t"));
        Lobby G2(2, 2);
        OnBoard(G2, 0, "BGS_115", true);
        CHECK(G2.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(G2.S(0).hand.Size() == 2);
    }

    void TestRefreshDiscount()
    {
        Lobby L(1, 2);
        SetGold(L, 0, 10);
        PlayCard(L, 0, "BGS_116");   // Refreshing Anomaly: the next Refresh is free
        CHECK(L.shop.RefreshCost(0) == 0);
        int const gold0 = L.S(0).gold;
        CHECK(L.shop.Refresh(0) == ActionResult::Ok);
        CHECK(L.S(0).gold == gold0);
        CHECK(L.shop.RefreshCost(0) == 1);

        Lobby R(3, 2);
        SetGold(R, 0, 10);
        PlayCard(R, 0, "BG21_040");   // Recycling Wraith: its own Elemental play discounts the next Refresh
        CHECK(R.shop.RefreshCost(0) == 0);
        R.shop.Refresh(0);
        CHECK(R.shop.RefreshCost(0) == 1);

        Lobby G3(4, 2);
        SetGold(G3, 0, 10);
        PlayCard(G3, 0, "BG21_040", true);   // golden: two discounted refreshes
        while (G3.shop.DiscoverOpen(0))       // the golden play opened a Discover: close it first
            CHECK(G3.shop.Choose(0, 0) == ActionResult::Ok);
        CHECK(G3.shop.RefreshCost(0) == 0);
        CHECK(G3.shop.Refresh(0) == ActionResult::Ok);
        CHECK(G3.shop.RefreshCost(0) == 0);
        CHECK(G3.shop.Refresh(0) == ActionResult::Ok);
        CHECK(G3.shop.RefreshCost(0) == 1);
    }

    void TestPartyElemental()
    {
        Lobby L(5, 2);
        uint32_t const droplet = OnBoard(L, 0, "BGS_115t");
        PlayCard(L, 0, "BGS_120");
        Minion const* d = Find(L, 0, droplet);
        CHECK(d && d->attack == 3 && d->health == 4);   // 2/2 + 1/+2, Party Elemental plays another
        Lobby G2(6, 2);
        uint32_t const droplet2 = OnBoard(G2, 0, "BGS_115t");
        PlayCard(G2, 0, "BGS_120", true);
        Minion const* d2 = Find(G2, 0, droplet2);
        CHECK(d2 && d2->attack == 4 && d2->health == 6);   // golden +2/+4
    }

    void TestGarrAndMoltenRock()
    {
        Lobby L(7, 2);
        uint32_t const garr = OnBoard(L, 0, "BGS_124");   // 8/8, Elemental
        uint32_t const molten = OnBoard(L, 0, "BGS_127"); // 3/2
        PlayCard(L, 0, "BGS_115t");                        // Water Droplet: a third Elemental
        Minion const* g = Find(L, 0, garr);
        CHECK(g && g->health == 11);                       // +1 for each of the 3 Elementals
        Minion const* m = Find(L, 0, molten);
        CHECK(m && m->health == 3);                        // Molten Rock's own +1 Health
    }

    void TestLilRag()
    {
        Lobby L(8, 2);
        OnBoard(L, 0, "BGS_100");   // 6/6 at index 0
        PlayCard(L, 0, "BGS_115t"); // the Elemental's Tier is 1: one friendly minion gets +1/+1
        int boosted = 0;
        for (Minion const& m : L.S(0).board.Items())
            if ((m.card == Cd("BGS_100") && m.attack == 7 && m.health == 7) ||
                (m.card == Cd("BGS_115t") && m.attack == 3 && m.health == 3))
                ++boosted;
        CHECK(boosted == 1);   // exactly one of the two friendly minions was picked
    }

    void TestDjinni()
    {
        CombatResult const r = Fight({ Mn("BGS_121") }, { Body(8, 50) }, 3);
        CHECK(Events(r, EventType::Summon).size() >= 1);
        bool elemental = false;
        for (CombatEvent const& e : Events(r, EventType::Summon))
            if (e.card >= 0 && HasTribe(CardAt(e.card).tribes, kTribeElemental))
                elemental = true;
        CHECK(elemental);
        int added = 0;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistAddToHand)
                ++added;
        CHECK(added >= 1);
    }

    void TestStasisAndTempest()
    {
        Lobby L(9, 2);
        std::size_t const offers0 = L.S(0).offers.size();
        PlayCard(L, 0, "BGS_122");
        CHECK(L.S(0).offers.size() == offers0 + 1);   // a random Elemental joined the Tavern
        for (std::size_t i = 0; i < offers0; ++i)
            CHECK((L.S(0).offers[i].flags & kMinionFrozen) == 0);     // only the added Elemental is Frozen
        CHECK((L.S(0).offers.back().flags & kMinionFrozen) != 0);
        CHECK(HasTribe(CardAt(L.S(0).offers.back().card).tribes, kTribeElemental));

        Lobby T(10, 2);
        T.pool.AddTribeCards(0, kTribeElemental, true);   // the tavern may not roll Elementals with the new tribe count
        int const hand0 = T.S(0).hand.Size();
        PlayCard(T, 0, "BGS_123");
        CHECK(T.S(0).hand.Size() == hand0 + 1);
        CHECK(HasTribe(CardAt(T.S(0).hand[hand0].card).tribes, kTribeElemental));
    }

    void TestMasterOfRealities()
    {
        Lobby L(11, 2);
        uint32_t const master = OnBoard(L, 0, "BG21_036");   // 6/6
        uint32_t const droplet = OnBoard(L, 0, "BGS_115t");
        PlayCard(L, 0, "BG21_021", false, -1, droplet);      // Smogger buffs the droplet: the Master grows too
        Minion const* m = Find(L, 0, master);
        CHECK(m && m->attack == 7 && m->health == 7);
    }

    void TestNomiAndTavernBonus()
    {
        Lobby L(12, 2);
        OnBoard(L, 0, "BGS_104");                    // Nomi
        PlayCard(L, 0, "BGS_115t");                  // an Elemental
        CHECK(L.S(0).elementalTavernAttack == 1);
        NewOffers(L, 0, { "BGS_115t", "BG21_013" }); // an Elemental and a tribeless minion
        CHECK(L.S(0).offers[0].attack == 3);         // 2/2 + 1/+1
        CHECK(L.S(0).offers[1].auraAttack == 0);     // the tribeless offer is untouched
    }

    void TestDazzlingLightspawn()
    {
        CombatResult const r = Fight({ With(Mn("BG21_020"), 2, 100), Body(0, 1), Body(0, 1) }, { Body(10, 100) }, 4);
        bool found = false;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistElementalTavernBonus && c.value == 1)
                found = true;
        CHECK(found);
        CombatResult const g = Fight({ With(Mn("BG21_020", true), 2, 100), Body(0, 1), Body(0, 1) }, { Body(10, 100) }, 4);
        bool golden = false;
        for (PersistentChange const& c : g.persistent)
            if (c.kind == kPersistElementalTavernBonus && c.value == 2)
                golden = true;
        CHECK(golden);
    }

    void TestMajordomoTipper()
    {
        Lobby L(13, 2);
        uint32_t const maj = OnBoard(L, 0, "BGS_105");   // 6/3, left-most
        PlayCard(L, 0, "BGS_115t");
        PlayCard(L, 0, "BGS_115t");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        Minion const* m = Find(L, 0, maj);
        CHECK(m && m->attack == 9 && m->health == 6);    // 1 + two Elementals: three +1/+1

        Lobby T(14, 2);
        uint32_t const tip = OnBoard(T, 0, "BG23_352");  // 2/2
        SetGold(T, 0, 3);
        T.shop.EndTurn(0);
        Minion const* t = Find(T, 0, tip);
        CHECK(t && t->attack == 3 && t->health == 4);
        Lobby N(15, 2);
        uint32_t const tip2 = OnBoard(N, 0, "BG23_352");
        SetGold(N, 0, 0);
        N.shop.EndTurn(0);
        Minion const* t2 = Find(N, 0, tip2);
        CHECK(t2 && t2->attack == 2 && t2->health == 2);
    }

    void TestTortollanBlueShell()
    {
        Lobby L(16, 2);
        OnBoard(L, 0, "BG24_018");
        SetGold(L, 0, 10);
        L.S(0).lostLastCombat = true;
        CHECK(L.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(L.S(0).gold == 15);   // 1 base + 4 extra
        Lobby W(17, 2);
        OnBoard(W, 0, "BG24_018");
        SetGold(W, 0, 10);
        CHECK(W.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(W.S(0).gold == 11);
    }

    void TestWildfireExcess()
    {
        // Wildfire 6/3 attacks a 0/1 Taunt; the 5 excess damage hits the minion beside it.
        CombatResult const r = Fight({ With(Mn("BGS_126"), 6, 3), Body(0, 1), Body(0, 1) },
            { Body(0, 1, kKwTaunt), Body(0, 100) }, 1);
        bool excess = false;
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Damage && e.side == 1 && e.amount == 5)
                excess = true;
        CHECK(excess);
    }

    void RunElementalTests()
    {
        RUN(TestSmogger);
        RUN(TestSellemental);
        RUN(TestRefreshDiscount);
        RUN(TestPartyElemental);
        RUN(TestGarrAndMoltenRock);
        RUN(TestLilRag);
        RUN(TestDjinni);
        RUN(TestStasisAndTempest);
        RUN(TestMasterOfRealities);
        RUN(TestNomiAndTavernBonus);
        RUN(TestDazzlingLightspawn);
        RUN(TestMajordomoTipper);
        RUN(TestTortollanBlueShell);
        RUN(TestWildfireExcess);
    }
}
