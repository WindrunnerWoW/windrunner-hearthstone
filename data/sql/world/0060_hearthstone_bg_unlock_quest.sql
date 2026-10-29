-- mod-hearthstone: the Warsong Battlegrounds unlock quests, a breadcrumb at Ezra Chatterton and Ibelin Redmoore.
--   920300  Horde:    Ezra Chatterton (919501) sends the character to the Warsong Outrider (920201)
--   920301  Alliance: Ibelin Redmoore (919502) sends the character to the Silverwing Sentinel (920200)
-- Like the ladder breadcrumb (migration 0054) the quests have no start relation: the module offers them through a two-step gossip
-- conversation (src/HsBgBreadcrumb.cpp), sends the quest details and takes the accept packet itself. No objectives, no reward.
-- The host ends the quest. Turning it in opens the host's tutorial and its second quest "The Rules of the Field"
-- (migration 0062), and THAT one opens play and the weekly Battlegrounds quests (conditions 920130 / 920131, migration 0061).
--     9195952  Ezra: the rumour about Warsong Gulch
--     9195953  Ibelin: the same from the Alliance side
-- Invented, not lore: short of recruits, the Warsong Outriders and the Silverwing Sentinels met under a truce and made up the
-- card game together at one table. Ezra's unpaid drink is invented too.
-- The hosts get the quest giver flag (npc_flags 3) so they can take the turn-in. $N is the player's name, $B a line break.
-- Rerunnable: only the ids named here are touched. The file contains no backslashes and no semicolons inside comments or strings.

UPDATE `creature_template` SET `npc_flags` = 3 WHERE `entry` IN (920200, 920201);

DELETE FROM `broadcast_text` WHERE `entry` IN (9195952, 9195953);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195952,
   "Ah, you've noticed.$B$BThe Outriders have bled for every tree they've taken out of Ashenvale, and the fresh recruits stopped coming months ago. The night elves of the Silverwing are no better off. So the story goes that the two sides met under a banner of truce, sat down at one table, and dealt themselves a game no one had ever played before.",
   "Ah, you've noticed.$B$BThe Outriders have bled for every tree they've taken out of Ashenvale, and the fresh recruits stopped coming months ago. The night elves of the Silverwing are no better off. So the story goes that the two sides met under a banner of truce, sat down at one table, and dealt themselves a game no one had ever played before."),
  (9195953,
   "Ah, you've noticed.$B$BThe Silverwing have held that forest against the orcs' axes for years, and the new recruits stopped coming long ago. The Warsong are no better off. So the story goes that the two sides met under a banner of truce, sat down at one table, and dealt themselves a game no one had ever played before.",
   "Ah, you've noticed.$B$BThe Silverwing have held that forest against the orcs' axes for years, and the new recruits stopped coming long ago. The Warsong are no better off. So the story goes that the two sides met under a banner of truce, sat down at one table, and dealt themselves a game no one had ever played before.");

DELETE FROM `npc_text` WHERE `ID` IN (9195952, 9195953);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195952, 9195952, 1), (9195953, 9195953, 1);

DELETE FROM `quest_template` WHERE `entry` IN (920300, 920301);
DELETE FROM `creature_questrelation` WHERE `quest` IN (920300, 920301);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (920300, 920301);

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`, `RewItemId2`, `RewItemCount2`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (920300, 2, -9100, 1, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   "Cards on the Field",
   "It's nothing like what we play here, $N. Four to a side, Horde against Alliance, and the last band standing holds the gulch until the next hand is dealt.$B$BThe Warsong are short of hands. I'd sooner see one of my own regulars sit at that table than some grunt who can't tell a card from a cudgel. If you want to help the Horde win, find the Warsong Outriders. They keep their table near the Hall of Legends in the Valley of Strength. Offer them your service.$B$BTell them Ezra sent you. One of them still owes me a drink.",
   "Speak with a Warsong Outrider near the Hall of Legends in the Valley of Strength.",
   "",
   "Another one come to gawk at the table? Or have you come to fight for it?",
   "Chatterton's sent you, has he? Then he's sent me someone worth more than his drink.$B$BWhen the night elves first sat down across from us, I thought it was a trick. It wasn't. They play to win as hard as any of my grunts, and they don't give an inch at the table. Four of us against four of them, $N. Show me what Orgrimmar taught you."),
  (920301, 2, -9100, 1, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   "Cards on the Field",
   "It's nothing like what we play here, $N. Four to a side, Alliance against Horde, and the last band standing holds the gulch until the next hand is dealt.$B$BThe Sentinels are short of hands. I'd sooner see one of my own regulars sit at that table than some recruit who can't tell a card from a shield. If you want to help the Alliance win, find the Silverwing Sentinel. She keeps her post in the park, here in Stormwind. Offer her your service.$B$BMention my name. The Sentinels remember a favor for a very long time.",
   "Speak with the Silverwing Sentinel in the park of Stormwind.",
   "",
   "Have you come to watch the table, or to fight for it?",
   "Redmoore sent you? Then you come well spoken for.$B$BWhen the orcs first sat down across from us, I expected treachery. Instead they play as fiercely as they fight, and they give nothing away at the table. Four of us against four of them, $N. Show me what Stormwind taught you.");

-- Ezra's quest is ended by the Warsong Outrider, Ibelin's by the Silverwing Sentinel.
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920201, 920300), (920200, 920301);
