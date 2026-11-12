-- mod-hearthstone: the Draft Arena introduction quest and the gossip texts of the arena at the
-- Hearthstone Masters.
--
-- Horde 919921 (Ezra Chatterton 919501) and Alliance 919931 (Ibelin Redmoore 919502). Both are plain event quests
-- (SpecialFlags 2, no objective rows, like the lesson 919704 / 919714): the module completes them when the account's
-- free arena run ends, whatever its result (src/HsArenaHost.cpp). The turn-in is the creature's involved relation.
-- There is NO start relation on purpose: the quest is offered only through the arena gossip while the ACCOUNT has all
-- nine classes unlocked and owns at least 140 different cards (kHsArenaIntroMinCards), which a start relation cannot
-- express. The module sends the quest details and takes the accept packet for these two quests itself. XP only, no
-- money or reputation (D11). MinLevel 10 like the other quests, QuestLevel 0 = the level of the character.
--
-- npc_text 9195900 / 9195901: the arena page of Ezra / Ibelin; 9195902 / 9195903: the confirmation page. Prices are
-- never in these texts (the options show them). Rebuilt each run; only the ids below are touched. No backslashes, no
-- semicolons in strings.

DELETE FROM `quest_template` WHERE `entry` IN (919921, 919931);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919921, 919931);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919921, 919931);

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919921, 2, -9100, 10, 0, 434, 0, 2,
   0, 0, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "Nothing but the Draw",
   "You have sat at a lot of tables, $N. Nine styles in your hands and a collection that has outgrown its box. Good. You will need all of it.$B$BThere is another way to play. Years ago a guard of the Night Watch in Darkshire, a human named Bill Petras, filled his long nights by building a deck at the table, one card at a time, from whatever the box gave him. His fellow watchmen took it up, then every traveler who came through Duskwood. Now they play it in Orgrimmar, and hardly anyone remembers whose idea it was.$B$BThey call it the Night Watch Arena. You sit down empty-handed and build as you go. It does not forgive mistakes. Every proper player should try it once, and I would rather you learned it from me.",
   "Draft a deck and play your first Night Watch Arena run.",
   "Finish your first Night Watch Arena run",
   "Not finished? Sit down when you are ready. The cards will still be here, and so will I.",
   "Hm. However far you got, you have sat at Petras's table now.$B$BMost players only learn how much they want it after the first run. From here on you sit down like everyone else who comes to this table.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919931, 2, -9100, 10, 0, 589, 0, 2,
   0, 0, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "The Luck of the Draw",
   "Nine styles mastered and a collection that no longer fits in one box. I would say you have earned a harder test, $N.$B$BHave you heard of Bill Petras? A watchman of the Night Watch in Darkshire. On the long nights between patrols he and his fellow guards took to building their decks at the table, one pick at a time, from whatever was offered. No favorites, no tried-and-true lists. Only judgment and a little luck. It spread from the barracks to the inn, from Duskwood to Stormwind, and much further than the watchmen ever meant it to.$B$BWe call it the Night Watch Arena. It is a hard teacher, so I would rather you met it once with me nearby.",
   "Draft a deck and play your first Night Watch Arena run.",
   "Finish your first Night Watch Arena run",
   "Not yet? Take your time. The table is not going anywhere, and neither am I.",
   "Well drafted, $N, whatever the final count. Petras would have offered you a seat by the watch fire.$B$BThe table is open to you now, as it is to anyone who cares to sit down. Do not expect the cards to be as kind next time.");

INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (919501, 919921), (919502, 919931);

DELETE FROM `npc_text` WHERE `ID` IN (9195900, 9195901, 9195902, 9195903);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195900, 9195901, 9195902, 9195903);

INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195900,
   'The Night Watch Arena. You draft your cards one at a time and play until you lose, or until nobody is left who can beat you.$B$BWhat will it be, $N?',
   'The Night Watch Arena. You draft your cards one at a time and play until you lose, or until nobody is left who can beat you.$B$BWhat will it be, $N?'),
  (9195901,
   'The Night Watch Arena, then. You build the deck at the table and play it against whoever sits down opposite you.$B$BWhat can I do for you, $N?',
   'The Night Watch Arena, then. You build the deck at the table and play it against whoever sits down opposite you.$B$BWhat can I do for you, $N?'),
  (9195902,
   'Are you sure, $N? Once the cards are dealt there is no taking them back.',
   'Are you sure, $N? Once the cards are dealt there is no taking them back.'),
  (9195903,
   'Are you certain, $N? Once you have begun there is no going back.',
   'Are you certain, $N? Once you have begun there is no going back.');

INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195900, 9195900, 1),
  (9195901, 9195901, 1),
  (9195902, 9195902, 1),
  (9195903, 9195903, 1);
