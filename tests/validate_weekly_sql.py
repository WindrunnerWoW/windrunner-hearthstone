"""Check weekly IDs read-only, then run migration 0056 twice on temporary schema copies."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', default='C:/TurtleDev/03_runtime_data/server-hearthstone/mangosd.conf')
    parser.add_argument('--client', default='C:/TurtleDev/03_runtime_data/database/mariadb/bin/mysql.exe')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    config = Path(args.config).read_text(encoding='utf-8-sig')
    host, port, user, password, database = re.search(r'^\s*WorldDatabase.Info\s*=\s*"([^"]+)"', config, re.M)[1].split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    def run(sql):
        result = subprocess.run([args.client, '--host='+host, '--port='+port, '--user='+user,
                                 '--database='+database, '--batch', '--skip-column-names'],
                                input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout.strip().splitlines()
    occupied = run("SELECT CONCAT('quest ',entry) FROM quest_template WHERE entry BETWEEN 919980 AND 919994 "
                   "OR entry BETWEEN 920110 AND 920118; SELECT CONCAT('stub ',entry) FROM creature_template "
                   "WHERE entry BETWEEN 920020 AND 920043; SELECT CONCAT('condition ',condition_entry) "
                   "FROM conditions WHERE condition_entry BETWEEN 920110 AND 920112;")
    if occupied:
        raise SystemExit('Occupied weekly IDs (inspect before applying): ' + repr(occupied))
    print('Live quest, credit and condition ID ranges are free.')
    tables = ('quest_template', 'creature_template', 'conditions', 'creature_questrelation', 'creature_involvedrelation')
    setup = '\n'.join(f'CREATE TEMPORARY TABLE `hs_weekly_seed_{table}` LIKE `{table}`; '
                      f'CREATE TEMPORARY TABLE `{table}` LIKE `hs_weekly_seed_{table}`;' for table in tables)
    migration = (root / 'data/sql/world/0056_hearthstone_weekly_quests.sql').read_text(encoding='utf-8')
    checks = [
        'SELECT COUNT(*)=23 FROM quest_template;',
        'SELECT COUNT(*)=23 FROM creature_template;',
        'SELECT COUNT(*)=46 FROM creature_involvedrelation;',
        'SELECT COUNT(*)=0 FROM creature_questrelation;',
        'SELECT COUNT(*)=3 FROM conditions;',
        'SELECT COUNT(*)=23 FROM quest_template WHERE SpecialFlags=2049 AND RequiredCondition=920112 '
        'AND QuestFlags=320 AND MinLevel=1 AND QuestLevel=1 AND Type=0 '
        'AND RewItemId1=919602 AND RewItemCount1=1 AND RewXP=0 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0 '
        'AND RewRepValue1=0 AND RewRepValue2=0 AND RewRepValue3=0 AND RewRepValue4=0 AND RewRepValue5=0;',
        'SELECT COUNT(*)=1 FROM quest_template WHERE entry=919992 AND ReqCreatureOrGOCount1=20;',
        'SELECT COUNT(*)=23 FROM quest_template q JOIN creature_template c ON q.ReqCreatureOrGOId1=c.entry '
        'WHERE c.npc_flags=0 AND c.unit_flags=33554432;',
        'SELECT COUNT(*)=1 FROM conditions WHERE condition_entry=920112 AND type=-2 AND value1=920110 AND value2=920111;',
        'SELECT COUNT(*)=2 FROM conditions WHERE type=8 AND value1 IN (919706,919716);',
    ]
    rows = run(setup + '\n' + (migration + '\n' + '\n'.join(checks)) * 2)
    if len(rows) != len(checks)*2 or any(row != '1' for row in rows):
        raise SystemExit('Weekly temporary SQL assertions failed: ' + repr(rows))
    print(f'Weekly migration: {len(rows)} assertions passed over two temporary-table runs. No live writes.')
    character = (root / 'data/sql/character/0020_hearthstone_weekly_quests.sql').read_text(encoding='utf-8')
    character = character.replace('CREATE TABLE IF NOT EXISTS', 'CREATE TEMPORARY TABLE IF NOT EXISTS')
    rows = run(character + '\n' + character + '''
INSERT INTO hearthstone_weekly_match (durable_id,match_no,guid,quest_mask) VALUES (1,1,100,7),(1099511627777,0,200,3);
SELECT COUNT(*)=2 FROM hearthstone_weekly_match;
UPDATE hearthstone_weekly_match SET quest_mask=quest_mask & 5 WHERE guid=100;
SELECT quest_mask=5 AND consumed=0 FROM hearthstone_weekly_match WHERE durable_id=1 AND match_no=1;
UPDATE hearthstone_weekly_match SET consumed=1 WHERE durable_id=1 AND match_no=1;
SELECT COUNT(*)=0 FROM hearthstone_weekly_match WHERE durable_id=1 AND consumed=0;
SELECT guid=200 AND quest_mask=3 AND consumed=0 FROM hearthstone_weekly_match WHERE durable_id=1099511627777;
INSERT INTO hearthstone_weekly_streak (guid,wins) VALUES (100,2),(200,1);
SELECT COALESCE((SELECT wins FROM hearthstone_weekly_streak WHERE guid=300),0)=0;
INSERT INTO hearthstone_weekly_streak (guid,wins) VALUES (100,0) ON DUPLICATE KEY UPDATE wins=VALUES(wins);
SELECT wins=0 FROM hearthstone_weekly_streak WHERE guid=100;
DELETE FROM hearthstone_weekly_streak WHERE guid=100;
SELECT COUNT(*)=1 FROM hearthstone_weekly_streak WHERE guid=200 AND wins=1;
INSERT INTO hearthstone_weekly_carry (guid,quest,amount) VALUES (100,919992,9) ON DUPLICATE KEY UPDATE amount=VALUES(amount);
INSERT INTO hearthstone_weekly_carry (guid,quest,amount) VALUES (100,919992,8) ON DUPLICATE KEY UPDATE amount=VALUES(amount);
SELECT COALESCE((SELECT amount FROM hearthstone_weekly_carry WHERE guid=100 AND quest=919992),0)=8;
DELETE FROM hearthstone_weekly_carry WHERE guid=100 AND quest=919992;
SELECT COALESCE((SELECT amount FROM hearthstone_weekly_carry WHERE guid=100 AND quest=919992),0)=0;
''')
    if len(rows) != 9 or any(row != '1' for row in rows):
        raise SystemExit('Weekly character temporary SQL assertions failed: ' + repr(rows))
    print('Character migration: nine persistence/isolation assertions passed, all tables temporary.')


if __name__ == '__main__':
    main()
