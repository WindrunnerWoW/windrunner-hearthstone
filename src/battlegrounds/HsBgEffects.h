/*
 * Battlegrounds minion effects: the registry of per-card behaviour and the FULL hook interface the
 * shop (HsBgShop.*) and the combat simulator (HsBgCombat.*) dispatch to. The card DATA gives stats, tiers and tribes, not
 * behaviour; every minion effect is hand-written as an entry of this registry, keyed by the card id.
 *
 * PURE C++ (standard library and HsBgTypes.h only).
 *
 * HOW A FILE ADDS ENTRIES (HsBgEffectsBeast.cpp, ...Mech, ...Murloc, ...Demon, ...Dragon, ...Neutral,
 * ...Heroes for hero-power tokens)
 * ----------------------------------------------------------------------------------------------------------
 * A batch is ONE .cpp file; it needs no change to this header or to HsBgEffects.cpp:
 *
 *     #include "HsBgEffects.h"
 *     namespace Hs { namespace Bg { namespace {
 *         void SewerRatDeathrattle(CombatCtx& c, bool golden)
 *         {
 *             int const token = FindCard("BG19_010t");
 *             c.Summon(token, golden);               // inserted at the dead minion's slot, board cap respected
 *         }
 *         void RegisterBeastEffects(EffectRegistry& r)
 *         {
 *             EffectDef d;                           // fill ONLY the hooks the minion has
 *             d.deathrattle = &SewerRatDeathrattle;
 *             r.Register("BG19_010", d);             // the NORMAL card id; the golden version runs the same entry
 *         }
 *         HS_BG_EFFECT_BATCH(Beast, &RegisterBeastEffects);
 *     } } }
 *
 * - One entry per minion covers its golden version: every handler gets `golden` (also stored in the context) and
 *   scales its numbers itself (usually x2, or two triggers). Register with the normal id; a golden id is rejected.
 * - HS_BG_EFFECT_BATCH registers the function in a list that EffectRegistry::Global() runs once (in batch-name order,
 *   so the result does not depend on static initialisation order). The build must link the batch file: a test or the
 *   server lists the .cpp (if a build ever drops an unreferenced object file, call the batch function directly).
 * - Register returns false (and Global().Errors() lists it) for an unknown id, a golden id or a duplicate, so a batch
 *   test can assert Errors().empty().
 * - A handler runs with a context; it must never keep pointers or references to minions across a call that can change
 *   a board (Summon, Destroy). Combat handlers refer to minions by instance id and re-fetch with api.Find.
 *
 * CONTEXTS AND HOOKS
 * ------------------
 * Combat hooks get a CombatCtx. Who fires and what `other` / `dead` / `amount` hold:
 *   startOfCombat   self, once per combat, first-attacking side first, left to right. (position = right of self)
 *   onAttack        self is about to attack; other = the defender. Fires before damage; killing the defender or self
 *                   here cancels the attack.
 *   afterAttack     self attacked (alive or not): other = the defender, amount = damage dealt to the defender.
 *                   Fires for the attacker only if it is still alive.
 *   chooseTarget    (returns a target) self is about to attack; return the id of the enemy minion to attack, or 0 for
 *                   the normal choice (random Taunt, else random). The engine does not apply Taunt to the returned
 *                   id; it must be a living enemy minion, else it is ignored.
 *   onDamaged       broadcast to EVERY living minion on the side of the damaged minion (also itself): other = the
 *                   damaged minion, amount = health lost (never fires for an absorbed hit). Check other == self for
 *                   "this minion takes damage".
 *   onShieldLost    broadcast like onDamaged: other = the minion whose Divine Shield popped.
 *   deathrattle     self just died (already removed from the board; `dead` is a copy of it with its final stats,
 *                   counters and attachments). position = the slot it stood in; c.Summon places there and moves right.
 *   onFriendDied    broadcast to living friendly minions when another friendly minion dies (not itself): other = the
 *                   dead minion's id, dead = a copy. Use dead->IsTribe(...) for "whenever a friendly Beast dies".
 *   avenge          the engine counts friendly deaths for a minion whose entry sets avenge = N (avengeGolden when
 *                   golden and non-zero). Every N-th death calls this hook once and restarts the count. The count is
 *                   kept in Minion::counters[kCounterAvenge], reset at the start of every combat; the Avenge minion
 *                   must be alive when the friend dies.
 *   onFriendSummoned broadcast to living friendly minions when another minion is summoned (including a Reborn return)
 *                   on their side during combat: other = the new minion.
 *   onFriendAttacked broadcast to the living minions of the DEFENDER's side when an attack is declared (after the
 *                   attacker's onAttack): other = the attacked minion, source = the attacker. Before damage.
 *   onFriendlyAttack broadcast to the living minions of the ATTACKER's side when an attack is declared (after the
 *                   defender's onFriendAttacked): other = the attacker, source = the attacker. Before damage.
 *   onAttacked      the attacked minion itself after the attack resolved, when it is still alive: other = the
 *                   attacker, amount = the damage it took. Yo-Ho-Ogre.
 *   onFriendBuffed  broadcast to the OTHER living friendly minions when api.Buff raised a minion's Attack: other = the buffed
 *                   minion, amount = attack gained, amount2 = health gained. Only api.Buff / BuffPermanent fire it
 *                   (not auras, not Minion::Buff called directly), so use the api for stat gains that are events.
 *   aura            not an event: see RecomputeAuras.
 * Registry FIELDS read by the engine (no hook needed): cleave (attack damages the defender's neighbours too),
 * deathrattleTimes / battlecryTimes / summonTimes (Baron Rivendare / Brann / Khadgar), heroImmune (Kathra'natir),
 * tavernAttack / tavernHealth (Legion Overseer), keepCombatBuffs (Tarecgosa). See EffectDef.
 * Shop hooks get a ShopCtx (below) and are dispatched by Shop (HsBgShop.h, which lists which action fires which
 * hook and in what order): battlecry (played from hand, optional target), onBuy, onSell, onStartOfTurn, onEndOfTurn,
 * onRefresh, onFriendlyPlayed, onFriendlySummonedShop, onTavernUpgrade, onFriendlyBuffedShop (like the combat onFriendBuffed).
 *
 * WHAT STAYS AFTER COMBAT: combat works on copies and never changes the shop boards. An effect that must persist
 * calls api.BuffPermanent(id, a, h) ("+2 Attack permanently"), api.RecordPersistentBuff(...) (a persisting buff that
 * is not applied in combat) or api.RecordPersistentAddToHand(side, card, golden) ("add a Demon to your hand"). The
 * result carries the list (CombatResult::persistent); Game applies it to the shop board / hand after each combat.
 * A minion with registry field keepCombatBuffs (Tarecgosa) has every api.Buff / GrantKeywords on it recorded too.
 *
 * HELPERS FOR BATCH FILES: CountTribe, TribesNotControlled, PickDistinctTypes / CountDistinctTypes (the "different
 * minion types" rule, Nightmare Amalgam aware), HasDeathrattle, CombatCtx::Summon (Khadgar aware, counts
 * failedSummons) / SummonOne, CombatCtx::Friends / Enemies, CombatApi::Neighbors / ExtraAttack /
 * TriggerDeathrattle / DeadFriendlies / RandomPoolCard, ShopCtx::PickTarget / PickOther / SetStats and the
 * Summon / TriggerBattlecry multipliers.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_EFFECTS_H
#define MOD_HEARTHSTONE_HS_BG_EFFECTS_H

#include "HsBgTypes.h"

#include <string>
#include <utility>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        // The hook that fired, for the "Trigger" event of the combat log (CombatEvent::amount).
        enum class HookKind : uint8_t
        {
            StartOfCombat, Attack, AfterAttack, Damaged, ShieldLost, Deathrattle, FriendDied, Avenge, FriendSummoned,
            ChooseTarget, Aura, FriendAttacked, FriendBuffed, FriendlyAttack, Attacked, SecretTrigger,
        };

        // -------------------------------------------------------------------------------------------- combat

        class EffectRegistry;

        // What a combat effect may do to the fight. Implemented by the combat simulator (and by test doubles).
        // Minion ids are the instance ids of the combat (HsBgCombat.h). Find returns nullptr for a minion that is
        // gone (dead minions are removed from the board before their Deathrattle runs).
        class CombatApi
        {
        public:
            virtual ~CombatApi() {}
            virtual Minion* Find(uint32_t id) = 0;
            virtual int SideOf(uint32_t id) const = 0;                    // 0 or 1; -1 for a minion that is gone
            virtual int PositionOf(uint32_t id) const = 0;                // slot on its board; -1 if gone
            virtual std::vector<uint32_t> BoardIds(int side) const = 0;   // living minions, left to right
            virtual int TavernTier(int side) const = 0;
            virtual Rng& Random() = 0;
            // Summons a minion at `position` (clamped) on `side`; 0 when the board is full (the minion is lost).
            // Logs the summon, recomputes auras, then fires onFriendSummoned on the side's other minions.
            virtual uint32_t SummonMinion(int side, Minion const& proto, int position) = 0;
            virtual void Buff(uint32_t id, int attack, int health) = 0;              // permanent for this combat
            virtual void GrantKeywords(uint32_t id, uint32_t keywords) = 0;
            // Effect damage (a Deathrattle "deal 2 damage"): Divine Shield absorbs it; never poisonous.
            virtual void Damage(uint32_t source, uint32_t target, int amount) = 0;
            virtual void Destroy(uint32_t target) = 0;                    // dies at the next death resolution
            // Persisting change, see the file comment: recorded with the side and the slot the minion had in the INPUT
            // board (-1 for a minion summoned during the combat).
            virtual void RecordPersistent(uint32_t id, int kind, int value) = 0;

            // ---- additions of step 3a (all have defaults, so older test doubles keep compiling) ----
            virtual EffectRegistry const& Effects() const;
            // How many times a card that summons minions summons (Khadgar: 2 / 3), at least 1. CombatCtx::Summon uses it.
            virtual int SummonTimes(int side) const { (void)side; return 1; }
            // Buff(id, a, h) that also persists after the combat ("+2 Attack permanently"): kPersistBuff.
            virtual void BuffPermanent(uint32_t id, int attack, int health) { Buff(id, attack, health); RecordPersistentBuff(id, attack, health, 0); }
            virtual void RecordPersistentBuff(uint32_t id, int attack, int health, uint32_t keywords) { (void)id; (void)attack; (void)health; (void)keywords; }
            // "Add a random Demon to your hand": kPersistAddToHand for the player of `side`.
            virtual void RecordPersistentAddToHand(int side, int card, bool golden) { (void)side; (void)card; (void)golden; }
            // Like the above, but the shop takes a real pool copy when one is left (Y'Shaarj).
            virtual void RecordPersistentAddToPoolHand(int side, int card, bool golden)
            {
                RecordPersistentAddToHand(side, card, golden);
            }
            // `attacker` attacks `target` right now (a full attack: onAttack, damage, deaths), outside the attack order.
            // False when it could not (a gone minion). Baby Krush's Devilsaur. `target` 0 picks a target normally.
            virtual bool ExtraAttack(uint32_t attacker, uint32_t target) { (void)attacker; (void)target; return false; }
            // `attacker` attacks again right now, picking its target normally (Scallywag's Sky Pirate, Yo-Ho-Ogre).
            virtual bool AttackNow(uint32_t attacker) { return ExtraAttack(attacker, 0); }
            // Makes a combat copy Golden for the rest of the fight and records the change for the shop board
            // (kPersistMakeGolden). False when it is already Golden or has no golden twin. Tony Two-Tusk.
            virtual bool MakeGolden(uint32_t id) { (void)id; return false; }
            // Triggers the Deathrattle(s) of a LIVING minion (own card, Magnetic attachments, granted), Baron aware; the
            // summons land to its right. False when it has none. Monstrous Macaw.
            virtual bool TriggerDeathrattle(uint32_t id) { (void)id; return false; }
            // Copies of the minions of `side` that died so far this combat, in order of death (final stats and all).
            virtual std::vector<Minion> DeadFriendlies(int side) const { (void)side; return std::vector<Minion>(); }
            // Pool minions of the side's faction lobby (card indexes) that have one of the tribes (0 = any) and,
            // when asked, a Deathrattle entry in the registry. Used by Imp Mama / Ghastcoiler style "random X".
            virtual std::vector<int> PoolCandidates(int side, uint32_t tribeAny, bool deathrattleOnly) const
            {
                (void)side;
                (void)tribeAny;
                (void)deathrattleOnly;
                return std::vector<int>();
            }
            // A random card of PoolCandidates (not `excludeCard`), or -1.
            int RandomPoolCard(int side, uint32_t tribeAny, bool deathrattleOnly, int excludeCard = -1);
            // The living minions directly left and right of `id` (0, 1 or 2 ids, left first).
            std::vector<uint32_t> Neighbors(uint32_t id);
        };

        struct CombatCtx
        {
            CombatApi& api;
            int side;                 // side of the minion whose hook fires
            uint32_t self;            // its id
            bool golden;              // its golden flag (the same value is passed to the handler)
            uint32_t other = 0;       // see the table in the file comment
            Minion const* dead = nullptr;
            int amount = 0;
            int position = 0;         // summon cursor, see deathrattle
            uint32_t source = 0;      // onFriendAttacked: the attacker
            int amount2 = 0;          // onFriendlyBuffedShop: health gained
            int failedSummons = 0;    // Summon attempts that found the board full (Omega Buster)

            CombatCtx(CombatApi& a, int s, uint32_t id, bool g) : api(a), side(s), self(id), golden(g) {}
            Minion* Self() const { return api.Find(self); }
            // Summons a minion of `card` at the cursor and moves the cursor right, as many times as Khadgar allows
            // (api.SummonTimes). Returns the id of the last minion that fit, 0 when none did; failedSummons counts
            // the ones that did not fit. SummonOne ignores Khadgar (use it when the card is not a summoning "card").
            uint32_t Summon(int card, bool goldenMinion);
            uint32_t Summon(Minion const& proto);
            uint32_t SummonOne(int card, bool goldenMinion);
            uint32_t SummonOne(Minion const& proto);
            int SummonTimes() const { return api.SummonTimes(side); }
            int EnemySide() const { return 1 - side; }
            // Ids of the living minions of a side (default: own side) for which pred(Minion const&) holds, left to
            // right. Friends skips self unless includeSelf.
            template <class Pred> std::vector<uint32_t> Friends(Pred pred, bool includeSelf = false) const
            {
                std::vector<uint32_t> out;
                for (uint32_t id : api.BoardIds(side))
                {
                    Minion const* m = api.Find(id);
                    if (m && (includeSelf || id != self) && pred(*m))
                        out.push_back(id);
                }
                return out;
            }
            template <class Pred> std::vector<uint32_t> Enemies(Pred pred) const
            {
                std::vector<uint32_t> out;
                for (uint32_t id : api.BoardIds(1 - side))
                {
                    Minion const* m = api.Find(id);
                    if (m && pred(*m))
                        out.push_back(id);
                }
                return out;
            }
            // One random id of a list (0 when empty).
            uint32_t RandomOf(std::vector<uint32_t> const& ids) const
            {
                return ids.empty() ? 0u : ids[api.Random().Uniform(static_cast<uint32_t>(ids.size()))];
            }
        };

        using CombatHook = void (*)(CombatCtx& ctx, bool golden);
        using TargetHook = uint32_t (*)(CombatCtx& ctx, bool golden);

        // -------------------------------------------------------------------------------------------- auras

        struct AuraBonus
        {
            int attack = 0;
            int health = 0;
            uint32_t keywords = 0;
        };

        // Context of an aura hook: the board the source stands on. Give adds a bonus for `targetIndex` this recompute.
        struct AuraCtx
        {
            Board const& board;
            int index;                // slot of the aura source
            bool golden;
            std::vector<AuraBonus>& bonus;
            void Give(int targetIndex, int attack, int health, uint32_t keywords = 0)
            {
                if (targetIndex < 0 || targetIndex >= board.Size())
                    return;
                AuraBonus& b = bonus[static_cast<std::size_t>(targetIndex)];
                b.attack += attack;
                b.health += health;
                b.keywords |= keywords;
            }
        };
        using AuraHook = void (*)(AuraCtx& ctx, bool golden);

        class EffectRegistry;
        // Recomputes every aura on a board from scratch (shop and combat): clears the old aura bonuses of all
        // minions, runs each source's aura hook left to right and applies the new totals. An aura health bonus raises
        // health with maxHealth when it grows; when it shrinks maxHealth falls and health is capped to it (never
        // below 1), as in Hearthstone.
        void RecomputeAuras(Board& board, EffectRegistry const& registry, AuraBonus const& extra = AuraBonus{});

        // -------------------------------------------------------------------------------------------- shop

        // The shop as an effect sees it, implemented by Shop (HsBgShop.h). The first six methods are required; the rest
        // have inert defaults so a test double can implement only what its effect uses. `player` is the lobby index.
        //
        // POINTERS. Every Minion& / Minion* an effect holds points into a player's board, hand or offers. Buff and
        // GiveKeywords keep them valid. SummonToBoard, AddToHand, AddRandomToHand, TakeOffer, ConsumeOffer, Discover and
        // TriggerBattlecry may move, add or remove minions (also through a triple merge): after calling one, re-fetch
        // every minion with FindMinion(player, uid) (ShopCtx::Self does it for the hook's own minion).
        //
        // POOL. Minions an effect creates with SummonToBoard / AddToHand are generated (poolCopies 0): they never touch
        // the shared pool, and selling them returns nothing. AddRandomToHand, Discover and TakeOffer use pool copies.
        class ShopApi
        {
        public:
            virtual ~ShopApi() {}
            virtual Board& PlayerBoard(int player) = 0;
            virtual Hand& PlayerHand(int player) = 0;
            virtual int Gold(int player) const = 0;
            virtual void AddGold(int player, int amount) = 0;   // never takes the gold below 0; may exceed 10 inside a turn
            virtual int TavernTier(int player) const = 0;
            virtual Rng& Random() = 0;

            // Bob's Tavern: the offers of the player, left to right (bought slots are gone). Modify the minions freely.
            virtual std::vector<Minion>& Offers(int player);
            // A minion of the player by uid (board, hand, offers, discover options), nullptr when gone.
            virtual Minion* FindMinion(int player, uint32_t uid);
            virtual EffectRegistry const& Effects() const;      // for "minion with a Deathrattle" style checks
            virtual int Turn() const { return 1; }
            // Permanent buff / keywords; recomputes the auras of the player's board. Safe on any minion.
            virtual void Buff(int player, Minion& m, int attack, int health) { (void)player; m.Buff(attack, health); }
            virtual void GiveKeywords(int player, Minion& m, uint32_t keywords) { (void)player; m.keywords |= keywords; }
            // Summons a generated minion at `position` (clamped) of the board; 0 when the board is full. Fires
            // onFriendlySummonedShop on the others and may complete a triple.
            virtual uint32_t SummonToBoard(int, int, bool, int) { return 0; }
            // Adds a generated minion to the hand; 0 when the hand is full (unless it completes a triple).
            virtual uint32_t AddToHand(int, int, bool) { return 0; }
            // Adds a random pool minion (a real pool copy) of the tier range and tribe set (0 = any tribe) to the hand;
            // 0 when the hand is full or the pool has nothing.
            virtual uint32_t AddRandomToHand(int, int, int, uint32_t) { return 0; }
            // Starts a Discover (3 distinct pool minions); the player must choose before doing anything else. Queued
            // behind a Discover already open. False when the pool has nothing to show.
            virtual bool Discover(int, int, int, uint32_t) { return false; }
            // Offer -> hand for free (a stolen minion); false when the hand is full or the index is bad.
            virtual bool TakeOffer(int, int) { return false; }
            // Removes an offer from the tavern (its copy returns to the pool); `out` receives the minion as it was.
            virtual bool ConsumeOffer(int, int, Minion&) { return false; }
            virtual void DamageHero(int, int) {}
            // Runs the battlecry of a minion on the board or in the hand (target uid or 0).
            virtual void TriggerBattlecry(int, uint32_t, uint32_t) {}

            // ---- additions of step 3a ----
            virtual int HeroHealth(int player) const { (void)player; return 40; }
            virtual int HeroMaxHealth(int player) const { (void)player; return 40; }
            // How many times a Battlecry fires (Brann: 2 / 3) and a summoning card summons (Khadgar), at least 1.
            virtual int BattlecryTimes(int player) const { (void)player; return 1; }
            virtual int SummonTimes(int player) const { (void)player; return 1; }
            // The board the player fought in the last combat as it entered the fight (the ghost's for a ghost fight);
            // nullptr before the first combat. Murozond.
            virtual Board const* LastOpponentBoard(int player) const { (void)player; return nullptr; }
            // AddRandomToHand without a given card (Tad: "another random Murloc").
            virtual uint32_t AddRandomToHandExcept(int player, int minTier, int maxTier, uint32_t tribeAny, int excludeCard)
            {
                (void)excludeCard;
                return AddRandomToHand(player, minTier, maxTier, tribeAny);
            }

            // ---- Elementals ----
            // "Your next Refresh costs (X) less": the next `charges` refreshes each cost `perRefresh` less (0 does
            // nothing). The shop keeps the larger per-charge discount while charges are left.
            virtual void AddRefreshDiscount(int player, int perRefresh, int charges)
            {
                (void)player;
                (void)perRefresh;
                (void)charges;
            }
            // "Elementals in Bob's Tavern have +X/+Y for the rest of the game" (Nomi, Dazzling Lightspawn): a
            // per-player bonus added to every Elemental offer in RecomputeOfferAuras.
            virtual void AddElementalTavernBonus(int player, int attack, int health)
            {
                (void)player;
                (void)attack;
                (void)health;
            }
            // "If you lost your last combat" (Tortollan Blue Shell).
            virtual bool LostLastCombat(int player) const
            {
                (void)player;
                return false;
            }
            // Adds a random pool minion of the tier range and tribe set to Bob's Tavern (Stasis Elemental); false
            // when the pool has nothing. The offer is a real pool copy, like a normal refresh.
            virtual bool AddRandomOffer(int player, int minTier, int maxTier, uint32_t tribeAny, int excludeCard = -1)
            {
                (void)player;
                (void)minTier;
                (void)maxTier;
                (void)tribeAny;
                (void)excludeCard;
                return false;
            }
            // Freezes one tavern offer, -1 = the newest (Stasis Elemental freezes what it added).
            virtual void FreezeOffer(int player, int index)
            {
                (void)player;
                (void)index;
            }
            // How many Elementals the player has played this turn (Majordomo Executus).
            virtual int ElementalsPlayedThisTurn(int player) const
            {
                (void)player;
                return 0;
            }

            // ---- Pirates ----
            // "Reduce the cost of upgrading Bob's Tavern by (X)" (Deck Swabbie).
            virtual void ReduceUpgradeCost(int player, int amount)
            {
                (void)player;
                (void)amount;
            }
            // Adds a random GOLDEN pool minion of the tier range and tribe set to the hand (a real pool copy turned
            // Golden); 0 when the hand is full or the pool has nothing. Nosy Looter, Eudora.
            virtual uint32_t AddRandomGoldenToHand(int player, int minTier, int maxTier, uint32_t tribeAny)
            {
                (void)player;
                (void)minTier;
                (void)maxTier;
                (void)tribeAny;
                return 0;
            }
            // How many Pirates the player bought this turn (Southsea Strongarm).
            virtual int PiratesBoughtThisTurn(int player) const
            {
                (void)player;
                return 0;
            }

            // ---- Naga and spells ----
            // A buff from a spell: `permanent` is false for "until next turn" (the shop records the amount and takes it
            // back at the owner's next StartTurn). The default is a plain permanent buff, for test doubles.
            virtual void SpellBuff(int player, Minion& m, int attack, int health, bool permanent)
            {
                (void)player;
                (void)permanent;
                m.Buff(attack, health);
            }
            // Keywords from a spell; `permanent` false means removed at the owner's next StartTurn.
            virtual void SpellKeywords(int player, Minion& m, uint32_t keywords, bool permanent)
            {
                (void)player;
                (void)permanent;
                m.keywords |= keywords;
            }
            // Adds a spell card to the hand (Spellcraft at the start of a turn, Gold Coins, Bananas); `temporary` marks
            // a Spellcraft card that leaves the hand at the owner's next StartTurn. 0 when the hand is full.
            virtual uint32_t AddSpellToHand(int player, int card, bool golden, bool temporary, bool permanentEffect)
            {
                (void)player;
                (void)card;
                (void)golden;
                (void)temporary;
                (void)permanentEffect;
                return 0;
            }
            // Starts a Discover of three Spellcraft spells of `maxTier` or lower (Lady Vashj); `permanentEffect` marks
            // the chosen spell so its buffs are permanent.
            virtual bool DiscoverSpell(int player, int maxTier, bool permanentEffect)
            {
                (void)player;
                (void)maxTier;
                (void)permanentEffect;
                return false;
            }

            // ---- Quilboar and Blood Gems ----
            // Plays a Blood Gem on a friendly minion: the gem's own +1/+1 (raised by Agamaggan) and then the "after a
            // Blood Gem is played on this" triggers. A gem cast from the hand goes through here too, so both paths
            // behave the same.
            virtual void PlayBloodGem(int player, Minion& target)
            {
                (void)player;
                (void)target;
            }

            // ---- Tickatus and the Darkmoon Prizes ----
            // A Discover of the Darkmoon Prizes of a Prize turn (1-4; -1 = any).
            virtual bool PrizeDiscover(int player, int prizeTurn)
            {
                (void)player;
                (void)prizeTurn;
                return false;
            }
            // "Minions in Bob's Tavern have +1 Attack for the rest of the game" (The Good Stuff).
            virtual void AddTavernAttack(int player, int amount)
            {
                (void)player;
                (void)amount;
            }
            // "Minions in Bob's Tavern cost (1) less for the rest of the game" (Friends and Family Discount).
            virtual void AddTavernCostDiscount(int player, int amount)
            {
                (void)player;
                (void)amount;
            }
            // "Add a minion to Bob's Tavern for the rest of the game" (New Recruit): one more tavern slot.
            virtual void AddExtraTavernOffer(int player) { (void)player; }
            // "Your first 5 Refreshes each turn cost (0)" (Open Bar).
            virtual void EnableOpenBar(int player) { (void)player; }
            // The Unlimited Coin returns to the hand at the end of the turn it was cast in.
            virtual void MarkUnlimitedCoin(int player) { (void)player; }
            // "Your Battlecries trigger twice this turn" (Brann's Blessing).
            virtual void EnableBrannsBlessing(int player) { (void)player; }
            // Make a tavern offer Golden (All That Glitters); false when the tavern is empty.
            virtual bool MakeOfferGolden(int player, int offerIndex)
            {
                (void)player;
                (void)offerIndex;
                return false;
            }
            // Remove a friendly minion to the hand (Repeat Customer: +2/+2; Raise the Stakes: Golden first). False when
            // the target is gone, is golden while nonGoldenOnly, or the hand has no room (the minion stays then).
            virtual bool TakeFriendlyToHand(int player, uint32_t uid, bool makeGolden, int attack, int health, bool nonGoldenOnly)
            {
                (void)player;
                (void)uid;
                (void)makeGolden;
                (void)attack;
                (void)health;
                (void)nonGoldenOnly;
                return false;
            }
            // "Replace all minions in Bob's Tavern with ones of a higher Tavern Tier" (Evolving Tavern).
            virtual void EvolveTavern(int player) { (void)player; }
            // "Steal all minions in Bob's Tavern, then Refresh it" (Mindflayer Goggles).
            virtual void StealTavern(int player) { (void)player; }
            // "Discover a new Hero Power" (Training Session): the swap pool and its deny list, as Master Nguyen.
            virtual bool DiscoverPower(int player, int count)
            {
                (void)player;
                (void)count;
                return false;
            }
            // Time Thief: a plain copy from the last opponent's warband.
            virtual bool DiscoverLastOpponent(int player)
            {
                (void)player;
                return false;
            }
        };

        // Context of a shop hook. See HsBgShop.h for which hook fires from which action and what the fields hold.
        // `self`, `target`, `other` are valid only until the handler changes a board / hand / offers; Self() re-fetches.
        struct ShopCtx
        {
            ShopApi& api;
            int player;
            bool golden;
            Minion* self = nullptr;     // the minion the hook belongs to (on the board; for buy / sell the bought / sold one)
            Minion* target = nullptr;   // battlecry: the chosen target, or nullptr
            Minion* other = nullptr;    // onFriendlyPlayed / onFriendlySummonedShop: the other minion
            int position = 0;           // board slot of self (battlecry: where it was played)
            uint32_t selfUid = 0;       // uid of self (set by the shop; 0 in hand-made contexts)
            // onFriendlyPlayed / onFriendlySummonedShop: the card that was PLAYED from the hand. For a Magnetic merge
            // `other` is the Mech it merged into while playedCard is the Magnetic card (and magnetic is true): use
            // playedCard for "after you play a minion with Battlecry / Deathrattle / no type / a Demon".
            int playedCard = -1;
            bool playedGolden = false;
            bool magnetic = false;
            int amountAttack = 0;       // onFriendlyBuffedShop: attack gained by `other`
            int amountHealth = 0;       // onFriendlyBuffedShop: health gained by `other`
            // OnSpellCast / onCast: the spell that was cast, whether it is a Spellcraft spell, and whether
            // its effect is permanent (Lady Vashj, Lava Lurker). `target` is the minion it was cast on, if any.
            int spellCard = -1;
            bool spellcraft = false;
            bool spellPermanent = false;
            int goldSpent = 0;          // onSpendGold: the Gold the player spent
            mutable int failedSummons = 0; // Summon attempts that found the board full

            ShopCtx(ShopApi& a, int p, bool g) : api(a), player(p), golden(g) {}
            Minion* Self() { return selfUid != 0 ? api.FindMinion(player, selfUid) : self; }
            // Short forms of the common calls.
            Board& PlayerBoard() const { return api.PlayerBoard(player); }
            Hand& PlayerHand() const { return api.PlayerHand(player); }
            std::vector<Minion>& Offers() const { return api.Offers(player); }
            int Tier() const { return api.TavernTier(player); }
            Rng& Random() const { return api.Random(); }
            void AddGold(int amount) const { api.AddGold(player, amount); }
            void Buff(Minion& m, int attack, int health) const { api.Buff(player, m, attack, health); }
            // Buff by uid (a minion an effect picked by id); does nothing when it is gone.
            void BuffUid(uint32_t uid, int attack, int health) const
            {
                if (Minion* m = api.FindMinion(player, uid))
                    api.Buff(player, *m, attack, health);
            }
            void GiveKeywords(Minion& m, uint32_t keywords) const { api.GiveKeywords(player, m, keywords); }
            // Summons as many times as Khadgar allows (each next one to the right of the last); the id of the last
            // that fit, 0 if none. SummonOne ignores Khadgar.
            uint32_t Summon(int card, bool goldenMinion, int pos) const
            {
                uint32_t last = 0;
                int const times = api.SummonTimes(player);
                for (int i = 0; i < times; ++i)
                {
                    uint32_t const id = api.SummonToBoard(player, card, goldenMinion, pos + i);
                    if (id != 0)
                        last = id;
                    else
                        ++failedSummons;
                }
                return last;
            }
            uint32_t SummonOne(int card, bool goldenMinion, int pos) const { return api.SummonToBoard(player, card, goldenMinion, pos); }
            // Battlecry target helpers. PickTarget: the chosen target if pred holds for it (and it is not self), else a
            // random other board minion with pred, else nullptr (the Battlecry fizzles). PickOther: random other board
            // minion with pred. Valid until the board changes.
            template <class Pred> Minion* PickTarget(Pred pred) const
            {
                if (target && target != self && pred(*target))
                    return target;
                return PickOther(pred);
            }
            template <class Pred> Minion* PickOther(Pred pred) const
            {
                std::vector<Minion*> c;
                Board& b = api.PlayerBoard(player);
                for (int i = 0; i < b.Size(); ++i)
                    if (b[i].uid != selfUid && pred(b[i]))
                        c.push_back(&b[i]);
                if (c.empty())
                    return nullptr;
                return c[api.Random().Uniform(static_cast<uint32_t>(c.size()))];
            }
            // Sets the BASE attack / health of a minion (aura bonuses stay on top), full health. Goes through Buff.
            void SetStats(Minion& m, int attack, int health) const
            {
                int const da = attack - (m.attack - m.auraAttack);
                int const dh = health - (m.maxHealth - m.auraHealth);
                api.Buff(player, m, da, dh);
                m.health = m.maxHealth;
            }
            uint32_t AddToHand(int card, bool goldenMinion) const { return api.AddToHand(player, card, goldenMinion); }
            // A spell's buff / keyword grant, temporary or permanent; a spell card to the hand.
            void SpellBuff(Minion& m, int attack, int health) const { api.SpellBuff(player, m, attack, health, spellPermanent); }
            void SpellKeywords(Minion& m, uint32_t keywords) const { api.SpellKeywords(player, m, keywords, spellPermanent); }
            uint32_t AddSpellToHand(int card, bool goldenSpell, bool temporary) const
            {
                return api.AddSpellToHand(player, card, goldenSpell, temporary, false);
            }
            // A Blood Gem on a friendly minion (the gem's value and the on-target triggers).
            void PlayBloodGem(Minion& m) const { api.PlayBloodGem(player, m); }
        };
        using ShopHook = void (*)(ShopCtx& ctx, bool golden);

        // -------------------------------------------------------------------------------------------- helpers

        // Friendly minions of a tribe on a board (Nightmare Amalgam counts), not counting index `exceptIndex`.
        int CountTribe(Board const& board, uint32_t tribe, int exceptIndex = -1);
        // Single-bit tribes (kTribeBeast..kTribeQuilboar) that NO minion of the board has; Amalgam has them all, so a
        // board with an Amalgam returns 0. Reef Explorer: a result of 0 means there is nothing to discover.
        uint32_t TribesNotControlled(Board const& board);
        // The "different minion types" rule. `tribeMasks` are the Tribes() of the candidate minions (a mask of 0 = no
        // type, never picked). Returns the indexes of up to `maxPicks` minions that can be given DISTINCT types, as a
        // maximum matching (Nightmare Amalgam fills any one type, once), chosen randomly; sorted ascending.
        // "A friendly minion of each type" = maxPicks 9. CountDistinctTypes is the matching size (no randomness).
        std::vector<int> PickDistinctTypes(std::vector<uint32_t> const& tribeMasks, Rng& rng, int maxPicks);
        int CountDistinctTypes(std::vector<uint32_t> const& tribeMasks);
        // Tribe masks of the minions of a board, in order (for the two functions above).
        std::vector<uint32_t> TribeMasks(Board const& board);
        // Does the minion have a Deathrattle: its own card's, a Magnetic attachment's, a granted one.
        bool HasDeathrattle(EffectRegistry const& registry, Minion const& m);
        // The sources of the Deathrattles a minion runs when it dies, in order: own card, attachments, granted. Each
        // entry carries the card, its golden flag and its granted kind (0 = a card); N'Zoth copies these onto the Fish.
        std::vector<GrantedEffect> DeathrattleSources(EffectRegistry const& registry, Minion const& m);
        // The Deathrattles a minion runs when it dies, in order: own card, attachments, granted. (hook, golden flag)
        std::vector<std::pair<CombatHook, bool>> DeathrattleInstances(EffectRegistry const& registry, Minion const& m);
        // Times a source on `board` multiplies something: max over its minions of the registry field, at least 1.
        enum class TimesKind : uint8_t { Deathrattle, Battlecry, Summon };
        int TimesFor(EffectRegistry const& registry, Board const& board, TimesKind kind);
        // The Spellcraft spell cards (normal indexes, card role Spell) of `maxTier` or lower, for Lady Vashj
        // and Pashmar. Empty when the effect batches are not linked.
        std::vector<int> SpellcraftSpells(int maxTier);

        // -------------------------------------------------------------------------------------------- registry

        // The behaviour of one card (normal and golden). Leave unused hooks null.
        struct EffectDef
        {
            // shop
            ShopHook battlecry = nullptr;
            ShopHook onBuy = nullptr;
            ShopHook onSell = nullptr;
            ShopHook onStartOfTurn = nullptr;
            ShopHook onEndOfTurn = nullptr;
            ShopHook onRefresh = nullptr;
            ShopHook onFriendlyPlayed = nullptr;
            ShopHook onFriendlySummonedShop = nullptr;
            ShopHook onCardToHand = nullptr;
            ShopHook onTavernUpgrade = nullptr;
            ShopHook onFriendlyBuffedShop = nullptr;
            ShopHook onBattlecryTriggered = nullptr;   // after a friendly Battlecry ran once (`other`: its minion)
            ShopHook onSpellCast = nullptr;            // after the player cast a spell (spellCard / target in the context)
            ShopHook onCast = nullptr;                 // a spell card: its own cast effect (registered on the spell)
            // Quilboar
            ShopHook onBloodGem = nullptr;             // after a Blood Gem was played on this minion (self = it)
            ShopHook onBloodGemPlayed = nullptr;       // ... on every friendly minion (other = the gem's target)
            ShopHook onSpendGold = nullptr;            // after the player spent Gold (amount in the context)
            // Naga
            char const* grantsSpellcraft = nullptr;    // this minion puts this spell in the hand at the owner's StartTurn
            bool isSpellcraftSpell = false;            // a spell card that counts as a Spellcraft spell (Critter Wrangler)
            bool spellNeedsTarget = false;             // the spell must be cast on a friendly board minion
            uint32_t spellTargetTribe = 0;             // ... of this tribe (0 = any)
            bool spellTargetNotGolden = false;         // ... and not a Golden one (Repeat Customer, Raise the Stakes)
            int spellcraftPermanentPerTurn = 0;        // Lava Lurker: this many Spellcraft spells on it are permanent
            int spellcraftPermanentPerTurnGolden = 0;  //   golden: 2
            // combat
            CombatHook startOfCombat = nullptr;
            CombatHook onAttack = nullptr;
            CombatHook afterAttack = nullptr;
            TargetHook chooseTarget = nullptr;
            CombatHook onDamaged = nullptr;
            CombatHook onShieldLost = nullptr;
            CombatHook deathrattle = nullptr;
            CombatHook onFriendDied = nullptr;
            CombatHook avenge = nullptr;
            int avengeCount = 0;          // N of "Avenge (N)"; 0 = no Avenge
            int avengeCountGolden = 0;    // N of the golden card; 0 = same as avengeCount
            CombatHook onFriendSummoned = nullptr;
            CombatHook onFriendAttacked = nullptr;
            CombatHook onFriendlyAttack = nullptr;
            CombatHook onAttacked = nullptr;
            CombatHook onFriendBuffed = nullptr;
            // aura
            AuraHook aura = nullptr;

            // Engine-read fields (0 / false = not used). `Golden` variants apply to a golden minion; 0 = same as normal.
            bool cleave = false;                // the attack also damages the defender's neighbours (Cave Hydra, Foe Reaper)
            bool heroImmune = false;            // Shop::DamageHero does nothing while this is on the board (Kathra'natir)
            int deathrattleTimes = 0;           // total triggers of friendly Deathrattles (Baron Rivendare: 2)
            int deathrattleTimesGolden = 0;     //   golden: 3
            int battlecryTimes = 0;             // total triggers of Battlecries played / triggered (Brann: 2, golden 3)
            int battlecryTimesGolden = 0;
            int summonTimes = 0;                // total summons of a summoning card (Khadgar: 2, golden 3)
            int summonTimesGolden = 0;
            int tavernAttack = 0;               // bonus of every minion in Bob's Tavern while this is on the board
            int tavernHealth = 0;
            int tavernAttackGolden = 0;
            int tavernHealthGolden = 0;
            int keepCombatBuffs = 0;            // factor by which api.Buff / GrantKeywords on it in combat persist (Tarecgosa 1)
            int keepCombatBuffsGolden = 0;      //   golden: 2 (0 = same as normal)
            bool excessToAdjacent = false;       // "after this attacks and kills a minion, deal excess damage to
                                                 // adjacent minions" (Wildfire Elemental; golden: all neighbours)
            // Pirates
            bool goldenFromTwo = false;    // First Mate Pip: two copies already make it Golden
            int sellGold = 0;              // Freedealing Gambler: the Gold it sells for (0 = the normal 1)
            int sellGoldGolden = 0;        // its golden twin
            int perPirateSummonedAttack = 0; // Salty Looter: +X/+Y for each Pirate summoned this game
            int perPirateSummonedHealth = 0;
            // Quilboar
            int bloodGemAttack = 0;        // Agamaggan: your Blood Gems give this much extra
            int bloodGemHealth = 0;
            int bloodGemAttackGolden = 0;  //   golden: +2/+2 (0 = same as normal)
            int bloodGemHealthGolden = 0;
        };

        class EffectRegistry
        {
        public:
            EffectRegistry();
            // Registers an entry under a NORMAL card id. False (and an entry in Errors) for an unknown or golden id
            // or a card that already has one.
            bool Register(char const* cardId, EffectDef const& def);
            EffectDef const* Find(int cardIndex) const; // nullptr when the card has no entry
            int Size() const { return static_cast<int>(m_defs.size()); }
            std::vector<std::string> const& Errors() const { return m_errors; }
            // Every batch of HS_BG_EFFECT_BATCH linked into the program, built on first use.
            static EffectRegistry const& Global();
        private:
            std::vector<int> m_slot; // card index -> index in m_defs, -1 = none
            std::vector<EffectDef> m_defs;
            std::vector<std::string> m_errors;
        };

        using EffectBatchFn = void (*)(EffectRegistry&);
        // Adds a batch to the list Global() runs. Used through HS_BG_EFFECT_BATCH.
        struct EffectBatchRegistrar
        {
            EffectBatchRegistrar(char const* name, EffectBatchFn fn);
        };
        // Runs every linked batch, in batch-name order.
        void RegisterAllEffects(EffectRegistry& registry);

        // Forces the link of every effect batch translation unit. Their registrars have internal linkage, so a
        // static-library link drops those objects and Batches() stays empty; the glue calls this once at startup
        // (before the first EffectRegistry::Global() use). The anchors are defined in the batch files.
        void ForceLinkEffectBatches();
        void HsBgBatchAnchor_Beast();
        void HsBgBatchAnchor_Demon();
        void HsBgBatchAnchor_Dragon();
        void HsBgBatchAnchor_Elemental();
        void HsBgBatchAnchor_Mech();
        void HsBgBatchAnchor_Murloc();
        void HsBgBatchAnchor_Naga();
        void HsBgBatchAnchor_Neutral();
        void HsBgBatchAnchor_Pirate();
        void HsBgBatchAnchor_Heroes();
        void HsBgBatchAnchor_Quilboar();
    }
}

#define HS_BG_EFFECT_BATCH(name, fn) static ::Hs::Bg::EffectBatchRegistrar const g_hsBgBatch_##name(#name, fn)

#endif
