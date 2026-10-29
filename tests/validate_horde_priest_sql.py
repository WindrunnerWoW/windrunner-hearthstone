#!/usr/bin/env python3
"""Validate migration 0027 twice using connection-local TEMPORARY tables only."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--fresh-spawn', action='store_true', help='Exercise local GUID allocation instead of reuse')
    args = parser.parse_args()
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                      args.config.read_text(encoding='utf-8-sig'), re.M)
    if not match:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = match.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--host='+host, '--port='+port, '--user='+user,
               '--database='+database, '--batch', '--skip-column-names']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migration = (root / 'data/sql/world/0027_hearthstone_horde_priest_quests.sql').read_text()
    if re.search(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?gameobject`?\s', migration, re.I):
        raise SystemExit('Priest pickups must remain runtime-only')
    tables = {
        'creature_template': 'entry IN (919501,4607)',
        'item_template': 'entry=919624',
        'gossip_menu': 'entry=65000', 'npc_text': 'ID=1', 'broadcast_text': 'entry=1',
        'gameobject_template': 'entry=180436', 'quest_template': 'entry=919704',
        'creature_questrelation': 'id=919501', 'creature_involvedrelation': 'id=919501',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919522',
        'creature': 'guid=(SELECT MIN(guid) FROM creature)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
        'creature_movement': 'id=(SELECT MIN(id) FROM creature_movement)',
    }
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+t+'`' for t in tables)+';'
    before = query(checksum_sql)
    fields = {table: [r.split('\t')[0] for r in query('SHOW COLUMNS FROM `'+table+'`;').splitlines()]
              for table in tables}
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_priest_seed_'+table
        setup.extend([
            f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
            f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
            f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
            f'INSERT INTO `{table}` SELECT * FROM `{seed}`;',
        ])
    # Preserve unrelated placement/path seeds. Exercise both an existing owner
    # placement with a stale route and a clean database with no Priest placement.
    setup += ['UPDATE gameobject SET id=919056;',
              'UPDATE hs_priest_seed_gameobject SET id=919056;']
    if not args.fresh_spawn:
        spawn_fields = [f for f in fields['creature'] if f != 'guid']
        setup += [
            'INSERT INTO creature ('+','.join('`'+f+'`' for f in spawn_fields)+') SELECT '+
            ','.join('919539' if f == 'id' else '`'+f+'`' for f in spawn_fields)+
            ' FROM hs_priest_seed_creature;',
            'SET @hs_priest_expected_guid=LAST_INSERT_ID();',
            'INSERT INTO creature_movement (id,point,position_x) VALUES (@hs_priest_expected_guid,99,1);',
        ]
    baseline_fields = ('level_min','level_max','health_min','health_max','armor','speed_walk','speed_run',
                       'detection_range','call_for_help_range','dmg_min','dmg_max','attack_power',
                       'base_attack_time','ranged_attack_time','unit_class','unit_flags','type','regeneration')
    baseline = ' AND '.join(f't.`{f}`=b.`{f}`' for f in baseline_fields)
    checks = [
        ('baseline and placeholder source exist', 'SELECT COUNT(*)=2 FROM hs_priest_seed_creature_template'),
        ('teacher identity model city services', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919539 "
         "AND name='Jeffrey Shih' AND display_id1=2626 AND faction=68 AND npc_flags=3 AND movement_type=0 "
         "AND gossip_menu_id=65019 AND subname='Hearthstone priest' AND script_name=''"),
        ('copied teacher baseline', 'SELECT COUNT(*)=1 FROM creature_template t JOIN hs_priest_seed_creature_template b '
         'ON b.entry=919501 WHERE t.entry=919539 AND '+baseline),
        ('placeholder matches local Undercity priest', 'SELECT COUNT(*)=1 FROM creature_template t '
         'JOIN hs_priest_seed_creature_template b ON b.entry=4607 WHERE t.entry=919539 AND t.display_id1=b.display_id1'),
        ('native level faction class and XP gates', 'SELECT COUNT(*)=2 FROM quest_template WHERE entry IN (919791,919792) '
         'AND Method=2 AND ZoneOrSort=-9100 AND MinLevel=10 AND QuestLevel=0 AND RequiredRaces=434 '
         'AND RequiredClasses=0 AND QuestFlags=0 AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0'),
        ('intro and chain links', 'SELECT COUNT(*)=2 FROM quest_template WHERE '
         '(entry=919791 AND PrevQuestId=919704 AND NextQuestInChain=919792 AND SpecialFlags=0) OR '
         '(entry=919792 AND PrevQuestId=919791 AND NextQuestInChain=0 AND SpecialFlags=2)'),
        ('exactly four fragments and four cards', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919791 '
         'AND ReqItemId1=919643 AND ReqItemCount1=4 AND ReqItemId2=919644 AND ReqItemCount2=4 '
         "AND Title='Dis''Carded'' Remains'"),
        ('event duel and existing Priest reward', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919792 '
         "AND Title='A Matter of Perspective' AND RewItemId1=919624 AND RewItemCount1=1"),
        ('no fetch reward items', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919791 '
         'AND RewItemId1=0 AND RewItemCount1=0'),
        ('all native starts and turn-ins', 'SELECT '
         '(SELECT COUNT(*) FROM creature_questrelation WHERE quest IN (919791,919792))=2 AND '
         '(SELECT COUNT(*) FROM creature_questrelation WHERE id=919539 AND quest IN (919791,919792))=2 AND '
         '(SELECT COUNT(*) FROM creature_involvedrelation WHERE quest IN (919791,919792))=2 AND '
         '(SELECT COUNT(*) FROM creature_involvedrelation WHERE id=919539 AND quest IN (919791,919792))=2'),
        ('two unsellable capped quest items', 'SELECT COUNT(*)=2 FROM item_template WHERE entry IN (919643,919644) '
         "AND class=12 AND max_count=4 AND stackable=4 AND bonding=4 AND buy_price=0 AND sell_price=0 "
         "AND start_quest=0 AND flags=0 AND script_name=''"),
        ('item names and models', "SELECT COUNT(*)=2 FROM item_template WHERE "
         "(entry=919643 AND name='Holy Tablet Fragment' AND display_id=11183) OR "
         "(entry=919644 AND name='Old Lordaeron Card' AND display_id=31755)"),
        ('shared consumable quest chests', 'SELECT COUNT(*)=2 FROM gameobject_template WHERE entry IN (919056,919057) '
         "AND type=3 AND data0=0 AND data1=0 AND data3=1 AND data8=919791 AND flags=0 AND script_name='' AND "
         '((entry=919056 AND displayId=6419 AND ABS(size-0.25)<0.00001) OR '
         '(entry=919057 AND displayId=220 AND ABS(size-0.35)<0.00001))'),
        ('greeting menu binding', 'SELECT COUNT(*)=1 FROM gossip_menu WHERE entry=65019 AND text_id=9195250'),
        ('both genders greeting text binding', 'SELECT COUNT(*)=1 FROM npc_text n JOIN broadcast_text b '
         'ON b.entry=n.BroadcastTextID0 WHERE n.ID=9195250 AND n.Probability0=1 AND b.male_text=b.female_text '
         "AND b.male_text='Faith is useful. Sentimentality takes up space in your hand.'"),
        ('quest-only direct opponent and account Horde roster', 'SELECT '
         '(SELECT COUNT(*) FROM hearthstone_opponent WHERE creature_entry=919539)=0 AND '
         '(SELECT COUNT(*) FROM hearthstone_npc_unlock WHERE creature_entry=919539 AND quest_id=919792 '
         "AND deck_key='normal_priest' AND faction='horde')=1"),
        ('fetch and turn-in wording matches quantities', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919791 "
         "AND Objectives LIKE '%four Holy Tablet Fragments and four Old Lordaeron Cards%return to Jeffrey Shih%' "
         "AND Details LIKE '%Undercity%four stone fragments and four cards%'"),
        ('duel challenge and deck reward wording', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919792 "
         "AND Details LIKE '%Play a game of Hearthstone with me and win.%' AND Objectives='Defeat Jeffrey Shih in a Hearthstone duel.' AND OfferRewardText LIKE '%Take this Priest Deck and use it wisely.%'"),
    ]
    checks.append(('captured spawn all fields', 'SELECT COUNT(*)=1 FROM creature WHERE `id`=919539 AND `id2`=0 AND `id3`=0 AND `id4`=0 AND `map`=0 AND `position_x`=CAST(1646.9200439453125 AS FLOAT) AND `position_y`=CAST(290.1390075683594 AS FLOAT) AND `position_z`=CAST(-62.18199920654297 AS FLOAT) AND `orientation`=CAST(2.4702401161193848 AS FLOAT) AND `spawntimesecsmin`=120 AND `spawntimesecsmax`=120 AND `wander_distance`=CAST(0 AS FLOAT) AND `health_percent`=CAST(100 AS FLOAT) AND `mana_percent`=CAST(100 AS FLOAT) AND `movement_type`=2 AND `spawn_flags`=0 AND `visibility_mod`=CAST(0 AS FLOAT)'))
    if not args.fresh_spawn:
        checks.append(('reuse existing local spawn GUID', 'SELECT COUNT(*)=1 FROM creature WHERE id=919539 AND guid=@hs_priest_expected_guid'))
    checks.append(('captured waypoint 1 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=1 AND m.`position_x`=CAST(1633.734619140625 AS FLOAT) AND m.`position_y`=CAST(298.728759765625 AS FLOAT) AND m.`position_z`=CAST(-62.17864990234375 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 2 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=2 AND m.`position_x`=CAST(1610.110107421875 AS FLOAT) AND m.`position_y`=CAST(308.1663818359375 AS FLOAT) AND m.`position_z`=CAST(-62.17864990234375 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 3 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=3 AND m.`position_x`=CAST(1588.984619140625 AS FLOAT) AND m.`position_y`=CAST(308.28125 AS FLOAT) AND m.`position_z`=CAST(-62.17864990234375 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 4 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=4 AND m.`position_x`=CAST(1561.356689453125 AS FLOAT) AND m.`position_y`=CAST(300.2469177246094 AS FLOAT) AND m.`position_z`=CAST(-62.17864990234375 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 5 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=5 AND m.`position_x`=CAST(1546.5582275390625 AS FLOAT) AND m.`position_y`=CAST(289.2732849121094 AS FLOAT) AND m.`position_z`=CAST(-62.181575775146484 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 6 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=6 AND m.`position_x`=CAST(1532.487548828125 AS FLOAT) AND m.`position_y`=CAST(268.4001159667969 AS FLOAT) AND m.`position_z`=CAST(-62.17864227294922 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 7 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=7 AND m.`position_x`=CAST(1527.780517578125 AS FLOAT) AND m.`position_y`=CAST(250.47503662109375 AS FLOAT) AND m.`position_z`=CAST(-62.17864227294922 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 8 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=8 AND m.`position_x`=CAST(1527.5367431640625 AS FLOAT) AND m.`position_y`=CAST(228.8719940185547 AS FLOAT) AND m.`position_z`=CAST(-62.17864227294922 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 9 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=9 AND m.`position_x`=CAST(1531.171630859375 AS FLOAT) AND m.`position_y`=CAST(208.995849609375 AS FLOAT) AND m.`position_z`=CAST(-62.17864227294922 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 10 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=10 AND m.`position_x`=CAST(1550.3897705078125 AS FLOAT) AND m.`position_y`=CAST(185.59970092773438 AS FLOAT) AND m.`position_z`=CAST(-62.18134307861328 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 11 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=11 AND m.`position_x`=CAST(1573.87939453125 AS FLOAT) AND m.`position_y`=CAST(174.09121704101562 AS FLOAT) AND m.`position_z`=CAST(-62.17879104614258 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 12 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=12 AND m.`position_x`=CAST(1602.5394287109375 AS FLOAT) AND m.`position_y`=CAST(173.20773315429688 AS FLOAT) AND m.`position_z`=CAST(-62.17879104614258 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 13 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=13 AND m.`position_x`=CAST(1627.7667236328125 AS FLOAT) AND m.`position_y`=CAST(180.37644958496094 AS FLOAT) AND m.`position_z`=CAST(-62.17879104614258 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 14 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=14 AND m.`position_x`=CAST(1644.432861328125 AS FLOAT) AND m.`position_y`=CAST(190.16107177734375 AS FLOAT) AND m.`position_z`=CAST(-62.1817741394043 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 15 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=15 AND m.`position_x`=CAST(1657.3623046875 AS FLOAT) AND m.`position_y`=CAST(208.63641357421875 AS FLOAT) AND m.`position_z`=CAST(-62.17878723144531 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 16 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=16 AND m.`position_x`=CAST(1663.880859375 AS FLOAT) AND m.`position_y`=CAST(235.48098754882812 AS FLOAT) AND m.`position_z`=CAST(-62.17878723144531 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 17 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=17 AND m.`position_x`=CAST(1663.4609375 AS FLOAT) AND m.`position_y`=CAST(260.1195983886719 AS FLOAT) AND m.`position_z`=CAST(-62.17878723144531 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    checks.append(('captured waypoint 18 all fields', 'SELECT COUNT(*)=1 FROM creature_movement m JOIN creature c ON c.guid=m.id WHERE c.id=919539 AND m.`point`=18 AND m.`position_x`=CAST(1647.08740234375 AS FLOAT) AND m.`position_y`=CAST(289.8534240722656 AS FLOAT) AND m.`position_z`=CAST(-62.181705474853516 AS FLOAT) AND m.`orientation`=CAST(100 AS FLOAT) AND m.`waittime`=0 AND m.`wander_distance`=CAST(0 AS FLOAT) AND m.`script_id`=0'))
    # Assert every objective slot, including all unused creature, source and spell
    # columns; item labels come from item names, with no fake talk counters.
    for quest in (919791,919792):
        zero = []
        for slot in range(1,5):
            zero += [f'{f}{slot}=0' for f in ('ReqCreatureOrGOId','ReqCreatureOrGOCount','ReqSourceId','ReqSourceCount','ReqSpellCast')]
            if quest == 919792 or slot > 2:
                zero += [f'ReqItemId{slot}=0',f'ReqItemCount{slot}=0']
            zero.append(f"COALESCE(ObjectiveText{slot},'')="+("'Defeat Jeffrey Shih at Hearthstone'" if quest == 919792 and slot == 1 else "''"))
        zero += ['SrcItemId=0','SrcItemCount=0','SrcSpell=0']
        checks.append((f'every objective slot {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(zero)))
    for slot in range(1,6):
        checks.append((f'no reputation rewards {slot}', 'SELECT COUNT(*)=2 FROM quest_template WHERE '
                       'entry IN (919791,919792) AND '+ ' AND '.join(f'{f}{slot}=0' for f in
                       ('RewRepFaction','RewRepValue'))))
    extra_rewards = [f'{f}{slot}=0' for slot in range(2,5) for f in ('RewItemId','RewItemCount')]
    extra_rewards += [f'{f}{slot}=0' for slot in range(1,7) for f in ('RewChoiceItemId','RewChoiceItemCount')]
    checks.append(('no extra rewards', 'SELECT COUNT(*)=2 FROM quest_template WHERE entry IN (919791,919792) AND '+
                   ' AND '.join(extra_rewards)))
    # Every copied seed record must remain byte-for-byte equivalent in all columns.
    for table in tables:
        comparison = ' AND '.join(f't.`{f}` <=> b.`{f}`' for f in fields[table])
        checks.append((f'preserve every seed field {table}',
                       f'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_priest_seed_{table}) FROM `{table}` t '
                       f'JOIN `hs_priest_seed_{table}` b ON {comparison}'))
    new_counts = {'creature_template':1,'item_template':2,'gossip_menu':1,'npc_text':1,'broadcast_text':1,
                  'gameobject_template':2,'quest_template':2,'creature_questrelation':2,'creature_involvedrelation':2,
                  'hearthstone_npc_unlock':1,'creature':1,'creature_movement':18}
    for table in tables:
        checks.append((f'idempotent row count {table}',
                       f'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_priest_seed_{table})+{new_counts.get(table,0)} FROM `{table}`'))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    # Two executions on the same connection and same shadows, then compare live
    # checksums using a different connection. No permanent DDL or live DML.
    output = query('\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed during validation')
    rows = output.strip().splitlines()
    failures = [r for r in rows if not r.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary-table validation failed: '+repr(failures or rows))
    print(f'Migration 0027 passed {len(rows)} assertions over two temporary-table runs; '
          f'{len(tables)} schemas shadowed. Independent live-table checksums unchanged.')


if __name__ == '__main__':
    main()
