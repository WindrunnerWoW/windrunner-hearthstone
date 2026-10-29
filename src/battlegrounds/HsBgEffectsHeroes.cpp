/*
 * Battlegrounds hero-power minions: the tokens a hero power brings along that need behaviour of
 * their own. The other hero tokens (The Curator's Amalgam, Ozumat's Tentacle, Onyxia's Whelp) are plain stat cards.
 *
 * The batch is one .cpp file in the style of HsBgEffectsBeast.cpp; see HsBgEffects.h for the hook rules.
 */

#include "HsBgEffects.h"

#include "HsBgHeroes.h"   // the Darkmoon Prize turns

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            // Fish of N'Zoth: after a friendly Deathrattle minion dies in combat, gain its Deathrattle. The gained ones
            // live only on the combat copy (the render says "in combat"); the golden Fish gains each one twice.
            void FishGainDeathrattle(CombatCtx& c, bool golden)
            {
                Minion const* dead = c.dead;
                Minion* self = c.Self();
                if (!dead || !self)
                    return;
                std::vector<GrantedEffect> const sources = DeathrattleSources(c.api.Effects(), *dead);
                if (sources.empty())
                    return;
                int const times = golden ? 2 : 1;
                for (int i = 0; i < times; ++i)
                    for (GrantedEffect const& g : sources)
                        self->granted.push_back(g);
            }

            // Consolation Coin: "Gain 1 Gold this turn only."
            void ConsolationCoin(ShopCtx& c, bool)
            {
                c.AddGold(1);
            }

            // The 29 Darkmoon Prizes. Every one is a spell with an onCast; a targeted prize is cast on a
            // friendly board minion (the Cast path checks spellNeedsTarget / spellTargetNotGolden).
            int Spell(char const* id) { return FindCard(id); }

            // Prize turn 1: Pocket Change, Gacha Gift, Might of Stormwind, The Good Stuff, Rocking and Rolling,
            // New Recruit, Banana Bunch.
            void PrizePocketChange(ShopCtx& c, bool)
            {
                c.AddSpellToHand(Spell("TB_BaconShop_HP_008a"), false, false);
                c.AddSpellToHand(Spell("TB_BaconShop_HP_008a"), false, false);
            }

            void PrizeGachaGift(ShopCtx& c, bool)
            {
                c.api.Discover(c.player, 1, 1, 0);
            }

            void PrizeMightOfStormwind(ShopCtx& c, bool)
            {
                Board& b = c.PlayerBoard();
                std::vector<int> idx(static_cast<std::size_t>(b.Size()));
                for (int i = 0; i < b.Size(); ++i)
                    idx[static_cast<std::size_t>(i)] = i;
                c.Random().Shuffle(idx);
                for (int i = 0; i < 3 && i < static_cast<int>(idx.size()); ++i)
                    c.Buff(b[idx[static_cast<std::size_t>(i)]], 1, 1);
            }

            void PrizeTheGoodStuff(ShopCtx& c, bool)
            {
                c.api.AddTavernAttack(c.player, 1);
            }

            void PrizeRockingAndRolling(ShopCtx& c, bool)
            {
                c.api.AddRefreshDiscount(c.player, 1, 3);   // the next three Refreshes cost (0): 1 Gold off each
            }

            void PrizeNewRecruit(ShopCtx& c, bool)
            {
                c.api.AddExtraTavernOffer(c.player);
            }

            void PrizeBananaBunch(ShopCtx& c, bool)
            {
                c.AddSpellToHand(Spell("TB_BaconShop_HP_038t"), false, false);
                c.AddSpellToHand(Spell("TB_BaconShop_HP_038t"), false, false);
            }

            // Prize turn 2: Evolving Tavern, Gruul Rules, Time Thief, On the House, The Unlimited Coin, I'm Still Just a
            // Rat in a Cage, The Bouncer, Brann's Blessing, Great Deal.
            void PrizeEvolvingTavern(ShopCtx& c, bool)
            {
                c.api.EvolveTavern(c.player);
            }

            void PrizeGruulRules(ShopCtx& c, bool)
            {
                if (c.target)
                    c.target->gruulRules = true;   // the +2/+2 is paid by Shop::EndTurn
            }

            void PrizeTimeThief(ShopCtx& c, bool)
            {
                c.api.DiscoverLastOpponent(c.player);
            }

            void PrizeOnTheHouse(ShopCtx& c, bool)
            {
                int const tier = c.Tier();
                c.api.Discover(c.player, tier, tier, 0);
            }

            void PrizeUnlimitedCoin(ShopCtx& c, bool)
            {
                c.AddGold(1);
                c.api.MarkUnlimitedCoin(c.player);
            }

            void PrizeRatInACage(ShopCtx& c, bool)
            {
                if (!c.target)
                    return;
                c.Buff(*c.target, 2, 0);
                c.Buff(*c.target, c.target->attack, 0);   // +2 Attack, then double it
            }

            void PrizeTheBouncer(ShopCtx& c, bool)
            {
                if (!c.target)
                    return;
                c.Buff(*c.target, 6, 6);
                c.GiveKeywords(*c.target, kKwTaunt);
            }

            void PrizeBrannsBlessing(ShopCtx& c, bool)
            {
                c.api.EnableBrannsBlessing(c.player);
            }

            void PrizeGreatDeal(ShopCtx& c, bool)
            {
                c.api.ReduceUpgradeCost(c.player, 5);
            }

            // Prize turn 3: Training Session, Buy the Holy Light, B.A.N.A.N.A.S., Top Shelf, Repeat Customer,
            // All That Glitters, Mindflayer Goggles.
            void PrizeTrainingSession(ShopCtx& c, bool)
            {
                c.api.DiscoverPower(c.player, 3);
            }

            void PrizeBuyTheHolyLight(ShopCtx& c, bool)
            {
                if (c.target)
                    c.GiveKeywords(*c.target, kKwDivineShield);
            }

            void PrizeBananarama(ShopCtx& c, bool)
            {
                while (!c.PlayerHand().Full())
                    if (c.AddSpellToHand(Spell("TB_BaconShop_HP_038t"), false, false) == 0)
                        break;
            }

            void PrizeTopShelf(ShopCtx& c, bool)
            {
                c.api.Discover(c.player, kMaxTier, kMaxTier, 0);
            }

            void PrizeRepeatCustomer(ShopCtx& c, bool)
            {
                if (c.target)
                    c.api.TakeFriendlyToHand(c.player, c.target->uid, false, 2, 2, true);
            }

            void PrizeAllThatGlitters(ShopCtx& c, bool)
            {
                std::vector<Minion>& offers = c.Offers();
                std::vector<int> candidates;
                for (int i = 0; i < static_cast<int>(offers.size()); ++i)
                {
                    CardDef const& d = CardAt(offers[static_cast<std::size_t>(i)].card);
                    if (!offers[static_cast<std::size_t>(i)].golden && d.role != CardRole::Spell && d.goldenId[0] != '\0')
                        candidates.push_back(i);
                }
                if (candidates.empty())
                    return;
                c.api.MakeOfferGolden(c.player,
                    candidates[c.Random().Uniform(static_cast<uint32_t>(candidates.size()))]);
            }

            void PrizeMindflayerGoggles(ShopCtx& c, bool)
            {
                c.api.StealTavern(c.player);
            }

            // Prize turn 4: Raise the Stakes, Friends and Family Discount, Open Bar, Fresh Tab, Give A Dog A Bone,
            // Big Winner!.
            void PrizeRaiseTheStakes(ShopCtx& c, bool)
            {
                if (c.target)
                    c.api.TakeFriendlyToHand(c.player, c.target->uid, true, 0, 0, true);
            }

            void PrizeFriendsAndFamily(ShopCtx& c, bool)
            {
                c.api.AddTavernCostDiscount(c.player, 1);
            }

            void PrizeOpenBar(ShopCtx& c, bool)
            {
                c.api.EnableOpenBar(c.player);
            }

            void PrizeFreshTab(ShopCtx& c, bool)
            {
                // "Refresh your Gold": refilled to the turn's 10, never lowered.
                int const gold = c.api.Gold(c.player);
                if (gold < 10)
                    c.AddGold(10 - gold);
            }

            void PrizeGiveADogABone(ShopCtx& c, bool)
            {
                if (!c.target)
                    return;
                c.Buff(*c.target, 10, 10);
                c.GiveKeywords(*c.target, kKwDivineShield | kKwWindfury);
            }

            void PrizeBigWinner(ShopCtx& c, bool)
            {
                // One Discover from each Prize turn before this one.
                int const turn = DarkmoonPrizeTurnForRound(c.api.Turn());
                for (int t = 1; t < turn; ++t)
                    c.api.PrizeDiscover(c.player, t);
            }

            void RegisterHeroEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onFriendDied = &FishGainDeathrattle;
                r.Register("TB_BaconShop_HP_105t", d);   // Fish of N'Zoth (golden: TB_BaconUps_307)

                // Consolation Coin (Mr. Bigglesworth, when a dead warband was empty).
                EffectDef coin;
                coin.onCast = &ConsolationCoin;
                r.Register("TB_BaconShop_HP_080t", coin);

                // The Darkmoon Prizes (CardDefs tag 1735), one entry each.
                struct PrizeEffect
                {
                    char const* id;
                    ShopHook onCast;
                    bool needsTarget;
                    bool notGolden;
                };
                static PrizeEffect const prizes[] = {
                    { "BGS_Treasures_001", &PrizePocketChange, false, false },
                    { "BGS_Treasures_004", &PrizeGachaGift, false, false },
                    { "BGS_Treasures_007", &PrizeMightOfStormwind, false, false },
                    { "BGS_Treasures_013", &PrizeTheGoodStuff, false, false },
                    { "BGS_Treasures_029", &PrizeRockingAndRolling, false, false },
                    { "BGS_Treasures_033", &PrizeNewRecruit, false, false },
                    { "BGS_Treasures_040", &PrizeBananaBunch, false, false },
                    { "BGS_Treasures_006", &PrizeEvolvingTavern, false, false },
                    { "BGS_Treasures_009", &PrizeGruulRules, true, false },
                    { "BGS_Treasures_010", &PrizeTimeThief, false, false },
                    { "BGS_Treasures_012", &PrizeOnTheHouse, false, false },
                    { "BGS_Treasures_014", &PrizeUnlimitedCoin, false, false },
                    { "BGS_Treasures_018", &PrizeRatInACage, true, false },
                    { "BGS_Treasures_026", &PrizeTheBouncer, true, false },
                    { "BGS_Treasures_030", &PrizeBrannsBlessing, false, false },
                    { "BGS_Treasures_036", &PrizeGreatDeal, false, false },
                    { "BGS_Treasures_011", &PrizeTrainingSession, false, false },
                    { "BGS_Treasures_015", &PrizeBuyTheHolyLight, true, false },
                    { "BGS_Treasures_019", &PrizeBananarama, false, false },
                    { "BGS_Treasures_020", &PrizeTopShelf, false, false },
                    { "BGS_Treasures_034", &PrizeRepeatCustomer, true, true },
                    { "BGS_Treasures_037", &PrizeAllThatGlitters, false, false },
                    { "BGS_Treasures_039", &PrizeMindflayerGoggles, false, false },
                    { "BGS_Treasures_016", &PrizeRaiseTheStakes, true, true },
                    { "BGS_Treasures_022", &PrizeFriendsAndFamily, false, false },
                    { "BGS_Treasures_023", &PrizeOpenBar, false, false },
                    { "BGS_Treasures_025", &PrizeFreshTab, false, false },
                    { "BGS_Treasures_028", &PrizeGiveADogABone, true, false },
                    { "BGS_Treasures_032", &PrizeBigWinner, false, false },
                };
                for (PrizeEffect const& p : prizes)
                {
                    EffectDef prize;
                    prize.onCast = p.onCast;
                    prize.spellNeedsTarget = p.needsTarget;
                    prize.spellTargetNotGolden = p.notGolden;
                    r.Register(p.id, prize);
                }
            }

            HS_BG_EFFECT_BATCH(Heroes, &RegisterHeroEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Heroes() {}
    }
}
