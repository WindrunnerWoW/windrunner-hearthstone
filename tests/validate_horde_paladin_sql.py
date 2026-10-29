#!/usr/bin/env python3
"""Validate migration 0028 twice using connection-local TEMPORARY tables only."""
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
    migration = (root / 'data/sql/world/0028_hearthstone_horde_paladin_quests.sql').read_text()
    touched = set(re.findall(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', migration, re.I))
    if touched & {'gameobject', 'creature_movement', 'npc_vendor', 'item_template'}:
        raise SystemExit('Only the owner-requested Drin Lia placement may be captured; no pickups, paths, stock or item changes')
    tables = {
        'creature_template': 'entry IN (919501,4575)',
        'gossip_menu': 'entry=65019', 'npc_text': 'ID=9195250', 'broadcast_text': 'entry=9195250',
        'quest_template': 'entry IN (919704,919791,919792)',
        'creature_questrelation': 'id=919501', 'creature_involvedrelation': 'id=919501',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919539',
        'item_template': 'entry=919621',
        'npc_vendor': 'entry=4587',
        'creature': 'guid=(SELECT MIN(guid) FROM creature)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
    }
    if not touched <= tables.keys():
        raise SystemExit('Unshadowed migration table: '+repr(touched - tables.keys()))
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+t+'`' for t in tables)+';'
    before = query(checksum_sql)
    # Fetch all metadata in one connection, and fail before constructing SQL if
    # the client returns incomplete output (an empty ON clause is not a migration error).
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
        raise SystemExit('Incomplete schema metadata from database client: '+repr(missing))
    if len(before.splitlines()) != len(tables):
        raise SystemExit('Incomplete initial live-table checksum output')
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_paladin_seed_'+table
        setup.extend([
            f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
            f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
            f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
            f'INSERT INTO `{table}` SELECT * FROM `{seed}`;',
        ])
    # Preserve unrelated placement while testing reuse of a stale local spawn,
    # or allocation of a new local GUID on a database without Drin Lia.
    if not args.fresh_spawn:
        spawn_fields = [field for field in fields['creature'] if field != 'guid']
        setup += [
            'INSERT INTO creature ('+','.join('`'+field+'`' for field in spawn_fields)+') SELECT '+
            ','.join('919540' if field == 'id' else '`'+field+'`' for field in spawn_fields)+
            ' FROM hs_paladin_seed_creature;',
            'SET @hs_paladin_expected_guid=LAST_INSERT_ID();',
        ]
    baseline_fields = ('level_min','level_max','health_min','health_max','armor','speed_walk','speed_run',
                       'detection_range','call_for_help_range','dmg_min','dmg_max','attack_power',
                       'base_attack_time','ranged_attack_time','unit_class','unit_flags','type','regeneration')
    baseline = ' AND '.join(f't.`{f}` <=> b.`{f}`' for f in baseline_fields)
    checks = [
        ('baseline and placeholder source exist', 'SELECT COUNT(*)=2 FROM hs_paladin_seed_creature_template'),
        ('teacher identity city model services', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919540 "
         "AND name='Drin Lia' AND subname='Hearthstone researcher' AND display_id1=2663 "
         "AND faction=68 AND npc_flags=3 AND movement_type=0 AND gossip_menu_id=65020 AND script_name=''"),
        ('copied teacher baseline', 'SELECT COUNT(*)=1 FROM creature_template t JOIN creature_template b ON b.entry=919501 '
         'WHERE t.entry=919540 AND '+baseline),
        ('temporary display source unchanged', 'SELECT COUNT(*)=1 FROM creature_template WHERE entry=4575 AND display_id1=2663'),
        ('objective templates', "SELECT COUNT(*)=4 FROM creature_template WHERE entry BETWEEN 919541 AND 919544 "
         "AND display_id1=2663 AND faction=68 AND npc_flags=0 AND unit_flags=33554432 "
         "AND movement_type=0 AND script_name=''"),
        ('quest restrictions and rewards', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919801 AND 919803 '
         'AND Method=2 AND ZoneOrSort=-9100 AND MinLevel=10 AND QuestLevel=0 AND RequiredRaces=434 AND RequiredClasses=0 '
         'AND QuestFlags=0 AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0'),
        ('no regular opponent', 'SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry=919540'),
        ('final Horde roster binding', "SELECT COUNT(*)=1 FROM hearthstone_npc_unlock WHERE creature_entry=919540 "
         "AND quest_id=919803 AND deck_key='normal_paladin' AND faction='horde'"),
        ('existing Paladin reward retained', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919621 AND name='Paladin Deck'"),
        ('every dialogue binds both genders', 'SELECT COUNT(*)=14 FROM npc_text n JOIN broadcast_text b '
         'ON b.entry=n.BroadcastTextID0 WHERE n.ID BETWEEN 9195260 AND 9195273 AND n.ID=b.entry '
         'AND n.Probability0=1 AND b.male_text=b.female_text AND LENGTH(b.female_text)>0'),
        ('assessment page', "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195271 "
         "AND female_text='Drin Lia waits for your assessment of her technique.'"),
        ('post-RP ending', "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195272 "
         "AND female_text='Drin Lia looks entirely satisfied with her demonstration.'"),
    ]
    for menu, text in ((65020,9195260),(65021,9195266),(65022,9195272),(65023,9195263),(65024,9195269),(65025,9195271),(65026,9195273)):
        checks.append((f'greeting/recovery menu {menu}', f'SELECT COUNT(*)=1 FROM gossip_menu WHERE entry={menu} AND text_id={text}'))
    titles = ('Peer-Reviewed Radiance','A Reasonable Approximation','Silence. I Am a Paladin')
    labels = (('Examine the libram','Read the Alliance accounts'),
              ('Arrange the practice deck','Review the demonstration'),
              ('Defeat Drin Lia at Hearthstone',''))
    for index, quest in enumerate((919801,919802,919803)):
        previous = (919704,919801,919802)[index]
        following = (919802,919803,0)[index]
        extra = f"Title='{titles[index]}' AND PrevQuestId={previous} AND NextQuestInChain={following} "
        extra += f'AND SpecialFlags={2 if index == 2 else 0} '
        extra += f'AND RewItemId1={919621 if index == 2 else 0} AND RewItemCount1={1 if index == 2 else 0}'
        checks.append((f'chain title links reward {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+extra))
        objective_fields = []
        for slot in range(1,5):
            credit = 919541 + index*2 + slot-1 if index < 2 and slot < 3 else 0
            objective_fields += [f'ReqCreatureOrGOId{slot}={credit}', f'ReqCreatureOrGOCount{slot}={1 if credit else 0}']
            objective_fields += [f'{field}{slot}=0' for field in ('ReqItemId','ReqItemCount','ReqSourceId','ReqSourceCount','ReqSpellCast')]
            label = labels[index][slot-1] if slot < 3 else ''
            objective_fields.append(f"COALESCE(ObjectiveText{slot},'')='{label}'")
        objective_fields += ['SrcItemId=0','SrcItemCount=0','SrcSpell=0']
        checks.append((f'every objective slot {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(objective_fields)))
        for relation in ('creature_questrelation','creature_involvedrelation'):
            checks.append((f'{relation} {quest}', f'SELECT COUNT(*)=1 FROM {relation} WHERE quest={quest} AND id=919540'))
        rewards = [f'{field}{slot}=0' for slot in range(1,6) for field in ('RewRepFaction','RewRepValue')]
        rewards += [f'{field}{slot}=0' for slot in range(1,7) for field in ('RewChoiceItemId','RewChoiceItemCount')]
        rewards += [f'{field}{slot}=0' for slot in range(2,5) for field in ('RewItemId','RewItemCount')]
        checks.append((f'no extra rewards {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(rewards)))
    checks.append(('captured spawn all fields', 'SELECT COUNT(*)=1 FROM creature WHERE `id`=919540 AND `id2`=0 AND `id3`=0 AND `id4`=0 AND `map`=0 AND `position_x`=CAST(1507.699951171875 AS FLOAT) AND `position_y`=CAST(417.0929870605469 AS FLOAT) AND `position_z`=CAST(-62.185001373291016 AS FLOAT) AND `orientation`=CAST(3.6074700355529785 AS FLOAT) AND `spawntimesecsmin`=120 AND `spawntimesecsmax`=120 AND `wander_distance`=0 AND `health_percent`=100 AND `mana_percent`=100 AND `movement_type`=0 AND `spawn_flags`=0 AND `visibility_mod`=0'))
    if not args.fresh_spawn:
        checks.append(('reuse local placement GUID', 'SELECT COUNT(*)=1 FROM creature WHERE id=919540 AND guid=@hs_paladin_expected_guid'))
    # Preserve every column of every unrelated seed, including placement sentinels,
    # teacher baseline, native merchant stock and the existing class reward.
    new_counts = {'creature_template':5, 'gossip_menu':7, 'npc_text':14, 'broadcast_text':14,
                  'quest_template':3,'creature_questrelation':3,'creature_involvedrelation':3,'hearthstone_npc_unlock':1, 'creature':1}
    for table in tables:
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        checks.append((f'preserve every seed field {table}',
                       f'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_paladin_seed_{table}) FROM `{table}` t '
                       f'JOIN `hs_paladin_seed_{table}` b ON {comparison}'))
        checks.append((f'idempotent row count {table}',
                       f'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_paladin_seed_{table})+{new_counts.get(table,0)} FROM `{table}`'))
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query('\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed during validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary-table validation failed: '+repr(failures or rows))
    bindings = json.loads((root/'data/quests/class_unlocks.json').read_text())['npcs']
    teacher = next(row for row in bindings if row['entry'] == 919540)
    assert teacher == {'npc':'Drin Lia','entry':919540,'quest':919803,
                       'quest_title':titles[2],'deck_key':'normal_paladin','faction':'horde'}
    print(f'Migration 0028 passed {len(rows)} assertions over two temporary-table runs; '
          f'{len(tables)} schemas shadowed. Registry matches. Independent live-table checksums unchanged.')


if __name__ == '__main__':
    main()
