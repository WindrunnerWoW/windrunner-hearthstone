#!/usr/bin/env python3
"""Validate Rogue spawn extraction twice using only connection-local temporary tables."""
import argparse
import os
from pathlib import Path
import re
import subprocess


# Exact stored positions read from tw_world:3307 on 2026-10-05, independent of migration text.
EXPECTED = (
    (919522, 1798.7900390625, -4267.58984375, 7.437290191650391, 2.934999942779541),
    (919523, 2018.6800537109375, -4575.52001953125, 46.41130065917969, 5.296679973602295),
    (919524, 1682.7900390625, -4271.14013671875, 32.867401123046875, 4.160980224609375),
    (919525, 2120.580078125, -4764.259765625, 50.407798767089844, 3.800489902496338),
    (919526, 1835.02001953125, -4565.10009765625, 23.530000686645508, 1.9013899564743042),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--port', type=int, help='Override the configured database port')
    args = parser.parse_args()
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                      args.config.read_text(encoding='utf-8-sig'), re.M)
    if not match:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = match.group(1).split(';')
    port = str(args.port) if args.port is not None else port
    root = Path(__file__).resolve().parents[1]
    migration = (root / 'data/sql/world/0024_hearthstone_horde_rogue_spawns.sql').read_text()
    # Shadow before any mutation. Keep the captured live rows in a separate temporary
    # snapshot to compare all fields; no persistent schema, spawn or GUID allocation changes.
    setup = '''
CREATE TEMPORARY TABLE hs_rogue_spawn_seed LIKE creature;
INSERT INTO hs_rogue_spawn_seed SELECT * FROM creature WHERE id BETWEEN 919522 AND 919526;
CREATE TEMPORARY TABLE creature LIKE hs_rogue_spawn_seed;
INSERT INTO creature SELECT * FROM hs_rogue_spawn_seed;
-- Duplicated local actors must be replaced, without touching unrelated rows.
INSERT INTO creature (id,map,position_x,position_y,position_z,orientation)
VALUES (919522,0,1,2,3,4),(1,0,11,12,13,1);
SET @unrelated_guid=(SELECT guid FROM creature WHERE id=1);
'''
    checks = [
        ('exactly five actors', 'SELECT COUNT(*)=5 FROM creature WHERE id BETWEEN 919522 AND 919526'),
        ('one spawn per actor', 'SELECT COUNT(*)=5 FROM (SELECT id FROM creature WHERE id BETWEEN 919522 AND 919526 GROUP BY id HAVING COUNT(*)=1) actors'),
        ('stationary map respawn state preserved', 'SELECT COUNT(*)=5 FROM creature WHERE id BETWEEN 919522 AND 919526 AND map=1 AND id2=0 AND id3=0 AND id4=0 AND spawntimesecsmin=120 AND spawntimesecsmax=120 AND wander_distance=0 AND health_percent=100 AND mana_percent=100 AND movement_type=0 AND spawn_flags=0 AND visibility_mod=0'),
        ('unrelated spawn and guid preserved', 'SELECT COUNT(*)=1 FROM creature WHERE id=1 AND guid=@unrelated_guid AND map=0 AND position_x=11 AND position_y=12 AND position_z=13 AND orientation=1'),
        ('no objective credit spawns', 'SELECT COUNT(*)=0 FROM creature WHERE id BETWEEN 919527 AND 919531'),
        ('destination guid allocation remains local', 'SELECT COUNT(*)=5 FROM creature WHERE id BETWEEN 919522 AND 919526 AND guid>0'),
    ]
    for entry, x, y, z, orientation in EXPECTED:
        checks.append((f'exact extracted position {entry}',
                       'SELECT COUNT(*)=1 FROM creature WHERE '
                       f'id={entry} AND ABS(position_x-({x}))<0.000001 '
                       f'AND ABS(position_y-({y}))<0.000001 AND ABS(position_z-({z}))<0.000001 '
                       f'AND ABS(orientation-({orientation}))<0.000001'))
    assertions = '\n'.join(f"SELECT '{label}', ({query});" for label, query in checks)
    sql = setup + '\n' + (migration + '\n' + assertions + '\n') * 2
    env = os.environ.copy()
    env['MYSQL_PWD'] = password
    def run(query):
        result = subprocess.run([str(args.client), '--protocol=TCP', '--host='+host, '--port='+port,
                                 '--user='+user, '--database='+database, '--batch', '--skip-column-names'],
                                input=query, text=True, capture_output=True, env=env)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    # Use fresh connections: even a same-schema-qualified name resolves to the
    # temporary shadow while that connection is open. Compare all source fields/GUIDs.
    snapshot = '''SELECT guid,id,id2,id3,id4,map,CAST(position_x AS DOUBLE),CAST(position_y AS DOUBLE),
CAST(position_z AS DOUBLE),CAST(orientation AS DOUBLE),spawntimesecsmin,spawntimesecsmax,
CAST(wander_distance AS DOUBLE),CAST(health_percent AS DOUBLE),CAST(mana_percent AS DOUBLE),
movement_type,spawn_flags,CAST(visibility_mod AS DOUBLE)
FROM creature WHERE id BETWEEN 919522 AND 919526 ORDER BY guid;'''
    before = run(snapshot)
    rows = run(sql).strip().splitlines()
    after = run(snapshot)
    if before != after:
        raise SystemExit('Live spawn records changed during validation')
    failures = [line for line in rows if not line.endswith('\t1')]
    if failures or len(rows) != len(checks)*2:
        raise SystemExit('Temporary spawn SQL validation failed: ' + repr(failures))
    print(f'Rogue spawns: {len(rows)+1} assertions passed across two temporary-table runs '
          f'on port {port}; live rows unchanged.')


if __name__ == '__main__':
    main()
