/*
 * Draft arena entry debit: the order of the steps that turn a reserved run (ArenaService::Start
 * answered needsDebit) into a paid one, so that the order and its failure handling can be unit-tested with fakes.
 *
 * The rule: the debit of the paying character and the run's entry confirmation commit TOGETHER, in one character
 * database transaction, or neither does.
 *
 *   HasFunds                      verified first: nothing is touched when the character cannot pay
 *   Begin                         opens the character database transaction (serialized with the character's guid)
 *   Confirm                       ArenaService::ConfirmEntry; its SaveRun joins the open transaction (queued only)
 *   Debit                         queues the debit into the open transaction (gold: taken in memory and saved; coins:
 *                                 the stacks are planned and their rows queued, the bags are not touched yet)
 *   Commit                        commits the transaction synchronously (coins are taken in memory right after a
 *                                 commit that happened, so the bags never differ from the database)
 *
 * Any failure before the commit: Rollback (drops the queued statements), and AbortEntry exactly once (the run and
 * its daily slot are released). A failed commit is ambiguous (the database may have committed before the error
 * reached us), so Landed() reads the run back first: confirmed means the debit committed with it and the entry
 * stands; unconfirmed means nothing was written, Undo() gives the in-memory debit back and the entry is aborted.
 *
 * PURE C++ (the arena service only). The core calls are the ports; HsArenaGlue.cpp implements them.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_ENTRY_H
#define MOD_HEARTHSTONE_HS_ARENA_ENTRY_H

#include "HsArenaService.h"

#include <cstdint>
#include <string>

namespace Hs
{
    // What the glue does for the core. RunArenaEntry calls them on one thread in the order above.
    class ArenaEntryPorts
    {
    public:
        virtual ~ArenaEntryPorts() = default;
        // The paying character holds the run's amount (gold, or coin items in the bags).
        virtual bool HasFunds() = 0;
        virtual bool Begin() = 0;
        // ArenaService::ConfirmEntry with its storage write joined to the open transaction.
        virtual ArenaOutcome Confirm() = 0;
        // Takes the amount (all or nothing) and queues the character save into the open transaction.
        virtual bool Debit() = 0;
        // CommitTransactionDirect: true only if it committed.
        virtual bool Commit() = 0;
        virtual void Rollback() = 0;
        // After a failed Commit: whether the run reads confirmed from storage (the commit did happen).
        virtual bool Landed() = 0;
        // Gives back what Debit took in memory (only after a Debit that succeeded and a commit that did not land).
        // Coins were not taken in memory at that point: nothing to give back.
        virtual void Undo() = 0;
    };

    struct ArenaEntryResult
    {
        bool ok = false;
        // when !ok: funds (cannot pay), storage (transaction or confirmation failed), debit (could not take the
        // amount), commit (the commit failed and did not land; the debit was undone)
        std::string why;
        bool landedAfterError = false; // ok, but the commit reported an error that turned out to be applied
        bool aborted = false;          // AbortEntry ran (always when !ok)
        bool abortOk = false;          // and the run and slot are gone (false: the reservation stays, a retry resumes it)
    };

    ArenaEntryResult RunArenaEntry(ArenaService& service, ArenaEntryPorts& ports, uint32_t accountId, uint32_t runId);
}

#endif
