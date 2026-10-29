-- Horde Mage unlock: Marie Cu Rie. Teacher 919510 (display 4123) is placed by migration 0042.
-- Reuses the Hearthstone teacher baseline. This file ships no creature spawns.
-- 919511-919513 are unspawned native quest-credit templates, not scene actors.

DELETE FROM `creature_template` WHERE `entry` BETWEEN 919510 AND 919513;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
VALUES
  (919510, 4123, 'Marie Cu Rie', 'Hearthstone researcher', 60, 60, 4500, 4500,
   3500, 68, 3, 1, 1.14286, 18, 5, 100, 130, 250, 2000, 2000, 8,
   4608, 7, 3, 0, 65008, ''),
  (919511, 4123, 'Observe the arcane experiment', '', 1, 1, 1, 1,
   0, 68, 0, 1, 1, 0, 0, 0, 0, 0, 2000, 2000, 1, 33554434, 7, 3, 0, 0, ''),
  (919512, 4123, 'Observe the frost experiment', '', 1, 1, 1, 1,
   0, 68, 0, 1, 1, 0, 0, 0, 0, 0, 2000, 2000, 1, 33554434, 7, 3, 0, 0, ''),
  (919513, 4123, 'Observe the fire experiment', '', 1, 1, 1, 1,
   0, 68, 0, 1, 1, 0, 0, 0, 0, 0, 2000, 2000, 1, 33554434, 7, 3, 0, 0, '');

DELETE FROM `gossip_menu` WHERE `entry` IN (65008, 65009);
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (65008, 9195140), (65009, 9195145);
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195140 AND 9195146;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195140 AND 9195146;
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195140, 'Do magically enhanced cards perform better? An excellent question. Hold these. Carefully.',
            'Do magically enhanced cards perform better? An excellent question. Hold these. Carefully.'),
  (9195141, 'Experiment one: arcane exposure. Stand nearby and observe the sample.$B$BAre you ready?',
            'Experiment one: arcane exposure. Stand nearby and observe the sample.$B$BAre you ready?'),
  (9195142, 'Experiment two: frost exposure. A brief blizzard. Two seconds should be sufficient.$B$BAre you ready?',
            'Experiment two: frost exposure. A brief blizzard. Two seconds should be sufficient.$B$BAre you ready?'),
  (9195143, 'Experiment three: fire exposure. I have accounted for every possible outcome.$B$BAre you ready?',
            'Experiment three: fire exposure. I have accounted for every possible outcome.$B$BAre you ready?'),
  (9195144, 'Observation recorded. We shall proceed when you are ready.',
            'Observation recorded. We shall proceed when you are ready.'),
  (9195145, 'Thanks for the Control sample.', 'Thanks for the Control sample.'),
  (9195146, 'Was.', 'Was.');
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195140, 9195140, 1), (9195141, 9195141, 1), (9195142, 9195142, 1),
  (9195143, 9195143, 1), (9195144, 9195144, 1), (9195145, 9195145, 1), (9195146, 9195146, 1);

-- Copy the icon from Broken Samophlange (11142). This is an ordinary native vendor purchase.
DELETE FROM `item_template` WHERE `entry` = 919631;
INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`,
   `buy_count`, `buy_price`, `sell_price`, `max_count`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
SELECT 919631, 12, 0, 'Magical Card Combobulator',
  'A precision instrument for combobulating cards. The warranty excludes family disputes.',
  `display_id`, 1, 1, 300, 0, 1, 1, 4, 1, 0, '' FROM `item_template` WHERE `entry` = 11142;

DELETE FROM `quest_template` WHERE `entry` IN (919741, 919742, 919743);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919741, 919742, 919743);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919741, 919742, 919743);
INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`,
   `ReqCreatureOrGOId1`, `ReqCreatureOrGOCount1`, `ReqCreatureOrGOId2`, `ReqCreatureOrGOCount2`,
   `ReqCreatureOrGOId3`, `ReqCreatureOrGOCount3`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `ObjectiveText2`, `ObjectiveText3`,
   `RequestItemsText`, `OfferRewardText`)
VALUES
  (919741, 2, -9100, 10, 0, 434, 0, 0, 919704, 919742,
   919511, 1, 919512, 1, 919513, 1, 0, 0, 500, 0, 0, 0, 0,
   'Applied Card Science',
   'Magical enhancement improves weapons. Why should cards be any different?$B$BIn life I studied magic at a desk. Death was a minor interruption. The loss of funding was worse. My work has not killed me yet. Given my condition, I consider that a research failure. The Forsaken have time, and I intend to use it.$B$BI require an assistant for three controlled experiments: arcane, frost, and fire. We shall expose a card to each form of energy and record the results.$B$BYour participation constitutes informed consent. Just tell me when you are ready.',
   'Observe Marie Cu Rie''s arcane, frost, and fire experiments.',
   'Observe the arcane experiment', 'Observe the frost experiment', 'Observe the fire experiment',
   'Three experiments. We must be thorough. Speak to me when you are ready for the next one.',
   'Arcane: inconclusive. Frost: damp. Fire: reduced sample mass.$B$BYes, that was your card.$B$BScience requires sacrifice. Preferably someone else''s. But even I am not immune to the occasional lab accident.'),
  (919742, 2, -9100, 10, 0, 434, 0, 0, 919741, 919743,
   0, 0, 0, 0, 0, 0, 919631, 1, 500, 0, 0, 0, 0,
   'A Combobulated Family Affair',
   'Before the field trial, I need a Magical Card Combobulator. Elizabeth Van Talen, the engineering supplier in the Undercity, sells them.$B$BI would fetch it myself, but she hates me. An old family feud. Our grandmother left one workshop and two heirs. The workshop is now a crater, and Elizabeth maintains the explosion was my experiment.$B$BIt was a test of her wiring. I was merely present.$B$BBuy one from her and bring it here. Do not mention my name. If she asks, you are combobulating independently.',
   'Buy a Magical Card Combobulator from Elizabeth Van Talen in the Undercity and return to Marie Cu Rie.',
   '', '', '',
   'One Magical Card Combobulator, quickly. No discussion of family history!',
   'Excellent. Still in its original packaging. No evidence of sabotage.$B$BWe may proceed to the field trial. If anyone asks, this equipment came from an anonymous benefactor, and you combobulated it yourself.'),
  (919743, 2, -9100, 10, 0, 434, 0, 2, 919742, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 500, 0, 0, 919626, 1,
   'Field Trial 47-B',
   'The laboratory work is complete. The combobulator is calibrated, and the sample is, regrettably, reduced. Now we test actual card performance.$B$BThis Hearthstone duel will be formally recorded as Field Trial 47-B. Challenge me and win.$B$BDo not concern yourself with the previous forty-six trials. Their assistants are no longer available for comment.',
   'Defeat Marie Cu Rie at Hearthstone.',
   'Win Field Trial 47-B', '', '',
   'The trial remains incomplete until you win. A loss is simply more data.',
   'Subject displayed unexpected competence.$B$BThere. Recorded without prejudice.$B$BTake this Mage Deck. And try to keep it away from open flames.');

INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
  (919510, 919741), (919510, 919742), (919510, 919743);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
  (919510, 919741), (919510, 919742), (919510, 919743);

-- Reuse any identical condition already in the database. Unique-condition keys make this idempotent.
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
VALUES (9, 919742, 1, 0, 0, 0);
DELETE FROM `npc_vendor` WHERE `entry` = 4587 AND `item` = 919631;
INSERT INTO `npc_vendor` (`entry`, `item`, `maxcount`, `incrtime`, `condition_id`)
SELECT 4587, 919631, 0, 0, `condition_entry` FROM `conditions`
WHERE `type` = 9 AND `value1` = 919742 AND `value2` = 1 AND `value3` = 0 AND `value4` = 0 AND `flags` = 0;

-- Same policy as Warrior: direct Normal quest duel, then account Horde random-roster access.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` = 919510;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` = 919510;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`, `quest_id`, `deck_key`, `faction`)
VALUES (919510, 919743, 'normal_mage', 'horde');
