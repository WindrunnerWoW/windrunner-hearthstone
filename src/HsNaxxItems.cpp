#include "HsNaxxItems.h"

#include "Chat.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HsCollectionStore.h"
#include "HsEconomyGlue.h"
#include "HsNaxxStore.h"
#include "quests/HsNaxxQuestData.h"

#include <cstdint>
#include <set>
#include <string>

namespace
{
    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    // The card item of the quest table, or null.
    Hs::NaxxItem const* FindCardItem(uint32_t entry)
    {
        for (Hs::NaxxQuest const& quest : Hs::kNaxxQuests)
            for (uint32_t i = 0; i < quest.itemCount; ++i)
                if (quest.items[i].entry == entry)
                    return &quest.items[i];
        return nullptr;
    }

    void UseUp(Player* player, Item* item)
    {
        uint32 one = 1;
        player->DestroyItemCount(item, one, true);
    }
}

bool HsNaxxHandleOpenItem(Player* player, WorldPacket const& packet)
{
    if (!player || !player->GetSession() || packet.size() < 2)
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    uint8 bag = 0, slot = 0;
    copy >> bag >> slot;
    Item* item = player->GetItemByPos(bag, slot);
    if (!item)
        return false;
    uint32_t const entry = item->GetEntry();
    Hs::NaxxItem const* const card = FindCardItem(entry);
    if (!card)
        return false;

    // From here on the item is ours (it has no loot of its own).
    if (item->IsInTrade())
        return true;
    uint32_t const accountId = player->GetSession()->GetAccountId();
    uint32_t const guidLow = player->GetGUIDLow();
    std::string name = card->cardId;
    if (ItemPrototype const* const proto = item->GetProto())
        name = proto->Name1;
    std::string const source = "item:" + std::to_string(entry) + ":" + std::to_string(guidLow);

    std::set<uint32_t> claims;
    if (!accountId || !HsNaxxLoadClaims(accountId, claims))
    {
        SysMessage(player, "your collection is unavailable right now. Try again in a moment.");
        return true;
    }

    if (claims.count(entry) == 0)
    {
        // The marker and the card commit together; a failed commit keeps the item.
        auto commit = [&](Hs::Mutation const& m) { return HsNaxxCommitClaim(accountId, m, entry, guidLow); };
        Hs::MutationResult const g = HsCollections().GrantVia(accountId, card->cardId, card->count,
            Hs::GrantSource::Quest, Hs::ProtectionMode::Reward, "naxx_reward", source, commit);
        bool added = g.status == Hs::MutationStatus::Ok;
        uint32_t version = g.mutation.versionAfter;
        if (!added)
        {
            // A commit reported as failed may still have committed: the marker says so. The service cache did not
            // apply it, so drop the cache and push the version read back from the database.
            std::set<uint32_t> after;
            added = HsNaxxLoadClaims(accountId, after) && after.count(entry) != 0;
            if (added)
            {
                HsCollections().Evict(accountId);
                Hs::Collection reread;
                version = HsCollections().Get(accountId, reread) ? reread.version : 0;
            }
        }
        if (!added)
        {
            sLog.outError("Hearthstone: %s (account %u) could not claim naxx item %u (%s): status %d",
                player->GetName(), accountId, entry, card->cardId, static_cast<int>(g.status));
            SysMessage(player, "the cards could not be added right now. The item stays in your bag; try again later.");
            return true;
        }
        UseUp(player, item);
        SysMessage(player, name + " added to your collection.");
        sLog.outString("Hearthstone: %s (account %u) used naxx item %u (%s)", player->GetName(), accountId, entry,
            card->cardId);
        if (version)
            HsPushCardChanged(accountId, version);
        return true;
    }

    // Already claimed by the account: a replay.
    if (card->replayPack)
    {
        Hs::PackStockResult const r = HsCollections().GrantPacks(accountId, 1, "naxx_replay", source);
        if (r.status != Hs::PackStatus::Ok)
        {
            SysMessage(player, "the booster pack could not be added right now. The item stays in your bag; try again later.");
            return true;
        }
        UseUp(player, item);
        SysMessage(player, "your account already has " + name + ", so you receive a booster pack instead.");
        HsPushPackChanged(accountId, r.after.stock);
        return true;
    }
    UseUp(player, item);
    SysMessage(player, "your account already has " + name + ".");
    return true;
}
