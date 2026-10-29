/*
 * Token bucket, one instance per player session. Pure C++.
 * Time is a monotonic millisecond value supplied by the caller.
 */

#ifndef MOD_HEARTHSTONE_HS_RATE_LIMITER_H
#define MOD_HEARTHSTONE_HS_RATE_LIMITER_H

#include <cstdint>

namespace Hs
{
    class RateLimiter
    {
    public:
        // Cheap; may be called before every use so config reloads take effect.
        void Configure(uint32_t perSecond, uint32_t burst);

        // Takes one token. False when the bucket is empty (drop the message).
        bool TryConsume(uint64_t nowMs);

        // True at most once per second: gate for the "ERR RATE" reply.
        bool AllowNotice(uint64_t nowMs);

    private:
        uint32_t m_perSecond = 10;
        uint32_t m_burst = 30;
        uint64_t m_milliTokens = 0;
        uint64_t m_lastMs = 0;
        bool m_started = false;
        uint64_t m_lastNoticeMs = 0;
        bool m_noticed = false;
    };
}

#endif
