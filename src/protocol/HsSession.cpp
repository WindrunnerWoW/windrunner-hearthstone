#include "HsSession.h"

#include <algorithm>
#include <map>

namespace Hs
{
    namespace
    {
        uint32_t Clamp(uint32_t v, uint32_t lo, uint32_t hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        bool IsReservedOp(std::string const& op)
        {
            static char const* const kPrefixes[] = { "COLL_", "PACK_", "DECK_", "GAME_", "WALLET_", "CRAFT_", "ARENA_", "LADDER_", "HERO_" };
            for (char const* p : kPrefixes)
                if (op.compare(0, std::char_traits<char>::length(p), p) == 0)
                    return true;
            return op == "NPC_START";
        }

        // Largest deck id / revision a request may name (ParseUint takes up to 9 digits).
        constexpr uint32_t kMaxDeckNumber = 999999999;

        bool ParseUint(std::string const& s, uint32_t maxValue, uint32_t& out)
        {
            if (s.empty() || s.size() > 9)
                return false;
            uint32_t v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                v = v * 10 + static_cast<uint32_t>(c - '0');
            }
            if (v > maxValue)
                return false;
            out = v;
            return true;
        }
    }

    Settings Sanitize(Settings s)
    {
        s.maxInboundBytes = Clamp(s.maxInboundBytes, 64, 65536);
        s.maxInboundParts = Clamp(s.maxInboundParts, 1, kMaxPartsHardLimit);
        s.reassemblyTimeoutMs = Clamp(s.reassemblyTimeoutMs, 500, 600000);
        s.maxPendingRequests = Clamp(s.maxPendingRequests, 1, 64);
        s.ratePerSecond = Clamp(s.ratePerSecond, 1, 1000);
        s.rateBurst = Clamp(s.rateBurst, 1, 1000);
        return s;
    }

    void AppendReply(std::vector<std::string>& out, std::string const& op, std::string const& reqId,
                     std::string const& payload)
    {
        std::vector<std::string> frames;
        if (ChunkPayload(op, reqId, payload, frames))
        {
            for (std::string& f : frames)
                out.push_back(std::move(f));
            return;
        }
        AppendError(out, reqId, ErrorCode::TooLarge, "Reply does not fit into a protocol message.");
    }

    void AppendError(std::vector<std::string>& out, std::string const& reqId, ErrorCode code, std::string const& msg,
                     std::string const& why)
    {
        KvList kv;
        kv.push_back(std::make_pair("code", std::string(ErrorCodeName(code))));
        kv.push_back(std::make_pair("msg", msg));
        if (!why.empty())
            kv.push_back(std::make_pair("why", why));
        std::vector<std::string> frames;
        std::string const id = IsValidReqId(reqId) ? reqId : std::string(kServerPushReqId);
        if (ChunkPayload("ERR", id, KvEncode(kv), frames))
            for (std::string& f : frames)
                out.push_back(std::move(f));
    }

    void AppendCollChanged(std::vector<std::string>& out, uint32_t version)
    {
        AppendReply(out, "COLL_CHANGED", kServerPushReqId, EncodeCollChangedPayload(version));
    }

    void AppendPackChanged(std::vector<std::string>& out, uint32_t stock)
    {
        AppendReply(out, "PACK_CHANGED", kServerPushReqId, EncodePackChangedPayload(stock));
    }

    void AppendDeckChanged(std::vector<std::string>& out, std::string const& why)
    {
        AppendReply(out, "DECK_CHANGED", kServerPushReqId, EncodeDeckChangedPayload(why));
    }

    void AppendHeroChanged(std::vector<std::string>& out, std::string const& why)
    {
        AppendReply(out, "HERO_CHANGED", kServerPushReqId, "why=" + why);
    }

    void AppendGame(std::vector<std::string>& out, std::string const& payload)
    {
        AppendReply(out, "GAME", kServerPushReqId, payload);
    }

    void AppendWalletChanged(std::vector<std::string>& out, uint32_t coins, Wallet const& wallet, std::string const& why,
                             uint32_t gain)
    {
        AppendReply(out, "WALLET_UPD", kServerPushReqId, EncodeWalletChangedPayload(coins, wallet, why, gain));
    }

    void AppendCraftAccess(std::vector<std::string>& out, bool open, std::string const& npc)
    {
        AppendReply(out, "CRAFT_ACCESS", kServerPushReqId, EncodeCraftAccessPayload(open, npc));
    }

    void AppendIntroDone(std::vector<std::string>& out)
    {
        AppendReply(out, "INTRO_DONE", kServerPushReqId, std::string());
    }

    namespace
    {
        // The answer to CRAFT_MAKE / CRAFT_SCRAP / CRAFT_EXTRAS: CRAFTED (plus COLL_CHANGED after a commit) or an
        // ERR. what: the participle ("crafted", "scrapped", "disenchanted"); subject: "The card was" or "The extra
        // cards were" (and the matching "could be" form for a failure).
        void AppendCraftResult(std::vector<std::string>& out, std::string const& reqId, char const* what,
                               CraftResult const& r, bool extras = false)
        {
            switch (r.status)
            {
                case CraftStatus::Ok:
                    AppendReply(out, "CRAFTED", reqId, EncodeCraftedPayload(r.record, false));
                    AppendCollChanged(out, r.record.collectionVersion);
                    return;
                case CraftStatus::Replay:
                    AppendReply(out, "CRAFTED", reqId, EncodeCraftedPayload(r.record, true));
                    return;
                case CraftStatus::Refused:
                    AppendError(out, reqId, ErrorCode::Refused,
                                std::string(extras ? "The extra cards were not " : "The card was not ") + what + ": " +
                                    CraftWhyText(r.why) + ". Nothing was changed.",
                                CraftWhyCode(r.why));
                    return;
                case CraftStatus::BadKey:
                    AppendError(out, reqId, ErrorCode::Malformed, "CRAFT_* needs key=<12-16 characters 0-9a-z>.");
                    return;
                case CraftStatus::Unavailable:
                case CraftStatus::CommitFailed:
                    AppendError(out, reqId, ErrorCode::Unavailable,
                                std::string(extras ? "The extra cards could not be " : "The card could not be ") + what +
                                " right now. Try again: the same request never runs twice.");
                    return;
            }
        }
    }

    void Session::Sweep(uint64_t nowMs, Settings const& settings, std::vector<std::string>& out)
    {
        std::vector<std::string> expired;
        m_reassembler.Sweep(nowMs, settings.reassemblyTimeoutMs, expired);
        if (!settings.enable)
            return;
        for (std::string const& id : expired)
            AppendError(out, id, ErrorCode::Timeout, "Request timed out before all parts arrived.");
    }

    void Session::OnFrame(uint64_t nowMs, std::string const& frame, int securityLevel,
                          Settings const& settings, ServerInfo const& info, std::vector<std::string>& out,
                          CollectionHandler* collections, MatchHandler* match, ArenaHandler* arena, LadderHandler* ladder)
    {
        m_limiter.Configure(settings.ratePerSecond, settings.rateBurst);
        if (!m_limiter.TryConsume(nowMs))
        {
            if (settings.enable && m_limiter.AllowNotice(nowMs))
                AppendError(out, kServerPushReqId, ErrorCode::Rate, "Too many requests, slow down.");
            return;
        }

        if (kPrefixLen + 1 + frame.size() > kMaxWireBytes)
        {
            if (settings.enable)
                AppendError(out, kServerPushReqId, ErrorCode::TooLarge, "Message exceeds the maximum frame size.");
            return;
        }

        Frame f;
        ParseResult const parsed = ParseFrame(frame, f);

        if (!settings.enable)
        {
            // Module disabled: only tell a greeting addon why nothing will happen.
            if (parsed.status == ParseStatus::Ok && f.op == "HELLO")
                AppendError(out, f.reqId, ErrorCode::Disabled, "Hearthstone is disabled on this server.");
            return;
        }

        if (parsed.status == ParseStatus::BadVersion)
        {
            AppendError(out, parsed.reqId, ErrorCode::Version,
                        "Protocol version mismatch: this server speaks v" + std::to_string(kProtocolVersion)
                        + ". Update the Hearthstone addon (or ask the server owner to update).");
            return;
        }
        if (parsed.status != ParseStatus::Ok)
        {
            AppendError(out, parsed.reqId, ErrorCode::Malformed, "Malformed frame.");
            return;
        }

        if (!m_handshaken && f.op != "HELLO" && f.op != "PING")
        {
            // One error per request: the first part answers, the others of a multi-part request are dropped.
            if (f.part == 1)
                AppendError(out, f.reqId, ErrorCode::NotReady, "Send HELLO first.");
            return;
        }

        ReassemblyLimits limits;
        limits.maxBytes = settings.maxInboundBytes;
        limits.maxParts = settings.maxInboundParts;
        limits.timeoutMs = settings.reassemblyTimeoutMs;
        limits.maxPending = settings.maxPendingRequests;

        Completed done;
        switch (m_reassembler.Add(nowMs, limits, f.op, f.reqId, f.part, f.total, f.payload, done))
        {
            case AddStatus::Complete:
                Dispatch(done, securityLevel, settings, info, out, collections, match, arena, ladder);
                break;
            case AddStatus::Pending:
            case AddStatus::Ignored:
                break;
            case AddStatus::Malformed:
                AppendError(out, f.reqId, ErrorCode::Malformed, "Inconsistent multi-part request.");
                break;
            case AddStatus::TooLarge:
                AppendError(out, f.reqId, ErrorCode::TooLarge, "Request is too large.");
                break;
            case AddStatus::Busy:
                AppendError(out, f.reqId, ErrorCode::Busy, "Too many unfinished requests.");
                break;
        }
    }

    void Session::Dispatch(Completed const& req, int securityLevel, Settings const& settings,
                           ServerInfo const& info, std::vector<std::string>& out, CollectionHandler* collections,
                           MatchHandler* match, ArenaHandler* arena, LadderHandler* ladder)
    {
        if (req.op == "LADDER_GET" || req.op == "LADDER_PLAY" || req.op == "LADDER_RES" || req.op == "LADDER_GIVE")
        {
            if (!ladder)
                AppendError(out, req.reqId, ErrorCode::Unavailable, "The ladder is not available.");
            else
                ladder->Defer(req);
            return;
        }
        if (req.op == "HELLO")
        {
            std::map<std::string, std::string> kv;
            if (!KvDecode(req.payload, kv) || kv.find("protocol") == kv.end())
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "HELLO payload is malformed.");
                return;
            }
            uint32_t protocol = 0;
            if (!ParseUint(kv["protocol"], 1000000, protocol))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "HELLO protocol is not a number.");
                return;
            }
            if (protocol != static_cast<uint32_t>(kProtocolVersion))
            {
                m_handshaken = false;
                AppendError(out, req.reqId, ErrorCode::Version,
                            "Protocol version mismatch: this server speaks v" + std::to_string(kProtocolVersion)
                            + ", your addon speaks v" + std::to_string(protocol)
                            + ". Update the Hearthstone addon (or ask the server owner to update).");
                return;
            }
            m_addonVersion = kv.count("addon") ? kv["addon"].substr(0, 32) : std::string();
            m_ladderUi = kv.count("ladderui") && kv["ladderui"] == "1";
            m_handshaken = true;
            // A fresh handshake means a fresh client (login, /reload): forget old request ids.
            m_reassembler.Clear();

            KvList w;
            w.push_back(std::make_pair("protocol", std::to_string(kProtocolVersion)));
            w.push_back(std::make_pair("server", info.moduleVersion));
            w.push_back(std::make_pair("dataset", info.datasetId.empty() ? std::string("-") : info.datasetId));
            w.push_back(std::make_pair("hash", info.datasetHash.empty() ? std::string("-") : info.datasetHash));
            std::string caps = settings.matchEnable ? "solo,chunk,packs,decks,wallet,match" : "solo,chunk,packs,decks,wallet";
            if (settings.matchEnable && info.arena)
                caps += ",arena";
            if (info.heroes)
                caps += ",heroes";
            w.push_back(std::make_pair("caps", caps));
            w.push_back(std::make_pair("maxframe", std::to_string(kMaxWireBytes)));
            // Only accounts that may actually use the debug ops are told debug is on.
            bool const debugForYou = settings.debug && securityLevel >= kDebugMinSecurity;
            w.push_back(std::make_pair("debug", std::string(debugForYou ? "1" : "0")));
            if (!info.intro.empty())
                w.push_back(std::make_pair("intro", info.intro));
            AppendReply(out, "WELCOME", req.reqId, KvEncode(w));
            return;
        }

        if (req.op == "PING")
        {
            if (req.payload.size() > kMaxPingPayload)
            {
                AppendError(out, req.reqId, ErrorCode::TooLarge, "PING payload too large.");
                return;
            }
            AppendReply(out, "PONG", req.reqId, req.payload);
            return;
        }

        if (req.op == "ECHO")
        {
            if (!settings.debug || securityLevel < kDebugMinSecurity)
            {
                AppendError(out, req.reqId, ErrorCode::NotImplemented, "Debug operations are not available to you.");
                return;
            }
            std::map<std::string, std::string> kv;
            if (!KvDecode(req.payload, kv))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "ECHO payload is malformed.");
                return;
            }
            if (kv.count("n"))
            {
                uint32_t n = 0;
                if (!ParseUint(kv["n"], kMaxEchoUnits, n) || n < 1)
                {
                    AppendError(out, req.reqId, ErrorCode::Malformed, "ECHO n must be 1.." + std::to_string(kMaxEchoUnits) + ".");
                    return;
                }
                std::string const data = MakePattern(static_cast<std::size_t>(n) * 100);
                KvList r;
                r.push_back(std::make_pair("n", std::to_string(n)));
                r.push_back(std::make_pair("len", std::to_string(data.size())));
                r.push_back(std::make_pair("sum", HexSum(Adler32(data))));
                r.push_back(std::make_pair("data", data));
                AppendReply(out, "ECHO", req.reqId, KvEncode(r));
                return;
            }
            if (kv.count("data"))
            {
                std::string const& data = kv["data"];
                KvList r;
                r.push_back(std::make_pair("len", std::to_string(data.size())));
                r.push_back(std::make_pair("sum", HexSum(Adler32(data))));
                AppendReply(out, "ECHO", req.reqId, KvEncode(r));
                return;
            }
            AppendError(out, req.reqId, ErrorCode::Malformed, "ECHO needs n= or data=.");
            return;
        }

        if (req.op == "COLL_GET")
        {
            // The payload is ignored (the addon sends it empty). The client never
            // sends counts: ownership comes only from the server's storage.
            Collection collection;
            if (!collections || !collections->GetCollection(collection))
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your card collection is not available right now. Try again later.");
                return;
            }
            AppendReply(out, "COLL", req.reqId, EncodeCollPayload(collection));
            return;
        }

        if (req.op == "PACK_GET")
        {
            // Payload ignored, like COLL_GET.
            PackView packs;
            if (!collections || !collections->GetPacks(packs))
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your card packs are not available right now. Try again later.");
                return;
            }
            AppendReply(out, "PACKS", req.reqId, EncodePacksPayload(packs.state, packs.recent));
            return;
        }

        if (req.op == "PACK_OPEN")
        {
            std::map<std::string, std::string> kv;
            if (!KvDecode(req.payload, kv) || !kv.count("key") || !IsValidPackKey(kv["key"]))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "PACK_OPEN needs key=<12-16 characters 0-9a-z>.");
                return;
            }
            if (!collections)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your card packs are not available right now. Nothing was opened; try again later.");
                return;
            }
            PackOpenResult const r = collections->OpenPack(kv["key"]);
            switch (r.status)
            {
                case PackStatus::Ok:
                    AppendReply(out, "PACK_OPENED", req.reqId, EncodePackOpenedPayload(r.opening, false, r.stockNow));
                    AppendCollChanged(out, r.opening.collectionVersion);
                    return;
                case PackStatus::Replay:
                    AppendReply(out, "PACK_OPENED", req.reqId, EncodePackOpenedPayload(r.opening, true, r.stockNow));
                    return;
                case PackStatus::NoStock:
                    AppendError(out, req.reqId, ErrorCode::NoStock, "You have no unopened packs.");
                    return;
                case PackStatus::BadKey:
                    AppendError(out, req.reqId, ErrorCode::Malformed, "PACK_OPEN needs key=<12-16 characters 0-9a-z>.");
                    return;
                case PackStatus::Refused:
                case PackStatus::BadCount:
                case PackStatus::Overflow:
                case PackStatus::NotEnough:
                    AppendError(out, req.reqId, ErrorCode::Refused,
                                std::string("This pack cannot be opened: ") + OwnershipErrorText(r.error)
                                + ". Nothing was changed.");
                    return;
                case PackStatus::Unavailable:
                case PackStatus::CommitFailed:
                    AppendError(out, req.reqId, ErrorCode::Unavailable,
                                "The pack could not be opened right now. Try again: the same request never opens a second pack.");
                    return;
            }
            return;
        }

        if (req.op == "DECK_LIST")
        {
            // Payload ignored, like COLL_GET.
            DeckList decks;
            if (!collections || !collections->ListDecks(decks))
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your decks are not available right now. Try again later.");
                return;
            }
            AppendReply(out, "DECKS", req.reqId, EncodeDecksPayload(decks));
            return;
        }

        if (req.op == "DECK_SAVE")
        {
            std::map<std::string, std::string> kv;
            DeckSaveRequest save;
            bool ok = KvDecode(req.payload, kv) && kv.count("key") && IsValidDeckKey(kv["key"]) &&
                kv.count("id") && ParseUint(kv["id"], kMaxDeckNumber, save.id) &&
                kv.count("rev") && ParseUint(kv["rev"], kMaxDeckNumber, save.baseRev) &&
                kv.count("class") && kv.count("name") && kv.count("cards") && DecodeDeckCards(kv["cards"], save.cards);
            // A new deck has no base revision.
            ok = ok && (save.id != 0 || save.baseRev == 0);
            if (!ok)
            {
                AppendError(out, req.reqId, ErrorCode::Malformed,
                            "DECK_SAVE needs key=<12-16 characters 0-9a-z>;id=;rev=;class=;name=;cards=<id>.<count>,...");
                return;
            }
            if (!collections)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your decks are not available right now. Nothing was saved; try again later.");
                return;
            }
            save.key = kv["key"];
            save.deckClass = kv["class"];
            save.name = kv["name"];
            DeckSaveResult const r = collections->SaveDeck(save);
            switch (r.status)
            {
                case DeckStatus::Ok:
                case DeckStatus::Replay:
                    AppendReply(out, "DECK_SAVED", req.reqId,
                                EncodeDeckSavedPayload(save.key, r.id, r.rev, r.status == DeckStatus::Replay, r.state));
                    return;
                case DeckStatus::Refused:
                    AppendError(out, req.reqId, ErrorCode::Refused,
                                std::string("The deck was not saved: ") + DeckWhyText(r.why) + ". Nothing was changed.",
                                DeckWhyCode(r.why));
                    return;
                case DeckStatus::BadKey:
                    AppendError(out, req.reqId, ErrorCode::Malformed, "DECK_SAVE needs key=<12-16 characters 0-9a-z>.");
                    return;
                case DeckStatus::Unavailable:
                case DeckStatus::CommitFailed:
                    AppendError(out, req.reqId, ErrorCode::Unavailable,
                                "The deck could not be saved right now. Try again: the same request never saves twice.");
                    return;
            }
            return;
        }

        if (req.op == "DECK_DELETE")
        {
            std::map<std::string, std::string> kv;
            DeckDeleteRequest del;
            bool const ok = KvDecode(req.payload, kv) && kv.count("key") && IsValidDeckKey(kv["key"]) &&
                kv.count("id") && ParseUint(kv["id"], kMaxDeckNumber, del.id) && del.id != 0 &&
                kv.count("rev") && ParseUint(kv["rev"], kMaxDeckNumber, del.baseRev);
            if (!ok)
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "DECK_DELETE needs key=<12-16 characters 0-9a-z>;id=;rev=");
                return;
            }
            if (!collections)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your decks are not available right now. Nothing was deleted; try again later.");
                return;
            }
            del.key = kv["key"];
            DeckDeleteResult const r = collections->DeleteDeck(del);
            switch (r.status)
            {
                case DeckStatus::Ok:
                case DeckStatus::Replay:
                    AppendReply(out, "DECK_DELETED", req.reqId,
                                EncodeDeckDeletedPayload(del.key, del.id, r.status == DeckStatus::Replay));
                    return;
                case DeckStatus::Refused:
                    AppendError(out, req.reqId, ErrorCode::Refused,
                                std::string("The deck was not deleted: ") + DeckWhyText(r.why) + ". Nothing was changed.",
                                DeckWhyCode(r.why));
                    return;
                case DeckStatus::BadKey:
                    AppendError(out, req.reqId, ErrorCode::Malformed, "DECK_DELETE needs key=<12-16 characters 0-9a-z>.");
                    return;
                case DeckStatus::Unavailable:
                case DeckStatus::CommitFailed:
                    AppendError(out, req.reqId, ErrorCode::Unavailable,
                                "The deck could not be deleted right now. Try again: the same request never deletes twice.");
                    return;
            }
            return;
        }

        if (req.op == "WALLET_GET")
        {
            // Payload ignored, like COLL_GET.
            WalletView view;
            if (!collections || !collections->GetWallet(view))
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Your coins and dust are not available right now. Try again later.");
                return;
            }
            AppendReply(out, "WALLET", req.reqId, EncodeWalletPayload(view));
            return;
        }

        if (req.op == "HERO_GET" || req.op == "HERO_SELECT" || req.op == "HERO_UNLOCK")
        {
            std::map<std::string, std::string> kv;
            bool const decoded = KvDecode(req.payload, kv);
            Heroes::HeroResult r;
            if (req.op == "HERO_GET")
            {
                // Payload ignored, like COLL_GET.
                r = collections ? collections->GetHeroes() : Heroes::HeroResult();
            }
            else if (req.op == "HERO_SELECT")
            {
                if (!decoded || !kv.count("class") || !kv.count("id") || kv["id"].empty() || kv["id"].size() > Heroes::kMaxIdLen)
                {
                    AppendError(out, req.reqId, ErrorCode::Malformed, "HERO_SELECT needs class=<CLASS>;id=<id|default>");
                    return;
                }
                r = collections ? collections->SelectHero(kv["class"], kv["id"]) : Heroes::HeroResult();
            }
            else
            {
                if (!decoded || !kv.count("key") || !Heroes::ValidKey(kv["key"]) || !kv.count("id") || kv["id"].empty() ||
                    kv["id"].size() > Heroes::kMaxIdLen)
                {
                    AppendError(out, req.reqId, ErrorCode::Malformed,
                                "HERO_UNLOCK needs key=<12-16 characters 0-9a-z>;id=<id>");
                    return;
                }
                r = collections ? collections->UnlockHero(kv["key"], kv["id"]) : Heroes::HeroResult();
            }
            switch (r.status)
            {
                case Heroes::HeroStatus::Ok:
                case Heroes::HeroStatus::Replay:
                    AppendReply(out, "HEROES", req.reqId, Heroes::EncodeHeroesPayload(r.state));
                    return;
                case Heroes::HeroStatus::Refused:
                    AppendError(out, req.reqId, ErrorCode::Refused,
                                std::string("The portrait request was refused: ") + Heroes::RefusalWhy(r.why) +
                                    ". Nothing was changed.", Heroes::RefusalWhy(r.why));
                    return;
                case Heroes::HeroStatus::Unavailable:
                    AppendError(out, req.reqId, ErrorCode::Unavailable,
                                "Your hero portraits are not available right now. Nothing was changed; try again later.");
                    return;
            }
            return;
        }

        if (req.op == "CRAFT_EXTRAS")
        {
            std::map<std::string, std::string> kv;
            if (!KvDecode(req.payload, kv) || !kv.count("key") || !IsValidCraftKey(kv["key"]))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed,
                            "CRAFT_EXTRAS needs key=<12-16 characters 0-9a-z>");
                return;
            }
            if (!collections)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Crafting is not available right now. Nothing was changed; try again later.");
                return;
            }
            AppendCraftResult(out, req.reqId, "disenchanted", collections->ScrapExtras(kv["key"]), true);
            return;
        }

        if (req.op == "CRAFT_MAKE" || req.op == "CRAFT_SCRAP")
        {
            bool const scrap = req.op == "CRAFT_SCRAP";
            std::map<std::string, std::string> kv;
            uint32_t count = 1;
            bool const ok = KvDecode(req.payload, kv) && kv.count("key") && IsValidCraftKey(kv["key"]) &&
                kv.count("card") && !kv["card"].empty() && kv["card"].size() <= kMaxCardIdLen &&
                (!scrap || (kv.count("n") && ParseUint(kv["n"], kMaxScrapPerOp, count) && count >= 1));
            if (!ok)
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, scrap
                    ? "CRAFT_SCRAP needs key=<12-16 characters 0-9a-z>;card=<card id>;n=<1-99>"
                    : "CRAFT_MAKE needs key=<12-16 characters 0-9a-z>;card=<card id>");
                return;
            }
            if (!collections)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable,
                            "Crafting is not available right now. Nothing was changed; try again later.");
                return;
            }
            CraftResult const r = scrap ? collections->ScrapCard(kv["key"], kv["card"], count)
                                        : collections->CraftCard(kv["key"], kv["card"]);
            AppendCraftResult(out, req.reqId, scrap ? "scrapped" : "crafted", r);
            return;
        }

        if (req.op == "GAME_GET")
        {
            // Payload ignored, like COLL_GET: a resync of the current state.
            if (!match)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable, "Card matches are not available on this server.");
                return;
            }
            AppendReply(out, "GAME", req.reqId, match->GameGet());
            return;
        }

        if (req.op == "GAME_ACT")
        {
            GameActRequest act;
            if (!DecodeGameAct(req.payload, act))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed,
                            "GAME_ACT needs mid=;seq=;a=mull|play|atk|hp|end|concede[;s=][;t=][;p=][;r=<id>,<id>]");
                return;
            }
            if (!match)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable, "Card matches are not available on this server.");
                return;
            }
            GameReply const r = match->GameAct(act);
            if (r.ok)
                AppendReply(out, "GAME", req.reqId, r.payload);
            else
                AppendError(out, req.reqId, ErrorCode::Refused, GameWhyText(r.why), r.why);
            return;
        }

        if (req.op == "GAME_LEAVE")
        {
            uint32_t matchId = 0;
            if (!DecodeGameLeave(req.payload, matchId))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "GAME_LEAVE needs mid=<match id>");
                return;
            }
            if (!match)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable, "Card matches are not available on this server.");
                return;
            }
            GameReply const r = match->GameLeave(matchId);
            if (r.ok)
                AppendReply(out, "GAME", req.reqId, r.payload);
            else
                AppendError(out, req.reqId, ErrorCode::Refused, GameWhyText(r.why), r.why);
            return;
        }

        if (req.op == "GAME_PAUSE")
        {
            uint32_t matchId = 0;
            if (!DecodeGameLeave(req.payload, matchId))
            {
                AppendError(out, req.reqId, ErrorCode::Malformed, "GAME_PAUSE needs mid=<match id>");
                return;
            }
            if (!match)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable, "Card matches are not available on this server.");
                return;
            }
            GameReply const r = match->GamePause(matchId);
            if (r.ok)
                AppendReply(out, "GAME", req.reqId, r.payload);
            else
                AppendError(out, req.reqId, ErrorCode::Refused, GameWhyText(r.why), r.why);
            return;
        }

        if (req.op == kOpArenaGet || req.op == kOpArenaClass || req.op == kOpArenaPick || req.op == kOpArenaClaim ||
            req.op == kOpArenaDuel || req.op == kOpArenaRetire)
        {
            bool const mutating = req.op != kOpArenaGet;
            ArenaRequest request;
            if (mutating && !DecodeArenaRequest(req.op, req.payload, request))
            {
                std::string const usage = req.op == kOpArenaClass ? "ARENA_CLASS needs key=<12-16 characters 0-9a-z>;class=<CLASS>[;run=<id>]"
                    : req.op == kOpArenaPick ? "ARENA_PICK needs key=<12-16 characters 0-9a-z>;no=<1-30>;id=<card id>[;run=<id>]"
                    : req.op == kOpArenaClaim ? "ARENA_CLAIM needs key=<12-16 characters 0-9a-z>;id=<card id>[;run=<id>]"
                    : req.op + " needs key=<12-16 characters 0-9a-z>[;run=<id>]";
                AppendError(out, req.reqId, ErrorCode::Malformed, usage);
                return;
            }
            if (!arena)
            {
                AppendError(out, req.reqId, ErrorCode::Unavailable, "The arena is not available on this server.");
                return;
            }
            request.reqId = req.reqId;
            ArenaReply const reply = mutating ? arena->Mutate(request) : arena->Get(req.reqId);
            AppendArenaReply(out, req.reqId, reply);
            return;
        }

        if (IsReservedOp(req.op))
        {
            AppendError(out, req.reqId, ErrorCode::NotImplemented, "This operation is reserved for a later phase.");
            return;
        }

        AppendError(out, req.reqId, ErrorCode::UnknownOp, "Unknown operation.");
    }
}
