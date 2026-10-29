// Mech effect batch (15 pool minions, 2 tokens).
//
// Cards (normal id, name, tier, text); the golden version runs the same registry entry.
//
//   BG21_022     Pupbot                     T1  Divine Shield
//   BG_ULD_217   Micro Mummy                T1  Reborn At the end of your turn, give another random friendly minion +1 Attack.
//   BG_BOT_606   Kaboom Bot                 T2  Deathrattle: Deal 4 damage to a random enemy minion.
//   BG_EX1_556   Harvest Golem              T2  Deathrattle: Summon a 2/1 Damaged Golem.
//   BG_GVG_048   Metaltooth Leaper          T2  Battlecry: Give your other Mechs +2 Attack.
//   BGS_071      Deflect-o-Bot              T3  Divine Shield Whenever you summon a Mech during combat, gain +2 Attack and Divine Shield.
//   BG_BOT_312   Replicating Menace         T3  Magnetic Deathrattle: Summon three 1/1 Microbots.
//   BG_GVG_027   Iron Sensei                T3  At the end of your turn, give another friendly Mech +2/+2.
//   BG21_023     Mechano-Tank               T4  Avenge (2): Deal 5 damage to the highest Health enemy minion.
//   BG_BOT_563   Wargear                    T4  Magnetic
//   BG_BOT_911   Annoy-o-Module             T4  Magnetic Divine Shield Taunt
//   BG20_401     Holy Mecherel              T5  After another friendly minion loses Divine Shield, gain Divine Shield.
//   BG21_024     Grease Bot                 T6  After a friendly minion loses Divine Shield, give it +2/+2 permanently.
//   BG21_025     Omega Buster               T6  Deathrattle: Summon six 1/1 Microbots. For each that doesn't fit, give your Mechs +1/+1.
//   BG_GVG_113   Foe Reaper 4000            T6  Also damages the minions next to whomever it attacks.
// Tokens (no effects, summoned or added by the cards above):
//   BG_BOT_312t  Microbot                   1/1 
//   BG_EX1_556t  Damaged Golem              2/1 

#include "HsBgEffects.h"

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsMech(Minion const& m) { return m.IsTribe(kTribeMech); }
            bool AnyMinion(Minion const&) { return true; }

            // Micro Mummy: +1 Attack (golden +2) to another random friendly minion, permanent in the shop.
            void MicroMummyEndOfTurn(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickOther(&AnyMinion);
                if (t)
                    c.Buff(*t, golden ? 2 : 1, 0);
            }

            // Kaboom Bot: 4 damage to a random enemy minion (golden: twice, a fresh pick each time).
            void KaboomBotDeathrattle(CombatCtx& c, bool golden)
            {
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    uint32_t const t = c.RandomOf(c.Enemies(&AnyMinion));
                    if (t != 0)
                        c.api.Damage(c.self, t, 4);
                }
            }

            // Harvest Golem: a Damaged Golem (golden: a golden one, 4/2).
            void HarvestGolemDeathrattle(CombatCtx& c, bool golden) { c.Summon(FindCard("BG_EX1_556t"), golden); }

            // Metaltooth Leaper: +2 Attack (golden +4) to the other friendly Mechs on the board.
            void MetaltoothBattlecry(ShopCtx& c, bool golden)
            {
                Board& b = c.PlayerBoard();
                for (int i = 0; i < b.Size(); ++i)
                    if (b[i].uid != c.selfUid && IsMech(b[i]))
                        c.Buff(b[i], golden ? 4 : 2, 0);
            }

            // Replicating Menace (also as a Magnetic attachment, the engine passes the attachment's golden flag).
            void ReplicatingMenaceDeathrattle(CombatCtx& c, bool golden)
            {
                int const microbot = FindCard("BG_BOT_312t");
                for (int i = 0; i < 3; ++i)
                    c.Summon(microbot, golden);
            }

            // Iron Sensei: another random friendly Mech gets +2/+2 (golden +4/+4).
            void IronSenseiEndOfTurn(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickOther(&IsMech);
                if (t)
                    c.Buff(*t, golden ? 4 : 2, golden ? 4 : 2);
            }

            // Deflect-o-Bot: whenever another Mech is summoned in combat, +2 Attack (golden +4) and Divine Shield.
            void DeflectOBotFriendSummoned(CombatCtx& c, bool golden)
            {
                Minion const* o = c.api.Find(c.other);
                if (!o || !IsMech(*o))
                    return;
                c.api.Buff(c.self, golden ? 4 : 2, 0);
                c.api.GrantKeywords(c.self, kKwDivineShield);
            }

            // Mechano-Tank: Avenge (2): 5 damage to the highest Health enemy minion (golden: twice, re-evaluated).
            void MechanoTankAvenge(CombatCtx& c, bool golden)
            {
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    int best = -1;
                    std::vector<uint32_t> top;
                    for (uint32_t const id : c.Enemies(&AnyMinion))
                    {
                        int const h = c.api.Find(id)->health;
                        if (h > best)
                        {
                            best = h;
                            top.clear();
                        }
                        if (h == best)
                            top.push_back(id);
                    }
                    uint32_t const t = c.RandomOf(top);
                    if (t != 0)
                        c.api.Damage(c.self, t, 5);
                }
            }

            // Holy Mecherel: after another friendly minion loses Divine Shield, gain Divine Shield.
            void HolyMecherelShieldLost(CombatCtx& c, bool)
            {
                if (c.other != c.self)
                    c.api.GrantKeywords(c.self, kKwDivineShield);
            }

            // Grease Bot: after a friendly minion (itself too) loses Divine Shield, it gets +2/+2 (golden +4/+4) permanently.
            void GreaseBotShieldLost(CombatCtx& c, bool golden)
            {
                if (c.api.Find(c.other))
                    c.api.BuffPermanent(c.other, golden ? 4 : 2, golden ? 4 : 2);
            }

            // Omega Buster: six Microbots (golden: golden ones); +1/+1 (golden +2/+2) to your Mechs for each that did not fit.
            void OmegaBusterDeathrattle(CombatCtx& c, bool golden)
            {
                int const microbot = FindCard("BG_BOT_312t");
                for (int i = 0; i < 6; ++i)
                    c.Summon(microbot, golden);
                int const failed = c.failedSummons;
                if (failed <= 0)
                    return;
                for (uint32_t const id : c.Friends(&IsMech, true))
                    c.api.Buff(id, failed * (golden ? 2 : 1), failed * (golden ? 2 : 1));
            }

            void RegisterMechEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onEndOfTurn = &MicroMummyEndOfTurn;
                r.Register("BG_ULD_217", d);
                d = EffectDef();
                d.deathrattle = &KaboomBotDeathrattle;
                r.Register("BG_BOT_606", d);
                d = EffectDef();
                d.deathrattle = &HarvestGolemDeathrattle;
                r.Register("BG_EX1_556", d);
                d = EffectDef();
                d.battlecry = &MetaltoothBattlecry;
                r.Register("BG_GVG_048", d);
                d = EffectDef();
                d.deathrattle = &ReplicatingMenaceDeathrattle;
                r.Register("BG_BOT_312", d);
                d = EffectDef();
                d.onEndOfTurn = &IronSenseiEndOfTurn;
                r.Register("BG_GVG_027", d);
                d = EffectDef();
                d.onFriendSummoned = &DeflectOBotFriendSummoned;
                r.Register("BGS_071", d);
                d = EffectDef();
                d.avenge = &MechanoTankAvenge;
                d.avengeCount = 2;
                r.Register("BG21_023", d);
                d = EffectDef();
                d.onShieldLost = &HolyMecherelShieldLost;
                r.Register("BG20_401", d);
                d = EffectDef();
                d.onShieldLost = &GreaseBotShieldLost;
                r.Register("BG21_024", d);
                d = EffectDef();
                d.deathrattle = &OmegaBusterDeathrattle;
                r.Register("BG21_025", d);
                d = EffectDef();
                d.cleave = true;
                r.Register("BG_GVG_113", d);
            }

            HS_BG_EFFECT_BATCH(Mech, &RegisterMechEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Mech() {}
    }
}
