// Dragon effect batch (14 pool minions, 0 tokens).
//
// Cards (normal id, name, tier, text); the golden version runs the same registry entry.
//
//   BG21_027     Evolving Chromawing        T1  After you upgrade your Tavern Tier, gain +1 Attack for each friendly Dragon.
//   BGS_019      Red Whelp                  T1  Start of Combat: Deal 1 damage per friendly Dragon to one random enemy minion.
//   BG24_003     Nether Drake               T2  At the end of your turn, give your other Dragons +1 Attack.
//   BGS_045      Glyph Guardian             T2  Whenever this attacks, double its Attack.
//   BG21_015     Tarecgosa                  T3  This permanently keeps enchantments from combat.
//   BG24_500     Amber Guardian             T3  Start of Combat: Give another friendly Dragon +3/+3 and Divine Shield.
//   BGS_034      Bronze Warden              T3  Divine Shield Reborn
//   BGS_038      Twilight Emissary          T3  Taunt Battlecry: Give a friendly Dragon +2/+2.
//   BG21_014     Prized Promo-Drake         T4  Start of Combat: Give adjacent minions +1/+1 for each friendly Dragon.
//   BG23_362     Atramedes                  T4  Whenever this attacks, deal 3 damage to the target and its neighbors.
//   BGS_067      Drakonid Enforcer          T4  After a friendly minion loses Divine Shield, gain +2/+2.
//   BGS_036      Razorgore, the Untamed     T5  At the end of your turn, gain +1/+1 for each Dragon you have.
//   BGS_043      Murozond                   T5  Battlecry: Get a plain copy of a minion from your last opponent's warband.
//   BGS_041      Kalecgos, Arcane Aspect    T6  After you trigger a Battlecry, give your Dragons +1/+1.

#include "HsBgEffects.h"

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            // ---- shared helpers ----

            bool IsDragon(Minion const& m) { return m.IsTribe(kTribeDragon); }

            // Uids of the friendly Dragons on the shop board (self included), left to right. Buffing may fire other
            // hooks, so effects walk this snapshot and re-fetch every minion.
            std::vector<uint32_t> ShopDragons(ShopCtx const& c)
            {
                std::vector<uint32_t> ids;
                Board const& b = c.PlayerBoard();
                for (int i = 0; i < b.Size(); ++i)
                    if (IsDragon(b[i]))
                        ids.push_back(b[i].uid);
                return ids;
            }

            void BuffShopDragons(ShopCtx& c, int attack, int health)
            {
                for (uint32_t const uid : ShopDragons(c))
                    if (Minion* m = c.api.FindMinion(c.player, uid))
                        c.Buff(*m, attack, health);
            }

            int CombatDragons(CombatCtx const& c) { return static_cast<int>(c.Friends(&IsDragon, true).size()); }

            // ---- shop ----

            // Evolving Chromawing: +1 Attack per friendly Dragon (itself and the Amalgam count) after an upgrade.
            void ChromawingUpgrade(ShopCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (!self)
                    return;
                int const n = CountTribe(c.PlayerBoard(), kTribeDragon);
                c.Buff(*self, n * (golden ? 2 : 1), 0);
            }

            // Nether Drake: every other Dragon +1 Attack (golden +2).
            void NetherDrakeEnd(ShopCtx& c, bool golden)
            {
                for (uint32_t const uid : ShopDragons(c))
                    if (uid != c.selfUid)
                        if (Minion* m = c.api.FindMinion(c.player, uid))
                            c.Buff(*m, golden ? 2 : 1, 0);
            }

            // Razorgore: +1/+1 per Dragon on the board, counted once at the trigger.
            void RazorgoreEnd(ShopCtx& c, bool golden)
            {
                Minion* self = c.Self();
                if (!self)
                    return;
                int const n = CountTribe(c.PlayerBoard(), kTribeDragon) * (golden ? 2 : 1);
                c.Buff(*self, n, n);
            }

            // Twilight Emissary: a friendly Dragon (the chosen one, else random) gets +2/+2.
            void EmissaryBattlecry(ShopCtx& c, bool golden)
            {
                Minion* t = c.PickTarget(&IsDragon);
                if (t)
                    c.Buff(*t, golden ? 4 : 2, golden ? 4 : 2);
            }

            // Murozond: a plain (normal, unbuffed) copy of a random minion of the board the player fought last goes to
            // the hand; golden: twice, each a new pick (Brann repeats the Battlecry, each run picks again).
            void MurozondBattlecry(ShopCtx& c, bool golden)
            {
                Board const* last = c.api.LastOpponentBoard(c.player);
                if (!last || last->Empty())
                    return;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                {
                    Minion const& pick = (*last)[static_cast<int>(c.Random().Uniform(static_cast<uint32_t>(last->Size())))];
                    c.AddToHand(pick.card, false);
                }
            }

            // Kalecgos: after you trigger a Battlecry, your Dragons get +1/+1 (golden +2/+2). Every run counts: Brann's
            // second run, Young Murk-Eye's triggers.
            void KalecgosBattlecry(ShopCtx& c, bool golden)
            {
                int const n = golden ? 2 : 1;
                BuffShopDragons(c, n, n);
            }

            // ---- combat ----

            // Red Whelp: 1 damage per friendly Dragon (itself included) to a random enemy minion; golden twice.
            void RedWhelpStart(CombatCtx& c, bool golden)
            {
                for (int run = 0; run < (golden ? 2 : 1); ++run)
                {
                    int const n = CombatDragons(c);
                    uint32_t const t = c.RandomOf(c.Enemies([](Minion const& m) { return m.health > 0; }));
                    if (t != 0 && n > 0)
                        c.api.Damage(c.self, t, n);
                }
            }

            // Glyph Guardian: doubles its Attack when it attacks (tripled when golden).
            void GlyphGuardianAttack(CombatCtx& c, bool golden)
            {
                Minion* m = c.Self();
                if (m)
                    c.api.Buff(c.self, m->attack * (golden ? 2 : 1), 0);
            }

            // Amber Guardian: another random friendly Dragon gets +3/+3 and Divine Shield (golden: two other Dragons).
            void AmberGuardianStart(CombatCtx& c, bool golden)
            {
                std::vector<uint32_t> ids = c.Friends(&IsDragon);
                for (int i = 0; i < (golden ? 2 : 1) && !ids.empty(); ++i)
                {
                    std::size_t const k = c.api.Random().Uniform(static_cast<uint32_t>(ids.size()));
                    uint32_t const t = ids[k];
                    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(k));
                    c.api.Buff(t, 3, 3);
                    c.api.GrantKeywords(t, kKwDivineShield);
                }
            }

            // Prized Promo-Drake: adjacent minions get +1/+1 per friendly Dragon (itself included); golden doubles.
            void PromoDrakeStart(CombatCtx& c, bool golden)
            {
                int const n = CombatDragons(c) * (golden ? 2 : 1);
                for (uint32_t const id : c.api.Neighbors(c.self))
                    c.api.Buff(id, n, n);
            }

            // Atramedes: 3 damage to the target and its neighbours before the attack's damage; golden twice.
            void AtramedesAttack(CombatCtx& c, bool golden)
            {
                std::vector<uint32_t> victims = c.api.Neighbors(c.other);
                victims.push_back(c.other);
                for (int run = 0; run < (golden ? 2 : 1); ++run)
                    for (uint32_t const id : victims)
                        if (c.api.Find(id))
                            c.api.Damage(c.self, id, 3);
            }

            // Drakonid Enforcer: after any friendly minion (itself too) loses Divine Shield, +2/+2.
            void DrakonidShieldLost(CombatCtx& c, bool golden)
            {
                c.api.Buff(c.self, golden ? 4 : 2, golden ? 4 : 2);
            }


            void RegisterDragonEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onTavernUpgrade = &ChromawingUpgrade;
                r.Register("BG21_027", d);
                d = EffectDef();
                d.startOfCombat = &RedWhelpStart;
                r.Register("BGS_019", d);
                d = EffectDef();
                d.onEndOfTurn = &NetherDrakeEnd;
                r.Register("BG24_003", d);
                d = EffectDef();
                d.onAttack = &GlyphGuardianAttack;
                r.Register("BGS_045", d);
                d = EffectDef();
                d.keepCombatBuffs = 1;
                d.keepCombatBuffsGolden = 2;
                r.Register("BG21_015", d);
                d = EffectDef();
                d.startOfCombat = &AmberGuardianStart;
                r.Register("BG24_500", d);
                d = EffectDef();
                d.battlecry = &EmissaryBattlecry;
                r.Register("BGS_038", d);
                d = EffectDef();
                d.startOfCombat = &PromoDrakeStart;
                r.Register("BG21_014", d);
                d = EffectDef();
                d.onAttack = &AtramedesAttack;
                r.Register("BG23_362", d);
                d = EffectDef();
                d.onShieldLost = &DrakonidShieldLost;
                r.Register("BGS_067", d);
                d = EffectDef();
                d.onEndOfTurn = &RazorgoreEnd;
                r.Register("BGS_036", d);
                d = EffectDef();
                d.battlecry = &MurozondBattlecry;
                r.Register("BGS_043", d);
                d = EffectDef();
                d.onBattlecryTriggered = &KalecgosBattlecry;
                r.Register("BGS_041", d);
                // Bronze Warden (BGS_034) is keywords only (Divine Shield, Reborn): no entry.
            }

            HS_BG_EFFECT_BATCH(Dragon, &RegisterDragonEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Dragon() {}
    }
}
