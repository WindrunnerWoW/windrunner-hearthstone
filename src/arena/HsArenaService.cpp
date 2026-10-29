#include "HsArenaService.h"

#include <algorithm>
#include <random>
#include <utility>

namespace Hs
{
    namespace
    {
        bool Active(ArenaState state)
        {
            return state == ArenaState::ChoosingClass || state == ArenaState::Drafting || state == ArenaState::Ready ||
                state == ArenaState::InDuel || state == ArenaState::Paused;
        }

        bool Contains(std::vector<std::string> const& list, std::string const& id)
        {
            return std::find(list.begin(), list.end(), id) != list.end();
        }
    }

    char const* ArenaStateCode(ArenaState state)
    {
        switch (state)
        {
            case ArenaState::ChoosingClass: return "class";
            case ArenaState::Drafting: return "draft";
            case ArenaState::Ready: return "ready";
            case ArenaState::InDuel: return "duel";
            case ArenaState::Paused: return "paused";
            case ArenaState::Finished: return "finished";
            case ArenaState::Retired: return "retired";
            case ArenaState::Settled: return "settled";
            case ArenaState::Repair: return "repair";
        }
        return "repair";
    }

    bool ParseArenaState(std::string const& code, ArenaState& out)
    {
        for (ArenaState state : { ArenaState::ChoosingClass, ArenaState::Drafting, ArenaState::Ready,
                                  ArenaState::InDuel, ArenaState::Paused, ArenaState::Finished, ArenaState::Retired,
                                  ArenaState::Settled, ArenaState::Repair })
            if (code == ArenaStateCode(state))
            {
                out = state;
                return true;
            }
        return false;
    }

    std::vector<std::string> ArenaRun::Deck() const
    {
        std::vector<std::string> deck;
        for (std::size_t i = 1; i < picks.size(); ++i)
            if (!picks[i].chosen.empty())
                deck.push_back(picks[i].chosen);
        return deck;
    }

    int ArenaRun::OpenPick() const
    {
        if (picks.empty() || !picks.back().chosen.empty())
            return -1;
        return static_cast<int>(picks.size()) - 1;
    }

    ArenaSettlement DescribeArenaSettlement(ArenaRun const& run)
    {
        ArenaSettlement s;
        s.runId = run.id;
        s.accountId = run.accountId;
        s.wins = run.wins;
        s.retired = run.retired;
        s.prize = run.prize;
        s.coinGuid = run.endedGuid;
        s.specialChoicePending = run.prize.specialChoice;
        return s;
    }

    ArenaService::ArenaService(ArenaStorage& storage) : m_storage(storage)
    {
        std::random_device device;
        m_rng = MakeSeededRng((static_cast<uint64_t>(device()) << 32) | device());
    }

    void ArenaService::SetData(std::shared_ptr<CardCatalog const> catalog, std::shared_ptr<Match::CardDb const> db,
                               std::string const& datasetHash)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_catalog = std::move(catalog);
        m_db = std::move(db);
        m_datasetHash = datasetHash;
        m_pools.clear();
        if (m_catalog && m_db)
            for (std::string const& deckClass : DeckClasses())
                m_pools.emplace(deckClass, ArenaPool::FromCatalog(*m_catalog, *m_db, deckClass));
    }

    void ArenaService::SetRng(PackRng rng)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rng = std::move(rng);
    }

    ArenaOutcome ArenaService::Refuse(char const* why, ArenaRun const* run)
    {
        ArenaOutcome out;
        out.why = why;
        if (run)
        {
            out.run = *run;
            out.hasRun = true;
        }
        return out;
    }

    // An answer that repeats the stored state: nothing is written.
    static ArenaOutcome Replay(ArenaRun const& run)
    {
        ArenaOutcome out;
        out.ok = true;
        out.replay = true;
        out.run = run;
        out.hasRun = true;
        return out;
    }

    ArenaOutcome ArenaService::SaveLocked(ArenaRun const& run)
    {
        if (!m_storage.SaveRun(run))
            return Refuse("storage");
        ArenaOutcome out;
        out.ok = true;
        out.run = run;
        out.hasRun = true;
        return out;
    }

    bool ArenaService::CompatibleLocked(ArenaRun const& run) const
    {
        return run.terms.rulesVersion == static_cast<uint32_t>(Match::kRulesVersion) &&
            run.terms.datasetHash == m_datasetHash;
    }

    bool ArenaService::LoadLocked(uint32_t accountId, uint32_t runId, ArenaRun& run, ArenaOutcome& out)
    {
        if (!m_catalog || !m_db)
        {
            out = Refuse("data");
            return false;
        }
        bool found = false;
        ArenaRun latest;
        if (!m_storage.LoadLatestRun(accountId, found, latest))
        {
            out = Refuse("storage");
            return false;
        }
        if (!found)
        {
            out = Refuse("no_run");
            return false;
        }
        if (runId != 0 && latest.id != runId)
        {
            out = Refuse("stale", &latest);
            return false;
        }
        // A run whose rules or dataset changed is kept in Repair, never played on (an unconfirmed entry is not a
        // real run yet and is left alone).
        if (Active(latest.state) && latest.entryConfirmed && !CompatibleLocked(latest))
        {
            latest.repairFrom = latest.state;
            latest.state = ArenaState::Repair;
            ArenaOutcome saved = SaveLocked(latest);
            if (!saved.ok)
            {
                out = saved;
                return false;
            }
        }
        run = std::move(latest);
        return true;
    }

    ArenaPool const* ArenaService::PoolLocked(std::string const& deckClass, ArenaRun const& run, ArenaPool& scratch)
    {
        if (run.questCards.empty())
        {
            // No quest card in this run: the shared pool, as without quest cards.
            auto it = m_pools.find(deckClass);
            return it == m_pools.end() ? nullptr : &it->second;
        }
        ArenaQuestSet const owned = run.QuestSet();
        scratch = ArenaPool::FromCatalog(*m_catalog, *m_db, deckClass, &owned);
        return &scratch;
    }

    bool ArenaService::OfferLocked(std::string const& deckClass, ArenaRun const& run, std::vector<std::string>& out)
    {
        ArenaPool scratch;
        ArenaPool const* pool = PoolLocked(deckClass, run, scratch);
        return pool && ArenaOffer(*pool, run.terms.rarityWeights, m_rng, out);
    }

    // The common gate of the draft and duel steps: not in Repair, the entry paid.
    static bool Gate(ArenaRun const& run, ArenaOutcome& out)
    {
        if (run.state == ArenaState::Repair)
        {
            out.why = "repair";
            out.run = run;
            out.hasRun = true;
            return false;
        }
        if (!run.entryConfirmed)
        {
            out.why = "unpaid";
            out.run = run;
            out.hasRun = true;
            return false;
        }
        return true;
    }

    bool ArenaService::EntryAvailable(uint32_t accountId, ArenaPay pay, uint64_t now, uint32_t swapSeconds,
                                      std::string& why)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        why.clear();
        bool found = false;
        ArenaRun latest;
        if (!m_storage.LoadLatestRun(accountId, found, latest))
        {
            why = "storage";
            return false;
        }
        if (found && latest.Open())
        {
            why = latest.state == ArenaState::Settled ? "special_pending" : "open_run";
            return false;
        }
        if (ArenaPayUsesDailySlot(pay))
        {
            bool used = false;
            if (!m_storage.IsSlotUsed(accountId, ArenaPeriod(now, swapSeconds), used))
            {
                why = "storage";
                return false;
            }
            if (used)
            {
                why = "daily_used";
                return false;
            }
        }
        return true;
    }

    ArenaOutcome ArenaService::Get(uint32_t accountId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, 0, run, out))
            return out;
        out.ok = true;
        out.run = run;
        out.hasRun = true;
        return out;
    }

    ArenaOutcome ArenaService::Start(ArenaStartRequest const& request)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_catalog || !m_db)
            return Refuse("data");
        bool found = false;
        ArenaRun latest;
        if (!m_storage.LoadLatestRun(request.accountId, found, latest))
            return Refuse("storage");
        if (found && latest.Open())
        {
            // The same entry again: its debit is still owed, whatever payment the retry asked for.
            if (!latest.entryConfirmed)
            {
                ArenaOutcome out;
                out.ok = true;
                out.replay = true;
                out.needsDebit = true;
                out.run = latest;
                out.hasRun = true;
                return out;
            }
            return Refuse(latest.state == ArenaState::Settled ? "special_pending" : "open_run", &latest);
        }

        bool const slot = ArenaPayUsesDailySlot(request.pay);
        uint64_t const period = ArenaPeriod(request.now, request.swapSeconds);
        if (slot)
        {
            bool used = false;
            if (!m_storage.IsSlotUsed(request.accountId, period, used))
                return Refuse("storage");
            if (used)
                return Refuse("daily_used");
        }

        ArenaRun run;
        run.accountId = request.accountId;
        run.state = ArenaState::ChoosingClass;
        run.terms = MakeArenaTerms(request.settings, request.pay, m_datasetHash);
        run.payingGuid = request.payingGuid;
        run.slotPeriod = slot ? period : 0;
        run.entryConfirmed = !slot; // a free run owes no debit
        run.created = request.now;
        if (request.collection)
            run.questCards = ArenaOwnedQuestCards(*m_catalog, *request.collection); // the entry snapshot
        ArenaPick classes;
        classes.offer = ArenaClassOffer(m_rng);
        run.picks.push_back(std::move(classes));
        switch (m_storage.CreateRun(run))
        {
            case ArenaStoreResult::Ok: break;
            case ArenaStoreResult::OpenRun: return Refuse("open_run");
            case ArenaStoreResult::SlotTaken: return Refuse("daily_used");
            case ArenaStoreResult::Failed: return Refuse("storage");
        }
        ArenaOutcome out;
        out.ok = true;
        out.needsDebit = slot;
        out.run = std::move(run);
        out.hasRun = true;
        return out;
    }

    ArenaOutcome ArenaService::ConfirmEntry(uint32_t accountId, uint32_t runId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
            return out;
        if (run.entryConfirmed)
            return Replay(run);
        run.entryConfirmed = true;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::AbortEntry(uint32_t accountId, uint32_t runId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
        {
            // Already aborted (or never created): nothing to undo.
            if (out.why == "no_run" || out.why == "stale")
            {
                ArenaOutcome done;
                done.ok = true;
                done.replay = true;
                return done;
            }
            return out;
        }
        if (run.entryConfirmed)
            return Refuse("bad_state", &run);
        if (!m_storage.DeleteUnconfirmedRun(accountId, runId))
            return Refuse("storage");
        ArenaOutcome done;
        done.ok = true;
        return done;
    }

    ArenaOutcome ArenaService::ChooseClass(uint32_t accountId, uint32_t runId, std::string const& deckClass)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out) || !Gate(run, out))
            return out;
        if (run.state != ArenaState::ChoosingClass)
        {
            if (run.picks.size() > 0 && run.picks[0].chosen == deckClass)
                return Replay(run);
            return Refuse("bad_state", &run);
        }
        if (!Contains(run.picks[0].offer, deckClass))
            return Refuse("not_offered", &run);
        std::vector<std::string> offer;
        if (!OfferLocked(deckClass, run, offer))
            return Refuse("data", &run);
        run.picks[0].chosen = deckClass;
        run.deckClass = deckClass;
        ArenaPick first;
        first.offer = std::move(offer);
        run.picks.push_back(std::move(first));
        run.state = ArenaState::Drafting;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::Pick(uint32_t accountId, uint32_t runId, uint32_t pickNo, std::string const& cardId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out) || !Gate(run, out))
            return out;
        if (pickNo < 1 || pickNo > kArenaDraftPicks)
            return Refuse("stale", &run);
        // A pick that was applied already: the same card is a replay, another one is refused.
        if (pickNo < run.picks.size() && !run.picks[pickNo].chosen.empty())
        {
            if (run.picks[pickNo].chosen == cardId)
                return Replay(run);
            return Refuse("picked", &run);
        }
        if (run.state != ArenaState::Drafting)
            return Refuse("bad_state", &run);
        if (run.OpenPick() != static_cast<int>(pickNo))
            return Refuse("stale", &run);
        if (!Contains(run.picks[pickNo].offer, cardId))
            return Refuse("not_offered", &run);
        run.picks[pickNo].chosen = cardId;
        if (pickNo == kArenaDraftPicks)
        {
            ArenaQuestSet const owned = run.QuestSet();
            if (ValidateArenaDeck(*m_catalog, *m_db, run.deckClass, run.Deck(), &owned) != ArenaDeckWhy::None)
                return Refuse("deck", &run);
            run.state = ArenaState::Ready;
            return SaveLocked(run);
        }
        ArenaPick next;
        if (!OfferLocked(run.deckClass, run, next.offer))
            return Refuse("data", &run);
        run.picks.push_back(std::move(next));
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::BeginDuel(uint32_t accountId, uint32_t runId, uint32_t guid)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out) || !Gate(run, out))
            return out;
        if (run.state == ArenaState::InDuel)
            return Replay(run);
        if (run.state != ArenaState::Ready)
            return Refuse("bad_state", &run);
        ++run.matchNo;
        run.controllerGuid = guid;
        run.state = ArenaState::InDuel;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::PauseDuel(uint32_t accountId, uint32_t runId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out) || !Gate(run, out))
            return out;
        if (run.state == ArenaState::Paused)
            return Replay(run);
        if (run.state != ArenaState::InDuel)
            return Refuse("bad_state", &run);
        run.state = ArenaState::Paused;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::ResumeDuel(uint32_t accountId, uint32_t runId, uint32_t guid)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out) || !Gate(run, out))
            return out;
        if (run.state == ArenaState::InDuel)
            return Replay(run);
        if (run.state != ArenaState::Paused)
            return Refuse("bad_state", &run);
        run.controllerGuid = guid;
        run.state = ArenaState::InDuel;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::EndLocked(ArenaRun run, ArenaState state, uint32_t guid, bool specialSetsComplete,
                                         uint64_t now)
    {
        run.state = state;
        run.retired = state == ArenaState::Retired;
        run.endedGuid = guid;
        run.ended = now;
        run.prize = PrizeFor(run.wins, run.terms, specialSetsComplete);
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::RecordResult(uint32_t accountId, uint32_t runId, uint32_t matchNo,
                                            ArenaDuelResult result, uint32_t guid, bool specialSetsComplete,
                                            uint64_t now)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out) || !Gate(run, out))
            return out;
        // A result that was recorded already (the matches of a run are strictly sequential).
        if (matchNo >= 1 && matchNo <= run.resultNo)
            return Replay(run);
        if (run.state != ArenaState::InDuel)
            return Refuse("bad_state", &run);
        if (matchNo != run.matchNo)
            return Refuse("stale", &run);
        run.resultNo = matchNo;
        if (result == ArenaDuelResult::Loss)
            ++run.losses;
        else
        {
            ++run.wins;
            if (result == ArenaDuelResult::Draw)
                ++run.draws;
        }
        if (result == ArenaDuelResult::Loss || run.wins >= kArenaMaxWins)
            return EndLocked(std::move(run), ArenaState::Finished, guid, specialSetsComplete, now);
        run.state = ArenaState::Ready;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::Retire(uint32_t accountId, uint32_t runId, uint32_t guid, bool specialSetsComplete,
                                      uint64_t now)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
            return out;
        if (run.retired)
            return Replay(run);
        if (!run.entryConfirmed)
            return Refuse("unpaid", &run);
        if (!Active(run.state) && run.state != ArenaState::Repair)
            return Refuse("bad_state", &run);
        return EndLocked(std::move(run), ArenaState::Retired, guid, specialSetsComplete, now);
    }

    ArenaOutcome ArenaService::Settle(uint32_t accountId, uint32_t runId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
            return out;
        if (run.state == ArenaState::Settled)
        {
            ArenaOutcome done = Replay(run);
            done.settlement = DescribeArenaSettlement(run);
            return done;
        }
        if (run.state != ArenaState::Finished && run.state != ArenaState::Retired)
            return Refuse("bad_state", &run);
        run.state = ArenaState::Settled;
        run.specialPending = run.prize.specialChoice;
        ArenaOutcome done = SaveLocked(run);
        if (done.ok)
            done.settlement = DescribeArenaSettlement(run);
        return done;
    }

    ArenaOutcome ArenaService::ClaimSpecial(uint32_t accountId, uint32_t runId, std::string const& cardId,
                                            Collection const& collection)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
            return out;
        if (run.state != ArenaState::Settled)
            return Refuse("bad_state", &run);
        if (!run.specialPending)
        {
            if (!run.specialCard.empty() && run.specialCard == cardId)
            {
                ArenaOutcome done = Replay(run);
                done.grantCopies = run.specialCopies;
                return done;
            }
            return Refuse("bad_state", &run);
        }
        ArenaSpecialCard const* card = FindArenaSpecialCard(cardId);
        if (!card)
            return Refuse("not_special", &run);
        if (ArenaSpecialSetsComplete(collection))
        {
            // Every set became complete since the run ended: the extra pack replaces the choice.
            run.specialPending = false;
            run.prize.specialChoice = false;
            ++run.prize.packs;
            ArenaOutcome done = SaveLocked(run);
            done.convertedToPack = done.ok;
            return done;
        }
        uint32_t const missing = ArenaSpecialMissingCopies(*card, collection);
        if (missing == 0)
            return Refuse("not_missing", &run);
        run.specialPending = false;
        run.specialCard = card->id;
        run.specialCopies = missing;
        ArenaOutcome done = SaveLocked(run);
        done.grantCopies = done.ok ? missing : 0;
        return done;
    }

    ArenaOutcome ArenaService::RepairRebind(uint32_t accountId, uint32_t runId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
            return out;
        if (run.state != ArenaState::Repair)
            return Refuse("bad_state", &run);
        run.terms.rulesVersion = static_cast<uint32_t>(Match::kRulesVersion);
        run.terms.datasetHash = m_datasetHash;
        run.state = run.repairFrom == ArenaState::InDuel ? ArenaState::Paused : run.repairFrom;
        return SaveLocked(run);
    }

    ArenaOutcome ArenaService::MarkRepair(uint32_t accountId, uint32_t runId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ArenaRun run;
        ArenaOutcome out;
        if (!LoadLocked(accountId, runId, run, out))
            return out;
        if (run.state == ArenaState::Repair)
            return Replay(run);
        if (!Active(run.state))
            return Refuse("bad_state", &run);
        if (!run.entryConfirmed)
            return Refuse("unpaid", &run);
        run.repairFrom = run.state;
        run.state = ArenaState::Repair;
        return SaveLocked(run);
    }

    // ---- memory storage ------------------------------------------------------

    bool ArenaMemoryStorage::LoadLatestRun(uint32_t accountId, bool& found, ArenaRun& out)
    {
        if (failReads)
            return false;
        found = false;
        for (auto const& entry : runs)
            if (entry.second.accountId == accountId && (!found || entry.first > out.id))
            {
                out = entry.second;
                found = true;
            }
        return true;
    }

    bool ArenaMemoryStorage::IsSlotUsed(uint32_t accountId, uint64_t period, bool& used)
    {
        if (failReads)
            return false;
        used = slots.count({ accountId, period }) != 0;
        return true;
    }

    ArenaStoreResult ArenaMemoryStorage::CreateRun(ArenaRun& run)
    {
        if (failWrites)
            return ArenaStoreResult::Failed;
        for (auto const& entry : runs)
            if (entry.second.accountId == run.accountId && entry.second.Open())
                return ArenaStoreResult::OpenRun;
        bool const slot = ArenaPayUsesDailySlot(run.terms.pay);
        if (slot && slots.count({ run.accountId, run.slotPeriod }))
            return ArenaStoreResult::SlotTaken;
        run.id = nextId++;
        runs[run.id] = run;
        if (slot)
            slots.insert({ run.accountId, run.slotPeriod });
        return ArenaStoreResult::Ok;
    }

    bool ArenaMemoryStorage::SaveRun(ArenaRun const& run)
    {
        if (failWrites)
            return false;
        auto it = runs.find(run.id);
        if (it == runs.end())
            return false;
        it->second = run;
        return true;
    }

    bool ArenaMemoryStorage::DeleteUnconfirmedRun(uint32_t accountId, uint32_t runId)
    {
        if (failWrites)
            return false;
        auto it = runs.find(runId);
        if (it == runs.end())
            return true;
        if (it->second.accountId != accountId || it->second.entryConfirmed)
            return false;
        if (ArenaPayUsesDailySlot(it->second.terms.pay))
            slots.erase({ accountId, it->second.slotPeriod });
        runs.erase(it);
        return true;
    }
}
