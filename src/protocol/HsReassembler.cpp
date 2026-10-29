#include "HsReassembler.h"

#include <algorithm>

namespace Hs
{
    bool Reassembler::IsRemembered(std::string const& reqId) const
    {
        return std::find(m_completed.begin(), m_completed.end(), reqId) != m_completed.end();
    }

    void Reassembler::Remember(std::string const& reqId)
    {
        auto const it = std::find(m_completed.begin(), m_completed.end(), reqId);
        if (it != m_completed.end())
            m_completed.erase(it);
        m_completed.push_back(reqId);
        while (m_completed.size() > kCompletedCapacity)
            m_completed.pop_front();
    }

    void Reassembler::Clear()
    {
        m_pending.clear();
        m_completed.clear();
    }

    AddStatus Reassembler::Add(uint64_t nowMs, ReassemblyLimits const& limits, std::string const& op,
                               std::string const& reqId, uint32_t part, uint32_t total,
                               std::string const& payload, Completed& out)
    {
        if (total < 1 || part < 1 || part > total)
            return AddStatus::Malformed;

        // A request that was already rejected / finished stays quiet: one error per reqId.
        if (total > 1 && IsRemembered(reqId))
            return AddStatus::Ignored;

        if (total > limits.maxParts)
        {
            m_pending.erase(reqId);
            Remember(reqId);
            return AddStatus::TooLarge;
        }

        // Single frame requests never touch the pending table or the remembered
        // list (a client may legitimately reuse a reqId for a new single request).
        if (total == 1)
        {
            if (payload.size() > limits.maxBytes)
                return AddStatus::TooLarge;
            out.op = op;
            out.reqId = reqId;
            out.payload = payload;
            return AddStatus::Complete;
        }

        auto it = m_pending.find(reqId);
        if (it == m_pending.end())
        {
            if (m_pending.size() >= limits.maxPending)
            {
                Remember(reqId);
                return AddStatus::Busy;
            }
            Pending p;
            p.op = op;
            p.total = total;
            p.startedMs = nowMs;
            p.parts.assign(total, std::string());
            p.have.assign(total, 0);
            it = m_pending.insert(std::make_pair(reqId, std::move(p))).first;
        }
        Pending& p = it->second;

        if (p.total != total || p.op != op)
        {
            m_pending.erase(it);
            Remember(reqId);
            return AddStatus::Malformed;
        }

        if (p.have[part - 1])
            return AddStatus::Ignored; // duplicate part: idempotent

        if (p.bytes + payload.size() > limits.maxBytes)
        {
            m_pending.erase(it);
            Remember(reqId);
            return AddStatus::TooLarge;
        }

        p.parts[part - 1] = payload;
        p.have[part - 1] = 1;
        p.bytes += payload.size();
        ++p.received;

        if (p.received < p.total)
            return AddStatus::Pending;

        out.op = p.op;
        out.reqId = reqId;
        out.payload.clear();
        out.payload.reserve(p.bytes);
        for (std::string const& s : p.parts)
            out.payload += s;
        m_pending.erase(it);
        Remember(reqId);
        return AddStatus::Complete;
    }

    std::size_t Reassembler::Sweep(uint64_t nowMs, uint32_t timeoutMs, std::vector<std::string>& expired)
    {
        std::size_t n = 0;
        for (auto it = m_pending.begin(); it != m_pending.end();)
        {
            if (nowMs >= it->second.startedMs && nowMs - it->second.startedMs >= timeoutMs)
            {
                expired.push_back(it->first);
                Remember(it->first);
                it = m_pending.erase(it);
                ++n;
            }
            else
                ++it;
        }
        return n;
    }
}
