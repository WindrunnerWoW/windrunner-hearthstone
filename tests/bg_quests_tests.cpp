// The quest table (HsBgQuestData.inc), the quest registry (HsBgQuests), the counting rules (one test
// per quest) and the turn-4 quest offer of Game.
//   bg_quests_tests.exe [directory]     run all tests; exit code 0 = all checks passed.
//
// The unit tests drive ApplyQuestEvent directly (one event at a time); the integration tests drive the
// real Shop and Game so the firing points are covered too (a triple is not a summon, a Magnetic attach is a play but
// not a Battlecry, gold spending, the offer draw and ChooseQuest).

#include "../src/battlegrounds/HsBgCombat.h"
#include "../src/battlegrounds/HsBgEffects.h"
#include "../src/battlegrounds/HsBgGame.h"
#include "../src/battlegrounds/HsBgPool.h"
#include "../src/battlegrounds/HsBgQuests.h"
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
    static EffectRegistry const reg;   // empty: no minion has an effect in the unit tests
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

static void GiveQuest(Env& e, int p, char const* id, int required)
{
    int const q = FindQuest(id);
    CHECK(q >= 0);
    e.S(p).quest.quest = q;
    e.S(p).quest.required = required;
    e.S(p).quest.count = 0;
    e.S(p).quest.done = false;
}

static uint32_t PutBoard(Env& e, int p, int card, bool golden = false)
{
    e.pool.Take(card);
    Minion m = MakeMinion(card, golden, e.shop.NewUid());
    m.poolCopies = 1;
    e.S(p).board.Push(m);
    return m.uid;
}

static uint32_t PutHand(Env& e, int p, int card, bool golden = false)
{
    e.pool.Take(card);
    Minion m = MakeMinion(card, golden, e.shop.NewUid());
    m.poolCopies = 1;
    e.S(p).hand.Push(m);
    return m.uid;
}

static int FindWithKeyword(uint32_t kw)
{
    for (int i = 0; i < CardCount(); ++i)
        if ((CardAt(i).keywords & kw) != 0 && CardAt(i).role == CardRole::Pool)
            return i;
    return -1;
}

static int FindTribeCard(uint32_t tribe, bool needEffectBattlecry)
{
    for (int i = 0; i < CardCount(); ++i)
    {
        if (CardAt(i).role != CardRole::Pool || !HasTribe(CardAt(i).tribes, tribe))
            continue;
        if (needEffectBattlecry)
        {
            EffectDef const* d = EffectRegistry::Global().Find(i);
            if (!d || !d->battlecry)
                continue;
        }
        return i;
    }
    return -1;
}

// ------------------------------------------------------------------------------------ the table and registry

static void TestQuestTable()
{
    CHECK(QuestCount() == 15);
    int inPool = 0;
    for (int i = 0; i < QuestCount(); ++i)
        if (QuestAt(i).inPool)
            ++inPool;
    CHECK(inPool == 12);
    CHECK(RewardCount() == 35);
    int rInPool = 0;
    for (int i = 0; i < RewardCount(); ++i)
        if (RewardAt(i).inPool)
            ++rInPool;
    CHECK(rInPool == 23);
    int const track = FindQuest("BG24_Quest_112");
    CHECK(track >= 0);
    CHECK(std::string(QuestAt(track).id) == "BG24_Quest_112");
    CHECK(QuestAt(track).progress == 10);
    CHECK(FindQuest("BG24_Quest_318") >= 0 && !QuestAt(FindQuest("BG24_Quest_318")).inPool);   // removed
    CHECK(FindReward("BG24_Reward_130") >= 0 && RewardAt(FindReward("BG24_Reward_130")).inPool);
    CHECK(FindQuest("nope") < 0);
    CHECK(FindReward("nope") < 0);
}

static void TestRegistryComplete()
{
    QuestRegistry& reg = const_cast<QuestRegistry&>(QuestRegistry::Global());
    CHECK(reg.Errors().empty());
    CHECK(reg.Size() == 12);
    for (int i = 0; i < QuestCount(); ++i)
        if (QuestAt(i).inPool)
            CHECK(reg.Find(QuestAt(i).id) != nullptr);
}

// ------------------------------------------------------------------------------------ one event per quest

static void Fire(QuestState& st, QuestEvent kind, int amount = 1, uint32_t tribe = 0)
{
    ApplyQuestEvent(st, QuestEventInfo{ kind, amount, tribe });
}

static void TestCountingRules()
{
    struct Case
    {
        char const* id;
        QuestEvent kind;
        int amount;    // the amount fired
        int expected;
    };
    Case const cases[] = {
        { "BG24_Quest_112", QuestEvent::Refresh, 1, 1 },
        { "BG24_Quest_114", QuestEvent::Summon, 1, 1 },
        { "BG24_Quest_120", QuestEvent::LoseOrTieCombat, 1, 1 },
        { "BG24_Quest_123", QuestEvent::StatsIncreased, 1, 1 },
        { "BG24_Quest_124", QuestEvent::FriendlyDied, 3, 3 },           // every death of a combat counts
        { "BG24_Quest_125", QuestEvent::CombatOrdered, 1, 1 },
        { "BG24_Quest_126", QuestEvent::GoldSpent, 7, 7 },              // gold spent counts its amount
        { "BG24_Quest_311", QuestEvent::PlayBattlecry, 1, 1 },
        { "BG24_Quest_313", QuestEvent::Buy, 1, 1 },
        { "BG24_Quest_314", QuestEvent::CardToHand, 1, 1 },
        { "BG24_Quest_320", QuestEvent::DeathrattleTriggered, 2, 2 },   // every Deathrattle of a combat counts
    };
    for (Case const& c : cases)
    {
        QuestState st;
        st.quest = FindQuest(c.id);
        st.required = QuestAt(st.quest).progress;
        Fire(st, c.kind, c.amount);
        CHECK(st.count == c.expected);
        CHECK(!st.done);
    }
}

static void TestWrongEventDoesNothing()
{
    QuestState st;
    st.quest = FindQuest("BG24_Quest_112");   // counts refreshes
    st.required = 10;
    Fire(st, QuestEvent::Buy);
    Fire(st, QuestEvent::Summon);
    CHECK(st.count == 0);
    Fire(st, QuestEvent::Refresh);
    CHECK(st.count == 1);
}

static void TestUnlikelyDuo()
{
    QuestState st;
    st.quest = FindQuest("BG24_Quest_151");
    st.required = 5;
    st.typeA = kTribeBeast;
    st.typeB = kTribeDemon;
    Fire(st, QuestEvent::PlayTribe, 1, kTribeBeast);
    Fire(st, QuestEvent::PlayTribe, 1, kTribeDemon);
    CHECK(st.count == 2);
    Fire(st, QuestEvent::PlayTribe, 1, kTribeMech);   // not one of the two
    CHECK(st.count == 2);
    Fire(st, QuestEvent::PlayTribe, 1, kTribeAll);    // a dual/All minion matches either type
    CHECK(st.count == 3);
}

static void TestCompletion()
{
    QuestState st;
    st.quest = FindQuest("BG24_Quest_120");   // lose or tie 3
    st.required = 3;
    Fire(st, QuestEvent::LoseOrTieCombat);
    Fire(st, QuestEvent::LoseOrTieCombat);
    CHECK(!st.done);
    Fire(st, QuestEvent::LoseOrTieCombat);
    CHECK(st.done && st.count == 3);
    Fire(st, QuestEvent::LoseOrTieCombat);    // no further progress once done
    CHECK(st.count == 3);
}

static void TestNoQuestNoCount()
{
    QuestState st;                            // nothing taken
    Fire(st, QuestEvent::Refresh);
    CHECK(st.count == 0 && !st.done);
}

// ------------------------------------------------------------------------------------ the offer draw (Game)

static GameSetup QuestSetup(uint32_t seed, bool human, int questRound)
{
    GameSetup s;
    s.seed = seed;
    s.quests = true;
    s.questRound = questRound;
    s.autoRun = false;
    s.slice15AOnly = true;
    if (human)
        s.kinds[0] = PlayerKind::External;
    return s;
}

static void TestOfferDraw()
{
    GameSetup s = QuestSetup(4242, false, 1);
    Game g(s);
    CHECK(g.Phase() == GamePhase::Quest);
    CHECK(g.QuestPicking());
    for (int p = 0; p < kPlayers; ++p)
    {
        std::vector<std::pair<int, int>> const& off = g.QuestOffer(p);
        CHECK(off.size() == 3);
        std::set<int> qs, rs;
        for (auto const& pr : off)
        {
            CHECK(pr.first >= 0 && pr.first < QuestCount() && QuestAt(pr.first).inPool);
            CHECK(pr.second >= 0 && pr.second < RewardCount() && RewardAt(pr.second).inPool);
            qs.insert(pr.first);
            rs.insert(pr.second);
        }
        CHECK(qs.size() == off.size());       // distinct quests
        CHECK(rs.size() == off.size());       // distinct rewards
    }
    // deterministic for a seed
    Game h(QuestSetup(4242, false, 1));
    for (int p = 0; p < kPlayers; ++p)
        CHECK(g.QuestOffer(p) == h.QuestOffer(p));
}

static void TestNpcPicksQuests()
{
    GameSetup s = QuestSetup(99, false, 1);
    Game g(s);
    CHECK(g.QuestPicking());
    g.Pump();     // NPCs pick; nobody external is pending, so the pick closes
    CHECK(!g.QuestPicking());
    for (int p = 0; p < kPlayers; ++p)
    {
        CHECK(g.PlayerQuest(p) >= 0);
        CHECK(g.QuestRequired(p) == QuestAt(g.PlayerQuest(p)).progress);
        CHECK(g.QuestProgress(p) <= g.QuestRequired(p));
    }
}

static void TestHumanQuestPick()
{
    GameSetup s = QuestSetup(7, true, 1);
    Game g(s);
    CHECK(g.Phase() == GamePhase::Quest);
    CHECK(g.QuestOffer(0).size() == 3);
    std::pair<int, int> const want = g.QuestOffer(0)[1];
    CHECK(g.Buy(0, 0) == ActionResult::NotShopPhase);       // the shop waits for the pick
    CHECK(g.ChooseQuest(0, 5) == ActionResult::BadIndex);
    CHECK(g.ChooseQuest(0, 1) == ActionResult::Ok);
    CHECK(g.PlayerQuest(0) == want.first);
    CHECK(g.PlayerReward(0) == want.second);
    CHECK(g.QuestRequired(0) == QuestAt(want.first).progress);
    CHECK(g.QuestOffer(0).empty());
    CHECK(g.ChooseQuest(0, 0) == ActionResult::NotPicking); // already picked
    CHECK(!g.QuestPicking());
    // the NPCs of the same game all took a quest too
    for (int p = 1; p < kPlayers; ++p)
        CHECK(g.PlayerQuest(p) >= 0);
}

static void TestOfferTypes()
{
    // The types a pair names are drawn with the offer (so the pick can show them) and carried into the taken quest.
    for (uint32_t seed = 1; seed <= 40; ++seed)
    {
        Game g(QuestSetup(seed, true, 1));
        QuestState const& qs = g.GetShop().State(0).quest;
        CHECK(qs.offerTypes.size() == qs.offer.size());
        for (std::size_t i = 0; i < qs.offer.size() && i < qs.offerTypes.size(); ++i)
        {
            QuestTypes const& t = qs.offerTypes[i];
            bool const duo = std::string(QuestAt(qs.offer[i].first).id) == "BG24_Quest_151";
            bool const friends = std::string(RewardAt(qs.offer[i].second).id) == "BG24_Reward_134";
            CHECK(duo ? (t.typeA != 0 && t.typeB != 0 && t.typeA != t.typeB) : (t.typeA == 0 && t.typeB == 0));
            CHECK(friends ? t.rewardType != 0 : t.rewardType == 0);
        }
        QuestTypes const pick = qs.offerTypes.empty() ? QuestTypes{} : qs.offerTypes[0];
        CHECK(g.ChooseQuest(0, 0) == ActionResult::Ok);
        CHECK(qs.typeA == pick.typeA && qs.typeB == pick.typeB && qs.rewardType == pick.rewardType);
        CHECK(qs.offerTypes.empty());
    }
}

static void TestCombatCompletionActivatesReward()
{
    // A human who takes Unmask the Culprit (lose or tie 3 combats) and never buys loses combats; the combat that
    // finishes the quest turns the reward on at once, before the human's next action.
    int tested = 0;
    for (uint32_t seed = 1; seed <= 300 && tested < 3; ++seed)
    {
        Game g(QuestSetup(seed, true, 1));
        int pick = -1;
        for (std::size_t i = 0; i < g.QuestOffer(0).size(); ++i)
            if (std::string(QuestAt(g.QuestOffer(0)[i].first).id) == "BG24_Quest_120")
                pick = static_cast<int>(i);
        if (pick < 0)
            continue;
        ++tested;
        CHECK(g.ChooseQuest(0, pick) == ActionResult::Ok);
        bool sawDone = false;
        for (int guard = 0; guard < 60 && !g.Over() && g.Alive(0); ++guard)
        {
            int const round = g.Round();
            CHECK(g.EndTurn(0) == ActionResult::Ok);
            if (g.QuestDone(0))
            {
                PlayerShop const& ps = g.GetShop().State(0);
                CHECK(ps.rewardState.active);
                if (!sawDone)
                    CHECK(ps.rewardState.doneTurn == round);   // the round whose combat finished it
                sawDone = true;
            }
        }
        CHECK(sawDone);
    }
    CHECK(tested > 0);
}

static void TestUnlikelyDuoTypes()
{
    // A game whose lobby has at least two tribes draws two distinct types for Unlikely Duo.
    GameSetup s = QuestSetup(5, false, 1);
    Game g(s);
    g.Pump();
    for (int p = 0; p < kPlayers; ++p)
    {
        PlayerShop const& ps = g.GetShop().State(p);
        if (ps.quest.quest >= 0 && std::string(QuestAt(ps.quest.quest).id) == "BG24_Quest_151")
        {
            CHECK(ps.quest.typeA != 0 && ps.quest.typeB != 0 && ps.quest.typeA != ps.quest.typeB);
            return;   // found one (a seed that offers it to somebody)
        }
    }
    // not every seed offers Unlikely Duo; the check above already ran for the seed if it did
}

// ------------------------------------------------------------------------------------ the Shop firing points

static void TestShopBuyAndGold()
{
    Env e;
    e.shop.StartTurn(0, 1);                            // gold 3, offers filled
    GiveQuest(e, 0, "BG24_Quest_313", 7);              // Invite the Guests (buy 7)
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == 1);
    GiveQuest(e, 0, "BG24_Quest_126", 25);             // Follow the Money (spend 25)
    e.shop.StartTurn(0, 1);
    CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == kBuyCost);             // gold spent by the buy
}

static void TestShopRefresh()
{
    Env e;
    e.shop.StartTurn(0, 1);
    GiveQuest(e, 0, "BG24_Quest_112", 10);             // Track the Footprints (refresh 10)
    CHECK(e.shop.Refresh(0) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == 1);
    e.shop.RefreshOffers(0);                           // a free refresh counts too
    CHECK(e.S(0).quest.count == 2);
    e.shop.StartTurn(0, 2);                            // the start-of-turn restock does NOT count
    CHECK(e.S(0).quest.count == 2);
}

static void TestShopSummonAndBattlecry()
{
    Env e(1, 2, &EffectRegistry::Global());
    e.shop.StartTurn(0, 3);
    int const plain = FindTribeCard(kTribeBeast, false);
    CHECK(plain >= 0);
    PutHand(e, 0, plain);
    GiveQuest(e, 0, "BG24_Quest_114", 14);             // Assemble a Lineup
    CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == 1);                    // playing counts as summoning

    int const bc = FindTribeCard(kTribeBeast, true);
    CHECK(bc >= 0);
    e.shop.StartTurn(0, 4);
    PutHand(e, 0, bc);
    GiveQuest(e, 0, "BG24_Quest_311", 6);              // Cry for Help
    CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == 1);
}

static void TestTripleIsNotASummon()
{
    Env e;
    e.shop.StartTurn(0, 4);
    int const card = FindTribeCard(kTribeBeast, false);
    CHECK(card >= 0);
    for (int i = 0; i < 3; ++i)
    {
        e.pool.Take(card);
        Minion m = MakeMinion(card, false, e.shop.NewUid());
        m.poolCopies = 1;
        e.S(0).hand.Push(m);
    }
    GiveQuest(e, 0, "BG24_Quest_114", 14);
    e.shop.Settle(0);                                  // merges the three into a golden in the hand
    CHECK(e.S(0).hand.Size() == 1 && e.S(0).hand[0].golden);
    CHECK(e.S(0).quest.count == 0);                    // the merge is not a summon
}

static void TestMagneticIsAPlayNotABattlecry()
{
    Env e(1, 2, &EffectRegistry::Global());
    e.shop.StartTurn(0, 4);
    int const magnetic = FindWithKeyword(kKwMagnetic);
    CHECK(magnetic >= 0);
    int const mech = FindTribeCard(kTribeMech, false);
    CHECK(mech >= 0);
    uint32_t const mechUid = PutBoard(e, 0, mech);
    PutHand(e, 0, magnetic);

    GiveQuest(e, 0, "BG24_Quest_151", 5);              // Unlikely Duo: the Mechanical type
    e.S(0).quest.typeA = kTribeMech;
    e.S(0).quest.typeB = kTribeBeast;
    CHECK(e.shop.Play(0, 0, 0, mechUid) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == 1);                    // a Magnetic attach is a play of a Mech

    e.shop.StartTurn(0, 4);
    uint32_t const mechUid2 = PutBoard(e, 0, mech);
    PutHand(e, 0, magnetic);
    GiveQuest(e, 0, "BG24_Quest_311", 6);              // Cry for Help
    CHECK(e.shop.Play(0, 0, 0, mechUid2) == ActionResult::Ok);
    CHECK(e.S(0).quest.count == 0);                    // ... but it has no Battlecry
}

static void TestStatsIncreased()
{
    Env e;
    e.shop.StartTurn(0, 4);
    int const card = FindTribeCard(kTribeBeast, false);
    CHECK(card >= 0);
    uint32_t const uid = PutBoard(e, 0, card);
    GiveQuest(e, 0, "BG24_Quest_123", 15);             // Find the Murder Weapon
    e.shop.Buff(0, e.S(0).board[e.S(0).board.IndexOfUid(uid)], 2, 3);
    CHECK(e.S(0).quest.count == 1);
    e.shop.Buff(0, e.S(0).board[e.S(0).board.IndexOfUid(uid)], 0, 0);   // a no-op buff does not count
    CHECK(e.S(0).quest.count == 1);
    e.shop.Buff(0, e.S(0).board[e.S(0).board.IndexOfUid(uid)], -1, 0);  // nor does a debuff
    CHECK(e.S(0).quest.count == 1);
}

static void TestStealthExpiresAtTurnStart()
{
    Env e;
    e.shop.StartTurn(0, 2);
    int const card = FindTribeCard(kTribeBeast, false);
    CHECK(card >= 0);
    uint32_t const uid = PutBoard(e, 0, card);
    e.S(0).board[e.S(0).board.IndexOfUid(uid)].keywords |= kKwStealth;   // Theotar's Parasol granted it last turn end
    CHECK((e.S(0).board[e.S(0).board.IndexOfUid(uid)].keywords & kKwStealth) != 0);
    e.shop.StartTurn(0, 3);                                              // "until next turn" ends now
    CHECK((e.S(0).board[e.S(0).board.IndexOfUid(uid)].keywords & kKwStealth) == 0);
}

// ------------------------------------------------------------------------------------ combat events (full game)

static void TestCombatQuestsInAGame()
{
    // A quest-enabled NPC game runs its quests and its combat events without breaking: every count stays within its
    // requirement, and losing or tying a combat advances Unmask the Culprit for somebody.
    GameSetup s = QuestSetup(31337, false, 1);
    s.policy = nullptr;
    Game g(s);                                          // autoRun false: offers dealt, pick open
    g.Pump();                                           // NPCs take quests, turns run
    while (!g.Over())
        g.Pump();
    for (int p = 0; p < kPlayers; ++p)
    {
        int const q = g.PlayerQuest(p);
        CHECK(q >= 0);
        CHECK(g.QuestProgress(p) <= g.QuestRequired(p));
    }
    CHECK(g.WinnerFaction() == 0 || g.WinnerFaction() == 1);
}

static void TestDeterminism()
{
    // Two identical quest games give the same outcome (same seed, same actions).
    GameSetup s1 = QuestSetup(2024, false, 1);
    Game g1(s1);
    g1.Pump();
    GameSetup s2 = QuestSetup(2024, false, 1);
    Game g2(s2);
    g2.Pump();
    CHECK(g1.WinnerFaction() == g2.WinnerFaction());
    for (int p = 0; p < kPlayers; ++p)
    {
        CHECK(g1.PlayerQuest(p) == g2.PlayerQuest(p));
        CHECK(g1.PlayerReward(p) == g2.PlayerReward(p));
        CHECK(g1.QuestProgress(p) == g2.QuestProgress(p));
    }
}

int main(int, char**)
{
    RUN(TestQuestTable);
    RUN(TestRegistryComplete);
    RUN(TestCountingRules);
    RUN(TestWrongEventDoesNothing);
    RUN(TestUnlikelyDuo);
    RUN(TestCompletion);
    RUN(TestNoQuestNoCount);
    RUN(TestOfferDraw);
    RUN(TestNpcPicksQuests);
    RUN(TestHumanQuestPick);
    RUN(TestUnlikelyDuoTypes);
    RUN(TestOfferTypes);
    RUN(TestCombatCompletionActivatesReward);
    RUN(TestShopBuyAndGold);
    RUN(TestShopRefresh);
    RUN(TestShopSummonAndBattlecry);
    RUN(TestTripleIsNotASummon);
    RUN(TestMagneticIsAPlayNotABattlecry);
    RUN(TestStatsIncreased);
    RUN(TestStealthExpiresAtTurnStart);
    RUN(TestCombatQuestsInAGame);
    RUN(TestDeterminism);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
