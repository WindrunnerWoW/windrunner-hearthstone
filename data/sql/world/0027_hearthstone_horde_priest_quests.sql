-- Horde Priest: Jeffrey Shih. Owner-placed NPC 919539 and 18 waypoints captured below.
-- C++ creates pickups 919056/919057 at the eight supplied positions; no GO spawn SQL.
-- Normal shared chest despawn/respawn; no per-character location tracking.
-- Display 2626 is a temporary Father Lankester model; the owner will supply the final display later.
DELETE FROM `creature_template` WHERE `entry` = 919539;
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,`detection_range`,`call_for_help_range`,
   `dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,`ranged_attack_time`,`unit_class`,
   `unit_flags`,`type`,`regeneration`,`movement_type`,`gossip_menu_id`,`script_name`)
SELECT 919539,2626,'Jeffrey Shih','Hearthstone priest',
  `level_min`,`level_max`,`health_min`,`health_max`,`armor`,68,3,`speed_walk`,`speed_run`,
  `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,
  `ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,0,65019,''
FROM `creature_template` WHERE `entry` = 919501;

DELETE FROM `gossip_menu` WHERE `entry` = 65019;
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES (65019,9195250);
DELETE FROM `npc_text` WHERE `ID` = 9195250;
DELETE FROM `broadcast_text` WHERE `entry` = 9195250;
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195250,'Faith is useful. Sentimentality takes up space in your hand.',
           'Faith is useful. Sentimentality takes up space in your hand.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`) VALUES (9195250,9195250,1);

DELETE FROM `item_template` WHERE `entry` IN (919643,919644);
INSERT INTO `item_template`
  (`entry`,`class`,`subclass`,`name`,`description`,`display_id`,`quality`,`buy_count`,
   `buy_price`,`sell_price`,`max_count`,`stackable`,`bonding`,`material`,`start_quest`,`script_name`)
VALUES
  (919643,12,0,'Holy Tablet Fragment','A broken stone fragment bearing a passage from a holy text.',11183,1,1,0,0,4,4,4,1,0,''),
  (919644,12,0,'Old Lordaeron Card','The edges are worn, but the faded crest of Lordaeron remains.',69179,1,1,0,0,4,4,4,1,0,'');
DELETE FROM `gameobject_template` WHERE `entry` IN (919056,919057);
-- Type 3 is required by the installed SuperWoW LootSparkle renderer. Chest data8
-- is questId, data1 is lootId. Scripted delivery suppresses native loot callbacks.
-- Shared HsQuestPickupAI: tablets cast for 2 seconds; cards for 0.5 seconds.
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`size`,`data0`,`data1`,`data3`,`data8`,`script_name`)
VALUES
  (919056,3,6419,'Discarded Holy Tablet Fragments',0.25,0,0,1,919791,''),
  (919057,3,220,'Old Lordaeron Cards',0.35,0,0,1,919791,'');

DELETE FROM `quest_template` WHERE `entry` IN (919791,919792);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919791,919792);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919791,919792);
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqItemId1`,`ReqItemCount1`,`ReqItemId2`,`ReqItemCount2`,
   `RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919791,2,-9100,10,0,434,0,0,0,919704,919792,919643,4,919644,4,500,0,0,0,0,
   'Dis''Carded'' Remains',
   'The Alliance treats every Priest deck like a sermon. Lordaeron had plenty of sermons. I preached a few of them myself. Look where that left us.$B$BThere are discarded holy tablets and old playing cards here in Undercity. You should be able to find some upstairs in the old throne room and the big hallway. Bring me four stone fragments and four cards. I want to see what survived when the promises did not.',
   'Collect four Holy Tablet Fragments and four Old Lordaeron Cards in Undercity, then return to Jeffrey Shih.',
   '','','','',
   'Four fragments and four cards. I can wait. The tablets have already had years of practice.',
   'The words promise deliverance. The cards merely offer you a hand. I find the cards more honest.$B$BKeep that in mind when we play.'),
  (919792,2,-9100,10,0,434,0,0,2,919791,0,0,0,0,0,500,0,0,919624,1,
   'A Matter of Perspective',
   'You brought me the fragments and the cards. One is a promise and the other is a hand, and I have kept both on this table long enough to know which I trust.$B$BA Priest can mend a wound or make an enemy regret having flesh. The Alliance tends to dwell on the first part.$B$BPlay a game of Hearthstone with me and win. Let us see which part of the Priest you favor.',
   'Defeat Jeffrey Shih in a Hearthstone duel.',
   'Defeat Jeffrey Shih at Hearthstone','','','',
   'The table is still here. So am I. We can play again when you are ready.',
   'The tablets are still broken. The cards, at least, can still be played.$B$BTake the deck.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919539,919791),(919539,919792);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919539,919791),(919539,919792);

-- Shared generated policy: quest-only Basic Priest / Normal duel; account Horde roster at final turn-in.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` = 919539;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` = 919539;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919539,919792,'normal_priest','horde');

-- Owner requested capturing Jeffrey Shih's placed spawn and path on 2026-10-05.
-- Source GUID 12585229 is provenance only. Coordinates were read as DOUBLE.
-- Reuse the existing local GUID; allocate a local GUID only when no spawn exists.
-- No addon, template/special path, group, linking or waypoint scripts were attached.
SET @hs_priest_spawn_guid = (SELECT MIN(`guid`) FROM `creature` WHERE `id`=919539);
INSERT INTO `creature` (`id`,`id2`,`id3`,`id4`,`map`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,`health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
SELECT 919539,0,0,0,0,1646.9200439453125,290.1390075683594,-62.18199920654297,2.4702401161193848,120,120,0,100,100,2,0,0 WHERE @hs_priest_spawn_guid IS NULL;
SET @hs_priest_spawn_guid = COALESCE(@hs_priest_spawn_guid, LAST_INSERT_ID());
UPDATE `creature` SET `id`=919539, `id2`=0, `id3`=0, `id4`=0, `map`=0, `position_x`=1646.9200439453125, `position_y`=290.1390075683594, `position_z`=-62.18199920654297, `orientation`=2.4702401161193848, `spawntimesecsmin`=120, `spawntimesecsmax`=120, `wander_distance`=0, `health_percent`=100, `mana_percent`=100, `movement_type`=2, `spawn_flags`=0, `visibility_mod`=0 WHERE `guid`=@hs_priest_spawn_guid;
DELETE FROM `creature_movement` WHERE `id`=@hs_priest_spawn_guid;
INSERT INTO `creature_movement` (`id`,`point`,`position_x`,`position_y`,`position_z`,`orientation`,`waittime`,`wander_distance`,`script_id`) VALUES
  (@hs_priest_spawn_guid,1,1633.734619140625,298.728759765625,-62.17864990234375,100,0,0,0),
  (@hs_priest_spawn_guid,2,1610.110107421875,308.1663818359375,-62.17864990234375,100,0,0,0),
  (@hs_priest_spawn_guid,3,1588.984619140625,308.28125,-62.17864990234375,100,0,0,0),
  (@hs_priest_spawn_guid,4,1561.356689453125,300.2469177246094,-62.17864990234375,100,0,0,0),
  (@hs_priest_spawn_guid,5,1546.5582275390625,289.2732849121094,-62.181575775146484,100,0,0,0),
  (@hs_priest_spawn_guid,6,1532.487548828125,268.4001159667969,-62.17864227294922,100,0,0,0),
  (@hs_priest_spawn_guid,7,1527.780517578125,250.47503662109375,-62.17864227294922,100,0,0,0),
  (@hs_priest_spawn_guid,8,1527.5367431640625,228.8719940185547,-62.17864227294922,100,0,0,0),
  (@hs_priest_spawn_guid,9,1531.171630859375,208.995849609375,-62.17864227294922,100,0,0,0),
  (@hs_priest_spawn_guid,10,1550.3897705078125,185.59970092773438,-62.18134307861328,100,0,0,0),
  (@hs_priest_spawn_guid,11,1573.87939453125,174.09121704101562,-62.17879104614258,100,0,0,0),
  (@hs_priest_spawn_guid,12,1602.5394287109375,173.20773315429688,-62.17879104614258,100,0,0,0),
  (@hs_priest_spawn_guid,13,1627.7667236328125,180.37644958496094,-62.17879104614258,100,0,0,0),
  (@hs_priest_spawn_guid,14,1644.432861328125,190.16107177734375,-62.1817741394043,100,0,0,0),
  (@hs_priest_spawn_guid,15,1657.3623046875,208.63641357421875,-62.17878723144531,100,0,0,0),
  (@hs_priest_spawn_guid,16,1663.880859375,235.48098754882812,-62.17878723144531,100,0,0,0),
  (@hs_priest_spawn_guid,17,1663.4609375,260.1195983886719,-62.17878723144531,100,0,0,0),
  (@hs_priest_spawn_guid,18,1647.08740234375,289.8534240722656,-62.181705474853516,100,0,0,0);
