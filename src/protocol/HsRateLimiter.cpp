#include "HsRateLimiter.h"

namespace Hs
{
    void RateLimiter::Configure(uint32_t perSecond, uint32_t burst)
    {
        m_perSecond = perSecond < 1 ? 1 : perSecond;
        m_burst = burst < 1 ? 1 : burst;
        uint64_t const cap = static_cast<uint64_t>(m_burst) * 1000;
        if (m_milliTokens > cap)
            m_milliTokens = cap;
    }

    bool RateLimiter::TryConsume(uint64_t nowMs)
    {
        uint64_t const cap = static_cast<uint64_t>(m_burst) * 1000;
        if (!m_started)
        {
            m_started = true;
            m_milliTokens = cap;
            m_lastMs = nowMs;
        }
        else
        {
            uint64_t const elapsed = nowMs > m_lastMs ? nowMs - m_lastMs : 0;
            m_lastMs = nowMs > m_lastMs ? nowMs : m_lastMs;
            m_milliTokens += elapsed * m_perSecond; // ms * tokens/s == milli-tokens
            if (m_milliTokens > cap)
                m_milliTokens = cap;
        }
        if (m_milliTokens < 1000)
            return false;
        m_milliTokens -= 1000;
        return true;
    }

    bool RateLimiter::AllowNotice(uint64_t nowMs)
    {
        if (m_noticed && nowMs >= m_lastNoticeMs && nowMs - m_lastNoticeMs < 1000)
            return false;
        m_noticed = true;
        m_lastNoticeMs = nowMs;
        return true;
    }
}
