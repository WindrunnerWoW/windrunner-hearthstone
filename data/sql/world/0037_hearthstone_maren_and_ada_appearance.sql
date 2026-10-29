-- Owner-selected models, Ada's gnome identity, Keep directions and case icon.
-- Apply after 0033 and 0036 on existing installations. Safe to repeat.
-- Updates only presentation fields; preserves placements and native quest progress.
UPDATE `creature_template` SET `display_id1`=5092 WHERE `entry`=919561;
UPDATE `creature_template` SET `display_id1`=6975,`name`='Ada Sparkplugz' WHERE `entry`=919577;

-- Silver Contact 4404 uses ItemDisplayInfo 7404 in the local game data.
UPDATE `item_template` SET `display_id`=7404,
  `description`='A worn card case with "Ada Sparkplugz" written beneath the lid.'
WHERE `entry`=919650;

UPDATE `broadcast_text`
SET `male_text`='Ada came through here earlier. If I heard her correctly, she was on her way to Stormwind Keep. The name inside the lid is hers; I''d rather those cards went home than became part of my collection.',
    `female_text`='Ada came through here earlier. If I heard her correctly, she was on her way to Stormwind Keep. The name inside the lid is hers; I''d rather those cards went home than became part of my collection.'
WHERE `entry`=9195370;
UPDATE `broadcast_text`
SET `male_text`='If you spot a battered card case, please let me know. My name is inside the lid. I''ve checked every pocket twice; I was about to start numbering them.',
    `female_text`='If you spot a battered card case, please let me know. My name is inside the lid. I''ve checked every pocket twice; I was about to start numbering them.'
WHERE `entry`=9195389;
UPDATE `broadcast_text`
SET `male_text`='Thank you again. Next time, my own cards are going on the delivery list.',
    `female_text`='Thank you again. Next time, my own cards are going on the delivery list.'
WHERE `entry`=9195390;

UPDATE `quest_template`
SET `Details`='I found this card case beside my chair. It belongs to Ada Sparkplugz, a gnome courier who came through here earlier. If I heard her correctly, she was on her way to Stormwind Keep.$B$BWould you take it back to her? She''s spent months saving for those cards. Losing a game is one thing. Losing the only deck you own is rather harder.',
    `Objectives`='Return Ada Sparkplugz''s card case.',
    `RequestItemsText`='Is that my case? I''ve checked every pocket, even the little one I keep spare washers in.',
    `OfferRewardText`='Oh, thank the Light, everything''s here! I was already calculating how many deliveries it would take to replace them.$B$BThank you. And thank Sir Rowan for me. If he offers you a game, don''t be fooled by his manners. I have never seen anyone apologize so sincerely while winning.'
WHERE `entry`=919851;
