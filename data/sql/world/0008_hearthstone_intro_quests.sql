-- mod-hearthstone: the introduction questlines.
--
-- Horde:    919701 breadcrumb (Scout Tharr 14377 to Ezra Chatterton 919501), 919702 Ezra to Grunt Brakgul 919503,
--           919703 Brakgul to Ezra with the item 919604, 919704 Ezra to Ezra (the lesson, for now a plain turn-in),
--           919705 Ezra to Byron Bernstein 919505 (find him), 919706 Byron to Byron (the Card Master's trade).
-- Alliance: 919711 breadcrumb (Stephanie Turner 6174 to Ibelin Redmoore 919502), 919712 Ibelin to Thomas Bell 919504,
--           919713 Thomas to Ibelin with the item 919605, 919714 Ibelin to Ibelin,
--           919715 Ibelin to Byron (find him), 919716 Byron to Byron (the same text as 919706).
-- The breadcrumb is optional (quest 1 has no previous quest) and closes once quest 1 is taken (NextQuestInChain).
-- Rewards: quest 2 lists the item 919610 Hearthstone Starter Deck (world migration 0011) as its reward: right-clicking it
-- gives the starter deck of the character class (HsIntroGlue.cpp). The module hands out the free booster pack (once per
-- account) at the turn-in of quest 3 (the turn-in packet, HsIntroGlue.cpp). The
-- quests grant no money or reputation (D11) and only XP (owner, session 14): 10 for the breadcrumb, 500 for quests 1 to 5. MinLevel is 10 (all quests).
-- QuestLevel 0 is the level of the character.
--
-- The students are new creatures without spawns: the owner places them (Brakgul copies an Orgrimmar Grunt 3296,
-- Thomas Bell a Dock Worker 60573). Scout Tharr gets the quest giver flag. All rows are rebuilt each run.
-- The quest texts use double-quoted strings (they contain apostrophes). No backslashes, no semicolons in strings.

DELETE FROM `item_template` WHERE `entry` IN (919604, 919605);

INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`, `buy_count`, `buy_price`,
   `sell_price`, `inventory_type`, `max_count`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
VALUES
  (919604, 12, 0, "Brakgul's Hearthstone Cards", "A worn deck of Hearthstone cards. Brakgul does not want them anymore.", 69179, 1, 0, 1, 0, 0, 0, 1, 1, 4, 1, 0, ''),
  (919605, 12, 0, "Thomas Bell's Hearthstone Cards", "A barely used deck of Hearthstone cards, bought for a lesson Thomas never finished.", 69179, 1, 0, 1, 0, 0, 0, 1, 1, 4, 1, 0, '');

DELETE FROM `creature_template` WHERE `entry` IN (919503, 919504);

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `inhabit_type`, `regeneration`, `equipment_id`)
VALUES
  (919503, 18118, 'Grunt Brakgul', '', 55, 55, 5446, 6085,
   7700, 85, 2, 1, 1.42857, 18, 5,
   259.182, 339.293, 248, 2000, 2000, 1,
   36864, 7, 1, 3, 3296),
  (919504, 341, 'Thomas Bell', 'Dockworker', 5, 5, 112, 112,
   145, 12, 2, 1, 1.14286, 18, 5,
   7.7, 11, 54, 2000, 2000, 1,
   0, 7, 3, 3, 0);

UPDATE `creature_template` SET `npc_flags` = `npc_flags` | 2 WHERE `entry` = 14377;

DELETE FROM `quest_template` WHERE `entry` IN (919701, 919702, 919703, 919704, 919705, 919706, 919711, 919712, 919713, 919714, 919715, 919716);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919701, 919702, 919703, 919704, 919705, 919706, 919711, 919712, 919713, 919714, 919715, 919716);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919701, 919702, 919703, 919704, 919705, 919706, 919711, 919712, 919713, 919714, 919715, 919716);

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919701, 2, -9100, 10, 0, 434, 0, 0,
   0, 919702, 0, 0, 0, 0,
   10, 0, 0, 0, 0,
   "A Different Kind of Battle",
   "You heard of Hearthstone? No?$B$BNo blades. No blood. Just cards.$B$BSounds weak, until you see two warriors nearly come to blows arguing over who played the better hand.$B$BA tauren named Ezra Chatterton has been teaching the game here in Orgrimmar. If you think you've got the wits for a different kind of battle, seek him out.$B$BYou'll find him in the goblin tavern in the Valley of Honor, if you are interested.",
   "Find Hearthstone Master Ezra Chatterton in the goblin tavern in the Valley of Honor.",
   "Find Hearthstone Master Ezra Chatterton",
   "Were you looking for someone?",
   "So you've come to learn about Hearthstone.$B$BGood.$B$BI am Ezra Chatterton. Around this table, strength alone won't win you anything.$B$BIf you want to learn, I can teach you. But first, I need to ask you for a favor...");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919702, 2, -9100, 10, 0, 434, 0, 0,
   0, 919703, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "A Student Gone Missing",
   "I seem to have angered a student, $N.$B$BGrunt Brakgul paid for a lesson in Hearthstone. We sat down, I dealt the cards, and I had barely begun explaining the rules when he stood up and walked away.$B$BNo complaint. No farewell. Not even a zug.$B$BI called after him, but he only walked faster.$B$BLast I saw, he was heading toward the Valley of Strength. Would you find him for me? I'd at least like to know what frightened him away from a lesson he'd already paid for.",
   "Find Grunt Brakgul and discover why he abandoned his Hearthstone lesson.",
   "Find Grunt Brakgul",
   "What? Ezra send you?$B$BBrakgul busy.$B$BVery busy.",
   "Brakgul not angry. Brakgul not run away.$B$BBrakgul... remembered important guard work.$B$B...$B$BFine.$B$BCards have too many rules.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919703, 2, -9100, 10, 0, 434, 0, 0,
   919702, 919704, 919604, 1, 919604, 1,
   500, 0, 0, 919610, 1,
   "Too Many Rules",
   "Other grunts play, Brakgul want to play, too. Zug Zug. Brakgul tried to learn.$B$BEzra says mana. Then crystals. Then minions. Then Brakgul puts minion down, but minion cannot attack yet.$B$BWhy not attack? Enemy right there!$B$BThen some minions can attack because little word on card says so. Other cards got different little words. Some cards got many little words.$B$BToo many rules.$B$BBrakgul bought these cards from Ezra before lesson. Brakgul does not want them anymore.$B$BYou take them.$B$BTell Ezra Brakgul has important guard work.$B$BFar away from cards.",
   "Take Brakgul's Hearthstone Cards back to Ezra.",
   "Return Brakgul's Hearthstone Cards to Ezra",
   "You've returned, $N. Did you manage to find Brakgul?$B$BI hope he is fine.",
   "Ah.$B$BThat explains the rather hurried departure.$B$BBrakgul is a dependable guard, but perhaps Hearthstone was asking a little too much of him.$B$BHe gave you the cards?$B$BWell, there's no sense in letting a perfectly good set go to waste.$B$BAnd since you're already holding them...$B$BPerhaps I should teach you instead.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919704, 2, -9100, 10, 0, 434, 0, 2,
   919703, 919705, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "A Seat at the Table",
   "Don't worry. I'll go more slowly than I did with Brakgul.$B$BHearthstone looks complicated when all the pieces are thrown at you at once, but the basics are simple enough.$B$BMana lets you play your cards. Minions fight for you. Bring your opponent down before they do the same to you.$B$BThe rest comes with practice.$B$BSit down, $N.",
   "Listen to Ezra's Hearthstone lesson until he has told you everything you need to know.",
   "Complete Ezra's Hearthstone lesson",
   "Still gathering your courage?$B$BDon't worry. The cards rarely bite.$B$BTake your seat when you're ready.",
   "Not bad, $N. Not bad at all.$B$BYou listened, considered your moves, and most importantly, you stayed in your chair for the entire lesson.$B$BThat already puts you ahead of my last student.$B$BKeep the cards. They're yours now.$B$BThere is much more to learn, of course, but the best way to learn Hearthstone is to play it.$B$BFind opponents. Build new decks. Try things that seem foolish.$B$BSometimes they even work.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919705, 2, -9100, 10, 0, 434, 0, 0,
   919704, 919706, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "The Card Master",
   "Before you go, $N, there is one more person you should know about.$B$BAs your collection grows, you'll eventually find yourself with cards you no longer need... and others you simply cannot seem to find.$B$BThat's where Byron Bernstein comes in.$B$BThe Hearthstone Committee sent him out as a Card Master. If you've got cards you don't want anymore, Byron can turn them into something useful. And if there's a particular card you're chasing, he's the one you want to talk to.$B$BLast I heard, he was somewhere in the Cleft of Shadow.$B$BGo find him. Trust me, you'll want to know where he is.",
   "Find Byron Bernstein.",
   "Find Byron Bernstein",
   "Hey.$B$BYou looking for me?",
   "Ezra sent you?$B$BYeah, figured he'd start sending players my way eventually.$B$BI'm Byron.$B$BIf you've got cards you don't want, bring them to me. If there's one you really want and can't seem to find, I can help with that too.$B$BPretty simple.$B$BStick around for a minute and I'll show you how it works.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919706, 2, -9100, 10, 0, 434, 0, 2,
   919705, 0, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "The Card Master's Trade",
   "Alright, $N. Before you disappear, let me show you something.$B$BYou're going to open packs. You're going to get cards you already have, cards you don't want, and probably everything except the card you're actually looking for.$B$BThat's normal.$B$BThat's where I come in.",
   "Speak with Byron Bernstein and learn what he can do for you.",
   "Learn about the Card Master's services",
   "We're not done yet.$B$BCome on, ask me something.",
   "There you go.$B$BThat's really all there is to it.$B$BBring me cards you don't need. Save the dust. Get the cards you actually want.$B$BAnd don't disappear on me now.$B$BDon't wait until we're old to come back, alright?");

INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES (14377, 919701), (919501, 919702), (919503, 919703), (919501, 919704), (919501, 919705), (919505, 919706);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (919501, 919701), (919503, 919702), (919501, 919703), (919501, 919704), (919505, 919705), (919505, 919706);

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919711, 2, -9100, 10, 0, 589, 0, 0,
   0, 919712, 0, 0, 0, 0,
   10, 0, 0, 0, 0,
   "A Curious New Game",
   "Have you heard of Hearthstone?$B$BIt's a card game that's become rather popular around Stormwind lately. Guards play it between shifts, merchants trade cards, and I've heard more than a few arguments over matches that were supposed to be friendly.$B$BIf you'd like to see what all the excitement is about, there's a man named Ibelin Redmoore who teaches newcomers how to play.$B$BI heared that you can find him at the Pig and Whistle Tavern in Old Town.",
   "Find Hearthstone Master Ibelin Redmoore at the Pig and Whistle Tavern in Old Town.",
   "Find Hearthstone Master Ibelin Redmoore",
   "Looking for someone?",
   "Ah, I take it you've heard about Hearthstone.$B$BI'm Ibelin Redmoore. If you're curious about the game, you've come to the right place.$B$BHave a seat. There's always room for another player at the table. But oh, please, before we start, help me solve a mystery.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919712, 2, -9100, 10, 0, 589, 0, 0,
   0, 919713, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "Lessons Interrupted",
   "I appear to have lost a student.$B$BThomas Bell, one of the dockworkers, paid for a Hearthstone lesson earlier today. We had only been playing for a few minutes when his eyes suddenly went wide.$B$BThen he stood up and ran.$B$BNo explanation. He simply left his chair and hurried off as though the city were burning.$B$BI saw him heading toward the canal. Would you find him for me?$B$BI'd rather know whether something happened before I assume my teaching drove him away.",
   "Find Dockworker Thomas Bell and discover why he abandoned his Hearthstone lesson.",
   "Find Dockworker Thomas Bell",
   "Can't talk long, friend.$B$BI've got crates waiting, a shipment overdue, and I'm already behind.",
   "The Hearthstone lesson?$B$BOh, right.$B$BI knew I'd forgotten something.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919713, 2, -9100, 10, 0, 589, 0, 0,
   919712, 919714, 919605, 1, 919605, 1,
   500, 0, 0, 919610, 1,
   "No Time to Play",
   "Look, I didn't mean to run out on Ibelin.$B$BWe'd barely started when I remembered a shipment I was supposed to deliver hours ago. If the foreman finds out those crates are still sitting around, Hearthstone will be the least of my worries.$B$BI thought I'd have time for a lesson.$B$BTurns out I barely have time to eat.$B$BHere, take the cards. I bought the deck from Ibelin for the lesson, but they'll only gather dust with me.$B$BMaybe you can make better use of them.$B$BAnd tell Ibelin I'm sorry, would you?",
   "Take Thomas Bell's Hearthstone Cards back to Ibelin.",
   "Return Thomas Bell's Hearthstone Cards to Ibelin",
   "You're back, $N.$B$BDid you find Thomas?$B$BJudging by how quickly he left, I imagined something rather urgent had come up.",
   "A forgotten delivery.$B$BYes, that sounds like Thomas.$B$BI suppose Hearthstone has difficulty competing with an angry dockmaster.$B$BHe gave you the cards?$B$BWell, there's no reason for them to go unused.$B$BAnd since you've already gone to the trouble of bringing them back...$B$BPerhaps you'd like the lesson instead.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919714, 2, -9100, 10, 0, 589, 0, 2,
   919713, 919715, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "Your First Hand",
   "Hearthstone doesn't require much to begin, $N.$B$BA deck, an opponent, and enough time to think before you start running toward the canals.$B$BFortunately, you already have the first two.$B$BI'll teach you the basics. How to spend your mana, how to play your minions, and how to make your opponent regret sitting across from you.$B$BTake Thomas's deck and have a seat.$B$BLet's me explain the rules.",
   "Listen to Ibelin's Hearthstone lesson until he has told you everything you need to know.",
   "Complete Ibelin's Hearthstone lesson",
   "Ready for your lesson, $N?$B$BDon't worry. If you've forgotten an urgent delivery, now would be the time to remember it.",
   "Well played, $N.$B$BYou picked up the basics quickly.$B$BAnd, unlike Thomas, you managed to finish the entire lesson before remembering somewhere else you needed to be.$B$BYou've learned enough to begin, but Hearthstone is best learned across the table from different opponents.$B$BPlay often, try new cards, and don't be afraid to lose a few games along the way.$B$BThat's usually where the useful lessons are.");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919715, 2, -9100, 10, 0, 589, 0, 0,
   919714, 919716, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "The Card Master",
   "There's one more person you should meet before you set off on your own, $N.$B$BSooner or later, every Hearthstone player ends up with cards they don't need and somehow never gets the one card they're actually looking for.$B$BThat's where Byron Bernstein comes in.$B$BThe Hearthstone Committee sent him out as a Card Master. He can make use of unwanted cards, and if there's something specific missing from your collection, he may be able to help you get it.$B$BLast I heard, Byron was checking card shipments around Cathedral Square.$B$BGo introduce yourself. You'll probably be seeing him again.",
   "Find Byron Bernstein.",
   "Find Byron Bernstein",
   "Hey, give me a second.$B$BJust making sure nobody put another stack of cards in the wrong shipment.$B$B...again.",
   "Ibelin sent you?$B$BNice. Guess that means you made it through your first game.$B$BI'm Byron.$B$BI deal with the cards people don't want anymore, and the cards they really want but can't find.$B$BIt's easier if I just show you.$B$BGot a minute?");

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919716, 2, -9100, 10, 0, 589, 0, 2,
   919715, 0, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   "The Card Master's Trade",
   "Alright, $N. Before you disappear, let me show you something.$B$BYou're going to open packs. You're going to get cards you already have, cards you don't want, and probably everything except the card you're actually looking for.$B$BThat's normal.$B$BThat's where I come in.",
   "Speak with Byron Bernstein and learn what he can do for you.",
   "Learn about the Card Master's services",
   "We're not done yet.$B$BCome on, ask me something.",
   "There you go.$B$BThat's really all there is to it.$B$BBring me cards you don't need. Save the dust. Get the cards you actually want.$B$BAnd don't disappear on me now.$B$BDon't wait until we're old to come back, alright?");

INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES (6174, 919711), (919502, 919712), (919504, 919713), (919502, 919714), (919502, 919715), (919505, 919716);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (919502, 919711), (919504, 919712), (919502, 919713), (919502, 919714), (919505, 919715), (919505, 919716);
