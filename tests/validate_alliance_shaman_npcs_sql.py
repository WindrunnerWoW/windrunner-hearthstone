#!/usr/bin/env python3
"""Validate 0029 twice against shadow TEMPORARY tables; never write live tables."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--port', type=int, help='Owner-supplied validation port override')
    args = parser.parse_args()
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                      args.config.read_text(encoding='utf-8-sig'), re.M)
    if not match:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = match.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--host='+host, '--port='+str(args.port or port),
               '--user='+user, '--database='+database, '--batch', '--skip-column-names']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migration = (root / 'data/sql/world/0029_hearthstone_alliance_shaman_npcs.sql').read_text()
    touched = set(re.findall(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', migration, re.I))
    if touched != {'creature_template', 'creature'}:
        raise SystemExit('Cast setup must change only templates and the John placement')
    if re.search(r'(?:DELETE\s+FROM|UPDATE)\s+`?creature`?\s', migration, re.I):
        raise SystemExit('Existing placements must be preserved')
    tables = {
        'creature_template': 'entry=919502',
        'creature': 'guid=(SELECT MIN(guid) FROM creature)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
        'creature_movement': 'id=(SELECT MIN(id) FROM creature_movement)',
        'creature_movement_template': 'entry=(SELECT MIN(entry) FROM creature_movement_template)',
        'creature_movement_special': 'id=(SELECT MIN(id) FROM creature_movement_special)',
        'creature_addon': 'guid=(SELECT MIN(guid) FROM creature_addon)',
    }
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tables)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tables):
        raise SystemExit('Incomplete initial live checksum output')
    names = ','.join("'"+table+"'" for table in tables)
    metadata = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS '
                     'WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('+names+') '
                     'ORDER BY TABLE_NAME,ORDINAL_POSITION;')
    fields = {table: [] for table in tables}
    for row in metadata.splitlines():
        table, column = row.split('\t')
        fields[table].append(column)
    missing = [table for table, columns in fields.items() if not columns]
    if missing:
        raise SystemExit('Incomplete schema metadata: '+repr(missing))
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_shaman_cast_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']
    # Existing setup placement and an owner-made entry route must survive reruns.
    for table, field, value in (('creature','id',919545),
                                ('creature_movement_template','entry',919546)):
        setup += [f'UPDATE `{table}` SET `{field}`={value};',
                  f'UPDATE `hs_shaman_cast_seed_{table}` SET `{field}`={value};']
    copied = ('level_min','level_max','health_min','health_max','armor','speed_walk','speed_run',
              'detection_range','call_for_help_range','dmg_min','dmg_max','attack_power',
              'base_attack_time','ranged_attack_time','unit_class','type','regeneration')
    baseline = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in copied)
    checks = [
        ('teacher baseline exists', 'SELECT COUNT(*)=1 FROM hs_shaman_cast_seed_creature_template'),
        ('copied baseline all actors', 'SELECT COUNT(*)=3 FROM creature_template t '
         'JOIN creature_template b ON b.entry=919502 WHERE t.entry IN (919545,919546,919547) AND '+baseline),
        ('idle friendly selectable cast', "SELECT COUNT(*)=3 FROM creature_template "
         "WHERE entry IN (919545,919546,919547) AND faction=55 AND movement_type=0 "
         "AND gossip_menu_id=IF(entry=919545,65027,0) AND script_name='' AND (unit_flags & 33554432)=0"),
    ]
    cast = ((919545,10184,'John Maloneybrew','Hearthstone researcher',3,0),
            (919546,8929,'Kibler McGriff','Wildhammer visitor',0,2),
            (919547,3898,'Ambassador Krip Parrianhoof','Tauren ambassador',0,2))
    for entry, display, name, subtitle, flags, safety in cast:
        checks.append((f'identity services safety {entry}',
            f"SELECT COUNT(*)=1 FROM creature_template t JOIN creature_template b ON b.entry=919502 "
            f"WHERE t.entry={entry} AND t.display_id1={display} AND t.name='{name}' "
            f"AND t.subname='{subtitle}' AND t.npc_flags={flags} AND t.unit_flags=(b.unit_flags | {safety})"))
    for table in tables:
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        seed = 'hs_shaman_cast_seed_'+table
        checks += [(f'preserve every seed field {table}',
                    f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) FROM `{table}` t '
                    f'JOIN `{seed}` b ON {comparison}'),
                   (f'idempotent exact count {table}',
                    f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`)+{3 if table == "creature_template" else 0} '
                    f'FROM `{table}`')]
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    # Also validate a missing John, with his source GUID occupied by another actor.
    # These mutations target only the already-shadowed temporary creature table.
    fresh_setup = (
        'DELETE FROM creature WHERE id=919545;\n'
        'INSERT INTO creature (guid,id,map,position_x,position_y,position_z) '
        'VALUES (12585232,919502,0,1,2,3);\n')
    fresh_checks = [
        ('fresh John exact placement', 'SELECT COUNT(*)=1 FROM creature WHERE id=919545 '
         'AND id2=0 AND id3=0 AND id4=0 AND map=0 '
         'AND CAST(position_x AS DOUBLE)=-4651.2001953125 '
         'AND CAST(position_y AS DOUBLE)=-1180.8699951171875 '
         'AND CAST(position_z AS DOUBLE)=501.64300537109375 '
         'AND CAST(orientation AS DOUBLE)=4.253230094909668 '
         'AND spawntimesecsmin=120 AND spawntimesecsmax=120 AND wander_distance=0 '
         'AND health_percent=100 AND mana_percent=100 AND movement_type=0 '
         'AND spawn_flags=0 AND visibility_mod=0 AND guid<>12585232'),
        ('source GUID collision preserved', 'SELECT COUNT(*)=1 FROM creature WHERE guid=12585232 '
         'AND id=919502 AND map=0 AND position_x=1 AND position_y=2 AND position_z=3'),
        ('visitors remain temporary', 'SELECT COUNT(*)=0 FROM creature WHERE id IN (919546,919547)'),
        ('fresh rerun count', 'SELECT COUNT(*)=2 FROM creature'),
    ]
    fresh_assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in fresh_checks)
    output = query('\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2+
                   fresh_setup+(migration+'\n'+fresh_assertions+'\n')*2)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live checksums changed during validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != (len(checks)+len(fresh_checks))*2 or failures:
        raise SystemExit('Temporary-table validation failed: '+repr(failures or rows))
    print(f'Migration 0029 passed {len(rows)} assertions over two temporary runs; '
          f'{len(tables)} schemas shadowed. Live checksums unchanged.')


if __name__ == '__main__':
    main()
