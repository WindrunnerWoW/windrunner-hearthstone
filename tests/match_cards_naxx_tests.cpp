// Standalone tests of the Naxxramas cards (src/match/HsMatchCardsNaxx.cpp): all 30 collectible Curse of Naxxramas cards
// of build 6024 (wave 1: 23 cards, wave 2: Avenge, Duplicate, Echoing Ooze, Baron Rivendare, Stalagg, Feugen,
// Kel'Thuzad). One or more focused scenarios per card and the interactions the rules call for
// (Undertaker with Deathrattle summons, Weblord taxing both hands, Loatheb for the next enemy turn only, Maexxna against
// Divine Shield, Poison Seeds with Deathrattles, Reincarnate on Deathrattle minions, Voidcaller without a Demon ...),
// LegalActions against Apply, determinism, a random-games soak with decks full of these cards, AI games and the
// redacted copy. Uses every card of the dataset (tests/match_all_cards.h).
//   match_cards_naxx_tests   run all tests; exit code 0 = all checks passed.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "match_all_cards.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace Hs::Match;

static int g_checks = 0;
static int g_failed = 0;
static char const* g_test = "";
static int g_gamesPerClass = 30; // --games N: a longer soak run

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);                  \
        }                                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                                \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        long long const va_ = static_cast<long long>(a);                                              \
        long long const vb_ = static_cast<long long>(b);                                              \
        if (va_ != vb_) {                                                                             \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s == %s (%lld vs %lld)\n", g_test, __FILE__, __LINE__, #a, #b, va_, vb_); \
        }                                                                                             \
    } while (0)

#define RUN(fn)                                                                                       \
    do {                                                                                              \
        g_test = #fn;                                                                                 \
        int const before = g_failed;                                                                  \
        fn();                                                                                         \
        std::printf("%-44s %s\n", #fn, g_failed == before ? "ok" : "FAILED");                         \
    } while (0)

namespace
{
    using TA = TestAccess;
    using Ids = std::vector<EntityId>;
    using Strs = std::vector<std::string>;

    CardDb const& Db() { return MatchAllCards::Db(); }

    // The 30 collectible cards (23 of wave 1, then the 7 of wave 2) and their tokens.
    char const* const kCards[] = {"FP1_001", "FP1_028", "FP1_002", "FP1_007", "FP1_024", "FP1_005", "FP1_027", "FP1_029",
        "FP1_023", "FP1_016", "FP1_019", "FP1_021", "FP1_026", "FP1_008", "FP1_012", "FP1_017", "FP1_011", "FP1_022",
        "FP1_025", "FP1_009", "FP1_004", "FP1_010", "FP1_030", "FP1_020", "FP1_018", "FP1_003", "FP1_031", "FP1_014",
        "FP1_015", "FP1_013"};
    char const* const kTokens[] = {"FP1_002t", "FP1_007t", "FP1_012t", "FP1_019t", "FP1_014t"};

    // ---- helpers ------------------------------------------------------------------------------

    // A match in its play phase, side 0 active on turn 1 with 10 mana; both hands empty, both decks 20 Murloc
    // Raiders, boards empty.
    std::unique_ptr<Match> Fresh(char const* c0 = "MAGE", char const* c1 = "WARRIOR", uint32_t seed = 11)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = 0;
        s.sides[0].name = "Jonas";
        s.sides[0].heroClass = c0;
        s.sides[0].deck = std::vector<std::string>(20, "CS2_168");
        s.sides[1].name = "Innkeeper";
        s.sides[1].heroClass = c1;
        s.sides[1].deck = std::vector<std::string>(20, "CS2_168");
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (!m)
        {
            std::printf("  create failed: %s\n", error.c_str());
            return m;
        }
        Action mull;
        mull.type = ActionType::Mulligan;
        m->Apply(0, mull);
        m->Apply(1, mull);
        TA::SetHand(*m, 0, {});
        TA::SetHand(*m, 1, {});
        TA::SetMana(*m, 0, 10, 10);
        TA::SetMana(*m, 1, 10, 10);
        return m;
    }

    Action Play(EntityId source, EntityId target = 0, int choice = 0, int position = -1)
    {
        Action a;
        a.type = ActionType::Play;
        a.source = source;
        a.target = target;
        a.choice = choice;
        a.position = position;
        return a;
    }

    Action Attack(EntityId source, EntityId target)
    {
        Action a;
        a.type = ActionType::Attack;
        a.source = source;
        a.target = target;
        return a;
    }

    Action End()
    {
        Action a;
        a.type = ActionType::EndTurn;
        return a;
    }

    Action Choose(EntityId id)
    {
        Action a;
        a.type = ActionType::Choose;
        a.source = id;
        return a;
    }

    Action Power(Match const& m, int side, EntityId target = 0)
    {
        Action a;
        a.type = ActionType::HeroPower;
        a.source = TA::PowerId(m, side);
        a.target = target;
        return a;
    }

    EntityId Hero(Match const& m, int side) { return TA::HeroId(m, side); }

    MinionView const* FindMinion(SideView const& s, EntityId id)
    {
        for (MinionView const& m : s.board)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    // The minion as side 0 sees it (id 0 when it is not on a board).
    MinionView MV(Match const& m, EntityId id)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        if (MinionView const* p = FindMinion(v.self, id))
            return *p;
        if (MinionView const* o = FindMinion(v.other, id))
            return *o;
        return MinionView();
    }

    Strs Board(Match const& m, int side) { return TA::BoardCardIds(m, side); }
    Strs Hand(Match const& m, int side) { return TA::HandCardIds(m, side); }
    int HP(Match const& m, int side) { return m.HeroHealth(side); }
    HeroView HV(Match const& m, int side)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        return side == 0 ? v.self.hero : v.other.hero;
    }

    int CountEvents(Match const& m, EventType type, uint32_t since = 0)
    {
        int n = 0;
        for (Event const& e : m.ViewFor(0, since).events)
            if (e.type == type)
                ++n;
        return n;
    }

    bool Has(Strs const& v, char const* id) { return std::find(v.begin(), v.end(), std::string(id)) != v.end(); }
    int CountOf(Strs const& v, char const* id) { return static_cast<int>(std::count(v.begin(), v.end(), std::string(id))); }

    void Mana(Match& m, int side = 0) { TA::SetMana(m, side, 10, 10); }

    // Puts exactly one card in the hand of side and plays it (the mana is topped up first).
    ActionError Cast(Match& m, int side, char const* cardId, EntityId target = 0, int choice = 0, int position = -1)
    {
        Mana(m, side);
        Ids const h = TA::SetHand(m, side, {cardId});
        return m.Apply(side, Play(h[0], target, choice, position));
    }

    void ToSide1(Match& m)
    {
        m.Apply(0, End());
        TA::SetMana(m, 1, 10, 10);
        TA::SetHand(m, 1, {});
    }
    void ToSide0(Match& m)
    {
        m.Apply(1, End());
        TA::SetMana(m, 0, 10, 10);
        TA::SetHand(m, 0, {});
    }

    bool Capped(Match const& m) { return m.DebugState().find("capped=1") != std::string::npos; }

    // Fireball (6 damage) on a minion by side.
    ActionError Kill(Match& m, int side, EntityId target) { return Cast(m, side, "CS2_029", target); }

    int Cost(Match const& m, EntityId handCard) { return TA::HandCost(m, handCard); }

    // ---- coverage -------------------------------------------------------------------------------

    void TestCoverage()
    {
        for (char const* id : kCards)
        {
            bool const ok = Db().Supported(id);
            if (!ok)
                std::printf("  not supported: %s\n", id);
            CHECK(ok);
        }
        for (char const* id : kTokens)
        {
            bool const ok = Db().Supported(id);
            if (!ok)
                std::printf("  not supported: %s\n", id);
            CHECK(ok);
        }
        // The definition flags the engine reads: Spectral Knight elusive, Maexxna poisonous, Battlecry / Deathrattle
        // mechanics (Weblord, Undertaker).
        CardDef const* knight = Db().Find("FP1_008");
        CHECK(knight && knight->elusive);
        CardDef const* maexxna = Db().Find("FP1_010");
        CHECK(maexxna && maexxna->poisonous);
        CHECK(Db().Find("FP1_016")->battlecry && !Db().Find("FP1_016")->deathrattle);
        CHECK(Db().Find("FP1_001")->deathrattle && !Db().Find("FP1_001")->battlecry);
        CHECK(!Db().Find("FP1_017")->battlecry); // the Weblord itself only mentions the word
        CHECK(Db().Find("FP1_005")->stealth);
        CHECK(Db().Find("FP1_024")->taunt && Db().Find("FP1_012")->taunt && Db().Find("FP1_009")->taunt);
    }

    // ---- Zombie Chow --------------------------------------------------------------------------------

    void TestZombieChow()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_001"});
        TA::SetHero(*m, 1, 20, 0);
        CHECK(Kill(*m, 0, b[0]) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 25); // 5 Health to the ENEMY hero
        CHECK_EQ(HP(*m, 0), 30);
        // Capped at the maximum.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 0, {"FP1_001"});
        TA::SetHero(*n, 1, 28, 0);
        CHECK(Kill(*n, 0, c[0]) == ActionError::None);
        CHECK_EQ(HP(*n, 1), 30);
        // The enemy's Chow heals MY hero.
        std::unique_ptr<Match> o = Fresh();
        Ids d = TA::SetBoard(*o, 1, {"FP1_001"});
        TA::SetHero(*o, 0, 10, 0);
        CHECK(Kill(*o, 0, d[0]) == ActionError::None);
        CHECK_EQ(HP(*o, 0), 15);
        // Auchenai Soulpriest on the Chow's side: the restore hurts the enemy hero instead.
        std::unique_ptr<Match> p = Fresh();
        Ids e = TA::SetBoard(*p, 0, {"FP1_001", "EX1_591"});
        CHECK(Kill(*p, 0, e[0]) == ActionError::None);
        CHECK_EQ(HP(*p, 1), 25);
        // Silenced: no Deathrattle.
        std::unique_ptr<Match> q = Fresh();
        Ids f = TA::SetBoard(*q, 0, {"FP1_001"});
        TA::SetHero(*q, 1, 20, 0);
        CHECK(Cast(*q, 0, "EX1_332", f[0]) == ActionError::None);
        CHECK(Kill(*q, 0, f[0]) == ActionError::None);
        CHECK_EQ(HP(*q, 1), 20);
    }

    // ---- Undertaker ---------------------------------------------------------------------------------

    void TestUndertaker()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_028"});
        MinionView u = MV(*m, b[0]);
        CHECK(u.attack == 1 && u.health == 2);
        // A minion without Deathrattle played from the hand: no gain.
        CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
        u = MV(*m, b[0]);
        CHECK(u.attack == 1 && u.health == 2);
        // Haunted Creeper played from the hand: +1/+1 (6024: both stats).
        CHECK(Cast(*m, 0, "FP1_002") == ActionError::None);
        u = MV(*m, b[0]);
        CHECK(u.attack == 2 && u.health == 3 && u.maxHealth == 3);
        // The Creeper dies: two Spectral Spiders have no Deathrattle, so nothing more.
        EntityId creeper = 0;
        for (EntityId id : TA::BoardIds(*m, 0))
            if (MV(*m, id).cardId == "FP1_002")
                creeper = id;
        CHECK(creeper != 0);
        CHECK(Kill(*m, 0, creeper) == ActionError::None);
        CHECK_EQ(CountOf(Board(*m, 0), "FP1_002t"), 2);
        u = MV(*m, b[0]);
        CHECK(u.attack == 2 && u.health == 3);
        // Nerubian Egg (a printed Deathrattle) from the hand: +1/+1 again.
        CHECK(Cast(*m, 0, "FP1_007") == ActionError::None);
        u = MV(*m, b[0]);
        CHECK(u.attack == 3 && u.health == 4);
        // Playing a Deathrattle minion on the ENEMY side does not count.
        ToSide1(*m);
        CHECK(Cast(*m, 1, "FP1_007") == ActionError::None);
        u = MV(*m, b[0]);
        CHECK(u.attack == 3 && u.health == 4);
        // Ancestral Spirit's resummon of a Deathrattle minion counts (the new copy is summoned).
        ToSide0(*m);
        Ids e = TA::SetBoard(*m, 0, {"FP1_028", "EX1_096"});
        CHECK(Cast(*m, 0, "CS2_038", e[1]) == ActionError::None);
        CHECK(Kill(*m, 0, e[1]) == ActionError::None); // Loot Hoarder dies and comes back: summoned, printed Deathrattle
        u = MV(*m, e[0]);
        CHECK(u.attack == 2 && u.health == 3);
        // Silenced Undertaker: no gain.
        Ids s = TA::SetBoard(*m, 0, {"FP1_028"});
        CHECK(Cast(*m, 0, "EX1_332", s[0]) == ActionError::None);
        CHECK(Cast(*m, 0, "FP1_007") == ActionError::None);
        u = MV(*m, s[0]);
        CHECK(u.attack == 1 && u.health == 2);
    }

    // The Undertaker of the OTHER player sees the minion Deathlord puts onto its board.
    void TestUndertakerDeathlord()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids a = TA::SetBoard(*m, 0, {"FP1_009"});
        Ids b = TA::SetBoard(*m, 1, {"FP1_028"});
        TA::SetDeck(*m, 1, {"FP1_007"});
        TA::SetMinion(*m, a[0], 2, 1, 8);
        CHECK(Kill(*m, 0, a[0]) == ActionError::None);
        CHECK(Board(*m, 1) == (Strs{"FP1_028", "FP1_007"}));
        MinionView const u = MV(*m, b[0]);
        CHECK(u.attack == 2 && u.health == 3);
    }

    // ---- Deathrattle summoners ----------------------------------------------------------------------

    void TestHauntedCreeper()
    {
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "FP1_002", "CS2_182"});
            CHECK(Kill(*m, 0, b[1]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_168", "FP1_002t", "FP1_002t", "CS2_182"})); // in the Creeper's place
            for (EntityId id : TA::BoardIds(*m, 0))
                if (MV(*m, id).cardId == "FP1_002t")
                    CHECK(MV(*m, id).attack == 1 && MV(*m, id).health == 1);
        }
        {
            // Only one slot free after the Creeper left: one Spider.
            std::unique_ptr<Match> m = Fresh();
            std::vector<std::string> cards(7, "CS2_168");
            cards[3] = "FP1_002";
            Ids b = TA::SetBoard(*m, 0, cards);
            CHECK(Kill(*m, 0, b[3]) == ActionError::None);
            CHECK_EQ(Board(*m, 0).size(), 7u);
            CHECK_EQ(CountOf(Board(*m, 0), "FP1_002t"), 1); // 6 minions left, one free slot: one Spider
        }
    }

    void TestNerubianEgg()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_168", "FP1_007", "CS2_182"});
        MinionView const egg = MV(*m, b[1]);
        CHECK(egg.attack == 0 && egg.health == 2);
        CHECK(Kill(*m, 0, b[1]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"CS2_168", "FP1_007t", "CS2_182"}));
        MinionView const n = MV(*m, TA::BoardIds(*m, 0)[1]);
        CHECK(n.attack == 4 && n.health == 4);
        // The Egg attacked and killed by a 2/1 on the enemy turn: the Nerubian still appears.
        std::unique_ptr<Match> o = Fresh();
        Ids e = TA::SetBoard(*o, 0, {"FP1_007"});
        ToSide1(*o);
        Ids a = TA::SetBoard(*o, 1, {"CS2_182"});
        TA::ReadyAll(*o);
        CHECK(o->Apply(1, Attack(a[0], e[0])) == ActionError::None);
        CHECK(Board(*o, 0) == Strs{"FP1_007t"});
    }

    void TestSludgeBelcher()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_168", "FP1_012", "CS2_168"});
        CHECK(MV(*m, b[1]).taunt && MV(*m, b[1]).attack == 3 && MV(*m, b[1]).health == 5);
        CHECK(Kill(*m, 0, b[1]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"CS2_168", "FP1_012t", "CS2_168"}));
        MinionView const slime = MV(*m, TA::BoardIds(*m, 0)[1]);
        CHECK(slime.taunt && slime.attack == 1 && slime.health == 2);
    }

    void TestUnstableGhoul()
    {
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_024", "CS2_168", "CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_182", "EX1_008"});
            CHECK(MV(*m, a[0]).taunt);
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            // 1 damage to ALL minions: both Murlocs (x/1) die, the Yetis are damaged, the Squire loses its shield.
            CHECK(Board(*m, 0) == Strs{"CS2_182"});
            CHECK(Board(*m, 1) == (Strs{"CS2_182", "EX1_008"}));
            CHECK_EQ(MV(*m, a[2]).health, 4);
            CHECK_EQ(MV(*m, e[1]).health, 4);
            CHECK(!MV(*m, e[2]).divineShield && MV(*m, e[2]).health == 1);
        }
        {
            // Two Ghouls, the second at 1 Health: the first one's Deathrattle kills it, its own Deathrattle follows.
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_024", "FP1_024", "CS2_182"});
            TA::SetMinion(*m, a[1], 1, 1, 3);
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_182"});
            CHECK_EQ(MV(*m, a[2]).health, 3); // two points of damage
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle), 2);
        }
    }

    void TestDancingSwords()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_029"});
        MatchView const before = m->ViewFor(0, m->LastEventSeq());
        CHECK(Kill(*m, 0, b[0]) == ActionError::None);
        MatchView const after = m->ViewFor(0, m->LastEventSeq());
        CHECK_EQ(after.other.handCount, before.other.handCount + 1); // the OPPONENT draws
        CHECK_EQ(after.other.deckCount, before.other.deckCount - 1);
        CHECK_EQ(after.self.handCount, 0);
        // The opponent's Swords make ME draw.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 1, {"FP1_029"});
        CHECK(Kill(*n, 0, c[0]) == ActionError::None);
        CHECK_EQ(Hand(*n, 0).size(), 1u);
        // Empty deck: fatigue.
        std::unique_ptr<Match> o = Fresh();
        Ids d = TA::SetBoard(*o, 0, {"FP1_029"});
        TA::SetDeck(*o, 1, {});
        CHECK(Kill(*o, 0, d[0]) == ActionError::None);
        CHECK_EQ(HP(*o, 1), 29);
    }

    void TestDarkCultist()
    {
        std::set<std::string> chosen;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids b = TA::SetBoard(*m, 0, {"FP1_023", "CS2_168", "CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Kill(*m, 0, b[0]) == ActionError::None);
            int buffed = 0;
            for (int i = 1; i <= 2; ++i)
            {
                MinionView const mv = MV(*m, b[static_cast<size_t>(i)]);
                int const base = i == 1 ? 1 : 5;
                if (mv.maxHealth == base + 3 && mv.health == base + 3)
                {
                    ++buffed;
                    chosen.insert(mv.cardId);
                }
                else
                    CHECK(mv.maxHealth == base);
            }
            CHECK_EQ(buffed, 1);
            CHECK_EQ(MV(*m, e[0]).maxHealth, 5); // never an enemy
        }
        CHECK_EQ(chosen.size(), 2u);
        // Nothing to give: no crash, nothing happens.
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_023"});
        CHECK(Kill(*m, 0, b[0]) == ActionError::None);
        CHECK(Board(*m, 0).empty());
    }

    void TestWailingSoul()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids a = TA::SetBoard(*m, 0, {"CS2_182", "CS1_042", "EX1_008"});
        TA::SetMinion(*m, a[0], 6, 8, 8); // a buffed Yeti
        Ids e = TA::SetBoard(*m, 1, {"CS1_042"});
        CHECK(MV(*m, a[1]).taunt && MV(*m, a[2]).divineShield);
        CHECK(Cast(*m, 0, "FP1_016") == ActionError::None);
        CHECK_EQ(CountEvents(*m, EventType::Silence), 3);
        MinionView y = MV(*m, a[0]);
        CHECK(y.silenced && y.attack == 4 && y.health == 5 && y.maxHealth == 5);
        CHECK(!MV(*m, a[1]).taunt && MV(*m, a[1]).silenced);
        CHECK(!MV(*m, a[2]).divineShield);
        // Wailing Soul itself and the enemy minion are untouched.
        EntityId soul = TA::BoardIds(*m, 0).back();
        CHECK(MV(*m, soul).cardId == "FP1_016" && !MV(*m, soul).silenced && MV(*m, soul).attack == 3 && MV(*m, soul).health == 5);
        CHECK(MV(*m, e[0]).taunt && !MV(*m, e[0]).silenced);
        // Alone on the board: nothing to silence.
        std::unique_ptr<Match> n = Fresh();
        CHECK(Cast(*n, 0, "FP1_016") == ActionError::None);
        CHECK_EQ(CountEvents(*n, EventType::Silence), 0);
    }

    // ---- Stealth and start-of-turn minions -------------------------------------------------------------

    void TestShade()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_005"});
        MinionView s = MV(*m, b[0]);
        CHECK(s.stealth && s.attack == 2 && s.health == 2);
        ToSide1(*m);
        s = MV(*m, b[0]);
        CHECK(s.attack == 2 && s.health == 2); // not at the start of the ENEMY's turn
        // An enemy spell cannot target it (Stealth).
        CHECK(Cast(*m, 1, "CS2_029", b[0]) == ActionError::Target);
        ToSide0(*m);
        s = MV(*m, b[0]);
        CHECK(s.attack == 3 && s.health == 3 && s.maxHealth == 3);
        CHECK(s.stealth);
        ToSide1(*m);
        ToSide0(*m);
        s = MV(*m, b[0]);
        CHECK(s.attack == 4 && s.health == 4);
        // Silenced: back to 2/2 and it stops growing.
        CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
        s = MV(*m, b[0]);
        CHECK(s.attack == 2 && s.health == 2 && !s.stealth);
        ToSide1(*m);
        ToSide0(*m);
        s = MV(*m, b[0]);
        CHECK(s.attack == 2 && s.health == 2);
    }

    void TestStoneskinGargoyle()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_027"});
        TA::SetMinion(*m, b[0], 1, 1, 4); // 1 of 4 Health left
        ToSide1(*m);
        CHECK_EQ(MV(*m, b[0]).health, 1); // not at the enemy's turn start
        ToSide0(*m);
        CHECK_EQ(MV(*m, b[0]).health, 4);
        uint32_t const since = m->LastEventSeq();
        ToSide1(*m);
        ToSide0(*m);
        CHECK_EQ(CountEvents(*m, EventType::Heal, since), 0); // undamaged: nothing to restore
        // Silenced: no more healing.
        TA::SetMinion(*m, b[0], 1, 1, 4);
        CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
        ToSide1(*m);
        ToSide0(*m);
        CHECK_EQ(MV(*m, b[0]).health, 1);
        // A buffed Gargoyle is restored to its buffed maximum.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 0, {"FP1_027"});
        TA::SetMinion(*n, c[0], 3, 2, 7);
        ToSide1(*n);
        ToSide0(*n);
        CHECK_EQ(MV(*n, c[0]).health, 7);
    }

    // ---- Death's Bite ------------------------------------------------------------------------------------

    void TestDeathsBite()
    {
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR", "MAGE");
            TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            TA::SetBoard(*m, 1, {"CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "FP1_021") == ActionError::None);
            HeroView h = HV(*m, 0);
            CHECK(h.weaponCardId == "FP1_021" && h.weaponAttack == 4 && h.weaponDurability == 2);
            TA::ReadyAll(*m);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle), 0);
            TA::ReadyAll(*m);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            // Durability 0: the weapon breaks and its Deathrattle deals 1 damage to ALL minions.
            CHECK(HV(*m, 0).weaponId == 0);
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle), 1);
            CHECK(Board(*m, 0) == Strs{"CS2_182"});
            CHECK(Board(*m, 1) == Strs{"CS2_182"});
            CHECK_EQ(MV(*m, TA::BoardIds(*m, 0)[0]).health, 4);
            CHECK_EQ(MV(*m, TA::BoardIds(*m, 1)[0]).health, 4);
            // The event names the weapon.
            bool named = false;
            for (Event const& e : m->ViewFor(0, 0).events)
                if (e.type == EventType::Deathrattle && e.cardId == "FP1_021")
                    named = true;
            CHECK(named);
        }
        {
            // Replaced by another weapon: the Deathrattle triggers.
            std::unique_ptr<Match> m = Fresh("WARRIOR", "MAGE");
            TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            TA::SetBoard(*m, 1, {"CS2_168"});
            TA::SetWeapon(*m, 0, "FP1_021");
            CHECK(Cast(*m, 0, "CS2_106") == ActionError::None);
            CHECK(HV(*m, 0).weaponCardId == "CS2_106");
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle), 1);
            CHECK(Board(*m, 0) == Strs{"CS2_182"});
            CHECK(Board(*m, 1).empty());
        }
        {
            // Destroyed by Acidic Swamp Ooze: the Deathrattle also hits the Ooze that is on the board by then.
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
            TA::SetWeapon(*m, 1, "FP1_021");
            TA::SetBoard(*m, 1, {"CS2_168"});
            CHECK(Cast(*m, 0, "EX1_066") == ActionError::None);
            CHECK(HV(*m, 1).weaponId == 0);
            CHECK(Board(*m, 1).empty());
            EntityId ooze = TA::BoardIds(*m, 0)[0];
            CHECK_EQ(MV(*m, ooze).health, 1);
        }
    }

    // ---- Anub'ar Ambusher, Webspinner, Voidcaller (Deathrattles that move cards) -----------------------------

    void TestAnubarAmbusher()
    {
        std::set<std::string> returned;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "FP1_026", "CS2_182"});
            CHECK(Kill(*m, 0, b[1]) == ActionError::None);
            Strs const hand = Hand(*m, 0);
            CHECK_EQ(hand.size(), 1u);
            CHECK_EQ(Board(*m, 0).size(), 1u);
            if (hand.size() == 1)
                returned.insert(hand[0]);
        }
        CHECK(returned == (std::set<std::string>{"CS2_168", "CS2_182"}));
        // Alone: nothing to return.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"FP1_026"});
            CHECK(Kill(*m, 0, b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty() && Hand(*m, 0).empty());
        }
        // Never an enemy minion.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"FP1_026"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Kill(*m, 0, b[0]) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"CS2_182"});
            CHECK(MV(*m, e[0]).id == e[0]);
        }
        // A full hand: the minion is destroyed instead.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"FP1_026", "CS2_182"});
            ToSide1(*m);
            TA::SetHand(*m, 0, Strs(10, "CS2_168"));
            CHECK(Kill(*m, 1, b[0]) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 10u);
            CHECK(Board(*m, 0).empty());
        }
    }

    void TestWebspinner()
    {
        // The pool is the dataset's entourage of Webspinner: 28 collectible Beasts (build 6024).
        std::set<std::string> seen;
        for (uint32_t seed = 1; seed <= 150; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids b = TA::SetBoard(*m, 0, {"FP1_011"});
            CHECK(Kill(*m, 0, b[0]) == ActionError::None);
            Strs const hand = Hand(*m, 0);
            CHECK_EQ(hand.size(), 1u);
            if (hand.size() != 1)
                continue;
            CardDef const* d = Db().Find(hand[0]);
            CHECK(d && d->race == "BEAST" && d->type == CardType::Minion && Db().Supported(hand[0]));
            seen.insert(hand[0]);
        }
        CHECK(seen.size() >= 22);
        CHECK(seen.count("FP1_010") || seen.count("FP1_002") || seen.count("FP1_011")); // the Naxxramas Beasts are in the pool
        CHECK(!seen.count("hexfrog") && !seen.count("EX1_534t")); // tokens are not
        // A full hand: nothing is added.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"FP1_011"});
            ToSide1(*m);
            TA::SetHand(*m, 0, Strs(10, "CS2_168"));
            CHECK(Kill(*m, 1, b[0]) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 10u);
        }
        // Hidden information: the other side sees a generated card without its identity.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"FP1_011"});
            uint32_t const since = m->LastEventSeq();
            CHECK(Kill(*m, 0, b[0]) == ActionError::None);
            bool own = false, other = false;
            for (Event const& e : m->ViewFor(0, since).events)
                if (e.type == EventType::Generate && !e.cardId.empty())
                    own = true;
            for (Event const& e : m->ViewFor(1, since).events)
                if (e.type == EventType::Generate)
                    other = other || e.cardId.empty();
            CHECK(own && other);
        }
    }

    void TestVoidcaller()
    {
        std::set<std::string> demons;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK", "WARRIOR", seed);
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "FP1_022", "CS2_168"});
            Mana(*m);
            Ids h = TA::SetHand(*m, 0, {"CS2_029", "CS2_065", "CS2_182", "EX1_306"});
            CHECK(m->Apply(0, Play(h[0], b[1])) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK_EQ(bd.size(), 3u);
            if (bd.size() == 3)
            {
                CHECK(bd[0] == "CS2_168" && bd[2] == "CS2_168"); // in the Voidcaller's slot
                demons.insert(bd[1]);
            }
            // No Battlecry: a Succubus put into the battlefield discards nothing; the other cards stay.
            Strs const hand = Hand(*m, 0);
            CHECK_EQ(hand.size(), 2u);
            CHECK(Has(hand, "CS2_182"));
            // The card kept its entity.
            bool kept = false;
            for (EntityId id : TA::BoardIds(*m, 0))
                kept = kept || id == h[1] || id == h[3];
            CHECK(kept);
        }
        CHECK(demons == (std::set<std::string>{"CS2_065", "EX1_306"}));
        // No Demon in the hand: nothing happens.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"FP1_022"});
            Mana(*m);
            Ids h = TA::SetHand(*m, 0, {"CS2_029", "CS2_182", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0], b[0])) == ActionError::None);
            CHECK(Board(*m, 0).empty());
            CHECK_EQ(Hand(*m, 0).size(), 2u);
        }
        // Another Voidcaller in the hand is a Demon too (put into play without Deathrattle trigger).
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"FP1_022"});
            Mana(*m);
            Ids h = TA::SetHand(*m, 0, {"CS2_029", "FP1_022"});
            CHECK(m->Apply(0, Play(h[0], b[0])) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"FP1_022"});
            CHECK(Hand(*m, 0).empty());
        }
        // No room: the Deathrattle of an older Creeper fills the board first, the Demon stays in the hand.
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARLOCK");
            Ids b = TA::SetBoard(*m, 1, {"FP1_002", "FP1_022", "CS2_200", "CS2_200", "CS2_200", "CS2_200", "CS2_200"});
            TA::SetMinion(*m, b[0], 1, 1, 2);
            TA::SetMinion(*m, b[1], 3, 1, 4);
            TA::SetHand(*m, 1, {"CS2_065"});
            Mana(*m);
            CHECK(Cast(*m, 0, "CS2_025") == ActionError::None); // 1 damage to all enemy minions
            TA::SetHand(*m, 1, {"CS2_065"});
            // (Cast replaced side 0's hand only; side 1 kept the Voidwalker set before the cast.)
        }
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARLOCK");
            Ids b = TA::SetBoard(*m, 1, {"FP1_002", "FP1_022", "CS2_200", "CS2_200", "CS2_200", "CS2_200", "CS2_200"});
            TA::SetMinion(*m, b[0], 1, 1, 2);
            TA::SetMinion(*m, b[1], 3, 1, 4);
            TA::SetHand(*m, 1, {"CS2_065"});
            CHECK(Cast(*m, 0, "CS2_025") == ActionError::None);
            CHECK_EQ(CountOf(Board(*m, 1), "FP1_002t"), 2);
            CHECK_EQ(Board(*m, 1).size(), 7u);
            CHECK(!Has(Board(*m, 1), "CS2_065"));
            CHECK(Has(Hand(*m, 1), "CS2_065"));
        }
    }

    // ---- Deathlord and Mad Scientist ---------------------------------------------------------------------------

    void TestDeathlord()
    {
        std::set<std::string> got;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids a = TA::SetBoard(*m, 0, {"FP1_009"});
            TA::SetBoard(*m, 1, {"CS2_168"});
            TA::SetDeck(*m, 1, {"CS2_029", "CS2_182", "FP1_007", "CS2_029"});
            MinionView const dl = MV(*m, a[0]);
            CHECK(dl.taunt && dl.attack == 2 && dl.health == 8);
            TA::SetMinion(*m, a[0], 2, 1, 8);
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            // The OPPONENT gets a random MINION of their deck on THEIR board (never a spell); the deck shrinks.
            Strs const e = Board(*m, 1);
            CHECK_EQ(e.size(), 2u);
            if (e.size() == 2)
                got.insert(e[1]);
            CHECK_EQ(TA::DeckCardIds(*m, 1).size(), 3u);
            CHECK_EQ(CountOf(TA::DeckCardIds(*m, 1), "CS2_029"), 2);
            CHECK(Board(*m, 0).empty());
        }
        CHECK(got == (std::set<std::string>{"CS2_182", "FP1_007"}));
        // No minion in the deck: nothing.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_009"});
            TA::SetDeck(*m, 1, {"CS2_029", "CS2_029"});
            TA::SetMinion(*m, a[0], 2, 1, 8);
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK(Board(*m, 1).empty());
            CHECK_EQ(TA::DeckCardIds(*m, 1).size(), 2u);
        }
        // A full enemy board: nothing happens and the card stays in the deck.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_009"});
            TA::SetBoard(*m, 1, Strs(7, "CS2_168"));
            TA::SetDeck(*m, 1, {"CS2_182"});
            TA::SetMinion(*m, a[0], 2, 1, 8);
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK_EQ(Board(*m, 1).size(), 7u);
            CHECK_EQ(TA::DeckCardIds(*m, 1).size(), 1u);
        }
        // Deathlord of side 1 dying: side 0 puts a minion into play.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 1, {"FP1_009"});
            TA::SetDeck(*m, 0, {"CS2_182"});
            TA::SetMinion(*m, a[0], 2, 1, 8);
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_182"});
            CHECK(Board(*m, 1).empty());
        }
    }

    void TestMadScientist()
    {
        // A Secret of the deck goes into play; never a spell that is no Secret.
        std::set<std::string> got;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids a = TA::SetBoard(*m, 0, {"FP1_004"});
            TA::SetDeck(*m, 0, {"CS2_029", "EX1_609", "CS2_182", "EX1_610"});
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            Strs const sec = TA::SecretCardIds(*m, 0);
            CHECK_EQ(sec.size(), 1u);
            if (sec.size() == 1)
                got.insert(sec[0]);
            CHECK_EQ(TA::DeckCardIds(*m, 0).size(), 3u);
        }
        CHECK(got == (std::set<std::string>{"EX1_609", "EX1_610"}));
        // The same Secret already in play: not put in again.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_004"});
            TA::SetSecrets(*m, 0, {"EX1_609"});
            TA::SetDeck(*m, 0, {"EX1_609", "CS2_182"});
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_609"});
            CHECK_EQ(TA::DeckCardIds(*m, 0).size(), 2u);
            // A different one is.
            std::unique_ptr<Match> n = Fresh();
            Ids b = TA::SetBoard(*n, 0, {"FP1_004"});
            TA::SetSecrets(*n, 0, {"EX1_609"});
            TA::SetDeck(*n, 0, {"EX1_609", "EX1_610"});
            CHECK(Kill(*n, 0, b[0]) == ActionError::None);
            CHECK(TA::SecretCardIds(*n, 0) == (Strs{"EX1_609", "EX1_610"}));
            CHECK(TA::DeckCardIds(*n, 0) == Strs{"EX1_609"});
        }
        // The Secret limit (5) holds.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_004"});
            TA::SetSecrets(*m, 0, {"EX1_609", "EX1_610", "EX1_611", "EX1_287", "EX1_289"});
            TA::SetDeck(*m, 0, {"EX1_294"});
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK_EQ(TA::SecretCardIds(*m, 0).size(), 5u);
            CHECK_EQ(TA::DeckCardIds(*m, 0).size(), 1u);
        }
        // No Secret in the deck.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_004"});
            TA::SetDeck(*m, 0, {"CS2_029", "CS2_182"});
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).empty());
        }
        // Hidden information: the other side sees THAT a Secret came into play, not which.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_004"});
            TA::SetDeck(*m, 0, {"EX1_609"});
            uint32_t const since = m->LastEventSeq();
            CHECK(Kill(*m, 0, a[0]) == ActionError::None);
            bool own = false, other = false;
            for (Event const& e : m->ViewFor(0, since).events)
                if (e.type == EventType::Secret && e.cardId == "EX1_609")
                    own = true;
            for (Event const& e : m->ViewFor(1, since).events)
                if (e.type == EventType::Secret)
                    other = other || e.cardId.empty();
            CHECK(own && other);
            MatchView const v1 = m->ViewFor(1, 0);
            CHECK_EQ(v1.other.secrets.size(), 1u);
            CHECK(v1.other.secrets[0].cardId.empty());
        }
        // The Secret works at once when the Scientist dies on the enemy's turn: Snipe kills the next minion played.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_004"});
            TA::SetDeck(*m, 0, {"EX1_609"});
            ToSide1(*m);
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            TA::ReadyAll(*m);
            CHECK(m->Apply(1, Attack(e[0], a[0])) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_609"});
            CHECK(Cast(*m, 1, "CS2_168") == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).empty());
            CHECK(Board(*m, 1) == Strs{"CS2_182"}); // the Murloc Raider died to Snipe
            CHECK_EQ(MV(*m, e[0]).health, 5 - 2);   // the Scientist's 2 Attack hurt the Yeti
        }
    }

    // ---- Reincarnate and Poison Seeds ----------------------------------------------------------------------------

    void TestReincarnate()
    {
        // A damaged and buffed minion returns as a fresh copy with full Health, at the right end of its board.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            TA::SetMinion(*m, b[0], 9, 2, 9);
            CHECK(Cast(*m, 0, "FP1_025", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_168", "CS2_182"}));
            MinionView const y = MV(*m, TA::BoardIds(*m, 0)[1]);
            CHECK(y.attack == 4 && y.health == 5 && y.maxHealth == 5 && y.id != b[0]);
            CHECK(!y.canAttack); // summoned: exhausted
        }
        // A Deathrattle minion: the Deathrattle triggers on the destroy, then the minion returns.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "FP1_012"});
            CHECK(Cast(*m, 0, "FP1_025", b[1]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_168", "FP1_012t", "FP1_012"}));
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle), 1);
        }
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"EX1_096"});
            CHECK(Cast(*m, 0, "FP1_025", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_096"});
            CHECK_EQ(Hand(*m, 0).size(), 1u); // Loot Hoarder drew
        }
        // An enemy minion returns to the ENEMY.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182", "FP1_007"});
            CHECK(Cast(*m, 0, "FP1_025", e[1]) == ActionError::None);
            // The Egg dies (Nerubian), then the Egg returns: Yeti, Nerubian, Egg.
            CHECK(Board(*m, 1) == (Strs{"CS2_182", "FP1_007t", "FP1_007"}));
            CHECK(Board(*m, 0).empty());
        }
        // Divine Shield comes back.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"EX1_008"});
            CHECK(Cast(*m, 0, "CS2_189", b[0]) == ActionError::None); // Elven Archer's Battlecry pops the shield
            CHECK(!MV(*m, b[0]).divineShield);
            CHECK(Cast(*m, 0, "FP1_025", b[0]) == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0).back()).divineShield);
        }
        // A full board: the Deathrattle fills the freed slot and the copy cannot return.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            std::vector<std::string> cards(7, "CS2_168");
            cards[2] = "FP1_012";
            Ids b = TA::SetBoard(*m, 0, cards);
            CHECK(Cast(*m, 0, "FP1_025", b[2]) == ActionError::None);
            CHECK_EQ(Board(*m, 0).size(), 7u);
            CHECK(Has(Board(*m, 0), "FP1_012t") && !Has(Board(*m, 0), "FP1_012"));
        }
        // Targets: no hero, no Stealth enemy, no Spectral Knight; the own minion is fine.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"FP1_008"});
            Ids e = TA::SetBoard(*m, 1, {"EX1_522"}); // Patient Assassin (Stealth)
            CHECK(Cast(*m, 0, "FP1_025", Hero(*m, 1)) == ActionError::Target);
            CHECK(Cast(*m, 0, "FP1_025", e[0]) == ActionError::Target);
            CHECK(Cast(*m, 0, "FP1_025", b[0]) == ActionError::Target);
            CHECK(Cast(*m, 0, "FP1_025") == ActionError::Target);
        }
        // Reincarnate on a minion the Undertaker owner controls: the copy counts as a summon.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"FP1_028", "FP1_007"});
            CHECK(Cast(*m, 0, "FP1_025", b[1]) == ActionError::None);
            // The Nerubian (no Deathrattle) does not count, the returning Egg does.
            CHECK(MV(*m, b[0]).attack == 2 && MV(*m, b[0]).health == 3);
        }
        // Legal-list agreement: exactly the valid minions.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetBoard(*m, 0, {"CS2_168", "FP1_008"});
            TA::SetBoard(*m, 1, {"CS2_182", "EX1_522"});
            Mana(*m);
            TA::SetHand(*m, 0, {"FP1_025"});
            int n = 0;
            for (Action const& a : m->LegalActions(0))
                if (a.type == ActionType::Play)
                    ++n;
            CHECK_EQ(n, 2);
        }
    }

    void TestPoisonSeeds()
    {
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            TA::SetBoard(*m, 1, {"CS2_182", "CS2_168", "CS2_200"});
            CHECK(Cast(*m, 0, "FP1_019") == ActionError::None);
            // As many Treants as each side lost.
            CHECK(Board(*m, 0) == (Strs{"FP1_019t", "FP1_019t"}));
            CHECK(Board(*m, 1) == (Strs{"FP1_019t", "FP1_019t", "FP1_019t"}));
            for (int s = 0; s < 2; ++s)
                for (EntityId id : TA::BoardIds(*m, s))
                    CHECK(MV(*m, id).attack == 2 && MV(*m, id).health == 2);
        }
        {
            // Divine Shield does not save a minion from "destroy".
            std::unique_ptr<Match> m = Fresh("DRUID");
            TA::SetBoard(*m, 1, {"EX1_008"});
            CHECK(Cast(*m, 0, "FP1_019") == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"FP1_019t"});
        }
        {
            // Empty boards: nothing happens.
            std::unique_ptr<Match> m = Fresh("DRUID");
            CHECK(Cast(*m, 0, "FP1_019") == ActionError::None);
            CHECK(Board(*m, 0).empty() && Board(*m, 1).empty());
        }
        // Deathrattles resolve BEFORE the Treants: Unstable Ghoul's damage cannot hit them (either order of the two
        // deathrattles), and a Creeper's Spiders come first on the board.
        for (int order = 0; order < 2; ++order)
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            if (order == 0)
            {
                TA::SetBoard(*m, 0, {"FP1_024"});
                TA::SetBoard(*m, 1, {"FP1_002", "CS2_168"});
            }
            else
            {
                TA::SetBoard(*m, 1, {"FP1_002", "CS2_168"});
                Ids a = TA::SetBoard(*m, 0, {"FP1_024"});
                (void)a;
            }
            // (SetBoard of the second call makes the later side newer.) Ghoul and Creeper both die.
            CHECK(Cast(*m, 0, "FP1_019") == ActionError::None);
            CHECK_EQ(CountOf(Board(*m, 0), "FP1_019t"), 1);
            CHECK_EQ(CountOf(Board(*m, 1), "FP1_019t"), 2);
            for (int s = 0; s < 2; ++s)
                for (EntityId id : TA::BoardIds(*m, s))
                    if (MV(*m, id).cardId == "FP1_019t")
                        CHECK(MV(*m, id).health == 2);
            // The Spiders of the Creeper stand left of the Treants when the Ghoul died first (they survived).
            if (order == 0)
            {
                Strs const e = Board(*m, 1);
                CHECK(e.size() == 4 && e[0] == "FP1_002t" && e[1] == "FP1_002t" && e[2] == "FP1_019t");
            }
        }
        {
            // A board that the Deathrattles fill: 7 minions with a Creeper -> 2 Spiders and only 5 Treants.
            std::unique_ptr<Match> m = Fresh("DRUID");
            std::vector<std::string> cards(7, "CS2_168");
            cards[3] = "FP1_002";
            TA::SetBoard(*m, 0, cards);
            CHECK(Cast(*m, 0, "FP1_019") == ActionError::None);
            CHECK_EQ(Board(*m, 0).size(), 7u);
            CHECK_EQ(CountOf(Board(*m, 0), "FP1_002t"), 2);
            CHECK_EQ(CountOf(Board(*m, 0), "FP1_019t"), 5);
        }
        {
            // Undertaker is destroyed too; a Treant is not a Deathrattle minion.
            std::unique_ptr<Match> m = Fresh("DRUID");
            TA::SetBoard(*m, 0, {"FP1_028", "FP1_007"});
            CHECK(Cast(*m, 0, "FP1_019") == ActionError::None);
            // The Egg's Nerubian is summoned by the Deathrattle (the Undertaker is already dead), then 2 Treants.
            CHECK(Board(*m, 0) == (Strs{"FP1_007t", "FP1_019t", "FP1_019t"}));
        }
    }

    // ---- Spectral Knight, Maexxna ----------------------------------------------------------------------------------

    void TestSpectralKnight()
    {
        std::unique_ptr<Match> m = Fresh("MAGE", "MAGE");
        Ids k = TA::SetBoard(*m, 1, {"FP1_008"});
        MinionView const mv = MV(*m, k[0]);
        CHECK(mv.attack == 4 && mv.health == 6);
        // Not by a spell, not by a Hero Power.
        CHECK(Cast(*m, 0, "CS2_029", k[0]) == ActionError::Target);
        CHECK(m->Apply(0, Power(*m, 0, k[0])) == ActionError::Target);
        // Area damage still hits it (not a target), battlecries may target it, attacks too.
        CHECK(Cast(*m, 0, "CS2_032") == ActionError::None); // Flamestrike
        CHECK_EQ(MV(*m, k[0]).health, 2);
        CHECK(Cast(*m, 0, "CS2_189", k[0]) == ActionError::None); // Elven Archer's Battlecry
        CHECK_EQ(MV(*m, k[0]).health, 1);
        // Silence (Ironbeak Owl's Battlecry) makes it targetable again.
        CHECK(Cast(*m, 0, "CS2_203", k[0]) == ActionError::None);
        CHECK(Cast(*m, 0, "CS2_029", k[0]) == ActionError::None);
        CHECK(Board(*m, 1).empty());
        // Own spells cannot target it either.
        std::unique_ptr<Match> n = Fresh("PRIEST");
        Ids o = TA::SetBoard(*n, 0, {"FP1_008"});
        CHECK(Cast(*n, 0, "CS2_004", o[0]) == ActionError::Target); // Power Word: Shield
        // It can attack and be attacked.
        TA::ReadyAll(*n);
        Ids e = TA::SetBoard(*n, 1, {"CS2_168"});
        CHECK(n->Apply(0, Attack(o[0], e[0])) == ActionError::None);
        CHECK(Board(*n, 1).empty());
    }

    void TestMaexxna()
    {
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_010"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            CHECK(MV(*m, a[0]).poisonous && MV(*m, a[0]).attack == 2 && MV(*m, a[0]).health == 8);
            // Attacking an Ogre (7 Health): destroyed by 2 damage.
            CHECK(m->Apply(0, Attack(a[0], e[0])) == ActionError::None);
            CHECK(Board(*m, 1).empty());
            CHECK_EQ(MV(*m, a[0]).health, 2); // the Ogre hit back for 6
        }
        {
            // Divine Shield: the shield absorbs the hit, no damage is dealt, so nothing is destroyed.
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_010"});
            Ids e = TA::SetBoard(*m, 1, {"EX1_008"});
            CHECK(m->Apply(0, Attack(a[0], e[0])) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"EX1_008"});
            CHECK(!MV(*m, e[0]).divineShield && MV(*m, e[0]).health == 1);
            CHECK_EQ(MV(*m, a[0]).health, 7); // the Squire hit back for 1
            // Next turn the second hit kills it either way; a bigger shielded minion survives the pop only.
        }
        {
            // Defending: the damage Maexxna deals back poisons the attacker.
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_010"});
            ToSide1(*m);
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::ReadyAll(*m);
            CHECK(m->Apply(1, Attack(e[0], a[0])) == ActionError::None);
            CHECK(Board(*m, 1).empty());
            CHECK_EQ(MV(*m, a[0]).health, 2);
        }
        {
            // Not against heroes (just damage), and not once silenced.
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"FP1_010"});
            CHECK(m->Apply(0, Attack(a[0], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            std::unique_ptr<Match> n = Fresh();
            Ids b = TA::SetBoard(*n, 0, {"FP1_010"});
            Ids e = TA::SetBoard(*n, 1, {"CS2_200"});
            CHECK(Cast(*n, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(!MV(*n, b[0]).poisonous);
            TA::ReadyAll(*n);
            CHECK(n->Apply(0, Attack(b[0], e[0])) == ActionError::None);
            CHECK(Board(*n, 1) == Strs{"CS2_200"});
        }
    }

    // ---- Nerub'ar Weblord and Loatheb: costs --------------------------------------------------------------------------

    void TestWeblord()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_017"});
        Ids h0 = TA::SetHand(*m, 0, {"CS2_042", "CS2_182", "CS2_029", "FP1_017", "FP1_016"});
        Ids h1 = TA::SetHand(*m, 1, {"CS2_042", "CS2_182", "CS2_029", "EX1_096"});
        // Battlecry minions cost (2) more in BOTH hands; minions without Battlecry, spells and the Weblord itself do not.
        CHECK_EQ(Cost(*m, h0[0]), 8);
        CHECK_EQ(Cost(*m, h0[1]), 4);
        CHECK_EQ(Cost(*m, h0[2]), 4);
        CHECK_EQ(Cost(*m, h0[3]), 2);
        CHECK_EQ(Cost(*m, h0[4]), 6); // Wailing Soul 4 + 2
        CHECK_EQ(Cost(*m, h1[0]), 8);
        CHECK_EQ(Cost(*m, h1[1]), 4);
        CHECK_EQ(Cost(*m, h1[3]), 2); // a Deathrattle minion is no Battlecry minion
        // The shown cost (the client's cost overlay) is the taxed one.
        MatchView const v0 = m->ViewFor(0, m->LastEventSeq());
        CHECK_EQ(v0.self.hand[0].cost, 8);
        // Payment: 7 mana is not enough for the Fire Elemental, 8 is.
        TA::SetMana(*m, 0, 7, 7);
        CHECK(m->Apply(0, Play(h0[0], Hero(*m, 1))) == ActionError::Mana);
        TA::SetMana(*m, 0, 8, 8);
        CHECK(m->Apply(0, Play(h0[0], Hero(*m, 1))) == ActionError::None);
        CHECK_EQ(HV(*m, 0).mana, 0);
        // A second Weblord: +4; Mana Wraith on top: +1.
        TA::SetBoard(*m, 0, {"FP1_017"});
        Ids h = TA::SetHand(*m, 0, {"CS2_042"});
        Ids w = TA::SetBoard(*m, 1, {"FP1_017", "EX1_616"});
        CHECK_EQ(Cost(*m, h[0]), 6 + 2 + 2 + 1);
        // Silenced Weblord: its tax is gone.
        TA::SetBoard(*m, 0, {"FP1_017"});
        TA::SetBoard(*m, 1, {});
        Ids silencer = TA::SetHand(*m, 0, {"EX1_332", "CS2_042"});
        CHECK_EQ(Cost(*m, silencer[1]), 8);
        CHECK(m->Apply(0, Play(silencer[0], TA::BoardIds(*m, 0)[0])) == ActionError::None);
        CHECK_EQ(Cost(*m, TA::HandIds(*m, 0)[0]), 6);
        // The Weblord leaving play ends it.
        Ids b2 = TA::SetBoard(*m, 0, {"FP1_017"});
        Ids h2 = TA::SetHand(*m, 0, {"CS2_042", "CS2_029"});
        CHECK_EQ(Cost(*m, h2[0]), 8);
        Mana(*m);
        CHECK(m->Apply(0, Play(h2[1], b2[0])) == ActionError::None);
        CHECK_EQ(Cost(*m, h2[0]), 6);
        (void)b;
        (void)w;
    }

    void TestLoatheb()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids mine = TA::SetHand(*m, 0, {"FP1_030", "CS2_029", "CS2_182"});
        Ids theirs = TA::SetHand(*m, 1, {"CS2_029", "CS2_182", "CS2_042"});
        CHECK_EQ(Cost(*m, theirs[0]), 4);
        CHECK(m->Apply(0, Play(mine[0])) == ActionError::None);
        CHECK(Board(*m, 0) == Strs{"FP1_030"});
        // Not yet: it is still MY turn.
        CHECK_EQ(Cost(*m, theirs[0]), 4);
        CHECK_EQ(m->ViewFor(0, 0).other.spellTaxNext, 5);
        CHECK_EQ(m->ViewFor(1, 0).self.spellTaxNext, 5);
        CHECK_EQ(Cost(*m, mine[1]), 4); // my own spells are not taxed
        // The enemy's next turn: spells cost 5 more, minions are not affected.
        m->Apply(0, End());
        CHECK_EQ(Cost(*m, theirs[0]), 9);
        CHECK_EQ(Cost(*m, theirs[1]), 4);
        CHECK_EQ(Cost(*m, theirs[2]), 6);
        CHECK_EQ(m->ViewFor(1, 0).self.spellTax, 5);
        CHECK_EQ(m->ViewFor(1, 0).self.hand[0].cost, 9);
        // A spell drawn during that turn is taxed too; with 8 mana the Fireball is out of reach, with 9 it is not.
        TA::SetMana(*m, 1, 8, 8);
        CHECK(m->Apply(1, Play(theirs[0], Hero(*m, 0))) == ActionError::Mana);
        TA::SetMana(*m, 1, 9, 9);
        CHECK(m->Apply(1, Play(theirs[0], Hero(*m, 0))) == ActionError::None);
        CHECK_EQ(HP(*m, 0), 24);
        // It ends with that turn.
        TA::SetHand(*m, 1, {"CS2_029"});
        m->Apply(1, End());
        CHECK_EQ(m->ViewFor(1, 0).self.spellTax, 0);
        Ids again = TA::SetHand(*m, 1, {"CS2_029"});
        CHECK_EQ(Cost(*m, again[0]), 4);
        m->Apply(0, End());
        CHECK_EQ(Cost(*m, TA::HandIds(*m, 1)[0]), 4); // the NEXT turn of the enemy after that is untaxed
        // Two Loathebs add up.
        std::unique_ptr<Match> n = Fresh();
        CHECK(Cast(*n, 0, "FP1_030") == ActionError::None);
        CHECK(Cast(*n, 0, "FP1_030") == ActionError::None);
        n->Apply(0, End());
        Ids t = TA::SetHand(*n, 1, {"CS2_029"});
        CHECK_EQ(Cost(*n, t[0]), 14);
    }

    // Millhouse Manastorm sets the cost to 0, Loatheb adds 5: the later of the two effects decides.
    void TestLoathebAndMillhouse()
    {
        for (int millhouseFirst = 0; millhouseFirst < 2; ++millhouseFirst)
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, millhouseFirst ? "NEW1_029" : "FP1_030") == ActionError::None);
            CHECK(Cast(*m, 0, millhouseFirst ? "FP1_030" : "NEW1_029") == ActionError::None);
            m->Apply(0, End());
            Ids t = TA::SetHand(*m, 1, {"CS2_029", "CS2_182"});
            CHECK_EQ(Cost(*m, t[0]), millhouseFirst ? 5 : 0);
            CHECK_EQ(Cost(*m, t[1]), 4);
        }
    }

    // ==================================================================================================================
    // Wave 2: Avenge, Duplicate, Echoing Ooze, Baron Rivendare, Stalagg, Feugen, Kel'Thuzad
    // ==================================================================================================================

    // 10 damage: kills anything of the dataset that these tests put on a board.
    ActionError Smash(Match& m, int side, EntityId target) { return Cast(m, side, "EX1_279", target); }

    // ---- Avenge ---------------------------------------------------------------------------------------------------

    void TestAvenge()
    {
        // A friendly minion dies on the enemy turn and another one lives: the receiver gets +3/+2, the secret is spent.
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_182"});
        TA::SetSecrets(*m, 0, {"FP1_020"});
        ToSide1(*m);
        CHECK(m->ViewFor(1, 0).other.secrets.size() == 1 && m->ViewFor(1, 0).other.secrets[0].cardId.empty());
        CHECK(Kill(*m, 1, b[0]) == ActionError::None);
        MinionView y = MV(*m, b[1]);
        CHECK(y.attack == 7 && y.health == 7 && y.maxHealth == 7);
        CHECK(TA::SecretCardIds(*m, 0).empty());
        CHECK_EQ(CountEvents(*m, EventType::Reveal), 1);
        bool revealed = false;
        for (Event const& e : m->ViewFor(1, 0).events)
            if (e.type == EventType::Reveal && e.cardId == "FP1_020")
                revealed = true;
        CHECK(revealed); // the secret is shown to the opponent when it fires

        // No other friendly minion: the secret stays hidden and armed.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 0, {"CS2_168"});
        TA::SetSecrets(*n, 0, {"FP1_020"});
        ToSide1(*n);
        CHECK(Kill(*n, 1, c[0]) == ActionError::None);
        CHECK(TA::SecretCardIds(*n, 0) == Strs{"FP1_020"});
        CHECK_EQ(CountEvents(*n, EventType::Reveal), 0);

        // Two minions die together and nothing is left: still armed. With a survivor: one trigger, one receiver.
        std::unique_ptr<Match> o = Fresh();
        TA::SetBoard(*o, 0, {"CS2_168", "CS2_168"});
        TA::SetSecrets(*o, 0, {"FP1_020"});
        ToSide1(*o);
        CHECK(Cast(*o, 1, "CS2_093") == ActionError::None); // Consecration: 2 damage to all enemies
        CHECK(Board(*o, 0).empty());
        CHECK(TA::SecretCardIds(*o, 0) == Strs{"FP1_020"});
        std::unique_ptr<Match> p = Fresh();
        Ids d = TA::SetBoard(*p, 0, {"CS2_168", "CS2_168", "CS2_182", "CS2_200"});
        TA::SetSecrets(*p, 0, {"FP1_020"});
        ToSide1(*p);
        CHECK(Cast(*p, 1, "CS2_093") == ActionError::None);
        CHECK(Board(*p, 0).size() == 2u); // both Raiders died, the Yeti (4/5) and the Boulderfist Ogre (6/7) live
        MinionView yeti = MV(*p, d[2]);
        MinionView ogre = MV(*p, d[3]);
        CHECK_EQ(yeti.attack + ogre.attack, 4 + 6 + 3); // exactly one of them got +3/+2, once
        CHECK_EQ(yeti.maxHealth + ogre.maxHealth, 5 + 7 + 2);
        CHECK(TA::SecretCardIds(*p, 0).empty());

        // The enemy's own minion dying does not trigger my secret; neither does a death on my own turn.
        std::unique_ptr<Match> q = Fresh();
        Ids e = TA::SetBoard(*q, 0, {"CS2_168", "CS2_182"});
        TA::SetBoard(*q, 1, {"CS2_168"});
        TA::SetSecrets(*q, 0, {"FP1_020"});
        CHECK(Kill(*q, 0, e[0]) == ActionError::None); // my turn: secrets are off
        CHECK(TA::SecretCardIds(*q, 0) == Strs{"FP1_020"});
        ToSide1(*q);
        CHECK(Kill(*q, 1, TA::BoardIds(*q, 1)[0]) == ActionError::None);
        CHECK(TA::SecretCardIds(*q, 0) == Strs{"FP1_020"});
        CHECK(MV(*q, e[1]).attack == 4);

        // An older Deathrattle that fills the board first provides the receiver (Haunted Creeper, then the secret).
        std::unique_ptr<Match> r = Fresh();
        Ids f = TA::SetBoard(*r, 0, {"FP1_002"});
        TA::SetSecrets(*r, 0, {"FP1_020"});
        ToSide1(*r);
        CHECK(Kill(*r, 1, f[0]) == ActionError::None);
        CHECK(TA::SecretCardIds(*r, 0).empty());
        int sumAttack = 0;
        for (EntityId id : TA::BoardIds(*r, 0))
            sumAttack += MV(*r, id).attack;
        CHECK_EQ(Board(*r, 0).size(), 2u);
        CHECK_EQ(sumAttack, 1 + 4);
    }

    // ---- Duplicate ------------------------------------------------------------------------------------------------

    void TestDuplicate()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
        TA::SetMinion(*m, b[0], 9, 3, 9); // buffed and damaged: the copies are base cards
        TA::SetSecrets(*m, 0, {"FP1_018"});
        ToSide1(*m);
        CHECK(Kill(*m, 1, b[0]) == ActionError::None);
        CHECK(Hand(*m, 0) == (Strs{"CS2_182", "CS2_182"}));
        CHECK(TA::SecretCardIds(*m, 0).empty());
        for (HandCardView const& h : m->ViewFor(0, 0).self.hand)
            CHECK(h.attack == 4 && h.health == 5);
        // The opponent sees the secret revealed but not the cards that arrive in my hand.
        bool hidden = false;
        bool revealed = false;
        for (Event const& e : m->ViewFor(1, 0).events)
        {
            if (e.type == EventType::Generate && e.side == 0 && e.cardId.empty())
                hidden = true;
            if (e.type == EventType::Reveal && e.cardId == "FP1_018")
                revealed = true;
        }
        CHECK(hidden && revealed);

        // Full hand: 9 cards leave room for one copy, 10 for none.
        for (int cards = 8; cards <= 10; ++cards)
        {
            std::unique_ptr<Match> n = Fresh();
            Ids c = TA::SetBoard(*n, 0, {"CS2_182"});
            TA::SetSecrets(*n, 0, {"FP1_018"});
            ToSide1(*n);
            TA::SetHand(*n, 0, Strs(static_cast<size_t>(cards), "CS2_029"));
            CHECK(Kill(*n, 1, c[0]) == ActionError::None);
            CHECK_EQ(Hand(*n, 0).size(), std::min<size_t>(10, static_cast<size_t>(cards) + 2));
            CHECK(TA::SecretCardIds(*n, 0).empty()); // it fired even though the cards were lost
        }

        // Only friendly deaths on the enemy turn: my own turn and the enemy's minions do nothing.
        std::unique_ptr<Match> o = Fresh();
        Ids d = TA::SetBoard(*o, 0, {"CS2_182"});
        TA::SetBoard(*o, 1, {"CS2_182"});
        TA::SetSecrets(*o, 0, {"FP1_018"});
        CHECK(Kill(*o, 0, d[0]) == ActionError::None);
        CHECK(Hand(*o, 0).empty() && TA::SecretCardIds(*o, 0) == Strs{"FP1_018"});
        ToSide1(*o);
        CHECK(Kill(*o, 1, TA::BoardIds(*o, 1)[0]) == ActionError::None);
        CHECK(Hand(*o, 0).empty());

        // Two minions die together: the copies are of the first one in play order. A token counts.
        std::unique_ptr<Match> p = Fresh();
        TA::SetBoard(*p, 0, {"CS2_182", "CS2_168"});
        TA::SetSecrets(*p, 0, {"FP1_018"});
        ToSide1(*p);
        CHECK(Cast(*p, 1, "EX1_312") == ActionError::None); // Twisting Nether
        CHECK(Hand(*p, 0) == (Strs{"CS2_182", "CS2_182"}));
        std::unique_ptr<Match> q = Fresh();
        Ids t = TA::SetBoard(*q, 0, {"FP1_002t"});
        TA::SetSecrets(*q, 0, {"FP1_018"});
        ToSide1(*q);
        CHECK(Kill(*q, 1, t[0]) == ActionError::None);
        CHECK(Hand(*q, 0) == (Strs{"FP1_002t", "FP1_002t"}));

        // Hidden information: in the copy the opponent's AI simulates on, my secret is an unknown card.
        std::unique_ptr<Match> r = Fresh();
        Ids u = TA::SetBoard(*r, 0, {"CS2_182"});
        TA::SetSecrets(*r, 0, {"FP1_018"});
        ToSide1(*r);
        std::unique_ptr<Match> copy = r->RedactedCopy(1, 5);
        CHECK(copy != nullptr);
        CHECK(Kill(*copy, 1, u[0]) == ActionError::None);
        CHECK(Hand(*copy, 0).empty());
        CHECK(Kill(*r, 1, u[0]) == ActionError::None);
        CHECK_EQ(Hand(*r, 0).size(), 2u);
    }

    // ---- Echoing Ooze ---------------------------------------------------------------------------------------------

    void TestEchoingOoze()
    {
        // The copy comes at the end of the turn, right of the original, with the same stats; no Battlecry, no second copy.
        std::unique_ptr<Match> m = Fresh();
        CHECK(Cast(*m, 0, "FP1_003") == ActionError::None);
        CHECK(Board(*m, 0) == Strs{"FP1_003"});
        CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).echoing);
        CHECK(m->Apply(0, End()) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"FP1_003", "FP1_003"}));
        Ids ids = TA::BoardIds(*m, 0);
        CHECK(!MV(*m, ids[0]).echoing && !MV(*m, ids[1]).echoing);
        CHECK(MV(*m, ids[1]).attack == 1 && MV(*m, ids[1]).health == 2);
        CHECK_EQ(CountEvents(*m, EventType::Summon), 2); // the Ooze itself, then its copy
        ToSide0(*m);
        m->Apply(0, End());
        ToSide0(*m);
        CHECK_EQ(Board(*m, 0).size(), 2u);

        // Position: right of the original, between the neighbours.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 0, {"CS2_168", "CS2_182"});
        CHECK(Cast(*n, 0, "FP1_003", 0, 0, 1) == ActionError::None);
        n->Apply(0, End());
        CHECK(Board(*n, 0) == (Strs{"CS2_168", "FP1_003", "FP1_003", "CS2_182"}));

        // Buffed before the end of the turn: the copy has the buff.
        std::unique_ptr<Match> o = Fresh();
        CHECK(Cast(*o, 0, "FP1_003") == ActionError::None);
        EntityId oo = TA::BoardIds(*o, 0)[0];
        CHECK(Cast(*o, 0, "CS2_092", oo) == ActionError::None); // Blessing of Kings
        o->Apply(0, End());
        Ids oi = TA::BoardIds(*o, 0);
        CHECK(oi.size() == 2u && MV(*o, oi[1]).attack == 5 && MV(*o, oi[1]).health == 6);
        // Damaged: the copy is damaged too.
        std::unique_ptr<Match> p = Fresh();
        CHECK(Cast(*p, 0, "FP1_003") == ActionError::None);
        EntityId po = TA::BoardIds(*p, 0)[0];
        TA::SetMinion(*p, po, 1, 1, 2);
        p->Apply(0, End());
        Ids pi = TA::BoardIds(*p, 0);
        CHECK(pi.size() == 2u && MV(*p, pi[1]).health == 1 && MV(*p, pi[1]).maxHealth == 2);
        // The buff of an aura counts as it is then (Dire Wolf Alpha next to it).
        std::unique_ptr<Match> a = Fresh();
        TA::SetBoard(*a, 0, {"EX1_162"});
        CHECK(Cast(*a, 0, "FP1_003") == ActionError::None); // right of the Alpha: +1 attack
        a->Apply(0, End());
        CHECK(Board(*a, 0) == (Strs{"EX1_162", "FP1_003", "FP1_003"}));
        Ids ai = TA::BoardIds(*a, 0);
        CHECK(MV(*a, ai[1]).attack == 2 && MV(*a, ai[2]).attack == 1); // the copy is not next to the Alpha

        // Silenced, destroyed, transformed first: no copy.
        std::unique_ptr<Match> s = Fresh();
        CHECK(Cast(*s, 0, "FP1_003") == ActionError::None);
        CHECK(Cast(*s, 0, "EX1_332", TA::BoardIds(*s, 0)[0]) == ActionError::None);
        s->Apply(0, End());
        CHECK_EQ(Board(*s, 0).size(), 1u);
        std::unique_ptr<Match> k = Fresh();
        CHECK(Cast(*k, 0, "FP1_003") == ActionError::None);
        CHECK(Kill(*k, 0, TA::BoardIds(*k, 0)[0]) == ActionError::None);
        k->Apply(0, End());
        CHECK(Board(*k, 0).empty());
        std::unique_ptr<Match> h = Fresh();
        CHECK(Cast(*h, 0, "FP1_003") == ActionError::None);
        CHECK(Cast(*h, 0, "EX1_246", TA::BoardIds(*h, 0)[0]) == ActionError::None); // Hex
        h->Apply(0, End());
        CHECK(Board(*h, 0) == Strs{"hexfrog"});
        // Full board: nothing, and no copy later either.
        std::unique_ptr<Match> f = Fresh();
        TA::SetBoard(*f, 0, Strs(6, "CS2_168"));
        CHECK(Cast(*f, 0, "FP1_003") == ActionError::None);
        f->Apply(0, End());
        CHECK_EQ(Board(*f, 0).size(), 7u);
        CHECK_EQ(CountOf(Board(*f, 0), "FP1_003"), 1);

        // Undertaker: the Ooze has no Deathrattle, so neither the Ooze nor its copy feeds him.
        std::unique_ptr<Match> u = Fresh();
        Ids ub = TA::SetBoard(*u, 0, {"FP1_028"});
        CHECK(Cast(*u, 0, "FP1_003") == ActionError::None);
        u->Apply(0, End());
        CHECK_EQ(Board(*u, 0).size(), 3u);
        CHECK(MV(*u, ub[0]).attack == 1 && MV(*u, ub[0]).health == 2);
        // Weblord: the Ooze has a Battlecry, so it costs 2 more.
        std::unique_ptr<Match> w = Fresh();
        TA::SetBoard(*w, 1, {"FP1_017"});
        Ids wh = TA::SetHand(*w, 0, {"FP1_003"});
        CHECK_EQ(Cost(*w, wh[0]), 4);

        // The enemy's Ooze copies itself at the end of ITS turn, not mine.
        std::unique_ptr<Match> e = Fresh();
        ToSide1(*e);
        CHECK(Cast(*e, 1, "FP1_003") == ActionError::None);
        CHECK(e->Apply(1, End()) == ActionError::None);
        CHECK_EQ(Board(*e, 1).size(), 2u);
        CHECK(Board(*e, 0).empty());

        // Power Overwhelming on the Ooze: the original dies at the end of the turn, the copy is made first, is spared and
        // dies at the end of the opponent's turn (hearthstone.wiki.gg).
        std::unique_ptr<Match> po2 = Fresh();
        CHECK(Cast(*po2, 0, "FP1_003") == ActionError::None);
        CHECK(Cast(*po2, 0, "EX1_316", TA::BoardIds(*po2, 0)[0]) == ActionError::None);
        po2->Apply(0, End());
        CHECK(Board(*po2, 0) == Strs{"FP1_003"});
        po2->Apply(1, End());
        CHECK(Board(*po2, 0).empty());
    }

    // ---- Baron Rivendare ------------------------------------------------------------------------------------------

    void TestBaronRivendare()
    {
        // Haunted Creeper: four Spiders. The Deathrattle is announced twice.
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_031", "FP1_002"});
        CHECK(Kill(*m, 0, b[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*m, 0), "FP1_002t"), 4);
        CHECK_EQ(Board(*m, 0).size(), 5u);
        CHECK_EQ(CountEvents(*m, EventType::Deathrattle), 2);
        // Sludge Belcher: two Slimes.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 0, {"FP1_031", "FP1_012"});
        CHECK(Kill(*n, 0, c[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*n, 0), "FP1_012t"), 2);
        // Ancestral Spirit: the Resummon is a granted Deathrattle and doubles: two fresh copies.
        std::unique_ptr<Match> o = Fresh();
        Ids d = TA::SetBoard(*o, 0, {"FP1_031", "CS2_182"});
        CHECK(Cast(*o, 0, "CS2_038", d[1]) == ActionError::None);
        CHECK(Kill(*o, 0, d[1]) == ActionError::None);
        CHECK(Board(*o, 0) == (Strs{"FP1_031", "CS2_182", "CS2_182"}));
        for (EntityId id : TA::BoardIds(*o, 0))
            if (MV(*o, id).cardId == "CS2_182")
                CHECK(MV(*o, id).attack == 4 && MV(*o, id).health == 5);
        // A minion with its own and a granted Deathrattle: each list twice (Loot Hoarder: 2 cards, Spirit: 2 minions).
        std::unique_ptr<Match> p = Fresh();
        Ids e = TA::SetBoard(*p, 0, {"FP1_031", "EX1_096"});
        CHECK(Cast(*p, 0, "CS2_038", e[1]) == ActionError::None);
        CHECK(Kill(*p, 0, e[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*p, 0), "EX1_096"), 2);
        CHECK_EQ(TA::HandIds(*p, 0).size(), 2u);
        // Soul of the Forest: the granted Treant Deathrattle twice.
        std::unique_ptr<Match> q = Fresh();
        Ids f = TA::SetBoard(*q, 0, {"FP1_031", "CS2_182"});
        CHECK(Cast(*q, 0, "EX1_158") == ActionError::None);
        CHECK(Kill(*q, 0, f[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*q, 0), "EX1_158t"), 2);

        // Only his own side: an enemy Baron does not double my Deathrattles, mine does not double theirs.
        std::unique_ptr<Match> r = Fresh();
        Ids g = TA::SetBoard(*r, 0, {"FP1_002"});
        TA::SetBoard(*r, 1, {"FP1_031", "FP1_002"});
        CHECK(Kill(*r, 0, g[0]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*r, 0), "FP1_002t"), 2);
        CHECK(Kill(*r, 0, TA::BoardIds(*r, 1)[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*r, 1), "FP1_002t"), 4);
        // Silenced Baron: no doubling. Two Barons: still twice, not three times.
        std::unique_ptr<Match> s = Fresh();
        Ids h = TA::SetBoard(*s, 0, {"FP1_031", "FP1_002"});
        CHECK(Cast(*s, 0, "EX1_332", h[0]) == ActionError::None);
        CHECK(Kill(*s, 0, h[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*s, 0), "FP1_002t"), 2);
        std::unique_ptr<Match> t = Fresh();
        Ids i = TA::SetBoard(*t, 0, {"FP1_031", "FP1_031", "FP1_002"});
        CHECK(Kill(*t, 0, i[2]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*t, 0), "FP1_002t"), 4);

        // Simultaneous death: a Baron that dies in the same death step does not double (hearthstone.wiki.gg).
        std::unique_ptr<Match> u = Fresh();
        TA::SetBoard(*u, 0, {"FP1_031", "FP1_002"});
        CHECK(Cast(*u, 0, "EX1_312") == ActionError::None); // Twisting Nether
        CHECK(Board(*u, 0) == (Strs{"FP1_002t", "FP1_002t"}));
        // He dies first, the Deathrattle minion afterwards: also once.
        std::unique_ptr<Match> v = Fresh();
        Ids j = TA::SetBoard(*v, 0, {"FP1_031", "FP1_002"});
        CHECK(Smash(*v, 0, j[0]) == ActionError::None);
        CHECK(Kill(*v, 0, j[1]) == ActionError::None);
        CHECK_EQ(Board(*v, 0).size(), 2u);

        // Weapons are not minions: Death's Bite replaced by a new axe deals its 1 damage once.
        std::unique_ptr<Match> w = Fresh();
        Ids k = TA::SetBoard(*w, 0, {"FP1_031"});
        TA::SetWeapon(*w, 0, "FP1_021");
        CHECK(Cast(*w, 0, "CS2_106") == ActionError::None); // Fiery War Axe replaces it
        CHECK(MV(*w, k[0]).health == 6);
        // Dancing Swords: the opponent draws two cards.
        std::unique_ptr<Match> x = Fresh();
        Ids l = TA::SetBoard(*x, 0, {"FP1_031", "FP1_029"});
        CHECK(Kill(*x, 0, l[1]) == ActionError::None);
        CHECK_EQ(TA::HandIds(*x, 1).size(), 2u);
        // Board limit: the second run finds no room.
        std::unique_ptr<Match> y = Fresh();
        Ids z = TA::SetBoard(*y, 0, {"FP1_031", "FP1_002", "CS2_168", "CS2_168", "CS2_168"});
        CHECK(Kill(*y, 0, z[1]) == ActionError::None);
        CHECK_EQ(Board(*y, 0).size(), 7u);
    }

    // ---- Stalagg and Feugen ---------------------------------------------------------------------------------------

    void TestStalaggFeugen()
    {
        for (int stalaggFirst = 0; stalaggFirst < 2; ++stalaggFirst)
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"FP1_014", "FP1_015"});
            CHECK(Smash(*m, 0, b[stalaggFirst ? 0 : 1]) == ActionError::None);
            CHECK_EQ(CountOf(Board(*m, 0), "FP1_014t"), 0);
            CHECK_EQ(Board(*m, 0).size(), 1u);
            CHECK(Smash(*m, 0, b[stalaggFirst ? 1 : 0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"FP1_014t"});
            MinionView th = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(th.attack == 11 && th.health == 11);
        }
        // Dying together: both Deathrattles see the other as dead and each summons a Thaddius.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 0, {"FP1_014", "FP1_015"});
        CHECK(Cast(*n, 0, "EX1_312") == ActionError::None);
        CHECK(Board(*n, 0) == (Strs{"FP1_014t", "FP1_014t"}));
        // Dying together across sides (Twisting Nether): each owner's Deathrattle sees the other as dead, one Thaddius each.
        std::unique_ptr<Match> u = Fresh();
        TA::SetBoard(*u, 0, {"FP1_014"});
        TA::SetBoard(*u, 1, {"FP1_015"});
        CHECK(Cast(*u, 0, "EX1_312") == ActionError::None);
        CHECK(Board(*u, 0) == Strs{"FP1_014t"});
        CHECK(Board(*u, 1) == Strs{"FP1_014t"});
        // Either side's deaths count ("also died this game"): the enemy's Stalagg dies first, my Feugen second.
        std::unique_ptr<Match> o = Fresh();
        Ids ob = TA::SetBoard(*o, 0, {"FP1_015"});
        Ids oe = TA::SetBoard(*o, 1, {"FP1_014"});
        CHECK(Smash(*o, 0, oe[0]) == ActionError::None);
        CHECK(Board(*o, 0) == Strs{"FP1_015"});
        CHECK(Smash(*o, 0, ob[0]) == ActionError::None);
        CHECK(Board(*o, 0) == Strs{"FP1_014t"});
        CHECK(Board(*o, 1).empty());
        // Silenced Stalagg: its death still counts for Feugen; a silenced Feugen has no Deathrattle.
        std::unique_ptr<Match> p = Fresh();
        Ids pb = TA::SetBoard(*p, 0, {"FP1_014", "FP1_015"});
        CHECK(Cast(*p, 0, "EX1_332", pb[0]) == ActionError::None);
        CHECK(Smash(*p, 0, pb[0]) == ActionError::None);
        CHECK(Smash(*p, 0, pb[1]) == ActionError::None);
        CHECK(Board(*p, 0) == Strs{"FP1_014t"});
        std::unique_ptr<Match> q = Fresh();
        Ids qb = TA::SetBoard(*q, 0, {"FP1_014", "FP1_015"});
        CHECK(Smash(*q, 0, qb[0]) == ActionError::None);
        CHECK(Cast(*q, 0, "EX1_332", qb[1]) == ActionError::None);
        CHECK(Smash(*q, 0, qb[1]) == ActionError::None);
        CHECK(Board(*q, 0).empty());
        // Baron Rivendare: the second to die summons two Thaddius.
        std::unique_ptr<Match> r = Fresh();
        Ids rb = TA::SetBoard(*r, 0, {"FP1_031", "FP1_014", "FP1_015"});
        CHECK(Smash(*r, 0, rb[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*r, 0), "FP1_014t"), 0);
        CHECK(Smash(*r, 0, rb[2]) == ActionError::None);
        CHECK(Board(*r, 0) == (Strs{"FP1_031", "FP1_014t", "FP1_014t"}));
        // A full board: no Thaddius fits.
        std::unique_ptr<Match> s = Fresh();
        Ids sb = TA::SetBoard(*s, 0, {"FP1_014", "FP1_015", "CS2_182", "CS2_182", "CS2_182", "CS2_182", "CS2_182"});
        CHECK(Smash(*s, 0, sb[0]) == ActionError::None);
        CHECK(Cast(*s, 0, "CS2_182") == ActionError::None); // refill the slot
        CHECK(Smash(*s, 0, sb[1]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*s, 0), "FP1_014t"), 1); // Feugen's own slot is free again
        // Reached through the enemy's removal on its turn too: the record is per side, not per turn.
        std::unique_ptr<Match> t = Fresh();
        Ids tb = TA::SetBoard(*t, 0, {"FP1_014", "FP1_015"});
        ToSide1(*t);
        CHECK(Smash(*t, 1, tb[0]) == ActionError::None);
        ToSide0(*t);
        ToSide1(*t);
        CHECK(Smash(*t, 1, tb[1]) == ActionError::None);
        CHECK(Board(*t, 0) == Strs{"FP1_014t"});
    }

    // ---- Kel'Thuzad -----------------------------------------------------------------------------------------------

    void TestKelThuzad()
    {
        // My turn: minions that died come back at the end of it, in order of death, at the right end.
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_013", "CS2_182", "CS2_168"});
        TA::SetMinion(*m, b[1], 9, 9, 9);
        CHECK(Kill(*m, 0, b[2]) == ActionError::None);
        CHECK(Smash(*m, 0, b[1]) == ActionError::None);
        CHECK(Board(*m, 0) == Strs{"FP1_013"});
        m->Apply(0, End());
        CHECK(Board(*m, 0) == (Strs{"FP1_013", "CS2_168", "CS2_182"})); // order of death, fresh copies
        Ids mi = TA::BoardIds(*m, 0);
        CHECK(MV(*m, mi[2]).attack == 4 && MV(*m, mi[2]).health == 5);
        // The record ends with the turn: nothing more at the end of the next turns.
        ToSide0(*m);
        m->Apply(0, End());
        CHECK_EQ(Board(*m, 0).size(), 3u);

        // The enemy turn counts too: my minion that dies on it returns at the end of it.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 0, {"FP1_013", "CS2_182"});
        ToSide1(*n);
        CHECK(Kill(*n, 1, c[1]) == ActionError::None);
        CHECK(Board(*n, 0) == Strs{"FP1_013"});
        n->Apply(1, End());
        CHECK(Board(*n, 0) == (Strs{"FP1_013", "CS2_182"}));

        // Only my side's minions: the enemy's dead stay dead.
        std::unique_ptr<Match> o = Fresh();
        TA::SetBoard(*o, 0, {"FP1_013"});
        Ids oe = TA::SetBoard(*o, 1, {"CS2_182"});
        CHECK(Kill(*o, 0, oe[0]) == ActionError::None);
        o->Apply(0, End());
        CHECK(Board(*o, 1).empty());
        CHECK(Board(*o, 0) == Strs{"FP1_013"});

        // Dead himself: nothing. Silenced: nothing.
        std::unique_ptr<Match> p = Fresh();
        Ids pb = TA::SetBoard(*p, 0, {"FP1_013", "CS2_182"});
        CHECK(Kill(*p, 0, pb[1]) == ActionError::None);
        CHECK(Smash(*p, 0, pb[0]) == ActionError::None);
        p->Apply(0, End());
        CHECK(Board(*p, 0).empty());
        std::unique_ptr<Match> q = Fresh();
        Ids qb = TA::SetBoard(*q, 0, {"FP1_013", "CS2_182"});
        CHECK(Cast(*q, 0, "EX1_332", qb[0]) == ActionError::None);
        CHECK(Kill(*q, 0, qb[1]) == ActionError::None);
        q->Apply(0, End());
        CHECK(Board(*q, 0) == Strs{"FP1_013"});

        // Deathrattle minions: the Spiders live, the Creeper comes back (and Undertaker, summoned with a Deathrattle card,
        // grows). Two Kel'Thuzads bring everything back twice.
        std::unique_ptr<Match> r = Fresh();
        Ids rb = TA::SetBoard(*r, 0, {"FP1_013", "FP1_028", "FP1_002"});
        CHECK(Kill(*r, 0, rb[2]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*r, 0), "FP1_002t"), 2);
        r->Apply(0, End());
        CHECK_EQ(CountOf(Board(*r, 0), "FP1_002"), 1);
        CHECK(MV(*r, rb[1]).attack == 2 && MV(*r, rb[1]).health == 3);
        std::unique_ptr<Match> s = Fresh();
        Ids sb = TA::SetBoard(*s, 0, {"FP1_013", "FP1_013", "CS2_182"});
        CHECK(Kill(*s, 0, sb[2]) == ActionError::None);
        s->Apply(0, End());
        CHECK_EQ(CountOf(Board(*s, 0), "CS2_182"), 2);
        CHECK_EQ(Board(*s, 0).size(), 4u);

        // Board limit: only as many as fit.
        std::unique_ptr<Match> t = Fresh();
        Ids tb = TA::SetBoard(*t, 0, {"FP1_013", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
        CHECK(Kill(*t, 0, tb[1]) == ActionError::None);
        CHECK(Kill(*t, 0, tb[2]) == ActionError::None);
        CHECK(Kill(*t, 0, tb[3]) == ActionError::None);
        CHECK(Cast(*t, 0, "CS2_182") == ActionError::None); // 4 + 1 = 5 on the board, room for two
        t->Apply(0, End());
        CHECK_EQ(Board(*t, 0).size(), 7u);
        CHECK_EQ(CountOf(Board(*t, 0), "CS2_168"), 5);

        // With Baron Rivendare: the Creeper's Deathrattle is doubled, and the Creeper itself returns once.
        std::unique_ptr<Match> u = Fresh();
        Ids ub = TA::SetBoard(*u, 0, {"FP1_013", "FP1_031", "FP1_002"});
        CHECK(Kill(*u, 0, ub[2]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*u, 0), "FP1_002t"), 4);
        u->Apply(0, End());
        CHECK_EQ(CountOf(Board(*u, 0), "FP1_002"), 1);

        // A minion that left play without dying (returned to the hand) does not come back.
        std::unique_ptr<Match> v = Fresh();
        Ids vb = TA::SetBoard(*v, 0, {"FP1_013", "CS2_182"});
        CHECK(Cast(*v, 0, "CS2_022", vb[1]) == ActionError::None); // Polymorph: transformed, nothing died
        v->Apply(0, End());
        CHECK(Board(*v, 0) == (Strs{"FP1_013", "CS2_tk1"}));
    }

    // ---- hidden information, replay and the AI for the wave 2 cards ----------------------------------------------

    void TestWave2Redaction()
    {
        // Kel'Thuzad and Rivendare are plain public minions: a redacted copy plays them the same way.
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"FP1_013", "FP1_031", "FP1_002"});
        std::unique_ptr<Match> r = m->RedactedCopy(1, 4);
        CHECK(Kill(*r, 0, b[2]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*r, 0), "FP1_002t"), 4);
        r->Apply(0, End());
        CHECK_EQ(CountOf(Board(*r, 0), "FP1_002"), 1);
        // The pending mark of an Ooze survives the copy.
        std::unique_ptr<Match> n = Fresh();
        CHECK(Cast(*n, 0, "FP1_003") == ActionError::None);
        std::unique_ptr<Match> c = n->RedactedCopy(1, 4);
        c->Apply(0, End());
        CHECK_EQ(Board(*c, 0).size(), 2u);
        // Avenge of the AI itself works in its own copy; the human's is unknown to it.
        std::unique_ptr<Match> a = Fresh();
        Ids ab = TA::SetBoard(*a, 0, {"CS2_168", "CS2_182"});
        TA::SetSecrets(*a, 0, {"FP1_020"});
        ToSide1(*a);
        std::unique_ptr<Match> own = a->RedactedCopy(0, 4);
        CHECK(Kill(*own, 1, ab[0]) == ActionError::None);
        CHECK(MV(*own, ab[1]).attack == 7);
        std::unique_ptr<Match> foe = a->RedactedCopy(1, 4);
        CHECK(Kill(*foe, 1, ab[0]) == ActionError::None);
        CHECK(MV(*foe, ab[1]).attack == 4);
    }

    void TestWave2Ai()
    {
        // Hard prefers Baron Rivendare over an equal-cost body when two Deathrattle minions stand on its board.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN", "WARRIOR", 21);
            TA::SetBoard(*m, 0, {"FP1_002", "FP1_012"});
            TA::SetHand(*m, 0, {"FP1_031", "CS2_182"});
            TA::SetMana(*m, 0, 4, 4);
            TA::SetHeroPowerUsed(*m, 0, true);
            TA::ReadyAll(*m);
            bool baron = false;
            for (int n = 0; n < 6; ++n)
            {
                Action const act = ChooseAiAction(*m, 0, Db(), n, AiProfile::Hard, 21 + static_cast<uint32_t>(n));
                if (act.type == ActionType::EndTurn)
                    break;
                if (act.type == ActionType::Play)
                    for (HandCardView const& h : m->ViewFor(0, m->LastEventSeq()).self.hand)
                        if (h.id == act.source && h.cardId == "FP1_031")
                            baron = true;
                CHECK(m->Apply(0, act) == ActionError::None);
            }
            CHECK(baron);
        }
        // Both profiles play a lone Echoing Ooze (a second body at the end of the turn).
        for (AiProfile prof : {AiProfile::Normal, AiProfile::Hard})
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", 22);
            TA::SetHand(*m, 0, {"FP1_003"});
            TA::SetMana(*m, 0, 2, 2);
            TA::SetHeroPowerUsed(*m, 0, true);
            Action const act = ChooseAiAction(*m, 0, Db(), 0, prof, 22);
            CHECK(act.type == ActionType::Play);
        }
        // An AI with Avenge in hand and two minions plays the secret; with a Duplicate and nothing else it plays that too.
        for (char const* secret : {"FP1_020", "FP1_018"})
        {
            std::unique_ptr<Match> m = Fresh("PALADIN", "WARRIOR", 23);
            TA::SetBoard(*m, 0, {"CS2_182", "CS2_182"});
            TA::SetHand(*m, 0, {secret});
            TA::SetMana(*m, 0, 3, 3);
            TA::SetHeroPowerUsed(*m, 0, true);
            bool played = false;
            for (int n = 0; n < 4; ++n)
            {
                Action const act = ChooseAiAction(*m, 0, Db(), n, AiProfile::Hard, 23 + static_cast<uint32_t>(n));
                if (act.type == ActionType::EndTurn)
                    break;
                if (act.type == ActionType::Play)
                    played = true;
                CHECK(m->Apply(0, act) == ActionError::None);
            }
            CHECK(played);
            CHECK_EQ(TA::SecretCardIds(*m, 0).size(), 1u);
        }
    }

    // ---- LegalActions against Apply -----------------------------------------------------------------------------

    // The same rich position every time: the card in the hand of side 0, a mixed battlefield.
    std::unique_ptr<Match> Rich(char const* card, bool fullBoard, bool secrets)
    {
        std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", 77);
        TA::SetBoard(*m, 0, fullBoard ? Strs(7, "CS2_182") : Strs{"CS2_182", "CS2_168", "EX1_029"});
        TA::SetBoard(*m, 1, {"CS2_168", "EX1_010", "CS2_200", "EX1_100", "FP1_008"});
        TA::SetMinion(*m, TA::BoardIds(*m, 1)[0], 2, 1, 1);
        if (secrets)
        {
            TA::SetSecrets(*m, 0, {"EX1_287"});
            TA::SetSecrets(*m, 1, {"EX1_289"});
        }
        TA::SetDeck(*m, 0, {"CS2_182", "CS2_168", "EX1_609", "CS2_182"});
        TA::SetDeck(*m, 1, {"CS2_182", "CS2_168", "CS2_146", "CS2_182"});
        TA::SetHand(*m, 0, {card});
        TA::SetMana(*m, 0, 10, 10);
        return m;
    }

    void CheckLegalAgreement(char const* card, bool fullBoard, bool secrets)
    {
        std::unique_ptr<Match> probe = Rich(card, fullBoard, secrets);
        EntityId const cardId = TA::HandIds(*probe, 0)[0];
        std::vector<Action> const legal = probe->LegalActions(0);
        Ids targets = {0};
        for (int s = 0; s < 2; ++s)
        {
            targets.push_back(TA::HeroId(*probe, s));
            for (EntityId id : TA::BoardIds(*probe, s))
                targets.push_back(id);
        }
        for (EntityId t : targets)
        {
            bool listed = false;
            for (Action const& a : legal)
                if (a.type == ActionType::Play && a.source == cardId && a.target == t)
                    listed = true;
            std::unique_ptr<Match> m = Rich(card, fullBoard, secrets);
            ActionError const e = m->Apply(0, Play(cardId, t));
            if ((e == ActionError::None) != listed)
            {
                std::printf("  legal/apply mismatch: %s target %u full=%d: listed=%d error=%s\n", card, t, fullBoard ? 1 : 0,
                    listed ? 1 : 0, ActionErrorCode(e));
                CHECK(false);
            }
            CHECK(!Capped(*m));
        }
    }

    void TestLegalAgreement()
    {
        for (char const* card : kCards)
            for (int variant = 0; variant < 4; ++variant)
                CheckLegalAgreement(card, (variant & 1) != 0, (variant & 2) != 0);
    }

    // ---- determinism, soak, AI -------------------------------------------------------------------------------------

    // The supported collectible cards a deck of heroClass may hold.
    std::vector<std::string> DeckPool(std::string const& heroClass)
    {
        static std::set<std::string> const tokens = {"CS2_050", "CS2_051", "CS2_052", "CS2_082", "CS2_101t", "CS2_152",
            "CS2_boar", "CS2_mirror", "CS2_tk1", "EX1_025t", "EX1_110t", "EX1_130a", "EX1_160t", "EX1_165a", "EX1_165b",
            "EX1_165t1", "EX1_165t2", "EX1_317t", "EX1_345t", "EX1_383t", "EX1_506a", "EX1_534t", "EX1_554t", "EX1_598",
            "EX1_finkle", "EX1_tk11", "EX1_tk28", "EX1_tk29", "EX1_tk34", "EX1_tk9", "GAME_002", "Mekka1", "Mekka2",
            "Mekka3", "Mekka4", "NEW1_009", "NEW1_032", "NEW1_033", "NEW1_034", "hexfrog", "skele11", "skele21", "tt_010a",
            "FP1_007t", "FP1_014t"};
        std::vector<std::string> pool;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            std::string const type = r.type;
            if (type != "MINION" && type != "SPELL" && type != "WEAPON")
                continue;
            if (std::string(r.rarity).empty() || tokens.count(r.id))
                continue;
            if (std::string(r.cls) != heroClass && std::string(r.cls) != "NEUTRAL")
                continue;
            if (!Db().Supported(r.id))
                continue;
            pool.push_back(r.id);
        }
        return pool;
    }

    // The wave 1 cards a deck of heroClass may hold.
    std::vector<std::string> OwnCards(std::string const& heroClass)
    {
        std::vector<std::string> out;
        for (char const* id : kCards)
        {
            CardDef const* def = Db().Find(id);
            if (def && (def->cardClass == heroClass || def->cardClass == "NEUTRAL"))
                out.push_back(id);
        }
        return out;
    }

    // 30 cards, at most 2 copies, 1 legendary; most of the wave 1 cards of the class go in first.
    std::vector<std::string> NaxxDeck(std::string const& heroClass, std::mt19937& rng)
    {
        std::vector<std::string> deck;
        std::map<std::string, int> copies;
        auto put = [&](std::string const& id) {
            CardDef const* def = Db().Find(id);
            int const limit = (def && def->rarity == "LEGENDARY") ? 1 : 2;
            if (copies[id] >= limit || deck.size() >= 30)
                return;
            ++copies[id];
            deck.push_back(id);
        };
        for (std::string const& id : OwnCards(heroClass))
            if (rng() % 4 != 0)
                put(id);
        std::vector<std::string> const pool = DeckPool(heroClass);
        int guard = 0;
        while (deck.size() < 30 && guard++ < 100000)
            put(pool[rng() % pool.size()]);
        return deck;
    }

    char const* const kClasses[] = {"WARRIOR", "SHAMAN", "ROGUE", "PALADIN", "HUNTER", "DRUID", "WARLOCK", "MAGE", "PRIEST"};

    struct SoakStats
    {
        int games = 0;
        int ended = 0;
        int actions = 0;
        int failures = 0;
        int capped = 0;
        std::map<std::string, int> played;
    };

    void SoakGame(uint32_t seed, std::string const& c0, std::string const& c1, SoakStats& st)
    {
        std::mt19937 rng(seed * 2654435761u + 99u);
        MatchSetup setup;
        setup.seed = seed;
        setup.firstSide = -1;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = c0;
        setup.sides[0].deck = NaxxDeck(c0, rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = c1;
        setup.sides[1].deck = NaxxDeck(c1, rng);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
        ++st.games;
        if (!m)
        {
            ++st.failures;
            std::printf("  create failed (seed %u %s vs %s): %s\n", seed, c0.c_str(), c1.c_str(), error.c_str());
            return;
        }
        for (int s = 0; s < 2; ++s)
        {
            MatchView const v = m->ViewFor(s, 0);
            Action mull;
            mull.type = ActionType::Mulligan;
            for (EntityId id : v.mulligan)
                if (rng() % 3 == 0)
                    mull.replace.push_back(id);
            if (m->Apply(s, mull) != ActionError::None)
                ++st.failures;
        }
        for (int step = 0; step < 400 && m->GetPhase() == Phase::Play; ++step)
        {
            int const side = m->ActiveSide();
            MatchView const v = m->ViewFor(side, m->LastEventSeq());
            Action a;
            if (v.pickPending && !v.pick.empty())
                a = Choose(v.pick[rng() % v.pick.size()].id);
            else
            {
                std::vector<Action> const& legal = v.legal;
                size_t const pick = rng() % (legal.size() + 1);
                if (pick == legal.size())
                    a = End();
                else
                {
                    a = legal[pick];
                    if (a.type == ActionType::Play)
                    {
                        a.position = static_cast<int>(rng() % (v.self.board.size() + 1));
                        for (HandCardView const& h : v.self.hand)
                            if (h.id == a.source)
                            {
                                CardDef const* def = Db().Find(h.cardId);
                                if (def && def->type != CardType::Minion)
                                    a.position = -1;
                                ++st.played[h.cardId];
                            }
                    }
                }
            }
            ActionError const e = m->Apply(side, a);
            if (e != ActionError::None)
            {
                ++st.failures;
                std::printf("  action refused (%s): seed %u step %d type %d source %u target %u\n", ActionErrorCode(e), seed, step,
                    static_cast<int>(a.type), a.source, a.target);
                break;
            }
            ++st.actions;
            if (Capped(*m))
            {
                ++st.capped;
                std::printf("  capped: seed %u step %d\n", seed, step);
            }
            for (int s = 0; s < 2; ++s)
            {
                MatchView const w = m->ViewFor(s, m->LastEventSeq());
                bool ok = w.self.board.size() <= static_cast<size_t>(kMaxBoard) && w.self.hand.size() <= static_cast<size_t>(kMaxHand) &&
                    w.self.hero.health <= w.self.hero.maxHealth && w.self.secrets.size() <= 5;
                for (MinionView const& mv : w.self.board)
                    if (mv.health <= 0 && m->GetPhase() != Phase::Over)
                        ok = false;
                if (!ok)
                {
                    ++st.failures;
                    std::printf("  invariant broken: seed %u step %d side %d\n", seed, step, s);
                }
            }
        }
        if (m->GetPhase() == Phase::Over)
            ++st.ended;
    }

    void TestRandomGames()
    {
        for (char const* c : kClasses)
            CHECK(DeckPool(c).size() >= 20);
        SoakStats st;
        for (int ci = 0; ci < 9; ++ci)
            for (uint32_t k = 0; k < static_cast<uint32_t>(g_gamesPerClass); ++k)
            {
                uint32_t const seed = 7000 + static_cast<uint32_t>(ci) * 100 + k;
                SoakGame(seed, kClasses[ci], kClasses[(ci + 1 + static_cast<int>(k % 8)) % 9], st);
            }
        std::printf("  random games: %d games, %d ended, %d actions, %zu distinct cards played\n", st.games, st.ended, st.actions,
            st.played.size());
        CHECK_EQ(st.failures, 0);
        CHECK_EQ(st.capped, 0);
        CHECK(st.ended * 3 >= st.games);
        int missing = 0;
        for (char const* id : kCards)
            if (!st.played.count(id))
            {
                ++missing;
                std::printf("  never played in the soak: %s\n", id);
            }
        CHECK(missing <= 2);
    }

    void TestDeterminism()
    {
        std::string states[2];
        for (int run = 0; run < 2; ++run)
        {
            std::mt19937 rng(7);
            MatchSetup setup;
            setup.seed = 4243;
            setup.firstSide = 0;
            setup.sides[0].name = "A";
            setup.sides[0].heroClass = "WARLOCK";
            setup.sides[0].deck = NaxxDeck("WARLOCK", rng);
            setup.sides[1].name = "B";
            setup.sides[1].heroClass = "HUNTER";
            setup.sides[1].deck = NaxxDeck("HUNTER", rng);
            std::string error;
            std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
            CHECK(m != nullptr);
            if (!m)
                return;
            Action mull;
            mull.type = ActionType::Mulligan;
            m->Apply(0, mull);
            m->Apply(1, mull);
            for (int step = 0; step < 250 && m->GetPhase() == Phase::Play; ++step)
            {
                int const side = m->ActiveSide();
                MatchView const v = m->ViewFor(side, m->LastEventSeq());
                if (v.pickPending && !v.pick.empty())
                {
                    m->Apply(side, Choose(v.pick[rng() % v.pick.size()].id));
                    continue;
                }
                size_t const pick = rng() % (v.legal.size() + 1);
                m->Apply(side, pick == v.legal.size() ? End() : v.legal[pick]);
            }
            states[run] = m->DebugState();
        }
        CHECK(states[0] == states[1]);
        CHECK(states[0].size() > 100);
    }

    // Plays one game with the AI on both sides; returns false when the match could not be created.
    bool AiGame(uint32_t seed, char const* c0, char const* c1, AiProfile p0, AiProfile p1, int& refusals, bool& over)
    {
        std::mt19937 rng(seed);
        MatchSetup setup;
        setup.seed = seed;
        setup.firstSide = -1;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = c0;
        setup.sides[0].deck = NaxxDeck(c0, rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = c1;
        setup.sides[1].deck = NaxxDeck(c1, rng);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
        if (!m)
            return false;
        AiProfile const prof[2] = {p0, p1};
        uint32_t const aiSeed[2] = {seed * 7919u + 1u, seed * 104729u + 2u};
        for (int side = 0; side < 2; ++side)
        {
            Action const a = ChooseAiAction(*m, side, Db(), 0, prof[side], aiSeed[side]);
            if (a.type != ActionType::Mulligan || m->Apply(side, a) != ActionError::None)
                ++refusals;
        }
        int actions = 0;
        int turn = m->Turn();
        for (int guard = 0; guard < 4000 && m->GetPhase() == Phase::Play; ++guard)
        {
            int const side = m->ActiveSide();
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                actions = 0;
            }
            Action const a = ChooseAiAction(*m, side, Db(), actions, prof[side], aiSeed[side]);
            if (m->Apply(side, a) != ActionError::None)
            {
                ++refusals;
                Action c;
                c.type = ActionType::Concede;
                m->Apply(side, c);
            }
            ++actions;
        }
        over = m->GetPhase() == Phase::Over;
        return true;
    }

    void TestAiPlaysThem()
    {
        int refusals = 0;
        int games = 0;
        int ended = 0;
        char const* const pairs[][2] = {{"WARLOCK", "DRUID"}, {"PRIEST", "SHAMAN"}, {"ROGUE", "PALADIN"}, {"MAGE", "HUNTER"},
            {"WARRIOR", "WARLOCK"}, {"DRUID", "PRIEST"}};
        for (int i = 0; i < 6; ++i)
        {
            bool over = false;
            CHECK(AiGame(600 + static_cast<uint32_t>(i), pairs[i][0], pairs[i][1], AiProfile::Normal, AiProfile::Normal, refusals, over));
            ++games;
            ended += over ? 1 : 0;
        }
        for (int i = 0; i < 3; ++i)
        {
            bool over = false;
            CHECK(AiGame(700 + static_cast<uint32_t>(i), pairs[i][0], pairs[i][1], AiProfile::Hard, AiProfile::Normal, refusals, over));
            ++games;
            ended += over ? 1 : 0;
        }
        {
            bool over = false;
            CHECK(AiGame(750, "HUNTER", "WARLOCK", AiProfile::Hard, AiProfile::Hard, refusals, over));
            ++games;
            ended += over ? 1 : 0;
        }
        std::printf("  AI games: %d, ended %d, refusals %d\n", games, ended, refusals);
        CHECK_EQ(refusals, 0);
        CHECK(ended * 2 >= games);
    }

    // The AI must be able to pick a play for each of these cards in a simple position without a refusal.
    void TestAiSingleCards()
    {
        int refused = 0;
        for (char const* card : kCards)
            for (AiProfile prof : {AiProfile::Normal, AiProfile::Hard})
            {
                std::unique_ptr<Match> m = Rich(card, false, true);
                Action const a = ChooseAiAction(*m, 0, Db(), 0, prof, 5);
                if (a.type != ActionType::EndTurn && m->Apply(0, a) != ActionError::None)
                {
                    ++refused;
                    std::printf("  AI refused with %s\n", card);
                }
            }
        CHECK_EQ(refused, 0);
    }

    // The Hard AI uses Reincarnate on its own damaged Sludge Belcher (a Deathrattle that pays twice) rather than on the
    // enemy minion or not at all, and a Loatheb in hand is played when the opponent holds a full hand of cards.
    void TestAiChoices()
    {
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN", "WARRIOR", 5);
            Ids a = TA::SetBoard(*m, 0, {"FP1_012"});
            TA::SetMinion(*m, a[0], 3, 1, 5);
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetHand(*m, 0, {"FP1_025"});
            TA::SetMana(*m, 0, 2, 2);
            TA::SetHeroPowerUsed(*m, 0, true);
            TA::ReadyAll(*m);
            // The AI hits the face with the Belcher first (no retaliation), then Reincarnates it: a fresh Belcher and a
            // Slime for 2 mana. It never casts Reincarnate on the enemy Ogre.
            for (int n = 0; n < 6; ++n)
            {
                Action const act = ChooseAiAction(*m, 0, Db(), n, AiProfile::Hard, 5 + static_cast<uint32_t>(n));
                if (act.type == ActionType::EndTurn)
                    break;
                CHECK(m->Apply(0, act) == ActionError::None);
                if (act.type == ActionType::Play)
                    CHECK(act.target == a[0]);
            }
            CHECK(Hand(*m, 0).empty());
            CHECK(Has(Board(*m, 0), "FP1_012") && Has(Board(*m, 0), "FP1_012t"));
            CHECK(Board(*m, 1) == Strs{"CS2_200"});
            CHECK(MV(*m, e[0]).id == e[0]);
        }
        {
            // Loatheb against a full enemy hand: played (5 mana, 5/5 body).
            std::unique_ptr<Match> m = Fresh("MAGE", "MAGE", 6);
            TA::SetHand(*m, 0, {"FP1_030"});
            TA::SetHand(*m, 1, Strs(8, "CS2_029"));
            TA::SetMana(*m, 0, 5, 5);
            Action const act = ChooseAiAction(*m, 0, Db(), 0, AiProfile::Hard, 6);
            CHECK(act.type == ActionType::Play);
        }
    }

    void TestRedactedCopy()
    {
        // Deathlord: in the copy of the dying side's opponent the deck is known, in the copy of the other side it is an
        // unknown placeholder, so nothing comes (no crash, nothing leaks).
        std::unique_ptr<Match> m = Fresh();
        Ids a = TA::SetBoard(*m, 0, {"FP1_009"});
        TA::SetDeck(*m, 1, {"CS2_182", "CS2_168"});
        TA::SetDeck(*m, 0, {"CS2_182"});
        TA::SetMinion(*m, a[0], 2, 1, 8);
        {
            std::unique_ptr<Match> r = m->RedactedCopy(1, 3); // side 1's AI: its own deck is real
            CHECK(r != nullptr);
            CHECK(Cast(*r, 0, "CS2_029", a[0]) == ActionError::None);
            CHECK_EQ(Board(*r, 1).size(), 1u);
        }
        {
            std::unique_ptr<Match> r = m->RedactedCopy(0, 3); // side 0's AI: side 1's deck is unknown
            CHECK(r != nullptr);
            CHECK(Cast(*r, 0, "CS2_029", a[0]) == ActionError::None);
            CHECK(Board(*r, 1).empty());
        }
        // Mad Scientist of the AI itself: its own deck is known to it.
        std::unique_ptr<Match> n = Fresh();
        Ids b = TA::SetBoard(*n, 0, {"FP1_004"});
        TA::SetDeck(*n, 0, {"EX1_609"});
        std::unique_ptr<Match> r = n->RedactedCopy(0, 9);
        CHECK(Cast(*r, 0, "CS2_029", b[0]) == ActionError::None);
        CHECK(TA::SecretCardIds(*r, 0) == Strs{"EX1_609"});
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--games")
            g_gamesPerClass = std::atoi(argv[i + 1]);
    RUN(TestCoverage);
    RUN(TestZombieChow);
    RUN(TestUndertaker);
    RUN(TestUndertakerDeathlord);
    RUN(TestHauntedCreeper);
    RUN(TestNerubianEgg);
    RUN(TestSludgeBelcher);
    RUN(TestUnstableGhoul);
    RUN(TestDancingSwords);
    RUN(TestDarkCultist);
    RUN(TestWailingSoul);
    RUN(TestShade);
    RUN(TestStoneskinGargoyle);
    RUN(TestDeathsBite);
    RUN(TestAnubarAmbusher);
    RUN(TestWebspinner);
    RUN(TestVoidcaller);
    RUN(TestDeathlord);
    RUN(TestMadScientist);
    RUN(TestReincarnate);
    RUN(TestPoisonSeeds);
    RUN(TestSpectralKnight);
    RUN(TestMaexxna);
    RUN(TestWeblord);
    RUN(TestLoatheb);
    RUN(TestLoathebAndMillhouse);
    RUN(TestAvenge);
    RUN(TestDuplicate);
    RUN(TestEchoingOoze);
    RUN(TestBaronRivendare);
    RUN(TestStalaggFeugen);
    RUN(TestKelThuzad);
    RUN(TestWave2Redaction);
    RUN(TestLegalAgreement);
    RUN(TestRandomGames);
    RUN(TestDeterminism);
    RUN(TestAiPlaysThem);
    RUN(TestAiSingleCards);
    RUN(TestAiChoices);
    RUN(TestWave2Ai);
    RUN(TestRedactedCopy);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
