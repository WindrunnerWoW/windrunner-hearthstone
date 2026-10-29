/*
 * Draft arena run service: the state machine of one arena run per account, from the entry
 * to the settlement, over an injected ArenaStorage.
 *
 *   ChoosingClass -> Drafting -> Ready -> InDuel <-> Paused -> Finished | Retired -> Settled
 *   any active state -> Repair (rules version or card dataset changed) -> back, or Retired
 *
 * PURE C++ (standard library, the arena rules and the match engine's CardDb, no core headers). The service decides
 * and records; it returns what the glue has to do (debit the entry, grant the packs, deliver the coins, grant the
 * special card) and never calls the game, the wallet or the collection.
 *
 * Entry (the order the glue follows): Start creates the run, offers its three classes and, for a paid entry,
 * reserves the account's daily slot, all in one storage write. The glue then debits the paying character and calls
 * ConfirmEntry, or AbortEntry on any failure (the run and the slot are released exactly once). A retried Start
 * returns the same unconfirmed run (replay, needsDebit), so a changed payment method never charges twice or
 * creates a second run. A Free run (arena introduction quest, GM start) is confirmed at once and never touches
 * the daily slot.
 *
 * Every mutation loads the run, changes a copy and saves it; the in-memory result is returned only when the save
 * succeeded (why "storage" otherwise, nothing changed). Offers are saved before they are returned, a pick accepts
 * only an id of its own offer, and a retry of an applied step answers with the stored state and replay = true.
 *
 * Threading: every public method takes the service mutex, which is a LEAF lock: the service calls only its
 * storage while holding it. Callers must not hold a lock that the storage takes.
 *
 * Refusal codes (ArenaOutcome::why): data (no card data), no_run, stale (another run id), open_run,
 * special_pending, daily_used, bad_state, unpaid, repair, bad_class, not_offered, picked, bad_card, not_special,
 * not_missing, deck, storage.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_SERVICE_H
#define MOD_HEARTHSTONE_HS_ARENA_SERVICE_H

#include "HsArenaRules.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Hs
{
    enum class ArenaState
    {
        ChoosingClass, // the class offer is open
        Drafting,      // a card offer is open
        Ready,         // 30 cards drafted, the next duel can start
        InDuel,
        Paused,        // the duel is saved; resume at the host
        Finished,      // a loss or ten wins ended the run; the prize is not settled yet
        Retired,       // the player retired; the prize is not settled yet
        Settled,       // packs and coins settled (a special-card choice may still be pending)
        Repair         // rules or dataset changed under the run: kept, never scored, needs an administrator
    };
    char const* ArenaStateCode(ArenaState state); // class draft ready duel paused finished retired settled repair
    bool ParseArenaState(std::string const& code, ArenaState& out);

    // One offer and its answer. Pick 0 is the class offer (the ids are class names).
    struct ArenaPick
    {
        std::vector<std::string> offer;
        std::string chosen; // "" while the offer is open
    };

    // One run (hearthstone_arena_run with its picks), as storage keeps it.
    struct ArenaRun
    {
        uint32_t id = 0;
        uint32_t accountId = 0;
        ArenaState state = ArenaState::ChoosingClass;
        ArenaState repairFrom = ArenaState::ChoosingClass; // the state before Repair
        ArenaTerms terms;
        uint32_t payingGuid = 0;     // the character that pays (or started a free run)
        uint64_t slotPeriod = 0;     // the daily period a paid entry reserves
        bool entryConfirmed = false; // false: the entry debit is still owed (paid runs only)
        // The quest (Naxxramas) cards the account owned at entry, sorted (ArenaOwnedQuestCards). Fixed at entry:
        // later grants never change the run's pool, its offers, its deck check or its AI opponents. Empty: none.
        std::vector<std::string> questCards;
        std::string deckClass;       // "" until the class is chosen
        uint32_t wins = 0;           // draws count as wins
        uint32_t draws = 0;          // how many of the wins were draws
        uint32_t losses = 0;
        uint32_t matchNo = 0;        // duels started
        uint32_t resultNo = 0;       // the newest duel whose result is recorded
        uint32_t controllerGuid = 0; // the character that started or resumed the current duel
        uint32_t endedGuid = 0;      // the character that ended the run (receives the coins)
        bool retired = false;        // the run ended by retirement (Finished and Retired both settle as Settled)
        uint64_t created = 0;
        uint64_t ended = 0;
        std::vector<ArenaPick> picks; // picks[0] class offer, picks[1..30] card offers
        ArenaPrize prize;            // set when the run ends
        bool specialPending = false; // settled, but the special-card choice is still open
        std::string specialCard;     // the claimed card
        uint32_t specialCopies = 0;  // the copies the claim grants

        // Unsettled, or settled with a special-card choice pending: blocks a new run.
        bool Open() const { return state != ArenaState::Settled || specialPending; }
        // The drafted cards so far, one entry per copy, in pick order.
        std::vector<std::string> Deck() const;
        // The pick number (0 = class) of the open offer, or -1.
        int OpenPick() const;
        // questCards as the set the rules take (empty: no quest card).
        ArenaQuestSet QuestSet() const { return ArenaQuestSet(questCards.begin(), questCards.end()); }
    };

    enum class ArenaStoreResult { Ok, Failed, OpenRun, SlotTaken };

    class ArenaStorage
    {
    public:
        virtual ~ArenaStorage() = default;
        // The account's newest run (highest id) in any state, if any: the open run when there is one, since a
        // new run can only start once the previous one is not Open(). False on a database failure.
        virtual bool LoadLatestRun(uint32_t accountId, bool& found, ArenaRun& out) = 0;
        // Whether a paid entry already used the daily period. False on a database failure.
        virtual bool IsSlotUsed(uint32_t accountId, uint64_t period, bool& used) = 0;
        // Inserts the run (storage assigns run.id) and, when ArenaPayUsesDailySlot(run.terms.pay), the slot
        // (accountId, run.slotPeriod), in ONE transaction. OpenRun: the account already has an open run (unique
        // in storage); SlotTaken: the slot is used. Nothing is written unless Ok.
        virtual ArenaStoreResult CreateRun(ArenaRun& run) = 0;
        // Replaces the stored run and its picks in one transaction. True only if it committed.
        virtual bool SaveRun(ArenaRun const& run) = 0;
        // Deletes a run that never confirmed its entry together with its slot. True only if it committed
        // (deleting a missing run is true).
        virtual bool DeleteUnconfirmedRun(uint32_t accountId, uint32_t runId) = 0;
    };

    enum class ArenaDuelResult { Win, Draw, Loss };

    // What the glue grants for a finished or retired run.
    struct ArenaSettlement
    {
        uint32_t runId = 0;
        uint32_t accountId = 0;
        uint32_t wins = 0;
        bool retired = false;
        ArenaPrize prize;
        uint32_t coinGuid = 0;             // the character that ended the run: coins go to its bags (mail when full)
        bool specialChoicePending = false; // the player now chooses one missing special card (ClaimSpecial)
    };
    ArenaSettlement DescribeArenaSettlement(ArenaRun const& run);

    struct ArenaOutcome
    {
        bool ok = false;
        std::string why;              // refusal code when !ok
        bool replay = false;          // the step was already applied; run is the stored state
        bool hasRun = false;          // run is filled in (always when ok; also on open_run, special_pending, repair)
        ArenaRun run;                 // the run after the call
        bool needsDebit = false;      // Start: the glue must debit run.terms.payAmount from run.payingGuid
        ArenaSettlement settlement;   // Settle
        uint32_t grantCopies = 0;     // ClaimSpecial: copies of run.specialCard to grant (0 with convertedToPack)
        bool convertedToPack = false; // ClaimSpecial: every set was complete already; one more pack replaces the card
    };

    struct ArenaStartRequest
    {
        uint32_t accountId = 0;
        ArenaPay pay = ArenaPay::Free;
        uint32_t payingGuid = 0;
        ArenaSettings settings;          // the sanitized current settings; the run snapshots them
        uint64_t now = 0;                // unix seconds
        uint32_t swapSeconds = 4 * 3600; // Hearthstone.Trader.SwapTime
        // The account's collection now: the run snapshots its owned quest cards (nullptr: none owned).
        Collection const* collection = nullptr;
    };

    class ArenaService
    {
    public:
        explicit ArenaService(ArenaStorage& storage);

        // The card data the drafts use and the dataset a run must have been created under. Until set, every
        // call refuses with "data". The pools of the nine classes are built here.
        void SetData(std::shared_ptr<CardCatalog const> catalog, std::shared_ptr<Match::CardDb const> db,
                     std::string const& datasetHash);
        // Replaces the generator of the offers (default: std::mt19937_64 seeded from std::random_device).
        void SetRng(PackRng rng);

        // Whether a new run with this payment could start now. false: why is open_run, special_pending (blocking
        // run), daily_used (a paid entry only; Free never is) or storage.
        bool EntryAvailable(uint32_t accountId, ArenaPay pay, uint64_t now, uint32_t swapSeconds, std::string& why);

        // The account's newest run (no_run without one); run.Open() tells whether it is still going. Moves an
        // active run of another rules version or dataset to Repair.
        ArenaOutcome Get(uint32_t accountId);

        ArenaOutcome Start(ArenaStartRequest const& request);
        ArenaOutcome ConfirmEntry(uint32_t accountId, uint32_t runId);
        ArenaOutcome AbortEntry(uint32_t accountId, uint32_t runId);

        ArenaOutcome ChooseClass(uint32_t accountId, uint32_t runId, std::string const& deckClass);
        // pickNo: 1..30, the number of the offer the id answers.
        ArenaOutcome Pick(uint32_t accountId, uint32_t runId, uint32_t pickNo, std::string const& cardId);

        // Ready -> InDuel, matchNo + 1. A second call while InDuel is a replay.
        ArenaOutcome BeginDuel(uint32_t accountId, uint32_t runId, uint32_t guid);
        ArenaOutcome PauseDuel(uint32_t accountId, uint32_t runId);
        ArenaOutcome ResumeDuel(uint32_t accountId, uint32_t runId, uint32_t guid);
        // The result of duel matchNo. Win and Draw count as a win; ten wins or a loss end the run (Finished, the
        // prize is fixed here), otherwise the run is Ready again. specialSetsComplete: PrizeFor's argument.
        ArenaOutcome RecordResult(uint32_t accountId, uint32_t runId, uint32_t matchNo, ArenaDuelResult result,
                                  uint32_t guid, bool specialSetsComplete, uint64_t now);
        // Explicit retirement from any active state or Repair; pays the prize of the wins so far.
        ArenaOutcome Retire(uint32_t accountId, uint32_t runId, uint32_t guid, bool specialSetsComplete, uint64_t now);

        // Finished or Retired -> Settled, after the glue granted ArenaOutcome::settlement (packs keyed by run id).
        ArenaOutcome Settle(uint32_t accountId, uint32_t runId);
        // Settled with a pending choice: the player takes the missing copies of cardId (a special card whose set
        // is incomplete in collection). A repeat of the same claim is a replay. When every set is already
        // complete the choice becomes one more pack instead (convertedToPack).
        ArenaOutcome ClaimSpecial(uint32_t accountId, uint32_t runId, std::string const& cardId,
                                  Collection const& collection);

        // Repair -> the state it came from (a duel becomes Paused), after an administrator checked the run;
        // the run now carries the current rules version and dataset hash.
        ArenaOutcome RepairRebind(uint32_t accountId, uint32_t runId);
        // A confirmed active run -> Repair, for a saved duel that cannot be restored (another rules version or
        // dataset than its log was recorded with, or a log the engine refuses). The run is kept, never scored;
        // a repeat is a replay.
        ArenaOutcome MarkRepair(uint32_t accountId, uint32_t runId);

    private:
        // All require m_mutex.
        // Loads the newest run and checks the id (0: any) and the data; on failure out is the refusal.
        bool LoadLocked(uint32_t accountId, uint32_t runId, ArenaRun& run, ArenaOutcome& out);
        bool CompatibleLocked(ArenaRun const& run) const;
        ArenaOutcome SaveLocked(ArenaRun const& run);
        static ArenaOutcome Refuse(char const* why, ArenaRun const* run = nullptr);
        bool OfferLocked(std::string const& deckClass, ArenaRun const& run, std::vector<std::string>& out);
        // The pool of a run's class: the shared pool of SetData when the run has no quest cards, else a pool built
        // for the run's snapshot in scratch. nullptr when the class has no pool.
        ArenaPool const* PoolLocked(std::string const& deckClass, ArenaRun const& run, ArenaPool& scratch);
        ArenaOutcome EndLocked(ArenaRun run, ArenaState state, uint32_t guid, bool specialSetsComplete, uint64_t now);

        ArenaStorage& m_storage;
        mutable std::mutex m_mutex;
        std::shared_ptr<CardCatalog const> m_catalog;
        std::shared_ptr<Match::CardDb const> m_db;
        std::string m_datasetHash;
        std::map<std::string, ArenaPool> m_pools; // deck class -> pool
        PackRng m_rng;
    };

    // Storage in memory, for tests and development. Not thread safe on its own (the service serializes access).
    // The failure switches simulate a database error on every read / write while set.
    class ArenaMemoryStorage : public ArenaStorage
    {
    public:
        bool LoadLatestRun(uint32_t accountId, bool& found, ArenaRun& out) override;
        bool IsSlotUsed(uint32_t accountId, uint64_t period, bool& used) override;
        ArenaStoreResult CreateRun(ArenaRun& run) override;
        bool SaveRun(ArenaRun const& run) override;
        bool DeleteUnconfirmedRun(uint32_t accountId, uint32_t runId) override;

        bool failReads = false;
        bool failWrites = false;
        std::map<uint32_t, ArenaRun> runs;             // by run id
        std::set<std::pair<uint32_t, uint64_t>> slots; // (account, period) of the paid entries
        uint32_t nextId = 1;
    };
}

#endif
