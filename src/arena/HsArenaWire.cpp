#include "HsArenaWire.h"

#include "../protocol/HsProtocol.h"

#include <map>

namespace Hs
{
    namespace
    {
        bool ParseU32(std::string const& s, uint32_t maxValue, uint32_t& out)
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

        // Card ids and class names: [A-Za-z0-9_], 1..32 characters.
        bool IsIdent(std::string const& s)
        {
            if (s.empty() || s.size() > 32)
                return false;
            for (char c : s)
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
                    return false;
            return true;
        }

        std::string Join(std::vector<std::string> const& items)
        {
            std::string out;
            for (std::string const& item : items)
                out += (out.empty() ? "" : ",") + item;
            return out;
        }

        std::string JoinCounts(std::vector<std::pair<std::string, uint32_t>> const& items)
        {
            std::string out;
            for (auto const& item : items)
                out += (out.empty() ? "" : ",") + item.first + "." + std::to_string(item.second);
            return out;
        }
    }

    bool IsValidArenaKey(std::string const& key)
    {
        if (key.size() < kMinArenaKeyLen || key.size() > kMaxArenaKeyLen)
            return false;
        for (char c : key)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
                return false;
        return true;
    }

    std::string EncodeArenaPayload(ArenaView const& v, bool replay)
    {
        KvList kv;
        kv.push_back({ "st", v.state });
        kv.push_back({ "run", std::to_string(v.runId) });
        kv.push_back({ "en", v.enabled ? "1" : "0" });
        kv.push_back({ "pay", v.pay });
        kv.push_back({ "cls", v.deckClass });
        kv.push_back({ "w", std::to_string(v.wins) });
        kv.push_back({ "d", std::to_string(v.draws) });
        kv.push_back({ "l", std::to_string(v.losses) });
        kv.push_back({ "duel", std::to_string(v.duels) });
        kv.push_back({ "no", v.offerNo < 0 ? std::string() : std::to_string(v.offerNo) });
        kv.push_back({ "of", Join(v.offer) });
        kv.push_back({ "pk", std::to_string(v.picked) });
        kv.push_back({ "dk", JoinCounts(v.deck) });
        kv.push_back({ "pz", std::to_string(v.prizePacks) + "." + std::to_string(v.prizeCoins) + "." +
                                 (v.prizeSpecial ? "1" : "0") });
        kv.push_back({ "tb", std::to_string(v.basePacks) + "." + std::to_string(v.bonusCoins) });
        kv.push_back({ "spc", v.specialPending ? "1" : "0" });
        kv.push_back({ "sp", JoinCounts(v.specials) });
        kv.push_back({ "sc", v.claimed });
        kv.push_back({ "live", v.live ? "1" : "0" });
        kv.push_back({ "mine", v.mine ? "1" : "0" });
        kv.push_back({ "rp", replay ? "1" : "0" });
        return KvEncode(kv);
    }

    bool DecodeArenaRequest(std::string const& op, std::string const& payload, ArenaRequest& out)
    {
        std::map<std::string, std::string> kv;
        if (!KvDecode(payload, kv))
            return false;
        ArenaRequest req;
        req.op = op;
        auto const take = [&kv](char const* name, std::string& value) {
            auto const it = kv.find(name);
            if (it == kv.end())
                return false;
            value = it->second;
            kv.erase(it);
            return true;
        };
        std::string text;
        if (!take("key", req.key) || !IsValidArenaKey(req.key))
            return false;
        if (take("run", text) && !ParseU32(text, 999999999, req.runId))
            return false;
        if (op == kOpArenaClass)
        {
            if (!take("class", req.deckClass) || !IsIdent(req.deckClass))
                return false;
        }
        else if (op == kOpArenaPick)
        {
            if (!take("no", text) || !ParseU32(text, 30, req.pickNo) || req.pickNo < 1)
                return false;
            if (!take("id", req.cardId) || !IsIdent(req.cardId))
                return false;
        }
        else if (op == kOpArenaClaim)
        {
            if (!take("id", req.cardId) || !IsIdent(req.cardId))
                return false;
        }
        else if (op != kOpArenaDuel && op != kOpArenaRetire)
            return false;
        if (!kv.empty())
            return false;
        out = req;
        return true;
    }

    void AppendArenaReply(std::vector<std::string>& out, std::string const& reqId, ArenaReply const& reply)
    {
        std::vector<std::string> frames;
        switch (reply.status)
        {
            case ArenaReply::Status::Ok:
            case ArenaReply::Status::Replay:
                if (ChunkPayload("ARENA", reqId, EncodeArenaPayload(reply.view, reply.status == ArenaReply::Status::Replay),
                                 frames))
                    for (std::string& f : frames)
                        out.push_back(std::move(f));
                return;
            case ArenaReply::Status::Refused:
            case ArenaReply::Status::Unavailable:
            {
                KvList kv;
                bool const refused = reply.status == ArenaReply::Status::Refused;
                kv.push_back({ "code", ErrorCodeName(refused ? ErrorCode::Refused : ErrorCode::Unavailable) });
                kv.push_back({ "msg", reply.msg });
                if (refused && !reply.why.empty())
                    kv.push_back({ "why", reply.why });
                std::string const id = IsValidReqId(reqId) ? reqId : std::string(kServerPushReqId);
                if (ChunkPayload("ERR", id, KvEncode(kv), frames))
                    for (std::string& f : frames)
                        out.push_back(std::move(f));
                return;
            }
            case ArenaReply::Status::Deferred:
                return;
        }
    }

    void AppendArenaUpdate(std::vector<std::string>& out, ArenaView const& view)
    {
        std::vector<std::string> frames;
        if (ChunkPayload("ARENA_UPD", kServerPushReqId, EncodeArenaPayload(view, false), frames))
            for (std::string& f : frames)
                out.push_back(std::move(f));
    }

    void AppendArenaOpen(std::vector<std::string>& out, ArenaView const& view)
    {
        std::vector<std::string> frames;
        if (ChunkPayload("ARENA_OPEN", kServerPushReqId, EncodeArenaPayload(view, false), frames))
            for (std::string& f : frames)
                out.push_back(std::move(f));
    }

    void AppendArenaClose(std::vector<std::string>& out)
    {
        std::vector<std::string> frames;
        if (ChunkPayload("ARENA_CLOSE", kServerPushReqId, std::string(), frames))
            for (std::string& f : frames)
                out.push_back(std::move(f));
    }
}
