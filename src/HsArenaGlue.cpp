/*
 * mod-hearthstone: Draft Arena glue, see HsArenaGlue.h.
 *
 * ENTRY PAYMENT AND CRASH SAFETY
 *
 * A paid entry is two database transactions, never one half:
 *
 *   T1 (HsArenaDbStorage::CreateRun): the run row (entry_confirmed = 0), the daily slot row and the class offer.
 *      The unique keys make "one open run per account" and "one paid entry per account and period" storage rules.
 *   T2 (RunArenaEntry): the character's debit AND entry_confirmed = 1:
 *        CharacterDatabase.BeginTransaction(guid)      the transaction of Player::SaveInventoryAndGoldToDB
 *        ConfirmEntry (SaveRun, joined)                 queues  UPDATE hearthstone_arena_run SET entry_confirmed = 1
 *        gold:  LogModifyMoney (in memory) + SaveInventoryAndGoldToDB   queues UPDATE characters SET money ...
 *        coins: the stacks to take are planned and their DELETE / UPDATE rows are queued; the bags are NOT touched yet
 *        CommitTransactionDirect                        one MySQL transaction, synchronous
 *        coins: only after a commit that happened, the planned stacks are taken in memory (the same stacks, so memory
 *               equals the database)
 *
 * Server death at each point (the database is the only thing that survives):
 *   - before T1 commits: nothing exists. The player was not charged.
 *   - after T1, before T2 commits: the run exists with entry_confirmed = 0 and holds the daily slot; the character
 *     still has its money / coins (the debit was only in memory). The player's next enter finds that run
 *     (ArenaService::Start answers needsDebit) and pays it with the run's ORIGINAL method, whichever method the retry
 *     named; a retry that cannot pay it releases the reservation (run and slot) once, so another method can then be
 *     chosen. The unconfirmed run blocks nothing else: it is neither playable nor retirable.
 *   - inside T2: MySQL applies the transaction completely or not at all. Not applied: as the previous case. Applied
 *     (the server died right after): the character is charged and the run is confirmed; nothing is owed.
 *   - after T2: a normal paid run.
 * A T2 that fails while the server lives: nothing before the commit has touched the database (the statements are only
 * queued), so a failure of Begin / Confirm rolls the queue back and releases the reservation (AbortEntry, once). If
 * the COMMIT itself reports an error, the run is read back: confirmed means the commit happened after all (the entry
 * stands), unconfirmed means nothing was written and the in-memory debit is given back (gold; coins were never taken
 * in memory, so there is nothing to give back and memory still equals the database) and the reservation is released.
 * Why the coins are taken after the commit: destroyed coin items cannot be recreated safely (new items next to the
 * old rows would duplicate at the next login, and a stale row in a reused slot would be overwritten and lost), so the
 * bags must never differ from the database. A crash between the commit and the in-memory take loses nothing and
 * charges once: the database already holds the debit, and the character is reloaded from it.
 * Why the synchronous CommitTransactionDirect and not the queued CommitTransaction: every later arena step reads
 * the run from the database, so the confirmation has to be visible when the call returns, and the glue has to know
 * whether the commit worked. (It is the form Player::SaveToDB(direct) and Mail use; the queued one is the
 * fire-and-forget form that cannot report a failure.) Residual window: statements for this character that are
 * already waiting in the delay queue are not ordered before a direct commit; they carry the state of the moment they
 * were queued and the character's next save rewrites money and bags from memory, so the debit cannot be lost to them
 * except in a crash inside that few-millisecond window, where the run would count as paid and the money would not
 * have been taken.
 *
 * ACTION LOG: every action of a duel is written with one synchronous INSERT before its GAME payload is sent
 * (HsArenaDbStorage::StoreActions); a failed write suspends the duel with only what was stored. A crash therefore
 * loses no action the player has seen. The run stays in state duel in the database; the next start pauses such runs
 * (HsArenaStore RecoverDuels), and a resume replays the log.
 */

#include "Bag.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsArenaGlue.h"
#include "HsWeeklyQuestGlue.h"
#include "HsArenaSettle.h"
#include "HsArenaStore.h"
#include "HsCollectionStore.h"
#include "HsDataset.h"
#include "HsEconomyGlue.h"
#include "HsLadderDuelStore.h"
#include "HsMatchGlue.h"
#include "HsBgGlue.h"
#include "HsMatchStore.h"
#include "arena/HsArenaDraftAi.h"
#include "arena/HsArenaEntry.h"
#include "arena/HsArenaRows.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32 kHostEzra = 919501;
    constexpr uint32 kHostIbelin = 919502;

    // Everything that changes a run or a duel runs under this mutex (see HsArenaGlue.h for the lock order).
    std::mutex g_mutex;

    // The card data of the arena: set once at startup (the catalog and the card database do not change afterwards).
    struct ArenaData
    {
        std::shared_ptr<Hs::CardCatalog const> catalog;
        std::shared_ptr<Hs::Match::CardDb const> db;
        std::map<std::string, Hs::ArenaPool> pools; // deck class -> pool
    };
    std::mutex g_dataMutex;
    std::shared_ptr<ArenaData const> g_data;
    std::atomic<bool> g_ready{false};

    std::shared_ptr<ArenaData const> Data()
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        return g_data;
    }

    uint32 AccountOf(Player* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    uint64 UnixNow()
    {
        return static_cast<uint64>(std::time(nullptr));
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    HsArenaReply Reply(Hs::ArenaOutcome const& outcome)
    {
        HsArenaReply reply;
        reply.ok = outcome.ok;
        reply.why = outcome.why;
        reply.replay = outcome.replay;
        reply.hasRun = outcome.hasRun;
        if (outcome.hasRun)
            reply.run = outcome.run;
        return reply;
    }

    HsArenaReply Refusal(char const* why, std::string const& text = std::string(), Hs::ArenaRun const* run = nullptr)
    {
        HsArenaReply reply;
        reply.why = why;
        reply.text = text;
        if (run)
        {
            reply.run = *run;
            reply.hasRun = true;
        }
        return reply;
    }

    bool SpecialSetsComplete(uint32 accountId)
    {
        Hs::Collection collection;
        // Unknown (the collection is unavailable): the player gets the choice, and ClaimSpecial turns it into the
        // extra pack when every set is complete by then.
        return HsCollections().Get(accountId, collection) && Hs::ArenaSpecialSetsComplete(collection);
    }

    // A live duel of this run: the controller's match runs in this process.
    bool ControllerLive(Hs::ArenaRun const& run)
    {
        return run.controllerGuid != 0 && HsMatches().IsRunning(run.controllerGuid) &&
            HsMatches().IsArena(run.controllerGuid);
    }

    // The account's current run, checked against the id the caller means (0: any). Every state is real: a run in state
    // duel without a live match is either being saved right now (StoreSuspended pauses it a moment later) or was left
    // by a crash (RecoverDuels pauses it at startup), so it is not normalized here.
    Hs::ArenaOutcome Current(uint32 accountId, uint32 runId)
    {
        Hs::ArenaOutcome got = HsArena().Get(accountId);
        if (got.ok && runId != 0 && got.run.id != runId)
        {
            Hs::ArenaOutcome stale;
            stale.why = "stale";
            stale.run = got.run;
            stale.hasRun = true;
            return stale;
        }
        return got;
    }

    // The settlement hook. A run reaches it exactly once when it ends (Finished by a loss or the tenth win, or Retired),
    // with its prize fixed. A special-card choice stays pending until ClaimSpecial.
    void SettleHook(Hs::ArenaRun const& run)
    {
        sLog.outString("Hearthstone: arena run %u of account %u ended (%s): %u win(s), %u loss(es), prize %u pack(s) + %u "
            "coin(s)%s", run.id, run.accountId, run.retired ? "retired" : "finished",
            run.wins, run.losses, run.prize.packs, run.prize.coins, run.prize.specialChoice ? " + a special card" : "");
        HsArenaSettleRun(run); // HsArenaSettle.cpp: one transaction; a failure is retried by HsArenaSettleUpdate
    }

    // ---- entry -----------------------------------------------------------------------------------------------

    std::string GoldText(uint32 copper)
    {
        return std::to_string(copper / 10000) + "g" + (copper % 10000 ? " " + std::to_string(copper % 10000 / 100) + "s" : "");
    }

    // The core side of RunArenaEntry for one paying character.
    class EntryPorts : public Hs::ArenaEntryPorts
    {
    public:
        EntryPorts(Player* player, Creature* host, Hs::ArenaRun const& run)
            : m_player(player), m_host(host), m_run(run), m_amount(run.terms.payAmount) {}

        bool HasFunds() override
        {
            return m_run.terms.pay == Hs::ArenaPay::Gold ? m_player->GetMoney() >= m_amount : HsCoinCount(m_player) >= m_amount;
        }

        bool Begin() override
        {
            return CharacterDatabase.BeginTransaction(m_player->GetGUIDLow());
        }

        Hs::ArenaOutcome Confirm() override
        {
            HsArenaStoreJoin join(m_player->GetGUIDLow());
            return HsArena().ConfirmEntry(m_run.accountId, m_run.id);
        }

        bool Debit() override
        {
            if (!HasFunds())
                return false;
            if (m_run.terms.pay == Hs::ArenaPay::Gold)
            {
                m_player->LogModifyMoney(-static_cast<int32>(m_amount), "HsArenaEntry", m_host->GetObjectGuid(), m_run.id);
                // Joins the open transaction (the player's own guid is its serial id).
                m_player->SaveInventoryAndGoldToDB();
                return true;
            }
            return PlanCoins();
        }

        bool Commit() override
        {
            bool const committed = CharacterDatabase.CommitTransactionDirect();
            if (committed)
                TakeCoins();
            return committed;
        }

        void Rollback() override
        {
            CharacterDatabase.RollbackTransaction();
            m_plan.clear();
        }

        bool Landed() override
        {
            bool found = false;
            Hs::ArenaRun run;
            bool const landed = HsArenaStore().LoadLatestRun(m_run.accountId, found, run) && found &&
                run.id == m_run.id && run.entryConfirmed;
            if (landed)
                TakeCoins(); // the database holds the debit: the bags follow it
            return landed;
        }

        void Undo() override
        {
            if (m_run.terms.pay == Hs::ArenaPay::Gold)
            {
                m_player->LogModifyMoney(static_cast<int32>(m_amount), "HsArenaEntryUndo", m_host->GetObjectGuid(), m_run.id);
                return;
            }
            // Coins: nothing was taken in memory (see TakeCoins), the database kept them.
            sLog.outError("Hearthstone: arena entry of run %u: the commit failed; the %u coin(s) of guid %u were not taken",
                m_run.id, m_amount, m_player->GetGUIDLow());
            SysMessage(m_player, "the entry could not be saved. Your " + std::to_string(m_amount) +
                " Hearthstone Coins were not spent.");
        }

    private:
        // One coin stack the entry takes: the whole stack (left = 0) or a part of it (left = what remains).
        struct CoinTake
        {
            Item* item;
            uint32 take;
            uint32 left;
        };

        void Consider(Item* item, uint32& remaining)
        {
            if (!item || remaining == 0 || item->GetEntry() != kHsCoinItem || item->IsInTrade())
                return;
            uint32 const take = std::min(item->GetCount(), remaining);
            m_plan.push_back({ item, take, item->GetCount() - take });
            remaining -= take;
        }

        // Plans the stacks (the order the core's DestroyItemCount walks: backpack, then the bags) and queues their
        // database rows into the open transaction. The bags stay as they are until TakeCoins.
        bool PlanCoins()
        {
            m_plan.clear();
            uint32 remaining = m_amount;
            for (uint32 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                Consider(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, i), remaining);
            for (uint32 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END && remaining > 0; ++i)
            {
                if (Bag const* bag = static_cast<Bag*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, i)))
                    for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                        Consider(bag->GetItemByPos(j), remaining);
            }
            if (remaining != 0)
            {
                m_plan.clear();
                return false;
            }
            for (CoinTake const& t : m_plan)
            {
                if (t.left == 0)
                {
                    t.item->DeleteFromInventoryDB();
                    t.item->DeleteFromDB();
                }
                else
                {
                    static SqlStatementID updCount;
                    SqlStatement stmt = CharacterDatabase.CreateStatement(updCount,
                        "UPDATE `item_instance` SET `count` = ? WHERE `guid` = ?");
                    stmt.PExecute(t.left, t.item->GetGUIDLow());
                }
            }
            return true;
        }

        // After a commit that happened: takes the planned stacks in memory (once). The items then save their already
        // committed state again at the next autosave, which changes nothing.
        void TakeCoins()
        {
            if (m_taken || m_plan.empty())
                return;
            m_taken = true;
            for (CoinTake const& t : m_plan)
            {
                uint32 count = t.take;
                m_player->DestroyItemCount(t.item, count, true);
            }
            m_plan.clear();
        }

        Player* m_player;
        Creature* m_host;
        Hs::ArenaRun m_run;
        uint32 m_amount;
        std::vector<CoinTake> m_plan;
        bool m_taken = false;
    };

    // Pays the run that Start reserved. The reservation exists; every failure releases it (inside RunArenaEntry).
    HsArenaReply PayEntry(Player* player, Creature* host, Hs::ArenaRun const& run)
    {
        EntryPorts ports(player, host, run);
        Hs::ArenaEntryResult const result = Hs::RunArenaEntry(HsArena(), ports, run.accountId, run.id);
        if (!result.ok)
        {
            sLog.outString("Hearthstone: arena entry of %s (account %u, run %u, %s %u) failed: %s%s", player->GetName(),
                run.accountId, run.id, Hs::ArenaPayCode(run.terms.pay), run.terms.payAmount, result.why.c_str(),
                result.abortOk ? " (reservation released)" : " (the reservation stays; a retry resumes it)");
            std::string text;
            if (result.why == "funds")
                text = run.terms.pay == Hs::ArenaPay::Gold
                    ? "You need " + GoldText(run.terms.payAmount) + " to enter."
                    : "You need " + std::to_string(run.terms.payAmount) + " Hearthstone Coins in your bags to enter.";
            return Refusal(result.why.c_str(), text);
        }
        if (result.landedAfterError)
            sLog.outError("Hearthstone: arena entry of run %u: the commit reported an error but was applied", run.id);
        HsArenaReply reply = Reply(HsArena().Get(run.accountId));
        sLog.outString("Hearthstone: arena run %u started by %s (account %u): %s %u paid", run.id, player->GetName(),
            run.accountId, Hs::ArenaPayCode(run.terms.pay), run.terms.payAmount);
        return reply;
    }

    // ---- duels -----------------------------------------------------------------------------------------------

    // Why a duel cannot start or resume for this character right now: a refusal code, text its detail; nullptr = it can.
    char const* DuelBlock(Player* player, Creature* host, std::string& text)
    {
        if (!g_ready.load(std::memory_order_relaxed) || !HsMatches().CardDb() || !sHearthstoneConfig.GetMatch().enable)
            return "unavailable";
        if (!player || !host || !HsArenaIsHost(host->GetEntry()))
            return "host";
        uint32 const guid = player->GetGUIDLow();
        if (!sHearthstoneConfig.Get().enable || !HsSessionHandshaken(guid))
        {
            text = "The Hearthstone addon is not running. Install it and /reload, then talk to me again.";
            return "world";
        }
        if (HsMatches().IsRunning(guid))
            return "running";
        // A Battlegrounds game blocks an arena duel (and an arena duel blocks a Battlegrounds game).
        if (HsBgBusy(guid))
            return "bg";
        // One card duel per account: a ladder duel (paused or running) blocks an arena duel.
        if (HsLadderDuels().Has(AccountOf(player)))
        {
            text = "You have a ladder duel to finish first. Sit down on a ladder chair to resume it or to surrender.";
            return "world";
        }
        if (!player->IsAlive())
        {
            text = "You cannot play while you are dead.";
            return "world";
        }
        if (player->IsInCombat())
        {
            text = "You cannot play while you are in combat.";
            return "world";
        }
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (member && member != player && member->IsInWorld() && member->IsInCombat())
                {
                    text = std::string(member->GetName()) + " in your group is in combat. Everyone has to be out of combat.";
                    return "world";
                }
            }
        }
        return nullptr;
    }

    // A run in a state that is not the one the call needs: the reason as a refusal.
    HsArenaReply WrongState(Hs::ArenaRun const& run)
    {
        if (!run.entryConfirmed)
            return Refusal("unpaid", std::string(), &run);
        switch (run.state)
        {
            case Hs::ArenaState::Repair: return Refusal("repair", std::string(), &run);
            case Hs::ArenaState::InDuel: return Refusal("busy", std::string(), &run);
            case Hs::ArenaState::Paused: return Refusal("paused", std::string(), &run);
            case Hs::ArenaState::Ready: return Refusal("ready", std::string(), &run);
            default: return Refusal("bad_state", std::string(), &run);
        }
    }

    // The AI opponent of a duel: a random class and a full draft under the run's own offer rules (no strengthening by
    // win count, no counter-drafting). False when the card data cannot make a legal deck.
    bool DraftOpponent(Player* player, Hs::ArenaRun const& run, uint32 matchNo, Hs::ArenaOpponent& out)
    {
        std::shared_ptr<ArenaData const> const data = Data();
        if (!data)
            return false;
        std::random_device device;
        Hs::PackRng const rng = Hs::MakeSeededRng((static_cast<uint64>(device()) << 32) | device());
        Hs::ArenaOpponent opponent;
        opponent.runId = run.id;
        opponent.matchNo = matchNo;
        opponent.deckClass = Hs::ArenaRandomClass(rng);
        // The run's own quest snapshot: the same pool the player drafts from. No quest card: the shared pool.
        Hs::ArenaQuestSet const owned = run.QuestSet();
        Hs::ArenaPool scratch;
        Hs::ArenaPool const* pool = nullptr;
        if (run.questCards.empty())
        {
            auto const shared = data->pools.find(opponent.deckClass);
            pool = shared == data->pools.end() ? nullptr : &shared->second;
        }
        else
        {
            scratch = Hs::ArenaPool::FromCatalog(*data->catalog, *data->db, opponent.deckClass, &owned);
            pool = &scratch;
        }
        if (!pool)
            return false;
        opponent.deck = Hs::ArenaDraftDeck(opponent.deckClass, *pool, run.terms.rarityWeights, *data->catalog,
            *data->db, rng);
        // Like the match finder: a random NPC of the player's faction; a list without names falls back to a fixed one.
        std::string name = HsFinderOpponentName(player->GetTeam() == ALLIANCE);
        opponent.name = Hs::ClipHistoryText(name.empty() ? std::string("Arena Opponent") : name, Hs::kArenaOpponentNameBytes);
        opponent.seed = std::random_device()();
        opponent.rulesVersion = Hs::Match::kRulesVersion;
        opponent.datasetHash = Hs::kDatasetHash;
        opponent.startedAt = static_cast<int64_t>(UnixNow());
        if (!Hs::ArenaOpponentOk(opponent, *data->catalog, *data->db, &owned))
            return false;
        out = std::move(opponent);
        return true;
    }

    // Ends the controller's live match of this run without a result (retirement): the match is dropped, the effect
    // removed, the client told its match is gone.
    void EndLiveDuel(Hs::ArenaRun const& run)
    {
        if (!ControllerLive(run))
            return;
        uint32 const controller = run.controllerGuid;
        HsMatches().Drop(controller);
        HsMatchDetachArena(nullptr, controller);
        HsMatchPushGame(nullptr, controller, Hs::EncodeGameNone());
        HsArenaStore().ClearActiveMatch(run.id);
    }

    // The result of the duel `run.matchNo` goes into the run. A run that a concurrent save left Paused is resumed
    // first (the result is for the duel that was running).
    Hs::ArenaOutcome RecordDuel(uint32 accountId, Hs::ArenaRun run, Hs::ArenaDuelResult result, uint32 guid)
    {
        if (run.state == Hs::ArenaState::Paused)
        {
            Hs::ArenaOutcome const resumed = HsArena().ResumeDuel(accountId, run.id, guid);
            if (resumed.ok)
                run = resumed.run;
        }
        Hs::ArenaOutcome out = HsArena().RecordResult(accountId, run.id, run.matchNo, result, guid,
            SpecialSetsComplete(accountId), UnixNow());
        if (out.ok && !out.replay)
        {
            if (!HsArenaStore().SetResult(run.id, run.matchNo, Hs::ArenaDuelResultCode(result)))
                sLog.outError("Hearthstone: could not store the result of arena duel %u of run %u", run.matchNo, run.id);
            sLog.outString("Hearthstone: arena run %u duel %u: %s (guid %u), now %u win(s) %u loss(es)", run.id, run.matchNo,
                Hs::ArenaDuelResultCode(result), guid, out.run.wins, out.run.losses);
            if (out.run.state == Hs::ArenaState::Finished)
                SettleHook(out.run);
        }
        return out;
    }

    // A duel is gone from this process without a result: drop what it left behind.
    void Abandon(Player* player, uint32 guid, Hs::ArenaRun const& run)
    {
        HsMatches().Drop(guid);
        HsMatchDetachArena(player, guid);
        HsArenaStore().ClearActiveMatch(run.id);
    }

    std::string GmName(ChatHandler* handler)
    {
        return HsGmName(handler);
    }
}

// ---- small public pieces ------------------------------------------------------------------------------------------

std::string HsArenaWhyText(std::string const& why)
{
    static std::map<std::string, std::string> const texts = {
        { "unavailable", "The Night Watch Arena is not available right now (see the server log)." },
        { "funds", "You cannot pay the entry fee." },
        { "debit", "The entry fee could not be taken. You were not charged; try again." },
        { "commit", "The entry could not be saved. You were not charged; try again." },
        { "host", "Talk to Ezra Chatterton or Ibelin Redmoore for that." },
        { "world", "You cannot do that right now." },
        { "running", "You are already in a match." },
        { "bg", "You are in a Battlegrounds game. Leave it with /hs bg leave first." },
        { "busy", "Another character of your account is in the Night Watch Arena duel, or it is being saved. Try again in a moment." },
        { "paused", "Your Night Watch Arena run has a paused duel. Resume it." },
        { "ready", "Your Night Watch Arena run has no paused duel." },
        { "match", "The duel could not be started (see the server log)." },
        { "data", "The Night Watch Arena card data is not loaded." },
        { "no_run", "You have no Night Watch Arena run." },
        { "stale", "That is not your current Night Watch Arena run." },
        { "open_run", "You already have a Night Watch Arena run. Finish or retire it first." },
        { "special_pending", "Choose the special card of your last run first." },
        { "daily_used", "You already started a paid Night Watch Arena run today. The next one is available after the daily reset." },
        { "bad_state", "That is not possible in the current state of your run." },
        { "unpaid", "The entry fee of this run is not paid yet." },
        { "repair", "This run was saved with different game data and needs a game master." },
        { "bad_class", "That is not one of the offered classes." },
        { "not_offered", "That card is not in the current offer." },
        { "picked", "That pick was made already." },
        { "bad_card", "That card cannot be drafted." },
        { "not_special", "That is not one of the special cards." },
        { "not_missing", "You own all copies of that card already." },
        { "deck", "The drafted deck is not legal (see the server log)." },
        { "storage", "The Night Watch Arena could not save right now. Try again." },
    };
    auto const it = texts.find(why);
    return it != texts.end() ? it->second : "The Night Watch Arena refused that (" + why + ").";
}

bool HsArenaIsHost(uint32_t creatureEntry)
{
    return creatureEntry == kHostEzra || creatureEntry == kHostIbelin;
}

bool HsArenaEnabled()
{
    return g_ready.load(std::memory_order_relaxed);
}

bool HsArenaReady()
{
    return g_ready.load(std::memory_order_relaxed);
}

bool HsArenaHasDuel(uint32_t accountId)
{
    if (!g_ready.load(std::memory_order_relaxed))
        return false;
    Hs::ArenaOutcome const got = HsArena().Get(accountId);
    return got.ok && got.hasRun && (got.run.state == Hs::ArenaState::Paused || got.run.state == Hs::ArenaState::InDuel);
}

// ---- startup --------------------------------------------------------------------------------------------------------

void HsArenaStartup()
{
    g_ready.store(false, std::memory_order_relaxed);
    bool const tables = CheckHsArenaTables();
    std::shared_ptr<Hs::CardCatalog const> const catalog = HsCollections().Catalog();
    std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
    if (!tables || !catalog || !db)
    {
        sLog.outError("Hearthstone: the draft arena is unavailable (%s)",
            !tables ? "character migration 0010_hearthstone_arena.sql not applied" : "the card data is not loaded");
        return;
    }
    auto data = std::make_shared<ArenaData>();
    data->catalog = catalog;
    data->db = db;
    std::string sizes;
    for (std::string const& deckClass : Hs::DeckClasses())
    {
        auto const pool = data->pools.emplace(deckClass, Hs::ArenaPool::FromCatalog(*catalog, *db, deckClass)).first;
        sizes += (sizes.empty() ? "" : ", ") + deckClass + " " + std::to_string(pool->second.Size());
    }
    HsArena().SetData(catalog, db, Hs::kDatasetHash);
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        g_data = data;
    }
    HsSetArenaDuelSink(&HsArenaStore());
    // A fresh process has no live duel: whatever a crash left in state duel is paused now (resume replays its log).
    uint32_t recovered = 0;
    if (!HsArenaStore().RecoverDuels(recovered))
    {
        sLog.outError("Hearthstone: the draft arena could not read its runs; it stays off");
        HsSetArenaDuelSink(nullptr);
        return;
    }
    g_ready.store(true, std::memory_order_relaxed);
    sLog.outString("Hearthstone: draft arena enabled, pools %s%s",
        sizes.c_str(), recovered ? (", " + std::to_string(recovered) + " duel(s) left by a crash paused").c_str() : "");
}

// ---- state ----------------------------------------------------------------------------------------------------------

HsArenaReply HsArenaGet(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_relaxed))
        return Refusal("unavailable");
    return Reply(HsArena().Get(accountId));
}

// ---- entry ----------------------------------------------------------------------------------------------------------

namespace
{
    // The caller holds g_mutex. A run that is open or reserved answers "open_run" first, which is not a used slot.
    bool HsArenaDailySlotUsedLocked(uint32 accountId)
    {
        std::string why;
        return !HsArena().EntryAvailable(accountId, Hs::ArenaPay::Gold, UnixNow(),
            sHearthstoneConfig.GetEconomy().traderSwapSeconds, why) && why == "daily_used";
    }
}

bool HsArenaDailySlotUsed(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_ready.load(std::memory_order_relaxed) && HsArenaDailySlotUsedLocked(accountId);
}

HsArenaReply HsArenaEnter(Player* player, Creature* host, Hs::ArenaPay pay)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_relaxed))
        return Refusal("unavailable");
    uint32 const accountId = AccountOf(player);
    if (!accountId || !host || !HsArenaIsHost(host->GetEntry()))
        return Refusal("host");

    Hs::ArenaSettings const settings = sHearthstoneConfig.GetArena();
    // The run the account has, if any: a reserved run still owes its debit, and is paid with ITS method.
    Hs::ArenaOutcome const current = HsArena().Get(accountId);
    bool const owed = current.ok && current.run.Open() && !current.run.entryConfirmed;
    if (!player->IsAlive())
        return Refusal("world", "You cannot enter while you are dead.");
    if (pay == Hs::ArenaPay::Practice && !(current.ok && current.run.Open()) && !HsArenaDailySlotUsedLocked(accountId))
        return Refusal("daily_open", "Your first run of the day is the one that pays: enter it first, practice runs come after it.");
    if (!owed && (pay == Hs::ArenaPay::Gold || pay == Hs::ArenaPay::Coins))
    {
        // Funds first: nothing is reserved for a character that cannot pay.
        if (pay == Hs::ArenaPay::Gold && player->GetMoney() < settings.entryCostCopper)
            return Refusal("funds", "You need " + GoldText(settings.entryCostCopper) + " to enter.");
        if (pay == Hs::ArenaPay::Coins && HsCoinCount(player) < settings.entryCostCoins)
            return Refusal("funds", "You need " + std::to_string(settings.entryCostCoins) +
                " Hearthstone Coins in your bags to enter.");
    }

    // The entry snapshot of the owned quest cards: read now, fixed in the run.
    Hs::Collection collection;
    if (!HsCollections().Get(accountId, collection))
        return Refusal("storage");
    Hs::ArenaStartRequest request;
    request.accountId = accountId;
    request.pay = pay;
    request.payingGuid = player->GetGUIDLow();
    request.settings = settings;
    request.now = UnixNow();
    request.swapSeconds = sHearthstoneConfig.GetEconomy().traderSwapSeconds;
    request.collection = &collection;
    Hs::ArenaOutcome const started = HsArena().Start(request);
    if (!started.ok || !started.needsDebit)
    {
        if (started.ok)
            sLog.outString("Hearthstone: %s arena run %u started by %s (account %u)", Hs::ArenaPayCode(pay), started.run.id,
                player->GetName(), accountId);
        return Reply(started);
    }
    return PayEntry(player, host, started.run);
}

HsArenaReply HsArenaStartFree(uint32_t accountId, uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_relaxed))
        return Refusal("unavailable");
    // The entry snapshot of the owned quest cards, as for a paid entry.
    Hs::Collection collection;
    if (!HsCollections().Get(accountId, collection))
        return Refusal("storage");
    Hs::ArenaStartRequest request;
    request.accountId = accountId;
    request.pay = Hs::ArenaPay::Free;
    request.payingGuid = guid;
    request.settings = sHearthstoneConfig.GetArena();
    request.now = UnixNow();
    request.swapSeconds = sHearthstoneConfig.GetEconomy().traderSwapSeconds;
    request.collection = &collection;
    return Reply(HsArena().Start(request));
}

// ---- drafting -------------------------------------------------------------------------------------------------------

HsArenaReply HsArenaChooseClass(uint32_t accountId, uint32_t runId, std::string const& deckClass)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_relaxed))
        return Refusal("unavailable");
    return Reply(HsArena().ChooseClass(accountId, runId, deckClass));
}

HsArenaReply HsArenaPick(uint32_t accountId, uint32_t runId, uint32_t pickNo, std::string const& cardId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_relaxed))
        return Refusal("unavailable");
    return Reply(HsArena().Pick(accountId, runId, pickNo, cardId));
}

// ---- duels ----------------------------------------------------------------------------------------------------------

HsArenaReply HsArenaStartDuel(Player* player, Creature* host, uint32_t runId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string text;
    if (char const* block = DuelBlock(player, host, text))
        return Refusal(block, text);
    uint32 const guid = player->GetGUIDLow();
    uint32 const accountId = AccountOf(player);
    Hs::ArenaOutcome const current = Current(accountId, runId);
    if (!current.ok)
        return Reply(current);
    Hs::ArenaRun const run = current.run;
    if (!run.entryConfirmed || run.state != Hs::ArenaState::Ready)
        return WrongState(run);

    // The opponent first, and stored: a crash or a relog from here on plays this one, never a new draw.
    uint32 const matchNo = run.matchNo + 1;
    bool found = false;
    Hs::ArenaOpponent opponent;
    std::shared_ptr<ArenaData const> const data = Data();
    if (!data || !HsArenaStore().LoadOpponent(run.id, matchNo, found, opponent))
        return Refusal("storage", std::string(), &run);
    Hs::ArenaQuestSet const owned = run.QuestSet();
    if (found && !Hs::ArenaOpponentOk(opponent, *data->catalog, *data->db, &owned))
    {
        sLog.outError("Hearthstone: the stored opponent of arena run %u duel %u is not playable; the run goes to repair",
            run.id, matchNo);
        Hs::ArenaOutcome const marked = HsArena().MarkRepair(accountId, run.id);
        return Refusal("repair", std::string(), marked.hasRun ? &marked.run : &run);
    }
    if (!found)
    {
        if (!DraftOpponent(player, run, matchNo, opponent))
        {
            sLog.outError("Hearthstone: could not draft an opponent for arena run %u duel %u", run.id, matchNo);
            return Refusal("data", std::string(), &run);
        }
        if (!HsArenaStore().SaveOpponent(opponent))
            return Refusal("storage", std::string(), &run);
        sLog.outString("Hearthstone: arena run %u duel %u opponent: %s (%s), seed %u", run.id, matchNo,
            opponent.name.c_str(), opponent.deckClass.c_str(), opponent.seed);
    }

    Hs::MatchStartParams const params = Hs::MakeArenaDuelParams(run, opponent, guid, player->GetName(), host->GetEntry());
    HsArenaStore().SetActiveMatch(run.id, matchNo);
    // A first try that stopped after some rows left them behind: the log restarts from the AI's mulligan.
    if (!HsArenaStore().ResetActions(run.id, matchNo))
    {
        HsArenaStore().ClearActiveMatch(run.id);
        return Refusal("storage", std::string(), &run);
    }
    Hs::MatchStartResult const started = HsMatches().Start(params, HsNowMs());
    if (!started.ok)
    {
        HsArenaStore().ClearActiveMatch(run.id);
        if (started.error != "running")
            sLog.outError("Hearthstone: could not create the arena duel of run %u for %s (guid %u): %s", run.id,
                player->GetName(), guid, started.error.c_str());
        return Refusal(started.error == "running" ? "running" : "match", std::string(), &run);
    }
    // The AI's opening mulligan is stored before anything is shown.
    if (!HsArenaStore().StoreActions(guid, run.id, 0, started.logged))
    {
        Abandon(player, guid, run);
        return Refusal("storage", std::string(), &run);
    }
    if (!HsMatchAttachArena(player, host))
    {
        Abandon(player, guid, run);
        sLog.outError("Hearthstone: the In Card Match effect could not be applied to %s (guid %u); the arena duel of run "
            "%u was dropped", player->GetName(), guid, run.id);
        return Refusal("match", "The duel could not be started (the In Card Match effect failed).", &run);
    }
    Hs::ArenaOutcome const begun = HsArena().BeginDuel(accountId, run.id, guid);
    if (!begun.ok)
    {
        Abandon(player, guid, run);
        return Reply(begun);
    }
    HsWeeklyMatchStarted(player, started.matchId, false, run.id, matchNo, run.terms.pay == Hs::ArenaPay::Practice);
    HsMatchPushGame(player, guid, started.payload);
    sLog.outString("Hearthstone: arena run %u duel %u started: %s (guid %u) vs %s (%s), seed %u", run.id, matchNo,
        player->GetName(), guid, opponent.name.c_str(), opponent.deckClass.c_str(), opponent.seed);
    HsArenaReply reply = Reply(begun);
    reply.payload = started.payload;
    return reply;
}

HsArenaReply HsArenaResumeDuel(Player* player, Creature* host, uint32_t runId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string text;
    if (char const* block = DuelBlock(player, host, text))
        return Refusal(block, text);
    uint32 const guid = player->GetGUIDLow();
    uint32 const accountId = AccountOf(player);
    Hs::ArenaOutcome const current = Current(accountId, runId);
    if (!current.ok)
        return Reply(current);
    Hs::ArenaRun const run = current.run;
    // One controller at a time: a duel in state duel belongs to the character whose match runs (or is being saved).
    if (!run.entryConfirmed || run.state != Hs::ArenaState::Paused)
        return WrongState(run);

    std::shared_ptr<ArenaData const> const data = Data();
    bool found = false;
    Hs::ArenaOpponent opponent;
    std::vector<std::string> rows;
    if (!data || !HsArenaStore().LoadOpponent(run.id, run.matchNo, found, opponent) ||
        !HsArenaStore().LoadActions(run.id, run.matchNo, rows))
        return Refusal("storage", std::string(), &run);
    // A duel the data cannot rebuild is kept for an administrator, never discarded and never scored.
    auto const repair = [&](char const* what)
    {
        sLog.outError("Hearthstone: the paused duel %u of arena run %u cannot be restored (%s); the run goes to repair",
            run.matchNo, run.id, what);
        Hs::ArenaOutcome const marked = HsArena().MarkRepair(accountId, run.id);
        return Refusal("repair", std::string(), marked.hasRun ? &marked.run : &run);
    };
    Hs::ArenaQuestSet const owned = run.QuestSet();
    if (!found || !Hs::ArenaOpponentOk(opponent, *data->catalog, *data->db, &owned))
        return repair("the stored opponent is missing or not playable");
    if (opponent.rulesVersion != Hs::Match::kRulesVersion || opponent.datasetHash != Hs::kDatasetHash)
        return repair("it was recorded with another rules version or card dataset");
    std::vector<Hs::LoggedAction> log;
    std::size_t failedAt = 0;
    std::string error;
    if (!Hs::DecodeArenaLog(rows, log, failedAt, error))
        return repair(("log row " + std::to_string(failedAt) + " does not decode: " + error).c_str());

    Hs::MatchStartParams const params = Hs::MakeArenaDuelParams(run, opponent, guid, player->GetName(), host->GetEntry());
    Hs::SuspendedDuel const duel = Hs::MakeSuspendedDuel(params, log, opponent);
    HsArenaStore().SetActiveMatch(run.id, run.matchNo);
    Hs::MatchRestoreResult const restored = HsMatches().Restore(duel, HsNowMs());
    if (!restored.ok)
    {
        HsArenaStore().ClearActiveMatch(run.id);
        if (restored.error == "running")
            return Refusal("running", std::string(), &run);
        if (restored.error == "rules" || restored.error == "dataset" || restored.error == "log")
            return repair(("the engine refused the log: " + restored.error).c_str());
        sLog.outError("Hearthstone: could not restore the arena duel of run %u for %s (guid %u): %s", run.id,
            player->GetName(), guid, restored.error.c_str());
        return Refusal("match", std::string(), &run);
    }

    HsWeeklyMatchStarted(player, restored.matchId, false, run.id, run.matchNo, run.terms.pay == Hs::ArenaPay::Practice, true);
    if (restored.ended)
        HsWeeklyMatchEnded(restored.end);
    // The log may replay to the end of the duel: the server died after the last action was stored and before the
    // result was recorded. The result is recorded now and the final state shown; there is nothing to protect.
    Hs::MatchInfo info;
    Hs::ArenaDuelResult ended = Hs::ArenaDuelResult::Loss;
    if (HsMatches().Info(guid, info) && !info.running)
    {
        if (!Hs::ArenaDuelResultOf(info.res, ended))
        {
            Abandon(player, guid, run);
            return Refusal("match", std::string(), &run);
        }
        Hs::ArenaOutcome const resumed = HsArena().ResumeDuel(accountId, run.id, guid);
        if (!resumed.ok)
        {
            Abandon(player, guid, run);
            return Reply(resumed);
        }
        Hs::ArenaOutcome const recorded = RecordDuel(accountId, resumed.run, ended, guid);
        HsArenaStore().ClearActiveMatch(run.id);
        HsMatchPushGame(player, guid, restored.payload);
        sLog.outString("Hearthstone: arena run %u duel %u had ended before the server stopped: %s", run.id, run.matchNo,
            info.res.c_str());
        HsArenaReply reply = Reply(recorded);
        reply.payload = restored.payload;
        return reply;
    }

    if (!HsMatchAttachArena(player, host))
    {
        Abandon(player, guid, run);
        sLog.outError("Hearthstone: the In Card Match effect could not be applied to %s (guid %u); the arena duel of run "
            "%u stays paused", player->GetName(), guid, run.id);
        return Refusal("match", "The duel could not be resumed (the In Card Match effect failed).", &run);
    }
    Hs::ArenaOutcome const resumed = HsArena().ResumeDuel(accountId, run.id, guid);
    if (!resumed.ok)
    {
        Abandon(player, guid, run);
        return Reply(resumed);
    }
    // The saved remainder of the turn timer is in the match now; a later crash starts that turn's timer afresh.
    HsArenaStore().SetTimer(run.id, run.matchNo, 0);
    HsMatchPushGame(player, guid, restored.payload);
    sLog.outString("Hearthstone: arena run %u duel %u resumed by %s (guid %u) after %u action(s)", run.id, run.matchNo,
        player->GetName(), guid, static_cast<uint32>(log.size()));
    HsArenaReply reply = Reply(resumed);
    reply.payload = restored.payload;
    return reply;
}

HsArenaReply HsArenaRetire(uint32_t accountId, uint32_t guid, uint32_t runId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_relaxed))
        return Refusal("unavailable");
    Hs::ArenaOutcome const current = Current(accountId, runId);
    if (!current.ok)
        return Reply(current);
    // A duel in progress ends without a result: retirement is not a loss, and a pause is not a retirement.
    EndLiveDuel(current.run);
    Hs::ArenaOutcome const retired = HsArena().Retire(accountId, current.run.id, guid, SpecialSetsComplete(accountId),
        UnixNow());
    if (retired.ok && !retired.replay)
    {
        sLog.outString("Hearthstone: arena run %u retired by guid %u with %u win(s)", current.run.id, guid, retired.run.wins);
        SettleHook(retired.run);
    }
    return Reply(retired);
}

void HsArenaDuelEnded(Hs::MatchEnd const& end)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32 const runId = static_cast<uint32>(end.arenaRunId);
    uint32 const accountId = end.meta.accountId;
    if (!g_ready.load(std::memory_order_relaxed))
        return;
    Hs::ArenaOutcome const current = HsArena().Get(accountId);
    if (!current.ok || current.run.id != runId)
    {
        sLog.outError("Hearthstone: the arena duel of run %u (guid %u) ended (%s) but the account's run is not that run (%s)",
            runId, end.guid, end.res.c_str(), current.ok ? "another run" : current.why.c_str());
        HsArenaStore().ClearActiveMatch(runId);
        return;
    }
    Hs::ArenaDuelResult result = Hs::ArenaDuelResult::Loss;
    if (!Hs::ArenaDuelResultOf(end.res, result))
    {
        // No result (the engine aborted the match after a refused action): every accepted action is stored, so the
        // run is paused and the duel resumes from there.
        sLog.outError("Hearthstone: the arena duel of run %u (guid %u) ended without a result (%s, %s); the run is paused",
            runId, end.guid, end.res.c_str(), end.why.c_str());
        if (current.run.state == Hs::ArenaState::InDuel)
            HsArena().PauseDuel(accountId, runId);
        HsArenaStore().ClearActiveMatch(runId);
        return;
    }
    RecordDuel(accountId, current.run, result, end.guid);
    HsArenaStore().ClearActiveMatch(runId);
}

// ---- GM commands -----------------------------------------------------------------------------------------------------

void HsArenaUsage(ChatHandler* handler)
{
    handler->SendSysMessage("Usage: .hs arena show <player|#accountId>");
    handler->SendSysMessage("       .hs arena start <player> free   (an unpaid run for playtesting the draft and the AI)");
    handler->SendSysMessage("       .hs arena retire <player|#accountId>");
    handler->SendSysMessage("       .hs arena repair <player|#accountId>   (rebinds a run in repair to the current rules and data, or "
        "pauses a duel stuck in progress)");
}

namespace
{
    std::string DescribeRun(Hs::ArenaRun const& run)
    {
        std::string text = "run #" + std::to_string(run.id) + " of account #" + std::to_string(run.accountId) + ": " +
            Hs::ArenaStateCode(run.state);
        if (run.state == Hs::ArenaState::Repair)
            text += " (from " + std::string(Hs::ArenaStateCode(run.repairFrom)) + ")";
        text += ", class " + (run.deckClass.empty() ? std::string("-") : run.deckClass) + ", " + std::to_string(run.wins) +
            " win(s) (" + std::to_string(run.draws) + " draw(s)), " + std::to_string(run.losses) + " loss(es), duel " +
            std::to_string(run.matchNo) + " (result recorded for " + std::to_string(run.resultNo) + "), " +
            std::to_string(run.Deck().size()) + "/" + std::to_string(Hs::kArenaDraftPicks) + " cards";
        text += std::string(", entry ") + Hs::ArenaPayCode(run.terms.pay) + " " + std::to_string(run.terms.payAmount) +
            (run.entryConfirmed ? " (paid)" : " (NOT PAID YET)") + " by guid " + std::to_string(run.payingGuid);
        if (run.slotPeriod)
            text += ", daily slot " + std::to_string(run.slotPeriod);
        text += ", terms " + std::to_string(run.terms.basePacks) + " base pack(s) + " + std::to_string(run.terms.bonusCoins) +
            " bonus coin(s), rules v" + std::to_string(run.terms.rulesVersion) + ", dataset " + run.terms.datasetHash;
        if (run.controllerGuid)
            text += ", controller guid " + std::to_string(run.controllerGuid);
        if (run.state == Hs::ArenaState::Finished || run.state == Hs::ArenaState::Retired || run.state == Hs::ArenaState::Settled)
            text += std::string(", ") + (run.retired ? "retired" : "ended") + " by guid " + std::to_string(run.endedGuid) +
                ", prize " + std::to_string(run.prize.packs) + " pack(s) + " + std::to_string(run.prize.coins) + " coin(s)" +
                (run.prize.specialChoice ? " + a special card" : "") +
                (run.specialPending ? " (special card choice pending)" : "") +
                (run.specialCard.empty() ? std::string() : ", claimed " + run.specialCard + " x" + std::to_string(run.specialCopies));
        return text;
    }

    // The character a GM names ("<name>"): guid and account.
    bool ResolveCharacter(ChatHandler* handler, std::string const& arg, uint32& guid, uint32& accountId, std::string& name)
    {
        name = arg;
        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage("Hearthstone: '" + arg + "' is not a valid character name.");
            return false;
        }
        std::string escaped = name;
        CharacterDatabase.escape_string(escaped);
        std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
            "SELECT `guid`, `account` FROM `characters` WHERE `name` = '%s'", escaped.c_str()));
        if (!row)
        {
            handler->SendSysMessage("Hearthstone: no character named " + name + ".");
            return false;
        }
        Field* f = row->Fetch();
        guid = f[0].GetUInt32();
        accountId = f[1].GetUInt32();
        return guid != 0 && accountId != 0;
    }

    void Show(ChatHandler* handler, uint32 accountId, std::string const& label)
    {
        HsArenaReply const reply = HsArenaGet(accountId);
        if (!reply.hasRun)
        {
            handler->SendSysMessage("Hearthstone: " + label + " has no arena run" +
                (reply.why == "no_run" ? std::string(".") : " (" + reply.why + ")."));
            return;
        }
        Hs::ArenaRun const& run = reply.run;
        handler->SendSysMessage("Hearthstone: " + label + ": " + DescribeRun(run));
        if (run.matchNo == 0 || run.state == Hs::ArenaState::Settled)
            return;
        bool found = false;
        Hs::ArenaOpponent opponent;
        uint32_t actions = 0;
        if (HsArenaStore().LoadOpponent(run.id, run.matchNo, found, opponent) && found)
        {
            HsArenaStore().CountActions(run.id, run.matchNo, actions);
            handler->SendSysMessage("  duel " + std::to_string(run.matchNo) + " vs " + opponent.name + " (" + opponent.deckClass +
                ", " + std::to_string(opponent.deck.size()) + " cards), seed " + std::to_string(opponent.seed) + ", " +
                std::to_string(actions) + " action(s) logged, timer " + std::to_string(opponent.timerMs) + " ms, " +
                (opponent.result.empty() ? std::string("undecided") : opponent.result) + ", live " +
                (ControllerLive(run) ? "yes" : "no"));
        }
    }
}

void HsArenaCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
{
    std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
    bool const mutating = sub == "start" || sub == "retire" || sub == "repair";
    if ((sub != "show" && !mutating) || args.size() < 3 || (sub != "start" && args.size() != 3) ||
        (sub == "start" && (args.size() != 4 || Lower(args[3]) != "free")))
    {
        HsArenaUsage(handler);
        return;
    }
    if (mutating && security < static_cast<int>(SEC_DEVELOPER))
    {
        handler->SendSysMessage("Hearthstone: .hs arena " + sub + " needs security level 3 (developer) or higher.");
        return;
    }
    if (!g_ready.load(std::memory_order_relaxed))
    {
        handler->SendSysMessage("Hearthstone: the draft arena is unavailable (migration 0010_hearthstone_arena.sql applied? card "
            "data loaded? see the server log).");
        return;
    }

    if (sub == "start")
    {
        uint32 guid = 0, accountId = 0;
        std::string name;
        if (!ResolveCharacter(handler, args[2], guid, accountId, name))
            return;
        HsArenaReply const reply = HsArenaStartFree(accountId, guid);
        if (!reply.ok)
        {
            handler->SendSysMessage("Hearthstone: no run started for " + name + ": " + HsArenaWhyText(reply.why) + " (" +
                reply.why + ")" + (reply.hasRun ? " " + DescribeRun(reply.run) : std::string()));
            return;
        }
        handler->SendSysMessage("Hearthstone: started the unpaid arena run #" + std::to_string(reply.run.id) + " for " + name +
            " (account #" + std::to_string(accountId) + "); the class offer is open.");
        sLog.outString("Hearthstone: GM %s started the free arena run %u for %s (guid %u, account %u)", GmName(handler).c_str(),
            reply.run.id, name.c_str(), guid, accountId);
        return;
    }

    uint32 accountId = 0;
    std::string label;
    if (!HsResolveAccountTarget(handler, args[2], accountId, label))
        return;
    if (sub == "show")
    {
        Show(handler, accountId, label);
        return;
    }

    HsArenaReply const current = HsArenaGet(accountId);
    if (!current.hasRun)
    {
        handler->SendSysMessage("Hearthstone: " + label + " has no arena run.");
        return;
    }
    Hs::ArenaRun const run = current.run;
    if (sub == "retire")
    {
        // The coins of the prize go to the character that played last, else the one that paid.
        uint32 const ender = run.controllerGuid ? run.controllerGuid : run.payingGuid;
        HsArenaReply const reply = HsArenaRetire(accountId, ender, run.id);
        if (!reply.ok)
        {
            handler->SendSysMessage("Hearthstone: the run of " + label + " was not retired: " + HsArenaWhyText(reply.why) + " (" +
                reply.why + ").");
            return;
        }
        handler->SendSysMessage("Hearthstone: retired arena run #" + std::to_string(run.id) + " of " + label + " with " +
            std::to_string(reply.run.wins) + " win(s)" + (reply.replay ? " (it was retired already)." : "."));
        sLog.outString("Hearthstone: GM %s retired arena run %u of account %u", GmName(handler).c_str(), run.id, accountId);
        return;
    }

    // repair
    std::lock_guard<std::mutex> lock(g_mutex);
    Hs::ArenaOutcome fixed;
    if (run.state == Hs::ArenaState::Repair)
        fixed = HsArena().RepairRebind(accountId, run.id);
    else if (run.state == Hs::ArenaState::InDuel && !ControllerLive(run))
        fixed = HsArena().PauseDuel(accountId, run.id);
    else
    {
        handler->SendSysMessage("Hearthstone: the run of " + label + " is " + Hs::ArenaStateCode(run.state) +
            "; only a run in repair, or in a duel that no longer runs, can be repaired.");
        return;
    }
    if (!fixed.ok)
    {
        handler->SendSysMessage("Hearthstone: the run of " + label + " was not repaired: " + HsArenaWhyText(fixed.why) + " (" +
            fixed.why + ").");
        return;
    }
    handler->SendSysMessage("Hearthstone: arena run #" + std::to_string(run.id) + " of " + label + " is now " +
        Hs::ArenaStateCode(fixed.run.state) + ". A paused duel recorded with other rules or data still cannot be "
        "restored: retire the run then.");
    sLog.outString("Hearthstone: GM %s repaired arena run %u of account %u (%s -> %s)", GmName(handler).c_str(), run.id,
        accountId, Hs::ArenaStateCode(run.state), Hs::ArenaStateCode(fixed.run.state));
}

void HsArenaStatus(ChatHandler* handler)
{
    Hs::ArenaSettings const a = sHearthstoneConfig.GetArena();
    handler->SendSysMessage(std::string("Arena: ") + (!g_ready.load(std::memory_order_relaxed)
        ? "UNAVAILABLE (migration 0010 or the card data missing, see the server log)"
        : "enabled") + ", entry " + GoldText(a.entryCostCopper) + " or " +
        std::to_string(a.entryCostCoins) + " coins, base reward " + std::to_string(a.baseRewardPacks) + " packs, bonus " +
        std::to_string(a.bonusCoins) + " coins, rarity weights " + Hs::FormatArenaRarityWeights(a.rarityWeights) + ", " +
        std::to_string(HsArenaStore().ActiveCount()) + " duel(s) live");
}
