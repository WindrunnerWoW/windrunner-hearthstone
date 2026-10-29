-- mod-hearthstone: account-wide unlocks of the quest NPC opponents (PHASE_11).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- Keyed by account id in the character database like the decks. The core AutoUpdater identifies a
-- migration by the SHA1 of its bytes: never edit this file after it was applied anywhere; add 0007_...
-- instead. It contains no backslashes.

-- One row per account and quest NPC (world table hearthstone_npc_unlock) the account unlocked: a
-- character of the account turned in quest_id. Written at login (a reconcile over the rewarded quests of
-- every character of the account, so quests done before the feature count too), when a character turns
-- in the quest, or when a character that already did talks to the NPC. Ordinary WoW quest records of
-- other characters are never changed. source: login:<character guid>, reward:<guid> or talk:<guid>.
CREATE TABLE IF NOT EXISTS `hearthstone_account_npc` (
  `account_id` INT UNSIGNED NOT NULL,
  `creature_entry` INT UNSIGNED NOT NULL,
  `quest_id` INT UNSIGNED NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `unlocked_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: quest NPC opponents unlocked per account';
