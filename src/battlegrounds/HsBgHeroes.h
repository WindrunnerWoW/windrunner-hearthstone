/*
 * Battlegrounds heroes and hero powers: 86 curated heroes and their powers.
 * A hero power is DATA (a HeroPowerDef keyed by the hero power id of the generated hero table), not a card: the same
 * way the effect registry keys effects by card id (HsBgEffects.h). Shop owns one PowerState per player and calls the
 * passive modifiers and hooks; the active powers are a Shop method (HeroPower) that validates the target and calls the
 * power's activate function.
 *
 * PURE C++ (the standard library, HsBgTypes.h; Shop is only forward-declared here, HsBgHeroes.cpp includes HsBgShop.h).
 *
 * The registry is complete for every curated hero (bg_heroes_tests fails when one has no entry).
 */

#ifndef MOD_HEARTHSTONE_HS_BG_HEROES_H
#define MOD_HEARTHSTONE_HS_BG_HEROES_H

#include "HsBgTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        class Shop;

        // Cariel Roame's Conviction improvements, offered as a power choice. They have no hero power def; the addon's card
        // DB names them (tools/battlegrounds/gen_bg_lua.py, SYNTHETIC_POWERS).
        constexpr char const* kStanceUnited = "BG21_HERO_000p_united";
        constexpr char const* kStanceOffense = "BG21_HERO_000p_offense";
        constexpr char const* kStanceDefense = "BG21_HERO_000p_defense";

        // What the active power needs clicked after it: nothing, a friendly board minion, or a tavern offer.
        // Seat: a player (Lord Barov; the shop gets the seat as the offer index). TwoMinions: two minions of the board or the
        // tavern (Vol'jin; the second one reaches the power in PowerState::secondTarget).
        enum class PowerTarget : uint8_t { None, FriendlyMinion, TavernOffer, Seat, TwoMinions };

        // Per-player hero power state: use limits and the small counters the powers keep.
        struct PowerState
        {
            int usesThisTurn = 0;
            int usesThisGame = 0;
            int buysThisTurn = 0;        // Aranna Starseeker (the first buy of a turn)
            int buysThisGame = 0;        // Edwin VanCleef (the buff grows every 4 buys)
            int playsThisTurn = 0;       // Kael'thas Sunstrider
            int refreshesThisTurn = 0;   // Nozdormu
            int refreshesThisGame = 0;
            int friendlyAttacks = 0;     // Aranna Starseeker: attacks of the player's minions in all combats so far
            int pendingGold = 0;         // Gallywix: arrives at the start of the next turn
            int elementalPlaysThisTurn = 0;  // Majordomo Executus
            int elementalPlaysThisGame = 0;  // Chenvaala: every 3 lowers an upgrade
            int healthBuffBonus = 0;     // Pyramad: grows each turn the power is not used
            int powerCostDiscount = 0;   // Patches (per Pirate bought) and Togwaggle (per turn)
            int digs = 0;                // Captain Eudora: digs done toward the Golden minion
            uint32_t ratKingTribe = 0;   // The Rat King: the minion type of this turn
            int kurtrusRank = 0;         // Kurtrus Ashfallen: 0, 1, 2
            int kurtrusBuys = 0;         // minions bought this turn toward the rank
            bool kurtrusDone = false;
            std::vector<uint32_t> kurtrusUids;   // the minions bought this turn
            int cthunRepeats = 0;        // C'Thun: extra end-of-turn buffs (one more each turn)
            bool cthunThisTurn = false;  // C'Thun: the power was used this turn
            // Naga and the coin heroes
            bool nagaConquest = false;   // Queen Azshara: the warband reached 25 Attack, Naga Conquest is active
            int barovGuess = -1;         // Lord Barov: the seat guessed for the next combat (-1 = none)
            int holmesOpponent = -1;     // Murloc Holmes: the card index of the opponent's minion among the two options
            // Cariel Roame: the Conviction improvements chosen after Tavern upgrades
            int stanceUnited = 0;        // United Stance: one more minion per use
            int stanceOffense = 0;       // Offensive Stance: +2 Attack to each buff
            int stanceDefense = 0;       // Defensive Stance: +2 Health to each buff
            uint32_t secondTarget = 0;   // Vol'jin: the uid of the second chosen minion while the power activates
            // Quilboar and the simple heroes
            int sellsThisGame = 0;       // Fungalmancer Flurgl: sold minions toward the next random Murloc
            int friendlyDeaths = 0;      // Ini Stormcoil: friendly minions that died in combat
            int battlecriesBought = 0;   // Dinotamer Brann: bought Battlecry minions toward Brann Bronzebeard
            bool brannDone = false;      // ... the once-per-game reward was handed out
            // The last heroes
            int minionsSoldThisGame = 0; // Ozumat: the Tentacle's +1/+1
            bool combatArmed = false;    // Y'Shaarj / Rafaam: the next combat uses the power
            // The power swap and the choices
            std::string powerOverride;   // the power that replaces the hero's own (a flightpath, Sulfuras, a swap)
            std::string combatPower;     // Tavish's aim / Bru'kan's invocation: the power that acts in combat
            bool combatPowerOnce = false; // Tavish: the aim lasts one combat
            int powerCountdown = 0;      // Galewing: the turns left on the chosen flightpath
            int enemyKills = 0;          // Ragnaros: enemy minions killed toward Sulfuras
            // The shop-system heroes
            uint32_t potTypes = 0;       // Cookie the Cook: the minion types thrown in the pot
            int potCount = 0;            // ... how many are in it (max 3)
            bool holdsOffer = false;     // Maiev Shadowsong: a tavern offer is held dormant
            Minion heldOffer;            // ... the held minion (its pool copy is kept)
            int heldTurns = 0;           // ... StartTurns left before it comes to the hand
            int tickets = 0;             // Silas Darkmoon: Darkmoon Tickets collected (0-2; 3 pays out)
            std::vector<Minion> expedition; // Ambassador Faelin: the Tier 2 / 4 / 6 minions waiting for their Tier
            // Tickatus's Prize Wall activations so far (0-4; it stops after the turn-16 one).
            int prizesTaken = 0;
            // Uses per hero power id: a swapped-in power (Nguyen, Finley) keeps its own once-per-game limit.
            std::map<std::string, int> usesByPower;
            int Uses(std::string const& powerId) const
            {
                auto it = usesByPower.find(powerId);
                return it == usesByPower.end() ? 0 : it->second;
            }
            void AddUse(std::string const& powerId) { ++usesByPower[powerId]; }
        };

        // The hero powers that act inside a combat. Game fills one per side from HeroCombatDefFor.
        struct HeroCombatDef
        {
            bool deathwing = false;   // Start of Combat: ALL minions get +2 Attack permanently
            bool alkair = false;      // Start of Combat: the left-most minion gets Windfury, Divine Shield and Taunt
            bool greybough = false;   // summons in combat get +1/+2 and Taunt
            bool illidan = false;     // Start of Combat: the ends get +2 Attack and attack immediately
            bool rokara = false;       // after a friendly minion kills an enemy, it gets +1 Attack permanently
            int avengeCount = 0;      // Drek'Thar (3), Vanndar (2), Onyxia (4); 0 = none
            int avengeAttack = 0;     // Drek'Thar: +1 Attack to your minions
            int avengeHealth = 0;     // Vanndar: +1 Health to your minions
            bool avengeWhelp = false; // Onyxia: summon a 3/1 Whelp that attacks immediately
            bool wagtoggle = false;   // Queen Wagtoggle: Start of Combat, 3 different-type minions get stats = their Tier
            int phylacteryTargets = 0; // Tamsin Roame: destroy the lowest-Health minion, give its stats to this many
            bool ozumat = false;       // Start of Combat: summon a Tentacle of 2/2 plus `ozumatSold`
            int ozumatSold = 0;
            bool yshaarj = false;       // Start of Combat: a Tier minion to the hand, a copy to the board
            // Bru'kan's invocations and Tavish's aim, which act at Start of Combat.
            bool invocationEarth = false;    // 4 random friends get "Deathrattle: summon a 1/1 Elemental"
            bool invocationFire = false;     // double the left-most minion's Attack
            bool invocationWater = false;    // the right-most minion gets +3 Health and Taunt
            bool invocationLightning = false; // 1 damage to 5 random enemies
            int tavishAim = 0;               // 1 left, 2 lowest Health, 3 highest Health, 4 right; 0 = none
        };
        HeroCombatDef HeroCombatDefFor(std::string const& powerId);

        using PowerActivate = void (*)(Shop& shop, int player, PowerState& state, uint32_t targetUid, int offerIndex);
        // Whether the power would do anything to this target; checked before gold or a use is spent.
        using PowerTargetCheck = bool (*)(Shop& shop, int player, uint32_t targetUid);

        // One hero power. `id` is the hero power id of the hero table (HeroDef::powerId). A cost of 0 with no activate
        // is a passive power. The modifier fields are read by Shop; activate runs the active power.
        struct HeroPowerDef
        {
            char const* id = "";
            int cost = 0;                 // active cost; 0 for a passive power
            int costGrowthPerUse = 0;     // Elise Starseeker: the cost of each later use
            PowerTarget target = PowerTarget::None;
            bool oncePerTurn = false;
            bool oncePerGame = false;
            // Passive modifiers (0 / false when the power does not use them).
            int buyCostDelta = 0;         // Millhouse Manastorm
            int refreshCostDelta = 0;
            int upgradeCostDelta = 0;
            bool firstRefreshFree = false; // Nozdormu
            int goldOnUpgrade = 0;        // Forest Warden Omu
            int goldOnSellNextTurn = 0;   // Trade Prince Gallywix
            bool hatOnBuy = false;        // Dancin' Deryl: a bought minion gets a +1/+1 hat that passes on when it is sold
            bool thirdPlayBuff = false;   // Kael'thas Sunstrider
            int freeBuyAfterAttacks = 0;  // Aranna Starseeker: after this many friendly attacks the first buy of a turn is free
            uint32_t summonAttackTribe = 0; // Millificent Manastorm: a summoned minion of this tribe (kTribeMech) gets
            int summonAttack = 0;           //   this much Attack, in the shop and in combat
            int elementalPlaysPerUpgrade = 0; // Chenvaala: every N Elementals played
            int upgradeCostReduction = 0;     //   reduce the upgrade cost by this much
            char const* startMinion = nullptr; // The Curator: this generated minion starts on the board
            bool growsHealthBuff = false;     // Pyramad: the buff grows each turn the power is not used
            // Pirates and the simple heroes
            uint32_t extraOfferTribe = 0;     // Ysera: one extra offer of this tribe on every refresh
            int discountPerTurn = 0;          // Heistbaron Togwaggle: the power costs this much less each turn
            bool buyOfTribeReducesCost = false; // Patches: a bought Pirate lowers the next power by (1)
            bool kurtrusPassive = false;       // Kurtrus Ashfallen: buy N minions in 1 turn
            bool ratKingDiscover = false;      // The Rat King: Discover a minion of the turn's type
            bool cthunEndOfTurn = false;       // C'Thun: an end-of-turn buff when the power was used
            int discoverTribeAtTier = 0;       // Alexstrasza: at this Tavern Tier, Discover ...
            uint32_t discoverTribe = 0;        //   ... this tribe
            int discoverCount = 0;             //   ... this many times
            bool digsForGolden = false;        // Captain Eudora: digs make a Golden minion
            int digsNeeded = 5;
            // Naga and the coin heroes
            int usesPerTurn = 0;               // 0 = follow oncePerTurn; Malygos is twice per turn
            int usesPerGame = 0;               // 0 = unlimited; Zephrys has 3 wishes
            char const* conquestPowerId = nullptr; // Queen Azshara: the power that replaces this one at 25 Attack
            bool bananarama = false;           // King Mukla: StartTurn bananas for everyone
            bool holmesCoin = false;           // Murloc Holmes: the coin-guess power
            bool barovGuess = false;           // Lord Barov: guess a player's next combat
            // Quilboar and the simple heroes
            bool twiceAsNice = false;          // Varden Dawngrasp: a Refresh copies the highest-Tier offer and freezes
            bool stayFrosty = false;           // Sindragosa: the active freezes, the passive buffs frozen offers at end of turn
            int murlocPerSells = 0;            // Fungalmancer Flurgl: every N sells, a random Murloc
            uint32_t needsLobbyTribe = 0;      // the power draws this tribe from the lobby pool: not dealt where it is absent
            int mechPerDeaths = 0;             // Ini Stormcoil: every N friendly combat deaths, a random Mech
            bool brannReward = false;          // Dinotamer Brann: after N Battlecry buys, add Brann (once per game)
            int brannBattlecries = 5;
            // The last heroes
            bool armsCombat = false;           // Y'Shaarj / Rafaam: the use arms the next combat (PowerState::combatArmed)
            // The power swap and the choices
            int countdownTurns = 0;            // Galewing's flightpaths: the passive completes after this many turns
            int countdownGold = 0;             // Ironforge: Gold gained when it completes
            int countdownAttack = 0;           // Westfall: the left-most minion gets this when it completes
            int countdownHealth = 0;
            bool countdownDiscover = false;    // Eastern Plaguelands: Discover a minion of the Tavern Tier
            bool swapEachTurn = false;         // Master Nguyen: a new power choice at every StartTurn
            bool startPowerChoice = false;     // Sir Finley: a power choice at the start of the game
            int killsForSulfuras = 0;          // Ragnaros: enemy kills that turn the power into Sulfuras
            bool endOfTurnEnds = false;        // Sulfuras: +3/+3 to the left- and right-most minions at EndTurn
            // The shop-system heroes
            bool consumesTarget = false;       // Hooktusk / Cookie: the friendly target is removed by the power
            bool holdsOffer = false;           // Maiev: the target tavern offer is held dormant (3 StartTurns, +2/+2)
            bool pot = false;                  // Cookie: the pot counts thrown minions, then Discovers
            bool bigglesworth = false;         // Mr. Bigglesworth: a Discover when another hero dies
            bool ticketsInTavern = false;      // Silas Darkmoon: one tavern offer per refresh / StartTurn has a ticket
            bool startQuestOffer = false;      // Sire Denathrius: a quest choice at the start of the game
            int skipTurns = 0;                 // A. F. Kay (2) / Ambassador Faelin (1): turns whose shop phase ends at once
            int skipDiscoverCount = 0;         // A. F. Kay: the number of Discovers after the skipped turns
            int skipDiscoverTier = 0;          // ... at this Tavern Tier
            bool expeditionTiers = false;      // Ambassador Faelin: Discover a Tier 2, 4 and 6 minion, held for those Tiers
            // Tickatus
            bool stanceChoice = false;         // Cariel Roame: choose an improvement after each Tavern upgrade
            int prizesEvery = 0;               // a Darkmoon Prize Discover every N shop turns (4, 8, 12, 16), then stop
            PowerActivate activate = nullptr;
            PowerTargetCheck targetOk = nullptr;   // null = any board minion
        };

        class HeroPowerRegistry
        {
        public:
            void Register(HeroPowerDef const& def);
            HeroPowerDef const* Find(std::string const& powerId) const;
            int Size() const { return static_cast<int>(m_defs.size()); }
            std::vector<std::string> const& Errors() const { return m_errors; }
            // All the first heroes' powers, built once.
            static HeroPowerRegistry const& Global();
        private:
            std::map<std::string, HeroPowerDef> m_defs;
            std::vector<std::string> m_errors;
        };

        // Registers every implemented power into `reg` (a duplicate id is an error).
        void RegisterAllHeroPowers(HeroPowerRegistry& reg);

        // The cost of the power's next use (base + Elise's growth); 0 for a passive power. The growth counts the uses of
        // this power id (a swapped-in power keeps its own count).
        int HeroPowerCost(HeroPowerDef const& def, PowerState const& state);
        // A fixed value for an NPC hero pick (higher is better).
        int HeroPickValue(int heroIndex);
        // The fixed value of a power (the choice options and the swap pool use it too).
        int PowerPickValue(std::string const& powerId);
        // The 10 Bacon Secrets (card indexes, in the order Akazamzarak's Discover filters them).
        std::vector<int> SecretCardIds();
        // The Darkmoon Prizes of a Prize turn (1-4), card indexes; prizeTurn -1 is every prize (Hand of
        // Fate). The list is empty for an unknown turn.
        std::vector<int> DarkmoonPrizeIds(int prizeTurn);
        // The Prize turn (1-4) of a prize card, 0 when it is not a prize.
        int DarkmoonPrizeTurnOf(int cardIndex);
        // The Prize turn of a shop turn: 0 before turn 4, then 1 at 4, 2 at 8, 3 at 12 and 4 from 16 on.
        int DarkmoonPrizeTurnForRound(int round);
        // Tickatus's power button: the turns until the next prize (0 once the fourth one was taken).
        int DarkmoonPrizeTurnsLeft(int round, int taken);
    }
}

#endif
