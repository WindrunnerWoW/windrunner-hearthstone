-- mod-hearthstone: practice runs of the Night Watch Arena.
-- After the day's paid run an account may start as many runs as it likes: they cost nothing, never use the daily slot
-- and pay no prize (pay_method = practice). Only the enum of hearthstone_arena_run.pay_method grows. Existing rows
-- keep their value, a rerun changes nothing.
--
-- The file contains no backslashes and no semicolons inside comments or strings.

ALTER TABLE `hearthstone_arena_run`
  MODIFY COLUMN `pay_method` ENUM('free','gold','coins','practice') NOT NULL;
