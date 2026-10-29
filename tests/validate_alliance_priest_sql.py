#!/usr/bin/env python3
"""Validate Alliance Priest SQL twice in temporary tables, leaving live data untouched."""
import argparse
from collections import Counter
from decimal import Decimal
import json
import os
from pathlib import Path
import re
import struct
import subprocess


CREATURES = (919578, 919579, 919580, 919581)
ACTORS = (919578, 919579)
QUESTS = (919861, 919862, 919863)
TEXTS = tuple(range(9195400, 9195409)) + tuple(range(9195410, 9195422))
MENUS = {
    65080: 9195400, 65081: 9195401, 65082: 9195402, 65083: 9195405,
    65084: 9195406, 65085: 9195407, 65086: 9195408, 65087: 9195410,
    65088: 9195411, 65089: 9195412, 65090: 9195415, 65091: 9195417,
    65092: 9195418, 65093: 9195419, 65094: 9195420, 65095: 9195421,
    65096: 9195418,
}
SPAWN_COLUMNS = ('id', 'id2', 'id3', 'id4', 'map', 'position_x', 'position_y', 'position_z',
                 'orientation', 'spawntimesecsmin', 'spawntimesecsmax', 'wander_distance',
                 'health_percent', 'mana_percent', 'movement_type', 'spawn_flags', 'visibility_mod')
SPAWN_FLOATS = {'position_x', 'position_y', 'position_z', 'orientation', 'wander_distance',
                'health_percent', 'mana_percent', 'visibility_mod'}
SPAWN_RELATED = ('creature_addon', 'creature_movement', 'creature_movement_special',
                 'creature_movement_template', 'creature_groups', 'creature_linking',
                 'creature_linking_template', 'game_event_creature', 'game_event_creature_data',
                 'pool_creature', 'pool_creature_template')


def quoted(value):
    return "'" + value.replace("'", "''") + "'"


def integers(values):
    return ','.join(str(value) for value in values)


def validate_targets(migration, allowed):
    # A database-qualified write bypasses same-named temporary tables. Strip
    # narrative/comments, then reject everything except ordinary shadowed DML.
    code = re.sub(r"'(?:''|\\.|[^'\\])*'|--[^\r\n]*|/\*[\s\S]*?\*/", ' ', migration)
    writes = re.findall(r'(?:INSERT\s+(?:IGNORE\s+)?INTO|UPDATE|DELETE\s+FROM)\s+'
                        r'(`?\w+`?(?:\s*\.\s*`?\w+`?)?)', code, re.I)
    if any('.' in target for target in writes):
        raise SystemExit('Schema-qualified writes cannot be safely shadowed')
    touched = {target.strip('`') for target in writes}
    if not touched <= allowed:
        raise SystemExit('Unexpected or unshadowed migration tables: ' + repr(touched - allowed))
    if re.search(r'\b(?:CREATE|ALTER|DROP|TRUNCATE|REPLACE|RENAME|USE|CALL|LOAD)\b', code, re.I):
        raise SystemExit('Only ordinary data writes may appear in this migration')
    return touched


def validate_spawn_export(migration, snapshot):
    """Compare numeric export fields with the independently captured native rows."""
    marker = '-- Owner-recorded Alliance Priest placements, 2026-10-10.'
    if (migration.count(marker) != 1 or snapshot.get('captured_on') != '2026-10-10'
            or tuple(snapshot['columns']) != SPAWN_COLUMNS):
        raise SystemExit('Missing or invalid Alliance Priest placement snapshot')
    before, block = migration.split(marker)
    validate_targets(before, {'creature_template', 'gossip_menu', 'npc_text', 'broadcast_text',
                             'quest_template', 'creature_questrelation', 'creature_involvedrelation',
                             'hearthstone_opponent', 'hearthstone_npc_unlock', 'item_template'})
    code = re.sub(r'--[^\r\n]*', ' ', block)
    number = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
    row = r'\(\s*' + number + r'(?:\s*,\s*' + number + r'){16}\s*\)'
    match = re.fullmatch(r'\s*DELETE\s+FROM\s+`creature`\s+WHERE\s+`id`\s+IN\s*'
                         r'\(([^)]+)\)\s*;\s*INSERT\s+INTO\s+`creature`\s*\(([^)]+)\)\s+VALUES\s*('
                         + row + r'(?:\s*,\s*' + row + r')*)\s*;\s*', code, re.I)
    if not match or tuple(map(int, match.group(1).split(','))) != ACTORS or tuple(
            column.strip().strip('`') for column in match.group(2).split(',')) != SPAWN_COLUMNS:
        raise SystemExit('Only the two actor entries and exact non-GUID numeric fields may be exported')
    expected = snapshot['spawns']
    if len(expected) != len(ACTORS) or any(set(spawn['fields']) != set(SPAWN_COLUMNS) for spawn in expected):
        raise SystemExit('Incomplete Alliance Priest native spawn fields')
    if {int(spawn['fields']['id']) for spawn in expected} != set(ACTORS):
        raise SystemExit('Placement snapshot must contain both permanent Priest actors only')
    source_guids = [spawn['source_guid'] for spawn in expected]
    if len(set(source_guids)) != len(source_guids) or any(int(guid) <= 0 for guid in source_guids):
        raise SystemExit('Invalid captured source-GUID provenance')
    expected_rows = Counter(tuple(Decimal(spawn['fields'][column]) for column in SPAWN_COLUMNS)
                            for spawn in expected)
    actual_rows = Counter(tuple(Decimal(value.strip()) for value in row.split(','))
                          for row in re.findall(r'\(([^)]+)\)', match.group(3)))
    if actual_rows != expected_rows:
        raise SystemExit('Exported Priest placements differ from captured native data')
    # The actual capture contains exactly one placement for each permanent actor.
    return [spawn['fields'] for spawn in expected]


def dbc_rows(path, entries):
    data = path.read_bytes()
    magic, count, fields, size, string_size = struct.unpack_from('<4sIIII', data)
    if magic != b'WDBC' or fields * 4 != size or len(data) != 20 + count * size + string_size:
        raise SystemExit('Invalid native DBC: ' + str(path))
    rows = {}
    for offset in range(20, 20 + count * size, size):
        entry = struct.unpack_from('<I', data, offset)[0]
        if entry in entries:
            rows[entry] = struct.unpack_from('<' + 'I' * fields, data, offset)
    if set(rows) != set(entries):
        raise SystemExit('Missing native DBC asset rows: ' + str(path))
    return rows, data[20 + count * size:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--dbc-dir', type=Path, help='Override the configured DataDir/dbc directory')
    parser.add_argument('--port', type=int, help='Override the configured local database port')
    args = parser.parse_args()
    configuration = args.config.read_text(encoding='utf-8-sig')
    setting = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                        configuration, re.M)
    if not setting:
        raise SystemExit('WorldDatabase.Info missing')
    data_dir = re.search(r'^\s*DataDir\s*=\s*"([^"]+)"', configuration, re.M)
    if not args.dbc_dir and not data_dir:
        raise SystemExit('DataDir missing; supply --dbc-dir for native asset validation')
    dbc_dir = args.dbc_dir or (args.config.parent / data_dir.group(1) / 'dbc').resolve()
    dbc_rows(dbc_dir / 'CreatureDisplayInfo.dbc', (2200, 2306))
    item_rows, strings = dbc_rows(dbc_dir / 'ItemDisplayInfo.dbc', (7366,))
    icon_offset = item_rows[7366][5]
    icon = strings[icon_offset:strings.find(b'\0', icon_offset)].decode('ascii')
    if icon != 'INV_Jewelry_Amulet_01':
        raise SystemExit('Native cloak-clasp icon mismatch')
    host, port, user, password, database = setting.group(1).split(';')
    if args.port is not None:
        port = str(args.port)
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--host=' + host, '--port=' + port, '--user=' + user,
               '--database=' + database, '--batch', '--skip-column-names', '--connect-timeout=5']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True,
                                env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migration = (root / 'data/sql/world/0039_hearthstone_alliance_priest_quests.sql').read_text()
    fixture = root / 'tests/fixtures/alliance_priest_spawns.json'
    spawns = validate_spawn_export(migration, json.loads(fixture.read_text(encoding='utf-8')))
    # Every table gets its full live schema. Seed every unrelated row, including
    # services, stock, quest links and owner placements, rather than selected fields.
    # Exclude only chain-owned records so this remains usable after installation.
    owned = {
        'creature_template': 'entry IN (' + integers(CREATURES) + ')',
        'gossip_menu': 'entry IN (' + integers(MENUS) + ')',
        'npc_text': 'ID IN (' + integers(TEXTS) + ')',
        'broadcast_text': 'entry IN (' + integers(TEXTS) + ')',
        'quest_template': 'entry IN (' + integers(QUESTS) + ')',
        'creature_questrelation': 'quest IN (' + integers(QUESTS) + ')',
        'creature_involvedrelation': 'quest IN (' + integers(QUESTS) + ')',
        'hearthstone_opponent': 'creature_entry=919578',
        'hearthstone_npc_unlock': 'creature_entry=919578',
        'item_template': 'entry=919651',
        'npc_vendor': 'FALSE',
        'npc_vendor_template': 'FALSE',
        'creature': 'id IN (' + integers(ACTORS) + ')',
        'gameobject': 'FALSE',
    }
    allowed = set(owned) - {'npc_vendor', 'npc_vendor_template', 'gameobject'}
    validate_targets(migration, allowed)
    prerequisites = query(
        'SELECT COUNT(*) FROM creature_template WHERE entry IN (919502,4090,4262);'
        'SELECT COUNT(*) FROM item_template WHERE entry IN (919624,12352);'
        'SELECT COUNT(*) FROM quest_template WHERE entry=919714;'
        'SELECT COUNT(*) FROM creature_template WHERE entry=4090 AND display_id1=2200;'
        'SELECT COUNT(*) FROM creature_template WHERE entry=4262 AND display_id1=2306;'
        'SELECT COUNT(*) FROM item_template WHERE entry=12352 AND display_id=7366;')
    if prerequisites.strip().splitlines() != ['3', '2', '1', '1', '1', '1']:
        raise SystemExit('Missing required Alliance world baselines, native models, clasp or deck')
    related = query('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() '
                    'AND TABLE_NAME IN (' + ','.join(quoted(table) for table in SPAWN_RELATED)
                    + ') ORDER BY TABLE_NAME;').splitlines()
    checksum_sql = 'CHECKSUM TABLE ' + ','.join('`' + table + '`' for table in (*owned, *related)) + ';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(owned) + len(related) or any(row.endswith('\tNULL') for row in before.splitlines()):
        raise SystemExit('Incomplete initial checksums')
    names = ','.join(quoted(table) for table in owned)
    metadata = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS '
                     'WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN (' + names + ') '
                     'ORDER BY TABLE_NAME,ORDINAL_POSITION;')
    fields = {table: [] for table in owned}
    for row in metadata.splitlines():
        table, column = row.split('\t')
        fields[table].append(column)
    keys = {table: [] for table in owned}
    primary = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.STATISTICS '
                    "WHERE TABLE_SCHEMA=DATABASE() AND INDEX_NAME='PRIMARY' AND TABLE_NAME IN (" + names + ') '
                    'ORDER BY TABLE_NAME,SEQ_IN_INDEX;')
    for row in primary.splitlines():
        table, column = row.split('\t')
        keys[table].append(column)
    if any(not columns for columns in fields.values()) or any(not columns for columns in keys.values()):
        raise SystemExit('Incomplete schema or primary-key metadata')
    if set(fields['creature']) != set(SPAWN_COLUMNS) | {'guid'}:
        raise SystemExit('Native creature schema differs from captured spawn fields')
    setup = []
    for table, predicate in owned.items():
        seed = 'hs_alliance_priest_seed_' + table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE NOT ({predicate});']
    # The unused gap must survive both applications. Add sentinel rows only in
    # temporary seeds if the real database has no unrelated row there already.
    setup += ["INSERT IGNORE INTO hs_alliance_priest_seed_broadcast_text (entry,male_text,female_text) "
              "VALUES (9195409,'Unowned reserved gap','Unowned reserved gap');",
              'INSERT IGNORE INTO hs_alliance_priest_seed_npc_text (ID,BroadcastTextID0,Probability0) '
              'VALUES (9195409,9195409,1);']
    for table in owned:
        seed = 'hs_alliance_priest_seed_' + table
        setup += [f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']
    # Prove rerunnable replacement cleans all owned quest relations and a stale
    # direct-play binding, without touching the baseline copied from Ibelin.
    columns = ','.join('`' + field + '`' for field in fields['hearthstone_opponent'])
    values = ','.join('919578' if field == 'creature_entry' else 'b.`' + field + '`'
                      for field in fields['hearthstone_opponent'])
    setup += [f'INSERT INTO hearthstone_opponent ({columns}) SELECT {values} '
              'FROM hs_alliance_priest_seed_hearthstone_opponent b WHERE b.creature_entry=919501;',
              'INSERT INTO creature_questrelation (id,quest) VALUES (919579,919861);',
              'INSERT INTO creature_involvedrelation (id,quest) VALUES (919579,919862);']
    # Exercise replacement of wrong positions and duplicates; only temporary
    # owned actor spawns are inserted, never bookkeeping credits.
    setup += ['INSERT INTO creature (id,map,position_x,position_y,position_z,orientation) VALUES '
              + ','.join(f'({entry},0,1,2,3,4)' for entry in ACTORS for _ in range(2)) + ';']

    checks = [
        ('no ordinary direct play', 'SELECT COUNT(*)=0 FROM hearthstone_opponent '
         'WHERE creature_entry IN (' + integers(CREATURES) + ')'),
        ('teacher roster unlock', "SELECT COUNT(*)=1 FROM hearthstone_npc_unlock "
         "WHERE creature_entry=919578 AND quest_id=919863 AND deck_key='normal_priest' AND faction='alliance'"),
        ('no supporting actor roster', 'SELECT COUNT(*)=0 FROM hearthstone_npc_unlock '
         'WHERE creature_entry IN (919579,919580,919581)'),
        ('credit templates unspawned', 'SELECT COUNT(*)=0 FROM creature WHERE id IN (919580,919581)'),
        ('dialogue gender bindings', 'SELECT COUNT(*)=21 FROM npc_text n JOIN broadcast_text b '
         'ON b.entry=n.BroadcastTextID0 WHERE n.ID IN (' + integers(TEXTS) + ') '
         'AND n.ID=b.entry AND n.Probability0=1 AND b.male_text=b.female_text AND LENGTH(b.male_text)>0'),
        ('unowned broadcast gap', 'SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195409'),
        ('unowned npc-text gap', 'SELECT COUNT(*)=1 FROM npc_text WHERE ID=9195409'),
    ]
    spawn_rows = Counter(tuple(Decimal(values[column]) for column in SPAWN_COLUMNS) for values in spawns)
    for ordinal, (values, count) in enumerate(spawn_rows.items(), 1):
        predicates = [f'`{column}` <=> CAST({value} AS FLOAT)' if column in SPAWN_FLOATS else
                      f'`{column}`={value}' for column, value in zip(SPAWN_COLUMNS, values)]
        checks.append((f'exact captured spawn {ordinal}', f'SELECT COUNT(*)={count} FROM creature WHERE '
                       + ' AND '.join(predicates)))
    checks += [('native destination spawn GUIDs', f'SELECT COUNT(*)={len(spawns)} FROM creature '
                'WHERE id IN (' + integers(ACTORS) + ') AND guid>@hs_alliance_priest_before_spawn_guid')]
    copied = ('level_min', 'level_max', 'health_min', 'health_max', 'mana_min', 'mana_max',
              'armor', 'speed_walk', 'speed_run', 'detection_range', 'call_for_help_range',
              'dmg_min', 'dmg_max', 'attack_power', 'base_attack_time', 'ranged_attack_time',
              'unit_class', 'type', 'regeneration')
    actors = (
        (919578, 'Priestess Alenya Moonbrook', 'Hearthstone teacher', 2200, 3, 65080),
        (919579, 'Captain Syrelle Starbough', 'Sentinel captain', 2306, 1, 65081),
        (919580, 'HS Priest Syrelle Account Heard', '', 2306, 0, 0),
        (919581, 'HS Priest Account Reported', '', 2200, 0, 0),
    )
    for entry, name, subname, display, flags, menu in actors:
        expected = {
            'entry': str(entry), 'name': quoted(name), 'subname': quoted(subname),
            'display_id1': str(display), 'faction': '79', 'npc_flags': str(flags),
            'gossip_menu_id': str(menu), 'movement_type': '0', 'phase_quest_id': '0',
            'auras': "''", 'script_name': "''",
            'unit_flags': '33554432' if flags == 0 else 'b.unit_flags',
        }
        expected.update({field: 'b.`' + field + '`' for field in copied})
        predicates = [f't.`{field}` <=> {expected.get(field, "DEFAULT(t.`" + field + "`)")}'
                      for field in fields['creature_template']]
        checks.append((f'full actor {entry}', 'SELECT COUNT(*)=1 FROM creature_template t '
                       'JOIN hs_alliance_priest_seed_creature_template b ON b.entry=919502 '
                       f'WHERE t.entry={entry} AND ' + ' AND '.join(predicates)))
    item_expected = {
        'entry': '919651', 'class': '12', 'subclass': '0', 'name': quoted("Maerith's Cloak Clasp"),
        'display_id': '7366', 'quality': '1', 'flags': '0', 'buy_price': '0', 'sell_price': '0',
        'inventory_type': '0', 'allowable_class': '-1', 'allowable_race': '-1', 'item_level': '1',
        'required_level': '0', 'max_count': '1', 'stackable': '1', 'bonding': '4',
        'description': quoted('A worn cloak clasp with a small notch beside the hinge.'),
        'start_quest': '0', 'script_name': "''",
    }
    predicates = [f'`{field}` <=> {item_expected.get(field, "DEFAULT(`" + field + "`)")}'
                  for field in fields['item_template']]
    checks.append(('full native quest clasp', 'SELECT COUNT(*)=1 FROM item_template WHERE entry=919651 AND '
                   + ' AND '.join(predicates)))
    for menu, text in MENUS.items():
        checks.append((f'menu {menu}', 'SELECT COUNT(*)=1 FROM gossip_menu '
                       f'WHERE entry={menu} AND text_id={text} AND script_id=0 AND condition_id=0'))
    for text in TEXTS:
        predicates = [f'BroadcastTextID{slot}=0 AND Probability{slot}=0' for slot in range(1, 8)]
        checks.append((f'npc text {text}', 'SELECT COUNT(*)=1 FROM npc_text '
                       f'WHERE ID={text} AND BroadcastTextID0={text} AND Probability0=1 AND '
                       + ' AND '.join(predicates)))
    quests = ((919861, 'The Last Order', 919714, 919862, (919580, 919581)),
              (919862, 'What She Brought Back', 919861, 919863, ()),
              (919863, 'After the Watch', 919862, 0, ()))
    labels = {
        919861: ("Hear Syrelle's account", 'Report to Alenya', '', ''),
        919862: ('', '', '', ''),
        919863: ('Defeat Alenya at Hearthstone', '', '', ''),
    }
    for quest, title, previous, following, credits in quests:
        final = quest == 919863
        delivery = quest == 919862
        expected = {
            'entry': str(quest), 'Title': quoted(title), 'Method': '2', 'ZoneOrSort': '-9100',
            'MinLevel': '10', 'QuestLevel': '0', 'RequiredRaces': '589', 'RequiredClasses': '0',
            'QuestFlags': '0', 'SpecialFlags': str(2 if final else 0), 'PrevQuestId': str(previous),
            'NextQuestInChain': str(following), 'RewXP': '500', 'RewOrReqMoney': '0',
            'RewMoneyMaxLevel': '0', 'RewItemId1': str(919624 if final else 0),
            'RewItemCount1': str(int(final)), 'SrcItemId': '0', 'SrcItemCount': '0', 'SrcSpell': '0',
        }
        for slot in range(1, 5):
            credit = credits[slot - 1] if slot <= len(credits) else 0
            item = 919651 if delivery and slot == 1 else 0
            expected.update({f'ReqCreatureOrGOId{slot}': str(credit),
                             f'ReqCreatureOrGOCount{slot}': str(int(bool(credit))),
                             f'ReqItemId{slot}': str(item), f'ReqItemCount{slot}': str(int(bool(item))),
                             f'ObjectiveText{slot}': quoted(labels[quest][slot - 1])})
            expected.update({f'{field}{slot}': '0' for field in ('ReqSourceId', 'ReqSourceCount', 'ReqSpellCast')})
            if slot > 1:
                expected.update({f'RewItemId{slot}': '0', f'RewItemCount{slot}': '0'})
        expected.update({f'{field}{slot}': '0' for slot in range(1, 7)
                         for field in ('RewChoiceItemId', 'RewChoiceItemCount')})
        expected.update({f'{field}{slot}': '0' for slot in range(1, 6)
                         for field in ('RewRepFaction', 'RewRepValue')})
        narrative = {'Details', 'Objectives', 'RequestItemsText', 'OfferRewardText'}
        predicates = [f'LENGTH(`{field}`)>0' if field in narrative else
                      f'`{field}` <=> {expected.get(field, "DEFAULT(`" + field + "`)")}'
                      for field in fields['quest_template']]
        checks.append((f'full quest {quest}', 'SELECT COUNT(*)=1 FROM quest_template '
                       f'WHERE entry={quest} AND ' + ' AND '.join(predicates)))
        for relation in ('creature_questrelation', 'creature_involvedrelation'):
            checks += [(f'{relation} teacher {quest}', f'SELECT COUNT(*)=1 FROM {relation} '
                        f'WHERE quest={quest} AND id=919578'),
                       (f'{relation} no other actor {quest}', f'SELECT COUNT(*)=1 FROM {relation} '
                        f'WHERE quest={quest}')]
    additions = {'creature_template': 4, 'gossip_menu': 17, 'npc_text': 21, 'broadcast_text': 21,
                 'quest_template': 3, 'creature_questrelation': 3, 'creature_involvedrelation': 3,
                 'hearthstone_npc_unlock': 1, 'item_template': 1, 'creature': len(spawns)}
    for table in owned:
        seed = 'hs_alliance_priest_seed_' + table
        key = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in keys[table])
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        first_key = keys[table][0]
        checks += [
            (f'preserve every unrelated row {table}', f'SELECT COUNT(*)=0 FROM `{seed}` b '
             f'LEFT JOIN `{table}` t ON {key} WHERE t.`{first_key}` IS NULL OR NOT ({comparison})'),
            (f'count {table}', f'SELECT (SELECT COUNT(*) FROM `{table}`)='
             f'(SELECT COUNT(*) FROM `{seed}`)+{additions.get(table, 0)}'),
        ]
    # Snapshot run one and compare every field and row after run two. Native
    # destination GUIDs for the two actors may change; exact captured non-GUID
    # fields are independently checked after each run. Unrelated rows stay exact.
    snapshot = []
    for table in owned:
        first = 'hs_alliance_priest_first_' + table
        snapshot += [f'CREATE TEMPORARY TABLE `{first}` LIKE `{table}`;',
                     f'INSERT INTO `{first}` SELECT * FROM `{table}`;']
    assertions = '\n'.join(f'SELECT {quoted(label)}, ({sql});' for label, sql in checks)
    rerun_checks = []
    for table in owned:
        first = 'hs_alliance_priest_first_' + table
        key = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in keys[table])
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        first_key = keys[table][0]
        unchanged = f'b.id NOT IN ({integers(ACTORS)}) AND ' if table == 'creature' else ''
        rerun_checks += [
            (f'rerun fields {table}', f'SELECT COUNT(*)=0 FROM `{first}` b LEFT JOIN `{table}` t ON {key} '
             f'WHERE {unchanged}(t.`{first_key}` IS NULL OR NOT ({comparison}))'),
            (f'rerun count {table}', f'SELECT (SELECT COUNT(*) FROM `{table}`)=(SELECT COUNT(*) FROM `{first}`)'),
        ]
    rerun_assertions = '\n'.join(f'SELECT {quoted(label)}, ({sql});' for label, sql in rerun_checks)
    allocate = 'SET @hs_alliance_priest_before_spawn_guid=IFNULL((SELECT MAX(guid) FROM creature),0);\n'
    output = query('\n'.join(setup) + '\n' + allocate + migration + '\n' + assertions + '\n'
                   + '\n'.join(snapshot) + '\n' + allocate + migration + '\n' + assertions + '\n' + rerun_assertions)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks) * 2 + len(rerun_checks) or failures:
        raise SystemExit('Temporary SQL assertions failed: ' + repr(failures or rows))
    registry = json.loads((root / 'data/quests/class_unlocks.json').read_text())['npcs']
    matching = [npc for npc in registry if npc['entry'] == 919578]
    if len(matching) != 1 or (matching[0]['quest'], matching[0]['quest_title'], matching[0]['deck_key'],
                            matching[0]['faction']) != (919863, 'After the Watch', 'normal_priest', 'alliance'):
        raise SystemExit('Alliance Priest teacher registry mismatch')
    if any(npc['entry'] in (919579, 919580, 919581) for npc in registry):
        raise SystemExit('Supporting Priest actors must not enter the opponent roster')
    placements = f'{len(spawns)} captured spawns, duplicate replacement and native GUIDs verified. '
    print(f'Alliance Priest migration passed {len(rows)} assertions over two temporary runs / '
          f'{len(owned)} full schemas. Every unrelated row and rerun field preserved. ' + placements +
          f'Native DBC models/icon verified. Registry matches. All {len(owned) + len(related)} '
          'live checksums unchanged.')


if __name__ == '__main__':
    main()
