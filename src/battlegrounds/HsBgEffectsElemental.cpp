// Elemental effect batch (14 pool minions, 1 token) plus the Elemental enablers that became
// playable with the tribe (Master of Realities, Nomi, Majordomo Executus, Tavern Tipper, Tortollan Blue Shell).
//
//   BG21_020     Dazzling Lightspawn      T4  Avenge (2): Elementals in Bob's Tavern have +1/+1 for the rest of the game.
//   BG21_021     Smogger                  T3  Battlecry: Give a friendly Elemental stats equal to your Tavern Tier.
//   BG21_040     Recycling Wraith          T4  After you play an Elemental, your next Refresh costs (1) less.
//   BGS_100      Lil' Rag                 T5  After you play an Elemental, give a friendly minion stats equal to its Tier.
//   BGS_115      Sellemental              T1  When you sell this, add a 2/2 Elemental to your hand.
//   BGS_116      Refreshing Anomaly       T1  Battlecry: Your next Refresh costs (0).
//   BGS_120      Party Elemental          T2  After you play an Elemental, an Elemental other than it gets +1/+2.
//   BGS_121      Gentle Djinni            T6  Taunt. Deathrattle: Add a random Elemental to your hand and summon a copy.
//   BGS_122      Stasis Elemental         T3  Battlecry: Add a random Elemental to the Tavern and Freeze it.
//   BGS_123      Tavern Tempest           T3  Battlecry: Get a random Elemental.
//   BGS_124      Lieutenant Garr          T6  Taunt. After you play an Elemental, gain +1 Health for each Elemental.
//   BGS_126      Wildfire Elemental       T4  After this attacks and kills, the excess damage hits adjacent minions.
//   BGS_127      Molten Rock              T2  After you play an Elemental, gain +1 Health.
//   BGS_115t     Water Droplet            T1  token
//   BG21_036     Master of Realities      T5  After a friendly Elemental gains stats, gain +1/+1.
//   BGS_104      Nomi, Kitchen Nightmare  T5  After you play an Elemental, Elementals in the Tavern get +1/+1.
//   BGS_105      Majordomo Executus       T4  End of turn: left-most minion +1/+1, repeated per Elemental played.
//   BG23_352     Tavern Tipper            T1  End of turn: with unspent Gold, gain +1/+2.
//   BG24_018     Tortollan Blue Shell     T5  After a lost combat this sells for 5 Gold.
//
// Keywords (Crackling Cyclone) are table data. The render overrides are in
// data/battlegrounds/render_overrides.json.

#include "HsBgCombat.h"
#include "HsBgEffects.h"

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsElemental(Minion const& m) { return m.IsTribe(kTribeElemental); }

            bool PlayedElemental(ShopCtx& c)
            {
                return c.playedCard >= 0 && HasTribe(CardAt(c.playedCard).tribes, kTribeElemental);
            }

            // Dazzling Lightspawn: Avenge (2): a permanent +1/+1 (golden +2/+2) for Elementals in Bob's Tavern.
            void DazzlingLightspawnAvenge(CombatCtx& c, bool golden)
            {
                c.api.RecordPersistent(c.self, kPersistElementalTavernBonus, golden ? 2 : 1);
            }

            // Smogger: a friendly Elemental gets stats equal to the Tavern Tier (golden: twice).
            void SmoggerBattlecry(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickTarget(&IsElemental);
                if (!t)
                    return;
                int const n = c.Tier() * (golden ? 2 : 1);
                c.Buff(*t, n, n);
            }

            // Recycling Wraith: the next Refresh (golden: the next two) costs (1) less.
            void RecyclingWraithPlayed(ShopCtx& c, bool golden)
            {
                if (PlayedElemental(c))
                    c.api.AddRefreshDiscount(c.player, 1, golden ? 2 : 1);
            }

            // Lil' Rag: a random friendly minion gets stats equal to the Elemental's Tier (golden: twice).
            void LilRagPlayed(ShopCtx& c, bool golden)
            {
                if (!PlayedElemental(c))
                    return;
                Board& b = c.PlayerBoard();
                if (b.Empty())
                    return;
                int const tier = CardAt(c.playedCard).tier;
                int const n = tier * (golden ? 2 : 1);
                c.Buff(b[static_cast<int>(c.Random().Uniform(static_cast<uint32_t>(b.Size())))], n, n);
            }

            // Sellemental: sell it, get a 2/2 Water Droplet (golden: two).
            void SellementalSold(ShopCtx& c, bool golden)
            {
                int const droplet = FindCard("BGS_115t");
                if (droplet < 0)
                    return;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.AddToHand(droplet, false);
            }

            // Refreshing Anomaly: the next Refresh (golden: the next two) is free.
            void RefreshingAnomalyBattlecry(ShopCtx& c, bool golden)
            {
                c.api.AddRefreshDiscount(c.player, 99, golden ? 2 : 1);   // "costs (0)": more than any Refresh costs, RefreshCost floors at 0
            }

            // Party Elemental: a friendly Elemental other than the one just played gets +1/+2 (golden +2/+4).
            void PartyElementalPlayed(ShopCtx& c, bool golden)
            {
                if (!PlayedElemental(c))
                    return;
                std::vector<Minion*> candidates;
                Board& b = c.PlayerBoard();
                for (int i = 0; i < b.Size(); ++i)
                    if ((!c.other || b[i].uid != c.other->uid) && IsElemental(b[i]))
                        candidates.push_back(&b[i]);
                if (candidates.empty())
                    return;
                Minion& t = *candidates[c.Random().Uniform(static_cast<uint32_t>(candidates.size()))];
                c.Buff(t, golden ? 2 : 1, golden ? 4 : 2);
            }

            // Gentle Djinni: a random Elemental to the hand and a copy of it on the board (golden: twice).
            void GentleDjinniDeathrattle(CombatCtx& c, bool golden)
            {
                int const self = c.Self() ? c.Self()->card : -1;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    int const card = c.api.RandomPoolCard(c.side, kTribeElemental, false, self);
                    if (card < 0)
                        return;
                    c.api.RecordPersistentAddToHand(c.side, card, false);
                    c.Summon(card, false);
                }
            }

            // Stasis Elemental: a random Elemental joins the Tavern and is Frozen (golden: two).
            void StasisElementalBattlecry(ShopCtx& c, bool golden)
            {
                int const own = c.Self() ? c.Self()->card : -1;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    if (c.api.AddRandomOffer(c.player, kMinTier, kMaxTier, kTribeElemental, own))
                        c.api.FreezeOffer(c.player, -1);
            }

            // Tavern Tempest: a random Elemental to the hand (golden: two).
            void TavernTempestBattlecry(ShopCtx& c, bool golden)
            {
                int const own = c.Self() ? c.Self()->card : -1;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.AddRandomToHandExcept(c.player, kMinTier, c.Tier(), kTribeElemental, own);
            }

            // Lieutenant Garr: +1 Health for each Elemental you have (golden: +2 each).
            void LieutenantGarrPlayed(ShopCtx& c, bool golden)
            {
                if (!PlayedElemental(c))
                    return;
                Minion* self = c.Self();
                if (!self)
                    return;
                int const n = CountTribe(c.PlayerBoard(), kTribeElemental) * (golden ? 2 : 1);
                c.Buff(*self, 0, n);
            }

            // Molten Rock: +1 Health (golden +2) after an Elemental is played, its own included.
            void MoltenRockPlayed(ShopCtx& c, bool golden)
            {
                if (!PlayedElemental(c))
                    return;
                if (Minion* self = c.Self())
                    c.Buff(*self, 0, golden ? 2 : 1);
            }

            // Master of Realities: after another friendly Elemental gains stats, gain +1/+1 (golden +2/+2).
            void MasterOfRealitiesBuffed(ShopCtx& c, bool golden)
            {
                Minion const* other = c.other;
                if (!other || other->uid == c.selfUid || !IsElemental(*other))
                    return;
                if (Minion* self = c.Self())
                    c.Buff(*self, golden ? 2 : 1, golden ? 2 : 1);
            }

            // Nomi: after an Elemental is played, Elementals in the Tavern get +1/+1 (golden +2/+2).
            void NomiPlayed(ShopCtx& c, bool golden)
            {
                if (PlayedElemental(c))
                    c.api.AddElementalTavernBonus(c.player, golden ? 2 : 1, golden ? 2 : 1);
            }

            // Majordomo Executus: left-most minion +1/+1, repeated once more per Elemental played this turn.
            void MajordomoEndOfTurn(ShopCtx& c, bool golden)
            {
                Board& b = c.PlayerBoard();
                if (b.Empty())
                    return;
                int const times = 1 + c.api.ElementalsPlayedThisTurn(c.player);
                int const n = golden ? 2 : 1;
                c.Buff(b[0], n * times, n * times);
            }

            // Tavern Tipper: with Gold left at the end of the turn, +1/+2 (golden +2/+4).
            void TavernTipperEndOfTurn(ShopCtx& c, bool golden)
            {
                if (c.api.Gold(c.player) <= 0)
                    return;
                if (Minion* self = c.Self())
                    c.Buff(*self, golden ? 2 : 1, golden ? 4 : 2);
            }

            // Tortollan Blue Shell: a lost last combat raises its sell price from 1 to 5 Gold (golden: 10).
            void TortollanBlueShellSold(ShopCtx& c, bool golden)
            {
                if (c.api.LostLastCombat(c.player))
                    c.AddGold(golden ? 9 : 4);
            }

            void RegisterElementalEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.avenge = &DazzlingLightspawnAvenge;
                d.avengeCount = 2;
                r.Register("BG21_020", d);
                d = EffectDef();
                d.battlecry = &SmoggerBattlecry;
                r.Register("BG21_021", d);
                d = EffectDef();
                d.onFriendlyPlayed = &RecyclingWraithPlayed;
                r.Register("BG21_040", d);
                d = EffectDef();
                d.onFriendlyPlayed = &LilRagPlayed;
                r.Register("BGS_100", d);
                d = EffectDef();
                d.onSell = &SellementalSold;
                r.Register("BGS_115", d);
                d = EffectDef();
                d.battlecry = &RefreshingAnomalyBattlecry;
                r.Register("BGS_116", d);
                d = EffectDef();
                d.onFriendlyPlayed = &PartyElementalPlayed;
                r.Register("BGS_120", d);
                d = EffectDef();
                d.deathrattle = &GentleDjinniDeathrattle;
                r.Register("BGS_121", d);
                d = EffectDef();
                d.battlecry = &StasisElementalBattlecry;
                r.Register("BGS_122", d);
                d = EffectDef();
                d.battlecry = &TavernTempestBattlecry;
                r.Register("BGS_123", d);
                d = EffectDef();
                d.onFriendlyPlayed = &LieutenantGarrPlayed;
                r.Register("BGS_124", d);
                d = EffectDef();
                d.excessToAdjacent = true;
                r.Register("BGS_126", d);
                d = EffectDef();
                d.onFriendlyPlayed = &MoltenRockPlayed;
                r.Register("BGS_127", d);
                d = EffectDef();
                d.onFriendlyBuffedShop = &MasterOfRealitiesBuffed;
                r.Register("BG21_036", d);
                d = EffectDef();
                d.onFriendlyPlayed = &NomiPlayed;
                r.Register("BGS_104", d);
                d = EffectDef();
                d.onEndOfTurn = &MajordomoEndOfTurn;
                r.Register("BGS_105", d);
                d = EffectDef();
                d.onEndOfTurn = &TavernTipperEndOfTurn;
                r.Register("BG23_352", d);
                d = EffectDef();
                d.onSell = &TortollanBlueShellSold;
                r.Register("BG24_018", d);
            }

            HS_BG_EFFECT_BATCH(Elemental, &RegisterElementalEffects);
        }

        void HsBgBatchAnchor_Elemental() {}
    }
}
