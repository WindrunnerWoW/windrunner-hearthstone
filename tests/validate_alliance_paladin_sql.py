#!/usr/bin/env python3
"""Validate Alliance Paladin SQL twice in temporary tables, leaving live data untouched."""
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
    setting = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                        args.config.read_text(encoding='utf-8-sig'), re.M)
    if not setting:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = setting.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--host='+host, '--port='+port, '--user='+user,
               '--database='+database, '--batch', '--skip-column-names']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True,
                                env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migration = (root/'data/sql/world/0036_hearthstone_alliance_paladin_quests.sql').read_text()
    tables = {
        'creature_template': 'entry IN (919502,5491,5492,928)',
        'gossip_menu': 'entry=65019',
        'npc_text': 'ID=9195250',
        'broadcast_text': 'entry=9195250',
        'quest_template': 'entry=919714',
        'creature_questrelation': 'id IN (919502,5491,5492,928)',
        'creature_involvedrelation': 'id IN (919502,5491,5492,928)',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919553',
        'item_template': 'entry IN (919621,919640,4404)',
        'npc_vendor': 'entry IN (1305,3335)',
        'npc_vendor_template': 'entry=1500048',
        'creature': 'id IN (919502,5491,5492,928) OR guid=(SELECT MIN(guid) FROM creature)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
    }
    allowed = set(tables) - {'npc_vendor', 'npc_vendor_template', 'creature', 'gameobject'}
    # Ignore quoted narrative and comments before checking statement targets. A
    # schema-qualified write would bypass a same-named temporary table, so forbid it.
    code = re.sub(r"'(?:''|\\.|[^'\\])*'|--[^\r\n]*|/\*[\s\S]*?\*/", ' ', migration)
    writes = re.findall(r'(?:INSERT\s+(?:IGNORE\s+)?INTO|UPDATE|DELETE\s+FROM)\s+'
                        r'(`?\w+`?(?:\s*\.\s*`?\w+`?)?)', code, re.I)
    if any('.' in target for target in writes):
        raise SystemExit('Schema-qualified writes cannot be safely shadowed')
    touched = {target.strip('`') for target in writes}
    if not touched <= allowed:
        raise SystemExit('Unexpected or unshadowed migration tables: '+repr(touched-allowed))
    if re.search(r'\b(?:CREATE|ALTER|DROP|TRUNCATE|REPLACE|RENAME|USE|CALL|LOAD)\b', code, re.I):
        raise SystemExit('Only ordinary data writes may appear in this migration')
    prerequisites = query('SELECT COUNT(*) FROM creature_template WHERE entry IN (919502,5491,5492,928);'
                          'SELECT COUNT(*) FROM item_template WHERE entry=919621;'
                          'SELECT COUNT(*) FROM quest_template WHERE entry=919714;')
    if prerequisites.strip().splitlines() != ['4', '1', '1']:
        raise SystemExit('Missing required Alliance world baselines, deck or introduction')
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tables)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tables) or any(row.endswith('\tNULL') for row in before.splitlines()):
        raise SystemExit('Incomplete initial checksums')
    names = ','.join("'"+table+"'" for table in tables)
    metadata = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS '
                     'WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('+names+') '
                     'ORDER BY TABLE_NAME,ORDINAL_POSITION;')
    fields = {table: [] for table in tables}
    for row in metadata.splitlines():
        table, column = row.split('\t')
        fields[table].append(column)
    if any(not columns for columns in fields.values()):
        raise SystemExit('Incomplete schema metadata')
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_alliance_paladin_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']

    # Exercise replacement of the former lesson, as well as clean installation.
    columns = ','.join(f'`{field}`' for field in fields['creature_template'])
    for entry in range(919572, 919575):
        values = ','.join(str(entry) if field == 'entry' else f'b.`{field}`'
                          for field in fields['creature_template'])
        setup.append(f'INSERT INTO creature_template ({columns}) SELECT {values} '
                     'FROM hs_alliance_paladin_seed_creature_template b WHERE b.entry=919502;')
    for text in range(9195371, 9195376):
        setup += [f"INSERT INTO broadcast_text (entry,male_text,female_text) VALUES ({text},'Retired lesson','Retired lesson');",
                  f'INSERT INTO npc_text (ID,BroadcastTextID0,Probability0) VALUES ({text},{text},1);']
    setup.append('INSERT INTO gossip_menu (entry,text_id) VALUES (65068,9195372),(65069,9195374);')

    checks = [
        ('no ordinary direct play', 'SELECT COUNT(*)=0 FROM hearthstone_opponent '
         'WHERE creature_entry BETWEEN 919570 AND 919577'),
        ('teacher roster unlock', "SELECT COUNT(*)=1 FROM hearthstone_npc_unlock "
         "WHERE creature_entry=919570 AND quest_id=919853 AND deck_key='normal_paladin' AND faction='alliance'"),
        ('no supporting actor roster', 'SELECT COUNT(*)=0 FROM hearthstone_npc_unlock '
         'WHERE creature_entry BETWEEN 919571 AND 919577'),
        ('actors awaiting manual placement', 'SELECT COUNT(*)=0 FROM creature WHERE id IN (919570,919571,919577)'),
        ('credit templates unspawned', 'SELECT COUNT(*)=0 FROM creature WHERE id BETWEEN 919572 AND 919576'),
        ('retired card-lesson credits removed', 'SELECT COUNT(*)=0 FROM creature_template WHERE entry BETWEEN 919572 AND 919574'),
        ('retired card-lesson menus removed', 'SELECT COUNT(*)=0 FROM gossip_menu WHERE entry BETWEEN 65068 AND 65069'),
        ('retired card-lesson texts removed', 'SELECT COUNT(*)=0 FROM npc_text WHERE ID BETWEEN 9195371 AND 9195375'),
        ('retired card-lesson broadcast texts removed', 'SELECT COUNT(*)=0 FROM broadcast_text WHERE entry BETWEEN 9195371 AND 9195375'),
        ('dialogue gender bindings', 'SELECT COUNT(*)=16 FROM npc_text n JOIN broadcast_text b '
         'ON b.entry=n.BroadcastTextID0 WHERE (n.ID=9195370 OR n.ID BETWEEN 9195376 AND 9195390) '
         'AND n.ID=b.entry AND n.Probability0=1 AND b.male_text=b.female_text AND LENGTH(b.male_text)>0'),
        ('native card-case item', 'SELECT COUNT(*)=1 FROM item_template WHERE entry=919650 '
         "AND name='Ada''s Card Case' AND class=12 AND display_id=(SELECT display_id FROM hs_alliance_paladin_seed_item_template WHERE entry=4404) AND quality=1 AND flags=0 "
         'AND allowable_class=-1 AND allowable_race=-1 AND item_level=1 AND required_level=0 '
         "AND max_count=1 AND stackable=1 AND bonding=4 AND buy_price=0 AND sell_price=0 AND start_quest=0 AND script_name=''"),
        ('card-case narrative', 'SELECT COUNT(*)=1 FROM item_template WHERE entry=919650 '
         "AND description LIKE '%Ada Sparkplugz%'"),
    ]
    copied = ('level_min', 'level_max', 'health_min', 'health_max', 'mana_min', 'mana_max',
              'armor', 'speed_walk', 'speed_run', 'detection_range', 'call_for_help_range',
              'dmg_min', 'dmg_max', 'attack_power', 'base_attack_time', 'ranged_attack_time',
              'unit_class', 'type', 'regeneration')
    baseline = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in copied)
    actors = ((919570, 'Sir Rowan Vale', 3284, 3, 65065),
              (919571, 'Squire Tess', 3289, 1, 65066),
              (919577, 'Ada Sparkplugz', 6975, 3, 65078))
    for entry, name, display, flags, menu in actors:
        checks += [
            (f'actor {entry}', f"SELECT COUNT(*)=1 FROM creature_template WHERE entry={entry} "
             f"AND name='{name}' AND display_id1={display} AND faction=12 AND npc_flags={flags} "
             f"AND gossip_menu_id={menu} AND movement_type=0 AND phase_quest_id=0 AND auras='' AND script_name=''"),
            (f'baseline {entry}', 'SELECT COUNT(*)=1 FROM creature_template t '
             'JOIN hs_alliance_paladin_seed_creature_template b ON b.entry=919502 '
             f'WHERE t.entry={entry} AND t.unit_flags <=> b.unit_flags AND '+baseline),
        ]
    for entry in (919575, 919576):
        checks += [
            (f'credit {entry}', f'SELECT COUNT(*)=1 FROM creature_template WHERE entry={entry} '
             "AND display_id1=3284 AND faction=12 AND npc_flags=0 AND unit_flags=33554432 "
             "AND gossip_menu_id=0 AND movement_type=0 AND phase_quest_id=0 AND auras='' AND script_name=''"),
            (f'credit baseline {entry}', 'SELECT COUNT(*)=1 FROM creature_template t '
             'JOIN hs_alliance_paladin_seed_creature_template b ON b.entry=919502 '
             f'WHERE t.entry={entry} AND '+baseline),
        ]
    menus = {65065:9195382, 65066:9195376, 65067:9195370, 65070:9195383, 65071:9195384, 65072:9195380,
             65073:9195385, 65074:9195386, 65075:9195387, 65076:9195388,
             65077:9195379, 65078:9195389, 65079:9195390}
    for menu, text in menus.items():
        checks.append((f'menu {menu}', f'SELECT COUNT(*)=1 FROM gossip_menu '
                       f'WHERE entry={menu} AND text_id={text}'))
    quests = ((919851, 'A Name on the Case', 919714, 919852, ()),
              (919852, 'An Empty Chair', 919851, 919853, (919575,919576)),
              (919853, 'Stand Together', 919852, 0, ()))
    for quest, title, previous, following, credits in quests:
        final = quest == 919853
        delivery = quest == 919851
        predicates = [f"Title='{title}'", 'Method=2', 'ZoneOrSort=-9100', 'MinLevel=10',
                      'QuestLevel=0', 'RequiredRaces=589', 'RequiredClasses=0', 'QuestFlags=0',
                      f'SpecialFlags={2 if final else 0}', f'PrevQuestId={previous}',
                      f'NextQuestInChain={following}', 'RewXP=500', 'RewOrReqMoney=0',
                      'RewMoneyMaxLevel=0', f'RewItemId1={919621 if final else 0}',
                      f'RewItemCount1={int(final)}', f'SrcItemId={919650 if delivery else 0}',
                      f'SrcItemCount={int(delivery)}',
                      'SrcSpell=0', 'RewMailTemplateId=0']
        for slot in range(1, 5):
            credit = credits[slot-1] if slot <= len(credits) else 0
            predicates += [f'ReqCreatureOrGOId{slot}={credit}',
                           f'ReqCreatureOrGOCount{slot}={int(bool(credit))}']
            item = 919650 if delivery and slot == 1 else 0
            predicates += [f'ReqItemId{slot}={item}', f'ReqItemCount{slot}={int(bool(item))}']
            predicates += [f'{field}{slot}=0' for field in
                           ('ReqSourceId', 'ReqSourceCount', 'ReqSpellCast')]
            if slot > 1:
                predicates += [f'RewItemId{slot}=0', f'RewItemCount{slot}=0']
            if credit or (final and slot == 1):
                predicates.append(f'LENGTH(ObjectiveText{slot})>0')
            else:
                predicates.append(f"COALESCE(ObjectiveText{slot},'')=''")
        predicates += [f'{field}{slot}=0' for slot in range(1, 7)
                       for field in ('RewChoiceItemId', 'RewChoiceItemCount')]
        predicates += [f'{field}{slot}=0' for slot in range(1, 6)
                       for field in ('RewRepFaction', 'RewRepValue')]
        checks.append((f'quest {quest}', f'SELECT COUNT(*)=1 FROM quest_template '
                       f'WHERE entry={quest} AND '+' AND '.join(predicates)))
        for relation in ('creature_questrelation', 'creature_involvedrelation'):
            actor = 919577 if delivery and relation == 'creature_involvedrelation' else 919570
            checks.append((f'{relation} {quest}', f'SELECT COUNT(*)=1 FROM {relation} '
                           f'WHERE quest={quest} AND id={actor}'))
        checks.append((f'text {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} '
                       'AND LENGTH(Details)>0 AND LENGTH(Objectives)>0 '
                       'AND LENGTH(RequestItemsText)>0 AND LENGTH(OfferRewardText)>0'))
    additions = {'creature_template':5, 'gossip_menu':13, 'npc_text':16, 'broadcast_text':16,
                 'quest_template':3, 'creature_questrelation':3, 'creature_involvedrelation':3,
                 'hearthstone_npc_unlock':1, 'item_template':1}
    for table in tables:
        seed = 'hs_alliance_paladin_seed_'+table
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        checks += [
            (f'preserve {table}', f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) '
             f'FROM `{table}` t JOIN `{seed}` b ON {comparison}'),
            (f'count {table}', f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`)'
             f'+{additions.get(table,0)} FROM `{table}`'),
        ]
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    output = query('\n'.join(setup)+'\n'+migration+'\n'+assertions+'\n'+migration+'\n'+assertions)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary SQL assertions failed: '+repr(failures or rows))
    registry = json.loads((root/'data/quests/class_unlocks.json').read_text())['npcs']
    matching = [npc for npc in registry if npc['entry'] == 919570]
    if len(matching) != 1 or (matching[0]['quest'], matching[0]['deck_key'], matching[0]['faction']) != (
            919853, 'normal_paladin', 'alliance'):
        raise SystemExit('Alliance Paladin teacher registry mismatch')
    if any(919571 <= npc['entry'] <= 919577 for npc in registry):
        raise SystemExit('Supporting Paladin actors must not enter the opponent roster')
    print(f'Alliance Paladin migration passed {len(rows)} assertions over two temporary runs / '
          f'{len(tables)} schemas. Registry matches. Live checksums unchanged.')


if __name__ == '__main__':
    main()
