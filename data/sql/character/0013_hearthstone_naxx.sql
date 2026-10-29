-- mod-hearthstone: the account-wide claim markers of the Naxxramas card items.
-- Additive only: CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere; add 0014_... instead. It contains no backslashes.

-- One row per account and card item (919652 to 919681). Each card item
-- grants its card once per account: the row is written in the SAME character database transaction as the card grant
-- (HsNaxxCommitClaim, src/HsNaxxStore.cpp). The primary key is the limit: a second claim of the same item fails the
-- transaction, so nothing is granted twice. character_guid is the character that claimed the item.
CREATE TABLE IF NOT EXISTS `hearthstone_account_naxx` (
  `account_id` INT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL,
  `character_guid` INT UNSIGNED NOT NULL,
  `claimed_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: Curse of Naxxramas card item claims per account';
