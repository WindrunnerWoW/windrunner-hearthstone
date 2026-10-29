-- mod-hearthstone: the offers of the World NPC Ladder chair. Per account the NPCs it was offered
-- the last time it sat down, how often offers were drawn for it and the NPC of its last ladder match.
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere, add 0017_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- The server (src/HsLadderStore.cpp, src/ladder/HsLadderRows.cpp) replaces the slots and the state row of one account
-- in ONE transaction. The offers are picked from the ladder by src/ladder/HsLadderOffers.cpp: the same ladder seed, account
-- and draw_counter always give the same offers, so a reroll (draw_counter + 1) is new but can be reproduced.

-- One row per account that was ever offered anything.
--   draw_counter   offers drawn for the account so far. The next draw uses draw_counter + 1 in its seed.
--   drawn_round    hearthstone_ladder.rounds_played when the current slots were drawn.
--   last_opponent  the entrant id (the creature entry) of the account's last ladder match, left out of the next
--                  offers unless no other NPC is left. 0 for none.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_offer_state` (
  `account_id` INT UNSIGNED NOT NULL,
  `draw_counter` INT UNSIGNED NOT NULL DEFAULT 0,
  `drawn_round` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_opponent` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder offer state per account';

-- The current offers of an account: slot 0 to 2 (best ladder position first) and the NPC (its creature entry) in
-- it. No rows after a ladder match until the next sit-down draws a new set.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_offer` (
  `account_id` INT UNSIGNED NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL,
  `npc_id` BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder offered NPCs per account';
