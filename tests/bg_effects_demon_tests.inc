// Tests of the Demon effect batch (src/battlegrounds/HsBgEffectsDemon.cpp).
// Included by bg_effects_tests.cpp, which provides CHECK, RUN and the helpers (see its header comment). One test per
// minion and per golden version; write each as a static function and call it from RunDemonTests with RUN(fn).
// Only this file and HsBgEffectsDemon.cpp belong to the Demon batch.

namespace DemonTests
{
    // The 14 real Demon entries plus test-only Brann / Baron Rivendare / Khadgar (engine fields only), so the
    // interactions can be tested whatever other batches are linked.
    EffectRegistry const& DemonReg()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            for (char const* id : { "BG24_009", "BGS_014", "BG21_006", "BG23_357", "BG_AV_309", "BG21_039", "BG23_361", "BGS_204",
                                    "BG_DMF_533", "BG21_004", "BGS_010", "BG_LOOT_368", "BG21_005", "BGS_044" })
            {
                EffectDef const* e = EffectRegistry::Global().Find(FindCard(id));
                CHECK(e != nullptr);
                if (e)
                    r.Register(id, *e);
            }
            EffectDef d;
            d.battlecryTimes = 2;
            d.battlecryTimesGolden = 3;
            r.Register("BG_LOE_077", d);   // Brann
            d = EffectDef();
            d.deathrattleTimes = 2;
            d.deathrattleTimesGolden = 3;
            r.Register("BG_FP1_031", d);   // Baron Rivendare
            d = EffectDef();
            d.summonTimes = 2;
            d.summonTimesGolden = 3;
            r.Register("BG_DAL_575", d);   // Khadgar
            return r;
        }();
        return reg;
    }

    // A fresh minion of the card with its own stats overridden by (a, h) and the given golden flag.
    Minion Dm(char const* id, bool golden, int a, int h) { return With(Mn(id, golden), a, h); }

    // The survivor of a side that is of the card, or nullptr.
    Minion const* SurvivorOf(CombatResult const& r, int side, char const* id)
    {
        int const card = Cd(id);
        for (int i = 0; i < Alive(r, side); ++i)
            if (Survivor(r, side, i).card == card)
                return &Survivor(r, side, i);
        return nullptr;
    }

    // Eaten in the shop tests: Brann 2/4 (no effect in the lobby unless the test registry gives it one).
    constexpr int kEatA = 2;
    constexpr int kEatH = 4;

    // ------------------------------------------------------------------------------------------------ Picky Eater

    void TestPickyEater()
    {
        Lobby L(1, 2, DemonReg());
        NewOffers(L, 0, { "BG_LOE_077", "BG_LOE_077" });
        uint32_t const uid = PlayCard(L, 0, "BG24_009");
        Minion* m = Find(L, 0, uid);
        CHECK(m && m->attack == 1 + kEatA && m->health == 1 + kEatH && m->maxHealth == 1 + kEatH);
        CHECK(L.S(0).offers.size() == 1);            // exactly one offer was eaten
        std::string why;
        CHECK(L.shop.CheckConservation(&why));       // its pool copy went back
        // an empty tavern: nothing happens
        Lobby E(1, 2, DemonReg());
        NewOffers(E, 0, {});
        uint32_t const u2 = PlayCard(E, 0, "BG24_009");
        Minion* m2 = Find(E, 0, u2);
        CHECK(m2 && m2->attack == 1 && m2->health == 1);
    }

    void TestPickyEaterGolden()
    {
        Lobby L(1, 2, DemonReg());
        NewOffers(L, 0, { "BG_LOE_077" });
        uint32_t const uid = PlayCard(L, 0, "BG24_009", true);
        Minion* m = Find(L, 0, uid);
        CHECK(m && m->attack == 2 + 2 * kEatA && m->health == 2 + 2 * kEatH);
        CHECK(L.S(0).offers.empty());
        // Brann: the Battlecry fires twice, eating two minions
        Lobby B(1, 2, DemonReg());
        OnBoard(B, 0, "BG_LOE_077");
        NewOffers(B, 0, { "BG_LOE_077", "BG_LOE_077", "BG_LOE_077" });
        uint32_t const ub = PlayCard(B, 0, "BG24_009");
        Minion* mb = Find(B, 0, ub);
        CHECK(mb && mb->attack == 1 + 2 * kEatA && mb->health == 1 + 2 * kEatH);
        CHECK(B.S(0).offers.size() == 1);
    }

    // ------------------------------------------------------------------------------------------------ Imprisoner

    void TestImprisoner()
    {
        int const imp = Cd("BG_BRM_006t");
        CombatResult const r = FightWith(DemonReg(), { Dm("BGS_014", false, 2, 2) }, { Body(2, 2) }, 1);
        CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).card == imp);
        CHECK(Survivor(r, 0, 0).attack == 1 && Survivor(r, 0, 0).health == 1 && !Survivor(r, 0, 0).golden);
        CHECK(Alive(r, 1) == 0);
    }

    void TestImprisonerGolden()
    {
        int const imp = Cd("BG_BRM_006t");
        CombatResult const r = FightWith(DemonReg(), { Dm("BGS_014", true, 4, 4) }, { Body(4, 4) }, 1);
        CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).card == imp);
        CHECK(Survivor(r, 0, 0).golden && Survivor(r, 0, 0).attack == 2 && Survivor(r, 0, 0).health == 2);
        // Khadgar doubles the summon, Baron Rivendare triggers the Deathrattle again (Baron: two Imps per run set)
        CombatResult const k = FightWith(DemonReg(), { Dm("BGS_014", false, 2, 2), Mn("BG_DAL_575") }, { Body(2, 2) }, 1);
        CHECK(Alive(k, 0) == 3);
        CombatResult const b = FightWith(DemonReg(), { Dm("BGS_014", false, 2, 2), Mn("BG_FP1_031") }, { Body(2, 2) }, 1);
        CHECK(Alive(b, 0) == 3);
    }

    // ------------------------------------------------------------------------------------------------ Impulsive Trickster

    void TestTrickster()
    {
        // The taunting Trickster trades with the enemy whoever attacks first; the other friend gets its max Health.
        Minion t = Dm("BG21_006", false, 5, 3);
        t.keywords |= kKwTaunt;
        for (uint32_t seed = 1; seed <= 6; ++seed)
        {
            CombatResult const x = FightWith(DemonReg(), { t, Body(2, 2) }, { Body(3, 5) }, seed);
            CHECK(Alive(x, 0) == 1 && Alive(x, 1) == 0);
            CHECK(Survivor(x, 0, 0).attack == 2 && Survivor(x, 0, 0).health == 2 + 3);
        }
    }

    void TestTricksterGolden()
    {
        Minion t = Dm("BG21_006", true, 5, 3);
        t.keywords |= kKwTaunt;
        for (uint32_t seed = 1; seed <= 6; ++seed)
        {
            CombatResult const x = FightWith(DemonReg(), { t, Body(2, 2) }, { Body(3, 5) }, seed);
            CHECK(Alive(x, 0) == 1 && Survivor(x, 0, 0).health == 2 + 6);   // twice: both picks reach the only friend
        }
        // no other friendly minion: nothing to give, nothing breaks
        CombatResult const solo = FightWith(DemonReg(), { t }, { Body(3, 5) }, 1);
        CHECK(Alive(solo, 0) == 0);
    }

    // ------------------------------------------------------------------------------------------------ Mind Muck

    void TestMindMuck()
    {
        Lobby L(1, 2, DemonReg());
        uint32_t const imp = OnBoard(L, 0, "BG_BRM_006t");
        uint32_t const other = OnBoard(L, 0, "BG_EX1_103");
        NewOffers(L, 0, { "BG_LOE_077", "BG_LOE_077" });
        PlayCard(L, 0, "BG23_357", false, -1, imp);
        Minion* m = Find(L, 0, imp);
        CHECK(m && m->attack == 1 + kEatA && m->health == 1 + kEatH);
        CHECK(Find(L, 0, other)->attack == Mn("BG_EX1_103").attack);       // the non-Demon is untouched
        CHECK(L.S(0).offers.size() == 1);
        // an invalid target (a non-Demon) is re-rolled to a random Demon
        Lobby R(1, 2, DemonReg());
        uint32_t const imp2 = OnBoard(R, 0, "BG_BRM_006t");
        uint32_t const brann = OnBoard(R, 0, "BG_EX1_103");
        NewOffers(R, 0, { "BG_LOE_077" });
        PlayCard(R, 0, "BG23_357", false, -1, brann);
        CHECK(Find(R, 0, imp2)->attack == 1 + kEatA);
        // no Demon on the board: the Battlecry does nothing (it never targets itself)
        Lobby N(1, 2, DemonReg());
        NewOffers(N, 0, { "BG_LOE_077" });
        uint32_t const mm = PlayCard(N, 0, "BG23_357");
        CHECK(N.S(0).offers.size() == 1);
        CHECK(Find(N, 0, mm)->attack == Mn("BG23_357").attack);
    }

    void TestMindMuckGolden()
    {
        Lobby L(1, 2, DemonReg());
        uint32_t const imp = OnBoard(L, 0, "BG_BRM_006t");
        NewOffers(L, 0, { "BG_LOE_077" });
        PlayCard(L, 0, "BG23_357", true, -1, imp);
        Minion* m = Find(L, 0, imp);
        CHECK(m && m->attack == 1 + 2 * kEatA && m->health == 1 + 2 * kEatH);
    }

    // ------------------------------------------------------------------------------------------------ Piggyback Imp

    void TestPiggybackImp()
    {
        CombatResult const r = FightWith(DemonReg(), { Dm("BG_AV_309", false, 2, 2) }, { Body(2, 2) }, 1);
        CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).card == Cd("BG_AV_309t"));
        CHECK(Survivor(r, 0, 0).attack == 4 && Survivor(r, 0, 0).health == 1 && !Survivor(r, 0, 0).golden);
    }

    void TestPiggybackImpGolden()
    {
        CombatResult const r = FightWith(DemonReg(), { Dm("BG_AV_309", true, 4, 4) }, { Body(4, 4) }, 1);
        CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).card == Cd("BG_AV_309t"));
        CHECK(Survivor(r, 0, 0).golden && Survivor(r, 0, 0).attack == 8 && Survivor(r, 0, 0).health == 2);
    }

    // ------------------------------------------------------------------------------------------------ Kathra'natir

    void TestKathra()
    {
        // combat: the other Demons get +2 Attack, Kathra'natir and non-Demons do not
        CombatResult const r = FightWith(DemonReg(), { Mn("BG21_039"), Mn("BG_BRM_006t"), Body(1, 1) }, { Body(0, 1) }, 1);
        CHECK(Alive(r, 0) == 3);
        CHECK(Survivor(r, 0, 0).attack == Mn("BG21_039").attack);
        CHECK(Survivor(r, 0, 1).attack == 1 + 2);
        CHECK(Survivor(r, 0, 2).attack == 1);
        // shop: the aura follows the board, the hero is Immune to the player's own effects
        Lobby L(1, 2, DemonReg());
        uint32_t const imp = OnBoard(L, 0, "BG_BRM_006t");
        CHECK(Find(L, 0, imp)->attack == 1);
        uint32_t const k = OnBoard(L, 0, "BG21_039");
        CHECK(Find(L, 0, imp)->attack == 3 && Find(L, 0, k)->attack == Mn("BG21_039").attack);
        int const hp = L.S(0).health;
        L.shop.DamageHero(0, 5);
        CHECK(L.S(0).health == hp);
        L.S(0).board.Remove(L.S(0).board.IndexOfUid(k));
        L.shop.Settle(0);
        CHECK(Find(L, 0, imp)->attack == 1);
        L.shop.DamageHero(0, 5);
        CHECK(L.S(0).health == hp - 5);
    }

    void TestKathraGolden()
    {
        Lobby L(1, 2, DemonReg());
        uint32_t const imp = OnBoard(L, 0, "BG_BRM_006t");
        uint32_t const k = OnBoard(L, 0, "BG21_039", true);
        CHECK(Find(L, 0, imp)->attack == 1 + 4);
        CHECK(Find(L, 0, k)->attack == Mn("BG21_039", true).attack);
        int const hp = L.S(0).health;
        L.shop.DamageHero(0, 3);
        CHECK(L.S(0).health == hp);
    }

    // ------------------------------------------------------------------------------------------------ Legion Overseer

    void TestLegionOverseer()
    {
        Lobby L(1, 2, DemonReg());
        NewOffers(L, 0, { "BG_LOE_077", "BG_LOE_077" });
        CHECK(L.S(0).offers[0].attack == kEatA && L.S(0).offers[0].health == kEatH);
        OnBoard(L, 0, "BG23_361");
        CHECK(L.S(0).offers[0].attack == kEatA + 2 && L.S(0).offers[0].health == kEatH + 1);
        OnBoard(L, 0, "BG23_361");                                  // two Overseers add up
        CHECK(L.S(0).offers[1].attack == kEatA + 4 && L.S(0).offers[1].health == kEatH + 2);
        L.S(0).board.Remove(1);
        L.shop.Settle(0);
        // a bought minion keeps the tavern bonus (one Overseer is left: +2/+1)
        SetGold(L, 0, 10);
        CHECK(L.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(L.S(0).hand.Size() == 1 && L.S(0).hand[0].attack == kEatA + 2 && L.S(0).hand[0].health == kEatH + 1);
        // an eaten one counts with the bonus
        uint32_t const eater = OnBoard(L, 0, "BG_BRM_006t");
        uint32_t const mm = PlayCard(L, 0, "BG23_357", false, -1, eater);
        CHECK(Find(L, 0, eater)->attack == 1 + kEatA + 2 && Find(L, 0, eater)->health == 1 + kEatH + 1);
        (void)mm;
    }

    void TestLegionOverseerGolden()
    {
        Lobby L(1, 2, DemonReg());
        NewOffers(L, 0, { "BG_LOE_077" });
        OnBoard(L, 0, "BG23_361", true);
        CHECK(L.S(0).offers[0].attack == kEatA + 4 && L.S(0).offers[0].health == kEatH + 2);
    }

    // ------------------------------------------------------------------------------------------------ Bigfernal

    void TestBigfernal()
    {
        // shop: playing a Demon is summoning it; Bigfernal itself and non-Demons do not count
        Lobby L(1, 2, DemonReg());
        PlayCard(L, 0, "BGS_204");
        uint32_t const b = L.S(0).board[0].uid;
        Minion const base = Mn("BGS_204");
        CHECK(Find(L, 0, b)->attack == base.attack && Find(L, 0, b)->health == base.health);
        PlayCard(L, 0, "BG_BRM_006t");
        CHECK(Find(L, 0, b)->attack == base.attack + 1 && Find(L, 0, b)->health == base.health + 1);
        PlayCard(L, 0, "BG_LOE_077");
        CHECK(Find(L, 0, b)->attack == base.attack + 1);
        // combat: the summoned Imp feeds it, and the gain persists
        Minion imprisoner = Dm("BGS_014", false, 1, 1);
        CombatResult const r = FightWith(DemonReg(), { imprisoner, Mn("BGS_204") }, { Body(1, 1) }, 1);
        Minion const* big = SurvivorOf(r, 0, "BGS_204");
        CHECK(big && big->attack == base.attack + 1 && big->health == base.health + 1);
        int found = 0;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistBuff && c.inputIndex == 1 && c.attack == 1 && c.health == 1)
                ++found;
        CHECK(found == 1);
    }

    void TestBigfernalGolden()
    {
        Lobby L(1, 2, DemonReg());
        PlayCard(L, 0, "BGS_204", true);
        uint32_t const b = L.S(0).board[0].uid;
        Minion const base = Mn("BGS_204", true);
        L.S(0).options.clear();                       // close the Triple Reward Discover of the golden play
        L.S(0).queue.clear();
        PlayCard(L, 0, "BG_BRM_006t");
        CHECK(Find(L, 0, b)->attack == base.attack + 2 && Find(L, 0, b)->health == base.health + 2);
        Minion imprisoner = Dm("BGS_014", false, 1, 1);
        CombatResult const r = FightWith(DemonReg(), { imprisoner, Mn("BGS_204", true) }, { Body(1, 1) }, 1);
        Minion const* big = SurvivorOf(r, 0, "BGS_204");
        CHECK(big && big->attack == base.attack + 2 && big->health == base.health + 2);
    }

    // ------------------------------------------------------------------------------------------------ Ring Matron

    void TestRingMatron()
    {
        CombatResult const r = FightWith(DemonReg(), { Dm("BG_DMF_533", false, 1, 1) }, { Body(1, 1) }, 1);
        CHECK(Alive(r, 0) == 2);
        for (int i = 0; i < Alive(r, 0); ++i)
            CHECK(Survivor(r, 0, i).card == Cd("BG_DMF_533t") && Survivor(r, 0, i).attack == 3 && Survivor(r, 0, i).health == 2);
        CHECK(SummonedCards(r, Cd("BG_DMF_533t")) == 2);
        // Khadgar doubles each summon: four Imps and Khadgar
        CombatResult const k = FightWith(DemonReg(), { Dm("BG_DMF_533", false, 1, 1), Mn("BG_DAL_575") }, { Body(1, 1) }, 1);
        CHECK(SummonedCards(k, Cd("BG_DMF_533t")) == 4);
    }

    void TestRingMatronGolden()
    {
        CombatResult const r = FightWith(DemonReg(), { Dm("BG_DMF_533", true, 1, 1) }, { Body(1, 1) }, 1);
        CHECK(Alive(r, 0) == 2);
        for (int i = 0; i < Alive(r, 0); ++i)
            CHECK(Survivor(r, 0, i).golden && Survivor(r, 0, i).attack == 6 && Survivor(r, 0, i).health == 4);
    }

    // ------------------------------------------------------------------------------------------------ Insatiable Ur'zul

    void TestUrzul()
    {
        Lobby L(1, 2, DemonReg());
        uint32_t const u = PlayCard(L, 0, "BG21_004");
        Minion const base = Mn("BG21_004");
        NewOffers(L, 0, { "BG_LOE_077", "BG_LOE_077" });
        CHECK(Find(L, 0, u)->attack == base.attack);              // its own play does not count
        PlayCard(L, 0, "BG_LOE_077");                              // a non-Demon: nothing
        CHECK(Find(L, 0, u)->attack == base.attack && L.S(0).offers.size() == 2);
        PlayCard(L, 0, "BG_BRM_006t");                             // a Demon: one minion is eaten
        CHECK(Find(L, 0, u)->attack == base.attack + kEatA && Find(L, 0, u)->health == base.health + kEatH);
        CHECK(L.S(0).offers.size() == 1);
        // an empty tavern: nothing
        NewOffers(L, 0, {});
        PlayCard(L, 0, "BG_CS2_065");
        CHECK(Find(L, 0, u)->attack == base.attack + kEatA);
    }

    void TestUrzulGolden()
    {
        Lobby L(1, 2, DemonReg());
        uint32_t const u = OnBoard(L, 0, "BG21_004", true);
        Minion const base = Mn("BG21_004", true);
        NewOffers(L, 0, { "BG_LOE_077" });
        PlayCard(L, 0, "BG_BRM_006t");
        CHECK(Find(L, 0, u)->attack == base.attack + 2 * kEatA && Find(L, 0, u)->health == base.health + 2 * kEatH);
    }

    // ------------------------------------------------------------------------------------------------ Annihilan Battlemaster

    void TestAnnihilan()
    {
        Minion const base = Mn("BGS_010");
        Lobby L(1, 2, DemonReg());
        uint32_t const full = PlayCard(L, 0, "BGS_010");           // unhurt hero: nothing
        CHECK(Find(L, 0, full)->health == base.health);
        Lobby H(1, 2, DemonReg());
        H.S(0).health = 30;
        uint32_t const a = PlayCard(H, 0, "BGS_010");
        CHECK(Find(H, 0, a)->health == base.health + 20 && Find(H, 0, a)->maxHealth == base.health + 20);   // +2 per missing
        CHECK(Find(H, 0, a)->attack == base.attack);
        // Brann: the second run adds the same again
        Lobby B(1, 2, DemonReg());
        OnBoard(B, 0, "BG_LOE_077");
        B.S(0).health = 36;
        uint32_t const ab = PlayCard(B, 0, "BGS_010");
        CHECK(Find(B, 0, ab)->health == base.health + 16);
    }

    void TestAnnihilanGolden()
    {
        Lobby L(1, 2, DemonReg());
        L.S(0).health = 30;
        uint32_t const a = PlayCard(L, 0, "BGS_010", true);
        Minion const base = Mn("BGS_010", true);
        CHECK(Find(L, 0, a)->health == base.health + 40);
    }

    // ------------------------------------------------------------------------------------------------ Voidlord

    void TestVoidlord()
    {
        CombatResult const r = FightWith(DemonReg(), { Dm("BG_LOOT_368", false, 1, 1) }, { Body(1, 1) }, 1);
        CHECK(Alive(r, 0) == 3);
        for (int i = 0; i < Alive(r, 0); ++i)
        {
            Minion const& m = Survivor(r, 0, i);
            CHECK(m.card == Cd("BG_CS2_065") && m.attack == 1 && m.health == 3 && m.Has(kKwTaunt) && !m.golden);
        }
        // Baron Rivendare: the Deathrattle runs twice, six Walkers (the board holds 7: Baron plus six)
        CombatResult const b = FightWith(DemonReg(), { Dm("BG_LOOT_368", false, 1, 1), Mn("BG_FP1_031") }, { Body(1, 1) }, 1);
        CHECK(SummonedCards(b, Cd("BG_CS2_065")) == 6);
        // Khadgar on a full board: the summons beyond the cap are lost
        CombatResult const k = FightWith(DemonReg(), { Dm("BG_LOOT_368", false, 1, 1), Mn("BG_DAL_575"), Mn("BG_DAL_575") }, { Body(1, 1) }, 1);
        CHECK(Alive(k, 0) <= 7);
    }

    void TestVoidlordGolden()
    {
        CombatResult const r = FightWith(DemonReg(), { Dm("BG_LOOT_368", true, 1, 1) }, { Body(1, 1) }, 1);
        CHECK(Alive(r, 0) == 3);
        for (int i = 0; i < Alive(r, 0); ++i)
            CHECK(Survivor(r, 0, i).golden && Survivor(r, 0, i).attack == 2 && Survivor(r, 0, i).health == 6);
    }

    // ------------------------------------------------------------------------------------------------ Famished Felbat

    void TestFelbat()
    {
        Lobby L(1, 2, DemonReg());
        L.shop.StartTurn(0, 1);
        uint32_t const f = OnBoard(L, 0, "BG21_005");
        uint32_t const imp = OnBoard(L, 0, "BG_BRM_006t");
        uint32_t const non = OnBoard(L, 0, "BG_LOE_077");
        NewOffers(L, 0, { "BG_LOE_077", "BG_LOE_077", "BG_LOE_077" });
        Minion const base = Mn("BG21_005");
        CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
        CHECK(Find(L, 0, f)->attack == base.attack + kEatA && Find(L, 0, f)->health == base.health + kEatH);
        CHECK(Find(L, 0, imp)->attack == 1 + kEatA && Find(L, 0, imp)->health == 1 + kEatH);
        CHECK(Find(L, 0, non)->attack == 2);                       // not a Demon
        CHECK(L.S(0).offers.size() == 1);
        // fewer offers than Demons: it stops when the tavern is empty
        Lobby S(1, 2, DemonReg());
        S.shop.StartTurn(0, 1);
        uint32_t const f2 = OnBoard(S, 0, "BG21_005");
        uint32_t const i2 = OnBoard(S, 0, "BG_BRM_006t");
        NewOffers(S, 0, { "BG_LOE_077" });
        S.shop.EndTurn(0);
        CHECK(Find(S, 0, f2)->attack + Find(S, 0, i2)->attack == base.attack + 1 + kEatA);
        CHECK(S.S(0).offers.empty());
    }

    void TestFelbatGolden()
    {
        Lobby L(1, 2, DemonReg());
        L.shop.StartTurn(0, 1);
        uint32_t const f = OnBoard(L, 0, "BG21_005", true);
        uint32_t const imp = OnBoard(L, 0, "BG_BRM_006t");
        NewOffers(L, 0, { "BG_LOE_077", "BG_LOE_077" });
        Minion const base = Mn("BG21_005", true);
        L.shop.EndTurn(0);
        CHECK(Find(L, 0, f)->attack == base.attack + 2 * kEatA && Find(L, 0, f)->health == base.health + 2 * kEatH);
        CHECK(Find(L, 0, imp)->attack == 1 + 2 * kEatA);
        CHECK(L.S(0).offers.empty());
    }

    // ------------------------------------------------------------------------------------------------ Imp Mama

    // Runs seeds until Imp Mama took (non-lethal) damage; calls check on every such fight and returns how many there were.
    template <class Check> int MamaFights(bool golden, std::vector<Minion> const& extra, Check check)
    {
        int hits = 0;
        for (uint32_t seed = 1; seed <= 24; ++seed)
        {
            std::vector<Minion> a = { Dm("BGS_044", golden, 1, 10) };
            for (Minion const& m : extra)
                a.push_back(m);
            CombatResult const r = FightWith(DemonReg(), a, { Body(1, 1) }, seed);
            Minion const* mama = SurvivorOf(r, 0, "BGS_044");
            if (!mama || mama->health >= mama->maxHealth)
                continue;
            ++hits;
            check(r);
        }
        return hits;
    }

    void TestImpMama()
    {
        int const hits = MamaFights(false, {}, [](CombatResult const& r)
        {
            CHECK(Alive(r, 0) == 2);                                // Mama and one Demon
            Minion const& d = Survivor(r, 0, 1);
            CHECK(d.IsTribe(kTribeDemon) && d.card != Cd("BGS_044") && d.Has(kKwTaunt) && !d.golden);
        });
        CHECK(hits > 0);
        // a Divine Shield hit is not damage
        Minion shielded = Dm("BGS_044", false, 1, 10);
        shielded.keywords |= kKwDivineShield;
        CombatResult const r = FightWith(DemonReg(), { shielded }, { Body(1, 1) }, 1);
        CHECK(Alive(r, 0) == 1);
        // Khadgar: the summon is doubled
        int const k = MamaFights(false, { Mn("BG_DAL_575") }, [](CombatResult const& r2)
        {
            CHECK(Alive(r2, 0) == 4);
            CHECK(Survivor(r2, 0, 1).Has(kKwTaunt) && Survivor(r2, 0, 2).Has(kKwTaunt));
        });
        CHECK(k > 0);
    }

    void TestImpMamaGolden()
    {
        int const hits = MamaFights(true, {}, [](CombatResult const& r)
        {
            CHECK(Alive(r, 0) == 3);                                // Mama and two Demons
            for (int i = 1; i < 3; ++i)
                CHECK(Survivor(r, 0, i).IsTribe(kTribeDemon) && Survivor(r, 0, i).Has(kKwTaunt));
        });
        CHECK(hits > 0);
    }

    // ------------------------------------------------------------------------------------------------ registry

    void TestDemonRegistry()
    {
        EffectRegistry const& g = EffectRegistry::Global();
        CHECK(g.Errors().empty());
        for (char const* id : { "BG24_009", "BGS_014", "BG21_006", "BG23_357", "BG_AV_309", "BG21_039", "BG23_361", "BGS_204",
                                "BG_DMF_533", "BG21_004", "BGS_010", "BG_LOOT_368", "BG21_005", "BGS_044" })
            CHECK(g.Find(Cd(id)) != nullptr);
        // the engine-read fields
        EffectDef const* k = g.Find(Cd("BG21_039"));
        CHECK(k && k->heroImmune && k->aura);
        EffectDef const* l = g.Find(Cd("BG23_361"));
        CHECK(l && l->tavernAttack == 2 && l->tavernHealth == 1 && l->tavernAttackGolden == 4 && l->tavernHealthGolden == 2);
    }
}

static void RunDemonTests()
{
    using namespace DemonTests;
    RUN(TestDemonRegistry);
    RUN(TestPickyEater);
    RUN(TestPickyEaterGolden);
    RUN(TestImprisoner);
    RUN(TestImprisonerGolden);
    RUN(TestTrickster);
    RUN(TestTricksterGolden);
    RUN(TestMindMuck);
    RUN(TestMindMuckGolden);
    RUN(TestPiggybackImp);
    RUN(TestPiggybackImpGolden);
    RUN(TestKathra);
    RUN(TestKathraGolden);
    RUN(TestLegionOverseer);
    RUN(TestLegionOverseerGolden);
    RUN(TestBigfernal);
    RUN(TestBigfernalGolden);
    RUN(TestRingMatron);
    RUN(TestRingMatronGolden);
    RUN(TestUrzul);
    RUN(TestUrzulGolden);
    RUN(TestAnnihilan);
    RUN(TestAnnihilanGolden);
    RUN(TestVoidlord);
    RUN(TestVoidlordGolden);
    RUN(TestFelbat);
    RUN(TestFelbatGolden);
    RUN(TestImpMama);
    RUN(TestImpMamaGolden);
}
