-- Owner requested extracting all five placed Horde Rogue actors on 2026-10-05.
-- Read from tw_world through mysql.exe, TCP port 3307. No live rows were changed.
-- Original local GUIDs: For'sen 12585214, Gora 12585217, Rikka 12585215,
-- Zikko 12585218, Sable 12585219. Allocate destination GUIDs locally.
-- Preserve full stored float precision and every non-GUID spawn field.
-- Source actors have no creature_addon, waypoint, group or linking records.
-- Reruns leave one of each actor; never spawn objective credits 919527-919531.
DELETE FROM `creature` WHERE `id` IN (919522,919523,919524,919525,919526);
INSERT INTO `creature`
  (`id`, `id2`, `id3`, `id4`, `map`,
   `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
   `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`)
VALUES
  (919522,0,0,0,1,1798.7900390625,-4267.58984375,7.437290191650391,2.934999942779541,
   120,120,0,100,100,0,0,0),
  (919523,0,0,0,1,2018.6800537109375,-4575.52001953125,46.41130065917969,5.296679973602295,
   120,120,0,100,100,0,0,0),
  (919524,0,0,0,1,1682.7900390625,-4271.14013671875,32.867401123046875,4.160980224609375,
   120,120,0,100,100,0,0,0),
  (919525,0,0,0,1,2120.580078125,-4764.259765625,50.407798767089844,3.800489902496338,
   120,120,0,100,100,0,0,0),
  (919526,0,0,0,1,1835.02001953125,-4565.10009765625,23.530000686645508,1.9013899564743042,
   120,120,0,100,100,0,0,0);
