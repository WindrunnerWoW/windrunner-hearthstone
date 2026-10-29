/*
 * Scripted Basic (CORE) cards of the 5834 snapshot. Keyword-only minions (Taunt, Charge, ...) and
 * weapons without text need no script. See HsMatchInternal.h for the builder and the primitives.
 */

#include "HsMatchInternal.h"

namespace Hs
{
namespace Match
{
namespace Detail
{
    namespace
    {
        // Frostwolf Warlord: gain +1/+1 for each other friendly minion on the battlefield.
        void FrostwolfWarlord(Effects& fx)
        {
            int const others = static_cast<int>(fx.Select(Sel::OtherFriendlyMinions).size());
            if (others > 0)
                fx.Buff(fx.Source(), others, others);
        }
    }

    void RegisterBasicScripts(ScriptTable& t)
    {
        // ---- Mage ----
        // Arcane Missiles: 3 damage randomly split among enemy characters.
        t.Add("EX1_277").Do(Op::Damage, Sel::RandomEnemyCharacter, 1, 0, kStepRepeatRandom | kStepCountSpellPower).Times(3);
        // Arcane Explosion: 1 damage to all enemy minions.
        t.Add("CS2_025").Do(Op::Damage, Sel::EnemyMinions, 1, 0, kStepSpellPower);
        // Arcane Intellect: draw 2.
        t.Add("CS2_023").Do(Op::Draw, Sel::None, 2);
        // Fireball: 6 damage.
        t.Add("CS2_029").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 6, 0, kStepSpellPower);
        // Polymorph: transform a minion into a 1/1 Sheep.
        t.Add("CS2_022").Target(TargetKind::AnyMinion).DoCard(Op::Transform, Sel::Target, kSheepId);
        // Frostbolt: 3 damage to a character and Freeze it.
        t.Add("CS2_024").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 3, 0, kStepSpellPower).Do(Op::Freeze, Sel::Target);
        // Water Elemental: Freeze any character damaged by this minion.
        t.Add("CS2_033").On(On::DealtDamage, Who::Self).Do(Op::Freeze, Sel::Subject);

        // ---- Warrior ----
        // Execute: destroy a damaged enemy minion.
        t.Add("CS2_108").Target(TargetKind::EnemyMinion, kTgtDamaged).Do(Op::Destroy, Sel::Target);
        // Whirlwind: 1 damage to ALL minions.
        t.Add("EX1_400").Do(Op::Damage, Sel::AllMinions, 1, 0, kStepSpellPower);
        // Heroic Strike: +4 hero attack this turn.
        t.Add("CS2_105").Do(Op::BuffTemp, Sel::OwnHero, 4);
        // Cleave: 2 damage to two random enemy minions (needs two).
        t.Add("CS2_114").Req(kReqMinEnemyMinions, 2).Do(Op::Damage, Sel::RandomEnemyMinion, 2, 0, kStepSpellPower).Times(2);
        // Charge: a friendly minion gets +2 attack and Charge.
        t.Add("CS2_103").Target(TargetKind::FriendlyMinion).Do(Op::Buff, Sel::Target, 2, 0).Do(Op::Keyword, Sel::Target, kKwCharge);
        // Shield Block: 5 armor, draw a card.
        t.Add("EX1_606").Do(Op::Armor, Sel::OwnHero, 5).Do(Op::Draw, Sel::None, 1);
        // Warsong Commander: whenever you summon a minion with 3 or less attack, give it Charge.
        t.Add("EX1_084").On(On::Summoned, Who::OtherFriendly, kTrigMaxAttack, 3).Do(Op::Keyword, Sel::Subject, kKwCharge);

        // ---- Druid ----
        // Wild Growth: gain an empty mana crystal.
        t.Add("CS2_013").Do(Op::GainCrystals, Sel::None, 1, 0).Ref(kExcessManaId);

        // ---- Hunter ----
        // Tracking: look at the top three cards of your deck; draw one, discard the others.
        t.Add("DS1_184").Do(Op::Pick, Sel::None, 3);

        // ---- Priest ----
        // Mind Control: take control of an enemy minion.
        t.Add("CS1_113").Target(TargetKind::EnemyMinion).Req(kReqFreeSlot).Do(Op::TakeControl, Sel::Target);

        // ---- Warlock ----
        // Corruption: choose an enemy minion; at the start of your turn, destroy it.
        t.Add("CS2_063").Target(TargetKind::EnemyMinion).Do(Op::DelayedDestroy, Sel::Target, 0, 0);

        // ---- Neutral ----
        // Stormwind Champion: your other minions have +1/+1.
        t.Add("CS2_222").Aura(AuraKind::AttackHealth, AuraScope::OtherFriendlyMinions, 1);
        // Novice Engineer: Battlecry: draw a card.
        t.Add("EX1_015").Do(Op::Draw, Sel::None, 1);
        // Nightblade: Battlecry: 3 damage to the enemy hero.
        t.Add("EX1_593").Do(Op::Damage, Sel::EnemyHero, 3);
        // Raid Leader: your other minions have +1 attack.
        t.Add("CS2_122").Aura(AuraKind::Attack, AuraScope::OtherFriendlyMinions, 1);

        // ---- Mage (continued) ----
        // Frost Nova: Freeze all enemy minions.
        t.Add("CS2_026").Do(Op::Freeze, Sel::EnemyMinions);
        // Mirror Image: summon two 0/2 minions with Taunt (the second only when there is room).
        t.Add("CS2_027").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "CS2_mirror", 2);
        // Flamestrike: 4 damage to all enemy minions.
        t.Add("CS2_032").Do(Op::Damage, Sel::EnemyMinions, 4, 0, kStepSpellPower);

        // ---- Shaman ----
        // Frost Shock: 1 damage to an enemy character and Freeze it.
        t.Add("CS2_037")
            .Target(TargetKind::EnemyCharacter)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower)
            .Do(Op::Freeze, Sel::Target);
        // Windfury: give a minion Windfury.
        t.Add("CS2_039").Target(TargetKind::AnyMinion).Do(Op::Keyword, Sel::Target, kKwWindfury);
        // Ancestral Healing: restore a minion to full health and give it Taunt.
        t.Add("CS2_041").Target(TargetKind::AnyMinion).Do(Op::HealFull, Sel::Target).Do(Op::Keyword, Sel::Target, kKwTaunt);
        // Fire Elemental: Battlecry: 3 damage.
        t.Add("CS2_042").Target(TargetKind::AnyCharacter, kTgtOptional).Do(Op::Damage, Sel::Target, 3);
        // Rockbiter Weapon: a friendly character gets +3 attack this turn.
        t.Add("CS2_045").Target(TargetKind::FriendlyCharacter).Do(Op::BuffTemp, Sel::Target, 3);
        // Bloodlust: your minions get +3 attack this turn.
        t.Add("CS2_046").Do(Op::BuffTemp, Sel::FriendlyMinions, 3);
        // Totemic Might: your Totems get +2 health.
        t.Add("EX1_244").Do(Op::Buff, Sel::FriendlyMinions, 0, 2).Race("TOTEM");
        // Hex: transform a minion into a 0/1 Frog with Taunt.
        t.Add("EX1_246").Target(TargetKind::AnyMinion).DoCard(Op::Transform, Sel::Target, "hexfrog");
        // Flametongue Totem: adjacent minions have +2 attack.
        t.Add("EX1_565").Aura(AuraKind::Attack, AuraScope::Adjacent, 2);
        // Windspeaker: Battlecry: give a friendly minion Windfury.
        t.Add("EX1_587").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Keyword, Sel::Target, kKwWindfury);

        // ---- Warlock (continued) ----
        // Shadow Bolt: 4 damage to a minion.
        t.Add("CS2_057").Target(TargetKind::AnyMinion).Do(Op::Damage, Sel::Target, 4, 0, kStepSpellPower);
        // Drain Life: 2 damage, restore 2 health to your hero.
        t.Add("CS2_061")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower)
            .Do(Op::Heal, Sel::OwnHero, 2);
        // Hellfire: 3 damage to ALL characters.
        t.Add("CS2_062").Do(Op::Damage, Sel::AllCharacters, 3, 0, kStepSpellPower);
        // Dread Infernal: Battlecry: 1 damage to ALL other characters.
        t.Add("CS2_064").Do(Op::Damage, Sel::AllOtherCharacters, 1);
        // Mortal Coil: 1 damage to a minion; if that kills it, draw a card.
        t.Add("EX1_302")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower)
            .If(Cond::TargetDead)
            .Do(Op::Draw, Sel::None, 1);
        // Succubus: Battlecry: discard a random card.
        t.Add("EX1_306").Do(Op::DiscardRandom, Sel::None, 1);
        // Soulfire: 4 damage, discard a random card.
        t.Add("EX1_308")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 4, 0, kStepSpellPower)
            .Do(Op::DiscardRandom, Sel::None, 1);
        // Sacrificial Pact: destroy a Demon, restore 5 health to your hero.
        t.Add("NEW1_003")
            .Target(TargetKind::AnyMinion, kTgtRace, 0, "DEMON")
            .Do(Op::Destroy, Sel::Target)
            .Do(Op::Heal, Sel::OwnHero, 5);

        // ---- Rogue ----
        // Backstab: 2 damage to an undamaged minion.
        t.Add("CS2_072").Target(TargetKind::AnyMinion, kTgtUndamaged).Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower);
        // Deadly Poison: your weapon gets +2 attack (needs a weapon).
        t.Add("CS2_074").Req(kReqWeapon).Do(Op::WeaponBuff, Sel::OwnHero, 2, 0);
        // Sinister Strike: 3 damage to the enemy hero.
        t.Add("CS2_075").Do(Op::Damage, Sel::EnemyHero, 3, 0, kStepSpellPower);
        // Assassinate: destroy an enemy minion.
        t.Add("CS2_076").Target(TargetKind::EnemyMinion).Do(Op::Destroy, Sel::Target);
        // Sprint: draw 4 cards.
        t.Add("CS2_077").Do(Op::Draw, Sel::None, 4);
        // Fan of Knives: 1 damage to all enemy minions, draw a card.
        t.Add("EX1_129").Do(Op::Damage, Sel::EnemyMinions, 1, 0, kStepSpellPower).Do(Op::Draw, Sel::None, 1);
        // Shiv: 1 damage, draw a card.
        t.Add("EX1_278")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower)
            .Do(Op::Draw, Sel::None, 1);
        // Sap: return an enemy minion to its owner's hand.
        t.Add("EX1_581").Target(TargetKind::EnemyMinion).Do(Op::Bounce, Sel::Target);
        // Vanish: return all minions to their owner's hand.
        t.Add("NEW1_004").Do(Op::Bounce, Sel::AllMinions);

        // ---- Hunter (continued) ----
        // Hunter's Mark: change a minion's health to 1.
        t.Add("CS2_084").Target(TargetKind::AnyMinion).Do(Op::SetHealth, Sel::Target, 1);
        // Starving Buzzard: whenever you summon a Beast, draw a card.
        t.Add("CS2_237").On(On::Summoned, Who::OtherFriendly, kTrigRace, 0, "BEAST").Do(Op::Draw, Sel::None, 1);
        // Houndmaster: Battlecry: give a friendly Beast +2/+2 and Taunt.
        t.Add("DS1_070")
            .Target(TargetKind::FriendlyMinion, kTgtOptional | kTgtRace, 0, "BEAST")
            .Do(Op::Buff, Sel::Target, 2, 2)
            .Do(Op::Keyword, Sel::Target, kKwTaunt);
        // Timber Wolf: your other Beasts have +1 attack.
        t.Add("DS1_175").Aura(AuraKind::Attack, AuraScope::OtherFriendlyMinions, 1, "BEAST");
        // Tundra Rhino: your Beasts have Charge.
        t.Add("DS1_178").Aura(AuraKind::Keyword, AuraScope::FriendlyMinions, kKwCharge, "BEAST");
        // Multi-Shot: 3 damage to two random enemy minions (needs two).
        t.Add("DS1_183").Req(kReqMinEnemyMinions, 2).Do(Op::Damage, Sel::RandomEnemyMinion, 3, 0, kStepSpellPower).Times(2);
        // Arcane Shot: 2 damage.
        t.Add("DS1_185").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower);
        // Kill Command: 3 damage; 5 if you have a Beast.
        t.Add("EX1_539")
            .Target(TargetKind::AnyCharacter)
            .If(Cond::HaveRace, "BEAST")
            .Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower)
            .If(Cond::NoRace, "BEAST")
            .Do(Op::Damage, Sel::Target, 3, 0, kStepSpellPower);
        // Animal Companion: summon a random Beast Companion (Misha, Leokk or Huffer; not unique).
        t.Add("NEW1_031").Req(kReqFreeSlot).DoList(Op::SummonRandomList, Sel::None, {"NEW1_032", "NEW1_033", "NEW1_034"}, 1);
        // Leokk: your other minions have +1 attack.
        t.Add("NEW1_033").Aura(AuraKind::Attack, AuraScope::OtherFriendlyMinions, 1);

        // ---- Paladin ----
        // Blessing of Might: give a minion +3 attack.
        t.Add("CS2_087").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 3, 0);
        // Guardian of Kings: Battlecry: restore 6 health to your hero.
        t.Add("CS2_088").Do(Op::Heal, Sel::OwnHero, 6);
        // Holy Light: restore 6 health.
        t.Add("CS2_089").Target(TargetKind::AnyCharacter).Do(Op::Heal, Sel::Target, 6);
        // Blessing of Kings: give a minion +4/+4.
        t.Add("CS2_092").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 4, 4);
        // Consecration: 2 damage to all enemies.
        t.Add("CS2_093").Do(Op::Damage, Sel::EnemyCharacters, 2, 0, kStepSpellPower);
        // Hammer of Wrath: 3 damage, draw a card.
        t.Add("CS2_094")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 3, 0, kStepSpellPower)
            .Do(Op::Draw, Sel::None, 1);
        // Truesilver Champion: whenever your hero attacks, restore 2 health to it.
        t.Add("CS2_097").On(On::Attacks, Who::OwnHero).Do(Op::Heal, Sel::OwnHero, 2);
        // Humility: change a minion's attack to 1.
        t.Add("EX1_360").Target(TargetKind::AnyMinion).Do(Op::SetAttack, Sel::Target, 1);
        // Hand of Protection: give a minion Divine Shield.
        t.Add("EX1_371").Target(TargetKind::AnyMinion).Do(Op::Keyword, Sel::Target, kKwDivineShield);

        // ---- Priest (continued) ----
        // Holy Nova: 2 damage to all enemies, restore 2 health to all friendly characters.
        t.Add("CS1_112").Do(Op::Damage, Sel::EnemyCharacters, 2, 0, kStepSpellPower).Do(Op::Heal, Sel::FriendlyCharacters, 2);
        // Holy Smite: 2 damage.
        t.Add("CS1_130").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower);
        // Mind Vision: put a copy of a random card in your opponent's hand into your hand.
        t.Add("CS2_003").Do(Op::CopyEnemyHandCard, Sel::None, 1);
        // Power Word: Shield: give a minion +2 health, draw a card.
        t.Add("CS2_004").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 0, 2).Do(Op::Draw, Sel::None, 1);
        // Shadow Word: Pain: destroy a minion with 3 or less attack.
        t.Add("CS2_234").Target(TargetKind::AnyMinion, kTgtMaxAttack, 3).Do(Op::Destroy, Sel::Target);
        // Northshire Cleric: whenever a minion is healed, draw a card.
        t.Add("CS2_235").On(On::Healed, Who::Any, kTrigMinion).Do(Op::Draw, Sel::None, 1);
        // Divine Spirit: double a minion's health.
        t.Add("CS2_236").Target(TargetKind::AnyMinion).Do(Op::DoubleHealth, Sel::Target);
        // Mind Blast: 5 damage to the enemy hero.
        t.Add("DS1_233").Do(Op::Damage, Sel::EnemyHero, 5, 0, kStepSpellPower);
        // Shadow Word: Death: destroy a minion with 5 or more attack.
        t.Add("EX1_622").Target(TargetKind::AnyMinion, kTgtMinAttack, 5).Do(Op::Destroy, Sel::Target);

        // ---- Druid (continued) ----
        // Claw: your hero gets +2 attack this turn and 2 armor.
        t.Add("CS2_005").Do(Op::BuffTemp, Sel::OwnHero, 2).Do(Op::Armor, Sel::OwnHero, 2);
        // Healing Touch: restore 8 health.
        t.Add("CS2_007").Target(TargetKind::AnyCharacter).Do(Op::Heal, Sel::Target, 8);
        // Moonfire: 1 damage.
        t.Add("CS2_008").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower);
        // Mark of the Wild: give a minion Taunt and +2/+2.
        t.Add("CS2_009").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 2, 2).Do(Op::Keyword, Sel::Target, kKwTaunt);
        // Savage Roar: your characters get +2 attack this turn.
        t.Add("CS2_011").Do(Op::BuffTemp, Sel::FriendlyCharacters, 2);
        // Swipe: 4 damage to an enemy and 1 damage to all other enemies.
        t.Add("CS2_012")
            .Target(TargetKind::EnemyCharacter)
            .Do(Op::Damage, Sel::Target, 4, 0, kStepSpellPower)
            .Do(Op::Damage, Sel::AllOtherThanTarget, 1, 0, kStepSpellPower);
        // Innervate: gain 2 mana crystals this turn only.
        t.Add("EX1_169").Do(Op::GainMana, Sel::None, 2);
        // Starfire: 5 damage, draw a card.
        t.Add("EX1_173")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower)
            .Do(Op::Draw, Sel::None, 1);

        // ---- Neutral (continued) ----
        // Ironforge Rifleman, Elven Archer: Battlecry: 1 damage. Stormpike Commando: 2 damage.
        t.Add("CS2_141").Target(TargetKind::AnyCharacter, kTgtOptional | kTgtNonSelf).Do(Op::Damage, Sel::Target, 1);
        t.Add("CS2_189").Target(TargetKind::AnyCharacter, kTgtOptional | kTgtNonSelf).Do(Op::Damage, Sel::Target, 1);
        t.Add("CS2_150").Target(TargetKind::AnyCharacter, kTgtOptional | kTgtNonSelf).Do(Op::Damage, Sel::Target, 2);
        // Gnomish Inventor: Battlecry: draw a card.
        t.Add("CS2_147").Do(Op::Draw, Sel::None, 1);
        // Razorfen Hunter: Battlecry: summon a 1/1 Boar.
        t.Add("CS2_196").DoCard(Op::Summon, Sel::None, "CS2_boar");
        // Frostwolf Warlord: Battlecry: gain +1/+1 for each other friendly minion on the battlefield.
        t.Add("CS2_226").Run(FrostwolfWarlord);
        // Darkscale Healer: Battlecry: restore 2 health to all friendly characters.
        t.Add("DS1_055").Do(Op::Heal, Sel::FriendlyCharacters, 2);
        // Voodoo Doctor: Battlecry: restore 2 health.
        t.Add("EX1_011").Target(TargetKind::AnyCharacter, kTgtOptional).Do(Op::Heal, Sel::Target, 2);
        // Shattered Sun Cleric: Battlecry: give a friendly minion +1/+1.
        t.Add("EX1_019").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Buff, Sel::Target, 1, 1);
        // Dragonling Mechanic: Battlecry: summon a 2/1 Mechanical Dragonling.
        t.Add("EX1_025").DoCard(Op::Summon, Sel::None, "EX1_025t");
        // Acidic Swamp Ooze: Battlecry: destroy your opponent's weapon.
        t.Add("EX1_066").Do(Op::DestroyWeapon, Sel::EnemyHero);
        // Gurubashi Berserker: whenever this minion takes damage, gain +3 attack.
        t.Add("EX1_399").On(On::Damaged, Who::Self).Do(Op::Buff, Sel::Self, 3, 0);
        // Murloc Tidehunter: Battlecry: summon a 1/1 Murloc Scout.
        t.Add("EX1_506").DoCard(Op::Summon, Sel::None, "EX1_506a");
        // Grimscale Oracle: ALL other Murlocs have +1 attack.
        t.Add("EX1_508").Aura(AuraKind::Attack, AuraScope::AllOtherMinions, 1, "MURLOC");
    }
}
}
}
