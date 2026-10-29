// The reward registry (HsBgRewards) and one test per reward (the shop-side ones through Shop, the
// four combat ones through SimulateCombat with a RewardState), plus the Wheel.
//   bg_rewards_tests.exe [directory]     run all tests; exit code 0 = all checks passed.

#include "../src/battlegrounds/HsBgCombat.h"
#include "../src/battlegrounds/HsBgEffects.h"
#include "../src/battlegrounds/HsBgGame.h"
#include "../src/battlegrounds/HsBgPool.h"
#include "../src/battlegrounds/HsBgRewards.h"
#include "../src/battlegrounds/HsBgShop.h"
#include "../src/battlegrounds/HsBgTypes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
    static EffectRegistry const reg;
    return reg;
}

struct Env
{
    Rng rng;
    PoolSet pool;
    Shop shop;
    explicit Env(uint32_t seed = 1, int players = 2, EffectRegistry const* fx = nullptr)
        : rng(seed), shop(pool, fx ? *fx : NoEffects(), rng, players)
    {
        pool.BuildLobby(rng);
    }
    PlayerShop& S(int p) { return shop.State(p); }
};

static int CardOfTier(int tier)
{
    for (int i = 0; i < CardCount(); ++i)
        if (CardAt(i).role == CardRole::Pool && CardAt(i).tier == tier)
            return i;
    return -1;
}

static Minion NewMinion(Env& e, int card, bool golden = false)
{
    e.pool.Take(card);
    Minion m = MakeMinion(card, golden, e.shop.NewUid());
    m.poolCopies = 1;
    return m;
}

static uint32_t PutBoard(Env& e, int card, bool golden = false)
{
    Minion m = NewMinion(e, card, golden);
    e.S(0).board.Push(m);
    return m.uid;
}

static uint32_t PutHand(Env& e, int card, bool golden = false)
{
    Minion m = NewMinion(e, card, golden);
    e.S(0).hand.Push(m);
    return m.uid;
}

static int PutOffer(Env& e, int card)
{
    int const pool = e.pool.HomeIn(card, e.S(0).pools);
    (void)pool;
    Minion m = NewMinion(e, card);
    e.S(0).offers.push_back(m);
    return static_cast<int>(e.S(0).offers.size()) - 1;
}

static RewardState& Activate(Env& e, char const* id)
{
    int const r = FindReward(id);
    CHECK(r >= 0);
    e.S(0).quest.reward = r;
    e.S(0).quest.done = true;
    e.shop.CheckReward(0);
    return e.S(0).rewardState;
}

// ------------------------------------------------------------------------------------ registry

static void TestRegistry()
{
    RewardRegistry& reg = const_cast<RewardRegistry&>(RewardRegistry::Global());
    CHECK(reg.Errors().empty());
    CHECK(reg.Size() == 23);
    int inPool = 0;
    for (int i = 0; i < RewardCount(); ++i)
        if (RewardAt(i).inPool)
        {
            ++inPool;
            CHECK(reg.Find(RewardAt(i).id) != nullptr);
        }
    CHECK(inPool == 23);
    CHECK(WheelOutcomeCount() == 6);
    CHECK(std::string(WheelOutcomeName(0)) == "Mysterybox");
    CHECK(std::string(WheelOutcomeName(5)) == "Rod of Roasting");
    // passives match the plan
    CHECK(RewardRegistry::Global().Find("BG24_Reward_125")->boardAttack == 5);
    CHECK(RewardRegistry::Global().Find("BG24_Reward_350")->twoForGolden);
    CHECK(RewardRegistry::Global().Find("BG24_Reward_130")->endOfTurnTwice);
    RewardState off;   // not active
    CHECK(RewardFor(off) == nullptr);
}

// ------------------------------------------------------------------------------------ the shop rewards

static void TestSmokingGun()
{
    Env e;
    Activate(e, "BG24_Reward_125");
    int const card = CardOfTier(1);
    int const base = CardAt(card).attack;
    uint32_t const uid = PutBoard(e, card);
    e.shop.Settle(0);
    int const i = e.S(0).board.IndexOfUid(uid);
    CHECK(e.S(0).board[i].attack == base + 5);   // +5 Attack aura
    CHECK(e.S(0).board[i].auraAttack == 5);
}

static void TestTealTiger()
{
    Env e;
    Activate(e, "BG24_Reward_308");
    e.shop.StartTurn(0, 3);
    e.shop.Refresh(0);
    e.shop.Refresh(0);
    CHECK(!e.S(0).offers.empty());
    CHECK(e.S(0).offers[0].auraAttack >= 2);   // +1/+1 per refresh this turn
    // a bought minion keeps the +2/+2 (Legion Overseer's bonus would go)
    Minion const offer = e.S(0).offers[0];
    CardDef const& d = CardAt(offer.card);
    e.S(0).gold = 10;
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    Minion const& bought = e.S(0).hand[e.S(0).hand.Size() - 1];
    CHECK(bought.card == offer.card && bought.auraAttack == 0 && bought.auraHealth == 0);
    CHECK(bought.attack == d.attack + 2 && bought.maxHealth == d.health + 2 && bought.health == d.health + 2);
    CHECK(e.shop.CheckConservation());
}

static void TestAlterEgo()
{
    Env e;
    Activate(e, "BG24_Reward_321");
    int const evenCard = CardOfTier(2);
    CHECK(CardAt(evenCard).tier % 2 == 0);
    PutOffer(e, evenCard);
    e.shop.Settle(0);
    CHECK(e.S(0).offers[0].auraAttack == 6);   // even tier +6/+6 on the Even turn
    CHECK(e.S(0).offers[0].auraHealth == 6);
}

static void TestPilferedLamps()
{
    Env e;
    Activate(e, "BG24_Reward_350");
    int const card = CardOfTier(1);
    PutHand(e, card);
    PutHand(e, card);
    e.shop.Settle(0);   // two copies make a Golden
    CHECK(e.S(0).hand.Size() == 1);
    CHECK(e.S(0).hand[0].golden);
}

static void TestGhastlyMask()
{
    Env e;
    Activate(e, "BG24_Reward_130");
    bool found = false;
    for (Minion const& m : e.S(0).hand.Items())
        if (m.card >= 0 && std::string(CardAt(m.card).id) == "BGS_009")
            found = true;
    CHECK(found);   // Lightfang Enforcer added on gain
}

static void TestCookedBook()
{
    Env e;
    Activate(e, "BG24_Reward_306");
    e.shop.StartTurn(0, 3);
    e.S(0).offers.clear();   // only the minion we buy
    int const card = CardOfTier(1);
    int const base = CardAt(card).attack;
    Minion m = NewMinion(e, card);
    e.S(0).offers.push_back(m);
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    Minion const* bought = e.shop.FindMinion(0, m.uid);
    CHECK(bought && bought->attack == base + 1);   // the first buy is +1/+1
    CHECK(e.S(0).rewardState.cookedBook == 2);
}

static void TestAnimaBribe()
{
    Env e;
    Activate(e, "BG24_Reward_305");
    e.shop.StartTurn(0, 3);
    e.S(0).offers.clear();   // only the offer we watch
    uint32_t const uid = PutBoard(e, CardOfTier(1));
    int const offered = PutOffer(e, CardOfTier(1));
    int const offerAttack = e.S(0).offers[offered].attack;
    int const soldAttack = e.S(0).board[e.S(0).board.IndexOfUid(uid)].attack;
    CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
    CHECK(e.S(0).offers[offered].attack == offerAttack + soldAttack);   // the sold stats went to a tavern minion
}

static void TestRedHand()
{
    Env e;
    Activate(e, "BG24_Reward_131");
    int const card = CardOfTier(1);
    int const base = CardAt(card).attack;
    PutHand(e, card);
    e.shop.StartTurn(0, 3);   // RewardStartTurn runs
    CHECK(e.S(0).hand[0].attack == base + 12);   // +12/+12
}

static void TestFriendsAlongTheWay()
{
    Env e;
    e.shop.SetLobbyTribes(std::vector<uint32_t>{ kTribeBeast });
    Activate(e, "BG24_Reward_134");
    CHECK(e.S(0).rewardState.friendType == kTribeBeast);
    e.shop.StartTurn(0, 3);   // 2 random Beasts
    int beasts = 0;
    for (Minion const& m : e.S(0).hand.Items())
        if (m.IsTribe(kTribeBeast))
            ++beasts;
    CHECK(beasts == 2);
}

static void TestTinyHenchmen()
{
    Env e;
    Activate(e, "BG24_Reward_136");
    e.shop.StartTurn(0, 3);
    uint32_t const a = PutBoard(e, CardOfTier(1));
    uint32_t const b = PutBoard(e, CardOfTier(2));
    e.shop.Settle(0);
    int const a0 = e.S(0).board[e.S(0).board.IndexOfUid(a)].attack;
    int const b0 = e.S(0).board[e.S(0).board.IndexOfUid(b)].attack;
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(e.S(0).board[e.S(0).board.IndexOfUid(a)].attack == a0 + 2);
    CHECK(e.S(0).board[e.S(0).board.IndexOfUid(b)].attack == b0 + 2);
}

static void TestSnickerSnacks()
{
    Env e(1, 2, &EffectRegistry::Global());
    Activate(e, "BG24_Reward_107");
    e.shop.StartTurn(0, 3);
    int plain = -1;   // a Tier 1 minion without a Battlecry
    for (int i = 0; i < CardCount() && plain < 0; ++i)
        if (CardAt(i).role == CardRole::Pool && CardAt(i).tier == 1)
            if (EffectDef const* d = EffectRegistry::Global().Find(i); !d || !d->battlecry)
                plain = i;
    CHECK(plain >= 0);
    PutBoard(e, plain);
    PutBoard(e, plain);
    PutBoard(e, FindCard("BG_CFM_315"));   // Alleycat: Battlecry: Summon a 1/1 Cat
    e.shop.Settle(0);
    CHECK(e.S(0).board.Size() == 3);
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(e.S(0).board.Size() == 4);   // the Battlecry minion is always picked, never the plain ones
}

static void TestParasol()
{
    Env e;
    Activate(e, "BG24_Reward_115");
    e.shop.StartTurn(0, 3);
    uint32_t const uid = PutBoard(e, CardOfTier(1));
    int const h0 = e.S(0).board[e.S(0).board.IndexOfUid(uid)].maxHealth;
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    Minion const& m = e.S(0).board[e.S(0).board.IndexOfUid(uid)];
    CHECK((m.keywords & kKwStealth) != 0);   // right-most minion got Stealth
    CHECK(m.maxHealth == h0 + 8);            // ... and +8 Health
}

static void TestDevilsInTheDetails()
{
    Env e;
    Activate(e, "BG24_Reward_309");
    e.shop.StartTurn(0, 3);
    uint32_t const uid = PutBoard(e, CardOfTier(1));
    PutOffer(e, CardOfTier(1));
    int const offers0 = static_cast<int>(e.S(0).offers.size());
    int const a0 = e.S(0).board[e.S(0).board.IndexOfUid(uid)].attack;
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(static_cast<int>(e.S(0).offers.size()) < offers0);   // a tavern minion was consumed
    CHECK(e.S(0).board[e.S(0).board.IndexOfUid(uid)].attack > a0);
}

static void TestMenagerieMayhem()
{
    Env e;
    Activate(e, "BG24_Reward_331");
    e.shop.StartTurn(0, 3);
    int const beast = CardOfTier(1);
    // two minions of different types
    int other = -1;
    for (int i = 0; i < CardCount(); ++i)
        if (CardAt(i).role == CardRole::Pool && CardAt(i).tribes != 0 && CardAt(i).tribes != kTribeAll &&
            CardAt(i).tribes != CardAt(beast).tribes)
        {
            other = i;
            break;
        }
    CHECK(other >= 0);
    uint32_t const u1 = PutBoard(e, beast);
    uint32_t const u2 = PutBoard(e, other);
    e.shop.Settle(0);
    int const a1 = e.S(0).board[e.S(0).board.IndexOfUid(u1)].attack;
    int const a2 = e.S(0).board[e.S(0).board.IndexOfUid(u2)].attack;
    CHECK(e.shop.EndTurn(0) == ActionResult::Ok);
    CHECK(e.S(0).board[e.S(0).board.IndexOfUid(u1)].attack == a1 + 2);   // 2 types
    CHECK(e.S(0).board[e.S(0).board.IndexOfUid(u2)].attack == a2 + 2);
}

static void TestMirrorShield()
{
    Env e;
    Activate(e, "BG24_Reward_128");
    e.shop.StartTurn(0, 3);
    e.shop.RefreshOffers(0);
    bool shielded = false;
    for (Minion const& o : e.S(0).offers)
        if (o.Has(kKwDivineShield))
            shielded = true;
    CHECK(shielded);
}

static void TestSecretSinstone()
{
    Env e;
    Activate(e, "BG24_Reward_129");
    e.shop.StartTurn(0, 3);
    CHECK(e.shop.Discover(0, 1, 6, 0));
    int const before = e.S(0).hand.Size();
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == before + 2);   // the chosen card and its extra copy
    CHECK(e.S(0).hand[e.S(0).hand.Size() - 1].card == e.S(0).hand[e.S(0).hand.Size() - 2].card);
}

static void TestYogg()
{
    Env e;
    Activate(e, "BG24_Reward_135");
    // The spin has six random outcomes; smoke-test that a start of turn (which spins) does not break anything.
    e.shop.StartTurn(0, 3);
    CHECK(e.S(0).rewardState.active);
}

// ------------------------------------------------------------------------------------ the combat rewards

static CombatSide Side(std::initializer_list<Minion> minions, int tier = 1)
{
    CombatSide s;
    s.tavernTier = tier;
    for (Minion const& m : minions)
        s.board.Push(m);
    return s;
}

static CombatSettings Cfg(RewardState const* mine, RewardState const* theirs = nullptr)
{
    CombatSettings s;
    s.effects = &NoEffects();
    s.rewardState[0] = mine;
    s.rewardState[1] = theirs;
    return s;
}

static Minion V(int attack, int health, int tier = 1)
{
    static char const* const ids[] = { "", "BG21_027", "BG21_013", "BG23_361", "BG23_362", "BG20_401", "BG24_020" };
    Minion m = MakeMinion(FindCard(ids[tier]));
    m.attack = attack;
    m.health = m.maxHealth = health;
    return m;
}

static RewardState CombatReward(char const* id)
{
    RewardState st;
    st.id = FindReward(id);
    st.active = true;
    return st;
}

static void TestStaffOfOrigination()
{
    RewardState st = CombatReward("BG24_Reward_312");
    Minion mine = V(2, 2, 3);
    Minion theirs = V(50, 50, 3);
    CombatResult r = SimulateCombat(Side({ mine }, 3), Side({ theirs }, 3), 5, Cfg(&st));
    CHECK(!r.start[0].Empty());
    CHECK(r.start[0][0].attack == 2);   // start board unchanged; the +15/+15 lands at start of combat
    // with +15/+15 the 2/2 becomes 17/17 and survives longer than it otherwise would
    CHECK(r.events.size() > 0);
}

static void TestEvilTwin()
{
    RewardState st = CombatReward("BG24_Reward_111");
    CombatResult r = SimulateCombat(Side({ V(1, 5, 1) }, 1), Side({ V(2, 2, 1), V(2, 2, 1) }, 1), 9, Cfg(&st));
    // a copy of the highest-Health minion was summoned for side 0
    bool summoned = false;
    for (CombatEvent const& e : r.events)
        if (e.type == EventType::Summon && e.side == 0)
            summoned = true;
    CHECK(summoned);
}

static int GoldenCapableCard(int tier)
{
    for (int i = 0; i < CardCount(); ++i)
        if (CardAt(i).role == CardRole::Pool && CardAt(i).tier == tier && CardAt(i).goldenId[0] != '\0')
            return i;
    return CardOfTier(tier);
}

static void TestStolenGold()
{
    RewardState st = CombatReward("BG24_Reward_109");
    int const edge = GoldenCapableCard(3);
    CHECK(CardAt(edge).goldenId[0] != '\0');
    Minion a = MakeMinion(edge);
    a.attack = 1;
    a.health = a.maxHealth = 5;
    Minion b = a, c = a;
    CombatResult r = SimulateCombat(Side({ a, b, c }, 3), Side({ V(0, 30, 1) }, 1), 11, Cfg(&st));
    // the two edge minions of side 0 entered combat golden (the wall has 0 attack, so they survive)
    CHECK(!r.survivors[0].Empty());
    CHECK(r.survivors[0][0].golden);
}

static void TestRitualDagger()
{
    RewardState st = CombatReward("BG24_Reward_113");
    // A Deathrattle minion of side 0 dies; the persistent change records +4/+4 (kind kPersistBuff).
    CombatSide a = Side({ V(1, 1, 2), V(1, 1, 2) }, 2);
    // give the first minion a Deathrattle effect from the real registry
    int dr = -1;
    for (int i = 0; i < CardCount() && dr < 0; ++i)
    {
        EffectDef const* e = EffectRegistry::Global().Find(i);
        if (e && e->deathrattle)
            dr = i;
    }
    CHECK(dr >= 0);
    a.board[0].card = dr;
    a.board[0].attack = 1;
    a.board[0].health = a.board[0].maxHealth = 1;
    CombatSettings cfg;
    cfg.effects = &EffectRegistry::Global();   // so DeathrattleInstances sees the Deathrattle
    cfg.rewardState[0] = &st;
    CombatResult r = SimulateCombat(a, Side({ V(50, 50, 6) }, 6), 13, cfg);
    bool buffed = false;
    for (PersistentChange const& c : r.persistent)
        if (c.kind == kPersistBuff && c.attack >= 4 && c.health >= 4)
            buffed = true;
    CHECK(buffed);
}

// ------------------------------------------------------------------------------------ the Wheel and full games

static void TestWheelOutcomes()
{
    // Run StartTurn with Yogg across many seeds: every outcome path runs without breaking (the spin is random).
    for (uint32_t seed = 1; seed <= 30; ++seed)
    {
        Env e(seed);
        Activate(e, "BG24_Reward_135");
        e.shop.StartTurn(0, 3);
        e.shop.Settle(0);
        CHECK(e.shop.CheckConservation());
    }
}

// A card of the tier that is in this lobby's pool (so taking a copy keeps the pool's count exact).
static int LobbyCardOfTier(Env& e, int tier)
{
    for (int i = 0; i < CardCount(); ++i)
        if (CardAt(i).role == CardRole::Pool && CardAt(i).tier == tier && e.pool.Initial(i) > 0)
            return i;
    return -1;
}

// Empties the tavern with its copies back in the pool (a plain clear would lose them for CheckConservation).
static void ClearOffers(Env& e)
{
    for (Minion const& o : e.S(0).offers)
        e.pool.Return(o.card, o.poolCopies);
    e.S(0).offers.clear();
}

// Hand of Fate gives real Darkmoon Prizes: a Discover of three, any Prize turn.
static void TestHandOfFate()
{
    Env e(13);
    e.shop.StartTurn(0, 3);
    e.shop.WheelOutcome(0, 1);
    CHECK(e.shop.DiscoverOpen(0));
    CHECK(static_cast<int>(e.S(0).options.size()) == 3);
    for (Minion const& m : e.S(0).options)
        CHECK(DarkmoonPrizeTurnOf(m.card) >= 1 && DarkmoonPrizeTurnOf(m.card) <= 4);
    CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).hand.Size() == 1 && CardAt(e.S(0).hand[0].card).role == CardRole::Spell);
}

static void TestCurseOfFlesh()
{
    // +3/+3, then the base stats shuffle as (Attack, max Health) pairs: every minion ends at full health and the pairs
    // are the old ones plus 3.
    Env e(11);
    e.shop.StartTurn(0, 3);
    uint32_t const u1 = PutBoard(e, LobbyCardOfTier(e, 1));
    uint32_t const u2 = PutBoard(e, LobbyCardOfTier(e, 2));
    e.shop.Settle(0);
    std::vector<std::pair<int, int>> want;
    for (Minion const& m : e.S(0).board.Items())
        want.emplace_back(m.attack + 3, m.maxHealth + 3);
    e.shop.WheelOutcome(0, 2);
    std::vector<std::pair<int, int>> got;
    for (Minion const& m : e.S(0).board.Items())
    {
        CHECK(m.health == m.maxHealth);
        got.emplace_back(m.attack, m.maxHealth);
    }
    std::sort(want.begin(), want.end());
    std::sort(got.begin(), got.end());
    CHECK(got == want);
    CHECK(e.shop.FindMinion(0, u1) && e.shop.FindMinion(0, u2));
}

static void TestMindflayerGogglesStripsAura()
{
    // Alter Ego puts +6/+6 on even-tier offers; Mindflayer Goggles takes them and they keep it as stats (as a Buy).
    Env e;
    e.shop.StartTurn(0, 3);
    Activate(e, "BG24_Reward_321");   // gained mid-turn: the Even form
    ClearOffers(e);
    int const evenCard = LobbyCardOfTier(e, 2);
    PutOffer(e, evenCard);
    e.shop.Settle(0);
    CHECK(e.S(0).offers[0].auraAttack == 6);
    int const handBefore = e.S(0).hand.Size();
    e.shop.WheelOutcome(0, 3);
    CHECK(e.S(0).hand.Size() == handBefore + 1);
    Minion const& taken = e.S(0).hand[e.S(0).hand.Size() - 1];
    CHECK(taken.auraAttack == 0 && taken.auraHealth == 0);
    CHECK(taken.attack == CardAt(evenCard).attack + 6);
    CHECK(e.shop.CheckConservation());
}

static void TestDevouringHungerConserves()
{
    Env e(3);
    e.shop.StartTurn(0, 3);
    PutBoard(e, LobbyCardOfTier(e, 1));
    e.shop.Settle(0);
    int const before = e.S(0).board[0].attack;
    int eaten = 0;
    for (Minion const& o : e.S(0).offers)
        eaten += o.attack;
    e.shop.WheelOutcome(0, 4);
    CHECK(e.S(0).board[0].attack == before + eaten);   // the only minion got every offer's Attack
    CHECK(!e.S(0).offers.empty());                     // restocked
    CHECK(e.shop.CheckConservation());
}

static void TestFriendsUsesOfferedType()
{
    // The type shown on the offer (QuestState::rewardType) is the one the reward uses.
    Env e;
    e.shop.SetLobbyTribes(std::vector<uint32_t>{ kTribeBeast, kTribeMech });
    e.S(0).quest.rewardType = kTribeMech;
    Activate(e, "BG24_Reward_134");
    CHECK(e.S(0).rewardState.friendType == kTribeMech);
}

static void TestActivationWaitsForTheAction()
{
    // Dust for Prints (a card to hand) paired with Pilfered Lamps, one short: the Buy that finishes it turns the
    // reward on at the END of the action, and the two copies then merge into a Golden without breaking the pool.
    Env e;
    e.shop.StartTurn(0, 3);
    ClearOffers(e);
    int const card = LobbyCardOfTier(e, 1);
    uint32_t const onBoard = PutBoard(e, card);
    e.S(0).offers.push_back(NewMinion(e, card));
    e.S(0).quest.quest = FindQuest("BG24_Quest_314");
    e.S(0).quest.reward = FindReward("BG24_Reward_350");
    e.S(0).quest.required = 1;
    e.S(0).quest.count = 0;
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).quest.done);
    CHECK(e.S(0).rewardState.active);
    CHECK(e.shop.FindMinion(0, onBoard) == nullptr);   // merged
    bool golden = false;
    for (Minion const& m : e.S(0).hand.Items())
        if (m.card == card && m.golden)
            golden = true;
    CHECK(golden);
    CHECK(e.shop.CheckConservation());
}

static void TestWisdomballPrefersTriples()
{
    // Holding two copies of a card, a Wisdomball owner sees it in the tavern clearly more often than without it.
    auto rate = [](bool withBall)
    {
        Env e(77);
        if (withBall)
            Activate(e, "BG24_Reward_313");
        e.shop.StartTurn(0, 3);
        int const card = LobbyCardOfTier(e, 1);
        PutBoard(e, card);
        PutBoard(e, card);
        e.shop.Settle(0);
        int seen = 0;
        int const runs = 300;
        for (int i = 0; i < runs; ++i)
        {
            e.shop.RefreshOffers(0);
            for (Minion const& o : e.S(0).offers)
                if (o.card == card)
                {
                    ++seen;
                    break;
                }
        }
        CHECK(e.shop.CheckConservation());
        return seen * 100 / runs;
    };
    int const plain = rate(false);
    int const ball = rate(true);
    CHECK(ball >= plain + 15);
}

static void TestFullGameWithRewards()
{
    // A quest+reward NPC game: quests complete, rewards activate and play out without a crash, and the pool conserves.
    GameSetup s;
    s.seed = 4242;
    s.quests = true;
    s.questRound = 1;
    s.heroPick = false;
    s.autoRun = false;
    Game g(s);
    g.Pump();
    while (!g.Over())
        g.Pump();
    CHECK(g.WinnerFaction() == 0 || g.WinnerFaction() == 1);
    CHECK(g.GetShop().CheckConservation());
    int rewardsActive = 0;
    int questsDone = 0;
    for (int p = 0; p < kPlayers; ++p)
    {
        if (g.QuestDone(p))
            ++questsDone;
        if (g.GetShop().State(p).rewardState.active)
            ++rewardsActive;
    }
    CHECK(rewardsActive == questsDone);   // every finished quest has its reward active
    CHECK(questsDone > 0);
}

int main(int, char**)
{
    RUN(TestRegistry);
    RUN(TestSmokingGun);
    RUN(TestTealTiger);
    RUN(TestAlterEgo);
    RUN(TestPilferedLamps);
    RUN(TestGhastlyMask);
    RUN(TestCookedBook);
    RUN(TestAnimaBribe);
    RUN(TestRedHand);
    RUN(TestFriendsAlongTheWay);
    RUN(TestTinyHenchmen);
    RUN(TestSnickerSnacks);
    RUN(TestParasol);
    RUN(TestDevilsInTheDetails);
    RUN(TestMenagerieMayhem);
    RUN(TestMirrorShield);
    RUN(TestSecretSinstone);
    RUN(TestYogg);
    RUN(TestStaffOfOrigination);
    RUN(TestEvilTwin);
    RUN(TestStolenGold);
    RUN(TestRitualDagger);
    RUN(TestWheelOutcomes);
    RUN(TestHandOfFate);
    RUN(TestCurseOfFlesh);
    RUN(TestMindflayerGogglesStripsAura);
    RUN(TestDevouringHungerConserves);
    RUN(TestFriendsUsesOfferedType);
    RUN(TestActivationWaitsForTheAction);
    RUN(TestWisdomballPrefersTriples);
    RUN(TestFullGameWithRewards);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
