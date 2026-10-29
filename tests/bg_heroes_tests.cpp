// And the hero table (HsBgHeroData.inc), the armor tiers and the 85 curated power entries.
//   bg_heroes_tests.exe [directory]     run all tests; exit code 0 = all checks passed.
//
// One test per hero, plus the registry coverage (every curated hero must have a power entry), the hero pick and
// the armor. The Shop is built with an EMPTY effect registry so a minion has no side effect: only the hero power is
// under test. SetHero picks the hero, StartTurn gives the turn's gold.

#include "../src/battlegrounds/HsBgCombat.h"
#include "../src/battlegrounds/HsBgEffects.h"
#include "../src/battlegrounds/HsBgGame.h"
#include "../src/battlegrounds/HsBgHeroes.h"
#include "../src/battlegrounds/HsBgPool.h"
#include "../src/battlegrounds/HsBgShop.h"
#include "../src/battlegrounds/HsBgTypes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace Hs::Bg;

static int g_checks = 0;
static int g_failed = 0;
static char const* g_test = "";

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);                  \
        }                                                                                             \
    } while (0)

#define RUN(fn)                                                                                       \
    do {                                                                                              \
        g_test = #fn;                                                                                 \
        fn();                                                                                         \
    } while (0)

static EffectRegistry const& NoEffects()
{
    static EffectRegistry const reg;   // empty: no minion never has an effect here
    return reg;
}

struct Env
{
    Rng rng;
    PoolSet pool;
    Shop shop;
    explicit Env(uint32_t seed = 1, int players = 2, EffectRegistry const& reg = NoEffects())
        : rng(seed), shop(pool, reg, rng, players)
    {
        pool.BuildLobby(rng);
    }
    PlayerShop& S(int p) { return shop.State(p); }
};

static int Hero(std::string const& id)
{
    int const h = FindHero(id);
    CHECK(h >= 0);
    return h;
}

static uint32_t PutBoard(Env& e, int p, int card, bool golden = false, int copies = 1)
{
    for (int i = 0; i < copies; ++i)
        e.pool.Take(card);
    Minion m = MakeMinion(card, golden, e.shop.NewUid());
    m.poolCopies = copies;
    e.S(p).board.Push(m);
    return m.uid;
}

static uint32_t PutHand(Env& e, int p, int card, bool golden = false, int copies = 1)
{
    for (int i = 0; i < copies; ++i)
        e.pool.Take(card);
    Minion m = MakeMinion(card, golden, e.shop.NewUid());
    m.poolCopies = copies;
    e.S(p).hand.Push(m);
    return m.uid;
}

// ------------------------------------------------------------------------------------ the table and registry

static void TestHeroTable()
{
    CHECK(HeroCount() == 86);
    int const patchwerk = Hero("TB_BaconShop_HERO_34");
    CHECK(std::string(HeroAt(patchwerk).id) == "TB_BaconShop_HERO_34");
    CHECK(HeroAt(patchwerk).health == 60);                 // Patchwerk is the only 60-health hero
    CHECK(HeroAt(patchwerk).armorTier == 2);
    CHECK(std::string(HeroAt(patchwerk).powerId) == "TB_BaconShop_HP_035");
    int curated = 0;
    for (int i = 0; i < HeroCount(); ++i)
    {
        CHECK(HeroAt(i).powerId[0] != '\0');
        CHECK(HeroAt(i).health == 40 || HeroAt(i).health == 60);
        CHECK(HeroAt(i).armorTier >= 1 && HeroAt(i).armorTier <= 7);
        if (HeroAt(i).curated)
            ++curated;
    }
    CHECK(curated == 86);
    CHECK(FindHero("no such hero") == -1);
}

static void TestRegistryComplete()
{
    HeroPowerRegistry const& reg = HeroPowerRegistry::Global();
    CHECK(reg.Errors().empty());
    CHECK(reg.Size() == 99);   // the 86 heroes, Queen Azshara's Naga Conquest and the 12 choice powers
    for (int i = 0; i < HeroCount(); ++i)
        if (HeroAt(i).curated)
            CHECK(reg.Find(HeroAt(i).powerId) != nullptr);   // every first hero has an implementation
}

static void TestArmorTiers()
{
    int lo = -1;
    int hi = -1;
    ArmorTierRange(1, lo, hi);
    CHECK(lo == 0 && hi == 0);
    ArmorTierRange(2, lo, hi);
    CHECK(lo == 2 && hi == 5);
    ArmorTierRange(4, lo, hi);
    CHECK(lo == 4 && hi == 7);
    ArmorTierRange(7, lo, hi);
    CHECK(lo == 7 && hi == 10);
    ArmorTierRange(0, lo, hi);
    CHECK(lo == 0 && hi == 0);
}

// ------------------------------------------------------------------------------------ the hero pick and armor

static void TestHeroPick()
{
    GameSetup s;
    s.seed = 7;
    s.heroPick = true;
    s.factionPools = true;
    s.autoRun = false;
    s.humanSeat = 0;
    s.kinds[0] = PlayerKind::External;   // player 0 is the human; 1-7 are NPCs
    Game g(s);
    CHECK(g.Picking() && g.Phase() == GamePhase::Pick);
    CHECK(g.HeroChoices(0).size() == 4);
    std::set<int> all;
    for (int h : g.HeroChoices(0))
        all.insert(h);
    for (int p = 1; p < kPlayers; ++p)
    {
        CHECK(g.HeroChoices(p).size() == 2);
        for (int h : g.HeroChoices(p))
            all.insert(h);
    }
    CHECK(all.size() == 18);                          // 4 + 7 x 2 distinct heroes (the whole curated list)
    // Only ChooseHero is allowed during the pick.
    CHECK(g.Buy(0, 0) == ActionResult::NotShopPhase);
    CHECK(g.ChooseHero(0, 4) == ActionResult::BadIndex);
    // The NPCs pick when pumped; the game then waits for the human.
    g.Pump();
    CHECK(g.Picking());
    for (int p = 1; p < kPlayers; ++p)
        CHECK(g.HeroPicked(p) && g.PlayerHero(p) >= 0);
    CHECK(!g.HeroPicked(0));
    int const chosen = g.HeroChoices(0)[0];
    CHECK(g.ChooseHero(0, 0) == ActionResult::Ok);
    CHECK(!g.Picking() && g.Round() == 1 && g.PlayerHero(0) == chosen);
    for (int p = 0; p < kPlayers; ++p)
    {
        PlayerShop const& ps = g.GetShop().State(p);
        CHECK(ps.hero >= 0 && ps.health == HeroAt(ps.hero).health && ps.maxHealth == HeroAt(ps.hero).health);
        int lo = 0;
        int hi = 0;
        ArmorTierRange(HeroAt(ps.hero).armorTier, lo, hi);
        CHECK(ps.armor >= lo && ps.armor <= hi);
    }
    (void)chosen;
    CHECK(g.GetShop().CheckConservation());

    // A whole NPC game with hero pick and faction pools runs to the end.
    GameSetup s2;
    s2.seed = 11;
    s2.heroPick = true;
    s2.factionPools = true;
    Game g2(s2);
    CHECK(g2.Over() && g2.GetShop().CheckConservation());
    for (int p = 0; p < kPlayers; ++p)
        CHECK(g2.GetShop().State(p).hero >= 0 || !g2.Alive(p));
}

// Review fix: a hero whose power draws a lobby tribe (Patches, Flurgl, Ini Stormcoil) is not dealt to the human seat
// when that tribe is not in its lobby.
static void TestHeroPickNeedsTribe()
{
    int dealt = 0;
    for (uint32_t seed = 1; seed <= 60; ++seed)
    {
        GameSetup s;
        s.seed = seed;
        s.heroPick = true;
        s.factionPools = true;
        s.autoRun = false;
        s.humanSeat = 0;
        s.kinds[0] = PlayerKind::External;
        Game g(s);
        for (int h : g.HeroChoices(0))
        {
            HeroPowerDef const* const power = HeroPowerRegistry::Global().Find(HeroAt(h).powerId);
            if (!power || power->needsLobbyTribe == 0)
                continue;
            ++dealt;
            bool inLobby = false;
            for (uint32_t t : g.FactionTribes(0))
                inLobby = inLobby || t == power->needsLobbyTribe;
            CHECK(inLobby);
        }
    }
    CHECK(dealt >= 0);
}

static void TestHumanPickStartsRound()
{
    GameSetup s;
    s.seed = 13;
    s.heroPick = true;
    s.autoRun = false;
    s.humanSeat = 0;
    s.kinds[0] = PlayerKind::External;
    Game g(s);
    CHECK(g.Picking());
    CHECK(g.ChooseHero(0, 1) == ActionResult::Ok);     // the human picks before any Pump
    g.Pump();
    CHECK(!g.Picking() && g.Round() == 1);
    CHECK(g.PlayerHero(0) == g.HeroChoices(0)[1]);
}

// ------------------------------------------------------------------------------------ one test per hero

static void TestPatchwerk()
{
    Env e(1);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_34"));
    CHECK(e.S(0).maxHealth == 60 && e.S(0).health == 60);
}

static void TestNozdormu()
{
    Env e(2);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_57"));
    e.shop.StartTurn(0, 1);
    CHECK(e.S(0).gold == 3);
    CHECK(e.shop.Refresh(0) == ActionResult::Ok && e.S(0).gold == 3);   // the first Refresh is free
    CHECK(e.shop.Refresh(0) == ActionResult::Ok && e.S(0).gold == 2);   // the second costs 1
}

static void TestOmu()
{
    Env e(3);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_74"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);                             // 13
    CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
    CHECK(e.S(0).tier == 2 && e.S(0).gold == 10);      // 13 - 5 + 2
}

static void TestGallywix()
{
    Env e(4);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_10"));
    e.shop.StartTurn(0, 1);
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);       // 3 -> 0
    CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);
    CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);   // +1 now, +1 next turn
    CHECK(e.S(0).power.pendingGold == 1);
    e.shop.StartTurn(0, 2);
    CHECK(e.S(0).gold == GoldForTurn(2) + 1);
}

static void TestMillhouse()
{
    Env e(5);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_49"));
    e.shop.StartTurn(0, 1);
    CHECK(e.shop.UpgradeCost(0) == 6);                 // tiers cost 1 more
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok && e.S(0).gold == 1);   // minions cost 2
    CHECK(e.shop.Refresh(0) == ActionResult::NotEnoughGold);           // Refresh costs 2
    e.shop.AddGold(0, 10);                             // 11
    CHECK(e.shop.Refresh(0) == ActionResult::Ok && e.S(0).gold == 9);
    // Refresh discounts are one entry per Refresh and do not blend: (1) less, then free, then full price.
    e.shop.AddRefreshDiscount(0, 1, 1);
    e.shop.AddRefreshDiscount(0, 99, 1);
    CHECK(e.shop.RefreshCost(0) == 1);
    CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    CHECK(e.shop.RefreshCost(0) == 0);
    CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    CHECK(e.shop.RefreshCost(0) == 2);
}

// Render text (2026-10-19): "Whenever you summon a Mech, give it +2 Attack" - in the shop and in combat.
static void TestMillificent()
{
    Env e(6);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_17"));
    e.shop.StartTurn(0, 1);
    int const mech = CardsOfTribe(kTribeMech, true)[0];
    CardDef const& d = CardAt(mech);
    e.S(0).offers[0] = MakeMinion(mech, false, e.shop.NewUid());
    e.shop.Settle(0);
    CHECK(e.S(0).offers[0].attack == d.attack && e.S(0).offers[0].auraAttack == 0);   // no tavern aura any more
    PutHand(e, 0, mech);
    CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);   // a played Mech is summoned
    CHECK(e.S(0).board[0].attack == d.attack + 2 && e.S(0).board[0].maxHealth == d.health);
    int plain = -1;
    for (int c : CardsOfTier(1, true))
        if (CardAt(c).tribes == 0)
            plain = c;
    CHECK(plain >= 0);
    PutHand(e, 0, plain);
    CHECK(e.shop.Play(0, 0, 1) == ActionResult::Ok);
    CHECK(e.S(0).board[1].attack == CardAt(plain).attack);   // not a Mech
    CHECK(e.shop.SummonToBoard(0, mech, false, 2) != 0);
    CHECK(e.S(0).board[2].attack == d.attack + 2);
    // combat: a Mech that comes back with Reborn is summoned too
    CombatSide a;
    Minion m = MakeMinion(mech, false, 1);
    m.health = m.maxHealth = 1;
    m.keywords |= kKwReborn;
    a.board.Push(m);
    CombatSide b;
    b.board.Push(MakeMinion(plain, false, 2));
    b.board[0].attack = 5;
    b.board[0].health = b.board[0].maxHealth = 100;
    CombatSettings settings;
    settings.effects = &NoEffects();
    settings.summonAttackTribe[0] = kTribeMech;
    settings.summonAttack[0] = 2;
    CombatResult const r = SimulateCombat(a, b, 1, settings);
    bool reborn = false;
    for (CombatEvent const& ev : r.events)
        if (ev.type == EventType::Reborn)
        {
            reborn = true;
            CHECK(ev.amount == d.attack + 2);
        }
    CHECK(reborn);
}

// Render text (2026-10-19): "When you buy a minion, give it a +1/+1 hat that passes to a friendly minion when sold."
static void TestDeryl()
{
    Env e(7);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_36"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    Minion const offer = e.S(0).offers[0];
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == 1 && e.S(0).hand[0].hats == 1);
    CHECK(e.S(0).hand[0].attack == offer.attack + 1 && e.S(0).hand[0].maxHealth == offer.maxHealth + 1);
    CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);
    int const other = CardsOfTier(1, true)[0];
    uint32_t const keep = PutBoard(e, 0, other, false, 1);
    CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);   // the hatted minion: its hat passes on
    CHECK(e.S(0).board.Size() == 1 && e.S(0).board[0].uid == keep && e.S(0).board[0].hats == 1);
    CHECK(e.S(0).board[0].attack == CardAt(other).attack + 1 && e.S(0).board[0].maxHealth == CardAt(other).health + 1);
    CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);   // nobody left to take it: the hat is gone
    CHECK(e.S(0).board.Empty());
}

static void TestKaelthas()
{
    Env e(8);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_60"));
    e.shop.StartTurn(0, 1);
    std::vector<int> const t1 = CardsOfTier(1, true);
    PutHand(e, 0, t1[0]);
    PutHand(e, 0, t1[1]);
    PutHand(e, 0, t1[2]);
    CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);
    CHECK(e.shop.Play(0, 0, 1) == ActionResult::Ok);
    CHECK(e.shop.Play(0, 0, 2) == ActionResult::Ok);   // the third play gains +2/+2
    CHECK(e.S(0).board[0].attack == CardAt(t1[0]).attack);
    CHECK(e.S(0).board[2].attack == CardAt(t1[2]).attack + 2);
    CHECK(e.S(0).board[2].maxHealth == CardAt(t1[2]).health + 2);
}

static void TestKaelthasTriple()
{
    // The third play completes a triple: the buff lands before the merge, the play counter still counts it.
    Env e(31);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_60"));
    e.shop.StartTurn(0, 1);
    std::vector<int> const t1 = CardsOfTier(1, true);
    PutBoard(e, 0, t1[0], false, 1);
    PutBoard(e, 0, t1[0], false, 1);
    PutHand(e, 0, t1[1]);
    PutHand(e, 0, t1[2]);
    PutHand(e, 0, t1[0]);
    CHECK(e.shop.Play(0, 0, 2) == ActionResult::Ok);
    CHECK(e.shop.Play(0, 0, 3) == ActionResult::Ok);
    CHECK(e.shop.Play(0, 0, 4) == ActionResult::Ok);
    CHECK(e.S(0).power.playsThisTurn == 3);
    bool golden = false;
    for (Minion const& m : e.S(0).hand.Items())
        golden = golden || m.golden;
    for (Minion const& m : e.S(0).board.Items())
        golden = golden || m.golden;
    CHECK(golden);
}

// Render text (2026-10-19): "After 14 friendly minions attack, the first minion you buy each turn is free."
static void TestAranna()
{
    Env e(9);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_59"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 20);
    for (int i = 0; i < 5; ++i)
        CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    CHECK(static_cast<int>(e.S(0).offers.size()) == OffersForTier(1));   // no seven offers any more
    e.S(0).power.friendlyAttacks = 13;
    int g = e.S(0).gold;
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok && e.S(0).gold == g - kBuyCost);   // 13 attacks: not yet
    e.shop.StartTurn(0, 2);
    e.shop.AddGold(0, 20);
    e.S(0).power.friendlyAttacks = 14;
    g = e.S(0).gold;
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok && e.S(0).gold == g);              // the first buy of the turn is free
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok && e.S(0).gold == g - kBuyCost);   // the second is not
    e.shop.StartTurn(0, 3);
    g = e.S(0).gold;
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok && e.S(0).gold == g);              // every turn again
}

static void TestBazhial()
{
    Env e(10);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_25"));
    e.shop.StartTurn(0, 1);
    // Render text (2026-10-19): "Steal a minion from the Tavern. Take damage equal to its Tier." Costs 1.
    e.S(0).armor = 0;
    int const hp0 = e.S(0).health;
    int const gold0 = e.S(0).gold;
    CHECK(e.shop.HeroPower(0) == ActionResult::BadTarget);          // it needs a tavern minion
    CHECK(e.shop.HeroPower(0, 0, 9) == ActionResult::BadTarget);
    CHECK(e.S(0).gold == gold0 && e.S(0).power.usesThisTurn == 0);
    Minion const target = e.S(0).offers[1];
    std::size_t const offers = e.S(0).offers.size();
    CHECK(e.shop.HeroPower(0, 0, 1) == ActionResult::Ok);
    CHECK(e.S(0).gold == gold0 - 1);
    CHECK(e.S(0).offers.size() == offers - 1);
    CHECK(e.S(0).hand.Size() == 1 && e.S(0).hand[0].card == target.card);
    CHECK(e.S(0).health == hp0 - target.Tier());
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::PowerUsed);    // once per turn
}

static void TestKragg()
{
    Env e(11);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_68"));
    e.shop.StartTurn(0, 2);
    int const gold0 = e.S(0).gold;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).gold == gold0 + e.shop.Turn());       // 1 Gold, +1 per turn
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);   // once per game
    e.shop.StartTurn(0, 3);
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);   // still used after a new turn
}

static void TestToki()
{
    Env e(12);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_28"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    bool higher = false;
    for (Minion const& o : e.S(0).offers)
        if (o.Tier() == 2)
            higher = true;
    CHECK(higher);
    int n = 0;
    for (Minion const& o : e.S(0).offers)
        n += o.Tier() == 2 ? 1 : 0;
    CHECK(n == 2);                                     // two minions from a higher tier (render text)
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);
}

static void TestElise()
{
    Env e(13);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_42"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 20);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);    // cost 1
    CHECK(e.shop.DiscoverOpen(0));                     // Discover from the current tier
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    // The power is once per turn; the NEXT turn's use costs 2 (the cost grows each use).
    e.shop.StartTurn(0, 2);
    e.shop.AddGold(0, 20);
    int const g1 = e.S(0).gold;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).gold == g1 - 2 && e.S(0).power.usesThisGame == 2);
}

static void TestGeorge()
{
    Env e(14);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_15"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    int plain = -1, shielded = -1;
    for (int c : CardsOfTier(1, true))
        (CardAt(c).keywords & kKwDivineShield ? shielded : plain) = c;
    CHECK(plain >= 0);
    if (shielded >= 0)
    {
        uint32_t const sh = PutBoard(e, 0, shielded, false, 1);
        int const gold = e.S(0).gold;
        CHECK(e.shop.HeroPower(0, sh) == ActionResult::BadTarget);   // a no-op is refused, not paid
        CHECK(e.S(0).gold == gold && e.S(0).power.usesThisTurn == 0);
        e.S(0).board.Remove(0);
    }
    uint32_t const uid = PutBoard(e, 0, plain, false, 1);
    CHECK(e.shop.HeroPower(0) == ActionResult::BadTarget);       // needs a friendly minion
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].Has(kKwDivineShield));
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::PowerUsed);
}

static void TestJaraxxus()
{
    Env e(15);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_37"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    int const demon = CardsOfTribe(kTribeDemon, true)[0];
    int plain = -1;
    for (int c : CardsOfTier(1, true))
        if (CardAt(c).tribes == 0)
        {
            plain = c;
            break;
        }
    CHECK(plain >= 0);
    uint32_t const dem = PutBoard(e, 0, demon, false, 1);
    uint32_t const pl = PutBoard(e, 0, plain, false, 1);
    int const demA = e.S(0).board[0].attack;
    int const demH = e.S(0).board[0].maxHealth;
    CHECK(e.shop.HeroPower(0) == ActionResult::BadTarget);        // needs a friendly minion
    CHECK(e.shop.HeroPower(0, pl) == ActionResult::BadTarget);    // ... and a Demon
    CHECK(e.S(0).power.usesThisTurn == 0);
    int sumA = 0, sumH = 0;
    for (Minion const& o : e.S(0).offers)
    {
        sumA += o.attack;
        sumH += o.health;
    }
    std::size_t const offers = e.S(0).offers.size();
    CHECK(offers > 0);
    CHECK(e.shop.HeroPower(0, dem) == ActionResult::Ok);
    CHECK(e.S(0).offers.size() == offers - 1);                   // one tavern minion consumed
    int leftA = 0, leftH = 0;
    for (Minion const& o : e.S(0).offers)
    {
        leftA += o.attack;
        leftH += o.health;
    }
    CHECK(e.S(0).board[0].attack == demA + (sumA - leftA));       // gains exactly its stats
    CHECK(e.S(0).board[0].maxHealth == demH + (sumH - leftH));
    CHECK(e.S(0).board[1].attack == CardAt(plain).attack);       // the other minion is untouched
    CHECK(e.shop.HeroPower(0, dem) == ActionResult::PowerUsed);
}

static void TestEdwin()
{
    Env e(16);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_01"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    // Render text (2026-10-19): "Give a minion +1/+1. Upgrades after you buy 4 minions."
    uint32_t const uid = PutBoard(e, 0, CardsOfTier(1, true)[0], false, 1);
    int const a0 = e.S(0).board[0].attack;
    int const h0 = e.S(0).board[0].maxHealth;
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].attack == a0 + 1 && e.S(0).board[0].maxHealth == h0 + 1);
    e.shop.StartTurn(0, 2);
    e.shop.AddGold(0, 20);
    for (int i = 0; i < 4; ++i)
    {
        if (e.S(0).offers.empty())
            CHECK(e.shop.Refresh(0) == ActionResult::Ok);
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    }
    CHECK(e.S(0).power.buysThisGame == 4);
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].attack == a0 + 1 + 2 && e.S(0).board[0].maxHealth == h0 + 1 + 2);   // upgraded: +2/+2
}

static void TestRakanishu()
{
    Env e(17);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_75"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    // Render text (2026-10-19): stats equal to YOUR Tier (the tavern is at tier 1), not the minion's
    int const card = CardsOfTier(3, true)[0];          // a tier 3 minion
    uint32_t const uid = PutBoard(e, 0, card, false, 1);
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].attack == CardAt(card).attack + 1);
    CHECK(e.S(0).board[0].maxHealth == CardAt(card).health + 1);
}

static void TestReno()
{
    Env e(18);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_41"));
    e.shop.StartTurn(0, 1);
    e.shop.AddGold(0, 10);
    int const card = CardsOfTier(1, true)[0];          // has a golden twin
    uint32_t const gold = PutBoard(e, 0, card, true, 1);
    CHECK(e.shop.HeroPower(0, gold) == ActionResult::BadTarget);  // already golden: refused, the once-per-game use is kept
    CHECK(e.S(0).power.usesThisGame == 0);
    e.S(0).board.Remove(0);
    uint32_t const uid = PutBoard(e, 0, card, false, 1);
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].golden);
    CHECK(e.S(0).board[0].attack == CardAt(card).goldenAttack);
    CHECK(e.S(0).board[0].poolCopies == 1);            // golden from one copy...
    CHECK(!e.shop.DiscoverOpen(0));                    // ...and no Triple Reward
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::PowerUsed);   // once per game
}

static void TestArmorAbsorbs()
{
    Env e(19);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_34"));   // Patchwerk, armor tier 2
    e.shop.StartTurn(0, 1);
    int const armor = e.S(0).armor;
    CHECK(armor >= 2 && armor <= 5);
    int const hp = e.S(0).health;
    CHECK(hp == 60);
    e.shop.TakeDamage(0, armor + 3);
    CHECK(e.S(0).armor == 0 && e.S(0).health == hp - 3);
    e.shop.TakeDamage(0, 5);
    CHECK(e.S(0).health == hp - 8);
    // DamageHero (own effects) takes armor first too.
    e.shop.SetHero(1, Hero("TB_BaconShop_HERO_34"));
    int const armor1 = e.S(1).armor;
    e.shop.DamageHero(1, armor1 + 2);
    CHECK(e.S(1).armor == 0 && e.S(1).health == 60 - 2);
}

static void TestNoPower()
{
    Env e(20);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_34"));   // Patchwerk has a passive power
    e.shop.StartTurn(0, 1);
    CHECK(e.shop.HeroPower(0) == ActionResult::NoPower);
}

// ------------------------------------------------------------------------------------ 15E batch 1 heroes

static int Card(char const* id)
{
    int const c = FindCard(id);
    CHECK(c >= 0);
    return c;
}

static Minion* FindMinion(Env& e, int p, uint32_t uid)
{
    return e.shop.FindMinion(p, uid);
}

static int HandOf(Env& e, int p, char const* id)
{
    int n = 0;
    for (Minion const& m : e.S(p).hand.Items())
        if (m.card == Card(id))
            ++n;
    return n;
}

static Minion Min(char const* id, int attack, int health, uint32_t uid = 0)
{
    Minion m = MakeMinion(Card(id), false, uid);
    m.attack = attack;
    m.health = m.maxHealth = health;
    return m;
}

static CombatSettings HeroSettings(int side, char const* powerId)
{
    CombatSettings s;
    s.effects = &NoEffects();
    s.hero[side] = HeroCombatDefFor(powerId);
    return s;
}

static void SummonDroplet(CombatCtx& c, bool) { c.Summon(Card("BGS_115t"), false); }

static void TestChenvaala()
{
    Env e(21);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_78"));
    e.shop.StartTurn(0, 1);
    int const before = e.shop.UpgradeCost(0);
    std::vector<int> const elementals = CardsOfTribe(kTribeElemental, true);
    CHECK(elementals.size() >= 3);
    for (int i = 0; i < 3 && i < static_cast<int>(elementals.size()); ++i)
    {
        PutHand(e, 0, elementals[static_cast<std::size_t>(i)]);
        CHECK(e.shop.Play(0, e.S(0).hand.Size() - 1, i) == ActionResult::Ok);
    }
    CHECK(e.shop.UpgradeCost(0) == before - 3);   // every 3 Elementals: 3 Gold off the upgrade
}

static void TestCurator()
{
    Env e(22);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_33"));
    CHECK(e.S(0).board.Size() == 1);
    CHECK(e.S(0).board[0].card == Card("TB_BaconShop_HP_033t"));
    CHECK(e.S(0).board[0].IsTribe(kTribeDemon) && e.S(0).board[0].IsTribe(kTribeMurloc));   // all minion types
    CHECK(e.S(0).board[0].attack == 1 && e.S(0).board[0].health == 1);
    CHECK(e.S(0).board[0].Has(kKwVenomous));
}

static void TestLichKing()
{
    Env e(23);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_22"));
    e.shop.StartTurn(0, 1);
    int const card = CardsOfTier(1, true)[0];
    uint32_t const uid = PutBoard(e, 0, card);
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].Has(kKwReborn));
    e.shop.StartTurn(0, 2);
    CHECK(!e.S(0).board[0].Has(kKwReborn));   // "until next turn"
}

static void TestPyramad()
{
    Env e(24);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_39"));
    e.shop.StartTurn(0, 1);
    e.shop.StartTurn(0, 2);   // not used on turn 1: the buff grows by one
    e.shop.AddGold(0, 5);
    uint32_t const uid = PutBoard(e, 0, CardsOfTier(1, true)[0]);
    int const h0 = e.S(0).board[0].health;
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).board[0].health == h0 + 3);   // +2 Health and +1 from the unused turn
}

static void TestDeathwing()
{
    CombatSide a;
    a.board.Push(Min("BGS_115t", 1, 10, 1));
    a.board.Push(Min("BGS_115t", 1, 10, 2));
    CombatSide b;
    b.board.Push(Min("BGS_115t", 1, 30, 8));
    CombatSettings s = HeroSettings(0, "TB_BaconShop_HP_061");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    // Start of Combat: ALL minions (both boards) get +2 Attack, so every hit deals 1 + 2.
    bool sawHit = false;
    for (CombatEvent const& ev : r.events)
        if (ev.type == EventType::Damage)
        {
            sawHit = true;
            CHECK(ev.amount == 3);
        }
    CHECK(sawHit);
    // Permanently: one +2 Attack write-back per starting minion, on both sides.
    int buffs[2] = { 0, 0 };
    for (PersistentChange const& c : r.persistent)
        if (c.kind == kPersistBuff && c.attack == 2 && c.health == 0 && c.inputIndex >= 0)
            ++buffs[c.side];
    CHECK(buffs[0] == 2);
    CHECK(buffs[1] == 1);
}

static void TestAlAkir()
{
    CombatSide a;
    a.board.Push(Min("BGS_115t", 0, 10, 1));
    CombatSide b;
    b.board.Push(Min("BGS_115t", 0, 10, 8));
    CombatSettings s = HeroSettings(0, "TB_BaconShop_HP_086");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    CHECK(r.stalled);
    CHECK(r.survivors[0].Size() == 1);
    Minion const& m = r.survivors[0][0];
    CHECK(m.Has(kKwWindfury) && m.Has(kKwDivineShield) && m.Has(kKwTaunt));
}

static void TestGreybough()
{
    static EffectRegistry const reg = []
    {
        EffectRegistry r;
        EffectDef d;
        d.deathrattle = &SummonDroplet;
        r.Register("BGS_121", d);
        return r;
    }();
    CombatSide a;
    a.board.Push(Min("BGS_121", 0, 1, 1));
    CombatSide b;
    b.board.Push(Min("BGS_115t", 9, 50, 8));
    CombatSettings s;
    s.effects = &reg;
    s.hero[0] = HeroCombatDefFor("TB_BaconShop_HP_107");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    bool buffed = false;
    for (CombatEvent const& ev : r.events)
        if (ev.type == EventType::Summon && ev.card == Card("BGS_115t") && ev.amount2 == 4 &&
            (ev.keywords & kKwTaunt) != 0)
            buffed = true;   // 2/2 + 1/+2 and Taunt
    CHECK(buffed);
}

static void TestRokara()
{
    CombatSide a;
    a.board.Push(Min("BGS_115t", 5, 10, 1));
    CombatSide b;
    b.board.Push(Min("BGS_115t", 0, 1, 8));
    CombatSettings s = HeroSettings(0, "BG20_HERO_100p");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    bool buffed = false;
    for (PersistentChange const& c : r.persistent)
        if (c.kind == kPersistBuff && c.attack == 1)
            buffed = true;
    CHECK(buffed);
}

static void TestAvengeHeroes()
{
    auto avenge = [](char const* power, int attacks, int healths, bool whelp)
    {
        CombatSide a;
        a.board.Push(Min("BGS_115t", 1, 500, 1));
        for (int i = 0; i < 5; ++i)
            a.board.Push(Min("BGS_115t", 0, 1, static_cast<uint32_t>(2 + i)));
        CombatSide b;
        b.board.Push(Min("BGS_115t", 10, 1000, 8));
        CombatSettings s = HeroSettings(0, power);
        CombatResult const r = SimulateCombat(a, b, 1, s);
        int buffA = 0;
        int buffH = 0;
        bool summoned = false;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistBuff)
            {
                if (c.attack == attacks)
                    ++buffA;
                if (c.health == healths)
                    ++buffH;
            }
        for (CombatEvent const& ev : r.events)
            if (ev.type == EventType::Summon && ev.card == Card("BG22_HERO_305t"))
                summoned = true;
        if (attacks > 0)
            CHECK(buffA > 0);
        if (healths > 0)
            CHECK(buffH > 0);
        if (whelp)
            CHECK(summoned);
    };
    avenge("BG22_HERO_002p", 1, 0, false);   // Drek'Thar: Avenge (3): +1 Attack
    avenge("BG22_HERO_003p", 0, 1, false);   // Vanndar: Avenge (2): +1 Health
    avenge("BG22_HERO_305p", 0, 0, true);    // Onyxia: Avenge (4): a 3/1 Whelp
}

static void TestIllidan()
{
    CombatSide a;
    a.board.Push(Min("BGS_115t", 1, 10, 1));
    a.board.Push(Min("BGS_115t", 1, 10, 2));
    CombatSide b;
    b.board.Push(Min("BGS_115t", 0, 40, 8));
    CombatSettings s = HeroSettings(0, "TB_BaconShop_HP_069");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    bool plusTwo = false;
    for (CombatEvent const& ev : r.events)
        if (ev.type == EventType::Damage && ev.amount == 3)   // 1 + 2, from the immediate attack
            plusTwo = true;
    CHECK(plusTwo);
}

// ------------------------------------------------------------------------------------ 15E batch 2 heroes

static void Offer(Env& e, int p, int card)
{
    Minion m = MakeMinion(card, false, e.shop.NewUid());
    if (e.pool.Take(card))
        m.poolCopies = 1;
    e.S(p).offers.push_back(m);
}

static HeroPowerDef const* PowerOf(Env& e, int p)
{
    return HeroPowerRegistry::Global().Find(HeroAt(e.S(p).hero).powerId);
}

static void TestPatchesPirate()
{
    Env e(30);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_18"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    HeroPowerDef const* const def = PowerOf(e, 0);
    CHECK(def != nullptr);
    int const before = HeroPowerCost(*def, e.S(0).power);
    e.S(0).offers.clear();
    Offer(e, 0, Card("BGS_061"));
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(HeroPowerCost(*def, e.S(0).power) == before - 1);   // a bought Pirate discounts the next use
}

static void TestEudora()
{
    Env e(31);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_64"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    for (int i = 0; i < 4; ++i)
        CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Empty());                         // four digs, no Golden yet
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);     // the fifth
    CHECK(e.S(0).hand.Size() == 1 && e.S(0).hand[0].golden);
}

static void TestTogwaggle()
{
    Env e(32);
    e.shop.SetHero(0, Hero("BG23_HERO_305"));
    e.shop.StartTurn(0, 2);
    HeroPowerDef const* const def = PowerOf(e, 0);
    CHECK(def != nullptr);
    CHECK(HeroPowerCost(*def, e.S(0).power) == 9);     // 10, one less after a turn
    e.S(0).gold = 20;
    e.S(0).offers.clear();
    Offer(e, 0, CardsOfTier(1, true)[0]);
    Offer(e, 0, CardsOfTier(1, true)[1]);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);    // steal both
    CHECK(e.S(0).hand.Size() == 2);
}

static void TestGuff()
{
    Env e(33);
    e.shop.SetHero(0, Hero("BG20_HERO_242"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    PutBoard(e, 0, CardsOfTier(1, true)[0]);
    PutBoard(e, 0, CardsOfTier(3, true)[0]);
    int const a0 = e.S(0).board[0].attack;
    int const a1 = e.S(0).board[1].attack;
    int const h0 = e.S(0).board[0].health;
    int const h1 = e.S(0).board[1].health;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).board[0].attack == a0 + 2);
    CHECK(e.S(0).board[1].attack == a1 + 2);
    CHECK(e.S(0).board[0].health == h0 + 1);
    CHECK(e.S(0).board[1].health == h1 + 1);
}

static void TestWagtoggle()
{
    // Wax Warband (render): Start of Combat gives up to 3 minions of different types stats equal to their Tier.
    CombatSide a;
    a.board.Push(Min("BGS_115t", 1, 10, 1));   // Elemental, tier 1
    a.board.Push(Min("BG_CFM_315", 1, 10, 2)); // Beast, tier 1
    a.board.Push(Min("BG_BOT_312t", 1, 10, 3)); // Mech, tier 1
    a.board.Push(Min("BGS_061t", 1, 10, 4));   // Pirate, tier 1
    CombatSide b;
    b.board.Push(Min("BGS_061t", 0, 100, 8));
    CombatSettings s;
    s.effects = &NoEffects();
    s.hero[0] = HeroCombatDefFor("TB_BaconShop_HP_037a");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    int buffed = 0;
    for (Minion const& m : r.survivors[0].Items())
        if (m.attack == 2 && m.health == 11)
            ++buffed;   // 1/10 + 1/1
    CHECK(buffed == 3);   // three different types, not the fourth
}

static void TestCariel()
{
    Env e(35);
    e.shop.SetHero(0, Hero("BG21_HERO_000"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    std::vector<int> const tier1 = CardsOfTier(1, true);
    for (int i = 0; i < 3; ++i)
        PutBoard(e, 0, tier1[static_cast<std::size_t>(i)]);
    int const a0 = e.S(0).board[0].attack;
    int const a1 = e.S(0).board[1].attack;
    int const a2 = e.S(0).board[2].attack;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    int buffed = 0;
    buffed += e.S(0).board[0].attack == a0 + 1 ? 1 : 0;
    buffed += e.S(0).board[1].attack == a1 + 1 ? 1 : 0;
    buffed += e.S(0).board[2].attack == a2 + 1 ? 1 : 0;
    CHECK(buffed == 2);                                 // two random minions at Tier 1

    // After a Tavern upgrade the player chooses an improvement; the three are offered each time and kept for good.
    auto const upgradeAndChoose = [&e](char const* id)
    {
        e.S(0).gold = 20;
        CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
        std::vector<std::string> const& options = e.shop.PowerChoices(0);
        CHECK(options.size() == 3);
        int pick = -1;
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
            if (options[static_cast<std::size_t>(i)] == id)
                pick = i;
        CHECK(pick >= 0);
        CHECK(e.shop.Choose(0, pick) == ActionResult::Ok);
        CHECK(e.shop.PowerChoices(0).empty());
        e.S(0).power.usesThisTurn = 0;
    };
    auto const cast = [&e]()
    {
        int before[3];
        for (int i = 0; i < 3; ++i)
            before[i] = e.S(0).board[i].attack;
        int hp[3];
        for (int i = 0; i < 3; ++i)
            hp[i] = e.S(0).board[i].maxHealth;
        CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
        e.S(0).power.usesThisTurn = 0;
        std::vector<std::pair<int, int>> gains;
        for (int i = 0; i < 3; ++i)
            if (e.S(0).board[i].attack != before[i] || e.S(0).board[i].maxHealth != hp[i])
                gains.push_back({ e.S(0).board[i].attack - before[i], e.S(0).board[i].maxHealth - hp[i] });
        return gains;
    };
    upgradeAndChoose(kStanceUnited);
    auto gains = cast();
    CHECK(gains.size() == 3 && gains[0].first == 1 && gains[0].second == 1);   // one more minion
    upgradeAndChoose(kStanceOffense);
    gains = cast();
    CHECK(gains.size() == 3 && gains[0].first == 3 && gains[0].second == 1);   // +2 Attack to each buff
    upgradeAndChoose(kStanceDefense);
    gains = cast();
    CHECK(gains.size() == 3 && gains[0].first == 3 && gains[0].second == 3);   // and +2 Health
    CHECK(e.shop.CheckConservation());
}

static void TestSaurfang()
{
    Env e(36);
    e.shop.SetHero(0, Hero("BG20_HERO_102"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    e.S(0).offers.clear();
    int const card = CardsOfTier(1, true)[0];
    Offer(e, 0, card);
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);   // turns 1-2: +2/+1
    CHECK(e.S(0).offers[0].attack == CardAt(card).attack + 2);
    CHECK(e.S(0).offers[0].health == CardAt(card).health + 1);

    Env g(42);
    g.shop.SetHero(0, Hero("BG20_HERO_102"));
    g.shop.StartTurn(0, 3);                                 // the buff doubles every three turns
    g.S(0).gold = 20;
    g.S(0).offers.clear();
    Offer(g, 0, card);
    CHECK(g.shop.HeroPower(0, 0, 0) == ActionResult::Ok);   // turn 3: +4/+2
    CHECK(g.S(0).offers[0].attack == CardAt(card).attack + 4);
    CHECK(g.S(0).offers[0].health == CardAt(card).health + 2);
}

static void TestRatKing()
{
    Env e(37);
    // The power discovers from the Turn's type; stock every playable tribe in the test pool.
    uint32_t all = 0;
    for (uint32_t bit = 1; bit < kTribeAll; bit <<= 1)
        all |= bit;
    e.pool.AddTribeCards(0, all, true);
    e.shop.SetLobbyTribes(0, SliceTribes(true));
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_12"));
    e.shop.StartTurn(0, 2);
    uint32_t const tribe = e.S(0).power.ratKingTribe;
    CHECK(tribe != 0);
    e.S(0).gold = 20;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);         // Discover a minion of the turn's type
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(!e.S(0).options.empty());
    for (Minion const& m : e.S(0).options)
        CHECK(m.IsTribe(tribe));
}

static void TestKurtrus()
{
    Env e(38);
    e.shop.SetHero(0, Hero("BG20_HERO_280"));
    e.shop.StartTurn(0, 2);
    e.S(0).gold = 40;
    e.S(0).offers.clear();
    std::vector<int> const tier1 = CardsOfTier(1, true);
    CHECK(tier1.size() >= 3);
    std::vector<int> bought;
    for (int i = 0; i < 3; ++i)
    {
        Offer(e, 0, tier1[static_cast<std::size_t>(i)]);
        bought.push_back(tier1[static_cast<std::size_t>(i)]);
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    }
    CHECK(e.S(0).power.kurtrusRank == 1);
    CHECK(e.S(0).hand.Size() == 3);
    for (int i = 0; i < e.S(0).hand.Size(); ++i)
        CHECK(e.S(0).hand[i].attack == CardAt(bought[static_cast<std::size_t>(i)]).attack + 2);
}

static void TestYsera()
{
    Env e(39);
    e.pool.AddTribeCards(0, kTribeDragon, true);        // make the lobby able to offer Dragons
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_53"));
    e.shop.StartTurn(0, 1);
    CHECK(static_cast<int>(e.S(0).offers.size()) == OffersForTier(e.S(0).tier) + 1);
    bool dragon = false;
    for (Minion const& m : e.S(0).offers)
        if (m.IsTribe(kTribeDragon))
            dragon = true;
    CHECK(dragon);
}

static void TestAlexstrasza()
{
    Env e(40);
    e.pool.AddTribeCards(0, kTribeDragon, true);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_56"));
    e.shop.StartTurn(0, 1);
    e.S(0).tier = 4;
    e.S(0).gold = 40;
    e.S(0).upgradeCost = 5;
    CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
    CHECK(e.S(0).tier == 5);
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(!e.S(0).options.empty());
    for (Minion const& m : e.S(0).options)
        CHECK(m.IsTribe(kTribeDragon));
}

static void TestCthun()
{
    Env e(41);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_29"));
    e.shop.StartTurn(0, 2);
    e.S(0).gold = 20;
    PutBoard(e, 0, CardsOfTier(1, true)[0]);
    int const a0 = e.S(0).board[0].attack;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(e.S(0).board[0].attack == a0 + 2);            // one end-of-turn buff and one repeat
}

static void TestVashj()
{
    Env e(50, 2, EffectRegistry::Global());
    e.shop.SetHero(0, Hero("BG23_HERO_304"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    uint32_t const target = PutBoard(e, 0, Card("BG19_010"));
    int const before = FindMinion(e, 0, target)->attack;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(!e.S(0).options.empty());
    for (Minion const& m : e.S(0).options)
        CHECK(CardAt(m.card).role == CardRole::Spell && m.permanentSpell);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    int const idx = e.S(0).hand.Size() - 1;
    CHECK(idx >= 0 && CardAt(e.S(0).hand[idx].card).role == CardRole::Spell);
    ActionResult const cr = e.shop.Cast(0, idx, target);
    CHECK(cr == ActionResult::Ok);
    CHECK(FindMinion(e, 0, target)->attack > before);
    int const after = FindMinion(e, 0, target)->attack;
    e.shop.StartTurn(0, 2);
    CHECK(FindMinion(e, 0, target)->attack == after);        // "its effect is permanent"
}

static void TestAzshara()
{
    Env e(51);
    e.pool.AddTribeCards(0, kTribeNaga, true);
    e.shop.SetLobbyTribes(0, SliceTribes(true));
    e.shop.SetHero(0, Hero("BG22_HERO_007"));
    e.shop.StartTurn(0, 1);
    CHECK(std::string(e.shop.Power(0)->id) == "BG22_HERO_007p");
    Minion big = MakeMinion(Card("BG23_013"), false, e.shop.NewUid());
    big.attack = 25;
    e.S(0).board.Push(big);
    e.shop.Settle(0);
    CHECK(e.S(0).power.nagaConquest);
    CHECK(std::string(e.shop.Power(0)->id) == "BG22_HERO_007p2");
    e.S(0).gold = 20;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.shop.DiscoverOpen(0));
    for (Minion const& m : e.S(0).options)
        CHECK(m.IsTribe(kTribeNaga));
}

static void TestMukla()
{
    Env e(52, 2);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_38"));
    e.shop.StartTurn(0, 1);
    CHECK(HandOf(e, 0, "TB_BaconShop_HP_038t") == 2);
    CHECK(HandOf(e, 1, "TB_BaconShop_HP_038t") == 1);
}

static void TestBarov()
{
    Env e(53, 2);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_72"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    CHECK(e.shop.HeroPower(0) == ActionResult::BadTarget);        // a guess names a seat
    CHECK(e.shop.HeroPower(0, 0, 2) == ActionResult::BadTarget);  // ... one that exists
    CHECK(e.shop.HeroPower(0, 0, 1) == ActionResult::Ok);
    int const guess = e.S(0).power.barovGuess;
    CHECK(guess == 1);
    e.shop.SetLastCombatWon(guess, true);
    CHECK(HandOf(e, 0, "TB_BaconShop_HP_008a") == 3);
    CHECK(e.S(0).power.barovGuess == -1);
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);
    e.shop.ClearBarovGuesses();
}

static void TestHolmes()
{
    Env e(54, 2);
    e.shop.SetHero(0, Hero("BG23_HERO_303"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    Board last;
    last.Push(MakeMinion(Card("BG19_010"), false, e.shop.NewUid()));
    e.shop.SetLastOpponent(0, last);
    // The power opens two options, one of them the opponent's minion; the right one is a Coin, the wrong one nothing.
    for (int round = 0; round < 2; ++round)
    {
        e.S(0).power.usesThisTurn = 0;
        CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
        CHECK(e.S(0).options.size() == 2);
        CHECK(e.S(0).power.holmesOpponent == Card("BG19_010"));
        int right = -1;
        for (int i = 0; i < 2; ++i)
            if (e.S(0).options[static_cast<std::size_t>(i)].card == Card("BG19_010"))
                right = i;
        CHECK(right >= 0);
        int const before = HandOf(e, 0, "TB_BaconShop_HP_008a");
        CHECK(e.shop.Choose(0, round == 0 ? right : 1 - right) == ActionResult::Ok);
        CHECK(HandOf(e, 0, "TB_BaconShop_HP_008a") == before + (round == 0 ? 1 : 0));
        CHECK(e.S(0).options.empty());
        CHECK(e.shop.CheckConservation());
    }
}

static void TestZephrys()
{
    Env e(55);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_91"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 40;
    PutBoard(e, 0, Card("BG19_010"));
    PutBoard(e, 0, Card("BG19_010"));
    int const wish = e.shop.PairForWish(0);
    CHECK(wish == Card("BG19_010"));
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).power.usesThisGame == 1);
    bool golden = false;
    for (Minion const& m : e.S(0).hand.Items())
        golden = golden || (m.card == Card("BG19_010") && m.golden);
    CHECK(golden);                                          // the third copy completes the triple
}

static void TestMalygos()
{
    Env e(56);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_58"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    e.S(0).offers.clear();
    Offer(e, 0, Card("BGS_061"));
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);   // twice per turn
    CHECK(e.S(0).power.usesThisTurn == 2);
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::PowerUsed || e.S(0).offers.empty());
}

static void TestGalakrond()
{
    Env e(57);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_02"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    e.S(0).offers.clear();
    Offer(e, 0, Card("BG19_010"));                          // tier 2
    int const old = CardAt(e.S(0).offers[0].card).tier;
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);
    CHECK(!e.S(0).offers.empty() && e.S(0).offers[0].Tier() >= old);
}

static void TestJandice()
{
    Env e(58);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_71"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    uint32_t const mine = PutBoard(e, 0, Card("BG19_010"));
    e.S(0).offers.clear();
    Offer(e, 0, Card("BGS_061"));
    CHECK(e.shop.HeroPower(0, mine) == ActionResult::Ok);
    CHECK(e.S(0).board.Size() == 1 && e.S(0).board[0].card == Card("BGS_061"));
    CHECK(!e.S(0).offers.empty() && e.S(0).offers[0].card == Card("BG19_010"));
}

static void TestVoljin()
{
    Env e(59);
    e.shop.SetHero(0, Hero("BG20_HERO_201"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    uint32_t const mine = PutBoard(e, 0, Card("BG19_010"));
    uint32_t const mine2 = PutBoard(e, 0, Card("BGS_061"));
    e.S(0).offers.clear();
    Offer(e, 0, Card("BGS_061"));
    uint32_t const theirs = e.S(0).offers[0].uid;
    int const a = e.S(0).board[0].attack;
    int const h = e.S(0).board[0].maxHealth;
    int const oa = e.S(0).offers[0].attack;
    int const oh = e.S(0).offers[0].maxHealth;
    CHECK(e.shop.HeroPower(0, mine) == ActionResult::BadTarget);          // two minions are needed
    CHECK(e.shop.HeroPower(0, mine, -1, mine) == ActionResult::BadTarget);
    CHECK(e.shop.HeroPower(0, mine, -1, 987654) == ActionResult::BadTarget);
    CHECK(e.shop.HeroPower(0, mine, -1, theirs) == ActionResult::Ok);     // a board and a tavern minion
    CHECK(e.S(0).board[0].attack == oa && e.S(0).board[0].maxHealth == oh);
    CHECK(e.S(0).offers[0].attack == a && e.S(0).offers[0].maxHealth == h);
    e.S(0).power.usesThisTurn = 0;
    int const a1 = e.S(0).board[0].attack;
    int const a2 = e.S(0).board[1].attack;
    CHECK(e.shop.HeroPower(0, mine, -1, mine2) == ActionResult::Ok);      // two board minions
    CHECK(e.S(0).board[0].attack == a2 && e.S(0).board[1].attack == a1);
}

static void TestXyrella()
{
    Env e(60);
    e.shop.SetHero(0, Hero("BG20_HERO_101"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    e.S(0).offers.clear();
    Offer(e, 0, Card("BGS_061"));
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);
    CHECK(e.S(0).offers.empty() && e.S(0).hand.Size() == 1);
    CHECK(e.S(0).hand[0].card == Card("BGS_061") && e.S(0).hand[0].attack == 2 && e.S(0).hand[0].health == 2);
}

static void TestYogg()
{
    Env e(61);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_35"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    e.S(0).offers.clear();
    Offer(e, 0, Card("BGS_061"));
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).offers.empty() && e.S(0).hand.Size() == 1);
    CHECK(e.S(0).hand[0].card == Card("BGS_061") && e.S(0).hand[0].attack == 4 && e.S(0).hand[0].health == 2);
}

// ------------------------------------------------------------------------------------ 15E batch 4 heroes

static int HandOf(Env& e, int p, int card)
{
    int n = 0;
    for (Minion const& m : e.S(p).hand.Items())
        if (m.card == card)
            ++n;
    return n;
}

static void TestBlackthorn()
{
    Env e(70);
    e.shop.SetHero(0, Hero("BG20_HERO_103"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);            // twice per turn
    CHECK(HandOf(e, 0, Card("BG20_GEM")) == 4);
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);
    e.shop.StartTurn(0, 2);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);            // a new turn allows two more
}

static void TestVarden()
{
    Env e(71);
    e.shop.SetHero(0, Hero("BG22_HERO_004"));
    e.shop.StartTurn(0, 1);
    int const before = static_cast<int>(e.S(0).offers.size());
    CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    CHECK(static_cast<int>(e.S(0).offers.size()) == before + 1);
    CHECK(!e.shop.OffersFrozen(0));                            // not the whole tavern
    int best = 0;
    for (Minion const& m : e.S(0).offers)
        if (m.Tier() > best)
            best = m.Tier();
    int copies = 0;
    int frozenCount = 0;
    int frozenTop = 0;
    for (Minion const& m : e.S(0).offers)
    {
        if (m.Tier() == best)
            ++copies;
        if ((m.flags & kMinionFrozen) != 0)
        {
            ++frozenCount;
            frozenTop += m.Tier() == best ? 1 : 0;
        }
    }
    CHECK(copies >= 2);                                        // the copy of the highest Tier
    CHECK(frozenCount == 2 && frozenTop == 2);                 // only the original and the copy are Frozen
    CHECK((e.S(0).offers.back().flags & kMinionFrozen) != 0);
}

static void TestSindragosa()
{
    Env e(72);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_27"));
    e.shop.StartTurn(0, 1);
    CHECK(!e.S(0).offers.empty());
    std::vector<std::pair<int, int>> stats;
    for (Minion const& m : e.S(0).offers)
        stats.push_back({ m.attack, m.maxHealth });
    CHECK(e.shop.HeroPower(0, 0, 1) == ActionResult::Ok);      // freeze the second offer (cost 0)
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::PowerUsed);   // once per turn
    CHECK((e.S(0).offers[1].flags & kMinionFrozen) != 0 && !e.shop.OffersFrozen(0));
    CHECK((e.S(0).offers[0].flags & kMinionFrozen) == 0);
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);              // at the end of the turn only the frozen offer gets +2/+1
    CHECK(e.S(0).offers.size() == stats.size());
    for (std::size_t i = 0; i < stats.size() && i < e.S(0).offers.size(); ++i)
    {
        int const d = i == 1 ? 1 : 0;
        CHECK(e.S(0).offers[i].attack == stats[i].first + 2 * d && e.S(0).offers[i].maxHealth == stats[i].second + d);
    }
}

// Sindragosa: the frozen offer stays through the next turn start (without its mark), the others are replaced; the
// power needs no hand room and only targets an offer.
static void TestSindragosaCarry()
{
    Env e(74);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_27"));
    e.shop.StartTurn(0, 1);
    uint32_t const keep = e.S(0).offers[2].uid;
    uint32_t const gone = e.S(0).offers[0].uid;
    CHECK(e.shop.HeroPower(0, 0, 9) == ActionResult::BadTarget);
    CHECK(e.shop.HeroPower(0, 0, -1) == ActionResult::BadTarget);
    while (!e.S(0).hand.Full())
        e.S(0).hand.Push(MakeMinion(Card("BG20_100"), false, e.shop.NewUid()));
    CHECK(e.shop.HeroPower(0, 0, 2) == ActionResult::Ok);      // a full hand does not matter: it only freezes
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    e.shop.StartTurn(0, 2);
    CHECK(e.S(0).offers.size() == 3 && e.S(0).offers[0].uid == keep && e.S(0).offers[1].uid != gone);
    CHECK(!e.shop.AnyOfferFrozen(0));
    CHECK(e.S(0).offers[0].attack >= 2);                       // it kept the end of turn +2/+1
}

static void TestFlurgl()
{
    Env e(73);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_55"));
    e.shop.StartTurn(0, 1);
    e.S(0).hand.Clear();
    for (int i = 0; i < 5; ++i)
        PutBoard(e, 0, Card("BGS_115t"));
    for (int i = 0; i < 5; ++i)
        CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == 1);                             // the fifth sale added a random Murloc
    bool murloc = false;
    for (Minion const& m : e.S(0).hand.Items())
        if (m.IsTribe(kTribeMurloc))
            murloc = true;
    CHECK(murloc);
}

static void TestStormcoil()
{
    Env e(74);
    e.shop.SetHero(0, Hero("BG22_HERO_200"));
    e.shop.StartTurn(0, 1);
    e.S(0).hand.Clear();
    e.shop.CountFriendlyDeaths(0, 9);
    CHECK(e.S(0).hand.Empty());
    e.shop.CountFriendlyDeaths(0, 1);
    CHECK(e.S(0).hand.Size() == 1);
    CHECK(e.S(0).hand[0].IsTribe(kTribeMech));
}

static void TestDinotamerBrann()
{
    Env e(75, 2, EffectRegistry::Global());
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_43"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 20;
    e.S(0).hand.Clear();
    e.S(0).offers.clear();
    EffectRegistry const& reg = EffectRegistry::Global();
    std::vector<int> battlecries;
    for (int c = 0; c < CardCount() && static_cast<int>(battlecries.size()) < 5; ++c)
        if (CardAt(c).role == CardRole::Pool && CardAt(c).slice15A)
            if (EffectDef const* const d = reg.Find(c))
                if (d->battlecry)
                    battlecries.push_back(c);
    CHECK(battlecries.size() == 5);
    for (int c : battlecries)
        e.S(0).offers.push_back(MakeMinion(c, false, e.shop.NewUid()));
    for (int i = 0; i < 5; ++i)
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(HandOf(e, 0, Card("BG_LOE_077")) == 1);              // Brann Bronzebeard, once per game
}

static void TestMutanus()
{
    Env e(76);
    e.shop.SetHero(0, Hero("BG20_HERO_301"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    uint32_t const eaten = PutBoard(e, 0, Card("BG20_100"));   // 3/1
    uint32_t const to = PutBoard(e, 0, Card("BGS_115t"));      // 1/2
    CHECK(e.shop.HeroPower(0, eaten) == ActionResult::Ok);
    CHECK(e.S(0).board.Size() == 1);
    CHECK(e.S(0).board[0].uid == to && e.S(0).board[0].attack == 5 && e.S(0).board[0].maxHealth == 3);
    CHECK(e.S(0).gold == 11);                                    // the power costs 0 and gives 1
    CHECK(e.shop.HeroPower(0, 0) == ActionResult::PowerUsed);   // once per turn
}

// Review fix: Mutanus passes on base stats only, not "until next turn" stats.
static void TestMutanusTempStats()
{
    Env e(176);
    e.shop.SetHero(0, Hero("BG20_HERO_301"));
    e.shop.StartTurn(0, 1);
    uint32_t const eaten = PutBoard(e, 0, Card("BG20_100"));   // 3/1
    uint32_t const to = PutBoard(e, 0, Card("BGS_115t"));
    Minion& m = e.S(0).board[0];
    m.attack += 4;
    m.tempAttack = 4;
    m.health += 2;
    m.maxHealth += 2;
    m.tempHealth = 2;
    CHECK(e.shop.HeroPower(0, eaten) == ActionResult::Ok);
    CHECK(e.S(0).board[0].uid == to && e.S(0).board[0].attack == 5 && e.S(0).board[0].maxHealth == 3);   // as unbuffed
}

// Review fix: stat-setting effects drop the Blood Gem counters of the minions they overwrite.
static void TestBloodGemStatsReset()
{
    {
        Env e(177);
        e.shop.SetHero(0, Hero("BG20_HERO_201"));
        e.shop.StartTurn(0, 1);
        e.S(0).gold = 20;
        uint32_t const mine = PutBoard(e, 0, Card("BG19_010"));
        e.S(0).offers.clear();
        Offer(e, 0, Card("BGS_061"));
        e.S(0).board[0].bloodGemAttack = e.S(0).board[0].bloodGemHealth = 1;
        e.S(0).offers[0].bloodGemAttack = e.S(0).offers[0].bloodGemHealth = 2;
        CHECK(e.shop.HeroPower(0, mine, -1, e.S(0).offers[0].uid) == ActionResult::Ok);
        CHECK(e.S(0).board[0].bloodGemAttack == 0 && e.S(0).board[0].bloodGemHealth == 0);
        CHECK(e.S(0).offers[0].bloodGemAttack == 0 && e.S(0).offers[0].bloodGemHealth == 0);
    }
    {
        Env e(178);
        e.shop.SetHero(0, Hero("BG20_HERO_101"));
        e.shop.StartTurn(0, 1);
        e.S(0).gold = 20;
        e.S(0).offers.clear();
        Offer(e, 0, Card("BGS_061"));
        e.S(0).offers[0].bloodGemAttack = e.S(0).offers[0].bloodGemHealth = 3;
        CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);
        CHECK(e.S(0).hand.Size() == 1 && e.S(0).hand[0].bloodGemAttack == 0 && e.S(0).hand[0].bloodGemHealth == 0);
    }
}

static void TestTamsin()
{
    CombatSide a;
    for (uint32_t i = 0; i < 7; ++i)
        a.board.Push(Min("BGS_115t", 1, 10 + static_cast<int>(i), i + 1));
    CombatSide b;
    b.board.Push(Min("BGS_115t", 0, 100, 100));
    CombatSettings s = HeroSettings(0, "BG20_HERO_282p");
    CombatResult const r = SimulateCombat(a, b, 1, s);
    CHECK(r.survivors[0].Size() == 6);                          // the lowest-Health minion was destroyed
    int buffs = 0;
    for (CombatEvent const& ev : r.events)
        if (ev.type == EventType::Buff && ev.amount == 1 && ev.amount2 == 10)
            ++buffs;
    CHECK(buffs == 5);                                          // its stats to five friendly minions
}

static void TestTess()
{
    Env e(77);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_50"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    Board last;
    last.Push(MakeMinion(Card("BG20_100"), false, 1));
    last.Push(MakeMinion(Card("BG20_101"), false, 2));
    e.shop.SetLastOpponent(0, last);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).offers.size() == 2);
    CHECK(e.S(0).offers[0].card == Card("BG20_100") && e.S(0).offers[1].card == Card("BG20_101"));
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);
}

// Review fix: Tess's offers are base-stat copies (golden kept), without the opponent's buffs or hats.
static void TestTessPlainCopies()
{
    Env e(179);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_50"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    Board last;
    Minion m = MakeMinion(Card("BG20_100"), false, 1);
    m.attack += 7;
    m.health += 7;
    m.maxHealth += 7;
    m.tempAttack = 3;
    m.hats = 2;
    m.bloodGemAttack = 4;
    last.Push(m);
    e.shop.SetLastOpponent(0, last);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).offers.size() == 1);
    Minion const base = MakeMinion(Card("BG20_100"), false, 0);
    Minion const& o = e.S(0).offers[0];
    CHECK(o.attack == base.attack && o.maxHealth == base.maxHealth && o.tempAttack == 0 && o.hats == 0 &&
          o.bloodGemAttack == 0);
}

static void TestScabbs()
{
    Env e(78);
    e.shop.SetHero(0, Hero("BG21_HERO_010"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    Board last;
    last.Push(MakeMinion(Card("BG20_100"), false, 1));
    last.Push(MakeMinion(Card("BG20_101"), false, 2));
    e.shop.SetLastOpponent(0, last);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).options.size() == 2);                          // the warband only had two minions
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == 1);
    CHECK(e.S(0).hand[0].card == Card("BG20_100") || e.S(0).hand[0].card == Card("BG20_101"));
}

// Review fix: Scabbs offers a plain (non-golden, base-stat) copy.
static void TestScabbsPlainCopy()
{
    Env e(180);
    e.shop.SetHero(0, Hero("BG21_HERO_010"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    Board last;
    Minion m = MakeMinion(Card("BG20_100"), true, 1);
    m.attack += 9;
    m.health += 9;
    m.maxHealth += 9;
    m.hats = 1;
    last.Push(m);
    e.shop.SetLastOpponent(0, last);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).options.size() == 1);
    Minion const base = MakeMinion(Card("BG20_100"), false, 0);
    Minion const& o = e.S(0).options[0];
    CHECK(!o.golden && o.attack == base.attack && o.maxHealth == base.maxHealth && o.hats == 0);
}

// ------------------------------------------------------------------------------------ 15E batch 5a heroes

// Shudderwock: trigger the Battlecry of the chosen friendly minion, once per turn, and only one that has one.
static void TestShudderwock()
{
    Env e(80, 2, EffectRegistry::Global());
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_23"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    uint32_t const plain = PutBoard(e, 0, Card("BGS_115t"));   // no Battlecry
    CHECK(e.shop.HeroPower(0, plain) == ActionResult::BadTarget);
    uint32_t const battlecry = PutBoard(e, 0, Card("BGS_116")); // Refreshing Anomaly: the next Refresh costs (0)
    CHECK(e.S(0).refreshDiscounts.empty());
    CHECK(e.shop.HeroPower(0, battlecry) == ActionResult::Ok);
    CHECK(!e.S(0).refreshDiscounts.empty());
    CHECK(e.shop.HeroPower(0, battlecry) == ActionResult::PowerUsed);
    e.shop.StartTurn(0, 2);
    CHECK(e.shop.HeroPower(0, battlecry) == ActionResult::Ok);   // a new turn
}

// Sneed: grant "Deathrattle: summon a random minion of a lower Tier"; a Tier 1 host summons nothing.
static void TestSneed()
{
    Env e(81);
    e.shop.SetHero(0, Hero("BG21_HERO_030"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    uint32_t const host = PutBoard(e, 0, Card("BG20_103"));   // a Tier 3 minion
    CHECK(e.shop.HeroPower(0, host) == ActionResult::Ok);
    CHECK(e.S(0).board[0].granted.size() == 1);
    CHECK(e.S(0).board[0].granted[0].kind == kGrantedSneedLowerTier);
    CHECK(e.shop.HeroPower(0, host) == ActionResult::PowerUsed);

    {
        CombatSide a;
        Minion m = MakeMinion(Card("BG20_103"), false, 1);
        m.attack = 0;
        m.health = m.maxHealth = 1;
        GrantedEffect g;
        g.kind = kGrantedSneedLowerTier;
        m.granted.push_back(g);
        a.board.Push(m);
        CombatSide b;
        b.board.Push(Min("BGS_115t", 20, 100, 8));
        CombatResult const r = SimulateCombat(a, b, 3);
        int summoned = 0;
        for (CombatEvent const& ev : r.events)
            if (ev.type == EventType::Summon && ev.side == 0 && ev.card >= 0 && CardAt(ev.card).tier == 2)
                ++summoned;
        CHECK(summoned == 1);
    }
    {
        CombatSide a;
        Minion m = MakeMinion(Card("BGS_115t"), false, 1);    // Tier 1
        m.attack = 0;
        m.health = m.maxHealth = 1;
        GrantedEffect g;
        g.kind = kGrantedSneedLowerTier;
        m.granted.push_back(g);
        a.board.Push(m);
        CombatSide b;
        b.board.Push(Min("BGS_115t", 20, 100, 8));
        CombatResult const r = SimulateCombat(a, b, 3);
        int summoned = 0;
        for (CombatEvent const& ev : r.events)
            if (ev.type == EventType::Summon && ev.side == 0)
                ++summoned;
        CHECK(summoned == 0);
    }
}

// N'Zoth: starts with the Fish, which gains the Deathrattle of a friendly Deathrattle minion that dies (twice, golden).
static void TestNzoth()
{
    Env e(82, 2, EffectRegistry::Global());
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_93"));
    CHECK(e.S(0).board.Size() == 1);
    CHECK(e.S(0).board[0].card == Card("TB_BaconShop_HP_105t"));

    for (int g = 0; g < 2; ++g)
    {
        CombatSide a;
        Minion fish = MakeMinion(Card("TB_BaconShop_HP_105t"), g != 0, 1);
        fish.attack = 0;
        fish.health = fish.maxHealth = 100;
        a.board.Push(fish);
        a.board.Push(Min("BG_OG_256", 1, 1, 2));   // Spawn of N'Zoth: Deathrattle
        CombatSide b;
        b.board.Push(Min("BGS_115t", 1, 1, 8));   // the Spawn trades with it; the Fish is never attacked
        CombatResult const r = SimulateCombat(a, b, 5);
        Minion const* after = nullptr;
        for (Minion const& m : r.survivors[0].Items())
            if (m.uid == 1)
                after = &m;
        CHECK(after != nullptr);
        if (after)
            CHECK(static_cast<int>(after->granted.size()) == (g != 0 ? 2 : 1));
    }
    CHECK(e.S(0).board[0].granted.empty());   // the gained Deathrattles are combat-only (the render)
}

// Ozumat: Start of Combat, a 2/2 Taunt Tentacle with +1/+1 for every minion sold this game.
static void TestOzumat()
{
    Env e(83);
    e.shop.SetHero(0, Hero("BG23_HERO_201"));
    e.shop.StartTurn(0, 1);
    for (int i = 0; i < 3; ++i)
        PutBoard(e, 0, Card("BGS_115t"));
    for (int i = 0; i < 3; ++i)
        CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
    CHECK(e.S(0).power.minionsSoldThisGame == 3);

    CombatSide a;
    CombatSide b;
    b.board.Push(Min("BGS_115t", 1, 10, 8));
    CombatSettings s;
    s.effects = &NoEffects();
    s.hero[0] = HeroCombatDefFor("BG23_HERO_201p");
    s.hero[0].ozumatSold = e.S(0).power.minionsSoldThisGame;
    CombatResult const r = SimulateCombat(a, b, 7, s);
    CHECK(!r.survivors[0].Empty());
    if (!r.survivors[0].Empty())
    {
        Minion const& t = r.survivors[0][0];
        CHECK(t.card == Card("BG23_HERO_201pt"));
        CHECK(t.attack == 5 && t.maxHealth == 5);   // 2/2 + 3/3
        CHECK(t.Has(kKwTaunt));
    }
}

// Y'Shaarj: the active arms the next combat; its Start of Combat adds a Tier minion and summons a copy.
static void TestYshaarj()
{
    Env e(84);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_92"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).power.combatArmed);
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);

    CombatSide a;
    e.S(0).tier = 4;
    a.tavernTier = 4;
    CombatSide b;
    b.board.Push(Min("BGS_115t", 1, 100, 8));
    CombatSettings s;
    s.effects = &NoEffects();
    s.hero[0] = HeroCombatDefFor("TB_BaconShop_HP_103");
    for (int card : e.pool.Cards())
        s.lobbyTribes[0] |= CardAt(card).tribes & (kAllTribesMask & ~kTribeAll);
    CombatResult const r = SimulateCombat(a, b, 9, s);
    int added = 0;
    int card = -1;
    for (PersistentChange const& c : r.persistent)
        if (c.side == 0 && c.kind == kPersistAddPoolToHand)
        {
            ++added;
            card = c.card;
        }
    CHECK(added == 1);
    CHECK(card >= 0 && CardAt(card).tier == 4);
    int summoned = 0;
    for (CombatEvent const& ev : r.events)
        if (ev.type == EventType::Summon && ev.side == 0 && ev.card == card)
            ++summoned;
    CHECK(summoned == 1);
    CHECK(e.shop.AddPoolCardToHand(0, card, false));
    CHECK(HandOf(e, 0, card) == 1);
}

// Rafaam: the combat reports each side's kills in order; the first enemy kill is taken from the pool as a plain copy.
static void TestRafaam()
{
    Env e(85);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_45"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).power.combatArmed);
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);

    CombatSide a;
    a.board.Push(Min("BGS_115t", 10, 10, 1));
    CombatSide b;
    b.board.Push(Min("BG_OG_256", 1, 1, 8));   // a tribeless pool minion: always in the lobby
    CombatResult const r = SimulateCombat(a, b, 11);
    CHECK(r.kills[0].size() == 1 && r.kills[0][0] == Card("BG_OG_256"));
    CHECK(r.kills[1].empty());
    int const before = e.pool.Left(Card("BG_OG_256"));
    CHECK(before > 0);
    CHECK(e.shop.AddPoolCardToHand(0, r.kills[0][0], false));
    CHECK(e.pool.Left(Card("BG_OG_256")) == before - 1);
    CHECK(HandOf(e, 0, Card("BG_OG_256")) == 1);
}

// ------------------------------------------------------------------------------------ the power swap

static int ChoiceOf(Env& e, int p, char const* powerId)
{
    std::vector<std::string> const& list = e.S(p).powerChoices;
    for (int i = 0; i < static_cast<int>(list.size()); ++i)
        if (list[static_cast<std::size_t>(i)] == powerId)
            return i;
    return -1;
}

// Galewing: Dungar's Gryphon offers three flightpaths; the chosen one completes after its countdown and gives the render
// bonus (Westfall +2/+2, Ironforge 2 Gold, Eastern Plaguelands a Discover of the Tavern Tier).
static void TestGalewing()
{
    {
        Env e(200);
        e.shop.SetHero(0, Hero("BG20_HERO_283"));
        e.shop.StartTurn(0, 1);
        e.S(0).gold = 10;
        PutBoard(e, 0, Card("BGS_115t"));   // 1/2, the left-most minion
        CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
        CHECK(e.S(0).powerChoices.size() == 3);
        int const pick = ChoiceOf(e, 0, "BG20_HERO_283p_t1");   // Westfall: in 1 turn, +2/+2
        CHECK(pick >= 0);
        CHECK(e.shop.Choose(0, pick) == ActionResult::Ok);
        CHECK(e.S(0).power.powerOverride == "BG20_HERO_283p_t1");
        CHECK(e.S(0).power.powerCountdown == 1);
        CHECK(e.shop.Power(0) && std::string(e.shop.Power(0)->id) == "BG20_HERO_283p_t1");
        CHECK(e.shop.HeroPower(0) == ActionResult::NoPower);   // a flightpath is passive
        e.shop.StartTurn(0, 2);
        CHECK(e.S(0).power.powerOverride.empty());             // back to the Gryphon
        CHECK(std::string(e.shop.Power(0)->id) == "BG20_HERO_283p");
        CHECK(e.S(0).board[0].attack == 4 && e.S(0).board[0].maxHealth == 4);   // +2/+2 on a 2/2
    }
    {
        Env e(201);
        e.shop.SetHero(0, Hero("BG20_HERO_283"));
        e.shop.StartTurn(0, 1);
        CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
        int const pick = ChoiceOf(e, 0, "BG20_HERO_283p_t2");   // Ironforge: in 2 turns, gain 2 Gold
        CHECK(pick >= 0 && e.shop.Choose(0, pick) == ActionResult::Ok);
        CHECK(e.S(0).power.powerCountdown == 2);
        e.shop.StartTurn(0, 2);
        CHECK(e.S(0).power.powerCountdown == 1);
        CHECK(e.S(0).gold == GoldForTurn(2));
        e.shop.StartTurn(0, 3);
        CHECK(e.S(0).power.powerOverride.empty());
        CHECK(e.S(0).gold == GoldForTurn(3) + 2);
    }
    {
        Env e(202);
        e.shop.SetHero(0, Hero("BG20_HERO_283"));
        e.shop.StartTurn(0, 1);
        CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
        int const pick = ChoiceOf(e, 0, "BG20_HERO_283p_t3");   // Eastern Plaguelands: in 3 turns, Discover
        CHECK(pick >= 0 && e.shop.Choose(0, pick) == ActionResult::Ok);
        e.shop.StartTurn(0, 2);
        e.shop.StartTurn(0, 3);
        CHECK(!e.shop.DiscoverOpen(0));
        e.shop.StartTurn(0, 4);
        CHECK(e.S(0).power.powerOverride.empty());
        CHECK(e.shop.DiscoverOpen(0) && !e.S(0).options.empty());
        for (Minion const& m : e.S(0).options)
            CHECK(m.Tier() == e.S(0).tier);
    }
}

// Bru'kan: the chosen invocation stays and acts at Start of Combat.
static void TestBrukan()
{
    Env e(203, 2, EffectRegistry::Global());
    e.shop.SetHero(0, Hero("BG22_HERO_001"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).powerChoices.size() == 4);
    int const pick = ChoiceOf(e, 0, "BG22_HERO_001p_t2");   // Fire
    CHECK(pick >= 0 && e.shop.Choose(0, pick) == ActionResult::Ok);
    CHECK(e.S(0).power.combatPower == "BG22_HERO_001p_t2");
    CHECK(e.S(0).power.powerOverride.empty());              // the active power stays
    CHECK(e.shop.Power(0) && std::string(e.shop.Power(0)->id) == "BG22_HERO_001p");
    e.shop.StartTurn(0, 2);
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);         // and can be used again on the next turn
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);

    {
        // Fire: double the left-most minion's Attack.
        CombatSide a;
        a.board.Push(Min("BGS_115t", 4, 10, 1));
        CombatSide b;
        b.board.Push(Min("BGS_115t", 1, 10, 8));
        CombatResult const r = SimulateCombat(a, b, 3, HeroSettings(0, "BG22_HERO_001p_t2"));
        CHECK(!r.survivors[0].Empty() && r.survivors[0][0].attack == 8);
    }
    {
        // Earth: four random friends gain the Stone Elemental Deathrattle.
        CombatSide a;
        for (uint32_t i = 0; i < 5; ++i)
            a.board.Push(Min("BGS_115t", 0, 50, i + 1));
        CombatSide b;
        b.board.Push(Min("BGS_115t", 0, 50, 8));
        CombatResult const r = SimulateCombat(a, b, 5, HeroSettings(0, "BG22_HERO_001p_t1"));
        int granted = 0;
        for (Minion const& m : r.survivors[0].Items())
            for (GrantedEffect const& g : m.granted)
                if (g.kind == kGrantedStoneElemental)
                    ++granted;
        CHECK(granted == 4);
    }
    {
        // The granted Deathrattle summons the 1/1 Stone Elemental.
        CombatSide a;
        Minion host = Min("BGS_115t", 0, 1, 1);
        GrantedEffect g;
        g.kind = kGrantedStoneElemental;
        host.granted.push_back(g);
        a.board.Push(host);
        CombatSide b;
        b.board.Push(Min("BGS_115t", 5, 10, 8));
        CombatResult const r = SimulateCombat(a, b, 7);
        int summoned = 0;
        for (CombatEvent const& ev : r.events)
            if (ev.type == EventType::Summon && ev.side == 0 && ev.card == Card("BG22_HERO_001p_t1et"))
                ++summoned;
        CHECK(summoned == 1);
    }
    {
        // Water: the right-most minion gets +3 Health and Taunt.
        CombatSide a;
        a.board.Push(Min("BGS_115t", 0, 10, 1));
        a.board.Push(Min("BGS_115t", 0, 10, 2));
        CombatSide b;
        b.board.Push(Min("BGS_115t", 0, 10, 8));
        CombatResult const r = SimulateCombat(a, b, 9, HeroSettings(0, "BG22_HERO_001p_t3"));
        CHECK(r.survivors[0].Size() == 2);
        CHECK(r.survivors[0][0].maxHealth == 10);
        CHECK(r.survivors[0][1].maxHealth == 13 && r.survivors[0][1].Has(kKwTaunt));
    }
    {
        // Lightning: 1 damage to five random enemies.
        CombatSide a;
        a.board.Push(Min("BGS_115t", 0, 10, 1));
        CombatSide b;
        for (uint32_t i = 0; i < 5; ++i)
            b.board.Push(Min("BGS_115t", 0, 1, 8 + i));
        CombatResult const r = SimulateCombat(a, b, 11, HeroSettings(0, "BG22_HERO_001p_t4"));
        CHECK(r.survivors[1].Empty());
    }
}

// Tavish: the aim chosen in the shop is the Start of Combat hit; 2 damage doubling every 3 turns.
static void TestTavish()
{
    Env e(204);
    e.shop.SetHero(0, Hero("BG22_HERO_000"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.S(0).powerChoices.size() == 4);
    int const pick = ChoiceOf(e, 0, "BG22_HERO_000p_t1");   // Aim Left!
    CHECK(pick >= 0 && e.shop.Choose(0, pick) == ActionResult::Ok);
    CHECK(e.S(0).power.combatPower == "BG22_HERO_000p_t1");
    CHECK(e.S(0).power.combatPowerOnce);                    // the aim lasts one combat

    auto hit = [](char const* aim, int round, int leftHealth, int rightHealth, int& leftAfter, int& rightAfter)
    {
        CombatSide a;
        a.board.Push(Min("BGS_115t", 0, 100, 1));
        CombatSide b;
        b.board.Push(Min("BGS_115t", 0, leftHealth, 8));
        b.board.Push(Min("BGS_115t", 0, rightHealth, 9));
        CombatSettings s = HeroSettings(0, aim);
        s.round = round;
        CombatResult const r = SimulateCombat(a, b, 13, s);
        leftAfter = r.survivors[1].Size() > 0 ? r.survivors[1][0].health : 0;
        rightAfter = r.survivors[1].Size() > 1 ? r.survivors[1][1].health : 0;
    };
    int l = 0;
    int r = 0;
    hit("BG22_HERO_000p_t1", 1, 10, 10, l, r);
    CHECK(l == 8 && r == 10);                               // 2 damage, left-most
    hit("BG22_HERO_000p_t4", 4, 10, 10, l, r);
    CHECK(l == 6 && r == 10);                               // 4 damage after 3 turns, left-most (the render of Aim Right)
    hit("BG22_HERO_000p_t2", 1, 10, 4, l, r);
    CHECK(l == 10 && r == 2);                               // lowest Health
    hit("BG22_HERO_000p_t3", 1, 4, 10, l, r);
    CHECK(l == 4 && r == 8);                                // highest Health
    hit("BG22_HERO_000p_t3", 1, 10, 4, l, r);
    CHECK(l == 8 && r == 4);
    hit("BG22_HERO_000p_t1", 7, 20, 20, l, r);
    CHECK(l == 12 && r == 20);                              // 8 damage in the 7th-9th turn
}

// Master Nguyen: at every StartTurn, a choice of 2 powers; the chosen one lasts the turn.
static void TestNguyen()
{
    Env e(205);
    e.shop.SetHero(0, Hero("BG20_HERO_202"));
    e.shop.StartTurn(0, 1);
    CHECK(e.S(0).powerChoices.size() == 2);
    CHECK(e.S(0).power.powerOverride.empty());
    std::string const first = e.S(0).powerChoices[0];
    CHECK(first != "BG20_HERO_202p");                       // its own power is never in the pool
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).power.powerOverride == first);
    CHECK(e.shop.Power(0) && std::string(e.shop.Power(0)->id) == first);
    e.shop.StartTurn(0, 2);
    CHECK(e.S(0).powerChoices.size() == 2);                  // a new choice each turn
    CHECK(e.S(0).power.powerOverride.empty());
    CHECK(std::string(e.shop.Power(0)->id) == "BG20_HERO_202p");
    // A swapped-in flightpath still pays on the next turn, and a swap takes its invocation with it.
    {
        Env f(215);
        f.shop.SetHero(0, Hero("BG20_HERO_202"));
        f.shop.StartTurn(0, 1);
        f.shop.Choose(0, 0);
        PutBoard(f, 0, Card("BGS_115t"));   // 1/2
        f.S(0).power.powerOverride = "BG20_HERO_283p_t1";   // Westfall, as if Galewing's power had been swapped in
        f.S(0).power.powerCountdown = 1;
        f.S(0).power.combatPower = "BG22_HERO_001p_t2";
        f.shop.StartTurn(0, 2);
        CHECK(f.S(0).board[0].attack == 4 && f.S(0).board[0].maxHealth == 4);   // the +2/+2 paid
        CHECK(f.S(0).power.combatPower.empty());
        CHECK(f.S(0).powerChoices.size() == 2);
    }
}

// Sir Finley: a choice of 3 powers at the start of the game, kept; the excluded powers and the lobby filter hold.
static void TestFinley()
{
    Env e(206);
    e.shop.SetLobbyTribes(0, std::vector<uint32_t>{ kTribeBeast });
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_40"));
    CHECK(e.S(0).powerChoices.size() == 3);
    for (std::string const& id : e.S(0).powerChoices)
    {
        CHECK(id != "BG21_HERO_000p" && id != "BG20_HERO_202p" && id != "TB_BaconShop_HP_001");
        HeroPowerDef const* const def = e.shop.FindPower(id);
        CHECK(def && (def->needsLobbyTribe == 0 || (def->needsLobbyTribe & kTribeBeast) != 0));
    }
    std::string const chosen = e.S(0).powerChoices[1];
    CHECK(e.shop.Choose(0, 1) == ActionResult::Ok);
    CHECK(e.S(0).power.powerOverride == chosen);
    // The powers that are dead picks once the game has started stay out of the swap pool.
    {
        std::vector<std::string> const pool = e.shop.SwapPool(0);
        for (char const* dead : { "TB_BaconShop_HP_035", "TB_BaconShop_HP_033", "TB_BaconShop_HP_105",
                 "TB_BaconShop_HP_044", "BG22_HERO_201p", "BG24_HERO_100p", "TB_BaconShop_HP_057" })
            CHECK(std::find(pool.begin(), pool.end(), dead) == pool.end());
    }
    e.shop.StartTurn(0, 1);
    CHECK(e.S(0).power.powerOverride == chosen);             // kept
    e.shop.StartTurn(0, 2);
    CHECK(e.S(0).power.powerOverride == chosen);
    CHECK(e.S(0).powerChoices.empty());
}

// Ragnaros: 25 enemy kills turn DIE, INSECTS! into Sulfuras; +3/+3 to the ends at EndTurn.
static void TestRagnaros()
{
    Env e(207);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_11"));
    e.shop.StartTurn(0, 1);
    CHECK(std::string(e.shop.Power(0)->id) == "TB_BaconShop_HP_087");
    e.shop.CountEnemyKills(0, 24);
    CHECK(std::string(e.shop.Power(0)->id) == "TB_BaconShop_HP_087");
    CHECK(e.S(0).power.enemyKills == 24);
    e.shop.CountEnemyKills(0, 1);
    CHECK(std::string(e.shop.Power(0)->id) == "TB_BaconShop_HP_087t");
    PutBoard(e, 0, Card("BGS_115t"));   // 1/2
    PutBoard(e, 0, Card("BGS_115t"));
    PutBoard(e, 0, Card("BGS_115t"));
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(e.S(0).board[0].attack == 5 && e.S(0).board[0].maxHealth == 5);
    CHECK(e.S(0).board[1].attack == 2 && e.S(0).board[1].maxHealth == 2);
    CHECK(e.S(0).board[2].attack == 5 && e.S(0).board[2].maxHealth == 5);
}

// ------------------------------------------------------------------------------------ shop systems

static int FirstOfTier(int tier, uint32_t tribe = 0)
{
    std::vector<int> const cards = tribe ? CardsOfTribe(tribe, true) : CardsOfTier(tier, true);
    for (int c : cards)
        if (CardAt(c).tier == tier)
            return c;
    return -1;
}

// Captain Hooktusk: remove a friendly minion, choose 1 of 2 from a Tavern Tier lower.
static void TestHooktusk()
{
    {
        Env e(208);
        e.shop.SetHero(0, Hero("TB_BaconShop_HERO_67"));
        e.shop.StartTurn(0, 1);
        e.S(0).gold = 10;
        int const tier2 = FirstOfTier(2);
        CHECK(tier2 >= 0);
        uint32_t const uid = PutBoard(e, 0, tier2);
        CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
        CHECK(e.S(0).board.Empty());                       // removed, its pool copies returned
        CHECK(e.shop.DiscoverOpen(0));
        CHECK(static_cast<int>(e.S(0).options.size()) == 2);
        for (Minion const& m : e.S(0).options)
            CHECK(m.Tier() == 1);
        CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
        CHECK(e.S(0).hand.Size() == 1);
    }
    {
        // A Tier 1 host still offers Tier 1.
        Env e(216);
        e.shop.SetHero(0, Hero("TB_BaconShop_HERO_67"));
        e.shop.StartTurn(0, 1);
        e.S(0).gold = 10;
        uint32_t const host = PutBoard(e, 0, FirstOfTier(1));
        CHECK(e.shop.HeroPower(0, host) == ActionResult::Ok);
        for (Minion const& m : e.S(0).options)
            CHECK(m.Tier() == 1);
    }
}

// Cookie the Cook: three thrown minions, then a Discover from their types at the Tavern Tier.
static void TestCookie()
{
    Env e(209);
    e.shop.SetHero(0, Hero("BG21_HERO_020"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    int const beast = FirstOfTier(1, kTribeBeast);
    CHECK(beast >= 0);
    for (int i = 0; i < 2; ++i)
    {
        uint32_t const uid = PutBoard(e, 0, beast);
        CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
        CHECK(!e.shop.DiscoverOpen(0));
        CHECK(e.S(0).power.potCount == i + 1);
        e.shop.StartTurn(0, i + 2);   // the power is once per turn
        e.S(0).gold = 10;
    }
    uint32_t const uid = PutBoard(e, 0, beast);
    CHECK(e.shop.HeroPower(0, uid) == ActionResult::Ok);
    CHECK(e.S(0).power.potCount == 0 && e.S(0).power.potTypes == 0);
    CHECK(e.shop.DiscoverOpen(0));
    for (Minion const& m : e.S(0).options)
    {
        CHECK(m.Tier() == e.S(0).tier);
        CHECK(m.IsTribe(kTribeBeast));
    }
}

// Maiev Shadowsong: a tavern offer is held dormant; after three StartTurns it comes to the hand with +2/+2.
static void TestMaiev()
{
    Env e(210);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_62"));
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 10;
    int const tier2 = FirstOfTier(2);
    Minion offer = MakeMinion(tier2, false, e.shop.NewUid());
    offer.poolCopies = 1;
    e.pool.Take(tier2);
    e.S(0).offers.insert(e.S(0).offers.begin(), offer);
    int const attack = offer.attack;
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::Ok);
    CHECK(e.S(0).power.holdsOffer);
    CHECK(e.S(0).power.heldTurns == 3);
    CHECK(e.S(0).offers.empty() || e.S(0).offers[0].uid != offer.uid);
    CHECK(e.shop.HeroPower(0, 0, 0) == ActionResult::PowerUsed);   // one held minion at a time
    e.shop.StartTurn(0, 2);
    e.shop.StartTurn(0, 3);
    CHECK(e.S(0).power.holdsOffer);
    e.shop.StartTurn(0, 4);
    CHECK(!e.S(0).power.holdsOffer);
    bool found = false;
    for (Minion const& m : e.S(0).hand.Items())
        if (m.card == tier2)
        {
            found = true;
            CHECK(m.attack == attack + 2 && m.maxHealth == CardAt(tier2).health + 2);
        }
    CHECK(found);
}

// Mr. Bigglesworth: a Discover from a dead warband that keeps its stats; a Consolation Coin for an empty one.
static void TestBigglesworth()
{
    Env e(211);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_70"));
    e.shop.StartTurn(0, 1);
    Board warband;
    Minion m = MakeMinion(Card("BGS_115t"), false, e.shop.NewUid());
    m.attack = 6;
    m.health = m.maxHealth = 9;
    warband.Push(m);
    e.shop.OfferWarbandDiscover(0, warband);
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(e.S(0).options.size() == 1);
    CHECK(e.S(0).options[0].attack == 6 && e.S(0).options[0].maxHealth == 9);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    Board empty;
    e.shop.OfferWarbandDiscover(0, empty);
    CHECK(HandOf(e, 0, "TB_BaconShop_HP_080t") == 1);   // Consolation Coin
}

// The Consolation Coin is a spell: it gives 1 Gold this turn.
static void TestConsolationCoin()
{
    Env e(217, 2, EffectRegistry::Global());
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_70"));
    e.shop.StartTurn(0, 1);
    int const gold = e.S(0).gold;
    CHECK(e.shop.AddSpellToHand(0, Card("TB_BaconShop_HP_080t"), false, false, false) != 0);
    int index = -1;
    for (int i = 0; i < e.S(0).hand.Size(); ++i)
        if (e.S(0).hand[i].card == Card("TB_BaconShop_HP_080t"))
            index = i;
    CHECK(index >= 0);
    CHECK(e.shop.Cast(0, index) == ActionResult::Ok);
    CHECK(e.S(0).gold == gold + 1);
}

// Silas Darkmoon: one ticket per restock; three ticketed buys Discover a minion of the Tavern Tier.
static void TestSilas()
{
    Env e(212);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_90"));
    e.shop.StartTurn(0, 1);
    int ticketed = 0;
    for (Minion const& o : e.S(0).offers)
        if ((o.flags & kMinionTicket) != 0)
            ++ticketed;
    CHECK(ticketed == 1);
    for (int round = 0; round < 3; ++round)
    {
        e.S(0).gold = 10;
        int const index = [&e]()
        {
            for (int i = 0; i < static_cast<int>(e.S(0).offers.size()); ++i)
                if ((e.S(0).offers[static_cast<std::size_t>(i)].flags & kMinionTicket) != 0)
                    return i;
            return -1;
        }();
        CHECK(index >= 0);
        CHECK(e.shop.Buy(0, index) == ActionResult::Ok);
        if (round < 2)
        {
            CHECK(e.shop.Refresh(0) == ActionResult::Ok);
            e.S(0).gold = 10;
        }
    }
    CHECK(e.S(0).power.tickets == 0);
    CHECK(e.shop.DiscoverOpen(0));
    for (Minion const& o : e.S(0).options)
        CHECK(o.Tier() == e.S(0).tier);
}

// Silas Darkmoon: a Frozen offer keeps its ticket through the next restock, and there is still only one ticket.
static void TestSilasFrozenTicket()
{
    Env e(213);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_90"));
    e.shop.StartTurn(0, 1);
    uint32_t ticketUid = 0;
    for (Minion const& o : e.S(0).offers)
        if ((o.flags & kMinionTicket) != 0)
            ticketUid = o.uid;
    CHECK(ticketUid != 0);
    CHECK(e.shop.Freeze(0, true) == ActionResult::Ok);
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    e.shop.StartTurn(0, 2);
    int ticketed = 0;
    bool kept = false;
    for (Minion const& o : e.S(0).offers)
        if ((o.flags & kMinionTicket) != 0)
        {
            ++ticketed;
            kept = o.uid == ticketUid;
        }
    CHECK(ticketed == 1 && kept);
    CHECK(e.shop.CheckConservation());
}

// Sire Denathrius: two quest slots, both counting the same events, each with its own reward.
static void TestDenathrius()
{
    Env e(213);
    e.shop.SetHero(0, Hero("BG24_HERO_100"));
    CHECK(e.shop.Power(0) && e.shop.Power(0)->startQuestOffer);
    e.shop.StartTurn(0, 1);
    CHECK(e.shop.FreeQuestSlot(0) == 0);
    int const quest = FindQuest("BG24_Quest_313");   // Invite the Guests: buy minions
    int const reward = FindReward("BG24_Reward_128");
    CHECK(quest >= 0 && reward >= 0);
    e.S(0).quest.quest = quest;
    e.S(0).quest.reward = reward;
    e.S(0).quest.required = 3;
    CHECK(e.shop.FreeQuestSlot(0) == 1);
    e.S(0).quest2.quest = quest;
    e.S(0).quest2.reward = reward;
    e.S(0).quest2.required = 3;
    CHECK(e.shop.FreeQuestSlot(0) == -1);
    e.S(0).gold = 30;
    for (int i = 0; i < 3; ++i)
    {
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
        e.S(0).gold = 10;
    }
    CHECK(e.S(0).quest.count == 3 && e.S(0).quest2.count == 3);
    CHECK(e.S(0).quest.done && e.S(0).quest2.done);
    CHECK(e.S(0).rewardState.active && e.S(0).rewardState2.active);
    CHECK(e.S(0).rewardState.id == reward && e.S(0).rewardState2.id == reward);
    // An offer in the second slot is found by OfferQuestSlot.
    e.S(0).quest2.done = false;
    e.S(0).quest2.quest = -1;
    e.S(0).quest2.offer.push_back(std::make_pair(quest, reward));
    CHECK(e.shop.OfferQuestSlot(0) == 1);
}

// A. F. Kay: the first two turns pass; turn 3 opens two Discovers of Tier 3.
static void TestAFKay()
{
    Env e(214);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_16"));
    e.shop.StartTurn(0, 1);
    CHECK(e.S(0).skipTurn);
    CHECK(e.S(0).gold == GoldForTurn(1));   // the gold curve advanced
    e.shop.StartTurn(0, 2);
    CHECK(e.S(0).skipTurn);
    e.shop.StartTurn(0, 3);
    CHECK(!e.S(0).skipTurn);
    CHECK(e.shop.DiscoverOpen(0));
    for (int i = 0; i < 2; ++i)
    {
        for (Minion const& m : e.S(0).options)
            CHECK(m.Tier() == 3);
        CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    }
    CHECK(e.S(0).hand.Size() == 2);
}

// Ambassador Faelin: turn 1 passes; a Tier 2, 4 and 6 minion are held until their Tier.
static void TestFaelin()
{
    Env e(215);
    e.shop.SetHero(0, Hero("BG22_HERO_201"));
    e.shop.StartTurn(0, 1);
    CHECK(e.S(0).skipTurn);
    e.shop.StartTurn(0, 2);
    CHECK(!e.S(0).skipTurn);
    for (int tier = 2; tier <= 6; tier += 2)
    {
        CHECK(e.shop.DiscoverOpen(0));
        for (Minion const& m : e.S(0).options)
            CHECK(m.Tier() == tier);
        CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    }
    CHECK(e.S(0).power.expedition.size() == 3);
    CHECK(e.S(0).hand.Empty());
    e.S(0).gold = 100;
    while (e.S(0).tier < 2)
        CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
    CHECK(e.S(0).power.expedition.size() == 2);
    CHECK(e.S(0).hand.Size() == 1);
    while (e.S(0).tier < 6)
        CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
    CHECK(e.S(0).power.expedition.empty());
    CHECK(e.S(0).hand.Size() == 3);
}

// Sire Denathrius through a real game: the start-of-game offer is two pairs and the quest pick blocks the shop.
static void TestDenathriusGame()
{
    int const target = FindHero("BG24_HERO_100");
    CHECK(target >= 0);
    bool found = false;
    for (uint32_t seed = 1; seed <= 200 && !found; ++seed)
    {
        GameSetup setup;
        setup.seed = seed;
        setup.heroPick = true;
        setup.kinds[0] = PlayerKind::External;
        Game g(setup);
        std::vector<int> const& choices = g.HeroChoices(0);
        int pick = -1;
        for (int i = 0; i < static_cast<int>(choices.size()); ++i)
            if (choices[static_cast<std::size_t>(i)] == target)
                pick = i;
        if (pick < 0)
            continue;
        CHECK(g.ChooseHero(0, pick) == ActionResult::Ok);
        CHECK(g.QuestPicking());
        CHECK(g.QuestOffer(0).size() == 2);
        CHECK(g.ChooseQuest(0, 0) == ActionResult::Ok);
        CHECK(g.PlayerQuest(0) >= 0);
        CHECK(g.Round() == 1);
        found = true;
    }
    CHECK(found);
}

// The Great Akazamzarak: Prestidigitation Discovers a Secret and puts it into play (not the hand); the cap is 5 and a
// Secret already in play is not offered again.
static void TestAkazamzarak()
{
    CHECK(SecretCardIds().size() == 10);
    Env e(217);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_21"));
    e.shop.StartTurn(0, 1);
    CHECK(std::string(e.shop.Power(0)->id) == "TB_BaconShop_HP_020");
    CHECK(e.shop.Power(0)->cost == 1);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(static_cast<int>(e.S(0).options.size()) == 3);
    for (Minion const& m : e.S(0).options)
        CHECK(CardAt(m.card).role == CardRole::Spell);
    int const first = e.S(0).options[0].card;
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Empty());                          // into play, not the hand
    CHECK(e.S(0).secrets.size() == 1 && e.S(0).secrets[0] == first);
    CHECK(e.shop.HeroPower(0) == ActionResult::PowerUsed);   // once per turn
    e.shop.StartTurn(0, 2);
    e.S(0).gold = 10;
    CHECK(e.shop.HeroPower(0) == ActionResult::Ok);
    for (Minion const& m : e.S(0).options)
        CHECK(m.card != first);                          // not one already in play
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).secrets.size() == 2);
    // The cap is 5 (owner point 9): with five in play the power is refused before the gold is spent.
    e.S(0).secrets.clear();
    for (int c : SecretCardIds())
        if (static_cast<int>(e.S(0).secrets.size()) < 5)
            e.S(0).secrets.push_back(c);
    e.shop.StartTurn(0, 3);
    e.S(0).gold = 10;
    int const gold = e.S(0).gold;
    CHECK(e.shop.HeroPower(0) == ActionResult::BadTarget);
    CHECK(e.S(0).gold == gold);
}

// The two Secrets outside combat: Competitive Spirit at StartTurn and Ice Block on the fatal hero damage.
static void TestSecretTurnAndIceBlock()
{
    Env e(218);
    e.shop.StartTurn(0, 1);
    int const spirit = FindCard("TB_Bacon_Secrets_13");
    int const ice = FindCard("TB_Bacon_Secrets_12");
    CHECK(spirit >= 0 && ice >= 0);
    uint32_t const uid = PutBoard(e, 0, FirstOfTier(1));
    int const atk = e.S(0).board[0].attack;
    int const hp = e.S(0).board[0].health;
    e.S(0).secrets.push_back(spirit);
    e.shop.StartTurn(0, 2);
    CHECK(e.S(0).board.IndexOfUid(uid) >= 0);
    CHECK(e.S(0).board[0].attack == atk + 1 && e.S(0).board[0].maxHealth == hp + 1);
    CHECK(e.S(0).secrets.empty());                       // it triggered and left play
    e.S(0).health = 5;
    e.S(0).armor = 0;
    e.S(0).secrets.push_back(ice);
    CHECK(!e.shop.PreventFatalDamage(0, 4));             // not fatal: Ice Block stays
    CHECK(e.S(0).secrets.size() == 1);
    CHECK(e.shop.PreventFatalDamage(0, 5));
    CHECK(e.S(0).secrets.empty());
    CHECK(e.S(0).health == 5);
}

// ------------------------------------------------------------------------------------ Tickatus and prizes

// A card of the tier with no effect entry, so a test sees only the prize under test.
static int VanillaTier(int tier)
{
    for (int c : CardsOfTier(tier, true))
        if (!EffectRegistry::Global().Find(c))
            return c;
    return -1;
}

// A vanilla card of the tier with a golden twin (Raise the Stakes needs one).
static int GoldenVanillaTier(int tier)
{
    for (int c : CardsOfTier(tier, true))
        if (!EffectRegistry::Global().Find(c) && CardAt(c).goldenId[0] != '\0')
            return c;
    return -1;
}

// Casts a prize from the hand; the target is a friendly board minion uid (0 = none).
static bool CastPrize(Env& e, char const* id, uint32_t target = 0)
{
    uint32_t const uid = PutHand(e, 0, Card(id));
    int const idx = e.S(0).hand.IndexOfUid(uid);
    return idx >= 0 && e.shop.Cast(0, idx, target) == ActionResult::Ok;
}

// `n` different cards of the tier (a triple merge would change a test that puts copies of one card on the board).
static std::vector<int> DistinctTier(int tier, int n)
{
    std::vector<int> out;
    for (int c : CardsOfTier(tier, true))
        if (static_cast<int>(out.size()) < n)
            out.push_back(c);
    return out;
}

// Makes sure the lobby pool has a card of the tier (the random 5 tribes may not cover it).
static void EnsureTier(Env& e, int tier)
{
    if (e.shop.Pool().CountCards(e.S(0).pools, tier, tier, 0) == 0)
        e.pool.AddCards(0, { FirstOfTier(tier) });
}

// Prize Wall: a prize Discover at the start of turns 4, 8, 12 and 16, then it stops.
static void TestTickatus()
{
    Env e(219);
    e.shop.SetHero(0, Hero("TB_BaconShop_HERO_94"));
    CHECK(std::string(e.shop.Power(0)->id) == "TB_BaconShop_HP_106");
    CHECK(e.shop.Power(0)->prizesEvery == 4 && e.shop.Power(0)->activate == nullptr);
    e.shop.StartTurn(0, 3);
    CHECK(!e.shop.DiscoverOpen(0));                         // turn 3: no prize yet
    e.shop.StartTurn(0, 4);
    CHECK(e.shop.DiscoverOpen(0));                          // prize turn 1
    CHECK(static_cast<int>(e.S(0).options.size()) == 3);
    for (Minion const& m : e.S(0).options)
        CHECK(DarkmoonPrizeTurnOf(m.card) == 1);
    int const first = e.S(0).options[0].card;
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == 1 && e.S(0).hand[0].card == first);   // into the hand as a spell
    CHECK(CardAt(first).role == CardRole::Spell);
    CHECK(e.S(0).power.prizesTaken == 1);
    e.shop.StartTurn(0, 8);
    CHECK(e.shop.DiscoverOpen(0));                          // prize turn 2
    for (Minion const& m : e.S(0).options)
        CHECK(DarkmoonPrizeTurnOf(m.card) == 2);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    e.shop.StartTurn(0, 12);
    CHECK(e.shop.DiscoverOpen(0));                          // prize turn 3
    for (Minion const& m : e.S(0).options)
        CHECK(DarkmoonPrizeTurnOf(m.card) == 3);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    e.shop.StartTurn(0, 16);
    CHECK(e.shop.DiscoverOpen(0));                          // prize turn 4, the last one
    for (Minion const& m : e.S(0).options)
        CHECK(DarkmoonPrizeTurnOf(m.card) == 4);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).power.prizesTaken == 4);
    e.shop.StartTurn(0, 20);
    CHECK(!e.shop.DiscoverOpen(0));                         // stopped after the fourth
    CHECK(DarkmoonPrizeTurnForRound(4) == 1 && DarkmoonPrizeTurnForRound(7) == 1);
    CHECK(DarkmoonPrizeTurnForRound(8) == 2 && DarkmoonPrizeTurnForRound(16) == 4 && DarkmoonPrizeTurnForRound(20) == 4);
    CHECK(DarkmoonPrizeTurnForRound(3) == 0);
    CHECK(DarkmoonPrizeTurnsLeft(1, 0) == 3 && DarkmoonPrizeTurnsLeft(4, 1) == 4 && DarkmoonPrizeTurnsLeft(16, 4) == 0);
    CHECK(DarkmoonPrizeIds(1).size() == 7 && DarkmoonPrizeIds(2).size() == 9 && DarkmoonPrizeIds(3).size() == 7);
    CHECK(DarkmoonPrizeIds(4).size() == 6 && DarkmoonPrizeIds(-1).size() == 29);
    CHECK(DarkmoonPrizeTurnOf(FindCard("BGS_Treasures_000")) == 0);   // Big Banana is not a prize
}

static void TestPrizePocketChange()
{
    Env e(300, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const gold = e.S(0).gold;
    CHECK(CastPrize(e, "BGS_Treasures_001"));
    CHECK(HandOf(e, 0, "TB_BaconShop_HP_008a") == 2);
    CHECK(e.S(0).gold == gold);
}

static void TestPrizeGachaGift()
{
    Env e(301, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_004"));
    CHECK(e.shop.DiscoverOpen(0) && static_cast<int>(e.S(0).options.size()) == 3);
    for (Minion const& m : e.S(0).options)
        CHECK(m.Tier() == 1);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == 1);
}

static void TestPrizeMightOfStormwind()
{
    Env e(302, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    std::vector<int> const cards = DistinctTier(1, 4);
    CHECK(static_cast<int>(cards.size()) == 4);
    std::vector<int> base;
    for (int card : cards)
    {
        PutBoard(e, 0, card);
        base.push_back(CardAt(card).attack);
    }
    CHECK(CastPrize(e, "BGS_Treasures_007"));
    int buffed = 0;
    for (int i = 0; i < e.S(0).board.Size(); ++i)
        if (e.S(0).board[i].attack == base[static_cast<std::size_t>(i)] + 1)
            ++buffed;
    CHECK(buffed == 3);
}

static void TestPrizeTheGoodStuff()
{
    Env e(303, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(!e.S(0).offers.empty());
    int const before = e.S(0).offers[0].attack;
    CHECK(CastPrize(e, "BGS_Treasures_013"));
    CHECK(e.S(0).offers[0].attack == before + 1);
    for (Minion const& o : e.S(0).offers)
        CHECK(o.auraAttack >= 1);
}

static void TestPrizeRockingAndRolling()
{
    Env e(304, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_029"));
    e.S(0).gold = 20;
    for (int i = 0; i < 3; ++i)
    {
        CHECK(e.shop.RefreshCost(0) == 0);
        CHECK(e.shop.Refresh(0) == ActionResult::Ok);
        CHECK(e.S(0).gold == 20);
    }
    CHECK(e.shop.RefreshCost(0) == 1);   // the three free ones are gone (a Refresh costs 1 in 24.2)
}

static void TestPrizeNewRecruit()
{
    Env e(305, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const before = static_cast<int>(e.S(0).offers.size());
    CHECK(CastPrize(e, "BGS_Treasures_033"));
    CHECK(static_cast<int>(e.S(0).offers.size()) == before + 1);
    CHECK(e.S(0).extraOffers == 1);
    CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    CHECK(static_cast<int>(e.S(0).offers.size()) == before + 1);   // the extra slot stays
}

static void TestPrizeBananaBunch()
{
    Env e(306, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_040"));
    CHECK(HandOf(e, 0, "TB_BaconShop_HP_038t") == 2);
}

static void TestPrizeEvolvingTavern()
{
    Env e(307, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    e.S(0).tier = 2;
    EnsureTier(e, 3);
    int const before = static_cast<int>(e.S(0).offers.size());
    CHECK(CastPrize(e, "BGS_Treasures_006"));
    CHECK(!e.S(0).offers.empty() && static_cast<int>(e.S(0).offers.size()) <= before);
    for (Minion const& o : e.S(0).offers)
        CHECK(o.Tier() == 3);
    CHECK(e.shop.CheckConservation());
}

static void TestPrizeGruulRules()
{
    Env e(308, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const card = VanillaTier(2);
    uint32_t const uid = PutBoard(e, 0, card);
    CHECK(CastPrize(e, "BGS_Treasures_009", uid));
    Minion* m = FindMinion(e, 0, uid);
    CHECK(m && m->gruulRules);
    int const attack = m->attack;
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    m = FindMinion(e, 0, uid);
    CHECK(m && m->attack == attack + 2);
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    m = FindMinion(e, 0, uid);
    CHECK(m && m->attack == attack + 4);
}

static void TestPrizeTimeThief()
{
    Env e(309, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    Board last;
    last.Push(MakeMinion(FirstOfTier(1), false, e.shop.NewUid()));
    e.shop.SetLastOpponent(0, last);
    CHECK(CastPrize(e, "BGS_Treasures_010"));
    CHECK(e.shop.DiscoverOpen(0) && static_cast<int>(e.S(0).options.size()) == 1);
    CHECK(e.S(0).options[0].card == FirstOfTier(1));
}

static void TestPrizeOnTheHouse()
{
    Env e(310, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    e.S(0).tier = 4;
    EnsureTier(e, 4);
    CHECK(CastPrize(e, "BGS_Treasures_012"));
    CHECK(e.shop.DiscoverOpen(0));
    for (Minion const& m : e.S(0).options)
        CHECK(m.Tier() == 4);
}

static void TestPrizeUnlimitedCoin()
{
    Env e(311, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const gold = e.S(0).gold;
    CHECK(CastPrize(e, "BGS_Treasures_014"));
    CHECK(e.S(0).gold == gold + 1);
    CHECK(e.S(0).hand.Empty());
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(HandOf(e, 0, "BGS_Treasures_014") == 1);   // back at the end of the turn
}

static void TestPrizeRatInACage()
{
    Env e(312, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    uint32_t const uid = PutBoard(e, 0, VanillaTier(2));
    FindMinion(e, 0, uid)->attack = 3;
    CHECK(CastPrize(e, "BGS_Treasures_018", uid));
    CHECK(FindMinion(e, 0, uid)->attack == 10);   // (3 + 2) doubled
}

static void TestPrizeTheBouncer()
{
    Env e(313, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    uint32_t const uid = PutBoard(e, 0, VanillaTier(2));
    int const atk = FindMinion(e, 0, uid)->attack;
    int const hp = FindMinion(e, 0, uid)->maxHealth;
    CHECK(CastPrize(e, "BGS_Treasures_026", uid));
    Minion* m = FindMinion(e, 0, uid);
    CHECK(m->attack == atk + 6 && m->maxHealth == hp + 6 && m->Has(kKwTaunt));
}

static void TestPrizeBrannsBlessing()
{
    Env e(314, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_030"));
    CHECK(e.shop.BattlecryTimes(0) == 2);
    e.shop.StartTurn(0, 2);
    CHECK(e.shop.BattlecryTimes(0) == 1);   // this turn only
}

static void TestPrizeGreatDeal()
{
    Env e(315, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const before = e.S(0).upgradeCost;
    CHECK(CastPrize(e, "BGS_Treasures_036"));
    CHECK(e.S(0).upgradeCost == std::max(0, before - 5));
}

static void TestPrizeTrainingSession()
{
    Env e(316, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_011"));
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(static_cast<int>(e.S(0).powerChoices.size()) == 3);
}

static void TestPrizeBuyTheHolyLight()
{
    Env e(317, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    uint32_t const uid = PutBoard(e, 0, VanillaTier(2));
    CHECK(CastPrize(e, "BGS_Treasures_015", uid));
    CHECK(FindMinion(e, 0, uid)->Has(kKwDivineShield));
}

static void TestPrizeBananarama()
{
    Env e(318, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    std::vector<int> const cards = DistinctTier(1, 7);
    CHECK(static_cast<int>(cards.size()) == 7);
    for (int card : cards)
        PutHand(e, 0, card);
    CHECK(CastPrize(e, "BGS_Treasures_019"));
    CHECK(e.S(0).hand.Size() == kHandMax);
    CHECK(HandOf(e, 0, "TB_BaconShop_HP_038t") == 3);   // the other slots filled
}

static void TestPrizeTopShelf()
{
    Env e(319, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    EnsureTier(e, 6);
    CHECK(CastPrize(e, "BGS_Treasures_020"));
    CHECK(e.shop.DiscoverOpen(0));
    for (Minion const& m : e.S(0).options)
        CHECK(m.Tier() == 6);
}

static void TestPrizeRepeatCustomer()
{
    Env e(320, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const card = VanillaTier(2);
    uint32_t const uid = PutBoard(e, 0, card);
    CHECK(CastPrize(e, "BGS_Treasures_034", uid));
    CHECK(e.S(0).board.Empty() && e.S(0).hand.Size() == 1);
    Minion const& m = e.S(0).hand[0];
    CHECK(m.card == card && m.attack == CardAt(card).attack + 2 && m.maxHealth == CardAt(card).health + 2);
    CHECK(e.shop.CheckConservation());
}

static void TestPrizeAllThatGlitters()
{
    Env e(321, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_037"));
    int golden = 0;
    for (Minion const& o : e.S(0).offers)
        if (o.golden)
            ++golden;
    CHECK(golden == 1);
    CHECK(e.shop.CheckConservation());
}

static void TestPrizeMindflayerGoggles()
{
    Env e(322, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const offers = static_cast<int>(e.S(0).offers.size());
    CHECK(CastPrize(e, "BGS_Treasures_039"));
    CHECK(e.S(0).hand.Size() >= offers);
    CHECK(!e.S(0).offers.empty());   // restocked
    CHECK(e.shop.CheckConservation());
}

static void TestPrizeRaiseTheStakes()
{
    Env e(323, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    int const card = GoldenVanillaTier(1);
    CHECK(card >= 0);
    uint32_t const uid = PutBoard(e, 0, card);
    CHECK(CastPrize(e, "BGS_Treasures_016", uid));
    CHECK(e.S(0).board.Empty() && e.S(0).hand.Size() == 1);
    CHECK(e.S(0).hand[0].card == card && e.S(0).hand[0].golden);
}

static void TestPrizeFriendsAndFamily()
{
    Env e(324, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_022"));
    CHECK(e.shop.BuyCost(0) == 2);
    e.S(0).gold = 10;
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).gold == 8);
}

static void TestPrizeOpenBar()
{
    Env e(325, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    CHECK(CastPrize(e, "BGS_Treasures_023"));
    e.S(0).gold = 20;
    for (int i = 0; i < 5; ++i)
    {
        CHECK(e.shop.RefreshCost(0) == 0);
        CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    }
    CHECK(e.S(0).gold == 20);
    CHECK(e.shop.RefreshCost(0) == 1);   // the fifth free one was used
    e.shop.StartTurn(0, 2);   // five more next turn
    CHECK(e.shop.RefreshCost(0) == 0);
}

static void TestPrizeFreshTab()
{
    Env e(326, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    e.S(0).gold = 0;
    CHECK(CastPrize(e, "BGS_Treasures_025"));
    CHECK(e.S(0).gold == 10);
}

static void TestPrizeGiveADogABone()
{
    Env e(327, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 1);
    uint32_t const uid = PutBoard(e, 0, VanillaTier(2));
    int const atk = FindMinion(e, 0, uid)->attack;
    CHECK(CastPrize(e, "BGS_Treasures_028", uid));
    Minion* m = FindMinion(e, 0, uid);
    CHECK(m->attack == atk + 10 && m->Has(kKwDivineShield) && m->Has(kKwWindfury));
}

static void TestPrizeBigWinner()
{
    Env e(328, 2, EffectRegistry::Global());
    e.shop.StartTurn(0, 16);   // the fourth prize turn: the three earlier ones are offered
    CHECK(CastPrize(e, "BGS_Treasures_032"));
    for (int turn = 1; turn <= 3; ++turn)
    {
        CHECK(e.shop.DiscoverOpen(0));
        CHECK(!e.S(0).options.empty());
        for (Minion const& m : e.S(0).options)
            CHECK(DarkmoonPrizeTurnOf(m.card) == turn);
        CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    }
    CHECK(!e.shop.DiscoverOpen(0));
    CHECK(e.S(0).hand.Size() == 3);
}

int main(int, char**)
{
    RUN(TestHeroTable);
    RUN(TestRegistryComplete);
    RUN(TestArmorTiers);
    RUN(TestHeroPick);
    RUN(TestHeroPickNeedsTribe);
    RUN(TestHumanPickStartsRound);
    RUN(TestPatchwerk);
    RUN(TestNozdormu);
    RUN(TestOmu);
    RUN(TestGallywix);
    RUN(TestMillhouse);
    RUN(TestMillificent);
    RUN(TestDeryl);
    RUN(TestKaelthas);
    RUN(TestKaelthasTriple);
    RUN(TestAranna);
    RUN(TestBazhial);
    RUN(TestKragg);
    RUN(TestToki);
    RUN(TestElise);
    RUN(TestGeorge);
    RUN(TestJaraxxus);
    RUN(TestEdwin);
    RUN(TestRakanishu);
    RUN(TestReno);
    RUN(TestArmorAbsorbs);
    RUN(TestNoPower);
    RUN(TestChenvaala);
    RUN(TestCurator);
    RUN(TestLichKing);
    RUN(TestPyramad);
    RUN(TestDeathwing);
    RUN(TestAlAkir);
    RUN(TestGreybough);
    RUN(TestRokara);
    RUN(TestAvengeHeroes);
    RUN(TestIllidan);
    RUN(TestPatchesPirate);
    RUN(TestEudora);
    RUN(TestTogwaggle);
    RUN(TestGuff);
    RUN(TestWagtoggle);
    RUN(TestCariel);
    RUN(TestSaurfang);
    RUN(TestRatKing);
    RUN(TestKurtrus);
    RUN(TestYsera);
    RUN(TestAlexstrasza);
    RUN(TestCthun);
    RUN(TestVashj);
    RUN(TestAzshara);
    RUN(TestMukla);
    RUN(TestBarov);
    RUN(TestHolmes);
    RUN(TestZephrys);
    RUN(TestMalygos);
    RUN(TestGalakrond);
    RUN(TestJandice);
    RUN(TestVoljin);
    RUN(TestXyrella);
    RUN(TestYogg);
    RUN(TestBlackthorn);
    RUN(TestVarden);
    RUN(TestSindragosa);
    RUN(TestSindragosaCarry);
    RUN(TestFlurgl);
    RUN(TestStormcoil);
    RUN(TestDinotamerBrann);
    RUN(TestMutanus);
    RUN(TestMutanusTempStats);
    RUN(TestBloodGemStatsReset);
    RUN(TestTamsin);
    RUN(TestTess);
    RUN(TestTessPlainCopies);
    RUN(TestScabbs);
    RUN(TestScabbsPlainCopy);
    RUN(TestShudderwock);
    RUN(TestSneed);
    RUN(TestNzoth);
    RUN(TestOzumat);
    RUN(TestYshaarj);
    RUN(TestRafaam);
    RUN(TestGalewing);
    RUN(TestBrukan);
    RUN(TestTavish);
    RUN(TestNguyen);
    RUN(TestFinley);
    RUN(TestRagnaros);
    RUN(TestHooktusk);
    RUN(TestCookie);
    RUN(TestMaiev);
    RUN(TestBigglesworth);
    RUN(TestConsolationCoin);
    RUN(TestSilas);
    RUN(TestSilasFrozenTicket);
    RUN(TestDenathrius);
    RUN(TestAFKay);
    RUN(TestFaelin);
    RUN(TestAkazamzarak);
    RUN(TestSecretTurnAndIceBlock);
    RUN(TestDenathriusGame);
    RUN(TestTickatus);
    RUN(TestPrizePocketChange);
    RUN(TestPrizeGachaGift);
    RUN(TestPrizeMightOfStormwind);
    RUN(TestPrizeTheGoodStuff);
    RUN(TestPrizeRockingAndRolling);
    RUN(TestPrizeNewRecruit);
    RUN(TestPrizeBananaBunch);
    RUN(TestPrizeEvolvingTavern);
    RUN(TestPrizeGruulRules);
    RUN(TestPrizeTimeThief);
    RUN(TestPrizeOnTheHouse);
    RUN(TestPrizeUnlimitedCoin);
    RUN(TestPrizeRatInACage);
    RUN(TestPrizeTheBouncer);
    RUN(TestPrizeBrannsBlessing);
    RUN(TestPrizeGreatDeal);
    RUN(TestPrizeTrainingSession);
    RUN(TestPrizeBuyTheHolyLight);
    RUN(TestPrizeBananarama);
    RUN(TestPrizeTopShelf);
    RUN(TestPrizeRepeatCustomer);
    RUN(TestPrizeAllThatGlitters);
    RUN(TestPrizeMindflayerGoggles);
    RUN(TestPrizeRaiseTheStakes);
    RUN(TestPrizeFriendsAndFamily);
    RUN(TestPrizeOpenBar);
    RUN(TestPrizeFreshTab);
    RUN(TestPrizeGiveADogABone);
    RUN(TestPrizeBigWinner);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
