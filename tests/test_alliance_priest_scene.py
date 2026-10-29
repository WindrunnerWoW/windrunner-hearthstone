#!/usr/bin/env python3
"""Exercise the Priest scene's real summon setter with native movement-stack code.

The setter and core movement methods are extracted from this checkout. World/AI
adapters avoid a server process; a setter without Initialize is a negative control.
Run in the VS x64 developer environment: python tests/test_alliance_priest_scene.py.
"""
import argparse
from pathlib import Path
import re
import subprocess

from test_hunter_projection_motion import block


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    module = Path(__file__).resolve().parents[1]
    parser.add_argument('--core', type=Path, default=module.parent.parent)
    parser.add_argument('--out', type=Path, default=module/'build/quest-check-tests/priest-scene')
    args = parser.parse_args()
    read = lambda path: path.read_text(encoding='utf-8')
    movement = args.core/'src/game/Movement'
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
    callback = block(read(module/'src/HsAlliancePriestQuest.cpp'), 'void ClaspSceneActor(')
    initialization = 'actor->GetMotionMaster()->Initialize();'
    if callback.count(initialization) != 1:
        raise SystemExit('Scene setter must initialize motion exactly once before map insertion.')
    negative = callback.replace('void ClaspSceneActor(', 'void UninitializedSceneActor(')
    negative = negative.replace(initialization, '')
    prelude = r'''
#include <cstdint>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
namespace Movement { struct Location {}; using PointsArray = std::vector<int>; }
#define MANGOS_ASSERT(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)
constexpr uint32 kHsSyrelle=919579, kHsAlenya=919578;
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
class CreatureAI { public: explicit CreatureAI(Creature*) {} virtual ~CreatureAI() = default; };
class Creature : public Unit {
public:
    uint32 entry=0, reaction=1, npcFlags=3, flags=0;
    bool temporary=false;
    CreatureAI* ai=nullptr;
    ~Creature() { delete ai; }
    uint32 GetEntry() const { return entry; }
    bool IsTemporarySummon() const { return temporary; }
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
class ClaspSceneAI : public CreatureAI {
public:
    bool initializedBeforeAI;
    explicit ClaspSceneAI(Creature* actor) : CreatureAI(actor),
        initializedBeforeAI(!actor->GetMotionMaster()->empty()) {}
};
// A scene summon selects idle before its native MovePoint is issued after insertion.
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
        Creature broken; broken.entry=kHsSyrelle; broken.temporary=true;
        UninitializedSceneActor(&broken);
        bool rejected=false;
        try { broken.motion.UpdateMotion(50); }
        catch (std::runtime_error const& e) { rejected=std::string(e.what())=="!empty()"; }
        Check(rejected,"negative control did not reproduce native !empty() assertion");

        Creature actor; actor.entry=kHsSyrelle; actor.temporary=true;
        Check(actor.motion.empty(),"a fresh summon should have empty motion");
        ClaspSceneActor(&actor);
        Check(!actor.motion.empty(),"scene setter left movement uninitialized");
        auto* sceneAI=dynamic_cast<ClaspSceneAI*>(actor.ai);
        Check(sceneAI && sceneAI->initializedBeforeAI,"motion must initialize before scene AI");
        Check(actor.reaction==REACT_PASSIVE && !actor.npcFlags,"scene retained interaction services");
        Check((actor.flags & (UNIT_FLAG_SPAWNING|UNIT_FLAG_NOT_SELECTABLE))==
            (UNIT_FLAG_SPAWNING|UNIT_FLAG_NOT_SELECTABLE),"scene actor remains selectable");
        for (uint32 tick=0; tick<64; ++tick) {
            actor.motion.UpdateMotion(50);
            actor.motion.UpdateMotionAsync(50);
            Check(!actor.motion.empty() && actor.motion.top()->GetMovementGeneratorType()==IDLE_MOTION_TYPE,
                "scene summon lost its native idle motion");
        }
        for (uint32 entry : {kHsSyrelle,kHsAlenya,uint32(919582),uint32(0)})
            for (bool temporary : {false,true}) {
                if (entry==kHsSyrelle && temporary) continue;
                Creature other; other.entry=entry; other.temporary=temporary;
                other.motion.Initialize(); other.ai=new CreatureAI(&other);
                CreatureAI* originalAI=other.ai;
                MovementGenerator* originalMotion=other.motion.top();
                ClaspSceneActor(&other);
                Check(other.ai==originalAI && other.motion.top()==originalMotion &&
                    other.npcFlags==3 && other.reaction==1 && other.flags==0,
                    "scene setter mutated a real actor or unrelated summon");
            }
        std::cout << "Priest scene: native idle update/async ticks survived; unrelated actors preserved; "
            "negative control reproduced !empty().\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''
    args.out.mkdir(parents=True, exist_ok=True)
    cpp = args.out/'alliance_priest_scene.cpp'
    exe = args.out/'alliance_priest_scene.exe'
    cpp.write_text('\n'.join([prelude, header, generator, idle, adapters, destructor, idle_reset,
        *methods, callback, negative, tests]), encoding='utf-8')
    result = subprocess.run(['cl.exe','/nologo','/EHsc','/std:c++17','/O2','/W4','/WX','/permissive-',
        '/Fo'+str(args.out.resolve())+'\\','/Fe'+str(exe.resolve()),str(cpp.resolve())],
        text=True, capture_output=True)
    if result.returncode: raise SystemExit(result.stdout+result.stderr)
    raise SystemExit(subprocess.run([str(exe.resolve())]).returncode)


if __name__ == '__main__': main()
