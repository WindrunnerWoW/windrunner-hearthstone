// Standalone tests of the remaining Classic cards (src/match/HsMatchCardsClassicRest.cpp): coverage of the
// cards and their tokens, at least one focused scenario per card (stats, targets, edge cases), LegalActions against
// Apply for every card, a random-games soak with decks full of these cards and AI games (Normal and Hard) that must
// never produce a refused action. Uses every card of the dataset (tests/match_all_cards.h) with the 5834 stats.
//   match_cards_rest_tests   run all tests; exit code 0 = all checks passed.

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
#include <tuple>
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

    // The cards of part F.
    char const* const kList[] = {
        "EX1_161", "EX1_578", "EX1_571", "EX1_573", "EX1_549", "EX1_531", "EX1_538", "EX1_537", "EX1_559", "EX1_279",
        "NEW1_017", "EX1_055", "EX1_058", "EX1_557", "NEW1_023", "NEW1_037", "EX1_050", "EX1_083", "EX1_103", "EX1_043",
        "EX1_057", "EX1_584", "CS2_151", "EX1_558", "NEW1_041", "EX1_577", "EX1_614", "NEW1_040", "EX1_249", "NEW1_038",
        "EX1_562", "EX1_349", "EX1_382", "EX1_355", "EX1_384", "EX1_354", "EX1_383", "EX1_091", "EX1_623", "EX1_144",
        "CS2_233", "EX1_575", "EX1_596", "EX1_607", "CS2_104"};
    char const* const kTokens[] = {"EX1_tk9", "EX1_573a", "EX1_573b", "EX1_573t", "EX1_538t", "CS2_152", "EX1_finkle",
        "EX1_tk28", "EX1_tk29", "EX1_614t", "NEW1_040t", "ds1_whelptoken", "EX1_383t"};

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

    bool OnBoard(Match const& m, int side, EntityId id)
    {
        Ids const b = TA::BoardIds(m, side);
        return std::find(b.begin(), b.end(), id) != b.end();
    }

    Strs Board(Match const& m, int side) { return TA::BoardCardIds(m, side); }
    Strs Hand(Match const& m, int side) { return TA::HandCardIds(m, side); }
    int HP(Match const& m, int side) { return m.HeroHealth(side); }
    HeroView HV(Match const& m, int side)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        return side == 0 ? v.self.hero : v.other.hero;
    }

    bool Has(Strs const& v, char const* id) { return std::find(v.begin(), v.end(), std::string(id)) != v.end(); }

    // Puts exactly one card in the hand of side and plays it.
    ActionError Cast(Match& m, int side, char const* cardId, EntityId target = 0, int choice = 0, int position = -1)
    {
        TA::SetMana(m, side, 10, 10); // every cast starts with full mana
        Ids const h = TA::SetHand(m, side, {cardId});
        return m.Apply(side, Play(h[0], target, choice, position));
    }

    // Ends side 0's turn (side 1 active with 10 mana, empty hand) / ends side 1's turn (side 0 again).
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

    EntityId Last(Match const& m, int side)
    {
        Ids const b = TA::BoardIds(m, side);
        return b.empty() ? 0 : b.back();
    }

    // ---- coverage -------------------------------------------------------------------------------

    void TestCoverage()
    {
        for (char const* id : kList)
        {
            bool const ok = Db().Supported(id);
            if (!ok)
                std::printf("  not supported: %s\n", id);
            CHECK(ok);
            CardDef const* def = Db().Find(id);
            CHECK(def && (def->type == CardType::Minion || def->type == CardType::Spell || def->type == CardType::Weapon));
        }
        for (char const* id : kTokens)
        {
            bool const ok = Db().Supported(id);
            if (!ok)
                std::printf("  not supported: %s\n", id);
            CHECK(ok);
        }
        // The 5834 stats the scripts rely on.
        CardDef const* cen = Db().Find("EX1_573");
        CHECK(cen && cen->attack == 5 && cen->health == 8 && cen->cost == 9);
        CardDef const* tk = Db().Find("EX1_tk9");
        CHECK(tk && tk->charge && tk->attack == 2 && tk->health == 2);
        CardDef const* fd = Db().Find("NEW1_023");
        CHECK(fd && fd->elusive);
        CHECK(Db().Find("NEW1_006") == nullptr); // Adrenaline Rush was never released: not in the dataset
    }

    // ---- Druid ------------------------------------------------------------------------------------

    void TestDruid()
    {
        // Naturalize: destroy a minion, the opponent draws 2 (own minions too; not a hero).
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids e = TA::SetBoard(*m, 1, {"CS2_186"});
            Ids b = TA::SetBoard(*m, 0, {"CS2_172"});
            CHECK(Cast(*m, 0, "EX1_161", Hero(*m, 1)) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_161", 0) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_161", e[0]) == ActionError::None);
            CHECK(!OnBoard(*m, 1, e[0]) && Hand(*m, 1).size() == 2 && Hand(*m, 0).empty());
            CHECK(Cast(*m, 0, "EX1_161", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty() && Hand(*m, 1).size() == 4);
        }
        // Savagery: damage equal to the hero's Attack (weapon, Claw, Spell Damage).
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids e = TA::SetBoard(*m, 1, {"CS2_186", "CS2_186"});
            CHECK(Cast(*m, 0, "EX1_578", e[0]) == ActionError::None); // 0 Attack: no damage
            CHECK_EQ(MV(*m, e[0]).health, 7);
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK(Cast(*m, 0, "EX1_578", e[0]) == ActionError::None);
            CHECK_EQ(MV(*m, e[0]).health, 4);
            TA::SetWeapon(*m, 0, "");
            CHECK(Cast(*m, 0, "CS2_005") == ActionError::None); // Claw: +2 Attack this turn
            CHECK(Cast(*m, 0, "EX1_578", e[1]) == ActionError::None);
            CHECK_EQ(MV(*m, e[1]).health, 5);
            TA::SetBoard(*m, 0, {"EX1_012"}); // Spell Damage +1
            CHECK(Cast(*m, 0, "EX1_578", e[1]) == ActionError::None);
            CHECK_EQ(MV(*m, e[1]).health, 2);
            CHECK(Cast(*m, 0, "EX1_578", Hero(*m, 1)) == ActionError::Target);
        }
        // Force of Nature: three 2/2 Treants with Charge that die at the end of the turn (not when silenced).
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            CHECK(Cast(*m, 0, "EX1_571") == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"EX1_tk9", "EX1_tk9", "EX1_tk9"}));
            Ids b = TA::BoardIds(*m, 0);
            MinionView t = MV(*m, b[0]);
            CHECK(t.attack == 2 && t.health == 2 && t.charge && t.canAttack);
            CHECK(m->Apply(0, Attack(b[0], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(Cast(*m, 0, "EX1_332", b[1]) == ActionError::None); // Silence one Treant
            m->Apply(0, End());
            CHECK(Board(*m, 0) == Strs{"EX1_tk9"} && OnBoard(*m, 0, b[1]));
            // Room: 6 minions -> one Treant; 7 minions -> refused.
            std::unique_ptr<Match> n = Fresh("DRUID");
            TA::SetBoard(*n, 0, Strs(6, "CS2_168"));
            CHECK(Cast(*n, 0, "EX1_571") == ActionError::None && Board(*n, 0).size() == 7);
            std::unique_ptr<Match> o = Fresh("DRUID");
            TA::SetBoard(*o, 0, Strs(7, "CS2_168"));
            CHECK(Cast(*o, 0, "EX1_571") == ActionError::BoardFull);
        }
        // Cenarius: +2/+2 for the other minions, or two Taunt Treants.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_573", 0, 0) == ActionError::Choice);
            CHECK(Cast(*m, 0, "EX1_573", 0, 3) == ActionError::Choice);
            CHECK(Cast(*m, 0, "EX1_573", 0, 1) == ActionError::None);
            EntityId const cen = Last(*m, 0);
            CHECK(MV(*m, b[0]).attack == 5 && MV(*m, b[0]).health == 4 && MV(*m, b[1]).attack == 4 && MV(*m, b[1]).health == 3);
            CHECK(MV(*m, cen).attack == 5 && MV(*m, cen).health == 8);
            std::unique_ptr<Match> n = Fresh("DRUID");
            TA::SetBoard(*n, 0, {"CS2_172"});
            CHECK(Cast(*n, 0, "EX1_573", 0, 2, 0) == ActionError::None);
            CHECK(Board(*n, 0) == (Strs{"EX1_573", "EX1_573t", "EX1_573t", "CS2_172"}));
            Ids nb = TA::BoardIds(*n, 0);
            CHECK(MV(*n, nb[1]).taunt && MV(*n, nb[1]).attack == 2 && MV(*n, nb[1]).health == 2 && MV(*n, nb[2]).taunt);
            std::unique_ptr<Match> o = Fresh("DRUID");
            TA::SetBoard(*o, 0, Strs(5, "CS2_172"));
            CHECK(Cast(*o, 0, "EX1_573", 0, 2) == ActionError::None);
            CHECK(Board(*o, 0).size() == 7 && Board(*o, 0).back() == "EX1_573t"); // 5 + Cenarius + one Treant
            std::unique_ptr<Match> p = Fresh("DRUID");
            TA::SetBoard(*p, 0, Strs(6, "CS2_172"));
            CHECK(Cast(*p, 0, "EX1_573", 0, 2) == ActionError::None && Board(*p, 0).size() == 7);
        }
    }

    // ---- Hunter ------------------------------------------------------------------------------------

    void TestHunter()
    {
        // Bestial Wrath: a Beast (either side's) gets +2 Attack and Immune this turn.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_168"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(Cast(*m, 0, "EX1_549", b[1]) == ActionError::Target); // a Murloc
            CHECK(Cast(*m, 0, "EX1_549", Hero(*m, 0)) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_549", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 5 && MV(*m, b[0]).immune);
            CHECK(Cast(*m, 0, "EX1_549", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).attack == 5 && MV(*m, e[0]).immune);
            CHECK(m->Apply(0, Attack(b[0], e[0])) == ActionError::Target); // Immune
            ToSide1(*m);
            CHECK(MV(*m, b[0]).attack == 3 && !MV(*m, b[0]).immune && MV(*m, e[0]).attack == 3 && !MV(*m, e[0]).immune);
        }
        // Scavenging Hyena: +2/+1 whenever a friendly Beast dies.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            Ids b = TA::SetBoard(*m, 0, {"EX1_531", "CS2_172", "CS2_168"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(Cast(*m, 0, "EX1_161", b[2]) == ActionError::None); // a Murloc: nothing
            CHECK(MV(*m, b[0]).attack == 2 && MV(*m, b[0]).health == 2);
            CHECK(Cast(*m, 0, "EX1_161", e[0]) == ActionError::None); // an enemy Beast: nothing
            CHECK(MV(*m, b[0]).attack == 2);
            CHECK(Cast(*m, 0, "EX1_161", b[1]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 4 && MV(*m, b[0]).health == 3);
            // Silenced: no gain.
            std::unique_ptr<Match> n = Fresh("HUNTER");
            Ids nb = TA::SetBoard(*n, 0, {"EX1_531", "CS2_172"});
            CHECK(Cast(*n, 0, "EX1_332", nb[0]) == ActionError::None);
            CHECK(Cast(*n, 0, "EX1_161", nb[1]) == ActionError::None);
            CHECK(MV(*n, nb[0]).attack == 2);
        }
        // Unleash the Hounds: a Hound for each enemy minion.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            CHECK(Cast(*m, 0, "EX1_538") == ActionError::Requirement); // no enemy minion
            TA::SetBoard(*m, 1, {"CS2_168", "CS2_168", "CS2_172"});
            CHECK(Cast(*m, 0, "EX1_538") == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"EX1_538t", "EX1_538t", "EX1_538t"}));
            Ids b = TA::BoardIds(*m, 0);
            MinionView h = MV(*m, b[0]);
            CHECK(h.attack == 1 && h.health == 1 && h.charge && h.canAttack);
            CHECK(m->Apply(0, Attack(b[0], Hero(*m, 1))) == ActionError::None);
            // Limited by room.
            std::unique_ptr<Match> n = Fresh("HUNTER");
            TA::SetBoard(*n, 0, Strs(5, "CS2_168"));
            TA::SetBoard(*n, 1, Strs(4, "CS2_168"));
            CHECK(Cast(*n, 0, "EX1_538") == ActionError::None);
            CHECK(Board(*n, 0).size() == 7);
            std::unique_ptr<Match> o = Fresh("HUNTER");
            TA::SetBoard(*o, 0, Strs(7, "CS2_168"));
            TA::SetBoard(*o, 1, {"CS2_168"});
            CHECK(Cast(*o, 0, "EX1_538") == ActionError::None && Board(*o, 0).size() == 7);
        }
        // Explosive Shot: 5 damage, 2 to the adjacent minions (Spell Damage on both parts).
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            Ids e = TA::SetBoard(*m, 1, {"CS2_186", "CS2_186", "CS2_186"});
            CHECK(Cast(*m, 0, "EX1_537", e[1]) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 5 && MV(*m, e[1]).health == 2 && MV(*m, e[2]).health == 5);
            CHECK(Cast(*m, 0, "EX1_537", e[0]) == ActionError::None); // an edge: one neighbor
            CHECK(MV(*m, e[0]).health == 0 || !OnBoard(*m, 1, e[0]));
            CHECK(!OnBoard(*m, 1, e[1]) && MV(*m, e[2]).health == 5);
            std::unique_ptr<Match> n = Fresh("HUNTER");
            Ids ne = TA::SetBoard(*n, 1, {"CS2_186", "CS2_186", "CS2_186"});
            TA::SetBoard(*n, 0, {"EX1_012"});
            CHECK(Cast(*n, 0, "EX1_537", ne[1]) == ActionError::None);
            CHECK(MV(*n, ne[0]).health == 4 && MV(*n, ne[1]).health == 1 && MV(*n, ne[2]).health == 4);
            CHECK(Cast(*n, 0, "EX1_537", Hero(*n, 1)) == ActionError::Target);
        }
    }

    // ---- Mage --------------------------------------------------------------------------------------

    void TestMage()
    {
        // Pyroblast: 10 damage to any character (Spell Damage adds).
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_186"});
            CHECK(Cast(*m, 0, "EX1_279", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 20);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_279", e[0]) == ActionError::None && !OnBoard(*m, 1, e[0]));
            TA::SetMana(*m, 0, 10, 10);
            TA::SetBoard(*m, 0, {"EX1_012"});
            CHECK(Cast(*m, 0, "EX1_279", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 9);
            CHECK(Cast(*m, 0, "EX1_279", 0) == ActionError::Target);
        }
        // Archmage Antonidas: a Fireball for each spell cast (not for minions, not for the opponent's spells,
        // not when silenced).
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids b = TA::SetBoard(*m, 0, {"EX1_559"});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_029"});
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            CHECK(Hand(*m, 0).empty());
            Ids h = TA::SetHand(*m, 0, {"CS2_029"});
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_029"} && HP(*m, 1) == 24);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "GAME_005") == ActionError::None);
            CHECK(Hand(*m, 0).size() == 1); // only the card of the draw... Antonidas did not react: still one Fireball
            ToSide0(*m);
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None); // Silence itself is cast first: a Fireball
            CHECK(Hand(*m, 0) == Strs{"CS2_029"});
            TA::SetHand(*m, 0, {});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None && Hand(*m, 0).empty());
        }
    }

    // ---- Neutral battlecries -----------------------------------------------------------------------

    void TestNeutralBattlecries()
    {
        // Hungry Crab: destroy a Murloc, gain +2/+2.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"CS2_168"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_182"});
            CHECK(Cast(*m, 0, "NEW1_017", e[1]) == ActionError::Target); // not a Murloc
            CHECK(Cast(*m, 0, "NEW1_017", 0) == ActionError::Target);    // a Murloc is available
            CHECK(Cast(*m, 0, "NEW1_017", e[0]) == ActionError::None);
            MinionView c = MV(*m, Last(*m, 0));
            CHECK(c.cardId == "NEW1_017" && c.attack == 3 && c.health == 4);
            CHECK(!OnBoard(*m, 1, e[0]));
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 1, {"CS2_182"});
            CHECK(Cast(*n, 0, "NEW1_017", 0) == ActionError::None); // no Murloc: plain 1/2
            MinionView d = MV(*n, Last(*n, 0));
            CHECK(d.attack == 1 && d.health == 2);
            std::unique_ptr<Match> o = Fresh();
            Ids ob = TA::SetBoard(*o, 0, {"CS2_168"});
            CHECK(Cast(*o, 0, "NEW1_017", ob[0]) == ActionError::None && Board(*o, 0) == Strs{"NEW1_017"});
        }
        // Mana Addict: +2 Attack this turn per spell.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_055"});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK_EQ(MV(*m, b[0]).attack, 3);
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK_EQ(MV(*m, b[0]).attack, 5);
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None); // a minion: nothing
            CHECK_EQ(MV(*m, b[0]).attack, 5);
            ToSide1(*m);
            CHECK_EQ(MV(*m, b[0]).attack, 1);
        }
        // Sunfury Protector: adjacent minions get Taunt.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_058", 0, 0, 1) == ActionError::None);
            Ids nb = TA::BoardIds(*m, 0);
            CHECK(nb.size() == 4 && nb[0] == b[0] && nb[2] == b[1]);
            CHECK(MV(*m, nb[0]).taunt && !MV(*m, nb[1]).taunt && MV(*m, nb[2]).taunt && !MV(*m, nb[3]).taunt);
            std::unique_ptr<Match> n = Fresh();
            Ids nbb = TA::SetBoard(*n, 0, {"CS2_168", "CS2_168"});
            CHECK(Cast(*n, 0, "EX1_058") == ActionError::None);
            CHECK(!MV(*n, nbb[0]).taunt && MV(*n, nbb[1]).taunt);
            std::unique_ptr<Match> o = Fresh();
            CHECK(Cast(*o, 0, "EX1_058") == ActionError::None && Board(*o, 0).size() == 1);
        }
        // Faerie Dragon: no spell or hero power can target it; battlecries and attacks can.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids e = TA::SetBoard(*m, 1, {"NEW1_023"});
            CHECK(Cast(*m, 0, "EX1_161", e[0]) == ActionError::Target);
            CHECK(m->Apply(0, Power(*m, 0, e[0])) == ActionError::Target);
            CHECK(Cast(*m, 0, "CS2_188", e[0]) == ActionError::None); // Abusive Sergeant's battlecry
            CHECK(MV(*m, e[0]).attack == 5);
            CHECK(Cast(*m, 0, "CS2_032") == ActionError::None); // Flamestrike hits it
            CHECK(!OnBoard(*m, 1, e[0]));
            std::unique_ptr<Match> n = Fresh("MAGE");
            Ids nb = TA::SetBoard(*n, 0, {"NEW1_023"});
            CHECK(Cast(*n, 0, "CS2_029", nb[0]) == ActionError::Target);
            CHECK(n->Apply(0, Attack(nb[0], Hero(*n, 1))) == ActionError::None);
            CHECK_EQ(HP(*n, 1), 27);
        }
        // Coldlight Oracle: each player draws 2.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_050") == ActionError::None);
            CHECK(Hand(*m, 0).size() == 2 && Hand(*m, 1).size() == 2);
        }
        // Tinkmaster Overspark: another random minion becomes a Devilsaur or a Squirrel.
        {
            std::set<std::string> seen;
            int ownHit = 0;
            int enemyHit = 0;
            for (uint32_t seed = 1; seed <= 40; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                Ids b = TA::SetBoard(*m, 0, {"CS2_172"});
                Ids e = TA::SetBoard(*m, 1, {"CS2_186"});
                CHECK(Cast(*m, 0, "EX1_083") == ActionError::None);
                Strs const own = Board(*m, 0);
                Strs const en = Board(*m, 1);
                CHECK(own.size() == 2 && en.size() == 1 && own[1] == "EX1_083");
                int changed = 0;
                for (std::string const& id : {own[0], en[0]})
                    if (id == "EX1_tk29" || id == "EX1_tk28")
                    {
                        ++changed;
                        seen.insert(id);
                    }
                CHECK_EQ(changed, 1);
                if (own[0] != "CS2_172")
                {
                    ++ownHit;
                    MinionView v = MV(*m, b[0]);
                    CHECK((v.attack == 5 && v.health == 5) || (v.attack == 1 && v.health == 1));
                }
                else
                {
                    ++enemyHit;
                    MinionView v = MV(*m, e[0]);
                    CHECK((v.attack == 5 && v.health == 5) || (v.attack == 1 && v.health == 1));
                }
            }
            CHECK(seen.size() == 2 && ownHit > 0 && enemyHit > 0);
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_083") == ActionError::None && Board(*n, 0) == Strs{"EX1_083"});
        }
        // Coldlight Seer: ALL other Murlocs get +2 Health.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_172"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168"});
            CHECK(Cast(*m, 0, "EX1_103") == ActionError::None);
            CHECK(MV(*m, b[0]).health == 3 && MV(*m, b[0]).attack == 2);
            CHECK(MV(*m, e[0]).health == 3);
            CHECK(MV(*m, b[1]).health == 2);
            MinionView seer = MV(*m, Last(*m, 0));
            CHECK(seer.attack == 2 && seer.health == 3);
        }
        // Twilight Drake: +1 Health per card in hand.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids h = TA::SetHand(*m, 0, {"EX1_043", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            MinionView d = MV(*m, Last(*m, 0));
            CHECK(d.attack == 4 && d.health == 4 && d.maxHealth == 4);
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_043") == ActionError::None);
            CHECK(MV(*n, Last(*n, 0)).health == 1);
        }
        // Ancient Brewmaster: return a friendly minion to your hand.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168"});
            CHECK(Cast(*m, 0, "EX1_057", e[0]) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_057", 0) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_057", b[0]) == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_182"} && Board(*m, 0) == Strs{"EX1_057"});
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_057", 0) == ActionError::None && Board(*n, 0) == Strs{"EX1_057"});
        }
        // Ancient Mage: adjacent minions get Spell Damage +1 (not itself).
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_168", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_584", 0, 0, 1) == ActionError::None);
            Ids nb = TA::BoardIds(*m, 0);
            CHECK(nb.size() == 4);
            CHECK(MV(*m, nb[0]).spellDamage == 1 && MV(*m, nb[1]).spellDamage == 0 && MV(*m, nb[2]).spellDamage == 1 &&
                MV(*m, nb[3]).spellDamage == 0);
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None); // 6 + 2
            CHECK_EQ(HP(*m, 1), 22);
            CHECK(Cast(*m, 0, "EX1_332", nb[0]) == ActionError::None); // Silence one: only one bonus left
            CHECK(MV(*m, nb[0]).spellDamage == 0);
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 15);
            (void)b;
            std::unique_ptr<Match> n = Fresh();
            Ids nn = TA::SetBoard(*n, 0, {"CS2_168", "CS2_168"});
            CHECK(Cast(*n, 0, "EX1_584") == ActionError::None);
            CHECK(MV(*n, nn[1]).spellDamage == 1 && MV(*n, nn[0]).spellDamage == 0);
            // Spell Damage granted after a Silence counts (review): a silenced Kobold Geomancer next to
            // a new Ancient Mage has Spell Damage +1.
            std::unique_ptr<Match> k = Fresh();
            Ids kb = TA::SetBoard(*k, 0, {"CS2_142"});
            CHECK(Cast(*k, 0, "EX1_332", kb[0]) == ActionError::None);
            CHECK(MV(*k, kb[0]).spellDamage == 0);
            CHECK(Cast(*k, 0, "EX1_584", 0, 0, 1) == ActionError::None);
            CHECK(MV(*k, kb[0]).spellDamage == 1);
            CHECK(Cast(*k, 0, "CS2_029", Hero(*k, 1)) == ActionError::None); // 6 + 1
            CHECK_EQ(HP(*k, 1), 23);
        }
        // Silver Hand Knight: a 2/2 Squire.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "CS2_151") == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_151", "CS2_152"}));
            MinionView s = MV(*m, Last(*m, 0));
            CHECK(s.attack == 2 && s.health == 2);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, Strs(6, "CS2_168"));
            CHECK(Cast(*n, 0, "CS2_151") == ActionError::None && Board(*n, 0).size() == 7);
        }
        // Harrison Jones: destroy the opponent's weapon, draw cards equal to its Durability.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetWeapon(*m, 1, "CS2_106"); // 3/2
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK(Cast(*m, 0, "EX1_558") == ActionError::None);
            CHECK(HV(*m, 1).weaponId == 0 && HV(*m, 0).weaponId != 0);
            CHECK_EQ(Hand(*m, 0).size(), 2);
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_558") == ActionError::None && Hand(*n, 0).empty());
        }
        // Stampeding Kodo: destroys a random enemy minion with 2 or less Attack.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_182", "CS2_168", "CS2_172"});
            CHECK(Cast(*m, 0, "NEW1_041") == ActionError::None);
            CHECK(OnBoard(*m, 1, e[0]) && !OnBoard(*m, 1, e[1]) && OnBoard(*m, 1, e[2]));
            std::unique_ptr<Match> n = Fresh();
            Ids ne = TA::SetBoard(*n, 1, {"CS2_182", "CS2_172"});
            CHECK(Cast(*n, 0, "NEW1_041") == ActionError::None && Board(*n, 1).size() == 2);
            std::unique_ptr<Match> o = Fresh();
            Ids oe = TA::SetBoard(*o, 1, {"EX1_010"}); // Stealthed minions are not targeted: they count
            CHECK(Cast(*o, 0, "NEW1_041") == ActionError::None && !OnBoard(*o, 1, oe[0]));
            (void)ne;
        }
        // The Beast: a Finkle Einhorn for the opponent.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_577"});
            CHECK(Cast(*m, 0, "EX1_161", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty() && Board(*m, 1) == Strs{"EX1_finkle"});
            MinionView f = MV(*m, Last(*m, 1));
            CHECK(f.attack == 3 && f.health == 3);
            std::unique_ptr<Match> n = Fresh();
            Ids nb = TA::SetBoard(*n, 0, {"EX1_577"});
            TA::SetBoard(*n, 1, Strs(7, "CS2_168"));
            CHECK(Cast(*n, 0, "EX1_161", nb[0]) == ActionError::None);
            CHECK(Board(*n, 1).size() == 7 && !Has(Board(*n, 1), "EX1_finkle"));
            // Attacked to death: the Finkle comes in place of the Beast on the other side.
            std::unique_ptr<Match> o = Fresh();
            Ids ob = TA::SetBoard(*o, 0, {"EX1_577"});
            Ids oe = TA::SetBoard(*o, 1, {"CS2_186", "CS2_186"});
            TA::SetMinion(*o, ob[0], 9, 3, 7);
            ToSide1(*o);
            TA::ReadyAll(*o);
            CHECK(o->Apply(1, Attack(oe[0], ob[0])) == ActionError::None);
            // The Beast (9 Attack) kills the attacking golem too; the Finkle joins the golem's side.
            CHECK(Board(*o, 0).empty() && Board(*o, 1) == (Strs{"CS2_186", "EX1_finkle"}));
        }
        // Illidan Stormrage: a 2/1 Flame of Azzinoth for every card played (not for himself).
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_614"});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"EX1_614", "EX1_614t"}));
            MinionView f = MV(*m, Last(*m, 0));
            CHECK(f.attack == 2 && f.health == 1);
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            // The new Flame appears right of Illidan (a triggered summon goes next to its source).
            CHECK(Board(*m, 0) == (Strs{"EX1_614", "EX1_614t", "EX1_614t", "CS2_168"}));
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_614") == ActionError::None && Board(*n, 0) == Strs{"EX1_614"});
            std::unique_ptr<Match> o = Fresh();
            TA::SetBoard(*o, 0, {"EX1_614", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(Cast(*o, 0, "CS2_168") == ActionError::None && Board(*o, 0).size() == 7);
            ToSide1(*o);
            CHECK(Cast(*o, 1, "GAME_005") == ActionError::None && Board(*o, 0).size() == 7); // the enemy's cards: none
        }
        // Onyxia: whelps until the board is full.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_562") == ActionError::None);
            CHECK(Board(*m, 0).size() == 7 && Board(*m, 0)[0] == "EX1_562");
            MinionView w = MV(*m, Last(*m, 0));
            CHECK(w.cardId == "ds1_whelptoken" && w.attack == 1 && w.health == 1);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, {"CS2_182", "CS2_182", "CS2_182"});
            CHECK(Cast(*n, 0, "EX1_562", 0, 0, 1) == ActionError::None);
            Strs b = Board(*n, 0);
            CHECK(b.size() == 7 && b[0] == "CS2_182" && b[1] == "EX1_562" && b[2] == "ds1_whelptoken" && b[6] == "CS2_182");
            std::unique_ptr<Match> o = Fresh();
            TA::SetBoard(*o, 0, Strs(6, "CS2_182"));
            CHECK(Cast(*o, 0, "EX1_562") == ActionError::None && Board(*o, 0).size() == 7);
        }
    }

    // ---- triggered neutral minions -----------------------------------------------------------------

    void TestNeutralTriggers()
    {
        // Nat Pagle: at the start of your turn a 50% chance of an extra card (never on the other side's turn).
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_557"});
            TA::SetHand(*m, 0, {});
            std::set<int> seen;
            ToSide1(*m);
            CHECK(Hand(*m, 0).empty());
            for (int i = 0; i < 40; ++i)
            {
                TA::SetDeck(*m, 0, Strs(20, "CS2_168"));
                TA::SetDeck(*m, 1, Strs(20, "CS2_168"));
                TA::SetHand(*m, 0, {});
                m->Apply(1, End()); // side 0's turn: the regular draw and maybe one more
                seen.insert(static_cast<int>(Hand(*m, 0).size()));
                TA::SetHand(*m, 1, {});
                TA::SetHand(*m, 0, {});
                m->Apply(0, End());
                CHECK(Hand(*m, 0).empty());
                if (m->GetPhase() != Phase::Play)
                    break;
            }
            CHECK(seen == (std::set<int>{1, 2}));
        }
        // Master Swordsmith: +1 Attack for another random friendly minion at the end of your turn.
        {
            std::set<int> got;
            for (uint32_t seed = 1; seed <= 24; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                Ids b = TA::SetBoard(*m, 0, {"NEW1_037", "CS2_172", "CS2_172"});
                m->Apply(0, End());
                int const a1 = MV(*m, b[1]).attack;
                int const a2 = MV(*m, b[2]).attack;
                CHECK(MV(*m, b[0]).attack == 1 && a1 + a2 == 7);
                got.insert(a1 == 4 ? 1 : 2);
                m->Apply(1, End());
                m->Apply(0, End());
                m->Apply(1, End());
                CHECK(MV(*m, b[1]).attack + MV(*m, b[2]).attack == 8); // one more of its own turn ends: +1, not +2
            }
            CHECK(got.size() == 2);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, {"NEW1_037"});
            CHECK(n->Apply(0, End()) == ActionError::None);
        }
        // Hogger: a 2/2 Gnoll with Taunt at the end of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"NEW1_040"});
            m->Apply(0, End());
            CHECK(Board(*m, 0) == (Strs{"NEW1_040", "NEW1_040t"}));
            MinionView g = MV(*m, Last(*m, 0));
            CHECK(g.taunt && g.attack == 2 && g.health == 2);
            m->Apply(1, End());
            CHECK(Board(*m, 0).size() == 2);
            m->Apply(0, End());
            CHECK(Board(*m, 0).size() == 3);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, {"NEW1_040", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(n->Apply(0, End()) == ActionError::None && Board(*n, 0).size() == 7);
        }
        // Baron Geddon: 2 damage to ALL other characters at the end of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_249", "CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182", "CS2_168"});
            m->Apply(0, End());
            CHECK(MV(*m, b[0]).health == 5 && MV(*m, b[1]).health == 3 && MV(*m, e[0]).health == 3 && !OnBoard(*m, 1, e[1]));
            CHECK(HP(*m, 0) == 28 && HP(*m, 1) == 28);
            m->Apply(1, End()); // the other side's turn end: nothing
            CHECK(HP(*m, 0) == 28 && MV(*m, b[1]).health == 3);
            // Silenced: no effect.
            std::unique_ptr<Match> n = Fresh();
            Ids nb = TA::SetBoard(*n, 0, {"EX1_249"});
            Cast(*n, 0, "EX1_332", nb[0]);
            n->Apply(0, End());
            CHECK(HP(*n, 0) == 30 && HP(*n, 1) == 30);
        }
        // Gruul: +1/+1 at the end of EACH turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"NEW1_038"});
            m->Apply(0, End());
            CHECK(MV(*m, b[0]).attack == 8 && MV(*m, b[0]).health == 8);
            m->Apply(1, End());
            CHECK(MV(*m, b[0]).attack == 9 && MV(*m, b[0]).health == 9);
        }
        // Mana Tide Totem: draw at the end of your turn only.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetBoard(*m, 0, {"EX1_575"});
            m->Apply(0, End());
            CHECK_EQ(Hand(*m, 0).size(), 1);
            TA::SetHand(*m, 0, {});
            m->Apply(1, End()); // side 0 draws its regular card only
            CHECK_EQ(Hand(*m, 0).size(), 1);
            TA::SetHand(*m, 0, {});
            m->Apply(0, End());
            CHECK_EQ(Hand(*m, 0).size(), 1);
        }
    }

    // ---- Paladin -----------------------------------------------------------------------------------

    void TestPaladin()
    {
        // Divine Favor: draw until you hold as many cards as the opponent.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetHand(*m, 1, Strs(5, "GAME_005"));
            Ids h = TA::SetHand(*m, 0, {"EX1_349", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 5);
            Ids h2 = TA::SetHand(*m, 0, {"EX1_349", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(m->Apply(0, Play(h2[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 5); // already equal
            std::unique_ptr<Match> n = Fresh("PALADIN");
            CHECK(Cast(*n, 0, "EX1_349") == ActionError::None && Hand(*n, 0).empty());
            std::unique_ptr<Match> o = Fresh("PALADIN");
            TA::SetHand(*o, 1, Strs(10, "GAME_005"));
            CHECK(Cast(*o, 0, "EX1_349") == ActionError::None && Hand(*o, 0).size() == 10);
        }
        // Aldor Peacekeeper: an enemy minion's Attack becomes 1.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_186"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_186"});
            CHECK(Cast(*m, 0, "EX1_382", b[0]) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_382", Hero(*m, 1)) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_382", 0) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_382", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).attack == 1 && MV(*m, e[0]).health == 7);
            std::unique_ptr<Match> n = Fresh("PALADIN");
            TA::SetBoard(*n, 0, {"CS2_186"});
            CHECK(Cast(*n, 0, "EX1_382", 0) == ActionError::None); // no enemy minion
        }
        // Blessed Champion: double a minion's Attack.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_186"});
            CHECK(Cast(*m, 0, "EX1_355", b[0]) == ActionError::None && MV(*m, b[0]).attack == 6);
            CHECK(Cast(*m, 0, "EX1_355", b[0]) == ActionError::None && MV(*m, b[0]).attack == 12);
            CHECK(Cast(*m, 0, "EX1_355", e[0]) == ActionError::None && MV(*m, e[0]).attack == 14);
            CHECK(Cast(*m, 0, "EX1_355", Hero(*m, 0)) == ActionError::Target);
            TA::SetMinion(*m, b[1], 0, 5, 5);
            CHECK(Cast(*m, 0, "EX1_355", b[1]) == ActionError::None && MV(*m, b[1]).attack == 0);
        }
        // Avenging Wrath: 8 missiles split among the enemy characters; Spell Damage adds missiles.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            CHECK(Cast(*m, 0, "EX1_384") == ActionError::None && HP(*m, 1) == 22 && HP(*m, 0) == 30);
            std::unique_ptr<Match> n = Fresh("PALADIN");
            TA::SetBoard(*n, 0, {"EX1_012"});
            CHECK(Cast(*n, 0, "EX1_384") == ActionError::None && HP(*n, 1) == 21);
            std::unique_ptr<Match> o = Fresh("PALADIN");
            Ids oe = TA::SetBoard(*o, 1, {"CS2_168", "CS2_168"});
            CHECK(Cast(*o, 0, "EX1_384") == ActionError::None);
            CHECK(!OnBoard(*o, 1, oe[0]) && !OnBoard(*o, 1, oe[1]) && HP(*o, 1) == 24); // 2 + 6 missiles
            std::unique_ptr<Match> p = Fresh("PALADIN");
            Ids pe = TA::SetBoard(*p, 1, {"CS2_186"});
            TA::SetBoard(*p, 0, {"CS2_182"});
            CHECK(Cast(*p, 0, "EX1_384") == ActionError::None);
            CHECK_EQ((30 - HP(*p, 1)) + (7 - std::max(0, MV(*p, pe[0]).health)), 8);
        }
        // Lay on Hands: restore 8 and draw 3.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetHero(*m, 0, 10, 0);
            CHECK(Cast(*m, 0, "EX1_354", Hero(*m, 0)) == ActionError::None);
            CHECK(HP(*m, 0) == 18 && Hand(*m, 0).size() == 3);
            Ids b = TA::SetBoard(*m, 1, {"CS2_186"});
            TA::SetMinion(*m, b[0], 7, 2, 7);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_354", b[0]) == ActionError::None && MV(*m, b[0]).health == 7);
            TA::SetMana(*m, 0, 10, 10);
            TA::SetHero(*m, 0, 28, 0);
            CHECK(Cast(*m, 0, "EX1_354", Hero(*m, 0)) == ActionError::None && HP(*m, 0) == 30);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_354", 0) == ActionError::Target);
        }
        // Tirion Fordring: Divine Shield, Taunt; the deathrattle equips a 5/3 Ashbringer (replacing a weapon).
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"EX1_383"});
            MinionView t = MV(*m, b[0]);
            CHECK(t.taunt && t.divineShield && t.attack == 6 && t.health == 6);
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK(Cast(*m, 0, "EX1_161", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
            HeroView h = HV(*m, 0);
            CHECK(h.weaponCardId == "EX1_383t" && h.weaponAttack == 5 && h.weaponDurability == 3);
            // Killed on the opponent's turn.
            std::unique_ptr<Match> n = Fresh("PALADIN");
            Ids nb = TA::SetBoard(*n, 0, {"EX1_383"});
            TA::SetMinion(*n, nb[0], 6, 1, 6);
            Ids ne = TA::SetBoard(*n, 1, {"CS2_182"});
            ToSide1(*n);
            TA::ReadyAll(*n);
            CHECK(n->Apply(1, Attack(ne[0], nb[0])) == ActionError::None); // Divine Shield pops
            CHECK(OnBoard(*n, 0, nb[0]) && !MV(*n, nb[0]).divineShield);
        }
    }

    // ---- Priest, Rogue, Shaman, Warlock, Warrior --------------------------------------------------

    void TestOtherClasses()
    {
        // Cabal Shadow Priest: take control of an enemy minion with 2 or less Attack.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetBoard(*m, 0, {"CS2_172"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_172"});
            CHECK(Cast(*m, 0, "EX1_091", e[1]) == ActionError::Target); // 3 Attack
            CHECK(Cast(*m, 0, "EX1_091", 0) == ActionError::Target);    // a valid target exists
            CHECK(Cast(*m, 0, "EX1_091", e[0]) == ActionError::None);
            CHECK(OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]) && Board(*m, 0).size() == 3);
            CHECK(!MV(*m, e[0]).canAttack);
            // Room: 5 minions + Cabal + the stolen one = 7.
            std::unique_ptr<Match> n = Fresh("PRIEST");
            TA::SetBoard(*n, 0, Strs(5, "CS2_172"));
            Ids ne = TA::SetBoard(*n, 1, {"CS2_168"});
            CHECK(Cast(*n, 0, "EX1_091", ne[0]) == ActionError::None && Board(*n, 0).size() == 7 && Board(*n, 1).empty());
            // No room: 6 minions + Cabal. The battlecry has no valid target then.
            std::unique_ptr<Match> o = Fresh("PRIEST");
            TA::SetBoard(*o, 0, Strs(6, "CS2_172"));
            Ids oe = TA::SetBoard(*o, 1, {"CS2_168"});
            CHECK(Cast(*o, 0, "EX1_091", oe[0]) == ActionError::Target);
            CHECK(Cast(*o, 0, "EX1_091", 0) == ActionError::None && Board(*o, 0).size() == 7 && Board(*o, 1).size() == 1);
            // Nothing to steal: plays without a target; Stealthed enemies are not targets.
            std::unique_ptr<Match> p = Fresh("PRIEST");
            Ids pe = TA::SetBoard(*p, 1, {"EX1_010", "CS2_182"});
            CHECK(Cast(*p, 0, "EX1_091", pe[0]) == ActionError::Target);
            CHECK(Cast(*p, 0, "EX1_091", 0) == ActionError::None);
        }
        // Temple Enforcer: a friendly minion gets +3 Health.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(Cast(*m, 0, "EX1_623", e[0]) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_623", 0) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_623", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).health == 5 && MV(*m, b[0]).maxHealth == 5 && MV(*m, b[0]).attack == 3);
            std::unique_ptr<Match> n = Fresh("PRIEST");
            CHECK(Cast(*n, 0, "EX1_623", 0) == ActionError::None);
        }
        // Shadowstep: a friendly minion returns to the hand and costs (2) less (never below 0).
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids b = TA::SetBoard(*m, 0, {"CS2_186", "CS2_168"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_186"});
            CHECK(Cast(*m, 0, "EX1_144", e[0]) == ActionError::Target);
            TA::SetMinion(*m, b[0], 12, 3, 7);
            CHECK(Cast(*m, 0, "EX1_144", b[0]) == ActionError::None);
            Ids h = TA::HandIds(*m, 0);
            CHECK(h.size() == 1 && Hand(*m, 0) == Strs{"CS2_186"} && TA::HandCost(*m, h[0]) == 5);
            CHECK(Cast(*m, 0, "EX1_144", b[1]) == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_168"} && TA::HandCost(*m, TA::HandIds(*m, 0)[0]) == 0);
            CHECK(Cast(*m, 0, "EX1_144", 0) == ActionError::Target);
            // The re-played minion has fresh stats.
            std::unique_ptr<Match> n = Fresh("ROGUE");
            Ids nb = TA::SetBoard(*n, 0, {"CS2_186"});
            Cast(*n, 0, "EX1_144", nb[0]);
            Ids h2 = TA::HandIds(*n, 0);
            CHECK(n->Apply(0, Play(h2[0])) == ActionError::None);
            CHECK(MV(*n, Last(*n, 0)).attack == 7 && n->ViewFor(0, 0).self.hero.mana == 5);
        }
        // Blade Flurry: destroy the weapon, its damage to all enemies (Spell Damage adds).
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_186", "CS2_168"});
            CHECK(Cast(*m, 0, "CS2_233") == ActionError::Requirement);
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK(Cast(*m, 0, "CS2_233") == ActionError::None);
            CHECK(HV(*m, 0).weaponId == 0 && HP(*m, 0) == 30 && HP(*m, 1) == 27);
            CHECK(MV(*m, e[0]).health == 4 && !OnBoard(*m, 1, e[1]) && MV(*m, b[0]).health == 2);
            std::unique_ptr<Match> n = Fresh("ROGUE");
            TA::SetBoard(*n, 0, {"EX1_012"});
            TA::SetWeapon(*n, 0, "CS2_106");
            CHECK(Cast(*n, 0, "CS2_233") == ActionError::None && HP(*n, 1) == 26);
            // LegalActions: not offered without a weapon.
            std::unique_ptr<Match> o = Fresh("ROGUE");
            Ids h = TA::SetHand(*o, 0, {"CS2_233"});
            bool offered = false;
            for (Action const& a : o->LegalActions(0))
                offered = offered || (a.type == ActionType::Play && a.source == h[0]);
            CHECK(!offered);
        }
        // Demonfire: 2 damage to a minion; a friendly Demon gets +2/+2 instead.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"CS2_065", "CS2_172"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_065", "CS2_182"});
            CHECK(Cast(*m, 0, "EX1_596", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[0]).health == 5);
            CHECK(Cast(*m, 0, "EX1_596", e[0]) == ActionError::None && MV(*m, e[0]).health == 1);
            CHECK(Cast(*m, 0, "EX1_596", e[1]) == ActionError::None && MV(*m, e[1]).health == 3);
            CHECK(Cast(*m, 0, "EX1_596", b[1]) == ActionError::None && !OnBoard(*m, 0, b[1])); // friendly non-Demon
            CHECK(Cast(*m, 0, "EX1_596", Hero(*m, 1)) == ActionError::Target);
            TA::SetBoard(*m, 0, {"EX1_012"});
            CHECK(Cast(*m, 0, "EX1_596", e[0]) == ActionError::None && !OnBoard(*m, 1, e[0]));
        }
        // Inner Rage: 1 damage and +2 Attack.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_607", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 6 && MV(*m, b[0]).health == 4);
            CHECK(Cast(*m, 0, "EX1_607", b[1]) == ActionError::None && !OnBoard(*m, 0, b[1]));
            TA::SetBoard(*m, 1, {"CS2_182"});
            Ids e = TA::BoardIds(*m, 1);
            TA::SetBoard(*m, 0, {"EX1_012"});
            CHECK(Cast(*m, 0, "EX1_607", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).attack == 6 && MV(*m, e[0]).health == 3);
            CHECK(Cast(*m, 0, "EX1_607", Hero(*m, 1)) == ActionError::Target);
        }
        // Rampage: +3/+3 for a damaged minion only.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_182"});
            TA::SetMinion(*m, b[0], 3, 1, 2);
            CHECK(Cast(*m, 0, "CS2_104", b[1]) == ActionError::Target);
            CHECK(Cast(*m, 0, "CS2_104", Hero(*m, 0)) == ActionError::Target);
            CHECK(Cast(*m, 0, "CS2_104", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 6 && MV(*m, b[0]).health == 4 && MV(*m, b[0]).maxHealth == 5);
            CHECK(Cast(*m, 0, "CS2_104", b[0]) == ActionError::None); // still damaged
            CHECK(MV(*m, b[0]).attack == 9 && MV(*m, b[0]).health == 7);
        }
    }

    // ---- LegalActions agrees with Apply -------------------------------------------------------------

    std::unique_ptr<Match> BuildAgreement(char const* card, EntityId& cardId)
    {
        std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", 77);
        Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_168", "CS2_065", "NEW1_023"});
        Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_172", "EX1_010", "CS2_186", "CS2_182"});
        TA::SetMinion(*m, b[0], 3, 1, 2);
        TA::SetMinion(*m, e[1], 3, 1, 2);
        TA::SetWeapon(*m, 0, "CS2_106");
        TA::SetWeapon(*m, 1, "CS2_106");
        TA::SetHand(*m, 1, {"GAME_005", "GAME_005", "CS2_168"});
        Ids h = TA::SetHand(*m, 0, {card, "CS2_168", "CS2_168"});
        cardId = h[0];
        return m;
    }

    void TestLegalActionsAgree()
    {
        int plays = 0;
        for (char const* card : kList)
        {
            EntityId src = 0;
            std::unique_ptr<Match> base = BuildAgreement(card, src);
            if (!base)
                return;
            std::set<std::tuple<EntityId, int>> legal; // target, choice
            for (Action const& a : base->LegalActions(0))
                if (a.type == ActionType::Play && a.source == src)
                    legal.insert(std::make_tuple(a.target, a.choice));
            std::vector<EntityId> targets = {0, Hero(*base, 0), Hero(*base, 1)};
            for (int s = 0; s < 2; ++s)
                for (EntityId id : TA::BoardIds(*base, s))
                    targets.push_back(id);
            for (EntityId t : targets)
                for (int ch = 0; ch <= 2; ++ch)
                {
                    EntityId s2 = 0;
                    std::unique_ptr<Match> m = BuildAgreement(card, s2);
                    ActionError const err = m->Apply(0, Play(s2, t, ch));
                    bool const isLegal = legal.count(std::make_tuple(t, ch)) > 0;
                    if ((err == ActionError::None) != isLegal)
                        std::printf("  disagree: %s target %u choice %d: error %s, legal %d\n", card, t, ch,
                            ActionErrorCode(err), isLegal ? 1 : 0);
                    CHECK((err == ActionError::None) == isLegal);
                    if (err == ActionError::None)
                    {
                        ++plays;
                        CHECK(!Capped(*m));
                    }
                }
            CHECK(!legal.empty()); // every card of the list is playable somewhere in this position
        }
        std::printf("  legal plays checked against Apply: %d\n", plays);
    }

    // ---- randomized robustness ---------------------------------------------------------------------------

    std::set<std::string> const& TokenIds()
    {
        static std::set<std::string> const ids = {"CS2_050", "CS2_051", "CS2_052", "CS2_082", "CS2_101t", "CS2_152",
            "CS2_boar", "CS2_mirror", "CS2_tk1", "EX1_025t", "EX1_110t", "EX1_130a", "EX1_160t", "EX1_165a", "EX1_165b",
            "EX1_165t1", "EX1_165t2", "EX1_317t", "EX1_345t", "EX1_383t", "EX1_506a", "EX1_534t", "EX1_554t", "EX1_598",
            "EX1_finkle", "EX1_tk11", "EX1_tk28", "EX1_tk29", "EX1_tk34", "EX1_tk9", "GAME_002", "Mekka1", "Mekka2",
            "Mekka3", "Mekka4", "NEW1_009", "NEW1_032", "NEW1_033", "NEW1_034", "hexfrog", "skele11", "skele21", "tt_010a"};
        return ids;
    }

    std::vector<std::string> DeckPool(std::string const& heroClass, bool onlyMine)
    {
        std::set<std::string> mine(std::begin(kList), std::end(kList));
        std::vector<std::string> pool;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            std::string const type = r.type;
            if (type != "MINION" && type != "SPELL" && type != "WEAPON")
                continue;
            if (TokenIds().count(r.id))
                continue;
            if (std::string(r.rarity).empty() && !mine.count(r.id))
                continue;
            if (std::string(r.cls) != heroClass && std::string(r.cls) != "NEUTRAL")
                continue;
            if (onlyMine && !mine.count(r.id))
                continue;
            if (!Db().Supported(r.id))
                continue;
            pool.push_back(r.id);
        }
        return pool;
    }

    // 30 cards: about half from the part F cards of the class, at most 2 copies, at most 1 legendary.
    std::vector<std::string> RandomDeck(std::string const& heroClass, std::mt19937& rng)
    {
        std::vector<std::string> const all = DeckPool(heroClass, false);
        std::vector<std::string> const mine = DeckPool(heroClass, true);
        std::vector<std::string> deck;
        std::map<std::string, int> copies;
        int guard = 0;
        while (deck.size() < 30 && guard++ < 100000)
        {
            std::vector<std::string> const& pool = (rng() % 2 == 0 && !mine.empty()) ? mine : all;
            std::string const& id = pool[rng() % pool.size()];
            CardDef const* def = Db().Find(id);
            int const limit = (def && def->rarity == "LEGENDARY") ? 1 : 2;
            if (copies[id] >= limit)
                continue;
            ++copies[id];
            deck.push_back(id);
        }
        return deck;
    }

    struct Stats
    {
        int games = 0;
        int ended = 0;
        int actions = 0;
        int failures = 0;
        int capped = 0;
        int refusals = 0;
        std::map<std::string, int> played;
    };

    void CheckInvariants(Match const& m, Stats& st, uint32_t seed, int step)
    {
        for (int s = 0; s < 2; ++s)
        {
            MatchView const v = m.ViewFor(s, m.LastEventSeq());
            bool const ok = v.self.board.size() <= static_cast<size_t>(kMaxBoard) && v.self.hand.size() <= static_cast<size_t>(kMaxHand) &&
                v.self.hero.maxMana <= kMaxMana && v.self.hero.mana <= kMaxMana && v.self.hero.health <= v.self.hero.maxHealth;
            if (!ok)
            {
                ++st.failures;
                std::printf("  invariant broken: seed %u step %d side %d\n", seed, step, s);
            }
            for (MinionView const& mv : v.self.board)
                if (mv.health <= 0 && m.GetPhase() != Phase::Over)
                {
                    ++st.failures;
                    std::printf("  dead minion on the board: seed %u step %d %s\n", seed, step, mv.cardId.c_str());
                }
        }
        if (Capped(m))
        {
            ++st.capped;
            std::printf("  capped: seed %u step %d\n", seed, step);
        }
    }

    std::unique_ptr<Match> MakeGame(uint32_t seed, std::string const& c0, std::string const& c1, std::mt19937& rng)
    {
        MatchSetup setup;
        setup.seed = seed;
        setup.firstSide = -1;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = c0;
        setup.sides[0].deck = RandomDeck(c0, rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = c1;
        setup.sides[1].deck = RandomDeck(c1, rng);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
        if (!m)
            std::printf("  create failed (seed %u %s vs %s): %s\n", seed, c0.c_str(), c1.c_str(), error.c_str());
        return m;
    }

    void RandomGame(uint32_t seed, std::string const& c0, std::string const& c1, Stats& st)
    {
        std::mt19937 rng(seed * 2654435761u + 12345u);
        std::unique_ptr<Match> m = MakeGame(seed, c0, c1, rng);
        ++st.games;
        if (!m)
        {
            ++st.failures;
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
        int step = 0;
        for (; step < 400 && m->GetPhase() == Phase::Play; ++step)
        {
            bool picked = false;
            for (int s = 0; s < 2 && !picked; ++s)
            {
                MatchView const v = m->ViewFor(s, m->LastEventSeq());
                if (v.pickPending && !v.pick.empty())
                {
                    if (m->Apply(s, Choose(v.pick[rng() % v.pick.size()].id)) != ActionError::None)
                        ++st.failures;
                    picked = true;
                }
            }
            if (picked)
                continue;
            int const side = m->ActiveSide();
            std::vector<Action> legal = m->LegalActions(side);
            Action a;
            size_t const pick = rng() % (legal.size() + 1);
            if (pick == legal.size())
                a = End();
            else
            {
                a = legal[pick];
                if (a.type == ActionType::Play)
                {
                    MatchView const v = m->ViewFor(side, m->LastEventSeq());
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
            ActionError const e = m->Apply(side, a);
            if (e != ActionError::None)
            {
                ++st.failures;
                std::printf("  action refused (%s): seed %u step %d type %d source %u target %u choice %d pos %d\n",
                    ActionErrorCode(e), seed, step, static_cast<int>(a.type), a.source, a.target, a.choice, a.position);
                break;
            }
            ++st.actions;
            CheckInvariants(*m, st, seed, step);
        }
        if (m->GetPhase() == Phase::Over)
            ++st.ended;
    }

    void TestRandomGames()
    {
        char const* const classes[] = {"WARRIOR", "SHAMAN", "ROGUE", "PALADIN", "HUNTER", "DRUID", "WARLOCK", "MAGE", "PRIEST"};
        for (char const* c : classes)
        {
            CHECK(DeckPool(c, false).size() >= 20);
            CHECK(DeckPool(c, true).size() >= 8);
        }
        Stats st;
        for (int ci = 0; ci < 9; ++ci)
            for (uint32_t k = 0; k < static_cast<uint32_t>(g_gamesPerClass); ++k)
            {
                uint32_t const seed = 5000 + static_cast<uint32_t>(ci) * 100 + k;
                RandomGame(seed, classes[ci], classes[(ci + 1 + static_cast<int>(k % 8)) % 9], st);
            }
        std::printf("  random games: %d games, %d ended, %d actions, %zu distinct cards played\n", st.games, st.ended,
            st.actions, st.played.size());
        CHECK_EQ(st.failures, 0);
        CHECK_EQ(st.capped, 0);
        CHECK(st.ended * 3 >= st.games * 2);
        int missing = 0;
        for (char const* id : kList)
            if (!st.played.count(id))
            {
                ++missing;
                std::printf("  never played in the soak: %s\n", id);
            }
        CHECK(missing <= 6);
    }

    // ---- the AI plays these cards -----------------------------------------------------------------------

    void AiGame(uint32_t seed, std::string const& c0, std::string const& c1, AiProfile p0, AiProfile p1, Stats& st)
    {
        std::mt19937 rng(seed * 7919u + 3u);
        std::unique_ptr<Match> m = MakeGame(seed, c0, c1, rng);
        ++st.games;
        if (!m)
        {
            ++st.refusals;
            return;
        }
        AiProfile const prof[2] = {p0, p1};
        uint32_t const aiSeed[2] = {seed * 7919u + 1u, seed * 104729u + 2u};
        for (int side = 0; side < 2; ++side)
        {
            Action const a = ChooseAiAction(*m, side, Db(), 0, prof[side], aiSeed[side]);
            if (a.type != ActionType::Mulligan || m->Apply(side, a) != ActionError::None)
                ++st.refusals;
        }
        int actions = 0;
        int turn = m->Turn();
        int guard = 0;
        while (m->GetPhase() == Phase::Play && ++guard < 3000)
        {
            int const side = m->ActiveSide();
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                actions = 0;
            }
            MatchView const v = m->ViewFor(side, m->LastEventSeq());
            Action const a = ChooseAiAction(*m, side, Db(), actions, prof[side], aiSeed[side]);
            if (a.type == ActionType::Play)
                for (HandCardView const& h : v.self.hand)
                    if (h.id == a.source)
                        ++st.played[h.cardId];
            if (m->Apply(side, a) != ActionError::None)
            {
                ++st.refusals;
                std::printf("  AI action refused: seed %u type %d source %u target %u\n", seed, static_cast<int>(a.type),
                    a.source, a.target);
                break;
            }
            ++actions;
            ++st.actions;
        }
        if (m->GetPhase() == Phase::Over)
            ++st.ended;
    }

    void TestAiPlaysTheCards()
    {
        char const* const classes[] = {"WARRIOR", "SHAMAN", "ROGUE", "PALADIN", "HUNTER", "DRUID", "WARLOCK", "MAGE", "PRIEST"};
        Stats st;
        for (int g = 0; g < 36; ++g)
            AiGame(9000 + static_cast<uint32_t>(g), classes[g % 9], classes[(g * 4 + 1) % 9], AiProfile::Normal,
                AiProfile::Normal, st);
        for (int g = 0; g < 12; ++g)
            AiGame(9500 + static_cast<uint32_t>(g), classes[(g * 2) % 9], classes[(g * 5 + 3) % 9],
                g % 2 ? AiProfile::Hard : AiProfile::Normal, AiProfile::Hard, st);
        std::printf("  AI games: %d games, %d ended, %d actions, %zu distinct cards played\n", st.games, st.ended, st.actions,
            st.played.size());
        CHECK_EQ(st.refusals, 0);
        CHECK(st.ended == st.games);
        int partF = 0;
        for (char const* id : kList)
            if (st.played.count(id))
                ++partF;
        std::printf("  part F cards played by the AI: %d of %zu\n", partF, sizeof(kList) / sizeof(kList[0]));
        CHECK(partF >= 20);
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--games")
            g_gamesPerClass = std::atoi(argv[i + 1]);
    RUN(TestCoverage);
    RUN(TestDruid);
    RUN(TestHunter);
    RUN(TestMage);
    RUN(TestNeutralBattlecries);
    RUN(TestNeutralTriggers);
    RUN(TestPaladin);
    RUN(TestOtherClasses);
    RUN(TestLegalActionsAgree);
    RUN(TestRandomGames);
    RUN(TestAiPlaysTheCards);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
