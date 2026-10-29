/*
 * mod-hearthstone: match glue, see HsMatchGlue.h.
 *
 * Threading. The hooks run on different threads:
 *   - CMSG_GOSSIP_HELLO / CMSG_GOSSIP_SELECT_OPTION (PACKET_PROCESS_MAP): map threads;
 *   - CMSG_CAST_SPELL, CMSG_ATTACKSWING, CMSG_PET_ACTION (spells), CMSG_USE_ITEM, CMSG_GAMEOBJ_USE,
 *     CMSG_ACTIVATETAXI*, CMSG_INITIATE_TRADE (map) and the loot opcodes (self items): the packet
 *     thread of the player's session, concurrent to the world thread;
 *   - CMSG_MESSAGECHAT (the GAME_* ops), WorldScript::OnUpdate (the AI tick) and the `.hs match`
 *     commands: the world thread;
 *   - OnCreatureRemoveWorld / OnUnitDeath of the opponent creature: the creature's map thread (the
 *     match ends there, its cleanup is deferred to the world thread, see RunDeferredEnds);
 *   - player hooks (teleport, map change, death, logout): the thread that runs the player's action.
 * The aura of a match is added and removed from whichever of these threads ends or starts the match,
 * like the core's own chat commands do from the world thread.
 *
 * Locks, outermost first (a lock is never taken while holding one to its left):
 *   g_mutex (session registry, HearthstoneScripts.cpp)
 *     -> match service mutex (leaf: HsMatches(), never calls out)
 *     -> deck service mutex -> collection service mutex (only from the COLL/DECK ops, not from here)
 *   g_regMutex (deck library and opponent registry snapshots) and g_bindMutex (character -> opponent
 *     creature / aura binding) are independent leaf locks: nothing is called while holding them. The
 *     registry is an immutable snapshot (shared_ptr): callers copy the pointer and work without a lock.
 * Everything that touches the core (adding or removing the aura, sending frames, gossip menus, log)
 * happens after every lock was released. The match service reports what has to be done
 * (payloads to push, a match that ended) and this file does it.
 *
 * The cheap per-packet check: HsMatches().RunningCount() is an atomic counter; a packet of a
 * character is only looked at further while at least one match runs on the server.
 */

#include "ScriptObjects.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsDeckStore.h"
#include "HsEconomyGlue.h"
#include "HsIntroGlue.h"
#include "HsNaxxItems.h"
#include "HsClassQuestGlue.h"
#include "HsNaxxQuestGlue.h"
#include "HsWeeklyQuestGlue.h"
#include "HsLadderTable.h"
#include "HsBgBreadcrumb.h"
#include "HsLadderBreadcrumb.h"
#include "HsNaxxBreadcrumb.h"
#include "HsAllianceMageQuest.h"
#include "quests/HsAllianceMageRules.h"
#include "HsHunterQuest.h"
#include "quests/HsHunterRules.h"
#include "HsHordeShamanQuest.h"
#include "HsWarlockQuest.h"
#include "quests/HsWarlockRules.h"
#include "HsHordeDruidQuest.h"
#include "HsHordeWarriorQuest.h"
#include "HsHordePriestQuest.h"
#include "HsAllianceShamanQuest.h"
#include "HsHordePaladinQuest.h"
#include "HsAlliancePaladinQuest.h"
#include "HsAlliancePriestQuest.h"
#include "quests/HsAlliancePriestRules.h"
#include "quests/HsHordePaladinRules.h"
#include "HsHordeRogueQuest.h"
#include "HsAllianceRogueQuest.h"
#include "HsCardTrader.h"
#include "HsAllianceDruidQuest.h"
#include "HsArenaGlue.h"
#include "HsArenaHost.h"
#include "HsBgGlue.h"
#include "HsDataset.h"
#include "HsLadderDuelStore.h"
#include "quests/HsAllianceDruidRules.h"
#include "HsMatchGlue.h"
#include "HsMatchStore.h"
#include "match/HsEncounters.h"
#include "match/HsOpponents.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

uint64_t HsNowMs()
{
    // 64-bit monotonic clock: WorldTimer::getMSTime() wraps after ~49 days.
    static auto const epoch = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - epoch).count());
}

namespace
{
    // Gossip. The sender is the module's reserved one (the other modules use 0x4352 crafting orders and
    // 0x52424B01 crafting storage; the core 0 and 1..10), the actions are looked up server side from the
    // stored menu, so the client cannot forge them (it only sends the list index).
    constexpr uint32 kSender = 0x4853u; // 'HS'
    constexpr uint32 kActionPlay = 0x48530001u;
    constexpr uint32 kActionNever = 0x48530002u;
    // A difficulty entry: kActionDifficultyBase + the Hs::Difficulty (Normal 0, Hard 1).
    constexpr uint32 kActionDifficultyBase = 0x48530010u;
    // A deck entry: the high bit, the difficulty in bits 28..29 (Normal 0, Hard 1, Boss 2) and the deck
    // id in the low 28 bits (ids stay far below 2^28).
    constexpr uint32 kActionDeckFlag = 0x80000000u;
    constexpr uint32 kDeckDifficultyShift = 28;
    constexpr uint32 kDeckDifficultyMask = 0x3u;
    constexpr uint32 kMaxDeckId = 0x0FFFFFFFu;
    constexpr uint32 kMaxMenuDecks = 100;
    // The deck menu shows kDecksPerPage decks, with "Previous page" / "Next page" entries: kActionDeckPageBase +
    // the difficulty * kDeckPageStride + the page (block 0x48530600..0x485306ff).
    constexpr uint32 kDecksPerPage = 10;
    constexpr uint32 kActionDeckPageBase = 0x48530600u;
    constexpr uint32 kDeckPageStride = 0x40u;

    // The client's Help > Stuck spell.
    constexpr uint32 kStuckSpellId = 7355;

    // The deck library and the opponent registry: immutable snapshots, replaced as a whole by
    // LoadOpponents at startup and on a config reload.
    std::mutex g_regMutex;
    std::shared_ptr<Hs::DeckLibrary const> g_library;
    std::shared_ptr<Hs::OpponentRegistry const> g_registry;
    // The names the match-finder's random opponents take (hearthstone_finder_npc).
    std::shared_ptr<std::vector<Hs::FinderName> const> g_finderNames;
    std::atomic<bool> g_matchEnabled{true};

    std::atomic<uint32> g_engineCards{0};
    std::atomic<uint32> g_engineSupported{0};

    // Character guid -> what a running match is bound to (the opponent creature and the aura applied).
    struct Binding
    {
        uint64 creature = 0; // ObjectGuid raw value of the opponent creature
        uint32 entry = 0;
        uint32 mapId = 0;
        uint32 aura = 0;     // the spell applied (0: the match runs without the effect)
    };
    std::mutex g_bindMutex;
    std::unordered_map<uint32, Binding> g_bindings;

    // ---- small helpers ------------------------------------------------------

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

    std::shared_ptr<Hs::OpponentRegistry const> Registry()
    {
        std::lock_guard<std::mutex> lock(g_regMutex);
        return g_registry;
    }

    std::shared_ptr<Hs::DeckLibrary const> Library()
    {
        std::lock_guard<std::mutex> lock(g_regMutex);
        return g_library;
    }

    std::shared_ptr<std::vector<Hs::FinderName> const> FinderNames()
    {
        std::lock_guard<std::mutex> lock(g_regMutex);
        return g_finderNames;
    }

    bool IsOpponentEntry(uint32 entry)
    {
        if (!g_matchEnabled.load(std::memory_order_relaxed))
            return false;
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        return registry && registry->Has(entry);
    }

    std::size_t OpponentCount()
    {
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        return registry ? registry->Count() : 0;
    }

    // Builds the deck library (hearthstone_deck_template) and the opponent registry (hearthstone_opponent
    // rows plus the Hearthstone.Match.Opponents config) and swaps them in. Needs the engine's CardDb (the
    // decks are checked against it); without it, or with matches disabled, nobody is an opponent. Logs the
    // counts and every deck or row that is skipped, with the reason.
    void LoadOpponents(Hs::MatchSettings const& settings)
    {
        auto library = std::make_shared<Hs::DeckLibrary const>();
        auto registry = std::make_shared<Hs::OpponentRegistry const>();
        auto finderNames = std::make_shared<std::vector<Hs::FinderName>>();
        std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
        if (!db)
            sLog.outError("Hearthstone: the card data is not loaded: no library deck can be checked, nobody can be "
                "an opponent and no starter deck can be awarded");
        // The library is built even with matches disabled, it holds the starter recipes.
        if (db)
        {
            std::vector<Hs::LibraryDeckRow> libraryRows;
            LoadHsLibraryRows(libraryRows);
            std::vector<std::string> errors;
            library = std::make_shared<Hs::DeckLibrary const>(Hs::DeckLibrary::Build(*db, libraryRows, errors));
            for (std::string const& error : errors)
                sLog.outError("Hearthstone: opponent decks: %s", error.c_str());
            sLog.outString("Hearthstone: deck library: %u decks, %u ready", static_cast<uint32>(library->Decks().size()),
                static_cast<uint32>(library->ReadyCount()));
        }
        if (settings.enable && db)
        {
            std::vector<Hs::OpponentRow> opponentRows;
            std::vector<uint32> finders;
            std::vector<Hs::NpcUnlockRow> unlocks;
            LoadHsOpponentRows(opponentRows);
            LoadHsFinderEntries(finders);
            LoadHsFinderNames(*finderNames);
            LoadHsNpcUnlockRows(unlocks);

            std::vector<std::string> errors;
            registry = std::make_shared<Hs::OpponentRegistry const>(
                Hs::OpponentRegistry::Build(*db, *library, opponentRows, settings.opponents, finders, unlocks, errors));
            for (std::string const& error : errors)
                sLog.outError("Hearthstone: opponents: %s", error.c_str());
            std::string list;
            for (uint32 entry : registry->Entries())
            {
                std::string modes;
                for (Hs::Difficulty d : registry->Offered(entry))
                    modes += (modes.empty() ? "" : "/") + std::string(Hs::DifficultyCode(d));
                if (registry->UnlockQuest(entry) != 0)
                    continue; // the quest NPCs get their own line
                list += (list.empty() ? "" : ", ") + std::to_string(entry) + (registry->IsFinder(entry) ? " finder" : "") +
                    " (" + modes + ")";
            }
            std::vector<Hs::OpponentRegistry::UnlockNpc> const npcs = registry->UnlockNpcs();
            sLog.outString("Hearthstone: %u of %u quest NPC opponents usable (each needs its quest turned in by a "
                "character of the account)", static_cast<uint32>(npcs.size()), static_cast<uint32>(unlocks.size()));
            if (!finders.empty())
            {
                uint32 alliance = 0, horde = 0, any = 0;
                for (Hs::FinderName const& n : *finderNames)
                    (n.faction == "alliance" ? alliance : n.faction == "horde" ? horde : any) += 1;
                sLog.outString("Hearthstone: match-finder: %u creature(s), pools normal %u / hard %u decks, opponent "
                    "names %u alliance / %u horde / %u any", static_cast<uint32>(finders.size()),
                    static_cast<uint32>(registry->PoolSize(finders.front(), Hs::Difficulty::Normal)),
                    static_cast<uint32>(registry->PoolSize(finders.front(), Hs::Difficulty::Hard)), alliance, horde, any);
            }
            sLog.outString("Hearthstone: %u opponent creature(s) from %u database row(s) and %u config entr%s%s%s",
                static_cast<uint32>(registry->Count()), static_cast<uint32>(opponentRows.size()),
                static_cast<uint32>(settings.opponents.size()), settings.opponents.size() == 1 ? "y" : "ies",
                list.empty() ? "" : ": ", list.c_str());
        }
        std::lock_guard<std::mutex> lock(g_regMutex);
        g_library = library;
        g_registry = registry;
        g_finderNames = finderNames;
    }

    // ---- quest NPC unlocks ---------------------------------------------------------

    std::string CreatureName(uint32 entry)
    {
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        return info ? info->name : "creature " + std::to_string(entry);
    }

    uint32 AccountOf(Player* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    // Unlocks the quest NPCs whose quest is in completed for the player's account and tells the player about each
    // new one. source: login:<guid>, reward:<guid> or talk:<guid>.
    void ReconcileUnlocks(Player* player, Hs::OpponentRegistry const& registry, std::set<uint32_t> const& completed,
                          char const* how)
    {
        uint32 const accountId = AccountOf(player);
        if (!accountId || completed.empty())
            return;
        std::vector<uint32_t> added;
        std::string const source = std::string(how) + ":" + std::to_string(player->GetGUIDLow());
        if (!HsNpcUnlocks().Reconcile(accountId, registry.UnlockNpcs(), completed, source, added))
            sLog.outError("Hearthstone: quest NPC unlocks of account %u (%s) could not all be written", accountId,
                source.c_str());
        for (uint32 entry : added)
        {
            std::string const name = CreatureName(entry);
            SysMessage(player, Hs::ClassQuestOpponentOf(entry)
                ? name + " has joined your account's random-opponent roster."
                : name + " will now play Hearthstone with you (and your other characters).");
            sLog.outString("Hearthstone: account %u unlocked the quest NPC %s (%u), %s", accountId, name.c_str(), entry,
                source.c_str());
        }
    }

    // The quests of the quest NPCs that this character has turned in (in memory, so a turn-in that is not saved yet
    // counts).
    std::set<uint32_t> RewardedByCharacter(Player* player, Hs::OpponentRegistry const& registry)
    {
        std::set<uint32_t> out;
        for (Hs::OpponentRegistry::UnlockNpc const& npc : registry.UnlockNpcs())
            if (player->GetQuestRewardStatus(npc.questId))
                out.insert(npc.questId);
        return out;
    }

    // May the player's account play the creature entry? True for every opponent that needs no unlock. For a quest NPC:
    // unlocked for the account, or unlocked now because this character turned in the quest (tryUnlock).
    bool OpponentAvailable(Player* player, Hs::OpponentRegistry const& registry, uint32 entry, bool tryUnlock)
    {
        if (!HsClassQuestOpponentAvailable(player, entry))
            return false;
        // A Naxxramas boss stand-in plays its normal match only for its open boss quest.
        if (!HsNaxxOpponentAvailable(player, entry))
            return false;
        // A class NPC's direct duel is its quest objective, before the roster unlock at the turn-in.
        if (Hs::ClassQuestOpponentOf(entry))
            return true;
        uint32 const quest = registry.UnlockQuest(entry);
        if (quest == 0)
            return true;
        uint32 const accountId = AccountOf(player);
        if (!accountId)
            return false;
        if (HsNpcUnlocks().IsUnlocked(accountId, entry))
            return true;
        if (!tryUnlock || !player->GetQuestRewardStatus(quest))
            return false;
        ReconcileUnlocks(player, registry, { quest }, "talk");
        return HsNpcUnlocks().IsUnlocked(accountId, entry);
    }

    // Class-quest duels keep their prescribed difficulty. The finder uses the registry's full roster pools.
    std::vector<Hs::Difficulty> DirectDifficulties(Hs::OpponentRegistry const& registry, uint32 entry)
    {
        std::vector<Hs::Difficulty> offered = registry.Offered(entry);
        if (Hs::ClassQuestOpponentOf(entry))
            offered.erase(std::remove_if(offered.begin(), offered.end(),
                [](Hs::Difficulty difficulty) { return difficulty != Hs::Difficulty::Normal; }), offered.end());
        return offered;
    }

    // The quest NPCs of the player's side that the account unlocked (the extra match-finder candidates).
    std::vector<uint32_t> UnlockedNpcsOfSide(Player* player, Hs::OpponentRegistry const& registry)
    {
        std::vector<uint32_t> out;
        std::map<uint32_t, uint32_t> unlocked;
        if (!HsNpcUnlocks().Get(AccountOf(player), unlocked))
            return out;
        char const* const side = player->GetTeam() == ALLIANCE ? "alliance" : "horde";
        for (Hs::OpponentRegistry::UnlockNpc const& npc : registry.UnlockNpcs())
            if (npc.faction == side && unlocked.count(npc.entry))
                out.push_back(npc.entry);
        return out;
    }

    void EnsureGossipFlag(Creature* creature)
    {
        if (creature && !creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP))
            creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    void MarkLoadedOpponents()
    {
        if (OpponentCount() == 0)
            return;
        // GetCreatureBySpawnIdStore returns a copy, so a grid unload cannot invalidate the traversal.
        sMapMgr.DoForAllMaps([](Map* map)
        {
            if (!map)
                return;
            for (auto const& pair : map->GetCreatureBySpawnIdStore())
                if (pair.second && IsOpponentEntry(pair.second->GetEntry()))
                    EnsureGossipFlag(pair.second);
        });
    }

    // ---- the In Card Match aura ---------------------------------------------

    enum class EffectState { Unset, Ok, Missing };

    EffectState GetEffectState(uint32 spellId)
    {
        if (spellId == 0)
            return EffectState::Unset;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell || !spell->IsSpellAppliesAura((1 << EFFECT_INDEX_0) | (1 << EFFECT_INDEX_1) | (1 << EFFECT_INDEX_2)))
            return EffectState::Missing;
        return EffectState::Ok;
    }

    void LogAuraState()
    {
        uint32 const id = sHearthstoneConfig.Get().matchAuraSpellId;
        switch (GetEffectState(id))
        {
            case EffectState::Unset:
                sLog.outError("Hearthstone: WARNING Hearthstone.MatchAuraSpellId is 0: matches start without the "
                    "In Card Match effect (no root, no damage immunity). The action block, Help > Stuck and every "
                    "end condition still work.");
                break;
            case EffectState::Missing:
                sLog.outError("Hearthstone: WARNING Hearthstone.MatchAuraSpellId %u does not exist in the spell data "
                    "or has no aura effect: matches start without the In Card Match effect", id);
                break;
            case EffectState::Ok:
                sLog.outString("Hearthstone: In Card Match effect: spell %u", id);
                break;
        }
    }

    // Adds the match aura. True when the match may run: no effect configured (applied = 0) or the
    // aura is on the player. False: the effect is configured and could not be applied.
    bool ApplyProtection(Player* player, uint32& applied)
    {
        applied = 0;
        uint32 const id = sHearthstoneConfig.Get().matchAuraSpellId;
        if (GetEffectState(id) != EffectState::Ok)
            return true;
        player->RemoveAurasDueToSpell(id); // a stale copy
        player->AddAura(id);
        if (!player->HasAura(id))
            return false;
        applied = id;
        return true;
    }

    // ---- the binding and the cleanup ------------------------------------------

    void Bind(uint32 guid, Binding const& binding)
    {
        std::lock_guard<std::mutex> lock(g_bindMutex);
        g_bindings[guid] = binding;
    }

    bool Unbind(uint32 guid, Binding& out)
    {
        std::lock_guard<std::mutex> lock(g_bindMutex);
        auto const it = g_bindings.find(guid);
        if (it == g_bindings.end())
            return false;
        out = it->second;
        g_bindings.erase(it);
        return true;
    }

    // Removes the aura this feature applied to the character (idempotent, never touches another
    // spell) and forgets the binding. player may be null (looked up when still in the world).
    void ReleaseProtection(Player* player, uint32 guid)
    {
        Binding binding;
        if (!Unbind(guid, binding) || binding.aura == 0)
            return;
        if (!player)
            player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
        if (player)
            player->RemoveAurasDueToSpell(binding.aura);
    }

    // One hearthstone_match_history row per ended match. Every end of a match reaches LogEnd exactly
    // once (the service reports an end once: Act, Tick, Abort and Drop each return it only for a match that was
    // running). Runs on the thread that ends the match; the insert is only queued to the database thread, and
    // nothing here may change how the match ends: a failure is logged and swallowed.
    void RecordHistory(Hs::MatchEnd const& end)
    {
        if (!sHearthstoneConfig.GetMatch().historyEnable)
            return;
        try
        {
            WriteHsMatchHistory(Hs::BuildMatchHistoryRow(end, static_cast<int64_t>(std::time(nullptr))));
        }
        catch (std::exception const& e)
        {
            sLog.outError("Hearthstone: match history of match #%u failed: %s", end.matchId, e.what());
        }
        catch (...)
        {
            sLog.outError("Hearthstone: match history of match #%u failed", end.matchId);
        }
    }

    void LogEnd(Hs::MatchEnd const& end)
    {
        RecordHistory(end);
        // The coins of a normally completed match (nothing for a surrender or an interrupted match).
        // A Draft Arena duel pays no per-match coins, its prize comes with the end of the run. A ladder
        // duel is a durable match too (end.arena) but pays the ordinary coins of its opponent's difficulty.
        bool const arenaRun = end.arena && !Hs::Ladder::IsLadderRun(end.arenaRunId);
        if (!arenaRun)
            HsEconomyMatchEnded(end);
        HsClassQuestMatchEnded(end);
        HsNaxxMatchEnded(end);
        HsWeeklyMatchEnded(end);
        HsLadderMatchEnded(end);
        sLog.outString("Hearthstone: match #%u of %s ended against %s: %s (%s) after %d turn(s)", end.matchId,
            end.humanName.c_str(), end.opponentName.c_str(), end.res.c_str(), end.why.c_str(), end.turn);
        // The result goes to the run once (the service reports an end once); settlement is HsArenaSettle.h.
        if (arenaRun)
            HsArenaDuelEnded(end);
    }

    // Sends a GAME push to the character when it is handshaken and in the world.
    void SendGamePush(Player* player, uint32 guid, std::string const& payload)
    {
        if (!sHearthstoneConfig.Get().enable || !HsSessionHandshaken(guid))
            return;
        if (!player)
            player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
        if (!player)
            return;
        std::vector<std::string> frames;
        Hs::AppendGame(frames, payload);
        for (std::string const& frame : frames)
            player->SendAddonMessage(Hs::kPrefix, frame);
    }

    // ---- Draft Arena duels --------------------------------------------------------------

    // The default sink, until the arena storage installs its own: nothing is kept.
    class NullArenaSink : public HsArenaDuelSink
    {
    public:
        bool StoreActions(uint32_t, uint64_t, uint32_t, std::vector<Hs::LoggedAction> const&) override { return true; }

        void StoreSuspended(Hs::SuspendedDuel const& duel, char const* why) override
        {
            sLog.outError("Hearthstone: arena duel of run %llu (guid %u) suspended (%s) with %u action(s), but no arena "
                "storage is installed: the duel is lost", static_cast<unsigned long long>(duel.params.arenaRunId),
                duel.params.guid, why, static_cast<uint32>(duel.log.size()));
        }
    };
    NullArenaSink g_nullArenaSink;
    std::atomic<HsArenaDuelSink*> g_arenaSink{nullptr};

    HsArenaDuelSink& ArenaSink()
    {
        HsArenaDuelSink* sink = g_arenaSink.load(std::memory_order_acquire);
        return sink ? *sink : g_nullArenaSink;
    }

    // Stores the log entries an action of an arena duel added; true when the payload may be sent (an ordinary
    // match adds none).
    bool StoreArenaActions(uint32 guid, uint64 runId, uint32 first, std::vector<Hs::LoggedAction> const& logged)
    {
        if (logged.empty())
            return true;
        // A ladder duel (the run id has the ladder bit) goes to the ladder duel store.
        if (Hs::Ladder::IsLadderRun(runId))
            return HsLadderDuels().StoreActions(guid, runId, first, logged);
        return ArenaSink().StoreActions(guid, runId, first, logged);
    }

    // The core work after a duel was suspended (the service no longer has it): remove the effect, hand the duel to
    // the sink, tell the client that its match is gone for now (push: a logout has nobody to tell).
    void FinishSuspended(Player* player, uint32 guid, Hs::SuspendedDuel const& duel, char const* why, bool push)
    {
        ReleaseProtection(player, guid);
        if (Hs::Ladder::IsLadderRun(duel.params.arenaRunId))
            HsLadderDuels().StoreSuspended(duel, why); // it logs itself and tells the chair
        else
        {
            ArenaSink().StoreSuspended(duel, why);
            sLog.outString("Hearthstone: arena duel of run %llu (guid %u) suspended (%s) after %u action(s)",
                static_cast<unsigned long long>(duel.params.arenaRunId), guid, why, static_cast<uint32>(duel.log.size()));
        }
        if (push)
            SendGamePush(player, guid, Hs::EncodeGameNone());
    }

    // Suspends the character's running arena duel. False for no match, an ordinary or a finished one.
    bool SuspendArena(Player* player, uint32 guid, char const* why, bool push = true)
    {
        if (!HsMatches().IsArena(guid) || !HsMatches().IsRunning(guid))
            return false;
        Hs::SuspendedDuel duel;
        if (!HsMatches().Suspend(guid, duel, HsNowMs()))
            return false;
        FinishSuspended(player, guid, duel, why, push);
        return true;
    }

    // The core work after a match ended: remove the effect, log, push the final state.
    void FinishEnded(Player* player, uint32 guid, Hs::MatchEnd const& end, std::string const& payload)
    {
        ReleaseProtection(player, guid);
        LogEnd(end);
        SendGamePush(player, guid, payload);
    }

    // Ends the character's running match from outside. True if there was one.
    bool InterruptMatch(Player* player, uint32 guid, Hs::Interrupt why)
    {
        // An arena duel is suspended, not ended.
        if (SuspendArena(player, guid, Hs::InterruptCode(why)))
            return true;
        Hs::MatchAbortOutcome const r = HsMatches().Abort(guid, why);
        if (!r.ended)
            return false;
        FinishEnded(player, guid, r.end, r.payload);
        return true;
    }

    // Cheap form for hooks that run for every player: nothing happens unless a match runs.
    void InterruptIfRunning(Player* player, Hs::Interrupt why)
    {
        if (!player || HsMatches().RunningCount() == 0)
            return;
        InterruptMatch(player, player->GetGUIDLow(), why);
    }

    // Logout: the match is dropped, nothing is kept and nothing is pushed.
    void DropMatch(Player* player, uint32 guid)
    {
        // A running arena duel is suspended at logout (nothing to push to).
        if (SuspendArena(player, guid, "logout", false))
            return;
        Hs::MatchEnd end;
        bool const wasRunning = HsMatches().Drop(guid, &end);
        // Always: a finished match whose cleanup is still deferred (RunDeferredEnds) keeps its binding.
        ReleaseProtection(player, guid);
        if (wasRunning)
            LogEnd(end);
    }

    // Matches the creature hooks ended. Those hooks run on the creature's map thread, where another
    // player must not be touched; the effect removal, the log and the push wait for HsMatchUpdate
    // (the world thread, after every map update has finished).
    struct DeferredEnd
    {
        uint32 guid = 0;
        Hs::MatchEnd end;
        std::string payload;
        bool suspended = false; // an arena duel that was suspended (duel), not ended
        Hs::SuspendedDuel duel;
    };
    std::mutex g_deferMutex;
    std::vector<DeferredEnd> g_deferred;

    void RunDeferredEnds()
    {
        std::vector<DeferredEnd> pending;
        {
            std::lock_guard<std::mutex> lock(g_deferMutex);
            pending.swap(g_deferred);
        }
        for (DeferredEnd const& d : pending)
        {
            if (d.suspended)
                FinishSuspended(nullptr, d.guid, d.duel, "npc", true);
            else
                FinishEnded(nullptr, d.guid, d.end, d.payload);
        }
    }

    // The opponent creature left the world or died: end the matches bound to it.
    void InterruptByCreature(uint64 creatureRaw)
    {
        if (HsMatches().RunningCount() == 0)
            return;
        std::vector<uint32> guids;
        {
            std::lock_guard<std::mutex> lock(g_bindMutex);
            for (auto const& entry : g_bindings)
                if (entry.second.creature == creatureRaw)
                    guids.push_back(entry.first);
        }
        for (uint32 guid : guids)
        {
            // An arena duel is suspended (the service part now, the rest on the world thread).
            if (HsMatches().IsArena(guid) && HsMatches().IsRunning(guid))
            {
                DeferredEnd d;
                d.guid = guid;
                d.suspended = HsMatches().Suspend(guid, d.duel, HsNowMs());
                if (d.suspended)
                {
                    std::lock_guard<std::mutex> lock(g_deferMutex);
                    g_deferred.push_back(std::move(d));
                    continue;
                }
            }
            Hs::MatchAbortOutcome const r = HsMatches().Abort(guid, Hs::Interrupt::Npc);
            if (!r.ended)
                continue;
            DeferredEnd d;
            d.guid = guid;
            d.end = r.end;
            d.payload = r.payload;
            std::lock_guard<std::mutex> lock(g_deferMutex);
            g_deferred.push_back(std::move(d));
        }
    }

    // ---- start -----------------------------------------------------------------

    // Everything that has to hold before the deck menu is shown and again when a deck is chosen.
    bool CheckStartPrerequisites(Player* player, std::string& why, bool ladderDuelOk = false)
    {
        // One card duel per account. A ladder duel (paused or not) blocks every other start; its own resume
        // passes ladderDuelOk.
        if (!ladderDuelOk && HsLadderDuels().Has(AccountOf(player)))
        {
            why = "You have a ladder duel to finish first. Sit down on a ladder chair to resume it or to surrender.";
            return false;
        }
        if (!g_matchEnabled.load(std::memory_order_relaxed))
        {
            why = "Card matches are switched off on this server.";
            return false;
        }
        if (!HsMatches().CardDb())
        {
            why = "Card matches are not available right now (the card data is not loaded, see the server log).";
            return false;
        }
        if (!sHearthstoneConfig.Get().enable || !HsSessionHandshaken(player->GetGUIDLow()))
        {
            why = "The Hearthstone addon is not running. Install it and /reload, then talk to me again.";
            return false;
        }
        if (HsMatches().IsRunning(player->GetGUIDLow()))
        {
            why = "You are already in a match.";
            return false;
        }
        // A Battlegrounds game blocks a card match, a ladder duel and an arena duel (and the reverse).
        if (HsBgBusy(player->GetGUIDLow()))
        {
            why = "You are in a Battlegrounds game. Finish it or leave with .hs bg leave.";
            return false;
        }
        // No matches (the opponent finder included) before the lesson of Ezra / Ibelin is complete.
        if (!HsLessonDone(player))
        {
            why = std::string("You have to finish the Hearthstone lesson first. ") +
                (player->GetTeam() == ALLIANCE ? "Ibelin Redmoore" : "Ezra Chatterton") + " teaches it.";
            return false;
        }
        if (!player->IsAlive())
        {
            why = "You cannot start a match while you are dead.";
            return false;
        }
        if (player->IsInCombat())
        {
            why = "You cannot start a match while you are in combat.";
            return false;
        }
        // Every member of the group that is in the world, companion bots included.
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref != nullptr; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (member && member != player && member->IsInWorld() && member->IsInCombat())
                {
                    why = std::string(member->GetName()) + " in your group is in combat. Everyone has to be out of combat.";
                    return false;
                }
            }
        }
        return true;
    }

    void Refuse(Player* player, Creature* creature, std::string const& why)
    {
        if (player->PlayerTalkClass)
            player->PlayerTalkClass->CloseGossip();
        SysMessage(player, why);
        sLog.outString("Hearthstone: refused the match start of %s (guid %u) at creature %u: %s", player->GetName(),
            player->GetGUIDLow(), creature ? creature->GetEntry() : 0u, why.c_str());
    }

    // The decks of the character that CheckForMatch accepts, at most kMaxMenuDecks, by deck id.
    bool ReadyDecks(Player* player, std::vector<std::pair<uint32, std::string>>& out, std::string& why)
    {
        uint32 const accountId = player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        std::string const startClass = Hs::DeckClassOfWowClass(player->getClass());
        Hs::DeckList list;
        if (accountId == 0 || !HsDecks().List(accountId, startClass, list))
        {
            why = "Your decks are not available right now. Try again later.";
            return false;
        }
        std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
        std::vector<std::string> notReady;
        for (Hs::DeckListEntry const& entry : list.decks)
        {
            if (out.size() >= kMaxMenuDecks)
                break;
            if (entry.deck.id == 0 || entry.deck.id > kMaxDeckId)
                continue;
            Hs::DeckMatchCheck const check = HsDecks().CheckForMatch(accountId, entry.deck.id, startClass);
            if (check.Ready())
            {
                out.emplace_back(entry.deck.id, entry.deck.name + " (" + entry.deck.deckClass + ")");
                continue;
            }
            Hs::Match::CardDef const* card = db && !check.unsupportedCard.empty() ? db->Find(check.unsupportedCard) : nullptr;
            notReady.push_back("\"" + entry.deck.name + "\": " +
                Hs::DeckNotReadyText(check, entry.deck.deckClass, card ? card->name : std::string()) + ".");
        }
        if (out.empty())
        {
            if (list.decks.empty())
            {
                why = "You have no decks. Build a deck of 30 cards in the Hearthstone window (/hs decks).";
                return false;
            }
            why = "None of your decks is ready for a match.";
            // At most three reasons: a system message line should stay readable.
            for (std::size_t i = 0; i < notReady.size() && i < 3; ++i)
                why += " " + notReady[i];
            if (notReady.size() > 3)
                why += " (and " + std::to_string(notReady.size() - 3) + " more; .hs deck check shows each)";
            return false;
        }
        return true;
    }

    // Difficulty menu: "Normal" / "Hard" (the difficulties the opponent offers) plus "Never mind".
    void ShowDifficultyMenu(Player* player, Creature* creature, std::vector<Hs::Difficulty> const& offered)
    {
        std::string why;
        if (!CheckStartPrerequisites(player, why))
        {
            Refuse(player, creature, why);
            return;
        }
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        bool const finder = registry && registry->IsFinder(creature->GetEntry());
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        for (Hs::Difficulty difficulty : offered)
        {
            if (difficulty == Hs::Difficulty::Boss)
                continue; // a boss has no difficulty choice
            std::string const label = finder ? "Random opponent (" + std::string(Hs::DifficultyName(difficulty)) + ")"
                : std::string(Hs::DifficultyName(difficulty));
            menu.AddMenuItem(GOSSIP_ICON_BATTLE, label, kSender,
                kActionDifficultyBase + static_cast<uint32>(difficulty), "", false);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        menu.AddMenuItem(GOSSIP_ICON_CHAT, "Never mind", kSender, kActionNever, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
        player->SendPreparedGossip(creature);
    }

    // Deck gossip menu: the match-ready decks plus "Never mind". The chosen difficulty travels in the
    // action value of every deck entry (see kDeckDifficultyShift) and is checked again when a deck is chosen.
    void ShowDeckMenu(Player* player, Creature* creature, Hs::Difficulty difficulty, uint32 page = 0)
    {
        std::string why;
        std::vector<std::pair<uint32, std::string>> decks;
        if (!CheckStartPrerequisites(player, why) || !ReadyDecks(player, decks, why))
        {
            Refuse(player, creature, why);
            return;
        }
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        uint32 const pages = std::max<uint32>(1, (static_cast<uint32>(decks.size()) + kDecksPerPage - 1) / kDecksPerPage);
        page = std::min(page, pages - 1);
        uint32 const pageAction = kActionDeckPageBase + static_cast<uint32>(difficulty) * kDeckPageStride;
        for (std::size_t i = page * kDecksPerPage; i < decks.size() && i < (page + 1) * kDecksPerPage; ++i)
        {
            menu.AddMenuItem(GOSSIP_ICON_BATTLE, decks[i].second, kSender,
                kActionDeckFlag | (static_cast<uint32>(difficulty) << kDeckDifficultyShift) | decks[i].first, "", false);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        if (page > 0)
        {
            menu.AddMenuItem(GOSSIP_ICON_CHAT, "Previous page", kSender, pageAction + page - 1, "", false);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        if (page + 1 < pages)
        {
            menu.AddMenuItem(GOSSIP_ICON_CHAT, "Next page", kSender, pageAction + page + 1, "", false);
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        menu.AddMenuItem(GOSSIP_ICON_CHAT, "Never mind", kSender, kActionNever, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
        player->SendPreparedGossip(creature);
    }

    // The deck was chosen: validate everything again, create the match, apply the effect, tell the client.
    // A class challenge (Naxxramas) passes its challenge and deckId 0. It plays the loaner class and deck of the
    // challenge against the stand-in's encounter: no deck is read and none of the player's decks is checked.
    void StartMatch(Player* player, Creature* creature, Hs::Difficulty difficulty, uint32 deckId,
                    Hs::Challenge const* challenge = nullptr)
    {
        std::string why;
        if (!CheckStartPrerequisites(player, why))
        {
            Refuse(player, creature, why);
            return;
        }
        // The registry may have been reloaded since the menu was shown: the opponent must still offer this
        // difficulty (a boss only Boss, an ordinary opponent only Normal / Hard with a ready deck).
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        std::vector<Hs::Difficulty> const offered = registry ? DirectDifficulties(*registry, creature->GetEntry()) : std::vector<Hs::Difficulty>();
        if (std::find(offered.begin(), offered.end(), difficulty) == offered.end())
        {
            Refuse(player, creature, "This opponent does not offer that difficulty (any more).");
            return;
        }
        if (challenge ? !HsNaxxChallengeAvailable(player, creature->GetEntry(), *challenge)
                      : !OpponentAvailable(player, *registry, creature->GetEntry(), false))
        {
            Refuse(player, creature, "This opponent does not play with you yet.");
            return;
        }
        uint32 const guid = player->GetGUIDLow();
        uint32 const accountId = player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        std::string humanClass;
        std::string deckName;
        std::vector<std::string> humanDeck;
        if (challenge)
        {
            std::shared_ptr<Hs::Match::CardDb const> const db = HsMatches().CardDb();
            if (!db || !Hs::ChallengeReady(*db, *challenge))
            {
                Refuse(player, creature, "This challenge is not ready yet.");
                sLog.outError("Hearthstone: the challenge %s is not ready for %s (guid %u)", challenge->key.c_str(),
                    player->GetName(), guid);
                return;
            }
            humanClass = challenge->heroClass;
            deckName = Hs::NaxxClassLabel(challenge->heroClass) + " challenge";
        }
        else
        {
            std::string const startClass = Hs::DeckClassOfWowClass(player->getClass());

            Hs::DeckMatchCheck const check = HsDecks().CheckForMatch(accountId, deckId, startClass);
            if (check.status == Hs::DeckMatchCheck::Status::Missing)
            {
                Refuse(player, creature, "That deck does not exist any more.");
                return;
            }
            if (check.status == Hs::DeckMatchCheck::Status::Unavailable)
            {
                Refuse(player, creature, "Your decks are not available right now. Try again later.");
                return;
            }
            if (!check.Ready())
            {
                Refuse(player, creature, "That deck is not ready for a match (" + check.Text() + ").");
                return;
            }

            Hs::DeckBook book;
            auto deck = book.decks.end();
            if (HsDecks().GetBook(accountId, book))
                deck = book.decks.find(deckId);
            if (deck == book.decks.end() || !Hs::ExpandDeck(deck->second.cards, humanDeck) || humanDeck.size() != Hs::kDeckSize)
            {
                Refuse(player, creature, "That deck could not be read. Try again later.");
                return;
            }
            humanClass = deck->second.deckClass;
            deckName = deck->second.name;
        }

        // One deck of the pool of this opponent and difficulty, chosen at random.
        std::random_device device;
        std::mt19937 rng(device());
        Hs::OpponentPick pick;
        bool const finder = registry->IsFinder(creature->GetEntry());
        uint32_t npcEntry = 0; // the finder picked an unlocked quest NPC
        bool const picked = finder
            ? registry->PickFinder(creature->GetEntry(), difficulty, UnlockedNpcsOfSide(player, *registry), rng, pick, npcEntry)
            : registry->Pick(creature->GetEntry(), difficulty, rng, pick);
        if (!picked)
        {
            Refuse(player, creature, "This opponent has no deck to play with.");
            sLog.outError("Hearthstone: opponent creature %u has no deck for difficulty %s", creature->GetEntry(),
                Hs::DifficultyCode(difficulty));
            return;
        }

        Hs::MatchStartParams params;
        params.guid = guid;
        params.humanName = player->GetName();
        params.humanClass = humanClass;
        params.humanDeck = std::move(humanDeck);
        params.opponentName = creature->GetName();
        // A match-finder's opponent is an unlocked quest NPC under its own name, or a library deck under the
        // name of a random real NPC of the player's faction (hearthstone_finder_npc).
        if (npcEntry != 0)
            params.opponentName = CreatureName(npcEntry);
        else if (finder)
        {
            std::shared_ptr<std::vector<Hs::FinderName> const> const names = FinderNames();
            std::string const name = names ? Hs::PickFinderName(*names, player->GetTeam() == ALLIANCE, rng) : std::string();
            if (!name.empty())
                params.opponentName = name;
        }
        params.opponentClass = pick.deckClass;
        params.opponentDeck = pick.deck;
        params.seed = std::random_device()();
        params.difficulty = Hs::DifficultyCode(pick.difficulty);
        params.profile = pick.profile;
        params.opponentHeroHealth = pick.overrides.heroHealth;
        params.opponentHeroArmor = pick.overrides.heroArmor;
        params.opponentHeroCardId = pick.overrides.heroCardId;
        params.opponentHeroPowerId = pick.overrides.heroPowerId;
        // The stand-in's encounter has to be the one the challenge is played against.
        if (challenge && pick.encounterKey != challenge->encounterKey)
        {
            Refuse(player, creature, "This opponent has no deck to play with.");
            sLog.outError("Hearthstone: opponent creature %u plays the encounter '%s', not that of the challenge %s",
                creature->GetEntry(), pick.encounterKey.c_str(), challenge->key.c_str());
            return;
        }
        // A boss encounter adds its start state (boards, weapon, extra hand cards, forced first side).
        if (!pick.encounterKey.empty())
        {
            Hs::Encounter const* const encounter = Hs::FindEncounter(pick.encounterKey);
            if (!encounter)
            {
                Refuse(player, creature, "This opponent has no deck to play with.");
                sLog.outError("Hearthstone: opponent creature %u names the unknown encounter %s", creature->GetEntry(),
                    pick.encounterKey.c_str());
                return;
            }
            Hs::ApplyEncounter(*encounter, params);
        }
        // A class challenge plays its loaner class and deck against the stand-in's encounter.
        if (challenge)
            Hs::ApplyChallenge(*challenge, params);
        // The identity of the match for its history row.
        params.meta.accountId = accountId;
        params.meta.opponentEntry = creature->GetEntry();
        params.meta.deckId = deckId;
        params.meta.deckName = deckName;
        params.meta.opponentDeckKey = pick.deckKey;
        params.meta.startedAt = static_cast<int64_t>(std::time(nullptr));

        Hs::MatchStartResult const started = HsMatches().Start(params, HsNowMs());
        if (!started.ok)
        {
            Refuse(player, creature, started.error == "running" ? std::string("You are already in a match.")
                : "The match could not be started (" + started.error + ").");
            if (started.error != "running")
                sLog.outError("Hearthstone: could not create a match for %s (guid %u): %s", player->GetName(), guid,
                    started.error.c_str());
            return;
        }

        // The protection comes only after the match exists, and a failure undoes the match: nothing
        // may stay behind that strands a rooted or protected character.
        Binding binding;
        if (!ApplyProtection(player, binding.aura))
        {
            HsMatches().Drop(guid);
            player->RemoveAurasDueToSpell(sHearthstoneConfig.Get().matchAuraSpellId);
            Refuse(player, creature, "The match could not be started (the In Card Match effect failed).");
            sLog.outError("Hearthstone: the In Card Match effect (spell %u) could not be applied to %s (guid %u); "
                "the match #%u was dropped", sHearthstoneConfig.Get().matchAuraSpellId, player->GetName(), guid,
                started.matchId);
            return;
        }
        binding.creature = creature->GetObjectGuid().GetRawValue();
        binding.entry = creature->GetEntry();
        binding.mapId = player->GetMapId();
        Bind(guid, binding);
        HsClassQuestMatchStarted(player, creature, started.matchId);
        HsNaxxMatchStarted(player, creature, started.matchId, challenge);
        HsWeeklyMatchStarted(player, started.matchId, finder && difficulty == Hs::Difficulty::Hard);

        player->PlayerTalkClass->CloseGossip();
        SendGamePush(player, guid, started.payload);
        std::string const deckLabel = challenge ? "challenge " + challenge->key : "deck #" + std::to_string(deckId);
        sLog.outString("Hearthstone: match #%u started: %s (guid %u, %s %s) against %s (creature %u, %s, deck %s "
            "%s), seed %u, effect %s", started.matchId, player->GetName(), guid, deckLabel.c_str(), params.humanClass.c_str(),
            params.opponentName.c_str(), creature->GetEntry(), params.difficulty.c_str(), pick.deckKey.c_str(),
            params.opponentClass.c_str(), params.seed, binding.aura ? std::to_string(binding.aura).c_str() : "none");
    }

    // ---- gossip hooks ------------------------------------------------------------

    bool HasOurItem(GossipMenu& menu)
    {
        for (unsigned int i = 0; i < menu.MenuItemCount(); ++i)
            if (menu.MenuItemSender(i) == kSender)
                return true;
        return false;
    }

    // CMSG_GOSSIP_HELLO was handled: append the option to the menu the core (or the creature's script)
    // just sent and send it again, like the crafting modules do.
    void HandleGossipHello(WorldSession* session, WorldPacket const& packet)
    {
        if (!session || packet.size() < sizeof(uint64))
            return;
        Player* player = session->GetPlayer();
        if (!player || !player->PlayerTalkClass)
            return;

        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid guid;
        copy >> guid;
        // The core ignores a hello it cannot serve and then keeps the old current gossip guid.
        if (!guid.IsAnyTypeCreature() || session->GetCurrentGossipGUID() != guid)
            return;

        Creature* creature = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        // A walking ladder opponent offers nothing: its gossip flags are cleared, this is the safety net.
        if (!creature || HsLadderIsWalker(creature->GetObjectGuid().GetRawValue()))
            return;
        if (HsHordeRogueHandleHello(player, creature, kSender))
            return;
        // A pack vendor / crafting NPC (hearthstone_vendor) gets its options too, with or without matches.
        bool const vendor = HsEconomyHasOptions(player, creature->GetEntry());
        // Byron's gossip tutorial while the quest "The Card Master's Trade" is open.
        bool const tutorial = HsIntroTutorialActive(player, creature);
        bool const classQuest = HsClassQuestGossipActive(player, creature);
        // A boss stand-in offers the class challenges whose quest is open (after the lesson, like a match).
        bool const challenge = g_matchEnabled.load(std::memory_order_relaxed) && HsLessonDone(player) &&
            HsNaxxChallengeGossipActive(player, creature);
        // A free arena run that ended completes the introduction quest here, before the quest menu is shown.
        bool questNow = false;
        if (HsArenaIsHost(creature->GetEntry()))
            questNow = HsArenaHostReconcile(player);
        bool const arena = questNow || HsArenaHostHasOptions(player, creature);
        // The Naxxramas breadcrumb quest at Ezra / Ibelin (no start relation, see HsNaxxBreadcrumb.h).
        bool const breadcrumb = HsNaxxBreadcrumbOffered(player, creature);
        // The ladder breadcrumb quest at Ezra / Ibelin (no start relation, see HsLadderBreadcrumb.h).
        bool const ladderCrumb = HsLadderBreadcrumbOffered(player, creature);
        // The Battlegrounds breadcrumb quest at Ezra / Ibelin (no start relation, see HsBgBreadcrumb.h).
        bool const bgCrumb = HsBgBreadcrumbOffered(player, creature);
        // A Warsong Battlegrounds host of the player's own faction offers the entry to the mode.
        bool const bgHost = HsBgIsHost(creature->GetEntry()) && HsBgHostServes(player, creature);
        bool opponent = g_matchEnabled.load(std::memory_order_relaxed) && IsOpponentEntry(creature->GetEntry());
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        // A quest NPC plays only for accounts that unlocked it (a character that turned in the quest
        // unlocks it right here). Faction rules are the core's: GetNPCIfCanInteractWith refuses hostile creatures.
        if (opponent && (!registry || !OpponentAvailable(player, *registry, creature->GetEntry(), true)))
            opponent = false;
        // The play option stays hidden until the lesson is complete (CheckStartPrerequisites refuses it as well).
        if (opponent && !HsLessonDone(player))
            opponent = false;
        if (!opponent && !vendor && !tutorial && !classQuest && !arena && !challenge && !breadcrumb && !ladderCrumb &&
            !bgCrumb && !bgHost)
            return;
        EnsureGossipFlag(creature);

        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        if (HasOurItem(menu) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            return;
        // A script opening a service window may leave both menus empty. Preserve
        // that window, except for Annie's unfinished mentors: non-Shamans have no
        // native training choice but still need the guidance conversation.
        uint32 const flags = creature->GetUInt32Value(UNIT_NPC_FLAGS);
        bool const plain = (flags & ~(UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER)) == 0;
        if (menu.Empty() && player->PlayerTalkClass->GetQuestMenu().Empty() && !plain &&
            !HsHordeShamanNeedsEmptyMentorGossip(player, creature))
            return;

        if (opponent)
        {
            bool const finder = registry->IsFinder(creature->GetEntry());
            char const* const play = creature->GetEntry() == kHsMaren || creature->GetEntry() == kHsAlenya ||
                creature->GetEntry() == kHsMitchellJones || HsHunterChainFor(creature->GetEntry())
                ? "I'm ready. Let's play." : "Play a game of Hearthstone";
            menu.AddMenuItem(GOSSIP_ICON_BATTLE, finder ? "Find me a Hearthstone opponent" : play,
                kSender, kActionPlay, "", false);
            // The core reads the parallel action data vector unchecked: keep it aligned.
            menu.AddGossipMenuItemData(0, 0, 0);
        }
        // The arena entry is the second option while it is a normal service (a run is open or paid entries are
        // unlocked); while it only leads into the introduction quest it sits at the bottom with the other quest starts.
        bool const arenaOptions = arena && HsArenaHostHasOptions(player, creature);
        bool const arenaQuestEntry = arenaOptions && HsArenaHostIsQuestEntry(player);
        if (arenaOptions && !arenaQuestEntry)
            HsArenaHostAppendGossip(player, creature, menu, kSender);
        if (vendor)
            HsEconomyAppendGossip(player, creature, menu, kSender);
        if (tutorial)
            HsIntroAppendGossip(player, creature, menu, kSender);
        if (classQuest)
            HsClassQuestAppendGossip(player, creature, menu, kSender);
        if (challenge)
            HsNaxxAppendChallengeGossip(player, creature, menu, kSender);
        // The class deck hint comes after the services and before the options that start quests (arena, Naxxramas).
        if (tutorial)
            HsIntroAppendClassHint(player, creature, menu, kSender);
        if (arenaQuestEntry)
            HsArenaHostAppendGossip(player, creature, menu, kSender);
        if (breadcrumb)
            HsNaxxBreadcrumbAppendGossip(player, creature, menu, kSender);
        if (ladderCrumb)
            HsLadderBreadcrumbAppendGossip(player, creature, menu, kSender);
        if (bgCrumb)
            HsBgBreadcrumbAppendGossip(player, creature, menu, kSender);
        // The Warsong Battlegrounds entry at one of the two hosts.
        if (bgHost)
            HsBgHostAppendGossip(player, creature, menu, kSender);
        if (questNow)
            player->PrepareQuestMenu(creature->GetObjectGuid());
        player->SendPreparedGossip(creature);
    }

    // CMSG_QUESTGIVER_CHOOSE_REWARD was handled: a turned-in unlock quest unlocks its NPC at once. The core
    // has no global quest reward hook; the login reconcile and the check at the NPC catch anything missed here.
    void HandleQuestTurnIn(WorldSession* session, WorldPacket const& packet)
    {
        if (!session || !g_matchEnabled.load(std::memory_order_relaxed) || packet.size() < sizeof(uint64) + sizeof(uint32))
            return;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            return;
        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid giver;
        uint32 quest = 0;
        copy >> giver >> quest;
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        if (!registry || !player->GetQuestRewardStatus(quest))
            return;
        for (Hs::OpponentRegistry::UnlockNpc const& npc : registry->UnlockNpcs())
            if (npc.questId == quest)
            {
                ReconcileUnlocks(player, *registry, { quest }, "reward");
                return;
            }
    }

    // CMSG_GOSSIP_SELECT_OPTION: an item with the module's sender is always consumed here (false), the
    // creature's own script and the core never see it. Everything else passes (true).
    bool HandleGossipSelect(WorldSession* session, WorldPacket const& packet)
    {
        if (!session || packet.size() < sizeof(uint64) + sizeof(uint32))
            return true;
        Player* player = session->GetPlayer();
        // The hook runs before the core checks the player: leave a select out of the world to the core.
        if (!player || !player->IsInWorld() || !player->PlayerTalkClass)
            return true;

        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid guid;
        uint32 index = 0;
        copy >> guid >> index;

        PlayerMenu* talk = player->PlayerTalkClass;
        if (index >= talk->GetGossipMenu().MenuItemCount() || talk->GossipOptionSender(index) != kSender)
            return true;
        uint32 const action = talk->GossipOptionAction(index);

        // From here on the select is ours.
        if (!guid.IsAnyTypeCreature() || session->GetCurrentGossipGUID() != guid)
        {
            talk->CloseGossip();
            return false;
        }
        Creature* creature = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (creature && HsLadderIsWalker(creature->GetObjectGuid().GetRawValue()))
            creature = nullptr; // a walking ladder opponent has no options: the select closes the window
        // The pack purchases and the crafting access of a vendor NPC.
        if (creature && HsEconomyHandleSelect(player, creature, action))
            return false;
        if (creature && HsCardTraderHandleSelect(player, creature, action))
            return false;
        // Byron's gossip tutorial.
        if (creature && HsIntroHandleSelect(player, creature, action, kSender))
            return false;
        if (creature && HsClassQuestHandleSelect(player, creature, action, kSender))
            return false;
        // The Draft Arena at Ezra / Ibelin.
        if (creature && HsArenaHostHandleSelect(player, creature, action, kSender))
            return false;
        // The Naxxramas breadcrumb quest option at Ezra / Ibelin.
        if (HsNaxxBreadcrumbHandleSelect(player, creature, action))
            return false;
        // The ladder breadcrumb quest option at Ezra / Ibelin.
        if (HsLadderBreadcrumbHandleSelect(player, creature, action))
            return false;
        // The Battlegrounds breadcrumb quest option at Ezra / Ibelin.
        if (HsBgBreadcrumbHandleSelect(player, creature, action))
            return false;
        // The Warsong Battlegrounds options at one of the two hosts.
        if (creature && HsBgHostHandleSelect(player, creature, action, kSender))
            return false;
        // A class challenge of a boss stand-in (loaner deck, no deck menu). StartMatch checks everything again.
        if (creature && HsNaxxIsChallengeAction(action))
        {
            Hs::Challenge const* const challenge = g_matchEnabled.load(std::memory_order_relaxed)
                ? HsNaxxChallengeOfAction(player, creature, action) : nullptr;
            if (challenge)
                StartMatch(player, creature, Hs::Difficulty::Boss, 0, challenge);
            else
                talk->CloseGossip();
            return false;
        }
        std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
        if (!creature || !g_matchEnabled.load(std::memory_order_relaxed) || !registry || !registry->Has(creature->GetEntry()) ||
            !OpponentAvailable(player, *registry, creature->GetEntry(), false))
        {
            talk->CloseGossip();
            return false;
        }
        std::vector<Hs::Difficulty> const offered = DirectDifficulties(*registry, creature->GetEntry());
        auto isOffered = [&offered](Hs::Difficulty d) { return std::find(offered.begin(), offered.end(), d) != offered.end(); };

        if (action == kActionPlay)
        {
            // Normal / Hard only when the opponent offers both; a single difficulty (and every boss) goes
            // straight to the deck menu.
            if (offered.empty())
                Refuse(player, creature, "This opponent has no deck to play with.");
            else if (offered.size() == 1)
                ShowDeckMenu(player, creature, offered[0]);
            else
                ShowDifficultyMenu(player, creature, offered);
        }
        else if ((action & kActionDeckFlag) != 0)
        {
            uint32 const value = (action >> kDeckDifficultyShift) & kDeckDifficultyMask;
            if (value > static_cast<uint32>(Hs::Difficulty::Boss))
                talk->CloseGossip();
            else
                StartMatch(player, creature, static_cast<Hs::Difficulty>(value), action & kMaxDeckId);
        }
        else if (action >= kActionDeckPageBase && action < kActionDeckPageBase + 3 * kDeckPageStride)
        {
            uint32 const offset = action - kActionDeckPageBase;
            Hs::Difficulty const difficulty = static_cast<Hs::Difficulty>(offset / kDeckPageStride);
            if (offset % kDeckPageStride < kMaxMenuDecks / kDecksPerPage && isOffered(difficulty))
                ShowDeckMenu(player, creature, difficulty, offset % kDeckPageStride);
            else
                talk->CloseGossip();
        }
        else if (action == kActionDifficultyBase + static_cast<uint32>(Hs::Difficulty::Normal) ||
                 action == kActionDifficultyBase + static_cast<uint32>(Hs::Difficulty::Hard))
        {
            Hs::Difficulty const difficulty = static_cast<Hs::Difficulty>(action - kActionDifficultyBase);
            if (isOffered(difficulty))
                ShowDeckMenu(player, creature, difficulty);
            else
                talk->CloseGossip();
        }
        else
            talk->CloseGossip(); // kActionNever or anything unknown
        return false;
    }

    // ---- the action block ------------------------------------------------------------

    // World gameplay actions that are dropped while a match runs. Not listed on purpose: logout, chat,
    // movement acknowledgements, addon traffic (they pass), CMSG_CANCEL_AURA (the core refuses it for
    // a SPELL_ATTR_CANT_CANCEL aura), CMSG_CANCEL_CAST, gossip and quest dialogues.
    bool IsBlockedOpcode(uint32 opcode)
    {
        switch (opcode)
        {
            case CMSG_CAST_SPELL:           // handled first: spell 7355 (Stuck) ends the match
            case CMSG_PET_CAST_SPELL:
            case CMSG_USE_ITEM:
            case CMSG_OPEN_ITEM:
            case CMSG_ATTACKSWING:
            case CMSG_GAMEOBJ_USE:
            case CMSG_ACTIVATETAXI:
            case CMSG_ACTIVATETAXIEXPRESS:
            case CMSG_INITIATE_TRADE:
            case CMSG_PET_ACTION:
            case CMSG_LOOT:
            case CMSG_LOOT_MONEY:
            case CMSG_AUTOSTORE_LOOT_ITEM:
                return true;
            default:
                return false;
        }
    }

    // CMSG_CAST_SPELL for the Stuck spell of a character with a running match (or the leftover
    // effect): end the match as interrupted, remove the effect, drop the packet (no cast bar, no
    // cooldown, no teleport). Returns true when the packet was consumed.
    bool HandleStuck(Player* player)
    {
        uint32 const guid = player->GetGUIDLow();
        bool const interrupted = InterruptMatch(player, guid, Hs::Interrupt::Stuck);
        bool orphan = false;
        uint32 const configured = sHearthstoneConfig.Get().matchAuraSpellId;
        if (!interrupted && configured != 0 && player->HasAura(configured))
        {
            // The effect without a running match (a leftover): Stuck frees the character too.
            player->RemoveAurasDueToSpell(configured);
            Binding unused;
            Unbind(guid, unused);
            orphan = true;
        }
        if (!interrupted && !orphan)
            return false;
        if (SpellEntry const* stuck = sSpellMgr.GetSpellEntry(kStuckSpellId))
            Spell::SendCastResult(player, stuck, SPELL_FAILED_DONT_REPORT);
        sLog.outString("Hearthstone: Help > Stuck of %s (guid %u) %s", player->GetName(), guid,
            interrupted ? "ended the match" : "removed a leftover In Card Match effect");
        return true;
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet)
    {
        uint32 const opcode = packet.GetOpcode();
        if (!HsHordeShamanCanInteractPacket(session ? session->GetPlayer() : nullptr, packet))
            return false;
        if (!HsAlliancePriestCanInteractPacket(session ? session->GetPlayer() : nullptr, packet))
            return false;
        if (!HsAllianceShamanCanInteractPacket(session ? session->GetPlayer() : nullptr, packet))
            return false;
        if (!HsHordePaladinCanInteractPacket(session ? session->GetPlayer() : nullptr, packet))
            return false;
        if (!HsHordeRogueCanVendorPacket(session ? session->GetPlayer() : nullptr, packet))
            return false;
        if (opcode == CMSG_GOSSIP_HELLO || opcode == CMSG_QUESTGIVER_COMPLETE_QUEST ||
            opcode == CMSG_QUESTGIVER_REQUEST_REWARD || opcode == CMSG_QUESTGIVER_CHOOSE_REWARD)
        {
            Player* questPlayer = session ? session->GetPlayer() : nullptr;
            if (questPlayer && questPlayer->IsInWorld())
                HsHordeDruidReconcileFeed(questPlayer);
        }
        if (opcode == CMSG_GOSSIP_SELECT_OPTION)
            return HandleGossipSelect(session, packet);
        // The arena introduction quests have no start relation; the module takes their accept packet.
        if (opcode == CMSG_QUESTGIVER_ACCEPT_QUEST && HsArenaHostHandleAccept(session, packet))
            return false;
        // The Naxxramas breadcrumb quest has no start relation either.
        if (opcode == CMSG_QUESTGIVER_ACCEPT_QUEST && HsNaxxBreadcrumbHandleAccept(session ? session->GetPlayer() : nullptr, packet))
            return false;
        // ... and so does the ladder breadcrumb quest.
        if (opcode == CMSG_QUESTGIVER_ACCEPT_QUEST && HsLadderBreadcrumbHandleAccept(session ? session->GetPlayer() : nullptr, packet))
            return false;
        // ... and the Battlegrounds one.
        if (opcode == CMSG_QUESTGIVER_ACCEPT_QUEST && HsBgBreadcrumbHandleAccept(session ? session->GetPlayer() : nullptr, packet))
            return false;
        // The booster pack listing for Hearthstone Coins (the vendor window of a pack vendor).
        if (opcode == CMSG_BUY_ITEM)
        {
            Player* buyer = session ? session->GetPlayer() : nullptr;
            return !(buyer && buyer->IsInWorld() && HsEconomyHandleBuy(buyer, packet));
        }
        // The introduction quests note whether the quest is still unrewarded (HsIntroGlue.h).
        if (opcode == CMSG_QUESTGIVER_CHOOSE_REWARD)
        {
            HsIntroBeforeTurnIn(session, packet);
            HsHordeShamanBeforeTurnIn(session ? session->GetPlayer() : nullptr, packet);
            HsAlliancePriestBeforeTurnIn(session ? session->GetPlayer() : nullptr, packet);
        }
        if (!IsBlockedOpcode(opcode))
            return true;

        Player* player = session ? session->GetPlayer() : nullptr;
        // Before the core's own status check: a packet of a player that is not in the world is the core's.
        if (!player || !player->IsInWorld())
            return true;

        // Help > Stuck: the instant intercept (also without a running match when the effect is left over).
        if (opcode == CMSG_CAST_SPELL)
        {
            if (packet.size() >= sizeof(uint32) && packet.read<uint32>(0) == kStuckSpellId)
                return !HandleStuck(player);
        }

        if (HsMatches().RunningCount() == 0 || !HsMatches().IsRunning(player->GetGUIDLow()))
        {
            // Right-clicking a booster pack item adds it to the account's packs, a deck item gives its deck.
            if (opcode == CMSG_OPEN_ITEM)
                return !(HsIntroHandleOpenItem(player, packet) || HsEconomyHandleOpenItem(player, packet) ||
                    HsNaxxHandleOpenItem(player, packet));
            return true;
        }
        return false;
    }
}

// ---- HsMatchSession ---------------------------------------------------------------

std::string HsMatchSession::GameGet()
{
    return HsMatches().Get(m_guid, m_now); // the clock for the tmr= countdown
}

Hs::GameReply HsMatchSession::GameAct(Hs::GameActRequest const& request)
{
    Hs::GameActOutcome const outcome = HsMatches().Act(m_guid, request, m_now);
    Hs::GameReply reply;
    // An arena duel's new log entries are stored before the reply goes out. When the store fails nothing of
    // the action is shown: the duel is suspended with the entries stored so far (the cleanup is HsMatchFinishFrame).
    if (outcome.ok && !StoreArenaActions(m_guid, outcome.arenaRunId, outcome.loggedFirst, outcome.logged))
    {
        if (HsMatches().Suspend(m_guid, m_duel, m_now, outcome.loggedFirst))
            m_suspended = true;
        reply.why = Hs::kWhyMatch;
        return reply;
    }
    reply.ok = outcome.ok;
    reply.why = outcome.why;
    reply.payload = outcome.payload;
    if (outcome.ended)
    {
        m_ended = true;
        m_end = outcome.end;
    }
    return reply;
}

Hs::GameReply HsMatchSession::GamePause(uint32_t matchId)
{
    Hs::GameReply reply;
    Hs::MatchInfo info;
    if (!HsMatches().Info(m_guid, info))
    {
        // No match any more: the duel is paused already (a retry after a lost answer).
        reply.ok = true;
        reply.payload = Hs::EncodeGameNone();
        return reply;
    }
    if (info.matchId != matchId)
        reply.why = Hs::kWhyMatch;
    else if (!info.running)
        reply.why = Hs::kWhyOver;
    else if (!HsMatches().IsArena(m_guid))
        reply.why = "ordinary";
    else if (!HsMatches().Suspend(m_guid, m_duel, m_now))
        reply.why = Hs::kWhyMatch;
    else
    {
        m_suspended = true;
        m_suspendWhy = "pause";
        reply.ok = true;
        reply.payload = Hs::EncodeGameNone();
    }
    return reply;
}

Hs::GameReply HsMatchSession::GameLeave(uint32_t matchId)
{
    // Closing a running arena duel is a pause, never a surrender (an ordinary match refuses with "active").
    {
        Hs::MatchInfo info;
        if (HsMatches().Info(m_guid, info) && info.running && info.matchId == matchId && HsMatches().IsArena(m_guid))
            return GamePause(matchId);
    }
    Hs::GameLeaveOutcome const outcome = HsMatches().Leave(m_guid, matchId);
    Hs::GameReply reply;
    reply.ok = outcome.ok;
    reply.why = outcome.why;
    reply.payload = outcome.payload;
    return reply;
}

void HsMatchFinishFrame(Player* player, HsMatchSession const& session)
{
    if (player && session.Suspended())
    {
        FinishSuspended(player, player->GetGUIDLow(), session.Duel(), session.SuspendWhy(), true);
        return;
    }
    if (!player || !session.Ended())
        return;
    ReleaseProtection(player, player->GetGUIDLow());
    LogEnd(session.End());
}

// ---- world hooks ------------------------------------------------------------------

std::vector<Hs::OpponentRegistry::UnlockNpc> HsQuestNpcs()
{
    std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
    return registry ? registry->UnlockNpcs() : std::vector<Hs::OpponentRegistry::UnlockNpc>();
}

bool HsStarterRecipe(std::string const& deckClass, Hs::DeckCards& cards, std::string& name)
{
    std::shared_ptr<Hs::DeckLibrary const> const library = Library();
    Hs::LibraryDeck const* deck = library ? library->Find(Hs::StarterDeckKey(deckClass)) : nullptr;
    if (!deck || !deck->ready || deck->kind != Hs::kDeckKindStarter || deck->deckClass != deckClass)
        return false;
    cards = deck->cards;
    name = deck->name;
    return true;
}

void HsSetArenaDuelSink(HsArenaDuelSink* sink)
{
    g_arenaSink.store(sink, std::memory_order_release);
}

bool HsMatchAttachArena(Player* player, Creature* host)
{
    Binding binding;
    if (!ApplyProtection(player, binding.aura))
    {
        player->RemoveAurasDueToSpell(sHearthstoneConfig.Get().matchAuraSpellId);
        return false;
    }
    binding.creature = host->GetObjectGuid().GetRawValue();
    binding.entry = host->GetEntry();
    binding.mapId = player->GetMapId();
    Bind(player->GetGUIDLow(), binding);
    return true;
}

void HsMatchDetachArena(Player* player, uint32_t guid)
{
    ReleaseProtection(player, guid);
}

void HsMatchPushGame(Player* player, uint32_t guid, std::string const& payload)
{
    SendGamePush(player, guid, payload);
}

namespace
{
    // A ladder duel needs an account without a paused arena duel (one card duel per account).
    std::string LadderStartBlock(Player* player)
    {
        if (HsArenaHasDuel(AccountOf(player)))
            return "You have a paused arena duel to finish first. Talk to a Hearthstone Master to resume it.";
        return std::string();
    }
}

std::string HsMatchStartRefusal(Player* player)
{
    std::string why;
    if (!CheckStartPrerequisites(player, why))
        return why;
    return LadderStartBlock(player);
}

std::string HsMatchResumeRefusal(Player* player)
{
    std::string why;
    return CheckStartPrerequisites(player, why, true) ? std::string() : why;
}

bool HsMatchReadyDecks(Player* player, std::vector<std::pair<uint32_t, std::string>>& out, std::string& why)
{
    out.clear();
    return ReadyDecks(player, out, why);
}

std::string HsMatchStartLadder(Player* player, Creature* creature, uint32_t deckId, HsLadderMatchSetup const& setup,
                               uint32_t& matchId)
{
    matchId = 0;
    if (!player || !creature)
        return "The opponent is not here.";
    std::string why;
    if (!CheckStartPrerequisites(player, why))
        return why;
    why = LadderStartBlock(player);
    if (!why.empty())
        return why;
    if (!HsLadderDuels().Ready())
        return "Ranked ladder duels are not available right now (see the server log).";
    if (setup.opponentDeck.size() != Hs::kDeckSize || setup.opponentClass.empty())
        return "This opponent has no deck to play with.";
    uint32 const guid = player->GetGUIDLow();
    uint32 const accountId = AccountOf(player);

    // The player's deck, checked as for every match start.
    std::string const startClass = Hs::DeckClassOfWowClass(player->getClass());
    Hs::DeckMatchCheck const check = HsDecks().CheckForMatch(accountId, deckId, startClass);
    if (check.status == Hs::DeckMatchCheck::Status::Missing)
        return "That deck does not exist any more.";
    if (check.status == Hs::DeckMatchCheck::Status::Unavailable)
        return "Your decks are not available right now. Try again later.";
    if (!check.Ready())
        return "That deck is not ready for a match (" + check.Text() + ").";
    Hs::DeckBook book;
    auto deck = book.decks.end();
    std::vector<std::string> humanDeck;
    if (HsDecks().GetBook(accountId, book))
        deck = book.decks.find(deckId);
    if (deck == book.decks.end() || !Hs::ExpandDeck(deck->second.cards, humanDeck) || humanDeck.size() != Hs::kDeckSize)
        return "That deck could not be read. Try again later.";

    Hs::Difficulty const difficulty = setup.profile == Hs::Match::AiProfile::Hard ? Hs::Difficulty::Hard : Hs::Difficulty::Normal;
    Hs::MatchStartParams params;
    params.guid = guid;
    params.humanName = player->GetName();
    params.humanClass = deck->second.deckClass;
    params.humanDeck = std::move(humanDeck);
    params.opponentName = setup.opponentName.empty() ? std::string(creature->GetName()) : setup.opponentName;
    params.opponentClass = setup.opponentClass;
    params.opponentDeck = setup.opponentDeck;
    params.seed = std::random_device()();
    params.difficulty = Hs::DifficultyCode(difficulty);
    params.profile = setup.profile;
    params.meta.accountId = accountId;
    params.meta.opponentEntry = creature->GetEntry();
    params.meta.deckId = deckId;
    params.meta.deckName = deck->second.name;
    params.meta.opponentDeckKey = setup.opponentDeckKey;
    params.meta.startedAt = static_cast<int64_t>(std::time(nullptr));

    // A ladder duel is a durable match: the duel is stored first (one per account), every action before the
    // player sees it, and an interruption pauses it instead of ending it.
    Hs::Ladder::DuelRow duel = Hs::Ladder::DuelFromParams(params, 0, setup.npcEntry,
        static_cast<uint32>(Hs::Match::kRulesVersion), Hs::kDatasetHash);
    if (!HsLadderDuels().Begin(duel))
        return "Your ladder duel could not be saved (see the server log). Try again later.";
    params.arena = true;
    params.arenaRunId = Hs::Ladder::RunOfDuel(duel.id);
    params.ladder = true;

    Hs::MatchStartResult const started = HsMatches().Start(params, HsNowMs());
    if (!started.ok)
    {
        HsLadderDuels().Abandon(duel.id);
        if (started.error == "running")
            return "You are already in a match.";
        sLog.outError("Hearthstone: could not create a ladder match for %s (guid %u): %s", player->GetName(), guid,
            started.error.c_str());
        return "The match could not be started (" + started.error + ").";
    }
    // The AI's opening mulligan is the first entry of the log: stored before the first payload goes out.
    if (!HsLadderDuels().StoreActions(guid, params.arenaRunId, 0, started.logged))
    {
        HsMatches().Drop(guid);
        HsLadderDuels().Abandon(duel.id);
        return "Your ladder duel could not be saved (see the server log). Try again later.";
    }
    // As in StartMatch: the effect comes after the match exists, and a failure undoes the match.
    Binding binding;
    if (!ApplyProtection(player, binding.aura))
    {
        HsMatches().Drop(guid);
        HsLadderDuels().Abandon(duel.id);
        player->RemoveAurasDueToSpell(sHearthstoneConfig.Get().matchAuraSpellId);
        sLog.outError("Hearthstone: the In Card Match effect (spell %u) could not be applied to %s (guid %u); the ladder "
            "match #%u was dropped", sHearthstoneConfig.Get().matchAuraSpellId, player->GetName(), guid, started.matchId);
        return "The match could not be started (the In Card Match effect failed).";
    }
    binding.creature = creature->GetObjectGuid().GetRawValue();
    binding.entry = creature->GetEntry();
    binding.mapId = player->GetMapId();
    Bind(guid, binding);

    matchId = started.matchId;
    HsWeeklyMatchStarted(player, matchId, false, params.arenaRunId);
    SendGamePush(player, guid, started.payload);
    sLog.outString("Hearthstone: ladder match #%u started: %s (guid %u, deck #%u %s) against %s (creature %u, %s, deck %s %s), "
        "seed %u, effect %s", started.matchId, player->GetName(), guid, deckId, params.humanClass.c_str(),
        params.opponentName.c_str(), creature->GetEntry(), params.difficulty.c_str(), setup.opponentDeckKey.c_str(),
        params.opponentClass.c_str(), params.seed, binding.aura ? std::to_string(binding.aura).c_str() : "none");
    return std::string();
}

std::string HsMatchResumeLadder(Player* player, Creature* creature, uint64_t duelId, uint32_t& matchId, std::string& endedRes)
{
    matchId = 0;
    endedRes.clear();
    if (!player || !creature)
        return "The opponent is not here.";
    std::string why;
    if (!CheckStartPrerequisites(player, why, true))
        return why;
    uint32 const guid = player->GetGUIDLow();
    uint32 const accountId = AccountOf(player);

    bool found = false;
    Hs::Ladder::DuelRow row;
    if (!HsLadderDuels().FindById(duelId, found, row))
        return "Your ladder duel could not be read (see the server log). Try again later.";
    if (!found || row.account != accountId)
        return "That duel is not yours any more.";
    // A duel the data cannot rebuild is kept for an administrator, never discarded and never scored.
    if (!Hs::Ladder::DuelReplayable(row, static_cast<uint32>(Hs::Match::kRulesVersion), Hs::kDatasetHash))
    {
        sLog.outError("Hearthstone: the ladder duel %llu of account %u cannot be resumed: it was recorded with another rules "
            "version or card dataset", static_cast<unsigned long long>(duelId), accountId);
        return "This duel was recorded with another version of the card game and cannot be resumed. A game master can "
               "remove it.";
    }
    std::vector<std::string> rows;
    std::vector<Hs::LoggedAction> log;
    std::size_t failedAt = 0;
    std::string error;
    if (!HsLadderDuels().LoadLog(duelId, rows))
        return "Your ladder duel could not be read (see the server log). Try again later.";
    if (!Hs::Ladder::DecodeDuelLog(rows, log, failedAt, error))
    {
        sLog.outError("Hearthstone: the ladder duel %llu of account %u cannot be resumed: log row %u does not decode (%s)",
            static_cast<unsigned long long>(duelId), accountId, static_cast<uint32>(failedAt), error.c_str());
        return "This duel's log is damaged and cannot be resumed. A game master can remove it.";
    }

    Hs::SuspendedDuel const duel = Hs::Ladder::SuspendedFromDuel(row, guid, player->GetName(), log);
    Hs::MatchRestoreResult const restored = HsMatches().Restore(duel, HsNowMs());
    if (!restored.ok)
    {
        if (restored.error == "running")
            return "You are already in a match.";
        sLog.outError("Hearthstone: could not restore the ladder duel %llu for %s (guid %u): %s",
            static_cast<unsigned long long>(duelId), player->GetName(), guid, restored.error.c_str());
        return "The duel could not be resumed (" + restored.error + "). A game master can remove it.";
    }
    matchId = restored.matchId;
    HsWeeklyMatchStarted(player, matchId, false, duel.params.arenaRunId, 0, false, true);
    if (restored.ended)
        HsWeeklyMatchEnded(restored.end);

    // The log may replay to the end of the duel: the server stopped after the last action was stored and before the
    // result was applied. There is nothing to protect: the caller applies the result and the final board is shown.
    Hs::MatchInfo info;
    if (HsMatches().Info(guid, info) && !info.running)
    {
        endedRes = info.res;
        SendGamePush(player, guid, restored.payload);
        sLog.outString("Hearthstone: ladder duel %llu of %s had ended before the server stopped: %s",
            static_cast<unsigned long long>(duelId), player->GetName(), info.res.c_str());
        return std::string();
    }

    Binding binding;
    if (!ApplyProtection(player, binding.aura))
    {
        HsMatches().Drop(guid);
        player->RemoveAurasDueToSpell(sHearthstoneConfig.Get().matchAuraSpellId);
        sLog.outError("Hearthstone: the In Card Match effect could not be applied to %s (guid %u); the ladder duel %llu stays "
            "paused", player->GetName(), guid, static_cast<unsigned long long>(duelId));
        matchId = 0;
        return "The duel could not be resumed (the In Card Match effect failed).";
    }
    binding.creature = creature->GetObjectGuid().GetRawValue();
    binding.entry = creature->GetEntry();
    binding.mapId = player->GetMapId();
    Bind(guid, binding);
    if (!HsLadderDuels().MarkActive(duelId, guid))
        sLog.outError("Hearthstone: ladder duel %llu was resumed but could not be marked active", static_cast<unsigned long long>(duelId));
    SendGamePush(player, guid, restored.payload);
    sLog.outString("Hearthstone: ladder duel %llu resumed by %s (guid %u) after %u action(s) against %s (creature %u)",
        static_cast<unsigned long long>(duelId), player->GetName(), guid, static_cast<uint32>(log.size()),
        row.opponentName.c_str(), creature->GetEntry());
    return std::string();
}

std::string HsFinderOpponentName(bool alliance)
{
    std::shared_ptr<std::vector<Hs::FinderName> const> const names = FinderNames();
    std::mt19937 rng{ std::random_device()() };
    return names ? Hs::PickFinderName(*names, alliance, rng) : std::string();
}

std::shared_ptr<Hs::DeckLibrary const> HsMatchLibrary()
{
    return Library();
}

void HsMatchStartup()
{
    Hs::MatchSettings const settings = sHearthstoneConfig.GetMatch();
    g_matchEnabled.store(settings.enable, std::memory_order_relaxed);
    HsMatches().SetAiDelayMs(settings.aiDelayMs);

    std::shared_ptr<Hs::Match::CardDb const> const db = LoadHsMatchCardDb();
    if (db)
    {
        HsMatches().SetCardDb(db);
        // The deck service reports a deck as match-ready only when every card is supported.
        HsDecks().SetEngineSupport([db](std::string const& cardId) { return db->Supported(cardId); });
        // The numbers of .hs status (LoadHsMatchCardDb logged the same).
        g_engineCards.store(static_cast<uint32>(db->Size()), std::memory_order_relaxed);
        g_engineSupported.store(static_cast<uint32>(db->SupportedCount()), std::memory_order_relaxed);
    }
    CheckHsNpcUnlockTable();
    // After the card data: the library decks and the opponent rows are checked against the engine's CardDb.
    LoadOpponents(settings);
    if (!settings.enable)
        sLog.outString("Hearthstone: card matches are disabled (Hearthstone.Match.Enable = 0)");
    else
    {
        LogAuraState();
        if (OpponentCount() == 0)
            sLog.outString("Hearthstone: no opponent creature (no usable hearthstone_opponent row and "
                "Hearthstone.Match.Opponents is empty): nobody can start a match");
        MarkLoadedOpponents();
    }
}

void HsMatchReloadConfig()
{
    Hs::MatchSettings const settings = sHearthstoneConfig.GetMatch();
    bool const wasEnabled = g_matchEnabled.exchange(settings.enable, std::memory_order_relaxed);
    LoadOpponents(settings);
    HsMatches().SetAiDelayMs(settings.aiDelayMs);
    if (!settings.enable)
    {
        if (wasEnabled)
        {
            // Running matches end now (reason gm); the opponent registry is already empty.
            for (Hs::MatchInfo const& info : HsMatches().List())
                if (info.running)
                    InterruptMatch(nullptr, info.guid, Hs::Interrupt::Gm);
        }
        return;
    }
    LogAuraState();
    MarkLoadedOpponents();
}

void HsMatchUpdate()
{
    RunDeferredEnds();
    if (HsMatches().RunningCount() == 0)
        return;
    std::vector<Hs::MatchPush> const pushes = HsMatches().Tick(HsNowMs());
    for (Hs::MatchPush const& push : pushes)
    {
        if (!push.note.empty())
            sLog.outError("Hearthstone: match of guid %u: %s", push.guid, push.note.c_str());
        // An arena duel's new log entries are stored before the push; a failed store suspends the duel
        // (with the entries stored so far) and shows nothing of the action.
        if (!StoreArenaActions(push.guid, push.arenaRunId, push.loggedFirst, push.logged))
        {
            Hs::SuspendedDuel duel;
            if (HsMatches().Suspend(push.guid, duel, HsNowMs(), push.loggedFirst))
                FinishSuspended(nullptr, push.guid, duel, "store", true);
            continue;
        }
        if (push.ended)
        {
            ReleaseProtection(nullptr, push.guid);
            LogEnd(push.end);
        }
        SendGamePush(nullptr, push.guid, push.payload);
    }
}

// ---- GM commands ---------------------------------------------------------------------

void HsMatchUsage(ChatHandler* handler)
{
    handler->SendSysMessage("Usage: .hs match show [player]");
    handler->SendSysMessage("       .hs match end <player>");
    handler->SendSysMessage("       .hs match decks");
    handler->SendSysMessage("       .hs match history <player|#accountId> [count=10, at most 50]");
}

namespace
{
    std::string Describe(Hs::MatchInfo const& info)
    {
        std::string text = "  #" + std::to_string(info.matchId) + " " + info.humanName + " (guid " + std::to_string(info.guid) +
            ") vs " + info.opponentName + " [" + info.difficulty + "]: ";
        if (info.running)
            text += "running";
        else
            text += "finished, " + info.res + " (" + info.why + ")";
        text += ", turn " + std::to_string(info.turn) + ", health " + std::to_string(info.humanHealth) + " vs " +
            std::to_string(info.opponentHealth) + ", seq " + std::to_string(info.seq) + ", seed " + std::to_string(info.seed);
        return text;
    }

    constexpr uint32 kHistoryDefaultCount = 10;
    constexpr uint32 kHistoryMaxCount = 50;

    // A decimal number of 1..9 digits.
    bool ParseCount(std::string const& s, uint32& out)
    {
        if (s.empty() || s.size() > 9)
            return false;
        uint32 v = 0;
        for (char c : s)
        {
            if (c < '0' || c > '9')
                return false;
            v = v * 10 + static_cast<uint32>(c - '0');
        }
        out = v;
        return true;
    }

    // `.hs match history <player|#accountId> [count]`: the newest recorded matches of a character (by name,
    // online or offline) or of every character of an account (#id), newest first.
    void ShowHistory(ChatHandler* handler, std::vector<std::string> const& args)
    {
        uint32 count = kHistoryDefaultCount;
        if (args.size() == 4 && (!ParseCount(args[3], count) || count == 0))
        {
            handler->SendSysMessage("Hearthstone: the count must be a number from 1 to " +
                std::to_string(kHistoryMaxCount) + ".");
            return;
        }
        count = std::min(count, kHistoryMaxCount);

        std::string const& target = args[2];
        uint32 accountId = 0;
        uint32 guid = 0;
        std::string label;
        if (!target.empty() && target[0] == '#')
        {
            std::string accountName;
            if (!ParseCount(target.substr(1), accountId) || accountId == 0 || !sAccountMgr.GetName(accountId, accountName))
            {
                handler->SendSysMessage("Hearthstone: '" + target + "' is not an existing account (#<account id>).");
                return;
            }
            label = "account #" + std::to_string(accountId) + " (" + accountName + ")";
        }
        else
        {
            std::string name = target;
            if (!normalizePlayerName(name))
            {
                handler->SendSysMessage("Hearthstone: '" + target + "' is not a valid character name.");
                return;
            }
            std::string escaped = name;
            CharacterDatabase.escape_string(escaped);
            std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
                "SELECT `guid`, `account`, `name` FROM `characters` WHERE `name` = '%s'", escaped.c_str()));
            if (!row)
            {
                handler->SendSysMessage("Hearthstone: no character named " + name + ".");
                return;
            }
            Field* f = row->Fetch();
            guid = f[0].GetUInt32();
            accountId = f[1].GetUInt32();
            label = f[2].GetCppString() + " (guid " + std::to_string(guid) + ", account #" + std::to_string(accountId) + ")";
        }

        std::vector<Hs::MatchHistoryListing> rows;
        if (!LoadHsMatchHistory(accountId, guid, count, rows))
        {
            handler->SendSysMessage("Hearthstone: the match history is not available (see the server log; "
                "migration 0004_hearthstone_match_history.sql applied?).");
            return;
        }
        if (rows.empty())
        {
            handler->SendSysMessage("Hearthstone: no recorded match for " + label + (sHearthstoneConfig.GetMatch().historyEnable
                ? std::string(".") : std::string(" (recording is off: Hearthstone.Match.History.Enable = 0).")));
            return;
        }
        handler->SendSysMessage("Hearthstone: the last " + std::to_string(rows.size()) + " recorded match(es) of " + label + ":");
        for (Hs::MatchHistoryListing const& entry : rows)
            handler->SendSysMessage(Hs::FormatMatchHistoryLine(entry));
    }

    std::string GmLabel(ChatHandler* handler)
    {
        WorldSession* session = handler->GetSession();
        if (!session)
            return "console";
        char const* name = session->GetPlayerName();
        return std::string(name ? name : "?") + " (account #" + std::to_string(session->GetAccountId()) + ")";
    }
}

void HsMatchCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
{
    std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
    if ((sub != "show" && sub != "end" && sub != "decks" && sub != "history") || (sub == "end" && args.size() != 3) ||
        (sub == "show" && args.size() > 3) || (sub == "decks" && args.size() != 2) ||
        (sub == "history" && (args.size() < 3 || args.size() > 4)))
    {
        HsMatchUsage(handler);
        return;
    }

    if (sub == "history")
    {
        ShowHistory(handler, args);
        return;
    }

    if (sub == "decks")
    {
        // Every library deck (hearthstone_deck_template) with its class and whether it is ready, or its first problem.
        std::shared_ptr<Hs::DeckLibrary const> const library = Library();
        if (!library || library->Decks().empty())
        {
            handler->SendSysMessage("Hearthstone: the opponent deck library is empty (table hearthstone_deck_template "
                "missing or without rows, or the card data is not loaded: see the server log).");
            return;
        }
        handler->SendSysMessage("Hearthstone: opponent deck library: " + std::to_string(library->ReadyCount()) + " of " +
            std::to_string(library->Decks().size()) + " decks ready (a deck is ready when every card exists and the match "
            "engine supports it).");
        for (Hs::LibraryDeck const& deck : library->Decks())
            handler->SendSysMessage("  " + deck.key + " (" + deck.deckClass + ", " + deck.name + "): " +
                (deck.ready ? std::string("ready") : "NOT READY: " + deck.problem));
        return;
    }

    if (sub == "show")
    {
        if (args.size() == 3)
        {
            std::string name = args[2];
            if (!normalizePlayerName(name))
            {
                handler->SendSysMessage("Hearthstone: '" + args[2] + "' is not a valid character name.");
                return;
            }
            uint32 guid = sObjectMgr.GetPlayerGuidByName(name).GetCounter();
            if (guid == 0)
            {
                handler->SendSysMessage("Hearthstone: no character named " + name + ".");
                return;
            }
            Hs::MatchInfo info;
            if (!HsMatches().Info(guid, info))
                handler->SendSysMessage("Hearthstone: " + name + " has no running or finished match.");
            else
                handler->SendSysMessage("Hearthstone: match of " + name + ":\n" + Describe(info));
            return;
        }
        std::vector<Hs::MatchInfo> const infos = HsMatches().List();
        handler->SendSysMessage("Hearthstone: " + std::to_string(HsMatches().RunningCount()) + " running, " +
            std::to_string(infos.size() - std::min(infos.size(), HsMatches().RunningCount())) +
            " finished matches (kept until the player leaves them or logs out).");
        for (Hs::MatchInfo const& info : infos)
            handler->SendSysMessage(Describe(info));
        return;
    }

    // end
    if (security < static_cast<int>(SEC_DEVELOPER))
    {
        handler->SendSysMessage("Hearthstone: .hs match end needs security level 3 (developer) or higher.");
        return;
    }
    std::string name = args[2];
    if (!normalizePlayerName(name))
    {
        handler->SendSysMessage("Hearthstone: '" + args[2] + "' is not a valid character name.");
        return;
    }
    Player* player = ObjectAccessor::FindPlayerByName(name.c_str());
    if (!player)
    {
        handler->SendSysMessage("Hearthstone: " + name + " is not online (matches end at logout).");
        return;
    }
    Hs::MatchInfo info;
    bool const had = HsMatches().Info(player->GetGUIDLow(), info) && info.running;
    bool const arena = had && HsMatches().IsArena(player->GetGUIDLow());
    if (!had || !InterruptMatch(player, player->GetGUIDLow(), Hs::Interrupt::Gm))
    {
        handler->SendSysMessage("Hearthstone: " + name + " has no running match.");
        return;
    }
    handler->SendSysMessage("Hearthstone: " + std::string(arena ? "suspended arena duel #" : "ended match #") +
        std::to_string(info.matchId) + " of " + name + (arena ? " (the run keeps it)." : " (interrupted, gm)."));
    sLog.outString("Hearthstone: GM %s ended match #%u of %s (guid %u)", GmLabel(handler).c_str(), info.matchId,
        name.c_str(), player->GetGUIDLow());
}

void HsMatchStatus(ChatHandler* handler)
{
    Hs::MatchSettings const m = sHearthstoneConfig.GetMatch();
    uint32 const aura = sHearthstoneConfig.Get().matchAuraSpellId;
    std::string effect;
    switch (GetEffectState(aura))
    {
        case EffectState::Unset:
            effect = "unset (0): matches start without the effect";
            break;
        case EffectState::Ok:
            effect = "spell " + std::to_string(aura) + " found";
            break;
        case EffectState::Missing:
            effect = "spell " + std::to_string(aura) + " MISSING (matches start without the effect)";
            break;
    }
    std::size_t const running = HsMatches().RunningCount();
    std::size_t const total = HsMatches().Count();
    std::string opponents;
    std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
    if (registry)
        for (uint32 entry : registry->Entries())
        {
            std::string modes;
            for (Hs::Difficulty d : registry->Offered(entry))
                modes += (modes.empty() ? "" : "/") + std::string(Hs::DifficultyCode(d));
            opponents += (opponents.empty() ? "" : ", ") + std::to_string(entry) + " (" + modes + ")";
        }
    std::shared_ptr<Hs::DeckLibrary const> const library = Library();
    std::string const decks = library ? std::to_string(library->ReadyCount()) + " of " + std::to_string(library->Decks().size()) +
        " library decks ready" : std::string("no deck library");
    handler->SendSysMessage(std::string("Matches: ") + (m.enable ? "enabled" : "DISABLED") + ", engine " +
        (HsMatches().CardDb() ? std::to_string(g_engineSupported.load()) + " of " + std::to_string(g_engineCards.load()) +
            " cards supported" : std::string("UNAVAILABLE (card data not loaded)")) + ", " + decks + ", opponents " +
        (opponents.empty() ? std::string("none") : opponents) + ", " + std::to_string(running) +
        " running + " + std::to_string(total - std::min(total, running)) + " finished, AI delay " +
        std::to_string(m.aiDelayMs) + "ms, history " + (m.historyEnable ? "on" : "off") + ", effect " + effect);
}

// ---- scripts -------------------------------------------------------------------------

class HearthstoneMatchPlayerScript : public PlayerScript
{
public:
    HearthstoneMatchPlayerScript()
        : PlayerScript("HearthstoneMatchPlayerScript", {
              PLAYERHOOK_ON_PLAYER_JUST_DIED,
              PLAYERHOOK_ON_BEFORE_TELEPORT,
              PLAYERHOOK_ON_MAP_CHANGED,
              PLAYERHOOK_ON_LOGIN,
              PLAYERHOOK_ON_BEFORE_LOGOUT,
              PLAYERHOOK_ON_LOGOUT,
              PLAYERHOOK_ON_DELETE
          })
    {
    }

    void OnPlayerJustDied(Player* player) override
    {
        InterruptIfRunning(player, Hs::Interrupt::Death);
        if (player)
        {
            HsHordePaladinForget(player->GetGUIDLow());
            HsAllianceShamanForget(player->GetGUIDLow());
            HsAllianceDruidForget(player->GetGUIDLow());
            HsAlliancePriestForget(player->GetGUIDLow());
            HsAllianceDruidQueue(player);
        }
    }

    void OnBeforeTeleport(Player* player, uint32 /*mapId*/, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/) override
    {
        InterruptIfRunning(player, Hs::Interrupt::Teleport);
        if (player)
        {
            HsHordePaladinForget(player->GetGUIDLow());
            HsAllianceShamanForget(player->GetGUIDLow());
            HsAlliancePriestForget(player->GetGUIDLow());
        }
    }

    void OnMapChanged(Player* player) override
    {
        InterruptIfRunning(player, Hs::Interrupt::Teleport);
        if (player)
        {
            HsHordePaladinForget(player->GetGUIDLow());
            HsAllianceShamanForget(player->GetGUIDLow());
            HsAlliancePriestForget(player->GetGUIDLow());
        }
        HsAllianceDruidQueue(player);
        HsWeeklyLogin(player);
    }

    void OnLogin(Player* player) override
    {
        if (!player)
            return;
        HsHordePaladinForget(player->GetGUIDLow());
        HsAllianceShamanForget(player->GetGUIDLow());
        HsAlliancePriestForget(player->GetGUIDLow());
        HsHordeDruidReconcileFeed(player);
        HsHordeRogueRefreshObjectives(player, true);
        HsAllianceDruidQueue(player);
        // A match never survives a logout; clear whatever a crashed or raced logout could have left.
        DropMatch(player, player->GetGUIDLow());
        // Orphan cleanup: the configured effect is removed whether or not a match exists.
        uint32 const aura = sHearthstoneConfig.Get().matchAuraSpellId;
        if (aura != 0 && player->HasAura(aura))
        {
            player->RemoveAurasDueToSpell(aura);
            sLog.outString("Hearthstone: removed a leftover In Card Match effect (spell %u) from %s (guid %u) at login",
                aura, player->GetName(), player->GetGUIDLow());
        }
        // Reconcile the quest NPC unlocks: quests turned in by any character of the account, also before
        // the feature existed. Only reads the WoW quest records; never changes them.
        if (uint32 const accountId = AccountOf(player))
        {
            HsNpcUnlocks().SetOnline(accountId);
            std::shared_ptr<Hs::OpponentRegistry const> const registry = Registry();
            if (registry && g_matchEnabled.load(std::memory_order_relaxed))
            {
                std::vector<uint32_t> quests;
                for (Hs::OpponentRegistry::UnlockNpc const& npc : registry->UnlockNpcs())
                    quests.push_back(npc.questId);
                std::set<uint32_t> completed = RewardedByCharacter(player, *registry);
                std::set<uint32_t> fromDb;
                if (LoadHsRewardedQuests(accountId, quests, fromDb))
                    completed.insert(fromDb.begin(), fromDb.end());
                ReconcileUnlocks(player, *registry, completed, "login");
            }
        }
    }

    void OnBeforeLogout(Player* player) override
    {
        if (player)
        {
            HsHordePaladinForget(player->GetGUIDLow());
            HsAllianceShamanForget(player->GetGUIDLow());
            HsAlliancePriestForget(player->GetGUIDLow());
            DropMatch(player, player->GetGUIDLow());
        }
    }

    void OnLogout(Player* player) override
    {
        if (!player)
            return;
        DropMatch(nullptr, player->GetGUIDLow());
        HsClassQuestForget(player->GetGUIDLow());
        HsNaxxForget(player->GetGUIDLow());
        HsWeeklyForget(player->GetGUIDLow());
        HsAlliancePriestForget(player->GetGUIDLow());
        HsHordeRogueForgetObjectives(player->GetGUIDLow());
        if (uint32 const accountId = AccountOf(player))
            HsNpcUnlocks().SetOffline(accountId);
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        DropMatch(nullptr, guid.GetCounter());
        HsHordeRogueForgetObjectives(guid.GetCounter());
        HsAllianceShamanForget(guid.GetCounter());
        HsAlliancePriestForget(guid.GetCounter());
    }
};

class HearthstoneMatchServerScript : public ServerScript
{
public:
    HearthstoneMatchServerScript()
        : ServerScript("HearthstoneMatchServerScript", {
              SERVERHOOK_CAN_PACKET_RECEIVE,
              SERVERHOOK_CAN_PACKET_SEND,
              SERVERHOOK_ON_PACKET_HANDLED
          })
    {
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        return ::CanPacketReceive(session, packet);
    }

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        Player* player = session ? session->GetPlayer() : nullptr;
        return HsHordeRogueCanSendQuest(player, packet);
    }

    void OnPacketHandled(WorldSession* session, WorldPacket const& packet) override
    {
        if (session && session->GetPlayer() && session->GetPlayer()->IsInWorld())
        {
            if (packet.GetOpcode() == CMSG_QUESTGIVER_ACCEPT_QUEST ||
                packet.GetOpcode() == CMSG_QUESTLOG_REMOVE_QUEST ||
                packet.GetOpcode() == CMSG_QUESTGIVER_CHOOSE_REWARD)
            {
                HsAllianceMageAfterQuestChange(session->GetPlayer());
                HsHunterAfterQuestChange(session->GetPlayer());
                HsNaxxRefreshVisibility(session->GetPlayer());
                uint32 acceptedQuest = 0;
                if (packet.GetOpcode() == CMSG_QUESTGIVER_ACCEPT_QUEST && packet.size() >= sizeof(uint64) + sizeof(uint32))
                {
                    WorldPacket copy(packet);
                    copy.rpos(sizeof(uint64));
                    copy >> acceptedQuest;
                }
                HsWeeklyAfterQuestChange(session->GetPlayer(), acceptedQuest);
            }
            if (packet.GetOpcode() == CMSG_QUESTLOG_REMOVE_QUEST)
            {
                HsClassQuestAfterQuestChange(session->GetPlayer());
                HsNaxxAfterQuestChange(session->GetPlayer());
                HsHordeWarriorAfterQuestChange(session->GetPlayer());
                HsHordeDruidAfterQuestChange(session->GetPlayer());
                HsWarlockAfterQuestChange(session->GetPlayer());
                HsAllianceRogueAfterQuestChange(session->GetPlayer());
                HsHordePriestAfterQuestChange(session->GetPlayer());
                HsHordePaladinAfterQuestChange(session->GetPlayer());
                HsAlliancePaladinAfterQuestChange(session->GetPlayer());
                HsAlliancePriestAfterQuestChange(session->GetPlayer());
                HsAllianceShamanAfterQuestChange(session->GetPlayer());
            }
            if (packet.GetOpcode() == CMSG_QUESTLOG_REMOVE_QUEST)
                HsHordeRogueAfterQuestChange(session->GetPlayer());
            // Objective changes from gossip credit refresh themselves; only quest-state packets need it here.
            if (packet.GetOpcode() == CMSG_QUESTGIVER_ACCEPT_QUEST ||
                packet.GetOpcode() == CMSG_QUESTLOG_REMOVE_QUEST)
                HsHordeRogueRefreshObjectives(session->GetPlayer(), true);
            else if (packet.GetOpcode() == CMSG_QUESTGIVER_CHOOSE_REWARD)
                HsHordeRogueRefreshObjectives(session->GetPlayer());
            if (packet.GetOpcode() == CMSG_QUESTLOG_REMOVE_QUEST &&
                session->GetPlayer()->GetQuestStatus(kHsAllianceDruidPatience) != QUEST_STATUS_INCOMPLETE)
                HsAllianceDruidForget(session->GetPlayer()->GetGUIDLow());
            if (packet.GetOpcode() == CMSG_QUESTGIVER_ACCEPT_QUEST ||
                packet.GetOpcode() == CMSG_QUESTLOG_REMOVE_QUEST)
                HsAllianceDruidQueue(session->GetPlayer());
        }
        if (packet.GetOpcode() == CMSG_GOSSIP_HELLO)
            HandleGossipHello(session, packet);
        else if (packet.GetOpcode() == CMSG_QUESTGIVER_CHOOSE_REWARD)
        {
            HandleQuestTurnIn(session, packet);
            HsIntroAfterTurnIn(session, packet);
            HsHordeShamanAfterTurnIn(session ? session->GetPlayer() : nullptr, packet);
            HsAlliancePriestAfterTurnIn(session ? session->GetPlayer() : nullptr, packet);
            HsArenaHostAfterTurnIn(session, packet);
        }
    }
};

class HearthstoneMatchCreatureScript : public AllCreatureScript
{
public:
    HearthstoneMatchCreatureScript() : AllCreatureScript("HearthstoneMatchCreatureScript") {}

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (creature && IsOpponentEntry(creature->GetEntry()))
            EnsureGossipFlag(creature);
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (creature && HsMatches().RunningCount() != 0)
            InterruptByCreature(creature->GetObjectGuid().GetRawValue());
    }
};

class HearthstoneMatchUnitScript : public UnitScript
{
public:
    HearthstoneMatchUnitScript() : UnitScript("HearthstoneMatchUnitScript", { UNITHOOK_ON_UNIT_DEATH }) {}

    // A dead opponent creature ends its matches; a player's death arrives through OnPlayerJustDied.
    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        if (unit && unit->GetTypeId() == TYPEID_UNIT && HsMatches().RunningCount() != 0)
            InterruptByCreature(unit->GetObjectGuid().GetRawValue());
    }
};

void AddHearthstoneMatchScripts()
{
    new HearthstoneMatchPlayerScript();
    new HearthstoneMatchServerScript();
    new HearthstoneMatchCreatureScript();
    new HearthstoneMatchUnitScript();
}
