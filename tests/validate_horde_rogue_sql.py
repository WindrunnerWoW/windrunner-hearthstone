#!/usr/bin/env python3
"""Run Rogue migrations 0022/0023 twice against session-local temporary tables only."""
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
    root = Path(__file__).resolve().parents[1]
    migrations = '\n'.join((root / 'data/sql/world' / name).read_text() for name in (
        '0022_hearthstone_horde_rogue_npcs.sql', '0023_hearthstone_horde_rogue_quests.sql'))
    tables = {
        'creature_template': 'entry=919501', 'item_template': 'entry=919623',
        'npc_vendor': 'entry=919501', 'conditions': None,
        'gossip_menu': None, 'gossip_menu_option': None, 'npc_text': None, 'broadcast_text': None,
        'quest_template': None, 'creature_questrelation': None, 'creature_involvedrelation': None,
        'hearthstone_opponent': None, 'hearthstone_npc_unlock': None,
        'creature': None, 'gameobject': None,
    }
    setup = []
    for table, seed_filter in tables.items():
        seed = 'hs_rogue_seed_' + table
        setup.append(f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;')
        if seed_filter:
            setup.append(f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {seed_filter};')
        setup.append(f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;')
        if seed_filter:
            setup.append(f'INSERT INTO `{table}` SELECT * FROM `{seed}`;')
        else:
            setup.append(f'DROP TEMPORARY TABLE `{seed}`;')
    baseline_fields = ('level_min', 'level_max', 'health_min', 'health_max', 'armor',
                       'speed_walk', 'speed_run', 'detection_range', 'call_for_help_range',
                       'dmg_min', 'dmg_max', 'attack_power', 'base_attack_time',
                       'ranged_attack_time', 'unit_class', 'unit_flags', 'type', 'regeneration')
    baseline = ' AND '.join(f't.{field}=b.{field}' for field in baseline_fields)
    checks = [
        ('five actor identities and displays', "SELECT COUNT(*)=5 FROM creature_template WHERE "
         "(entry=919522 AND name='For''sen' AND display_id1=4070 AND npc_flags=3 AND gossip_menu_id=65012) OR "
         "(entry=919523 AND name='Gora' AND display_id1=3128 AND npc_flags=1 AND gossip_menu_id=65013) OR "
         "(entry=919524 AND name='Rikka' AND display_id1=10746 AND npc_flags=1 AND gossip_menu_id=65014) OR "
         "(entry=919525 AND name='Zikko' AND display_id1=6060 AND npc_flags=1 AND gossip_menu_id=65015) OR "
         "(entry=919526 AND name='Sable' AND display_id1=2659 AND npc_flags=5 AND gossip_menu_id=65016)"),
        ('all teacher baseline fields', 'SELECT COUNT(*)=5 FROM creature_template t '
         'JOIN hs_rogue_seed_creature_template b ON b.entry=919501 WHERE t.entry BETWEEN 919522 AND 919526 AND ' + baseline),
        ('friendly stationary actors without scripts', 'SELECT COUNT(*)=5 FROM creature_template '
         "WHERE entry BETWEEN 919522 AND 919526 AND faction=85 AND movement_type=0 AND script_name=''"),
        ('unspawned objective credits', 'SELECT COUNT(*)=4 FROM creature_template '
         'WHERE entry BETWEEN 919527 AND 919530 AND npc_flags=0 AND unit_flags=33554434'),
        ('no confrontation counter template', 'SELECT COUNT(*)=0 FROM creature_template WHERE entry=919531'),
        ('quest restrictions and XP', 'SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919771 AND 919773 '
         'AND MinLevel=10 AND RequiredRaces=434 AND RequiredClasses=0 AND QuestLevel=0 AND ZoneOrSort=-9100 '
         'AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0 AND QuestFlags=0'),
        ('five native objectives and intro prerequisite', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919771 '
         'AND PrevQuestId=919704 AND NextQuestInChain=919772 AND SpecialFlags=0 '
         'AND ReqCreatureOrGOId1=919527 AND ReqCreatureOrGOId2=919528 AND ReqCreatureOrGOId3=919529 '
         'AND ReqCreatureOrGOId4=919530 AND ReqCreatureOrGOCount1=1 AND ReqCreatureOrGOCount2=1 '
         'AND ReqCreatureOrGOCount3=1 AND ReqCreatureOrGOCount4=1 AND RewItemId1=0 '
         'AND ReqItemId1=919637 AND ReqItemCount1=1'),
        ('four contact labels and initial Rikka instructions', "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919771 "
         "AND ObjectiveText1='Talk to Rikka' AND ObjectiveText2='Talk to Gora' "
         "AND ObjectiveText3='Talk to Zikko' AND ObjectiveText4='Talk to Sable' "
         "AND Objectives='Talk to Rikka about the missing deck.'"),
        ('unique native return objective item', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919637 "
         "AND name='Report to For''sen' AND class=12 AND buy_price=0 AND sell_price=0 "
         "AND max_count=1 AND stackable=1 AND bonding=4 AND flags=0 AND script_name=''"),
        ('required evidence and saved confrontation', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919772 '
         'AND PrevQuestId=919771 AND NextQuestInChain=919773 AND SpecialFlags=0 '
         'AND ReqItemId1=919636 AND ReqItemCount1=1 AND ReqItemId2=919648 AND ReqItemCount2=1 '
         'AND ReqCreatureOrGOId1=0 AND ReqCreatureOrGOCount1=0 AND RewItemId1=0'),
        ('native confrontation objective item', "SELECT COUNT(*)=1 FROM item_template WHERE entry=919648 "
         "AND name='Confront For''sen about the theft' AND class=12 AND buy_price=0 AND sell_price=0 "
         "AND max_count=1 AND stackable=1 AND bonding=4 AND flags=0 AND script_name=''"),
        ('native duel event and Rogue reward', 'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919773 '
         'AND PrevQuestId=919772 AND NextQuestInChain=0 AND SpecialFlags=2 AND RewItemId1=919623 AND RewItemCount1=1'),
        ('three teacher starts and turn-ins', 'SELECT '
         '(SELECT COUNT(*) FROM creature_questrelation)=3 AND '
         '(SELECT COUNT(*) FROM creature_questrelation WHERE id=919522 AND quest BETWEEN 919771 AND 919773)=3 AND '
         '(SELECT COUNT(*) FROM creature_involvedrelation)=3 AND '
         '(SELECT COUNT(*) FROM creature_involvedrelation WHERE id=919522 AND quest BETWEEN 919771 AND 919773)=3'),
        ('unique native evidence and three silver', 'SELECT COUNT(*)=1 FROM item_template WHERE entry=919636 '
         "AND class=12 AND display_id=31755 AND buy_price=300 AND sell_price=0 AND max_count=1 "
         "AND stackable=1 AND bonding=4 AND flags=0 AND script_name=''"),
        ('native vendor gated to incomplete recovery quest', 'SELECT COUNT(*)=1 FROM npc_vendor v '
         'JOIN conditions c ON c.condition_entry=v.condition_id WHERE v.entry=919526 AND v.item=919636 '
         'AND v.maxcount=0 AND v.incrtime=0 AND c.type=9 AND c.value1=919772 '
         'AND c.value2=1 AND c.value3=0 AND c.value4=0 AND c.flags=0'),
        ('vendor idempotence and existing stock retained', 'SELECT '
         '(SELECT COUNT(*) FROM npc_vendor WHERE entry=919526)=1 AND '
         '(SELECT COUNT(*) FROM npc_vendor WHERE entry=919501)=(SELECT COUNT(*) FROM hs_rogue_seed_npc_vendor) AND '
         '(SELECT COUNT(*) FROM conditions WHERE type=9 AND value1=919772)=1 AND '
         '(SELECT COUNT(*) FROM conditions WHERE type=8 AND value1=919772)=0'),
        ('thirty dialogue bindings', 'SELECT COUNT(*)=30 FROM npc_text n '
         'JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 WHERE '
         '((n.ID BETWEEN 9195180 AND 9195184) OR (n.ID BETWEEN 9195190 AND 9195214)) '
         'AND n.Probability0=1 AND b.female_text=b.male_text'),
        ('five greetings', 'SELECT COUNT(*)=5 FROM gossip_menu g JOIN npc_text n ON n.ID=g.text_id '
         'WHERE g.entry BETWEEN 65012 AND 65016'),
        ('quest-only merchant option during recovery', 'SELECT COUNT(*)=1 FROM gossip_menu_option g '
         'JOIN conditions c ON c.condition_entry=g.condition_id WHERE g.menu_id=65016 AND g.id=0 '
         'AND g.option_id=3 AND g.npc_option_npcflag=4 AND c.type=9 AND c.value1=919772 '
         'AND c.value2=1 AND c.value3=0 AND c.value4=0 AND c.flags=0'),
        ('no regular merchant option: the daily card is module gossip', 'SELECT COUNT(*)=1 FROM gossip_menu_option '
         'WHERE menu_id=65016'),
        ('separate completed-contact directions', "SELECT "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195208 AND male_text LIKE '%behind the waterfall%')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195209 AND male_text LIKE '%Ring of Valor%')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195211 AND male_text LIKE '%Three silver for the deck%')=1"),
        ('Zikko is a mage with magic dice', "SELECT "
         "(SELECT COUNT(*) FROM creature_template WHERE entry=919525 AND subname='Mage and bookmaker')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195196 AND male_text LIKE '%a mage%')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195198 AND male_text LIKE '%magic%')=1"),
        ('Sable greeting and sale framing', "SELECT "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195184 "
         "AND male_text='Three silver. For the case, the cards, and my remarkable restraint.')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195199 AND male_text LIKE 'I trade cards.%')=1 AND "
         "(SELECT COUNT(*) FROM quest_template WHERE entry=919772 AND Details LIKE '%she sells.%' "
         "AND Details NOT LIKE '%recovery fee%' AND OfferRewardText LIKE '%card trade is open%')=1"),
        ('Sable knows theft but not thief', "SELECT "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195200 AND male_text LIKE '%Didn''t see the theft coming%')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195201 AND male_text LIKE 'No. Whoever it was%' "
         "AND male_text NOT LIKE '%For''sen%')=1 AND "
         "(SELECT COUNT(*) FROM broadcast_text WHERE entry=9195204 AND male_text LIKE '%Lifted it off Sable%')=1"),
        ('Basic Rogue roster and no ordinary direct opponent', 'SELECT '
         '(SELECT COUNT(*) FROM hearthstone_opponent WHERE creature_entry=919522)=0 AND '
         '(SELECT COUNT(*) FROM hearthstone_npc_unlock WHERE creature_entry=919522 AND quest_id=919773 '
         "AND deck_key='normal_rogue' AND faction='horde')=1"),
        ('existing Rogue reward unchanged', 'SELECT COUNT(*)=1 FROM item_template i '
         'JOIN hs_rogue_seed_item_template b ON b.entry=i.entry WHERE i.entry=919623 '
         'AND i.display_id=b.display_id AND i.class=b.class AND i.flags=b.flags AND i.name=b.name'),
        ('baseline template unchanged', 'SELECT COUNT(*)=1 FROM creature_template t '
         'JOIN hs_rogue_seed_creature_template b ON t.entry=b.entry WHERE t.entry=919501 AND ' + baseline +
         ' AND t.name=b.name AND t.npc_flags=b.npc_flags AND t.gossip_menu_id=b.gossip_menu_id'),
        ('no creature or gameobject spawns', 'SELECT (SELECT COUNT(*) FROM creature)=0 AND (SELECT COUNT(*) FROM gameobject)=0'),
    ]
    assertions = '\n'.join(f"SELECT '{label}', ({query});" for label, query in checks)
    sql = '\n'.join(setup) + '\n' + (migrations + '\n' + assertions + '\n') * 2
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
    print(f'Rogue migrations passed {len(rows)} assertions over two runs against '
          f'{len(tables)} temporary schema copies. Live tables unchanged.')


if __name__ == '__main__':
    main()
