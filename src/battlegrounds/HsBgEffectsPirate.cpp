// Pirate effect batch (15 pool minions, 1 token).
//
//   BG21_016     Peggy Brittlebone        T4  After a card is added to your hand, another friendly Pirate gets +1/+1.
//   BG21_019     Nosy Looter             T6  Every two turns, add a random Golden minion to your hand (golden: every turn).
//   BG21_031     Tony Two-Tusk           T5  Avenge (4): make another friendly Pirate Golden permanently.
//   BG23_192     First Mate Pip          T3  Two copies make it Golden.
//   BGS_047      Dread Admiral Eliza     T6  Whenever a friendly Pirate attacks, all friendly minions get +2/+1.
//   BGS_048      Southsea Strongarm      T3  Battlecry: a friendly Pirate gets +1/+1, once more per Pirate bought this turn.
//   BGS_049      Freedealing Gambler    T2  Sells for 3 Gold (golden 6).
//   BGS_055      Deck Swabbie            T1  Battlecry: the next upgrade costs (1) less (golden (2)).
//   BGS_056      Ripsnarl Captain        T4  Whenever a friendly Pirate attacks, give it +3 Attack.
//   BGS_060      Yo-Ho-Ogre              T2  Taunt. After it survives being attacked, attack immediately.
//   BGS_061      Scallywag               T1  Deathrattle: summon a 1/1 Pirate that attacks immediately.
//   BGS_066      Goldgrubber             T4  End of turn: +2/+2 for each friendly Golden minion.
//   BGS_072      Cap'n Hoggarr           T5  Whenever you buy a Pirate, gain 1 Gold.
//   BGS_081      Salty Looter            T3  Has +1/+1 for each Pirate you've summoned this game (wherever this is).
//   BG_NEW1_027  Southsea Captain        T2  Your other Pirates have +1/+1.
//   BGS_061t     Sky Pirate              T1  token
//
// Ripsnarl Captain and Salty Looter follow their hover renders (render_overrides.json). The combat hooks
// onFriendlyAttack and onAttacked, and the engine support (sell value, two-copy triples, the per-Pirate counter,
// Golden write-back) are in HsBgEffects.h / HsBgCombat.cpp / HsBgShop.cpp.

#include "HsBgCombat.h"
#include "HsBgEffects.h"

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsPirate(Minion const& m) { return m.IsTribe(kTribePirate); }

            // ---- Shop ----

            // Peggy Brittlebone: another friendly Pirate gets +1/+1 (golden +2/+2) when a card enters the hand.
            void PeggyCardToHand(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickOther(&IsPirate);
                if (t)
                    c.Buff(*t, golden ? 2 : 1, golden ? 2 : 1);
            }

            // Nosy Looter: every two turns (golden: every turn) a random Golden minion joins the hand.
            void NosyLooterStartOfTurn(ShopCtx& c, bool golden)
            {
                if (!golden && (c.api.Turn() % 2) != 0)
                    return;
                c.api.AddRandomGoldenToHand(c.player, kMinTier, c.Tier(), 0);
            }

            // Southsea Strongarm: a friendly Pirate +1/+1, repeated once more per Pirate bought this turn.
            void StrongarmBattlecry(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickTarget(&IsPirate);
                if (!t)
                    return;
                int const times = 1 + c.api.PiratesBoughtThisTurn(c.player);
                int const n = golden ? 2 : 1;
                c.Buff(*t, n * times, n * times);
            }

            // Deck Swabbie: the next Tavern upgrade costs (1) less (golden (2)).
            void DeckSwabbieBattlecry(ShopCtx& c, bool golden)
            {
                c.api.ReduceUpgradeCost(c.player, golden ? 2 : 1);
            }

            // Cap'n Hoggarr: whenever you buy a Pirate, gain 1 Gold this turn (golden 2).
            void HoggarrBuy(ShopCtx& c, bool golden)
            {
                if (c.playedCard >= 0 && HasTribe(CardAt(c.playedCard).tribes, kTribePirate))
                    c.AddGold(golden ? 2 : 1);
            }

            // Goldgrubber: at the end of your turn, +2/+2 for each friendly Golden minion (golden +4/+4).
            void GoldgrubberEndOfTurn(ShopCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (!self)
                    return;
                int n = 0;
                for (Minion const& m : c.PlayerBoard().Items())
                    if (m.golden)
                        ++n;
                int const per = golden ? 4 : 2;
                c.Buff(*self, per * n, per * n);
            }

            // ---- Combat ----

            // Dread Admiral Eliza: whenever a friendly Pirate attacks, give all friendly minions +2/+1 (golden +4/+2).
            void ElizaFriendlyAttack(CombatCtx& c, bool golden)
            {
                Minion const* attacker = c.api.Find(c.other);
                if (!attacker || !IsPirate(*attacker))
                    return;
                for (uint32_t id : c.api.BoardIds(c.side))
                    c.api.Buff(id, golden ? 4 : 2, golden ? 2 : 1);
            }

            // Ripsnarl Captain: whenever a friendly Pirate attacks, give it +3 Attack (golden +6).
            void RipsnarlFriendlyAttack(CombatCtx& c, bool golden)
            {
                Minion const* attacker = c.api.Find(c.other);
                if (attacker && IsPirate(*attacker))
                    c.api.Buff(c.other, golden ? 6 : 3, 0);
            }

            // Yo-Ho-Ogre: after it survives being attacked, it attacks immediately.
            void YohOgreAttacked(CombatCtx& c, bool)
            {
                c.api.AttackNow(c.self);
            }

            // Scallywag: Deathrattle: summon a 1/1 Sky Pirate (golden 2/2) that attacks immediately.
            void ScallywagDeathrattle(CombatCtx& c, bool golden)
            {
                uint32_t const id = c.SummonOne(FindCard("BGS_061t"), golden);
                if (id == 0)
                {
                    ++c.failedSummons;
                    return;
                }
                c.api.AttackNow(id);
            }

            // Tony Two-Tusk: Avenge (4): make another friendly Pirate Golden permanently (golden: two).
            void TonyAvenge(CombatCtx& c, bool golden)
            {
                int const times = golden ? 2 : 1;
                for (int i = 0; i < times; ++i)
                {
                    uint32_t const id = c.RandomOf(c.Friends([](Minion const& m) {
                        return m.IsTribe(kTribePirate) && !m.golden &&
                            CardAt(m.card).goldenId[0] != '\0';
                    }));
                    if (id == 0 || !c.api.MakeGolden(id))
                        return;
                }
            }

            // Southsea Captain: your other Pirates have +1/+1 (golden +2/+2).
            void SouthseaCaptainAura(AuraCtx& c, bool golden)
            {
                for (int i = 0; i < c.board.Size(); ++i)
                    if (i != c.index && IsPirate(c.board[i]))
                        c.Give(i, golden ? 2 : 1, golden ? 2 : 1);
            }

            void RegisterPirateEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onCardToHand = &PeggyCardToHand;
                r.Register("BG21_016", d);
                d = EffectDef();
                d.onStartOfTurn = &NosyLooterStartOfTurn;
                r.Register("BG21_019", d);
                d = EffectDef();
                d.avenge = &TonyAvenge;
                d.avengeCount = 4;
                d.avengeCountGolden = 4;
                r.Register("BG21_031", d);
                d = EffectDef();
                d.goldenFromTwo = true;
                r.Register("BG23_192", d);
                d = EffectDef();
                d.onFriendlyAttack = &ElizaFriendlyAttack;
                r.Register("BGS_047", d);
                d = EffectDef();
                d.battlecry = &StrongarmBattlecry;
                r.Register("BGS_048", d);
                d = EffectDef();
                d.sellGold = 3;
                d.sellGoldGolden = 6;
                r.Register("BGS_049", d);
                d = EffectDef();
                d.battlecry = &DeckSwabbieBattlecry;
                r.Register("BGS_055", d);
                d = EffectDef();
                d.onFriendlyAttack = &RipsnarlFriendlyAttack;
                r.Register("BGS_056", d);
                d = EffectDef();
                d.onAttacked = &YohOgreAttacked;
                r.Register("BGS_060", d);
                d = EffectDef();
                d.deathrattle = &ScallywagDeathrattle;
                r.Register("BGS_061", d);
                d = EffectDef();
                d.onEndOfTurn = &GoldgrubberEndOfTurn;
                r.Register("BGS_066", d);
                d = EffectDef();
                d.onBuy = &HoggarrBuy;
                r.Register("BGS_072", d);
                d = EffectDef();
                d.perPirateSummonedAttack = 1;
                d.perPirateSummonedHealth = 1;
                r.Register("BGS_081", d);
                d = EffectDef();
                d.aura = &SouthseaCaptainAura;
                r.Register("BG_NEW1_027", d);
            }

            HS_BG_EFFECT_BATCH(Pirate, &RegisterPirateEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h).
        void HsBgBatchAnchor_Pirate() {}
    }
}
