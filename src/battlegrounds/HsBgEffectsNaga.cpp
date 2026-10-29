// Naga and the spell system (15 Naga pool minions, Orgozoa, 9 spell cards).
//
//   BG23_000     Mini-Myrmidon          T1  Spellcraft: +2 Attack until next turn.
//   BG23_001     Snail Cavalry          T2  Once per turn, after you cast a spell, gain +2 Health.
//   BG23_002     Shell Collector        T1  Battlecry: get a Gold Coin (golden: two).
//   BG23_003     Critter Wrangler       T5  After you cast a Spellcraft spell on a minion, give it +1/+2.
//   BG23_004     Deep-Sea Angler        T2  Spellcraft: +4 Health and Taunt until next turn.
//   BG23_006     Eelbound Archer        T4  Spellcraft: +8 Attack until next turn.
//   BG23_007     Waverider              T4  Spellcraft: +2/+2 and Windfury until next turn.
//   BG23_008     Glowscale              T5  Taunt. Spellcraft: Divine Shield until next turn.
//   BG23_009     Lava Lurker            T2  The first Spellcraft spell on this each turn is permanent.
//   BG23_010     Eventide Brute         T4  After you cast a spell, gain +1/+1.
//   BG23_011     Shoal Commander        T3  Spellcraft: +1/+1 for each friendly Naga until next turn.
//   BG23_012     Corrupted Myrmidon     T5  Start of Combat: double this minion's stats.
//   BG23_013     Tidemistress Athissa   T6  After you cast a spell, give four friendly Naga +1/+1.
//   BG23_014     Pashmar the Vengeful   T3  Avenge (3): get a Spellcraft spell of your Tier or lower.
//   BG23_015     Orgozoa, the Tender    T6  Spellcraft: Discover a Naga.
//   BGS_200      Warden of Old          T3  Deathrattle: get a Gold Coin (golden: two).
//
// Spells:
//   BG23_000t Mini-Trident, BG23_004t Angler's Lure, BG23_006t Eelectrify, BG23_007t Undersea Mount,
//   BG23_008t Glowing Crown, BG23_011t Mighty Shoal, BG23_015t Azshara's Hatchery, TB_BaconShop_HP_008a Gold Coin,
//   TB_BaconShop_HP_038t Wild Banana.
//
// The cards follow their hover renders (render_overrides.json). The engine support (temporary stats / keywords,
// temporary Spellcraft cards, the cast action, the onSpellCast / onCast hooks and the persistent spell add) is in
// HsBgTypes.h / HsBgEffects.h / HsBgShop.cpp / HsBgGame.cpp / HsBgCombat.cpp.

#include "HsBgEffects.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsNaga(Minion const& m) { return m.IsTribe(kTribeNaga); }

            // ---- spell effects (onCast: the spell's own effect) ----

            void MiniTrident(ShopCtx& c, bool golden)
            {
                if (c.target)
                    c.SpellBuff(*c.target, golden ? 4 : 2, 0);
            }

            void AnglersLure(ShopCtx& c, bool golden)
            {
                if (!c.target)
                    return;
                c.SpellBuff(*c.target, 0, golden ? 8 : 4);
                c.SpellKeywords(*c.target, kKwTaunt);
            }

            void Eelectrify(ShopCtx& c, bool golden)
            {
                if (c.target)
                    c.SpellBuff(*c.target, golden ? 16 : 8, 0);
            }

            void UnderseaMount(ShopCtx& c, bool golden)
            {
                if (!c.target)
                    return;
                c.SpellBuff(*c.target, golden ? 4 : 2, golden ? 4 : 2);
                c.SpellKeywords(*c.target, kKwWindfury);
            }

            void GlowingCrown(ShopCtx& c, bool golden)
            {
                (void)golden;
                if (c.target)
                    c.SpellKeywords(*c.target, kKwDivineShield);
            }

            void MightyShoal(ShopCtx& c, bool golden)
            {
                if (!c.target)
                    return;
                int const naga = CountTribe(c.PlayerBoard(), kTribeNaga);
                int const per = golden ? 2 : 1;
                c.SpellBuff(*c.target, per * naga, per * naga);
            }

            // Orgozoa's Spellcraft: Discover a Naga (golden: two).
            void AzsharasHatchery(ShopCtx& c, bool golden)
            {
                int const times = golden ? 2 : 1;
                for (int i = 0; i < times; ++i)
                    c.api.Discover(c.player, kMinTier, c.Tier(), kTribeNaga);
            }

            void GoldCoin(ShopCtx& c, bool golden)
            {
                (void)golden;
                c.AddGold(1);
            }

            void WildBanana(ShopCtx& c, bool golden)
            {
                if (c.target)
                    c.SpellBuff(*c.target, golden ? 2 : 1, golden ? 2 : 1);
            }

            // ---- shop effects ----

            void ShellCollectorBattlecry(ShopCtx& c, bool golden)
            {
                int const coins = golden ? 2 : 1;
                for (int i = 0; i < coins; ++i)
                    c.api.AddSpellToHand(c.player, FindCard("TB_BaconShop_HP_008a"), false, false, false);
            }

            // "After you cast a spell": Snail Cavalry is once per turn.
            void SnailSpellCast(ShopCtx& c, bool golden)
            {
                Minion* const self = c.Self();
                if (!self || self->counters[kCounterOncePerTurn] != 0)
                    return;
                self->counters[kCounterOncePerTurn] = 1;
                c.api.Buff(c.player, *self, 0, golden ? 4 : 2);
            }

            void EventideSpellCast(ShopCtx& c, bool golden)
            {
                c.api.Buff(c.player, *c.Self(), golden ? 2 : 1, golden ? 2 : 1);
            }

            // Athissa: four friendly Naga +1/+1 (golden +2/+2), herself included.
            void AthissaSpellCast(ShopCtx& c, bool golden)
            {
                std::vector<uint32_t> naga;
                for (Minion const& m : c.PlayerBoard().Items())
                    if (IsNaga(m))
                        naga.push_back(m.uid);
                c.Random().Shuffle(naga);
                if (static_cast<int>(naga.size()) > 4)
                    naga.resize(4);
                for (uint32_t id : naga)
                    c.BuffUid(id, golden ? 2 : 1, golden ? 2 : 1);
            }

            // Critter Wrangler: a Spellcraft spell cast on a minion gives it +1/+2 (golden +2/+4).
            void CritterWranglerSpellCast(ShopCtx& c, bool golden)
            {
                if (!c.spellcraft || !c.target)
                    return;
                c.api.Buff(c.player, *c.target, golden ? 2 : 1, golden ? 4 : 2);
            }

            // ---- combat effects ----

            // Corrupted Myrmidon: Start of Combat doubles its stats (golden: triples).
            void CorruptedMyrmidonStartOfCombat(CombatCtx& c, bool golden)
            {
                Minion* const self = c.Self();
                if (!self)
                    return;
                int const n = golden ? 2 : 1;
                c.api.Buff(c.self, self->attack * n, self->health * n);
            }

            // Pashmar: Avenge (3): get a Spellcraft spell of the player's Tier or lower (golden: two).
            void PashmarAvenge(CombatCtx& c, bool golden)
            {
                std::vector<int> spells = SpellcraftSpells(c.api.TavernTier(c.side));
                if (spells.empty())
                    return;
                int const times = golden ? 2 : 1;
                for (int i = 0; i < times; ++i)
                {
                    int const card = spells[c.api.Random().Uniform(static_cast<uint32_t>(spells.size()))];
                    c.api.RecordPersistentAddToHand(c.side, card, false);
                }
            }

            // Warden of Old: Deathrattle: get a Gold Coin (golden: two).
            void WardenDeathrattle(CombatCtx& c, bool golden)
            {
                int const coins = golden ? 2 : 1;
                for (int i = 0; i < coins; ++i)
                    c.api.RecordPersistentAddToHand(c.side, FindCard("TB_BaconShop_HP_008a"), false);
            }

            void RegisterNagaEffects(EffectRegistry& r)
            {
                // Spells first: the onCast effect and the Spellcraft flag (Lava Lurker / Vashj / Critter Wrangler).
                EffectDef d;
                d.onCast = &MiniTrident;
                d.isSpellcraftSpell = true;
                d.spellNeedsTarget = true;
                r.Register("BG23_000t", d);
                d = EffectDef();
                d.onCast = &AnglersLure;
                d.isSpellcraftSpell = true;
                d.spellNeedsTarget = true;
                r.Register("BG23_004t", d);
                d = EffectDef();
                d.onCast = &Eelectrify;
                d.isSpellcraftSpell = true;
                d.spellNeedsTarget = true;
                r.Register("BG23_006t", d);
                d = EffectDef();
                d.onCast = &UnderseaMount;
                d.isSpellcraftSpell = true;
                d.spellNeedsTarget = true;
                r.Register("BG23_007t", d);
                d = EffectDef();
                d.onCast = &GlowingCrown;
                d.isSpellcraftSpell = true;
                d.spellNeedsTarget = true;
                r.Register("BG23_008t", d);
                d = EffectDef();
                d.onCast = &MightyShoal;
                d.isSpellcraftSpell = true;
                d.spellNeedsTarget = true;
                r.Register("BG23_011t", d);
                d = EffectDef();
                d.onCast = &AzsharasHatchery;
                d.isSpellcraftSpell = true;
                r.Register("BG23_015t", d);
                d = EffectDef();
                d.onCast = &GoldCoin;
                r.Register("TB_BaconShop_HP_008a", d);
                d = EffectDef();
                d.onCast = &WildBanana;
                d.spellNeedsTarget = true;
                d.spellTargetTribe = kTribeBeast;
                r.Register("TB_BaconShop_HP_038t", d);

                // Minions.
                d = EffectDef();
                d.grantsSpellcraft = "BG23_000t";
                r.Register("BG23_000", d);
                d = EffectDef();
                d.onSpellCast = &SnailSpellCast;
                r.Register("BG23_001", d);
                d = EffectDef();
                d.battlecry = &ShellCollectorBattlecry;
                r.Register("BG23_002", d);
                d = EffectDef();
                d.onSpellCast = &CritterWranglerSpellCast;
                r.Register("BG23_003", d);
                d = EffectDef();
                d.grantsSpellcraft = "BG23_004t";
                r.Register("BG23_004", d);
                d = EffectDef();
                d.grantsSpellcraft = "BG23_006t";
                r.Register("BG23_006", d);
                d = EffectDef();
                d.grantsSpellcraft = "BG23_007t";
                r.Register("BG23_007", d);
                d = EffectDef();
                d.grantsSpellcraft = "BG23_008t";
                r.Register("BG23_008", d);
                d = EffectDef();
                d.spellcraftPermanentPerTurn = 1;
                d.spellcraftPermanentPerTurnGolden = 2;
                r.Register("BG23_009", d);
                d = EffectDef();
                d.onSpellCast = &EventideSpellCast;
                r.Register("BG23_010", d);
                d = EffectDef();
                d.grantsSpellcraft = "BG23_011t";
                r.Register("BG23_011", d);
                d = EffectDef();
                d.startOfCombat = &CorruptedMyrmidonStartOfCombat;
                r.Register("BG23_012", d);
                d = EffectDef();
                d.onSpellCast = &AthissaSpellCast;
                r.Register("BG23_013", d);
                d = EffectDef();
                d.avenge = &PashmarAvenge;
                d.avengeCount = 3;
                d.avengeCountGolden = 3;
                r.Register("BG23_014", d);
                d = EffectDef();
                d.grantsSpellcraft = "BG23_015t";
                r.Register("BG23_015", d);
                d = EffectDef();
                d.deathrattle = &WardenDeathrattle;
                r.Register("BGS_200", d);
            }

            HS_BG_EFFECT_BATCH(Naga, &RegisterNagaEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h).
        void HsBgBatchAnchor_Naga() {}
    }
}
