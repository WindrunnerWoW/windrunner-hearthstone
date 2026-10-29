-- mod-hearthstone: the settlement of a Draft Arena run (packs, coins, special-card claim).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere; add 0012_... instead. It contains no backslashes.

-- One row per payout of a run, written in the SAME character database transaction as the pack stock change and the
-- run's move to settled (kind 'run'), or as the extra pack that replaces a special-card choice once every set is
-- complete (kind 'convert'). The primary key is the idempotency key: a second settlement of the same run fails this
-- insert and so rolls the whole transaction back, which is why a run can never pay its packs twice, across
-- retries, restarts and characters.
--   packs      the packs added to the account's pack stock by this payout.
--   coins      the Hearthstone Coin items owed to coin_guid (kind 'run' only); the delivery state is in
--              hearthstone_arena_coin_delivery.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_payout` (
  `run_id` INT UNSIGNED NOT NULL,
  `kind` VARCHAR(8) NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `packs` INT UNSIGNED NOT NULL DEFAULT 0,
  `coins` INT UNSIGNED NOT NULL DEFAULT 0,
  `coin_guid` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`run_id`, `kind`),
  KEY `idx_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena payouts (idempotency key per run)';

-- The coins of a settled run that are not in the character's bags or mail yet. Inserted in the settlement
-- transaction, so a crash between the settlement and the delivery leaves this row; the delivery retries from it.
--   coins_left   still to deliver. Delivery into the bags commits together with the character's inventory save and
--                the decrement of this row (one transaction); a mail chunk first records mail_item / mail_coins, so
--                that after a crash the chunk is recognised as sent (the item is in mail_items) or unsent.
-- The row is deleted when coins_left reaches 0.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_coin_delivery` (
  `run_id` INT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `coins_left` INT UNSIGNED NOT NULL,
  `mail_item` INT UNSIGNED NOT NULL DEFAULT 0,
  `mail_coins` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`run_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena coins awaiting delivery';
