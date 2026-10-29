-- mod-hearthstone: the two Warsong Battlegrounds hosts.
--   920200  Silverwing Sentinel  (Alliance, Huntress Leafrunner look, display 14419, on a nightsaber)
--   920201  Warsong Outrider     (Horde, display 14719)
-- Dummy level 60 humanoids with a gossip menu only (npc_flags 1), each on its own side: the Sentinel is Darnassus
-- (faction 79, like the Darnassus Sentinel), the Outrider Orgrimmar (faction 29, like the core Warsong Outrider 12864).
-- The module appends the options ("Play Warsong Battlegrounds", "How does it work?", "Leave my game" while one is open)
-- to the greeting from src/HsBgGlue.cpp. It picks the host by the player's team, not by the creature's faction.
-- The owner places the hosts (H2), and their placements are recorded at the end of this file like the class-quest actors.
-- Gossip greeting texts are npc_text / broadcast_text below. The "How does it work?" tutorial pages are migration 0062
-- (it replaced the single help page 9195972 that used to be here).
-- Random-boss display ids are reused on purpose (no new art). Rerunnable: only the ids named here are touched.
-- The file contains no backslashes and no semicolons inside comments or strings.

DELETE FROM `creature_template` WHERE `entry` IN (920200, 920201);

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `gossip_menu_id`, `script_name`)
VALUES
  (920200, 14419, 'Silverwing Sentinel', 'Warsong Battlegrounds', 60, 60, 4500, 4500,
   3500, 79, 1, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 65290, '');

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `gossip_menu_id`, `script_name`)
VALUES
  (920201, 14719, 'Warsong Outrider', 'Warsong Battlegrounds', 60, 60, 4500, 4500,
   3500, 29, 1, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 65291, '');

DELETE FROM `broadcast_text` WHERE `entry` IN (9195970, 9195971);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195970,
   'The Silverwing holds this field against the Warsong. For one game the fighting stops and the cards decide it, four against four.$B$BReady to take a seat, $N?',
   'The Silverwing holds this field against the Warsong. For one game the fighting stops and the cards decide it, four against four.$B$BReady to take a seat, $N?'),
  (9195971,
   'The Warsong rides for the Horde. For one game the axes stay sheathed and the cards do the talking, four against four.$B$BReady to take a seat, $N?',
   'The Warsong rides for the Horde. For one game the axes stay sheathed and the cards do the talking, four against four.$B$BReady to take a seat, $N?');

DELETE FROM `npc_text` WHERE `ID` IN (9195970, 9195971);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195970, 9195970, 1), (9195971, 9195971, 1);

DELETE FROM `gossip_menu` WHERE `entry` IN (65290, 65291);
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES
  (65290, 9195970), (65291, 9195971);

-- Owner placements, captured read-only from tw_world:3307 on 2026-10-23 (local guids 12585389-12585392, provenance only).
-- Orgrimmar (map 1): two Warsong Outriders with a Dire Riding Wolf (51580) and a Timber Riding Wolf (51581) beside them.
-- The wolves are shared rental-mount templates spawned elsewhere too, so their rows are matched by position, not by id.
DELETE FROM `creature` WHERE `id` = 920201;
DELETE FROM `creature` WHERE `map` = 1 AND (
  (`id` = 51580 AND ABS(`position_x` - 1586.31) < 1 AND ABS(`position_y` + 4240.58) < 1) OR
  (`id` = 51581 AND ABS(`position_x` - 1590.44) < 1 AND ABS(`position_y` + 4245.64) < 1));
INSERT INTO `creature`
  (`id`, `id2`, `id3`, `id4`, `map`,
   `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
   `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`)
VALUES
  (920201,0,0,0,1,1587.969970703125,-4241.830078125,47.06719970703125,5.377600193023682,120,120,0,100,100,0,0,0),
  (920201,0,0,0,1,1589.3900146484375,-4243.580078125,47.105499267578125,2.2462100982666016,120,120,0,100,100,0,0,0),
  (51580,0,0,0,1,1586.31005859375,-4240.580078125,47.11069869995117,0.6008149981498718,120,120,0,100,100,0,0,0),
  (51581,0,0,0,1,1590.43994140625,-4245.64013671875,47.21870040893555,0.7390339970588684,120,120,0,100,100,0,0,0);

-- Stormwind park (map 0): the Silverwing Sentinel, captured 2026-10-23 (local guid 12585398, provenance only).
-- She rides a nightsaber (mount display 9991, as Huntress Leafrunner's spawn). The mount is a per-spawn creature_addon
-- row, so it is tied to the guid this insert creates.
DELETE FROM `creature_addon` WHERE `guid` IN (SELECT `guid` FROM `creature` WHERE `id` = 920200);
DELETE FROM `creature` WHERE `id` = 920200;
INSERT INTO `creature`
  (`id`, `id2`, `id3`, `id4`, `map`,
   `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
   `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`)
VALUES
  (920200,0,0,0,0,-8743.7998046875,968.9619750976562,99.1958999633789,4.78971004486084,120,120,0,100,100,0,0,0);
INSERT INTO `creature_addon` (`guid`, `mount_display_id`) VALUES (LAST_INSERT_ID(), 9991);
