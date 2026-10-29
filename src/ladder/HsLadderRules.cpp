/*
 * World NPC Ladder: the ladder rules, the roster assignment and the NPC rounds. See
 * HsLadderRules.h.
 */

#include "HsLadderRules.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <set>
#include <thread>

namespace Hs
{
namespace Ladder
{
    char const* const kClasses[9] = {"DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR"};

    MixSlot const kMix[4] = {
        {Match::AiProfile::Normal, DeckRating::Easy},
        {Match::AiProfile::Hard, DeckRating::Hard},
        {Match::AiProfile::Normal, DeckRating::Hard},
        {Match::AiProfile::Hard, DeckRating::Easy},
    };

    namespace
    {
        // ---- small helpers ------------------------------------------------------------------------

        // A running FNV-1a 64 over numbers and strings (StateHash).
        struct Fnv
        {
            uint64_t h = 0xCBF29CE484222325ull;
            void Byte(unsigned char c)
            {
                h ^= c;
                h *= 0x100000001B3ull;
            }
            void U(uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                    Byte(static_cast<unsigned char>(v >> (8 * i)));
            }
            void S(std::string const& s)
            {
                U(s.size());
                for (unsigned char c : s)
                    Byte(c);
            }
        };

        void SortUnique(std::vector<EntrantId>& ids)
        {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }

        void UpdateBest(Entrant& e)
        {
            if (BetterPosition(e.Position(), Stars{e.bestRank, e.bestStars}))
            {
                e.bestRank = e.rank;
                e.bestStars = e.stars;
            }
        }

        Result ResultOfSide(SimOutcome outcome, int side)
        {
            if (outcome == SimOutcome::Draw)
                return Result::Draw;
            return (outcome == SimOutcome::Win0) == (side == 0) ? Result::Win : Result::Loss;
        }

        Result Opposite(Result r)
        {
            return r == Result::Win ? Result::Loss : r == Result::Loss ? Result::Win : Result::Draw;
        }

        void CountResult(Entrant& e, Result r)
        {
            ++e.games;
            if (r == Result::Win)
                ++e.wins;
            else if (r == Result::Loss)
                ++e.losses;
            else
                ++e.draws;
        }

        // The stars (and, for a player, the streak) after one result. Returns true when the streak bonus star was
        // earned. NPCs never get the bonus and never keep a streak.
        bool ApplyStarResult(Entrant& e, Result r)
        {
            bool const player = e.IsPlayer();
            Stars const before = e.Position();
            Stars after = before;
            bool bonus = false;
            if (r == Result::Win)
            {
                after = StarUp(before);
                if (player)
                {
                    ++e.winStreak;
                    // The rank BEFORE the win decides, so the star that promotes still carries the bonus.
                    if (e.winStreak >= kStreakBonusFrom && before.rank > kStreakBonusAbove)
                    {
                        after = StarUp(after);
                        bonus = true;
                    }
                }
            }
            else
            {
                if (r == Result::Loss)
                    after = StarDown(before);
                if (player)
                    e.winStreak = 0; // a loss or a draw ends the run
            }
            e.rank = after.rank;
            e.stars = after.stars;
            return bonus;
        }

        // Both entrants of a decided or drawn game: Elo, zero-sum.
        int32_t ApplyElo(Entrant& a, Entrant& b, Result resultOfA)
        {
            int32_t const delta = EloDeltaA(a.rating, b.rating, resultOfA);
            a.rating += delta;
            b.rating -= delta;
            return delta;
        }

        bool Repeat(Entrant const* x, Entrant const* y)
        {
            return x->lastOpponent == y->id || y->lastOpponent == x->id;
        }

        // v holds an even number of entrants, paired (0,1) (2,3) ...: swaps a repeating pair's second member with
        // the nearest entrant of another pair when that leaves both new pairs without a repeat (later pairs first,
        // then earlier ones). A pair that cannot be fixed stays.
        void AvoidRepeats(std::vector<Entrant const*>& v)
        {
            for (std::size_t p = 0; p + 1 < v.size(); p += 2)
            {
                if (!Repeat(v[p], v[p + 1]))
                    continue;
                auto trySwap = [&](std::size_t q) {
                    std::swap(v[p + 1], v[q]);
                    if (!Repeat(v[p], v[p + 1]) && !Repeat(v[q], v[q ^ 1]))
                        return true;
                    std::swap(v[p + 1], v[q]);
                    return false;
                };
                bool done = false;
                for (std::size_t q = p + 2; q < v.size() && !done; ++q)
                    done = trySwap(q);
                for (std::size_t q = p; q > 0 && !done; --q)
                    done = trySwap(q - 1);
            }
        }

        std::size_t ClassIndex(std::string const& cls)
        {
            for (std::size_t i = 0; i < 9; ++i)
                if (cls == kClasses[i])
                    return i;
            return 9;
        }

        LibraryDeck const* FindDeck(std::vector<LibraryDeck> const& library, std::string const& key)
        {
            for (LibraryDeck const& d : library)
                if (d.key == key)
                    return &d;
            return nullptr;
        }

        // Gives a new NPC its class, AI profile and deck. False when the library has no deck at all.
        bool AssignNew(LadderState const& state, std::vector<LibraryDeck> const& library, Entrant& npc)
        {
            // Classes that have at least one deck, and how many enabled NPCs each already has.
            bool hasDeck[9][2] = {};
            for (LibraryDeck const& d : library)
            {
                std::size_t const c = ClassIndex(d.heroClass);
                if (c < 9)
                    hasDeck[c][d.rating == DeckRating::Hard ? 1 : 0] = true;
            }
            int count[9] = {};
            for (Entrant const& e : state.entrants)
                if (!e.IsPlayer() && e.enabled)
                {
                    std::size_t const c = ClassIndex(e.heroClass);
                    if (c < 9)
                        ++count[c];
                }
            int least = -1;
            for (std::size_t c = 0; c < 9; ++c)
                if ((hasDeck[c][0] || hasDeck[c][1]) && (least < 0 || count[c] < least))
                    least = count[c];
            if (least < 0)
                return false;
            std::vector<std::size_t> candidates;
            for (std::size_t c = 0; c < 9; ++c)
                if ((hasDeck[c][0] || hasDeck[c][1]) && count[c] == least)
                    candidates.push_back(c);
            std::size_t const cls = candidates[Mix(Mix(state.seed, 0xC1A55u), static_cast<uint32_t>(npc.id)) % candidates.size()];

            // The least used (AI profile, deck rating) slot of the class, in rotation order on a tie.
            int usage[4] = {};
            for (Entrant const& e : state.entrants)
            {
                if (e.IsPlayer() || ClassIndex(e.heroClass) != cls)
                    continue;
                LibraryDeck const* d = FindDeck(library, e.deckKey);
                if (!d)
                    continue;
                for (int m = 0; m < 4; ++m)
                    if (kMix[m].profile == e.profile && kMix[m].rating == d->rating)
                        ++usage[m];
            }
            int slot = -1;
            for (int m = 0; m < 4; ++m)
                if (hasDeck[cls][kMix[m].rating == DeckRating::Hard ? 1 : 0] && (slot < 0 || usage[m] < usage[slot]))
                    slot = m;

            // The least used deck of that class and rating (the library is in key order).
            LibraryDeck const* best = nullptr;
            int bestUse = 0;
            for (LibraryDeck const& d : library)
            {
                if (ClassIndex(d.heroClass) != cls || d.rating != kMix[slot].rating)
                    continue;
                int use = 0;
                for (Entrant const& e : state.entrants)
                    if (!e.IsPlayer() && e.deckKey == d.key)
                        ++use;
                if (!best || use < bestUse)
                {
                    best = &d;
                    bestUse = use;
                }
            }
            npc.heroClass = kClasses[cls];
            npc.profile = kMix[slot].profile;
            npc.deckKey = best->key;
            return true;
        }
    }

    // ---- enums -------------------------------------------------------------------------------------------

    char const* ResultCode(Result result)
    {
        switch (result)
        {
            case Result::Win: return "win";
            case Result::Draw: return "draw";
            case Result::Loss: return "loss";
        }
        return "loss";
    }

    char const* FactionCode(Faction faction)
    {
        switch (faction)
        {
            case Faction::Alliance: return "alliance";
            case Faction::Horde: return "horde";
            case Faction::Any: return "any";
        }
        return "any";
    }

    bool ParseFaction(std::string const& code, Faction& out)
    {
        if (code == "alliance")
            out = Faction::Alliance;
        else if (code == "horde")
            out = Faction::Horde;
        else if (code == "any")
            out = Faction::Any;
        else
            return false;
        return true;
    }

    char const* DeckRatingCode(DeckRating rating)
    {
        return rating == DeckRating::Hard ? "hard" : "easy";
    }

    // ---- seeds -------------------------------------------------------------------------------------------

    uint32_t Mix(uint32_t a, uint32_t b)
    {
        // The "lowbias32" finalizer over a and the scrambled b (not commutative).
        uint32_t x = a ^ (b * 0x9E3779B9u + 0x7F4A7C15u);
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    uint32_t RoundSeed(uint32_t ladderSeed, uint32_t round) { return Mix(ladderSeed, round); }
    uint32_t GameSeed(uint32_t roundSeed, uint32_t slot) { return Mix(roundSeed, slot); }

    // ---- stars -------------------------------------------------------------------------------------------

    int MaxStars(int rank)
    {
        if (rank <= kLegend)
            return 0;
        // the original ladder: 2 stars for 25..21, 3 for 20..16, 4 for 15..10, 5 for 9..1
        if (rank >= 21)
            return 2;
        if (rank >= 16)
            return 3;
        return rank >= 10 ? 4 : 5;
    }

    bool operator==(Stars const& a, Stars const& b)
    {
        return a.rank == b.rank && a.stars == b.stars;
    }

    Stars StarUp(Stars s)
    {
        if (s.rank == kLegend)
            return s;
        if (s.stars < MaxStars(s.rank))
            return Stars{s.rank, s.stars + 1};
        if (s.rank == kTopRank)
            return Stars{kLegend, 0};
        return Stars{s.rank - 1, 1};
    }

    Stars StarDown(Stars s)
    {
        if (s.rank == kLegend)
            return s; // a loss in Legend costs only hidden rating
        if (s.stars > 0)
            return Stars{s.rank, s.stars - 1};
        if (s.rank == kBottomRank)
            return s;
        return Stars{s.rank + 1, MaxStars(s.rank + 1) - 1};
    }

    bool BetterPosition(Stars a, Stars b)
    {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.stars > b.stars;
    }

    // ---- hidden rating -----------------------------------------------------------------------------------

    int32_t EloDeltaA(int32_t ratingA, int32_t ratingB, Result resultOfA)
    {
        // Ratings are centi-points: the 400 of the formula is 40000 here.
        double const diff = static_cast<double>(ratingB) - static_cast<double>(ratingA);
        double const expected = 1.0 / (1.0 + std::pow(10.0, diff / 40000.0));
        double const score = resultOfA == Result::Win ? 1.0 : resultOfA == Result::Draw ? 0.5 : 0.0;
        // llround: half away from zero. K 32 = 3200 centi-points.
        return static_cast<int32_t>(std::llround(kEloK * 100.0 * (score - expected)));
    }

    // ---- state -------------------------------------------------------------------------------------------

    Entrant* LadderState::Find(EntrantId id)
    {
        for (Entrant& e : entrants)
            if (e.id == id)
                return &e;
        return nullptr;
    }

    Entrant const* LadderState::Find(EntrantId id) const
    {
        for (Entrant const& e : entrants)
            if (e.id == id)
                return &e;
        return nullptr;
    }

    VsRecord const* LadderState::FindVs(EntrantId player, EntrantId npc) const
    {
        for (VsRecord const& v : vs)
            if (v.player == player && v.npc == npc)
                return &v;
        return nullptr;
    }

    uint64_t StateHash(LadderState const& state)
    {
        Fnv f;
        f.U(state.seed);
        f.U(state.roundsPlayed);
        f.U(state.started ? 1 : 0);
        std::vector<Entrant const*> sorted;
        for (Entrant const& e : state.entrants)
            sorted.push_back(&e);
        std::sort(sorted.begin(), sorted.end(), [](Entrant const* a, Entrant const* b) { return a->id < b->id; });
        f.U(sorted.size());
        for (Entrant const* e : sorted)
        {
            f.U(e->id);
            f.U(static_cast<uint64_t>(e->faction));
            f.S(e->heroClass);
            f.S(e->deckKey);
            f.U(static_cast<uint64_t>(e->profile));
            f.U(e->enabled ? 1 : 0);
            f.U(e->joinedRound);
            f.U(static_cast<uint64_t>(e->rank));
            f.U(static_cast<uint64_t>(e->stars));
            f.U(static_cast<uint64_t>(static_cast<int64_t>(e->rating)));
            f.U(e->games);
            f.U(e->wins);
            f.U(e->losses);
            f.U(e->draws);
            f.U(static_cast<uint64_t>(e->winStreak));
            f.U(static_cast<uint64_t>(e->bestRank));
            f.U(static_cast<uint64_t>(e->bestStars));
            f.U(e->byes);
            f.U(e->lastOpponent);
        }
        std::vector<VsRecord> vs = state.vs;
        std::sort(vs.begin(), vs.end(), [](VsRecord const& a, VsRecord const& b) {
            return a.player != b.player ? a.player < b.player : a.npc < b.npc;
        });
        f.U(vs.size());
        for (VsRecord const& v : vs)
        {
            f.U(v.player);
            f.U(v.npc);
            f.U(v.wins);
            f.U(v.losses);
            f.U(v.draws);
        }
        return f.h;
    }

    bool LadderLess(Entrant const& a, Entrant const& b)
    {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.rank != kLegend && a.stars != b.stars)
            return a.stars > b.stars;
        if (a.rating != b.rating)
            return a.rating > b.rating;
        return a.id < b.id;
    }

    std::vector<Entrant const*> LadderOrder(LadderState const& state)
    {
        std::vector<Entrant const*> out;
        for (Entrant const& e : state.entrants)
            if (e.enabled)
                out.push_back(&e);
        std::sort(out.begin(), out.end(), [](Entrant const* a, Entrant const* b) { return LadderLess(*a, *b); });
        return out;
    }

    Entrant& EnsurePlayer(LadderState& state, uint32_t accountId, std::string const& name)
    {
        EntrantId const id = PlayerId(accountId);
        if (Entrant* e = state.Find(id))
            return *e;
        Entrant e;
        e.id = id;
        e.name = name;
        e.joinedRound = state.roundsPlayed;
        state.entrants.push_back(e);
        return state.entrants.back();
    }

    std::vector<EntrantId> EnforceLegendLimit(LadderState& state, LadderRules const& rules)
    {
        std::vector<Entrant*> legend;
        for (Entrant& e : state.entrants)
            if (e.enabled && e.rank == kLegend)
                legend.push_back(&e);
        std::vector<EntrantId> pushed;
        std::size_t const places = static_cast<std::size_t>(std::max(0, rules.legendSize));
        if (legend.size() <= places)
            return pushed;
        std::sort(legend.begin(), legend.end(), [](Entrant const* a, Entrant const* b) { return LadderLess(*a, *b); });
        for (std::size_t i = places; i < legend.size(); ++i)
        {
            legend[i]->rank = kTopRank;
            legend[i]->stars = kPushedOutStars;
            pushed.push_back(legend[i]->id);
        }
        return pushed;
    }

    // ---- roster ------------------------------------------------------------------------------------------

    RosterChange UpdateRoster(LadderState& state, std::vector<FinderName> const& finder,
                              std::vector<LibraryDeck> const& library, LadderRules const& rules)
    {
        RosterChange change;
        std::vector<FinderName> names = finder;
        std::stable_sort(names.begin(), names.end(), [](FinderName const& a, FinderName const& b) { return a.entry < b.entry; });
        names.erase(std::unique(names.begin(), names.end(),
            [](FinderName const& a, FinderName const& b) { return a.entry == b.entry; }), names.end());
        std::vector<LibraryDeck> lib = library;
        std::stable_sort(lib.begin(), lib.end(), [](LibraryDeck const& a, LibraryDeck const& b) { return a.key < b.key; });

        // The NPCs that already exist: refreshed name, disabled or enabled again.
        std::map<uint32_t, FinderName const*> byEntry;
        for (FinderName const& n : names)
            byEntry[n.entry] = &n;
        for (Entrant& e : state.entrants)
        {
            if (e.IsPlayer())
                continue;
            auto it = byEntry.find(static_cast<uint32_t>(e.id));
            if (it != byEntry.end())
            {
                e.name = it->second->name;
                if (!e.enabled)
                {
                    e.enabled = true;
                    change.enabled.push_back(e.id);
                }
            }
            else if (e.enabled)
            {
                e.enabled = false;
                change.disabled.push_back(e.id);
            }
        }
        SortUnique(change.enabled);
        SortUnique(change.disabled);

        // The new names, in entry order.
        for (FinderName const& n : names)
        {
            if (state.Find(NpcId(n.entry)))
                continue;
            Entrant npc;
            npc.id = NpcId(n.entry);
            npc.name = n.name;
            npc.faction = n.faction;
            npc.joinedRound = state.roundsPlayed;
            if (!AssignNew(state, lib, npc))
            {
                change.unassigned.push_back(n.entry);
                continue;
            }
            state.entrants.push_back(npc);
            change.added.push_back(npc.id);
        }

        if (!change.enabled.empty())
            change.pushedOut = EnforceLegendLimit(state, rules);
        return change;
    }

    // ---- ladder start ------------------------------------------------------------------------------------

    void PlaceNpcs(LadderState& state, LadderRules const& rules)
    {
        std::vector<Entrant*> npcs;
        for (Entrant& e : state.entrants)
            if (!e.IsPlayer() && e.enabled)
                npcs.push_back(&e);
        std::sort(npcs.begin(), npcs.end(), [](Entrant const* a, Entrant const* b) {
            return a->rating != b->rating ? a->rating > b->rating : a->id < b->id;
        });
        std::size_t const legend = std::min(npcs.size(), static_cast<std::size_t>(std::max(0, rules.legendSize)));
        std::size_t const rest = npcs.size() - legend;
        for (std::size_t i = 0; i < npcs.size(); ++i)
        {
            Entrant& e = *npcs[i];
            if (i < legend)
                e.rank = kLegend;
            else
                e.rank = kTopRank + static_cast<int>((i - legend) * static_cast<std::size_t>(kBottomRank) / rest);
            e.stars = 0;
            UpdateBest(e);
        }
        state.started = true;
        EnforceLegendLimit(state, rules); // only matters when a player already stands in Legend
    }

    // ---- rounds ------------------------------------------------------------------------------------------

    RoundPlan PlanRound(LadderState const& state, RoundOptions const& options)
    {
        RoundPlan plan;
        plan.round = state.roundsPlayed + 1;
        plan.kind = options.kind;
        plan.seed = RoundSeed(state.seed, plan.round);
        plan.excluded = options.excludedNpc;

        std::vector<Entrant const*> pool;
        for (Entrant const& e : state.entrants)
            if (!e.IsPlayer() && e.enabled && e.id != options.excludedNpc)
                pool.push_back(&e);

        // The rotating bye: fewest byes so far, then the smallest hash of the round seed and the id.
        if (pool.size() % 2 == 1)
        {
            auto key = [&](Entrant const* e) { return Mix(plan.seed, static_cast<uint32_t>(e->id)); };
            auto worse = [&](Entrant const* a, Entrant const* b) {
                if (a->byes != b->byes)
                    return a->byes < b->byes;
                if (key(a) != key(b))
                    return key(a) < key(b);
                return a->id < b->id;
            };
            std::vector<Entrant const*>::iterator pick = std::min_element(pool.begin(), pool.end(), worse);
            plan.bye = (*pick)->id;
            pool.erase(pick);
        }

        if (options.kind == RoundKind::RatingOnly)
        {
            std::sort(pool.begin(), pool.end(), [&](Entrant const* a, Entrant const* b) {
                if (a->rating != b->rating)
                    return a->rating > b->rating;
                uint32_t const ka = Mix(plan.seed, static_cast<uint32_t>(a->id));
                uint32_t const kb = Mix(plan.seed, static_cast<uint32_t>(b->id));
                if (ka != kb)
                    return ka < kb;
                return a->id < b->id;
            });
        }
        else
            std::sort(pool.begin(), pool.end(), [](Entrant const* a, Entrant const* b) { return LadderLess(*a, *b); });
        AvoidRepeats(pool);

        for (std::size_t i = 0; i + 1 < pool.size(); i += 2)
        {
            GamePlan g;
            g.slot = static_cast<uint32_t>(i / 2);
            g.a = pool[i]->id;
            g.b = pool[i + 1]->id;
            g.seed = GameSeed(plan.seed, g.slot);
            g.firstSide = -1;
            plan.games.push_back(g);
        }
        return plan;
    }

    GameResult ToGameResult(GamePlan const& plan, SimResult const& sim)
    {
        GameResult r;
        r.plan = plan;
        r.outcome = sim.outcome;
        r.firstSide = sim.firstSide;
        r.turns = sim.turns;
        r.error = sim.error;
        if (sim.outcome == SimOutcome::Failed && r.error.empty())
            r.error = "failed";
        return r;
    }

    SimRoster BuildSimRoster(LadderState const& state, std::vector<LibraryDeck> const& library)
    {
        SimRoster roster;
        for (Entrant const& e : state.entrants)
        {
            if (e.IsPlayer())
                continue;
            LibraryDeck const* d = FindDeck(library, e.deckKey);
            if (!d)
                continue;
            SimEntrant s;
            s.name = e.name;
            s.heroClass = e.heroClass;
            s.deck = d->cards;
            s.profile = e.profile;
            roster[e.id] = s;
        }
        return roster;
    }

    GameFn MakeSimGameFn(Match::CardDb const& db, SimRoster const& roster, SimOptions const& options)
    {
        return [&db, &roster, options](GamePlan const& g) {
            SimRoster::const_iterator a = roster.find(g.a);
            SimRoster::const_iterator b = roster.find(g.b);
            if (a == roster.end() || b == roster.end())
            {
                SimResult r;
                r.error = std::string("no deck for entrant ") + std::to_string(a == roster.end() ? g.a : g.b);
                return r;
            }
            return SimulateGame(db, a->second, b->second, g.seed, g.firstSide, options);
        };
    }

    std::vector<GameResult> PlayRound(RoundPlan const& plan, GameFn const& game, int threads)
    {
        std::vector<GameResult> results(plan.games.size());
        auto run = [&](std::size_t i) {
            GamePlan const& g = plan.games[i];
            try
            {
                results[i] = ToGameResult(g, game(g));
            }
            catch (std::exception const& ex)
            {
                results[i].plan = g;
                results[i].error = std::string("exception: ") + ex.what();
            }
            catch (...)
            {
                results[i].plan = g;
                results[i].error = "exception";
            }
        };
        if (threads <= 1 || plan.games.size() <= 1)
        {
            for (std::size_t i = 0; i < plan.games.size(); ++i)
                run(i);
            return results;
        }
        std::atomic<std::size_t> next{0};
        std::size_t const workers = std::min<std::size_t>(static_cast<std::size_t>(threads), plan.games.size());
        std::vector<std::thread> pool;
        for (std::size_t t = 0; t < workers; ++t)
            pool.emplace_back([&]() {
                for (std::size_t i = next++; i < plan.games.size(); i = next++)
                    run(i);
            });
        for (std::thread& t : pool)
            t.join();
        return results;
    }

    RoundReport ApplyRound(LadderState& state, RoundPlan const& plan, std::vector<GameResult> const& results,
                           LadderRules const& rules)
    {
        RoundReport report;
        report.round = plan.round;
        if (plan.round != state.roundsPlayed + 1)
        {
            report.error = "stale_round";
            return report;
        }
        bool matches = results.size() == plan.games.size();
        for (std::size_t i = 0; matches && i < results.size(); ++i)
            matches = results[i].plan.slot == plan.games[i].slot && results[i].plan.a == plan.games[i].a &&
                      results[i].plan.b == plan.games[i].b;
        if (!matches)
        {
            report.error = "results_do_not_match_plan";
            return report;
        }

        std::vector<EntrantId> touched;
        for (GameResult const& r : results) // slot order
        {
            Entrant* a = state.Find(r.plan.a);
            Entrant* b = state.Find(r.plan.b);
            if (!a || !b || a == b)
            {
                GameResult failed = r;
                failed.outcome = SimOutcome::Failed;
                failed.error = "unknown_entrant";
                report.failures.push_back(failed);
                continue;
            }
            if (r.outcome == SimOutcome::Failed)
            {
                report.failures.push_back(r);
                continue;
            }
            Result const ra = ResultOfSide(r.outcome, 0);
            ApplyElo(*a, *b, ra);
            CountResult(*a, ra);
            CountResult(*b, Opposite(ra));
            if (plan.kind == RoundKind::Ladder)
            {
                ApplyStarResult(*a, ra);
                ApplyStarResult(*b, Opposite(ra));
            }
            a->lastOpponent = b->id;
            b->lastOpponent = a->id;
            touched.push_back(a->id);
            touched.push_back(b->id);
            ++report.played;
        }
        if (plan.bye != 0)
            if (Entrant* e = state.Find(plan.bye))
            {
                ++e->byes;
                touched.push_back(e->id);
            }
        state.roundsPlayed = plan.round;

        if (plan.kind == RoundKind::Ladder)
        {
            report.pushedOut = EnforceLegendLimit(state, rules);
            touched.insert(touched.end(), report.pushedOut.begin(), report.pushedOut.end());
        }
        SortUnique(touched);
        for (EntrantId id : touched)
            if (Entrant* e = state.Find(id))
                UpdateBest(*e);
        report.touched = touched;
        report.ok = true;
        return report;
    }

    RoundReport RunRound(LadderState& state, GameFn const& game, int threads, RoundOptions const& options,
                         LadderRules const& rules)
    {
        RoundPlan const plan = PlanRound(state, options);
        std::vector<GameResult> const results = PlayRound(plan, game, threads);
        return ApplyRound(state, plan, results, rules);
    }

    int RunLadderStart(LadderState& state, GameFn const& game, int threads, LadderRules const& rules,
                       std::function<void(LadderState const&, RoundReport const&)> const& onRound)
    {
        if (state.started)
            return 0;
        int played = 0;
        while (state.roundsPlayed < static_cast<uint32_t>(std::max(0, rules.startRounds)))
        {
            RoundOptions options;
            options.kind = RoundKind::RatingOnly;
            RoundReport const report = RunRound(state, game, threads, options, rules);
            if (!report.ok)
                break;
            ++played;
            if (onRound)
                onRound(state, report);
        }
        PlaceNpcs(state, rules);
        return played;
    }

    // ---- player result -----------------------------------------------------------------------------------

    PlayerResultReport ApplyPlayerResult(LadderState& state, EntrantId player, EntrantId npc, Result playerResult,
                                         LadderRules const& rules)
    {
        PlayerResultReport report;
        Entrant* p = state.Find(player);
        Entrant* n = state.Find(npc);
        if (!p || !IsPlayerId(player))
        {
            report.error = "unknown_player";
            return report;
        }
        if (!n || IsPlayerId(npc))
        {
            report.error = "unknown_npc";
            return report;
        }
        report.before = p->Position();
        report.npcBefore = n->Position();
        Stars const bestBefore{p->bestRank, p->bestStars};

        report.ratingDelta = ApplyElo(*p, *n, playerResult);
        CountResult(*p, playerResult);
        CountResult(*n, Opposite(playerResult));
        report.streakBonus = ApplyStarResult(*p, playerResult);
        ApplyStarResult(*n, Opposite(playerResult));

        VsRecord* rec = nullptr;
        for (VsRecord& v : state.vs)
            if (v.player == player && v.npc == npc)
                rec = &v;
        if (!rec)
        {
            VsRecord fresh;
            fresh.player = player;
            fresh.npc = npc;
            state.vs.push_back(fresh);
            rec = &state.vs.back();
        }
        if (playerResult == Result::Win)
            ++rec->wins;
        else if (playerResult == Result::Loss)
            ++rec->losses;
        else
            ++rec->draws;

        // The Legend check may move entrants (the pointers stay valid: nothing was added or removed).
        report.pushedOut = EnforceLegendLimit(state, rules);
        UpdateBest(*p);
        UpdateBest(*n);
        for (EntrantId id : report.pushedOut)
            if (Entrant* e = state.Find(id))
                UpdateBest(*e);
        report.after = p->Position();
        report.npcAfter = n->Position();
        report.winStreak = p->winStreak;
        report.newBest = BetterPosition(Stars{p->bestRank, p->bestStars}, bestBefore);
        report.ok = true;
        return report;
    }
}
}
