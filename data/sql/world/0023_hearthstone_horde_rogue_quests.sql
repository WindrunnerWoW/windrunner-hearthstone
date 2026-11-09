-- Horde Rogue: No Honor Among Cards. All five actors are manually placed by the owner.
-- No creature or gameobject spawns. Objective-only templates 919527-919531 must never be placed.
-- A quest never mixes two objectives in the same slot: the client then draws both lines from one packed
-- counter and repeats the item progress. Possession Is Nine-Tenths keeps one objective per slot:
-- objective 1 is the vendored deck (item), objective 2 is the report conversation (native speak-to credit).
-- The core's item push for these vendored items is dropped by HsTraderQuestCanSendPacket: the client
-- already draws the purchase line itself, so the push would print it twice.
-- For'sen 919522 and Gora 919523 are supplied by migration 0022.
UPDATE `creature_template` SET `gossip_menu_id`=65012 WHERE `entry`=919522;
UPDATE `creature_template` SET `gossip_menu_id`=65013 WHERE `entry`=919523;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 919524 AND 919531;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
SELECT c.entry, c.display_id, c.name, c.subname,
  b.level_min, b.level_max, b.health_min, b.health_max, b.armor, 85, c.flags, b.speed_walk, b.speed_run,
  b.detection_range, b.call_for_help_range, b.dmg_min, b.dmg_max, b.attack_power, b.base_attack_time,
  b.ranged_attack_time, b.unit_class, b.unit_flags, b.type, b.regeneration, 0, c.menu, ''
FROM `creature_template` b CROSS JOIN (
  SELECT 919524 AS entry, 10746 AS display_id, 'Rikka' AS name, 'Discreet trader' AS subname, 1 AS flags, 65014 AS menu
  UNION ALL SELECT 919525, 6060, 'Zikko', 'Mage and bookmaker', 1, 65015
  UNION ALL SELECT 919526, 2659, 'Sable', 'Card dealer', 5, 65016
) c WHERE b.entry=919501;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `level_min`, `level_max`, `health_min`, `health_max`,
   `faction`, `npc_flags`, `unit_flags`, `type`, `regeneration`, `movement_type`, `script_name`)
VALUES
  (919527, 4070, 'Question Rikka about the deck', 1, 1, 1, 1, 85, 0, 33554434, 7, 3, 0, ''),
  (919528, 4070, 'Question Gora about the deck', 1, 1, 1, 1, 85, 0, 33554434, 7, 3, 0, ''),
  (919529, 4070, 'Question Zikko about the deck', 1, 1, 1, 1, 85, 0, 33554434, 7, 3, 0, ''),
  (919530, 4070, 'Question Sable about the deck', 1, 1, 1, 1, 85, 0, 33554434, 7, 3, 0, ''),
  (919531, 4070, 'Confront For''sen about the theft', 1, 1, 1, 1, 85, 0, 33554434, 7, 3, 0, '');

DELETE FROM `gossip_menu` WHERE `entry` BETWEEN 65012 AND 65016;
-- After Possession Is Nine-Tenths is turned in, Sable greets the buyer with her trading (highest satisfied condition wins).
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`) VALUES (8, 919772, 0, 0, 0, 0);
INSERT INTO `gossip_menu` (`entry`, `text_id`, `condition_id`) VALUES
  (65012,9195180,0), (65013,9195181,0), (65014,9195182,0), (65015,9195183,0), (65016,9195184,0);
INSERT INTO `gossip_menu` (`entry`, `text_id`, `condition_id`)
SELECT 65016, 9195215, `condition_entry` FROM `conditions`
WHERE `type` = 8 AND `value1` = 919772 AND `value2` = 0 AND `value3` = 0 AND `value4` = 0 AND `flags` = 0;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195180 AND 9195184 OR `ID` BETWEEN 9195190 AND 9195215;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195180 AND 9195184 OR `entry` BETWEEN 9195190 AND 9195215;
INSERT INTO `broadcast_text` (`entry`, `male_text`) VALUES
  (9195180, 'Good cards. Bad company. Ya lookin'' for a lesson, or lookin'' for trouble, mon?'),
  (9195181, 'I deliver things. You need something delivered, without anyone knowing, let me know.'),
  (9195182, 'Discretion costs extra.'),
  (9195183, 'Feeling lucky? Everyone does, at first.'),
  (9195184, 'Three silver. For the case, the cards, and my remarkable restraint.'),
  (9195190, 'For''sen sent you? Yes, I had the deck.'),
  (9195191, 'He said it belonged to his cousin. Didn''t know trolls had that many cousins. Gora took it off my hands. She seemed eager to play a few rounds with it.'),
  (9195192, 'A handling fee. Gora''s your next conversation.'),
  (9195193, 'Small case. Dagger scratched on the lid. Yes, I carried it. No, I don''t have it.'),
  (9195194, 'Zikko said one quick wager. I could double my fee. Funny how the odds changed once my money was down.'),
  (9195195, 'Only after taking a card for my trouble. Don''t look at me like that. It was a very long walk.'),
  (9195196, 'I''m a mage, mon, not some cutpurse. Gora lost fair an'' square. Don''t let nobody tell ya otherwise. But I''m guessin'' ya here about dat little case with da crooked dagger, eh?'),
  (9195197, 'Sold it to Sable in da Drag, mon. She knew what it was before I even opened da lid. Paid quick, too.'),
  (9195198, 'A little magic keeps da dice agreeable, mon. Ya got ya next name. Don''t be pushin'' ya luck.'),
  (9195199, 'I trade cards. Buy them, sell them, sometimes win them back. That deck was mine before it was stolen. See the little nicks on the backs? Took me all evening to mark those.'),
  (9195200, 'I like to know what''s coming. Didn''t see the theft coming, though. One moment the case was on my table. Next moment, gone.'),
  (9195201, 'No. Whoever it was had better hands than manners. Bought it back from Zikko. But I''m a trader, not a collector. Three silver and the whole deck is yours. Tell your friend.'),
  (9195202, 'Ah, dere it is. Knew ya could find it, mon. Sable got her coin, and me got da deck. Good business.'),
  (9195203, 'Sable says plenty when she wants a sale, mon. Ya bought da cards, not her whole story.'),
  (9195204, 'Heh. Truth is, me lifted it off Sable''s table while she was busy markin'' da next card. Didn''t even look up.'),
  (9195205, 'Sable marks her cards. Rikka moves stolen goods. Gora helps herself to deliveries. Zikko bends da dice with magic. Everybody got a little somethin'', mon.'),
  (9195206, 'Me sent ya to follow da deck, and ya followed every hand it passed through. Ya kept askin'' questions until da story fell apart. Dat is a useful pair of eyes for a Rogue player, mon.'),
  (9195207, 'Da deck is back, and ya got da truth. One game now, mon. Beat me, and da deck is yours.'),
  (9195208, 'Gora does her business behind the waterfall. Go look for her there, and don''t waste any more of my time.'),
  (9195209, 'Zikko hangs around the Ring of Valor. Calls himself a mage, but he likes picking fights as much as taking bets. You''ll find him there.'),
  (9195210, 'Sable trades cards in da Drag, mon. Follow da coin and ya find da dealer.'),
  (9195211, 'Three silver for the deck. I deal in cards, not favors.'),
  (9195212, 'Ya found it? Where did it end up, mon?'),
  (9195213, 'Three silver? Everybody found a way to take a little off da top. At least we know where da deck is. Dat is da whole trail, yeah?'),
  (9195215, 'The deck was a one-time bargain. I keep a few special cards in limited stock, and the selection changes every day. Take a look.'),
  (9195214, 'Good work, mon. Ya found da cards, and ya brought me da price. Now we can talk about gettin'' dem back.');
UPDATE `broadcast_text` SET `female_text`=`male_text`
WHERE `entry` BETWEEN 9195180 AND 9195184 OR `entry` BETWEEN 9195190 AND 9195215;
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`)
SELECT `entry`, `entry`, 1 FROM `broadcast_text`
WHERE `entry` BETWEEN 9195180 AND 9195184 OR `entry` BETWEEN 9195190 AND 9195215;

DELETE FROM `item_template` WHERE `entry` IN (919636,919637,919648);
INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`,
   `buy_count`, `buy_price`, `sell_price`, `max_count`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
VALUES (919636,12,0,'Recovered Card Deck','A well-traveled case with a crooked dagger scratched into its lid.',
  69179,1,1,300,0,1,1,4,1,0,''),
  (919637,12,0,'Report to For''sen','For''sen has heard where the deck ended up and what Sable wants for it.',
  69179,1,1,0,0,1,1,4,1,0,''),
-- Legacy objective item from the first recovery build. It is no longer a quest requirement; the row is
-- kept so copies left in bags keep a prototype, and HsHordeRogueAfterQuestChange destroys them.
  (919648,12,0,'Confront For''sen about the theft','You know who took the deck, and so does For''sen.',
  69179,1,1,0,0,1,1,4,1,0,'');

DELETE FROM `quest_template` WHERE `entry` IN (919771,919772,919773);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919771,919772,919773);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919771,919772,919773);
INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`,
   `ReqCreatureOrGOId1`, `ReqCreatureOrGOCount1`, `ReqCreatureOrGOId2`, `ReqCreatureOrGOCount2`,
   `ReqCreatureOrGOId3`, `ReqCreatureOrGOCount3`, `ReqCreatureOrGOId4`, `ReqCreatureOrGOCount4`,
   `ReqItemId1`, `ReqItemCount1`, `ReqItemId2`, `ReqItemCount2`, `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `ObjectiveText2`, `ObjectiveText3`, `ObjectiveText4`,
   `RequestItemsText`, `OfferRewardText`)
VALUES
  (919771,2,-9100,10,0,434,0,0,919704,919772,
   919527,1,919528,1,919529,1,919530,1,919637,1,0,0,500,0,0,0,0,
   'A Deck Goes Missing',
   'Me deck''s gone missin''. Good cards, too. Got it from me cousin, and I want it back before somebody starts askin'' da wrong questions.$B$BRikka knows plenty about things changin'' hands without much fuss. She does her business below da bridge in da Valley of Strength. Me can''t ask her meself, mon. Too many people know me face. Dat is where ya come in.$B$BHelp me out, and we talk about lessons. Keep this between us, yeah?',
   'Talk to Rikka about the missing deck.',
   'Talk to Rikka','Talk to Gora','Talk to Zikko','Talk to Sable',
   'Find da deck, mon. Quietly. Me face is too well known for dis.',
   'So Sable''s got da deck. Figures, mon. Everybody on da trail found a way to make a little coin.$B$BMe still want da cards. Go and buy dem from her.'),
  (919772,2,-9100,10,0,434,0,0,919771,919773,
   0,0,919531,1,0,0,0,0,919636,1,0,0,500,0,0,0,0,
   'Possession Is Nine-Tenths',
   'Sable is a card trader, mon. If da price is right, she sells, and da price is a steal. Lil'' joke.$B$BGo back to her in da Drag, buy da cards, and bring dem here. Me want dat deck back.$B$BThen we sit down for a proper talk. Ya got questions, and me got answers. Some of dem even true.',
   'Buy the Recovered Card Deck and return it to For''sen.',
   '','Confront For''sen about the theft','','',
   'Ya got da deck, mon? Bring it here, and we talk. Me can see da questions on ya face.',
   'Da cards are back, and now ya know how dey went missin''. Ya followed da whole trail, mon.$B$BOne more thing before we are done...'),
  (919773,2,-9100,10,0,434,0,2,919772,0,
   0,0,0,0,0,0,0,0,0,0,0,0,500,0,0,919623,1,
   'No Honor Among Cards',
   'Ya know da story now. Everybody on da trail took a little somethin'', and me took da most. Now read da table, mon.$B$BMe use Rogue. Beat me, and we call it even. More or less.',
   'Defeat For''sen in a Hearthstone duel.',
   'Defeat For''sen at Hearthstone','','','',
   'One game, mon. Or another, if ya need it. Come when ya ready.',
   'Ya kept track of da whole mess and still found a way to win. Take dis. It belongs to ya, mon. For real, dis time.$B$BMe cousin sends his regards. Me cousin does not exist.$B$BSee ya around.');
INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
  (919522,919771),(919522,919772),(919522,919773);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
  (919522,919771),(919522,919772),(919522,919773);

-- Quest-only vendor sale while recovery is incomplete (per character). The daily card is a gossip
-- option added by the module after the quest is rewarded. Packet/greeting checks enforce the vendor
-- gate for GMs and direct vendor requests as well.
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
VALUES (9,919772,1,0,0,0);
DELETE FROM `gossip_menu_option` WHERE `menu_id`=65016;
INSERT INTO `gossip_menu_option`
  (`menu_id`, `id`, `option_icon`, `option_text`, `option_id`, `npc_option_npcflag`, `condition_id`)
SELECT 65016,0,1,'I want to buy the deck.',3,4,`condition_entry` FROM `conditions`
WHERE `type`=9 AND `value1`=919772 AND `value2`=1 AND `value3`=0 AND `value4`=0 AND `flags`=0;
DELETE FROM `npc_vendor` WHERE `entry`=919526 AND `item`=919636;
INSERT INTO `npc_vendor` (`entry`, `item`, `maxcount`, `incrtime`, `condition_id`)
SELECT 919526,919636,0,0,`condition_entry` FROM `conditions`
WHERE `type`=9 AND `value1`=919772 AND `value2`=1 AND `value3`=0 AND `value4`=0 AND `flags`=0;

-- Shared quest-only Normal duel, then account Horde random roster on final turn-in.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919522;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919522;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`, `quest_id`, `deck_key`, `faction`)
VALUES (919522,919773,'normal_rogue','horde');
