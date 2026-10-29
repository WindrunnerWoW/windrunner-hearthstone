// Tests of the Mech effect batch (src/battlegrounds/HsBgEffectsMech.cpp).
// Included by bg_effects_tests.cpp, which provides CHECK, RUN and the helpers (see its header comment). One test per
// minion and per golden version; write each as a static function and call it from RunMechTests with RUN(fn).
// Only this file and HsBgEffectsMech.cpp belong to the Mech batch.
//
// Combat ids: the left-most minion of side 0 is 1, of side 1 is 8; summoned minions start at kFirstSummonId.

namespace
{
    // The real Mech entries plus the multipliers of other batches, as test-only fields (Brann, Baron, Khadgar).
    EffectRegistry const& MechReg()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            for (char const* id : { "BG_ULD_217", "BG_BOT_606", "BG_EX1_556", "BG_GVG_048", "BG_BOT_312", "BG_GVG_027", "BGS_071",
                                    "BG21_023", "BG20_401", "BG21_024", "BG21_025", "BG_GVG_113" })
                r.Register(id, *EffectRegistry::Global().Find(Cd(id)));
            EffectDef d;
            d.battlecryTimes = 2;
            d.battlecryTimesGolden = 3;
            r.Register("BG_LOE_077", d);                  // Brann
            d = EffectDef();
            d.deathrattleTimes = 2;
            d.deathrattleTimesGolden = 3;
            r.Register("BG_FP1_031", d);                  // Baron Rivendare
            d = EffectDef();
            d.summonTimes = 2;
            d.summonTimesGolden = 3;
            r.Register("BG_DAL_575", d);                  // Khadgar
            return r;
        }();
        return reg;
    }

    void EndTurnOf(Lobby& L, int p)
    {
        L.shop.StartTurn(p, 1);
        CHECK(L.shop.EndTurn(p) == ActionResult::Ok);
        L.shop.Settle(p);
    }

    // A sturdy multiplier minion that stays out of the way.
    Minion Tough(char const* id, bool golden) { return With(Mn(id, golden), 0, 200); }

    int SumDamageFrom(CombatResult const& r, uint32_t actor, int side)
    {
        int n = 0;
        for (CombatEvent const& e : Events(r, EventType::Damage))
            if (e.actor == actor && e.side == side)
                n += e.amount;
        return n;
    }
}

static void TestPupbotWargearAnnoy()
{
    CHECK(Mn("BG21_022").Has(kKwDivineShield) && Mn("BG21_022").IsTribe(kTribeMech));
    CHECK(Mn("BG21_022", true).Has(kKwDivineShield) && Mn("BG21_022", true).attack == 4);
    Lobby L(1, 2, MechReg());
    uint32_t const host = OnBoard(L, 0, "BG_EX1_556");              // 2/3
    PlayCard(L, 0, "BG_BOT_563", false, -1, host);                  // Wargear +6/+5 (render stats)
    CHECK(L.S(0).board.Size() == 1);
    CHECK(Find(L, 0, host)->attack == 8 && Find(L, 0, host)->health == 8);
    PlayCard(L, 0, "BG_BOT_911", false, -1, host);                  // Annoy-o-Module +2/+4, Divine Shield, Taunt
    Minion const* h = Find(L, 0, host);
    CHECK(h->attack == 10 && h->health == 12 && h->Has(kKwDivineShield) && h->Has(kKwTaunt));
    Lobby G(1, 2, MechReg());
    uint32_t const gh = OnBoard(G, 0, "BG_EX1_556");
    PlayCard(G, 0, "BG_BOT_563", true, -1, gh);                     // golden Wargear +12/+10
    CHECK(Find(G, 0, gh)->attack == 14 && Find(G, 0, gh)->health == 13);
}

static void TestMicroMummy()
{
    for (int g = 0; g < 2; ++g)
    {
        Lobby L(1, 2, MechReg());
        OnBoard(L, 0, "BG_ULD_217", g != 0);
        uint32_t const mon = OnBoard(L, 0, "BG23_353_Gt");
        int const a = Find(L, 0, mon)->attack;
        EndTurnOf(L, 0);
        CHECK(Find(L, 0, mon)->attack == a + (g != 0 ? 2 : 1));      // any tribe, permanent
        CHECK(Find(L, 0, mon)->health == Mn("BG23_353_Gt").health);
    }
    Lobby A(1, 2, MechReg());                                       // alone: nothing, and never itself
    uint32_t const mum = OnBoard(A, 0, "BG_ULD_217");
    EndTurnOf(A, 0);
    CHECK(Find(A, 0, mum)->attack == Mn("BG_ULD_217").attack);
}

static void TestKaboomBot()
{
    for (int g = 0; g < 2; ++g)
    {
        CombatResult r = Fight({ With(Mn("BG_BOT_606", g != 0), 0, 1) }, { Body(5, 50) }, 2);
        CHECK(SumDamageFrom(r, 1, 1) == (g != 0 ? 8 : 4));           // golden: twice
        CHECK(Alive(r, 1) == 1 && Survivor(r, 1, 0).health == 50 - (g != 0 ? 8 : 4));
    }
    // kills resolve after the handler: the last enemy minion dies with Kaboom Bot (draw)
    CombatResult d = Fight({ With(Mn("BG_BOT_606"), 0, 1) }, { Body(5, 4) }, 3);
    CHECK(Alive(d, 0) == 0 && Alive(d, 1) == 0);
    // the hit goes to some enemy minion; over many seeds both are hit
    int hit8 = 0;
    int hit9 = 0;
    for (uint32_t s = 1; s <= 30; ++s)
    {
        CombatResult r = Fight({ With(Mn("BG_BOT_606"), 0, 1) }, { Body(5, 50), Body(5, 50) }, s);
        for (CombatEvent const& e : Events(r, EventType::Damage))
            if (e.actor == 1 && e.side == 1 && e.amount == 4)
                (e.target == 8 ? hit8 : hit9) += 1;
    }
    CHECK(hit8 > 0 && hit9 > 0);
}

static void TestHarvestGolem()
{
    int const golem = Cd("BG_EX1_556t");
    CombatResult r = Fight({ With(Mn("BG_EX1_556"), 1, 1) }, { Body(3, 20) }, 1);
    CHECK(SummonedCards(r, golem) == 1);
    std::vector<CombatEvent> s = Events(r, EventType::Summon);
    CHECK(!s.empty() && !s[0].golden && s[0].amount == 2 && s[0].amount2 == 1);
    CombatResult g = Fight({ With(Mn("BG_EX1_556", true), 1, 1) }, { Body(3, 20) }, 1);
    s = Events(g, EventType::Summon);
    CHECK(!s.empty() && s[0].card == golem && s[0].golden && s[0].amount == 4 && s[0].amount2 == 2);
}

static void TestMetaltoothLeaper()
{
    for (int g = 0; g < 2; ++g)
    {
        Lobby L(1, 2, MechReg());
        uint32_t const micro = OnBoard(L, 0, "BG_BOT_312t");
        uint32_t const pup = OnBoard(L, 0, "BG21_022");
        uint32_t const mon = OnBoard(L, 0, "BG23_353_Gt");
        uint32_t const self = PlayCard(L, 0, "BG_GVG_048", g != 0);
        int const add = g != 0 ? 4 : 2;
        CHECK(Find(L, 0, micro)->attack == 1 + add && Find(L, 0, pup)->attack == 2 + add);
        CHECK(Find(L, 0, mon)->attack == Mn("BG23_353_Gt").attack);           // not a Mech
        CHECK(Find(L, 0, self)->attack == Mn("BG_GVG_048", g != 0).attack);   // only the other Mechs
        CHECK(Find(L, 0, micro)->health == 1);
    }
    // Brann: the Battlecry fires twice (three times golden)
    for (int g = 0; g < 2; ++g)
    {
        Lobby L(1, 2, MechReg());
        OnBoard(L, 0, "BG_LOE_077", g != 0);
        uint32_t const micro = OnBoard(L, 0, "BG_BOT_312t");
        PlayCard(L, 0, "BG_GVG_048");
        CHECK(Find(L, 0, micro)->attack == 1 + (g != 0 ? 6 : 4));
    }
}

static void TestReplicatingMenace()
{
    int const micro = Cd("BG_BOT_312t");
    CombatResult r = Fight({ With(Mn("BG_BOT_312"), 1, 1) }, { Body(3, 50) }, 1);
    CHECK(SummonedCards(r, micro) == 3);
    for (CombatEvent const& e : Events(r, EventType::Summon))
        CHECK(!e.golden && e.amount == 1 && e.amount2 == 1);
    CombatResult g = Fight({ With(Mn("BG_BOT_312", true), 1, 1) }, { Body(3, 50) }, 1);
    CHECK(SummonedCards(g, micro) == 3);
    for (CombatEvent const& e : Events(g, EventType::Summon))
        CHECK(e.golden && e.amount == 2 && e.amount2 == 2);
    // Baron Rivendare: twice (golden: three times, cut by the board cap)
    CombatResult b = FightWith(MechReg(), { Tough("BG_FP1_031", false), With(Mn("BG_BOT_312"), 1, 1) }, { Body(3, 50) }, 1);
    CHECK(SummonedCards(b, micro) == 6);
    CombatResult b3 = FightWith(MechReg(), { Tough("BG_FP1_031", true), With(Mn("BG_BOT_312"), 1, 1) }, { Body(3, 50) }, 1);
    CHECK(SummonedCards(b3, micro) == 6);                            // board: Baron + 6 Microbots = 7 (nine would not fit)
    // Khadgar: each summon is doubled
    CombatResult k = FightWith(MechReg(), { Tough("BG_DAL_575", false), With(Mn("BG_BOT_312"), 1, 1) }, { Body(3, 50) }, 1);
    CHECK(SummonedCards(k, micro) == 6);
    // Magnetic: merged into a Mech in the shop, the host's death runs it (the attachment's golden flag)
    for (int gold = 0; gold < 2; ++gold)
    {
        Lobby L(1, 2, MechReg());
        uint32_t const host = OnBoard(L, 0, "BG_EX1_556t");        // 2/1 Mech token
        PlayCard(L, 0, "BG_BOT_312", gold != 0, -1, host);
        CHECK(L.S(0).board.Size() == 1 && Find(L, 0, host)->attachments.size() == 1);
        Minion h = *Find(L, 0, host);
        CHECK(h.attack == 2 + Mn("BG_BOT_312", gold != 0).attack && h.health == 1 + Mn("BG_BOT_312", gold != 0).health);
        h = With(h, 1, 1);
        CombatResult m = Fight({ h }, { Body(3, 50) }, 1);
        std::vector<CombatEvent> const s = Events(m, EventType::Summon);
        CHECK(s.size() == 3);
        for (CombatEvent const& e : s)
            CHECK(e.card == micro && e.golden == (gold != 0));
        // Baron: the attachment's Deathrattle runs twice
        CombatResult bm = FightWith(MechReg(), { Tough("BG_FP1_031", false), h }, { Body(3, 50) }, 1);
        CHECK(SummonedCards(bm, micro) == 6);
    }
}

static void TestIronSensei()
{
    for (int g = 0; g < 2; ++g)
    {
        Lobby L(1, 2, MechReg());
        uint32_t const sensei = OnBoard(L, 0, "BG_GVG_027", g != 0);
        uint32_t const mon = OnBoard(L, 0, "BG23_353_Gt");
        uint32_t const micro = OnBoard(L, 0, "BG_BOT_312t");
        EndTurnOf(L, 0);
        int const add = g != 0 ? 4 : 2;
        CHECK(Find(L, 0, micro)->attack == 1 + add && Find(L, 0, micro)->health == 1 + add);
        CHECK(Find(L, 0, mon)->attack == Mn("BG23_353_Gt").attack);   // not a Mech
        CHECK(Find(L, 0, sensei)->attack == Mn("BG_GVG_027", g != 0).attack);
    }
    Lobby A(1, 2, MechReg());                                       // no other Mech: nothing
    uint32_t const mon = OnBoard(A, 0, "BG23_353_Gt");
    uint32_t const s = OnBoard(A, 0, "BG_GVG_027");
    EndTurnOf(A, 0);
    CHECK(Find(A, 0, mon)->attack == Mn("BG23_353_Gt").attack && Find(A, 0, s)->attack == 2);
}

static void TestDeflectOBot()
{
    int const golem = Cd("BG_EX1_556t");
    for (int g = 0; g < 2; ++g)
    {
        int ran = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            Minion d = With(Mn("BGS_071", g != 0), 3, 80);
            d.keywords = 0;                                         // lost its shield: it must gain one
            CombatResult r = Fight({ d, With(Mn("BG_EX1_556"), 1, 1) }, { Body(1, 6) }, seed);
            if (SummonedCards(r, golem) != 1)
                continue;
            ++ran;
            Minion const& s = Survivor(r, 0, 0);
            CHECK(s.card == Cd("BGS_071") && s.attack == 3 + (g != 0 ? 4 : 2) && s.Has(kKwDivineShield));
        }
        CHECK(ran > 0);
    }
    // a summoned non-Mech does not trigger it
    Minion d = With(Mn("BGS_071"), 3, 80);
    d.keywords = 0;
    CombatResult r = Fight({ d, With(Body(1, 1), 1, 1) }, { Body(1, 6) }, 1);
    CHECK(Triggers(r, 1, HookKind::FriendSummoned) == 0);
}

static void TestMechanoTank()
{
    for (int g = 0; g < 2; ++g)
    {
        int ran = 0;
        for (uint32_t seed = 1; seed <= 8; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG21_023", g != 0), 0, 100), Body(0, 1), Body(0, 1) },
                                   { Body(3, g != 0 ? 14 : 30), Body(3, 10) }, seed);
            std::vector<uint32_t> hits;
            bool avenged = false;
            for (CombatEvent const& e : r.events)
            {
                if (e.type == EventType::Trigger && e.actor == 1 && e.amount == static_cast<int>(HookKind::Avenge))
                    avenged = true;
                else if (avenged && e.type == EventType::Damage && e.actor == 1 && e.amount == 5 && hits.size() < 2)
                    hits.push_back(e.target);
            }
            if (!avenged)
                continue;
            ++ran;
            CHECK(!hits.empty() && hits[0] == 8);                   // the highest Health enemy
            if (g != 0)
                CHECK(hits.size() == 2 && hits[1] == 9);             // twice, re-evaluated: the other one is higher now
        }
        CHECK(ran > 0);
    }
}

static void TestHolyMecherel()
{
    for (int g = 0; g < 2; ++g)
    {
        int ran = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG20_401", g != 0), 1, 100), With(Mn("BG21_022"), 1, 100) }, { Body(1, 6) }, seed);
            bool popped = false;
            for (CombatEvent const& e : Events(r, EventType::ShieldPop))
                if (e.target == 2)
                    popped = true;
            if (!popped)
                continue;
            ++ran;
            bool gained = false;
            for (CombatEvent const& e : Events(r, EventType::Buff))
                if (e.target == 1 && (e.keywords & kKwDivineShield))
                    gained = true;
            CHECK(gained);
        }
        CHECK(ran > 0);
    }
    // its own Divine Shield does not give it a new one
    Minion m = With(Mn("BG20_401"), 1, 100);
    m.keywords |= kKwDivineShield;
    CombatResult r = Fight({ m }, { Body(1, 6) }, 1);
    CHECK(Events(r, EventType::ShieldPop).size() == 1);
    CHECK(Alive(r, 0) == 1 && !Survivor(r, 0, 0).Has(kKwDivineShield));
}

static void TestGreaseBot()
{
    for (int g = 0; g < 2; ++g)
    {
        int const add = g != 0 ? 4 : 2;
        int ran = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG21_024", g != 0), 1, 100), With(Mn("BG21_022"), 1, 100) }, { Body(1, 6) }, seed);
            bool popped = false;
            for (CombatEvent const& e : Events(r, EventType::ShieldPop))
                if (e.target == 2)
                    popped = true;
            if (!popped)
                continue;
            ++ran;
            bool found = false;
            for (PersistentChange const& c : r.persistent)
                if (c.kind == kPersistBuff && c.side == 0 && c.inputIndex == 1 && c.attack == add && c.health == add)
                    found = true;
            CHECK(found);                                            // permanent: written back to the shop board
        }
        CHECK(ran > 0);
        // it includes itself
        Minion m = With(Mn("BG21_024", g != 0), 1, 100);
        m.keywords |= kKwDivineShield;
        CombatResult r = Fight({ m }, { Body(1, 6) }, 1);
        bool self = false;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistBuff && c.inputIndex == 0 && c.attack == add && c.health == add)
                self = true;
        CHECK(self);
    }
    // through the game: the buff stays on the shop board after the round
    ManualGame G(5);
    for (int p = 0; p < kPlayers; ++p)
    {
        Minion gb = With(Mn("BG21_024", false, G.game.GetShop().NewUid()), 1, 60);
        gb.keywords |= kKwDivineShield;
        G.S(p).board.Push(gb);
    }
    G.EndRound();
    int buffed = 0;
    for (int p = 0; p < kPlayers; ++p)
        if (G.S(p).board.Size() >= 1 && G.S(p).board[0].attack >= 3)
            ++buffed;
    CHECK(buffed > 0);
}

static void TestShieldChain()
{
    // Grease Bot + Holy Mecherel: a Pupbot pops; Mecherel regains a shield (and Pupbot is buffed)
    int ran = 0;
    for (uint32_t seed = 1; seed <= 12; ++seed)
    {
        CombatResult r = Fight({ With(Mn("BG21_024"), 1, 100), With(Mn("BG20_401"), 1, 100), With(Mn("BG21_022"), 1, 100) },
                               { Body(1, 8) }, seed);
        bool popped = false;
        for (CombatEvent const& e : Events(r, EventType::ShieldPop))
            if (e.target == 3)
                popped = true;
        if (!popped)
            continue;
        ++ran;
        bool mech = false;
        for (CombatEvent const& e : Events(r, EventType::Buff))
            if (e.target == 2 && (e.keywords & kKwDivineShield))
                mech = true;
        CHECK(mech);
    }
    CHECK(ran > 0);
}

static void TestOmegaBuster()
{
    int const micro = Cd("BG_BOT_312t");
    for (int g = 0; g < 2; ++g)
    {
        CombatResult r = Fight({ With(Mn("BG21_025", g != 0), 1, 1) }, { Body(3, 50) }, 1);
        CHECK(SummonedCards(r, micro) == 6);                         // six (render text)
        for (CombatEvent const& e : Events(r, EventType::Summon))
            CHECK(e.golden == (g != 0) && e.amount == (g != 0 ? 2 : 1));
        // everything fits: nobody is buffed
        int buffs = 0;
        for (CombatEvent const& e : Events(r, EventType::Buff))
            if (e.amount2 >= 1)
                ++buffs;
        CHECK(buffs == 0);
    }
    // full board: 6 minions remain after its death, one slot free -> 5 fail
    for (int g = 0; g < 2; ++g)
    {
        Minion omega = With(Mn("BG21_025", g != 0), 1, 1);
        omega.keywords |= kKwTaunt;                                  // the enemy must hit it
        CombatResult r = Fight({ omega, BodyOf("BG_BOT_312t", 0, 100), With(Mn("BG21_022"), 0, 100), BodyOf("BG_BOT_312t", 0, 100),
                                 Body(0, 100), Body(0, 100), Body(0, 100) },
                               { Body(3, 50) }, 1);
        CHECK(SummonedCards(r, micro) == 1);
        int buffs = 0;
        for (CombatEvent const& e : Events(r, EventType::Buff))
            if (e.amount == 5 * (g != 0 ? 2 : 1) && e.amount2 == 5 * (g != 0 ? 2 : 1))
                ++buffs;
        CHECK(buffs == 4);                                           // two Microbots, Pupbot, and the new Microbot
    }
    // Khadgar doubles the attempts: 12 attempts, 6 free slots -> 6 failures
    Minion omega = With(Mn("BG21_025"), 1, 1);
    omega.keywords |= kKwTaunt;
    CombatResult k = FightWith(MechReg(), { Tough("BG_DAL_575", false), omega }, { Body(3, 50) }, 1);
    CHECK(SummonedCards(k, micro) == 6);
    int kb = 0;
    for (CombatEvent const& e : Events(k, EventType::Buff))
        if (e.amount == 6 && e.amount2 == 6)
            ++kb;
    CHECK(kb == 6);
}

static void TestFoeReaper4000()
{
    for (int g = 0; g < 2; ++g)
    {
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG_GVG_113", g != 0), 3, 40) }, { Body(0, 20), Body(0, 20), Body(0, 20) }, seed);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            CHECK(!atk.empty());
            if (atk.empty())
                continue;
            int const slot = static_cast<int>(atk[0].target) - 8;
            int const expected = 1 + (slot > 0 ? 1 : 0) + (slot < 2 ? 1 : 0);
            int dmg = 0;
            bool seen = false;
            for (CombatEvent const& e : r.events)
            {
                if (e.type == EventType::Attack)
                {
                    if (seen)
                        break;
                    seen = true;
                }
                if (e.type == EventType::Damage && e.side == 1)
                    ++dmg;
            }
            CHECK(dmg == expected);
        }
    }
    // Divine Shield aware: shields on the neighbours pop instead of taking damage
    CombatResult s = Fight({ With(Mn("BG_GVG_113"), 3, 40) }, { Body(0, 20, kKwDivineShield), Body(0, 20, kKwDivineShield) }, 1);
    CHECK(!Events(s, EventType::Attack).empty());
    int pops = 0;
    bool seen = false;
    for (CombatEvent const& e : s.events)
    {
        if (e.type == EventType::Attack)
        {
            if (seen)
                break;
            seen = true;
        }
        if (e.type == EventType::ShieldPop)
            ++pops;
    }
    CHECK(pops == 2);
}

static void RunMechTests()
{
    CHECK(EffectRegistry::Global().Errors().empty());
    RUN(TestPupbotWargearAnnoy);
    RUN(TestMicroMummy);
    RUN(TestKaboomBot);
    RUN(TestHarvestGolem);
    RUN(TestMetaltoothLeaper);
    RUN(TestReplicatingMenace);
    RUN(TestIronSensei);
    RUN(TestDeflectOBot);
    RUN(TestMechanoTank);
    RUN(TestHolyMecherel);
    RUN(TestGreaseBot);
    RUN(TestShieldChain);
    RUN(TestOmegaBuster);
    RUN(TestFoeReaper4000);
}
