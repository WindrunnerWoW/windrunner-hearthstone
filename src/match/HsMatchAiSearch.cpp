/*
 * The Hard profile of the opponent AI plus the simulation helpers both profiles share.
 *
 * Hard plays its own turn by SEARCH on redacted copies of the match (Match::RedactedCopy: unknown cards are
 * unplayable placeholders, own deck reshuffled, random outcomes are not the real ones), so it works for every
 * card the engine supports without knowing any card:
 *   1. a lethal stage (only when the enemy hero could possibly die this turn): beam search for a sequence that
 *      kills the enemy hero, ranked by "enemy health left minus the damage still available";
 *   2. a beam search over action sequences ranked by a static evaluation of the position (board control with
 *      keywords, card advantage, hero health, hero power / mana use, overload, secrets, the opponent's threat on
 *      its next turn and the pressure on it), at most kAiHardMaxSimulations Apply calls in all.
 * The first action of the best sequence is returned. The service calls once per action, so the plan is made
 * again after every real action (random outcomes of the copies were not the real ones).
 */

#include "HsMatchAi.h"
#include "HsMatchAiInternal.h"
#include "HsMatchInternal.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Hs
{
namespace Match
{
namespace AiDetail
{
#ifdef HS_AI_TUNING
    thread_local Weights* g_tuneWeights = nullptr;
#endif

    namespace
    {
        Weights const& W()
        {
#ifdef HS_AI_TUNING
            if (g_tuneWeights)
                return *g_tuneWeights;
#endif
            static Weights const w;
            return w;
        }

        int Sz(std::size_t n)
        {
            return static_cast<int>(n);
        }

        double Dbl(int v)
        {
            return static_cast<double>(v);
        }

        // ---- card and minion values --------------------------------------------------------------

        struct ScriptInfo
        {
            bool deathrattle = false;
            bool trigger = false;
            bool aura = false;
            bool adjacentAura = false;
            bool battlecry = false;
            bool doublesDeathrattles = false; // Baron Rivendare
            bool wipesBoard = false;          // Doomsayer: destroys every minion at the start of its owner's turn
            bool adjacentPlay = false;        // a Battlecry for the minions next to it (Defender of Argus)
        };

        ScriptInfo InfoOf(std::string const& cardId)
        {
            ScriptInfo i;
            Detail::Script const* sc = Detail::FindScript(cardId);
            if (!sc)
                return i;
            i.deathrattle = !sc->deathrattle.empty();
            i.trigger = !sc->triggers.empty();
            i.aura = !sc->auras.empty();
            for (Detail::AuraSpec const& a : sc->auras)
                if (a.scope == Detail::AuraScope::Adjacent)
                    i.adjacentAura = true;
            i.battlecry = !sc->play.empty();
            for (Detail::Step const& st : sc->play)
                if (st.sel == Detail::Sel::AdjacentToSelf)
                    i.adjacentPlay = true;
            i.doublesDeathrattles = sc->doublesDeathrattles;
            for (Detail::TriggerSpec const& t : sc->triggers)
                if (t.on == Detail::On::TurnStart && t.who == Detail::Who::Friendly)
                    for (Detail::Step const& st : t.steps)
                        if (st.op == Detail::Op::Destroy && st.sel == Detail::Sel::AllMinions)
                            i.wipesBoard = true;
            return i;
        }

        // Value of a minion on the board (stat points, roughly 1 point = 1 point of attack or health).
        double MinionBoardValue(MinionView const& m, bool mine)
        {
            double atk = m.cantAttack ? 0.0 : Dbl(m.attack);
            if (m.frozen && !mine)
                atk *= 0.5;
            double v = atk + Dbl(m.health) + W().presence;
            if (m.taunt)
                v += 1.0 + 0.2 * Dbl(m.health);
            if (m.divineShield)
                v += 1.0 + 0.7 * Dbl(m.attack);
            if (m.windfury && !m.silenced)
                v += 0.8 * atk;
            if (m.stealth)
                v += 0.5 + 0.3 * Dbl(m.attack);
            if (m.poisonous)
                v += 2.5;
            v += 1.5 * Dbl(m.spellDamage);
            // An Echoing Ooze about to copy itself is worth a second body of its current size.
            if (m.echoing)
                v += Dbl(m.attack) + Dbl(m.health) + W().presence;
            if (!m.silenced)
            {
                ScriptInfo const i = InfoOf(m.cardId);
                if (i.deathrattle)
                    v += 1.5;
                if (i.trigger)
                    v += 1.5;
                if (i.aura)
                    v += 2.0;
            }
            return v;
        }

        // Value of a card in the hand (about 0.72 of what the minion is worth on the board, so playing it pays).
        double CardValue(CardDef const& d, int cost, int attack, int health)
        {
            switch (d.type)
            {
                case CardType::Minion:
                {
                    double v = Dbl(attack) + Dbl(health) + W().presence;
                    if (d.taunt)
                        v += 1.0 + 0.2 * Dbl(health);
                    if (d.divineShield)
                        v += 1.0 + 0.7 * Dbl(attack);
                    if (d.windfury)
                        v += 0.8 * Dbl(attack);
                    if (d.stealth)
                        v += 0.5 + 0.3 * Dbl(attack);
                    if (d.poisonous)
                        v += 2.5;
                    if (d.charge)
                        v += 0.5 + 0.3 * Dbl(attack);
                    v += 1.5 * Dbl(d.spellDamage);
                    ScriptInfo const i = InfoOf(d.id);
                    if (i.deathrattle)
                        v += 1.5;
                    if (i.trigger)
                        v += 1.5;
                    if (i.aura)
                        v += 2.0;
                    if (i.battlecry)
                        v += 1.0;
                    return W().handMinion * v;
                }
                case CardType::Spell:
                    if (d.id == Detail::kCoinId)
                        return 0.3;
                    if (d.secret)
                        return W().secretHand;
                    return W().spellBase + W().spellPerCost * Dbl(cost);
                case CardType::Weapon:
                    return W().weaponHand * (1.5 + 0.9 * Dbl(attack) * Dbl(health) + 0.5 * Dbl(health));
                default:
                    return 3.0;
            }
        }

        double WeaponValue(int attack, int durability)
        {
            if (durability <= 0)
                return 0.0;
            return 1.5 + 0.9 * Dbl(attack) * Dbl(durability) + 0.5 * Dbl(durability);
        }

        // Interactions between the minions of one side (wave 2): Baron Rivendare makes every Deathrattle on his board
        // worth about twice (the generic value counts one), Kel'Thuzad turns this turn's deaths into minions again.
        double SynergyValue(SideView const& sd)
        {
            bool baron = false;
            int deathrattles = 0;
            double v = 0.0;
            for (MinionView const& m : sd.board)
            {
                if (m.silenced)
                    continue;
                ScriptInfo const i = InfoOf(m.cardId);
                if (i.doublesDeathrattles)
                    baron = true;
                else if (i.deathrattle)
                    ++deathrattles;
                if (m.cardId == "FP1_013")
                    v += 2.5;
            }
            if (baron)
                v += 1.5 * Dbl(deathrattles);
            return v;
        }

        // Does a minion of the side make its hero Immune for as long as it stands (the Horsemen of the Naxxramas encounter)?
        // Only that aura counts, not the one-turn Immune of Ice Block, so ordinary matches evaluate as before.
        bool HeroImmuneByAura(SideView const& sd)
        {
            for (MinionView const& m : sd.board)
            {
                if (m.silenced)
                    continue;
                Detail::Script const* sc = Detail::FindScript(m.cardId);
                if (!sc)
                    continue;
                for (Detail::AuraSpec const& a : sc->auras)
                    if (a.kind == Detail::AuraKind::HeroImmune)
                        return true;
            }
            return false;
        }

        // What the known Secrets of a side are worth on top of the flat value of a Secret: Avenge needs a second minion to
        // buff, Duplicate pays for the minions that matter and needs room in the hand.
        double SecretFit(SideView const& sd)
        {
            double v = 0.0;
            for (SecretView const& sec : sd.secrets)
            {
                if (sec.cardId == "FP1_020")
                    v += sd.board.size() >= 2 ? 1.5 : -0.4; // small: a lone Secret must still be played
                else if (sec.cardId == "FP1_018")
                {
                    int big = 0;
                    for (MinionView const& m : sd.board)
                        if (m.attack + m.health >= 6)
                            ++big;
                    v += std::min(3.0, 0.75 * Dbl(big)) - (sd.handCount >= 9 ? 2.0 : 0.0);
                }
            }
            return v;
        }

        // Hero health is worth more the lower it is.
        double HeroHpValue(double x)
        {
            if (x <= 0.0)
                return -300.0;
            auto clampd = [](double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); };
            return W().heroScale * (2.0 * clampd(x, 0.0, 7.0) + 1.2 * clampd(x - 7.0, 0.0, 5.0) + 0.8 * clampd(x - 12.0, 0.0, 8.0) +
                                    0.5 * clampd(x - 20.0, 0.0, 10.0) + 0.3 * (x > 30.0 ? x - 30.0 : 0.0));
        }

        // Value of having n mana crystals (the next crystals are worth less and less).
        double CrystalValue(int n)
        {
            double v = 0.0;
            for (int i = 1; i <= n && i <= 10; ++i)
                v += (10.0 - Dbl(i)) * 0.45 + 0.4;
            return v;
        }

        // What the board of A can do to the hero of D on A's next turn (Taunt minions of D soak up damage first).
        double FaceDamage(SideView const& a, SideView const& d, bool aIsMine)
        {
            double total = 0.0;
            for (MinionView const& m : a.board)
            {
                if (m.cantAttack || m.attack <= 0)
                    continue;
                if (m.frozen && !aIsMine)
                    continue;
                total += Dbl(m.attack) * (m.windfury ? 2.0 : 1.0);
            }
            if (a.hero.weaponId != 0 && !(a.hero.frozen && !aIsMine))
                total += Dbl(a.hero.weaponAttack) * (a.hero.weaponWindfury ? 2.0 : 1.0);
            double soak = 0.0;
            for (MinionView const& m : d.board)
                if (m.taunt && !m.stealth)
                    soak += Dbl(m.health) + (m.divineShield ? Dbl(std::max(1, m.attack)) : 0.0);
            return total > soak ? total - soak : 0.0;
        }

        // What the opponent could add from its hand (and hero power) on its next turn, over its board: a guess by
        // class and hand size. Cards in hand are unknown, so this is the burst that class tends to keep.
        double BurstGuess(SideView const& op)
        {
            if (op.handCount <= 0)
                return 0.0;
            double const nextMana = Dbl(std::min(kMaxMana, op.hero.maxMana + 1));
            if (!W().classBurst)
                return std::min(8.0, nextMana * 0.7);
            struct ClassBurst
            {
                char const* cls;
                double perMana; // damage per mana crystal
                double cap;     // the most a hand tends to hold
                double power;   // hero power damage to the face
            };
            static ClassBurst const table[] = {
                {"MAGE", 0.8, 9.0, 1.0}, {"ROGUE", 0.75, 8.0, 0.0}, {"HUNTER", 0.7, 8.0, 2.0},
                {"WARRIOR", 0.7, 8.0, 0.0}, {"DRUID", 0.7, 8.0, 0.0}, {"SHAMAN", 0.65, 7.0, 0.0},
                {"WARLOCK", 0.6, 7.0, 0.0}, {"PALADIN", 0.6, 6.5, 0.0}, {"PRIEST", 0.4, 5.0, 0.0},
            };
            ClassBurst const* cb = nullptr;
            for (ClassBurst const& e : table)
                if (op.heroClass == e.cls)
                    cb = &e;
            double const hand = op.handCount >= 3 ? 1.0 : (op.handCount == 2 ? 0.8 : 0.55);
            if (!cb)
                return hand * std::min(8.0, nextMana * 0.7);
            return hand * std::min(cb->cap, cb->perMana * nextMana) + cb->power;
        }

        // Static evaluation of the position from the viewer's side: higher is better for the viewer.
        double EvalPosition(MatchView const& v, CardDb const& db)
        {
            if (v.result.over || v.phase == Phase::Over)
            {
                if (v.result.winner == v.viewer)
                    return 1000000.0;
                if (v.result.winner < 0)
                    return -5000.0;
                return -1000000.0;
            }
            SideView const& me = v.self;
            SideView const& op = v.other;
            double s = 0.0;

            double const myHp = Dbl(me.hero.health + me.hero.armor);
            double const opHp = Dbl(op.hero.health + op.hero.armor);
            s += W().myHero * HeroHpValue(myHp) - HeroHpValue(opHp);

            // Doomsayer: the opponent's destroys every minion as soon as my turn ends (its minions never attack me, mine
            // never attack it); mine goes off after the opponent's turn (its minions still attack me first, and it may
            // kill or silence mine). The Doomsayer itself is only worth what it takes with it.
            bool const opWipe = WipesBoard(op);
            bool const myWipe = !opWipe && WipesBoard(me);
            double const myBoardF = opWipe || myWipe ? 0.0 : 1.0;
            double const opBoardF = opWipe ? 0.0 : (myWipe ? 1.0 - kOwnWipeOdds : 1.0);
            SideView emptyMe, emptyOp;
            if (opWipe || myWipe)
            {
                emptyMe.hero = me.hero;
                emptyOp.hero = op.hero;
            }
            SideView const& meAttacks = opWipe || myWipe ? emptyMe : me; // what is left of my board on my next turn
            SideView const& opAttacks = opWipe ? emptyOp : op;            // ... and of the opponent's on its turn
            SideView const& meDefends = opWipe ? emptyMe : me;
            SideView const& opDefends = opWipe || myWipe ? emptyOp : op;

            for (MinionView const& m : me.board)
                s += myBoardF * MinionBoardValue(m, true);
            for (MinionView const& m : op.board)
                s -= opBoardF * W().enemyMinion * MinionBoardValue(m, false);
            // Kel'Thuzad on a board brings back every minion of that side that died this turn when the turn ends, so
            // while he stands those deaths are worth nothing to the killer (and cost nothing to his owner): count the
            // dead as still there, at the size of the card. Killing him takes the comeback away.
            if (HasKelThuzad(op))
                for (MinionView const& m : op.diedThisTurn)
                    s -= opBoardF * W().enemyMinion * MinionBoardValue(m, false);
            if (HasKelThuzad(me))
                for (MinionView const& m : me.diedThisTurn)
                    s += myBoardF * MinionBoardValue(m, true);

            for (HandCardView const& h : me.hand)
            {
                CardDef const* d = db.Find(h.cardId);
                s += d ? CardValue(*d, h.cost, h.attack, h.health) : 3.0;
            }
            s -= W().unknownCard * Dbl(op.handCount);
            if (Sz(me.hand.size()) >= kMaxHand)
                s -= 2.0;
            s -= W().manaWaste * Dbl(me.hero.mana);

            if (me.hero.weaponId != 0)
                s += WeaponValue(me.hero.weaponAttack, me.hero.weaponDurability);
            if (op.hero.weaponId != 0)
                s -= 1.1 * WeaponValue(op.hero.weaponAttack, op.hero.weaponDurability);

            s += W().secretBoard * Dbl(Sz(me.secrets.size()));
            s -= W().secretOpp * Dbl(Sz(op.secrets.size()));
            s += SecretFit(me) + SynergyValue(me);
            s -= W().enemyMinion * SynergyValue(op);
            s -= 1.6 * Dbl(me.overloadOwed);
            s += W().crystal * (CrystalValue(me.hero.maxMana) - CrystalValue(op.hero.maxMana));

            if (me.deckCount <= 0)
                s -= 2.0 * Dbl(me.hero.fatigue + 1);
            if (op.deckCount <= 0)
                s += 2.0 * Dbl(op.hero.fatigue + 1);

            // The opponent's threat on its next turn, and mine on it.
            double const threat = FaceDamage(opAttacks, meDefends, false);
            double burst = BurstGuess(op);
            // Loatheb: the opponent's spells cost more on its next turn, so most of the burst it could hold is out
            // of reach, and every card it holds is worth a little less to it.
            if (op.spellTaxNext > 0)
            {
                double const tax = std::min(5.0, Dbl(op.spellTaxNext));
                burst *= 1.0 - 0.16 * tax;
                s += 0.08 * tax * Dbl(std::min(5, op.handCount));
            }
            // A hero kept Immune by its minions (the Horsemen's Baron) cannot be hit until the minions that protect it are gone, so what the
            // opponent has on the board is only half the danger.
            double threatEff = threat;
            if (HeroImmuneByAura(me))
            {
                threatEff *= 0.5;
                burst *= 0.5;
            }
            s -= W().threatW * threatEff;
            double const margin = myHp - threatEff - burst;
            if (myHp <= threatEff)
                s -= 4000.0;
            else if (margin <= 0.0)
                s -= 25.0;
            else if (margin < 10.0)
                s -= (10.0 - margin) * 1.0;

            double const press = HeroImmuneByAura(op) ? 0.0 : FaceDamage(meAttacks, opDefends, true); // nothing reaches an Immune hero
            s += W().pressureW * std::min(press, opHp);
            if (press >= opHp)
                s += 8.0;
            return s;
        }

        // ---- hashing ----------------------------------------------------------------------------------

        struct Hasher
        {
            uint64_t h = 1469598103934665603ull;
            void Add(uint64_t x)
            {
                h ^= x + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
                h *= 1099511628211ull;
            }
            void AddStr(std::string const& s) { Add(std::hash<std::string>()(s)); }
        };

        uint64_t MinionSig(MinionView const& m, bool withReady)
        {
            Hasher x;
            x.AddStr(m.cardId);
            x.Add(static_cast<uint64_t>(m.attack));
            x.Add(static_cast<uint64_t>(m.health));
            x.Add(static_cast<uint64_t>(m.maxHealth));
            unsigned flags = 0;
            flags |= m.taunt ? 1u : 0u;
            flags |= m.charge ? 2u : 0u;
            flags |= m.divineShield ? 4u : 0u;
            flags |= m.windfury ? 8u : 0u;
            flags |= m.stealth ? 16u : 0u;
            flags |= m.frozen ? 32u : 0u;
            flags |= m.silenced ? 64u : 0u;
            flags |= m.poisonous ? 128u : 0u;
            flags |= m.immune ? 256u : 0u;
            flags |= m.cantAttack ? 512u : 0u;
            flags |= (withReady && m.canAttack) ? 1024u : 0u;
            x.Add(flags);
            x.Add(static_cast<uint64_t>(m.spellDamage));
            return x.h;
        }

        uint64_t HashView(MatchView const& v)
        {
            Hasher x;
            x.Add(static_cast<uint64_t>(v.phase));
            x.Add(v.pickPending ? 1u : 0u);
            for (int pass = 0; pass < 2; ++pass)
            {
                SideView const& sd = pass == 0 ? v.self : v.other;
                HeroView const& h = sd.hero;
                x.Add(static_cast<uint64_t>(h.health));
                x.Add(static_cast<uint64_t>(h.armor));
                x.Add(static_cast<uint64_t>(h.attack));
                x.Add(static_cast<uint64_t>(h.weaponAttack));
                x.Add(static_cast<uint64_t>(h.weaponDurability));
                x.Add(static_cast<uint64_t>(h.mana));
                x.Add(static_cast<uint64_t>(h.maxMana));
                x.Add((h.powerUsed ? 1u : 0u) | (h.frozen ? 2u : 0u) | (h.canAttack ? 4u : 0u));
                x.AddStr(h.powerCardId);
                x.Add(static_cast<uint64_t>(sd.handCount));
                x.Add(static_cast<uint64_t>(sd.deckCount));
                x.Add(static_cast<uint64_t>(sd.secrets.size()));
                x.Add(static_cast<uint64_t>(sd.overloadOwed));
                x.Add(static_cast<uint64_t>(sd.cardsPlayed > 0 ? 1 : 0));
                x.Add(0xB0A4Du);
                for (MinionView const& m : sd.board)
                    x.Add(MinionSig(m, pass == 0));
                x.Add(0x4A11Du);
                for (MinionView const& m : sd.diedThisTurn)
                    x.AddStr(m.cardId);
                x.Add(0xDEADu);
            }
            for (HandCardView const& c : v.self.hand)
            {
                x.AddStr(c.cardId);
                x.Add(static_cast<uint64_t>(c.cost));
                x.Add(static_cast<uint64_t>(c.attack));
                x.Add(static_cast<uint64_t>(c.health));
            }
            return x.h;
        }

        // Signature of an entity of the view for the equivalence of actions (two identical minions).
        uint64_t EntitySig(MatchView const& v, EntityId id)
        {
            if (id == 0)
                return 1;
            if (id == v.self.hero.id)
                return 2;
            if (id == v.other.hero.id)
                return 3;
            for (MinionView const& m : v.self.board)
                if (m.id == id)
                    return MinionSig(m, true) ^ 0x5A5A5A5Aull;
            for (MinionView const& m : v.other.board)
                if (m.id == id)
                    return MinionSig(m, true) ^ 0xA5A5A5A5ull;
            for (HandCardView const& c : v.self.hand)
                if (c.id == id)
                {
                    Hasher x;
                    x.AddStr(c.cardId);
                    x.Add(static_cast<uint64_t>(c.cost));
                    x.Add(static_cast<uint64_t>(c.attack));
                    x.Add(static_cast<uint64_t>(c.health));
                    return x.h;
                }
            return 0x1000000ull + id;
        }

        uint64_t ActionKey(MatchView const& v, Action const& a)
        {
            Hasher x;
            x.Add(static_cast<uint64_t>(a.type));
            x.Add(a.type == ActionType::HeroPower ? 7ull : EntitySig(v, a.source));
            x.Add(EntitySig(v, a.target));
            x.Add(static_cast<uint64_t>(a.choice));
            x.Add(static_cast<uint64_t>(a.position + 2));
            return x.h;
        }

        // Does playing this action have a random outcome (a random target, a random card, a custom effect)?
        bool IsRandomAction(MatchView const& v, Action const& a)
        {
            std::string cardId;
            if (a.type == ActionType::Play)
            {
                for (HandCardView const& h : v.self.hand)
                    if (h.id == a.source)
                        cardId = h.cardId;
            }
            else if (a.type == ActionType::HeroPower)
                cardId = v.self.hero.powerCardId;
            if (cardId.empty())
                return false;
            Detail::Script const* sc = Detail::FindScript(cardId);
            if (!sc)
                return false;
            for (Detail::Step const& st : sc->play)
            {
                if (st.sel >= Detail::Sel::RandomEnemyMinion || st.custom != nullptr)
                    return true;
                switch (st.op)
                {
                    case Detail::Op::TransformRandomList:
                    case Detail::Op::SummonRandomList:
                    case Detail::Op::AddRandomToHand:
                    case Detail::Op::DiscardRandom:
                    case Detail::Op::TakeControlRandom:
                    case Detail::Op::CopyEnemyHandCard:
                        return true;
                    default:
                        break;
                }
            }
            return false;
        }

        // ---- search -------------------------------------------------------------------------------------

        enum class Mode { Lethal, Full, Simple };

        // A minion that a played card marked for destruction at the start of my next turn (Corruption): the position
        // does not show it, so the search remembers it. value: what its loss is worth to me (negative: it is mine).
        struct Mark
        {
            EntityId id = 0;
            double value = 0.0;
        };

        // The mark that playing action a puts on a minion (nothing for most actions).
        void AddMark(MatchView const& v, Action const& a, std::vector<Mark>& marks)
        {
            if (a.type != ActionType::Play || a.target == 0)
                return;
            HandCardView const* hc = nullptr;
            for (HandCardView const& h : v.self.hand)
                if (h.id == a.source)
                    hc = &h;
            if (!hc)
                return;
            Detail::Script const* sc = Detail::FindScript(hc->cardId);
            if (!sc)
                return;
            bool delayed = false;
            for (Detail::Step const& st : sc->play)
                if (st.op == Detail::Op::DelayedDestroy && st.b == 0 && st.sel == Detail::Sel::Target)
                    delayed = true;
            if (!delayed)
                return;
            for (MinionView const& m : v.other.board)
                if (m.id == a.target)
                {
                    Mark k;
                    k.id = m.id;
                    k.value = 0.8 * W().enemyMinion * MinionBoardValue(m, false);
                    marks.push_back(k);
                }
            for (MinionView const& m : v.self.board)
                if (m.id == a.target)
                {
                    Mark k;
                    k.id = m.id;
                    k.value = -0.8 * MinionBoardValue(m, true);
                    marks.push_back(k);
                }
        }

        double MarkBonus(MatchView const& v, std::vector<Mark> const& marks)
        {
            double sum = 0.0;
            for (Mark const& k : marks)
            {
                bool alive = false;
                for (MinionView const& m : v.other.board)
                    alive = alive || m.id == k.id;
                for (MinionView const& m : v.self.board)
                    alive = alive || m.id == k.id;
                if (alive)
                    sum += k.value;
            }
            return sum;
        }

        struct Node
        {
            std::shared_ptr<Match> m; // null for the root (the real match)
            MatchView v;
            double score = 0.0;       // the position as if the turn ended now
            double rank = 0.0;        // beam ordering
            uint64_t hash = 0;
            int depth = 0;
            Action first;
            bool hasFirst = false;
            double firstScore = 0.0;  // the score of the position right after the first action
            std::vector<Mark> marks;  // minions marked for destruction (not visible in the position)
        };

        struct Ctx
        {
            Match const* real = nullptr;
            CardDb const* db = nullptr;
            int side = 0;
            uint32_t seed = 0;
            uint32_t counter = 0;
            int sims = 0;
            int budget = 0;
        };

        double EnemyHpEff(MatchView const& v)
        {
            return Dbl(v.other.hero.health + v.other.hero.armor);
        }

        // Damage still on offer this turn and the enemy health left: guides the lethal stage.
        double LethalScore(MatchView const& v, int depth)
        {
            if (v.result.over || v.phase == Phase::Over)
            {
                if (v.result.winner == v.viewer)
                    return 1000000.0;
                return -1000000.0;
            }
            double const opHp = EnemyHpEff(v);
            double potential = 0.0;
            for (MinionView const& m : v.self.board)
                if (m.canAttack)
                    potential += Dbl(m.attack) * (m.windfury ? 2.0 : 1.0);
            if (v.self.hero.canAttack)
                potential += Dbl(v.self.hero.attack) * (v.self.hero.weaponWindfury ? 2.0 : 1.0);
            double soak = 0.0;
            for (MinionView const& m : v.other.board)
                if (m.taunt && !m.stealth)
                    soak += Dbl(m.health) + (m.divineShield ? Dbl(std::max(1, m.attack)) : 0.0);
            potential = potential > soak ? potential - soak : 0.0;
            return -10.0 * opHp + 9.0 * std::min(potential, opHp) - 0.01 * Dbl(depth);
        }

        double NodeScore(MatchView const& v, CardDb const& db, Mode mode, int depth)
        {
            if (mode == Mode::Lethal)
                return LethalScore(v, depth);
            return mode == Mode::Simple ? SimpleEvalPosition(v, db) : EvalPosition(v, db);
        }

        // Does a minion of the side turn its healing into damage (Auchenai Soulpriest)?
        bool HealHurts(SideView const& sd)
        {
            for (MinionView const& m : sd.board)
            {
                if (m.silenced)
                    continue;
                Detail::Script const* sc = Detail::FindScript(m.cardId);
                if (!sc)
                    continue;
                for (Detail::AuraSpec const& a : sc->auras)
                    if (a.kind == Detail::AuraKind::HealIsDamage)
                        return true;
            }
            return false;
        }

        // Do the steps only ever do good to the chosen target (a heal, a buff, a keyword)? False when no step touches
        // the target or one of them can hurt it (damage, Silence, Inner Fire, a custom effect ...).
        bool HelpsTargetOnly(std::vector<Detail::Step> const& steps, bool healHurts)
        {
            bool helps = false;
            for (Detail::Step const& st : steps)
            {
                if (st.sel != Detail::Sel::Target)
                    continue;
                if (st.custom != nullptr)
                    return false;
                switch (st.op)
                {
                    case Detail::Op::Heal:
                    case Detail::Op::HealFull:
                        if (healHurts)
                            return false;
                        break;
                    case Detail::Op::Buff:
                        if (st.a < 0 || st.b < 0)
                            return false;
                        break;
                    case Detail::Op::BuffTemp:
                        if (st.a < 0)
                            return false;
                        break;
                    case Detail::Op::Keyword:
                    case Detail::Op::KeywordTemp:
                        if (st.a & Detail::kKwCantAttack)
                            return false;
                        break;
                    case Detail::Op::DoubleHealth:
                    case Detail::Op::DoubleAttack:
                    case Detail::Op::SpellDamageBuff:
                        break;
                    default:
                        return false;
                }
                helps = true;
            }
            return helps;
        }

        // Is the action a card or hero power that only helps its target, aimed at an enemy? The evaluation can still
        // like such a play as a way to spend mana (healing a minion that dies later this turn anyway) or for a side
        // effect (Power Word: Shield draws a card), but to the player it looks like the AI is helping them.
        bool HelpsEnemy(MatchView const& v, Action const& a)
        {
            if (a.target == 0 || (a.type != ActionType::Play && a.type != ActionType::HeroPower))
                return false;
            bool enemy = a.target == v.other.hero.id;
            for (MinionView const& m : v.other.board)
                enemy = enemy || m.id == a.target;
            if (!enemy)
                return false;
            std::string cardId;
            if (a.type == ActionType::HeroPower)
                cardId = v.self.hero.powerCardId;
            else
                for (HandCardView const& h : v.self.hand)
                    if (h.id == a.source)
                        cardId = h.cardId;
            Detail::Script const* sc = cardId.empty() ? nullptr : Detail::FindScript(cardId);
            if (sc && a.choice > 0 && sc->chooseOne())
                sc = Detail::FindScript(sc->options[a.choice == 1 ? 0 : 1]);
            if (!sc)
                return false;
            std::vector<Detail::Step> const& steps = !sc->combo.empty() && v.self.cardsPlayed > 0 ? sc->combo : sc->play;
            return HelpsTargetOnly(steps, HealHurts(v.self));
        }

        // Is the action a card whose only effect on its target is a temporary attack bonus (Rockbiter Weapon), aimed
        // at an own minion that cannot attack this turn (summoning sick, frozen, already attacked)? The bonus would
        // expire unused. A minion with 0 attack that is not frozen stays allowed: the bonus is what lets it attack.
        bool WastedTempAttack(MatchView const& v, Action const& a)
        {
            if (a.target == 0 || a.type != ActionType::Play)
                return false;
            MinionView const* tgt = nullptr;
            for (MinionView const& m : v.self.board)
                if (m.id == a.target)
                    tgt = &m;
            if (!tgt || tgt->canAttack || (tgt->attack <= 0 && !tgt->frozen))
                return false;
            std::string cardId;
            for (HandCardView const& h : v.self.hand)
                if (h.id == a.source)
                    cardId = h.cardId;
            Detail::Script const* sc = cardId.empty() ? nullptr : Detail::FindScript(cardId);
            if (!sc)
                return false;
            std::vector<Detail::Step> const& steps = !sc->combo.empty() && v.self.cardsPlayed > 0 ? sc->combo : sc->play;
            bool temp = false;
            for (Detail::Step const& st : steps)
            {
                if (st.sel != Detail::Sel::Target)
                    continue;
                if (st.custom != nullptr || st.op != Detail::Op::BuffTemp || st.a <= 0)
                    return false;
                temp = true;
            }
            return temp;
        }

        // Candidate actions of a node: the legal list without equivalent duplicates, minion cards with an
        // adjacency aura tried at every board slot; in the lethal stage without attacks that cannot help; never a
        // card or hero power that only helps its target aimed at an enemy (a minion card keeps one such play when
        // it has no other, so its body still reaches the board).
        std::vector<Action> Candidates(MatchView const& v, std::vector<Action> const& legal, CardDb const& db, Mode mode)
        {
            std::vector<Action> out;
            std::unordered_set<uint64_t> seen;
            bool enemyTaunt = false;
            for (MinionView const& m : v.other.board)
                if (m.taunt && !m.stealth)
                    enemyTaunt = true;
            for (Action const& a : legal)
            {
                if (mode == Mode::Lethal && a.type == ActionType::Attack && a.target != v.other.hero.id && !enemyTaunt)
                    continue;
                if (WastedTempAttack(v, a))
                    continue;
                if (HelpsEnemy(v, a))
                {
                    bool minionCard = false;
                    if (a.type == ActionType::Play)
                        for (HandCardView const& h : v.self.hand)
                            if (h.id == a.source)
                            {
                                CardDef const* d = db.Find(h.cardId);
                                minionCard = d && d->type == CardType::Minion;
                            }
                    bool other = false;
                    if (minionCard)
                        for (Action const& b : legal)
                            other = other || (b.type == a.type && b.source == a.source && !HelpsEnemy(v, b));
                    if (!minionCard || other)
                        continue;
                }
                if (!seen.insert(ActionKey(v, a)).second)
                    continue;
                out.push_back(a);
                if (a.type == ActionType::Play && a.position < 0 && !v.self.board.empty() && Sz(v.self.board.size()) < kMaxBoard)
                {
                    HandCardView const* hc = nullptr;
                    for (HandCardView const& c : v.self.hand)
                        if (c.id == a.source)
                            hc = &c;
                    // Every slot for a minion whose aura (Hard) or Battlecry (both profiles: Defender of Argus at the
                    // edge buffs one minion instead of two) works on its neighbours.
                    if (hc && mode != Mode::Lethal)
                    {
                        CardDef const* d = db.Find(hc->cardId);
                        ScriptInfo const info = d && d->type == CardType::Minion ? InfoOf(d->id) : ScriptInfo();
                        if ((mode == Mode::Full && info.adjacentAura) || info.adjacentPlay)
                            for (int p = 0; p < Sz(v.self.board.size()); ++p)
                            {
                                Action b = a;
                                b.position = p;
                                if (seen.insert(ActionKey(v, b)).second)
                                    out.push_back(b);
                            }
                    }
                }
            }
            return out;
        }

        // A leaf worth a closer look: a position reached by a sequence of actions (match null: the real match,
        // nothing done yet), with the first action of the sequence.
        struct Leaf
        {
            std::shared_ptr<Match> m;
            Action first;
            bool hasFirst = false;
            double score = 0.0;
            int depth = 0;
            double firstScore = 0.0; // the score of the position right after the first action
            std::vector<Mark> marks;
        };

        struct Outcome
        {
            bool found = false; // a winning sequence
            Action action;
            std::vector<Leaf> top; // the best leaves by score (the root included), best first
        };

        // Beam search from the real match. Returns the first action of the best sequence (or of a win).
        void AddTop(std::vector<Leaf>& top, int keep, Leaf const& l)
        {
            if (Sz(top.size()) >= keep && !(l.score > top.back().score))
                return;
            std::size_t i = 0;
            while (i < top.size() && top[i].score >= l.score)
                ++i;
            top.insert(top.begin() + static_cast<std::ptrdiff_t>(i), l);
            if (Sz(top.size()) > keep)
                top.pop_back();
        }

        Outcome Search(Ctx& c, MatchView const& rootView, Mode mode, int width, int maxDepth, int budgetEnd, int keep)
        {
            Outcome out;
            Node root;
            root.v = rootView;
            root.score = NodeScore(rootView, *c.db, mode, 0);
            root.hash = HashView(rootView);
            {
                Leaf l;
                l.first = EndTurnAction();
                l.score = root.score;
                out.top.push_back(l);
            }

            std::unordered_set<uint64_t> visited;
            visited.insert(root.hash);
            std::vector<Node> beam;
            beam.push_back(std::move(root));

            for (int depth = 1; depth <= maxDepth && !beam.empty(); ++depth)
            {
                std::vector<Node> cands;
                bool stop = false;
                for (Node const& n : beam)
                {
                    if (stop)
                        break;
                    std::vector<Action> const acts = Candidates(n.v, n.v.legal, *c.db, mode);
                    for (Action const& a : acts)
                    {
                        if (c.sims + 3 > budgetEnd)
                        {
                            stop = true;
                            break;
                        }
                        Node ch;
                        Match const& src = n.m ? *n.m : *c.real;
                        ch.m = src.RedactedCopy(c.side, Mix(c.seed, ++c.counter));
                        if (!ch.m)
                            continue;
                        if (ApplyAndView(*ch.m, c.side, a, *c.db, c.sims, ch.v) != ActionError::None)
                            continue;
                        ch.depth = depth;
                        ch.first = n.hasFirst ? n.first : a;
                        ch.hasFirst = true;
                        if (ch.v.result.over && ch.v.result.winner == c.side)
                        {
                            out.found = true;
                            out.action = ch.first;
                            return out;
                        }
                        ch.marks = n.marks;
                        if (mode == Mode::Full)
                            AddMark(n.v, a, ch.marks);
                        ch.hash = HashView(ch.v);
                        for (Mark const& k : ch.marks)
                            ch.hash = ch.hash * 1099511628211ull + static_cast<uint64_t>(k.id) + 0x9E37u;
                        if (!visited.insert(ch.hash).second)
                            continue;
                        ch.score = NodeScore(ch.v, *c.db, mode, depth) + MarkBonus(ch.v, ch.marks);
                        ch.firstScore = n.hasFirst ? n.firstScore : ch.score;
                        ch.rank = ch.score;
                        if (mode == Mode::Full)
                            ch.rank += W().manaWaste * Dbl(ch.v.self.hero.mana); // unspent mana can still be spent
                        else if (mode == Mode::Simple)
                            ch.rank += kSimpleManaWaste * Dbl(ch.v.self.hero.mana);
                        {
                            Leaf l;
                            l.m = ch.m;
                            l.first = ch.first;
                            l.hasFirst = true;
                            l.score = ch.score;
                            l.depth = depth;
                            l.firstScore = ch.firstScore;
                            l.marks = ch.marks;
                            AddTop(out.top, keep, l);
                        }
                        if (ch.v.result.over)
                            continue; // lost or drawn: no continuation
                        cands.push_back(std::move(ch));
                        if (Sz(cands.size()) > width * 3)
                        {
                            std::stable_sort(cands.begin(), cands.end(), [](Node const& x, Node const& y) { return x.rank > y.rank; });
                            cands.resize(static_cast<std::size_t>(width));
                        }
                    }
                }
                std::stable_sort(cands.begin(), cands.end(), [](Node const& x, Node const& y) { return x.rank > y.rank; });
                if (Sz(cands.size()) > width)
                    cands.resize(static_cast<std::size_t>(width));
                beam = std::move(cands);
                if (stop)
                    break;
            }
            return out;
        }

        // The position after the turn ends and the opponent plays its board greedily (it cannot play cards: its hand
        // is unknown to the copy), each time the move that is worst for me, until nothing hurts me more.
        double ReplyScore(Ctx& c, Match const& base, std::vector<Mark> const& marks, int budgetEnd)
        {
            int const opp = 1 - c.side;
            std::unique_ptr<Match> cur = base.RedactedCopy(c.side, Mix(c.seed, ++c.counter));
            if (!cur)
                return -1e9;
            MatchView view;
            Action end;
            end.type = ActionType::EndTurn;
            if (ApplyAndView(*cur, c.side, end, *c.db, c.sims, view) != ActionError::None)
                return -1e9;
            // A secret that went off did its work: it still counts (the opponent did not know about it, and what it
            // did shows in the position), so the reply is not judged as if the secret had merely vanished.
            int const secretsBefore = Sz(view.self.secrets.size());
            auto judge = [&](MatchView const& v) {
                double const spent = Dbl(std::max(0, secretsBefore - Sz(v.self.secrets.size())));
                return EvalPosition(v, *c.db) + W().secretBoard * spent + MarkBonus(v, marks);
            };
            double score = judge(view);
            for (int step = 0; step < 6 && !view.result.over; ++step)
            {
                MatchView const ov = cur->ViewFor(opp, cur->LastEventSeq());
                if (ov.phase != Phase::Play || ov.activeSide != opp || ov.legal.empty())
                    break;
                std::vector<Action> const acts = Candidates(ov, ov.legal, *c.db, Mode::Full);
                std::unique_ptr<Match> bestM;
                MatchView bestV;
                double bestS = score - 0.05;
                for (Action const& a : acts)
                {
                    if (c.sims + 2 > budgetEnd)
                        break;
                    std::unique_ptr<Match> sim = cur->RedactedCopy(c.side, Mix(c.seed, ++c.counter));
                    if (!sim)
                        continue;
                    ++c.sims;
                    if (sim->Apply(opp, a) != ActionError::None)
                        continue;
                    MatchView const v2 = sim->ViewFor(c.side, sim->LastEventSeq());
                    double const sc = judge(v2);
                    if (sc < bestS)
                    {
                        bestS = sc;
                        bestM = std::move(sim);
                        bestV = v2;
                    }
                }
                if (!bestM)
                    break;
                cur = std::move(bestM);
                view = bestV;
                score = bestS;
            }
            return score;
        }

        // Could the enemy hero die this turn at all? (a loose upper bound; the evaluation still sees wins)
        bool LethalPossible(MatchView const& v)
        {
            double bound = 0.0;
            int ready = 0;
            for (MinionView const& m : v.self.board)
                if (m.canAttack)
                {
                    bound += Dbl(m.attack) * (m.windfury ? 2.0 : 1.0);
                    ++ready;
                }
            if (v.self.hero.canAttack)
                bound += Dbl(v.self.hero.attack) * (v.self.hero.weaponWindfury ? 2.0 : 1.0);
            bound += 2.0 * Dbl(v.self.hero.mana) + 3.0 * Dbl(ready) + 3.0;
            if (!v.self.hand.empty())
                bound += 2.0;
            return bound >= EnemyHpEff(v);
        }
    }

    // ---- shared helpers -------------------------------------------------------------------------------

    bool WipesBoard(SideView const& sd)
    {
        for (MinionView const& m : sd.board)
            if (!m.silenced && InfoOf(m.cardId).wipesBoard)
                return true;
        return false;
    }

    bool HasKelThuzad(SideView const& sd)
    {
        for (MinionView const& m : sd.board)
            if (m.cardId == "FP1_013" && !m.silenced)
                return true;
        return false;
    }

    double PickValue(CardDb const& db, std::string const& cardId, int mana)
    {
        CardDef const* d = db.Find(cardId);
        if (!d)
            return 0.0;
        double v = CardValue(*d, d->cost, d->attack, d->type == CardType::Weapon ? d->durability : d->health);
        if (d->cost <= mana + 1)
            v += 1.0; // playable soon
        return v;
    }

    ActionError ApplyAndView(Match& sim, int side, Action const& a, CardDb const& db, int& sims, MatchView& view)
    {
        ActionError const e = sim.Apply(side, a);
        ++sims;
        view = sim.ViewFor(side, sim.LastEventSeq());
        if (e != ActionError::None)
            return e;
        for (int guard = 0; guard < 4 && view.pickPending && !view.pick.empty(); ++guard)
        {
            PickView const* best = &view.pick[0];
            double bestV = -1e9;
            for (PickView const& p : view.pick)
            {
                double const val = PickValue(db, p.cardId, view.self.hero.mana);
                if (val > bestV)
                {
                    bestV = val;
                    best = &p;
                }
            }
            Action pa;
            pa.type = ActionType::Choose;
            pa.source = best->id;
            sim.Apply(side, pa);
            ++sims;
            view = sim.ViewFor(side, sim.LastEventSeq());
        }
        return ActionError::None;
    }

    // ---- Hard: mulligan ---------------------------------------------------------------------------------

    Action ChooseHardMulligan(MatchView const& v, CardDb const& db)
    {
        if (W().mulliganMode == 0)
            return SimpleMulligan(v, db);
        Action a;
        a.type = ActionType::Mulligan;
        struct Offer
        {
            EntityId id;
            int cost;
            bool minion;
        };
        std::vector<Offer> offers;
        for (EntityId id : v.mulligan)
            for (HandCardView const& h : v.self.hand)
                if (h.id == id)
                {
                    CardDef const* d = db.Find(h.cardId);
                    Offer o;
                    o.id = id;
                    o.cost = d ? d->cost : h.cost;
                    o.minion = d && d->type == CardType::Minion;
                    offers.push_back(o);
                }
        std::stable_sort(offers.begin(), offers.end(), [](Offer const& x, Offer const& y) { return x.cost < y.cost; });
        bool const second = v.self.hand.size() >= static_cast<std::size_t>(kStartHandSecond);
        int cheap = 0;      // kept cards costing 1-2
        int threes = 0;
        int fours = 0;
        for (Offer const& o : offers)
        {
            bool keep = false;
            if (o.cost <= 2)
            {
                keep = true;
                ++cheap;
            }
            else if (o.cost == 3)
            {
                keep = threes < 1 || (cheap >= 1 && threes < 2);
                if (keep)
                    ++threes;
            }
            else if (o.cost == 4)
            {
                keep = second && fours < 1 && cheap >= 1 && threes <= 1;
                if (keep)
                    ++fours;
            }
            if (!keep)
                a.replace.push_back(o.id);
        }
        return a;
    }

    // ---- Hard: pick ---------------------------------------------------------------------------------------

    Action ChooseHardPick(Match const& match, MatchView const& view, int side, CardDb const& db, uint32_t aiSeed)
    {
        Action best;
        best.type = ActionType::Choose;
        if (view.pick.empty())
            return best;
        best.source = view.pick[0].id;
        double bestScore = -1e18;
        int sims = 0;
        for (std::size_t i = 0; i < view.pick.size(); ++i)
        {
            std::unique_ptr<Match> sim = match.RedactedCopy(side, Mix(aiSeed, 0x9E7u + static_cast<uint32_t>(i)));
            if (!sim)
                continue;
            Action a;
            a.type = ActionType::Choose;
            a.source = view.pick[i].id;
            MatchView after;
            if (ApplyAndView(*sim, side, a, db, sims, after) != ActionError::None)
                continue;
            double const sc = EvalPosition(after, db);
            if (sc > bestScore + 1e-9)
            {
                bestScore = sc;
                best.source = view.pick[i].id;
            }
        }
        return best;
    }

    // ---- Hard: the turn -----------------------------------------------------------------------------------

    Action ChooseHard(Match const& match, MatchView const& view, int side, CardDb const& db, int actionsThisTurn,
        uint32_t aiSeed, AiDecisionInfo* info)
    {
        Ctx c;
        c.real = &match;
        c.db = &db;
        c.side = side;
        c.seed = Mix(aiSeed, Mix(view.seq, static_cast<uint32_t>(actionsThisTurn) + 0x51u));
        int const total = kAiHardMaxSimulations;
        int const maxDepth = std::max(1, std::min(12, kAiMaxActionsPerTurn - actionsThisTurn - 1));

        if (LethalPossible(view))
        {
            Outcome const l = Search(c, view, Mode::Lethal, W().lethalWidth, maxDepth, std::min(total, 300), 1);
            if (l.found)
            {
                if (info)
                {
                    info->simulations = c.sims;
                    info->lethal = true;
                }
                return l.action;
            }
        }
        int const replyK = W().replyK;
        int const reserve = replyK > 0 ? W().replyBudget : 0;
        Outcome o = Search(c, view, Mode::Full, W().width, maxDepth, std::min(total - reserve, c.sims + 800),
            std::max(1, replyK + 1));
        if (info)
        {
            info->simulations = c.sims;
            info->lethal = o.found;
        }
        if (o.found)
            return o.action;
        // o.top is sorted by score, the best first (a tie: the earlier, i.e. fewer actions). Each leaf is valued by
        // the opponent's reply (when on) and corrected for the luck of its first action: a random effect (Arcane
        // Missiles, Ragnaros, a drawn card) went one way in the copy the search used, and the plan that got the
        // lucky sample looks better than it is. The first action is applied to a few more copies and the plan is
        // moved by the difference between their mean and the sample it was found with.
        std::size_t pick = 0;
        std::size_t const n = std::min<std::size_t>(o.top.size(), static_cast<std::size_t>(std::max(0, replyK)) + 1);
        if (n > 1)
        {
            std::vector<std::pair<uint64_t, double>> sampled; // first action -> mean score of its samples
            auto firstMean = [&](Leaf const& l, bool& ok) {
                uint64_t const key = ActionKey(view, l.first);
                for (auto const& e : sampled)
                    if (e.first == key)
                    {
                        ok = true;
                        return e.second;
                    }
                double sum = 0.0;
                int got = 0;
                for (int k = 0; k < W().resampleN && c.sims + 4 <= total; ++k)
                {
                    std::unique_ptr<Match> sim = match.RedactedCopy(side, Mix(c.seed, ++c.counter));
                    if (!sim)
                        continue;
                    MatchView after;
                    if (ApplyAndView(*sim, side, l.first, db, c.sims, after) != ActionError::None)
                        continue;
                    std::vector<Mark> marks;
                    AddMark(view, l.first, marks);
                    sum += EvalPosition(after, db) + MarkBonus(after, marks);
                    ++got;
                }
                ok = got > 0;
                double const mean = got > 0 ? sum / Dbl(got) : 0.0;
                if (got > 0)
                    sampled.emplace_back(key, mean);
                return mean;
            };
            double best = -1e18;
            for (std::size_t i = 0; i < n; ++i)
            {
                Leaf const& l = o.top[i];
                Match const& base = l.m ? *l.m : match;
                double value = l.score;
                if (replyK > 0)
                {
                    if (c.sims + 10 > total)
                        break;
                    value = ReplyScore(c, base, l.marks, total);
                }
                if (W().resample && l.hasFirst && l.depth > 0 && IsRandomAction(view, l.first))
                {
                    bool ok = false;
                    double const mean = firstMean(l, ok);
                    if (ok)
                        value += mean - l.firstScore;
                }
                if (value > best + 1e-9)
                {
                    best = value;
                    pick = i;
                }
            }
            if (info)
                info->simulations = c.sims;
        }
        return o.top[pick].hasFirst ? o.top[pick].first : EndTurnAction();
    }

    // ---- Normal: the turn ---------------------------------------------------------------------------------

    // A shallow search over its own turn with the plain evaluation: it can see that two attackers kill a wall, that
    // the Coin pays off with a 4-drop, that a weapon and the hero power go together. It does not look at the
    // opponent's reply and has no lethal stage of its own. Among its best few plans it takes the one with the best
    // noisy score, so it errs between close plans (and sometimes ends its turn with mana left) but not in the same
    // systematic way every game.
    Action ChooseNormal(Match const& match, MatchView const& view, int side, CardDb const& db, int actionsThisTurn,
        uint32_t aiSeed, AiDecisionInfo* info)
    {
        Ctx c;
        c.real = &match;
        c.db = &db;
        c.side = side;
        c.seed = Mix(aiSeed, Mix(view.seq, static_cast<uint32_t>(actionsThisTurn) + 0x33u));
        int const maxDepth = std::max(1, std::min(W().normalDepth, kAiMaxActionsPerTurn - actionsThisTurn - 1));
        Outcome const o = Search(c, view, Mode::Simple, W().normalWidth, maxDepth, W().normalBudget, std::max(1, W().normalKeep));
        if (info)
        {
            info->simulations = c.sims;
            info->lethal = o.found;
        }
        if (o.found)
            return o.action;
        Rng rng(Mix(c.seed, 0x7A3Bu));
        std::size_t pick = 0;
        double best = -1e18;
        for (std::size_t i = 0; i < o.top.size(); ++i)
        {
            double const noisy = o.top[i].score + W().normalNoise * rng.Unit();
            if (noisy > best)
            {
                best = noisy;
                pick = i;
            }
        }
        return o.top[pick].hasFirst ? o.top[pick].first : EndTurnAction();
    }
}
}
}
