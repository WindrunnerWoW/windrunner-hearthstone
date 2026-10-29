#!/usr/bin/env python3
"""Validate existing migration 0016 twice using connection-local temporary tables."""
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
    info = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                     args.config.read_text(encoding='utf-8-sig'), re.M)
    if not info:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = info.group(1).split(';')
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
    migration = (root / 'data/sql/world/0016_hearthstone_horde_warrior_quests.sql').read_text()
    tables = {
        'creature_template': 'entry IN (919507,919501)', 'item_template': 'entry=919620',
        'gameobject_template': 'entry=180436', 'quest_template': 'entry=919704',
        'gossip_menu': 'entry=65000', 'npc_text': 'ID=1', 'broadcast_text': 'entry=1',
        'creature_questrelation': 'id=919501', 'creature_involvedrelation': 'id=919501',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919522',
        'creature': 'id=919507', 'gameobject': 'id=919050',
    }
    writes = set(re.findall(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)', migration, re.I))
    if not writes <= tables.keys() or writes & {'creature','gameobject'}:
        raise SystemExit('Migration writes an unshadowed table or placement table')
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+t+'`' for t in tables)+';'
    before = query(checksum_sql)
    fields = {t: [r.split('\t')[0] for r in query('SHOW COLUMNS FROM `'+t+'`;').splitlines()]
              for t in tables}
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_warrior_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']
    checks = [
        ('teacher greeting update', 'SELECT COUNT(*)=1 FROM creature_template WHERE entry=919507 AND gossip_menu_id=65007'),
        ('glitter compatible independent chest', 'SELECT COUNT(*)=1 FROM gameobject_template WHERE entry=919050 '
         'AND type=3 AND displayId=10 AND ABS(size-0.4)<0.00001 AND data0=0 AND data1=0 '
         "AND data3=0 AND data8=919732 AND flags=0 AND script_name=''"),
        ('one native deck item', 'SELECT COUNT(*)=1 FROM item_template WHERE entry=919630 '
         'AND class=12 AND max_count=1 AND stackable=1 AND bonding=4 AND buy_price=0 AND sell_price=0'),
        ('native gates and experience', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919731 AND 919733 '
         'AND MinLevel=10 AND RequiredRaces=434 AND RequiredClasses=0 AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0'),
        ('intro and sequential links', 'SELECT COUNT(*)=3 FROM quest_template WHERE '
         '(entry=919731 AND PrevQuestId=919704 AND NextQuestInChain=919732 AND SpecialFlags=2) OR '
         '(entry=919732 AND PrevQuestId=919731 AND NextQuestInChain=919733 AND SpecialFlags=0) OR '
         '(entry=919733 AND PrevQuestId=919732 AND NextQuestInChain=0 AND SpecialFlags=2)'),
        ('item only retrieval', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919732 '
         'AND ReqItemId1=919630 AND ReqItemCount1=1 AND RewItemId1=0 AND RewItemCount1=0'),
        ('native duel reward', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919733 '
         'AND RewItemId1=919620 AND RewItemCount1=1'),
        ('native relations', 'SELECT (SELECT COUNT(*) FROM creature_questrelation WHERE id=919507)=3 AND '
         '(SELECT COUNT(*) FROM creature_involvedrelation WHERE id=919507)=3'),
        ('greeting bindings', 'SELECT COUNT(*)=1 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 '
         'JOIN gossip_menu g ON g.text_id=n.ID WHERE n.ID=9195130 AND g.entry=65007 '
         'AND n.Probability0=1 AND b.male_text=b.female_text'),
        ('Basic Warrior roster after turn in', 'SELECT '
         '(SELECT COUNT(*) FROM hearthstone_opponent WHERE creature_entry=919507)=0 AND '
         '(SELECT COUNT(*) FROM hearthstone_npc_unlock WHERE creature_entry=919507 AND quest_id=919733 '
         "AND deck_key='normal_warrior' AND faction='horde')=1"),
    ]
    for quest in (919731,919732,919733):
        zero = []
        for slot in range(1,5):
            zero += [f'{f}{slot}=0' for f in ('ReqCreatureOrGOId','ReqCreatureOrGOCount','ReqSourceId','ReqSourceCount','ReqSpellCast')]
            if quest != 919732 or slot != 1:
                zero += [f'ReqItemId{slot}=0',f'ReqItemCount{slot}=0']
        checks.append((f'every objective slot {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(zero)))
    for table in tables:
        comparisons = []
        for field in fields[table]:
            wanted = f'b.`{field}`'
            if table == 'creature_template' and field == 'gossip_menu_id':
                wanted = f'IF(b.entry=919507,65007,{wanted})'
            comparisons.append(f't.`{field}` <=> {wanted}')
        checks.append((f'preserve every seed field {table}',
            f'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_warrior_seed_{table}) FROM `{table}` t '
            f'JOIN `hs_warrior_seed_{table}` b ON '+' AND '.join(comparisons)))
    additions = {'item_template':1,'gameobject_template':1,'quest_template':3,
                 'gossip_menu':1,'npc_text':1,'broadcast_text':1,'creature_questrelation':3,
                 'creature_involvedrelation':3,'hearthstone_npc_unlock':1}
    for table in tables:
        checks.append((f'idempotent row count {table}',
            f'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_warrior_seed_{table})+{additions.get(table,0)} FROM `{table}`'))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query('\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2)
    if before != query(checksum_sql):
        raise SystemExit('Independent live-table checksums changed')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary-table validation failed: '+repr(failures or rows))
    print(f'Migration 0016 passed {len(rows)} assertions over two temporary-table runs; '
          f'{len(tables)} schemas shadowed. Independent live-table checksums unchanged.')


if __name__ == '__main__':
    main()
