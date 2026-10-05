#!/usr/bin/env python3
# Copyright (C) 2026 erickdavestech
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the performance panel artwork (rings, fan, icons, bars, accent line) and its
embedded C tables."""

import io
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
PNG_DIR = ROOT / "assets" / "perf" / "png"
OUT_H = ROOT / "src" / "ps5" / "shellui_payload" / "perf_assets.h"
OUT_C = ROOT / "src" / "ps5" / "shellui_payload" / "perf_assets.c"

SCALE = 2
SS = 4
ICON_PX = 48
FAN_FRAMES = 8
FAN_BLADES = 7
FAN_PX = 96
RING_FRAMES = 21
RING_LOGICAL = 48
RING_STROKE = 5.0

MINT = (0, 255, 136)
CYAN = (21, 253, 145)

LINE_TEX_W, LINE_TEX_H = 4, 10
LINE_EDGE_PX = 2
LINE_EDGE = (0, 0, 0, 150)
TRACK_FILL = (0, 0, 0, 72)



def outlined(img, grow, blur, alpha):
    """Put a soft dark halo under the shape so it reads on any background."""
    a = img.getchannel("A")
    halo = a.filter(ImageFilter.MaxFilter(grow * 2 + 1)).filter(ImageFilter.GaussianBlur(blur))
    halo = halo.point(lambda v: int(v * alpha))
    base = Image.new("RGBA", img.size, (0, 0, 0, 0))
    base.putalpha(halo)
    base.alpha_composite(img)
    return base


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def ring(frame):
    n = RING_LOGICAL * SCALE * SS
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    stroke = int(RING_STROKE * SCALE * SS)
    pad = stroke / 2 + SS
    box = (pad, pad, n - pad, n - pad)
    d.arc(box, 0, 360, fill=(125, 135, 145, 120), width=stroke)
    pct = frame / (RING_FRAMES - 1)
    if pct > 0:
        steps = max(2, int(360 * pct))
        for i in range(steps):
            a0 = -90 + 360 * pct * i / steps
            a1 = -90 + 360 * pct * (i + 1) / steps + 0.6
            d.arc(box, a0, a1, fill=lerp(MINT, CYAN, i / steps) + (255,), width=stroke)
        cx = cy = n / 2
        mid = (n - 2 * pad) / 2 - stroke / 2
        for a, col in ((-90, MINT), (-90 + 360 * pct, CYAN)):
            ar = math.radians(a)
            ex = cx + mid * math.cos(ar)
            ey = cy + mid * math.sin(ar)
            cr = stroke / 2
            d.ellipse((ex - cr, ey - cr, ex + cr, ey + cr), fill=col + (255,))
    img = outlined(img, SS * 2, SS * 1.5, 0.55)
    return img.resize((RING_LOGICAL * SCALE, RING_LOGICAL * SCALE), Image.LANCZOS)


def fan(frame):
    n = FAN_PX * SS
    c = n / 2
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r_in, r_out = n * 0.12, n * 0.40
    base = frame * (360.0 / FAN_BLADES / FAN_FRAMES)
    for k in range(FAN_BLADES):
        a0 = math.radians(base + k * 360.0 / FAN_BLADES)
        pts = []
        steps = 12
        for i in range(steps + 1):
            t = i / steps
            r = r_in + (r_out - r_in) * t
            a = a0 + math.radians(40.0 * t * t)
            pts.append((c + r * math.cos(a), c + r * math.sin(a)))
        for i in range(steps, -1, -1):
            t = i / steps
            r = r_in + (r_out - r_in) * t
            a = a0 + math.radians(40.0 * t * t + 24.0 * (1.0 - 0.35 * t))
            pts.append((c + r * math.cos(a), c + r * math.sin(a)))
        d.polygon(pts, fill=lerp(MINT, CYAN, k / FAN_BLADES) + (230,))
    hub = n * 0.12
    d.ellipse((c - hub, c - hub, c + hub, c + hub), fill=(12, 18, 26, 255),
              outline=MINT + (255,), width=int(n * 0.035))
    img = outlined(img, SS * 3, SS * 2, 0.6)
    return img.resize((FAN_PX, FAN_PX), Image.LANCZOS)


def icon(kind):
    n = ICON_PX * SS
    shape = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(shape)
    w = int(n * 0.08)
    if kind == "bolt":
        pts = [(0.58, 0.04), (0.20, 0.56), (0.47, 0.56), (0.38, 0.96), (0.80, 0.40),
               (0.53, 0.40), (0.62, 0.04)]
        d.polygon([(x * n, y * n) for x, y in pts], fill=255)
    elif kind == "thermo":
        d.rounded_rectangle((n * 0.38, n * 0.04, n * 0.62, n * 0.70), radius=n * 0.12,
                            outline=255, width=w)
        d.ellipse((n * 0.26, n * 0.56, n * 0.74, n * 0.98), fill=255)
        d.rectangle((n * 0.46, n * 0.30, n * 0.54, n * 0.66), fill=255)
    elif kind == "gauge":
        d.arc((n * 0.06, n * 0.14, n * 0.94, n * 1.02), start=180, end=360, fill=255, width=w)
        cx, cy = n * 0.5, n * 0.58
        a = math.radians(-35)
        d.line((cx, cy, cx + n * 0.36 * math.cos(a), cy + n * 0.36 * math.sin(a)), fill=255,
               width=w)
        d.ellipse((cx - n * 0.08, cy - n * 0.08, cx + n * 0.08, cy + n * 0.08), fill=255)
    grad = Image.new("RGBA", (n, n))
    gd = ImageDraw.Draw(grad)
    for y in range(n):
        gd.line((0, y, n, y), fill=lerp(MINT, CYAN, y / (n - 1)) + (255,))
    grad.putalpha(shape)
    padded = Image.new("RGBA", (int(n * 1.3), int(n * 1.3)), (0, 0, 0, 0))
    padded.alpha_composite(grad, (int(n * 0.15), int(n * 0.15)))
    padded = outlined(padded, SS * 3, SS * 2, 0.65)
    return padded.resize((ICON_PX, ICON_PX), Image.LANCZOS)


def vertical_bar():
    h = 64
    img = Image.new("RGBA", (10, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line((0, y, 10, y), fill=lerp(CYAN, MINT, y / (h - 1)) + (245,))
    d.line((0, 0, 0, h), fill=(0, 0, 0, 150))
    d.line((9, 0, 9, h), fill=(0, 0, 0, 150))
    return img


def track():
    return Image.new("RGBA", (8, 8), TRACK_FILL)


def frame():
    img = Image.new("RGBA", (LINE_TEX_W, LINE_TEX_H), MINT + (255,))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, LINE_TEX_W - 1, LINE_EDGE_PX - 1), fill=LINE_EDGE)
    d.rectangle((0, LINE_TEX_H - LINE_EDGE_PX, LINE_TEX_W - 1, LINE_TEX_H - 1), fill=LINE_EDGE)
    return img


def bar_h():
    img = Image.new("RGBA", (8, 8), MINT + (245,))
    d = ImageDraw.Draw(img)
    d.line((0, 0, 7, 0), fill=(0, 0, 0, 150))
    d.line((0, 7, 7, 7), fill=(0, 0, 0, 150))
    return img


def build():
    assets = [("FRAME", "frame.png", frame())]
    for i in range(RING_FRAMES):
        assets.append((f"RING_{i}", f"ring_{i}.png", ring(i)))
    for i in range(FAN_FRAMES):
        assets.append((f"FAN_{i}", f"fan_{i}.png", fan(i)))
    for kind in ("bolt", "thermo", "gauge"):
        assets.append((f"ICON_{kind.upper()}", f"icon_{kind}.png", icon(kind)))
    assets.append(("BAR", "bar.png", vertical_bar()))
    assets.append(("BAR_H", "bar_h.png", bar_h()))
    assets.append(("TRACK", "track.png", track()))

    PNG_DIR.mkdir(parents=True, exist_ok=True)
    for old in PNG_DIR.glob("*.png"):
        if not old.name.startswith("_"):
            old.unlink()
    blobs = []
    for key, name, img in assets:
        img.save(PNG_DIR / name)
        buf = io.BytesIO()
        img.save(buf, format="PNG", optimize=True)
        blobs.append((key, name, buf.getvalue()))

    header = [
        "/*",
        " * Generated by tools/build_perf_assets.py - do not edit by hand.",
        " * Copyright (C) 2026 erickdavestech",
        " * SPDX-License-Identifier: GPL-3.0-or-later",
        " */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "typedef struct { const char *file; const uint8_t *data; uint32_t size; } perf_png_t;",
        "",
        "enum {",
    ]
    header += [f"    PERF_PNG_{key}," for key, _, _ in blobs]
    header += [
        "    PERF_PNG_COUNT",
        "};",
        "",
        f"#define PERF_FAN_FRAMES {FAN_FRAMES}",
        f"#define PERF_RING_FRAMES {RING_FRAMES}",
        "",
        "extern const perf_png_t perf_pngs[PERF_PNG_COUNT];",
        "",
    ]
    OUT_H.write_bytes("\n".join(header).encode())

    body = [
        "/*",
        " * Generated by tools/build_perf_assets.py - do not edit by hand.",
        " * Copyright (C) 2026 erickdavestech",
        " * SPDX-License-Identifier: GPL-3.0-or-later",
        " */",
        '#include "perf_assets.h"',
        "",
    ]
    for key, _, data in blobs:
        body.append(f"static const uint8_t perf_png_{key.lower()}[{len(data)}] = {{")
        for i in range(0, len(data), 20):
            chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 20])
            body.append(f"    {chunk},")
        body.append("};")
        body.append("")
    body.append("const perf_png_t perf_pngs[PERF_PNG_COUNT] = {")
    for key, name, data in blobs:
        body.append(f'    {{"{name}", perf_png_{key.lower()}, {len(data)}}},')
    body.append("};")
    body.append("")
    OUT_C.write_bytes("\n".join(body).encode())

    total = sum(len(d) for _, _, d in blobs)
    print(f"{len(blobs)} PNG, {total} bytes embedded")


if __name__ == "__main__":
    build()
