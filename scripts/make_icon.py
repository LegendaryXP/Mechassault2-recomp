"""Make the app icon (assets/icon.png, assets/icon.ico) from the game's own
title image: the picture the Xbox dashboard shows for MechAssault 2, kept in
ma2.xbe's $$XTIMAGE section as an XPR0 texture. Read from the player's own
game\\ma2.xbe at build time (CMakeLists.txt); the result is the game's art,
so it is not kept in the source tree. Run from the project folder:
    python scripts/make_icon.py [path\\to\\ma2.xbe]
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw

xbe_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("game", "ma2.xbe")
d = open(xbe_path, "rb").read()


def section(name):
    base = struct.unpack_from("<I", d, 0x104)[0]
    count = struct.unpack_from("<I", d, 0x11C)[0]
    hdrs = struct.unpack_from("<I", d, 0x120)[0] - base
    for i in range(count):
        _, _, _, raw, size, name_va = struct.unpack_from("<IIIIII", d, hdrs + i * 56)
        n = name_va - base
        if d[n:d.index(b"\0", n)].decode() == name:
            return d[raw:raw + size]
    raise SystemExit(f"{xbe_path}: no {name} section")


def xpr_texture(blob):
    """The first texture of an XPR0 bundle, as a PIL image."""
    if blob[:4] != b"XPR0":
        raise SystemExit("not an XPR0 bundle")
    header_size = struct.unpack_from("<I", blob, 8)[0]
    common, data, lock, fmt, size = struct.unpack_from("<IIIII", blob, 12)
    kind = (fmt >> 8) & 0xFF
    w, h = 1 << ((fmt >> 20) & 0xF), 1 << ((fmt >> 24) & 0xF)
    pix = blob[header_size + data:]
    fourcc = {0x0C: b"DXT1", 0x0E: b"DXT3", 0x0F: b"DXT5"}.get(kind)
    if fourcc:                              # block compressed: not swizzled
        dds = struct.pack("<4sIIIIIII44xII4s20xI16x", b"DDS ", 124, 0x1007, h, w,
                          0, 0, 0, 32, 0x4, fourcc, 0x1000)
        return Image.open(io.BytesIO(dds + pix)).convert("RGBA")
    if kind == 0x06:                        # A8R8G8B8, swizzled (Morton order)
        out = Image.new("RGBA", (w, h))
        px = out.load()
        for y in range(h):
            for x in range(w):
                m, bit, xi, yi = 0, 0, x, y
                while xi or yi:             # interleave x and y bits
                    m |= (xi & 1) << bit; bit += 1; xi >>= 1
                    m |= (yi & 1) << bit; bit += 1; yi >>= 1
                b, g, r, a = pix[m * 4:m * 4 + 4]
                px[x, y] = (r, g, b, a)
        return out
    raise SystemExit(f"texture format 0x{kind:02X} not handled")


img = xpr_texture(section("$$XTIMAGE"))
print(f"title image {img.size[0]}x{img.size[1]}")
os.makedirs("assets", exist_ok=True)
big = img.resize((256, 256), Image.LANCZOS)

# The picture is a round badge on opaque black (DXT1, no alpha): keep the
# circle, clear the corners, so the icon sits on any background.
rgb = big.convert("RGB")
lit = [x for x in range(256) if sum(rgb.getpixel((x, 128))) > 60]
x0, x1 = lit[0], lit[-1] + 1
mask = Image.new("L", (1024, 1024), 0)
ImageDraw.Draw(mask).ellipse([x0 * 4, x0 * 4, x1 * 4, x1 * 4], fill=255)
big.putalpha(mask.resize((256, 256), Image.LANCZOS))
big.save("assets/icon.png")
big.save("assets/icon.ico",
         sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("assets/icon.png, assets/icon.ico")
