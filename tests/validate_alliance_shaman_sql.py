#!/usr/bin/env python3
"""Validate migrations 0029/0030 twice in TEMPORARY tables, preserving live data."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--port', type=int)
    args = parser.parse_args()
    config = args.config.read_text(encoding='utf-8-sig')
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"', config, re.M)
    if not match:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = match.group(1).split(';')
    env = os.environ.copy()
    env['MYSQL_PWD'] = password
    command = [str(args.client), '--host='+host, '--port='+str(args.port or port),
               '--user='+user, '--database='+database, '--batch', '--skip-column-names']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True, env=env)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migrations = '\n'.join((root/'data/sql/world'/name).read_text() for name in (
        '0029_hearthstone_alliance_shaman_npcs.sql', '0030_hearthstone_alliance_shaman_quests.sql'))
    touched = set(re.findall(r'(?:INSERT(?:\s+IGNORE)?\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', migrations, re.I))
    tables = {
        'creature_template': 'entry IN (919502,5111,5163)',
        'gossip_menu': 'entry=65019',
        'npc_text': 'ID=9195250',
        'broadcast_text': 'entry=9195250',
        'quest_template': 'entry IN (919714,919791,919792)',
        'creature_questrelation': 'id=919502',
        'creature_involvedrelation': 'id=919502',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919539',
        'item_template': 'entry IN (11142,2595,919625)',
        'gameobject_template': 'entry=919057',
        'npc_vendor': '(entry IN (5111,5163) AND item<>919645)',
        'conditions': 'condition_entry=(SELECT MIN(condition_entry) FROM conditions)',
        'creature': 'id IN (919545,919546,919547) OR guid=(SELECT MIN(guid) FROM creature)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
        'creature_movement': 'id=(SELECT MIN(id) FROM creature_movement)',
        'creature_movement_template': 'entry IN (919545,919546,919547) OR entry=(SELECT MIN(entry) FROM creature_movement_template)',
        'creature_movement_special': 'id=(SELECT MIN(id) FROM creature_movement_special)',
        'creature_addon': 'guid=(SELECT MIN(guid) FROM creature_addon)',
    }
    forbidden = {'gameobject','creature_movement','creature_movement_template',
                 'creature_movement_special','creature_addon'}
    if touched & forbidden or not touched <= tables.keys():
        raise SystemExit('Unexpected or unshadowed migration tables: '+repr(touched))
    if re.search(r'(?:DELETE\s+FROM|UPDATE)\s+`?creature`?\s', migrations, re.I):
        raise SystemExit('Existing placements must be preserved')
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tables)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tables):
        raise SystemExit('Incomplete initial checksum output')
    names = ','.join("'"+table+"'" for table in tables)
    metadata = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS '
                     'WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('+names+') '
                     'ORDER BY TABLE_NAME,ORDINAL_POSITION;')
    fields = {table: [] for table in tables}
    for row in metadata.splitlines():
        table, column = row.split('\t')
        fields[table].append(column)
    if any(not columns for columns in fields.values()):
        raise SystemExit('Incomplete table metadata')
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_shaman_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']
    copied = ('level_min','level_max','health_min','health_max','armor','speed_walk','speed_run',
              'detection_range','call_for_help_range','dmg_min','dmg_max','attack_power',
              'base_attack_time','ranged_attack_time','unit_class','type','regeneration')
    baseline = ' AND '.join(f't.`{f}` <=> b.`{f}`' for f in copied)
    checks = [
        ('permanent John placement exists', 'SELECT COUNT(*)>=1 FROM creature WHERE id=919545'),
        ('source templates and native item sources exist',
         'SELECT (SELECT COUNT(*) FROM creature_template WHERE entry IN (919502,5111,5163))=3 '
         'AND (SELECT COUNT(*) FROM item_template WHERE entry IN (11142,2595,919625))=3'),
        ('copied actor baselines', 'SELECT COUNT(*)=3 FROM creature_template t JOIN creature_template b '
         'ON b.entry=919502 WHERE t.entry IN (919545,919546,919547) AND '+baseline),
        ('bookkeeping templates only', 'SELECT COUNT(*)=4 FROM creature_template WHERE entry BETWEEN 919548 AND 919551 '
         "AND display_id1=10184 AND npc_flags=0 AND unit_flags=33554432 AND faction=55 AND movement_type=0 AND script_name=''"),
        ('retired supply box absent', 'SELECT COUNT(*)=0 FROM gameobject_template WHERE entry=919058'),
        ('Ikae kit item', 'SELECT COUNT(*)=1 FROM item_template t JOIN item_template b ON b.entry=11142 '
         "WHERE t.entry=919645 AND t.name='Ikae \"Earth Totem Kit\"' AND t.display_id=b.display_id AND t.class=12 "
         'AND t.quality=1 AND t.max_count=1 AND t.stackable=1 AND t.bonding=4 AND t.buy_price=500 AND t.sell_price=0 '
         "AND t.start_quest=0 AND t.script_name=''"),
        ('temporary Ikae chair template', "SELECT COUNT(*)=1 FROM gameobject_template WHERE entry=919059 AND type=7 "
         "AND displayId=39 AND name='Ikae Earth Totem' AND ABS(size-1)<0.00001 "
         "AND data0=1 AND data1=1 AND data2=0 AND data3=0 AND script_name=''"),
        ('existing merchant stocks bourbon', 'SELECT COUNT(*)=1 FROM npc_vendor WHERE entry=5111 AND item=2595'),
        ('Burbik services preserved', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=5163 "
         "AND name='Burbik Gearspanner' AND (npc_flags & 4)=4"),
        ('quest-gated Ikae sale', 'SELECT COUNT(*)=1 FROM npc_vendor v JOIN conditions c '
         'ON c.condition_entry=v.condition_id WHERE v.entry=5163 AND v.item=919645 '
         'AND v.maxcount=0 AND v.incrtime=0 AND c.type=9 AND c.value1=919811 '
         'AND c.value2=1 AND c.value3=0 AND c.value4=0 AND c.flags=0'),
        ('existing reward', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919625 AND name='Shaman Deck'"),
        ('native quest common settings', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919811 AND 919813 '
         'AND Method=2 AND ZoneOrSort=-9100 AND MinLevel=10 AND QuestLevel=0 AND RequiredRaces=589 AND RequiredClasses=0 '
         'AND QuestFlags=0 AND RewXP=500 AND RewMoneyMaxLevel=0'),
        ('quest only opponent', 'SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry=919545'),
        ('final account Alliance roster', "SELECT COUNT(*)=1 FROM hearthstone_npc_unlock WHERE creature_entry=919545 "
         "AND quest_id=919813 AND deck_key='normal_shaman' AND faction='alliance'"),
        ('both dialogue genders', 'SELECT COUNT(*)=19 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 '
         'WHERE n.ID BETWEEN 9195280 AND 9195298 AND n.ID=b.entry AND n.Probability0=1 '
         'AND b.male_text=b.female_text AND LENGTH(b.female_text)>0'),
    ]
    for entry, display, name, flags, safety, menu in (
        (919545,10184,'John Maloneybrew',3,0,65027),
        (919546,8929,'Kibler McGriff',0,2,0),
        (919547,3898,'Ambassador Krip Parrianhoof',0,2,0)):
        checks.append((f'cast identity services and safety {entry}',
            f'SELECT COUNT(*)=1 FROM creature_template t JOIN creature_template b ON b.entry=919502 '
            f"WHERE t.entry={entry} AND t.display_id1={display} AND t.name='{name}' AND t.npc_flags={flags} "
            f"AND t.unit_flags=(b.unit_flags | {safety}) AND t.faction=55 AND t.movement_type=0 "
            f"AND t.gossip_menu_id={menu} AND t.script_name=''"))
    for menu, text in ([(65027,9195280),(65028,9195282),(65029,9195281)]+
                       [(65030+i,9195280 if i==0 else 9195283+4*i) for i in range(4)]+
                       [(65034+i,9195285+4*i) for i in range(4)]):
        checks.append((f'menu {menu}', f'SELECT COUNT(*)=1 FROM gossip_menu WHERE entry={menu} AND text_id={text}'))
    titles = ['Spirited Research','A Question of Totems','Applied Shamanism']
    labels = [['','','',''], ["Watch John''s totem demonstration",'','',''],
              ['Defeat John at Hearthstone','','','']]
    for index, quest in enumerate(range(919811,919814)):
        checks.append((f'quest chain and reward {quest}', f"SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} "
            f"AND Title='{titles[index]}' AND PrevQuestId={[919714,919811,919812][index]} "
            f'AND NextQuestInChain={[919812,919813,0][index]} AND SpecialFlags={2 if index==2 else 0} '
            f'AND RewItemId1={919625 if index==2 else 0} AND RewItemCount1={1 if index==2 else 0} '
            f'AND RewOrReqMoney={2500 if index==0 else 0}'))
        objectives = []
        for slot in range(1,5):
            credit = 919548 if index==1 and slot==1 else 0
            item = [919645,2595,0,0][slot-1] if index==0 else 0
            objectives += [f'ReqCreatureOrGOId{slot}={credit}', f'ReqCreatureOrGOCount{slot}={1 if credit else 0}',
                           f'ReqItemId{slot}={item}', f'ReqItemCount{slot}={1 if item else 0}',
                           f"COALESCE(ObjectiveText{slot},'')='{labels[index][slot-1]}'"]
            objectives += [f'{field}{slot}=0' for field in ('ReqSourceId','ReqSourceCount','ReqSpellCast')]
        objectives += ['SrcItemId=0','SrcItemCount=0','SrcSpell=0']
        checks.append((f'every objective slot {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(objectives)))
        for table in ('creature_questrelation','creature_involvedrelation'):
            checks.append((f'{table} {quest}', f'SELECT COUNT(*)=1 FROM {table} WHERE id=919545 AND quest={quest}'))
        rewards = [f'{field}{slot}=0' for slot in range(1,6) for field in ('RewRepFaction','RewRepValue')]
        rewards += [f'{field}{slot}=0' for slot in range(1,7) for field in ('RewChoiceItemId','RewChoiceItemCount')]
        rewards += [f'{field}{slot}=0' for slot in range(2,5) for field in ('RewItemId','RewItemCount')]
        checks.append((f'no extra rewards {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(rewards)))
    counts = {'creature_template':7,'gossip_menu':11,'npc_text':19,'broadcast_text':19,'quest_template':3,
              'creature_questrelation':3,'creature_involvedrelation':3,'item_template':1,
              'gameobject_template':1,'npc_vendor':1,'conditions':1,'hearthstone_npc_unlock':1}
    for table in tables:
        seed = 'hs_shaman_seed_'+table
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        added = (f'IF((SELECT COUNT(*) FROM `{seed}` WHERE id=919545)=0,1,0)'
                 if table == 'creature' else str(counts.get(table,0)))
        checks += [(f'preserve all seed fields {table}', f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) '
                    f'FROM `{table}` t JOIN `{seed}` b ON {comparison}'),
                   (f'idempotent row count {table}', f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`)+{added} FROM `{table}`')]
    checks.append(('unused chair data zero', 'SELECT COUNT(*)=1 FROM gameobject_template WHERE entry=919059 AND '+
                   ' AND '.join(f'data{i}=0' for i in range(24) if i not in (0,1))))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query('\n'.join(setup)+'\n'+(migrations+'\n'+assertions+'\n')*2)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows)!=len(checks)*2 or failures:
        raise SystemExit('Temporary validation failed: '+repr(failures or rows))
    registry = json.loads((root/'data/quests/class_unlocks.json').read_text())['npcs']
    assert next(row for row in registry if row['entry']==919545) == {
        'npc':'John Maloneybrew','entry':919545,'quest':919813,'quest_title':titles[2],
        'deck_key':'normal_shaman','faction':'alliance'}
    print(f'Migrations 0029/0030 passed {len(rows)} assertions over two temporary runs; '
          f'{len(tables)} schemas shadowed. Registry matches; live checksums unchanged.')


if __name__ == '__main__':
    main()
