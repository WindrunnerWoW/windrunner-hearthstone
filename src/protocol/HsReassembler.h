/*
 * Bounded inbound reassembly of multi-part requests for ONE player session.
 * Pure C++ (no core headers). The owner passes a monotonic millisecond clock.
 */

#ifndef MOD_HEARTHSTONE_HS_REASSEMBLER_H
#define MOD_HEARTHSTONE_HS_REASSEMBLER_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace Hs
{
    struct ReassemblyLimits
    {
        std::size_t maxBytes = 4096;    // reassembled (decoded) payload bytes
        uint32_t maxParts = 16;         // highest accepted "total"
        uint32_t timeoutMs = 10000;     // lifetime of an incomplete request
        uint32_t maxPending = 4;        // incomplete requests at the same time
    };

    enum class AddStatus
    {
        Complete,   // request finished, Completed filled
        Pending,    // stored, waiting for more parts
        Ignored,    // duplicate part, or late part of an already finished/dropped request
        Malformed,  // bad part/total, or total/op conflicts with the pending request (request dropped)
        TooLarge,   // total > maxParts, or cumulative bytes > maxBytes (request dropped)
        Busy        // too many incomplete requests (new request rejected)
    };

    struct Completed
    {
        std::string op;
        std::string reqId;
        std::string payload;
    };

    class Reassembler
    {
    public:
        // Completed / dropped reqIds remembered so that late duplicate parts are
        // ignored instead of being handled as a new request.
        static constexpr std::size_t kCompletedCapacity = 16;

        AddStatus Add(uint64_t nowMs, ReassemblyLimits const& limits, std::string const& op,
                      std::string const& reqId, uint32_t part, uint32_t total,
                      std::string const& payload, Completed& out);

        // Drops requests older than limits.timeoutMs. Their reqIds are appended to expired.
        std::size_t Sweep(uint64_t nowMs, uint32_t timeoutMs, std::vector<std::string>& expired);

        std::size_t PendingCount() const { return m_pending.size(); }
        bool IsRemembered(std::string const& reqId) const;
        void Clear();

    private:
        struct Pending
        {
            std::string op;
            uint32_t total = 0;
            uint32_t received = 0;
            std::size_t bytes = 0;
            uint64_t startedMs = 0;
            std::vector<std::string> parts;
            std::vector<char> have;
        };

        void Remember(std::string const& reqId);

        std::map<std::string, Pending> m_pending;
        std::deque<std::string> m_completed;
    };
}

#endif
