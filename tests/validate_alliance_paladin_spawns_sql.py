#!/usr/bin/env python3
"""Validate the exported Alliance Paladin spawns twice in temporary tables only."""
import argparse
from decimal import Decimal
import os
from pathlib import Path
import re
import subprocess


# Independent full-precision read-only snapshot from tw_world:3307, 2026-10-08.
SPAWNS = (
    (919570, 0, -8643.48046875, 753.8499755859375, 97.26799774169922, 1.2381399869918823),
    (919571, 0, -8644.259765625, 754.7059936523438, 97.26789855957031, 0.6828640103340149),
    (919577, 0, -8560.3896484375, 371.2120056152344, 108.38600158691406, 3.718440055847168),
)
ACTORS = (919570, 919571, 919577)
COLUMNS = ('id', 'id2', 'id3', 'id4', 'map', 'position_x', 'position_y', 'position_z',
           'orientation', 'spawntimesecsmin', 'spawntimesecsmax', 'wander_distance',
           'health_percent', 'mana_percent', 'movement_type', 'spawn_flags', 'visibility_mod')
TRACKED = ('creature', 'creature_addon', 'creature_movement', 'creature_movement_special',
           'creature_movement_template', 'creature_groups', 'creature_linking',
           'creature_linking_template', 'game_event_creature', 'game_event_creature_data',
           'pool_creature', 'pool_creature_template')


def parse_migration(source):
    """Allow only the exact scoped DELETE and a numeric, unqualified INSERT."""
    # Block comments, client directives, quoted strings and schema-qualified tables
    # cannot match this grammar. In particular, executable /*!...*/ is rejected.
    code = re.sub(r'--[^\r\n]*', ' ', source)
    statements = [statement.strip() for statement in code.split(';')]
    if len(statements) != 3 or statements[-1]:
        raise SystemExit('Expected exactly two semicolon-terminated spawn statements')
    delete = re.fullmatch(r'DELETE\s+FROM\s+`?creature`?\s+WHERE\s+`?id`?\s+IN\s*'
                          r'\(\s*(\d+\s*(?:,\s*\d+\s*)*)\)', statements[0], re.I)
    if not delete or tuple(sorted(map(int, delete.group(1).split(',')))) != ACTORS:
        raise SystemExit('Only the three Paladin actor entries may be deleted')
    insert = re.fullmatch(r'INSERT\s+INTO\s+`?creature`?\s*\(([^)]+)\)\s+VALUES\s+(.+)',
                          statements[1], re.I | re.S)
    if not insert:
        raise SystemExit('Only an unqualified numeric creature INSERT is allowed')
    columns = [column.strip() for column in insert.group(1).split(',')]
    if any(not re.fullmatch(r'`?[A-Za-z_]\w*`?', column) for column in columns):
        raise SystemExit('Invalid spawn column names')
    if tuple(column.strip('`').lower() for column in columns) != COLUMNS:
        raise SystemExit('Spawn columns must contain every captured field and omit GUID')
    number = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
    row = r'\(\s*' + number + r'(?:\s*,\s*' + number + r'){16}\s*\)'
    if not re.fullmatch(row + r'(?:\s*,\s*' + row + r'){2}', insert.group(2)):
        raise SystemExit('Expected exactly three rows of 17 numeric spawn literals')
    captured = {}
    for values in re.findall(r'\(([^)]+)\)', insert.group(2)):
        values = tuple(Decimal(value.strip()) for value in values.split(','))
        entry = int(values[0])
        if values[0] != entry or entry in captured:
            raise SystemExit('Invalid or duplicate actor entry')
        captured[entry] = values
    for entry, map_id, x, y, z, orientation in SPAWNS:
        expected = (entry, 0, 0, 0, map_id, x, y, z, orientation, 120, 120, 0, 100, 100, 0, 0, 0)
        if captured.get(entry) != tuple(Decimal(str(value)) for value in expected):
            raise SystemExit('Captured spawn state differs for entry ' + str(entry))
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    migration = parse_migration((root / 'data/sql/world/0038_hearthstone_alliance_paladin_spawns.sql')
                                .read_text(encoding='utf-8-sig'))
    setting = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                        args.config.read_text(encoding='utf-8-sig'), re.M)
    if not setting:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = setting.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password

    def query(sql):
        result = subprocess.run([str(args.client), '--protocol=TCP', '--host=' + host, '--port=' + port,
                                 '--user=' + user, '--database=' + database, '--batch', '--skip-column-names'],
                                input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    tracked = query('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() '
                    'AND TABLE_NAME IN (' + ','.join("'" + table + "'" for table in TRACKED) + ') '
                    'ORDER BY TABLE_NAME;').splitlines()
    if 'creature' not in tracked:
        raise SystemExit('Native creature table missing')
    checksum_sql = 'CHECKSUM TABLE ' + ','.join('`' + table + '`' for table in tracked) + ';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tracked) or any(row.endswith('\tNULL') for row in before.splitlines()):
        raise SystemExit('Incomplete live checksums; migration not executed')
    columns = [row.split('\t')[0] for row in query('SHOW COLUMNS FROM creature;').splitlines()]
    if set(columns) != set(COLUMNS) | {'guid'}:
        raise SystemExit('Native creature schema differs from captured spawn fields')
    setup = '''
CREATE TEMPORARY TABLE hs_paladin_spawn_seed LIKE creature;
INSERT INTO hs_paladin_spawn_seed SELECT * FROM creature
WHERE id BETWEEN 919559 AND 919579 OR id IN (461,1305,3325,3326,3335,6122,6374,62100);
CREATE TEMPORARY TABLE creature LIKE hs_paladin_spawn_seed;
INSERT INTO creature SELECT * FROM hs_paladin_spawn_seed;
-- Existing actor duplicates must be replaced; credit and retired entries must survive.
INSERT INTO creature (id,map,position_x,position_y,position_z,orientation)
VALUES (919570,1,1,2,3,4),(919571,1,1,2,3,4),(919577,1,1,2,3,4),
       (1,0,11,12,13,1),(919572,0,21,22,23,2),(919573,0,31,32,33,3),
       (919574,0,41,42,43,4),(919575,0,51,52,53,5),(919576,0,61,62,63,6);
CREATE TEMPORARY TABLE hs_paladin_unrelated_seed LIKE creature;
INSERT INTO hs_paladin_unrelated_seed SELECT * FROM creature WHERE id NOT IN (919570,919571,919577);
'''
    comparison = ' AND '.join(f't.`{column}` <=> b.`{column}`' for column in columns)
    checks = [
        ('unrelated full rows preserved', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_paladin_unrelated_seed) '
         'FROM creature t JOIN hs_paladin_unrelated_seed b ON ' + comparison),
        ('exact scoped row count', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_paladin_unrelated_seed)+3 FROM creature'),
        ('one of each actor', 'SELECT COUNT(*)=3 FROM (SELECT id FROM creature WHERE id IN (919570,919571,919577) '
         'GROUP BY id HAVING COUNT(*)=1) actors'),
        ('captured non-coordinate state', 'SELECT COUNT(*)=3 FROM creature WHERE id IN (919570,919571,919577) '
         'AND id2=0 AND id3=0 AND id4=0 AND spawntimesecsmin=120 AND spawntimesecsmax=120 '
         'AND wander_distance=0 AND health_percent=100 AND mana_percent=100 AND movement_type=0 '
         'AND spawn_flags=0 AND visibility_mod=0'),
        ('destination GUID allocation', 'SELECT COUNT(*)=3 FROM creature WHERE id IN (919570,919571,919577) '
         'AND guid>@hs_paladin_before_guid'),
        ('no new retired or credit spawns', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_paladin_unrelated_seed '
         'WHERE id BETWEEN 919572 AND 919576) FROM creature WHERE id BETWEEN 919572 AND 919576'),
    ]
    for entry, map_id, x, y, z, orientation in SPAWNS:
        checks.append((f'exact captured placement {entry}', f'SELECT COUNT(*)=1 FROM creature WHERE id={entry} '
                       f'AND map={map_id} AND position_x={x} AND position_y={y} AND position_z={z} AND orientation={orientation}'))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    run = 'SET @hs_paladin_before_guid=(SELECT MAX(guid) FROM creature);\n' + migration + '\n' + assertions + '\n'
    output = query(setup + '\n' + run * 2)
    if before != query(checksum_sql):
        raise SystemExit('Live table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if failures or len(rows) != len(checks) * 2:
        raise SystemExit('Temporary Paladin spawn validation failed: ' + repr(failures or rows))
    print(f'Alliance Paladin spawns: {len(rows)} assertions passed over two temporary runs; '
          f'duplicates replaced, unrelated rows preserved, all {len(tracked)} live-table checksums unchanged.')


if __name__ == '__main__':
    main()
