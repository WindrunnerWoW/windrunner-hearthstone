-- mod-hearthstone economy: the NPCs that sell booster packs and offer crafting.
-- Owner data: CREATE TABLE IF NOT EXISTS, never dropped; seeded with INSERT IGNORE, so changes stay.
--
-- hearthstone_vendor: one row per creature entry. packs = 1: the vendor window may sell the booster pack listing for
-- Hearthstone Coins (item 919603 of migration 0006, answered by the module at Hearthstone.Pack.PriceCoins coins; the
-- NPC also needs the vendor npc flag and npc_vendor rows). crafting = 1: the gossip offers Craft and scrap cards, which
-- opens the collection window in crafting mode while the player stays near the NPC.
-- Seeded: the Hearthstone Masters Ezra Chatterton (919501) and Ibelin Redmoore (919502), world migration 0004, as pack
-- vendors without crafting. Only Byron Bernstein (919505, world migration 0009) crafts.
-- Add any NPC you placed, for example
--   INSERT INTO hearthstone_vendor (creature_entry, packs, crafting) VALUES (6740, 1, 0)
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

CREATE TABLE IF NOT EXISTS `hearthstone_vendor` (
  `creature_entry` INT UNSIGNED NOT NULL,
  `packs` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `crafting` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Pack vendor and crafting NPCs of mod-hearthstone (owner data)';

INSERT IGNORE INTO `hearthstone_vendor` (`creature_entry`, `packs`, `crafting`) VALUES (919501, 1, 0), (919502, 1, 0);
