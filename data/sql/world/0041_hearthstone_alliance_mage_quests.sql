-- Alliance Mage: Mitchell Jones in Darnassus; three owner-placed speaking images.
-- Existing locations/models only. Owner-placed actors are recorded below; never place credits 919600-919603.
-- Quiz acknowledgement saves a visible native 0/10 counter; sightseeing has three native 0/1 counters.
DELETE FROM `creature_template` WHERE `entry` IN (919596,919597,919598,919599,919600,919601,919602,919603);
INSERT INTO `creature_template`
 (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
  `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,
  `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,
  `base_attack_time`,`ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,
  `movement_type`,`gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,1484,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
 b.`mana_min`,b.`mana_max`,b.`armor`,79,c.`flags`,b.`speed_walk`,b.`speed_run`,
 b.`detection_range`,b.`call_for_help_range`,IF(c.`flags`=0,0,b.`dmg_min`),IF(c.`flags`=0,0,b.`dmg_max`),
 IF(c.`flags`=0,0,b.`attack_power`),b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
 IF(c.`flags`=0,33554432,b.`unit_flags`),b.`type`,b.`regeneration`,0,c.`menu`,c.`auras`,0,''
FROM `creature_template` b CROSS JOIN (
 SELECT 919596 AS `entry`,'Mitchell Jones' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65230 AS `menu`,'' AS `auras`
 UNION ALL SELECT 919597,'Mitchell Jones','Speaking image',1,65247,'9617'
 UNION ALL SELECT 919598,'Mitchell Jones','Speaking image',1,65248,'9617'
 UNION ALL SELECT 919599,'Mitchell Jones','Speaking image',1,65249,'9617'
 UNION ALL SELECT 919600,'HS Mage Quiz Answer','',0,0,''
 UNION ALL SELECT 919601,'HS Mage Warrior Terrace','',0,0,''
 UNION ALL SELECT 919602,'HS Mage Craftsmen Terrace','',0,0,''
 UNION ALL SELECT 919603,'HS Mage Temple Conversation','',0,0,''
) c WHERE b.`entry`=919502;

DELETE FROM `gossip_menu` WHERE `entry` IN (65230,65231,65232,65233,65234,65235,65236,65237,65238,65239,65240,65241,65242,65243,65244,65247,65248,65249,65250,65251,65252);
DELETE FROM `npc_text` WHERE `ID` IN (9195600,9195601,9195602,9195603,9195604,9195605,9195606,9195607,9195608,9195609,9195610,9195611,9195612,9195613,9195614,9195620,9195621,9195622,9195623,9195624,9195625,9195626,9195627,9195628,9195629,9195630,9195631,9195632,9195633,9195634,9195635,9195636,9195637,9195638,9195639,9195640,9195641,9195642,9195643,9195644,9195645,9195646,9195647,9195648,9195649,9195650,9195651,9195652,9195653,9195654,9195655,9195656,9195657,9195658,9195659,9195660,9195661,9195662,9195663,9195664);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195600,9195601,9195602,9195603,9195604,9195605,9195606,9195607,9195608,9195609,9195610,9195611,9195612,9195613,9195614,9195620,9195621,9195622,9195623,9195624,9195625,9195626,9195627,9195628,9195629,9195630,9195631,9195632,9195633,9195634,9195635,9195636,9195637,9195638,9195639,9195640,9195641,9195642,9195643,9195644,9195645,9195646,9195647,9195648,9195649,9195650,9195651,9195652,9195653,9195654,9195655,9195656,9195657,9195658,9195659,9195660,9195661,9195662,9195663,9195664);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
 (9195600,'Mitchell Jones. My father calls this historical research. I call it living the arcane dream: seeing the world and finding out which parts the books missed.','Mitchell Jones. My father calls this historical research. I call it living the arcane dream: seeing the world and finding out which parts the books missed.'),
 (9195601,'Have you traveled far? My father copies histories in Stormwind. I send him accounts from the road, and he sends back questions in the margins.','Have you traveled far? My father copies histories in Stormwind. I send him accounts from the road, and he sends back questions in the margins.'),
 (9195602,'Shall we begin? An honest guess is quite welcome.','Shall we begin? An honest guess is quite welcome.'),
 (9195603,'There you are. We can carry on where we left off.','There you are. We can carry on where we left off.'),
 (9195604,'Enough questions. Names are useful, but they do not tell you everything about a place. Let us put a few sights beside them.','Enough questions. Names are useful, but they do not tell you everything about a place. Let us put a few sights beside them.'),
 (9195605,'That is all ten. Thank you for staying with me. There are a few places I would like you to see next.','That is all ten. Thank you for staying with me. There are a few places I would like you to see next.'),
 (9195606,'We have the names and dates. Now I would like to show you what my father keeps asking about: the city between the battles.','We have the names and dates. Now I would like to show you what my father keeps asking about: the city between the battles.'),
 (9195607,'I am still at the three places I mentioned. Hear what each has to say, then find me here by the bank.','I am still at the three places I mentioned. Hear what each has to say, then find me here by the bank.'),
 (9195608,'You have found me in all three places. Good. I have seen more of Darnassus today than in all the weeks before, and I have not worn out a single boot.','You have found me in all three places. Good. I have seen more of Darnassus today than in all the weeks before, and I have not worn out a single boot.'),
 (9195609,'That gives me something worth writing home. There is time for a game before I put pen to paper.','That gives me something worth writing home. There is time for a game before I put pen to paper.'),
 (9195610,'No history questions this time. I would like to see what you do when the answer has to be played.','No history questions this time. I would like to see what you do when the answer has to be played.'),
 (9195611,'There is room for another game. Shall we try again?','There is room for another game. Shall we try again?'),
 (9195612,'A fine game. I have your Mage Deck ready, and a correction to make in my account of my own skill.','A fine game. I have your Mage Deck ready, and a correction to make in my account of my own skill.'),
 (9195613,'Good to see you, $n. My letter has become rather long. I have decided to blame the city. It has far too much to notice.','Good to see you, $n. My letter has become rather long. I have decided to blame the city. It has far too much to notice.'),
 (9195614,'The next place that makes me put the book down. I have a long list. My father has a longer one.','The next place that makes me put the book down. I have a long list. My father has a longer one.'),
 (9195620,'Before the Dark Portal opened, which world was the orcs'' home?','Before the Dark Portal opened, which world was the orcs'' home?'),
 (9195621,'That''s right. The orcs came from Draenor. The Dark Portal brought their invasion to Azeroth.','That''s right. The orcs came from Draenor. The Dark Portal brought their invasion to Azeroth.'),
 (9195622,'Not quite. The orcs came from Draenor. The Dark Portal brought their invasion to Azeroth.','Not quite. The orcs came from Draenor. The Dark Portal brought their invasion to Azeroth.'),
 (9195623,'Now, a question about my profession. Which human mage worked with Gul''dan to open the Dark Portal?','Now, a question about my profession. Which human mage worked with Gul''dan to open the Dark Portal?'),
 (9195624,'That''s right. Medivh. He was possessed by Sargeras. It is an uncomfortable chapter in my profession''s history.','That''s right. Medivh. He was possessed by Sargeras. It is an uncomfortable chapter in my profession''s history.'),
 (9195625,'Not quite. Medivh. He was possessed by Sargeras. It is an uncomfortable chapter in my profession''s history.','Not quite. Medivh. He was possessed by Sargeras. It is an uncomfortable chapter in my profession''s history.'),
 (9195626,'Let us stay with the First War. Which human city fell to the Horde at its end?','Let us stay with the First War. Which human city fell to the Horde at its end?'),
 (9195627,'That''s right. Stormwind. Much of what you see there today was built after that defeat.','That''s right. Stormwind. Much of what you see there today was built after that defeat.'),
 (9195628,'Not quite. Stormwind. Much of what you see there today was built after that defeat.','Not quite. Stormwind. Much of what you see there today was built after that defeat.'),
 (9195629,'A later war, then. Before Arthas became a death knight, which kingdom was he prince of?','A later war, then. Before Arthas became a death knight, which kingdom was he prince of?'),
 (9195630,'That''s right. Lordaeron. He once set out to protect its people from the Scourge.','That''s right. Lordaeron. He once set out to protect its people from the Scourge.'),
 (9195631,'Not quite. Lordaeron. He once set out to protect its people from the Scourge.','Not quite. Lordaeron. He once set out to protect its people from the Scourge.'),
 (9195632,'Another journey west. Which mage led human survivors to Kalimdor during the Third War?','Another journey west. Which mage led human survivors to Kalimdor during the Third War?'),
 (9195633,'That''s right. Jaina Proudmoore. She later founded Theramore.','That''s right. Jaina Proudmoore. She later founded Theramore.'),
 (9195634,'Not quite. Jaina Proudmoore. She later founded Theramore.','Not quite. Jaina Proudmoore. She later founded Theramore.'),
 (9195635,'That brings us to the stand in Kalimdor. Where did the allies make their final stand against Archimonde in the Third War?','That brings us to the stand in Kalimdor. Where did the allies make their final stand against Archimonde in the Third War?'),
 (9195636,'That''s right. Mount Hyjal. Humans, orcs, and night elves held the line while Malfurion prepared the blow that destroyed him.','That''s right. Mount Hyjal. Humans, orcs, and night elves held the line while Malfurion prepared the blow that destroyed him.'),
 (9195637,'Not quite. Mount Hyjal. Humans, orcs, and night elves held the line while Malfurion prepared the blow that destroyed him.','Not quite. Mount Hyjal. Humans, orcs, and night elves held the line while Malfurion prepared the blow that destroyed him.'),
 (9195638,'What about Gnomeregan? What creatures invaded it from below?','What about Gnomeregan? What creatures invaded it from below?'),
 (9195639,'That''s right. Troggs. The gnomes'' attempt to stop them left their own city poisoned.','That''s right. Troggs. The gnomes'' attempt to stop them left their own city poisoned.'),
 (9195640,'Not quite. Troggs. The gnomes'' attempt to stop them left their own city poisoned.','Not quite. Troggs. The gnomes'' attempt to stop them left their own city poisoned.'),
 (9195641,'Now for Lordaeron again. What name did Sylvanas''s free-willed undead take?','Now for Lordaeron again. What name did Sylvanas''s free-willed undead take?'),
 (9195642,'That''s right. The Forsaken. They broke from the Scourge and claimed the ruins of Lordaeron.','That''s right. The Forsaken. They broke from the Scourge and claimed the ruins of Lordaeron.'),
 (9195643,'Not quite. The Forsaken. They broke from the Scourge and claimed the ruins of Lordaeron.','Not quite. The Forsaken. They broke from the Scourge and claimed the ruins of Lordaeron.'),
 (9195644,'Now for the city around us. Who is honored in Darnassus''s Temple of the Moon?','Now for the city around us. Who is honored in Darnassus''s Temple of the Moon?'),
 (9195645,'That''s right. Elune, the moon goddess. Tyrande Whisperwind is her High Priestess.','That''s right. Elune, the moon goddess. Tyrande Whisperwind is her High Priestess.'),
 (9195646,'Not quite. Elune, the moon goddess. Tyrande Whisperwind is her High Priestess.','Not quite. Elune, the moon goddess. Tyrande Whisperwind is her High Priestess.'),
 (9195647,'One last question. What is the name of the enormous tree beneath this city?','One last question. What is the name of the enormous tree beneath this city?'),
 (9195648,'That''s right. Teldrassil. Darnassus rests among its boughs. A rather ambitious foundation.','That''s right. Teldrassil. Darnassus rests among its boughs. A rather ambitious foundation.'),
 (9195649,'Not quite. Teldrassil. Darnassus rests among its boughs. A rather ambitious foundation.','Not quite. Teldrassil. Darnassus rests among its boughs. A rather ambitious foundation.'),
 (9195650,'There you are. The arcane dream has its advantages. This is Warrior''s Terrace, the eastern edge of Darnassus and its main gate. Everything that enters the city passes through here first.','There you are. The arcane dream has its advantages. This is Warrior''s Terrace, the eastern edge of Darnassus and its main gate. Everything that enters the city passes through here first.'),
 (9195651,'The Sentinels. They stood with Tyrande through the Third War and held the line at Hyjal. General Shandris Feathermoon leads them now, and they guard the roads of Teldrassil as well as the gate. Their huntresses ride nightsabers, which I have been advised not to ask about.','The Sentinels. They stood with Tyrande through the Third War and held the line at Hyjal. General Shandris Feathermoon leads them now, and they guard the roads of Teldrassil as well as the gate. Their huntresses ride nightsabers, which I have been advised not to ask about.'),
 (9195652,'Quieter than I expected. I pictured drums and banners. Instead there are training dummies, long stretches of nothing, and a good many people who look as though they would rather not be thanked. I suspect that is what a watch is for.','Quieter than I expected. I pictured drums and banners. Instead there are training dummies, long stretches of nothing, and a good many people who look as though they would rather not be thanked. I suspect that is what a watch is for.'),
 (9195653,'Good. I am enjoying this more than I expected, and I have not even had to walk.','Good. I am enjoying this more than I expected, and I have not even had to walk.'),
 (9195654,'The watch has its place in the account. I am still at the other two places, if you have not been already.','The watch has its place in the account. I am still at the other two places, if you have not been already.'),
 (9195655,'Craftsmen''s Terrace. Alchemists, tailors, leatherworkers, enchanters, cooks. Nearly every trade in Darnassus lives in this one district, which is practical, and means a failed potion is never far from a nice tapestry.','Craftsmen''s Terrace. Alchemists, tailors, leatherworkers, enchanters, cooks. Nearly every trade in Darnassus lives in this one district, which is practical, and means a failed potion is never far from a nice tapestry.'),
 (9195656,'It is calm because of where it stands. After Hyjal, the druids grew Teldrassil to give the night elves a new World Tree. Nozdormu refused to bless it, so the immortality they hoped for never came. Darnassus is built in the boughs. None of this was quarried.','It is calm because of where it stands. After Hyjal, the druids grew Teldrassil to give the night elves a new World Tree. Nozdormu refused to bless it, so the immortality they hoped for never came. Darnassus is built in the boughs. None of this was quarried.'),
 (9195657,'As far as I can tell. Think what that means for an apprentice. Someone teaches them to shape a board in a city where the wood was a tree before it was a floor. I would like to know how they decide where to cut. I plan to ask politely, and from a distance.','As far as I can tell. Think what that means for an apprentice. Someone teaches them to shape a board in a city where the wood was a tree before it was a floor. I would like to know how they decide where to cut. I plan to ask politely, and from a distance.'),
 (9195658,'I will. A description of the world ought to leave room for the people doing the work.','I will. A description of the world ought to leave room for the people doing the work.'),
 (9195659,'The people doing the work have their place in the letter. I am still at the other two places, if you have not been already.','The people doing the work have their place in the letter. I am still at the other two places, if you have not been already.'),
 (9195660,'Here we are. The Temple of the Moon, Elune''s temple and the seat of Tyrande Whisperwind. It was built after the Battle of Mount Hyjal, if you remember my questions. I have been keeping my voice down.','Here we are. The Temple of the Moon, Elune''s temple and the seat of Tyrande Whisperwind. It was built after the Battle of Mount Hyjal, if you remember my questions. I have been keeping my voice down.'),
 (9195661,'A statue standing in the water. The books disagree on whom it honors. I am told the Sisters of Elune train in these halls, and that Tyrande comes here when she needs quiet.','A statue standing in the water. The books disagree on whom it honors. I am told the Sisters of Elune train in these halls, and that Tyrande comes here when she needs quiet.'),
 (9195662,'I wanted to stand here and understand it. Mostly I feel I ought to lower my voice. I can tell you whose temple this is. I cannot claim I understand every person who comes here. Some parts of a record should begin with a question.','I wanted to stand here and understand it. Mostly I feel I ought to lower my voice. I can tell you whose temple this is. I cannot claim I understand every person who comes here. Some parts of a record should begin with a question.'),
 (9195663,'I hope so. I would rather learn this place quietly than shout it into a letter.','I hope so. I would rather learn this place quietly than shout it into a letter.'),
 (9195664,'This part of the letter can wait until I understand more.','This part of the letter can wait until I understand more.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` IN (9195600,9195601,9195602,9195603,9195604,9195605,9195606,9195607,9195608,9195609,9195610,9195611,9195612,9195613,9195614,9195620,9195621,9195622,9195623,9195624,9195625,9195626,9195627,9195628,9195629,9195630,9195631,9195632,9195633,9195634,9195635,9195636,9195637,9195638,9195639,9195640,9195641,9195642,9195643,9195644,9195645,9195646,9195647,9195648,9195649,9195650,9195651,9195652,9195653,9195654,9195655,9195656,9195657,9195658,9195659,9195660,9195661,9195662,9195663,9195664);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
 (65230,9195600),
 (65231,9195601),
 (65232,9195602),
 (65233,9195603),
 (65234,9195604),
 (65235,9195605),
 (65236,9195606),
 (65237,9195607),
 (65238,9195608),
 (65239,9195609),
 (65240,9195610),
 (65241,9195611),
 (65242,9195612),
 (65243,9195613),
 (65244,9195614),
 (65247,9195650),
 (65248,9195655),
 (65249,9195660),
 (65250,9195654),
 (65251,9195659),
 (65252,9195664);

DELETE FROM `quest_template` WHERE `entry` IN (919891,919892,919893);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919891,919892,919893);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919891,919892,919893);
INSERT INTO `quest_template`
 (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
  `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,
  `ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,
  `ReqCreatureOrGOId4`,`ReqCreatureOrGOCount4`,`RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
  `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
  `RequestItemsText`,`OfferRewardText`)
VALUES
 (919891,2,-9100,10,0,589,0,0,0,919714,919892,919600,10,0,0,0,0,0,0,500,0,0,0,0,
  'A Traveler''s Memory',
  'My father copies histories in Stormwind. He taught me the names of distant cities long before I saw one for myself, and now he asks me for letters from the road. Every letter comes back with questions written in the margin.$B$BThe latest asks which stories travelers actually carry with them, and which stay behind in the footnotes. Ten questions, and nothing to win. I''d like to hear what has reached you.',
  'Answer Mitchell Jones''s ten lore questions.',
  'Answer Mitchell''s lore questions',
  '',
  '',
  '',
  'We can leave the remaining questions for a moment. I have no bell to ring when you take too long.',
  'Thank you. It is good to hear which stories have traveled farther than I have.$B$BNow let us put a few sights beside those names. My father will be pleased to receive a letter with something other than another war in it.'),
 (919892,2,-9100,10,0,589,0,0,0,919891,919893,919601,1,919602,1,919603,1,0,0,500,0,0,0,0,
  'Beyond the Footnotes',
  'I have read about Darnassus for years, and I would like to see how much of it I got right. Books can tell you what a place was built for. They are less helpful about what it is like to stand there.$B$BI will see you in three places: Warrior''s Terrace, Craftsmen''s Terrace, and the Temple of the Moon. Not in person, I am afraid. I do not care for walking, so I have arranged to be in several places at once. Do not be alarmed. It is really me, just not all of me.$B$BVisit them in whichever order suits you, then come back here to the bank.',
  'Speak with Mitchell at Warrior''s Terrace, at Craftsmen''s Terrace and at the Temple of the Moon, then return to Mitchell Jones.',
  'Discuss Warrior''s Terrace',
  'Discuss Craftsmen''s Terrace',
  'Discuss the Temple',
  '',
  'I am still at the three places I mentioned. Hear what each has to say, then find me here by the bank.',
  'A gate, a workshop and a temple. I know all three better than the books taught me.$B$BThank you for looking with me. My father will have something worth picturing this time.'),
 (919893,2,-9100,10,0,589,0,0,2,919892,0,0,0,0,0,0,0,0,0,500,0,0,919626,1,
  'A Question of Timing',
  'You have been very patient with my tour, $n, and I did not even have to walk. That is the arcane dream.$B$BIt earns you a game before I write home. A mage''s whole art is timing. The power is all there. The trick is to play the right card at the right moment, and I have been known to get excited and spend it too soon. My father calls this a character flaw.$B$BDefeat me, and this Mage Deck is yours.',
  'Defeat Mitchell Jones in a game of Hearthstone.',
  'Defeat Mitchell at Hearthstone',
  '',
  '',
  '',
  'The cards are ready whenever you are. I am quite willing to be corrected again.',
  'Well played, $n. Take this Mage Deck. I hope you keep better time with it than I do.$B$BMy father will ask whether I won. I suppose an honest account ought to include that too.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919596,919891),(919596,919892),(919596,919893);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919596,919891),(919596,919892),(919596,919893);

-- Shared generated policy owns the quest-only Basic Mage / Normal match.
-- Only the permanent teacher joins the account Alliance finder roster on final turn-in.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919596;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919596;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919596,919893,'normal_mage','alliance');

-- Owner requested recording all four placed Alliance Mage actors, 2026-10-09.
-- Captured read-only from tw_world:3307 with coordinates/facing read as DOUBLE.
-- Actor 919596: original local GUID 12585265.
-- Actor 919597: original local GUID 12585267.
-- Actor 919598: original local GUID 12585268.
-- Actor 919599: original local GUID 12585266.
-- GUIDs above are provenance only; each database allocates its own GUIDs.
-- Preserve every non-GUID spawn field and full stored precision. No attached
-- addons, GUID/entry waypoints, groups, links, events or pools were found.
-- Reruns replace only the teacher and three images. Unrelated spawns and
-- unspawned bookkeeping entries 919600-919603 remain untouched. Export only.
DELETE FROM `creature` WHERE `id` IN (919596,919597,919598,919599);
INSERT INTO `creature`
  (`id`,`id2`,`id3`,`id4`,`map`,
   `position_x`,`position_y`,`position_z`,`orientation`,
   `spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,
   `health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
  (919596,0,0,0,1,9915.990234375,2530.860107421875,1316.9000244140625,6.067140102386475,
   120,120,0,100,100,0,0,0),
  (919597,0,0,0,1,9952.5400390625,2244.469970703125,1334.06005859375,1.5707299709320068,
   120,120,0,100,100,0,0,0),
  (919598,0,0,0,1,10186.2998046875,2302.800048828125,1328.3399658203125,3.2554099559783936,
   120,120,0,100,100,0,0,0),
  (919599,0,0,0,1,9676.3701171875,2519.330078125,1334.77001953125,2.976599931716919,
   120,120,0,100,100,0,0,0);
