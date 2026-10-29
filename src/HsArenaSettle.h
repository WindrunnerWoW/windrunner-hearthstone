/*
 * mod-hearthstone: settlement of the Draft Arena. The order of the steps and their failure
 * handling are pure C++ in src/arena/HsArenaPayout.*; this file connects them to the core and the database:
 *   - a run that ended (a loss, the tenth win, retirement) is settled exactly once: packs into the account's pack
 *     stock, the run Settled, the payout key and the pending coin row, all in one character database transaction;
 *   - the coins are delivered afterwards from the pending row to the bags of the character that ended the run
 *     (mail when the bags are full or the character is offline); the row is cleared together with the delivery;
 *   - the special card of a ten-win run is chosen with HsArenaClaimSpecial and granted once;
 *   - HsArenaSettleUpdate (called from HsEconomyUpdate on the world thread) recovers whatever a crash left: runs that
 *     ended but are not settled, claims without their card, pending coins. It runs once as soon as the arena is
 *     ready, then every 30 seconds while something might be pending.
 *
 * Threading. Everything here runs under ONE mutex of its own (a leaf below the arena glue mutex: HsArenaSettleRun is
 * called by the arena glue while it holds its mutex, and this file never calls back into the arena glue except the
 * state read HsArenaGet at the start of the update, made without any lock). Item and mail creation happens on the
 * world thread (HsArenaSettleUpdate), like the match coins of HsEconomyGlue.cpp.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_SETTLE_H
#define MOD_HEARTHSTONE_HS_ARENA_SETTLE_H

#include "arena/HsArenaPayout.h"
#include "arena/HsArenaService.h"

#include <cstdint>
#include <string>
#include <vector>

// The settlement hook of the arena glue: a run reached Finished / Retired. Settles it (idempotent, safe to call for a run that
// is settled already). A failure is logged and retried by HsArenaSettleUpdate.
void HsArenaSettleRun(Hs::ArenaRun const& run);

struct HsArenaClaimReply
{
    bool ok = false;
    // refusal codes of the arena service (no_run, stale, bad_state, not_special, not_missing) and the glue's:
    // unavailable (arena or collections not ready), storage, bad_card, overflow
    std::string why;
    bool replay = false;          // the claim was made (and delivered) before: nothing was granted now
    bool convertedToPack = false; // every set was complete: one pack replaced the card
    bool hasRun = false;
    Hs::ArenaRun run;
    std::string card;             // the card of the claim
    uint32_t copies = 0;          // the copies granted for it
};

// The player's choice of the special card after a ten-win run. guid is the character that chose (log only: the run
// and the card belong to the account). Idempotent: a repeat of the same claim, also after a restart or from another
// character, grants nothing a second time; a crash between recording the claim and the grant is finished by the
// repeat or by the recovery pass.
HsArenaClaimReply HsArenaClaimSpecial(uint32_t accountId, uint32_t guid, uint32_t runId, std::string const& cardId);

// The special cards a pending choice may take now (the sets the account is missing, minus cards an arena milestone
// claimed), with the copies each grants. Empty when none (or on a failure): with a choice pending and no option the
// claim of any special card turns into the extra pack.
std::vector<Hs::ArenaSpecialMissing> HsArenaSpecialOptions(uint32_t accountId);

// World update: the recovery pass and the coin deliveries (see above). diff in milliseconds.
void HsArenaSettleUpdate(uint32_t diff);

#endif
