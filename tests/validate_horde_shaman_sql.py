#!/usr/bin/env python3
"""Validate Annie's migration twice using connection-local TEMPORARY tables."""
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
    migration = (root/'data/sql/world/0031_hearthstone_horde_shaman_quests.sql').read_text()
    tables = {
        'creature_template': 'entry IN (919501,3030,3031,3032,3027,60863,60864,60865)',
        'gossip_menu': 'entry=65019',
        'npc_text': 'ID=9195250',
        'broadcast_text': 'entry=9195250',
        'quest_template': 'entry=919704',
        'creature_questrelation': 'id IN (919501,3030,3031,3032,60863,60864,60865)',
        'creature_involvedrelation': 'id IN (919501,3030,3031,3032,60863,60864,60865)',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919539',
        'item_template': 'entry IN (159,919625)',
        'npc_vendor': 'entry=3027',
        'creature': 'id IN (919552,919553) OR guid=(SELECT MIN(guid) FROM creature)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
        'creature_movement_template': 'entry=919552 OR entry=(SELECT MIN(entry) FROM creature_movement_template)',
        'creature_addon': 'guid=(SELECT MIN(guid) FROM creature_addon)',
    }
    touched = set(re.findall(r'(?:INSERT\s+INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', migration, re.I))
    allowed = {'creature_template', 'gossip_menu', 'npc_text', 'broadcast_text', 'quest_template',
               'creature_questrelation', 'creature_involvedrelation', 'hearthstone_opponent', 'hearthstone_npc_unlock'}
    if not touched <= allowed or not touched <= tables.keys():
        raise SystemExit('Unexpected/unshadowed migration tables: '+repr(touched))
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tables)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tables):
        raise SystemExit('Incomplete initial live checksums')
    names = ','.join("'"+table+"'" for table in tables)
    metadata = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS '
                     'WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('+names+') '
                     'ORDER BY TABLE_NAME,ORDINAL_POSITION;')
    fields = {table: [] for table in tables}
    for row in metadata.splitlines():
        table, column = row.split('\t')
        fields[table].append(column)
    if any(not columns for columns in fields.values()):
        raise SystemExit('Incomplete metadata; no migration executed')
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_annie_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']

    copied = ('level_min', 'level_max', 'health_min', 'health_max', 'mana_min', 'mana_max', 'armor',
              'speed_walk', 'speed_run', 'detection_range', 'call_for_help_range', 'dmg_min', 'dmg_max',
              'attack_power', 'base_attack_time', 'ranged_attack_time', 'unit_class', 'type', 'regeneration')
    baseline = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in copied)
    checks = [
        ('teacher baseline exists', 'SELECT COUNT(*)=1 FROM creature_template WHERE entry=919501'),
        ('both Annie identities and services', "SELECT COUNT(*)=2 FROM creature_template "
         "WHERE entry IN (919552,919553) AND display_id1=3780 AND faction=104 AND npc_flags=3 "
         "AND movement_type=0 AND (unit_flags & 33554432)=0"),
        ('both Annie baselines', 'SELECT COUNT(*)=2 FROM creature_template t JOIN creature_template b ON b.entry=919501 '
         'WHERE t.entry IN (919552,919553) AND t.unit_flags=b.unit_flags AND '+baseline),
        ('living identity and script markers', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919552 "
         "AND name='Annie Foxstalker' AND subname='Aspiring shaman' AND phase_quest_id=0 AND auras='' "
         "AND gossip_menu_id=65038 AND script_name=''"),
        ('spirit phase aura and identity', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919553 "
         "AND name='Spirit of Annie Foxstalker' AND subname='Hearthstone teacher' AND phase_quest_id=919824 "
         "AND auras='9617' AND gossip_menu_id=65039 AND script_name=''"),
        ('unspawned credits', 'SELECT COUNT(*)=5 FROM creature_template WHERE entry BETWEEN 919554 AND 919558 '
         "AND npc_flags=0 AND unit_flags=33554432 AND phase_quest_id=0 AND movement_type=0 AND script_name=''"),
        ('quest restrictions and ordinary rewards', 'SELECT COUNT(*)=5 FROM quest_template WHERE entry BETWEEN 919821 AND 919825 '
         'AND Method=2 AND ZoneOrSort=-9100 AND MinLevel=10 AND QuestLevel=0 AND RequiredRaces=434 AND RequiredClasses=0 '
         'AND QuestFlags=0 AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0'),
        ('no ordinary direct opponent', 'SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry IN (919552,919553)'),
        ('spirit only Horde roster', "SELECT COUNT(*)=1 FROM hearthstone_npc_unlock WHERE creature_entry IN (919552,919553) "
         "AND creature_entry=919553 AND quest_id=919825 AND deck_key='normal_shaman' AND faction='horde'"),
        ('native water source retained', 'SELECT COUNT(*)=1 FROM npc_vendor WHERE entry=3027 AND item=159'),
        ('existing reward retained', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919625 AND name='Shaman Deck'"),
        ('all texts both genders', 'SELECT COUNT(*)=25 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 '
         'WHERE n.ID BETWEEN 9195300 AND 9195327 AND n.ID=b.entry AND n.Probability0=1 '
         'AND b.male_text=b.female_text AND LENGTH(b.male_text)>0'),
        ('only new spirit in duel relation', 'SELECT COUNT(*)=1 FROM creature_questrelation WHERE quest=919825 AND id=919553'),
    ]
    for menu in range(65038, 65046):
        checks.append((f'gossip binding {menu}',
            f'SELECT COUNT(*)=1 FROM gossip_menu WHERE entry={menu} AND text_id={9195300+menu-65038}'))
    titles = ('Words She Cannot Say', 'A Voice That Trembles', 'Room for Silence',
              'Something I Can Give', 'A Seat for Annie')
    labels = (('Ask Siln about Annie', 'Ask Beram about Annie', 'Ask Tigor about Annie', ''),
              ('Practice the introduction', '', '', ''), ('Listen beside Annie', '', '', ''),
              ('', '', '', ''), ('Defeat Annie at Hearthstone', '', '', ''))
    for index, quest in enumerate(range(919821, 919826)):
        previous = 919704 if index == 0 else quest-1
        following = quest+1 if index < 4 else 0
        predicate = [f"Title='{titles[index]}'", f'PrevQuestId={previous}', f'NextQuestInChain={following}',
                     f'SpecialFlags={2 if index == 4 else 0}',
                     f'RewItemId1={919625 if index == 4 else 0}', f'RewItemCount1={1 if index == 4 else 0}']
        for slot in range(1, 5):
            credit = 919553+slot if index == 0 and slot <= 3 else \
                919556+index if index in (1, 2) and slot == 1 else 0
            item = 159 if index == 3 and slot == 1 else 0
            predicate += [f'ReqCreatureOrGOId{slot}={credit}', f'ReqCreatureOrGOCount{slot}={int(bool(credit))}',
                          f'ReqItemId{slot}={item}', f'ReqItemCount{slot}={int(bool(item))}',
                          f"COALESCE(ObjectiveText{slot},'')='{labels[index][slot-1]}'"]
            predicate += [f'{field}{slot}=0' for field in ('ReqSourceId', 'ReqSourceCount', 'ReqSpellCast')]
            if slot > 1:
                predicate += [f'RewItemId{slot}=0', f'RewItemCount{slot}=0']
        predicate += ['SrcItemId=0', 'SrcItemCount=0', 'SrcSpell=0', 'RewMailTemplateId=0',
                      'ReqSpell=0'] if 'ReqSpell' in fields['quest_template'] else \
                     ['SrcItemId=0', 'SrcItemCount=0', 'SrcSpell=0', 'RewMailTemplateId=0']
        predicate += [f'{field}{slot}=0' for slot in range(1, 7) for field in ('RewChoiceItemId', 'RewChoiceItemCount')]
        predicate += [f'{field}{slot}=0' for slot in range(1, 6) for field in ('RewRepFaction', 'RewRepValue')]
        checks.append((f'every objective reward gate {quest}',
            f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(predicate)))
        for relation in ('creature_questrelation', 'creature_involvedrelation'):
            checks.append((f'{relation} {quest}',
                f'SELECT COUNT(*)=1 FROM {relation} WHERE quest={quest} AND id={919553 if index == 4 else 919552}'))
        checks.append((f'story text present {quest}',
            f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND LENGTH(Details)>0 '
            'AND LENGTH(Objectives)>0 AND LENGTH(RequestItemsText)>0 AND LENGTH(OfferRewardText)>0'))
    checks.append(('spirit remembers the trader', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919825 "
                   "AND Details LIKE '%trader%' AND Details LIKE '%Shaman deck%'"))
    additions = {'creature_template': 7, 'gossip_menu': 8, 'npc_text': 25, 'broadcast_text': 25,
                 'quest_template': 5, 'creature_questrelation': 5, 'creature_involvedrelation': 5,
                 'hearthstone_npc_unlock': 1}
    for table in tables:
        seed = 'hs_annie_seed_'+table
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        checks += [(f'preserve every seed field {table}',
                    f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) FROM `{table}` t JOIN `{seed}` b ON {comparison}'),
                   (f'exact idempotent count {table}',
                    f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`)+{additions.get(table, 0)} FROM `{table}`')]
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query('\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary SQL assertions failed: '+repr(failures or rows))
    registry = json.loads((root/'data/quests/class_unlocks.json').read_text())['npcs']
    assert not any(row['entry'] == 919552 for row in registry)
    assert next(row for row in registry if row['entry'] == 919553) == {
        'npc': 'Spirit of Annie Foxstalker', 'entry': 919553, 'quest': 919825,
        'quest_title': 'A Seat for Annie', 'deck_key': 'normal_shaman', 'faction': 'horde'}
    print(f'Migration 0031 passed {len(rows)} assertions over two temporary-table runs; '
          f'{len(tables)} schemas shadowed. Registry matches. Live-table checksums unchanged.')


if __name__ == '__main__':
    main()
