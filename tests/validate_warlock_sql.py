#!/usr/bin/env python3
"""Validate both Warlock migrations in connection-local temporary tables only."""
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
        result = subprocess.run(command, input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migrations = [(root/'data/sql/world'/name).read_text() for name in
                  ('0032_hearthstone_horde_warlock_quests.sql', '0033_hearthstone_alliance_warlock_quests.sql')]
    tables = {
        'creature_template': 'entry IN (919501,919502,416,461,1305,3325,3335,6122)',
        'gossip_menu': 'entry=65019', 'npc_text': 'ID=9195250', 'broadcast_text': 'entry=9195250',
        'quest_template': 'entry IN (919704,919714)',
        'creature_questrelation': 'id IN (461,6122,919501,919502)',
        'creature_involvedrelation': 'id IN (461,6122,919501,919502)',
        'hearthstone_opponent': 'creature_entry=919501',
        'hearthstone_npc_unlock': 'creature_entry=919553',
        'item_template': 'entry IN (919640,919627,2593,10940)',
        'npc_vendor': 'entry IN (1305,3335) AND item<>919647', 'npc_vendor_template': 'entry=1500048',
        'conditions': 'condition_entry=(SELECT MIN(condition_entry) FROM conditions WHERE type<>9 OR value1<>919832)',
        'creature': 'id IN (1305,461,6122,3325,3335)',
        'gameobject': 'guid=(SELECT MIN(guid) FROM gameobject)',
    }
    allowed = set(tables) - {'npc_vendor_template', 'creature', 'gameobject'}
    touched = set(re.findall(r'(?:INSERT\s+(?:IGNORE\s+)?INTO|UPDATE|DELETE\s+FROM)\s+`?(\w+)`?', '\n'.join(migrations), re.I))
    if not touched <= allowed:
        raise SystemExit('Unexpected migration table: '+repr(touched-allowed))
    # Fail before mutations if a baseline needed by INSERT SELECT is absent.
    prerequisites = query('SELECT COUNT(*) FROM creature_template WHERE entry IN (919501,919502,3335);'
                          'SELECT COUNT(*) FROM item_template WHERE entry IN (919640,919627,2593,10940);')
    if prerequisites.strip().splitlines() != ['3', '4']:
        raise SystemExit('Missing required world baselines/items')
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in tables)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tables):
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
        seed = 'hs_warlock_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                  f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `{seed}`;']

    checks = [
        ('practice item exact', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919646 AND class=12 "
         "AND name='Claw-Marked Practice Card' AND display_id=(SELECT display_id FROM hs_warlock_seed_item_template WHERE entry=919640) "
         "AND max_count=1 AND stackable=1 AND bonding=4 AND buy_price=0 AND sell_price=0 AND script_name='' AND start_quest=0"),
        ('existing port stock', 'SELECT COUNT(*)=1 FROM npc_vendor_template WHERE entry=1500048 AND item=2593'),
        ('ashes item exact', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919647 AND class=12 "
         "AND name='Burnt Bone Ashes' AND display_id=(SELECT display_id FROM hs_warlock_seed_item_template WHERE entry=10940) "
         "AND buy_count=1 AND buy_price=300 AND sell_price=0 AND max_count=1 AND stackable=1 AND bonding=4 AND script_name='' AND start_quest=0"),
        ('ashes quest vendor gate', 'SELECT COUNT(*)=1 FROM npc_vendor v JOIN conditions c ON c.condition_entry=v.condition_id '
         'WHERE v.entry=3335 AND v.item=919647 AND v.maxcount=0 AND v.incrtime=0 AND v.itemflags=0 '
         'AND c.type=9 AND c.value1=919832 AND c.value2=1 AND c.value3=0 AND c.value4=0 AND c.flags=0'),
        ('ashes condition reused', 'SELECT COUNT(*)=1 FROM conditions WHERE type=9 AND value1=919832 '
         'AND value2=1 AND value3=0 AND value4=0 AND flags=0'),
        ('Hagrus native service', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=3335 AND npc_flags=4 AND vendor_id=0 AND script_name=''"),
        ('card objective and narrative hint', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919832 "
         "AND Details LIKE '%always wants a favor%' AND Details LIKE '%Bring him Burnt Bone Ashes instead%' "
         "AND Objectives='Get the practice card marked by Skrit and return it to Varka.' "
         "AND CONCAT(Details,Objectives) NOT LIKE '%silver%'"),
        ('imp accepts ashes and directs to Hagrus', "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195352 "
         "AND male_text LIKE 'Better than nothing.%Hagrus%' AND male_text NOT LIKE '%silver%'"),
        ('no ordinary direct play', 'SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry BETWEEN 919559 AND 919563'),
        ('only teachers in roster', "SELECT COUNT(*)=2 FROM hearthstone_npc_unlock WHERE creature_entry BETWEEN 919559 AND 919563 "
         "AND deck_key='normal_warlock' AND ((creature_entry=919559 AND quest_id=919833 AND faction='horde') "
         "OR (creature_entry=919561 AND quest_id=919843 AND faction='alliance'))"),
        ('no imp witness roster', 'SELECT COUNT(*)=0 FROM hearthstone_npc_unlock WHERE creature_entry IN (919560,919562,919563)'),
        ('credits unspawned', 'SELECT COUNT(*)=0 FROM creature WHERE id BETWEEN 919564 AND 919569'),
        ('actors manually placed', 'SELECT COUNT(*)=0 FROM creature WHERE id BETWEEN 919559 AND 919563'),
        ('dialogue gender binding', 'SELECT COUNT(*)=37 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 '
         'WHERE n.ID BETWEEN 9195330 AND 9195366 AND n.ID=b.entry AND n.Probability0=1 '
         'AND b.male_text=b.female_text AND LENGTH(b.male_text)>0'),
    ]
    actors = ((919559,1325,85,3,65046), (919560,4449,85,1,65047),
              (919561,5092,12,3,65048), (919562,4449,12,3,65063), (919563,1490,12,1,65050))
    copied = ('level_min','level_max','health_min','health_max','mana_min','mana_max','armor',
              'speed_walk','speed_run','detection_range','call_for_help_range','dmg_min','dmg_max',
              'attack_power','base_attack_time','ranged_attack_time','unit_class','unit_flags','regeneration')
    baseline = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in copied)
    for entry, display, faction, flags, menu in actors:
        checks += [(f'actor {entry}', f'SELECT COUNT(*)=1 FROM creature_template WHERE entry={entry} '
                    f'AND display_id1={display} AND faction={faction} AND npc_flags={flags} AND gossip_menu_id={menu} '
                    "AND movement_type=0 AND phase_quest_id=0 AND auras='' AND script_name=''"),
                   (f'baseline {entry}', 'SELECT COUNT(*)=1 FROM creature_template t JOIN hs_warlock_seed_creature_template b '
                    f'ON b.entry={919501 if faction == 85 else 919502} WHERE t.entry={entry} AND '+baseline)]
    checks.append(('credit definitions', 'SELECT COUNT(*)=6 FROM creature_template WHERE entry BETWEEN 919564 AND 919569 '
                   "AND npc_flags=0 AND unit_flags=33554432 AND movement_type=0 AND gossip_menu_id=0 AND script_name=''"))
    menus = {65046:9195330,65047:9195331,65048:9195332,65049:9195333,65050:9195334,
             65051:9195335,65052:9195336,65053:9195337,65054:9195338,65055:9195339,
             65056:9195340,65057:9195341,65058:9195342,65059:9195343,
             65060:9195364,65061:9195365,65062:9195366,65063:9195360,65064:9195357}
    for menu, text in menus.items():
        checks.append((f'menu {menu}', f'SELECT COUNT(*)=1 FROM gossip_menu WHERE entry={menu} AND text_id={text}'))
    quests = (
        (919831,'Read the Scars',919704,919832,434,-9100,(919564,919565,919566),0,919559,919559),
        (919832,"The Imp's Cut",919831,919833,434,-9100,(),919646,919559,919559),
        (919833,'The Price of Power',919832,0,434,-9100,(),0,919559,919559),
        (919841,'A Suitable Witness',919714,919842,589,-9100,(919567,919568),0,919561,919561),
        (919842,'A Small Consideration',919841,919843,589,-9100,(919569,),2593,919561,919562),
        (919843,'Terms and Conditions Apply',919842,0,589,-9100,(),0,919561,919561),
    )
    for quest, title, previous, following, races, zone, credits, item, start, finish in quests:
        final = quest in (919833,919843)
        escaped = title.replace("'", "''")
        predicates = [f"Title='{escaped}'", 'Method=2', 'MinLevel=10', 'QuestLevel=0', f'RequiredRaces={races}',
                      f'ZoneOrSort={zone}', 'RequiredClasses=0', 'QuestFlags=0', f'SpecialFlags={2 if final else 0}',
                      f'PrevQuestId={previous}', f'NextQuestInChain={following}', 'RewXP=500', 'RewOrReqMoney=0',
                      'RewMoneyMaxLevel=0', f'RewItemId1={919627 if final else 0}', f'RewItemCount1={int(final)}',
                      'SrcItemId=0', 'SrcItemCount=0', 'SrcSpell=0', 'RewMailTemplateId=0']
        for slot in range(1,5):
            credit = credits[slot-1] if slot <= len(credits) else 0
            required = item if slot == 1 else 0
            predicates += [f'ReqCreatureOrGOId{slot}={credit}', f'ReqCreatureOrGOCount{slot}={int(bool(credit))}',
                           f'ReqItemId{slot}={required}', f'ReqItemCount{slot}={int(bool(required))}']
            predicates += [f'{field}{slot}=0' for field in ('ReqSourceId','ReqSourceCount','ReqSpellCast')]
            if slot > 1:
                predicates += [f'RewItemId{slot}=0', f'RewItemCount{slot}=0']
            if credit:
                predicates.append(f'LENGTH(ObjectiveText{slot})>0')
            elif not final or slot != 1:
                predicates.append(f"COALESCE(ObjectiveText{slot},'')=''")
        predicates += [f'{field}{slot}=0' for slot in range(1,7) for field in ('RewChoiceItemId','RewChoiceItemCount')]
        predicates += [f'{field}{slot}=0' for slot in range(1,6) for field in ('RewRepFaction','RewRepValue')]
        checks.append((f'quest {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} AND '+' AND '.join(predicates)))
        for relation, npc in (('creature_questrelation',start),('creature_involvedrelation',finish)):
            checks.append((f'{relation} {quest}', f'SELECT COUNT(*)=1 FROM {relation} WHERE quest={quest} AND id={npc}'))
        checks.append((f'text {quest}', f'SELECT COUNT(*)=1 FROM quest_template WHERE entry={quest} '
                       'AND LENGTH(Details)>0 AND LENGTH(Objectives)>0 AND LENGTH(RequestItemsText)>0 AND LENGTH(OfferRewardText)>0'))
    additions = {'creature_template':11,'gossip_menu':19,'npc_text':37,'broadcast_text':37,
                 'item_template':2,'quest_template':6,'creature_questrelation':6,'creature_involvedrelation':6,
                 'hearthstone_npc_unlock':2,'npc_vendor':1,'conditions':1}
    for table in tables:
        seed = 'hs_warlock_seed_'+table
        comparison = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in fields[table])
        checks += [(f'preserve {table}', f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) FROM `{table}` t JOIN `{seed}` b ON {comparison}'),
                   (f'count {table}', f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`)+{additions.get(table,0)} FROM `{table}`')]
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    # Reverse the second pass as well: each migration must preserve the other faction's rows.
    output = query('\n'.join(setup)+'\n'+'\n'.join(migrations)+'\n'+assertions+'\n'+
                   '\n'.join(reversed(migrations))+'\n'+assertions)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary SQL assertions failed: '+repr(failures or rows))
    registry = json.loads((root/'data/quests/class_unlocks.json').read_text())['npcs']
    for entry, quest, faction in ((919559,919833,'horde'),(919561,919843,'alliance')):
        npc = next(row for row in registry if row['entry'] == entry)
        assert (npc['quest'],npc['deck_key'],npc['faction']) == (quest,'normal_warlock',faction)
    assert not any(row['entry'] in (919560,919562,919563) for row in registry)
    print(f'Warlock migrations passed {len(rows)} assertions over two temporary runs / '
          f'{len(tables)} schemas, reversed second order. Registry matches. Live checksums unchanged.')


if __name__ == '__main__':
    main()
