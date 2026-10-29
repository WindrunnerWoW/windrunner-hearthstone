/*
 * mod-hearthstone: the host side of the Draft Arena, see HsArenaHost.h.
 */

#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsArenaGlue.h"
#include "HsArenaHost.h"
#include "HsArenaSettle.h"
#include "HsCollectionStore.h"
#include "HsDataset.h"
#include "HsDeckStore.h"
#include "HsEconomyGlue.h"
#include "HsMatchGlue.h"
#include "HsMatchStore.h"
#include "quests/HsArenaIntroRules.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    // Gossip actions of the arena (sender 0x4853, block 0x48530300..0x4853031f; see the other 0x4853xxxx blocks).
    constexpr uint32 kActBase = 0x48530300u;
    constexpr uint32 kActCount = 0x20u;
    constexpr uint32 kActArena = kActBase + 0;      // the arena page
    constexpr uint32 kActAskQuest = kActBase + 1;   // show the introduction quest
    constexpr uint32 kActFreeAsk = kActBase + 2;    // confirmation page
    constexpr uint32 kActFreeGo = kActBase + 3;
    constexpr uint32 kActGoldAsk = kActBase + 4;
    constexpr uint32 kActGoldGo = kActBase + 5;
    constexpr uint32 kActCoinsAsk = kActBase + 6;
    constexpr uint32 kActCoinsGo = kActBase + 7;
    constexpr uint32 kActDraft = kActBase + 8;      // continue drafting / open the arena window
    constexpr uint32 kActDuel = kActBase + 9;       // next duel
    constexpr uint32 kActResume = kActBase + 10;    // resume the paused duel
    constexpr uint32 kActRetireAsk = kActBase + 11;
    constexpr uint32 kActRetireGo = kActBase + 12;
    constexpr uint32 kActClaimList = kActBase + 13; // the special-card page
    constexpr uint32 kActClose = kActBase + 14;     // "Never mind"
    constexpr uint32 kActClaimFirst = kActBase + 16; // + index into Hs::ArenaSpecialCards() (4)
    constexpr uint32 kActPracticeAsk = kActBase + 15; // a practice run: after the day's paid run, free, no prize
    constexpr uint32 kActPracticeGo = kActBase + 20;

    constexpr uint32 kHostIbelin = 919502;
    constexpr uint32 kTextPageHorde = 9195900;
    constexpr uint32 kTextPageAlliance = 9195901;
    constexpr uint32 kTextConfirmHorde = 9195902;
    constexpr uint32 kTextConfirmAlliance = 9195903;

    // The name of the feature is not decided: one constant.
    constexpr char const* kArenaName = "The Night Watch Arena";

    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    uint32 AccountOf(Player* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    std::string MoneyText(uint32 copper)
    {
        uint32 const gold = copper / 10000;
        uint32 const silver = copper / 100 % 100;
        uint32 const rest = copper % 100;
        std::string text;
        if (gold)
            text += std::to_string(gold) + "g";
        if (silver)
            text += (text.empty() ? "" : " ") + std::to_string(silver) + "s";
        if (rest || text.empty())
            text += (text.empty() ? "" : " ") + std::to_string(rest) + "c";
        return text;
    }

    // ---- the account's facts ---------------------------------------------------------------------------------

    std::mutex g_doneMutex;
    std::unordered_map<uint32, bool> g_paidDone; // positive answers only

    // The account has all nine classes unlocked and owns enough different collectible cards.
    bool AccountMeetsGate(uint32 accountId)
    {
        Hs::DeckBook book;
        if (!accountId || !HsDecks().GetBook(accountId, book))
            return false;
        std::set<std::string> const unlocked = book.UnlockedClasses();
        uint32 classes = 0;
        for (std::string const& deckClass : Hs::DeckClasses())
            classes += unlocked.count(deckClass) ? 1u : 0u;
        // Cheap test first: the card count needs the whole collection.
        if (classes < Hs::kArenaIntroClasses)
            return false;
        std::shared_ptr<Hs::CardCatalog const> const catalog = HsCollections().Catalog();
        Hs::Collection collection;
        if (!catalog || !HsCollections().Get(accountId, collection))
            return false;
        uint32 distinct = 0;
        for (auto const& entry : collection.cards)
        {
            Hs::CardRule const* rule = catalog->Find(entry.first);
            if (rule && rule->collectible && entry.second.Total() > 0)
                ++distinct;
        }
        return Hs::ArenaIntroGate(classes, distinct);
    }

    Hs::ArenaIntroRunFacts RunFacts(HsArenaReply const& reply)
    {
        Hs::ArenaIntroRunFacts facts;
        if (!reply.hasRun || !reply.run.entryConfirmed)
            return facts;
        facts.exists = true;
        facts.free = reply.run.terms.pay == Hs::ArenaPay::Free;
        facts.open = reply.run.Open();
        facts.ended = reply.run.state == Hs::ArenaState::Finished || reply.run.state == Hs::ArenaState::Retired ||
            reply.run.state == Hs::ArenaState::Settled;
        return facts;
    }

    bool IsActive(Hs::ArenaState state)
    {
        return state != Hs::ArenaState::Finished && state != Hs::ArenaState::Retired && state != Hs::ArenaState::Settled;
    }

    bool DuelLive(Hs::ArenaRun const& run)
    {
        return run.controllerGuid != 0 && HsMatches().IsRunning(run.controllerGuid) && HsMatches().IsArena(run.controllerGuid);
    }

    // What the gossip needs to know now.
    struct Facts
    {
        bool enabled = false;
        bool hasRun = false;
        Hs::ArenaRun run;
        bool open = false;       // an open, confirmed run of the account
        bool paid = false;       // the quest was turned in on some character: paid entry unlocked
        bool dailyUsed = false;  // paid entry unlocked and today's paid run was already started: practice runs only
        uint32 quest = 0;        // the arena quest of this host
        QuestStatus qs = QUEST_STATUS_NONE;
        bool eligible = false;   // the quest may be offered to this character now
        bool freeAllowed = false; // the quest is in the log and the account has not used its free run
    };

    Facts Gather(Player* player, Creature* creature)
    {
        Facts f;
        f.enabled = HsArenaEnabled();
        HsArenaReply const reply = HsArenaGet(AccountOf(player));
        f.hasRun = reply.hasRun && reply.run.entryConfirmed;
        if (f.hasRun)
        {
            f.run = reply.run;
            f.open = reply.run.Open();
        }
        f.paid = HsArenaPaidUnlocked(player);
        f.dailyUsed = f.paid && !f.open && HsArenaDailySlotUsed(AccountOf(player));
        f.quest = Hs::ArenaIntroQuestOfHost(creature ? creature->GetEntry() : 0);
        f.qs = f.quest ? player->GetQuestStatus(f.quest) : QUEST_STATUS_NONE;
        bool const rewardedHere = f.quest && player->GetQuestRewardStatus(f.quest);
        f.eligible = f.quest && !f.paid && !rewardedHere && f.qs == QUEST_STATUS_NONE && AccountMeetsGate(AccountOf(player));
        f.freeAllowed = f.quest && !f.paid && f.qs == QUEST_STATUS_INCOMPLETE &&
            Hs::ArenaIntroFreeRunAllowed(RunFacts(reply));
        return f;
    }

    // ---- the view of a run -------------------------------------------------------------------------------------

    Hs::ArenaView ViewOf(Hs::ArenaRun const& run, uint32 accountId, uint32 guid)
    {
        Hs::ArenaView v;
        v.enabled = HsArenaEnabled();
        if (!run.entryConfirmed)
            return v; // a reservation that waits for its debit is not a run yet
        v.state = Hs::ArenaStateCode(run.state);
        v.runId = run.id;
        v.pay = Hs::ArenaPayCode(run.terms.pay);
        v.deckClass = run.deckClass;
        v.wins = run.wins;
        v.draws = run.draws;
        v.losses = run.losses;
        v.duels = run.matchNo;
        int const open = run.OpenPick();
        if (open >= 0 && static_cast<std::size_t>(open) < run.picks.size())
        {
            v.offerNo = open;
            v.offer = run.picks[static_cast<std::size_t>(open)].offer;
        }
        std::map<std::string, uint32> counts;
        for (std::string const& card : run.Deck())
            ++counts[card];
        v.picked = static_cast<uint32>(run.Deck().size());
        for (auto const& entry : counts)
            v.deck.emplace_back(entry.first, entry.second);
        v.basePacks = run.terms.basePacks;
        v.bonusCoins = run.terms.bonusCoins;
        if (run.state == Hs::ArenaState::Finished || run.state == Hs::ArenaState::Retired || run.state == Hs::ArenaState::Settled)
        {
            v.prizePacks = run.prize.packs;
            v.prizeCoins = run.prize.coins;
            v.prizeSpecial = run.prize.specialChoice;
        }
        else
        {
            Hs::Collection collection;
            bool const complete = HsCollections().Get(accountId, collection) && Hs::ArenaSpecialSetsComplete(collection);
            Hs::ArenaPrize const prize = Hs::PrizeFor(run.wins, run.terms, complete);
            v.prizePacks = prize.packs;
            v.prizeCoins = prize.coins;
            v.prizeSpecial = prize.specialChoice;
        }
        v.specialPending = run.specialPending;
        if (run.specialPending)
            for (Hs::ArenaSpecialMissing const& option : HsArenaSpecialOptions(accountId))
                v.specials.emplace_back(option.id, option.copies);
        v.claimed = run.specialCard;
        v.live = DuelLive(run);
        v.mine = v.live && run.controllerGuid == guid;
        return v;
    }

    // ---- idempotency keys (hearthstone_arena_op) -------------------------------------------------------------------

    // 1 = the key exists (stored: its result), 0 = unknown, -1 = the database failed.
    int LookupKey(uint32 accountId, std::string const& key, std::string& stored)
    {
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `hearthstone_arena_op` WHERE `account_id` = %u AND `op_key` = '%s'), "
            "(SELECT `result` FROM `hearthstone_arena_op` WHERE `account_id` = %u AND `op_key` = '%s' LIMIT 1)",
            accountId, key.c_str(), accountId, key.c_str()));
        if (!head)
            return -1;
        Field* f = head->Fetch();
        if (f[0].GetUInt64() == 0)
            return 0;
        stored = f[1].IsNULL() ? std::string() : f[1].GetCppString();
        return 1;
    }

    // The result column of a request: what the key was used for (the run id is left out: a retry names no new run).
    std::string ResultOf(Hs::ArenaRequest const& r)
    {
        if (r.op == Hs::kOpArenaClass)
            return "class:" + r.deckClass;
        if (r.op == Hs::kOpArenaPick)
            return "pick:" + std::to_string(r.pickNo) + ":" + r.cardId;
        if (r.op == Hs::kOpArenaDuel)
            return "duel";
        if (r.op == Hs::kOpArenaRetire)
            return "retire";
        return "claim:" + r.cardId;
    }

    void StoreKey(uint32 accountId, std::string const& key, std::string const& result)
    {
        std::string escaped = result;
        CharacterDatabase.escape_string(escaped);
        if (!CharacterDatabase.DirectPExecute(
            "INSERT IGNORE INTO `hearthstone_arena_op` (`account_id`, `op_key`, `result`) VALUES (%u, '%s', '%s')",
            accountId, key.c_str(), escaped.c_str()))
            sLog.outError("Hearthstone: could not store the arena op key %s of account %u (the step is idempotent anyway)",
                key.c_str(), accountId);
    }

    bool Unavailable(std::string const& why)
    {
        return why == "unavailable" || why == "storage" || why == "data" || why == "commit" || why == "debit";
    }

    Hs::ArenaReply RefusalReply(std::string const& why, std::string const& text)
    {
        Hs::ArenaReply r;
        r.why = why;
        r.msg = text.empty() ? HsArenaWhyText(why) : text;
        r.status = Unavailable(why) ? Hs::ArenaReply::Status::Unavailable : Hs::ArenaReply::Status::Refused;
        return r;
    }

    // ---- the host that opened the arena window ---------------------------------------------------------------------
    // The window's Play and Retire buttons (ARENA_DUEL / ARENA_RETIRE) work only at a host. The host whose gossip opened
    // the window is remembered per character; HsArenaHostUpdate drops it and sends ARENA_CLOSE when the character is
    // no longer in interaction range of it (the addon then closes the window).
    std::mutex g_visitMutex;
    std::unordered_map<uint32, ObjectGuid> g_visits; // character guid (low) -> the host
    uint32 g_visitTimer = 0;
    constexpr uint32 kVisitCheckMs = 400;

    void RememberHost(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return;
        std::lock_guard<std::mutex> lock(g_visitMutex);
        g_visits[player->GetGUIDLow()] = creature->GetObjectGuid();
    }

    // The remembered host when the player can talk to it now, else nullptr.
    Creature* HostInReach(Player* player)
    {
        if (!player || !player->IsInWorld())
            return nullptr;
        ObjectGuid host;
        {
            std::lock_guard<std::mutex> lock(g_visitMutex);
            auto const it = g_visits.find(player->GetGUIDLow());
            if (it == g_visits.end())
                return nullptr;
            host = it->second;
        }
        Creature* creature = player->GetNPCIfCanInteractWith(host, UNIT_NPC_FLAG_NONE);
        return creature && HsArenaIsHost(creature->GetEntry()) ? creature : nullptr;
    }

    // ---- gossip --------------------------------------------------------------------------------------------------------

    void AddItem(GossipMenu& menu, uint8 icon, std::string const& text, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            return;
        menu.AddMenuItem(icon, text, sender, action, "", false);
        // The core reads the parallel action data vector unchecked: keep it aligned.
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    bool IsAlliance(Creature* creature)
    {
        return creature->GetEntry() == kHostIbelin;
    }

    void Refuse(Player* player, std::string const& text)
    {
        if (player->PlayerTalkClass)
            player->PlayerTalkClass->CloseGossip();
        SysMessage(player, text);
    }

    void Close(Player* player)
    {
        if (player->PlayerTalkClass)
            player->PlayerTalkClass->CloseGossip();
    }

    // The arena entry: with an open run it opens the arena window (the draft, the duels, Retire and the special card are
    // all there); without one it shows the ways to start a run (the first free run, gold, Hearthstone Coins, or the
    // introduction quest). Every selection checks again.
    void ShowArenaPage(Player* player, Creature* creature, uint32 sender)
    {
        PlayerMenu* talk = player->PlayerTalkClass;
        Facts const f = Gather(player, creature);
        if (f.open)
        {
            Close(player);
            RememberHost(player, creature);
            HsPushArena(AccountOf(player), true);
            return;
        }
        talk->ClearMenus();
        GossipMenu& menu = talk->GetGossipMenu();
        if (f.enabled && f.paid && f.dailyUsed)
            AddItem(menu, GOSSIP_ICON_BATTLE, "Start a practice run (free, no rewards)", sender, kActPracticeAsk);
        else if (f.enabled && f.paid)
        {
            Hs::ArenaSettings const s = sHearthstoneConfig.GetArena();
            AddItem(menu, GOSSIP_ICON_MONEY_BAG, "Enter with gold (" + MoneyText(s.entryCostCopper) + ")", sender, kActGoldAsk);
            AddItem(menu, GOSSIP_ICON_MONEY_BAG, "Enter with Hearthstone Coins (" + std::to_string(s.entryCostCoins) + ")",
                sender, kActCoinsAsk);
        }
        else if (f.enabled && f.freeAllowed)
            AddItem(menu, GOSSIP_ICON_BATTLE, "Start my first run", sender, kActFreeAsk);
        else if (f.enabled && f.eligible)
            AddItem(menu, GOSSIP_ICON_CHAT, "Tell me about the Night Watch Arena.", sender, kActAskQuest);
        if (menu.Empty())
        {
            Close(player);
            return;
        }
        talk->SendGossipMenu(IsAlliance(creature) ? kTextPageAlliance : kTextPageHorde, creature->GetObjectGuid());
    }

    // A confirmation page: the question (npc_text) and "Yes, ..." / "Never mind".
    void ShowConfirm(Player* player, Creature* creature, uint32 sender, std::string const& yes, uint32 yesAction)
    {
        PlayerMenu* talk = player->PlayerTalkClass;
        talk->ClearMenus();
        GossipMenu& menu = talk->GetGossipMenu();
        AddItem(menu, GOSSIP_ICON_BATTLE, yes, sender, yesAction);
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", sender, kActClose);
        talk->SendGossipMenu(IsAlliance(creature) ? kTextConfirmAlliance : kTextConfirmHorde, creature->GetObjectGuid());
    }

    void ShowClaimList(Player* player, Creature* creature, uint32 sender)
    {
        PlayerMenu* talk = player->PlayerTalkClass;
        std::vector<Hs::ArenaSpecialMissing> const options = HsArenaSpecialOptions(AccountOf(player));
        talk->ClearMenus();
        GossipMenu& menu = talk->GetGossipMenu();
        std::vector<Hs::ArenaSpecialCard> const& cards = Hs::ArenaSpecialCards();
        std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
        for (std::size_t i = 0; i < cards.size() && i < 4; ++i)
        {
            auto const option = std::find_if(options.begin(), options.end(),
                [&](Hs::ArenaSpecialMissing const& m) { return m.id == cards[i].id; });
            if (option == options.end())
                continue;
            Hs::Match::CardDef const* def = db ? db->Find(cards[i].id) : nullptr;
            std::string label = def ? def->name : std::string(cards[i].id);
            if (option->copies > 1)
                label += " (x" + std::to_string(option->copies) + ")";
            AddItem(menu, GOSSIP_ICON_TALK, label, sender, kActClaimFirst + static_cast<uint32>(i));
        }
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", sender, kActClose);
        talk->SendGossipMenu(IsAlliance(creature) ? kTextPageAlliance : kTextPageHorde, creature->GetObjectGuid());
    }

    // The common start of an entry: shows the reply as a message. True when the run exists now.
    bool Entered(Player* player, Creature* creature, HsArenaReply const& reply, std::string const& what)
    {
        Close(player);
        if (!reply.ok)
        {
            SysMessage(player, reply.text.empty() ? HsArenaWhyText(reply.why) : reply.text);
            return false;
        }
        SysMessage(player, what);
        RememberHost(player, creature);
        HsPushArena(AccountOf(player), true);
        return true;
    }

    bool OfferedQuest(Player* player, uint32 questId)
    {
        if (!Hs::IsArenaIntroQuest(questId) || !player || !player->IsInWorld())
            return false;
        if (HsArenaPaidUnlocked(player) || player->GetQuestRewardStatus(questId) ||
            player->GetQuestStatus(questId) != QUEST_STATUS_NONE)
            return false;
        return AccountMeetsGate(AccountOf(player));
    }
}

// ---- gossip -----------------------------------------------------------------------------------------------------------

bool HsArenaHostHasOptions(Player* player, Creature* creature)
{
    if (!player || !creature || !HsArenaIsHost(creature->GetEntry()) || !HsArenaReady())
        return false;
    uint32 const quest = Hs::ArenaIntroQuestOfHost(creature->GetEntry());
    HsArenaReply const reply = HsArenaGet(AccountOf(player));
    if (reply.hasRun && reply.run.entryConfirmed && reply.run.Open())
        return true;
    if (!HsArenaEnabled())
        return false;
    return HsArenaPaidUnlocked(player) || player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE ||
        (quest && !player->GetQuestRewardStatus(quest) && player->GetQuestStatus(quest) == QUEST_STATUS_NONE &&
            AccountMeetsGate(AccountOf(player)));
}

bool HsArenaHostIsQuestEntry(Player* player)
{
    if (!player)
        return false;
    HsArenaReply const reply = HsArenaGet(AccountOf(player));
    if (reply.hasRun && reply.run.entryConfirmed && reply.run.Open())
        return false;
    return !HsArenaPaidUnlocked(player);
}

void HsArenaHostAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!player || !creature)
        return;
    AddItem(menu, GOSSIP_ICON_BATTLE, kArenaName, sender, kActArena);
}

bool HsArenaHostHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kActBase || action >= kActBase + kActCount)
        return false;
    PlayerMenu* talk = player ? player->PlayerTalkClass : nullptr;
    if (!talk)
        return true;
    if (!creature || !HsArenaIsHost(creature->GetEntry()) || !HsArenaReady())
    {
        talk->CloseGossip();
        return true;
    }
    uint32 const accountId = AccountOf(player);
    uint32 const guid = player->GetGUIDLow();

    if (action == kActClose)
    {
        talk->CloseGossip();
        return true;
    }
    if (action == kActArena)
    {
        ShowArenaPage(player, creature, sender);
        return true;
    }

    Facts const f = Gather(player, creature);
    Hs::ArenaSettings const settings = sHearthstoneConfig.GetArena();

    if (action == kActAskQuest)
    {
        if (!f.enabled || !f.eligible || !OfferedQuest(player, f.quest))
        {
            Refuse(player, "There is nothing to tell you about the Night Watch Arena right now.");
            return true;
        }
        Quest const* quest = sObjectMgr.GetQuestTemplate(f.quest);
        if (!quest)
        {
            Close(player);
            return true;
        }
        talk->CloseGossip();
        talk->SendQuestGiverQuestDetails(quest, creature->GetObjectGuid(), true);
        return true;
    }

    // ---- a practice run: the runs after the day's paid one cost nothing and pay nothing ----
    if (action == kActPracticeAsk || action == kActPracticeGo)
    {
        if (!f.enabled || f.open)
        {
            Refuse(player, f.open ? HsArenaWhyText("open_run") : HsArenaWhyText("unavailable"));
            return true;
        }
        if (!f.paid || !f.dailyUsed)
        {
            Refuse(player, "A practice run comes after your first run of the day.");
            return true;
        }
        if (action == kActPracticeAsk)
        {
            ShowConfirm(player, creature, sender, "Yes, start a practice run (no rewards)", kActPracticeGo);
            return true;
        }
        HsArenaReply const reply = HsArenaEnter(player, creature, Hs::ArenaPay::Practice);
        Entered(player, creature, reply,
            "your practice run has begun. It costs nothing and pays nothing. Choose your class in the arena window.");
        return true;
    }

    // ---- entry: a confirmation page, then the entry itself ----
    if (action == kActFreeAsk || action == kActGoldAsk || action == kActCoinsAsk)
    {
        if (!f.enabled || f.open)
        {
            Refuse(player, f.open ? HsArenaWhyText("open_run") : HsArenaWhyText("unavailable"));
            return true;
        }
        if (action == kActFreeAsk && !f.freeAllowed)
        {
            Refuse(player, "That is not possible right now.");
            return true;
        }
        if (action != kActFreeAsk && !f.paid)
        {
            Refuse(player, "You have not played the Night Watch Arena yet.");
            return true;
        }
        if (action == kActFreeAsk)
            ShowConfirm(player, creature, sender, "Yes, start my first run", kActFreeGo);
        else if (action == kActGoldAsk)
            ShowConfirm(player, creature, sender, "Yes, pay " + MoneyText(settings.entryCostCopper) + " and enter", kActGoldGo);
        else
            ShowConfirm(player, creature, sender, "Yes, pay " + std::to_string(settings.entryCostCoins) +
                " Hearthstone Coins and enter", kActCoinsGo);
        return true;
    }
    if (action == kActFreeGo || action == kActGoldGo || action == kActCoinsGo)
    {
        if (!f.enabled)
        {
            Refuse(player, HsArenaWhyText("unavailable"));
            return true;
        }
        if (action == kActFreeGo ? !f.freeAllowed : !f.paid)
        {
            Refuse(player, "That is not possible right now.");
            return true;
        }
        Hs::ArenaPay const pay = action == kActFreeGo ? Hs::ArenaPay::Free
            : action == kActGoldGo ? Hs::ArenaPay::Gold : Hs::ArenaPay::Coins;
        HsArenaReply const reply = HsArenaEnter(player, creature, pay);
        Entered(player, creature, reply, pay == Hs::ArenaPay::Free
            ? "your first Night Watch Arena has begun. Choose your class in the arena window."
            : "you have entered the Night Watch Arena. Choose your class in the arena window.");
        return true;
    }

    // ---- an open run ----
    if (action == kActDraft)
    {
        Close(player);
        if (!f.open)
        {
            SysMessage(player, HsArenaWhyText("no_run"));
            return true;
        }
        SysMessage(player, "opening the arena window.");
        RememberHost(player, creature);
        HsPushArena(accountId, true);
        return true;
    }
    if (action == kActDuel || action == kActResume)
    {
        HsArenaReply const reply = action == kActDuel ? HsArenaStartDuel(player, creature, 0)
                                                      : HsArenaResumeDuel(player, creature, 0);
        Close(player);
        if (!reply.ok)
            SysMessage(player, reply.text.empty() ? HsArenaWhyText(reply.why) : reply.text);
        return true;
    }
    if (action == kActRetireAsk || action == kActRetireGo)
    {
        if (!f.open || !IsActive(f.run.state))
        {
            Refuse(player, HsArenaWhyText("no_run"));
            return true;
        }
        if (DuelLive(f.run))
        {
            Refuse(player, "Your duel is still running. Finish it or concede it first.");
            return true;
        }
        if (action == kActRetireAsk)
        {
            ShowConfirm(player, creature, sender, "Yes, retire my run (" + std::to_string(f.run.wins) + " win" +
                (f.run.wins == 1 ? "" : "s") + ")", kActRetireGo);
            return true;
        }
        HsArenaReply const reply = HsArenaRetire(accountId, guid, f.run.id);
        Close(player);
        if (!reply.ok)
            SysMessage(player, reply.text.empty() ? HsArenaWhyText(reply.why) : reply.text);
        else
            SysMessage(player, "your run has ended with " + std::to_string(reply.run.wins) + " win" +
                (reply.run.wins == 1 ? "" : "s") + ". The prize for it is settled automatically.");
        return true;
    }
    if (action == kActClaimList)
    {
        if (!f.open || f.run.state != Hs::ArenaState::Settled || !f.run.specialPending)
        {
            Refuse(player, HsArenaWhyText("bad_state"));
            return true;
        }
        ShowClaimList(player, creature, sender);
        return true;
    }
    if (action >= kActClaimFirst && action < kActClaimFirst + 4)
    {
        std::vector<Hs::ArenaSpecialCard> const& cards = Hs::ArenaSpecialCards();
        std::size_t const index = action - kActClaimFirst;
        if (!f.open || !f.run.specialPending || index >= cards.size())
        {
            Refuse(player, HsArenaWhyText("bad_state"));
            return true;
        }
        HsArenaClaimReply const reply = HsArenaClaimSpecial(accountId, guid, f.run.id, cards[index].id);
        Close(player);
        if (!reply.ok)
        {
            SysMessage(player, HsArenaWhyText(reply.why));
            return true;
        }
        if (reply.convertedToPack)
            SysMessage(player, "every special card was yours already: one more booster pack was added instead.");
        else
        {
            std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
            Hs::Match::CardDef const* def = db ? db->Find(reply.card) : nullptr;
            SysMessage(player, "you receive " + (def ? def->name : reply.card) +
                (reply.copies > 1 ? " (x" + std::to_string(reply.copies) + ")" : std::string()) + ".");
        }
        return true;
    }
    talk->CloseGossip();
    return true;
}

// ---- the quest -----------------------------------------------------------------------------------------------------------

bool HsArenaHostHandleAccept(WorldSession* session, WorldPacket const& packet)
{
    if (!session || packet.size() < sizeof(uint64) + sizeof(uint32))
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid giver;
    uint32 questId = 0;
    copy >> giver >> questId;
    if (!Hs::IsArenaIntroQuest(questId))
        return false;
    // From here on the packet is ours: the core would refuse it (the quest has no start relation).
    Player* player = session->GetPlayer();
    if (!player || !player->IsInWorld())
        return true;
    Creature* creature = giver.IsAnyTypeCreature() ? player->GetNPCIfCanInteractWith(giver, UNIT_NPC_FLAG_NONE) : nullptr;
    if (!creature || Hs::ArenaIntroQuestOfHost(creature->GetEntry()) != questId || !OfferedQuest(player, questId))
    {
        if (player->PlayerTalkClass)
            player->PlayerTalkClass->CloseGossip();
        return true;
    }
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (quest && player->CanTakeQuest(quest, true) && player->CanAddQuest(quest, true))
    {
        player->AddQuest(quest, creature);
        sLog.outString("Hearthstone: %s (account %u) accepted the arena introduction quest %u", player->GetName(),
            AccountOf(player), questId);
        HsArenaHostReconcile(player);
    }
    if (player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    return true;
}

void HsArenaHostAfterTurnIn(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session ? session->GetPlayer() : nullptr;
    if (!player || packet.size() < sizeof(uint64) + sizeof(uint32))
        return;
    uint32 const questId = packet.read<uint32>(sizeof(uint64));
    if (!Hs::IsArenaIntroQuest(questId) || !player->GetQuestRewardStatus(questId))
        return;
    uint32 const accountId = AccountOf(player);
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        g_paidDone[accountId] = true;
    }
    sLog.outString("Hearthstone: %s (account %u) turned in the arena introduction quest %u: paid entry is open", player->GetName(),
        accountId, questId);
    HsArenaMarkDirty(accountId); // the addon learns about it with the next ARENA_UPD
}

bool HsArenaPaidUnlocked(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = AccountOf(player);
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        auto const it = g_paidDone.find(accountId);
        if (it != g_paidDone.end() && it->second)
            return true;
    }
    bool done = player->GetQuestRewardStatus(Hs::kArenaIntroQuestHorde) || player->GetQuestRewardStatus(Hs::kArenaIntroQuestAlliance);
    if (!done)
    {
        // A scalar head row: null means a failed query (then nothing is cached and the next ask reads again).
        std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
            "SELECT (SELECT COUNT(*) FROM `character_queststatus` q JOIN `characters` c ON c.`guid` = q.`guid` "
            "WHERE c.`account` = %u AND q.`rewarded` = 1 AND q.`quest` IN (%u, %u))", accountId,
            Hs::kArenaIntroQuestHorde, Hs::kArenaIntroQuestAlliance));
        if (!head)
        {
            sLog.outError("Hearthstone: cannot read the arena introduction quests of account %u", accountId);
            return false;
        }
        done = head->Fetch()[0].GetUInt64() > 0;
    }
    if (done)
    {
        std::lock_guard<std::mutex> lock(g_doneMutex);
        g_paidDone[accountId] = true;
    }
    return done;
}

bool HsArenaHostReconcile(Player* player)
{
    if (!player || !player->IsInWorld() || !HsArenaReady())
        return false;
    uint32 holding = 0;
    for (uint32 quest : { Hs::kArenaIntroQuestHorde, Hs::kArenaIntroQuestAlliance })
        if (player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE)
            holding = quest;
    if (!holding)
        return false;
    if (!Hs::ArenaIntroShouldComplete(RunFacts(HsArenaGet(AccountOf(player)))))
        return false;
    player->AreaExploredOrEventHappens(holding);
    sLog.outString("Hearthstone: the arena introduction quest %u of %s (account %u) is complete: the free run has ended",
        holding, player->GetName(), AccountOf(player));
    return true;
}

void HsArenaHostForget(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_doneMutex);
    g_paidDone.erase(accountId);
}

void HsArenaHostOnLogin(Player* player)
{
    HsArenaHostReconcile(player);
}

// ---- changes -----------------------------------------------------------------------------------------------------------------

namespace
{
    std::mutex g_dirtyMutex;
    std::map<uint32, uint64> g_dirty; // account -> the time of its newest write (ms)
    uint32 g_dirtyTimer = 0;
    // A write is announced a little later, so that the commit of a joined transaction is visible to the reader.
    constexpr uint64 kDirtyDelayMs = 100;
}

void HsArenaMarkDirty(uint32_t accountId)
{
    if (!accountId)
        return;
    std::lock_guard<std::mutex> lock(g_dirtyMutex);
    g_dirty[accountId] = HsNowMs();
}

namespace
{
    // Drops the remembered hosts the characters walked away from (or that are gone) and closes their arena window.
    // Not during a match: the window is hidden then, and the duel itself keeps the player near the host.
    void CheckVisits()
    {
        std::vector<std::pair<uint32, ObjectGuid>> visits;
        {
            std::lock_guard<std::mutex> lock(g_visitMutex);
            if (g_visits.empty())
                return;
            visits.assign(g_visits.begin(), g_visits.end());
        }
        for (auto const& visit : visits)
        {
            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, visit.first));
            bool keep = false;
            bool tell = false;
            if (player && player->IsInWorld())
            {
                tell = true;
                Creature* host = player->GetMap()->GetCreature(visit.second);
                keep = HsMatches().IsRunning(visit.first) ||
                    (host && host->IsAlive() && player->IsWithinDistInMap(host, INTERACTION_DISTANCE));
            }
            if (keep)
                continue;
            {
                std::lock_guard<std::mutex> lock(g_visitMutex);
                auto const it = g_visits.find(visit.first);
                if (it == g_visits.end() || it->second != visit.second)
                    continue; // the player talked to a host again meanwhile
                g_visits.erase(it);
            }
            if (tell)
                HsPushArenaClose(visit.first);
        }
    }
}

void HsArenaHostUpdate(uint32_t diff)
{
    g_visitTimer += diff;
    if (g_visitTimer >= kVisitCheckMs)
    {
        g_visitTimer = 0;
        CheckVisits();
    }
    g_dirtyTimer += diff;
    if (g_dirtyTimer < 200)
        return;
    g_dirtyTimer = 0;
    std::vector<uint32> due;
    {
        std::lock_guard<std::mutex> lock(g_dirtyMutex);
        if (g_dirty.empty())
            return;
        uint64 const now = HsNowMs();
        for (auto it = g_dirty.begin(); it != g_dirty.end();)
        {
            if (now >= it->second + kDirtyDelayMs)
            {
                due.push_back(it->first);
                it = g_dirty.erase(it);
            }
            else
                ++it;
        }
    }
    for (uint32 accountId : due)
    {
        uint32 const guid = HsOnlineCharacterOf(accountId);
        if (!guid)
            continue; // nobody to tell: a character that logs in asks ARENA_GET and reconciles its quest
        if (Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid)))
            HsArenaHostReconcile(player);
        HsPushArena(accountId, false);
    }
}

// ---- protocol -----------------------------------------------------------------------------------------------------------------

Hs::ArenaView HsArenaMakeView(uint32_t accountId, uint32_t guid)
{
    HsArenaReply const reply = HsArenaGet(accountId);
    if (!reply.hasRun)
    {
        Hs::ArenaView v;
        v.enabled = HsArenaEnabled();
        return v;
    }
    return ViewOf(reply.run, accountId, guid);
}

Hs::ArenaReply HsArenaSession::Get(std::string const& reqId)
{
    m_pending = true;
    m_isGet = true;
    m_request = Hs::ArenaRequest();
    m_request.reqId = reqId; // Finish answers with it (without it the reply carried no reqId and the addon timed out)
    Hs::ArenaReply r;
    r.status = Hs::ArenaReply::Status::Deferred;
    return r;
}

Hs::ArenaReply HsArenaSession::Mutate(Hs::ArenaRequest const& request)
{
    m_pending = true;
    m_isGet = false;
    m_request = request;
    Hs::ArenaReply r;
    r.status = Hs::ArenaReply::Status::Deferred;
    return r;
}

void HsArenaSession::Finish(std::vector<std::string>& out)
{
    if (!m_pending)
        return;
    m_pending = false;
    Hs::AppendArenaReply(out, m_request.reqId, Run());
}

Hs::ArenaReply HsArenaSession::Run()
{
    if (!HsArenaReady())
        return RefusalReply("unavailable", std::string());
    Hs::ArenaReply reply;
    if (m_isGet)
    {
        reply.status = Hs::ArenaReply::Status::Ok;
        reply.view = HsArenaMakeView(m_account, m_guid);
        return reply;
    }

    Hs::ArenaRequest const& r = m_request;
    std::string const result = ResultOf(r);
    std::string stored;
    int const found = LookupKey(m_account, r.key, stored);
    if (found < 0)
        return RefusalReply("storage", std::string());
    if (found == 1)
    {
        // The key was used: the same request again is a replay of the stored step, another request is a mistake.
        if (stored != result)
            return RefusalReply("key", "That request key was used for another step.");
        reply.status = Hs::ArenaReply::Status::Replay;
        reply.view = HsArenaMakeView(m_account, m_guid);
        return reply;
    }

    HsArenaReply done;
    if (r.op == Hs::kOpArenaDuel || r.op == Hs::kOpArenaRetire)
    {
        Creature* host = HostInReach(m_player);
        if (!host)
            return RefusalReply("host", std::string());
        HsArenaReply const current = HsArenaGet(m_account);
        if (!current.hasRun || !current.run.entryConfirmed || !IsActive(current.run.state))
            return RefusalReply("no_run", std::string());
        if (r.op == Hs::kOpArenaDuel)
        {
            if (current.run.state == Hs::ArenaState::Ready)
                done = HsArenaStartDuel(m_player, host, r.runId);
            else if (current.run.state == Hs::ArenaState::Paused)
                done = HsArenaResumeDuel(m_player, host, r.runId);
            else
                return RefusalReply("bad_state", std::string());
        }
        else
        {
            if (DuelLive(current.run))
                return RefusalReply("running", "Your duel is still running. Finish it or concede it first.");
            done = HsArenaRetire(m_account, m_guid, r.runId ? r.runId : current.run.id);
            if (done.ok && !done.replay)
                SysMessage(m_player, "your run has ended with " + std::to_string(done.run.wins) + " win" +
                    (done.run.wins == 1 ? "" : "s") + ". The prize for it is settled automatically.");
        }
    }
    else if (r.op == Hs::kOpArenaClass)
        done = HsArenaChooseClass(m_account, r.runId, r.deckClass);
    else if (r.op == Hs::kOpArenaPick)
        done = HsArenaPick(m_account, r.runId, r.pickNo, r.cardId);
    else
    {
        HsArenaClaimReply const claim = HsArenaClaimSpecial(m_account, m_guid, r.runId, r.cardId);
        done.ok = claim.ok;
        done.why = claim.why;
        done.replay = claim.replay;
        done.hasRun = claim.hasRun;
        done.run = claim.run;
    }
    if (!done.ok)
        return RefusalReply(done.why, done.text);
    StoreKey(m_account, r.key, result);
    reply.status = done.replay ? Hs::ArenaReply::Status::Replay : Hs::ArenaReply::Status::Ok;
    reply.view = done.hasRun ? ViewOf(done.run, m_account, m_guid) : HsArenaMakeView(m_account, m_guid);
    return reply;
}
