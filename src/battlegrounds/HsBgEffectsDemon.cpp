// Demon effect batch (14 pool minions, 4 tokens).
//
// Cards (normal id, name, tier, text); the golden version runs the same registry entry.
//
//   BG24_009     Picky Eater                T1  Battlecry: Consume a random minion in Bob's Tavern to gain its stats.
//   BGS_014      Imprisoner                 T1  Taunt Deathrattle: Summon a 1/1 Imp.
//   BG21_006     Impulsive Trickster        T2  Deathrattle: Give this minion's maximum Health to another friendly minion.
//   BG23_357     Mind Muck                  T2  Battlecry: Choose a friendly Demon. It consumes a minion in Bob's Tavern to gain its stats.
//   BG_AV_309    Piggyback Imp              T2  Deathrattle: Summon a 4/1 Imp.
//   BG21_039     Kathra'natir               T3  Your other Demons have +2 Attack. Your hero is Immune.
//   BG23_361     Legion Overseer            T3  Minions in Bob's Tavern have +2/+1.
//   BGS_204      Bigfernal                  T4  After you summon a Demon, gain +1/+1 permanently.
//   BG_DMF_533   Ring Matron                T4  Taunt Deathrattle: Summon two 3/2 Imps.
//   BG21_004     Insatiable Ur'zul          T5  Taunt. After you play a Demon, consume a minion in Bob's Tavern to gain its stats.
//   BGS_010      Annihilan Battlemaster     T5  Battlecry: Gain +2 Health for each Health your hero is missing.
//   BG_LOOT_368  Voidlord                   T5  Taunt Deathrattle: Summon three 1/3 Demons with Taunt.
//   BG21_005     Famished Felbat            T6  At the end of your turn, each friendly Demon consumes a minion in Bob's Tavern to gain its stats.
//   BGS_044      Imp Mama                   T6  Whenever this minion takes damage, summon a random Demon and give it Taunt.
// Tokens (no effects, summoned or added by the cards above):
//   BG_AV_309t   Backpiggy Imp              4/1
//   BG_BRM_006t  Imp                        1/1
//   BG_CS2_065   Voidwalker                 1/3 Taunt
//   BG_DMF_533t  Fiery Imp                  3/2

#include "HsBgEffects.h"

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            // ---------------------------------------------------------------------------------------- helpers

            // uid of the minion whose shop hook fires (the shop sets selfUid; hand-made contexts only self).
            uint32_t SelfUid(ShopCtx const& c) { return c.selfUid != 0 ? c.selfUid : (c.self ? c.self->uid : 0u); }

            // "Consumes a random minion in Bob's Tavern to gain its stats": the offer leaves the tavern (its pool copy
            // returns) and the eater gains its stats, Legion Overseer bonus included; golden: double. Nothing happens
            // with an empty tavern or when the eater is gone. Re-fetches the eater by uid (ConsumeOffer moves things).
            void Consume(ShopCtx& c, uint32_t eaterUid, bool golden)
            {
                std::vector<Minion>& offers = c.Offers();
                if (offers.empty())
                    return;
                int const index = static_cast<int>(c.Random().Uniform(static_cast<uint32_t>(offers.size())));
                Minion eaten;
                if (!c.api.ConsumeOffer(c.player, index, eaten))
                    return;
                Minion* eater = c.api.FindMinion(c.player, eaterUid);
                if (!eater)
                    return;
                int const k = golden ? 2 : 1;
                c.Buff(*eater, eaten.attack * k, eaten.health * k);
            }

            bool IsDemon(Minion const& m) { return m.IsTribe(kTribeDemon); }

            // ---------------------------------------------------------------------------------------- tier 1

            // Picky Eater: Battlecry: Consume a random minion in Bob's Tavern to gain its stats.
            void PickyEaterBattlecry(ShopCtx& c, bool golden) { Consume(c, SelfUid(c), golden); }

            // Imprisoner: Taunt Deathrattle: Summon a 1/1 Imp (golden: a golden Imp).
            void ImprisonerDeathrattle(CombatCtx& c, bool golden) { c.Summon(FindCard("BG_BRM_006t"), golden); }

            // ---------------------------------------------------------------------------------------- tier 2

            // Impulsive Trickster: Deathrattle: Give this minion's maximum Health to another friendly minion.
            // Golden: twice, each with a fresh random pick.
            void TricksterDeathrattle(CombatCtx& c, bool golden)
            {
                int const health = c.dead ? c.dead->maxHealth : 0;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    uint32_t const id = c.RandomOf(c.Friends([](Minion const&) { return true; }));
                    if (id != 0)
                        c.api.Buff(id, 0, health);
                }
            }

            // Mind Muck: Battlecry: Choose a friendly Demon. It consumes a minion in Bob's Tavern to gain its stats.
            void MindMuckBattlecry(ShopCtx& c, bool golden)
            {
                Minion const* t = c.PickTarget(&IsDemon);
                if (t)
                    Consume(c, t->uid, golden);
            }

            // Piggyback Imp: Deathrattle: Summon a 4/1 Imp.
            void PiggybackDeathrattle(CombatCtx& c, bool golden) { c.Summon(FindCard("BG_AV_309t"), golden); }

            // ---------------------------------------------------------------------------------------- tier 3

            // Kathra'natir: Your other Demons have +2 Attack (golden +4). The Immune hero is the heroImmune field.
            void KathraAura(AuraCtx& a, bool golden)
            {
                for (int i = 0; i < a.board.Size(); ++i)
                    if (i != a.index && a.board[i].IsTribe(kTribeDemon))
                        a.Give(i, golden ? 4 : 2, 0);
            }

            // ---------------------------------------------------------------------------------------- tier 4

            // Bigfernal: After you summon a Demon, gain +1/+1 permanently (golden +2/+2). Not for itself (the hooks
            // only reach the other minions). In the shop playing a Demon counts as summoning it.
            void BigfernalSummonedShop(ShopCtx& c, bool golden)
            {
                if (!c.other || !c.other->IsTribe(kTribeDemon))
                    return;
                Minion* self = c.Self();
                if (self)
                    c.Buff(*self, golden ? 2 : 1, golden ? 2 : 1);
            }

            void BigfernalSummoned(CombatCtx& c, bool golden)
            {
                Minion const* m = c.api.Find(c.other);
                if (m && m->IsTribe(kTribeDemon))
                    c.api.BuffPermanent(c.self, golden ? 2 : 1, golden ? 2 : 1);
            }

            // Ring Matron: Taunt Deathrattle: Summon two 3/2 Imps (golden: two golden ones).
            void RingMatronDeathrattle(CombatCtx& c, bool golden)
            {
                int const imp = FindCard("BG_DMF_533t");
                c.Summon(imp, golden);
                c.Summon(imp, golden);
            }

            // ---------------------------------------------------------------------------------------- tier 5

            // Insatiable Ur'zul: After you play a Demon, consume a minion in Bob's Tavern to gain its stats. The hook
            // reaches only the other minions, so its own play does not count; a Magnetic merge reports the real card.
            void UrzulPlayed(ShopCtx& c, bool golden)
            {
                if (c.playedCard < 0 || !HasTribe(CardAt(c.playedCard).tribes, kTribeDemon))
                    return;
                Consume(c, SelfUid(c), golden);
            }

            // Annihilan Battlemaster: Battlecry: Gain +2 Health for each Health your hero is missing (golden +4).
            void AnnihilanBattlecry(ShopCtx& c, bool golden)
            {
                int const missing = c.api.HeroMaxHealth(c.player) - c.api.HeroHealth(c.player);
                Minion* self = c.Self();
                if (self && missing > 0)
                    c.Buff(*self, 0, missing * (golden ? 4 : 2));
            }

            // Voidlord: Taunt Deathrattle: Summon three 1/3 Demons with Taunt (golden: golden Voidwalkers).
            void VoidlordDeathrattle(CombatCtx& c, bool golden)
            {
                int const walker = FindCard("BG_CS2_065");
                for (int i = 0; i < 3; ++i)
                    c.Summon(walker, golden);
            }

            // ---------------------------------------------------------------------------------------- tier 6

            // Famished Felbat: At the end of your turn, each friendly Demon consumes a minion in Bob's Tavern to gain
            // its stats. The Demons are snapshotted left to right (the Felbat included); the eating stops when the
            // tavern is empty.
            void FelbatEndOfTurn(ShopCtx& c, bool golden)
            {
                std::vector<uint32_t> eaters;
                Board const& b = c.PlayerBoard();
                for (int i = 0; i < b.Size(); ++i)
                    if (b[i].IsTribe(kTribeDemon))
                        eaters.push_back(b[i].uid);
                for (uint32_t uid : eaters)
                {
                    if (c.Offers().empty())
                        break;
                    Consume(c, uid, golden);
                }
            }

            // Imp Mama: Whenever this minion takes damage, summon a random Demon and give it Taunt. Golden: twice,
            // each with a fresh pick. Khadgar multiplies each summon; every minion that fits gets Taunt.
            void ImpMamaDamaged(CombatCtx& c, bool golden)
            {
                if (c.other != c.self)
                    return;
                Minion const* self = c.Self();
                if (!self)
                    return;
                int const own = self->card;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    int const card = c.api.RandomPoolCard(c.side, kTribeDemon, false, own);
                    if (card < 0)
                        continue;
                    int const times = c.SummonTimes();
                    for (int t = 0; t < times; ++t)
                    {
                        uint32_t const id = c.SummonOne(card, false);
                        if (id != 0)
                            c.api.GrantKeywords(id, kKwTaunt);
                        else
                            ++c.failedSummons;
                    }
                }
            }

            void RegisterDemonEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.battlecry = &PickyEaterBattlecry;
                r.Register("BG24_009", d);

                d = EffectDef();
                d.deathrattle = &ImprisonerDeathrattle;
                r.Register("BGS_014", d);

                d = EffectDef();
                d.deathrattle = &TricksterDeathrattle;
                r.Register("BG21_006", d);

                d = EffectDef();
                d.battlecry = &MindMuckBattlecry;
                r.Register("BG23_357", d);

                d = EffectDef();
                d.deathrattle = &PiggybackDeathrattle;
                r.Register("BG_AV_309", d);

                d = EffectDef();
                d.aura = &KathraAura;
                d.heroImmune = true;
                r.Register("BG21_039", d);

                d = EffectDef();
                d.tavernAttack = 2;
                d.tavernHealth = 1;
                d.tavernAttackGolden = 4;
                d.tavernHealthGolden = 2;
                r.Register("BG23_361", d);

                d = EffectDef();
                d.onFriendlySummonedShop = &BigfernalSummonedShop;
                d.onFriendSummoned = &BigfernalSummoned;
                r.Register("BGS_204", d);

                d = EffectDef();
                d.deathrattle = &RingMatronDeathrattle;
                r.Register("BG_DMF_533", d);

                d = EffectDef();
                d.onFriendlyPlayed = &UrzulPlayed;
                r.Register("BG21_004", d);

                d = EffectDef();
                d.battlecry = &AnnihilanBattlecry;
                r.Register("BGS_010", d);

                d = EffectDef();
                d.deathrattle = &VoidlordDeathrattle;
                r.Register("BG_LOOT_368", d);

                d = EffectDef();
                d.onEndOfTurn = &FelbatEndOfTurn;
                r.Register("BG21_005", d);

                d = EffectDef();
                d.onDamaged = &ImpMamaDamaged;
                r.Register("BGS_044", d);
            }

            HS_BG_EFFECT_BATCH(Demon, &RegisterDemonEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Demon() {}
    }
}
