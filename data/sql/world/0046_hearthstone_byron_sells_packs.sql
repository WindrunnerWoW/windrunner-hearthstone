-- mod-hearthstone: Byron Bernstein (919505) sells the booster packs, Ezra Chatterton (919501) and Ibelin Redmoore
-- (919502) only start Night Watch Arena runs and normal duels (and give their quests).
--   npc_flags: gossip 1, quest giver 2, vendor 4. Ezra and Ibelin 7 -> 3 (no vendor window), Byron 3 -> 7.
--   npc_vendor: the booster pack listing (919603, coins) and the pack item (919602) move from Ezra and Ibelin to Byron.
--   hearthstone_vendor: packs 1 for Byron (his crafting stays), packs 0 for Ezra and Ibelin.
-- Reruns give the same result. Owner changes to other NPCs are not touched.
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

UPDATE `creature_template` SET `npc_flags` = 3 WHERE `entry` IN (919501, 919502);
UPDATE `creature_template` SET `npc_flags` = 7 WHERE `entry` = 919505;

DELETE FROM `npc_vendor` WHERE `entry` IN (919501, 919502, 919505) AND `item` IN (919602, 919603);

INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id`) VALUES
  (919505, 0, 919603, 0, 0, 0, 0),
  (919505, 1, 919602, 0, 0, 0, 0);

UPDATE `hearthstone_vendor` SET `packs` = 0 WHERE `creature_entry` IN (919501, 919502);
INSERT INTO `hearthstone_vendor` (`creature_entry`, `packs`, `crafting`) VALUES (919505, 1, 1)
ON DUPLICATE KEY UPDATE `packs` = 1;
