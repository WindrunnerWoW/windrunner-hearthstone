#!/usr/bin/env python3
"""Generates addon/Hearthstone/MatchFxProfiles.lua from data/fx/effect_profiles.json.

The manifest lists the native WoW spell models (paths into the player's installed client data, nothing is
redistributed), per-family defaults and per-card overrides used by the match animation effects
(see addon/Hearthstone/MatchFx.lua). Run after every edit of the manifest:

    python tools/fx/gen_effect_profiles.py            write the Lua file
    python tools/fx/gen_effect_profiles.py --check    exit 1 when the Lua file is not what the manifest generates

The generated file carries the Adler-32 checksum of the manifest (line endings normalised), so the Lua tests
notice a manifest that was edited without regenerating.

Manifest shape (see data/fx/effect_profiles.json):
    presets   [ { key, model, kind (missile|impact|aura), scale (relative: 1 = the size MatchFx.lua derives from the
                  anchor), pos [x,y,z] (a small offset in model units on top of the centred placement), facing, dur,
                  ext? (natural radius of the model in yards, tools/fx/model_extents.py; the default of its role when
                  missing), size? (on-screen diameter in design pixels; overrides the size derived from the anchor),
                  camera?, fallback?, sound?, tint? [r,g,b], source (design|client|guess), exists? (file found in the
                  client MPQs, tools/fx/mpq_has.py), verified (looked right in game), note? } ]
    families  { <family>: { missile?, impact?, area? (preset keys), fallback? (2D style), trail?, sound? } }
    cards     { <cardId>: { name?, family? (overrides the event-based family guess), missile? impact? area?
                (preset key, or false = none), fallback?, trail?, sound?, visual?, flight? (0.1..1 seconds),
                tint? [r,g,b], fxScale? (0.5..2), resultVisuals? { protocolEventType: visual },
                choices? [firstOptionCardId, secondOptionCardId], cueOnPlay? (unconditional ability activation) } }
"""
import argparse
import json
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(ROOT, "data", "fx", "effect_profiles.json")
OUT = os.path.join(ROOT, "addon", "Hearthstone", "MatchFxProfiles.lua")

# keep in step with HsMatchAnim.FAMILIES (MatchAnimations.lua; the Lua tests compare)
FAMILIES = ["fire", "frost", "arcane", "shadow", "holy", "nature", "heal", "area", "buff", "silence", "transform",
            "summon", "destroy", "secret", "generic"]
# 2D fallback styles known to MatchFx.lua (FX.STYLES)
STYLES = ["pulse", "ring", "flash", "implode", "rise", "ripple", "burst"]
KINDS = ["missile", "impact", "aura"]
ROLES = ["missile", "impact", "area"]
MAX_DUR = 2.5
MAX_FLIGHT = 1.0
VISUALS = ["fireball", "bolt", "impact", "slash", "nova", "rain", "heal", "buff", "lightning", "silence",
           "transform", "summon", "destroy", "shield", "bounce", "control", "secret", "drain", "resource",
           "volley", "freeze", "stealth", "curse"]
RESULT_TYPES = ["dmg", "heal", "dest", "sil", "frz", "tf", "buff", "sum", "bnc", "ctl", "arm", "sec",
                "eq", "stl", "rev"]
KEY_RE = re.compile(r"^[a-z][a-z0-9_]*$")
CARD_RE = re.compile(r"^[A-Za-z0-9_]+$")


class ManifestError(Exception):
    pass


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def fail(msg):
    raise ManifestError(msg)


def check_tint(where, v):
    if not (isinstance(v, list) and len(v) == 3 and all(
            is_num(x) and x == x and abs(x) != float("inf") and 0 <= x <= 1 for x in v)):
        fail("%s: tint must be [r, g, b] with values from 0 to 1" % where)


def validate(doc):
    if doc.get("schema") != 1:
        fail("schema must be 1")
    presets = doc.get("presets")
    if not isinstance(presets, list) or not presets:
        fail("presets must be a non-empty list")
    keys = {}
    for p in presets:
        k = p.get("key")
        where = "preset %r" % (k,)
        if not isinstance(k, str) or not KEY_RE.match(k):
            fail("%s: key must be lower case letters, digits and _" % where)
        if k in keys:
            fail("%s: duplicate key" % where)
        keys[k] = p
        if not isinstance(p.get("model"), str) or not p["model"].strip():
            fail("%s: model path missing" % where)
        if p.get("kind") not in KINDS:
            fail("%s: kind must be one of %s" % (where, "|".join(KINDS)))
        if not is_num(p.get("scale")) or p["scale"] <= 0:
            fail("%s: scale must be a positive number" % where)
        pos = p.get("pos")
        if not (isinstance(pos, list) and len(pos) == 3 and all(is_num(x) for x in pos)):
            fail("%s: pos must be [x, y, z]" % where)
        if not is_num(p.get("facing")):
            fail("%s: facing must be a number (radians)" % where)
        if not is_num(p.get("dur")) or not (0 < p["dur"] <= MAX_DUR):
            fail("%s: dur must be above 0 and at most %s seconds" % (where, MAX_DUR))
        if "size" in p and not (is_num(p["size"]) and 16 <= p["size"] <= 512):
            fail("%s: size must be 16 to 512 pixels" % where)
        if "ext" in p and not (is_num(p["ext"]) and 0 < p["ext"] <= 100):
            fail("%s: ext must be a radius in yards above 0 and at most 100" % where)
        if "camera" in p and not (is_num(p["camera"]) and p["camera"] == int(p["camera"]) and p["camera"] >= 0):
            fail("%s: camera must be a whole number" % where)
        if "fallback" in p and p["fallback"] not in STYLES:
            fail("%s: fallback must be one of %s" % (where, "|".join(STYLES)))
        if "sound" in p and not (isinstance(p["sound"], str) and p["sound"]):
            fail("%s: sound must be a string" % where)
        if "tint" in p:
            check_tint(where, p["tint"])
        if p.get("source") not in ("design", "client", "guess"):
            fail("%s: source must be design, client or guess" % where)
        if "exists" in p and not isinstance(p["exists"], bool):
            fail("%s: exists must be true or false" % where)
        if not isinstance(p.get("verified"), bool):
            fail("%s: verified must be true or false" % where)

    def check_role(where, role, value, allow_false):
        if value is False and allow_false:
            return
        if not isinstance(value, str) or value not in keys:
            fail("%s: %s names no preset (%r)" % (where, role, value))
        kind = keys[value]["kind"]
        if role == "missile" and kind != "missile":
            fail("%s: %s preset %r is not a missile" % (where, role, value))
        if role != "missile" and kind == "missile":
            fail("%s: %s preset %r is a missile" % (where, role, value))

    fams = doc.get("families")
    if not isinstance(fams, dict):
        fail("families must be an object")
    for name in FAMILIES:
        if name not in fams:
            fail("family %r is missing" % name)
    for name, f in fams.items():
        where = "family %r" % name
        if name not in FAMILIES:
            fail("%s: unknown family" % where)
        for k, v in f.items():
            if k in ROLES:
                check_role(where, k, v, False)
            elif k == "fallback":
                if v not in STYLES:
                    fail("%s: fallback must be one of %s" % (where, "|".join(STYLES)))
            elif k == "trail":
                if not isinstance(v, bool):
                    fail("%s: trail must be true or false" % where)
            elif k == "sound":
                if not (isinstance(v, str) and v):
                    fail("%s: sound must be a string" % where)
            else:
                fail("%s: unknown field %r" % (where, k))
    cards = doc.get("cards")
    if not isinstance(cards, dict):
        fail("cards must be an object")
    for cid, c in cards.items():
        where = "card %r" % cid
        if not CARD_RE.match(cid):
            fail("%s: bad card id" % where)
        for k, v in c.items():
            if k == "name":
                if not isinstance(v, str):
                    fail("%s: name must be a string" % where)
            elif k == "family":
                if v not in FAMILIES:
                    fail("%s: unknown family %r" % (where, v))
            elif k in ROLES:
                check_role(where, k, v, True)
            elif k == "fallback":
                if v not in STYLES:
                    fail("%s: fallback must be one of %s" % (where, "|".join(STYLES)))
            elif k == "trail":
                if not isinstance(v, bool):
                    fail("%s: trail must be true or false" % where)
            elif k == "sound":
                if not (isinstance(v, str) and v):
                    fail("%s: sound must be a string" % where)
            elif k == "visual":
                if v not in VISUALS:
                    fail("%s: visual must be one of %s" % (where, "|".join(VISUALS)))
            elif k == "flight":
                if not (is_num(v) and v == v and abs(v) != float("inf") and 0.1 <= v <= MAX_FLIGHT):
                    fail("%s: flight must be finite and from 0.1 to %s seconds" % (where, MAX_FLIGHT))
            elif k == "tint":
                check_tint(where, v)
            elif k == "fxScale":
                if not (is_num(v) and v == v and abs(v) != float("inf") and 0.5 <= v <= 2):
                    fail("%s: fxScale must be finite and from 0.5 to 2" % where)
            elif k == "cueOnPlay":
                if not isinstance(v, bool):
                    fail("%s: cueOnPlay must be true or false" % where)
            elif k == "choices":
                if not (isinstance(v, list) and len(v) == 2 and all(
                        isinstance(option, str) and CARD_RE.match(option) and option in cards
                        and isinstance(cards[option], dict) and "visual" in cards[option] for option in v)):
                    fail("%s: choices must name two cards with visual profiles" % where)
            elif k == "resultVisuals":
                if not isinstance(v, dict) or not v:
                    fail("%s: resultVisuals must be a non-empty object" % where)
                for result, visual in v.items():
                    if result not in RESULT_TYPES:
                        fail("%s: unknown resultVisuals event type %r" % (where, result))
                    if visual not in VISUALS:
                        fail("%s: resultVisuals[%s] must be one of %s" % (where, result, "|".join(VISUALS)))
            else:
                fail("%s: unknown field %r" % (where, k))
        if ("visual" in c) != ("flight" in c):
            fail("%s: visual and flight must be specified together" % where)
        if ("tint" in c or "fxScale" in c) and "visual" not in c:
            fail("%s: tint and fxScale require visual" % where)
        if "resultVisuals" in c and ("visual" not in c or "flight" not in c):
            fail("%s: resultVisuals requires visual and flight" % where)
        if "cueOnPlay" in c and "visual" not in c:
            fail("%s: cueOnPlay requires visual and flight" % where)


def checksum(raw):
    """Adler-32 of the manifest bytes with CRLF turned into LF (git may check files out either way)."""
    return zlib.adler32(raw.replace(b"\r\n", b"\n")) & 0xFFFFFFFF


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_num(v):
    if isinstance(v, float) and v == int(v):
        v = int(v)
    if isinstance(v, int):
        return str(v)
    return repr(round(v, 5))


def lua_val(v):
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, str):
        return lua_str(v)
    if isinstance(v, list):
        return "{ " + ", ".join(lua_val(x) for x in v) + " }"
    return lua_num(v)


def lua_fields(d, order, skip=()):
    parts = []
    for k in order:
        if k in d and k not in skip:
            if k == "resultVisuals":
                mapping = d[k]
                value = "{ " + ", ".join("%s = %s" % (key, lua_str(mapping[key]))
                                          for key in RESULT_TYPES if key in mapping) + " }"
            else:
                value = lua_val(d[k])
            parts.append("%s = %s" % (k, value))
    return "{ " + ", ".join(parts) + " }"


def generate(raw):
    doc = json.loads(raw.decode("utf-8"))
    validate(doc)
    out = []
    w = out.append
    w("-- GENERATED by tools/fx/gen_effect_profiles.py from data/fx/effect_profiles.json.")
    w("-- Do not edit by hand: change the manifest and run `python tools/fx/gen_effect_profiles.py`.")
    w("-- Effect profiles of the match animations (part B; MatchFx.lua). The model and sound paths point")
    w("-- into the player's installed WoW data; no model or sound is part of the addon. verified = false: not yet")
    w("-- checked in game with /hs fx. Lua 5.0 (WoW 1.12).")
    w("")
    w("HS_EffectProfiles = {")
    w("    schema = 1,")
    w("    manifestAdler = %d," % checksum(raw))
    w("    presets = {")
    for p in doc["presets"]:
        w("        %s = %s," % (p["key"], lua_fields(p, ["model", "kind", "scale", "pos", "facing", "dur", "ext", "size", "camera",
                                                          "fallback", "sound", "tint", "source", "exists", "verified"])))
    w("    },")
    w("    families = {")
    for name in FAMILIES:
        w("        %s = %s," % (name, lua_fields(doc["families"][name], ["missile", "impact", "area", "fallback", "trail", "sound"])))
    w("    },")
    w("    cards = {")
    for cid in sorted(doc["cards"]):
        c = doc["cards"][cid]
        w("        %s = %s,%s" % (cid, lua_fields(c, ["missile", "impact", "area", "fallback", "trail", "sound", "visual", "flight", "tint", "fxScale", "resultVisuals", "choices", "cueOnPlay"]),
                                 ("  -- " + c["name"]) if "name" in c else ""))
    w("    },")
    w("    -- card id -> family: overrides the family the animation guesses from the events (HsMatchAnim.CARD_FAMILY)")
    w("    cardFamily = {")
    for cid in sorted(doc["cards"]):
        c = doc["cards"][cid]
        if "family" in c:
            w("        %s = %s," % (cid, lua_str(c["family"])))
    w("    },")
    w("}")
    return "\n".join(out) + "\n"


def coverage(doc):
    """Compact auditable report: visual key -> mapped card count and card ids."""
    groups = {}
    for cid, card in doc["cards"].items():
        visuals = []
        if "visual" in card:
            visuals.append(card["visual"])
        visuals.extend(card.get("resultVisuals", {}).values())
        for visual in visuals:
            ids = groups.setdefault(visual, [])
            if cid not in ids:
                ids.append(cid)
    return groups


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--check", action="store_true", help="exit 1 when the output file is stale")
    args = ap.parse_args(argv)
    with open(args.manifest, "rb") as f:
        raw = f.read()
    try:
        text = generate(raw)
    except (ManifestError, ValueError) as e:
        print("effect manifest error: %s" % e, file=sys.stderr)
        return 2
    if args.check:
        try:
            with open(args.out, "rb") as f:
                have = f.read().replace(b"\r\n", b"\n")
        except OSError:
            have = b""
        if have != text.encode("utf-8"):
            print("STALE: %s differs from what %s generates. Run: python tools/fx/gen_effect_profiles.py" %
                  (args.out, args.manifest), file=sys.stderr)
            return 1
        print("up to date: %s" % args.out)
        return 0
    with open(args.out, "wb") as f:
        f.write(text.encode("utf-8"))
    doc = json.loads(raw.decode("utf-8"))
    print("wrote %s (%d presets)" % (args.out, len(doc["presets"])))
    groups = coverage(doc)
    mapped_cards = sum(1 for card in doc["cards"].values() if "visual" in card)
    print("visual coverage: %d mapped cards, %d result tags across %d of %d keys" %
          (mapped_cards, sum(len(ids) for ids in groups.values()), len(groups), len(VISUALS)))
    for visual in VISUALS:
        ids = groups.get(visual, [])
        print("  %s (%d): %s" % (visual, len(ids), ", ".join(sorted(ids)) or "—"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
