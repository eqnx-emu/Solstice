"""Render every Solstice icon from the shapes in solstice.svg.

Solstice uses the same mark as Equinox (Moonlight-Switch fork), so the two apps read as a pair.

Run from the repository root:  python art/logo/render_logo.py
Needs Pillow. Shapes live on a 100-unit grid (same as solstice.svg). Raster output is drawn at
8x supersampling and downscaled so edges stay smooth. The SVG outputs are written from the same
shape data, so every format matches.

The tray state icons (playing, pausing, locked) put a round badge in the lower right corner of
the mark. Output file names keep their upstream Sunshine names so no build or code change is
needed.
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))  # art/logo -> repo
WEB_IMG = os.path.join(ROOT, "src_assets", "common", "assets", "web", "public", "images")
MACOS = os.path.join(ROOT, "src_assets", "macos")
FONT = os.path.join(HERE, "onest_semibold.ttf")

DUSK = (22, 26, 51)
DUSK_GLOW = (35, 40, 88)
SUN = (244, 184, 96)
MOON = (157, 184, 255)
TEXT_LIGHT = (238, 240, 250)
SS = 8

RAYS = [(14, 50, 4, 50), (21.7, 27, 14.6, 19.9), (21.7, 73, 14.6, 80.1), (47, 10, 47, 2), (47, 90, 47, 98)]
RAY_WIDTH = 5
CRATERS = [(62, 38, 4, 0.35), (66, 60, 6, 0.25)]
STAR = [(88, 22), (89.6, 26), (93.6, 27.6), (89.6, 29.2), (88, 33.2), (86.4, 29.2), (82.4, 27.6), (86.4, 26)]
DOT = (92, 72, 2.2)

# Tray state badge: a disc in the lower right corner, cut out from the mark by a thin gap.
BADGE_X, BADGE_Y, BADGE_R, BADGE_GAP = 76, 76, 21, 4
BADGE_COLORS = {
    "playing": (126, 224, 168),
    "pausing": SUN,
    "locked": (163, 171, 201),
}
PLAY = [(70.5, 65.5), (70.5, 86.5), (88, 76)]
PAUSE_BARS = [(71, 68, 71, 84), (81, 68, 81, 84)]
PAUSE_WIDTH = 5
LOCK_BODY = (67, 74, 85, 87, 3)  # x1, y1, x2, y2, corner radius
LOCK_SHACKLE = (76, 74, 6, 3.5)  # cx, cy, radius, stroke width


def hexc(rgb):
    """Format an RGB tuple as #rrggbb."""
    return "#%02x%02x%02x" % rgb


def fmt(v):
    """Format a number for SVG without trailing zeros."""
    return ("%.2f" % v).rstrip("0").rstrip(".")


def mark_svg_body(detail=True, badge=None):
    """SVG elements (100-unit grid) for the mark, optionally with a tray state badge."""
    out = ['<g stroke="%s" stroke-width="%s" stroke-linecap="round">' % (hexc(SUN), RAY_WIDTH)]
    for x1, y1, x2, y2 in RAYS:
        out.append('  <line x1="%s" y1="%s" x2="%s" y2="%s"/>' % tuple(map(fmt, (x1, y1, x2, y2))))
    out.append("</g>")
    out.append('<path d="M47 20 A30 30 0 0 0 47 80 Z" fill="%s"/>' % hexc(SUN))
    out.append('<path d="M53 20 A30 30 0 0 1 53 80 Z" fill="%s"/>' % hexc(MOON))
    if detail:
        for cx, cy, r, a in CRATERS:
            out.append('<circle cx="%s" cy="%s" r="%s" fill="%s" opacity="%s"/>' % (cx, cy, r, hexc(DUSK), a))
        if badge is None:
            pts = " ".join("%s,%s" % (fmt(x), fmt(y)) for x, y in STAR)
            out.append('<polygon points="%s" fill="%s"/>' % (pts, hexc(MOON)))
            out.append('<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (DOT[0], DOT[1], DOT[2], hexc(MOON)))
    if badge is None:
        return out
    # Wrap the mark in a mask so the badge sits in a transparent gap.
    mask = (
        '<mask id="badge-gap" maskUnits="userSpaceOnUse" x="0" y="0" width="100" height="100">'
        '<rect width="100" height="100" fill="#fff"/>'
        '<circle cx="%s" cy="%s" r="%s" fill="#000"/></mask>' % (BADGE_X, BADGE_Y, BADGE_R + BADGE_GAP)
    )
    out = ["<defs>%s</defs>" % mask, '<g mask="url(#badge-gap)">'] + ["  " + line for line in out] + ["</g>"]
    out.append('<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (BADGE_X, BADGE_Y, BADGE_R, hexc(BADGE_COLORS[badge])))
    if badge == "playing":
        pts = " ".join("%s,%s" % (fmt(x), fmt(y)) for x, y in PLAY)
        out.append('<polygon points="%s" fill="%s" stroke="%s" stroke-width="2" stroke-linejoin="round"/>'
                   % (pts, hexc(DUSK), hexc(DUSK)))
    elif badge == "pausing":
        out.append('<g stroke="%s" stroke-width="%s" stroke-linecap="round">' % (hexc(DUSK), PAUSE_WIDTH))
        for x1, y1, x2, y2 in PAUSE_BARS:
            out.append('  <line x1="%s" y1="%s" x2="%s" y2="%s"/>' % (x1, y1, x2, y2))
        out.append("</g>")
    elif badge == "locked":
        cx, cy, r, w = LOCK_SHACKLE
        out.append('<path d="M%s %s V%s A%s %s 0 0 1 %s %s V%s" fill="none" stroke="%s" stroke-width="%s"/>'
                   % (fmt(cx - r), fmt(cy + 1), fmt(cy), r, r, fmt(cx + r), fmt(cy), fmt(cy + 1), hexc(DUSK), w))
        x1, y1, x2, y2, rr = LOCK_BODY
        out.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="%s"/>'
                   % (x1, y1, x2 - x1, y2 - y1, rr, hexc(DUSK)))
    return out


def mark_svg(title, detail=True, badge=None):
    """A standalone SVG document of the mark."""
    body = "\n".join("  " + line for line in mark_svg_body(detail, badge))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 100 100">\n'
            "  <!-- Generated by art/logo/render_logo.py from art/logo/solstice.svg. Do not edit by hand. -->\n"
            "  <title>%s</title>\n%s\n</svg>\n" % (title, body))


def tile_svg(title):
    """The macOS app icon source: the mark on a rounded dusk tile with a soft glow."""
    body = "\n".join("    " + line for line in mark_svg_body())
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 100 100">\n'
            "  <!-- Generated by art/logo/render_logo.py from art/logo/solstice.svg. Do not edit by hand. -->\n"
            "  <title>%s</title>\n"
            "  <defs>\n"
            '    <radialGradient id="glow" cx="50" cy="35" r="70" gradientUnits="userSpaceOnUse">\n'
            '      <stop offset="0" stop-color="%s"/>\n'
            '      <stop offset="1" stop-color="%s"/>\n'
            "    </radialGradient>\n"
            "  </defs>\n"
            '  <rect width="100" height="100" rx="20.3" fill="url(#glow)"/>\n'
            '  <g transform="translate(12.5 12.5) scale(0.75)">\n%s\n  </g>\n</svg>\n'
            % (title, hexc(DUSK_GLOW), hexc(DUSK), body))


def draw_mark(size, detail=True, badge=None):
    """The mark on a transparent square of `size` px."""
    big = size * SS
    k = big / 100.0
    im = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    def p(x, y):
        return (x * k, y * k)

    def round_line(draw, x1, y1, x2, y2, width, fill):
        w = width * k
        draw.line([p(x1, y1), p(x2, y2)], fill=fill, width=round(w))
        for x, y in ((x1, y1), (x2, y2)):
            cx, cy = p(x, y)
            draw.ellipse([cx - w / 2, cy - w / 2, cx + w / 2, cy + w / 2], fill=fill)

    for x1, y1, x2, y2 in RAYS:
        round_line(d, x1, y1, x2, y2, RAY_WIDTH, SUN)

    # Sun half (left) and moon half (right), radius 30, 6-unit gap.
    d.pieslice([p(17, 20), p(77, 80)], 90, 270, fill=SUN)
    d.pieslice([p(23, 20), p(83, 80)], 270, 90, fill=MOON)

    if detail:
        crater = Image.new("RGBA", im.size, (0, 0, 0, 0))
        cd = ImageDraw.Draw(crater)
        for cx, cy, r, a in CRATERS:
            cd.ellipse([p(cx - r, cy - r), p(cx + r, cy + r)], fill=DUSK + (round(255 * a),))
        im = Image.alpha_composite(im, crater)
        d = ImageDraw.Draw(im)
        if badge is None:
            d.polygon([p(x, y) for x, y in STAR], fill=MOON)
            d.ellipse([p(DOT[0] - DOT[2], DOT[1] - DOT[2]), p(DOT[0] + DOT[2], DOT[1] + DOT[2])], fill=MOON)

    if badge is not None:
        gap = Image.new("L", im.size, 255)
        rg = BADGE_R + BADGE_GAP
        ImageDraw.Draw(gap).ellipse([p(BADGE_X - rg, BADGE_Y - rg), p(BADGE_X + rg, BADGE_Y + rg)], fill=0)
        im.putalpha(ImageChops.multiply(im.getchannel("A"), gap))
        d = ImageDraw.Draw(im)
        r = BADGE_R
        d.ellipse([p(BADGE_X - r, BADGE_Y - r), p(BADGE_X + r, BADGE_Y + r)], fill=BADGE_COLORS[badge])
        if badge == "playing":
            d.polygon([p(x, y) for x, y in PLAY], fill=DUSK)
            for (x1, y1), (x2, y2) in zip(PLAY, PLAY[1:] + PLAY[:1]):
                round_line(d, x1, y1, x2, y2, 2, DUSK)
        elif badge == "pausing":
            for x1, y1, x2, y2 in PAUSE_BARS:
                round_line(d, x1, y1, x2, y2, PAUSE_WIDTH, DUSK)
        elif badge == "locked":
            cx, cy, sr, w = LOCK_SHACKLE
            outer = sr + w / 2
            d.arc([p(cx - outer, cy - outer), p(cx + outer, cy + outer)], 180, 360, fill=DUSK, width=round(w * k))
            d.rectangle([p(cx - outer, cy), p(cx - sr + w / 2, cy + 1)], fill=DUSK)
            d.rectangle([p(cx + sr - w / 2, cy), p(cx + outer, cy + 1)], fill=DUSK)
            x1, y1, x2, y2, rr = LOCK_BODY
            d.rounded_rectangle([p(x1, y1), p(x2, y2)], radius=rr * k, fill=DUSK)

    return im.resize((size, size), Image.LANCZOS)


def glow_tile(width, height, radius=0):
    """A dusk tile with a soft glow centered in the upper part, optionally with rounded corners."""
    bw, bh = width * SS, height * SS
    tile = Image.new("RGBA", (bw, bh), DUSK + (255,))
    glow = Image.new("L", (bw, bh), 0)
    gd = ImageDraw.Draw(glow)
    cx, cy, rmax = bw * 0.5, bh * 0.35, max(bw, bh) * 0.7
    steps = 64
    for i in range(steps, 0, -1):
        r = rmax * i / steps
        gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=round(255 * (1 - i / steps)))
    tile.paste(Image.new("RGBA", (bw, bh), DUSK_GLOW + (255,)), (0, 0), glow)
    if radius:
        mask = Image.new("L", (bw, bh), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, bw - 1, bh - 1], radius=radius * SS, fill=255)
        tile.putalpha(mask)
    return tile.resize((width, height), Image.LANCZOS)


def app_icon(size, rounded=False):
    """Square app icon: the mark at 75% on a dusk tile."""
    tile = glow_tile(size, size, round(size * 0.203) if rounded else 0)
    mark = draw_mark(round(size * 0.75))
    off = (size - mark.width) // 2
    tile.alpha_composite(mark, (off, off))
    return tile


def wordmark(text_color, width=848, height=208):
    """Mark plus "Solstice" on a transparent background, for the README."""
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    mark = draw_mark(176)
    out.alpha_composite(mark, (8, (height - 176) // 2))
    font = ImageFont.truetype(FONT, 132)
    d = ImageDraw.Draw(out)
    box = d.textbbox((0, 0), "Solstice", font=font)
    x = 8 + 176 + 36
    y = (height - (box[3] - box[1])) // 2 - box[1]
    d.text((x, y), "Solstice", font=font, fill=text_color)
    return out


def poster(width=720, height=1080):
    """Store poster art: the mark and the name on a tall dusk tile."""
    out = glow_tile(width, height)
    mark = draw_mark(round(width * 0.6))
    out.alpha_composite(mark, ((width - mark.width) // 2, round(height * 0.36 - mark.height / 2)))
    font = ImageFont.truetype(FONT, 104)
    d = ImageDraw.Draw(out)
    box = d.textbbox((0, 0), "Solstice", font=font)
    d.text(((width - (box[2] - box[0])) // 2 - box[0], round(height * 0.68)), "Solstice", font=font, fill=TEXT_LIGHT)
    return out.convert("RGB")


def save_ico(path, sizes, badge=None):
    """Write a multi-size .ico, dropping the small details below 64 px."""
    images = [draw_mark(s, detail=s >= 64, badge=badge) for s in sizes]
    images[-1].save(path, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[:-1])


def write(path, text):
    """Write a text file with LF line endings."""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
TRAY_ICO_SIZES = [16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 80, 96, 112, 128, 144, 160, 176, 192,
                  208, 224, 240, 256]


def main():
    # App icon and tray default (vite copies sunshine.svg to images/logo-sunshine.svg).
    write(os.path.join(ROOT, "sunshine.svg"), mark_svg("Solstice"))
    draw_mark(256).save(os.path.join(ROOT, "sunshine.png"))
    save_ico(os.path.join(ROOT, "sunshine.ico"), ICO_SIZES)

    # Web UI logo and favicon.
    draw_mark(16, detail=False).save(os.path.join(WEB_IMG, "logo-sunshine-16.png"))
    draw_mark(45, detail=False).save(os.path.join(WEB_IMG, "logo-sunshine-45.png"))
    save_ico(os.path.join(WEB_IMG, "sunshine.ico"), ICO_SIZES)

    # Tray state icons.
    for state in BADGE_COLORS:
        base = os.path.join(WEB_IMG, "sunshine-%s" % state)
        write(base + ".svg", mark_svg("Solstice (%s)" % state, badge=state))
        draw_mark(256, badge=state).save(base + ".png")
        draw_mark(16, detail=False, badge=state).save(base + "-16.png")
        draw_mark(45, detail=False, badge=state).save(base + "-45.png")
        save_ico(base + ".ico", TRAY_ICO_SIZES, badge=state)

    # macOS app icon.
    write(os.path.join(MACOS, "sunshine-icon.svg"), tile_svg("Solstice macOS app icon"))
    app_icon(1024, rounded=True).save(os.path.join(MACOS, "build", "sunshine.icns"), format="ICNS")

    # Documentation site and store art.
    draw_mark(256).save(os.path.join(ROOT, "gh-pages-template", "assets", "img", "navbar-avatar.png"))
    draw_mark(1080).save(os.path.join(ROOT, "branding", "ms-store", "box-art.png"))
    poster().save(os.path.join(ROOT, "branding", "ms-store", "poster-art.png"))

    # README wordmarks.
    wordmark(TEXT_LIGHT).save(os.path.join(ROOT, "branding", "solstice_logo_dark.png"))
    wordmark(DUSK).save(os.path.join(ROOT, "branding", "solstice_logo_light.png"))
    print("ok")


if __name__ == "__main__":
    main()
