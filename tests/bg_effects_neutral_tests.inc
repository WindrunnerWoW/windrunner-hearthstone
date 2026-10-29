// Tests of the Neutral effect batch (src/battlegrounds/HsBgEffectsNeutral.cpp).
// Included by bg_effects_tests.cpp, which provides CHECK, RUN and the helpers (see its header comment). One test per
// minion and per golden version; write each as a static function and call it from RunNeutralTests with RUN(fn).
// Only this file and HsBgEffectsNeutral.cpp belong to the Neutral batch.
//
// Combat ids: side 0 slot i = 1 + i, side 1 slot i = 8 + i, summoned minions from 15. The Buff event carries the minion
// whose effect fired (actor), the target, the Attack / Health gained and the keywords granted.

namespace
{
    // Buff events fired by `actor`, in order.
    std::vector<CombatEvent> NtBuffsBy(CombatResult const& r, uint32_t actor)
    {
        std::vector<CombatEvent> out;
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Buff && e.actor == actor)
                out.push_back(e);
        return out;
    }

    int NtDamageBy(CombatResult const& r, uint32_t actor, int amount)
    {
        int n = 0;
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Damage && e.actor == actor && e.amount == amount)
                ++n;
        return n;
    }

    Minion NtOf(char const* id, bool golden, int attack, int health)
    {
        return With(Mn(id, golden), attack, health);
    }

    bool NtHasPersistent(CombatResult const& r, int side, int inputIndex, int attack)
    {
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistBuff && c.side == side && c.inputIndex == inputIndex && c.attack == attack)
                return true;
        return false;
    }

    int NtHandCount(Lobby& L, int p, char const* id)
    {
        int n = 0;
        for (Minion const& m : L.S(p).hand.Items())
            if (m.card == Cd(id))
                ++n;
        return n;
    }

    // ------------------------------------------------------------------------------------------ registry

    void NtTestNeutralRegistry()
    {
        EffectRegistry const& reg = EffectRegistry::Global();
        char const* const ids[] = {
            "BGS_004", "BG21_013", "BG23_350", "BG_AT_069", "BG_OG_221", "BG_OG_256", "BG21_002", "BGS_002", "BGS_110",
            "BG_DAL_575", "BG_DS1_070", "BG21_007", "BG23_016", "BG23_353", "BG24_022", "BGS_083", "BG_DAL_775",
            "BG_ICC_807", "BG23_318", "BGS_009", "BGS_012", "BG_FP1_031", "BG_LOE_077", "BG22_402", "BG23_190",
            "BG24_020", "BGS_022", "BGS_040",
        };
        for (char const* id : ids)
            CHECK(reg.Find(Cd(id)) != nullptr);
        CHECK(reg.Find(Cd("BG_GIL_681")) == nullptr);       // Nightmare Amalgam: table data only
        EffectDef const* khadgar = reg.Find(Cd("BG_DAL_575"));
        EffectDef const* baron = reg.Find(Cd("BG_FP1_031"));
        EffectDef const* brann = reg.Find(Cd("BG_LOE_077"));
        CHECK(khadgar && khadgar->summonTimes == 2 && khadgar->summonTimesGolden == 3);
        CHECK(baron && baron->deathrattleTimes == 2 && baron->deathrattleTimesGolden == 3);
        CHECK(brann && brann->battlecryTimes == 2 && brann->battlecryTimesGolden == 3);
    }

    // ------------------------------------------------------------------------------------------ Brann / Baron / Khadgar

    // Brann doubles (golden: triples) Battlecries; the best Brann counts; triggered Battlecries are multiplied too.
    void NtTestBrann()
    {
        int const mult[2] = { 2, 3 };
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            OnBoard(L, 0, "BG_LOE_077", g == 1);
            uint32_t const monkey = OnBoard(L, 0, "BG23_353_Gt");               // 6/6 Taunt
            uint32_t const scav = PlayCard(L, 0, "BG_ICC_807");                 // Battlecry: Taunt minions +2/+2
            CHECK(L.shop.BattlecryTimes(0) == mult[g]);
            CHECK(Find(L, 0, monkey)->attack == 6 + 2 * mult[g] && Find(L, 0, monkey)->health == 6 + 2 * mult[g]);
            L.shop.TriggerBattlecry(0, scav, 0);
            CHECK(Find(L, 0, monkey)->attack == 6 + 4 * mult[g]);
        }
        Lobby L(2, 2);
        OnBoard(L, 0, "BG_LOE_077");
        OnBoard(L, 0, "BG_LOE_077");                                            // two Branns do not stack
        uint32_t const monkey = OnBoard(L, 0, "BG23_353_Gt");
        PlayCard(L, 0, "BG_ICC_807");
        CHECK(Find(L, 0, monkey)->attack == 10);
        // Without Brann the Battlecry fires once; a mere Menagerie Jug under Brann buffs twice
        Lobby J(3, 2);
        OnBoard(J, 0, "BG_LOE_077");
        uint32_t const beast = OnBoard(J, 0, "BG_EX1_534t");
        PlayCard(J, 0, "BGS_083");
        CHECK(Find(J, 0, beast)->attack == CardAt(Cd("BG_EX1_534t")).attack + 4);
    }

    // Baron Rivendare: every Deathrattle runs twice (golden: three times); the best Baron counts.
    void NtTestBaron()
    {
        // Spawn of N'Zoth dies first (it attacks first and takes 5); its buffs land on the Baron, once per run.
        for (int g = 0; g < 2; ++g)
        {
            Minion baron = NtOf("BG_FP1_031", g == 1, 0, 500);
            CombatResult r = Fight({ With(Mn("BG_OG_256"), 1, 1), baron }, { Body(5, 500) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(static_cast<int>(b.size()) == 2 + g);
            for (CombatEvent const& e : b)
                CHECK(e.target == 2 && e.amount == 1 && e.amount2 == 1);
        }
        // Two Barons do not stack
        CombatResult r = Fight({ With(Mn("BG_OG_256"), 1, 1), NtOf("BG_FP1_031", false, 0, 500), NtOf("BG_FP1_031", false, 0, 500) }, { Body(5, 500) });
        int onFirstBaron = 0;
        for (CombatEvent const& e : NtBuffsBy(r, 1))
            if (e.target == 2)
                ++onFirstBaron;
        CHECK(onFirstBaron == 2);
    }

    // Kangor's Apprentice summons the first 2 (golden 4) Mechs that died, before it; Khadgar multiplies the summons.
    void CheckKangor(bool golden, bool khadgar, int seed)
    {
        int const leaper = Cd("BG_GVG_048");
        int const kangorCard = Cd("BGS_012");
        std::vector<Minion> board;
        if (khadgar)
            board.push_back(NtOf("BG_DAL_575", false, 0, 3000));
        board.push_back(NtOf("BG_GVG_048", false, 3, 3));
        board.push_back(NtOf("BG_GVG_048", false, 3, 3));
        board.push_back(NtOf("BGS_012", golden, 1, 1));
        uint32_t const kangorId = static_cast<uint32_t>(board.size());
        CombatResult const r = Fight(board, { Body(4, 400) }, static_cast<uint32_t>(seed));
        int mechDeaths = 0;
        int summoned = 0;
        int expected = -1;
        for (CombatEvent const& e : r.events)
        {
            if (e.type == EventType::Death && e.card == leaper && expected < 0)
                ++mechDeaths;
            else if (e.type == EventType::Death && e.card == kangorCard && e.target == kangorId)
                expected = std::min(mechDeaths, golden ? 4 : 2) * (khadgar ? 2 : 1);
            else if (e.type == EventType::Summon && e.actor == kangorId && e.card == leaper)
                ++summoned;
        }
        CHECK(expected >= 0);
        CHECK(summoned == expected);
    }

    void NtTestKangorAndKhadgar()
    {
        for (int seed = 1; seed <= 8; ++seed)
        {
            CheckKangor(false, false, seed);
            CheckKangor(true, false, seed);
            CheckKangor(false, true, seed);
            CheckKangor(true, true, seed);
        }
        // Kangor's Apprentice with two dead Mechs always summons both: Mechs first in the line, enemy kills them in order
        CombatResult r = Fight({ NtOf("BG_GVG_048", false, 1, 1), NtOf("BGS_012", false, 1, 1) }, { Body(3, 500), Body(3, 500) });
        CHECK(r.winner == 1);
    }

    // Khadgar: the shop summons twice (golden: three times) for a card that summons; here a test-only Battlecry.
    void NtTestKhadgarShop()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry rr;
            EffectDef d;
            d.battlecry = [](ShopCtx& c, bool golden) { c.Summon(Cd("BG_CFM_315t"), golden, c.position + 1); };
            rr.Register("BG_CFM_315", d);
            d = EffectDef();
            d.summonTimes = 2;
            d.summonTimesGolden = 3;
            rr.Register("BG_DAL_575", d);
            return rr;
        }();
        Lobby L(1, 2, reg);
        OnBoard(L, 0, "BG_DAL_575");
        PlayCard(L, 0, "BG_CFM_315");
        CHECK(L.S(0).board.Size() == 4);
        Lobby G(1, 2, reg);
        OnBoard(G, 0, "BG_DAL_575", true);
        PlayCard(G, 0, "BG_CFM_315", true);
        CHECK(G.S(0).board.Size() == 5);
        // the real registry agrees on the fields
        CHECK(L.shop.SummonTimes(0) == 2 && G.shop.SummonTimes(0) == 3);
    }

    // ------------------------------------------------------------------------------------------ shop cards

    void NtTestWrathWeaver()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const ww = OnBoard(L, 0, "BGS_004", g == 1);
            int const a = Find(L, 0, ww)->attack;
            int const h = Find(L, 0, ww)->health;
            PlayCard(L, 0, "BG23_353_Gt");                                      // not a Demon
            CHECK(Find(L, 0, ww)->attack == a && L.shop.HeroHealth(0) == kStartHealth);
            PlayCard(L, 0, "BG_BRM_006t");                                      // a Demon: +2/+1 (golden: all twice)
            CHECK(Find(L, 0, ww)->attack == a + 2 * (g + 1) && Find(L, 0, ww)->health == h + 1 * (g + 1));
            CHECK(L.shop.HeroHealth(0) == kStartHealth - (g + 1));
        }
        // Kathra'natir's Immune stops the damage but not the buff (only when the Demon batch has landed)
        if (EffectRegistry::Global().Find(Cd("BG21_039")))
        {
            Lobby L(2, 2);
            uint32_t const ww = OnBoard(L, 0, "BGS_004");
            OnBoard(L, 0, "BG21_039");
            PlayCard(L, 0, "BG_BRM_006t");
            CHECK(L.shop.HeroHealth(0) == kStartHealth && Find(L, 0, ww)->attack == CardAt(Cd("BGS_004")).attack + 2);
        }
    }

    void NtTestWhelpSmugglerShop()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            OnBoard(L, 0, "BG21_013", g == 1);
            uint32_t const dragon = OnBoard(L, 0, "BGS_034");
            uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
            int const h = Find(L, 0, dragon)->health;
            L.shop.Buff(0, *Find(L, 0, dragon), 2, 0);
            CHECK(Find(L, 0, dragon)->health == h + 1 + g);
            L.shop.Buff(0, *Find(L, 0, dragon), 0, 3);                          // a Health gain does not loop
            CHECK(Find(L, 0, dragon)->health == h + 1 + g + 3);
            int const bh = Find(L, 0, beast)->health;
            L.shop.Buff(0, *Find(L, 0, beast), 2, 0);                           // not a Dragon
            CHECK(Find(L, 0, beast)->health == bh);
        }
    }

    // Combat: Yrel's buff gives the Dragon Attack, so the Smuggler answers with Health every time.
    void NtTestWhelpSmugglerCombat()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ NtOf("BG23_350", false, 1, 50), NtOf("BG21_013", g == 1, 0, 50), NtOf("BGS_034", false, 0, 50) }, { Body(0, 3) });
            int yrelOnDragon = 0;
            for (CombatEvent const& e : NtBuffsBy(r, 1))
                if (e.target == 3)
                    ++yrelOnDragon;
            std::vector<CombatEvent> const mine = NtBuffsBy(r, 2);
            CHECK(yrelOnDragon >= 2);
            CHECK(static_cast<int>(mine.size()) == yrelOnDragon);
            for (CombatEvent const& e : mine)
                CHECK(e.target == 3 && e.amount == 0 && e.amount2 == 1 + g);
        }
    }

    void NtTestYrel()
    {
        for (int g = 0; g < 2; ++g)
        {
            Minion beast2 = Mn("BG_EX1_534t");
            CombatResult r = Fight({ NtOf("BG23_350", g == 1, 3, 50), Mn("BG_EX1_534t"), beast2, Mn("BG_BOT_312t") }, { Body(0, 300) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(b.size() >= 2 && b.size() % 2 == 0);
            for (CombatEvent const& e : b)
                CHECK(e.amount == 1 + g && e.amount2 == 2 + 2 * g && e.target != 1);
            if (b.size() >= 2)
            {
                bool const firstIsMech = b[0].target == 4;
                bool const secondIsMech = b[1].target == 4;
                CHECK(firstIsMech != secondIsMech);                              // one Beast and the Mech, not two Beasts
            }
        }
    }

    void NtTestSparringPartner()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
            uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
            PlayCard(L, 0, "BG_AT_069", g == 1, -1, mech);
            CHECK(Find(L, 0, mech)->Has(kKwTaunt) && !Find(L, 0, beast)->Has(kKwTaunt));
        }
        Lobby L(2, 2);                                                           // no target: a random valid one
        uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
        PlayCard(L, 0, "BG_AT_069");
        CHECK(Find(L, 0, beast)->Has(kKwTaunt));
        Lobby E(3, 2);                                                           // nothing to target: no effect
        PlayCard(E, 0, "BG_AT_069");
        CHECK(E.S(0).board.Size() == 1);
    }

    void NtTestHoundmaster()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
            uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
            int const a = Find(L, 0, beast)->attack;
            PlayCard(L, 0, "BG_DS1_070", g == 1, -1, mech);                      // the Mech is no valid target: random Beast
            CHECK(Find(L, 0, beast)->attack == a + 2 * (g + 1) && Find(L, 0, beast)->Has(kKwTaunt));
            CHECK(!Find(L, 0, mech)->Has(kKwTaunt));
        }
        Lobby E(2, 2);
        uint32_t const mech = OnBoard(E, 0, "BG_BOT_312t");
        PlayCard(E, 0, "BG_DS1_070");                                            // no Beast: nothing
        CHECK(!Find(E, 0, mech)->Has(kKwTaunt) && Find(E, 0, mech)->attack == 1);
    }

    void NtTestReefExplorer()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            OnBoard(L, 0, "BG_EX1_534t");
            uint32_t const reef = OnBoard(L, 0, "BG23_016", g == 1);
            L.shop.TriggerBattlecry(0, reef, 0);
            PlayerShop& ps = L.S(0);
            CHECK(ps.options.size() == 3);
            CHECK(ps.queue.size() == static_cast<std::size_t>(g));
            for (Minion const& o : ps.options)
                CHECK(o.Tribes() != kTribeBeast);                                // never a type the board already has
        }
        Lobby A(2, 2);                                                           // the Amalgam is every type: nothing to find
        OnBoard(A, 0, "BG_GIL_681");
        uint32_t const reef = OnBoard(A, 0, "BG23_016");
        A.shop.TriggerBattlecry(0, reef, 0);
        CHECK(A.S(0).options.empty());
    }

    void NtTestElise()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            OnBoard(L, 0, "BG23_353", g == 1);
            SetGold(L, 0, 30);
            char const* const monkey = "BG23_353_Gt";
            for (int i = 0; i < 4; ++i)
                CHECK(L.shop.Refresh(0) == ActionResult::Ok);
            CHECK(NtHandCount(L, 0, monkey) == 0);
            CHECK(L.shop.Refresh(0) == ActionResult::Ok);
            CHECK(NtHandCount(L, 0, monkey) == 1 + g);
            for (int i = 0; i < 6; ++i)
                CHECK(L.shop.Refresh(0) == ActionResult::Ok);
            CHECK(NtHandCount(L, 0, monkey) == 1 + g);                           // found only once
        }
    }

    void NtTestRendle()
    {
        Lobby L(1, 2);
        NewOffers(L, 0, { "BG19_010", "BGS_009", "BG_ICC_807" });                // tiers 2, 5, 4
        OnBoard(L, 0, "BG24_022");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(L.S(0).hand.Size() == 1 && L.S(0).hand[0].card == Cd("BGS_009"));
        CHECK(L.S(0).offers.size() == 2);
        CHECK(L.shop.CheckConservation());
        Lobby G(1, 2);                                                           // golden: two, highest first
        NewOffers(G, 0, { "BG19_010", "BGS_009", "BG_ICC_807" });
        OnBoard(G, 0, "BG24_022", true);
        G.shop.EndTurn(0);
        CHECK(G.S(0).hand.Size() == 2 && G.S(0).hand[0].card == Cd("BGS_009") && G.S(0).hand[1].card == Cd("BG_ICC_807"));
        CHECK(G.S(0).offers.size() == 1);
        Lobby F(2, 2);                                                           // a full hand: nothing is taken
        NewOffers(F, 0, { "BGS_009" });
        OnBoard(F, 0, "BG24_022");
        for (char const* id : { "BG19_010", "BGS_004", "BG21_013", "BG23_350", "BG_AT_069", "BG_OG_221", "BG_OG_256", "BG21_002", "BGS_002", "BGS_110" })
            InHand(F, 0, id);
        F.shop.EndTurn(0);
        CHECK(F.S(0).offers.size() == 1 && F.S(0).hand.Size() == 10);
    }

    void NtTestMenagerieJug()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const b1 = OnBoard(L, 0, "BG_EX1_534t");
            uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
            uint32_t const demon = OnBoard(L, 0, "BG_BRM_006t");
            uint32_t const b2 = OnBoard(L, 0, "BG_CFM_315t");
            PlayCard(L, 0, "BGS_083", g == 1);
            int buffed = 0;
            int beasts = 0;
            for (uint32_t uid : { b1, mech, demon, b2 })
            {
                Minion const* m = Find(L, 0, uid);
                if (m->attack > CardAt(m->card).attack)
                {
                    ++buffed;
                    CHECK(m->attack == CardAt(m->card).attack + 2 * (g + 1) && m->health == CardAt(m->card).health + 2 * (g + 1));
                    if (m->IsTribe(kTribeBeast))
                        ++beasts;
                }
            }
            CHECK(buffed == 3 && beasts == 1);
        }
        Lobby S(2, 2);                                                           // only two Beasts: one of them
        uint32_t const x = OnBoard(S, 0, "BG_EX1_534t");
        uint32_t const y = OnBoard(S, 0, "BG_CFM_315t");
        PlayCard(S, 0, "BGS_083");
        CHECK((Find(S, 0, x)->attack > 2) != (Find(S, 0, y)->attack > 1));
    }

    void NtTestStrongshell()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const taunt = OnBoard(L, 0, "BG23_353_Gt");
            uint32_t const plain = OnBoard(L, 0, "BG_EX1_534t");
            uint32_t const granted = OnBoard(L, 0, "BG_BOT_312t");
            L.shop.GiveKeywords(0, *Find(L, 0, granted), kKwTaunt);              // Taunt from any source
            PlayCard(L, 0, "BG_ICC_807", g == 1);
            CHECK(Find(L, 0, taunt)->attack == 6 + 2 * (g + 1));
            CHECK(Find(L, 0, granted)->health == 1 + 2 * (g + 1));
            CHECK(Find(L, 0, plain)->attack == 2);
        }
    }

    void NtTestLightfang()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const b1 = OnBoard(L, 0, "BG_EX1_534t");
            uint32_t const b2 = OnBoard(L, 0, "BG_CFM_315t");
            uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
            uint32_t const light = OnBoard(L, 0, "BGS_009", g == 1);
            int const lightAttack = Find(L, 0, light)->attack;
            CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
            int const beastGain = (Find(L, 0, b1)->attack - 2) + (Find(L, 0, b2)->attack - 1);
            CHECK(beastGain == 2 * (g + 1));                                     // exactly one of the two Beasts
            CHECK(Find(L, 0, mech)->attack == 1 + 2 * (g + 1) && Find(L, 0, mech)->health == 1 + 2 * (g + 1));
            CHECK(Find(L, 0, light)->attack == lightAttack);                     // it has no type itself
        }
    }

    void NtTestUther()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            uint32_t const t = OnBoard(L, 0, "BG_EX1_534t");
            uint32_t const other = OnBoard(L, 0, "BG_BOT_312t");
            PlayCard(L, 0, "BG23_190", g == 1, -1, t);
            int const v = g == 1 ? 30 : 15;
            CHECK(Find(L, 0, t)->attack == v && Find(L, 0, t)->health == v && Find(L, 0, t)->maxHealth == v);
            CHECK(Find(L, 0, other)->attack == 1);
        }
        Lobby R(2, 2);                                                           // no target: the only other minion
        uint32_t const t = OnBoard(R, 0, "BG_EX1_534t");
        uint32_t const uther = PlayCard(R, 0, "BG23_190");
        CHECK(Find(R, 0, t)->attack == 15);
        CHECK(Find(R, 0, uther)->attack == CardAt(Cd("BG23_190")).attack);        // never itself
    }

    void NtTestTheotar()
    {
        for (int g = 0; g < 2; ++g)
        {
            Lobby L(1, 2);
            OnBoard(L, 0, "BG24_020", g == 1);
            uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
            uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
            PlayCard(L, 0, "BG_CFM_315t");                                       // a Beast: no trigger
            CHECK(Find(L, 0, beast)->attack == 2 && Find(L, 0, mech)->attack == 1);
            PlayCard(L, 0, "BG23_353_Gt");                                       // no minion type: trigger
            CHECK(Find(L, 0, mech)->attack == 1 + 4 * (g + 1) && Find(L, 0, mech)->health == 1 + 4 * (g + 1));
            CHECK(Find(L, 0, beast)->attack + L.S(0).board[3].attack == 2 + 1 + 4 * (g + 1));   // one of the two Beasts
        }
        Lobby M(2, 2);                                                           // Wargear merged into a Mech has a type
        OnBoard(M, 0, "BG24_020");
        uint32_t const mech = OnBoard(M, 0, "BG_GVG_048");
        OnBoard(M, 0, "BG_EX1_534t");
        int const before = Find(M, 0, mech)->attack;
        PlayCard(M, 0, "BG_BOT_563", false, -1, mech);
        CHECK(Find(M, 0, mech)->attack == before + CardAt(Cd("BG_BOT_563")).attack);
    }

    // ------------------------------------------------------------------------------------------ combat cards

    void NtTestSelflessHero()
    {
        {
            CombatResult r = Fight({ With(Mn("BG_OG_221"), 1, 1), Body(0, 100), Body(0, 100) }, { Body(10, 100) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(b.size() == 1 && (b[0].keywords & kKwDivineShield) != 0 && b[0].target >= 2 && b[0].target <= 3);
        }
        {
            CombatResult r = Fight({ With(Mn("BG_OG_221", true), 1, 1), Body(0, 100), Body(0, 100) }, { Body(10, 100) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(b.size() == 2 && b[0].target != b[1].target);                  // two different minions
        }
        {
            CombatResult r = Fight({ With(Mn("BG_OG_221"), 1, 1), Body(0, 100, kKwDivineShield) }, { Body(10, 100) });
            CHECK(NtBuffsBy(r, 1).empty());                                      // everyone already has one
        }
    }

    void NtTestSpawnOfNZoth()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ With(Mn("BG_OG_256", g == 1), 1, 1), Body(0, 300), Body(0, 300) }, { Body(5, 500) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(b.size() == 2);
            for (CombatEvent const& e : b)
                CHECK(e.amount == 1 + g && e.amount2 == 1 + g);
        }
    }

    void NtTestBirdBuddy()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ NtOf("BG21_002", g == 1, 0, 500), Mn("BG_EX1_534t"), Mn("BG_EX1_534t"), Body(1, 1) }, { Body(3, 100) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(!b.empty());
            for (CombatEvent const& e : b)
                CHECK(e.amount == 1 + g && e.amount2 == 1 + g && (e.target == 2 || e.target == 3));   // only Beasts, never the Body
        }
    }

    void NtTestSoulJuggler()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ NtOf("BGS_002", g == 1, 0, 100), With(Mn("BG_BRM_006t"), 1, 1) }, { Body(2, 50) });
            CHECK(NtDamageBy(r, 1, 3) == 1 + g);                                 // one Demon died: 3 damage (golden: twice)
            CombatResult q = Fight({ NtOf("BGS_002", g == 1, 0, 100), With(Mn("BG_EX1_534t"), 1, 1) }, { Body(2, 50) });
            CHECK(NtDamageBy(q, 1, 3) == 0);                                     // a Beast died: nothing
        }
    }

    void NtTestArmOfTheEmpire()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ Mn("BGS_110", g == 1), Body(1, 50, kKwTaunt) }, { Body(1, 10) });
            CHECK(NtHasPersistent(r, 0, 1, 2 * (g + 1)));
            CombatResult q = Fight({ Mn("BGS_110", g == 1), Body(1, 50) }, { Body(1, 10) });
            CHECK(q.persistent.empty());                                         // nobody has Taunt
        }
    }

    void NtTestDoomsayer()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ NtOf("BG21_007", g == 1, 0, 500), Body(1, 1), Body(1, 1), Body(1, 1), Body(1, 1) }, { Body(3, 500) });
            int adds = 0;
            for (PersistentChange const& c : r.persistent)
                if (c.kind == kPersistAddToHand)
                {
                    ++adds;
                    CHECK(c.side == 0 && c.card >= 0 && HasTribe(CardAt(c.card).tribes, kTribeDemon));
                }
            CHECK(adds == 1 + g);                                                // after the 4th friendly death
            CombatResult q = Fight({ NtOf("BG21_007", g == 1, 0, 500), Body(1, 1), Body(1, 1), Body(1, 1) }, { Body(3, 500) });
            bool added = false;
            for (PersistentChange const& c : q.persistent)
                added = added || c.kind == kPersistAddToHand;
            CHECK(!added);                                                       // three deaths are not enough
        }
    }

    void NtTestTunnelBlaster()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ With(Mn("BG_DAL_775", g == 1), 1, 1), Body(0, 20) }, { Body(2, 40) });
            CHECK(NtDamageBy(r, 1, 3) == 2 * (1 + g));                           // the friendly Body and the enemy, per run
            bool friendlyHit = false;
            bool enemyHit = false;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Damage && e.actor == 1 && e.amount == 3)
                {
                    friendlyHit = friendlyHit || e.target == 2;
                    enemyHit = enemyHit || e.target == 8;
                }
            CHECK(friendlyHit && enemyHit);
        }
        // Divine Shield absorbs the first run (golden) completely
        CombatResult s = Fight({ With(Mn("BG_DAL_775"), 1, 1), Body(0, 20, kKwDivineShield) }, { Body(2, 40) });
        CHECK(Events(s, EventType::ShieldPop).size() == 1);
    }

    void NtTestLeeroy()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ With(Mn("BG23_318", g == 1), 1, 1) }, { Body(5, 5, kKwDivineShield) });
            bool destroyed = false;
            for (CombatEvent const& e : Events(r, EventType::Death))
                if (e.target == 8 && e.amount == 1)
                    destroyed = true;
            CHECK(destroyed);                                                    // the killer dies despite its Divine Shield
            CHECK(Alive(r, 0) == 0 && Alive(r, 1) == 0 && r.winner == -1);
        }
    }

    void NtTestMantidQueen()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ Mn("BG22_402", g == 1), Mn("BG_EX1_534t"), Mn("BG_BOT_312t") }, { Body(0, 5) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(b.size() == static_cast<std::size_t>(2 * (1 + g)));            // two types, one roll each (golden: two)
            for (CombatEvent const& e : b)
                CHECK(e.target == 1 && ((e.amount == 5 && e.amount2 == 5) || e.keywords != 0));
            CombatResult none = Fight({ Mn("BG22_402", g == 1) }, { Body(0, 5) });
            CHECK(NtBuffsBy(none, 1).empty());                                   // it has no type itself
            CombatResult amalgam = Fight({ Mn("BG22_402", g == 1), Mn("BG_GIL_681") }, { Body(0, 5) });
            CHECK(NtBuffsBy(amalgam, 1).size() == static_cast<std::size_t>(1 + g));   // the Amalgam is one type
        }
        // Windfury on a minion that has it becomes Mega-Windfury
        bool mega = false;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            Minion q = Mn("BG22_402", true);
            q.keywords |= kKwWindfury;
            CombatResult r = Fight({ q, Mn("BG_EX1_534t"), Mn("BG_BOT_312t"), Mn("BG_BRM_006t") }, { Body(0, 5) }, seed);
            for (CombatEvent const& e : NtBuffsBy(r, 1))
            {
                CHECK(e.keywords != kKwWindfury);
                if (e.keywords == kKwMegaWindfury)
                    mega = true;
            }
        }
        CHECK(mega);
        // Venomous is Poisonous once: the first hit destroys, then it is gone (two 0/3 enemies: 1 + 3 attacks)
        CombatResult v = Fight({ Body(1, 100, kKwVenomous) }, { Body(0, 3), Body(0, 3) });
        CHECK(v.winner == 0 && v.attacks == 4);
        CHECK(v.survivors[0].Size() == 1 && !v.survivors[0][0].Has(kKwVenomous));
        CombatResult p = Fight({ Body(1, 100, kKwPoisonous) }, { Body(0, 3), Body(0, 3) });
        CHECK(p.winner == 0 && p.attacks == 2);                              // Poisonous stays
    }

    void NtTestZapp()
    {
        for (int g = 0; g < 2; ++g)
        {
            std::vector<Minion> enemies = { Body(5, 100, kKwTaunt), Body(1, 100), Body(3, 100) };
            CombatResult r = Fight({ Mn("BGS_022", g == 1), Body(0, 5), Body(0, 5), Body(0, 5) }, enemies);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            CHECK(atk.size() >= 4);
            std::size_t const swings = 2;                                        // Windfury, the golden one too (render text)
            for (std::size_t i = 0; i < swings && i < atk.size(); ++i)
                CHECK(atk[i].actor == 1 && atk[i].target == 9);                  // the lowest Attack, through the Taunt
        }
        // ties are random: both Attack-1 minions get picked over several seeds
        std::set<uint32_t> targets;
        for (uint32_t seed = 1; seed <= 30; ++seed)
        {
            CombatResult r = Fight({ Mn("BGS_022"), Body(0, 5), Body(0, 5), Body(0, 5) }, { Body(1, 100), Body(1, 100), Body(4, 100) }, seed);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            if (!atk.empty())
                targets.insert(atk[0].target);
        }
        CHECK(targets.size() == 2 && targets.count(8) == 1 && targets.count(9) == 1);
    }

    void NtTestNadina()
    {
        for (int g = 0; g < 2; ++g)
        {
            CombatResult r = Fight({ With(Mn("BGS_040", g == 1), 1, 1), PlainOf(kTribeDragon), Body(0, 100) }, { Body(5, 100) });
            std::vector<CombatEvent> const b = NtBuffsBy(r, 1);
            CHECK(b.size() == 1 && b[0].target == 2 && (b[0].keywords & kKwDivineShield) != 0);
            // at most 3 (golden 6) Dragons
            std::vector<Minion> mine = { With(Mn("BGS_040", g == 1), 1, 1) };
            for (int i = 0; i < 6; ++i)
                mine.push_back(PlainOf(kTribeDragon));
            CombatResult const r2 = Fight(mine, { Body(5, 100) });
            CHECK(static_cast<int>(NtBuffsBy(r2, 1).size()) == (g == 1 ? 6 : 3));
        }
    }

}

static void RunNeutralTests()
{
    RUN(NtTestNeutralRegistry);
    RUN(NtTestBrann);
    RUN(NtTestBaron);
    RUN(NtTestKangorAndKhadgar);
    RUN(NtTestKhadgarShop);
    RUN(NtTestWrathWeaver);
    RUN(NtTestWhelpSmugglerShop);
    RUN(NtTestWhelpSmugglerCombat);
    RUN(NtTestYrel);
    RUN(NtTestSparringPartner);
    RUN(NtTestHoundmaster);
    RUN(NtTestReefExplorer);
    RUN(NtTestElise);
    RUN(NtTestRendle);
    RUN(NtTestMenagerieJug);
    RUN(NtTestStrongshell);
    RUN(NtTestLightfang);
    RUN(NtTestUther);
    RUN(NtTestTheotar);
    RUN(NtTestSelflessHero);
    RUN(NtTestSpawnOfNZoth);
    RUN(NtTestBirdBuddy);
    RUN(NtTestSoulJuggler);
    RUN(NtTestArmOfTheEmpire);
    RUN(NtTestDoomsayer);
    RUN(NtTestTunnelBlaster);
    RUN(NtTestLeeroy);
    RUN(NtTestMantidQueen);
    RUN(NtTestZapp);
    RUN(NtTestNadina);
}
