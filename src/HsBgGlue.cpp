/*
 * mod-hearthstone: the core-facing glue of the Warsong Battlegrounds, see HsBgGlue.h.
 */

#include "HsBgGlue.h"

#include "Chat.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsArenaGlue.h"
#include "HsBgBreadcrumb.h"
#include "HsCollectionStore.h"
#include "HsEconomyGlue.h"
#include "HsLadderDuelStore.h"
#include "HsMatchGlue.h"
#include "HsMatchStore.h"
#include "HsWeeklyQuestGlue.h"
#include "quests/HsBgWeeklyQuests.h"
#include "battlegrounds/HsBgEffects.h"
#include "battlegrounds/HsBgNames.h"
#include "battlegrounds/HsBgSession.h"
#include "battlegrounds/HsBgWire.h"
#include "protocol/HsProtocol.h"
#include "protocol/HsSession.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace Bg = Hs::Bg;

    // The effect entries the playable dataset registers; the running server must have exactly this many (see
    // HsBgStartup): the minion effects, the hero tokens and the 29 Darkmoon Prizes.
    constexpr int kExpectedEffects = 202;
    // "WRBG" + the tab that separates it from the frame.
    constexpr std::size_t kPrefixLen = 4;

    // The two Warsong Battlegrounds hosts (world migration 0059; owner places them).
    constexpr uint32 kHostAlliance = 920200;   // Silverwing Sentinel
    constexpr uint32 kHostHorde = 920201;      // Warsong Outrider
    constexpr uint32 kHostGreetingAlliance = 9195970;
    constexpr uint32 kHostGreetingHorde = 9195971;
    // The tutorial "How does it work?" (world migration 0062): npc_text pages kTutorialFirst.. in order, shared by both hosts.
    constexpr uint32 kTutorialFirst = 9195973;
    constexpr int kTutorialPages = 9;
    // The option under page p (1..kTutorialPages - 1) that opens page p + 1.
    constexpr char const* kTutorialNext[kTutorialPages - 1] = {
        "How does a game begin?",
        "How do I recruit?",
        "And when my board is full?",
        "How do I find stronger minions?",
        "What happens in the fight?",
        "You said I fight alongside allies?",
        "Anything else I should know?",
        "What do I get for playing?",
    };
    // Gossip actions of the hosts (sender 0x4853, block 0x48530700..0x4853070f; see the other 0x4853xxxx blocks).
    constexpr uint32 kActBase = 0x48530700u;
    constexpr uint32 kActPlay = kActBase + 0;
    constexpr uint32 kActHelp = kActBase + 1;   // the first tutorial page
    constexpr uint32 kActLeave = kActBase + 2;
    constexpr uint32 kActBack = kActBase + 3;
    constexpr uint32 kActPage = kActBase + 4;   // + p - 2: tutorial page p (2..kTutorialPages)
    constexpr uint32 kActLast = kActPage + kTutorialPages - 2;
    static_assert(kActLast <= kActBase + 0x0f, "the host actions outgrew their block");

    // The finished game as the Battlegrounds weeklies count it (the quest module stays free of the engine).
    Hs::BgWeeklyGame WeeklyGame(Hs::Bg::GameRecord const& r)
    {
        Hs::BgWeeklyGame g;
        g.placement = r.placement;
        g.teamWon = r.teamWon;
        g.tier = r.tier;
        g.heroPowers = r.heroPowers;
        g.bought = r.stats.bought;
        g.sold = r.stats.sold;
        g.spells = r.stats.spells;
        g.triples = r.stats.triples;
        g.buffs = r.stats.buffs;
        g.battlecries = r.stats.battlecries;
        g.refreshes = r.stats.refreshes;
        g.deathrattles = r.stats.deathrattles;
        g.kills = r.stats.kills;
        g.combatWins = r.stats.combatWins;
        g.damageDealt = r.stats.damageDealt;
        for (int t = 0; t < 9; ++t)
            g.played[t] = r.stats.played[t];
        return g;
    }

    // The finder names the seven NPC seats are drawn from, loaded once at startup (LoadHsFinderNames) into a
    // Battlegrounds-owned copy; empty until HsBgStartup ran. A game falls back to the generic names when it is missing.
    std::shared_ptr<std::vector<Bg::NamePoolEntry> const> g_seatNamePool;

    // One character's Battlegrounds client state: the inbound reassembler and rate limiter (its own, separate from the
    // Hs::Session ones), and the game.
    struct Client
    {
        Hs::Reassembler reassembler;
        Hs::RateLimiter limiter;
        std::unique_ptr<Bg::Session> game;
        bool paid = false;   // the finished game's payout was delivered
    };

    std::mutex g_mutex;   // leaf lock (see HsBgGlue.h)
    std::unordered_map<uint32, Client> g_clients;

    uint64_t NowMs() { return HsNowMs(); }

    Hs::ReassemblyLimits Limits(Hs::Settings const& s)
    {
        Hs::ReassemblyLimits l;
        l.maxBytes = s.maxInboundBytes;
        l.maxParts = s.maxInboundParts;
        l.timeoutMs = s.reassemblyTimeoutMs;
        l.maxPending = s.maxPendingRequests;
        return l;
    }

    // Sends frames with the Battlegrounds prefix. Call without holding g_mutex.
    void SendBg(Player* player, std::vector<std::string> const& frames)
    {
        for (std::string const& frame : frames)
            player->SendAddonMessage(Bg::kBgPrefix, frame);
    }

    bool StripBgPrefix(std::string const& wire, std::string& frame)
    {
        if (wire.size() <= kPrefixLen + 1 || wire.compare(0, kPrefixLen, Bg::kBgPrefix) != 0 ||
            wire[kPrefixLen] != '\t')
            return false;
        frame = wire.substr(kPrefixLen + 1);
        return true;
    }

    uint32 AccountOf(Player* player)
    {
        return player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    // The world prerequisites of a Battlegrounds game: mirrors the arena's duel block (the same checks), plus the one
    // card-duel-per-account rule. Empty = the player may start now.
    std::string StartRefusal(Player* player)
    {
        uint32 const guid = player->GetGUIDLow();
        if (!HsSessionHandshaken(guid))
            return "The Hearthstone addon is not running. Install it and /reload, then try again.";
        if (HsMatches().IsRunning(guid))
            return "you are already in a card match.";
        uint32 const account = AccountOf(player);
        if (HsArenaHasDuel(account))
            return "you have a Night Watch Arena duel to finish first.";
        if (account && HsLadderDuels().Has(account))
            return "you have a ladder duel to finish first.";
        if (!player->IsAlive())
            return "you cannot play while you are dead.";
        if (player->IsInCombat())
            return "you cannot play while you are in combat.";
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (member && member != player && member->IsInWorld() && member->IsInCombat())
                    return std::string(member->GetName()) +
                        " in your group is in combat. Everyone has to be out of combat.";
            }
        }
        return std::string();
    }

    // The seven NPC seat names of a game from the loaded finder pool. Call with g_mutex held (reads g_seatNamePool).
    std::vector<std::string> SeatNames(Player* player, uint32_t seed)
    {
        bool const alliance = player->GetTeam() == ALLIANCE;
        std::vector<Bg::NamePoolEntry> const empty;
        std::vector<Bg::NamePoolEntry> const& pool = g_seatNamePool ? *g_seatNamePool : empty;
        std::vector<std::string> const picked = Bg::PickSeatNames(pool, alliance, seed);
        std::vector<std::string> out(static_cast<std::size_t>(Bg::kPlayers));
        for (std::size_t i = 0; i < picked.size() && i + 1 < out.size(); ++i)
            out[i + 1] = picked[i];
        return out;
    }

    // Creates the game for the character and fills frames with the first BG_STATE push. False with why when a game is
    // already open (even an ended one: the end screen is still up).
    bool StartGame(Player* player, std::vector<std::string>& frames, std::string& why)
    {
        Hs::Settings const settings = sHearthstoneConfig.Get();
        std::random_device device;
        Bg::SessionOptions options;
        options.seed = (static_cast<uint32_t>(device()) << 16) ^ static_cast<uint32_t>(device());
        options.seat = 0;
        // Faction pools, the hero pick, and the seat names of the lobby.
        options.heroes = true;
        options.factionPools = true;
        // The turn-4 quest and reward offer, and the payout (config keys).
        options.quests = true;
        options.payoutBase = settings.bgRewardBase;
        options.payoutTeam = settings.bgRewardTeam;
        options.payoutFirst = settings.bgRewardFirst;
        options.honorBase = settings.bgHonorBase;
        options.honorTeam = settings.bgHonorTeam;
        options.honorFirst = settings.bgHonorFirst;

        std::lock_guard<std::mutex> lock(g_mutex);
        options.seatNames = SeatNames(player, options.seed);
        Client& client = g_clients[player->GetGUIDLow()];
        client.limiter.Configure(settings.ratePerSecond, settings.rateBurst);
        if (client.game)
        {
            why = "you already have a game open. Leave it with .hs bg leave first.";
            return false;
        }
        client.game.reset(new Bg::Session(options));
        client.paid = false;   // a new game has its own payout
        Bg::AppendBgSnapshot(frames, Hs::kServerPushReqId, *client.game);
        return true;
    }

    bool DropGame(uint32 guidLow, std::vector<std::string>* closeFrames)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_clients.find(guidLow);
        if (it == g_clients.end() || !it->second.game)
            return false;
        it->second.game.reset();
        if (closeFrames)
            Bg::AppendBgClose(*closeFrames);
        return true;
    }

    // ---- the two host NPCs ----------------------------------------------------------------

    void HostAddItem(GossipMenu& menu, uint8 icon, std::string const& text, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            return;
        menu.AddMenuItem(icon, text, sender, action, "", false);
        // The core reads the parallel action data vector unchecked: keep it aligned.
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    bool HasOpenGame(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const it = g_clients.find(guidLow);
        return it != g_clients.end() && it->second.game != nullptr;
    }

    void HostSysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Battlegrounds: " + text);
    }

    // A host explains the game to a character that turned in a Battlegrounds breadcrumb (migration 0060): hearing the
    // tutorial completes the rules quest the host then offers (migration 0062).
    bool BreadcrumbDone(Player* player)
    {
        return player->GetQuestRewardStatus(Hs::kBgBreadcrumbQuests[0]) ||
            player->GetQuestRewardStatus(Hs::kBgBreadcrumbQuests[1]);
    }

    // A host plays only with a character that turned in the rules quest, the same rule as the weeklies' condition 920132
    // (either quest). The GM command .hs bg start does not ask.
    bool RulesDone(Player* player)
    {
        return player->GetQuestRewardStatus(Hs::kBgRulesQuests[0]) ||
            player->GetQuestRewardStatus(Hs::kBgRulesQuests[1]);
    }

    void AppendHostOptions(Player* player, GossipMenu& menu, uint32 sender)
    {
        if (RulesDone(player))
            HostAddItem(menu, GOSSIP_ICON_BATTLE, "Play Warsong Battlegrounds", sender, kActPlay);
        if (BreadcrumbDone(player))
            HostAddItem(menu, GOSSIP_ICON_CHAT, "How does it work?", sender, kActHelp);
        if (HasOpenGame(player->GetGUIDLow()))
            HostAddItem(menu, GOSSIP_ICON_CHAT, "Leave my game", sender, kActLeave);
    }

    uint32 HostGreeting(uint32 entry)
    {
        return entry == kHostAlliance ? kHostGreetingAlliance : kHostGreetingHorde;
    }

    void ShowHostPage(Player* player, Creature* creature, uint32 sender)
    {
        PlayerMenu* talk = player->PlayerTalkClass;
        talk->ClearMenus();
        AppendHostOptions(player, talk->GetGossipMenu(), sender);
        // The host's quests (the rules quest, the weeklies) stay on the page after "Back".
        player->PrepareQuestMenu(creature->GetObjectGuid());
        talk->SendGossipMenu(HostGreeting(creature->GetEntry()), creature->GetObjectGuid());
    }

    // Tutorial page `page` (1..kTutorialPages). Each page leads to the next. The last one completes the rules quest of the
    // character's faction while it is open (SpecialFlags 2: hearing every page is the objective) and offers the
    // turn-in on the same page.
    void ShowHostTutorial(Player* player, Creature* creature, int page, uint32 sender)
    {
        PlayerMenu* talk = player->PlayerTalkClass;
        talk->ClearMenus();
        if (page < kTutorialPages)
            HostAddItem(talk->GetGossipMenu(), GOSSIP_ICON_CHAT, kTutorialNext[page - 1], sender,
                kActPage + static_cast<uint32>(page) - 1);
        else
        {
            uint32 const quest = Hs::kBgRulesQuests[player->GetTeam() == ALLIANCE ? 1 : 0];
            if (player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE)
            {
                player->AreaExploredOrEventHappens(quest);
                sLog.outString("Hearthstone: %s heard the Battlegrounds rules (quest %u)", player->GetName(), quest);
            }
            HostAddItem(talk->GetGossipMenu(), GOSSIP_ICON_CHAT, "Back", sender, kActBack);
            player->PrepareQuestMenu(creature->GetObjectGuid());
        }
        talk->SendGossipMenu(kTutorialFirst + static_cast<uint32>(page) - 1, creature->GetObjectGuid());
    }

    void DispatchBg(Hs::Completed const& req, Client& client, std::vector<std::string>& out)
    {
        if (req.op == Bg::kOpBgGet)
        {
            if (!client.game)
            {
                Hs::AppendError(out, req.reqId, Hs::ErrorCode::NotReady, "No Battlegrounds game is open.");
                return;
            }
            Bg::AppendBgSnapshot(out, req.reqId, *client.game);
            return;
        }
        if (req.op == Bg::kOpBgAct)
        {
            Bg::Action action;
            if (!Bg::DecodeBgAction(req.payload, action))
            {
                Bg::AppendBgMalformed(out, req.reqId);
                return;
            }
            if (!client.game)
            {
                Hs::AppendError(out, req.reqId, Hs::ErrorCode::NotReady, "No Battlegrounds game is open.");
                return;
            }
            Bg::ActionResult const result = client.game->Apply(action);
            Bg::AppendBgActionReply(out, req.reqId, *client.game, result, &action);
            return;
        }
        Bg::AppendBgMalformed(out, req.reqId);
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
}

// Cleared by HsBgStartup when the effect registry is incomplete or has registration errors: the mode then stays off
// instead of misplaying.
static bool g_effectsOk = true;

bool HsBgEnabled()
{
    return g_effectsOk;
}

char const* HsBgPrefix()
{
    return Bg::kBgPrefix;
}

void HsBgStartup()
{
    // The seat names come from hearthstone_finder_npc, the ladder roster's own source, read directly so the mode
    // does not depend on the ladder being enabled. Loaded once, into a Battlegrounds-owned copy.
    {
        std::vector<Hs::FinderName> names;
        bool const tableOk = LoadHsFinderNames(names);
        std::shared_ptr<std::vector<Bg::NamePoolEntry>> pool = std::make_shared<std::vector<Bg::NamePoolEntry>>();
        for (Hs::FinderName const& row : names)
        {
            if (row.name.empty())
                continue;
            Bg::NamePoolEntry entry;
            entry.name = row.name;
            entry.side = row.faction == "alliance" ? Bg::NameSide::Alliance
                : row.faction == "horde" ? Bg::NameSide::Horde : Bg::NameSide::Any;
            pool->push_back(entry);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_seatNamePool = pool;
        if (!tableOk)
            sLog.outError("Hearthstone: Battlegrounds seat names: hearthstone_finder_npc is missing, the lobby uses the "
                "generic names");
        else if (pool->size() < static_cast<std::size_t>(Bg::kPlayers - 1))
            sLog.outError("Hearthstone: Battlegrounds seat names: only %u names in hearthstone_finder_npc, the generic "
                "names fill the gaps", static_cast<uint32>(pool->size()));
        else
            sLog.outString("Hearthstone: Battlegrounds seat names: %u names from hearthstone_finder_npc",
                static_cast<uint32>(pool->size()));
    }

    // Force the effect batch objects into the link (their registrars are internal), then check the registry size.
    Bg::ForceLinkEffectBatches();
    Bg::EffectRegistry const& registry = Bg::EffectRegistry::Global();
    int const effects = registry.Size();
    g_effectsOk = effects == kExpectedEffects && registry.Errors().empty();
    if (effects == kExpectedEffects)
    {
        sLog.outString("Hearthstone: Battlegrounds enabled, effect registry %d entries", effects);
    }
    else
    {
        sLog.outError("Hearthstone: Battlegrounds effect registry has %d entries, expected %d (the mode is switched "
            "off; see HsBgEffects.h)", effects, kExpectedEffects);
    }
    for (std::string const& error : registry.Errors())
        sLog.outError("Hearthstone: Battlegrounds effect error: %s", error.c_str());
}

bool HsBgBusy(uint32 guidLow)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_clients.find(guidLow);
    return it != g_clients.end() && it->second.game != nullptr;
}

void HsBgHandleFrame(Player* player, std::string const& wire)
{
    if (!player || !HsBgEnabled())
        return;
    uint32 const guid = player->GetGUIDLow();
    if (!HsSessionHandshaken(guid))
        return;
    std::string frame;
    if (!StripBgPrefix(wire, frame))
        return;

    Hs::Settings const settings = sHearthstoneConfig.Get();
    uint64_t const now = NowMs();
    std::vector<std::string> out;
    bool payNow = false;
    int payCoins = 0;
    int payPacks = 0;
    int payHonor = 0;
    Hs::BgWeeklyGame payGame;
    uint32 payAccount = 0;
    uint32 payGuid = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Client& client = g_clients[guid];
        client.limiter.Configure(settings.ratePerSecond, settings.rateBurst);
        if (!client.limiter.TryConsume(now))
        {
            if (client.limiter.AllowNotice(now))
                Hs::AppendError(out, Hs::kServerPushReqId, Hs::ErrorCode::Rate, "Too many requests, slow down.");
        }
        else if (kPrefixLen + 1 + frame.size() > Hs::kMaxWireBytes)
        {
            Hs::AppendError(out, Hs::kServerPushReqId, Hs::ErrorCode::TooLarge, "Message exceeds the maximum frame size.");
        }
        else
        {
            Hs::Frame f;
            Hs::ParseResult const parsed = Hs::ParseFrame(frame, f);
            if (parsed.status != Hs::ParseStatus::Ok)
            {
                Bg::AppendBgMalformed(out, parsed.reqId);
            }
            else
            {
                Hs::Completed done;
                switch (client.reassembler.Add(now, Limits(settings), f.op, f.reqId, f.part, f.total, f.payload, done))
                {
                    case Hs::AddStatus::Complete:
                        DispatchBg(done, client, out);
                        // A finished game pays once (a game the player left is dropped, so it never gets here).
                        if (client.game && client.game->Over() && !client.paid)
                        {
                            client.paid = true;
                            payNow = true;
                            payCoins = client.game->PayoutCoins();
                            payPacks = client.game->PayoutPacks();
                            payHonor = client.game->PayoutHonor();
                            payGame = WeeklyGame(client.game->Record());
                            payAccount = AccountOf(player);
                            payGuid = guid;
                        }
                        break;
                    case Hs::AddStatus::Malformed:
                    case Hs::AddStatus::TooLarge:
                    case Hs::AddStatus::Busy: Bg::AppendBgMalformed(out, f.reqId); break;
                    case Hs::AddStatus::Pending:
                    case Hs::AddStatus::Ignored: break;
                }
            }
        }
    }
    // The payout runs outside g_mutex (the push helpers must be called without holding a lock).
    if (payNow && payAccount)
    {
        if (payPacks > 0)
        {
            Hs::PackStockResult const r = HsCollections().GrantPacks(payAccount, static_cast<uint32_t>(payPacks), "bg", "bg");
            if (r.status == Hs::PackStatus::Ok)
                HsPushPackChanged(payAccount, r.after.stock);
        }
        if (payCoins > 0)
            HsEconomyBattlegroundEnded(payGuid, payAccount, static_cast<uint32_t>(payCoins));
        // The faction war pays honor (the bonus honor of the core's honor system) and feeds the Battlegrounds weeklies.
        if (payHonor > 0 && player)
            player->GetHonorMgr().Add(static_cast<float>(payHonor), BONUS);
        HsWeeklyBgGameEnded(payGuid, payAccount, payGame);
        if ((payCoins > 0 || payPacks > 0 || payHonor > 0) && player && player->GetSession())
        {
            std::vector<std::string> parts;
            if (payCoins > 0)
                parts.push_back(std::to_string(payCoins) + " Hearthstone Coin" + (payCoins == 1 ? "" : "s"));
            if (payPacks > 0)
                parts.push_back(std::to_string(payPacks) + (payPacks == 1 ? " booster pack" : " booster packs"));
            if (payHonor > 0)
                parts.push_back(std::to_string(payHonor) + " honor");
            std::string line;
            for (std::size_t i = 0; i < parts.size(); ++i)
                line += (i == 0 ? "" : (i + 1 == parts.size() ? " and " : ", ")) + parts[i];
            ChatHandler(player->GetSession()).SendSysMessage("Warsong Battlegrounds: you earned " + line + ".");
        }
    }
    SendBg(player, out);
}

void HsBgOnLogout(uint32 guidLow)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.erase(guidLow);
}

void HsBgOnMapChanged(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUIDLow();
    std::vector<std::string> frames;
    bool const had = DropGame(guid, &frames);
    if (had && HsBgEnabled() && HsSessionHandshaken(guid))
        SendBg(player, frames);
}

void HsBgUpdate(uint32 /*diff*/)
{
    Hs::Settings const settings = sHearthstoneConfig.Get();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_clients.empty())
            return;
        for (auto& entry : g_clients)
        {
            std::vector<std::string> expired;
            entry.second.reassembler.Sweep(NowMs(), settings.reassemblyTimeoutMs, expired);
        }
    }
}

void HsBgCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
{
    if (!handler)
        return;
    (void)security;
    Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();

    if (sub == "leave")
    {
        if (!player)
        {
            handler->SendSysMessage("Hearthstone: no character to leave with.");
            return;
        }
        std::vector<std::string> frames;
        bool const had = DropGame(player->GetGUIDLow(), &frames);
        if (had && HsBgEnabled())
            SendBg(player, frames);
        handler->SendSysMessage(had ? "Battlegrounds: you left the game."
                                    : "Battlegrounds: you have no game open.");
        return;
    }

    if (sub == "start")
    {
        if (!HsBgEnabled())
        {
            handler->SendSysMessage("Hearthstone: the Battlegrounds are not available "
                "(the effect registry failed to load, see the server log).");
            return;
        }
        // Once the hosts exist, .hs bg start is a GM/testing entry only; players use a host.
        if (security < Hs::kDebugMinSecurity)
        {
            handler->SendSysMessage("Battlegrounds: only a game master may start a game that way. "
                "Talk to a Warsong Battlegrounds host instead.");
            return;
        }
        if (!player)
        {
            handler->SendSysMessage("Hearthstone: a character has to run that command.");
            return;
        }
        std::string const why = StartRefusal(player);
        if (!why.empty())
        {
            handler->SendSysMessage("Battlegrounds: cannot start: " + why);
            return;
        }
        std::vector<std::string> frames;
        std::string refused;
        if (!StartGame(player, frames, refused))
        {
            handler->SendSysMessage("Battlegrounds: cannot start: " + refused);
            return;
        }
        SendBg(player, frames);
        handler->SendSysMessage("Battlegrounds: a 4v4 game started. Leave it with .hs bg leave.");
        return;
    }

    if (sub == "status")
    {
        if (!player)
        {
            handler->SendSysMessage("Hearthstone: no character.");
            return;
        }
        bool has = false;
        int round = 0;
        bool over = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto const it = g_clients.find(player->GetGUIDLow());
            if (it != g_clients.end() && it->second.game)
            {
                has = true;
                over = it->second.game->Over();
                round = it->second.game->GetGame().Round();
            }
        }
        handler->SendSysMessage(has ? "Battlegrounds: round " + std::to_string(round) + (over ? " (finished, leave with "
            ".hs bg leave)." : " running.") : "Battlegrounds: you have no game open.");
        return;
    }

    HsBgUsage(handler);
}

void HsBgUsage(ChatHandler* handler)
{
    handler->SendSysMessage("Usage: .hs bg start (GM) | .hs bg leave | .hs bg status");
}

void HsBgStatus(ChatHandler* handler)
{
    bool const enabled = HsBgEnabled();
    int const effects = Bg::EffectRegistry::Global().Size();
    std::size_t open = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto const& entry : g_clients)
            if (entry.second.game)
                ++open;
    }
    handler->SendSysMessage("Battlegrounds: " + std::string(enabled ? "enabled" : "disabled") + ", " +
        std::to_string(open) + " game(s) open, effect registry " + std::to_string(effects) + " entries");
}

// ---- the two host NPCs --------------------------------------------------------------------------------

bool HsBgIsHost(uint32 entry)
{
    return entry == kHostAlliance || entry == kHostHorde;
}

bool HsBgHostServes(Player* player, Creature* creature)
{
    if (!player || !creature || !HsBgEnabled())
        return false;
    return creature->GetEntry() == (player->GetTeam() == ALLIANCE ? kHostAlliance : kHostHorde);
}

void HsBgHostAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32 sender)
{
    if (!player || !creature || !HsBgHostServes(player, creature))
        return;
    AppendHostOptions(player, menu, sender);
}

bool HsBgHostHandleSelect(Player* player, Creature* creature, uint32 action, uint32 sender)
{
    if (action < kActBase || action > kActLast)
        return false;
    PlayerMenu* talk = player ? player->PlayerTalkClass : nullptr;
    if (!talk)
        return true;
    if (!creature || !HsBgIsHost(creature->GetEntry()) || !HsBgHostServes(player, creature))
    {
        talk->CloseGossip();
        return true;
    }

    if (action == kActBack)
    {
        ShowHostPage(player, creature, sender);
        return true;
    }
    if (action == kActHelp || action >= kActPage)
    {
        if (!BreadcrumbDone(player))
        {
            talk->CloseGossip();
            return true;
        }
        int const page = action == kActHelp ? 1 : static_cast<int>(action - kActPage) + 2;
        ShowHostTutorial(player, creature, page, sender);
        return true;
    }
    if (action == kActLeave)
    {
        std::vector<std::string> frames;
        bool const had = DropGame(player->GetGUIDLow(), &frames);
        talk->CloseGossip();
        if (had && HsBgEnabled())
            SendBg(player, frames);
        HostSysMessage(player, had ? "you left the game." : "you have no game open.");
        return true;
    }
    if (action == kActPlay)
    {
        if (!RulesDone(player))
        {
            talk->CloseGossip();
            return true;
        }
        std::string const why = StartRefusal(player);
        if (!why.empty())
        {
            talk->CloseGossip();
            HostSysMessage(player, "cannot start: " + why);
            return true;
        }
        std::vector<std::string> frames;
        std::string refused;
        if (!StartGame(player, frames, refused))
        {
            talk->CloseGossip();
            HostSysMessage(player, "cannot start: " + refused);
            return true;
        }
        talk->CloseGossip();
        SendBg(player, frames);
        HostSysMessage(player, "a 4v4 game started. Leave it with .hs bg leave or the host's option.");
        return true;
    }
    talk->CloseGossip();
    return true;
}
