#!/usr/bin/env python3
"""Validate the Maren/Ada presentation update twice in temporary tables only."""
import argparse
import os
from pathlib import Path
import re
import subprocess


LITERAL = r"'(?:''|\\.|[^'\\])*'|[0-9]+"
ALLOWED = {
    ('creature_template', 919561): {'display_id1'},
    ('creature_template', 919577): {'display_id1', 'name'},
    ('item_template', 919650): {'display_id', 'description'},
    ('quest_template', 919851): {
        'details', 'objectives', 'requestitemstext', 'offerrewardtext'},
    ('broadcast_text', 9195370): {'male_text', 'female_text'},
    ('broadcast_text', 9195389): {'male_text', 'female_text'},
    ('broadcast_text', 9195390): {'male_text', 'female_text'},
}


def parse_updates(source):
    """Accept only a bounded literal UPDATE grammar, before opening a DB session."""
    # Keep quoted text intact while removing SQL comments. Semicolons inside a
    # quoted sentence do not end a statement.
    code = re.sub(r"'(?:''|\\.|[^'\\])*'|--[^\r\n]*|/\*[\s\S]*?\*/",
                  lambda match: match.group() if match.group().startswith("'") else ' ',
                  source)
    statements = []
    start = 0
    for token in re.finditer(r"'(?:''|\\.|[^'\\])*'|;", code):
        if token.group() == ';':
            statements.append(code[start:token.start()].strip())
            start = token.end()
    if code[start:].strip():
        raise SystemExit('Every presentation UPDATE must end with a semicolon')
    updates = {}
    assignment = re.compile(r'\s*`?([A-Za-z_]\w*)`?\s*=\s*(' + LITERAL + r')\s*')
    for statement in statements:
        match = re.fullmatch(r'UPDATE\s+`?(\w+)`?\s+SET\s+([\s\S]+?)\s+'
                             r'WHERE\s+`?entry`?\s*=\s*(\d+)\s*', statement, re.I)
        if not match:
            raise SystemExit('Only unqualified literal UPDATE ... WHERE entry=<ID> is allowed')
        key = (match.group(1).lower(), int(match.group(3)))
        if key not in ALLOWED or key in updates:
            raise SystemExit('Unexpected or duplicate presentation UPDATE: ' + repr(key))
        values = {}
        position = 0
        while position < len(match.group(2)):
            value = assignment.match(match.group(2), position)
            if not value:
                raise SystemExit('Only quoted text or integer assignments are allowed')
            field = value.group(1).lower()
            if field in values:
                raise SystemExit('Duplicate presentation field: ' + field)
            values[field] = value.group(2)
            position = value.end()
            if position == len(match.group(2)):
                break
            if match.group(2)[position] != ',':
                raise SystemExit('Invalid presentation assignment separator')
            position += 1
        if set(values) != ALLOWED[key]:
            raise SystemExit('Unexpected presentation fields: ' + repr(key))
        updates[key] = values
    if set(updates) != set(ALLOWED):
        raise SystemExit('Presentation migration is missing a required UPDATE')
    required = {
        ('creature_template', 919561, 'display_id1'): '5092',
        ('creature_template', 919577, 'display_id1'): '6975',
        ('creature_template', 919577, 'name'): "'Ada Sparkplugz'",
        ('item_template', 919650, 'display_id'): '7404',
    }
    for (table, entry, field), value in required.items():
        if updates[(table, entry)][field] != value:
            raise SystemExit('Owner-selected name, model or icon mismatch')
    for entry in (9195370, 9195389, 9195390):
        values = updates[('broadcast_text', entry)]
        if values['male_text'] != values['female_text']:
            raise SystemExit('Dialogue variants must match')
    return updates


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    migration = (root / 'data/sql/world/0037_hearthstone_maren_and_ada_appearance.sql').read_text()
    updates = parse_updates(migration)
    warlock = (root / 'data/sql/world/0033_hearthstone_alliance_warlock_quests.sql').read_text()
    paladin = (root / 'data/sql/world/0036_hearthstone_alliance_paladin_quests.sql').read_text()
    if not re.search(r'SELECT\s+919561\s+AS\s+`entry`,\s*5092\s+AS\s+`display`', warlock):
        raise SystemExit('Clean-install Maren model differs from forward migration')
    if not re.search(r"UNION\s+ALL\s+SELECT\s+919577,\s*6975,\s*'Ada Sparkplugz'", paladin):
        raise SystemExit('Clean-install Ada identity differs from forward migration')
    if not re.search(r"VALUES\s*\(919650,12,0,'Ada''s Card Case',7404,", paladin):
        raise SystemExit('Clean-install card-case icon differs from forward migration')
    for (table, _), values in updates.items():
        if table in ('broadcast_text', 'quest_template', 'item_template'):
            for literal in values.values():
                if literal.startswith("'") and literal not in paladin:
                    raise SystemExit('Clean-install Paladin text differs from forward migration')

    setting = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                        args.config.read_text(encoding='utf-8-sig'), re.M)
    if not setting:
        raise SystemExit('WorldDatabase.Info missing')
    host, port, user, password, database = setting.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--host=' + host, '--port=' + port, '--user=' + user,
               '--database=' + database, '--batch', '--skip-column-names']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True,
                                env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    tables = {
        'creature_template': 'entry IN (919561,919577,919570,919571,6374,62100)',
        'item_template': 'entry IN (4404,919650,919621)',
        'broadcast_text': '(entry BETWEEN 9195370 AND 9195390) OR (entry BETWEEN 9195330 AND 9195366)',
        'quest_template': 'entry IN (919851,919852,919853,919841,919842,919843)',
        'creature': 'id IN (919561,919577,919570,919571,6374,62100)',
    }
    checksum_sql = 'CHECKSUM TABLE ' + ','.join('`' + table + '`' for table in tables) + ';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(tables) or any(row.endswith('\tNULL') for row in before.splitlines()):
        raise SystemExit('Incomplete initial live checksums')
    names = ','.join("'" + table + "'" for table in tables)
    metadata = query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS '
                     'WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN (' + names + ') '
                     'ORDER BY TABLE_NAME,ORDINAL_POSITION;')
    fields = {table: [] for table in tables}
    for row in metadata.splitlines():
        table, field = row.split('\t')
        fields[table].append(field)
    if any(not columns for columns in fields.values()):
        raise SystemExit('Incomplete native schema metadata')
    setup = []
    for table, predicate in tables.items():
        seed = 'hs_presentation_seed_' + table
        setup.extend([f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                      f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {predicate};',
                      f'CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;',
                      f'INSERT INTO `{table}` SELECT * FROM `{seed}`;'])
    checks = [
        ('native Maren model', 'SELECT COUNT(*)=1 FROM hs_presentation_seed_creature_template '
         'WHERE entry=6374 AND display_id1=5092'),
        ('native Ada model', 'SELECT COUNT(*)=1 FROM hs_presentation_seed_creature_template '
         'WHERE entry=62100 AND display_id1=6975'),
        ('native Silver Contact icon', 'SELECT COUNT(*)=1 FROM hs_presentation_seed_item_template '
         'WHERE entry=4404 AND display_id=7404'),
    ]
    for (table, entry), values in updates.items():
        checks.append((f'presentation {table} {entry}',
                       f'SELECT COUNT(*)=1 FROM `{table}` WHERE entry={entry} AND ' +
                       ' AND '.join(f'`{field}` <=> {literal}' for field, literal in values.items())))
    for table in tables:
        seed = 'hs_presentation_seed_' + table
        key = 'guid' if table == 'creature' else 'entry'
        predicates = []
        for field in fields[table]:
            exceptions = [entry for (target, entry), values in updates.items()
                          if target == table and field.lower() in values]
            same = f't.`{field}` <=> b.`{field}`'
            if exceptions:
                same = f'(t.`{key}` IN ({",".join(map(str, exceptions))}) OR ({same}))'
            predicates.append(same)
        checks.extend([
            (f'preserve every other column {table}',
             f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) FROM `{table}` t '
             f'JOIN `{seed}` b ON t.`{key}`=b.`{key}` WHERE ' + ' AND '.join(predicates)),
            (f'preserve row count {table}',
             f'SELECT COUNT(*)=(SELECT COUNT(*) FROM `{seed}`) FROM `{table}`'),
        ])
    assertions = '\n'.join(f"SELECT '{label}', ({sql});" for label, sql in checks)
    try:
        output = query('\n'.join(setup) + '\n' + migration + '\n' + assertions +
                       '\n' + migration + '\n' + assertions)
    finally:
        if query(checksum_sql) != before:
            raise SystemExit('Live-table checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != len(checks) * 2 or failures:
        raise SystemExit('Temporary presentation assertions failed: ' + repr(failures or rows))
    print(f'NPC presentation migration passed {len(rows)} assertions over two temporary runs / '
          f'{len(tables)} complete schemas. Clean-install texts match. Live checksums unchanged.')


if __name__ == '__main__':
    main()
