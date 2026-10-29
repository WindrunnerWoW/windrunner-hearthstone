/*
 * Battlegrounds shop: the recruit phase of all players of a lobby. Gold, tavern tier and upgrade
 * cost, Bob's Tavern offers, hand and board, Magnetic, triples, Discover, and the dispatch of every shop hook of the
 * effect registry.
 *
 * PURE C++ (standard library, HsBgTypes.h, HsBgEffects.h, HsBgPool.h). Shop implements ShopApi, so an effect handler
 * works on the very objects the actions change.
 *
 * NUMBERS. Gold: 3 on turn 1, +1 per turn, 10 at most at the START of a turn (the gold is set, not added: nothing is
 * carried over); effects and selling may take it above 10 inside the turn. Buy 3, sell +1, refresh 1, freeze free.
 * Tavern upgrade to tier 2..6 costs 5 / 7 / 8 / 9 / 11; each turn after the first the cost of the NEXT upgrade drops by 1
 * (to 0); an upgrade resets the cost of the next tier to its full price. Offers by tavern tier 1..6: 3 / 4 / 4 / 5 / 5 / 6.
 * Hand 10, board 7.
 *
 * ACTIONS AND THE HOOKS THEY FIRE (a hook fires on board minions left to right, by uid snapshot, so a handler may
 * summon or sell; a minion that left the board meanwhile is skipped):
 *   StartTurn  gold set, offers renewed (Frozen offers stay and thaw, the rest is replaced up to the tier's size),
 *              then onStartOfTurn.
 *   EndTurn    onEndOfTurn. Refused while a Discover is open.
 *   Buy        offer -> hand, onBuy (self = the bought minion, in the hand), then triples.
 *   Sell       onSell (self = the sold minion, still in place), then it leaves, +1 gold, its pool copies return.
 *   Play       hand -> board slot; battlecry (self, target); onFriendlyPlayed and onFriendlySummonedShop on the OTHER
 *              board minions (other = the played minion: playing counts as summoning); golden: a Triple Reward Discover
 *              from min(tavern tier + 1, 6) opens. Magnetic with a Mech target: the minion merges, no battlecry, no
 *              summon hook, onFriendlyPlayed fires with other = the Mech.
 *   Move       reorders the board.
 *   Refresh    onRefresh on the board after the new offers are drawn (manual refreshes only).
 *   Upgrade    onTavernUpgrade on the board.
 *   Choose     picks a Discover option.
 * Effect calls (ShopApi) that summon fire onFriendlySummonedShop. Hooks nest at most kMaxHookDepth deep (an endless
 * chain is cut). RecomputeAuras runs after every change of a board; triples are checked after every change.
 *
 * POOL. Offers, Discover options and bought minions are real pool copies (Minion::poolCopies); the copies of unsold
 * offers return to the pool when the tavern is renewed. CheckConservation proves pool + held == initial for all cards.
 * Every player draws from the pool ids in PlayerShop::pools ({0} in the single-pool game, one pool per faction otherwise; see HsBgPool.h).
 */

#ifndef MOD_HEARTHSTONE_HS_BG_SHOP_H
#define MOD_HEARTHSTONE_HS_BG_SHOP_H

#include "HsBgEffects.h"
#include "HsBgHeroes.h"
#include "HsBgPool.h"
#include "HsBgQuests.h"
#include "HsBgRewards.h"
#include "HsBgTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        constexpr int kStartGold = 3;
        constexpr int kMaxStartGold = 10;
        constexpr int kBuyCost = 3;
        constexpr int kSellGold = 1;
        constexpr int kRefreshCost = 1;
        constexpr int kStartHealth = 40;
        constexpr int kDiscoverOptions = 3;
        constexpr int kTripleSize = 3;
        constexpr int kMaxHookDepth = 24;

        // Minions the tavern shows at a tavern tier (3 / 4 / 4 / 5 / 5 / 6); 0 outside 1..6.
        int OffersForTier(int tier);
        // Full price of the upgrade that LEADS TO `toTier` (5 / 7 / 8 / 9 / 11 for 2..6); 0 otherwise.
        int UpgradeBaseCost(int toTier);
        // Gold at the start of a turn (turn 1 = 3, capped at 10).
        int GoldForTurn(int turn);

        // The answer to an action: Ok, or the reason it was refused (nothing changed).
        enum class ActionResult : uint8_t
        {
            Ok,
            BadPlayer,        // no such player
            Eliminated,       // the player is out of the game
            GameOver,         // the game has ended
            NotShopPhase,     // the player has ended the turn / it is not the recruit phase
            DiscoverPending,  // a Discover is open: only Choose is allowed
            NoDiscover,       // Choose without an open Discover
            BadIndex,         // offer / hand / board index out of range
            BadTarget,        // the battlecry or Magnetic target is not valid
            NotEnoughGold,
            HandFull,
            BoardFull,
            MaxTier,          // the tavern is already tier 6
            NotAlly,          // the send target is not a living ally of the same faction (or is the player itself)
            SendUsed,         // the send of this shop turn is already used
            NoPower,          // the hero has no active power / it is a passive
            PowerUsed,        // the power is once per turn or once per game and already used
            NotPicking,       // the hero-pick phase is not open (or this player already picked)
        };
        char const* ResultName(ActionResult r);

        enum class Zone : uint8_t { Board, Hand };

        // What a power choice does with the chosen power.
        //   Swap       Finley's start-of-game power, Nguyen's per-turn choice: it becomes the current power
        //   Flightpath Galewing: it becomes the current passive and completes after its countdown
        //   Invocation Bru'kan: it stays as the combat power until changed
        //   Aim        Tavish: it is the combat power of the next combat only
        //   Stance     Cariel Roame: an improvement of Conviction after a Tavern upgrade (kStance* ids, no power def)
        enum class PowerChoiceKind : uint8_t { None, Swap, Flightpath, Invocation, Aim, Stance };

        // A Discover waiting to be shown, or the Triple Reward of a played golden minion.
        struct DiscoverRequest
        {
            int minTier = 1;
            int maxTier = 6;
            uint32_t tribeAny = 0;
            bool spell = false;          // three Spellcraft spells, not pool minions (Lady Vashj)
            bool permanentSpell = false; // the chosen spell's effect is permanent
            bool boardCopy = false;      // plain copies from the last opponent's warband (Scabbs)
            // A choice of hero powers, not minions (powerOptions holds the candidate ids).
            PowerChoiceKind powerKind = PowerChoiceKind::None;
            std::vector<std::string> powerOptions;
            int powerCount = 3;          // how many of them to offer
            int optionCount = 0;         // how many minions to offer; 0 = kDiscoverOptions (Hooktusk offers 2)
            bool hold = false;           // Ambassador Faelin: the chosen minion waits for its Tier (PowerState::expedition)
            std::vector<Minion> warband; // Mr. Bigglesworth: the dead warband, its minions keep stats and enchantments
            bool coinIfEmpty = false;    // ... a Consolation Coin when the dead warband is empty
            bool secret = false;         // the options are Secrets; the chosen one goes into play
            int holmesCard = -1;         // Murloc Holmes: the opponent's minion (card index) among two options; -1 = not Holmes
            // The options are the Darkmoon Prizes of this Prize turn (1-4); -1 = any of them (Hand of
            // Fate). The chosen one goes to the hand as a spell.
            int prizeTurn = 0;
        };

        // What the player did over the game, for the Battlegrounds weeklies (see Session::Record).
        constexpr int kWeeklyTribes = 9;   // Beast .. Quilboar, the tribe bits 0-8
        struct GameStats
        {
            int bought = 0;                    // minions bought from the tavern
            int sold = 0;                      // minions sold
            int spells = 0;                    // spells cast
            int triples = 0;                   // golden minions made by a triple
            int buffs = 0;                     // buff instances on a friendly board / hand minion (Shop::Buff)
            int battlecries = 0;               // minions with a Battlecry played from the hand
            int refreshes = 0;                 // manual or free Refreshes of the tavern
            int deathrattles = 0;              // friendly Deathrattle triggers in combat
            int kills = 0;                     // enemy minions the player's side destroyed in combat
            int combatWins = 0;                // combats won
            int damageDealt = 0;               // damage dealt to enemy heroes by winning combats
            int played[kWeeklyTribes] = {};    // minions played from the hand, by tribe bit (an All minion counts for each)
        };

        // Everything one player owns during the recruit phase.
        struct PlayerShop
        {
            GameStats stats;
            Board board;
            Hand hand;
            int gold = 0;
            int tier = 1;
            int upgradeCost = 5;                // price of the next upgrade now; 0 and unused at tier 6
            int health = kStartHealth;          // owned here so effects can hurt the hero (DamageHero)
            int maxHealth = kStartHealth;       // the hero's starting health (Patchwerk 60, others 40)
            int armor = 0;                      // absorbs damage before health; rolled at the hero pick
            int hero = -1;                      // index into the hero table, -1 until the hero is picked
            PowerState power;                   // the hero power state (uses, counters)
            QuestState quest;                   // the 24.2 quest, its progress and its reward
            RewardState rewardState;            // the paired reward and its counters
            // Sire Denathrius holds a second quest (the start-of-game one in `quest`, the turn-4 one
            // here) with its own reward.
            QuestState quest2;
            RewardState rewardState2;
            // Elementals: "your next Refresh costs (X) less" (Recycling Wraith, Refreshing Anomaly),
            // the permanent Elemental tavern bonus (Nomi, Dazzling Lightspawn), Tortollan Blue Shell, and the
            // player's own faction tribes for The Friends Along the Way.
            std::vector<int> refreshDiscounts;   // gold off each of the next refreshes, the next one first
            int elementalTavernAttack = 0;
            int elementalTavernHealth = 0;
            bool lostLastCombat = false;
            std::vector<uint32_t> lobbyTribes;
            // Pirates: Pirates bought this turn (Southsea Strongarm) and summoned this game (Salty
            // Looter), both per player and reset with the game, the first also at StartTurn.
            int piratesBoughtThisTurn = 0;
            int piratesSummonedThisGame = 0;
            std::vector<Minion> offers;         // Bob's Tavern, left to right
            std::vector<int> pools = { 0 };     // pool ids the tavern and Discover draw from
            std::vector<Minion> options;        // the open Discover options (empty = none open)
            bool optionsHold = false;           // the open options become held (Faelin), not hand cards
            std::vector<std::string> powerChoices; // the open hero-power choice, instead of options
            PowerChoiceKind choiceKind = PowerChoiceKind::None;
            std::vector<DiscoverRequest> queue; // Discovers waiting behind the open one
            int spent = 0;                      // gold spent on actions this game (statistics)
            bool sentThisTurn = false;          // one send to an ally per shop turn (reset at StartTurn)
            std::vector<int> gifts;             // cards allies sent this player, oldest first (the session drains it)
            std::vector<int> wheelSpins;        // outcomes (0-5) of the Wheel of Yogg-Saron spun since the last drain (the session drains it)
            Board lastOpponent;                 // the board of the last combat opponent (ghost included), see SetLastOpponent
            bool hasLastOpponent = false;
            bool skipTurn = false;              // this shop turn ends at once (A. F. Kay, Faelin)
            // The Secrets of The Great Akazamzarak, in the order they were put into play (the trigger
            // order). A secret stays until it triggers; the cap is 5.
            std::vector<int> secrets;
            bool optionsSecret = false;         // the open Discover options are Secrets, not hand cards
            int optionsHolmes = -1;             // Murloc Holmes: the right answer of the open options (card index), -1 = none
            // Tickatus and the Darkmoon Prizes.
            int extraOffers = 0;                // New Recruit: extra tavern slots for the rest of the game
            int tavernAttackAll = 0;            // The Good Stuff: +Attack on every tavern offer for the rest of the game
            int tavernCostDiscount = 0;         // Friends and Family Discount: every tavern minion costs this much less
            bool openBar = false;               // Open Bar: the first freeRefreshes refreshes of a turn are free
            int freeRefreshes = 0;
            bool battlecriesTwice = false;      // Brann's Blessing: Battlecries trigger twice this turn
            int unlimitedCoinsOut = 0;          // The Unlimited Coin: copies that come back at end of turn
        };

        class Shop : public ShopApi
        {
        public:
            // `effects` and `rng` must outlive the Shop. `heroPowers` defaults to the global registry. All players
            // start at tier 1 with no gold; call StartTurn.
            Shop(PoolSet& pool, EffectRegistry const& effects, Rng& rng, int players,
                 HeroPowerRegistry const* heroPowers = nullptr);

            int Players() const { return static_cast<int>(m_players.size()); }
            PlayerShop& State(int player) { return m_players[static_cast<std::size_t>(player)]; }
            PlayerShop const& State(int player) const { return m_players[static_cast<std::size_t>(player)]; }
            PoolSet& Pool() { return m_pool; }
            PoolSet const& Pool() const { return m_pool; }
            bool ValidPlayer(int player) const { return player >= 0 && player < Players(); }
            bool DiscoverOpen(int player) const
            {
                return ValidPlayer(player) && (!State(player).options.empty() || !State(player).powerChoices.empty());
            }
            // The open hero-power choice of a player (empty = none).
            std::vector<std::string> const& PowerChoices(int player) const;
            // The two quest slots (0 = the standard quest, 1 = Denathrius's second one).
            QuestState const& QuestSlot(int player, int slot) const;
            RewardState const& RewardSlot(int player, int slot) const;
            int FreeQuestSlot(int player) const;      // the first slot without a taken quest, -1 when both are taken
            int OfferQuestSlot(int player) const;     // the slot with a pending offer, -1 when none
            // Puts the chosen minions of a held Discover (Faelin) in the hand once the player reaches their Tier.
            void ReleaseExpedition(int player);
            // The powers a swap may pick from (the plan's rule: every curated power but three, lobby tribes aware).
            std::vector<std::string> SwapPool(int player) const;
            // Queues a choice of `count` powers drawn from `pool` (shuffled when it opens).
            bool OfferPowerChoice(int player, PowerChoiceKind kind, std::vector<std::string> const& pool, int count);
            // The power that acts in combat: the chosen aim / invocation, else the current power.
            std::string CombatPowerId(int player) const;
            // Ragnaros: enemy minions his side killed; at 25 the power becomes Sulfuras.
            void CountEnemyKills(int player, int kills);
            // A power definition of the shop's registry (the AI's power picks use it).
            HeroPowerDef const* FindPower(std::string const& powerId) const
            {
                return m_heroPowers.Find(powerId);
            }
            // The price of the next upgrade; -1 at tier 6.
            int UpgradeCost(int player) const;
            uint32_t NewUid() { return m_nextUid++; }

            // ------------------------------------------------------------------------ turn flow (called by the game)
            void StartTurn(int player, int turn);
            ActionResult EndTurn(int player);

            // ------------------------------------------------------------------------ actions
            ActionResult Buy(int player, int offerIndex);
            int BuyCost(int player) const;                  // the next buy's price (Friends and Family Discount)
            ActionResult Sell(int player, Zone zone, int index);
            // targetUid: a battlecry target on the player's board, or the Mech a Magnetic minion merges into; 0 = none.
            ActionResult Play(int player, int handIndex, int slot, uint32_t targetUid = 0);
            // Cast a spell from the hand, on a friendly board minion when the spell needs one.
            ActionResult Cast(int player, int handIndex, uint32_t targetUid = 0);
            ActionResult Move(int player, int fromSlot, int toSlot);
            ActionResult Refresh(int player);
            int RefreshCost(int player) const;   // what the next manual Refresh costs (hero power and discounts)
            // The Freeze button: freezes every offer of the tavern, or unfreezes them all.
            ActionResult Freeze(int player, bool frozen);
            // Frozen is per offer. Frozen offers stay through the next turn start (Sindragosa buffs them).
            bool OffersFrozen(int player) const;      // the tavern has offers and every one is Frozen (the button shows Unfreeze)
            bool AnyOfferFrozen(int player) const;
            ActionResult Upgrade(int player);
            ActionResult Choose(int player, int optionIndex);
            // Move a hand minion to a living ally of the same faction. The caller (Game) checks the faction and
            // the ally's life; Shop checks the turn rule and the ally's hand room and resolves a triple on the receiver.
            ActionResult Send(int player, int handIndex, int ally);
            // Use the player's active hero power. `targetUid` is the friendly board minion for a FriendlyMinion
            // power; `offerIndex` is unused for the first heroes. Armor/DamageHero take armor first.
            ActionResult HeroPower(int player, uint32_t targetUid = 0, int offerIndex = -1, uint32_t secondUid = 0);
            // Set the player's hero and its health / max health, then roll the armor of its tier.
            void SetHero(int player, int heroIndex);
            // A free tavern refresh (no gold) that still fires onRefresh: Infinite Toki calls it.
            void RefreshOffers(int player);
            // Draws one offer from a tier above the player's (Infinite Toki); a no-op at tier 6 / when the pool is dry.
            void DrawHigherTierOffer(int player);
            // Turns a friendly board minion into its golden version in place, keeping its buffs and pool copies, and
            // without a Triple Reward (Reno Jackson). False when it is already golden / has no golden version.
            bool MakeGolden(int player, uint32_t uid);
            // Combat damage to a hero: armor absorbs it first. No immunity check (see DamageHero).
            void TakeDamage(int player, int amount);

            // Triples, auras and tavern bonuses after a change made outside the actions (tests editing boards directly).
            void Settle(int player);

            // Activates the paired reward when the quest is done (called after a quest event). Safe to call often.
            void CheckReward(int player);
            // The lobby's tribes, for The Friends Along the Way (the game supplies them after choosing the tribes).
            void SetLobbyTribes(std::vector<uint32_t> const& tribes) { m_lobbyTribes = tribes; }
            // Each player's own faction set (the two may differ). Overrides the lobby-wide list above.
            void SetLobbyTribes(int player, std::vector<uint32_t> const& tribes);
            std::vector<uint32_t> const& PlayerLobbyTribes(int player) const;
            void SetLastCombatWon(int player, bool won);

            // One Wheel of Yogg-Saron outcome (0-5, WheelOutcomeName order). Yogg-tastic Tasties rolls it; tests
            // call it directly.
            void WheelOutcome(int player, int outcome);

            // Game: remembers the board the player's last opponent brought to the fight (Murozond).
            void SetLastOpponent(int player, Board const& opponent);

            // The player is out: every copy of the board, hand, tavern and open Discover goes back to the pool.
            void ReleasePlayer(int player);

            // Room for a minion of this card in the hand (a full hand still takes a triple completer): a need question
            // the AI and the wire ask without changing anything.
            bool CanReceive(int player, int card, bool golden) const;
            // What selling this card pays now (Freedealing Gambler and its golden twin pay more; a spell pays 0):
            // the UI's sell price, without changing anything.
            int SellGold(Minion const& m) const;

            // ------------------------------------------------------------------------ audit
            // Pool copies held by players per card index (boards, hands, offers, Discover options).
            std::vector<int> HeldCopies() const;
            // For every card of the lobby: pool.Left + held == pool.Initial. `why` (optional) names the first failure.
            bool CheckConservation(std::string* why = nullptr) const;

            // ------------------------------------------------------------------------ ShopApi
            Board& PlayerBoard(int player) override { return State(player).board; }
            Hand& PlayerHand(int player) override { return State(player).hand; }
            int Gold(int player) const override { return State(player).gold; }
            void AddGold(int player, int amount) override;
            int TavernTier(int player) const override { return State(player).tier; }
            Rng& Random() override { return m_rng; }
            std::vector<Minion>& Offers(int player) override { return State(player).offers; }
            Minion* FindMinion(int player, uint32_t uid) override;
            EffectRegistry const& Effects() const override { return m_effects; }
            int Turn() const override { return m_turn; }
            void Buff(int player, Minion& m, int attack, int health) override;
            void GiveKeywords(int player, Minion& m, uint32_t keywords) override;
            uint32_t SummonToBoard(int player, int card, bool golden, int position) override;
            uint32_t AddToHand(int player, int card, bool golden) override;
            uint32_t AddRandomToHand(int player, int minTier, int maxTier, uint32_t tribeAny) override;
            bool Discover(int player, int minTier, int maxTier, uint32_t tribeAny) override;
            bool TakeOffer(int player, int index) override;
            bool ConsumeOffer(int player, int index, Minion& out) override;
            void DamageHero(int player, int amount) override;
            void TriggerBattlecry(int player, uint32_t uid, uint32_t targetUid) override;
            int HeroHealth(int player) const override { return State(player).health; }
            int HeroMaxHealth(int) const override { return kStartHealth; }
            int BattlecryTimes(int player) const override;
            int SummonTimes(int player) const override;
            Board const* LastOpponentBoard(int player) const override;
            uint32_t AddRandomToHandExcept(int player, int minTier, int maxTier, uint32_t tribeAny, int excludeCard) override;
            void AddRefreshDiscount(int player, int perRefresh, int charges) override;
            void AddElementalTavernBonus(int player, int attack, int health) override;
            bool LostLastCombat(int player) const override { return ValidPlayer(player) && State(player).lostLastCombat; }
            bool AddRandomOffer(int player, int minTier, int maxTier, uint32_t tribeAny, int excludeCard = -1) override;
            void FreezeOffer(int player, int index) override;
            int ElementalsPlayedThisTurn(int player) const override
            {
                return ValidPlayer(player) ? State(player).power.elementalPlaysThisTurn : 0;
            }
            // Pirates
            void ReduceUpgradeCost(int player, int amount) override;
            uint32_t AddRandomGoldenToHand(int player, int minTier, int maxTier, uint32_t tribeAny) override;
            int PiratesBoughtThisTurn(int player) const override
            {
                return ValidPlayer(player) ? State(player).piratesBoughtThisTurn : 0;
            }
            // Naga and spells
            void SpellBuff(int player, Minion& m, int attack, int health, bool permanent) override;
            void SpellKeywords(int player, Minion& m, uint32_t keywords, bool permanent) override;
            uint32_t AddSpellToHand(int player, int card, bool golden, bool temporary, bool permanentEffect) override;
            bool DiscoverSpell(int player, int maxTier, bool permanentEffect) override;
            // The Great Akazamzarak. A Discover of up to three Secrets the player does not have yet;
            // the chosen one goes into play, not the hand. False when no Secret is left.
            bool DiscoverSecret(int player);
            // The Ice Block secret: true when the damage would kill the hero, in which case it is prevented and the
            // secret leaves play (the hero is Immune for the rest of the round).
            bool PreventFatalDamage(int player, int amount);
            // Quilboar: play a Blood Gem on a friendly minion (its own effect and the on-target triggers).
            void PlayBloodGem(int player, Minion& target) override;
            // Swap a friendly non-golden board minion with a random tavern offer (Jandice Barov); false when it cannot.
            bool SwapWithRandomOffer(int player, uint32_t uid);
            // Swap the stats of a friendly board minion and a random tavern offer (Vol'jin), without moving them.
            bool SwapStatsOf(int player, uint32_t uidA, uint32_t uidB);
            // Replace a tavern offer with a random pool minion of the tier range (Malygos, Galakrond).
            bool ReplaceOffer(int player, int offerIndex, int minTier, int maxTier);
            // Take a tavern offer to the hand and buff it (+a/+h), or set its stats to 2/2 (Xyrella, Yogg).
            bool TakeOfferAndBuff(int player, int offerIndex, int attack, int health, bool setToStats);
            // A custom Discover (Hooktusk's two options, Faelin's held tiers, Bigglesworth's warband).
            bool QueueCustomDiscover(int player, DiscoverRequest const& request);
            // Bigglesworth's Discover from a dead warband (or its Consolation Coin).
            void OfferWarbandDiscover(int player, Board const& warband);
            // One random offer of Silas's tavern gets a ticket (after a Refresh / the StartTurn restock).
            void MarkTicketOffer(int player);
            // Maiev Shadowsong takes a tavern offer into the power's held slot (pool copy kept).
            bool HoldOffer(int player, int offerIndex);
            // A plain pool copy of a specific card to the hand (Y'Shaarj, Rafaam). False when the pool has
            // no copy left or the hand is full.
            bool AddPoolCardToHand(int player, int card, bool golden);
            // Mutanus removes a board minion and returns its pool copies; Tess refills the tavern with
            // the last opponent's warband; Scabbs discovers a copy from it.
            bool RemoveFromBoard(int player, uint32_t uid);
            bool RefreshWithLastOpponent(int player);
            bool DiscoverLastOpponent(int player);
            // Ini Stormcoil counts friendly minions that died in combat (every 10 gives a random Mech).
            void CountFriendlyDeaths(int player, int deaths);
            // Salty Looter counts the Pirates summoned on the player's side during combat (summoned and reborn).
            void CountCombatPirateSummons(int player, int count);
            // Murloc Holmes: two options, the card is the right one (a Coin when chosen).
            void DiscoverHolmes(int player, int card);
            // Tickatus's prize Discover (prizeTurn 1-4; -1 = any prize, Hand of Fate).
            bool PrizeDiscover(int player, int prizeTurn);
            // The tavern pieces the prizes use.
            void AddTavernAttack(int player, int amount);
            void AddTavernCostDiscount(int player, int amount);
            void AddExtraTavernOffer(int player);
            void EnableOpenBar(int player);
            void MarkUnlimitedCoin(int player);
            void EnableBrannsBlessing(int player);
            bool MakeOfferGolden(int player, int offerIndex);
            bool TakeFriendlyToHand(int player, uint32_t uid, bool makeGolden, int attack, int health, bool nonGoldenOnly);
            void EvolveTavern(int player);
            void StealTavern(int player);
            bool DiscoverPower(int player, int count);
            // The card of the pair Queen Azshara turns into a triple (Three Wishes): a normal minion with exactly two
            // copies on the board or in the hand, or -1 when there is none.
            int PairForWish(int player) const;
            // Lord Barov: a guess for a player that rested (no fight) simply fails; the game clears the round's guesses.
            void ClearBarovGuesses();
            // Salty Looter: the stats from Pirates summoned this game live in counters[1] of every owned copy.
            void ApplyPirateSummonBonuses(int player);
            void CountPirateSummoned(int player, int card);

            HeroPowerDef const* Power(int player) const;     // the player's hero power, nullptr before the pick / passive-free

        private:
            // Marks a public action; when the outermost one ends, a reward whose quest it finished turns on.
            class ActionScope
            {
            public:
                explicit ActionScope(Shop& shop);
                ~ActionScope();
                ActionScope(ActionScope const&) = delete;
                ActionScope& operator=(ActionScope const&) = delete;
            private:
                Shop& m_shop;
            };

            using HookMember = ShopHook EffectDef::*;
            void FireOn(int player, HookMember hook, uint32_t uid, uint32_t otherUid = 0, uint32_t targetUid = 0);
            void RollArmor(int player);                      // roll the armor of the picked hero's tier
            void AfterPlay(int player, uint32_t uid, int card);        // Kael'thas: count the play, buff every third minion
            void RecomputeOfferAuras(int player);            // Legion Overseer style bonuses on the offers
            static void StripAura(Minion& m);                // strips an aura bonus off a minion
            void LeaveTavern(int player, Minion& m);         // a bought / taken offer keeps its tavern bonus as its own stats
            void FireBoard(int player, HookMember hook, uint32_t exceptUid = 0, uint32_t otherUid = 0);
            // Fires onSpellCast for every board minion after a spell was cast.
            void FireSpellCast(int player, int spellCard, bool spellGolden, bool spellcraft, uint32_t targetUid);
            // Fires onSpendGold for every board minion after the player spends Gold.
            void FireGoldSpent(int player, int amount);
            void ClearTemporary(int player);       // removes "until next turn" stats / keywords and temporary spells
            void AddSpellcraftSpells(int player);  // StartTurn: every board Spellcraft minion puts its spell in the hand
            void RunPowerCountdown(int player);    // Galewing's flightpath at StartTurn
            void CheckNagaConquest(int player);    // Azshara: 25 total Attack unlocks Naga Conquest
            // One run of the Battlecry of `uid`, then onBattlecryTriggered of the board (Kalecgos) when it had one.
            void RunBattlecry(int player, uint32_t uid, uint32_t targetUid);
            // The hero power's reaction to a minion the player summoned (Millificent: a Mech gets +2 Attack).
            void HeroSummoned(int player, uint32_t uid);
            void FireQuest(int player, QuestEventInfo const& ev);   // one quest event for one player
            // Reward behaviours (HsBgRewards.cpp), dispatched by RewardKind. A reward lives in one of
            // the two quest slots (Denathrius).
            void RewardOnGain(int player, int slot);
            void RewardStartTurn(int player);
            void RewardEndTurn(int player);
            void RewardRefresh(int player);
            void RewardWisdomball(int player);
            void RewardBuy(int player, uint32_t uid);
            void RewardSell(int player, uint32_t uid);
            void RewardDiscover(int player, int card, bool golden);
            void Wheel(int player);                                  // Yogg-tastic Tasties: one spin (rolls WheelOutcome)
            AuraBonus RewardBoardAura(PlayerShop const& ps) const;   // The Smoking Gun (+5 Attack)
            void RecomputeBoard(int player)
            {
                RecomputeAuras(State(player).board, m_effects, RewardBoardAura(State(player)));
                CheckNagaConquest(player);
            }
            // Copies of a normal card that already make a Golden: 3, or 2 for Pilfered Lamps and First Mate Pip.
            int TripleNeed(int player, int card = -1) const;
            void ResolveTriples(int player);
            bool PushToHand(int player, Minion const& m);
            int NormalCopies(int player, int card, int* inHand = nullptr) const;
            void ReturnToPool(int player, Minion const& m);
            int OwnPool(int player, int card) const;
            void ReturnOffers(int player);
            void FillOffers(int player);
            void QueueDiscover(int player, DiscoverRequest const& request);
            void ActivateDiscover(int player);
            ActionResult Gate(int player, bool discoverOk = false) const;
            Minion MakeCopy(int card, bool golden, int poolCopies);

            PoolSet& m_pool;
            EffectRegistry const& m_effects;
            HeroPowerRegistry const& m_heroPowers;
            Rng& m_rng;
            std::vector<PlayerShop> m_players;
            uint32_t m_nextUid = 1;
            int m_turn = 1;
            int m_depth = 0;
            // Context of the hook being dispatched (see ShopCtx::playedCard / amountAttack): saved and restored around
            // the dispatch of onFriendlyPlayed / onFriendlySummonedShop / onFriendBuffed.
            int m_playedCard = -1;
            bool m_playedGolden = false;
            bool m_playedMagnetic = false;
            int m_buffAttack = 0;
            int m_buffHealth = 0;
            std::vector<uint32_t> m_lobbyTribes;   // The Friends Along the Way picks a type from these
            int m_actionDepth = 0;                 // open ActionScopes (rewards turn on at depth 0)
        };
    }
}

#endif
