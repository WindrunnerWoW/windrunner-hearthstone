// Beast effect batch (16 pool minions, 5 tokens).
//
// Cards (normal id, name, tier, text); the golden version runs the same registry entry.
//
//   BG_CFM_315   Alleycat                   T1  Battlecry: Summon a 1/1 Cat.
//   BG_EX1_531   Scavenging Hyena           T1  Whenever a friendly Beast dies, gain +2/+1.
//   BG19_010     Sewer Rat                  T2  Deathrattle: Summon a 2/3 Turtle with Taunt.
//   BG21_000     Leapfrogger                T2  Deathrattle: Give a friendly Beast +1/+1 and this Deathrattle.
//   BGS_075      Rabid Saurolisk            T2  After you play a minion with Deathrattle, gain +1/+2.
//   BGS_078      Monstrous Macaw            T3  After this attacks, trigger another friendly minion's Deathrattle.
//   BG_CFM_316   Rat Pack                   T3  Deathrattle: Summon a number of 1/1 Rats equal to this minion's Attack.
//   BG21_003     Reanimating Rattler        T4  At the end of your turn, give another friendly Beast Reborn.
//   BG_EX1_534   Savannah Highmane          T4  Deathrattle: Summon two 2/2 Hyenas.
//   BG_LOOT_078  Cave Hydra                 T4  Also damages the minions next to whomever this attacks.
//   BG20_205     Agamaggan, the Great Boar  T5  Your Blood Gems give an extra +1/+1.
//   BG21_001     Palescale Crocolisk        T5  Avenge (2) and Deathrattle: Give another friendly Beast +6/+6.
//   BG22_001     Baby Krush                 T5  Whenever this attacks, summon an 8/8 Devilsaur to attack the target first.
//   BGS_021      Mama Bear                  T5  Whenever you summon a Beast, give it +3/+3.
//   BGS_008      Ghastcoiler                T6  Deathrattle: Summon 2 random Deathrattle minions.
//   BGS_018      Goldrinn, the Great Wolf   T6  Deathrattle: For the rest of this combat, your Beasts have +3/+3.
// Tokens (no effects, summoned or added by the cards above):
//   BG19_010t    Half-Shell                 2/3 Taunt
//   BG22_001t2   Devilsaur                  8/8 
//   BG_CFM_315t  Tabbycat                   1/1 
//   BG_CFM_316t  Rat                        1/1 
//   BG_EX1_534t  Hyena                      2/2 

#include "HsBgEffects.h"

#include <vector>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsBeast(Minion const& m) { return m.IsTribe(kTribeBeast); }

            // ---- Shop: Battlecries and shop-phase triggers ----

            // Alleycat: Battlecry: Summon a 1/1 Cat (golden: Tabbycat 2/2). To the right of the Alleycat; Khadgar doubles it.
            void AlleycatBattlecry(ShopCtx& c, bool golden)
            {
                c.Summon(FindCard("BG_CFM_315t"), golden, c.position + 1);
            }

            // Reanimating Rattler (render text): at the end of your turn, give another friendly Beast Reborn
            // (golden: 2 others). Beasts that lack Reborn first, so the trigger is not wasted.
            void RattlerEndOfTurn(ShopCtx& c, bool golden)
            {
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    Minion* t = c.PickOther([](Minion const& m) { return IsBeast(m) && !m.Has(kKwReborn); });
                    if (!t)
                        return;
                    c.GiveKeywords(*t, kKwReborn);
                }
            }

            // Rabid Saurolisk: after you play a minion with Deathrattle, gain +1/+2 (golden +2/+4). Reads the card that was
            // played, so a Magnetic Replicating Menace counts; summons do not (that is another hook).
            void SauroliskPlayed(ShopCtx& c, bool golden)
            {
                if (c.playedCard < 0)
                    return;
                EffectDef const* e = c.api.Effects().Find(c.playedCard);
                if (!e || !e->deathrattle)
                    return;
                Minion* self = c.Self();
                if (self)
                    c.Buff(*self, golden ? 2 : 1, golden ? 4 : 2);
            }

            // Mama Bear (shop phase): whenever you summon a Beast, give it +3/+3 permanently (golden +6/+6). Not herself;
            // a Beast played from the hand counts as summoned.
            void MamaBearSummonedShop(ShopCtx& c, bool golden)
            {
                if (c.other && c.other->IsTribe(kTribeBeast))
                    c.Buff(*c.other, golden ? 6 : 3, golden ? 6 : 3);
            }

            // ---- Combat ----

            // Scavenging Hyena: whenever a friendly Beast dies, gain +2/+1 (golden +4/+2).
            void HyenaFriendDied(CombatCtx& c, bool golden)
            {
                if (c.dead && c.dead->IsTribe(kTribeBeast) && c.api.Find(c.self))
                    c.api.Buff(c.self, golden ? 4 : 2, golden ? 2 : 1);
            }

            // Sewer Rat: Deathrattle: Summon a 2/3 Turtle with Taunt (golden Half-Shell 4/6).
            void SewerRatDeathrattle(CombatCtx& c, bool golden)
            {
                c.Summon(FindCard("BG19_010t"), golden);
            }

            // Leapfrogger: Deathrattle: Give a random other friendly Beast +1/+1 (golden +2/+2) and this Deathrattle. The
            // granted instance keeps the golden flag; a Beast that already has one gets a second instance.
            void LeapfroggerDeathrattle(CombatCtx& c, bool golden)
            {
                uint32_t const id = c.RandomOf(c.Friends(&IsBeast));
                if (id == 0)
                    return;
                c.api.Buff(id, golden ? 2 : 1, golden ? 2 : 1);
                Minion* m = c.api.Find(id);
                if (!m)
                    return;
                GrantedEffect g;
                g.card = FindCard("BG21_000");
                g.golden = golden;
                m->granted.push_back(g);
            }

            // Monstrous Macaw: after this attacks, trigger another friendly minion's Deathrattle (golden: twice, the same
            // minion). Baron, attached and granted Deathrattles are handled by the engine.
            void MacawAfterAttack(CombatCtx& c, bool golden)
            {
                std::vector<uint32_t> const ids = c.Friends([&c](Minion const& m) { return HasDeathrattle(c.api.Effects(), m); });
                uint32_t const t = c.RandomOf(ids);
                if (t == 0)
                    return;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    if (!c.api.TriggerDeathrattle(t))
                        break;
            }

            // Rat Pack: Deathrattle: Summon 1/1 Rats equal to this minion's Attack (golden Rats 2/2). The Attack at death
            // (auras and buffs included); the board cap and Khadgar apply to every Rat.
            void RatPackDeathrattle(CombatCtx& c, bool golden)
            {
                if (!c.dead)
                    return;
                int const n = c.dead->attack;
                int const rat = FindCard("BG_CFM_316t");
                for (int i = 0; i < n && i < kBoardMax; ++i)   // more than a board of Rats can never fit
                    c.Summon(rat, golden);
            }

            // Savannah Highmane: Deathrattle: Summon two 2/2 Hyenas (golden: golden Hyenas 4/4).
            void HighmaneDeathrattle(CombatCtx& c, bool golden)
            {
                int const hyena = FindCard("BG_EX1_534t");
                c.Summon(hyena, golden);
                c.Summon(hyena, golden);
            }

            // Palescale Crocolisk: Avenge (2) and Deathrattle: Give another friendly Beast +6/+6 (golden +12/+12).
            void CrocoliskGive(CombatCtx& c, bool golden)
            {
                uint32_t const id = c.RandomOf(c.Friends(&IsBeast));
                if (id != 0)
                    c.api.Buff(id, golden ? 12 : 6, golden ? 12 : 6);
            }

            // Baby Krush: whenever this attacks, summon an 8/8 Devilsaur (golden 16/16) to the right of it; the Devilsaur
            // attacks the same target first. Khadgar gives one Devilsaur per step. A full board: no Devilsaur.
            void BabyKrushAttack(CombatCtx& c, bool golden)
            {
                int const card = FindCard("BG22_001t2");
                int const times = c.SummonTimes();
                for (int i = 0; i < times; ++i)
                {
                    uint32_t const id = c.SummonOne(card, golden);
                    if (id == 0)
                    {
                        ++c.failedSummons;
                        continue;
                    }
                    c.api.ExtraAttack(id, c.other);
                }
            }

            // Mama Bear (combat): whenever you summon a Beast, give it +3/+3 (golden +6/+6); Reborn returns and
            // Deathrattle tokens count. The buff lasts for this combat.
            void MamaBearFriendSummoned(CombatCtx& c, bool golden)
            {
                Minion* m = c.api.Find(c.other);
                if (m && m->IsTribe(kTribeBeast))
                    c.api.Buff(c.other, golden ? 6 : 3, golden ? 6 : 3);
            }

            // Ghastcoiler: Deathrattle: Summon 2 random Deathrattle minions (golden 4). Normal copies from the lobby's pool
            // (not Ghastcoiler); Khadgar doubles each summon. The summoned minions do not trigger.
            void GhastcoilerDeathrattle(CombatCtx& c, bool golden)
            {
                int const self = c.dead ? c.dead->card : FindCard("BGS_008");
                for (int i = 0; i < (golden ? 4 : 2); ++i)
                {
                    int const card = c.api.RandomPoolCard(c.side, 0, true, self);
                    if (card >= 0)
                        c.Summon(card, false);
                }
            }

            // Goldrinn, the Great Wolf: Deathrattle: For the rest of this combat, your Beasts have +3/+3 (golden +6/+6).
            // A combat buff of the Beasts standing when it dies (combat buffs never outlive the fight).
            void GoldrinnDeathrattle(CombatCtx& c, bool golden)
            {
                for (uint32_t id : c.Friends(&IsBeast))
                    c.api.Buff(id, golden ? 6 : 3, golden ? 6 : 3);
            }

            void RegisterBeastEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.battlecry = &AlleycatBattlecry;
                r.Register("BG_CFM_315", d);
                d = EffectDef();
                d.onFriendDied = &HyenaFriendDied;
                r.Register("BG_EX1_531", d);
                d = EffectDef();
                d.deathrattle = &SewerRatDeathrattle;
                r.Register("BG19_010", d);
                d = EffectDef();
                d.deathrattle = &LeapfroggerDeathrattle;
                r.Register("BG21_000", d);
                d = EffectDef();
                d.onFriendlyPlayed = &SauroliskPlayed;
                r.Register("BGS_075", d);
                d = EffectDef();
                d.afterAttack = &MacawAfterAttack;
                r.Register("BGS_078", d);
                d = EffectDef();
                d.deathrattle = &RatPackDeathrattle;
                r.Register("BG_CFM_316", d);
                d = EffectDef();
                d.cleave = true;
                r.Register("BG_LOOT_078", d);
                d = EffectDef();
                d.onEndOfTurn = &RattlerEndOfTurn;
                r.Register("BG21_003", d);
                d = EffectDef();
                d.deathrattle = &HighmaneDeathrattle;
                r.Register("BG_EX1_534", d);
                d = EffectDef();
                d.avenge = &CrocoliskGive;
                d.avengeCount = 2;
                d.deathrattle = &CrocoliskGive;
                r.Register("BG21_001", d);
                d = EffectDef();
                d.onAttack = &BabyKrushAttack;
                r.Register("BG22_001", d);
                d = EffectDef();
                d.onFriendlySummonedShop = &MamaBearSummonedShop;
                d.onFriendSummoned = &MamaBearFriendSummoned;
                r.Register("BGS_021", d);
                d = EffectDef();
                d.deathrattle = &GhastcoilerDeathrattle;
                r.Register("BGS_008", d);
                d = EffectDef();
                d.deathrattle = &GoldrinnDeathrattle;
                r.Register("BGS_018", d);
                d = EffectDef();
                d.bloodGemAttack = 1;
                d.bloodGemHealth = 1;
                d.bloodGemAttackGolden = 2;
                d.bloodGemHealthGolden = 2;
                r.Register("BG20_205", d);   // Agamaggan raises Blood Gems
            }

            HS_BG_EFFECT_BATCH(Beast, &RegisterBeastEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Beast() {}
    }
}
