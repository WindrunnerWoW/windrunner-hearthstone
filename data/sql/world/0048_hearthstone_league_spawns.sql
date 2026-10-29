-- mod-hearthstone: the owner-placed spawns of the World NPC Ladder.
-- Read from tw_world on 2026-10-14 (TCP port 3307, SELECT only), full stored float precision, every spawn field.
--   Tables (gameobject 2002877, DwarvenTableOrnate02): guid 7003001 Thunder Bluff, guid 7003002 Darnassus. The table
--   GUIDs are kept so the ladder chairs of 14C can name their table. No gameobject addon table exists.
--   League Committee (world migration 0047): Amaz Chan 920100 in Darnassus (source guid 12585350), The Firebat 920101
--   in Thunder Bluff (source guid 12585348). Creature GUIDs are allocated by the database. No creature_addon rows.
-- Reruns leave exactly these two tables and one spawn of each NPC.
-- The file contains no backslashes and no semicolons inside comments or strings.

DELETE FROM `gameobject` WHERE `guid` IN (7003001, 7003002);
INSERT INTO `gameobject`
  (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `rotation0`, `rotation1`, `rotation2`, `rotation3`,
   `spawntimesecsmin`, `spawntimesecsmax`, `animprogress`, `state`, `spawn_flags`, `visibility_mod`)
VALUES
  (7003001, 2002877, 1, -1224.239990234375, 152.55599975585938, 133.19200134277344, 4.649409770965576,
   0, 0, 0.7290199995040894, -0.6844919919967651,
   300, 300, 100, 1, 0, 0),
  (7003002, 2002877, 1, 9819.169921875, 2350.610107421875, 1321.6800537109375, 0.40598300099372864,
   0, 0, 0.20160000026226044, 0.9794679880142212,
   300, 300, 100, 1, 0, 0);

DELETE FROM `creature` WHERE `id` IN (920100, 920101);
INSERT INTO `creature`
  (`id`, `id2`, `id3`, `id4`, `map`,
   `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
   `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`)
VALUES
  (920100, 0, 0, 0, 1, 9821.76953125, 2345.409912109375, 1321.6800537109375, 5.092440128326416,
   120, 120, 0, 100, 100, 0, 0, 0),
  (920101, 0, 0, 0, 1, -1221.0899658203125, 146.55799865722656, 133.5540008544922, 4.601490020751953,
   120, 120, 0, 100, 100, 0, 0, 0);
