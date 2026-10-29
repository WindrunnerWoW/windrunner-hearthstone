#!/usr/bin/env python3
"""Exercise actual shared duel-credit glue without a server/database.

Imports current Battle/Notice declarations and five handler bodies verbatim.
Only core world/session access uses adapters. All registered teachers are covered.
This verifies shared abandonment/credit behavior, not a live-map integration.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def block(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for match in re.finditer(tokens, source[opening:]):
        token = match.group()
        if token == '{':
            depth += 1
        elif token == '}':
            depth -= 1
            if not depth:
                return source[start:opening + match.end()]
    raise ValueError('Unclosed current source body: ' + signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    module = args.module.resolve()
    source = (module / 'src/HsClassQuestGlue.cpp').read_text(encoding='utf-8')
    rows = json.loads((module / 'data/quests/class_unlocks.json').read_text(encoding='utf-8'))['npcs']
    if not any(row['entry'] == 919596 for row in rows):
        raise ValueError('Mitchell Jones must be registered/generated before this verification')
    declarations = '\n'.join(block(source, s) + ';' for s in ('struct Battle\n', 'struct Notice\n'))
    methods = '\n'.join(block(source, signature) for signature in (
        'void HsClassQuestMatchStarted(', 'void HsClassQuestMatchEnded(', 'void HsClassQuestUpdate(',
        'void HsClassQuestInvalidateQuest(', 'void HsClassQuestAfterQuestChange('))
    include = (module / 'src/quests/HsClassQuestRules.h').as_posix()
    prelude = r'''
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "RULES_INCLUDE"
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
constexpr uint32 ALLIANCE=0, HORDE=1, HIGHGUID_PLAYER=1, LANG_UNIVERSAL=0;
enum QuestStatus { QUEST_STATUS_NONE, QUEST_STATUS_INCOMPLETE, QUEST_STATUS_COMPLETE, QUEST_STATUS_REWARDED };
struct ObjectGuid {
    uint64 value;
    explicit ObjectGuid(uint64 v) : value(v) {}
    ObjectGuid(uint32, uint64 v) : value(v) {}
    uint64 GetRawValue() const { return value; }
};
struct WorldSession { uint32 account=1; uint32 GetAccountId() const { return account; } };
struct Player;
struct Creature {
    uint32 entry=0; uint64 guid=123; bool alive=true;
    uint32 GetEntry() const { return entry; }
    ObjectGuid GetObjectGuid() const { return ObjectGuid(guid); }
    bool IsAlive() const { return alive; }
    void MonsterYell(char const*,uint32,Player*) {}
    void MonsterSayToPlayer(char const*,Player*) {}
};
struct Map { Creature* actor=nullptr; Creature* GetCreature(ObjectGuid) const { return actor; } };
struct Player {
    uint32 guid=1, team=ALLIANCE, mapId=1, credits=0, lastQuest=0;
    bool world=true, online=true;
    WorldSession session;
    Map map;
    std::map<uint32,QuestStatus> quests;
    WorldSession* GetSession() { return online ? &session : nullptr; }
    uint32 GetGUIDLow() const { return guid; }
    uint32 GetTeam() const { return team; }
    uint32 GetMapId() const { return mapId; }
    Map* GetMap() { return &map; }
    bool IsInWorld() const { return world; }
    QuestStatus GetQuestStatus(uint32 quest) const {
        auto it=quests.find(quest); return it==quests.end() ? QUEST_STATUS_NONE : it->second;
    }
    void AreaExploredOrEventHappens(uint32 quest) { ++credits; lastQuest=quest; quests[quest]=QUEST_STATUS_COMPLETE; }
};
namespace ObjectAccessor {
    std::map<uint64,Player*> players;
    Player* FindPlayer(ObjectGuid guid) {
        auto it=players.find(guid.value); return it==players.end() ? nullptr : it->second;
    }
}
struct ChatHandler {
    explicit ChatHandler(WorldSession*) {}
    void PSendSysMessage(char const*, char const*) {}
};
'''.replace('RULES_INCLUDE', include)
    globals_ = r'''
std::mutex g_mutex;
std::unordered_map<uint32,Battle> g_battles;
std::vector<Notice> g_notices;
'''
    tests = r'''
static uint32 checks=0;
void Check(bool ok, char const* reason) { ++checks; if(!ok) throw std::runtime_error(reason); }
struct Fixture {
    Player player;
    Creature teacher;
    uint32 quest=0;
    Fixture(uint32 entry,uint32 q,bool alliance) : quest(q) {
        g_battles.clear(); g_notices.clear(); ObjectAccessor::players.clear();
        teacher.entry=entry; player.team=alliance ? ALLIANCE : HORDE;
        player.quests[q]=QUEST_STATUS_INCOMPLETE; player.map.actor=&teacher;
        ObjectAccessor::players[player.guid]=&player;
    }
    void Start() { HsClassQuestMatchStarted(&player,&teacher,41); }
    Hs::MatchEnd End(char const* result="win",char const* reason="hero") const {
        Hs::MatchEnd end; end.guid=player.guid; end.matchId=41;
        end.meta.accountId=player.session.account; end.meta.opponentEntry=teacher.entry;
        end.res=result; end.why=reason; return end;
    }
    void Finish() { HsClassQuestMatchEnded(End()); HsClassQuestUpdate(); }
    void Abandon() { player.quests[quest]=QUEST_STATUS_NONE; HsClassQuestAfterQuestChange(&player); }
    void Reaccept() { player.quests[quest]=QUEST_STATUS_INCOMPLETE; }
};
void VerifyTeacher(uint32 entry,uint32 quest,bool alliance) {
    {
        Fixture f(entry,quest,alliance); f.Start(); f.Finish();
        Check(f.player.credits==1 && f.player.lastQuest==quest,"valid win failed to complete its own active final quest");
        f.Finish(); Check(f.player.credits==1,"replayed end duplicated credit");
    }
    for (char const* result : {"loss","draw","int","surrender","interrupted"}) {
        Fixture f(entry,quest,alliance); f.Start(); HsClassQuestMatchEnded(f.End(result)); HsClassQuestUpdate();
        Check(f.player.credits==0,"invalid match result counted");
    }
    for (char const* reason : {"concede","disconnect","logout","timeout","death","turns","gm","error"}) {
        Fixture f(entry,quest,alliance); f.Start(); HsClassQuestMatchEnded(f.End("win",reason)); HsClassQuestUpdate();
        Check(f.player.credits==0,"non-hero win counted");
    }
    for (QuestStatus status : {QUEST_STATUS_NONE,QUEST_STATUS_COMPLETE,QUEST_STATUS_REWARDED}) {
        Fixture f(entry,quest,alliance); f.player.quests[quest]=status; f.Start(); f.Reaccept(); f.Finish();
        Check(f.player.credits==0,"a duel started without an incomplete quest counted after later acceptance");
    }
    for (QuestStatus status : {QUEST_STATUS_NONE,QUEST_STATUS_COMPLETE,QUEST_STATUS_REWARDED}) {
        Fixture f(entry,quest,alliance); f.Start(); HsClassQuestMatchEnded(f.End());
        f.player.quests[quest]=status; HsClassQuestUpdate();
        Check(f.player.credits==0,"credit applied without an incomplete quest at delivery");
    }
    for (uint32 mismatch=0;mismatch<3;++mismatch) {
        Fixture f(entry,quest,alliance); f.Start(); auto end=f.End();
        if(!mismatch) ++end.matchId;
        if(mismatch==1) ++end.meta.accountId;
        if(mismatch==2) end.meta.opponentEntry=919597;
        HsClassQuestMatchEnded(end); HsClassQuestUpdate();
        Check(f.player.credits==0,"mismatched match/account/opponent counted");
    }
    {
        Fixture f(entry,quest,alliance); f.Start(); f.Abandon();
        Check(!g_battles.at(f.player.guid).questOpen,"running abandonment did not invalidate the tracked acceptance");
        f.Reaccept(); f.Finish(); Check(f.player.credits==0,"earlier running win credited a new acceptance");
        f.Start(); f.Finish(); Check(f.player.credits==1,"fresh duel after reacceptance was incorrectly blocked");
    }
    {
        Fixture f(entry,quest,alliance); f.Start(); HsClassQuestMatchEnded(f.End());
        Check(g_notices.size()==1,"valid match did not queue credit");
        f.Abandon(); Check(!g_notices.front().battle.questOpen,"queued abandonment did not invalidate pending credit");
        f.Reaccept(); HsClassQuestUpdate(); Check(f.player.credits==0,"earlier queued win credited a new acceptance");
        f.Start(); f.Finish(); Check(f.player.credits==1,"new match after queued abandonment was blocked");
    }
    {
        Fixture f(entry,quest,alliance); f.Start();
        HsClassQuestInvalidateQuest(f.player.guid,quest+1000);
        HsClassQuestInvalidateQuest(f.player.guid+1,quest);
        Check(g_battles.at(f.player.guid).questOpen,"unrelated quest/character invalidated this duel");
        f.Finish(); Check(f.player.credits==1,"unrelated invalidation swallowed valid credit");
    }
    for (uint32 gate=0;gate<4;++gate) {
        Fixture f(entry,quest,alliance); f.Start(); HsClassQuestMatchEnded(f.End());
        if(!gate) f.player.world=false;
        if(gate==1) f.player.online=false;
        if(gate==2) ++f.player.session.account;
        if(gate==3) f.player.team=alliance ? HORDE : ALLIANCE;
        HsClassQuestUpdate(); Check(f.player.credits==0,"invalid delivery identity/world/session/faction counted");
    }
    {
        Fixture f(entry,quest,alliance); f.teacher.entry=919597; f.Start();
        Check(g_battles.empty(),"an image was recorded as a class teacher");
        f.Finish(); Check(f.player.credits==0,"unregistered image match counted");
    }
}
int main() {
    try {
        TEACHER_CALLS
        HsClassQuestAfterQuestChange(nullptr);
        std::cout << "Shared actual duel-credit glue: " << checks << " checks across TEACHER_COUNT registered teachers.\n";
        return 0;
    } catch(std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''.replace('TEACHER_CALLS', '\n        '.join(
        f"VerifyTeacher({r['entry']},{r['quest']},{str(r['faction']=='alliance').lower()});" for r in rows)
    ).replace('TEACHER_COUNT', str(len(rows)))
    args.out.mkdir(parents=True, exist_ok=True)
    cpp = args.out / 'class_quest_credit.cpp'
    cpp.write_text(prelude + declarations + globals_ + methods + tests, encoding='utf-8')
    exe = args.out / 'class_quest_credit.exe'
    command = ['cl.exe','/nologo','/EHsc','/std:c++17','/O2','/W4','/WX','/permissive-',
               '/Fo'+str(args.out.resolve())+'\\','/Fe'+str(exe.resolve()),str(cpp.resolve())]
    subprocess.run(command, check=True)
    subprocess.run([str(exe.resolve())], check=True)


if __name__ == '__main__':
    main()
