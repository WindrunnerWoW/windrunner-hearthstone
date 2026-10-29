// Standalone tests of the encounter-only cards: the cards and boss hero powers that the
// Normal encounters of wings 1-3 (Anub'Rekhan, Faerlina, Maexxna, Noth, Heigan, Loatheb, Razuvious, Gothik, The Four
// Horsemen) and wings 4-5 (Patchwerk, Grobbulus, Gluth, Thaddius, Sapphiron, Kel'Thuzad) need, plus the shared Deathcharger,
// Necroknight and Skeletal Smith (src/match/HsMatchCardsNaxx.cpp, engine in HsMatch.cpp):
//   - one or more scenarios per card and hero power (the boss hero powers are given to a side through
//     SideSetup::heroPowerId, like the encounter glue does), the engine features behind them (passive hero power, hero
//     Immune aura, hero Attack aura, left-most targeting, neighbours at death, double damage to heroes, summon for the
//     other side, take control of named minions) and that they are inert for ordinary matches;
//   - legal-action generation and the Hard AI with these hero powers, the redacted copy;
//   - wings 4-5: Hateful Strike and Hook, Poison Cloud, Decimate and Jaws, the automatic hero powers (Polarity Shift,
//     Frost Breath, Frost Blast), the Frozen Champion, and Kel'Thuzad's second phase (Armor broken in the middle of the
//     player's turn, the forced 11th turn, Chains, the Guardians, once only);
//   - a full Hard AI vs Hard AI play-out for each of the 15 encounters through ApplyEncounter, which must all be ready.
//   match_encounter_cards_tests <tests dir>   run all tests; exit code 0 = all checks passed.

#include "../src/match/HsEncounters.h"
#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "match_all_cards.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
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

    constexpr char const* kMurloc = "CS2_168";   // Murloc Raider 2/1
    constexpr char const* kYeti = "CS2_182";     // Chillwind Yeti 4/5
    constexpr char const* kFireball = "CS2_029";
    constexpr char const* kSilence = "EX1_332";
    constexpr char const* kAxe = "CS2_106";      // Fiery War Axe 3/2
    constexpr char const* kFrostbolt = "CS2_024";

    // A match in its play phase, side 0 active on turn 1 with 10 mana; both hands empty, both decks 20 Murloc Raiders,
    // boards empty. power0 / power1 / hp1 override the hero power of a side (the way an encounter does) and the health of
    // side 1.
    std::unique_ptr<Match> Fresh(char const* power0 = "", char const* power1 = "", uint32_t seed = 11, int hp1 = 0, int armor1 = 0)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = 0;
        s.sides[0].name = "Jonas";
        s.sides[0].heroClass = "MAGE";
        s.sides[0].heroPowerId = power0;
        s.sides[0].deck = std::vector<std::string>(20, kMurloc);
        s.sides[1].name = "Boss";
        s.sides[1].heroClass = "WARLOCK";
        s.sides[1].heroPowerId = power1;
        s.sides[1].heroHealth = hp1;
        s.sides[1].heroArmor = armor1;
        s.sides[1].deck = std::vector<std::string>(20, kMurloc);
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

    Action Play(EntityId source, EntityId target = 0)
    {
        Action a;
        a.type = ActionType::Play;
        a.source = source;
        a.target = target;
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

    Action Power(Match const& m, int side, EntityId target = 0)
    {
        Action a;
        a.type = ActionType::HeroPower;
        a.source = TA::PowerId(m, side);
        a.target = target;
        return a;
    }

    EntityId Hero(Match const& m, int side) { return TA::HeroId(m, side); }

    Strs Board(Match const& m, int side) { return TA::BoardCardIds(m, side); }
    Strs Hand(Match const& m, int side) { return TA::HandCardIds(m, side); }
    int HP(Match const& m, int side) { return m.HeroHealth(side); }

    HeroView HV(Match const& m, int side)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        return side == 0 ? v.self.hero : v.other.hero;
    }

    MinionView MV(Match const& m, EntityId id)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        for (MinionView const& x : v.self.board)
            if (x.id == id)
                return x;
        for (MinionView const& x : v.other.board)
            if (x.id == id)
                return x;
        return MinionView();
    }

    int CountOf(Strs const& v, char const* id) { return static_cast<int>(std::count(v.begin(), v.end(), std::string(id))); }

    void Mana(Match& m, int side = 0) { TA::SetMana(m, side, 10, 10); }

    // Puts exactly one card in the hand of side and plays it (the mana is topped up first).
    ActionError Cast(Match& m, int side, char const* cardId, EntityId target = 0)
    {
        Mana(m, side);
        Ids const h = TA::SetHand(m, side, {cardId});
        return m.Apply(side, Play(h[0], target));
    }

    // Uses the hero power of side (mana topped up, power refreshed).
    ActionError UsePower(Match& m, int side, EntityId target = 0)
    {
        Mana(m, side);
        TA::SetHeroPowerUsed(m, side, false);
        return m.Apply(side, Power(m, side, target));
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

    // Fireball (6 damage) by side on a target.
    ActionError Kill(Match& m, int side, EntityId target) { return Cast(m, side, kFireball, target); }

    int CountEvents(Match const& m, EventType type)
    {
        int n = 0;
        for (Event const& e : m.ViewFor(0, 0).events)
            if (e.type == type)
                ++n;
        return n;
    }

    bool LegalHasPower(Match const& m, int side)
    {
        for (Action const& a : m.LegalActions(side))
            if (a.type == ActionType::HeroPower)
                return true;
        return false;
    }

    // ---- coverage -------------------------------------------------------------------------------------

    char const* const kCards[] = {"FP1_006", "NAXM_001", "NAXM_002", "NAX1_04", "NAX1_05", "NAX2_03", "NAX2_05", "NAX3_02",
        "NAX3_03", "NAX4_04", "NAX4_05", "NAX5_02", "NAX5_03", "NAX6_02", "NAX6_03", "NAX6_04", "NAX7_03", "NAX7_04",
        "NAX7_05", "NAX8_02", "NAX8_03", "NAX8_04", "NAX8_05", "NAX9_02", "NAX9_03", "NAX9_04", "NAX9_05", "NAX9_06",
        "NAX9_07", "NAX10_02", "NAX10_03", "NAX11_02", "NAX11_04", "NAX12_02", "NAX12_03", "NAX12_04", "NAX13_02",
        "NAX13_03", "NAX14_02", "NAX14_03", "NAX14_04", "NAX15_02", "NAX15_04"};
    char const* const kTokens[] = {"NAX1_03", "NAX4_03", "NAX6_03t", "NAX7_02", "NAX8_03t", "NAX8_04t", "NAX8_05t", "NAX11_03",
        "NAX15_03n"};
    char const* const kWings123[] = {"naxx_anubrekhan", "naxx_faerlina", "naxx_maexxna", "naxx_noth", "naxx_heigan",
        "naxx_loatheb", "naxx_razuvious", "naxx_gothik", "naxx_horsemen"};
    char const* const kWings45[] = {"naxx_patchwerk", "naxx_grobbulus", "naxx_gluth", "naxx_thaddius", "naxx_sapphiron",
        "naxx_kelthuzad"};

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
            CHECK(Db().Supported(id));
        // Hero powers are hero powers; the passive one is flagged in its script.
        for (char const* id : {"NAX1_04", "NAX2_03", "NAX3_02", "NAX4_04", "NAX5_02", "NAX6_02", "NAX7_03", "NAX8_02", "NAX9_06"})
            CHECK(Db().Find(id) && Db().Find(id)->type == CardType::HeroPower);
        CHECK(Detail::FindScript("NAX4_04") && Detail::FindScript("NAX4_04")->passive);
        CHECK(!Detail::FindScript("NAX1_04")->passive);
        for (char const* id : {"NAX10_03", "NAX11_02", "NAX12_02", "NAX13_02", "NAX14_02", "NAX15_02", "NAX15_04"})
            CHECK(Db().Find(id) && Db().Find(id)->type == CardType::HeroPower);
        for (char const* id : {"NAX13_02", "NAX14_02", "NAX15_02"})
            CHECK(Detail::FindScript(id) && Detail::FindScript(id)->autoCast && !Detail::FindScript(id)->passive);
        for (char const* id : {"NAX10_03", "NAX11_02", "NAX12_02", "NAX15_04"})
            CHECK(!Detail::FindScript(id)->autoCast);
        CHECK(Db().Find("NAX10_02")->type == CardType::Weapon && Db().Find("NAX12_03")->type == CardType::Weapon);
        CHECK(Db().Find("FP1_006")->charge && Db().Find("FP1_006")->deathrattle);
        // All 15 encounters are ready (wings 1-3 since step E2a, wings 4-5 since step E2b).
        CHECK_EQ(Hs::Encounters().size(), 15u);
        for (Hs::Encounter const& e : Hs::Encounters())
            CHECK(Hs::EncounterReady(Db(), e));
        for (char const* key : kWings45)
        {
            Hs::Encounter const* e = Hs::FindEncounter(key);
            CHECK(e != nullptr);
            if (!e)
                continue;
            std::string const problem = Hs::CheckEncounter(Db(), *e, true);
            if (!problem.empty())
                std::printf("  %s: %s\n", key, problem.c_str());
            CHECK(Hs::EncounterReady(Db(), *e));
            CHECK(Hs::EncounterUnsupported(Db(), *e).empty());
        }
        for (char const* key : kWings123)
        {
            Hs::Encounter const* e = Hs::FindEncounter(key);
            CHECK(e != nullptr);
            if (!e)
                continue;
            std::string const problem = Hs::CheckEncounter(Db(), *e, true);
            if (!problem.empty())
                std::printf("  %s: %s\n", key, problem.c_str());
            CHECK(Hs::EncounterReady(Db(), *e));
            CHECK(Hs::EncounterUnsupported(Db(), *e).empty());
        }
    }

    // ---- shared cards -----------------------------------------------------------------------------------

    void TestDeathcharger()
    {
        std::unique_ptr<Match> m = Fresh();
        CHECK(Cast(*m, 0, "FP1_006") == ActionError::None);
        Ids b = TA::BoardIds(*m, 0);
        CHECK(b.size() == 1);
        MinionView const d = MV(*m, b[0]);
        CHECK(d.attack == 2 && d.health == 3 && d.charge && d.canAttack);
        CHECK(m->Apply(0, Attack(b[0], Hero(*m, 1))) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 28);
        // Deathrattle: 3 damage to its own hero (the controller's, not the killer's).
        CHECK(Kill(*m, 0, b[0]) == ActionError::None);
        CHECK_EQ(HP(*m, 0), 27);
        CHECK_EQ(HP(*m, 1), 28);
        std::unique_ptr<Match> n = Fresh();
        Ids e = TA::SetBoard(*n, 1, {"FP1_006"});
        CHECK(Kill(*n, 0, e[0]) == ActionError::None);
        CHECK_EQ(HP(*n, 1), 27);
        CHECK_EQ(HP(*n, 0), 30);
        // Silenced: no Deathrattle.
        std::unique_ptr<Match> o = Fresh();
        Ids f = TA::SetBoard(*o, 1, {"FP1_006"});
        CHECK(Cast(*o, 0, kSilence, f[0]) == ActionError::None);
        CHECK(Kill(*o, 0, f[0]) == ActionError::None);
        CHECK_EQ(HP(*o, 1), 30);
    }

    void TestNecroknight()
    {
        // Both neighbours go with it.
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {kYeti, "NAXM_001", kMurloc, kYeti});
        CHECK(Kill(*m, 0, b[1]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{kYeti}));
        // On an edge: the one neighbour.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 1, {"NAXM_001", kYeti, kMurloc});
        CHECK(Kill(*n, 0, c[0]) == ActionError::None);
        CHECK(Board(*n, 1) == (Strs{kMurloc}));
        // Alone: nothing to destroy, nothing happens.
        std::unique_ptr<Match> o = Fresh();
        Ids d = TA::SetBoard(*o, 0, {"NAXM_001"});
        CHECK(Kill(*o, 0, d[0]) == ActionError::None);
        CHECK(Board(*o, 0).empty());
        // Neighbours at the moment of death: [far][near][Necroknight][right] with Hellfire killing the Necroknight and its
        // left neighbour together: the right neighbour (it survives the 3 damage) is destroyed, the minion further left is not
        // next to the Necroknight and stays.
        std::unique_ptr<Match> p = Fresh();
        Ids e = TA::SetBoard(*p, 1, {kYeti, kMurloc, "NAXM_001", kYeti});
        TA::SetMinion(*p, e[2], 5, 3, 6);
        CHECK(Cast(*p, 0, "CS2_062") == ActionError::None); // Hellfire: 3 damage to ALL characters
        CHECK(Board(*p, 1) == (Strs{kYeti}));
        CHECK(MV(*p, e[0]).id == e[0]);
        // Silenced: no Deathrattle.
        std::unique_ptr<Match> q = Fresh();
        Ids f = TA::SetBoard(*q, 0, {kYeti, "NAXM_001", kYeti});
        CHECK(Cast(*q, 0, kSilence, f[1]) == ActionError::None);
        CHECK(Kill(*q, 0, f[1]) == ActionError::None);
        CHECK_EQ(Board(*q, 0).size(), 2u);
        // The destroyed neighbours' own Deathrattles run (Deathcharger hurts its controller).
        std::unique_ptr<Match> r = Fresh();
        Ids g = TA::SetBoard(*r, 1, {"FP1_006", "NAXM_001"});
        CHECK(Kill(*r, 0, g[1]) == ActionError::None);
        CHECK(Board(*r, 1).empty());
        CHECK_EQ(HP(*r, 1), 27);
    }

    void TestSkeletalSmith()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids b = TA::SetBoard(*m, 0, {"NAXM_002"});
        TA::SetWeapon(*m, 1, kAxe);
        TA::SetWeapon(*m, 0, kAxe);
        CHECK(Kill(*m, 0, b[0]) == ActionError::None);
        CHECK(HV(*m, 1).weaponId == 0); // the opponent's weapon is destroyed
        CHECK(HV(*m, 0).weaponId != 0); // its own stays
        // The enemy's Smith destroys MY weapon.
        std::unique_ptr<Match> n = Fresh();
        Ids c = TA::SetBoard(*n, 1, {"NAXM_002"});
        TA::SetWeapon(*n, 0, kAxe);
        CHECK(Kill(*n, 0, c[0]) == ActionError::None);
        CHECK(HV(*n, 0).weaponId == 0);
        // Without a weapon: nothing happens.
        std::unique_ptr<Match> o = Fresh();
        Ids d = TA::SetBoard(*o, 0, {"NAXM_002"});
        CHECK(Kill(*o, 0, d[0]) == ActionError::None);
    }

    // ---- wing 1 ------------------------------------------------------------------------------------------

    void TestSkitter()
    {
        std::unique_ptr<Match> m = Fresh("NAX1_04");
        HeroView h = HV(*m, 0);
        CHECK(h.powerCardId == "NAX1_04" && h.powerCost == 2);
        CHECK(LegalHasPower(*m, 0));
        TA::SetMana(*m, 0, 1, 10);
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Mana);
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"NAX1_03"}));
        Ids b = TA::BoardIds(*m, 0);
        MinionView const n = MV(*m, b[0]);
        CHECK(n.attack == 3 && n.health == 1);
        CHECK_EQ(HV(*m, 0).mana, 8);
        // Once per turn.
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Used);
        // A full board cannot use it.
        TA::SetBoard(*m, 0, Strs(7, kMurloc));
        TA::SetHeroPowerUsed(*m, 0, false);
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::BoardFull);
        CHECK(!LegalHasPower(*m, 0));
    }

    void TestLocustSwarm()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids mine = TA::SetBoard(*m, 0, {kYeti});
        Ids foe = TA::SetBoard(*m, 1, {kYeti, kMurloc, "CS2_121"});
        TA::SetHero(*m, 0, 20, 0);
        TA::SetHero(*m, 1, 12, 0);
        CHECK(Cast(*m, 0, "NAX1_05") == ActionError::None);
        CHECK_EQ(MV(*m, foe[0]).health, 2);       // 3 damage to every enemy minion
        CHECK(MV(*m, foe[1]).id == 0);             // the Murloc died
        CHECK_EQ(MV(*m, mine[0]).health, 5);       // own minions are safe
        CHECK_EQ(HP(*m, 0), 23);                   // 3 restored to the own hero
        CHECK_EQ(HP(*m, 1), 12);                   // the enemy hero is not touched
        CHECK_EQ(HV(*m, 0).mana, 3);               // cost 7
        // Capped at the maximum.
        std::unique_ptr<Match> n = Fresh();
        TA::SetHero(*n, 0, 29, 0);
        CHECK(Cast(*n, 0, "NAX1_05") == ActionError::None);
        CHECK_EQ(HP(*n, 0), 30);
        // Spell Damage does not add to it (the 6024 text has no $).
        std::unique_ptr<Match> o = Fresh();
        TA::SetBoard(*o, 0, {"CS2_052"});
        Ids f = TA::SetBoard(*o, 1, {kYeti});
        CHECK(Cast(*o, 0, "NAX1_05") == ActionError::None);
        CHECK_EQ(MV(*o, f[0]).health, 2);
    }

    // ---- wing 1: Faerlina ------------------------------------------------------------------------------------

    void TestRainOfFire()
    {
        // Only the hero to hit: one missile per card in the opponent's hand.
        std::unique_ptr<Match> m = Fresh("NAX2_03");
        TA::SetHand(*m, 1, Strs(5, kMurloc));
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 25);
        CHECK_EQ(HP(*m, 0), 30);
        CHECK_EQ(HV(*m, 0).mana, 8);
        // Minions are fair targets as well: the missiles add up to the hand size.
        std::unique_ptr<Match> n = Fresh("NAX2_03");
        Ids f = TA::SetBoard(*n, 1, {kYeti, kYeti});
        TA::SetHand(*n, 1, Strs(4, kMurloc));
        int const hand = static_cast<int>(Hand(*n, 1).size());
        CHECK(UsePower(*n, 0) == ActionError::None);
        int const lost = (30 - HP(*n, 1)) + (5 - MV(*n, f[0]).health) + (5 - MV(*n, f[1]).health);
        CHECK_EQ(lost, hand);
        CHECK_EQ(CountEvents(*n, EventType::Damage), hand);
        // An empty hand: no missile, and it is still a legal use.
        std::unique_ptr<Match> o = Fresh("NAX2_03");
        CHECK(UsePower(*o, 0) == ActionError::None);
        CHECK_EQ(HP(*o, 1), 30);
        CHECK_EQ(CountEvents(*o, EventType::Damage), 0);
        // The enemy's hand is counted when it is used, not the own one.
        std::unique_ptr<Match> p = Fresh("NAX2_03");
        TA::SetHand(*p, 0, Strs(6, kMurloc));
        CHECK(UsePower(*p, 0) == ActionError::None);
        CHECK_EQ(HP(*p, 1), 30);
        // Spell Damage does not make the missiles stronger (hero power, "ImmuneToSpellpower").
        std::unique_ptr<Match> q = Fresh("NAX2_03");
        TA::SetBoard(*q, 0, {"CS2_052"});
        TA::SetHand(*q, 1, Strs(3, kMurloc));
        CHECK(UsePower(*q, 0) == ActionError::None);
        CHECK_EQ(HP(*q, 1), 27);
        // An Immune hero takes nothing from them (Horsemen).
        std::unique_ptr<Match> r = Fresh("NAX2_03");
        TA::SetBoard(*r, 1, {"NAX9_02"});
        TA::SetHand(*r, 1, Strs(2, kMurloc));
        CHECK(UsePower(*r, 0) == ActionError::None);
        CHECK_EQ(HP(*r, 1), 30);
    }

    void TestWorshipper()
    {
        std::unique_ptr<Match> m = Fresh();
        CHECK_EQ(HV(*m, 0).attack, 0);
        CHECK(!HV(*m, 0).canAttack);
        TA::SetBoard(*m, 0, {"NAX2_05"});
        HeroView h = HV(*m, 0);
        CHECK_EQ(h.attack, 1);
        CHECK(h.canAttack);
        CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 29);
        CHECK(!HV(*m, 0).canAttack); // once per turn
        // Only on its controller's turn.
        CHECK_EQ(HV(*m, 1).attack, 0);
        ToSide1(*m);
        CHECK_EQ(HV(*m, 0).attack, 0);
        CHECK(!HV(*m, 0).canAttack);
        ToSide0(*m);
        CHECK_EQ(HV(*m, 0).attack, 1);
        // They stack, and add to a weapon and temporary attack.
        TA::SetBoard(*m, 0, {"NAX2_05", "NAX2_05"});
        CHECK_EQ(HV(*m, 0).attack, 2);
        TA::SetWeapon(*m, 0, kAxe);
        CHECK_EQ(HV(*m, 0).attack, 5);
        Ids w = TA::BoardIds(*m, 0);
        CHECK(Cast(*m, 0, kSilence, w[0]) == ActionError::None); // a silenced Worshipper gives nothing
        CHECK_EQ(HV(*m, 0).attack, 4);
        // The boss side: Worshipper on side 1 makes ITS hero able to attack on its turn.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 1, {"NAX2_05"});
        CHECK_EQ(HV(*n, 1).attack, 0); // not its turn
        ToSide1(*n);
        CHECK_EQ(HV(*n, 1).attack, 1);
        CHECK(HV(*n, 1).canAttack);
        CHECK(n->Apply(1, Attack(Hero(*n, 1), Hero(*n, 0))) == ActionError::None);
        CHECK_EQ(HP(*n, 0), 29);
    }

    // ---- wing 1: Maexxna ------------------------------------------------------------------------------------

    void TestWebWrap()
    {
        std::unique_ptr<Match> m = Fresh("NAX3_02");
        CHECK(!LegalHasPower(*m, 0)); // no enemy minion
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Requirement);
        Ids f = TA::SetBoard(*m, 1, {kYeti, kMurloc});
        TA::SetBoard(*m, 0, {kYeti});
        CHECK(LegalHasPower(*m, 0));
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK_EQ(HV(*m, 0).mana, 7); // cost 3
        CHECK_EQ(Board(*m, 1).size(), 1u);
        CHECK_EQ(Hand(*m, 1).size(), 1u); // back in the OPPONENT's hand
        CHECK_EQ(Board(*m, 0).size(), 1u);
        CHECK(Hand(*m, 0).empty());
        // The minion that went is one of the two, and it is random: over seeds both are returned.
        std::set<std::string> seen;
        for (uint32_t seed = 1; seed <= 24; ++seed)
        {
            std::unique_ptr<Match> n = Fresh("NAX3_02", "", seed);
            TA::SetBoard(*n, 1, {kYeti, kMurloc});
            CHECK(UsePower(*n, 0) == ActionError::None);
            Strs const h = Hand(*n, 1);
            if (h.size() == 1)
                seen.insert(h[0]);
        }
        CHECK_EQ(seen.size(), 2u);
        (void)f;
        // A full hand: the minion is destroyed instead.
        std::unique_ptr<Match> o = Fresh("NAX3_02");
        TA::SetBoard(*o, 1, {kYeti});
        TA::SetHand(*o, 1, Strs(10, kMurloc));
        CHECK(UsePower(*o, 0) == ActionError::None);
        CHECK(Board(*o, 1).empty());
        CHECK_EQ(Hand(*o, 1).size(), 10u);
    }

    void TestNecroticPoison()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids f = TA::SetBoard(*m, 1, {"FP1_010"});
        TA::SetMana(*m, 0, 2, 2);
        Ids h = TA::SetHand(*m, 0, {"NAX3_03"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 2);
        CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::Target); // a minion only
        CHECK(m->Apply(0, Play(h[0], 0)) == ActionError::Target);
        CHECK(m->Apply(0, Play(h[0], f[0])) == ActionError::None);
        CHECK(Board(*m, 1).empty());
        // Own minions too ("a minion").
        std::unique_ptr<Match> n = Fresh();
        Ids g = TA::SetBoard(*n, 0, {kYeti});
        CHECK(Cast(*n, 0, "NAX3_03", g[0]) == ActionError::None);
        CHECK(Board(*n, 0).empty());
    }

    // ---- wing 2: Noth ------------------------------------------------------------------------------------------

    void TestRaiseDead()
    {
        std::unique_ptr<Match> m = Fresh("NAX4_04");
        // Passive: never an action, never in the legal list, shown as spent, cost 0.
        CHECK(!LegalHasPower(*m, 0));
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Requirement);
        HeroView h = HV(*m, 0);
        CHECK(h.powerCardId == "NAX4_04" && h.powerCost == 0 && h.powerUsed);
        CHECK_EQ(HV(*m, 0).mana, 10);
        // An enemy minion dies: a Skeleton for the hero power's owner.
        Ids f = TA::SetBoard(*m, 1, {kYeti, kYeti});
        CHECK(Kill(*m, 0, f[0]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{"NAX4_03"}));
        MinionView const s = MV(*m, TA::BoardIds(*m, 0)[0]);
        CHECK(s.attack == 1 && s.health == 1);
        // Two enemy minions die together: two Skeletons.
        TA::SetBoard(*m, 1, {kMurloc, kMurloc, kYeti});
        CHECK(Cast(*m, 0, "CS2_032") == ActionError::None); // Flamestrike: 4 damage to all enemy minions
        CHECK_EQ(CountOf(Board(*m, 0), "NAX4_03"), 3);
        // Own minions dying raise nothing; the enemy's Raise Dead is not mine.
        std::unique_ptr<Match> n = Fresh("NAX4_04");
        Ids own = TA::SetBoard(*n, 0, {kYeti});
        CHECK(Kill(*n, 0, own[0]) == ActionError::None);
        CHECK(Board(*n, 0).empty());
        CHECK(Board(*n, 1).empty());
        // Silence does not stop it, a full board gets no Skeleton, and Skeletons dying raise others for the enemy side only.
        std::unique_ptr<Match> o = Fresh("NAX4_04");
        TA::SetBoard(*o, 0, Strs(7, kMurloc));
        Ids g = TA::SetBoard(*o, 1, {kYeti});
        TA::SetBoard(*o, 0, Strs(7, kMurloc));
        CHECK(Kill(*o, 0, g[0]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*o, 0), "NAX4_03"), 0);
        CHECK_EQ(Board(*o, 0).size(), 7u);
        // As the boss (side 1): the human's minion dying raises a Skeleton on the boss side.
        std::unique_ptr<Match> p = Fresh("", "NAX4_04");
        Ids hm = TA::SetBoard(*p, 0, {kYeti, kMurloc});
        CHECK(Kill(*p, 0, hm[0]) == ActionError::None);
        CHECK(Board(*p, 1) == (Strs{"NAX4_03"}));
        CHECK(Board(*p, 0) == (Strs{kMurloc}));
        ToSide1(*p);
        CHECK(!LegalHasPower(*p, 1));
        CHECK(p->Apply(1, Power(*p, 1)) == ActionError::Requirement);
        // The Skeleton arrives at the right end of the board, after the other summons of the same death.
        std::unique_ptr<Match> q = Fresh("NAX4_04");
        Ids hq = TA::SetBoard(*q, 1, {"FP1_007"}); // Nerubian Egg: its Deathrattle summons a 4/4 Nerubian for the Egg's owner
        TA::SetBoard(*q, 0, {kMurloc});
        CHECK(Kill(*q, 0, hq[0]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*q, 0), "NAX4_03"), 1);
        CHECK_EQ(CountOf(Board(*q, 1), "FP1_007t"), 1);
    }

    void TestPlague()
    {
        std::unique_ptr<Match> m = Fresh("NAX4_04");
        TA::SetBoard(*m, 0, {"NAX4_03", kYeti});
        TA::SetBoard(*m, 1, {kYeti, "NAX4_03", "FP1_007"});
        Mana(*m, 0);
        Ids h = TA::SetHand(*m, 0, {"NAX4_05"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 6);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        // Skeletons live (both sides); everything else died, own minions too; the Egg's Nerubian arrived after the spell
        // and the two enemy deaths raised two more Skeletons for the Raise Dead side.
        CHECK_EQ(CountOf(Board(*m, 1), "NAX4_03"), 1);
        CHECK_EQ(CountOf(Board(*m, 1), kYeti), 0);
        CHECK_EQ(CountOf(Board(*m, 1), "FP1_007"), 0);
        CHECK_EQ(CountOf(Board(*m, 1), "FP1_007t"), 1);
        CHECK_EQ(CountOf(Board(*m, 0), kYeti), 0);
        CHECK_EQ(CountOf(Board(*m, 0), "NAX4_03"), 3);
        // No target needed, and it works on an empty board.
        std::unique_ptr<Match> n = Fresh();
        CHECK(Cast(*n, 0, "NAX4_05") == ActionError::None);
    }

    // ---- wing 2: Heigan -------------------------------------------------------------------------------------------

    void TestEruption()
    {
        std::unique_ptr<Match> m = Fresh("NAX5_02");
        CHECK(!LegalHasPower(*m, 0));
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Requirement);
        Ids f = TA::SetBoard(*m, 1, {kYeti, kYeti, kMurloc});
        CHECK(LegalHasPower(*m, 0));
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK_EQ(MV(*m, f[0]).health, 3); // the left-most one
        CHECK_EQ(MV(*m, f[1]).health, 5);
        CHECK_EQ(MV(*m, f[2]).health, 1);
        CHECK_EQ(HV(*m, 0).mana, 9); // cost 1
        // No target is chosen: the action takes none.
        TA::SetHeroPowerUsed(*m, 0, false);
        CHECK(m->Apply(0, Power(*m, 0, f[1])) == ActionError::Target);
        // When it dies the next one is left-most; the hero is never hit.
        TA::SetMinion(*m, f[0], 4, 2, 5);
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK(MV(*m, f[0]).id == 0);
        CHECK_EQ(MV(*m, f[1]).health, 5);
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK_EQ(MV(*m, f[1]).health, 3);
        CHECK_EQ(HP(*m, 1), 30);
        // Stealth and "can't be targeted" do not matter: it is not a targeted effect.
        std::unique_ptr<Match> n = Fresh("NAX5_02");
        Ids g = TA::SetBoard(*n, 1, {"FP1_008", kYeti});
        CHECK(UsePower(*n, 0) == ActionError::None);
        CHECK_EQ(MV(*n, g[0]).health, 4);
        std::unique_ptr<Match> o = Fresh("NAX5_02");
        Ids i = TA::SetBoard(*o, 1, {"FP1_005", kYeti}); // Shade of Naxxramas: Stealth
        CHECK(UsePower(*o, 0) == ActionError::None);
        CHECK(MV(*o, i[0]).id == 0); // the 2/2 Shade dies to the 2 damage although it is in Stealth
        // Your own minions are never the target.
        std::unique_ptr<Match> p = Fresh("NAX5_02");
        Ids own = TA::SetBoard(*p, 0, {kYeti});
        Ids foe = TA::SetBoard(*p, 1, {kMurloc, kYeti});
        CHECK(UsePower(*p, 0) == ActionError::None);
        CHECK_EQ(MV(*p, own[0]).health, 5);
        CHECK(MV(*p, foe[0]).id == 0);
    }

    void TestMindpocalypse()
    {
        std::unique_ptr<Match> m = Fresh();
        TA::SetMana(*m, 0, 5, 5);
        TA::SetMana(*m, 1, 3, 3);
        Ids h = TA::SetHand(*m, 0, {"NAX5_03"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 2);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK_EQ(Hand(*m, 0).size(), 2u);
        CHECK_EQ(Hand(*m, 1).size(), 2u);
        // A full Mana Crystal each: the caster has 5 - 2 + 1 mana left of 6, the other side 4 of 4.
        CHECK_EQ(HV(*m, 0).maxMana, 6);
        CHECK_EQ(HV(*m, 0).mana, 4);
        CHECK_EQ(HV(*m, 1).maxMana, 4);
        CHECK_EQ(HV(*m, 1).mana, 4);
        // At 10 crystals nothing more is gained (no Excess Mana card for a full crystal).
        std::unique_ptr<Match> n = Fresh();
        TA::SetHand(*n, 0, {});
        CHECK(Cast(*n, 0, "NAX5_03") == ActionError::None);
        CHECK_EQ(HV(*n, 0).maxMana, 10);
        CHECK_EQ(HV(*n, 1).maxMana, 10);
        CHECK_EQ(Hand(*n, 0).size(), 2u);
        // Fatigue for both sides with empty decks (1 then 2 damage).
        std::unique_ptr<Match> o = Fresh();
        TA::SetDeck(*o, 0, {});
        TA::SetDeck(*o, 1, {});
        CHECK(Cast(*o, 0, "NAX5_03") == ActionError::None);
        CHECK_EQ(HP(*o, 0), 27);
        CHECK_EQ(HP(*o, 1), 27);
        // A full hand burns the extra cards.
        std::unique_ptr<Match> p = Fresh();
        TA::SetHand(*p, 1, Strs(9, kMurloc));
        CHECK(Cast(*p, 0, "NAX5_03") == ActionError::None);
        CHECK_EQ(Hand(*p, 1).size(), 10u);
    }

    // ---- wing 2: Loatheb -------------------------------------------------------------------------------------------

    void TestNecroticAura()
    {
        std::unique_ptr<Match> m = Fresh("NAX6_02");
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 27);
        CHECK_EQ(HP(*m, 0), 30);
        CHECK_EQ(HV(*m, 0).mana, 8);
        CHECK_EQ(CountEvents(*m, EventType::Damage), 1);
        // Armor soaks it; minions are not hurt.
        std::unique_ptr<Match> n = Fresh("NAX6_02");
        TA::SetHero(*n, 1, 30, 2);
        Ids f = TA::SetBoard(*n, 1, {kYeti});
        CHECK(UsePower(*n, 0) == ActionError::None);
        CHECK_EQ(HV(*n, 1).armor, 0);
        CHECK_EQ(HP(*n, 1), 29);
        CHECK_EQ(MV(*n, f[0]).health, 5);
        // The boss side with its large health pool (75).
        std::unique_ptr<Match> o = Fresh("", "NAX6_02", 11, 75);
        CHECK_EQ(HP(*o, 1), 75);
        ToSide1(*o);
        CHECK(UsePower(*o, 1) == ActionError::None);
        CHECK_EQ(HP(*o, 0), 27);
    }

    void TestDeathbloomAndSporeburst()
    {
        // Deathbloom: 5 damage to a minion, a Spore for the caster.
        std::unique_ptr<Match> m = Fresh();
        Ids f = TA::SetBoard(*m, 1, {kYeti, kYeti});
        TA::SetMinion(*m, f[0], 4, 6, 6);
        TA::SetMana(*m, 0, 4, 4);
        Ids h = TA::SetHand(*m, 0, {"NAX6_03"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 4);
        CHECK(m->Apply(0, Play(h[0], Hero(*m, 1))) == ActionError::Target);
        CHECK(m->Apply(0, Play(h[0], f[0])) == ActionError::None);
        CHECK_EQ(MV(*m, f[0]).health, 1);
        CHECK(Board(*m, 0) == (Strs{"NAX6_03t"}));
        Ids sp = TA::BoardIds(*m, 0);
        MinionView const s = MV(*m, sp[0]);
        CHECK(s.attack == 0 && s.health == 1);
        // The Spore's Deathrattle: all ENEMY minions of the Spore get +8 Attack.
        TA::SetBoard(*m, 0, {"NAX6_03t", kMurloc});
        sp = TA::BoardIds(*m, 0);
        CHECK(Kill(*m, 0, sp[0]) == ActionError::None);
        CHECK_EQ(MV(*m, f[0]).attack, 12);
        CHECK_EQ(MV(*m, f[1]).attack, 12);
        CHECK_EQ(MV(*m, TA::BoardIds(*m, 0)[0]).attack, 2); // the own Murloc is untouched
        // A Spore of the other side hits MY minions.
        std::unique_ptr<Match> n = Fresh();
        Ids mine = TA::SetBoard(*n, 0, {kYeti});
        Ids foe = TA::SetBoard(*n, 1, {"NAX6_03t"});
        CHECK(Kill(*n, 0, foe[0]) == ActionError::None);
        CHECK_EQ(MV(*n, mine[0]).attack, 12);
        // Spell Damage counts for Deathbloom (a "$5" spell).
        std::unique_ptr<Match> o = Fresh();
        Ids g = TA::SetBoard(*o, 1, {kYeti});
        TA::SetMinion(*o, g[0], 4, 6, 6);
        TA::SetBoard(*o, 0, {"CS2_052"});
        CHECK(Cast(*o, 0, "NAX6_03", g[0]) == ActionError::None);
        CHECK(MV(*o, g[0]).id == 0);
        // Sporeburst: 1 damage to all enemy minions, a Spore; Spell Damage counts.
        std::unique_ptr<Match> p = Fresh();
        Ids a = TA::SetBoard(*p, 1, {kYeti, kMurloc});
        Ids own = TA::SetBoard(*p, 0, {kYeti});
        CHECK(Cast(*p, 0, "NAX6_04") == ActionError::None);
        CHECK_EQ(MV(*p, a[0]).health, 4);
        CHECK(MV(*p, a[1]).id == 0);
        CHECK_EQ(MV(*p, own[0]).health, 5);
        CHECK_EQ(HV(*p, 0).mana, 9);
        CHECK_EQ(CountOf(Board(*p, 0), "NAX6_03t"), 1);
        CHECK_EQ(HP(*p, 1), 30);
        std::unique_ptr<Match> q = Fresh();
        Ids b = TA::SetBoard(*q, 1, {kYeti});
        TA::SetBoard(*q, 0, {"CS2_052"});
        CHECK(Cast(*q, 0, "NAX6_04") == ActionError::None);
        CHECK_EQ(MV(*q, b[0]).health, 3);
        // A full board: the damage is dealt, no Spore.
        std::unique_ptr<Match> r = Fresh();
        Ids c = TA::SetBoard(*r, 1, {kYeti});
        TA::SetBoard(*r, 0, Strs(7, kMurloc));
        CHECK(Cast(*r, 0, "NAX6_04") == ActionError::None);
        CHECK_EQ(MV(*r, c[0]).health, 4);
        CHECK_EQ(CountOf(Board(*r, 0), "NAX6_03t"), 0);
    }

    // ---- wing 3: Razuvious ---------------------------------------------------------------------------------------

    void TestUnbalancingStrike()
    {
        std::unique_ptr<Match> m = Fresh("NAX7_03");
        Ids f = TA::SetBoard(*m, 1, {kYeti});
        CHECK(LegalHasPower(*m, 0));
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Target); // needs a target
        CHECK(UsePower(*m, 0, f[0]) == ActionError::None);
        CHECK_EQ(MV(*m, f[0]).health, 2);
        CHECK_EQ(HV(*m, 0).mana, 8);
        CHECK(UsePower(*m, 0, Hero(*m, 1)) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 27);
        // Any character, the own ones too.
        CHECK(UsePower(*m, 0, Hero(*m, 0)) == ActionError::None);
        CHECK_EQ(HP(*m, 0), 27);
        // Every target is in the legal list (both heroes and the minion).
        TA::SetHeroPowerUsed(*m, 0, false);
        int n = 0;
        for (Action const& a : m->LegalActions(0))
            if (a.type == ActionType::HeroPower)
                ++n;
        CHECK_EQ(n, 3);
        // An Immune hero cannot be the target.
        std::unique_ptr<Match> o = Fresh("NAX7_03");
        TA::SetBoard(*o, 1, {"NAX9_03"});
        CHECK(UsePower(*o, 0, Hero(*o, 1)) == ActionError::Target);
    }

    void TestMassiveRuneblade()
    {
        std::unique_ptr<Match> m = Fresh();
        Mana(*m, 0);
        Ids h = TA::SetHand(*m, 0, {"NAX7_04"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 3);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        HeroView hv = HV(*m, 0);
        CHECK(hv.weaponCardId == "NAX7_04" && hv.weaponAttack == 5 && hv.weaponDurability == 2 && hv.attack == 5);
        // Double damage to heroes.
        CHECK(m->Apply(0, Attack(Hero(*m, 0), Hero(*m, 1))) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 20);
        CHECK_EQ(HV(*m, 0).weaponDurability, 1);
        // Not against minions, not for minions attacking, and the hero's displayed attack stays 5.
        std::unique_ptr<Match> n = Fresh();
        Ids f = TA::SetBoard(*n, 1, {kYeti});
        TA::SetWeapon(*n, 0, "NAX7_04");
        CHECK(n->Apply(0, Attack(Hero(*n, 0), f[0])) == ActionError::None);
        CHECK(MV(*n, f[0]).id == 0);
        CHECK_EQ(HP(*n, 0), 26); // the Yeti hit back for 4 (normal retaliation)
        // The boss side hitting the human hero for 10.
        std::unique_ptr<Match> o = Fresh();
        TA::SetWeapon(*o, 1, "NAX7_04");
        ToSide1(*o);
        TA::SetWeapon(*o, 1, "NAX7_04");
        TA::ReadyAll(*o);
        CHECK(o->Apply(1, Attack(Hero(*o, 1), Hero(*o, 0))) == ActionError::None);
        CHECK_EQ(HP(*o, 0), 20);
        // Armor soaks the doubled damage first.
        std::unique_ptr<Match> p = Fresh();
        TA::SetWeapon(*p, 0, "NAX7_04");
        TA::SetHero(*p, 1, 30, 4);
        CHECK(p->Apply(0, Attack(Hero(*p, 0), Hero(*p, 1))) == ActionError::None);
        CHECK_EQ(HP(*p, 1), 24);
        CHECK_EQ(HV(*p, 1).armor, 0);
        // Another weapon does not double.
        std::unique_ptr<Match> q = Fresh();
        TA::SetWeapon(*q, 0, kAxe);
        CHECK(q->Apply(0, Attack(Hero(*q, 0), Hero(*q, 1))) == ActionError::None);
        CHECK_EQ(HP(*q, 1), 27);
    }

    void TestMindControlCrystal()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids foe = TA::SetBoard(*m, 1, {"NAX7_02", kYeti, "NAX7_02"});
        Mana(*m, 0);
        Ids h = TA::SetHand(*m, 0, {"NAX7_05"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 1);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None); // no target
        CHECK(Board(*m, 0) == (Strs{"NAX7_02", "NAX7_02"}));
        CHECK(Board(*m, 1) == (Strs{kYeti}));
        CHECK_EQ(CountEvents(*m, EventType::Control), 2);
        MinionView const u = MV(*m, foe[0]);
        CHECK(u.attack == 0 && u.health == 7 && u.taunt);
        CHECK_EQ(HV(*m, 0).mana, 9);
        // Only as many as fit; the rest stay with the owner (not destroyed).
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 1, {"NAX7_02", "NAX7_02"});
        TA::SetBoard(*n, 0, Strs(6, kMurloc));
        CHECK(Cast(*n, 0, "NAX7_05") == ActionError::None);
        CHECK_EQ(CountOf(Board(*n, 0), "NAX7_02"), 1);
        CHECK_EQ(CountOf(Board(*n, 1), "NAX7_02"), 1);
        // Without Understudies it is still a legal play that does nothing.
        std::unique_ptr<Match> o = Fresh();
        TA::SetBoard(*o, 1, {kYeti});
        CHECK(Cast(*o, 0, "NAX7_05") == ActionError::None);
        CHECK(Board(*o, 0).empty() && Board(*o, 1) == (Strs{kYeti}));
        // The Understudies you took are yours: they do not come back, and Taunt applies for the new owner.
        ToSide1(*m);
        CHECK(Board(*m, 0) == (Strs{"NAX7_02", "NAX7_02"}));
        // Played by the other side it takes the first side's Understudies (the card is symmetric).
        std::unique_ptr<Match> p = Fresh();
        TA::SetBoard(*p, 0, {"NAX7_02"});
        ToSide1(*p);
        CHECK(Cast(*p, 1, "NAX7_05") == ActionError::None);
        CHECK(Board(*p, 1) == (Strs{"NAX7_02"}) && Board(*p, 0).empty());
    }

    // ---- wing 3: Gothik ----------------------------------------------------------------------------------------------

    void TestHarvest()
    {
        std::unique_ptr<Match> m = Fresh("NAX8_02");
        CHECK(LegalHasPower(*m, 0));
        CHECK(UsePower(*m, 0) == ActionError::None);
        CHECK_EQ(Hand(*m, 0).size(), 1u);
        CHECK_EQ(HV(*m, 0).mana, 8);
        CHECK(m->Apply(0, Power(*m, 0)) == ActionError::Used);
        // Unholy Shadow (Horsemen): 5 mana, draw 2.
        std::unique_ptr<Match> n = Fresh("NAX9_06");
        TA::SetMana(*n, 0, 4, 4);
        CHECK(n->Apply(0, Power(*n, 0)) == ActionError::Mana);
        CHECK(UsePower(*n, 0) == ActionError::None);
        CHECK_EQ(Hand(*n, 0).size(), 2u);
        CHECK_EQ(HV(*n, 0).mana, 5);
    }

    void TestUnrelentingAndSpectral()
    {
        struct Row { char const* card; char const* token; int health; };
        for (Row const& r : {Row{"NAX8_03", "NAX8_03t", 2}, Row{"NAX8_04", "NAX8_04t", 4}, Row{"NAX8_05", "NAX8_05t", 6}})
        {
            // The boss's Unrelenting minion dies: the spectral twin goes to the OPPONENT of the boss (side 0).
            std::unique_ptr<Match> m = Fresh();
            Ids f = TA::SetBoard(*m, 1, {r.card});
            CHECK(Kill(*m, 0, f[0]) == ActionError::None);
            CHECK(Board(*m, 0) == (Strs{r.token}));
            CHECK(Board(*m, 1).empty());
            MinionView const s = MV(*m, TA::BoardIds(*m, 0)[0]);
            CHECK(s.attack == 0 && s.health == r.health);
            // At the start of its controller's turn it deals 1 damage to ITS hero, only then.
            CHECK_EQ(HP(*m, 0), 30);
            ToSide1(*m);
            CHECK_EQ(HP(*m, 0), 30);
            CHECK_EQ(HP(*m, 1), 30);
            ToSide0(*m);
            CHECK_EQ(HP(*m, 0), 29);
            CHECK_EQ(HP(*m, 1), 30);
            ToSide1(*m);
            ToSide0(*m);
            CHECK_EQ(HP(*m, 0), 28);
        }
        // The same card on my side sends the twin to the other side.
        std::unique_ptr<Match> n = Fresh();
        Ids g = TA::SetBoard(*n, 0, {"NAX8_04"});
        CHECK(Kill(*n, 0, g[0]) == ActionError::None);
        CHECK(Board(*n, 1) == (Strs{"NAX8_04t"}) && Board(*n, 0).empty());
        ToSide1(*n);
        CHECK_EQ(HP(*n, 1), 29);
        // A full board on the receiving side: no twin.
        std::unique_ptr<Match> o = Fresh();
        Ids h = TA::SetBoard(*o, 1, {"NAX8_03"});
        TA::SetBoard(*o, 0, Strs(7, kMurloc));
        CHECK(Kill(*o, 0, h[0]) == ActionError::None);
        CHECK_EQ(CountOf(Board(*o, 0), "NAX8_03t"), 0);
        CHECK_EQ(Board(*o, 0).size(), 7u);
        // Silenced Unrelenting minion: no twin. A silenced Spectral minion: no damage.
        std::unique_ptr<Match> p = Fresh();
        Ids i = TA::SetBoard(*p, 1, {"NAX8_05"});
        CHECK(Cast(*p, 0, kSilence, i[0]) == ActionError::None);
        CHECK(Kill(*p, 0, i[0]) == ActionError::None);
        CHECK(Board(*p, 0).empty());
        std::unique_ptr<Match> q = Fresh();
        Ids j = TA::SetBoard(*q, 0, {"NAX8_03t"});
        CHECK(Cast(*q, 0, kSilence, j[0]) == ActionError::None);
        ToSide1(*q);
        ToSide0(*q);
        CHECK_EQ(HP(*q, 0), 30);
        // The twin is placed at the right end of the receiving board, once per death even for several.
        std::unique_ptr<Match> s = Fresh();
        TA::SetBoard(*s, 0, {kMurloc});
        TA::SetBoard(*s, 1, {"NAX8_03", "NAX8_04"});
        CHECK(Cast(*s, 0, "CS2_032") == ActionError::None); // Flamestrike: 4 damage kills the Trainee, the Warrior has 4
        CHECK(Board(*s, 0) == (Strs{kMurloc, "NAX8_03t", "NAX8_04t"}));
    }

    // ---- wing 3: the Four Horsemen -------------------------------------------------------------------------------------

    void TestHorsemenImmune()
    {
        std::unique_ptr<Match> m = Fresh("", "", 11, 7);
        Ids hs = TA::SetBoard(*m, 1, {"NAX9_02", "NAX9_03", "NAX9_04"});
        TA::SetBoard(*m, 0, {kMurloc});
        TA::ReadyAll(*m);
        CHECK(HV(*m, 1).immune);
        CHECK(!HV(*m, 0).immune);
        CHECK_EQ(HP(*m, 1), 7);
        for (EntityId id : hs)
        {
            MinionView const x = MV(*m, id);
            CHECK(x.attack == 1 && x.health == 7);
        }
        // Immune: no attack, no targeted spell, no damage from area effects or the own hero power.
        EntityId const mine = TA::BoardIds(*m, 0)[0];
        TA::SetBoard(*m, 1, {"NAX9_02", "NAX9_03", "NAX9_04"});
        CHECK(m->Apply(0, Attack(mine, Hero(*m, 1))) == ActionError::Target);
        CHECK(Cast(*m, 0, kFireball, Hero(*m, 1)) == ActionError::Target);
        CHECK(Cast(*m, 0, "CS2_062") == ActionError::None); // Hellfire hits all characters
        CHECK_EQ(HP(*m, 1), 7);
        CHECK_EQ(HP(*m, 0), 27);
        // Still immune on the next turn (the aura is not a temporary effect).
        ToSide1(*m);
        CHECK(HV(*m, 1).immune);
        ToSide0(*m);
        CHECK(HV(*m, 1).immune);
        // It is gone only when the last Horseman is gone.
        hs = TA::BoardIds(*m, 1);
        CHECK(Kill(*m, 0, hs[0]) == ActionError::None);
        CHECK(HV(*m, 1).immune);
        CHECK(Kill(*m, 0, hs[1]) == ActionError::None);
        CHECK(HV(*m, 1).immune);
        CHECK(Kill(*m, 0, hs[2]) == ActionError::None);
        CHECK(!HV(*m, 1).immune);
        CHECK(Cast(*m, 0, kFireball, Hero(*m, 1)) == ActionError::None);
        CHECK_EQ(HP(*m, 1), 1);
        // Silencing a Horseman removes ITS part (the last one: the hero is vulnerable).
        std::unique_ptr<Match> n = Fresh();
        Ids a = TA::SetBoard(*n, 1, {"NAX9_04"});
        CHECK(HV(*n, 1).immune);
        CHECK(Cast(*n, 0, kSilence, a[0]) == ActionError::None);
        CHECK(!HV(*n, 1).immune);
        // The aura belongs to the hero of the Horseman's controller: a stolen Horseman protects the thief.
        std::unique_ptr<Match> o = Fresh();
        TA::SetBoard(*o, 1, {"NAX9_02"});
        Mana(*o, 0);
        Ids h = TA::SetHand(*o, 0, {"CS1_113"}); // Mind Control
        CHECK(o->Apply(0, Play(h[0], TA::BoardIds(*o, 1)[0])) == ActionError::None);
        CHECK(HV(*o, 0).immune);
        CHECK(!HV(*o, 1).immune);
        // Fatigue does not hurt an Immune hero.
        std::unique_ptr<Match> p = Fresh();
        TA::SetBoard(*p, 1, {"NAX9_02"});
        TA::SetDeck(*p, 1, {});
        ToSide1(*p);
        CHECK_EQ(HP(*p, 1), 30);
    }

    void TestRuneblade()
    {
        std::unique_ptr<Match> m = Fresh();
        TA::SetBoard(*m, 1, {"NAX9_02", "NAX9_03", "NAX9_04"});
        ToSide1(*m);
        Mana(*m, 1);
        Ids h = TA::SetHand(*m, 1, {"NAX9_05"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 3);
        CHECK(m->Apply(1, Play(h[0])) == ActionError::None);
        HeroView hv = HV(*m, 1);
        CHECK(hv.weaponCardId == "NAX9_05" && hv.weaponAttack == 1 && hv.weaponDurability == 3);
        // +3 only when none of the Horsemen is left on either board.
        Ids hs = TA::BoardIds(*m, 1);
        for (EntityId id : hs)
            TA::SetMinion(*m, id, 1, 3, 7); // a Fireball kills a wounded Horseman
        ToSide0(*m);
        CHECK(Kill(*m, 0, hs[0]) == ActionError::None);
        CHECK(Kill(*m, 0, hs[1]) == ActionError::None);
        CHECK_EQ(HV(*m, 1).weaponAttack, 1);
        CHECK(Kill(*m, 0, hs[2]) == ActionError::None);
        CHECK_EQ(HV(*m, 1).weaponAttack, 4);
        ToSide1(*m);
        CHECK_EQ(HV(*m, 1).attack, 4);
        TA::ReadyAll(*m);
        CHECK(m->Apply(1, Attack(Hero(*m, 1), Hero(*m, 0))) == ActionError::None);
        CHECK_EQ(HP(*m, 0), 26);
        // A Horseman on the OTHER board (stolen) still counts as alive.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 0, {"NAX9_03"});
        TA::SetWeapon(*n, 1, "NAX9_05");
        CHECK_EQ(HV(*n, 1).weaponAttack, 1);
        // Without any Horseman from the start (the weapon alone) it hits for 4.
        std::unique_ptr<Match> o = Fresh();
        TA::SetWeapon(*o, 0, "NAX9_05");
        CHECK_EQ(HV(*o, 0).weaponAttack, 4);
    }

    void TestMarkOfTheHorsemen()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids a = TA::SetBoard(*m, 0, {"NAX9_02", kMurloc});
        Ids f = TA::SetBoard(*m, 1, {kYeti});
        TA::SetWeapon(*m, 0, "NAX9_05");
        Mana(*m, 0);
        Ids h = TA::SetHand(*m, 0, {"NAX9_07"});
        CHECK_EQ(TA::HandCost(*m, h[0]), 5);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        MinionView const x = MV(*m, a[0]);
        CHECK(x.attack == 2 && x.health == 8);
        CHECK(MV(*m, a[1]).attack == 3 && MV(*m, a[1]).health == 2);
        CHECK(MV(*m, f[0]).attack == 4 && MV(*m, f[0]).health == 5);
        HeroView hv = HV(*m, 0);
        CHECK(hv.weaponAttack == 2 && hv.weaponDurability == 4);
        // Without a weapon the minions still get it.
        std::unique_ptr<Match> n = Fresh();
        Ids b = TA::SetBoard(*n, 0, {kMurloc});
        CHECK(Cast(*n, 0, "NAX9_07") == ActionError::None);
        CHECK(MV(*n, b[0]).attack == 3 && MV(*n, b[0]).health == 2);
        CHECK_EQ(HV(*n, 0).weaponId, 0u);
    }

    // ---- legal actions, the AI and hidden information ---------------------------------------------------------------

    void TestLegalActions()
    {
        // Every legal action applies, hero powers included, for each of the boss powers.
        for (char const* power : {"NAX1_04", "NAX2_03", "NAX3_02", "NAX4_04", "NAX5_02", "NAX6_02", "NAX7_03", "NAX8_02", "NAX9_06"})
        {
            std::unique_ptr<Match> m = Fresh(power);
            TA::SetBoard(*m, 1, {kYeti, kMurloc});
            TA::SetBoard(*m, 0, {kMurloc});
            TA::SetHand(*m, 1, {kMurloc, kYeti});
            Mana(*m, 0);
            bool listed = false;
            for (Action const& a : m->LegalActions(0))
                if (a.type == ActionType::HeroPower)
                {
                    listed = true;
                    std::unique_ptr<Match> copy = m->RedactedCopy(0, 5);
                    CHECK(copy->Apply(0, a) == ActionError::None);
                }
            CHECK(listed == (std::string(power) != "NAX4_04"));
        }
    }

    // The Hard AI uses the boss hero powers on its turn when they pay, never the passive one, and never gets refused.
    void TestAiHeroPowers()
    {
        struct Case { char const* power; bool minions; };
        for (Case const& c : {Case{"NAX1_04", false}, Case{"NAX3_02", true}, Case{"NAX5_02", true}, Case{"NAX6_02", false},
                 Case{"NAX7_03", true}, Case{"NAX8_02", false}})
        {
            std::unique_ptr<Match> m = Fresh("", c.power);
            if (c.minions)
                TA::SetBoard(*m, 0, {kYeti});
            ToSide1(*m);
            Action const a = ChooseAiAction(*m, 1, Db(), 0, AiProfile::Hard, 99);
            CHECK(a.type == ActionType::HeroPower);
            CHECK(m->Apply(1, a) == ActionError::None);
        }
        // Raise Dead is never chosen, over a whole turn of options.
        std::unique_ptr<Match> m = Fresh("", "NAX4_04");
        TA::SetBoard(*m, 1, {kYeti});
        ToSide1(*m);
        TA::SetHand(*m, 1, {kMurloc});
        for (int i = 0; i < 4; ++i)
        {
            Action const a = ChooseAiAction(*m, 1, Db(), i, AiProfile::Hard, 7 + static_cast<uint32_t>(i));
            CHECK(a.type != ActionType::HeroPower);
            if (a.type == ActionType::EndTurn)
                break;
            CHECK(m->Apply(1, a) == ActionError::None);
        }
    }

    // The AI of the boss does not leak: its copy of the human side has only placeholder cards, and Mindpocalypse /
    // Rain of Fire / Web Wrap / Mind Control Crystal simulated on it reveal nothing.
    void TestRedactedCopy()
    {
        std::unique_ptr<Match> m = Fresh("", "NAX2_03");
        TA::SetHand(*m, 0, {"NAX7_05", kYeti, kFireball});
        TA::SetBoard(*m, 0, {kYeti});
        ToSide1(*m);
        TA::SetHand(*m, 1, {"NAX5_03", "NAX3_03"});
        std::unique_ptr<Match> c = m->RedactedCopy(1, 31);
        MatchView const v = c->ViewFor(1, c->LastEventSeq());
        CHECK(v.other.hand.empty());
        CHECK_EQ(v.other.handCount, 3);
        CHECK_EQ(v.self.hand.size(), 2u);
        // The human's hand is unknown cards: Crystal and Fireball never fire there.
        for (std::string const& id : Hand(*c, 0))
            CHECK(id == "?");
        // Rain of Fire in the copy hits for the hand size of the human side.
        Mana(*c, 1);
        TA::SetHeroPowerUsed(*c, 1, false);
        CHECK(c->Apply(1, Power(*c, 1)) == ActionError::None);
        CHECK_EQ((30 - c->HeroHealth(0)) + (5 - MV(*c, TA::BoardIds(*c, 0)[0]).health), 3);
        // Mindpocalypse in the copy: the human side draws placeholders from its (unknown) deck.
        std::unique_ptr<Match> d = m->RedactedCopy(1, 32);
        Mana(*d, 1);
        Ids h = TA::HandIds(*d, 1);
        CHECK(d->Apply(1, Play(h[0])) == ActionError::None);
        CHECK_EQ(d->ViewFor(1, d->LastEventSeq()).other.handCount, 5);
        for (std::string const& id : Hand(*d, 0))
            CHECK(id == "?");
        CHECK(d->ViewFor(1, 0).other.hand.empty());
        // The real match is untouched by the copies.
        CHECK_EQ(Hand(*m, 0).size(), 3u);
        CHECK_EQ(HP(*m, 0), 30);
    }

    // ======================================================================================================
    // Step E2b: wings 4-5 (Patchwerk, Grobbulus, Gluth, Thaddius, Sapphiron, Kel'Thuzad)
    // ======================================================================================================

    int PowerEvents(Match const& m, int side)
    {
        int n = 0;
        for (Event const& e : m.ViewFor(0, 0).events)
            if (e.type == EventType::HeroPower && e.side == side)
                ++n;
        return n;
    }

    std::string PowerOf(Match const& m, int side)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        return side == 0 ? v.self.hero.powerCardId : v.other.hero.powerCardId;
    }

    bool OnBoard(Match const& m, int side, EntityId id)
    {
        Ids const b = TA::BoardIds(m, side);
        return std::find(b.begin(), b.end(), id) != b.end();
    }

    void TestHatefulStrike()
    {
        std::unique_ptr<Match> m = Fresh("", "NAX10_03");
        Ids e = TA::SetBoard(*m, 0, {kYeti, kMurloc});
        ToSide1(*m);
        TA::SetBoard(*m, 1, {kMurloc});
        // 4 mana: not with 3.
        TA::SetMana(*m, 1, 3, 10);
        TA::SetHeroPowerUsed(*m, 1, false);
        CHECK(m->Apply(1, Power(*m, 1, e[0])) == ActionError::Mana);
        // A target is required; a hero is no target; a minion of either side is.
        CHECK(UsePower(*m, 1, 0) == ActionError::Target);
        CHECK(UsePower(*m, 1, Hero(*m, 0)) == ActionError::Target);
        CHECK(UsePower(*m, 1, e[0]) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{kMurloc}));
        CHECK_EQ(HV(*m, 1).mana, 6);
        CHECK(UsePower(*m, 1, TA::BoardIds(*m, 1)[0]) == ActionError::None); // its own minion too
        CHECK(Board(*m, 1).empty());
        // Stealth hides a minion from a hero power.
        std::unique_ptr<Match> n = Fresh("", "NAX10_03");
        Ids s = TA::SetBoard(*n, 0, {"EX1_010"});
        ToSide1(*n);
        CHECK(UsePower(*n, 1, s[0]) == ActionError::Target);
        // Deathrattles of the destroyed minion run.
        std::unique_ptr<Match> o = Fresh("", "NAX10_03");
        Ids c = TA::SetBoard(*o, 0, {"FP1_001"}); // Zombie Chow: heals the enemy hero 5
        ToSide1(*o);
        TA::SetHero(*o, 1, 20, 0);
        CHECK(UsePower(*o, 1, c[0]) == ActionError::None);
        CHECK_EQ(HP(*o, 1), 25);
    }

    void TestHook()
    {
        // Hook leaves play by durability: back into its owner's hand.
        std::unique_ptr<Match> m = Fresh("", "NAX10_03");
        Ids wall = TA::SetBoard(*m, 0, {kYeti});
        TA::SetMinion(*m, wall[0], 0, 99, 99);
        ToSide1(*m);
        TA::SetWeapon(*m, 1, "NAX10_02");
        CHECK_EQ(HV(*m, 1).weaponAttack, 5);
        CHECK_EQ(HV(*m, 1).weaponDurability, 8);
        for (int i = 0; i < 7; ++i)
        {
            TA::ReadyAll(*m);
            CHECK(m->Apply(1, Attack(Hero(*m, 1), wall[0])) == ActionError::None);
        }
        CHECK_EQ(HV(*m, 1).weaponDurability, 1);
        CHECK(CountOf(Hand(*m, 1), "NAX10_02") == 0);
        TA::ReadyAll(*m);
        CHECK(m->Apply(1, Attack(Hero(*m, 1), wall[0])) == ActionError::None);
        CHECK_EQ(HV(*m, 1).weaponId, 0u);
        CHECK(CountOf(Hand(*m, 1), "NAX10_02") == 1);
        // ... and can be equipped again with its full durability (3 mana).
        Mana(*m, 1);
        Ids h = TA::HandIds(*m, 1);
        CHECK(m->Apply(1, Play(h[0])) == ActionError::None);
        CHECK(HV(*m, 1).weaponDurability == 8 && HV(*m, 1).weaponAttack == 5);
        // Destroyed by an effect (Skeletal Smith's Deathrattle): also back in the hand.
        std::unique_ptr<Match> n = Fresh("", "NAX10_03");
        Ids smith = TA::SetBoard(*n, 0, {"NAXM_002"});
        TA::SetWeapon(*n, 1, "NAX10_02");
        CHECK(Kill(*n, 0, smith[0]) == ActionError::None);
        CHECK_EQ(HV(*n, 1).weaponId, 0u);
        CHECK(CountOf(Hand(*n, 1), "NAX10_02") == 1);
        // Replaced by another weapon: the Deathrattle runs as well.
        std::unique_ptr<Match> o = Fresh("", "NAX10_03");
        ToSide1(*o);
        TA::SetWeapon(*o, 1, "NAX10_02");
        CHECK(Cast(*o, 1, kAxe) == ActionError::None);
        CHECK(HV(*o, 1).weaponAttack == 3);
        CHECK(CountOf(Hand(*o, 1), "NAX10_02") == 1);
        // A full hand: the weapon is lost (no crash).
        std::unique_ptr<Match> p = Fresh("", "NAX10_03");
        Ids smith2 = TA::SetBoard(*p, 0, {"NAXM_002"});
        TA::SetWeapon(*p, 1, "NAX10_02");
        TA::SetHand(*p, 1, Strs(10, kMurloc));
        CHECK(Kill(*p, 0, smith2[0]) == ActionError::None);
        CHECK_EQ(Hand(*p, 1).size(), 10u);
        CHECK(CountOf(Hand(*p, 1), "NAX10_02") == 0);
    }

    void TestPoisonCloud()
    {
        // One slime for every minion of either side that dies of the 1 damage; they join Grobbulus's right end.
        std::unique_ptr<Match> m = Fresh("", "NAX11_02");
        Ids mine = TA::SetBoard(*m, 0, {kMurloc, kYeti, kMurloc});
        ToSide1(*m);
        TA::SetBoard(*m, 1, {kMurloc, kYeti});
        CHECK(UsePower(*m, 1) == ActionError::None);
        CHECK(Board(*m, 0) == (Strs{kYeti}));
        CHECK(Board(*m, 1) == (Strs{kYeti, "NAX11_03", "NAX11_03", "NAX11_03"}));
        CHECK_EQ(MV(*m, mine[1]).health, 4);
        CHECK(MV(*m, TA::BoardIds(*m, 1)[1]).attack == 2 && MV(*m, TA::BoardIds(*m, 1)[1]).health == 2);
        CHECK_EQ(HV(*m, 1).mana, 8);
        // Nothing dies: no slime. A Divine Shield is lost instead of Health.
        std::unique_ptr<Match> n = Fresh("", "NAX11_02");
        Ids sq = TA::SetBoard(*n, 0, {"EX1_008", kYeti});
        ToSide1(*n);
        TA::SetBoard(*n, 1, {kYeti});
        CHECK(UsePower(*n, 1) == ActionError::None);
        CHECK_EQ(Board(*n, 1).size(), 1u);
        CHECK_EQ(Board(*n, 0).size(), 2u);
        CHECK(!MV(*n, sq[0]).divineShield);
        // Without any minion the power is a legal no-op (the card has no requirement).
        std::unique_ptr<Match> o = Fresh("", "NAX11_02");
        ToSide1(*o);
        CHECK(UsePower(*o, 1) == ActionError::None);
        CHECK(Board(*o, 1).empty());
        // A full board: only as many slimes as there is room for once the dead are gone.
        std::unique_ptr<Match> p = Fresh("", "NAX11_02");
        TA::SetBoard(*p, 0, Strs(3, kMurloc));
        ToSide1(*p);
        TA::SetBoard(*p, 1, Strs(7, kMurloc));
        CHECK(UsePower(*p, 1) == ActionError::None);
        CHECK(Board(*p, 1) == Strs(7, "NAX11_03"));
        CHECK(Board(*p, 0).empty());
        // Deathrattles resolve before the slimes arrive: a dead Haunted Creeper leaves its Spiders on the board first.
        std::unique_ptr<Match> q = Fresh("", "NAX11_02");
        TA::SetBoard(*q, 0, {kMurloc});
        ToSide1(*q);
        Ids cr = TA::SetBoard(*q, 1, {"FP1_002"});
        TA::SetMinion(*q, cr[0], 1, 1, 2);
        CHECK(UsePower(*q, 1) == ActionError::None);
        CHECK(Board(*q, 1) == (Strs{"FP1_002t", "FP1_002t", "NAX11_03", "NAX11_03"}));
    }

    void TestMutatingInjection()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids a = TA::SetBoard(*m, 0, {kMurloc});
        Ids b = TA::SetBoard(*m, 1, {kYeti});
        CHECK(Cast(*m, 0, "NAX11_04", a[0]) == ActionError::None);
        MinionView const x = MV(*m, a[0]);
        CHECK(x.attack == 6 && x.health == 5 && x.taunt);
        CHECK(Cast(*m, 0, "NAX11_04", b[0]) == ActionError::None); // either side
        CHECK(MV(*m, b[0]).attack == 8 && MV(*m, b[0]).health == 9 && MV(*m, b[0]).taunt);
        CHECK(Cast(*m, 0, "NAX11_04", Hero(*m, 1)) == ActionError::Target);
        CHECK(Cast(*m, 0, "NAX11_04") == ActionError::Target);
    }

    void TestDecimate()
    {
        std::unique_ptr<Match> m = Fresh("", "NAX12_02");
        Ids mine = TA::SetBoard(*m, 0, {kYeti, "CS2_187"});
        TA::SetMinion(*m, mine[0], 4, 2, 5); // damaged: Health becomes 1 all the same, the damage is gone
        ToSide1(*m);
        Ids own = TA::SetBoard(*m, 1, {"CS2_186"}); // War Golem 7/7
        CHECK(UsePower(*m, 1) == ActionError::None);
        CHECK(MV(*m, mine[0]).attack == 4 && MV(*m, mine[0]).health == 1 && MV(*m, mine[0]).maxHealth == 1);
        CHECK(MV(*m, mine[1]).attack == 5 && MV(*m, mine[1]).health == 1);
        CHECK(MV(*m, own[0]).attack == 7 && MV(*m, own[0]).health == 1); // its own minions as well
        CHECK_EQ(HV(*m, 1).mana, 8);
        // A Health buff afterwards works as usual (Supercharge: +2 Health).
        CHECK(Cast(*m, 1, "NAX13_03") == ActionError::None);
        CHECK_EQ(MV(*m, own[0]).health, 3);
        // Needs an enemy minion.
        std::unique_ptr<Match> n = Fresh("", "NAX12_02");
        ToSide1(*n);
        TA::SetBoard(*n, 1, {kYeti});
        CHECK(UsePower(*n, 1) == ActionError::Requirement);
        CHECK(!LegalHasPower(*n, 1));
        CHECK_EQ(MV(*n, TA::BoardIds(*n, 1)[0]).health, 5);
    }

    void TestJawsAndEnrage()
    {
        std::unique_ptr<Match> m = Fresh();
        Ids mine = TA::SetBoard(*m, 0, {"FP1_002"});
        Ids foe = TA::SetBoard(*m, 1, {"FP1_001", kYeti, "FP1_007", kMurloc});
        TA::SetWeapon(*m, 0, "NAX12_03");
        CHECK(HV(*m, 0).weaponAttack == 1 && HV(*m, 0).weaponDurability == 5);
        CHECK(Kill(*m, 0, foe[0]) == ActionError::None); // Zombie Chow: Deathrattle
        CHECK_EQ(HV(*m, 0).weaponAttack, 3);
        CHECK(Kill(*m, 0, foe[1]) == ActionError::None); // Yeti: no Deathrattle
        CHECK_EQ(HV(*m, 0).weaponAttack, 3);
        CHECK(Kill(*m, 0, foe[2]) == ActionError::None); // Nerubian Egg (its Nerubian has no Deathrattle)
        CHECK_EQ(HV(*m, 0).weaponAttack, 5);
        CHECK(Kill(*m, 0, foe[3]) == ActionError::None); // Murloc: no
        CHECK_EQ(HV(*m, 0).weaponAttack, 5);
        CHECK(Kill(*m, 0, mine[0]) == ActionError::None); // its own side counts too (Haunted Creeper)
        CHECK_EQ(HV(*m, 0).weaponAttack, 7);
        CHECK_EQ(HV(*m, 0).weaponDurability, 5);
        // Two minions with Deathrattle dying at once: +2 each.
        std::unique_ptr<Match> n = Fresh();
        TA::SetBoard(*n, 1, {"FP1_001", "FP1_001", kMurloc});
        TA::SetWeapon(*n, 0, "NAX12_03");
        CHECK(Cast(*n, 0, "CS2_062") == ActionError::None); // Hellfire
        CHECK_EQ(HV(*n, 0).weaponAttack, 5);
        // Enrage: +6 Attack for the hero this turn only.
        std::unique_ptr<Match> o = Fresh("", "NAX12_02");
        ToSide1(*o);
        CHECK(Cast(*o, 1, "NAX12_04") == ActionError::None);
        CHECK_EQ(HV(*o, 1).attack, 6);
        CHECK(HV(*o, 1).canAttack);
        CHECK(o->Apply(1, Attack(Hero(*o, 1), Hero(*o, 0))) == ActionError::None);
        CHECK_EQ(HP(*o, 0), 24);
        ToSide0(*o);
        ToSide1(*o);
        CHECK_EQ(HV(*o, 1).attack, 0);
        // With a weapon it adds up.
        std::unique_ptr<Match> p = Fresh();
        TA::SetWeapon(*p, 0, kAxe);
        CHECK(Cast(*p, 0, "NAX12_04") == ActionError::None);
        CHECK_EQ(HV(*p, 0).attack, 9);
    }

    void TestPolarityShiftAndSupercharge()
    {
        std::unique_ptr<Match> m = Fresh("", "NAX13_02");
        // The power shows as spent before it has fired and is never an action.
        CHECK(HV(*m, 1).powerUsed);
        Ids mine = TA::SetBoard(*m, 0, {kYeti, kMurloc, "FP1_007"});
        TA::SetMinion(*m, mine[0], 4, 3, 5); // damaged Yeti, current Health 3
        Ids boss = TA::SetBoard(*m, 1, {kYeti});
        int const before = PowerEvents(*m, 1);
        m->Apply(0, End());
        CHECK_EQ(PowerEvents(*m, 1), before + 1);
        // Attack and current Health swapped for ALL minions, damage gone; the Egg (0 Attack) died, its Nerubian came.
        MinionView const y = MV(*m, mine[0]);
        CHECK(y.attack == 3 && y.health == 4 && y.maxHealth == 4);
        MinionView const r = MV(*m, mine[1]);
        CHECK(r.attack == 1 && r.health == 2);
        CHECK(Board(*m, 0) == (Strs{kYeti, kMurloc, "FP1_007t"}));
        MinionView const nerub = MV(*m, TA::BoardIds(*m, 0)[2]);
        CHECK(nerub.attack == 4 && nerub.health == 4);
        CHECK(MV(*m, boss[0]).attack == 5 && MV(*m, boss[0]).health == 4); // the boss's own minion as well
        CHECK(HV(*m, 1).powerUsed);
        CHECK(!LegalHasPower(*m, 1));
        CHECK(m->Apply(1, Power(*m, 1)) != ActionError::None);
        // It costs nothing and fires at the start of every boss turn, swapping back.
        CHECK_EQ(HV(*m, 1).mana, 10);
        ToSide0(*m);
        ToSide1(*m);
        CHECK(MV(*m, mine[0]).attack == 4 && MV(*m, mine[0]).health == 3);
        CHECK_EQ(PowerEvents(*m, 1), before + 2);
        // It comes after the draw of the turn.
        std::unique_ptr<Match> n = Fresh("", "NAX13_02");
        n->Apply(0, End());
        uint32_t drawSeq = 0, powerSeq = 0;
        for (Event const& e : n->ViewFor(0, 0).events)
        {
            if (e.type == EventType::Draw && e.side == 1)
                drawSeq = e.seq;
            if (e.type == EventType::HeroPower && e.side == 1)
                powerSeq = e.seq;
        }
        CHECK(drawSeq != 0 && powerSeq > drawSeq);
        // Another side's turn start swaps nothing.
        std::unique_ptr<Match> o = Fresh();
        TA::SetBoard(*o, 0, {kYeti});
        o->Apply(0, End());
        CHECK(MV(*o, TA::BoardIds(*o, 0)[0]).attack == 4);
        // Supercharge: +2 Health for the caster's minions only.
        std::unique_ptr<Match> s = Fresh();
        Ids a = TA::SetBoard(*s, 0, {kYeti, kMurloc});
        Ids b = TA::SetBoard(*s, 1, {kYeti});
        CHECK(Cast(*s, 0, "NAX13_03") == ActionError::None);
        CHECK(MV(*s, a[0]).health == 7 && MV(*s, a[1]).health == 3 && MV(*s, b[0]).health == 5);
        CHECK(MV(*s, a[0]).attack == 4);
    }

    void TestFrostBreath()
    {
        // [Murloc][Yeti][Champion][Yeti][Murloc]: the Champion and its two neighbours survive.
        std::unique_ptr<Match> m = Fresh("", "NAX14_02");
        Ids b = TA::SetBoard(*m, 0, {kMurloc, kYeti, "NAX14_03", kYeti, kMurloc});
        MinionView const champ = MV(*m, b[2]);
        CHECK(champ.attack == 2 && champ.health == 10 && champ.frozen);
        CHECK(Db().Find("NAX14_03")->cost == 5);
        m->Apply(0, End());
        CHECK(Board(*m, 0) == (Strs{kYeti, "NAX14_03", kYeti}));
        CHECK_EQ(PowerEvents(*m, 1), 1);
        CHECK_EQ(HV(*m, 1).mana, 10);
        // The Champion stays Frozen through the player's turns (it never thaws) and keeps protecting.
        ToSide0(*m);
        CHECK(MV(*m, b[2]).frozen);
        TA::ReadyAll(*m);
        CHECK(!MV(*m, b[2]).canAttack);
        ToSide1(*m);
        CHECK(Board(*m, 0) == (Strs{kYeti, "NAX14_03", kYeti}));
        // Minions that are put next to nothing protected die, those next to the Champion live.
        ToSide0(*m);
        Mana(*m, 0);
        Ids h = TA::SetHand(*m, 0, {kMurloc, kMurloc});
        Action far = Play(h[0]);
        far.position = 0;
        CHECK(m->Apply(0, far) == ActionError::None);
        far = Play(h[1]);
        far.position = 0;
        CHECK(m->Apply(0, far) == ActionError::None);
        CHECK_EQ(Board(*m, 0).size(), 5u);
        ToSide1(*m);
        CHECK(Board(*m, 0) == (Strs{kYeti, "NAX14_03", kYeti}));
        // The boss's own minions are not touched.
        std::unique_ptr<Match> n = Fresh("", "NAX14_02");
        TA::SetBoard(*n, 1, {kYeti, kMurloc});
        TA::SetBoard(*n, 0, {kMurloc});
        n->Apply(0, End());
        CHECK_EQ(Board(*n, 1).size(), 2u);
        CHECK(Board(*n, 0).empty());
        // A Frozen minion is spared: it attacked, then the player's own Frostbolt froze it and it stays frozen through the
        // turn change.
        std::unique_ptr<Match> o = Fresh("", "NAX14_02");
        Ids t = TA::SetBoard(*o, 0, {kYeti, kMurloc, kYeti});
        CHECK(o->Apply(0, Attack(t[0], Hero(*o, 1))) == ActionError::None);
        CHECK(Cast(*o, 0, kFrostbolt, t[0]) == ActionError::None);
        CHECK(MV(*o, t[0]).frozen);
        o->Apply(0, End());
        CHECK(Board(*o, 0) == (Strs{kYeti}));
        CHECK(MV(*o, t[0]).frozen);
        // Silenced, the Champion thaws and protects nobody: the whole board dies.
        std::unique_ptr<Match> p = Fresh("", "NAX14_02");
        Ids c = TA::SetBoard(*p, 0, {kYeti, "NAX14_03", kYeti});
        CHECK(Cast(*p, 0, kSilence, c[1]) == ActionError::None);
        CHECK(!MV(*p, c[1]).frozen);
        p->Apply(0, End());
        CHECK(Board(*p, 0).empty());
        // Two Champions: [Y][C][Y][C][Y] keeps all five.
        std::unique_ptr<Match> q = Fresh("", "NAX14_02");
        TA::SetBoard(*q, 0, {kYeti, "NAX14_03", kYeti, "NAX14_03", kYeti});
        q->Apply(0, End());
        CHECK_EQ(Board(*q, 0).size(), 5u);
        // A Champion on any board is Frozen from the start.
        std::unique_ptr<Match> r = Fresh();
        Ids x = TA::SetBoard(*r, 1, {"NAX14_03"});
        CHECK(MV(*r, x[0]).frozen);
    }

    void TestPureColdAndFrostBlast()
    {
        std::unique_ptr<Match> m = Fresh();
        CHECK(Cast(*m, 0, "NAX14_04") == ActionError::None);
        CHECK_EQ(HP(*m, 1), 22);
        CHECK(HV(*m, 1).frozen);
        TA::SetBoard(*m, 0, {"CS2_142"}); // Spell Damage +1
        CHECK(Cast(*m, 0, "NAX14_04") == ActionError::None);
        CHECK_EQ(HP(*m, 1), 13);
        // Armor soaks it.
        std::unique_ptr<Match> n = Fresh();
        TA::SetHero(*n, 1, 30, 5);
        CHECK(Cast(*n, 0, "NAX14_04") == ActionError::None);
        CHECK(HP(*n, 1) == 27 && HV(*n, 1).armor == 0);
        // Frost Blast: automatic at the start of Kel'Thuzad's turn, 2 damage and Freeze for the enemy hero.
        std::unique_ptr<Match> k = Fresh("", "NAX15_02");
        TA::SetWeapon(*k, 0, kAxe);
        CHECK(HV(*k, 1).powerUsed);
        CHECK(!LegalHasPower(*k, 1));
        k->Apply(0, End());
        CHECK_EQ(HP(*k, 0), 28);
        CHECK(HV(*k, 0).frozen);
        CHECK_EQ(PowerEvents(*k, 1), 1);
        ToSide0(*k);
        CHECK_EQ(HP(*k, 0), 28);
        CHECK(HV(*k, 0).frozen && !HV(*k, 0).canAttack); // the weapon is useless
        TA::ReadyAll(*k);
        CHECK(k->Apply(0, Attack(Hero(*k, 0), Hero(*k, 1))) == ActionError::Attack);
        k->Apply(0, End());
        CHECK_EQ(HP(*k, 0), 26);
        ToSide0(*k);
        CHECK(HV(*k, 0).frozen);
        CHECK_EQ(HV(*k, 1).mana, 10); // it costs nothing
    }

    // ---- Kel'Thuzad's second phase ----------------------------------------------------------------------------

    std::unique_ptr<Match> FreshKt(uint32_t seed = 11, int armor = 10)
    {
        return Fresh("", "NAX15_02", seed, 30, armor);
    }

    void TestPhaseTwoScript()
    {
        Detail::Script const* sc = Detail::FindScript("NAX15_02");
        CHECK(sc && sc->autoCast && sc->phase.present);
        CHECK(sc && sc->phase.powerId == "NAX15_04" && sc->phase.summonId == "NAX15_03n");
        CHECK(sc && sc->phase.summons == 2 && sc->phase.forcedTurn == 11);
        // The encounter data says the same (CheckEncounter compares both).
        Hs::Encounter const* e = Hs::FindEncounter("naxx_kelthuzad");
        CHECK(e && e->phase2.heroPowerId == "NAX15_04" && e->phase2.summons.size() == 2 && e->phase2.forcedTurn == 11);
        CHECK(e && e->heroArmor == 10 && e->heroHealth == 30);
        CHECK(Db().Find("NAX15_03n")->attack == 3 && Db().Find("NAX15_03n")->health == 3 && Db().Find("NAX15_03n")->taunt);
        CHECK(Db().Find("NAX15_04")->cost == 8);
    }

    // The Armor breaks in the middle of the player's turn: the action finishes (Deathrattles included), the turn ends at once.
    void TestPhaseTwoArmorBreakEndsTurn()
    {
        std::unique_ptr<Match> m = FreshKt();
        TA::SetHero(*m, 1, 30, 3);
        TA::SetBoard(*m, 0, {"FP1_002", kYeti});
        TA::SetBoard(*m, 1, {kMurloc});
        CHECK(PowerOf(*m, 1) == "NAX15_02");
        // Hellfire: 3 damage to everything: the Armor (3) is gone, the Creeper dies and leaves its Spiders first.
        Ids hand = TA::SetHand(*m, 0, {"CS2_062", kFireball});
        Mana(*m, 0);
        CHECK(m->Apply(0, Play(hand[0])) == ActionError::None);
        CHECK_EQ(m->ActiveSide(), 1);
        CHECK_EQ(m->Turn(), 2);
        CHECK(Board(*m, 1) == (Strs{"NAX15_03n", "NAX15_03n"})); // the Murloc died of the Hellfire
        CHECK(Board(*m, 0) == (Strs{"FP1_002t", "FP1_002t", kYeti}));
        CHECK(PowerOf(*m, 1) == "NAX15_04");
        CHECK(!HV(*m, 1).powerUsed);
        CHECK(HV(*m, 1).armor == 0 && HP(*m, 1) == 30 && HV(*m, 1).maxHealth == 30); // no reset: Hellfire hit the Armor only
        // The boss's turn started normally; Frost Blast did not fire (the hero power is Chains now).
        CHECK_EQ(HV(*m, 1).mana, 10);
        CHECK_EQ(HP(*m, 0), 27);
        CHECK(!HV(*m, 0).frozen);
        CHECK_EQ(PowerEvents(*m, 1), 0);
        // The Guardians can attack at once (they were there when the turn began): 3/3 Taunt.
        MinionView const g = MV(*m, TA::BoardIds(*m, 1)[0]);
        CHECK(g.attack == 3 && g.health == 3 && g.taunt && g.canAttack);
        // The leftover Fireball stays in the hand; the player may not act any more.
        CHECK_EQ(Hand(*m, 0).size(), 1u);
        CHECK(m->Apply(0, Play(hand[1], Hero(*m, 1))) == ActionError::Turn);
        // A trigger event announces the change.
        bool announced = false;
        for (Event const& e : m->ViewFor(0, 0).events)
            if (e.type == EventType::Trigger && e.cardId == "NAX15_04")
                announced = true;
        CHECK(announced);
        // It happens once: the Armor going again does nothing special.
        TA::SetHero(*m, 1, 27, 4);
        CHECK(Cast(*m, 1, kFireball, Hero(*m, 1)) == ActionError::None);
        CHECK(HP(*m, 1) == 25 && HV(*m, 1).armor == 0);
        CHECK(m->ActiveSide() == 1 && Board(*m, 1).size() == 2u && PowerOf(*m, 1) == "NAX15_04");
        std::unique_ptr<Match> fresh = FreshKt();
        CHECK(m->DebugState().find("done") != std::string::npos);
        CHECK(fresh->DebugState().find("done") == std::string::npos);
    }

    void TestPhaseTwoByAttackAndOverkill()
    {
        // A minion attack that breaks the Armor, with the damage going on into the Health: no reset, one phase.
        std::unique_ptr<Match> m = FreshKt();
        TA::SetHero(*m, 1, 30, 3);
        Ids a = TA::SetBoard(*m, 0, {kYeti, kMurloc});
        CHECK(m->Apply(0, Attack(a[0], Hero(*m, 1))) == ActionError::None);
        CHECK_EQ(m->ActiveSide(), 1);
        CHECK_EQ(HP(*m, 1), 29);
        CHECK_EQ(Board(*m, 1).size(), 2u);
        // A spell that overkills the Armor.
        std::unique_ptr<Match> n = FreshKt();
        TA::SetHero(*n, 1, 30, 3);
        CHECK(Cast(*n, 0, kFireball, Hero(*n, 1)) == ActionError::None);
        CHECK_EQ(HP(*n, 1), 27);
        CHECK(PowerOf(*n, 1) == "NAX15_04" && n->ActiveSide() == 1);
        // Damage that leaves Armor behind changes nothing.
        std::unique_ptr<Match> o = FreshKt();
        CHECK(Cast(*o, 0, kFireball, Hero(*o, 1)) == ActionError::None);
        CHECK(HV(*o, 1).armor == 4 && HP(*o, 1) == 30);
        CHECK(PowerOf(*o, 1) == "NAX15_02" && o->ActiveSide() == 0 && Board(*o, 1).empty());
        // A killing blow through the Armor ends the match, no phase.
        std::unique_ptr<Match> p = FreshKt();
        TA::SetHero(*p, 1, 2, 3);
        CHECK(Cast(*p, 0, kFireball, Hero(*p, 1)) == ActionError::None);
        CHECK(p->GetPhase() == Phase::Over && p->GetResult().winner == 0);
    }

    void TestPhaseTwoOnOwnTurn()
    {
        // The Armor breaks on Kel'Thuzad's own turn (his Deathcharger dies): no turn ends, the phase starts at once.
        std::unique_ptr<Match> m = FreshKt();
        ToSide1(*m);
        TA::SetHero(*m, 1, 30, 3);
        Ids dc = TA::SetBoard(*m, 1, {"FP1_006"});
        int const turn = m->Turn();
        CHECK(Cast(*m, 1, kFireball, dc[0]) == ActionError::None);
        CHECK_EQ(m->ActiveSide(), 1);
        CHECK_EQ(m->Turn(), turn);
        CHECK(PowerOf(*m, 1) == "NAX15_04");
        CHECK(Board(*m, 1) == (Strs{"NAX15_03n", "NAX15_03n"}));
        CHECK(HV(*m, 1).armor == 0 && HP(*m, 1) == 30);
        // The Guardians summoned this turn cannot attack yet; Chains is fresh.
        CHECK(!MV(*m, TA::BoardIds(*m, 1)[0]).canAttack);
        CHECK(!HV(*m, 1).powerUsed);
    }

    // The Armor still stands: Kel'Thuzad's 11th own turn forces the phase (before the hero power, so Frost Blast does not fire).
    void TestPhaseTwoForcedTurn()
    {
        std::unique_ptr<Match> m = FreshKt();
        for (int round = 1; round <= 10; ++round)
        {
            CHECK(m->Apply(0, End()) == ActionError::None); // Kel'Thuzad's turn number `round`
            CHECK(PowerOf(*m, 1) == "NAX15_02");
            CHECK_EQ(HP(*m, 0), 30 - 2 * round);
            CHECK(Board(*m, 1).empty());
            CHECK(m->Apply(1, End()) == ActionError::None);
        }
        CHECK_EQ(m->Turn(), 21);
        CHECK(m->Apply(0, End()) == ActionError::None); // his 11th turn
        CHECK_EQ(m->Turn(), 22);
        CHECK_EQ(m->ActiveSide(), 1);
        CHECK(PowerOf(*m, 1) == "NAX15_04");
        CHECK(Board(*m, 1) == (Strs{"NAX15_03n", "NAX15_03n"}));
        CHECK_EQ(HP(*m, 0), 10); // no Frost Blast on the forced turn
        CHECK(HV(*m, 1).armor == 10 && HP(*m, 1) == 30); // the Armor stays
        CHECK_EQ(PowerEvents(*m, 1), 10);
        // Chains does not freeze: the player's hero thaws after the last Frost Blast.
        CHECK(m->Apply(1, End()) == ActionError::None);
        CHECK(!HV(*m, 0).frozen);
        // The phase happens only once: the Guardians are not summoned again.
        CHECK(m->Apply(0, End()) == ActionError::None);
        CHECK(m->Apply(1, End()) == ActionError::None);
        CHECK_EQ(Board(*m, 1).size(), 2u);
    }

    void TestPhaseTwoBoardRoom()
    {
        // Only one slot free: one Guardian.
        std::unique_ptr<Match> m = FreshKt();
        TA::SetHero(*m, 1, 30, 1);
        TA::SetBoard(*m, 1, Strs(6, kMurloc));
        Ids a = TA::SetBoard(*m, 0, {kYeti});
        CHECK(m->Apply(0, Attack(a[0], Hero(*m, 1))) == ActionError::None);
        CHECK_EQ(Board(*m, 1).size(), 7u);
        CHECK_EQ(CountOf(Board(*m, 1), "NAX15_03n"), 1);
        // A full board: none, and no crash.
        std::unique_ptr<Match> n = FreshKt();
        TA::SetHero(*n, 1, 30, 1);
        TA::SetBoard(*n, 1, Strs(7, kMurloc));
        Ids b = TA::SetBoard(*n, 0, {kYeti});
        CHECK(n->Apply(0, Attack(b[0], Hero(*n, 1))) == ActionError::None);
        CHECK_EQ(CountOf(Board(*n, 1), "NAX15_03n"), 0);
        CHECK(PowerOf(*n, 1) == "NAX15_04");
    }

    void TestChains()
    {
        std::unique_ptr<Match> m = FreshKt();
        TA::SetHero(*m, 1, 30, 3);
        Ids mine = TA::SetBoard(*m, 0, {kYeti, kMurloc, "CS2_187"});
        Ids hand = TA::SetHand(*m, 0, {kFireball});
        Mana(*m, 0);
        CHECK(m->Apply(0, Play(hand[0], Hero(*m, 1))) == ActionError::None);
        CHECK(PowerOf(*m, 1) == "NAX15_04" && m->ActiveSide() == 1);
        TA::SetMana(*m, 1, 8, 10);
        TA::SetHeroPowerUsed(*m, 1, false);
        CHECK(LegalHasPower(*m, 1));
        CHECK(m->Apply(1, Power(*m, 1)) == ActionError::None);
        CHECK_EQ(HV(*m, 1).mana, 0);
        CHECK_EQ(Board(*m, 0).size(), 2u);
        CHECK_EQ(Board(*m, 1).size(), 3u);
        EntityId stolen = 0;
        for (EntityId id : mine)
            if (OnBoard(*m, 1, id))
                stolen = id;
        CHECK(stolen != 0);
        CHECK(MV(*m, stolen).canAttack); // it may attack at once
        // Used once per turn.
        TA::SetMana(*m, 1, 10, 10);
        CHECK(m->Apply(1, Power(*m, 1)) == ActionError::Used);
        // At the end of the turn it goes back to its owner.
        ToSide0(*m);
        CHECK_EQ(Board(*m, 0).size(), 3u);
        CHECK_EQ(Board(*m, 1).size(), 2u);
        CHECK(OnBoard(*m, 0, stolen));
        // Not with 7 mana.
        ToSide1(*m);
        TA::SetMana(*m, 1, 7, 10);
        TA::SetHeroPowerUsed(*m, 1, false);
        CHECK(m->Apply(1, Power(*m, 1)) == ActionError::Mana);
        // Not without an enemy minion.
        std::unique_ptr<Match> n = FreshKt();
        TA::SetHero(*n, 1, 30, 1);
        Ids x = TA::SetBoard(*n, 0, {kYeti});
        CHECK(n->Apply(0, Attack(x[0], Hero(*n, 1))) == ActionError::None);
        TA::SetBoard(*n, 0, {});
        CHECK(UsePower(*n, 1) == ActionError::Requirement);
        CHECK(!LegalHasPower(*n, 1));
        // Not with a full board.
        std::unique_ptr<Match> o = FreshKt();
        TA::SetHero(*o, 1, 30, 1);
        Ids y = TA::SetBoard(*o, 0, {kYeti});
        CHECK(o->Apply(0, Attack(y[0], Hero(*o, 1))) == ActionError::None);
        TA::SetBoard(*o, 1, Strs(7, kMurloc));
        CHECK(UsePower(*o, 1) == ActionError::BoardFull);
        // A stolen minion that dies on his turn is simply gone.
        std::unique_ptr<Match> p = FreshKt();
        TA::SetHero(*p, 1, 30, 1);
        Ids z = TA::SetBoard(*p, 0, {kYeti});
        CHECK(p->Apply(0, Attack(z[0], Hero(*p, 1))) == ActionError::None);
        CHECK(UsePower(*p, 1) == ActionError::None);
        CHECK(OnBoard(*p, 1, z[0]));
        CHECK(Kill(*p, 1, z[0]) == ActionError::None);
        ToSide0(*p);
        CHECK(Board(*p, 0).empty());
    }

    // ---- legal actions, the AI and hidden information for wings 4-5 ------------------------------------------------

    void TestLegalActionsWings45()
    {
        for (char const* power : {"NAX10_03", "NAX11_02", "NAX12_02", "NAX15_04"})
        {
            std::unique_ptr<Match> m = Fresh(power);
            TA::SetBoard(*m, 1, {kYeti, kMurloc});
            TA::SetBoard(*m, 0, {kMurloc});
            Mana(*m, 0);
            bool listed = false;
            for (Action const& a : m->LegalActions(0))
                if (a.type == ActionType::HeroPower)
                {
                    listed = true;
                    std::unique_ptr<Match> copy = m->RedactedCopy(0, 5);
                    CHECK(copy->Apply(0, a) == ActionError::None);
                }
            CHECK(listed);
        }
        // Hateful Strike is listed once per minion (two enemy, one own).
        std::unique_ptr<Match> h = Fresh("NAX10_03");
        TA::SetBoard(*h, 1, {kYeti, kMurloc});
        TA::SetBoard(*h, 0, {kMurloc});
        Mana(*h, 0);
        int n = 0;
        for (Action const& a : h->LegalActions(0))
            if (a.type == ActionType::HeroPower)
                ++n;
        CHECK_EQ(n, 3);
        // The automatic powers are never listed and never accepted, for either side.
        for (char const* power : {"NAX13_02", "NAX14_02", "NAX15_02"})
            for (int side = 0; side < 2; ++side)
            {
                std::unique_ptr<Match> m = side == 0 ? Fresh(power) : Fresh("", power);
                if (side == 1)
                    ToSide1(*m);
                TA::SetBoard(*m, 1 - side, {kYeti});
                CHECK(!LegalHasPower(*m, side));
                CHECK(m->Apply(side, Power(*m, side)) != ActionError::None);
                CHECK(HV(*m, side).powerUsed);
            }
        // An automatic power on the side that moves first fires on its first turn, before any action.
        std::unique_ptr<Match> f = Fresh("NAX15_02");
        CHECK_EQ(PowerEvents(*f, 0), 1);
        CHECK_EQ(HP(*f, 1), 28);
        f->Apply(0, End());
        f->Apply(1, End());
        CHECK_EQ(PowerEvents(*f, 0), 2);
        CHECK_EQ(HP(*f, 1), 26);
    }

    void TestAiHeroPowersWings45()
    {
        // Hateful Strike on a minion.
        std::unique_ptr<Match> m = Fresh("", "NAX10_03");
        Ids e = TA::SetBoard(*m, 0, {kYeti, kMurloc});
        ToSide1(*m);
        Action const a = ChooseAiAction(*m, 1, Db(), 0, AiProfile::Hard, 99);
        CHECK(a.type == ActionType::HeroPower);
        CHECK(a.target == e[0] || a.target == e[1]);
        CHECK(m->Apply(1, a) == ActionError::None);
        // The others are never refused over a few actions of the Hard AI.
        for (char const* power : {"NAX11_02", "NAX12_02", "NAX15_04"})
        {
            std::unique_ptr<Match> n = Fresh("", power);
            TA::SetBoard(*n, 0, {kYeti, kMurloc, kYeti});
            TA::SetBoard(*n, 1, {kMurloc});
            ToSide1(*n);
            for (int i = 0; i < 5; ++i)
            {
                Action const b = ChooseAiAction(*n, 1, Db(), i, AiProfile::Hard, 21 + static_cast<uint32_t>(i));
                if (b.type == ActionType::EndTurn)
                    break;
                CHECK(n->Apply(1, b) == ActionError::None);
            }
        }
        // The AI never tries an automatic power as an action.
        for (char const* power : {"NAX13_02", "NAX14_02", "NAX15_02"})
        {
            std::unique_ptr<Match> n = Fresh("", power);
            TA::SetBoard(*n, 0, {kYeti});
            ToSide1(*n);
            TA::SetHand(*n, 1, {kMurloc});
            for (int i = 0; i < 4; ++i)
            {
                Action const b = ChooseAiAction(*n, 1, Db(), i, AiProfile::Hard, 5 + static_cast<uint32_t>(i));
                CHECK(b.type != ActionType::HeroPower);
                if (b.type == ActionType::EndTurn)
                    break;
                CHECK(n->Apply(1, b) == ActionError::None);
            }
        }
    }

    // The AI of the boss simulates on a redacted copy: the phase change and the automatic powers work there and the real
    // match is not touched.
    void TestRedactedCopyWings45()
    {
        std::unique_ptr<Match> m = FreshKt();
        TA::SetHero(*m, 1, 30, 3);
        Ids a = TA::SetBoard(*m, 0, {kYeti});
        TA::SetHand(*m, 0, {kFireball, kYeti});
        TA::SetHand(*m, 1, {kYeti, kYeti});
        std::unique_ptr<Match> c = m->RedactedCopy(0, 77);
        CHECK(c->Apply(0, Attack(a[0], Hero(*c, 1))) == ActionError::None);
        CHECK_EQ(c->ActiveSide(), 1);
        CHECK(PowerOf(*c, 1) == "NAX15_04");
        CHECK(Board(*c, 1) == (Strs{"NAX15_03n", "NAX15_03n"}));
        // The real match: still turn 1, Armor up, Frost Blast.
        CHECK(m->ActiveSide() == 0 && m->Turn() == 1);
        CHECK(HV(*m, 1).armor == 3 && PowerOf(*m, 1) == "NAX15_02" && Board(*m, 1).empty());
        // The copy shows nothing of the boss's hand.
        MatchView const v = c->ViewFor(0, c->LastEventSeq());
        CHECK(v.other.hand.empty());
        for (std::string const& id : Hand(*c, 1))
            CHECK(id == "?");
        // The automatic power of the opponent fires in a copy as well.
        std::unique_ptr<Match> k = Fresh("", "NAX13_02");
        TA::SetBoard(*k, 0, {kYeti});
        std::unique_ptr<Match> kc = k->RedactedCopy(0, 5);
        kc->Apply(0, End());
        CHECK(MV(*kc, TA::BoardIds(*kc, 0)[0]).attack == 5);
        CHECK(MV(*k, TA::BoardIds(*k, 0)[0]).attack == 4);
    }

    void TestInertWings45()
    {
        // No ordinary hero power uses the new script features, and only the three automatic powers, the one phase and the
        // Champion carry them in the whole dataset.
        for (char const* id : {"CS2_034", "CS2_102", "CS1h_001", "DS1h_292", "CS2_056", "CS2_101", "CS2_083b", "CS2_017", "CS2_049"})
        {
            Detail::Script const* sc = Detail::FindScript(id);
            CHECK(sc && !sc->autoCast && !sc->phase.present && !sc->permFrozen);
        }
        int autos = 0, phases = 0, frozen = 0;
        for (MatchAllCards::Row const& row : MatchAllCards::kRows)
        {
            Detail::Script const* sc = Detail::FindScript(row.id);
            if (!sc)
                continue;
            autos += sc->autoCast ? 1 : 0;
            phases += sc->phase.present ? 1 : 0;
            frozen += sc->permFrozen ? 1 : 0;
        }
        CHECK_EQ(autos, 3);
        CHECK_EQ(phases, 1);
        CHECK_EQ(frozen, 1);
        // An ordinary match ends turns without any of it: the state shows no phase.
        std::unique_ptr<Match> m = Fresh();
        m->Apply(0, End());
        m->Apply(1, End());
        CHECK(m->DebugState().find("phase:") == std::string::npos);
    }

    // ---- ordinary matches stay what they were -------------------------------------------------------------------------

    void TestInertForOrdinaryCards()
    {
        // No hero power is passive unless it says so; ordinary classes have no passive power, no aura of the new kinds.
        for (char const* id : {"CS2_034", "CS2_102", "CS1h_001", "DS1h_292", "CS2_056", "CS2_101", "CS2_083b", "CS2_017", "CS2_049"})
        {
            Detail::Script const* sc = Detail::FindScript(id);
            CHECK(sc && !sc->passive);
        }
        // A normal match with the same seed is the same match twice, and the new state shows as before.
        MatchSetup s;
        s.seed = 4;
        s.firstSide = 0;
        s.sides[0].heroClass = "MAGE";
        s.sides[0].deck = std::vector<std::string>(30, kYeti);
        s.sides[1].heroClass = "WARRIOR";
        s.sides[1].deck = std::vector<std::string>(30, kMurloc);
        std::string error;
        std::unique_ptr<Match> a = Match::Create(Db(), s, error);
        std::unique_ptr<Match> b = Match::Create(Db(), s, error);
        CHECK(a && b && a->DebugState() == b->DebugState());
        CHECK(a->ViewFor(0, 0).self.hero.immune == false && a->ViewFor(0, 0).self.hero.powerUsed == false);
    }

    // ---- full encounters, Hard AI against Hard AI ---------------------------------------------------------------------

    // Human decks of the play-outs: plain minions and a few spells.
    Strs HumanDeck(int variant)
    {
        if (variant % 2 == 0)
        {
            char const* const pool[] = {"CS2_168", "CS2_172", "CS2_182", "CS2_179", "CS2_189", "CS2_121", "CS2_029", "CS2_024",
                "CS2_033", "CS2_032"};
            Strs deck;
            for (int i = 0; i < 30; ++i)
                deck.push_back(pool[i % 10]);
            return deck;
        }
        char const* const pool[] = {"CS2_168", "CS2_122", "CS2_141", "CS2_142", "CS2_147", "CS2_124", "CS2_127", "CS2_131",
            "CS2_093", "CS2_200"};
        Strs deck;
        for (int i = 0; i < 30; ++i)
            deck.push_back(pool[i % 10]);
        return deck;
    }

    struct GameStats
    {
        int games = 0;
        int ended = 0;
        int refused = 0;
        int bossPower = 0;      // hero power uses by the boss
        int humanPower = 0;
        int bossWins = 0;
        int humanWins = 0;
        int phase2 = 0;         // games in which Kel'Thuzad ended in his second phase
    };

    void PlayEncounterGame(char const* key, uint32_t seed, GameStats& st)
    {
        Hs::Encounter const* e = Hs::FindEncounter(key);
        CHECK(e != nullptr);
        if (!e)
            return;
        Hs::MatchStartParams p;
        p.humanName = "Jonas";
        p.humanClass = (seed % 2 == 0) ? "MAGE" : "PALADIN";
        p.humanDeck = HumanDeck(static_cast<int>(seed));
        p.opponentName = e->name;
        p.seed = seed;
        Hs::ApplyEncounter(*e, p);
        MatchSetup s;
        s.seed = p.seed;
        s.firstSide = p.firstSide;
        s.sides[0].name = p.humanName;
        s.sides[0].heroClass = p.humanClass;
        s.sides[0].deck = p.humanDeck;
        s.sides[0].start = p.humanStart;
        s.sides[1].name = p.opponentName;
        s.sides[1].heroClass = p.opponentClass;
        s.sides[1].deck = p.opponentDeck;
        s.sides[1].heroHealth = p.opponentHeroHealth;
        s.sides[1].heroArmor = p.opponentHeroArmor;
        s.sides[1].heroCardId = p.opponentHeroCardId;
        s.sides[1].heroPowerId = p.opponentHeroPowerId;
        s.sides[1].start = p.opponentStart;
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        CHECK(m != nullptr);
        if (!m)
        {
            std::printf("  %s: create failed: %s\n", key, error.c_str());
            return;
        }
        ++st.games;
        for (int side = 0; side < 2; ++side)
        {
            Action const a = ChooseAiAction(*m, side, Db(), 0, AiProfile::Hard, seed * 31u + static_cast<uint32_t>(side));
            if (a.type != ActionType::Mulligan || m->Apply(side, a) != ActionError::None)
                ++st.refused;
        }
        int guard = 0;
        int actions = 0;
        int turn = m->Turn();
        while (m->GetPhase() == Phase::Play && ++guard < 6000)
        {
            int const side = m->ActiveSide();
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                actions = 0;
            }
            Action const a = ChooseAiAction(*m, side, Db(), actions, AiProfile::Hard, seed * 7919u + static_cast<uint32_t>(guard));
            if (m->Apply(side, a) != ActionError::None)
            {
                ++st.refused;
                std::printf("  %s seed %u: refused action type %d on side %d (turn %d)\n", key, seed, static_cast<int>(a.type), side,
                    m->Turn());
                break;
            }
            ++actions;
        }
        if (m->GetPhase() == Phase::Over)
        {
            ++st.ended;
            if (m->GetResult().winner == 1)
                ++st.bossWins;
            else if (m->GetResult().winner == 0)
                ++st.humanWins;
        }
        for (Event const& ev : m->ViewFor(0, 0).events)
        {
            if (ev.type == EventType::HeroPower)
                (ev.side == 1 ? st.bossPower : st.humanPower)++;
        }
        if (PowerOf(*m, 1) == "NAX15_04")
            ++st.phase2;
        CHECK(m->GetPhase() == Phase::Over);
    }

    int GamesPerEncounter() { return 4; }

    void RunEncounter(char const* key, bool expectPower)
    {
        GameStats st;
        for (int i = 1; i <= GamesPerEncounter(); ++i)
            PlayEncounterGame(key, static_cast<uint32_t>(100 + i), st);
        if (std::string(key) == "naxx_kelthuzad")
            std::printf("  kelthuzad: second phase reached in %d of %d games\n", st.phase2, st.games);
        CHECK_EQ(st.games, GamesPerEncounter());
        CHECK_EQ(st.ended, st.games);
        CHECK_EQ(st.refused, 0);
        if (expectPower)
            CHECK(st.bossPower > 0);
        else
            CHECK_EQ(st.bossPower, 0);
        std::printf("  %-18s %d games, boss %d / human %d wins, boss hero power uses %d\n", key, st.games, st.bossWins, st.humanWins,
            st.bossPower);
    }

    void TestSmokeAnubrekhan() { RunEncounter("naxx_anubrekhan", true); }
    void TestSmokeFaerlina() { RunEncounter("naxx_faerlina", true); }
    void TestSmokeMaexxna() { RunEncounter("naxx_maexxna", true); }
    void TestSmokeNoth() { RunEncounter("naxx_noth", false); }
    void TestSmokeHeigan() { RunEncounter("naxx_heigan", true); }
    void TestSmokeLoatheb() { RunEncounter("naxx_loatheb", true); }
    void TestSmokeRazuvious() { RunEncounter("naxx_razuvious", true); }
    void TestSmokeGothik() { RunEncounter("naxx_gothik", true); }
    void TestSmokeHorsemen() { RunEncounter("naxx_horsemen", true); }
    void TestSmokePatchwerk() { RunEncounter("naxx_patchwerk", true); }
    void TestSmokeGrobbulus() { RunEncounter("naxx_grobbulus", true); }
    void TestSmokeGluth() { RunEncounter("naxx_gluth", true); }
    void TestSmokeThaddius() { RunEncounter("naxx_thaddius", true); }
    void TestSmokeSapphiron() { RunEncounter("naxx_sapphiron", true); }
    void TestSmokeKelThuzad() { RunEncounter("naxx_kelthuzad", true); }

    // Razuvious: the Crystal in the human's opening hand is used by the Hard AI at some point in a normal game, and the
    // Understudies end up on the human side.
    void TestRazuviousCrystal()
    {
        int used = 0;
        int games = 0;
        for (uint32_t seed = 201; seed <= 206; ++seed)
        {
            Hs::Encounter const* e = Hs::FindEncounter("naxx_razuvious");
            MatchSetup s;
            s.seed = seed;
            s.firstSide = -1;
            s.sides[0].name = "Jonas";
            s.sides[0].heroClass = "MAGE";
            s.sides[0].deck = HumanDeck(0);
            s.sides[0].start = e->human;
            s.sides[1].name = e->name;
            s.sides[1].heroClass = e->heroClass;
            s.sides[1].deck = Hs::ExpandEncounterDeck(*e);
            s.sides[1].heroHealth = e->heroHealth;
            s.sides[1].heroCardId = e->heroCardId;
            s.sides[1].heroPowerId = e->heroPowerId;
            s.sides[1].start = e->boss;
            std::string error;
            std::unique_ptr<Match> m = Match::Create(Db(), s, error);
            CHECK(m != nullptr);
            if (!m)
                return;
            ++games;
            // The Crystal is in the human's hand from the start, outside the mulligan.
            CHECK(CountOf(Hand(*m, 0), "NAX7_05") == 1);
            CHECK(Board(*m, 1) == (Strs{"NAX7_02", "NAX7_02"}));
            for (int side = 0; side < 2; ++side)
                m->Apply(side, ChooseAiAction(*m, side, Db(), 0, AiProfile::Hard, seed + static_cast<uint32_t>(side)));
            int guard = 0;
            int actions = 0;
            int turn = m->Turn();
            while (m->GetPhase() == Phase::Play && m->Turn() <= 6 && ++guard < 800)
            {
                int const side = m->ActiveSide();
                if (m->Turn() != turn)
                {
                    turn = m->Turn();
                    actions = 0;
                }
                Action const a = ChooseAiAction(*m, side, Db(), actions, AiProfile::Hard, seed * 13u + static_cast<uint32_t>(guard));
                CHECK(m->Apply(side, a) == ActionError::None);
                ++actions;
            }
            for (Event const& ev : m->ViewFor(0, 0).events)
                if (ev.type == EventType::Play && ev.cardId == "NAX7_05")
                    ++used;
        }
        CHECK_EQ(games, 6);
        CHECK(used > 0);
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    RUN(TestCoverage);
    RUN(TestDeathcharger);
    RUN(TestNecroknight);
    RUN(TestSkeletalSmith);
    RUN(TestSkitter);
    RUN(TestLocustSwarm);
    RUN(TestRainOfFire);
    RUN(TestWorshipper);
    RUN(TestWebWrap);
    RUN(TestNecroticPoison);
    RUN(TestRaiseDead);
    RUN(TestPlague);
    RUN(TestEruption);
    RUN(TestMindpocalypse);
    RUN(TestNecroticAura);
    RUN(TestDeathbloomAndSporeburst);
    RUN(TestUnbalancingStrike);
    RUN(TestMassiveRuneblade);
    RUN(TestMindControlCrystal);
    RUN(TestHarvest);
    RUN(TestUnrelentingAndSpectral);
    RUN(TestHorsemenImmune);
    RUN(TestRuneblade);
    RUN(TestMarkOfTheHorsemen);
    RUN(TestHatefulStrike);
    RUN(TestHook);
    RUN(TestPoisonCloud);
    RUN(TestMutatingInjection);
    RUN(TestDecimate);
    RUN(TestJawsAndEnrage);
    RUN(TestPolarityShiftAndSupercharge);
    RUN(TestFrostBreath);
    RUN(TestPureColdAndFrostBlast);
    RUN(TestPhaseTwoScript);
    RUN(TestPhaseTwoArmorBreakEndsTurn);
    RUN(TestPhaseTwoByAttackAndOverkill);
    RUN(TestPhaseTwoOnOwnTurn);
    RUN(TestPhaseTwoForcedTurn);
    RUN(TestPhaseTwoBoardRoom);
    RUN(TestChains);
    RUN(TestLegalActions);
    RUN(TestLegalActionsWings45);
    RUN(TestAiHeroPowers);
    RUN(TestAiHeroPowersWings45);
    RUN(TestRedactedCopy);
    RUN(TestRedactedCopyWings45);
    RUN(TestInertForOrdinaryCards);
    RUN(TestInertWings45);
    RUN(TestRazuviousCrystal);
    RUN(TestSmokeAnubrekhan);
    RUN(TestSmokeFaerlina);
    RUN(TestSmokeMaexxna);
    RUN(TestSmokeNoth);
    RUN(TestSmokeHeigan);
    RUN(TestSmokeLoatheb);
    RUN(TestSmokeRazuvious);
    RUN(TestSmokeGothik);
    RUN(TestSmokeHorsemen);
    RUN(TestSmokePatchwerk);
    RUN(TestSmokeGrobbulus);
    RUN(TestSmokeGluth);
    RUN(TestSmokeThaddius);
    RUN(TestSmokeSapphiron);
    RUN(TestSmokeKelThuzad);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed != 0)
    {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("all encounter card checks passed\n");
    return 0;
}
