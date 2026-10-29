"""Checks for the Battlegrounds art: the procedural mode textures (determinism, committed BLP
equals generated output, manifest sizes and hashes, transparent perimeter, representative samples) and the portrait
manifest's id set against the engine's own card table.

  tools/art/.venv/Scripts/python tools/art/test_bg_art.py

The portraits themselves need the (gitignored) Blizzard renders in art/cache, so here only the committed manifest is
checked for drift: if src/battlegrounds/HsBgCardData.inc gains or loses a card, re-run tools/art/bg_minions.py.
"""
import hashlib
import io
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bg_textures as bx  # noqa: E402
import bg_heroes as bh  # noqa: E402
import bg_minions as bm  # noqa: E402
import bg_quests as bq  # noqa: E402
import convert_blp  # noqa: E402


def test_textures():
    with open(bx.MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)["entries"]
    assert set(manifest) == set(bx.NAMES), "manifest names differ from the generator's"
    for name, size in bx.NAMES.items():
        image = bx.render(name)
        assert image.size == ((size, size) if name not in bx.SPECIAL else {"quest_frame": (512, 512), "reward_card": (256, 512), "quest_glow": (512, 512)}[name]), name + " size"
        with open(os.path.join(bx.OUT, name + ".blp"), "rb") as f:
            encoded = f.read()
        expected, mips = convert_blp.build_blp(image)
        again, mips2 = convert_blp.build_blp(image)
        assert expected == again and mips == mips2, name + " output must be deterministic"
        record = manifest[name]
        digest = hashlib.sha256(expected).hexdigest()
        assert encoded == expected, name + " shipped BLP differs from generated output"
        assert len(encoded) == record["bytes"], name + " manifest byte size mismatch"
        assert digest == record["sha256"], name + " manifest hash mismatch"
        assert record["size"] == [image.size[0], image.size[1]], name + " manifest size"
        assert record["file"] == "addon/Hearthstone/Textures/Bg/" + name + ".blp", name + " manifest file path"
        if name in bx.SPECIAL:       # drawn into a corner of a canvas, not centred: no perimeter / centre rules
            print("PASS %-12s bytes=%d" % (name, len(encoded)))
            continue
        # no square edge: the whole perimeter is transparent
        perimeter = ([image.getpixel((x, 0))[3] for x in range(size)] +
                     [image.getpixel((x, size - 1))[3] for x in range(size)] +
                     [image.getpixel((0, y))[3] for y in range(size)] +
                     [image.getpixel((size - 1, y))[3] for y in range(size)])
        assert max(perimeter) == 0, name + " must have a transparent perimeter"
        decoded = Image.open(io.BytesIO(encoded)).convert("RGBA")
        assert decoded.size == image.size
        assert decoded.getpixel((size // 2, size // 2))[3] > 0 or name in ("golden_ring", "hero_frame", "hero_glow"), name + " center sample"
        print("PASS %-12s bytes=%d" % (name, len(encoded)))
    # the ring is an oval band: the middle is open, the rim is not
    ring = Image.open(os.path.join(bx.OUT, "golden_ring.blp")).convert("RGBA")
    assert ring.getpixel((64, 64))[3] == 0, "the ring must keep its center open"
    assert ring.getpixel((64, 8))[3] > 0 or ring.getpixel((64, 11))[3] > 0, "the ring must be visible at the top"
    assert ring.getpixel((0, 0))[3] == 0 and ring.getpixel((3, 3))[3] == 0, "the ring must not reach the texture corners"


def test_portrait_manifest():
    ids = bm.table_ids()
    assert len(ids) == len(set(ids)), "duplicate ids in the engine table"
    assert len(ids) == 188, "the table should hold 167 pool minions + 21 tokens, got %d" % len(ids)
    with open(bm.MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)
    entries = manifest["entries"]
    # the spells (Blood Gem, Bananas, Gold Coin, ...) get an oval portrait too, except the one token spell with no render
    spells = set(bm.all_table_ids()) - set(ids) - {"BG23_018t"}
    assert set(entries) == set(ids) | spells, "bg_minions.manifest.json is stale; re-run tools/art/bg_minions.py"
    assert manifest["count"] == len(entries), "manifest count"
    for cid, rec in entries.items():
        assert rec["blp_sha256"] and rec["blp_bytes"] > 0, cid + " needs a generated portrait"
        assert rec["render_id"] == bm.render_candidates(cid)[0], cid + " render id"
    print("PASS portraits  %d minions + %d spells, %d manifest entries" % (len(ids), len(spells), len(entries)))

    # the full cards: every normal id (except the two token spells with no HearthstoneJSON render), plus golden twins
    # (only those with a render; the addon falls back otherwise)
    cards = manifest["cards"]
    goldens = bm.golden_ids()
    all_ids = bm.all_table_ids()
    missing = set(all_ids) - set(cards)
    assert missing <= {"BG23_018t"}, \
        "bg_minions.manifest.json cards miss normal ids; re-run tools/art/bg_minions.py: " + " ".join(sorted(missing))
    extra = set(cards) - set(all_ids)
    assert extra <= {goldens[cid] for cid in all_ids if cid in goldens}, \
        "bg_minions.manifest.json cards hold unknown ids: " + " ".join(sorted(extra - set(goldens.values())))
    for key, rec in cards.items():
        assert rec["card"] == key, key + " card id"
        assert rec["size"] == [256, 512], key + " card size"
        assert rec["format"].startswith("BLP2 DXT5"), key + " card format"
        assert rec["sha256"] and rec["bytes"] > 0, key + " needs a generated card"
        assert 0 < rec["bottom"] <= 1 and abs(rec["bottom"] * 512 - round(rec["bottom"] * 512)) < 1e-9, key + " bottom"
        if key in all_ids:
            assert rec["render_id"] == bm.render_candidates(key)[0], key + " card render id"
        else:
            assert goldens[rec["golden_of"]] == key, key + " golden_of"
    print("PASS cards  %d normal, %d golden" % (len(all_ids), len(extra)))


def test_hero_manifest():
    # the curated heroes of hero_armor.json and the extra choice powers come from bg.normalized.json; curated_heroes
    # raises if one is missing.
    heroes = bh.curated_heroes(bh.load_bg())            # (hero id, name, power id, power name), curated order
    curated = [h[0] for h in heroes]
    powers = {pid for pid, _ in bh.curated_powers(bh.load_bg(), heroes)}
    assert len(curated) == 86, "the curated list should hold 86 heroes, got %d" % len(curated)
    assert len(set(curated)) == 86, "duplicate curated hero id"
    assert len(powers) == 98, "the curated heroes and extra powers should hold 98 hero powers, got %d" % len(powers)
    with open(bh.MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)
    assert manifest["bob"] == bh.BOB_ID
    assert set(manifest["entries"]) == set(curated) | {"bob", "bob_arch"} | {h + "_arch" for h in curated}, \
        "bg_heroes.manifest.json id set drifted; re-run tools/art/bg_heroes.py"
    assert set(manifest["powers"]) == powers, \
        "bg_heroes.manifest.json powers drifted; re-run tools/art/bg_heroes.py"
    assert set(manifest["powerCards"]) == powers,         "bg_heroes.manifest.json powerCards drifted; re-run tools/art/bg_heroes.py"
    for hid, _, pid, _ in heroes:
        assert manifest["entries"][hid]["card"] == hid, hid + " manifest card id"
        assert manifest["powers"][pid]["card"] == pid, pid + " manifest card id"
    print("PASS hero manifest  %d heroes, %d powers" % (len(curated), len(powers)))


def test_quest_manifest():
    # the in-pool selection is quest_values.json (`in_pool`) looked up in bg.normalized.json: 12 quests, 23 rewards.
    # load_selection raises if a pool id is missing from the dataset, so this cannot drift from the engine.
    selection = bq.load_selection()                      # (group, id, dbf_id, name)
    quests = [r for r in selection if r[0] == "quest"]
    rewards = [r for r in selection if r[0] == "reward"]
    assert len(quests) == 12, "the 24.2 in-pool list should hold 12 quests, got %d" % len(quests)
    assert len(rewards) == 23, "the 24.2 in-pool list should hold 23 rewards, got %d" % len(rewards)
    with open(bq.MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)
    entries = manifest["entries"]
    want = {r[1] for r in selection}
    assert set(entries) == want, "bg_quests.manifest.json id set drifted; re-run tools/art/bg_quests.py"
    assert manifest["count"] == len(want), "manifest count"
    for group, cid, dbf_id, name in selection:
        rec = entries[cid]
        assert rec["card"] == cid, cid + " manifest card id"
        assert rec["dbf_id"] == dbf_id, cid + " manifest dbf_id"
        assert rec["group"] == group, cid + " manifest group"
        assert rec["name"] == name, cid + " manifest name"
        assert rec["file"] == "art/out/BgQuests/" + cid + ".blp", cid + " manifest output file"
        assert rec["sha256"] and rec["bytes"] > 0, cid + " needs a generated portrait"
        assert rec["size"] == [bq.SIZE, bq.SIZE], cid + " manifest size"
    print("PASS quest manifest  %d quests, %d rewards" % (len(quests), len(rewards)))


if __name__ == "__main__":
    test_textures()
    test_portrait_manifest()
    test_hero_manifest()
    test_quest_manifest()
    print("all ok")
