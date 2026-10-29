// Effect tests (Naga and the spell system). Included by bg_effects_tests.cpp after the helpers.

namespace
{
    uint32_t SpellInHand(Lobby& L, int p, char const* id, bool golden = false)
    {
        uint32_t const uid = InHand(L, p, id, golden);
        CHECK(CardAt(Cd(id)).role == CardRole::Spell);
        return uid;
    }

    ActionResult CastSpell(Lobby& L, int p, uint32_t uid, uint32_t target = 0)
    {
        int const idx = L.S(p).hand.IndexOfUid(uid);
        CHECK(idx >= 0);
        return L.shop.Cast(p, idx, target);
    }

    int HandOf(Lobby& L, int p, char const* id)
    {
        int n = 0;
        for (Minion const& m : L.S(p).hand.Items())
            if (m.card == Cd(id))
                ++n;
        return n;
    }

    void TestNagaSpellcraftStartTurn()
    {
        Lobby L(3, 2);
        OnBoard(L, 0, "BG23_000");                       // Mini-Myrmidon, Spellcraft: Mini-Trident
        L.shop.StartTurn(0, 1);
        CHECK(HandOf(L, 0, "BG23_000t") == 1);
        uint32_t const first = L.S(0).hand[L.S(0).hand.Size() - 1].uid;
        CHECK(L.S(0).hand[L.S(0).hand.Size() - 1].temporary);
        // Uncast, it leaves the hand at the next StartTurn; a fresh one is added.
        L.shop.StartTurn(0, 2);
        CHECK(L.S(0).hand.IndexOfUid(first) < 0);
        CHECK(HandOf(L, 0, "BG23_000t") == 1);
    }

    void TestNagaSpellCastTemporary()
    {
        {
            Lobby L(4, 2);
            uint32_t const myrmidon = OnBoard(L, 0, "BG23_000");
            Minion const* m = Find(L, 0, myrmidon);
            CHECK(m && m->attack == 3);
            L.shop.StartTurn(0, 1);
            int const idx = L.S(0).hand.Size() - 1;
            CHECK(CardAt(L.S(0).hand[idx].card).id == std::string("BG23_000t"));
            CHECK(L.shop.Cast(0, idx, myrmidon) == ActionResult::Ok);
            m = Find(L, 0, myrmidon);
            CHECK(m && m->attack == 5 && m->tempAttack == 2);
            // until next turn: the attack goes back, while the card was consumed.
            L.shop.StartTurn(0, 2);
            m = Find(L, 0, myrmidon);
            CHECK(m && m->attack == 3 && m->tempAttack == 0);
        }
        {
            // A golden source hands the golden spell (+4), which is temporary too.
            Lobby L(5, 2);
            uint32_t const myrmidon = OnBoard(L, 0, "BG23_000", true);
            L.shop.StartTurn(0, 1);
            int const idx = L.S(0).hand.Size() - 1;
            CHECK(L.S(0).hand[idx].golden);
            CHECK(L.shop.Cast(0, idx, myrmidon) == ActionResult::Ok);
            Minion const* m = Find(L, 0, myrmidon);
            CHECK(m && m->attack == 10 && m->tempAttack == 4);   // 6 base + 4
        }
    }

    void TestNagaSpellTargetAndTribes()
    {
        Lobby L(6, 2);
        uint32_t const beast = OnBoard(L, 0, "BG19_010");
        uint32_t const naga = OnBoard(L, 0, "BG23_000");
        uint32_t const banana = SpellInHand(L, 0, "TB_BaconShop_HP_038t");
        CHECK(CastSpell(L, 0, banana, naga) == ActionResult::BadTarget);   // a friendly Beast only
        CHECK(L.S(0).hand.IndexOfUid(banana) >= 0);                        // refused: still in the hand
        CHECK(CastSpell(L, 0, banana, beast) == ActionResult::Ok);
        Minion const* b = Find(L, 0, beast);
        CHECK(b && b->attack == 4 && b->health == 3);
        // Gold Coin: no target, +1 Gold.
        int const gold = L.S(0).gold;
        uint32_t const coin = SpellInHand(L, 0, "TB_BaconShop_HP_008a");
        CHECK(CastSpell(L, 0, coin) == ActionResult::Ok);
        CHECK(L.S(0).gold == gold + 1);
        CHECK(CardAt(Cd("TB_BaconShop_HP_008a")).role == CardRole::Spell);
    }

    void TestNagaLavaLurker()
    {
        Lobby L(7, 2);
        uint32_t const lurker = OnBoard(L, 0, "BG23_009");
        uint32_t const first = SpellInHand(L, 0, "BG23_000t");
        CHECK(CastSpell(L, 0, first, lurker) == ActionResult::Ok);
        Minion const* m = Find(L, 0, lurker);
        CHECK(m && m->attack == 6 && m->tempAttack == 0);   // 4 base + 2, permanent
        uint32_t const second = SpellInHand(L, 0, "BG23_000t");
        CHECK(CastSpell(L, 0, second, lurker) == ActionResult::Ok);
        m = Find(L, 0, lurker);
        CHECK(m && m->attack == 8 && m->tempAttack == 2);   // the second one is until next turn
        L.shop.StartTurn(0, 2);
        m = Find(L, 0, lurker);
        CHECK(m && m->attack == 6 && m->tempAttack == 0);
    }

    void TestNagaLavaLurkerGolden()
    {
        Lobby L(8, 2);
        uint32_t const lurker = OnBoard(L, 0, "BG23_009", true);
        int const before = Find(L, 0, lurker)->attack;
        for (int i = 0; i < 3; ++i)
        {
            uint32_t const spell = SpellInHand(L, 0, "BG23_000t");
            CHECK(CastSpell(L, 0, spell, lurker) == ActionResult::Ok);
        }
        Minion const* m = Find(L, 0, lurker);
        CHECK(m && m->tempAttack == 2);                      // the third is temporary
        CHECK(m->attack == before + 4 + 2);                  // the first two are permanent
    }

    void TestNagaSpellCastHooks()
    {
        // Snail Cavalry: once per turn, on any spell (the Beast is only the target, not a Naga).
        {
            Lobby L(9, 2);
            uint32_t const snail = OnBoard(L, 0, "BG23_001");
            uint32_t const beast = OnBoard(L, 0, "BG19_010");
            for (int i = 0; i < 2; ++i)
            {
                uint32_t const spell = SpellInHand(L, 0, "BG23_000t");
                CHECK(CastSpell(L, 0, spell, beast) == ActionResult::Ok);
            }
            CHECK(Find(L, 0, snail)->health == 5);               // 3 base + 2, once
            L.shop.StartTurn(0, 2);
            uint32_t const spell = SpellInHand(L, 0, "BG23_000t");
            CHECK(CastSpell(L, 0, spell, beast) == ActionResult::Ok);
            CHECK(Find(L, 0, snail)->health == 7);               // the mark reset at StartTurn
        }
        // Eventide Brute: +1/+1 on every spell, Spellcraft or not.
        {
            Lobby L(10, 2);
            uint32_t const eventide = OnBoard(L, 0, "BG23_010");
            for (int i = 0; i < 2; ++i)
            {
                uint32_t const spell = SpellInHand(L, 0, "TB_BaconShop_HP_008a");
                CHECK(CastSpell(L, 0, spell) == ActionResult::Ok);
            }
            CHECK(Find(L, 0, eventide)->attack == 8 && Find(L, 0, eventide)->health == 6);
        }
    }

    void TestNagaCritterWrangler()
    {
        Lobby L(10, 2);
        uint32_t const wrangler = OnBoard(L, 0, "BG23_003");
        CHECK(Find(L, 0, wrangler) != nullptr);
        uint32_t const target = OnBoard(L, 0, "BG23_000");
        uint32_t const spell = SpellInHand(L, 0, "BG23_004t");   // Angler's Lure: +4 Health and Taunt
        CHECK(CastSpell(L, 0, spell, target) == ActionResult::Ok);
        Minion const* m = Find(L, 0, target);
        CHECK(m && m->health == 10);                           // 4 base + 4 + 2 from the Wrangler
        CHECK(m && (m->keywords & kKwTaunt) != 0);
        CHECK(m && m->tempKeywords != 0);
        L.shop.StartTurn(0, 2);
        m = Find(L, 0, target);
        CHECK(m && m->health == 6 && (m->keywords & kKwTaunt) == 0);   // the +1/+2 of the Wrangler stays
    }

    void TestNagaAthissaFour()
    {
        Lobby L(11, 2);
        OnBoard(L, 0, "BG23_013");                             // Athissa, 8 Attack
        char const* const others[] = { "BG23_000", "BG23_004", "BG23_006", "BG23_007", "BG23_008" };
        for (char const* id : others)
            OnBoard(L, 0, id);                                 // five more Naga of distinct cards (no triple)
        uint32_t const spell = SpellInHand(L, 0, "BG23_000t");
        CHECK(CastSpell(L, 0, spell, L.S(0).board[0].uid) == ActionResult::Ok);
        int total = 0;
        for (Minion const& m : L.S(0).board.Items())
            total += m.attack;
        // 31 base + the spell's +2 + four Naga +1 (Athissa picks four of the six, always four).
        CHECK(total == 37);
    }

    void TestNagaShellCollector()
    {
        Lobby L(12, 2);
        SetGold(L, 0, 10);
        PlayCard(L, 0, "BG23_002");
        CHECK(HandOf(L, 0, "TB_BaconShop_HP_008a") == 1);
        uint32_t const coin = L.S(0).hand[L.S(0).hand.Size() - 1].uid;
        CHECK(CastSpell(L, 0, coin) == ActionResult::Ok);
        CHECK(HandOf(L, 0, "TB_BaconShop_HP_008a") == 0);
    }

    void TestNagaOrgozoa()
    {
        Lobby L(13, 2);
        L.pool.AddTribeCards(0, kTribeNaga, true);
        SetTier(L, 0, 6);
        OnBoard(L, 0, "BG23_015");
        L.shop.StartTurn(0, 1);
        CHECK(HandOf(L, 0, "BG23_015t") == 1);
        uint32_t const spell = L.S(0).hand[L.S(0).hand.Size() - 1].uid;
        CHECK(CastSpell(L, 0, spell) == ActionResult::Ok);
        CHECK(L.S(0).options.size() == 3);
        for (Minion const& o : L.S(0).options)
            CHECK(o.IsTribe(kTribeNaga));
        CHECK(L.shop.Choose(0, 0) == ActionResult::Ok);
        CHECK(L.S(0).options.empty());
    }

    void TestNagaPashmar()
    {
        bool found = false;
        for (uint32_t seed = 1; seed <= 20 && !found; ++seed)
        {
            std::vector<Minion> mine = { With(Mn("BG23_014"), 30, 30) };
            for (int i = 0; i < 6; ++i)
                mine.push_back(Body(1, 1));
            std::vector<Minion> theirs;
            for (int i = 0; i < 5; ++i)
                theirs.push_back(Body(10, 10));
            CombatResult const r = Fight(mine, theirs, seed, 4, 4);
            for (PersistentChange const& c : r.persistent)
                if (c.kind == kPersistAddToHand && c.card >= 0 && CardAt(c.card).role == CardRole::Spell)
                {
                    CHECK(CardAt(c.card).tier >= 0 && CardAt(c.card).tier <= 4);
                    CHECK(CardAt(c.card).id != std::string("TB_BaconShop_HP_008a"));
                    found = true;
                }
        }
        CHECK(found);
    }

    void TestNagaCorruptedMyrmidon()
    {
        CombatResult const r = Fight({ Mn("BG23_012") }, { Body(0, 100) }, 1);
        CHECK(Alive(r, 0) == 1);
        if (Alive(r, 0) == 1)
            CHECK(Survivor(r, 0, 0).attack == 10 && Survivor(r, 0, 0).health == 8);
        CombatResult const g = Fight({ Mn("BG23_012", true) }, { Body(0, 100) }, 1);
        CHECK(Alive(g, 0) == 1);
        if (Alive(g, 0) == 1)
            CHECK(Survivor(g, 0, 0).attack == 30 && Survivor(g, 0, 0).health == 24);   // triple
    }

    void TestNagaWarden()
    {
        CombatResult const r = Fight({ Mn("BGS_200") }, { Body(20, 20) }, 1);
        int coins = 0;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistAddToHand && c.card == Cd("TB_BaconShop_HP_008a"))
                ++coins;
        CHECK(coins == 1);
        CombatResult const g = Fight({ Mn("BGS_200", true) }, { Body(20, 20) }, 1);
        coins = 0;
        for (PersistentChange const& c : g.persistent)
            if (c.kind == kPersistAddToHand && c.card == Cd("TB_BaconShop_HP_008a"))
                ++coins;
        CHECK(coins == 2);
    }

    void TestNagaSpellRows()
    {
        // Every included spell is a Spell row and the tier rule of the Spellcraft cards holds.
        char const* const spells[] = { "BG23_000t", "BG23_004t", "BG23_006t", "BG23_007t", "BG23_008t", "BG23_011t",
                                       "BG23_015t", "TB_BaconShop_HP_008a", "TB_BaconShop_HP_038t" };
        for (char const* id : spells)
        {
            int const c = Cd(id);
            CHECK(c >= 0 && CardAt(c).role == CardRole::Spell);
        }
        CHECK(CardAt(Cd("BG23_000t")).tier == 1);
        CHECK(CardAt(Cd("BG23_004t")).tier == 2);
        CHECK(CardAt(Cd("BG23_011t")).tier == 3);
        CHECK(CardAt(Cd("BG23_015t")).tier == 6);
        CHECK(CardAt(Cd("TB_BaconShop_HP_008a")).tier == 0);
        CHECK(SpellcraftSpells(1).size() == 1);
        CHECK(SpellcraftSpells(6).size() == 7);
    }

    void RunNagaTests()
    {
        RUN(TestNagaSpellcraftStartTurn);
        RUN(TestNagaSpellCastTemporary);
        RUN(TestNagaSpellTargetAndTribes);
        RUN(TestNagaLavaLurker);
        RUN(TestNagaLavaLurkerGolden);
        RUN(TestNagaSpellCastHooks);
        RUN(TestNagaCritterWrangler);
        RUN(TestNagaAthissaFour);
        RUN(TestNagaShellCollector);
        RUN(TestNagaOrgozoa);
        RUN(TestNagaPashmar);
        RUN(TestNagaCorruptedMyrmidon);
        RUN(TestNagaWarden);
        RUN(TestNagaSpellRows);
    }
}
