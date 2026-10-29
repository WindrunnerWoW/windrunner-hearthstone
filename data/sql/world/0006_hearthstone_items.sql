-- mod-hearthstone economy (session 12): Hearthstone coins and booster packs as real items, sold in the
-- real vendor window of the Hearthstone Masters (Ezra Chatterton 919501, Ibelin Redmoore 919502, migration 0004).
--
--   919601 Hearthstone Coin          the match reward (Hearthstone.Coins.*), stacks to 250, can be mailed and banked
--   919602 Hearthstone Booster Pack  right-click: the module adds it to the account unopened packs (the Packs tab).
--                                    Sold for 5 gold (buy_price, change it here)
--   919603 Hearthstone Booster Pack  the vendor listing for coins: no gold price, the module takes
--                                    Hearthstone.Pack.PriceCoins coin items and gives a 919602 instead. Never ends up in
--                                    a bag (the module answers every purchase of it). Keep its text in step with the price.
--
-- Display ids: the coin has the owner's own icon (69176, client patch, 2026-10-04). The booster packs use the owner's icon 69177 (INV_Hearthstone_Booster). The templates are rebuilt each time this file runs. The vendor rows are rebuilt for the two
-- Hearthstone Masters only; add rows for any other vendor NPC (it also needs the vendor npc flag and a
-- hearthstone_vendor row with packs = 1 for the coin purchase).
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

DELETE FROM `item_template` WHERE `entry` IN (919601, 919602, 919603);

INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`, `buy_count`, `buy_price`,
   `sell_price`, `inventory_type`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
VALUES
  (919601, 15, 0, 'Hearthstone Coin', 'Won in Hearthstone matches. A Hearthstone Master sells booster packs for them.',
   69176, 1, 0, 1, 0, 0, 0, 250, 0, 0, 1, ''),
  (919602, 15, 0, 'Hearthstone Booster Pack', 'Five Hearthstone cards. Right-click to add it to your packs.',
   69177, 3, 4, 1, 50000, 0, 0, 20, 0, 0, 1, ''),
  (919603, 15, 0, 'Hearthstone Booster Pack', 'Costs 10 Hearthstone Coins.',
   69177, 3, 0, 1, 0, 0, 0, 1, 0, 0, 1, '');

DELETE FROM `npc_vendor` WHERE `entry` IN (919501, 919502);

INSERT INTO `npc_vendor` (`entry`, `slot`, `item`, `maxcount`, `incrtime`, `itemflags`, `condition_id`) VALUES
  (919501, 0, 919603, 0, 0, 0, 0),
  (919501, 1, 919602, 0, 0, 0, 0),
  (919502, 0, 919603, 0, 0, 0, 0),
  (919502, 1, 919602, 0, 0, 0, 0);
