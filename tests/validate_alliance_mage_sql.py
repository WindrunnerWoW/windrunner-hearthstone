#!/usr/bin/env python3
"""Validate Alliance Mage twice in full-schema temporary tables; never write live data."""
import argparse
from decimal import Decimal
import json
import os
from pathlib import Path
import re
import struct
import subprocess


# Independent exact text expectation extracted from the owner's approved readthrough.
EXPECTED = {'quests': [{'entry': 919891,
             'Title': "A Traveler's Memory",
             'Details': 'My father copies histories for a living. He taught me the names of distant cities '
                        'long before I saw one for myself.$B$BNow I send him accounts from the road. Before we '
                        "look around Darnassus, I'd like to hear which stories have reached you.",
             'Objectives': "Answer Mitchell Jones's ten lore questions.",
             'OfferRewardText': 'Thank you. It is good to hear which stories have traveled farther than I '
                                'have.$B$BNow let us put a few sights beside those names. My father will be '
                                'pleased to receive a letter with something other than another war in it.',
             'RequestItemsText': 'We can leave the remaining questions for a moment. I have no bell to ring '
                                 'when you take too long.',
             'credits': [919600],
             'counts': [10],
             'labels': ["Answer Mitchell's lore questions"]},
            {'entry': 919892,
             'Title': 'Beyond the Footnotes',
             'Details': 'Most accounts my father copies are all armies and battles. He wants to know what a '
                        "city feels like when nobody is fighting.$B$BI've left a speaking image on Warrior's "
                        "Terrace, another on Craftsmen's Terrace, and a third beside the fountain in the "
                        'Temple of the Moon. Go and hear what caught my attention. Visit them in whichever '
                        'order suits you, then find the solid version of me here by the bank.',
             'Objectives': "Speak with Mitchell's images on Warrior's Terrace, on Craftsmen's Terrace, and at "
                           'the Temple of the Moon, then return to Mitchell Jones.',
             'OfferRewardText': 'People keeping watch, making things, and finding a moment to pray. That '
                                'sounds much more like a city than the paragraph I arrived with.$B$BThank you '
                                'for looking with me. My father will have something worth picturing this time.',
             'RequestItemsText': 'My images are still at the three places I mentioned. Hear what each has to '
                                 'say, then find me here by the bank.',
             'credits': [919601, 919602, 919603],
             'counts': [1, 1, 1],
             'labels': ["Discuss Warrior's Terrace", "Discuss Craftsmen's Terrace", 'Discuss the Temple']},
            {'entry': 919893,
             'Title': 'A Question of Timing',
             'Details': "I'm living the arcane dream, $n. A new city, good company, and time for a game before "
                        'I write home.$B$BMy Mage deck has plenty of power, provided I play the right card at '
                        "the right moment. I've been known to get excited and spend it too soon.$B$BBring "
                        'whichever deck suits you. Defeat me, and this Mage Deck is yours.',
             'Objectives': 'Defeat Mitchell Jones in a game of Hearthstone.',
             'OfferRewardText': 'Well played, $n. Take this Mage Deck. I hope it gives you as much pleasure as '
                                'mine has given me.$B$BMy father will ask whether I won. I suppose an honest '
                                'account ought to include that too.',
             'RequestItemsText': 'The cards are ready whenever you are. I am quite willing to be corrected '
                                 'again.',
             'credits': [],
             'counts': [],
             'labels': ['Defeat Mitchell at Hearthstone']}],
 'texts': {'0': 'Mitchell Jones. My father calls this historical research. I call it living the arcane dream: '
                'seeing the world and finding out which parts the books missed.',
           '1': 'Have you traveled far? My father copies histories in Stormwind. I send him accounts from the '
                'road, and he sends back questions in the margins.',
           '2': 'Shall we begin? An honest guess is quite welcome.',
           '3': 'There you are. We can carry on where we left off.',
           '4': 'Enough questions. Names are useful, but they do not tell you everything about a place. Let us '
                'put a few sights beside them.',
           '5': 'That is all ten. Thank you for staying with me. There are a few places I would like you to '
                'see next.',
           '6': 'We have the names and dates. Now I would like to show you what my father keeps asking about: '
                'the city between the battles.',
           '7': 'My images are still at the three places I mentioned. Hear what each has to say, then find me '
                'here by the bank.',
           '8': 'You have heard from all three images. Good. I was beginning to suspect they were enjoying the '
                'city more than I was.',
           '9': 'That gives me something worth writing home. There is time for a game before I put pen to '
                'paper.',
           '10': 'No history questions this time. I would like to see what you do when the answer has to be '
                 'played.',
           '11': 'There is room for another game. Shall we try again?',
           '12': 'A fine game. I have your Mage Deck ready, and a correction to make in my account of my own '
                 'skill.',
           '13': 'Good to see you, $n. My letter has become rather long. I have decided to blame the city. It '
                 'has far too much to notice.',
           '14': 'The next place that makes me put the book down. I have a long list. My father has a longer '
                 'one.',
           '20': "Before the Dark Portal opened, which world was the orcs' home?",
           '21': "That's right. The orcs came from Draenor. The Dark Portal brought their invasion to Azeroth.",
           '22': 'Not quite. The orcs came from Draenor. The Dark Portal brought their invasion to Azeroth.',
           '23': "Now, a question about my profession. Which human mage worked with Gul'dan to open the Dark "
                 'Portal?',
           '24': "That's right. Medivh. He was possessed by Sargeras. It is an uncomfortable chapter in my "
                 "profession's history.",
           '25': 'Not quite. Medivh. He was possessed by Sargeras. It is an uncomfortable chapter in my '
                 "profession's history.",
           '26': 'Let us stay with the First War. Which human city fell to the Horde at its end?',
           '27': "That's right. Stormwind. Much of what you see there today was built after that defeat.",
           '28': 'Not quite. Stormwind. Much of what you see there today was built after that defeat.',
           '29': 'A later war, then. Before Arthas became a death knight, which kingdom was he prince of?',
           '30': "That's right. Lordaeron. He once set out to protect its people from the Scourge.",
           '31': 'Not quite. Lordaeron. He once set out to protect its people from the Scourge.',
           '32': 'Another journey west. Which mage led human survivors to Kalimdor during the Third War?',
           '33': "That's right. Jaina Proudmoore. She later founded Theramore.",
           '34': 'Not quite. Jaina Proudmoore. She later founded Theramore.',
           '35': 'That brings us to the stand in Kalimdor. Where did the allies make their final stand against '
                 'Archimonde in the Third War?',
           '36': "That's right. Mount Hyjal. Humans, orcs, and night elves held the line while Malfurion "
                 'prepared the blow that destroyed him.',
           '37': 'Not quite. Mount Hyjal. Humans, orcs, and night elves held the line while Malfurion prepared '
                 'the blow that destroyed him.',
           '38': 'What about Gnomeregan? What creatures invaded it from below?',
           '39': "That's right. Troggs. The gnomes' attempt to stop them left their own city poisoned.",
           '40': "Not quite. Troggs. The gnomes' attempt to stop them left their own city poisoned.",
           '41': "Now for Lordaeron again. What name did Sylvanas's free-willed undead take?",
           '42': "That's right. The Forsaken. They broke from the Scourge and claimed the ruins of Lordaeron.",
           '43': 'Not quite. The Forsaken. They broke from the Scourge and claimed the ruins of Lordaeron.',
           '44': "Now for the city around us. Who is honored in Darnassus's Temple of the Moon?",
           '45': "That's right. Elune, the moon goddess. Tyrande Whisperwind is her High Priestess.",
           '46': 'Not quite. Elune, the moon goddess. Tyrande Whisperwind is her High Priestess.',
           '47': 'One last question. What is the name of the enormous tree beneath this city?',
           '48': "That's right. Teldrassil. Darnassus rests among its boughs. A rather ambitious foundation.",
           '49': 'Not quite. Teldrassil. Darnassus rests among its boughs. A rather ambitious foundation.',
           '50': "Ahead of you again. The arcane dream has its advantages. I wanted to see how Warrior's "
                 'Terrace looks on an ordinary day.',
           '51': 'The Sentinels. People speak of night elves as if their trees keep them safe by themselves. '
                 'Someone still has to stand watch.',
           '52': 'That Darnassus has guards as well as groves. I would like him to picture the people keeping '
                 'the city safe, not only the branches above it.',
           '53': 'Exactly. And there is more to see here than the guards, if you have the patience for it. '
                 'I keep finding things worth writing down. The one with the ink on his fingers is the '
                 'original, in case you lose track of us.',
           '54': 'The defenders belong in the account as much as the city they guard. There is more to see, '
                 'and my other images have noticed things I walked straight past.',
           '55': "I wanted a look at Craftsmen's Terrace. My father's last letter asked who makes the things a "
                 'city needs. Most of his books have very little to say about a worn pair of boots.',
           '56': 'Usually a line between two battles. Yet someone has to stitch, shape, mend, and begin again. '
                 'Even a city in a tree needs people who know their tools.',
           '57': 'It is. I spent three paragraphs on the height of Teldrassil and forgot to mention that '
                 'people live ordinary lives up here.',
           '58': "I'll give it a few. A description of the world ought to leave room for the people doing the "
                 'work. There is more to see, and I would hate to rush any of it.',
           '59': 'The craftsmen have their place in the letter. There is still more to see, and my other '
                 'images have been busy noticing things of their own.',
           '60': 'Here we are. I wanted to see the Temple of the Moon after reading its name so often. I have '
                 'been keeping my voice down.',
           '61': 'How little a visitor can know from a name alone. I can tell my father whose temple this is. '
                 'I cannot pretend that makes me understand every person who comes here.',
           '62': 'That I stood beside this fountain, listened, and knew enough to be a guest. Some parts of an '
                 'account ought to begin with a question.',
           '63': 'I hope so. There is more to this place than a name and a fountain, and I would rather '
                 'learn it quietly than shout it into a letter.',
           '64': 'This part of the letter can wait until I understand more. There is more to see, and I '
                 'would rather look twice than write something hasty.'},
 'menus': {'65230': 9195600,
           '65231': 9195601,
           '65232': 9195602,
           '65233': 9195603,
           '65234': 9195604,
           '65235': 9195605,
           '65236': 9195606,
           '65237': 9195607,
           '65238': 9195608,
           '65239': 9195609,
           '65240': 9195610,
           '65241': 9195611,
           '65242': 9195612,
           '65243': 9195613,
           '65244': 9195614,
           '65247': 9195650,
           '65248': 9195655,
           '65249': 9195660,
           '65250': 9195654,
           '65251': 9195659,
           '65252': 9195664},
 'actors': [[919596, 'Mitchell Jones', 'Hearthstone teacher', 3, 65230, ''],
            [919597, 'Mitchell Jones', 'Speaking image', 1, 65247, '9617'],
            [919598, 'Mitchell Jones', 'Speaking image', 1, 65248, '9617'],
            [919599, 'Mitchell Jones', 'Speaking image', 1, 65249, '9617'],
            [919600, 'HS Mage Quiz Answer', '', 0, 0, ''],
            [919601, 'HS Mage Warrior Terrace', '', 0, 0, ''],
            [919602, 'HS Mage Craftsmen Terrace', '', 0, 0, ''],
            [919603, 'HS Mage Temple Conversation', '', 0, 0, '']],
 'quiz': [{'question': "Before the Dark Portal opened, which world was the orcs' home?",
           'answers': ['They came from Azeroth.', 'Their home was Draenor.', 'Kalimdor, I think.'],
           'correct': 1,
           'acknowledgement': 'That explains the crossing.'},
          {'question': "Now, a question about my profession. Which human mage worked with Gul'dan to open the "
                       'Dark Portal?',
           'answers': ['That was Khadgar.', 'It was Antonidas.', 'Medivh helped open it.'],
           'correct': 2,
           'acknowledgement': "I'll remember the name."},
          {'question': 'Let us stay with the First War. Which human city fell to the Horde at its end?',
           'answers': ['Stormwind fell.', 'It was Lordaeron.', 'Dalaran was destroyed.'],
           'correct': 0,
           'acknowledgement': 'I see. Stormwind was rebuilt.'},
          {'question': 'A later war, then. Before Arthas became a death knight, which kingdom was he prince '
                       'of?',
           'answers': ['He was prince of Stromgarde.', 'Lordaeron was his kingdom.', 'He belonged to Alterac.'],
           'correct': 1,
           'acknowledgement': 'Lordaeron. I understand.'},
          {'question': 'Another journey west. Which mage led human survivors to Kalimdor during the Third War?',
           'answers': ['Medivh led them.', 'That was Khadgar.', 'Jaina Proudmoore.'],
           'correct': 2,
           'acknowledgement': 'That was quite a journey.'},
          {'question': 'That brings us to the stand in Kalimdor. Where did the allies make their final stand '
                       'against Archimonde in the Third War?',
           'answers': ['At Blackrock Mountain.', 'At Mount Hyjal.', 'At the gates of Ironforge.'],
           'correct': 1,
           'acknowledgement': "I'll remember Hyjal."},
          {'question': 'What about Gnomeregan? What creatures invaded it from below?',
           'answers': ['Troggs.', 'Kobolds, surely.', 'The Dark Iron dwarves.'],
           'correct': 0,
           'acknowledgement': 'A hard way to lose a city.'},
          {'question': "Now for Lordaeron again. What name did Sylvanas's free-willed undead take?",
           'answers': ['They called themselves the Scourge.', 'The Cult of the Damned.', 'The Forsaken.'],
           'correct': 2,
           'acknowledgement': "That's an important difference."},
          {'question': "Now for the city around us. Who is honored in Darnassus's Temple of the Moon?",
           'answers': ['The Earth Mother.', "It is Elune's temple.", 'They honor the Light.'],
           'correct': 1,
           'acknowledgement': "I'll remember whose temple it is."},
          {'question': 'One last question. What is the name of the enormous tree beneath this city?',
           'answers': ['We are standing on Nordrassil.', 'Aldrassil, perhaps.', 'The tree is Teldrassil.'],
           'correct': 2,
           'acknowledgement': "I'd like to see more of it."}],
 'tours': [{'name': "Warrior's Terrace",
            'replies': ['What caught your attention?',
                        'What will you write to your father?',
                        'The watch belongs in your account too.']},
           {'name': "Craftsmen's Terrace",
            'replies': ['Not much of a chapter for the books?',
                        'Is that going in your letter?',
                        'The work deserves a few lines.']},
           {'name': 'Temple of the Moon',
            'replies': ['What did the books leave out?',
                        'What can you tell him, then?',
                        "That's an honest place to begin."]}]}
CREATURES = tuple(range(919596, 919604))
QUESTS = (919891, 919892, 919893)
ACTORS = (919596, 919597, 919598, 919599)
SPAWN_COLUMNS = ('id', 'id2', 'id3', 'id4', 'map', 'position_x', 'position_y', 'position_z',
                 'orientation', 'spawntimesecsmin', 'spawntimesecsmax', 'wander_distance',
                 'health_percent', 'mana_percent', 'movement_type', 'spawn_flags', 'visibility_mod')
SPAWN_FLOATS = {'position_x', 'position_y', 'position_z', 'orientation', 'wander_distance',
                'health_percent', 'mana_percent', 'visibility_mod'}
# Independent 17-field snapshot read as DOUBLE from tw_world:3307 on 2026-10-09.
# Source GUIDs are provenance only; the migration must use native allocation.
SPAWNS = (
    ('919596', '0', '0', '0', '1', '9915.990234375', '2530.860107421875',
     '1316.9000244140625', '6.067140102386475', '120', '120', '0', '100', '100', '0', '0', '0'),
    ('919597', '0', '0', '0', '1', '9952.5400390625', '2244.469970703125',
     '1334.06005859375', '1.5707299709320068', '120', '120', '0', '100', '100', '0', '0', '0'),
    ('919598', '0', '0', '0', '1', '10186.2998046875', '2302.800048828125',
     '1328.3399658203125', '3.2554099559783936', '120', '120', '0', '100', '100', '0', '0', '0'),
    ('919599', '0', '0', '0', '1', '9676.3701171875', '2519.330078125',
     '1334.77001953125', '2.976599931716919', '120', '120', '0', '100', '100', '0', '0', '0'),
)


def quoted(value):
    return "'" + value.replace("'", "''") + "'"


def integers(values):
    return ','.join(str(value) for value in values)


def validate_targets(migration, allowed):
    # Database-qualified writes bypass temporary shadows. Only ordinary DML is
    # allowed, and every target is required to have a same-named temporary table.
    if re.search(r'/\*\s*(?:!|[mM]!)', migration):
        raise SystemExit('Executable SQL comments cannot be safely shadowed')
    code = re.sub(r"'(?:''|\\.|[^'\\])*'|--[^\r\n]*|/\*[\s\S]*?\*/", ' ', migration)
    writes = re.findall(r'(?:INSERT\s+(?:IGNORE\s+)?INTO|UPDATE|DELETE\s+FROM)\s+'
                        r'(`?\w+`?(?:\s*\.\s*`?\w+`?)?)', code, re.I)
    if any('.' in target for target in writes):
        raise SystemExit('Schema-qualified writes cannot be safely shadowed')
    touched = {target.strip('`') for target in writes}
    if not touched <= allowed:
        raise SystemExit('Unexpected or unshadowed migration tables: ' + repr(touched - allowed))
    if re.search(r'\b(?:CREATE|ALTER|DROP|TRUNCATE|REPLACE|RENAME|USE|CALL|LOAD|OUTFILE|DUMPFILE)\b', code, re.I):
        raise SystemExit('Only ordinary data writes may appear in this migration')


def validate_spawn_statements(migration):
    """Require the captured numeric rows and the exact owned-entry replacement."""
    code = re.sub(r"'(?:''|\\.|[^'\\])*'|--[^\r\n]*|/\*[\s\S]*?\*/", ' ', migration)
    statements = [statement.strip() for statement in code.split(';')]
    spawns = [statement for statement in statements if re.match(
        r'(?:INSERT\s+(?:IGNORE\s+)?INTO|UPDATE|DELETE\s+FROM)\s+`?creature`?(?![\w`])', statement, re.I)]
    if len(spawns) != 2:
        raise SystemExit('Expected exactly one owned creature DELETE and one captured INSERT')
    delete = re.fullmatch(r'DELETE\s+FROM\s+`?creature`?\s+WHERE\s+`?id`?\s+IN\s*'
                          r'\(\s*(\d+\s*(?:,\s*\d+\s*)*)\)', spawns[0], re.I)
    if not delete or tuple(sorted(map(int, delete.group(1).split(',')))) != ACTORS:
        raise SystemExit('Only the four Mitchell actor entries may be replaced')
    insert = re.fullmatch(r'INSERT\s+INTO\s+`?creature`?\s*\(([^)]+)\)\s+VALUES\s+(.+)',
                          spawns[1], re.I | re.S)
    if not insert:
        raise SystemExit('Only an unqualified numeric creature INSERT is allowed')
    columns = [column.strip() for column in insert.group(1).split(',')]
    if any(not re.fullmatch(r'`?[A-Za-z_]\w*`?', column) for column in columns):
        raise SystemExit('Invalid spawn column names')
    if tuple(column.strip('`').lower() for column in columns) != SPAWN_COLUMNS:
        raise SystemExit('Spawn INSERT must include all 17 captured fields and omit GUID')
    number = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
    row = r'\(\s*' + number + r'(?:\s*,\s*' + number + r'){16}\s*\)'
    if not re.fullmatch(row + r'(?:\s*,\s*' + row + r'){3}', insert.group(2)):
        raise SystemExit('Expected exactly four rows of 17 numeric spawn literals')
    captured = {}
    for values in re.findall(r'\(([^)]+)\)', insert.group(2)):
        values = tuple(Decimal(value.strip()) for value in values.split(','))
        entry = int(values[0])
        if values[0] != entry or entry in captured:
            raise SystemExit('Invalid or duplicate actor entry')
        captured[entry] = values
    for expected in SPAWNS:
        if captured.get(int(expected[0])) != tuple(Decimal(value) for value in expected):
            raise SystemExit('Captured full-precision spawn state differs for entry ' + expected[0])


def dbc_rows(path, entries):
    data = path.read_bytes()
    magic, count, fields, size, string_size = struct.unpack_from('<4sIIII', data)
    if magic != b'WDBC' or fields * 4 != size or len(data) != 20 + count * size + string_size:
        raise SystemExit('Invalid native DBC: ' + str(path))
    rows = {}
    for offset in range(20, 20 + count * size, size):
        entry = struct.unpack_from('<I', data, offset)[0]
        if entry in entries:
            rows[entry] = struct.unpack_from('<' + 'I' * fields, data, offset)
    if set(rows) != set(entries):
        raise SystemExit('Missing native DBC asset rows: ' + str(path))
    return rows, data[20 + count * size:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--dbc-dir', type=Path)
    parser.add_argument('--root', type=Path, help='Module root; defaults to this script\'s parent module')
    args = parser.parse_args()
    root = args.root or Path(__file__).resolve().parents[1]
    migration = (root/'data/sql/world/0041_hearthstone_alliance_mage_quests.sql').read_text(encoding='utf-8')
    configuration = args.config.read_text(encoding='utf-8-sig')
    setting = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"', configuration, re.M)
    data_dir = re.search(r'^\s*DataDir\s*=\s*"([^"]+)"', configuration, re.M)
    if not setting or (not data_dir and not args.dbc_dir):
        raise SystemExit('Database/DataDir settings missing')
    dbc_dir = args.dbc_dir or (args.config.parent/data_dir.group(1)/'dbc').resolve()
    displays, _ = dbc_rows(dbc_dir/'CreatureDisplayInfo.dbc', (1484,))
    dbc_rows(dbc_dir/'CreatureModelData.dbc', (displays[1484][1],))
    extra, _ = dbc_rows(dbc_dir/'CreatureDisplayInfoExtra.dbc', (displays[1484][3],))
    if extra[displays[1484][3]][1:3] != (1, 0):
        raise SystemExit('Mitchell must use the approved human male display')
    dbc_rows(dbc_dir/'Spell.dbc', (9617,))
    areas, strings = dbc_rows(dbc_dir/'AreaTable.dbc', (1657, 1659, 1660))
    for entry, name, parent in ((1657, 'Darnassus', 0), (1659, "Craftsmen's Terrace", 1657),
                                (1660, "Warrior's Terrace", 1657)):
        row = areas[entry]
        if row[1] != 1 or row[2] != parent or not any(
                strings[value:strings.find(b'\0', value)].decode('utf-8', errors='replace') == name
                for value in row if value < len(strings)):
            raise SystemExit('Wrong local city area: ' + name)
    source = (root/'src/HsAllianceMageQuest.cpp').read_text(encoding='utf-8')
    source += (root/'src/HsClassQuestGlue.cpp').read_text(encoding='utf-8')
    source += (root/'src/HsMatchGlue.cpp').read_text(encoding='utf-8')
    replies = [answer for question in EXPECTED['quiz'] for answer in question['answers']]
    replies += [question['acknowledgement'] for question in EXPECTED['quiz']]
    replies += [reply for tour in EXPECTED['tours'] for reply in tour['replies']]
    replies += ['Ask away.', "Go on. I'm listening.", "I'm ready. Let's play.",
                'What will you write about next?']
    for reply in replies:
        if json.dumps(reply, ensure_ascii=False) not in source:
            raise SystemExit('Missing approved player reply: ' + reply)

    host, port, user, password, database = setting.group(1).split(';')
    environment = os.environ.copy()
    environment['MYSQL_PWD'] = password
    command = [str(args.client), '--no-defaults', '--host='+host, '--port='+port, '--user='+user,
               '--database='+database, '--batch', '--skip-column-names', '--connect-timeout=5']

    def query(sql):
        result = subprocess.run(command, input=sql, text=True, capture_output=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stderr)
        return result.stdout

    texts = [9195600 + int(offset) for offset in EXPECTED['texts']]
    menus = [int(menu) for menu in EXPECTED['menus']]
    owned = {
        'creature_template': 'entry IN ('+integers(CREATURES)+')',
        'quest_template': 'entry IN ('+integers(QUESTS)+')',
        'broadcast_text': 'entry IN ('+integers(texts)+')',
        'npc_text': 'ID IN ('+integers(texts)+')',
        'gossip_menu': 'entry IN ('+integers(menus)+')',
        'creature_questrelation': 'quest IN ('+integers(QUESTS)+')',
        'creature_involvedrelation': 'quest IN ('+integers(QUESTS)+')',
        'hearthstone_opponent': 'creature_entry=919596',
        'hearthstone_npc_unlock': 'creature_entry=919596',
        'item_template': 'FALSE', 'npc_vendor': 'FALSE', 'npc_vendor_template': 'FALSE',
        'creature': 'id IN ('+integers(ACTORS)+')', 'gameobject': 'FALSE',
    }
    allowed = set(owned) - {'item_template', 'npc_vendor', 'npc_vendor_template', 'gameobject'}
    validate_targets(migration, allowed)
    validate_spawn_statements(migration)
    prerequisite_sql = (
        'SELECT COUNT(*) FROM creature_template WHERE entry=919502;'
        'SELECT COUNT(*) FROM creature_template WHERE entry=331 AND display_id1=1484;'
        'SELECT COUNT(*) FROM item_template WHERE entry=919626 AND max_count=1;'
        'SELECT COUNT(*) FROM quest_template WHERE entry=919714;'
        "SELECT COUNT(*) FROM hearthstone_deck_template WHERE deck_key='normal_mage';")
    if query(prerequisite_sql).strip().splitlines() != ['1', '1', '1', '1', '1']:
        raise SystemExit('Missing Alliance baseline, approved Mage appearance, deck reward/introduction or Basic Mage')
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+table+'`' for table in owned)+';'
    before = query(checksum_sql)
    if len(before.splitlines()) != len(owned) or any(row.endswith('\tNULL') for row in before.splitlines()):
        raise SystemExit('Incomplete live checksums')
    names = ','.join(quoted(table) for table in owned)
    fields = {table: [] for table in owned}
    keys = {table: [] for table in owned}
    string_fields = {table: set() for table in owned}
    for row in query('SELECT TABLE_NAME,COLUMN_NAME,DATA_TYPE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() '
                     'AND TABLE_NAME IN ('+names+') ORDER BY TABLE_NAME,ORDINAL_POSITION;').splitlines():
        table, column, kind = row.split('\t')
        fields[table].append(column)
        if kind in ('char', 'varchar', 'text', 'tinytext', 'mediumtext', 'longtext', 'enum', 'set'):
            string_fields[table].add(column)
    for row in query("SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() "
                     "AND INDEX_NAME='PRIMARY' AND TABLE_NAME IN ("+names+') ORDER BY TABLE_NAME,SEQ_IN_INDEX;').splitlines():
        table, column = row.split('\t')
        keys[table].append(column)
    if any(not fields[table] or not keys[table] for table in owned):
        raise SystemExit('Incomplete schema/primary keys')
    if tuple(fields['creature']) != ('guid',)+SPAWN_COLUMNS or keys['creature'] != ['guid']:
        raise SystemExit('Native creature schema differs from captured spawn fields')
    setup = []
    for table, predicate in owned.items():
        seed = 'hs_alliance_mage_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
                  f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE NOT ({predicate});']
    # Bookkeeping actors must neither be created by the migration nor removed if
    # they were mistakenly placed already. These sentinels exist only in temp data.
    setup += ['INSERT INTO hs_alliance_mage_seed_creature (id,map,position_x,position_y,position_z,orientation) '
              'VALUES (919600,0,11,12,13,1),(919601,0,21,22,23,2),'
              '(919602,0,31,32,33,3),(919603,0,41,42,43,4);']
    for table in owned:
        setup += [f'CREATE TEMPORARY TABLE `{table}` LIKE `hs_alliance_mage_seed_{table}`;',
                  f'INSERT INTO `{table}` SELECT * FROM `hs_alliance_mage_seed_{table}`;']
    # These wrong bindings are created in temporary tables only; the migration
    # must replace them rather than leave an image offering/receiving a quest.
    setup += ['INSERT INTO creature_questrelation (id,quest) VALUES (919597,919891);',
              'INSERT INTO creature_involvedrelation (id,quest) VALUES (919599,919893);',
              # Duplicate wrong actor placements force the first run to exercise
              # scoped replacement, rather than merely insertion into empty data.
              'INSERT INTO creature (id,map,position_x,position_y,position_z,orientation) VALUES '
              '(919596,0,1,2,3,4),(919596,0,2,3,4,5),'
              '(919597,0,1,2,3,4),(919597,0,2,3,4,5),'
              '(919598,0,1,2,3,4),(919598,0,2,3,4,5),'
              '(919599,0,1,2,3,4),(919599,0,2,3,4,5);']
    checks = []

    def field_equal(table, field, left, right):
        if field in string_fields[table]:
            return f'BINARY {left} <=> BINARY {right}'
        return f'{left} <=> {right}'

    def full_record(table, entry, key, expected, baseline=None):
        predicates = [field_equal(table, field, f't.`{field}`', expected.get(field,"DEFAULT(t.`"+field+"`)"))
                      for field in fields[table]]
        join = f' JOIN hs_alliance_mage_seed_creature_template b ON b.entry={baseline}' if baseline else ''
        checks.append((f'full {table} {entry}', f'SELECT COUNT(*)=1 FROM `{table}` t'+join+
                       f' WHERE t.`{key}`={entry} AND '+' AND '.join(predicates)))

    copied = ('level_min', 'level_max', 'health_min', 'health_max', 'mana_min', 'mana_max', 'armor',
              'speed_walk', 'speed_run', 'detection_range', 'call_for_help_range', 'base_attack_time',
              'ranged_attack_time', 'unit_class', 'type', 'regeneration')
    for entry, name, subname, flags, menu, aura in EXPECTED['actors']:
        expected = {field: 'b.`'+field+'`' for field in copied}
        expected.update({'entry': str(entry), 'display_id1': '1484', 'name': quoted(name),
                         'subname': quoted(subname), 'npc_flags': str(flags), 'gossip_menu_id': str(menu),
                         'faction': '79', 'unit_flags': 'b.unit_flags' if flags else '33554432',
                         'movement_type': '0', 'phase_quest_id': '0', 'auras': quoted(aura), 'script_name': "''"})
        expected.update({field: 'b.`'+field+'`' if flags else '0'
                         for field in ('dmg_min', 'dmg_max', 'attack_power')})
        full_record('creature_template', entry, 'entry', expected, 919502)
    for index, quest in enumerate(EXPECTED['quests']):
        final = index == 2
        expected = {field: quoted(quest[field]) for field in
                    ('Title', 'Details', 'Objectives', 'RequestItemsText', 'OfferRewardText')}
        expected.update({'entry': str(quest['entry']), 'Method': '2', 'ZoneOrSort': '-9100', 'MinLevel': '10',
                         'QuestLevel': '0', 'RequiredRaces': '589', 'RequiredClasses': '0', 'QuestFlags': '0',
                         'SpecialFlags': '2' if final else '0',
                         'PrevQuestId': str(919714 if index == 0 else quest['entry']-1),
                         'NextQuestInChain': str(0 if final else quest['entry']+1), 'RewXP': '500',
                         'RewOrReqMoney': '0', 'RewMoneyMaxLevel': '0',
                         'RewItemId1': '919626' if final else '0', 'RewItemCount1': '1' if final else '0'})
        for slot in range(1, 5):
            credit = quest['credits'][slot-1] if slot <= len(quest['credits']) else 0
            count = quest['counts'][slot-1] if slot <= len(quest['counts']) else 0
            expected.update({f'ReqCreatureOrGOId{slot}': str(credit), f'ReqCreatureOrGOCount{slot}': str(count),
                             f'ReqItemId{slot}': '0', f'ReqItemCount{slot}': '0',
                             f'ObjectiveText{slot}': quoted(quest['labels'][slot-1] if slot <= len(quest['labels']) else '')})
        full_record('quest_template', quest['entry'], 'entry', expected)
        for table in ('creature_questrelation', 'creature_involvedrelation'):
            checks.append((f'{table} {quest["entry"]}', f'SELECT COUNT(*)=1 AND MIN(id)=919596 '
                           f'FROM {table} WHERE quest={quest["entry"]}'))
    for offset, story in EXPECTED['texts'].items():
        text = 9195600 + int(offset)
        full_record('broadcast_text', text, 'entry', {'entry': str(text), 'male_text': quoted(story), 'female_text': quoted(story)})
        full_record('npc_text', text, 'ID', {'ID': str(text), 'BroadcastTextID0': str(text), 'Probability0': '1'})
    for menu, text in EXPECTED['menus'].items():
        full_record('gossip_menu', int(menu), 'entry', {'entry': str(menu), 'text_id': str(text), 'script_id': '0', 'condition_id': '0'})
    full_record('hearthstone_npc_unlock', 919596, 'creature_entry', {'creature_entry': '919596',
                'quest_id': '919893', 'deck_key': quoted('normal_mage'), 'faction': quoted('alliance')})
    for expected in SPAWNS:
        comparisons = []
        for field, value in zip(SPAWN_COLUMNS, expected):
            if field in SPAWN_FLOATS:
                comparisons.append(f'CAST(`{field}` AS DOUBLE) <=> CAST({value} AS DOUBLE)')
            else:
                comparisons.append(f'`{field}` <=> {value}')
        checks.append(('captured full spawn '+expected[0], 'SELECT COUNT(*)=1 FROM creature WHERE '+
                       ' AND '.join(comparisons)))
    checks += [
        ('no direct opponent bindings', 'SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry IN ('+integers(CREATURES)+')'),
        ('only teacher roster', 'SELECT COUNT(*)=0 FROM hearthstone_npc_unlock WHERE creature_entry BETWEEN 919597 AND 919603'),
        ('no new bookkeeping spawns', 'SELECT (SELECT COUNT(*) FROM creature WHERE id BETWEEN 919600 AND 919603)='
         '(SELECT COUNT(*) FROM hs_alliance_mage_seed_creature WHERE id BETWEEN 919600 AND 919603)'),
        ('native actor GUID allocation', 'SELECT COUNT(*)=4 FROM creature WHERE id IN ('+integers(ACTORS)+') '
         'AND guid>@hs_alliance_mage_before_guid'),
        ('interactive actors selectable', 'SELECT COUNT(*)=4 FROM creature_template WHERE entry BETWEEN 919596 AND 919599 AND (unit_flags & 33554432)=0'),
        ('images gossip-only and ghost visual', "SELECT COUNT(*)=3 FROM creature_template WHERE entry BETWEEN 919597 AND 919599 AND npc_flags=1 AND auras='9617'"),
    ]
    additions = {'creature_template': 8, 'quest_template': 3, 'broadcast_text': 60, 'npc_text': 60,
                 'gossip_menu': 21, 'creature_questrelation': 3, 'creature_involvedrelation': 3,
                 'hearthstone_npc_unlock': 1, 'creature': 4}
    snapshots = []
    rerun = []
    for table in owned:
        seed = 'hs_alliance_mage_seed_'+table
        first = 'hs_alliance_mage_first_'+table
        key = ' AND '.join(f't.`{field}` <=> b.`{field}`' for field in keys[table])
        equal = ' AND '.join(field_equal(table, field, f't.`{field}`', f'b.`{field}`') for field in fields[table])
        primary = keys[table][0]
        checks += [(f'preserve {table}', f'SELECT COUNT(*)=0 FROM `{seed}` b LEFT JOIN `{table}` t ON {key} '
                    f'WHERE t.`{primary}` IS NULL OR NOT ({equal})'),
                   (f'count {table}', f'SELECT (SELECT COUNT(*) FROM `{table}`)='
                    f'(SELECT COUNT(*) FROM `{seed}`)+{additions.get(table,0)}')]
        snapshots += [f'CREATE TEMPORARY TABLE `{first}` LIKE `{table}`;', f'INSERT INTO `{first}` SELECT * FROM `{table}`;']
        # The native allocator may give replaced actors different GUIDs on run two.
        # All other records, including unrelated creature GUIDs, must remain exact.
        unowned = 'b.id NOT IN ('+integers(ACTORS)+') AND ' if table == 'creature' else ''
        rerun += [(f'rerun {table}', f'SELECT COUNT(*)=0 FROM `{first}` b LEFT JOIN `{table}` t ON {key} '
                   f'WHERE {unowned}(t.`{primary}` IS NULL OR NOT ({equal}))'),
                  (f'rerun count {table}', f'SELECT (SELECT COUNT(*) FROM `{table}`)=(SELECT COUNT(*) FROM `{first}`)')]
    assertions = '\n'.join(f'SELECT {quoted(label)},({sql});' for label, sql in checks)
    allocate = 'SET @hs_alliance_mage_before_guid=(SELECT MAX(guid) FROM creature);\n'
    output = query('\n'.join(setup)+'\n'+allocate+migration+'\n'+assertions+'\n'+'\n'.join(snapshots)+'\n'+
                   allocate+migration+'\n'+assertions+'\n'+'\n'.join(f'SELECT {quoted(label)},({sql});' for label, sql in rerun))
    after = query(checksum_sql)
    if after != before:
        raise SystemExit('Database checksums changed during temporary validation')
    rows = output.strip().splitlines()
    failures = [row for row in rows if not row.endswith('\t1')]
    if len(rows) != 2*len(checks)+len(rerun) or failures:
        raise SystemExit('Temporary assertions failed: '+repr(failures or rows))
    registry = json.loads((root/'data/quests/class_unlocks.json').read_text(encoding='utf-8'))['npcs']
    matching = [npc for npc in registry if npc['entry'] == 919596]
    expected = {'quest': 919893, 'quest_title': 'A Question of Timing', 'deck_key': 'normal_mage', 'faction': 'alliance',
                'victory_lines': ['Well played. That answer was yours.'],
                'duel_complete': 'Duel complete. Return to Mitchell Jones for your Mage Deck. Right-click it to unlock Mage for your account.'}
    if len(matching) != 1 or any(matching[0].get(key) != value for key, value in expected.items()):
        raise SystemExit('Alliance Mage teacher registry policy mismatch')
    if any(npc['entry'] in CREATURES[1:] for npc in registry):
        raise SystemExit('Mage images/credits must never enter the opponent registry')
    print(f'Alliance Mage: {len(rows)} assertions passed over two temporary runs / {len(owned)} full schemas. '
          'Duplicate actors replaced with exact 17-field placements and native GUIDs. '
          'All unrelated rows/GUIDs, bookkeeping spawns, exact text/default fields and database checksums preserved. '
          'Native human male model/areas/visual and approved player replies verified.')


if __name__ == '__main__':
    main()
