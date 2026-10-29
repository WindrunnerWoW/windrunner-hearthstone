// Tests of the Dragon effect batch (src/battlegrounds/HsBgEffectsDragon.cpp).
// Included by bg_effects_tests.cpp, which provides CHECK, RUN and the helpers (see its header comment). One test per
// minion and per golden version; each is a static function called from RunDragonTests with RUN(fn).
// Only this file and HsBgEffectsDragon.cpp belong to the Dragon batch.

namespace
{
    // The real Dragon entries plus a test-only Brann, for the Battlecry multiplier interactions.
    EffectRegistry const& DragonBrannReg()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            for (char const* id : { "BGS_038", "BGS_041", "BGS_043" })
                r.Register(id, *EffectRegistry::Global().Find(FindCard(id)));
            EffectDef d;
            d.battlecryTimes = 2;
            d.battlecryTimesGolden = 3;
            r.Register("BG_LOE_077", d);
            return r;
        }();
        return reg;
    }

    // Damage amounts dealt to side 1 before the second Attack event (the first attack with all its triggers).
    int DamageInFirstAttack(CombatResult const& r)
    {
        int sum = 0;
        int attacks = 0;
        for (CombatEvent const& e : r.events)
        {
            if (e.type == EventType::Attack && ++attacks == 2)
                break;
            if (e.type == EventType::Damage && e.side == 1)
                sum += e.amount;
        }
        return sum;
    }

    // ---------------------------------------------------------------------------------------- shop

    void TestEvolvingChromawing()
    {
        Lobby L(1, 2);
        uint32_t const c = OnBoard(L, 0, "BG21_027");
        OnBoard(L, 0, "BGS_034");
        SetGold(L, 0, 40);
        CHECK(L.shop.Upgrade(0) == ActionResult::Ok);
        CHECK(Find(L, 0, c)->attack == 1 + 2);              // itself and the other Dragon
        OnBoard(L, 0, "BG_GIL_681");                         // the Amalgam counts as a Dragon
        CHECK(L.shop.Upgrade(0) == ActionResult::Ok);
        CHECK(Find(L, 0, c)->attack == 3 + 3);
        OnBoard(L, 0, "BG_EX1_534t");                        // a Beast does not
        CHECK(L.shop.Upgrade(0) == ActionResult::Ok);
        CHECK(Find(L, 0, c)->attack == 6 + 3);
    }

    void TestEvolvingChromawingGolden()
    {
        Lobby L(1, 2);
        uint32_t const c = OnBoard(L, 0, "BG21_027", true);
        OnBoard(L, 0, "BGS_034");
        SetGold(L, 0, 40);
        CHECK(L.shop.Upgrade(0) == ActionResult::Ok);
        CHECK(Find(L, 0, c)->attack == 2 + 4);               // +2 per Dragon
        CHECK(Find(L, 0, c)->health == 8);
    }

    void TestNetherDrake()
    {
        Lobby L(1, 2);
        uint32_t const n = OnBoard(L, 0, "BG24_003");
        uint32_t const w = OnBoard(L, 0, "BGS_034");
        uint32_t const b = OnBoard(L, 0, "BG_EX1_534t");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, n)->attack == 2);                   // other Dragons only (render text): not itself
        CHECK(Find(L, 0, w)->attack == 3);
        CHECK(Find(L, 0, b)->attack == 2);                   // the Hyena is no Dragon
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, n)->attack == 2 && Find(L, 0, w)->attack == 4);
    }

    void TestNetherDrakeGolden()
    {
        Lobby L(1, 2);
        uint32_t const n = OnBoard(L, 0, "BG24_003", true);
        uint32_t const w = OnBoard(L, 0, "BGS_034");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, n)->attack == 4 && Find(L, 0, w)->attack == 4);
    }

    void TestRazorgore()
    {
        Lobby L(1, 2);
        uint32_t const r = OnBoard(L, 0, "BGS_036");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, r)->attack == 5 && Find(L, 0, r)->health == 7);   // itself
        OnBoard(L, 0, "BGS_034");
        OnBoard(L, 0, "BG_GIL_681");
        OnBoard(L, 0, "BG_EX1_534t");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, r)->attack == 8 && Find(L, 0, r)->health == 10);  // three Dragons incl. the Amalgam
    }

    void TestRazorgoreGolden()
    {
        Lobby L(1, 2);
        uint32_t const r = OnBoard(L, 0, "BGS_036", true);
        OnBoard(L, 0, "BGS_034");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, r)->attack == 8 + 4 && Find(L, 0, r)->health == 12 + 4);
    }

    void TestTwilightEmissary()
    {
        Lobby L(1, 2);
        uint32_t const w = OnBoard(L, 0, "BGS_034");
        uint32_t const w2 = OnBoard(L, 0, "BGS_034");
        uint32_t const h = OnBoard(L, 0, "BG_EX1_534t");
        uint32_t const e = PlayCard(L, 0, "BGS_038", false, -1, w2);
        CHECK(Find(L, 0, w2)->attack == 4 && Find(L, 0, w2)->health == 3);   // the chosen Dragon
        CHECK(Find(L, 0, w)->attack == 2 && Find(L, 0, h)->attack == 2);
        CHECK(Find(L, 0, e)->attack == 3);                                   // never itself
        // a non-Dragon target is not valid: a random Dragon gets the buff instead
        PlayCard(L, 0, "BGS_038", false, -1, h);
        CHECK(Find(L, 0, h)->attack == 2);
        int const total = Find(L, 0, w)->attack + Find(L, 0, w2)->attack + Find(L, 0, e)->attack;
        CHECK(total == 2 + 4 + 3 + 2);
        // no other Dragon: nothing happens
        Lobby M(2, 2);
        uint32_t const lone = PlayCard(M, 0, "BGS_038");
        CHECK(Find(M, 0, lone)->attack == 3);
    }

    void TestTwilightEmissaryGolden()
    {
        Lobby L(1, 2);
        uint32_t const w = OnBoard(L, 0, "BGS_034");
        PlayCard(L, 0, "BGS_038", true, -1, w);
        CHECK(Find(L, 0, w)->attack == 2 + 4 && Find(L, 0, w)->health == 1 + 4);
    }

    void TestMurozond()
    {
        {
            Lobby L(1, 2);
            PlayCard(L, 0, "BGS_043");                       // before the first combat: nothing
            CHECK(L.S(0).hand.Size() == 0);
        }
        Lobby L(3, 2);
        Board opp;
        opp.Push(Mn("BGS_019"));
        opp.Push(Mn("BGS_034"));
        L.shop.SetLastOpponent(0, opp);
        PlayCard(L, 0, "BGS_043");
        CHECK(L.S(0).hand.Size() == 1);
        if (L.S(0).hand.Size() == 1)
        {
            Minion const& m = L.S(0).hand[0];
            CHECK((m.card == Cd("BGS_019") || m.card == Cd("BGS_034")) && !m.golden);
            CHECK(m.attack == CardAt(m.card).attack);
        }
        // empty last board: nothing
        Lobby E(3, 2);
        E.shop.SetLastOpponent(0, Board());
        PlayCard(E, 0, "BGS_043");
        CHECK(E.S(0).hand.Size() == 0);
    }

    void TestMurozondGolden()
    {
        Lobby L(3, 2);
        Board opp;
        opp.Push(Mn("BGS_019"));
        L.shop.SetLastOpponent(0, opp);
        PlayCard(L, 0, "BGS_043", true);
        CHECK(L.S(0).hand.Size() == 2);                      // a plain copy, twice (render text)
        for (int i = 0; i < L.S(0).hand.Size(); ++i)
            CHECK(L.S(0).hand[i].card == Cd("BGS_019") && !L.S(0).hand[i].golden);
    }

    void TestMurozondBrann()
    {
        Lobby L(4, 2, DragonBrannReg());
        Board opp;
        opp.Push(Mn("BGS_019"));
        opp.Push(Mn("BGS_034"));
        L.shop.SetLastOpponent(0, opp);
        OnBoard(L, 0, "BG_LOE_077");
        PlayCard(L, 0, "BGS_043");
        CHECK(L.S(0).hand.Size() == 2);                      // the Battlecry fired twice: two minions
    }

    void TestMurozondAfterCombat()
    {
        // the board of the real last combat is the one Murozond reads
        ManualGame G(5);
        for (int p = 0; p < kPlayers; ++p)
            G.S(p).board.Push(With(Mn("BGS_034", false, G.game.GetShop().NewUid()), 1, 1));
        G.EndRound();
        Minion mz = Mn("BGS_043", false, G.game.GetShop().NewUid());
        G.S(0).hand.Push(mz);
        CHECK(G.game.GetShop().Play(0, 0, 0, 0) == ActionResult::Ok);
        CHECK(G.S(0).hand.Size() == 1 && G.S(0).hand[0].card == Cd("BGS_034"));
    }

    void TestKalecgos()
    {
        Lobby L(1, 2);
        uint32_t const k = OnBoard(L, 0, "BGS_041");
        uint32_t const w = OnBoard(L, 0, "BGS_034");
        uint32_t const h = OnBoard(L, 0, "BG_EX1_534t");
        PlayCard(L, 0, "BGS_034");                           // no Battlecry: nothing
        CHECK(Find(L, 0, k)->attack == 4 && Find(L, 0, w)->attack == 2);
        uint32_t const e = PlayCard(L, 0, "BGS_038", false, -1, w);   // Battlecry minion
        CHECK(Find(L, 0, k)->attack == 5 && Find(L, 0, k)->health == 13);   // includes itself
        CHECK(Find(L, 0, w)->attack == 2 + 2 + 1 && Find(L, 0, w)->health == 1 + 2 + 1);
        CHECK(Find(L, 0, e)->attack == 4);                   // the played Dragon (3/3) is buffed too
        CHECK(Find(L, 0, h)->attack == 2);                   // a Hyena is not
    }

    void TestKalecgosGolden()
    {
        Lobby L(1, 2);
        uint32_t const k = OnBoard(L, 0, "BGS_041", true);
        PlayCard(L, 0, "BGS_038");
        CHECK(Find(L, 0, k)->attack == 8 + 2 + 2 && Find(L, 0, k)->health == 24 + 2 + 2);   // +2 of the Emissary, +2 of Kalecgos
    }

    void TestKalecgosMagneticAndBrann()
    {
        // a Magnetic merge is judged by the Magnetic card: Annoy-o-Module has no Battlecry
        Lobby L(1, 2);
        uint32_t const k = OnBoard(L, 0, "BGS_041");
        uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
        PlayCard(L, 0, "BG_BOT_911", false, -1, mech);
        CHECK(Find(L, 0, k)->attack == 4);
        // Brann doubles the Battlecry and Kalecgos triggers after each run ("After you trigger a Battlecry")
        Lobby B(1, 2, DragonBrannReg());
        uint32_t const k2 = OnBoard(B, 0, "BGS_041");
        OnBoard(B, 0, "BG_LOE_077");
        uint32_t const w = OnBoard(B, 0, "BGS_034");
        PlayCard(B, 0, "BGS_038", false, -1, w);
        CHECK(Find(B, 0, k2)->attack == 6);
        CHECK(Find(B, 0, w)->attack == 2 + 4 + 2);           // +2 twice from Brann, +1 twice from Kalecgos
    }

    // ---------------------------------------------------------------------------------------- combat

    void TestRedWhelp()
    {
        for (uint32_t seed = 1; seed <= 6; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BGS_019"), 0, 5), With(PlainOf(kTribeDragon), 0, 5) }, { Body(0, 10), Body(0, 10) }, seed);
            CHECK((Survivor(r, 1, 0).health + Survivor(r, 1, 1).health) == 20 - 2);   // 2 Dragons = 2 damage to one minion
            CHECK(Survivor(r, 1, 0).health == 8 || Survivor(r, 1, 1).health == 8);
        }
        // alone: 1 damage; the Amalgam is a Dragon
        CombatResult r = Fight({ With(Mn("BGS_019"), 0, 5) }, { Body(0, 10) });
        CHECK(Survivor(r, 1, 0).health == 9);
        r = Fight({ With(Mn("BGS_019"), 0, 5), With(Mn("BG_GIL_681"), 0, 5), With(PlainOf(kTribeDragon), 0, 5) }, { Body(0, 10) });
        CHECK(Survivor(r, 1, 0).health == 7);
        // the Hyena is no Dragon
        r = Fight({ With(Mn("BGS_019"), 0, 5), With(PlainOf(kTribeBeast), 0, 5) }, { Body(0, 10) });
        CHECK(Survivor(r, 1, 0).health == 9);
    }

    void TestRedWhelpGolden()
    {
        CombatResult r = Fight({ With(Mn("BGS_019", true), 0, 5), With(PlainOf(kTribeDragon), 0, 5) }, { Body(0, 10) });
        CHECK(Survivor(r, 1, 0).health == 10 - 4);           // twice, 2 Dragons each
        r = Fight({ With(Mn("BGS_019", true), 0, 5) }, { Body(0, 10), Body(0, 10) }, 2);
        CHECK(Survivor(r, 1, 0).health + Survivor(r, 1, 1).health == 18);
    }

    void TestGlyphGuardian()
    {
        CombatResult r = Fight({ With(Mn("BGS_045"), 2, 4) }, { Body(0, 20) });
        CHECK(r.winner == 0);
        CHECK(Survivor(r, 0, 0).attack == 16);               // 2 -> 4 -> 8 -> 16: dies on the third hit (4 + 8 + 16)
        CHECK(Triggers(r, 1, HookKind::Attack) == 3);
    }

    void TestGlyphGuardianGolden()
    {
        CombatResult r = Fight({ With(Mn("BGS_045", true), 4, 8) }, { Body(0, 20) });
        CHECK(r.winner == 0);
        CHECK(Survivor(r, 0, 0).attack == 36);               // tripled: 4 -> 12 (hits for 12) -> 36 (kills)
    }

    void TestGlyphGuardianWindfury()
    {
        // every Windfury attack triggers again
        Minion g = With(Mn("BGS_045"), 1, 4);
        g.keywords |= kKwWindfury;
        CombatResult r = Fight({ g }, { Body(0, 1000) });
        std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
        CHECK(atk.size() >= 2);
        CHECK(Triggers(r, 1, HookKind::Attack) == static_cast<int>(atk.size()));
    }

    void TestTarecgosa()
    {
        // Amber Guardian gives +3/+3 and Divine Shield to Tarecgosa; the board slot of Tarecgosa keeps all of it
        CombatResult r = Fight({ With(Mn("BG24_500"), 0, 5), Mn("BG21_015") }, { Body(0, 5) });
        int attack = 0;
        int health = 0;
        uint32_t kw = 0;
        for (PersistentChange const& c : r.persistent)
            if (c.side == 0 && c.inputIndex == 1 && c.kind == kPersistBuff)
            {
                attack += c.attack;
                health += c.health;
                kw |= c.keywords;
            }
        CHECK(attack == 3 && health == 3);
        CHECK((kw & kKwDivineShield) != 0);
        // golden Tarecgosa keeps the enchantments twice
        r = Fight({ With(Mn("BG24_500"), 0, 5), Mn("BG21_015", true) }, { Body(0, 5) });
        attack = health = 0;
        for (PersistentChange const& c : r.persistent)
            if (c.side == 0 && c.inputIndex == 1 && c.kind == kPersistBuff)
            {
                attack += c.attack;
                health += c.health;
            }
        CHECK(attack == 6 && health == 6);
        // an ordinary Dragon keeps nothing
        r = Fight({ With(Mn("BG24_500"), 0, 5), PlainOf(kTribeDragon) }, { Body(0, 5) });
        CHECK(r.persistent.empty());
    }

    void TestTarecgosaThroughGame()
    {
        ManualGame G(6);
        for (int p = 0; p < kPlayers; ++p)
        {
            G.S(p).board.Push(With(Mn("BG24_500", false, G.game.GetShop().NewUid()), 0, 5));
            G.S(p).board.Push(With(Mn("BG21_015", false, G.game.GetShop().NewUid()), 1, 50));
        }
        G.EndRound();
        for (int p = 0; p < kPlayers; ++p)
        {
            CHECK(G.S(p).board.Size() >= 2);
            if (G.S(p).board.Size() >= 2)
            {
                Minion const& t = G.S(p).board[1];
                CHECK(t.card == Cd("BG21_015") && t.attack == 4 && t.maxHealth == 50 + 3 && t.Has(kKwDivineShield));
            }
        }
    }

    void TestAmberGuardian()
    {
        CombatResult r = Fight({ With(Mn("BG24_500"), 0, 5), With(PlainOf(kTribeDragon), 0, 5), With(PlainOf(kTribeBeast), 0, 5) }, { Body(0, 5) });
        CHECK(Survivor(r, 0, 0).attack == 0 && !Survivor(r, 0, 0).Has(kKwDivineShield));          // not itself
        CHECK(Survivor(r, 0, 1).attack == 3 && Survivor(r, 0, 1).health == 8 && Survivor(r, 0, 1).Has(kKwDivineShield));
        CHECK(Survivor(r, 0, 2).attack == 0 && Survivor(r, 0, 2).health == 5);                   // not the Beast
        // no other Dragon: nothing
        r = Fight({ With(Mn("BG24_500"), 0, 5), With(PlainOf(kTribeBeast), 0, 5) }, { Body(0, 5) });
        CHECK(Survivor(r, 0, 1).attack == 0 && Survivor(r, 0, 1).health == 5);
        // the Amalgam is a valid Dragon target
        r = Fight({ With(Mn("BG24_500"), 0, 5), With(Mn("BG_GIL_681"), 0, 5) }, { Body(0, 5) });
        CHECK(Survivor(r, 0, 1).attack == 3);
    }

    void TestAmberGuardianGolden()
    {
        // two other Dragons get +3/+3 and Divine Shield (render text)
        CombatResult r = Fight({ With(Mn("BG24_500", true), 0, 5), With(PlainOf(kTribeDragon), 0, 5), With(PlainOf(kTribeDragon), 0, 5) },
                               { Body(0, 5) });
        for (int i = 1; i <= 2; ++i)
            CHECK(Survivor(r, 0, i).attack == 3 && Survivor(r, 0, i).health == 8 && Survivor(r, 0, i).Has(kKwDivineShield));
        CHECK(Survivor(r, 0, 0).attack == 0);
    }

    void TestPromoDrake()
    {
        // 2 Dragons (itself and the one on the far right): both neighbours +2/+2
        CombatResult r = Fight({ Body(0, 5), With(Mn("BG21_014"), 0, 5), Body(0, 5), Body(0, 5), PlainOf(kTribeDragon) }, { Body(0, 5) });
        CHECK(Survivor(r, 0, 0).attack == 2 && Survivor(r, 0, 0).health == 7);
        CHECK(Survivor(r, 0, 2).attack == 2 && Survivor(r, 0, 2).health == 7);
        CHECK(Survivor(r, 0, 3).attack == 0 && Survivor(r, 0, 3).health == 5);   // not adjacent
        // at the edge only one neighbour; the Amalgam counts
        r = Fight({ With(Mn("BG21_014"), 0, 5), Body(0, 5), With(Mn("BG_GIL_681"), 0, 5) }, { Body(0, 5) });
        CHECK(Survivor(r, 0, 1).attack == 2 && Survivor(r, 0, 1).health == 7);
        CHECK(Survivor(r, 0, 2).attack == 0);
    }

    void TestPromoDrakeGolden()
    {
        CombatResult r = Fight({ Body(0, 5), With(Mn("BG21_014", true), 0, 5), PlainOf(kTribeDragon) }, { Body(0, 5) });
        CHECK(Survivor(r, 0, 0).attack == 4 && Survivor(r, 0, 0).health == 9);   // +2/+2 per Dragon, 2 Dragons
        CHECK(Survivor(r, 0, 2).attack == 2 + 4);                 // the plain Dragon has 2 Attack
    }

    void TestAtramedes()
    {
        for (uint32_t seed = 1; seed <= 6; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG23_362"), 1, 100) }, { Body(0, 30), Body(0, 30), Body(0, 30) }, seed);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            CHECK(!atk.empty());
            if (atk.empty())
                continue;
            int const slot = static_cast<int>(atk[0].target) - 8;
            int const hit = 1 + (slot > 0 ? 1 : 0) + (slot < 2 ? 1 : 0);
            CHECK(DamageInFirstAttack(r) == 3 * hit + 1);    // 3 to the target and each neighbour, then the 1 of the attack
        }
        // killing the only target cancels the attack: no damage back, the Atramedes is untouched
        CombatResult r = Fight({ With(Mn("BG23_362"), 1, 4), Body(0, 50) }, { Body(5, 3) });   // more minions: attacks first
        CHECK(r.winner == 0 && Survivor(r, 0, 0).health == 4);
    }

    void TestAtramedesGolden()
    {
        for (uint32_t seed = 1; seed <= 4; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG23_362", true), 2, 100) }, { Body(0, 60), Body(0, 60), Body(0, 60) }, seed);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            if (atk.empty())
            {
                CHECK(false);
                continue;
            }
            int const slot = static_cast<int>(atk[0].target) - 8;
            int const hit = 1 + (slot > 0 ? 1 : 0) + (slot < 2 ? 1 : 0);
            CHECK(DamageInFirstAttack(r) == 6 * hit + 2);    // the effect runs twice
        }
    }

    void TestDrakonidEnforcer()
    {
        // a shielded Taunt takes the first hit; the Enforcer gains +2/+2
        CombatResult r = Fight({ With(Mn("BGS_067"), 3, 7), Body(0, 5, kKwDivineShield | kKwTaunt) }, { Body(1, 30) });
        CHECK(Survivor(r, 0, 0).attack == 5 && Survivor(r, 0, 0).maxHealth == 9);
        // its own Divine Shield counts too
        Minion e = With(Mn("BGS_067"), 3, 7);
        e.keywords |= kKwDivineShield | kKwTaunt;
        r = Fight({ e }, { Body(1, 30) });
        CHECK(Survivor(r, 0, 0).attack == 5);
        // an enemy shield does not
        r = Fight({ With(Mn("BGS_067"), 3, 7) }, { Body(1, 9, kKwDivineShield) });
        CHECK(Survivor(r, 0, 0).attack == 3);
    }

    void TestDrakonidEnforcerGolden()
    {
        CombatResult r = Fight({ With(Mn("BGS_067", true), 6, 14), Body(0, 5, kKwDivineShield | kKwTaunt) }, { Body(1, 30) });
        CHECK(Survivor(r, 0, 0).attack == 10 && Survivor(r, 0, 0).maxHealth == 18);
    }

    void TestBronzeWardenIsKeywordsOnly()
    {
        CHECK(EffectRegistry::Global().Find(Cd("BGS_034")) == nullptr);
        Minion w = Mn("BGS_034");
        CHECK(w.Has(kKwDivineShield) && w.Has(kKwReborn) && w.IsTribe(kTribeDragon));
    }

    void TestRegistered()
    {
        for (char const* id : { "BG21_027", "BGS_019", "BG24_003", "BGS_045", "BG21_015", "BG24_500", "BGS_038", "BG21_014",
                                "BG23_362", "BGS_067", "BGS_036", "BGS_043", "BGS_041" })
            CHECK(EffectRegistry::Global().Find(Cd(id)) != nullptr);
    }
}

static void RunDragonTests()
{
    RUN(TestRegistered);
    RUN(TestBronzeWardenIsKeywordsOnly);
    RUN(TestEvolvingChromawing);
    RUN(TestEvolvingChromawingGolden);
    RUN(TestRedWhelp);
    RUN(TestRedWhelpGolden);
    RUN(TestNetherDrake);
    RUN(TestNetherDrakeGolden);
    RUN(TestGlyphGuardian);
    RUN(TestGlyphGuardianGolden);
    RUN(TestGlyphGuardianWindfury);
    RUN(TestTarecgosa);
    RUN(TestTarecgosaThroughGame);
    RUN(TestAmberGuardian);
    RUN(TestAmberGuardianGolden);
    RUN(TestTwilightEmissary);
    RUN(TestTwilightEmissaryGolden);
    RUN(TestPromoDrake);
    RUN(TestPromoDrakeGolden);
    RUN(TestAtramedes);
    RUN(TestAtramedesGolden);
    RUN(TestDrakonidEnforcer);
    RUN(TestDrakonidEnforcerGolden);
    RUN(TestRazorgore);
    RUN(TestRazorgoreGolden);
    RUN(TestMurozond);
    RUN(TestMurozondGolden);
    RUN(TestMurozondBrann);
    RUN(TestMurozondAfterCombat);
    RUN(TestKalecgos);
    RUN(TestKalecgosGolden);
    RUN(TestKalecgosMagneticAndBrann);
}
