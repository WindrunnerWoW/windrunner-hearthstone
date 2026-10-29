-- Alliance Druid teacher Thijs Nightbloom 919517: placed spawn recorded from tw_world:3307.
-- Original local GUID 12585213 is provenance only; let each database allocate its own GUID.
-- Stationary, no spawn addon, no movement template, no pool, game event or linking.
-- Requires the template from 0021. Reruns replace only this entry's spawns.
DELETE FROM `creature` WHERE `id` = 919517;
INSERT INTO `creature`
  (`id`,`id2`,`id3`,`id4`,`map`,
   `position_x`,`position_y`,`position_z`,`orientation`,
   `spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,
   `health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
  (919517,0,0,0,1,10125.2,2548.86,1321.63,4.8993,
   120,120,0,100,100,0,0,0);
