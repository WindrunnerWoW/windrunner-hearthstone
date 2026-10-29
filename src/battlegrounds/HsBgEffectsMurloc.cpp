// Murloc effect batch (12 pool minions, 0 tokens).
//
// Cards (normal id, name, tier, text); the golden version runs the same registry entry.
//
//   BG22_401     Swampstriker               T1  After you summon a Murloc, gain +1 Attack.
//   BG_UNG_073   Rockpool Hunter            T1  Battlecry: Give a friendly Murloc +1/+1.
//   BG21_008     Saltscale Honcho           T2  After you play a Murloc, give two other friendly Murlocs +1 Health.
//   BG22_202     Tad                        T2  When you sell this, add another random Murloc to your hand.
//   BG_EX1_507   Murloc Warleader           T2  Your other Murlocs have +2 Attack.
//   BG21_010     Swolefin                   T3  Battlecry: Gain +2/+1 for each other friendly Murloc.
//   BG_BT_010    Felfin Navigator           T3  Battlecry: Give your other Murlocs +1/+1.
//   BG_EX1_103   Coldlight Seer             T3  Battlecry: Give your other Murlocs +2 Health.
//   BGS_020      Primalfin Lookout          T4  Battlecry: If you control another Murloc, Discover a Murloc.
//   BG_DAL_077   Toxfin                     T4  Battlecry: Give a friendly Murloc Poisonous.
//   BGS_030      King Bagurgle              T5  Battlecry: Give your other Murlocs +3/+3.
//   BG22_403     Young Murk-Eye             T6  At the end of your turn, adjacent minions trigger their Battlecries.

#include "HsBgEffects.h"

#include <cstddef>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            // Nightmare Amalgam counts as a Murloc through Minion::IsTribe.
            bool IsMurloc(Minion const& m) { return m.IsTribe(kTribeMurloc); }

            // Up to `n` distinct random OTHER friendly Murlocs of the shop board (uids); fewer when fewer exist.
            std::vector<uint32_t> PickOtherMurlocs(ShopCtx& c, int n)
            {
                std::vector<uint32_t> pool;
                Board const& b = c.PlayerBoard();
                for (int i = 0; i < b.Size(); ++i)
                    if (b[i].uid != c.selfUid && IsMurloc(b[i]))
                        pool.push_back(b[i].uid);
                std::vector<uint32_t> out;
                while (!pool.empty() && static_cast<int>(out.size()) < n)
                {
                    std::size_t const k = c.Random().Uniform(static_cast<uint32_t>(pool.size()));
                    out.push_back(pool[k]);
                    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(k));
                }
                return out;
            }

            // Gives every OTHER friendly Murloc of the shop board +a/+h (Felfin Navigator, Coldlight Seer, Bagurgle).
            void BuffOtherMurlocsShop(ShopCtx& c, int a, int h)
            {
                Board const& b = c.PlayerBoard();
                std::vector<uint32_t> uids;
                for (int i = 0; i < b.Size(); ++i)
                    if (b[i].uid != c.selfUid && IsMurloc(b[i]))
                        uids.push_back(b[i].uid);
                for (uint32_t uid : uids)
                    if (Minion* m = c.api.FindMinion(c.player, uid))
                        c.Buff(*m, a, h);
            }

            // ---- Swampstriker: after you summon a Murloc, gain +1 Attack (shop: also when one is played; permanent)
            void SwampstrikerShop(ShopCtx& c, bool golden)
            {
                if (!c.other || c.other->uid == c.selfUid || !IsMurloc(*c.other))
                    return;
                if (Minion* s = c.Self())
                    c.Buff(*s, golden ? 2 : 1, 0);
            }
            void SwampstrikerCombat(CombatCtx& c, bool golden)
            {
                Minion const* o = c.api.Find(c.other);
                if (o && c.other != c.self && IsMurloc(*o))
                    c.api.Buff(c.self, golden ? 2 : 1, 0);
            }

            // ---- Rockpool Hunter: Battlecry: give a friendly Murloc +1/+1
            void RockpoolHunter(ShopCtx& c, bool golden)
            {
                if (Minion* t = c.PickTarget(&IsMurloc))
                    c.Buff(*t, golden ? 2 : 1, golden ? 2 : 1);
            }

            // ---- Saltscale Honcho: after you play a Murloc, give two other friendly Murlocs +1 Health
            void SaltscaleHoncho(ShopCtx& c, bool golden)
            {
                if (c.playedCard < 0 || !HasTribe(CardAt(c.playedCard).tribes, kTribeMurloc))
                    return;
                for (uint32_t uid : PickOtherMurlocs(c, 2))
                    if (Minion* m = c.api.FindMinion(c.player, uid))
                        c.Buff(*m, 0, golden ? 2 : 1);
            }

            // ---- Tad: when you sell this, add another random Murloc to your hand (golden: two)
            void TadSell(ShopCtx& c, bool golden)
            {
                int const card = c.self ? c.self->card : -1;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.AddRandomToHandExcept(c.player, 1, kMaxTier, kTribeMurloc, card);
            }

            // ---- Murloc Warleader: your other Murlocs have +2 Attack
            void WarleaderAura(AuraCtx& a, bool golden)
            {
                for (int i = 0; i < a.board.Size(); ++i)
                    if (i != a.index && IsMurloc(a.board[i]))
                        a.Give(i, golden ? 4 : 2, 0);
            }

            // ---- Swolefin: Battlecry: gain +2/+1 for each other friendly Murloc (Brann: counts again)
            void Swolefin(ShopCtx& c, bool golden)
            {
                int const idx = c.PlayerBoard().IndexOfUid(c.selfUid);
                if (idx < 0)
                    return;
                int const n = CountTribe(c.PlayerBoard(), kTribeMurloc, idx);
                if (n > 0)
                    c.Buff(c.PlayerBoard()[idx], (golden ? 4 : 2) * n, (golden ? 2 : 1) * n);
            }

            // ---- Felfin Navigator: Battlecry: give your other Murlocs +1/+1
            void FelfinNavigator(ShopCtx& c, bool golden) { BuffOtherMurlocsShop(c, golden ? 2 : 1, golden ? 2 : 1); }

            // ---- Coldlight Seer: Battlecry: give your other Murlocs +2 Health
            void ColdlightSeer(ShopCtx& c, bool golden) { BuffOtherMurlocsShop(c, 0, golden ? 4 : 2); }

            // ---- Primalfin Lookout: Battlecry: if you control another Murloc, Discover a Murloc (golden: two)
            void PrimalfinLookout(ShopCtx& c, bool golden)
            {
                int const idx = c.PlayerBoard().IndexOfUid(c.selfUid);
                if (idx < 0 || CountTribe(c.PlayerBoard(), kTribeMurloc, idx) == 0)
                    return;
                for (int i = 0; i < (golden ? 2 : 1); ++i)
                    c.api.Discover(c.player, 1, c.Tier(), kTribeMurloc);
            }

            // ---- Toxfin: Battlecry: give a friendly Murloc Poisonous
            void Toxfin(ShopCtx& c, bool)
            {
                if (Minion* t = c.PickTarget(&IsMurloc))
                    c.GiveKeywords(*t, kKwPoisonous);
            }

            // ---- King Bagurgle: Battlecry: give your other Murlocs +3/+3 (render text: no Deathrattle)
            void BagurgleBattlecry(ShopCtx& c, bool golden) { BuffOtherMurlocsShop(c, golden ? 6 : 3, golden ? 6 : 3); }

            // ---- Young Murk-Eye: at the end of your turn, adjacent minions (any type) trigger their Battlecries (golden:
            // twice). Brann is applied by TriggerBattlecry; a targeted Battlecry rolls its target.
            void YoungMurkEye(ShopCtx& c, bool golden)
            {
                Board const& b = c.PlayerBoard();
                int const idx = b.IndexOfUid(c.selfUid);
                if (idx < 0)
                    return;
                std::vector<uint32_t> uids;
                if (idx > 0)
                    uids.push_back(b[idx - 1].uid);
                if (idx + 1 < b.Size())
                    uids.push_back(b[idx + 1].uid);
                for (int run = 0; run < (golden ? 2 : 1); ++run)
                    for (uint32_t uid : uids)
                        c.api.TriggerBattlecry(c.player, uid, 0);
            }

            void RegisterMurlocEffects(EffectRegistry& r)
            {
                EffectDef d;
                d.onFriendlySummonedShop = &SwampstrikerShop;
                d.onFriendSummoned = &SwampstrikerCombat;
                r.Register("BG22_401", d);
                d = EffectDef();
                d.battlecry = &RockpoolHunter;
                r.Register("BG_UNG_073", d);
                d = EffectDef();
                d.onFriendlyPlayed = &SaltscaleHoncho;
                r.Register("BG21_008", d);
                d = EffectDef();
                d.onSell = &TadSell;
                r.Register("BG22_202", d);
                d = EffectDef();
                d.aura = &WarleaderAura;
                r.Register("BG_EX1_507", d);
                d = EffectDef();
                d.battlecry = &Swolefin;
                r.Register("BG21_010", d);
                d = EffectDef();
                d.battlecry = &FelfinNavigator;
                r.Register("BG_BT_010", d);
                d = EffectDef();
                d.battlecry = &ColdlightSeer;
                r.Register("BG_EX1_103", d);
                d = EffectDef();
                d.battlecry = &PrimalfinLookout;
                r.Register("BGS_020", d);
                d = EffectDef();
                d.battlecry = &Toxfin;
                r.Register("BG_DAL_077", d);
                d = EffectDef();
                d.battlecry = &BagurgleBattlecry;
                r.Register("BGS_030", d);
                d = EffectDef();
                d.onEndOfTurn = &YoungMurkEye;
                r.Register("BG22_403", d);
            }

            HS_BG_EFFECT_BATCH(Murloc, &RegisterMurlocEffects);
        }

        // Forces the link of this translation unit (ForceLinkEffectBatches, HsBgEffects.h): the registrar above has
        // internal linkage, so a static-library link would otherwise drop this object and its batch would not run.
        void HsBgBatchAnchor_Murloc() {}
    }
}
