-- mod-hearthstone: the quest (Naxxramas) cards a Draft Arena run may draft.
-- Additive only: CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere; add 0013_... instead. It contains no backslashes.

-- One row per quest card the account OWNED when the run was entered (count >= 1 of an acquisition quest card).
-- Written in the SAME transaction as the run row (HsArenaDbStorage::CreateRun) and never changed afterwards: a later
-- grant does not change a running run's pool, offers, deck check or AI opponents. A run with no rows here (every run
-- created before this migration) has the empty snapshot: no quest card is offered, which is the old behaviour.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_quest` (
  `run_id` INT UNSIGNED NOT NULL,
  `card_id` VARCHAR(32) NOT NULL,
  PRIMARY KEY (`run_id`, `card_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena owned quest cards at entry';
