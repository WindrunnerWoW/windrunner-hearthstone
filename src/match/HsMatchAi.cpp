#include "HsMatchAi.h"
#include "HsMatchAiInternal.h"

#include <algorithm>
#include <cstddef>
#include <memory>

namespace Hs
{
namespace Match
{
    namespace
    {
        // Play order inside the chosen set of cards: draw first (more options), then removal and
        // damage (before new minions can be hit by an area spell), minions, weapons, buffs last.
        constexpr int kOrderDraw = 1;
        constexpr int kOrderSpell = 2;
        constexpr int kOrderMinion = 3;
        constexpr int kOrderWeapon = 4;
        constexpr int kOrderBuff = 5;

        Action EndTurnAction()
        {
            Action a;
            a.type = ActionType::EndTurn;
            return a;
        }

        int Sz(std::size_t n)
        {
            return static_cast<int>(n);
        }

        // Worth of a minion on the board for trade decisions.
        int MinionValue(MinionView const& m)
        {
            int v = m.attack * 2 + m.health;
            if (m.taunt)
                v += 2;
            if (m.divineShield)
                v += 2;
            if (m.windfury)
                v += m.attack;
            if (m.charge)
                v += 1;
            v += m.spellDamage * 2;
            if (m.cardId == "CS2_122" || m.cardId == "EX1_084")
                v += 3; // Raid Leader / Warsong Commander
            return v;
        }

        int DefValue(CardDef const& d)
        {
            int v = d.attack * 2 + d.health;
            if (d.taunt)
                v += 2;
            if (d.divineShield)
                v += 2;
            if (d.windfury)
                v += d.attack;
            if (d.charge)
                v += 1;
            v += d.spellDamage * 2;
            if (d.id == "CS2_122" || d.id == "EX1_084")
                v += 3;
            return v;
        }

        HandCardView const* FindHand(MatchView const& v, EntityId id)
        {
            for (HandCardView const& h : v.self.hand)
                if (h.id == id)
                    return &h;
            return nullptr;
        }

        MinionView const* FindMinion(SideView const& s, EntityId id)
        {
            for (MinionView const& m : s.board)
                if (m.id == id)
                    return &m;
            return nullptr;
        }

        bool EnemyHasTaunt(MatchView const& v)
        {
            for (MinionView const& m : v.other.board)
                if (m.taunt)
                    return true;
            return false;
        }

        int OwnSpellDamage(MatchView const& v)
        {
            int sd = 0;
            for (MinionView const& m : v.self.board)
                sd += m.spellDamage;
            return sd;
        }

        // ---- mulligan ----------------------------------------------------------

        Action ChooseMulligan(MatchView const& v, CardDb const& db)
        {
            Action a;
            a.type = ActionType::Mulligan;
            for (EntityId id : v.mulligan)
            {
                HandCardView const* h = FindHand(v, id);
                int cost = h ? h->cost : 0;
                if (h)
                    if (CardDef const* d = db.Find(h->cardId))
                        cost = d->cost;
                if (cost >= 4)
                    a.replace.push_back(id);
            }
            return a;
        }

        // ---- lethal --------------------------------------------------------------

        struct Burn
        {
            Action action;
            int damage = 0;
            int cost = 0;
            int order = 0; // 0 first
        };

        bool TryLethal(MatchView const& v, CardDb const& db, Action& out)
        {
            int const enemyHp = v.other.hero.health + v.other.hero.armor;
            bool const taunt = EnemyHasTaunt(v);
            int const mana = v.self.hero.mana;
            int const sd = OwnSpellDamage(v);

            int attackDamage = 0;
            if (!taunt)
            {
                for (MinionView const& m : v.self.board)
                    if (m.canAttack)
                        attackDamage += m.attack;
                if (v.self.hero.canAttack)
                    attackDamage += v.self.hero.attack;
            }

            std::vector<Burn> burns;
            for (Action const& e : v.legal)
            {
                Burn b;
                b.action = e;
                if (e.type == ActionType::Play)
                {
                    HandCardView const* h = FindHand(v, e.source);
                    if (!h)
                        continue;
                    b.cost = h->cost;
                    if (h->cardId == "CS2_029" && e.target == v.other.hero.id)
                    {
                        b.damage = 6 + sd;
                        b.order = 1;
                    }
                    else if (h->cardId == "EX1_593")
                    {
                        b.damage = 3;
                        b.order = 2;
                    }
                    else if (h->cardId == "CS2_105" && !taunt)
                    {
                        int const temp = v.self.hero.attack - v.self.hero.weaponAttack;
                        if (temp == 0 && (v.self.hero.canAttack || v.self.hero.attack == 0))
                        {
                            b.damage = 4;
                            b.order = 0;
                        }
                    }
                }
                else if (e.type == ActionType::HeroPower)
                {
                    b.cost = v.self.hero.powerCost;
                    if (v.self.hero.powerCardId == "CS2_034" && e.target == v.other.hero.id)
                    {
                        b.damage = 1;
                        b.order = 3;
                    }
                    else if (v.self.hero.powerCardId == "DS1h_292")
                    {
                        b.damage = 2;
                        b.order = 3;
                    }
                }
                if (b.damage > 0)
                    burns.push_back(b);
            }
            (void)db;

            // Best subset of burn options within the mana (at most one Fireball per source: legal entries
            // of the same source are alternatives, which the subset search handles by source id).
            int bestDamage = 0;
            unsigned bestMask = 0;
            std::size_t const n = std::min<std::size_t>(burns.size(), 14);
            for (unsigned mask = 1; mask < (1u << n); ++mask)
            {
                int cost = 0;
                int dmg = 0;
                bool ok = true;
                for (std::size_t i = 0; i < n && ok; ++i)
                {
                    if (!(mask & (1u << i)))
                        continue;
                    for (std::size_t j = 0; j < i; ++j)
                        if ((mask & (1u << j)) && burns[j].action.type == burns[i].action.type && burns[j].action.source == burns[i].action.source)
                            ok = false;
                    cost += burns[i].cost;
                    dmg += burns[i].damage;
                }
                if (ok && cost <= mana && dmg > bestDamage)
                {
                    bestDamage = dmg;
                    bestMask = mask;
                }
            }
            if (attackDamage + bestDamage < enemyHp || attackDamage + bestDamage == 0)
                return false;

            // Burn first (cheapest order index first), then everything at the face.
            int pick = -1;
            for (std::size_t i = 0; i < n; ++i)
                if ((bestMask & (1u << i)) && (pick < 0 || burns[i].order < burns[static_cast<std::size_t>(pick)].order))
                    pick = static_cast<int>(i);
            if (pick >= 0)
            {
                out = burns[static_cast<std::size_t>(pick)].action;
                return true;
            }
            for (Action const& e : v.legal)
                if (e.type == ActionType::Attack && e.target == v.other.hero.id)
                {
                    out = e;
                    return true;
                }
            return false;
        }

        // ---- card plays ------------------------------------------------------------

        struct Candidate
        {
            Action action;
            int cost = 0;
            int value = 0;
            int order = 0;
            bool isMinion = false;
            bool needsCoin = false;
        };

        // The legal entries of one source, or empty.
        std::vector<Action const*> OptionsOf(MatchView const& v, EntityId source)
        {
            std::vector<Action const*> out;
            for (Action const& e : v.legal)
                if (e.type == ActionType::Play && e.source == source)
                    out.push_back(&e);
            return out;
        }

        Action const* OptionWithTarget(std::vector<Action const*> const& opts, EntityId target)
        {
            for (Action const* o : opts)
                if (o->target == target)
                    return o;
            return nullptr;
        }

        // Decides whether playing hand card hc (definition d) is sensible now and how. opts: its legal
        // entries (empty for a card that is only playable with The Coin: then untargeted, synthesized).
        bool Evaluate(MatchView const& v, HandCardView const& hc, CardDef const& d, std::vector<Action const*> const& opts,
                      bool synthesized, Candidate& c)
        {
            c.cost = hc.cost;
            c.isMinion = d.type == CardType::Minion;
            c.needsCoin = synthesized;
            Action base;
            base.type = ActionType::Play;
            base.source = hc.id;
            base.target = 0;
            base.position = -1;
            c.action = opts.empty() ? base : *opts[0];

            int const sd = OwnSpellDamage(v);
            int const enemyHp = v.other.hero.health + v.other.hero.armor;
            SideView const& enemy = v.other;
            SideView const& self = v.self;

            if (d.type == CardType::Minion)
            {
                c.order = kOrderMinion;
                c.value = 10 + DefValue(d);
                if (d.id == "EX1_593")
                    c.value += 3;
                if (d.id == "EX1_015" && self.handCount > 8)
                    c.value -= 2;
                return true;
            }
            if (d.type == CardType::Weapon)
            {
                c.order = kOrderWeapon;
                if (self.hero.weaponId != 0 && d.attack <= self.hero.weaponAttack)
                    return false;
                c.value = 8 + d.attack * 2 + d.durability;
                return true;
            }
            if (d.type != CardType::Spell)
                return false;

            std::string const& id = d.id;
            if (id == "CS2_023") // Arcane Intellect
            {
                c.order = kOrderDraw;
                c.value = 7;
                return self.handCount <= 8;
            }
            if (id == "EX1_606") // Shield Block
            {
                c.order = kOrderDraw;
                c.value = 5 + (self.hero.health + self.hero.armor < 15 ? 3 : 0);
                return self.handCount <= 9;
            }
            if (id == "EX1_277") // Arcane Missiles: random, so only an estimate
            {
                c.order = kOrderSpell;
                int small = 0;
                for (MinionView const& m : enemy.board)
                    if (m.health <= 2 && !m.divineShield)
                        ++small;
                c.value = 4 + small * 3 + (enemyHp <= 3 + sd ? 20 : 0);
                return true;
            }
            if (id == "CS2_025") // Arcane Explosion
            {
                c.order = kOrderSpell;
                int const dmg = 1 + sd;
                int kills = 0;
                int hits = 0;
                int value = 0;
                for (MinionView const& m : enemy.board)
                {
                    ++hits;
                    if (!m.divineShield && m.health <= dmg)
                    {
                        ++kills;
                        value += MinionValue(m) / 2 + 1;
                    }
                    else
                        value += 1;
                }
                c.value = value;
                return hits >= 2 || kills >= 1;
            }
            if (id == "EX1_400") // Whirlwind: all minions
            {
                c.order = kOrderSpell;
                int const dmg = 1 + sd;
                int net = 0;
                for (MinionView const& m : enemy.board)
                    net += (!m.divineShield && m.health <= dmg) ? MinionValue(m) / 2 + 1 : 1;
                for (MinionView const& m : self.board)
                    net -= (!m.divineShield && m.health <= dmg) ? MinionValue(m) / 2 + 1 : 1;
                c.value = net;
                return net >= 4;
            }
            if (id == "CS2_114") // Cleave
            {
                c.order = kOrderSpell;
                int const dmg = 2 + sd;
                int killable = 0;
                for (MinionView const& m : enemy.board)
                    if (!m.divineShield && m.health <= dmg)
                        ++killable;
                c.value = 4 + killable * 3;
                return Sz(enemy.board.size()) >= 2 && killable >= 1;
            }
            if (id == "CS2_105") // Heroic Strike
            {
                c.order = kOrderBuff;
                c.value = 5;
                int const temp = self.hero.attack - self.hero.weaponAttack;
                if (temp != 0 || !(self.hero.canAttack || self.hero.attack == 0))
                    return false;
                for (MinionView const& m : enemy.board)
                    if (m.taunt && m.health > 4 + self.hero.weaponAttack)
                        return false;
                return true;
            }
            if (id == "CS2_029") // Fireball
            {
                c.order = kOrderSpell;
                int const dmg = 6 + sd;
                MinionView const* best = nullptr;
                for (MinionView const& m : enemy.board)
                    if (!m.divineShield && m.health <= dmg && OptionWithTarget(opts, m.id) && (!best || MinionValue(m) > MinionValue(*best)))
                        best = &m;
                if (best && MinionValue(*best) >= 7)
                {
                    c.value = 10 + MinionValue(*best);
                    c.action = *OptionWithTarget(opts, best->id);
                    return true;
                }
                Action const* face = OptionWithTarget(opts, enemy.hero.id);
                if (!face)
                    return false;
                c.value = 6 + (enemyHp <= 12 ? 6 : 0);
                c.action = *face;
                return true;
            }
            if (id == "CS2_022") // Polymorph
            {
                c.order = kOrderSpell;
                MinionView const* best = nullptr;
                for (MinionView const& m : enemy.board)
                    if (OptionWithTarget(opts, m.id) && (!best || MinionValue(m) > MinionValue(*best)))
                        best = &m;
                if (!best || MinionValue(*best) < 9)
                    return false;
                c.value = MinionValue(*best) - 3;
                c.action = *OptionWithTarget(opts, best->id);
                return true;
            }
            if (id == "CS2_108") // Execute
            {
                c.order = kOrderSpell;
                MinionView const* best = nullptr;
                for (MinionView const& m : enemy.board)
                    if (OptionWithTarget(opts, m.id) && (!best || MinionValue(m) > MinionValue(*best)))
                        best = &m;
                if (!best || MinionValue(*best) < 4)
                    return false;
                c.value = 4 + MinionValue(*best);
                c.action = *OptionWithTarget(opts, best->id);
                return true;
            }
            if (id == "CS2_103") // Charge: a minion that cannot attack yet
            {
                c.order = kOrderBuff;
                MinionView const* best = nullptr;
                for (MinionView const& m : self.board)
                    if (OptionWithTarget(opts, m.id) && !m.canAttack && !m.charge && (!best || m.attack + m.health > best->attack + best->health))
                        best = &m;
                if (!best)
                    return false;
                c.value = 3 + best->attack + best->health;
                c.action = *OptionWithTarget(opts, best->id);
                return true;
            }
            return false; // The Coin is handled separately; unknown spells are never played
        }

        struct SubsetResult
        {
            int used = -1;
            int value = 0;
            unsigned mask = 0;
        };

        // Best subset of cands within the mana: most mana used, then the highest value.
        SubsetResult BestSubset(std::vector<Candidate> const& cands, int mana, int freeSlots)
        {
            SubsetResult best;
            best.used = -1;
            std::size_t const n = std::min<std::size_t>(cands.size(), 12);
            for (unsigned mask = 0; mask < (1u << n); ++mask)
            {
                int cost = 0;
                int value = 0;
                int minions = 0;
                for (std::size_t i = 0; i < n; ++i)
                    if (mask & (1u << i))
                    {
                        cost += cands[i].cost;
                        value += cands[i].value;
                        minions += cands[i].isMinion ? 1 : 0;
                    }
                if (cost > mana || minions > freeSlots)
                    continue;
                if (cost > best.used || (cost == best.used && value > best.value))
                {
                    best.used = cost;
                    best.value = value;
                    best.mask = mask;
                }
            }
            return best;
        }

        // The next card of a subset to play: lowest order, then the highest value.
        int PickFromSubset(std::vector<Candidate> const& cands, unsigned mask)
        {
            int pick = -1;
            for (std::size_t i = 0; i < cands.size() && i < 12; ++i)
            {
                if (!(mask & (1u << i)))
                    continue;
                if (pick < 0)
                {
                    pick = static_cast<int>(i);
                    continue;
                }
                Candidate const& a = cands[i];
                Candidate const& b = cands[static_cast<std::size_t>(pick)];
                if (a.order < b.order || (a.order == b.order && a.value > b.value))
                    pick = static_cast<int>(i);
            }
            return pick;
        }

        bool TryPlay(MatchView const& v, CardDb const& db, Action& out)
        {
            int const mana = v.self.hero.mana;
            int const freeSlots = kMaxBoard - Sz(v.self.board.size());
            Action const* coin = nullptr;
            std::vector<Candidate> normal;
            std::vector<Candidate> withCoin; // normal plus the cards that only fit with The Coin
            for (HandCardView const& hc : v.self.hand)
            {
                CardDef const* d = db.Find(hc.cardId);
                if (!d)
                    continue;
                std::vector<Action const*> opts = OptionsOf(v, hc.id);
                if (d->id == "GAME_005")
                {
                    if (!opts.empty())
                        coin = opts[0];
                    continue;
                }
                Candidate c;
                if (!opts.empty())
                {
                    if (Evaluate(v, hc, *d, opts, false, c))
                    {
                        normal.push_back(c);
                        withCoin.push_back(c);
                    }
                }
                else if (hc.cost == mana + 1 && (d->type == CardType::Minion || d->type == CardType::Weapon))
                {
                    if (d->type == CardType::Minion && freeSlots <= 0)
                        continue;
                    if (Evaluate(v, hc, *d, opts, true, c))
                        withCoin.push_back(c);
                }
            }

            SubsetResult const a = BestSubset(normal, mana, freeSlots);
            if (coin)
            {
                SubsetResult const b = BestSubset(withCoin, mana + 1, freeSlots);
                if (b.used > mana && b.value >= a.value + 3)
                {
                    // Play The Coin first when a card of the set needs it, else the set itself.
                    bool needsCoin = false;
                    for (std::size_t i = 0; i < withCoin.size() && i < 12; ++i)
                        if ((b.mask & (1u << i)) && withCoin[i].needsCoin)
                            needsCoin = true;
                    if (needsCoin)
                    {
                        out = *coin;
                        return true;
                    }
                    int const p = PickFromSubset(withCoin, b.mask);
                    if (p >= 0)
                    {
                        out = withCoin[static_cast<std::size_t>(p)].action;
                        return true;
                    }
                }
            }
            if (a.used < 0 || a.mask == 0)
                return false;
            int const p = PickFromSubset(normal, a.mask);
            if (p < 0)
                return false;
            out = normal[static_cast<std::size_t>(p)].action;
            return true;
        }

        // ---- hero power --------------------------------------------------------------

        bool TryHeroPower(MatchView const& v, Action& out)
        {
            Action const* entry = nullptr;
            std::vector<Action const*> opts;
            for (Action const& e : v.legal)
                if (e.type == ActionType::HeroPower)
                    opts.push_back(&e);
            if (opts.empty())
                return false;
            HeroView const& h = v.self.hero;
            std::string const& id = h.powerCardId;
            auto withTarget = [&](EntityId t) -> Action const* {
                for (Action const* o : opts)
                    if (o->target == t)
                        return o;
                return nullptr;
            };

            if (id == "CS2_034") // Fireblast: finish a 1 health minion, else the enemy hero
            {
                MinionView const* best = nullptr;
                for (MinionView const& m : v.other.board)
                    if (m.health <= 1 && !m.divineShield && withTarget(m.id) && (!best || MinionValue(m) > MinionValue(*best)))
                        best = &m;
                entry = best ? withTarget(best->id) : withTarget(v.other.hero.id);
            }
            else if (id == "CS1h_001") // Lesser Heal: the most damaged friendly character
            {
                int bestHeal = 0;
                if (withTarget(h.id) && std::min(2, h.maxHealth - h.health) > bestHeal)
                {
                    bestHeal = std::min(2, h.maxHealth - h.health);
                    entry = withTarget(h.id);
                }
                for (MinionView const& m : v.self.board)
                    if (withTarget(m.id) && std::min(2, m.maxHealth - m.health) > bestHeal)
                    {
                        bestHeal = std::min(2, m.maxHealth - m.health);
                        entry = withTarget(m.id);
                    }
            }
            else if (id == "CS2_056") // Life Tap
            {
                if (v.self.handCount <= 8 && h.health + h.armor > 12)
                    entry = opts[0];
            }
            else if (id == "CS2_083b") // Dagger Mastery
            {
                if (h.weaponId == 0)
                    entry = opts[0];
            }
            else if (id == "CS2_102" || id == "DS1h_292" || id == "CS2_101" || id == "CS2_017" || id == "CS2_049")
                entry = opts[0];
            if (!entry)
                return false;
            out = *entry;
            return true;
        }

        // ---- attacks -------------------------------------------------------------------

        struct Attacker
        {
            EntityId id = 0;
            bool hero = false;
            int attack = 0;
            int health = 0; // heroes: health + armor
            bool divine = false;
            int value = 0;
        };

        bool TryAttack(MatchView const& v, Action& out)
        {
            std::vector<Attacker> attackers;
            if (v.self.hero.canAttack)
            {
                Attacker a;
                a.id = v.self.hero.id;
                a.hero = true;
                a.attack = v.self.hero.attack;
                a.health = v.self.hero.health + v.self.hero.armor;
                a.value = 1000;
                attackers.push_back(a);
            }
            for (MinionView const& m : v.self.board)
                if (m.canAttack)
                {
                    Attacker a;
                    a.id = m.id;
                    a.attack = m.attack;
                    a.health = m.health;
                    a.divine = m.divineShield;
                    a.value = MinionValue(m);
                    attackers.push_back(a);
                }
            if (attackers.empty())
                return false;

            bool const forced = EnemyHasTaunt(v);
            int const enemyHp = v.other.hero.health + v.other.hero.armor;
            int bestScore = 0;
            Action const* best = nullptr;
            for (Action const& e : v.legal)
            {
                if (e.type != ActionType::Attack)
                    continue;
                Attacker const* a = nullptr;
                for (Attacker const& x : attackers)
                    if (x.id == e.source)
                        a = &x;
                if (!a)
                    continue;
                int score = 0;
                if (e.target == v.other.hero.id)
                {
                    score = 50 + a->attack + (a->attack >= enemyHp ? 1000 : 0);
                }
                else
                {
                    MinionView const* t = FindMinion(v.other, e.target);
                    if (!t)
                        continue;
                    bool const kills = !t->divineShield && a->attack >= t->health;
                    bool dies;
                    if (a->hero)
                        dies = a->health - t->attack < 12; // never risk the hero for a trade
                    else
                        dies = !a->divine && t->attack >= a->health;
                    if (kills && !dies)
                        score = 200 + MinionValue(*t);
                    else if (kills && dies)
                    {
                        int const worth = MinionValue(*t) - a->value;
                        score = worth > 0 ? 100 + worth : 0;
                    }
                    else if (!kills && !dies)
                        score = 10 + (t->divineShield ? 5 : 0);
                    else
                        score = 0;
                    if (!forced && !kills)
                        score = 0; // no pointless hits (shield pops included): go face instead
                }
                if (score > bestScore)
                {
                    bestScore = score;
                    best = &e;
                }
            }
            if (best && !(forced && bestScore < 50))
            {
                out = *best;
                return true;
            }
            if (!forced)
                return false;

            // A Taunt blocks and nobody kills it alone: attack it with the cheapest set of attackers that can kill
            // it together, if there is such a set.
            MinionView const* weakest = nullptr;
            for (MinionView const& m : v.other.board)
                if (m.taunt && !m.divineShield && (!weakest || m.health < weakest->health))
                    weakest = &m;
            if (!weakest)
                return false;
            std::vector<Attacker> order;
            for (Attacker const& a : attackers)
                if (!a.hero || a.health - weakest->attack >= 12)
                    order.push_back(a);
            std::sort(order.begin(), order.end(), [](Attacker const& x, Attacker const& y) {
                return x.value != y.value ? x.value < y.value : x.id < y.id;
            });
            int sum = 0;
            for (Attacker const& a : order)
                sum += a.attack;
            if (sum < weakest->health || order.empty())
                return false;
            // Cheapest attacker that kills it alone, else the cheapest attacker (the others finish it later).
            EntityId first = order[0].id;
            for (Attacker const& a : order)
                if (a.attack >= weakest->health)
                {
                    first = a.id;
                    break;
                }
            for (Action const& e : v.legal)
                if (e.type == ActionType::Attack && e.source == first && e.target == weakest->id)
                {
                    out = e;
                    return true;
                }
            return false;
        }
    }

    namespace
    {
        // The best option of a pending pick by a plain card value (view-only callers cannot simulate).
        Action ChooseViewPick(MatchView const& view, CardDb const& db)
        {
            Action a;
            a.type = ActionType::Choose;
            double best = -1e18;
            for (PickView const& p : view.pick)
            {
                double const val = AiDetail::PickValue(db, p.cardId, view.self.hero.mana);
                if (val > best)
                {
                    best = val;
                    a.source = p.id;
                }
            }
            return a;
        }
    }

    Action ChooseAiAction(MatchView const& view, CardDb const& db, int actionsThisTurn)
    {
        if (view.mulliganPending)
            return ChooseMulligan(view, db);
        if (view.phase == Phase::Play && view.pickPending && !view.pick.empty())
            return ChooseViewPick(view, db);
        if (view.phase != Phase::Play || view.activeSide != view.viewer)
            return EndTurnAction();
        if (actionsThisTurn >= kAiMaxActionsPerTurn || view.legal.empty())
            return EndTurnAction();

        Action a;
        if (TryLethal(view, db, a))
            return a;
        if (TryPlay(view, db, a))
            return a;
        if (TryHeroPower(view, a))
            return a;
        if (TryAttack(view, a))
            return a;
        return EndTurnAction();
    }

    char const* AiProfileCode(AiProfile p)
    {
        return p == AiProfile::Hard ? "hard" : "normal";
    }

    // ---- the profiles -----------------------------------------------------------------------

    namespace AiDetail
    {
        Action EndTurnAction()
        {
            Action a;
            a.type = ActionType::EndTurn;
            return a;
        }

        Action SimpleMulligan(MatchView const& v, CardDb const& db)
        {
            return ChooseMulligan(v, db);
        }

        // The profile would end its turn with a Taunt blocking the way to the hero (no attack on the hero is
        // legal): the evaluation sees no gain in hitting a wall it cannot kill, so the board would sit idle for
        // good. Keep hitting the weakest Taunt anyway; the damage stays, and the wall falls over the turns.
        // An attacker that survives the hit goes first (the hardest hitter), else the cheapest one.
        bool BreakTaunt(MatchView const& v, Action& out)
        {
            for (Action const& e : v.legal)
                if (e.type == ActionType::Attack && e.target == v.other.hero.id)
                    return false;

            Action const* best = nullptr;
            bool bestSurvives = false;
            int bestKey = 0;
            int bestHealth = 0;
            for (Action const& e : v.legal)
            {
                if (e.type != ActionType::Attack)
                    continue;
                MinionView const* t = FindMinion(v.other, e.target);
                if (!t || !t->taunt)
                    continue;
                int attack = 0;
                int health = 0;
                bool survives = false;
                int key = 0; // survivors: the higher attack is better; the others: the lower value is better
                if (e.source == v.self.hero.id)
                {
                    attack = v.self.hero.attack;
                    health = v.self.hero.health + v.self.hero.armor;
                    if (health - t->attack < 12)
                        continue; // never risk the hero on a wall
                    survives = true;
                    key = attack;
                }
                else
                {
                    MinionView const* a = FindMinion(v.self, e.source);
                    if (!a)
                        continue;
                    attack = a->attack;
                    survives = a->divineShield || t->attack < a->health;
                    key = survives ? attack : -MinionValue(*a);
                }
                if (attack <= 0)
                    continue;
                int const effHealth = t->health + (t->divineShield ? 1000 : 0);
                bool better = !best;
                if (best)
                {
                    if (effHealth != bestHealth)
                        better = effHealth < bestHealth;
                    else if (survives != bestSurvives)
                        better = survives;
                    else
                        better = key > bestKey;
                }
                if (better)
                {
                    best = &e;
                    bestSurvives = survives;
                    bestKey = key;
                    bestHealth = effHealth;
                }
            }
            if (!best)
                return false;
            out = *best;
            return true;
        }

        namespace
        {
            // Normal's evaluation: plain stat counting, no idea of tempo, threats or what the opponent can do.
            double SimpleMinionValue(MinionView const& m)
            {
                double v = static_cast<double>((m.cantAttack ? 0 : m.attack) + m.health);
                if (m.taunt)
                    v += 2.0;
                if (m.divineShield)
                    v += 2.0;
                if (m.windfury)
                    v += 0.5 * static_cast<double>(m.attack);
                if (m.poisonous)
                    v += 2.0;
                v += static_cast<double>(m.spellDamage);
                return v;
            }

            // What the visible board and weapon of D can do to the hero of V next turn (V's Taunts soak up damage first).
            double SimpleThreat(SideView const& attacker, SideView const& victim)
            {
                double total = 0.0;
                for (MinionView const& m : attacker.board)
                    if (!m.cantAttack && !m.frozen && m.attack > 0)
                        total += static_cast<double>(m.attack) * (m.windfury ? 2.0 : 1.0);
                if (attacker.hero.weaponId != 0 && !attacker.hero.frozen)
                    total += static_cast<double>(attacker.hero.weaponAttack) * (attacker.hero.weaponWindfury ? 2.0 : 1.0);
                double soak = 0.0;
                for (MinionView const& m : victim.board)
                    if (m.taunt && !m.stealth)
                        soak += static_cast<double>(m.health);
                return total > soak ? total - soak : 0.0;
            }

            double SimpleHandValue(HandCardView const& h, CardDb const& db)
            {
                CardDef const* d = db.Find(h.cardId);
                if (!d)
                    return 2.5;
                switch (d->type)
                {
                    case CardType::Minion: return 0.7 * static_cast<double>(h.attack + h.health) + (d->taunt ? 1.0 : 0.0);
                    case CardType::Spell:
                        if (d->id == "GAME_005")
                            return 0.5;
                        return 1.5 + (d->secret ? 0.4 : 0.8) * static_cast<double>(h.cost);
                    case CardType::Weapon: return 0.7 * static_cast<double>(2 * h.attack + h.health);
                    default: return 2.5;
                }
            }

            double SimpleEval(MatchView const& v, CardDb const& db)
            {
                if (v.result.over || v.phase == Phase::Over)
                {
                    if (v.result.winner == v.viewer)
                        return 100000.0;
                    return v.result.winner < 0 ? -10000.0 : -100000.0;
                }
                double s = 0.5 * static_cast<double>(v.self.hero.health + v.self.hero.armor) -
                           0.5 * static_cast<double>(v.other.hero.health + v.other.hero.armor);
                // Doomsayer: a board with a live one is gone at the start of its owner's turn (see EvalPosition).
                bool const opWipe = WipesBoard(v.other);
                bool const myWipe = !opWipe && WipesBoard(v.self);
                double const myBoardF = opWipe || myWipe ? 0.0 : 1.0;
                double const opBoardF = opWipe ? 0.0 : (myWipe ? 1.0 - kOwnWipeOdds : 1.0);
                for (MinionView const& m : v.self.board)
                    s += myBoardF * SimpleMinionValue(m);
                for (MinionView const& m : v.other.board)
                    s -= opBoardF * SimpleMinionValue(m);
                // Kel'Thuzad brings back this turn's dead minions of his side when the turn ends (see EvalPosition).
                if (HasKelThuzad(v.other))
                    for (MinionView const& m : v.other.diedThisTurn)
                        s -= opBoardF * SimpleMinionValue(m);
                if (HasKelThuzad(v.self))
                    for (MinionView const& m : v.self.diedThisTurn)
                        s += myBoardF * SimpleMinionValue(m);
                for (HandCardView const& h : v.self.hand)
                    s += SimpleHandValue(h, db);
                s -= 2.5 * static_cast<double>(v.other.handCount);
                if (v.self.hero.weaponId != 0)
                    s += static_cast<double>(2 * v.self.hero.weaponAttack + v.self.hero.weaponDurability);
                if (v.other.hero.weaponId != 0)
                    s -= static_cast<double>(2 * v.other.hero.weaponAttack + v.other.hero.weaponDurability);
                s += 4.5 * static_cast<double>(v.self.secrets.size());
                s -= 4.5 * static_cast<double>(v.other.secrets.size());
                s -= 1.0 * static_cast<double>(v.self.overloadOwed);
                s -= kSimpleManaWaste * static_cast<double>(v.self.hero.mana);

                // A basic sense of survival: what is on the enemy's board now (nothing about its hand) against
                // my health. Without it the AI races blindly and walks into a lethal it could see coming.
                double const hp = static_cast<double>(v.self.hero.health + v.self.hero.armor);
                SideView noBoard;
                noBoard.hero = v.other.hero;
                double const threat = SimpleThreat(opWipe ? noBoard : v.other, v.self); // its minions die before they attack
                if (threat >= hp)
                    s -= 30.0;
                else
                {
                    s -= 0.25 * threat;
                    double const left = hp - threat;
                    if (left < 6.0)
                        s -= 0.8 * (6.0 - left);
                }
                return s;
            }
        }

        double SimpleEvalPosition(MatchView const& v, CardDb const& db)
        {
            return SimpleEval(v, db);
        }

        Action ChooseNormalPick(Match const& match, MatchView const& view, int side, CardDb const& db, uint32_t aiSeed)
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
                std::unique_ptr<Match> sim = match.RedactedCopy(side, Mix(aiSeed, 0x7C1u + static_cast<uint32_t>(i)));
                if (!sim)
                    continue;
                Action a;
                a.type = ActionType::Choose;
                a.source = view.pick[i].id;
                MatchView after;
                if (ApplyAndView(*sim, side, a, db, sims, after) != ActionError::None)
                    continue;
                double const sc = SimpleEval(after, db);
                if (sc > bestScore + 1e-9)
                {
                    bestScore = sc;
                    best.source = view.pick[i].id;
                }
            }
            return best;
        }
    }

    Action ChooseAiAction(Match const& match, int side, CardDb const& db, int actionsThisTurn, AiProfile profile,
        uint32_t aiSeed)
    {
        return ChooseAiAction(match, side, db, actionsThisTurn, profile, aiSeed, nullptr);
    }

    Action ChooseAiAction(Match const& match, int side, CardDb const& db, int actionsThisTurn, AiProfile profile,
        uint32_t aiSeed, AiDecisionInfo* info)
    {
        if (info)
            *info = AiDecisionInfo();
        if (side < 0 || side > 1)
            return EndTurnAction();
        MatchView const view = match.ViewFor(side, match.LastEventSeq());
        bool const hard = profile == AiProfile::Hard;
        if (view.mulliganPending)
            return hard ? AiDetail::ChooseHardMulligan(view, db) : AiDetail::SimpleMulligan(view, db);
        if (view.phase != Phase::Play)
            return EndTurnAction();
        if (view.pickPending && !view.pick.empty())
            return hard ? AiDetail::ChooseHardPick(match, view, side, db, aiSeed)
                        : AiDetail::ChooseNormalPick(match, view, side, db, aiSeed);
        if (view.activeSide != side || actionsThisTurn >= kAiMaxActionsPerTurn || view.legal.empty())
            return EndTurnAction();
        Action const chosen = hard ? AiDetail::ChooseHard(match, view, side, db, actionsThisTurn, aiSeed, info)
                                   : AiDetail::ChooseNormal(match, view, side, db, actionsThisTurn, aiSeed, info);
        Action wall;
        if (chosen.type == ActionType::EndTurn && AiDetail::BreakTaunt(view, wall))
            return wall;
        return chosen;
    }
}
}
