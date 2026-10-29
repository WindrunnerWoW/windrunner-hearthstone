#!/usr/bin/env python3
"""Validate migration 0021 twice using connection-local temporary schema copies only."""
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
                 'data/sql/world/0021_hearthstone_alliance_druid_quests.sql').read_text()
    tables = {
        'creature_template': 'entry=919502', 'item_template': 'entry=919628',
        'gossip_menu': None, 'npc_text': None, 'broadcast_text': None, 'quest_template': None,
        'creature_questrelation': None, 'creature_involvedrelation': None,
        'hearthstone_opponent': None, 'hearthstone_npc_unlock': None,
        'creature': None, 'gameobject': None,
    }
    setup=[]
    for table, filter in tables.items():
        seed = 'hs_alliance_druid_seed_' + table
        setup.append(f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;')
        if filter:
            setup.append(f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {filter};')
        setup.append(f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;')
        if filter:
            setup.append(f'INSERT INTO `{table}` SELECT * FROM `{seed}`;')
        else:
            setup.append(f'DROP TEMPORARY TABLE `{seed}`;')
    checks = [
        ('teacher baseline', 'SELECT COUNT(*)=1 FROM creature_template t '
         'JOIN hs_alliance_druid_seed_creature_template b ON b.entry=919502 '
         "WHERE t.entry=919517 AND t.name='Thijs Nightbloom' AND t.display_id1=2266 "
         'AND t.faction=79 AND t.npc_flags=3 AND t.level_min=b.level_min AND t.level_max=b.level_max '
         'AND t.health_min=b.health_min AND t.health_max=b.health_max AND t.armor=b.armor '
         'AND t.speed_walk=b.speed_walk AND t.speed_run=b.speed_run AND t.dmg_min=b.dmg_min '
         'AND t.dmg_max=b.dmg_max AND t.attack_power=b.attack_power AND t.unit_flags=b.unit_flags '
         "AND t.movement_type=0 AND t.gossip_menu_id=65011 AND t.script_name=''"),
        ('saved objective templates', 'SELECT COUNT(*)=4 FROM creature_template WHERE entry BETWEEN 919518 AND 919521 '
         "AND npc_flags=0 AND unit_flags=33554434 AND script_name=''"),
        ('Alliance level and rewards', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919761 AND 919763 '
         'AND MinLevel=10 AND RequiredRaces=589 AND RequiredClasses=0 AND ZoneOrSort=-9100 '
         'AND QuestFlags=0 AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0'),
        ('patience native two objectives', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919761 '
         'AND PrevQuestId=919714 AND NextQuestInChain=919762 AND SpecialFlags=0 '
         'AND ReqCreatureOrGOId1=919518 AND ReqCreatureOrGOCount1=1 '
         'AND ReqCreatureOrGOId2=919519 AND ReqCreatureOrGOCount2=1 AND RewItemId1=0 '
         "AND Title='A Lesson in Patience' AND ObjectiveText2='Tell Thijs what you learned'"),
        ('growth native two objectives', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919762 '
         'AND PrevQuestId=919761 AND NextQuestInChain=919763 AND SpecialFlags=0 '
         'AND ReqCreatureOrGOId1=919520 AND ReqCreatureOrGOCount1=1 '
         'AND ReqCreatureOrGOId2=919521 AND ReqCreatureOrGOCount2=1 AND RewItemId1=0 '
         "AND Title='Natural Growth' AND ObjectiveText2='Tell Thijs about the view'"),
        ('duel event and existing reward', 'SELECT COUNT(*)=1 FROM quest_template q JOIN item_template i ON i.entry=q.RewItemId1 '
         'WHERE q.entry=919763 AND q.PrevQuestId=919762 AND q.NextQuestInChain=0 AND q.SpecialFlags=2 '
         'AND q.ReqCreatureOrGOId1=0 AND q.ReqCreatureOrGOId2=0 AND q.RewItemId1=919628 AND q.RewItemCount1=1 '
         "AND q.Title='The Long Game' AND i.name='Druid Deck'"),
        ('no collection objectives', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919761 AND 919763 '
         'AND ReqItemId1=0 AND ReqItemId2=0 AND ReqItemId3=0 AND ReqItemId4=0'),
        ('native quest relations', 'SELECT (SELECT COUNT(*) FROM creature_questrelation WHERE id=919517)=3 '
         'AND (SELECT COUNT(*) FROM creature_involvedrelation WHERE id=919517)=3'),
        ('five gossip text bindings', 'SELECT COUNT(*)=5 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 '
         'WHERE n.ID IN (9195170,9195172,9195173,9195174,9195175) AND n.Probability0=1 AND b.male_text=b.female_text'),
        ('greeting binding', 'SELECT COUNT(*)=1 FROM gossip_menu WHERE entry=65011 AND text_id=9195170'),
        ('patience question and accepted answers', "SELECT (SELECT COUNT(*) FROM broadcast_text WHERE entry=9195172 "
         "AND male_text='What did you learn?')=1 AND (SELECT COUNT(*) FROM broadcast_text WHERE entry=9195173 "
         "AND male_text LIKE 'A fair answer.%')=1"),
        ('growth question and reply', "SELECT (SELECT COUNT(*) FROM broadcast_text WHERE entry=9195174 "
         "AND male_text='What did the wind and the view teach you?')=1 "
         "AND (SELECT COUNT(*) FROM broadcast_text WHERE entry=9195175 AND male_text LIKE 'Exactly.%')=1"),
        ('Basic Druid quest-only roster', 'SELECT (SELECT COUNT(*) FROM hearthstone_opponent WHERE creature_entry=919517)=0 '
         'AND (SELECT COUNT(*) FROM hearthstone_npc_unlock WHERE creature_entry=919517 AND quest_id=919763 '
         "AND deck_key='normal_druid' AND faction='alliance')=1"),
        ('no spawns', 'SELECT (SELECT COUNT(*) FROM creature)=0 AND (SELECT COUNT(*) FROM gameobject)=0'),
        ('existing deck item unchanged', 'SELECT COUNT(*)=1 FROM item_template i JOIN hs_alliance_druid_seed_item_template b '
         'ON b.entry=i.entry WHERE i.entry=919628 AND i.display_id=b.display_id AND i.class=b.class AND i.flags=b.flags'),
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
    print(f'Migration 0021 passed {len(rows)} assertions over two runs against {len(tables)} temporary schema copies. Live tables unchanged.')

if __name__ == '__main__':
    main()
