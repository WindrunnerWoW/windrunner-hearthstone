// Standalone tests of the Classic engine cards (src/match/HsMatchCardsClassicEngine.cpp): the cards that needed
// engine work (Lord Jaraxxus, granted Deathrattles / triggers, temporary control, per-turn counters ...) and the
// four reward-only promo cards. One or more focused scenarios per card, the interactions (Silence, stacking order,
// control changes), LegalActions against Apply, determinism, a random-games soak with decks full of these cards,
// AI games (both profiles) and the redacted copy. Uses every card of the dataset (tests/match_all_cards.h).
//   match_cards_engine_tests   run all tests; exit code 0 = all checks passed.

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

    // Every card of part E (the 27 cards, the 4 promos and their tokens).
    char const* const kCards[] = {"EX1_323", "EX1_158", "CS2_038", "EX1_363", "EX1_334", "NEW1_036", "EX1_137", "CS2_053",
        "EX1_365", "EX1_076", "EX1_616", "EX1_561", "EX1_339", "EX1_544", "EX1_100", "EX1_274", "EX1_080", "EX1_059",
        "EX1_619", "EX1_126", "EX1_304", "EX1_590", "NEW1_030", "EX1_062", "NEW1_016", "EX1_112", "PRO_001"};
    char const* const kTokens[] = {"EX1_323h", "EX1_323w", "EX1_tk33", "EX1_tk34", "EX1_158e", "EX1_158t", "CS2_038e",
        "EX1_363e", "Mekka1", "Mekka2", "Mekka3", "Mekka4", "Mekka4t", "PRO_001a", "PRO_001at", "PRO_001b", "PRO_001c"};

    // ---- helpers ------------------------------------------------------------------------------

    // A match in its play phase, side 0 active on turn 1 with 10 mana; both hands empty, both decks 20 Murloc
    // Raiders, boards empty.
    std::unique_ptr<Match> FreshWith(char const* c0, char const* c1, uint32_t seed, int hp1)
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
        s.sides[1].heroHealth = hp1;
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

    std::unique_ptr<Match> Fresh(char const* c0 = "MAGE", char const* c1 = "WARRIOR", uint32_t seed = 11)
    {
        return FreshWith(c0, c1, seed, 0);
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
        CHECK(!Db().Supported("EX1_161o")); // an enchantment stays unsupported
        // The data's entourage of Power of the Horde and of Gelbin.
        for (char const* id : {"CS2_121", "EX1_021", "EX1_023", "EX1_110", "EX1_390", "CS2_179"})
            CHECK(Db().Supported(id));
        // The promo tokens are in the dataset and supported.
        CHECK(Db().Supported("PRO_001at") && Db().Supported("Mekka4t"));
        CHECK(Db().Supported("EX1_506a") && Db().Supported("CS2_tk1"));
    }

    // ---- Lord Jaraxxus -------------------------------------------------------------------------------

    void TestJaraxxus()
    {
        // The hero becomes Lord Jaraxxus: card, 15 Health, Blood Fury, INFERNO!; the minion does not stay.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetHero(*m, 0, 12, 6);
            TA::SetWeapon(*m, 0, "CS2_106");
            TA::SetHeroPowerUsed(*m, 0, true);
            Ids b = TA::SetBoard(*m, 0, {"EX1_595"}); // Cult Master: no death trigger for Jaraxxus
            (void)b;
            CHECK(Cast(*m, 0, "EX1_323") == ActionError::None);
            HeroView const h = HV(*m, 0);
            CHECK(h.cardId == "EX1_323h");
            CHECK_EQ(h.health, 15);
            CHECK_EQ(h.maxHealth, 15);
            CHECK_EQ(h.armor, 0); // the new hero has no armor
            CHECK(h.weaponCardId == "EX1_323w" && h.weaponAttack == 3 && h.weaponDurability == 8);
            CHECK(h.powerCardId == "EX1_tk33" && h.powerCost == 2 && !h.powerUsed);
            CHECK(Board(*m, 0) == Strs{"EX1_595"});
            CHECK_EQ(Hand(*m, 0).size(), 0u);
            CHECK_EQ(HP(*m, 0), 15);
            // Blood Fury: the hero attacks for 3.
            CHECK(h.canAttack && h.attack == 3);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 27);
            CHECK_EQ(HV(*m, 0).weaponDurability, 7);
        }
        // INFERNO!: a 6/6 Infernal for 2 mana; needs a free slot; shows up in the legal list.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            CHECK(Cast(*m, 0, "EX1_323") == ActionError::None);
            Mana(*m);
            bool legal = false;
            for (Action const& a : m->LegalActions(0))
                legal = legal || (a.type == ActionType::HeroPower && a.source == TA::PowerId(*m, 0));
            CHECK(legal);
            CHECK(m->Apply(0, Power(*m, 0)) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_tk34"});
            MinionView const inf = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(inf.attack == 6 && inf.health == 6);
            CHECK_EQ(HV(*m, 0).mana, 8);
            CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Used);
            // A full board refuses the power.
            std::unique_ptr<Match> n = Fresh("WARLOCK");
            CHECK(Cast(*n, 0, "EX1_323") == ActionError::None);
            TA::SetBoard(*n, 0, Strs(7, "CS2_168"));
            Mana(*n);
            CHECK(n->Apply(0, Power(*n, 0)) == ActionError::BoardFull);
        }
        // A second Jaraxxus: health back to 15, a new weapon; Jaraxxus cannot be played on a full board.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            CHECK(Cast(*m, 0, "EX1_323") == ActionError::None);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            EntityId const oldWeapon = HV(*m, 0).weaponId;
            TA::SetHero(*m, 0, 3, 2);
            CHECK(Cast(*m, 0, "EX1_323") == ActionError::None);
            CHECK_EQ(HP(*m, 0), 15);
            CHECK_EQ(HV(*m, 0).armor, 0);
            CHECK(HV(*m, 0).weaponId != oldWeapon && HV(*m, 0).weaponDurability == 8);
            CHECK(HV(*m, 0).cardId == "EX1_323h");
            TA::SetBoard(*m, 0, Strs(7, "CS2_168"));
            CHECK(Cast(*m, 0, "EX1_323") == ActionError::BoardFull);
        }
        // The hero can die as Jaraxxus (15 Health) and the opponent's view shows the new hero.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            CHECK(Cast(*m, 0, "EX1_323") == ActionError::None);
            MatchView const v1 = m->ViewFor(1, 0);
            CHECK(v1.other.hero.cardId == "EX1_323h" && v1.other.hero.maxHealth == 15);
            CHECK(CountEvents(*m, EventType::Transform) == 1);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 9);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 3);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::None);
            CHECK(m->GetPhase() == Phase::Over && m->GetResult().winner == 1);
        }
    }

    // ---- granted enchantments ------------------------------------------------------------------------

    void TestSoulOfTheForest()
    {
        // Every minion you control gets the Deathrattle; a Treant 2/2 takes the slot.
        std::unique_ptr<Match> m = Fresh("DRUID");
        Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_172", "CS2_182"});
        CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
        CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
        Strs bd = Board(*m, 0);
        CHECK(bd.size() == 3 && bd[0] == "EX1_158t" && bd[1] == "CS2_172" && bd[2] == "CS2_182");
        MinionView const t = MV(*m, TA::BoardIds(*m, 0)[0]);
        CHECK(t.attack == 2 && t.health == 2);
        CHECK(Cast(*m, 0, "CS2_029", b[2]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"EX1_158t", "CS2_172", "EX1_158t"}));
        // A Treant has no deathrattle of its own: killing it leaves nothing.
        CHECK(Cast(*m, 0, "CS2_029", TA::BoardIds(*m, 0)[0]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"CS2_172", "EX1_158t"}));
        // Minions played later, and enemy minions, do not have it.
        Ids e = TA::SetBoard(*m, 1, {"CS2_168"});
        CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
        EntityId const late = TA::BoardIds(*m, 0).back();
        CHECK(Cast(*m, 0, "CS2_029", late) == ActionError::None);
        CHECK(Cast(*m, 0, "CS2_029", e[0]) == ActionError::None);
        CHECK(Board(*m, 1).empty());
        CHECK(Board(*m, 0) == (Strs{"CS2_172", "EX1_158t"}));
        // Two casts: two Treants.
        std::unique_ptr<Match> n = Fresh("DRUID");
        Ids nb = TA::SetBoard(*n, 0, {"CS2_182"});
        CHECK(Cast(*n, 0, "EX1_158") == ActionError::None);
        CHECK(Cast(*n, 0, "EX1_158") == ActionError::None);
        CHECK(Cast(*n, 0, "CS2_029", nb[0]) == ActionError::None);
        CHECK(Board(*n, 0) == (Strs{"EX1_158t", "EX1_158t"}));
    }

    void TestAncestralSpirit()
    {
        // The minion returns as a fresh copy with full Health; the second death is final.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_182", "CS2_172"});
            TA::SetMinion(*m, b[1], 6, 5, 8); // buffed: the copy is fresh, not buffed
            CHECK(Cast(*m, 0, "CS2_038", b[1]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[1]) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 3 && bd[1] == "CS2_182");
            EntityId const again = TA::BoardIds(*m, 0)[1];
            CHECK(again != b[1]);
            MinionView const mv = MV(*m, again);
            CHECK(mv.attack == 4 && mv.health == 5 && mv.maxHealth == 5);
            CHECK(Cast(*m, 0, "CS2_029", again) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_172", "CS2_172"}));
        }
        // An enemy minion returns for its controller.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "CS2_038", e[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", e[0]) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"CS2_182"} && Board(*m, 0).empty());
        }
        // A full board: the minion cannot return.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            TA::SetBoard(*m, 0, {"CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            Ids c = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            (void)c;
            // The new board has no enchantment at all (SetBoard makes fresh minions): nothing returns.
            CHECK(Cast(*m, 0, "CS2_029", TA::BoardIds(*m, 0)[0]) == ActionError::None);
            CHECK_EQ(Board(*m, 0).size(), 6u);
        }
    }

    // Build 6024: Ancestral Spirit gives "Deathrattle: Resummon this minion." It is a real Deathrattle.
    void TestAncestralSpiritIsADeathrattle()
    {
        // The minion has no Deathrattle of its own; the death still resolves a Deathrattle (a "dr" event of the dead
        // minion's card) and the minion comes back as a fresh copy.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetMinion(*m, b[0], 6, 5, 8); // buffed
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            uint32_t const before = m->LastEventSeq();
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle, before), 0);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle, before), 1);
            bool drOfYeti = false;
            for (Event const& e : m->ViewFor(0, before).events)
                if (e.type == EventType::Deathrattle && e.source == b[0] && e.cardId == "CS2_182")
                    drOfYeti = true;
            CHECK(drOfYeti);
            Ids const now = TA::BoardIds(*m, 0);
            CHECK(now.size() == 1 && now[0] != b[0]);
            MinionView const mv = MV(*m, now[0]);
            CHECK(mv.attack == 4 && mv.health == 5 && mv.maxHealth == 5); // base stats, nothing carried over
            // The copy carries no enchantment: its death is final and has no Deathrattle.
            uint32_t const before2 = m->LastEventSeq();
            CHECK(Cast(*m, 0, "CS2_029", now[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle, before2), 0);
        }
        // Silenced first: no Deathrattle at all.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            uint32_t const before = m->LastEventSeq();
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
            CHECK_EQ(CountEvents(*m, EventType::Deathrattle, before), 0);
        }
        // Two casts are two Deathrattles: two fresh copies (they are two separate "Resummon this minion").
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_182", "CS2_182"}));
        }
        // With the minion's own Deathrattle: Harvest Golem. Its own Deathrattle resolves first (the Damaged Golem),
        // then Ancestral Spirit's (a fresh Harvest Golem); both resolve.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "EX1_556", "CS2_168"});
            CHECK(Cast(*m, 0, "CS2_038", b[1]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[1]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_168", "skele21", "EX1_556", "CS2_168"}));
            // The fresh Harvest Golem still has its own Deathrattle, but no Ancestral Spirit: it dies for good.
            CHECK(Cast(*m, 0, "CS2_029", TA::BoardIds(*m, 0)[2]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_168", "skele21", "skele21", "CS2_168"}));
        }
        // With the minion's own Deathrattle: Loot Hoarder. Both resolve: a card is drawn and the Hoarder returns.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"EX1_096"});
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            CHECK(Hand(*m, 0).empty());
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_096"});
            CHECK_EQ(Hand(*m, 0).size(), 1u);
            CHECK(TA::BoardIds(*m, 0)[0] != b[0]);
            // The fresh Hoarder draws once more when it dies, and does not come back (Cast replaces the hand, so one
            // card in hand is the new draw).
            CHECK(Cast(*m, 0, "CS2_029", TA::BoardIds(*m, 0)[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
            CHECK_EQ(Hand(*m, 0).size(), 1u);
        }
    }

    void TestGrantsAndSilence()
    {
        // Silence removes a granted Deathrattle: Soul of the Forest.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
        }
        // Silence removes Ancestral Spirit.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "CS2_038", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
        }
        // Silence removes Blessing of Wisdom; the minion then attacks without drawing.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_363", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(m->Apply(0, Attack(b[0], Hero(*m, 1))) == ActionError::None);
            CHECK(Hand(*m, 0).empty());
        }
        // A silenced minion can still be given a Deathrattle afterwards (the silence came first).
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_158t"});
        }
        // Gone when the minion returns to the hand and is played again.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "DREAM_04", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty() && Hand(*m, 0) == Strs{"CS2_182"});
            Mana(*m);
            CHECK(m->Apply(0, Play(TA::HandIds(*m, 0)[0])) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", TA::BoardIds(*m, 0)[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
        }
        // Gone when the minion is transformed.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_022", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_tk1"});
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0).empty());
        }
        // Kept when control changes: the Treant appears for the new controller.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS1_113", b[0]) == ActionError::None);
            CHECK(OnBoard(*m, 1, b[0]));
            CHECK(Cast(*m, 1, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"EX1_158t"} && Board(*m, 0).empty());
        }
    }

    void TestGrantOrder()
    {
        // Ancestral Spirit first, then Soul of the Forest: the minion returns, then the Treant (granted order).
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_182", "CS2_172"});
            CHECK(Cast(*m, 0, "CS2_038", b[1]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[1]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_172", "CS2_182", "EX1_158t", "CS2_172"}));
            // The copy is fresh: it has no Soul of the Forest, so its death leaves only a Treant of the others? No:
            // the copy dies for good.
            CHECK(Cast(*m, 0, "CS2_029", TA::BoardIds(*m, 0)[1]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_172", "EX1_158t", "CS2_172"}));
        }
        // Soul of the Forest first, then Ancestral Spirit: the Treant comes first.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_182", "CS2_172"});
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_038", b[1]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[1]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"CS2_172", "EX1_158t", "CS2_182", "CS2_172"}));
        }
        // The minion's own Deathrattle comes first: Harvest Golem, then the Treant.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"EX1_556"});
            CHECK(Cast(*m, 0, "EX1_158") == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"skele21", "EX1_158t"}));
        }
    }

    void TestBlessingOfWisdom()
    {
        // Own minion: draws for its owner on every attack.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_363", b[0]) == ActionError::None);
            CHECK(Hand(*m, 0).empty());
            CHECK(m->Apply(0, Attack(b[0], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 1u);
            CHECK_EQ(HP(*m, 1), 26);
        }
        // An enemy minion: the player who blessed it draws when it attacks.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_363", e[0]) == ActionError::None);
            ToSide1(*m);
            CHECK(m->Apply(1, Attack(e[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 1u); // the blesser drew
            CHECK(Hand(*m, 1).empty());       // the attacker's controller did not
            CHECK_EQ(HP(*m, 0), 26);
        }
        // The minion changes sides: the blesser still draws.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_363", b[0]) == ActionError::None);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS1_113", b[0]) == ActionError::None);
            TA::ReadyAll(*m);
            CHECK(m->Apply(1, Attack(b[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 1u);
            CHECK(Hand(*m, 1).empty());
        }
        // Two blessings: two cards; the draw happens before the damage (a Taunt-less attack on a minion).
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168"});
            CHECK(Cast(*m, 0, "EX1_363", b[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_363", b[0]) == ActionError::None);
            CHECK(m->Apply(0, Attack(b[0], e[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 2u);
        }
    }

    // ---- Shadow Madness -------------------------------------------------------------------------------

    void TestShadowMadness()
    {
        // Targets: an enemy minion with 3 or less Attack; it attacks at once and goes home at the end of the turn.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_200"});
            Ids h = TA::SetHand(*m, 0, {"EX1_334", "EX1_334", "EX1_334"});
            CHECK(m->Apply(0, Play(h[0], e[1])) == ActionError::Target); // 6 Attack
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0], 0)) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
            CHECK(OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]));
            CHECK(MV(*m, e[0]).canAttack);
            CHECK(m->Apply(0, Attack(e[0], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(OnBoard(*m, 1, e[0]) && !OnBoard(*m, 0, e[0]));
            CHECK(MV(*m, e[0]).canAttack); // ready on its owner's turn
            // Back home, it is an ordinary minion again.
            CHECK(m->Apply(1, End()) == ActionError::None);
            Mana(*m);
            CHECK(!OnBoard(*m, 0, e[0]));
        }
        // A stealthy minion is no target; a full own board refuses the card.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"EX1_010"}); // Worgen Infiltrator: Stealth
            CHECK(Cast(*m, 0, "EX1_334", e[0]) == ActionError::Target);
            std::unique_ptr<Match> n = Fresh("PRIEST");
            Ids f = TA::SetBoard(*n, 1, {"CS2_168"});
            TA::SetBoard(*n, 0, Strs(7, "CS2_182"));
            CHECK(Cast(*n, 0, "EX1_334", f[0]) == ActionError::BoardFull);
        }
        // Silence makes the control permanent.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(Cast(*m, 0, "EX1_334", e[0]) == ActionError::None);
            CHECK(Cast(*m, 0, "EX1_332", e[0]) == ActionError::None);
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]));
            CHECK(MV(*m, e[0]).silenced);
        }
        // If the owner's board is full when it should return, it dies.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(Cast(*m, 0, "EX1_334", e[0]) == ActionError::None);
            TA::SetBoard(*m, 1, Strs(7, "CS2_168"));
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(!OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]));
            CHECK_EQ(Board(*m, 1).size(), 7u);
            CHECK(Board(*m, 0).empty());
        }
        // It can die for the thief (the Deathrattle belongs to the new controller); no return then.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"EX1_029"}); // Leper Gnome 2/1
            CHECK(Cast(*m, 0, "EX1_334", e[0]) == ActionError::None);
            Ids f = TA::SetBoard(*m, 1, {"CS2_182"});
            (void)f;
            CHECK(Cast(*m, 0, "CS2_029", e[0]) == ActionError::None);
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(!OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]));
        }
        // The minion keeps its damage and buffs when it comes back.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"CS2_172"});
            TA::SetMinion(*m, e[0], 3, 1, 2);
            CHECK(Cast(*m, 0, "EX1_334", e[0]) == ActionError::None);
            CHECK(m->Apply(0, End()) == ActionError::None);
            MinionView const mv = MV(*m, e[0]);
            CHECK(mv.attack == 3 && mv.health == 1);
        }
    }

    // ---- Commanding Shout ------------------------------------------------------------------------------

    void TestCommandingShout()
    {
        std::unique_ptr<Match> m = Fresh("WARRIOR");
        Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
        Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
        CHECK(Cast(*m, 0, "NEW1_036") == ActionError::None);
        CHECK_EQ(Hand(*m, 0).size(), 1u); // drew a card
        CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
        CHECK(OnBoard(*m, 0, b[0]) && MV(*m, b[0]).health == 1);
        // A minion played afterwards is protected too.
        CHECK(Cast(*m, 0, "CS2_182") == ActionError::None);
        EntityId const late = TA::BoardIds(*m, 0).back();
        CHECK(Cast(*m, 0, "CS2_029", late) == ActionError::None);
        CHECK(OnBoard(*m, 0, late) && MV(*m, late).health == 1);
        // Combat damage: an enemy minion cannot kill it either; and enemy minions are not protected.
        CHECK(Cast(*m, 0, "CS2_029", e[0]) == ActionError::None);
        CHECK(!OnBoard(*m, 1, e[0]));
        // Destroy effects still kill.
        CHECK(Cast(*m, 0, "EX1_312") == ActionError::None);
        CHECK(Board(*m, 0).empty());
        // The effect ends with the turn.
        std::unique_ptr<Match> n = Fresh("WARRIOR");
        Ids c = TA::SetBoard(*n, 0, {"CS2_182"});
        CHECK(Cast(*n, 0, "NEW1_036") == ActionError::None);
        CHECK(n->Apply(0, End()) == ActionError::None);
        CHECK(n->Apply(1, End()) == ActionError::None);
        CHECK(Cast(*n, 0, "CS2_029", c[0]) == ActionError::None);
        CHECK(!OnBoard(*n, 0, c[0]));
    }

    // ---- Headcrack, Far Sight, Holy Wrath ------------------------------------------------------------------

    void TestHeadcrack()
    {
        // Without Combo: 2 damage and it stays gone.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            CHECK(Cast(*m, 0, "EX1_137") == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(m->Apply(1, End()) == ActionError::None);
            CHECK(!Has(Hand(*m, 0), "EX1_137"));
        }
        // With Combo: returns to the hand at the start of the next turn (once).
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"GAME_005", "EX1_137"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(!Has(Hand(*m, 0), "EX1_137")); // not yet
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(!Has(Hand(*m, 0), "EX1_137")); // not on the opponent's turn
            CHECK(m->Apply(1, End()) == ActionError::None);
            CHECK_EQ(CountOf(Hand(*m, 0), "EX1_137"), 1);
            // Played again without Combo: gone for good.
            Mana(*m);
            EntityId card = 0;
            Ids const ids = TA::HandIds(*m, 0);
            Strs const names = Hand(*m, 0);
            for (size_t i = 0; i < ids.size(); ++i)
                if (names[i] == "EX1_137")
                    card = ids[i];
            CHECK(card != 0);
            CHECK(m->Apply(0, Play(card)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
            CHECK(m->Apply(0, End()) == ActionError::None);
            CHECK(m->Apply(1, End()) == ActionError::None);
            CHECK(!Has(Hand(*m, 0), "EX1_137"));
        }
        // Spell Damage counts; with a full hand the card is lost.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            TA::SetBoard(*m, 0, {"CS2_142"});
            Ids h = TA::SetHand(*m, 0, {"GAME_005", "EX1_137"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 27);
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK(Has(Hand(*m, 0), "EX1_137"));
        }
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"GAME_005", "EX1_137"});
            m->Apply(0, Play(h[0]));
            m->Apply(0, Play(h[1]));
            m->Apply(0, End());
            TA::SetHand(*m, 0, Strs(10, "CS2_168"));
            m->Apply(1, End());
            CHECK_EQ(Hand(*m, 0).size(), 10u); // no room: Headcrack is lost (and the drawn card burned)
            CHECK(!Has(Hand(*m, 0), "EX1_137"));
        }
    }

    void TestFarSight()
    {
        // Only the drawn card is cheaper.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetDeck(*m, 0, {"CS2_182", "CS2_179", "CS2_168"});
            CHECK(Cast(*m, 0, "CS2_053") == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_182"});
            CHECK_EQ(TA::HandCost(*m, TA::HandIds(*m, 0)[0]), 1);
            m->Apply(0, End());
            m->Apply(1, End()); // draws CS2_179
            Ids const h = TA::HandIds(*m, 0);
            CHECK_EQ(h.size(), 2u);
            CHECK_EQ(TA::HandCost(*m, h[0]), 1);
            CHECK_EQ(TA::HandCost(*m, h[1]), 4);
        }
        // Costs never go below 0.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetDeck(*m, 0, {"CS2_168"});
            CHECK(Cast(*m, 0, "CS2_053") == ActionError::None);
            CHECK_EQ(TA::HandCost(*m, TA::HandIds(*m, 0)[0]), 0);
        }
        // An empty deck: fatigue, no card, nothing else.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetDeck(*m, 0, {});
            CHECK(Cast(*m, 0, "CS2_053") == ActionError::None);
            CHECK(Hand(*m, 0).empty());
            CHECK_EQ(HP(*m, 0), 29);
        }
        // A full hand after the play: the drawn card goes into the 10th slot; one more is burned.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetDeck(*m, 0, {"CS2_182", "CS2_179"});
            Ids h = TA::SetHand(*m, 0, Strs(9, "CS2_168"));
            h = TA::SetHand(*m, 0, {"CS2_053", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 10u);
            CHECK_EQ(TA::HandCost(*m, TA::HandIds(*m, 0).back()), 1);
        }
    }

    void TestHolyWrath()
    {
        // Damage equals the cost of the drawn card.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetDeck(*m, 0, {"CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_365", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
            CHECK(Hand(*m, 0) == Strs{"CS2_182"});
        }
        // A minion target; Spell Damage counts.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetBoard(*m, 0, {"CS2_142"});
            TA::SetDeck(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_365", e[0]) == ActionError::None);
            CHECK_EQ(MV(*m, e[0]).health, 2); // 7 - (4 + 1)
        }
        // The current cost counts (Mana Wraith: +1).
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetBoard(*m, 1, {"EX1_616"});
            TA::SetDeck(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_365", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 25);
        }
        // A free card deals nothing; an empty deck deals nothing but fatigue to the caster.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetDeck(*m, 0, {"GAME_005"});
            CHECK(Cast(*m, 0, "EX1_365", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 30);
            TA::SetDeck(*m, 0, {});
            CHECK(Cast(*m, 0, "EX1_365", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 30);
            CHECK_EQ(HP(*m, 0), 29);
        }
        // Any character is a target, a target is required.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            CHECK(Cast(*m, 0, "EX1_365") == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_365", Hero(*m, 0)) == ActionError::None);
        }
    }

    // ---- cost auras ------------------------------------------------------------------------------------

    void TestPintSizedSummoner()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"EX1_076"});
        Ids h = TA::SetHand(*m, 0, {"CS2_182", "CS2_182", "CS2_029", "CS2_168"});
        Ids e = TA::SetHand(*m, 1, {"CS2_182"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 3);
        CHECK_EQ(TA::HandCost(*m, h[2]), 4); // spells are not discounted
        CHECK_EQ(TA::HandCost(*m, h[3]), 0); // 1 - 1
        CHECK_EQ(TA::HandCost(*m, e[0]), 4); // only the owner
        // Spells and generated minions do not count as "played a minion".
        CHECK(Cast(*m, 0, "EX1_248") == ActionError::None);
        h = TA::SetHand(*m, 0, {"CS2_182", "CS2_182"});
        CHECK_EQ(TA::HandCost(*m, h[1]), 3);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK_EQ(HV(*m, 0).mana, 10 - 3 - 3); // Feral Spirit 3, the discounted Yeti 3
        CHECK_EQ(TA::HandCost(*m, h[1]), 4);
        // The next turn the discount is back.
        m->Apply(0, End());
        m->Apply(1, End());
        CHECK_EQ(TA::HandCost(*m, h[1]), 3);
        // Silence removes it.
        CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
        h = TA::SetHand(*m, 0, {"CS2_182"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 4);
        // Pint-Sized itself pays full price and then switches the discount off.
        std::unique_ptr<Match> n = Fresh();
        Ids g = TA::SetHand(*n, 0, {"EX1_076", "CS2_182"});
        CHECK_EQ(TA::HandCost(*n, g[0]), 2);
        CHECK(n->Apply(0, Play(g[0])) == ActionError::None);
        CHECK_EQ(TA::HandCost(*n, g[1]), 4);
        // Two of them do not stack up: still (1) less.
        std::unique_ptr<Match> k = Fresh();
        TA::SetBoard(*k, 0, {"EX1_076", "EX1_076"});
        Ids kh = TA::SetHand(*k, 0, {"CS2_182"});
        CHECK_EQ(TA::HandCost(*k, kh[0]), 2);
    }

    void TestManaWraith()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids w = TA::SetBoard(*m, 0, {"EX1_616"});
        Ids h = TA::SetHand(*m, 0, {"CS2_182", "CS2_029", "GAME_005"});
        Ids e = TA::SetHand(*m, 1, {"CS2_182", "CS2_029"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 5); // minion: +1
        CHECK_EQ(TA::HandCost(*m, h[1]), 4); // spell: unchanged
        CHECK_EQ(TA::HandCost(*m, e[0]), 5); // the enemy hand too
        CHECK_EQ(TA::HandCost(*m, e[1]), 4);
        // Wraith owned by the enemy taxes my minions as well.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 1, {"EX1_616"});
        Ids nh = TA::SetHand(*n, 0, {"CS2_182"});
        CHECK_EQ(TA::HandCost(*n, nh[0]), 5);
        // With Pint-Sized Summoner: 4 for the first minion, then 5.
        std::unique_ptr<Match> k = Fresh();
        TA::SetBoard(*k, 0, {"EX1_616", "EX1_076"});
        Ids kh = TA::SetHand(*k, 0, {"CS2_182", "CS2_182"});
        CHECK_EQ(TA::HandCost(*k, kh[0]), 4);
        CHECK(k->Apply(0, Play(kh[0])) == ActionError::None);
        CHECK_EQ(TA::HandCost(*k, kh[1]), 5);
        // Silencing the Wraith removes the tax.
        CHECK(Cast(*m, 0, "EX1_332", w[0]) == ActionError::None);
        h = TA::SetHand(*m, 0, {"CS2_182"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 4);
        // Playing a minion costs the taxed price.
        std::unique_ptr<Match> p = Fresh();
        TA::SetBoard(*p, 1, {"EX1_616"});
        TA::SetMana(*p, 0, 4, 10);
        Ids ph = TA::SetHand(*p, 0, {"CS2_182"});
        CHECK(p->Apply(0, Play(ph[0])) == ActionError::Mana);
        TA::SetMana(*p, 0, 5, 10);
        CHECK(p->Apply(0, Play(ph[0])) == ActionError::None);
    }

    // ---- Alexstrasza, Thoughtsteal, Flare, Cho, Arcanist, Secretkeeper ----------------------------------------

    void TestAlexstrasza()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_168"});
        CHECK(Cast(*m, 0, "EX1_561", b[0]) == ActionError::Target);
        CHECK(Cast(*m, 0, "EX1_561") == ActionError::Target);
        CHECK(Cast(*m, 0, "EX1_561", Hero(*m, 1)) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 15);
        CHECK_EQ(HV(*m, 1).maxHealth, 30);
        CHECK_EQ(CountEvents(*m, EventType::Heal), 0);
        // Own hero with 5 Health and armor: Health 15, armor kept.
        std::unique_ptr<Match> n = Fresh();
        TA::SetHero(*n, 0, 5, 3);
        CHECK(Cast(*n, 0, "EX1_561", Hero(*n, 0)) == ActionError::None);
        CHECK_EQ(HP(*n, 0), 15);
        CHECK_EQ(HV(*n, 0).armor, 3);
        CHECK(Board(*n, 0) == Strs{"EX1_561"});
        // A hero with a lower maximum (boss encounters): the maximum rises to 15.
        std::unique_ptr<Match> k = FreshWith("MAGE", "WARRIOR", 11, 10);
        CHECK_EQ(HP(*k, 1), 10);
        CHECK(Cast(*k, 0, "EX1_561", Hero(*k, 1)) == ActionError::None);
        CHECK_EQ(HP(*k, 1), 15);
        CHECK_EQ(HV(*k, 1).maxHealth, 15);
        // A healing source cannot push Lord Jaraxxus above 15.
        std::unique_ptr<Match> j = Fresh("WARLOCK");
        CHECK(Cast(*j, 0, "EX1_323") == ActionError::None);
        CHECK(Cast(*j, 0, "EX1_561", Hero(*j, 0)) == ActionError::None);
        CHECK_EQ(HP(*j, 0), 15);
        CHECK_EQ(HV(*j, 0).maxHealth, 15);
    }

    void TestThoughtsteal()
    {
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetDeck(*m, 1, {"CS2_182", "CS2_200", "CS2_179"});
            CHECK(Cast(*m, 0, "EX1_339") == ActionError::None);
            Strs const h = Hand(*m, 0);
            CHECK_EQ(h.size(), 2u);
            CHECK(h[0] != h[1]); // distinct positions (the three cards differ)
            for (std::string const& id : h)
                CHECK(id == "CS2_182" || id == "CS2_200" || id == "CS2_179");
            CHECK_EQ(TA::DeckIds(*m, 1).size(), 3u); // copies: the deck is untouched
        }
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetDeck(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_339") == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_182"});
            std::unique_ptr<Match> n = Fresh("PRIEST");
            TA::SetDeck(*n, 1, {});
            CHECK(Cast(*n, 0, "EX1_339") == ActionError::None);
            CHECK(Hand(*n, 0).empty());
        }
        // The cards are copies you can play; a full hand keeps only what fits.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetDeck(*m, 1, {"CS2_182", "CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"EX1_339", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168",
                                         "CS2_168", "CS2_168", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 10u);
            CHECK_EQ(CountOf(Hand(*m, 0), "CS2_182"), 1);
        }
    }

    void TestThoughtstealHiddenInfo()
    {
        std::unique_ptr<Match> m = Fresh("PRIEST");
        TA::SetDeck(*m, 1, {"CS2_200", "CS2_182", "CS2_179"});
        uint32_t const before = m->LastEventSeq();
        CHECK(Cast(*m, 0, "EX1_339") == ActionError::None);
        // The other side sees that cards were generated, never which.
        int gen = 0;
        for (Event const& e : m->ViewFor(1, before).events)
            if (e.type == EventType::Generate)
            {
                ++gen;
                CHECK(e.cardId.empty());
            }
        CHECK_EQ(gen, 2);
        MatchView const v1 = m->ViewFor(1, 0);
        CHECK(v1.other.hand.empty() && v1.other.handCount == 2);
        // Side 1's redacted copy: side 0's hand and deck are unknown placeholders, its own deck is known.
        std::unique_ptr<Match> r1 = m->RedactedCopy(1, 5);
        for (std::string const& id : Hand(*r1, 0))
            CHECK(id == "?");
        for (std::string const& id : TA::DeckCardIds(*r1, 0))
            CHECK(id == "?");
        CHECK_EQ(Hand(*r1, 0).size(), 2u);
        CHECK(TA::DeckCardIds(*r1, 1).size() == 3u && TA::DeckCardIds(*r1, 1)[0] != "?");
        // Side 0's copy: the enemy deck is unknown; a Thoughtsteal simulated there yields placeholders only.
        std::unique_ptr<Match> r0 = m->RedactedCopy(0, 6);
        for (std::string const& id : TA::DeckCardIds(*r0, 1))
            CHECK(id == "?");
        TA::SetMana(*r0, 0, 10, 10);
        Ids h = TA::SetHand(*r0, 0, {"EX1_339"});
        r0->Apply(0, Play(h[0]));
        Strs const nh = Hand(*r0, 0);
        CHECK_EQ(nh.size(), 2u);
        for (std::string const& id : nh)
            CHECK(id == "?");
        CHECK_EQ(TA::DeckIds(*r0, 1).size(), 3u);
        // The placeholders are never legal to play.
        for (Action const& a : r0->LegalActions(0))
            if (a.type == ActionType::Play)
                CHECK(a.source != TA::HandIds(*r0, 0)[0] && a.source != TA::HandIds(*r0, 0)[1]);
    }

    void TestFlare()
    {
        std::unique_ptr<Match> m = Fresh("HUNTER");
        Ids b = TA::SetBoard(*m, 0, {"EX1_010", "CS2_168"});
        Ids e = TA::SetBoard(*m, 1, {"EX1_010", "EX1_522"});
        CHECK(MV(*m, b[0]).stealth && MV(*m, e[0]).stealth);
        TA::SetSecrets(*m, 1, {"EX1_289", "EX1_594"});
        TA::SetSecrets(*m, 0, {"EX1_287"});
        uint32_t const before = m->LastEventSeq();
        CHECK(Cast(*m, 0, "EX1_544") == ActionError::None);
        CHECK(!MV(*m, b[0]).stealth && !MV(*m, e[0]).stealth && !MV(*m, e[1]).stealth);
        CHECK(MV(*m, e[1]).poisonous); // only Stealth goes
        CHECK(TA::SecretCardIds(*m, 1).empty());
        CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_287"});
        CHECK_EQ(Hand(*m, 0).size(), 1u);
        CHECK_EQ(CountEvents(*m, EventType::Reveal, before), 2);
        CHECK_EQ(CountEvents(*m, EventType::Stealth, before), 3);
        // Stealth until your next turn (Conceal) is lost too.
        std::unique_ptr<Match> n = Fresh("HUNTER");
        Ids c = TA::SetBoard(*n, 1, {"CS2_182"});
        TA::SetSecrets(*n, 1, {"EX1_132"});
        CHECK(Cast(*n, 0, "EX1_128") == ActionError::None); // my Conceal does nothing for the enemy
        ToSide1(*n);
        CHECK(Cast(*n, 1, "EX1_128") == ActionError::None);
        CHECK(MV(*n, c[0]).stealth);
        ToSide0(*n);
        CHECK(Cast(*n, 0, "EX1_544") == ActionError::None);
        CHECK(!MV(*n, c[0]).stealth);
        // Eaglehorn Bow does not grow when its owner's secret is destroyed.
        std::unique_ptr<Match> k = Fresh("HUNTER");
        TA::SetSecrets(*k, 1, {"EX1_132"});
        TA::SetWeapon(*k, 1, "EX1_536");
        CHECK(Cast(*k, 0, "EX1_544") == ActionError::None);
        CHECK(TA::SecretCardIds(*k, 1).empty());
        CHECK_EQ(HV(*k, 1).weaponDurability, 2);
    }

    void TestLorewalkerCho()
    {
        // A spell of mine is copied for the opponent, and the other way round.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_100"});
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None);
            CHECK(Hand(*m, 1) == Strs{"CS2_029"});
            CHECK(Hand(*m, 0).empty());
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_025") == ActionError::None);
            CHECK(Hand(*m, 0) == Strs{"CS2_025"});
            CHECK(Hand(*m, 1).empty());
        }
        // The enemy's Cho works for both: my spell gives the enemy two copies when both sides have a Cho.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_100"});
            TA::SetBoard(*m, 1, {"EX1_100"});
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(CountOf(Hand(*m, 1), "CS2_029"), 2);
        }
        // Minions and weapons are not spells; a full hand takes no copy; the copy is a normal card.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_100"});
            CHECK(Cast(*m, 0, "CS2_182") == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_106") == ActionError::None);
            CHECK(Hand(*m, 1).empty());
            TA::SetHand(*m, 1, Strs(10, "CS2_168"));
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(Hand(*m, 1).size(), 10u);
            CHECK_EQ(CountOf(Hand(*m, 1), "CS2_029"), 0);
        }
        // The Coin is a spell as well; Silence stops it.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_100"});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK(Hand(*m, 1) == Strs{"GAME_005"});
            TA::SetHand(*m, 1, {});
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(Hand(*m, 1) == Strs{"EX1_332"}); // Cho reacts to the cast itself, before the Silence resolves
            TA::SetHand(*m, 1, {});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK(Hand(*m, 1).empty()); // silenced: no more copies
        }
    }

    void TestEtherealArcanist()
    {
        std::unique_ptr<Match> m = Fresh("MAGE");
        Ids b = TA::SetBoard(*m, 0, {"EX1_274"});
        m->Apply(0, End()); // no Secret: no gain
        CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[0]).health == 3);
        m->Apply(1, End());
        TA::SetSecrets(*m, 0, {"EX1_287"});
        m->Apply(0, End());
        CHECK(MV(*m, b[0]).attack == 5 && MV(*m, b[0]).health == 5);
        // The opponent's end of turn does nothing; the enemy's own Secret does not count for me.
        m->Apply(1, End());
        CHECK(MV(*m, b[0]).attack == 5);
        std::unique_ptr<Match> n = Fresh("MAGE");
        Ids c = TA::SetBoard(*n, 0, {"EX1_274"});
        TA::SetSecrets(*n, 1, {"EX1_287"});
        n->Apply(0, End());
        CHECK(MV(*n, c[0]).attack == 3);
        // A secret that triggered in the meantime is gone: no gain.
        std::unique_ptr<Match> k = Fresh("MAGE");
        Ids d = TA::SetBoard(*k, 0, {"EX1_274"});
        TA::SetSecrets(*k, 0, {"EX1_289"});
        k->Apply(0, End());
        CHECK(MV(*k, d[0]).attack == 5);
    }

    void TestSecretkeeper()
    {
        std::unique_ptr<Match> m = Fresh("MAGE");
        Ids b = TA::SetBoard(*m, 0, {"EX1_080"});
        CHECK(MV(*m, b[0]).attack == 1 && MV(*m, b[0]).health == 2);
        CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None); // not a secret
        CHECK(MV(*m, b[0]).attack == 1);
        CHECK(Cast(*m, 0, "EX1_289") == ActionError::None);              // my secret
        CHECK(MV(*m, b[0]).attack == 2 && MV(*m, b[0]).health == 3);
        ToSide1(*m);
        CHECK(Cast(*m, 1, "EX1_287") == ActionError::None);              // the enemy's secret
        CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[0]).health == 4);
        // A minion with another Secretkeeper: both grow.
        std::unique_ptr<Match> n = Fresh("MAGE");
        Ids c = TA::SetBoard(*n, 0, {"EX1_080"});
        Ids d = TA::SetBoard(*n, 1, {"EX1_080"});
        CHECK(Cast(*n, 0, "EX1_287") == ActionError::None);
        CHECK(MV(*n, c[0]).attack == 2 && MV(*n, d[0]).attack == 2);
        // Silenced: no more growth.
        CHECK(Cast(*n, 0, "EX1_332", c[0]) == ActionError::None);
        CHECK(Cast(*n, 0, "EX1_594") == ActionError::None);
        CHECK(MV(*n, c[0]).attack == 1 && MV(*n, d[0]).attack == 3);
    }

    // ---- minion battlecries -------------------------------------------------------------------------------

    void TestCrazedAlchemist()
    {
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_172"});
            Ids h = TA::SetHand(*m, 0, {"EX1_059"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::Target); // a target exists: required
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0], b[0])) == ActionError::None);
            MinionView const mv = MV(*m, b[0]);
            CHECK(mv.attack == 5 && mv.health == 4 && mv.maxHealth == 4); // 4/5 -> 5/4
        }
        // A damaged minion: Attack = current Health, Health = Attack, undamaged. Permanent until Silence.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            TA::SetMinion(*m, e[0], 4, 2, 5);
            Ids h = TA::SetHand(*m, 0, {"EX1_059"});
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
            MinionView const mv = MV(*m, e[0]);
            CHECK(mv.attack == 2 && mv.health == 4 && mv.maxHealth == 4);
            CHECK(Cast(*m, 0, "EX1_332", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).attack == 4 && MV(*m, e[0]).health == 5);
        }
        // An enraged, damaged Amani Berserker (5/2 with 1 damage) becomes 2/5: undamaged, the Enrage is off
        // (review).
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"EX1_393"});
            TA::SetMinion(*m, e[0], 2, 2, 3);
            CHECK(MV(*m, e[0]).attack == 5 && MV(*m, e[0]).health == 2);
            Ids h = TA::SetHand(*m, 0, {"EX1_059"});
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
            MinionView const mv = MV(*m, e[0]);
            CHECK(mv.attack == 2 && mv.health == 5 && mv.maxHealth == 5 && !mv.enraged);
        }
        // Attack 0 becomes Health 0: the minion dies. Itself is no target; alone it needs none.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"EX1_100"}); // Lorewalker Cho 0/4
            Ids h = TA::SetHand(*m, 0, {"EX1_059"});
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
            CHECK(Board(*m, 1).empty());
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_059") == ActionError::None);
            CHECK(Board(*n, 0) == Strs{"EX1_059"});
        }
        // Swapping a buffed/aura minion uses the current values.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_122", "CS2_172"}); // Raid Leader: +1 Attack for the Raptor
            Ids h = TA::SetHand(*m, 0, {"EX1_059"});
            CHECK(MV(*m, b[1]).attack == 4);
            CHECK(m->Apply(0, Play(h[0], b[1])) == ActionError::None);
            CHECK(MV(*m, b[1]).health == 4); // the new Health is the old (aura) Attack
            CHECK(MV(*m, b[1]).attack == 2); // the old Health 2, plus the Raid Leader's +1 minus ... swapped values

        }
    }

    void TestEquality()
    {
        std::unique_ptr<Match> m = Fresh("PALADIN");
        Ids b = TA::SetBoard(*m, 0, {"CS2_182", "EX1_008"});
        Ids e = TA::SetBoard(*m, 1, {"CS2_200", "CS2_168"});
        TA::SetMinion(*m, b[0], 4, 2, 5); // damaged
        CHECK(Cast(*m, 0, "EX1_619") == ActionError::None);
        for (EntityId id : {b[0], b[1], e[0], e[1]})
        {
            MinionView const mv = MV(*m, id);
            CHECK(mv.health == 1 && mv.maxHealth == 1);
        }
        CHECK(MV(*m, b[1]).divineShield); // keywords stay
        CHECK(MV(*m, e[0]).attack == 6);   // Attack untouched
        // Healing cannot lift it; the minions die to anything.
        CHECK(Cast(*m, 0, "EX1_621") == ActionError::None); // Circle of Healing
        CHECK(MV(*m, b[0]).health == 1);
        CHECK(Cast(*m, 0, "CS2_025") == ActionError::None); // Arcane Explosion: 1 damage to enemy minions
        CHECK(Board(*m, 1).empty());
        // Works on an empty battlefield.
        std::unique_ptr<Match> n = Fresh("PALADIN");
        CHECK(Cast(*n, 0, "EX1_619") == ActionError::None);
    }

    void TestBetrayal()
    {
        // The target deals its Attack to the minions next to it, not to itself.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_172", "CS2_119", "CS2_120", "CS2_182"});
            // Raptor 3/2, Snapjaw 2/7, Crocolisk 2/3, Yeti 4/5
            CHECK(Cast(*m, 0, "EX1_126", e[1]) == ActionError::None);
            CHECK(MV(*m, e[0]).id == 0);          // the Raptor took 2 and died
            CHECK(MV(*m, e[1]).health == 7);      // the target is unharmed
            CHECK(MV(*m, e[2]).health == 1);      // the Crocolisk took 2
            CHECK(MV(*m, e[3]).health == 5);
            // Raptor (now at the edge of the board is gone): the Yeti's neighbour is the Crocolisk
            CHECK(Cast(*m, 0, "EX1_126", e[3]) == ActionError::None);
            CHECK(!OnBoard(*m, 1, e[2]));          // took 4 more
        }
        // Spell Damage does not add (the damage is the minion's Attack); Divine Shield pops.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            TA::SetBoard(*m, 0, {"CS2_142"});
            Ids e = TA::SetBoard(*m, 1, {"EX1_008", "CS2_182", "CS2_172"});
            CHECK(Cast(*m, 0, "EX1_126", e[1]) == ActionError::None);
            CHECK(!MV(*m, e[0]).divineShield && MV(*m, e[0]).health == 1);
            CHECK(!OnBoard(*m, 1, e[2])); // Raptor 3/2 took 4
        }
        // Poisonous: the neighbours are destroyed.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200", "EX1_170", "CS2_200"});
            CHECK(Cast(*m, 0, "EX1_126", e[1]) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"EX1_170"});
        }
        // An enemy minion only; a lone minion deals nothing.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_126", b[0]) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_126", Hero(*m, 1)) == ActionError::Target);
            CHECK(Cast(*m, 0, "EX1_126", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 5);
        }
        // The minion's Attack at that moment (Dire Wolf Alpha's aura counts).
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "EX1_162", "CS2_182"});
            // Murloc Raider 2/1 (+1 from Dire Wolf = 3), Dire Wolf Alpha 2/2, Yeti 4/5 (+1 = 5)
            CHECK(Cast(*m, 0, "EX1_126", e[2]) == ActionError::None);
            CHECK(MV(*m, e[1]).health == 0 || !OnBoard(*m, 1, e[1])); // 5 damage kills the Dire Wolf
        }
    }

    void TestVoidTerror()
    {
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_172", "CS2_182"});
            TA::SetMinion(*m, b[1], 3, 1, 2); // damaged Raptor: current Health 1
            Ids h = TA::SetHand(*m, 0, {"EX1_304"});
            CHECK(m->Apply(0, Play(h[0], 0, 0, 2)) == ActionError::None); // between the Raptor and the Yeti
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 2 && bd[0] == "CS2_168" && bd[1] == "EX1_304");
            // Played at slot 2: neighbours are the Raptor (slot 1) and the Yeti (slot 3 -> now slot 2)
            CHECK(!OnBoard(*m, 0, b[1]) && !OnBoard(*m, 0, b[2]) && OnBoard(*m, 0, b[0]));
            MinionView const vt = MV(*m, TA::BoardIds(*m, 0)[1]);
            CHECK(vt.attack == 3 + 3 + 4 && vt.health == 3 + 1 + 5);
        }
        // Edge of the board: one neighbour; no neighbours: nothing.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetBoard(*m, 0, {"CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"EX1_304"});
            CHECK(m->Apply(0, Play(h[0], 0, 0, 1)) == ActionError::None);
            MinionView const vt = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(vt.attack == 7 && vt.health == 8);
            CHECK_EQ(Board(*m, 0).size(), 1u);
            std::unique_ptr<Match> n = Fresh("WARLOCK");
            CHECK(Cast(*n, 0, "EX1_304") == ActionError::None);
            MinionView const alone = MV(*n, TA::BoardIds(*n, 0)[0]);
            CHECK(alone.attack == 3 && alone.health == 3);
        }
        // Their Deathrattles trigger; the enemy's minions are not touched.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetBoard(*m, 0, {"EX1_029", "CS2_168"}); // Leper Gnome: 2 damage to the enemy hero
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"EX1_304"});
            CHECK(m->Apply(0, Play(h[0], 0, 0, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(OnBoard(*m, 1, e[0]));
            CHECK(Board(*m, 0) == Strs{"EX1_304"}); // both neighbours (the Gnome and the Raider) are consumed
        }
    }

    void TestBloodKnight()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"EX1_008", "CS2_168"});
        Ids e = TA::SetBoard(*m, 1, {"EX1_023", "EX1_008"});
        CHECK(Cast(*m, 0, "EX1_590") == ActionError::None);
        for (EntityId id : {b[0], e[0], e[1]})
            CHECK(!MV(*m, id).divineShield);
        MinionView const bk = MV(*m, TA::BoardIds(*m, 0).back());
        CHECK(bk.attack == 12 && bk.health == 12); // 3 + 3 shields * 3
        // No shields: nothing.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 1, {"CS2_182"});
        CHECK(Cast(*n, 0, "EX1_590") == ActionError::None);
        MinionView const plain = MV(*n, TA::BoardIds(*n, 0)[0]);
        CHECK(plain.attack == 3 && plain.health == 3);
        // The buff is permanent (until Silence).
        CHECK(Cast(*m, 0, "EX1_332", TA::BoardIds(*m, 0).back()) == ActionError::None);
        CHECK(MV(*m, TA::BoardIds(*m, 0).back()).attack == 3);
    }

    void TestDeathwing()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_182", "EX1_096"}); // Loot Hoarder: draws after the discard
        Ids e = TA::SetBoard(*m, 1, {"CS2_200", "CS2_168"});
        Ids h = TA::SetHand(*m, 0, {"NEW1_030", "CS2_029", "CS2_168", "CS2_182"});
        uint32_t const before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK(Board(*m, 0) == Strs{"NEW1_030"});
        CHECK(Board(*m, 1).empty());
        MinionView const dw = MV(*m, TA::BoardIds(*m, 0)[0]);
        CHECK(dw.attack == 12 && dw.health == 12);
        CHECK_EQ(CountEvents(*m, EventType::Discard, before), 3);
        CHECK_EQ(Hand(*m, 0).size(), 1u); // the Loot Hoarder's card came after the discard
        (void)b;
        (void)e;
        // An empty battlefield and an empty hand are fine.
        std::unique_ptr<Match> n = Fresh();
        CHECK(Cast(*n, 0, "NEW1_030") == ActionError::None);
        CHECK(Board(*n, 0) == Strs{"NEW1_030"});
    }

    // ---- the reward-only promos -------------------------------------------------------------------------------

    void TestOldMurkEye()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_168"});
        Ids e = TA::SetBoard(*m, 1, {"CS2_168", "EX1_506a", "CS2_182"});
        Ids h = TA::SetHand(*m, 0, {"EX1_062"});
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        EntityId const murk = TA::BoardIds(*m, 0).back();
        MinionView mv = MV(*m, murk);
        CHECK(mv.attack == 2 + 4 && mv.health == 4 && mv.charge && mv.canAttack); // four other Murlocs, both sides
        // A Murloc dies: the Attack drops; the Murk-Eye does not count itself.
        CHECK(Cast(*m, 0, "CS2_029", e[0]) == ActionError::None);
        CHECK_EQ(MV(*m, murk).attack, 5);
        // Silence: back to 2.
        CHECK(Cast(*m, 0, "EX1_332", murk) == ActionError::None);
        CHECK_EQ(MV(*m, murk).attack, 2);
        // Two Murk-Eyes count each other.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 0, {"EX1_062", "EX1_062"});
        CHECK(MV(*n, c[0]).attack == 3 && MV(*n, c[1]).attack == 3);
        (void)b;
        // It attacks at once (Charge).
        CHECK(m->Apply(0, Attack(b[0], Hero(*m, 1))) == ActionError::None);
    }

    void TestCaptainsParrot()
    {
        // A Pirate from your own deck goes to your hand.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetDeck(*m, 0, {"CS2_168", "CS2_146", "CS2_182", "NEW1_025", "CS2_168"});
            TA::SetDeck(*m, 1, {"CS2_146"});
            CHECK(Cast(*m, 0, "NEW1_016") == ActionError::None);
            Strs const h = Hand(*m, 0);
            CHECK_EQ(h.size(), 1u);
            CHECK(h[0] == "CS2_146" || h[0] == "NEW1_025");
            CHECK_EQ(TA::DeckIds(*m, 0).size(), 4u);
            CHECK_EQ(TA::DeckIds(*m, 1).size(), 1u);
        }
        // Over many seeds both pirates show up; without a pirate nothing happens.
        {
            std::set<std::string> seen;
            for (uint32_t seed = 1; seed <= 40; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                TA::SetDeck(*m, 0, {"CS2_146", "NEW1_025", "CS2_182"});
                Cast(*m, 0, "NEW1_016");
                for (std::string const& id : Hand(*m, 0))
                    seen.insert(id);
            }
            CHECK(seen.count("CS2_146") && seen.count("NEW1_025") && seen.size() == 2u);
            std::unique_ptr<Match> n = Fresh();
            size_t const deckBefore = TA::DeckIds(*n, 0).size();
            CHECK(Cast(*n, 0, "NEW1_016") == ActionError::None);
            CHECK(Hand(*n, 0).empty());
            CHECK_EQ(TA::DeckIds(*n, 0).size(), deckBefore);
        }
        // A full hand burns it.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetDeck(*m, 0, {"CS2_146"});
            Ids h = TA::SetHand(*m, 0, {"NEW1_016", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168",
                                         "CS2_168", "CS2_168", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 10u);
        }
    }

    void TestMekkatorque()
    {
        std::set<std::string> seen;
        for (uint32_t seed = 1; seed <= 60; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids h = TA::SetHand(*m, 0, {"EX1_112"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 2 && bd[0] == "EX1_112");
            if (bd.size() == 2)
                seen.insert(bd[1]);
        }
        CHECK(seen == (std::set<std::string>{"Mekka1", "Mekka2", "Mekka3", "Mekka4"}));
        // Homing Chicken: destroys itself and draws 3 at the start of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"Mekka1"});
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK(!OnBoard(*m, 0, b[0]));
            CHECK_EQ(Hand(*m, 0).size(), 4u); // 3 + the turn's draw
        }
        // Repair Bot: restores 6 to a damaged character at the end of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"Mekka2"});
            TA::SetHero(*m, 0, 20, 0);
            m->Apply(0, End());
            CHECK_EQ(HP(*m, 0), 26);
            m->Apply(1, End());
            m->Apply(0, End()); // 26 -> 30 (caps at the maximum)
            CHECK_EQ(HP(*m, 0), 30);
            // Any damaged character of either side can be picked.
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, {"Mekka2"});
            Ids e = TA::SetBoard(*n, 1, {"CS2_182"});
            TA::SetMinion(*n, e[0], 4, 1, 5);
            n->Apply(0, End());
            CHECK_EQ(MV(*n, e[0]).health, 5);
        }
        // Emboldener 3000: +1/+1 to a random minion at the end of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"Mekka3"});
            m->Apply(0, End());
            CHECK(MV(*m, b[0]).attack == 1 && MV(*m, b[0]).health == 5);
            std::unique_ptr<Match> n = Fresh();
            Ids c = TA::SetBoard(*n, 0, {"Mekka3", "CS2_182"});
            Ids e = TA::SetBoard(*n, 1, {"CS2_182"});
            n->Apply(0, End());
            int total = 0;
            for (EntityId id : {c[0], c[1], e[0]})
                total += MV(*n, id).attack;
            CHECK_EQ(total, 0 + 4 + 4 + 1);
        }
        // Poultryizer: transforms a random minion into a 1/1 Chicken.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"Mekka4", "CS2_182"});
            m->Apply(0, End());
            m->Apply(1, End());
            Strs const bd = Board(*m, 0);
            CHECK_EQ(CountOf(bd, "Mekka4t"), 1);
            CHECK_EQ(bd.size(), 2u);
        }
    }

    void TestEliteTauren()
    {
        std::set<std::string> seen;
        for (uint32_t seed = 1; seed <= 60; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            Ids h = TA::SetHand(*m, 0, {"PRO_001"});
            TA::SetHand(*m, 1, {"CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            Strs const mine = Hand(*m, 0);
            Strs const theirs = Hand(*m, 1);
            CHECK_EQ(mine.size(), 1u);
            CHECK_EQ(theirs.size(), 2u);
            CHECK(Board(*m, 0) == Strs{"PRO_001"});
            for (std::string const& id : mine)
                seen.insert(id);
            for (std::string const& id : theirs)
                if (id != "CS2_168")
                    seen.insert(id);
        }
        CHECK(seen == (std::set<std::string>{"PRO_001a", "PRO_001b", "PRO_001c"}));
        // I Am Murloc: three to five 1/1 Murlocs.
        std::set<size_t> counts;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            CHECK(Cast(*m, 0, "PRO_001a") == ActionError::None);
            size_t const n = Board(*m, 0).size();
            counts.insert(n);
            for (EntityId id : TA::BoardIds(*m, 0))
            {
                MinionView const mv = MV(*m, id);
                CHECK(mv.attack == 1 && mv.health == 1);
            }
        }
        CHECK(counts == (std::set<size_t>{3, 4, 5}));
        // The Murlocs need room: not playable on a full board; fewer when the board fills up.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, Strs(7, "CS2_168"));
            CHECK(Cast(*m, 0, "PRO_001a") == ActionError::BoardFull);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, Strs(5, "CS2_168"));
            CHECK(Cast(*n, 0, "PRO_001a") == ActionError::None);
            CHECK_EQ(Board(*n, 0).size(), 7u);
        }
        // Rogues Do It...: 4 damage (Spell Damage counts) and a card.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"CS2_142"});
            CHECK(Cast(*m, 0, "PRO_001b") == ActionError::Target);
            CHECK(Cast(*m, 0, "PRO_001b", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 25);
            CHECK_EQ(Hand(*m, 0).size(), 1u);
        }
        // Power of the Horde: one of the six Horde Warriors.
        std::set<std::string> horde;
        for (uint32_t seed = 1; seed <= 80; ++seed)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
            CHECK(Cast(*m, 0, "PRO_001c") == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK_EQ(bd.size(), 1u);
            if (bd.size() == 1)
                horde.insert(bd[0]);
        }
        CHECK(horde == (std::set<std::string>{"CS2_121", "EX1_021", "EX1_023", "EX1_110", "EX1_390", "CS2_179"}));
    }

    // ---- LegalActions against Apply -----------------------------------------------------------------------------

    // The same rich position every time: the card in the hand of side 0, a mixed battlefield.
    std::unique_ptr<Match> Rich(char const* card, bool fullBoard, bool secrets)
    {
        std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", 77);
        TA::SetBoard(*m, 0, fullBoard ? Strs(7, "CS2_182") : Strs{"CS2_182", "CS2_168", "EX1_029"});
        TA::SetBoard(*m, 1, {"CS2_168", "EX1_010", "CS2_200", "EX1_100", "CS2_179"});
        TA::SetMinion(*m, TA::BoardIds(*m, 1)[0], 2, 1, 1);
        if (secrets)
        {
            TA::SetSecrets(*m, 0, {"EX1_287"});
            TA::SetSecrets(*m, 1, {"EX1_289"});
        }
        TA::SetDeck(*m, 0, {"CS2_182", "CS2_168", "CS2_146", "CS2_182"});
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
        int legalCount = 0;
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
                std::printf("  legal/apply mismatch: %s target %u full=%d: listed=%d error=%s\n", card, t, fullBoard ? 1 : 0, listed ? 1 : 0,
                    ActionErrorCode(e));
                CHECK(false);
            }
            if (listed)
                ++legalCount;
            else
                CHECK(e != ActionError::None);
            // Whatever happens, the position stays sound.
            CHECK(!Capped(*m));
        }
        // The card is playable somehow unless the position forbids it.
        (void)legalCount;
    }

    void TestLegalAgreement()
    {
        for (char const* card : kCards)
            for (int variant = 0; variant < 4; ++variant)
                CheckLegalAgreement(card, (variant & 1) != 0, (variant & 2) != 0);
        for (char const* card : {"PRO_001a", "PRO_001b", "PRO_001c"})
            for (int variant = 0; variant < 2; ++variant)
                CheckLegalAgreement(card, variant != 0, true);
        // Counts that matter: the targeted cards list exactly their valid targets.
        std::unique_ptr<Match> m = Rich("EX1_334", false, false);
        int n = 0;
        for (Action const& a : m->LegalActions(0))
            if (a.type == ActionType::Play)
                ++n;
        CHECK_EQ(n, 3); // the 2/1 Murloc, Cho (0/4) and the 3/5 Shieldmasta; Worgen Infiltrator is stealthy, the Ogre has 6 Attack
    }

    // ---- determinism, soak, AI -------------------------------------------------------------------------------------

    // The supported collectible cards a deck of heroClass may hold.
    std::vector<std::string> DeckPool(std::string const& heroClass)
    {
        static std::set<std::string> const tokens = {"CS2_050", "CS2_051", "CS2_052", "CS2_082", "CS2_101t", "CS2_152",
            "CS2_boar", "CS2_mirror", "CS2_tk1", "EX1_025t", "EX1_110t", "EX1_130a", "EX1_160t", "EX1_165a", "EX1_165b",
            "EX1_165t1", "EX1_165t2", "EX1_317t", "EX1_345t", "EX1_383t", "EX1_506a", "EX1_534t", "EX1_554t", "EX1_598",
            "EX1_finkle", "EX1_tk11", "EX1_tk28", "EX1_tk29", "EX1_tk34", "EX1_tk9", "GAME_002", "Mekka1", "Mekka2",
            "Mekka3", "Mekka4", "NEW1_009", "NEW1_032", "NEW1_033", "NEW1_034", "hexfrog", "skele11", "skele21", "tt_010a"};
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

    // The part E cards a deck of heroClass may hold.
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

    // 30 cards, at most 2 copies, 1 legendary; most of the part E cards of the class go in first.
    std::vector<std::string> EngineDeck(std::string const& heroClass, std::mt19937& rng)
    {
        std::vector<std::string> deck;
        std::map<std::string, int> copies;
        auto add = [&](std::string const& id) {
            CardDef const* def = Db().Find(id);
            int const limit = (def && def->rarity == "LEGENDARY") ? 1 : 2;
            if (copies[id] >= limit || deck.size() >= 30)
                return;
            ++copies[id];
            deck.push_back(id);
        };
        for (std::string const& id : OwnCards(heroClass))
            if (rng() % 4 != 0)
                add(id);
        std::vector<std::string> const pool = DeckPool(heroClass);
        int guard = 0;
        while (deck.size() < 30 && guard++ < 100000)
            add(pool[rng() % pool.size()]);
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
        setup.sides[0].deck = EngineDeck(c0, rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = c1;
        setup.sides[1].deck = EngineDeck(c1, rng);
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
                    w.self.hero.health <= w.self.hero.maxHealth;
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
                uint32_t const seed = 5000 + static_cast<uint32_t>(ci) * 100 + k;
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
            setup.sides[0].deck = EngineDeck("WARLOCK", rng);
            setup.sides[1].name = "B";
            setup.sides[1].heroClass = "PRIEST";
            setup.sides[1].deck = EngineDeck("PRIEST", rng);
            std::string error;
            std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
            CHECK(m != nullptr);
            if (!m)
                return;
            Action mull;
            mull.type = ActionType::Mulligan;
            m->Apply(0, mull);
            m->Apply(1, mull);
            for (int step = 0; step < 200 && m->GetPhase() == Phase::Play; ++step)
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

    // Plays one game with the AI on both sides; returns false when the AI chose a refused action.
    bool AiGame(uint32_t seed, char const* c0, char const* c1, AiProfile p0, AiProfile p1, int& refusals, bool& over)
    {
        std::mt19937 rng(seed);
        MatchSetup setup;
        setup.seed = seed;
        setup.firstSide = -1;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = c0;
        setup.sides[0].deck = EngineDeck(c0, rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = c1;
        setup.sides[1].deck = EngineDeck(c1, rng);
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
            CHECK(AiGame(300 + static_cast<uint32_t>(i), pairs[i][0], pairs[i][1], AiProfile::Normal, AiProfile::Normal, refusals, over));
            ++games;
            ended += over ? 1 : 0;
        }
        for (int i = 0; i < 3; ++i)
        {
            bool over = false;
            CHECK(AiGame(400 + static_cast<uint32_t>(i), pairs[i][0], pairs[i][1], AiProfile::Hard, AiProfile::Normal, refusals, over));
            ++games;
            ended += over ? 1 : 0;
        }
        {
            bool over = false;
            CHECK(AiGame(450, "WARLOCK", "PRIEST", AiProfile::Hard, AiProfile::Hard, refusals, over));
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

    void TestRedactedCopyEngine()
    {
        // The redacted copy of a position full of part E cards plays on without refusals.
        std::mt19937 rng(99);
        MatchSetup setup;
        setup.seed = 31;
        setup.firstSide = 0;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = "WARLOCK";
        setup.sides[0].deck = EngineDeck("WARLOCK", rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = "PRIEST";
        setup.sides[1].deck = EngineDeck("PRIEST", rng);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
        CHECK(m != nullptr);
        if (!m)
            return;
        Action mull;
        mull.type = ActionType::Mulligan;
        m->Apply(0, mull);
        m->Apply(1, mull);
        for (int round = 0; round < 8 && m->GetPhase() == Phase::Play; ++round)
        {
            int const side = m->ActiveSide();
            std::unique_ptr<Match> r = m->RedactedCopy(side, 2000 + static_cast<uint32_t>(round));
            CHECK(r != nullptr);
            if (!r)
                return;
            for (int step = 0; step < 40 && r->GetPhase() == Phase::Play; ++step)
            {
                int const s = r->ActiveSide();
                MatchView const v = r->ViewFor(s, r->LastEventSeq());
                if (v.pickPending && !v.pick.empty())
                {
                    CHECK(r->Apply(s, Choose(v.pick[rng() % v.pick.size()].id)) == ActionError::None);
                    continue;
                }
                size_t const pick = rng() % (v.legal.size() + 1);
                CHECK(r->Apply(s, pick == v.legal.size() ? End() : v.legal[pick]) == ActionError::None);
            }
            // Never an enemy hand or deck card in the copy.
            for (std::string const& id : Hand(*r, 1 - side))
                CHECK(id == "?");
            m->Apply(side, End());
        }
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--games")
            g_gamesPerClass = std::atoi(argv[i + 1]);
    RUN(TestCoverage);
    RUN(TestJaraxxus);
    RUN(TestSoulOfTheForest);
    RUN(TestAncestralSpirit);
    RUN(TestAncestralSpiritIsADeathrattle);
    RUN(TestGrantsAndSilence);
    RUN(TestGrantOrder);
    RUN(TestBlessingOfWisdom);
    RUN(TestShadowMadness);
    RUN(TestCommandingShout);
    RUN(TestHeadcrack);
    RUN(TestFarSight);
    RUN(TestHolyWrath);
    RUN(TestPintSizedSummoner);
    RUN(TestManaWraith);
    RUN(TestAlexstrasza);
    RUN(TestThoughtsteal);
    RUN(TestThoughtstealHiddenInfo);
    RUN(TestFlare);
    RUN(TestLorewalkerCho);
    RUN(TestEtherealArcanist);
    RUN(TestSecretkeeper);
    RUN(TestCrazedAlchemist);
    RUN(TestEquality);
    RUN(TestBetrayal);
    RUN(TestVoidTerror);
    RUN(TestBloodKnight);
    RUN(TestDeathwing);
    RUN(TestOldMurkEye);
    RUN(TestCaptainsParrot);
    RUN(TestMekkatorque);
    RUN(TestEliteTauren);
    RUN(TestLegalAgreement);
    RUN(TestRandomGames);
    RUN(TestDeterminism);
    RUN(TestAiPlaysThem);
    RUN(TestAiSingleCards);
    RUN(TestRedactedCopyEngine);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
