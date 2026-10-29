/*
 * Scripted Curse of Naxxramas cards (set NAXX, build 6024 of 2014-07-22), all 30 collectible cards. Wave 1
 * is the 23 cards that need no death tracking; wave 2 (Avenge, Duplicate, Echoing Ooze, Baron Rivendare, Stalagg,
 * Feugen, Kel'Thuzad) needs the per-side death records and the end-of-turn copy. See HsMatchInternal.h for the builder
 * and the primitives.
 *
 * Rules as of build 6024 (the text of the dataset, not the later nerfs):
 *  - Undertaker gains +1/+1 (the 2015 change to +1 Attack does not apply) whenever its owner summons a minion with a
 *    printed Deathrattle. "Summons" includes playing the minion from the hand, Deathrattle summons, Ancestral Spirit's
 *    resummon and Poison Seeds / Reincarnate replacements. A Deathrattle granted by an enchantment does not count.
 *  - Poison Seeds and Reincarnate destroy first, resolve the deaths and every Deathrattle (Op::Settle), and only then
 *    summon the replacements at the right end of each board, as many Treants per side as that side lost minions. A board
 *    that is full by then gets fewer (or none). Reincarnate's minion is a fresh copy for the side that controlled it.
 *  - Death's Bite is the first weapon with a Deathrattle: it triggers when the weapon breaks by durability, is destroyed
 *    or is replaced by another weapon.
 *  - Nerub'ar Weblord taxes the Battlecry minions in BOTH hands (the card's BATTLECRY mechanic) and stacks with Mana
 *    Wraith and other Weblords. Loatheb's tax adds up per Loatheb and lasts for the victim's next turn only; against
 *    Millhouse Manastorm's "cost (0)" the later of the two effects decides (cost 0, or 0 + the tax).
 *  - Voidcaller, Deathlord and Mad Scientist put cards from the hand / deck onto the battlefield / into play without a
 *    Battlecry or a cast: a full board (or the secret limit) means nothing happens, and Mad Scientist never puts in a
 *    Secret whose card is already active.
 *  - Webspinner draws from the 28 collectible Beasts of build 6024 (dataset entourage of FP1_011): every standard Beast,
 *    the reward-only Captain's Parrot and the Naxxramas Beasts Haunted Creeper, Maexxna and Webspinner itself.
 *  - Maexxna is Poisonous (the definition's mechanic); Spectral Knight is elusive (definition), like Faerie Dragon.
 *
 * Wave 2:
 *  - Avenge (Paladin Secret) fires when a friendly minion dies AND a living friendly minion is left to receive the +3/+2
 *    (random pick, match RNG). The check is made when the secret would fire, so the dead minions are already off the board
 *    (simultaneous deaths: none of them can receive it), and a Deathrattle of an older minion that resolves first (a
 *    Haunted Creeper's Spiders) can provide the receiver. Without a receiver the secret stays hidden and armed.
 *  - Duplicate (Mage Secret) puts two fresh copies of the first friendly minion that died (play order of the dead) into
 *    the hand: the base card, no enchantments; tokens too. A full hand loses the excess copies (no Burn event: like every
 *    generated card in this engine).
 *  - Echoing Ooze: the Battlecry marks the Ooze; at the end of the turn (its own, the turn it was played) it summons an
 *    exact copy right of itself: stats, damage, keywords, freeze, enchantments and granted abilities as they are then, but
 *    not the mark (no second copy) and no Battlecry. No copy when the Ooze was silenced, killed, bounced or transformed
 *    first (the mark is gone), or when the board is full. Power Overwhelming's "dies at the end of the turn" is copied, but
 *    the copy is spared that end of turn and dies at the end of the OPPONENT's turn (hearthstone.wiki.gg).
 *  - Baron Rivendare: every Deathrattle list of every minion of his side runs twice in a row, the granted ones (Ancestral
 *    Spirit, Soul of the Forest) too, each list on its own (own, own, granted, granted). Weapon Deathrattles (Death's Bite)
 *    are not doubled. Several Barons still make two. The Baron must still be on the board after the death step: a Baron
 *    that dies together with the others does not double their Deathrattles (hearthstone.wiki.gg; the same wiki says the
 *    doubling is decided for the whole death step, which is how the engine's simultaneous death step works).
 *  - Stalagg / Feugen: the death of each is recorded per side when it leaves the board (silenced or not), before any
 *    Deathrattle of that death step resolves. Each Deathrattle then checks the record for the other one: if both die
 *    together both Deathrattles see the other as dead and summon a Thaddius each (hearthstone.wiki.gg: "If both die
 *    simultaneously, Thaddius will be summoned by both Deathrattles"). Deaths count on both sides ("also died this game"),
 *    so a Stalagg of one player and a Feugen of the other that die together summon a Thaddius for each of them.
 *  - Kel'Thuzad: at the end of EVERY turn (either player's) he summons, at the right end of his controller's board, a
 *    fresh copy of every minion of that side that died during the turn, in order of death, while there is room. Each
 *    Kel'Thuzad does it (two make two copies each, as far as the board allows). Minions removed without dying (bounce,
 *    Jaraxxus) do not count; neither do deaths after Kel'Thuzad's trigger in the same end-of-turn step (Power
 *    Overwhelming). Kel'Thuzad must be alive when his trigger resolves.
 *
 * Encounter cards:
 *  - Hero powers are ordinary scripts (cost from the card). Raise Dead is a PASSIVE hero power: it is never legal as an
 *    action and shows as spent; its trigger works from the start of the match for as long as it is the hero power.
 *  - Rain of Fire fires one missile (1 damage, random enemy character, each re-rolled) per card in the opponent's hand,
 *    counted when it is used. Eruption hits the left-most living enemy minion (Stealth does not hide it: it is not a
 *    targeted effect). Web Wrap returns a random enemy minion; with a full hand the minion is destroyed (Bounce).
 *  - Necroknight destroys the minions that stood next to it at the moment it died (not the ones that were next to it
 *    only after other deaths of the same step).
 *  - Plague destroys every minion on both boards except Skeletons (NAX4_03). Mindpocalypse: both players draw 2 cards, then
 *    both gain a full Mana Crystal (nothing at 10).
 *  - Locust Swarm, Necrotic Aura and Rain of Fire are not affected by Spell Damage (no "$" in the build 6024 text);
 *    Deathbloom and Sporeburst are. Their Spore (0/1) gives all enemy minions +8 Attack when it dies.
 *  - The Spectral minions are summoned for the OTHER side of their Unrelenting minion (right end of its board, nothing with
 *    a full board) and deal 1 damage to their controller's hero at the start of that player's turns.
 *  - The Horsemen make their controller's hero Immune while they are on the board and not silenced. Runeblade has +3
 *    Attack while none of the three Horsemen is on either board. Massive Runeblade doubles the hero's attack against a
 *    hero. Worshipper: +1 Attack for the hero during its controller's turn (stacks).
 *  - Mind Control Crystal takes control of every Understudy (NAX7_02) of the opponent while there is room on the board.
 *
 * Encounter cards of wings 4-5:
 *  - Hateful Strike (Patchwerk, 4) destroys any minion it can target (his own side too); Hook (5/8) puts itself back into
 *    its owner's hand whenever it leaves play (durability gone, destroyed or replaced): a fresh card, not a copy.
 *  - Poison Cloud (Grobbulus, 2) deals 1 damage to all minions; every minion of EITHER side that dies of that damage gives
 *    Grobbulus one Fallout Slime (2/2) at the right end of his board, after the Deathrattles of that death step have
 *    resolved and while there is room. The card text says "If any die, summon a slime"; Icy Veins and GamesBeat (2014) say
 *    one slime per dead minion, which is what is played. Mutating Injection: +4/+4 and Taunt for any minion.
 *  - Decimate (Gluth, 2) sets the Health of ALL minions to 1 (damage cleared; needs an enemy minion). Jaws (1/5) gains +2
 *    Attack whenever a minion with a printed Deathrattle dies, on either side. Enrage: +6 Attack for the hero this turn.
 *  - Polarity Shift (Thaddius, 0), Frost Breath (Sapphiron, 0) and Frost Blast (Kel'Thuzad, 0) are automatic hero powers: the
 *    engine uses them at the start of the boss's own turn, after the draw. Polarity Shift swaps Attack and Health of all
 *    minions (a 0 Attack minion dies; the swap is permanent and undamaged, as Crazed Alchemist). Frost Breath destroys every
 *    enemy minion that is not Frozen and not next to a Frozen Champion; the Champion (2/10) is Frozen from the start, does not
 *    thaw and protects its neighbours through an aura (Silence ends both). Frost Blast: 2 damage to the enemy hero and Freeze.
 *    Supercharge: +2 Health for all friendly minions. Pure Cold: 8 damage (Spell Damage counts) to the enemy hero and Freeze.
 *  - Kel'Thuzad's second phase is data of Frost Blast (PhaseTwo): when his Armor is gone, or when his 11th own turn starts with
 *    the Armor still up, his hero power becomes Chains (8, fresh) and two Guardians of Icecrown (3/3 Taunt) join his board.
 *    Chains takes control of a random enemy minion until the end of the turn (it may attack at once); it needs an enemy
 *    minion and a free slot. See HsMatch.cpp (CheckPhases) for how the opponent's turn ends.
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
        // Stoneskin Gargoyle: restore this minion to full Health (a heal of exactly the damage, so Auchenai Soulpriest turns
        // it into damage like any other healing).
        void StoneskinGargoyleFx(Effects& fx)
        {
            EntityId const self = fx.Source();
            if (!fx.Alive(self))
                return;
            int const missing = fx.MaxHealth(self) - fx.Health(self);
            if (missing > 0)
                fx.Heal(self, missing);
        }

        // Duplicate: two copies of the minion that died (its card, no enchantments) go into the secret owner's hand.
        void DuplicateFx(Effects& fx)
        {
            std::string const card = fx.CardOf(fx.Subject());
            if (card.empty())
                return;
            Step st;
            st.op = Op::AddToHand;
            st.sel = Sel::OwnHero;
            st.card = card;
            st.a = 2;
            fx.Run(st);
        }

        // Rain of Fire (Faerlina): a missile for each card in the opponent's hand, 1 damage to a random enemy character each.
        void RainOfFireFx(Effects& fx)
        {
            int const missiles = fx.HandCount(1 - fx.Side());
            if (missiles <= 0)
                return;
            Step st;
            st.op = Op::Damage;
            st.sel = Sel::RandomEnemyCharacter;
            st.a = 1;
            st.flags = kStepRepeatRandom;
            st.count = missiles;
            fx.Run(st);
        }

        // Plague (Noth): destroy all minions except Skeletons.
        void PlagueFx(Effects& fx)
        {
            for (EntityId id : fx.Select(Sel::AllMinions))
                if (fx.CardOf(id) != "NAX4_03")
                    fx.Destroy(id);
        }

        // Poison Cloud (Grobbulus): 1 damage to all minions; one Fallout Slime for every minion that dies of it.
        void PoisonCloudFx(Effects& fx)
        {
            std::vector<EntityId> const all = fx.Select(Sel::AllMinions);
            if (all.empty())
                return;
            Step dmg;
            dmg.op = Op::Damage;
            dmg.sel = Sel::AllMinions;
            dmg.a = 1;
            fx.Run(dmg);
            int dead = 0;
            for (EntityId id : all)
                if (!fx.Alive(id))
                    ++dead;
            // The deaths and their Deathrattles come first; the slimes fill what is left of the board.
            Step settle;
            settle.op = Op::Settle;
            fx.Run(settle);
            for (int k = 0; k < dead; ++k)
                if (fx.Summon(fx.Side(), "NAX11_03", -1) == 0)
                    break;
        }

        // The Beast pool of Webspinner (collectible Beasts of build 6024, dataset order).
        std::vector<std::string> const& BeastPool()
        {
            static std::vector<std::string> const pool = {"FP1_011", "DS1_175", "EX1_531", "CS2_237", "DS1_178", "EX1_534",
                "EX1_543", "EX1_009", "NEW1_017", "CS2_171", "CS2_169", "CS2_172", "NEW1_016", "EX1_162", "FP1_002", "CS2_203",
                "CS2_120", "EX1_170", "CS2_125", "EX1_017", "EX1_014", "CS2_127", "CS2_119", "NEW1_041", "EX1_028", "EX1_577",
                "CS2_201", "FP1_010"};
            return pool;
        }
    }

    void RegisterNaxxScripts(ScriptTable& t)
    {
        // ================= Neutral minions =================
        // Zombie Chow 2/3: Deathrattle: restore 5 Health to the enemy hero.
        t.Add("FP1_001").Deathrattle().Do(Op::Heal, Sel::EnemyHero, 5);
        // Undertaker 1/2: whenever you summon a minion with Deathrattle, gain +1/+1 (6024).
        t.Add("FP1_028").On(On::Summoned, Who::Friendly, kTrigDeathrattle).Do(Op::Buff, Sel::Self, 1, 1);
        // Haunted Creeper 1/2: Deathrattle: summon two 1/1 Spectral Spiders.
        t.Add("FP1_002").Deathrattle().DoCard(Op::Summon, Sel::None, "FP1_002t", 2);
        t.Add("FP1_002t"); // Spectral Spider 1/1
        // Nerubian Egg 0/2: Deathrattle: summon a 4/4 Nerubian.
        t.Add("FP1_007").Deathrattle().DoCard(Op::Summon, Sel::None, "FP1_007t");
        t.Add("FP1_007t"); // Nerubian 4/4
        // Unstable Ghoul 1/3, Taunt: Deathrattle: deal 1 damage to all minions.
        t.Add("FP1_024").Deathrattle().Do(Op::Damage, Sel::AllMinions, 1);
        // Shade of Naxxramas 2/2, Stealth: at the start of your turn, gain +1/+1.
        t.Add("FP1_005").On(On::TurnStart, Who::Friendly).Do(Op::Buff, Sel::Self, 1, 1);
        // Stoneskin Gargoyle 1/4: at the start of your turn, restore this minion to full Health.
        t.Add("FP1_027").On(On::TurnStart, Who::Friendly).Run(StoneskinGargoyleFx);
        // Dancing Swords 4/4: Deathrattle: your opponent draws a card.
        t.Add("FP1_029").Deathrattle().Do(Op::Draw, Sel::EnemyHero, 1);
        // Wailing Soul 3/5: Battlecry: silence your other minions.
        t.Add("FP1_016").Do(Op::Silence, Sel::OtherFriendlyMinions);
        // Spectral Knight 4/6: can't be targeted by spells or Hero Powers (the elusive flag of the definition).
        t.Add("FP1_008");
        // Sludge Belcher 3/5, Taunt: Deathrattle: summon a 1/2 Slime with Taunt.
        t.Add("FP1_012").Deathrattle().DoCard(Op::Summon, Sel::None, "FP1_012t");
        t.Add("FP1_012t"); // Slime 1/2, Taunt
        // Nerub'ar Weblord 1/4: minions with Battlecry cost (2) more (both players' hands).
        t.Add("FP1_017").Aura(AuraKind::Cost, AuraScope::AllMinionCards, 2).BattlecryOnly();
        // Deathlord 2/8, Taunt: Deathrattle: your opponent puts a minion from their deck into the battlefield.
        t.Add("FP1_009").Deathrattle().Do(Op::SummonFromDeck, Sel::None, 0, 1);
        // Mad Scientist 2/2: Deathrattle: put a Secret from your deck into the battlefield.
        t.Add("FP1_004").Deathrattle().Do(Op::SecretFromDeck, Sel::None);
        // Maexxna 2/8: destroy any minion damaged by this minion (Poisonous, from the definition).
        t.Add("FP1_010");
        // Loatheb 5/5: Battlecry: enemy spells cost (5) more next turn.
        t.Add("FP1_030").Do(Op::SpellTaxEnemy, Sel::None, 5);

        // ================= Wave 2 =================
        // Echoing Ooze 1/2: Battlecry: summon an exact copy of this minion at the end of the turn.
        t.Add("FP1_003")
            .Do(Op::EchoMark, Sel::Self)
            .On(On::TurnEnd, Who::Any)
            .When(Cond::EchoPending)
            .Do(Op::EchoCopy, Sel::Self);
        // Baron Rivendare 1/7: your minions trigger their Deathrattles twice.
        t.Add("FP1_031").DoublesDeathrattles();
        // Stalagg 7/4: Deathrattle: if Feugen also died this game, summon Thaddius.
        t.Add("FP1_014").Deathrattle().If(Cond::FeugenDied).DoCard(Op::Summon, Sel::None, "FP1_014t");
        // Feugen 4/7: Deathrattle: if Stalagg also died this game, summon Thaddius.
        t.Add("FP1_015").Deathrattle().If(Cond::StalaggDied).DoCard(Op::Summon, Sel::None, "FP1_014t");
        t.Add("FP1_014t"); // Thaddius 11/11
        // Kel'Thuzad 6/8: at the end of each turn, summon all friendly minions that died this turn.
        t.Add("FP1_013").On(On::TurnEnd, Who::Any).Do(Op::SummonDied, Sel::None);

        // ================= Encounter cards: the bosses' minions, spells and hero powers =================
        // Deathcharger 1 mana 2/3, Charge. Deathrattle: deal 3 damage to your hero.
        t.Add("FP1_006").Deathrattle().Do(Op::Damage, Sel::OwnHero, 3);
        // Necroknight 4 mana 5/6. Deathrattle: destroy the minions next to this one as well.
        t.Add("NAXM_001").Deathrattle().Do(Op::Destroy, Sel::AdjacentAtDeath);
        // Skeletal Smith 3 mana 4/3. Deathrattle: destroy your opponent's weapon.
        t.Add("NAXM_002").Deathrattle().Do(Op::DestroyWeapon, Sel::EnemyHero);

        // Anub'Rekhan: Skitter (2): summon a 3/1 Nerubian. Locust Swarm (7): 3 damage to all enemy minions, restore 3 Health
        // to your hero.
        t.Add("NAX1_04").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "NAX1_03");
        t.Add("NAX1_03"); // Nerubian 3/1
        t.Add("NAX1_05").Do(Op::Damage, Sel::EnemyMinions, 3).Do(Op::Heal, Sel::OwnHero, 3);

        // Faerlina: Rain of Fire (2). Worshipper 3 mana 1/4: your hero has +1 Attack on your turn.
        t.Add("NAX2_03").Run(RainOfFireFx);
        t.Add("NAX2_05").Aura(AuraKind::HeroAttackOwnTurn, AuraScope::OwnSide, 1);

        // Maexxna: Web Wrap (3): return a random enemy minion to your opponent's hand. Necrotic Poison (2): destroy a minion.
        t.Add("NAX3_02").Req(kReqMinEnemyMinions, 1).Do(Op::Bounce, Sel::RandomEnemyMinion);
        t.Add("NAX3_03").Target(TargetKind::AnyMinion).Do(Op::Destroy, Sel::Target);

        // Noth: Raise Dead (0, passive): whenever an enemy minion dies, raise a 1/1 Skeleton. Plague (6).
        t.Add("NAX4_04").Passive().On(On::Died, Who::Enemy, kTrigMinion).DoCard(Op::Summon, Sel::None, "NAX4_03");
        t.Add("NAX4_03"); // Skeleton 1/1
        t.Add("NAX4_05").Run(PlagueFx);

        // Heigan: Eruption (1): 2 damage to the left-most enemy minion. Mindpocalypse (2): both players draw 2 cards and
        // gain a Mana Crystal.
        t.Add("NAX5_02").Req(kReqMinEnemyMinions, 1).Do(Op::Damage, Sel::LeftmostEnemyMinion, 2);
        t.Add("NAX5_03")
            .Do(Op::Draw, Sel::OwnHero, 2)
            .Do(Op::Draw, Sel::EnemyHero, 2)
            .Do(Op::GainCrystals, Sel::None, 1, 1)
            .Do(Op::GainCrystals, Sel::EnemyHero, 1, 1);

        // Loatheb: Necrotic Aura (2): 3 damage to the enemy hero. Deathbloom (4): 5 damage to a minion, summon a Spore.
        // Sporeburst (1): 1 damage to all enemy minions, summon a Spore. Spore 0/1, Deathrattle: give all enemy minions +8 Attack.
        t.Add("NAX6_02").Do(Op::Damage, Sel::EnemyHero, 3);
        t.Add("NAX6_03")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower)
            .DoCard(Op::Summon, Sel::None, "NAX6_03t");
        t.Add("NAX6_04")
            .Do(Op::Damage, Sel::EnemyMinions, 1, 0, kStepSpellPower)
            .DoCard(Op::Summon, Sel::None, "NAX6_03t");
        t.Add("NAX6_03t").Deathrattle().Do(Op::Buff, Sel::EnemyMinions, 8, 0);

        // Razuvious: Unbalancing Strike (2): deal 3 damage. Massive Runeblade (3) 5/2: double damage to heroes. Understudy 0/7
        // Taunt. Mind Control Crystal (1, the player's card): take control of the Understudies.
        t.Add("NAX7_03").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 3);
        t.Add("NAX7_04").Aura(AuraKind::DoubleHeroDamage, AuraScope::OwnSide, 0);
        t.Add("NAX7_02");
        t.Add("NAX7_05").DoCard(Op::TakeControlCard, Sel::None, "NAX7_02");

        // Gothik: Harvest (2): draw a card. The Unrelenting minions summon their Spectral twin for the opponent; a Spectral
        // minion deals 1 damage to its controller's hero at the start of that player's turn.
        t.Add("NAX8_02").Do(Op::Draw, Sel::None, 1);
        t.Add("NAX8_03").Deathrattle().DoCard(Op::Summon, Sel::None, "NAX8_03t", 1, 1);
        t.Add("NAX8_04").Deathrattle().DoCard(Op::Summon, Sel::None, "NAX8_04t", 1, 1);
        t.Add("NAX8_05").Deathrattle().DoCard(Op::Summon, Sel::None, "NAX8_05t", 1, 1);
        t.Add("NAX8_03t").On(On::TurnStart, Who::Friendly).Do(Op::Damage, Sel::OwnHero, 1);
        t.Add("NAX8_04t").On(On::TurnStart, Who::Friendly).Do(Op::Damage, Sel::OwnHero, 1);
        t.Add("NAX8_05t").On(On::TurnStart, Who::Friendly).Do(Op::Damage, Sel::OwnHero, 1);

        // The Four Horsemen: "Your hero is Immune." (Blaumeux, Korth'azz, Zeliek 1/7). Runeblade (3) 1/3: +3 Attack if the other
        // Horsemen are dead. Unholy Shadow (5): draw 2 cards. Mark of the Horsemen (5): +1/+1 for your minions and weapon.
        t.Add("NAX9_02").Aura(AuraKind::HeroImmune, AuraScope::OwnSide, 1);
        t.Add("NAX9_03").Aura(AuraKind::HeroImmune, AuraScope::OwnSide, 1);
        t.Add("NAX9_04").Aura(AuraKind::HeroImmune, AuraScope::OwnSide, 1);
        t.Add("NAX9_05").Aura(AuraKind::WeaponAttackNoHorsemen, AuraScope::OwnSide, 3);
        t.Add("NAX9_06").Do(Op::Draw, Sel::None, 2);
        t.Add("NAX9_07").Do(Op::Buff, Sel::FriendlyMinions, 1, 1).Do(Op::WeaponBuff, Sel::OwnHero, 1, 1);

        // ================= Encounter cards of wings 4-5 =================
        // Patchwerk: Hateful Strike (4): destroy a minion. Hook (3) 5/8: Deathrattle: put this weapon into your hand.
        t.Add("NAX10_03").Target(TargetKind::AnyMinion).Do(Op::Destroy, Sel::Target);
        t.Add("NAX10_02").Deathrattle().DoCard(Op::AddToHand, Sel::OwnHero, "NAX10_02");

        // Grobbulus: Poison Cloud (2): 1 damage to all minions, a slime for each that dies. Mutating Injection (3): +4/+4 and
        // Taunt. Fallout Slime 2/2.
        t.Add("NAX11_02").Run(PoisonCloudFx, {"NAX11_03"});
        t.Add("NAX11_03");
        t.Add("NAX11_04").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 4, 4).Do(Op::Keyword, Sel::Target, kKwTaunt);

        // Gluth: Decimate (2): all minions' Health becomes 1. Jaws (1) 1/5: +2 Attack when a minion with Deathrattle dies.
        // Enrage (3): your hero has +6 Attack this turn.
        t.Add("NAX12_02").Req(kReqMinEnemyMinions, 1).Do(Op::SetHealth, Sel::AllMinions, 1);
        t.Add("NAX12_03").On(On::Died, Who::Any, kTrigMinion | kTrigDeathrattle).Do(Op::WeaponBuff, Sel::OwnHero, 2, 0);
        t.Add("NAX12_04").Do(Op::HeroAttack, Sel::None, 6);

        // Thaddius: Polarity Shift (0, automatic): swap Attack and Health of all minions. Supercharge (2): +2 Health for your
        // minions.
        t.Add("NAX13_02").AutoCast().Do(Op::SwapAttackHealth, Sel::AllMinions);
        t.Add("NAX13_03").Do(Op::Buff, Sel::FriendlyMinions, 0, 2);

        // Sapphiron: Frost Breath (0, automatic): destroy all enemy minions that aren't Frozen. Frozen Champion (the player's)
        // 2/10: permanently Frozen, adjacent minions are Immune to Frost Breath. Pure Cold (5): 8 damage to the enemy hero and
        // Freeze it.
        t.Add("NAX14_02").AutoCast().Do(Op::DestroyUnfrozen, Sel::EnemyMinions);
        t.Add("NAX14_03").PermanentlyFrozen().Aura(AuraKind::Keyword, AuraScope::Adjacent, kKwFrostProof);
        t.Add("NAX14_04").Do(Op::Damage, Sel::EnemyHero, 8, 0, kStepSpellPower).Do(Op::Freeze, Sel::EnemyHero);

        // Kel'Thuzad: Frost Blast (0, automatic): 2 damage to the enemy hero and Freeze it; when his Armor is gone, or his 11th
        // turn starts, he turns to phase 2 (Chains, two Guardians of Icecrown). Chains (8): take control of a random enemy minion
        // until the end of the turn. Guardian of Icecrown 3/3 Taunt.
        t.Add("NAX15_02")
            .AutoCast()
            .PhaseTwo("NAX15_04", "NAX15_03n", 2, 11)
            .Do(Op::Damage, Sel::EnemyHero, 2)
            .Do(Op::Freeze, Sel::EnemyHero);
        t.Add("NAX15_04").Req(kReqMinEnemyMinions, 1).Req(kReqFreeSlot).Do(Op::TakeControlTemp, Sel::RandomEnemyMinion);
        t.Add("NAX15_03n");

        // ================= Class cards =================
        // Dark Cultist (Priest) 3/4: Deathrattle: give a random friendly minion +3 Health.
        t.Add("FP1_023").Deathrattle().Do(Op::Buff, Sel::RandomFriendlyMinion, 0, 3);
        // Anub'ar Ambusher (Rogue) 5/5: Deathrattle: return a random friendly minion to your hand.
        t.Add("FP1_026").Deathrattle().Do(Op::Bounce, Sel::RandomFriendlyMinion);
        // Webspinner (Hunter) 1/1: Deathrattle: add a random Beast card to your hand.
        t.Add("FP1_011").Deathrattle().DoList(Op::AddRandomToHand, Sel::None, BeastPool(), 1);
        // Voidcaller (Warlock) 3/4: Deathrattle: put a random Demon from your hand into the battlefield.
        t.Add("FP1_022").Deathrattle().Do(Op::SummonFromHand, Sel::None).Race("DEMON");

        // ================= Spells =================
        // Poison Seeds (Druid, 4): destroy all minions and summon 2/2 Treants to replace them.
        t.Add("FP1_019").DoCard(Op::DestroyReplace, Sel::AllMinions, "FP1_019t");
        t.Add("FP1_019t"); // Treant 2/2
        // Reincarnate (Shaman, 2): destroy a minion, then return it to life with full Health.
        t.Add("FP1_025").Target(TargetKind::AnyMinion).Do(Op::DestroyReplace, Sel::Target);

        // Avenge (Paladin Secret, 1): when one of your minions dies, give a random friendly minion +3/+2 (needs a receiver).
        t.Add("FP1_020")
            .On(On::Died, Who::Friendly, kTrigMinion)
            .When(Cond::HaveMinion)
            .Do(Op::Buff, Sel::RandomFriendlyMinion, 3, 2);
        // Duplicate (Mage Secret, 3): when a friendly minion dies, put 2 copies of it into your hand.
        t.Add("FP1_018").On(On::Died, Who::Friendly, kTrigMinion).Run(DuplicateFx);

        // ================= Weapon =================
        // Death's Bite (Warrior, 4) 4/2: Deathrattle: deal 1 damage to all minions.
        t.Add("FP1_021").Deathrattle().Do(Op::Damage, Sel::AllMinions, 1);
    }
}
}
}
