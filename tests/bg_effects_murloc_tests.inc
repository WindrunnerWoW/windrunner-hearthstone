// Tests of the Murloc effect batch (src/battlegrounds/HsBgEffectsMurloc.cpp).
// Included by bg_effects_tests.cpp, which provides CHECK, RUN and the helpers (see its header comment). One test per
// minion and per golden version; write each as a static function and call it from RunMurlocTests with RUN(fn).
// Only this file and HsBgEffectsMurloc.cpp belong to the Murloc batch.
//
// Murlocs are only in the pool when the lobby drew their tribe, so every shop test uses MurSeed(), a seed whose lobby has
// them. Murk-Eye (BG22_403) is the "inert Murloc": it has no Battlecry and no aura, so playing it only triggers the
// Murloc reactions under test.

// Lobby seeds whose pool contains the Murlocs (Rng based, so the search is deterministic); nth picks another one.
static uint32_t MurSeed(int nth = 0)
{
    static std::vector<uint32_t> found;
    for (uint32_t s = 1; s < 500 && static_cast<int>(found.size()) <= nth; ++s)
    {
        if (!found.empty() && s <= found.back())
            continue;
        Lobby L(s, 2);
        if (L.pool.Has(Cd("BG22_403")) && L.pool.Has(Cd("BG22_202")) && L.pool.Has(Cd("BGS_020")))
            found.push_back(s);
    }
    CHECK(static_cast<int>(found.size()) > nth);
    return static_cast<int>(found.size()) > nth ? found[static_cast<std::size_t>(nth)] : 1;
}

// Attack / maxHealth of a board minion above its card's own numbers.
static int MurAtk(Lobby& L, uint32_t uid, int p = 0)
{
    Minion* m = Find(L, p, uid);
    CHECK(m != nullptr);
    return m ? m->attack - MakeMinion(m->card, m->golden).attack : 0;
}
static int MurHp(Lobby& L, uint32_t uid, int p = 0)
{
    Minion* m = Find(L, p, uid);
    CHECK(m != nullptr);
    return m ? m->maxHealth - MakeMinion(m->card, m->golden).maxHealth : 0;
}

// The survivor of a side with this card, or nullptr.
static Minion const* MurSurvivor(CombatResult const& r, int side, char const* id)
{
    for (int i = 0; i < Alive(r, side); ++i)
        if (Survivor(r, side, i).card == Cd(id))
            return &Survivor(r, side, i);
    return nullptr;
}

// A private registry with Brann (and the Murloc entries the test needs), to test Battlecry repeats without the Neutral batch.
static EffectRegistry const& MurBrannReg()
{
    static EffectRegistry const reg = []
    {
        EffectRegistry r;
        for (char const* id : { "BG21_010", "BG_EX1_103", "BG22_403", "BG_UNG_073", "BG_DAL_077", "BG_BT_010" })
        {
            EffectDef const* e = EffectRegistry::Global().Find(Cd(id));
            CHECK(e != nullptr);
            if (e)
                r.Register(id, *e);
        }
        EffectDef brann;
        brann.battlecryTimes = 2;
        brann.battlecryTimesGolden = 3;
        r.Register("BG_LOE_077", brann);
        return r;
    }();
    return reg;
}

static void MurlocRegistryEntries()
{
    EffectRegistry const& g = EffectRegistry::Global();
    for (char const* id : { "BG22_401", "BG_UNG_073", "BG21_008", "BG22_202", "BG_EX1_507", "BG21_010", "BG_BT_010", "BG_EX1_103",
                            "BGS_020", "BG_DAL_077", "BGS_030", "BG22_403" })
        CHECK(g.Find(Cd(id)) != nullptr);
    CHECK(g.Errors().empty());
}

// ------------------------------------------------------------------------------------------------ Swampstriker

static void TestSwampstrikerShop()
{
    Lobby L(MurSeed(), 2);
    uint32_t const s = OnBoard(L, 0, "BG22_401");
    CHECK(MurAtk(L, s) == 0);
    PlayCard(L, 0, "BG22_403");                         // a played Murloc counts as a summon
    CHECK(MurAtk(L, s) == 1);
    PlayCard(L, 0, "BG_EX1_534t");                      // a Beast does not
    CHECK(MurAtk(L, s) == 1);
    PlayCard(L, 0, "BG_GIL_681");                       // the Nightmare Amalgam is a Murloc
    CHECK(MurAtk(L, s) == 2);
    CHECK(L.shop.SummonToBoard(0, Cd("BG22_403"), false, 0) != 0);   // a summon as well
    CHECK(MurAtk(L, s) == 3);
    CHECK(MurHp(L, s) == 0);
    // it does not trigger for itself
    Lobby A(MurSeed(), 2);
    uint32_t const alone = PlayCard(A, 0, "BG22_401");
    CHECK(MurAtk(A, alone) == 0);
}

static void TestSwampstrikerShopGolden()
{
    Lobby L(MurSeed(), 2);
    uint32_t const s = OnBoard(L, 0, "BG22_401", true);
    PlayCard(L, 0, "BG22_403");
    CHECK(MurAtk(L, s) == 2);
    PlayCard(L, 0, "BG_EX1_534t");
    CHECK(MurAtk(L, s) == 2);
}

// Reborn returns a Murloc during combat: each return is a summon. The Striker is big so it lives to the end.
static void MurSwampstrikerFights(bool golden)
{
    int sawReturn = 0;
    for (uint32_t seed = 1; seed <= 30; ++seed)
    {
        Minion striker = With(Mn("BG22_401", golden), 1, 100);
        Minion murloc = With(Mn("BG22_403"), 1, 1);
        murloc.keywords |= kKwReborn;
        CombatResult const r = Fight({ striker, murloc }, { Body(1, 6) }, seed);
        Minion const* s = MurSurvivor(r, 0, "BG22_401");
        CHECK(s != nullptr);
        if (!s)
            continue;
        int const returns = static_cast<int>(Events(r, EventType::Reborn).size());
        CHECK(returns <= 1);
        CHECK(s->attack == 1 + returns * (golden ? 2 : 1));
        sawReturn += returns;
    }
    CHECK(sawReturn > 0);
}
static void TestSwampstrikerCombat() { MurSwampstrikerFights(false); }
static void TestSwampstrikerCombatGolden() { MurSwampstrikerFights(true); }

// ------------------------------------------------------------------------------------------------ Rockpool Hunter

static void TestRockpoolHunter()
{
    Lobby L(MurSeed(), 2);
    uint32_t const a = OnBoard(L, 0, "BG22_403");
    uint32_t const b = OnBoard(L, 0, "BG_DAL_077");
    uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
    PlayCard(L, 0, "BG_UNG_073", false, -1, b);         // the chosen Murloc
    CHECK(MurAtk(L, b) == 1 && MurHp(L, b) == 1);
    CHECK(MurAtk(L, a) == 0 && MurHp(L, a) == 0);
    // an invalid target (a Beast) is replaced by a random other Murloc (a, b or the first Hunter), never the second Hunter
    uint32_t const hunter1 = L.S(0).board[L.S(0).board.Size() - 1].uid;
    uint32_t const hunter2 = PlayCard(L, 0, "BG_UNG_073", false, -1, beast);
    CHECK(MurAtk(L, beast) == 0 && MurHp(L, beast) == 0);
    CHECK(MurAtk(L, hunter2) == 0 && MurHp(L, hunter2) == 0);
    int gained = 0;
    for (uint32_t uid : { a, b, hunter1 })
        gained += MurAtk(L, uid) + MurHp(L, uid);
    CHECK(gained == 4);                                 // b already had +1/+1, one more +1/+1 landed on one of the three
}

static void TestRockpoolHunterNoMurloc()
{
    Lobby L(MurSeed(), 2);
    uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const hunter = PlayCard(L, 0, "BG_UNG_073");   // plays only for its stats: nothing to buff
    CHECK(MurAtk(L, beast) == 0 && MurHp(L, beast) == 0);
    CHECK(MurAtk(L, hunter) == 0 && MurHp(L, hunter) == 0); // never targets itself
}

static void TestRockpoolHunterGolden()
{
    Lobby L(MurSeed(), 2);
    uint32_t const a = OnBoard(L, 0, "BG22_403");
    PlayCard(L, 0, "BG_UNG_073", true, -1, a);
    CHECK(MurAtk(L, a) == 2 && MurHp(L, a) == 2);
    // the Amalgam is a valid Murloc target
    Lobby A(MurSeed(), 2);
    uint32_t const am = OnBoard(A, 0, "BG_GIL_681");
    PlayCard(A, 0, "BG_UNG_073", false, -1, am);
    CHECK(MurAtk(A, am) == 1 && MurHp(A, am) == 1);
}

// ------------------------------------------------------------------------------------------------ Saltscale Honcho

static int MurHealthGains(Lobby& L, std::initializer_list<uint32_t> uids, int& most)
{
    int sum = 0;
    most = 0;
    for (uint32_t uid : uids)
    {
        int const h = MurHp(L, uid);
        sum += h;
        most = std::max(most, h);
    }
    return sum;
}

static void MurHoncho(bool golden)
{
    int const per = golden ? 2 : 1;
    for (int extra = 0; extra < 4; ++extra)              // vary the random picks
    {
        Lobby L(MurSeed(extra), 2);
        uint32_t const h = OnBoard(L, 0, "BG21_008", golden);
        uint32_t const a = OnBoard(L, 0, "BG22_403");
        uint32_t const b = OnBoard(L, 0, "BGS_020");
        uint32_t const c = OnBoard(L, 0, "BG21_010");
        uint32_t const played = PlayCard(L, 0, "BG_DAL_077");   // Toxfin: a Murloc, its Battlecry changes no health
        int most = 0;
        int const sum = MurHealthGains(L, { a, b, c, played }, most);
        CHECK(sum == 2 * per);                           // exactly two Murlocs, +1 Health each (may include the played one)
        CHECK(most == per);                              // two different ones
        CHECK(MurHp(L, h) == 0);                         // never itself
    }
}
static void TestSaltscaleHoncho() { MurHoncho(false); }
static void TestSaltscaleHonchoGolden() { MurHoncho(true); }

static void TestSaltscaleHonchoEdges()
{
    Lobby L(MurSeed(), 2);
    uint32_t const h = OnBoard(L, 0, "BG21_008");
    uint32_t const a = OnBoard(L, 0, "BG22_403");
    PlayCard(L, 0, "BG_EX1_534t");                      // not a Murloc: nothing
    CHECK(MurHp(L, a) == 0);
    PlayCard(L, 0, "BG_GIL_681");                       // the Amalgam is a Murloc: two others (a and the Amalgam)
    CHECK(MurHp(L, a) == 1);
    CHECK(MurHp(L, h) == 0);
    // only one other Murloc: only that one
    Lobby S(MurSeed(), 2);
    OnBoard(S, 0, "BG21_008");
    uint32_t const lone = PlayCard(S, 0, "BG22_403");
    CHECK(MurHp(S, lone) == 1);
}

// ------------------------------------------------------------------------------------------------ Tad

static void TestTad()
{
    Lobby L(MurSeed(), 2);
    OnBoard(L, 0, "BG22_202");
    int const gold = L.S(0).gold;
    CHECK(L.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
    CHECK(L.S(0).board.Size() == 0);
    CHECK(L.S(0).gold == gold + 1);
    CHECK(L.S(0).hand.Size() == 1);
    if (L.S(0).hand.Size() == 1)
    {
        CHECK(L.S(0).hand[0].IsTribe(kTribeMurloc));
        CHECK(L.S(0).hand[0].card != Cd("BG22_202"));    // "another" Murloc
        CHECK(L.S(0).hand[0].poolCopies == 1);           // a real pool copy
    }
}

static void TestTadGolden()
{
    Lobby L(MurSeed(), 2);
    OnBoard(L, 0, "BG22_202", true);
    CHECK(L.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
    CHECK(L.S(0).hand.Size() == 2);
    for (int i = 0; i < L.S(0).hand.Size(); ++i)
        CHECK(L.S(0).hand[i].IsTribe(kTribeMurloc) && L.S(0).hand[i].card != Cd("BG22_202"));
}

static void TestTadFullHand()
{
    Lobby L(MurSeed(), 2);
    for (char const* id : { "BG_EX1_534t", "BG_CFM_315t", "BG_CFM_316t", "BG_BOT_312t", "BG_BRM_006t", "BG19_010t", "BG22_001t2",
                            "BG_EX1_556t", "BG_DMF_533t", "BG_AV_309t" })
        InHand(L, 0, id);
    CHECK(L.S(0).hand.Size() == 10);
    OnBoard(L, 0, "BG22_202");
    CHECK(L.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
    CHECK(L.S(0).board.Size() == 0);
    CHECK(L.S(0).hand.Size() == 10);                    // the Murloc was lost
}

// ------------------------------------------------------------------------------------------------ Murloc Warleader

static void TestMurlocWarleader()
{
    Lobby L(MurSeed(), 2);
    uint32_t const w = OnBoard(L, 0, "BG_EX1_507");
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const am = OnBoard(L, 0, "BG_GIL_681");
    CHECK(MurAtk(L, w) == 0);                           // "other"
    CHECK(MurAtk(L, m) == 2);
    CHECK(MurAtk(L, beast) == 0);
    CHECK(MurAtk(L, am) == 2);                          // the Amalgam is a Murloc
    CHECK(MurHp(L, m) == 0);
    // two Warleaders buff each other and everyone twice
    uint32_t const w2 = OnBoard(L, 0, "BG_EX1_507");
    CHECK(MurAtk(L, w) == 2 && MurAtk(L, w2) == 2 && MurAtk(L, m) == 4);
    // selling it removes the bonus
    CHECK(L.shop.Sell(0, Zone::Board, 4) == ActionResult::Ok);
    CHECK(MurAtk(L, m) == 2);
}

static void TestMurlocWarleaderGolden()
{
    Lobby L(MurSeed(), 2);
    uint32_t const w = OnBoard(L, 0, "BG_EX1_507", true);
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    CHECK(MurAtk(L, w) == 0);
    CHECK(MurAtk(L, m) == 4);
}

static void TestMurlocWarleaderCombat()
{
    // the aura works in combat as well: a 1-attack Murloc next to the Warleader kills a 3-health body in one hit
    Minion warleader = With(Mn("BG_EX1_507"), 0, 5);
    Minion murloc = With(Mn("BG22_403"), 1, 5);
    CombatResult const r = Fight({ warleader, murloc }, { Body(0, 3) }, 1);
    CHECK(Alive(r, 1) == 0);
}

// ------------------------------------------------------------------------------------------------ Swolefin

static void TestSwolefin()
{
    Lobby L(MurSeed(), 2);
    OnBoard(L, 0, "BG22_403");
    OnBoard(L, 0, "BGS_020");
    OnBoard(L, 0, "BG_EX1_534t");                       // a Beast does not count
    uint32_t const sw = PlayCard(L, 0, "BG21_010");
    CHECK(MurAtk(L, sw) == 4 && MurHp(L, sw) == 2);     // two other Murlocs: +4/+2
    Lobby E(MurSeed(), 2);
    uint32_t const alone = PlayCard(E, 0, "BG21_010");
    CHECK(MurAtk(E, alone) == 0 && MurHp(E, alone) == 0);
    Lobby A(MurSeed(), 2);
    OnBoard(A, 0, "BG_GIL_681");
    uint32_t const withAm = PlayCard(A, 0, "BG21_010");
    CHECK(MurAtk(A, withAm) == 2 && MurHp(A, withAm) == 1);
}

static void TestSwolefinGolden()
{
    Lobby L(MurSeed(), 2);
    OnBoard(L, 0, "BG22_403");
    OnBoard(L, 0, "BGS_020");
    uint32_t const sw = PlayCard(L, 0, "BG21_010", true);
    CHECK(MurAtk(L, sw) == 8 && MurHp(L, sw) == 4);
}

static void TestSwolefinBrann()
{
    Lobby L(MurSeed(), 2, MurBrannReg());
    OnBoard(L, 0, "BG_LOE_077");
    OnBoard(L, 0, "BG22_403");
    uint32_t const sw = PlayCard(L, 0, "BG21_010");
    CHECK(MurAtk(L, sw) == 4 && MurHp(L, sw) == 2);     // one other Murloc, the Battlecry fires twice
}

// ------------------------------------------------------------------------------------------------ Felfin / Coldlight

static void MurTeam(char const* id, bool golden, int a, int h)
{
    Lobby L(MurSeed(), 2);
    uint32_t const m1 = OnBoard(L, 0, "BG22_403");
    uint32_t const m2 = OnBoard(L, 0, "BGS_020");
    uint32_t const am = OnBoard(L, 0, "BG_GIL_681");
    uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const self = PlayCard(L, 0, id, golden);
    for (uint32_t uid : { m1, m2, am })
        CHECK(MurAtk(L, uid) == a && MurHp(L, uid) == h);
    CHECK(MurAtk(L, beast) == 0 && MurHp(L, beast) == 0);
    CHECK(MurAtk(L, self) == 0 && MurHp(L, self) == 0);  // "other" Murlocs
}
static void TestFelfinNavigator() { MurTeam("BG_BT_010", false, 1, 1); }
static void TestFelfinNavigatorGolden() { MurTeam("BG_BT_010", true, 2, 2); }
static void TestColdlightSeer() { MurTeam("BG_EX1_103", false, 0, 2); }
static void TestColdlightSeerGolden() { MurTeam("BG_EX1_103", true, 0, 4); }

static void TestFelfinBrann()
{
    Lobby L(MurSeed(), 2, MurBrannReg());
    OnBoard(L, 0, "BG_LOE_077");
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    PlayCard(L, 0, "BG_BT_010");
    CHECK(MurAtk(L, m) == 2 && MurHp(L, m) == 2);
}

// ------------------------------------------------------------------------------------------------ Primalfin Lookout

static void TestPrimalfinLookout()
{
    {
        Lobby L(MurSeed(), 2);
        SetTier(L, 0, 4);
        PlayCard(L, 0, "BGS_020");                      // no other Murloc: nothing
        CHECK(L.S(0).options.empty());
    }
    {
        Lobby L(MurSeed(), 2);
        SetTier(L, 0, 4);
        OnBoard(L, 0, "BG22_403");
        PlayCard(L, 0, "BGS_020");
        CHECK(L.S(0).options.size() == 3);
        for (Minion const& o : L.S(0).options)
            CHECK(o.IsTribe(kTribeMurloc) && o.Tier() <= 4);
        int const hand = L.S(0).hand.Size();
        CHECK(L.shop.Choose(0, 0) == ActionResult::Ok);
        CHECK(L.S(0).hand.Size() == hand + 1);
        CHECK(L.S(0).options.empty());                  // only one Discover
    }
    {
        Lobby L(MurSeed(), 2);                          // the Amalgam is another Murloc
        SetTier(L, 0, 4);
        OnBoard(L, 0, "BG_GIL_681");
        PlayCard(L, 0, "BGS_020");
        CHECK(L.S(0).options.size() == 3);
    }
}

static void TestPrimalfinLookoutGolden()
{
    Lobby L(MurSeed(), 2);
    SetTier(L, 0, 4);
    OnBoard(L, 0, "BG22_403");
    PlayCard(L, 0, "BGS_020", true);
    CHECK(L.S(0).options.size() == 3);
    CHECK(L.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(L.S(0).options.size() == 3);                  // the second Discover opens
    for (Minion const& o : L.S(0).options)
        CHECK(o.IsTribe(kTribeMurloc));
    CHECK(L.shop.Choose(0, 1) == ActionResult::Ok);
}

// ------------------------------------------------------------------------------------------------ Toxfin

static void TestToxfin()
{
    Lobby L(MurSeed(), 2);
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    uint32_t const other = OnBoard(L, 0, "BGS_020");
    PlayCard(L, 0, "BG_DAL_077", false, -1, m);
    CHECK(Find(L, 0, m)->Has(kKwPoisonous));
    CHECK(!Find(L, 0, other)->Has(kKwPoisonous));
    // an invalid target (a Beast) turns into a random Murloc; it never poisons itself
    Lobby B(MurSeed(), 2);
    uint32_t const beast = OnBoard(B, 0, "BG_EX1_534t");
    uint32_t const target = OnBoard(B, 0, "BG22_403");
    uint32_t const tox = PlayCard(B, 0, "BG_DAL_077", false, -1, beast);
    CHECK(!Find(B, 0, beast)->Has(kKwPoisonous));
    CHECK(Find(B, 0, target)->Has(kKwPoisonous));
    CHECK(!Find(B, 0, tox)->Has(kKwPoisonous));
    // no Murloc: nothing happens
    Lobby E(MurSeed(), 2);
    uint32_t const b2 = OnBoard(E, 0, "BG_EX1_534t");
    PlayCard(E, 0, "BG_DAL_077");
    CHECK(!Find(E, 0, b2)->Has(kKwPoisonous));
}

static void TestToxfinGolden()
{
    Lobby L(MurSeed(), 2);
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    PlayCard(L, 0, "BG_DAL_077", true, -1, m);
    CHECK(Find(L, 0, m)->Has(kKwPoisonous));
    CHECK(MurAtk(L, m) == 0 && MurHp(L, m) == 0);       // same effect as the normal card
}

// ------------------------------------------------------------------------------------------------ King Bagurgle

static void TestKingBagurgleBattlecry()
{
    Lobby L(MurSeed(), 2);
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    uint32_t const am = OnBoard(L, 0, "BG_GIL_681");
    uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const k = PlayCard(L, 0, "BGS_030");
    CHECK(MurAtk(L, m) == 3 && MurHp(L, m) == 3);       // +3/+3 (render text)
    CHECK(MurAtk(L, am) == 3 && MurHp(L, am) == 3);
    CHECK(MurAtk(L, beast) == 0 && MurHp(L, beast) == 0);
    CHECK(MurAtk(L, k) == 0 && MurHp(L, k) == 0);
}

static void TestKingBagurgleBattlecryGolden()
{
    Lobby L(MurSeed(), 2);
    uint32_t const m = OnBoard(L, 0, "BG22_403");
    PlayCard(L, 0, "BGS_030", true);
    CHECK(MurAtk(L, m) == 6 && MurHp(L, m) == 6);
}

static void MurBagurgleDeath(bool golden)
{
    int sawDeath = 0;
    for (uint32_t seed = 1; seed <= 30; ++seed)
    {
        Minion king = With(Mn("BGS_030", golden), 0, 1);
        Minion murloc = With(Mn("BG22_403"), 5, 50);
        Minion beast = With(Mn("BG_EX1_534t"), 5, 50);
        CombatResult const r = Fight({ king, murloc, beast }, { Body(1, 3), Body(1, 3), Body(1, 3), Body(1, 3) }, seed);
        Minion const* m = MurSurvivor(r, 0, "BG22_403");
        Minion const* b = MurSurvivor(r, 0, "BG_EX1_534t");
        CHECK(m != nullptr && b != nullptr);
        if (!m || !b)
            continue;
        CHECK(b->attack == 5 && b->maxHealth == 50);
        if (!MurSurvivor(r, 0, "BGS_030"))
            ++sawDeath;
        CHECK(m->attack == 5 && m->maxHealth == 50);     // the render has no Deathrattle: its death buffs nobody
    }
    CHECK(sawDeath > 0);
}
static void TestKingBagurgleDeathrattle() { MurBagurgleDeath(false); }
static void TestKingBagurgleDeathrattleGolden() { MurBagurgleDeath(true); }

// ------------------------------------------------------------------------------------------------ Young Murk-Eye

static void TestYoungMurkEye()
{
    // the left Murloc (Coldlight Seer: other Murlocs +2 Health) fires again at the end of the turn
    Lobby L(MurSeed(), 2);
    uint32_t const seer = OnBoard(L, 0, "BG_EX1_103");
    uint32_t const murk = OnBoard(L, 0, "BG22_403");
    uint32_t const right = OnBoard(L, 0, "BG22_202");
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(MurHp(L, murk) == 2);
    CHECK(MurHp(L, seer) == 0);
    CHECK(MurHp(L, right) == 2);                        // every other Murloc, as the Battlecry says
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(MurHp(L, murk) == 4);                         // every turn
}

static void TestYoungMurkEyeNotMurloc()
{
    Lobby L(MurSeed(), 2);
    // adjacent minions of any type, on both sides (render text): the Hyena has no Battlecry, the Seer to the right fires
    uint32_t const beast = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const murk = OnBoard(L, 0, "BG22_403");
    uint32_t const seer = OnBoard(L, 0, "BG_EX1_103");
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(MurHp(L, murk) == 2 && MurHp(L, beast) == 0 && MurHp(L, seer) == 0);
    // leftmost: no neighbour
    Lobby E(MurSeed(), 2);
    uint32_t const lone = OnBoard(E, 0, "BG22_403");
    CHECK(E.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(MurHp(E, lone) == 0);
}

static void TestYoungMurkEyeGolden()
{
    // golden: both neighbours twice. Left Seer (+2 Health to the others), right Felfin Navigator (+1/+1 to the others)
    Lobby L(MurSeed(), 2);
    uint32_t const seer = OnBoard(L, 0, "BG_EX1_103");
    uint32_t const murk = OnBoard(L, 0, "BG22_403", true);
    uint32_t const felfin = OnBoard(L, 0, "BG_BT_010");
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(MurHp(L, murk) == 4 + 2);
    CHECK(MurAtk(L, murk) == 2);
    CHECK(MurHp(L, seer) == 2 && MurAtk(L, seer) == 2);   // from Felfin, twice
    CHECK(MurHp(L, felfin) == 4 && MurAtk(L, felfin) == 0); // from the Seer, twice
}

static void TestYoungMurkEyeBrann()
{
    Lobby L(MurSeed(), 2, MurBrannReg());
    OnBoard(L, 0, "BG_LOE_077");
    uint32_t const sw = OnBoard(L, 0, "BG21_010");       // Swolefin: +2/+1 per other Murloc
    uint32_t const murk = OnBoard(L, 0, "BG22_403");
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(MurAtk(L, sw) == 4 && MurHp(L, sw) == 2);      // Brann doubles the triggered Battlecry; one other Murloc each time
    (void)murk;
}

static void TestYoungMurkEyeTargeted()
{
    // a targeted Battlecry on the left: Toxfin poisons a random other Murloc (itself excluded)
    Lobby L(MurSeed(), 2);
    uint32_t const tox = OnBoard(L, 0, "BG_DAL_077");
    OnBoard(L, 0, "BG22_403");
    OnBoard(L, 0, "BG_EX1_507");                          // Warleader as the only other Murloc besides Murk-Eye
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    int poisoned = 0;
    for (int i = 0; i < L.S(0).board.Size(); ++i)
        if (L.S(0).board[i].Has(kKwPoisonous))
            ++poisoned;
    CHECK(poisoned == 1);
    CHECK(!Find(L, 0, tox)->Has(kKwPoisonous));
}

static void RunMurlocTests()
{
    RUN(MurlocRegistryEntries);
    RUN(TestSwampstrikerShop);
    RUN(TestSwampstrikerShopGolden);
    RUN(TestSwampstrikerCombat);
    RUN(TestSwampstrikerCombatGolden);
    RUN(TestRockpoolHunter);
    RUN(TestRockpoolHunterNoMurloc);
    RUN(TestRockpoolHunterGolden);
    RUN(TestSaltscaleHoncho);
    RUN(TestSaltscaleHonchoGolden);
    RUN(TestSaltscaleHonchoEdges);
    RUN(TestTad);
    RUN(TestTadGolden);
    RUN(TestTadFullHand);
    RUN(TestMurlocWarleader);
    RUN(TestMurlocWarleaderGolden);
    RUN(TestMurlocWarleaderCombat);
    RUN(TestSwolefin);
    RUN(TestSwolefinGolden);
    RUN(TestSwolefinBrann);
    RUN(TestFelfinNavigator);
    RUN(TestFelfinNavigatorGolden);
    RUN(TestFelfinBrann);
    RUN(TestColdlightSeer);
    RUN(TestColdlightSeerGolden);
    RUN(TestPrimalfinLookout);
    RUN(TestPrimalfinLookoutGolden);
    RUN(TestToxfin);
    RUN(TestToxfinGolden);
    RUN(TestKingBagurgleBattlecry);
    RUN(TestKingBagurgleBattlecryGolden);
    RUN(TestKingBagurgleDeathrattle);
    RUN(TestKingBagurgleDeathrattleGolden);
    RUN(TestYoungMurkEye);
    RUN(TestYoungMurkEyeNotMurloc);
    RUN(TestYoungMurkEyeGolden);
    RUN(TestYoungMurkEyeBrann);
    RUN(TestYoungMurkEyeTargeted);
}
