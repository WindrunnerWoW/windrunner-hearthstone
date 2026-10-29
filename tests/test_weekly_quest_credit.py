"""Compile the actual weekly glue with headless core/database adapters, without a server."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    game = root.parents[1] / 'src/game'
    source = ''
    # The server's rotation (header, then source) goes in ahead of the glue that registers with it.
    for path in (game / 'WeeklyQuestRotation.h', game / 'WeeklyQuestRotation.cpp', root / 'src/HsWeeklyQuestGlue.cpp'):
        source += re.sub(r'^#include[^\n]*\n', '', path.read_text(encoding='utf-8'), flags=re.M)
    harness = (root / 'tests/weekly_credit_harness.cpp').read_text(encoding='utf-8')
    harness = harness.replace('// INSERT_CURRENT_GLUE', source)
    harness = harness.replace('WEEKLY_HEADER', (root / 'src/quests/HsWeeklyQuests.h').as_posix())
    harness = harness.replace('ROTATION_DRAW_HEADER', (game / 'WeeklyRotationDraw.h').as_posix())
    harness = harness.replace('GLUE_HEADER', (root / 'src/HsWeeklyQuestGlue.h').as_posix())
    args.out.mkdir(parents=True, exist_ok=True)
    cpp = args.out / 'weekly_credit.cpp'
    cpp.write_text(harness, encoding='utf-8')
    exe = args.out / 'weekly_credit.exe'
    subprocess.run(['cl.exe', '/nologo', '/EHsc', '/std:c++17', '/O2', '/W4', '/WX', '/permissive-', '/wd4996',
                    '/I'+str(game), '/Fo'+str(args.out.resolve())+'\\', '/Fe'+str(exe.resolve()), str(cpp.resolve()),
                    str(root / 'src/quests/HsWeeklyQuests.cpp'), str(root / 'src/quests/HsBgWeeklyQuests.cpp')],
                   check=True)
    subprocess.run([str(exe.resolve())], check=True)


if __name__ == '__main__':
    main()
