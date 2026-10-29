#!/usr/bin/env python3
"""Bake the body texture of a CreatureDisplayInfoExtra row (the "BakeName" file).

The vanilla-family client builds a humanoid NPC's body only from a pre-rendered 256x256 texture in
Textures\\BakedNpcTextures\\<BakeName>. An empty BakeName renders a white model, because the client does not
compose the armor from the NPCItemDisplayID columns on its own (hoods, shoulders and the glove or boot
geosets still come from the item ids). This script renders that texture:

  base skin + face (taken from an existing bake) + underwear + armor textures of the row's items, in order

and writes a BLP2 (DXT5 with mips) named after the MD5 of its content, ready to go into a patch MPQ at
Textures\\BakedNpcTextures\\<name>.blp. The row's BakeName must then be set to that file name (DBC edit and
MPQ packing are done by hand, this tool changes neither).

Atlas layout (x, y, w, h): ArmUpper 0,0,128,64 / ArmLower 0,64,128,64 / Hand 0,128,128,32 / Face 0,160,128,96 /
TorsoUpper 128,0,128,64 / TorsoLower 128,64,128,32 / LegUpper 128,96,128,64 / LegLower 128,160,128,64 /
Foot 128,224,128,32.

Palettized BLP2 files with an 8-bit alpha plane (most armor overlays: belts, bracers, chest upper ...) are decoded
here, because Pillow drops that plane and returns them fully transparent.

Needs Pillow and mpyq (pip install mpyq), plus the client Data folder (MPQs are only read).

  python tools/art/bake_npc_texture.py --extra-id 15992 --face-bake b6ef4ea7c2bade2344f774d1b958ee8f.blp
  ... --items 33650 31115 31111 31110 31127 33651   # override the row's items (layer order, bottom to top)
  ... --out C:/somewhere --data C:/TurtleDev/04_client/game/Data --preview

--face-bake is the BakeName of an existing row with the same race, sex, skin, face and facial hair; only its face
area (x 0..128, y 160..256) is copied. Without it the bald base face is used.
"""
import argparse
import hashlib
import io
import os
import struct
import sys
import warnings

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402

import convert_blp as C  # noqa: E402

warnings.filterwarnings("ignore")
BS = "\\"
DEFAULT_DATA = r"C:\TurtleDev\04_client\game\Data"

RACE_DIR = {1: "Human", 2: "Orc", 3: "Dwarf", 4: "NightElf", 5: "Scourge", 6: "Tauren", 7: "Gnome", 8: "Troll"}
SEX_DIR = {0: "Male", 1: "Female"}

# region directory -> (x, y, w, h); ItemDisplayInfo texture slot i uses SLOT_REGION[i]
REGION = {
    "ArmUpperTexture": (0, 0, 128, 64),
    "ArmLowerTexture": (0, 64, 128, 64),
    "HandTexture": (0, 128, 128, 32),
    "TorsoUpperTexture": (128, 0, 128, 64),
    "TorsoLowerTexture": (128, 64, 128, 32),
    "LegUpperTexture": (128, 96, 128, 64),
    "LegLowerTexture": (128, 160, 128, 64),
    "FootTexture": (128, 224, 128, 32),
}
SLOT_REGION = ["ArmUpperTexture", "ArmLowerTexture", "HandTexture", "TorsoUpperTexture", "TorsoLowerTexture",
               "LegUpperTexture", "LegLowerTexture", "FootTexture"]


class Client:
    """Reads files from the client MPQs, highest priority archive first."""

    def __init__(self, data_dir):
        import mpyq
        names = [f for f in os.listdir(data_dir) if f.lower().endswith(".mpq") and " " not in f]

        def prio(f):
            low = f.lower()
            if low.startswith("patch-"):
                return (0, tuple(-ord(c) for c in low))  # patch-W before patch-9 before patch-3
            if low == "patch.mpq":
                return (1, ())
            return (2, tuple(ord(c) for c in low))

        self.paths = [os.path.join(data_dir, f) for f in sorted(names, key=prio)]
        self.mpq = mpyq
        self.open = {}

    def get(self, name):
        for p in self.paths:
            if p not in self.open:
                self.open[p] = self.mpq.MPQArchive(p, listfile=False)
            try:
                d = self.open[p].read_file(name)
            except Exception:
                d = None
            if d:
                return d
        return None


def decode_blp(d):
    typ, enc, ad, ae, mips, w, h = struct.unpack_from("<IBBBBII", d, 4)
    if enc != 1:
        return Image.open(io.BytesIO(d)).convert("RGBA")
    off = struct.unpack_from("<I", d, 20)[0]
    size = struct.unpack_from("<I", d, 84)[0]
    pal = d[148:148 + 1024]
    idx = d[off:off + w * h]
    rest = d[off + w * h:off + size]
    if ad == 8:
        a = rest[:w * h]
    elif ad == 4:
        a = bytes(((rest[i // 2] >> (4 * (i & 1))) & 15) * 17 for i in range(w * h))
    elif ad == 1:
        a = bytes(255 * ((rest[i // 8] >> (i & 7)) & 1) for i in range(w * h))
    else:
        a = None
    out = bytearray()
    for i in range(w * h):
        b, g, r, _ = pal[idx[i] * 4:idx[i] * 4 + 4]
        out += bytes((r, g, b, a[i] if a else 255))
    return Image.frombytes("RGBA", (w, h), bytes(out))


def read_dbc(client, name):
    d = client.get("DBFilesClient" + BS + name + ".dbc")
    if not d:
        sys.exit("DBFilesClient\\%s.dbc not found in the client MPQs" % name)
    magic, rc, fc, rs, ss = struct.unpack_from("<4sIIII", d, 0)
    sb = 20 + rc * rs

    def string(o):
        return d[sb + o:d.index(b"\0", sb + o)].decode("latin1")

    return d, rc, fc, rs, string


def extra_row(client, extra_id):
    d, rc, fc, rs, string = read_dbc(client, "CreatureDisplayInfoExtra")
    for i in range(rc):
        r = struct.unpack_from("<%dI" % fc, d, 20 + i * rs)
        if r[0] == extra_id:
            return {"race": r[1], "sex": r[2], "skin": r[3], "items": list(r[8:17]), "bake": string(r[18])}
    sys.exit("CreatureDisplayInfoExtra id %d not found" % extra_id)


def item_textures(client, item_ids):
    d, rc, fc, rs, string = read_dbc(client, "ItemDisplayInfo")
    want = set(item_ids)
    out = {}
    for i in range(rc):
        r = struct.unpack_from("<23I", d, 20 + i * rs)
        if r[0] in want:
            out[r[0]] = [string(x) for x in r[14:22]]
    return out


def load(client, name):
    d = client.get(name)
    return decode_blp(d) if d else None


def bake(client, race, sex, skin, items, face_bake):
    rd, sd = RACE_DIR[race], SEX_DIR[sex]
    char = "Character" + BS + rd + BS + sd + BS + rd + sd
    canvas = load(client, "%sSkin00_%02d.blp" % (char, skin))
    if canvas is None:
        sys.exit("base skin %sSkin00_%02d.blp not found" % (char, skin))
    if face_bake:
        old = load(client, "Textures" + BS + "BakedNpcTextures" + BS + face_bake)
        if old is None:
            sys.exit("face bake %s not found" % face_bake)
        canvas.paste(old.crop((0, 160, 128, 256)), (0, 160))
    for part, region in (("NakedPelvisSkin", "LegUpperTexture"), ("NakedTorsoSkin", "TorsoUpperTexture")):
        im = load(client, "%s%s00_%02d.blp" % (char, part, skin))
        if im is not None:
            x, y, w, h = REGION[region]
            canvas.alpha_composite(im.resize((w, h)), (x, y))
    textures = item_textures(client, items)
    for iid in items:
        if iid not in textures:
            print("item display %d has no ItemDisplayInfo row, skipped" % iid)
            continue
        for slot, tex in enumerate(textures[iid]):
            if not tex:
                continue
            region = SLOT_REGION[slot]
            x, y, w, h = REGION[region]
            order = ("F", "U", "M") if sex else ("M", "U", "F")
            im = None
            for suf in order:
                im = load(client, "Item" + BS + "TextureComponents" + BS + region + BS + tex + "_" + suf + ".blp")
                if im is not None:
                    break
            if im is None:
                print("missing texture %s %s" % (region, tex))
                continue
            print("item %d  %-18s %s_%s" % (iid, region, tex, suf))
            if im.size != (w, h):
                im = im.resize((w, h), Image.LANCZOS)
            canvas.alpha_composite(im, (x, y))
    return canvas


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--extra-id", type=int, help="CreatureDisplayInfoExtra id: race, sex, skin and items come from it")
    ap.add_argument("--race", type=int, choices=sorted(RACE_DIR), help="DisplayRaceID (instead of --extra-id)")
    ap.add_argument("--sex", type=int, choices=(0, 1), help="DisplaySexID (instead of --extra-id)")
    ap.add_argument("--skin", type=int, help="SkinID (default: from the row, or 0)")
    ap.add_argument("--items", type=int, nargs="+", help="item display ids in layer order (default: the row's items)")
    ap.add_argument("--face-bake", help="BakeName of an existing bake to take the face area from")
    ap.add_argument("--data", default=DEFAULT_DATA, help="client Data folder (default: %(default)s)")
    ap.add_argument("--out", default=".", help="output folder (default: current directory)")
    ap.add_argument("--preview", action="store_true", help="also write a PNG of the atlas next to the BLP")
    args = ap.parse_args()

    client = Client(args.data)
    row = extra_row(client, args.extra_id) if args.extra_id else None
    race = args.race or (row and row["race"])
    sex = args.sex if args.sex is not None else (row and row["sex"])
    skin = args.skin if args.skin is not None else (row["skin"] if row else 0)
    items = args.items or (row and [i for i in row["items"] if i])
    if not race or sex is None or not items:
        sys.exit("give --extra-id, or --race, --sex and --items")
    print("race %d (%s), sex %d, skin %d, items %s" % (race, RACE_DIR[race], sex, skin, items))
    canvas = bake(client, race, sex, skin, items, args.face_bake)
    data, nmips = C.build_blp(canvas)
    name = hashlib.md5(data).hexdigest() + ".blp"
    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, name)
    with open(out, "wb") as fh:
        fh.write(data)
    if args.preview:
        canvas.save(out[:-4] + ".png")
    print("wrote %s (%d bytes, %d mips)" % (out, len(data), nmips))
    print("MPQ path : Textures\\BakedNpcTextures\\" + name)
    print("BakeName : " + name)


if __name__ == "__main__":
    main()
