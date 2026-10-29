/*
 * mod-hearthstone: Draft Arena settlement glue, see HsArenaSettle.h and
 * src/arena/HsArenaPayout.h for the transaction layout.
 *
 * CRASH ANALYSIS
 *
 * Packs and the run state (one transaction, HsArenaPayout.cpp):
 *   - before the commit: nothing is written. The run is still Finished / Retired (open, it blocks a new entry); the
 *     recovery pass settles it (startup, then every 30 s) or the next call of the hook does.
 *   - inside the commit: MySQL applies the pack stock, the ledger row, the run state, the payout key and the pending
 *     coin row completely or not at all.
 *   - after the commit: the run is Settled and its key exists; every later call answers replay and grants nothing.
 *   - a commit that reports an error: the payout key is read back; present = it landed.
 *   The payout key (run_id, 'run') is a primary key, so even a logic error that tries a second settlement rolls the
 *   whole transaction back instead of granting twice.
 *
 * Coins (hearthstone_arena_coin_delivery, coins_left):
 *   - bags: the coins are stored in memory, then ONE transaction holds the character's inventory save
 *     (SaveInventoryAndGoldToDB) and the decrement of coins_left. A crash before the commit leaves neither the items
 *     nor the decrement (redelivered); after it both. A failed commit takes the items out of the bags again and
 *     keeps the row.
 *   - mail: before a chunk is sent, its item guid and size are recorded in the row (synchronously). After a crash the
 *     chunk is recognised as sent when its item is in mail_items (then the row is only decremented) and as unsent
 *     otherwise (then it is sent again with a new item). The one window left is the few milliseconds between the mail
 *     commit and the row update; a crash in it is recognised by the mail_items probe above.
 *
 * Special card: the claim is recorded in the run first (ArenaService::ClaimSpecial), then the card and the
 * hearthstone_arena_milestone row commit together (CollectionService::GrantVia). A crash between the two is finished
 * by the repeated claim or the recovery pass, which grant exactly the recorded copies unless the milestone row exists.
 */

#include "ScriptObjects.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "HsArenaGlue.h"
#include "HsArenaSettle.h"
#include "HsArenaStore.h"
#include "HsCollectionStore.h"
#include "HsEconomyGlue.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Settlement, claims and the recovery of runs: one mutex (see HsArenaSettle.h).
    std::mutex g_mutex;
    std::atomic<bool> g_wake{false};        // a settlement queued coins: deliver at the next update
    std::atomic<bool> g_recheckClaims{true}; // run the claim recovery at the next pass (true at startup)
    uint32 g_timer = 0;
    bool g_ready = false;                   // the arena answered once (data loaded)
    uint32 g_readyCheck = 5000;             // ms until the next readiness probe
    std::atomic<int> g_tablesState{0};                  // 0 unknown, 1 present, -1 missing
    uint32 g_failLogged = 0;                // run id whose settlement failure was logged last (no log spam)

    std::string Escaped(std::string s, std::size_t maxLen)
    {
        if (s.size() > maxLen)
            s.resize(maxLen);
        CharacterDatabase.escape_string(s);
        return s;
    }

    bool TablesPresent()
    {
        if (g_tablesState.load() != 0)
            return g_tablesState.load() > 0;
        bool ok = true;
        for (char const* name : { "hearthstone_arena_payout", "hearthstone_arena_coin_delivery", "hearthstone_arena_milestone" })
        {
            std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", name));
            if (!found)
            {
                sLog.outError("Hearthstone: table %s is missing in the character database (migration "
                    "0011_hearthstone_arena_settlement.sql / 0010_hearthstone_arena.sql not applied? check "
                    "Database.AutoUpdate.*): arena runs cannot be settled", name);
                ok = false;
            }
        }
        g_tablesState.store(ok ? 1 : -1);
        return ok;
    }

    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    // The ports of HsArenaPayout over the character database and the collection store.
    class DbPorts : public Hs::ArenaPayoutPorts
    {
    public:
        bool Begin() override { return CharacterDatabase.BeginTransaction(); }
        bool Commit() override { return CharacterDatabase.CommitTransactionDirect(); }
        void Rollback() override { CharacterDatabase.RollbackTransaction(); }

        Hs::ArenaOutcome SettleJoined(uint32_t accountId, uint32_t runId) override
        {
            HsArenaStoreJoin join(0);
            return HsArena().Settle(accountId, runId);
        }

        bool QueuePackChange(uint32_t accountId, Hs::PackStockChange const& change) override
        {
            return HsQueuePackChange(accountId, change);
        }

        bool QueuePayout(uint32_t runId, uint32_t accountId, char const* kind, uint32_t packs, uint32_t coins,
                         uint32_t coinGuid) override
        {
            return CharacterDatabase.PExecute(
                "INSERT INTO `hearthstone_arena_payout` (`run_id`, `kind`, `account_id`, `packs`, `coins`, `coin_guid`) "
                "VALUES (%u, '%s', %u, %u, %u, %u)", runId, Escaped(kind, 8).c_str(), accountId, packs, coins, coinGuid);
        }

        bool QueueCoinDelivery(uint32_t runId, uint32_t accountId, uint32_t guid, uint32_t coins) override
        {
            return CharacterDatabase.PExecute(
                "INSERT INTO `hearthstone_arena_coin_delivery` (`run_id`, `account_id`, `guid`, `coins_left`) "
                "VALUES (%u, %u, %u, %u)", runId, accountId, guid, coins);
        }

        bool QueueCardMutation(uint32_t accountId, Hs::Mutation const& mutation) override
        {
            return HsQueueCardMutation(accountId, mutation);
        }

        bool QueueMilestone(uint32_t accountId, std::string const& cardId, uint32_t runId) override
        {
            return CharacterDatabase.PExecute(
                "INSERT INTO `hearthstone_arena_milestone` (`account_id`, `card_id`, `run_id`) VALUES (%u, '%s', %u)",
                accountId, Escaped(cardId, Hs::kMaxCardIdLen).c_str(), runId);
        }

        bool LoadPayout(uint32_t runId, char const* kind, bool& found, uint32_t& packs) override
        {
            // The scalar head row (always one row) tells a failed query from "no row", as in the other stores.
            std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
                "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_payout` WHERE `run_id` = %u AND `kind` = '%s'), "
                "(SELECT COALESCE(MAX(`packs`), 0) FROM `hearthstone_arena_payout` WHERE `run_id` = %u AND `kind` = '%s')",
                runId, Escaped(kind, 8).c_str(), runId, Escaped(kind, 8).c_str()));
            if (!head)
            {
                sLog.outError("Hearthstone: cannot read the arena payout of run %u", runId);
                return false;
            }
            found = head->Fetch()[0].GetUInt64() != 0;
            packs = head->Fetch()[1].GetUInt32();
            return true;
        }

        bool LoadMilestones(uint32_t accountId, Hs::ArenaMilestones& out) override
        {
            std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
                "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_milestone` WHERE `account_id` = %u)", accountId));
            if (!head)
            {
                sLog.outError("Hearthstone: cannot read the arena milestones of account %u", accountId);
                return false;
            }
            out.clear();
            if (head->Fetch()[0].GetUInt64() == 0)
                return true;
            std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
                "SELECT `card_id`, `run_id` FROM `hearthstone_arena_milestone` WHERE `account_id` = %u", accountId));
            if (!rows)
                return false;
            do
            {
                Field* f = rows->Fetch();
                out[f[0].GetCppString()] = f[1].GetUInt32();
            } while (rows->NextRow());
            return true;
        }

        bool LoadRunForClaim(uint32_t runId, bool& found, Hs::ArenaRun& out) override
        {
            std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
                "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `run_id` = %u)", runId));
            if (!head)
                return false;
            found = head->Fetch()[0].GetUInt64() != 0;
            if (!found)
                return true;
            std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
                "SELECT `account_id`, `state`, `special_pending`, `special_card`, `special_copies`, `prize_packs`, "
                "`prize_special` FROM `hearthstone_arena_run` WHERE `run_id` = %u", runId));
            if (!row)
                return false;
            Field* f = row->Fetch();
            Hs::ArenaRun run;
            run.id = runId;
            run.accountId = f[0].GetUInt32();
            if (!Hs::ParseArenaState(f[1].GetCppString(), run.state))
                return false;
            run.specialPending = f[2].GetUInt32() != 0;
            run.specialCard = f[3].IsNULL() ? std::string() : f[3].GetCppString();
            run.specialCopies = f[4].GetUInt32();
            run.prize.packs = f[5].GetUInt32();
            run.prize.specialChoice = f[6].GetUInt32() != 0;
            out = std::move(run);
            return true;
        }
    };

    // ---- coin delivery -------------------------------------------------------------------------------------------

    struct DeliveryRow
    {
        uint32 runId = 0;
        uint32 accountId = 0;
        uint32 guid = 0;
        uint32 left = 0;
        uint32 mailItem = 0;
        uint32 mailCoins = 0;
    };

    bool ReadRows(std::vector<DeliveryRow>& out)
    {
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_coin_delivery`)"));
        if (!head)
            return false;
        out.clear();
        if (head->Fetch()[0].GetUInt64() == 0)
            return true;
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT `run_id`, `account_id`, `guid`, `coins_left`, `mail_item`, `mail_coins` "
            "FROM `hearthstone_arena_coin_delivery` ORDER BY `run_id` LIMIT 20"));
        if (!rows)
            return false;
        do
        {
            Field* f = rows->Fetch();
            DeliveryRow row;
            row.runId = f[0].GetUInt32();
            row.accountId = f[1].GetUInt32();
            row.guid = f[2].GetUInt32();
            row.left = f[3].GetUInt32();
            row.mailItem = f[4].GetUInt32();
            row.mailCoins = f[5].GetUInt32();
            out.push_back(row);
        } while (rows->NextRow());
        return true;
    }

    // Takes `n` coins off the row (and removes it at 0), synchronously. Clears the mail marker.
    bool Decrement(DeliveryRow& row, uint32 n)
    {
        n = std::min(n, row.left);
        if (!CharacterDatabase.DirectPExecute(
                "UPDATE `hearthstone_arena_coin_delivery` SET `coins_left` = `coins_left` - %u, `mail_item` = 0, "
                "`mail_coins` = 0 WHERE `run_id` = %u AND `coins_left` >= %u", n, row.runId, n))
            return false;
        row.left -= n;
        row.mailItem = 0;
        row.mailCoins = 0;
        if (row.left == 0)
            CharacterDatabase.DirectPExecute("DELETE FROM `hearthstone_arena_coin_delivery` WHERE `run_id` = %u AND "
                "`coins_left` = 0", row.runId);
        return true;
    }

    bool MailItemSent(uint32 itemGuid)
    {
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `mail_items` WHERE `item_guid` = %u)", itemGuid));
        return head && head->Fetch()[0].GetUInt64() != 0;
    }

    bool HordeOf(uint32 guid, Player* player)
    {
        if (player)
            return player->GetTeam() == HORDE;
        std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery("SELECT `race` FROM `characters` WHERE `guid` = %u", guid));
        return row && Player::TeamForRace(row->Fetch()[0].GetUInt8()) == HORDE;
    }

    // Delivers what is left of one row. True when the row is finished (deleted) or cannot progress this pass.
    void Deliver(DeliveryRow row)
    {
        if (row.left == 0)
        {
            // Everything was delivered but a crash (or a failed delete) left the row behind.
            CharacterDatabase.DirectPExecute("DELETE FROM `hearthstone_arena_coin_delivery` WHERE `run_id` = %u AND "
                "`coins_left` = 0", row.runId);
            return;
        }
        ObjectGuid const guid(HIGHGUID_PLAYER, row.guid);
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (player && !player->IsInWorld())
            player = nullptr;

        // A mail chunk that a crash interrupted: sent (its item is in a mail) or not.
        if (row.mailItem != 0)
        {
            if (MailItemSent(row.mailItem))
            {
                sLog.outString("Hearthstone: arena run %u: the %u coin mail chunk (item %u) of guid %u was sent before the "
                    "restart", row.runId, row.mailCoins, row.mailItem, row.guid);
                if (!Decrement(row, row.mailCoins))
                    return;
            }
            else if (!CharacterDatabase.DirectPExecute("UPDATE `hearthstone_arena_coin_delivery` SET `mail_item` = 0, "
                    "`mail_coins` = 0 WHERE `run_id` = %u", row.runId))
                return;
            else
            {
                row.mailItem = 0;
                row.mailCoins = 0;
            }
            if (row.left == 0)
                return;
        }

        uint32 inBags = 0;
        uint32 mailed = 0;

        // The bags: stored in memory, then the inventory save and the decrement commit together.
        if (player && !CharacterDatabase.InTransaction())
        {
            uint32 stored = row.left;
            uint32 noSpace = 0;
            ItemPosCountVec dest;
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, kHsCoinItem, stored, &noSpace) != EQUIP_ERR_OK)
                stored = stored > noSpace ? stored - noSpace : 0;
            if (stored > 0 && !dest.empty())
            {
                Item* item = player->StoreNewItem(dest, kHsCoinItem, true);
                if (item)
                {
                    player->SendNewItem(item, stored, true, false);
                    bool committed = false;
                    if (CharacterDatabase.BeginTransaction(player->GetGUIDLow()))
                    {
                        player->SaveInventoryAndGoldToDB();
                        bool const queued = CharacterDatabase.PExecute(
                            "UPDATE `hearthstone_arena_coin_delivery` SET `coins_left` = `coins_left` - %u WHERE "
                            "`run_id` = %u AND `coins_left` >= %u", stored, row.runId, stored);
                        if (queued)
                            committed = CharacterDatabase.CommitTransactionDirect();
                        else
                            CharacterDatabase.RollbackTransaction();
                    }
                    bool landed = committed;
                    if (!committed)
                    {
                        // The commit reported an error: the row tells whether it was applied.
                        std::unique_ptr<QueryResult> now(CharacterDatabase.PQuery(
                            "SELECT `coins_left` FROM `hearthstone_arena_coin_delivery` WHERE `run_id` = %u", row.runId));
                        landed = now && now->Fetch()[0].GetUInt32() + stored == row.left;
                    }
                    if (landed)
                    {
                        row.left -= stored;
                        inBags = stored;
                        if (row.left == 0)
                            CharacterDatabase.DirectPExecute("DELETE FROM `hearthstone_arena_coin_delivery` WHERE `run_id` = %u "
                                "AND `coins_left` = 0", row.runId);
                    }
                    else
                    {
                        // Nothing was written: take the coins out of the bags again, the row stays for the next pass.
                        player->DestroyItemCount(kHsCoinItem, stored, true);
                        sLog.outError("Hearthstone: arena run %u: the delivery of %u coins to %s could not be saved; retrying",
                            row.runId, stored, player->GetName());
                        return;
                    }
                }
            }
        }

        // The rest by mail (bags full, or the character is not in the world).
        while (row.left > 0)
        {
            uint32 const n = std::min(row.left, Hs::kArenaMailCoinStack);
            Item* item = Item::CreateItem(kHsCoinItem, n, player);
            if (!item)
            {
                sLog.outError("Hearthstone: arena run %u: could not create %u coin items for guid %u", row.runId, n, row.guid);
                break;
            }
            if (!CharacterDatabase.DirectPExecute("UPDATE `hearthstone_arena_coin_delivery` SET `mail_item` = %u, "
                    "`mail_coins` = %u WHERE `run_id` = %u", item->GetGUIDLow(), n, row.runId))
            {
                delete item;
                break;
            }
            item->SaveToDB(); // the mail loads it from the database
            MailDraft("Hearthstone Coins", "Your prize for arena run " + std::to_string(row.runId) + ": " + std::to_string(n) +
                " Hearthstone Coins.")
                .AddItem(item)
                .SendMailTo(player ? MailReceiver(player) : MailReceiver(guid),
                    MailSender(MAIL_CREATURE, HordeOf(row.guid, player) ? 919501u : 919502u), MAIL_CHECK_MASK_NONE, 0, 0, true);
            if (!Decrement(row, n))
                break; // the marker stays: the next pass sees the item in mail_items and only decrements
            mailed += n;
        }

        if (inBags + mailed > 0)
        {
            sLog.outString("Hearthstone: arena run %u paid %u coins to guid %u (account %u): %u in the bags, %u by mail%s",
                row.runId, inBags + mailed, row.guid, row.accountId, inBags, mailed, row.left ? ", more pending" : "");
            if (player)
                SysMessage(player, "your arena prize: " + std::to_string(inBags + mailed) + " Hearthstone Coins" +
                    (mailed ? " (" + std::to_string(mailed) + " by mail: your bags were full)." : "."));
            HsPushWalletChanged(row.accountId, "arena", inBags + mailed);
        }
    }

    // Logs a settlement failure once per run (the pass retries every 30 seconds).
    void LogSettleFail(uint32 runId, uint32 accountId, std::string const& why)
    {
        if (g_failLogged == runId)
            return;
        g_failLogged = runId;
        sLog.outError("Hearthstone: arena run %u of account %u could not be settled: %s; it stays open and is retried",
            runId, accountId, why.c_str());
    }

    struct SettledPush
    {
        uint32 accountId = 0;
        uint32 stock = 0;
        uint32 packs = 0;
    };

    // Settles one run under the mutex; fills push when packs were added.
    Hs::ArenaSettleResult SettleLocked(uint32 accountId, uint32 runId, SettledPush& push)
    {
        DbPorts ports;
        Hs::ArenaSettleResult const r = Hs::SettleArenaRun(HsArena(), HsCollections(), ports, accountId, runId);
        if (!r.ok)
        {
            LogSettleFail(runId, accountId, r.why);
            return r;
        }
        if (r.replay)
            return r;
        g_failLogged = 0;
        if (r.coins > 0)
            g_wake.store(true, std::memory_order_relaxed);
        if (r.packs > 0)
        {
            push.accountId = accountId;
            push.stock = r.stockAfter;
            push.packs = r.packs;
        }
        sLog.outString("Hearthstone: arena run %u of account %u settled (%s, %u win(s)): %u pack(s) -> stock %u, %u coin(s) "
            "to guid %u%s%s", runId, accountId, r.run.retired ? "retired" : "finished", r.run.wins, r.packs, r.stockAfter,
            r.coins, r.coinGuid, r.specialPending ? ", a special card is to be chosen" : "",
            r.landedAfterError ? " (the commit reported an error but was applied)" : "");
        return r;
    }

    void FinishPush(SettledPush const& push)
    {
        if (push.packs > 0)
            HsPushPackChanged(push.accountId, push.stock);
    }

    // Recovery: runs that ended and are not settled, then claims and conversions a crash left unfinished.
    void RecoverRuns()
    {
        std::vector<std::pair<uint32, uint32>> ended; // account, run
        {
            std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
                "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_run` WHERE `open_account` IS NOT NULL AND `state` IN "
                "('finished', 'retired'))"));
            if (head && head->Fetch()[0].GetUInt64() != 0)
            {
                std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
                    "SELECT `account_id`, `run_id` FROM `hearthstone_arena_run` WHERE `open_account` IS NOT NULL AND "
                    "`state` IN ('finished', 'retired') ORDER BY `run_id` LIMIT 50"));
                if (rows)
                    do
                    {
                        ended.emplace_back(rows->Fetch()[0].GetUInt32(), rows->Fetch()[1].GetUInt32());
                    } while (rows->NextRow());
            }
        }
        for (auto const& entry : ended)
        {
            SettledPush push;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                SettleLocked(entry.first, entry.second, push);
            }
            FinishPush(push);
        }

        if (!g_recheckClaims.exchange(false, std::memory_order_relaxed))
            return;
        std::vector<std::pair<uint32, uint32>> owed;
        std::unique_ptr<QueryResult> special(CharacterDatabase.PQuery(
            "SELECT r.`account_id`, r.`run_id` FROM `hearthstone_arena_run` r LEFT JOIN `hearthstone_arena_milestone` m "
            "ON m.`account_id` = r.`account_id` AND m.`card_id` = r.`special_card` WHERE r.`state` = 'settled' AND "
            "r.`special_card` <> '' AND m.`card_id` IS NULL ORDER BY r.`run_id` LIMIT 50"));
        if (special)
            do
            {
                owed.emplace_back(special->Fetch()[0].GetUInt32(), special->Fetch()[1].GetUInt32());
            } while (special->NextRow());
        std::unique_ptr<QueryResult> converted(CharacterDatabase.PQuery(
            "SELECT r.`account_id`, r.`run_id` FROM `hearthstone_arena_run` r JOIN `hearthstone_arena_payout` p ON "
            "p.`run_id` = r.`run_id` AND p.`kind` = 'run' LEFT JOIN `hearthstone_arena_payout` c ON c.`run_id` = "
            "r.`run_id` AND c.`kind` = 'convert' WHERE r.`state` = 'settled' AND r.`special_pending` = 0 AND "
            "r.`special_card` = '' AND r.`prize_packs` > p.`packs` AND c.`run_id` IS NULL ORDER BY r.`run_id` LIMIT 50"));
        if (converted)
            do
            {
                owed.emplace_back(converted->Fetch()[0].GetUInt32(), converted->Fetch()[1].GetUInt32());
            } while (converted->NextRow());
        for (auto const& entry : owed)
        {
            Hs::ArenaClaimResult r;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                DbPorts ports;
                r = Hs::FinishArenaClaim(HsArena(), HsCollections(), ports, entry.first, entry.second);
            }
            if (!r.ok)
            {
                sLog.outError("Hearthstone: arena run %u of account %u: its claim could not be finished: %s", entry.second,
                    entry.first, r.why.c_str());
                g_recheckClaims.store(true, std::memory_order_relaxed);
            }
            else if (r.delivered)
            {
                sLog.outString("Hearthstone: arena run %u of account %u: finished the claim a restart interrupted (%s)",
                    entry.second, entry.first, r.card.empty() ? "an extra pack" : r.card.c_str());
                if (r.packs > 0)
                    HsPushPackChanged(entry.first, r.stockAfter);
                if (r.copies > 0)
                    HsPushCardChanged(entry.first, r.versionAfter);
            }
        }
    }
}

void HsArenaSettleRun(Hs::ArenaRun const& run)
{
    if (!TablesPresent())
        return;
    SettledPush push;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        SettleLocked(run.accountId, run.id, push);
    }
    FinishPush(push);
}

HsArenaClaimReply HsArenaClaimSpecial(uint32_t accountId, uint32_t guid, uint32_t runId, std::string const& cardId)
{
    HsArenaClaimReply reply;
    if (!TablesPresent() || !HsCollections().Catalog())
    {
        reply.why = "unavailable";
        return reply;
    }
    Hs::ArenaClaimResult r;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        DbPorts ports;
        r = Hs::ClaimArenaSpecial(HsArena(), HsCollections(), ports, accountId, runId, cardId);
    }
    reply.ok = r.ok;
    reply.why = r.why;
    reply.replay = r.replay;
    reply.convertedToPack = r.convertedToPack;
    reply.hasRun = r.hasRun;
    reply.run = r.run;
    reply.card = r.card;
    reply.copies = r.copies;
    if (r.ok && r.delivered)
    {
        sLog.outString("Hearthstone: arena run %u of account %u: guid %u took %s", runId, accountId, guid,
            r.convertedToPack ? "an extra pack instead of a special card" :
            (r.card + " x" + std::to_string(r.copies)).c_str());
        if (r.packs > 0)
            HsPushPackChanged(accountId, r.stockAfter);
        if (r.copies > 0)
            HsPushCardChanged(accountId, r.versionAfter);
    }
    else if (!r.ok)
    {
        sLog.outString("Hearthstone: arena run %u of account %u: the special card claim of guid %u (%s) was refused: %s",
            runId, accountId, guid, cardId.c_str(), r.why.c_str());
        // The claim may have been recorded while the grant failed: let the recovery pass look again.
        if (r.why == "storage" || r.why == "unavailable")
            g_recheckClaims.store(true, std::memory_order_relaxed);
    }
    return reply;
}

std::vector<Hs::ArenaSpecialMissing> HsArenaSpecialOptions(uint32_t accountId)
{
    std::vector<Hs::ArenaSpecialMissing> options;
    if (!TablesPresent())
        return options;
    Hs::Collection collection;
    Hs::ArenaMilestones claimed;
    DbPorts ports;
    if (!HsCollections().Get(accountId, collection) || !ports.LoadMilestones(accountId, claimed))
        return options;
    return Hs::ArenaSpecialOptions(collection, claimed);
}

void HsArenaSettleUpdate(uint32_t diff)
{
    // The arena has to be up (card data loaded, tables present) before anything is read or settled.
    if (!g_ready)
    {
        g_readyCheck = g_readyCheck > diff ? g_readyCheck - diff : 0;
        if (g_readyCheck > 0)
            return;
        g_readyCheck = 5000;
        if (HsArenaGet(0).why == "unavailable" || !TablesPresent())
            return;
        g_ready = true;
        g_timer = 30000; // the recovery pass runs now
    }

    g_timer += diff;
    bool const wake = g_wake.exchange(false, std::memory_order_relaxed);
    if (!wake && g_timer < 30000)
        return;
    if (g_timer >= 30000)
    {
        g_timer = 0;
        RecoverRuns();
    }

    std::vector<DeliveryRow> rows;
    if (!ReadRows(rows))
    {
        sLog.outError("Hearthstone: cannot read the pending arena coin deliveries");
        return;
    }
    for (DeliveryRow const& row : rows)
        Deliver(row);
}
