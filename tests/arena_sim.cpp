// Draft arena AI simulation: drafts two AI decks per game (src/arena/HsArenaDraftAi.cpp, the
// same offer rules as a player's draft) for a random class pair and plays them Hard vs Hard on the match engine.
// Reports per class: games, win / draw / loss, win rate, average mana cost of the drafted decks, average game length;
// and the refused actions, unfinished games and runtimes. Not part of the default test run.
//   arena_sim.exe [directory] [games] [seed]     games default 200, seed default 1; exit code 1 when an AI action
//                                                was refused or a game did not finish.

#include "../src/arena/HsArenaDraftAi.h"
#include "../src/arena/HsArenaRules.h"
#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "card_catalog.h"
#include "match_all_cards.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace M = Hs::Match;
using Clock = std::chrono::steady_clock;

namespace
{
    // The dataset as the module sees it (same as tests/arena_tests.cpp).
    std::shared_ptr<Hs::CardCatalog const> Catalog()
    {
        static std::shared_ptr<Hs::CardCatalog const> const catalog = []
        {
            auto c = std::make_shared<Hs::CardCatalog>();
            std::map<std::string, MatchAllCards::Row const*> rows;
            for (MatchAllCards::Row const& r : MatchAllCards::kRows)
                rows[r.id] = &r;
            for (::CardCatalog::Row const& r : ::CardCatalog::kRows)
            {
                auto it = rows.find(r.id);
                if (it == rows.end())
                    continue;
                c->Add(r.id, r.collectible != 0, r.acquisition, it->second->rarity, it->second->cls);
            }
            return std::shared_ptr<Hs::CardCatalog const>(c);
        }();
        return catalog;
    }

    M::CardDb const& Db() { return MatchAllCards::Db(); }

    struct Totals
    {
        int refusals = 0; // actions the engine refused or that were not in the legal list
        int createFailed = 0;
        int notOver = 0;
        int maxActions = 0;
        long hardCalls = 0;
        double hardMs = 0;
        double hardMaxMs = 0;
    };

    struct ClassStats
    {
        int games = 0, wins = 0, draws = 0, losses = 0;
        long turns = 0;
        long deckCost = 0; // summed mana cost of the drafted decks
        int decks = 0;
        int lowCards = 0;  // cards of 3 mana or less
        int minions = 0;
    };

    std::unique_ptr<M::Match> CreateMatch(std::vector<std::string> const& deck0, std::string const& cls0,
                                          std::vector<std::string> const& deck1, std::string const& cls1, uint32_t seed,
                                          int firstSide)
    {
        M::MatchSetup s;
        s.seed = seed;
        s.firstSide = firstSide;
        s.sides[0].name = "A";
        s.sides[0].heroClass = cls0;
        s.sides[0].deck = deck0;
        s.sides[1].name = "B";
        s.sides[1].heroClass = cls1;
        s.sides[1].deck = deck1;
        std::string error;
        std::unique_ptr<M::Match> m = M::Match::Create(Db(), s, error);
        if (!m)
            std::printf("  create failed: %s\n", error.c_str());
        return m;
    }

    bool InLegal(M::MatchView const& v, M::Action const& a)
    {
        for (M::Action const& l : v.legal)
            if (l.type == a.type && l.source == a.source && l.target == a.target && l.choice == a.choice)
                return true;
        return false;
    }

    // Both sides mulligan with their AI.
    void AiMulligans(M::Match& m, uint32_t const aiSeed[2], Totals& t)
    {
        for (int side = 0; side < 2; ++side)
        {
            M::MatchView const v = m.ViewFor(side, 0);
            M::Action const a = M::ChooseAiAction(m, side, Db(), 0, M::AiProfile::Hard, aiSeed[side]);
            bool ok = a.type == M::ActionType::Mulligan && v.mulliganPending;
            for (M::EntityId id : a.replace)
                ok = ok && std::find(v.mulligan.begin(), v.mulligan.end(), id) != v.mulligan.end();
            if (!ok || m.Apply(side, a) != M::ActionError::None)
                ++t.refusals;
        }
    }

    // Asks the Hard AI for the next action of `side` and checks it against the legal list / the pending pick.
    M::Action AiStep(M::Match const& m, int side, int actions, uint32_t aiSeed, Totals& t)
    {
        M::MatchView const v = m.ViewFor(side, m.LastEventSeq());
        Clock::time_point const t0 = Clock::now();
        M::Action const a = M::ChooseAiAction(m, side, Db(), actions, M::AiProfile::Hard, aiSeed);
        double const ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        t.hardMs += ms;
        t.hardMaxMs = std::max(t.hardMaxMs, ms);
        ++t.hardCalls;
        if (v.pickPending)
        {
            bool found = false;
            for (M::PickView const& p : v.pick)
                found = found || p.id == a.source;
            if (a.type != M::ActionType::Choose || !found)
                ++t.refusals;
        }
        else if (a.type != M::ActionType::EndTurn && !InLegal(v, a))
            ++t.refusals;
        return a;
    }

    // Plays one game; returns the winner side, -1 for a draw (or a game that did not finish), -2 when it could not
    // be created. turns receives the turn count.
    int PlayGame(std::vector<std::string> const& deck0, std::string const& cls0, std::vector<std::string> const& deck1,
                 std::string const& cls1, uint32_t seed, int firstSide, Totals& t, int& turns)
    {
        std::unique_ptr<M::Match> m = CreateMatch(deck0, cls0, deck1, cls1, seed, firstSide);
        if (!m)
        {
            ++t.createFailed;
            return -2;
        }
        uint32_t const aiSeed[2] = { seed * 7919u + 1u, seed * 104729u + 2u };
        AiMulligans(*m, aiSeed, t);
        int actions = 0;
        int turn = m->Turn();
        int guard = 0;
        while (m->GetPhase() == M::Phase::Play && ++guard < 6000)
        {
            int const side = m->ActiveSide();
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                actions = 0;
            }
            M::Action const a = AiStep(*m, side, actions, aiSeed[side], t);
            if (m->Apply(side, a) != M::ActionError::None)
            {
                ++t.refusals;
                M::Action concede;
                concede.type = M::ActionType::Concede; // cannot continue
                m->Apply(side, concede);
            }
            ++actions;
            t.maxActions = std::max(t.maxActions, actions);
        }
        turns = m->Turn();
        if (m->GetPhase() != M::Phase::Over)
        {
            ++t.notOver;
            return -1;
        }
        return m->GetResult().winner;
    }

    void CountDeck(ClassStats& cs, std::vector<std::string> const& deck)
    {
        ++cs.decks;
        for (std::string const& id : deck)
        {
            M::CardDef const* def = Db().Find(id);
            if (!def)
                continue;
            cs.deckCost += def->cost;
            cs.lowCards += def->cost <= 3 ? 1 : 0;
            cs.minions += def->type == M::CardType::Minion ? 1 : 0;
        }
    }
}

int main(int argc, char** argv)
{
    int const games = argc > 2 ? std::max(1, std::atoi(argv[2])) : 200;
    uint64_t const seed = argc > 3 ? static_cast<uint64_t>(std::atoll(argv[3])) : 1;
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::array<uint32_t, Hs::kPackRarityCount> const weights = Hs::ArenaSettings().rarityWeights;
    std::map<std::string, Hs::ArenaPool> pools;
    for (std::string const& cls : Hs::DeckClasses())
        pools.emplace(cls, Hs::ArenaPool::FromCatalog(*Catalog(), Db(), cls));

    std::map<std::string, ClassStats> stats;
    Totals totals;
    double draftMs = 0;
    int illegalDecks = 0;
    Hs::PackRng const pick = Hs::MakeSeededRng(seed);
    Clock::time_point const t0 = Clock::now();
    for (int g = 0; g < games; ++g)
    {
        // The first class cycles through the nine so each is covered; the second is a random class.
        std::string const cls0 = Hs::DeckClasses()[static_cast<std::size_t>(g) % Hs::DeckClasses().size()];
        std::string const cls1 = Hs::ArenaRandomClass(pick);
        uint64_t const draftSeed0 = seed * 1000003u + static_cast<uint64_t>(g) * 2u;
        uint64_t const draftSeed1 = draftSeed0 + 1u;

        Clock::time_point const d0 = Clock::now();
        std::vector<std::string> const deck0 =
            Hs::ArenaDraftDeck(cls0, pools.at(cls0), weights, *Catalog(), Db(), Hs::MakeSeededRng(draftSeed0));
        std::vector<std::string> const deck1 =
            Hs::ArenaDraftDeck(cls1, pools.at(cls1), weights, *Catalog(), Db(), Hs::MakeSeededRng(draftSeed1));
        draftMs += std::chrono::duration<double, std::milli>(Clock::now() - d0).count();
        if (Hs::ValidateArenaDeck(*Catalog(), Db(), cls0, deck0) != Hs::ArenaDeckWhy::None ||
            Hs::ValidateArenaDeck(*Catalog(), Db(), cls1, deck1) != Hs::ArenaDeckWhy::None)
        {
            ++illegalDecks;
            continue;
        }
        CountDeck(stats[cls0], deck0);
        CountDeck(stats[cls1], deck1);

        int turns = 0;
        uint32_t const matchSeed = static_cast<uint32_t>(seed * 7u + static_cast<uint64_t>(g) * 31337u + 5u);
        int const winner = PlayGame(deck0, cls0, deck1, cls1, matchSeed, g % 2, totals, turns);
        if (winner == -2)
            continue;
        ClassStats& s0 = stats[cls0];
        ClassStats& s1 = stats[cls1];
        ++s0.games;
        ++s1.games;
        s0.turns += turns;
        s1.turns += turns;
        if (winner == 0)
        {
            ++s0.wins;
            ++s1.losses;
        }
        else if (winner == 1)
        {
            ++s1.wins;
            ++s0.losses;
        }
        else
        {
            ++s0.draws;
            ++s1.draws;
        }
    }
    double const totalMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    std::printf("arena_sim: %d Hard vs Hard games, seed %llu\n", games, static_cast<unsigned long long>(seed));
    std::printf("%-8s %6s %5s %5s %5s %8s %9s %8s %8s %8s\n", "class", "games", "win", "draw", "loss", "win %",
        "avg cost", "<=3 mana", "minions", "avg turn");
    ClassStats all;
    for (std::string const& cls : Hs::DeckClasses())
    {
        ClassStats const& s = stats[cls];
        double const decks = std::max(1, s.decks);
        double const g = std::max(1, s.games);
        std::printf("%-8s %6d %5d %5d %5d %7.1f%% %9.2f %8.1f %8.1f %8.1f\n", cls.c_str(), s.games, s.wins, s.draws,
            s.losses, 100.0 * (s.wins + 0.5 * s.draws) / g, s.deckCost / (decks * Hs::kArenaDraftPicks),
            s.lowCards / decks, s.minions / decks, s.turns / g);
        all.games += s.games;
        all.turns += s.turns;
        all.deckCost += s.deckCost;
        all.decks += s.decks;
        all.lowCards += s.lowCards;
        all.minions += s.minions;
    }
    {
        double const decks = std::max(1, all.decks);
        double const g = std::max(1, all.games);
        std::printf("%-8s %6d %5s %5s %5s %8s %9.2f %8.1f %8.1f %8.1f\n", "all", all.games / 2, "", "", "", "",
            all.deckCost / (decks * Hs::kArenaDraftPicks), all.lowCards / decks, all.minions / decks, all.turns / g);
    }
    std::printf("refused or illegal AI actions: %d, games not finished: %d, games not created: %d, illegal decks: %d\n",
        totals.refusals, totals.notOver, totals.createFailed, illegalDecks);
    std::printf("most actions in one turn: %d\n", totals.maxActions);
    std::printf("runtime: %.1f s total, %.1f ms per game, drafts %.2f ms per deck, Hard decision %.1f ms avg / %.0f ms max "
                "(%ld calls)\n",
        totalMs / 1000.0, totalMs / games, draftMs / (2.0 * games), totals.hardCalls ? totals.hardMs / totals.hardCalls : 0.0,
        totals.hardMaxMs, totals.hardCalls);
    return totals.refusals == 0 && totals.notOver == 0 && totals.createFailed == 0 && illegalDecks == 0 ? 0 : 1;
}
