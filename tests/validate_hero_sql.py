"""Run character migration 0021 and the portrait SQL builders on temporary tables. No live writes."""
import argparse
import os
import re
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', default='C:/TurtleDev/03_runtime_data/server-hearthstone/mangosd.conf')
    parser.add_argument('--client', default='C:/TurtleDev/03_runtime_data/database/mariadb/bin/mysql.exe')
    parser.add_argument('--exe', required=True, help='hero_portrait_tests.exe, which prints the builder SQL with --sql')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    config = Path(args.config).read_text(encoding='utf-8-sig')
    host, port, user, password, database = re.search(
        r'^\s*CharacterDatabase.Info\s*=\s*"([^"]+)"', config, re.M)[1].split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password

    def run(sql):
        result = subprocess.run([args.client, '--host=' + host, '--port=' + port, '--user=' + user,
                                 '--database=' + database, '--batch', '--skip-column-names'],
                                input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout.strip().splitlines()

    migration = (root / 'data/sql/character/0021_hearthstone_hero_portraits.sql').read_text(encoding='utf-8')
    assert chr(92) not in migration, 'a backslash in the migration'
    assert not [line for line in migration.splitlines() if ';' in line.rstrip(';')], 'a semicolon inside a line'
    migration = migration.replace('CREATE TABLE IF NOT EXISTS', 'CREATE TEMPORARY TABLE IF NOT EXISTS')

    builders = subprocess.run([args.exe, '--sql'], capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(builders) == 18 + 3 + 2 + 3, len(builders)
    (grant1, grant1b, grant2, count0, look0, claim1, unlock1, count1, look1, claim2, unlock2, count2, owned,
     set1, set2, choices, clear, choices2, gm1, gm2, set3) = builders[:21]
    revoke = builders[21:23]
    reset = builders[23:]
    first = re.search(r"`portrait_id` = '([^']+)'", claim1)[1]
    second = re.search(r"`portrait_id` = '([^']+)'", claim2)[1]
    script = [migration, migration,  # the second run must change nothing
              grant1, grant1b, grant2, count0, look0, claim1, unlock1, count1, look1, claim2, unlock2, count2, owned,
              set1, set2, choices, clear, choices2,
              'SELECT league, portrait_id, claim_key FROM hearthstone_hero_credit WHERE account_id = 7 ORDER BY league',
              "SELECT COUNT(*) FROM hearthstone_hero_portrait WHERE source = 'legend1' AND unlocked_at > 0",
              # GM credits take the leagues from 1000000 up
              gm1, gm2, 'SELECT league FROM hearthstone_hero_credit WHERE account_id = 9 ORDER BY league',
              # revoking removes the portrait and the choice that used it, and keeps the other portrait
              set3, *revoke,
              'SELECT portrait_id FROM hearthstone_hero_portrait WHERE account_id = 7',
              'SELECT COUNT(*) FROM hearthstone_hero_choice WHERE account_id = 7',
              *reset,
              'SELECT (SELECT COUNT(*) FROM hearthstone_hero_credit WHERE account_id = 7) + '
              '(SELECT COUNT(*) FROM hearthstone_hero_portrait WHERE account_id = 7) + '
              '(SELECT COUNT(*) FROM hearthstone_hero_choice WHERE account_id = 7)',
              'SELECT COUNT(*) FROM hearthstone_hero_credit WHERE account_id = 9']
    rows = run(';\n'.join(s.rstrip(';') for s in script) + ';')
    tab = chr(9)
    expected = ['2',                        # a replayed grant added nothing
                '1', first,                 # one credit left after the claim, and the replay lookup
                '0',                        # none left
                *sorted([first, second]),   # owned, in primary key order
                'MAGE' + tab + second,      # REPLACE kept the last choice only
                '1' + tab + first + tab + 'abcdef123456',   # the oldest league was claimed first
                '2' + tab + second + tab + 'ghijkl654321',
                '2',
                '1000000', '1000001',      # GM credits
                first,                     # only the portrait that was not revoked is left
                '0',                       # and the choice of the revoked one is gone
                '0',                       # reset forgets the account
                '2']                       # and touches no other account
    if rows != expected:
        raise SystemExit('Hero portrait SQL assertions failed: ' + repr(rows) + ' expected ' + repr(expected))

    # A claim key can be used once per account: the unique index refuses the second use.
    try:
        run(migration + "\nINSERT INTO hearthstone_hero_credit (account_id,league,claim_key) VALUES (8,1,'abcdef123456');\n"
            "INSERT INTO hearthstone_hero_credit (account_id,league,claim_key) VALUES (8,2,'abcdef123456');")
    except SystemExit as error:
        if 'Duplicate entry' not in str(error):
            raise
    else:
        raise SystemExit('The unique claim key did not refuse a second use')
    print('Hero portrait migration and %d builder statements: assertions passed over temporary tables.' % len(builders))


if __name__ == '__main__':
    main()
