#!/usr/bin/env python3
"""Validate the exported Warlock spawns twice using only temporary tables."""
import argparse
import os
from pathlib import Path
import re
import subprocess


# Independent full-precision snapshot read from tw_world:3307 on 2026-10-08.
SPAWNS = (
    (919559, 1, 1855.1800537109375, -4354.330078125, -14.996199607849121, 3.275049924850464),
    (919560, 1, 1800.280029296875, -4376.64013671875, -17.64069938659668, 1.4914100170135498),
    (919561, 0, -8969.919921875, 1037.489990234375, 101.40399932861328, 2.215169906616211),
    (919562, 0, -8971.58984375, 1040, 101.40399932861328, 5.364620208740234),
    (919563, 0, -8950.9599609375, 994.7780151367188, 122.0260009765625, 3.7663400173187256),
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
    migration = (root/'data/sql/world/0035_hearthstone_warlock_spawns.sql').read_text()
    touched = set(re.findall(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', migration, re.I))
    if touched != {'creature'}:
        raise SystemExit('Unexpected migration tables: '+repr(touched))
    tracked = ('creature', 'creature_addon', 'creature_movement', 'creature_movement_template',
               'creature_groups', 'creature_linking')
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tracked)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tracked) or any(line.endswith('\tNULL') for line in before.splitlines()):
        raise SystemExit('Incomplete live checksums; migration not executed')
    setup = '''
CREATE TEMPORARY TABLE hs_warlock_spawn_seed LIKE creature;
INSERT INTO hs_warlock_spawn_seed SELECT * FROM creature
WHERE id BETWEEN 919559 AND 919569 OR id IN (461,1305,3325,3326,3335,6122);
CREATE TEMPORARY TABLE creature LIKE hs_warlock_spawn_seed;
INSERT INTO creature SELECT * FROM hs_warlock_spawn_seed;
-- Duplicate each actor and introduce an unrelated spawn before the first pass.
INSERT INTO creature (id,map,position_x,position_y,position_z,orientation)
VALUES (919559,0,1,2,3,4),(919560,0,1,2,3,4),(919561,1,1,2,3,4),
       (919562,1,1,2,3,4),(919563,1,1,2,3,4),(1,0,11,12,13,1);
SET @hs_warlock_unrelated_guid=(SELECT guid FROM creature WHERE id=1);
'''
    columns = [line.split('\t')[0] for line in query('SHOW COLUMNS FROM creature;').splitlines()]
    comparison = ' AND '.join(f't.`{column}` <=> b.`{column}`' for column in columns)
    checks = [
        ('preserve unrelated full spawn rows',
         'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_warlock_spawn_seed WHERE id NOT BETWEEN 919559 AND 919563) '
         'FROM creature t JOIN hs_warlock_spawn_seed b ON '+comparison+' WHERE b.id NOT BETWEEN 919559 AND 919563'),
        ('exact scoped row count', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_warlock_spawn_seed '
         'WHERE id NOT BETWEEN 919559 AND 919563)+6 FROM creature'),
        ('one of each actor', 'SELECT COUNT(*)=5 FROM (SELECT id FROM creature WHERE id BETWEEN 919559 AND 919563 '
         'GROUP BY id HAVING COUNT(*)=1) actors'),
        ('captured non-coordinate state', 'SELECT COUNT(*)=5 FROM creature WHERE id BETWEEN 919559 AND 919563 '
         'AND id2=0 AND id3=0 AND id4=0 AND spawntimesecsmin=120 AND spawntimesecsmax=120 '
         'AND wander_distance=0 AND health_percent=100 AND mana_percent=100 AND movement_type=0 '
         'AND spawn_flags=0 AND visibility_mod=0'),
        ('destination GUID allocation', 'SELECT COUNT(*)=5 FROM creature WHERE id BETWEEN 919559 AND 919563 '
         'AND guid>0 AND guid NOT IN (12585241,12585243,12585244,12585245,12585246)'),
        ('unrelated spawn preserved', 'SELECT COUNT(*)=1 FROM creature WHERE id=1 AND guid=@hs_warlock_unrelated_guid '
         'AND map=0 AND position_x=11 AND position_y=12 AND position_z=13 AND orientation=1'),
        ('no objective credit spawns', 'SELECT COUNT(*)=0 FROM creature WHERE id BETWEEN 919564 AND 919569'),
    ]
    for entry, map_id, x, y, z, orientation in SPAWNS:
        checks.append((f'exact captured placement {entry}', f'SELECT COUNT(*)=1 FROM creature WHERE id={entry} '
                       f'AND map={map_id} AND position_x={x} AND position_y={y} AND position_z={z} AND orientation={orientation}'))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query(setup+'\n'+(migration+'\n'+assertions+'\n')*2)
    if before != query(checksum_sql):
        raise SystemExit('Live table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if failures or len(rows) != len(checks)*2:
        raise SystemExit('Temporary spawn validation failed: '+repr(failures or rows))
    print(f'Warlock spawns: {len(rows)} assertions passed over two temporary runs; '
          'duplicates replaced, unrelated rows preserved, all six live-table checksums unchanged.')


if __name__ == '__main__':
    main()
