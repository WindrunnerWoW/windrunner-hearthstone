/*
 * Draft arena settlement: the order of the steps that pay a finished or retired run exactly once,
 * and that hand out the special card of a ten-win run, so that the order and its failure handling can be unit-tested
 * with fakes. The core and database calls are the ports; src/HsArenaSettle.cpp implements them.
 *
 * SETTLEMENT (SettleArenaRun). One character database transaction contains
 *     the pack stock change and its ledger row   (CollectionService::GrantPacksVia, under the service mutex)
 *     the run's move to Settled                   (ArenaService::Settle, its SaveRun joined to the transaction)
 *     the payout row  (run_id, 'run')             the idempotency key: a second settlement of the run fails this
 *                                                 insert, so the whole transaction rolls back and nothing is granted
 *     the pending coin delivery row               (only when the prize has coins)
 * so a crash leaves either nothing (the run stays Finished / Retired and the next pass settles it) or everything.
 * A commit that reports an error is ambiguous; the payout row is read back (Landed). The coins are delivered
 * afterwards from the pending row (src/HsArenaSettle.cpp), which is cleared only together with the delivery.
 *
 * SPECIAL CARD (ClaimArenaSpecial). The claim is recorded in the run first (ArenaService::ClaimSpecial: the card and
 * the exact copies, a replay of the same claim grants nothing new), then the card is granted by
 * CollectionService::GrantVia together with the milestone row (account, card) in one transaction. A crash between the
 * two leaves a claimed run without a milestone row; FinishArenaClaim (the startup/periodic recovery, and every replay
 * of the claim) grants it then. The milestone row is the delivery marker, so the card is granted exactly once. When
 * every set is complete already the choice becomes one pack instead: payout row (run_id, 'convert') + pack stock in one
 * transaction, owed again after a crash while run.prize.packs exceeds the packs of the 'run' payout.
 *
 * PURE C++ (the arena service, the collection service and their rules; no core headers).
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_PAYOUT_H
#define MOD_HEARTHSTONE_HS_ARENA_PAYOUT_H

#include "../collection/HsCollectionService.h"
#include "HsArenaService.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Hs
{
    // Payout kinds (hearthstone_arena_payout.kind).
    constexpr char const* kArenaPayoutRun = "run";
    constexpr char const* kArenaPayoutConvert = "convert";

    // A 1.12 mail carries one item; a stack of Hearthstone Coin items is 250 at most.
    constexpr uint32_t kArenaMailCoinStack = 250;

    // ---- coins ------------------------------------------------------------------------------------------------

    // Where the coins of a run go: as many as fit into the bags (bagRoom), the rest in mail chunks of at most
    // kArenaMailCoinStack. bagRoom is 0 for a character that is not in the world.
    struct ArenaCoinPlan
    {
        uint32_t bags = 0;
        std::vector<uint32_t> mail;
    };
    ArenaCoinPlan PlanArenaCoins(uint32_t coins, uint32_t bagRoom);

    // The character that receives the coins: the one that ended the run; for a row without it the one that played
    // last, then the one that paid. 0: none known.
    uint32_t ArenaCoinRecipient(ArenaRun const& run);

    // ---- special cards ----------------------------------------------------------------------------------------

    // card id -> run that claimed it (hearthstone_arena_milestone).
    using ArenaMilestones = std::map<std::string, uint32_t>;
    // The collection as the claim sees it: a card that an arena milestone claimed counts as owned in full, so it is
    // never offered or granted a second time (also not after a GM removed its copies).
    Collection ArenaEffectiveCollection(Collection const& collection, ArenaMilestones const& claimed);
    // The cards a pending choice may take now, in ArenaSpecialCards() order.
    std::vector<ArenaSpecialMissing> ArenaSpecialOptions(Collection const& collection, ArenaMilestones const& claimed);

    // ---- ports --------------------------------------------------------------------------------------------------

    class ArenaPayoutPorts
    {
    public:
        virtual ~ArenaPayoutPorts() = default;
        // The character database transaction of this thread (CommitTransactionDirect for Commit).
        virtual bool Begin() = 0;
        virtual bool Commit() = 0;
        virtual void Rollback() = 0;
        // ArenaService::Settle with its storage write joined to the open transaction.
        virtual ArenaOutcome SettleJoined(uint32_t accountId, uint32_t runId) = 0;
        // Queue rows into the open transaction (false: not queued, the caller rolls back).
        virtual bool QueuePackChange(uint32_t accountId, PackStockChange const& change) = 0;
        virtual bool QueuePayout(uint32_t runId, uint32_t accountId, char const* kind, uint32_t packs, uint32_t coins,
                                 uint32_t coinGuid) = 0;
        virtual bool QueueCoinDelivery(uint32_t runId, uint32_t accountId, uint32_t guid, uint32_t coins) = 0;
        virtual bool QueueCardMutation(uint32_t accountId, Mutation const& mutation) = 0;
        virtual bool QueueMilestone(uint32_t accountId, std::string const& cardId, uint32_t runId) = 0;
        // Reads (false: database failure).
        virtual bool LoadPayout(uint32_t runId, char const* kind, bool& found, uint32_t& packs) = 0;
        virtual bool LoadMilestones(uint32_t accountId, ArenaMilestones& out) = 0;
        // The settled-run fields a recovery needs (id, accountId, state, specialPending, specialCard, specialCopies,
        // prize), of ANY run of the account, not only the newest. found = false for an unknown run.
        virtual bool LoadRunForClaim(uint32_t runId, bool& found, ArenaRun& out) = 0;
    };

    // ---- settlement --------------------------------------------------------------------------------------------

    struct ArenaSettleResult
    {
        bool ok = false;
        // when !ok: data / no_run / storage / stale (not the account's current run), bad_state (the run has not
        // ended), overflow (the pack stock would exceed its limit), unavailable (collections), no_recipient
        // (coins but no character to receive them)
        std::string why;
        bool replay = false;           // ok: the run was settled before; nothing was granted now
        bool landedAfterError = false; // ok: the commit reported an error that turned out to be applied
        ArenaRun run;                  // the run after the call (when hasRun)
        bool hasRun = false;
        uint32_t packs = 0;            // granted now
        uint32_t coins = 0;            // queued for delivery now
        uint32_t coinGuid = 0;
        uint32_t stockAfter = 0;       // the account's pack stock after the grant (when packs > 0)
        bool specialPending = false;   // the player now chooses a special card
    };

    // Finished / Retired -> Settled with the pack grant, the payout key and the pending coins in one transaction.
    // Idempotent: a settled run answers ok + replay. runId 0: the account's current run.
    ArenaSettleResult SettleArenaRun(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                     uint32_t accountId, uint32_t runId);

    // ---- special card claim ---------------------------------------------------------------------------------------

    struct ArenaClaimResult
    {
        bool ok = false;
        // when !ok: the ArenaService codes (no_run, stale, bad_state, not_special, not_missing, ...) and storage,
        // unavailable (collections), bad_card (the grant was rejected)
        std::string why;
        bool replay = false;           // the claim was recorded (and delivered) before; nothing new was granted
        bool convertedToPack = false;  // the choice became one more pack
        bool hasRun = false;
        ArenaRun run;
        std::string card;              // the card granted (empty with convertedToPack)
        uint32_t copies = 0;           // copies of card that were granted now
        uint32_t packs = 0;            // packs granted now (convertedToPack)
        uint32_t stockAfter = 0;
        uint32_t versionAfter = 0;     // the collection version after the grant (copies > 0)
        bool delivered = false;        // something was granted now
    };

    // The player's choice. A repeat of the same claim is a replay and finishes an interrupted delivery.
    ArenaClaimResult ClaimArenaSpecial(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                       uint32_t accountId, uint32_t runId, std::string const& cardId);
    // Recovery: finishes what a recorded claim left (the special card without its milestone row, or the extra pack of
    // a converted choice) for the run runId (any run of the account, found through ports.LoadRunForClaim). ok with
    // delivered = false when nothing was owed.
    ArenaClaimResult FinishArenaClaim(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                      uint32_t accountId, uint32_t runId);
}

#endif
