-- mod-hearthstone: the quest headers of the two ladder breadcrumb quests and the second quest of each chain,
-- "The Rules of the League".
--   Headers: the quests of migration 0054 had no zone, so the quest log filed them under no heading. Ezra Chatterton's quest 919970 (Horde)
--   is filed under Orgrimmar (zone 1637, Horde races 434), Ibelin Redmoore's quest 919971 (Alliance) under Stormwind City (zone 1519,
--   Alliance races 589), the same values as the lesson and the arena introduction quests. Both are level 10 like those.
--   Chain: 919970 -> 919972 (The Firebat) and 919971 -> 919973 (Amaz Chan). The second quest is given by the committee NPC that
--   ended the first one (a normal start relation, offered when the first is turned in) and is an event quest (SpecialFlags 2, no
--   objective rows): the character has to hear the rules, that is click through the tutorial pages of the NPC (migration 0052).
--   The last click, option 0 of menu 65284, runs the gossip script 9195960, which completes whichever of the two quests the character has
--   (the other command does nothing for a character without that quest). The pages stay available any time.
--   Turning in the second quest unlocks the ladder chairs (src/HsLadderBreadcrumb.cpp). No reward: the reward track is still to be built.
--   Entry fee: RewOrReqMoney is -500000 (fifty gold in copper, a negative value is a payment the character makes). The core
--   (Player::CanCompleteQuest and MoneyChanged) completes the quest only once the rules were heard AND the character holds the fifty
--   gold, and takes the gold when the quest is turned in. Without the gold the character can still hear the rules, and the quest
--   completes as soon as the gold is there. The turn-in window shows the fee itself.
--   Both quest texts approved 2026-10-15. $N is the player's name.
-- Rebuilt each time this file runs, only the ids named here are touched.
-- The file contains no backslashes and no semicolons inside comments or strings.

UPDATE `quest_template` SET `ZoneOrSort` = -9100, `MinLevel` = 10, `RequiredRaces` = 434, `NextQuestInChain` = 919972 WHERE `entry` = 919970;
UPDATE `quest_template` SET `ZoneOrSort` = -9100, `MinLevel` = 10, `RequiredRaces` = 589, `NextQuestInChain` = 919973 WHERE `entry` = 919971;

DELETE FROM `quest_template` WHERE `entry` IN (919972, 919973);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919972, 919973);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919972, 919973);

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`, `RewItemId2`, `RewItemCount2`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919972, 2, -9100, 10, 0, 434, 0, 2, 919970, 0, 0, 0, 0, 0, 0, -500000, 0, 0, 0, 0, 0,
   "The Rules of the League",
   "Every tavern's got someone who claims they're the finest player in Azeroth. Ask who they've beaten and suddenly they've got somewhere else to be.$B$BWe keep a proper count here. Wins, losses, a place in the standings. If you want to climb, you'll have to earn it.$B$BGive me a moment before you sit down, $N. I'll explain how we arrange the games and what the results mean for you. Saves us an argument later.$B$BEntry costs fifty gold. Bring it when we've finished going over the rules.",
   "Listen to The Firebat explain the rules of the league.",
   "",
   "Hold on. I haven't finished, and your first opponent won't wait while I explain the rest.",
   "That covers it. You're welcome at the league tables now, $N.$B$BBring a deck you trust. You'll find out soon enough which cards you ought to have left at home."),
  (919973, 2, -9100, 10, 0, 589, 0, 2, 919971, 0, 0, 0, 0, 0, 0, -500000, 0, 0, 0, 0, 0,
   "The Rules of the League",
   "When I founded the Archons, I discovered that even excellent players can disagree about the simplest things. Especially after losing.$B$BThe league brings together players from all over Azeroth. Before anyone draws a card, we ought to agree on how the competition is run.$B$BStay a moment, $N. I'll explain what you need to know before your first game. I'd much rather welcome you properly than settle a dispute halfway through it.$B$BThe entry fee is fifty gold. You can settle that with me once we've gone over the rules.",
   "Listen to Amaz Chan explain the rules of the league.",
   "",
   "A little patience, $N. We haven't covered everything yet, and I'd hate to send you to the table unprepared.",
   "There we are. You're welcome to take your place at the league tables, $N.$B$BI look forward to seeing how far you climb. Should you become champion, do remember who took the time to explain everything.");

-- The second quests are given and ended by the committee NPC of the chain: The Firebat 920101, Amaz Chan 920100.
INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES (920101, 919972), (920100, 919973);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920101, 919972), (920100, 919973);

-- The last click of the tutorial completes the rules quest of the character (command 7 = QUEST_EXPLORED: datalong the quest,
-- datalong2 the distance, 0 for none).
DELETE FROM `gossip_scripts` WHERE `id` = 9195960;
INSERT INTO `gossip_scripts` (`id`, `delay`, `command`, `datalong`, `datalong2`, `comments`) VALUES
  (9195960, 0, 7, 919972, 0, 'Hearthstone ladder: the rules are heard, Horde quest'),
  (9195960, 0, 7, 919973, 0, 'Hearthstone ladder: the rules are heard, Alliance quest');

UPDATE `gossip_menu_option` SET `action_script_id` = 0 WHERE `menu_id` IN (65280, 65281, 65282, 65283, 65284, 65285) AND `action_script_id` = 9195960;
UPDATE `gossip_menu_option` SET `action_script_id` = 9195960 WHERE `menu_id` = 65284 AND `id` = 0;
