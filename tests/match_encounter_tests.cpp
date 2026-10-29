// Standalone tests of the boss encounters (the framework and the 15 Normal encounter definitions):
//   - the engine's start state (Match::Create with SideStart): a starting board per side summoned without Battlecries or
//     triggers, a starting weapon, extra hand cards outside the opening hand and the mulligan, a side that skips its
//     turn-start draw (empty deck: no fatigue there; a forced draw still takes fatigue), the forced first side, validation, and that nothing of it disturbs the seeded shuffle
//     or an ordinary match;
//   - src/match/HsEncounters.* : the 15 definitions (decks card for card, hero, health, hero power), exact deck totals,
//     every id in the dataset, the readiness check with a synthetic encounter, and ApplyEncounter;
//   - the nine class challenges (loaner decks): the quest reward cards, readiness, ApplyChallenge, and one
//     Hard-vs-Hard play-out per challenge.
// Uses every card of the dataset (tests/match_all_cards.h) and the generated card data.
//   match_encounter_tests <tests dir>             run all tests; exit code 0 = all checks passed.
//   match_encounter_tests <tests dir> --missing   list, per encounter, the cards the engine cannot play yet.

#include "../src/match/HsEncounters.h"
#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "match_all_cards.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Hs::Match;

static int g_checks = 0;
static int g_failed = 0;
static char const* g_test = "";
static std::string g_testsDir = ".";

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
    using Strs = std::vector<std::string>;

    CardDb const& Db() { return MatchAllCards::Db(); }

    // Cards of the start-state tests (all supported by the engine today): a plain minion, a minion whose Battlecry would
    // hurt a hero, a minion that reacts to summons, a weapon and a spell.
    constexpr char const* kMurloc = "CS2_168";     // Murloc Raider 2/1
    constexpr char const* kRifleman = "CS2_141";   // Ironforge Rifleman: Battlecry, deal 1 damage
    constexpr char const* kJuggler = "NEW1_019";   // Knife Juggler: after you summon a minion, 1 damage to a random enemy
    constexpr char const* kAxe = "CS2_106";        // Fiery War Axe 3/2
    constexpr char const* kFireball = "CS2_029";

    Strs Mixed30()
    {
        char const* const pool[] = {"CS2_168", "CS2_172", "CS2_182", "CS2_179", "CS2_189", "CS2_121"};
        Strs deck;
        for (int i = 0; i < 30; ++i)
            deck.push_back(pool[i % 6]);
        return deck;
    }

    MatchSetup BaseSetup(uint32_t seed, int firstSide = 0)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = firstSide;
        s.sides[0].name = "Jonas";
        s.sides[0].heroClass = "MAGE";
        s.sides[0].deck = Mixed30();
        s.sides[1].name = "Boss";
        s.sides[1].heroClass = "WARRIOR";
        s.sides[1].deck = Mixed30();
        return s;
    }

    std::unique_ptr<Match> Make(MatchSetup const& s, std::string* errorOut = nullptr)
    {
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (errorOut)
            *errorOut = error;
        else if (!m)
            std::printf("  create failed: %s\n", error.c_str());
        return m;
    }

    void MulliganBoth(Match& m)
    {
        Action a;
        a.type = ActionType::Mulligan;
        CHECK(m.Apply(0, a) == ActionError::None);
        CHECK(m.Apply(1, a) == ActionError::None);
    }

    Action PlayCard(EntityId source)
    {
        Action a;
        a.type = ActionType::Play;
        a.source = source;
        return a;
    }

    int CountEvents(MatchView const& v, EventType type)
    {
        int n = 0;
        for (Event const& e : v.events)
            if (e.type == type)
                ++n;
        return n;
    }

    Strs BoardIds(SideView const& s)
    {
        Strs out;
        for (MinionView const& m : s.board)
            out.push_back(m.cardId);
        return out;
    }

    // ---- helpers --------------------------------------------------------------------------------

    std::string ReadFile(std::string const& path)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // ---- start state ----------------------------------------------------------------------------

    void TestStartBoardBothSides()
    {
        MatchSetup s = BaseSetup(7);
        s.sides[0].start.board = {kMurloc, kRifleman, kJuggler};
        s.sides[1].start.board = {kJuggler, kMurloc};
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        // Present before the mulligan, in order, for both sides (side 0's view: own board, then the other's).
        MatchView v = m->ViewFor(0, 0);
        CHECK(v.phase == Phase::Mulligan && v.mulliganPending);
        CHECK(BoardIds(v.self) == Strs({kMurloc, kRifleman, kJuggler}));
        CHECK(BoardIds(v.other) == Strs({kJuggler, kMurloc}));
        CHECK(Strs(TA::BoardCardIds(*m, 0)) == Strs({kMurloc, kRifleman, kJuggler}));
        CHECK_EQ(CountEvents(v, EventType::Summon), 5);

        // Summoned without Battlecry and without triggers: nobody lost a point of health, no damage events at all.
        CHECK_EQ(CountEvents(v, EventType::Damage), 0);
        CHECK_EQ(CountEvents(v, EventType::Trigger), 0);
        CHECK_EQ(m->HeroHealth(0), kHeroHealth);
        CHECK_EQ(m->HeroHealth(1), kHeroHealth);
        for (MinionView const& mv : v.self.board)
            CHECK(mv.health == mv.maxHealth);
        for (MinionView const& mv : v.other.board)
            CHECK(mv.health == mv.maxHealth);

        // After the mulligans the first side's board is ready to attack (not exhausted), the other side's is not (it is
        // not its turn), and the mulligan did not touch the boards.
        MulliganBoth(*m);
        v = m->ViewFor(0, 0);
        CHECK(v.phase == Phase::Play && v.activeSide == 0);
        CHECK(BoardIds(v.self) == Strs({kMurloc, kRifleman, kJuggler}));
        CHECK(BoardIds(v.other) == Strs({kJuggler, kMurloc}));
        for (MinionView const& mv : v.self.board)
            CHECK(mv.canAttack);
        for (MinionView const& mv : v.other.board)
            CHECK(!mv.canAttack);
        // Ordinary plays still work next to the opening board, and the Juggler of the opening board reacts to a
        // minion played later (it only skipped the opening summons).
        CHECK_EQ(CountEvents(v, EventType::Damage), 0);
    }

    void TestStartBoardSideTwoReady()
    {
        // When the boss side goes first its board can attack on turn 1 as well.
        MatchSetup s = BaseSetup(8, 1);
        s.sides[1].start.board = {kMurloc, kMurloc};
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MulliganBoth(*m);
        MatchView const v = m->ViewFor(1, 0);
        CHECK(v.activeSide == 1);
        CHECK_EQ(v.self.board.size(), 2u);
        for (MinionView const& mv : v.self.board)
            CHECK(mv.canAttack);
    }

    void TestStartWeapon()
    {
        MatchSetup s = BaseSetup(9, 1);
        s.sides[1].start.weapon = kAxe;
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MatchView v = m->ViewFor(1, 0);
        CHECK(v.self.hero.weaponCardId == kAxe && v.self.hero.weaponAttack == 3 && v.self.hero.weaponDurability == 2);
        CHECK(v.other.hero.weaponCardId.empty());
        CHECK_EQ(CountEvents(v, EventType::Equip), 1);
        MulliganBoth(*m);
        v = m->ViewFor(1, 0);
        CHECK(v.activeSide == 1 && v.self.hero.attack == 3 && v.self.hero.canAttack);
        // The hero attacks with it: the weapon works like an equipped one.
        Action a;
        a.type = ActionType::Attack;
        a.source = v.self.hero.id;
        a.target = v.other.hero.id;
        CHECK(m->Apply(1, a) == ActionError::None);
        CHECK_EQ(m->HeroHealth(0), kHeroHealth - 3);
        CHECK_EQ(m->ViewFor(1, 0).self.hero.weaponDurability, 1);
    }

    void TestStartHandCards()
    {
        MatchSetup s = BaseSetup(10, 0);
        s.sides[0].start.hand = {kFireball};
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MatchView v = m->ViewFor(0, 0);
        // In addition to the opening hand: 3 as the first side + the extra card; the other side keeps its 4.
        CHECK_EQ(v.self.hand.size(), static_cast<std::size_t>(kStartHandFirst + 1));
        CHECK_EQ(v.other.handCount, kStartHandSecond);
        Strs const hand = TA::HandCardIds(*m, 0);
        CHECK(std::count(hand.begin(), hand.end(), std::string(kFireball)) >= 1);
        CHECK(hand.back() == kFireball); // after the opening hand
        // The deck is untouched by it: 30 - 3.
        CHECK_EQ(v.self.deckCount, 30 - kStartHandFirst);
        // It is not offered in the mulligan, so it cannot be shuffled away: the offer is the opening hand only.
        CHECK_EQ(v.mulligan.size(), static_cast<std::size_t>(kStartHandFirst));
        EntityId const extra = v.self.hand.back().id;
        CHECK(std::find(v.mulligan.begin(), v.mulligan.end(), extra) == v.mulligan.end());
        Action replace;
        replace.type = ActionType::Mulligan;
        replace.replace = {extra};
        CHECK(m->Apply(0, replace) == ActionError::Mulligan);
        // Replace the whole opening hand: the extra card stays.
        replace.replace = v.mulligan;
        CHECK(m->Apply(0, replace) == ActionError::None);
        Action keep;
        keep.type = ActionType::Mulligan;
        CHECK(m->Apply(1, keep) == ActionError::None);
        v = m->ViewFor(0, 0);
        CHECK(v.phase == Phase::Play);
        bool stillThere = false;
        for (HandCardView const& h : v.self.hand)
            stillThere = stillThere || h.id == extra;
        CHECK(stillThere);
        // Turn 1 draw on top: 3 + 1 (extra) + 1 (turn draw); the Coin belongs to the second side.
        CHECK_EQ(v.self.hand.size(), static_cast<std::size_t>(kStartHandFirst + 2));
        Strs const other = TA::HandCardIds(*m, 1);
        CHECK(std::count(other.begin(), other.end(), std::string("GAME_005")) == 1);
    }

    void TestStartHandOnSecondSide()
    {
        // The extra card also works for a side that goes second (4 cards + extra + Coin).
        MatchSetup s = BaseSetup(11, 1);
        s.sides[0].start.hand = {kFireball, kMurloc};
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MulliganBoth(*m);
        Strs const hand = TA::HandCardIds(*m, 0);
        CHECK_EQ(hand.size(), static_cast<std::size_t>(kStartHandSecond + 2 + 1)); // + extras + Coin
        CHECK(std::count(hand.begin(), hand.end(), std::string("GAME_005")) == 1);
    }

    void TestForcedFirstSide()
    {
        int sideZero = 0;
        for (uint32_t seed = 1; seed <= 24; ++seed)
        {
            for (int forced : {0, 1})
            {
                std::unique_ptr<Match> m = Make(BaseSetup(seed, forced));
                CHECK(m != nullptr);
                if (!m)
                    return;
                MulliganBoth(*m);
                CHECK_EQ(m->ActiveSide(), forced);
                CHECK_EQ(TA::HandCardIds(*m, 1 - forced).size() > 0, 1);
            }
            // -1 still tosses the seeded coin; a forced side equal to the toss is the very same match (the toss is
            // consumed either way, so forcing does not move the rest of the random sequence).
            std::unique_ptr<Match> tossed = Make(BaseSetup(seed, -1));
            CHECK(tossed != nullptr);
            if (!tossed)
                return;
            int const toss = tossed->ActiveSide();
            sideZero += toss == 0 ? 1 : 0;
            std::unique_ptr<Match> forced = Make(BaseSetup(seed, toss));
            CHECK(forced != nullptr);
            if (forced)
                CHECK(forced->DebugState() == tossed->DebugState());
        }
        CHECK(sideZero > 0 && sideZero < 24); // the coin toss is not stuck
        // The second side gets The Coin.
        std::unique_ptr<Match> m = Make(BaseSetup(3, 0));
        MulliganBoth(*m);
        Strs const second = TA::HandCardIds(*m, 1);
        CHECK(std::count(second.begin(), second.end(), std::string("GAME_005")) == 1);
        Strs const first = TA::HandCardIds(*m, 0);
        CHECK(std::count(first.begin(), first.end(), std::string("GAME_005")) == 0);
        MatchSetup bad = BaseSetup(3, 2);
        std::string error;
        CHECK(Make(bad, &error) == nullptr && error == "bad first side");
    }

    void TestNoDrawSide()
    {
        // An empty deck is accepted only for a side that never draws.
        MatchSetup s = BaseSetup(12, 0);
        s.sides[1].deck.clear();
        std::string error;
        CHECK(Make(s, &error) == nullptr && error == "deck size out of range");
        s.sides[1].start.noDraw = true;
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        // No opening hand to mulligan (the mulligan still has to be answered), no cards ever, and no fatigue.
        CHECK_EQ(m->ViewFor(1, 0).self.hand.size(), 0u);
        CHECK(m->MulliganPending(1));
        MulliganBoth(*m);
        CHECK(m->GetPhase() == Phase::Play);
        for (int turn = 0; turn < 12 && m->GetPhase() == Phase::Play; ++turn)
        {
            Action end;
            end.type = ActionType::EndTurn;
            CHECK(m->Apply(m->ActiveSide(), end) == ActionError::None);
        }
        MatchView const v = m->ViewFor(1, 0);
        CHECK(v.phase == Phase::Play);
        CHECK_EQ(m->HeroHealth(1), kHeroHealth);
        CHECK_EQ(v.self.hero.fatigue, 0);
        CHECK_EQ(CountEvents(m->ViewFor(1, 0), EventType::Fatigue), 0);
        // Only The Coin ever reached its hand (side 0 went first), and the other side draws as usual: it had turns 1, 3 ..
        // 13, so 30 cards - 3 - 7 draws.
        CHECK_EQ(TA::HandCardIds(*m, 1).size(), 1u);
        CHECK_EQ(v.self.deckCount, 0);
        CHECK_EQ(v.other.deckCount, 30 - 3 - 7);
        // The flag is shown in the debug state, and only then.
        CHECK(m->DebugState().find("nodraw") != std::string::npos);
        CHECK(Make(BaseSetup(12, 0))->DebugState().find("nodraw") == std::string::npos);
    }

    void TestNoDrawSkipsTurnDrawOnly()
    {
        // noDraw skips the turn-start draw whatever the deck holds: the deck of 4 stays untouched over three of the
        // side's turns. The same deck without the flag draws each turn and, once empty, takes fatigue.
        MatchSetup s = BaseSetup(13, 0);
        s.sides[1].deck.resize(10);
        s.sides[1].start.noDraw = true;
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MulliganBoth(*m);
        int const deckBefore = m->ViewFor(1, 0).self.deckCount;
        Strs const handBefore = TA::HandCardIds(*m, 1);
        for (int turn = 0; turn < 6; ++turn)
        {
            Action end;
            end.type = ActionType::EndTurn;
            CHECK(m->Apply(m->ActiveSide(), end) == ActionError::None);
        }
        CHECK_EQ(m->ViewFor(1, 0).self.deckCount, deckBefore);
        CHECK(TA::HandCardIds(*m, 1) == handBefore);
        CHECK_EQ(m->ViewFor(1, 0).self.hero.fatigue, 0);
        // The same deck without the flag takes fatigue once it is empty (the ordinary rule is unchanged).
        MatchSetup t = BaseSetup(13, 0);
        t.sides[1].deck.resize(4);
        std::unique_ptr<Match> n = Make(t);
        CHECK(n != nullptr);
        if (!n)
            return;
        MulliganBoth(*n);
        for (int turn = 0; turn < 6; ++turn)
        {
            Action end;
            end.type = ActionType::EndTurn;
            n->Apply(n->ActiveSide(), end);
        }
        CHECK(n->ViewFor(1, 0).self.hero.fatigue > 0 && n->HeroHealth(1) < kHeroHealth);
    }

    void TestNoDrawForcedDrawFatigue()
    {
        // A noDraw side with an empty deck: no turn-start draw and no fatigue at the start of its turn, but a draw
        // forced by an effect (Mindpocalypse makes both sides draw) takes fatigue 1, then 2.
        MatchSetup s = BaseSetup(14, 0);
        s.sides[1].deck.clear();
        s.sides[1].start.noDraw = true;
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MulliganBoth(*m);
        Strs const handBefore = TA::HandCardIds(*m, 1);
        Action end;
        end.type = ActionType::EndTurn;
        CHECK(m->Apply(0, end) == ActionError::None); // side 1's turn starts
        CHECK_EQ(m->ActiveSide(), 1);
        CHECK(TA::HandCardIds(*m, 1) == handBefore);
        CHECK_EQ(m->ViewFor(1, 0).self.hero.fatigue, 0);
        CHECK_EQ(m->HeroHealth(1), kHeroHealth);
        CHECK(m->Apply(1, end) == ActionError::None);
        CHECK_EQ(m->ActiveSide(), 0);

        TA::SetMana(*m, 0, 10, 10);
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"NAX5_03"});
        // Mindpocalypse draws 2 for each side: the empty side takes 1, then 2 (3 damage) in this one cast.
        CHECK(m->Apply(0, PlayCard(h[0])) == ActionError::None);
        CHECK_EQ(m->ViewFor(0, 0).other.hero.fatigue, 2);
        CHECK_EQ(m->HeroHealth(1), kHeroHealth - 3);

        // The next cast takes 3, then 4 more (the counter keeps running: 10 damage in all).
        TA::SetMana(*m, 0, 10, 10);
        h = TA::SetHand(*m, 0, {"NAX5_03"});
        CHECK(m->Apply(0, PlayCard(h[0])) == ActionError::None);
        CHECK_EQ(m->ViewFor(0, 0).other.hero.fatigue, 4);
        CHECK_EQ(m->HeroHealth(1), kHeroHealth - 10);
    }

    void TestStartStateValidation()
    {
        std::string error;
        MatchSetup s = BaseSetup(14);
        s.sides[1].start.board = Strs(8, kMurloc);
        CHECK(Make(s, &error) == nullptr && error == "start board too large");
        s = BaseSetup(14);
        s.sides[1].start.board = {kFireball};
        CHECK(Make(s, &error) == nullptr && error.find("cannot be a start minion") != std::string::npos);
        s = BaseSetup(14);
        s.sides[1].start.board = {"NOPE_01"};
        CHECK(Make(s, &error) == nullptr && error == "unknown card NOPE_01");
        s = BaseSetup(14);
        s.sides[0].start.board = {"NAX1_03"}; // Nerubian 3/1: a vanilla token, supported
        CHECK(Make(s, &error) != nullptr);
        s = BaseSetup(14);
        s.sides[0].start.hand = {"NAX15_05"}; // Mr. Bigglesworth: flavour, no script
        CHECK(Make(s, &error) == nullptr && error == "unsupported card NAX15_05");
        s = BaseSetup(14);
        s.sides[1].start.weapon = kMurloc;
        CHECK(Make(s, &error) == nullptr && error.find("cannot be a start weapon") != std::string::npos);
        s = BaseSetup(14);
        s.sides[1].start.weapon = "NOPE_02";
        CHECK(Make(s, &error) == nullptr && error == "unknown card NOPE_02");
        s = BaseSetup(14);
        s.sides[0].start.hand = Strs(6, kFireball); // 4 + 6 + Coin is more than the hand holds
        CHECK(Make(s, &error) == nullptr && error == "start hand too large");
        s = BaseSetup(14);
        s.sides[0].start.hand = {"CS2_034"}; // a hero power is no hand card
        CHECK(Make(s, &error) == nullptr);
    }

    void TestDefaultsAreInert()
    {
        // The start state is applied after the shuffle, the toss and the opening hands: with or without it the decks,
        // the opening hands and the first side are the same cards, and a default SideStart changes nothing.
        for (uint32_t seed : {1u, 2u, 3u, 4u, 5u})
        {
            MatchSetup plain = BaseSetup(seed, -1);
            std::unique_ptr<Match> a = Make(plain);
            MatchSetup loaded = BaseSetup(seed, -1);
            loaded.sides[0].start.board = {kMurloc};
            loaded.sides[1].start.board = {kJuggler};
            loaded.sides[1].start.weapon = kAxe;
            loaded.sides[0].start.hand = {kFireball};
            std::unique_ptr<Match> b = Make(loaded);
            CHECK(a != nullptr && b != nullptr);
            if (!a || !b)
                return;
            CHECK_EQ(a->ActiveSide(), b->ActiveSide());
            for (int side = 0; side < 2; ++side)
            {
                CHECK(TA::DeckCardIds(*a, side) == TA::DeckCardIds(*b, side));
                Strs ha = TA::HandCardIds(*a, side);
                Strs hb = TA::HandCardIds(*b, side);
                if (side == 0)
                {
                    CHECK(hb.size() == ha.size() + 1 && hb.back() == kFireball);
                    hb.pop_back();
                }
                CHECK(ha == hb);
            }
            // Everything else equals an explicitly default start state.
            MatchSetup explicitDefault = BaseSetup(seed, -1);
            explicitDefault.sides[0].start = SideStart();
            explicitDefault.sides[1].start = SideStart();
            std::unique_ptr<Match> c = Make(explicitDefault);
            CHECK(c && c->DebugState() == a->DebugState());
        }
    }

    // ---- an encounter-shaped game with the AI ---------------------------------------------------

    void TestEncounterGamesWithAi()
    {
        // A boss that never draws, with a weapon and a board, against a human side with a board and an extra card:
        // both sides played by the Hard AI, to the end, with no refused action and no fatigue on the empty side.
        int games = 0;
        int ended = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            MatchSetup s = BaseSetup(seed, seed % 2 == 0 ? 0 : 1);
            s.sides[1].deck.clear();
            s.sides[1].start.noDraw = true;
            s.sides[1].start.weapon = kAxe;
            s.sides[1].start.board = {kMurloc, kJuggler};
            s.sides[0].start.board = {kMurloc};
            s.sides[0].start.hand = {kFireball};
            std::unique_ptr<Match> m = Make(s);
            CHECK(m != nullptr);
            if (!m)
                return;
            ++games;
            int refused = 0;
            for (int side = 0; side < 2; ++side)
            {
                Action const a = ChooseAiAction(*m, side, Db(), 0, AiProfile::Hard, seed * 31u + side);
                if (a.type != ActionType::Mulligan || m->Apply(side, a) != ActionError::None)
                    ++refused;
            }
            int guard = 0;
            int actions = 0;
            int turn = m->Turn();
            while (m->GetPhase() == Phase::Play && ++guard < 4000)
            {
                int const side = m->ActiveSide();
                if (m->Turn() != turn)
                {
                    turn = m->Turn();
                    actions = 0;
                }
                Action const a = ChooseAiAction(*m, side, Db(), actions, AiProfile::Hard, seed * 7919u + guard);
                if (m->Apply(side, a) != ActionError::None)
                {
                    ++refused;
                    break;
                }
                ++actions;
            }
            CHECK_EQ(refused, 0);
            if (m->GetPhase() == Phase::Over)
                ++ended;
            CHECK_EQ(m->ViewFor(1, 0).self.hero.fatigue, 0);
        }
        CHECK_EQ(games, 12);
        CHECK_EQ(ended, 12);
    }

    // ---- the definitions ------------------------------------------------------------------------

    char const* const kOrder[] = {"naxx_anubrekhan", "naxx_faerlina", "naxx_maexxna", "naxx_noth", "naxx_heigan",
        "naxx_loatheb", "naxx_razuvious", "naxx_gothik", "naxx_horsemen", "naxx_patchwerk", "naxx_grobbulus",
        "naxx_gluth", "naxx_thaddius", "naxx_sapphiron", "naxx_kelthuzad"};

    int DeckTotal(Hs::Encounter const& e)
    {
        int n = 0;
        for (Hs::EncounterCard const& c : e.deck)
            n += c.count;
        return n;
    }

    void TestTableShape()
    {
        std::vector<Hs::Encounter> const& all = Hs::Encounters();
        CHECK_EQ(all.size(), 15u);
        std::set<std::string> keys;
        for (std::size_t i = 0; i < all.size() && i < 15; ++i)
        {
            Hs::Encounter const& e = all[i];
            CHECK(e.key == kOrder[i]);
            CHECK(keys.insert(e.key).second);
            CHECK(Hs::FindEncounter(e.key) == &e);
            // Everything resolves in the dataset and is consistent (data checks only; readiness is separate).
            std::string const problem = Hs::CheckEncounter(Db(), e, false);
            if (!problem.empty())
                std::printf("  %s: %s\n", e.key.c_str(), problem.c_str());
            CHECK(problem.empty());
            CHECK(Db().Find(e.heroCardId) && Db().Find(e.heroCardId)->type == CardType::Hero);
            CHECK(Db().Find(e.heroPowerId) && Db().Find(e.heroPowerId)->type == CardType::HeroPower);
            // Boss decks: 30 cards, none for Patchwerk; the expanded list is sorted by id, one entry per copy.
            std::vector<std::string> const ids = Hs::ExpandEncounterDeck(e);
            CHECK(std::is_sorted(ids.begin(), ids.end()));
            if (e.key == "naxx_patchwerk")
            {
                CHECK(e.deck.empty() && ids.empty() && e.boss.noDraw);
            }
            else
            {
                CHECK_EQ(DeckTotal(e), 30);
                CHECK_EQ(ids.size(), 30u);
                CHECK(!e.boss.noDraw);
                for (Hs::EncounterCard const& c : e.deck)
                    CHECK(c.count >= 1 && c.count <= 6 && Db().Find(c.id) != nullptr);
            }
        }
        CHECK(Hs::FindEncounter("naxx_nope") == nullptr && Hs::FindEncounter("") == nullptr &&
              Hs::FindEncounter("normal_mage") == nullptr);
        int decks = 0;
        for (Hs::Encounter const& e : all)
            decks += DeckTotal(e) == 30 ? 1 : 0;
        CHECK_EQ(decks, 14);
    }

    void TestTableSetups()
    {
        auto get = [](char const* key) -> Hs::Encounter const& { return *Hs::FindEncounter(key); };
        // Health, armor and heroes.
        CHECK_EQ(get("naxx_loatheb").heroHealth, 75);
        CHECK_EQ(get("naxx_razuvious").heroHealth, 40);
        CHECK_EQ(get("naxx_horsemen").heroHealth, 7);
        CHECK(get("naxx_horsemen").heroCardId == "NAX9_01"); // Baron Rivendare
        CHECK_EQ(get("naxx_kelthuzad").heroArmor, 10);
        CHECK_EQ(get("naxx_kelthuzad").heroHealth, 30);
        int armored = 0;
        for (Hs::Encounter const& e : Hs::Encounters())
        {
            armored += e.heroArmor > 0 ? 1 : 0;
            if (e.key != "naxx_loatheb" && e.key != "naxx_razuvious" && e.key != "naxx_horsemen")
                CHECK_EQ(e.heroHealth, 30);
        }
        CHECK_EQ(armored, 1);

        // Starting boards.
        Hs::Encounter const& razuvious = get("naxx_razuvious");
        CHECK(razuvious.boss.board == Strs({"NAX7_02", "NAX7_02"}));
        CHECK(razuvious.human.hand == Strs({"NAX7_05"}) && razuvious.human.board.empty());
        Hs::Encounter const& horsemen = get("naxx_horsemen");
        CHECK(horsemen.boss.board == Strs({"NAX9_02", "NAX9_03", "NAX9_04"}) && horsemen.human.board.empty());
        Hs::Encounter const& sapphiron = get("naxx_sapphiron");
        CHECK(sapphiron.boss.board.empty() && sapphiron.human.board == Strs({"NAX14_03"})); // the PLAYER's side
        Hs::Encounter const& patchwerk = get("naxx_patchwerk");
        CHECK(patchwerk.boss.weapon == "NAX10_02" && patchwerk.boss.noDraw && patchwerk.boss.board.empty());
        // The setup of every other boss is empty ("none documented", Heroic-only boards are not used).
        for (Hs::Encounter const& e : Hs::Encounters())
        {
            if (e.key != "naxx_razuvious" && e.key != "naxx_horsemen" && e.key != "naxx_sapphiron" &&
                e.key != "naxx_patchwerk")
            {
                CHECK(e.boss.board.empty() && e.boss.weapon.empty() && e.boss.hand.empty() && !e.boss.noDraw);
                CHECK(e.human.board.empty() && e.human.hand.empty());
            }
            CHECK(e.human.weapon.empty() && !e.human.noDraw);
        }

        // First side: only Grobbulus forces it (the player goes first; the boss gets The Coin).
        for (Hs::Encounter const& e : Hs::Encounters())
            CHECK((e.first == Hs::EncounterFirst::Human) == (e.key == "naxx_grobbulus"));

        // Kel'Thuzad's second phase as data.
        Hs::Encounter const& kt = get("naxx_kelthuzad");
        CHECK(kt.phase2.present && kt.phase2.heroPowerId == "NAX15_04" && kt.phase2.forcedTurn == 11 && kt.phase2.onArmorGone);
        CHECK(kt.phase2.summons == Strs({"NAX15_03n", "NAX15_03n"}));
        CHECK(kt.heroPowerId == "NAX15_02");
        for (Hs::Encounter const& e : Hs::Encounters())
            CHECK(e.phase2.present == (e.key == "naxx_kelthuzad"));
    }


    // ---- readiness ------------------------------------------------------------------------------

    // An encounter made of cards the engine plays today: a Shaman hero card with the Mage's Fireblast.
    Hs::Encounter Synthetic()
    {
        Hs::Encounter e;
        e.key = "test_boss";
        e.name = "Test Boss";
        e.heroClass = "WARRIOR";
        e.heroCardId = "HERO_02";
        e.heroHealth = 40;
        e.heroArmor = 5;
        e.heroPowerId = "CS2_034";
        e.deck = {{"CS2_168", 10}, {"CS2_172", 10}, {"CS2_182", 10}};
        e.boss.board = {kMurloc, kJuggler};
        e.boss.weapon = kAxe;
        e.human.board = {kMurloc};
        e.human.hand = {kFireball};
        e.first = Hs::EncounterFirst::Human;
        return e;
    }

    void TestReadiness()
    {
        Hs::Encounter e = Synthetic();
        CHECK(Hs::CheckEncounter(Db(), e, false).empty());
        CHECK(Hs::CheckEncounter(Db(), e, true).empty());
        CHECK(Hs::EncounterReady(Db(), e) && Hs::EncounterUnsupported(Db(), e).empty());

        // One card the engine does not script yet (an encounter-only card): not ready, and named.
        Hs::Encounter unscripted = e;
        unscripted.deck[2] = {"NAX12_03H", 10}; // Jaws of the Heroic mode (never scripted)
        CHECK(Hs::CheckEncounter(Db(), unscripted, false).empty()); // the data is fine ...
        std::string const problem = Hs::CheckEncounter(Db(), unscripted, true);
        CHECK(problem.find("Jaws") != std::string::npos && problem.find("not supported") != std::string::npos);
        CHECK(!Hs::EncounterReady(Db(), unscripted));
        CHECK(Hs::EncounterUnsupported(Db(), unscripted) == Strs({"NAX12_03H"}));

        // A start card, a hero power and the phase 2 cards count as well.
        Hs::Encounter power = e;
        power.heroPowerId = "NAX10_03H"; // Hateful Strike of the Heroic mode
        CHECK(!Hs::EncounterReady(Db(), power) && Hs::EncounterUnsupported(Db(), power) == Strs({"NAX10_03H"}));
        Hs::Encounter board = e;
        board.boss.board.push_back("NAX15_05"); // Mr. Bigglesworth
        CHECK(!Hs::EncounterReady(Db(), board));
        Hs::Encounter hand = e;
        hand.human.hand = {"NAX15_05"};
        CHECK(!Hs::EncounterReady(Db(), hand));
        Hs::Encounter weapon = e;
        weapon.boss.weapon = "NAX10_02H"; // Hook of the Heroic mode
        CHECK(!Hs::EncounterReady(Db(), weapon));
        // A second phase is only ready when the boss's hero power really starts it with the same data (here the Mage power
        // does not), even when every card of it plays; an unsupported card of the phase counts like any other.
        Hs::Encounter phased = e;
        phased.phase2.present = true;
        phased.phase2.heroPowerId = "CS2_034";
        phased.phase2.summons = {kMurloc};
        phased.phase2.forcedTurn = 11;
        CHECK(Hs::CheckEncounter(Db(), phased, false).empty());
        CHECK(Hs::CheckEncounter(Db(), phased, true).find("second phase") != std::string::npos);
        Hs::Encounter kt = *Hs::FindEncounter("naxx_kelthuzad");
        CHECK(Hs::EncounterReady(Db(), kt));
        Hs::Encounter wrong = kt; // the data must match the script of Frost Blast
        wrong.phase2.forcedTurn = 9;
        CHECK(Hs::CheckEncounter(Db(), wrong, false).empty() && Hs::CheckEncounter(Db(), wrong, true).find("second phase") != std::string::npos);
        wrong = kt;
        wrong.phase2.summons = {"NAX15_03n"};
        CHECK(Hs::CheckEncounter(Db(), wrong, true).find("second phase") != std::string::npos);
        wrong = kt;
        wrong.phase2.heroPowerId = "NAX10_03";
        CHECK(Hs::CheckEncounter(Db(), wrong, true).find("second phase") != std::string::npos);

        // Data problems are errors with or without requireSupported.
        Hs::Encounter bad = e;
        bad.deck[0].count = 9;
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("29 cards instead of 30") != std::string::npos);
        bad = e;
        bad.deck.push_back({"CS2_168", 1});
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("listed twice") != std::string::npos);
        bad = e;
        bad.deck[0].id = "NOPE_03";
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("unknown card NOPE_03") != std::string::npos);
        bad = e;
        bad.deck[0].id = "HERO_01";
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("cannot be in a deck") != std::string::npos);
        bad = e;
        bad.deck.clear(); // empty deck, but the boss draws
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("boss draws") != std::string::npos);
        bad.boss.noDraw = true;
        CHECK(Hs::CheckEncounter(Db(), bad, false).empty());
        bad = e;
        bad.heroCardId = "CS2_168";
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("not a hero card") != std::string::npos);
        bad = e;
        bad.heroPowerId = "CS2_168";
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("not a hero power") != std::string::npos);
        bad = e;
        bad.heroHealth = 0;
        CHECK(!Hs::CheckEncounter(Db(), bad, false).empty());
        bad = e;
        bad.heroArmor = 201;
        CHECK(!Hs::CheckEncounter(Db(), bad, false).empty());
        bad = e;
        bad.heroClass = "PIRATE";
        CHECK(!Hs::CheckEncounter(Db(), bad, false).empty());
        bad = e;
        bad.key = "bad key";
        CHECK(!Hs::CheckEncounter(Db(), bad, false).empty());
        bad = e;
        bad.boss.board = Strs(8, kMurloc);
        CHECK(!Hs::CheckEncounter(Db(), bad, false).empty());
        bad = e;
        bad.boss.board = {kFireball};
        CHECK(Hs::CheckEncounter(Db(), bad, false).find("not a minion") != std::string::npos);
        bad = e;
        bad.human.weapon = kAxe; // the human only gets a board and hand cards
        CHECK(!Hs::CheckEncounter(Db(), bad, false).empty());
    }

    void TestRealEncountersReadiness()
    {
        // Readiness of the real table follows the engine: ready exactly when nothing of it is unsupported (the phase 2 of
        // Kel'Thuzad has its rules since step E2b, so it no longer holds an encounter back). Since step E2b all 15 are
        // ready; the rule itself is what is pinned first.
        int ready = 0;
        for (Hs::Encounter const& e : Hs::Encounters())
        {
            bool const noMissing = Hs::EncounterUnsupported(Db(), e).empty();
            bool const isReady = Hs::EncounterReady(Db(), e);
            CHECK(isReady == noMissing);
            ready += isReady ? 1 : 0;
            std::string const problem = Hs::CheckEncounter(Db(), e, true);
            CHECK(problem.empty() == isReady);
            if (!isReady && !noMissing)
                CHECK(problem.find("not supported by the match engine yet") != std::string::npos);
            // Whatever is not ready names a card that the engine really cannot play.
            for (std::string const& id : Hs::EncounterUnsupported(Db(), e))
                CHECK(!Db().Supported(id) && Db().Find(id) != nullptr);
        }
        CHECK(Hs::EncounterReady(Db(), *Hs::FindEncounter("naxx_kelthuzad")));
        CHECK_EQ(ready, 15);
        std::printf("  %d of %zu encounters ready\n", ready, Hs::Encounters().size());
        // Every boss hero power is scripted since step E2b.
        int powersMissing = 0;
        for (Hs::Encounter const& e : Hs::Encounters())
            powersMissing += Db().Supported(e.heroPowerId) ? 0 : 1;
        std::printf("  %d of %zu boss hero powers unsupported\n", powersMissing, Hs::Encounters().size());
    }

    // ---- ApplyEncounter -------------------------------------------------------------------------

    void TestApplyEncounter()
    {
        Hs::MatchStartParams p;
        p.humanName = "Jonas";
        p.humanClass = "MAGE";
        p.humanDeck = Mixed30();
        p.opponentName = "Instructor Razuvious";
        p.seed = 77;
        p.meta.accountId = 5;
        Hs::ApplyEncounter(*Hs::FindEncounter("naxx_razuvious"), p);
        CHECK(p.difficulty == "boss" && p.profile == AiProfile::Hard);
        CHECK(p.opponentClass == "WARLOCK" && p.opponentDeck.size() == 30 && std::is_sorted(p.opponentDeck.begin(), p.opponentDeck.end()));
        CHECK_EQ(p.opponentHeroHealth, 40);
        CHECK_EQ(p.opponentHeroArmor, 0);
        CHECK(p.opponentHeroCardId == "NAX7_01" && p.opponentHeroPowerId == "NAX7_03");
        CHECK(p.opponentStart.board == Strs({"NAX7_02", "NAX7_02"}) && p.humanStart.hand == Strs({"NAX7_05"}));
        CHECK_EQ(p.firstSide, -1);
        CHECK(p.meta.opponentDeckKey == "naxx_razuvious");
        // The human keeps their own deck, class and name; nothing else was touched.
        CHECK(p.humanName == "Jonas" && p.humanClass == "MAGE" && p.humanDeck == Mixed30() && p.opponentName == "Instructor Razuvious");
        CHECK(p.seed == 77u && p.meta.accountId == 5u);

        Hs::MatchStartParams q;
        Hs::ApplyEncounter(*Hs::FindEncounter("naxx_grobbulus"), q);
        CHECK_EQ(q.firstSide, 0);
        Hs::MatchStartParams r;
        Hs::ApplyEncounter(*Hs::FindEncounter("naxx_patchwerk"), r);
        CHECK(r.opponentDeck.empty() && r.opponentStart.noDraw && r.opponentStart.weapon == "NAX10_02");
        CHECK(r.humanStart.board.empty() && r.humanStart.hand.empty() && !r.humanStart.noDraw);
        Hs::MatchStartParams t;
        Hs::ApplyEncounter(*Hs::FindEncounter("naxx_sapphiron"), t);
        CHECK(t.humanStart.board == Strs({"NAX14_03"}) && t.opponentStart.board.empty());
        Hs::MatchStartParams u;
        Hs::ApplyEncounter(*Hs::FindEncounter("naxx_kelthuzad"), u);
        CHECK_EQ(u.opponentHeroArmor, 10);
        CHECK_EQ(u.opponentHeroHealth, 30);

        // A synthetic encounter all the way into a match: the params of ApplyEncounter are what the service feeds
        // Match::Create (SetupOf copies exactly these fields).
        Hs::Encounter const e = Synthetic();
        Hs::MatchStartParams w;
        w.humanName = "Jonas";
        w.humanClass = "MAGE";
        w.humanDeck = Mixed30();
        w.opponentName = "Test Boss";
        w.seed = 4242;
        Hs::ApplyEncounter(e, w);
        MatchSetup s;
        s.seed = w.seed;
        s.firstSide = w.firstSide;
        s.sides[0].name = w.humanName;
        s.sides[0].heroClass = w.humanClass;
        s.sides[0].deck = w.humanDeck;
        s.sides[0].start = w.humanStart;
        s.sides[1].name = w.opponentName;
        s.sides[1].heroClass = w.opponentClass;
        s.sides[1].deck = w.opponentDeck;
        s.sides[1].heroHealth = w.opponentHeroHealth;
        s.sides[1].heroArmor = w.opponentHeroArmor;
        s.sides[1].heroCardId = w.opponentHeroCardId;
        s.sides[1].heroPowerId = w.opponentHeroPowerId;
        s.sides[1].start = w.opponentStart;
        std::unique_ptr<Match> m = Make(s);
        CHECK(m != nullptr);
        if (!m)
            return;
        MulliganBoth(*m);
        MatchView const v = m->ViewFor(0, 0);
        CHECK(v.activeSide == 0); // the Human-first rule
        CHECK(v.other.hero.health == 40 && v.other.hero.armor == 5 && v.other.hero.cardId == "HERO_02");
        CHECK(v.other.hero.weaponCardId == kAxe);
        CHECK(BoardIds(v.other) == Strs({kMurloc, kJuggler}) && BoardIds(v.self) == Strs({kMurloc}));
        Strs const second = TA::HandCardIds(*m, 1);
        CHECK(std::count(second.begin(), second.end(), std::string("GAME_005")) == 1); // the boss has The Coin
        Strs const first = TA::HandCardIds(*m, 0);
        CHECK(std::count(first.begin(), first.end(), std::string(kFireball)) >= 1);
    }
    // ---- class challenges (loaner decks) --------------------------------------------------------

    struct ChallengeExpect
    {
        char const* cls;
        char const* heroClass;
        char const* key;
        char const* encounter;
        char const* reward;
    };

    // Adventure order: wing 1 Druid, Rogue; wing 2 Mage, Hunter; wing 3 Shaman, Warlock; wing 4 Warrior, Priest; wing 5 Paladin.
    ChallengeExpect const kChallenges[] = {
        {"Druid", "DRUID", "naxx_challenge_druid", "naxx_faerlina", "FP1_019"},
        {"Rogue", "ROGUE", "naxx_challenge_rogue", "naxx_maexxna", "FP1_026"},
        {"Mage", "MAGE", "naxx_challenge_mage", "naxx_heigan", "FP1_018"},
        {"Hunter", "HUNTER", "naxx_challenge_hunter", "naxx_loatheb", "FP1_011"},
        {"Shaman", "SHAMAN", "naxx_challenge_shaman", "naxx_gothik", "FP1_025"},
        {"Warlock", "WARLOCK", "naxx_challenge_warlock", "naxx_horsemen", "FP1_022"},
        {"Warrior", "WARRIOR", "naxx_challenge_warrior", "naxx_grobbulus", "FP1_021"},
        {"Priest", "PRIEST", "naxx_challenge_priest", "naxx_thaddius", "FP1_023"},
        {"Paladin", "PALADIN", "naxx_challenge_paladin", "naxx_kelthuzad", "FP1_020"},
    };


    // The acquisition of one card in the generated dataset (data/cards/cards.normalized.json), "" when not found.
    std::string AcquisitionOf(std::string const& json, std::string const& id)
    {
        std::string const key = "\"id\": \"" + id + "\"";
        std::string const field = "\"acquisition\": \"";
        std::size_t const at = json.find(key);
        if (at == std::string::npos)
            return std::string();
        std::size_t const next = json.find("\"id\": \"", at + key.size());
        std::size_t const acq = json.find(field, at);
        if (acq == std::string::npos || (next != std::string::npos && acq > next))
            return std::string();
        std::size_t const start = acq + field.size();
        return json.substr(start, json.find('"', start) - start);
    }

    void TestChallengeTable()
    {
        std::vector<Hs::Challenge> const& all = Hs::Challenges();
        CHECK_EQ(all.size(), 9u);
        std::set<std::string> keys;
        std::set<std::string> rewards;
        for (std::size_t i = 0; i < all.size() && i < 9; ++i)
        {
            Hs::Challenge const& c = all[i];
            ChallengeExpect const& x = kChallenges[i];
            CHECK(c.key == x.key);
            CHECK(c.heroClass == x.heroClass);
            CHECK(c.encounterKey == x.encounter);
            CHECK(c.rewardCardId == x.reward);
            CHECK(keys.insert(c.key).second);
            CHECK(rewards.insert(c.rewardCardId).second);
            CHECK(Hs::FindChallenge(c.key) == &c);
            CHECK(Hs::FindEncounter(c.encounterKey) != nullptr);
            CHECK_EQ(c.deck.size(), 30u);
            CHECK(std::is_sorted(c.deck.begin(), c.deck.end()));
            for (std::string const& id : c.deck)
                CHECK(Db().Find(id) != nullptr);
            CHECK(Db().Find(c.rewardCardId) != nullptr);
            // The reward card sits in the loaner deck, as in the original (the human holds it during the challenge).
            CHECK(std::find(c.deck.begin(), c.deck.end(), c.rewardCardId) != c.deck.end());
            std::string const problem = Hs::CheckChallenge(Db(), c, false);
            if (!problem.empty())
                std::printf("  %s: %s\n", c.key.c_str(), problem.c_str());
            CHECK(problem.empty());
        }
        CHECK(Hs::FindChallenge("naxx_nope") == nullptr && Hs::FindChallenge("") == nullptr);
        CHECK(!all.empty() && Hs::FindChallenge("naxx_challenge_druid") == &all[0]);
    }


    void TestChallengeRewardsAreQuestCards()
    {
        std::string const json = ReadFile(g_testsDir + "/../data/cards/cards.normalized.json");
        CHECK(!json.empty());
        for (Hs::Challenge const& c : Hs::Challenges())
            CHECK(AcquisitionOf(json, c.rewardCardId) == "quest");
        CHECK(AcquisitionOf(json, "NOPE_06").empty());
    }

    void TestChallengeReadiness()
    {
        for (Hs::Challenge const& c : Hs::Challenges())
        {
            std::string const problem = Hs::CheckChallenge(Db(), c, true);
            if (!problem.empty())
                std::printf("  %s: %s\n", c.key.c_str(), problem.c_str());
            CHECK(problem.empty());
            CHECK(Hs::ChallengeReady(Db(), c));
        }

        Hs::Challenge const base = *Hs::FindChallenge("naxx_challenge_druid");
        // The loaner rule: the class and deck are not checked against the player or the class; a Druid deck as a Mage passes.
        Hs::Challenge other = base;
        other.heroClass = "MAGE";
        CHECK(Hs::CheckChallenge(Db(), other, true).empty());
        Hs::Challenge bad = base;
        bad.deck.pop_back();
        CHECK(Hs::CheckChallenge(Db(), bad, false).find("29 cards instead of 30") != std::string::npos);
        CHECK(!Hs::ChallengeReady(Db(), bad));
        bad = base;
        bad.deck[0] = "NOPE_04";
        CHECK(Hs::CheckChallenge(Db(), bad, false).find("unknown card NOPE_04") != std::string::npos);
        bad = base;
        bad.deck[0] = "HERO_01";
        CHECK(Hs::CheckChallenge(Db(), bad, false).find("cannot be in a deck") != std::string::npos);
        bad = base;
        bad.deck[0] = "NAX12_03H"; // Jaws of the Heroic mode: known, never scripted
        CHECK(Hs::CheckChallenge(Db(), bad, false).empty());
        CHECK(Hs::CheckChallenge(Db(), bad, true).find("not supported") != std::string::npos);
        CHECK(!Hs::ChallengeReady(Db(), bad));
        bad = base;
        bad.heroClass = "PIRATE";
        CHECK(!Hs::CheckChallenge(Db(), bad, false).empty());
        bad = base;
        bad.key = "bad key";
        CHECK(!Hs::CheckChallenge(Db(), bad, false).empty());
        bad = base;
        bad.encounterKey = "naxx_nope";
        CHECK(Hs::CheckChallenge(Db(), bad, false).find("does not exist") != std::string::npos);
        CHECK(!Hs::ChallengeReady(Db(), bad));
        bad = base;
        bad.rewardCardId = "NOPE_05";
        CHECK(Hs::CheckChallenge(Db(), bad, false).find("reward card NOPE_05") != std::string::npos);
    }

    void TestApplyChallenge()
    {
        Hs::MatchStartParams p;
        p.humanName = "Jonas";
        p.humanClass = "MAGE";
        p.humanDeck = Mixed30();
        p.opponentName = "Grand Widow Faerlina";
        p.seed = 5;
        p.meta.accountId = 7;
        Hs::Challenge const& c = *Hs::FindChallenge("naxx_challenge_druid");
        Hs::ApplyChallenge(c, p);
        CHECK(p.humanClass == "DRUID" && p.humanDeck == c.deck && p.humanDeck.size() == 30u);
        CHECK(p.humanName == "Jonas" && p.opponentName == "Grand Widow Faerlina");
        CHECK_EQ(p.seed, 5);
        CHECK_EQ(p.meta.accountId, 7);
        // The opponent is untouched by the human helper.
        CHECK(p.opponentDeck.empty() && p.difficulty == "normal" && p.firstSide == -1);
        // Then the boss, as a challenge match does: the human keeps the loaner class and deck.
        Hs::ApplyEncounter(*Hs::FindEncounter(c.encounterKey), p);
        CHECK(p.difficulty == "boss" && p.humanClass == "DRUID" && p.humanDeck == c.deck);
        CHECK(p.opponentDeck.size() == 30u && p.opponentHeroCardId == "NAX2_01" && p.meta.opponentDeckKey == "naxx_faerlina");
    }

    Hs::MatchStartParams ChallengeMatch(Hs::Challenge const& c, uint32_t seed)
    {
        Hs::MatchStartParams p;
        p.humanName = "Loaner";
        p.humanClass = "MAGE";
        p.humanDeck = Mixed30();
        p.opponentName = "Boss";
        p.seed = seed;
        Hs::ApplyChallenge(c, p);
        Hs::ApplyEncounter(*Hs::FindEncounter(c.encounterKey), p);
        return p;
    }

    MatchSetup SetupOf(Hs::MatchStartParams const& w)
    {
        MatchSetup s;
        s.seed = w.seed;
        s.firstSide = w.firstSide;
        s.sides[0].name = w.humanName;
        s.sides[0].heroClass = w.humanClass;
        s.sides[0].deck = w.humanDeck;
        s.sides[0].start = w.humanStart;
        s.sides[1].name = w.opponentName;
        s.sides[1].heroClass = w.opponentClass;
        s.sides[1].deck = w.opponentDeck;
        s.sides[1].heroHealth = w.opponentHeroHealth;
        s.sides[1].heroArmor = w.opponentHeroArmor;
        s.sides[1].heroCardId = w.opponentHeroCardId;
        s.sides[1].heroPowerId = w.opponentHeroPowerId;
        s.sides[1].start = w.opponentStart;
        return s;
    }

    void TestChallengePlayOuts()
    {
        // Each challenge against its boss's Normal setup, the Hard AI on both sides (the loaner class and deck for the human),
        // played to the end: no refused action and a result.
        for (Hs::Challenge const& c : Hs::Challenges())
        {
            Hs::MatchStartParams const p = ChallengeMatch(c, 2026);
            std::unique_ptr<Match> m = Make(SetupOf(p));
            CHECK(m != nullptr);
            if (!m)
                return;
            int refused = 0;
            for (int side = 0; side < 2; ++side)
            {
                Action const a = ChooseAiAction(*m, side, Db(), 0, AiProfile::Hard, 2026u * 31u + side);
                if (m->Apply(side, a) != ActionError::None)
                    ++refused;
            }
            int guard = 0;
            int actions = 0;
            int turn = m->Turn();
            while (m->GetPhase() == Phase::Play && ++guard < 4000)
            {
                int const side = m->ActiveSide();
                if (m->Turn() != turn)
                {
                    turn = m->Turn();
                    actions = 0;
                }
                Action const a = ChooseAiAction(*m, side, Db(), actions, AiProfile::Hard, 2026u * 7919u + guard);
                if (m->Apply(side, a) != ActionError::None)
                {
                    ++refused;
                    break;
                }
                ++actions;
            }
            if (refused != 0 || m->GetPhase() != Phase::Over)
                std::printf("  %s: %d refused, phase %d\n", c.key.c_str(), refused, static_cast<int>(m->GetPhase()));
            CHECK_EQ(refused, 0);
            CHECK(m->GetPhase() == Phase::Over);
        }
    }

}

int main(int argc, char** argv)
{
    if (argc > 1)
        g_testsDir = argv[1];
    if (argc > 2 && std::string(argv[2]) == "--missing")
    {
        for (Hs::Encounter const& e : Hs::Encounters())
        {
            std::printf("%s (%s)\n", e.key.c_str(), Hs::CheckEncounter(Db(), e, true).c_str());
            for (std::string const& id : Hs::EncounterUnsupported(Db(), e))
                std::printf("  %s %s\n", id.c_str(), Db().Find(id)->name.c_str());
        }
        return 0;
    }
    RUN(TestStartBoardBothSides);
    RUN(TestStartBoardSideTwoReady);
    RUN(TestStartWeapon);
    RUN(TestStartHandCards);
    RUN(TestStartHandOnSecondSide);
    RUN(TestForcedFirstSide);
    RUN(TestNoDrawSide);
    RUN(TestNoDrawSkipsTurnDrawOnly);
    RUN(TestNoDrawForcedDrawFatigue);
    RUN(TestStartStateValidation);
    RUN(TestDefaultsAreInert);
    RUN(TestEncounterGamesWithAi);
    RUN(TestTableShape);
    RUN(TestTableSetups);
    RUN(TestReadiness);
    RUN(TestRealEncountersReadiness);
    RUN(TestApplyEncounter);
    RUN(TestChallengeTable);
    RUN(TestChallengeRewardsAreQuestCards);
    RUN(TestChallengeReadiness);
    RUN(TestApplyChallenge);
    RUN(TestChallengePlayOuts);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed != 0)
    {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("all encounter checks passed\n");
    return 0;
}
