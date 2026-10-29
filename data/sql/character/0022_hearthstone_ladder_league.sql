-- mod-hearthstone: the league counter of the NPC ladder. Additive only: CREATE TABLE IF NOT EXISTS,
-- nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere, add 0023_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- One row per league. A league starts when a new ladder is created: the first ladder of a server, and every
-- ladder reset (.hs ladder reset confirm), which deletes the whole ladder (ranks, stars, NPC decks, the NPC
-- games) and plays the start again. The current league is the highest number; no row means league 0, in which
-- nobody earns a hero portrait credit (a ladder that existed before this table is not paid retroactively).
-- The table is not part of the ladder tables, so a ladder reset never deletes it.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_league` (
  `league` INT UNSIGNED NOT NULL,
  `started_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`league`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder leagues';
