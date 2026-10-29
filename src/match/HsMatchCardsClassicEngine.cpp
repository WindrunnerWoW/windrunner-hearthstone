/*
 * Scripted Classic (EXPERT1) cards of the 5834 / 6024 snapshots, the cards that needed engine work (Lord
 * Jaraxxus, granted Deathrattles and triggers, temporary control ...) and the four reward-only promo cards.
 * See HsMatchInternal.h for the builder and the primitives, and HsMatchCardsClassic.cpp for the rules of thumb
 * ("$N" = Spell Damage, "#N" = healing, optional battlecry targets, empty scripts for keyword-only cards).
 *
 * Choices where the 5834 behaviour is not obvious (the mid-2014 behaviour of the real game is used):
 *  - Lord Jaraxxus: the new hero has 15 Health and a maximum of 15, loses its Armor and its Frozen state, gets the
 *    hero power INFERNO! (usable at once) and the weapon Blood Fury (replacing the old weapon). The Jaraxxus minion
 *    leaves play without dying (no death triggers). The hero is not treated as a Demon.
 *  - Granted enchantments (Soul of the Forest, Ancestral Spirit, Blessing of Wisdom) are enchantment scripts
 *    attached to the minion (Op::GrantScript): Silence removes them, they are lost when the minion leaves play,
 *    is bounced or transformed, and they stay when control changes. Granted Deathrattles run after the minion's own.
 *  - Shadow Madness: the minion may attack at once, returns at the end of the turn (destroyed when the owner's
 *    board is full); Silence or another Mind Control makes it permanent. Needs a free slot to be played.
 *  - Commanding Shout: a side flag for the rest of the turn, so minions played later are protected too.
 *  - Holy Wrath: damage = the cost of the drawn (or burned) card + Spell Damage; none when the deck is empty.
 *  - Alexstrasza: a hero's Health becomes 15 (the maximum rises to 15 when lower); not a heal.
 *  - The reward-only tokens PRO_001at (Murloc, I Am Murloc) and Mekka4t (Chicken, Poultryizer) are in the card dataset
 *    (they come along with their promo cards, cardlib.IMPLICIT_REFERENCES).
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
        // ---- Custom handlers ------------------------------------------------------------------

        // Void Terror: Battlecry: destroy the minions on either side of this minion and gain their Attack and Health
        // (the current values).
        void VoidTerrorFx(Effects& fx)
        {
            int attack = 0;
            int health = 0;
            for (EntityId id : fx.Select(Sel::AdjacentToSelf))
            {
                attack += fx.Attack(id);
                health += fx.Health(id);
                fx.Destroy(id);
            }
            if (attack > 0 || health > 0)
                fx.Buff(fx.Source(), attack, health);
        }

        // Elite Tauren Chieftain: Battlecry: each player gets a random Power Chord (two independent rolls).
        void EliteTaurenFx(Effects& fx)
        {
            Step own;
            own.op = Op::AddRandomToHand;
            own.sel = Sel::None;
            own.a = 1;
            own.list = {"PRO_001a", "PRO_001b", "PRO_001c"};
            fx.Run(own);
            Step other = own;
            other.b = 1; // into the enemy hand
            fx.Run(other);
        }

        // I Am Murloc: summon three, four or five 1/1 Murlocs.
        void IAmMurlocFx(Effects& fx)
        {
            int const n = 3 + fx.Rand(3);
            for (int i = 0; i < n; ++i)
                fx.Summon(fx.Side(), "PRO_001at", -1);
        }

        // Poultryizer: at the start of your turn, transform a random minion into a 1/1 Chicken.
        void PoultryizerFx(Effects& fx)
        {
            Step st;
            st.op = Op::Transform;
            st.sel = Sel::RandomMinion;
            st.card = "Mekka4t";
            fx.Run(st);
        }
    }

    void RegisterClassicEngineScripts(ScriptTable& t)
    {
        // ================= Lord Jaraxxus =================
        // Battlecry: destroy your hero and replace him with Lord Jaraxxus (15 Health, INFERNO!, Blood Fury).
        t.Add("EX1_323")
            .DoCard(Op::ReplaceHero, Sel::None, "EX1_323h")
            .DoCard(Op::ReplaceHeroPower, Sel::None, "EX1_tk33")
            .DoCard(Op::Equip, Sel::None, "EX1_323w")
            .Do(Op::RemoveFromPlay, Sel::Self);
        t.Add("EX1_323h"); // the hero card
        t.Add("EX1_323w"); // Blood Fury 3/8 (no text)
        // INFERNO!: summon a 6/6 Infernal.
        t.Add("EX1_tk33").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "EX1_tk34");
        t.Add("EX1_tk34"); // Infernal 6/6

        // ================= Granted enchantments =================
        // Soul of the Forest: give your minions "Deathrattle: Summon a 2/2 Treant."
        t.Add("EX1_158").DoCard(Op::GrantScript, Sel::FriendlyMinions, "EX1_158e");
        t.Add("EX1_158e").Deathrattle().DoCard(Op::Summon, Sel::None, "EX1_158t");
        t.Add("EX1_158t"); // Treant 2/2
        // Ancestral Spirit (6024): give a minion "Deathrattle: Resummon this minion." The enchantment is a real
        // granted Deathrattle: it resolves after the minion's own Deathrattle and earlier grants, Silence removes
        // it, and the resummoned minion is a fresh copy of the card (base stats, no enchantments).
        t.Add("CS2_038").Target(TargetKind::AnyMinion).DoCard(Op::GrantScript, Sel::Target, "CS2_038e");
        t.Add("CS2_038e").Deathrattle().Do(Op::Resummon, Sel::Subject);
        // Blessing of Wisdom: choose a minion. Whenever it attacks, the player who blessed it draws a card.
        t.Add("EX1_363").Target(TargetKind::AnyMinion).DoCard(Op::GrantScript, Sel::Target, "EX1_363e");
        t.Add("EX1_363e").On(On::Attacks, Who::Self).Do(Op::Draw, Sel::None, 1);

        // ================= Spells =================
        // Shadow Madness: gain control of an enemy minion with 3 or less Attack until end of turn.
        t.Add("EX1_334")
            .Target(TargetKind::EnemyMinion, kTgtMaxAttack, 3)
            .Req(kReqFreeSlot)
            .Do(Op::TakeControlTemp, Sel::Target);
        // Commanding Shout: your minions can't be reduced below 1 Health this turn. Draw a card.
        t.Add("NEW1_036").Do(Op::ProtectMinions, Sel::None).Do(Op::Draw, Sel::None, 1);
        // Headcrack: deal $2 damage to the enemy hero. Combo: return this to your hand next turn.
        t.Add("EX1_137")
            .Do(Op::Damage, Sel::EnemyHero, 2, 0, kStepSpellPower)
            .Combo()
            .Do(Op::Damage, Sel::EnemyHero, 2, 0, kStepSpellPower)
            .DoCard(Op::AddToHandNextTurn, Sel::None, "EX1_137");
        // Far Sight: draw a card. That card costs (3) less.
        t.Add("CS2_053").Do(Op::DrawDiscount, Sel::None, 3);
        // Holy Wrath: draw a card and deal damage equal to its cost (Spell Damage counts in 5834).
        t.Add("EX1_365").Target(TargetKind::AnyCharacter).Do(Op::DrawDealCost, Sel::Target, 0, 0, kStepSpellPower);
        // Thoughtsteal: copy 2 cards from your opponent's deck and put them into your hand.
        t.Add("EX1_339").Do(Op::CopyEnemyDeckCards, Sel::None, 2);
        // Flare: all minions lose Stealth. Destroy all enemy Secrets. Draw a card.
        t.Add("EX1_544")
            .Do(Op::RemoveKeyword, Sel::AllMinions, kKwStealth)
            .Do(Op::DestroyEnemySecrets, Sel::None)
            .Do(Op::Draw, Sel::None, 1);
        // Equality: change the Health of ALL minions to 1.
        t.Add("EX1_619").Do(Op::SetHealth, Sel::AllMinions, 1);
        // Betrayal: force an enemy minion to deal its damage to the minions next to it.
        t.Add("EX1_126").Target(TargetKind::EnemyMinion).Do(Op::DamageFromTarget, Sel::AdjacentToTarget);

        // ================= Minions =================
        // Pint-Sized Summoner: the first minion you play each turn costs (1) less.
        t.Add("EX1_076").Aura(AuraKind::Cost, AuraScope::OwnMinionCards, -1).FirstMinionOnly();
        // Mana Wraith: ALL minions cost (1) more.
        t.Add("EX1_616").Aura(AuraKind::Cost, AuraScope::AllMinionCards, 1);
        // Alexstrasza: Battlecry: set a hero's remaining Health to 15.
        t.Add("EX1_561").Target(TargetKind::AnyHero).Do(Op::SetHeroHealth, Sel::Target, 15);
        // Lorewalker Cho: whenever a player casts a spell, put a copy into the other player's hand.
        t.Add("EX1_100").On(On::SpellCast, Who::Any).Do(Op::CopySubjectToOtherHand, Sel::None);
        // Ethereal Arcanist: if you control a Secret at the end of your turn, gain +2/+2.
        t.Add("EX1_274").On(On::TurnEnd, Who::Friendly).When(Cond::HaveSecret).Do(Op::Buff, Sel::Self, 2, 2);
        // Secretkeeper: whenever a Secret is played (either side), gain +1/+1.
        t.Add("EX1_080").On(On::Played, Who::Any, kTrigSecret).Do(Op::Buff, Sel::Self, 1, 1);
        // Crazed Alchemist: Battlecry: swap the Attack and Health of a minion.
        t.Add("EX1_059").Target(TargetKind::AnyMinion, kTgtOptional).Do(Op::SwapAttackHealth, Sel::Target);
        // Void Terror: Battlecry: destroy the minions on either side of this minion and gain their Attack and Health.
        t.Add("EX1_304").Run(VoidTerrorFx);
        // Blood Knight: Battlecry: all minions lose Divine Shield. Gain +3/+3 for each Shield lost.
        t.Add("EX1_590")
            .Do(Op::RemoveKeyword, Sel::AllMinions, kKwDivineShield)
            .Do(Op::BuffPerRemoved, Sel::Self, 3, 3);
        // Deathwing: Battlecry: destroy all other minions and discard your hand.
        t.Add("NEW1_030").Do(Op::Destroy, Sel::AllOtherMinions).Do(Op::DiscardHand, Sel::None);

        // ================= Reward-only promo cards =================
        // Old Murk-Eye: Charge (from the definition). Has +1 Attack for each other Murloc on the battlefield.
        t.Add("EX1_062").Aura(AuraKind::AttackPerOtherRace, AuraScope::Self, 1, "MURLOC");
        // Captain's Parrot: Battlecry: put a random Pirate from your deck into your hand.
        t.Add("NEW1_016").Do(Op::TakeRandomFromDeck, Sel::None).Race("PIRATE");
        // Gelbin Mekkatorque: Battlecry: summon an AWESOME invention (one of the four, at random).
        t.Add("EX1_112").DoList(Op::SummonRandomList, Sel::None, {"Mekka1", "Mekka2", "Mekka3", "Mekka4"}, 1);
        // Homing Chicken: at the start of your turn, destroy this minion and draw 3 cards.
        t.Add("Mekka1").On(On::TurnStart, Who::Friendly).Do(Op::Destroy, Sel::Self).Do(Op::Draw, Sel::None, 3);
        // Repair Bot: at the end of your turn, restore 6 Health to a damaged character.
        t.Add("Mekka2").On(On::TurnEnd, Who::Friendly).Do(Op::Heal, Sel::RandomDamagedCharacter, 6);
        // Emboldener 3000: at the end of your turn, give a random minion +1/+1.
        t.Add("Mekka3").On(On::TurnEnd, Who::Friendly).Do(Op::Buff, Sel::RandomMinion, 1, 1);
        // Chicken: the 1/1 token of Poultryizer. Its text is only flavour ("Hey Chicken!"), so it needs a script entry.
        t.Add("Mekka4t");
        // Poultryizer: at the start of your turn, transform a random minion into a 1/1 Chicken.
        t.Add("Mekka4").On(On::TurnStart, Who::Friendly).Run(PoultryizerFx, {"Mekka4t"});
        // Elite Tauren Chieftain: Battlecry: give both players a random Power Chord.
        t.Add("PRO_001").Run(EliteTaurenFx, {"PRO_001a", "PRO_001b", "PRO_001c"});
        // I Am Murloc: summon three, four or five 1/1 Murlocs.
        t.Add("PRO_001a").Req(kReqFreeSlot).Run(IAmMurlocFx, {"PRO_001at"});
        // Rogues Do It...: deal $4 damage. Draw a card.
        t.Add("PRO_001b")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 4, 0, kStepSpellPower)
            .Do(Op::Draw, Sel::None, 1);
        // Power of the Horde: summon a random Horde Warrior (the entourage of the card data).
        t.Add("PRO_001c")
            .Req(kReqFreeSlot)
            .DoList(Op::SummonRandomList, Sel::None, {"CS2_121", "EX1_021", "EX1_023", "EX1_110", "EX1_390", "CS2_179"}, 1);
    }
}
}
}
