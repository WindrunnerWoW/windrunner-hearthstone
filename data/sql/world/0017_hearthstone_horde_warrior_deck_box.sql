-- Owner requested a permanent visible box, interactive only during the deck retrieval quest.
-- Template 919050 comes from world migration 0016. Item 919630 is supplied by the C++ click handler.
-- Corrected owner position beside Sarlek, from the second screenshot on 2026-10-04.
-- The previous screenshot targeted Sarlek and put the box underneath him.
-- No private visibility or per-player summons. Collection never removes this shared box.
-- Allocate a local guid. Reruns leave exactly one spawn.

DELETE FROM `gameobject` WHERE `id` = 919050;
INSERT INTO `gameobject`
  (`id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `rotation0`, `rotation1`, `rotation2`, `rotation3`,
   `spawntimesecsmin`, `spawntimesecsmax`, `animprogress`, `state`)
VALUES
  (919050, 1, 1837.373047, -4459.301270, 48.385082, 3.307281,
   0, 0, 0.996570384, -0.082749444, 120, 120, 100, 1);
