// Standalone tests of the Basic (CORE) card scripts (src/match/HsMatchCardsBasic.cpp): coverage of the
// whole set, the targeting rules against the dataset's play requirements, one focused scenario per
// scripted card (effect, spell damage, targeting), cross-card interactions and a randomized robustness
// run over random legal actions with random CORE decks. Uses every card of the dataset
// (tests/match_all_cards.h, real 5834 stats) and the GM fixture decks of tests/match_test_cards.h.
//   match_cards_basic_tests.exe   run all tests; exit code 0 = all checks passed.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchInternal.h"
#include "match_all_cards.h"
#include "match_test_cards.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace Hs::Match;

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
        int const before = g_failed;                                                                  \
        fn();                                                                                         \
        std::printf("%-44s %s\n", #fn, g_failed == before ? "ok" : "FAILED");                         \
    } while (0)

namespace
{
    using TA = TestAccess;
    using Strs = std::vector<std::string>;

    // The 133 collectible standard CORE cards of data/cards/cards.normalized.json.
    char const* const kCoreIds[] = {
        "CS1_042", "CS1_112", "CS1_113", "CS1_130", "CS2_003", "CS2_004", "CS2_005", "CS2_007", "CS2_008", "CS2_009",
        "CS2_011", "CS2_012", "CS2_013", "CS2_022", "CS2_023", "CS2_024", "CS2_025", "CS2_026", "CS2_027", "CS2_029",
        "CS2_032", "CS2_033", "CS2_037", "CS2_039", "CS2_041", "CS2_042", "CS2_045", "CS2_046", "CS2_057", "CS2_061",
        "CS2_062", "CS2_063", "CS2_064", "CS2_065", "CS2_072", "CS2_074", "CS2_075", "CS2_076", "CS2_077", "CS2_080",
        "CS2_084", "CS2_087", "CS2_088", "CS2_089", "CS2_091", "CS2_092", "CS2_093", "CS2_094", "CS2_097", "CS2_103",
        "CS2_105", "CS2_106", "CS2_108", "CS2_112", "CS2_114", "CS2_118", "CS2_119", "CS2_120", "CS2_121", "CS2_122",
        "CS2_124", "CS2_125", "CS2_127", "CS2_131", "CS2_141", "CS2_142", "CS2_147", "CS2_150", "CS2_155", "CS2_162",
        "CS2_168", "CS2_171", "CS2_172", "CS2_173", "CS2_179", "CS2_182", "CS2_186", "CS2_187", "CS2_189", "CS2_196",
        "CS2_197", "CS2_200", "CS2_201", "CS2_213", "CS2_222", "CS2_226", "CS2_232", "CS2_234", "CS2_235", "CS2_236",
        "CS2_237", "DS1_055", "DS1_070", "DS1_175", "DS1_178", "DS1_183", "DS1_184", "DS1_185", "DS1_233", "EX1_011",
        "EX1_015", "EX1_019", "EX1_025", "EX1_066", "EX1_084", "EX1_129", "EX1_169", "EX1_173", "EX1_244", "EX1_246",
        "EX1_277", "EX1_278", "EX1_302", "EX1_306", "EX1_308", "EX1_360", "EX1_371", "EX1_399", "EX1_400", "EX1_506",
        "EX1_508", "EX1_539", "EX1_565", "EX1_581", "EX1_582", "EX1_587", "EX1_593", "EX1_606", "EX1_622",
        "NEW1_003", "NEW1_004", "NEW1_011", "NEW1_031",
    };

    // Tokens and generated cards the CORE cards create (all must be Supported too).
    char const* const kTokenIds[] = {"CS2_mirror", "CS2_boar", "EX1_506a", "EX1_025t", "hexfrog", "NEW1_032", "NEW1_033",
        "NEW1_034", "CS2_tk1", "CS2_013t", "CS2_050", "CS2_051", "CS2_052", "NEW1_009", "CS2_101t", "CS2_082", "GAME_005"};

    // Play requirements of the dataset (play_requirements) that concern targets.
    enum ReqBits : unsigned
    {
        kRTarget = 1,       // REQ_TARGET_TO_PLAY
        kRMinion = 2,       // REQ_MINION_TARGET
        kREnemy = 4,        // REQ_ENEMY_TARGET
        kRFriendly = 8,     // REQ_FRIENDLY_TARGET
        kRDamaged = 16,     // REQ_DAMAGED_TARGET
        kRUndamaged = 32,   // REQ_UNDAMAGED_TARGET
        kRMaxAtt = 64,      // REQ_TARGET_MAX_ATTACK (value)
        kRMinAtt = 128,     // REQ_TARGET_MIN_ATTACK (value)
        kRIfAvail = 256,    // REQ_TARGET_IF_AVAILABLE
        kRNonSelf = 512,    // REQ_NONSELF_TARGET
        kRRace = 1024       // REQ_TARGET_WITH_RACE (value: 15 Demon, 20 Beast)
    };

    struct ReqRow
    {
        char const* id;
        unsigned bits;
        int value;
    };

    ReqRow const kReqs[] = {
        {"CS1_113", kREnemy | kRMinion | kRTarget, 0},
        {"CS1_130", kRTarget, 0},
        {"CS2_004", kRMinion | kRTarget, 0},
        {"CS2_007", kRTarget, 0},
        {"CS2_008", kRTarget, 0},
        {"CS2_009", kRMinion | kRTarget, 0},
        {"CS2_012", kREnemy | kRTarget, 0},
        {"CS2_022", kRMinion | kRTarget, 0},
        {"CS2_024", kRTarget, 0},
        {"CS2_029", kRTarget, 0},
        {"CS2_037", kREnemy | kRTarget, 0},
        {"CS2_039", kRMinion | kRTarget, 0},
        {"CS2_041", kRMinion | kRTarget, 0},
        {"CS2_042", kRIfAvail, 0},
        {"CS2_045", kRFriendly | kRTarget, 0},
        {"CS2_057", kRMinion | kRTarget, 0},
        {"CS2_061", kRTarget, 0},
        {"CS2_063", kREnemy | kRMinion | kRTarget, 0},
        {"CS2_072", kRMinion | kRTarget | kRUndamaged, 0},
        {"CS2_076", kREnemy | kRMinion | kRTarget, 0},
        {"CS2_084", kRMinion | kRTarget, 0},
        {"CS2_087", kRMinion | kRTarget, 0},
        {"CS2_089", kRTarget, 0},
        {"CS2_092", kRMinion | kRTarget, 0},
        {"CS2_094", kRTarget, 0},
        {"CS2_103", kRFriendly | kRMinion | kRTarget, 0},
        {"CS2_108", kRDamaged | kREnemy | kRMinion | kRTarget, 0},
        {"CS2_141", kRNonSelf | kRIfAvail, 0},
        {"CS2_150", kRNonSelf | kRIfAvail, 0},
        {"CS2_189", kRNonSelf | kRIfAvail, 0},
        {"CS2_234", kRMinion | kRMaxAtt | kRTarget, 3},
        {"CS2_236", kRMinion | kRTarget, 0},
        {"DS1_070", kRFriendly | kRIfAvail | kRRace, 20},
        {"DS1_185", kRTarget, 0},
        {"EX1_011", kRIfAvail, 0},
        {"EX1_019", kRFriendly | kRMinion | kRIfAvail, 0},
        {"EX1_173", kRTarget, 0},
        {"EX1_246", kRMinion | kRTarget, 0},
        {"EX1_278", kRTarget, 0},
        {"EX1_302", kRMinion | kRTarget, 0},
        {"EX1_308", kRTarget, 0},
        {"EX1_360", kRMinion | kRTarget, 0},
        {"EX1_371", kRMinion | kRTarget, 0},
        {"EX1_539", kRTarget, 0},
        {"EX1_581", kREnemy | kRMinion | kRTarget, 0},
        {"EX1_587", kRFriendly | kRMinion | kRIfAvail, 0},
        {"EX1_622", kRMinion | kRMinAtt | kRTarget, 5},
        {"NEW1_003", kRTarget | kRRace, 15},
    };

    CardDb const& Db() { return MatchAllCards::Db(); }

    // A match in its play phase, side 0 active on turn 1 with 10 mana; both hands empty, both decks
    // 20 Murloc Raiders, boards empty.
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

    // Side 0 plays with a full mana tray (a test plays several expensive cards in a row).
    ActionError Do0(Match& m, Action const& a)
    {
        TA::SetMana(m, 0, 10, 10);
        return m.Apply(0, a);
    }

    // Side 0's hand becomes exactly {card}; plays it (mana is refilled to 10 first).
    ActionError Cast(Match& m, char const* card, EntityId target = 0, int position = -1)
    {
        TA::SetMana(m, 0, 10, 10);
        std::vector<EntityId> h = TA::SetHand(m, 0, {card});
        return m.Apply(0, Play(h[0], target, 0, position));
    }

    ActionError CastHand(Match& m, Strs const& hand, char const* card, EntityId target = 0, int position = -1)
    {
        TA::SetMana(m, 0, 10, 10);
        std::vector<EntityId> h = TA::SetHand(m, 0, hand);
        for (std::size_t i = 0; i < hand.size(); ++i)
            if (hand[i] == card)
                return m.Apply(0, Play(h[i], target, 0, position));
        return ActionError::Source;
    }

    MinionView const* FindMinion(SideView const& s, EntityId id)
    {
        for (MinionView const& mv : s.board)
            if (mv.id == id)
                return &mv;
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
        std::vector<EntityId> const b = TA::BoardIds(m, side);
        return std::find(b.begin(), b.end(), id) != b.end();
    }

    int CountEvents(Match const& m, EventType type, uint32_t since = 0)
    {
        int n = 0;
        for (Event const& e : m.ViewFor(0, since).events)
            if (e.type == type)
                ++n;
        return n;
    }

    int Hand(Match const& m, int side) { return static_cast<int>(TA::HandIds(m, side).size()); }
    int Hp(Match const& m, int side) { return m.HeroHealth(side); }
    int ArmorOf(Match const& m, int side) { return m.ViewFor(side, m.LastEventSeq()).self.hero.armor; }
    int ManaOf(Match const& m, int side) { return m.ViewFor(side, m.LastEventSeq()).self.hero.mana; }
    HeroView HeroOfSide0(Match const& m) { return m.ViewFor(0, m.LastEventSeq()).self.hero; }
    EntityId HeroId(Match const& m, int side) { return TA::HeroId(m, side); }

    int BoardCount(Match const& m, int side, char const* card)
    {
        int n = 0;
        for (std::string const& c : TA::BoardCardIds(m, side))
            n += c == card;
        return n;
    }

    // ---- coverage -----------------------------------------------------------

    void TestCoverage()
    {
        static_assert(sizeof(kCoreIds) / sizeof(kCoreIds[0]) == 133, "133 CORE cards");
        std::set<std::string> seen;
        for (char const* id : kCoreIds)
        {
            CHECK(Db().Find(id) != nullptr);
            CHECK(Db().Supported(id));
            if (!Db().Supported(id))
                std::printf("  unsupported CORE card %s\n", id);
            CHECK(seen.insert(id).second);
        }
        for (char const* id : kTokenIds)
        {
            CHECK(Db().Find(id) != nullptr);
            CHECK(Db().Supported(id));
            if (!Db().Supported(id))
                std::printf("  unsupported token %s\n", id);
        }
        // The GM fixture decks and the keyword cards of the engine tests.
        for (bool mage : {true, false})
            for (std::string const& id : MatchTestCards::Fixture(mage))
                CHECK(Db().Supported(id));
        for (char const* id : {"CS2_121", "CS2_131", "CS2_142", "CS2_065", "CS2_033", "CS2_041"})
            CHECK(Db().Supported(id));
        // Scripted cards: every one of them refers to cards the database has.
        int scripted = 0;
        for (char const* id : kCoreIds)
            if (Detail::FindScript(id))
                ++scripted;
        CHECK(scripted >= 90);
        // Cards of the other sets stay out unless scripted (an enchantment id stays unsupported).
        CHECK(!Db().Supported("EX1_161o")); // an enchantment: never a playable card
        CHECK(!Db().Supported("nope"));
    }

    // ---- targeting rules against the dataset's play requirements -------------

    struct Cand
    {
        char const* name;
        int side;       // owner of the candidate
        int index;      // board slot, -1 = hero
        bool minion() const { return index >= 0; }
        bool damaged;
        int attack;
        char const* race;
    };

    // Boards used by the probe: side 0 [Murloc Raider 2/1, damaged Yeti 4/3, Voidwalker 1/3, Raptor 3/2],
    // side 1 [Boulderfist 6/7, damaged Yeti 4/3, Voidwalker 1/3, Raptor 3/2, Core Hound 9/5].
    Strs const kProbeBoard0 = {"CS2_168", "CS2_182", "CS2_065", "CS2_172"};
    Strs const kProbeBoard1 = {"CS2_200", "CS2_182", "CS2_065", "CS2_172", "CS2_201"};

    ReqRow const* ReqOf(char const* id)
    {
        for (ReqRow const& r : kReqs)
            if (std::string(r.id) == id)
                return &r;
        return nullptr;
    }

    void TestTargetRulesMatchData()
    {
        std::vector<Cand> cands;
        cands.push_back({"own hero", 0, -1, false, 0, ""});
        cands.push_back({"enemy hero", 1, -1, false, 0, ""});
        int const attacks0[] = {2, 4, 1, 3};
        char const* const races0[] = {"MURLOC", "", "DEMON", "BEAST"};
        for (int i = 0; i < 4; ++i)
            cands.push_back({"own minion", 0, i, i == 1, attacks0[i], races0[i]});
        int const attacks1[] = {6, 4, 1, 3, 9};
        char const* const races1[] = {"", "", "DEMON", "BEAST", "BEAST"};
        for (int i = 0; i < 5; ++i)
            cands.push_back({"enemy minion", 1, i, i == 1, attacks1[i], races1[i]});

        int probes = 0;
        for (char const* id : kCoreIds)
        {
            CardDef const* def = Db().Find(id);
            if (!def || (def->type != CardType::Spell && def->type != CardType::Minion && def->type != CardType::Weapon))
                continue;
            ReqRow const* req = ReqOf(id);
            unsigned const bits = req ? req->bits : 0;
            int const value = req ? req->value : 0;
            bool const targeted = (bits & (kRTarget | kRIfAvail)) != 0;
            // target 0
            {
                std::unique_ptr<Match> m = Fresh();
                TA::SetBoard(*m, 0, kProbeBoard0);
                TA::SetBoard(*m, 1, kProbeBoard1);
                TA::SetWeapon(*m, 0, "CS2_082");
                ActionError const e = Cast(*m, id, 0);
                bool const wanted = !targeted;
                if ((e == ActionError::None) != wanted)
                    std::printf("  %s with no target: got %s\n", id, ActionErrorCode(e));
                CHECK((e == ActionError::None) == wanted);
                ++probes;
            }
            for (Cand const& c : cands)
            {
                std::unique_ptr<Match> m = Fresh();
                std::vector<EntityId> b0 = TA::SetBoard(*m, 0, kProbeBoard0);
                std::vector<EntityId> b1 = TA::SetBoard(*m, 1, kProbeBoard1);
                TA::SetMinion(*m, b0[1], 4, 3, 5);
                TA::SetMinion(*m, b1[1], 4, 3, 5);
                TA::SetWeapon(*m, 0, "CS2_082");
                EntityId target = c.minion() ? (c.side == 0 ? b0 : b1)[static_cast<std::size_t>(c.index)] : HeroId(*m, c.side);
                bool wanted = targeted;
                if (targeted)
                {
                    bool const enemy = c.side == 1;
                    if ((bits & kRMinion) && !c.minion())
                        wanted = false;
                    if ((bits & kREnemy) && !enemy)
                        wanted = false;
                    if ((bits & kRFriendly) && enemy)
                        wanted = false;
                    if ((bits & kRDamaged) && !c.damaged)
                        wanted = false;
                    if ((bits & kRUndamaged) && c.damaged)
                        wanted = false;
                    if ((bits & kRMaxAtt) && (!c.minion() || c.attack > value))
                        wanted = false;
                    if ((bits & kRMinAtt) && (!c.minion() || c.attack < value))
                        wanted = false;
                    if (bits & kRRace)
                    {
                        std::string const race = value == 15 ? "DEMON" : "BEAST";
                        if (!c.minion() || race != c.race)
                            wanted = false;
                    }
                }
                ActionError const e = Cast(*m, id, target);
                if ((e == ActionError::None) != wanted)
                    std::printf("  %s on %s (side %d slot %d): got %s, wanted %s\n", id, c.name, c.side, c.index,
                        ActionErrorCode(e), wanted ? "ok" : "refusal");
                CHECK((e == ActionError::None) == wanted);
                ++probes;
            }
        }
        CHECK(probes > 1000);
    }

    // ---- keyword-only cards ---------------------------------------------------

    void TestKeywordCards()
    {
        // Taunt: Goldshire Footman protects the hero.
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> mine = TA::SetBoard(*m, 0, {"CS2_200"});
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS1_042", "CS2_168"});
        CHECK(m->Apply(0, Attack(mine[0], HeroId(*m, 1))) == ActionError::Taunt);
        CHECK(m->Apply(0, Attack(mine[0], en[1])) == ActionError::Taunt);
        CHECK(m->Apply(0, Attack(mine[0], en[0])) == ActionError::None);

        // Charge: Stonetusk Boar and Wolfrider attack at once, Murloc Raider does not.
        std::unique_ptr<Match> n = Fresh();
        std::vector<EntityId> h = TA::SetHand(*n, 0, {"CS2_171", "CS2_124", "CS2_168"});
        for (EntityId id : h)
            CHECK(Do0(*n, Play(id)) == ActionError::None);
        std::vector<EntityId> b = TA::BoardIds(*n, 0);
        CHECK(MV(*n, b[0]).canAttack && MV(*n, b[1]).canAttack && !MV(*n, b[2]).canAttack);

        // Spell Damage: Kobold Geomancer (+1), Ogre Magi (+1) and Archmage (+1) stack.
        std::unique_ptr<Match> k = Fresh();
        TA::SetBoard(*k, 0, {"CS2_142", "CS2_197"});
        CHECK(Cast(*k, "CS2_029", HeroId(*k, 1)) == ActionError::None);
        CHECK(Hp(*k, 1) == 30 - 8);

        // Weapons without text.
        struct W { char const* id; int attack, durability; };
        for (W const& w : {W{"CS2_106", 3, 2}, W{"CS2_112", 5, 2}, W{"CS2_091", 1, 4}, W{"CS2_080", 3, 4}})
        {
            std::unique_ptr<Match> x = Fresh();
            CHECK(Cast(*x, w.id) == ActionError::None);
            HeroView const hv = HeroOfSide0(*x);
            CHECK(hv.weaponAttack == w.attack && hv.weaponDurability == w.durability && hv.attack == w.attack);
        }
    }

    // ---- Mage -------------------------------------------------------------------

    void TestMage()
    {
        // Arcane Missiles: 3 damage split among enemies (+1 missile with Spell Damage); only the hero when alone.
        std::unique_ptr<Match> m = Fresh();
        CHECK(Cast(*m, "EX1_277") == ActionError::None);
        CHECK(Hp(*m, 1) == 27);
        std::unique_ptr<Match> m2 = Fresh();
        TA::SetBoard(*m2, 0, {"CS2_142"});
        CHECK(Cast(*m2, "EX1_277") == ActionError::None);
        CHECK(Hp(*m2, 1) == 26);
        std::unique_ptr<Match> m3 = Fresh();
        std::vector<EntityId> e3 = TA::SetBoard(*m3, 1, {"CS2_200"});
        CHECK(Cast(*m3, "EX1_277") == ActionError::None);
        CHECK((30 - Hp(*m3, 1)) + (7 - MV(*m3, e3[0]).health) == 3);
        CHECK(Hp(*m3, 0) == 30);

        // Arcane Explosion: 1 to enemy minions only.
        std::unique_ptr<Match> a = Fresh();
        std::vector<EntityId> own = TA::SetBoard(*a, 0, {"CS2_182"});
        std::vector<EntityId> en = TA::SetBoard(*a, 1, {"CS2_182", "CS2_168"});
        CHECK(Cast(*a, "CS2_025") == ActionError::None);
        CHECK(MV(*a, en[0]).health == 4 && !OnBoard(*a, 1, en[1]) && MV(*a, own[0]).health == 5 && Hp(*a, 1) == 30);

        // Arcane Intellect: draw 2.
        std::unique_ptr<Match> i = Fresh();
        CHECK(Cast(*i, "CS2_023") == ActionError::None);
        CHECK(Hand(*i, 0) == 2);

        // Fireball 6 (7 with Spell Damage).
        std::unique_ptr<Match> f = Fresh();
        CHECK(Cast(*f, "CS2_029", HeroId(*f, 1)) == ActionError::None);
        CHECK(Hp(*f, 1) == 24);
        TA::SetBoard(*f, 0, {"EX1_582"});
        CHECK(Cast(*f, "CS2_029", HeroId(*f, 1)) == ActionError::None);
        CHECK(Hp(*f, 1) == 17);

        // Polymorph: a 1/1 Sheep, on either side.
        std::unique_ptr<Match> p = Fresh();
        std::vector<EntityId> pe = TA::SetBoard(*p, 1, {"CS2_200"});
        CHECK(Cast(*p, "CS2_022", pe[0]) == ActionError::None);
        CHECK(TA::BoardCardIds(*p, 1)[0] == "CS2_tk1" && MV(*p, pe[0]).attack == 1 && MV(*p, pe[0]).health == 1);
        CHECK(Cast(*p, "CS2_022", HeroId(*p, 1)) == ActionError::Target);

        // Frostbolt: 3 damage and Freeze, on a hero too.
        std::unique_ptr<Match> fb = Fresh();
        CHECK(Cast(*fb, "CS2_024", HeroId(*fb, 1)) == ActionError::None);
        CHECK(Hp(*fb, 1) == 27 && fb->ViewFor(0, 0).other.hero.frozen);

        // Water Elemental freezes what it damages (also when it is attacked).
        std::unique_ptr<Match> w = Fresh();
        std::vector<EntityId> we = TA::SetBoard(*w, 0, {"CS2_033"});
        std::vector<EntityId> boss = TA::SetBoard(*w, 1, {"CS2_200"});
        CHECK(w->Apply(0, Attack(we[0], boss[0])) == ActionError::None);
        CHECK(MV(*w, boss[0]).frozen);

        // Frost Nova: freezes enemy minions only.
        std::unique_ptr<Match> fn = Fresh();
        std::vector<EntityId> fo = TA::SetBoard(*fn, 0, {"CS2_168"});
        std::vector<EntityId> fe = TA::SetBoard(*fn, 1, {"CS2_182", "CS2_200"});
        CHECK(Cast(*fn, "CS2_026") == ActionError::None);
        CHECK(MV(*fn, fe[0]).frozen && MV(*fn, fe[1]).frozen && !MV(*fn, fo[0]).frozen);
        CHECK(!fn->ViewFor(0, 0).other.hero.frozen);

        // Mirror Image: two 0/2 Taunts; only one fits with six minions; refused with seven.
        std::unique_ptr<Match> mi = Fresh();
        CHECK(Cast(*mi, "CS2_027") == ActionError::None);
        std::vector<EntityId> mb = TA::BoardIds(*mi, 0);
        CHECK(mb.size() == 2 && BoardCount(*mi, 0, "CS2_mirror") == 2);
        CHECK(MV(*mi, mb[0]).attack == 0 && MV(*mi, mb[0]).health == 2 && MV(*mi, mb[0]).taunt);
        TA::SetBoard(*mi, 0, Strs(6, "CS2_168"));
        CHECK(Cast(*mi, "CS2_027") == ActionError::None);
        CHECK(TA::BoardIds(*mi, 0).size() == 7 && BoardCount(*mi, 0, "CS2_mirror") == 1);
        TA::SetBoard(*mi, 0, Strs(7, "CS2_168"));
        CHECK(Cast(*mi, "CS2_027") == ActionError::BoardFull);

        // Flamestrike: 4 damage to enemy minions (5 with Spell Damage), not to the hero or own minions.
        std::unique_ptr<Match> fs = Fresh();
        std::vector<EntityId> so = TA::SetBoard(*fs, 0, {"CS2_182"});
        std::vector<EntityId> se = TA::SetBoard(*fs, 1, {"CS2_182", "CS2_119"});
        CHECK(Cast(*fs, "CS2_032") == ActionError::None);
        CHECK(MV(*fs, se[0]).health == 1 && MV(*fs, se[1]).health == 3);
        CHECK(MV(*fs, so[0]).health == 5 && Hp(*fs, 1) == 30);
        TA::SetBoard(*fs, 0, {"CS2_142"});
        CHECK(Cast(*fs, "CS2_032") == ActionError::None);
        CHECK(!OnBoard(*fs, 1, se[0]) && !OnBoard(*fs, 1, se[1]));
    }

    // ---- Warrior ------------------------------------------------------------------

    void TestWarrior()
    {
        // Execute: a damaged enemy minion only.
        std::unique_ptr<Match> m = Fresh("WARRIOR");
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS2_200", "CS2_182"});
        TA::SetMinion(*m, en[1], 4, 4, 5);
        CHECK(Cast(*m, "CS2_108", en[0]) == ActionError::Target);
        CHECK(Cast(*m, "CS2_108", en[1]) == ActionError::None && !OnBoard(*m, 1, en[1]));

        // Whirlwind: 1 to all minions (both sides), none to heroes.
        std::unique_ptr<Match> w = Fresh("WARRIOR");
        std::vector<EntityId> wo = TA::SetBoard(*w, 0, {"CS2_182"});
        std::vector<EntityId> we = TA::SetBoard(*w, 1, {"CS2_182"});
        CHECK(Cast(*w, "EX1_400") == ActionError::None);
        CHECK(MV(*w, wo[0]).health == 4 && MV(*w, we[0]).health == 4 && Hp(*w, 0) == 30 && Hp(*w, 1) == 30);

        // Heroic Strike: +4 attack this turn.
        std::unique_ptr<Match> h = Fresh("WARRIOR");
        CHECK(Cast(*h, "CS2_105") == ActionError::None);
        CHECK(HeroOfSide0(*h).attack == 4 && HeroOfSide0(*h).canAttack);
        CHECK(h->Apply(0, Attack(HeroId(*h, 0), HeroId(*h, 1))) == ActionError::None && Hp(*h, 1) == 26);

        // Cleave: needs two enemy minions; 2 damage to two of them (3 with Spell Damage).
        std::unique_ptr<Match> c = Fresh("WARRIOR");
        std::vector<EntityId> ce = TA::SetBoard(*c, 1, {"CS2_200"});
        CHECK(Cast(*c, "CS2_114") == ActionError::Requirement);
        ce = TA::SetBoard(*c, 1, {"CS2_200", "CS2_200", "CS2_200"});
        CHECK(Cast(*c, "CS2_114") == ActionError::None);
        int hit = 0;
        for (EntityId id : ce)
            hit += MV(*c, id).health == 5;
        CHECK(hit == 2);

        // Charge: +2 attack and Charge for a friendly minion (not for enemies).
        std::unique_ptr<Match> ch = Fresh("WARRIOR");
        std::vector<EntityId> chb = TA::SetHand(*ch, 0, {"CS2_182", "CS2_103"});
        CHECK(Do0(*ch, Play(chb[0])) == ActionError::None);
        std::vector<EntityId> yeti = TA::BoardIds(*ch, 0);
        CHECK(!MV(*ch, yeti[0]).canAttack);
        TA::SetBoard(*ch, 1, {"CS2_168"});
        CHECK(Do0(*ch, Play(chb[1], TA::BoardIds(*ch, 1)[0])) == ActionError::Target);
        CHECK(Do0(*ch, Play(chb[1], HeroId(*ch, 0))) == ActionError::Target);
        CHECK(Do0(*ch, Play(chb[1], yeti[0])) == ActionError::None);
        CHECK(MV(*ch, yeti[0]).attack == 6 && MV(*ch, yeti[0]).charge && MV(*ch, yeti[0]).canAttack);

        // Shield Block: 5 armor and a card.
        std::unique_ptr<Match> sb = Fresh("WARRIOR");
        CHECK(Cast(*sb, "EX1_606") == ActionError::None);
        CHECK(ArmorOf(*sb, 0) == 5 && Hand(*sb, 0) == 1);

        // Warsong Commander: minions with 3 or less attack get Charge, others not; the commander itself not.
        std::unique_ptr<Match> wc = Fresh("WARRIOR");
        std::vector<EntityId> wh = TA::SetHand(*wc, 0, {"EX1_084", "CS2_172", "CS2_200"});
        for (EntityId id : wh)
            CHECK(Do0(*wc, Play(id)) == ActionError::None);
        std::vector<EntityId> wb = TA::BoardIds(*wc, 0);
        CHECK(!MV(*wc, wb[0]).canAttack && MV(*wc, wb[1]).canAttack && !MV(*wc, wb[2]).canAttack);
    }

    // ---- Shaman ---------------------------------------------------------------------

    void TestShaman()
    {
        // Frost Shock: 1 damage and Freeze an enemy character; friendly targets are refused.
        std::unique_ptr<Match> m = Fresh("SHAMAN");
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS2_200"});
        std::vector<EntityId> own = TA::SetBoard(*m, 0, {"CS2_200"});
        CHECK(Cast(*m, "CS2_037", own[0]) == ActionError::Target);
        CHECK(Cast(*m, "CS2_037", HeroId(*m, 0)) == ActionError::Target);
        CHECK(Cast(*m, "CS2_037", en[0]) == ActionError::None);
        CHECK(MV(*m, en[0]).health == 6 && MV(*m, en[0]).frozen);
        CHECK(Cast(*m, "CS2_037", HeroId(*m, 1)) == ActionError::None);
        CHECK(Hp(*m, 1) == 29 && m->ViewFor(0, 0).other.hero.frozen);

        // Windfury: attacks twice.
        std::unique_ptr<Match> wf = Fresh("SHAMAN");
        std::vector<EntityId> wo = TA::SetBoard(*wf, 0, {"CS2_200"});
        CHECK(Cast(*wf, "CS2_039", wo[0]) == ActionError::None);
        CHECK(MV(*wf, wo[0]).windfury);
        CHECK(wf->Apply(0, Attack(wo[0], HeroId(*wf, 1))) == ActionError::None);
        CHECK(wf->Apply(0, Attack(wo[0], HeroId(*wf, 1))) == ActionError::None);
        CHECK(wf->Apply(0, Attack(wo[0], HeroId(*wf, 1))) == ActionError::Attack);
        CHECK(Hp(*wf, 1) == 18);

        // Ancestral Healing: full health and Taunt (also on an undamaged minion).
        std::unique_ptr<Match> ah = Fresh("SHAMAN");
        std::vector<EntityId> ao = TA::SetBoard(*ah, 0, {"CS2_182", "CS2_168"});
        TA::SetMinion(*ah, ao[0], 4, 2, 5);
        CHECK(Cast(*ah, "CS2_041", ao[0]) == ActionError::None);
        CHECK(MV(*ah, ao[0]).health == 5 && MV(*ah, ao[0]).taunt);
        CHECK(Cast(*ah, "CS2_041", ao[1]) == ActionError::None && MV(*ah, ao[1]).taunt && MV(*ah, ao[1]).health == 1);
        CHECK(Cast(*ah, "CS2_041", HeroId(*ah, 0)) == ActionError::Target);

        // Fire Elemental: 3 damage to a chosen character; a target is required while one exists.
        std::unique_ptr<Match> fe = Fresh("SHAMAN");
        std::vector<EntityId> fh = TA::SetHand(*fe, 0, {"CS2_042", "CS2_042"});
        CHECK(Do0(*fe, Play(fh[0], 0)) == ActionError::Target);
        CHECK(Do0(*fe, Play(fh[0], HeroId(*fe, 1))) == ActionError::None);
        CHECK(Hp(*fe, 1) == 27 && BoardCount(*fe, 0, "CS2_042") == 1);
        std::vector<EntityId> fo = TA::BoardIds(*fe, 0);
        CHECK(Do0(*fe, Play(fh[1], fo[0])) == ActionError::None);
        CHECK(MV(*fe, fo[0]).health == 2);

        // Rockbiter Weapon: +3 attack this turn for a friendly character.
        std::unique_ptr<Match> rb = Fresh("SHAMAN");
        std::vector<EntityId> rm = TA::SetBoard(*rb, 0, {"CS2_168"});
        TA::SetBoard(*rb, 1, {"CS2_200"});
        CHECK(Cast(*rb, "CS2_045", TA::BoardIds(*rb, 1)[0]) == ActionError::Target);
        CHECK(Cast(*rb, "CS2_045", HeroId(*rb, 1)) == ActionError::Target);
        CHECK(Cast(*rb, "CS2_045", HeroId(*rb, 0)) == ActionError::None);
        CHECK(HeroOfSide0(*rb).attack == 3 && HeroOfSide0(*rb).canAttack);
        CHECK(Cast(*rb, "CS2_045", rm[0]) == ActionError::None);
        CHECK(MV(*rb, rm[0]).attack == 5);
        rb->Apply(0, End());
        CHECK(MV(*rb, rm[0]).attack == 2);

        // Bloodlust: +3 attack this turn for your minions only.
        std::unique_ptr<Match> bl = Fresh("SHAMAN");
        std::vector<EntityId> bo = TA::SetBoard(*bl, 0, {"CS2_168", "CS2_200"});
        std::vector<EntityId> be = TA::SetBoard(*bl, 1, {"CS2_168"});
        CHECK(Cast(*bl, "CS2_046") == ActionError::None);
        CHECK(MV(*bl, bo[0]).attack == 5 && MV(*bl, bo[1]).attack == 9 && MV(*bl, be[0]).attack == 2);
        CHECK(HeroOfSide0(*bl).attack == 0);
        bl->Apply(0, End());
        CHECK(MV(*bl, bo[0]).attack == 2 && MV(*bl, bo[1]).attack == 6);

        // Totemic Might: +2 health for your Totems only.
        std::unique_ptr<Match> tm = Fresh("SHAMAN");
        std::vector<EntityId> to = TA::SetBoard(*tm, 0, {"CS2_050", "CS2_168", "NEW1_009"});
        std::vector<EntityId> te = TA::SetBoard(*tm, 1, {"CS2_050"});
        CHECK(Cast(*tm, "EX1_244") == ActionError::None);
        CHECK(MV(*tm, to[0]).health == 3 && MV(*tm, to[0]).attack == 1 && MV(*tm, to[1]).health == 1 &&
            MV(*tm, to[2]).health == 4 && MV(*tm, te[0]).health == 1);

        // Hex: a 0/1 Frog with Taunt in the same slot (own minions and heroes: only minions).
        std::unique_ptr<Match> hx = Fresh("SHAMAN");
        std::vector<EntityId> hb = TA::SetBoard(*hx, 1, {"CS2_168", "CS2_200", "CS2_168"});
        CHECK(Cast(*hx, "EX1_246", HeroId(*hx, 1)) == ActionError::Target);
        CHECK(Cast(*hx, "EX1_246", hb[1]) == ActionError::None);
        Strs const cards = TA::BoardCardIds(*hx, 1);
        CHECK(cards.size() == 3 && cards[1] == "hexfrog");
        MinionView const frog = MV(*hx, hb[1]);
        CHECK(frog.attack == 0 && frog.health == 1 && frog.taunt);

        // Flametongue Totem: adjacent minions +2 attack, recomputed when a neighbour dies.
        std::unique_ptr<Match> ft = Fresh("SHAMAN");
        std::vector<EntityId> fb = TA::SetBoard(*ft, 0, {"CS2_168", "EX1_565", "CS2_200", "CS2_182"});
        CHECK(MV(*ft, fb[0]).attack == 4 && MV(*ft, fb[2]).attack == 8 && MV(*ft, fb[3]).attack == 4);
        TA::SetMinion(*ft, fb[2], 6, 3, 7);
        CHECK(Cast(*ft, "CS2_057", fb[2]) == ActionError::None);
        CHECK(!OnBoard(*ft, 0, fb[2]));
        CHECK(MV(*ft, fb[0]).attack == 4 && MV(*ft, fb[3]).attack == 6);
        // The totem itself is not buffed and its own death ends the aura.
        CHECK(MV(*ft, fb[1]).attack == 0);
        CHECK(Cast(*ft, "CS2_057", fb[1]) == ActionError::None);
        CHECK(MV(*ft, fb[1]).id == 0);
        CHECK(Cast(*ft, "CS2_057", fb[1]) == ActionError::Target);

        // Windspeaker: Windfury for a friendly minion; optional target only when no friendly minion exists.
        std::unique_ptr<Match> ws = Fresh("SHAMAN");
        std::vector<EntityId> wsh = TA::SetHand(*ws, 0, {"EX1_587", "EX1_587", "EX1_587"});
        std::vector<EntityId> wse = TA::SetBoard(*ws, 1, {"CS2_200"});
        CHECK(Do0(*ws, Play(wsh[0], wse[0])) == ActionError::Target);
        CHECK(Do0(*ws, Play(wsh[0], 0)) == ActionError::None);
        std::vector<EntityId> wso = TA::BoardIds(*ws, 0);
        CHECK(!MV(*ws, wso[0]).windfury);
        CHECK(Do0(*ws, Play(wsh[1], 0)) == ActionError::Target);
        CHECK(Do0(*ws, Play(wsh[1], HeroId(*ws, 0))) == ActionError::Target);
        CHECK(Do0(*ws, Play(wsh[1], wso[0])) == ActionError::None);
        CHECK(MV(*ws, wso[0]).windfury && !MV(*ws, TA::BoardIds(*ws, 0)[1]).windfury);
    }

    // ---- Warlock --------------------------------------------------------------------

    void TestWarlock()
    {
        // Shadow Bolt: 4 damage to a minion only.
        std::unique_ptr<Match> m = Fresh("WARLOCK");
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS2_200"});
        CHECK(Cast(*m, "CS2_057", HeroId(*m, 1)) == ActionError::Target);
        CHECK(Cast(*m, "CS2_057", 0) == ActionError::Target);
        CHECK(Cast(*m, "CS2_057", en[0]) == ActionError::None && MV(*m, en[0]).health == 3);
        TA::SetBoard(*m, 0, {"EX1_582"});
        CHECK(Cast(*m, "CS2_057", en[0]) == ActionError::None && !OnBoard(*m, 1, en[0]));

        // Drain Life: 2 damage (3 with Spell Damage), restore 2 to your hero (not more).
        std::unique_ptr<Match> d = Fresh("WARLOCK");
        TA::SetHero(*d, 0, 20, 0);
        CHECK(Cast(*d, "CS2_061", HeroId(*d, 1)) == ActionError::None);
        CHECK(Hp(*d, 1) == 28 && Hp(*d, 0) == 22);
        std::vector<EntityId> dsp = TA::SetBoard(*d, 0, {"CS2_142"});
        CHECK(Cast(*d, "CS2_061", HeroId(*d, 1)) == ActionError::None);
        CHECK(Hp(*d, 1) == 25 && Hp(*d, 0) == 24);
        CHECK(Cast(*d, "CS2_061", dsp[0]) == ActionError::None);
        CHECK(!OnBoard(*d, 0, dsp[0]));
        CHECK(Hp(*d, 0) == 26);

        // Hellfire: 3 damage to ALL characters (Spell Damage counts).
        std::unique_ptr<Match> h = Fresh("WARLOCK");
        std::vector<EntityId> ho = TA::SetBoard(*h, 0, {"CS2_200"});
        std::vector<EntityId> he = TA::SetBoard(*h, 1, {"CS2_200"});
        CHECK(Cast(*h, "CS2_062") == ActionError::None);
        CHECK(MV(*h, ho[0]).health == 4 && MV(*h, he[0]).health == 4 && Hp(*h, 0) == 27 && Hp(*h, 1) == 27);

        // Dread Infernal: 1 damage to all other characters, not to itself.
        std::unique_ptr<Match> di = Fresh("WARLOCK");
        std::vector<EntityId> dio = TA::SetBoard(*di, 0, {"CS2_168", "CS2_200"});
        std::vector<EntityId> die = TA::SetBoard(*di, 1, {"CS2_200"});
        std::vector<EntityId> dih = TA::SetHand(*di, 0, {"CS2_064"});
        CHECK(Do0(*di, Play(dih[0])) == ActionError::None);
        CHECK(!OnBoard(*di, 0, dio[0]) && MV(*di, dio[1]).health == 6 && MV(*di, die[0]).health == 6);
        CHECK(Hp(*di, 0) == 29 && Hp(*di, 1) == 29);
        std::vector<EntityId> dib = TA::BoardIds(*di, 0);
        CHECK(MV(*di, dib.back()).health == 6 && MV(*di, dib.back()).attack == 6);

        // Mortal Coil: 1 damage to a minion; draws only when it kills (not through a Divine Shield).
        std::unique_ptr<Match> mc = Fresh("WARLOCK");
        std::vector<EntityId> mce = TA::SetBoard(*mc, 1, {"CS2_168", "CS2_200", "CS2_168"});
        CHECK(Cast(*mc, "EX1_302", HeroId(*mc, 1)) == ActionError::Target);
        CHECK(Cast(*mc, "EX1_302", mce[0]) == ActionError::None);
        CHECK(!OnBoard(*mc, 1, mce[0]) && Hand(*mc, 0) == 1);
        CHECK(Cast(*mc, "EX1_302", mce[1]) == ActionError::None);
        CHECK(Hand(*mc, 0) == 0 && MV(*mc, mce[1]).health == 6);
        CHECK(Cast(*mc, "EX1_371", mce[2]) == ActionError::None);
        CHECK(Cast(*mc, "EX1_302", mce[2]) == ActionError::None);
        CHECK(OnBoard(*mc, 1, mce[2]) && Hand(*mc, 0) == 0 && !MV(*mc, mce[2]).divineShield);
        CHECK(Cast(*mc, "EX1_302", mce[2]) == ActionError::None);
        CHECK(!OnBoard(*mc, 1, mce[2]) && Hand(*mc, 0) == 1);

        // Succubus: Battlecry: discard a random card (nothing to discard is fine).
        std::unique_ptr<Match> su = Fresh("WARLOCK");
        std::vector<EntityId> sh = TA::SetHand(*su, 0, {"EX1_306", "CS2_029", "CS2_022"});
        uint32_t const before = su->LastEventSeq();
        CHECK(Do0(*su, Play(sh[0])) == ActionError::None);
        CHECK(Hand(*su, 0) == 1 && CountEvents(*su, EventType::Discard, before) == 1);
        std::vector<EntityId> sh2 = TA::SetHand(*su, 0, {"EX1_306"});
        CHECK(Do0(*su, Play(sh2[0])) == ActionError::None && Hand(*su, 0) == 0);

        // Soulfire: 4 damage (5 with Spell Damage) and discard a random card.
        std::unique_ptr<Match> sf = Fresh("WARLOCK");
        std::vector<EntityId> sfh = TA::SetHand(*sf, 0, {"EX1_308", "CS2_168"});
        CHECK(Do0(*sf, Play(sfh[0], HeroId(*sf, 1))) == ActionError::None);
        CHECK(Hp(*sf, 1) == 26 && Hand(*sf, 0) == 0 && CountEvents(*sf, EventType::Discard) == 1);
        CHECK(Cast(*sf, "EX1_308", HeroId(*sf, 1)) == ActionError::None && Hp(*sf, 1) == 22);

        // Sacrificial Pact: destroy a Demon of either side, restore 5 to your hero; no other target.
        std::unique_ptr<Match> sp = Fresh("WARLOCK");
        std::vector<EntityId> spo = TA::SetBoard(*sp, 0, {"CS2_065", "CS2_168"});
        std::vector<EntityId> spe = TA::SetBoard(*sp, 1, {"CS2_065", "CS2_200"});
        TA::SetHero(*sp, 0, 20, 0);
        CHECK(Cast(*sp, "NEW1_003", spo[1]) == ActionError::Target);
        CHECK(Cast(*sp, "NEW1_003", spe[1]) == ActionError::Target);
        CHECK(Cast(*sp, "NEW1_003", HeroId(*sp, 1)) == ActionError::Target);
        CHECK(Cast(*sp, "NEW1_003", spe[0]) == ActionError::None);
        CHECK(!OnBoard(*sp, 1, spe[0]) && Hp(*sp, 0) == 25);
        CHECK(Cast(*sp, "NEW1_003", spo[0]) == ActionError::None);
        CHECK(!OnBoard(*sp, 0, spo[0]) && Hp(*sp, 0) == 30);

        // Corruption: destroyed at the start of your next turn.
        std::unique_ptr<Match> co = Fresh("WARLOCK");
        std::vector<EntityId> coe = TA::SetBoard(*co, 1, {"CS2_200"});
        CHECK(Cast(*co, "CS2_063", coe[0]) == ActionError::None);
        co->Apply(0, End());
        co->Apply(1, End());
        CHECK(!OnBoard(*co, 1, coe[0]));
    }

    void TestCorruptionFeedback()
    {
        // A mark reports the resolved target without claiming immediate damage or destruction.
        std::unique_ptr<Match> m = Fresh("WARLOCK", "MAGE");
        EntityId const target = TA::SetBoard(*m, 1, {"CS2_200"})[0];
        EntityId const source = TA::SetHand(*m, 0, {"CS2_063"})[0];
        uint32_t const before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(source, target)) == ActionError::None);
        CHECK(CountEvents(*m, EventType::Buff, before) == 1);
        CHECK(CountEvents(*m, EventType::Destroy, before) == 0);
        CHECK(CountEvents(*m, EventType::Damage, before) == 0);
        CHECK(OnBoard(*m, 1, target) && MV(*m, target).health == 7);
        for (Event const& e : m->ViewFor(0, before).events)
            if (e.type == EventType::Buff)
                CHECK(e.source == source && e.target == target && e.side == 1 && e.amount == 0);
        CHECK(m->Apply(0, End()) == ActionError::None && OnBoard(*m, 1, target));
        uint32_t const beforeReturn = m->LastEventSeq();
        CHECK(m->Apply(1, End()) == ActionError::None && !OnBoard(*m, 1, target));
        CHECK(CountEvents(*m, EventType::Destroy, beforeReturn) == 1);
        CHECK(CountEvents(*m, EventType::Buff, beforeReturn) == 0);

        // Counterspell cannot announce an enchantment that was never applied.
        std::unique_ptr<Match> countered = Fresh("WARLOCK", "MAGE");
        EntityId const counterTarget = TA::SetBoard(*countered, 1, {"CS2_200"})[0];
        TA::SetSecrets(*countered, 1, {"EX1_287"});
        uint32_t const beforeCounter = countered->LastEventSeq();
        CHECK(Cast(*countered, "CS2_063", counterTarget) == ActionError::None);
        CHECK(CountEvents(*countered, EventType::Counter, beforeCounter) == 1);
        CHECK(CountEvents(*countered, EventType::Buff, beforeCounter) == 0);
        CHECK(countered->Apply(0, End()) == ActionError::None);
        CHECK(countered->Apply(1, End()) == ActionError::None && OnBoard(*countered, 1, counterTarget));

        // Spellbender redirects the mark: the requested Play target is not the enchanted entity.
        std::unique_ptr<Match> redirected = Fresh("WARLOCK", "MAGE");
        EntityId const requested = TA::SetBoard(*redirected, 1, {"CS2_200"})[0];
        TA::SetSecrets(*redirected, 1, {"tt_010"});
        uint32_t const beforeRedirect = redirected->LastEventSeq();
        CHECK(Cast(*redirected, "CS2_063", requested) == ActionError::None);
        EntityId resolved = 0;
        for (Event const& e : redirected->ViewFor(0, beforeRedirect).events)
            if (e.type == EventType::Buff)
                resolved = e.target;
        CHECK(resolved != 0 && resolved != requested && OnBoard(*redirected, 1, resolved));
        CHECK(CountEvents(*redirected, EventType::Buff, beforeRedirect) == 1);
        CHECK(redirected->Apply(0, End()) == ActionError::None);
        CHECK(redirected->Apply(1, End()) == ActionError::None);
        CHECK(OnBoard(*redirected, 1, requested) && !OnBoard(*redirected, 1, resolved));

        // Removing the enchantment suppresses its later destruction, rather than replaying a cached cue.
        std::unique_ptr<Match> silenced = Fresh("WARLOCK", "MAGE");
        EntityId const silenceTarget = TA::SetBoard(*silenced, 1, {"CS2_200"})[0];
        CHECK(Cast(*silenced, "CS2_063", silenceTarget) == ActionError::None);
        CHECK(Cast(*silenced, "EX1_332", silenceTarget) == ActionError::None);
        uint32_t const beforeSilenceReturn = silenced->LastEventSeq();
        CHECK(silenced->Apply(0, End()) == ActionError::None);
        CHECK(silenced->Apply(1, End()) == ActionError::None && OnBoard(*silenced, 1, silenceTarget));
        CHECK(CountEvents(*silenced, EventType::Destroy, beforeSilenceReturn) == 0);
    }

    // ---- Rogue ----------------------------------------------------------------------

    void TestRogue()
    {
        // Backstab: 2 damage to an undamaged minion.
        std::unique_ptr<Match> m = Fresh("ROGUE");
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS2_200", "CS2_182"});
        TA::SetMinion(*m, en[1], 4, 4, 5);
        CHECK(Cast(*m, "CS2_072", en[1]) == ActionError::Target);
        CHECK(Cast(*m, "CS2_072", HeroId(*m, 1)) == ActionError::Target);
        CHECK(Cast(*m, "CS2_072", en[0]) == ActionError::None && MV(*m, en[0]).health == 5);
        CHECK(Cast(*m, "CS2_072", en[0]) == ActionError::Target);

        // Deadly Poison: +2 attack for the weapon; needs one.
        std::unique_ptr<Match> dp = Fresh("ROGUE");
        CHECK(Cast(*dp, "CS2_074") == ActionError::Requirement);
        CHECK(Cast(*dp, "CS2_082") == ActionError::None);
        CHECK(Cast(*dp, "CS2_074") == ActionError::None);
        CHECK(HeroOfSide0(*dp).weaponAttack == 3 && HeroOfSide0(*dp).attack == 3 && HeroOfSide0(*dp).weaponDurability == 2);

        // Sinister Strike: 3 damage to the enemy hero (4 with Spell Damage); no target.
        std::unique_ptr<Match> ss = Fresh("ROGUE");
        CHECK(Cast(*ss, "CS2_075", HeroId(*ss, 1)) == ActionError::Target);
        CHECK(Cast(*ss, "CS2_075") == ActionError::None && Hp(*ss, 1) == 27 && Hp(*ss, 0) == 30);
        TA::SetBoard(*ss, 0, {"CS2_142"});
        CHECK(Cast(*ss, "CS2_075") == ActionError::None && Hp(*ss, 1) == 23);

        // Assassinate: an enemy minion only.
        std::unique_ptr<Match> as = Fresh("ROGUE");
        std::vector<EntityId> ao = TA::SetBoard(*as, 0, {"CS2_200"});
        std::vector<EntityId> ae = TA::SetBoard(*as, 1, {"CS2_200"});
        CHECK(Cast(*as, "CS2_076", ao[0]) == ActionError::Target);
        CHECK(Cast(*as, "CS2_076", HeroId(*as, 1)) == ActionError::Target);
        CHECK(Cast(*as, "CS2_076", ae[0]) == ActionError::None && !OnBoard(*as, 1, ae[0]) && OnBoard(*as, 0, ao[0]));

        // Sprint: draw 4.
        std::unique_ptr<Match> sp = Fresh("ROGUE");
        std::size_t const deckBefore = TA::DeckIds(*sp, 0).size();
        CHECK(Cast(*sp, "CS2_077") == ActionError::None && Hand(*sp, 0) == 4 && TA::DeckIds(*sp, 0).size() == deckBefore - 4);

        // Fan of Knives: 1 to enemy minions and a card.
        std::unique_ptr<Match> fk = Fresh("ROGUE");
        std::vector<EntityId> fo = TA::SetBoard(*fk, 0, {"CS2_168"});
        std::vector<EntityId> fe = TA::SetBoard(*fk, 1, {"CS2_168", "CS2_200"});
        CHECK(Cast(*fk, "EX1_129") == ActionError::None);
        CHECK(!OnBoard(*fk, 1, fe[0]) && MV(*fk, fe[1]).health == 6 && OnBoard(*fk, 0, fo[0]) && Hand(*fk, 0) == 1);
        TA::SetBoard(*fk, 0, {"CS2_142"});
        CHECK(Cast(*fk, "EX1_129") == ActionError::None && MV(*fk, fe[1]).health == 4);

        // Shiv: 1 damage (2 with Spell Damage) and a card.
        std::unique_ptr<Match> sh = Fresh("ROGUE");
        CHECK(Cast(*sh, "EX1_278", HeroId(*sh, 1)) == ActionError::None);
        CHECK(Hp(*sh, 1) == 29 && Hand(*sh, 0) == 1);
        CHECK(Cast(*sh, "EX1_278", 0) == ActionError::Target);

        // Sap: an enemy minion returns to its owner's hand; destroyed when that hand is full.
        std::unique_ptr<Match> sa = Fresh("ROGUE");
        std::vector<EntityId> so = TA::SetBoard(*sa, 0, {"CS2_200"});
        std::vector<EntityId> se = TA::SetBoard(*sa, 1, {"CS2_200", "CS2_182"});
        CHECK(Cast(*sa, "EX1_581", so[0]) == ActionError::Target);
        CHECK(Cast(*sa, "EX1_581", se[0]) == ActionError::None);
        CHECK(!OnBoard(*sa, 1, se[0]) && TA::HandCardIds(*sa, 1) == Strs{"CS2_200"});
        TA::SetHand(*sa, 1, Strs(10, "CS2_168"));
        CHECK(Cast(*sa, "EX1_581", se[1]) == ActionError::None);
        CHECK(!OnBoard(*sa, 1, se[1]) && Hand(*sa, 1) == 10);

        // Vanish: all minions return to their owners' hands (own first); a full hand destroys the rest.
        std::unique_ptr<Match> va = Fresh("ROGUE");
        std::vector<EntityId> vo = TA::SetBoard(*va, 0, {"CS2_182", "CS2_168", "CS2_200"});
        std::vector<EntityId> ve = TA::SetBoard(*va, 1, {"CS2_168", "CS2_200"});
        CHECK(Cast(*va, "NEW1_004") == ActionError::None);
        CHECK(TA::BoardIds(*va, 0).empty() && TA::BoardIds(*va, 1).empty());
        CHECK(Hand(*va, 0) == 3 && Hand(*va, 1) == 2);
        (void)vo;
        (void)ve;
        // Full hands: 8 cards are left in the hand after Vanish, so two of the three minions fit, the third is
        // destroyed; the enemy hand is full, so both its minions are destroyed.
        std::unique_ptr<Match> vf = Fresh("ROGUE");
        TA::SetBoard(*vf, 0, {"CS2_182", "CS2_168", "CS2_200"});
        TA::SetBoard(*vf, 1, {"CS2_168", "CS2_200"});
        TA::SetHand(*vf, 1, Strs(10, "CS2_168"));
        Strs hand(8, "CS2_168");
        hand.push_back("NEW1_004");
        uint32_t const since = vf->LastEventSeq();
        CHECK(CastHand(*vf, hand, "NEW1_004") == ActionError::None);
        CHECK(TA::BoardIds(*vf, 0).empty() && TA::BoardIds(*vf, 1).empty());
        CHECK(Hand(*vf, 0) == 10 && Hand(*vf, 1) == 10);
        CHECK(CountEvents(*vf, EventType::Bounce, since) == 2 && CountEvents(*vf, EventType::Destroy, since) == 3);
    }

    // ---- Hunter ---------------------------------------------------------------------

    void TestHunter()
    {
        // Hunter's Mark: health becomes 1 (damaged or not).
        std::unique_ptr<Match> m = Fresh("HUNTER");
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS2_200", "CS2_182"});
        TA::SetMinion(*m, en[1], 4, 2, 5);
        CHECK(Cast(*m, "CS2_084", HeroId(*m, 1)) == ActionError::Target);
        CHECK(Cast(*m, "CS2_084", en[0]) == ActionError::None);
        CHECK(MV(*m, en[0]).health == 1 && MV(*m, en[0]).maxHealth == 1 && MV(*m, en[0]).attack == 6);
        CHECK(Cast(*m, "CS2_084", en[1]) == ActionError::None);
        CHECK(MV(*m, en[1]).health == 1 && MV(*m, en[1]).maxHealth == 1);

        // Starving Buzzard: draws whenever you summon another Beast; not for other minions, not for the enemy.
        std::unique_ptr<Match> sb = Fresh("HUNTER");
        TA::SetBoard(*sb, 0, {"CS2_237"});
        std::vector<EntityId> h = TA::SetHand(*sb, 0, {"CS2_172", "CS2_168", "CS2_237"});
        CHECK(Do0(*sb, Play(h[0])) == ActionError::None && Hand(*sb, 0) == 3);
        CHECK(Do0(*sb, Play(h[1])) == ActionError::None && Hand(*sb, 0) == 2);
        CHECK(Do0(*sb, Play(h[2])) == ActionError::None && Hand(*sb, 0) == 2);
        TA::SetBoard(*sb, 1, {"CS2_237"});
        TA::SetHand(*sb, 0, {});
        sb->Apply(0, End());
        std::vector<EntityId> eh = TA::SetHand(*sb, 1, {"CS2_172"});
        TA::SetMana(*sb, 1, 10, 10);
        CHECK(sb->Apply(1, Play(eh[0])) == ActionError::None && Hand(*sb, 1) == 1 && Hand(*sb, 0) == 0);
        // The first Buzzard on an empty board draws nothing for itself.
        std::unique_ptr<Match> sb2 = Fresh("HUNTER");
        CHECK(Cast(*sb2, "CS2_237") == ActionError::None && Hand(*sb2, 0) == 0);

        // Houndmaster: a friendly Beast gets +2/+2 and Taunt; optional when you have no Beast.
        std::unique_ptr<Match> hm = Fresh("HUNTER");
        std::vector<EntityId> hmo = TA::SetBoard(*hm, 0, {"CS2_168", "CS2_172"});
        TA::SetBoard(*hm, 1, {"CS2_172"});
        std::vector<EntityId> hmh = TA::SetHand(*hm, 0, {"DS1_070", "DS1_070", "DS1_070"});
        CHECK(Do0(*hm, Play(hmh[0], 0)) == ActionError::Target);
        CHECK(Do0(*hm, Play(hmh[0], hmo[0])) == ActionError::Target);
        CHECK(Do0(*hm, Play(hmh[0], TA::BoardIds(*hm, 1)[0])) == ActionError::Target);
        CHECK(Do0(*hm, Play(hmh[0], HeroId(*hm, 0))) == ActionError::Target);
        CHECK(Do0(*hm, Play(hmh[0], hmo[1])) == ActionError::None);
        CHECK(MV(*hm, hmo[1]).attack == 5 && MV(*hm, hmo[1]).health == 4 && MV(*hm, hmo[1]).taunt);
        std::unique_ptr<Match> hn = Fresh("HUNTER");
        TA::SetBoard(*hn, 0, {"CS2_168"});
        std::vector<EntityId> hnh = TA::SetHand(*hn, 0, {"DS1_070"});
        CHECK(Do0(*hn, Play(hnh[0], 0)) == ActionError::None);
        std::unique_ptr<Match> hz = Fresh("HUNTER");
        std::vector<EntityId> hzh = TA::SetHand(*hz, 0, {"DS1_070"});
        CHECK(Do0(*hz, Play(hzh[0], 0)) == ActionError::None);
        // The Houndmaster itself is no Beast.
        CHECK(MV(*hz, TA::BoardIds(*hz, 0)[0]).attack == 4 && !MV(*hz, TA::BoardIds(*hz, 0)[0]).taunt);

        // Timber Wolf: your other Beasts +1 attack.
        std::unique_ptr<Match> tw = Fresh("HUNTER");
        std::vector<EntityId> two = TA::SetBoard(*tw, 0, {"DS1_175", "CS2_172", "CS2_168"});
        std::vector<EntityId> twe = TA::SetBoard(*tw, 1, {"CS2_172"});
        CHECK(MV(*tw, two[0]).attack == 1 && MV(*tw, two[1]).attack == 4 && MV(*tw, two[2]).attack == 2);
        CHECK(MV(*tw, twe[0]).attack == 3);
        std::vector<EntityId> two2 = TA::SetBoard(*tw, 0, {"DS1_175", "DS1_175"});
        CHECK(MV(*tw, two2[0]).attack == 2 && MV(*tw, two2[1]).attack == 2);

        // Tundra Rhino: your Beasts have Charge (itself included, new Beasts too), other minions not.
        std::unique_ptr<Match> tr = Fresh("HUNTER");
        std::vector<EntityId> trh = TA::SetHand(*tr, 0, {"DS1_178", "CS2_172", "CS2_168"});
        for (EntityId id : trh)
            CHECK(Do0(*tr, Play(id)) == ActionError::None);
        std::vector<EntityId> trb = TA::BoardIds(*tr, 0);
        CHECK(MV(*tr, trb[0]).charge && MV(*tr, trb[0]).canAttack);
        CHECK(MV(*tr, trb[1]).charge && MV(*tr, trb[1]).canAttack);
        CHECK(!MV(*tr, trb[2]).charge && !MV(*tr, trb[2]).canAttack);
        TA::SetBoard(*tr, 1, {"CS2_172"});
        CHECK(!MV(*tr, TA::BoardIds(*tr, 1)[0]).charge);

        // Multi-Shot: needs two enemy minions; 3 damage to two of them.
        std::unique_ptr<Match> ms = Fresh("HUNTER");
        std::vector<EntityId> mse = TA::SetBoard(*ms, 1, {"CS2_200"});
        CHECK(Cast(*ms, "DS1_183") == ActionError::Requirement);
        mse = TA::SetBoard(*ms, 1, {"CS2_200", "CS2_200", "CS2_200"});
        CHECK(Cast(*ms, "DS1_183") == ActionError::None);
        int hit = 0;
        for (EntityId id : mse)
            hit += MV(*ms, id).health == 4;
        CHECK(hit == 2 && Hp(*ms, 1) == 30);
        TA::SetBoard(*ms, 0, {"CS2_142"});
        TA::SetBoard(*ms, 1, {"CS2_200", "CS2_200"});
        mse = TA::BoardIds(*ms, 1);
        CHECK(Cast(*ms, "DS1_183") == ActionError::None);
        CHECK(MV(*ms, mse[0]).health == 3 && MV(*ms, mse[1]).health == 3);

        // Arcane Shot: 2 damage (3 with Spell Damage).
        std::unique_ptr<Match> as = Fresh("HUNTER");
        CHECK(Cast(*as, "DS1_185", HeroId(*as, 1)) == ActionError::None && Hp(*as, 1) == 28);
        TA::SetBoard(*as, 0, {"CS2_142"});
        CHECK(Cast(*as, "DS1_185", HeroId(*as, 1)) == ActionError::None && Hp(*as, 1) == 25);

        // Kill Command: 3 damage, or 5 when you have a Beast (an enemy Beast does not count); Spell Damage adds.
        std::unique_ptr<Match> kc = Fresh("HUNTER");
        TA::SetBoard(*kc, 1, {"CS2_172"});
        CHECK(Cast(*kc, "EX1_539", HeroId(*kc, 1)) == ActionError::None && Hp(*kc, 1) == 27);
        TA::SetBoard(*kc, 0, {"CS2_172"});
        CHECK(Cast(*kc, "EX1_539", HeroId(*kc, 1)) == ActionError::None && Hp(*kc, 1) == 22);
        TA::SetBoard(*kc, 0, {"CS2_172", "CS2_142"});
        CHECK(Cast(*kc, "EX1_539", HeroId(*kc, 1)) == ActionError::None && Hp(*kc, 1) == 16);
        TA::SetBoard(*kc, 0, {"CS2_142"});
        CHECK(Cast(*kc, "EX1_539", HeroId(*kc, 1)) == ActionError::None && Hp(*kc, 1) == 12);

        // Animal Companion: Misha, Leokk or Huffer at random (not unique); needs room.
        std::set<std::string> seen;
        int duplicates = 0;
        for (uint32_t seed = 1; seed <= 60; ++seed)
        {
            std::unique_ptr<Match> ac = Fresh("HUNTER", "WARRIOR", seed);
            TA::SetBoard(*ac, 0, {"NEW1_032"});
            CHECK(Cast(*ac, "NEW1_031") == ActionError::None);
            Strs const b = TA::BoardCardIds(*ac, 0);
            CHECK(b.size() == 2);
            seen.insert(b[1]);
            duplicates += b[1] == "NEW1_032";
            CHECK(b[1] == "NEW1_032" || b[1] == "NEW1_033" || b[1] == "NEW1_034");
        }
        CHECK(seen.size() == 3 && duplicates > 0);
        std::unique_ptr<Match> af = Fresh("HUNTER");
        TA::SetBoard(*af, 0, Strs(7, "CS2_168"));
        CHECK(Cast(*af, "NEW1_031") == ActionError::BoardFull);

        // Leokk: your other minions +1 attack; Misha and Huffer as printed.
        std::unique_ptr<Match> lk = Fresh("HUNTER");
        std::vector<EntityId> lb = TA::SetBoard(*lk, 0, {"NEW1_033", "CS2_168", "NEW1_032", "NEW1_034"});
        CHECK(MV(*lk, lb[0]).attack == 2 && MV(*lk, lb[1]).attack == 3 && MV(*lk, lb[2]).attack == 5);
        CHECK(MV(*lk, lb[2]).taunt && MV(*lk, lb[3]).charge && MV(*lk, lb[3]).attack == 5);

        // Tracking: look at three, draw one.
        std::unique_ptr<Match> tk = Fresh("HUNTER");
        TA::SetDeck(*tk, 0, {"CS2_029", "CS2_024", "CS2_172", "CS2_168"});
        CHECK(Cast(*tk, "DS1_184") == ActionError::None);
        MatchView const v = tk->ViewFor(0, 0);
        CHECK(v.pickPending && v.pick.size() == 3);
        CHECK(tk->Apply(0, Choose(v.pick[2].id)) == ActionError::None && Hand(*tk, 0) == 1);
    }

    // ---- Paladin --------------------------------------------------------------------

    void TestPaladin()
    {
        // Blessing of Might: +3 attack (minions only).
        std::unique_ptr<Match> m = Fresh("PALADIN");
        std::vector<EntityId> o = TA::SetBoard(*m, 0, {"CS2_168"});
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_182"});
        CHECK(Cast(*m, "CS2_087", HeroId(*m, 0)) == ActionError::Target);
        CHECK(Cast(*m, "CS2_087", o[0]) == ActionError::None && MV(*m, o[0]).attack == 5 && MV(*m, o[0]).health == 1);
        CHECK(Cast(*m, "CS2_087", e[0]) == ActionError::None && MV(*m, e[0]).attack == 7);

        // Blessing of Kings: +4/+4 keeping damage.
        std::unique_ptr<Match> bk = Fresh("PALADIN");
        std::vector<EntityId> bo = TA::SetBoard(*bk, 0, {"CS2_182"});
        TA::SetMinion(*bk, bo[0], 4, 3, 5);
        CHECK(Cast(*bk, "CS2_092", bo[0]) == ActionError::None);
        CHECK(MV(*bk, bo[0]).attack == 8 && MV(*bk, bo[0]).health == 7 && MV(*bk, bo[0]).maxHealth == 9);

        // Guardian of Kings: Battlecry: restore 6 to your hero (capped at its maximum).
        std::unique_ptr<Match> gk = Fresh("PALADIN");
        TA::SetHero(*gk, 0, 20, 0);
        std::vector<EntityId> gh = TA::SetHand(*gk, 0, {"CS2_088", "CS2_088"});
        CHECK(Do0(*gk, Play(gh[0])) == ActionError::None && Hp(*gk, 0) == 26);
        CHECK(Do0(*gk, Play(gh[1])) == ActionError::None && Hp(*gk, 0) == 30);

        // Holy Light: restore 6 (Spell Damage does not change healing).
        std::unique_ptr<Match> hl = Fresh("PALADIN");
        TA::SetBoard(*hl, 0, {"CS2_142"});
        TA::SetHero(*hl, 0, 10, 0);
        CHECK(Cast(*hl, "CS2_089", HeroId(*hl, 0)) == ActionError::None && Hp(*hl, 0) == 16);
        std::vector<EntityId> hlo = TA::SetBoard(*hl, 0, {"CS2_200"});
        TA::SetMinion(*hl, hlo[0], 6, 1, 7);
        CHECK(Cast(*hl, "CS2_089", hlo[0]) == ActionError::None && MV(*hl, hlo[0]).health == 7);
        CHECK(Cast(*hl, "CS2_089", 0) == ActionError::Target);

        // Consecration: 2 damage to all enemies (heroes and minions), none to friends.
        std::unique_ptr<Match> cs = Fresh("PALADIN");
        std::vector<EntityId> co = TA::SetBoard(*cs, 0, {"CS2_182"});
        std::vector<EntityId> ce = TA::SetBoard(*cs, 1, {"CS2_182", "CS2_168"});
        CHECK(Cast(*cs, "CS2_093") == ActionError::None);
        CHECK(Hp(*cs, 1) == 28 && Hp(*cs, 0) == 30 && MV(*cs, co[0]).health == 5 && MV(*cs, ce[0]).health == 3 && !OnBoard(*cs, 1, ce[1]));

        // Hammer of Wrath: 3 damage and a card.
        std::unique_ptr<Match> hw = Fresh("PALADIN");
        CHECK(Cast(*hw, "CS2_094", HeroId(*hw, 1)) == ActionError::None && Hp(*hw, 1) == 27 && Hand(*hw, 0) == 1);

        // Truesilver Champion: whenever your hero attacks, restore 2 to it.
        std::unique_ptr<Match> ts = Fresh("PALADIN");
        TA::SetHero(*ts, 0, 20, 0);
        CHECK(Cast(*ts, "CS2_097") == ActionError::None);
        CHECK(HeroOfSide0(*ts).weaponAttack == 4 && HeroOfSide0(*ts).weaponDurability == 2);
        CHECK(ts->Apply(0, Attack(HeroId(*ts, 0), HeroId(*ts, 1))) == ActionError::None);
        CHECK(Hp(*ts, 1) == 26 && Hp(*ts, 0) == 22 && HeroOfSide0(*ts).weaponDurability == 1);
        // A minion's attack does not heal the hero.
        std::vector<EntityId> tm = TA::SetBoard(*ts, 0, {"CS2_182"});
        CHECK(ts->Apply(0, Attack(tm[0], HeroId(*ts, 1))) == ActionError::None && Hp(*ts, 0) == 22);

        // Humility: attack becomes 1 (also removes buffs).
        std::unique_ptr<Match> hu = Fresh("PALADIN");
        std::vector<EntityId> hb = TA::SetBoard(*hu, 1, {"CS2_200"});
        CHECK(Cast(*hu, "EX1_360", HeroId(*hu, 1)) == ActionError::Target);
        CHECK(Cast(*hu, "EX1_360", hb[0]) == ActionError::None && MV(*hu, hb[0]).attack == 1 && MV(*hu, hb[0]).health == 7);

        // Hand of Protection: Divine Shield absorbs the next damage.
        std::unique_ptr<Match> hp = Fresh("PALADIN");
        std::vector<EntityId> hpo = TA::SetBoard(*hp, 0, {"CS2_182"});
        CHECK(Cast(*hp, "EX1_371", hpo[0]) == ActionError::None && MV(*hp, hpo[0]).divineShield);
        CHECK(Cast(*hp, "CS2_029", hpo[0]) == ActionError::None);
        CHECK(MV(*hp, hpo[0]).health == 5 && !MV(*hp, hpo[0]).divineShield);

        // Reinforce-less weapon: Light's Justice is a plain 1/4.
        std::unique_ptr<Match> lj = Fresh("PALADIN");
        CHECK(Cast(*lj, "CS2_091") == ActionError::None);
        CHECK(lj->Apply(0, Attack(HeroId(*lj, 0), HeroId(*lj, 1))) == ActionError::None && Hp(*lj, 1) == 29);
    }

    // ---- Priest ---------------------------------------------------------------------

    void TestPriest()
    {
        // Holy Nova: 2 damage to all enemies, restore 2 to all friendly characters (Spell Damage: damage only).
        std::unique_ptr<Match> m = Fresh("PRIEST");
        std::vector<EntityId> o = TA::SetBoard(*m, 0, {"CS2_182", "CS2_168"});
        TA::SetMinion(*m, o[0], 4, 2, 5);
        TA::SetHero(*m, 0, 25, 0);
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_200"});
        CHECK(Cast(*m, "CS1_112") == ActionError::None);
        CHECK(Hp(*m, 1) == 28 && !OnBoard(*m, 1, e[0]) && MV(*m, e[1]).health == 5);
        CHECK(MV(*m, o[0]).health == 4 && Hp(*m, 0) == 27 && MV(*m, o[1]).health == 1);
        TA::SetBoard(*m, 0, {"CS2_142"});
        CHECK(Cast(*m, "CS1_112") == ActionError::None && Hp(*m, 1) == 25 && MV(*m, e[1]).health == 2);
        CHECK(Hp(*m, 0) == 29);

        // Holy Smite: 2 damage.
        std::unique_ptr<Match> hs = Fresh("PRIEST");
        CHECK(Cast(*hs, "CS1_130", HeroId(*hs, 1)) == ActionError::None && Hp(*hs, 1) == 28);

        // Mind Vision: a copy of a random card of the opponent's hand (the opponent keeps it).
        std::set<std::string> seen;
        for (uint32_t seed = 1; seed <= 30; ++seed)
        {
            std::unique_ptr<Match> mv = Fresh("PRIEST", "WARRIOR", seed);
            TA::SetHand(*mv, 1, {"CS2_029", "CS2_168"});
            CHECK(Cast(*mv, "CS2_003") == ActionError::None);
            CHECK(Hand(*mv, 0) == 1 && Hand(*mv, 1) == 2);
            seen.insert(TA::HandCardIds(*mv, 0)[0]);
        }
        CHECK(seen.size() == 2 && seen.count("CS2_029") && seen.count("CS2_168"));
        std::unique_ptr<Match> me = Fresh("PRIEST");
        CHECK(Cast(*me, "CS2_003") == ActionError::None && Hand(*me, 0) == 0 && Hand(*me, 1) == 0);

        // Power Word: Shield: +2 health and a card.
        std::unique_ptr<Match> pw = Fresh("PRIEST");
        std::vector<EntityId> po = TA::SetBoard(*pw, 0, {"CS2_168"});
        CHECK(Cast(*pw, "CS2_004", HeroId(*pw, 0)) == ActionError::Target);
        CHECK(Cast(*pw, "CS2_004", po[0]) == ActionError::None);
        CHECK(MV(*pw, po[0]).health == 3 && MV(*pw, po[0]).maxHealth == 3 && MV(*pw, po[0]).attack == 2 && Hand(*pw, 0) == 1);

        // Shadow Word: Pain: 3 or less attack.
        std::unique_ptr<Match> sp = Fresh("PRIEST");
        std::vector<EntityId> se = TA::SetBoard(*sp, 1, {"CS2_168", "CS2_172", "CS2_182"});
        CHECK(Cast(*sp, "CS2_234", se[2]) == ActionError::Target);
        CHECK(Cast(*sp, "CS2_234", HeroId(*sp, 1)) == ActionError::Target);
        CHECK(Cast(*sp, "CS2_234", se[1]) == ActionError::None && !OnBoard(*sp, 1, se[1]));
        TA::SetMinion(*sp, se[0], 4, 1, 1);
        CHECK(Cast(*sp, "CS2_234", se[0]) == ActionError::Target);

        // Shadow Word: Death: 5 or more attack.
        std::unique_ptr<Match> sd = Fresh("PRIEST");
        std::vector<EntityId> sde = TA::SetBoard(*sd, 1, {"CS2_200", "CS2_182", "CS2_168"});
        TA::SetMinion(*sd, sde[2], 5, 1, 1);
        CHECK(Cast(*sd, "EX1_622", sde[1]) == ActionError::Target);
        CHECK(Cast(*sd, "EX1_622", sde[0]) == ActionError::None && !OnBoard(*sd, 1, sde[0]));
        CHECK(Cast(*sd, "EX1_622", sde[2]) == ActionError::None && !OnBoard(*sd, 1, sde[2]));

        // Divine Spirit: doubles the (current) health.
        std::unique_ptr<Match> ds = Fresh("PRIEST");
        std::vector<EntityId> dso = TA::SetBoard(*ds, 0, {"CS2_182", "CS2_182"});
        TA::SetMinion(*ds, dso[1], 4, 2, 5);
        CHECK(Cast(*ds, "CS2_236", dso[0]) == ActionError::None);
        CHECK(MV(*ds, dso[0]).health == 10 && MV(*ds, dso[0]).maxHealth == 10);
        CHECK(Cast(*ds, "CS2_236", dso[1]) == ActionError::None);
        CHECK(MV(*ds, dso[1]).health == 4 && MV(*ds, dso[1]).maxHealth == 7);
        CHECK(Cast(*ds, "CS2_236", HeroId(*ds, 0)) == ActionError::Target);

        // Mind Blast: 5 damage to the enemy hero, no target.
        std::unique_ptr<Match> mb = Fresh("PRIEST");
        CHECK(Cast(*mb, "DS1_233", HeroId(*mb, 1)) == ActionError::Target);
        CHECK(Cast(*mb, "DS1_233") == ActionError::None && Hp(*mb, 1) == 25);
        TA::SetBoard(*mb, 0, {"CS2_142"});
        CHECK(Cast(*mb, "DS1_233") == ActionError::None && Hp(*mb, 1) == 19);

        // Mind Control: an enemy minion changes sides (it cannot attack yet); needs a free slot; enemies only.
        std::unique_ptr<Match> mc = Fresh("PRIEST");
        std::vector<EntityId> mco = TA::SetBoard(*mc, 0, {"CS2_168"});
        std::vector<EntityId> mce = TA::SetBoard(*mc, 1, {"CS2_200", "CS2_182"});
        CHECK(Cast(*mc, "CS1_113", mco[0]) == ActionError::Target);
        CHECK(Cast(*mc, "CS1_113", HeroId(*mc, 1)) == ActionError::Target);
        CHECK(Cast(*mc, "CS1_113", mce[0]) == ActionError::None);
        CHECK(OnBoard(*mc, 0, mce[0]) && !OnBoard(*mc, 1, mce[0]) && !MV(*mc, mce[0]).canAttack);
        TA::SetBoard(*mc, 0, Strs(7, "CS2_168"));
        CHECK(Cast(*mc, "CS1_113", mce[1]) == ActionError::BoardFull);

        // Northshire Cleric: draws whenever a minion is healed (not heroes, not for no healing).
        std::unique_ptr<Match> nc = Fresh("PRIEST");
        std::vector<EntityId> no = TA::SetBoard(*nc, 0, {"CS2_235", "CS2_182", "CS2_200"});
        TA::SetMinion(*nc, no[1], 4, 3, 5);
        TA::SetHero(*nc, 0, 20, 0);
        CHECK(Cast(*nc, "CS2_089", no[1]) == ActionError::None && Hand(*nc, 0) == 1);
        CHECK(Cast(*nc, "CS2_089", no[2]) == ActionError::None && Hand(*nc, 0) == 0);
        CHECK(Cast(*nc, "CS2_089", HeroId(*nc, 0)) == ActionError::None && Hand(*nc, 0) == 0 && Hp(*nc, 0) == 26);
        // An enemy Cleric draws for its owner when one of your minions is healed.
        TA::SetBoard(*nc, 1, {"CS2_235"});
        TA::SetMinion(*nc, no[1], 4, 1, 5);
        int const enemyHand = Hand(*nc, 1);
        CHECK(Cast(*nc, "CS2_089", no[1]) == ActionError::None);
        CHECK(Hand(*nc, 0) == 1 && Hand(*nc, 1) == enemyHand + 1);
    }

    // ---- Druid ----------------------------------------------------------------------

    void TestDruid()
    {
        // Claw: +2 attack this turn and 2 armor.
        std::unique_ptr<Match> m = Fresh("DRUID");
        CHECK(Cast(*m, "CS2_005") == ActionError::None);
        CHECK(HeroOfSide0(*m).attack == 2 && ArmorOf(*m, 0) == 2 && HeroOfSide0(*m).canAttack);
        m->Apply(0, End());
        m->Apply(1, End());
        CHECK(HeroOfSide0(*m).attack == 0);

        // Healing Touch: restore 8 (hero or minion).
        std::unique_ptr<Match> ht = Fresh("DRUID");
        TA::SetHero(*ht, 0, 15, 0);
        CHECK(Cast(*ht, "CS2_007", HeroId(*ht, 0)) == ActionError::None && Hp(*ht, 0) == 23);
        std::vector<EntityId> ho = TA::SetBoard(*ht, 0, {"CS2_200"});
        TA::SetMinion(*ht, ho[0], 6, 1, 7);
        CHECK(Cast(*ht, "CS2_007", ho[0]) == ActionError::None && MV(*ht, ho[0]).health == 7);
        TA::SetHero(*ht, 1, 5, 0);
        CHECK(Cast(*ht, "CS2_007", HeroId(*ht, 1)) == ActionError::None && Hp(*ht, 1) == 13);

        // Moonfire: 1 damage (2 with Spell Damage).
        std::unique_ptr<Match> mf = Fresh("DRUID");
        CHECK(Cast(*mf, "CS2_008", HeroId(*mf, 1)) == ActionError::None && Hp(*mf, 1) == 29);
        TA::SetBoard(*mf, 0, {"CS2_142"});
        CHECK(Cast(*mf, "CS2_008", HeroId(*mf, 1)) == ActionError::None && Hp(*mf, 1) == 27);

        // Mark of the Wild: Taunt and +2/+2.
        std::unique_ptr<Match> mw = Fresh("DRUID");
        std::vector<EntityId> mo = TA::SetBoard(*mw, 0, {"CS2_168"});
        CHECK(Cast(*mw, "CS2_009", HeroId(*mw, 0)) == ActionError::Target);
        CHECK(Cast(*mw, "CS2_009", mo[0]) == ActionError::None);
        CHECK(MV(*mw, mo[0]).attack == 4 && MV(*mw, mo[0]).health == 3 && MV(*mw, mo[0]).taunt);

        // Savage Roar: +2 attack this turn for all your characters.
        std::unique_ptr<Match> sr = Fresh("DRUID");
        std::vector<EntityId> sro = TA::SetBoard(*sr, 0, {"CS2_168", "CS2_200"});
        std::vector<EntityId> sre = TA::SetBoard(*sr, 1, {"CS2_168"});
        CHECK(Cast(*sr, "CS2_011") == ActionError::None);
        CHECK(MV(*sr, sro[0]).attack == 4 && MV(*sr, sro[1]).attack == 8 && MV(*sr, sre[0]).attack == 2);
        CHECK(HeroOfSide0(*sr).attack == 2 && HeroOfSide0(*sr).canAttack);
        CHECK(sr->ViewFor(0, 0).other.hero.attack == 0);
        sr->Apply(0, End());
        CHECK(MV(*sr, sro[0]).attack == 2 && MV(*sr, sro[1]).attack == 6);
        sr->Apply(1, End());
        CHECK(HeroOfSide0(*sr).attack == 0);

        // Swipe: 4 damage to an enemy, 1 to all other enemies; Spell Damage adds to both.
        std::unique_ptr<Match> sw = Fresh("DRUID");
        std::vector<EntityId> swo = TA::SetBoard(*sw, 0, {"CS2_200"});
        std::vector<EntityId> swe = TA::SetBoard(*sw, 1, {"CS2_200", "CS2_182", "CS2_168"});
        CHECK(Cast(*sw, "CS2_012", swo[0]) == ActionError::Target);
        CHECK(Cast(*sw, "CS2_012", HeroId(*sw, 0)) == ActionError::Target);
        CHECK(Cast(*sw, "CS2_012", 0) == ActionError::Target);
        CHECK(Cast(*sw, "CS2_012", swe[0]) == ActionError::None);
        CHECK(MV(*sw, swe[0]).health == 3 && MV(*sw, swe[1]).health == 4 && !OnBoard(*sw, 1, swe[2]) && Hp(*sw, 1) == 29);
        CHECK(MV(*sw, swo[0]).health == 7 && Hp(*sw, 0) == 30);
        CHECK(Cast(*sw, "CS2_012", HeroId(*sw, 1)) == ActionError::None);
        CHECK(Hp(*sw, 1) == 25 && MV(*sw, swe[0]).health == 2 && MV(*sw, swe[1]).health == 3);
        TA::SetBoard(*sw, 0, {"CS2_142"});
        CHECK(Cast(*sw, "CS2_012", HeroId(*sw, 1)) == ActionError::None);
        CHECK(Hp(*sw, 1) == 20 && !OnBoard(*sw, 1, swe[0]));
        CHECK(MV(*sw, swe[1]).health == 1);

        // Innervate: 2 mana this turn (never above 10).
        std::unique_ptr<Match> in = Fresh("DRUID");
        TA::SetMana(*in, 0, 3, 10);
        std::vector<EntityId> ih = TA::SetHand(*in, 0, {"EX1_169"});
        CHECK(in->Apply(0, Play(ih[0])) == ActionError::None && ManaOf(*in, 0) == 5);
        TA::SetMana(*in, 0, 9, 10);
        ih = TA::SetHand(*in, 0, {"EX1_169"});
        CHECK(in->Apply(0, Play(ih[0])) == ActionError::None && ManaOf(*in, 0) == 10);
        TA::SetMana(*in, 0, 0, 1);
        ih = TA::SetHand(*in, 0, {"EX1_169", "CS2_008"});
        CHECK(in->Apply(0, Play(ih[0])) == ActionError::None && ManaOf(*in, 0) == 2);
        CHECK(in->Apply(0, Play(ih[1], HeroId(*in, 1))) == ActionError::None && ManaOf(*in, 0) == 2);

        // Starfire: 5 damage and a card.
        std::unique_ptr<Match> sf = Fresh("DRUID");
        CHECK(Cast(*sf, "EX1_173", HeroId(*sf, 1)) == ActionError::None && Hp(*sf, 1) == 25 && Hand(*sf, 0) == 1);

        // Wild Growth: an empty crystal (Excess Mana at 10).
        std::unique_ptr<Match> wg = Fresh("DRUID");
        TA::SetMana(*wg, 0, 4, 4);
        std::vector<EntityId> wh = TA::SetHand(*wg, 0, {"CS2_013"});
        CHECK(wg->Apply(0, Play(wh[0])) == ActionError::None);
        CHECK(wg->ViewFor(0, 0).self.hero.maxMana == 5 && ManaOf(*wg, 0) == 2);
    }

    // ---- Neutral ---------------------------------------------------------------------

    void TestNeutralBattlecries()
    {
        // Elven Archer, Ironforge Rifleman (1 damage) and Stormpike Commando (2): a target is required, never itself.
        struct B { char const* id; int damage; };
        for (B const& b : {B{"CS2_189", 1}, B{"CS2_141", 1}, B{"CS2_150", 2}})
        {
            std::unique_ptr<Match> m = Fresh();
            std::vector<EntityId> o = TA::SetBoard(*m, 0, {"CS2_200"});
            std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_200"});
            std::vector<EntityId> h = TA::SetHand(*m, 0, {b.id, b.id, b.id});
            CHECK(Do0(*m, Play(h[0], 0)) == ActionError::Target);
            CHECK(Do0(*m, Play(h[0], HeroId(*m, 1))) == ActionError::None && Hp(*m, 1) == 30 - b.damage);
            CHECK(Do0(*m, Play(h[1], e[0])) == ActionError::None && MV(*m, e[0]).health == 7 - b.damage);
            CHECK(Do0(*m, Play(h[2], o[0])) == ActionError::None && MV(*m, o[0]).health == 7 - b.damage);
            // Stealthed enemies cannot be chosen; with an empty board the hero is still a target.
            std::unique_ptr<Match> n = Fresh();
            std::vector<EntityId> nh = TA::SetHand(*n, 0, {b.id});
            CHECK(Do0(*n, Play(nh[0], 0)) == ActionError::Target);
            CHECK(Do0(*n, Play(nh[0], HeroId(*n, 0))) == ActionError::None && Hp(*n, 0) == 30 - b.damage);
        }

        // Voodoo Doctor: restore 2 (hero or minion), a target is required.
        std::unique_ptr<Match> vd = Fresh();
        TA::SetHero(*vd, 0, 20, 0);
        std::vector<EntityId> vo = TA::SetBoard(*vd, 0, {"CS2_200"});
        TA::SetMinion(*vd, vo[0], 6, 1, 7);
        std::vector<EntityId> vh = TA::SetHand(*vd, 0, {"EX1_011", "EX1_011", "EX1_011"});
        CHECK(Do0(*vd, Play(vh[0], 0)) == ActionError::Target);
        CHECK(Do0(*vd, Play(vh[0], HeroId(*vd, 0))) == ActionError::None && Hp(*vd, 0) == 22);
        CHECK(Do0(*vd, Play(vh[1], vo[0])) == ActionError::None && MV(*vd, vo[0]).health == 3);
        CHECK(Do0(*vd, Play(vh[2], HeroId(*vd, 1))) == ActionError::None && Hp(*vd, 1) == 30);

        // Gnomish Inventor and Novice Engineer draw a card; Nightblade hits the hero for 3.
        std::unique_ptr<Match> gi = Fresh();
        std::vector<EntityId> gh = TA::SetHand(*gi, 0, {"CS2_147", "EX1_015", "EX1_593"});
        for (EntityId id : gh)
            CHECK(Do0(*gi, Play(id)) == ActionError::None);
        CHECK(Hand(*gi, 0) == 2 && Hp(*gi, 1) == 27);

        // Razorfen Hunter: a Boar right of the hunter (slot 0 played: hunter, boar, ...); none when the board is full.
        std::unique_ptr<Match> rh = Fresh();
        TA::SetBoard(*rh, 0, {"CS2_168"});
        std::vector<EntityId> rhh = TA::SetHand(*rh, 0, {"CS2_196"});
        CHECK(Do0(*rh, Play(rhh[0], 0, 0, 0)) == ActionError::None);
        CHECK(TA::BoardCardIds(*rh, 0) == Strs({"CS2_196", "CS2_boar", "CS2_168"}));
        std::unique_ptr<Match> rf = Fresh();
        TA::SetBoard(*rf, 0, Strs(6, "CS2_168"));
        std::vector<EntityId> rfh = TA::SetHand(*rf, 0, {"CS2_196"});
        CHECK(Do0(*rf, Play(rfh[0])) == ActionError::None && TA::BoardIds(*rf, 0).size() == 7 && BoardCount(*rf, 0, "CS2_boar") == 0);

        // Murloc Tidehunter and Dragonling Mechanic summon their tokens to their right.
        std::unique_ptr<Match> mt = Fresh();
        std::vector<EntityId> mth = TA::SetHand(*mt, 0, {"EX1_506", "EX1_025"});
        CHECK(Do0(*mt, Play(mth[0])) == ActionError::None);
        CHECK(Do0(*mt, Play(mth[1])) == ActionError::None);
        CHECK(TA::BoardCardIds(*mt, 0) == Strs({"EX1_506", "EX1_506a", "EX1_025", "EX1_025t"}));
        std::vector<EntityId> mtb = TA::BoardIds(*mt, 0);
        CHECK(MV(*mt, mtb[1]).attack == 1 && MV(*mt, mtb[1]).health == 1 && MV(*mt, mtb[3]).attack == 2 && MV(*mt, mtb[3]).health == 1);

        // Frostwolf Warlord: +1/+1 for each other friendly minion (enemy minions do not count).
        for (int others = 0; others <= 6; ++others)
        {
            std::unique_ptr<Match> fw = Fresh();
            TA::SetBoard(*fw, 0, Strs(static_cast<std::size_t>(others), "CS2_168"));
            TA::SetBoard(*fw, 1, Strs(3, "CS2_168"));
            std::vector<EntityId> fh = TA::SetHand(*fw, 0, {"CS2_226"});
            CHECK(Do0(*fw, Play(fh[0], 0, 0, others > 0 ? others / 2 : -1)) == ActionError::None);
            EntityId warlord = 0;
            for (EntityId id : TA::BoardIds(*fw, 0))
                if (MV(*fw, id).attack >= 4 && MV(*fw, id).maxHealth >= 4)
                    warlord = id;
            CHECK(warlord != 0 && MV(*fw, warlord).attack == 4 + others && MV(*fw, warlord).health == 4 + others);
        }
        // With Stormwind Champion's aura the buff counts the Champion too (and the aura adds on top).
        std::unique_ptr<Match> fc = Fresh();
        TA::SetBoard(*fc, 0, {"CS2_222", "CS2_168"});
        std::vector<EntityId> fch = TA::SetHand(*fc, 0, {"CS2_226"});
        CHECK(Do0(*fc, Play(fch[0])) == ActionError::None);
        MinionView const wl = MV(*fc, TA::BoardIds(*fc, 0).back());
        CHECK(wl.attack == 7 && wl.health == 7);

        // Darkscale Healer: restore 2 to all friendly characters.
        std::unique_ptr<Match> dh = Fresh();
        TA::SetHero(*dh, 0, 20, 0);
        TA::SetHero(*dh, 1, 20, 0);
        std::vector<EntityId> dho = TA::SetBoard(*dh, 0, {"CS2_200", "CS2_168"});
        std::vector<EntityId> dhe = TA::SetBoard(*dh, 1, {"CS2_200"});
        TA::SetMinion(*dh, dho[0], 6, 1, 7);
        TA::SetMinion(*dh, dhe[0], 6, 1, 7);
        std::vector<EntityId> dhh = TA::SetHand(*dh, 0, {"DS1_055"});
        CHECK(Do0(*dh, Play(dhh[0])) == ActionError::None);
        CHECK(Hp(*dh, 0) == 22 && Hp(*dh, 1) == 20 && MV(*dh, dho[0]).health == 3 && MV(*dh, dhe[0]).health == 1);

        // Shattered Sun Cleric: +1/+1 for a friendly minion; optional only without one.
        std::unique_ptr<Match> sc = Fresh();
        std::vector<EntityId> sce = TA::SetBoard(*sc, 1, {"CS2_200"});
        std::vector<EntityId> sch = TA::SetHand(*sc, 0, {"EX1_019", "EX1_019"});
        CHECK(Do0(*sc, Play(sch[0], sce[0])) == ActionError::Target);
        CHECK(Do0(*sc, Play(sch[0], 0)) == ActionError::None);
        std::vector<EntityId> sco = TA::BoardIds(*sc, 0);
        CHECK(Do0(*sc, Play(sch[1], 0)) == ActionError::Target);
        CHECK(Do0(*sc, Play(sch[1], HeroId(*sc, 0))) == ActionError::Target);
        CHECK(Do0(*sc, Play(sch[1], sco[0])) == ActionError::None);
        CHECK(MV(*sc, sco[0]).attack == 4 && MV(*sc, sco[0]).health == 3);

        // Acidic Swamp Ooze: destroys the enemy weapon (nothing to destroy is fine, own weapon stays).
        std::unique_ptr<Match> oz = Fresh();
        TA::SetWeapon(*oz, 0, "CS2_106");
        std::vector<EntityId> oh = TA::SetHand(*oz, 0, {"EX1_066", "EX1_066"});
        CHECK(Do0(*oz, Play(oh[0])) == ActionError::None);
        CHECK(HeroOfSide0(*oz).weaponAttack == 3);
        TA::SetWeapon(*oz, 1, "CS2_112");
        CHECK(oz->ViewFor(0, 0).other.hero.weaponAttack == 5);
        uint32_t const since = oz->LastEventSeq();
        CHECK(Do0(*oz, Play(oh[1])) == ActionError::None);
        CHECK(oz->ViewFor(0, 0).other.hero.weaponAttack == 0 && CountEvents(*oz, EventType::WeaponBreak, since) == 1);
        CHECK(HeroOfSide0(*oz).weaponAttack == 3);
    }

    void TestNeutralMinions()
    {
        // Gurubashi Berserker: +3 attack whenever it takes damage.
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> g = TA::SetBoard(*m, 0, {"EX1_399"});
        CHECK(Cast(*m, "CS2_008", g[0]) == ActionError::None);
        CHECK(MV(*m, g[0]).attack == 5 && MV(*m, g[0]).health == 6);
        CHECK(Cast(*m, "CS2_008", g[0]) == ActionError::None);
        CHECK(MV(*m, g[0]).attack == 8 && MV(*m, g[0]).health == 5);
        CHECK(Cast(*m, "EX1_400") == ActionError::None);
        CHECK(MV(*m, g[0]).attack == 11 && MV(*m, g[0]).health == 4);
        // Fighting back counts as damage too.
        std::vector<EntityId> en = TA::SetBoard(*m, 1, {"CS2_168"});
        CHECK(m->Apply(0, Attack(g[0], en[0])) == ActionError::None);
        CHECK(MV(*m, g[0]).attack == 14 && MV(*m, g[0]).health == 2);

        // Grimscale Oracle: ALL other Murlocs +1 attack (both sides, not itself, not other races).
        std::unique_ptr<Match> go = Fresh();
        std::vector<EntityId> o = TA::SetBoard(*go, 0, {"EX1_508", "CS2_168", "CS2_172"});
        std::vector<EntityId> e = TA::SetBoard(*go, 1, {"CS2_168"});
        CHECK(MV(*go, o[0]).attack == 1 && MV(*go, o[1]).attack == 3 && MV(*go, o[2]).attack == 3 && MV(*go, e[0]).attack == 3);
        std::vector<EntityId> two = TA::SetBoard(*go, 0, {"EX1_508", "EX1_508"});
        CHECK(MV(*go, two[0]).attack == 2 && MV(*go, two[1]).attack == 2 && MV(*go, e[0]).attack == 4);

        // Stormwind Champion and Raid Leader: auras for the other minions, gone with the source.
        std::unique_ptr<Match> au = Fresh();
        std::vector<EntityId> ab = TA::SetBoard(*au, 0, {"CS2_222", "CS2_122", "CS2_168"});
        CHECK(MV(*au, ab[0]).attack == 7 && MV(*au, ab[0]).health == 6);
        CHECK(MV(*au, ab[1]).attack == 3 && MV(*au, ab[1]).health == 3);
        CHECK(MV(*au, ab[2]).attack == 4 && MV(*au, ab[2]).health == 2);
        CHECK(Cast(*au, "CS2_057", ab[0]) == ActionError::None);
        TA::SetMinion(*au, ab[0], 6, 1, 6);
        CHECK(Cast(*au, "CS2_057", ab[0]) == ActionError::None);
        CHECK(!OnBoard(*au, 0, ab[0]) && MV(*au, ab[2]).attack == 3 && MV(*au, ab[2]).health == 1);
    }

    // ---- cross-card interactions --------------------------------------------------------

    void TestInteractions()
    {
        // Northshire Cleric + Holy Nova: a card for every healed minion (not for the hero, not for the undamaged).
        std::unique_ptr<Match> m = Fresh("PRIEST");
        std::vector<EntityId> o = TA::SetBoard(*m, 0, {"CS2_235", "CS2_182", "CS2_200", "CS2_168"});
        TA::SetMinion(*m, o[1], 4, 3, 5);
        TA::SetMinion(*m, o[2], 6, 6, 7);
        TA::SetHero(*m, 0, 20, 0);
        TA::SetBoard(*m, 1, {"CS2_200"});
        CHECK(Cast(*m, "CS1_112") == ActionError::None);
        CHECK(Hand(*m, 0) == 2 && Hp(*m, 0) == 22 && Hp(*m, 1) == 28);
        // The Cleric itself, damaged and healed in the same Nova, draws once more.
        std::unique_ptr<Match> n = Fresh("PRIEST");
        std::vector<EntityId> c = TA::SetBoard(*n, 0, {"CS2_235"});
        TA::SetMinion(*n, c[0], 1, 1, 3);
        CHECK(Cast(*n, "CS1_112") == ActionError::None && Hand(*n, 0) == 1 && MV(*n, c[0]).health == 3);

        // Tundra Rhino + Starving Buzzard + Animal Companion: the companion is a Beast, so the Buzzard draws,
        // and it has Charge at once.
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            std::unique_ptr<Match> t = Fresh("HUNTER", "WARRIOR", seed);
            TA::SetBoard(*t, 0, {"DS1_178", "CS2_237"});
            CHECK(Cast(*t, "NEW1_031") == ActionError::None);
            std::vector<EntityId> b = TA::BoardIds(*t, 0);
            CHECK(b.size() == 3 && Hand(*t, 0) == 1);
            MinionView const comp = MV(*t, b[2]);
            CHECK(comp.charge && comp.canAttack);
            // Leokk also buffs Rhino (5 -> 3 attack... 2+1) and the Buzzard.
            if (TA::BoardCardIds(*t, 0)[2] == "NEW1_033")
                CHECK(MV(*t, b[0]).attack == 3 && MV(*t, b[1]).attack == 3 && comp.attack == 2);
        }
        // Silencing the Rhino takes Charge away from the Beasts (Ironbeak Owl).
        std::unique_ptr<Match> sr = Fresh("HUNTER");
        std::vector<EntityId> rb = TA::SetHand(*sr, 0, {"DS1_178"});
        CHECK(Do0(*sr, Play(rb[0])) == ActionError::None);
        std::vector<EntityId> rhino = TA::BoardIds(*sr, 0);
        CHECK(MV(*sr, rhino[0]).canAttack);
        CHECK(Cast(*sr, "CS2_203", rhino[0]) == ActionError::None);
        CHECK(!MV(*sr, rhino[0]).canAttack && !MV(*sr, rhino[0]).charge);

        // Vanish with full hands: the minions that find no room are destroyed.
        std::unique_ptr<Match> v = Fresh("ROGUE");
        TA::SetBoard(*v, 0, {"CS2_182", "CS2_168"});
        TA::SetBoard(*v, 1, {"CS2_200"});
        TA::SetHand(*v, 1, Strs(10, "CS2_168"));
        Strs hand(9, "CS2_168");
        hand.push_back("NEW1_004");
        CHECK(CastHand(*v, hand, "NEW1_004") == ActionError::None);
        CHECK(TA::BoardIds(*v, 0).empty() && TA::BoardIds(*v, 1).empty() && Hand(*v, 0) == 10 && Hand(*v, 1) == 10);

        // Mind Vision with an empty enemy hand does nothing (and is not refused).
        std::unique_ptr<Match> mv = Fresh("PRIEST");
        TA::SetHand(*mv, 1, {});
        CHECK(Cast(*mv, "CS2_003") == ActionError::None && Hand(*mv, 0) == 0);

        // Flametongue adjacency after a minion dies (also an enemy-side totem and Whirlwind kills).
        std::unique_ptr<Match> ft = Fresh("WARRIOR", "SHAMAN");
        std::vector<EntityId> fb = TA::SetBoard(*ft, 1, {"CS2_168", "CS2_168", "EX1_565", "CS2_200"});
        CHECK(MV(*ft, fb[1]).attack == 4 && MV(*ft, fb[3]).attack == 8 && MV(*ft, fb[0]).attack == 2);
        CHECK(Cast(*ft, "EX1_400") == ActionError::None);
        // The two Raiders die; the Ogre (adjacent to the totem) keeps its bonus, the totem takes 1.
        CHECK(TA::BoardIds(*ft, 1).size() == 2 && MV(*ft, fb[3]).attack == 8);
        // Two minions die, the survivor moves next to the totem and gets its bonus.
        std::vector<EntityId> fb2 = TA::SetBoard(*ft, 1, {"CS2_168", "EX1_565", "CS2_200", "CS2_182"});
        TA::SetMinion(*ft, fb2[2], 6, 1, 7);
        CHECK(MV(*ft, fb2[0]).attack == 4 && MV(*ft, fb2[3]).attack == 4);
        CHECK(Cast(*ft, "EX1_400") == ActionError::None);
        CHECK(TA::BoardCardIds(*ft, 1) == Strs({"EX1_565", "CS2_182"}));
        CHECK(MV(*ft, fb2[3]).attack == 6 && MV(*ft, fb2[3]).health == 4);

        // Frostwolf Warlord counts minions that died in the same turn correctly (only living ones).
        std::unique_ptr<Match> fw = Fresh("WARRIOR");
        TA::SetBoard(*fw, 0, Strs(3, "CS2_168"));
        CHECK(Cast(*fw, "EX1_400") == ActionError::None && TA::BoardIds(*fw, 0).empty());
        std::vector<EntityId> fh = TA::SetHand(*fw, 0, {"CS2_226"});
        CHECK(Do0(*fw, Play(fh[0])) == ActionError::None);
        CHECK(MV(*fw, TA::BoardIds(*fw, 0)[0]).attack == 4);

        // Spell Damage never changes healing, and a Truesilver hero heals before the hit lands.
        std::unique_ptr<Match> sp = Fresh("PALADIN");
        TA::SetBoard(*sp, 0, {"CS2_142", "CS2_155"});
        TA::SetHero(*sp, 0, 20, 0);
        CHECK(Cast(*sp, "CS2_089", HeroId(*sp, 0)) == ActionError::None && Hp(*sp, 0) == 26);
        CHECK(Cast(*sp, "CS2_093") == ActionError::None && Hp(*sp, 1) == 26);

        // Hunter's Mark + Execute-free kill chain: a marked minion (health 1) dies to Moonfire.
        std::unique_ptr<Match> hm = Fresh("HUNTER");
        std::vector<EntityId> he = TA::SetBoard(*hm, 1, {"CS2_200"});
        CHECK(Cast(*hm, "CS2_084", he[0]) == ActionError::None);
        CHECK(Cast(*hm, "CS2_008", he[0]) == ActionError::None && !OnBoard(*hm, 1, he[0]));

        // Savage Roar plus a weapon: the hero attacks with weapon + 2.
        std::unique_ptr<Match> sv = Fresh("DRUID");
        TA::SetWeapon(*sv, 0, "CS2_106");
        CHECK(Cast(*sv, "CS2_011") == ActionError::None && HeroOfSide0(*sv).attack == 5);
        CHECK(sv->Apply(0, Attack(HeroId(*sv, 0), HeroId(*sv, 1))) == ActionError::None && Hp(*sv, 1) == 25);

        // Hex on a buffed, damaged minion: a fresh Frog; Divine Spirit then doubles its health.
        std::unique_ptr<Match> hx = Fresh("SHAMAN");
        std::vector<EntityId> hb = TA::SetBoard(*hx, 1, {"CS2_182"});
        CHECK(Cast(*hx, "CS2_092", hb[0]) == ActionError::None);
        CHECK(Cast(*hx, "EX1_246", hb[0]) == ActionError::None);
        CHECK(MV(*hx, hb[0]).attack == 0 && MV(*hx, hb[0]).health == 1 && MV(*hx, hb[0]).maxHealth == 1);
        CHECK(Cast(*hx, "CS2_236", hb[0]) == ActionError::None && MV(*hx, hb[0]).health == 2);
    }

    // ---- randomized robustness ------------------------------------------------------------

    struct Pool
    {
        std::vector<std::string> byClass[9];
        std::vector<std::string> neutral;
    };

    char const* const kClasses[9] = {"MAGE", "WARRIOR", "SHAMAN", "ROGUE", "PALADIN", "HUNTER", "DRUID", "WARLOCK", "PRIEST"};

    Pool BuildPool()
    {
        Pool p;
        for (char const* id : kCoreIds)
        {
            CardDef const* def = Db().Find(id);
            if (!def)
                continue;
            if (def->cardClass == "NEUTRAL")
                p.neutral.push_back(id);
            else
                for (int c = 0; c < 9; ++c)
                    if (def->cardClass == kClasses[c])
                        p.byClass[c].push_back(id);
        }
        return p;
    }

    std::vector<std::string> RandomDeck(Pool const& pool, int cls, std::mt19937& rng)
    {
        std::vector<std::string> cards = pool.neutral;
        cards.insert(cards.end(), pool.byClass[cls].begin(), pool.byClass[cls].end());
        std::vector<std::string> deck;
        std::vector<std::string> counted;
        while (deck.size() < 30)
        {
            std::string const& id = cards[rng() % cards.size()];
            int copies = 0;
            for (std::string const& d : deck)
                copies += d == id;
            if (copies < 2)
                deck.push_back(id);
        }
        return deck;
    }

    // Plays one random game; returns the number of actions applied. Every legal action must be accepted.
    int RandomGame(Pool const& pool, uint32_t seed, bool probeAll, int& probed, std::set<std::string>& played)
    {
        std::mt19937 rng(seed * 2654435761u + 17u);
        int const c0 = static_cast<int>(rng() % 9);
        int const c1 = static_cast<int>(rng() % 9);
        MatchSetup s;
        s.seed = seed;
        s.sides[0].name = "A";
        s.sides[0].heroClass = kClasses[c0];
        s.sides[0].deck = RandomDeck(pool, c0, rng);
        s.sides[1].name = "B";
        s.sides[1].heroClass = kClasses[c1];
        s.sides[1].deck = RandomDeck(pool, c1, rng);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        CHECK(m != nullptr);
        if (!m)
        {
            std::printf("  create failed: %s\n", error.c_str());
            return 0;
        }
        for (int side = 0; side < 2; ++side)
        {
            MatchView const v = m->ViewFor(side, 0);
            Action mull;
            mull.type = ActionType::Mulligan;
            for (EntityId id : v.mulligan)
                if (rng() % 3 == 0)
                    mull.replace.push_back(id);
            CHECK(m->Apply(side, mull) == ActionError::None);
        }
        int actions = 0;
        auto invariants = [&]() {
            for (int side = 0; side < 2; ++side)
            {
                MatchView const v = m->ViewFor(side, m->LastEventSeq());
                CHECK(v.self.board.size() <= static_cast<std::size_t>(kMaxBoard));
                CHECK(v.self.handCount <= kMaxHand);
                CHECK(v.self.hero.mana <= kMaxMana && v.self.hero.maxMana <= kMaxMana);
                for (MinionView const& mv : v.self.board)
                    CHECK(mv.health > 0 && mv.health <= mv.maxHealth && mv.attack >= 0);
            }
        };
        while (actions < 200 && m->GetPhase() != Phase::Over)
        {
            int side = m->ActiveSide();
            for (int sd = 0; sd < 2; ++sd)
                if (m->ViewFor(sd, m->LastEventSeq()).pickPending)
                    side = sd;
            MatchView const v = m->ViewFor(side, m->LastEventSeq());
            Action a;
            if (v.pickPending)
            {
                a = Choose(v.pick[rng() % v.pick.size()].id);
            }
            else
            {
                std::vector<Action> legal = v.legal;
                if (probeAll && actions % 3 == 0)
                {
                    // Every legal action must apply on a redacted copy (own information is complete there).
                    for (Action const& la : legal)
                    {
                        std::unique_ptr<Match> copy = m->RedactedCopy(side, seed + static_cast<uint32_t>(actions));
                        CHECK(copy != nullptr);
                        if (copy)
                        {
                            ActionError const e = copy->Apply(side, la);
                            if (e != ActionError::None)
                                std::printf("  seed %u action %d: legal action refused on the copy: %s\n", seed, actions,
                                    ActionErrorCode(e));
                            CHECK(e == ActionError::None);
                            ++probed;
                        }
                    }
                }
                legal.push_back(End());
                // Prefer playing something: EndTurn is chosen less often than a random legal action.
                a = legal[rng() % legal.size()];
                if (a.type == ActionType::EndTurn && legal.size() > 1 && rng() % 3 != 0)
                    a = legal[rng() % (legal.size() - 1)];
                if (a.type == ActionType::Play)
                    a.position = static_cast<int>(rng() % (v.self.board.size() + 2)) - 1;
            }
            uint32_t const eventsBefore = m->LastEventSeq();
            ActionError e = m->Apply(side, a);
            if (e == ActionError::Position)
            {
                a.position = -1;
                e = m->Apply(side, a);
            }
            if (e != ActionError::None)
                std::printf("  seed %u action %d: refused (%s)\n", seed, actions, ActionErrorCode(e));
            CHECK(e == ActionError::None);
            if (e != ActionError::None)
                return actions;
            ++actions;
            for (Event const& ev : m->ViewFor(side, eventsBefore).events)
                if (ev.type == EventType::Play && !ev.cardId.empty())
                    played.insert(ev.cardId);
            invariants();
        }
        // Run the match out: end turns until a hero dies or the turn limit draws it.
        for (int guard = 0; guard < 400 && m->GetPhase() != Phase::Over; ++guard)
        {
            int side = m->ActiveSide();
            for (int sd = 0; sd < 2; ++sd)
                if (m->ViewFor(sd, m->LastEventSeq()).pickPending)
                    side = sd;
            MatchView const v = m->ViewFor(side, m->LastEventSeq());
            ActionError const e = v.pickPending ? m->Apply(side, Choose(v.pick[0].id)) : m->Apply(side, End());
            CHECK(e == ActionError::None);
            if (e != ActionError::None)
                break;
        }
        CHECK(m->GetPhase() == Phase::Over && m->GetResult().over);
        return actions;
    }

    void TestRandomGames()
    {
        Pool const pool = BuildPool();
        int total = 0;
        int probed = 0;
        int const games = 150;
        std::set<std::string> played;
        for (int g = 0; g < games; ++g)
            total += RandomGame(pool, static_cast<uint32_t>(g + 1), g % 5 == 0, probed, played);
        std::printf("    %d random games, %d actions, %d legal actions probed on copies, %zu distinct cards played\n", games,
            total, probed, played.size());
        CHECK(played.size() >= 100);
        CHECK(total > games * 30);
        CHECK(probed > 100);
    }

    // Decks with every CORE class card and neutral card in turn: each card is played at least once somewhere.
    void TestEveryCardPlays()
    {
        int played = 0;
        for (char const* id : kCoreIds)
        {
            CardDef const* def = Db().Find(id);
            if (!def || (def->type != CardType::Spell && def->type != CardType::Minion && def->type != CardType::Weapon))
                continue;
            // A busy board and a weapon, so that every requirement can hold; side 0 plays the card at any legal target.
            std::unique_ptr<Match> m = Fresh();
            TA::SetBoard(*m, 0, kProbeBoard0);
            std::vector<EntityId> const b1 = TA::SetBoard(*m, 1, kProbeBoard1);
            TA::SetMinion(*m, b1[1], 4, 3, 5);
            TA::SetWeapon(*m, 0, "CS2_082");
            TA::SetMana(*m, 0, 10, 10);
            TA::SetHand(*m, 0, {id});
            bool found = false;
            for (Action const& a : m->LegalActions(0))
                if (a.type == ActionType::Play)
                {
                    CHECK(m->Apply(0, a) == ActionError::None);
                    found = true;
                    ++played;
                    break;
                }
            if (!found)
                std::printf("  no legal play for %s\n", id);
            CHECK(found);
        }
        CHECK(played > 120);
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    RUN(TestCoverage);
    RUN(TestTargetRulesMatchData);
    RUN(TestKeywordCards);
    RUN(TestMage);
    RUN(TestWarrior);
    RUN(TestShaman);
    RUN(TestWarlock);
    RUN(TestCorruptionFeedback);
    RUN(TestRogue);
    RUN(TestHunter);
    RUN(TestPaladin);
    RUN(TestPriest);
    RUN(TestDruid);
    RUN(TestNeutralBattlecries);
    RUN(TestNeutralMinions);
    RUN(TestInteractions);
    RUN(TestEveryCardPlays);
    RUN(TestRandomGames);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
