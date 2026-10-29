-- mod-hearthstone: the texts of the ladder chair window.
--   The chair (gameobject 919100, world migration 0049) opens a gossip window from the module code
--   (src/HsLadderTable.cpp). The module sends the three pages with these npc_text rows, the options (the standing, the three
--   offers, the decks, the confirmation) are menu lines written by the code, because gossip text cannot be sent from code.
--     9195930  the offers page:        the three opponents and what is at stake
--     9195931  the deck page:          which deck to bring
--     9195932  the confirmation page:  the opponent walks over, the match starts when it sits down
--   Texts are drafts for owner review. $N is the player's name, $B a line break.
-- Rebuilt each time this file runs, only the ids named above are touched (npc_text and broadcast_text entries).
-- The file contains no backslashes and no semicolons inside comments or strings.

DELETE FROM `broadcast_text` WHERE `entry` IN (9195930, 9195931, 9195932);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195930,
   "Take a seat, $N. Three players of the league are ready to sit down with you. Pick the one whose deck you would like to face.$B$BA win earns a star, a loss costs one.",
   "Take a seat, $N. Three players of the league are ready to sit down with you. Pick the one whose deck you would like to face.$B$BA win earns a star, a loss costs one."),
  (9195931,
   "Which of your decks do you bring to the table, $N?",
   "Which of your decks do you bring to the table, $N?"),
  (9195932,
   "Is this the game you want, $N?",
   "Is this the game you want, $N?");

DELETE FROM `npc_text` WHERE `ID` IN (9195930, 9195931, 9195932);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195930, 9195930, 1), (9195931, 9195931, 1), (9195932, 9195932, 1);
