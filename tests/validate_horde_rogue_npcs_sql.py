#!/usr/bin/env python3
"""Validate migration 0022 twice against connection-local temporary tables only."""
import argparse
import os
from pathlib import Path
import re
import subprocess


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
    migration = (Path(__file__).resolve().parents[1] /
                 'data/sql/world/0022_hearthstone_horde_rogue_npcs.sql').read_text()
    setup = [
        'CREATE TEMPORARY TABLE hs_rogue_npc_seed LIKE creature_template;',
        'INSERT INTO hs_rogue_npc_seed SELECT * FROM creature_template WHERE entry=919501;',
        'CREATE TEMPORARY TABLE creature_template LIKE hs_rogue_npc_seed;',
        'INSERT INTO creature_template SELECT * FROM hs_rogue_npc_seed;',
        'CREATE TEMPORARY TABLE hs_rogue_creature_seed LIKE creature;',
        'CREATE TEMPORARY TABLE creature LIKE hs_rogue_creature_seed;',
        'DROP TEMPORARY TABLE hs_rogue_creature_seed;',
        'CREATE TEMPORARY TABLE hs_rogue_gameobject_seed LIKE gameobject;',
        'CREATE TEMPORARY TABLE gameobject LIKE hs_rogue_gameobject_seed;',
        'DROP TEMPORARY TABLE hs_rogue_gameobject_seed;',
    ]
    fields = ('level_min', 'level_max', 'health_min', 'health_max', 'armor',
              'speed_walk', 'speed_run', 'detection_range', 'call_for_help_range',
              'dmg_min', 'dmg_max', 'attack_power', 'base_attack_time',
              'ranged_attack_time', 'unit_class', 'unit_flags', 'type', 'regeneration')
    baseline = ' AND '.join(f't.{field}=b.{field}' for field in fields)
    checks = [
        ('teacher baseline exists', 'SELECT COUNT(*)=1 FROM hs_rogue_npc_seed'),
        ('Forsen identity', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919522 "
         "AND name='For''sen' AND display_id1=4070 AND npc_flags=3 "
         "AND subname='Hearthstone enthusiast'"),
        ('Gora identity', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919523 "
         "AND name='Gora' AND display_id1=3128 AND npc_flags=1 AND subname='Card courier'"),
        ('copied teacher stats', 'SELECT COUNT(*)=2 FROM creature_template t '
         'JOIN hs_rogue_npc_seed b ON b.entry=919501 WHERE t.entry IN (919522,919523) AND ' + baseline),
        ('friendly stationary native gossip', 'SELECT COUNT(*)=2 FROM creature_template '
         "WHERE entry IN (919522,919523) AND faction=85 AND movement_type=0 "
         "AND gossip_menu_id=0 AND script_name=''"),
        ('no extra templates', 'SELECT COUNT(*)=3 FROM creature_template'),
        ('baseline unchanged', 'SELECT COUNT(*)=1 FROM creature_template t '
         'JOIN hs_rogue_npc_seed b ON b.entry=t.entry WHERE t.entry=919501 AND ' + baseline +
         ' AND t.name=b.name AND t.display_id1=b.display_id1 AND t.npc_flags=b.npc_flags '
         'AND t.faction=b.faction AND t.movement_type=b.movement_type '
         'AND t.gossip_menu_id=b.gossip_menu_id AND t.script_name=b.script_name'),
        ('no spawns', 'SELECT (SELECT COUNT(*) FROM creature)=0 '
         'AND (SELECT COUNT(*) FROM gameobject)=0'),
    ]
    assertions = '\n'.join(f"SELECT '{label}', ({query});" for label, query in checks)
    sql = '\n'.join(setup) + '\n' + (migration + '\n' + assertions + '\n') * 2
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    result = subprocess.run([str(args.client), '--host='+host, '--port='+port, '--user='+user,
                             '--database='+database, '--batch', '--skip-column-names'],
                            input=sql, text=True, capture_output=True, env=environment)
    if result.returncode:
        raise SystemExit(result.stderr)
    rows = result.stdout.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary-table validation failed: ' + repr(failures or rows))
    print(f'Migration 0022 passed {len(rows)} assertions over two runs against '
          'three temporary schema copies. Live tables unchanged.')


if __name__ == '__main__':
    main()
