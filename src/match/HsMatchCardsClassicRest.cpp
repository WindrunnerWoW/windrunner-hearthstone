/*
 * Scripted Classic (EXPERT1) cards of the 5834 snapshot, the rest of the Classic cards that the
 * Primitives (plus small additions) express.
 * See HsMatchInternal.h for the builder and the primitives, and HsMatchCardsClassic.cpp for the rules of thumb
 * ("$N" = Spell Damage, "#N" = healing, optional battlecry targets, empty scripts for keyword-only cards).
 */

#include "HsMatchInternal.h"

#include <string>
#include <vector>

namespace Hs
{
namespace Match
{
namespace Detail
{
    namespace
    {
        Step OneStep(Op op, Sel sel, int a = 0, int b = 0)
        {
            Step st;
            st.op = op;
            st.sel = sel;
            st.a = a;
            st.b = b;
            return st;
        }

        // ---- Custom handlers ------------------------------------------------------------------

        // Savagery: damage equal to your hero's Attack to a minion. 2014 Hearthstone: Spell Damage adds to it (the
        // lead's decision; the 5834 text has no "$"). Nothing happens for 0 damage.
        void SavageryFx(Effects& fx)
        {
            if (fx.Target() == 0)
                return;
            int const dmg = (fx.Attack(fx.HeroOf(fx.Side())) + fx.SpellPower()) * fx.DamageMultiplier();
            if (dmg > 0)
                fx.Damage(fx.Target(), dmg);
        }

        // Unleash the Hounds: for each enemy minion summon a 1/1 Hound with Charge (counted before the Hounds come).
        void UnleashFx(Effects& fx)
        {
            std::size_t const n = fx.Select(Sel::EnemyMinions).size();
            for (std::size_t i = 0; i < n; ++i)
                if (fx.Summon(fx.Side(), "EX1_538t", -1) == 0)
                    break;
        }

        // Hungry Crab: Battlecry: destroy a Murloc and gain +2/+2 (without a Murloc target nothing happens).
        void HungryCrabFx(Effects& fx)
        {
            EntityId const t = fx.Target();
            if (t == 0 || !fx.IsMinion(t))
                return;
            fx.Destroy(t);
            fx.Buff(fx.Source(), 2, 2);
        }

        // Nat Pagle: at the start of your turn, a 50% chance to draw an extra card.
        void NatPagleFx(Effects& fx)
        {
            if (fx.Rand(2) == 0)
                fx.Draw(fx.Side(), 1);
        }

        // Twilight Drake: Battlecry: gain +1 Health for each card in your hand (the Drake itself is not in it).
        void TwilightDrakeFx(Effects& fx)
        {
            int const n = fx.HandCount(fx.Side());
            if (n > 0)
                fx.Buff(fx.Source(), 0, n);
        }

        // Stampeding Kodo: Battlecry: destroy a random enemy minion with 2 or less Attack (not targeted: a
        // Stealthed minion counts).
        void KodoFx(Effects& fx)
        {
            std::vector<EntityId> pool;
            for (EntityId id : fx.Select(Sel::EnemyMinions))
                if (fx.Attack(id) <= 2)
                    pool.push_back(id);
            if (!pool.empty())
                fx.Destroy(pool[static_cast<std::size_t>(fx.Rand(static_cast<int>(pool.size())))]);
        }

        // Harrison Jones: Battlecry: destroy your opponent's weapon and draw cards equal to its Durability.
        void HarrisonFx(Effects& fx)
        {
            int const d = fx.WeaponDurability(1 - fx.Side());
            if (d <= 0)
                return;
            fx.Run(OneStep(Op::DestroyWeapon, Sel::EnemyHero));
            fx.Draw(fx.Side(), d);
        }

        // Divine Favor: draw cards until you have as many in hand as your opponent (the spell has left the hand).
        void DivineFavorFx(Effects& fx)
        {
            int const n = fx.HandCount(1 - fx.Side()) - fx.HandCount(fx.Side());
            if (n > 0)
                fx.Draw(fx.Side(), n);
        }

        // Blade Flurry: destroy your weapon and deal its damage to all enemies. 2014: the weapon's Attack plus Spell
        // Damage (and Prophet Velen's doubling), like a "$" spell.
        void BladeFlurryFx(Effects& fx)
        {
            int const side = fx.Side();
            int const atk = fx.WeaponAttack(side);
            if (fx.WeaponDurability(side) <= 0)
                return;
            fx.Run(OneStep(Op::DestroyWeapon, Sel::OwnHero));
            int const dmg = (atk + fx.SpellPower()) * fx.DamageMultiplier();
            if (dmg <= 0)
                return;
            for (EntityId id : fx.Select(Sel::EnemyCharacters))
                fx.Damage(id, dmg);
        }

        // Demonfire: $2 damage to a minion; a friendly Demon gets +2/+2 instead.
        void DemonfireFx(Effects& fx)
        {
            EntityId const t = fx.Target();
            if (t == 0)
                return;
            if (fx.SideOf(t) == fx.Side() && fx.RaceOf(t) == "DEMON")
                fx.Buff(t, 2, 2);
            else
                fx.Damage(t, (2 + fx.SpellPower()) * fx.DamageMultiplier());
        }
    }

    void RegisterClassicRestScripts(ScriptTable& t)
    {
        // ================= Druid =================
        // Naturalize: destroy a minion. Your opponent draws 2 cards.
        t.Add("EX1_161").Target(TargetKind::AnyMinion).Do(Op::Destroy, Sel::Target).Do(Op::Draw, Sel::EnemyHero, 2);
        // Savagery: deal damage equal to your hero's Attack to a minion.
        t.Add("EX1_578").Target(TargetKind::AnyMinion).Run(SavageryFx);
        // Force of Nature: summon three 2/2 Treants with Charge that die at the end of the turn.
        t.Add("EX1_571").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "EX1_tk9", 3);
        // The Treant: Charge (definition); at the end of the turn destroy this minion (a Silence removes that).
        t.Add("EX1_tk9").On(On::TurnEnd, Who::Any).Do(Op::Destroy, Sel::Self);
        // Cenarius: Choose One - give your other minions +2/+2; or summon two 2/2 Treants with Taunt. No free-slot
        // requirement: with a nearly full board fewer Treants appear.
        t.Add("EX1_573").ChooseOne("EX1_573a", "EX1_573b");
        t.Add("EX1_573a").Do(Op::Buff, Sel::OtherFriendlyMinions, 2, 2);
        t.Add("EX1_573b").DoCard(Op::Summon, Sel::None, "EX1_573t", 2);
        t.Add("EX1_573t"); // Treant (Taunt from the definition)

        // ================= Hunter =================
        // Bestial Wrath: give a Beast (any side's, as in 2014) +2 Attack and Immune this turn.
        t.Add("EX1_549")
            .Target(TargetKind::AnyMinion, kTgtRace, 0, "BEAST")
            .Do(Op::BuffTemp, Sel::Target, 2)
            .Do(Op::KeywordTemp, Sel::Target, kKwImmune);
        // Scavenging Hyena: whenever a friendly Beast dies, gain +2/+1.
        t.Add("EX1_531").On(On::Died, Who::Friendly, kTrigMinion | kTrigRace, 0, "BEAST").Do(Op::Buff, Sel::Self, 2, 1);
        // Unleash the Hounds: for each enemy minion summon a 1/1 Hound with Charge (needs an enemy minion).
        t.Add("EX1_538").Req(kReqMinEnemyMinions, 1).Run(UnleashFx, {"EX1_538t"});
        t.Add("EX1_538t"); // Hound (Charge from the definition)
        // Explosive Shot: $5 damage to a minion and $2 damage to adjacent ones.
        t.Add("EX1_537")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower)
            .Do(Op::Damage, Sel::AdjacentToTarget, 2, 0, kStepSpellPower);

        // ================= Mage =================
        // Archmage Antonidas: whenever you cast a spell, put a Fireball into your hand (on the cast).
        t.Add("EX1_559").On(On::SpellCast, Who::Friendly).DoCard(Op::AddToHand, Sel::OwnHero, "CS2_029");
        // Pyroblast: deal $10 damage.
        t.Add("EX1_279").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 10, 0, kStepSpellPower);

        // ================= Neutral =================
        // Hungry Crab: Battlecry: destroy a Murloc and gain +2/+2.
        t.Add("NEW1_017").Target(TargetKind::AnyMinion, kTgtOptional | kTgtRace, 0, "MURLOC").Run(HungryCrabFx);
        // Mana Addict: whenever you cast a spell, gain +2 Attack this turn.
        t.Add("EX1_055").On(On::SpellCast, Who::Friendly).Do(Op::BuffTemp, Sel::Self, 2);
        // Sunfury Protector: Battlecry: give adjacent minions Taunt.
        t.Add("EX1_058").Do(Op::Keyword, Sel::AdjacentToSelf, kKwTaunt);
        // Nat Pagle: at the start of your turn, a 50% chance to draw an extra card.
        t.Add("EX1_557").On(On::TurnStart, Who::Friendly).Run(NatPagleFx);
        // Faerie Dragon: can't be targeted by Spells or Hero Powers (definition: elusive).
        t.Add("NEW1_023");
        // Master Swordsmith: at the end of your turn, give another random friendly minion +1 Attack.
        t.Add("NEW1_037").On(On::TurnEnd, Who::Friendly).Do(Op::Buff, Sel::RandomOtherFriendlyMinion, 1, 0);
        // Coldlight Oracle: Battlecry: each player draws 2 cards.
        t.Add("EX1_050").Do(Op::Draw, Sel::OwnHero, 2).Do(Op::Draw, Sel::EnemyHero, 2);
        // Tinkmaster Overspark: Battlecry: transform another random minion into a 5/5 Devilsaur or a 1/1 Squirrel.
        t.Add("EX1_083").DoList(Op::TransformRandomList, Sel::RandomOtherMinion, {"EX1_tk29", "EX1_tk28"});
        // Coldlight Seer: Battlecry: give ALL other Murlocs +2 Health.
        t.Add("EX1_103").Do(Op::Buff, Sel::AllOtherMinions, 0, 2).Race("MURLOC");
        // Twilight Drake: Battlecry: gain +1 Health for each card in your hand.
        t.Add("EX1_043").Run(TwilightDrakeFx);
        // Ancient Brewmaster: Battlecry: return a friendly minion from the battlefield to your hand.
        t.Add("EX1_057").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Bounce, Sel::Target);
        // Ancient Mage: Battlecry: give adjacent minions Spell Damage +1.
        t.Add("EX1_584").Do(Op::SpellDamageBuff, Sel::AdjacentToSelf, 1);
        // Silver Hand Knight: Battlecry: summon a 2/2 Squire.
        t.Add("CS2_151").DoCard(Op::Summon, Sel::None, "CS2_152");
        // Harrison Jones: Battlecry: destroy your opponent's weapon and draw cards equal to its Durability.
        t.Add("EX1_558").Run(HarrisonFx);
        // Stampeding Kodo: Battlecry: destroy a random enemy minion with 2 or less Attack.
        t.Add("NEW1_041").Run(KodoFx);
        // The Beast: Deathrattle: summon a 3/3 Finkle Einhorn for your opponent.
        t.Add("EX1_577").Deathrattle().DoCard(Op::Summon, Sel::None, "EX1_finkle", 1, 1);
        // Illidan Stormrage: whenever you play a card, summon a 2/1 Flame of Azzinoth.
        t.Add("EX1_614").On(On::Played, Who::Friendly).DoCard(Op::Summon, Sel::None, "EX1_614t");
        // Hogger: at the end of your turn, summon a 2/2 Gnoll with Taunt.
        t.Add("NEW1_040").On(On::TurnEnd, Who::Friendly).DoCard(Op::Summon, Sel::None, "NEW1_040t");
        // Baron Geddon: at the end of your turn, deal 2 damage to ALL other characters.
        t.Add("EX1_249").On(On::TurnEnd, Who::Friendly).Do(Op::Damage, Sel::AllOtherCharacters, 2);
        // Gruul: at the end of each turn, gain +1/+1.
        t.Add("NEW1_038").On(On::TurnEnd, Who::Any).Do(Op::Buff, Sel::Self, 1, 1);
        // Onyxia: Battlecry: summon 1/1 Whelps until your side of the battlefield is full (5834: ds1_whelptoken).
        t.Add("EX1_562").DoCard(Op::Summon, Sel::None, "ds1_whelptoken", 7);

        // ================= Paladin =================
        // Divine Favor: draw cards until you have as many in hand as your opponent.
        t.Add("EX1_349").Run(DivineFavorFx);
        // Aldor Peacekeeper: Battlecry: change an enemy minion's Attack to 1.
        t.Add("EX1_382").Target(TargetKind::EnemyMinion, kTgtOptional).Do(Op::SetAttack, Sel::Target, 1);
        // Blessed Champion: double a minion's Attack.
        t.Add("EX1_355").Target(TargetKind::AnyMinion).Do(Op::DoubleAttack, Sel::Target);
        // Avenging Wrath: $8 damage randomly split among enemy characters (Spell Damage adds missiles).
        t.Add("EX1_384")
            .Do(Op::Damage, Sel::RandomEnemyCharacter, 1, 0, kStepRepeatRandom | kStepCountSpellPower)
            .Times(8);
        // Lay on Hands: restore #8 Health. Draw 3 cards.
        t.Add("EX1_354").Target(TargetKind::AnyCharacter).Do(Op::Heal, Sel::Target, 8).Do(Op::Draw, Sel::None, 3);
        // Tirion Fordring: Divine Shield, Taunt (definition). Deathrattle: equip a 5/3 Ashbringer.
        t.Add("EX1_383").Deathrattle().DoCard(Op::Equip, Sel::None, "EX1_383t");
        t.Add("EX1_383t");

        // ================= Priest =================
        // Cabal Shadow Priest: Battlecry: take control of an enemy minion with 2 or less Attack (needs room).
        t.Add("EX1_091")
            .Target(TargetKind::EnemyMinion, kTgtOptional | kTgtMaxAttack | kTgtRoomToSteal, 2)
            .Do(Op::TakeControl, Sel::Target);
        // Temple Enforcer: Battlecry: give a friendly minion +3 Health.
        t.Add("EX1_623").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Buff, Sel::Target, 0, 3);

        // ================= Rogue =================
        // Shadowstep: return a friendly minion to your hand. It costs (2) less.
        t.Add("EX1_144").Target(TargetKind::FriendlyMinion).Do(Op::Bounce, Sel::Target, 0, -2);
        // Blade Flurry: destroy your weapon and deal its damage to all enemies (needs a weapon).
        t.Add("CS2_233").Req(kReqWeapon).Run(BladeFlurryFx);

        // ================= Shaman =================
        // Mana Tide Totem: at the end of your turn, draw a card.
        t.Add("EX1_575").On(On::TurnEnd, Who::Friendly).Do(Op::Draw, Sel::None, 1);

        // ================= Warlock =================
        // Demonfire: $2 damage to a minion; a friendly Demon gets +2/+2 instead.
        t.Add("EX1_596").Target(TargetKind::AnyMinion).Run(DemonfireFx);

        // ================= Warrior =================
        // Inner Rage: deal $1 damage to a minion and give it +2 Attack.
        t.Add("EX1_607")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower)
            .Do(Op::Buff, Sel::Target, 2, 0);
        // Rampage: give a damaged minion +3/+3.
        t.Add("CS2_104").Target(TargetKind::AnyMinion, kTgtDamaged).Do(Op::Buff, Sel::Target, 3, 3);
    }
}
}
}
