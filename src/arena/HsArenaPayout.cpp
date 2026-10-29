#include "HsArenaPayout.h"

#include <algorithm>
#include <utility>

namespace Hs
{
    ArenaCoinPlan PlanArenaCoins(uint32_t coins, uint32_t bagRoom)
    {
        ArenaCoinPlan plan;
        plan.bags = std::min(coins, bagRoom);
        uint32_t left = coins - plan.bags;
        while (left > 0)
        {
            uint32_t const n = std::min(left, kArenaMailCoinStack);
            plan.mail.push_back(n);
            left -= n;
        }
        return plan;
    }

    uint32_t ArenaCoinRecipient(ArenaRun const& run)
    {
        if (run.endedGuid)
            return run.endedGuid;
        return run.controllerGuid ? run.controllerGuid : run.payingGuid;
    }

    Collection ArenaEffectiveCollection(Collection const& collection, ArenaMilestones const& claimed)
    {
        Collection effective = collection;
        for (ArenaSpecialCard const& card : ArenaSpecialCards())
        {
            if (!claimed.count(card.id))
                continue;
            CardCount have = effective.Get(card.id);
            if (have.Total() < card.copies)
            {
                have.protectedQty += card.copies - have.Total();
                effective.Set(card.id, have);
            }
        }
        return effective;
    }

    std::vector<ArenaSpecialMissing> ArenaSpecialOptions(Collection const& collection, ArenaMilestones const& claimed)
    {
        return ArenaMissingSpecials(ArenaEffectiveCollection(collection, claimed));
    }

    namespace
    {
        std::string RunSource(uint32_t runId, char const* suffix = "")
        {
            return "arena:" + std::to_string(runId) + suffix;
        }

        ArenaSettleResult SettleFail(char const* why, ArenaRun const* run = nullptr)
        {
            ArenaSettleResult r;
            r.why = why;
            if (run)
            {
                r.run = *run;
                r.hasRun = true;
            }
            return r;
        }

        ArenaClaimResult ClaimFail(std::string const& why, ArenaRun const* run = nullptr)
        {
            ArenaClaimResult r;
            r.why = why;
            if (run)
            {
                r.run = *run;
                r.hasRun = true;
            }
            return r;
        }

        // The extra pack that replaces a special-card choice (payout kind 'convert').
        ArenaClaimResult GrantConvertPack(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                          ArenaRun const& run)
        {
            bool found = false;
            uint32_t ignored = 0;
            if (!ports.LoadPayout(run.id, kArenaPayoutConvert, found, ignored))
                return ClaimFail("storage", &run);
            ArenaClaimResult r;
            r.ok = true;
            r.convertedToPack = true;
            r.hasRun = true;
            r.run = run;
            if (found)
            {
                r.replay = true;
                return r;
            }
            bool commitFailed = false;
            auto txn = [&](PackStockChange const& change) -> bool
            {
                if (!ports.Begin())
                    return false;
                if (!ports.QueuePackChange(run.accountId, change) ||
                    !ports.QueuePayout(run.id, run.accountId, kArenaPayoutConvert, 1, 0, 0))
                {
                    ports.Rollback();
                    return false;
                }
                if (!ports.Commit())
                {
                    commitFailed = true;
                    return false;
                }
                return true;
            };
            PackStockResult const g = collections.GrantPacksVia(run.accountId, 1, "arena_prize", RunSource(run.id, ":x"), txn);
            if (g.status == PackStatus::Ok)
            {
                r.packs = 1;
                r.delivered = true;
                r.stockAfter = g.after.stock;
                return r;
            }
            if (commitFailed && ports.LoadPayout(run.id, kArenaPayoutConvert, found, ignored) && found)
            {
                r.packs = 1;
                r.delivered = true;
                r.replay = false;
                PackView view;   // the stock as stored (the failed commit dropped the cache), for the client push
                if (collections.GetPacks(run.accountId, view))
                    r.stockAfter = view.state.stock;
                return r;
            }
            (void)service;
            return ClaimFail(g.status == PackStatus::Overflow ? "overflow"
                : g.status == PackStatus::Unavailable ? "unavailable" : "storage", &run);
        }

        // The special card of a recorded claim, granted together with its milestone row (the delivery marker).
        ArenaClaimResult DeliverSpecial(CollectionService& collections, ArenaPayoutPorts& ports, ArenaRun const& run)
        {
            ArenaMilestones claimed;
            if (!ports.LoadMilestones(run.accountId, claimed))
                return ClaimFail("storage", &run);
            ArenaClaimResult r;
            r.ok = true;
            r.hasRun = true;
            r.run = run;
            r.card = run.specialCard;
            auto const it = claimed.find(run.specialCard);
            if (it != claimed.end())
            {
                if (it->second != run.id)
                    return ClaimFail("not_missing", &run);
                r.replay = true; // delivered before
                return r;
            }
            bool commitFailed = false;
            auto txn = [&](Mutation const& mutation) -> bool
            {
                if (!ports.Begin())
                    return false;
                if (!ports.QueueCardMutation(run.accountId, mutation) ||
                    !ports.QueueMilestone(run.accountId, run.specialCard, run.id))
                {
                    ports.Rollback();
                    return false;
                }
                if (!ports.Commit())
                {
                    commitFailed = true;
                    return false;
                }
                return true;
            };
            MutationResult const g = collections.GrantVia(run.accountId, run.specialCard, run.specialCopies,
                GrantSource::SpecialReward, ProtectionMode::Reward, "arena_special", RunSource(run.id), txn);
            if (g.status == MutationStatus::Ok)
            {
                r.copies = run.specialCopies;
                r.delivered = true;
                r.versionAfter = g.mutation.versionAfter;
                return r;
            }
            claimed.clear();
            if (commitFailed && ports.LoadMilestones(run.accountId, claimed) && claimed.count(run.specialCard) &&
                claimed[run.specialCard] == run.id)
            {
                r.copies = run.specialCopies;
                r.delivered = true;
                return r;
            }
            return ClaimFail(g.status == MutationStatus::Rejected ? "bad_card"
                : g.status == MutationStatus::Unavailable ? "unavailable" : "storage", &run);
        }
    }

    ArenaSettleResult SettleArenaRun(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                     uint32_t accountId, uint32_t runId)
    {
        ArenaOutcome const got = service.Get(accountId);
        if (!got.ok)
            return SettleFail(got.why.c_str());
        ArenaRun const run = got.run;
        if (runId != 0 && run.id != runId)
            return SettleFail("stale", &run);
        if (run.state == ArenaState::Settled)
        {
            ArenaSettleResult r;
            r.ok = true;
            r.replay = true;
            r.run = run;
            r.hasRun = true;
            r.specialPending = run.specialPending;
            return r;
        }
        if (run.state != ArenaState::Finished && run.state != ArenaState::Retired)
            return SettleFail("bad_state", &run);

        // The payout key exists but the run is not settled: a settlement that landed in a form that left the run
        // behind. The packs are in (same transaction as the key), so only the state is finished.
        bool found = false;
        uint32_t keyedPacks = 0;
        if (!ports.LoadPayout(run.id, kArenaPayoutRun, found, keyedPacks))
            return SettleFail("storage", &run);
        if (found)
        {
            ArenaOutcome const done = service.Settle(accountId, run.id);
            if (!done.ok)
                return SettleFail(done.why.c_str(), &run);
            ArenaSettleResult r;
            r.ok = true;
            r.replay = true;
            r.run = done.run;
            r.hasRun = true;
            r.specialPending = done.run.specialPending;
            return r;
        }

        uint32_t const packs = run.prize.packs;
        uint32_t const coins = run.prize.coins;
        uint32_t const coinGuid = ArenaCoinRecipient(run);
        if (coins > 0 && coinGuid == 0)
            return SettleFail("no_recipient", &run);

        char const* failWhy = nullptr;
        // Everything but the pack row, queued into the open transaction; nullptr = queued.
        auto queueRest = [&]() -> char const*
        {
            ArenaOutcome const settled = ports.SettleJoined(accountId, run.id);
            if (!settled.ok)
                return "storage";
            if (settled.replay)
                return "replay"; // settled by someone else since we looked: nothing may be granted
            if (!ports.QueuePayout(run.id, accountId, kArenaPayoutRun, packs, coins, coinGuid))
                return "storage";
            if (coins > 0 && !ports.QueueCoinDelivery(run.id, accountId, coinGuid, coins))
                return "storage";
            return nullptr;
        };
        auto txn = [&](PackStockChange const* change) -> bool
        {
            if (!ports.Begin())
            {
                failWhy = "storage";
                return false;
            }
            if (change && !ports.QueuePackChange(accountId, *change))
            {
                ports.Rollback();
                failWhy = "storage";
                return false;
            }
            if (char const* why = queueRest())
            {
                ports.Rollback();
                failWhy = why;
                return false;
            }
            if (!ports.Commit())
            {
                failWhy = "commit"; // ambiguous: the payout key is read back
                return false;
            }
            return true;
        };

        bool committed = false;
        uint32_t stockAfter = 0;
        PackStatus packStatus = PackStatus::Ok;
        if (packs > 0)
        {
            PackStockResult const g = collections.GrantPacksVia(accountId, packs, "arena_prize", RunSource(run.id),
                [&](PackStockChange const& change) { return txn(&change); });
            committed = g.status == PackStatus::Ok;
            packStatus = g.status;
            stockAfter = g.after.stock;
        }
        else
            committed = txn(nullptr);

        bool landed = false;
        if (!committed && failWhy && std::string(failWhy) == "commit")
        {
            bool exists = false;
            uint32_t ignored = 0;
            landed = ports.LoadPayout(run.id, kArenaPayoutRun, exists, ignored) && exists;
        }
        if (!committed && !landed)
        {
            if (failWhy && std::string(failWhy) == "replay")
            {
                ArenaOutcome const again = service.Get(accountId);
                ArenaSettleResult r;
                r.ok = again.ok && again.run.state == ArenaState::Settled;
                r.why = r.ok ? "" : "storage";
                r.replay = true;
                r.hasRun = again.ok;
                r.run = again.run;
                r.specialPending = again.run.specialPending;
                return r;
            }
            if (failWhy)
                return SettleFail(failWhy, &run);
            return SettleFail(packStatus == PackStatus::Overflow ? "overflow"
                : packStatus == PackStatus::Unavailable ? "unavailable" : "storage", &run);
        }

        ArenaSettleResult r;
        r.ok = true;
        r.landedAfterError = landed;
        r.packs = packs;
        r.coins = coins;
        r.coinGuid = coinGuid;
        r.stockAfter = stockAfter;
        ArenaOutcome const after = service.Get(accountId);
        r.hasRun = after.ok;
        r.run = after.ok ? after.run : run;
        r.specialPending = r.run.specialPending;
        return r;
    }

    ArenaClaimResult ClaimArenaSpecial(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                       uint32_t accountId, uint32_t runId, std::string const& cardId)
    {
        Collection collection;
        if (!collections.Get(accountId, collection))
            return ClaimFail("unavailable");
        ArenaMilestones claimed;
        if (!ports.LoadMilestones(accountId, claimed))
            return ClaimFail("storage");

        ArenaOutcome const o = service.ClaimSpecial(accountId, runId, cardId, ArenaEffectiveCollection(collection, claimed));
        if (!o.ok)
        {
            // A choice that became a pack before: the retry is a replay (the extra pack is finished if owed).
            if (o.why == "bad_state" && o.hasRun && o.run.state == ArenaState::Settled && !o.run.specialPending &&
                o.run.specialCard.empty() && !o.run.prize.specialChoice)
            {
                bool found = false;
                uint32_t ignored = 0;
                if (ports.LoadPayout(o.run.id, kArenaPayoutConvert, found, ignored) && found)
                {
                    ArenaClaimResult r;
                    r.ok = true;
                    r.replay = true;
                    r.convertedToPack = true;
                    r.hasRun = true;
                    r.run = o.run;
                    return r;
                }
            }
            return ClaimFail(o.why, o.hasRun ? &o.run : nullptr);
        }
        if (o.convertedToPack)
            return GrantConvertPack(service, collections, ports, o.run);
        ArenaClaimResult r = DeliverSpecial(collections, ports, o.run);
        if (r.ok && o.replay && !r.delivered)
            r.replay = true;
        return r;
    }

    ArenaClaimResult FinishArenaClaim(ArenaService& service, CollectionService& collections, ArenaPayoutPorts& ports,
                                      uint32_t accountId, uint32_t runId)
    {
        bool found = false;
        ArenaRun loaded;
        if (!ports.LoadRunForClaim(runId, found, loaded))
            return ClaimFail("storage");
        if (!found || loaded.accountId != accountId)
            return ClaimFail("no_run");
        ArenaRun const& run = loaded;
        ArenaClaimResult none;
        none.ok = true;
        none.replay = true;
        none.hasRun = true;
        none.run = run;
        if (run.state != ArenaState::Settled || run.specialPending)
            return none;
        if (!run.specialCard.empty())
            return DeliverSpecial(collections, ports, run);
        // A converted choice: the run's prize grew by the extra pack that the 'run' payout did not carry.
        uint32_t keyed = 0;
        found = false;
        if (!ports.LoadPayout(run.id, kArenaPayoutRun, found, keyed))
            return ClaimFail("storage", &run);
        if (!found || run.prize.packs <= keyed)
            return none;
        return GrantConvertPack(service, collections, ports, run);
    }
}
