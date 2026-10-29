#include "ScriptObjects.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GossipDef.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "NullCreatureAI.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsLadderBreadcrumb.h"
#include "HsLadderDuelStore.h"
#include "HsLadderGlue.h"
#include "HsLadderStore.h"
#include "HsLadderTable.h"
#include "HsMatchGlue.h"
#include "HsMatchStore.h"
#include "ladder/HsLadderDuel.h"
#include "ladder/HsLadderPlay.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace HL = Hs::Ladder;

namespace
{
    // Gossip. The sender is the chair's own (the match glue's 0x4853 never sees it: its select hook lets every other
    // sender through to the core, which hands a gameobject select to the script). The actions are looked up server side
    // from the stored menu, so the client cannot forge them.
    constexpr uint32 kSender = 0x4C44u; // 'LD'
    constexpr uint32 kActionRefresh = 0x4C440001u;   // the standing line: shows the offers again
    constexpr uint32 kActionNever = 0x4C440002u;
    constexpr uint32 kActionOfferBase = 0x4C440100u; // + the slot
    constexpr uint32 kActionConfirm = 0x4C440200u;
    constexpr uint32 kActionBack = 0x4C440201u;      // back to the deck page
    constexpr uint32 kActionResume = 0x4C440300u;    // the window of a paused duel
    constexpr uint32 kActionSurrender = 0x4C440301u;
    constexpr uint32 kActionSurrenderYes = 0x4C440302u;
    constexpr uint32 kActionDeckFlag = 0x80000000u;  // | the deck id
    constexpr uint32 kMaxDeckId = 0x0FFFFFFFu;
    // npc_text rows of world migration 0050 (the ids are HsLadderPlay.h, checked against the migration by the tests).
    constexpr uint32 kTextOffers = HL::kChairTextOffers;
    constexpr uint32 kTextDecks = HL::kChairTextDecks;
    constexpr uint32 kTextConfirm = HL::kChairTextConfirm;
    constexpr uint32 kTextPaused = HL::kChairTextPaused;
    constexpr uint32 kTextSurrender = HL::kChairTextSurrender;

    constexpr uint32 kFriendlyFaction = 35;             // "Friendly" to everybody: for this one summoned copy only
    constexpr uint32 kWalkTimeoutMs = 20000;            // the walk in: then it is placed at the end of the route
    constexpr uint32 kSeatPauseMs = 1500;               // sitting down, then the match starts
    constexpr uint32 kPartPauseMs = 3500;               // the line is said, then it stands up
    constexpr uint32 kBackTimeoutMs = 20000;            // the walk back: then it is unsummoned where it is
    constexpr uint32 kSafetyLifetimeMs = 90u * 60u * 1000u; // the summon's own timer: a net under the state machine
    constexpr float kArriveYards = 0.8f;                // a point counts as reached within this distance (2D)
    constexpr float kSeatSnapYards = 0.05f;             // closer than this to the seat: already on it
    constexpr float kStopSlackYards = 2.5f;            // ... or when the creature stopped within this distance
    constexpr float kAwayYards = 4.0f;                  // the player moved this far from the chair: the walk-up is off
    constexpr uint32 kPointBase = 0x4C440000u;          // MovePoint ids

    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    uint32 AccountOf(Player* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    float Distance2d(float ax, float ay, float bx, float by)
    {
        float const dx = ax - bx;
        float const dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    // ---- state ----------------------------------------------------------------------------------------------

    // Where the opponent sits.
    struct Seat
    {
        bool known = false;
        float x = 0, y = 0, z = 0, o = 0;
        uint8 state = UNIT_STAND_STATE_SIT_LOW_CHAIR;
    };

    enum class Phase { WalkIn, Seated, InMatch, Parting, WalkBack };

    struct Walk
    {
        HL::LadderTable table;
        bool preview = false;       // .hs ladder walk: no match, nothing counted
        uint32 playerGuid = 0;      // the player (a preview: the GM)
        uint32 accountId = 0;
        std::string playerName;
        uint32 deckId = 0;          // the player's deck
        HsLadderNpc npc;            // a preview: only the entry
        uint64 creature = 0;        // ObjectGuid raw value of the summon
        uint32 mapId = 0;
        uint32 instanceId = 0;
        Seat seat;
        float playerSeatX = 0, playerSeatY = 0; // the ladder chair's slot: the player sits here
        std::vector<HL::WorldPoint> in;   // the legs of the walk in, the last one the seat
        std::vector<HL::WorldPoint> back; // the legs of the walk back, the last one the spawn point
        std::size_t leg = 0;        // legs reached on the way in
        std::size_t backLeg = 0;
        bool moving = false;        // a MovePoint toward the current leg was given
        Phase phase = Phase::WalkIn;
        uint32 phaseMs = 0;
        bool cancel = false;        // an event asked to cancel before the match
        bool done = false;
        uint32 matchId = 0;
        uint64 duelId = 0;          // a resume: the stored ladder duel that is played on (0 for a new match)
    };

    // One ladder match that runs (or ran): the ladder result of its end is routed with this.
    struct LadderMatch
    {
        uint32 matchId = 0; // 0: just being started
        uint32 accountId = 0;
        std::string playerName;
        uint32 npcEntry = 0;
    };

    struct Ended
    {
        uint32 guid = 0;
        LadderMatch match;
        std::string res; // win loss draw int, or "paused" (the duel was suspended, no result)
        std::string why;
        uint32 endedMatchId = 0;
        uint64 duelId = 0; // the ladder duel the match was (deleted with its result)
    };

    struct TableEvent
    {
        enum class Kind { Cancel, Teleport } kind = Kind::Cancel;
        uint32 guid = 0;
        uint32 mapId = 0;
        float x = 0, y = 0;
    };

    // A pick in the window: which offer, which deck.
    struct Pick
    {
        uint32 tableGuid = 0;
        uint32 npcEntry = 0;
        uint32 deckId = 0;
    };

    // A leaf lock: nothing of the core, the match glue or the ladder glue is called while it is held.
    std::mutex g_mutex;
    std::map<uint32, uint32> g_tableClaim;  // table guid -> player guid
    std::map<uint32, uint32> g_playerClaim; // player guid -> table guid
    std::map<uint32, LadderMatch> g_matches; // player guid -> its ladder match
    std::map<uint32, Pick> g_picks;
    std::map<uint32, uint64> g_uiVisits; // character -> chair raw GUID, only after using an authorized chair
    std::set<uint64> g_walkers;             // raw guids of the live opponents
    std::vector<Walk> g_newWalks;           // summoned on a map thread, adopted by the world thread
    std::vector<TableEvent> g_events;
    std::vector<Ended> g_ended;
    // True while a claim or a ladder match exists: the hooks of every player stay cheap otherwise.
    std::atomic<uint32> g_busy{0};

    // Owned by the world thread (HsLadderTableUpdate and Shutdown), no lock.
    std::vector<Walk> g_active;

    bool ParseId(std::string const& s, uint32& out)
    {
        if (s.empty() || s.size() > 10)
            return false;
        uint64 value = 0;
        for (char c : s)
        {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + static_cast<uint32>(c - '0');
        }
        if (!value || value > UINT32_MAX)
            return false;
        out = static_cast<uint32>(value);
        return true;
    }

    void Recount()
    {
        g_busy.store(static_cast<uint32>(g_tableClaim.size() + g_matches.size()), std::memory_order_relaxed);
    }

    // ---- the claim --------------------------------------------------------------------------------------

    bool Claim(uint32 tableGuid, uint32 playerGuid, std::string& why)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_tableClaim.count(tableGuid))
        {
            why = "The table is taken.";
            return false;
        }
        if (g_playerClaim.count(playerGuid))
        {
            why = "You are already waiting for an opponent.";
            return false;
        }
        g_tableClaim[tableGuid] = playerGuid;
        g_playerClaim[playerGuid] = tableGuid;
        Recount();
        return true;
    }

    void Release(uint32 tableGuid)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const it = g_tableClaim.find(tableGuid);
        if (it == g_tableClaim.end())
            return;
        g_playerClaim.erase(it->second);
        g_tableClaim.erase(it);
        Recount();
    }

    // The player has a walk-up or a ladder match (at any table).
    bool HasClaim(uint32 playerGuid)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_playerClaim.count(playerGuid) != 0;
    }

    // The table is claimed by somebody else than the player.
    bool TakenByOther(uint32 tableGuid, uint32 playerGuid)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const it = g_tableClaim.find(tableGuid);
        return it != g_tableClaim.end() && it->second != playerGuid;
    }

    // ---- the window --------------------------------------------------------------------------------------

    void AddItem(GossipMenu& menu, uint8 icon, std::string const& text, uint32 action)
    {
        menu.AddMenuItem(icon, text, kSender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0); // the core reads the parallel action data vector unchecked: keep it aligned
    }

    void Refuse(Player* player, std::string const& why)
    {
        if (player->PlayerTalkClass)
            player->PlayerTalkClass->CloseGossip();
        SysMessage(player, why);
        sLog.outString("Hearthstone: ladder chair: refused %s (guid %u): %s", player->GetName(), player->GetGUIDLow(), why.c_str());
    }

    void ShowOffers(Player* player, GameObject* chair, HsLadderStanding const& standing,
                    std::vector<HsLadderOfferView> const& offers)
    {
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        AddItem(menu, GOSSIP_ICON_CHAT, HL::StandingText(standing.onLadder, standing.rank, standing.stars, standing.wins,
            standing.losses, standing.draws), kActionRefresh);
        for (std::size_t i = 0; i < offers.size(); ++i)
        {
            HsLadderOfferView const& o = offers[i];
            AddItem(menu, GOSSIP_ICON_BATTLE, HL::OfferLineText(o.name, o.heroClass, o.faction, o.rank, o.stars, o.wins, o.losses,
                o.draws), kActionOfferBase + static_cast<uint32>(i));
        }
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", kActionNever);
        player->PlayerTalkClass->SendGossipMenu(kTextOffers, chair->GetObjectGuid());
    }

    // Page 1. False (told) when the ladder cannot offer.
    bool OpenOffers(Player* player, GameObject* chair)
    {
        HsLadderStanding standing;
        std::vector<HsLadderOfferView> offers;
        std::string why;
        if (!HsLadderOffersFor(AccountOf(player), standing, offers, why))
        {
            Refuse(player, why);
            return false;
        }
        ShowOffers(player, chair, standing, offers);
        return true;
    }

    // ---- the window of a paused duel ---------------------------------------------------------------------

    HsLadderNpc NpcOfDuel(HL::DuelRow const& row)
    {
        HsLadderNpc npc;
        npc.entry = row.npcEntry;
        npc.name = row.opponentName;
        npc.heroClass = row.opponentClass;
        npc.deckKey = row.opponentDeckKey;
        npc.profile = row.profile == "hard" ? Hs::Match::AiProfile::Hard : Hs::Match::AiProfile::Normal;
        npc.deck = row.opponentDeck;
        return npc;
    }

    void ShowPausedDuel(Player* player, GameObject* chair, HL::DuelRow const& row)
    {
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        AddItem(menu, GOSSIP_ICON_CHAT, "Your game against " + row.opponentName + " (" + HL::ClassText(row.opponentClass) +
            ") is waiting.", kActionRefresh);
        AddItem(menu, GOSSIP_ICON_BATTLE, "Resume the duel", kActionResume);
        AddItem(menu, GOSSIP_ICON_CHAT, "Surrender (counts as a loss)", kActionSurrender);
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", kActionNever);
        player->PlayerTalkClass->SendGossipMenu(kTextPaused, chair->GetObjectGuid());
    }

    void ShowSurrender(Player* player, GameObject* chair, HL::DuelRow const& row)
    {
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        AddItem(menu, GOSSIP_ICON_BATTLE, "Yes: give up the game against " + row.opponentName, kActionSurrenderYes);
        AddItem(menu, GOSSIP_ICON_CHAT, "No: back to my game", kActionRefresh);
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", kActionNever);
        player->PlayerTalkClass->SendGossipMenu(kTextSurrender, chair->GetObjectGuid());
    }

    // The stored duel of the player's account that can be played or given up now. False (told) otherwise.
    bool FindPausedDuel(Player* player, HL::DuelRow& row)
    {
        bool found = false;
        if (!HsLadderDuels().Find(AccountOf(player), found, row))
        {
            Refuse(player, "Your ladder duel could not be read right now (see the server log). Try again later.");
            return false;
        }
        if (!found)
        {
            Refuse(player, "You have no paused ladder duel.");
            return false;
        }
        if (row.state != HL::kDuelPaused)
        {
            Refuse(player, "Your ladder duel is being played right now.");
            return false;
        }
        if (HsLadderDuelQueued(row.id))
        {
            Refuse(player, "The result of that duel is still being recorded. Try again in a moment.");
            return false;
        }
        return true;
    }

    // The player has a stored ladder duel: the paused window instead of the offers. False: none (failed: the read failed and
    // the player was told).
    bool OpenPausedWindow(Player* player, GameObject* chair, bool& failed)
    {
        failed = false;
        if (!HsLadderDuels().Ready())
            return false;
        bool found = false;
        HL::DuelRow row;
        if (!HsLadderDuels().Find(AccountOf(player), found, row))
        {
            Refuse(player, "Your ladder duel could not be read right now (see the server log). Try again later.");
            failed = true;
            return false;
        }
        if (!found)
            return false;
        ShowPausedDuel(player, chair, row);
        return true;
    }

    bool AddonView(Player* player, std::string& payload, std::string& why)
    {
        Hs::KvList kv;
        HsLadderStanding standing;
        bool const hadClaim = HasClaim(player->GetGUIDLow());
        HL::DuelRow duel;
        bool found = false;
        if (!HsLadderDuels().Ready() || !HsLadderDuels().Find(AccountOf(player), found, duel))
        {
            why = "Your ladder duel could not be read. Try again later.";
            return false;
        }
        if (found)
        {
            kv = {{"st", duel.state == HL::kDuelPaused && !HsLadderDuelQueued(duel.id) ? "paused" : "waiting"},
                {"duel", std::to_string(duel.id)}, {"npc", std::to_string(duel.npcEntry)},
                {"name", duel.opponentName}, {"class", duel.opponentClass}};
        }
        else if (hadClaim)
            kv = {{"st", "waiting"}};
        else
        {
            std::vector<HsLadderOfferView> offers;
            if (!HsLadderOffersFor(AccountOf(player), standing, offers, why))
                return false;
            kv = {{"st", "offers"}, {"n", std::to_string(offers.size())}};
            for (std::size_t i = 0; i < offers.size(); ++i)
            {
                auto const& o = offers[i];
                std::string const p = "o" + std::to_string(i + 1);
                kv.emplace_back(p + "id", std::to_string(o.entry));
                kv.emplace_back(p + "name", o.name);
                kv.emplace_back(p + "class", o.heroClass);
                kv.emplace_back(p + "faction", HL::FactionText(o.faction));
                kv.emplace_back(p + "rank", std::to_string(o.rank));
                kv.emplace_back(p + "stars", std::to_string(o.stars));
                kv.emplace_back(p + "wins", std::to_string(o.wins));
                kv.emplace_back(p + "losses", std::to_string(o.losses));
                kv.emplace_back(p + "draws", std::to_string(o.draws));
            }
            std::vector<std::pair<uint32_t, std::string>> decks;
            std::string deckWhy;
            if (!HsMatchReadyDecks(player, decks, deckWhy))
                decks.clear();
            kv.emplace_back("dn", std::to_string(decks.size()));
            kv.emplace_back("note", deckWhy);
            for (std::size_t i = 0; i < decks.size(); ++i)
            {
                std::string const p = "d" + std::to_string(i + 1);
                kv.emplace_back(p + "id", std::to_string(decks[i].first));
                kv.emplace_back(p + "name", decks[i].second);
            }
        }
        if (found || hadClaim)
        {
            if (!HsLadderStandingFor(AccountOf(player), standing))
            {
                why = "Your ladder standing could not be read. Try again later.";
                return false;
            }
        }
        kv.emplace_back("rank", std::to_string(standing.rank));
        kv.emplace_back("stars", std::to_string(standing.stars));
        kv.emplace_back("maxstars", std::to_string(HL::MaxStars(standing.rank)));
        kv.emplace_back("legendpos", std::to_string(standing.legendPosition));
        kv.emplace_back("wins", std::to_string(standing.wins));
        kv.emplace_back("losses", std::to_string(standing.losses));
        kv.emplace_back("draws", std::to_string(standing.draws));
        payload = Hs::KvEncode(kv);
        return true;
    }

    GameObject* AddonChair(Player* player)
    {
        uint64 raw = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto const it = g_uiVisits.find(player->GetGUIDLow());
            if (it != g_uiVisits.end())
                raw = it->second;
        }
        GameObject* chair = raw && player->IsInWorld() ? player->GetMap()->GetGameObject(ObjectGuid(raw)) : nullptr;
        return chair && player->IsAlive() && player->IsSittingDown() && player->IsWithinDistInMap(chair, kAwayYards)
            ? chair : nullptr;
    }

    // Page 2: the ready decks.
    void ShowDecks(Player* player, GameObject* chair)
    {
        std::vector<std::pair<uint32_t, std::string>> decks;
        std::string why;
        if (!HsMatchReadyDecks(player, decks, why))
        {
            Refuse(player, why);
            return;
        }
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        for (auto const& deck : decks)
            AddItem(menu, GOSSIP_ICON_BATTLE, deck.second, kActionDeckFlag | (deck.first & kMaxDeckId));
        AddItem(menu, GOSSIP_ICON_CHAT, "Back to the offers", kActionRefresh);
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", kActionNever);
        player->PlayerTalkClass->SendGossipMenu(kTextDecks, chair->GetObjectGuid());
    }

    // Page 3: the question.
    void ShowConfirm(Player* player, GameObject* chair, std::string const& npcName, std::string const& deckName)
    {
        player->PlayerTalkClass->ClearMenus();
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        AddItem(menu, GOSSIP_ICON_BATTLE, "Yes: play " + npcName + " with " + deckName, kActionConfirm);
        AddItem(menu, GOSSIP_ICON_CHAT, "Choose another deck", kActionBack);
        AddItem(menu, GOSSIP_ICON_CHAT, "Never mind", kActionNever);
        player->PlayerTalkClass->SendGossipMenu(kTextConfirm, chair->GetObjectGuid());
    }

    // ---- the summon ---------------------------------------------------------------------------------------

    // Where the opponent sits: the chair of the table (ObjectMgr's spawn data, the live gameobject for a chair with
    // several slots).
    Seat ResolveSeat(Map* map, HL::LadderTable const& table)
    {
        Seat seat;
        GameObjectData const* data = sObjectMgr.GetGOData(table.opponentChairGuid);
        GameObjectInfo const* info = data ? sObjectMgr.GetGameObjectInfo(data->id) : nullptr;
        if (!data || !info || info->type != GAMEOBJECT_TYPE_CHAIR)
            return seat;
        seat.known = true;
        seat.x = data->position.x;
        seat.y = data->position.y;
        seat.z = data->position.z;
        seat.o = data->position.o;
        seat.state = static_cast<uint8>(UNIT_STAND_STATE_SIT_LOW_CHAIR + info->chair.height);
        if (info->chair.slots > 1 && map)
            if (GameObject* go = map->GetGameObject(ObjectGuid(HIGHGUID_GAMEOBJECT, data->id, table.opponentChairGuid)))
                go->GetClosestChairSlotPosition(table.stop.x, table.stop.y, seat.x, seat.y);
        return seat;
    }

    // The module's own idle AI instead of the creature's: no combat, no script, no event AI.
    void SetupAi(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
    }

    // Makes the summoned copy a harmless walker: friendly to everybody, no services, passive, not to be hurt.
    void MakeWalker(Creature* creature)
    {
        creature->SetFactionTemporary(kFriendlyFaction, TEMPFACTION_NONE);
        creature->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_NONE);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED);
        creature->SetReactState(REACT_PASSIVE);
        creature->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
        creature->SetStandState(UNIT_STAND_STATE_STAND);
        creature->SetWalk(true);
    }

    // Claims the table, summons the opponent and hands the walk to the world thread. player summons (and, for a
    // preview, is the GM). chair: the player's ladder chair (null for a preview). False with the reason.
    bool Launch(Player* player, GameObject* chair, HL::LadderTable const& table, HsLadderNpc const& npc, uint32 deckId,
                bool preview, std::string& why, uint64 resumeDuel = 0)
    {
        Map* map = player->GetMap();
        if (!map || !player->IsInWorld())
        {
            why = "You are not in the world.";
            return false;
        }
        if (!Claim(table.tableGuid, player->GetGUIDLow(), why))
            return false;
        Walk w;
        w.table = table;
        w.preview = preview;
        w.playerGuid = player->GetGUIDLow();
        w.accountId = AccountOf(player);
        w.playerName = player->GetName();
        w.deckId = deckId;
        w.npc = npc;
        w.duelId = resumeDuel;
        w.seat = ResolveSeat(map, table);
        w.playerSeatX = player->GetPositionX();
        w.playerSeatY = player->GetPositionY();
        if (chair)
        {
            float x = w.playerSeatX, y = w.playerSeatY;
            chair->GetClosestChairSlotPosition(w.playerSeatX, w.playerSeatY, x, y);
            w.playerSeatX = x;
            w.playerSeatY = y;
        }
        HL::WorldPoint const seatPoint = w.seat.known ? HL::WorldPoint{ w.seat.x, w.seat.y, w.seat.z } : table.stop;
        w.in = HL::WalkInPoints(table, seatPoint);

        Creature* creature = player->SummonCreature(npc.entry, table.spawn.x, table.spawn.y, table.spawn.z, table.spawnO,
            TEMPSUMMON_TIMED_DESPAWN, kSafetyLifetimeMs, false, 0, &SetupAi);
        if (!creature)
        {
            Release(table.tableGuid);
            why = "The opponent could not be summoned.";
            sLog.outError("Hearthstone: ladder walk-up: creature %u could not be summoned at the table in %s", npc.entry,
                table.city.c_str());
            return false;
        }
        MakeWalker(creature);
        w.creature = creature->GetObjectGuid().GetRawValue();
        w.mapId = creature->GetMapId();
        w.instanceId = creature->GetInstanceId();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_walkers.insert(w.creature);
            g_newWalks.push_back(w);
        }
        sLog.outString("Hearthstone: ladder walk-up%s: %s (creature %u) walks to the table in %s for %s (guid %u)%s",
            preview ? " preview" : "", creature->GetName(), npc.entry, table.city.c_str(), player->GetName(),
            player->GetGUIDLow(), w.seat.known ? "" : ", the opponent chair is not found: it stands at the stop point");
        return true;
    }

    // ---- stepping a walk-up -----------------------------------------------------------------------------------

    Creature* FindCreature(Walk const& w)
    {
        Map* map = sMapMgr.FindMap(w.mapId, w.instanceId);
        Creature* creature = map ? map->GetCreature(ObjectGuid(w.creature)) : nullptr;
        return creature && creature->IsInWorld() ? creature : nullptr;
    }

    // The player still sits at the chair (a preview: the GM is in the world).
    bool PlayerThere(Walk const& w, Player* player)
    {
        if (!player || !player->IsInWorld())
            return false;
        if (w.preview)
            return true;
        if (!player->IsAlive() || !player->IsSittingDown() || player->GetMapId() != w.table.mapId)
            return false;
        return Distance2d(player->GetPositionX(), player->GetPositionY(), w.playerSeatX, w.playerSeatY) <= kAwayYards;
    }

    // Moves along pts. True when every leg is reached.
    bool Advance(Walk& w, Creature* creature, std::vector<HL::WorldPoint> const& pts, std::size_t& leg, float finalO)
    {
        while (leg < pts.size())
        {
            HL::WorldPoint const& t = pts[leg];
            float const d = Distance2d(creature->GetPositionX(), creature->GetPositionY(), t.x, t.y);
            if (w.moving)
            {
                if (d > kArriveYards && !(creature->IsStopped() && d <= kStopSlackYards))
                    return false;
                w.moving = false;
                ++leg;
                continue;
            }
            if (d <= kArriveYards)
            {
                ++leg; // already there (the seat leg from the stop point, a leg of a walk back from the seat)
                continue;
            }
            bool const last = leg + 1 == pts.size();
            creature->GetMotionMaster()->MovePoint(kPointBase + static_cast<uint32>(leg), t.x, t.y, t.z, MOVE_WALK_MODE, 0.0f,
                last ? finalO : -10.0f);
            w.moving = true;
            return false;
        }
        return true;
    }

    // Unsummons the opponent and ends the walk-up (idempotent).
    void Finish(Walk& w, Creature* creature)
    {
        if (w.done)
            return;
        w.done = true;
        if (creature)
        {
            creature->GetMotionMaster()->MoveIdle();
            if (creature->IsTemporarySummon())
                static_cast<TemporarySummon*>(creature)->UnSummon();
            else
                creature->AddObjectToRemoveList();
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_walkers.erase(w.creature);
        }
        Release(w.table.tableGuid);
    }

    // The way back: stand up and walk the points that were passed, then unsummon.
    void StartBack(Walk& w, Creature* creature, char const* why)
    {
        if (why)
            sLog.outString("Hearthstone: ladder walk-up: %s (guid %u) goes back to the spawn point: %s", creature->GetName(),
                w.playerGuid, why);
        creature->SetStandState(UNIT_STAND_STATE_STAND);
        w.back = HL::WalkBackPoints(w.table, w.in, w.leg);
        w.backLeg = 0;
        w.moving = false;
        w.phase = Phase::WalkBack;
        w.phaseMs = 0;
    }

    // The opponent sits down: on its chair, or it stands at the stop point looking at the player's chair.
    void SitDown(Walk& w, Creature* creature)
    {
        if (w.seat.known)
        {
            // Always onto the exact seat: the walk counts as arrived within kArriveYards (kStopSlackYards when it
            // stopped), which would leave the NPC sitting next to the chair.
            if (Distance2d(creature->GetPositionX(), creature->GetPositionY(), w.seat.x, w.seat.y) > kSeatSnapYards ||
                std::fabs(creature->GetPositionZ() - w.seat.z) > kSeatSnapYards)
                creature->NearTeleportTo(w.seat.x, w.seat.y, w.seat.z, w.seat.o);
            creature->SetFacingTo(w.seat.o);
            creature->SetStandState(w.seat.state);
        }
        else
        {
            if (Distance2d(creature->GetPositionX(), creature->GetPositionY(), w.table.stop.x, w.table.stop.y) > kStopSlackYards)
                creature->NearTeleportTo(w.table.stop.x, w.table.stop.y, w.table.stop.z, creature->GetOrientation());
            creature->SetFacingTo(std::atan2(w.playerSeatY - creature->GetPositionY(), w.playerSeatX - creature->GetPositionX()));
        }
        w.phase = Phase::Seated;
        w.phaseMs = 0;
    }

    // The line, then the pause before it stands up.
    void StartParting(Walk& w, Creature* creature, Player* player, std::string const& line)
    {
        if (!line.empty())
            creature->MonsterSay(line, LANG_UNIVERSAL, player);
        w.phase = Phase::Parting;
        w.phaseMs = 0;
    }

    void RegisterMatch(Walk const& w)
    {
        LadderMatch m;
        m.accountId = w.accountId;
        m.playerName = w.playerName;
        m.npcEntry = w.npc.entry;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_matches[w.playerGuid] = m;
        Recount();
    }

    void UnregisterMatch(uint32 guid)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_matches.erase(guid);
        Recount();
    }

    bool EndPending(uint32 guid)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (Ended const& e : g_ended)
            if (e.guid == guid)
                return true;
        return false;
    }

    void PostEnded(Ended const& e)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_ended.push_back(e);
    }

    void StartMatchNow(Walk& w, Creature* creature, Player* player)
    {
        HsLadderMatchSetup setup;
        setup.npcEntry = w.npc.entry;
        setup.opponentName = creature->GetName();
        setup.opponentClass = w.npc.heroClass;
        setup.opponentDeckKey = w.npc.deckKey;
        setup.opponentDeck = w.npc.deck;
        setup.profile = w.npc.profile;
        // Registered first: an end that comes the moment the match exists must find its ladder entry.
        RegisterMatch(w);
        uint32 matchId = 0;
        std::string endedRes;
        // A resume rebuilds the stored duel (its own decks and seed), a new match is stored as it starts.
        std::string const why = w.duelId != 0 ? HsMatchResumeLadder(player, creature, w.duelId, matchId, endedRes)
                                              : HsMatchStartLadder(player, creature, w.deckId, setup, matchId);
        if (why.empty() && !endedRes.empty())
        {
            // The log replayed to the end of the duel (the server stopped before its result was applied): the result is
            // applied now, like the end of a match.
            LadderMatch m;
            m.matchId = matchId;
            m.accountId = w.accountId;
            m.playerName = w.playerName;
            m.npcEntry = w.npc.entry;
            Ended e;
            e.guid = w.playerGuid;
            e.match = m;
            e.res = endedRes;
            e.why = "replayed";
            e.endedMatchId = matchId;
            e.duelId = w.duelId;
            UnregisterMatch(w.playerGuid);
            PostEnded(e); // the table stays claimed until the walk is finished
            w.matchId = matchId;
            w.phase = Phase::InMatch;
            w.phaseMs = 0;
            return;
        }
        if (!why.empty())
        {
            UnregisterMatch(w.playerGuid);
            SysMessage(player, why);
            sLog.outString("Hearthstone: ladder match of %s (guid %u) against %s could not start: %s", player->GetName(),
                w.playerGuid, creature->GetName(), why.c_str());
            StartBack(w, creature, "the match could not start");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto const it = g_matches.find(w.playerGuid);
            if (it != g_matches.end())
                it->second.matchId = matchId;
        }
        w.matchId = matchId;
        w.phase = Phase::InMatch;
        w.phaseMs = 0;
    }

    void Step(Walk& w, uint32 diff)
    {
        w.phaseMs += diff;
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, w.playerGuid));
        Creature* creature = FindCreature(w);
        if (!creature)
        {
            sLog.outString("Hearthstone: ladder walk-up for guid %u ends: the opponent (creature %u) is gone", w.playerGuid,
                w.npc.entry);
            Finish(w, nullptr);
            return;
        }
        switch (w.phase)
        {
            case Phase::WalkIn:
            {
                if (w.cancel || !PlayerThere(w, player))
                {
                    StartBack(w, creature, w.cancel ? "the player logged out, left the map, died or teleported"
                        : "the player stood up or moved away");
                    return;
                }
                bool arrived = Advance(w, creature, w.in, w.leg, w.seat.known ? w.seat.o : -10.0f);
                if (!arrived && w.phaseMs >= kWalkTimeoutMs)
                {
                    // Late (a blocked path, a slow walker): placed at the end of the route.
                    sLog.outString("Hearthstone: ladder walk-up: %s (creature %u) did not arrive in %u s, placed at the table",
                        creature->GetName(), w.npc.entry, kWalkTimeoutMs / 1000);
                    w.leg = w.in.size();
                    arrived = true;
                    HL::WorldPoint const& end = w.in.back();
                    creature->NearTeleportTo(end.x, end.y, end.z, w.seat.known ? w.seat.o : creature->GetOrientation());
                }
                if (arrived)
                    SitDown(w, creature);
                return;
            }
            case Phase::Seated:
            {
                if (w.cancel || !PlayerThere(w, player))
                {
                    StartBack(w, creature, w.cancel ? "the player logged out, left the map, died or teleported"
                        : "the player stood up or moved away");
                    return;
                }
                if (w.phaseMs < kSeatPauseMs)
                    return;
                if (w.preview)
                {
                    uint32 const seed = static_cast<uint32>(HsNowMs()) ^ (w.playerGuid * 2654435761u);
                    StartParting(w, creature, player, HsLadderLine(static_cast<HL::Result>(seed % 3), seed));
                    return;
                }
                StartMatchNow(w, creature, player);
                return;
            }
            case Phase::InMatch:
            {
                // The end of the match arrives as an event (HandleEnded). A match that is gone without one (nothing
                // should do that) frees the table anyway.
                if (!HsMatches().IsRunning(w.playerGuid) && !EndPending(w.playerGuid))
                {
                    UnregisterMatch(w.playerGuid);
                    StartBack(w, creature, "the match is not running any more");
                }
                return;
            }
            case Phase::Parting:
            {
                if (w.phaseMs >= kPartPauseMs)
                    StartBack(w, creature, nullptr);
                return;
            }
            case Phase::WalkBack:
            {
                if (Advance(w, creature, w.back, w.backLeg, -10.0f) || w.phaseMs >= kBackTimeoutMs)
                    Finish(w, creature);
                return;
            }
        }
    }

    // ---- results ---------------------------------------------------------------------------------------------

    // The text a player reads after a counted ladder match.
    std::string ResultText(HsLadderResultReply const& reply, HL::Result result, std::string const& npcName)
    {
        char const* const word = result == HL::Result::Win ? "won against " : result == HL::Result::Loss ? "lost against "
            : "drew against ";
        if (reply.queued)
            return std::string("Ladder: you ") + word + npcName + ". The result is recorded as soon as the ladder is free.";
        HL::PlayerResultReport const& r = reply.report;
        std::string text = std::string("Ladder: you ") + word + npcName + ". " + HL::PositionText(r.before.rank, r.before.stars);
        if (r.before.rank == r.after.rank && r.before.stars == r.after.stars)
            text += ", no change.";
        else
            text += " -> " + HL::PositionText(r.after.rank, r.after.stars) + ".";
        if (r.streakBonus)
            text += " (win streak bonus star)";
        if (r.newBest)
            text += " New best rank!";
        for (std::string const& milestone : reply.milestones)
            text += " Milestone: " + HL::MilestoneText(milestone);
        return text;
    }

    Walk* FindWalk(uint32 playerGuid)
    {
        for (Walk& w : g_active)
            if (w.playerGuid == playerGuid && !w.done)
                return &w;
        return nullptr;
    }

    // The stored duel was left "active" by an end that did not count (a refused result, an interruption that is no
    // suspension): it is paused, so the account can resume it and the result is applied again from the replayed log.
    void PauseStoredDuel(uint64 duelId)
    {
        if (duelId == 0)
            return;
        if (!HsLadderStore().Run(HL::DuelPauseSql(duelId, 0, false, 0), "duel pause"))
            sLog.outError("Hearthstone: ladder duel %llu could not be marked paused after its end", static_cast<unsigned long long>(duelId));
    }

    // A ladder match ended or its duel was paused (world thread): a result goes to the ladder, then the opponent says its
    // line; a pause only sends the opponent home.
    void HandleEnded(Ended const& e)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, e.guid));
        std::string line;
        HL::Result result = HL::Result::Win;
        if (e.res == "paused")
        {
            // The sink stored the duel and the log (HsLadderDuelStorage::StoreSuspended) before this event was posted.
            SysMessage(player, "Your ladder duel is paused. Sit down on a ladder chair to resume it.");
            sLog.outString("Hearthstone: ladder match #%u of %s (account %u) against %u was paused (%s): no ladder result, the duel "
                "waits at the chairs", e.endedMatchId, e.match.playerName.c_str(), e.match.accountId, e.match.npcEntry, e.why.c_str());
        }
        else if (HL::PlayerResultOfMatch(e.res, result))
        {
            HsLadderResultReply const reply = HsLadderMatchResult(e.match.accountId, e.match.playerName, e.match.npcEntry, result,
                e.duelId);
            HsLadderNpc npc;
            std::string const npcName = HsLadderNpcSetup(e.match.npcEntry, npc) ? npc.name : "your opponent";
            if (reply.ok)
            {
                SysMessage(player, ResultText(reply, result, npcName));
                uint32 const seed = static_cast<uint32>(HsNowMs()) ^ (e.guid * 2654435761u);
                line = HsLadderLine(result, seed);
                sLog.outString("Hearthstone: ladder match #%u of %s (account %u) against %s (%u): %s (%s)%s", e.endedMatchId,
                    e.match.playerName.c_str(), e.match.accountId, npcName.c_str(), e.match.npcEntry, e.res.c_str(),
                    e.why.c_str(), reply.queued ? ", queued" : "");
            }
            else
            {
                // The duel stays stored (paused): resuming it replays the finished game and records the result again.
                PauseStoredDuel(e.duelId);
                SysMessage(player, "Ladder: the result of your match could not be recorded (" + HsLadderWhyText(reply.why) +
                    "). Sit down on a ladder chair to try again.");
                sLog.outError("Hearthstone: ladder match #%u of %s (account %u) against %u: result %s not recorded (%s); the duel "
                    "stays stored", e.endedMatchId, e.match.playerName.c_str(), e.match.accountId, e.match.npcEntry, e.res.c_str(),
                    reply.why.c_str());
            }
        }
        else
        {
            // An end that is no result and no pause (an engine error): no loss and no win, the duel stays stored.
            PauseStoredDuel(e.duelId);
            SysMessage(player, "Your ladder match was interrupted. It does not count: sit down on a ladder chair to resume it.");
            sLog.outString("Hearthstone: ladder match #%u of %s (account %u) against %u was interrupted (%s): no ladder result, the "
                "duel stays stored", e.endedMatchId, e.match.playerName.c_str(), e.match.accountId, e.match.npcEntry,
                e.why.c_str());
        }
        Walk* w = FindWalk(e.guid);
        if (!w || w->phase != Phase::InMatch)
            return;
        Creature* creature = FindCreature(*w);
        if (!creature)
        {
            Finish(*w, nullptr);
            return;
        }
        if (!line.empty())
            StartParting(*w, creature, player, line);
        else
            StartBack(*w, creature, nullptr);
    }

    void HandleEvent(TableEvent const& e)
    {
        Walk* w = FindWalk(e.guid);
        if (!w)
            return;
        if (e.kind == TableEvent::Kind::Cancel)
            w->cancel = true;
        else if (e.mapId != w->table.mapId ||
                 Distance2d(e.x, e.y, w->playerSeatX, w->playerSeatY) > kAwayYards)
            w->cancel = true; // a teleport away from the chair (sitting down is a small one and does not count)
    }

    // ---- the hooks ------------------------------------------------------------------------------------------

    void PostCancel(uint32 guid, bool forgetPick)
    {
        if (g_busy.load(std::memory_order_relaxed) == 0 && !forgetPick)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (forgetPick)
            g_picks.erase(guid);
        if (g_playerClaim.count(guid))
        {
            TableEvent e;
            e.kind = TableEvent::Kind::Cancel;
            e.guid = guid;
            g_events.push_back(e);
        }
    }

    // ---- resume and surrender -------------------------------------------------------------------------------

    void ResumeDuel(Player* player, GameObject* chair, HL::LadderTable const& table)
    {
        std::string why = HsMatchResumeRefusal(player);
        if (why.empty() && TakenByOther(table.tableGuid, player->GetGUIDLow()))
            why = "The table is taken.";
        if (!why.empty())
        {
            Refuse(player, why);
            return;
        }
        HL::DuelRow row;
        if (!FindPausedDuel(player, row))
            return;
        HsLadderNpc const npc = NpcOfDuel(row);
        if (!Launch(player, chair, table, npc, row.deckId, false, why, row.id))
        {
            Refuse(player, why);
            return;
        }
        player->PlayerTalkClass->CloseGossip();
        SysMessage(player, npc.name + " is on the way to the table to finish your game.");
    }

    void SurrenderDuel(Player* player)
    {
        HL::DuelRow row;
        if (!FindPausedDuel(player, row))
            return;
        player->PlayerTalkClass->CloseGossip();
        HsLadderResultReply const reply = HsLadderMatchResult(AccountOf(player), player->GetName(), row.npcEntry,
            HL::Result::Loss, row.id);
        if (reply.ok)
        {
            SysMessage(player, ResultText(reply, HL::Result::Loss, row.opponentName));
            sLog.outString("Hearthstone: ladder duel %llu of %s (account %u) against %u was given up at the chair: a loss%s",
                static_cast<unsigned long long>(row.id), player->GetName(), AccountOf(player), row.npcEntry,
                reply.queued ? ", queued" : "");
        }
        else
        {
            SysMessage(player, "Ladder: your surrender could not be recorded (" + HsLadderWhyText(reply.why) + "). Your duel stays.");
            sLog.outError("Hearthstone: ladder duel %llu of %s (account %u): the surrender was not recorded (%s)",
                static_cast<unsigned long long>(row.id), player->GetName(), AccountOf(player), reply.why.c_str());
        }
    }

    // ---- the chair script ---------------------------------------------------------------------------------------

    void Select(Player* player, GameObject* chair, uint32 action)
    {
        if (!player->IsInWorld() || !player->PlayerTalkClass)
            return;
        uint32 const guid = player->GetGUIDLow();
        HL::LadderTable table;
        if (!HsLadderTableByChair(chair->GetGUIDLow(), table))
        {
            player->PlayerTalkClass->CloseGossip();
            return;
        }
        if (action == kActionNever)
        {
            player->PlayerTalkClass->CloseGossip();
            return;
        }
        std::string why = HsLadderChairWhy();
        if (!why.empty())
        {
            Refuse(player, why);
            return;
        }
        if (!HsLadderChairUnlocked(player))
        {
            Refuse(player, "You do not know the rules of the league yet.");
            return;
        }
        if (!player->IsSittingDown())
        {
            Refuse(player, "Sit down on the chair first.");
            return;
        }
        if (action == kActionRefresh)
        {
            bool failed = false;
            if (!OpenPausedWindow(player, chair, failed) && !failed)
                OpenOffers(player, chair);
            return;
        }
        // A paused duel: resume it at this table or give it up.
        if (action == kActionResume)
        {
            ResumeDuel(player, chair, table);
            return;
        }
        if (action == kActionSurrender)
        {
            HL::DuelRow row;
            if (FindPausedDuel(player, row))
                ShowSurrender(player, chair, row);
            return;
        }
        if (action == kActionSurrenderYes)
        {
            SurrenderDuel(player);
            return;
        }

        if (action >= kActionOfferBase && action < kActionOfferBase + static_cast<uint32>(HL::kOfferCount))
        {
            why = HsMatchStartRefusal(player);
            if (why.empty() && TakenByOther(table.tableGuid, guid))
                why = "The table is taken.";
            if (!why.empty())
            {
                Refuse(player, why);
                return;
            }
            HsLadderStanding standing;
            std::vector<HsLadderOfferView> offers;
            std::size_t const slot = action - kActionOfferBase;
            if (!HsLadderOffersFor(AccountOf(player), standing, offers, why) || slot >= offers.size())
            {
                Refuse(player, why.empty() ? std::string("That offer is not there any more. Sit down again.") : why);
                return;
            }
            HsLadderNpc npc;
            if (!HsLadderNpcSetup(offers[slot].entry, npc) || npc.blocked || !npc.enabled)
            {
                Refuse(player, "That opponent cannot sit down with you right now.");
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                Pick pick;
                pick.tableGuid = table.tableGuid;
                pick.npcEntry = npc.entry;
                g_picks[guid] = pick;
            }
            ShowDecks(player, chair);
            return;
        }

        Pick pick;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto const it = g_picks.find(guid);
            if (it != g_picks.end())
                pick = it->second;
        }
        if (pick.npcEntry == 0 || pick.tableGuid != table.tableGuid)
        {
            Refuse(player, "Pick an opponent first. Sit down again.");
            return;
        }

        if ((action & kActionDeckFlag) != 0 || action == kActionBack || action == kActionConfirm)
        {
            why = HsMatchStartRefusal(player);
            if (why.empty() && TakenByOther(table.tableGuid, guid))
                why = "The table is taken.";
            if (!why.empty())
            {
                Refuse(player, why);
                return;
            }
        }

        if (action == kActionBack)
        {
            ShowDecks(player, chair);
            return;
        }
        if ((action & kActionDeckFlag) != 0)
        {
            uint32 const deckId = action & kMaxDeckId;
            std::vector<std::pair<uint32_t, std::string>> decks;
            if (!HsMatchReadyDecks(player, decks, why))
            {
                Refuse(player, why);
                return;
            }
            auto const deck = std::find_if(decks.begin(), decks.end(),
                [deckId](std::pair<uint32_t, std::string> const& d) { return d.first == deckId; });
            HsLadderNpc npc;
            if (deck == decks.end() || !HsLadderNpcSetup(pick.npcEntry, npc))
            {
                Refuse(player, "That deck or that opponent is not available any more.");
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_picks[guid].deckId = deckId;
            }
            ShowConfirm(player, chair, npc.name, deck->second);
            return;
        }
        if (action == kActionConfirm)
        {
            HsLadderNpc npc;
            if (pick.deckId == 0 || !HsLadderIsOffered(AccountOf(player), pick.npcEntry) ||
                !HsLadderNpcSetup(pick.npcEntry, npc) || npc.blocked || !npc.enabled)
            {
                Refuse(player, "Your offers changed. Sit down again.");
                return;
            }
            if (!Launch(player, chair, table, npc, pick.deckId, false, why))
            {
                Refuse(player, why);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_picks.erase(guid);
            }
            player->PlayerTalkClass->CloseGossip();
            SysMessage(player, npc.name + " is on the way to the table.");
            return;
        }
        player->PlayerTalkClass->CloseGossip(); // anything unknown
    }

    // ---- the glow of the chair ----------------------------------------------------------------------------------------
    // Hearthstone.Ladder.ChairGlowSpellId (0: off). A standing player within kGlowNearYards turns the spell on: a trigger
    // creature (template kLadderChairGlowEntry, world migration 0057) stands on the chair and casts it. The chair cannot
    // cast it itself: the core shows a ground spell through a DynamicObject only when the spell is a persistent area aura,
    // and a plain visual or aura spell needs a unit to sit on. The creature is a copy of the Invisible Stalker without
    // its "invisible" flag: the core hides that flag's creatures from every player but game masters. Someone sitting on
    // the chair, or nobody near, removes the creature (and so the spell) again.
    constexpr float kGlowNearYards = 8.0f;               // a standing, living player this close turns the glow on
    constexpr float kGlowSeatYards = 2.5f;               // a sitting player this close is on the chair: the glow is off
    constexpr uint32 kGlowPollMs = 500;
    constexpr uint32 kGlowLifeMs = 120000;               // the trigger despawns by itself, so a lost one cannot stay

    class LadderChairGlowAI final : public GameObjectAI
    {
    public:
        LadderChairGlowAI(GameObject* chair, uint32 spellId) : GameObjectAI(chair), m_spellId(spellId) {}

        void UpdateAI(uint32 const diff) override
        {
            m_elapsed += diff;
            if (m_elapsed < kGlowPollMs)
                return;
            m_elapsed = 0;
            Map* map = me->GetMap();
            if (!map)
                return;

            bool occupied = false, standingNear = false;
            for (auto const& reference : map->GetPlayers())
            {
                Player* player = reference.getSource();
                if (!player || !player->IsInWorld())
                    continue;
                float const d = Distance2d(me->GetPositionX(), me->GetPositionY(), player->GetPositionX(), player->GetPositionY());
                if (player->IsSittingDown())
                {
                    occupied = occupied || d <= kGlowSeatYards;
                    continue;
                }
                standingNear = standingNear || (d <= kGlowNearYards && player->IsAlive());
            }

            if (occupied || !standingNear)
                Hide(map);
            else
                Show(map);
        }

    private:
        // The trigger is made on the first call and kept; the spell is cast again when it has run out (a persistent area
        // spell ends with its duration, an aura spell with its aura).
        void Show(Map* map)
        {
            Creature* trigger = m_trigger.IsEmpty() ? nullptr : map->GetCreature(m_trigger);
            if (!trigger || !trigger->IsAlive())
            {
                trigger = me->SummonCreature(HL::kLadderChairGlowEntry, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(),
                    me->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, kGlowLifeMs);
                if (!trigger)
                {
                    if (!m_reported)
                        sLog.outError("Hearthstone: the ladder chair glow creature %u could not be summoned (world migration 0057 missing?)",
                            HL::kLadderChairGlowEntry);
                    m_reported = true;
                    return;
                }
                m_trigger = trigger->GetObjectGuid();
            }
            if (trigger->GetDynObject(m_spellId) || trigger->HasAura(m_spellId))
                return;
            SpellCastResult const result = trigger->CastSpell(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), m_spellId, true);
            if (result != SPELL_CAST_OK && !m_reported)
                sLog.outError("Hearthstone: the ladder chair glow spell %u did not cast (result %d)", m_spellId, static_cast<int>(result));
            m_reported = m_reported || result != SPELL_CAST_OK;
        }

        void Hide(Map* map)
        {
            if (m_trigger.IsEmpty())
                return;
            if (Creature* trigger = map->GetCreature(m_trigger))
            {
                trigger->RemoveDynObject(m_spellId);
                trigger->ForcedDespawn();
            }
            m_trigger.Clear();
        }

        uint32 m_spellId;
        uint32 m_elapsed = 0;
        ObjectGuid m_trigger;    // the trigger creature that carries the spell (empty: none)
        bool m_reported = false; // a failure was logged (once per chair)
    };

    class LadderChairScript final : public GameObjectScript
    {
    public:
        LadderChairScript() : GameObjectScript(HL::kLadderChairScript) {}

        // The glow AI when the spell is configured. Checked by entry, not by the ladder table: the AI is made when the
        // chair is created, and the ladder world may not be loaded yet (or the ladder is off).
        GameObjectAI* GetAI(GameObject* chair) const override
        {
            uint32 const spellId = sHearthstoneConfig.GetLadder().chairGlowSpellId;
            if (!spellId || !chair || chair->GetEntry() != HL::kLadderChairEntry)
                return nullptr;
            return new LadderChairGlowAI(chair, spellId);
        }

        // The core calls this before its chair code and goes on to seat the player when it returns false.
        bool OnGossipHello(Player* player, GameObject* chair) override
        {
            HL::LadderTable table;
            if (!player || !chair || !HsLadderTableByChair(chair->GetGUIDLow(), table))
                return false; // not a ladder table's chair: an ordinary sit
            std::string const why = HsLadderChairWhy();
            if (!why.empty())
            {
                SysMessage(player, why);
                return false;
            }
            // The chairs open with the breadcrumb quest of the League Committee (a game master always).
            if (!HsLadderChairUnlocked(player))
            {
                SysMessage(player, "You do not know the rules of the league yet. Ask Ezra Chatterton or Ibelin Redmoore about the League "
                    "Committee.");
                return false;
            }
            // Returning true keeps the core from seating the player: a second seat-down (a near teleport) would end the
            // match of a player who already has an opponent coming, and a player at a taken table must not sit on its chair.
            if (HasClaim(player->GetGUIDLow()))
            {
                SysMessage(player, "You are already waiting for an opponent, or playing.");
                return true;
            }
            if (TakenByOther(table.tableGuid, player->GetGUIDLow()))
            {
                SysMessage(player, "The table is taken.");
                return true;
            }
            // A paused duel of the account comes first: resume it or give it up, no new offers.
            if (HsSessionHasLadderUi(player->GetGUIDLow()))
            {
                std::string payload, error;
                if (AddonView(player, payload, error))
                {
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_uiVisits[player->GetGUIDLow()] = chair->GetObjectGuid().GetRawValue();
                    }
                    player->PlayerTalkClass->CloseGossip();
                    HsPushLadder(player->GetGUIDLow(), "LADDER_OPEN", payload);
                }
                else
                    SysMessage(player, error);
                return false;
            }
            bool failed = false;
            if (!OpenPausedWindow(player, chair, failed) && !failed)
                OpenOffers(player, chair);
            return false;
        }

        bool OnGossipSelect(Player* player, GameObject* chair, uint32 sender, uint32 action) override
        {
            if (sender != kSender || !player || !chair)
                return false;
            Select(player, chair, action);
            return true;
        }
    };

    class LadderTablePlayerScript final : public PlayerScript
    {
    public:
        LadderTablePlayerScript()
            : PlayerScript("HearthstoneLadderTablePlayerScript", {
                  PLAYERHOOK_ON_PLAYER_JUST_DIED,
                  PLAYERHOOK_ON_BEFORE_TELEPORT,
                  PLAYERHOOK_ON_MAP_CHANGED,
                  PLAYERHOOK_ON_BEFORE_LOGOUT,
                  PLAYERHOOK_ON_LOGOUT,
                  PLAYERHOOK_ON_DELETE
              })
        {
        }

        void OnPlayerJustDied(Player* player) override
        {
            if (player)
                PostCancel(player->GetGUIDLow(), true);
        }

        // Sitting down on the chair is a near teleport too: only a teleport away from the chair cancels.
        void OnBeforeTeleport(Player* player, uint32 mapId, float x, float y, float /*z*/, float /*orientation*/) override
        {
            if (!player || g_busy.load(std::memory_order_relaxed) == 0)
                return;
            std::lock_guard<std::mutex> lock(g_mutex);
            g_picks.erase(player->GetGUIDLow());
            if (g_playerClaim.count(player->GetGUIDLow()))
            {
                TableEvent e;
                e.kind = TableEvent::Kind::Teleport;
                e.guid = player->GetGUIDLow();
                e.mapId = mapId;
                e.x = x;
                e.y = y;
                g_events.push_back(e);
            }
        }

        void OnMapChanged(Player* player) override
        {
            if (player)
                PostCancel(player->GetGUIDLow(), true);
        }

        void OnBeforeLogout(Player* player) override
        {
            if (player)
                PostCancel(player->GetGUIDLow(), true);
        }

        void OnLogout(Player* player) override
        {
            if (player)
                PostCancel(player->GetGUIDLow(), true);
        }

        void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
        {
            PostCancel(guid.GetCounter(), true);
        }
    };
}

// ---- API ---------------------------------------------------------------------------------------------------------

void AddHsLadderTableScripts()
{
    new LadderChairScript();
    new LadderTablePlayerScript();
}

void HsLadderSession::Finish(std::vector<std::string>& out)
{
    for (auto const& req : m_requests)
    {
        std::map<std::string, std::string> kv;
        uint32 npcEntry = 0, deckId = 0;
        bool const play = req.op == "LADDER_PLAY";
        if (!Hs::KvDecode(req.payload, kv) ||
            (play && (!kv.count("npc") || !ParseId(kv["npc"], npcEntry) ||
                !kv.count("deck") || !ParseId(kv["deck"], deckId))))
        {
            Hs::AppendError(out, req.reqId, Hs::ErrorCode::Malformed, "Invalid ladder request.");
            continue;
        }
        GameObject* chair = AddonChair(m_player);
        HL::LadderTable table;
        std::string why = HsLadderChairWhy();
        if (!chair || !HsLadderTableByChair(chair->GetGUIDLow(), table) || !HsLadderChairUnlocked(m_player))
            why = "Sit down at an unlocked ladder chair first.";
        if (why.empty() && req.op != "LADDER_GET" && TakenByOther(table.tableGuid, m_player->GetGUIDLow()))
            why = "The table is taken.";
        if (why.empty() && (req.op == "LADDER_RES" || req.op == "LADDER_GIVE"))
        {
            HL::DuelRow row;
            bool found = false;
            if (!kv.count("duel") || !HsLadderDuels().Find(AccountOf(m_player), found, row) ||
                !found || kv["duel"] != std::to_string(row.id) || row.state != HL::kDuelPaused ||
                HsLadderDuelQueued(row.id))
                why = "That paused duel is no longer available. Sit down again.";
        }
        if (why.empty() && play)
        {
            why = HsMatchStartRefusal(m_player);
            HsLadderNpc npc;
            if (why.empty() && (!HsLadderIsOffered(AccountOf(m_player), npcEntry) ||
                !HsLadderNpcSetup(npcEntry, npc) || npc.blocked || !npc.enabled))
                why = "Your offers changed. Sit down again.";
            std::vector<std::pair<uint32_t, std::string>> decks;
            if (why.empty())
            {
                if (!HsMatchReadyDecks(m_player, decks, why) && why.empty())
                    why = "Your decks could not be read.";
                if (why.empty() && std::none_of(decks.begin(), decks.end(),
                    [deckId](auto const& d) { return d.first == deckId; }))
                    why = "That deck is not ready to play.";
            }
            if (why.empty() && HasClaim(m_player->GetGUIDLow()))
                why = "Your opponent is already on the way.";
            if (why.empty() && Launch(m_player, chair, table, npc, deckId, false, why))
            {
                Hs::AppendReply(out, "LADDER", req.reqId, Hs::KvEncode({{"st", "waiting"}, {"name", npc.name}}));
                continue;
            }
        }
        if (why.empty() && req.op == "LADDER_RES")
        {
            ResumeDuel(m_player, chair, table);
            if (!HasClaim(m_player->GetGUIDLow()))
                why = "The duel could not be resumed. See the message in chat.";
        }
        if (why.empty() && req.op == "LADDER_GIVE")
        {
            // The addon's confirmation sends this operation; the stored duel is checked again server side.
            SurrenderDuel(m_player);
        }
        std::string payload;
        if (why.empty())
            AddonView(m_player, payload, why);
        if (!why.empty())
            Hs::AppendError(out, req.reqId, Hs::ErrorCode::Refused, why);
        else
            Hs::AppendReply(out, "LADDER", req.reqId, payload);
    }
    m_requests.clear();
}

void HsLadderTableUpdate(uint32_t diff)
{
    static uint32 visitMs = 0;
    visitMs += diff;
    if (visitMs >= 1000)
    {
        visitMs = 0;
        std::vector<uint32> visitors;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto const& v : g_uiVisits)
                visitors.push_back(v.first);
        }
        for (uint32 guid : visitors)
        {
            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
            if (player && AddonChair(player))
                continue;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_uiVisits.erase(guid);
            }
            HsPushLadder(guid, "LADDER_CLOSE", "");
        }
    }
    std::vector<Walk> fresh;
    std::vector<TableEvent> events;
    std::vector<Ended> ended;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_newWalks.empty() && g_events.empty() && g_ended.empty() && g_active.empty())
            return;
        fresh.swap(g_newWalks);
        events.swap(g_events);
        ended.swap(g_ended);
    }
    for (Walk& w : fresh)
        g_active.push_back(std::move(w));
    for (TableEvent const& e : events)
        HandleEvent(e);
    for (Ended const& e : ended)
        HandleEnded(e);
    for (Walk& w : g_active)
        if (!w.done)
            Step(w, diff);
    g_active.erase(std::remove_if(g_active.begin(), g_active.end(), [](Walk const& w) { return w.done; }), g_active.end());
}

void HsLadderTableShutdown()
{
    std::vector<Walk> all;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        all.swap(g_newWalks);
        g_events.clear();
        g_ended.clear();
        g_picks.clear();
        g_uiVisits.clear();
    }
    for (Walk& w : g_active)
        all.push_back(std::move(w));
    g_active.clear();
    for (Walk& w : all)
        Finish(w, FindCreature(w));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_matches.clear();
        g_tableClaim.clear();
        g_playerClaim.clear();
        g_walkers.clear();
        Recount();
    }
    if (!all.empty())
        sLog.outString("Hearthstone: ladder: %u walk-up(s) cleaned up at shutdown", static_cast<uint32>(all.size()));
}

void HsLadderMatchEnded(Hs::MatchEnd const& end)
{
    if (g_busy.load(std::memory_order_relaxed) == 0)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_matches.find(end.guid);
    if (it == g_matches.end() || (it->second.matchId != 0 && it->second.matchId != end.matchId))
        return;
    Ended e;
    e.guid = end.guid;
    e.match = it->second;
    e.res = end.res;
    e.why = end.why;
    e.endedMatchId = end.matchId;
    e.duelId = end.arena && HL::IsLadderRun(end.arenaRunId) ? HL::DuelIdOfRun(end.arenaRunId) : 0;
    g_ended.push_back(e);
    g_matches.erase(it);
    Recount();
}

void HsLadderMatchSuspended(uint32_t guid, char const* why)
{
    if (g_busy.load(std::memory_order_relaxed) == 0)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_matches.find(guid);
    if (it == g_matches.end())
        return;
    Ended e;
    e.guid = guid;
    e.match = it->second;
    e.res = "paused";
    e.why = why ? why : "";
    e.endedMatchId = it->second.matchId;
    g_ended.push_back(e);
    g_matches.erase(it);
    Recount();
}

bool HsLadderIsWalker(uint64_t creatureRaw)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_walkers.count(creatureRaw) != 0;
}

bool HsLadderTableBusy()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return !g_tableClaim.empty() || !g_matches.empty() || !g_newWalks.empty() || !g_ended.empty();
}

bool HsLadderPreviewWalk(Player* gm, HL::LadderTable const& table, uint32_t npcEntry, std::string& why)
{
    if (!gm || !gm->IsInWorld())
    {
        why = "you are not in the world.";
        return false;
    }
    if (gm->GetMapId() != table.mapId)
    {
        why = "the table in " + table.city + " is on another map (map " + std::to_string(table.mapId) + "): go there first.";
        return false;
    }
    if (!sObjectMgr.GetCreatureTemplate(npcEntry))
    {
        why = "creature " + std::to_string(npcEntry) + " has no creature template.";
        return false;
    }
    HsLadderNpc npc;
    npc.entry = npcEntry;
    if (!Launch(gm, nullptr, table, npc, 0, true, why))
    {
        if (!why.empty())
            why[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(why[0])));
        return false;
    }
    return true;
}
