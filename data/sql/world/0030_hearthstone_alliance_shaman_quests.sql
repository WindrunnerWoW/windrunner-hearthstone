-- Alliance Shaman: native quests, quest-gated vendor purchases and an uninterrupted walking scene.
-- John is manually placed. Visitors and the chair are temporary runtime summons.
-- Never add spawns, delete owner paths, or replace either merchant's existing stock here.
UPDATE
    `creature_template`
SET
    `gossip_menu_id`=65027
WHERE
    `entry`=919545;
DELETE
FROM
    `creature_template`
WHERE
    `entry` BETWEEN 919548 AND 919551;
INSERT INTO `creature_template`
    (
        `entry`        ,
        `display_id1`  ,
        `name`         ,
        `level_min`    ,
        `level_max`    ,
        `health_min`   ,
        `health_max`   ,
        `faction`      ,
        `npc_flags`    ,
        `unit_flags`   ,
        `type`         ,
        `regeneration` ,
        `movement_type`,
        `script_name`
    )
VALUES
    (
        919548,10184                    ,
        'HS Shaman Earth Totem Reviewed',
        1,1                             ,
        1,1                             ,
        55,0                            ,
        33554432,7                      ,
        3,0                             ,
        ''
    )
    ,
    (
        919549,10184            ,
        'HS Shaman Kibler Heard',
        1,1                     ,
        1,1                     ,
        55,0                    ,
        33554432,7              ,
        3,0                     ,
        ''
    )
    ,
    (
        919550,10184                    ,
        'HS Shaman Water Totem Reviewed',
        1,1                             ,
        1,1                             ,
        55,0                            ,
        33554432,7                      ,
        3,0                             ,
        ''
    )
    ,
    (
        919551,10184                             ,
        'HS Shaman Ambassador Heard and Returned',
        1,1                                      ,
        1,1                                      ,
        55,0                                     ,
        33554432,7                               ,
        3,0                                      ,
        ''
    )
;
DELETE
FROM
    `item_template`
WHERE
    `entry`=919645;
INSERT INTO `item_template`
    (
        `entry`          ,
        `class`          ,
        `subclass`       ,
        `name`           ,
        `description`    ,
        `display_id`     ,
        `quality`        ,
        `buy_count`      ,
        `buy_price`      ,
        `sell_price`     ,
        `inventory_type` ,
        `allowable_class`,
        `allowable_race` ,
        `item_level`     ,
        `required_level` ,
        `max_count`      ,
        `stackable`      ,
        `bonding`        ,
        `material`       ,
        `script_name`
    )
SELECT
    919645,12
    ,
    0
    ,
    'Ikae "Earth Totem Kit"'
    ,
    'A flat-packed chair with a simple assembly manual. Several screws are missing. Includes a coupon for one free hot dog.'
    ,
    `display_id`
    ,
    1,1
    ,
    500,0
    ,
    0,-1
    ,
    -1,1
    ,
    0,1
    ,
    1,4
    ,
    1
    ,
    ''
FROM
    `item_template`
WHERE
    `entry`=11142;
DELETE
FROM
    `gameobject_template`
WHERE
    `entry` IN (919058,919059);
INSERT INTO `gameobject_template`
    (
        `entry`    ,
        `type`     ,
        `displayId`,
        `name`     ,
        `size`     ,
        `data0`    ,
        `data1`    ,
        `data2`    ,
        `data3`    ,
        `script_name`
    )
VALUES
    (
        919059,7          ,
        39                ,
        'Ikae Earth Totem',
        1,1               ,
        1,0               ,
        0                 ,
        ''
    )
;
DELETE
FROM
    `gossip_menu`
WHERE
    `entry` BETWEEN 65027 AND 65037;
DELETE
FROM
    `npc_text`
WHERE
    `ID` BETWEEN 9195280 AND 9195298;
DELETE
FROM
    `broadcast_text`
WHERE
    `entry` BETWEEN 9195280 AND 9195298;
INSERT INTO `broadcast_text`
    (
        `entry`    ,
        `male_text`,
        `female_text`
    )
VALUES
    (
        9195280                                                                                                                                                           ,
        'Shamans make a fine showing at the Hearthstone table. Been studying their ways myself: totems, spirits, and enough lightning to settle an argument in one shot.',
        'Shamans make a fine showing at the Hearthstone table. Been studying their ways myself: totems, spirits, and enough lightning to settle an argument in one shot.'
    )
    ,
    (
        9195281                                                                                   ,
        'I''m in the middle of a demonstration. Give us a little room, aye? This next part ought to work.',
        'I''m in the middle of a demonstration. Give us a little room, aye? This next part ought to work.'
    )
    ,
    (
        9195282                                                                                     ,
        'Ye''ve seen my handiwork. Reckon we''ve enough to discuss without another walk.',
        'Ye''ve seen my handiwork. Reckon we''ve enough to discuss without another walk.'
    )
    ,
    (
        9195283                                                              ,
        'The Earth Totem''s assembled. A few pieces left over, but the important bits are all there. I think.',
        'The Earth Totem''s assembled. A few pieces left over, but the important bits are all there. I think.'
    )
    ,
    (
        9195284                                                                                       ,
        'Stay close, and I''ll show ye the Earth Totem. Mind the loose screw; I haven''t decided where it belongs.',
        'Stay close, and I''ll show ye the Earth Totem. Mind the loose screw; I haven''t decided where it belongs.'
    )
    ,
    (
        9195285                                        ,
        'Solid as a mountain. Hasn''t budged an inch. That''s earth for ye.',
        'Solid as a mountain. Hasn''t budged an inch. That''s earth for ye.'
    )
    ,
    (
        9195286                                                                      ,
        'Aye, it stayed put. I''ll count that as progress.',
        'Aye, it stayed put. I''ll count that as progress.'
    )
    ,
    (
        9195287                                                            ,
        'The Earth Totem''s ready. Let''s finish the demonstration, aye?',
        'The Earth Totem''s ready. Let''s finish the demonstration, aye?'
    )
    ,
    (
        9195288                                                     ,
        'Give me a moment. There''s still a bit more to this demonstration.',
        'Give me a moment. There''s still a bit more to this demonstration.'
    )
    ,
    (
        9195289                                                        ,
        'Kibler reckons I''ve built a chair. We''ll leave that point unsettled for now.',
        'Kibler reckons I''ve built a chair. We''ll leave that point unsettled for now.'
    )
    ,
    (
        9195290                                                  ,
        'Speak with the spirits first. Aye, I''ve written it down. Let''s see how the Water Totem fares.',
        'Speak with the spirits first. Aye, I''ve written it down. Let''s see how the Water Totem fares.'
    )
    ,
    (
        9195291                                                                                    ,
        'Next, the Water Totem. Plenty of liquid, good stout oak, and nae missing screws. A promising start.',
        'Next, the Water Totem. Plenty of liquid, good stout oak, and nae missing screws. A promising start.'
    )
    ,
    (
        9195292                                                    ,
        'The keg holds spirits. The totem calls spirits. Ye can see why I thought this would work.',
        'The keg holds spirits. The totem calls spirits. Ye can see why I thought this would work.'
    )
    ,
    (
        9195293                                                               ,
        'Nae answer from the water spirit. Perhaps shouting wasn''t the best introduction.',
        'Nae answer from the water spirit. Perhaps shouting wasn''t the best introduction.'
    )
    ,
    (
        9195294                                                                                ,
        'Well, nae lightning yet. Perhaps I''ve missed something in the instructions.',
        'Well, nae lightning yet. Perhaps I''ve missed something in the instructions.'
    )
    ,
    (
        9195295                                                                          ,
        'The Water Totem''s ready. We''ll finish here, then head back to the table.',
        'The Water Totem''s ready. We''ll finish here, then head back to the table.'
    )
    ,
    (
        9195296                                                    ,
        'We''ll head back to the table when we''re done here. I''ve a feeling my notes need work.',
        'We''ll head back to the table when we''re done here. I''ve a feeling my notes need work.'
    )
    ,
    (
        9195297                                        ,
        'Well, we''re back. What did ye make of all that? Be honest; I''ve heard worse about my furniture.',
        'Well, we''re back. What did ye make of all that? Be honest; I''ve heard worse about my furniture.'
    )
    ,
    (
        9195298                                                                                                   ,
        'Aye. I was so busy planning the oneshot, I forgot to ask whether the spirits wanted any part of it. Let''s see what we can learn at the table.',
        'Aye. I was so busy planning the oneshot, I forgot to ask whether the spirits wanted any part of it. Let''s see what we can learn at the table.'
    )
;
INSERT INTO `npc_text`
    (
        `ID`              ,
        `BroadcastTextID0`,
        `Probability0`
    )
VALUES
    (
        9195280,9195280,
        1
    )
    ,
    (
        9195281,9195281,
        1
    )
    ,
    (
        9195282,9195282,
        1
    )
    ,
    (
        9195283,9195283,
        1
    )
    ,
    (
        9195284,9195284,
        1
    )
    ,
    (
        9195285,9195285,
        1
    )
    ,
    (
        9195286,9195286,
        1
    )
    ,
    (
        9195287,9195287,
        1
    )
    ,
    (
        9195288,9195288,
        1
    )
    ,
    (
        9195289,9195289,
        1
    )
    ,
    (
        9195290,9195290,
        1
    )
    ,
    (
        9195291,9195291,
        1
    )
    ,
    (
        9195292,9195292,
        1
    )
    ,
    (
        9195293,9195293,
        1
    )
    ,
    (
        9195294,9195294,
        1
    )
    ,
    (
        9195295,9195295,
        1
    )
    ,
    (
        9195296,9195296,
        1
    )
    ,
    (
        9195297,9195297,
        1
    )
    ,
    (
        9195298,9195298,
        1
    )
;
INSERT INTO `gossip_menu`
    (
        `entry`,
        `text_id`
    )
VALUES
    (
        65027,9195280
    )
    ,
    (
        65028,9195282
    )
    ,
    (
        65029,9195281
    )
    ,
    (
        65030,9195280
    )
    ,
    (
        65031,9195287
    )
    ,
    (
        65032,9195291
    )
    ,
    (
        65033,9195295
    )
    ,
    (
        65034,9195285
    )
    ,
    (
        65035,9195289
    )
    ,
    (
        65036,9195293
    )
    ,
    (
        65037,9195297
    )
;
DELETE
FROM
    `quest_template`
WHERE
    `entry` BETWEEN 919811 AND 919813;
DELETE
FROM
    `creature_questrelation`
WHERE
    `quest` BETWEEN 919811 AND 919813;
DELETE
FROM
    `creature_involvedrelation`
WHERE
    `quest` BETWEEN 919811 AND 919813;
INSERT INTO `quest_template`
    (
        `entry`                ,
        `Method`               ,
        `ZoneOrSort`           ,
        `MinLevel`             ,
        `QuestLevel`           ,
        `RequiredRaces`        ,
        `RequiredClasses`      ,
        `QuestFlags`           ,
        `SpecialFlags`         ,
        `PrevQuestId`          ,
        `NextQuestInChain`     ,
        `ReqCreatureOrGOId1`   ,
        `ReqCreatureOrGOCount1`,
        `ReqItemId1`           ,
        `ReqItemCount1`        ,
        `ReqCreatureOrGOId2`   ,
        `ReqCreatureOrGOCount2`,
        `ReqItemId2`           ,
        `ReqItemCount2`        ,
        `ReqCreatureOrGOId3`   ,
        `ReqCreatureOrGOCount3`,
        `ReqItemId3`           ,
        `ReqItemCount3`        ,
        `ReqCreatureOrGOId4`   ,
        `ReqCreatureOrGOCount4`,
        `ReqItemId4`           ,
        `ReqItemCount4`        ,
        `RewXP`                ,
        `RewOrReqMoney`        ,
        `RewMoneyMaxLevel`     ,
        `RewItemId1`           ,
        `RewItemCount1`        ,
        `Title`                ,
        `Details`              ,
        `Objectives`           ,
        `ObjectiveText1`       ,
        `ObjectiveText2`       ,
        `ObjectiveText3`       ,
        `ObjectiveText4`       ,
        `RequestItemsText`     ,
        `OfferRewardText`
    )
VALUES
    (
        919811,2                                                                                                                                                                                                                                                                                                                                                                         ,
        -9100,10                                                                                                                                                                                                                                                                                                                                                                          ,
        0,589                                                                                                                                                                                                                                                                                                                                                                            ,
        0,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,919714                                                                                                                                                                                                                                                                                                                                                                         ,
        919812,0                                                                                                                                                                                                                                                                                                                                                                         ,
        0,919645                                                                                                                                                                                                                                                                                                                                                                         ,
        1,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,2595                                                                                                                                                                                                                                                                                                                                                                           ,
        1,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,500                                                                                                                                                                                                                                                                                                                                                                            ,
        2500,0                                                                                                                                                                                                                                                                                                                                                                              ,
        0,0                                                                                                                                                                                                                                                                                                                                                                              ,
        'Spirited Research'                                                                                                                                                                                                                                                                                                                                                              ,
        'I''ve been reading Wildhammer tales and Horde battle reports. Totems, spirits, lightning... put the lot together, and ye ought to have a proper oneshot.$B$BI''ve lost enough games to know what I need: a deck that ends the argument in a single turn. A shaman, done properly, would do it.$B$BI''ve ordered an Ikae Earth Totem Kit from Burbik Gearspanner at Burbik''s Supplies in the Great Forge. Fetch it and a Jug of Bourbon from Innkeeper Firebrew. Ye''ll have to pay up front, but I''ll pay ye back.',
        'Bring John Maloneybrew an Ikae Earth Totem Kit and a Jug of Bourbon.'                                                                                                                                                                                                                                                                                                   ,
        ''                                                                                                                                                                                                                                                                                                                                                                               ,
        ''                                                                                                                                                                                                                                                                                                                                                                               ,
        ''                                                                                                                                                                                                                                                                                                                                                                               ,
        ''                                                                                                                                                                                                                                                                                                                                                                               ,
        'Burbik''s Supplies in the Great Forge has the kit, and Firebrew sells the bourbon. I''ll pay ye back when ye bring both; mind ye don''t drink my research on the way.'                                                                                                                                                                           ,
        'Aye, the pieces fit. Several screws are missing, but I won''t be sitting on the thing.$B$BThe bourbon''s in the keg, too. Here''s yer money back, as promised. That''s both totems ready for a proper demonstration.'
    )
    ,
    (
        919812,2                                                                                                                                                                                                                                 ,
        -9100,10                                                                                                                                                                                                                                  ,
        0,589                                                                                                                                                                                                                                    ,
        0,0                                                                                                                                                                                                                                      ,
        0,919811                                                                                                                                                                                                                                 ,
        919813,919548                                                                                                                                                                                                                            ,
        1,0                                                                                                                                                                                                                                      ,
        0,0                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                      ,
        0,0                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                      ,
        0,0                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                      ,
        0,500                                                                                                                                                                                                                                    ,
        0,0                                                                                                                                                                                                                                      ,
        0,0                                                                                                                                                                                                                                      ,
        'A Question of Totems'                                                                                                                                                                                                                   ,
        'Enough reading. Time to see whether I''ve got the knack.$B$BThe Earth Totem''s ready, and the Water Totem''s waiting by the kegs. Come along and watch closely; if this works, we''ll be back at the table with a oneshot worth remembering.',
        'Follow John''s totem demonstration and return with him to the table.'                                                                                                                                              ,
        'Watch John''s totem demonstration'                                                                                                                                                                                                                 ,
        ''                                                                                                                                                                                                                    ,
        ''                                                                                                                                                                                                                 ,
        ''                                                                                                                                                                                                                   ,
        'Stay close while I show ye the totems. We''ll talk it over when we''re back at the table.'                                                                                                                                      ,
        'A chair and a keg. Aye, there''s nae arguing with that. Ikae can have the chair back; I''ll keep the bourbon.$B$BThose reports told me what shamans could do. They left out how much listening came before the lightning.'
    )
    ,
    (
        919813,2                                                                                                                                                                                                                                                            ,
        -9100,10                                                                                                                                                                                                                                                             ,
        0,589                                                                                                                                                                                                                                                               ,
        0,0                                                                                                                                                                                                                                                                 ,
        2,919812                                                                                                                                                                                                                                                            ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,0                                                                                                                                                                                                                                                                 ,
        0,500                                                                                                                                                                                                                                                               ,
        0,0                                                                                                                                                                                                                                                                 ,
        919625,1                                                                                                                                                                                                                                                            ,
        'Applied Shamanism'                                                                                                                                                                                                                                                 ,
        'The chair''s nae totem, but these Hearthstone cards summon the real thing. At least, the little wooden sort that fits on a table.$B$BSit down and play a match with me. I''ll put my Shaman deck to work, and we''ll see how far a little patience gets me. Beat me, and I''ve a Shaman Deck for ye to take home.',
        'Defeat John Maloneybrew in a Hearthstone match.'                                                                                                                                                                                                                   ,
        'Defeat John at Hearthstone'                                                                                                                                                                                                                                        ,
        ''                                                                                                                                                                                                                                                                  ,
        ''                                                                                                                                                                                                                                                                  ,
        ''                                                                                                                                                                                                                                                                  ,
        'Ready for another hand? I''ve a plan that lasts past the first turn this time.'                                                                                                                                                                                         ,
        'Well played! Ye gave me plenty to think about, and nae furniture was harmed.$B$BHere''s your Shaman Deck. I''ll keep practising. A oneshot, mind, but with patience this time. First, though, I''ve a receipt to find so I can return that chair to Ikae.'
    )
;
INSERT INTO `creature_questrelation`
    (
        `id`,
        `quest`
    )
VALUES
    (
        919545,919811
    )
    ,
    (
        919545,919812
    )
    ,
    (
        919545,919813
    )
;
INSERT INTO `creature_involvedrelation`
    (
        `id`,
        `quest`
    )
VALUES
    (
        919545,919811
    )
    ,
    (
        919545,919812
    )
    ,
    (
        919545,919813
    )
;
-- Native quest-only listing. Preserve Burbik's existing services and stock.
INSERT IGNORE
INTO
    `conditions`
    (
        `type`  ,
        `value1`,
        `value2`,
        `value3`,
        `value4`,
        `flags`
    )
VALUES
    (
        9,919811,
        1,0     ,
        0,0
    )
;
DELETE
FROM
    `npc_vendor`
WHERE
    `entry`=5163
AND `item` =919645;
INSERT INTO `npc_vendor`
    (
        `entry`   ,
        `item`    ,
        `maxcount`,
        `incrtime`,
        `condition_id`
    )
SELECT
    5163,919645
    ,
    0,0
    ,
    `condition_entry`
FROM
    `conditions`
WHERE
    `type`  =9
AND `value1`=919811
AND `value2`=1
AND `value3`=0
AND `value4`=0
AND `flags` =0;
-- Quest-only Basic Shaman / Normal; account Alliance roster at final turn-in.
DELETE
FROM
    `hearthstone_opponent`
WHERE
    `creature_entry`=919545;
DELETE
FROM
    `hearthstone_npc_unlock`
WHERE
    `creature_entry`=919545;
INSERT INTO `hearthstone_npc_unlock`
    (
        `creature_entry`,
        `quest_id`      ,
        `deck_key`      ,
        `faction`
    )
VALUES
    (
        919545,919813  ,
        'normal_shaman',
        'alliance'
    );