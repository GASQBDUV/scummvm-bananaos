#!/usr/bin/env python3
"""Builds the "Banana Midnight" ScummVM theme (scummbanana.zip) from ScummVM's own scummremastered theme.

    python3 make-theme.py <scummremastered.zip> <dir with Inter-Regular.ttf and Inter-Bold.ttf> <output.zip>

The colours, the 2 px edges and the yellow focus come from Banana Player's "Midnight" theme (docs/design/midnight.md in
tonywei92/banana-player). Needs Pillow. scummremastered.zip is built with ScummVM (gui/themes/); Inter is SIL OFL 1.1."""
import re, shutil, os, sys, tempfile, zipfile
from PIL import Image

if len(sys.argv) != 4:
    sys.exit(__doc__)
SRC_ZIP, FONTS, OUT = sys.argv[1:4]
DST = tempfile.mkdtemp(prefix="scummbanana-")
with zipfile.ZipFile(SRC_ZIP) as z:
    z.extractall(DST)

# Midnight palette from banana-player docs/design/midnight.md
pal = {
    'background': '0, 0, 0',
    'thumb_color': '18, 17, 22',
    'griditem_highlight_color': '38, 36, 46',
    'button_idle': '38, 36, 46',
    'button_hover': '62, 59, 74',
    'button_pressed': '80, 76, 92',
    'button_disabled': '30, 29, 36',
    'dialog_background': '18, 17, 22',
    'blandyellow': '30, 29, 36',
    'highlight': '255, 210, 63',
    'paleyellow': '30, 29, 36',
    'white': '96, 93, 96',
    'shadowcolor': '143, 138, 128',
    'darkgray': '62, 59, 74',
    'darkgray2': '143, 138, 128',
    'lightgray': '38, 36, 46',
    'lightgray2': '30, 29, 36',
}
p = os.path.join(DST, 'remastered_gfx.stx')
s = open(p, encoding='utf-8').read()
for k, v in pal.items():
    s, n = re.subn(r"(<color name = '%s'\s+rgb = ')[^']*(')" % k, r"\g<1>%s\g<2>" % v, s)
    assert n == 1, k

# text colours
tc = {
    'color_normal': '232, 226, 211',
    'color_normal_inverted': '0, 0, 0',
    'color_normal_hover': '255, 210, 63',
    'color_normal_disabled': '143, 138, 128',
    'color_alternative': '171, 165, 151',
    'color_button_hover': '255, 210, 63',
    'color_alternative_inverted': '0, 0, 0',
    'color_alternative_hover': '255, 210, 63',
    'color_alternative_disabled': '143, 138, 128',
    'color_button': '232, 226, 211',
    'color_button_disabled': '143, 138, 128',
    'color_override': '255, 210, 63',
    'color_override_inverted': '0, 0, 0',
    'color_override_hover': '255, 230, 138',
    'color_override_disabled': '143, 138, 128',
}
for k, v in tc.items():
    s, n = re.subn(r"(<text_color\s+id = '%s'\s+color = ')[^']*(')" % k, r"\g<1>%s\g<2>" % v, s)
    assert n == 1, k

# literal tab colours
s = s.replace("'248, 232, 168'", "'62, 59, 74'").replace("'248,232,168'", "'62,59,74'")
s = s.replace("'239, 202, 109'", "'30, 29, 36'").replace("'239,202,109'", "'30,29,36'")

# fonts: Inter instead of Liberation Sans / bitmap fonts
s = s.replace("<language id = '*'   scalable_file = 'LiberationSans-Bold.ttf'/>", "<language id = '*'   scalable_file = 'Inter-Bold.ttf'/>")
s = s.replace("<language id = '*'   scalable_file = 'LiberationSans-Regular.ttf'/>", "<language id = '*'   scalable_file = 'Inter-Regular.ttf'/>")
open(p, 'w', encoding='utf-8').write(s)

for f in ('Inter-Bold.ttf', 'Inter-Regular.ttf'):
    shutil.copy(os.path.join(FONTS, f), DST)

# THEMERC
t = open(os.path.join(DST, 'THEMERC')).read()
t = t.replace('ScummVM Modern Theme Remastered:No Author', 'Banana Midnight:Banana')
open(os.path.join(DST, 'THEMERC'), 'w').write(t)

# recolor bitmaps: light glyphs stay, light-cream backgrounds become the dark surface
SURF = (18, 17, 22)
CREAM = (232, 226, 211)
for f in os.listdir(DST):
    if not f.endswith('.bmp') or f.startswith(('logo', 'cursor')):
        continue
    im = Image.open(os.path.join(DST, f)).convert('RGB')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = px[x, y]
            if (r, g, b) == (255, 0, 255):
                continue
            # cream background of the old dialog: r>g>b and bluish-yellow
            if r > 235 and g > 225 and b < 225 and r - b > 25:
                px[x, y] = SURF
    im.save(os.path.join(DST, f))

s = open(p, encoding='utf-8').read()
s = s.replace("fg_color = '232, 180, 80'", "fg_color = '18, 17, 22'")
s = s.replace("fg_color = '123, 112, 56'", "fg_color = '96, 93, 96'")
s = re.sub(r"(id = 'slider_full'.*?gradient_start = ')button_idle('\s*gradient_end = ')button_idle", r"\g<1>255, 210, 63\g<2>255, 210, 63", s, count=1, flags=re.S)
open(p, 'w', encoding='utf-8').write(s)
Y = (255, 210, 63)
for f in ('checkbox.bmp', 'radiobutton.bmp'):
    im = Image.open(os.path.join(DST, f)).convert('RGB'); px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = px[x, y]
            if (r, g, b) == (255, 0, 255): continue
            if r > 200 and g > 200 and b > 200:
                px[x, y] = (0, 0, 0)
            elif r > g + 20:
                t = min(1.0, (r - g) / 103.0)
                px[x, y] = tuple(int(SURF[i] * (1 - t) + Y[i] * t) for i in range(3))
    im.save(os.path.join(DST, f))

# ScummVM renders the .svg versions at this resolution, so recolor them too
for f in os.listdir(DST):
    if not f.endswith('.svg') or f.startswith(('logo', 'cursor')):
        continue
    t = open(os.path.join(DST, f), encoding='utf-8').read()
    t = t.replace('#831c00', '#ffd23f').replace('#fbf1ce', '#121116')
    if f in ('checkbox.svg', 'radiobutton.svg'):
        t = t.replace('fill:#fff', 'fill:#000').replace('#ffffff', '#000000')
    if f.endswith('_empty.svg'):
        t = t.replace('fill:#121116', 'fill:#26242e').replace('stroke:#000', 'stroke:#605d60')
    open(os.path.join(DST, f), 'w', encoding='utf-8').write(t)

# yellow mouse cursor: red fill -> accent yellow, keep the grey/white outline
for f in os.listdir(DST):
    if f.startswith('cursor'):
        fp = os.path.join(DST, f)
        if f.endswith('.svg'):
            t = open(fp, encoding='utf-8').read().replace('#ef0000', '#ffd23f').replace('#d40000', '#ffd23f')
            open(fp, 'w', encoding='utf-8').write(t)
        elif f.endswith('.bmp'):
            im = Image.open(fp).convert('RGB'); px = im.load()
            for y in range(im.height):
                for x in range(im.width):
                    r, g, b = px[x, y]
                    if (r, g, b) == (255, 0, 255) or r - max(g, b) < 25:
                        continue
                    t = min(1.0, (r - max(g, b)) / 200.0)
                    base = (g + b) // 2
                    px[x, y] = tuple(int(base * (1 - t) + c * t) for c in (255, 210, 63))
            im.save(fp)

# Midnight edges: grey outline on buttons and dialogs, yellow outline + yellow text when focused
s = open(p, encoding='utf-8').read()
def block(sid, fn):
    global s
    m = re.search(r"<drawdata id = '%s' cache = 'false'>.*?</drawdata>" % sid, s, flags=re.S)
    assert m, sid
    s = s[:m.start()] + fn(m.group(0)) + s[m.end():]
block('button_idle', lambda b: b.replace("stroke = '0'", "stroke = '2'").replace("'darkredborder'", "'96, 93, 96'"))
block('button_hover', lambda b: b.replace("stroke = '0'", "stroke = '2'").replace("'darkredborder'", "'255, 210, 63'").replace("'color_button'", "'color_button_hover'"))
block('default_bg', lambda b: b.replace("radius = '6'", "radius = '8'").replace("stroke = '0'", "stroke = '2'\n\t\t\t\t\tfg_color = '82, 80, 79'"))
open(p, 'w', encoding='utf-8').write(s)

with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(DST)):
        z.write(os.path.join(DST, f), f)
shutil.rmtree(DST, ignore_errors=True)
print('wrote', OUT)
