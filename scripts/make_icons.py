"""Generates the Rallypoint icon set from code: python3 scripts/make_icons.py
Writes launcher/src-tauri/icons/* (png sizes, icon.ico, icon.icns on macOS) and launcher/ui/logo.png.
Original artwork: a shield with a star and chevrons, and a rally pennant behind it."""
import math, os, struct, subprocess, sys, tempfile
from PIL import Image, ImageChops, ImageDraw, ImageFilter

S = 4096                      # work at 4x of 1024, then downsample
ROOT = os.path.join(os.path.dirname(__file__), "..")
ICONS = os.path.join(ROOT, "launcher/src-tauri/icons")

def p(x, y):                  # 1024-space -> work space
    return (x * S / 1024, y * S / 1024)

def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))

def gradient(size, top, bottom, angle_diag=False):
    g = Image.new("RGB", (1, size))
    for y in range(size):
        g.putpixel((0, y), lerp(top, bottom, y / (size - 1)))
    return g.resize((size, size))

def shield_points(scale=1.0, cx=512, top=330, tip=872, half=262):
    """Heater-shield outline in 1024-space, scaled about its centre."""
    def curve(p0, c, p1, n=40):
        return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * c[0] + t * t * p1[0],
                 (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * c[1] + t * t * p1[1]) for t in [i / n for i in range(n + 1)]]
    mid = top + 215
    right = [(cx + half, top), (cx + half, mid)] + curve((cx + half, mid), (cx + half, tip - 120), (cx, tip))
    left = [(2 * cx - x, y) for x, y in reversed(right)]
    pts = right + left
    cy = (top + tip) / 2
    return [p(cx + (x - cx) * scale, cy + (y - cy) * scale) for x, y in pts]

def star(cx, cy, r_out, r_in):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = r_out if i % 2 == 0 else r_in
        pts.append(p(cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def chevron(cx, apex_y, half_w, rise, thick):
    top = [(cx - half_w, apex_y - rise), (cx, apex_y), (cx + half_w, apex_y - rise)]
    bot = [(cx + half_w, apex_y - rise + thick), (cx, apex_y + thick), (cx - half_w, apex_y - rise + thick)]
    return [p(x, y) for x, y in top + bot]

def render(simple=False):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # background tile
    bg = gradient(S, (18, 74, 84), (7, 30, 38)).convert("RGBA")
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([p(24, 24), p(1000, 1000)], radius=S * 0.2, fill=255)
    # soft top highlight: a vertical alpha ramp, so there is no visible edge
    ramp = Image.new("L", (1, S))
    for y in range(S):
        ramp.putpixel((0, y), int(max(0.0, 1 - y / (S * 0.55)) ** 2 * 60))
    glow = Image.new("RGBA", (S, S), (150, 215, 225, 0))
    glow.putalpha(ramp.resize((S, S)))
    bg = Image.alpha_composite(bg, glow)
    img.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([p(44, 44), p(980, 980)], radius=S * 0.18, outline=(214, 226, 230, 90), width=int(S * 0.006))

    if not simple:
        # pennant on a pole, behind the shield
        d.rectangle([p(503, 120), p(521, 360)], fill=(214, 226, 230, 255))
        d.ellipse([p(497, 106), p(527, 136)], fill=(242, 201, 76, 255))
        d.polygon([p(521, 150), p(700, 205), p(521, 262)], fill=(224, 87, 58, 255))
        d.polygon([p(521, 150), p(700, 205), p(521, 176)], fill=(240, 130, 96, 255))

    # shield: soft shadow, silver rim, gold face
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).polygon([(x + S * 0.01, y + S * 0.02) for x, y in shield_points()], fill=(0, 0, 0, 150))
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(S * 0.015)))
    d = ImageDraw.Draw(img)
    d.polygon(shield_points(1.0), fill=(222, 232, 236, 255))
    face = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    fm = Image.new("L", (S, S), 0)
    ImageDraw.Draw(fm).polygon(shield_points(0.92), fill=255)
    face.paste(gradient(S, (247, 214, 98), (196, 140, 26)).convert("RGBA"), (0, 0), fm)
    img = Image.alpha_composite(img, face)
    d = ImageDraw.Draw(img)
    # star and chevrons, clipped to the gold face so nothing can poke past the rim
    marks = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    md = ImageDraw.Draw(marks)
    cy = 520 if not simple else 565
    md.polygon(star(512, cy, 132 if not simple else 185, 55 if not simple else 76), fill=(11, 52, 62, 255))
    if not simple:
        for k in range(2):
            md.polygon(chevron(512, 668 + k * 72, 138, 56, 32), fill=(11, 52, 62, 255))
    clip = Image.new("L", (S, S), 0)
    ImageDraw.Draw(clip).polygon(shield_points(0.9), fill=255)
    marks.putalpha(ImageChops.multiply(marks.getchannel("A"), clip))
    img = Image.alpha_composite(img, marks)
    return img

def save_sizes():
    full, simple = render(), render(simple=True)
    def at(n):
        return (simple if n <= 48 else full).resize((n, n), Image.LANCZOS)
    os.makedirs(ICONS, exist_ok=True)
    at(1024).save(os.path.join(ICONS, "icon.png"))
    at(32).save(os.path.join(ICONS, "32x32.png"))
    at(128).save(os.path.join(ICONS, "128x128.png"))
    at(256).save(os.path.join(ICONS, "128x128@2x.png"))
    at(128).save(os.path.join(ROOT, "launcher/ui/logo.png"))
    # .ico with one PNG-compressed image per size (small sizes use the simplified mark)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    blobs = []
    for n in sizes:
        tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
        at(n).save(tmp.name)
        blobs.append(open(tmp.name, "rb").read())
        os.unlink(tmp.name)
    out = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    for n, b in zip(sizes, blobs):
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(b), offset)
        offset += len(b)
    open(os.path.join(ICONS, "icon.ico"), "wb").write(out + b"".join(blobs))
    # .icns (macOS only: needs iconutil)
    if sys.platform == "darwin":
        with tempfile.TemporaryDirectory() as d:
            iconset = os.path.join(d, "icon.iconset")
            os.mkdir(iconset)
            for base in (16, 32, 128, 256, 512):
                at(base).save(os.path.join(iconset, f"icon_{base}x{base}.png"))
                at(base * 2).save(os.path.join(iconset, f"icon_{base}x{base}@2x.png"))
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(ICONS, "icon.icns")], check=True)
    # preview sheet for eyeballing (not committed)
    sheet = Image.new("RGBA", (1024 + 40 + 256 + 40 + 64 * 3 + 40, 1024), (240, 240, 240, 255))
    sheet.paste(at(1024), (0, 0), at(1024))
    sheet.paste(at(256), (1064, 0), at(256))
    for i, n in enumerate((16, 32, 48)):
        sheet.paste(at(n), (1064 + 256 + 40 + i * 70, 20), at(n))
    sheet.convert("RGB").save(os.environ.get("ICON_PREVIEW", os.path.join(tempfile.gettempdir(), "rallypoint-icon-preview.png")))

if __name__ == "__main__":
    save_sizes()
