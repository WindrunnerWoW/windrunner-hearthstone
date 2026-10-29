-- mod-hearthstone: the introduction questline (world migration 0008).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere; add a new file instead. It contains no backslashes.

-- One row per account that received the free booster pack of the introduction (the turn-in of the lesson quest,
-- 919704 or 919714). The pack is granted once per account (session 1 decision): a later character of the account
-- that finishes the introduction again gets no second pack. character_guid is the character that received it.
CREATE TABLE IF NOT EXISTS `hearthstone_account_intro` (
  `account_id` INT UNSIGNED NOT NULL,
  `character_guid` INT UNSIGNED NOT NULL,
  `quest_id` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: introduction pack per account';
