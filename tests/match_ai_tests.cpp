// Standalone tests of the opponent AI (src/match/HsMatchAi.cpp, HsMatchAiSearch.cpp): the Normal and
// Hard profiles over decks built from every card the engine supports (tests/match_all_cards.h).
//   match_ai_tests.exe [directory [games]]   run all tests; exit code 0 = all checks passed.
// Optional second argument: games per pairing / mirrored pairs (default 200; 50 in sanitizer builds, which are
// several times slower).

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "match_all_cards.h"

#include <algorithm>
#include <chrono>
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

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define HS_AI_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HS_AI_SANITIZED 1
#endif
#endif
#ifndef HS_AI_SANITIZED
#define HS_AI_SANITIZED 0
#endif

namespace
{
    using TA = TestAccess;
    using Clock = std::chrono::steady_clock;

    CardDb const& Db() { return MatchAllCards::Db(); }

    char const* const kClasses[9] = {"MAGE", "WARRIOR", "HUNTER", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "DRUID"};

    int g_gamesArg = 0;

    int GamesPerPairing()
    {
        if (g_gamesArg > 0)
            return std::max(2, g_gamesArg);
        return HS_AI_SANITIZED ? 50 : 200;
    }

    // ---- decks from the supported cards --------------------------------------------------------------

    // Supported playable cards of a class (plus neutral), no tokens (cards other scripts refer to), id order.
    std::vector<std::string> Pool(std::string const& cls)
    {
        static std::set<std::string> tokens;
        if (tokens.empty())
        {
            tokens.insert(Detail::kCoinId);
            for (MatchAllCards::Row const& r : MatchAllCards::kRows)
                if (Detail::Script const* sc = Detail::FindScript(r.id))
                    for (std::string const& ref : sc->refs)
                        tokens.insert(ref);
        }
        std::vector<std::string> out;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            if (!Db().Supported(r.id) || tokens.count(r.id))
                continue;
            CardDef const* d = Db().Find(r.id);
            if (!d || (d->type != CardType::Minion && d->type != CardType::Spell && d->type != CardType::Weapon))
                continue;
            if (d->cardClass != cls && d->cardClass != "NEUTRAL")
                continue;
            out.push_back(r.id);
        }
        return out;
    }

    int Bucket(int cost)
    {
        return cost <= 1 ? 0 : std::min(6, cost - 1);
    }

    // A 30 card deck with a sensible curve, deterministic for (class, seed): the pool is shuffled by the seed and
    // filled bucket by bucket (cost 0-1: 4, 2: 6, 3: 6, 4: 5, 5: 4, 6: 3, 7+: 2), at most two copies of a card.
    std::vector<std::string> BuildDeck(std::string const& cls, uint32_t seed)
    {
        std::vector<std::string> pool = Pool(cls);
        std::mt19937 rng(seed * 2654435761u + 17u);
        std::shuffle(pool.begin(), pool.end(), rng);
        int const want[7] = {4, 6, 6, 5, 4, 3, 2};
        std::vector<std::string> deck;
        std::map<std::string, int> copies;
        for (int b = 0; b < 7; ++b)
        {
            int have = 0;
            for (std::string const& id : pool)
            {
                if (have >= want[b] || deck.size() >= 30)
                    break;
                if (Bucket(Db().Find(id)->cost) != b)
                    continue;
                for (int c = 0; c < 2 && have < want[b]; ++c)
                {
                    deck.push_back(id);
                    ++copies[id];
                    ++have;
                }
            }
        }
        // Short buckets: fill up with more copies in pool order, then by repeating.
        for (std::string const& id : pool)
            while (deck.size() < 30 && copies[id] < 2)
            {
                deck.push_back(id);
                ++copies[id];
            }
        for (std::size_t i = 0; deck.size() < 30 && !pool.empty(); ++i)
            deck.push_back(pool[i % pool.size()]);
        return deck;
    }

    // ---- games ------------------------------------------------------------------------------------------

    struct Stats
    {
        int games = 0;
        int refusals = 0;   // actions the engine refused or that were not in the legal list
        int notOver = 0;    // games that did not end
        int maxActions = 0; // most actions of one turn (EndTurn included)
        int maxTurns = 0;
        int picks = 0;      // pending picks answered
        double hardMs = 0, hardMaxMs = 0;
        long hardCalls = 0;
        long hardSims = 0;
        int hardMaxSims = 0;
        int lethalFound = 0;
        double normalMs = 0, normalMaxMs = 0;
        long normalCalls = 0;
    };

    Stats g_all; // every game of the program (timing report)

    struct GameResult
    {
        int winner = -1; // -1 draw
        int turns = 0;
        bool over = false;
    };

    Action Simple(ActionType t)
    {
        Action a;
        a.type = t;
        return a;
    }

    bool SameAction(Action const& a, Action const& b)
    {
        return a.type == b.type && a.source == b.source && a.target == b.target && a.position == b.position &&
               a.choice == b.choice && a.replace == b.replace;
    }

    bool InLegal(MatchView const& v, Action const& a)
    {
        for (Action const& l : v.legal)
            if (l.type == a.type && l.source == a.source && l.target == a.target && l.choice == a.choice)
                return true;
        return false;
    }

    void Merge(Stats& into, Stats const& s)
    {
        into.games += s.games;
        into.refusals += s.refusals;
        into.notOver += s.notOver;
        into.maxActions = std::max(into.maxActions, s.maxActions);
        into.maxTurns = std::max(into.maxTurns, s.maxTurns);
        into.picks += s.picks;
        into.hardMs += s.hardMs;
        into.hardMaxMs = std::max(into.hardMaxMs, s.hardMaxMs);
        into.hardCalls += s.hardCalls;
        into.hardSims += s.hardSims;
        into.hardMaxSims = std::max(into.hardMaxSims, s.hardMaxSims);
        into.lethalFound += s.lethalFound;
        into.normalMs += s.normalMs;
        into.normalMaxMs = std::max(into.normalMaxMs, s.normalMaxMs);
        into.normalCalls += s.normalCalls;
    }

    // Asks the AI for the next action of `side` and checks it (legal list, pending pick); fills the stats.
    Action AiStep(Match const& m, int side, int actions, AiProfile prof, uint32_t aiSeed, Stats& st)
    {
        MatchView const v = m.ViewFor(side, m.LastEventSeq());
        AiDecisionInfo info;
        Clock::time_point const t0 = Clock::now();
        Action const a = ChooseAiAction(m, side, Db(), actions, prof, aiSeed, &info);
        double const ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (prof == AiProfile::Hard)
        {
            st.hardMs += ms;
            st.hardMaxMs = std::max(st.hardMaxMs, ms);
            ++st.hardCalls;
            st.hardSims += info.simulations;
            st.hardMaxSims = std::max(st.hardMaxSims, info.simulations);
            st.lethalFound += info.lethal ? 1 : 0;
        }
        else
        {
            st.normalMs += ms;
            st.normalMaxMs = std::max(st.normalMaxMs, ms);
            ++st.normalCalls;
        }
        if (v.pickPending)
        {
            bool found = false;
            for (PickView const& p : v.pick)
                found = found || p.id == a.source;
            if (a.type != ActionType::Choose || !found)
                ++st.refusals;
            ++st.picks;
        }
        else if (a.type != ActionType::EndTurn && !InLegal(v, a))
            ++st.refusals;
        return a;
    }

    std::unique_ptr<Match> CreateMatch(std::vector<std::string> const& deck0, char const* cls0,
                                       std::vector<std::string> const& deck1, char const* cls1, uint32_t seed, int firstSide)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = firstSide;
        s.sides[0].name = "A";
        s.sides[0].heroClass = cls0;
        s.sides[0].deck = deck0;
        s.sides[1].name = "B";
        s.sides[1].heroClass = cls1;
        s.sides[1].deck = deck1;
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (!m)
            std::printf("  create failed: %s\n", error.c_str());
        return m;
    }

    // Both sides mulligan with their AI.
    void AiMulligans(Match& m, AiProfile const prof[2], uint32_t const aiSeed[2], Stats& st)
    {
        for (int side = 0; side < 2; ++side)
        {
            MatchView const v = m.ViewFor(side, 0);
            Action const a = ChooseAiAction(m, side, Db(), 0, prof[side], aiSeed[side]);
            bool ok = a.type == ActionType::Mulligan && v.mulliganPending;
            for (EntityId id : a.replace)
                ok = ok && std::find(v.mulligan.begin(), v.mulligan.end(), id) != v.mulligan.end();
            if (!ok || m.Apply(side, a) != ActionError::None)
                ++st.refusals;
        }
    }

    // Plays one game with AIs of the given profiles; every chosen action is checked.
    GameResult PlayGame(std::vector<std::string> const& deck0, char const* cls0, std::vector<std::string> const& deck1,
                        char const* cls1, uint32_t seed, int firstSide, AiProfile p0, AiProfile p1, Stats& st,
                        std::set<std::string>* played = nullptr)
    {
        GameResult res;
        std::unique_ptr<Match> m = CreateMatch(deck0, cls0, deck1, cls1, seed, firstSide);
        if (!m)
        {
            ++st.refusals;
            return res;
        }
        ++st.games;
        AiProfile const prof[2] = {p0, p1};
        uint32_t const aiSeed[2] = {seed * 7919u + 1u, seed * 104729u + 2u};
        AiMulligans(*m, prof, aiSeed, st);
        int actions = 0;
        int turn = m->Turn();
        int guard = 0;
        while (m->GetPhase() == Phase::Play && ++guard < 6000)
        {
            int const side = m->ActiveSide();
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                actions = 0;
            }
            Action const a = AiStep(*m, side, actions, prof[side], aiSeed[side], st);
            if (m->Apply(side, a) != ActionError::None)
            {
                ++st.refusals;
                m->Apply(side, Simple(ActionType::Concede)); // cannot continue
            }
            ++actions;
            st.maxActions = std::max(st.maxActions, actions);
        }
        res.turns = m->Turn();
        res.over = m->GetPhase() == Phase::Over;
        if (!res.over)
            ++st.notOver;
        res.winner = m->GetResult().winner;
        st.maxTurns = std::max(st.maxTurns, res.turns);
        if (played)
            for (int side = 0; side < 2; ++side)
                for (Event const& e : m->ViewFor(side, 0).events)
                    if (e.type == EventType::Play && !e.cardId.empty())
                        played->insert(e.cardId);
        return res;
    }

    // A deterministic pairing: classes and decks of game number g.
    struct Pairing
    {
        char const* cls0;
        char const* cls1;
        std::vector<std::string> deck0, deck1;
    };

    Pairing MakePairing(int g)
    {
        std::mt19937 rng(static_cast<uint32_t>(g) * 31337u + 5u);
        Pairing p;
        p.cls0 = kClasses[(g + static_cast<int>(rng() % 3)) % 9];
        p.cls1 = kClasses[(g * 4 + 1 + static_cast<int>(rng() % 5)) % 9];
        p.deck0 = BuildDeck(p.cls0, static_cast<uint32_t>(g) * 2u + 1u);
        p.deck1 = BuildDeck(p.cls1, static_cast<uint32_t>(g) * 2u + 2u);
        return p;
    }

    // ---- scenario helpers (hand-built positions) --------------------------------------------------------

    // A match in its play phase, side 0 active on turn 1 with 10 mana; both hands empty, boards empty.
    std::unique_ptr<Match> Fresh(char const* c0, char const* c1, uint32_t seed = 11)
    {
        std::vector<std::string> const filler(20, "CS2_168");
        std::unique_ptr<Match> m = CreateMatch(filler, c0, filler, c1, seed, 0);
        if (!m)
            return m;
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

    // ---- tests ----------------------------------------------------------------------------------------

    void TestDecks()
    {
        for (char const* cls : kClasses)
        {
            std::vector<std::string> const pool = Pool(cls);
            std::vector<std::string> const deck = BuildDeck(cls, 3);
            std::printf("    %-8s pool %3d supported cards\n", cls, static_cast<int>(pool.size()));
            CHECK(pool.size() >= 20);
            CHECK(deck.size() == 30);
            std::map<std::string, int> n;
            for (std::string const& id : deck)
            {
                CHECK(Db().Supported(id));
                ++n[id];
            }
            for (auto const& e : n)
                CHECK(e.second <= 2);
            CHECK(deck == BuildDeck(cls, 3));
        }
    }

    // 1. Every chosen action applies, games end, the action bound holds, pending picks are answered.
    void RunPairing(char const* label, AiProfile p0, AiProfile p1, int games, uint32_t base)
    {
        Stats st;
        int wins[2] = {0, 0};
        int draws = 0;
        for (int g = 0; g < games; ++g)
        {
            Pairing const p = MakePairing(static_cast<int>(base) + g);
            GameResult const r = PlayGame(p.deck0, p.cls0, p.deck1, p.cls1, base * 1000u + static_cast<uint32_t>(g), g % 2, p0, p1, st);
            if (r.winner >= 0)
                ++wins[r.winner];
            else
                ++draws;
        }
        std::printf("    %-14s %3d games: wins %d / %d, draws %d, longest %d turns, max %d actions in a turn, %d picks, "
                    "%d refusals\n",
                    label, st.games, wins[0], wins[1], draws, st.maxTurns, st.maxActions, st.picks, st.refusals);
        CHECK(st.games == games);
        CHECK(st.refusals == 0);
        CHECK(st.notOver == 0);
        CHECK(st.maxTurns <= kMaxTurns);
        CHECK(st.maxActions <= kAiMaxActionsPerTurn + 1); // the bound plus the EndTurn
        CHECK(st.hardMaxSims <= kAiHardMaxSimulations);
        Merge(g_all, st);
    }

    void TestAiVersusAiNormal()
    {
        RunPairing("normal-normal", AiProfile::Normal, AiProfile::Normal, GamesPerPairing(), 101);
    }

    void TestAiVersusAiHardNormal()
    {
        RunPairing("hard-normal", AiProfile::Hard, AiProfile::Normal, GamesPerPairing(), 202);
        RunPairing("normal-hard", AiProfile::Normal, AiProfile::Hard, GamesPerPairing() / 2, 303);
    }

    void TestAiVersusAiHardHard()
    {
        RunPairing("hard-hard", AiProfile::Hard, AiProfile::Hard, std::max(10, GamesPerPairing() / 5), 404);
    }

    // 2. Mirrored matches: the same two decks, the controllers swapped (and the first player swapped), so deck
    // strength and luck of the draw cancel out.
    void TestHardBeatsNormal()
    {
        int const pairs = GamesPerPairing();
        int hardWins = 0, normalWins = 0, draws = 0;
        Stats st;
        for (int g = 0; g < pairs; ++g)
        {
            Pairing const p = MakePairing(1000 + g);
            uint32_t const seed = 7000u + static_cast<uint32_t>(g);
            // Game 1: Hard plays deck A (side 0, first), Normal plays deck B.
            GameResult const r1 = PlayGame(p.deck0, p.cls0, p.deck1, p.cls1, seed, 0, AiProfile::Hard, AiProfile::Normal, st);
            // Game 2: Normal plays deck A (side 0, first), Hard plays deck B (second).
            GameResult const r2 = PlayGame(p.deck0, p.cls0, p.deck1, p.cls1, seed + 500u, 0, AiProfile::Normal, AiProfile::Hard, st);
            for (int k = 0; k < 2; ++k)
            {
                GameResult const& r = k == 0 ? r1 : r2;
                int const hardSide = k == 0 ? 0 : 1;
                if (r.winner < 0)
                    ++draws;
                else if (r.winner == hardSide)
                    ++hardWins;
                else
                    ++normalWins;
            }
        }
        int const games = hardWins + normalWins + draws;
        double const rate = (static_cast<double>(hardWins) + 0.5 * draws) / static_cast<double>(std::max(1, games));
        std::printf("    Hard vs Normal, %d mirrored games: Hard %d, Normal %d, draws %d: Hard win rate %.1f%%\n", games,
                    hardWins, normalWins, draws, rate * 100.0);
        CHECK(st.refusals == 0 && st.notOver == 0);
        CHECK(games == 2 * pairs);
        CHECK(rate > 0.60);
        Merge(g_all, st);
    }

    // Every supported card shows up in a deck: decks are built from consecutive slices of each class's pool, so
    // cards other agents add later are covered automatically. Games are played to the end by both profiles.
    void TestEveryCardInPlay()
    {
        std::set<std::string> played;
        std::set<std::string> inDecks;
        Stats st;
        int games = 0;
        for (int c = 0; c < 9; ++c)
        {
            std::vector<std::string> const pool = Pool(kClasses[c]);
            char const* const oppClass = kClasses[(c + 4) % 9];
            std::vector<std::string> const opp = BuildDeck(oppClass, 90u + static_cast<uint32_t>(c));
            for (std::size_t start = 0; start < pool.size(); start += 15)
            {
                std::vector<std::string> deck;
                for (std::size_t i = 0; i < 15; ++i)
                {
                    std::string const& id = pool[(start + i) % pool.size()];
                    deck.push_back(id);
                    deck.push_back(id);
                    inDecks.insert(id);
                }
                for (int k = 0; k < 2; ++k)
                {
                    uint32_t const seed = 31000u + static_cast<uint32_t>(c * 100 + static_cast<int>(start) + k);
                    AiProfile const mine = k == 0 ? AiProfile::Hard : AiProfile::Normal;
                    PlayGame(deck, kClasses[c], opp, oppClass, seed, k, mine, AiProfile::Normal, st, &played);
                    ++games;
                }
            }
        }
        std::printf("    %d games with %d distinct cards in the decks, %d of them played\n", games,
                    static_cast<int>(inDecks.size()), static_cast<int>(played.size()));
        for (std::string const& id : inDecks)
            if (!played.count(id))
                std::printf("    never played: %s %s\n", id.c_str(), Db().Find(id)->name.c_str());
        CHECK(st.games == games);
        CHECK(st.refusals == 0 && st.notOver == 0);
        CHECK(st.maxActions <= kAiMaxActionsPerTurn + 1);
        CHECK(played.size() * 10 >= inDecks.size() * 8); // most cards got played at least once
        Merge(g_all, st);
    }

    // 3. Hidden information: the AI's decision does not depend on the other side's hand, deck or secrets.
    std::unique_ptr<Match> MidGame(uint32_t seed, int untilTurn, AiProfile profile, Pairing const& p)
    {
        std::unique_ptr<Match> m = CreateMatch(p.deck0, p.cls0, p.deck1, p.cls1, seed, 0);
        AiProfile const prof[2] = {AiProfile::Normal, profile};
        uint32_t const aiSeed[2] = {seed + 1u, seed + 2u};
        Stats st;
        AiMulligans(*m, prof, aiSeed, st);
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
            if (m->Turn() >= untilTurn && side == 1 && actions == 0 && m->ViewFor(1, 0).phase == Phase::Play)
                break;
            m->Apply(side, AiStep(*m, side, actions, prof[side], aiSeed[side], st));
            ++actions;
        }
        return m;
    }

    std::vector<std::string> RandomCards(std::mt19937& rng, std::vector<std::string> const& pool, std::size_t n)
    {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < n; ++i)
            out.push_back(pool[rng() % pool.size()]);
        return out;
    }

    void TestHiddenInformation()
    {
        std::vector<std::string> secrets;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            CardDef const* d = Db().Find(r.id);
            if (d && d->secret && d->type == CardType::Spell && Db().Supported(r.id))
                secrets.push_back(r.id);
        }
        int compared = 0, differentActions = 0, nonTrivial = 0;
        int const games = HS_AI_SANITIZED ? 12 : 40;
        for (int g = 0; g < games; ++g)
        {
            AiProfile const profile = (g % 2 == 0) ? AiProfile::Hard : AiProfile::Normal;
            Pairing const p = MakePairing(2000 + g);
            uint32_t const seed = 9100u + static_cast<uint32_t>(g);
            int const turn = 4 + (g % 7);
            std::unique_ptr<Match> m1 = MidGame(seed, turn, profile, p);
            std::unique_ptr<Match> m2 = MidGame(seed, turn, profile, p);
            if (m1->GetPhase() != Phase::Play || m1->ActiveSide() != 1)
                continue;
            CHECK(m1->DebugState() == m2->DebugState());
            // Replace everything side 0 (the "human") holds hidden, differently in the two matches.
            std::mt19937 rng(static_cast<uint32_t>(g) + 77u);
            std::vector<std::string> const pool0 = Pool("MAGE");
            std::vector<std::string> const pool1 = Pool("WARLOCK");
            std::size_t const hand = TA::HandIds(*m1, 0).size();
            std::size_t const deck = TA::DeckIds(*m1, 0).size();
            std::size_t const sec = TA::SecretCardIds(*m1, 0).size();
            TA::SetHand(*m1, 0, RandomCards(rng, pool0, hand));
            TA::SetHand(*m2, 0, RandomCards(rng, pool1, hand));
            TA::SetDeck(*m1, 0, RandomCards(rng, pool0, deck));
            TA::SetDeck(*m2, 0, RandomCards(rng, pool1, deck));
            if (sec > 0 && secrets.size() >= 2)
            {
                std::vector<std::string> s1, s2;
                for (std::size_t i = 0; i < sec; ++i)
                {
                    s1.push_back(secrets[(i * 2) % secrets.size()]);
                    s2.push_back(secrets[(i * 2 + 1) % secrets.size()]);
                }
                TA::SetSecrets(*m1, 0, s1);
                TA::SetSecrets(*m2, 0, s2);
            }
            CHECK(m1->DebugState() != m2->DebugState());
            for (int k = 0; k < 6; ++k)
            {
                if (m1->GetPhase() != Phase::Play || m1->ActiveSide() != 1)
                    break;
                Stats st;
                Action const a1 = AiStep(*m1, 1, k, profile, 555u + static_cast<uint32_t>(g), st);
                Action const a2 = AiStep(*m2, 1, k, profile, 555u + static_cast<uint32_t>(g), st);
                ++compared;
                if (!SameAction(a1, a2))
                    ++differentActions;
                if (a1.type != ActionType::EndTurn)
                    ++nonTrivial;
                CHECK(SameAction(a1, a2));
                if (a1.type == ActionType::EndTurn)
                    break;
                // Continue only while the two matches look the same from the AI's side.
                m1->Apply(1, a1);
                m2->Apply(1, a2);
                MatchView const v1 = m1->ViewFor(1, m1->LastEventSeq());
                MatchView const v2 = m2->ViewFor(1, m2->LastEventSeq());
                if (v1.legal.size() != v2.legal.size() || v1.self.hand.size() != v2.self.hand.size())
                    break;
            }
        }
        std::printf("    %d decisions compared (%d not EndTurn), %d differed\n", compared, nonTrivial, differentActions);
        CHECK(compared >= games);
        CHECK(differentActions == 0);
    }

    // 4. Determinism: the same state and aiSeed give the same action; whole games repeat exactly.
    void TestDeterminism()
    {
        int checked = 0;
        for (int g = 0; g < 10; ++g)
        {
            Pairing const p = MakePairing(3000 + g);
            AiProfile const profile = (g % 2 == 0) ? AiProfile::Hard : AiProfile::Normal;
            std::unique_ptr<Match> m1 = MidGame(4200u + static_cast<uint32_t>(g), 5 + g % 5, profile, p);
            std::unique_ptr<Match> m2 = MidGame(4200u + static_cast<uint32_t>(g), 5 + g % 5, profile, p);
            if (m1->GetPhase() != Phase::Play)
                continue;
            int const side = m1->ActiveSide();
            for (int k = 0; k < 5; ++k)
            {
                Stats st;
                Action const a1 = AiStep(*m1, side, k, profile, 31337u, st);
                Action const a2 = AiStep(*m2, side, k, profile, 31337u, st);
                Action const a3 = AiStep(*m1, side, k, profile, 31337u, st); // asking again changes nothing
                CHECK(SameAction(a1, a2) && SameAction(a1, a3));
                ++checked;
                if (a1.type == ActionType::EndTurn)
                    break;
                m1->Apply(side, a1);
                m2->Apply(side, a2);
            }
            CHECK(m1->DebugState() == m2->DebugState());
        }
        // Whole games repeat; another seed gives another game.
        Pairing const p = MakePairing(3100);
        Stats st;
        auto play = [&](uint32_t seed, AiProfile a, AiProfile b) {
            std::unique_ptr<Match> m = CreateMatch(p.deck0, p.cls0, p.deck1, p.cls1, seed, 0);
            AiProfile const prof[2] = {a, b};
            uint32_t const aiSeed[2] = {seed + 1u, seed + 2u};
            AiMulligans(*m, prof, aiSeed, st);
            int actions = 0, turn = m->Turn(), guard = 0;
            while (m->GetPhase() == Phase::Play && ++guard < 6000)
            {
                int const side = m->ActiveSide();
                if (m->Turn() != turn)
                {
                    turn = m->Turn();
                    actions = 0;
                }
                m->Apply(side, AiStep(*m, side, actions, prof[side], aiSeed[side], st));
                ++actions;
            }
            return m->DebugState();
        };
        std::string const g1 = play(8801, AiProfile::Hard, AiProfile::Normal);
        std::string const g2 = play(8801, AiProfile::Hard, AiProfile::Normal);
        std::string const g3 = play(8802, AiProfile::Hard, AiProfile::Normal);
        CHECK(g1 == g2);
        CHECK(g1 != g3);
        CHECK(st.refusals == 0);
        CHECK(checked > 10);
    }

    // 5. Lethal: constructed positions where a win this turn exists; Hard plays it out before ending the turn.
    bool WinsThisTurn(Match& m, AiProfile profile, int& actionsUsed)
    {
        Stats st;
        for (int k = 0; k < kAiMaxActionsPerTurn && m.GetPhase() == Phase::Play; ++k)
        {
            Action const a = AiStep(m, 0, k, profile, 99u, st);
            if (a.type == ActionType::EndTurn)
                break;
            if (m.Apply(0, a) != ActionError::None)
                return false;
            ++actionsUsed;
        }
        return m.GetPhase() == Phase::Over && m.GetResult().winner == 0;
    }

    void TestLethal()
    {
        int normalWins = 0;
        for (AiProfile const profile : {AiProfile::Hard, AiProfile::Normal})
        {
            int wins = 0;
            int used = 0;
            int scenario = 0;
            auto expect = [&](std::unique_ptr<Match> m) {
                ++scenario;
                bool const won = WinsThisTurn(*m, profile, used);
                wins += won ? 1 : 0;
                if (profile == AiProfile::Hard)
                {
                    if (!won)
                        std::printf("    Hard missed lethal in scenario %d\n", scenario);
                    CHECK(won);
                }
            };
            // Attackers and a Fireball: 4 on board + 6.
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 1, 10, 0);
                TA::SetMana(*m, 0, 4, 4);
                TA::SetBoard(*m, 0, {"CS2_200"});
                TA::SetBoard(*m, 1, {"CS2_182"});
                TA::SetHand(*m, 0, {"CS2_029"});
                // Enemy Yeti has no Taunt: the Boulderfist is not needed.
                expect(std::move(m));
            }
            // Two Taunts in the way: kill both, then the Ogre goes face for 6.
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 1, 6, 0);
                TA::SetMana(*m, 0, 0, 0);
                TA::SetBoard(*m, 0, {"CS2_182", "CS2_120", "CS2_200"});
                TA::SetBoard(*m, 1, {"CS1_042", "CS2_121"});
                expect(std::move(m));
            }
            // Weapon plus Fireball: Fiery War Axe (3) and Fireball (6) for 9.
            {
                std::unique_ptr<Match> m = Fresh("WARRIOR", "MAGE");
                TA::SetHero(*m, 1, 9, 0);
                TA::SetMana(*m, 0, 6, 6);
                TA::SetBoard(*m, 1, {"CS2_182"});
                TA::SetHand(*m, 0, {"CS2_029", "CS2_106"});
                expect(std::move(m));
            }
            // Charge minions from the hand: Rocketeer 5 + Wolfrider 3 + Boar 1 = 9 with 10 mana.
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 1, 9, 0);
                TA::SetMana(*m, 0, 10, 10);
                TA::SetHand(*m, 0, {"CS2_171", "CS2_213", "CS2_124"});
                expect(std::move(m));
            }
            // Burn only: Fireball 6 + Frostbolt 3 + Fireblast 1 = 10 for 8 mana (an enemy minion to tempt it).
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 1, 10, 0);
                TA::SetMana(*m, 0, 8, 8);
                TA::SetBoard(*m, 1, {"CS2_182", "CS2_120"});
                TA::SetHand(*m, 0, {"CS2_029", "CS2_024"});
                expect(std::move(m));
            }
            // Armor counts: 4 armor + 6 health = 10 for Fireball + 4 attack.
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 1, 6, 4);
                TA::SetMana(*m, 0, 4, 4);
                TA::SetBoard(*m, 0, {"CS2_200"});
                TA::SetHand(*m, 0, {"CS2_029"});
                expect(std::move(m));
            }
            if (profile == AiProfile::Normal)
                normalWins = wins;
            else
                CHECK(wins == 6);
        }
        std::printf("    Normal finds %d of 6 lethal positions (Hard: all)\n", normalWins);
    }

    // Hard does not throw a game away: facing lethal next turn it makes a board the enemy cannot attack through.
    void TestSurvival()
    {
        std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
        TA::SetHero(*m, 0, 6, 0);
        TA::SetMana(*m, 0, 3, 3);
        std::vector<EntityId> const foe = TA::SetBoard(*m, 1, {"CS2_200"}); // Boulderfist Ogre 6/7
        TA::SetHand(*m, 0, {"CS1_042", "CS2_168", "CS2_023"});               // Footman (Taunt), Murloc, Arcane Intellect
        Stats st;
        for (int k = 0; k < kAiMaxActionsPerTurn; ++k)
        {
            Action const a = AiStep(*m, 0, k, AiProfile::Hard, 5u, st);
            if (a.type == ActionType::EndTurn)
                break;
            CHECK(m->Apply(0, a) == ActionError::None);
        }
        CHECK(st.refusals == 0);
        // The enemy's turn: the Ogre cannot hit the hero through the Taunt.
        CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::None);
        Action hit;
        hit.type = ActionType::Attack;
        hit.source = foe[0];
        hit.target = TA::HeroId(*m, 0);
        TA::ReadyAll(*m);
        CHECK(m->Apply(1, hit) == ActionError::Taunt);
    }

    // A Taunt with more health than the board can kill in one turn is still attacked: the AI wears it down
    // (the damage stays) instead of ending the turn with its minions idle behind it.
    void TestAttacksBigTaunt()
    {
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
        {
            for (int health : {8, 12, 30})
            for (int tatk : {1, 4, 6})
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 0, 30, 0);
                TA::SetHero(*m, 1, 30, 0);
                TA::SetMana(*m, 0, 0, 0);
                TA::SetBoard(*m, 0, {"CS2_182", "CS2_182"});                       // two Chillwind Yetis 4/5
                std::vector<EntityId> const foe = TA::SetBoard(*m, 1, {"CS1_042"}); // Goldshire Footman, Taunt
                TA::SetMinion(*m, foe[0], tatk, health, health);
                TA::ReadyAll(*m);
                Stats st;
                int attacks = 0;
                for (int k = 0; k < kAiMaxActionsPerTurn; ++k)
                {
                    Action const a = AiStep(*m, 0, k, profile, 5u, st);
                    if (a.type == ActionType::EndTurn)
                        break;
                    if (a.type == ActionType::Attack && a.target == foe[0])
                        ++attacks;
                    CHECK(m->Apply(0, a) == ActionError::None);
                }
                if (attacks == 0)
                    std::printf("    %s ignored a %d/%d Taunt\n", AiProfileCode(profile), tatk, health);
                CHECK(attacks > 0);
            }
        }
    }

    // Normal is allowed to err, but not in the same systematic way every game: facing a lethal threat on the enemy's
    // board it usually puts a Taunt in the way. (Counts over many seeds; the noise makes single games differ.)
    void TestNormalHoles()
    {
        int const runs = 40;
        int blocked = 0;
        for (int seed = 1; seed <= runs; ++seed)
        {
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
                TA::SetHero(*m, 0, 6, 0);
                TA::SetMana(*m, 0, 3, 3);
                std::vector<EntityId> const foe = TA::SetBoard(*m, 1, {"CS2_200"}); // Boulderfist Ogre 6/7
                TA::SetHand(*m, 0, {"CS1_042", "CS2_168", "CS2_023"});               // Footman (Taunt), Murloc, Arcane Intellect
                Stats st;
                for (int k = 0; k < kAiMaxActionsPerTurn; ++k)
                {
                    Action const a = AiStep(*m, 0, k, AiProfile::Normal, static_cast<uint32_t>(seed), st);
                    if (a.type == ActionType::EndTurn)
                        break;
                    CHECK(m->Apply(0, a) == ActionError::None);
                }
                CHECK(m->Apply(0, Simple(ActionType::EndTurn)) == ActionError::None);
                Action hit;
                hit.type = ActionType::Attack;
                hit.source = foe[0];
                hit.target = TA::HeroId(*m, 0);
                TA::ReadyAll(*m);
                if (m->Apply(1, hit) == ActionError::Taunt)
                    ++blocked;
            }
        }
        std::printf("    Normal blocked the lethal threat in %d of %d games\n", blocked, runs);
        CHECK(blocked * 10 >= runs * 7);
    }

    // Pending picks (Tracking) are answered by both profiles with one of the offered cards.
    void TestPendingPick()
    {
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
        {
            std::unique_ptr<Match> m = Fresh("HUNTER", "MAGE");
            std::vector<EntityId> const h = TA::SetHand(*m, 0, {"DS1_184"});
            TA::SetDeck(*m, 0, {"CS2_168", "CS2_200", "CS2_182", "CS2_168", "CS2_168"});
            Action play;
            play.type = ActionType::Play;
            play.source = h[0];
            CHECK(m->Apply(0, play) == ActionError::None);
            MatchView const v = m->ViewFor(0, m->LastEventSeq());
            CHECK(v.pickPending && v.pick.size() == 3);
            Action const a = ChooseAiAction(*m, 0, Db(), 1, profile, 42u);
            bool offered = false;
            for (PickView const& p : v.pick)
                offered = offered || p.id == a.source;
            CHECK(a.type == ActionType::Choose && offered);
            CHECK(m->Apply(0, a) == ActionError::None);
            CHECK(!m->ViewFor(0, m->LastEventSeq()).pickPending);
            // The view-only function answers too.
            std::unique_ptr<Match> n = Fresh("HUNTER", "MAGE");
            std::vector<EntityId> const h2 = TA::SetHand(*n, 0, {"DS1_184"});
            TA::SetDeck(*n, 0, {"CS2_168", "CS2_200", "CS2_182", "CS2_168", "CS2_168"});
            play.source = h2[0];
            n->Apply(0, play);
            Action const b = ChooseAiAction(n->ViewFor(0, 0), Db(), 1);
            CHECK(b.type == ActionType::Choose && n->Apply(0, b) == ActionError::None);
        }
    }

    // Mulligans: both profiles return legal subsets; Hard keeps cheap cards and replaces expensive ones.
    void TestMulligan()
    {
        int hardReplacedExpensive = 0, hardKeptCheap = 0, expensive = 0, cheap = 0;
        for (int g = 0; g < 60; ++g)
        {
            Pairing const p = MakePairing(5000 + g);
            std::unique_ptr<Match> m = CreateMatch(p.deck0, p.cls0, p.deck1, p.cls1, 6000u + static_cast<uint32_t>(g), g % 2);
            for (int side = 0; side < 2; ++side)
            {
                MatchView const v = m->ViewFor(side, 0);
                for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
                {
                    Action const a = ChooseAiAction(*m, side, Db(), 0, profile, 1u);
                    CHECK(a.type == ActionType::Mulligan);
                    for (EntityId id : a.replace)
                        CHECK(std::find(v.mulligan.begin(), v.mulligan.end(), id) != v.mulligan.end());
                    if (profile == AiProfile::Hard)
                        for (HandCardView const& h : v.self.hand)
                        {
                            bool const replaced = std::find(a.replace.begin(), a.replace.end(), h.id) != a.replace.end();
                            if (h.cost >= 5)
                            {
                                ++expensive;
                                hardReplacedExpensive += replaced ? 1 : 0;
                            }
                            if (h.cost <= 2)
                            {
                                ++cheap;
                                hardKeptCheap += replaced ? 0 : 1;
                            }
                        }
                }
                // The view-only function (Normal) still works.
                Action const b = ChooseAiAction(v, Db(), 0);
                CHECK(b.type == ActionType::Mulligan);
                CHECK(m->Apply(side, b) == ActionError::None);
            }
        }
        CHECK(expensive > 0 && hardReplacedExpensive == expensive);
        CHECK(cheap > 0 && hardKeptCheap == cheap);
        std::printf("    Hard mulligan: replaced %d of %d expensive cards, kept %d of %d cheap cards\n",
                    hardReplacedExpensive, expensive, hardKeptCheap, cheap);
    }

    // Edge cases: not its turn, match over, action bound, hand-built boards with odd cards.
    void TestEdgeCases()
    {
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
            TA::SetHand(*m, 0, {"CS2_168", "CS2_029"});
            CHECK(ChooseAiAction(*m, 1, Db(), 0, profile, 1u).type == ActionType::EndTurn); // not its turn
            CHECK(ChooseAiAction(*m, 0, Db(), kAiMaxActionsPerTurn, profile, 1u).type == ActionType::EndTurn);
            CHECK(ChooseAiAction(*m, 0, Db(), 0, profile, 1u).type != ActionType::Mulligan);
            CHECK(ChooseAiAction(*m, 5, Db(), 0, profile, 1u).type == ActionType::EndTurn);
            m->Apply(1, Simple(ActionType::Concede));
            CHECK(ChooseAiAction(*m, 0, Db(), 0, profile, 1u).type == ActionType::EndTurn); // over
        }
        CHECK(std::string(AiProfileCode(AiProfile::Normal)) == "normal");
        CHECK(std::string(AiProfileCode(AiProfile::Hard)) == "hard");
    }

    // A card or hero power that only helps its target is never aimed at the enemy: not Power Word: Shield for its
    // draw, not Lesser Heal on an enemy minion that dies to an attack afterwards anyway (both looked like a way to
    // spend mana). Priest AIs with heals and buffs against a minion deck, every action of both sides checked.
    void TestHelpsOnlyFriends()
    {
        std::vector<std::string> const priest = {"CS2_004", "CS2_004", "CS2_235", "CS2_235", "CS2_236", "CS2_236",
            "CS1_130", "CS1_130", "CS2_234", "CS2_234", "EX1_019", "EX1_019", "CS2_117", "CS2_117", "EX1_011",
            "EX1_011", "CS2_182", "CS2_182", "CS2_120", "CS2_120", "CS2_179", "CS2_179", "CS2_200", "CS2_200",
            "CS2_168", "CS2_168", "CS2_172", "CS2_172", "CS2_124", "CS2_124"};
        std::vector<std::string> const minions = {"CS2_182", "CS2_182", "CS2_120", "CS2_120", "CS2_179", "CS2_179",
            "CS2_200", "CS2_200", "CS2_168", "CS2_168", "CS2_172", "CS2_172", "CS2_124", "CS2_124", "CS1_042",
            "CS1_042", "CS2_186", "CS2_186", "CS2_121", "CS2_121", "CS2_127", "CS2_127", "CS2_131", "CS2_131",
            "CS2_141", "CS2_141", "CS2_147", "CS2_147", "CS2_150", "CS2_150"};
        std::set<std::string> const helpful = {"CS2_004", "CS1h_001", "CS2_236", "EX1_019", "CS2_117", "EX1_011"};
        int targeted = 0, atEnemy = 0;
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
            for (uint32_t g = 0; g < 12; ++g)
            {
                std::unique_ptr<Match> m = CreateMatch(minions, "WARRIOR", priest, "PRIEST", 1000u + g, static_cast<int>(g % 2));
                if (!m)
                    continue;
                for (int side = 0; side < 2; ++side)
                    m->Apply(side, ChooseAiAction(*m, side, Db(), 0, profile, 7u + static_cast<uint32_t>(side)));
                int actions = 0, turn = m->Turn();
                for (int guard = 0; guard < 4000 && m->GetPhase() == Phase::Play; ++guard)
                {
                    int const side = m->ActiveSide();
                    if (m->Turn() != turn)
                    {
                        turn = m->Turn();
                        actions = 0;
                    }
                    MatchView const v = m->ViewFor(side, m->LastEventSeq());
                    Action const a = ChooseAiAction(*m, side, Db(), actions, profile, (1000u + g) * 31u + m->Seq());
                    std::string card = a.type == ActionType::HeroPower ? v.self.hero.powerCardId : std::string();
                    if (a.type == ActionType::Play)
                        for (HandCardView const& h : v.self.hand)
                            if (h.id == a.source)
                                card = h.cardId;
                    if (a.target != 0 && helpful.count(card))
                    {
                        ++targeted;
                        for (MinionView const& mv : v.other.board)
                            if (mv.id == a.target)
                            {
                                ++atEnemy;
                                std::printf("    %s game %u turn %d: %s on the enemy %s\n", AiProfileCode(profile), g, m->Turn(),
                                            card.c_str(), mv.cardId.c_str());
                            }
                    }
                    if (m->Apply(side, a) != ActionError::None)
                        m->Apply(side, Simple(ActionType::Concede));
                    ++actions;
                }
            }
        std::printf("    %d targeted heals and buffs, %d at an enemy minion\n", targeted, atEnemy);
        CHECK(targeted > 50);
        CHECK(atEnemy == 0);
    }

    // Doomsayer destroys every minion at the start of its owner's turn: never played onto empty boards (it would
    // only destroy itself), but played to answer a board the AI cannot beat otherwise (Hard).
    void TestDoomsayer()
    {
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
            for (uint32_t seed = 1; seed <= 4; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                TA::SetMana(*m, 0, 2, 2);
                TA::SetHeroPowerUsed(*m, 0, true);
                TA::SetHand(*m, 0, {"NEW1_021"});
                bool played = false;
                for (int step = 0; step < kAiMaxActionsPerTurn && m->GetPhase() == Phase::Play; ++step)
                {
                    Action const a = ChooseAiAction(*m, 0, Db(), step, profile, seed * 13u + static_cast<uint32_t>(step));
                    played = played || a.type == ActionType::Play;
                    if (m->Apply(0, a) != ActionError::None || a.type == ActionType::EndTurn)
                        break;
                }
                if (played)
                    std::printf("    %s seed %u: Doomsayer onto empty boards\n", AiProfileCode(profile), seed);
                CHECK(!played);
            }
        // Two Boulderfist Ogres against nothing: Doomsayer takes them with it.
        std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", 3);
        TA::SetMana(*m, 0, 2, 2);
        TA::SetHeroPowerUsed(*m, 0, true);
        TA::SetHero(*m, 0, 20, 0);
        TA::SetBoard(*m, 1, {"CS2_200", "CS2_200"});
        std::vector<EntityId> const hand = TA::SetHand(*m, 0, {"NEW1_021"});
        Action const a = ChooseAiAction(*m, 0, Db(), 0, AiProfile::Hard, 5u);
        CHECK(a.type == ActionType::Play && a.source == hand[0]);
    }

    // Rockbiter Weapon (+3 attack this turn) is never put on an own minion that cannot attack this turn (summoning
    // sick: played earlier in the same turn). Chillwind Yeti then Rockbiter in hand, every action checked.
    void TestRockbiterNotOnSickMinion()
    {
        int rockbiters = 0;
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
            for (uint32_t seed = 1; seed <= 6; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("SHAMAN", "WARRIOR", seed);
                TA::SetMana(*m, 0, 10, 10);
                TA::SetHeroPowerUsed(*m, 0, true);
                TA::SetHand(*m, 0, {"CS2_182", "CS2_045", "CS2_045"});
                for (int step = 0; step < kAiMaxActionsPerTurn && m->GetPhase() == Phase::Play; ++step)
                {
                    MatchView const v = m->ViewFor(0, m->LastEventSeq());
                    Action const a = ChooseAiAction(*m, 0, Db(), step, profile, seed * 17u + static_cast<uint32_t>(step));
                    if (a.type == ActionType::Play)
                        for (HandCardView const& h : v.self.hand)
                            if (h.id == a.source && h.cardId == "CS2_045")
                            {
                                ++rockbiters;
                                for (MinionView const& mv : v.self.board)
                                    if (mv.id == a.target)
                                    {
                                        std::printf("    %s seed %u: Rockbiter Weapon on a minion that cannot attack\n",
                                                    AiProfileCode(profile), seed);
                                        CHECK(mv.canAttack);
                                    }
                            }
                    if (m->Apply(0, a) != ActionError::None || a.type == ActionType::EndTurn)
                        break;
                }
            }
        std::printf("    %d Rockbiter Weapon plays checked\n", rockbiters);
    }

    // Kel'Thuzad brings back every minion that died this turn, so killing his neighbours while he stands is a waste:
    // a Boulderfist Ogre that could kill a Chillwind Yeti next to him does not (both profiles).
    void TestNoKillsNextToKelThuzad()
    {
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
            for (uint32_t seed = 1; seed <= 6; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                TA::SetMana(*m, 0, 0, 0);
                TA::SetHeroPowerUsed(*m, 0, true);
                TA::SetHand(*m, 0, {});
                std::vector<EntityId> const mine = TA::SetBoard(*m, 0, {"CS2_200"});
                std::vector<EntityId> const theirs = TA::SetBoard(*m, 1, {"FP1_013", "CS2_182"});
                Action const a = ChooseAiAction(*m, 0, Db(), 0, profile, seed * 29u);
                if (a.type == ActionType::Attack && a.target == theirs[1])
                    std::printf("    %s seed %u: attacks the Chillwind Yeti next to Kel'Thuzad\n", AiProfileCode(profile), seed);
                CHECK(!(a.type == ActionType::Attack && a.source == mine[0] && a.target == theirs[1]));
            }
    }

    // Defender of Argus goes between two minions (its Battlecry buffs both neighbours), on both profiles.
    void TestArgusBetweenMinions()
    {
        for (AiProfile const profile : {AiProfile::Normal, AiProfile::Hard})
            for (uint32_t seed = 1; seed <= 4; ++seed)
            {
                std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", seed);
                TA::SetMana(*m, 0, 4, 4);
                TA::SetHeroPowerUsed(*m, 0, true);
                std::vector<EntityId> const own = TA::SetBoard(*m, 0, {"CS2_182", "CS2_182"});
                std::vector<EntityId> const hand = TA::SetHand(*m, 0, {"EX1_093"});
                Action const a = ChooseAiAction(*m, 0, Db(), 0, profile, seed * 17u);
                CHECK(a.type == ActionType::Play && a.source == hand[0]);
                if (m->Apply(0, a) != ActionError::None)
                {
                    CHECK(false);
                    continue;
                }
                int taunts = 0;
                for (MinionView const& mv : m->ViewFor(0, m->LastEventSeq()).self.board)
                    if ((mv.id == own[0] || mv.id == own[1]) && mv.taunt)
                        ++taunts;
                if (taunts != 2)
                    std::printf("    %s seed %u: Argus at slot %d buffed %d minions\n", AiProfileCode(profile), seed, a.position, taunts);
                CHECK(taunts == 2);
            }
    }

    // Secrets are played when there is mana for them (both profiles), and a turn where nothing else is worth
    // doing still ends.
    void TestSecretsArePlayed()
    {
        int tried = 0, hardPlayed = 0, normalPlayed = 0;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            CardDef const* d = Db().Find(r.id);
            if (!d || !d->secret || d->type != CardType::Spell || !Db().Supported(r.id))
                continue;
            for (AiProfile const profile : {AiProfile::Hard, AiProfile::Normal})
            {
                std::unique_ptr<Match> m = Fresh(d->cardClass == "NEUTRAL" ? "MAGE" : d->cardClass.c_str(), "WARRIOR");
                TA::SetHand(*m, 0, {r.id});
                TA::SetBoard(*m, 1, {"CS2_182"});
                TA::SetMana(*m, 0, 10, 10);
                Stats st;
                for (int k = 0; k < 6; ++k)
                {
                    Action const a = AiStep(*m, 0, k, profile, 5u, st);
                    if (a.type == ActionType::EndTurn || m->Apply(0, a) != ActionError::None)
                        break;
                }
                bool const played = TA::HandIds(*m, 0).empty();
                CHECK(st.refusals == 0);
                if (profile == AiProfile::Hard)
                {
                    ++tried;
                    hardPlayed += played ? 1 : 0;
                    if (!played)
                        std::printf("    Hard does not play %s %s\n", r.id, d->name.c_str());
                }
                else
                    normalPlayed += played ? 1 : 0;
            }
        }
        std::printf("    %d secrets: Hard plays %d, Normal plays %d\n", tried, hardPlayed, normalPlayed);
        CHECK(tried == 0 || hardPlayed == tried);
        CHECK(tried == 0 || normalPlayed == tried);
    }

    // Minions that buff their neighbours (Flametongue Totem, Dire Wolf Alpha) are placed between two minions by Hard.
    // Looks for such a card among the supported ones; nothing to check until a card agent adds one.
    void TestAdjacencyPlacement()
    {
        std::string card;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            Detail::Script const* sc = Detail::FindScript(r.id);
            CardDef const* d = Db().Find(r.id);
            if (!sc || !d || d->type != CardType::Minion || !Db().Supported(r.id) || d->cost > 6)
                continue;
            for (Detail::AuraSpec const& a : sc->auras)
                if (a.scope == Detail::AuraScope::Adjacent && (a.kind == Detail::AuraKind::Attack || a.kind == Detail::AuraKind::AttackHealth))
                    card = r.id;
        }
        if (card.empty())
        {
            std::printf("    no supported minion with an adjacency aura yet: skipped\n");
            return;
        }
        std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR");
        TA::SetBoard(*m, 0, {"CS2_182", "CS2_182", "CS2_182"});
        TA::SetHand(*m, 0, {card});
        TA::SetMana(*m, 0, 10, 10);
        Stats st;
        Action const a = AiStep(*m, 0, 0, AiProfile::Hard, 3u, st);
        CHECK(a.type == ActionType::Play);
        CHECK(a.position == 1 || a.position == 2);
        CHECK(m->Apply(0, a) == ActionError::None);
    }

    // The most expensive kind of position: full boards, a full hand, everything affordable. The decision stays
    // within the simulation budget and a sane time.
    void TestWorstCase()
    {
        double worstMs = 0.0;
        int worstSims = 0;
        for (int variant = 0; variant < 4; ++variant)
        {
            std::unique_ptr<Match> m = Fresh(variant % 2 ? "MAGE" : "SHAMAN", variant < 2 ? "WARRIOR" : "PRIEST", 21u + static_cast<uint32_t>(variant));
            std::vector<std::string> const mine = {"CS2_182", "CS2_120", "CS2_200", "CS2_168", "CS2_171", "CS2_124", "CS2_179"};
            std::vector<std::string> const theirs = {"CS2_182", "CS2_121", "CS1_042", "CS2_168", "CS2_125", "CS2_131", "CS2_118"};
            TA::SetBoard(*m, 0, mine);
            TA::SetBoard(*m, 1, theirs);
            TA::SetHand(*m, 0, {"CS2_029", "CS2_024", "EX1_277", "CS2_182", "CS2_168", "CS2_120", "CS2_106", "CS2_025", "CS2_023", "CS2_022"});
            TA::SetMana(*m, 0, 10, 10);
            Stats st;
            for (int k = 0; k < 6; ++k)
            {
                AiDecisionInfo info;
                Clock::time_point const t0 = Clock::now();
                Action const a = ChooseAiAction(*m, 0, Db(), k, AiProfile::Hard, 8u, &info);
                double const ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                worstMs = std::max(worstMs, ms);
                worstSims = std::max(worstSims, info.simulations);
                CHECK(info.simulations <= kAiHardMaxSimulations);
                if (a.type == ActionType::EndTurn || m->Apply(0, a) != ActionError::None)
                    break;
            }
        }
        std::printf("    worst case position: %.2f ms, %d simulations%s\n", worstMs, worstSims,
                    HS_AI_SANITIZED ? " [sanitizer build: time inflated]" : "");
#if !HS_AI_SANITIZED
        CHECK(worstMs < 100.0);
#endif
    }

    void TestTimingReport()
    {
        double const avg = g_all.hardCalls ? g_all.hardMs / static_cast<double>(g_all.hardCalls) : 0.0;
        double const avgSims = g_all.hardCalls ? static_cast<double>(g_all.hardSims) / static_cast<double>(g_all.hardCalls) : 0.0;
        double const navg = g_all.normalCalls ? g_all.normalMs / static_cast<double>(g_all.normalCalls) : 0.0;
        std::printf("    Hard decisions: %ld, avg %.2f ms, max %.2f ms, avg %.0f simulations, max %d (budget %d), "
                    "%d with lethal found%s\n",
                    g_all.hardCalls, avg, g_all.hardMaxMs, avgSims, g_all.hardMaxSims, kAiHardMaxSimulations,
                    g_all.lethalFound, HS_AI_SANITIZED ? " [sanitizer build: times are inflated]" : "");
        std::printf("    Normal decisions: %ld, avg %.2f ms, max %.2f ms\n", g_all.normalCalls, navg, g_all.normalMaxMs);
        CHECK(g_all.hardCalls > 0);
        CHECK(g_all.hardMaxSims <= kAiHardMaxSimulations);
#if !HS_AI_SANITIZED
        CHECK(avg < 20.0);
#endif
    }
}

int main(int argc, char** argv)
{
    if (argc > 2)
        g_gamesArg = std::atoi(argv[2]);
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep the output of a crashing test
    RUN(TestDecks);
    RUN(TestEdgeCases);
    RUN(TestMulligan);
    RUN(TestPendingPick);
    RUN(TestLethal);
    RUN(TestSurvival);
    RUN(TestAttacksBigTaunt);
    RUN(TestNormalHoles);
    RUN(TestDeterminism);
    RUN(TestHiddenInformation);
    RUN(TestAiVersusAiNormal);
    RUN(TestAiVersusAiHardNormal);
    RUN(TestAiVersusAiHardHard);
    RUN(TestEveryCardInPlay);
    RUN(TestHardBeatsNormal);
    RUN(TestHelpsOnlyFriends);
    RUN(TestRockbiterNotOnSickMinion);
    RUN(TestNoKillsNextToKelThuzad);
    RUN(TestDoomsayer);
    RUN(TestArgusBetweenMinions);
    RUN(TestSecretsArePlayed);
    RUN(TestAdjacencyPlacement);
    RUN(TestWorstCase);
    RUN(TestTimingReport);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
