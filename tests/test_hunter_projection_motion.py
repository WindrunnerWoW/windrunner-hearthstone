#!/usr/bin/env python3
"""Exercise the real Hunter setter and native movement-stack code without a live server.

The harness imports the current core's declarations/method bodies verbatim. Only
world, AI, and movement-factory dependencies use adapters; the stack, idle
generator, initialization, and update assertions are native. The pre-fix setter
is a negative control. No database, map insertion, or server process is involved.
Run through build_tests.ps1 -Only hunter_projection_motion for the MSVC environment.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def block(source, signature):
    """Return one declaration/body, skipping braces inside comments and literals."""
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for match in re.finditer(tokens, source[opening:]):
        token = match.group()
        if token == '{': depth += 1
        elif token == '}':
            depth -= 1
            if not depth: return source[start:opening+match.end()]
    raise ValueError('Unclosed native body: '+signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    module = Path(__file__).resolve().parents[1]
    parser.add_argument('--core', type=Path, default=module.parent.parent)
    parser.add_argument('--out', type=Path, default=module/'build/quest-check-tests/hunter-motion')
    args = parser.parse_args()
    movement = args.core/'src/game/Movement'
    read = lambda path: path.read_text(encoding='utf-8')
    header = re.sub(r'^\s*#include[^\n]*\n', '', read(movement/'MotionMaster.h'), flags=re.M)
    generator = block(read(movement/'MovementGenerator.h'), 'class MovementGenerator\n')+';'
    idle = block(read(movement/'IdleMovementGenerator.h'), 'class IdleMovementGenerator :')+';'
    motion = read(movement/'MotionMaster.cpp')
    methods = [block(motion, signature) for signature in (
        'inline bool isStatic(', 'void MotionMaster::Initialize()', 'MotionMaster::~MotionMaster()',
        'void MotionMaster::UpdateMotion(', 'void MotionMaster::UpdateMotionAsync(',
        'void MotionMaster::DirectClean(', 'void MotionMaster::DelayedClean(',
        'void MotionMaster::DirectExpire(', 'void MotionMaster::DelayedExpire(')]
    destructor = block(read(movement/'MovementGenerator.cpp'), 'MovementGenerator::~MovementGenerator()')
    idle_reset = block(read(movement/'IdleMovementGenerator.cpp'), 'void\nIdleMovementGenerator::Reset(')
    callback = block(read(module/'src/HsHunterQuest.cpp'), 'void CosmeticActor(')
    old = callback.replace('void CosmeticActor(', 'void UninitializedActor(')
    old = old.replace('actor->GetMotionMaster()->Initialize();', '')
    chains = json.loads(read(module/'tests/fixtures/hunter_questline.json'))
    entries = ','.join(str(d['projection']+i) for d in chains for i in range(9))
    prelude = r'''
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <vector>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
namespace Movement { struct Location {}; using PointsArray = std::vector<int>; }
#define MANGOS_ASSERT(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)
'''
    adapters = r'''
struct CharmInfo {
    void SetIsAtStay(bool) {}
    void SetIsReturning(bool) {}
    void SetIsFollowing(bool) {}
};
constexpr uint32 UNIT_STAT_POSSESSED=1, UNIT_STAT_CAN_NOT_MOVE=2, PATH_NO_PATH=0;
constexpr uint32 UNIT_NPC_FLAGS=0, UNIT_FIELD_FLAGS=1;
constexpr uint32 UNIT_FLAG_SPAWNING=1, UNIT_FLAG_NOT_SELECTABLE=0x02000000;
constexpr uint32 REACT_PASSIVE=0;
class Unit {
public:
    MotionMaster motion{this};
    bool IsStopped() const { return true; }
    void StopMoving() {}
    bool IsCreature() const { return true; }
    bool IsAlive() const { return true; }
    bool HasUnitState(uint32) const { return false; }
    CharmInfo* GetCharmInfo() { return nullptr; }
};
class Creature;
class CreatureAI { public: explicit CreatureAI(Creature*) {} };
class Creature : public Unit {
public:
    uint32 entry=0, reaction=1, npcFlags=3, flags=0;
    CreatureAI* ai=nullptr;
    ~Creature() { delete ai; }
    MotionMaster* GetMotionMaster() { return &motion; }
    void SetAI(CreatureAI* value) { ai=value; }
    void SetReactState(uint32 value) { reaction=value; }
    void SetUInt32Value(uint32 field, uint32 value) {
        if (field!=UNIT_NPC_FLAGS) throw std::runtime_error("unexpected field");
        npcFlags=value;
    }
    void SetFlag(uint32 field, uint32 value) {
        if (field!=UNIT_FIELD_FLAGS) throw std::runtime_error("unexpected flag field");
        flags |= value;
    }
};
// Projection templates select idle; unrelated factory registrations/world data are not loaded.
namespace FactorySelector { MovementGenerator* selectMovementGenerator(Creature*) { return nullptr; } }
template<class T> class WaypointMovementGenerator : public IdleMovementGenerator {
public: void InitializeWaypointPath(T&, int, uint32, int, int, int, bool) {}
};
IdleMovementGenerator si_idleMovement;
'''
    tests = r'''
void Check(bool condition, char const* reason) {
    if (!condition) throw std::runtime_error(reason);
}
int main() {
    try {
        Creature broken;
        UninitializedActor(&broken);
        bool rejected=false;
        try { broken.motion.UpdateMotion(50); }
        catch (std::runtime_error const& e) { rejected=std::string(e.what())=="!empty()"; }
        Check(rejected,"pre-fix control did not reproduce the reported native assertion");
        uint32 const entries[] = {ENTRIES};
        for (uint32 entry : entries) {
            Creature actor; actor.entry=entry;
            Check(actor.motion.empty(),"a new summon should start with empty motion");
            CosmeticActor(&actor);
            Check(!actor.motion.empty(),"setter left movement uninitialized");
            Check(actor.motion.top()->GetMovementGeneratorType()==IDLE_MOTION_TYPE,"projection moves");
            Check(actor.ai && actor.reaction==REACT_PASSIVE && !actor.npcFlags,"cosmetic AI/services changed");
            Check((actor.flags & (UNIT_FLAG_SPAWNING|UNIT_FLAG_NOT_SELECTABLE))==
                (UNIT_FLAG_SPAWNING|UNIT_FLAG_NOT_SELECTABLE),"projection remains selectable");
            for (uint32 tick=0; tick<64; ++tick) {
                actor.motion.UpdateMotion(50);
                actor.motion.UpdateMotionAsync(50);
                Check(!actor.motion.empty(),"motion stack lost after update");
                Check(actor.motion.top()->GetMovementGeneratorType()==IDLE_MOTION_TYPE,"projection ceased to idle");
            }
        }
        std::cout << "Hunter projection motion: 18 entries survived 64 native update/async ticks each; "
            "pre-fix control reproduced !empty().\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''.replace('ENTRIES', entries)
    args.out.mkdir(parents=True, exist_ok=True)
    cpp = args.out/'hunter_projection_motion.cpp'
    exe = args.out/'hunter_projection_motion.exe'
    cpp.write_text('\n'.join([prelude, header, generator, idle, adapters, destructor, idle_reset,
        *methods, callback, old, tests]), encoding='utf-8')
    result = subprocess.run(['cl.exe','/nologo','/EHsc','/std:c++17','/O2','/W4','/WX','/permissive-',
        '/Fo'+str(args.out.resolve())+'\\','/Fe'+str(exe.resolve()),str(cpp.resolve())],
        text=True, capture_output=True)
    if result.returncode: raise SystemExit(result.stdout+result.stderr)
    raise SystemExit(subprocess.run([str(exe.resolve())]).returncode)


if __name__ == '__main__': main()
