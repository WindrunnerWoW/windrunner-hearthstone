#!/usr/bin/env python3
"""One command to rebuild every Blizzard-derived texture that is not in git.

  python tools/art/setup_art.py            # whole chain, skips nothing that is missing
  python tools/art/setup_art.py --check    # only report which outputs are missing, change nothing
  python tools/art/setup_art.py --from patch   # resume at a step (venv fetch patch cards minions bgminions bgheroes bgquests powers heroes skins)

What is committed: the scripts, data/art/*.json, build/assets (the owner's sheets), art/source and the BLPs cut
from the owner's sheets (art/out/Board, UI, Collection). What this script produces (all gitignored, see README
"Licence and redistribution"): art/cache (downloaded renders), art/patched, art/out/Cards, art/out/Minions,
art/out/BgMinions, art/out/BgHeroes, art/out/BgQuests, art/out/Powers, art/out/Heroes and
art/out/Collection/hero_*.blp.

Downloads are resumable and rate limited (fetch_renders.py); the other steps are deterministic, so after a full run
`git status data/art` must be clean - a changed manifest means a hash or a source differs from the committed state.
Run from anywhere; paths resolve against the module root. Standard library only (Pillow goes into the venv).
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ART_TOOLS = os.path.join(ROOT, "tools", "art")
VENV = os.path.join(ART_TOOLS, ".venv")
VPY = os.path.join(VENV, "Scripts" if os.name == "nt" else "bin", "python.exe" if os.name == "nt" else "python")

STEPS = ["venv", "fetch", "patch", "cards", "minions", "bgminions", "bgheroes", "bgquests", "powers", "heroes", "skins"]

# step -> (what it needs to be considered done, path relative to the module root)
OUTPUTS = {
    "fetch": ("art/cache/negative.json", None),
    "patch": ("art/patched/EX1_131.png", None),
    "cards": ("art/out/Cards", 490),
    "minions": ("art/out/Minions", 268),
    "bgminions": ("art/out/BgMinions", 179),
    "bgheroes": ("art/out/BgHeroes", "data/art/bg_heroes.manifest.json"),
    "bgquests": ("art/out/BgQuests", "data/art/bg_quests.manifest.json"),
    "powers": ("art/out/Powers", 12),
    "heroes": ("art/out/Collection/hero_mage.blp", None),
    "skins": ("art/out/Heroes", 71),
}


def run(*args):
    print("\n$", " ".join(os.path.relpath(a, ROOT) if os.path.isabs(a) else a for a in args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def count_blp(rel):
    p = os.path.join(ROOT, rel)
    return len([f for f in os.listdir(p) if f.endswith(".blp")]) if os.path.isdir(p) else 0


def missing_from_manifest(out_rel, manifest_rel):
    """True when a hero or hero power of the manifest has no BLP (<id>.blp, powers/<id>.blp). A count is not enough:
    the old output (bob + h01..h24) has more files than the current one but none of its names."""
    with open(os.path.join(ROOT, manifest_rel), encoding="utf-8") as f:
        manifest = json.load(f)
    out = os.path.join(ROOT, out_rel)
    want = [os.path.join(out, k + ".blp") for k in manifest.get("entries", {})]
    want += [os.path.join(out, "powers", k + ".blp") for k in manifest.get("powers", {})]
    return any(not os.path.exists(p) for p in want)


def missing(step):
    if step == "venv":
        return not os.path.exists(VPY)
    path, need = OUTPUTS[step]
    if need is None:
        return not os.path.exists(os.path.join(ROOT, path))
    if isinstance(need, str):
        return missing_from_manifest(path, need)
    return count_blp(path) < need


def do_step(step):
    py = [VPY]
    if step == "venv":
        if not os.path.exists(VPY):
            run(sys.executable, "-m", "venv", VENV)
        run(VPY, "-m", "pip", "install", "--quiet", "Pillow")
    elif step == "fetch":
        run(sys.executable, "tools/art/fetch_renders.py")
        run(sys.executable, "tools/art/fetch_renders.py", "--hires")
    elif step == "patch":
        run(*py, "tools/art/patch_renders.py")
    elif step == "cards":
        run(*py, "tools/art/convert_blp.py", "--all", "--verify")
    elif step == "minions":
        run(*py, "tools/art/minion_portraits.py")
    elif step == "bgminions":
        run(*py, "tools/art/bg_minions.py")
    elif step == "bgheroes":
        run(*py, "tools/art/bg_heroes.py")
    elif step == "bgquests":
        run(*py, "tools/art/bg_quests.py")
    elif step == "powers":
        run(*py, "tools/art/power_portraits.py", "--fetch")
    elif step == "heroes":
        run(*py, "tools/art/slice_collection.py", "--only", "hero_strips")
    elif step == "skins":
        run(*py, "tools/art/hero_skins.py", "--fetch")


def manifest_state():
    r = subprocess.run(["git", "status", "--porcelain", "--", "data/art"], cwd=ROOT, capture_output=True, text=True)
    return r.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="report missing outputs, run nothing")
    ap.add_argument("--from", dest="start", choices=STEPS, default=STEPS[0], help="first step to run")
    ap.add_argument("--force", action="store_true", help="rerun steps whose output already exists")
    args = ap.parse_args()

    todo = [s for s in STEPS[STEPS.index(args.start):] if args.force or missing(s)]
    if args.check:
        for s in STEPS:
            print("%-8s %s" % (s, "MISSING" if missing(s) else "ok"))
        return 1 if any(missing(s) for s in STEPS) else 0
    if not todo:
        print("nothing to do: every Blizzard-derived output is present (use --force to rebuild)")
        return 0
    print("steps:", ", ".join(todo))
    before = manifest_state()
    for s in todo:
        do_step(s)
    after = manifest_state()
    if after != before:
        print("\nWARNING: data/art manifests differ from the committed state:\n" + after)
        print("Expected after a clean rebuild; check `git diff data/art` before committing them.")
    else:
        print("\nDone. data/art manifests unchanged: output matches the committed hashes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
