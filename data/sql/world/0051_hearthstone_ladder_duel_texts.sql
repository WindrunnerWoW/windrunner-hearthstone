-- mod-hearthstone: the texts of the ladder chair window for a paused duel. A ladder duel that was interrupted (logout, teleport, death, closing the window) waits for its player: the chair
-- offers to resume it or to surrender, nothing else. The options are menu lines written by the code (src/HsLadderTable.cpp).
--     9195933  the paused duel page:   the game is saved, resume it or give it up
--     9195934  the surrender page:     the question before a surrender counts as a loss
--   Texts are drafts for owner review. $N is the player's name, $B a line break.
-- Rebuilt each time this file runs, only the ids named above are touched (npc_text and broadcast_text entries).
-- The file contains no backslashes and no semicolons inside comments or strings.

DELETE FROM `broadcast_text` WHERE `entry` IN (9195933, 9195934);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195933,
   "Back so soon, $N?",
   "Back so soon, $N?"),
  (9195934,
   "Are you sure, $N? If you give up this game it counts as a loss, one star gone.",
   "Are you sure, $N? If you give up this game it counts as a loss, one star gone.");

DELETE FROM `npc_text` WHERE `ID` IN (9195933, 9195934);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195933, 9195933, 1), (9195934, 9195934, 1);
