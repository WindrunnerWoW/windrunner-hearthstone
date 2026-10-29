// Standalone tests of the scripted Classic (EXPERT1) cards (src/match/HsMatchCardsClassic.cpp):
// coverage of the representative list, at least one focused scenario per scripted card, the tricky interactions
// (secrets, deathrattle order, Brawl with Cult Master / Sylvanas, enrage, auras, cost auras ...) and a randomized
// robustness test over many seeds. Uses every card of the dataset (tests/match_all_cards.h) with the 5834 stats.
//   match_cards_classic_tests   run all tests; exit code 0 = all checks passed.

#include "../src/match/HsMatch.h"
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
static int g_gamesPerClass = 24; // --games N: a longer soak run

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);                  \
        }                                                                                             \
    } while (0)

// Integer comparison that prints both values.
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

    // The representative Classic list (164 ids).
    char const* const kList[] = {
        "EX1_029", "EX1_556", "EX1_096", "EX1_097", "EX1_110", "EX1_016", "EX1_534", "EX1_012", "EX1_287", "EX1_294",
        "EX1_289", "EX1_295", "EX1_594", "EX1_610", "EX1_611", "EX1_554", "EX1_130", "EX1_132", "EX1_136", "EX1_379",
        "EX1_533", "EX1_609", "tt_010", "EX1_393", "EX1_412", "EX1_009", "CS2_221", "EX1_414", "EX1_604", "CS2_203",
        "EX1_048", "EX1_332", "EX1_626", "EX1_245", "EX1_010", "EX1_017", "EX1_028", "NEW1_014", "EX1_128", "EX1_522",
        "CS2_059", "EX1_275", "CS2_028", "EX1_283", "CS2_031", "EX1_124", "EX1_134", "EX1_131", "NEW1_005", "EX1_133",
        "CS2_073", "EX1_613", "EX1_238", "EX1_248", "EX1_243", "EX1_250", "EX1_258", "EX1_241", "EX1_251", "EX1_567",
        "EX1_259", "EX1_154", "NEW1_007", "EX1_164", "EX1_166", "EX1_165", "EX1_160", "EX1_155", "NEW1_008", "EX1_178",
        "EX1_536", "EX1_411", "EX1_366", "EX1_398", "DS1_188", "EX1_284", "EX1_563", "EX1_162", "EX1_507", "NEW1_027",
        "EX1_608", "CS2_227", "EX1_315", "EX1_007", "NEW1_019", "EX1_595", "EX1_402", "NEW1_012", "EX1_044", "NEW1_020",
        "EX1_597", "EX1_001", "tt_004", "EX1_509", "EX1_095", "EX1_298", "NEW1_021", "EX1_102", "EX1_591", "EX1_350",
        "EX1_572", "EX1_116", "EX1_089", "EX1_082", "EX1_407", "EX1_617", "EX1_312", "EX1_570", "EX1_105", "EX1_586",
        "EX1_620", "EX1_145", "EX1_612", "NEW1_029", "NEW1_022", "EX1_049", "EX1_564", "EX1_005", "CS2_188", "EX1_046",
        "EX1_008", "EX1_020", "EX1_067", "EX1_362", "EX1_390", "CS2_146", "EX1_045", "CS2_117", "CS2_181", "EX1_583",
        "EX1_085", "EX1_006", "EX1_014", "EX1_093", "EX1_301", "EX1_313", "EX1_310", "EX1_319", "EX1_316", "EX1_317",
        "EX1_320", "EX1_303", "EX1_309", "EX1_335", "EX1_341", "EX1_345", "CS1_129", "EX1_621", "EX1_624", "EX1_625",
        "CS1_069", "EX1_002", "EX1_004", "NEW1_025", "NEW1_026", "NEW1_018", "NEW1_024", "EX1_170", "EX1_603", "EX1_391",
        "EX1_392", "EX1_408", "EX1_409", "EX1_410"};

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
    SideView SV(Match const& m, int side)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        return side == 0 ? v.self : v.other;
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

    // Puts exactly one card in the hand of side and plays it.
    ActionError Cast(Match& m, int side, char const* cardId, EntityId target = 0, int choice = 0, int position = -1)
    {
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

    // ---- coverage -------------------------------------------------------------------------------

    void TestCoverage()
    {
        static_assert(sizeof(kList) / sizeof(kList[0]) == 164, "164 listed Classic cards");
        for (char const* id : kList)
        {
            bool const ok = Db().Supported(id);
            if (!ok)
                std::printf("  not supported: %s\n", id);
            CHECK(ok);
        }
        // Tokens, options and generated cards the scripts refer to.
        for (char const* id : {"skele21", "EX1_110t", "EX1_534t", "EX1_130a", "EX1_554t", "EX1_tk11", "EX1_131t", "EX1_160t",
                 "EX1_598", "EX1_116t", "NEW1_026t", "EX1_398t", "EX1_409t", "tt_010a", "EX1_014t", "EX1_317t", "EX1_345t",
                 "EX1_625t", "EX1_625t2", "DREAM_01", "DREAM_02", "DREAM_03", "DREAM_04", "DREAM_05", "EX1_154a", "EX1_154b",
                 "NEW1_007a", "NEW1_007b", "EX1_164a", "EX1_164b", "EX1_166a", "EX1_166b", "EX1_165a", "EX1_165b",
                 "EX1_165t1", "EX1_165t2", "EX1_160a", "EX1_160b", "EX1_155a", "EX1_155b", "NEW1_008a", "NEW1_008b",
                 "EX1_178a", "EX1_178b"})
        {
            bool const ok = Db().Supported(id);
            if (!ok)
                std::printf("  not supported: %s\n", id);
            CHECK(ok);
        }
        CHECK(!Db().Supported("EX1_161o")); // an enchantment: never a playable card
        CHECK(!Db().Supported("nope"));
        // Every id of the list is a deck card (minion, spell or weapon).
        for (char const* id : kList)
        {
            CardDef const* def = Db().Find(id);
            CHECK(def && (def->type == CardType::Minion || def->type == CardType::Spell || def->type == CardType::Weapon));
        }
    }

    // ---- deathrattles -----------------------------------------------------------------------------

    void TestDeathrattles()
    {
        // Leper Gnome: 2 damage to the enemy hero.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_029"});
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK_EQ(HP(*m, 0), 30);
        }
        // Loot Hoarder: draw a card.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_096"});
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 1);
        }
        // Cairne: Baine Bloodhoof 4/5 in its slot.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "EX1_110", "CS2_168"});
            CHECK(Cast(*m, 0, "CS2_029", b[1]) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 3 && bd[1] == "EX1_110t");
            MinionView baine = MV(*m, TA::BoardIds(*m, 0)[1]);
            CHECK(baine.attack == 4 && baine.health == 5);
        }
        // Sylvanas: take control of a random enemy minion.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_016"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK(OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]));
            CHECK(!MV(*m, e[0]).canAttack); // summoning sick under its new master
        }
        // Savannah Highmane: two Hyenas at the slot of the dead minion.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "EX1_534", "CS2_172"});
            CHECK(Cast(*m, 0, "CS2_029", b[1]) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 4 && bd[0] == "CS2_172" && bd[1] == "EX1_534t" && bd[2] == "EX1_534t" && bd[3] == "CS2_172");
        }
        // Bloodmage Thalnos: Spell Damage +1 and a card when it dies.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_012"});
            CHECK(MV(*m, b[0]).spellDamage == 1);
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 23); // 6 + 1
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 1);
        }
        // Harvest Golem: Damaged Golem 2/1.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_556"});
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 1 && bd[0] == "skele21");
        }
        // Deathrattles resolve in play order, not board order: the Leper Gnome is older than the Loot Hoarder
        // that stands to its left.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_029"});
            Ids h = TA::SetHand(*m, 0, {"EX1_096"});
            CHECK(m->Apply(0, Play(h[0], 0, 0, 0)) == ActionError::None);
            Ids const now = TA::BoardIds(*m, 0);
            CHECK(now.size() == 2 && now[1] == b[0]);
            EntityId const hoarder = now[0];
            uint32_t const since = m->LastEventSeq();
            CHECK(Cast(*m, 0, "EX1_312") == ActionError::None); // Twisting Nether
            Ids order;
            for (Event const& e : m->ViewFor(0, since).events)
                if (e.type == EventType::Deathrattle)
                    order.push_back(e.source);
            CHECK(order.size() == 2 && order[0] == b[0] && order[1] == hoarder);
            CHECK_EQ(HP(*m, 1), 28);
        }
        // Silenced deathrattles do not resolve (Earth Shock kills a 1 health Leper Gnome after silencing it).
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids b = TA::SetBoard(*m, 1, {"EX1_029"});
            CHECK(Cast(*m, 0, "EX1_245", b[0]) == ActionError::None);
            CHECK(TA::BoardIds(*m, 1).empty());
            CHECK_EQ(HP(*m, 0), 30);
        }
        // Sylvanas + Brawl: the survivor is random; if the ogre survives, Sylvanas' deathrattle takes it.
        {
            int sylvanasLives = 0, ogreMine = 0, other = 0;
            for (uint32_t seed = 1; seed <= 40; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("WARRIOR", "MAGE", seed);
                Ids b = TA::SetBoard(*m, 0, {"EX1_016"});
                Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
                CHECK(Cast(*m, 0, "EX1_407") == ActionError::None);
                if (Board(*m, 0) == Strs{"EX1_016"} && Board(*m, 1).empty())
                    ++sylvanasLives;
                else if (Board(*m, 0) == Strs{"CS2_200"} && Board(*m, 1).empty() && OnBoard(*m, 0, e[0]))
                    ++ogreMine;
                else
                    ++other;
                (void)b;
            }
            CHECK(sylvanasLives > 0 && ogreMine > 0 && other == 0);
        }
        // Cult Master + Brawl: a Cult Master that survives draws a card for every other minion of its side that died.
        {
            int drew = 0, none = 0, bad = 0;
            for (uint32_t seed = 1; seed <= 40; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("WARRIOR", "MAGE", seed);
                Ids b = TA::SetBoard(*m, 0, {"EX1_595", "CS2_168", "CS2_168"});
                TA::SetBoard(*m, 1, {"CS2_200"});
                CHECK(Cast(*m, 0, "EX1_407") == ActionError::None);
                bool const cultLives = Board(*m, 0) == Strs{"EX1_595"};
                size_t const cards = Hand(*m, 0).size();
                if (cultLives)
                    cards == 2 ? ++drew : ++bad; // the two raiders died (the ogre is the enemy's)
                else
                    cards == 0 ? ++none : ++bad;
            }
            CHECK(drew > 0 && none > 0 && bad == 0);
        }
        // Brawl needs two minions.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            TA::SetBoard(*m, 0, {"CS2_168"});
            Ids h = TA::SetHand(*m, 0, {"EX1_407"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::Requirement);
        }
    }

    // ---- secrets -------------------------------------------------------------------------------------

    void TestSecretsBasics()
    {
        // Only on the opponent's turn, hidden from the opponent, revealed when triggered.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            Ids h = TA::SetHand(*m, 0, {"EX1_609", "CS2_200"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            MatchView const other = m->ViewFor(1, 0);
            CHECK(other.other.secrets.size() == 1 && other.other.secrets[0].cardId.empty());
            for (Event const& e : other.events)
                if (e.type == EventType::Secret || (e.type == EventType::Play && e.source == h[0]))
                    CHECK(e.cardId.empty());
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None); // own turn: Snipe does not fire
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).health == 7);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1);
            ToSide1(*m);
            Ids y = TA::SetHand(*m, 1, {"CS2_182"});
            CHECK(m->Apply(1, Play(y[0])) == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 1)[0]).health == 1); // Snipe: 4 damage
            CHECK(TA::SecretCardIds(*m, 0).empty());
            CHECK_EQ(CountEvents(*m, EventType::Reveal), 1);
        }
        // Counterspell vs a Secret: the countered secret never enters play.
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "HUNTER");
            TA::SetSecrets(*m, 0, {"EX1_287"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "EX1_609") == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 1).empty() && TA::SecretCardIds(*m, 0).empty());
            CHECK_EQ(CountEvents(*m, EventType::Counter), 1);
            CHECK_EQ(HV(*m, 1).mana, 8); // the cost is paid
        }
        // Counterspell vs a minion: nothing happens (it only counters spells).
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_287"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_172") == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1 && Board(*m, 1).size() == 1);
        }
        // Mirror Entity: a copy of the played minion.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_294"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_182") == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_182"} && Board(*m, 1) == Strs{"CS2_182"});
            MinionView copy = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(copy.attack == 4 && copy.health == 5);
        }
        // Repentance: health becomes 1.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetSecrets(*m, 0, {"EX1_379"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_200") == ActionError::None);
            MinionView o = MV(*m, TA::BoardIds(*m, 1)[0]);
            CHECK(o.health == 1 && o.maxHealth == 1 && o.attack == 6);
        }
        // Ice Barrier: 8 armor as soon as the hero is attacked (the attack then hits the armor).
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_289"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HV(*m, 0).armor, 5);
            CHECK_EQ(HP(*m, 0), 30);
        }
        // Vaporize: destroys a minion attacking the hero (not one attacking a minion).
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids mine = TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetSecrets(*m, 0, {"EX1_594"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_200", "CS2_172"});
            CHECK(m->Apply(1, Attack(a[1], mine[0])) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1);
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).empty() && !OnBoard(*m, 1, a[0]));
            CHECK_EQ(HP(*m, 0), 30);
        }
        // Explosive Trap with Spell Damage: 2 + 1 damage to all enemies.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            TA::SetBoard(*m, 0, {"EX1_012"});
            TA::SetSecrets(*m, 0, {"EX1_610"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_182", "CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 27);
            CHECK_EQ(HP(*m, 0), 26); // the attacker (4/5 -> 2 health left) still hits
            CHECK(OnBoard(*m, 1, a[0]) && MV(*m, a[0]).health == 2);
            CHECK(!OnBoard(*m, 1, a[1])); // 3 damage kills the 3/2
        }
        // Freezing Trap: the attacker returns to its hand and costs (2) more.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            TA::SetSecrets(*m, 0, {"EX1_611"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::BoardIds(*m, 1).empty() && Hand(*m, 1) == Strs{"CS2_172"});
            CHECK_EQ(TA::HandCost(*m, a[0]), 4);
            CHECK_EQ(HP(*m, 0), 30);
        }
        // Freezing Trap does not trigger on a hero attack ("enemy minion").
        {
            std::unique_ptr<Match> m = Fresh("HUNTER", "WARRIOR");
            TA::SetSecrets(*m, 0, {"EX1_611"});
            ToSide1(*m);
            TA::SetWeapon(*m, 1, "CS2_106");
            CHECK(m->Apply(1, Attack(Hero(*m, 1), Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1 && HP(*m, 0) == 27);
        }
        // Snake Trap: three snakes when one of your minions is attacked (not when the hero is).
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            Ids mine = TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetSecrets(*m, 0, {"EX1_554"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172", "CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1);
            CHECK(m->Apply(1, Attack(a[1], mine[0])) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).empty());
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 4 && std::count(bd.begin(), bd.end(), std::string("EX1_554t")) == 3);
        }
        // Knife Juggler + Snake Trap: every Snake pings a random enemy (3 damage in total) before the attack.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            Ids mine = TA::SetBoard(*m, 0, {"NEW1_019"});
            TA::SetSecrets(*m, 0, {"EX1_554"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_200"});
            CHECK(m->Apply(1, Attack(a[0], mine[0])) == ActionError::None);
            CHECK(!OnBoard(*m, 0, mine[0]));
            int const ogreLost = 7 - MV(*m, a[0]).health;
            CHECK_EQ((30 - HP(*m, 1)) + (ogreLost - 3), 3); // pings split between the hero and the ogre; the juggler hit for 3
            CHECK_EQ(Board(*m, 0).size(), 3);
        }
        // Noble Sacrifice: a Defender 2/1 becomes the target.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetSecrets(*m, 0, {"EX1_130"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_200"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 30);
            CHECK(Board(*m, 0).empty() && MV(*m, a[0]).health == 5);
        }
        // Eye for an Eye: the damage taken is dealt to the enemy hero.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetSecrets(*m, 0, {"EX1_132"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 27);
            CHECK_EQ(HP(*m, 1), 27);
        }
        // Redemption: returns the minion with 1 Health.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids mine = TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetSecrets(*m, 0, {"EX1_136"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", mine[0]) == ActionError::None);
            Ids const now = TA::BoardIds(*m, 0);
            CHECK(now.size() == 1 && Board(*m, 0) == Strs{"CS2_182"});
            MinionView y = MV(*m, now[0]);
            CHECK(y.health == 1 && y.maxHealth == 5 && y.attack == 4);
            CHECK(TA::SecretCardIds(*m, 0).empty());
        }
        // Spellbender: a spell aimed at a minion hits the 1/3 instead; a hero target does not trigger it.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids mine = TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetSecrets(*m, 0, {"tt_010"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1 && HP(*m, 0) == 24);
            TA::SetMana(*m, 1, 10, 10);
            CHECK(Cast(*m, 1, "CS2_029", mine[0]) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).empty());
            CHECK(MV(*m, mine[0]).health == 5 && OnBoard(*m, 0, mine[0]));
            CHECK(Board(*m, 0) == Strs{"CS2_182"}); // the Spellbender minion (1/3) died to the Fireball
        }
        // Spellbender needs room for the minion; without it the secret stays hidden and the spell hits.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids mine = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            TA::SetSecrets(*m, 0, {"tt_010"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", mine[0]) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1 && !OnBoard(*m, 0, mine[0]));
        }
        // Misdirection: the attacker hits another random character; the hero never takes the hit.
        {
            int hitOwn = 0, hitMinion = 0, bad = 0;
            for (uint32_t seed = 1; seed <= 40; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("HUNTER", "WARRIOR", seed);
                Ids mine = TA::SetBoard(*m, 0, {"CS2_182"});
                TA::SetSecrets(*m, 0, {"EX1_533"});
                ToSide1(*m);
                Ids a = TA::SetBoard(*m, 1, {"CS2_200"});
                CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
                CHECK_EQ(HP(*m, 0), 30);
                if (HP(*m, 1) == 24 && OnBoard(*m, 0, mine[0]))
                    ++hitOwn;
                else if (HP(*m, 1) == 30 && !OnBoard(*m, 0, mine[0]) && MV(*m, a[0]).health == 3)
                    ++hitMinion;
                else
                    ++bad;
            }
            CHECK(hitOwn > 0 && hitMinion > 0 && bad == 0);
        }
        // Ice Block: fatal damage is prevented, the hero is immune for the turn.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_295"});
            TA::SetHero(*m, 0, 5, 0);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::None);
            CHECK(HP(*m, 0) == 5 && m->GetPhase() == Phase::Play && HV(*m, 0).immune);
            TA::SetMana(*m, 1, 10, 10);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::Target); // immune
        }
    }

    void TestSecretInteractions()
    {
        // Two attack secrets on the same attack: Ice Barrier (older) and Explosive Trap both fire; the trap kills
        // the attacker before it deals damage.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_289", "EX1_610"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0).empty());
            CHECK_EQ(HV(*m, 0).armor, 8);
            CHECK_EQ(HP(*m, 0), 30);
            CHECK(Board(*m, 1).empty() && HP(*m, 1) == 28);
        }
        // Noble Sacrifice (older) + Explosive Trap: the Defender becomes the target, so the trap (hero attacked)
        // stays hidden.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetSecrets(*m, 0, {"EX1_130", "EX1_610"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_610"});
            CHECK(Board(*m, 0).empty() && Board(*m, 1).empty()); // Defender 2/1 and the raptor 3/2 traded
            CHECK_EQ(HP(*m, 0), 30);
        }
        // Explosive Trap (older) + Noble Sacrifice: the trap kills the attacker; the attack is gone, so Noble
        // Sacrifice stays hidden.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetSecrets(*m, 0, {"EX1_610", "EX1_130"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_130"});
            CHECK(Board(*m, 1).empty() && Board(*m, 0).empty());
        }
        // Noble Sacrifice with a 6/7 attacker: the Defender dies, the attacker keeps its hit points minus 2.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetSecrets(*m, 0, {"EX1_130"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_200"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(MV(*m, a[0]).health == 5);
        }
        // Vaporize + Freezing Trap: whichever is older fires and removes the attacker; the other stays hidden.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_594", "EX1_611"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_611"});
            CHECK(Board(*m, 1).empty() && Hand(*m, 1).empty());
        }
        // Misdirection + Ice Barrier: the redirect makes the hero no longer the defender.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetSecrets(*m, 0, {"EX1_533", "EX1_289"});
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None);
            CHECK(TA::SecretCardIds(*m, 0) == Strs{"EX1_289"});
            CHECK_EQ(HV(*m, 0).armor, 0);
        }
        // Eaglehorn Bow: +1 Durability whenever one of its owner's secrets is revealed.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            TA::SetWeapon(*m, 0, "EX1_536");
            TA::SetSecrets(*m, 0, {"EX1_609", "EX1_611"});
            CHECK_EQ(HV(*m, 0).weaponDurability, 2);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_182") == ActionError::None); // Snipe
            CHECK_EQ(HV(*m, 0).weaponDurability, 3);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(m->Apply(1, Attack(a[0], Hero(*m, 0))) == ActionError::None); // Freezing Trap
            CHECK_EQ(HV(*m, 0).weaponDurability, 4);
        }
        // Eaglehorn Bow (build 6024: "friendly Secret"): the opponent's secrets being revealed do not count, in
        // either direction.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            TA::SetSecrets(*m, 0, {"EX1_609"}); // side 0: Snipe
            ToSide1(*m);
            TA::SetWeapon(*m, 1, "EX1_536");    // side 1: the Bow, no secrets of its own
            CHECK_EQ(HV(*m, 1).weaponDurability, 2);
            uint32_t const before = m->LastEventSeq();
            CHECK(Cast(*m, 1, "CS2_182") == ActionError::None); // reveals side 0's Snipe
            int reveals = 0;
            for (Event const& e : m->ViewFor(0, before).events)
                if (e.type == EventType::Reveal)
                    ++reveals;
            CHECK_EQ(reveals, 1);
            CHECK(TA::SecretCardIds(*m, 0).empty());
            CHECK_EQ(HV(*m, 1).weaponDurability, 2); // an enemy secret: no gain
        }
        // Both sides hold a Bow and a secret: each Bow grows only for its own secret.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            TA::SetWeapon(*m, 0, "EX1_536");
            TA::SetSecrets(*m, 0, {"EX1_609"});
            ToSide1(*m);
            TA::SetWeapon(*m, 1, "EX1_536");
            TA::SetSecrets(*m, 1, {"EX1_609"}); // not armed yet: it is side 1's own turn
            CHECK(Cast(*m, 1, "CS2_182") == ActionError::None); // side 0's Snipe goes off
            CHECK_EQ(HV(*m, 0).weaponDurability, 3);
            CHECK_EQ(HV(*m, 1).weaponDurability, 2);
        }
        // Ice Block vs fatigue: on the opponent's turn a Loot Hoarder death draws from an empty deck.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids b = TA::SetBoard(*m, 0, {"EX1_096"});
            TA::SetSecrets(*m, 0, {"EX1_295"});
            TA::SetDeck(*m, 0, {});
            TA::SetFatigue(*m, 0, 5);
            TA::SetHero(*m, 0, 3, 0);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", b[0]) == ActionError::None);
            CHECK(m->GetPhase() == Phase::Play && HP(*m, 0) == 3);
            CHECK(TA::SecretCardIds(*m, 0).empty());
        }
        // ... but a secret does not protect its owner from fatigue on the owner's own turn.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_295"});
            TA::SetDeck(*m, 0, {});
            TA::SetFatigue(*m, 0, 5);
            TA::SetHero(*m, 0, 3, 0);
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK(m->GetPhase() == Phase::Over && m->GetResult().winner == 1);
        }
        // Counterspell vs a spell that has a secret-like effect on an Ice Barrier owner: spells are countered
        // before they resolve (Fireball at the hero does nothing).
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            TA::SetSecrets(*m, 0, {"EX1_287", "EX1_295"});
            TA::SetHero(*m, 0, 4, 0);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", Hero(*m, 0)) == ActionError::None);
            CHECK(HP(*m, 0) == 4 && TA::SecretCardIds(*m, 0) == Strs{"EX1_295"});
        }
        // Secrets of both sides at once, ordered by when they entered play.
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "HUNTER");
            TA::SetSecrets(*m, 0, {"EX1_294"});
            TA::SetSecrets(*m, 1, {"EX1_609"});
            // Side 0 plays a minion on its own turn: only side 1's Snipe is armed.
            CHECK(Cast(*m, 0, "CS2_182") == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).health == 1);
            CHECK(TA::SecretCardIds(*m, 0).size() == 1 && TA::SecretCardIds(*m, 1).empty());
        }
    }

    // ---- enrage, silence, auras ------------------------------------------------------------------

    void TestEnrage()
    {
        // Amani Berserker: +3 Attack while damaged; healing and Silence switch it off.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 0, {"EX1_393"});
            TA::SetMinion(*m, e[0], 2, 2, 3);
            CHECK(MV(*m, e[0]).attack == 5 && MV(*m, e[0]).enraged);
            CHECK(Cast(*m, 0, "EX1_621") == ActionError::None); // Circle of Healing
            MinionView a = MV(*m, e[0]);
            CHECK(a.attack == 2 && !a.enraged && a.health == 3);
            TA::SetMinion(*m, e[0], 2, 1, 3);
            CHECK(MV(*m, e[0]).attack == 5);
            CHECK(Cast(*m, 0, "EX1_332", e[0]) == ActionError::None); // Silence
            a = MV(*m, e[0]);
            CHECK(a.attack == 2 && a.silenced && !a.enraged && a.health == 1);
        }
        // Raging Worgen: Windfury and +1 Attack while damaged.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 0, {"EX1_412"});
            CHECK(MV(*m, e[0]).attack == 3 && !MV(*m, e[0]).windfury);
            TA::SetMinion(*m, e[0], 3, 2, 3);
            MinionView w = MV(*m, e[0]);
            CHECK(w.attack == 4 && w.windfury && w.enraged);
            CHECK(m->Apply(0, Attack(e[0], Hero(*m, 1))) == ActionError::None);
            CHECK(m->Apply(0, Attack(e[0], Hero(*m, 1))) == ActionError::None);
            CHECK(m->Apply(0, Attack(e[0], Hero(*m, 1))) == ActionError::Attack);
            CHECK_EQ(HP(*m, 1), 22);
        }
        // Angry Chicken, Tauren Warrior, Grommash Hellscream.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 0, {"EX1_009", "EX1_390", "EX1_414"});
            TA::SetMinion(*m, e[0], 1, 1, 2);
            TA::SetMinion(*m, e[1], 2, 1, 3);
            TA::SetMinion(*m, e[2], 4, 5, 9);
            CHECK(MV(*m, e[0]).attack == 6 && MV(*m, e[1]).attack == 5 && MV(*m, e[1]).taunt && MV(*m, e[2]).attack == 10);
            std::unique_ptr<Match> n = Fresh();
            Ids h = TA::SetHand(*n, 0, {"EX1_414"});
            CHECK(n->Apply(0, Play(h[0])) == ActionError::None);
            Ids g = TA::BoardIds(*n, 0);
            CHECK(MV(*n, g[0]).charge && MV(*n, g[0]).canAttack && MV(*n, g[0]).attack == 4);
        }
        // Spiteful Smith: the weapon has +2 Attack while the Smith is damaged.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids s = TA::SetBoard(*m, 0, {"CS2_221"});
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK_EQ(HV(*m, 0).weaponAttack, 3);
            TA::SetMinion(*m, s[0], 4, 5, 6);
            CHECK_EQ(HV(*m, 0).weaponAttack, 5);
            CHECK_EQ(HV(*m, 0).attack, 5);
            CHECK(Cast(*m, 0, "CS2_203", s[0]) == ActionError::None); // Ironbeak Owl silences the Smith
            CHECK_EQ(HV(*m, 0).weaponAttack, 3);
        }
        // Frothing Berserker: +1 Attack for every minion that takes damage (Whirlwind hits all minions).
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids f = TA::SetBoard(*m, 0, {"EX1_604"});
            TA::SetBoard(*m, 1, {"CS2_172", "CS2_172"});
            CHECK(Cast(*m, 0, "EX1_400") == ActionError::None);
            CHECK(MV(*m, f[0]).attack == 5 && MV(*m, f[0]).health == 3);
        }
        // Armorsmith: 1 Armor whenever a friendly minion takes damage.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            TA::SetBoard(*m, 0, {"EX1_402", "CS2_182"});
            TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_400") == ActionError::None);
            CHECK_EQ(HV(*m, 0).armor, 2);
        }
    }

    void TestAurasAndSilence()
    {
        // Murloc Warleader: +2/+1 for ALL other Murlocs; losing a health aura never kills.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_507", "CS2_168"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_172"});
            MinionView r = MV(*m, b[1]);
            CHECK(r.attack == 4 && r.health == 2 && r.maxHealth == 2);
            CHECK(MV(*m, e[0]).attack == 4 && MV(*m, e[1]).attack == 3); // enemy Murlocs too, a Raptor is no Murloc
            TA::SetMinion(*m, b[1], 2, 0, 1); // 1 damage: health 1 with the aura
            CHECK(MV(*m, b[1]).health == 1);
            CHECK(Cast(*m, 0, "CS2_029", b[0]) == ActionError::None);
            r = MV(*m, b[1]);
            CHECK(r.id && r.health == 1 && r.maxHealth == 1 && r.attack == 2);
        }
        // Southsea Captain: +1/+1 for other Pirates only.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"NEW1_027", "NEW1_022", "CS2_168"});
            CHECK(MV(*m, b[1]).attack == 4 && MV(*m, b[1]).health == 4 && MV(*m, b[2]).attack == 2 && MV(*m, b[0]).attack == 3);
        }
        // Dire Wolf Alpha: adjacent minions +1 Attack.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "EX1_162", "CS2_168", "CS2_168"});
            CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[1]).attack == 2 && MV(*m, b[2]).attack == 3 && MV(*m, b[3]).attack == 2);
        }
        // Silence removes an aura source's aura and Spell Damage.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids b = TA::SetBoard(*m, 0, {"EX1_507", "CS2_168", "EX1_012"});
            CHECK(MV(*m, b[1]).attack == 4);
            CHECK(Cast(*m, 0, "EX1_332", b[0]) == ActionError::None);
            CHECK(MV(*m, b[1]).attack == 2 && MV(*m, b[0]).silenced);
            CHECK(Cast(*m, 0, "EX1_332", b[2]) == ActionError::None);
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 24);
        }
        // Ironbeak Owl / Spellbreaker: silence removes Taunt.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_179", "EX1_390"});
            CHECK(MV(*m, e[0]).taunt);
            CHECK(Cast(*m, 0, "EX1_048", e[0]) == ActionError::None);
            CHECK(!MV(*m, e[0]).taunt && MV(*m, e[0]).silenced);
            Ids h = TA::SetHand(*m, 0, {"CS2_203"});
            CHECK(m->Apply(0, Play(h[0], e[1])) == ActionError::None);
            CHECK(!MV(*m, e[1]).taunt);
        }
        // Mass Dispel: silence all enemy minions and draw a card.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids e = TA::SetBoard(*m, 1, {"CS2_179", "EX1_007"});
            Ids mine = TA::SetBoard(*m, 0, {"CS2_179"});
            CHECK(Cast(*m, 0, "EX1_626") == ActionError::None);
            CHECK(MV(*m, e[0]).silenced && MV(*m, e[1]).silenced && !MV(*m, e[0]).taunt);
            CHECK(!MV(*m, mine[0]).silenced && MV(*m, mine[0]).taunt);
            CHECK_EQ(Hand(*m, 0).size(), 1);
        }
        // Earth Shock: silence a minion, then 1 damage (+1 with Spell Damage).
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids e = TA::SetBoard(*m, 1, {"EX1_393"});
            TA::SetBoard(*m, 0, {"EX1_012"});
            CHECK(Cast(*m, 0, "EX1_245", e[0]) == ActionError::None);
            MinionView a = MV(*m, e[0]);
            CHECK(a.silenced && a.health == 1 && a.attack == 2);
        }
        // Lightspawn: Attack always equals Health.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids l = TA::SetBoard(*m, 0, {"EX1_335"});
            CHECK(MV(*m, l[0]).attack == 5);
            TA::SetMinion(*m, l[0], 0, 3, 5);
            CHECK(MV(*m, l[0]).attack == 3);
            CHECK(Cast(*m, 0, "EX1_332", l[0]) == ActionError::None);
            CHECK(MV(*m, l[0]).attack == 0);
        }
    }

    void TestStealthAndPoison()
    {
        // Master of Disguise: a friendly minion gains Stealth; the enemy can neither target nor attack it.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "NEW1_014", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).stealth);
            ToSide1(*m);
            Ids a = TA::SetBoard(*m, 1, {"CS2_172"});
            CHECK(Cast(*m, 1, "CS2_029", b[0]) == ActionError::Target);
            CHECK(m->Apply(1, Attack(a[0], b[0])) == ActionError::Target);
            // Without a friendly minion the battlecry has no target.
            std::unique_ptr<Match> n = Fresh("ROGUE");
            CHECK(Cast(*n, 0, "NEW1_014") == ActionError::None);
        }
        // Stealth minions of the list: Worgen Infiltrator, Jungle Panther, Stranglethorn Tiger.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_010", "EX1_017", "EX1_028"});
            for (EntityId id : b)
                CHECK(MV(*m, id).stealth);
            ToSide1(*m);
            for (EntityId id : b)
                CHECK(Cast(*m, 1, "CS2_029", id) == ActionError::Target);
        }
        // Patient Assassin: Stealth, destroys any minion it damages.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids a = TA::SetBoard(*m, 0, {"EX1_522"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            MinionView v = MV(*m, a[0]);
            CHECK(v.stealth && v.poisonous && v.attack == 1);
            CHECK(m->Apply(0, Attack(a[0], e[0])) == ActionError::None);
            CHECK(!OnBoard(*m, 1, e[0]));
        }
        // Emperor Cobra: Poisonous.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"EX1_170"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            CHECK(MV(*m, a[0]).poisonous);
            CHECK(m->Apply(0, Attack(a[0], e[0])) == ActionError::None);
            CHECK(!OnBoard(*m, 1, e[0]) && !OnBoard(*m, 0, a[0])); // the ogre is destroyed, the Cobra dies of the counter attack
        }
        // Blood Imp and Young Priestess: at the end of your turn another random friendly minion gets +1 Health.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"CS2_059", "CS2_182"});
            CHECK(MV(*m, b[0]).stealth);
            m->Apply(0, End());
            CHECK(MV(*m, b[1]).health == 6 && MV(*m, b[0]).health == 1);
            std::unique_ptr<Match> n = Fresh();
            Ids c = TA::SetBoard(*n, 0, {"EX1_004", "CS2_182"});
            n->Apply(0, End());
            CHECK(MV(*n, c[1]).maxHealth == 6);
            std::unique_ptr<Match> k = Fresh();
            Ids d = TA::SetBoard(*k, 0, {"EX1_004"}); // alone: nothing happens
            k->Apply(0, End());
            CHECK(MV(*k, d[0]).maxHealth == 1);
        }
    }

    void TestFreeze()
    {
        // Cone of Cold: the target and its neighbours are frozen and take 1 damage.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_172", "CS2_182", "CS2_172", "CS2_200"});
            CHECK(Cast(*m, 0, "EX1_275", e[1]) == ActionError::None);
            CHECK(MV(*m, e[0]).frozen && MV(*m, e[1]).frozen && MV(*m, e[2]).frozen && !MV(*m, e[3]).frozen);
            CHECK(MV(*m, e[0]).health == 1 && MV(*m, e[1]).health == 4 && MV(*m, e[2]).health == 1 && MV(*m, e[3]).health == 7);
            // at the edge only one neighbour
            Ids f = TA::SetBoard(*m, 1, {"CS2_182", "CS2_172", "CS2_172"});
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_275", f[0]) == ActionError::None);
            CHECK(MV(*m, f[0]).frozen && MV(*m, f[1]).frozen && !MV(*m, f[2]).frozen);
        }
        // Blizzard: 2 damage to all enemy minions and freeze them.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182", "CS2_200"});
            Ids mine = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "CS2_028") == ActionError::None);
            CHECK(MV(*m, e[0]).health == 3 && MV(*m, e[1]).health == 5 && MV(*m, e[0]).frozen && MV(*m, e[1]).frozen);
            CHECK(MV(*m, mine[0]).health == 5 && !MV(*m, mine[0]).frozen);
        }
        // Frost Elemental: freeze a character (here the enemy hero).
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_283", Hero(*m, 1)) == ActionError::None);
            CHECK(HV(*m, 1).frozen);
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_283") == ActionError::Target); // heroes are valid targets
        }
        // Ice Lance: freezes; a frozen target takes 4 damage (5 with Spell Damage) instead.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            CHECK(Cast(*m, 0, "CS2_031", Hero(*m, 1)) == ActionError::None);
            CHECK(HV(*m, 1).frozen && HP(*m, 1) == 30);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "CS2_031", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
            TA::SetBoard(*m, 0, {"EX1_012"});
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "CS2_031", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 21);
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetFrozen(*m, e[0], true);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "CS2_031", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 2);
        }
    }

    void TestCombo()
    {
        // Defias Ringleader: Combo summons a 2/1 Defias Bandit.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"EX1_131", "GAME_005", "EX1_131"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_131"});
            CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
            Strs const b = Board(*m, 0);
            CHECK(b.size() == 3 && std::count(b.begin(), b.end(), std::string("EX1_131t")) == 1);
        }
        // Kidnapper: Combo returns a minion; without Combo it has no target.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"NEW1_005", "GAME_005", "NEW1_005"});
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(m->Apply(0, Play(h[2], e[0])) == ActionError::None);
            CHECK(Board(*m, 1).empty() && Hand(*m, 1) == Strs{"CS2_182"});
        }
        // Perdition's Blade: 1 damage; Combo: 2.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"EX1_133", "GAME_005", "EX1_133"});
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 29);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK(m->Apply(0, Play(h[2], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 27);
            CHECK_EQ(HV(*m, 0).weaponDurability, 2);
        }
        // Cold Blood: +2 Attack; Combo +4.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"CS2_073", "CS2_073"});
            CHECK(m->Apply(0, Play(h[0], b[0])) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 6);
            CHECK(m->Apply(0, Play(h[1], b[0])) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 10);
        }
        // Edwin VanCleef: +2/+2 per card played earlier this turn.
        for (int earlier = 0; earlier <= 2; ++earlier)
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Strs hand;
            for (int i = 0; i < earlier; ++i)
                hand.push_back("GAME_005");
            hand.push_back("EX1_613");
            Ids h = TA::SetHand(*m, 0, hand);
            for (size_t i = 0; i < h.size(); ++i)
                CHECK(m->Apply(0, Play(h[i])) == ActionError::None);
            MinionView e = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(e.attack == 2 + 2 * earlier && e.health == 2 + 2 * earlier);
        }
        // Preparation: the next spell costs (3) less; only that one.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"EX1_145", "CS2_029", "CS2_029"});
            CHECK_EQ(TA::HandCost(*m, h[1]), 4);
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(TA::HandCost(*m, h[1]), 1);
            CHECK(m->Apply(0, Play(h[1], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(TA::HandCost(*m, h[2]), 4);
            CHECK_EQ(HV(*m, 0).mana, 9);
        }
        // Kirin Tor Mage: the next Secret costs (0); the one after is full price.
        {
            std::unique_ptr<Match> m = Fresh("MAGE");
            Ids h = TA::SetHand(*m, 0, {"EX1_612", "EX1_609", "EX1_287"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(TA::HandCost(*m, h[1]), 0);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK_EQ(HV(*m, 0).mana, 7);
            CHECK_EQ(TA::HandCost(*m, h[2]), 3);
            CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
            CHECK_EQ(HV(*m, 0).mana, 4);
        }
        // Millhouse Manastorm: enemy spells cost (0) during their next turn only.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "NEW1_029") == ActionError::None);
            ToSide1(*m);
            Ids h = TA::SetHand(*m, 1, {"CS2_029", "CS2_029"});
            CHECK_EQ(TA::HandCost(*m, h[0]), 0);
            CHECK(m->Apply(1, Play(h[0], Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HV(*m, 1).mana, 10);
            ToSide0(*m);
            ToSide1(*m);
            Ids g = TA::SetHand(*m, 1, {"CS2_029"});
            CHECK_EQ(TA::HandCost(*m, g[0]), 4);
        }
    }

    void TestOverloadClassic()
    {
        // Feral Spirit: two Spirit Wolves with Taunt; Overload (2).
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            CHECK(Cast(*m, 0, "EX1_248") == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{"EX1_tk11", "EX1_tk11"}));
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).taunt);
            CHECK_EQ(SV(*m, 0).overloadOwed, 2);
        }
        // Unbound Elemental: +1/+1 for every Overload card (spells, minions, weapons) played; not for others.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids u = TA::SetBoard(*m, 0, {"EX1_258"});
            CHECK(Cast(*m, 0, "EX1_238", Hero(*m, 1)) == ActionError::None);
            CHECK(MV(*m, u[0]).attack == 3 && MV(*m, u[0]).health == 5);
            CHECK(Cast(*m, 0, "EX1_243") == ActionError::None);
            CHECK(MV(*m, u[0]).attack == 4);
            CHECK(Cast(*m, 0, "EX1_567") == ActionError::None);
            CHECK(MV(*m, u[0]).attack == 5);
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            CHECK(MV(*m, u[0]).attack == 5);
        }
        // Lava Burst: 5 damage, Overload (2).
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            CHECK(Cast(*m, 0, "EX1_241", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 25);
            CHECK_EQ(SV(*m, 0).overloadOwed, 2);
        }
        // Forked Lightning: 2 damage to 2 random enemy minions; needs two.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            TA::SetBoard(*m, 1, {"CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"EX1_251"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::Requirement);
            Ids e2 = TA::SetBoard(*m, 1, {"CS2_182", "CS2_182", "CS2_182"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            int hurt = 0;
            for (EntityId id : e2)
                hurt += MV(*m, id).health == 3;
            CHECK_EQ(hurt, 2);
        }
        // Lightning Storm: 2-3 damage to every enemy minion, rolled per minion.
        {
            int twos = 0, threes = 0, other = 0;
            for (uint32_t seed = 1; seed <= 12; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("SHAMAN", "WARRIOR", seed);
                Ids e = TA::SetBoard(*m, 1, {"CS2_200", "CS2_200", "CS2_200"});
                Ids mine = TA::SetBoard(*m, 0, {"CS2_200"});
                CHECK(Cast(*m, 0, "EX1_259") == ActionError::None);
                for (EntityId id : e)
                {
                    int const d = 7 - MV(*m, id).health;
                    d == 2 ? ++twos : d == 3 ? ++threes : ++other;
                }
                CHECK(MV(*m, mine[0]).health == 7);
            }
            CHECK(twos > 0 && threes > 0 && other == 0);
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetBoard(*m, 0, {"EX1_012"});
            CHECK(Cast(*m, 0, "EX1_259") == ActionError::None);
            int const d = 7 - MV(*m, e[0]).health;
            CHECK(d == 3 || d == 4);
        }
        // Doomhammer: Windfury weapon (2/8) with Overload (2); Dust Devil, Earth Elemental.
        {
            std::unique_ptr<Match> m = Fresh("SHAMAN");
            CHECK(Cast(*m, 0, "EX1_567") == ActionError::None);
            HeroView h = HV(*m, 0);
            CHECK(h.weaponAttack == 2 && h.weaponDurability == 8 && h.weaponWindfury && h.canAttack);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::Attack);
            CHECK_EQ(HP(*m, 1), 26);
            CHECK_EQ(SV(*m, 0).overloadOwed, 2);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_250") == ActionError::None);
            CHECK_EQ(SV(*m, 0).overloadOwed, 5);
            CHECK(Cast(*m, 0, "EX1_243") == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[1]).windfury);
        }
    }

    void TestChooseOneClassic()
    {
        // Wrath: 3 damage to a minion; or 1 damage and draw.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            Ids g = TA::SetHand(*m, 0, {"EX1_154", "EX1_154", "EX1_154"});
            CHECK(m->Apply(0, Play(g[0], Hero(*m, 1), 1)) == ActionError::Target); // minions only
            CHECK(m->Apply(0, Play(g[0], e[0], 1)) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 4);
            CHECK(m->Apply(0, Play(g[1], e[0], 2)) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 3 && Hand(*m, 0).size() == 2);
        }
        // Starfall: 5 damage to a minion; or 2 damage to all enemy minions. Spell Damage counts.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200", "CS2_200"});
            TA::SetBoard(*m, 0, {"EX1_012"});
            Ids h = TA::SetHand(*m, 0, {"NEW1_007", "NEW1_007"});
            CHECK(m->Apply(0, Play(h[0], e[0], 1)) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 1);
            CHECK(m->Apply(0, Play(h[1], 0, 2)) == ActionError::None);
            CHECK(!OnBoard(*m, 1, e[0]) && MV(*m, e[1]).health == 4);
        }
        // Nourish: 2 Mana Crystals (full); or draw 3 cards. No Excess Mana at 10 crystals.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            TA::SetMana(*m, 0, 5, 5);
            CHECK(Cast(*m, 0, "EX1_164", 0, 1) == ActionError::None);
            CHECK(HV(*m, 0).maxMana == 7 && HV(*m, 0).mana == 2);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_164", 0, 1) == ActionError::None);
            CHECK(HV(*m, 0).maxMana == 10 && HV(*m, 0).mana == 5 && Hand(*m, 0).empty());
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_164", 0, 2) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 3);
        }
        // Keeper of the Grove: 2 damage to a character; or silence a minion.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids e = TA::SetBoard(*m, 1, {"CS2_179"});
            Ids h = TA::SetHand(*m, 0, {"EX1_166", "EX1_166"});
            int c1 = 0, c2 = 0;
            for (Action const& a : m->LegalActions(0))
                if (a.source == h[0])
                {
                    c1 += a.choice == 1;
                    c2 += a.choice == 2;
                }
            CHECK(c1 > 0 && c2 > 0);
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1), 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(m->Apply(0, Play(h[1], Hero(*m, 1), 2)) == ActionError::Target); // Silence: minions only
            CHECK(m->Apply(0, Play(h[1], e[0], 2)) == ActionError::None);
            CHECK(MV(*m, e[0]).silenced && !MV(*m, e[0]).taunt);
            CHECK(Board(*m, 0) == (Strs{"EX1_166", "EX1_166"}));
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).attack == 2);
        }
        // Druid of the Claw: Cat Form (Charge) / Bear Form (+2 Health and Taunt).
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids h = TA::SetHand(*m, 0, {"EX1_165", "EX1_165"});
            CHECK(m->Apply(0, Play(h[0], 0, 1)) == ActionError::None);
            CHECK(m->Apply(0, Play(h[1], 0, 2)) == ActionError::None);
            Ids b = TA::BoardIds(*m, 0);
            CHECK(MV(*m, b[0]).charge && MV(*m, b[0]).health == 4);
            CHECK(MV(*m, b[1]).taunt && MV(*m, b[1]).health == 6 && MV(*m, b[1]).attack == 4);
        }
        // Power of the Wild: +1/+1 for your minions; or a 3/2 Panther.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_182"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"EX1_160", "EX1_160"});
            CHECK(m->Apply(0, Play(h[0], 0, 1)) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[1]).health == 6 && MV(*m, e[0]).health == 5);
            CHECK(m->Apply(0, Play(h[1], 0, 2)) == ActionError::None);
            Strs const bd = Board(*m, 0);
            CHECK(bd.size() == 3 && bd[2] == "EX1_160t");
        }
        // Mark of Nature: +4 Attack; or +4 Health and Taunt.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            Ids b = TA::SetBoard(*m, 0, {"CS2_172", "CS2_172"});
            Ids h = TA::SetHand(*m, 0, {"EX1_155", "EX1_155"});
            CHECK(m->Apply(0, Play(h[0], b[0], 1)) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 7);
            CHECK(m->Apply(0, Play(h[1], b[1], 2)) == ActionError::None);
            CHECK(MV(*m, b[1]).health == 6 && MV(*m, b[1]).taunt && MV(*m, b[1]).attack == 3);
        }
        // Ancient of Lore: draw 2; or restore 5 Health (a character). Ancient of War: +5 Attack; or +5 Health and Taunt.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            TA::SetHero(*m, 0, 20, 0);
            Ids h = TA::SetHand(*m, 0, {"NEW1_008", "NEW1_008", "EX1_178", "EX1_178"});
            CHECK(m->Apply(0, Play(h[0], 0, 1)) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 5);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(m->Apply(0, Play(h[1], Hero(*m, 0), 2)) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 25);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(m->Apply(0, Play(h[2], 0, 1)) == ActionError::None);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(m->Apply(0, Play(h[3], 0, 2)) == ActionError::None);
            Ids b = TA::BoardIds(*m, 0);
            CHECK(b.size() == 4);
            CHECK(MV(*m, b[2]).attack == 10 && MV(*m, b[2]).health == 5);
            CHECK(MV(*m, b[3]).taunt && MV(*m, b[3]).health == 10 && MV(*m, b[3]).attack == 5);
        }
        // Bite: +4 Attack this turn and 4 Armor.
        {
            std::unique_ptr<Match> m = Fresh("DRUID");
            CHECK(Cast(*m, 0, "EX1_570") == ActionError::None);
            CHECK(HV(*m, 0).attack == 4 && HV(*m, 0).armor == 4);
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
        }
    }

    void TestWeapons()
    {
        // Gorehowl: attacking a minion costs 1 Attack instead of 1 Durability.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            TA::SetWeapon(*m, 0, "EX1_411");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(m->Apply(0, Attack(Hero(*m, 0), e[0])) == ActionError::None);
            HeroView h = HV(*m, 0);
            CHECK(h.weaponAttack == 6 && h.weaponDurability == 1);
            CHECK_EQ(HP(*m, 0), 26);
            TA::ReadyAll(*m);
            TA::SetBoard(*m, 1, {});
            CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None); // a hero: durability
            CHECK(HV(*m, 0).weaponId == 0);
            CHECK_EQ(HP(*m, 1), 24);
        }
        // Sword of Justice: +1/+1 for every summoned minion, the sword loses 1 Durability.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            TA::SetWeapon(*m, 0, "EX1_366");
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            MinionView r = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(r.attack == 3 && r.health == 2);
            CHECK_EQ(HV(*m, 0).weaponDurability, 4);
            for (int i = 0; i < 4; ++i)
                CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            CHECK_EQ(HV(*m, 0).weaponId, 0);
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[5]).attack == 2);
        }
        // Arathi Weaponsmith: equips a 2/2 Battle Axe. Upgrade!: +1/+1 or a 1/3 Heavy Axe.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            CHECK(Cast(*m, 0, "EX1_398") == ActionError::None);
            CHECK(HV(*m, 0).weaponCardId == "EX1_398t" && HV(*m, 0).weaponAttack == 2 && HV(*m, 0).weaponDurability == 2);
            CHECK(Cast(*m, 0, "EX1_409") == ActionError::None);
            CHECK(HV(*m, 0).weaponCardId == "EX1_398t" && HV(*m, 0).weaponAttack == 3 && HV(*m, 0).weaponDurability == 3);
            std::unique_ptr<Match> n = Fresh("WARRIOR");
            CHECK(Cast(*n, 0, "EX1_409") == ActionError::None);
            CHECK(HV(*n, 0).weaponCardId == "EX1_409t" && HV(*n, 0).weaponAttack == 1 && HV(*n, 0).weaponDurability == 3);
        }
        // Gladiator's Longbow: the hero is Immune while attacking.
        {
            std::unique_ptr<Match> m = Fresh("HUNTER");
            TA::SetWeapon(*m, 0, "DS1_188");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(m->Apply(0, Attack(Hero(*m, 0), e[0])) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 30);
            CHECK_EQ(HV(*m, 0).weaponDurability, 1);
            // Without it the hero takes the counter damage.
            std::unique_ptr<Match> n = Fresh("HUNTER");
            TA::SetWeapon(*n, 0, "CS2_106");
            Ids f = TA::SetBoard(*n, 1, {"CS2_182"});
            CHECK(n->Apply(0, Attack(Hero(*n, 0), f[0])) == ActionError::None);
            CHECK_EQ(HP(*n, 0), 26);
        }
        // Bloodsail Corsair: -1 Durability for the enemy weapon; Captain Greenskin: +1/+1; Bloodsail Raider: Attack.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            TA::SetWeapon(*m, 1, "CS2_106");
            CHECK(Cast(*m, 0, "NEW1_025") == ActionError::None);
            CHECK_EQ(HV(*m, 1).weaponDurability, 1);
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK(Cast(*m, 0, "NEW1_024") == ActionError::None);
            CHECK(HV(*m, 0).weaponAttack == 4 && HV(*m, 0).weaponDurability == 3);
            CHECK(Cast(*m, 0, "NEW1_018") == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[2]).attack == 2 + 4);
            // The enemy weapon breaks at 0 Durability.
            CHECK(Cast(*m, 0, "NEW1_025") == ActionError::None);
            CHECK_EQ(HV(*m, 1).weaponId, 0);
            // Without a weapon Greenskin and the Raider do nothing special.
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "NEW1_024") == ActionError::None);
            CHECK(Cast(*n, 0, "NEW1_018") == ActionError::None);
            CHECK(MV(*n, TA::BoardIds(*n, 0)[1]).attack == 2);
        }
        // Southsea Deckhand: Charge only while the hero has a weapon.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids h = TA::SetHand(*m, 0, {"CS2_146", "CS2_146"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            Ids b = TA::BoardIds(*m, 0);
            CHECK(!MV(*m, b[0]).charge && !MV(*m, b[0]).canAttack);
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK(MV(*m, b[0]).charge && MV(*m, b[0]).canAttack);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[1]).canAttack);
            TA::SetWeapon(*m, 0, "");
            CHECK(!MV(*m, TA::BoardIds(*m, 0)[1]).charge);
        }
    }

    void TestCostAuras()
    {
        // Venture Co. Mercenary: YOUR minions cost (3) more, spells are unaffected.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"CS2_227"});
            Ids h = TA::SetHand(*m, 0, {"CS2_168", "CS2_029"});
            Ids g = TA::SetHand(*m, 1, {"CS2_168"});
            CHECK_EQ(TA::HandCost(*m, h[0]), 4);
            CHECK_EQ(TA::HandCost(*m, h[1]), 4);
            CHECK_EQ(TA::HandCost(*m, g[0]), 1);
            CHECK_EQ(m->ViewFor(0, 0).self.hand[0].cost, 4);
        }
        // Summoning Portal: minions cost (2) less, but not less than (1).
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetBoard(*m, 0, {"EX1_315"});
            Ids h = TA::SetHand(*m, 0, {"CS2_168", "CS2_182", "CS2_200", "CS2_231", "CS2_029"});
            CHECK_EQ(TA::HandCost(*m, h[0]), 1);
            CHECK_EQ(TA::HandCost(*m, h[1]), 2);
            CHECK_EQ(TA::HandCost(*m, h[2]), 4);
            CHECK_EQ(TA::HandCost(*m, h[3]), 0); // a 0 cost Wisp stays free
            CHECK_EQ(TA::HandCost(*m, h[4]), 4);
        }
        // The Giants.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids h = TA::SetHand(*m, 0, {"EX1_105", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK_EQ(TA::HandCost(*m, h[0]), 8);
            Ids g = TA::SetHand(*m, 0, {"EX1_586"});
            TA::SetBoard(*m, 0, {"CS2_168", "CS2_168"});
            TA::SetBoard(*m, 1, {"CS2_168", "CS2_168", "CS2_168"});
            CHECK_EQ(TA::HandCost(*m, g[0]), 5);
            Ids k = TA::SetHand(*m, 0, {"EX1_620"});
            CHECK_EQ(TA::HandCost(*m, k[0]), 20);
            TA::SetHero(*m, 0, 20, 0);
            CHECK_EQ(TA::HandCost(*m, k[0]), 10);
            TA::SetHero(*m, 0, 1, 0);
            CHECK_EQ(TA::HandCost(*m, k[0]), 0); // 20 - 29 floors at 0
        }
        // Dread Corsair: costs (1) less per Attack of the weapon.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids h = TA::SetHand(*m, 0, {"NEW1_022"});
            CHECK_EQ(TA::HandCost(*m, h[0]), 4);
            TA::SetWeapon(*m, 0, "CS2_106");
            CHECK_EQ(TA::HandCost(*m, h[0]), 1);
            TA::SetWeapon(*m, 0, "EX1_411");
            CHECK_EQ(TA::HandCost(*m, h[0]), 0);
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).taunt);
        }
        // Sorcerer's Apprentice together with Preparation.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_608"});
            Ids h = TA::SetHand(*m, 0, {"EX1_145", "CS2_029"});
            CHECK_EQ(TA::HandCost(*m, h[1]), 3);
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(TA::HandCost(*m, h[1]), 0);
        }
    }

    // ---- triggered minions ----------------------------------------------------------------------------

    void TestTriggeredMinions()
    {
        // Wild Pyromancer: after you cast a spell, 1 damage to ALL minions.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"NEW1_020", "CS2_168", "CS2_172"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK(MV(*m, b[0]).health == 1 && !OnBoard(*m, 0, b[1]) && MV(*m, b[2]).health == 1 && MV(*m, e[0]).health == 4);
        }
        // Two Wild Pyromancers: both resolve for one spell, both die; the loop ends.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"NEW1_020", "NEW1_020", "EX1_007"});
            Ids h = TA::SetHand(*m, 0, {"GAME_005", "GAME_005"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(Board(*m, 0).empty() || Board(*m, 0) == Strs{"EX1_007"});
            CHECK(!Capped(*m));
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK(!Capped(*m) && m->GetPhase() == Phase::Play);
        }
        // Pyromancer with Violet Teacher and Gadgetzan Auctioneer: the apprentice and the card come on the cast, the
        // Pyromancer's damage after it kills the apprentice.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"NEW1_020", "NEW1_026", "EX1_095"});
            CHECK(Cast(*m, 0, "GAME_005") == ActionError::None);
            CHECK(!Has(Board(*m, 0), "NEW1_026t") && Board(*m, 0).size() == 3 && Hand(*m, 0).size() == 1);
            CHECK(!Capped(*m));
        }
        // Mana Wyrm / Questing Adventurer / Violet Teacher / Gadgetzan Auctioneer.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"NEW1_012", "EX1_044"});
            Ids h = TA::SetHand(*m, 0, {"GAME_005", "GAME_005", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 2 && MV(*m, b[1]).attack == 3 && MV(*m, b[1]).health == 3);
            CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
            CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[1]).attack == 5 && MV(*m, b[1]).health == 5);
            std::unique_ptr<Match> n = Fresh();
            Ids q = TA::SetHand(*n, 0, {"EX1_044"});
            CHECK(n->Apply(0, Play(q[0])) == ActionError::None);
            CHECK(MV(*n, TA::BoardIds(*n, 0)[0]).attack == 2); // not for itself
            std::unique_ptr<Match> k = Fresh();
            Ids t = TA::SetBoard(*k, 0, {"NEW1_026"});
            CHECK(Cast(*k, 0, "GAME_005") == ActionError::None);
            CHECK(Board(*k, 0) == (Strs{"NEW1_026", "NEW1_026t"}) && MV(*k, t[0]).attack == 3);
        }
        // Lightwarden: +2 Attack whenever a character is healed.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids b = TA::SetBoard(*m, 0, {"EX1_001", "CS2_182"});
            CHECK(Cast(*m, 0, "EX1_621") == ActionError::None); // nothing damaged: no healing
            CHECK(MV(*m, b[0]).attack == 1);
            TA::SetMinion(*m, b[1], 4, 3, 5);
            CHECK(Cast(*m, 0, "EX1_621") == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 3);
        }
        // Flesheating Ghoul: +1 Attack for every minion that dies.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids g = TA::SetBoard(*m, 0, {"tt_004"});
            TA::SetBoard(*m, 1, {"CS2_168", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_400") == ActionError::None);
            CHECK(MV(*m, g[0]).attack == 4 && MV(*m, g[0]).health == 2);
        }
        // Murloc Tidecaller: +1 Attack whenever a Murloc is summoned (either side), not for itself.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_509") == ActionError::None);
            Ids t = TA::BoardIds(*m, 0);
            CHECK(MV(*m, t[0]).attack == 1);
            CHECK(Cast(*m, 0, "CS2_168") == ActionError::None);
            CHECK(MV(*m, t[0]).attack == 2);
            CHECK(Cast(*m, 0, "CS2_172") == ActionError::None); // a Beast
            CHECK(MV(*m, t[0]).attack == 2);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_168") == ActionError::None);
            CHECK(MV(*m, t[0]).attack == 3);
        }
        // Imp Master: at the end of your turn 1 damage to itself and a 1/1 Imp to its right.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_597", "CS2_168"});
            m->Apply(0, End());
            CHECK(MV(*m, b[0]).health == 4);
            CHECK(Board(*m, 0) == (Strs{"EX1_597", "EX1_598", "CS2_168"}));
        }
        // Ragnaros the Firelord: Can't Attack; 8 damage to a random enemy at the end of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids r = TA::SetBoard(*m, 0, {"EX1_298"});
            CHECK(MV(*m, r[0]).cantAttack && !MV(*m, r[0]).canAttack);
            CHECK(m->Apply(0, Attack(r[0], Hero(*m, 1))) == ActionError::Attack);
            m->Apply(0, End());
            CHECK_EQ(HP(*m, 1), 22);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, {"EX1_298"});
            Ids e = TA::SetBoard(*n, 1, {"CS2_200"});
            n->Apply(0, End());
            CHECK((HP(*n, 1) == 22 && MV(*n, e[0]).health == 7) || (HP(*n, 1) == 30 && !OnBoard(*n, 1, e[0])));
        }
        // Doomsayer: at the start of your turn destroy ALL minions.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"NEW1_021", "CS2_182"});
            TA::SetBoard(*m, 1, {"CS2_200"});
            m->Apply(0, End());
            CHECK(Board(*m, 0).size() == 2 && Board(*m, 1).size() == 1);
            m->Apply(1, End());
            CHECK(Board(*m, 0).empty() && Board(*m, 1).empty());
            CHECK(m->GetPhase() == Phase::Play && m->ActiveSide() == 0);
        }
        // Doomsayer's start-of-turn trigger kills deathrattle minions too (Harvest Golem leaves a Damaged Golem).
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"NEW1_021", "EX1_556"});
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK(Board(*m, 0) == Strs{"skele21"});
        }
        // Demolisher: 2 damage to a random enemy at the start of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_102"});
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK_EQ(HP(*m, 1), 28);
        }
        // Lightwell: restore 3 Health to a damaged friendly character at the start of your turn.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids b = TA::SetBoard(*m, 0, {"EX1_341", "CS2_182"});
            TA::SetMinion(*m, b[1], 4, 3, 5);
            TA::SetHero(*m, 0, 20, 0);
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK_EQ((HP(*m, 0) - 20) + (MV(*m, b[1]).health - 3), 3);
        }
        // Alarm-o-Bot: swap with a random minion of the hand at the start of your turn.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, {"EX1_006"});
            TA::SetHand(*m, 0, {"CS2_182", "CS2_029"});
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK(Board(*m, 0) == Strs{"CS2_182"});
            Strs const h = Hand(*m, 0);
            CHECK(h.size() == 3 && Has(h, "EX1_006") && Has(h, "CS2_029"));
            CHECK(!MV(*m, TA::BoardIds(*m, 0)[0]).canAttack);
            // No minion in hand: nothing happens.
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 0, {"EX1_006"});
            TA::SetHand(*n, 0, {"CS2_029"});
            n->Apply(0, End());
            n->Apply(1, End());
            CHECK(Board(*n, 0) == Strs{"EX1_006"});
        }
        // Ysera: a Dream Card at the end of your turn (all five occur).
        {
            std::set<std::string> seen;
            for (uint32_t seed = 1; seed <= 60; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                TA::SetBoard(*m, 0, {"EX1_572"});
                m->Apply(0, End());
                Strs const h = Hand(*m, 0);
                CHECK(h.size() == 1);
                if (!h.empty())
                    seen.insert(h[0]);
            }
            CHECK(seen == (std::set<std::string>{"DREAM_01", "DREAM_02", "DREAM_03", "DREAM_04", "DREAM_05"}));
        }
    }

    void TestDreamCards()
    {
        // Ysera Awakens: 5 damage to all characters except Ysera.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_572", "CS2_182"});
            TA::SetBoard(*m, 1, {"CS2_182", "DREAM_01"});
            CHECK(Cast(*m, 0, "DREAM_02") == ActionError::None);
            CHECK(HP(*m, 0) == 25 && HP(*m, 1) == 25);
            CHECK(Board(*m, 0) == Strs{"EX1_572"} && Board(*m, 1).empty() && MV(*m, b[0]).health == 12);
        }
        // Dream returns a minion; Laughing Sister cannot be targeted by spells; Emerald Drake 7/6.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_182", "DREAM_01"});
            CHECK(Cast(*m, 0, "DREAM_04", e[1]) == ActionError::Target);
            CHECK(Cast(*m, 0, "CS2_029", e[1]) == ActionError::Target);
            CHECK(Cast(*m, 0, "DREAM_04", e[0]) == ActionError::None);
            CHECK(Hand(*m, 1) == Strs{"CS2_182"} && Board(*m, 1) == Strs{"DREAM_01"});
            CHECK(Cast(*m, 0, "DREAM_03") == ActionError::None);
            MinionView d = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(d.attack == 7 && d.health == 6);
        }
        // Nightmare: +5/+5, destroyed at the start of your next turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "DREAM_05", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 9 && MV(*m, b[0]).health == 10);
            m->Apply(0, End());
            CHECK(OnBoard(*m, 0, b[0]));
            m->Apply(1, End());
            CHECK(Board(*m, 0).empty());
        }
    }

    void TestPriest()
    {
        // Auchenai Soulpriest: healing deals damage (Circle of Healing, Lesser Heal).
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids b = TA::SetBoard(*m, 0, {"EX1_591", "CS2_182"});
            CHECK(Cast(*m, 0, "EX1_621") == ActionError::None);
            CHECK(MV(*m, b[0]).health == 1 && MV(*m, b[1]).health == 1);
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 28);
            // Without Auchenai the same cards heal.
            std::unique_ptr<Match> n = Fresh("PRIEST");
            TA::SetHero(*n, 0, 20, 0);
            CHECK(n->Apply(0, Power(*n, 0, Hero(*n, 0))) == ActionError::None);
            CHECK_EQ(HP(*n, 0), 22);
        }
        // Prophet Velen: doubles the damage and healing of spells and the hero power.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetBoard(*m, 0, {"EX1_350"});
            TA::SetHero(*m, 0, 20, 0);
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 0))) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 24);
            CHECK(Cast(*m, 0, "CS2_024", Hero(*m, 1)) == ActionError::None); // Frostbolt 3 -> 6
            CHECK_EQ(HP(*m, 1), 24);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "CS2_029", Hero(*m, 1)) == ActionError::None); // Fireball 6 -> 12
            CHECK_EQ(HP(*m, 1), 12);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_624", Hero(*m, 1)) == ActionError::None); // Holy Fire 5 -> 10, heal 5 -> 10
            CHECK_EQ(HP(*m, 1), 2);
            CHECK_EQ(HP(*m, 0), 30);
            // Not for a battlecry: Priestess of Elune heals 4 only.
            TA::SetHero(*m, 0, 10, 0);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_583") == ActionError::None);
            CHECK_EQ(HP(*m, 0), 14);
        }
        // Velen with Shadowform: Mind Spike 2 -> 4.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetBoard(*m, 0, {"EX1_350"});
            CHECK(Cast(*m, 0, "EX1_625") == ActionError::None);
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
        }
        // Shadowform: Mind Spike (2 damage); a second Shadowform: Mind Shatter (3 damage).
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 0))) == ActionError::None); // Lesser Heal used
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 0))) == ActionError::Used);
            CHECK(Cast(*m, 0, "EX1_625") == ActionError::None);
            CHECK(HV(*m, 0).powerCardId == "EX1_625t" && !HV(*m, 0).powerUsed);
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 1))) == ActionError::None); // usable again this turn
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 1))) == ActionError::Used);
            ToSide1(*m);
            ToSide0(*m);
            CHECK(Cast(*m, 0, "EX1_625") == ActionError::None);
            CHECK(HV(*m, 0).powerCardId == "EX1_625t2");
            CHECK(m->Apply(0, Power(*m, 0, Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 25);
            ToSide1(*m);
            ToSide0(*m);
            CHECK(Cast(*m, 0, "EX1_625") == ActionError::None);
            CHECK(HV(*m, 0).powerCardId == "EX1_625t2"); // stays Mind Shatter
        }
        // Mindgames: a copy of a random minion of the opponent's deck; Shadow of Nothing without one.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetDeck(*m, 1, {"CS2_029", "CS2_182", "CS2_029"});
            CHECK(Cast(*m, 0, "EX1_345") == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_182"} && TA::DeckCardIds(*m, 1).size() == 3);
            std::unique_ptr<Match> n = Fresh("PRIEST");
            TA::SetDeck(*n, 1, {"CS2_029", "CS2_029"});
            CHECK(Cast(*n, 0, "EX1_345") == ActionError::None);
            CHECK(Board(*n, 0) == Strs{"EX1_345t"});
            MinionView s = MV(*n, TA::BoardIds(*n, 0)[0]);
            CHECK(s.attack == 0 && s.health == 1);
            // Mindgames needs a free board slot.
            std::unique_ptr<Match> k = Fresh("PRIEST");
            TA::SetBoard(*k, 0, {"CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(Cast(*k, 0, "EX1_345") == ActionError::BoardFull);
        }
        // Inner Fire: Attack becomes the current Health.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            Ids o = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetMinion(*m, o[0], 6, 3, 7);
            CHECK(Cast(*m, 0, "CS1_129", o[0]) == ActionError::None);
            CHECK(MV(*m, o[0]).attack == 3 && MV(*m, o[0]).health == 3);
        }
        // Holy Fire: 5 damage and restore 5 Health to your hero.
        {
            std::unique_ptr<Match> m = Fresh("PRIEST");
            TA::SetHero(*m, 0, 20, 0);
            CHECK(Cast(*m, 0, "EX1_624", Hero(*m, 1)) == ActionError::None);
            CHECK(HP(*m, 0) == 25 && HP(*m, 1) == 25);
        }
    }

    void TestWarlock()
    {
        // Power Overwhelming: +4/+4, destroyed at the end of the turn (its deathrattle still resolves).
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"EX1_556", "CS2_182"});
            CHECK(Cast(*m, 0, "EX1_316", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 6 && MV(*m, b[0]).health == 7);
            m->Apply(0, End());
            CHECK(Board(*m, 0) == (Strs{"skele21", "CS2_182"}));
        }
        // Sense Demons: 2 random Demons from the deck, Worthless Imps for the missing ones.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetDeck(*m, 0, {"CS2_168", "EX1_598", "CS2_168", "EX1_319", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_317") == ActionError::None);
            Strs h = Hand(*m, 0);
            std::sort(h.begin(), h.end());
            CHECK(h == (Strs{"EX1_319", "EX1_598"}) && TA::DeckCardIds(*m, 0).size() == 3);
            std::unique_ptr<Match> n = Fresh("WARLOCK");
            TA::SetDeck(*n, 0, {"CS2_168", "EX1_598", "CS2_168"});
            CHECK(Cast(*n, 0, "EX1_317") == ActionError::None);
            Strs g = Hand(*n, 0);
            std::sort(g.begin(), g.end());
            CHECK(g == (Strs{"EX1_317t", "EX1_598"}));
            std::unique_ptr<Match> k = Fresh("WARLOCK");
            TA::SetDeck(*k, 0, {"CS2_168", "CS2_168"});
            CHECK(Cast(*k, 0, "EX1_317") == ActionError::None);
            CHECK(Hand(*k, 0) == (Strs{"EX1_317t", "EX1_317t"}));
        }
        // Bane of Doom: 2 damage; if that kills it, a random Demon.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_182"});
            CHECK(Cast(*m, 0, "EX1_320", e[1]) == ActionError::None);
            CHECK(Board(*m, 0).empty() && MV(*m, e[1]).health == 3);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_320", e[0]) == ActionError::None);
            Strs const b = Board(*m, 0);
            CHECK(b.size() == 1 && Db().Find(b[0]) && Db().Find(b[0])->race == "DEMON");
            // Divine Shield absorbs it: no kill, no Demon.
            std::unique_ptr<Match> n = Fresh("WARLOCK");
            Ids s = TA::SetBoard(*n, 1, {"EX1_008"});
            CHECK(Cast(*n, 0, "EX1_320", s[0]) == ActionError::None);
            CHECK(Board(*n, 0).empty() && OnBoard(*n, 1, s[0]));
        }
        // Shadowflame: destroy a friendly minion, its Attack to all enemy minions.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_200", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_303", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_168"} && Board(*m, 1) == Strs{"CS2_200"} && MV(*m, e[0]).health == 3);
            CHECK(Cast(*m, 0, "EX1_303", Hero(*m, 1)) == ActionError::Target);
        }
        // Siphon Soul: destroy a minion, restore 3 Health to your hero.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetHero(*m, 0, 20, 0);
            CHECK(Cast(*m, 0, "EX1_309", e[0]) == ActionError::None);
            CHECK(Board(*m, 1).empty() && HP(*m, 0) == 23);
        }
        // Felguard: destroy one of your Mana Crystals (an empty one first); Taunt.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetMana(*m, 0, 6, 7);
            CHECK(Cast(*m, 0, "EX1_301") == ActionError::None);
            CHECK(HV(*m, 0).maxMana == 6 && HV(*m, 0).mana == 3);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).taunt);
            TA::SetMana(*m, 0, 3, 3);
            CHECK(Cast(*m, 0, "EX1_301") == ActionError::None);
            CHECK(HV(*m, 0).maxMana == 2 && HV(*m, 0).mana == 0);
        }
        // Doomguard (Charge, discards two random cards), Pit Lord, Flame Imp.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            Ids h = TA::SetHand(*m, 0, {"EX1_310", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 1);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).canAttack);
            CHECK(Cast(*m, 0, "EX1_313") == ActionError::None);
            CHECK_EQ(HP(*m, 0), 25);
            CHECK(Cast(*m, 0, "EX1_319") == ActionError::None);
            CHECK_EQ(HP(*m, 0), 22);
        }
        // Twisting Nether and Deadly Shot.
        {
            std::unique_ptr<Match> m = Fresh("WARLOCK");
            TA::SetBoard(*m, 0, {"CS2_182"});
            TA::SetBoard(*m, 1, {"CS2_200", "CS2_182"});
            CHECK(Cast(*m, 0, "EX1_312") == ActionError::None);
            CHECK(Board(*m, 0).empty() && Board(*m, 1).empty());
            std::unique_ptr<Match> n = Fresh("HUNTER");
            Ids h = TA::SetHand(*n, 0, {"EX1_617"});
            CHECK(n->Apply(0, Play(h[0])) == ActionError::Requirement);
            Ids e = TA::SetBoard(*n, 1, {"CS2_200", "CS2_182"});
            CHECK(n->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(Board(*n, 1).size() == 1);
            (void)e;
        }
    }

    void TestWarrior()
    {
        // Slam: 2 damage; draw if the minion survives.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_391", e[0]) == ActionError::None);
            CHECK(Hand(*m, 0).size() == 1 && MV(*m, e[0]).health == 3);
            TA::SetHand(*m, 0, {});
            CHECK(Cast(*m, 0, "EX1_391", e[1]) == ActionError::None);
            CHECK(Hand(*m, 0).empty() && !OnBoard(*m, 1, e[1]));
        }
        // Battle Rage: a card for each damaged friendly character.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_182", "CS2_182"});
            TA::SetMinion(*m, b[0], 4, 3, 5);
            TA::SetMinion(*m, b[1], 4, 4, 5);
            TA::SetHero(*m, 0, 29, 0);
            CHECK(Cast(*m, 0, "EX1_392") == ActionError::None);
            CHECK_EQ(Hand(*m, 0).size(), 3);
            std::unique_ptr<Match> n = Fresh("WARRIOR");
            CHECK(Cast(*n, 0, "EX1_392") == ActionError::None);
            CHECK(Hand(*n, 0).empty());
        }
        // Mortal Strike: 4 damage; 6 with 12 or less Health (Spell Damage counts).
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            CHECK(Cast(*m, 0, "EX1_408", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 26);
            TA::SetHero(*m, 0, 12, 0);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_408", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 20);
            TA::SetBoard(*m, 0, {"EX1_012"});
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_408", Hero(*m, 1)) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 13);
        }
        // Shield Slam: 1 damage to a minion for each Armor.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetHero(*m, 0, 30, 5);
            CHECK(Cast(*m, 0, "EX1_410", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 2 && HV(*m, 0).armor == 5);
        }
        // Cruel Taskmaster: 1 damage to a minion and +2 Attack to it.
        {
            std::unique_ptr<Match> m = Fresh("WARRIOR");
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_603", e[0]) == ActionError::None);
            CHECK(MV(*m, e[0]).health == 4 && MV(*m, e[0]).attack == 6);
        }
    }

    void TestNeutralBattlecries()
    {
        // Abusive Sergeant / Dark Iron Dwarf: +2 Attack this turn.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168"});
            CHECK(Cast(*m, 0, "CS2_188", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 4);
            CHECK(Cast(*m, 0, "EX1_046", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 6);
            m->Apply(0, End());
            m->Apply(1, End());
            CHECK(MV(*m, b[0]).attack == 2);
        }
        // Argent Protector gives Divine Shield; the Divine Shield minions are keyword-only.
        {
            std::unique_ptr<Match> m = Fresh("PALADIN");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "EX1_008", "EX1_020", "EX1_067"});
            CHECK(!MV(*m, b[0]).divineShield && MV(*m, b[1]).divineShield && MV(*m, b[2]).divineShield);
            CHECK(MV(*m, b[3]).divineShield && MV(*m, b[3]).charge);
            CHECK(Cast(*m, 0, "EX1_362", b[0]) == ActionError::None);
            CHECK(MV(*m, b[0]).divineShield);
        }
        // Healing battlecries: Earthen Ring Farseer, Priestess of Elune; Injured Blademaster hurts itself.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetHero(*m, 0, 20, 0);
            CHECK(Cast(*m, 0, "CS2_117", Hero(*m, 0)) == ActionError::None);
            CHECK_EQ(HP(*m, 0), 23);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "EX1_583") == ActionError::None);
            CHECK_EQ(HP(*m, 0), 27);
            TA::SetMana(*m, 0, 10, 10);
            CHECK(Cast(*m, 0, "CS2_181") == ActionError::None);
            MinionView bm = MV(*m, TA::BoardIds(*m, 0).back());
            CHECK(bm.attack == 4 && bm.health == 3 && bm.maxHealth == 7);
        }
        // Mind Control Tech: takes a minion only when the opponent has 4 or more.
        {
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 1, {"CS2_168", "CS2_168", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_085") == ActionError::None);
            CHECK(Board(*m, 0).size() == 1 && Board(*m, 1).size() == 3);
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 1, {"CS2_168", "CS2_168", "CS2_168", "CS2_168"});
            CHECK(Cast(*n, 0, "EX1_085") == ActionError::None);
            CHECK(Board(*n, 0).size() == 2 && Board(*n, 1).size() == 3);
        }
        // King Mukla: the opponent gets 2 Bananas (+1/+1).
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_014") == ActionError::None);
            CHECK(Hand(*m, 1) == (Strs{"EX1_014t", "EX1_014t"}));
            Ids r = TA::SetBoard(*m, 1, {"CS2_168"});
            ToSide1(*m);
            CHECK(Cast(*m, 1, "EX1_014t", r[0]) == ActionError::None);
            CHECK(MV(*m, r[0]).attack == 3 && MV(*m, r[0]).health == 2);
        }
        // Defender of Argus: adjacent minions +1/+1 and Taunt.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_168", "CS2_168"});
            Ids h = TA::SetHand(*m, 0, {"EX1_093"});
            CHECK(m->Apply(0, Play(h[0], 0, 0, 1)) == ActionError::None);
            CHECK(MV(*m, b[0]).attack == 3 && MV(*m, b[0]).taunt && MV(*m, b[1]).health == 2 && MV(*m, b[1]).taunt);
            CHECK(MV(*m, b[2]).attack == 2 && !MV(*m, b[2]).taunt);
        }
        // Leeroy Jenkins: Charge; two Whelps for the opponent. Arcane Golem: a full crystal for the opponent.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_116") == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_116"} && Board(*m, 1) == (Strs{"EX1_116t", "EX1_116t"}));
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).canAttack);
            TA::SetMana(*m, 1, 3, 5);
            CHECK(Cast(*m, 0, "EX1_089") == ActionError::None);
            CHECK(HV(*m, 1).maxMana == 6 && HV(*m, 1).mana == 4);
            TA::SetMana(*m, 1, 10, 10);
            CHECK(Cast(*m, 0, "EX1_089") == ActionError::None);
            CHECK(HV(*m, 1).maxMana == 10 && HV(*m, 1).mana == 10 && Hand(*m, 1).empty());
        }
        // Mad Bomber: 3 damage randomly split between all OTHER characters.
        {
            std::set<int> heroHits;
            for (uint32_t seed = 1; seed <= 30; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                Ids a = TA::SetBoard(*m, 0, {"CS2_200"});
                Ids b = TA::SetBoard(*m, 1, {"CS2_200"});
                CHECK(Cast(*m, 0, "EX1_082") == ActionError::None);
                int const total = (30 - HP(*m, 0)) + (30 - HP(*m, 1)) + (7 - MV(*m, a[0]).health) + (7 - MV(*m, b[0]).health);
                CHECK_EQ(total, 3);
                heroHits.insert(30 - HP(*m, 1));
                CHECK(MV(*m, TA::BoardIds(*m, 0).back()).health == 2); // the bomber itself is never hit
            }
            CHECK(heroHits.size() >= 3);
        }
        // Youthful Brewmaster: return a friendly minion to your hand.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"CS2_182"});
            CHECK(Cast(*m, 0, "EX1_049", b[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"EX1_049"} && Hand(*m, 0) == Strs{"CS2_182"});
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 1, {"CS2_182"});
            Ids h = TA::SetHand(*n, 0, {"EX1_049"});
            CHECK(n->Apply(0, Play(h[0], TA::BoardIds(*n, 1)[0])) == ActionError::Target); // friendly only
            CHECK(n->Apply(0, Play(h[0])) == ActionError::None);
        }
        // The Faceless Manipulator: becomes a copy of the chosen minion.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetMinion(*m, e[0], 6, 3, 7);
            CHECK(Cast(*m, 0, "EX1_564", e[0]) == ActionError::None);
            CHECK(Board(*m, 0) == Strs{"CS2_200"});
            MinionView f = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(f.attack == 6 && f.maxHealth == 7 && f.health == 3);
            std::unique_ptr<Match> n = Fresh();
            CHECK(Cast(*n, 0, "EX1_564") == ActionError::None);
            CHECK(Board(*n, 0) == Strs{"EX1_564"});
        }
        // Big Game Hunter: destroys a minion with 7 or more Attack.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_200", "EX1_250"});
            Ids h = TA::SetHand(*m, 0, {"EX1_005"});
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::Target); // 6 Attack
            CHECK(m->Apply(0, Play(h[0], 0)) == ActionError::Target);    // a valid target exists
            CHECK(m->Apply(0, Play(h[0], e[1])) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"CS2_200"});
            std::unique_ptr<Match> n = Fresh();
            TA::SetBoard(*n, 1, {"CS2_200"});
            CHECK(Cast(*n, 0, "EX1_005") == ActionError::None);
        }
        // The Black Knight: destroys an enemy minion with Taunt.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids e = TA::SetBoard(*m, 1, {"CS2_179", "CS2_182"});
            Ids h = TA::SetHand(*m, 0, {"EX1_002"});
            CHECK(m->Apply(0, Play(h[0], e[1])) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
            CHECK(Board(*m, 1) == Strs{"CS2_182"});
        }
        // Keyword-only minions of the list.
        {
            std::unique_ptr<Match> m = Fresh();
            Ids b = TA::SetBoard(*m, 0, {"EX1_045", "CS1_069", "EX1_563", "EX1_284", "EX1_243", "EX1_250", "CS2_168"});
            CHECK(MV(*m, b[0]).cantAttack && !MV(*m, b[0]).canAttack);
            CHECK(MV(*m, b[1]).taunt);
            CHECK(MV(*m, b[2]).spellDamage == 5 && MV(*m, b[3]).spellDamage == 1);
            CHECK(MV(*m, b[4]).windfury && MV(*m, b[5]).taunt);
            CHECK(Cast(*m, 0, "EX1_284") == ActionError::BoardFull);
            CHECK(MV(*m, b[2]).health == 12);
        }
        // Azure Drake: Spell Damage +1 and draws a card.
        {
            std::unique_ptr<Match> m = Fresh();
            CHECK(Cast(*m, 0, "EX1_284") == ActionError::None);
            CHECK(MV(*m, TA::BoardIds(*m, 0)[0]).spellDamage == 1 && Hand(*m, 0).size() == 1);
        }
    }

    // ---- randomized robustness ---------------------------------------------------------------------------

    // The 43 non-collectible dataset cards that carry a rarity (tokens): never put in a random deck.
    std::set<std::string> const& TokenIds()
    {
        static std::set<std::string> const ids = {"CS2_050", "CS2_051", "CS2_052", "CS2_082", "CS2_101t", "CS2_152",
            "CS2_boar", "CS2_mirror", "CS2_tk1", "EX1_025t", "EX1_110t", "EX1_130a", "EX1_160t", "EX1_165a", "EX1_165b",
            "EX1_165t1", "EX1_165t2", "EX1_317t", "EX1_345t", "EX1_383t", "EX1_506a", "EX1_534t", "EX1_554t", "EX1_598",
            "EX1_finkle", "EX1_tk11", "EX1_tk28", "EX1_tk29", "EX1_tk34", "EX1_tk9", "GAME_002", "Mekka1", "Mekka2",
            "Mekka3", "Mekka4", "NEW1_009", "NEW1_032", "NEW1_033", "NEW1_034", "hexfrog", "skele11", "skele21", "tt_010a"};
        return ids;
    }

    // The supported collectible cards a deck of heroClass may hold (its own class and neutral), CORE and EXPERT1.
    std::vector<std::string> DeckPool(std::string const& heroClass)
    {
        std::vector<std::string> pool;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            std::string const type = r.type;
            if (type != "MINION" && type != "SPELL" && type != "WEAPON")
                continue;
            if (std::string(r.rarity).empty() || TokenIds().count(r.id))
                continue;
            if (std::string(r.cls) != heroClass && std::string(r.cls) != "NEUTRAL")
                continue;
            if (!Db().Supported(r.id))
                continue;
            pool.push_back(r.id);
        }
        return pool;
    }

    // 30 cards: at most 2 copies, at most 1 legendary.
    std::vector<std::string> RandomDeck(std::vector<std::string> const& pool, std::mt19937& rng)
    {
        std::vector<std::string> deck;
        std::map<std::string, int> copies;
        int guard = 0;
        while (deck.size() < 30 && guard++ < 100000)
        {
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

    struct RobustStats
    {
        int games = 0;
        int ended = 0;
        int actions = 0;
        int failures = 0;
        int capped = 0;
        int picks = 0;
        int secretsTriggered = 0;
        int minionsPlayed = 0;
        std::map<std::string, int> played; // card id -> times played
    };

    void CheckInvariants(Match const& m, RobustStats& st, uint32_t seed, int step)
    {
        for (int s = 0; s < 2; ++s)
        {
            MatchView const v = m.ViewFor(s, m.LastEventSeq());
            bool const ok = v.self.board.size() <= static_cast<size_t>(kMaxBoard) && v.self.hand.size() <= static_cast<size_t>(kMaxHand) &&
                v.self.hero.maxMana <= kMaxMana && v.self.hero.mana <= kMaxMana && v.self.hero.health <= v.self.hero.maxHealth &&
                v.self.secrets.size() <= static_cast<size_t>(Detail::kMaxSecrets);
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

    void RandomGame(uint32_t seed, std::string const& c0, std::string const& c1, RobustStats& st)
    {
        std::mt19937 rng(seed * 2654435761u + 12345u);
        MatchSetup setup;
        setup.seed = seed;
        setup.firstSide = -1;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = c0;
        setup.sides[0].deck = RandomDeck(DeckPool(c0), rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = c1;
        setup.sides[1].deck = RandomDeck(DeckPool(c1), rng);
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
        int step = 0;
        for (; step < 300 && m->GetPhase() == Phase::Play; ++step)
        {
            bool picked = false;
            for (int s = 0; s < 2 && !picked; ++s)
            {
                MatchView const v = m->ViewFor(s, m->LastEventSeq());
                if (v.pickPending && !v.pick.empty())
                {
                    Action a = Choose(v.pick[rng() % v.pick.size()].id);
                    ActionError const e = m->Apply(s, a);
                    if (e != ActionError::None)
                    {
                        ++st.failures;
                        std::printf("  pick refused (%s): seed %u step %d\n", ActionErrorCode(e), seed, step);
                    }
                    ++st.picks;
                    picked = true;
                }
            }
            if (picked)
            {
                ++st.actions;
                continue;
            }
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
                            else if (def)
                                ++st.minionsPlayed;
                            ++st.played[h.cardId];
                        }
                }
            }
            uint32_t const before = m->LastEventSeq();
            ActionError const e = m->Apply(side, a);
            if (e != ActionError::None)
            {
                ++st.failures;
                std::printf("  action refused (%s): seed %u step %d type %d source %u target %u choice %d pos %d\n",
                    ActionErrorCode(e), seed, step, static_cast<int>(a.type), a.source, a.target, a.choice, a.position);
                break;
            }
            ++st.actions;
            for (Event const& ev : m->ViewFor(0, before).events)
                if (ev.type == EventType::Reveal)
                    ++st.secretsTriggered;
            CheckInvariants(*m, st, seed, step);
            if (m->GetPhase() == Phase::Over)
                break;
        }
        if (m->GetPhase() == Phase::Over)
            ++st.ended;
    }

    void TestRandomGames()
    {
        char const* const classes[] = {"WARRIOR", "SHAMAN", "ROGUE", "PALADIN", "HUNTER", "DRUID", "WARLOCK", "MAGE", "PRIEST"};
        for (char const* c : classes)
            CHECK(DeckPool(c).size() >= 20);
        RobustStats st;
        for (int ci = 0; ci < 9; ++ci)
            for (uint32_t k = 0; k < static_cast<uint32_t>(g_gamesPerClass); ++k)
            {
                uint32_t const seed = 1000 + static_cast<uint32_t>(ci) * 100 + k;
                RandomGame(seed, classes[ci], classes[(ci + 1 + static_cast<int>(k % 8)) % 9], st);
            }
        std::printf("  random games: %d games, %d ended, %d actions, %d picks, %d secrets revealed, %d minions played, %zu distinct cards\n",
            st.games, st.ended, st.actions, st.picks, st.secretsTriggered, st.minionsPlayed, st.played.size());
        CHECK_EQ(st.failures, 0);
        CHECK_EQ(st.capped, 0);
        CHECK(st.ended * 3 >= st.games); // most games finish within the action budget
        CHECK(st.secretsTriggered > 0);
        // Every card of the representative list shows up somewhere in the random games' pool.
        int missing = 0;
        for (char const* id : kList)
            if (!st.played.count(id))
                ++missing;
        std::printf("  list cards not drawn into a played hand in the random games: %d of %zu\n", missing, sizeof(kList) / sizeof(kList[0]));
    }

    // The same seed and the same random choices give the same match (determinism with the new cards).
    void TestRandomDeterminism()
    {
        std::string states[2];
        for (int run = 0; run < 2; ++run)
        {
            RobustStats st;
            std::mt19937 rng(7);
            MatchSetup setup;
            setup.seed = 4242;
            setup.firstSide = 0;
            setup.sides[0].name = "A";
            setup.sides[0].heroClass = "SHAMAN";
            setup.sides[0].deck = RandomDeck(DeckPool("SHAMAN"), rng);
            setup.sides[1].name = "B";
            setup.sides[1].heroClass = "PRIEST";
            setup.sides[1].deck = RandomDeck(DeckPool("PRIEST"), rng);
            std::string error;
            std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
            CHECK(m != nullptr);
            if (!m)
                return;
            Action mull;
            mull.type = ActionType::Mulligan;
            m->Apply(0, mull);
            m->Apply(1, mull);
            for (int step = 0; step < 120 && m->GetPhase() == Phase::Play; ++step)
            {
                int const side = m->ActiveSide();
                std::vector<Action> legal = m->LegalActions(side);
                MatchView const v = m->ViewFor(side, m->LastEventSeq());
                if (v.pickPending && !v.pick.empty())
                {
                    m->Apply(side, Choose(v.pick[rng() % v.pick.size()].id));
                    continue;
                }
                size_t const pick = rng() % (legal.size() + 1);
                m->Apply(side, pick == legal.size() ? End() : legal[pick]);
            }
            states[run] = m->DebugState();
        }
        CHECK(states[0] == states[1]);
    }

    // The redacted copy of a match full of Classic cards plays on without surprises (what the AI relies on).
    void TestRedactedCopyClassic()
    {
        std::mt19937 rng(99);
        MatchSetup setup;
        setup.seed = 31;
        setup.firstSide = 0;
        setup.sides[0].name = "A";
        setup.sides[0].heroClass = "ROGUE";
        setup.sides[0].deck = RandomDeck(DeckPool("ROGUE"), rng);
        setup.sides[1].name = "B";
        setup.sides[1].heroClass = "MAGE";
        setup.sides[1].deck = RandomDeck(DeckPool("MAGE"), rng);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
        CHECK(m != nullptr);
        if (!m)
            return;
        Action mull;
        mull.type = ActionType::Mulligan;
        m->Apply(0, mull);
        m->Apply(1, mull);
        TA::SetSecrets(*m, 1, {"EX1_289", "EX1_594"});
        for (int round = 0; round < 6 && m->GetPhase() == Phase::Play; ++round)
        {
            int const side = m->ActiveSide();
            std::unique_ptr<Match> r = m->RedactedCopy(side, 1000 + static_cast<uint32_t>(round));
            CHECK(r != nullptr);
            if (!r)
                return;
            // Random play on the copy never produces a refused action, whatever the unknown cards are.
            for (int step = 0; step < 40 && r->GetPhase() == Phase::Play; ++step)
            {
                int const s = r->ActiveSide();
                MatchView const v = r->ViewFor(s, r->LastEventSeq());
                if (v.pickPending && !v.pick.empty())
                {
                    CHECK(r->Apply(s, Choose(v.pick[rng() % v.pick.size()].id)) == ActionError::None);
                    continue;
                }
                std::vector<Action> legal = r->LegalActions(s);
                size_t const pick = rng() % (legal.size() + 1);
                CHECK(r->Apply(s, pick == legal.size() ? End() : legal[pick]) == ActionError::None);
            }
            m->Apply(side, End());
        }
    }

    // ---- the seed scripts of the engine core, kept and re-checked ------------------------------------------

    void TestSeedCards()
    {
        // Abomination: Taunt; Deathrattle: 2 damage to ALL characters (a chain through a second Abomination).
        {
            std::unique_ptr<Match> m = Fresh();
            Ids a = TA::SetBoard(*m, 0, {"EX1_097"});
            Ids e = TA::SetBoard(*m, 1, {"CS2_182"});
            CHECK(MV(*m, a[0]).taunt);
            CHECK(Cast(*m, 0, "CS2_029", a[0]) == ActionError::None);
            CHECK(HP(*m, 0) == 28 && HP(*m, 1) == 28 && MV(*m, e[0]).health == 3);
        }
        // Conceal: Stealth until your next turn.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids b = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
            CHECK(Cast(*m, 0, "EX1_128") == ActionError::None);
            CHECK(MV(*m, b[0]).stealth && MV(*m, b[1]).stealth);
            ToSide1(*m);
            CHECK(Cast(*m, 1, "CS2_029", b[0]) == ActionError::Target);
            ToSide0(*m);
            CHECK(!MV(*m, b[0]).stealth && !MV(*m, b[1]).stealth);
        }
        // Eviscerate: 2 damage; Combo 4 (Spell Damage counts).
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"EX1_124", "EX1_124"});
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            CHECK(m->Apply(0, Play(h[1], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 24);
            std::unique_ptr<Match> n = Fresh("ROGUE");
            TA::SetBoard(*n, 0, {"EX1_012"});
            CHECK(Cast(*n, 0, "EX1_124", Hero(*n, 1)) == ActionError::None);
            CHECK_EQ(HP(*n, 1), 27);
        }
        // SI:7 Agent: Combo: 2 damage; a minion without Combo is played with no target.
        {
            std::unique_ptr<Match> m = Fresh("ROGUE");
            Ids h = TA::SetHand(*m, 0, {"EX1_134", "EX1_134"});
            CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::Target);
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            CHECK(m->Apply(0, Play(h[1], Hero(*m, 1))) == ActionError::None);
            CHECK_EQ(HP(*m, 1), 28);
            std::unique_ptr<Match> n = Fresh("ROGUE");
            Ids k = TA::SetHand(*n, 0, {"GAME_005", "EX1_134"});
            n->Apply(0, Play(k[0]));
            CHECK(n->Apply(0, Play(k[1])) == ActionError::Target); // a Combo target is required while one exists
            CHECK(n->Apply(0, Play(k[1], Hero(*n, 0))) == ActionError::None); // own hero is a legal target
            CHECK_EQ(HP(*n, 0), 28);
        }
    }

    // @@MORE-TESTS@@
}

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--games")
            g_gamesPerClass = std::atoi(argv[i + 1]);
    RUN(TestCoverage);
    RUN(TestDeathrattles);
    RUN(TestSecretsBasics);
    RUN(TestSecretInteractions);
    RUN(TestEnrage);
    RUN(TestAurasAndSilence);
    RUN(TestStealthAndPoison);
    RUN(TestFreeze);
    RUN(TestCombo);
    RUN(TestOverloadClassic);
    RUN(TestChooseOneClassic);
    RUN(TestWeapons);
    RUN(TestCostAuras);
    RUN(TestTriggeredMinions);
    RUN(TestDreamCards);
    RUN(TestPriest);
    RUN(TestWarlock);
    RUN(TestWarrior);
    RUN(TestNeutralBattlecries);
    RUN(TestRandomGames);
    RUN(TestRandomDeterminism);
    RUN(TestRedactedCopyClassic);
    RUN(TestSeedCards);
    // @@MORE-RUNS@@
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
