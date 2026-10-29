/*
 * mod-hearthstone: database glue of the Curse of Naxxramas card item claims, see HsNaxxStore.h and
 * data/sql/character/0013_hearthstone_naxx.sql. The transaction layout follows the arena settlement (HsArenaSettle.cpp).
 */

#include "HsNaxxStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <memory>

bool HsNaxxLoadClaims(uint32_t accountId, std::set<uint32_t>& itemEntries)
{
    itemEntries.clear();

    // A plain SELECT returns null both for "no rows" and for a failed query: a scalar head row (always one row) tells
    // them apart, as in HsArenaDbStorage::LoadLatestRun.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_account_naxx` WHERE `account_id` = %u)", accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the Naxxramas claims of account %u "
            "(character migration 0013_hearthstone_naxx.sql not applied, or database error)", accountId);
        return false;
    }
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;

    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT `item_entry` FROM `hearthstone_account_naxx` WHERE `account_id` = %u", accountId));
    if (!rows)
    {
        sLog.outError("Hearthstone: cannot read the Naxxramas claim rows of account %u", accountId);
        return false;
    }
    do
    {
        itemEntries.insert(rows->Fetch()[0].GetUInt32());
    } while (rows->NextRow());
    return true;
}

bool HsNaxxCommitClaim(uint32_t accountId, Hs::Mutation const& mutation, uint32_t itemEntry, uint32_t characterGuid)
{
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the Naxxramas claim of item %u of account %u",
            itemEntry, accountId);
        return false;
    }

    // The card rows and the marker are queued into the open transaction and run at the commit. A statement that was
    // not queued means the transaction must not commit.
    bool queued = HsQueueCardMutation(accountId, mutation);
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_account_naxx` (`account_id`, `item_entry`, `character_guid`) VALUES (%u, %u, %u)",
        accountId, itemEntry, characterGuid);
    if (!queued)
    {
        CharacterDatabase.RollbackTransaction();
        sLog.outError("Hearthstone: could not queue the Naxxramas claim of item %u (card %s) of account %u",
            itemEntry, mutation.cardId.c_str(), accountId);
        return false;
    }

    // Synchronous commit: a duplicate marker makes it fail here, and the card is not granted.
    if (!CharacterDatabase.CommitTransactionDirect())
    {
        sLog.outError("Hearthstone: commit failed for the Naxxramas claim of item %u (card %s) of account %u; "
            "its outcome is unknown, the claim is read from the database again", itemEntry, mutation.cardId.c_str(),
            accountId);
        return false;
    }
    return true;
}
