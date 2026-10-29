/*
 * Match engine internals shared by HsMatch.cpp (the rules and the effect
 * interpreter), the scripted card tables (HsMatchCards.cpp: heroes, hero
 * powers, engine cards and the keyword parser; HsMatchCardsBasic.cpp,
 * HsMatchCardsClassic*.cpp and HsMatchCardsNaxx.cpp: the cards) and the tests. Not part of the public API.
 *
 * Scripts are DATA built once at startup with a small builder (see
 * ScriptBuilder below): a target rule, play requirements, effect Steps
 * (reusable primitives such as "damage", "heal", "summon", applied to a
 * selection such as "the target", "all enemy minions" or "a random enemy
 * character"), Combo steps, Choose One options, a Deathrattle, triggered
 * abilities, auras (Enrage is an aura that is active while the minion is
 * damaged), a cost rule and, only where no primitive fits, a Custom handler
 * that drives the engine through the Effects facade. Adding a card means
 * adding a builder chain; adding an Op means implementing it once in
 * HsMatch.cpp RunStep (and documenting it here).
 *
 * Sequencing (simplified from Hearthstone 5834):
 *  - An action runs its main effect; triggered abilities that fire meanwhile
 *    are QUEUED and resolve afterwards in the order their owners entered play
 *    (play order), each followed by a death step; a death step removes every
 *    minion at 0 or less health (or destroyed) at once, in board order (side
 *    of the acting player first), then queues Died triggers and Deathrattles
 *    (in play order of the dead), then checks the heroes. This repeats until
 *    nothing is queued and nothing is dead.
 *  - "Pre" triggers resolve at once, before the thing they react to:
 *    SpellCast (Counterspell, Spellbender, Mana Wyrm ...), Attacked (attack
 *    secrets; the attack is cancelled when attacker or defender left play or
 *    died) and FatalDamage (Ice Block).
 *  - Termination: at most kMaxResolutionSteps trigger/deathrattle resolutions
 *    per action; the rest are dropped (a TurnLimit-free safety net, logged in
 *    DebugState as "capped").
 *  - Auras are computed on demand; health auras keep the Hearthstone rule
 *    "losing an aura never kills": when a minion's aura health drops by d,
 *    its damage drops by min(damage, d).
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_INTERNAL_H
#define MOD_HEARTHSTONE_HS_MATCH_INTERNAL_H

#include "HsMatch.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hs
{
namespace Match
{
namespace Detail
{
    constexpr int kMaxResolutionSteps = 400;
    constexpr int kMaxSecrets = 5;

    // ---- targeting ------------------------------------------------------------

    // Who a card, battlecry or hero power may point at. Enemy minions with Stealth, and
    // Immune enemy characters, are never valid targets. Elusive minions (Faerie Dragon)
    // are not valid targets of spells and hero powers.
    enum class TargetKind : uint8_t
    {
        None,              // no target (the action's target must be 0)
        AnyCharacter,      // either hero or any minion
        AnyMinion,         // any minion of either side
        AnyHero,           // either hero
        FriendlyCharacter, // the own hero or a minion of the acting side
        FriendlyMinion,    // a minion of the acting side
        EnemyCharacter,    // the other side's hero or minion
        EnemyMinion,       // a minion of the other side
        // Name, kept: EnemyMinion + kTgtDamaged
        EnemyDamagedMinion
    };

    enum TargetFlags : unsigned
    {
        kTgtOptional = 1,   // battlecry REQ_TARGET_IF_AVAILABLE: when no valid target exists the card is
                            // played with target 0 and the targeted steps do nothing
        kTgtDamaged = 2,    // the target must be damaged
        kTgtUndamaged = 4,  // the target must be undamaged
        kTgtMaxAttack = 8,  // target attack <= value
        kTgtMinAttack = 16, // target attack >= value
        kTgtRace = 32,      // a minion of race
        kTgtNonSelf = 64,   // not the source itself (only matters for on-board sources)
        kTgtTaunt = 128     // Classic: a minion with Taunt (The Black Knight)
        // ---- the rest of Classic ----
        , kTgtRoomToSteal = 256 // the acting side needs a free board slot for the stolen minion: when the source is a
                                // minion CARD being played it takes a slot too (Cabal Shadow Priest); with no room
                                // there is no valid target and an optional battlecry is played without one
    };

    struct TargetRule
    {
        TargetKind kind = TargetKind::None;
        unsigned flags = 0;
        int value = 0;
        std::string race;
    };

    // Play requirements (checked by validation and the legal list).
    enum ReqFlags : unsigned
    {
        kReqWeapon = 1,          // REQ_WEAPON_EQUIPPED
        kReqMinEnemyMinions = 2, // at least reqValue living enemy minions
        kReqFreeSlot = 4,        // REQ_NUM_MINION_SLOTS: a free slot on the own board
        kReqMinTotalMinions = 8, // at least reqValue living minions on both boards
        kReqMissingTotem = 16    // Totemic Call: a basic totem is not on the own board
    };

    // ---- selections -------------------------------------------------------------

    // What a step affects. "Friendly" / "Enemy" are relative to the side the effect belongs
    // to (the player of the card, the controller of the minion / weapon / secret). Lists are in
    // board order (own side first, hero before minions). Random picks consider living
    // characters only and use the match RNG.
    enum class Sel : uint8_t
    {
        None,
        Target,              // the chosen target (or, in a trigger, the attack's defender / the spell's target)
        Self,                // the source: the minion itself; for a spell / hero power / secret / weapon: nothing
        Subject,             // what a trigger is about: the damaged / healed / summoned / played / dying / attacking entity
        LastSummoned,        // the minion summoned by the latest Summon step of this resolution
        OwnHero,
        EnemyHero,
        BothHeroes,
        FriendlyMinions,
        OtherFriendlyMinions, // friendly minions except Self
        EnemyMinions,
        AllMinions,
        AllOtherMinions,     // except Self
        FriendlyCharacters,
        EnemyCharacters,
        AllCharacters,
        AllOtherCharacters,  // except Self
        AllOtherThanTarget,  // all characters of the target's side except the target (Swipe: enemy side)
        AdjacentToSelf,
        AdjacentToTarget,
        // ---- boss encounters (kept before the random selections: the AI treats every Sel from RandomEnemyMinion
        // on as a random outcome) ----
        LeftmostEnemyMinion, // the first living enemy minion in board order (Heigan's Eruption); nothing without one
        AdjacentAtDeath,     // a Deathrattle only: the living minions that stood left and right of the dying minion when
                             // it died (Necroknight); a neighbour that died in the same death step is gone
        RandomEnemyMinion,
        RandomEnemyCharacter,
        RandomFriendlyMinion,
        RandomOtherFriendlyMinion,
        RandomMinion,
        RandomOtherMinion,
        RandomOtherCharacter,  // any living character except Self (Mad Bomber)
        RandomOtherCharacterForAttack, // Misdirection: a random living character other than the attacker and the defender
        // ---- Classic engine cards ----
        RandomDamagedCharacter // a random damaged living character of either side (Repair Bot)
    };

    // ---- effect primitives --------------------------------------------------------

    // Effect primitives. Unless stated otherwise each applies to every entity of the step's
    // selection (Sel); a/b/card/list meaning per primitive. Amounts with kStepSpellPower grow by
    // the Spell Damage of the effect's side when a spell resolves the step.
    enum class Op : uint8_t
    {
        Damage,             // a damage
        Heal,               // restore a health
        HealFull,           // restore to full health
        Destroy,            // destroy (dies in the next death step)
        Silence,
        Freeze,
        Buff,               // +a attack, +b health (max health too); permanent
        BuffTemp,           // +a attack until the end of the turn (minions and heroes)
        Keyword,            // grant keyword a (KeywordBit), permanent
        KeywordTemp,        // grant keyword a until the end of the turn (Immune, Stealth ...)
        SetAttack,          // attack becomes a (Humility)
        SetHealth,          // max health and health become a (Hunter's Mark)
        AttackToHealth,     // attack becomes current health (Inner Fire)
        DoubleHealth,       // +current health as a health buff (Divine Spirit)
        DoubleAttack,       // +current attack as an attack buff (Blessed Champion)
        Transform,          // becomes card (same id and slot, fresh stats, keeps exhaustion)
        TransformRandomList,// becomes a random card of list
        Summon,             // summon a copies of card for the own side (b = 1: for the enemy side) right of
                            // Self (battlecries), at the dead minion's slot (deathrattles) or rightmost
        SummonRandomList,   // summon a random card of list (a copies); kStepUnique: not already on the board
        SummonCopy,         // summon a copy of each selected minion for the own side (Mirror Entity, Faceless: see Custom)
        Resummon,           // summon a fresh copy of each selected (dead) minion's card with health a (Redemption: 1); 0 = full
        Equip,              // equip card (replaces the current weapon)
        DestroyWeapon,      // the weapon of each selected hero is destroyed
        WeaponBuff,         // the own weapon: +a attack, +b durability (needs a weapon; no-op without)
        Armor,              // each selected hero gains a armor
        Draw,               // the side of each selected hero draws a cards
        GainMana,           // a mana this turn for the own side (capped at 10)
        GainCrystals,       // a mana crystals for the own side (sel EnemyHero: the enemy side, Arcane Golem): b = 1 full,
                            // 0 empty; at 10 crystals an EMPTY one becomes an Excess Mana card, a full one does nothing
        DestroyCrystals,    // destroy a of the own side's crystals (b = 1: the enemy side's)
        AddToHand,          // put card into the hand of each selected hero's side (a copies)
        AddRandomToHand,    // put a random card of list into the own hand (b = 1: into the enemy hand)
        CopyEnemyHandCard,  // a copies of random cards from the enemy hand into the own hand (Mind Vision)
        DiscardRandom,      // the own side discards a random cards
        Bounce,             // return each selected minion to its owner's hand (b = cost change of the card)
        TakeControl,        // each selected enemy minion moves to the own side (destroyed when the board is full)
        TakeControlRandom,  // a random enemy minion moves to the own side (Sylvanas)
        Pick,               // the own side looks at the top a cards of its deck: a pending pick (draw one, the
                            // others are discarded)
        DiscountNextSpell,  // the next spell of the own side this turn costs a less (Preparation)
        FreeNextSecret,     // the next secret of the own side this turn costs 0 (Kirin Tor Mage)
        EnemySpellsFreeNext,// the enemy's spells cost 0 during its next turn (Millhouse Manastorm)
        Counter,            // the spell being cast is countered (Counterspell)
        RedirectAttack,     // the attack being declared now targets the first entity of the selection
        RedirectSpell,      // the spell being cast now targets the first entity of the selection (Spellbender)
        PreventDamage,      // the fatal damage being dealt is prevented (Ice Block)
        HeroAttack,         // +a temporary attack for the own hero (= BuffTemp on OwnHero)
        DelayedDestroy,     // b = 0: destroyed at the start of the effect side's next turn (Corruption);
                            // b = 1: destroyed at the end of this turn (Power Overwhelming). Silence removes it.
        ReplaceHeroPower,   // the own hero power becomes card (fresh, usable this turn)
        Custom,             // custom(fx): anything the primitives cannot express
        // ---- Classic cards (appended) ----
        SwapWithHandMinion, // Self (a minion on the board) swaps places with a random minion card of the own hand: the
                            // hand minion is summoned into Self's slot (no battlecry), Self returns to the hand
                            // (Alarm-o-Bot); nothing happens without a minion card in the hand
        SummonFromEnemyDeck,// summon a copy of a random MINION card of the enemy's deck for the own side; card = the
                            // minion summoned instead when the enemy deck holds none (Mindgames)
        // ---- Classic engine cards ----
        GrantScript,        // attach the enchantment script card (Deathrattle / triggers) to each selected minion; it works
                            // for the granting side (the own side), is removed by Silence and lost when the minion leaves
                            // play, is bounced or transformed, and stays when control changes. Granted Deathrattles run
                            // after the minion's own, in the order they were granted (Soul of the Forest, Ancestral
                            // Spirit); a granted trigger counts as owned by the granting side with the minion as Self
                            // (Blessing of Wisdom).
        TakeControlTemp,    // each selected enemy minion moves to the own side until the end of this turn and may attack at
                            // once; Silence or another change of control makes it permanent (Shadow Madness)
        ProtectMinions,     // the own minions cannot be reduced below 1 Health for the rest of this turn (Commanding Shout)
        AddToHandNextTurn,  // card goes into the own hand at the start of the own next turn (Headcrack)
        DrawDiscount,       // draw a card, it costs a less (Far Sight); nothing happens to a burned or missing card
        DrawDealCost,       // draw a card and deal damage equal to its cost (burned: its cost, none left: 0) to the selection;
                            // kStepSpellPower adds Spell Damage when a spell resolves it (Holy Wrath)
        SetHeroHealth,      // each selected hero's Health becomes a (the maximum grows to a when lower): not a heal
        CopyEnemyDeckCards, // a copies of distinct random cards of the enemy deck go into the own hand (Thoughtsteal); a
                            // redacted copy puts unknown placeholder cards there
        RemoveKeyword,      // each selected minion loses the keyword a (KeywordBit, no auras); counts into the context's
                            // "removed" counter
        BuffPerRemoved,     // each selected minion gets +a/+b for every keyword removed so far in this resolution
        DestroyEnemySecrets,// every Secret of the enemy side is destroyed (Flare)
        CopySubjectToOtherHand, // the trigger's subject card (a spell being cast) is copied into the hand of the side that did not
                            // play it (Lorewalker Cho)
        SwapAttackHealth,   // each selected minion: Attack becomes its Health, Health (and maximum) its Attack, undamaged
        DamageFromTarget,   // each selected character takes damage equal to the Attack of the target minion, dealt BY that
                            // minion (Poisonous works): Betrayal
        DiscardHand,        // the own side discards its whole hand (Deathwing)
        TakeRandomFromDeck, // a random card of the own deck whose race is Step::race (empty: any minion) goes into the hand
        ReplaceHero,        // the own hero becomes the hero card (health, maximum 15 from the card, armor lost, thawed)
        RemoveFromPlay,     // each selected minion leaves play without dying (no death triggers, no deathrattle)
        // ---- the rest of Classic ----
        SpellDamageBuff,     // each selected minion gets Spell Damage +a (permanent; Silence removes it) (Ancient Mage)
        // ---- Naxxramas ----
        Settle,              // resolve the queued abilities and the death steps NOW, in the middle of an effect (spells and
                             // battlecries only: the steps after it see the aftermath, Deathrattles included)
        DestroyReplace,      // destroy each selected minion, Settle, then summon for each one's side (right end) a fresh
                             // minion: card (Poison Seeds' Treant), or, with card empty, a copy of what was destroyed
                             // (Reincarnate). A full board summons nothing.
        SummonFromHand,      // a random minion card of the own hand (race filter: Step::race) goes into the battlefield: it
                             // keeps its entity, no Battlecry, Summoned triggers fire; nothing with a full board (Voidcaller)
        SummonFromDeck,      // the same from a deck: b = 0 the own deck, b = 1 the ENEMY deck onto the ENEMY board (Deathlord)
        SecretFromDeck,      // a random Secret of the own deck goes into play (no cast, no cost): not one whose card is already
                             // active, nothing at the secret limit (Mad Scientist)
        SpellTaxEnemy,       // the enemy's spells cost a more during its next turn (Loatheb); effects add up
        // ---- Naxxramas, wave 2 ----
        EchoMark,            // Self (a minion) will copy itself at the end of the turn (Echoing Ooze's Battlecry)
        EchoCopy,            // Self, when marked and alive, clears the mark and summons an exact copy of itself right of it (the
                             // copy has stats, damage, keywords, enchantments and grants as Self has now, but no mark); nothing
                             // without a free slot (the mark is spent anyway)
        SummonDied,          // summon, at the right end of the own board, a fresh minion of every card of the own side's minions
                             // that died this turn, in order of death, while there is room; Self must be alive (Kel'Thuzad)
        // ---- boss encounters ----
        TakeControlCard,     // take control of every enemy minion of the card `card`, left to right, while the own board has
                             // room; the ones that do not fit stay where they are (Mind Control Crystal)
        // ---- boss encounters, wings 4-5 ----
        DestroyUnfrozen      // destroy every selected minion that is not Frozen and not protected by a neighbouring Frozen
                             // Champion (kKwFrostProof), whatever its other keywords (Sapphiron's Frost Breath)
    };

    enum KeywordBit : int
    {
        kKwTaunt = 1,
        kKwCharge = 2,
        kKwDivineShield = 4,
        kKwWindfury = 8,
        kKwStealth = 16,
        kKwImmune = 32,
        kKwPoisonous = 64,
        kKwCantAttack = 128,
        kKwFrostProof = 256  // Immune to Frost Breath (only ever granted by the Frozen Champion's aura)
    };

    enum StepFlags : unsigned
    {
        kStepSpellPower = 1,      // the amount grows with Spell Damage when a spell resolves the step
        kStepCountSpellPower = 2, // the number of hits/copies (b for Damage on random selections) grows with Spell Damage
        kStepGrantCharge = 4,     // Buff also gives Charge
        kStepUnique = 8,          // SummonRandomList: only cards not already on the own board
        kStepRepeatRandom = 16,   // a random selection is re-rolled count times (Arcane Missiles: 1 damage, 3 hits)
        kStepUntilNextTurn = 32,  // KeywordTemp: lasts until the start of the effect side's next turn (Conceal)
        kStepTriggerAmount = 64,  // the amount is the trigger's amount (Eye for an Eye: the damage taken)
        kStepRandomBonus = 128    // Damage: every target takes a + (0..b) damage, rolled per target (Lightning Storm: 2 + 0..1)
    };

    // A step runs only when its condition holds (checked when the step starts).
    enum class Cond : uint8_t
    {
        None,
        HaveRace,         // the own board has a living minion of race (Kill Command)
        NoRace,           // ... has none
        TargetDead,       // the target is dead / destroyed now (Mortal Coil, after its damage step)
        TargetAlive,
        HandNotEmpty,
        EnemyHasWeapon,
        HaveWeapon,
        DamagedSelf,      // Self is damaged
        FreeSlot,         // the own board has a free slot
        SubjectMinion,    // the trigger's subject is a minion
        TargetMinion,     // the target is a minion (Spellbender)
        TargetFrozen,     // the target is frozen (Ice Lance)
        TargetNotFrozen,
        // ---- Classic cards (appended) ----
        TargetMinionFreeSlot, // the target is a minion AND the own board has a free slot (Spellbender)
        NoWeapon,             // the own hero has no weapon (Upgrade!)
        // ---- Classic engine cards ----
        HaveSecret,           // the own side controls a Secret (Ethereal Arcanist)
        // ---- Naxxramas, wave 2 ----
        HaveMinion,           // the own board has a living minion (Avenge: checked when the secret would fire, the dying
                              // minions are already off the board)
        EchoPending,          // Self is a minion marked by Op::EchoMark
        StalaggDied,          // a Stalagg (either side) died this game (Feugen's Deathrattle)
        FeugenDied            // a Feugen (either side) died this game (Stalagg's Deathrattle)
    };

    struct Effects; // facade for Custom handlers (below)
    using CustomFn = void (*)(Effects& fx);

    struct Step
    {
        Op op = Op::Damage;
        Sel sel = Sel::None;
        int a = 0;
        int b = 0;
        int count = 0;                 // random selections: how many distinct picks (0 = 1); with kStepRepeatRandom
                                       // the number of hits, re-rolled each time
        std::string card;              // Transform / Summon / Equip / AddToHand ...
        std::vector<std::string> list; // ... RandomList ops
        std::string race;              // when set, the selection keeps minions of this race only
        unsigned flags = 0;
        Cond cond = Cond::None;
        std::string condRace;          // HaveRace / NoRace
        CustomFn custom = nullptr;
    };

    // ---- triggers -------------------------------------------------------------------

    enum class On : uint8_t
    {
        TurnStart,    // whose turn: Who (Friendly = own turn, Enemy = the other side's, Any)
        TurnEnd,
        Summoned,     // a minion entered play (summoned or played), after it is on the board; Subject = it
        Played,       // a card was played (after a minion's battlecry / a weapon's equip; before a spell resolves
                      // its pre triggers fire as SpellCast); Subject = the card
        SpellCast,    // PRE: a spell is being cast (before its effect); Subject = the spell, Target = its target
        AfterSpell,   // a spell finished resolving ("After you cast a spell")
        Damaged,      // a character took damage (> 0); Subject = it, amount = damage; source of the damage known
        DealtDamage,  // Self dealt damage (Water Elemental); Subject = the damaged character
        Healed,       // a character was healed (> 0); Subject = it
        Died,         // a minion died; Subject = it (it is already off the board)
        Attacked,     // PRE: an attack is declared; Subject = the attacker, Target = the defender; Who filters the DEFENDER,
                      // kTrigMinion / kTrigHero filter the ATTACKER, kTrigDefenderMinion the defender
        Attacks,      // PRE: an attack is declared; Who filters the ATTACKER (Truesilver: the own hero)
        FatalDamage,  // PRE: the own hero is about to take damage that would bring it to 0 or less (Ice Block)
        SecretRevealed // a secret of Who's side triggered (Eaglehorn Bow)
    };

    // Who the trigger's subject must be (relative to the trigger owner's side).
    enum class Who : uint8_t { Self, Friendly, OtherFriendly, Enemy, Any, AnyOther, OwnHero, EnemyHero };

    enum TriggerFlags : unsigned
    {
        kTrigMinion = 1,     // the subject is a minion (card: a minion card)
        kTrigHero = 2,       // the subject is a hero
        kTrigRace = 4,       // the subject is a minion of race
        kTrigSpell = 8,      // the subject card is a spell
        kTrigOverload = 16,  // the subject card has Overload
        kTrigMaxAttack = 32, // the subject minion has attack <= value
        kTrigDefenderMinion = 64, // Attacked: the defender is a minion (Snake Trap)
        // ---- Classic engine cards ----
        kTrigSecret = 128,   // the subject card is a Secret (Secretkeeper)
        // ---- Naxxramas ----
        kTrigDeathrattle = 256 // the subject card has a printed Deathrattle (Undertaker)
    };

    struct TriggerSpec
    {
        On on = On::TurnStart;
        Who who = Who::Any;
        unsigned flags = 0;
        int value = 0;
        std::string race;
        Cond cond = Cond::None;  // checked before the trigger fires (a secret that cannot fire stays hidden)
        std::string condRace;
        std::vector<Step> steps;
    };

    // ---- auras -------------------------------------------------------------------------

    enum class AuraKind : uint8_t
    {
        Attack,       // +value attack
        Health,       // +value health
        AttackHealth, // +value/+value
        Keyword,      // keyword value (KeywordBit): Charge (Tundra Rhino), Windfury (enrage) ...
        SpellDamage,  // +value Spell Damage for the own side
        Cost,         // +value cost (negative: cheaper) of the cards in scope
        WeaponAttack, // +value attack for the own weapon (Spiteful Smith)
        HealIsDamage, // the own side's healing deals damage instead (Auchenai Soulpriest)
        DoubleSpellHeal, // the own side's spells and hero power deal double damage and healing (Prophet Velen)
        ImmuneWhileAttacking, // weapon: the own hero takes no damage while it attacks (Gladiator's Longbow)
        AttackCostsAttack,    // weapon: attacking a minion costs 1 attack instead of 1 durability (Gorehowl)
        AttackEqualsHealth,   // Self: the attack is always equal to the current health (Lightspawn)
        // ---- Classic engine cards ----
        AttackPerOtherRace,   // Self: +value attack for each other minion of race on the battlefield, both sides (Old Murk-Eye)
        // ---- boss encounters (OwnSide scope) ----
        HeroAttackOwnTurn,    // the own hero has +value Attack during the own turn only (Worshipper)
        HeroImmune,           // the own hero is Immune while the source minion is on the board and not silenced (Horsemen)
        DoubleHeroDamage,     // weapon: the hero's attack with it deals double damage to heroes (Massive Runeblade)
        WeaponAttackNoHorsemen // weapon: +value Attack while none of the three Horsemen minions is on either board (Runeblade)
    };

    enum class AuraScope : uint8_t
    {
        Self,                 // the minion itself (Enrage)
        OtherFriendlyMinions,
        FriendlyMinions,
        AllOtherMinions,      // both sides, except Self (Grimscale Oracle with race)
        Adjacent,             // the minions left and right of Self
        OwnSide,              // player level: SpellDamage, HealIsDamage, DoubleSpellHeal, WeaponAttack
        OwnMinionCards,       // cost: the own hand's minion cards
        OwnSpellCards,        // cost: the own hand's spell cards
        AllMinionCards        // cost: both hands' minion cards (unused: Venture Co. Mercenary only taxes ITS OWNER's
                              // minions, which is OwnMinionCards)
    };

    struct AuraSpec
    {
        AuraKind kind = AuraKind::Attack;
        AuraScope scope = AuraScope::Self;
        int value = 0;
        std::string race;         // scope keeps minions of this race only
        bool whileDamaged = false; // Enrage
        bool whileWeapon = false;  // only while the owner's hero has a weapon (Southsea Deckhand's Charge)
        int costFloor = 0;        // Cost: the cost never goes below this through this aura (Summoning Portal: 1)
        // ---- Classic engine cards ----
        bool firstMinionOnly = false; // only while the owner has played no minion card this turn (Pint-Sized Summoner)
        // ---- Naxxramas ----
        bool battlecryOnly = false;   // Cost: only cards whose definition has a Battlecry (Nerub'ar Weblord)
    };

    // A card's own cost from the state (the Giants, Dread Corsair).
    enum class CostRule : uint8_t
    {
        None,
        MinusPerOtherHandCard,  // Mountain Giant: -value per other card in the own hand
        MinusPerMinion,         // Sea Giant: -value per other minion on the battlefield
        MinusPerHeroDamage,     // Molten Giant: -value per damage of the own hero
        MinusWeaponAttack       // Dread Corsair: -value * the own weapon's attack
    };

    // ---- the script ---------------------------------------------------------------------

    struct Script
    {
        std::string id;
        TargetRule target;
        bool hasComboTarget = false; // Combo uses comboTarget instead of target (SI:7 Agent, Kidnapper)
        TargetRule comboTarget;
        unsigned reqs = 0;
        int reqValue = 0;
        std::vector<Step> play;      // spell effect, battlecry, weapon battlecry, hero power effect
        std::vector<Step> combo;     // replaces play when the side already played a card this turn
        std::string options[2];      // Choose One: the two option cards (each has its own Script)
        std::vector<Step> deathrattle;
        std::vector<TriggerSpec> triggers;
        std::vector<AuraSpec> auras;
        CostRule costRule = CostRule::None;
        int costValue = 0;
        // Wave 2: the minion's side triggers the Deathrattles of its minions twice (Baron Rivendare)
        bool doublesDeathrattles = false;
        // A hero power that is never used as an action; its triggers work all game (Raise Dead)
        bool passive = false;
        // A hero power the engine uses by itself at the start of its owner's turn (after the draw,
        // when it is affordable and its requirements hold). Never legal as an action; shown as spent (Polarity Shift,
        // Frost Breath, Frost Blast).
        bool autoCast = false;
        // A minion that is Frozen from the moment it enters play and does not thaw (Frozen
        // Champion); Silence ends it.
        bool permFrozen = false;
        // A second phase of the hero that owns this hero power (Kel'Thuzad). It starts the moment
        // the hero's Armor is gone (the action that broke it finishes, then the opponent's turn ends if it was
        // running) or, with the Armor still up, when the hero's own `forcedTurn`-th turn starts: the hero power becomes
        // `powerId` (fresh, usable at once) and `summons` minions of card `summonId` join its board.
        struct PhaseTwo
        {
            bool present = false;
            std::string powerId;
            std::string summonId;
            int summons = 0;
            int forcedTurn = 0;
        } phase;
        bool chooseOne() const { return !options[0].empty(); }
        // Every card id the script may create or refer to (filled by the builder); CardDb::Supported
        // requires all of them.
        std::vector<std::string> refs;
        // Passive codes are gone: Raid Leader, Warsong Commander and Healing Totem are now an
        // aura and triggers.
    };

    // The scripted card / hero power / option with this id, or nullptr.
    Script const* FindScript(std::string const& cardId);

    // ---- builder ---------------------------------------------------------------------------

    class ScriptTable;

    // Fluent builder. Every Do/DoCard/DoList/Run call appends a step to the current list: play
    // (the default), combo (after Combo()), deathrattle (after Deathrattle()) or the trigger
    // started by the latest On(). If(...) sets the condition of the NEXT step only.
    class ScriptBuilder
    {
    public:
        ScriptBuilder(Script& s) : m_s(&s) {}
        ScriptBuilder& Target(TargetKind kind, unsigned flags = 0, int value = 0, char const* race = nullptr);
        ScriptBuilder& ComboTarget(TargetKind kind, unsigned flags = 0, int value = 0, char const* race = nullptr);
        ScriptBuilder& Req(unsigned flag, int value = 0);
        ScriptBuilder& Do(Op op, Sel sel, int a = 0, int b = 0, unsigned flags = 0);
        ScriptBuilder& DoCard(Op op, Sel sel, char const* card, int a = 1, int b = 0, unsigned flags = 0);
        ScriptBuilder& DoList(Op op, Sel sel, std::vector<std::string> const& list, int a = 1, unsigned flags = 0);
        ScriptBuilder& Run(CustomFn fn, std::vector<std::string> const& refs = std::vector<std::string>());
        ScriptBuilder& Race(char const* race);            // race filter of the LAST step
        ScriptBuilder& Times(int count);                  // count of the LAST step (random picks / hits)
        ScriptBuilder& Flags(unsigned flags);             // adds step flags to the LAST step
        ScriptBuilder& If(Cond cond, char const* race = nullptr);
        ScriptBuilder& Combo();
        ScriptBuilder& Deathrattle();
        ScriptBuilder& On(Detail::On on, Who who, unsigned flags = 0, int value = 0, char const* race = nullptr);
        ScriptBuilder& When(Cond cond, char const* race = nullptr); // condition of the current trigger
        ScriptBuilder& Aura(AuraKind kind, AuraScope scope, int value, char const* race = nullptr);
        ScriptBuilder& Enrage(AuraKind kind, int value);  // an aura on Self while damaged
        ScriptBuilder& AuraFloor(int floor);               // costFloor of the LAST aura
        ScriptBuilder& WhileWeapon();                      // the LAST aura works only while the owner has a weapon
        ScriptBuilder& ChooseOne(char const* option1, char const* option2);
        ScriptBuilder& Cost(CostRule rule, int value);
        ScriptBuilder& Ref(char const* card);              // an extra referenced card id
        // ---- Classic engine cards ----
        ScriptBuilder& FirstMinionOnly();                  // the LAST aura works only before the owner played a minion this turn
        // ---- Naxxramas ----
        ScriptBuilder& BattlecryOnly();                    // the LAST (cost) aura covers cards with a Battlecry only
        // ---- Naxxramas, wave 2 ----
        ScriptBuilder& DoublesDeathrattles();              // Baron Rivendare: the owner's minion Deathrattles trigger twice
        // ---- boss encounters ----
        ScriptBuilder& Passive();                          // a hero power that cannot be used, only triggers (Noth's Raise Dead)
        ScriptBuilder& AutoCast();                         // a hero power the engine uses at the start of its owner's turn
        ScriptBuilder& PermanentlyFrozen();                // a minion that is Frozen from the start and never thaws
        ScriptBuilder& PhaseTwo(char const* powerId, char const* summonId, int summons, int forcedTurn); // Kel'Thuzad

    private:
        std::vector<Step>& Cur();
        void AddRef(std::string const& id);
        Script* m_s;
        int m_list = 0; // 0 play, 1 combo, 2 deathrattle, 3 trigger
        Cond m_nextCond = Cond::None;
        std::string m_nextCondRace;
    };

    class ScriptTable
    {
    public:
        // Starts the script of card id (replacing an earlier one).
        ScriptBuilder Add(char const* id);
        Script const* Find(std::string const& id) const;
        std::size_t Size() const { return m_scripts.size(); }
        std::vector<std::string> Ids() const;

    private:
        std::vector<std::unique_ptr<Script>> m_scripts;
        mutable std::unordered_map<std::string, Script const*> m_index;
    };

    // The table registrations (each file adds its cards; called once, in this order).
    void RegisterEngineScripts(ScriptTable& t);  // HsMatchCards.cpp: hero powers, The Coin, engine tokens
    void RegisterBasicScripts(ScriptTable& t);   // HsMatchCardsBasic.cpp: the CORE set
    void RegisterClassicScripts(ScriptTable& t); // HsMatchCardsClassic.cpp: the representative EXPERT1 list
    void RegisterClassicRestScripts(ScriptTable& t);   // HsMatchCardsClassicRest.cpp: the rest of EXPERT1
    void RegisterClassicEngineScripts(ScriptTable& t); // HsMatchCardsClassicEngine.cpp: cards that needed
                                                       // engine work (Lord Jaraxxus ...) and the reward-only promos
    void RegisterNaxxScripts(ScriptTable& t);          // HsMatchCardsNaxx.cpp: Curse of Naxxramas (build 6024)

    // ---- facade for Custom handlers ---------------------------------------------------------

    // Implemented by the engine; valid only during the Custom call. Entity ids are match-local.
    struct Effects
    {
        virtual ~Effects() {}
        // context
        virtual int Side() const = 0;          // the side the effect belongs to
        virtual EntityId Source() const = 0;   // the card / minion / weapon / secret / hero power
        virtual EntityId Target() const = 0;   // the chosen target (0 = none)
        virtual EntityId Subject() const = 0;  // a trigger's subject (0 = none)
        virtual int Amount() const = 0;        // a trigger's amount (damage taken ...)
        virtual bool FromSpell() const = 0;
        virtual int SpellPower() const = 0;    // Spell Damage of Side() (counts only for spells)
        // queries
        virtual std::vector<EntityId> Select(Sel sel) = 0;
        virtual bool IsMinion(EntityId id) const = 0;
        virtual bool IsHero(EntityId id) const = 0;
        virtual bool Alive(EntityId id) const = 0; // on the board / hero, health > 0, not destroyed
        virtual int SideOf(EntityId id) const = 0; // -1 unknown
        virtual int Attack(EntityId id) const = 0;
        virtual int Health(EntityId id) const = 0;
        virtual int MaxHealth(EntityId id) const = 0;
        virtual std::string CardOf(EntityId id) const = 0;
        virtual std::string RaceOf(EntityId id) const = 0;
        virtual EntityId HeroOf(int side) const = 0;
        virtual std::vector<EntityId> Board(int side) const = 0;
        virtual int HandCount(int side) const = 0;
        virtual int CardsPlayedThisTurn(int side) const = 0;
        virtual int Rand(int n) = 0;           // 0..n-1 from the match RNG
        // actions (each emits its events and queues its triggers)
        virtual void Run(Step const& step) = 0; // one primitive with this context
        virtual void Damage(EntityId target, int amount) = 0;
        virtual void Heal(EntityId target, int amount) = 0;
        virtual void Destroy(EntityId target) = 0;
        virtual void Buff(EntityId target, int attack, int health) = 0;
        virtual EntityId Summon(int side, std::string const& cardId, int position) = 0; // -1 rightmost; 0 = full
        virtual void Draw(int side, int n) = 0;
        virtual EntityId CopyMinion(EntityId from, EntityId into) = 0; // Faceless: into becomes a copy of from
        virtual std::string HeroPowerOf(int side) const = 0;
        virtual int Armor(int side) const = 0;
        virtual int HeroHealth(int side) const = 0;
        virtual int WeaponAttack(int side) const = 0; // 0 without a weapon
        virtual bool IsFrozen(EntityId id) const = 0;
        virtual std::vector<std::string> DeckCards(int side) const = 0; // own deck only; the other side: unknown ids "?"
        virtual bool TakeFromDeck(int side, std::string const& cardId) = 0; // moves one copy into the hand (Sense Demons)
        // Classic cards (appended)
        virtual int DamageMultiplier() const = 0; // 2 while a spell / hero power resolves under Prophet Velen, else 1
        virtual CardDef const* CardInfo(std::string const& cardId) const = 0; // the definition of a card id (nullptr: unknown)
        // ---- the rest of Classic ----
        virtual int WeaponDurability(int side) const = 0; // 0 without a weapon (Harrison Jones)
    };

    // ---- keywords ---------------------------------------------------------------------------

    struct Keywords
    {
        bool taunt = false;
        bool charge = false;
        bool divineShield = false;
        bool windfury = false;
        bool stealth = false;
        bool poisonous = false;
        bool cantAttack = false;
        int spellDamage = 0;
        int overload = 0;
    };

    // True when text consists only of supported keywords (or is empty); fills out.
    // Keywords: Taunt, Charge, Divine Shield, Windfury, Stealth, Poisonous, Spell Damage +N,
    // Overload: (N).
    bool ParseKeywordText(std::string const& text, Keywords& out);
    // Keywords of a card as played: the def flags, plus the text keywords of a keyword-only card.
    Keywords KeywordsOf(CardDef const& def);

    // The four basic totems of Totemic Call.
    std::vector<std::string> const& TotemIds();

    constexpr char const* kCoinId = "GAME_005";
    constexpr char const* kSheepId = "CS2_tk1";
    constexpr char const* kExcessManaId = "CS2_013t";
}

    // Test-only state surgery for tests (a friend of Match). Production code never calls it.
    // Each setter replaces what it names with fresh entities.
    struct TestAccess
    {
        static std::vector<EntityId> SetHand(Match& m, int side, std::vector<std::string> const& cardIds);
        // Board minions, left to right, not exhausted.
        static std::vector<EntityId> SetBoard(Match& m, int side, std::vector<std::string> const& cardIds);
        // The deck; the first entry is drawn next.
        static std::vector<EntityId> SetDeck(Match& m, int side, std::vector<std::string> const& topFirst);
        static void SetMana(Match& m, int side, int mana, int maxMana);
        static void SetHero(Match& m, int side, int health, int armor);
        static void SetFatigue(Match& m, int side, int fatigue);
        // cardId empty: no weapon.
        static EntityId SetWeapon(Match& m, int side, std::string const& cardId);
        // attack / health / maxHealth as the permanent values (no auras); damage = maxHealth - health.
        static void SetMinion(Match& m, EntityId id, int attack, int health, int maxHealth);
        static void SetTurn(Match& m, int turn);
        static void ReadyAll(Match& m);
        static void SetHeroPowerUsed(Match& m, int side, bool used);
        static std::vector<EntityId> SetSecrets(Match& m, int side, std::vector<std::string> const& cardIds);
        static void SetFrozen(Match& m, EntityId id, bool frozen);
        static void SetCardsPlayed(Match& m, int side, int n);
        static void SetOverload(Match& m, int side, int locked, int owed);

        static EntityId HeroId(Match const& m, int side);
        static EntityId PowerId(Match const& m, int side);
        static std::vector<EntityId> HandIds(Match const& m, int side);
        static std::vector<EntityId> BoardIds(Match const& m, int side);
        static std::vector<EntityId> DeckIds(Match const& m, int side);        // next draw first
        static std::vector<std::string> DeckCardIds(Match const& m, int side); // next draw first
        static std::vector<std::string> HandCardIds(Match const& m, int side);
        static std::vector<std::string> BoardCardIds(Match const& m, int side);
        static std::vector<std::string> SecretCardIds(Match const& m, int side);
        static int HandCost(Match const& m, EntityId handCard); // the cost now (auras, modifiers, cost rule)
    };
}
}

#endif
