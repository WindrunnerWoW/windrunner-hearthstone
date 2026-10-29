-- mod-hearthstone: the League Committee becomes neutral, the ladder breadcrumb quest (a DUMMY: placeholder
-- text, no reward, to be reviewed one quest at a time) and the ladder tutorial pages.
--   Amaz Chan (920100, Darnassus) and The Firebat (920101, Thunder Bluff) get faction 35 (friendly to everybody) instead of an
--   Alliance and a Horde faction, so both factions can talk to both of them. They also become quest givers (npc_flags 3) to end
--   the quest. Migration 0047 stays as it was (it may already be applied): this one changes the two rows afterwards.
--   Quest 919970 "A Seat at the League": no start relation (Ezra Chatterton 919501 and Ibelin Redmoore 919502 offer it through a
--   gossip option of the module, src/HsLadderBreadcrumb.cpp), ended by either committee NPC. No objectives, no reward. Turning it in
--   unlocks the ladder chairs for the character.
--   The tutorial is database gossip: the greeting menus 65270 / 65271 get a second option that opens the pages 65280 to 65285
--   (shared by both NPCs). Texts 9195940 to 9195945 approved 2026-10-15, including the planned portrait reward and new league.
--   $N is the player's name, $B a line break.
-- Rebuilt each time this file runs, only the ids named here are touched (and option 1 of the menus 65270 / 65271).
-- The file contains no backslashes and no semicolons inside comments or strings.

UPDATE `creature_template` SET `faction` = 35, `npc_flags` = 3 WHERE `entry` IN (920100, 920101);

DELETE FROM `quest_template` WHERE `entry` = 919970;
DELETE FROM `creature_questrelation` WHERE `quest` = 919970;
DELETE FROM `creature_involvedrelation` WHERE `quest` = 919970;

INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`, `RewItemId2`, `RewItemCount2`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919970, 2, -9100, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   "A Seat at the League",
   "There is a league that plays its games in Darnassus and in Thunder Bluff, $N, and players of both factions sit down at the same tables. The League Committee keeps its standings and sees to it that the games are fair. Now that you know how the game is played, they will want to meet you.",
   "Speak with Amaz Chan in Darnassus or with The Firebat in Thunder Bluff.",
   "",
   "Yes? The Committee is busy. State your business.",
   "So the old hands sent you. Good. The league has a place for you, $N, and the standings are open to anyone who can play.");

INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES (920100, 919970), (920101, 919970);

DELETE FROM `broadcast_text` WHERE `entry` IN (9195940, 9195941, 9195942, 9195943, 9195944, 9195945);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195940,
   "Everyone starts at Rank 15. We mark your progress with stars: three at each rank through Rank 11, then five from Rank 10 onward. Earn another star when your row is full and you advance.$B$BA victory earns a star. A defeat costs one, and losing with no stars left drops you a rank. You cannot fall below Rank 15.$B$BFrom Rank 15 through Rank 6, your third consecutive victory earns an extra star, as does each victory that follows. A defeat or draw ends that run. A draw costs no stars.",
   "Everyone starts at Rank 15. We mark your progress with stars: three at each rank through Rank 11, then five from Rank 10 onward. Earn another star when your row is full and you advance.$B$BA victory earns a star. A defeat costs one, and losing with no stars left drops you a rank. You cannot fall below Rank 15.$B$BFrom Rank 15 through Rank 6, your third consecutive victory earns an extra star, as does each victory that follows. A defeat or draw ends that run. A draw costs no stars."),
  (9195941,
   "That was the Committee's whole idea. Get the Horde and Alliance sitting across a table, talking instead of fighting. We tried letters, envoys, banquets... Cards finally got them into the same room. Now they argue about cards.$B$BThe tauren were reasonable enough to welcome visiting Hearthstone players to Thunder Bluff. The night elves agreed to the same arrangement in Darnassus, after rather more correspondence.$B$BBut if you're with the Horde, don't try to enter Darnassus without a Committee guard escorting you. The sentinels will kill you. Our invitation is no use if you don't live long enough to explain it.",
   "That was the Committee's whole idea. Get the Horde and Alliance sitting across a table, talking instead of fighting. We tried letters, envoys, banquets... Cards finally got them into the same room. Now they argue about cards.$B$BThe tauren were reasonable enough to welcome visiting Hearthstone players to Thunder Bluff. The night elves agreed to the same arrangement in Darnassus, after rather more correspondence.$B$BBut if you're with the Horde, don't try to enter Darnassus without a Committee guard escorting you. The sentinels will kill you. Our invitation is no use if you don't live long enough to explain it."),
  (9195942,
   "Take a seat at either league table and we'll offer you up to three opponents. Consider whom you'd like to face, choose your deck, and we'll arrange the game.$B$BYour opponent will join you at the table when it's time to begin. Bring your best cards, $N. Courtesy won't oblige anyone to let you win.",
   "Take a seat at either league table and we'll offer you up to three opponents. Consider whom you'd like to face, choose your deck, and we'll arrange the game.$B$BYour opponent will join you at the table when it's time to begin. Bring your best cards, $N. Courtesy won't oblige anyone to let you win."),
  (9195943,
   "We record the result, and the rest of the league plays its round as well. Keep an eye on the standings. The player you passed yesterday may be climbing past you today.$B$BWhen you're ready for another game, we'll offer you opponents again. You won't be the only one hoping for a better hand.",
   "We record the result, and the rest of the league plays its round as well. Keep an eye on the standings. The player you passed yesterday may be climbing past you today.$B$BWhen you're ready for another game, we'll offer you opponents again. You won't be the only one hoping for a better hand."),
  (9195944,
   "Not quite. Win with all five stars at Rank 1 and you contend for a place among the Legends. There are only ten places, and we judge their order by results and the strength of the opponents faced.$B$BWhen another contender qualifies, whoever stands lowest returns to Rank 1 with four stars. To become champion, you must take first place among the Legends themselves.",
   "Not quite. Win with all five stars at Rank 1 and you contend for a place among the Legends. There are only ten places, and we judge their order by results and the strength of the opponents faced.$B$BWhen another contender qualifies, whoever stands lowest returns to Rank 1 with four stars. To become champion, you must take first place among the Legends themselves."),
  (9195945,
   "Take first place among the Legends and the Committee will offer you a new hero portrait of your choice to use in your games. A distinction for your collection, and one you've earned.$B$BYou may then begin a fresh league and make the climb again, keeping the portrait you've won. Reach the top once more and you can choose another.$B$BThat covers the rules, $N. Speak with me once more before you take your seat.",
   "Take first place among the Legends and the Committee will offer you a new hero portrait of your choice to use in your games. A distinction for your collection, and one you've earned.$B$BYou may then begin a fresh league and make the climb again, keeping the portrait you've won. Reach the top once more and you can choose another.$B$BThat covers the rules, $N. Speak with me once more before you take your seat.");

DELETE FROM `npc_text` WHERE `ID` IN (9195940, 9195941, 9195942, 9195943, 9195944, 9195945);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195940, 9195940, 1), (9195941, 9195941, 1), (9195942, 9195942, 1), (9195943, 9195943, 1), (9195944, 9195944, 1), (9195945, 9195945, 1);

DELETE FROM `gossip_menu` WHERE `entry` IN (65280, 65281, 65282, 65283, 65284, 65285);
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES
  (65280, 9195940), (65281, 9195941), (65282, 9195942), (65283, 9195943), (65284, 9195944), (65285, 9195945);

-- option_id 1 / npc_option_npcflag 1: a plain gossip option that opens action_menu_id. Option 1 of the two greeting menus
-- starts the tutorial, the pages lead on one by one (the last page has no option).
DELETE FROM `gossip_menu_option` WHERE (`menu_id` IN (65270, 65271) AND `id` = 1) OR `menu_id` IN (65280, 65281, 65282, 65283, 65284, 65285);
INSERT INTO `gossip_menu_option`
  (`menu_id`, `id`, `option_icon`, `option_text`, `option_id`, `npc_option_npcflag`, `action_menu_id`)
VALUES
  (65270, 1, 0, 'How does the league work?', 1, 1, 65280),
  (65271, 1, 0, 'How does the league work?', 1, 1, 65280),
  (65280, 0, 0, 'You allow Horde and Alliance players?', 1, 1, 65281),
  (65281, 0, 0, 'How do I arrange a game?', 1, 1, 65282),
  (65282, 0, 0, 'What happens after the game?', 1, 1, 65283),
  (65283, 0, 0, 'So reaching Rank 1 makes me champion?', 1, 1, 65284),
  (65284, 0, 0, 'And what does the champion earn?', 1, 1, 65285);
