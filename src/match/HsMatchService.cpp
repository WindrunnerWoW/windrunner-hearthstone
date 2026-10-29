#include "HsMatchService.h"

#include "../HsDataset.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Hs
{
    namespace
    {
        std::string Trim(std::string const& s)
        {
            std::size_t b = 0;
            std::size_t e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
                ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
                --e;
            return s.substr(b, e - b);
        }

        Match::MatchSetup SetupOf(MatchStartParams const& params)
        {
            Match::MatchSetup setup;
            setup.seed = params.seed;
            setup.sides[0].name = params.humanName;
            setup.sides[0].heroClass = params.humanClass;
            setup.sides[0].deck = params.humanDeck;
            setup.sides[1].name = params.opponentName;
            setup.sides[1].heroClass = params.opponentClass;
            setup.sides[1].deck = params.opponentDeck;
            setup.sides[1].heroHealth = params.opponentHeroHealth;
            setup.sides[1].heroArmor = params.opponentHeroArmor;
            setup.sides[1].heroCardId = params.opponentHeroCardId;
            setup.sides[1].heroPowerId = params.opponentHeroPowerId;
            setup.sides[0].start = params.humanStart;
            setup.sides[1].start = params.opponentStart;
            setup.firstSide = params.firstSide;
            return setup;
        }

        // res= and why= of a finished match from the human's side.
        void ResultOf(Match::MatchView const& view, std::string const& abortWhy, std::string& res, std::string& why)
        {
            res = GameResultCode(view);
            why = GameWhyCode(view, abortWhy);
        }
    }

    char const* InterruptCode(Interrupt why)
    {
        switch (why)
        {
            case Interrupt::Stuck:
                return "stuck";
            case Interrupt::Teleport:
                return "teleport";
            case Interrupt::Death:
                return "death";
            case Interrupt::Npc:
                return "npc";
            case Interrupt::Gm:
                return "gm";
            case Interrupt::Error:
                return "error";
        }
        return "error";
    }

    MatchService::MatchService(uint32_t firstMatchId)
        : m_nextMatchId(firstMatchId == 0 ? 1 : firstMatchId)
    {
    }

    void MatchService::SetCardDb(std::shared_ptr<Match::CardDb const> db)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_db = std::move(db);
    }

    std::shared_ptr<Match::CardDb const> MatchService::CardDb() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_db;
    }

    void MatchService::SetAiDelayMs(uint32_t ms)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_aiDelayMs = std::min(std::max(ms, kMinAiDelayMs), kMaxAiDelayMs);
    }

    uint32_t MatchService::AiDelayMs() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_aiDelayMs;
    }

    void MatchService::RecountLocked()
    {
        std::size_t running = 0;
        for (auto const& entry : m_matches)
            if (entry.second.match && entry.second.match->GetPhase() != Match::Phase::Over)
                ++running;
        m_running.store(running, std::memory_order_relaxed);
    }

    std::string MatchService::PayloadLocked(Entry& e, uint32_t sinceSeq, bool markNewest,
                                          std::vector<Match::Event>* events)
    {
        Match::MatchView const view = e.match->ViewFor(0, sinceSeq);
        if (events)
            *events = view.events;
        GameHeader header;
        header.matchId = e.matchId;
        header.myName = e.humanName;
        header.opponentName = e.opponentName;
        header.difficulty = e.difficulty;
        header.durable = e.params && e.params->ladder;
        header.abortWhy = e.abortWhy;
        if (e.deadlineMs != 0 && view.phase == Match::Phase::Play && view.turnTimeLimitSec > 0)
        {
            uint64_t const left = e.deadlineMs > e.lastMs ? e.deadlineMs - e.lastMs : 0;
            header.timerSec = static_cast<int>(std::max<uint64_t>(1, (left + 999) / 1000));
        }
        std::string const payload = EncodeGame(header, view);
        if (markNewest)
            e.sentEventSeq = e.match->LastEventSeq();
        return payload;
    }

    void MatchService::UpdateTimerLocked(Entry& e, uint64_t nowMs)
    {
        e.lastMs = std::max(e.lastMs, nowMs);
        Match::Match const& m = *e.match;
        if (m.GetPhase() != Match::Phase::Play)
        {
            e.deadlineMs = 0;
            e.timerTurn = -1;
            return;
        }
        if (m.Turn() != e.timerTurn)
        {
            e.timerTurn = m.Turn();
            e.deadlineMs = 0;
        }
        // Only the human's turns are timed: the AI paces its actions with AiDelayMs (1.5 s each by default), so a
        // 15 s limit would cut a long AI turn short; its own thinking time is a few milliseconds.
        int const limit = m.ActiveSide() == 0 ? m.TurnTimeLimitSec() : 0;
        if (limit <= 0)
            e.deadlineMs = 0;
        else if (e.deadlineMs == 0)
            e.deadlineMs = e.lastMs + static_cast<uint64_t>(limit) * 1000; // turn start or the moment the limit appeared
    }

    bool MatchService::ExpireTurnLocked(Entry& e, uint64_t nowMs, std::string& note)
    {
        Match::Match& m = *e.match;
        int const side = m.ActiveSide();
        // A pending pick (Tracking) is answered with its first option first.
        for (int guard = 0; guard < 4 && m.GetPhase() == Match::Phase::Play; ++guard)
        {
            Match::MatchView const view = m.ViewFor(side, m.LastEventSeq());
            if (!view.pickPending || view.pick.empty())
                break;
            Match::Action choose;
            choose.type = Match::ActionType::Choose;
            choose.source = view.pick[0].id;
            Match::ActionError const err = ApplyLocked(e, side, choose);
            if (err != Match::ActionError::None)
            {
                note = std::string("the turn timer could not answer a pending pick (") + Match::ActionErrorCode(err) + ")";
                break;
            }
        }
        if (m.GetPhase() == Match::Phase::Play)
        {
            Match::Action end;
            end.type = Match::ActionType::EndTurn;
            Match::ActionError const err = ApplyLocked(e, side, end);
            if (err != Match::ActionError::None)
            {
                note += std::string(note.empty() ? "" : "; ") + "the turn timer could not end the turn (" +
                    Match::ActionErrorCode(err) + "), the match is aborted";
                m.Abort();
                e.abortWhy = InterruptCode(Interrupt::Error);
            }
        }
        e.nextAiMs = nowMs + m_aiDelayMs;
        UpdateTimerLocked(e, nowMs);
        return true;
    }

    Match::ActionError MatchService::ApplyLocked(Entry& e, int side, Match::Action const& action)
    {
        Match::ActionError const err = e.match->Apply(side, action);
        if (err == Match::ActionError::None && e.arena)
        {
            LoggedAction logged;
            logged.side = side;
            logged.action = action;
            e.log.push_back(std::move(logged));
        }
        return err;
    }

    std::vector<LoggedAction> MatchService::LoggedSince(Entry const& e, std::size_t from)
    {
        if (!e.arena || from >= e.log.size())
            return std::vector<LoggedAction>();
        return std::vector<LoggedAction>(e.log.begin() + static_cast<std::ptrdiff_t>(from), e.log.end());
    }

    uint32_t MatchService::RemainingTimerMs(Entry const& e, uint64_t nowMs)
    {
        if (e.deadlineMs == 0)
            return 0;
        uint64_t const now = std::max(e.lastMs, nowMs);
        uint64_t const left = e.deadlineMs > now ? e.deadlineMs - now : 0;
        return static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(left, 1), 0xFFFFFFFFull));
    }

    MatchEnd MatchService::EndOfLocked(uint32_t guid, Entry const& e) const
    {
        MatchEnd end;
        end.guid = guid;
        end.matchId = e.matchId;
        end.turn = e.match->Turn();
        end.ownTurns = e.match->OwnTurns(0);
        end.tallies = {e.match->Tally(0), e.match->Tally(1)};
        end.humanName = e.humanName;
        end.opponentName = e.opponentName;
        end.difficulty = e.difficulty;
        end.meta = e.meta;
        end.arena = e.arena;
        end.arenaRunId = e.arena && e.params ? e.params->arenaRunId : 0;
        ResultOf(e.match->ViewFor(0, e.match->LastEventSeq()), e.abortWhy, end.res, end.why);
        return end;
    }

    MatchInfo MatchService::InfoOf(uint32_t guid, Entry const& e)
    {
        MatchInfo info;
        info.guid = guid;
        info.matchId = e.matchId;
        info.humanName = e.humanName;
        info.opponentName = e.opponentName;
        info.difficulty = e.difficulty;
        info.running = e.match->GetPhase() != Match::Phase::Over;
        info.turn = e.match->Turn();
        info.humanHealth = e.match->HeroHealth(0);
        info.opponentHealth = e.match->HeroHealth(1);
        info.seq = e.match->Seq();
        info.seed = e.match->Seed();
        if (info.running)
        {
            info.res = "-";
            info.why = "-";
        }
        else
            ResultOf(e.match->ViewFor(0, e.match->LastEventSeq()), e.abortWhy, info.res, info.why);
        return info;
    }

    MatchStartResult MatchService::Start(MatchStartParams const& params, uint64_t nowMs)
    {
        MatchStartResult result;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_db)
        {
            result.error = "engine";
            return result;
        }
        auto const existing = m_matches.find(params.guid);
        if (existing != m_matches.end() && existing->second.match &&
            existing->second.match->GetPhase() != Match::Phase::Over)
        {
            result.error = "running";
            return result;
        }

        if (params.difficulty != "normal" && params.difficulty != "hard" && params.difficulty != "boss")
        {
            result.error = "the difficulty must be normal, hard or boss";
            return result;
        }

        std::string error;
        std::unique_ptr<Match::Match> match = Match::Match::Create(*m_db, SetupOf(params), error);
        if (!match)
        {
            result.error = error.empty() ? std::string("the match could not be created") : error;
            return result;
        }

        // The AI mulligans at once; the human's mulligan is the first thing the client shows.
        Match::MatchView const aiView = match->ViewFor(1, match->LastEventSeq());
        if (aiView.mulliganPending)
        {
            Match::Action action = Match::ChooseAiAction(*match, 1, *m_db, 0, params.profile,
                AiSeedFor(params.seed, match->Seq()));
            if (action.type != Match::ActionType::Mulligan)
            {
                action = Match::Action();
                action.type = Match::ActionType::Mulligan;
            }
            Match::ActionError const err = match->Apply(1, action);
            if (err != Match::ActionError::None)
            {
                result.error = std::string("the AI mulligan was refused (") + Match::ActionErrorCode(err) + ")";
                return result;
            }
            if (params.arena)
            {
                LoggedAction logged;
                logged.side = 1;
                logged.action = action;
                result.logged.push_back(std::move(logged));
            }
        }
        result.arenaRunId = params.arena ? params.arenaRunId : 0;

        Entry entry = MakeEntryLocked(params, std::move(match), nowMs);
        entry.log = result.logged;

        // A finished match of the character is replaced.
        m_matches.erase(params.guid);
        Entry& stored = m_matches.emplace(params.guid, std::move(entry)).first->second;
        UpdateTimerLocked(stored, nowMs);
        result.ok = true;
        result.matchId = stored.matchId;
        result.payload = PayloadLocked(stored, 0, true);
        RecountLocked();
        return result;
    }

    MatchService::Entry MatchService::MakeEntryLocked(MatchStartParams const& params,
                                                      std::unique_ptr<Match::Match> match, uint64_t nowMs)
    {
        Entry entry;
        entry.matchId = m_nextMatchId++;
        if (m_nextMatchId == 0)
            m_nextMatchId = 1;
        entry.db = m_db;
        entry.match = std::move(match);
        entry.humanName = params.humanName;
        entry.opponentName = params.opponentName;
        entry.difficulty = params.difficulty;
        entry.meta = params.meta;
        entry.meta.seed = entry.match->Seed();
        entry.meta.aiDelayMs = m_aiDelayMs;
        entry.meta.humanClass = params.humanClass;
        entry.meta.opponentClass = params.opponentClass;
        entry.meta.humanCards = EncodeCardList(params.humanDeck);
        entry.meta.opponentCards = EncodeCardList(params.opponentDeck);
        entry.profile = params.profile;
        entry.nextAiMs = nowMs + m_aiDelayMs;
        entry.arena = params.arena;
        if (params.arena)
            entry.params = std::make_shared<MatchStartParams const>(params);
        return entry;
    }

    std::string MatchService::Get(uint32_t guid, uint64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end() || !it->second.match)
            return EncodeGameNone();
        it->second.lastMs = std::max(it->second.lastMs, nowMs);
        // A resync: no events, the mark moves to the newest one.
        return PayloadLocked(it->second, it->second.match->LastEventSeq(), true);
    }

    GameActOutcome MatchService::Act(uint32_t guid, GameActRequest const& request, uint64_t nowMs)
    {
        GameActOutcome out;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end() || !it->second.match || it->second.matchId != request.matchId)
        {
            out.why = kWhyMatch;
            return out;
        }
        Entry& e = it->second;
        Match::Match& m = *e.match;
        if (m.GetPhase() == Match::Phase::Over)
        {
            out.why = kWhyOver;
            return out;
        }
        if (request.action.type != Match::ActionType::Concede && request.seq != m.Seq())
        {
            out.why = kWhyStale;
            return out;
        }

        Match::Action action = request.action;
        if (action.type == Match::ActionType::HeroPower)
            action.source = m.ViewFor(0, m.LastEventSeq()).self.hero.powerId;

        std::size_t const logFirst = e.log.size();
        Match::ActionError const err = ApplyLocked(e, 0, action);
        if (err != Match::ActionError::None)
        {
            out.why = Match::ActionErrorCode(err);
            return out;
        }

        e.nextAiMs = nowMs + m_aiDelayMs;
        UpdateTimerLocked(e, nowMs);
        out.ok = true;
        out.arenaRunId = e.arena ? e.params->arenaRunId : 0;
        out.loggedFirst = static_cast<uint32_t>(logFirst);
        out.logged = LoggedSince(e, logFirst);
        out.payload = PayloadLocked(e, e.sentEventSeq, true, &out.events);
        if (m.GetPhase() == Match::Phase::Over)
        {
            out.ended = true;
            out.end = EndOfLocked(guid, e);
        }
        RecountLocked();
        return out;
    }

    GameLeaveOutcome MatchService::Leave(uint32_t guid, uint32_t matchId)
    {
        GameLeaveOutcome out;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it != m_matches.end() && it->second.match && it->second.matchId == matchId)
        {
            if (it->second.match->GetPhase() != Match::Phase::Over)
            {
                out.why = kWhyActive;
                return out;
            }
            m_matches.erase(it);
        }
        out.ok = true;
        out.payload = EncodeGameNone();
        return out;
    }

    MatchAbortOutcome MatchService::Abort(uint32_t guid, Interrupt why)
    {
        MatchAbortOutcome out;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end() || !it->second.match || it->second.match->GetPhase() == Match::Phase::Over)
            return out;
        Entry& e = it->second;
        e.match->Abort();
        e.abortWhy = InterruptCode(why);
        out.ended = true;
        out.payload = PayloadLocked(e, e.sentEventSeq, true);
        out.end = EndOfLocked(guid, e);
        RecountLocked();
        return out;
    }

    bool MatchService::Drop(uint32_t guid, MatchEnd* end)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end())
            return false;
        bool const wasRunning = it->second.match && it->second.match->GetPhase() != Match::Phase::Over;
        if (wasRunning && end)
        {
            // The match ends without a winner: the engine's own result is reported as an interruption.
            it->second.match->Abort();
            it->second.abortWhy = "error";
            *end = EndOfLocked(guid, it->second);
            end->why = "logout";
        }
        m_matches.erase(it);
        RecountLocked();
        return wasRunning;
    }

    bool MatchService::IsArena(uint32_t guid) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        return it != m_matches.end() && it->second.match && it->second.arena;
    }

    bool MatchService::Suspend(uint32_t guid, SuspendedDuel& out, uint64_t nowMs, std::size_t keepActions)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end() || !it->second.match || !it->second.arena)
            return false;
        Entry& e = it->second;
        SuspendedDuel duel;
        duel.params = *e.params;
        duel.params.guid = guid;
        duel.log = e.log;
        duel.timerMs = RemainingTimerMs(e, nowMs);
        duel.over = e.match->GetPhase() == Match::Phase::Over;
        if (keepActions < duel.log.size())
        {
            // What the store did not get is not replayed: the timer of the shorter state starts again.
            duel.log.resize(keepActions);
            duel.timerMs = 0;
            duel.over = false;
        }
        duel.rulesVersion = Match::kRulesVersion;
        duel.datasetHash = kDatasetHash;
        out = std::move(duel);
        m_matches.erase(it);
        RecountLocked();
        return true;
    }

    MatchRestoreResult MatchService::Restore(SuspendedDuel const& duel, uint64_t nowMs)
    {
        MatchRestoreResult result;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_db)
        {
            result.error = "engine";
            return result;
        }
        MatchStartParams const& params = duel.params;
        if (!params.arena)
        {
            result.error = "arena";
            return result;
        }
        if (duel.rulesVersion != Match::kRulesVersion)
        {
            result.error = "rules";
            return result;
        }
        if (duel.datasetHash != kDatasetHash)
        {
            result.error = "dataset";
            return result;
        }
        auto const existing = m_matches.find(params.guid);
        if (existing != m_matches.end() && existing->second.match &&
            existing->second.match->GetPhase() != Match::Phase::Over)
        {
            result.error = "running";
            return result;
        }
        if (params.difficulty != "normal" && params.difficulty != "hard" && params.difficulty != "boss")
        {
            result.error = "the difficulty must be normal, hard or boss";
            return result;
        }

        std::string error;
        std::unique_ptr<Match::Match> match = Match::Match::Create(*m_db, SetupOf(params), error);
        if (!match)
        {
            result.error = error.empty() ? std::string("the match could not be created") : error;
            return result;
        }

        // Replay. The AI's per-turn counters follow the rule of Tick: an AI action counts for the turn it was
        // applied in, from the first action of that turn; the opening mulligan is not a Tick action.
        int aiTurn = -1;
        int aiActions = 0;
        for (std::size_t i = 0; i < duel.log.size(); ++i)
        {
            LoggedAction const& step = duel.log[i];
            bool const aiStep = step.side == 1 && match->GetPhase() == Match::Phase::Play;
            if (aiStep && match->Turn() != aiTurn)
            {
                aiTurn = match->Turn();
                aiActions = 0;
            }
            if (step.side < 0 || step.side > 1 || match->Apply(step.side, step.action) != Match::ActionError::None)
            {
                result.error = "log";
                result.failedAt = static_cast<uint32_t>(i);
                return result;
            }
            if (aiStep)
                ++aiActions;
        }

        Entry entry = MakeEntryLocked(params, std::move(match), nowMs);
        entry.log = duel.log;
        entry.aiTurn = aiTurn;
        entry.aiActions = aiActions;

        m_matches.erase(params.guid);
        Entry& stored = m_matches.emplace(params.guid, std::move(entry)).first->second;
        UpdateTimerLocked(stored, nowMs);
        // The saved remainder replaces the fresh deadline the limit would give (UpdateTimerLocked decided whether a
        // deadline runs at all: only for the human's turn while the limit applies).
        if (stored.deadlineMs != 0 && duel.timerMs != 0)
            stored.deadlineMs = nowMs + duel.timerMs;
        result.ok = true;
        result.matchId = stored.matchId;
        // A clean resync: the mark moves to the newest event, the payload carries none (GAME_GET).
        result.ended = stored.match->GetPhase() == Match::Phase::Over;
        if (result.ended)
            result.end = EndOfLocked(params.guid, stored);
        result.payload = PayloadLocked(stored, stored.match->LastEventSeq(), true);
        RecountLocked();
        return result;
    }

    std::vector<MatchPush> MatchService::Tick(uint64_t nowMs)
    {
        std::vector<MatchPush> pushes;
        std::lock_guard<std::mutex> lock(m_mutex);
        bool changed = false;
        for (auto& entry : m_matches)
        {
            Entry& e = entry.second;
            if (!e.match)
                continue;
            Match::Match& m = *e.match;
            e.lastMs = std::max(e.lastMs, nowMs);
            // The Nozdormu turn timer (the human's turns only).
            if (m.GetPhase() == Match::Phase::Play && e.deadlineMs != 0 && nowMs >= e.deadlineMs)
            {
                MatchPush push;
                push.guid = entry.first;
                push.matchId = e.matchId;
                std::size_t const logFirst = e.log.size();
                ExpireTurnLocked(e, nowMs, push.note);
                push.arenaRunId = e.arena ? e.params->arenaRunId : 0;
                push.loggedFirst = static_cast<uint32_t>(logFirst);
                push.logged = LoggedSince(e, logFirst);
                push.payload = PayloadLocked(e, e.sentEventSeq, true, &push.events);
                if (m.GetPhase() == Match::Phase::Over)
                {
                    push.ended = true;
                    push.end = EndOfLocked(entry.first, e);
                    changed = true;
                }
                pushes.push_back(std::move(push));
                continue;
            }
            if (m.GetPhase() != Match::Phase::Play || nowMs < e.nextAiMs)
                continue;
            // The AI acts in its own turn or when it has a pending pick (Tracking); it waits while the
            // human has a pending pick (opponentPicking of the AI's view).
            Match::MatchView const view = m.ViewFor(1, m.LastEventSeq());
            bool const aiActs = view.pickPending || (m.ActiveSide() == 1 && !view.opponentPicking);
            if (!aiActs)
                continue;

            if (m.Turn() != e.aiTurn)
            {
                e.aiTurn = m.Turn();
                e.aiActions = 0;
            }
            MatchPush push;
            push.guid = entry.first;
            push.matchId = e.matchId;
            std::size_t const logFirst = e.log.size();

            Match::Action action = Match::ChooseAiAction(m, 1, *e.db, e.aiActions, e.profile,
                AiSeedFor(m.Seed(), m.Seq()));
            Match::ActionError err = ApplyLocked(e, 1, action);
            if (err != Match::ActionError::None)
            {
                // The AI contract says this never happens; keep the match moving anyway.
                m_aiRefusals.fetch_add(1, std::memory_order_relaxed);
                push.note = std::string("the AI action was refused (") + Match::ActionErrorCode(err) +
                    "); it ends its turn instead";
                Match::Action fallback;
                fallback.type = Match::ActionType::EndTurn;
                if (view.pickPending && !view.pick.empty())
                {
                    // A pending pick only accepts Choose: take the first option.
                    fallback.type = Match::ActionType::Choose;
                    fallback.source = view.pick[0].id;
                    push.note = std::string("the AI action was refused (") + Match::ActionErrorCode(err) +
                        "); it takes the first pick option instead";
                }
                err = ApplyLocked(e, 1, fallback);
                if (err != Match::ActionError::None)
                {
                    push.note += "; the fallback was refused too (" + std::string(Match::ActionErrorCode(err)) +
                        "), the match is aborted";
                    m.Abort();
                    e.abortWhy = InterruptCode(Interrupt::Error);
                }
            }
            ++e.aiActions;
            e.nextAiMs = nowMs + m_aiDelayMs;
            UpdateTimerLocked(e, nowMs);

            push.arenaRunId = e.arena ? e.params->arenaRunId : 0;
            push.loggedFirst = static_cast<uint32_t>(logFirst);
            push.logged = LoggedSince(e, logFirst);
            push.payload = PayloadLocked(e, e.sentEventSeq, true, &push.events);
            if (m.GetPhase() == Match::Phase::Over)
            {
                push.ended = true;
                push.end = EndOfLocked(entry.first, e);
                changed = true;
            }
            pushes.push_back(std::move(push));
        }
        if (changed)
            RecountLocked();
        return pushes;
    }

    bool MatchService::IsRunning(uint32_t guid) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        return it != m_matches.end() && it->second.match && it->second.match->GetPhase() != Match::Phase::Over;
    }

    bool MatchService::Has(uint32_t guid) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_matches.find(guid) != m_matches.end();
    }

    std::size_t MatchService::Count() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_matches.size();
    }

    std::vector<MatchInfo> MatchService::List() const
    {
        std::vector<MatchInfo> out;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& entry : m_matches)
            if (entry.second.match)
                out.push_back(InfoOf(entry.first, entry.second));
        std::sort(out.begin(), out.end(), [](MatchInfo const& a, MatchInfo const& b) { return a.matchId < b.matchId; });
        return out;
    }

    bool MatchService::Info(uint32_t guid, MatchInfo& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end() || !it->second.match)
            return false;
        out = InfoOf(guid, it->second);
        return true;
    }

    Match::Match* MatchService::MatchForTests(uint32_t guid)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        return it == m_matches.end() ? nullptr : it->second.match.get();
    }

    bool MatchService::AiCountersForTests(uint32_t guid, int& turn, int& actions) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_matches.find(guid);
        if (it == m_matches.end())
            return false;
        turn = it->second.aiTurn;
        actions = it->second.aiActions;
        return true;
    }

    uint32_t AiSeedFor(uint32_t matchSeed, uint32_t seq)
    {
        // A 32-bit mix (xorshift-multiply, the "lowbias32" finalizer) of both numbers.
        uint32_t x = matchSeed ^ (seq * 0x9E3779B9u + 0x7F4A7C15u);
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    void ParseMatchOpponents(std::string const& text, std::vector<MatchOpponent>& out, std::vector<std::string>& errors)
    {
        out.clear();
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t const comma = text.find(',', start);
            std::string const item = Trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            start = comma == std::string::npos ? text.size() + 1 : comma + 1;
            if (item.empty())
                continue;

            std::size_t const colon = item.find(':');
            if (colon == std::string::npos)
            {
                errors.push_back("'" + item + "' is not <creature entry>:<class>");
                continue;
            }
            std::string const entryText = Trim(item.substr(0, colon));
            std::string const classText = Trim(item.substr(colon + 1));
            unsigned long long entry = 0;
            bool digits = !entryText.empty() && entryText.size() <= 10;
            for (char c : entryText)
            {
                if (c < '0' || c > '9')
                {
                    digits = false;
                    break;
                }
                entry = entry * 10 + static_cast<unsigned long long>(c - '0');
            }
            if (!digits || entry == 0 || entry > 0xFFFFFFFFull)
            {
                errors.push_back("'" + item + "': the creature entry must be a number (1..4294967295)");
                continue;
            }
            std::string const deckClass = ParseDeckClass(classText);
            if (deckClass.empty())
            {
                errors.push_back("'" + item + "': the class must be one of the nine classes (druid, hunter, mage, paladin, "
                    "priest, rogue, shaman, warlock, warrior)");
                continue;
            }
            bool duplicate = false;
            for (MatchOpponent const& known : out)
                if (known.entry == static_cast<uint32_t>(entry))
                    duplicate = true;
            if (duplicate)
            {
                errors.push_back("'" + item + "': creature entry " + entryText + " is listed twice, the first one is used");
                continue;
            }
            MatchOpponent opponent;
            opponent.entry = static_cast<uint32_t>(entry);
            opponent.deckClass = deckClass;
            out.push_back(opponent);
        }
    }

    bool ExpandDeck(DeckCards const& cards, std::vector<std::string>& out)
    {
        out.clear();
        for (auto const& entry : cards)
        {
            if (entry.second == 0 || entry.second > Match::kMaxDeckCards)
                return false;
            for (uint32_t i = 0; i < entry.second; ++i)
                out.push_back(entry.first);
            if (out.size() > Match::kMaxDeckCards)
                return false;
        }
        return !out.empty();
    }

    std::string DeckNotReadyText(DeckMatchCheck const& check, std::string const& deckClass, std::string const& cardName)
    {
        if (check.status == DeckMatchCheck::Status::Missing)
            return "the deck does not exist any more";
        if (check.status != DeckMatchCheck::Status::Ok)
            return "your decks cannot be read right now";
        if (check.Ready())
            return std::string();
        std::string cls;
        for (std::size_t i = 0; i < deckClass.size(); ++i)
            cls.push_back(i == 0 ? deckClass[i] : static_cast<char>(std::tolower(static_cast<unsigned char>(deckClass[i]))));
        std::vector<std::string> reasons;
        if (check.state.size)
            reasons.push_back("fewer than 30 cards");
        if (check.state.owned)
            reasons.push_back("uses more copies of a card than you own");
        if (check.state.invalid)
            reasons.push_back("breaks a deck rule");
        if (check.state.locked)
            reasons.push_back("this character cannot play " + (cls.empty() ? std::string("its class") : cls) +
                " (it is not your class and not unlocked)");
        if (check.engine)
        {
            std::string const card = !cardName.empty() ? cardName : check.unsupportedCard;
            reasons.push_back(card.empty() ? std::string("the match engine is not ready")
                                           : card + " does not work in matches yet");
        }
        std::string text;
        for (std::size_t i = 0; i < reasons.size(); ++i)
            text += (i ? "; " : "") + reasons[i];
        return text;
    }
}
