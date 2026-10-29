#include "HsArenaEntry.h"

namespace Hs
{
    ArenaEntryResult RunArenaEntry(ArenaService& service, ArenaEntryPorts& ports, uint32_t accountId, uint32_t runId)
    {
        ArenaEntryResult result;
        // The one AbortEntry of a failed entry.
        auto fail = [&](char const* why)
        {
            result.why = why;
            result.aborted = true;
            result.abortOk = service.AbortEntry(accountId, runId).ok;
            return result;
        };

        if (!ports.HasFunds())
            return fail("funds");
        if (!ports.Begin())
            return fail("storage");
        if (!ports.Confirm().ok)
        {
            ports.Rollback();
            return fail("storage");
        }
        if (!ports.Debit())
        {
            ports.Rollback();
            return fail("debit");
        }
        if (ports.Commit())
        {
            result.ok = true;
            return result;
        }
        // The commit reported a failure. If the database applied it anyway the run reads confirmed and the debit
        // stands; otherwise nothing was written and the memory goes back to what the database holds.
        if (ports.Landed())
        {
            result.ok = true;
            result.landedAfterError = true;
            return result;
        }
        ports.Undo();
        return fail("commit");
    }
}
