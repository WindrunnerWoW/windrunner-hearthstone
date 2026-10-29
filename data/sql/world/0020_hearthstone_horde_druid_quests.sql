-- Horde Druid: The Balance of Things. Kolento Treewatcher's owner-placed spawn is recorded below.
-- Four shared runtime quest pickups use the owner's positions in HsHordeDruidRules.h.
-- Nan Mistrunner (3017) keeps her native vendor, quests and script. No fruit item is sold or recovered.
-- Enable gossip before the client chooses how to open this vendor. Adding it inside GOSSIP_HELLO is too late.
UPDATE `creature_template` SET `npc_flags` = `npc_flags` | 1 WHERE `entry` = 3017;

DELETE FROM `creature_template` WHERE `entry` IN (919514, 919515, 919516);
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
SELECT 919514, 11773, 'Kolento Treewatcher', 'Hearthstone enthusiast',
  `level_min`, `level_max`, `health_min`, `health_max`, `armor`, 104, 3, `speed_walk`, `speed_run`,
  `detection_range`, `call_for_help_range`, `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`,
  `ranged_attack_time`, `unit_class`, `unit_flags`, `type`, `regeneration`, 0, 65010, ''
FROM `creature_template` WHERE `entry` = 919501;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `level_min`, `level_max`, `health_min`, `health_max`,
   `faction`, `npc_flags`, `unit_flags`, `type`, `regeneration`, `movement_type`, `script_name`)
VALUES
  (919515, 11773, 'Ask Nan about the kodo''s fruit', 1, 1, 1, 1, 104, 0, 33554434, 7, 3, 0, ''),
  (919516, 11773, 'Report the missing fruit to Kolento', 1, 1, 1, 1, 104, 0, 33554434, 7, 3, 0, '');

DELETE FROM `gossip_menu` WHERE `entry` = 65010;
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (65010, 9195160);
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195160 AND 9195165;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195160 AND 9195165;
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195160, 'Everyone asks me which cards are strongest. Nobody asks whether the kodo has eaten.$B$BPerhaps we should begin there.',
            'Everyone asks me which cards are strongest. Nobody asks whether the kodo has eaten.$B$BPerhaps we should begin there.'),
  (9195161, 'The fruit I save every morning for the old kodo? I am afraid it is gone. A giant buzzard took the whole basket.',
            'The fruit I save every morning for the old kodo? I am afraid it is gone. A giant buzzard took the whole basket.'),
  (9195162, 'Yes. It swooped down, took the basket, and flew off before I could stop it.$B$BI always keep the best for that kodo, and today the buzzard was quicker.',
            'Yes. It swooped down, took the basket, and flew off before I could stop it.$B$BI always keep the best for that kodo, and today the buzzard was quicker.'),
  (9195163, 'Tell him I am sorry about his kodo. I will set more aside tomorrow, and I expect the buzzard is very pleased with itself.',
            'Tell him I am sorry about his kodo. I will set more aside tomorrow, and I expect the buzzard is very pleased with itself.'),
  (9195164, 'That''s the way of life.$B$BWe went to feed a kodo, and a buzzard found its meal first. The kodo will have to wait a little longer.',
            'That''s the way of life.$B$BWe went to feed a kodo, and a buzzard found its meal first. The kodo will have to wait a little longer.'),
  (9195165, 'The wind has had its turn. Now we shall have ours.$B$BThank you for bringing all four cards back. Let us find out how they play.',
            'The wind has had its turn. Now we shall have ours.$B$BThank you for bringing all four cards back. Let us find out how they play.');
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195160, 9195160, 1), (9195161, 9195161, 1), (9195162, 9195162, 1),
  (9195163, 9195163, 1), (9195164, 9195164, 1), (9195165, 9195165, 1);

-- Separate unique quest items prevent farming one pickup for all four cards. Native turn-in consumes all four.
DELETE FROM `item_template` WHERE `entry` BETWEEN 919632 AND 919635;
INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`,
   `buy_price`, `sell_price`, `max_count`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
VALUES
  (919632, 12, 0, 'Windblown Hearthstone Card I', 'One of Kolento''s missing cards.', 69179, 1, 0, 0, 1, 1, 4, 1, 0, ''),
  (919633, 12, 0, 'Windblown Hearthstone Card II', 'One of Kolento''s missing cards.', 69179, 1, 0, 0, 1, 1, 4, 1, 0, ''),
  (919634, 12, 0, 'Windblown Hearthstone Card III', 'One of Kolento''s missing cards.', 69179, 1, 0, 0, 1, 1, 4, 1, 0, ''),
  (919635, 12, 0, 'Windblown Hearthstone Card IV', 'One of Kolento''s missing cards.', 69179, 1, 0, 0, 1, 1, 4, 1, 0, '');
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 919051 AND 919054;
-- Small floor scroll/map model (220), pending in-game visual QA. The Warrior-style handler grants native items.
-- Type 3 enables LootSparkle. data8 is questId; no native shared loot or consumption.
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `data1`, `data3`, `data8`, `script_name`)
VALUES
  (919051, 3, 220, 'Windblown Hearthstone Card I', 0.35, 0, 0, 919752, ''),
  (919052, 3, 220, 'Windblown Hearthstone Card II', 0.35, 0, 0, 919752, ''),
  (919053, 3, 220, 'Windblown Hearthstone Card III', 0.35, 0, 0, 919752, ''),
  (919054, 3, 220, 'Windblown Hearthstone Card IV', 0.35, 0, 0, 919752, '');

DELETE FROM `quest_template` WHERE `entry` IN (919751, 919752, 919753);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919751, 919752, 919753);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919751, 919752, 919753);
INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `ReqCreatureOrGOId1`, `ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`, `ReqCreatureOrGOCount2`,
   `ReqItemId1`, `ReqItemCount1`, `ReqItemId2`, `ReqItemCount2`,
   `ReqItemId3`, `ReqItemCount3`, `ReqItemId4`, `ReqItemCount4`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `ObjectiveText2`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919751, 2, -9100, 10, 0, 434, 0, 0, 919704, 919752, 919515, 1, 919516, 1,
   0, 0, 0, 0, 0, 0, 0, 0, 500, 0, 0, 0, 0,
   'A Kodo''s Breakfast',
   'Which card is strongest? Which deck wins most often? Those are the only questions anyone brings me. Nobody asks whether the game is in balance.$B$BMy old kodo does not care which deck wins. He cares whether he has eaten, and so should we. Let us start there.$B$BNan Mistrunner, the fruit vendor here in Thunder Bluff, always sets some aside for him. Ask her for it, and return to me when you have spoken with her.',
   'Ask Nan Mistrunner about the fruit she saves for Kolento''s old kodo, then return to Kolento Treewatcher.',
   'Ask Nan about the kodo''s fruit', 'Report the missing fruit to Kolento',
   'Speak with Nan, then tell me what she said.',
   'A buzzard ate well today. The kodo will wait, as he has before. Nothing in the world takes more than it needs for very long.$B$BThank you for asking. There is another small matter you can help me with.'),
  (919752, 2, -9100, 10, 0, 434, 0, 0, 919751, 919753, 0, 0, 0, 0,
   919632, 1, 919633, 1, 919634, 1, 919635, 1, 500, 0, 0, 0, 0,
   'Gone with the Wind',
   'A buzzard in the morning and a gust in the afternoon. The world is taking its turn with me today.$B$BI was preparing to teach you the druid way of Hearthstone. I had my cards laid out on Elder Rise when the wind scattered four of them.$B$BThey are lying about somewhere. I think I can see one below me, so you will need to look beyond the edge of this rise. I also think there is one next to the elevator down there, maybe near the Totem.$B$BI overheard someone say that there is one on the bridge, and one upstairs next to the elevator. Maybe check those places too.$B$BPlease bring all four back. I would rather play a full deck than argue with the wind.',
   'Recover Kolento''s four windblown Hearthstone cards.',
   '', '',
   'Four cards are missing. Check the surroundings.',
   'Thank you for bringing all four back. I would have said the wind had taken them for good, but you have proven me wrong.$B$BLet us find out how they play.'),
  (919753, 2, -9100, 10, 0, 434, 0, 2, 919752, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 500, 0, 0, 919628, 1,
   'The Balance of Things',
   'The cards are back, and Mulgore stretches out below us. A fine place for a game.$B$BThe buzzard took the kodo''s breakfast and the wind took my cards, and neither was wrong to do it. A game works the same way. One side takes a turn, then the other. Nothing stays strong for long.$B$BChallenge me to Hearthstone and win. Perhaps we can spend one game enjoying the cards without asking which one is strongest.',
   'Defeat Kolento Treewatcher at Hearthstone in Thunder Bluff.',
   'Win against Kolento Treewatcher', '',
   'Take your time. We can play again whenever you are ready.',
   'Well played. You have earned this Druid Deck.$B$BAnd when someone asks which card is strongest, perhaps suggest they play a game and see.');
INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
  (919514, 919751), (919514, 919752), (919514, 919753);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
  (919514, 919751), (919514, 919752), (919514, 919753);

DELETE FROM `hearthstone_opponent` WHERE `creature_entry` = 919514;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` = 919514;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`, `quest_id`, `deck_key`, `faction`)
VALUES (919514, 919753, 'normal_druid', 'horde');

-- Owner requested recording the placed teacher on 2026-10-04. Original local guid 12585211.
-- Preserve the Thunder Bluff position, orientation, respawn times and stationary movement.
-- Allocate the guid locally, matching the established teacher spawn migrations. Reruns leave one Kolento.
-- Never spawn conversation-credit templates 919515/919516 or the runtime card pickups.
DELETE FROM `creature` WHERE `id` = 919514;
INSERT INTO `creature`
  (`id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
VALUES
  (919514, 1, -1010.159973145, -244.287994385, 160.039001465, 0.470948994,
   120, 120, 0, 0);
