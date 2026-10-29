// Tests of the Beast effect batch (src/battlegrounds/HsBgEffectsBeast.cpp).
// Included by bg_effects_tests.cpp, which provides CHECK, RUN and the helpers (see its header comment). One test per
// minion and per golden version; each is a static function called from RunBeastTests with RUN(fn).
// Only this file and HsBgEffectsBeast.cpp belong to the Beast batch.
//
// Tests that need a Neutral card (Brann, Khadgar, Baron Rivendare) run only when that card has a registry entry, so they
// are skipped (and trivially pass) until the Neutral batch has landed.

// Does the card have an entry in the real registry?
static bool BeastReg(char const* id)
{
    int const c = FindCard(id);
    return c >= 0 && EffectRegistry::Global().Find(c) != nullptr;
}

// Summon events of `card` that happen before the n-th Attack event (n >= 1).
static int BeastSummonsBeforeAttack(CombatResult const& r, int card, int n)
{
    int attacks = 0;
    int count = 0;
    for (CombatEvent const& e : r.events)
    {
        if (e.type == EventType::Attack && ++attacks >= n)
            break;
        if (e.type == EventType::Summon && e.card == card)
            ++count;
    }
    return count;
}

// A Beast with fixed stats and no effect.
static Minion BeastBody(int attack, int health) { return With(PlainOf(kTribeBeast), attack, health); }

static int BeastBuffEvents(CombatResult const& r, uint32_t target, int attack, int health)
{
    int n = 0;
    for (CombatEvent const& e : Events(r, EventType::Buff))
        if (e.target == target && e.amount == attack && e.amount2 == health)
            ++n;
    return n;
}

static void TestBeastRegistry()
{
    EffectRegistry const& reg = EffectRegistry::Global();
    for (char const* id : { "BG_CFM_315", "BG_EX1_531", "BG19_010", "BG21_000", "BGS_075", "BGS_078", "BG_CFM_316", "BG_LOOT_078",
                            "BG21_003", "BG_EX1_534", "BG21_001", "BG22_001", "BGS_021", "BGS_008", "BGS_018" })
        CHECK(reg.Find(Cd(id)) != nullptr);
    CHECK(reg.Find(Cd("BG20_205")) != nullptr && reg.Find(Cd("BG20_205"))->bloodGemAttack == 1);
    CHECK(reg.Find(Cd("BG_LOOT_078"))->cleave);
    CHECK(reg.Find(Cd("BG21_001"))->avengeCount == 2);
}

// ------------------------------------------------------------------------------------------------ Alleycat

static void TestBeastAlleycat()
{
    int const cat = Cd("BG_CFM_315t");
    Lobby L(1, 2);
    uint32_t const a = PlayCard(L, 0, "BG_CFM_315");
    CHECK(L.S(0).board.Size() == 2);
    CHECK(L.S(0).board[0].uid == a && L.S(0).board[1].card == cat && !L.S(0).board[1].golden);   // the cat lands to the right

    Lobby G(1, 2);
    PlayCard(G, 0, "BG_CFM_315", true);
    CHECK(G.S(0).board.Size() == 2);
    CHECK(G.S(0).board[1].card == cat && G.S(0).board[1].golden && G.S(0).board[1].attack == CardAt(cat).goldenAttack);

    // a full board loses the cat
    Lobby F(1, 2);
    for (char const* id : { "BG_EX1_534t", "BG_CFM_315t", "BG_CFM_316t", "BG19_010t", "BG22_001t2", "BG23_353_Gt" })
        OnBoard(F, 0, id);
    PlayCard(F, 0, "BG_CFM_315");
    CHECK(F.S(0).board.Size() == 7);
}

static void TestBeastAlleycatBrannKhadgar()
{
    int const cat = Cd("BG_CFM_315t");
    if (BeastReg("BG_LOE_077"))
    {
        Lobby L(1, 2);
        OnBoard(L, 0, "BG_LOE_077");
        PlayCard(L, 0, "BG_CFM_315");
        int cats = 0;
        for (Minion const& m : L.S(0).board.Items())
            cats += m.card == cat ? 1 : 0;
        CHECK(cats == 2);                                  // Brann: the Battlecry runs twice
    }
    if (BeastReg("BG_DAL_575"))
    {
        Lobby L(1, 2);
        OnBoard(L, 0, "BG_DAL_575");
        PlayCard(L, 0, "BG_CFM_315");
        CHECK(L.S(0).board.Size() == 4);                   // Khadgar, Alleycat, two cats
    }
}

// ------------------------------------------------------------------------------------------------ Scavenging Hyena

static void TestBeastHyena()
{
    // one Beast dies: +2/+1 once, to the Hyena (slot 0, id 1)
    CombatResult r = Fight({ With(Mn("BG_EX1_531"), 1, 100), BeastBody(1, 1) }, { Body(2, 30) });
    CHECK(BeastBuffEvents(r, 1, 2, 1) == 1);
    CHECK(Alive(r, 0) >= 1 && Survivor(r, 0, 0).card == Cd("BG_EX1_531") && Survivor(r, 0, 0).attack == 3);
    // a minion that is not a Beast does not feed it
    r = Fight({ With(Mn("BG_EX1_531"), 1, 100), Body(1, 1) }, { Body(2, 30) });
    CHECK(Events(r, EventType::Buff).empty());
}

static void TestBeastHyenaGolden()
{
    CombatResult r = Fight({ With(Mn("BG_EX1_531", true), 1, 100), BeastBody(1, 1) }, { Body(2, 30) });
    CHECK(BeastBuffEvents(r, 1, 4, 2) == 1);
    CHECK(Survivor(r, 0, 0).attack == 5);
}

// ------------------------------------------------------------------------------------------------ Sewer Rat

static void TestBeastSewerRat()
{
    CombatResult r = Fight({ With(Mn("BG19_010"), 1, 1) }, { Body(5, 100) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(s.size() == 1);
    CHECK(!s.empty() && s[0].card == Cd("BG19_010t") && !s[0].golden && s[0].amount == 2 && s[0].amount2 == 3);
    CHECK(!s.empty() && s[0].position == 0);
}

static void TestBeastSewerRatGolden()
{
    CombatResult r = Fight({ With(Mn("BG19_010", true), 1, 1) }, { Body(5, 100) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(s.size() == 1);
    CHECK(!s.empty() && s[0].card == Cd("BG19_010t") && s[0].golden && s[0].amount == 4 && s[0].amount2 == 6);
}

static void TestBeastSewerRatMultipliers()
{
    int const shell = Cd("BG19_010t");
    if (BeastReg("BG_FP1_031"))
    {
        CombatResult r = Fight({ With(Mn("BG_FP1_031"), 0, 500), With(Mn("BG19_010"), 1, 1) }, { Body(5, 100) });
        CHECK(SummonedCards(r, shell) == 2);               // Baron: the Deathrattle runs twice
    }
    if (BeastReg("BG_DAL_575"))
    {
        CombatResult r = Fight({ With(Mn("BG_DAL_575"), 0, 500), With(Mn("BG19_010"), 1, 1) }, { Body(5, 100) });
        CHECK(SummonedCards(r, shell) == 2);               // Khadgar: summons twice
    }
    if (BeastReg("BG_FP1_031") && BeastReg("BG_DAL_575"))
    {
        CombatResult r = Fight({ With(Mn("BG_DAL_575"), 0, 500), With(Mn("BG_FP1_031"), 0, 500), With(Mn("BG19_010"), 1, 1) },
                               { Body(5, 100) });
        CHECK(SummonedCards(r, shell) == 4);
    }
}

// ------------------------------------------------------------------------------------------------ Leapfrogger

static void TestBeastLeapfrogger()
{
    // the Leapfrogger dies on its first attack (the enemy hits back); the only other Beast gets +1/+1 and the Deathrattle
    CombatResult r = Fight({ With(Mn("BG21_000"), 1, 1), BeastBody(5, 1000) }, { Body(1, 30) });
    CHECK(Alive(r, 0) == 1);
    if (Alive(r, 0) == 1)
    {
        Minion const& b = Survivor(r, 0, 0);
        CHECK(b.attack == 6 && b.maxHealth == 1001);
        CHECK(b.granted.size() == 1 && b.granted[0].card == Cd("BG21_000") && !b.granted[0].golden);
    }
    // no other Beast: nothing happens
    r = Fight({ With(Mn("BG21_000"), 1, 1), Body(5, 1000) }, { Body(1, 30) });
    CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).attack == 5 && Survivor(r, 0, 0).granted.empty());
}

static void TestBeastLeapfroggerGolden()
{
    CombatResult r = Fight({ With(Mn("BG21_000", true), 1, 1), BeastBody(5, 1000) }, { Body(1, 30) });
    CHECK(Alive(r, 0) == 1);
    if (Alive(r, 0) == 1)
    {
        Minion const& b = Survivor(r, 0, 0);
        CHECK(b.attack == 7 && b.maxHealth == 1002);
        CHECK(b.granted.size() == 1 && b.granted[0].golden);
    }
}

static void TestBeastLeapfroggerChain()
{
    // the granted Deathrattle runs when its new host dies: the last Beast ends up with two instances stacked
    // (Leapfrogger -> B, B dies -> C gets +1/+1 and one more instance)
    CombatResult r = Fight({ With(Mn("BG21_000"), 1, 1), BeastBody(1, 1), BeastBody(5, 1000) }, { Body(3, 40) });
    CHECK(Alive(r, 0) == 1);
    if (Alive(r, 0) == 1)
    {
        Minion const& c = Survivor(r, 0, 0);
        CHECK(c.attack >= 6 && c.attack <= 7);             // +1 from the first Deathrattle, +1 more when the host died
        CHECK(!c.granted.empty());
    }
}

// ------------------------------------------------------------------------------------------------ Rabid Saurolisk

static void TestBeastSaurolisk()
{
    Lobby L(1, 2);
    uint32_t const s = OnBoard(L, 0, "BGS_075");
    int const a = CardAt(Cd("BGS_075")).attack;
    int const h = CardAt(Cd("BGS_075")).health;
    PlayCard(L, 0, "BG_CFM_315");                          // Alleycat: no Deathrattle
    CHECK(Find(L, 0, s)->attack == a && Find(L, 0, s)->health == h);
    PlayCard(L, 0, "BG19_010");                            // Sewer Rat: Deathrattle
    CHECK(Find(L, 0, s)->attack == a + 1 && Find(L, 0, s)->health == h + 2);
    PlayCard(L, 0, "BG_EX1_534");                          // Highmane: Deathrattle
    CHECK(Find(L, 0, s)->attack == a + 2 && Find(L, 0, s)->health == h + 4);
}

static void TestBeastSauroliskGolden()
{
    Lobby L(1, 2);
    uint32_t const s = OnBoard(L, 0, "BGS_075", true);
    int const a = CardAt(Cd("BGS_075")).goldenAttack;
    int const h = CardAt(Cd("BGS_075")).goldenHealth;
    PlayCard(L, 0, "BG19_010");
    CHECK(Find(L, 0, s)->attack == a + 2 && Find(L, 0, s)->health == h + 4);
    // it does not trigger on its own play
    Lobby S(1, 2);
    uint32_t const own = PlayCard(S, 0, "BGS_075");
    CHECK(Find(S, 0, own)->attack == CardAt(Cd("BGS_075")).attack);
}

static void TestBeastSauroliskMagnetic()
{
    if (!BeastReg("BG_BOT_312"))
        return;
    Lobby L(1, 2);
    uint32_t const s = OnBoard(L, 0, "BGS_075");
    uint32_t const mech = OnBoard(L, 0, "BG_BOT_312t");
    int const a = CardAt(Cd("BGS_075")).attack;
    PlayCard(L, 0, "BG_BOT_312", false, -1, mech);         // Replicating Menace merges: it was played, and it has a Deathrattle
    CHECK(Find(L, 0, s)->attack == a + 1);
}

// ------------------------------------------------------------------------------------------------ Monstrous Macaw

static void TestBeastMacaw()
{
    int const shell = Cd("BG19_010t");
    // Macaw attacks first, then the Rat: the Macaw's trigger summons one Half-Shell before the second attack
    CombatResult r = Fight({ With(Mn("BGS_078"), 1, 500), With(Mn("BG19_010"), 1, 500) }, { Body(0, 40) });
    CHECK(BeastSummonsBeforeAttack(r, shell, 2) == 1);
    bool beforeDeath = false;
    for (CombatEvent const& e : r.events)
    {
        if (e.type == EventType::Death)
            break;
        if (e.type == EventType::Summon && e.card == shell)
            beforeDeath = true;
    }
    CHECK(beforeDeath);
    // no other minion with a Deathrattle: nothing
    r = Fight({ With(Mn("BGS_078"), 1, 500), Body(1, 500) }, { Body(0, 40) });
    CHECK(SummonedCards(r, shell) == 0);
}

static void TestBeastMacawGolden()
{
    int const shell = Cd("BG19_010t");
    CombatResult r = Fight({ With(Mn("BGS_078", true), 1, 500), With(Mn("BG19_010"), 1, 500) }, { Body(0, 40) });
    CHECK(BeastSummonsBeforeAttack(r, shell, 2) == 2);     // twice, the same Rat
}

static void TestBeastMacawBaron()
{
    if (!BeastReg("BG_FP1_031"))
        return;
    CombatResult r = Fight({ With(Mn("BG_FP1_031"), 0, 500), With(Mn("BGS_078"), 1, 500), With(Mn("BG19_010"), 1, 500) },
                           { Body(0, 40) });
    CHECK(BeastSummonsBeforeAttack(r, Cd("BG19_010t"), 2) == 2);   // Baron multiplies the triggered Deathrattle
}

// ------------------------------------------------------------------------------------------------ Rat Pack

static void TestBeastRatPack()
{
    int const rat = Cd("BG_CFM_316t");
    CombatResult r = Fight({ With(Mn("BG_CFM_316"), 3, 1) }, { Body(5, 100) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(SummonedCards(r, rat) == 3);
    CHECK(!s.empty() && !s[0].golden && s[0].amount == 1 && s[0].amount2 == 1);
    // the board cap limits it
    r = Fight({ With(Mn("BG_CFM_316"), 9, 1) }, { Body(5, 100) });
    CHECK(BeastSummonsBeforeAttack(r, rat, 100) == 7);
    // no Attack, no Rats
    r = Fight({ With(Mn("BG_CFM_316"), 0, 1) }, { Body(5, 100) });
    CHECK(SummonedCards(r, rat) == 0);
}

static void TestBeastRatPackGolden()
{
    CombatResult r = Fight({ With(Mn("BG_CFM_316", true), 2, 1) }, { Body(5, 100) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(SummonedCards(r, Cd("BG_CFM_316t")) == 2);       // the count is its Attack
    CHECK(!s.empty() && s[0].golden && s[0].amount == 2 && s[0].amount2 == 2);
}

static void TestBeastRatPackKhadgar()
{
    if (!BeastReg("BG_DAL_575"))
        return;
    CombatResult r = Fight({ With(Mn("BG_DAL_575"), 0, 500), With(Mn("BG_CFM_316"), 2, 1) }, { Body(5, 100) });
    CHECK(SummonedCards(r, Cd("BG_CFM_316t")) == 4);       // every Rat is summoned twice
}

// ------------------------------------------------------------------------------------------------ Cave Hydra

static void TestBeastCaveHydra()
{
    for (bool golden : { false, true })
        for (uint32_t seed = 1; seed <= 10; ++seed)
        {
            CombatResult r = Fight({ With(Mn("BG_LOOT_078", golden), 5, 100) }, { Body(0, 5), Body(0, 5), Body(0, 5) }, seed);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            CHECK(!atk.empty());
            if (atk.empty())
                continue;
            int const slot = static_cast<int>(atk[0].target) - 8;
            int const expected = 1 + (slot > 0 ? 1 : 0) + (slot < 2 ? 1 : 0);
            int deaths = 0;
            int attacks = 0;
            for (CombatEvent const& e : r.events)
            {
                if (e.type == EventType::Attack && ++attacks >= 2)
                    break;
                if (e.type == EventType::Death && e.side == 1)
                    ++deaths;
            }
            CHECK(deaths == expected);                     // the target and its neighbours die in the first hit
            CHECK(r.winner == 0);
        }
}

static void TestBeastCaveHydraShield()
{
    // a neighbour's Divine Shield absorbs the cleave damage; Poisonous cleaves kill (they hit with the attacker's poison)
    for (uint32_t seed = 1; seed <= 10; ++seed)
    {
        CombatResult r = Fight({ With(Mn("BG_LOOT_078"), 5, 100) }, { Body(0, 5, kKwDivineShield), Body(0, 5, kKwDivineShield) }, seed);
        CHECK(!Events(r, EventType::ShieldPop).empty());
        CHECK(r.winner == 0);
    }
}

// ------------------------------------------------------------------------------------------------ Reanimating Rattler

// Render text (2026-10-19): "At the end of your turn, give another friendly Beast Reborn" (golden: 2 others).
static void TestBeastRattler()
{
    Lobby L(1, 2);
    uint32_t const b = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const mon = OnBoard(L, 0, "BG23_353_Gt");
    uint32_t const r = PlayCard(L, 0, "BG21_003");
    CHECK(!Find(L, 0, b)->Has(kKwReborn));                 // no Battlecry any more
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(Find(L, 0, b)->Has(kKwReborn));
    CHECK(!Find(L, 0, r)->Has(kKwReborn) && !Find(L, 0, mon)->Has(kKwReborn));   // never itself, never a non-Beast
    // no other Beast: nothing
    Lobby N(1, 2);
    uint32_t const mon2 = OnBoard(N, 0, "BG23_353_Gt");
    uint32_t const self = OnBoard(N, 0, "BG21_003");
    CHECK(N.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(!Find(N, 0, mon2)->Has(kKwReborn) && !Find(N, 0, self)->Has(kKwReborn));
}

static void TestBeastRattlerGolden()
{
    Lobby L(1, 2);
    uint32_t const b = OnBoard(L, 0, "BG_EX1_534t");
    uint32_t const b2 = OnBoard(L, 0, "BG_EX1_534t");
    OnBoard(L, 0, "BG21_003", true);
    CHECK(L.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(Find(L, 0, b)->Has(kKwReborn) && Find(L, 0, b2)->Has(kKwReborn));   // two other Beasts
    // the Reborn minion comes back in combat
    CombatResult r = Fight({ With(Mn("BG_EX1_534t"), 1, 1) }, { Body(5, 100) });
    CHECK(Events(r, EventType::Reborn).empty());
    Minion m = With(Mn("BG_EX1_534t"), 1, 1);
    m.keywords |= kKwReborn;
    r = Fight({ m }, { Body(5, 100) });
    CHECK(!Events(r, EventType::Reborn).empty());
}

// ------------------------------------------------------------------------------------------------ Savannah Highmane

static void TestBeastHighmane()
{
    CombatResult r = Fight({ With(Mn("BG_EX1_534"), 1, 1) }, { Body(5, 100) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(SummonedCards(r, Cd("BG_EX1_534t")) == 2);
    CHECK(s.size() == 2 && !s[0].golden && s[0].amount == 2 && s[0].amount2 == 2);
    if (s.size() == 2)
        CHECK(s[0].position == 0 && s[1].position == 1);
}

static void TestBeastHighmaneGolden()
{
    CombatResult r = Fight({ With(Mn("BG_EX1_534", true), 1, 1) }, { Body(5, 100) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(s.size() == 2);
    CHECK(s.size() == 2 && s[0].golden && s[1].golden && s[0].amount == 4 && s[0].amount2 == 4);
}

static void TestBeastHighmaneFullBoard()
{
    CombatResult r = Fight({ With(Mn("BG_EX1_534"), 1, 1), Body(0, 100), Body(0, 100), Body(0, 100), Body(0, 100), Body(0, 100),
                             Body(0, 100) },
                           { Body(5, 100) });
    CHECK(SummonedCards(r, Cd("BG_EX1_534t")) == 1);       // one slot freed, the second Hyena is lost
}

// ------------------------------------------------------------------------------------------------ Agamaggan

static void TestBeastAgamaggan()
{
    Minion const m = Mn("BG20_205");
    CHECK(m.attack == 6 && m.health == 6);                 // vanilla
    CombatResult r = Fight({ m }, { Body(1, 3) });
    CHECK(r.winner == 0);
}

// ------------------------------------------------------------------------------------------------ Palescale Crocolisk

static void TestBeastCrocolisk()
{
    // Deathrattle: the other Beast gets +6/+6
    CombatResult r = Fight({ With(Mn("BG21_001"), 1, 1), BeastBody(5, 1000) }, { Body(1, 30) });
    CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).attack == 11 && Survivor(r, 0, 0).maxHealth == 1006);
    // Avenge (2): two friendly deaths -> +6/+6 for another Beast (once; one death alone does nothing)
    r = Fight({ With(Mn("BG21_001"), 1, 500), BeastBody(1, 1), BeastBody(1, 1), BeastBody(1, 500) }, { Body(3, 40) });
    CHECK(Alive(r, 0) == 2 && Survivor(r, 0, 1).attack == 7);
    r = Fight({ With(Mn("BG21_001"), 1, 500), BeastBody(1, 1), BeastBody(1, 500) }, { Body(3, 40) });
    CHECK(Alive(r, 0) == 3 || Alive(r, 0) == 2);
    CHECK(Survivor(r, 0, Alive(r, 0) - 1).attack == 1);
}

static void TestBeastCrocoliskGolden()
{
    CombatResult r = Fight({ With(Mn("BG21_001", true), 1, 1), BeastBody(5, 1000) }, { Body(1, 30) });
    CHECK(Alive(r, 0) == 1 && Survivor(r, 0, 0).attack == 17 && Survivor(r, 0, 0).maxHealth == 1012);
    r = Fight({ With(Mn("BG21_001", true), 1, 500), BeastBody(1, 1), BeastBody(1, 1), BeastBody(1, 500) }, { Body(3, 40) });
    CHECK(Alive(r, 0) == 2 && Survivor(r, 0, 1).attack == 13);
}

// ------------------------------------------------------------------------------------------------ Baby Krush

static void TestBeastBabyKrush()
{
    int const dino = Cd("BG22_001t2");
    CombatResult r = Fight({ Mn("BG22_001") }, { Body(0, 10) });
    std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
    CHECK(atk.size() >= 2 && atk[0].actor == 1 && atk[1].actor == kFirstSummonId && atk[1].target == atk[0].target);
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(!s.empty() && s[0].card == dino && !s[0].golden && s[0].amount == 8 && s[0].amount2 == 8 && s[0].position == 1);
    CHECK(r.winner == 0);
    // the Devilsaur kills the target: Krush does not get his own hit in (one attack event only for this exchange)
    r = Fight({ Mn("BG22_001") }, { Body(0, 5) });
    CHECK(r.winner == 0 && SummonedCards(r, dino) == 1);
    // full board: no Devilsaur
    r = Fight({ Mn("BG22_001"), Body(0, 5), Body(0, 5), Body(0, 5), Body(0, 5), Body(0, 5), Body(0, 5) }, { Body(0, 30) });
    CHECK(SummonedCards(r, dino) == 0);
}

static void TestBeastBabyKrushGolden()
{
    CombatResult r = Fight({ Mn("BG22_001", true) }, { Body(0, 40) });
    std::vector<CombatEvent> const s = Events(r, EventType::Summon);
    CHECK(!s.empty() && s[0].card == Cd("BG22_001t2") && s[0].golden && s[0].amount == 16 && s[0].amount2 == 16);
}

static void TestBeastBabyKrushKhadgar()
{
    if (!BeastReg("BG_DAL_575"))
        return;
    CombatResult r = Fight({ With(Mn("BG_DAL_575"), 0, 500), Mn("BG22_001") }, { Body(0, 200) });
    std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
    CHECK(atk.size() >= 3 && atk[0].actor == 2 && atk[1].actor >= kFirstSummonId && atk[2].actor >= kFirstSummonId);
}

// ------------------------------------------------------------------------------------------------ Mama Bear

static void TestBeastMamaBearShop()
{
    int const cat = Cd("BG_CFM_315t");
    Lobby L(1, 2);
    uint32_t const mama = OnBoard(L, 0, "BGS_021");
    uint32_t const a = PlayCard(L, 0, "BG_CFM_315");       // a Beast is played, and its Battlecry summons a Beast
    CHECK(Find(L, 0, a)->attack == CardAt(Cd("BG_CFM_315")).attack + 3 && Find(L, 0, a)->health == CardAt(Cd("BG_CFM_315")).health + 3);
    CHECK(L.S(0).board.Size() == 3);
    Minion const& c = L.S(0).board[2];
    CHECK(c.card == cat && c.attack == CardAt(cat).attack + 3 && c.health == CardAt(cat).health + 3);
    CHECK(Find(L, 0, mama)->attack == CardAt(Cd("BGS_021")).attack);   // not for herself
    // a minion that is not a Beast is not buffed
    PlayCard(L, 0, "BG23_353_Gt");
    CHECK(L.S(0).board[3].attack == CardAt(Cd("BG23_353_Gt")).attack);
}

static void TestBeastMamaBearShopGolden()
{
    Lobby L(1, 2);
    OnBoard(L, 0, "BGS_021", true);
    uint32_t const a = PlayCard(L, 0, "BG_CFM_315");
    CHECK(Find(L, 0, a)->attack == CardAt(Cd("BG_CFM_315")).attack + 6);
    CHECK(L.S(0).board[2].attack == CardAt(Cd("BG_CFM_315t")).attack + 6);
}

static void TestBeastMamaBearCombat()
{
    // Highmane's two Hyenas each get +3/+3; the Mama Bear herself does not
    CombatResult r = Fight({ With(Mn("BGS_021"), 1, 500), With(Mn("BG_EX1_534"), 1, 1) }, { Body(5, 300) });
    CHECK(BeastBuffEvents(r, kFirstSummonId, 3, 3) == 1 && BeastBuffEvents(r, kFirstSummonId + 1, 3, 3) == 1);
    CHECK(BeastBuffEvents(r, 1, 3, 3) == 0);
}

static void TestBeastMamaBearCombatGolden()
{
    CombatResult r = Fight({ With(Mn("BGS_021", true), 1, 500), With(Mn("BG_EX1_534"), 1, 1) }, { Body(5, 300) });
    CHECK(BeastBuffEvents(r, kFirstSummonId, 6, 6) == 1 && BeastBuffEvents(r, kFirstSummonId + 1, 6, 6) == 1);
}

// ------------------------------------------------------------------------------------------------ Ghastcoiler

static void TestBeastGhastcoiler()
{
    EffectRegistry const& reg = EffectRegistry::Global();
    int const self = Cd("BGS_008");
    for (uint32_t seed = 1; seed <= 6; ++seed)
    {
        CombatResult r = Fight({ With(Mn("BGS_008"), 1, 1) }, { Body(5, 100) }, seed);
        int n = 0;
        for (CombatEvent const& e : Events(r, EventType::Summon))
            if (e.actor == 1)
            {
                ++n;
                EffectDef const* d = reg.Find(e.card);
                CHECK(d && d->deathrattle && e.card != self && !e.golden);
            }
        CHECK(n == 2);
    }
}

static void TestBeastGhastcoilerGolden()
{
    for (uint32_t seed = 1; seed <= 4; ++seed)
    {
        CombatResult r = Fight({ With(Mn("BGS_008", true), 1, 1) }, { Body(5, 100) }, seed);
        int n = 0;
        for (CombatEvent const& e : Events(r, EventType::Summon))
            if (e.actor == 1)
                ++n;
        CHECK(n == 4);
    }
}

static void TestBeastGhastcoilerKhadgar()
{
    if (!BeastReg("BG_DAL_575"))
        return;
    CombatResult r = Fight({ With(Mn("BG_DAL_575"), 0, 500), With(Mn("BGS_008"), 1, 1) }, { Body(5, 100) });
    int n = 0;
    for (CombatEvent const& e : Events(r, EventType::Summon))
        if (e.actor == 2)
            ++n;
    CHECK(n >= 4 - 0 && n <= 4);                           // two picks, each summoned twice (the board has room)
}

// ------------------------------------------------------------------------------------------------ Goldrinn

static void TestBeastGoldrinn()
{
    // Goldrinn dies on its first attack; the living Beast gets +3/+3 for the rest of the combat, the non-Beast does not
    CombatResult r = Fight({ With(Mn("BGS_018"), 1, 1), BeastBody(1, 200), Body(1, 200) }, { Body(3, 30) });
    CHECK(Alive(r, 0) == 2);
    if (Alive(r, 0) == 2)
    {
        CHECK(Survivor(r, 0, 0).attack == 4 && Survivor(r, 0, 0).maxHealth == 203);
        CHECK(Survivor(r, 0, 1).attack == 1 && Survivor(r, 0, 1).maxHealth == 200);
    }
}

static void TestBeastGoldrinnGolden()
{
    CombatResult r = Fight({ With(Mn("BGS_018", true), 1, 1), BeastBody(1, 200), BeastBody(1, 200) }, { Body(3, 30) });
    CHECK(Alive(r, 0) == 2);
    if (Alive(r, 0) == 2)
        CHECK(Survivor(r, 0, 0).attack == 7 && Survivor(r, 0, 1).attack == 7 && Survivor(r, 0, 0).maxHealth == 206);
}

static void RunBeastTests()
{
    RUN(TestBeastRegistry);
    RUN(TestBeastAlleycat);
    RUN(TestBeastAlleycatBrannKhadgar);
    RUN(TestBeastHyena);
    RUN(TestBeastHyenaGolden);
    RUN(TestBeastSewerRat);
    RUN(TestBeastSewerRatGolden);
    RUN(TestBeastSewerRatMultipliers);
    RUN(TestBeastLeapfrogger);
    RUN(TestBeastLeapfroggerGolden);
    RUN(TestBeastLeapfroggerChain);
    RUN(TestBeastSaurolisk);
    RUN(TestBeastSauroliskGolden);
    RUN(TestBeastSauroliskMagnetic);
    RUN(TestBeastMacaw);
    RUN(TestBeastMacawGolden);
    RUN(TestBeastMacawBaron);
    RUN(TestBeastRatPack);
    RUN(TestBeastRatPackGolden);
    RUN(TestBeastRatPackKhadgar);
    RUN(TestBeastCaveHydra);
    RUN(TestBeastCaveHydraShield);
    RUN(TestBeastRattler);
    RUN(TestBeastRattlerGolden);
    RUN(TestBeastHighmane);
    RUN(TestBeastHighmaneGolden);
    RUN(TestBeastHighmaneFullBoard);
    RUN(TestBeastAgamaggan);
    RUN(TestBeastCrocolisk);
    RUN(TestBeastCrocoliskGolden);
    RUN(TestBeastBabyKrush);
    RUN(TestBeastBabyKrushGolden);
    RUN(TestBeastBabyKrushKhadgar);
    RUN(TestBeastMamaBearShop);
    RUN(TestBeastMamaBearShopGolden);
    RUN(TestBeastMamaBearCombat);
    RUN(TestBeastMamaBearCombatGolden);
    RUN(TestBeastGhastcoiler);
    RUN(TestBeastGhastcoilerGolden);
    RUN(TestBeastGhastcoilerKhadgar);
    RUN(TestBeastGoldrinn);
    RUN(TestBeastGoldrinnGolden);
}
