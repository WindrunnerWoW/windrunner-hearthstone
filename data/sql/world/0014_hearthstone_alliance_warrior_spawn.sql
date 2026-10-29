-- Steeg Haskell (template 919506 in world migration 0012).
-- Owner placement captured from tw_world on port 3307 on 2026-10-04.
-- Ironforge, map 0, original local guid 12585201.
-- Preserve the position, orientation and stationary movement of the placed NPC.
-- Allocate the guid locally, matching the existing master spawn migration.
-- Reruns leave exactly one Steeg spawn.

DELETE FROM `creature` WHERE `id` = 919506;
INSERT INTO `creature`
  (`id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
VALUES
  (919506, 0, -4851.649902344, -864.325988770, 510.247009277, 0.287734985,
   120, 120, 0, 0);
