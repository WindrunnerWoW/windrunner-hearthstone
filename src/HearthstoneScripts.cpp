/*
 * mod-hearthstone: game-facing glue. All protocol logic lives in src/protocol
 * (pure C++); this file only moves strings between the core and Hs::Session.
 *
 * Threading: CMSG_MESSAGECHAT and WorldScript::OnUpdate run on the world
 * thread. Gossip hooks run on map threads, so the session registry is
 * guarded by a mutex; frames are sent after the lock has been released.
 * Lock order: g_mutex, then the match service mutex (GAME_*, a leaf lock), or
 * the deck service mutex, then the collection service mutex (COLL_GET,
 * PACK_GET, PACK_OPEN, DECK_*), never the other way round. The match glue
 * (HsMatchGlue.cpp) owns the match hooks; COLL_CHANGED / PACK_CHANGED / DECK_CHANGED pushes of GM commands are
 * sent after the mutation returned.
 */

#include "ScriptObjects.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsCollectionStore.h"
#include "HsDataset.h"
#include "HsDeckStore.h"
#include "HsEconomyGlue.h"
#include "HsHeroGlue.h"
#include "HsHeroPortraitStore.h"
#include "HsCardTrader.h"
#include "HsIntroGlue.h"
#include "HsClassQuestGlue.h"
#include "HsNaxxQuestGlue.h"
#include "HsWeeklyQuestGlue.h"
#include "HsAllianceMageQuest.h"
#include "HsHunterQuest.h"
#include "HsHordeShamanQuest.h"
#include "HsHordeWarriorQuest.h"
#include "HsHordeMageQuest.h"
#include "HsAllianceShamanQuest.h"
#include "HsHordePaladinQuest.h"
#include "HsHordeDruidQuest.h"
#include "HsHordePriestQuest.h"
#include "HsAlliancePriestQuest.h"
#include "HsQuestPickup.h"
#include "HsAllianceRogueQuest.h"
#include "HsAllianceDruidQuest.h"
#include "HsArenaGlue.h"
#include "HsArenaHost.h"
#include "HsBgGlue.h"
#include "HsLadderGlue.h"
#include "HsLadderTable.h"
#include "HsMatchGlue.h"
#include "HsMatchStore.h"
#include "protocol/HsSession.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static_assert(SEC_MODERATOR == Hs::kDebugMinSecurity, "debug security threshold must match SEC_MODERATOR");

namespace
{
    std::mutex g_mutex;
    std::unordered_map<uint32, Hs::Session> g_sessions;
    // account id -> guid of its character in the world (for COLL_CHANGED pushes)
    std::unordered_map<uint32, uint32> g_onlineCharacter;

    uint64_t NowMs()
    {
        // One clock for the sessions and the match service (AI pacing), see HsMatchGlue.cpp.
        return HsNowMs();
    }

    Hs::ServerInfo MakeServerInfo()
    {
        Hs::ServerInfo info;
        info.moduleVersion = Hs::kModuleVersion;
        info.datasetId = Hs::kDatasetId;
        info.datasetHash = Hs::kDatasetHash;
        return info;
    }

    // Our own control traffic: LANG_ADDON and a "WRHS\t..." or "WRBG\t..." prefix. It is
    // always consumed, whatever chat type it was sent on, so it is never relayed
    // to a guild, party or channel. WRBG is the port of the Battlegrounds.
    bool IsOurs(uint32 lang, std::string const& msg)
    {
        if (lang != LANG_ADDON)
            return false;
        auto const hasPrefix = [&msg](char const* prefix, std::size_t len)
        {
            return msg.size() >= len + 1 && msg.compare(0, len, prefix) == 0 && msg[len] == '\t';
        };
        return hasPrefix(Hs::kPrefix, Hs::kPrefixLen) ||
            hasPrefix(HsBgPrefix(), std::char_traits<char>::length(HsBgPrefix()));
    }

    void SendFrames(Player* player, std::vector<std::string> const& frames)
    {
        for (std::string const& frame : frames)
            player->SendAddonMessage(Hs::kPrefix, frame);
    }

    // The world DB (data/sql/world migration) carries the same card dataset as this binary and the
    // addon. Report a missing or different one at startup; never fail the startup for it.
    void CheckDatasetTable()
    {
        std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_dataset'"));
        if (!table)
        {
            sLog.outError("Hearthstone: table hearthstone_dataset is missing in the world database "
                "(migration 0001_hearthstone_card_template.sql not applied? check Database.AutoUpdate.*)");
            return;
        }

        std::unique_ptr<QueryResult> row(WorldDatabase.PQuery(
            "SELECT `content_hash`, `card_count` FROM `hearthstone_dataset` WHERE `dataset_id` = '%s'",
            Hs::kDatasetId));
        if (!row)
        {
            sLog.outError("Hearthstone: the world database has no row for dataset %s "
                "(regenerate the SQL with tools/carddata/generate_all.py and restart)", Hs::kDatasetId);
            return;
        }

        Field* fields = row->Fetch();
        std::string const dbHash = fields[0].GetCppString();
        uint32 const dbCount = fields[1].GetUInt32();
        if (dbHash != Hs::kDatasetHashFull || dbCount != Hs::kDatasetCardCount)
        {
            sLog.outError("Hearthstone: WARNING world database dataset %s differs from this binary "
                "(db hash %.16s, %u cards; binary hash %s, %u cards). Card data is out of sync.",
                Hs::kDatasetId, dbHash.c_str(), dbCount, Hs::kDatasetHash, Hs::kDatasetCardCount);
            return;
        }
        sLog.outString("Hearthstone: world database dataset %s matches (%u cards)", Hs::kDatasetId, dbCount);
    }

    void DropSession(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessions.erase(guidLow);
    }

    std::string DescribePackCards(Hs::PackOpening const& o)
    {
        std::shared_ptr<Hs::CardCatalog const> const catalog = HsCollections().Catalog();
        std::string text;
        for (std::size_t i = 0; i < Hs::kPackSize; ++i)
        {
            if (i)
                text += ", ";
            text += o.cards[i];
            Hs::CardRule const* rule = catalog ? catalog->Find(o.cards[i]) : nullptr;
            Hs::PackRarity bucket;
            if (rule && Hs::PackRarityOf(rule->rarity, bucket))
                text += std::string(" (") + Hs::PackRarityName(bucket) + (o.isNew[i] ? ", new)" : ")");
            else if (o.isNew[i])
                text += " (new)";
        }
        return text;
    }

    std::string DescribeDeckCards(Hs::DeckCards const& cards)
    {
        std::string text;
        for (auto const& entry : cards)
        {
            if (!text.empty())
                text += ", ";
            text += entry.first + (entry.second == 1 ? std::string() : " x" + std::to_string(entry.second));
        }
        return text.empty() ? std::string("no cards") : text;
    }

    // COLL_GET, PACK_GET, PACK_OPEN and DECK_* of one player: the player's
    // account, and the character's starting class for the deck states.
    class AccountCollectionHandler : public Hs::CollectionHandler
    {
    public:
        AccountCollectionHandler(Player* player, uint32 accountId, uint32 guidLow, std::string name, std::string startClass)
            : m_player(player), m_accountId(accountId), m_guidLow(guidLow), m_name(std::move(name)),
              m_startClass(std::move(startClass)) {}

        // Economy.
        bool GetWallet(Hs::WalletView& out) override
        {
            if (m_accountId == 0 || !HsCollections().GetWallet(m_accountId, out.wallet))
                return false;
            out.coins = HsCoinCount(m_player);
            out.values = HsCollections().GetDustValues();
            out.packPriceCoins = sHearthstoneConfig.GetEconomy().packPriceCoins;
            out.craftOpen = HsCraftAccess(m_player, out.craftNpc);
            return true;
        }

        // Hero portraits.
        Hs::Heroes::HeroResult GetHeroes() override
        {
            HsHeroDbStore store;
            return m_accountId != 0 ? Hs::Heroes::GetHeroes(store, m_accountId) : Hs::Heroes::HeroResult();
        }

        Hs::Heroes::HeroResult SelectHero(std::string const& heroClass, std::string const& id) override
        {
            HsHeroDbStore store;
            if (m_accountId == 0)
                return Hs::Heroes::HeroResult();
            Hs::Heroes::HeroResult const r = Hs::Heroes::SelectPortrait(store, m_accountId, heroClass, id);
            if (r.status == Hs::Heroes::HeroStatus::Ok)
                sLog.outString("Hearthstone: %s (account %u) chose the %s portrait %s", m_name.c_str(), m_accountId,
                    heroClass.c_str(), id.c_str());
            return r;
        }

        Hs::Heroes::HeroResult UnlockHero(std::string const& key, std::string const& id) override
        {
            HsHeroDbStore store;
            if (m_accountId == 0)
                return Hs::Heroes::HeroResult();
            Hs::Heroes::HeroResult const r = Hs::Heroes::UnlockPortrait(store, m_accountId, key, id);
            if (r.status == Hs::Heroes::HeroStatus::Ok)
                sLog.outString("Hearthstone: %s (account %u) unlocked the portrait %s with a Legend #1 credit",
                    m_name.c_str(), m_accountId, id.c_str());
            else if (r.status == Hs::Heroes::HeroStatus::Replay)
                sLog.outString("Hearthstone: %s (account %u) repeated HERO_UNLOCK key %s: replayed",
                    m_name.c_str(), m_accountId, key.c_str());
            return r;
        }

        Hs::CraftResult CraftCard(std::string const& key, std::string const& card) override
        {
            return CraftOrScrap(Hs::CraftKind::Make, key, card, 1);
        }

        Hs::CraftResult ScrapCard(std::string const& key, std::string const& card, uint32_t count) override
        {
            return CraftOrScrap(Hs::CraftKind::Scrap, key, card, count);
        }

        Hs::CraftResult ScrapExtras(std::string const& key) override
        {
            return CraftOrScrap(Hs::CraftKind::Extras, key, Hs::kExtrasCardId, 0);
        }

        bool GetCollection(Hs::Collection& out) override
        {
            // May read the database (first request after login) while the
            // caller holds g_mutex; everything involved runs on the world thread.
            return m_accountId != 0 && HsCollections().Get(m_accountId, out);
        }

        bool GetPacks(Hs::PackView& out) override
        {
            return m_accountId != 0 && HsCollections().GetPacks(m_accountId, out);
        }

        Hs::PackOpenResult OpenPack(std::string const& key) override
        {
            if (m_accountId == 0)
                return Hs::PackOpenResult();
            Hs::PackOpenResult const r = HsCollections().OpenPack(m_accountId, key, "char:" + std::to_string(m_guidLow));
            switch (r.status)
            {
                case Hs::PackStatus::Ok:
                    sLog.outString("Hearthstone: %s (account %u) opened pack #%u: %s; %u left, collection version %u, "
                        "packs without a Legendary %u -> %u%s%s", m_name.c_str(), m_accountId, r.opening.seq,
                        DescribePackCards(r.opening).c_str(), r.stockNow, r.opening.collectionVersion,
                        r.opening.sinceLegendaryBefore, r.opening.sinceLegendaryAfter,
                        r.roll.pityApplied ? ", Legendary guarantee used" : "",
                        r.roll.rareUpgraded ? ", Rare guarantee used" : "");
                    break;
                case Hs::PackStatus::Replay:
                    sLog.outString("Hearthstone: %s (account %u) repeated PACK_OPEN key %s: replayed pack #%u",
                        m_name.c_str(), m_accountId, key.c_str(), r.opening.seq);
                    break;
                case Hs::PackStatus::Refused:
                    sLog.outError("Hearthstone: %s (account %u) could not open a pack: %s (%s); nothing was changed",
                        m_name.c_str(), m_accountId, Hs::OwnershipErrorText(r.error), r.card.c_str());
                    break;
                case Hs::PackStatus::Unavailable:
                case Hs::PackStatus::CommitFailed:
                    sLog.outError("Hearthstone: %s (account %u) could not open a pack: %s", m_name.c_str(),
                        m_accountId, Hs::PackStatusText(r.status));
                    break;
                default:
                    break;
            }
            return r;
        }

        bool ListDecks(Hs::DeckList& out) override
        {
            return m_accountId != 0 && HsDecks().List(m_accountId, m_startClass, out);
        }

        Hs::DeckSaveResult SaveDeck(Hs::DeckSaveRequest const& request) override
        {
            if (m_accountId == 0)
                return Hs::DeckSaveResult();
            // The client chooses key, deck, base revision, class, name and cards;
            // everything else is the server's.
            Hs::DeckSaveRequest req = request;
            req.origin = Hs::kDeckOriginPlayer;
            req.reason = "deck_save";
            req.source = "char:" + std::to_string(m_guidLow);
            req.bypassCap = false;
            Hs::DeckSaveResult const r = HsDecks().SaveDeck(m_accountId, req, m_startClass);
            switch (r.status)
            {
                case Hs::DeckStatus::Ok:
                    sLog.outString("Hearthstone: %s (account %u) %s deck #%u (%s, \"%s\", %u cards, rev %u, %s): %s",
                        m_name.c_str(), m_accountId, r.created ? "created" : "saved", r.id, r.deck.deckClass.c_str(),
                        r.deck.name.c_str(), r.deck.CardCount(), r.rev, r.state.c_str(),
                        DescribeDeckCards(r.deck.cards).c_str());
                    break;
                case Hs::DeckStatus::Replay:
                    sLog.outString("Hearthstone: %s (account %u) repeated DECK_SAVE key %s: replayed deck #%u rev %u (%s)",
                        m_name.c_str(), m_accountId, req.key.c_str(), r.id, r.rev, r.state.c_str());
                    break;
                case Hs::DeckStatus::Refused:
                    sLog.outString("Hearthstone: %s (account %u) could not save deck #%u: %s (%s); nothing was changed",
                        m_name.c_str(), m_accountId, req.id, Hs::DeckWhyText(r.why), Hs::DeckWhyCode(r.why));
                    break;
                case Hs::DeckStatus::Unavailable:
                case Hs::DeckStatus::CommitFailed:
                    sLog.outError("Hearthstone: %s (account %u) could not save deck #%u: %s", m_name.c_str(),
                        m_accountId, req.id, Hs::DeckStatusText(r.status));
                    break;
                case Hs::DeckStatus::BadKey:
                    break;
            }
            return r;
        }

        Hs::DeckDeleteResult DeleteDeck(Hs::DeckDeleteRequest const& request) override
        {
            if (m_accountId == 0)
                return Hs::DeckDeleteResult();
            Hs::DeckDeleteRequest req = request;
            req.anyRev = false;
            req.reason = "deck_delete";
            req.source = "char:" + std::to_string(m_guidLow);
            Hs::DeckDeleteResult const r = HsDecks().DeleteDeck(m_accountId, req);
            switch (r.status)
            {
                case Hs::DeckStatus::Ok:
                    sLog.outString("Hearthstone: %s (account %u) deleted deck #%u (%s, \"%s\", rev %u)",
                        m_name.c_str(), m_accountId, req.id, r.deck.deckClass.c_str(), r.deck.name.c_str(), r.deck.rev);
                    break;
                case Hs::DeckStatus::Replay:
                    sLog.outString("Hearthstone: %s (account %u) repeated DECK_DELETE key %s of deck #%u: replayed",
                        m_name.c_str(), m_accountId, req.key.c_str(), req.id);
                    break;
                case Hs::DeckStatus::Refused:
                    sLog.outString("Hearthstone: %s (account %u) could not delete deck #%u: %s (%s); nothing was changed",
                        m_name.c_str(), m_accountId, req.id, Hs::DeckWhyText(r.why), Hs::DeckWhyCode(r.why));
                    break;
                case Hs::DeckStatus::Unavailable:
                case Hs::DeckStatus::CommitFailed:
                    sLog.outError("Hearthstone: %s (account %u) could not delete deck #%u: %s", m_name.c_str(),
                        m_accountId, req.id, Hs::DeckStatusText(r.status));
                    break;
                case Hs::DeckStatus::BadKey:
                    break;
            }
            return r;
        }

    private:
        Hs::CraftResult CraftOrScrap(Hs::CraftKind kind, std::string const& key, std::string const& card, uint32_t count)
        {
            Hs::CraftResult r;
            if (m_accountId == 0)
                return r;
            // Only while a crafting NPC is open and the character stays near it.
            std::string npc;
            if (!HsCraftAccess(m_player, npc))
            {
                r.status = Hs::CraftStatus::Refused;
                r.why = Hs::CraftWhy::Npc;
                return r;
            }
            std::string const source = "char:" + std::to_string(m_guidLow);
            if (kind == Hs::CraftKind::Make)
                r = HsCollections().Craft(m_accountId, key, card, source);
            else if (kind == Hs::CraftKind::Extras)
                r = HsCollections().ScrapExtras(m_accountId, key, source);
            else
                r = HsCollections().Scrap(m_accountId, key, card, count, source);
            char const* const what = Hs::CraftKindCode(kind);
            switch (r.status)
            {
                case Hs::CraftStatus::Ok:
                    if (kind == Hs::CraftKind::Extras)
                    {
                        sLog.outString("Hearthstone: %s (account %u) scrapped %u extra cards at %s: dust %d -> %u, "
                            "collection version %u", m_name.c_str(), m_accountId, r.record.count, npc.c_str(),
                            static_cast<int>(r.record.dustAfter) - r.record.dustDelta, r.record.dustAfter,
                            r.record.collectionVersion);
                        break;
                    }
                    sLog.outString("Hearthstone: %s (account %u) %s %ux %s at %s: dust %d -> %u, now %u/%u "
                        "(protected/unprotected), collection version %u", m_name.c_str(), m_accountId,
                        kind == Hs::CraftKind::Make ? "crafted" : "scrapped", r.record.count, r.record.cardId.c_str(),
                        npc.c_str(), static_cast<int>(r.record.dustAfter) - r.record.dustDelta, r.record.dustAfter,
                        r.record.cardAfter.protectedQty, r.record.cardAfter.unprotectedQty, r.record.collectionVersion);
                    break;
                case Hs::CraftStatus::Replay:
                    sLog.outString("Hearthstone: %s (account %u) repeated %s key %s: replayed", m_name.c_str(),
                        m_accountId, what, key.c_str());
                    break;
                case Hs::CraftStatus::Refused:
                    sLog.outString("Hearthstone: %s (account %u) could not %s %s: %s", m_name.c_str(), m_accountId, what,
                        card.c_str(), Hs::CraftWhyCode(r.why));
                    break;
                case Hs::CraftStatus::Unavailable:
                case Hs::CraftStatus::CommitFailed:
                    sLog.outError("Hearthstone: %s (account %u) could not %s %s: %s", m_name.c_str(), m_accountId, what,
                        card.c_str(), Hs::CraftStatusText(r.status));
                    break;
                case Hs::CraftStatus::BadKey:
                    break;
            }
            return r;
        }

        Player* m_player;
        uint32 m_accountId;
        uint32 m_guidLow;
        std::string m_name;
        std::string m_startClass; // "" if the character's class has no Hearthstone class
    };

    // Sends the frames build appends to the account's character, if it is in
    // the world and handshaken. Call without holding any lock.
    void PushToAccount(uint32 accountId, std::function<void(std::vector<std::string>&)> const& build)
    {
        if (!sHearthstoneConfig.Get().enable)
            return;
        uint32 guidLow = 0;
        std::vector<std::string> frames;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto const online = g_onlineCharacter.find(accountId);
            if (online == g_onlineCharacter.end())
                return;
            guidLow = online->second;
            auto const session = g_sessions.find(guidLow);
            if (session == g_sessions.end() || !session->second.Handshaken())
                return;
            build(frames);
        }
        if (Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow)))
            SendFrames(player, frames);
    }

    // The account's collection changed (COLL_CHANGED).
    void PushCollectionChanged(uint32 accountId, uint32 version)
    {
        PushToAccount(accountId, [version](std::vector<std::string>& out) { Hs::AppendCollChanged(out, version); });
    }

    // The account now holds stock unopened packs (PACK_CHANGED).
    void PushPackChanged(uint32 accountId, uint32 stock)
    {
        PushToAccount(accountId, [stock](std::vector<std::string>& out) { Hs::AppendPackChanged(out, stock); });
    }

    // Decks ("gm") or class access ("class") changed outside the client (DECK_CHANGED).
    void PushDeckChanged(uint32 accountId, char const* why)
    {
        std::string const w = why;
        PushToAccount(accountId, [&w](std::vector<std::string>& out) { Hs::AppendDeckChanged(out, w); });
    }

    void OnCharacterEnter(Player* player)
    {
        uint32 const guidLow = player->GetGUIDLow();
        uint32 const accountId = player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_sessions.erase(guidLow); // stale state from a previous login
            if (accountId)
                g_onlineCharacter[accountId] = guidLow;
        }
        if (accountId)
        {
            HsIntroForget(accountId);
            HsArenaHostForget(accountId);
            HsCollections().SetOnline(accountId); // also drops a stale cache entry
            HsDecks().SetOnline(accountId);
        }
    }

    void OnCharacterLeave(Player* player)
    {
        uint32 const guidLow = player->GetGUIDLow();
        uint32 const accountId = player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_sessions.erase(guidLow);
            auto const online = g_onlineCharacter.find(accountId);
            if (online != g_onlineCharacter.end() && online->second == guidLow)
                g_onlineCharacter.erase(online);
        }
        if (accountId)
        {
            HsCollections().SetOffline(accountId);
            HsDecks().SetOffline(accountId);
        }
        HsEconomyLogout(guidLow);
        // A Battlegrounds game does not survive a logout (no resume).
        HsBgOnLogout(guidLow);
    }

    // ---- .hs coll ---------------------------------------------------------

    std::vector<std::string> SplitArgs(char const* args)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char const* p = args ? args : ""; *p; ++p)
        {
            if (*p == ' ' || *p == '\t')
            {
                if (!cur.empty())
                    out.push_back(cur);
                cur.clear();
            }
            else
                cur.push_back(*p);
        }
        if (!cur.empty())
            out.push_back(cur);
        return out;
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Decimal digits only, at most 9 (range checks are the service's job).
    bool ParseNumber(std::string const& s, uint32& out)
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

    struct CollTarget
    {
        uint32 accountId = 0;
        std::string label;      // for messages and logs
        bool character = false; // named by a character (not by #account)
        std::string startClass; // the character's Hearthstone starting class ("" for #account)
    };

    // "<character name>" (online or offline, via the characters table) or "#<account id>".
    bool ResolveCollTarget(ChatHandler* handler, std::string const& arg, CollTarget& out)
    {
        if (!arg.empty() && arg[0] == '#')
        {
            uint32 id = 0;
            std::string name;
            if (!ParseNumber(arg.substr(1), id) || id == 0)
            {
                handler->SendSysMessage("Hearthstone: '" + arg + "' is not an account id (#<number>).");
                return false;
            }
            if (!sAccountMgr.GetName(id, name))
            {
                handler->SendSysMessage("Hearthstone: account #" + std::to_string(id) + " does not exist.");
                return false;
            }
            out.accountId = id;
            out.label = "account #" + std::to_string(id) + " (" + name + ")";
            return true;
        }

        std::string name = arg;
        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage("Hearthstone: '" + arg + "' is not a valid character name.");
            return false;
        }
        std::string escaped = name;
        CharacterDatabase.escape_string(escaped);
        std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
            "SELECT `account`, `name`, `class` FROM `characters` WHERE `name` = '%s'", escaped.c_str()));
        if (!row)
        {
            handler->SendSysMessage("Hearthstone: no character named " + name + ".");
            return false;
        }
        Field* fields = row->Fetch();
        out.accountId = fields[0].GetUInt32();
        out.label = fields[1].GetCppString() + " (account #" + std::to_string(out.accountId) + ")";
        out.character = true;
        out.startClass = Hs::DeckClassOfWowClass(fields[2].GetUInt32());
        return out.accountId != 0;
    }

    std::string DescribeCount(Hs::CardCount const& c)
    {
        return std::to_string(c.Total()) + (c.Total() == 1 ? " copy (" : " copies (") +
            std::to_string(c.protectedQty) + " protected, " + std::to_string(c.unprotectedQty) + " unprotected)";
    }

    std::string GmSource(ChatHandler* handler)
    {
        WorldSession* session = handler->GetSession();
        return session ? "gm:" + std::to_string(session->GetAccountId()) : std::string("gm:console");
    }

    std::string GmName(ChatHandler* handler)
    {
        WorldSession* session = handler->GetSession();
        if (!session)
            return "console";
        char const* name = session->GetPlayerName();
        return std::string(name ? name : "?") + " (account #" + std::to_string(session->GetAccountId()) + ")";
    }

    void SendCollUsage(ChatHandler* handler)
    {
        handler->SendSysMessage("Usage: .hs coll show <player|#accountId> [cardId]");
        handler->SendSysMessage("       .hs coll grant <player|#accountId> <cardId> [count=1] [unprotected|protected|reward]");
        handler->SendSysMessage("       .hs coll remove <player|#accountId> <cardId> [count|all]");
    }

    // Reports the outcome of a grant or remove to the GM and the log, and pushes
    // COLL_CHANGED after a commit.
    void FinishMutation(ChatHandler* handler, char const* verb, CollTarget const& target,
                        std::string const& what, Hs::MutationResult const& r)
    {
        Hs::Mutation const& m = r.mutation;
        switch (r.status)
        {
            case Hs::MutationStatus::Ok:
                handler->SendSysMessage("Hearthstone: " + std::string(verb) + " " + what + " for " + target.label +
                    ": now " + DescribeCount(m.after) + ", collection version " + std::to_string(m.versionAfter) + ".");
                sLog.outString("Hearthstone: GM %s %s %s for account %u: %s %u/%u -> %u/%u (protected/unprotected), version %u",
                    GmName(handler).c_str(), verb, what.c_str(), target.accountId, m.cardId.c_str(),
                    m.before.protectedQty, m.before.unprotectedQty, m.after.protectedQty, m.after.unprotectedQty,
                    m.versionAfter);
                PushCollectionChanged(target.accountId, m.versionAfter);
                return;
            case Hs::MutationStatus::Rejected:
                handler->SendSysMessage("Hearthstone: rejected, " + std::string(Hs::OwnershipErrorText(r.error)) +
                    (m.cardId.empty() ? std::string() : " (" + m.cardId + ": " + DescribeCount(m.before) + ")") +
                    ". Nothing was changed.");
                sLog.outString("Hearthstone: GM %s %s %s for account %u: rejected (%s)", GmName(handler).c_str(),
                    verb, what.c_str(), target.accountId, Hs::OwnershipErrorText(r.error));
                return;
            case Hs::MutationStatus::Unavailable:
                handler->SendSysMessage("Hearthstone: the collection is unavailable (card data or character database "
                    "not ready, see the server log). Nothing was changed.");
                sLog.outError("Hearthstone: GM %s %s %s for account %u: failed, collection unavailable",
                    GmName(handler).c_str(), verb, what.c_str(), target.accountId);
                return;
            case Hs::MutationStatus::CommitFailed:
                handler->SendSysMessage("Hearthstone: the database write failed. Nothing was changed.");
                sLog.outError("Hearthstone: GM %s %s %s for account %u: failed, commit failed",
                    GmName(handler).c_str(), verb, what.c_str(), target.accountId);
                return;
        }
    }

    // ---- .hs pack ---------------------------------------------------------

    void SendPackUsage(ChatHandler* handler)
    {
        handler->SendSysMessage("Usage: .hs pack show <player|#accountId>");
        handler->SendSysMessage("       .hs pack grant <player|#accountId> [count=1]");
        handler->SendSysMessage("       .hs pack remove <player|#accountId> [count|all]");
    }

    void FinishPackStock(ChatHandler* handler, char const* verb, CollTarget const& target, std::string const& what,
                         Hs::PackStockResult const& r)
    {
        if (r.status == Hs::PackStatus::Ok)
        {
            handler->SendSysMessage("Hearthstone: " + std::string(verb) + " " + what + " for " + target.label +
                ": now " + std::to_string(r.after.stock) + " unopened.");
            sLog.outString("Hearthstone: GM %s %s %s for account %u: stock %u -> %u", GmName(handler).c_str(), verb,
                what.c_str(), target.accountId, r.before.stock, r.after.stock);
            PushPackChanged(target.accountId, r.after.stock);
            return;
        }
        std::string const why = Hs::PackStatusText(r.status);
        bool const failed = r.status == Hs::PackStatus::Unavailable || r.status == Hs::PackStatus::CommitFailed;
        // BadCount and Unavailable are answered before the stock is read: no count to show.
        bool const stockKnown = r.status != Hs::PackStatus::BadCount && r.status != Hs::PackStatus::Unavailable;
        handler->SendSysMessage("Hearthstone: " + std::string(failed ? "failed, " : "rejected, ") + why +
            (stockKnown ? " (" + std::to_string(r.before.stock) + " unopened)" : std::string()) + ". Nothing was changed.");
        if (failed)
            sLog.outError("Hearthstone: GM %s %s %s for account %u: failed, %s", GmName(handler).c_str(), verb,
                what.c_str(), target.accountId, why.c_str());
        else
            sLog.outString("Hearthstone: GM %s %s %s for account %u: rejected (%s)", GmName(handler).c_str(), verb,
                what.c_str(), target.accountId, why.c_str());
    }

    // .hs pack ... (args[0] == "pack"). The caller already checked security >= MODERATOR.
    void HandlePackCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
    {
        std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
        bool const mutating = sub == "grant" || sub == "remove";
        if ((sub != "show" && !mutating) || args.size() < 3)
        {
            SendPackUsage(handler);
            return;
        }
        if (mutating && security < static_cast<int>(SEC_DEVELOPER))
        {
            handler->SendSysMessage("Hearthstone: .hs pack " + sub + " needs security level 3 (developer) or higher.");
            return;
        }
        if (!HsCollections().Catalog())
        {
            handler->SendSysMessage("Hearthstone: packs are UNAVAILABLE: the card data of dataset " +
                std::string(Hs::kDatasetId) + " could not be loaded at startup (see the server log).");
            return;
        }

        CollTarget target;
        if (!ResolveCollTarget(handler, args[2], target))
            return;

        if (sub == "show")
        {
            if (args.size() > 3)
            {
                SendPackUsage(handler);
                return;
            }
            Hs::PackView view;
            if (!HsCollections().GetPacks(target.accountId, view))
            {
                handler->SendSysMessage("Hearthstone: the packs of " + target.label + " could not be read (see the server log).");
                return;
            }
            Hs::PackOdds const odds = HsCollections().GetPackOdds();
            std::string const state = HsCollections().IsOnline(target.accountId) ? "online" : "offline";
            handler->SendSysMessage("Hearthstone: packs of " + target.label + " (" + state + "): " +
                std::to_string(view.state.stock) + " unopened, " + std::to_string(view.state.opened) + " opened, " +
                std::to_string(view.state.sinceLegendary) + " in a row without a Legendary" +
                (odds.legendaryPity ? " (guaranteed in pack " + std::to_string(odds.legendaryPity) + ")" : std::string()) + ".");
            for (Hs::PackOpening const& o : view.recent)
                handler->SendSysMessage("  #" + std::to_string(o.seq) + ": " + DescribePackCards(o) +
                    (o.pityApplied ? " [Legendary guarantee]" : ""));
            return;
        }

        if (sub == "grant")
        {
            uint32 count = 1;
            if (args.size() > 3 && !ParseNumber(args[3], count))
            {
                handler->SendSysMessage("Hearthstone: '" + args[3] + "' is not a count (1.." +
                    std::to_string(Hs::kMaxPacksPerOp) + ").");
                return;
            }
            if (args.size() > 4)
            {
                SendPackUsage(handler);
                return;
            }
            Hs::PackStockResult const r = HsCollections().GrantPacks(target.accountId, count, "gm_pack_grant",
                GmSource(handler));
            FinishPackStock(handler, "granted", target, std::to_string(count) + " pack" + (count == 1 ? "" : "s"), r);
            return;
        }

        // remove
        uint32 count = 1;
        bool all = false;
        if (args.size() > 3)
        {
            if (Lower(args[3]) == "all")
                all = true;
            else if (!ParseNumber(args[3], count))
            {
                handler->SendSysMessage("Hearthstone: '" + args[3] + "' is neither a count (1.." +
                    std::to_string(Hs::kMaxPacksPerOp) + ") nor 'all'.");
                return;
            }
        }
        if (args.size() > 4)
        {
            SendPackUsage(handler);
            return;
        }
        Hs::PackStockResult const r = HsCollections().RemovePacks(target.accountId, count, all, "gm_pack_remove",
            GmSource(handler));
        FinishPackStock(handler, "removed", target, all ? std::string("all packs")
            : std::to_string(count) + " pack" + (count == 1 ? "" : "s"), r);
    }

    // .hs coll ... (args[0] == "coll"). The caller already checked security >= MODERATOR.
    void HandleCollCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
    {
        std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
        bool const mutating = sub == "grant" || sub == "remove";
        if ((sub != "show" && !mutating) || args.size() < 3)
        {
            SendCollUsage(handler);
            return;
        }
        if (mutating && security < static_cast<int>(SEC_DEVELOPER))
        {
            handler->SendSysMessage("Hearthstone: .hs coll " + sub + " needs security level 3 (developer) or higher.");
            return;
        }
        std::shared_ptr<Hs::CardCatalog const> const catalog = HsCollections().Catalog();
        if (!catalog)
        {
            handler->SendSysMessage("Hearthstone: card collections are UNAVAILABLE: the card data of dataset " +
                std::string(Hs::kDatasetId) + " could not be loaded at startup (see the server log).");
            return;
        }

        CollTarget target;
        if (!ResolveCollTarget(handler, args[2], target))
            return;

        if (sub == "show")
        {
            std::string cardId;
            if (args.size() > 3)
            {
                cardId = catalog->Resolve(args[3]);
                if (cardId.empty())
                {
                    handler->SendSysMessage("Hearthstone: unknown card id " + args[3] + ".");
                    return;
                }
            }
            Hs::Collection collection;
            if (!HsCollections().Get(target.accountId, collection))
            {
                handler->SendSysMessage("Hearthstone: the collection of " + target.label +
                    " could not be read (see the server log).");
                return;
            }
            std::string const state = HsCollections().IsOnline(target.accountId) ? "online" : "offline";
            if (!cardId.empty())
            {
                handler->SendSysMessage("Hearthstone: " + cardId + " for " + target.label + " (" + state + "): " +
                    DescribeCount(collection.Get(cardId)) + ", collection version " +
                    std::to_string(collection.version) + ".");
                return;
            }
            Hs::Collection::Summary const s = collection.Summarize();
            handler->SendSysMessage("Hearthstone: collection of " + target.label + " (" + state + "): " +
                std::to_string(s.distinct) + " distinct cards, " + std::to_string(s.copies) + " copies (" +
                std::to_string(s.protectedCopies) + " protected), version " + std::to_string(collection.version) + ".");
            return;
        }

        if (args.size() < 4)
        {
            SendCollUsage(handler);
            return;
        }
        std::string const& cardArg = args[3];

        if (sub == "grant")
        {
            uint32 count = 1;
            Hs::ProtectionMode mode = Hs::ProtectionMode::Unprotected;
            std::size_t next = 4;
            if (args.size() > next && ParseNumber(args[next], count))
                ++next;
            if (args.size() > next)
            {
                if (!Hs::ParseProtectionMode(args[next], mode))
                {
                    handler->SendSysMessage("Hearthstone: '" + args[next] + "' is neither a count (1..99) nor a mode "
                        "(unprotected, protected, reward).");
                    return;
                }
                ++next;
            }
            if (args.size() > next)
            {
                SendCollUsage(handler);
                return;
            }
            Hs::MutationResult const r = HsCollections().Grant(target.accountId, cardArg, count, Hs::GrantSource::Gm,
                mode, "gm_grant", GmSource(handler));
            std::string const id = r.mutation.cardId.empty() ? cardArg : r.mutation.cardId;
            FinishMutation(handler, "granted", target, std::to_string(count) + " x " + id + " (" +
                Hs::ProtectionModeName(mode) + ")", r);
            return;
        }

        // remove
        uint32 count = 1;
        bool all = false;
        if (args.size() > 4)
        {
            if (Lower(args[4]) == "all")
                all = true;
            else if (!ParseNumber(args[4], count))
            {
                handler->SendSysMessage("Hearthstone: '" + args[4] + "' is neither a count (1..99) nor 'all'.");
                return;
            }
        }
        if (args.size() > 5)
        {
            SendCollUsage(handler);
            return;
        }
        Hs::MutationResult const r = HsCollections().Remove(target.accountId, cardArg, count, all, "gm_remove",
            GmSource(handler));
        std::string const id = r.mutation.cardId.empty() ? cardArg : r.mutation.cardId;
        FinishMutation(handler, "removed", target, (all ? std::string("all") : std::to_string(count)) + " x " + id, r);
    }
    // ---- .hs deck / .hs class ----------------------------------------------

    void SendDeckUsage(ChatHandler* handler)
    {
        handler->SendSysMessage("Usage: .hs deck show <player|#accountId> [deckId]");
        handler->SendSysMessage("       .hs deck check <player> <deckId>");
        handler->SendSysMessage("       .hs deck delete <player|#accountId> <deckId>");
    }

    void SendClassUsage(ChatHandler* handler)
    {
        handler->SendSysMessage("Usage: .hs class show <player|#accountId>");
        handler->SendSysMessage("       .hs class unlock|lock <player|#accountId> <class>  (unlock also awards the starter deck)");
    }

    void SendNpcUsage(ChatHandler* handler)
    {
        handler->SendSysMessage("Usage: .hs npc show <player|#accountId>");
        handler->SendSysMessage("       .hs npc unlock <player|#accountId> <creature entry|all>  (as if the quest was turned in)");
    }

    // .hs npc ... (args[0] == "npc"): the quest NPC opponents of an account.
    void HandleNpcCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
    {
        std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
        bool const unlock = sub == "unlock";
        if ((sub != "show" && !unlock) || args.size() != (unlock ? 4u : 3u))
        {
            SendNpcUsage(handler);
            return;
        }
        if (unlock && security < static_cast<int>(SEC_DEVELOPER))
        {
            handler->SendSysMessage("Hearthstone: .hs npc unlock needs security level 3 (developer) or higher.");
            return;
        }
        std::vector<Hs::OpponentRegistry::UnlockNpc> const npcs = HsQuestNpcs();
        if (npcs.empty())
        {
            handler->SendSysMessage("Hearthstone: no quest NPC opponents are loaded (matches disabled, or table "
                "hearthstone_npc_unlock missing: see the server log).");
            return;
        }
        CollTarget target;
        if (!ResolveCollTarget(handler, args[2], target))
            return;
        auto nameOf = [](uint32 entry)
        {
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
            return info ? info->name : "creature " + std::to_string(entry);
        };

        if (unlock)
        {
            std::vector<Hs::OpponentRegistry::UnlockNpc> chosen;
            std::set<uint32_t> quests;
            uint32 entry = 0;
            bool const all = Lower(args[3]) == "all";
            if (!all && !ParseNumber(args[3], entry))
            {
                SendNpcUsage(handler);
                return;
            }
            for (Hs::OpponentRegistry::UnlockNpc const& npc : npcs)
                if (all || npc.entry == entry)
                {
                    chosen.push_back(npc);
                    quests.insert(npc.questId);
                }
            if (chosen.empty())
            {
                handler->SendSysMessage("Hearthstone: " + args[3] + " is not a quest NPC opponent (.hs npc show lists them).");
                return;
            }
            std::vector<uint32_t> added;
            bool const ok = HsNpcUnlocks().Reconcile(target.accountId, chosen, quests, GmSource(handler), added);
            std::string names;
            for (uint32 e : added)
                names += (names.empty() ? "" : ", ") + nameOf(e);
            handler->SendSysMessage("Hearthstone: unlocked for " + target.label + ": " +
                (names.empty() ? std::string("nothing new") : names) + (ok ? "." : " (some could not be written, see the log)."));
            if (!added.empty())
                sLog.outString("Hearthstone: GM %s unlocked %u quest NPC(s) for account %u", GmName(handler).c_str(),
                    static_cast<uint32>(added.size()), target.accountId);
            return;
        }

        std::map<uint32_t, uint32_t> unlocked;
        if (!HsNpcUnlocks().Get(target.accountId, unlocked))
        {
            handler->SendSysMessage("Hearthstone: the quest NPC unlocks of " + target.label + " could not be read "
                "(character migration 0006 applied?).");
            return;
        }
        handler->SendSysMessage("Hearthstone: quest NPC opponents of " + target.label + " (" +
            std::to_string(unlocked.size()) + " of " + std::to_string(npcs.size()) + " unlocked):");
        for (Hs::OpponentRegistry::UnlockNpc const& npc : npcs)
            handler->SendSysMessage("  " + std::to_string(npc.entry) + " " + nameOf(npc.entry) + " (" + npc.faction +
                ", quest " + std::to_string(npc.questId) + "): " + (unlocked.count(npc.entry) ? "unlocked" : "locked"));
    }

    void SendStarterUsage(ChatHandler* handler)
    {
        handler->SendSysMessage("Usage: .hs starter show <player|#accountId>");
        handler->SendSysMessage("       .hs starter grant <player|#accountId> <class>  (cards every time, the deck once per account)");
    }

    std::string JoinClasses(std::vector<std::string> const& classes)
    {
        std::string text;
        for (std::string const& c : classes)
            text += (text.empty() ? "" : ", ") + c;
        return text.empty() ? std::string("none") : text;
    }

    std::string DescribeDeck(Hs::Deck const& d, std::string const& state)
    {
        return "#" + std::to_string(d.id) + " " + d.deckClass + " \"" + d.name + "\" " + std::to_string(d.CardCount()) +
            "/" + std::to_string(Hs::kDeckSize) + " cards, rev " + std::to_string(d.rev) + ", " + d.origin + ": " + state;
    }

    // Where the starting class of a target comes from, for messages.
    std::string DescribeStart(CollTarget const& target)
    {
        if (!target.character)
            return "by account: the starting class depends on the character, locked is not checked";
        return "starting class " + (target.startClass.empty() ? std::string("none") : target.startClass);
    }

    void ReportDecksUnavailable(ChatHandler* handler, CollTarget const& target)
    {
        handler->SendSysMessage("Hearthstone: the decks of " + target.label + " could not be read (see the server log).");
    }

    // .hs deck show
    void HandleDeckShow(ChatHandler* handler, CollTarget const& target, std::vector<std::string> const& args)
    {
        uint32 deckId = 0;
        if (args.size() > 4 || (args.size() > 3 && (!ParseNumber(args[3], deckId) || deckId == 0)))
        {
            SendDeckUsage(handler);
            return;
        }
        Hs::DeckList list;
        if (!HsDecks().List(target.accountId, target.startClass, list))
        {
            ReportDecksUnavailable(handler, target);
            return;
        }
        std::string const state = HsDecks().IsOnline(target.accountId) ? "online" : "offline";
        if (deckId)
        {
            for (Hs::DeckListEntry const& e : list.decks)
            {
                if (e.deck.id != deckId)
                    continue;
                handler->SendSysMessage("Hearthstone: deck of " + target.label + " (" + state + ", " +
                    DescribeStart(target) + "): " + DescribeDeck(e.deck, e.state));
                handler->SendSysMessage("  " + DescribeDeckCards(e.deck.cards));
                return;
            }
            handler->SendSysMessage("Hearthstone: " + target.label + " has no deck #" + std::to_string(deckId) +
                " (never created or deleted).");
            return;
        }
        handler->SendSysMessage("Hearthstone: decks of " + target.label + " (" + state + "): " +
            std::to_string(list.decks.size()) + " of " + std::to_string(list.cap) + " saved; " + DescribeStart(target) +
            "; account unlocks: " + JoinClasses(list.unlocked) + ".");
        for (Hs::DeckListEntry const& e : list.decks)
            handler->SendSysMessage("  " + DescribeDeck(e.deck, e.state));
    }

    // .hs deck check: the match-start revalidation for one character.
    void HandleDeckCheck(ChatHandler* handler, CollTarget const& target, std::vector<std::string> const& args)
    {
        uint32 deckId = 0;
        if (args.size() != 4 || !ParseNumber(args[3], deckId) || deckId == 0)
        {
            SendDeckUsage(handler);
            return;
        }
        if (!target.character)
        {
            handler->SendSysMessage("Hearthstone: .hs deck check needs a character name (class access is per character).");
            return;
        }
        Hs::DeckMatchCheck const check = HsDecks().CheckForMatch(target.accountId, deckId, target.startClass);
        if (check.status == Hs::DeckMatchCheck::Status::Unavailable)
        {
            ReportDecksUnavailable(handler, target);
            return;
        }
        if (check.status == Hs::DeckMatchCheck::Status::Missing)
        {
            handler->SendSysMessage("Hearthstone: " + target.label + " has no deck #" + std::to_string(deckId) + ".");
            return;
        }
        std::string const who = "deck #" + std::to_string(deckId) + " for " + target.label + " (" + DescribeStart(target) + ")";
        if (check.Ready())
        {
            handler->SendSysMessage("Hearthstone: " + who + ": match-ready.");
            return;
        }
        handler->SendSysMessage("Hearthstone: " + who + ": NOT match-ready (" + check.Text() + ").");
        if (check.state.size)
            handler->SendSysMessage("  size: fewer than " + std::to_string(Hs::kDeckSize) + " cards");
        if (check.state.owned)
            handler->SendSysMessage("  owned: the account owns fewer copies of a card than the deck uses");
        if (check.state.invalid)
            handler->SendSysMessage("  invalid: the deck breaks a deck rule now (card data changed?)");
        if (check.state.locked)
            handler->SendSysMessage("  locked: the character cannot play this class (starting class or account unlock)");
        if (check.engine)
            handler->SendSysMessage(check.unsupportedCard.empty()
                ? std::string("  engine: no match engine is registered")
                : "  engine: the match engine does not support " + check.unsupportedCard);
    }

    // .hs deck delete
    void HandleDeckDelete(ChatHandler* handler, CollTarget const& target, std::vector<std::string> const& args)
    {
        uint32 deckId = 0;
        if (args.size() != 4 || !ParseNumber(args[3], deckId) || deckId == 0)
        {
            SendDeckUsage(handler);
            return;
        }
        Hs::DeckDeleteRequest req;
        req.id = deckId;
        req.anyRev = true;
        req.reason = "gm_delete";
        req.source = GmSource(handler);
        Hs::DeckDeleteResult const r = HsDecks().DeleteDeck(target.accountId, req);
        if (r.status == Hs::DeckStatus::Ok)
        {
            handler->SendSysMessage("Hearthstone: deleted deck " + DescribeDeck(r.deck, "deleted") + " of " +
                target.label + ".");
            sLog.outString("Hearthstone: GM %s deleted deck #%u (%s, \"%s\") of account %u", GmName(handler).c_str(),
                deckId, r.deck.deckClass.c_str(), r.deck.name.c_str(), target.accountId);
            PushDeckChanged(target.accountId, "gm");
            return;
        }
        std::string const why = r.status == Hs::DeckStatus::Refused ? std::string(Hs::DeckWhyText(r.why))
            : std::string(Hs::DeckStatusText(r.status));
        handler->SendSysMessage("Hearthstone: deck #" + std::to_string(deckId) + " of " + target.label +
            " was not deleted: " + why + ".");
        if (r.status != Hs::DeckStatus::Refused)
            sLog.outError("Hearthstone: GM %s delete of deck #%u of account %u failed: %s", GmName(handler).c_str(),
                deckId, target.accountId, why.c_str());
    }

    // .hs deck ... (args[0] == "deck"). The caller already checked security >= MODERATOR.
    void HandleDeckCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
    {
        std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
        bool const mutating = sub == "delete";
        if ((sub != "show" && sub != "check" && !mutating) || args.size() < 3)
        {
            SendDeckUsage(handler);
            return;
        }
        if (mutating && security < static_cast<int>(SEC_DEVELOPER))
        {
            handler->SendSysMessage("Hearthstone: .hs deck " + sub + " needs security level 3 (developer) or higher.");
            return;
        }
        if (!HsCollections().Catalog())
        {
            handler->SendSysMessage("Hearthstone: decks are UNAVAILABLE: the card data of dataset " +
                std::string(Hs::kDatasetId) + " could not be loaded at startup (see the server log).");
            return;
        }
        CollTarget target;
        if (!ResolveCollTarget(handler, args[2], target))
            return;
        if (sub == "show")
            HandleDeckShow(handler, target, args);
        else if (sub == "check")
            HandleDeckCheck(handler, target, args);
        else
            HandleDeckDelete(handler, target, args);
    }

    // .hs class ... (args[0] == "class"). The caller already checked security >= MODERATOR.
    void HandleClassCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
    {
        std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
        bool const mutating = sub == "unlock" || sub == "lock";
        if ((sub != "show" && !mutating) || args.size() < 3 || args.size() != (mutating ? 4u : 3u))
        {
            SendClassUsage(handler);
            return;
        }
        if (mutating && security < static_cast<int>(SEC_DEVELOPER))
        {
            handler->SendSysMessage("Hearthstone: .hs class " + sub + " needs security level 3 (developer) or higher.");
            return;
        }
        CollTarget target;
        if (!ResolveCollTarget(handler, args[2], target))
            return;

        if (sub == "show")
        {
            Hs::DeckBook book;
            if (!HsDecks().GetBook(target.accountId, book))
            {
                ReportDecksUnavailable(handler, target);
                return;
            }
            std::string unlocks;
            for (auto const& entry : book.unlocks)
                unlocks += (unlocks.empty() ? "" : ", ") + entry.first + " (" + entry.second + ")";
            handler->SendSysMessage("Hearthstone: class access of " + target.label + ": " + DescribeStart(target) +
                "; account unlocks: " + (unlocks.empty() ? std::string("none") : unlocks) + ".");
            if (target.character)
            {
                std::set<std::string> const playable = Hs::PlayableClasses(target.startClass, book.UnlockedClasses());
                handler->SendSysMessage("  playable by this character: " +
                    JoinClasses(std::vector<std::string>(playable.begin(), playable.end())) + ".");
            }
            return;
        }

        std::string const cls = Hs::ParseDeckClass(args[3]);
        if (cls.empty())
        {
            handler->SendSysMessage("Hearthstone: '" + args[3] + "' is not a class (" +
                JoinClasses(Hs::DeckClasses()) + ").");
            return;
        }
        bool const unlock = sub == "unlock";
        Hs::ClassChangeResult const r = unlock
            ? HsDecks().UnlockClass(target.accountId, cls, "gm_unlock", GmSource(handler))
            : HsDecks().LockClass(target.accountId, cls, "gm_lock", GmSource(handler));
        if (r.status == Hs::ClassStatus::Ok)
        {
            handler->SendSysMessage("Hearthstone: " + std::string(unlock ? "unlocked " : "locked ") + cls + " for " +
                target.label + " (account-wide).");
            sLog.outString("Hearthstone: GM %s %s class %s for account %u", GmName(handler).c_str(),
                unlock ? "unlocked" : "locked", cls.c_str(), target.accountId);
            PushDeckChanged(target.accountId, "class");
            if (unlock)
            {
                // The first unlock of a class brings its starter deck.
                std::string text;
                HsAwardStarter(target.accountId, cls, "unlock:" + GmSource(handler), text);
                handler->SendSysMessage("Hearthstone: " + text);
            }
            return;
        }
        bool const failed = r.status == Hs::ClassStatus::Unavailable || r.status == Hs::ClassStatus::CommitFailed;
        handler->SendSysMessage("Hearthstone: " + std::string(failed ? "failed, " : "rejected, ") +
            Hs::ClassStatusText(r.status) + ". Nothing was changed.");
        if (failed)
            sLog.outError("Hearthstone: GM %s %s class %s for account %u: failed, %s", GmName(handler).c_str(),
                unlock ? "unlock" : "lock", cls.c_str(), target.accountId, Hs::ClassStatusText(r.status));
    }

    // .hs starter ... (args[0] == "starter"). The caller already checked security >= MODERATOR.
    void HandleStarterCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
    {
        std::string const sub = args.size() > 1 ? Lower(args[1]) : std::string();
        bool const grant = sub == "grant";
        if ((sub != "show" && !grant) || args.size() != (grant ? 4u : 3u))
        {
            SendStarterUsage(handler);
            return;
        }
        if (grant && security < static_cast<int>(SEC_DEVELOPER))
        {
            handler->SendSysMessage("Hearthstone: .hs starter grant needs security level 3 (developer) or higher.");
            return;
        }
        CollTarget target;
        if (!ResolveCollTarget(handler, args[2], target))
            return;

        if (sub == "show")
        {
            Hs::DeckBook book;
            if (!HsDecks().GetBook(target.accountId, book))
            {
                ReportDecksUnavailable(handler, target);
                return;
            }
            if (!book.startersKnown)
            {
                handler->SendSysMessage("Hearthstone: the starter awards of " + target.label +
                    " could not be read (character migration 0005 applied?).");
                return;
            }
            std::string awards;
            for (auto const& entry : book.starters)
            {
                std::string deck = "cards only";
                if (entry.second != 0)
                    deck = "deck #" + std::to_string(entry.second) + (book.decks.count(entry.second) ? "" : ", deleted");
                awards += (awards.empty() ? "" : ", ") + entry.first + " (" + deck + ")";
            }
            handler->SendSysMessage("Hearthstone: starter decks awarded to " + target.label + ": " +
                (awards.empty() ? std::string("none") : awards) + ".");
            return;
        }

        std::string const cls = Hs::ParseDeckClass(args[3]);
        if (cls.empty())
        {
            handler->SendSysMessage("Hearthstone: '" + args[3] + "' is not a class (" + JoinClasses(Hs::DeckClasses()) + ").");
            return;
        }
        std::string text;
        HsAwardStarter(target.accountId, cls, GmSource(handler), text);
        handler->SendSysMessage("Hearthstone: " + text);
    }
}

bool HsAwardStarter(uint32_t accountId, std::string const& deckClass, std::string const& source, std::string& text)
{
    Hs::DeckCards recipe;
    std::string name;
    if (!HsStarterRecipe(deckClass, recipe, name))
    {
        text = "no starter deck for " + deckClass + ": the library deck " + Hs::StarterDeckKey(deckClass) +
            " is missing or not ready (see .hs match decks and the server log).";
        sLog.outError("Hearthstone: starter award %s for account %u failed: %s", deckClass.c_str(), accountId, text.c_str());
        return false;
    }

    // 1. The cards: protected copies up to the recipe. Idempotent, so every award or retry runs it.
    Hs::TopUpResult const cards = HsCollections().TopUpProtected(accountId, recipe, "starter", source);
    if (cards.status != Hs::MutationStatus::Ok)
    {
        text = "the " + deckClass + " starter cards were not granted (" +
            (cards.status == Hs::MutationStatus::Rejected
                ? std::string(Hs::OwnershipErrorText(cards.error)) + ", card " + cards.card
                : std::string("collection unavailable or commit failed")) + "); nothing was changed.";
        sLog.outError("Hearthstone: starter award %s for account %u (%s): %s", deckClass.c_str(), accountId, source.c_str(),
            text.c_str());
        return false;
    }
    if (!cards.mutations.empty())
        PushCollectionChanged(accountId, cards.mutations.front().versionAfter);

    // 2. The deck and the award record, once per account.
    Hs::StarterAwardRequest req;
    req.cls = deckClass;
    req.name = name;
    req.cards = recipe;
    req.source = source;
    Hs::StarterAwardResult const deck = HsDecks().AwardStarter(accountId, req);
    std::string const added = std::to_string(cards.added) + " protected card cop" + (cards.added == 1 ? "y" : "ies") + " added";
    switch (deck.status)
    {
        case Hs::StarterStatus::Ok:
            if (deck.deckCreated)
                text = "awarded the " + deckClass + " starter deck \"" + deck.deck.name + "\" (deck #" +
                    std::to_string(deck.deckId) + ", " + added + ").";
            else
                text = "awarded the " + deckClass + " starter cards (" + added + "); the deck list is full, so no deck "
                    "was created: the player can build it from the cards.";
            PushDeckChanged(accountId, "starter");
            sLog.outString("Hearthstone: starter %s awarded to account %u (%s): %s, %u protected copies added",
                deckClass.c_str(), accountId, source.c_str(), deck.deckCreated ? ("deck #" + std::to_string(deck.deckId)).c_str()
                : "cards only (deck list full)", cards.added);
            return true;
        case Hs::StarterStatus::Already:
            text = "the account already received the " + deckClass + " starter deck; only missing protected cards were "
                "topped up (" + added + ").";
            if (cards.added)
                sLog.outString("Hearthstone: starter %s top-up for account %u (%s): %u protected copies added",
                    deckClass.c_str(), accountId, source.c_str(), cards.added);
            return true;
        case Hs::StarterStatus::Refused:
            text = "the " + deckClass + " starter recipe breaks a deck rule (" + Hs::DeckWhyText(deck.why) + "); " + added + ".";
            break;
        default:
            text = "the " + deckClass + " starter deck could not be recorded (" + Hs::StarterStatusText(deck.status) +
                "); " + added + ". Granting it again later completes it.";
            break;
    }
    sLog.outError("Hearthstone: starter award %s for account %u (%s): %s", deckClass.c_str(), accountId, source.c_str(),
        text.c_str());
    return false;
}

bool HsUnlockClass(uint32_t accountId, std::string const& deckClass, std::string const& source, std::string& text)
{
    Hs::ClassChangeResult const r = HsDecks().UnlockClass(accountId, deckClass, "item_unlock", source);
    if (r.status != Hs::ClassStatus::Ok && r.status != Hs::ClassStatus::Already)
    {
        text = std::string("the ") + deckClass + " class could not be unlocked (" + Hs::ClassStatusText(r.status) +
            "); nothing was changed.";
        sLog.outError("Hearthstone: class unlock %s for account %u (%s): %s", deckClass.c_str(), accountId, source.c_str(),
            Hs::ClassStatusText(r.status));
        return false;
    }
    if (r.status == Hs::ClassStatus::Ok)
    {
        sLog.outString("Hearthstone: class %s unlocked for account %u (%s)", deckClass.c_str(), accountId, source.c_str());
        PushDeckChanged(accountId, "class");
    }
    std::string award;
    bool const ok = HsAwardStarter(accountId, deckClass, source, award);
    text = (r.status == Hs::ClassStatus::Ok ? "the " + deckClass + " class is unlocked for your whole account. "
                                            : "the " + deckClass + " class was already unlocked. ") + award;
    return ok;
}

bool HsSessionHandshaken(uint32_t guidLow)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_sessions.find(guidLow);
    return it != g_sessions.end() && it->second.Handshaken();
}

// ---- the pushes and helpers of HsEconomyGlue.h ------------------------------

uint32_t HsOnlineCharacterOf(uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const online = g_onlineCharacter.find(accountId);
    return online == g_onlineCharacter.end() ? 0 : online->second;
}

void HsPushLadder(uint32_t guidLow, std::string const& op, std::string const& payload)
{
    if (!sHearthstoneConfig.Get().enable || !HsSessionHandshaken(guidLow))
        return;
    if (Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow)))
    {
        std::vector<std::string> frames;
        Hs::AppendReply(frames, op, Hs::kServerPushReqId, payload);
        SendFrames(player, frames);
    }
}

bool HsSessionHasLadderUi(uint32_t guidLow)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto const it = g_sessions.find(guidLow);
    return it != g_sessions.end() && it->second.LadderUi();
}

void HsPushArena(uint32_t accountId, bool open)
{
    if (!sHearthstoneConfig.Get().enable)
        return;
    uint32 const guidLow = HsOnlineCharacterOf(accountId);
    // The view takes the arena glue mutex: no registry lock may be held here (HsSessionHandshaken locks and unlocks).
    if (!guidLow || !HsSessionHandshaken(guidLow))
        return;
    Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
    if (!player)
        return;
    Hs::ArenaView const view = HsArenaMakeView(accountId, guidLow);
    std::vector<std::string> frames;
    if (open)
        Hs::AppendArenaOpen(frames, view);
    else
        Hs::AppendArenaUpdate(frames, view);
    SendFrames(player, frames);
}

void HsPushArenaClose(uint32_t guidLow)
{
    if (!sHearthstoneConfig.Get().enable || !guidLow || !HsSessionHandshaken(guidLow))
        return;
    Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
    if (!player)
        return;
    std::vector<std::string> frames;
    Hs::AppendArenaClose(frames);
    SendFrames(player, frames);
}

void HsPushIntroDone(uint32_t accountId)
{
    if (sHearthstoneConfig.Get().enable)
        PushToAccount(accountId, [](std::vector<std::string>& out) { Hs::AppendIntroDone(out); });
}

void HsPushWalletChanged(uint32_t accountId, std::string const& why, uint32_t gain)
{
    if (!sHearthstoneConfig.Get().enable)
        return;
    uint32 guidLow = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto const online = g_onlineCharacter.find(accountId);
        if (online == g_onlineCharacter.end())
            return;
        guidLow = online->second;
        auto const session = g_sessions.find(guidLow);
        if (session == g_sessions.end() || !session->second.Handshaken())
            return;
    }
    Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
    Hs::Wallet wallet;
    if (!player || !HsCollections().GetWallet(accountId, wallet))
        return;
    std::vector<std::string> frames;
    Hs::AppendWalletChanged(frames, HsCoinCount(player), wallet, why, gain);
    SendFrames(player, frames);
}

void HsPushHeroChanged(uint32_t accountId, std::string const& why)
{
    PushToAccount(accountId, [&why](std::vector<std::string>& out) { Hs::AppendHeroChanged(out, why); });
}

void HsPushPackChanged(uint32_t accountId, uint32_t stock)
{
    PushPackChanged(accountId, stock);
}

void HsPushCardChanged(uint32_t accountId, uint32_t version)
{
    PushCollectionChanged(accountId, version);
}

void HsPushCraftAccess(uint32_t accountId, bool open, std::string const& npc)
{
    PushToAccount(accountId, [open, &npc](std::vector<std::string>& out) { Hs::AppendCraftAccess(out, open, npc); });
}

bool HsResolveAccountTarget(ChatHandler* handler, std::string const& arg, uint32_t& accountId, std::string& label)
{
    CollTarget target;
    if (!ResolveCollTarget(handler, arg, target))
        return false;
    accountId = target.accountId;
    label = target.label;
    return true;
}

std::string HsGmSource(ChatHandler* handler)
{
    return GmSource(handler);
}

std::string HsGmName(ChatHandler* handler)
{
    return GmName(handler);
}

class HearthstoneWorldScript : public WorldScript
{
public:
    HearthstoneWorldScript()
        : WorldScript("HearthstoneWorldScript", {
              WORLDHOOK_ON_AFTER_CONFIG_LOAD,
              WORLDHOOK_ON_STARTUP,
              WORLDHOOK_ON_UPDATE,
              WORLDHOOK_ON_SHUTDOWN
          })
    {
    }

    void OnStartup() override
    {
        // The initial config load precedes script registration, so it never
        // reaches OnAfterConfigLoad: read the (already merged) values here.
        sHearthstoneConfig.Load(false);
        sLog.outString("Hearthstone: module %s, protocol v%d, dataset %s (%s, %u cards)",
            Hs::kModuleVersion, Hs::kProtocolVersion, Hs::kDatasetId, Hs::kDatasetHash, Hs::kDatasetCardCount);
        CheckDatasetTable();
        CheckHsCollectionTables();
        CheckHsDeckTables();
        CheckHsMatchHistoryTable();
        // Card rules for ownership validation and the pack pool. Without them
        // every collection and pack operation answers ERR UNAVAILABLE (and the
        // GM commands say so).
        HsCollections().SetCatalog(LoadHsCardCatalog());
        HsCollections().SetPackOdds(sHearthstoneConfig.GetPackOdds());
        HsDecks().SetMaxSaved(sHearthstoneConfig.GetDeckMaxSaved());
        if (std::shared_ptr<Hs::PackPool const> const pool = HsCollections().Pool())
            sLog.outString("Hearthstone: pack pool %u cards (%u common incl. basic, %u rare, %u epic, %u legendary)",
                static_cast<uint32>(pool->Size()),
                static_cast<uint32>(pool->Cards(Hs::PackRarity::Common).size()),
                static_cast<uint32>(pool->Cards(Hs::PackRarity::Rare).size()),
                static_cast<uint32>(pool->Cards(Hs::PackRarity::Epic).size()),
                static_cast<uint32>(pool->Cards(Hs::PackRarity::Legendary).size()));
        // Phases 07 and 08: the match engine's cards (and the deck service's engine predicate), the deck library and
        // the opponent registry (world database rows plus the config), the effect.
        HsMatchStartup();
        // The dust values and the pack vendor / crafting NPCs.
        HsEconomyStartup();
        HsCardTraderLoad();
        // The draft arena (tables, the run service's card data, the duel sink, runs a crash left in a duel).
        HsArenaStartup();
        // The World NPC Ladder (the roster, the stored ladder, the ladder start on a background thread).
        HsLadderStartup();
        // The Battlegrounds effect registry (112 entries) is checked and logged here.
        HsBgStartup();
        // The weekly quest rotations (the server swaps the quest givers' weeklies, see WeeklyQuestRotation).
        HsWeeklyRegisterRotations();
        HsWeeklyUpdate();
    }

    void OnShutdown() override
    {
        // Stops the ladder's background thread (an unfinished round is dropped and replays identically at the next start).
        HsLadderShutdown();
        // Unsummons the ladder opponents (nothing of the walk-ups survives a stop).
        HsLadderTableShutdown();
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
        {
            sHearthstoneConfig.Load(true);
            HsCollections().SetPackOdds(sHearthstoneConfig.GetPackOdds());
            HsDecks().SetMaxSaved(sHearthstoneConfig.GetDeckMaxSaved());
            HsMatchReloadConfig();
            HsEconomyReloadConfig();
            HsCardTraderLoad();
        }
    }

    void OnUpdate(uint32 diff) override
    {
        // The AI of running matches (one paced action per match; nothing to do without a match).
        HsMatchUpdate();
        // Sweeps the Battlegrounds reassemblers and ends games the switch no longer allows.
        HsBgUpdate(diff);
        HsClassQuestUpdate();
        HsNaxxUpdate();
        HsWeeklyUpdate();
        HsHunterUpdate(diff);
        HsHordeWarriorUpdate(diff);
        HsHordeMageUpdate(diff);
        HsHordePaladinUpdate(diff);
        HsAllianceShamanUpdate(diff);
        HsHordeDruidUpdate(diff);
        HsHordePriestUpdate(diff);
        HsAllianceRogueUpdate(diff);
        HsAllianceDruidUpdate(diff);
        // Closes the crafting access of characters that walked away from the NPC.
        HsEconomyUpdate(diff);
        // ARENA_UPD pushes for changed runs and the completion of the arena introduction quest.
        HsArenaHostUpdate(diff);
        // Takes a finished NPC round from the ladder's worker, saves it and plans the next one.
        HsLadderUpdate(diff);
        // The walk-up of the ladder opponent (movement, sitting, the match start, the line, the despawn).
        HsLadderTableUpdate(diff);

        m_sweepTimer += diff;
        if (m_sweepTimer < 500)
            return;
        m_sweepTimer = 0;

        Hs::Settings const settings = sHearthstoneConfig.Get();
        uint64_t const now = NowMs();

        std::vector<std::pair<uint32, std::vector<std::string>>> outgoing;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& entry : g_sessions)
            {
                if (entry.second.PendingCount() == 0)
                    continue;
                std::vector<std::string> frames;
                entry.second.Sweep(now, settings, frames);
                if (!frames.empty())
                    outgoing.emplace_back(entry.first, std::move(frames));
            }
        }

        for (auto const& out : outgoing)
            if (Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, out.first)))
                SendFrames(player, out.second);
    }

private:
    uint32 m_sweepTimer = 0;
};

class HearthstonePlayerScript : public PlayerScript
{
public:
    HearthstonePlayerScript()
        : PlayerScript("HearthstonePlayerScript", {
              PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
              PLAYERHOOK_CAN_USE_GROUP_CHAT,
              PLAYERHOOK_ON_LOGIN,
              PLAYERHOOK_ON_LOGOUT,
              PLAYERHOOK_ON_DELETE,
              PLAYERHOOK_ON_MAP_CHANGED
          })
    {
    }

    void OnBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        if (!player || !IsOurs(lang, msg))
            return;
        // The client sends through the GUILD distribution; anything else that
        // carries our prefix is ignored here and swallowed in CanUseGroupChat.
        if (type != CHAT_MSG_GUILD)
            return;

        // The Battlegrounds have their own prefix and their own session (HsBgGlue.h); the WRHS path below
        // is unchanged. IsOurs already checked the prefix (WRHS or WRBG).
        if (msg.compare(0, std::char_traits<char>::length(HsBgPrefix()), HsBgPrefix()) == 0)
        {
            HsBgHandleFrame(player, msg);
            return;
        }

        std::string frame;
        if (!Hs::StripPrefix(msg, frame))
            return;

        Hs::Settings const settings = sHearthstoneConfig.Get();
        int const security = player->GetSession() ? static_cast<int>(player->GetSession()->GetSecurity()) : 0;
        Hs::ServerInfo info = MakeServerInfo();
        info.intro = HsIntroDone(player) ? "1" : "0";
        info.arena = HsArenaReady();
        info.heroes = HsHeroReady();
        AccountCollectionHandler collections(player, player->GetSession() ? player->GetSession()->GetAccountId() : 0,
            player->GetGUIDLow(), player->GetName(), Hs::DeckClassOfWowClass(player->getClass()));
        uint64_t const now = NowMs();
        HsMatchSession match(player->GetGUIDLow(), now);
        // The arena ops run after the registry lock is released (see HsArenaHost.h).
        HsArenaSession arena(player, player->GetSession() ? player->GetSession()->GetAccountId() : 0, player->GetGUIDLow());
        HsLadderSession ladder(player);

        std::vector<std::string> out;
        bool handshaken = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            Hs::Session& session = g_sessions[player->GetGUIDLow()];
            session.OnFrame(now, frame, security, settings, info, out, &collections,
                settings.matchEnable ? &match : nullptr, settings.matchEnable ? &arena : nullptr,
                settings.matchEnable ? &ladder : nullptr);
            handshaken = session.Handshaken();
        }
        if (settings.debug)
            sLog.outString("Hearthstone[debug]: %s (guid %u): %u bytes in, %u frames out, handshaken=%u",
                player->GetName(), player->GetGUIDLow(), static_cast<uint32>(frame.size()),
                static_cast<uint32>(out.size()), handshaken ? 1u : 0u);
        arena.Finish(out);
        ladder.Finish(out);
        SendFrames(player, out);
        // A concede ended the match (or a pause suspended the arena duel): remove the effect and log, outside every lock.
        HsMatchFinishFrame(player, match);
    }

    bool CanUseGroupChat(Player* /*player*/, uint32 /*type*/, uint32 lang, std::string& msg) override
    {
        return !IsOurs(lang, msg);
    }

    void OnLogin(Player* player) override
    {
        if (player)
        {
            OnCharacterEnter(player);
            HsArenaHostOnLogin(player); // the quest of a free run that ended meanwhile
        }
    }

    void OnLogout(Player* player) override
    {
        if (player)
            OnCharacterLeave(player);
    }

    void OnDelete(ObjectGuid guid, uint32 accountId) override
    {
        DropSession(guid.GetCounter());
        // Deleting happens at character selection, so the account is not in the
        // world; drop anything cached for it anyway.
        if (accountId)
        {
            HsCollections().Evict(accountId);
            HsDecks().Evict(accountId);
        }
    }

    // A map change ends an open Battlegrounds game (the plan: cleanup on map leave).
    void OnMapChanged(Player* player) override
    {
        if (player)
            HsBgOnMapChanged(player);
    }
};

class HearthstoneCommandScript : public AllCommandScript
{
public:
    HearthstoneCommandScript() : AllCommandScript("HearthstoneCommandScript") {}

    // Returning false claims the command, true leaves it to the core's table.
    bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* args) override
    {
        if (!handler || !command)
            return true;
        std::string name = command;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "hs")
            return true;

        int const security = handler->GetSession() ? static_cast<int>(handler->GetSession()->GetSecurity())
                                                   : static_cast<int>(SEC_CONSOLE);
        std::vector<std::string> const argv = SplitArgs(args);
        std::string const sub = argv.empty() ? std::string() : Lower(argv[0]);

        // The Battlegrounds entry is open to EVERY player, unlike the rest of .hs, which is MODERATOR-only.
        // A GM may always inspect it.
        if (sub == "bg" && (HsBgEnabled() || security >= static_cast<int>(SEC_MODERATOR)))
        {
            HsBgCommand(handler, security, argv);
            return false;
        }

        if (security < static_cast<int>(SEC_MODERATOR))
            return true; // not a command for this player: behave as if it did not exist

        if (sub == "coll")
        {
            HandleCollCommand(handler, security, argv);
            return false;
        }
        if (sub == "pack")
        {
            HandlePackCommand(handler, security, argv);
            return false;
        }
        if (sub == "deck")
        {
            HandleDeckCommand(handler, security, argv);
            return false;
        }
        if (sub == "class")
        {
            HandleClassCommand(handler, security, argv);
            return false;
        }
        if (sub == "match")
        {
            HsMatchCommand(handler, security, argv);
            return false;
        }
        if (sub == "starter")
        {
            HandleStarterCommand(handler, security, argv);
            return false;
        }
        if (sub == "npc")
        {
            HandleNpcCommand(handler, security, argv);
            return false;
        }
        if (sub == "wallet")
        {
            HsWalletCommand(handler, security, argv);
            return false;
        }
        if (sub == "hero")
        {
            HsHeroCommand(handler, security, argv);
            return false;
        }
        if (sub == "arena")
        {
            HsArenaCommand(handler, security, argv);
            return false;
        }
        if (sub == "ladder")
        {
            HsLadderCommand(handler, security, argv);
            return false;
        }
        if (sub != "status")
        {
            handler->SendSysMessage("Usage: .hs status");
            SendCollUsage(handler);
            SendPackUsage(handler);
            SendDeckUsage(handler);
            SendClassUsage(handler);
            SendStarterUsage(handler);
            SendNpcUsage(handler);
            HsWalletUsage(handler);
            HsArenaUsage(handler);
            HsHeroUsage(handler);
            HsLadderUsage(handler);
            HsMatchUsage(handler);
            return false;
        }

        Hs::Settings const s = sHearthstoneConfig.Get();
        size_t total = 0;
        size_t handshaken = 0;
        size_t pending = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            total = g_sessions.size();
            for (auto const& entry : g_sessions)
            {
                if (entry.second.Handshaken())
                    ++handshaken;
                pending += entry.second.PendingCount();
            }
        }

        handler->SendSysMessage(std::string("Hearthstone: ") + (s.enable ? "enabled" : "DISABLED") +
            ", debug " + (s.debug ? "on" : "off") + ", module " + Hs::kModuleVersion +
            ", protocol v" + std::to_string(Hs::kProtocolVersion));
        handler->SendSysMessage(std::string("Dataset: ") + Hs::kDatasetId + " hash " + Hs::kDatasetHashFull);
        handler->SendSysMessage("Sessions: " + std::to_string(handshaken) + " handshaken (" + std::to_string(total) +
            " known), pending reassemblies: " + std::to_string(pending));
        handler->SendSysMessage("Limits: maxBytes " + std::to_string(s.maxInboundBytes) + ", maxParts " +
            std::to_string(s.maxInboundParts) + ", timeout " + std::to_string(s.reassemblyTimeoutMs) +
            "ms, maxPending " + std::to_string(s.maxPendingRequests) + ", rate " + std::to_string(s.ratePerSecond) +
            "/s burst " + std::to_string(s.rateBurst));
        handler->SendSysMessage("MatchAuraSpellId: " + (s.matchAuraSpellId ? std::to_string(s.matchAuraSpellId)
            : std::string("unset (0)")));
        std::shared_ptr<Hs::CardCatalog const> const catalog = HsCollections().Catalog();
        handler->SendSysMessage("Collections: " + (catalog ? std::to_string(catalog->Size()) + " card rules loaded"
            : std::string("UNAVAILABLE (card data not loaded)")) + ", cached accounts: " +
            std::to_string(HsCollections().CachedCount()));
        std::shared_ptr<Hs::PackPool const> const pool = HsCollections().Pool();
        Hs::PackOdds const odds = HsCollections().GetPackOdds();
        handler->SendSysMessage("Packs: " + (pool && pool->Complete() ? std::to_string(pool->Size()) + " cards in the pool"
            : std::string("UNAVAILABLE (card data not loaded)")) + ", weights " + std::to_string(odds.weight[0]) + "/" +
            std::to_string(odds.weight[1]) + "/" + std::to_string(odds.weight[2]) + "/" + std::to_string(odds.weight[3]) +
            ", Legendary guarantee every " + std::to_string(odds.legendaryPity) + " packs");
        handler->SendSysMessage("Decks: " + (catalog ? std::string("at most ") + std::to_string(HsDecks().MaxSaved()) +
            " saved per account" : std::string("UNAVAILABLE (card data not loaded)")) + ", cached accounts: " +
            std::to_string(HsDecks().CachedCount()) + ", match engine: " + (HsDecks().HasEngineSupport()
            ? "registered" : "none (the match engine's card data is not loaded)"));
        HsEconomyStatus(handler);
        HsArenaStatus(handler);
        HsLadderStatus(handler);
        HsBgStatus(handler);
        HsMatchStatus(handler);
        return false;
    }
};

void AddHearthstoneScripts()
{
    new HearthstoneWorldScript();
    new HearthstonePlayerScript();
    new HearthstoneCommandScript();
    AddHearthstoneMatchScripts();
    AddHsHordeWarriorQuestScripts();
    AddHsHordeDruidQuestScripts();
    AddHsHordePriestQuestScripts();
    AddHsAlliancePriestQuestScripts();
    AddHsQuestPickupScripts();
    AddHsAllianceRogueQuestScripts();
    AddHsAllianceShamanQuestScripts();
    AddHsHordeShamanQuestScripts();
    AddHsHunterQuestScripts();
    AddHsAllianceMageQuestScripts();
    AddHsNaxxQuestScripts();
    AddHsLadderTableScripts();
}
