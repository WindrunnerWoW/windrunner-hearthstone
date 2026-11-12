-- mod-hearthstone: silent default-gossip greetings.
--
-- The voiceover addon plays a line on every GOSSIP_SHOW, keyed by NPC and gossip text. Most module
-- NPCs show a conversation line as their default gossip (the first or last page of a series), so
-- right-clicking replayed a line that was meant to be heard only during the conversation.
--
-- data/voiceover/gossip_speakers.json now lists those greeting texts under "unused", so the voiceover
-- generator no longer voices them; the conversation pages stay in "speakers" and remain voiced.
--
-- Seven menus below reuse a text that is *also* a spoken conversation page, so silencing the text would
-- silence the page too. They get their own greeting wording here (9195990 to 9195996), deliberately
-- left out of the voice manifest. Only the menu's default text changes; the scripts keep sending the
-- original page ids.
--
-- Rebuilt idempotently by the module updater. No backslashes, no semicolons in strings.

DELETE FROM `broadcast_text` WHERE `entry` IN (9195990, 9195991, 9195992, 9195993, 9195994, 9195995, 9195996);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195990, 'The libram is read and my notes are in order.$B$BShall we turn to the Alliance accounts?',
            'The libram is read and my notes are in order.$B$BShall we turn to the Alliance accounts?'),
  (9195991, 'I have enough evidence to prepare the practice deck.$B$BShall we begin?',
            'I have enough evidence to prepare the practice deck.$B$BShall we begin?'),
  (9195992, 'The cards are arranged.$B$BI believe I am ready to demonstrate a blessing.',
            'The cards are arranged.$B$BI believe I am ready to demonstrate a blessing.'),
  (9195993, 'My demonstration is complete.$B$BI await your assessment.',
            'My demonstration is complete.$B$BI await your assessment.'),
  (9195994, 'I am quite satisfied with that demonstration.$B$BShall we put it to the test at the table?',
            'I am quite satisfied with that demonstration.$B$BShall we put it to the test at the table?'),
  (9195995, 'AH! THE APPRENTICE RETURNS!$B$BCome to discuss my fee, have you?',
            'AH! THE APPRENTICE RETURNS!$B$BCome to discuss my fee, have you?'),
  (9195996, 'I believe you have my Control sample.$B$BShall we finish up?',
            'I believe you have my Control sample.$B$BShall we finish up?');

DELETE FROM `npc_text` WHERE `ID` IN (9195990, 9195991, 9195992, 9195993, 9195994, 9195995, 9195996);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195990, 9195990, 1), (9195991, 9195991, 1), (9195992, 9195992, 1),
  (9195993, 9195993, 1), (9195994, 9195994, 1), (9195995, 9195995, 1), (9195996, 9195996, 1);

-- Drin Lia (919540), HsHordePaladinAppendGossip menus.
UPDATE `gossip_menu` SET `text_id` = 9195990 WHERE `entry` = 65023;
UPDATE `gossip_menu` SET `text_id` = 9195991 WHERE `entry` = 65021;
UPDATE `gossip_menu` SET `text_id` = 9195992 WHERE `entry` = 65024;
UPDATE `gossip_menu` SET `text_id` = 9195993 WHERE `entry` = 65025;
UPDATE `gossip_menu` SET `text_id` = 9195994 WHERE `entry` = 65022;
-- Nib (919562), HsWarlockAppendGossip menu.
UPDATE `gossip_menu` SET `text_id` = 9195995 WHERE `entry` = 65063;
-- Marie Cu Rie (919510), HsHordeMageAppendGossip control-sample menu.
UPDATE `gossip_menu` SET `text_id` = 9195996 WHERE `entry` = 65009;
