-- mod-hearthstone economy: the account wallet (crafting dust) and crafting. Hearthstone coins are a real
-- bag item (world migration 0006), not a balance here.
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped.
-- Keyed by account id like the collection (D20: the dust is shared by all characters of an account).
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere; add a new file instead. It contains no backslashes.

-- One row per account that ever had dust. A missing row is an empty wallet.
CREATE TABLE IF NOT EXISTS `hearthstone_account_wallet` (
  `account_id` INT UNSIGNED NOT NULL,
  `dust` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: crafting dust per account';

-- Audit trail: one row per committed dust change (reason for example craft, scrap, gm_dust; source for example
-- char:42 or gm:5).
CREATE TABLE IF NOT EXISTS `hearthstone_wallet_ledger` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `dust_delta` INT NOT NULL,
  `dust_after` INT UNSIGNED NOT NULL,
  `reason` VARCHAR(32) NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: wallet changes';

-- One row per craft or scrap, keyed by the client idempotency key: a repeated request returns the stored
-- outcome instead of crafting or scrapping again. Kept forever (audit).
CREATE TABLE IF NOT EXISTS `hearthstone_craft_op` (
  `account_id` INT UNSIGNED NOT NULL,
  `op_key` VARCHAR(16) NOT NULL,
  `kind` ENUM('craft','scrap') NOT NULL,
  `card_id` VARCHAR(32) NOT NULL,
  `count` SMALLINT UNSIGNED NOT NULL,
  `dust_delta` INT NOT NULL,
  `dust_after` INT UNSIGNED NOT NULL,
  `collection_version` INT UNSIGNED NOT NULL,
  `card_total` SMALLINT UNSIGNED NOT NULL,
  `card_protected` SMALLINT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `op_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: crafts and scraps';
