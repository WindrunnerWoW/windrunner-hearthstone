/*
 * Draft arena rules: which cards may be drafted, the three-card offers and the class offer,
 * the run-end prize table, the special-card sets, the daily period and the arena deck check. Also the
 * Hearthstone.Arena.* settings with their bounds and the terms a run snapshots at entry.
 *
 * PURE C++ (standard library, the collection and deck rules and the match engine's CardDb, no core headers) so it
 * can be unit-tested headlessly. The run state machine lives in HsArenaService.*.
 *
 * Randomness is injected as a PackRng (uniform integer in [0, bound)), so a seeded generator reproduces a draft
 * exactly; MakeSeededRng builds one.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_RULES_H
#define MOD_HEARTHSTONE_HS_ARENA_RULES_H

#include "../collection/HsOwnership.h"
#include "../collection/HsPacks.h"
#include "../decks/HsDecks.h"
#include "../match/HsMatch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Hs
{
    // Picks of a draft (= cards of the deck), cards of one offer, classes of the class offer, wins that end a run.
    constexpr uint32_t kArenaDraftPicks = kDeckSize;
    constexpr std::size_t kArenaOfferSize = 3;
    constexpr std::size_t kArenaClassOfferSize = 3;
    constexpr uint32_t kArenaMaxWins = 10;

    // How a run was entered. Free is the run of the arena introduction quest and the GM start: it pays nothing,
    // never uses the daily slot of the paid entry, and still pays the normal prize table. Practice is every run after
    // the day's paid entry: it costs nothing, never uses the daily slot, and pays NO prize at all (PrizeFor).
    enum class ArenaPay { Free, Gold, Coins, Practice };
    char const* ArenaPayCode(ArenaPay pay); // "free", "gold", "coins", "practice"
    bool ParseArenaPay(std::string const& code, ArenaPay& out);
    // Only a paid entry reserves the account's daily slot (one paid entry per account and trader period).
    constexpr bool ArenaPayUsesDailySlot(ArenaPay pay) { return pay == ArenaPay::Gold || pay == ArenaPay::Coins; }

    // Hearthstone.Arena.* (conf/mod-hearthstone.conf.dist). Reload affects new runs only: a run keeps ArenaTerms.
    struct ArenaSettings
    {
        uint32_t entryCostCopper = 150000;  // Hearthstone.Arena.EntryCostCopper (15 gold)
        uint32_t entryCostCoins = 30;       // Hearthstone.Arena.EntryCostCoins
        uint32_t baseRewardPacks = 3;       // Hearthstone.Arena.BaseRewardPacks
        uint32_t bonusCoins = 10;           // Hearthstone.Arena.BonusCoins (paid at six or more wins)
        // Hearthstone.Arena.RarityWeights: Common (Basic counts as Common), Rare, Epic, Legendary.
        std::array<uint32_t, kPackRarityCount> rarityWeights = { { 716, 228, 44, 11 } };
    };
    constexpr uint32_t kMaxArenaEntryCopper = 100000000; // 10000 gold
    constexpr uint32_t kMaxArenaEntryCoins = 1000;
    // The ten-win extra pack must still fit one pack grant (kMaxPacksPerOp).
    constexpr uint32_t kMaxArenaBaseRewardPacks = kMaxPacksPerOp - 1;
    constexpr uint32_t kMaxArenaBonusCoins = 1000;
    constexpr uint32_t kMaxArenaRarityWeight = 1000000;
    // Clamps every value to its bounds (entry costs and the base reward to at least 1, the weights to
    // 0..kMaxArenaRarityWeight; all weights 0: the defaults).
    ArenaSettings SanitizeArenaSettings(ArenaSettings settings);
    // "716,228,44,11": exactly four unsigned decimals separated by commas (spaces allowed around them). False
    // (out untouched) for anything else.
    bool ParseArenaRarityWeights(std::string const& text, std::array<uint32_t, kPackRarityCount>& out);
    std::string FormatArenaRarityWeights(std::array<uint32_t, kPackRarityCount> const& weights);

    // What a run snapshots at entry (hearthstone_arena_run.terms and the version columns).
    struct ArenaTerms
    {
        ArenaPay pay = ArenaPay::Free;
        uint32_t payAmount = 0; // copper for Gold, Hearthstone Coin items for Coins, 0 for Free
        uint32_t basePacks = 3;
        uint32_t bonusCoins = 10;
        std::array<uint32_t, kPackRarityCount> rarityWeights = { { 716, 228, 44, 11 } };
        uint32_t rulesVersion = 0;  // Match::kRulesVersion at entry
        std::string datasetHash;    // content hash of the card dataset at entry
    };
    // The terms of a new run from the current (sanitized) settings.
    ArenaTerms MakeArenaTerms(ArenaSettings const& settings, ArenaPay pay, std::string const& datasetHash);

    // ---- prizes ----------------------------------------------------------

    // The run-end payout. Packs go to the account's pack stock, coins to the bags of the character that ended the
    // run. specialChoice: the player chooses one missing special-card set (never together with the extra pack).
    struct ArenaPrize
    {
        uint32_t packs = 0;
        uint32_t coins = 0;
        bool specialChoice = false;
        bool operator==(ArenaPrize const& o) const
        {
            return packs == o.packs && coins == o.coins && specialChoice == o.specialChoice;
        }
        bool operator!=(ArenaPrize const& o) const { return !(*this == o); }
    };
    // A practice run pays nothing, whatever it wins. Confirmed table otherwise: 0 wins nothing; 1..2 one pack; 3..5 the base packs; 6..9 the base packs and the bonus coins;
    // 10 the same plus a special-card choice, or one more pack once specialSetsComplete. Wins above 10 count as 10.
    ArenaPrize PrizeFor(uint32_t wins, ArenaTerms const& terms, bool specialSetsComplete);

    // The four reward-only cards a ten-win run can award, with the copies of a full playable set.
    struct ArenaSpecialCard
    {
        char const* id;
        uint32_t copies;
    };
    // EX1_062 x1, NEW1_016 x2, EX1_112 x1, PRO_001 x1.
    std::vector<ArenaSpecialCard> const& ArenaSpecialCards();
    // The special card with this exact id, or nullptr.
    ArenaSpecialCard const* FindArenaSpecialCard(std::string const& id);
    // Copies of a full set the account still lacks (counting all owned copies, protected or not).
    uint32_t ArenaSpecialMissingCopies(ArenaSpecialCard const& card, Collection const& collection);
    struct ArenaSpecialMissing
    {
        std::string id;
        uint32_t copies = 0; // > 0
    };
    // The sets that are not complete yet, in the order of ArenaSpecialCards().
    std::vector<ArenaSpecialMissing> ArenaMissingSpecials(Collection const& collection);
    bool ArenaSpecialSetsComplete(Collection const& collection);

    // ---- daily period ----------------------------------------------------

    // The UTC boundary of the trader's swap time (Hs::TraderPeriod); the key of the one paid entry per day.
    uint64_t ArenaPeriod(uint64_t now, uint32_t swapSeconds);

    // ---- eligibility and offers --------------------------------------------

    // The quest (Naxxramas) cards a run may draft: the run's snapshot of the account's owned quest cards at entry
    // (ArenaOwnedQuestCards), sorted. nullptr or an empty set: no quest card is allowed (the default).
    using ArenaQuestSet = std::set<std::string>;

    // A card the arena may offer, whatever the class: collectible, a rarity, fully supported by the engine, and
    // either acquisition standard or a quest card in questOwned. Excludes tokens, unreleased, the four reward-only
    // cards, and quest cards the account did not own at entry.
    bool ArenaCardOk(std::string const& id, CardRule const& rule, Match::CardDb const& db,
                     ArenaQuestSet const* questOwned = nullptr);
    // ArenaCardOk and the card is of deckClass or NEUTRAL.
    bool ArenaEligible(std::string const& id, CardRule const& rule, Match::CardDb const& db,
                       std::string const& deckClass, ArenaQuestSet const* questOwned = nullptr);
    // The quest cards the account owns (count >= 1) at a run's entry, sorted by id. Only acquisition quest cards;
    // ArenaCardOk still decides whether one is offered.
    std::vector<std::string> ArenaOwnedQuestCards(CardCatalog const& catalog, Collection const& collection);

    // The eligible cards of one class by rarity bucket (FREE counts as Common), sorted by id.
    class ArenaPool
    {
    public:
        static ArenaPool FromCatalog(CardCatalog const& catalog, Match::CardDb const& db, std::string const& deckClass,
                                     ArenaQuestSet const* questOwned = nullptr);
        std::vector<std::string> const& Cards(PackRarity r) const { return m_cards[static_cast<std::size_t>(r)]; }
        std::size_t Size() const;

    private:
        std::array<std::vector<std::string>, kPackRarityCount> m_cards;
    };

    // Seeded deterministic generator (std::mt19937_64, bound by modulo): the same seed gives the same sequence.
    PackRng MakeSeededRng(uint64_t seed);

    // One offer of three distinct ids. Every slot rolls a rarity by weights, then picks uniformly among the cards of
    // that rarity not yet in the offer; when there is none the next lower rarity is used, and only if every lower
    // one is empty too the next higher one. All weights 0 roll like the defaults. False (out untouched) when the
    // pool holds fewer than three cards.
    bool ArenaOffer(ArenaPool const& pool, std::array<uint32_t, kPackRarityCount> const& weights, PackRng const& rng,
                    std::vector<std::string>& out);
    // Three distinct classes of the nine deck classes, in the order drawn.
    std::vector<std::string> ArenaClassOffer(PackRng const& rng);

    // ---- arena deck ----------------------------------------------------------

    // Why an arena deck is refused. Class: not one of the nine; Size: not exactly 30 cards; Card: unknown or not
    // arena-eligible (see ArenaCardOk); CardClass: neither the deck class nor NEUTRAL. The first broken rule in
    // that order wins.
    enum class ArenaDeckWhy { None, Class, Size, Card, CardClass };
    char const* ArenaDeckWhyCode(ArenaDeckWhy why); // "class", "size", "card", "cardclass" ("" for None)
    char const* ArenaDeckWhyText(ArenaDeckWhy why);
    // One entry per copy. There is deliberately no copy cap (arena rule), unlike ValidateDeckCards.
    ArenaDeckWhy ValidateArenaDeck(CardCatalog const& catalog, Match::CardDb const& db, std::string const& deckClass,
                                   std::vector<std::string> const& cards, ArenaQuestSet const* questOwned = nullptr);
}

#endif
