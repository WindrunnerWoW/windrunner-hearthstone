-- Alliance Druid: The Long Game. Teacher Thijs Nightbloom (919517) is placed by migration 0043.
-- No creature or gameobject spawns here. Four objective-only templates must never be placed.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 919517 AND 919521;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
SELECT 919517, 2266, 'Thijs Nightbloom', 'Hearthstone enthusiast',
  `level_min`, `level_max`, `health_min`, `health_max`, `armor`, 79, 3, `speed_walk`, `speed_run`,
  `detection_range`, `call_for_help_range`, `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`,
  `ranged_attack_time`, `unit_class`, `unit_flags`, `type`, `regeneration`, 0, 65011, ''
FROM `creature_template` WHERE `entry` = 919502;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `level_min`, `level_max`, `health_min`, `health_max`,
   `faction`, `npc_flags`, `unit_flags`, `type`, `regeneration`, `movement_type`, `script_name`)
VALUES
  (919518, 2266, 'Observe a nightsaber for ten seconds', 1, 1, 1, 1, 79, 0, 33554434, 7, 3, 0, ''),
  (919519, 2266, 'Tell Thijs what you learned', 1, 1, 1, 1, 79, 0, 33554434, 7, 3, 0, ''),
  (919520, 2266, 'Enjoy the view from the tower', 1, 1, 1, 1, 79, 0, 33554434, 7, 3, 0, ''),
  (919521, 2266, 'Tell Thijs about the view', 1, 1, 1, 1, 79, 0, 33554434, 7, 3, 0, '');

DELETE FROM `gossip_menu` WHERE `entry` = 65011;
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (65011, 9195170);
DELETE FROM `npc_text` WHERE `ID` IN (9195170,9195172,9195173,9195174,9195175);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195170,9195172,9195173,9195174,9195175);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195170, 'Everyone wants to play the strongest card. A druid asks a different question: is this the right moment?$B$BThere is time. Let us begin slowly.', 'Everyone wants to play the strongest card. A druid asks a different question: is this the right moment?$B$BThere is time. Let us begin slowly.'),
  (9195172, 'What did you learn?', 'What did you learn?'),
  (9195173, 'A fair answer.$B$BSometimes waiting is the move. The nightsaber does not hurry simply because we are watching.$B$BNow, let us give you a little room to grow.', 'A fair answer.$B$BSometimes waiting is the move. The nightsaber does not hurry simply because we are watching.$B$BNow, let us give you a little room to grow.'),
  (9195174, 'What did the wind and the view teach you?', 'What did the wind and the view teach you?'),
  (9195175, 'Exactly. The trees did not grow in a single turn.$B$BTake your time, build your strength, and know when to act.', 'Exactly. The trees did not grow in a single turn.$B$BTake your time, build your strength, and know when to act.');
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195170, 9195170, 1),
  (9195172, 9195172, 1),
  (9195173, 9195173, 1),
  (9195174, 9195174, 1),
  (9195175, 9195175, 1);

DELETE FROM `quest_template` WHERE `entry` IN (919761,919762,919763);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919761,919762,919763);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919761,919762,919763);
INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `ReqCreatureOrGOId1`, `ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`, `ReqCreatureOrGOCount2`, `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`,
   `RewItemId1`, `RewItemCount1`, `Title`, `Details`, `Objectives`, `ObjectiveText1`, `ObjectiveText2`,
   `RequestItemsText`, `OfferRewardText`)
VALUES
  (919761, 2, -9100, 10, 0, 589, 0, 0, 919714, 919762, 919518, 1, 919519, 1, 500, 0, 0, 0, 0,
   'A Lesson in Patience',
   'A druid does not win by playing the strongest card at every opportunity. We learn when to act.$B$BWhen I was young, I tried to hurry a sapling along with a spell. It grew crooked and never recovered. It is still standing, somewhere on Teldrassil, and I have been patient ever since.$B$BFor your first lesson, observe a nightsaber. You need not chase it or disturb it. Just take your time.',
   'Wait ten seconds to observe a nightsaber, then tell Thijs Nightbloom what you learned.',
   'Observe a nightsaber for ten seconds',
   'Tell Thijs what you learned',
   'Take your time. After watching, tell me what you learned.',
   'A fair answer. Sometimes waiting is the move.$B$BThe nightsaber does not hurry simply because we are watching. Now, let us give you a little room to grow.'),
  (919762, 2, -9100, 10, 0, 589, 0, 0, 919761, 919763, 919520, 1, 919521, 1, 500, 0, 0, 0, 0,
   'Natural Growth',
   'Climb to the top of the tower in the Cenarion Enclave, and step outside. From there you can see Darnassus spread through the boughs, and feel the same wind that bent my sapling.$B$BYou need not collect anything. Enjoy the view, then return and tell me what it taught you.$B$BEven the tallest tree began with a little patience.',
   'Visit the overlook at the top of the Cenarion Enclave tower, then speak with Thijs Nightbloom.',
   'Enjoy the view from the tower',
   'Tell Thijs about the view',
   'Have you felt the wind at the top of the tower? Return when you have, and tell me about it.',
   'Take your time, build your strength, and know when to act.$B$BThe tower will be there whenever you want to look again.'),
  (919763, 2, -9100, 10, 0, 589, 0, 2, 919762, 0, 0, 0, 0, 0, 500, 0, 0, 919628, 1,
   'The Long Game',
   'You have watched, waited, and taken the longer view. Now it is time to act.$B$BThe sapling bent because I forced it. A card is the same. Play it too early and it never grows into what it could have been.$B$BChallenge me to Hearthstone and win. Do not rush to play the strongest card. Ask yourself whether this is its moment.',
   'Defeat Thijs Nightbloom in a Hearthstone duel.',
   'Defeat Thijs Nightbloom at Hearthstone',
   '',
   'There is time for another game. Challenge me when you are ready.',
   'You knew when to wait, and when to act. Well played.$B$BThe sapling is still crooked, but I like it better that way. Take the deck.');
INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
  (919517,919761), (919517,919762), (919517,919763);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
  (919517,919761), (919517,919762), (919517,919763);

-- Generated shared policy: direct Normal only during the final quest; account Alliance roster at turn-in.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` = 919517;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` = 919517;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`, `quest_id`, `deck_key`, `faction`)
VALUES (919517, 919763, 'normal_druid', 'alliance');
