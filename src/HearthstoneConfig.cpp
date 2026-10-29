#include "HearthstoneConfig.h"
#include "collection/HsTraderRules.h"

#include "Config/Config.h"
#include "Log.h"

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

namespace
{
    uint32_t ReadUint(char const* key, uint32_t def, uint32_t maxValue)
    {
        int32 const raw = sConfig.GetIntDefault(key, static_cast<int32>(def));
        if (raw < 0)
        {
            sLog.outError("Hearthstone: %s must not be negative (%d); using %u", key, static_cast<int>(raw), def);
            return def;
        }
        return std::min(static_cast<uint32_t>(raw), maxValue);
    }
}

HearthstoneConfig& HearthstoneConfig::Instance()
{
    static HearthstoneConfig instance;
    return instance;
}

Hs::Settings HearthstoneConfig::Get() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_settings;
}

Hs::PackOdds HearthstoneConfig::GetPackOdds() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_packOdds;
}

uint32_t HearthstoneConfig::GetDeckMaxSaved() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_deckMaxSaved;
}

Hs::MatchSettings HearthstoneConfig::GetMatch() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_match;
}

Hs::EconomySettings HearthstoneConfig::GetEconomy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_economy;
}

Hs::ArenaSettings HearthstoneConfig::GetArena() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_arena;
}

Hs::LadderSettings HearthstoneConfig::GetLadder() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ladder;
}

void HearthstoneConfig::Load(bool reload)
{
    Hs::Settings s;
    s.enable = sConfig.GetBoolDefault("Hearthstone.Enable", true);
    s.debug = sConfig.GetBoolDefault("Hearthstone.Debug", false);
    s.maxInboundBytes = ReadUint("Hearthstone.Protocol.MaxInboundMessageBytes", 4096, 1000000);
    s.maxInboundParts = ReadUint("Hearthstone.Protocol.MaxInboundParts", 16, 1000);
    s.reassemblyTimeoutMs = ReadUint("Hearthstone.Protocol.ReassemblyTimeoutMs", 10000, 100000000);
    s.maxPendingRequests = ReadUint("Hearthstone.Protocol.MaxPendingRequests", 4, 1000);
    s.ratePerSecond = ReadUint("Hearthstone.Protocol.RateLimit.PerSecond", 10, 100000);
    s.rateBurst = ReadUint("Hearthstone.Protocol.RateLimit.Burst", 30, 100000);
    s.matchAuraSpellId = ReadUint("Hearthstone.MatchAuraSpellId", 0, 0xFFFFFFFFu);
    s.matchEnable = sConfig.GetBoolDefault("Hearthstone.Match.Enable", true);
    {
        // The payout: the base for everyone, the winning faction's total and the first place's total.
        s.bgRewardBase = static_cast<int>(ReadUint("Hearthstone.Battlegrounds.Reward.Base", 3, 1000000));
        s.bgRewardTeam = static_cast<int>(ReadUint("Hearthstone.Battlegrounds.Reward.WinningTeam", 5, 1000000));
        s.bgRewardFirst = static_cast<int>(ReadUint("Hearthstone.Battlegrounds.Reward.First", 7, 1000000));
        s.bgHonorBase = static_cast<int>(ReadUint("Hearthstone.Battlegrounds.Honor.Base", 10, 1000000));
        s.bgHonorTeam = static_cast<int>(ReadUint("Hearthstone.Battlegrounds.Honor.WinningTeam", 50, 1000000));
        s.bgHonorFirst = static_cast<int>(ReadUint("Hearthstone.Battlegrounds.Honor.First", 70, 1000000));
    }
    s = Hs::Sanitize(s);

    Hs::MatchSettings match;
    match.enable = s.matchEnable;
    match.aiDelayMs = ReadUint("Hearthstone.Match.AiActionDelayMs", Hs::kDefaultAiDelayMs, 100000000);
    if (match.aiDelayMs < Hs::kMinAiDelayMs || match.aiDelayMs > Hs::kMaxAiDelayMs)
    {
        uint32_t const clamped = std::min(std::max(match.aiDelayMs, Hs::kMinAiDelayMs), Hs::kMaxAiDelayMs);
        sLog.outError("Hearthstone: Hearthstone.Match.AiActionDelayMs must be %u..%u (%u); using %u", Hs::kMinAiDelayMs,
            Hs::kMaxAiDelayMs, match.aiDelayMs, clamped);
        match.aiDelayMs = clamped;
    }
    match.historyEnable = sConfig.GetBoolDefault("Hearthstone.Match.History.Enable", true);
    std::vector<std::string> opponentErrors;
    Hs::ParseMatchOpponents(sConfig.GetStringDefault("Hearthstone.Match.Opponents", ""), match.opponents, opponentErrors);
    for (std::string const& error : opponentErrors)
        sLog.outError("Hearthstone: Hearthstone.Match.Opponents: %s", error.c_str());

    Hs::PackOdds const defaults;
    Hs::PackOdds odds;
    odds.weight[0] = ReadUint("Hearthstone.Pack.Weight.Common", defaults.weight[0], 1000000);
    odds.weight[1] = ReadUint("Hearthstone.Pack.Weight.Rare", defaults.weight[1], 1000000);
    odds.weight[2] = ReadUint("Hearthstone.Pack.Weight.Epic", defaults.weight[2], 1000000);
    odds.weight[3] = ReadUint("Hearthstone.Pack.Weight.Legendary", defaults.weight[3], 1000000);
    odds.legendaryPity = ReadUint("Hearthstone.Pack.LegendaryPity", defaults.legendaryPity, 1000);
    if (odds.weight[0] + odds.weight[1] + odds.weight[2] + odds.weight[3] == 0)
        sLog.outError("Hearthstone: every Hearthstone.Pack.Weight.* is 0; using the defaults");
    odds = Hs::SanitizePackOdds(odds);

    uint32_t deckMaxSaved = ReadUint("Hearthstone.Deck.MaxSaved", Hs::kDefaultMaxSavedDecks, 1000000);
    if (deckMaxSaved < 1 || deckMaxSaved > Hs::kMaxSavedDecksLimit)
    {
        uint32_t const clamped = std::min(std::max<uint32_t>(deckMaxSaved, 1), Hs::kMaxSavedDecksLimit);
        sLog.outError("Hearthstone: Hearthstone.Deck.MaxSaved must be 1..%u (%u); using %u", Hs::kMaxSavedDecksLimit,
            deckMaxSaved, clamped);
        deckMaxSaved = clamped;
    }

    // Dust values (2014 defaults), match coins and the pack prices.
    Hs::EconomySettings economy;
    {
        Hs::DustValues const d;
        static char const* const kRarity[] = { "Common", "Rare", "Epic", "Legendary" };
        for (std::size_t i = 0; i < 4; ++i)
        {
            std::string const scrapKey = std::string("Hearthstone.Dust.Scrap.") + kRarity[i];
            std::string const craftKey = std::string("Hearthstone.Dust.Craft.") + kRarity[i];
            economy.dust.scrap[i] = ReadUint(scrapKey.c_str(), d.scrap[i], 1000000);
            economy.dust.craft[i] = ReadUint(craftKey.c_str(), d.craft[i], 1000000);
        }
        economy.dust = Hs::SanitizeDustValues(economy.dust);
        Hs::MatchRewards const r;
        economy.rewards.loss = ReadUint("Hearthstone.Coins.Loss", r.loss, 1000);
        economy.rewards.winNormal = ReadUint("Hearthstone.Coins.WinNormal", r.winNormal, 1000);
        economy.rewards.winHard = ReadUint("Hearthstone.Coins.WinHard", r.winHard, 1000);
        economy.rewards.winBoss = ReadUint("Hearthstone.Coins.WinBoss", r.winBoss, 1000);
        economy.rewards = Hs::SanitizeMatchRewards(economy.rewards);
        // at least 1 (a free pack would be a bug), at most a few stacks
        economy.packPriceCoins = ReadUint("Hearthstone.Pack.PriceCoins", 10, 1000);
        if (economy.packPriceCoins == 0)
            economy.packPriceCoins = 1;
        std::string const swap = sConfig.GetStringDefault("Hearthstone.Trader.SwapTime", "04:00");
        if (!Hs::ParseTraderSwapTime(swap, economy.traderSwapSeconds))
            sLog.outError("Hearthstone: Hearthstone.Trader.SwapTime must be HH:MM UTC, using 04:00");
        economy.traderExcludedCards = sConfig.GetStringDefault("Hearthstone.Trader.ExcludedCards", "");
    }

    // Draft arena: out of range values are clamped (and logged) by SanitizeArenaSettings' bounds.
    Hs::ArenaSettings arena;
    {
        Hs::ArenaSettings const d;
        arena.entryCostCopper = ReadUint("Hearthstone.Arena.EntryCostCopper", d.entryCostCopper, 0xFFFFFFFFu);
        arena.entryCostCoins = ReadUint("Hearthstone.Arena.EntryCostCoins", d.entryCostCoins, 0xFFFFFFFFu);
        arena.baseRewardPacks = ReadUint("Hearthstone.Arena.BaseRewardPacks", d.baseRewardPacks, 0xFFFFFFFFu);
        arena.bonusCoins = ReadUint("Hearthstone.Arena.BonusCoins", d.bonusCoins, 0xFFFFFFFFu);
        std::string const weights = sConfig.GetStringDefault("Hearthstone.Arena.RarityWeights",
            Hs::FormatArenaRarityWeights(d.rarityWeights));
        if (!Hs::ParseArenaRarityWeights(weights, arena.rarityWeights))
            sLog.outError("Hearthstone: Hearthstone.Arena.RarityWeights must be four comma separated numbers "
                "(common,rare,epic,legendary), using %s", Hs::FormatArenaRarityWeights(d.rarityWeights).c_str());
        Hs::ArenaSettings const raw = arena;
        arena = Hs::SanitizeArenaSettings(arena);
        if (arena.entryCostCopper != raw.entryCostCopper || arena.entryCostCoins != raw.entryCostCoins ||
            arena.baseRewardPacks != raw.baseRewardPacks || arena.bonusCoins != raw.bonusCoins ||
            arena.rarityWeights != raw.rarityWeights)
            sLog.outError("Hearthstone: Hearthstone.Arena.* out of range: EntryCostCopper 1..%u, EntryCostCoins 1..%u, "
                "BaseRewardPacks 1..%u, BonusCoins 0..%u, RarityWeights 0..%u each (not all 0); values clamped",
                Hs::kMaxArenaEntryCopper, Hs::kMaxArenaEntryCoins, Hs::kMaxArenaBaseRewardPacks,
                Hs::kMaxArenaBonusCoins, Hs::kMaxArenaRarityWeight);
    }

    // The rounds of the start 0..kMaxLadderStartRounds, the threads 1..hardware threads.
    Hs::LadderSettings ladder;
    {
        Hs::LadderSettings const d;
        ladder.enable = sConfig.GetBoolDefault("Hearthstone.Ladder.Enable", d.enable);
        ladder.startRounds = ReadUint("Hearthstone.Ladder.StartRounds", d.startRounds, 0xFFFFFFFFu);
        if (ladder.startRounds > Hs::kMaxLadderStartRounds)
        {
            sLog.outError("Hearthstone: Hearthstone.Ladder.StartRounds must be 0..%u (%u); using %u",
                Hs::kMaxLadderStartRounds, ladder.startRounds, Hs::kMaxLadderStartRounds);
            ladder.startRounds = Hs::kMaxLadderStartRounds;
        }
        uint32_t const hardware = std::max<uint32_t>(1, std::thread::hardware_concurrency());
        ladder.chairGlowSpellId = ReadUint("Hearthstone.Ladder.ChairGlowSpellId", d.chairGlowSpellId, 0xFFFFFFFFu);
        ladder.threads = ReadUint("Hearthstone.Ladder.Threads", d.threads, 0xFFFFFFFFu);
        if (ladder.threads < 1 || ladder.threads > hardware)
        {
            uint32_t const clamped = std::min(std::max<uint32_t>(ladder.threads, 1), hardware);
            sLog.outError("Hearthstone: Hearthstone.Ladder.Threads must be 1..%u (%u); using %u", hardware,
                ladder.threads, clamped);
            ladder.threads = clamped;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_settings = s;
        m_packOdds = odds;
        m_deckMaxSaved = deckMaxSaved;
        m_match = match;
        m_economy = economy;
        m_arena = arena;
        m_ladder = ladder;
    }

    sLog.outString("Hearthstone: %s (reload=%u) debug=%u maxBytes=%u maxParts=%u timeoutMs=%u maxPending=%u rate=%u/s burst=%u matchAura=%u",
        s.enable ? "enabled" : "disabled", reload ? 1u : 0u, s.debug ? 1u : 0u, s.maxInboundBytes,
        s.maxInboundParts, s.reassemblyTimeoutMs, s.maxPendingRequests, s.ratePerSecond, s.rateBurst,
        s.matchAuraSpellId);
    sLog.outString("Hearthstone: pack weights common %u rare %u epic %u legendary %u, Legendary guarantee every %u packs%s",
        odds.weight[0], odds.weight[1], odds.weight[2], odds.weight[3], odds.legendaryPity,
        odds.legendaryPity ? "" : " (off)");
    sLog.outString("Hearthstone: at most %u saved decks per account", deckMaxSaved);
    sLog.outString("Hearthstone: economy: pack price %u coins, match coins loss %u win %u/%u/%u "
        "(normal/hard/boss), dust scrap %u/%u/%u/%u craft %u/%u/%u/%u (common/rare/epic/legendary)",
        economy.packPriceCoins, economy.rewards.loss, economy.rewards.winNormal,
        economy.rewards.winHard, economy.rewards.winBoss, economy.dust.scrap[0], economy.dust.scrap[1],
        economy.dust.scrap[2], economy.dust.scrap[3], economy.dust.craft[0], economy.dust.craft[1],
        economy.dust.craft[2], economy.dust.craft[3]);
    sLog.outString("Hearthstone: draft arena entry %u copper or %u coins, base reward %u packs, bonus %u coins, "
        "rarity weights %s", arena.entryCostCopper, arena.entryCostCoins,
        arena.baseRewardPacks, arena.bonusCoins, Hs::FormatArenaRarityWeights(arena.rarityWeights).c_str());
    sLog.outString("Hearthstone: NPC ladder %s, %u start round(s), %u thread(s)", ladder.enable ? "enabled" : "disabled",
        ladder.startRounds, ladder.threads);
    sLog.outString("Hearthstone: matches %s, AI action delay %u ms, %u config opponent creature(s) (database opponents are read at startup), match history %s",
        match.enable ? "enabled" : "DISABLED", match.aiDelayMs, static_cast<uint32_t>(match.opponents.size()),
        match.historyEnable ? "on" : "off");
}
