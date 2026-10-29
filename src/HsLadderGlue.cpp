#include "Chat.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsEconomyGlue.h"
#include "HsHeroPortraitStore.h"
#include "HsDataset.h"
#include "HsLadderDuelStore.h"
#include "HsLadderGlue.h"
#include "HsLadderStore.h"
#include "HsLadderTable.h"
#include "HsLadderWorldStore.h"
#include "HsMatchGlue.h"
#include "HsMatchStore.h"
#include "ladder/HsLadderDuel.h"
#include "ladder/HsLadderPlay.h"
#include "ladder/HsLadderRows.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace HL = Hs::Ladder;

namespace
{
    constexpr uint32_t kRowsPerPage = 20;
    constexpr uint32_t kMaxGmRounds = 50;     // .hs ladder round [n]
    constexpr std::size_t kMaxQueue = 500;    // queued rounds and results
    constexpr int kRetryMs = 30000;           // pause after a failed save before the round is planned again
    constexpr int kMaxRoundRetries = 3;       // a round that cannot be played or applied is tried again this often, then the ladder stops
    constexpr uint32_t kLastGames = 5;        // games shown by .hs ladder npc
    constexpr uint32_t kMaxVsLines = 20;      // records shown by .hs ladder player
    constexpr float kWalkCityRange = 250.0f;  // .hs ladder walk: a table this close to the GM is "the GM's city"

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool AllDigits(std::string const& s)
    {
        return !s.empty() && s.size() <= 10 &&
            std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    uint32_t NewSeed()
    {
        std::random_device device;
        uint32_t const seed = static_cast<uint32_t>(device()) ^ static_cast<uint32_t>(std::time(nullptr));
        return seed ? seed : 1u;
    }

    char const* FactionWord(HL::Faction faction)
    {
        return faction == HL::Faction::Alliance ? "Alliance" : faction == HL::Faction::Horde ? "Horde" : "any faction";
    }

    std::string Record(HL::Entrant const& e)
    {
        return std::to_string(e.wins) + "-" + std::to_string(e.losses) + "-" + std::to_string(e.draws);
    }

    // ---- the worker -----------------------------------------------------------------------------------------

    // One background thread that plays one whole round at a time. It reads only what the job carries (the plan and
    // shared_ptr snapshots of the card database and the NPC decks, which therefore stay alive until the round is
    // played whatever the glue does meanwhile) and hands the results back through a one-slot box. Stop() makes the
    // games that have not started fail at once and joins: a stopped round is marked cancelled and dropped.
    class Worker
    {
    public:
        struct Job
        {
            HL::RoundPlan plan;
            std::shared_ptr<Hs::Match::CardDb const> db;
            std::shared_ptr<HL::SimRoster const> roster;
            int threads = 1;
        };
        struct Done
        {
            HL::RoundPlan plan;
            std::vector<HL::GameResult> results;
            std::string error;     // the round could not be played at all
            uint32_t ms = 0;
            bool cancelled = false;
        };

        ~Worker() { Stop(); }

        void Start()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_thread.joinable() && !m_quit)
                m_thread = std::thread([this]() { Run(); });
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_quit = true;
                m_cancel.store(true, std::memory_order_relaxed);
            }
            m_cv.notify_all();
            if (m_thread.joinable())
                m_thread.join();
        }

        // False when a job is waiting, running or not yet taken.
        bool Submit(Job job)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_quit || !m_thread.joinable() || m_hasJob || m_playing || m_hasDone)
                return false;
            m_job = std::move(job);
            m_hasJob = true;
            m_cv.notify_all();
            return true;
        }

        bool Take(Done& out)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_hasDone)
                return false;
            out = std::move(m_done);
            m_done = Done();
            m_hasDone = false;
            return true;
        }

        bool Busy() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_hasJob || m_playing || m_hasDone;
        }

    private:
        void Run()
        {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this]() { return m_quit || m_hasJob; });
                    if (m_quit)
                        return;
                    job = std::move(m_job);
                    m_job = Job();
                    m_hasJob = false;
                    m_playing = true;
                }

                Done done;
                done.plan = job.plan;
                auto const begin = std::chrono::steady_clock::now();
                try
                {
                    HL::GameFn const real = HL::MakeSimGameFn(*job.db, *job.roster);
                    HL::GameFn const guarded = [this, &real](HL::GamePlan const& game) {
                        if (m_cancel.load(std::memory_order_relaxed))
                        {
                            HL::SimResult stopped;
                            stopped.error = "stopped";
                            return stopped;
                        }
                        return real(game);
                    };
                    done.results = HL::PlayRound(job.plan, guarded, job.threads);
                }
                catch (std::exception const& ex)
                {
                    done.error = ex.what();
                }
                catch (...)
                {
                    done.error = "unknown exception";
                }
                done.ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begin).count());
                done.cancelled = m_cancel.load(std::memory_order_relaxed);

                std::lock_guard<std::mutex> lock(m_mutex);
                m_done = std::move(done);
                m_hasDone = true;
                m_playing = false;
            }
        }

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::thread m_thread;
        Job m_job;
        Done m_done;
        bool m_quit = false;
        bool m_hasJob = false;
        bool m_playing = false;
        bool m_hasDone = false;
        std::atomic<bool> m_cancel{false};
    };

    // ---- the ladder -------------------------------------------------------------------------------------------

    // Work waiting for the worker, in order.
    struct Work
    {
        enum class Kind { Round, PlayerResult } kind = Kind::Round;
        HL::EntrantId excluded = 0;     // Round: the NPC that sits out
        uint32_t account = 0;           // PlayerResult
        std::string name;
        HL::EntrantId npc = 0;
        HL::Result result = HL::Result::Win;
        uint64_t duelId = 0;            // PlayerResult: the ladder duel it settles (deleted in the same transaction), 0 for none
    };

    struct PlayerOutcome
    {
        bool ok = false;
        std::string why;
        HL::PlayerResultReport report;
        HL::Entrant playerBefore, playerAfter, npcBefore, npcAfter;
        std::vector<std::string> milestones;
    };

    // The queue as hearthstone_ladder_queue stores it (migration 0019), in order, and back.
    std::vector<HL::QueueRow> QueueRowsOf(std::deque<Work> const& queue)
    {
        std::vector<HL::QueueRow> rows;
        for (Work const& item : queue)
        {
            HL::QueueRow row;
            if (item.kind == Work::Kind::Round)
                row.excludedNpc = item.excluded;
            else
            {
                row.kind = HL::kQueueResult;
                row.account = item.account;
                row.playerName = item.name;
                row.npcId = item.npc;
                row.result = static_cast<uint8_t>(item.result);
                row.duelId = item.duelId;
            }
            rows.push_back(row);
        }
        return rows;
    }

    Work WorkOf(HL::QueueRow const& row)
    {
        Work item;
        if (row.kind == HL::kQueueRound)
        {
            item.excluded = row.excludedNpc;
            return item;
        }
        item.kind = Work::Kind::PlayerResult;
        item.account = row.account;
        item.name = row.playerName;
        item.npc = row.npcId;
        item.result = static_cast<HL::Result>(row.result);
        item.duelId = row.duelId;
        return item;
    }

    // Everything below runs under g_mutex (the world thread; the worker is the only other thread and shares nothing
    // but its job and result).
    struct Runtime
    {
        bool enabled = false;      // Hearthstone.Ladder.Enable
        bool ready = false;        // loaded and usable
        bool stopping = false;
        bool stalled = false;      // a round could not be applied or played at all: nothing more runs (see the log)
        int roundFailures = 0;     // failed attempts of the current round in a row (kMaxRoundRetries)
        bool tooFew = false;       // fewer than 2 enabled NPCs and no start yet: the ladder start waits
        int retryMs = 0;
        HL::LadderRules rules;
        HL::LadderState state;
        std::vector<HL::LibraryDeck> library;
        std::shared_ptr<Hs::Match::CardDb const> db;
        std::shared_ptr<HL::SimRoster const> roster;
        Worker worker;
        // Every item here is also stored (hearthstone_ladder_queue): a change of the queue is written with the change it
        // describes, so the queue survives a stop. The item being played is not in the queue but still stored until its
        // save deletes it.
        std::deque<Work> queue;
        // The chairs. The world data (tables, lines, blocked NPCs) is read once at startup; the offers of an
        // account are read when it first sits down and kept (they are saved at every change).
        HL::LadderWorld world;
        bool worldReady = false;   // at least one table works
        bool offerTables = false;  // character migration 0016 is applied
        std::map<uint32_t, HL::OfferSet> offers;
        HL::RoundOptions flight;   // the options of the round being played
        // The ladder start of this run (for the log).
        bool startTimed = false;
        std::chrono::steady_clock::time_point startBegin;
        uint32_t startRoundsRun = 0, startGames = 0, startFailed = 0;
        uint32_t startThreads = 0;

        bool Busy() const
        {
            return worker.Busy() || !queue.empty() || retryMs > 0 || (!state.started && !tooFew && !stalled);
        }
    };

    std::mutex g_mutex;
    Runtime& R()
    {
        static Runtime runtime;
        return runtime;
    }
    std::atomic<bool> g_enabled{false};
    std::atomic<bool> g_ready{false};

    std::string NameOf(HL::LadderState const& state, HL::EntrantId id)
    {
        HL::Entrant const* e = state.Find(id);
        if (!e)
            return "#" + std::to_string(id);
        if (!e->name.empty())
            return e->name;
        return e->IsPlayer() ? "account #" + std::to_string(id - HL::kPlayerIdBase) : "creature " + std::to_string(id);
    }

    uint32_t NpcCount(HL::LadderState const& state)
    {
        uint32_t n = 0;
        for (HL::Entrant const& e : state.entrants)
            if (!e.IsPlayer() && e.enabled)
                ++n;
        return n;
    }

    uint32_t PlayerCount(HL::LadderState const& state)
    {
        uint32_t n = 0;
        for (HL::Entrant const& e : state.entrants)
            if (e.IsPlayer())
                ++n;
        return n;
    }

    // The library decks the ladder can use: kind library, ready, with a finder rating (normal and ladder_easy = easy,
    // hard and ladder_hard = hard). The ladder_ values are decks for the ladder only, not in the match-finder pools.
    std::vector<HL::LibraryDeck> LadderLibrary(Hs::DeckLibrary const& library)
    {
        std::vector<HL::LibraryDeck> out;
        for (Hs::LibraryDeck const& deck : library.Decks())
        {
            if (deck.kind != Hs::kDeckKindLibrary || !deck.ready)
                continue;
            HL::LibraryDeck d;
            if (deck.finder == "normal" || deck.finder == "ladder_easy")
                d.rating = HL::DeckRating::Easy;
            else if (deck.finder == "hard" || deck.finder == "ladder_hard")
                d.rating = HL::DeckRating::Hard;
            else
                continue;
            d.key = deck.key;
            d.heroClass = deck.deckClass;
            d.cards = deck.ids;
            out.push_back(std::move(d));
        }
        return out;
    }

    // ---- applying and saving ------------------------------------------------------------------------

    void LogFailures(HL::RoundReport const& report, HL::LadderState const& state)
    {
        std::size_t shown = 0;
        for (HL::GameResult const& g : report.failures)
        {
            if (++shown > 3)
                break;
            sLog.outError("Hearthstone: ladder round %u game %u (%s vs %s) failed: %s", report.round, g.plan.slot,
                NameOf(state, g.plan.a).c_str(), NameOf(state, g.plan.b).c_str(), g.error.c_str());
        }
        if (report.failures.size() > 3)
            sLog.outError("Hearthstone: ladder round %u: %u more failed game(s)", report.round,
                static_cast<uint32_t>(report.failures.size() - 3));
    }

    // A round that could not be played or applied: planned again after a pause, at most kMaxRoundRetries times in a
    // row; then the ladder stops until .hs ladder resume (or a restart).
    void RetryOrStall(Runtime& rt, HL::RoundPlan const& plan, char const* what, std::string const& error)
    {
        if (++rt.roundFailures > kMaxRoundRetries)
        {
            sLog.outError("Hearthstone: ladder round %u %s (%s) %d times in a row; the ladder stops (.hs ladder resume)",
                plan.round, what, error.c_str(), rt.roundFailures);
            rt.stalled = true;
            return;
        }
        sLog.outError("Hearthstone: ladder round %u %s (%s); it is tried again in %d s (attempt %d of %d)", plan.round,
            what, error.c_str(), kRetryMs / 1000, rt.roundFailures, kMaxRoundRetries);
        rt.retryMs = kRetryMs;
        if (plan.kind == HL::RoundKind::Ladder)
        {
            Work again;
            again.excluded = rt.flight.excludedNpc;
            rt.queue.push_front(again);
        }
    }

    // Applies a played round to the state and saves it in one transaction. A failed save puts the state back (the
    // round is planned again after a pause). True when the round was applied and saved.
    bool ApplyAndSave(Runtime& rt, HL::RoundPlan const& plan, std::vector<HL::GameResult> const& results, uint32_t ms)
    {
        HL::LadderState backup = rt.state;
        HL::RoundReport const report = HL::ApplyRound(rt.state, plan, results, rt.rules);
        if (!report.ok)
        {
            rt.state = std::move(backup);
            RetryOrStall(rt, plan, "was not applied", report.error);
            return false;
        }
        // The queue that is left (the round being saved is not in it) is stored in the transaction of the round, so
        // the round is gone from the database exactly when its results are.
        std::vector<std::string> const extra = LadderQueueStatements(QueueRowsOf(rt.queue));
        if (!HsLadderStore().SaveRound(rt.state, plan, results, report, extra))
        {
            sLog.outError("Hearthstone: ladder round %u could not be saved; it is played again in %d s", plan.round,
                kRetryMs / 1000);
            rt.state = std::move(backup);
            rt.retryMs = kRetryMs;
            if (plan.kind == HL::RoundKind::Ladder)
            {
                Work again;
                again.excluded = rt.flight.excludedNpc;
                rt.queue.push_front(again);
            }
            return false;
        }
        rt.roundFailures = 0;
        LogFailures(report, rt.state);
        if (plan.kind == HL::RoundKind::RatingOnly)
        {
            ++rt.startRoundsRun;
            rt.startGames += static_cast<uint32_t>(report.played + report.failures.size());
            rt.startFailed += static_cast<uint32_t>(report.failures.size());
            if (plan.round % 5 == 0 && plan.round < rt.rules.startRounds)
                sLog.outString("Hearthstone: ladder start: round %u of %u done (%u ms)", plan.round, rt.rules.startRounds, ms);
        }
        return true;
    }

    // Plays nothing when the plan has no games (fewer than two NPCs), else hands the round to the worker. True when
    // something was applied and the caller may go on at once.
    bool Launch(Runtime& rt, HL::RoundPlan const& plan, HL::RoundOptions const& options)
    {
        rt.flight = options;
        if (plan.kind == HL::RoundKind::RatingOnly && !rt.startTimed)
        {
            rt.startTimed = true;
            rt.startBegin = std::chrono::steady_clock::now();
        }
        if (plan.games.empty())
            return ApplyAndSave(rt, plan, std::vector<HL::GameResult>(), 0);
        Worker::Job job;
        job.plan = plan;
        job.db = rt.db;
        job.roster = rt.roster;
        rt.startThreads = sHearthstoneConfig.GetLadder().threads;
        job.threads = static_cast<int>(rt.startThreads);
        if (!rt.worker.Submit(std::move(job)))
            RetryOrStall(rt, plan, "was not taken by the worker", "busy");
        return false;
    }

    // The end of the ladder start: places the NPCs on the star ladder and saves.
    bool FinishStart(Runtime& rt)
    {
        HL::LadderState backup = rt.state;
        HL::PlaceNpcs(rt.state, rt.rules);
        if (!HsLadderStore().SavePlacement(rt.state))
        {
            sLog.outError("Hearthstone: the ladder placement could not be saved; it is tried again in %d s", kRetryMs / 1000);
            rt.state = std::move(backup);
            rt.retryMs = kRetryMs;
            return false;
        }
        uint32_t legend = 0;
        for (HL::Entrant const& e : rt.state.entrants)
            if (!e.IsPlayer() && e.enabled && e.rank == HL::kLegend)
                ++legend;
        double const seconds = rt.startTimed
            ? std::chrono::duration<double>(std::chrono::steady_clock::now() - rt.startBegin).count() : 0.0;
        sLog.outString("Hearthstone: ladder start finished: %u NPC(s) placed (%u in Legend), %u round(s) played this "
            "run (%u in all), %u game(s), %u failed, %.1f s with %u thread(s)", NpcCount(rt.state), legend,
            rt.startRoundsRun, rt.state.roundsPlayed, rt.startGames, rt.startFailed, seconds, rt.startThreads);
        rt.startTimed = false;
        rt.startRoundsRun = rt.startGames = rt.startFailed = 0;
        return true;
    }

    // Applies one player result to the state, saves it and queues the following NPC round at the front. The caller
    // made sure the worker is idle.
    PlayerOutcome ApplyPlayer(Runtime& rt, Work const& item)
    {
        PlayerOutcome out;
        HL::LadderState backup = rt.state;
        HL::Entrant const* npc = rt.state.Find(item.npc);
        if (!npc || HL::IsPlayerId(item.npc))
        {
            out.why = "unknown_npc";
            return out;
        }
        HL::Entrant& player = HL::EnsurePlayer(rt.state, item.account, item.name);
        if (!item.name.empty())
            player.name = item.name;
        HL::EntrantId const playerId = player.id;
        out.playerBefore = player;
        out.npcBefore = *rt.state.Find(item.npc);
        out.report = HL::ApplyPlayerResult(rt.state, playerId, item.npc, item.result, rt.rules);
        if (!out.report.ok)
        {
            out.why = out.report.error;
            rt.state = std::move(backup);
            return out;
        }
        // The result, the deletion of its duel and the NPC round that follows (first in the queue) are ONE transaction:
        // a result is applied exactly once, and a stop before the round is played does not lose the round.
        Work round;
        round.excluded = item.npc;
        std::deque<Work> after = rt.queue;
        after.push_front(round);
        std::vector<std::string> extra = HL::SettleSql(item.duelId);
        std::vector<std::string> const queue = LadderQueueStatements(QueueRowsOf(after));
        extra.insert(extra.end(), queue.begin(), queue.end());
        if (!HsLadderStore().SavePlayerResult(rt.state, playerId, item.npc, out.report.pushedOut, extra))
        {
            out.why = "storage";
            rt.state = std::move(backup);
            return out;
        }
        if (item.duelId != 0)
            HsLadderDuels().Settled(item.account);
        out.playerAfter = *rt.state.Find(playerId);
        out.npcAfter = *rt.state.Find(item.npc);
        out.ok = true;
        // The one-time milestones (no reward yet): written once, told to the player the first time.
        {
            std::vector<HL::Entrant const*> const order = HL::LadderOrder(rt.state);
            bool const first = !order.empty() && order.front()->id == playerId;
            std::vector<std::string> const wanted = HL::MilestonesFor(out.playerAfter.rank, first);
            // Hero portrait reward: one credit per league for the number one of Legend. Keyed by (account, league)
            // with INSERT IGNORE, so it is repeated on every result while the player stays number one: a failed write
            // heals itself, and the milestone above stays a once-only record. League 0 (no league yet) pays nothing.
            if (first && out.playerAfter.rank == HL::kLegend && HsHeroGrantCredit(item.account, HsHeroCurrentLeague()))
                HsPushHeroChanged(item.account, "legend1");
            if (!wanted.empty())
            {
                std::vector<std::string> reached;
                if (HsLadderStore().LoadMilestones(item.account, reached))
                    for (std::string const& name : wanted)
                        if (std::find(reached.begin(), reached.end(), name) == reached.end() &&
                            HsLadderStore().SaveMilestone(item.account, name))
                        {
                            out.milestones.push_back(name);
                            sLog.outString("Hearthstone: ladder milestone '%s' reached by account %u", name.c_str(), item.account);
                        }
            }
        }
        rt.queue.push_front(round);
        return out;
    }

    // Plans and starts the next piece of work. True when something was applied and the caller may ask again.
    bool StartNext(Runtime& rt)
    {
        if (!rt.state.started)
        {
            if (rt.tooFew)
                return false;
            if (rt.state.roundsPlayed < rt.rules.startRounds)
            {
                HL::RoundOptions options;
                options.kind = HL::RoundKind::RatingOnly;
                return Launch(rt, HL::PlanRound(rt.state, options), options);
            }
            return FinishStart(rt);
        }
        if (rt.queue.empty())
            return false;
        Work const item = rt.queue.front();
        rt.queue.pop_front();
        if (item.kind == Work::Kind::Round)
        {
            HL::RoundOptions options;
            options.excludedNpc = item.excluded;
            return Launch(rt, HL::PlanRound(rt.state, options), options);
        }
        PlayerOutcome const out = ApplyPlayer(rt, item);
        if (out.ok)
            sLog.outString("Hearthstone: ladder: queued %s of account %u against %s applied: %s -> %s",
                HL::ResultCode(item.result), item.account, NameOf(rt.state, item.npc).c_str(),
                HL::RankText(out.report.before.rank, out.report.before.stars).c_str(),
                HL::RankText(out.report.after.rank, out.report.after.stars).c_str());
        else
            sLog.outError("Hearthstone: ladder: queued %s of account %u against creature %llu was dropped (%s)",
                HL::ResultCode(item.result), item.account, static_cast<unsigned long long>(item.npc), out.why.c_str());
        return true;
    }

    // Takes a finished round and starts work while the worker is idle.
    void Pump(Runtime& rt)
    {
        if (!rt.ready || rt.stopping)
            return;
        Worker::Done done;
        if (rt.worker.Take(done))
        {
            if (done.cancelled)
                return;
            if (!done.error.empty())
                RetryOrStall(rt, done.plan, "could not be played", done.error);
            else
                ApplyAndSave(rt, done.plan, done.results, done.ms);
        }
        while (!rt.stalled && rt.retryMs <= 0 && !rt.worker.Busy() && StartNext(rt))
        {
        }
    }

    // A player result against an NPC (the caller holds g_mutex): applied now, or queued while the ladder is busy.
    HsLadderResultReply SubmitPlayer(Runtime& rt, uint32_t accountId, std::string const& playerName, uint32_t npcEntry,
                                     HL::Result result, uint64_t duelId = 0)
    {
        HsLadderResultReply reply;
        if (!rt.enabled)
        {
            reply.why = "disabled";
            return reply;
        }
        if (!rt.ready)
        {
            reply.why = "unavailable";
            return reply;
        }
        if (rt.stalled)
        {
            reply.why = "stalled";
            return reply;
        }
        if (!rt.state.started)
        {
            reply.why = "updating";
            return reply;
        }
        HL::Entrant const* npc = rt.state.Find(HL::NpcId(npcEntry));
        if (!npc || npc->IsPlayer())
        {
            reply.why = "unknown_npc";
            return reply;
        }
        Work item;
        item.kind = Work::Kind::PlayerResult;
        item.account = accountId;
        item.name = playerName;
        item.npc = npc->id;
        item.result = result;
        item.duelId = duelId;
        if (rt.worker.Busy() || !rt.queue.empty() || rt.retryMs > 0)
        {
            // The state must not change under a running round: the result waits its turn. It is stored first, so a stop
            // after the reply does not lose it; when it cannot be stored the reply says so and nothing is queued.
            if (rt.queue.size() >= kMaxQueue)
            {
                reply.why = "full";
                return reply;
            }
            std::deque<Work> next = rt.queue;
            next.push_back(item);
            if (!HsLadderStore().Run(LadderQueueStatements(QueueRowsOf(next)), "ladder queue"))
            {
                reply.why = "storage";
                return reply;
            }
            rt.queue = std::move(next);
            reply.ok = true;
            reply.queued = true;
            return reply;
        }
        PlayerOutcome const out = ApplyPlayer(rt, item);
        reply.why = out.why;
        if (!out.ok)
            return reply;
        reply.ok = true;
        reply.report = out.report;
        reply.playerBefore = out.playerBefore;
        reply.playerAfter = out.playerAfter;
        reply.npcBefore = out.npcBefore;
        reply.npcAfter = out.npcAfter;
        reply.milestones = out.milestones;
        Pump(rt); // starts the NPC round that follows
        return reply;
    }

    // ---- loading ----------------------------------------------------------------------------------------

    // Reads the cards, decks, finder names and the stored ladder (or makes a new one when fresh), updates and saves the
    // roster, builds the NPC snapshot and lets the work begin. False (logged) leaves the ladder unavailable.
    bool Boot(Runtime& rt, bool fresh)
    {
        rt.ready = false;
        g_ready.store(false, std::memory_order_relaxed);
        rt.stalled = false;
        rt.roundFailures = 0;
        rt.tooFew = false;
        rt.retryMs = 0;
        rt.queue.clear();
        if (!CheckHsLadderTables())
        {
            sLog.outError("Hearthstone: the NPC ladder is unavailable (character migration 0015_hearthstone_ladder.sql "
                "not applied)");
            return false;
        }
        std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
        std::shared_ptr<Hs::DeckLibrary const> const library = HsMatchLibrary();
        if (!db || !library)
        {
            sLog.outError("Hearthstone: the NPC ladder is unavailable (the card data or the deck library is not loaded)");
            return false;
        }
        std::vector<HL::LibraryDeck> ladderDecks = LadderLibrary(*library);
        std::vector<Hs::FinderName> names;
        if (!LoadHsFinderNames(names))
        {
            sLog.outError("Hearthstone: the NPC ladder is unavailable (no hearthstone_finder_npc table)");
            return false;
        }
        std::vector<HL::FinderName> finder;
        for (Hs::FinderName const& n : names)
        {
            HL::FinderName f;
            f.entry = n.entry;
            f.name = n.name;
            if (!HL::ParseFaction(n.faction, f.faction))
            {
                sLog.outError("Hearthstone: ladder: finder entry %u has the unknown faction '%s'; it is left out",
                    n.entry, n.faction.c_str());
                continue;
            }
            finder.push_back(f);
        }

        HL::LadderState state;
        bool created = false;
        if (!fresh)
        {
            bool found = false;
            uint32_t rulesVersion = 0;
            if (!HsLadderStore().Load(found, state, rulesVersion))
            {
                sLog.outError("Hearthstone: the NPC ladder is unavailable (the stored ladder could not be read)");
                return false;
            }
            if (found && rulesVersion != static_cast<uint32_t>(Hs::Match::kRulesVersion))
                sLog.outString("Hearthstone: ladder: it was last saved with match rules version %u, now %d; played rounds "
                    "stay as they are, a round that is played again may differ", rulesVersion, Hs::Match::kRulesVersion);
            created = !found;
        }
        else
            created = true;
        if (created)
        {
            // A new ladder starts from empty tables (a reset deletes them too, this clears leftovers of a hand edit).
            if (!HsLadderStore().Reset())
            {
                sLog.outError("Hearthstone: the NPC ladder is unavailable (the old ladder rows could not be cleared)");
                return false;
            }
            // The duels and the waiting rounds of an old ladder belong to nobody now.
            if (HsLadderDuels().Ready() && !HsLadderDuels().Reset())
            {
                sLog.outError("Hearthstone: the NPC ladder is unavailable (the old ladder duels could not be cleared)");
                return false;
            }
            state = HL::LadderState();
            state.seed = NewSeed();
            // A new ladder is a new league: Legend #1 pays a hero portrait credit again, in this league. The decks of the NPCs
            // come from the new seed (UpdateRoster below), and the start rounds are played again.
            if (!HsHeroStartLeague())
                sLog.outError("Hearthstone: ladder: the league counter could not be advanced (character migration "
                    "0022_hearthstone_ladder_league.sql applied?); the ladder starts anyway");
        }

        std::map<HL::EntrantId, std::string> namesBefore;
        for (HL::Entrant const& e : state.entrants)
            namesBefore[e.id] = e.name;
        HL::RosterChange const change = HL::UpdateRoster(state, finder, ladderDecks, rt.rules);
        std::vector<HL::EntrantId> changed;
        changed.insert(changed.end(), change.added.begin(), change.added.end());
        changed.insert(changed.end(), change.enabled.begin(), change.enabled.end());
        changed.insert(changed.end(), change.disabled.begin(), change.disabled.end());
        changed.insert(changed.end(), change.pushedOut.begin(), change.pushedOut.end());
        for (HL::Entrant const& e : state.entrants)
        {
            auto const before = namesBefore.find(e.id);
            if (before != namesBefore.end() && before->second != e.name)
                changed.push_back(e.id);
        }
        if ((created || !changed.empty()) && !HsLadderStore().SaveRoster(state, changed))
        {
            sLog.outError("Hearthstone: the NPC ladder is unavailable (the roster could not be saved)");
            return false;
        }

        rt.state = std::move(state);
        rt.library = std::move(ladderDecks);
        rt.db = db;
        rt.roster = std::make_shared<HL::SimRoster const>(HL::BuildSimRoster(rt.state, rt.library));
        rt.tooFew = !rt.state.started && NpcCount(rt.state) < 2;
        rt.startTimed = false;
        rt.startRoundsRun = rt.startGames = rt.startFailed = 0;
        rt.worker.Start();
        // The queued work that was not applied or played before the last stop comes first, in its order.
        if (!created && HsLadderDuels().Ready())
        {
            std::vector<HL::QueueRow> waiting;
            if (HsLadderDuels().LoadQueue(waiting))
            {
                for (HL::QueueRow const& row : waiting)
                    rt.queue.push_back(WorkOf(row));
                if (!waiting.empty())
                    sLog.outString("Hearthstone: ladder: %u queued item(s) that were waiting when the server stopped are played now",
                        static_cast<uint32_t>(waiting.size()));
            }
            else
                sLog.outError("Hearthstone: ladder: the queued work could not be read; it stays stored");
        }
        rt.ready = true;
        g_ready.store(true, std::memory_order_relaxed);

        std::string unassigned;
        for (std::size_t i = 0; i < change.unassigned.size() && i < 10; ++i)
            unassigned += (i ? ", " : "") + std::to_string(change.unassigned[i]);
        std::string what;
        if (rt.state.started)
            what = "ladder ready";
        else if (rt.tooFew)
            what = "the ladder start waits for at least 2 NPCs";
        else
        {
            uint32_t const total = static_cast<uint32_t>(std::max(0, rt.rules.startRounds));
            what = "ladder start running in the background: " + std::to_string(total - std::min(total, rt.state.roundsPlayed)) +
                " of " + std::to_string(total) + " round(s) to play with " +
                std::to_string(sHearthstoneConfig.GetLadder().threads) + " thread(s)";
        }
        sLog.outString("Hearthstone: NPC ladder %s: %u NPC(s) (%u added, %u disabled, %u enabled again, %u without a deck%s%s), "
            "%u player(s), %u round(s) played, %s", created ? "created" : "loaded", NpcCount(rt.state),
            static_cast<uint32_t>(change.added.size()), static_cast<uint32_t>(change.disabled.size()),
            static_cast<uint32_t>(change.enabled.size()), static_cast<uint32_t>(change.unassigned.size()),
            unassigned.empty() ? "" : ": ", unassigned.c_str(), PlayerCount(rt.state), rt.state.roundsPlayed, what.c_str());
        Pump(rt);
        return true;
    }

    // ---- GM helpers ---------------------------------------------------------------------------------------

    char const* StateWord(Runtime const& rt)
    {
        if (!rt.enabled)
            return "disabled";
        if (!rt.ready)
            return "unavailable";
        if (rt.stalled)
            return "stalled";
        if (rt.tooFew)
            return "waiting for NPCs";
        return rt.Busy() ? "updating" : "ready";
    }

    // False (told) when the ladder cannot be used.
    bool UsableOrTell(Runtime const& rt, ChatHandler* handler)
    {
        if (!rt.enabled)
        {
            handler->SendSysMessage("Hearthstone: the ladder is disabled (Hearthstone.Ladder.Enable = 0).");
            return false;
        }
        if (!rt.ready)
        {
            handler->SendSysMessage("Hearthstone: the ladder is unavailable (migration 0015_hearthstone_ladder.sql "
                "applied? card data and deck library loaded? see the server log).");
            return false;
        }
        return true;
    }

    // The NPCs a GM text names: an entry number, an exact name, or part of a name (case ignored).
    std::vector<HL::Entrant const*> FindNpcs(HL::LadderState const& state, std::string const& text)
    {
        std::vector<HL::Entrant const*> found;
        if (AllDigits(text))
        {
            if (HL::Entrant const* e = state.Find(HL::NpcId(static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 10)))))
                found.push_back(e);
            return found;
        }
        std::string const needle = Lower(text);
        for (HL::Entrant const& e : state.entrants)
        {
            if (e.IsPlayer())
                continue;
            std::string const name = Lower(e.name);
            if (name == needle)
                return std::vector<HL::Entrant const*>(1, &e);
            if (name.find(needle) != std::string::npos)
                found.push_back(&e);
        }
        return found;
    }

    // args[from..to) joined with spaces, without the quotes a GM may put around a name.
    std::string JoinWords(std::vector<std::string> const& args, std::size_t from, std::size_t to)
    {
        std::string text;
        for (std::size_t i = from; i < to && i < args.size(); ++i)
            text += (i > from ? " " : "") + args[i];
        text.erase(std::remove(text.begin(), text.end(), '"'), text.end());
        return text;
    }

    // One NPC for a GM text, or tells why not.
    HL::Entrant const* OneNpcOrTell(HL::LadderState const& state, ChatHandler* handler, std::string const& text)
    {
        std::vector<HL::Entrant const*> const found = FindNpcs(state, text);
        if (found.empty())
        {
            handler->SendSysMessage("Hearthstone: no ladder NPC matches '" + text + "'.");
            return nullptr;
        }
        if (found.size() > 1)
        {
            std::string list;
            for (std::size_t i = 0; i < found.size() && i < 8; ++i)
                list += (i ? ", " : "") + found[i]->name + " (" + std::to_string(found[i]->id) + ")";
            handler->SendSysMessage("Hearthstone: " + std::to_string(found.size()) + " NPCs match '" + text + "': " + list +
                (found.size() > 8 ? ", ..." : "") + ". Use the entry number or more of the name.");
            return nullptr;
        }
        return found.front();
    }

    // The 1-based position of an enabled entrant in the ladder order (0 for a disabled one).
    uint32_t PositionOf(std::vector<HL::Entrant const*> const& order, HL::EntrantId id)
    {
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i]->id == id)
                return static_cast<uint32_t>(i + 1);
        return 0;
    }

    // The account of a GM command: the named character or #account, else the GM's own. name = the character's name.
    bool ResolveAccountOrTell(ChatHandler* handler, std::vector<std::string> const& args, std::size_t index,
                              uint32_t& accountId, std::string& label, std::string& name)
    {
        if (args.size() > index)
        {
            if (!HsResolveAccountTarget(handler, args[index], accountId, label))
                return false;
            std::size_t const cut = label.find(" (account #");
            name = cut != std::string::npos ? label.substr(0, cut) : std::string();
            return true;
        }
        WorldSession* session = handler->GetSession();
        if (!session)
        {
            handler->SendSysMessage("Hearthstone: name a character (or #accountId) from the console.");
            return false;
        }
        accountId = session->GetAccountId();
        label = "your account";
        char const* own = session->GetPlayerName();
        name = own ? own : "";
        return true;
    }

    void Show(Runtime& rt, ChatHandler* handler, std::vector<std::string> const& args)
    {
        std::vector<HL::Entrant const*> const order = HL::LadderOrder(rt.state);
        uint32_t const total = static_cast<uint32_t>(order.size());
        uint32_t const pages = std::max<uint32_t>(1, (total + kRowsPerPage - 1) / kRowsPerPage);
        uint32_t page = 1;
        if (args.size() > 2)
        {
            if (!AllDigits(args[2]) || (page = static_cast<uint32_t>(std::strtoul(args[2].c_str(), nullptr, 10))) < 1 ||
                page > pages)
            {
                handler->SendSysMessage("Hearthstone: the page is 1 to " + std::to_string(pages) + ".");
                return;
            }
        }
        handler->SendSysMessage(std::string("Hearthstone: ladder ") + StateWord(rt) + ", " + std::to_string(rt.state.roundsPlayed) +
            " round(s) played, " + std::to_string(NpcCount(rt.state)) + " NPC(s), " + std::to_string(PlayerCount(rt.state)) +
            " player(s), page " + std::to_string(page) + " of " + std::to_string(pages) + ".");
        if (!rt.state.started && !rt.tooFew)
            handler->SendSysMessage("  The ladder start is not finished: the order follows the hidden ratings so far.");
        for (uint32_t i = (page - 1) * kRowsPerPage; i < total && i < page * kRowsPerPage; ++i)
        {
            HL::Entrant const& e = *order[i];
            handler->SendSysMessage(std::to_string(i + 1) + ". " + NameOf(rt.state, e.id) + (e.IsPlayer() ? " (player)" : "") +
                " - " + HL::RankText(e.rank, e.stars) + " - " + (e.IsPlayer() ? std::string("-") : HL::ClassText(e.heroClass)) +
                " - " + (e.IsPlayer() ? "-" : FactionWord(e.faction)) + " - " + Record(e) + " - " + HL::RatingText(e.rating));
        }
    }

    void ShowNpc(Runtime& rt, ChatHandler* handler, std::vector<std::string> const& args)
    {
        if (args.size() < 3)
        {
            HsLadderUsage(handler);
            return;
        }
        HL::Entrant const* npc = OneNpcOrTell(rt.state, handler, JoinWords(args, 2, args.size()));
        if (!npc)
            return;
        std::vector<HL::Entrant const*> const order = HL::LadderOrder(rt.state);
        uint32_t const position = PositionOf(order, npc->id);
        handler->SendSysMessage("Hearthstone: " + npc->name + " (entry " + std::to_string(npc->id) + "): " +
            HL::RankText(npc->rank, npc->stars) + ", hidden rating " + HL::RatingText(npc->rating) + ", " +
            (npc->enabled ? "position " + std::to_string(position) + " of " + std::to_string(order.size())
                          : std::string("left the roster")));
        handler->SendSysMessage("  " + HL::ClassText(npc->heroClass) + ", " + FactionWord(npc->faction) + ", deck " + npc->deckKey +
            ", AI " + Hs::Match::AiProfileCode(npc->profile) + ", joined at round " + std::to_string(npc->joinedRound));
        handler->SendSysMessage("  Record " + Record(*npc) + " (" + std::to_string(npc->games) + " games), " +
            std::to_string(npc->byes) + " bye(s), last opponent " +
            (npc->lastOpponent ? NameOf(rt.state, npc->lastOpponent) : std::string("none")));
        std::vector<HL::GameRow> games;
        if (!HsLadderStore().LoadGames(npc->id, kLastGames, games))
        {
            handler->SendSysMessage("  The game log could not be read (see the server log).");
            return;
        }
        if (games.empty())
        {
            handler->SendSysMessage("  No logged games.");
            return;
        }
        for (HL::GameRow const& g : games)
        {
            bool const first = g.a == npc->id;
            HL::EntrantId const other = first ? g.b : g.a;
            std::string verdict;
            if (g.outcome == "failed")
                verdict = "failed (" + g.error + ")";
            else
            {
                verdict = g.outcome == "draw" ? "draw" : (g.outcome == (first ? "win0" : "win1") ? "won" : "lost");
                verdict += g.firstSide < 0 ? std::string() : std::string(", ") + ((g.firstSide == 0) == first ? "went first" : "went second") +
                    ", " + std::to_string(g.turns) + " turns";
            }
            handler->SendSysMessage("  round " + std::to_string(g.round) + (g.ratingOnly ? " (start)" : "") + " vs " +
                NameOf(rt.state, other) + ": " + verdict);
        }
    }

    void ShowPlayer(Runtime& rt, ChatHandler* handler, std::vector<std::string> const& args)
    {
        uint32_t accountId = 0;
        std::string label, name;
        if (!ResolveAccountOrTell(handler, args, 2, accountId, label, name))
            return;
        HL::Entrant const* p = rt.state.Find(HL::PlayerId(accountId));
        if (!p)
        {
            handler->SendSysMessage("Hearthstone: " + label + " has no ladder entry yet (no result so far).");
            return;
        }
        std::vector<HL::Entrant const*> const order = HL::LadderOrder(rt.state);
        handler->SendSysMessage("Hearthstone: " + label + " (account #" + std::to_string(accountId) + ", " +
            NameOf(rt.state, p->id) + "): " + HL::RankText(p->rank, p->stars) + ", position " +
            std::to_string(PositionOf(order, p->id)) + " of " + std::to_string(order.size()) + ", win streak " +
            std::to_string(p->winStreak) + ", hidden rating " + HL::RatingText(p->rating));
        handler->SendSysMessage("  Best " + HL::RankText(p->bestRank, p->bestStars) + ", " + std::to_string(p->games) +
            " game(s), " + Record(*p));
        std::vector<std::string> milestones;
        if (HsLadderStore().LoadMilestones(accountId, milestones) && !milestones.empty())
        {
            std::string text;
            for (std::string const& m : milestones)
                text += (text.empty() ? "" : ", ") + m;
            handler->SendSysMessage("  Milestones: " + text);
        }
        std::vector<HL::VsRecord const*> records;
        for (HL::VsRecord const& v : rt.state.vs)
            if (v.player == p->id)
                records.push_back(&v);
        std::sort(records.begin(), records.end(), [](HL::VsRecord const* a, HL::VsRecord const* b) {
            uint32_t const ga = a->wins + a->losses + a->draws, gb = b->wins + b->losses + b->draws;
            return ga != gb ? ga > gb : a->npc < b->npc;
        });
        for (std::size_t i = 0; i < records.size() && i < kMaxVsLines; ++i)
            handler->SendSysMessage("  vs " + NameOf(rt.state, records[i]->npc) + ": " + std::to_string(records[i]->wins) + "-" +
                std::to_string(records[i]->losses) + "-" + std::to_string(records[i]->draws));
        if (records.size() > kMaxVsLines)
            handler->SendSysMessage("  ... and " + std::to_string(records.size() - kMaxVsLines) + " more.");
    }

    // ---- the ladder duels (GM) ------------------------------------------------------------------------

    // The result of that duel waits in the queue (the caller holds g_mutex).
    bool DuelQueued(Runtime const& rt, uint64_t duelId)
    {
        for (Work const& item : rt.queue)
            if (item.kind == Work::Kind::PlayerResult && item.duelId == duelId)
                return true;
        return false;
    }

    // .hs ladder duel [character|#accountId]  /  .hs ladder duel abandon <character|#accountId> confirm
    void DuelCommand(Runtime& rt, ChatHandler* handler, std::vector<std::string> const& args, bool abandon)
    {
        if (!HsLadderDuels().Ready())
        {
            handler->SendSysMessage("Hearthstone: ranked ladder duels are not set up (character migration 0017, see the server log).");
            return;
        }
        uint32_t accountId = 0;
        std::string label, name;
        if (!ResolveAccountOrTell(handler, args, abandon ? 3 : 2, accountId, label, name))
            return;
        bool found = false;
        HL::DuelRow row;
        if (!HsLadderDuels().Find(accountId, found, row))
        {
            handler->SendSysMessage("Hearthstone: the duel of " + label + " could not be read (see the server log).");
            return;
        }
        if (!found)
        {
            handler->SendSysMessage("Hearthstone: " + label + " has no ladder duel.");
            return;
        }
        uint32_t actions = 0;
        HsLadderDuels().CountLog(row.id, actions);
        if (!abandon)
        {
            handler->SendSysMessage("Hearthstone: " + label + " (account #" + std::to_string(accountId) + "): " +
                HL::DuelText(row, actions) + ", duel " + std::to_string(row.id) + ", character guid " + std::to_string(row.guid) +
                ", deck " + row.deckName + ", AI " + row.profile);
            handler->SendSysMessage(std::string("  ") + (DuelQueued(rt, row.id) ? "Its result waits in the ladder queue. " : "") +
                (HL::DuelReplayable(row, static_cast<uint32_t>(Hs::Match::kRulesVersion), Hs::kDatasetHash)
                     ? "It can be resumed at a ladder chair."
                     : "It was recorded with another rules version or card dataset: it cannot be resumed."));
            return;
        }
        if (DuelQueued(rt, row.id))
        {
            handler->SendSysMessage("Hearthstone: its result is queued; abandon it when the ladder is idle.");
            return;
        }
        if (row.state != HL::kDuelPaused && HsMatches().IsRunning(row.guid))
        {
            handler->SendSysMessage("Hearthstone: the duel is being played right now; end it first (.hs match end <character>).");
            return;
        }
        sLog.outString("Hearthstone: GM %s abandons ladder duel %llu of %s (account %u): no result, the offers stay",
            HsGmName(handler).c_str(), static_cast<unsigned long long>(row.id), label.c_str(), accountId);
        if (!HsLadderDuels().Abandon(row.id))
        {
            handler->SendSysMessage("Hearthstone: the duel could not be removed (see the server log).");
            return;
        }
        handler->SendSysMessage("Hearthstone: the ladder duel of " + label + " was removed without a result; its offers stay.");
    }

    // ---- the chairs ------------------------------------------------------------------------------------

    // The reason the chairs cannot be used (written for the player), "" when they can.
    std::string ChairWhy(Runtime const& rt)
    {
        if (!rt.enabled)
            return "The ladder is switched off on this server.";
        if (!rt.ready || rt.stalled)
            return "The ladder is not available right now (see the server log).";
        if (!rt.offerTables || !rt.worldReady)
            return "The ladder chairs are not set up on this server (see the server log).";
        if (!HsLadderDuels().Ready())
            return "Ranked ladder duels are not set up on this server (see the server log).";
        if (!rt.state.started)
            return "The league is still being set up (the ladder start is running). Try again in a minute.";
        return std::string();
    }

    // The account's offer set (read when it is first needed). nullptr: the stored rows could not be read.
    HL::OfferSet* OfferSetOf(Runtime& rt, uint32_t accountId)
    {
        auto const cached = rt.offers.find(accountId);
        if (cached != rt.offers.end())
            return &cached->second;
        bool stored = false;
        HL::OfferSet set;
        if (!HsLadderStore().LoadOffers(accountId, stored, set))
            return nullptr;
        if (!stored)
        {
            set = HL::OfferSet();
            set.account = accountId;
        }
        return &(rt.offers[accountId] = set);
    }

    HsLadderStanding StandingOf(Runtime const& rt, uint32_t accountId)
    {
        HsLadderStanding standing;
        if (HL::Entrant const* p = rt.state.Find(HL::PlayerId(accountId)))
        {
            standing.onLadder = true;
            standing.rank = p->rank;
            standing.stars = p->stars;
            standing.wins = p->wins;
            standing.losses = p->losses;
            standing.draws = p->draws;
            if (p->rank == HL::kLegend)
                standing.legendPosition = PositionOf(HL::LadderOrder(rt.state), p->id);
        }
        return standing;
    }

    bool OfferViewOf(Runtime const& rt, uint32_t accountId, HL::EntrantId npcId, HsLadderOfferView& out)
    {
        HL::Entrant const* npc = rt.state.Find(npcId);
        if (!npc || npc->IsPlayer())
            return false;
        out = HsLadderOfferView();
        out.entry = static_cast<uint32_t>(npc->id);
        out.name = NameOf(rt.state, npc->id);
        out.heroClass = npc->heroClass;
        out.faction = npc->faction;
        out.rank = npc->rank;
        out.stars = npc->stars;
        if (HL::VsRecord const* vs = rt.state.FindVs(HL::PlayerId(accountId), npc->id))
        {
            out.wins = vs->wins;
            out.losses = vs->losses;
            out.draws = vs->draws;
        }
        return true;
    }

    // Draws (or keeps) the offers of an account and saves a change. False: the stored rows could not be read.
    bool EnsureOffersOf(Runtime& rt, uint32_t accountId, HL::OfferSet*& set)
    {
        set = OfferSetOf(rt, accountId);
        if (!set)
            return false;
        if (HL::EnsureOffers(*set, rt.state, accountId, rt.world.blocked) && !HsLadderStore().SaveOffers(*set))
            sLog.outError("Hearthstone: ladder: the offers of account %u could not be saved (they are kept in memory; "
                "the same draw follows after a restart)", accountId);
        return true;
    }

    // The table whose city is `text` (case ignored; an exact name, else a part of a name that only one city has).
    bool CityTable(Runtime const& rt, std::string const& text, HL::LadderTable& out)
    {
        std::string const needle = Lower(text);
        if (needle.empty())
            return false;
        HL::LadderTable const* part = nullptr;
        int parts = 0;
        for (HL::LadderTable const& t : rt.world.tables)
        {
            std::string const city = Lower(t.city);
            if (city == needle)
            {
                out = t;
                return true;
            }
            if (city.find(needle) != std::string::npos)
            {
                part = &t;
                ++parts;
            }
        }
        if (parts != 1)
            return false;
        out = *part;
        return true;
    }

    // `.hs ladder offers [reroll] [character|#accountId]`: the stored offers of an account, or new ones.
    void OffersCommand(Runtime& rt, ChatHandler* handler, std::vector<std::string> const& args, bool reroll)
    {
        uint32_t accountId = 0;
        std::string label, name;
        if (!ResolveAccountOrTell(handler, args, reroll ? 3 : 2, accountId, label, name))
            return;
        if (!rt.offerTables || !rt.worldReady)
        {
            handler->SendSysMessage("Hearthstone: the ladder chairs are not set up (character migration "
                "0016_hearthstone_ladder_offer.sql and world migration 0049_hearthstone_ladder_tables.sql applied? see the "
                "server log).");
            return;
        }
        if (!rt.state.started)
        {
            handler->SendSysMessage("Hearthstone: the ladder start is not finished; offers work after it.");
            return;
        }
        HL::OfferSet* set = OfferSetOf(rt, accountId);
        if (!set)
        {
            handler->SendSysMessage("Hearthstone: the stored offers of " + label + " could not be read (see the server log).");
            return;
        }
        if (reroll)
        {
            HL::RedrawOffers(*set, rt.state, accountId, rt.world.blocked);
            bool const saved = HsLadderStore().SaveOffers(*set);
            sLog.outString("Hearthstone: GM %s redrew the ladder offers of account %u%s", HsGmName(handler).c_str(), accountId,
                saved ? "" : " (not saved)");
            handler->SendSysMessage("Hearthstone: new offers drawn for " + label + (saved ? "." : " (they could not be saved: "
                "see the server log)."));
        }
        std::string text = "Hearthstone: offers of " + label + ": drawn at round " + std::to_string(set->round) + " (the ladder is at round " +
            std::to_string(rt.state.roundsPlayed) + "), draw #" + std::to_string(set->drawCounter) + ", last opponent " +
            (set->lastOpponent ? NameOf(rt.state, set->lastOpponent) : std::string("none"));
        handler->SendSysMessage(text);
        if (set->slots.empty())
        {
            handler->SendSysMessage("  Nothing stored: three are drawn when the player next sits on a ladder chair.");
            return;
        }
        if (!HL::OfferSetValid(*set, rt.state, rt.world.blocked))
            handler->SendSysMessage("  This set is not valid any more (an NPC left the roster or is blocked): it is redrawn "
                "at the next sit-down.");
        for (std::size_t i = 0; i < set->slots.size(); ++i)
        {
            HsLadderOfferView v;
            if (!OfferViewOf(rt, accountId, set->slots[i], v))
            {
                handler->SendSysMessage("  " + std::to_string(i + 1) + ". #" + std::to_string(set->slots[i]) + " (unknown NPC)");
                continue;
            }
            handler->SendSysMessage("  " + std::to_string(i + 1) + ". " + HL::OfferLineText(v.name, v.heroClass, v.faction, v.rank, v.stars,
                v.wins, v.losses, v.draws, 200) + " (entry " + std::to_string(v.entry) + ")");
        }
    }

    // The table named by the GM (a part of the city name), else the table in the GM's city: the nearest table on the
    // GM's map. False (told) when there is none.
    bool TableForWalk(ChatHandler* handler, std::string const& cityText, HL::LadderTable& out)
    {
        if (!cityText.empty())
        {
            if (HsLadderTableByCity(cityText, out))
                return true;
            std::string cities;
            for (HL::LadderTable const& t : HsLadderTables())
                cities += (cities.empty() ? "" : ", ") + t.city;
            handler->SendSysMessage("Hearthstone: no single ladder table matches the city '" + cityText + "'. Tables: " +
                (cities.empty() ? std::string("none") : cities) + ".");
            return false;
        }
        WorldSession* session = handler->GetSession();
        Player* gm = session ? session->GetPlayer() : nullptr;
        if (!gm || !gm->IsInWorld())
        {
            handler->SendSysMessage("Hearthstone: .hs ladder walk needs a character in the world (the NPC is summoned next to it).");
            return false;
        }
        float best = 0;
        bool found = false;
        for (HL::LadderTable const& t : HsLadderTables())
        {
            if (t.mapId != gm->GetMapId())
                continue;
            float const d = gm->GetDistance3dToCenter(t.spawn.x, t.spawn.y, t.spawn.z);
            if (!found || d < best)
            {
                best = d;
                out = t;
                found = true;
            }
        }
        if (!found || best > kWalkCityRange)
        {
            handler->SendSysMessage("Hearthstone: you are not in a ladder city (no table within " + std::to_string(static_cast<int>(kWalkCityRange)) +
                " yards on this map). Name the city: .hs ladder walk <npc> <city>.");
            return false;
        }
        return true;
    }

    // `.hs ladder walk <npc entry or name> [city]` (the caller checked security; the ladder mutex is NOT held).
    void WalkCommand(ChatHandler* handler, std::vector<std::string> const& args)
    {
        if (args.size() < 3)
        {
            HsLadderUsage(handler);
            return;
        }
        // The NPC is the words up to a split; the rest, when it names a table's city, is the city.
        HL::Entrant npc;
        std::string cityText;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            Runtime& rt = R();
            if (!UsableOrTell(rt, handler))
                return;
            if (!rt.worldReady)
            {
                handler->SendSysMessage("Hearthstone: no ladder table is set up (world migration "
                    "0049_hearthstone_ladder_tables.sql applied? see the server log).");
                return;
            }
            HL::Entrant const* found = nullptr;
            for (std::size_t split = args.size(); split >= 3 && !found; --split)
            {
                std::vector<HL::Entrant const*> const matches = FindNpcs(rt.state, JoinWords(args, 2, split));
                if (matches.size() != 1)
                    continue;
                std::string const rest = JoinWords(args, split, args.size());
                HL::LadderTable probe;
                if (!rest.empty() && !CityTable(rt, rest, probe))
                    continue; // the words after the NPC do not name a table
                found = matches.front();
                cityText = rest;
            }
            if (!found)
            {
                OneNpcOrTell(rt.state, handler, JoinWords(args, 2, args.size()));
                return;
            }
            npc = *found;
        }
        HL::LadderTable table;
        if (!TableForWalk(handler, cityText, table))
            return;
        Player* gm = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!gm || !gm->IsInWorld())
        {
            handler->SendSysMessage("Hearthstone: .hs ladder walk needs a character in the world.");
            return;
        }
        std::string why;
        if (!HsLadderPreviewWalk(gm, table, static_cast<uint32_t>(npc.id), why))
        {
            handler->SendSysMessage("Hearthstone: no walk-up: " + why);
            return;
        }
        handler->SendSysMessage("Hearthstone: " + npc.name + " (entry " + std::to_string(npc.id) + ") walks to the table in " + table.city +
            " (no match, nothing is counted); it says a line, stands up and walks back.");
        sLog.outString("Hearthstone: GM %s started a walk-up preview of %s (%llu) at the table in %s", HsGmName(handler).c_str(),
            npc.name.c_str(), static_cast<unsigned long long>(npc.id), table.city.c_str());
    }

    // Reads the world data of the chairs once at startup and logs what it found (the ladder must be ready).
    void LoadChairData(Runtime& rt)
    {
        rt.world = HL::LadderWorld();
        rt.worldReady = false;
        rt.offers.clear();
        rt.offerTables = CheckHsLadderOfferTables();
        if (CheckHsLadderWorldTables() && LoadHsLadderWorld(rt.world))
            rt.worldReady = !rt.world.tables.empty();
        std::vector<HL::RosterRisk> risks;
        if (LoadHsLadderRosterRisks(risks))
        {
            std::string const summary = HL::RosterRiskSummary(risks, rt.world.blocked);
            if (!summary.empty())
                sLog.outString("Hearthstone: ladder: %s", summary.c_str());
        }
        if (rt.worldReady && rt.offerTables)
            sLog.outString("Hearthstone: ladder chairs ready: %u table(s)", static_cast<uint32_t>(rt.world.tables.size()));
        else
            sLog.outError("Hearthstone: the ladder chairs are unavailable (%s); the simulated ladder keeps running",
                !rt.offerTables ? "character migration 0016_hearthstone_ladder_offer.sql missing" : "no working ladder table");
    }
}

// ---- API --------------------------------------------------------------------------------------------------------------

bool HsLadderEnabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

bool HsLadderReady()
{
    return g_ready.load(std::memory_order_relaxed);
}

void HsLadderStartup()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    Hs::LadderSettings const settings = sHearthstoneConfig.GetLadder();
    rt.enabled = settings.enable;
    g_enabled.store(settings.enable, std::memory_order_relaxed);
    g_ready.store(false, std::memory_order_relaxed);
    if (!settings.enable)
    {
        sLog.outString("Hearthstone: the NPC ladder is disabled (Hearthstone.Ladder.Enable = 0)");
        return;
    }
    rt.rules.startRounds = static_cast<int>(settings.startRounds);
    // The ladder duels (character migration 0017) are read before the ladder boots: Boot queues the NPC rounds that
    // were waiting. Without the tables the simulated ladder runs, the chairs refuse to start duels.
    if (!HsLadderDuels().Startup())
        sLog.outError("Hearthstone: ranked ladder duels are unavailable; the simulated ladder keeps running");
    if (Boot(rt, false))
        LoadChairData(rt);
}

void HsLadderUpdate(uint32_t diff)
{
    if (!g_ready.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    if (rt.retryMs > 0)
        rt.retryMs = rt.retryMs > static_cast<int>(diff) ? rt.retryMs - static_cast<int>(diff) : 0;
    Pump(rt);
}

void HsLadderShutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    if (!rt.enabled)
        return;
    bool const wasBusy = rt.worker.Busy();
    rt.stopping = true;
    g_ready.store(false, std::memory_order_relaxed);
    rt.worker.Stop();
    sLog.outString("Hearthstone: NPC ladder stopped at round %u%s", rt.state.roundsPlayed,
        wasBusy ? " (the round in progress is dropped and is played again at the next start)" : "");
}

std::string HsLadderWhyText(std::string const& why)
{
    if (why == "disabled")
        return "the ladder is disabled";
    if (why == "unavailable")
        return "the ladder is unavailable (see the server log)";
    if (why == "updating")
        return "the ladder start is not finished yet";
    if (why == "stalled")
        return "the ladder stopped after an error (see the server log)";
    if (why == "unknown_npc")
        return "that NPC is not on the ladder";
    if (why == "full")
        return "too much ladder work is queued";
    if (why == "storage")
        return "the result could not be saved (nothing changed)";
    return why;
}

HsLadderResultReply HsLadderPlayerResult(uint32_t accountId, std::string const& playerName, uint32_t npcEntry,
                                         HL::Result result)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return SubmitPlayer(R(), accountId, playerName, npcEntry, result);
}

// ---- the chairs -------------------------------------------------------------------------------------------------

std::string HsLadderChairWhy()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ChairWhy(R());
}

bool HsLadderTableByChair(uint32_t chairGuid, HL::LadderTable& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    HL::LadderTable const* t = R().world.FindByChair(chairGuid);
    if (t)
        out = *t;
    return t != nullptr;
}

bool HsLadderTableByGuid(uint32_t tableGuid, HL::LadderTable& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    HL::LadderTable const* t = R().world.FindByTable(tableGuid);
    if (t)
        out = *t;
    return t != nullptr;
}

bool HsLadderTableByCity(std::string const& city, HL::LadderTable& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return CityTable(R(), city, out);
}

std::vector<HL::LadderTable> HsLadderTables()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return R().world.tables;
}

bool HsLadderStandingFor(uint32_t accountId, HsLadderStanding& standing)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime const& rt = R();
    if (!rt.ready)
        return false;
    standing = StandingOf(rt, accountId);
    return true;
}

bool HsLadderOffersFor(uint32_t accountId, HsLadderStanding& standing, std::vector<HsLadderOfferView>& offers,
                       std::string& why)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    offers.clear();
    why = ChairWhy(rt);
    if (!why.empty())
        return false;
    HL::OfferSet* set = nullptr;
    if (!EnsureOffersOf(rt, accountId, set))
    {
        why = "Your offers could not be read right now (see the server log). Try again later.";
        return false;
    }
    standing = StandingOf(rt, accountId);
    for (HL::EntrantId id : set->slots)
    {
        HsLadderOfferView view;
        if (OfferViewOf(rt, accountId, id, view))
            offers.push_back(view);
    }
    if (offers.empty())
    {
        why = "The league has nobody to offer you right now.";
        return false;
    }
    return true;
}

bool HsLadderIsOffered(uint32_t accountId, uint32_t npcEntry)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    if (!ChairWhy(rt).empty())
        return false;
    auto const found = rt.offers.find(accountId);
    if (found == rt.offers.end() || !HL::OfferSetValid(found->second, rt.state, rt.world.blocked))
        return false;
    for (HL::EntrantId id : found->second.slots)
        if (id == HL::NpcId(npcEntry))
            return true;
    return false;
}

bool HsLadderNpcSetup(uint32_t npcEntry, HsLadderNpc& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    HL::Entrant const* npc = rt.ready ? rt.state.Find(HL::NpcId(npcEntry)) : nullptr;
    if (!npc || npc->IsPlayer())
        return false;
    for (HL::LibraryDeck const& deck : rt.library)
        if (deck.key == npc->deckKey)
        {
            out = HsLadderNpc();
            out.entry = npcEntry;
            out.name = NameOf(rt.state, npc->id);
            out.heroClass = npc->heroClass;
            out.faction = npc->faction;
            out.deckKey = npc->deckKey;
            out.profile = npc->profile;
            out.deck = deck.cards;
            out.enabled = npc->enabled;
            out.blocked = rt.world.blocked.count(npc->id) != 0;
            return true;
        }
    return false;
}

std::string HsLadderLine(HL::Result playerResult, uint32_t seed)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    HL::LadderLine const* line = R().world.PickLine(HL::LineKindForPlayerResult(playerResult), seed);
    return line ? line->text : std::string();
}

bool HsLadderDuelQueued(uint64_t duelId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return DuelQueued(R(), duelId);
}

HsLadderResultReply HsLadderMatchResult(uint32_t accountId, std::string const& playerName, uint32_t npcEntry,
                                        HL::Result result, uint64_t duelId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    HsLadderResultReply reply = SubmitPlayer(rt, accountId, playerName, npcEntry, result, duelId);
    if (!reply.ok)
        return reply;
    // The match counted: the three offers are used up, the next sit-down draws new ones from the ladder after the NPC
    // round that follows (OffersAfterMatch also remembers the NPC as the last opponent).
    HL::OfferSet* set = rt.offerTables ? OfferSetOf(rt, accountId) : nullptr;
    if (!set)
    {
        sLog.outError("Hearthstone: ladder: the offers of account %u could not be updated after the match (the old "
            "offers stay)", accountId);
        return reply;
    }
    HL::OffersAfterMatch(*set, HL::NpcId(npcEntry));
    if (!HsLadderStore().SaveOffers(*set))
        sLog.outError("Hearthstone: ladder: the offers of account %u could not be saved after the match (kept in memory)",
            accountId);
    return reply;
}

// ---- GM commands -----------------------------------------------------------------------------------------------------

void HsLadderUsage(ChatHandler* handler)
{
    handler->SendSysMessage("Usage: .hs ladder show [page]");
    handler->SendSysMessage("       .hs ladder npc <entry|name part>");
    handler->SendSysMessage("       .hs ladder player [character|#accountId]");
    handler->SendSysMessage("       .hs ladder offers [character|#accountId]   (the three stored offers of the chair)");
    handler->SendSysMessage("       .hs ladder offers reroll [character|#accountId]   (draws three new offers and saves them)");
    handler->SendSysMessage("       .hs ladder walk <entry|name> [city]   (the NPC walks to the table of your city or the named one, says a "
        "line, walks back; no match, nothing is counted)");
    handler->SendSysMessage("       .hs ladder result win|loss|draw <entry|name> [character|#accountId]   (a fake "
        "player result, then one NPC round)");
    handler->SendSysMessage("       .hs ladder round [n]   (queues n NPC rounds, at most 50)");
    handler->SendSysMessage("       .hs ladder duel [character|#accountId]   (the stored ladder duel of the account: paused or running)");
    handler->SendSysMessage("       .hs ladder duel abandon <character|#accountId> confirm   (removes a ladder duel without a result)");
    handler->SendSysMessage("       .hs ladder resume   (runs the ladder again after it stopped on an error)");
    handler->SendSysMessage("       .hs ladder reset confirm   (deletes the ladder, the offers and the ladder duels and starts a new one)");
}

void HsLadderCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
{
    std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
    bool const reroll = sub == "offers" && args.size() > 2 && Lower(args[2]) == "reroll";
    bool const abandon = sub == "duel" && args.size() > 2 && Lower(args[2]) == "abandon";
    bool const mutating = sub == "result" || sub == "round" || sub == "reset" || sub == "resume" || sub == "walk" ||
        reroll || abandon;
    if ((sub != "show" && sub != "npc" && sub != "player" && sub != "offers" && sub != "duel" && !mutating) ||
        (sub == "duel" && (abandon ? (args.size() != 5 || Lower(args[4]) != "confirm") : args.size() > 3)) ||
        (sub == "result" && args.size() < 4) || (sub == "walk" && args.size() < 3) ||
        (sub == "reset" && (args.size() != 3 || Lower(args[2]) != "confirm")) || (sub == "round" && args.size() > 3) ||
        (sub == "resume" && args.size() != 2) ||
        (sub == "offers" && args.size() > (reroll ? 4u : 3u)))
    {
        HsLadderUsage(handler);
        return;
    }
    if (mutating && security < static_cast<int>(SEC_DEVELOPER))
    {
        handler->SendSysMessage("Hearthstone: .hs ladder " + sub + (reroll ? " reroll" : "") + (abandon ? " abandon" : "") +
            " needs security level 3 (developer) or higher.");
        return;
    }
    if (sub == "walk")
    {
        WalkCommand(handler, args); // takes the ladder mutex itself (it calls into the table code)
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    // The reset is the way out of an unavailable ladder (a stored ladder that no longer loads), so it is not refused here.
    if (sub != "reset" && !UsableOrTell(rt, handler))
        return;

    if (sub == "offers")
    {
        OffersCommand(rt, handler, args, reroll);
        return;
    }
    if (sub == "duel")
    {
        DuelCommand(rt, handler, args, abandon);
        return;
    }

    if (sub == "show")
    {
        Show(rt, handler, args);
        return;
    }
    if (sub == "npc")
    {
        ShowNpc(rt, handler, args);
        return;
    }
    if (sub == "player")
    {
        ShowPlayer(rt, handler, args);
        return;
    }

    if (sub == "reset")
    {
        // Real work only (a start or a round running, queued work, a retry): an unavailable ladder is not started, which
        // Busy() also counts as busy, so that alone must not block the reset.
        if (rt.worker.Busy() || !rt.queue.empty() || rt.retryMs > 0)
        {
            handler->SendSysMessage(std::string("Hearthstone: the ladder is ") + StateWord(rt) + "; reset it when it is done.");
            return;
        }
        if (HsLadderTableBusy())
        {
            handler->SendSysMessage("Hearthstone: a ladder table is in use (a walk-up or a ladder match is running); reset it "
                "when that is done.");
            return;
        }
        sLog.outString("Hearthstone: GM %s resets the NPC ladder", HsGmName(handler).c_str());
        // The offers name NPCs of the old ladder: they go with it (the tables only exist with migration 0016).
        if (rt.offerTables && !HsLadderStore().ResetOffers())
        {
            handler->SendSysMessage("Hearthstone: the stored offers could not be deleted; nothing was reset (see the server log).");
            return;
        }
        rt.offers.clear();
        if (!Boot(rt, true))
        {
            handler->SendSysMessage("Hearthstone: the reset failed; the ladder is unavailable (see the server log).");
            return;
        }
        // The chairs read their data once at startup (LoadChairData); a reset on a server whose start failed never did.
        LoadChairData(rt);
        handler->SendSysMessage("Hearthstone: the ladder was deleted and league " + std::to_string(HsHeroCurrentLeague()) +
            " has started (" + std::to_string(NpcCount(rt.state)) +
            " NPC(s), new decks, everyone back at rank 25); .hs ladder show reports the progress.");
        return;
    }

    if (sub == "resume")
    {
        if (!rt.stalled)
        {
            handler->SendSysMessage(std::string("Hearthstone: the ladder has not stopped (") + StateWord(rt) + ").");
            return;
        }
        rt.stalled = false;
        rt.roundFailures = 0;
        sLog.outString("Hearthstone: GM %s resumes the NPC ladder", HsGmName(handler).c_str());
        Pump(rt);
        handler->SendSysMessage(std::string("Hearthstone: the ladder runs again (") + StateWord(rt) + ").");
        return;
    }

    if (!rt.state.started)
    {
        handler->SendSysMessage(std::string("Hearthstone: the ladder start is not finished (") + StateWord(rt) +
            "); " + sub + " works after it.");
        return;
    }

    if (sub == "round")
    {
        uint32_t n = 1;
        if (args.size() == 3 && (!AllDigits(args[2]) || (n = static_cast<uint32_t>(std::strtoul(args[2].c_str(), nullptr, 10))) < 1 ||
            n > kMaxGmRounds))
        {
            handler->SendSysMessage("Hearthstone: n is 1 to " + std::to_string(kMaxGmRounds) + ".");
            return;
        }
        if (rt.queue.size() + n > kMaxQueue)
        {
            handler->SendSysMessage("Hearthstone: too much ladder work is queued (" + std::to_string(rt.queue.size()) + ").");
            return;
        }
        if (rt.stalled)
        {
            handler->SendSysMessage("Hearthstone: the ladder stopped after an error (see the server log; .hs ladder resume).");
            return;
        }
        std::deque<Work> next = rt.queue;
        for (uint32_t i = 0; i < n; ++i)
            next.push_back(Work());
        if (!HsLadderStore().Run(LadderQueueStatements(QueueRowsOf(next)), "ladder queue"))
        {
            handler->SendSysMessage("Hearthstone: the rounds could not be stored; nothing was queued (see the server log).");
            return;
        }
        rt.queue = std::move(next);
        Pump(rt);
        handler->SendSysMessage("Hearthstone: queued " + std::to_string(n) + " NPC round(s); " + std::to_string(rt.queue.size()) +
            " waiting, ladder " + StateWord(rt) + ".");
        sLog.outString("Hearthstone: GM %s queued %u ladder round(s)", HsGmName(handler).c_str(), n);
        return;
    }

    // result win|loss|draw <npc> [character]
    std::string const word = Lower(args[2]);
    HL::Result result = HL::Result::Win;
    if (word == "win")
        result = HL::Result::Win;
    else if (word == "loss" || word == "lose")
        result = HL::Result::Loss;
    else if (word == "draw")
        result = HL::Result::Draw;
    else
    {
        HsLadderUsage(handler);
        return;
    }
    // The NPC is all the words after the result. Only when they name no single NPC and the words without the last
    // one do, the last word is the character (or #accountId).
    std::size_t npcEnd = args.size();
    if (FindNpcs(rt.state, JoinWords(args, 3, npcEnd)).size() != 1 && args.size() > 4 &&
        FindNpcs(rt.state, JoinWords(args, 3, args.size() - 1)).size() == 1)
        npcEnd = args.size() - 1;
    HL::Entrant const* npc = OneNpcOrTell(rt.state, handler, JoinWords(args, 3, npcEnd));
    if (!npc)
        return;
    uint32_t accountId = 0;
    std::string label, name;
    if (!ResolveAccountOrTell(handler, args, npcEnd, accountId, label, name))
        return;
    std::string const npcName = npc->name;
    HsLadderResultReply const reply = SubmitPlayer(rt, accountId, name, static_cast<uint32_t>(npc->id), result);
    if (!reply.ok)
    {
        handler->SendSysMessage("Hearthstone: no result applied: " + HsLadderWhyText(reply.why) + ".");
        return;
    }
    if (reply.queued)
    {
        handler->SendSysMessage("Hearthstone: the ladder is " + std::string(StateWord(rt)) + ": the " + HL::ResultCode(result) +
            " for " + label + " against " + npcName + " is queued and applies when the running work is done.");
        return;
    }
    HL::PlayerResultReport const& r = reply.report;
    handler->SendSysMessage("Hearthstone: " + label + " (" + NameOf(rt.state, reply.playerAfter.id) + ") " + HL::ResultCode(result) +
        " against " + npcName + ": " + HL::RankText(r.before.rank, r.before.stars) + " -> " + HL::RankText(r.after.rank, r.after.stars) +
        ", rating " + HL::RatingText(reply.playerBefore.rating) + " -> " + HL::RatingText(reply.playerAfter.rating) +
        ", streak " + std::to_string(r.winStreak) + (r.streakBonus ? " (bonus star)" : "") + (r.newBest ? ", new best" : ""));
    handler->SendSysMessage("  " + npcName + ": " + HL::RankText(r.npcBefore.rank, r.npcBefore.stars) + " -> " +
        HL::RankText(r.npcAfter.rank, r.npcAfter.stars) + ", rating " + HL::RatingText(reply.npcBefore.rating) + " -> " +
        HL::RatingText(reply.npcAfter.rating) + ". One NPC round is queued without " + npcName + ".");
    for (HL::EntrantId id : r.pushedOut)
        handler->SendSysMessage("  " + NameOf(rt.state, id) + " was pushed out of Legend.");
    sLog.outString("Hearthstone: GM %s applied %s of account %u against %s (%llu): %s -> %s", HsGmName(handler).c_str(),
        HL::ResultCode(result), accountId, npcName.c_str(), static_cast<unsigned long long>(npc->id),
        HL::RankText(r.before.rank, r.before.stars).c_str(), HL::RankText(r.after.rank, r.after.stars).c_str());
}

void HsLadderStatus(ChatHandler* handler)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Runtime& rt = R();
    std::string text = std::string("Ladder: ") + StateWord(rt);
    if (rt.enabled && rt.ready)
    {
        Hs::LadderSettings const settings = sHearthstoneConfig.GetLadder();
        text += ", " + std::to_string(rt.state.roundsPlayed) + " round(s) played, " + std::to_string(NpcCount(rt.state)) +
            " NPC(s), " + std::to_string(PlayerCount(rt.state)) + " player(s), " + std::to_string(rt.queue.size()) +
            " queued, " + std::to_string(settings.threads) + " thread(s), chairs " +
            (rt.worldReady && rt.offerTables ? std::to_string(rt.world.tables.size()) + " table(s)" : std::string("unavailable"));
    }
    handler->SendSysMessage(text);
}
