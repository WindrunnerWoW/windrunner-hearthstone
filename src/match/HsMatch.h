/*
 * Match engine: card definitions for play, one match's mutable
 * state, the rules, legal-action enumeration, filtered views and the event
 * log.
 *
 * PURE C++ (standard library only, no core headers), so it runs headless in
 * tests/match_tests.cpp.
 *
 * Sides are 0 and 1. The match service puts the human on side 0 and the AI on
 * side 1, but the engine does not care which side is which: both sides go
 * through the same Apply() validation and resolution path.
 *
 * Determinism: every random choice (shuffle, first player, random targets,
 * random totems) comes from the match's own std::mt19937 seeded with
 * MatchSetup::seed, so the same setup and the same action sequence always give
 * the same match.
 *
 * Hidden information: ViewFor(side) is the only way to look at a match from a
 * side's point of view. It never contains the other side's hand cards, either
 * deck's order or contents, or the RNG state. The AI only ever sees a view.
 *
 * Rules: triggers, deathrattles,
 * auras, enrage, silence, freeze, stealth, secrets, combo, overload, Choose One,
 * pending picks (Tracking), cost modifiers, boss setup overrides and the
 * redacted copy the AI simulates on.
 *
 * Not thread safe: the owner (the match service) serializes access.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_H
#define MOD_HEARTHSTONE_HS_MATCH_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hs
{
namespace Match
{
    // The version of the rules the engine plays. A match is reproducible from its seed, the decks,
    // the dataset and the actions only for the same rules version: it is stored with every match history row
    // (hearthstone_match_history.rules_version). Raise it whenever a change can alter what a match with the
    // same seed, decks and actions does: a rule, a card script, the AI's decisions, or the order or amount of
    // the random numbers drawn. Changes that only add events or views of the same game do not need it.
    // 10 = the first versioned engine (the sum / die event positions).
    // 11 = the card data of build 6024 (Naxxramas launch): Ancestral Spirit grants a real Deathrattle ("Resummon this
    //      minion") and Eaglehorn Bow reacts to friendly Secrets only. Both were already scripted that way, but the
    //      rules are now defined by the 6024 text, and matches stored under version 10 used the 5834 dataset.
    //      Wave 1 (23 Naxxramas cards, new ops, weapon Deathrattles, spell tax) did not raise it: the new
    //      engine paths are reachable only through cards that no earlier deck could hold, and nothing else changed.
    //      Wave 2 (Avenge, Duplicate, Echoing Ooze, Rivendare, Stalagg, Feugen, Kel'Thuzad) likewise.
    //      The boss encounter steps (E1, E2a, E2b: start states, passive / automatic hero powers, Kel'Thuzad's second phase,
    //      the encounter-only cards) likewise: nothing an ordinary deck can reach behaves differently.
    constexpr int kRulesVersion = 11;

    constexpr int kMaxHand = 10;
    constexpr int kMaxBoard = 7;
    constexpr int kMaxMana = 10;
    constexpr int kHeroHealth = 30;
    constexpr int kStartHandFirst = 3;
    constexpr int kStartHandSecond = 4;
    // A match still running after this many turns (both sides counted) ends in a draw.
    constexpr int kMaxTurns = 90;
    // Deck sizes Create() accepts (real matches use 30; tests may use fewer).
    constexpr std::size_t kMinDeckCards = 1;
    constexpr std::size_t kMaxDeckCards = 60;

    using EntityId = uint32_t; // match-local, 1.., never reused within a match; 0 = none

    // ---- definitions ----------------------------------------------------

    enum class CardType : uint8_t { Minion, Spell, Weapon, HeroPower, Hero, Other };

    // One card as the engine plays it. Filled from the world table
    // hearthstone_card_template (glue) or by hand (tests).
    struct CardDef
    {
        std::string id;
        std::string name;
        std::string cardClass; // MAGE ... WARRIOR, NEUTRAL, ...
        std::string rarity;    // FREE, COMMON, RARE, EPIC, LEGENDARY or empty
        std::string race;      // MURLOC, BEAST, TOTEM, ... or empty
        std::string text;      // raw card text (used to decide keyword-only minions)
        CardType type = CardType::Other;
        int cost = 0;
        int attack = 0;        // minions and weapons
        int health = 0;        // minions and heroes
        int durability = 0;    // weapons
        int spellDamage = 0;   // spell_damage column (Wrath of Air Totem: 1)
        int overload = 0;      // overload column
        bool taunt = false;
        bool charge = false;
        bool divineShield = false;
        bool windfury = false;
        // Mechanics flags (from the mechanics column)
        bool stealth = false;    // STEALTH
        bool poisonous = false;  // POISONOUS
        bool secret = false;     // SECRET
        bool cantAttack = false; // CANT_ATTACK
        bool elusive = false;    // CANT_BE_TARGETED_BY_SPELLS / CANT_BE_TARGETED_BY_HERO_POWERS
        // Naxxramas: the BATTLECRY / DEATHRATTLE mechanics of the card as printed (Nerub'ar Weblord taxes
        // Battlecry minions, Undertaker reacts to Deathrattle minions). Granted Deathrattles do not count.
        bool battlecry = false;
        bool deathrattle = false;
    };

    // card_type column value -> CardType (unknown: Other).
    CardType ParseCardType(std::string const& cardType);
    // Sets the keyword flags from the mechanics column ("TAUNT,CHARGE", "DIVINE_SHIELD", "STEALTH",
    // "POISONOUS", "SECRET", "CANT_ATTACK", "CANT_BE_TARGETED_BY_SPELLS", ...).
    // Unknown mechanics are ignored here (Supported() looks at the text).
    void ApplyMechanics(std::string const& mechanicsCsv, CardDef& def);

    class CardDb
    {
    public:
        // Adds or replaces a definition (keyed by id).
        void Add(CardDef const& def);
        CardDef const* Find(std::string const& id) const;
        std::size_t Size() const { return m_cards.size(); }

        // True if the engine implements the card completely: a scripted card
        // (Detail::FindScript) whose referenced cards (tokens, options, generated
        // cards) are all in this CardDb, a minion whose text consists of
        // supported keywords only (Taunt, Charge, Divine Shield, Windfury,
        // Stealth, Poisonous, Spell Damage +N, Overload: (N), or no text), or a
        // weapon without text. Unknown ids are unsupported.
        bool Supported(std::string const& id) const;
        // How many of the definitions Supported() accepts.
        std::size_t SupportedCount() const;

    private:
        std::unordered_map<std::string, CardDef> m_cards;
    };

    // The classic hero card / hero power of a deck class ("" for an unknown class):
    // WARRIOR HERO_01/CS2_102, SHAMAN HERO_02/CS2_049, ROGUE HERO_03/CS2_083b,
    // PALADIN HERO_04/CS2_101, HUNTER HERO_05/DS1h_292, DRUID HERO_06/CS2_017,
    // WARLOCK HERO_07/CS2_056, MAGE HERO_08/CS2_034, PRIEST HERO_09/CS1h_001.
    std::string HeroCardOf(std::string const& heroClass);
    std::string HeroPowerOf(std::string const& heroClass);
    // Every card id the engine may create on its own (The Coin, Sheep, tokens of
    // hero powers, the heroes and hero powers). Create() fails if one of those it
    // needs is missing from the CardDb.
    std::vector<std::string> EngineCardIds();

    // ---- setup and results ----------------------------------------------

    // What a side starts with beyond its deck (boss encounters, src/match/HsEncounters.h). Everything at its
    // default = the normal rules, so an ordinary match is the same match as before. Every card must be known and
    // Supported(); nothing here uses the match RNG, so the same seed still gives the same match.
    struct SideStart
    {
        // Minion cards put onto the board before the mulligan, left to right, at most kMaxBoard. They are summoned
        // without Battlecry and without any trigger of the summon (the opening board is part of the setup, not a
        // play), are not exhausted when their side's first turn starts and cost nothing.
        std::vector<std::string> board;
        // A weapon the hero starts with (Patchwerk's Hook); replaces nothing, there is none yet.
        std::string weapon;
        // Cards put into the hand IN ADDITION to the opening hand (3 or 4 cards) and The Coin: the opening hand
        // is dealt as always, these cards come after it, are not part of the deck and are not offered in the
        // mulligan (they cannot be shuffled away). The hand limit applies (kMaxHand).
        std::vector<std::string> hand;
        // The side skips its normal draw at the start of its turn (Patchwerk: no deck, so no turn draw). Any other
        // draw, forced by a card effect, works as usual, so from an empty deck it deals fatigue like any side's
        // (wiki Patchwerk page: "still takes fatigue damage if forced to draw").
        bool noDraw = false;
    };

    struct SideSetup
    {
        std::string name;              // shown in the match window (character or NPC name)
        std::string heroClass;         // MAGE ... (decides hero and hero power)
        std::vector<std::string> deck; // card ids, one entry per copy; shuffled with the seed
        // Encounter overrides (bosses; empty / 0 = the normal rules). They change
        // this match only, never a shared card definition.
        int heroHealth = 0;            // starting and maximum hero health (0: kHeroHealth); 1..200
        int heroArmor = 0;             // starting armor; 0..200
        std::string heroCardId;        // hero card shown (portrait); must be a known card
        std::string heroPowerId;       // hero power; must be a scripted HeroPower card
        // Starting board / weapon / extra hand cards / no-draw rule of an encounter. An empty deck is
        // accepted only together with start.noDraw.
        SideStart start;
    };

    struct MatchSetup
    {
        uint32_t seed = 0;
        SideSetup sides[2];
        // 0 or 1; -1: chosen by the seeded RNG. A forced side still consumes the coin toss, so the rest of the
        // random sequence does not depend on whether the first side was forced.
        int firstSide = -1;
    };

    enum class Phase : uint8_t { Mulligan, Play, Over };

    enum class EndReason : uint8_t
    {
        None,
        HeroDied,  // one or both heroes reached 0 health (both: draw)
        Concede,   // a side conceded (the other side wins)
        TurnLimit, // kMaxTurns reached: draw
        Aborted    // ended from outside (Abort()); no winner
    };

    struct Result
    {
        bool over = false;
        int winner = -1;   // 0 or 1; -1: draw or no winner (Aborted)
        EndReason reason = EndReason::None;
    };

    // ---- actions --------------------------------------------------------

    // Choose: answers a pending pick (MatchView::pick, for example Tracking); source = the picked
    // option's id. While a pick is pending only Choose and Concede are accepted from that side.
    enum class ActionType : uint8_t { Mulligan, Play, Attack, HeroPower, EndTurn, Concede, Choose };

    struct Action
    {
        ActionType type = ActionType::EndTurn;
        EntityId source = 0;           // Play: the hand card; Attack: the attacking minion or hero; HeroPower: the hero power; Choose: the picked option
        EntityId target = 0;           // 0 = no target
        int position = -1;             // Play of a minion: board slot 0..board size; -1 = rightmost
        int choice = 0;                // Play of a Choose One card: 1 or 2 (the option); 0 for every other card
        std::vector<EntityId> replace; // Mulligan: the offered cards to shuffle back (may be empty)
    };

    enum class ActionError : uint8_t
    {
        None,
        Over,            // the match is over
        Phase,           // not allowed in this phase (for example Play during the mulligan)
        Turn,            // not this side's turn
        Source,          // the source entity is not this side's hand card / attacker / hero power
        Mana,            // not enough mana
        Target,          // a target is required, not allowed, or not a valid target
        BoardFull,       // no free board slot
        Attack,          // the attacker cannot attack now (exhausted, 0 attack, already attacked)
        Taunt,           // the defender has a Taunt minion and the target is not one of them
        Used,            // the hero power was already used this turn
        Requirement,     // a play requirement fails (Cleave needs two enemy minions, totems, ...)
        Mulligan,        // bad mulligan (already chosen, or an id that was not offered)
        Position,        // board position out of range
        Unsupported,     // the card has no implementation (cannot happen for a deck Create() accepted)
        Choice,          // Choose One: choice missing (0) for a Choose One card, out of range, or given for another card
        Pending,         // a pick is pending: only Choose (or Concede) now; or Choose without a pending pick
        Secret           // that secret is already active on this side
    };
    // Wire code for ERR REFUSED why= (over, phase, turn, source, mana, target, board, attack, taunt,
    // used, requirement, mulligan, position, unsupported, choice, pending, secret; "" for None).
    char const* ActionErrorCode(ActionError e);

    // ---- events ---------------------------------------------------------

    enum class EventType : uint8_t
    {
        Mulligan,    // side replaced amount cards
        TurnStart,   // side's turn starts; amount = turn number
        Draw,        // side drew target (cardId hidden from the other side)
        Burn,        // side drew target with a full hand; it is destroyed (cardId revealed)
        Fatigue,     // side's hero takes amount fatigue damage (target = hero)
        Play,        // side played source (cardId revealed), on target (0 = none)
        Summon,      // side got minion target on the board; amount = its 1-based board position (0 = unknown)
        Attack,      // source attacks target
        Damage,      // target took amount damage from source (0 = none / fatigue)
        Heal,        // target restored amount health
        Armor,       // side's hero gained amount armor
        ShieldPop,   // target lost Divine Shield instead of taking damage
        Death,       // minion target died or left play; amount = the 1-based position it had on its board before the death step removed anything (0 = unknown)
        Equip,       // side's hero equipped weapon target
        WeaponBreak, // weapon target was destroyed
        Transform,   // minion target became cardId
        Buff,        // target changed attack/health/keywords (amount = attack change)
        HeroPower,   // side used hero power source on target (0 = none)
        ManaGain,    // side gained amount mana this turn (The Coin)
        GameOver,    // the match ended (see Result)
        // ---- appended later; codes in brackets ----
        Silence,     // [sil] minion target was silenced
        Freeze,      // [frz] character target was frozen
        Secret,      // [sec] side put secret target into play (cardId hidden from the other side)
        Reveal,      // [rev] secret target of side triggered (cardId revealed); source = what triggered it
        Trigger,     // [trig] an ability of source (minion, weapon, hero power) triggered; cardId = source's card
        Deathrattle, // [dr] the deathrattle of the dead minion source resolves; cardId = its card
        Discard,     // [disc] side discarded card target (cardId revealed)
        Bounce,      // [bnc] minion target returned to side's hand (cardId revealed)
        Control,     // [ctl] minion target changed controller to side
        Overload,    // [ovl] side overloaded amount crystals (locked next turn)
        Generate,    // [gen] card target was put into side's hand by an effect (cardId hidden from the other side)
        Destroy,     // [dest] target was destroyed by an effect (it dies in the next death step)
        Counter,     // [cnt] the spell source of side was countered
        Pick,        // [pick] side picked card target of a pending pick (cardId hidden from the other side)
        Crystal,     // [crys] side's mana crystals changed by amount (+ gained, - destroyed)
        Stealth      // [stl] target lost Stealth
    };
    // Wire code (mull, turn, draw, burn, fat, play, sum, atk, dmg, heal, arm, pop, die, eq, wbrk, tf, buff, hp, mana, end,
    // sil, frz, sec, rev, trig, dr, disc, bnc, ctl, ovl, gen, dest, cnt, pick, crys, stl).
    char const* EventTypeCode(EventType t);

    struct Event
    {
        uint32_t seq = 0;  // 1.., increasing across the match
        EventType type = EventType::GameOver;
        int side = -1;     // the side the subject belongs to (-1: none)
        EntityId source = 0;
        EntityId target = 0;
        int amount = 0;
        std::string cardId; // empty when hidden from the viewer or not applicable
        bool hideFromOther = false; // ViewFor clears cardId for the other side
    };

    // ---- views ----------------------------------------------------------

    struct MinionView
    {
        EntityId id = 0;
        std::string cardId;
        int attack = 0;    // current (buffs and auras included)
        int health = 0;    // current
        int maxHealth = 0; // current maximum
        bool taunt = false, charge = false, divineShield = false, windfury = false;
        int spellDamage = 0;
        bool canAttack = false; // could attack right now (only ever true for the active side)
        bool stealth = false;
        bool frozen = false;
        bool silenced = false;
        bool poisonous = false;
        bool enraged = false;    // damaged with an active Enrage ability
        bool immune = false;
        bool cantAttack = false; // a Can't Attack minion (Ancient Watcher, Ragnaros) that is not silenced
        // Wave 2: an Echoing Ooze that will copy itself at the end of the turn (public: the real game shows it)
        bool echoing = false;
    };

    struct HandCardView
    {
        EntityId id = 0;
        std::string cardId;
        int cost = 0;
        int attack = 0; // minion attack / weapon attack; 0 for spells
        int health = 0; // minion health / weapon durability; 0 for spells
        // Choose One cards: the two option card ids (option 1, option 2); empty otherwise
        std::vector<std::string> options;
    };

    // A secret in play. cardId is empty in the view of the other side.
    struct SecretView
    {
        EntityId id = 0;
        std::string cardId;
    };

    // An option of a pending pick (Tracking: one of the top three cards).
    struct PickView
    {
        EntityId id = 0;
        std::string cardId;
    };

    struct HeroView
    {
        EntityId id = 0;
        std::string cardId;   // HERO_08 ...
        int health = kHeroHealth;
        int maxHealth = kHeroHealth;
        int armor = 0;
        int attack = 0;       // weapon (on its own turn) plus temporary attack
        bool canAttack = false;
        EntityId powerId = 0;
        std::string powerCardId;
        int powerCost = 2;
        bool powerUsed = false;
        EntityId weaponId = 0; // 0: no weapon
        std::string weaponCardId;
        int weaponAttack = 0;
        int weaponDurability = 0;
        int mana = 0;     // available now
        int maxMana = 0;  // mana crystals
        int fatigue = 0;  // damage of the next fatigue draw minus one (0 = none taken yet)
        bool frozen = false;
        bool immune = false;
        bool weaponWindfury = false; // the weapon lets the hero attack twice (Doomhammer)
    };

    struct SideView
    {
        std::string name;
        std::string heroClass;
        HeroView hero;
        std::vector<MinionView> board;   // left to right
        std::vector<HandCardView> hand;  // only for the viewer's own side; empty for the other side
        int handCount = 0;
        int deckCount = 0;
        std::vector<SecretView> secrets; // in play order; cardId only in the own side's view
        int cardsPlayed = 0;     // cards played this turn (Combo is active when > 0 on its own turn)
        int overloadLocked = 0;  // crystals locked this turn by last turn's Overload
        int overloadOwed = 0;    // crystals that will be locked next turn
        // Loatheb ("Enemy spells cost (5) more next turn"). spellTax: how much more this side's spells cost
        // during its current turn (0 outside its own turn); spellTaxNext: during its next turn. Both are already part
        // of the hand card costs the owner sees; the AI reads spellTaxNext of the other side.
        int spellTax = 0;
        int spellTaxNext = 0;
        // The minions of this side that died during the current turn, as fresh bodies (card stats, in order of death;
        // public information). The AI reads it for Kel'Thuzad, who brings them back at the end of the turn.
        std::vector<MinionView> diedThisTurn;
    };

    struct MatchView
    {
        int viewer = 0;
        uint32_t seq = 0;    // Match::Seq()
        Phase phase = Phase::Mulligan;
        int turn = 0;        // 1.. (0 during the mulligan)
        int activeSide = 0;
        Result result;
        SideView self;       // the viewer's side
        SideView other;
        bool mulliganPending = false;   // the viewer still has to choose its mulligan
        std::vector<EntityId> mulligan; // the viewer's offered cards while mulliganPending
        // The viewer must answer a pick (Choose action) before anything else; pick = its options.
        // The other side only sees that a pick is pending (pickPending of its own view is false;
        // opponentPicking is true), never the options.
        bool pickPending = false;
        std::vector<PickView> pick;
        bool opponentPicking = false;
        // The viewer's legal Play / Attack / HeroPower actions now, one entry per
        // (source, choice, target) (target 0 = no target; position -1; choice 1/2 only
        // for Choose One cards). Empty when it is not the viewer's turn or a pick is pending. EndTurn and Concede are always allowed for the
        // active side and are not listed; Concede is allowed for both sides until
        // the match is over.
        std::vector<Action> legal;
        // Events with seq > the sinceEventSeq of ViewFor(), filtered: a Draw,
        // Secret, Generate or Pick of the other side carries no cardId, and neither
        // does the Play event of the other side's secret.
        std::vector<Event> events;
        // Match::TurnTimeLimitSec() when the view was built (0 = no turn timer).
        int turnTimeLimitSec = 0;
    };

    // ---- the match ------------------------------------------------------

    struct TestAccess; // tests/match_tests.cpp state surgery (declared in HsMatchInternal.h)

    struct SideTally
    {
        uint32_t turns = 0;
        uint32_t heroPowers = 0;
        uint32_t cardsDrawn = 0;
        uint32_t minionsPlayed = 0;
        uint32_t spellsPlayed = 0;
        uint32_t minionsDestroyed = 0;
        uint32_t heroDamage = 0;
        uint32_t beastsPlayed = 0;
        uint32_t expensivePlayed = 0;
        bool operator==(SideTally const& other) const
        {
            return turns == other.turns && heroPowers == other.heroPowers && cardsDrawn == other.cardsDrawn &&
                minionsPlayed == other.minionsPlayed && spellsPlayed == other.spellsPlayed &&
                minionsDestroyed == other.minionsDestroyed && heroDamage == other.heroDamage &&
                beastsPlayed == other.beastsPlayed && expensivePlayed == other.expensivePlayed;
        }
    };

    class Match
    {
    public:
        // Builds a match: validates the setup (known class, every card known and
        // Supported(), deck sizes within kMinDeckCards..kMaxDeckCards, the engine
        // cards it needs present), shuffles, chooses the first side and deals the
        // opening hands (3 for the first side, 4 for the second). The match starts
        // in Phase::Mulligan. Returns nullptr and sets error on failure.
        // A side's SideStart (starting board, weapon, extra hand cards, noDraw) is applied last, after the
        // opening hands, and is validated like a deck (known, Supported, right card type).
        // The CardDb must outlive the match.
        static std::unique_ptr<Match> Create(CardDb const& db, MatchSetup const& setup, std::string& error);

        ~Match();

        // Validates and resolves one action of side. Nothing changes on an error.
        // Mulligan: both sides choose once; when both have chosen, the second side
        // gets The Coin and the first side's turn 1 starts (with its draw).
        // EndTurn: end-of-turn triggers, then the other side's turn starts (mana,
        // draw or fatigue, exhaustion cleared, hero power refreshed).
        // Every successful Apply increments Seq().
        ActionError Apply(int side, Action const& action);

        // The legal Play / Attack / HeroPower actions of side right now (see MatchView::legal).
        std::vector<Action> LegalActions(int side) const;

        // side's view with the events after sinceEventSeq.
        MatchView ViewFor(int side, uint32_t sinceEventSeq) const;

        // Ends a running match from outside (no winner, EndReason::Aborted,
        // a GameOver event). No effect on a match that is already over.
        void Abort();

        Phase GetPhase() const;
        int ActiveSide() const;
        int Turn() const;
        int OwnTurns(int side) const;
        SideTally Tally(int side) const;
        uint32_t Seq() const;
        uint32_t LastEventSeq() const;
        Result GetResult() const;
        uint32_t Seed() const;
        bool MulliganPending(int side) const;
        int HeroHealth(int side) const;
        // Nozdormu (EX1_560), "Players only have 15 seconds to take their turns". 15 while at
        // least one Nozdormu that is not silenced is on either board (match running), else 0 (no limit).
        // The service enforces it (HsMatchService.h).
        int TurnTimeLimitSec() const;

        // Full state as text (both hands, deck order included) for determinism
        // tests and GM diagnostics. Never sent to a client.
        std::string DebugState() const;

        // A copy for side's AI to simulate on: everything side
        // may know, nothing else. The other side's hand cards, the other side's
        // deck and secrets become unknown placeholder cards (same counts; they
        // can never be played and never trigger), side's own deck keeps its
        // contents but is reshuffled, a pending pick of the other side is
        // dropped, and the RNG is reseeded with seed (future random outcomes are
        // not the real ones). The copy has no events. Simulating on it uses the
        // normal Apply / LegalActions / ViewFor.
        std::unique_ptr<Match> RedactedCopy(int side, uint32_t seed) const;

    private:
        friend struct TestAccess;
        Match();
        struct State;
        std::unique_ptr<State> m_state;
    };
}
}

#endif
