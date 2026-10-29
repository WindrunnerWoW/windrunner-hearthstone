// Neutral effect batch (29 pool minions, 1 tokens).
//
// Cards (normal id, name, tier, text); the golden version runs the same registry entry.
//
//   BGS_004      Wrath Weaver               T1  After you play a Demon, deal 1 damage to your hero and gain +2/+1.
//   BG21_013     Whelp Smuggler             T2  After a friendly Dragon gains Attack, give it +1 Health.
//   BG23_350     Yrel                       T2  After this attacks, give a friendly minion of each minion type +1/+2.
//   BG_AT_069    Sparring Partner           T2  Taunt Battlecry: Give a minion Taunt.
//   BG_OG_221    Selfless Hero              T2  Deathrattle: Give a random friendly minion Divine Shield.
//   BG_OG_256    Spawn of N'Zoth            T2  Deathrattle: Give your minions +1/+1.
//   BG21_002     Bird Buddy                 T3  Avenge (1): Give your Beasts +1/+1.
//   BGS_002      Soul Juggler               T3  After a friendly Demon dies, deal 3 damage to a random enemy minion.
//   BGS_110      Arm of the Empire          T3  Whenever a friendly Taunt minion is attacked, give it +2 Attack permanently.
//   BG_DAL_575   Khadgar                    T3  Your cards that summon minions summon twice as many.
//   BG_DS1_070   Houndmaster                T3  Battlecry: Give a friendly Beast +2/+2 and Taunt.
//   BG_GIL_681   Nightmare Amalgam          T3  This has all minion types.
//   BG21_007     Impatient Doomsayer        T4  Avenge (4): Add a random Demon to your hand.
//   BG23_016     Reef Explorer              T4  Battlecry: Discover a minion from a minion type you don't control.
//   BG23_353     Treasure-Seeker Elise      T4  After you Refresh 5 times, find the Golden Monkey! (5 left!)
//   BG24_022     Rendle the Mistermind      T4  At the end of your turn, steal the highest Tier minion from Bob's Tavern.
//   BGS_083      Menagerie Jug              T4  Battlecry: Give 3 random friendly minions of different minion types +2/+2.
//   BG_DAL_775   Tunnel Blaster             T4  Taunt Deathrattle: Deal 3 damage to all minions.
//   BG_ICC_807   Strongshell Scavenger      T4  Battlecry: Give your Taunt minions +2/+2.
//   BG23_318     Leeroy the Reckless        T5  Deathrattle: Destroy the minion that killed this.
//   BGS_009      Lightfang Enforcer         T5  At the end of your turn, give a friendly minion of each minion type +2/+2.
//   BGS_012      Kangor's Apprentice        T5  Deathrattle: Summon the first 2 friendly Mechs that died this combat.
//   BG_FP1_031   Baron Rivendare            T5  Your minions trigger their Deathrattles twice.
//   BG_LOE_077   Brann Bronzebeard          T5  Your Battlecries trigger twice.
//   BG22_402     Mantid Queen               T6  Venomous. Start of Combat: For each of your minion types gain +5/+5, Windfury, Reborn, or Taunt.
//   BG23_190     Uther the Lightbringer     T6  Battlecry: Set a minion's Attack and Health to 15.
//   BG24_020     Tea Master Theotar         T6  After you play a minion with no minion type, give 3 friendly minions of different types +4/+4.
//   BGS_022      Zapp Slywick               T6  Windfury This minion always attacks the enemy minion with the lowest Attack.
//   BGS_040      Nadina the Red             T6  Deathrattle: Give 3 friendly Dragons Divine Shield.
// Tokens (no effects, summoned or added by the cards above):
//   BG23_353_Gt  Golden Monkey              6/6 Taunt (You found it!)

#include "HsBgEffects.h"

#include <algorithm>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            // ---------------------------------------------------------------------------------------------- helpers

            // A minion that is still in the fight: not at 0 health and not already marked to die (poison, Destroy).
            bool Living(Minion const& m) { return m.health > 0 && (m.flags & kMinionDestroyed) == 0; }

            int Times(bool golden) { return golden ? 2 : 1; }

            bool PlayedIsTribe(ShopCtx const& c, uint32_t tribe)
            {
                return c.playedCard >= 0 && HasTribe(CardAt(c.playedCard).tribes, tribe);
            }

            // Combat: ids of one friendly minion per minion type (up to maxPicks), the "different types" rule.
            std::vector<uint32_t> DistinctFriends(CombatCtx& c, int maxPicks)
            {
                std::vector<uint32_t> const ids = c.api.BoardIds(c.side);
                std::vector<uint32_t> masks;
                for (uint32_t id : ids)
                {
                    Minion const* m = c.api.Find(id);
                    masks.push_back(m && Living(*m) ? m->Tribes() : 0u);
                }
                std::vector<uint32_t> out;
                for (int i : PickDistinctTypes(masks, c.api.Random(), maxPicks))
                    out.push_back(ids[static_cast<std::size_t>(i)]);
                return out;
            }

            // Shop: uids of one board minion per minion type (up to maxPicks); `exceptUid` never counts.
            std::vector<uint32_t> DistinctOnBoard(ShopCtx& c, int maxPicks, uint32_t exceptUid)
            {
                Board const& b = c.PlayerBoard();
                std::vector<uint32_t> masks = TribeMasks(b);
                for (int i = 0; i < b.Size(); ++i)
                    if (exceptUid != 0 && b[i].uid == exceptUid)
                        masks[static_cast<std::size_t>(i)] = 0;
                std::vector<uint32_t> out;
                for (int i : PickDistinctTypes(masks, c.Random(), maxPicks))
                    out.push_back(b[i].uid);
                return out;
            }

            void BuffUid(ShopCtx& c, uint32_t uid, int attack, int health)
            {
                Minion* m = c.api.FindMinion(c.player, uid);
                if (m)
                    c.Buff(*m, attack, health);
            }

            // ---------------------------------------------------------------------------------------------- shop

            // Wrath Weaver: after you play a Demon, 1 damage to your hero and +2/+1 (the buff stays when Immune). Golden
            // (render: "..., twice."): the whole effect twice.
            void WrathWeaverPlayed(ShopCtx& c, bool golden)
            {
                if (!PlayedIsTribe(c, kTribeDemon))
                    return;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    c.api.DamageHero(c.player, 1);
                    Minion* self = c.Self();
                    if (self)
                        c.Buff(*self, 2, 1);
                }
            }

            // Whelp Smuggler: after a friendly Dragon gains Attack, give it +1 Health (shop and combat versions).
            void WhelpSmugglerShop(ShopCtx& c, bool golden)
            {
                if (c.other && c.other->IsTribe(kTribeDragon))
                    c.Buff(*c.other, 0, golden ? 2 : 1);
            }
            void WhelpSmugglerCombat(CombatCtx& c, bool golden)
            {
                Minion const* m = c.api.Find(c.other);
                if (m && m->IsTribe(kTribeDragon))
                    c.api.Buff(c.other, 0, golden ? 2 : 1);
            }

            // Yrel: after this attacks, a friendly minion of each type gets +1/+2.
            void YrelAfterAttack(CombatCtx& c, bool golden)
            {
                for (uint32_t id : DistinctFriends(c, 9))
                    if (c.api.Find(id))
                        c.api.Buff(id, golden ? 2 : 1, golden ? 4 : 2);
            }

            // Sparring Partner: Battlecry: give a minion Taunt.
            void SparringPartnerBattlecry(ShopCtx& c, bool)
            {
                Minion* t = c.PickTarget([](Minion const&) { return true; });
                if (t)
                    c.GiveKeywords(*t, kKwTaunt);
            }

            // Selfless Hero: Deathrattle: a random friendly minion without Divine Shield gets one (golden: two minions).
            void SelflessHeroDeathrattle(CombatCtx& c, bool golden)
            {
                std::vector<uint32_t> ids = c.Friends([](Minion const& m) { return Living(m) && !m.Has(kKwDivineShield); }, true);
                for (int i = 0; i < Times(golden) && !ids.empty(); ++i)
                {
                    std::size_t const k = c.api.Random().Uniform(static_cast<uint32_t>(ids.size()));
                    c.api.GrantKeywords(ids[k], kKwDivineShield);
                    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(k));
                }
            }

            // Spawn of N'Zoth: Deathrattle: your minions get +1/+1.
            void SpawnDeathrattle(CombatCtx& c, bool golden)
            {
                for (uint32_t id : c.Friends([](Minion const& m) { return Living(m); }, true))
                    c.api.Buff(id, Times(golden), Times(golden));
            }

            // Bird Buddy: Avenge (1): your Beasts get +1/+1.
            void BirdBuddyAvenge(CombatCtx& c, bool golden)
            {
                for (uint32_t id : c.Friends([](Minion const& m) { return Living(m) && m.IsTribe(kTribeBeast); }, true))
                    c.api.Buff(id, Times(golden), Times(golden));
            }

            // Soul Juggler: after a friendly Demon dies, 3 damage to a random enemy minion (golden: twice).
            void SoulJugglerFriendDied(CombatCtx& c, bool golden)
            {
                if (!c.dead || !c.dead->IsTribe(kTribeDemon))
                    return;
                for (int i = 0; i < Times(golden); ++i)
                {
                    uint32_t const t = c.RandomOf(c.Enemies([](Minion const& m) { return Living(m); }));
                    if (t != 0)
                        c.api.Damage(c.self, t, 3);
                }
            }

            // Arm of the Empire: whenever a friendly Taunt minion is attacked, it gets +2 Attack permanently.
            void ArmOfTheEmpireAttacked(CombatCtx& c, bool golden)
            {
                Minion const* m = c.api.Find(c.other);
                if (m && m->Has(kKwTaunt))
                    c.api.BuffPermanent(c.other, golden ? 4 : 2, 0);
            }

            // Houndmaster: Battlecry: give a friendly Beast +2/+2 and Taunt.
            void HoundmasterBattlecry(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickTarget([](Minion const& m) { return m.IsTribe(kTribeBeast); });
                if (!t)
                    return;
                c.Buff(*t, golden ? 4 : 2, golden ? 4 : 2);
                c.GiveKeywords(*t, kKwTaunt);
            }

            // Impatient Doomsayer: Avenge (4): add a random Demon to your hand after the fight (golden: two).
            void DoomsayerAvenge(CombatCtx& c, bool golden)
            {
                for (int i = 0; i < Times(golden); ++i)
                {
                    int const card = c.api.RandomPoolCard(c.side, kTribeDemon, false);
                    if (card >= 0)
                        c.api.RecordPersistentAddToHand(c.side, card, false);
                }
            }

            // Reef Explorer: Battlecry: Discover a minion of a type you don't control (golden: two Discovers).
            void ReefExplorerBattlecry(ShopCtx& c, bool golden)
            {
                uint32_t const mask = TribesNotControlled(c.PlayerBoard());
                if (mask == 0)
                    return;
                for (int i = 0; i < Times(golden); ++i)
                    c.api.Discover(c.player, 1, c.Tier(), mask);
            }

            // Treasure-Seeker Elise: after you Refresh 5 times, add the Golden Monkey (golden: two) and stop counting.
            void EliseRefresh(ShopCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (!self || self->counters[2] != 0)
                    return;
                if (++self->counters[1] < 5)
                    return;
                self->counters[2] = 1;                     // set before adding: a triple may move the minions
                                                           // (a full hand loses the Monkey for good, as in the game)
                int const monkey = FindCard("BG23_353_Gt");
                for (int i = 0; i < Times(golden) && monkey >= 0; ++i)
                    c.AddToHand(monkey, false);
            }

            // Rendle the Mistermind: at the end of your turn, steal the highest Tier minion of Bob's Tavern (golden: two).
            void RendleEndOfTurn(ShopCtx& c, bool golden)
            {
                for (int n = 0; n < Times(golden); ++n)
                {
                    std::vector<Minion> const& offers = c.Offers();
                    int best = 0;
                    for (Minion const& o : offers)
                        best = std::max(best, o.Tier());
                    std::vector<int> top;
                    for (std::size_t i = 0; i < offers.size(); ++i)
                        if (offers[i].Tier() == best)
                            top.push_back(static_cast<int>(i));
                    if (top.empty())
                        return;
                    int const pick = top[c.Random().Uniform(static_cast<uint32_t>(top.size()))];
                    if (!c.api.TakeOffer(c.player, pick))
                        return;                            // hand full
                }
            }

            // Menagerie Jug: Battlecry: 3 random friendly minions of different types get +2/+2.
            void MenagerieJugBattlecry(ShopCtx& c, bool golden)
            {
                for (uint32_t uid : DistinctOnBoard(c, 3, c.selfUid))
                    BuffUid(c, uid, golden ? 4 : 2, golden ? 4 : 2);
            }

            // Strongshell Scavenger: Battlecry: your Taunt minions get +2/+2.
            void StrongshellBattlecry(ShopCtx& c, bool golden)
            {
                std::vector<uint32_t> uids;
                Board const& b = c.PlayerBoard();
                for (int i = 0; i < b.Size(); ++i)
                    if (b[i].uid != c.selfUid && b[i].Has(kKwTaunt))
                        uids.push_back(b[i].uid);
                for (uint32_t uid : uids)
                    BuffUid(c, uid, golden ? 4 : 2, golden ? 4 : 2);
            }

            // Lightfang Enforcer: at the end of your turn, a friendly minion of each type gets +2/+2.
            void LightfangEndOfTurn(ShopCtx& c, bool golden)
            {
                for (uint32_t uid : DistinctOnBoard(c, 9, 0))
                    BuffUid(c, uid, golden ? 4 : 2, golden ? 4 : 2);
            }

            // Uther the Lightbringer: Battlecry: set a minion's Attack and Health to 15 (golden: 30).
            void UtherBattlecry(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickTarget([](Minion const&) { return true; });
                if (t)
                    c.SetStats(*t, golden ? 30 : 15, golden ? 30 : 15);
            }

            // Tea Master Theotar: after you play a minion with no type, 3 friendly minions of different types get +4/+4.
            void TheotarPlayed(ShopCtx& c, bool golden)
            {
                if (c.playedCard < 0 || CardAt(c.playedCard).tribes != 0)
                    return;
                for (uint32_t uid : DistinctOnBoard(c, 3, 0))
                    BuffUid(c, uid, golden ? 8 : 4, golden ? 8 : 4);
            }

            // ---------------------------------------------------------------------------------------------- combat

            // Tunnel Blaster: Deathrattle: 3 damage to all minions (golden: twice).
            void TunnelBlasterDeathrattle(CombatCtx& c, bool golden)
            {
                for (int n = 0; n < Times(golden); ++n)
                {
                    std::vector<uint32_t> all = c.Friends([](Minion const& m) { return Living(m); }, true);
                    for (uint32_t id : c.Enemies([](Minion const& m) { return Living(m); }))
                        all.push_back(id);
                    for (uint32_t id : all)
                        c.api.Damage(c.self, id, 3);
                }
            }

            // Leeroy the Reckless: Deathrattle: destroy the minion that killed this (if it is still alive).
            void LeeroyDeathrattle(CombatCtx& c, bool)
            {
                if (c.dead && c.dead->killer != 0 && c.api.Find(c.dead->killer))
                    c.api.Destroy(c.dead->killer);
            }

            // Kangor's Apprentice: Deathrattle: summon the first 2 friendly Mechs that died this combat (golden: 4).
            void KangorDeathrattle(CombatCtx& c, bool golden)
            {
                std::vector<Minion> const dead = c.api.DeadFriendlies(c.side);
                int left = golden ? 4 : 2;
                for (Minion const& d : dead)
                {
                    if (left == 0)
                        break;
                    if (!d.IsTribe(kTribeMech))
                        continue;
                    --left;
                    c.Summon(d.card, d.golden);            // a fresh copy; Khadgar multiplies it
                }
            }

            // Mantid Queen: Venomous. Start of Combat: for each of your minion types, one random of +5/+5, Windfury, Reborn,
            // Taunt (golden: two rolls per type). The Amalgam fills one type; Windfury on Windfury is Mega-Windfury.
            void MantidQueenStart(CombatCtx& c, bool golden)
            {
                std::vector<uint32_t> masks;
                for (uint32_t id : c.api.BoardIds(c.side))
                {
                    Minion const* m = c.api.Find(id);
                    masks.push_back(m && Living(*m) ? m->Tribes() : 0u);
                }
                int const rolls = CountDistinctTypes(masks) * Times(golden);
                for (int i = 0; i < rolls; ++i)
                {
                    Minion const* self = c.api.Find(c.self);
                    if (!self)
                        return;
                    switch (c.api.Random().Uniform(4))
                    {
                    case 0:
                        c.api.Buff(c.self, 5, 5);
                        break;
                    case 1:
                        c.api.GrantKeywords(c.self, self->Has(kKwWindfury | kKwMegaWindfury) ? kKwMegaWindfury : kKwWindfury);
                        break;
                    case 2:
                        c.api.GrantKeywords(c.self, kKwReborn);
                        break;
                    default:
                        c.api.GrantKeywords(c.self, kKwTaunt);
                        break;
                    }
                }
            }

            // Zapp Slywick: always attacks the enemy minion with the lowest Attack (ties random, Taunt ignored).
            uint32_t ZappChooseTarget(CombatCtx& c, bool)
            {
                std::vector<uint32_t> best;
                int low = 0;
                for (uint32_t id : c.Enemies([](Minion const& m) { return Living(m); }))
                {
                    int const a = c.api.Find(id)->attack;
                    if (best.empty() || a < low)
                    {
                        best.clear();
                        low = a;
                    }
                    if (a == low)
                        best.push_back(id);
                }
                return c.RandomOf(best);
            }

            // Nadina the Red: Deathrattle: give 3 friendly Dragons Divine Shield (golden 6); Dragons without one first.
            void NadinaDeathrattle(CombatCtx& c, bool golden)
            {
                int left = golden ? 6 : 3;
                std::vector<uint32_t> groups[2] = {   // the Dragons without a Shield first, picked before any is given
                    c.Friends([](Minion const& m) { return Living(m) && m.IsTribe(kTribeDragon) && !m.Has(kKwDivineShield); }, true),
                    c.Friends([](Minion const& m) { return Living(m) && m.IsTribe(kTribeDragon) && m.Has(kKwDivineShield); }, true) };
                for (std::vector<uint32_t>& ids : groups)
                    while (left > 0 && !ids.empty())
                    {
                        std::size_t const k = c.api.Random().Uniform(static_cast<uint32_t>(ids.size()));
                        c.api.GrantKeywords(ids[k], kKwDivineShield);
                        ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(k));
                        --left;
                    }
            }

            // ---------------------------------------------------------------------------------------------- registry

            void RegisterNeutralEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onFriendlyPlayed = &WrathWeaverPlayed;
                r.Register("BGS_004", d);
                d = EffectDef();
                d.onFriendlyBuffedShop = &WhelpSmugglerShop;
                d.onFriendBuffed = &WhelpSmugglerCombat;
                r.Register("BG21_013", d);
                d = EffectDef();
                d.afterAttack = &YrelAfterAttack;
                r.Register("BG23_350", d);
                d = EffectDef();
                d.battlecry = &SparringPartnerBattlecry;
                r.Register("BG_AT_069", d);
                d = EffectDef();
                d.deathrattle = &SelflessHeroDeathrattle;
                r.Register("BG_OG_221", d);
                d = EffectDef();
                d.deathrattle = &SpawnDeathrattle;
                r.Register("BG_OG_256", d);
                d = EffectDef();
                d.avenge = &BirdBuddyAvenge;
                d.avengeCount = 1;
                r.Register("BG21_002", d);
                d = EffectDef();
                d.onFriendDied = &SoulJugglerFriendDied;
                r.Register("BGS_002", d);
                d = EffectDef();
                d.onFriendAttacked = &ArmOfTheEmpireAttacked;
                r.Register("BGS_110", d);
                d = EffectDef();
                d.summonTimes = 2;                         // Khadgar
                d.summonTimesGolden = 3;
                r.Register("BG_DAL_575", d);
                d = EffectDef();
                d.battlecry = &HoundmasterBattlecry;
                r.Register("BG_DS1_070", d);
                d = EffectDef();
                d.avenge = &DoomsayerAvenge;
                d.avengeCount = 4;
                r.Register("BG21_007", d);
                d = EffectDef();
                d.battlecry = &ReefExplorerBattlecry;
                r.Register("BG23_016", d);
                d = EffectDef();
                d.onRefresh = &EliseRefresh;
                r.Register("BG23_353", d);
                d = EffectDef();
                d.onEndOfTurn = &RendleEndOfTurn;
                r.Register("BG24_022", d);
                d = EffectDef();
                d.battlecry = &MenagerieJugBattlecry;
                r.Register("BGS_083", d);
                d = EffectDef();
                d.deathrattle = &TunnelBlasterDeathrattle;
                r.Register("BG_DAL_775", d);
                d = EffectDef();
                d.battlecry = &StrongshellBattlecry;
                r.Register("BG_ICC_807", d);
                d = EffectDef();
                d.deathrattle = &LeeroyDeathrattle;
                r.Register("BG23_318", d);
                d = EffectDef();
                d.onEndOfTurn = &LightfangEndOfTurn;
                r.Register("BGS_009", d);
                d = EffectDef();
                d.deathrattle = &KangorDeathrattle;
                r.Register("BGS_012", d);
                d = EffectDef();
                d.deathrattleTimes = 2;                    // Baron Rivendare
                d.deathrattleTimesGolden = 3;
                r.Register("BG_FP1_031", d);
                d = EffectDef();
                d.battlecryTimes = 2;                      // Brann Bronzebeard
                d.battlecryTimesGolden = 3;
                r.Register("BG_LOE_077", d);
                d = EffectDef();
                d.startOfCombat = &MantidQueenStart;
                r.Register("BG22_402", d);
                d = EffectDef();
                d.battlecry = &UtherBattlecry;
                r.Register("BG23_190", d);
                d = EffectDef();
                d.onFriendlyPlayed = &TheotarPlayed;
                r.Register("BG24_020", d);
                d = EffectDef();
                // Windfury (golden Mega-Windfury) comes from the card table.
                d.chooseTarget = &ZappChooseTarget;
                r.Register("BGS_022", d);
                d = EffectDef();
                d.deathrattle = &NadinaDeathrattle;
                r.Register("BGS_040", d);
                // Nightmare Amalgam (BG_GIL_681): its tribe is table data, no entry.
            }

            HS_BG_EFFECT_BATCH(Neutral, &RegisterNeutralEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Neutral() {}
    }
}
