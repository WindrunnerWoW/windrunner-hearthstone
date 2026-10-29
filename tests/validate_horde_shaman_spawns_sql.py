#!/usr/bin/env python3
"""Validate Annie's exported spawns/route twice using only temporary tables."""
import argparse
import os
from pathlib import Path
import re
import subprocess


# Independent full-precision snapshot read from tw_world:3307 on 2026-10-08.
SPAWNS = (
    (919552, -1102.2900390625, 117.27799987792969, 152.76499938964844, 0.9399710297584534),
    (919553, -942.6309814453125, 283.875, 135.74600219726562, 1.082129955291748),
)
POINTS = (
    (1, -1106.3184814453125, 114.64447784423828, 152.65628051757812),
    (2, -1109.6396484375, 105.42436981201172, 151.70179748535156),
    (3, -1116.23828125, 106.95344543457031, 151.70179748535156),
    (4, -1122.35009765625, 90.43563842773438, 143.4742431640625),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    args = parser.parse_args()
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                      args.config.read_text(encoding='utf-8-sig'), re.M)
    if not match:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = match.group(1).split(';')
    env = os.environ.copy()
    env['MYSQL_PWD'] = password

    def query(sql):
        result = subprocess.run([str(args.client), '--protocol=TCP', '--host='+host, '--port='+port,
                                 '--user='+user, '--database='+database, '--batch', '--skip-column-names'],
                                input=sql, text=True, capture_output=True, env=env)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migration = (root/'data/sql/world/0034_hearthstone_horde_shaman_spawns.sql').read_text()
    tables = {'creature', 'creature_movement_template'}
    touched = set(re.findall(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', migration, re.I))
    if touched != tables:
        raise SystemExit('Unexpected migration tables: '+repr(touched))
    tracked = sorted(tables | {'creature_addon', 'creature_movement', 'creature_groups', 'creature_linking'})
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tracked)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tracked) or any(line.endswith('\tNULL') for line in before.splitlines()):
        raise SystemExit('Incomplete live checksums; migration not executed')
    setup = []
    checks = []
    for table, predicate, key in (
        ('creature', 'id IN (919552,919553,3030,3031,3032)', 'id'),
        ('creature_movement_template', 'entry IN (919552,919553,3030,3031,3032)', 'entry'),
    ):
        seed = 'hs_annie_spawn_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']
        columns = query(f'SHOW COLUMNS FROM `{table}`;').splitlines()
        comparison = ' AND '.join(f't.`{line.split(chr(9))[0]}` <=> b.`{line.split(chr(9))[0]}`' for line in columns)
        owned = '(919552,919553)' if key == 'id' else '(919552)'
        checks.append((f'preserve unrelated {table} fields',
            f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}` WHERE `{key}` NOT IN {owned}) '
            f'FROM `{table}` t JOIN `{seed}` b ON {comparison} WHERE b.`{key}` NOT IN {owned}'))
        addition = 3 if key == 'id' else 5  # Export plus one synthetic unrelated row.
        checks.append((f'exact scoped count {table}',
            f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}` WHERE `{key}` NOT IN {owned})+{addition} FROM `{table}`'))
    setup += [
        'INSERT INTO creature (id,map,position_x,position_y,position_z,orientation) '
        'VALUES (919552,1,1,2,3,4),(919553,1,4,3,2,1),(1,0,11,12,13,1);',
        'SET @hs_annie_unrelated_guid=(SELECT guid FROM creature WHERE id=1);',
        'INSERT INTO creature_movement_template (entry,point,position_x,position_y,position_z) '
        'VALUES (919552,99,1,2,3),(1,1,21,22,23);',
    ]
    checks += [
        ('exactly one of each actor', 'SELECT COUNT(*)=2 FROM (SELECT id FROM creature WHERE id IN '
         '(919552,919553) GROUP BY id HAVING COUNT(*)=1) actors'),
        ('spawn state preserved', 'SELECT COUNT(*)=2 FROM creature WHERE id IN (919552,919553) '
         'AND map=1 AND id2=0 AND id3=0 AND id4=0 AND spawntimesecsmin=120 AND spawntimesecsmax=120 '
         'AND wander_distance=0 AND health_percent=100 AND mana_percent=100 AND movement_type=0 '
         'AND spawn_flags=0 AND visibility_mod=0'),
        ('local GUID allocation', 'SELECT COUNT(*)=2 FROM creature WHERE id IN (919552,919553) '
         'AND guid>0 AND guid NOT IN (12585236,12585235)'),
        ('unrelated spawn preserved', 'SELECT COUNT(*)=1 FROM creature WHERE id=1 '
         'AND guid=@hs_annie_unrelated_guid AND map=0 AND position_x=11 AND position_y=12 AND position_z=13 AND orientation=1'),
        ('four-point native route', 'SELECT COUNT(*)=4 FROM creature_movement_template WHERE entry=919552 '
         'AND point BETWEEN 1 AND 4 AND orientation=100 AND waittime=0 AND wander_distance=0 AND script_id=0'),
        ('obsolete route point removed', 'SELECT COUNT(*)=0 FROM creature_movement_template WHERE entry=919552 AND point=99'),
        ('unrelated route preserved', 'SELECT COUNT(*)=1 FROM creature_movement_template WHERE entry=1 '
         'AND point=1 AND position_x=21 AND position_y=22 AND position_z=23'),
        ('no bookkeeping spawns', 'SELECT COUNT(*)=0 FROM creature WHERE id BETWEEN 919554 AND 919558'),
    ]
    for entry, x, y, z, orientation in SPAWNS:
        checks.append((f'exact captured spawn {entry}', f'SELECT COUNT(*)=1 FROM creature WHERE id={entry} '
            f'AND position_x={x} AND position_y={y} AND position_z={z} AND orientation={orientation}'))
    for point, x, y, z in POINTS:
        checks.append((f'exact captured waypoint {point}',
            f'SELECT COUNT(*)=1 FROM creature_movement_template WHERE entry=919552 AND point={point} '
            f'AND position_x={x} AND position_y={y} AND position_z={z}'))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query('\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2)
    if before != query(checksum_sql):
        raise SystemExit('Live table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if failures or len(rows) != len(checks)*2:
        raise SystemExit('Temporary spawn/waypoint validation failed: '+repr(failures or rows))
    print(f'Annie spawns/waypoints: {len(rows)} assertions passed over two temporary runs; '
          'duplicates and obsolete points replaced; all six live-table checksums unchanged.')


if __name__ == '__main__':
    main()
