#!/usr/bin/env python3
"""Run Alliance Rogue migration 0025 twice against connection-local temporary tables only."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--port')
    args = parser.parse_args()
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                      args.config.read_text(encoding='utf-8-sig'), re.M)
    if not match:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = match.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--host='+host, '--port='+(args.port or port),
               '--user='+user, '--database='+database, '--batch', '--skip-column-names']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    root = Path(__file__).resolve().parents[1]
    migration = (root / 'data/sql/world/0025_hearthstone_alliance_rogue_quests.sql').read_text()
    # Only schemas and selected seed rows are read from live tables. Every table
    # named by the migration is shadowed on this one connection before it runs.
    tables = {
        'creature_template': 'entry IN (919502,1323,1478)',
        'item_template': 'entry=919623',
        'gossip_menu': 'entry=688', 'gossip_menu_option': 'menu_id=688',
        'npc_text': 'ID=1', 'broadcast_text': 'entry=1',
        'quest_template': 'entry IN (6261,919714)',
        'creature_questrelation': 'id IN (1323,1478)',
        'creature_involvedrelation': 'id IN (1323,1478)',
        'gameobject_template': 'entry=1', 'hearthstone_opponent': 'creature_entry=919502',
        'hearthstone_npc_unlock': 'creature_entry=919522',
        'npc_vendor': 'entry IN (1323,1478,919502)',
        'creature': 'id IN (1323,1478,919502)', 'gameobject': 'guid=1',
    }
    # Independent connections before/after catch accidental writes to the live
    # tables; checksums never print content or database credentials.
    checksum_sql = 'CHECKSUM TABLE ' + ','.join('`'+t+'`' for t in tables) + ';'
    before = query(checksum_sql)
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_ar_seed_'+table
        setup.extend([
            f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
            f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
            f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
            f'INSERT INTO `{table}` SELECT * FROM `{seed}`;',
        ])
    fields = [row.split('\t')[0] for row in query('SHOW COLUMNS FROM creature_template;').splitlines()]
    preserved = ' AND '.join(
        't.npc_flags=(b.npc_flags | 1)' if f == 'npc_flags' else f't.`{f}` <=> b.`{f}`'
        for f in fields)
    baseline_fields = ('level_min','level_max','health_min','health_max','armor','speed_walk','speed_run',
                       'detection_range','call_for_help_range','dmg_min','dmg_max','attack_power',
                       'base_attack_time','ranged_attack_time','unit_class','unit_flags','type','regeneration')
    baseline = ' AND '.join(f't.`{f}`=b.`{f}`' for f in baseline_fields)
    checks = [
        ('seed actors exist', 'SELECT COUNT(*)=3 FROM hs_ar_seed_creature_template'),
        ('Matt identity and no questgiver marker', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919532 "
         "AND name='Operative Matt Dixon' AND display_id1=4338 AND npc_flags=1 AND subname='SI:7' AND gossip_menu_id=65017"),
        ('Alex corrected display and flags', "SELECT COUNT(*)=1 FROM creature_template WHERE entry=919533 "
         "AND name='Alex Horley' AND display_id1=5551 AND npc_flags=3 AND subname='Card trader' AND gossip_menu_id=65018"),
        ('copied Alliance teacher baseline', 'SELECT COUNT(*)=2 FROM creature_template t '
         'JOIN hs_ar_seed_creature_template b ON b.entry=919502 WHERE t.entry IN (919532,919533) AND '+baseline),
        ('friendly stationary no scripts', "SELECT COUNT(*)=2 FROM creature_template WHERE entry IN (919532,919533) "
         "AND faction=12 AND movement_type=0 AND script_name=''"),
        ('native actors preserved in every column', 'SELECT COUNT(*)=2 FROM creature_template t '
         'JOIN hs_ar_seed_creature_template b ON b.entry=t.entry WHERE t.entry IN (1323,1478) AND '+preserved),
        ('three unspawned credits', 'SELECT COUNT(*)=3 FROM creature_template WHERE entry BETWEEN 919534 AND 919538 '
         'AND npc_flags=0 AND unit_flags=33554434 AND faction=12'),
        ('invitation starts real quest', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919638 "
         "AND name='Someone Else''s Card' AND start_quest=919781 AND class=12 AND max_count=1 AND stackable=1 "
         "AND bonding=4 AND buy_price=0 AND sell_price=0 AND flags=0 AND script_name=''"),
        ('three distinct native cards', 'SELECT COUNT(*)=3 FROM item_template WHERE entry BETWEEN 919639 AND 919641 '
         "AND start_quest=0 AND class=12 AND max_count=1 AND stackable=1 AND bonding=4 AND buy_price=0 "
         "AND sell_price=0 AND flags=0 AND script_name=''"),
        ('native quest restrictions', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919781 AND 919783 '
         'AND MinLevel=10 AND RequiredRaces=589 AND RequiredClasses=0 AND QuestLevel=0 AND ZoneOrSort=-9100 '
         'AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0 AND QuestFlags=0'),
        ('saved investigation and intro', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919781 '
         'AND PrevQuestId=919714 AND NextQuestInChain=919782 AND SpecialFlags=0 '
         'AND ReqCreatureOrGOId1=919534 AND ReqCreatureOrGOId2=919535 AND ReqCreatureOrGOId3=919536 '
         'AND ReqCreatureOrGOCount1=1 AND ReqCreatureOrGOCount2=1 AND ReqCreatureOrGOCount3=1 AND RewItemId1=0'),
        ('three card objectives only', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919782 '
         'AND PrevQuestId=919781 AND NextQuestInChain=919783 AND ReqCreatureOrGOId1=0 '
         'AND ReqCreatureOrGOId2=0 AND ReqCreatureOrGOCount1=0 AND ReqCreatureOrGOCount2=0 '
         "AND ObjectiveText1='' AND ObjectiveText2='' "
         'AND ReqItemId1=919639 AND ReqItemId2=919640 AND ReqItemId3=919641 '
         'AND ReqItemCount1=1 AND ReqItemCount2=1 AND ReqItemCount3=1 AND RewItemId1=0'),
        ('native final duel existing reward', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919783 '
         'AND PrevQuestId=919782 AND NextQuestInChain=0 AND SpecialFlags=2 AND ReqCreatureOrGOId1=0 '
         'AND ReqItemId1=0 AND RewItemId1=919623 AND RewItemCount1=1'),
        ('Matt never starts or turns in quests', 'SELECT '
         '(SELECT COUNT(*) FROM creature_questrelation WHERE id=919532)=0 AND '
         '(SELECT COUNT(*) FROM creature_involvedrelation WHERE id=919532)=0'),
        ('investigation only item started', 'SELECT COUNT(*)=0 FROM creature_questrelation WHERE quest=919781'),
        ('Alex starts only second and duel', 'SELECT COUNT(*)=2 FROM creature_questrelation '
         'WHERE id=919533 AND quest IN (919782,919783)'),
        ('three Alex turn-ins', 'SELECT COUNT(*)=3 FROM creature_involvedrelation '
         'WHERE id=919533 AND quest IN (919781,919782,919783)'),
        ('existing native starts preserved', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_ar_seed_creature_questrelation) '
         'FROM creature_questrelation WHERE id IN (1323,1478)'),
        ('existing native turn-ins preserved', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_ar_seed_creature_involvedrelation) '
         'FROM creature_involvedrelation WHERE id IN (1323,1478)'),
        ('all dialogue bindings', 'SELECT COUNT(*)=23 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 '
         'WHERE n.ID BETWEEN 9195220 AND 9195243 AND b.male_text=b.female_text AND n.Probability0=1'),
        ('denial and discovery exact', "SELECT COUNT(*)=2 FROM broadcast_text WHERE "
         "(entry=9195222 AND male_text='Don''t know what you''re talking about. I am not interested in card games.') OR "
         "(entry=9195229 AND male_text='Took you long enough.')"),
        ('owner shop directions', "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195240 "
         "AND male_text LIKE '%Gerik Koen%Heavy-Handed Weapons%' AND male_text LIKE '%keeps his cards upstairs%'"),
        ('rigged source dice', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919642 "
         "AND name='Alex''s Rigged Dice' AND class=12 AND max_count=1 AND stackable=1 AND bonding=4 "
         "AND buy_price=0 AND sell_price=0 AND start_quest=0 AND script_name=''"),
        ('native source grant and any-order collection', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919782 "
         "AND SrcItemId=919642 AND SrcItemCount=1 AND Details LIKE '%without being detected%' "
         "AND Details LIKE '%any order%'"),
        ('ready concealed first-round victory', "SELECT COUNT(*)=5 FROM broadcast_text WHERE "
         "(entry=9195232 AND male_text LIKE '%You sure?%A ten.%your turn%') OR (entry=9195233 AND male_text LIKE '%Twelve%') "
         "OR (entry=9195235 AND male_text LIKE '%you win%') OR (entry=9195241 AND male_text LIKE '%without letting him notice%') "
         "OR (entry=9195242 AND male_text LIKE '%switch the dice%')"),
        ('obsolete random loss removed', 'SELECT (SELECT COUNT(*) FROM broadcast_text WHERE entry=9195234)=0 '
         'AND (SELECT COUNT(*) FROM npc_text WHERE ID=9195234)=0'),
        ('persuasion acknowledges successful delivery', "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195238 "
         "AND male_text LIKE '%Alex?%Take this and put it to good use%'"),
        ('small quest pickup', "SELECT COUNT(*)=1 FROM gameobject_template WHERE entry=919055 AND type=3 "
         "AND displayId=220 AND ABS(size-0.35)<0.00001 AND data0=0 AND data1=0 AND data3=0 AND data8=919782 AND script_name=''"),
        ('quest-only duel no ordinary opponent', 'SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry=919533'),
        ('account Alliance Rogue roster binding', "SELECT COUNT(*)=1 FROM hearthstone_npc_unlock WHERE creature_entry=919533 "
         "AND quest_id=919783 AND deck_key='normal_rogue' AND faction='alliance'"),
        ('trading explicitly before duel', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919782 "
         "AND OfferRewardText LIKE '%Come back%I sell special cards with limited stock%Sit down and play%'"),
        ('no actor or object placements', 'SELECT '
         '(SELECT COUNT(*) FROM creature)=(SELECT COUNT(*) FROM hs_ar_seed_creature) AND '
         '(SELECT COUNT(*) FROM gameobject)=(SELECT COUNT(*) FROM hs_ar_seed_gameobject) AND '
         '(SELECT COUNT(*) FROM creature WHERE id BETWEEN 919532 AND 919538)=0 AND '
         '(SELECT COUNT(*) FROM gameobject WHERE id=919055)=0'),
        ('native vendor stock unchanged', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_ar_seed_npc_vendor) FROM npc_vendor'),
        ('existing Osric menu unchanged', 'SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_ar_seed_gossip_menu_option) '
         'FROM gossip_menu_option WHERE menu_id=688'),
        ('existing reward item unchanged', 'SELECT COUNT(*)=1 FROM item_template t JOIN hs_ar_seed_item_template b '
         'ON b.entry=t.entry WHERE t.entry=919623 AND t.name=b.name AND t.start_quest=b.start_quest '
         'AND t.script_name=b.script_name AND t.flags=b.flags'),
        ('idempotent row counts', 'SELECT (SELECT COUNT(*) FROM creature_template)=(SELECT COUNT(*) FROM hs_ar_seed_creature_template)+5 AND '
         '(SELECT COUNT(*) FROM item_template)=6 AND '
         '(SELECT COUNT(*) FROM quest_template)=(SELECT COUNT(*) FROM hs_ar_seed_quest_template)+3 AND '
         '(SELECT COUNT(*) FROM hearthstone_npc_unlock)=(SELECT COUNT(*) FROM hs_ar_seed_hearthstone_npc_unlock)+1'),
    ]
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    sql = '\n'.join(setup)+'\n'+(migration+'\n'+assertions+'\n')*2
    output = query(sql)
    after = query(checksum_sql)
    if before != after:
        raise SystemExit('Live-table checksums changed during validation.')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks)*2 or failures:
        raise SystemExit('Temporary-table validation failed: '+repr(failures or rows))
    print(f'Migration 0025 passed {len(rows)} assertions over two temporary-table runs; '
          f'{len(tables)} schemas shadowed. Independent live-table checksums unchanged.')


if __name__ == '__main__':
    main()
