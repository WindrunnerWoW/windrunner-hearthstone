-- mod-hearthstone: the spawns of the two Hearthstone Masters (templates in world migration 0004), of the two
-- introduction quest students (templates in world migration 0008) and of the Card Master (template in 0009).
-- Positions are the owner placement on the test realm, read from tw_world on 2026-10-03:
--   Ezra Chatterton  919501  map 1  Orgrimmar  (test realm guid 2902740 before this migration)
--   Ibelin Redmoore  919502  map 0  Stormwind  (test realm guid 2902739 before this migration)
--   Grunt Brakgul    919503  map 1  Orgrimmar  (read from tw_world on 2026-10-04, test realm guid 12585181)
--   Thomas Bell      919504  map 0  Stormwind  (read from tw_world on 2026-10-04, test realm guid 12585182)
--   Byron Bernstein  919505  map 0  Stormwind, Cathedral Square (test realm guid 12585193) and
--                            map 1  Orgrimmar, Cleft of Shadow (test realm guid 12585194), read from tw_world on 2026-10-04
-- Existing spawns of all five entries are removed first, so a rerun leaves exactly one spawn each.
-- The guid is left to AUTO_INCREMENT so it cannot clash with another database.
-- To move an NPC: change its row here (a new file hash reruns the migration at the next start).
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

DELETE FROM `creature` WHERE `id` IN (919501, 919502, 919503, 919504, 919505);

INSERT INTO `creature`
  (`id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
VALUES
  (919501, 1, 2025.49, -4637.01, 29.5544, 5.87399, 120, 120, 0, 0),
  (919502, 0, -8621.19, 389.725, 102.925, 4.77959, 120, 120, 0, 0),
  (919503, 1, 1778.98, -4548.13, 25.7929, 1.30421, 120, 120, 0, 0),
  (919504, 0, -8665.56, 547.405, 97.3289, 4.39936, 120, 120, 0, 0),
  (919505, 0, -8684.95, 774.695, 97.9326, 2.60473, 120, 120, 0, 0),
  (919505, 1, 1845.22, -4377.01, -14.4172, 2.51952, 120, 120, 0, 0);
