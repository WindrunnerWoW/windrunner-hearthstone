#!/usr/bin/env python3
"""Run migration 0020 twice against session-local copies of the configured world schemas.

Only TEMPORARY tables are written; the connection's world tables are never modified.
Usage: python tests/validate_horde_druid_sql.py --config <test mangosd.conf> --client <mariadb.exe>
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    args = parser.parse_args()
    match = re.search(r'^\s*WorldDatabase\.Info\s*=\s*"([^"]+)"',
                      args.config.read_text(encoding="utf-8-sig"), re.M)
    if not match:
        raise SystemExit("WorldDatabase.Info is missing from the test configuration")
    host, port, user, password, database = match.group(1).split(";")
    migration = (Path(__file__).resolve().parents[1] / "data/sql/world/0020_hearthstone_horde_druid_quests.sql").read_text()
    # A temporary table shadows its permanent counterpart even with a database qualifier.
    # Read each needed baseline into a separate temporary seed BEFORE creating the shadow.
    tables = {
        "creature_template": "entry IN (3017, 919501)",
        "npc_vendor": "entry = 3017",
        "gossip_menu": None, "npc_text": None, "broadcast_text": None,
        "item_template": None, "gameobject_template": None, "quest_template": None,
        "creature_questrelation": None, "creature_involvedrelation": None,
        "hearthstone_opponent": None, "hearthstone_npc_unlock": None,
        "creature": None, "gameobject": None,
    }
    setup = []
    for table, seed_filter in tables.items():
        seed = "hs_druid_seed_" + table
        setup.append(f"CREATE TEMPORARY TABLE `{seed}` LIKE `{table}`;")
        if seed_filter:
            setup += [f"INSERT INTO `{seed}` SELECT * FROM `{table}` WHERE {seed_filter};",
                      f"CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;",
                      f"INSERT INTO `{table}` SELECT * FROM `{seed}`;"]
        else:
            setup += [f"CREATE TEMPORARY TABLE `{table}` LIKE `{seed}`;",
                      f"DROP TEMPORARY TABLE `{seed}`;"]
    checks = [
        ("teacher baseline", "SELECT COUNT(*) = 1 FROM creature_template t JOIN creature_template b ON b.entry=919501 "
         "WHERE t.entry=919514 AND t.name='Kolento Treewatcher' AND t.display_id1=11773 AND t.faction=104 "
         "AND t.npc_flags=3 AND t.level_min=b.level_min AND t.health_max=b.health_max "
         "AND t.unit_flags=b.unit_flags AND t.script_name='' AND t.gossip_menu_id=65010"),
        ("unspawned conversation credits", "SELECT COUNT(*)=2 FROM creature_template WHERE entry IN (919515,919516) AND npc_flags=0"),
        ("quest restrictions", "SELECT COUNT(*)=3 FROM quest_template WHERE entry BETWEEN 919751 AND 919753 "
         "AND MinLevel=10 AND RequiredRaces=434 AND RewXP=500 AND RewOrReqMoney=0 AND RewMoneyMaxLevel=0"),
        ("feed objective and prerequisite", "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919751 "
         "AND PrevQuestId=919704 AND NextQuestInChain=919752 AND ReqCreatureOrGOId1=919515 "
         "AND ReqCreatureOrGOCount1=1 AND ReqCreatureOrGOId2=919516 AND ReqCreatureOrGOCount2=1 "
         "AND ObjectiveText2='Report the missing fruit to Kolento' AND ReqItemId1=0"),
        ("four distinct native items", "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919752 "
         "AND PrevQuestId=919751 AND NextQuestInChain=919753 "
         "AND ReqItemId1=919632 AND ReqItemId2=919633 AND ReqItemId3=919634 AND ReqItemId4=919635 "
         "AND ReqItemCount1=1 AND ReqItemCount2=1 AND ReqItemCount3=1 AND ReqItemCount4=1"),
        ("duel reward", "SELECT COUNT(*)=1 FROM quest_template WHERE entry=919753 AND PrevQuestId=919752 "
         "AND SpecialFlags=2 AND RewItemId1=919628 AND RewItemCount1=1"),
        ("quest relations", "SELECT (SELECT COUNT(*) FROM creature_questrelation WHERE id=919514)=3 "
         "AND (SELECT COUNT(*) FROM creature_involvedrelation WHERE id=919514)=3"),
        ("unique quest cards", "SELECT COUNT(*)=4 FROM item_template WHERE entry BETWEEN 919632 AND 919635 "
         "AND class=12 AND max_count=1 AND stackable=1 AND bonding=4 AND sell_price=0"),
        ("pickup templates", "SELECT COUNT(*)=4 FROM gameobject_template WHERE entry BETWEEN 919051 AND 919054 "
         "AND type=3 AND displayId=220 AND ABS(size-0.35)<0.0001 AND data0=0 AND data1=0 AND data3=0 AND data8=919752 AND script_name=''"),
        ("gossip text bindings", "SELECT COUNT(*)=6 FROM npc_text n JOIN broadcast_text b ON b.entry=n.BroadcastTextID0 "
         "WHERE n.ID BETWEEN 9195160 AND 9195165 AND n.Probability0=1"),
        ("greeting menu", "SELECT COUNT(*)=1 FROM gossip_menu WHERE entry=65010 AND text_id=9195160"),
        ("bad-news exchange", "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195164 "
         "AND male_text LIKE 'That''s the way of life.%'"),
        ("revised wind dialogue", "SELECT COUNT(*)=1 FROM broadcast_text WHERE entry=9195165 "
         "AND male_text LIKE 'The wind has had its turn.%' AND male_text NOT LIKE '%wished you%'"),
        ("Nan gossip enabled with native functions retained", "SELECT "
         "(SELECT COUNT(*) FROM creature_template n JOIN hs_druid_seed_creature_template b ON b.entry=n.entry "
         "WHERE n.entry=3017 AND n.npc_flags=(b.npc_flags | 1) AND n.script_name=b.script_name "
         "AND n.gossip_menu_id=b.gossip_menu_id)=1 "
         "AND (SELECT COUNT(*) FROM npc_vendor)=(SELECT COUNT(*) FROM hs_druid_seed_npc_vendor)"),
        ("one recorded teacher and no credit/pickup spawns", "SELECT (SELECT COUNT(*) FROM creature)=1 "
         "AND (SELECT COUNT(*) FROM creature WHERE id=919514 AND map=1 "
         "AND ABS(position_x-(-1010.159973145))<0.000001 "
         "AND ABS(position_y-(-244.287994385))<0.000001 AND ABS(position_z-160.039001465)<0.000001 "
         "AND ABS(orientation-0.470948994)<0.0000001 "
         "AND spawntimesecsmin=120 AND spawntimesecsmax=120 AND wander_distance=0 AND movement_type=0)=1 "
         "AND (SELECT COUNT(*) FROM gameobject)=0"),
        ("Basic Druid roster only", "SELECT (SELECT COUNT(*) FROM hearthstone_opponent WHERE creature_entry=919514)=0 "
         "AND (SELECT COUNT(*) FROM hearthstone_npc_unlock WHERE creature_entry=919514 AND quest_id=919753 "
         "AND deck_key='normal_druid' AND faction='horde')=1"),
    ]
    assertions = "\n".join(f"SELECT '{label}', ({query});" for label, query in checks)
    sql = "\n".join(setup) + "\n" + (migration + "\n" + assertions + "\n") * 2
    environment = os.environ.copy()
    environment["MYSQL_PWD"] = password
    result = subprocess.run([str(args.client), "--host=" + host, "--port=" + port, "--user=" + user,
                             "--database=" + database, "--batch", "--skip-column-names"],
                            input=sql, text=True, capture_output=True, env=environment)
    if result.returncode:
        raise SystemExit(result.stderr)
    rows = result.stdout.strip().splitlines()
    failures = [row for row in rows if not row.endswith("\t1")]
    if len(rows) != len(checks) * 2 or failures:
        raise SystemExit("Temporary-table validation failed: " + repr(failures or rows))
    print(f"Migration 0020 passed {len(rows)} assertions across two runs against {len(tables)} temporary schema copies. Live tables unchanged.")


if __name__ == "__main__":
    main()
