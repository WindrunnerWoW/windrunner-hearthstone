// Quilboar and Blood Gems (16 Quilboar pool minions, Prophet of the Boar and the Blood Gem spell).
//
//   BG20_GEM     Blood Gem              0   Give a minion +1/+1 (Agamaggan raises it; Necrolyte moves its stats).
//   BG20_100     Razorfen Geomancer     T1  Battlecry: get a Blood Gem (golden: two).
//   BG20_101     Roadboar               T2  Frenzy: get a Blood Gem (golden: two).
//   BG20_102     Tough Tusk             T2  After a Blood Gem is played on this, gain Divine Shield until next turn.
//   BG20_103     Bristleback Brute      T3  The first Blood Gem on this each turn gives an extra +3/+3 (golden +6/+6).
//   BG20_104     Bonker                 T4  Windfury. After this attacks, get a Blood Gem (golden: two).
//   BG20_105     Thorncaller            T3  Deathrattle: get a Blood Gem (golden: two).
//   BG20_106     Groundshaker           T4  After a Blood Gem is played on this, your other minions get +2 Attack
//                                            until next turn (golden +4).
//   BG20_201     Bannerboar             T3  End of turn: play a Blood Gem on adjacent minions (golden: two each).
//   BG20_202     Necrolyte              T4  Battlecry: play 2 Blood Gems on a friendly minion (golden: 4); it steals
//                                            the Blood Gems of its neighbours.
//   BG20_203     Prophet of the Boar    T2  Taunt. After you play a Quilboar, get a Blood Gem (golden: two).
//   BG20_204     Bristleback Knight     T5  Windfury, Divine Shield. The first time it survives damage, gain Divine
//                                            Shield (golden: the first two times).
//   BG20_205     Agamaggan              T5  Your Blood Gems give an extra +1/+1 (golden +2/+2; registered in the Beast
//                                            batch, the fields live here).
//   BG20_207     Dynamic Duo            T4  Taunt. After a Blood Gem is played on another friendly Quilboar, +1/+1.
//   BG20_301     Sun-Bacon Relaxer      T1  When you sell this, get 2 Blood Gems (golden: 4).
//   BG20_302     Aggem Thorncurse       T5  After a Blood Gem is played on this, give a friendly minion of each type
//                                            +2/+1 (golden +4/+2).
//   BG20_303     Charlga                T6  End of turn: play a Blood Gem on your other minions (golden: two each).
//   BG21_037     Gemsplitter            T3  After a friendly minion loses Divine Shield, get a Blood Gem (golden: two).
//   BG23_018     Darkgaze Elder         T6  After you spend 5 Gold, play a Blood Gem on four friendly Quilboar
//                                            (golden: twice).
//
// The cards follow their hover renders (render_overrides.json). The engine support (PlayBloodGem, the onBloodGem /
// onBloodGemPlayed / onSpendGold hooks and the Blood Gem stat counters) is in HsBgTypes.h / HsBgEffects.h /
// HsBgShop.cpp.

#include "HsBgEffects.h"

#include <algorithm>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsQuilboar(Minion const& m) { return m.IsTribe(kTribeQuilboar); }

            void AddBloodGems(ShopCtx& c, int count)
            {
                int const gem = FindCard("BG20_GEM");
                for (int i = 0; i < count; ++i)
                    c.api.AddSpellToHand(c.player, gem, false, false, false);
            }

            // The Blood Gem's own effect: +1/+1 plus every Agamaggan on the board. The stats are recorded on the
            // minion so Necrolyte can move them (a triple sums them too).
            void BloodGem(ShopCtx& c, bool)
            {
                if (!c.target)
                    return;
                int attack = 1;
                int health = 1;
                for (Minion const& m : c.PlayerBoard().Items())
                {
                    EffectDef const* const e = c.api.Effects().Find(m.card);
                    if (!e)
                        continue;
                    attack += m.golden && e->bloodGemAttackGolden != 0 ? e->bloodGemAttackGolden : e->bloodGemAttack;
                    health += m.golden && e->bloodGemHealthGolden != 0 ? e->bloodGemHealthGolden : e->bloodGemHealth;
                }
                uint32_t const uid = c.target->uid;
                c.api.SpellBuff(c.player, *c.target, attack, health, true);
                if (Minion* after = c.api.FindMinion(c.player, uid))
                {
                    after->bloodGemAttack += attack;
                    after->bloodGemHealth += health;
                }
            }

            // ---- shop effects ----

            void AddOneGemBattlecry(ShopCtx& c, bool golden) { AddBloodGems(c, golden ? 2 : 1); }

            // Necrolyte: 2 gems (golden 4) on a friendly minion, then its neighbours' gem stats move onto it.
            void NecrolyteBattlecry(ShopCtx& c, bool golden)
            {
                Board& board = c.PlayerBoard();
                if (board.Empty())
                    return;
                Minion* target = c.target;
                if (!target || board.IndexOfUid(target->uid) < 0)
                    target = &board[static_cast<int>(c.Random().Uniform(static_cast<uint32_t>(board.Size())))];
                uint32_t const targetUid = target->uid;
                int const gems = golden ? 4 : 2;
                for (int i = 0; i < gems; ++i)
                {
                    Minion* t = c.api.FindMinion(c.player, targetUid);
                    if (!t)
                        return;
                    c.PlayBloodGem(*t);
                }
                int const pos = board.IndexOfUid(targetUid);
                if (pos < 0)
                    return;
                std::vector<uint32_t> neighbours;
                if (pos > 0)
                    neighbours.push_back(board[pos - 1].uid);
                if (pos + 1 < board.Size())
                    neighbours.push_back(board[pos + 1].uid);
                int stolenAttack = 0;
                int stolenHealth = 0;
                for (uint32_t uid : neighbours)
                {
                    Minion* n = c.api.FindMinion(c.player, uid);
                    if (!n)
                        continue;
                    // Steal only what can be removed: the gem stats may exceed the current stats after a stat-setting
                    // effect, and the neighbour keeps Attack >= 0 and Health >= 1.
                    int const a = std::max(0, std::min(n->bloodGemAttack, n->attack));
                    int const h = std::max(0, std::min(n->bloodGemHealth, n->health - 1));
                    n->bloodGemAttack = n->bloodGemHealth = 0;
                    if (a == 0 && h == 0)
                        continue;
                    c.api.Buff(c.player, *n, -a, -h);
                    stolenAttack += a;
                    stolenHealth += h;
                }
                if ((stolenAttack != 0 || stolenHealth != 0))
                    if (Minion* t = c.api.FindMinion(c.player, targetUid))
                    {
                        c.api.SpellBuff(c.player, *t, stolenAttack, stolenHealth, true);
                        t->bloodGemAttack += stolenAttack;
                        t->bloodGemHealth += stolenHealth;
                    }
            }

            void SunBaconSell(ShopCtx& c, bool golden) { AddBloodGems(c, golden ? 4 : 2); }

            void ProphetPlayed(ShopCtx& c, bool golden)
            {
                if (c.other && c.other->IsTribe(kTribeQuilboar))
                    AddBloodGems(c, golden ? 2 : 1);
            }

            // "After a Blood Gem is played on this" (the hook fires on the gem's target).
            void ToughTuskGem(ShopCtx& c, bool)
            {
                if (Minion* self = c.Self())
                    c.api.SpellKeywords(c.player, *self, kKwDivineShield, false);
            }

            void BristlebackBruteGem(ShopCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (!self || self->counters[kCounterOncePerTurn] != 0)
                    return;
                self->counters[kCounterOncePerTurn] = 1;
                c.api.Buff(c.player, *self, golden ? 6 : 3, golden ? 6 : 3);
            }

            void GroundshakerGem(ShopCtx& c, bool golden)
            {
                int const attack = golden ? 4 : 2;
                std::vector<uint32_t> others;
                for (Minion const& m : c.PlayerBoard().Items())
                    if (m.uid != c.selfUid)
                        others.push_back(m.uid);
                for (uint32_t uid : others)
                    if (Minion* m = c.api.FindMinion(c.player, uid))
                        c.api.SpellBuff(c.player, *m, attack, 0, false);
            }

            void AggemGem(ShopCtx& c, bool golden)
            {
                Board const& board = c.PlayerBoard();
                std::vector<int> const picks = PickDistinctTypes(TribeMasks(board), c.Random(), 9);
                for (int index : picks)
                    if (index >= 0 && index < board.Size())
                        c.api.Buff(c.player, c.PlayerBoard()[index], golden ? 4 : 2, golden ? 2 : 1);
            }

            void DynamicDuoGemPlayed(ShopCtx& c, bool golden)
            {
                if (!c.other || c.other->uid == c.selfUid || !IsQuilboar(*c.other))
                    return;
                if (Minion* self = c.Self())
                    c.api.Buff(c.player, *self, golden ? 2 : 1, golden ? 2 : 1);
            }

            // Bannerboar: one gem (golden: two) on each adjacent minion at the end of the turn.
            void BannerboarEnd(ShopCtx& c, bool golden)
            {
                Board& board = c.PlayerBoard();
                int const pos = board.IndexOfUid(c.selfUid);
                if (pos < 0)
                    return;
                std::vector<uint32_t> adjacent;
                if (pos > 0)
                    adjacent.push_back(board[pos - 1].uid);
                if (pos + 1 < board.Size())
                    adjacent.push_back(board[pos + 1].uid);
                for (int round = 0; round < (golden ? 2 : 1); ++round)
                    for (uint32_t uid : adjacent)
                        if (Minion* m = c.api.FindMinion(c.player, uid))
                            c.PlayBloodGem(*m);
            }

            // Charlga: one gem (golden: two) on every other friendly minion at the end of the turn.
            void CharlgaEnd(ShopCtx& c, bool golden)
            {
                std::vector<uint32_t> others;
                for (Minion const& m : c.PlayerBoard().Items())
                    if (m.uid != c.selfUid)
                        others.push_back(m.uid);
                for (int round = 0; round < (golden ? 2 : 1); ++round)
                    for (uint32_t uid : others)
                        if (Minion* m = c.api.FindMinion(c.player, uid))
                            c.PlayBloodGem(*m);
            }

            // Darkgaze Elder: every 5 Gold spent, one gem on four random friendly Quilboar (golden: twice each).
            void DarkgazeSpend(ShopCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (!self)
                    return;
                self->counters[1] += c.goldSpent;
                while (self->counters[1] >= 5)
                {
                    self->counters[1] -= 5;
                    std::vector<uint32_t> boars;
                    for (Minion const& m : c.PlayerBoard().Items())
                        if (IsQuilboar(m))
                            boars.push_back(m.uid);
                    c.Random().Shuffle(boars);
                    if (boars.size() > 4)
                        boars.resize(4);
                    for (int round = 0; round < (golden ? 2 : 1); ++round)
                        for (uint32_t uid : boars)
                            if (Minion* m = c.api.FindMinion(c.player, uid))
                                c.PlayBloodGem(*m);
                    self = c.Self();
                    if (!self)
                        return;
                }
            }

            // ---- combat effects ----

            // Frenzy: the first time it survives damage (golden Bristleback Knight: the first two).
            void RoadboarFrenzy(CombatCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (c.other != c.self || !self || self->health <= 0 || (self->flags & kMinionDestroyed) != 0 ||
                    self->counters[kCounterFrenzy] != 0)
                    return;
                self->counters[kCounterFrenzy] = 1;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.RecordPersistentAddToHand(c.side, FindCard("BG20_GEM"), false);
            }

            void BristlebackKnightFrenzy(CombatCtx& c, bool golden)
            {
                Minion* self = c.Self();
                int const allowed = golden ? 2 : 1;
                if (c.other != c.self || !self || self->health <= 0 || (self->flags & kMinionDestroyed) != 0 ||
                    self->counters[kCounterFrenzy] >= allowed)
                    return;
                ++self->counters[kCounterFrenzy];
                c.api.GrantKeywords(c.self, kKwDivineShield);
            }

            void BonkerAfterAttack(CombatCtx& c, bool golden)
            {
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.RecordPersistentAddToHand(c.side, FindCard("BG20_GEM"), false);
            }

            void ThorncallerDeathrattle(CombatCtx& c, bool golden)
            {
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.RecordPersistentAddToHand(c.side, FindCard("BG20_GEM"), false);
            }

            void GemsplitterShieldLost(CombatCtx& c, bool golden)
            {
                if (!c.api.Find(c.other))
                    return;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.RecordPersistentAddToHand(c.side, FindCard("BG20_GEM"), false);
            }

            void RegisterQuilboarEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onCast = &BloodGem;
                d.spellNeedsTarget = true;
                r.Register("BG20_GEM", d);
                d = EffectDef();
                d.battlecry = &AddOneGemBattlecry;
                r.Register("BG20_100", d);
                d = EffectDef();
                d.onDamaged = &RoadboarFrenzy;
                r.Register("BG20_101", d);
                d = EffectDef();
                d.onBloodGem = &ToughTuskGem;
                r.Register("BG20_102", d);
                d = EffectDef();
                d.onBloodGem = &BristlebackBruteGem;
                r.Register("BG20_103", d);
                d = EffectDef();
                d.afterAttack = &BonkerAfterAttack;
                r.Register("BG20_104", d);
                d = EffectDef();
                d.deathrattle = &ThorncallerDeathrattle;
                r.Register("BG20_105", d);
                d = EffectDef();
                d.onBloodGem = &GroundshakerGem;
                r.Register("BG20_106", d);
                d = EffectDef();
                d.onEndOfTurn = &BannerboarEnd;
                r.Register("BG20_201", d);
                d = EffectDef();
                d.battlecry = &NecrolyteBattlecry;
                r.Register("BG20_202", d);
                d = EffectDef();
                d.onFriendlyPlayed = &ProphetPlayed;
                r.Register("BG20_203", d);
                d = EffectDef();
                d.onDamaged = &BristlebackKnightFrenzy;
                r.Register("BG20_204", d);
                d = EffectDef();
                d.onBloodGemPlayed = &DynamicDuoGemPlayed;
                r.Register("BG20_207", d);
                d = EffectDef();
                d.onSell = &SunBaconSell;
                r.Register("BG20_301", d);
                d = EffectDef();
                d.onBloodGem = &AggemGem;
                r.Register("BG20_302", d);
                d = EffectDef();
                d.onEndOfTurn = &CharlgaEnd;
                r.Register("BG20_303", d);
                d = EffectDef();
                d.onShieldLost = &GemsplitterShieldLost;
                r.Register("BG21_037", d);
                d = EffectDef();
                d.onSpendGold = &DarkgazeSpend;
                r.Register("BG23_018", d);
            }

            HS_BG_EFFECT_BATCH(Quilboar, &RegisterQuilboarEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h).
        void HsBgBatchAnchor_Quilboar() {}
    }
}
