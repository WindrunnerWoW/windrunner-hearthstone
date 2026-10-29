// Standalone unit tests of the match engine and AI (src/match/HsMatch*.cpp; no core headers).
//   match_tests.exe            run all tests
// Card definitions are built by hand with the real Hearthstone 1.0.0.5834 stats of the cards used.
// Exit code 0 = all checks passed.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "match_test_cards.h"

#include <algorithm>
#include <cstdio>
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
    using MatchTestCards::Db;
    using MatchTestCards::Fixture;
    using MatchTestCards::kRows;
    using MatchTestCards::Row;

    std::vector<std::string> Filler(int n, char const* id = "CS2_168")
    {
        return std::vector<std::string>(static_cast<std::size_t>(n), id);
    }

    std::unique_ptr<Match> Make(std::vector<std::string> const& d0, std::vector<std::string> const& d1, int first = 0,
                                char const* c0 = "MAGE", char const* c1 = "WARRIOR", uint32_t seed = 7)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = first;
        s.sides[0].name = "Jonas";
        s.sides[0].heroClass = c0;
        s.sides[0].deck = d0;
        s.sides[1].name = "Innkeeper";
        s.sides[1].heroClass = c1;
        s.sides[1].deck = d1;
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (!m)
            std::printf("  create failed: %s\n", error.c_str());
        return m;
    }

    Action Mull(std::vector<EntityId> replace = {})
    {
        Action a;
        a.type = ActionType::Mulligan;
        a.replace = replace;
        return a;
    }

    Action Play(EntityId source, EntityId target = 0, int position = -1)
    {
        Action a;
        a.type = ActionType::Play;
        a.source = source;
        a.target = target;
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

    Action Power(EntityId source, EntityId target = 0)
    {
        Action a;
        a.type = ActionType::HeroPower;
        a.source = source;
        a.target = target;
        return a;
    }

    Action Simple(ActionType t)
    {
        Action a;
        a.type = t;
        return a;
    }

    // A match in Phase::Play, side 0 active on turn 1 with an empty hand and board on both sides,
    // 20 Murloc Raiders in each deck and 10 mana for side 0.
    std::unique_ptr<Match> Scenario(char const* c0 = "MAGE", char const* c1 = "WARRIOR")
    {
        std::unique_ptr<Match> m = Make(Filler(20), Filler(20), 0, c0, c1);
        m->Apply(0, Mull());
        m->Apply(1, Mull());
        TA::SetHand(*m, 0, {});
        TA::SetHand(*m, 1, {});
        TA::SetMana(*m, 0, 10, 10);
        return m;
    }

    std::vector<Event> NewEvents(Match const& m, uint32_t& mark, int side = 0)
    {
        MatchView const v = m.ViewFor(side, mark);
        mark = m.LastEventSeq();
        return v.events;
    }

    int CountEvents(std::vector<Event> const& evs, EventType t)
    {
        int n = 0;
        for (Event const& e : evs)
            if (e.type == t)
                ++n;
        return n;
    }

    MinionView const* MinionOf(SideView const& s, EntityId id)
    {
        for (MinionView const& m : s.board)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    bool InLegal(MatchView const& v, Action const& a)
    {
        for (Action const& l : v.legal)
            if (l.type == a.type && l.source == a.source && l.target == a.target)
                return true;
        return false;
    }

    // ---- setup, mulligan, turns ------------------------------------------------

    void TestCreateValidation()
    {
        std::string error;
        MatchSetup s;
        s.sides[0].heroClass = "MAGE";
        s.sides[1].heroClass = "WARRIOR";
        s.sides[0].deck = Filler(30);
        s.sides[1].deck = Filler(30);
        CHECK(Match::Create(Db(), s, error) != nullptr && error.empty());
        s.sides[1].heroClass = "DEATHKNIGHT";
        CHECK(!Match::Create(Db(), s, error) && !error.empty());
        s.sides[1].heroClass = "warrior"; // case-insensitive
        CHECK(Match::Create(Db(), s, error) != nullptr);
        s.sides[1].deck = {};
        CHECK(!Match::Create(Db(), s, error));
        s.sides[1].deck = Filler(61);
        CHECK(!Match::Create(Db(), s, error));
        s.sides[1].deck = Filler(30);
        s.sides[1].deck[3] = "EX1_323"; // Lord Jaraxxus: unsupported in this test set (no hero card / weapon tokens)
        CHECK(!Match::Create(Db(), s, error) && error.find("EX1_323") != std::string::npos);
        s.sides[1].deck[3] = "NOPE_1";
        CHECK(!Match::Create(Db(), s, error));
        s.sides[1].deck[3] = "CS2_102"; // a hero power is no deck card
        CHECK(!Match::Create(Db(), s, error));
        s.sides[1].deck[3] = "CS2_168";
        s.firstSide = 2;
        CHECK(!Match::Create(Db(), s, error));

        // An engine card missing from the database.
        CardDb small;
        for (Row const& r : kRows)
            if (std::string(r.id) != "GAME_005")
            {
                CardDef d;
                d.id = r.id;
                d.type = ParseCardType(r.type);
                d.attack = r.attack;
                d.health = r.health;
                small.Add(d);
            }
        s.firstSide = -1;
        CHECK(!Match::Create(small, s, error) && error.find("GAME_005") != std::string::npos);
    }

    void TestSupported()
    {
        CardDb const& db = Db();
        for (char const* id : {"EX1_277", "CS2_025", "CS2_023", "CS2_029", "CS2_022", "EX1_015", "CS2_122", "EX1_593", "CS2_108",
                 "EX1_400", "CS2_105", "CS2_114", "CS2_103", "EX1_606", "EX1_084", "NEW1_009", "GAME_005", "CS2_168", "CS2_179",
                 "CS2_124", "CS2_200", "CS2_106", "CS2_112", "CS2_082", "EX1_008", "CS2_169", "CS2_142", "CS2_052", "CS2_051", "CS2_tk1",
                 "CS2_034", "CS2_102", "CS1h_001", "DS1h_292", "CS2_056", "CS2_101", "CS2_083b", "CS2_017", "CS2_049"})
            CHECK(db.Supported(id));
        CHECK(!db.Supported("EX1_323")); // Lord Jaraxxus: unsupported in THIS test set (its hero card and weapon are missing)
        CHECK(!db.Supported("EX1_161o"));
        CHECK(!db.Supported("NOPE"));
        for (std::string const& f : Fixture(true))
            CHECK(db.Supported(f));
        for (std::string const& f : Fixture(false))
            CHECK(db.Supported(f));
        for (std::string const& id : EngineCardIds())
            CHECK(db.Find(id) != nullptr);

        Detail::Keywords k;
        CHECK(Detail::ParseKeywordText("<b>Taunt</b>. <b>Divine Shield</b>", k) && k.taunt && k.divineShield);
        CHECK(Detail::ParseKeywordText("<b>Windfury, Charge, Divine Shield, Taunt</b>", k) && k.windfury && k.charge && k.taunt);
        CHECK(Detail::ParseKeywordText("<b>Spell Damage +5</b>", k) && k.spellDamage == 5);
        CHECK(Detail::ParseKeywordText("", k));
        CHECK(!Detail::ParseKeywordText("<b>Battlecry:</b> Draw a card.", k));
        CHECK(Detail::KeywordsOf(*db.Find("CS2_052")).spellDamage == 1);

        CHECK(HeroCardOf("mage") == "HERO_08" && HeroPowerOf("MAGE") == "CS2_034");
        CHECK(HeroCardOf("WARRIOR") == "HERO_01" && HeroPowerOf("PRIEST") == "CS1h_001");
        CHECK(HeroCardOf("NEUTRAL").empty());
        CHECK(ParseCardType("HERO_POWER") == CardType::HeroPower && ParseCardType("minion") == CardType::Minion);
        CHECK(std::string(ActionErrorCode(ActionError::BoardFull)) == "board" && std::string(ActionErrorCode(ActionError::None)).empty());
        CHECK(std::string(EventTypeCode(EventType::ShieldPop)) == "pop" && std::string(EventTypeCode(EventType::GameOver)) == "end");
    }

    void TestOpeningAndCoin()
    {
        for (int first = 0; first < 2; ++first)
        {
            std::unique_ptr<Match> m = Make(Fixture(true), Fixture(false), first);
            CHECK(m->GetPhase() == Phase::Mulligan && m->Turn() == 0);
            MatchView const vf = m->ViewFor(first, 0);
            MatchView const vs = m->ViewFor(1 - first, 0);
            CHECK(vf.self.hand.size() == 3 && vf.mulliganPending && vf.mulligan.size() == 3);
            CHECK(vs.self.hand.size() == 4 && vs.mulligan.size() == 4);
            CHECK(vf.self.deckCount == 27 && vs.self.deckCount == 26);
            CHECK(vf.legal.empty() && vs.legal.empty());
            // Nothing but the mulligan (and concede) during the mulligan.
            CHECK(m->Apply(first, Simple(ActionType::EndTurn)) == ActionError::Phase);
            CHECK(m->Apply(first, Play(vf.self.hand[0].id)) == ActionError::Phase);
            CHECK(m->Apply(first, Mull()) == ActionError::None);
            CHECK(m->GetPhase() == Phase::Mulligan);
            CHECK(m->Apply(1 - first, Mull()) == ActionError::None);
            CHECK(m->GetPhase() == Phase::Play && m->Turn() == 1 && m->ActiveSide() == first);
            MatchView const af = m->ViewFor(first, 0);
            MatchView const as = m->ViewFor(1 - first, 0);
            CHECK(af.self.hand.size() == 4 && af.self.hero.mana == 1 && af.self.hero.maxMana == 1);
            CHECK(as.self.hand.size() == 5 && as.self.hand.back().cardId == "GAME_005" && as.self.hero.maxMana == 0);
            CHECK(af.other.handCount == 5 && af.other.hand.empty());
        }
        // The first side is chosen by the seed when not set: both happen.
        std::set<int> firsts;
        for (uint32_t seed = 1; seed < 40; ++seed)
        {
            MatchSetup s;
            s.seed = seed;
            s.sides[0].heroClass = "MAGE";
            s.sides[1].heroClass = "WARRIOR";
            s.sides[0].deck = Fixture(true);
            s.sides[1].deck = Fixture(false);
            std::string error;
            std::unique_ptr<Match> m = Match::Create(Db(), s, error);
            firsts.insert(m->ViewFor(0, 0).self.hand.size() == 3 ? 0 : 1);
        }
        CHECK(firsts.size() == 2);
    }

    void TestMulligan()
    {
        std::unique_ptr<Match> m = Make(Fixture(true), Fixture(false), 0);
        std::vector<EntityId> const offered = m->ViewFor(0, 0).mulligan;
        CHECK(m->Apply(0, Mull({offered[0], offered[0]})) == ActionError::Mulligan);
        CHECK(m->Apply(0, Mull({9999})) == ActionError::Mulligan);
        std::vector<EntityId> const other = m->ViewFor(1, 0).mulligan;
        CHECK(m->Apply(0, Mull({other[0]})) == ActionError::Mulligan); // not ours
        uint32_t const seq = m->Seq();
        CHECK(m->Apply(0, Mull({offered[0], offered[2]})) == ActionError::None);
        CHECK(m->Seq() == seq + 1);
        MatchView v = m->ViewFor(0, 0);
        CHECK(!v.mulliganPending && v.mulligan.empty());
        CHECK(v.self.hand.size() == 3 && v.self.deckCount == 27);
        // The kept card is still there; the deck holds the returned ones.
        CHECK(v.self.hand[0].id == offered[1]);
        std::vector<EntityId> const deck = TA::DeckIds(*m, 0);
        int returned = 0;
        for (EntityId id : deck)
            if (id == offered[0] || id == offered[2])
                ++returned;
        for (HandCardView const& h : v.self.hand)
            if (h.id == offered[0] || h.id == offered[2])
                ++returned;
        CHECK(returned == 2);
        CHECK(m->Apply(0, Mull()) == ActionError::Mulligan); // only once
        // The other side sees that we replaced two cards, but not which.
        std::vector<Event> const evs = m->ViewFor(1, 0).events;
        bool sawMull = false;
        for (Event const& e : evs)
        {
            if (e.type == EventType::Mulligan && e.side == 0)
                sawMull = e.amount == 2;
            if (e.type == EventType::Draw && e.side == 0)
                CHECK(e.cardId.empty());
        }
        CHECK(sawMull);
        CHECK(m->Apply(1, Mull(other)) == ActionError::None); // replace all four
        CHECK(m->ViewFor(1, 0).self.hand.size() == 5);
        CHECK(m->Apply(1, Mull()) == ActionError::Phase);
    }

    void TestManaAndTurns()
    {
        std::unique_ptr<Match> m = Make(Filler(30), Filler(30), 0);
        m->Apply(0, Mull());
        m->Apply(1, Mull());
        for (int t = 1; t <= 24; ++t)
        {
            int const side = (t - 1) % 2;
            CHECK(m->ActiveSide() == side && m->Turn() == t);
            MatchView const v = m->ViewFor(side, 0);
            int const expected = std::min(10, (t + 1) / 2);
            CHECK(v.self.hero.maxMana == expected && v.self.hero.mana == expected);
            CHECK(m->Apply(1 - side, Simple(ActionType::EndTurn)) == ActionError::Turn);
            TA::SetHand(*m, side, {}); // keep the hands from burning
            CHECK(m->Apply(side, Simple(ActionType::EndTurn)) == ActionError::None);
        }
    }

    void TestCoin()
    {
        std::unique_ptr<Match> m = Scenario();
        TA::SetMana(*m, 0, 1, 1);
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"GAME_005", "CS2_172"});
        CHECK(m->Apply(0, Play(h[1])) == ActionError::Mana);
        CHECK(!InLegal(m->ViewFor(0, 0), Play(h[1])));
        uint32_t mark = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.hero.mana == 2 && m->ViewFor(0, 0).self.hero.maxMana == 1);
        CHECK(CountEvents(NewEvents(*m, mark), EventType::ManaGain) == 1);
        CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
        // Never above 10.
        std::vector<EntityId> const c = TA::SetHand(*m, 0, {"GAME_005"});
        TA::SetMana(*m, 0, 10, 10);
        CHECK(m->Apply(0, Play(c[0])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.hero.mana == 10);
    }

    // ---- draws, fatigue, burn ---------------------------------------------------

    void TestFatigue()
    {
        std::unique_ptr<Match> m = Scenario();
        TA::SetDeck(*m, 1, {});
        TA::SetDeck(*m, 0, {});
        TA::SetHero(*m, 1, 30, 2);
        uint32_t mark = m->LastEventSeq();
        CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::None); // side 1 draws: fatigue 1 into armor
        MatchView v = m->ViewFor(1, 0);
        CHECK(v.self.hero.armor == 1 && v.self.hero.health == 30 && v.self.hero.fatigue == 1 && v.self.handCount == 0);
        std::vector<Event> evs = NewEvents(*m, mark, 1);
        CHECK(CountEvents(evs, EventType::Fatigue) == 1);
        m->Apply(1, Simple(ActionType::EndTurn)); // side 0: fatigue 1
        m->Apply(0, Simple(ActionType::EndTurn)); // side 1: fatigue 2 (1 armor, 1 health)
        v = m->ViewFor(1, 0);
        CHECK(v.self.hero.armor == 0 && v.self.hero.health == 29 && v.self.hero.fatigue == 2);
        m->Apply(1, Simple(ActionType::EndTurn));
        m->Apply(0, Simple(ActionType::EndTurn)); // fatigue 3
        CHECK(m->HeroHealth(1) == 26);
        CHECK(m->HeroHealth(0) == 30 - 1 - 2);
        // Fatigue can kill: the match ends on the draw.
        TA::SetHero(*m, 0, 3, 0);
        CHECK(m->Apply(1, Simple(ActionType::EndTurn)) == ActionError::None);
        CHECK(m->GetPhase() == Phase::Over && m->GetResult().winner == 1 && m->GetResult().reason == EndReason::HeroDied);
        CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::Over);
        CHECK(m->ViewFor(0, 0).legal.empty());
    }

    void TestBurn()
    {
        std::unique_ptr<Match> m = Scenario();
        TA::SetHand(*m, 1, Filler(10, "CS2_172"));
        std::vector<EntityId> const deck = TA::SetDeck(*m, 1, {"CS2_200", "CS2_168"});
        uint32_t mark = m->LastEventSeq();
        m->Apply(0, Simple(ActionType::EndTurn));
        std::vector<Event> const evs = NewEvents(*m, mark, 0);
        bool burned = false;
        for (Event const& e : evs)
            if (e.type == EventType::Burn && e.side == 1 && e.target == deck[0] && e.cardId == "CS2_200")
                burned = true; // revealed to the other side too
        CHECK(burned);
        MatchView const v = m->ViewFor(1, 0);
        CHECK(v.self.handCount == 10 && v.self.deckCount == 1);
        // Arcane Intellect with 9 cards: one drawn, one burned.
        std::unique_ptr<Match> n = Scenario();
        std::vector<std::string> nine = Filler(8, "CS2_172");
        nine.push_back("CS2_023");
        std::vector<EntityId> const h = TA::SetHand(*n, 0, nine);
        TA::SetDeck(*n, 0, {"CS2_200", "CS2_120", "CS2_168"});
        CHECK(n->Apply(0, Play(h[8])) == ActionError::None);
        CHECK(n->ViewFor(0, 0).self.handCount == 10 && n->ViewFor(0, 0).self.deckCount == 1);
        CHECK(TA::HandCardIds(*n, 0).back() == "CS2_120");
    }

    // ---- board, minions, combat --------------------------------------------------

    void TestBoardLimit()
    {
        std::unique_ptr<Match> m = Scenario("PALADIN", "WARRIOR");
        TA::SetBoard(*m, 0, Filler(6));
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_172", "CS2_120"});
        CHECK(m->Apply(0, Play(h[0], 0, 7)) == ActionError::Position);
        CHECK(m->Apply(0, Play(h[0], 0, -2)) == ActionError::Position);
        CHECK(m->Apply(0, Play(h[0], 3, -1)) == ActionError::Target); // no target for a vanilla minion
        CHECK(m->Apply(0, Play(h[0], 0, 2)) == ActionError::None);
        CHECK(TA::BoardIds(*m, 0)[2] == h[0]);
        CHECK(m->Apply(0, Play(h[1])) == ActionError::BoardFull);
        MatchView const v = m->ViewFor(0, 0);
        CHECK(!InLegal(v, Play(h[1])));
        CHECK(!InLegal(v, Power(TA::PowerId(*m, 0)))); // Reinforce needs a slot
        CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::BoardFull);
        // Rightmost by default.
        std::unique_ptr<Match> n = Scenario();
        TA::SetBoard(*n, 0, Filler(2));
        std::vector<EntityId> const g = TA::SetHand(*n, 0, {"CS2_172"});
        CHECK(n->Apply(0, Play(g[0])) == ActionError::None);
        CHECK(TA::BoardIds(*n, 0).back() == g[0]);
    }

    void TestChargeAndExhaustion()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_172", "CS2_124"});
        EntityId const enemy = TA::HeroId(*m, 1);
        m->Apply(0, Play(h[0]));
        m->Apply(0, Play(h[1]));
        CHECK(m->Apply(0, Attack(h[0], enemy)) == ActionError::Attack);
        MatchView v = m->ViewFor(0, 0);
        CHECK(!MinionOf(v.self, h[0])->canAttack && MinionOf(v.self, h[1])->canAttack && MinionOf(v.self, h[1])->charge);
        CHECK(m->Apply(0, Attack(h[1], enemy)) == ActionError::None);
        CHECK(m->HeroHealth(1) == 27);
        CHECK(m->Apply(0, Attack(h[1], enemy)) == ActionError::Attack); // once per turn
        m->Apply(0, Simple(ActionType::EndTurn));
        // On the other side's turn nobody of side 0 can attack.
        CHECK(m->Apply(0, Attack(h[0], enemy)) == ActionError::Turn);
        CHECK(!MinionOf(m->ViewFor(0, 0).self, h[0])->canAttack);
        m->Apply(1, Simple(ActionType::EndTurn));
        CHECK(m->Apply(0, Attack(h[0], enemy)) == ActionError::None);
        // A minion with 0 attack never attacks.
        std::unique_ptr<Match> n = Scenario();
        std::vector<EntityId> const b = TA::SetBoard(*n, 0, {"CS2_051"});
        CHECK(n->Apply(0, Attack(b[0], TA::HeroId(*n, 1))) == ActionError::Attack);
    }

    void TestWindfury()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const b = TA::SetBoard(*m, 0, {"CS2_169"});
        EntityId const enemy = TA::HeroId(*m, 1);
        CHECK(m->Apply(0, Attack(b[0], enemy)) == ActionError::None);
        CHECK(MinionOf(m->ViewFor(0, 0).self, b[0])->canAttack);
        CHECK(m->Apply(0, Attack(b[0], enemy)) == ActionError::None);
        CHECK(m->Apply(0, Attack(b[0], enemy)) == ActionError::Attack);
        CHECK(m->HeroHealth(1) == 28);
    }

    void TestTaunt()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const mine = TA::SetBoard(*m, 0, {"CS2_200", "CS2_200"});
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_168", "CS2_179", "CS2_121"});
        EntityId const enemy = TA::HeroId(*m, 1);
        CHECK(m->Apply(0, Attack(mine[0], enemy)) == ActionError::Taunt);
        CHECK(m->Apply(0, Attack(mine[0], theirs[0])) == ActionError::Taunt);
        MatchView const v = m->ViewFor(0, 0);
        CHECK(InLegal(v, Attack(mine[0], theirs[1])) && InLegal(v, Attack(mine[0], theirs[2])));
        CHECK(!InLegal(v, Attack(mine[0], enemy)) && !InLegal(v, Attack(mine[0], theirs[0])));
        // Spells and hero powers ignore Taunt.
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_029"});
        CHECK(InLegal(v, Power(TA::PowerId(*m, 0), enemy)));
        CHECK(m->Apply(0, Play(h[0], enemy)) == ActionError::None);
        CHECK(m->HeroHealth(1) == 24);
        // Kill both taunts: the hero is open again.
        CHECK(m->Apply(0, Attack(mine[0], theirs[2])) == ActionError::None);
        CHECK(m->Apply(0, Attack(mine[1], theirs[1])) == ActionError::None);
        CHECK(TA::BoardIds(*m, 1).size() == 1);
        // Never attack the own side.
        TA::ReadyAll(*m);
        CHECK(m->Apply(0, Attack(mine[0], mine[1])) == ActionError::Target);
        CHECK(m->Apply(0, Attack(mine[0], TA::HeroId(*m, 0))) == ActionError::Target);
        CHECK(m->Apply(0, Attack(theirs[0], mine[0])) == ActionError::Source);
    }

    void TestDivineShieldAndCombat()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const mine = TA::SetBoard(*m, 0, {"CS2_172", "CS2_120", "CS2_200"});
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"EX1_008", "CS2_172", "CS2_182"});
        uint32_t mark = m->LastEventSeq();
        // Raptor (3/2) hits the Squire (1/1, shield): the shield pops, the Raptor takes 1.
        CHECK(m->Apply(0, Attack(mine[0], theirs[0])) == ActionError::None);
        std::vector<Event> evs = NewEvents(*m, mark);
        CHECK(CountEvents(evs, EventType::ShieldPop) == 1 && CountEvents(evs, EventType::Death) == 0);
        MatchView v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.self, mine[0])->health == 1 && !MinionOf(v.other, theirs[0])->divineShield);
        CHECK(MinionOf(v.other, theirs[0])->health == 1);
        // Simultaneous damage: Crocolisk (2/3) and enemy Raptor (3/2) kill each other.
        CHECK(m->Apply(0, Attack(mine[1], theirs[1])) == ActionError::None);
        evs = NewEvents(*m, mark);
        CHECK(CountEvents(evs, EventType::Death) == 2 && CountEvents(evs, EventType::Damage) == 2);
        // Ogre (6/7) into Yeti (4/5): the Yeti dies, the Ogre survives at 3.
        CHECK(m->Apply(0, Attack(mine[2], theirs[2])) == ActionError::None);
        v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.self, mine[2])->health == 3 && MinionOf(v.self, mine[2])->maxHealth == 7);
        CHECK(MinionOf(v.other, theirs[2]) == nullptr && v.other.board.size() == 1);
        // A defending hero deals no damage back; armor absorbs first.
        std::unique_ptr<Match> n = Scenario();
        std::vector<EntityId> const b = TA::SetBoard(*n, 0, {"CS2_200"});
        TA::SetHero(*n, 1, 30, 4);
        CHECK(n->Apply(0, Attack(b[0], TA::HeroId(*n, 1))) == ActionError::None);
        MatchView const w = n->ViewFor(0, 0);
        CHECK(w.other.hero.armor == 0 && w.other.hero.health == 28 && w.self.board[0].health == 7);
    }

    void TestDeathOrderAndWin()
    {
        std::unique_ptr<Match> m = Scenario("WARRIOR", "MAGE");
        std::vector<EntityId> const mine = TA::SetBoard(*m, 0, {"CS2_168", "CS2_200"});
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_168", "CS2_169", "CS2_200"});
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"EX1_400"});
        uint32_t mark = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        std::vector<Event> const evs = NewEvents(*m, mark);
        std::vector<EntityId> deaths;
        for (Event const& e : evs)
            if (e.type == EventType::Death)
                deaths.push_back(e.target);
        CHECK(deaths.size() == 3 && deaths[0] == mine[0] && deaths[1] == theirs[0] && deaths[2] == theirs[1]);
        // Lethal: the match ends with a winner.
        TA::SetHero(*m, 1, 2, 0);
        TA::ReadyAll(*m);
        CHECK(m->Apply(0, Attack(mine[1], TA::HeroId(*m, 1))) == ActionError::None);
        CHECK(m->GetPhase() == Phase::Over && m->GetResult().winner == 0 && m->GetResult().reason == EndReason::HeroDied);
        MatchView const v = m->ViewFor(1, 0);
        CHECK(v.result.over && v.legal.empty() && v.events.back().type == EventType::GameOver);
        CHECK(m->Apply(1, Simple(ActionType::Concede)) == ActionError::Over);
    }

    void TestBothHeroesDie()
    {
        std::unique_ptr<Match> m = Scenario();
        TA::SetHero(*m, 0, 0, 0);
        TA::SetHero(*m, 1, -2, 0);
        CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::None);
        CHECK(m->GetPhase() == Phase::Over && m->GetResult().winner == -1 && m->GetResult().reason == EndReason::HeroDied);
    }

    void TestConcedeAndAbort()
    {
        std::unique_ptr<Match> m = Make(Fixture(true), Fixture(false), 0);
        // Concede during the mulligan, by the side whose turn it is not.
        CHECK(m->Apply(1, Simple(ActionType::Concede)) == ActionError::None);
        CHECK(m->GetResult().over && m->GetResult().winner == 0 && m->GetResult().reason == EndReason::Concede);
        CHECK(m->Apply(0, Simple(ActionType::Concede)) == ActionError::Over);

        std::unique_ptr<Match> n = Scenario();
        CHECK(n->Apply(1, Simple(ActionType::Concede)) == ActionError::None); // not its turn: still allowed
        CHECK(n->GetResult().winner == 0);

        std::unique_ptr<Match> a = Scenario();
        uint32_t const seq = a->Seq();
        a->Abort();
        CHECK(a->GetResult().over && a->GetResult().winner == -1 && a->GetResult().reason == EndReason::Aborted);
        CHECK(a->Seq() == seq + 1);
        uint32_t const last = a->LastEventSeq();
        a->Abort(); // no effect any more
        CHECK(a->LastEventSeq() == last && a->Seq() == seq + 1);
        CHECK(a->Apply(0, Simple(ActionType::EndTurn)) == ActionError::Over);
    }

    void TestTurnLimit()
    {
        std::unique_ptr<Match> m = Scenario();
        TA::SetTurn(*m, kMaxTurns - 1);
        CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::None);
        CHECK(m->Turn() == kMaxTurns && m->GetPhase() == Phase::Play);
        CHECK(m->Apply(1, Simple(ActionType::EndTurn)) == ActionError::None);
        CHECK(m->GetPhase() == Phase::Over && m->GetResult().winner == -1 && m->GetResult().reason == EndReason::TurnLimit);
    }

    // ---- scripted cards -----------------------------------------------------------

    void TestMageSpells()
    {
        // Fireball, spell damage, invalid targets.
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_029", "CS2_029", "CS2_025", "EX1_277", "CS2_023"});
        std::vector<EntityId> const mine = TA::SetBoard(*m, 0, {"CS2_142"});
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_200", "EX1_008", "CS2_168"});
        CHECK(m->Apply(0, Play(h[0])) == ActionError::Target);        // needs a target
        CHECK(m->Apply(0, Play(h[0], 4242)) == ActionError::Target);  // unknown target
        CHECK(m->Apply(0, Play(h[0], h[1])) == ActionError::Target);  // a hand card is no character
        CHECK(m->Apply(0, Play(h[0], theirs[0])) == ActionError::None);
        CHECK(TA::BoardIds(*m, 1).size() == 2); // 6 + 1 spell damage kills the 6/7 Ogre
        CHECK(m->Apply(0, Play(h[0], theirs[1])) == ActionError::Source); // played already (duplicate)
        // Fireball at the own hero is allowed.
        CHECK(m->Apply(0, Play(h[1], TA::HeroId(*m, 0))) == ActionError::None);
        CHECK(m->HeroHealth(0) == 23);
        // Arcane Explosion with spell damage +1: 2 to every enemy minion; the Squire's shield pops.
        CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
        MatchView v = m->ViewFor(0, 0);
        CHECK(v.other.board.size() == 1 && v.other.board[0].id == theirs[1] && !v.other.board[0].divineShield);
        CHECK(MinionOf(v.self, mine[0]) != nullptr);
        // Arcane Missiles: 3 + 1 missiles, 1 damage each, only enemies.
        TA::SetBoard(*m, 1, {});
        TA::SetHero(*m, 1, 30, 0);
        CHECK(m->ViewFor(0, 0).self.hero.mana == 0);
        CHECK(m->Apply(0, Play(h[3])) == ActionError::Mana);
        TA::SetMana(*m, 0, 10, 10);
        CHECK(m->Apply(0, Play(h[3])) == ActionError::None);
        CHECK(m->HeroHealth(1) == 26);
        CHECK(m->HeroHealth(0) == 23);
        // Arcane Intellect draws two.
        int const before = m->ViewFor(0, 0).self.deckCount;
        CHECK(m->Apply(0, Play(h[4])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.handCount == 2 && m->ViewFor(0, 0).self.deckCount == before - 2);
    }

    void TestArcaneMissilesRandomTargets()
    {
        // Over many seeds, missiles hit only living enemy characters and never more than 3 in total.
        std::set<EntityId> hit;
        for (uint32_t seed = 1; seed < 60; ++seed)
        {
            std::unique_ptr<Match> m = Make(Filler(20), Filler(20), 0, "MAGE", "WARRIOR", seed);
            m->Apply(0, Mull());
            m->Apply(1, Mull());
            TA::SetMana(*m, 0, 10, 10);
            std::vector<EntityId> const h = TA::SetHand(*m, 0, {"EX1_277"});
            std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_168", "CS2_119"});
            uint32_t mark = m->LastEventSeq();
            CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
            int total = 0;
            for (Event const& e : NewEvents(*m, mark))
                if (e.type == EventType::Damage)
                {
                    total += e.amount;
                    hit.insert(e.target == TA::HeroId(*m, 1) ? 1u : (e.target == theirs[0] ? 2u : (e.target == theirs[1] ? 3u : 99u)));
                }
            CHECK(total == 3);
        }
        CHECK(hit.count(99) == 0 && hit.size() == 3);
        // A dead target is not picked again: a lone 1/1 dies and the rest goes to the hero.
        for (uint32_t seed = 1; seed < 30; ++seed)
        {
            std::unique_ptr<Match> m = Make(Filler(20), Filler(20), 0, "MAGE", "WARRIOR", seed);
            m->Apply(0, Mull());
            m->Apply(1, Mull());
            TA::SetMana(*m, 0, 10, 10);
            std::vector<EntityId> const h = TA::SetHand(*m, 0, {"EX1_277"});
            TA::SetBoard(*m, 1, {"CS2_168"});
            uint32_t mark = m->LastEventSeq();
            m->Apply(0, Play(h[0]));
            int onMinion = 0;
            for (Event const& e : NewEvents(*m, mark))
                if (e.type == EventType::Damage && e.target != TA::HeroId(*m, 1))
                    ++onMinion;
            CHECK(onMinion <= 1);
        }
    }

    void TestPolymorph()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_168", "CS2_179", "CS2_122"});
        TA::SetMinion(*m, theirs[1], 5, 2, 6);
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_022", "CS2_022"});
        CHECK(m->Apply(0, Play(h[0], TA::HeroId(*m, 1))) == ActionError::Target); // minions only
        CHECK(m->Apply(0, Play(h[0], theirs[1])) == ActionError::None);
        MatchView v = m->ViewFor(0, 0);
        MinionView const* sheep = MinionOf(v.other, theirs[1]);
        CHECK(sheep && sheep->cardId == "CS2_tk1" && v.other.board[1].id == theirs[1]);
        CHECK(sheep->attack == 2 && sheep->health == 1 && sheep->maxHealth == 1 && !sheep->taunt); // 1 + Raid Leader
        // Polymorph the Raid Leader: the aura is gone.
        CHECK(m->Apply(0, Play(h[1], theirs[2])) == ActionError::None);
        v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.other, theirs[0])->attack == 2 && MinionOf(v.other, theirs[1])->attack == 1);
        // Keeps exhaustion: a freshly played Wolfrider polymorphed loses Charge and cannot attack.
        std::unique_ptr<Match> n = Scenario();
        std::vector<EntityId> const g = TA::SetHand(*n, 0, {"CS2_124", "CS2_022"});
        n->Apply(0, Play(g[0]));
        CHECK(n->Apply(0, Play(g[1], g[0])) == ActionError::None);
        CHECK(!MinionOf(n->ViewFor(0, 0).self, g[0])->canAttack);
        CHECK(n->Apply(0, Attack(g[0], TA::HeroId(*n, 1))) == ActionError::Attack);
    }

    void TestBattlecriesAndAuras()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const mine = TA::SetBoard(*m, 0, {"CS2_142"}); // spell damage does not touch battlecries
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"EX1_593", "EX1_015", "CS2_122"});
        int const deck = m->ViewFor(0, 0).self.deckCount;
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK(m->HeroHealth(1) == 27);
        CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.deckCount == deck - 1 && m->ViewFor(0, 0).self.handCount == 2);
        CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
        MatchView v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.self, mine[0])->attack == 3 && MinionOf(v.self, h[0])->attack == 5 && MinionOf(v.self, h[2])->attack == 2);
        // The aura counts in combat and disappears with its source.
        TA::ReadyAll(*m);
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_120", "CS2_182"});
        CHECK(m->Apply(0, Attack(mine[0], theirs[0])) == ActionError::None); // 3 damage kills the 2/3
        CHECK(TA::BoardIds(*m, 1).size() == 1);
        CHECK(m->Apply(0, Attack(h[2], theirs[1])) == ActionError::None);    // Raid Leader 2/2 into Yeti 4/5: dies
        v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.self, h[2]) == nullptr && MinionOf(v.self, h[0])->attack == 4);
    }

    void TestWarriorCards()
    {
        std::unique_ptr<Match> m = Scenario("WARRIOR", "MAGE");
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_200", "CS2_168"});
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_108", "CS2_105", "CS2_106", "EX1_606", "CS2_114"});
        EntityId const me = TA::HeroId(*m, 0);
        EntityId const enemy = TA::HeroId(*m, 1);
        // Execute needs a damaged enemy minion.
        CHECK(m->Apply(0, Play(h[0], theirs[0])) == ActionError::Target);
        CHECK(!InLegal(m->ViewFor(0, 0), Play(h[0], theirs[0])));
        // Heroic Strike: +4 this turn; the hero attacks the Raider and takes 2.
        CHECK(m->Apply(0, Play(h[1])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.hero.attack == 4 && m->ViewFor(0, 0).self.hero.canAttack);
        CHECK(m->Apply(0, Attack(me, theirs[0])) == ActionError::None);
        CHECK(m->HeroHealth(0) == 24 && m->Apply(0, Attack(me, enemy)) == ActionError::Attack);
        CHECK(m->Apply(0, Play(h[0], theirs[0])) == ActionError::None); // damaged now: destroyed
        CHECK(TA::BoardIds(*m, 1).size() == 1);
        // Weapon: attack only on the own turn, durability, break.
        CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
        MatchView v = m->ViewFor(0, 0);
        CHECK(v.self.hero.weaponId == h[2] && v.self.hero.weaponAttack == 3 && v.self.hero.weaponDurability == 2);
        CHECK(v.self.hero.attack == 7 && !v.self.hero.canAttack); // already attacked this turn
        m->Apply(0, Simple(ActionType::EndTurn));
        CHECK(m->ViewFor(1, 0).other.hero.attack == 0);
        m->Apply(1, Simple(ActionType::EndTurn));
        CHECK(m->ViewFor(0, 0).self.hero.attack == 3); // Heroic Strike ended
        CHECK(m->Apply(0, Attack(me, enemy)) == ActionError::None);
        m->Apply(0, Simple(ActionType::EndTurn));
        m->Apply(1, Simple(ActionType::EndTurn));
        uint32_t mark = m->LastEventSeq();
        CHECK(m->Apply(0, Attack(me, enemy)) == ActionError::None);
        CHECK(CountEvents(NewEvents(*m, mark), EventType::WeaponBreak) == 1);
        CHECK(m->ViewFor(0, 0).self.hero.weaponId == 0 && m->HeroHealth(1) == 24);
        // Shield Block.
        TA::SetMana(*m, 0, 10, 10);
        int const hand = m->ViewFor(0, 0).self.handCount;
        CHECK(m->Apply(0, Play(h[3])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.hero.armor == 5 && m->ViewFor(0, 0).self.handCount == hand);
        // Cleave needs two enemy minions.
        CHECK(m->Apply(0, Play(h[4])) == ActionError::Requirement);
        CHECK(!InLegal(m->ViewFor(0, 0), Play(h[4])));
        std::vector<EntityId> const two = TA::SetBoard(*m, 1, {"CS2_120", "CS2_182", "CS2_200"});
        CHECK(m->Apply(0, Play(h[4])) == ActionError::None);
        int damaged = 0;
        for (MinionView const& mv : m->ViewFor(0, 0).other.board)
            if (mv.health < mv.maxHealth)
                ++damaged;
        CHECK(damaged + (3 - static_cast<int>(m->ViewFor(0, 0).other.board.size())) == 2);
        (void)two;
    }

    void TestChargeSpellAndWarsong()
    {
        std::unique_ptr<Match> m = Scenario("WARRIOR", "MAGE");
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_182", "CS2_103", "EX1_084", "CS2_172", "CS2_200", "CS2_103"});
        CHECK(m->Apply(0, Play(h[1])) == ActionError::Target); // needs a friendly minion
        m->Apply(0, Play(h[0]));
        CHECK(m->Apply(0, Play(h[1], TA::HeroId(*m, 0))) == ActionError::Target);
        CHECK(m->Apply(0, Play(h[1], h[0])) == ActionError::None);
        MatchView v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.self, h[0])->attack == 6 && MinionOf(v.self, h[0])->charge && MinionOf(v.self, h[0])->canAttack);
        // Warsong Commander: not itself; a 3 attack minion gets Charge, a 6 attack one not.
        TA::SetMana(*m, 0, 10, 10);
        CHECK(m->Apply(0, Play(h[2])) == ActionError::None);
        CHECK(!MinionOf(m->ViewFor(0, 0).self, h[2])->charge);
        CHECK(m->Apply(0, Play(h[3])) == ActionError::None);
        CHECK(MinionOf(m->ViewFor(0, 0).self, h[3])->charge && MinionOf(m->ViewFor(0, 0).self, h[3])->canAttack);
        TA::SetMana(*m, 0, 10, 10);
        CHECK(m->Apply(0, Play(h[4])) == ActionError::None);
        CHECK(!MinionOf(m->ViewFor(0, 0).self, h[4])->charge);
        // Charge on an enemy minion is refused.
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_168"});
        CHECK(m->Apply(0, Play(h[5], theirs[0])) == ActionError::Target);
    }

    void TestHealingTotem()
    {
        std::unique_ptr<Match> m = Scenario("SHAMAN", "MAGE");
        std::vector<EntityId> const b = TA::SetBoard(*m, 0, {"NEW1_009", "CS2_200", "CS2_120"});
        TA::SetMinion(*m, b[1], 6, 3, 7);
        TA::SetMinion(*m, b[2], 2, 3, 3);
        std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"CS2_200"});
        TA::SetMinion(*m, theirs[0], 6, 2, 7);
        m->Apply(0, Simple(ActionType::EndTurn));
        MatchView v = m->ViewFor(0, 0);
        CHECK(MinionOf(v.self, b[1])->health == 4 && MinionOf(v.self, b[2])->health == 3);
        CHECK(MinionOf(v.other, theirs[0])->health == 2); // only friendly minions, only at its side's turn end
        m->Apply(1, Simple(ActionType::EndTurn));
        CHECK(MinionOf(m->ViewFor(0, 0).self, b[1])->health == 4);
    }

    // ---- hero powers --------------------------------------------------------------

    void TestHeroPowers()
    {
        {   // Fireblast: 1 damage, once per turn, 2 mana, not boosted by spell damage.
            std::unique_ptr<Match> m = Scenario("MAGE", "WARRIOR");
            TA::SetBoard(*m, 0, {"CS2_142"});
            EntityId const p = TA::PowerId(*m, 0);
            CHECK(m->Apply(0, Power(p)) == ActionError::Target);
            CHECK(m->Apply(0, Power(TA::HeroId(*m, 0) + 1000, TA::HeroId(*m, 1))) == ActionError::Source);
            CHECK(m->Apply(0, Power(p, TA::HeroId(*m, 1))) == ActionError::None);
            CHECK(m->HeroHealth(1) == 29 && m->ViewFor(0, 0).self.hero.mana == 8 && m->ViewFor(0, 0).self.hero.powerUsed);
            CHECK(m->Apply(0, Power(p, TA::HeroId(*m, 1))) == ActionError::Used);
            m->Apply(0, Simple(ActionType::EndTurn));
            m->Apply(1, Simple(ActionType::EndTurn));
            CHECK(!m->ViewFor(0, 0).self.hero.powerUsed);
            TA::SetMana(*m, 0, 1, 1);
            CHECK(m->Apply(0, Power(p, TA::HeroId(*m, 1))) == ActionError::Mana);
            CHECK(m->Apply(1, Power(TA::PowerId(*m, 1))) == ActionError::Turn);
        }
        {   // Armor Up!
            std::unique_ptr<Match> m = Scenario("WARRIOR", "MAGE");
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0), TA::HeroId(*m, 1))) == ActionError::Target);
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            CHECK(m->ViewFor(0, 0).self.hero.armor == 2);
        }
        {   // Lesser Heal: never above the maximum.
            std::unique_ptr<Match> m = Scenario("PRIEST", "MAGE");
            TA::SetHero(*m, 0, 29, 0);
            std::vector<EntityId> const b = TA::SetBoard(*m, 1, {"CS2_200"});
            TA::SetMinion(*m, b[0], 6, 2, 7);
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0), TA::HeroId(*m, 0))) == ActionError::None);
            CHECK(m->HeroHealth(0) == 30);
            TA::SetHeroPowerUsed(*m, 0, false);
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0), b[0])) == ActionError::None); // an enemy minion is fine
            CHECK(m->ViewFor(0, 0).other.board[0].health == 4);
        }
        {   // Steady Shot.
            std::unique_ptr<Match> m = Scenario("HUNTER", "MAGE");
            TA::SetBoard(*m, 1, {"CS2_179"});
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            CHECK(m->HeroHealth(1) == 28);
        }
        {   // Life Tap: draw, then 2 damage.
            std::unique_ptr<Match> m = Scenario("WARLOCK", "MAGE");
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            CHECK(m->HeroHealth(0) == 28 && m->ViewFor(0, 0).self.handCount == 1);
            // Life Tap can be lethal for the own hero.
            TA::SetHero(*m, 0, 2, 0);
            TA::SetHeroPowerUsed(*m, 0, false);
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            CHECK(m->GetResult().over && m->GetResult().winner == 1);
        }
        {   // Reinforce.
            std::unique_ptr<Match> m = Scenario("PALADIN", "MAGE");
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            MatchView const v = m->ViewFor(0, 0);
            CHECK(v.self.board.size() == 1 && v.self.board[0].cardId == "CS2_101t" && !v.self.board[0].canAttack);
        }
        {   // Dagger Mastery replaces a weapon.
            std::unique_ptr<Match> m = Scenario("ROGUE", "MAGE");
            TA::SetWeapon(*m, 0, "CS2_112");
            uint32_t mark = m->LastEventSeq();
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            CHECK(CountEvents(NewEvents(*m, mark), EventType::WeaponBreak) == 1);
            MatchView const v = m->ViewFor(0, 0);
            CHECK(v.self.hero.weaponCardId == "CS2_082" && v.self.hero.weaponAttack == 1 && v.self.hero.weaponDurability == 2);
            CHECK(v.self.hero.canAttack && v.self.hero.attack == 1);
        }
        {   // Shapeshift: +1 attack this turn, +1 armor.
            std::unique_ptr<Match> m = Scenario("DRUID", "MAGE");
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
            CHECK(m->ViewFor(0, 0).self.hero.attack == 1 && m->ViewFor(0, 0).self.hero.armor == 1);
            CHECK(m->Apply(0, Attack(TA::HeroId(*m, 0), TA::HeroId(*m, 1))) == ActionError::None);
            CHECK(m->HeroHealth(1) == 29);
            m->Apply(0, Simple(ActionType::EndTurn));
            m->Apply(1, Simple(ActionType::EndTurn));
            CHECK(m->ViewFor(0, 0).self.hero.attack == 0 && m->ViewFor(0, 0).self.hero.armor == 1);
        }
        {   // Totemic Call: four different totems, then unusable.
            std::unique_ptr<Match> m = Scenario("SHAMAN", "MAGE");
            std::set<std::string> totems;
            for (int i = 0; i < 4; ++i)
            {
                TA::SetHeroPowerUsed(*m, 0, false);
                TA::SetMana(*m, 0, 10, 10);
                CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::None);
                totems.insert(m->ViewFor(0, 0).self.board.back().cardId);
            }
            CHECK(totems.size() == 4);
            TA::SetHeroPowerUsed(*m, 0, false);
            CHECK(m->Apply(0, Power(TA::PowerId(*m, 0))) == ActionError::Requirement);
            CHECK(!InLegal(m->ViewFor(0, 0), Power(TA::PowerId(*m, 0))));
            // Wrath of Air Totem adds spell damage, Stoneclaw is a Taunt.
            MatchView const v = m->ViewFor(0, 0);
            int sd = 0;
            bool taunt = false;
            for (MinionView const& mv : v.self.board)
            {
                sd += mv.spellDamage;
                taunt = taunt || (mv.cardId == "CS2_051" && mv.taunt);
            }
            CHECK(sd == 1 && taunt);
        }
    }

    // ---- validation and the legal list ---------------------------------------------

    void TestOutOfTurnAndDuplicates()
    {
        std::unique_ptr<Match> m = Scenario();
        std::vector<EntityId> const h1 = TA::SetHand(*m, 1, {"CS2_168"});
        TA::SetMana(*m, 1, 10, 10);
        CHECK(m->Apply(1, Play(h1[0])) == ActionError::Turn);
        CHECK(m->Apply(1, Simple(ActionType::EndTurn)) == ActionError::Turn);
        CHECK(m->ViewFor(1, 0).legal.empty());
        CHECK(m->Apply(2, Simple(ActionType::EndTurn)) == ActionError::Turn);
        CHECK(m->Apply(-1, Simple(ActionType::Concede)) == ActionError::Turn);
        std::vector<EntityId> const h = TA::SetHand(*m, 0, {"CS2_172"});
        CHECK(m->Apply(0, Play(h1[0])) == ActionError::Source); // the other side's card
        CHECK(m->Apply(0, Play(0)) == ActionError::Source);
        uint32_t const seq = m->Seq();
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK(m->Apply(0, Play(h[0])) == ActionError::Source); // a repeated action: the source is gone
        CHECK(m->Seq() == seq + 1);
        CHECK(m->Apply(0, Attack(h[0], TA::HeroId(*m, 1))) == ActionError::Attack);
        // Nothing changes on an error.
        std::string const before = m->DebugState();
        m->Apply(0, Attack(h[0], TA::HeroId(*m, 1)));
        m->Apply(0, Play(h[0]));
        m->Apply(1, Simple(ActionType::EndTurn));
        CHECK(m->DebugState() == before && m->Seq() == seq + 1);
    }

    void TestNoLegalActions()
    {
        std::unique_ptr<Match> m = Scenario();
        TA::SetMana(*m, 0, 1, 1);
        TA::SetHand(*m, 0, {"CS2_200"});
        MatchView const v = m->ViewFor(0, 0);
        CHECK(v.legal.empty());
        Action const ai = ChooseAiAction(v, Db(), 0);
        CHECK(ai.type == ActionType::EndTurn);
        CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::None);
    }

    // Every listed action applies; every other (source, target) combination is refused.
    void CheckLegalAgreesWithApply(Match& m, int side, std::vector<EntityId> const& universe, int& checked)
    {
        MatchView const v = m.ViewFor(side, m.LastEventSeq());
        std::string const before = m.DebugState();
        std::vector<EntityId> targets = universe;
        targets.push_back(0);
        for (EntityId s : universe)
            for (EntityId t : targets)
                for (ActionType type : {ActionType::Play, ActionType::Attack, ActionType::HeroPower})
                {
                    Action a;
                    a.type = type;
                    a.source = s;
                    a.target = t;
                    if (InLegal(v, a))
                        continue;
                    ++checked;
                    CHECK(m.Apply(side, a) != ActionError::None);
                }
        CHECK(m.DebugState() == before);
    }

    void TestLegalAgreesWithApply()
    {
        int checked = 0;
        int applied = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            std::unique_ptr<Match> m = Make(Fixture(seed % 2 == 0), Fixture(seed % 3 == 0), -1, seed % 2 ? "WARRIOR" : "MAGE",
                seed % 3 ? "SHAMAN" : "PALADIN", seed);
            m->Apply(0, Mull());
            m->Apply(1, Mull());
            uint32_t rng = seed * 2654435761u;
            for (int step = 0; step < 400 && m->GetPhase() == Phase::Play; ++step)
            {
                int const side = m->ActiveSide();
                MatchView const v = m->ViewFor(side, m->LastEventSeq());
                if (step % 7 == 0)
                {
                    std::vector<EntityId> universe;
                    for (EntityId id = 1; id < 90; ++id)
                        universe.push_back(id);
                    CheckLegalAgreesWithApply(*m, side, universe, checked);
                }
                rng = rng * 1664525u + 1013904223u;
                if (v.legal.empty() || (rng >> 24) % 6 == 0)
                {
                    CHECK(m->Apply(side, Simple(ActionType::EndTurn)) == ActionError::None);
                    continue;
                }
                Action const& a = v.legal[(rng >> 8) % v.legal.size()];
                CHECK(m->Apply(side, a) == ActionError::None);
                ++applied;
            }
        }
        CHECK(checked > 10000 && applied > 300);
    }

    // ---- determinism and hidden information ----------------------------------------

    std::string PlayAiGame(uint32_t seed, bool mage0, bool mage1, int& refusals, int& maxActions, int& turns)
    {
        MatchSetup s;
        s.seed = seed;
        s.sides[0].name = "A";
        s.sides[0].heroClass = mage0 ? "MAGE" : "WARRIOR";
        s.sides[0].deck = Fixture(mage0);
        s.sides[1].name = "B";
        s.sides[1].heroClass = mage1 ? "MAGE" : "WARRIOR";
        s.sides[1].deck = Fixture(mage1);
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (!m)
        {
            ++refusals;
            return error;
        }
        for (int side = 0; side < 2; ++side)
        {
            Action const a = ChooseAiAction(m->ViewFor(side, 0), Db(), 0);
            if (a.type != ActionType::Mulligan || m->Apply(side, a) != ActionError::None)
                ++refusals;
        }
        int actions = 0;
        int turn = m->Turn();
        int guard = 0;
        while (m->GetPhase() == Phase::Play && ++guard < 20000)
        {
            int const side = m->ActiveSide();
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                actions = 0;
            }
            MatchView const v = m->ViewFor(side, m->LastEventSeq());
            Action const a = ChooseAiAction(v, Db(), actions);
            if (a.type != ActionType::EndTurn && !InLegal(v, a))
                ++refusals;
            if (m->Apply(side, a) != ActionError::None)
            {
                ++refusals;
                m->Apply(side, Simple(ActionType::EndTurn));
            }
            ++actions;
            maxActions = std::max(maxActions, actions);
        }
        turns = m->Turn();
        if (m->GetPhase() != Phase::Over)
            ++refusals;
        return m->DebugState();
    }

    void TestDeterminism()
    {
        int r = 0, mx = 0, t = 0;
        std::string const a = PlayAiGame(4242, true, false, r, mx, t);
        std::string const b = PlayAiGame(4242, true, false, r, mx, t);
        std::string const c = PlayAiGame(4243, true, false, r, mx, t);
        CHECK(r == 0 && a == b && a != c);
        // The same seed and the same explicit actions give the same state.
        std::unique_ptr<Match> m1 = Make(Fixture(true), Fixture(false), -1, "MAGE", "WARRIOR", 99);
        std::unique_ptr<Match> m2 = Make(Fixture(true), Fixture(false), -1, "MAGE", "WARRIOR", 99);
        CHECK(m1->DebugState() == m2->DebugState());
        std::vector<EntityId> const off = m1->ViewFor(0, 0).mulligan;
        m1->Apply(0, Mull({off[0]}));
        m2->Apply(0, Mull({off[0]}));
        m1->Apply(1, Mull());
        m2->Apply(1, Mull());
        CHECK(m1->DebugState() == m2->DebugState());
    }

    void TestHiddenInformation()
    {
        std::unique_ptr<Match> m = Make(Fixture(true), Fixture(false), 0, "MAGE", "WARRIOR", 5);
        m->Apply(0, Mull());
        m->Apply(1, Mull());
        for (int i = 0; i < 6; ++i)
            m->Apply(m->ActiveSide(), Simple(ActionType::EndTurn));
        for (int side = 0; side < 2; ++side)
        {
            MatchView const v = m->ViewFor(side, 0);
            CHECK(v.other.hand.empty() && v.other.handCount > 0 && !v.self.hand.empty());
            std::set<EntityId> otherHand;
            for (EntityId id : TA::HandIds(*m, 1 - side))
                otherHand.insert(id);
            int otherDraws = 0;
            for (Event const& e : v.events)
            {
                if (e.type == EventType::Draw && e.side != side)
                {
                    CHECK(e.cardId.empty());
                    ++otherDraws;
                }
                if (e.type == EventType::Draw && e.side == side)
                    CHECK(!e.cardId.empty());
            }
            CHECK(otherDraws > 0);
            // The legal list never names a card of the other hand.
            for (Action const& a : v.legal)
                CHECK(otherHand.count(a.source) == 0 && otherHand.count(a.target) == 0);
        }
        // Deck entity ids are handed out in the shuffled order (top card lowest), so the id of a hidden draw
        // says nothing about which card of the (sorted) decklist it is.
        std::unique_ptr<Match> n = Make(Fixture(true), Fixture(false), 0, "MAGE", "WARRIOR", 11);
        std::vector<EntityId> const ids = TA::DeckIds(*n, 1);
        CHECK(std::is_sorted(ids.begin(), ids.end()));
        std::vector<std::string> const cards = TA::DeckCardIds(*n, 1);
        CHECK(!std::is_sorted(cards.begin(), cards.end()));
        // The AI decides from its view only: the same view gives the same action whatever the hidden cards are.
        int same = 0;
        for (uint32_t seed = 1; seed < 30; ++seed)
        {
            std::unique_ptr<Match> x = Make(Fixture(true), Fixture(false), 1, "MAGE", "WARRIOR", seed);
            MatchView const v = x->ViewFor(1, 0);
            Action const a1 = ChooseAiAction(v, Db(), 0);
            MatchView w = v;
            w.other.handCount = 9; // a different hidden state must not matter beyond what the view says
            Action const a2 = ChooseAiAction(w, Db(), 0);
            same += (a1.type == a2.type && a1.replace == a2.replace) ? 1 : 0;
        }
        CHECK(same == 29);
    }

    // ---- the AI ----------------------------------------------------------------------

    void TestAiMulligan()
    {
        for (uint32_t seed = 1; seed < 50; ++seed)
        {
            std::unique_ptr<Match> m = Make(Fixture(true), Fixture(false), -1, "MAGE", "WARRIOR", seed);
            for (int side = 0; side < 2; ++side)
            {
                MatchView const v = m->ViewFor(side, 0);
                Action const a = ChooseAiAction(v, Db(), 0);
                CHECK(a.type == ActionType::Mulligan);
                for (HandCardView const& h : v.self.hand)
                {
                    bool const replaced = std::find(a.replace.begin(), a.replace.end(), h.id) != a.replace.end();
                    CHECK(replaced == (h.cost >= 4));
                }
                CHECK(m->Apply(side, a) == ActionError::None);
            }
        }
    }

    void TestAiBasics()
    {
        // Not its turn / over: EndTurn. At the action bound: EndTurn.
        std::unique_ptr<Match> m = Scenario();
        TA::SetHand(*m, 0, {"CS2_168"});
        CHECK(ChooseAiAction(m->ViewFor(1, 0), Db(), 0).type == ActionType::EndTurn);
        CHECK(ChooseAiAction(m->ViewFor(0, 0), Db(), kAiMaxActionsPerTurn).type == ActionType::EndTurn);
        CHECK(ChooseAiAction(m->ViewFor(0, 0), Db(), 0).type == ActionType::Play);
        // Lethal: Fireball the face for exactly 6.
        std::unique_ptr<Match> l = Scenario("MAGE", "WARRIOR");
        TA::SetHero(*l, 1, 6, 0);
        std::vector<EntityId> const h = TA::SetHand(*l, 0, {"CS2_029", "CS2_200"});
        TA::SetBoard(*l, 1, {"CS2_200"});
        Action a = ChooseAiAction(l->ViewFor(0, 0), Db(), 0);
        CHECK(a.type == ActionType::Play && a.source == h[0] && a.target == TA::HeroId(*l, 1));
        // Lethal through attacks: go face.
        std::unique_ptr<Match> f = Scenario();
        TA::SetHero(*f, 1, 5, 0);
        TA::SetMana(*f, 0, 0, 0); // no hero power: the contract tries it before attacks
        std::vector<EntityId> const b = TA::SetBoard(*f, 0, {"CS2_200"});
        TA::SetBoard(*f, 1, {"CS2_182"});
        a = ChooseAiAction(f->ViewFor(0, 0), Db(), 0);
        CHECK(a.type == ActionType::Attack && a.source == b[0] && a.target == TA::HeroId(*f, 1));
        // A Taunt in the way: kill it.
        std::unique_ptr<Match> t = Scenario();
        TA::SetMana(*t, 0, 0, 0);
        std::vector<EntityId> const mine = TA::SetBoard(*t, 0, {"CS2_200"});
        std::vector<EntityId> const tt = TA::SetBoard(*t, 1, {"CS2_121"});
        a = ChooseAiAction(t->ViewFor(0, 0), Db(), 0);
        CHECK(a.type == ActionType::Attack && a.source == mine[0] && a.target == tt[0]);
        // A good trade: Ogre kills the Yeti and survives.
        std::unique_ptr<Match> g = Scenario();
        TA::SetMana(*g, 0, 0, 0);
        std::vector<EntityId> const og = TA::SetBoard(*g, 0, {"CS2_200"});
        std::vector<EntityId> const ye = TA::SetBoard(*g, 1, {"CS2_182"});
        a = ChooseAiAction(g->ViewFor(0, 0), Db(), 0);
        CHECK(a.type == ActionType::Attack && a.source == og[0] && a.target == ye[0]);
        // With mana: the hero power comes before the attacks (contract order), Fireblast finishes a 1 health minion.
        std::unique_ptr<Match> p = Scenario();
        std::vector<EntityId> const one = TA::SetBoard(*p, 1, {"CS2_168", "CS2_182"});
        a = ChooseAiAction(p->ViewFor(0, 0), Db(), 0);
        CHECK(a.type == ActionType::HeroPower && a.target == one[0]);
    }

    void TestAiVersusAi()
    {
        int refusals = 0;
        int maxActions = 0;
        int maxTurns = 0;
        int games = 0;
        for (uint32_t seed = 1; seed <= 600; ++seed)
        {
            int turns = 0;
            PlayAiGame(seed * 7919u, seed % 2 == 0, seed % 3 != 0, refusals, maxActions, turns);
            maxTurns = std::max(maxTurns, turns);
            ++games;
        }
        std::printf("    %d AI games: longest %d turns, at most %d actions in one turn, %d refusals\n", games, maxTurns,
                    maxActions, refusals);
        CHECK(refusals == 0);
        CHECK(maxTurns <= kMaxTurns);
        CHECK(maxActions <= kAiMaxActionsPerTurn + 1); // the bound plus the EndTurn
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep the output of a crashing test
    RUN(TestCreateValidation);
    RUN(TestSupported);
    RUN(TestOpeningAndCoin);
    RUN(TestMulligan);
    RUN(TestManaAndTurns);
    RUN(TestCoin);
    RUN(TestFatigue);
    RUN(TestBurn);
    RUN(TestBoardLimit);
    RUN(TestChargeAndExhaustion);
    RUN(TestWindfury);
    RUN(TestTaunt);
    RUN(TestDivineShieldAndCombat);
    RUN(TestDeathOrderAndWin);
    RUN(TestBothHeroesDie);
    RUN(TestConcedeAndAbort);
    RUN(TestTurnLimit);
    RUN(TestMageSpells);
    RUN(TestArcaneMissilesRandomTargets);
    RUN(TestPolymorph);
    RUN(TestBattlecriesAndAuras);
    RUN(TestWarriorCards);
    RUN(TestChargeSpellAndWarsong);
    RUN(TestHealingTotem);
    RUN(TestHeroPowers);
    RUN(TestOutOfTurnAndDuplicates);
    RUN(TestNoLegalActions);
    RUN(TestLegalAgreesWithApply);
    RUN(TestDeterminism);
    RUN(TestHiddenInformation);
    RUN(TestAiMulligan);
    RUN(TestAiBasics);
    RUN(TestAiVersusAi);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
