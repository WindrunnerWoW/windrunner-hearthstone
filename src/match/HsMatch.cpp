#include "HsMatch.h"
#include "HsMatchInternal.h"

#include <algorithm>
#include <cstddef>
#include <random>
#include <sstream>

namespace Hs
{
namespace Match
{
    using namespace Detail;

    namespace
    {
        // The card every hidden card becomes in a redacted copy: never playable, never triggers.
        CardDef const& UnknownDef()
        {
            static CardDef const def = []() {
                CardDef d;
                d.id = "?";
                d.name = "Unknown";
                d.type = CardType::Other;
                d.cost = 99;
                return d;
            }();
            return def;
        }

        struct Card
        {
            EntityId id = 0;
            CardDef const* def = nullptr;
            int costMod = 0; // permanent cost change of this copy (Freezing Trap: +2)
        };

        // An enchantment script attached to a minion (Soul of the Forest, Ancestral Spirit, Blessing of Wisdom):
        // its Deathrattle / triggers work for side, the player who granted it.
        struct Granted
        {
            Script const* sc = nullptr;
            int side = 0;
        };

        struct Minion
        {
            EntityId id = 0;
            CardDef const* def = nullptr;
            Script const* sc = nullptr; // nullptr: no script (keyword-only card)
            int attack = 0;    // permanent attack (printed plus buffs; auras and temporary attack are added on demand)
            int maxHealth = 0; // permanent maximum health (printed plus buffs)
            int damage = 0;
            int auraHealth = 0; // aura health applied at the last RefreshAuras
            unsigned kw = 0;     // permanent keywords (KeywordBit)
            unsigned tempKw = 0; // until the end of the turn
            unsigned nextKw = 0; // until the start of nextKwSide's next turn (Conceal)
            int nextKwSide = -1;
            int tempAttack = 0;
            int spellDamage = 0;
            bool frozen = false;
            bool freezeKeep = false; // frozen after it attacked this turn: stays frozen through its next turn
            bool silenced = false;
            bool destroyed = false;
            int destroyAtStartOf = -1; // Corruption
            bool dieAtEndOfTurn = false; // Power Overwhelming
            bool dieSkipOnce = false;    // an Echoing Ooze copy of such a minion: spared this end of turn, dies at the next one
            bool sick = true;
            int attacks = 0;
            uint32_t order = 0; // play order
            std::vector<Granted> grants; // granted enchantments, in the order they were granted
            int returnSide = -1;         // Shadow Madness: goes back to this side at the end of the turn
            bool echo = false;           // wave 2: Echoing Ooze will copy itself at the end of the turn
        };

        struct Weapon
        {
            EntityId id = 0; // 0: no weapon
            CardDef const* def = nullptr;
            Script const* sc = nullptr;
            int attack = 0;
            int durability = 0;
            bool windfury = false;
            uint32_t order = 0;
        };

        struct SecretCard
        {
            EntityId id = 0;
            CardDef const* def = nullptr;
            Script const* sc = nullptr;
            uint32_t order = 0;
        };

        struct SideState
        {
            std::string name;
            std::string heroClass;
            EntityId heroId = 0;
            std::string heroCardId;
            int health = kHeroHealth;
            int maxHealth = kHeroHealth;
            int armor = 0;
            int tempAttack = 0;
            int heroAttacks = 0;
            bool heroFrozen = false;
            bool heroFreezeKeep = false;
            bool heroImmune = false; // until the end of the turn
            EntityId powerId = 0;
            CardDef const* power = nullptr;
            Script const* powerPassive = nullptr; // the script of a passive hero power with triggers (Raise Dead)
            bool powerUsed = false;
            Weapon weapon;
            std::vector<Card> deck; // back = top
            std::vector<Card> hand;
            std::vector<Minion> board;
            std::vector<SecretCard> secrets;
            std::vector<Card> pick; // pending pick options
            int mana = 0;
            int maxMana = 0;
            int fatigue = 0;
            bool mulliganDone = false;
            std::vector<EntityId> offered;
            int cardsPlayed = 0;
            int overloadLocked = 0;
            int overloadOwed = 0;
            int spellDiscount = 0;          // Preparation, this turn
            bool freeSecret = false;        // Kirin Tor Mage, this turn
            bool spellsFree = false;        // Millhouse Manastorm (enemy), during this side's current turn
            bool spellsFreeNext = false;    // ... during this side's next turn
            // Part E
            bool minionFloor = false;       // Commanding Shout: own minions cannot go below 1 Health this turn
            int minionsPlayed = 0;          // minion cards played from the hand this turn (Pint-Sized Summoner)
            std::vector<CardDef const*> nextTurnCards; // put into the hand at the start of the next own turn (Headcrack)
            // Naxxramas: Loatheb. The tax of this side's spells during its current / next turn; the Seq
            // values order it against Millhouse Manastorm's "cost (0)" (effects apply in the order they were made).
            int spellTax = 0;
            int spellTaxNext = 0;
            uint32_t spellTaxSeq = 0;
            uint32_t spellTaxNextSeq = 0;
            uint32_t spellsFreeSeq = 0;
            uint32_t spellsFreeNextSeq = 0;
            // Wave 2: deaths of this side's minions. diedThisTurn: the cards of the minions that died during the
            // current turn, in order of death (Kel'Thuzad); cleared when a turn starts. diedMask: kDied* bits of the
            // Stalagg / Feugen cards of this side that died at any time this game.
            std::vector<CardDef const*> diedThisTurn;
            unsigned diedMask = 0;
            // Encounters (SideStart::noDraw): no turn-start draw; effect draws work as usual (fatigue included).
            bool noDraw = false;
            // Encounters, wings 4-5: the hero's own turns started so far, and the second phase of a hero power
            // with Script::phase (Kelthuzad): done once it has happened, pending while the Armor just broke.
            int turnsStarted = 0;
            SideTally tally;
            bool phaseDone = false;
            bool phasePending = false;
        };

        constexpr unsigned kDiedStalagg = 1;
        constexpr unsigned kDiedFeugen = 2;

        // A character: a hero (idx -1) or a board minion.
        struct CharRef
        {
            int side = -1;
            int idx = -1;
            bool Valid() const { return side >= 0; }
            bool IsMinion() const { return side >= 0 && idx >= 0; }
            bool IsHero() const { return side >= 0 && idx < 0; }
        };

        // The context of a resolution (an action's main effect or a triggered ability).
        struct Ctx
        {
            int side = 0;
            EntityId source = 0;
            EntityId target = 0;
            EntityId subject = 0;
            int amount = 0;
            bool spell = false;      // a spell resolves (Spell Damage counts)
            bool power = false;      // a hero power resolves
            int spellPower = 0;
            EntityId lastSummoned = 0;
            int summonSide = -1;     // where Summon puts minions: side and slot (-1 rightmost)
            int summonPos = -1;
            // Part E
            int removed = 0;                      // keywords removed by RemoveKeyword so far (Blood Knight)
            int subjectSide = -1;                 // the side of a trigger's subject
            CardDef const* subjectCard = nullptr; // the card of a trigger's subject (a spell being cast)
            // A Deathrattle's neighbours at the time of death (Sel::AdjacentAtDeath)
            EntityId adjLeft = 0;
            EntityId adjRight = 0;
        };

        // A queued triggered ability or deathrattle.
        struct Pending
        {
            uint32_t order = 0;
            uint32_t serial = 0;
            std::vector<Step> const* steps = nullptr;
            Cond cond = Cond::None;
            std::string condRace;
            Ctx ctx;
            int kind = 0; // 0 trigger, 1 deathrattle, 2 secret
            TriggerSpec const* spec = nullptr; // the trigger it came from (PRE attack triggers re-check it)
            EntityId owner = 0;
            std::string ownerCard;
            bool granted = false;              // a granted trigger (works on a silenced minion too)
            std::vector<std::vector<Step> const*> more; // further deathrattles (granted ones) run after steps
            int times = 1;                     // Baron Rivendare: every deathrattle list runs this often in a row
        };

        // Result of drawing one card: kind 0 fatigue, 1 into the hand, 2 burned (hand full).
        struct DrawResult
        {
            int kind = 0;
            Card card;
        };

        std::string Upper(std::string s)
        {
            for (char& c : s)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return s;
        }

        int Sz(std::size_t n)
        {
            return static_cast<int>(n);
        }

        // The event log. Copying a State (only RedactedCopy does) starts an empty log: the copy keeps
        // eventSeq and eventBase, and an AI simulating thousands of copies never pays for the history.
        struct EventLog : std::vector<Event>
        {
            EventLog() = default;
            EventLog(EventLog const&) : std::vector<Event>() {}
            EventLog& operator=(EventLog const&)
            {
                clear();
                return *this;
            }
        };

        bool Has(unsigned set, int bit)
        {
            return (set & static_cast<unsigned>(bit)) != 0;
        }
    }

    // ---- the state and the rules -------------------------------------------

    struct Match::State
    {
        CardDb const* db = nullptr;
        std::mt19937 rng;
        uint32_t seed = 0;
        Phase phase = Phase::Mulligan;
        Result result;
        int turn = 0;
        int active = 0;
        int first = 0;
        uint32_t seq = 0;
        uint32_t eventSeq = 0;
        uint32_t eventBase = 0; // events dropped before events[0] (redacted copies)
        EntityId nextId = 1;
        uint32_t playOrder = 0;
        uint32_t effectSeq = 0; // orders cost effects that last into a later turn (Millhouse Manastorm, Loatheb)
        uint32_t serial = 0;
        int resolutions = 0;
        bool capped = false;
        SideState sides[2];
        EventLog events;
        std::vector<Pending> queue;
        std::vector<Minion> graveyard; // minions that died during the current action (Resummon, Subject lookups)
        // The attack / spell being resolved right now (pre triggers may change them).
        bool attackActive = false;
        EntityId attackAttacker = 0;
        EntityId attackDefender = 0;
        bool spellActive = false;
        bool spellCountered = false;
        EntityId spellTarget = 0;
        bool preventDamage = false;
        // The second phase of a boss started during the opponent's turn and that turn has to end as
        // soon as nothing is pending any more (see CheckPhases).
        bool turnEndPending = false;

        // ---- small helpers ---------------------------------------------------

        EntityId NewId() { return nextId++; }

        int Rand(int n) { return n <= 1 ? 0 : static_cast<int>(rng() % static_cast<uint32_t>(n)); }

        template <typename T>
        void Shuffle(std::vector<T>& v)
        {
            for (std::size_t i = v.size(); i > 1; --i)
            {
                std::size_t const j = static_cast<std::size_t>(Rand(static_cast<int>(i)));
                std::swap(v[i - 1], v[j]);
            }
        }

        Event& Emit(EventType type, int side, EntityId source, EntityId target, int amount, std::string const& cardId = std::string())
        {
            if (side >= 0 && side < 2)
            {
                if (type == EventType::Draw && phase == Phase::Play && cardId != kCoinId)
                    ++sides[side].tally.cardsDrawn;
                if (type == EventType::HeroPower)
                    ++sides[side].tally.heroPowers;
            }
            Event e;
            e.seq = ++eventSeq;
            e.type = type;
            e.side = side;
            e.source = source;
            e.target = target;
            e.amount = amount;
            e.cardId = cardId;
            events.push_back(e);
            return events.back();
        }

        CharRef FindChar(EntityId id) const
        {
            CharRef r;
            if (id == 0)
                return r;
            for (int s = 0; s < 2; ++s)
            {
                if (sides[s].heroId == id)
                {
                    r.side = s;
                    r.idx = -1;
                    return r;
                }
                for (int i = 0; i < Sz(sides[s].board.size()); ++i)
                    if (sides[s].board[static_cast<std::size_t>(i)].id == id)
                    {
                        r.side = s;
                        r.idx = i;
                        return r;
                    }
            }
            return r;
        }

        CharRef Hero(int side) const
        {
            CharRef r;
            r.side = side;
            r.idx = -1;
            return r;
        }

        Minion& MinionAt(CharRef r) { return sides[r.side].board[static_cast<std::size_t>(r.idx)]; }
        Minion const& MinionAt(CharRef r) const { return sides[r.side].board[static_cast<std::size_t>(r.idx)]; }

        EntityId CharId(CharRef r) const { return r.idx < 0 ? sides[r.side].heroId : MinionAt(r).id; }

        Minion const* FindAnyMinion(EntityId id) const
        {
            CharRef r = FindChar(id);
            if (r.IsMinion())
                return &MinionAt(r);
            for (std::size_t i = graveyard.size(); i > 0; --i)
                if (graveyard[i - 1].id == id)
                    return &graveyard[i - 1];
            return nullptr;
        }

        // ---- auras and derived values ------------------------------------------

        // srcSide: the side of the minion carrying the aura (whileWeapon looks at its hero).
        bool AuraActive(int srcSide, Minion const& src, AuraSpec const& a) const
        {
            if (src.silenced || !src.sc)
                return false;
            if (a.whileDamaged && src.damage <= 0)
                return false;
            if (a.whileWeapon && sides[srcSide].weapon.id == 0)
                return false;
            if (a.firstMinionOnly && sides[srcSide].minionsPlayed > 0)
                return false;
            return true;
        }

        // Does aura a of the minion at (ss, si) cover the minion at (ts, ti)?
        bool AuraCovers(int ss, int si, AuraSpec const& a, int ts, int ti) const
        {
            Minion const& t = sides[ts].board[static_cast<std::size_t>(ti)];
            if (!a.race.empty() && t.def->race != a.race)
                return false;
            switch (a.scope)
            {
                case AuraScope::Self: return ss == ts && si == ti;
                case AuraScope::OtherFriendlyMinions: return ss == ts && si != ti;
                case AuraScope::FriendlyMinions: return ss == ts;
                case AuraScope::AllOtherMinions: return !(ss == ts && si == ti);
                case AuraScope::Adjacent: return ss == ts && (si - ti == 1 || ti - si == 1);
                default: return false;
            }
        }

        // Sum of the minion auras of kind (Attack, Health, AttackHealth count for both) for the minion at (ts, ti).
        int AuraSum(int ts, int ti, bool health) const
        {
            int sum = 0;
            for (int s = 0; s < 2; ++s)
                for (int i = 0; i < Sz(sides[s].board.size()); ++i)
                {
                    Minion const& src = sides[s].board[static_cast<std::size_t>(i)];
                    if (!src.sc || src.silenced)
                        continue;
                    for (AuraSpec const& a : src.sc->auras)
                    {
                        bool const kindOk = a.kind == AuraKind::AttackHealth ||
                            (health ? a.kind == AuraKind::Health : a.kind == AuraKind::Attack);
                        if (kindOk && AuraActive(s, src, a) && AuraCovers(s, i, a, ts, ti))
                            sum += a.value;
                    }
                }
            return sum;
        }

        unsigned AuraKeywords(int ts, int ti) const
        {
            unsigned kw = 0;
            for (int s = 0; s < 2; ++s)
                for (int i = 0; i < Sz(sides[s].board.size()); ++i)
                {
                    Minion const& src = sides[s].board[static_cast<std::size_t>(i)];
                    if (!src.sc || src.silenced)
                        continue;
                    for (AuraSpec const& a : src.sc->auras)
                        if (a.kind == AuraKind::Keyword && AuraActive(s, src, a) && AuraCovers(s, i, a, ts, ti))
                            kw |= static_cast<unsigned>(a.value);
                }
            return kw;
        }

        // Side-level aura value (OwnSide scope) of kind for side: minions and the weapon.
        int SideAura(int side, AuraKind kind) const
        {
            int sum = 0;
            for (Minion const& m : sides[side].board)
                if (m.sc && !m.silenced)
                    for (AuraSpec const& a : m.sc->auras)
                        if (a.kind == kind && a.scope == AuraScope::OwnSide && AuraActive(side, m, a))
                            sum += a.value;
            Weapon const& w = sides[side].weapon;
            if (w.id != 0 && w.sc)
                for (AuraSpec const& a : w.sc->auras)
                    if (a.kind == kind)
                        sum += a.value == 0 ? 1 : a.value;
            return sum;
        }

        unsigned Keywords(int side, int idx) const
        {
            Minion const& m = sides[side].board[static_cast<std::size_t>(idx)];
            return m.kw | m.tempKw | m.nextKw | AuraKeywords(side, idx);
        }

        bool MinionHas(CharRef r, int bit) const
        {
            return r.IsMinion() && Has(Keywords(r.side, r.idx), bit);
        }

        int EffAttack(int side, int idx) const
        {
            Minion const& m = sides[side].board[static_cast<std::size_t>(idx)];
            // Lightspawn: the attack is always the current health (buffs and auras do not count).
            if (m.sc && !m.silenced)
                for (AuraSpec const& au : m.sc->auras)
                    if (au.kind == AuraKind::AttackEqualsHealth)
                        return std::max(0, EffHealth(m));
            int a = m.attack + m.tempAttack + AuraSum(side, idx, false);
            if (m.sc && !m.silenced)
                for (AuraSpec const& au : m.sc->auras)
                    if (au.kind == AuraKind::AttackPerOtherRace)
                    {
                        // Old Murk-Eye: +value for each OTHER minion of the race on the battlefield
                        int n = 0;
                        for (int s = 0; s < 2; ++s)
                            for (Minion const& o : sides[s].board)
                                if (o.id != m.id && !o.destroyed && EffHealth(o) > 0 && o.def->race == au.race)
                                    ++n;
                        a += au.value * n;
                    }
            return a < 0 ? 0 : a;
        }

        int EffMaxHealth(Minion const& m) const { return m.maxHealth + m.auraHealth; }
        int EffHealth(Minion const& m) const { return EffMaxHealth(m) - m.damage; }

        int WeaponAttack(int side) const
        {
            Weapon const& w = sides[side].weapon;
            if (w.id == 0)
                return 0;
            int a = w.attack + SideAura(side, AuraKind::WeaponAttack);
            if (w.sc)
                for (AuraSpec const& au : w.sc->auras)
                    if (au.kind == AuraKind::WeaponAttackNoHorsemen && !HorsemanOnBoard())
                        a += au.value;
            return a < 0 ? 0 : a;
        }

        // Runeblade: "the other Horsemen are dead" = none of Lady Blaumeux, Thane Korth'azz, Sir Zeliek is on either board.
        bool HorsemanOnBoard() const
        {
            for (int s = 0; s < 2; ++s)
                for (Minion const& m : sides[s].board)
                    if (!m.destroyed && EffHealth(m) > 0 &&
                        (m.def->id == "NAX9_02" || m.def->id == "NAX9_03" || m.def->id == "NAX9_04"))
                        return true;
            return false;
        }

        int HeroAttackValue(int side) const
        {
            SideState const& S = sides[side];
            int a = S.tempAttack;
            if (side == active && S.weapon.id != 0)
                a += WeaponAttack(side);
            if (side == active)
                a += SideAura(side, AuraKind::HeroAttackOwnTurn); // Worshipper
            return a < 0 ? 0 : a;
        }

        int SpellPower(int side) const
        {
            int sp = 0;
            for (Minion const& m : sides[side].board)
                sp += m.spellDamage; // Silence zeroes it; Spell Damage granted afterwards (Ancient Mage) counts
            return sp + SideAura(side, AuraKind::SpellDamage);
        }

        int AttackAt(CharRef r) const { return r.IsMinion() ? EffAttack(r.side, r.idx) : HeroAttackValue(r.side); }

        int HealthAt(CharRef r) const { return r.IsMinion() ? EffHealth(MinionAt(r)) : sides[r.side].health; }

        bool Alive(CharRef r) const
        {
            if (!r.Valid())
                return false;
            if (r.IsHero())
                return sides[r.side].health > 0;
            Minion const& m = MinionAt(r);
            return EffHealth(m) > 0 && !m.destroyed;
        }

        bool IsFrozen(CharRef r) const { return r.IsMinion() ? MinionAt(r).frozen : sides[r.side].heroFrozen; }

        // The hero power of a side; a passive one with triggers (Raise Dead) is remembered for the trigger owners.
        void SetPower(int side, CardDef const* def)
        {
            sides[side].power = def;
            Script const* sc = def ? FindScript(def->id) : nullptr;
            sides[side].powerPassive = (sc && sc->passive && !sc->triggers.empty()) ? sc : nullptr;
        }

        // A hero is Immune until the end of the turn (Keyword steps) or while one of its minions says so (Horsemen).
        bool HeroImmune(int side) const
        {
            return sides[side].heroImmune || SideAura(side, AuraKind::HeroImmune) > 0;
        }

        bool IsImmune(CharRef r) const
        {
            if (r.IsHero())
                return HeroImmune(r.side);
            return MinionHas(r, kKwImmune);
        }

        bool MinionCanAttack(int side, int idx) const
        {
            Minion const& m = sides[side].board[static_cast<std::size_t>(idx)];
            if (EffHealth(m) <= 0 || m.destroyed || m.frozen || EffAttack(side, idx) <= 0)
                return false;
            unsigned const kw = Keywords(side, idx);
            if (Has(kw, kKwCantAttack))
                return false;
            if (m.sick && !Has(kw, kKwCharge))
                return false;
            return m.attacks < (Has(kw, kKwWindfury) ? 2 : 1);
        }

        bool HeroCanAttack(int side) const
        {
            SideState const& S = sides[side];
            if (S.health <= 0 || S.heroFrozen || HeroAttackValue(side) <= 0)
                return false;
            int const maxAttacks = (S.weapon.id != 0 && S.weapon.windfury) ? 2 : 1;
            return S.heroAttacks < maxAttacks;
        }

        // Taunt minions that can be attacked (Stealth hides Taunt).
        bool HasTaunt(int side) const
        {
            for (int i = 0; i < Sz(sides[side].board.size()); ++i)
            {
                CharRef r;
                r.side = side;
                r.idx = i;
                unsigned const kw = Keywords(side, i);
                if (Alive(r) && Has(kw, kKwTaunt) && !Has(kw, kKwStealth))
                    return true;
            }
            return false;
        }

        // Health auras: losing aura health lowers damage first (never kills).
        void RefreshAuras()
        {
            for (int s = 0; s < 2; ++s)
                for (int i = 0; i < Sz(sides[s].board.size()); ++i)
                {
                    Minion& m = sides[s].board[static_cast<std::size_t>(i)];
                    int const now = AuraSum(s, i, true);
                    if (now < m.auraHealth)
                        m.damage = std::max(0, m.damage - (m.auraHealth - now));
                    m.auraHealth = now;
                }
        }

        // ---- costs ---------------------------------------------------------------

        int CostOf(int side, Card const& c) const
        {
            CardDef const& def = *c.def;
            int cost = def.cost + c.costMod;
            Script const* sc = FindScript(def.id);
            if (sc)
            {
                SideState const& S = sides[side];
                switch (sc->costRule)
                {
                    case CostRule::MinusPerOtherHandCard:
                        cost -= sc->costValue * std::max(0, Sz(S.hand.size()) - 1);
                        break;
                    case CostRule::MinusPerMinion:
                        cost -= sc->costValue * Sz(sides[0].board.size() + sides[1].board.size());
                        break;
                    case CostRule::MinusPerHeroDamage:
                        cost -= sc->costValue * std::max(0, S.maxHealth - S.health);
                        break;
                    case CostRule::MinusWeaponAttack:
                        cost -= sc->costValue * WeaponAttack(side);
                        break;
                    case CostRule::None:
                        break;
                }
            }
            bool const minion = def.type == CardType::Minion;
            bool const spellCard = def.type == CardType::Spell;
            int floor = 0;
            for (int s = 0; s < 2; ++s)
                for (Minion const& m : sides[s].board)
                {
                    if (!m.sc || m.silenced)
                        continue;
                    for (AuraSpec const& a : m.sc->auras)
                    {
                        if (a.kind != AuraKind::Cost || !AuraActive(s, m, a))
                            continue;
                        bool applies = false;
                        if (a.scope == AuraScope::OwnMinionCards)
                            applies = minion && s == side;
                        else if (a.scope == AuraScope::OwnSpellCards)
                            applies = spellCard && s == side;
                        else if (a.scope == AuraScope::AllMinionCards)
                            applies = minion;
                        if (applies && a.battlecryOnly && !def.battlecry)
                            applies = false;
                        if (!applies)
                            continue;
                        cost += a.value;
                        if (a.costFloor > floor)
                            floor = a.costFloor;
                    }
                }
            if (floor > 0 && cost < floor)
                cost = std::min(floor, std::max(cost, def.cost + c.costMod));
            SideState const& S = sides[side];
            if (spellCard)
            {
                // Millhouse Manastorm sets the cost to 0, Loatheb adds to it: whichever was made later wins its way
                // (0 + 5 = 5 when Loatheb came after Millhouse, else 0).
                if (S.spellsFree)
                    cost = S.spellTax > 0 && S.spellTaxSeq > S.spellsFreeSeq ? S.spellTax : 0;
                else
                    cost += S.spellTax;
                if (def.secret && S.freeSecret)
                    cost = 0;
                cost -= S.spellDiscount;
            }
            return cost < 0 ? 0 : cost;
        }

        // ---- triggers --------------------------------------------------------------

        bool WhoMatches(Who who, int ownerSide, EntityId owner, int subjectSide, EntityId subject) const
        {
            switch (who)
            {
                case Who::Self: return subject != 0 && subject == owner;
                case Who::Friendly: return subjectSide == ownerSide;
                case Who::OtherFriendly: return subjectSide == ownerSide && subject != owner;
                case Who::Enemy: return subjectSide >= 0 && subjectSide != ownerSide;
                case Who::Any: return true;
                case Who::AnyOther: return subject != owner;
                case Who::OwnHero: return subject != 0 && subject == sides[ownerSide].heroId;
                case Who::EnemyHero: return subject != 0 && subject == sides[1 - ownerSide].heroId;
            }
            return false;
        }

        // What a trigger's subject is (for the TriggerFlags filters).
        struct SubjectInfo
        {
            int side = -1;
            EntityId id = 0;
            CardDef const* card = nullptr;
            bool minion = false;
            bool hero = false;
            int attack = 0;
        };

        bool FlagsMatch(TriggerSpec const& t, SubjectInfo const& s) const
        {
            if ((t.flags & kTrigMinion) && !(s.minion || (s.card && s.card->type == CardType::Minion && !s.hero)))
                return false;
            if ((t.flags & kTrigHero) && !s.hero)
                return false;
            if ((t.flags & kTrigRace) && (!s.card || s.card->race != t.race))
                return false;
            if ((t.flags & kTrigSpell) && (!s.card || s.card->type != CardType::Spell))
                return false;
            if ((t.flags & kTrigOverload) && (!s.card || KeywordsOf(*s.card).overload <= 0))
                return false;
            if ((t.flags & kTrigMaxAttack) && s.attack > t.value)
                return false;
            if ((t.flags & kTrigSecret) && (!s.card || !s.card->secret))
                return false;
            if ((t.flags & kTrigDeathrattle) && (!s.card || !s.card->deathrattle))
                return false;
            return true;
        }

        SubjectInfo InfoOf(EntityId id) const
        {
            SubjectInfo s;
            s.id = id;
            CharRef r = FindChar(id);
            if (r.IsHero())
            {
                s.side = r.side;
                s.hero = true;
                return s;
            }
            if (r.IsMinion())
            {
                s.side = r.side;
                s.minion = true;
                s.card = MinionAt(r).def;
                s.attack = EffAttack(r.side, r.idx);
                return s;
            }
            if (Minion const* m = FindAnyMinion(id))
            {
                s.minion = true;
                s.card = m->def;
                s.attack = m->attack;
            }
            return s;
        }

        // A trigger owner: a minion on the board, a weapon, or a secret.
        struct Owner
        {
            uint32_t order;
            int side;
            EntityId id;
            Script const* sc;
            std::string card;
            int kind; // 0 minion, 1 weapon, 2 secret
            bool granted = false; // a granted enchantment script (side = the granting side)
        };

        std::vector<Owner> Owners() const
        {
            std::vector<Owner> out;
            for (int s = 0; s < 2; ++s)
            {
                SideState const& S = sides[s];
                for (Minion const& m : S.board)
                {
                    if (m.sc && !m.silenced && !m.sc->triggers.empty()) // mortally wounded minions still trigger
                        out.push_back(Owner{m.order, s, m.id, m.sc, m.def->id, 0, false});
                    for (Granted const& g : m.grants)
                        if (!g.sc->triggers.empty())
                            out.push_back(Owner{m.order, g.side, m.id, g.sc, m.def->id, 0, true});
                }
                if (S.weapon.id != 0 && S.weapon.sc && !S.weapon.sc->triggers.empty())
                    out.push_back(Owner{S.weapon.order, s, S.weapon.id, S.weapon.sc, S.weapon.def->id, 1});
                if (S.powerPassive) // a passive hero power reacts from the start: it is older than every minion
                    out.push_back(Owner{0, s, S.powerId, S.powerPassive, S.power->id, 3});
                for (SecretCard const& sec : S.secrets)
                    if (sec.sc && s != active)
                        out.push_back(Owner{sec.order, s, sec.id, sec.sc, sec.def->id, 2});
            }
            std::stable_sort(out.begin(), out.end(), [](Owner const& a, Owner const& b) { return a.order < b.order; });
            return out;
        }

        // Collects the triggered abilities that fire for on. subjectSide is the side of the subject (or, for
        // TurnStart / TurnEnd / Played / SpellCast / AfterSpell, of the player whose turn / card it is).
        std::vector<Pending> Collect(On on, int subjectSide, SubjectInfo const& subj, EntityId target, int amount,
            EntityId damageSource)
        {
            std::vector<Pending> out;
            for (Owner const& o : Owners())
            {
                for (TriggerSpec const& t : o.sc->triggers)
                {
                    if (t.on != on)
                        continue;
                    bool match = false;
                    switch (on)
                    {
                        case On::TurnStart:
                        case On::TurnEnd:
                            match = t.who == Who::Any || (t.who == Who::Friendly && subjectSide == o.side) ||
                                (t.who == Who::Enemy && subjectSide != o.side);
                            break;
                        case On::DealtDamage:
                            match = damageSource == o.id && WhoMatches(t.who == Who::Self ? Who::Any : t.who, o.side,
                                o.id, subj.side, subj.id);
                            break;
                        case On::Attacked:
                        {
                            SubjectInfo const d = InfoOf(target);
                            match = WhoMatches(t.who, o.side, o.id, d.side, d.id) &&
                                (!(t.flags & kTrigDefenderMinion) || d.minion);
                            break;
                        }
                        case On::Played:
                        case On::SpellCast:
                        case On::AfterSpell:
                            match = (t.who == Who::Any || t.who == Who::AnyOther ||
                                        (t.who == Who::Friendly && subjectSide == o.side) ||
                                        (t.who == Who::OtherFriendly && subjectSide == o.side) ||
                                        (t.who == Who::Enemy && subjectSide != o.side)) &&
                                subj.id != o.id;
                            break;
                        default:
                            match = WhoMatches(t.who, o.side, o.id, subj.side, subj.id);
                            break;
                    }
                    if (!match || !FlagsMatch(t, subj))
                        continue;
                    Pending p;
                    p.order = o.order;
                    p.serial = ++serial;
                    p.steps = &t.steps;
                    p.spec = &t;
                    p.cond = t.cond;
                    p.condRace = t.condRace;
                    p.kind = o.kind == 2 ? 2 : 0;
                    p.owner = o.id;
                    p.ownerCard = o.card;
                    p.granted = o.granted;
                    p.ctx.side = o.side;
                    p.ctx.source = o.id;
                    p.ctx.subject = subj.id;
                    p.ctx.subjectSide = subj.side >= 0 ? subj.side : subjectSide;
                    p.ctx.subjectCard = subj.card;
                    p.ctx.target = target;
                    p.ctx.amount = amount;
                    out.push_back(p);
                    if (o.kind == 2)
                        break; // a secret triggers once
                }
            }
            return out;
        }

        void Queue(On on, int subjectSide, SubjectInfo const& subj, EntityId target = 0, int amount = 0, EntityId damageSource = 0)
        {
            if (phase == Phase::Over)
                return;
            std::vector<Pending> const p = Collect(on, subjectSide, subj, target, amount, damageSource);
            queue.insert(queue.end(), p.begin(), p.end());
        }

        // Resolves the triggers of a PRE event at once (in play order). Returns how many fired.
        int FireNow(On on, int subjectSide, SubjectInfo const& subj, EntityId target, int amount = 0)
        {
            if (phase == Phase::Over)
                return 0;
            std::vector<Pending> p = Collect(on, subjectSide, subj, target, amount, 0);
            int fired = 0;
            for (Pending& pe : p)
            {
                if (phase == Phase::Over)
                    break;
                if (on == On::Attacked || on == On::Attacks)
                {
                    if (!attackActive)
                        break;
                    // An earlier trigger of the same attack (Vaporize, Freezing Trap, a trap that killed the
                    // attacker) may have removed the attacker: the attack is gone, nothing else reacts to it.
                    if (!Alive(FindChar(attackAttacker)))
                        break;
                    pe.ctx.target = attackDefender;
                    // The defender may have changed (Misdirection, Noble Sacrifice): the secrets that
                    // were collected for the original defender only fire when they still match it.
                    if (on == On::Attacked && pe.spec)
                    {
                        SubjectInfo const d = InfoOf(attackDefender);
                        if (!WhoMatches(pe.spec->who, pe.ctx.side, pe.owner, d.side, d.id) ||
                            ((pe.spec->flags & kTrigDefenderMinion) && !d.minion))
                            continue;
                    }
                }
                if (on == On::SpellCast)
                {
                    if (spellCountered)
                        break;
                    pe.ctx.target = spellTarget;
                }
                if (Resolve(pe))
                    ++fired;
            }
            return fired;
        }

        bool CondHolds(Cond cond, std::string const& race, Ctx const& c) const
        {
            SideState const& S = sides[c.side];
            switch (cond)
            {
                case Cond::None: return true;
                case Cond::HaveRace:
                case Cond::NoRace:
                {
                    bool have = false;
                    for (int i = 0; i < Sz(S.board.size()); ++i)
                    {
                        CharRef r;
                        r.side = c.side;
                        r.idx = i;
                        if (Alive(r) && S.board[static_cast<std::size_t>(i)].def->race == race &&
                            S.board[static_cast<std::size_t>(i)].id != c.source)
                            have = true;
                    }
                    return cond == Cond::HaveRace ? have : !have;
                }
                case Cond::TargetDead:
                {
                    CharRef t = FindChar(c.target);
                    return c.target != 0 && !Alive(t);
                }
                case Cond::TargetAlive: return Alive(FindChar(c.target));
                case Cond::HandNotEmpty: return !S.hand.empty();
                case Cond::EnemyHasWeapon: return sides[1 - c.side].weapon.id != 0;
                case Cond::HaveWeapon: return S.weapon.id != 0;
                case Cond::DamagedSelf:
                {
                    CharRef r = FindChar(c.source);
                    return r.IsMinion() && MinionAt(r).damage > 0;
                }
                case Cond::FreeSlot: return Sz(S.board.size()) < kMaxBoard;
                case Cond::SubjectMinion: return FindAnyMinion(c.subject) != nullptr;
                case Cond::TargetMinion: return FindChar(c.target).IsMinion();
                case Cond::TargetMinionFreeSlot: return FindChar(c.target).IsMinion() && Sz(S.board.size()) < kMaxBoard;
                case Cond::NoWeapon: return S.weapon.id == 0;
                case Cond::HaveSecret: return !S.secrets.empty();
                case Cond::HaveMinion: return !Living(c.side, false, true).empty();
                case Cond::EchoPending:
                {
                    CharRef r = FindChar(c.source);
                    return r.IsMinion() && MinionAt(r).echo;
                }
                // "died this game" counts both sides' deaths (the card text has no "friendly"; the wiki: whoever owns them)
                case Cond::StalaggDied: return ((sides[0].diedMask | sides[1].diedMask) & kDiedStalagg) != 0;
                case Cond::FeugenDied: return ((sides[0].diedMask | sides[1].diedMask) & kDiedFeugen) != 0;
                case Cond::TargetFrozen:
                case Cond::TargetNotFrozen:
                {
                    CharRef t = FindChar(c.target);
                    bool const frozen = t.Valid() && IsFrozen(t);
                    return cond == Cond::TargetFrozen ? frozen : !frozen;
                }
            }
            return true;
        }

        // Runs one queued / pre ability. False when its condition failed (nothing happened).
        bool Resolve(Pending& p)
        {
            if (phase == Phase::Over)
                return false;
            if (++resolutions > kMaxResolutionSteps)
            {
                capped = true;
                queue.clear();
                return false;
            }
            if (p.kind == 2)
            {
                p.ctx.spell = true; // secrets are spells: Spell Damage counts
                p.ctx.spellPower = SpellPower(p.ctx.side);
                // A secret: still in play, still the other side's turn, condition holds.
                SideState& S = sides[p.ctx.side];
                auto it = std::find_if(S.secrets.begin(), S.secrets.end(), [&](SecretCard const& sc) { return sc.id == p.owner; });
                if (it == S.secrets.end() || active == p.ctx.side || !CondHolds(p.cond, p.condRace, p.ctx))
                    return false;
                SecretCard const sec = *it;
                S.secrets.erase(it);
                Emit(EventType::Reveal, p.ctx.side, p.ctx.subject, sec.id, 0, sec.def->id);
                SubjectInfo si;
                si.side = p.ctx.side;
                si.id = sec.id;
                si.card = sec.def;
                Queue(On::SecretRevealed, p.ctx.side, si);
            }
            else
            {
                if (p.kind == 0)
                {
                    // A queued ability still resolves when its minion died meanwhile (Acolyte of Pain, Water
                    // Elemental), but not when the minion was silenced, returned to the hand or the weapon is gone. A passive
                    // hero power (Raise Dead) is always there.
                    CharRef r = FindChar(p.owner);
                    bool const weapon = sides[p.ctx.side].weapon.id == p.owner ||
                        (sides[p.ctx.side].powerPassive && sides[p.ctx.side].powerId == p.owner);
                    bool dead = false;
                    for (Minion const& g : graveyard)
                        if (g.id == p.owner)
                            dead = true;
                    if (!weapon && !dead && !(r.IsMinion() && (!MinionAt(r).silenced || p.granted)))
                        return false;
                }
                if (!CondHolds(p.cond, p.condRace, p.ctx))
                    return false;
                Emit(p.kind == 1 ? EventType::Deathrattle : EventType::Trigger, p.ctx.side, p.owner, p.ctx.subject, 0, p.ownerCard);
            }
            // Baron Rivendare (times = 2): every deathrattle list of the minion runs twice in a row, nothing in between.
            // The second run of the first list is announced like the first.
            int const times = p.times < 1 ? 1 : p.times;
            for (int k = 0; k < times && phase != Phase::Over; ++k)
            {
                if (k > 0)
                    Emit(EventType::Deathrattle, p.ctx.side, p.owner, p.ctx.subject, 0, p.ownerCard);
                RunSteps(*p.steps, p.ctx);
            }
            for (std::vector<Step> const* more : p.more)
                for (int k = 0; k < times && phase != Phase::Over; ++k)
                    RunSteps(*more, p.ctx);
            return true;
        }

        // Resolves the queue and the death steps until nothing is left.
        void Settle()
        {
            for (int guard = 0; guard < 10000 && phase != Phase::Over; ++guard)
            {
                RefreshAuras();
                if (DeathStep())
                    continue;
                if (phase == Phase::Over)
                    break;
                if (queue.empty())
                    break;
                std::vector<Pending> batch;
                batch.swap(queue);
                std::stable_sort(batch.begin(), batch.end(),
                    [](Pending const& a, Pending const& b) { return a.order != b.order ? a.order < b.order : a.serial < b.serial; });
                for (Pending& p : batch)
                {
                    if (phase == Phase::Over)
                        break;
                    Resolve(p);
                    RefreshAuras();
                    DeathStep();
                }
            }
            queue.clear();
            RefreshAuras();
        }

        // ---- death -------------------------------------------------------------------

        // Removes every dead minion at once and queues Died triggers and deathrattles; then checks the heroes.
        // True when a minion died.
        bool DeathStep()
        {
            if (phase == Phase::Over)
                return false;
            struct Dead
            {
                Minion m;
                int side;
                int slot;
                int pos; // 1-based index on the board before this death step removed anything
                EntityId adjLeft;  // the neighbours before this death step removed anything (Necroknight)
                EntityId adjRight;
            };
            std::vector<Dead> dead;
            int const firstSide = active;
            for (int k = 0; k < 2; ++k)
            {
                int const s = k == 0 ? firstSide : 1 - firstSide;
                std::vector<Minion>& board = sides[s].board;
                int survivors = 0;
                for (std::size_t i = 0; i < board.size(); ++i)
                {
                    if (EffHealth(board[i]) <= 0 || board[i].destroyed)
                        dead.push_back(Dead{board[i], s, survivors, static_cast<int>(i) + 1,
                            i > 0 ? board[i - 1].id : 0, i + 1 < board.size() ? board[i + 1].id : 0});
                    else
                        ++survivors;
                }
            }
            // Weapons at 0 durability break here too.
            for (int s = 0; s < 2; ++s)
                if (sides[s].weapon.id != 0 && sides[s].weapon.durability <= 0)
                    BreakWeapon(s, 0);
            if (!dead.empty())
            {
                for (int s = 0; s < 2; ++s)
                {
                    std::vector<Minion>& board = sides[s].board;
                    board.erase(std::remove_if(board.begin(), board.end(),
                                    [&](Minion const& m) { return EffHealth(m) <= 0 || m.destroyed; }),
                        board.end());
                }
                for (Dead const& d : dead)
                {
                    ++sides[1 - d.side].tally.minionsDestroyed;
                    Emit(EventType::Death, d.side, 0, d.m.id, d.pos, d.m.def->id);
                    graveyard.push_back(d.m);
                }
                RefreshAuras();
                // Baron Rivendare doubles the Deathrattles of the dead minions' side only while he is still on the board
                // after this death step: a Baron that dies in the same step does not double them (hearthstone.wiki.gg).
                int times[2] = {1, 1};
                for (int s = 0; s < 2; ++s)
                    for (Minion const& m : sides[s].board)
                        if (m.sc && !m.silenced && m.sc->doublesDeathrattles)
                            times[s] = 2;
                // The deaths are recorded before any ability of this step resolves (Kel'Thuzad, Stalagg / Feugen: two
                // that die together both see the other as dead).
                for (Dead const& d : dead)
                {
                    sides[d.side].diedThisTurn.push_back(d.m.def);
                    if (d.m.def->id == "FP1_014")
                        sides[d.side].diedMask |= kDiedStalagg;
                    else if (d.m.def->id == "FP1_015")
                        sides[d.side].diedMask |= kDiedFeugen;
                }
                std::vector<Dead> byOrder = dead;
                std::stable_sort(byOrder.begin(), byOrder.end(), [](Dead const& a, Dead const& b) { return a.m.order < b.m.order; });
                for (Dead const& d : byOrder)
                {
                    SubjectInfo si;
                    si.side = d.side;
                    si.id = d.m.id;
                    si.card = d.m.def;
                    si.minion = true;
                    si.attack = d.m.attack;
                    Queue(On::Died, d.side, si);
                    // The minion's own deathrattle first, then the granted ones in the order they were granted.
                    std::vector<std::vector<Step> const*> lists;
                    if (d.m.sc && !d.m.silenced && !d.m.sc->deathrattle.empty())
                        lists.push_back(&d.m.sc->deathrattle);
                    for (Granted const& g : d.m.grants)
                        if (!g.sc->deathrattle.empty())
                            lists.push_back(&g.sc->deathrattle);
                    if (!lists.empty())
                    {
                        Pending p;
                        p.order = d.m.order;
                        p.serial = ++serial;
                        p.steps = lists[0];
                        p.more.assign(lists.begin() + 1, lists.end());
                        p.kind = 1;
                        p.times = times[d.side];
                        p.owner = d.m.id;
                        p.ownerCard = d.m.def->id;
                        p.ctx.side = d.side;
                        p.ctx.source = d.m.id;
                        p.ctx.subject = d.m.id;
                        p.ctx.summonSide = d.side;
                        p.ctx.summonPos = d.slot;
                        p.ctx.adjLeft = d.adjLeft;
                        p.ctx.adjRight = d.adjRight;
                        queue.push_back(p);
                    }
                }
            }
            bool const d0 = sides[0].health <= 0;
            bool const d1 = sides[1].health <= 0;
            if (d0 || d1)
                EndMatch(d0 && d1 ? -1 : (d0 ? 1 : 0), EndReason::HeroDied);
            return !dead.empty();
        }

        // ---- primitive effects -------------------------------------------------

        // The weapon of side leaves play (durability gone, destroyed or replaced). A Deathrattle of the weapon (Death's
        // Bite) is queued like a minion's; Baron Rivendare does not double it (he only doubles minions).
        void BreakWeapon(int side, EntityId source)
        {
            Weapon const w = sides[side].weapon;
            if (w.id == 0)
                return;
            Emit(EventType::WeaponBreak, side, source, w.id, 0, w.def->id);
            sides[side].weapon = Weapon();
            if (w.sc && !w.sc->deathrattle.empty())
            {
                Pending p;
                p.order = w.order;
                p.serial = ++serial;
                p.steps = &w.sc->deathrattle;
                p.kind = 1;
                p.owner = w.id;
                p.ownerCard = w.def->id;
                p.ctx.side = side;
                p.ctx.source = w.id;
                p.ctx.subject = w.id;
                queue.push_back(p);
            }
        }

        void EndMatch(int winner, EndReason reason)
        {
            if (phase == Phase::Over)
                return;
            phase = Phase::Over;
            result.over = true;
            result.winner = winner;
            result.reason = reason;
            queue.clear();
            Emit(EventType::GameOver, winner, 0, 0, static_cast<int>(reason));
        }

        int SourceSideOf(EntityId source) const
        {
            CharRef r = FindChar(source);
            if (r.Valid())
                return r.side;
            for (int s = 0; s < 2; ++s)
                if (sides[s].weapon.id == source || sides[s].powerId == source)
                    return s;
            return -1;
        }

        void Damage(EntityId source, CharRef t, int amount, int sourceSide = -1)
        {
            if (amount <= 0 || !t.Valid() || phase == Phase::Over)
                return;
            if (IsImmune(t))
                return;
            if (t.IsMinion())
            {
                Minion& m = MinionAt(t);
                if (Has(m.kw | m.tempKw | m.nextKw | AuraKeywords(t.side, t.idx), kKwDivineShield))
                {
                    m.kw &= ~static_cast<unsigned>(kKwDivineShield);
                    m.tempKw &= ~static_cast<unsigned>(kKwDivineShield);
                    Emit(EventType::ShieldPop, t.side, source, m.id, 0);
                    return;
                }
                m.damage += amount;
                // Commanding Shout: the minions of that side cannot be reduced below 1 Health this turn.
                if (sides[t.side].minionFloor && EffHealth(m) < 1)
                    m.damage = std::max(0, EffMaxHealth(m) - 1);
                EntityId const id = m.id;
                Emit(EventType::Damage, t.side, source, id, amount);
                AfterDamage(source, t, amount);
                return;
            }
            SideState& S = sides[t.side];
            if (S.health + S.armor - amount <= 0)
            {
                preventDamage = false;
                SubjectInfo si = InfoOf(S.heroId);
                FireNow(On::FatalDamage, t.side, si, 0, amount);
                if (preventDamage || HeroImmune(t.side))
                {
                    preventDamage = false;
                    return;
                }
            }
            Emit(EventType::Damage, t.side, source, S.heroId, amount);
            int const owner = sourceSide >= 0 ? sourceSide : (source ? SourceSideOf(source) : -1);
            if (owner == 1 - t.side)
                sides[owner].tally.heroDamage += static_cast<uint32_t>(std::min(amount, S.armor + std::max(0, S.health)));
            int const absorbed = std::min(S.armor, amount);
            S.armor -= absorbed;
            S.health -= amount - absorbed;
            if (absorbed > 0 && S.armor == 0 && !S.phaseDone && S.power)
                if (Script const* ps = FindScript(S.power->id))
                    if (ps->phase.present)
                        S.phasePending = true; // the Armor is gone: the second phase starts (see CheckPhases)
            AfterDamage(source, t, amount);
        }

        void AfterDamage(EntityId source, CharRef t, int amount)
        {
            EntityId const id = CharId(t);
            SubjectInfo const si = InfoOf(id);
            // Poisonous source minion.
            CharRef src = FindChar(source);
            if (t.IsMinion() && src.IsMinion() && MinionHas(src, kKwPoisonous))
            {
                Minion& m = MinionAt(t);
                if (!m.destroyed)
                {
                    m.destroyed = true;
                    Emit(EventType::Destroy, t.side, source, id, 0);
                }
            }
            Queue(On::Damaged, t.side, si, 0, amount, source);
            Queue(On::DealtDamage, t.side, si, 0, amount, source);
        }

        void Heal(int side, EntityId source, CharRef t, int amount)
        {
            if (amount <= 0 || !t.Valid())
                return;
            if (side >= 0 && SideAura(side, AuraKind::HealIsDamage) > 0)
            {
                Damage(source, t, amount, side);
                return;
            }
            int healed = 0;
            if (t.IsMinion())
            {
                Minion& m = MinionAt(t);
                healed = std::min(amount, m.damage);
                m.damage -= healed;
            }
            else
            {
                SideState& S = sides[t.side];
                int const nh = std::min(S.maxHealth, S.health + amount);
                healed = nh - S.health;
                S.health = nh;
            }
            if (healed > 0)
            {
                Emit(EventType::Heal, t.side, source, CharId(t), healed);
                Queue(On::Healed, t.side, InfoOf(CharId(t)));
            }
        }

        void GainArmor(int side, EntityId source, int amount)
        {
            if (amount <= 0)
                return;
            sides[side].armor += amount;
            Emit(EventType::Armor, side, source, sides[side].heroId, amount);
        }

        DrawResult DrawOne(int side)
        {
            SideState& S = sides[side];
            DrawResult r;
            if (S.deck.empty())
            {
                // noDraw skips only the turn-start draw (see TurnStart); a forced draw takes fatigue like any side's.
                ++S.fatigue;
                Emit(EventType::Fatigue, side, 0, S.heroId, S.fatigue);
                Damage(0, Hero(side), S.fatigue);
                return r;
            }
            Card const c = S.deck.back();
            S.deck.pop_back();
            r.card = c;
            if (Sz(S.hand.size()) >= kMaxHand)
            {
                Emit(EventType::Burn, side, 0, c.id, 0, c.def->id);
                r.kind = 2;
                return r;
            }
            S.hand.push_back(c);
            Emit(EventType::Draw, side, 0, c.id, 0, c.def->id).hideFromOther = true;
            r.kind = 1;
            return r;
        }

        void DrawCards(int side, int n)
        {
            for (int i = 0; i < n && phase != Phase::Over; ++i)
                DrawOne(side);
        }

        // Opening / mulligan draws: never fatigue (a tiny test deck simply runs out).
        void DrawNoFatigue(int side, int n)
        {
            for (int i = 0; i < n && !sides[side].deck.empty(); ++i)
                DrawOne(side);
        }

        Minion MakeMinion(CardDef const& def, EntityId id)
        {
            Detail::Keywords const k = KeywordsOf(def);
            Minion m;
            m.id = id;
            m.def = &def;
            m.sc = FindScript(def.id);
            m.attack = def.attack;
            m.maxHealth = def.health;
            m.kw = 0;
            if (k.taunt)
                m.kw |= kKwTaunt;
            if (k.charge)
                m.kw |= kKwCharge;
            if (k.divineShield)
                m.kw |= kKwDivineShield;
            if (k.windfury)
                m.kw |= kKwWindfury;
            if (k.stealth)
                m.kw |= kKwStealth;
            if (k.poisonous)
                m.kw |= kKwPoisonous;
            if (k.cantAttack)
                m.kw |= kKwCantAttack;
            m.spellDamage = k.spellDamage;
            m.sick = true;
            if (m.sc && m.sc->permFrozen)
                m.frozen = true; // Frozen Champion
            return m;
        }

        // Summons def at pos (-1 or beyond: rightmost). Returns the minion id, 0 when the board is full.
        EntityId Summon(int side, CardDef const& def, int pos, EntityId id)
        {
            SideState& S = sides[side];
            if (Sz(S.board.size()) >= kMaxBoard || def.type != CardType::Minion)
                return 0;
            Minion m = MakeMinion(def, id ? id : NewId());
            m.order = ++playOrder;
            int const at = (pos < 0 || pos > Sz(S.board.size())) ? Sz(S.board.size()) : pos;
            S.board.insert(S.board.begin() + at, m);
            Emit(EventType::Summon, side, 0, m.id, at + 1, def.id);
            RefreshAuras();
            Queue(On::Summoned, side, InfoOf(m.id));
            return m.id;
        }

        void Equip(int side, CardDef const& def, EntityId id)
        {
            SideState& S = sides[side];
            if (S.weapon.id != 0)
                BreakWeapon(side, 0);
            S.weapon.id = id ? id : NewId();
            S.weapon.def = &def;
            S.weapon.sc = FindScript(def.id);
            S.weapon.attack = def.attack;
            S.weapon.durability = def.durability;
            S.weapon.windfury = KeywordsOf(def).windfury;
            S.weapon.order = ++playOrder;
            Emit(EventType::Equip, side, 0, S.weapon.id, 0, def.id);
        }

        void Transform(CharRef t, CardDef const& def)
        {
            Minion& m = MinionAt(t);
            Minion fresh = MakeMinion(def, m.id);
            fresh.sick = m.sick;
            fresh.attacks = m.attacks;
            fresh.order = m.order;
            m = fresh;
            Emit(EventType::Transform, t.side, 0, m.id, 0, def.id);
            RefreshAuras();
        }

        void Silence(CharRef t, EntityId source)
        {
            Minion& m = MinionAt(t);
            m.attack = m.def->attack;
            m.maxHealth = m.def->health;
            m.kw = 0;
            m.tempKw = 0;
            m.nextKw = 0;
            m.tempAttack = 0;
            m.spellDamage = 0;
            m.frozen = false;
            m.freezeKeep = false;
            m.destroyAtStartOf = -1;
            m.dieAtEndOfTurn = false;
            m.dieSkipOnce = false;
            m.echo = false;     // an Echoing Ooze silenced before the end of the turn does not copy itself
            m.grants.clear();   // granted deathrattles / triggers go (Soul of the Forest ...)
            m.returnSide = -1;  // a Shadow Madness'd minion stays with its thief
            m.silenced = true;
            Emit(EventType::Silence, t.side, source, m.id, 0);
            RefreshAuras();
        }

        void Freeze(CharRef t, EntityId source)
        {
            if (t.IsMinion())
            {
                Minion& m = MinionAt(t);
                m.frozen = true;
                m.freezeKeep = (t.side == active && m.attacks > 0);
                Emit(EventType::Freeze, t.side, source, m.id, 0);
            }
            else
            {
                SideState& S = sides[t.side];
                S.heroFrozen = true;
                S.heroFreezeKeep = (t.side == active && S.heroAttacks > 0);
                Emit(EventType::Freeze, t.side, source, S.heroId, 0);
            }
        }

        void AddToHand(int side, CardDef const& def, EntityId source)
        {
            SideState& S = sides[side];
            if (Sz(S.hand.size()) >= kMaxHand)
                return;
            Card c;
            c.id = NewId();
            c.def = &def;
            S.hand.push_back(c);
            Emit(EventType::Generate, side, source, c.id, 0, def.id).hideFromOther = true;
        }

        void Bounce(CharRef t, int costChange)
        {
            int const owner = t.side;
            Minion const m = MinionAt(t);
            SideState& S = sides[owner];
            if (Sz(S.hand.size()) >= kMaxHand)
            {
                MinionAt(t).destroyed = true;
                Emit(EventType::Destroy, owner, 0, m.id, 0);
                return;
            }
            S.board.erase(S.board.begin() + t.idx);
            Card c;
            c.id = m.id;
            c.def = m.def;
            c.costMod = costChange;
            S.hand.push_back(c);
            Emit(EventType::Bounce, owner, 0, m.id, 0, m.def->id);
            RefreshAuras();
        }

        void TakeControl(CharRef t, int newSide)
        {
            if (t.side == newSide)
                return;
            if (Sz(sides[newSide].board.size()) >= kMaxBoard)
            {
                MinionAt(t).destroyed = true;
                Emit(EventType::Destroy, t.side, 0, MinionAt(t).id, 0);
                return;
            }
            Minion m = MinionAt(t);
            sides[t.side].board.erase(sides[t.side].board.begin() + t.idx);
            m.sick = true;
            m.attacks = 0;
            m.returnSide = -1;
            sides[newSide].board.push_back(m);
            Emit(EventType::Control, newSide, 0, m.id, 0, m.def->id);
            RefreshAuras();
        }

        void Destroy(CharRef t, EntityId source)
        {
            if (t.IsMinion())
            {
                Minion& m = MinionAt(t);
                if (!m.destroyed)
                {
                    m.destroyed = true;
                    Emit(EventType::Destroy, t.side, source, m.id, 0);
                }
            }
            else if (t.IsHero())
            {
                // Destroying a hero: health to 0 (no such Classic card; kept for completeness).
                sides[t.side].health = 0;
            }
        }

        void DiscardRandom(int side, int n)
        {
            SideState& S = sides[side];
            for (int k = 0; k < n && !S.hand.empty(); ++k)
            {
                std::size_t const i = static_cast<std::size_t>(Rand(Sz(S.hand.size())));
                Card const c = S.hand[i];
                S.hand.erase(S.hand.begin() + static_cast<std::ptrdiff_t>(i));
                Emit(EventType::Discard, side, 0, c.id, 0, c.def->id);
            }
        }

        void GainCrystals(int side, int n, bool full, EntityId source)
        {
            SideState& S = sides[side];
            for (int k = 0; k < n; ++k)
            {
                if (S.maxMana >= kMaxMana)
                {
                    // Only an EMPTY crystal (Wild Growth) turns into Excess Mana; Nourish and Arcane Golem
                    // (full crystals) do nothing at 10.
                    if (!full)
                        if (CardDef const* ex = db->Find(kExcessManaId))
                            AddToHand(side, *ex, source);
                    continue;
                }
                ++S.maxMana;
                if (full)
                    S.mana = std::min(kMaxMana, S.mana + 1);
                Emit(EventType::Crystal, side, source, 0, 1);
            }
        }

        void DestroyCrystals(int side, int n, EntityId source)
        {
            SideState& S = sides[side];
            int const k = std::min(n, S.maxMana);
            if (k <= 0)
                return;
            S.maxMana -= k;
            S.mana = std::min(S.mana, S.maxMana);
            Emit(EventType::Crystal, side, source, 0, -k);
        }

        // ---- selections -----------------------------------------------------------

        std::vector<CharRef> Living(int side, bool hero, bool minions, EntityId except = 0) const
        {
            std::vector<CharRef> out;
            if (hero && sides[side].health > 0 && sides[side].heroId != except)
                out.push_back(Hero(side));
            if (minions)
                for (int i = 0; i < Sz(sides[side].board.size()); ++i)
                {
                    CharRef r;
                    r.side = side;
                    r.idx = i;
                    if (Alive(r) && MinionAt(r).id != except)
                        out.push_back(r);
                }
            return out;
        }

        void Append(std::vector<EntityId>& out, std::vector<CharRef> const& refs) const
        {
            for (CharRef const& r : refs)
                out.push_back(CharId(r));
        }

        std::vector<EntityId> RandomPick(std::vector<CharRef> cand, int count)
        {
            std::vector<EntityId> out;
            for (int k = 0; k < count && !cand.empty(); ++k)
            {
                std::size_t const pick = static_cast<std::size_t>(Rand(Sz(cand.size())));
                out.push_back(CharId(cand[pick]));
                cand.erase(cand.begin() + static_cast<std::ptrdiff_t>(pick));
            }
            return out;
        }

        std::vector<CharRef> RandomPool(Sel sel, Ctx const& c) const
        {
            int const own = c.side;
            int const enemy = 1 - c.side;
            std::vector<CharRef> cand;
            switch (sel)
            {
                case Sel::RandomEnemyMinion: return Living(enemy, false, true);
                case Sel::RandomEnemyCharacter: return Living(enemy, true, true);
                case Sel::RandomFriendlyMinion: return Living(own, false, true);
                case Sel::RandomOtherFriendlyMinion: return Living(own, false, true, c.source);
                case Sel::RandomMinion:
                case Sel::RandomOtherMinion:
                {
                    EntityId const except = sel == Sel::RandomOtherMinion ? c.source : 0;
                    cand = Living(own, false, true, except);
                    std::vector<CharRef> e = Living(enemy, false, true, except);
                    cand.insert(cand.end(), e.begin(), e.end());
                    return cand;
                }
                case Sel::RandomOtherCharacter:
                {
                    cand = Living(own, true, true, c.source);
                    std::vector<CharRef> e = Living(enemy, true, true, c.source);
                    cand.insert(cand.end(), e.begin(), e.end());
                    return cand;
                }
                case Sel::RandomDamagedCharacter:
                {
                    for (int s = 0; s < 2; ++s)
                        for (CharRef const& r : Living(s, true, true))
                            if (HealthAt(r) < (r.IsMinion() ? EffMaxHealth(MinionAt(r)) : sides[r.side].maxHealth))
                                cand.push_back(r);
                    return cand;
                }
                case Sel::RandomOtherCharacterForAttack:
                {
                    for (int s = 0; s < 2; ++s)
                        for (CharRef const& r : Living(s, true, true))
                        {
                            EntityId const id = CharId(r);
                            if (id != attackAttacker && id != attackDefender)
                                cand.push_back(r);
                        }
                    return cand;
                }
                default: return cand;
            }
        }

        static bool IsRandomSel(Sel sel)
        {
            return sel == Sel::RandomEnemyMinion || sel == Sel::RandomEnemyCharacter || sel == Sel::RandomFriendlyMinion ||
                sel == Sel::RandomOtherFriendlyMinion || sel == Sel::RandomMinion || sel == Sel::RandomOtherMinion ||
                sel == Sel::RandomOtherCharacter || sel == Sel::RandomOtherCharacterForAttack ||
                sel == Sel::RandomDamagedCharacter;
        }

        std::vector<EntityId> Select(Sel sel, Ctx const& c, int count = 1)
        {
            std::vector<EntityId> out;
            int const own = c.side;
            int const enemy = 1 - c.side;
            switch (sel)
            {
                case Sel::None: break;
                case Sel::Target:
                    if (c.target)
                        out.push_back(c.target);
                    break;
                case Sel::Self:
                {
                    CharRef r = FindChar(c.source);
                    if (r.IsMinion())
                        out.push_back(c.source);
                    break;
                }
                case Sel::Subject:
                    if (c.subject)
                        out.push_back(c.subject);
                    break;
                case Sel::LastSummoned:
                    if (c.lastSummoned)
                        out.push_back(c.lastSummoned);
                    break;
                case Sel::OwnHero: out.push_back(sides[own].heroId); break;
                case Sel::EnemyHero: out.push_back(sides[enemy].heroId); break;
                case Sel::BothHeroes:
                    out.push_back(sides[own].heroId);
                    out.push_back(sides[enemy].heroId);
                    break;
                case Sel::FriendlyMinions: Append(out, Living(own, false, true)); break;
                case Sel::OtherFriendlyMinions: Append(out, Living(own, false, true, c.source)); break;
                case Sel::EnemyMinions: Append(out, Living(enemy, false, true)); break;
                case Sel::AllMinions:
                    Append(out, Living(own, false, true));
                    Append(out, Living(enemy, false, true));
                    break;
                case Sel::AllOtherMinions:
                    Append(out, Living(own, false, true, c.source));
                    Append(out, Living(enemy, false, true, c.source));
                    break;
                case Sel::FriendlyCharacters: Append(out, Living(own, true, true)); break;
                case Sel::EnemyCharacters: Append(out, Living(enemy, true, true)); break;
                case Sel::AllCharacters:
                    Append(out, Living(own, true, true));
                    Append(out, Living(enemy, true, true));
                    break;
                case Sel::AllOtherCharacters:
                    Append(out, Living(own, true, true, c.source));
                    Append(out, Living(enemy, true, true, c.source));
                    break;
                case Sel::AllOtherThanTarget:
                {
                    CharRef t = FindChar(c.target);
                    if (t.Valid())
                        Append(out, Living(t.side, true, true, c.target));
                    break;
                }
                case Sel::LeftmostEnemyMinion:
                {
                    std::vector<CharRef> const e = Living(enemy, false, true);
                    if (!e.empty())
                        out.push_back(CharId(e.front()));
                    break;
                }
                case Sel::AdjacentAtDeath:
                    for (EntityId id : {c.adjLeft, c.adjRight})
                    {
                        CharRef n = FindChar(id);
                        if (n.IsMinion() && Alive(n))
                            out.push_back(id);
                    }
                    break;
                case Sel::AdjacentToSelf:
                case Sel::AdjacentToTarget:
                {
                    CharRef r = FindChar(sel == Sel::AdjacentToSelf ? c.source : c.target);
                    if (r.IsMinion())
                        for (int d = -1; d <= 1; d += 2)
                        {
                            CharRef n;
                            n.side = r.side;
                            n.idx = r.idx + d;
                            if (n.idx >= 0 && n.idx < Sz(sides[r.side].board.size()) && Alive(n))
                                out.push_back(CharId(n));
                        }
                    break;
                }
                default:
                    if (IsRandomSel(sel))
                        out = RandomPick(RandomPool(sel, c), count < 1 ? 1 : count);
                    break;
            }
            return out;
        }

        std::vector<EntityId> FilterRace(std::vector<EntityId> const& ids, std::string const& race) const
        {
            if (race.empty())
                return ids;
            std::vector<EntityId> out;
            for (EntityId id : ids)
            {
                CharRef r = FindChar(id);
                if (r.IsMinion() && MinionAt(r).def->race == race)
                    out.push_back(id);
            }
            return out;
        }

        // ---- the step interpreter --------------------------------------------------

        int Amount(Step const& st, Ctx const& c) const
        {
            int a = (st.flags & kStepTriggerAmount) ? c.amount : st.a;
            if ((st.flags & kStepSpellPower) && c.spell)
                a += c.spellPower;
            return a;
        }

        int Doubling(Ctx const& c) const
        {
            if (!(c.spell || c.power))
                return 1;
            int const v = SideAura(c.side, AuraKind::DoubleSpellHeal);
            return v > 0 ? 2 : 1;
        }

        int SummonOne(Ctx& c, int side, CardDef const& def, EntityId entity = 0)
        {
            int pos = -1;
            if (side == c.summonSide && c.summonPos >= 0)
                pos = c.summonPos++;
            else if (side == c.side)
            {
                CharRef self = FindChar(c.source);
                if (self.IsMinion() && self.side == side)
                    pos = self.idx + 1;
            }
            EntityId const id = Summon(side, def, pos, entity);
            if (id)
                c.lastSummoned = id;
            return id ? 1 : 0;
        }

        void RunSteps(std::vector<Step> const& steps, Ctx& c)
        {
            for (Step const& st : steps)
            {
                if (phase == Phase::Over)
                    return;
                if (!CondHolds(st.cond, st.condRace, c))
                    continue;
                RunStep(st, c);
                RefreshAuras();
            }
        }

        void RunStep(Step const& st, Ctx& c)
        {
            int const own = c.side;
            int const enemy = 1 - c.side;
            int const amount = Amount(st, c);
            switch (st.op)
            {
                case Op::Damage:
                {
                    int const dmg = amount * Doubling(c);
                    if ((st.flags & kStepRepeatRandom) && IsRandomSel(st.sel))
                    {
                        int hits = st.count < 1 ? 1 : st.count;
                        if ((st.flags & kStepCountSpellPower) && c.spell)
                            hits += c.spellPower;
                        for (int k = 0; k < hits && phase != Phase::Over; ++k)
                        {
                            std::vector<EntityId> t = FilterRace(Select(st.sel, c, 1), st.race);
                            if (t.empty())
                                break;
                            Damage(c.source, FindChar(t[0]), dmg, c.side);
                        }
                        break;
                    }
                    std::vector<EntityId> const targets = FilterRace(Select(st.sel, c, st.count), st.race);
                    for (EntityId id : targets)
                    {
                        CharRef t = FindChar(id);
                        if (!t.Valid())
                            continue;
                        int d = dmg;
                        if (st.flags & kStepRandomBonus)
                            d = (amount + Rand(st.b + 1)) * Doubling(c);
                        Damage(c.source, t, d, c.side);
                    }
                    break;
                }
                case Op::Heal:
                case Op::HealFull:
                {
                    int const heal = amount * Doubling(c);
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.Valid())
                            continue;
                        int const n = st.op == Op::HealFull ? 1000 : heal;
                        Heal(own, c.source, t, n);
                    }
                    break;
                }
                case Op::Destroy:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                        Destroy(FindChar(id), c.source);
                    break;
                case Op::Silence:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (t.IsMinion())
                            Silence(t, c.source);
                    }
                    break;
                case Op::Freeze:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (t.Valid())
                            Freeze(t, c.source);
                    }
                    break;
                case Op::Buff:
                case Op::BuffTemp:
                case Op::HeroAttack:
                {
                    std::vector<EntityId> targets;
                    if (st.op == Op::HeroAttack)
                        targets.push_back(sides[own].heroId);
                    else
                        targets = FilterRace(Select(st.sel, c, st.count), st.race);
                    for (EntityId id : targets)
                    {
                        CharRef t = FindChar(id);
                        if (!t.Valid())
                            continue;
                        if (t.IsHero())
                        {
                            sides[t.side].tempAttack += st.a; // heroes only get temporary attack
                            Emit(EventType::Buff, t.side, c.source, id, st.a);
                            continue;
                        }
                        Minion& m = MinionAt(t);
                        if (st.op == Op::Buff)
                        {
                            m.attack += st.a;
                            m.maxHealth += st.b;
                            if (st.flags & kStepGrantCharge)
                                m.kw |= kKwCharge;
                        }
                        else
                            m.tempAttack += st.a;
                        Emit(EventType::Buff, t.side, c.source, id, st.a);
                    }
                    break;
                }
                case Op::Keyword:
                case Op::KeywordTemp:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (t.IsHero())
                        {
                            if (Has(static_cast<unsigned>(st.a), kKwImmune))
                                sides[t.side].heroImmune = true;
                            Emit(EventType::Buff, t.side, c.source, id, 0);
                            continue;
                        }
                        if (!t.IsMinion())
                            continue;
                        Minion& m = MinionAt(t);
                        if (st.op == Op::Keyword)
                            m.kw |= static_cast<unsigned>(st.a);
                        else if (st.flags & kStepUntilNextTurn)
                        {
                            m.nextKw |= static_cast<unsigned>(st.a);
                            m.nextKwSide = own;
                        }
                        else
                            m.tempKw |= static_cast<unsigned>(st.a);
                        Emit(EventType::Buff, t.side, c.source, id, 0);
                    }
                    break;
                case Op::SetAttack:
                case Op::SetHealth:
                case Op::AttackToHealth:
                case Op::DoubleHealth:
                case Op::DoubleAttack:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        Minion& m = MinionAt(t);
                        int const before = EffAttack(t.side, t.idx);
                        switch (st.op)
                        {
                            case Op::SetAttack:
                                m.attack = st.a;
                                m.tempAttack = 0;
                                break;
                            case Op::SetHealth:
                                m.maxHealth = st.a - m.auraHealth;
                                m.damage = 0;
                                break;
                            case Op::AttackToHealth:
                                m.attack = EffHealth(m);
                                m.tempAttack = 0;
                                break;
                            case Op::DoubleHealth:
                                m.maxHealth += EffHealth(m);
                                break;
                            case Op::DoubleAttack:
                                m.attack += EffAttack(t.side, t.idx);
                                break;
                            default:
                                break;
                        }
                        Emit(EventType::Buff, t.side, c.source, id, EffAttack(t.side, t.idx) - before);
                    }
                    break;
                case Op::Transform:
                case Op::TransformRandomList:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        std::string card = st.card;
                        if (st.op == Op::TransformRandomList && !st.list.empty())
                            card = st.list[static_cast<std::size_t>(Rand(Sz(st.list.size())))];
                        if (CardDef const* def = db->Find(card))
                            Transform(t, *def);
                    }
                    break;
                case Op::Summon:
                {
                    CardDef const* def = db->Find(st.card);
                    if (!def)
                        break;
                    int const side = st.b == 1 ? enemy : own;
                    int const n = st.a < 1 ? 1 : st.a;
                    for (int k = 0; k < n; ++k)
                        SummonOne(c, side, *def);
                    break;
                }
                case Op::SummonRandomList:
                {
                    int const n = st.a < 1 ? 1 : st.a;
                    for (int k = 0; k < n; ++k)
                    {
                        std::vector<std::string> pool;
                        for (std::string const& id : st.list)
                        {
                            bool present = false;
                            if (st.flags & kStepUnique)
                                for (Minion const& m : sides[own].board)
                                    if (m.def->id == id)
                                        present = true;
                            if (!present)
                                pool.push_back(id);
                        }
                        if (pool.empty())
                            break;
                        if (CardDef const* def = db->Find(pool[static_cast<std::size_t>(Rand(Sz(pool.size())))]))
                            SummonOne(c, own, *def);
                    }
                    break;
                }
                case Op::SummonCopy:
                case Op::Resummon:
                    for (EntityId id : Select(st.sel, c, st.count))
                    {
                        Minion const* src = FindAnyMinion(id);
                        if (!src)
                            continue;
                        if (st.op == Op::SummonCopy)
                        {
                            CharRef sr = FindChar(id);
                            int const side = own;
                            if (Sz(sides[side].board.size()) >= kMaxBoard)
                                continue;
                            Minion copy = *src;
                            copy.id = NewId();
                            copy.order = ++playOrder;
                            copy.sick = true;
                            copy.attacks = 0;
                            copy.destroyed = false;
                            copy.auraHealth = 0;
                            copy.returnSide = -1;
                            copy.echo = false;
                            (void)sr;
                            sides[side].board.push_back(copy);
                            Emit(EventType::Summon, side, 0, copy.id, Sz(sides[side].board.size()), copy.def->id);
                            c.lastSummoned = copy.id;
                            RefreshAuras();
                            Queue(On::Summoned, side, InfoOf(copy.id));
                        }
                        else
                        {
                            CardDef const* def = src->def;
                            if (SummonOne(c, own, *def) && st.a > 0)
                            {
                                CharRef r = FindChar(c.lastSummoned);
                                if (r.IsMinion())
                                {
                                    Minion& m = MinionAt(r);
                                    m.damage = std::max(0, EffMaxHealth(m) - st.a);
                                }
                            }
                        }
                    }
                    break;
                case Op::Equip:
                    if (CardDef const* def = db->Find(st.card))
                        Equip(own, *def, 0);
                    break;
                case Op::DestroyWeapon:
                    for (EntityId id : Select(st.sel == Sel::None ? Sel::EnemyHero : st.sel, c))
                    {
                        CharRef h = FindChar(id);
                        if (h.IsHero() && sides[h.side].weapon.id != 0)
                            BreakWeapon(h.side, c.source);
                    }
                    break;
                case Op::WeaponBuff:
                    for (EntityId id : Select(st.sel == Sel::None ? Sel::OwnHero : st.sel, c))
                    {
                        CharRef h = FindChar(id);
                        if (!h.IsHero() || sides[h.side].weapon.id == 0)
                            continue;
                        Weapon& w = sides[h.side].weapon;
                        w.attack += st.a;
                        w.durability += st.b;
                        Emit(EventType::Buff, h.side, c.source, w.id, st.a);
                    }
                    break;
                case Op::Armor:
                    for (EntityId id : Select(st.sel == Sel::None ? Sel::OwnHero : st.sel, c))
                    {
                        CharRef h = FindChar(id);
                        if (h.IsHero())
                            GainArmor(h.side, c.source, amount);
                    }
                    break;
                case Op::Draw:
                    for (EntityId id : Select(st.sel == Sel::None ? Sel::OwnHero : st.sel, c))
                    {
                        CharRef h = FindChar(id);
                        if (h.IsHero())
                            DrawCards(h.side, amount);
                    }
                    break;
                case Op::GainMana:
                    sides[own].mana = std::min(kMaxMana, sides[own].mana + st.a);
                    Emit(EventType::ManaGain, own, c.source, 0, st.a);
                    break;
                case Op::GainCrystals:
                    // sel EnemyHero: the crystals go to the enemy side (Arcane Golem)
                    GainCrystals(st.sel == Sel::EnemyHero ? enemy : own, st.a, st.b == 1, c.source);
                    break;
                case Op::DestroyCrystals:
                    DestroyCrystals(st.b == 1 ? enemy : own, st.a, c.source);
                    break;
                case Op::AddToHand:
                    if (CardDef const* def = db->Find(st.card))
                        for (EntityId id : Select(st.sel == Sel::None ? Sel::OwnHero : st.sel, c))
                        {
                            CharRef h = FindChar(id);
                            if (h.IsHero())
                                for (int k = 0; k < (st.a < 1 ? 1 : st.a); ++k)
                                    AddToHand(h.side, *def, c.source);
                        }
                    break;
                case Op::AddRandomToHand:
                    for (int k = 0; k < (st.a < 1 ? 1 : st.a) && !st.list.empty(); ++k)
                        if (CardDef const* def = db->Find(st.list[static_cast<std::size_t>(Rand(Sz(st.list.size())))]))
                            AddToHand(st.b == 1 ? enemy : own, *def, c.source);
                    break;
                case Op::CopyEnemyHandCard:
                {
                    std::vector<Card> pool = sides[enemy].hand;
                    for (int k = 0; k < (st.a < 1 ? 1 : st.a) && !pool.empty(); ++k)
                    {
                        std::size_t const i = static_cast<std::size_t>(Rand(Sz(pool.size())));
                        AddToHand(own, *pool[i].def, c.source);
                        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                    break;
                }
                case Op::DiscardRandom:
                    DiscardRandom(own, st.a < 1 ? 1 : st.a);
                    break;
                case Op::Bounce:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (t.IsMinion())
                            Bounce(t, st.b);
                    }
                    break;
                case Op::TakeControl:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (t.IsMinion() && t.side != own)
                            TakeControl(t, own);
                    }
                    break;
                case Op::TakeControlRandom:
                {
                    std::vector<EntityId> pick = RandomPick(Living(enemy, false, true), st.a < 1 ? 1 : st.a);
                    for (EntityId id : pick)
                    {
                        CharRef t = FindChar(id);
                        if (t.IsMinion())
                            TakeControl(t, own);
                    }
                    break;
                }
                case Op::Pick:
                {
                    SideState& S = sides[own];
                    S.pick.clear();
                    for (int k = 0; k < st.a && !S.deck.empty(); ++k)
                    {
                        S.pick.push_back(S.deck.back());
                        S.deck.pop_back();
                    }
                    break;
                }
                case Op::DiscountNextSpell:
                    sides[own].spellDiscount += st.a;
                    break;
                case Op::FreeNextSecret:
                    sides[own].freeSecret = true;
                    break;
                case Op::EnemySpellsFreeNext:
                    sides[enemy].spellsFreeNext = true;
                    sides[enemy].spellsFreeNextSeq = ++effectSeq;
                    break;
                case Op::Counter:
                    spellCountered = true;
                    break;
                case Op::RedirectAttack:
                {
                    std::vector<EntityId> t = Select(st.sel, c, 1);
                    if (!t.empty() && attackActive)
                        attackDefender = t[0];
                    break;
                }
                case Op::RedirectSpell:
                {
                    std::vector<EntityId> t = Select(st.sel, c, 1);
                    if (!t.empty() && spellActive)
                        spellTarget = t[0];
                    break;
                }
                case Op::PreventDamage:
                    preventDamage = true;
                    break;
                case Op::DelayedDestroy:
                    for (EntityId id : Select(st.sel, c, st.count))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        if (st.b == 1)
                            MinionAt(t).dieAtEndOfTurn = true;
                        else
                        {
                            MinionAt(t).destroyAtStartOf = own;
                            // Announce the applied enchantment on the resolved target; the later destruction
                            // remains a separate event. A Play target alone can be countered or redirected.
                            Emit(EventType::Buff, t.side, c.source, id, 0);
                        }
                    }
                    break;
                case Op::ReplaceHeroPower:
                    if (CardDef const* def = db->Find(st.card))
                    {
                        SetPower(own, def);
                        sides[own].powerUsed = false;
                    }
                    break;
                case Op::Custom:
                    if (st.custom)
                        RunCustom(st.custom, c);
                    break;
                case Op::SwapWithHandMinion:
                {
                    // Alarm-o-Bot: the hand minion takes Self's slot (summoned: no battlecry, Summoned triggers
                    // fire), Self returns to the hand. The new minion is exhausted like any summoned minion.
                    CharRef self = FindChar(c.source);
                    if (!self.IsMinion() || self.side != own)
                        break;
                    SideState& S = sides[own];
                    std::vector<std::size_t> pool;
                    for (std::size_t i = 0; i < S.hand.size(); ++i)
                        if (S.hand[i].def->type == CardType::Minion)
                            pool.push_back(i);
                    if (pool.empty())
                        break;
                    std::size_t const pick = pool[static_cast<std::size_t>(Rand(Sz(pool.size())))];
                    Card const incoming = S.hand[pick];
                    S.hand.erase(S.hand.begin() + static_cast<std::ptrdiff_t>(pick));
                    int const slot = self.idx;
                    Bounce(self, 0);
                    Summon(own, *incoming.def, slot, incoming.id);
                    break;
                }
                case Op::SummonFromEnemyDeck:
                {
                    // Mindgames: a copy of a random minion of the enemy's deck, else the fallback card.
                    std::vector<CardDef const*> pool;
                    for (Card const& dc : sides[enemy].deck)
                        if (dc.def->type == CardType::Minion)
                            pool.push_back(dc.def);
                    CardDef const* def = nullptr;
                    if (!pool.empty())
                        def = pool[static_cast<std::size_t>(Rand(Sz(pool.size())))];
                    else
                        def = db->Find(st.card);
                    if (def)
                        SummonOne(c, own, *def);
                    break;
                }
                // ---- the rest of Classic ----
                case Op::SpellDamageBuff:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        MinionAt(t).spellDamage += st.a;
                        Emit(EventType::Buff, t.side, c.source, id, 0);
                    }
                    break;
                // ---- Classic engine cards ----
                case Op::GrantScript:
                {
                    Script const* gs = FindScript(st.card);
                    if (!gs)
                        break;
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        Granted g;
                        g.sc = gs;
                        g.side = own;
                        MinionAt(t).grants.push_back(g);
                        Emit(EventType::Buff, t.side, c.source, id, 0);
                    }
                    break;
                }
                case Op::TakeControlTemp:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion() || t.side == own)
                            continue;
                        int const from = t.side;
                        TakeControl(t, own);
                        CharRef now = FindChar(id);
                        if (now.IsMinion() && now.side == own)
                        {
                            Minion& m = MinionAt(now);
                            m.returnSide = from;
                            m.sick = false; // it may attack this turn
                            m.attacks = 0;
                        }
                    }
                    break;
                case Op::ProtectMinions:
                    sides[own].minionFloor = true;
                    break;
                case Op::AddToHandNextTurn:
                    if (CardDef const* def = db->Find(st.card))
                        sides[own].nextTurnCards.push_back(def);
                    break;
                case Op::DrawDiscount:
                {
                    DrawResult const r = DrawOne(own);
                    if (r.kind == 1 && !sides[own].hand.empty())
                        sides[own].hand.back().costMod -= st.a;
                    break;
                }
                case Op::DrawDealCost:
                {
                    std::vector<EntityId> const targets = Select(st.sel, c, st.count);
                    DrawResult const r = DrawOne(own);
                    if (r.kind == 0 || phase == Phase::Over)
                        break;
                    int dmg = CostOf(own, r.card);
                    if ((st.flags & kStepSpellPower) && c.spell)
                        dmg += c.spellPower;
                    dmg *= Doubling(c);
                    for (EntityId id : targets)
                        Damage(c.source, FindChar(id), dmg, c.side);
                    break;
                }
                case Op::SetHeroHealth:
                    for (EntityId id : Select(st.sel, c, st.count))
                    {
                        CharRef h = FindChar(id);
                        if (!h.IsHero())
                            continue;
                        SideState& S = sides[h.side];
                        S.health = st.a;
                        if (S.maxHealth < st.a)
                            S.maxHealth = st.a;
                        Emit(EventType::Buff, h.side, c.source, id, 0);
                    }
                    break;
                case Op::CopyEnemyDeckCards:
                {
                    std::vector<std::size_t> pool;
                    for (std::size_t i = 0; i < sides[enemy].deck.size(); ++i)
                        pool.push_back(i);
                    for (int k = 0; k < (st.a < 1 ? 1 : st.a) && !pool.empty(); ++k)
                    {
                        std::size_t const at = static_cast<std::size_t>(Rand(Sz(pool.size())));
                        AddToHand(own, *sides[enemy].deck[pool[at]].def, c.source);
                        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(at));
                    }
                    break;
                }
                case Op::RemoveKeyword:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        Minion& m = MinionAt(t);
                        unsigned const bit = static_cast<unsigned>(st.a);
                        if (!Has(m.kw | m.tempKw | m.nextKw, st.a))
                            continue;
                        m.kw &= ~bit;
                        m.tempKw &= ~bit;
                        m.nextKw &= ~bit;
                        ++c.removed;
                        if (st.a == kKwDivineShield)
                            Emit(EventType::ShieldPop, t.side, c.source, id, 0);
                        else if (st.a == kKwStealth)
                            Emit(EventType::Stealth, t.side, c.source, id, 0);
                        else
                            Emit(EventType::Buff, t.side, c.source, id, 0);
                    }
                    break;
                case Op::BuffPerRemoved:
                    if (c.removed > 0)
                        for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                        {
                            CharRef t = FindChar(id);
                            if (!t.IsMinion())
                                continue;
                            Minion& m = MinionAt(t);
                            m.attack += st.a * c.removed;
                            m.maxHealth += st.b * c.removed;
                            Emit(EventType::Buff, t.side, c.source, id, st.a * c.removed);
                        }
                    break;
                case Op::DestroyEnemySecrets:
                {
                    std::vector<SecretCard> const gone = sides[enemy].secrets;
                    sides[enemy].secrets.clear();
                    for (SecretCard const& sec : gone)
                        Emit(EventType::Reveal, enemy, c.source, sec.id, 0, sec.def->id);
                    break;
                }
                case Op::CopySubjectToOtherHand:
                    if (c.subjectCard && c.subjectSide >= 0 && c.subjectCard->id != "?")
                        AddToHand(1 - c.subjectSide, *c.subjectCard, c.source);
                    break;
                case Op::SwapAttackHealth:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        Minion& m = MinionAt(t);
                        int const atk = EffAttack(t.side, t.idx);
                        int const hp = EffHealth(m);
                        // Health first (undamaged, so an Enrage switches off), then the Attack is measured again and
                        // set to the old Health; temporary Attack is replaced too.
                        m.maxHealth = atk - m.auraHealth;     // the Health now equals the old Attack
                        m.damage = 0;
                        m.tempAttack = 0;
                        m.attack += hp - EffAttack(t.side, t.idx); // the Attack now equals the old Health
                        Emit(EventType::Buff, t.side, c.source, id, hp - atk);
                    }
                    break;
                case Op::DamageFromTarget:
                {
                    CharRef src = FindChar(c.target);
                    if (!src.IsMinion())
                        break;
                    int const dmg = EffAttack(src.side, src.idx);
                    for (EntityId id : Select(st.sel, c, st.count))
                        Damage(c.target, FindChar(id), dmg);
                    break;
                }
                case Op::DiscardHand:
                {
                    SideState& S = sides[own];
                    std::vector<Card> const gone = S.hand;
                    S.hand.clear();
                    for (Card const& dc : gone)
                        Emit(EventType::Discard, own, 0, dc.id, 0, dc.def->id);
                    break;
                }
                case Op::TakeRandomFromDeck:
                {
                    SideState& S = sides[own];
                    std::vector<std::size_t> pool;
                    for (std::size_t i = 0; i < S.deck.size(); ++i)
                    {
                        CardDef const& d = *S.deck[i].def;
                        if (d.type == CardType::Minion && (st.race.empty() || d.race == st.race))
                            pool.push_back(i);
                    }
                    if (pool.empty())
                        break;
                    std::size_t const at = pool[static_cast<std::size_t>(Rand(Sz(pool.size())))];
                    Card const card = S.deck[at];
                    S.deck.erase(S.deck.begin() + static_cast<std::ptrdiff_t>(at));
                    if (Sz(S.hand.size()) >= kMaxHand)
                        Emit(EventType::Burn, own, 0, card.id, 0, card.def->id);
                    else
                    {
                        S.hand.push_back(card);
                        Emit(EventType::Draw, own, 0, card.id, 0, card.def->id).hideFromOther = true;
                    }
                    break;
                }
                case Op::ReplaceHero:
                    if (CardDef const* def = db->Find(st.card))
                    {
                        // Lord Jaraxxus: a new hero with its own health (armor is lost; weapon and hero power are
                        // replaced by the following steps of the script).
                        SideState& S = sides[own];
                        S.heroCardId = def->id;
                        S.health = S.maxHealth = def->health > 0 ? def->health : kHeroHealth;
                        S.armor = 0;
                        S.heroFrozen = false;
                        S.heroFreezeKeep = false;
                        Emit(EventType::Transform, own, c.source, S.heroId, 0, def->id);
                    }
                    break;
                case Op::RemoveFromPlay:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        Minion const gone = MinionAt(t);
                        sides[t.side].board.erase(sides[t.side].board.begin() + t.idx);
                        graveyard.push_back(gone); // Subject lookups of the card's Played trigger still work
                        Emit(EventType::Death, t.side, 0, gone.id, t.idx + 1, gone.def->id);
                        RefreshAuras();
                    }
                    break;
                // ---- Naxxramas ----
                case Op::Settle:
                    Settle();
                    break;
                case Op::DestroyReplace:
                {
                    struct Replacement
                    {
                        int side;
                        CardDef const* def;
                    };
                    std::vector<Replacement> reps;
                    CardDef const* fixed = st.card.empty() ? nullptr : db->Find(st.card);
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion())
                            continue;
                        CardDef const* def = fixed ? fixed : MinionAt(t).def;
                        reps.push_back(Replacement{t.side, def});
                        Destroy(t, c.source);
                    }
                    // The deaths and their Deathrattles come first; the replacements fill what is left of the boards.
                    Settle();
                    for (Replacement const& r : reps)
                        if (phase != Phase::Over)
                            Summon(r.side, *r.def, -1, 0);
                    break;
                }
                case Op::SummonFromHand:
                {
                    SideState& S = sides[own];
                    if (Sz(S.board.size()) >= kMaxBoard)
                        break;
                    std::vector<std::size_t> pool;
                    for (std::size_t i = 0; i < S.hand.size(); ++i)
                        if (S.hand[i].def->type == CardType::Minion && (st.race.empty() || S.hand[i].def->race == st.race))
                            pool.push_back(i);
                    if (pool.empty())
                        break;
                    std::size_t const pick = pool[static_cast<std::size_t>(Rand(Sz(pool.size())))];
                    Card const card = S.hand[pick];
                    S.hand.erase(S.hand.begin() + static_cast<std::ptrdiff_t>(pick));
                    SummonOne(c, own, *card.def, card.id);
                    break;
                }
                case Op::SummonFromDeck:
                {
                    int const side = st.b == 1 ? enemy : own;
                    SideState& S = sides[side];
                    if (Sz(S.board.size()) >= kMaxBoard)
                        break;
                    std::vector<std::size_t> pool;
                    for (std::size_t i = 0; i < S.deck.size(); ++i)
                        if (S.deck[i].def->type == CardType::Minion && (st.race.empty() || S.deck[i].def->race == st.race))
                            pool.push_back(i);
                    if (pool.empty())
                        break;
                    std::size_t const at = pool[static_cast<std::size_t>(Rand(Sz(pool.size())))];
                    Card const card = S.deck[at];
                    S.deck.erase(S.deck.begin() + static_cast<std::ptrdiff_t>(at));
                    SummonOne(c, side, *card.def, card.id);
                    break;
                }
                case Op::SecretFromDeck:
                {
                    SideState& S = sides[own];
                    if (Sz(S.secrets.size()) >= kMaxSecrets)
                        break;
                    std::vector<std::size_t> pool;
                    for (std::size_t i = 0; i < S.deck.size(); ++i)
                    {
                        CardDef const& d = *S.deck[i].def;
                        if (d.type != CardType::Spell || !d.secret)
                            continue;
                        bool inPlay = false;
                        for (SecretCard const& sec : S.secrets)
                            if (sec.def->id == d.id)
                                inPlay = true;
                        if (!inPlay)
                            pool.push_back(i);
                    }
                    if (pool.empty())
                        break;
                    std::size_t const at = pool[static_cast<std::size_t>(Rand(Sz(pool.size())))];
                    Card const card = S.deck[at];
                    S.deck.erase(S.deck.begin() + static_cast<std::ptrdiff_t>(at));
                    SecretCard sec;
                    sec.id = card.id;
                    sec.def = card.def;
                    sec.sc = FindScript(card.def->id);
                    sec.order = ++playOrder;
                    S.secrets.push_back(sec);
                    Emit(EventType::Secret, own, card.id, card.id, 0, card.def->id).hideFromOther = true;
                    break;
                }
                case Op::SpellTaxEnemy:
                    sides[enemy].spellTaxNext += st.a;
                    sides[enemy].spellTaxNextSeq = ++effectSeq;
                    break;
                // ---- Naxxramas, wave 2 ----
                case Op::EchoMark:
                    for (EntityId id : Select(st.sel == Sel::None ? Sel::Self : st.sel, c))
                    {
                        CharRef t = FindChar(id);
                        if (t.IsMinion())
                            MinionAt(t).echo = true;
                    }
                    break;
                case Op::EchoCopy:
                {
                    CharRef t = FindChar(c.source);
                    if (!t.IsMinion() || !Alive(t) || !MinionAt(t).echo)
                        break;
                    MinionAt(t).echo = false; // spent, with or without room for the copy
                    int const side = t.side;
                    if (Sz(sides[side].board.size()) >= kMaxBoard)
                        break;
                    Minion copy = MinionAt(t);
                    copy.id = NewId();
                    copy.order = ++playOrder;
                    copy.sick = true;
                    copy.attacks = 0;
                    copy.destroyed = false;
                    copy.auraHealth = 0;
                    copy.returnSide = -1;
                    copy.echo = false;
                    // An effect that kills the Ooze at the end of the turn (Power Overwhelming) is carried over, but the
                    // copy is spared this end of turn: it dies at the end of the opponent's turn (hearthstone.wiki.gg).
                    copy.dieSkipOnce = copy.dieAtEndOfTurn;
                    int const at = t.idx + 1;
                    sides[side].board.insert(sides[side].board.begin() + at, copy);
                    Emit(EventType::Summon, side, c.source, copy.id, at + 1, copy.def->id);
                    c.lastSummoned = copy.id;
                    RefreshAuras();
                    Queue(On::Summoned, side, InfoOf(copy.id));
                    break;
                }
                case Op::TakeControlCard:
                    for (EntityId id : Select(Sel::EnemyMinions, c))
                    {
                        CharRef t = FindChar(id);
                        if (!t.IsMinion() || MinionAt(t).def->id != st.card || Sz(sides[own].board.size()) >= kMaxBoard)
                            continue;
                        TakeControl(t, own);
                    }
                    break;
                case Op::DestroyUnfrozen:
                    for (EntityId id : FilterRace(Select(st.sel, c, st.count), st.race))
                    {
                        CharRef t = FindChar(id);
                        if (t.IsMinion() && !MinionAt(t).frozen && !Has(Keywords(t.side, t.idx), kKwFrostProof))
                            Destroy(t, c.source);
                    }
                    break;
                case Op::SummonDied:
                {
                    CharRef t = FindChar(c.source);
                    if (!t.IsMinion() || !Alive(t))
                        break;
                    std::vector<CardDef const*> const list = sides[own].diedThisTurn; // the summons add no deaths
                    for (CardDef const* def : list)
                    {
                        if (phase == Phase::Over || Sz(sides[own].board.size()) >= kMaxBoard)
                            break;
                        Summon(own, *def, -1, 0);
                    }
                    break;
                }
            }
        }

        void RunCustom(CustomFn fn, Ctx& c);

        // ---- turn flow ----------------------------------------------------------

        void StartTurn(int side)
        {
            if (turn + 1 > kMaxTurns)
            {
                EndMatch(-1, EndReason::TurnLimit);
                return;
            }
            ++turn;
            active = side;
            SideState& S = sides[side];
            S.maxMana = std::min(kMaxMana, S.maxMana + 1);
            S.overloadLocked = S.overloadOwed;
            S.overloadOwed = 0;
            S.mana = std::max(0, S.maxMana - S.overloadLocked);
            S.tempAttack = 0;
            S.heroAttacks = 0;
            S.powerUsed = false;
            ++S.turnsStarted;
            S.cardsPlayed = 0;
            S.minionsPlayed = 0;
            S.spellsFree = S.spellsFreeNext;
            S.spellsFreeNext = false;
            S.spellsFreeSeq = S.spellsFreeNextSeq;
            S.spellsFreeNextSeq = 0;
            S.spellTax = S.spellTaxNext;
            S.spellTaxNext = 0;
            S.spellTaxSeq = S.spellTaxNextSeq;
            S.spellTaxNextSeq = 0;
            sides[0].diedThisTurn.clear();
            sides[1].diedThisTurn.clear();
            for (int s = 0; s < 2; ++s)
                for (Minion& m : sides[s].board)
                    if (m.nextKwSide == side)
                    {
                        m.nextKw = 0;
                        m.nextKwSide = -1;
                    }
            for (Minion& m : S.board)
            {
                m.sick = false;
                m.attacks = 0;
            }
            Emit(EventType::TurnStart, side, 0, 0, turn);
            // Delayed destruction (Corruption) of minions marked by this side.
            for (int s = 0; s < 2; ++s)
                for (Minion& m : sides[s].board)
                    if (m.destroyAtStartOf == side && !m.destroyed)
                    {
                        m.destroyed = true;
                        m.destroyAtStartOf = -1;
                        Emit(EventType::Destroy, s, 0, m.id, 0);
                    }
            // Cards promised for this turn (Headcrack's Combo) arrive before the start-of-turn triggers.
            {
                std::vector<CardDef const*> const promised = S.nextTurnCards;
                S.nextTurnCards.clear();
                for (CardDef const* def : promised)
                    AddToHand(side, *def, 0);
            }
            SubjectInfo none;
            none.side = side;
            Queue(On::TurnStart, side, none);
            Settle();
            if (phase == Phase::Over)
                return;
            // Encounters (SideStart::noDraw): the turn-start draw is skipped; effect draws are not.
            if (!S.noDraw)
                DrawCards(side, 1);
            Settle();
            if (phase == Phase::Over)
                return;
            // The forced second phase of a boss (before the hero power, which it replaces), then the
            // hero power that is used by itself.
            if (!S.phaseDone && S.power)
                if (Script const* ps = FindScript(S.power->id))
                    if (ps->phase.present && S.turnsStarted >= ps->phase.forcedTurn)
                        PhaseTwo(side);
            if (phase != Phase::Over)
                AutoCastPower(side);
        }

        // The hero power of side is used by the engine at the start of its turn (Script::autoCast): it costs what the card
        // says, is skipped when it is not affordable or its play requirement fails, and is announced like any use.
        void AutoCastPower(int side)
        {
            SideState& S = sides[side];
            if (!S.power || S.powerUsed || phase == Phase::Over)
                return;
            Script const* sc = FindScript(S.power->id);
            if (!sc || !sc->autoCast || S.mana < S.power->cost || CheckReqs(side, *sc) != ActionError::None)
                return;
            S.mana -= S.power->cost;
            S.powerUsed = true;
            Emit(EventType::HeroPower, side, S.powerId, 0, 0, S.power->id);
            Ctx ctx;
            ctx.side = side;
            ctx.source = S.powerId;
            ctx.power = true;
            RunSteps(sc->play, ctx);
            Settle();
        }

        // The second phase of side's hero power (Script::phase), once: the hero power is replaced by the next one (fresh,
        // usable at once), the guardians join the right end of the board (as many as fit) and the event log says so.
        void PhaseTwo(int side)
        {
            SideState& S = sides[side];
            S.phasePending = false;
            if (S.phaseDone || !S.power)
                return;
            Script const* ps = FindScript(S.power->id);
            CardDef const* next = (ps && ps->phase.present) ? db->Find(ps->phase.powerId) : nullptr;
            CardDef const* guard = (ps && ps->phase.present) ? db->Find(ps->phase.summonId) : nullptr;
            if (!next)
                return;
            S.phaseDone = true;
            int const count = ps->phase.summons;
            Emit(EventType::Trigger, side, S.heroId, S.heroId, 0, next->id);
            SetPower(side, next);
            S.powerUsed = false;
            for (int k = 0; k < count && guard && phase != Phase::Over; ++k)
                Summon(side, *guard, -1, 0);
            Settle();
        }

        // After every action: a boss whose Armor just broke starts its second phase. When that happened during the
        // opponent's turn, that turn ends, but only once nothing is left pending (a pick), and never twice.
        void CheckPhases()
        {
            for (int side = 0; side < 2 && phase != Phase::Over; ++side)
                if (sides[side].phasePending)
                {
                    PhaseTwo(side);
                    if (side != active && phase == Phase::Play)
                        turnEndPending = true;
                }
            if (turnEndPending && phase == Phase::Play && sides[0].pick.empty() && sides[1].pick.empty())
            {
                turnEndPending = false;
                EndTurn(active);
            }
            else if (phase == Phase::Over)
                turnEndPending = false;
        }

        void StartPlay()
        {
            phase = Phase::Play;
            int const second = 1 - first;
            CardDef const* coin = db->Find(kCoinId);
            Card c;
            c.id = NewId();
            c.def = coin;
            sides[second].hand.push_back(c);
            Emit(EventType::Draw, second, 0, c.id, 0, coin->id).hideFromOther = true;
            StartTurn(first);
        }

        // Shadow Madness: minions borrowed for the turn go back to their owner (destroyed when the board is full).
        void ReturnBorrowed()
        {
            for (int s = 0; s < 2; ++s)
            {
                std::vector<Minion>& board = sides[s].board;
                for (std::size_t i = 0; i < board.size();)
                {
                    int const to = board[i].returnSide;
                    if (to < 0)
                    {
                        ++i;
                        continue;
                    }
                    if (to == s)
                    {
                        board[i].returnSide = -1;
                        ++i;
                        continue;
                    }
                    if (Sz(sides[to].board.size()) >= kMaxBoard)
                    {
                        // no room at home: it is destroyed (the next death step removes it)
                        board[i].returnSide = -1;
                        board[i].destroyed = true;
                        Emit(EventType::Destroy, s, 0, board[i].id, 0);
                        ++i;
                        continue;
                    }
                    Minion m = board[i];
                    m.returnSide = -1;
                    board.erase(board.begin() + static_cast<std::ptrdiff_t>(i));
                    sides[to].board.push_back(m);
                    Emit(EventType::Control, to, 0, m.id, 0, m.def->id);
                }
            }
            RefreshAuras();
        }

        void EndTurn(int side)
        {
            SubjectInfo none;
            none.side = side;
            Queue(On::TurnEnd, side, none);
            Settle();
            if (phase == Phase::Over)
                return;
            for (int s = 0; s < 2; ++s)
                for (Minion& m : sides[s].board)
                    if (m.dieSkipOnce)
                        m.dieSkipOnce = false;
                    else if (m.dieAtEndOfTurn && !m.destroyed)
                    {
                        m.destroyed = true;
                        Emit(EventType::Destroy, s, 0, m.id, 0);
                    }
            Settle();
            if (phase == Phase::Over)
                return;
            ReturnBorrowed();
            Settle();
            if (phase == Phase::Over)
                return;
            // Temporary effects end for everyone.
            for (int s = 0; s < 2; ++s)
            {
                sides[s].tempAttack = 0;
                sides[s].heroImmune = false;
                sides[s].minionFloor = false;
                for (Minion& m : sides[s].board)
                {
                    m.tempAttack = 0;
                    m.tempKw = 0;
                }
            }
            SideState& S = sides[side];
            S.spellDiscount = 0;
            S.freeSecret = false;
            S.spellsFree = false;
            S.spellTax = 0;
            // Thaw: frozen characters of this side thaw unless frozen after they attacked this turn.
            for (Minion& m : S.board)
            {
                if (!m.frozen)
                    continue;
                if (m.sc && m.sc->permFrozen && !m.silenced)
                    continue; // Frozen Champion
                if (m.freezeKeep)
                    m.freezeKeep = false;
                else
                    m.frozen = false;
            }
            if (S.heroFrozen)
            {
                if (S.heroFreezeKeep)
                    S.heroFreezeKeep = false;
                else
                    S.heroFrozen = false;
            }
            RefreshAuras();
            StartTurn(1 - side);
        }

        // ---- validation (shared by Apply and LegalActions) ---------------------

        ActionError Gate(int side) const
        {
            if (phase == Phase::Over)
                return ActionError::Over;
            if (phase != Phase::Play)
                return ActionError::Phase;
            if (!sides[side].pick.empty())
                return ActionError::Pending;
            if (!sides[1 - side].pick.empty())
                return ActionError::Turn;
            if (active != side)
                return ActionError::Turn;
            return ActionError::None;
        }

        // srcKind: 0 spell, 1 hero power, 2 battlecry / other
        bool TargetOk(int side, TargetRule const& rule, CharRef t, int srcKind, EntityId self) const
        {
            if (!Alive(t))
                return false;
            bool const enemy = t.side != side;
            switch (rule.kind)
            {
                case TargetKind::None: return false;
                case TargetKind::AnyCharacter: break;
                case TargetKind::AnyMinion:
                    if (!t.IsMinion())
                        return false;
                    break;
                case TargetKind::AnyHero:
                    if (!t.IsHero())
                        return false;
                    break;
                case TargetKind::FriendlyCharacter:
                    if (enemy)
                        return false;
                    break;
                case TargetKind::FriendlyMinion:
                    if (enemy || !t.IsMinion())
                        return false;
                    break;
                case TargetKind::EnemyCharacter:
                    if (!enemy)
                        return false;
                    break;
                case TargetKind::EnemyMinion:
                case TargetKind::EnemyDamagedMinion:
                    if (!enemy || !t.IsMinion())
                        return false;
                    if (rule.kind == TargetKind::EnemyDamagedMinion && MinionAt(t).damage <= 0)
                        return false;
                    break;
            }
            if (t.IsMinion())
            {
                Minion const& m = MinionAt(t);
                unsigned const kw = Keywords(t.side, t.idx);
                if (enemy && Has(kw, kKwStealth))
                    return false;
                if (enemy && Has(kw, kKwImmune))
                    return false;
                if ((srcKind == 0 || srcKind == 1) && m.def->elusive && !m.silenced)
                    return false;
                if ((rule.flags & kTgtDamaged) && m.damage <= 0)
                    return false;
                if ((rule.flags & kTgtUndamaged) && m.damage > 0)
                    return false;
                if ((rule.flags & kTgtMaxAttack) && EffAttack(t.side, t.idx) > rule.value)
                    return false;
                if ((rule.flags & kTgtMinAttack) && EffAttack(t.side, t.idx) < rule.value)
                    return false;
                if ((rule.flags & kTgtRace) && m.def->race != rule.race)
                    return false;
                if ((rule.flags & kTgtNonSelf) && m.id == self)
                    return false;
                if ((rule.flags & kTgtTaunt) && !Has(kw, kKwTaunt))
                    return false;
                if ((rule.flags & kTgtRoomToSteal) && enemy && Sz(sides[side].board.size()) + (srcKind == 2 ? 1 : 0) >= kMaxBoard)
                    return false;
            }
            else
            {
                if (enemy && HeroImmune(t.side))
                    return false;
                if (rule.flags & (kTgtRace | kTgtMaxAttack | kTgtMinAttack | kTgtTaunt))
                    return false;
                if ((rule.flags & kTgtDamaged) && sides[t.side].health >= sides[t.side].maxHealth)
                    return false;
                if ((rule.flags & kTgtUndamaged) && sides[t.side].health < sides[t.side].maxHealth)
                    return false;
            }
            return true;
        }

        bool AnyTarget(int side, TargetRule const& rule, int srcKind, EntityId self) const
        {
            for (int s = 0; s < 2; ++s)
            {
                if (TargetOk(side, rule, Hero(s), srcKind, self))
                    return true;
                for (int i = 0; i < Sz(sides[s].board.size()); ++i)
                {
                    CharRef r;
                    r.side = s;
                    r.idx = i;
                    if (TargetOk(side, rule, r, srcKind, self))
                        return true;
                }
            }
            return false;
        }

        ActionError ValidateTarget(int side, TargetRule const& rule, EntityId target, int srcKind, EntityId self) const
        {
            if (rule.kind == TargetKind::None)
                return target == 0 ? ActionError::None : ActionError::Target;
            if (target == 0)
            {
                if ((rule.flags & kTgtOptional) && !AnyTarget(side, rule, srcKind, self))
                    return ActionError::None;
                return ActionError::Target;
            }
            return TargetOk(side, rule, FindChar(target), srcKind, self) ? ActionError::None : ActionError::Target;
        }

        bool MissingTotem(int side) const
        {
            for (std::string const& t : TotemIds())
            {
                bool present = false;
                for (Minion const& m : sides[side].board)
                    if (m.def->id == t)
                        present = true;
                if (!present)
                    return true;
            }
            return false;
        }

        ActionError CheckReqs(int side, Script const& sc) const
        {
            SideState const& S = sides[side];
            if ((sc.reqs & kReqWeapon) && S.weapon.id == 0)
                return ActionError::Requirement;
            if ((sc.reqs & kReqMinEnemyMinions) && Sz(Living(1 - side, false, true).size()) < sc.reqValue)
                return ActionError::Requirement;
            if ((sc.reqs & kReqMinTotalMinions) &&
                Sz(Living(0, false, true).size() + Living(1, false, true).size()) < sc.reqValue)
                return ActionError::Requirement;
            if ((sc.reqs & kReqFreeSlot) && Sz(S.board.size()) >= kMaxBoard)
                return ActionError::BoardFull;
            if ((sc.reqs & kReqMissingTotem) && !MissingTotem(side))
                return ActionError::Requirement;
            return ActionError::None;
        }

        Card const* HandCard(int side, EntityId id) const
        {
            for (Card const& c : sides[side].hand)
                if (c.id == id)
                    return &c;
            return nullptr;
        }

        // The script whose target / steps a play of card uses (the option's for Choose One).
        Script const* PlayScript(Card const& card, int choice) const
        {
            Script const* sc = FindScript(card.def->id);
            if (sc && sc->chooseOne() && choice >= 1 && choice <= 2)
                return FindScript(sc->options[choice - 1]);
            return sc;
        }

        bool ComboActive(int side) const { return sides[side].cardsPlayed > 0; }

        TargetRule const* PlayRule(int side, Script const* sc) const
        {
            static TargetRule const none;
            if (!sc)
                return &none;
            if (ComboActive(side) && sc->hasComboTarget)
                return &sc->comboTarget;
            return &sc->target;
        }

        ActionError ValidatePlay(int side, EntityId source, EntityId target, int position, int choice) const
        {
            ActionError e = Gate(side);
            if (e != ActionError::None)
                return e;
            SideState const& S = sides[side];
            Card const* card = source ? HandCard(side, source) : nullptr;
            if (!card)
                return ActionError::Source;
            CardDef const& def = *card->def;
            if (def.type != CardType::Spell && def.type != CardType::Minion && def.type != CardType::Weapon)
                return ActionError::Unsupported;
            Script const* base = FindScript(def.id);
            if (!base && def.type == CardType::Spell)
                return ActionError::Unsupported;
            if (base && base->chooseOne())
            {
                if (choice < 1 || choice > 2 || !FindScript(base->options[choice - 1]))
                    return ActionError::Choice;
            }
            else if (choice != 0)
                return ActionError::Choice;
            if (S.mana < CostOf(side, *card))
                return ActionError::Mana;
            if (def.type == CardType::Minion)
            {
                if (Sz(S.board.size()) >= kMaxBoard)
                    return ActionError::BoardFull;
                if (position < -1 || position > Sz(S.board.size()))
                    return ActionError::Position;
            }
            if (def.secret)
            {
                if (Sz(S.secrets.size()) >= kMaxSecrets)
                    return ActionError::Secret;
                for (SecretCard const& s : S.secrets)
                    if (s.def->id == def.id)
                        return ActionError::Secret;
            }
            if (base)
            {
                e = CheckReqs(side, *base);
                if (e != ActionError::None)
                    return e;
            }
            Script const* sc = PlayScript(*card, choice);
            if (sc && sc != base)
            {
                e = CheckReqs(side, *sc);
                if (e != ActionError::None)
                    return e;
            }
            int const srcKind = def.type == CardType::Spell ? 0 : 2;
            return ValidateTarget(side, *PlayRule(side, sc), target, srcKind, 0);
        }

        ActionError ValidateAttack(int side, EntityId source, EntityId target) const
        {
            ActionError e = Gate(side);
            if (e != ActionError::None)
                return e;
            SideState const& S = sides[side];
            if (source == 0)
                return ActionError::Source;
            if (source == S.heroId)
            {
                if (!HeroCanAttack(side))
                    return ActionError::Attack;
            }
            else
            {
                CharRef r = FindChar(source);
                if (!r.IsMinion() || r.side != side)
                    return ActionError::Source;
                if (!MinionCanAttack(side, r.idx))
                    return ActionError::Attack;
            }
            CharRef t = FindChar(target);
            if (!Alive(t) || t.side != 1 - side)
                return ActionError::Target;
            if (IsImmune(t))
                return ActionError::Target;
            if (t.IsMinion() && Has(Keywords(t.side, t.idx), kKwStealth))
                return ActionError::Target;
            if (HasTaunt(1 - side) && !(t.IsMinion() && Has(Keywords(t.side, t.idx), kKwTaunt)))
                return ActionError::Taunt;
            return ActionError::None;
        }

        ActionError ValidateHeroPower(int side, EntityId source, EntityId target) const
        {
            ActionError e = Gate(side);
            if (e != ActionError::None)
                return e;
            SideState const& S = sides[side];
            if (source == 0 || source != S.powerId)
                return ActionError::Source;
            if (S.powerUsed)
                return ActionError::Used;
            if (S.mana < S.power->cost)
                return ActionError::Mana;
            Script const* sc = FindScript(S.power->id);
            if (!sc)
                return ActionError::Unsupported;
            if (sc->passive || sc->autoCast)
                return ActionError::Requirement; // a passive or automatic hero power is never an action
            e = CheckReqs(side, *sc);
            if (e != ActionError::None)
                return e;
            return ValidateTarget(side, sc->target, target, 1, 0);
        }

        ActionError ValidateMulligan(int side, Action const& a) const
        {
            if (phase == Phase::Over)
                return ActionError::Over;
            if (phase != Phase::Mulligan)
                return ActionError::Phase;
            SideState const& S = sides[side];
            if (S.mulliganDone)
                return ActionError::Mulligan;
            for (std::size_t i = 0; i < a.replace.size(); ++i)
            {
                EntityId const id = a.replace[i];
                if (std::find(S.offered.begin(), S.offered.end(), id) == S.offered.end())
                    return ActionError::Mulligan;
                for (std::size_t j = 0; j < i; ++j)
                    if (a.replace[j] == id)
                        return ActionError::Mulligan;
            }
            return ActionError::None;
        }

        ActionError ValidateChoose(int side, Action const& a) const
        {
            if (phase == Phase::Over)
                return ActionError::Over;
            if (phase != Phase::Play)
                return ActionError::Phase;
            SideState const& S = sides[side];
            if (S.pick.empty())
                return ActionError::Pending;
            for (Card const& c : S.pick)
                if (c.id == a.source && a.source != 0)
                    return ActionError::None;
            return ActionError::Source;
        }

        ActionError Validate(int side, Action const& a) const
        {
            if (side < 0 || side > 1)
                return ActionError::Turn;
            if (a.type != ActionType::Play && a.choice != 0)
                return ActionError::Choice;
            switch (a.type)
            {
                case ActionType::Mulligan: return ValidateMulligan(side, a);
                case ActionType::Play: return ValidatePlay(side, a.source, a.target, a.position, a.choice);
                case ActionType::Attack: return ValidateAttack(side, a.source, a.target);
                case ActionType::HeroPower: return ValidateHeroPower(side, a.source, a.target);
                case ActionType::EndTurn: return Gate(side);
                case ActionType::Concede: return phase == Phase::Over ? ActionError::Over : ActionError::None;
                case ActionType::Choose: return ValidateChoose(side, a);
            }
            return ActionError::Source;
        }

        static Action MakeAction(ActionType type, EntityId source, EntityId target, int choice = 0)
        {
            Action a;
            a.type = type;
            a.source = source;
            a.target = target;
            a.position = -1;
            a.choice = choice;
            return a;
        }

        std::vector<Action> Legal(int side) const
        {
            std::vector<Action> out;
            if (Gate(side) != ActionError::None)
                return out;
            SideState const& S = sides[side];
            std::vector<EntityId> all;
            for (int s = 0; s < 2; ++s)
            {
                all.push_back(sides[s].heroId);
                for (Minion const& m : sides[s].board)
                    all.push_back(m.id);
            }
            std::sort(all.begin(), all.end());

            for (Card const& c : S.hand)
            {
                Script const* base = FindScript(c.def->id);
                int const choices = (base && base->chooseOne()) ? 2 : 0;
                for (int ch = (choices ? 1 : 0); ch <= choices; ++ch)
                {
                    if (ValidatePlay(side, c.id, 0, -1, ch) == ActionError::None)
                    {
                        out.push_back(MakeAction(ActionType::Play, c.id, 0, ch));
                        continue;
                    }
                    Script const* sc = PlayScript(c, ch);
                    if (PlayRule(side, sc)->kind == TargetKind::None)
                        continue;
                    for (EntityId t : all)
                        if (ValidatePlay(side, c.id, t, -1, ch) == ActionError::None)
                            out.push_back(MakeAction(ActionType::Play, c.id, t, ch));
                }
            }

            auto addAttacks = [&](EntityId source) {
                for (EntityId t : all)
                    if (ValidateAttack(side, source, t) == ActionError::None)
                        out.push_back(MakeAction(ActionType::Attack, source, t));
            };
            if (HeroCanAttack(side))
                addAttacks(S.heroId);
            for (int i = 0; i < Sz(S.board.size()); ++i)
                if (MinionCanAttack(side, i))
                    addAttacks(S.board[static_cast<std::size_t>(i)].id);

            if (S.powerId != 0 && S.power && !S.powerUsed && S.mana >= S.power->cost)
            {
                if (ValidateHeroPower(side, S.powerId, 0) == ActionError::None)
                    out.push_back(MakeAction(ActionType::HeroPower, S.powerId, 0));
                else
                {
                    Script const* sc = FindScript(S.power->id);
                    if (sc && sc->target.kind != TargetKind::None)
                        for (EntityId t : all)
                            if (ValidateHeroPower(side, S.powerId, t) == ActionError::None)
                                out.push_back(MakeAction(ActionType::HeroPower, S.powerId, t));
                }
            }
            return out;
        }

        // ---- execution of validated actions --------------------------------------

        void BeginAction()
        {
            resolutions = 0;
            graveyard.clear();
            queue.clear();
        }

        void ExecMulligan(int side, Action const& a)
        {
            SideState& S = sides[side];
            std::vector<Card> back;
            std::vector<Card> keep;
            for (Card const& c : S.hand)
            {
                if (std::find(a.replace.begin(), a.replace.end(), c.id) != a.replace.end())
                    back.push_back(c);
                else
                    keep.push_back(c);
            }
            S.hand = keep;
            for (Card const& c : back)
            {
                std::size_t const pos = static_cast<std::size_t>(Rand(Sz(S.deck.size()) + 1));
                S.deck.insert(S.deck.begin() + static_cast<std::ptrdiff_t>(pos), c);
            }
            Emit(EventType::Mulligan, side, 0, 0, Sz(back.size()));
            S.mulliganDone = true;
            S.offered.clear();
            DrawNoFatigue(side, Sz(back.size()));
            if (sides[0].mulliganDone && sides[1].mulliganDone)
                StartPlay();
        }

        void ExecPlay(int side, Action const& a)
        {
            SideState& S = sides[side];
            std::size_t ci = 0;
            while (S.hand[ci].id != a.source)
                ++ci;
            Card const card = S.hand[ci];
            int const cost = CostOf(side, card);
            S.hand.erase(S.hand.begin() + static_cast<std::ptrdiff_t>(ci));
            CardDef const& def = *card.def;
            S.mana -= cost;
            bool const combo = ComboActive(side);
            ++S.cardsPlayed;
            if (def.type == CardType::Minion)
            {
                ++S.tally.minionsPlayed;
                if (def.race == "BEAST")
                    ++S.tally.beastsPlayed;
            }
            if (def.type == CardType::Spell)
                ++S.tally.spellsPlayed;
            if (cost >= 7)
                ++S.tally.expensivePlayed;
            if (def.type == CardType::Minion)
                ++S.minionsPlayed;
            if (def.type == CardType::Spell)
            {
                S.spellDiscount = 0;
                if (def.secret)
                    S.freeSecret = false;
            }
            Script const* base = FindScript(def.id);
            Script const* sc = PlayScript(card, a.choice);
            Event& pe = Emit(EventType::Play, side, card.id, a.target, a.choice, def.id);
            if (def.secret)
                pe.hideFromOther = true;
            int const overload = KeywordsOf(def).overload;
            if (overload > 0)
            {
                S.overloadOwed += overload;
                Emit(EventType::Overload, side, card.id, 0, overload);
            }

            Ctx ctx;
            ctx.side = side;
            ctx.source = card.id;
            ctx.target = a.target;
            ctx.spell = def.type == CardType::Spell;
            ctx.spellPower = ctx.spell ? SpellPower(side) : 0;
            std::vector<Step> const* steps = nullptr;
            if (sc)
                steps = (combo && !sc->combo.empty()) ? &sc->combo : &sc->play;

            SubjectInfo played;
            played.side = side;
            played.id = card.id;
            played.card = &def;

            switch (def.type)
            {
                case CardType::Minion:
                {
                    EntityId const id = Summon(side, def, a.position, card.id);
                    (void)id;
                    if (steps)
                        RunSteps(*steps, ctx);
                    Queue(On::Played, side, InfoOf(card.id));
                    break;
                }
                case CardType::Weapon:
                    Equip(side, def, card.id);
                    if (steps)
                        RunSteps(*steps, ctx);
                    Queue(On::Played, side, played);
                    break;
                case CardType::Spell:
                {
                    spellActive = true;
                    spellCountered = false;
                    spellTarget = a.target;
                    FireNow(On::SpellCast, side, played, a.target);
                    Settle();
                    ctx.target = spellTarget;
                    bool const countered = spellCountered;
                    spellActive = false;
                    spellCountered = false;
                    if (phase == Phase::Over)
                        break;
                    if (countered)
                    {
                        Emit(EventType::Counter, side, card.id, 0, 0, def.id);
                        break;
                    }
                    if (def.secret)
                    {
                        SecretCard sec;
                        sec.id = card.id;
                        sec.def = &def;
                        sec.sc = base;
                        sec.order = ++playOrder;
                        S.secrets.push_back(sec);
                        Emit(EventType::Secret, side, card.id, card.id, 0, def.id).hideFromOther = true;
                    }
                    else if (steps)
                        RunSteps(*steps, ctx);
                    Queue(On::Played, side, played);
                    Queue(On::AfterSpell, side, played);
                    break;
                }
                default:
                    break;
            }
            Settle();
        }

        void ExecAttack(int side, Action const& a)
        {
            attackActive = true;
            attackAttacker = a.source;
            attackDefender = a.target;
            Emit(EventType::Attack, side, a.source, a.target, 0);
            SubjectInfo att = InfoOf(a.source);
            // Attacks (attacker side, e.g. Truesilver) and Attacked (defender side, secrets) fire first.
            FireNow(On::Attacks, side, att, a.target);
            if (attackActive)
            {
                SubjectInfo def = InfoOf(attackDefender);
                FireNow(On::Attacked, def.side, att, attackDefender);
            }
            Settle();
            CharRef attR = FindChar(attackAttacker);
            CharRef defR = FindChar(attackDefender);
            bool const go = phase != Phase::Over && attackActive && Alive(attR) && Alive(defR) && attR.side == side;
            EntityId const defender = attackDefender;
            attackActive = false;
            if (!go)
            {
                // The attack was cancelled; the attacker still used its attack if it is still on the board.
                if (attR.IsMinion() && attR.side == side)
                    ++MinionAt(attR).attacks;
                else if (attR.IsHero() && attR.side == side)
                    ++sides[side].heroAttacks;
                Settle();
                return;
            }
            bool const heroAttack = attR.IsHero();
            if (attR.IsMinion())
            {
                Minion& m = MinionAt(attR);
                if (m.kw & static_cast<unsigned>(kKwStealth) || m.tempKw & static_cast<unsigned>(kKwStealth) ||
                    m.nextKw & static_cast<unsigned>(kKwStealth))
                {
                    m.kw &= ~static_cast<unsigned>(kKwStealth);
                    m.tempKw &= ~static_cast<unsigned>(kKwStealth);
                    m.nextKw &= ~static_cast<unsigned>(kKwStealth);
                    Emit(EventType::Stealth, side, 0, m.id, 0);
                }
            }
            int dealt = AttackAt(attR);
            // Massive Runeblade: the hero's attack with it deals double damage to heroes.
            if (heroAttack && defR.IsHero() && sides[side].weapon.id != 0 && SideAura(side, AuraKind::DoubleHeroDamage) > 0)
                dealt *= 2;
            int const back = defR.IsMinion() ? EffAttack(defR.side, defR.idx) : 0;
            bool heroSafe = false;
            if (heroAttack && sides[side].weapon.id != 0 && sides[side].weapon.sc)
                for (AuraSpec const& au : sides[side].weapon.sc->auras)
                    if (au.kind == AuraKind::ImmuneWhileAttacking)
                        heroSafe = true;
            // Simultaneous damage: both values were taken before either is dealt.
            Damage(a.source, defR, dealt);
            CharRef attNow = FindChar(a.source);
            if (back > 0 && attNow.Valid() && !heroSafe)
                Damage(defender, attNow, back);
            attNow = FindChar(a.source);
            if (heroAttack)
            {
                ++sides[side].heroAttacks;
                Weapon& w = sides[side].weapon;
                if (w.id != 0)
                {
                    bool gorehowl = false;
                    if (w.sc)
                        for (AuraSpec const& au : w.sc->auras)
                            if (au.kind == AuraKind::AttackCostsAttack)
                                gorehowl = true;
                    if (gorehowl && defR.IsMinion())
                        --w.attack;
                    else
                        --w.durability;
                    if (w.durability <= 0 || w.attack <= 0)
                        BreakWeapon(side, 0);
                }
            }
            else if (attNow.IsMinion())
                ++MinionAt(attNow).attacks;
            Settle();
        }

        void ExecHeroPower(int side, Action const& a)
        {
            SideState& S = sides[side];
            S.mana -= S.power->cost;
            S.powerUsed = true;
            Emit(EventType::HeroPower, side, S.powerId, a.target, 0, S.power->id);
            Ctx ctx;
            ctx.side = side;
            ctx.source = S.powerId;
            ctx.target = a.target;
            ctx.power = true;
            if (Script const* sc = FindScript(S.power->id))
                RunSteps(sc->play, ctx);
            Settle();
        }

        void ExecChoose(int side, Action const& a)
        {
            SideState& S = sides[side];
            std::vector<Card> options;
            options.swap(S.pick);
            for (Card const& c : options)
            {
                if (c.id == a.source)
                {
                    Emit(EventType::Pick, side, 0, c.id, 0, c.def->id).hideFromOther = true;
                    if (Sz(S.hand.size()) >= kMaxHand)
                        Emit(EventType::Burn, side, 0, c.id, 0, c.def->id);
                    else
                    {
                        S.hand.push_back(c);
                        ++S.tally.cardsDrawn; // Tracking's chosen card comes from the deck.
                    }
                }
                else
                    Emit(EventType::Discard, side, 0, c.id, 1, c.def->id).hideFromOther = true;
            }
            Settle();
        }

        // ---- views ---------------------------------------------------------------

        SideView BuildSideView(int side, bool own) const
        {
            SideState const& S = sides[side];
            bool const playing = phase == Phase::Play && sides[0].pick.empty() && sides[1].pick.empty();
            SideView v;
            v.name = S.name;
            v.heroClass = S.heroClass;
            HeroView& h = v.hero;
            h.id = S.heroId;
            h.cardId = S.heroCardId;
            h.health = S.health;
            h.maxHealth = S.maxHealth;
            h.armor = S.armor;
            h.attack = HeroAttackValue(side);
            h.canAttack = playing && active == side && HeroCanAttack(side);
            h.powerId = S.powerId;
            h.powerCardId = S.power ? S.power->id : std::string();
            h.powerCost = S.power ? S.power->cost : 2;
            h.powerUsed = S.powerUsed || S.powerPassive != nullptr; // a passive power shows as spent: nothing to click
            if (!h.powerUsed && S.power)
                if (Script const* ps = FindScript(S.power->id))
                    h.powerUsed = ps->autoCast; // so does an automatic one
            if (S.weapon.id != 0)
            {
                h.weaponId = S.weapon.id;
                h.weaponCardId = S.weapon.def->id;
                h.weaponAttack = WeaponAttack(side);
                h.weaponDurability = S.weapon.durability;
                h.weaponWindfury = S.weapon.windfury;
            }
            h.mana = S.mana;
            h.maxMana = S.maxMana;
            h.fatigue = S.fatigue;
            h.frozen = S.heroFrozen;
            h.immune = HeroImmune(side);
            for (int i = 0; i < Sz(S.board.size()); ++i)
            {
                Minion const& m = S.board[static_cast<std::size_t>(i)];
                unsigned const kw = Keywords(side, i);
                MinionView mv;
                mv.id = m.id;
                mv.cardId = m.def->id;
                mv.attack = EffAttack(side, i);
                mv.health = EffHealth(m);
                mv.maxHealth = EffMaxHealth(m);
                mv.taunt = Has(kw, kKwTaunt);
                mv.charge = Has(kw, kKwCharge);
                mv.divineShield = Has(kw, kKwDivineShield);
                mv.windfury = Has(kw, kKwWindfury);
                mv.spellDamage = m.spellDamage;
                mv.canAttack = playing && active == side && MinionCanAttack(side, i);
                mv.stealth = Has(kw, kKwStealth);
                mv.frozen = m.frozen;
                mv.silenced = m.silenced;
                mv.poisonous = Has(kw, kKwPoisonous);
                mv.immune = Has(kw, kKwImmune);
                mv.cantAttack = Has(kw, kKwCantAttack);
                mv.echoing = m.echo;
                bool enrage = false;
                if (m.sc && !m.silenced && m.damage > 0)
                    for (AuraSpec const& au : m.sc->auras)
                        if (au.whileDamaged)
                            enrage = true;
                mv.enraged = enrage;
                v.board.push_back(mv);
            }
            if (own)
            {
                for (Card const& c : S.hand)
                {
                    HandCardView hv;
                    hv.id = c.id;
                    hv.cardId = c.def->id;
                    hv.cost = CostOf(side, c);
                    if (c.def->type == CardType::Minion)
                    {
                        hv.attack = c.def->attack;
                        hv.health = c.def->health;
                    }
                    else if (c.def->type == CardType::Weapon)
                    {
                        hv.attack = c.def->attack;
                        hv.health = c.def->durability;
                    }
                    if (Script const* sc = FindScript(c.def->id))
                        if (sc->chooseOne())
                        {
                            hv.options.push_back(sc->options[0]);
                            hv.options.push_back(sc->options[1]);
                        }
                    v.hand.push_back(hv);
                }
            }
            for (SecretCard const& s : S.secrets)
            {
                SecretView sv;
                sv.id = s.id;
                if (own)
                    sv.cardId = s.def->id;
                v.secrets.push_back(sv);
            }
            v.handCount = Sz(S.hand.size());
            v.deckCount = Sz(S.deck.size());
            v.cardsPlayed = S.cardsPlayed;
            v.overloadLocked = S.overloadLocked;
            v.overloadOwed = S.overloadOwed;
            v.spellTax = S.spellTax;
            v.spellTaxNext = S.spellTaxNext;
            for (CardDef const* d : S.diedThisTurn)
            {
                MinionView mv;
                mv.cardId = d->id;
                mv.attack = d->attack;
                mv.health = d->health;
                mv.maxHealth = d->health;
                mv.taunt = d->taunt;
                mv.divineShield = d->divineShield;
                mv.windfury = d->windfury;
                mv.stealth = d->stealth;
                mv.poisonous = d->poisonous;
                mv.charge = d->charge;
                mv.spellDamage = d->spellDamage;
                v.diedThisTurn.push_back(mv);
            }
            return v;
        }
    };

    // ---- Custom handlers: the Effects facade ------------------------------------

    namespace
    {
        // A template so that it never names the private Match::State (instantiated from State::RunCustom).
        template <typename StateT>
        struct FxImpl final : Effects
        {
            StateT& st;
            Ctx& c;
            FxImpl(StateT& s, Ctx& ctx) : st(s), c(ctx) {}

            int Side() const override { return c.side; }
            EntityId Source() const override { return c.source; }
            EntityId Target() const override { return c.target; }
            EntityId Subject() const override { return c.subject; }
            int Amount() const override { return c.amount; }
            bool FromSpell() const override { return c.spell; }
            int SpellPower() const override { return c.spell ? c.spellPower : 0; }
            std::vector<EntityId> Select(Sel sel) override { return st.Select(sel, c, 1); }
            bool IsMinion(EntityId id) const override { return st.FindChar(id).IsMinion(); }
            bool IsHero(EntityId id) const override { return st.FindChar(id).IsHero(); }
            bool Alive(EntityId id) const override { return st.Alive(st.FindChar(id)); }
            int SideOf(EntityId id) const override { return st.FindChar(id).side; }
            int Attack(EntityId id) const override
            {
                CharRef r = st.FindChar(id);
                return r.Valid() ? st.AttackAt(r) : 0;
            }
            int Health(EntityId id) const override
            {
                CharRef r = st.FindChar(id);
                return r.Valid() ? st.HealthAt(r) : 0;
            }
            int MaxHealth(EntityId id) const override
            {
                CharRef r = st.FindChar(id);
                if (r.IsMinion())
                    return st.EffMaxHealth(st.MinionAt(r));
                return r.IsHero() ? st.sides[r.side].maxHealth : 0;
            }
            std::string CardOf(EntityId id) const override
            {
                if (Minion const* m = st.FindAnyMinion(id))
                    return m->def->id;
                CharRef r = st.FindChar(id);
                return r.IsHero() ? st.sides[r.side].heroCardId : std::string();
            }
            std::string RaceOf(EntityId id) const override
            {
                Minion const* m = st.FindAnyMinion(id);
                return m ? m->def->race : std::string();
            }
            EntityId HeroOf(int side) const override { return st.sides[side].heroId; }
            std::vector<EntityId> Board(int side) const override
            {
                std::vector<EntityId> ids;
                for (Minion const& m : st.sides[side].board)
                    ids.push_back(m.id);
                return ids;
            }
            int HandCount(int side) const override { return Sz(st.sides[side].hand.size()); }
            int CardsPlayedThisTurn(int side) const override { return st.sides[side].cardsPlayed; }
            int Rand(int n) override { return st.Rand(n); }
            void Run(Step const& step) override
            {
                if (st.CondHolds(step.cond, step.condRace, c))
                {
                    st.RunStep(step, c);
                    st.RefreshAuras();
                }
            }
            void Damage(EntityId target, int amount) override { st.Damage(c.source, st.FindChar(target), amount, c.side); }
            void Heal(EntityId target, int amount) override { st.Heal(c.side, c.source, st.FindChar(target), amount); }
            void Destroy(EntityId target) override { st.Destroy(st.FindChar(target), c.source); }
            void Buff(EntityId target, int attack, int health) override
            {
                CharRef r = st.FindChar(target);
                if (!r.IsMinion())
                    return;
                Minion& m = st.MinionAt(r);
                m.attack += attack;
                m.maxHealth += health;
                st.Emit(EventType::Buff, r.side, c.source, target, attack);
                st.RefreshAuras();
            }
            EntityId Summon(int side, std::string const& cardId, int position) override
            {
                CardDef const* def = st.db->Find(cardId);
                if (!def)
                    return 0;
                EntityId const id = st.Summon(side, *def, position, 0);
                if (id)
                    c.lastSummoned = id;
                return id;
            }
            void Draw(int side, int n) override { st.DrawCards(side, n); }
            EntityId CopyMinion(EntityId from, EntityId into) override
            {
                Minion const* src = st.FindAnyMinion(from);
                CharRef r = st.FindChar(into);
                if (!src || !r.IsMinion())
                    return 0;
                Minion copy = *src;
                Minion& m = st.MinionAt(r);
                copy.id = m.id;
                copy.order = m.order;
                copy.sick = true;
                copy.attacks = 0;
                copy.destroyed = false;
                copy.auraHealth = 0;
                copy.destroyAtStartOf = -1;
                copy.returnSide = -1;
                copy.echo = false;
                m = copy;
                st.Emit(EventType::Transform, r.side, c.source, m.id, 0, m.def->id);
                st.RefreshAuras();
                return m.id;
            }
            std::string HeroPowerOf(int side) const override
            {
                return st.sides[side].power ? st.sides[side].power->id : std::string();
            }
            int Armor(int side) const override { return st.sides[side].armor; }
            int HeroHealth(int side) const override { return st.sides[side].health; }
            int WeaponAttack(int side) const override { return st.WeaponAttack(side); }
            bool IsFrozen(EntityId id) const override
            {
                CharRef r = st.FindChar(id);
                return r.Valid() && st.IsFrozen(r);
            }
            std::vector<std::string> DeckCards(int side) const override
            {
                std::vector<std::string> ids;
                for (std::size_t i = st.sides[side].deck.size(); i > 0; --i)
                    ids.push_back(side == c.side ? st.sides[side].deck[i - 1].def->id : std::string("?"));
                return ids;
            }
            bool TakeFromDeck(int side, std::string const& cardId) override
            {
                SideState& S = st.sides[side];
                for (std::size_t i = S.deck.size(); i > 0; --i)
                    if (S.deck[i - 1].def->id == cardId)
                    {
                        Card const card = S.deck[i - 1];
                        S.deck.erase(S.deck.begin() + static_cast<std::ptrdiff_t>(i - 1));
                        if (Sz(S.hand.size()) >= kMaxHand)
                            st.Emit(EventType::Burn, side, 0, card.id, 0, card.def->id);
                        else
                        {
                            S.hand.push_back(card);
                            st.Emit(EventType::Draw, side, 0, card.id, 0, card.def->id).hideFromOther = true;
                        }
                        return true;
                    }
                return false;
            }
            int DamageMultiplier() const override { return st.Doubling(c); }
            CardDef const* CardInfo(std::string const& cardId) const override { return st.db->Find(cardId); }
            // ---- the rest of Classic ----
            int WeaponDurability(int side) const override
            {
                Weapon const& w = st.sides[side].weapon;
                return w.id == 0 ? 0 : std::max(0, w.durability);
            }
        };
    }

    void Match::State::RunCustom(CustomFn fn, Ctx& c)
    {
        FxImpl<State> fx(*this, c);
        fn(fx);
    }

    // ---- Match -----------------------------------------------------------------

    Match::Match() = default;
    Match::~Match() = default;

    std::unique_ptr<Match> Match::Create(CardDb const& db, MatchSetup const& setup, std::string& error)
    {
        error.clear();
        for (int s = 0; s < 2; ++s)
        {
            SideSetup const& ss = setup.sides[s];
            if (HeroCardOf(ss.heroClass).empty())
            {
                error = "unknown hero class '" + ss.heroClass + "'";
                return nullptr;
            }
            // An encounter side that never draws (Patchwerk) may have no deck at all.
            bool const noDeck = ss.deck.empty() && ss.start.noDraw;
            if (!noDeck && (ss.deck.size() < kMinDeckCards || ss.deck.size() > kMaxDeckCards))
            {
                error = "deck size out of range";
                return nullptr;
            }
            if (ss.heroHealth < 0 || ss.heroHealth > 200 || ss.heroArmor < 0 || ss.heroArmor > 200)
            {
                error = "hero health or armor out of range";
                return nullptr;
            }
            for (std::string const& id : ss.deck)
            {
                CardDef const* def = db.Find(id);
                if (!def)
                {
                    error = "unknown card " + id;
                    return nullptr;
                }
                if (!db.Supported(id))
                {
                    error = "unsupported card " + id;
                    return nullptr;
                }
                if (def->type != CardType::Minion && def->type != CardType::Spell && def->type != CardType::Weapon)
                {
                    error = "card " + id + " cannot be in a deck";
                    return nullptr;
                }
            }
            // Encounter start state: the same rules as for a deck card (known, Supported), plus the type.
            if (ss.start.board.size() > static_cast<std::size_t>(kMaxBoard))
            {
                error = "start board too large";
                return nullptr;
            }
            if (ss.start.hand.size() + static_cast<std::size_t>(kStartHandSecond) + 1 > static_cast<std::size_t>(kMaxHand))
            {
                error = "start hand too large";
                return nullptr;
            }
            auto const checkStart = [&](std::string const& id, char const* what, bool (*typeOk)(CardType)) -> bool {
                CardDef const* def = db.Find(id);
                if (!def)
                    error = "unknown card " + id;
                else if (!db.Supported(id))
                    error = "unsupported card " + id;
                else if (!typeOk(def->type))
                    error = std::string("card ") + id + " cannot be a " + what;
                return error.empty();
            };
            for (std::string const& id : ss.start.board)
                if (!checkStart(id, "start minion", [](CardType t) { return t == CardType::Minion; }))
                    return nullptr;
            if (!ss.start.weapon.empty() &&
                !checkStart(ss.start.weapon, "start weapon", [](CardType t) { return t == CardType::Weapon; }))
                return nullptr;
            for (std::string const& id : ss.start.hand)
                if (!checkStart(id, "start hand card", [](CardType t) {
                        return t == CardType::Minion || t == CardType::Spell || t == CardType::Weapon; }))
                    return nullptr;
            std::string const power = ss.heroPowerId.empty() ? HeroPowerOf(ss.heroClass) : ss.heroPowerId;
            std::vector<std::string> needed;
            needed.push_back(ss.heroCardId.empty() ? HeroCardOf(ss.heroClass) : ss.heroCardId);
            needed.push_back(power);
            needed.push_back(kCoinId);
            for (std::string const& id : needed)
                if (!db.Find(id))
                {
                    error = "engine card " + id + " missing from the card database";
                    return nullptr;
                }
            CardDef const* pdef = db.Find(power);
            if (pdef->type != CardType::HeroPower || !db.Supported(power))
            {
                error = "hero power " + power + " is not supported";
                return nullptr;
            }
        }
        if (setup.firstSide < -1 || setup.firstSide > 1)
        {
            error = "bad first side";
            return nullptr;
        }

        std::unique_ptr<Match> m(new Match());
        m->m_state.reset(new State());
        State& st = *m->m_state;
        st.db = &db;
        st.seed = setup.seed;
        st.rng.seed(setup.seed);

        for (int s = 0; s < 2; ++s)
        {
            SideSetup const& ss = setup.sides[s];
            SideState& S = st.sides[s];
            S.name = ss.name;
            S.heroClass = Upper(ss.heroClass);
            S.heroCardId = ss.heroCardId.empty() ? HeroCardOf(ss.heroClass) : ss.heroCardId;
            S.health = S.maxHealth = ss.heroHealth > 0 ? ss.heroHealth : kHeroHealth;
            S.armor = ss.heroArmor;
            S.noDraw = ss.start.noDraw;
            S.heroId = st.NewId();
        }
        for (int s = 0; s < 2; ++s)
        {
            SideState& S = st.sides[s];
            S.powerId = st.NewId();
            st.SetPower(s, db.Find(setup.sides[s].heroPowerId.empty() ? HeroPowerOf(setup.sides[s].heroClass) : setup.sides[s].heroPowerId));
        }
        // Entity ids are handed out AFTER the shuffle, in the shuffled order. The decklist is sorted by
        // card id (ExpandDeck), so ids in decklist order would tell the other side which card a hidden
        // draw is. An id in shuffled order only says where the card lay in a random order.
        for (int s = 0; s < 2; ++s)
        {
            SideState& S = st.sides[s];
            for (std::string const& id : setup.sides[s].deck)
            {
                Card c;
                c.def = db.Find(id);
                S.deck.push_back(c);
            }
        }
        st.Shuffle(st.sides[0].deck);
        st.Shuffle(st.sides[1].deck);
        for (int s = 0; s < 2; ++s)
            for (std::size_t i = st.sides[s].deck.size(); i > 0; --i)
                st.sides[s].deck[i - 1].id = st.NewId(); // the top card (back) gets the lowest id
        int const coinToss = st.Rand(2);
        st.first = setup.firstSide >= 0 ? setup.firstSide : coinToss;
        st.active = st.first;

        st.DrawNoFatigue(st.first, kStartHandFirst);
        st.DrawNoFatigue(1 - st.first, kStartHandSecond);
        for (int s = 0; s < 2; ++s)
            for (Card const& c : st.sides[s].hand)
                st.sides[s].offered.push_back(c.id);

        // Encounter start state, after everything above so the shuffle, the coin toss and the opening
        // hands (and so every existing match) are exactly what they were. The extra hand cards come after the
        // offered list was taken: they stay in the hand through the mulligan.
        for (int s = 0; s < 2; ++s)
        {
            SideStart const& start = setup.sides[s].start;
            for (std::string const& id : start.board)
                st.Summon(s, *db.Find(id), -1, 0);
            if (!start.weapon.empty())
                st.Equip(s, *db.Find(start.weapon), 0);
            for (std::string const& id : start.hand)
                st.AddToHand(s, *db.Find(id), 0);
        }
        st.queue.clear(); // the Summoned triggers of the opening board never run
        st.graveyard.clear();
        return m;
    }

    ActionError Match::Apply(int side, Action const& action)
    {
        State& st = *m_state;
        ActionError const e = st.Validate(side, action);
        if (e != ActionError::None)
            return e;
        st.BeginAction();
        switch (action.type)
        {
            case ActionType::Mulligan:
                st.ExecMulligan(side, action);
                break;
            case ActionType::Play:
                st.ExecPlay(side, action);
                break;
            case ActionType::Attack:
                st.ExecAttack(side, action);
                break;
            case ActionType::HeroPower:
                st.ExecHeroPower(side, action);
                break;
            case ActionType::EndTurn:
                st.EndTurn(side);
                break;
            case ActionType::Concede:
                st.EndMatch(1 - side, EndReason::Concede);
                break;
            case ActionType::Choose:
                st.ExecChoose(side, action);
                break;
        }
        st.CheckPhases();
        st.RefreshAuras();
        st.graveyard.clear();
        ++st.seq;
        return ActionError::None;
    }

    std::vector<Action> Match::LegalActions(int side) const
    {
        if (side < 0 || side > 1)
            return std::vector<Action>();
        return m_state->Legal(side);
    }

    MatchView Match::ViewFor(int side, uint32_t sinceEventSeq) const
    {
        State const& st = *m_state;
        MatchView v;
        if (side < 0 || side > 1)
            side = 0;
        v.viewer = side;
        v.seq = st.seq;
        v.phase = st.phase;
        v.turn = st.turn;
        v.activeSide = st.active;
        v.result = st.result;
        v.self = st.BuildSideView(side, true);
        v.other = st.BuildSideView(1 - side, false);
        v.mulliganPending = MulliganPending(side);
        if (v.mulliganPending)
            v.mulligan = st.sides[side].offered;
        if (st.phase == Phase::Play)
        {
            for (Card const& c : st.sides[side].pick)
            {
                PickView pv;
                pv.id = c.id;
                pv.cardId = c.def->id;
                v.pick.push_back(pv);
            }
            v.pickPending = !v.pick.empty();
            v.opponentPicking = !st.sides[1 - side].pick.empty();
        }
        v.legal = st.Legal(side);
        v.turnTimeLimitSec = TurnTimeLimitSec();
        uint32_t const from = std::max(sinceEventSeq, st.eventBase);
        for (std::size_t i = from - st.eventBase; i < st.events.size(); ++i)
        {
            Event e = st.events[i];
            if (e.side != side && e.hideFromOther)
                e.cardId.clear();
            v.events.push_back(e);
        }
        return v;
    }

    void Match::Abort()
    {
        State& st = *m_state;
        if (st.phase == Phase::Over)
            return;
        st.EndMatch(-1, EndReason::Aborted);
        ++st.seq;
    }

    Phase Match::GetPhase() const { return m_state->phase; }
    int Match::ActiveSide() const { return m_state->active; }
    int Match::Turn() const { return m_state->turn; }
    int Match::OwnTurns(int side) const { return side >= 0 && side < 2 ? m_state->sides[side].turnsStarted : 0; }
    SideTally Match::Tally(int side) const
    {
        if (side < 0 || side > 1)
            return {};
        SideTally tally = m_state->sides[side].tally;
        tally.turns = static_cast<uint32_t>(m_state->sides[side].turnsStarted);
        return tally;
    }
    uint32_t Match::Seq() const { return m_state->seq; }
    uint32_t Match::LastEventSeq() const { return m_state->eventSeq; }
    Result Match::GetResult() const { return m_state->result; }
    uint32_t Match::Seed() const { return m_state->seed; }

    bool Match::MulliganPending(int side) const
    {
        return side >= 0 && side <= 1 && m_state->phase == Phase::Mulligan && !m_state->sides[side].mulliganDone;
    }

    int Match::HeroHealth(int side) const
    {
        return (side >= 0 && side <= 1) ? m_state->sides[side].health : 0;
    }

    int Match::TurnTimeLimitSec() const
    {
        if (m_state->phase != Phase::Play)
            return 0;
        for (int s = 0; s < 2; ++s)
            for (Minion const& m : m_state->sides[s].board)
                if (m.def && m.def->id == "EX1_560" && !m.silenced && !m.destroyed)
                    return 15;
        return 0;
    }

    std::unique_ptr<Match> Match::RedactedCopy(int side, uint32_t seed) const
    {
        if (side < 0 || side > 1)
            return nullptr;
        std::unique_ptr<Match> m(new Match());
        m->m_state.reset(new State(*m_state));
        State& st = *m->m_state;
        int const other = 1 - side;
        CardDef const* unknown = &UnknownDef();
        for (Card& c : st.sides[other].hand)
        {
            c.def = unknown;
            c.costMod = 0;
        }
        for (Card& c : st.sides[other].deck)
            c.def = unknown;
        for (SecretCard& s : st.sides[other].secrets)
        {
            s.def = unknown;
            s.sc = nullptr;
        }
        st.sides[other].pick.clear();
        st.sides[other].offered.clear();
        st.rng.seed(seed);
        st.seed = seed;
        st.Shuffle(st.sides[side].deck);
        st.eventBase = st.eventSeq;
        st.events.clear();
        st.queue.clear();
        st.graveyard.clear();
        return m;
    }

    std::string Match::DebugState() const
    {
        State const& st = *m_state;
        std::ostringstream o;
        std::mt19937 probe = st.rng;
        o << "seed=" << st.seed << " phase=" << static_cast<int>(st.phase) << " over=" << st.result.over
          << " winner=" << st.result.winner << " reason=" << static_cast<int>(st.result.reason) << " turn=" << st.turn
          << " active=" << st.active << " first=" << st.first << " seq=" << st.seq << " eseq=" << st.eventSeq
          << " nextId=" << st.nextId << " order=" << st.playOrder << " capped=" << st.capped << " rng=" << probe() << "\n";
        for (int s = 0; s < 2; ++s)
        {
            SideState const& S = st.sides[s];
            o << "side" << s << " " << S.name << " " << S.heroClass << " hero=" << S.heroId << "/" << S.health << "/"
              << S.maxHealth << "/" << S.armor << "/t" << S.tempAttack << "/a" << S.heroAttacks << "/f" << S.heroFrozen
              << "/i" << st.HeroImmune(s) << " power=" << S.powerId << "/" << (S.power ? S.power->id : "-") << "/u" << S.powerUsed
              << " weapon=" << S.weapon.id << "/" << (S.weapon.def ? S.weapon.def->id : "-") << "/" << S.weapon.attack << "/"
              << S.weapon.durability << " mana=" << S.mana << "/" << S.maxMana << " ovl=" << S.overloadLocked << "/"
              << S.overloadOwed << " played=" << S.cardsPlayed << " fat=" << S.fatigue << (S.noDraw ? " nodraw" : "") << " mull=" << S.mulliganDone << " mp=" << S.minionsPlayed
              << " floor=" << S.minionFloor << " tax=" << S.spellTax << "/" << S.spellTaxNext << " next=";
            for (CardDef const* d : S.nextTurnCards)
                o << d->id << ",";
            o << "\n";
            o << " offered:";
            for (EntityId id : S.offered)
                o << " " << id;
            o << "\n deck:";
            for (std::size_t i = S.deck.size(); i > 0; --i)
                o << " " << S.deck[i - 1].id << ":" << S.deck[i - 1].def->id;
            o << "\n hand:";
            for (Card const& c : S.hand)
                o << " " << c.id << ":" << c.def->id << (c.costMod ? "+" + std::to_string(c.costMod) : "");
            o << "\n board:";
            for (Minion const& m : S.board)
                o << " " << m.id << ":" << m.def->id << "/" << m.attack << "/" << m.maxHealth << "-" << m.damage << "/k" << m.kw
                  << "." << m.tempKw << "." << m.nextKw << "/s" << m.spellDamage << "/" << m.sick << m.attacks << m.frozen
                  << m.silenced << "/o" << m.order << "/r" << m.returnSide << "/g" << m.grants.size() << "/e" << m.echo;
            if (S.phaseDone || S.phasePending)
                o << "\n phase: turns=" << S.turnsStarted << (S.phaseDone ? " done" : "") << (S.phasePending ? " pending" : "");
            o << "\n tally:" << S.turnsStarted << ":" << S.tally.heroPowers << ":" << S.tally.cardsDrawn
              << ":" << S.tally.minionsPlayed << ":" << S.tally.spellsPlayed << ":" << S.tally.minionsDestroyed
              << ":" << S.tally.heroDamage << ":" << S.tally.beastsPlayed << ":" << S.tally.expensivePlayed;
            o << "\n died: mask=" << S.diedMask << " turn=";
            for (CardDef const* d : S.diedThisTurn)
                o << d->id << ",";
            o << "\n grants:";
            for (Minion const& m : S.board)
                for (Granted const& g : m.grants)
                    o << " " << m.id << ":" << g.sc->id << "@" << g.side;
            o << "\n secrets:";
            for (SecretCard const& sc : S.secrets)
                o << " " << sc.id << ":" << sc.def->id;
            o << "\n pick:";
            for (Card const& c : S.pick)
                o << " " << c.id << ":" << c.def->id;
            o << "\n";
        }
        return o.str();
    }

    // ---- test access -------------------------------------------------------------

    std::vector<EntityId> TestAccess::SetHand(Match& m, int side, std::vector<std::string> const& cardIds)
    {
        Match::State& st = *m.m_state;
        std::vector<EntityId> ids;
        st.sides[side].hand.clear();
        for (std::string const& cid : cardIds)
        {
            Card c;
            c.id = st.NewId();
            c.def = st.db->Find(cid);
            st.sides[side].hand.push_back(c);
            ids.push_back(c.id);
        }
        return ids;
    }

    std::vector<EntityId> TestAccess::SetBoard(Match& m, int side, std::vector<std::string> const& cardIds)
    {
        Match::State& st = *m.m_state;
        std::vector<EntityId> ids;
        st.sides[side].board.clear();
        for (std::string const& cid : cardIds)
        {
            Minion mn = st.MakeMinion(*st.db->Find(cid), st.NewId());
            mn.sick = false;
            mn.order = ++st.playOrder;
            st.sides[side].board.push_back(mn);
            ids.push_back(mn.id);
        }
        st.RefreshAuras();
        // Fresh minions start undamaged even when an aura gives them health.
        for (int s = 0; s < 2; ++s)
            for (Minion& mn : st.sides[s].board)
                if (std::find(ids.begin(), ids.end(), mn.id) != ids.end())
                    mn.damage = 0;
        return ids;
    }

    std::vector<EntityId> TestAccess::SetDeck(Match& m, int side, std::vector<std::string> const& topFirst)
    {
        Match::State& st = *m.m_state;
        std::vector<EntityId> ids;
        st.sides[side].deck.clear();
        for (std::size_t i = topFirst.size(); i > 0; --i)
        {
            Card c;
            c.id = st.NewId();
            c.def = st.db->Find(topFirst[i - 1]);
            st.sides[side].deck.push_back(c);
        }
        for (std::size_t i = st.sides[side].deck.size(); i > 0; --i)
            ids.push_back(st.sides[side].deck[i - 1].id);
        return ids;
    }

    void TestAccess::SetMana(Match& m, int side, int mana, int maxMana)
    {
        m.m_state->sides[side].mana = mana;
        m.m_state->sides[side].maxMana = maxMana;
    }

    void TestAccess::SetHero(Match& m, int side, int health, int armor)
    {
        m.m_state->sides[side].health = health;
        m.m_state->sides[side].armor = armor;
    }

    void TestAccess::SetFatigue(Match& m, int side, int fatigue)
    {
        m.m_state->sides[side].fatigue = fatigue;
    }

    EntityId TestAccess::SetWeapon(Match& m, int side, std::string const& cardId)
    {
        Match::State& st = *m.m_state;
        st.sides[side].weapon = Weapon();
        if (cardId.empty())
            return 0;
        CardDef const* def = st.db->Find(cardId);
        Weapon w;
        w.id = st.NewId();
        w.def = def;
        w.sc = FindScript(def->id);
        w.attack = def->attack;
        w.durability = def->durability;
        w.windfury = KeywordsOf(*def).windfury;
        w.order = ++st.playOrder;
        st.sides[side].weapon = w;
        return w.id;
    }

    void TestAccess::SetMinion(Match& m, EntityId id, int attack, int health, int maxHealth)
    {
        Match::State& st = *m.m_state;
        CharRef r = st.FindChar(id);
        if (r.IsMinion())
        {
            Minion& mn = st.MinionAt(r);
            mn.attack = attack;
            mn.maxHealth = maxHealth;
            mn.damage = maxHealth - health;
        }
        st.RefreshAuras();
    }

    void TestAccess::SetTurn(Match& m, int turn)
    {
        m.m_state->turn = turn;
    }

    void TestAccess::ReadyAll(Match& m)
    {
        for (SideState& S : m.m_state->sides)
        {
            S.heroAttacks = 0;
            for (Minion& mn : S.board)
            {
                mn.sick = false;
                mn.attacks = 0;
            }
        }
    }

    void TestAccess::SetHeroPowerUsed(Match& m, int side, bool used)
    {
        m.m_state->sides[side].powerUsed = used;
    }

    std::vector<EntityId> TestAccess::SetSecrets(Match& m, int side, std::vector<std::string> const& cardIds)
    {
        Match::State& st = *m.m_state;
        std::vector<EntityId> ids;
        st.sides[side].secrets.clear();
        for (std::string const& cid : cardIds)
        {
            SecretCard s;
            s.id = st.NewId();
            s.def = st.db->Find(cid);
            s.sc = FindScript(cid);
            s.order = ++st.playOrder;
            st.sides[side].secrets.push_back(s);
            ids.push_back(s.id);
        }
        return ids;
    }

    void TestAccess::SetFrozen(Match& m, EntityId id, bool frozen)
    {
        Match::State& st = *m.m_state;
        CharRef r = st.FindChar(id);
        if (r.IsMinion())
        {
            st.MinionAt(r).frozen = frozen;
            st.MinionAt(r).freezeKeep = false;
        }
        else if (r.IsHero())
        {
            st.sides[r.side].heroFrozen = frozen;
            st.sides[r.side].heroFreezeKeep = false;
        }
    }

    void TestAccess::SetCardsPlayed(Match& m, int side, int n)
    {
        m.m_state->sides[side].cardsPlayed = n;
    }

    void TestAccess::SetOverload(Match& m, int side, int locked, int owed)
    {
        m.m_state->sides[side].overloadLocked = locked;
        m.m_state->sides[side].overloadOwed = owed;
    }

    EntityId TestAccess::HeroId(Match const& m, int side)
    {
        return m.m_state->sides[side].heroId;
    }

    EntityId TestAccess::PowerId(Match const& m, int side)
    {
        return m.m_state->sides[side].powerId;
    }

    std::vector<EntityId> TestAccess::HandIds(Match const& m, int side)
    {
        std::vector<EntityId> ids;
        for (Card const& c : m.m_state->sides[side].hand)
            ids.push_back(c.id);
        return ids;
    }

    std::vector<EntityId> TestAccess::BoardIds(Match const& m, int side)
    {
        std::vector<EntityId> ids;
        for (Minion const& mn : m.m_state->sides[side].board)
            ids.push_back(mn.id);
        return ids;
    }

    std::vector<EntityId> TestAccess::DeckIds(Match const& m, int side)
    {
        std::vector<EntityId> ids;
        std::vector<Card> const& d = m.m_state->sides[side].deck;
        for (std::size_t i = d.size(); i > 0; --i)
            ids.push_back(d[i - 1].id);
        return ids;
    }

    std::vector<std::string> TestAccess::DeckCardIds(Match const& m, int side)
    {
        std::vector<std::string> ids;
        std::vector<Card> const& d = m.m_state->sides[side].deck;
        for (std::size_t i = d.size(); i > 0; --i)
            ids.push_back(d[i - 1].def->id);
        return ids;
    }

    std::vector<std::string> TestAccess::HandCardIds(Match const& m, int side)
    {
        std::vector<std::string> ids;
        for (Card const& c : m.m_state->sides[side].hand)
            ids.push_back(c.def->id);
        return ids;
    }

    std::vector<std::string> TestAccess::BoardCardIds(Match const& m, int side)
    {
        std::vector<std::string> ids;
        for (Minion const& mn : m.m_state->sides[side].board)
            ids.push_back(mn.def->id);
        return ids;
    }

    std::vector<std::string> TestAccess::SecretCardIds(Match const& m, int side)
    {
        std::vector<std::string> ids;
        for (SecretCard const& s : m.m_state->sides[side].secrets)
            ids.push_back(s.def->id);
        return ids;
    }

    int TestAccess::HandCost(Match const& m, EntityId handCard)
    {
        Match::State const& st = *m.m_state;
        for (int s = 0; s < 2; ++s)
            for (Card const& c : st.sides[s].hand)
                if (c.id == handCard)
                    return st.CostOf(s, c);
        return -1;
    }
}
}
