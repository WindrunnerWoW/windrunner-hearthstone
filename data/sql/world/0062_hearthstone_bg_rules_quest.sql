-- mod-hearthstone: the second Warsong Battlegrounds unlock quest, "The Rules of the Field", and the hosts' tutorial.
--   920302  Horde:    given and ended by the Warsong Outrider (920201), after Ezra Chatterton's breadcrumb 920300
--   920303  Alliance: given and ended by the Silverwing Sentinel (920200), after Ibelin Redmoore's breadcrumb 920301
-- Chain: 920300 -> 920302 and 920301 -> 920303. Like the ladder's "The Rules of the League" (migration 0055) the second quest is
-- a normal start relation of the host, offered when the breadcrumb is turned in, and an event quest (SpecialFlags 2, no
-- objective rows): the character has to hear the rules, that is click through every page of the host's "How does it work?"
-- tutorial. The last page completes the quest of the character's faction and offers the turn-in on the same page
-- (src/HsBgGlue.cpp). No reward. Turning it in opens the hosts' "Play" option and the weekly Battlegrounds quests
-- (conditions 920130 / 920131, migration 0061). The pages stay available any time after the breadcrumb.
-- Tutorial pages: npc_text 9195973 to 9195981 in order, shared by both hosts. They replace the single help page 9195972 of
-- migration 0059 (deleted here). The numbers on the pages are the engine's (src/battlegrounds/HsBgShop.h, HsBgGame.h,
-- HsBgCombat.h). The coin and honor amounts are config values, so the last page names no figures.
-- $N is the player's name, $B a line break.
-- Rerunnable: only the ids named here are touched. The file contains no backslashes and no semicolons inside comments or strings.

UPDATE `quest_template` SET `NextQuestInChain` = 920302 WHERE `entry` = 920300;
UPDATE `quest_template` SET `NextQuestInChain` = 920303 WHERE `entry` = 920301;

DELETE FROM `quest_template` WHERE `entry` IN (920302, 920303);
DELETE FROM `creature_questrelation` WHERE `quest` IN (920302, 920303);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (920302, 920303);

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`, `RewItemId2`, `RewItemCount2`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (920302, 2, -9100, 1, 0, 434, 0, 2, 920300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   "The Rules of the Field",
   "Not so fast, $N. Before you sit down at this table, you hear how we play out here.$B$BForget the game they deal in the taverns. No decks, no duels, nothing you think you already know. And a player who sits down blind doesn't lose alone. Three of our own go down with them.$B$BSo listen. I'll walk you through all of it once, from the first hand to the last.",
   "Listen to the Warsong Outrider explain how the Warsong Battlegrounds are played.",
   "",
   "We're not done. You don't take a seat with half the rules in your head.",
   "Now you know as much as any grunt at this table, $N.$B$BTake your seat whenever you're ready. The night elves are already waiting, and they hate waiting almost as much as they hate losing."),
  (920303, 2, -9100, 1, 0, 589, 0, 2, 920301, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   "The Rules of the Field",
   "Patience, $N. Before you take a seat at this table, you must learn how we play on this field.$B$BIt is nothing like the game in the taverns. No decks, no duels, little of what you already know. And a player who sits down unprepared does not lose alone. Three of ours fall with them.$B$BSo listen closely. I will explain all of it once, from the first round to the last.",
   "Listen to the Silverwing Sentinel explain how the Warsong Battlegrounds are played.",
   "",
   "We are not finished yet. The Sentinels do not send anyone to the table half taught.",
   "Good. You know the field now, $N.$B$BTake your seat whenever you are ready. The orcs never keep the table waiting long, and they will not go easy on a newcomer.");

-- Both quests are given and ended by the host of their faction: the Warsong Outrider 920201, the Silverwing Sentinel 920200.
INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES (920201, 920302), (920200, 920303);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920201, 920302), (920200, 920303);

-- The tutorial. Each page is opened by the option under the page before it (src/HsBgGlue.cpp, kTutorialNext):
--   9195973  "How does it work?"                    what the game is, four against four
--   9195974  "How does a game begin?"               the hero, its power and armor, the two halves of a round
--   9195975  "How do I recruit?"                    buying, gold, hand and board, refresh and freeze
--   9195976  "And when my board is full?"           selling, the order of the warband
--   9195977  "How do I find stronger minions?"      tavern tiers, triples, tribes
--   9195978  "What happens in the fight?"           combat, damage, the cap, resting
--   9195979  "You said I fight alongside allies?"   allies, sending a minion
--   9195980  "Anything else I should know?"         the turn-4 quests, the portraits
--   9195981  "What do I get for playing?"           coins, honor, weeklies, leaving (the last page)
DELETE FROM `broadcast_text` WHERE `entry` IN (9195972, 9195973, 9195974, 9195975, 9195976, 9195977, 9195978, 9195979, 9195980, 9195981);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195973,
   "Forget the game you play in the taverns, $N. Out here nobody brings a deck. There is no mana, no thirty cards and no duel of one against one.$B$BFour of us sit down against four of them. Every player leads a hero and builds a warband of minions, round by round, and the warbands fight each other. Most heroes start with 40 health. Lose a fight and you lose health. Lose all of it and you are out of the game.$B$BThe last side with a player still standing holds the field.",
   "Forget the game you play in the taverns, $N. Out here nobody brings a deck. There is no mana, no thirty cards and no duel of one against one.$B$BFour of us sit down against four of them. Every player leads a hero and builds a warband of minions, round by round, and the warbands fight each other. Most heroes start with 40 health. Lose a fight and you lose health. Lose all of it and you are out of the game.$B$BThe last side with a player still standing holds the field."),
  (9195974,
   "With a hero. You are offered four, and each has a hero power of its own. Some powers cost gold to use, others work all by themselves. Read them before you choose. The right power can carry you through a whole game.$B$BSome heroes also bring armor. Armor takes the blows before your health does, and once it is gone it does not come back.$B$BAfter that, every round has two halves. First you recruit in Bob's Tavern. Then your warband fights.",
   "With a hero. You are offered four, and each has a hero power of its own. Some powers cost gold to use, others work all by themselves. Read them before you choose. The right power can carry you through a whole game.$B$BSome heroes also bring armor. Armor takes the blows before your health does, and once it is gone it does not come back.$B$BAfter that, every round has two halves. First you recruit in Bob's Tavern. Then your warband fights."),
  (9195975,
   "Bob shows you a few minions. Each one costs 3 gold, however strong it is. Click a minion to buy it and it goes to your hand, then put it on your board. Your hand holds ten cards, your board seven minions.$B$BYou get 3 gold in the first round and one more every round after that, up to 10. Gold you do not spend is gone when the round ends, so spend it.$B$BDon't like the offers? Refresh the tavern for 1 gold. Like them but can't pay? Freeze the tavern for free, and the same minions will wait for you next round.",
   "Bob shows you a few minions. Each one costs 3 gold, however strong it is. Click a minion to buy it and it goes to your hand, then put it on your board. Your hand holds ten cards, your board seven minions.$B$BYou get 3 gold in the first round and one more every round after that, up to 10. Gold you do not spend is gone when the round ends, so spend it.$B$BDon't like the offers? Refresh the tavern for 1 gold. Like them but can't pay? Freeze the tavern for free, and the same minions will wait for you next round."),
  (9195976,
   "Then you sell. Drag a minion from your board onto Bob and he pays you 1 gold for it. A few minions are worth more, and their card says so. Whatever you sell goes back into the tavern, for you or someone else to find again.$B$BThink about the order, too. Drag your minions along the board to rearrange them. In the fight they attack one after another from the left, so put the ones that should strike first on the left.$B$BAn empty slot wins nothing. A weak minion is still better than no minion, until you have the gold to replace it.",
   "Then you sell. Drag a minion from your board onto Bob and he pays you 1 gold for it. A few minions are worth more, and their card says so. Whatever you sell goes back into the tavern, for you or someone else to find again.$B$BThink about the order, too. Drag your minions along the board to rearrange them. In the fight they attack one after another from the left, so put the ones that should strike first on the left.$B$BAn empty slot wins nothing. A weak minion is still better than no minion, until you have the gold to replace it."),
  (9195977,
   "Upgrade the tavern. Bob starts at Tavern Tier 1, and every tier up to 6 brings stronger minions and more of them on offer. An upgrade is expensive, but its price drops by 1 gold for every round you wait.$B$BFind three copies of the same minion and they become one golden minion with twice the stats. Play it and Bob lets you pick a minion from the tier above your tavern.$B$BEach side only sees five tribes in a game. Watch which ones Bob keeps offering and build around them. A warband of one tribe is far stronger than a crowd of strangers.",
   "Upgrade the tavern. Bob starts at Tavern Tier 1, and every tier up to 6 brings stronger minions and more of them on offer. An upgrade is expensive, but its price drops by 1 gold for every round you wait.$B$BFind three copies of the same minion and they become one golden minion with twice the stats. Play it and Bob lets you pick a minion from the tier above your tavern.$B$BEach side only sees five tribes in a game. Watch which ones Bob keeps offering and build around them. A warband of one tribe is far stronger than a crowd of strangers."),
  (9195978,
   "Nothing you have to do but watch. When everyone has ended the turn, your warband fights one player from the other side, never one of your own. The minions fight by themselves until one warband is gone.$B$BThe winner strikes the loser's hero for their tavern tier plus the tier of every minion they have left. Until the first player falls, no fight deals more than 15 damage. A draw hurts nobody.$B$BWhen one side has more players standing, some of them sit out a round and heal 3 health. Late in the game the healing stops, so even a lone survivor can still win the field.",
   "Nothing you have to do but watch. When everyone has ended the turn, your warband fights one player from the other side, never one of your own. The minions fight by themselves until one warband is gone.$B$BThe winner strikes the loser's hero for their tavern tier plus the tier of every minion they have left. Until the first player falls, no fight deals more than 15 damage. A draw hurts nobody.$B$BWhen one side has more players standing, some of them sit out a round and heal 3 health. Late in the game the healing stops, so even a lone survivor can still win the field."),
  (9195979,
   "That is what makes this field different from any tavern. The three others on your side are your allies. You win together or you lose together.$B$BOnce every round you may give a minion from your hand to a living ally, and it costs you nothing. Drag the card onto your ally's portrait in the column beside the tavern. Spells cannot be given. If your ally is collecting copies, your gift may complete a golden minion for them.$B$BYour allies will send minions to you as well, so watch your hand. A fallen ally takes no more gifts, so help them while they still stand.",
   "That is what makes this field different from any tavern. The three others on your side are your allies. You win together or you lose together.$B$BOnce every round you may give a minion from your hand to a living ally, and it costs you nothing. Drag the card onto your ally's portrait in the column beside the tavern. Spells cannot be given. If your ally is collecting copies, your gift may complete a golden minion for them.$B$BYour allies will send minions to you as well, so watch your hand. A fallen ally takes no more gifts, so help them while they still stand."),
  (9195980,
   "In the fourth round Bob offers you three quests, each with a reward beside it. Choose one. Finish the quest and its reward stays with you for the rest of the game, so pick one your warband can actually finish.$B$BAnd keep an eye on the portraits beside the tavern. They show every player on both sides, with their health. Know who is strong, who is falling, and who on your side needs your help most.",
   "In the fourth round Bob offers you three quests, each with a reward beside it. Choose one. Finish the quest and its reward stays with you for the rest of the game, so pick one your warband can actually finish.$B$BAnd keep an eye on the portraits beside the tavern. They show every player on both sides, with their health. Know who is strong, who is falling, and who on your side needs your help most."),
  (9195981,
   "Every game you play to the end pays Hearthstone coins and honor, win or lose. If your side holds the field you earn more, and the best player of the winning side earns the most. Leave before the end and you earn nothing at all.$B$BWe post new tasks here every week as well. Finish them and there are booster packs in it for you.$B$BThat is everything, $N. The rest you learn at the table.",
   "Every game you play to the end pays Hearthstone coins and honor, win or lose. If your side holds the field you earn more, and the best player of the winning side earns the most. Leave before the end and you earn nothing at all.$B$BWe post new tasks here every week as well. Finish them and there are booster packs in it for you.$B$BThat is everything, $N. The rest you learn at the table.");

DELETE FROM `npc_text` WHERE `ID` IN (9195972, 9195973, 9195974, 9195975, 9195976, 9195977, 9195978, 9195979, 9195980, 9195981);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195973, 9195973, 1), (9195974, 9195974, 1), (9195975, 9195975, 1), (9195976, 9195976, 1), (9195977, 9195977, 1),
  (9195978, 9195978, 1), (9195979, 9195979, 1), (9195980, 9195980, 1), (9195981, 9195981, 1);
