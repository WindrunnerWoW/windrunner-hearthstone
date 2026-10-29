/*
 * mod-hearthstone configuration cache.
 *
 * Values are read from sConfig in WorldScript::OnStartup (the initial config
 * load happens before module scripts exist) and again from
 * OnAfterConfigLoad(reload = true).
 */

#ifndef MOD_HEARTHSTONE_CONFIG_H
#define MOD_HEARTHSTONE_CONFIG_H

#include "arena/HsArenaRules.h"
#include "collection/HsPacks.h"
#include "collection/HsWallet.h"
#include "decks/HsDecks.h"
#include "match/HsMatchService.h"
#include "protocol/HsSession.h"

#include <mutex>
#include <vector>

namespace Hs
{
    // Module version reported in WELCOME and .hs status.
    constexpr char const* kModuleVersion = "0.1.0";

    // Settings of the match service.
    struct MatchSettings
    {
        bool enable = true;                           // Hearthstone.Match.Enable
        uint32_t aiDelayMs = kDefaultAiDelayMs;       // Hearthstone.Match.AiActionDelayMs
        bool historyEnable = true;                    // Hearthstone.Match.History.Enable
        // Hearthstone.Match.Opponents: development opponents "<entry>:<class>" (the nine classes);
        // they play normal_<class> / hard_<class> of the deck library. The hearthstone_opponent rows of
        // the world database win for the same entry (HsMatchGlue.cpp builds the registry).
        std::vector<MatchOpponent> opponents;
    };

    // World NPC Ladder (Hearthstone.Ladder.*), read once at startup (Threads also at every round).
    struct LadderSettings
    {
        bool enable = false;           // Hearthstone.Ladder.Enable
        uint32_t startRounds = 20;     // Hearthstone.Ladder.StartRounds (the rating-only rounds of the ladder start)
        uint32_t threads = 2;          // Hearthstone.Ladder.Threads (worker threads that play the games of a round)
        uint32_t chairGlowSpellId = 25824; // Hearthstone.Ladder.ChairGlowSpellId (0: no glow on the chair)
    };
    constexpr uint32_t kMaxLadderStartRounds = 200;

    // Economy.
    struct EconomySettings
    {
        DustValues dust;                 // Hearthstone.Dust.Scrap.* / Hearthstone.Dust.Craft.*
        MatchRewards rewards;            // Hearthstone.Coins.Loss / WinNormal / WinHard / WinBoss
        // Hearthstone.Pack.PriceCoins: Hearthstone Coin items for one pack at a vendor (the gold price is the buy_price of
        // the pack item, world migration 0006).
        uint32_t packPriceCoins = 10;
        uint32_t traderSwapSeconds = 4 * 3600; // Hearthstone.Trader.SwapTime (HH:MM UTC)
        std::string traderExcludedCards;       // canonical card ids, commas or whitespace
    };
}

class HearthstoneConfig
{
public:
    static HearthstoneConfig& Instance();

    void Load(bool reload);
    Hs::Settings Get() const;
    Hs::PackOdds GetPackOdds() const;
    // Hearthstone.Deck.MaxSaved, 1..Hs::kMaxSavedDecksLimit.
    uint32_t GetDeckMaxSaved() const;
    Hs::MatchSettings GetMatch() const;
    Hs::EconomySettings GetEconomy() const;
    // Draft arena (Hearthstone.Arena.*), sanitized.
    Hs::ArenaSettings GetArena() const;
    // Ladder (Hearthstone.Ladder.*), sanitized.
    Hs::LadderSettings GetLadder() const;

private:
    HearthstoneConfig() = default;

    mutable std::mutex m_mutex;
    Hs::Settings m_settings;
    Hs::PackOdds m_packOdds;
    uint32_t m_deckMaxSaved = Hs::kDefaultMaxSavedDecks;
    Hs::MatchSettings m_match;
    Hs::EconomySettings m_economy;
    Hs::ArenaSettings m_arena;
    Hs::LadderSettings m_ladder;
};

#define sHearthstoneConfig HearthstoneConfig::Instance()

#endif
