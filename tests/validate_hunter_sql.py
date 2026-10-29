#!/usr/bin/env python3
"""Validate both Hunter chains twice in full-schema temporary tables; never write live data."""
import argparse
from decimal import Decimal
import json
import os
from pathlib import Path
import re
import subprocess
from validate_alliance_priest_sql import dbc_rows, integers, quoted, validate_targets


ACTORS = (919582,919583,919584,919585)
SPAWN_COLUMNS = ('id','id2','id3','id4','map','position_x','position_y','position_z','orientation',
    'spawntimesecsmin','spawntimesecsmax','wander_distance','health_percent','mana_percent',
    'movement_type','spawn_flags','visibility_mod')
SPAWN_FLOATS = {'position_x','position_y','position_z','orientation','wander_distance',
    'health_percent','mana_percent','visibility_mod'}
SPAWN_RELATED = ('creature_addon','creature_movement','creature_movement_special','creature_movement_template',
    'creature_groups','creature_linking','creature_linking_template','game_event_creature',
    'game_event_creature_data','pool_creature','pool_creature_template')


def validate_spawn_export(migration, snapshot):
    """Check exact numeric spawn SQL against an independent read-only database snapshot."""
    marker = '-- Owner-recorded Hunter placements, 2026-10-09.'
    if migration.count(marker)!=1 or tuple(snapshot['columns'])!=SPAWN_COLUMNS:
        raise SystemExit('Missing or invalid Hunter placement snapshot')
    before,block = migration.split(marker)
    if 'creature' in validate_targets(before,{'creature_template','quest_template','broadcast_text','npc_text',
        'gossip_menu','creature_questrelation','creature_involvedrelation','hearthstone_opponent','hearthstone_npc_unlock'}):
        raise SystemExit('Spawn writes must be confined to the export block')
    code = re.sub(r'--[^\r\n]*',' ',block)
    number = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
    row = r'\(\s*'+number+r'(?:\s*,\s*'+number+r'){16}\s*\)'
    match = re.fullmatch(r'\s*DELETE\s+FROM\s+`creature`\s+WHERE\s+`id`\s+IN\s*'
        r'\(([^)]+)\)\s*;\s*INSERT\s+INTO\s+`creature`\s*\(([^)]+)\)\s+VALUES\s*('
        +row+r'(?:\s*,\s*'+row+r'){3})\s*;\s*',code,re.I)
    if not match or tuple(map(int,match.group(1).split(',')))!=ACTORS or tuple(
        c.strip().strip('`') for c in match.group(2).split(','))!=SPAWN_COLUMNS:
        raise SystemExit('Only the four actor entries and exact non-GUID numeric spawn fields may be exported')
    expected = {int(s['fields']['id']):s['fields'] for s in snapshot['spawns']}
    if tuple(expected)!=ACTORS or len(snapshot['spawns'])!=4:
        raise SystemExit('Placement snapshot must contain exactly the four permanent actors')
    captured = {}
    for values in re.findall(r'\(([^)]+)\)',match.group(3)):
        values = tuple(Decimal(v.strip()) for v in values.split(',')); entry = int(values[0])
        if entry in captured: raise SystemExit('Duplicate exported actor')
        captured[entry] = values
    for entry,fields in expected.items():
        if captured.get(entry)!=tuple(Decimal(fields[c]) for c in SPAWN_COLUMNS):
            raise SystemExit('Exported placement differs from captured native data: '+str(entry))
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--dbc-dir', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    chains = json.loads((root/'tests/fixtures/hunter_questline.json').read_text(encoding='utf-8'))
    migration = (root/'data/sql/world/0040_hearthstone_hunter_quests.sql').read_text(encoding='utf-8')
    spawns = validate_spawn_export(migration,json.loads((root/'tests/fixtures/hunter_spawns.json').read_text(encoding='utf-8')))
    configuration = args.config.read_text(encoding='utf-8-sig')
    setting = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',configuration,re.M)
    data_dir = re.search(r'^\s*DataDir\s*=\s*"([^"]+)"',configuration,re.M)
    if not setting or (not data_dir and not args.dbc_dir): raise SystemExit('Database/DataDir settings missing')
    dbc = args.dbc_dir or (args.config.parent/data_dir.group(1)/'dbc').resolve()
    models = {d['display'] for d in chains} | {d['helperdisplay'] for d in chains} | {m for d in chains for _,m in d['animals']}
    displays,_ = dbc_rows(dbc/'CreatureDisplayInfo.dbc',models)
    dbc_rows(dbc/'CreatureModelData.dbc',{row[1] for row in displays.values()})
    extras,_ = dbc_rows(dbc/'CreatureDisplayInfoExtra.dbc',{displays[m][3] for m in (4511,9269,7041,2306)})
    for model,race,sex in ((4511,6,0),(9269,6,1),(7041,7,0),(2306,4,1)):
        if extras[displays[model][3]][1:3] != (race,sex): raise SystemExit('Wrong NPC race/sex model')
    dbc_rows(dbc/'Spell.dbc',(9617,))
    areas,strings = dbc_rows(dbc/'AreaTable.dbc',(1638,1641,1657,1658))
    for entry,name,parent in ((1638,'Thunder Bluff',0),(1641,'Hunter Rise',1638),(1657,'Darnassus',0),(1658,'Cenarion Enclave',1657)):
        row = areas[entry]
        if row[1]!=1 or row[2]!=parent or not any(strings[v:strings.find(b'\0',v)].decode('utf-8',errors='replace')==name
            for v in row if v<len(strings)): raise SystemExit('Wrong local city area: '+name)
    source = (root/'src/HsHunterQuest.cpp').read_text(encoding='utf-8')
    for chain in chains:
        for reply in chain['replies']:
            if json.dumps(reply,ensure_ascii=False) not in source: raise SystemExit('Missing approved player reply: '+reply)
    host,port,user,password,database = setting.group(1).split(';')
    environment = os.environ.copy(); environment['MYSQL_PWD']=password
    command = [str(args.client),'--host='+host,'--port='+port,'--user='+user,'--database='+database,
        '--batch','--skip-column-names','--connect-timeout=5']
    def query(sql):
        result = subprocess.run(command,input=sql,text=True,capture_output=True,env=environment)
        if result.returncode: raise SystemExit(result.stderr)
        return result.stdout

    creatures = [x for d in chains for x in (d['teacher'],d['helper'],*range(d['credit'],d['credit']+5),*range(d['projection'],d['projection']+9))]
    quests = [d['base']+i for d in chains for i in range(3)]
    texts = [d['text']+int(i) for d in chains for i in d['texts']]
    menus = [d['menu']+int(i) for d in chains for i in d['texts']]
    owned = {
        'creature_template':'entry IN ('+integers(creatures)+')',
        'quest_template':'entry IN ('+integers(quests)+')',
        'broadcast_text':'entry IN ('+integers(texts)+')',
        'npc_text':'ID IN ('+integers(texts)+')',
        'gossip_menu':'entry IN ('+integers(menus)+')',
        'creature_questrelation':'quest IN ('+integers(quests)+')',
        'creature_involvedrelation':'quest IN ('+integers(quests)+')',
        'hearthstone_opponent':'creature_entry IN (919582,919584)',
        'hearthstone_npc_unlock':'creature_entry IN (919582,919584)',
        'item_template':'FALSE','npc_vendor':'FALSE','npc_vendor_template':'FALSE',
        'creature':'id IN ('+integers(ACTORS)+')','gameobject':'FALSE',
    }
    allowed = set(owned)-{'item_template','npc_vendor','npc_vendor_template','gameobject'}
    validate_targets(migration,allowed)
    prerequisites = query('SELECT COUNT(*) FROM creature_template WHERE entry IN (919501,919502);'
        'SELECT COUNT(*) FROM item_template WHERE entry=919622 AND max_count=1;'
        'SELECT COUNT(*) FROM quest_template WHERE entry IN (919704,919714);'
        "SELECT COUNT(*) FROM hearthstone_deck_template WHERE deck_key='normal_hunter';")
    if prerequisites.strip().splitlines()!=['2','1','2','1']: raise SystemExit('Missing baselines, deck reward/intros or normal_hunter')
    related = query('SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('+
        ','.join(quoted(t) for t in SPAWN_RELATED)+') ORDER BY TABLE_NAME;').splitlines()
    checksum_sql = 'CHECKSUM TABLE '+','.join('`'+t+'`' for t in (*owned,*related))+';'
    before = query(checksum_sql)
    if len(before.splitlines())!=len(owned)+len(related) or any(row.endswith('\tNULL') for row in before.splitlines()):
        raise SystemExit('Incomplete live checksums')
    names = ','.join(quoted(t) for t in owned)
    fields = {t:[] for t in owned}; keys = {t:[] for t in owned}
    for row in query('SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() '
        'AND TABLE_NAME IN ('+names+') ORDER BY TABLE_NAME,ORDINAL_POSITION;').splitlines():
        table,column=row.split('\t'); fields[table].append(column)
    for row in query("SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND INDEX_NAME='PRIMARY' "
        'AND TABLE_NAME IN ('+names+') ORDER BY TABLE_NAME,SEQ_IN_INDEX;').splitlines():
        table,column=row.split('\t'); keys[table].append(column)
    if any(not fields[t] or not keys[t] for t in owned): raise SystemExit('Incomplete schema/primary keys')
    if set(fields['creature'])!=set(SPAWN_COLUMNS)|{'guid'}:
        raise SystemExit('Native creature schema differs from captured spawn fields')
    setup=[]
    for table,predicate in owned.items():
        seed='hs_hunter_seed_'+table
        setup += [f'CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;',
            f'INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE NOT ({predicate});']
    for table in owned:
        setup += [f'CREATE TEMPORARY TABLE `{table}` LIKE `hs_hunter_seed_{table}`;',
            f'INSERT INTO `{table}` SELECT * FROM `hs_hunter_seed_{table}`;']
    # Incorrect old ownership/service bindings exist only in temporary tables.
    setup += ['INSERT INTO creature_questrelation (id,quest) VALUES (919583,919871);',
        'INSERT INTO creature_involvedrelation (id,quest) VALUES (919585,919882);']
    # Owned duplicates must be replaced, while all unrelated full spawn rows remain unchanged.
    setup += ['INSERT INTO creature (id,map,position_x,position_y,position_z,orientation) VALUES '+
        ','.join(f'({entry},0,1,2,3,4)' for entry in ACTORS for _ in range(2))+';']
    checks=[]
    def full_record(table,entry,key,expected,baseline=None,omit=()):
        predicates = [f't.`{f}` <=> {expected.get(f,"DEFAULT(t.`"+f+"`)")}' for f in fields[table] if f not in omit]
        join = f' JOIN hs_hunter_seed_creature_template b ON b.entry={baseline}' if baseline else ''
        checks.append((f'full {table} {entry}',f'SELECT COUNT(*)=1 FROM `{table}` t'+join+
            f' WHERE t.`{key}`={entry} AND '+' AND '.join(predicates)))
    for entry,values in spawns.items():
        full_record('creature',entry,'id',{c:f'CAST({values[c]} AS FLOAT)' if c in SPAWN_FLOATS else values[c]
            for c in SPAWN_COLUMNS},omit=('guid',))
    checks += [('new destination spawn GUIDs',f'SELECT COUNT(*)=4 FROM creature WHERE id IN ({integers(ACTORS)}) '
        'AND guid>@hs_hunter_before_spawn_guid'),
        ('no additional credit or temporary spawns','SELECT COUNT(*)=(SELECT COUNT(*) FROM hs_hunter_seed_creature '
        'WHERE id IN ('+integers([x for x in creatures if x not in ACTORS])+')) FROM creature WHERE id IN ('+
        integers([x for x in creatures if x not in ACTORS])+')')]
    copied = ('level_min','level_max','health_min','health_max','mana_min','mana_max','armor','speed_walk','speed_run',
        'detection_range','call_for_help_range','base_attack_time','ranged_attack_time','unit_class','type','regeneration')
    for d in chains:
        baseline=919501 if d['city']==104 else 919502
        actors=[(d['teacher'],d['display'],d['name'],'Hearthstone teacher',3,d['menu'],False,d.get('scale',1)),
            (d['helper'],d['helperdisplay'],d['friend'],"Boro's niece" if d['city']==104 else 'Sentinel',
                1,d['menu']+1,False,d.get('helperscale',1))]
        actors += [(d['credit']+i,d['display'],'HS Hunter '+d['name']+' step '+str(i+1),'',0,0,False,1) for i in range(5)]
        actors += [(d['projection']+i,model,name,'',0,0,True,0.75 if i<8 else d.get('accountantscale',1))
            for i,(name,model) in enumerate(d['animals'])]
        for entry,model,name,sub,flags,menu,projection,scale in actors:
            expected={f:'b.`'+f+'`' for f in copied}
            expected.update({'entry':str(entry),'display_id1':str(model),'name':quoted(name),'subname':quoted(sub),
                'npc_flags':str(flags),'gossip_menu_id':str(menu),'faction':str(d['city']),
                'scale':f'CAST({scale} AS FLOAT)',
                'unit_flags':'b.unit_flags' if flags else '33554432','movement_type':'0','phase_quest_id':'0',
                'auras':quoted('9617' if projection else ''),'script_name':"''"})
            expected.update({f:'b.`'+f+'`' if flags else '0' for f in ('dmg_min','dmg_max','attack_power')})
            full_record('creature_template',entry,'entry',expected,baseline)
        for i,quest in enumerate(d['quests']):
            expected={f:quoted(quest[f]) for f in ('Title','Details','Objectives','RequestItemsText','OfferRewardText')}
            expected.update({'entry':str(quest['entry']),'Method':'2','ZoneOrSort':'-9100','MinLevel':'10','QuestLevel':'0',
                'RequiredRaces':str(d['mask']),'RequiredClasses':'0','QuestFlags':'0','SpecialFlags':'2' if i==2 else '0',
                'PrevQuestId':str(d['intro'] if i==0 else quest['entry']-1),'NextQuestInChain':str(quest['entry']+1 if i<2 else 0),
                'RewXP':'500','RewOrReqMoney':'0','RewMoneyMaxLevel':'0','RewItemId1':'919622' if i==2 else '0',
                'RewItemCount1':'1' if i==2 else '0'})
            for slot in range(1,5):
                credit=quest['credits'][slot-1] if slot<=len(quest['credits']) else 0
                expected.update({f'ReqCreatureOrGOId{slot}':str(credit),f'ReqCreatureOrGOCount{slot}':str(int(bool(credit))),
                    f'ReqItemId{slot}':'0',f'ReqItemCount{slot}':'0',
                    f'ObjectiveText{slot}':quoted(quest['labels'][slot-1] if slot<=len(quest['labels']) else '')})
            full_record('quest_template',quest['entry'],'entry',expected)
            for table in ('creature_questrelation','creature_involvedrelation'):
                checks.append((f'{table} {quest["entry"]}',f'SELECT COUNT(*)=1 AND MIN(id)={d["teacher"]} FROM {table} WHERE quest={quest["entry"]}'))
        for offset,story in d['texts'].items():
            offset=int(offset); text=d['text']+offset; menu=d['menu']+offset
            full_record('broadcast_text',text,'entry',{'entry':str(text),'male_text':quoted(story),'female_text':quoted(story)})
            full_record('npc_text',text,'ID',{'ID':str(text),'BroadcastTextID0':str(text),'Probability0':'1'})
            full_record('gossip_menu',menu,'entry',{'entry':str(menu),'text_id':str(text),'script_id':'0','condition_id':'0'})
        full_record('hearthstone_npc_unlock',d['teacher'],'creature_entry',{'creature_entry':str(d['teacher']),
            'quest_id':str(d['base']+2),'deck_key':quoted('normal_hunter'),'faction':quoted('horde' if d['city']==104 else 'alliance')})
    checks += [('no direct bindings','SELECT COUNT(*)=0 FROM hearthstone_opponent WHERE creature_entry IN ('+integers(creatures)+')'),
        ('no helper/credit/temporary actor roster','SELECT COUNT(*)=0 FROM hearthstone_npc_unlock WHERE creature_entry IN ('+
            integers([x for x in creatures if x not in (919582,919584)])+')')]
    additions={'creature_template':32,'quest_template':6,'broadcast_text':len(texts),'npc_text':len(texts),'gossip_menu':len(menus),
        'creature_questrelation':6,'creature_involvedrelation':6,'hearthstone_npc_unlock':2,'creature':4}
    snapshots=[]; rerun=[]
    for table in owned:
        seed='hs_hunter_seed_'+table; first='hs_hunter_first_'+table
        key=' AND '.join(f't.`{f}` <=> b.`{f}`' for f in keys[table])
        equal=' AND '.join(f't.`{f}` <=> b.`{f}`' for f in fields[table])
        pk=keys[table][0]
        checks += [(f'preserve {table}',f'SELECT COUNT(*)=0 FROM `{seed}` b LEFT JOIN `{table}` t ON {key} WHERE t.`{pk}` IS NULL OR NOT ({equal})'),
            (f'count {table}',f'SELECT (SELECT COUNT(*) FROM `{table}`)=(SELECT COUNT(*) FROM `{seed}`)+{additions.get(table,0)}')]
        snapshots += [f'CREATE TEMPORARY TABLE `{first}` LIKE `{table}`;',f'INSERT INTO `{first}` SELECT * FROM `{table}`;']
        # Owned spawn GUIDs deliberately change on each application. Exact non-GUID
        # actor records are checked after both runs; unrelated GUIDs/rows must still match.
        unchanged = f'b.id NOT IN ({integers(ACTORS)}) AND ' if table=='creature' else ''
        rerun += [(f'rerun {table}',f'SELECT COUNT(*)=0 FROM `{first}` b LEFT JOIN `{table}` t ON {key} '
            f'WHERE {unchanged}(t.`{pk}` IS NULL OR NOT ({equal}))'),
            (f'rerun count {table}',f'SELECT (SELECT COUNT(*) FROM `{table}`)=(SELECT COUNT(*) FROM `{first}`)')]
    assertions='\n'.join(f'SELECT {quoted(label)},({sql});' for label,sql in checks)
    allocate='SET @hs_hunter_before_spawn_guid=IFNULL((SELECT MAX(guid) FROM creature),0);\n'
    output=query('\n'.join(setup)+'\n'+allocate+migration+'\n'+assertions+'\n'+'\n'.join(snapshots)+'\n'+allocate+migration+'\n'+assertions+'\n'+
        '\n'.join(f'SELECT {quoted(label)},({sql});' for label,sql in rerun))
    after=query(checksum_sql)
    if after!=before: raise SystemExit('Live checksums changed during temporary validation')
    results=output.strip().splitlines(); failures=[x for x in results if not x.endswith('\t1')]
    if len(results)!=2*len(checks)+len(rerun) or failures: raise SystemExit('Temporary assertions failed: '+repr(failures or results))
    registry=json.loads((root/'data/quests/class_unlocks.json').read_text())['npcs']
    for d in chains:
        actual=[r for r in registry if r['entry']==d['teacher']]
        if len(actual)!=1 or any(actual[0][k]!=v for k,v in {'quest':d['base']+2,'quest_title':d['quests'][2]['Title'],
            'deck_key':'normal_hunter','faction':'horde' if d['city']==104 else 'alliance'}.items()): raise SystemExit('Hunter registry mismatch')
    if any(r['entry'] in creatures and r['entry'] not in (919582,919584) for r in registry): raise SystemExit('Unexpected supporting actor registry')
    print(f'Hunter: {len(results)} assertions passed over two temporary runs / {len(owned)} full schemas. '
        f'All unrelated rows, exact text/default fields, four captured spawns and rerun data preserved; '
        f'all {len(owned)+len(related)} live checksums unchanged. Native models/races/areas verified.')


if __name__=='__main__': main()
