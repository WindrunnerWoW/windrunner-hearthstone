/*
 * mod-hearthstone: the introduction questline glue, see HsIntroGlue.h.
 *
 * Threading. Both packet functions run on the thread that handles the character's quest packets, right before and
 * right after the core's handler (ServerScript CanPacketReceive / OnPacketHandled, like the NPC unlock turn-in hook of
 * HsMatchGlue.cpp). They touch only the character's own items and mail and call HsAwardStarter, whose services take
 * their own locks and whose pushes take the session registry mutex. g_pendingMutex is a leaf lock.
 *
 * Before / after: the reward is given only when the quest was not rewarded before the packet and is rewarded after
 * it, so a repeated or refused turn-in packet never rewards twice.
 *
 * The pack: the account row is written before the item is created, so a failure in between loses the pack rather
 * than granting two (the log line names the account; `.additem 919602` repairs it). Only one character of an account
 * can be in the world, so the read-then-insert cannot race.
 */

#include "ScriptObjects.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsDeckStore.h"
#include "HsEconomyGlue.h"
#include "HsIntroGlue.h"
#include "HsMatchGlue.h"
#include "decks/HsDecks.h"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace
{
    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    // Quest 3: one booster pack item, once per account.
    void GrantIntroPack(Player* player, uint32 questId)
    {
        uint32 const accountId = player->GetSession()->GetAccountId();
        uint32 const guid = player->GetGUIDLow();
        // A scalar head row (see HsCollectionDbStorage::Load): null means a failed query or a missing table.
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `hearthstone_account_intro` WHERE `account_id` = %u)", accountId));
        if (!head)
        {
            sLog.outError("Hearthstone: intro quest %u: cannot read hearthstone_account_intro for account %u "
                "(character migration 0008_hearthstone_intro.sql applied?); no pack given", questId, accountId);
            SysMessage(player, "your free booster pack could not be given right now. Please tell a GM.");
            return;
        }
        if (head->Fetch()[0].GetUInt64() > 0)
        {
            SysMessage(player, "your account already received its free booster pack from the introduction.");
            return;
        }
        if (!CharacterDatabase.DirectPExecute(
                "INSERT INTO `hearthstone_account_intro` (`account_id`, `character_guid`, `quest_id`) VALUES (%u, %u, %u)",
                accountId, guid, questId))
        {
            sLog.outError("Hearthstone: intro quest %u: cannot record the pack of account %u; no pack given", questId,
                accountId);
            SysMessage(player, "your free booster pack could not be given right now. Please tell a GM.");
            return;
        }

        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, kHsPackItem, 1) == EQUIP_ERR_OK)
        {
            if (Item* item = player->StoreNewItem(dest, kHsPackItem, true))
            {
                player->SendNewItem(item, 1, true, false);
                sLog.outString("Hearthstone: intro pack given to %s (account %u, quest %u) in the bags",
                    player->GetName(), accountId, questId);
                return;
            }
        }
        Item* item = Item::CreateItem(kHsPackItem, 1, player);
        if (!item)
        {
            sLog.outError("Hearthstone: intro quest %u: could not create the pack item for account %u (recorded as "
                "given: repair with .additem %u)", questId, accountId, kHsPackItem);
            return;
        }
        item->SaveToDB(); // the mail loads it from the database
        bool const horde = player->GetTeam() == HORDE;
        MailDraft("Hearthstone Booster Pack", "Your bags were full when you finished your first Hearthstone lesson. "
            "Here is your free booster pack.")
            .AddItem(item)
            .SendMailTo(MailReceiver(player), MailSender(MAIL_CREATURE, horde ? 919501u : 919502u));
        SysMessage(player, "your bags are full: your free Hearthstone Booster Pack was sent to you by mail.");
        sLog.outString("Hearthstone: intro pack mailed to %s (account %u, quest %u)", player->GetName(), accountId,
            questId);
    }
}

namespace
{
    // The gossip tutorials. The replies are npc_text rows (world migration 0010, generated from the script): the reply
    // to option n (1 to 7) is textBase + n - 1; the last one is the closing dialogue. The option labels are the
    // "Gossip Option" lines of the script; label n is the option that leads
    // to page n. The gossip action of page n is actionBase + n (sender 0x4853 'HS', see HsMatchGlue.cpp).
    constexpr uint32 kTutorialPages = 7;

    struct Tutorial
    {
        uint32 creatures[2];        // the NPCs that tell it (0 = none)
        uint32 questHorde;          // the quest it completes, by the team of the character
        uint32 questAlliance;
        uint32 actionBase;
        uint32 textBase;
        char const* log;
        char const* options[kTutorialPages];
    };

    Tutorial const kTutorials[] = {
        // Byron: the Card Master's trade.
        { { 919505, 0 }, kHsIntroTradeHorde, kHsIntroTradeAlliance, 0x48530040u, 9195101u, "Byron's trade",
          { "So what do you actually do?",
            "What can I do with cards I don't need?",
            "Alright. What's Arcane Dust for?",
            "So I can choose a specific card?",
            "How did you get into all this?",
            "Got any advice for me?",
            "I'll be back." } },
        // Ezra and Ibelin: the lesson.
        { { 919501, 919502 }, kHsIntroLessonHorde, kHsIntroLessonAlliance, 0x48530050u, 9195111u, "the Hearthstone lesson",
          { "Alright. How does Hearthstone work?",
            "What happens once the game starts?",
            "Tell me about minions.",
            "Can minions attack the enemy Hero?",
            "What about spells and the other cards?",
            "Anything else I should know?",
            "I think I've got it." } },
    };

    uint32 QuestOf(Tutorial const& t, Player* player)
    {
        return player->GetTeam() == HORDE ? t.questHorde : t.questAlliance;
    }

    // The tutorial the creature tells to the player now (its quest is accepted and not complete), or null.
    Tutorial const* ActiveTutorial(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return nullptr;
        for (Tutorial const& t : kTutorials)
            if ((creature->GetEntry() == t.creatures[0] || creature->GetEntry() == t.creatures[1]) &&
                player->GetQuestStatus(QuestOf(t, player)) == QUEST_STATUS_INCOMPLETE)
                return &t;
        return nullptr;
    }

    void AddTutorialOption(Tutorial const& t, GossipMenu& menu, uint32 page, uint32 sender)
    {
        menu.AddMenuItem(GOSSIP_ICON_CHAT, t.options[page - 1], sender, t.actionBase + page, "", false);
        // The core reads the parallel action data vector unchecked: keep it aligned.
        menu.AddGossipMenuItemData(0, 0, 0);
    }
}

namespace
{
    // The class deck hint: once Byron's trade is turned in (the whole introduction is done), Ezra and Ibelin answer where
    // to learn the other classes. The option opens a class list (npc_text listText); every class option answers with its
    // own story that names the teacher and the place (npc_text classTextFirst + class index) and shows the list again.
    // The npc_text rows are in world migration 0018, the class order is the order of the deck items (kClassDecks).
    constexpr char const* kClassHintOption = "Where can I learn to play other class decks?";
    constexpr uint32 kClassHintAction = 0x485300A0u;
    constexpr uint32 kClassHintClassFirst = 0x485300C0u; // + class index, 0x485300C0-0x485300C8
    constexpr uint32 kClassHintClassCount = 9;
    char const* const kClassHintClassOptions[kClassHintClassCount] = {
        "I want to learn Warrior.", "I want to learn Paladin.", "I want to learn Hunter.",
        "I want to learn Rogue.",   "I want to learn Priest.",  "I want to learn Shaman.",
        "I want to learn Mage.",    "I want to learn Warlock.", "I want to learn Druid."};

    struct ClassHint
    {
        uint32 creature;
        uint32 listText;
        uint32 classTextFirst;
    };

    ClassHint const kClassHints[] = {
        { 919501, 9195150, 9195700 }, // Ezra: Zerok, Drin Lia, Boro, For'sen, Jeffrey, Annie, Marie, Varka, Kolento
        { 919502, 9195151, 9195710 }, // Ibelin: Steeg, Rowan, Fenn, Matt and Alex, Alenya, John, Mitchell, Maren, Thijs
    };

    // The class of each class deck item (kHsClassDeckItemFirst + index): the order of world migration 0011, which is
    // also the order of the class options above.
    char const* const kClassDecks[kHsClassDeckItemCount] = {
        "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "SHAMAN", "MAGE", "WARLOCK", "DRUID"};

    // The classes this character can already play: its own class and every class the account unlocked. A failed read
    // counts as none, so every class stays on the list.
    std::set<std::string> PlayableClassesOf(Player* player)
    {
        Hs::DeckBook book;
        if (!player || !player->GetSession() || !HsDecks().GetBook(player->GetSession()->GetAccountId(), book))
            return {};
        return Hs::PlayableClasses(Hs::DeckClassOfWowClass(player->getClass()), book.UnlockedClasses());
    }

    // The hint of the creature, or null when it has none, the introduction is not done or no class is missing any more.
    ClassHint const* ClassHintOf(Player* player, Creature* creature)
    {
        if (!player || !creature || !HsCardMasterDone(player))
            return nullptr;
        for (ClassHint const& hint : kClassHints)
            if (creature->GetEntry() == hint.creature)
            {
                std::set<std::string> const playable = PlayableClassesOf(player);
                for (char const* cls : kClassDecks)
                    if (!playable.count(cls))
                        return &hint;
                return nullptr;
            }
        return nullptr;
    }

    // One option per class the character cannot play yet. The core reads the parallel action data vector unchecked:
    // keep it aligned.
    void AddClassHintOptions(GossipMenu& menu, uint32 sender, std::set<std::string> const& playable)
    {
        for (uint32 i = 0; i < kClassHintClassCount && menu.MenuItemCount() < GOSSIP_MAX_MENU_ITEMS; ++i)
        {
            if (playable.count(kClassDecks[i]))
                continue;
            menu.AddMenuItem(GOSSIP_ICON_CHAT, kClassHintClassOptions[i], sender, kClassHintClassFirst + i, "", false);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
    }
}

bool HsIntroHandleOpenItem(Player* player, WorldPacket const& packet)
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
    uint32 const entry = item->GetEntry();
    bool const starter = entry == kHsStarterDeckItem;
    bool const classDeck = entry >= kHsClassDeckItemFirst && entry < kHsClassDeckItemFirst + kHsClassDeckItemCount;
    if (!starter && !classDeck)
        return false;

    // From here on the item is ours (it has no loot of its own).
    if (item->IsInTrade())
        return true;
    uint32 const accountId = player->GetSession()->GetAccountId();
    std::string const cls = starter ? Hs::DeckClassOfWowClass(player->getClass())
                                    : std::string(kClassDecks[entry - kHsClassDeckItemFirst]);
    if (cls.empty())
    {
        SysMessage(player, "your class has no Hearthstone starter deck.");
        return true;
    }
    std::string text;
    std::string const source = "item:" + std::to_string(entry) + ":" + std::to_string(player->GetGUIDLow());
    // The starter deck: the cards and the deck of this character's class (a class is never unlocked by it). A class
    // deck: the class for the whole account, then its starter deck. Both are idempotent; a failure keeps the item.
    bool const ok = starter ? HsAwardStarter(accountId, cls, source, text) : HsUnlockClass(accountId, cls, source, text);
    if (!ok)
        SysMessage(player, text);  // a success is silent: the item is used up and the class shows in the collection
    else
    {
        uint32 one = 1;
        player->DestroyItemCount(item, one, true);
        sLog.outString("Hearthstone: %s (account %u) used item %u (%s)", player->GetName(), accountId, entry, cls.c_str());
    }
    return true;
}

bool HsIntroTutorialActive(Player* player, Creature* creature)
{
    return ActiveTutorial(player, creature) != nullptr || ClassHintOf(player, creature) != nullptr;
}

void HsIntroAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (Tutorial const* const t = ActiveTutorial(player, creature))
        if (menu.MenuItemCount() < GOSSIP_MAX_MENU_ITEMS)
            AddTutorialOption(*t, menu, 1, sender);
}

void HsIntroAppendClassHint(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (ClassHintOf(player, creature) && menu.MenuItemCount() < GOSSIP_MAX_MENU_ITEMS)
    {
        menu.AddMenuItem(GOSSIP_ICON_CHAT, kClassHintOption, sender, kClassHintAction, "", false);
        // The core reads the parallel action data vector unchecked: keep it aligned.
        menu.AddGossipMenuItemData(0, 0, 0);
    }
}

bool HsIntroHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action == kClassHintAction ||
        (action >= kClassHintClassFirst && action < kClassHintClassFirst + kClassHintClassCount))
    {
        PlayerMenu* const hintTalk = player ? player->PlayerTalkClass : nullptr;
        if (!hintTalk)
            return true;
        ClassHint const* const hint = ClassHintOf(player, creature);
        if (!hint)
        {
            hintTalk->CloseGossip();
            return true;
        }
        hintTalk->ClearMenus();
        AddClassHintOptions(hintTalk->GetGossipMenu(), sender, PlayableClassesOf(player));
        uint32 const text = action == kClassHintAction ? hint->listText
                                                       : hint->classTextFirst + (action - kClassHintClassFirst);
        hintTalk->SendGossipMenu(text, creature->GetObjectGuid());
        return true;
    }
    Tutorial const* t = nullptr;
    for (Tutorial const& candidate : kTutorials)
        if (action > candidate.actionBase && action <= candidate.actionBase + kTutorialPages)
            t = &candidate;
    if (!t)
        return false;
    PlayerMenu* talk = player ? player->PlayerTalkClass : nullptr;
    if (!talk)
        return true;
    if (ActiveTutorial(player, creature) != t)
    {
        talk->CloseGossip();
        return true;
    }
    uint32 const page = action - t->actionBase;
    talk->ClearMenus();
    if (page < kTutorialPages)
        AddTutorialOption(*t, talk->GetGossipMenu(), page + 1, sender);
    else
    {
        // The last option: the quest is complete, and the turn-in is offered right here.
        uint32 const quest = QuestOf(*t, player);
        player->AreaExploredOrEventHappens(quest);
        player->PrepareQuestMenu(creature->GetObjectGuid());
        sLog.outString("Hearthstone: %s (account %u) finished the gossip tutorial %s (quest %u)", player->GetName(),
            player->GetSession()->GetAccountId(), t->log, quest);
    }
    talk->SendGossipMenu(t->textBase + page - 1, creature->GetObjectGuid());
    return true;
}

namespace
{
    // Account -> has a character turned in the card quest (HsIntroDone).
    std::mutex g_doneMutex;
    std::unordered_map<uint32, bool> g_done;
    // Account -> has a character turned in Byron's trade quest (HsCardMasterDone).
    std::unordered_map<uint32, bool> g_masterDone;
    // Account -> has a character completed the lesson quest (HsLessonDone).
    std::unordered_map<uint32, bool> g_lessonDone;

    void SetDone(uint32 accountId)
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        g_done[accountId] = true;
    }

    void SetMasterDone(uint32 accountId)
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        g_masterDone[accountId] = true;
    }

    // Character guid -> the intro quest of a turn-in packet that was not rewarded before the core handled it.
    std::mutex g_pendingMutex;
    std::unordered_map<uint32, uint32> g_pending;

    bool IsIntroRewardQuest(uint32 id)
    {
        return id == kHsIntroCardsHorde || id == kHsIntroCardsAlliance || id == kHsIntroLessonHorde ||
            id == kHsIntroLessonAlliance || id == kHsIntroTradeHorde || id == kHsIntroTradeAlliance;
    }

    // The quest id of CMSG_QUESTGIVER_CHOOSE_REWARD (giver guid, quest, reward choice), or 0.
    uint32 TurnInQuest(WorldPacket const& packet)
    {
        if (packet.size() < sizeof(uint64) + sizeof(uint32))
            return 0;
        return packet.read<uint32>(sizeof(uint64));
    }
}

void HsIntroBeforeTurnIn(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || !player->IsInWorld() || !sHearthstoneConfig.Get().enable)
        return;
    uint32 const quest = TurnInQuest(packet);
    if (!IsIntroRewardQuest(quest) || player->GetQuestRewardStatus(quest))
        return;
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pending[player->GetGUIDLow()] = quest;
}

void HsIntroAfterTurnIn(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player)
        return;
    uint32 quest = 0;
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        auto const it = g_pending.find(player->GetGUIDLow());
        if (it == g_pending.end())
            return;
        quest = it->second;
        g_pending.erase(it);
    }
    if (quest != TurnInQuest(packet) || !player->IsInWorld() || !player->GetQuestRewardStatus(quest))
        return;
    if (quest == kHsIntroCardsHorde || quest == kHsIntroCardsAlliance)
    {
        // The deck comes with the reward item "Hearthstone Starter Deck" (the core gives it, right-click uses it).
        SysMessage(player, "right-click your Hearthstone Starter Deck to receive the starter deck of your class.");
        uint32 const accountId = player->GetSession()->GetAccountId();
        SetDone(accountId);
        HsPushIntroDone(accountId);  // the addon now shows its micro menu button
    }
    else if (quest == kHsIntroTradeHorde || quest == kHsIntroTradeAlliance)
        SetMasterDone(player->GetSession()->GetAccountId());
    else
        GrantIntroPack(player, quest);
}

bool HsIntroDone(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        auto const it = g_done.find(accountId);
        if (it != g_done.end())
            return it->second;
    }
    bool done = player->GetQuestRewardStatus(kHsIntroCardsHorde) || player->GetQuestRewardStatus(kHsIntroCardsAlliance);
    if (!done)
    {
        // A scalar head row: null means a failed query; then nothing is cached and the next frame asks again.
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `character_queststatus` q JOIN `characters` c ON c.`guid` = q.`guid` "
            "WHERE c.`account` = %u AND q.`rewarded` = 1 AND q.`quest` IN (%u, %u))",
            accountId, kHsIntroCardsHorde, kHsIntroCardsAlliance));
        if (!head)
        {
            sLog.outError("Hearthstone: cannot read the introduction quests of account %u", accountId);
            return false;
        }
        done = head->Fetch()[0].GetUInt64() > 0;
    }
    std::lock_guard<std::mutex> lock(g_doneMutex);
    g_done[accountId] = done;
    return done;
}

bool HsLessonDone(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        auto const it = g_lessonDone.find(accountId);
        if (it != g_lessonDone.end() && it->second)
            return true;
    }
    // Completed (the last gossip option) or turned in: the quest status of the character is the quick answer.
    bool done = false;
    for (uint32 quest : { kHsIntroLessonHorde, kHsIntroLessonAlliance })
        done = done || player->GetQuestStatus(quest) == QUEST_STATUS_COMPLETE || player->GetQuestRewardStatus(quest);
    if (!done)
    {
        // Another character of the account: a scalar head row, null means a failed query (nothing is cached).
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `character_queststatus` q JOIN `characters` c ON c.`guid` = q.`guid` "
            "WHERE c.`account` = %u AND (q.`rewarded` = 1 OR q.`status` = %u) AND q.`quest` IN (%u, %u))",
            accountId, uint32(QUEST_STATUS_COMPLETE), kHsIntroLessonHorde, kHsIntroLessonAlliance));
        if (!head)
        {
            sLog.outError("Hearthstone: cannot read the lesson quests of account %u", accountId);
            return false;
        }
        done = head->Fetch()[0].GetUInt64() > 0;
    }
    if (done) // only a positive answer is cached
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        g_lessonDone[accountId] = true;
    }
    return done;
}

bool HsCardMasterDone(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        auto const it = g_masterDone.find(accountId);
        if (it != g_masterDone.end() && it->second)
            return true;
    }
    bool done = player->GetQuestRewardStatus(kHsIntroTradeHorde) || player->GetQuestRewardStatus(kHsIntroTradeAlliance);
    if (!done)
    {
        // A scalar head row: null means a failed query; then nothing is cached and the next talk asks again.
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `character_queststatus` q JOIN `characters` c ON c.`guid` = q.`guid` "
            "WHERE c.`account` = %u AND q.`rewarded` = 1 AND q.`quest` IN (%u, %u))",
            accountId, kHsIntroTradeHorde, kHsIntroTradeAlliance));
        if (!head)
        {
            sLog.outError("Hearthstone: cannot read the Card Master quests of account %u", accountId);
            return false;
        }
        done = head->Fetch()[0].GetUInt64() > 0;
    }
    if (done) // only a positive answer is cached: the quest can be turned in later this session
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        g_masterDone[accountId] = true;
    }
    return done;
}

void HsIntroForget(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_doneMutex);
    g_done.erase(accountId);
    g_masterDone.erase(accountId);
    g_lessonDone.erase(accountId);
}
