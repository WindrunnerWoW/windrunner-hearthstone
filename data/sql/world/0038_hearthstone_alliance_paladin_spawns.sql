-- Owner requested recording the placed Alliance Paladin actors, 2026-10-08.
-- Captured read-only from tw_world:3307, with coordinates read as DOUBLE.
-- Sir Rowan Vale 919570: original local GUID 12585254.
-- Squire Tess 919571: original local GUID 12585256.
-- Ada Sparkplugz 919577: original local GUID 12585257.
-- GUIDs above are provenance only; each database allocates its own GUIDs.
-- Preserve every non-GUID spawn field and the full stored coordinate precision.
-- No addons, GUID/entry paths, groups, links, events or pools are attached.
-- Requires templates from 0036; 0037 supplies Ada's selected name/model.
-- Reruns replace only these three actors. Other NPCs and unspawned objective
-- credits 919572-919576 remain untouched. This export does not apply live SQL.
DELETE FROM `creature` WHERE `id` IN (919570,919571,919577);
INSERT INTO `creature`
  (`id`,`id2`,`id3`,`id4`,`map`,
   `position_x`,`position_y`,`position_z`,`orientation`,
   `spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,
   `health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
  (919570,0,0,0,0,-8643.48046875,753.8499755859375,97.26799774169922,1.2381399869918823,
   120,120,0,100,100,0,0,0),
  (919571,0,0,0,0,-8644.259765625,754.7059936523438,97.26789855957031,0.6828640103340149,
   120,120,0,100,100,0,0,0),
  (919577,0,0,0,0,-8560.3896484375,371.2120056152344,108.38600158691406,3.718440055847168,
   120,120,0,100,100,0,0,0);
