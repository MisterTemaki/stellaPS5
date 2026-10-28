#!/usr/bin/env python3
# Stella PS5: the app's home-screen art, drawn from scratch (no logos, no trademarks, no artwork of real products).
#   make_art.py <sce_sys dir> <font.ttf> [bc7enc] [--icon] [--background]
#     --icon         icon-source.png (1024x1024) and icon0.png (512x512)
#     --background   background-source.png (3840x2160)
#     bc7enc         then pic0.dds / pic1.dds from background-source.png: 3840x2160, BC7 DX10 DDS without mipmaps
#                    (bc7enc_rdo), with the header the home screen takes (as ps5-native-app-boilerplate's
#                    tools/prepare-assets.sh writes it)
# The picture: the late-1970s living-room console look -- a ribbed black top over a walnut woodgrain front -- with a
# generic one-button joystick and a blank cartridge (and, on the background, a keypad controller and a handheld: the
# other systems), all drawn here. The font is the caller's ("make art" passes DejaVu Sans Bold).
# SPDX-License-Identifier: MIT
import os
import shutil
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

out, font_path = sys.argv[1], sys.argv[2]
args = [a for a in sys.argv[3:] if not a.startswith('--')]
bc7enc = args[0] if args else None
want_icon = '--icon' in sys.argv
want_bg = '--background' in sys.argv

ORANGE = (240, 128, 40)
CREAM = (238, 226, 200)
RED = (210, 38, 30)


def woodgrain(w, h, seed=2600):
    """Walnut: long dark grain lines, warped by smooth noise, with a few knots' worth of waviness."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    s = w / 1024.0
    warp = np.zeros((h, w), np.float32)
    for k in range(5):
        fx, fy = rng.uniform(0.5, 3.0) / (w / 2), rng.uniform(2.0, 9.0) / h
        ph = rng.uniform(0, 6.28)
        warp += np.sin(x * fx * 6.28 + np.sin(y * fy * 6.28 + ph) * 0.8 + ph) * (3 + 2 * k) * s
    t = (y + warp) / (3.2 * s)
    grain = 0.5 + 0.5 * np.sin(t + 0.6 * np.sin(t * 0.37))
    fine = 0.5 + 0.5 * np.sin((y + warp * 0.6) / (0.9 * s) + x / (300 * s))
    v = 0.55 * grain ** 3 + 0.25 * fine + 0.2 * rng.random((h, w), dtype=np.float32) * 0.5
    dark, light = np.array([46, 24, 12], np.float32), np.array([128, 74, 38], np.float32)
    rgb = light[None, None, :] * (1 - v[..., None]) + dark[None, None, :] * v[..., None]
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB')


def ribbed_black(w, h, rib):
    """The black top: fine horizontal ribs with a soft sheen."""
    y = np.arange(h, dtype=np.float32)
    rib_v = 0.5 + 0.5 * np.cos(y / rib * 6.28)
    base = 14 + 18 * rib_v ** 6
    sheen = np.linspace(1.25, 0.85, h, dtype=np.float32)
    col = np.clip(base * sheen, 0, 255)
    im = np.repeat(col[:, None], w, axis=1)
    return Image.fromarray(np.stack([im, im, im * 1.05], -1).clip(0, 255).astype(np.uint8), 'RGB')


def shadow_under(layer, box, blur, alpha=150):
    sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse(box, fill=(0, 0, 0, alpha))
    layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(blur)))


def joystick(layer, cx, base_y, size):
    """A generic one-button joystick: a square black base, a red button at a top corner, the stick with its boot."""
    d = ImageDraw.Draw(layer)
    bw, bh = size, size * 0.34
    x0, y0 = cx - bw / 2, base_y - bh
    shadow_under(layer, [x0 - bw * 0.05, base_y - bh * 0.25, x0 + bw * 1.08, base_y + bh * 0.35], size * 0.04)
    d = ImageDraw.Draw(layer)
    # the base: a slanted top face and a front face
    top = [(x0 + bw * 0.08, y0 - bh * 0.55), (x0 + bw * 0.92, y0 - bh * 0.55), (x0 + bw, y0), (x0, y0)]
    d.polygon(top, fill=(40, 40, 44, 255))
    d.rectangle([x0, y0, x0 + bw, base_y], fill=(18, 18, 20, 255))
    d.line([(x0, y0), (x0 + bw, y0)], fill=(90, 90, 96, 255), width=max(2, int(size * 0.012)))
    # the ribbed front, as the console's top
    for i in range(1, 6):
        yy = y0 + bh * i / 6
        d.line([(x0 + bw * 0.04, yy), (x0 + bw * 0.96, yy)], fill=(34, 34, 38, 255), width=max(1, int(size * 0.008)))
    # the button
    bx, by, br = x0 + bw * 0.2, y0 - bh * 0.3, size * 0.075
    d.ellipse([bx - br, by - br * 0.55 + br * 0.25, bx + br, by + br * 0.55 + br * 0.25], fill=(110, 16, 12, 255))
    d.ellipse([bx - br, by - br * 0.55, bx + br, by + br * 0.55], fill=RED + (255,))
    d.ellipse([bx - br * 0.5, by - br * 0.35, bx + br * 0.1, by - br * 0.05], fill=(255, 120, 100, 160))
    # the boot and the stick
    sx, sy = cx + bw * 0.08, y0 - bh * 0.28
    d.ellipse([sx - bw * 0.16, sy - bh * 0.16, sx + bw * 0.16, sy + bh * 0.16], fill=(26, 26, 28, 255))
    d.ellipse([sx - bw * 0.11, sy - bh * 0.3, sx + bw * 0.11, sy + bh * 0.02], fill=(36, 36, 40, 255))
    sw, sh = bw * 0.065, size * 0.78
    tilt = size * 0.06
    d.polygon([(sx - sw / 2, sy - bh * 0.1), (sx + sw / 2, sy - bh * 0.1), (sx + sw / 2 + tilt, sy - sh),
               (sx - sw / 2 + tilt, sy - sh)], fill=(20, 20, 22, 255))
    d.line([(sx - sw * 0.15, sy - bh * 0.1), (sx - sw * 0.15 + tilt, sy - sh)], fill=(95, 95, 102, 255),
           width=max(1, int(sw * 0.22)))
    d.ellipse([sx + tilt - sw * 0.62, sy - sh - sw * 0.5, sx + tilt + sw * 0.62, sy - sh + sw * 0.35],
              fill=(30, 30, 33, 255))


def cartridge(layer, cx, base_y, width, angle=0):
    """A blank cartridge: black ribbed shell, a cream label with orange stripes (no names, no art)."""
    w, h = int(width), int(width * 1.22)
    pad = int(width * 0.2)
    c = Image.new('RGBA', (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    x0, y0, x1, y1 = pad, pad, pad + w, pad + h
    r = w * 0.06
    d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=(22, 22, 24, 255), outline=(70, 70, 76, 255),
                        width=max(2, int(w * 0.012)))
    for i in range(8):
        yy = y0 + h * (0.08 + i * 0.025)
        d.line([(x0 + w * 0.08, yy), (x1 - w * 0.08, yy)], fill=(44, 44, 48, 255), width=max(1, int(w * 0.008)))
    lx0, ly0, lx1, ly1 = x0 + w * 0.12, y0 + h * 0.32, x1 - w * 0.12, y1 - h * 0.1
    d.rectangle([lx0, ly0, lx1, ly1], fill=CREAM + (255,))
    for i, col in enumerate([ORANGE, (220, 90, 30), (180, 50, 30)]):
        yy = ly0 + (ly1 - ly0) * (0.62 + i * 0.09)
        d.rectangle([lx0, yy, lx1, yy + (ly1 - ly0) * 0.06], fill=col + (255,))
    # a little pixel gem on the label
    px = (lx1 - lx0) / 22
    ship = ["....##....", "...####...", "..######..", ".########.", "..######..", "...####...", "....##...."]
    sx0, sy0 = (lx0 + lx1) / 2 - 5 * px, ly0 + (ly1 - ly0) * 0.14
    for j, row in enumerate(ship):
        for i, ch in enumerate(row):
            if ch == '#':
                d.rectangle([sx0 + i * px, sy0 + j * px, sx0 + (i + 1) * px - 1, sy0 + (j + 1) * px - 1],
                            fill=(40, 30, 60, 255))
    sh = Image.new('RGBA', c.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([x0 + w * 0.04, y0 + w * 0.05, x1 + w * 0.04, y1 + w * 0.05], radius=r,
                                         fill=(0, 0, 0, 140))
    sh = sh.filter(ImageFilter.GaussianBlur(w * 0.03))
    sh.alpha_composite(c)
    rot = sh.rotate(angle, resample=Image.BICUBIC, expand=True)
    layer.alpha_composite(rot, (int(cx - rot.width / 2), int(base_y - rot.height + pad * 0.6)))


def keypad_controller(layer, cx, base_y, size):
    """A generic keypad controller: a grey body, a small stick on top, twelve keys."""
    d = ImageDraw.Draw(layer)
    w, h = size * 0.62, size
    x0, y0 = cx - w / 2, base_y - h
    shadow_under(layer, [x0 - w * 0.05, base_y - h * 0.06, x0 + w * 1.1, base_y + h * 0.06], size * 0.03)
    d = ImageDraw.Draw(layer)
    d.rounded_rectangle([x0, y0, x0 + w, base_y], radius=w * 0.12, fill=(52, 52, 56, 255), outline=(96, 96, 104, 255),
                        width=max(2, int(size * 0.008)))
    d.ellipse([cx - w * 0.2, y0 + h * 0.06, cx + w * 0.2, y0 + h * 0.06 + w * 0.4], fill=(24, 24, 26, 255))
    d.ellipse([cx - w * 0.08, y0 + h * 0.06 + w * 0.12, cx + w * 0.08, y0 + h * 0.06 + w * 0.28], fill=(70, 70, 76, 255))
    kw, kh = w * 0.2, h * 0.075
    for r in range(4):
        for c in range(3):
            kx = x0 + w * 0.14 + c * (kw + w * 0.07)
            ky = y0 + h * 0.42 + r * (kh + h * 0.035)
            d.rounded_rectangle([kx, ky, kx + kw, ky + kh], radius=kh * 0.3, fill=(196, 190, 176, 255))
    d.rectangle([x0 - w * 0.06, y0 + h * 0.48, x0, y0 + h * 0.6], fill=ORANGE + (255,))
    d.rectangle([x0 + w, y0 + h * 0.48, x0 + w * 1.06, y0 + h * 0.6], fill=ORANGE + (255,))


def handheld(layer, cx, base_y, size):
    """A generic wide handheld: a grey body, a screen in the middle, a D-pad on the left, two buttons on the right."""
    d = ImageDraw.Draw(layer)
    w, h = size, size * 0.42
    x0, y0 = cx - w / 2, base_y - h
    shadow_under(layer, [x0, base_y - h * 0.12, x0 + w, base_y + h * 0.12], size * 0.03)
    d = ImageDraw.Draw(layer)
    d.rounded_rectangle([x0, y0, x0 + w, base_y], radius=h * 0.3, fill=(60, 60, 66, 255), outline=(110, 110, 118, 255),
                        width=max(2, int(size * 0.006)))
    sx0, sy0, sx1, sy1 = x0 + w * 0.3, y0 + h * 0.16, x0 + w * 0.7, base_y - h * 0.16
    d.rounded_rectangle([sx0 - w * 0.02, sy0 - h * 0.05, sx1 + w * 0.02, sy1 + h * 0.05], radius=h * 0.06,
                        fill=(20, 20, 22, 255))
    g = Image.new('RGBA', (int(sx1 - sx0), int(sy1 - sy0)))
    gd = ImageDraw.Draw(g)
    for yy in range(g.height):
        t = yy / max(1, g.height - 1)
        gd.line([(0, yy), (g.width, yy)], fill=(int(40 + 60 * t), int(70 + 90 * (1 - t)), int(140 - 40 * t), 255))
    layer.alpha_composite(g, (int(sx0), int(sy0)))
    d = ImageDraw.Draw(layer)
    dx, dy, a = x0 + w * 0.15, (y0 + base_y) / 2, h * 0.12
    d.rectangle([dx - a * 1.5, dy - a / 2, dx + a * 1.5, dy + a / 2], fill=(24, 24, 26, 255))
    d.rectangle([dx - a / 2, dy - a * 1.5, dx + a / 2, dy + a * 1.5], fill=(24, 24, 26, 255))
    for i, (bx, by) in enumerate([(x0 + w * 0.82, dy + h * 0.1), (x0 + w * 0.89, dy - h * 0.1)]):
        d.ellipse([bx - a * 0.8, by - a * 0.8, bx + a * 0.8, by + a * 0.8], fill=(28, 28, 30, 255))


def text_outline(d, xy, s, font, fill, outline, width):
    x, y = xy
    for dx in range(-width, width + 1):
        for dy in range(-width, width + 1):
            if dx * dx + dy * dy <= width * width:
                d.text((x + dx, y + dy), s, font=font, fill=outline)
    d.text((x, y), s, font=font, fill=fill)


def console_front(w, h, split):
    """Ribbed black above, woodgrain below, a brushed-metal strip between."""
    hy = int(h * split)
    im = Image.new('RGB', (w, h))
    im.paste(ribbed_black(w, hy, max(4, h / 90)), (0, 0))
    im.paste(woodgrain(w, h - hy), (0, hy))
    d = ImageDraw.Draw(im)
    strip = max(4, int(h * 0.018))
    for i in range(strip):
        v = int(150 + 70 * (1 - abs(i / strip - 0.4)))
        d.line([(0, hy - strip + i), (w, hy - strip + i)], fill=(v, v - 6, v - 20))
    d.line([(0, hy), (w, hy)], fill=(20, 12, 6), width=max(2, h // 400))
    return im


def vignette(im, strength):
    w, h = im.size
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.sqrt(((x - w / 2) / (w / 2)) ** 2 + ((y - h / 2) / (h / 2)) ** 2)
    k = np.clip(1 - strength * np.maximum(0, r - 0.55), 0.35, 1)
    a = np.asarray(im, np.float32) * k[..., None]
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), 'RGB')


def icon(size):
    im = console_front(size, size, 0.6).convert('RGBA')
    layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    cartridge(layer, size * 0.67, size * 0.9, size * 0.36, -8)
    joystick(layer, size * 0.33, size * 0.92, size * 0.4)
    im.alpha_composite(layer)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(font_path, int(size * 0.2))
    t = 'Stella'
    tw = d.textlength(t, font=f)
    text_outline(d, ((size - tw) / 2, size * 0.05), t, f, CREAM + (255,), (10, 8, 6, 255), max(2, size // 110))
    f2 = ImageFont.truetype(font_path, int(size * 0.1))
    t2 = 'PS5'
    tw2 = d.textlength(t2, font=f2)
    text_outline(d, ((size - tw2) / 2, size * 0.29), t2, f2, ORANGE + (255,), (10, 8, 6, 255), max(2, size // 160))
    return vignette(im.convert('RGB'), 0.5)


def background(w, h):
    im = console_front(w, h, 0.58).convert('RGBA')
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    # the right half: the home screen's title and buttons sit on the left
    handheld(layer, w * 0.875, h * 0.75, h * 0.4)
    keypad_controller(layer, w * 0.715, h * 0.84, h * 0.36)
    cartridge(layer, w * 0.5, h * 0.85, h * 0.24, 6)
    joystick(layer, w * 0.615, h * 0.97, h * 0.26)
    im.alpha_composite(layer)
    return vignette(im.convert('RGB'), 0.45)


os.makedirs(out, exist_ok=True)
if want_icon:
    ic = icon(1024)
    ic.save(os.path.join(out, 'icon-source.png'))
    ic.resize((512, 512), Image.LANCZOS).save(os.path.join(out, 'icon0.png'))
if want_bg:
    background(3840, 2160).save(os.path.join(out, 'background-source.png'))
if bc7enc:
    bg = Image.open(os.path.join(out, 'background-source.png')).convert('RGB').resize((3840, 2160), Image.LANCZOS)
    tmp = tempfile.mkdtemp()
    src = os.path.join(tmp, 'bg.png')
    bg.save(src)
    subprocess.run([bc7enc, '-q', '-g', src, os.path.join(tmp, 'bg.dds')], check=True, cwd=tmp)
    # the header the console's home screen takes: mip count 1 (with its flag), depth 1, DX10 alpha mode "straight"
    dds = bytearray(open(os.path.join(tmp, 'bg.dds'), 'rb').read())
    assert dds[:4] == b'DDS ' and dds[84:88] == b'DX10'
    struct.pack_into('<I', dds, 8, struct.unpack_from('<I', dds, 8)[0] | 0x20000)
    struct.pack_into('<I', dds, 24, 1)
    struct.pack_into('<I', dds, 28, 1)
    struct.pack_into('<I', dds, 144, 1)
    for name in ('pic0.dds', 'pic1.dds'):
        open(os.path.join(out, name), 'wb').write(dds)
    shutil.rmtree(tmp)
print('art written to', out)
