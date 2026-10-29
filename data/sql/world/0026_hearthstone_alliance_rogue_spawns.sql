-- Owner requested exporting the placed Alliance Rogue actors on 2026-10-05.
-- Captured from tw_world:3307; no live spawn rows were changed.
-- Operative Matt Dixon 919532: original local GUID 12585225.
-- Alex Horley 919533: original local GUID 12585226.
-- Preserve every non-GUID spawn field and full stored float precision.
-- Allocate GUIDs locally; reruns leave exactly one of each new actor.
-- No addon, waypoint, group or linking records are attached to these spawns.
-- Existing Aedis/Osric placements are untouched; no objective or pickup spawns.
DELETE FROM `creature` WHERE `id` IN (919532,919533);
INSERT INTO `creature`
  (`id`, `id2`, `id3`, `id4`, `map`,
   `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
   `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`)
VALUES
  (919532,0,0,0,0,-8792.9501953125,328.4530029296875,103.13300323486328,6.147520065307617,
   120,120,0,100,100,0,0,0),
  (919533,0,0,0,0,-8667.349609375,465.1300048828125,100.24600219726562,4.6709699630737305,
   120,120,0,100,100,0,0,0);
