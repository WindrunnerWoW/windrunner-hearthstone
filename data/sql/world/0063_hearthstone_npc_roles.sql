-- mod-hearthstone: the final roles of the Hearthstone Masters and the Card Master, applied again after every migration
-- that rebuilds their templates (0004 Ezra and Ibelin, 0006 their vendor rows, 0009 Byron).
-- The updater reruns a migration whose file changed but not the unchanged ones after it, so a rerun of 0004, 0006 or
-- 0009 alone would undo 0046 (Byron sells the packs) and 0053 (Ezra's size). This file repeats both and runs last.
--   Ezra Chatterton 919501 and Ibelin Redmoore 919502: npc_flags 3 (gossip, quests), no vendor window, no pack listing.
--   Byron Bernstein 919505: npc_flags 7 (gossip, quests, vendor), sells the pack item (919602) and the coin listing (919603).
--   Ezra is drawn at scale 1.35 (like Boro Softstep, migration 0040).
-- Reruns give the same result.
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

UPDATE `creature_template` SET `npc_flags` = 3 WHERE `entry` IN (919501, 919502);
UPDATE `creature_template` SET `npc_flags` = 7 WHERE `entry` = 919505;
UPDATE `creature_template` SET `scale` = 1.35 WHERE `entry` = 919501;

DELETE FROM `npc_vendor` WHERE `entry` IN (919501, 919502, 919505) AND `item` IN (919602, 919603);

INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id`) VALUES
  (919505, 0, 919603, 0, 0, 0, 0),
  (919505, 1, 919602, 0, 0, 0, 0);

UPDATE `hearthstone_vendor` SET `packs` = 0, `crafting` = 0 WHERE `creature_entry` IN (919501, 919502);
INSERT INTO `hearthstone_vendor` (`creature_entry`, `packs`, `crafting`) VALUES (919505, 1, 1)
ON DUPLICATE KEY UPDATE `packs` = 1, `crafting` = 1;
