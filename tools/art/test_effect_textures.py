"""Targeted generation, alpha edge, determinism, and BLP round-trip checks."""
import hashlib
import io
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import effect_textures as fx  # noqa: E402
import convert_blp  # noqa: E402

PRIOR_HASHES = {
    "fire_core": "2522743e408e512fb47f7e9c40172f959bb307d3ce6d0bbed3d3e512abecef02",
    "soft_glow": "a396798d6f27103273facdd77e53719f1bb8bad4a3b55a032d67cb1882560318",
    "spark": "787bc1c20eb69940e23408975f1b43b109c2e5e40f1a36d716522c20a8eae172",
    "shockwave": "8ce1099c2b08754c597308e94e7a256adc6cb44b0eb9ee0cd366afa89e50b371",
    "neutral_core": "2cf865fbfb8aaa6aabee6fdf8aef876e54ab518750133c218747001aa58b0eed",
    "neutral_glow": "d391432b768345a36c63a3b6e87b3aad9a92571b999428db3c24d974221928c9",
    "neutral_spark": "cdd5ca12e973b091a7068e3790e96571106a8f156addc40fb74ddafd4a6941ae",
    "neutral_ring": "c0b48f292244f70cf6a4b31922b1825f87cf5a24e32c0649127e9c6d756e9ac0",
    "frost_shard": "736efee4c2e66973ea3661bdffce157be51653846f84a3e77e2313871861021b",
    "slash_arc": "679aa2a2e1c72775637ebf0a836a67095234cae3722cc0549754117fe0b176e6",
}


def main():
    with open(fx.MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)["entries"]
    for name in fx.NAMES:
        image = fx.render(name)
        assert image.size == (fx.SIZE, fx.SIZE)
        path = os.path.join(fx.OUT, name + ".blp")
        with open(path, "rb") as f:
            encoded = f.read()
        expected, mip_count = convert_blp.build_blp(image)
        again, again_mips = convert_blp.build_blp(image)
        assert expected == again and mip_count == again_mips, name + " output must be deterministic"
        record = manifest[name]
        digest = hashlib.sha256(expected).hexdigest()
        assert encoded == expected, name + " shipped BLP differs from generated output"
        assert len(encoded) == record["bytes"] and len(encoded) == len(expected), name + " manifest byte size mismatch"
        assert digest == record["sha256"], name + " manifest hash mismatch"
        if name in PRIOR_HASHES:
            assert digest == PRIOR_HASHES[name], name + " previously shipped texture must remain bit-for-bit unchanged"
        # The entire source perimeter should be transparent to avoid any square texture edge.
        perimeter = ([image.getpixel((x, 0))[3] for x in range(fx.SIZE)] +
                     [image.getpixel((x, fx.SIZE-1))[3] for x in range(fx.SIZE)] +
                     [image.getpixel((0, y))[3] for y in range(fx.SIZE)] +
                     [image.getpixel((fx.SIZE-1, y))[3] for y in range(fx.SIZE)])
        assert max(perimeter) == 0, name + " must have transparent perimeter"
        decoded = Image.open(io.BytesIO(encoded)).convert("RGBA")
        assert decoded.size == image.size
        decoded_perimeter = ([decoded.getpixel((x, 0))[3] for x in range(fx.SIZE)] +
                             [decoded.getpixel((x, fx.SIZE-1))[3] for x in range(fx.SIZE)] +
                             [decoded.getpixel((0, y))[3] for y in range(fx.SIZE)] +
                             [decoded.getpixel((fx.SIZE-1, y))[3] for y in range(fx.SIZE)])
        assert max(decoded_perimeter) == 0, name + " BLP must keep transparent perimeter"
        # Sample each primitive where its shape should be visible, and nearby negative
        # space where its geometry should remain transparent.
        point = {
            "fire_core": (128, 128), "soft_glow": (128, 128), "spark": (157, 72),
            "shockwave": (205, 128), "neutral_core": (128, 128),
            "neutral_glow": (128, 128), "neutral_spark": (157, 72),
            "neutral_ring": (205, 128), "frost_shard": (128, 128), "slash_arc": (202, 143),
            "lightning_arc": (186, 187), "smoke_puff": (128, 128),
            "portal_ring": (201, 128), "shield_shell": (128, 54),
        }[name]
        sample = decoded.getpixel(point)[3]
        assert sample > 0, name + " representative shape sample should be visible"
        if name in ("shockwave", "neutral_ring", "portal_ring"):
            assert decoded.getpixel((128, 128))[3] == 0, name + " must keep its center open"
        if name in ("spark", "neutral_spark"):
            visible = sum(decoded.getchannel("A").histogram()[51:])
            assert visible < fx.SIZE * fx.SIZE * .08, "spark should stay a narrow ember rather than fill its quad"
        if name == "slash_arc":
            assert decoded.getpixel((128, 128))[3] < decoded.getpixel(point)[3], "slash arc should retain a translucent center channel"
        if name == "shield_shell":
            assert decoded.getpixel((128, 128))[3] > 0, "shield should have a subtle interior fill"
            assert decoded.getpixel((226, 128))[3] == 0, "shield should have no rectangular side border"
        if name in ("lightning_arc", "smoke_puff"):
            assert decoded.getpixel((230, 42))[3] == 0, name + " should leave its outer corner transparent"
        print("PASS %-12s bytes=%d sample_alpha=%d" % (name, len(encoded), sample))


if __name__ == "__main__":
    main()
