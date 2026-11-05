#!/usr/bin/env python3
"""
sz81 for the Atari - build the resource file sz81.rsc and the header
atari/sz81_rsc.h

    atari/mkrsc.py <out.rsc> <out.h>     write both files
    atari/mkrsc.py --dump <file.rsc>     list what is in a resource file

The texts of the GEM user interface (menu bar, Hardware dialog, alerts,
window titles, the About window) are in SZ81.RSC, next to the program, so
it can be translated with a resource editor: keep the trees, the order of
the objects and the order of the free strings, change the texts. The
writer is the one of the user's CHIP-8 and Pong projects.

Coordinates are in characters; a value can also be given as (chars, pixels),
stored as pixels * 256 + chars. In the Hardware dialog the y values of the
objects in a frame are rows (1 = first); the program sets the pixels
(hw_layout() in atari.c), so that buttons are at least 17 pixels high.
"""

import struct
import sys

# ---------------------------------------------------------------- types

G_BOX, G_TEXT, G_BOXTEXT, G_IMAGE, G_USERDEF, G_IBOX, G_BUTTON, \
    G_BOXCHAR, G_STRING, G_FTEXT, G_FBOXTEXT, G_ICON, G_TITLE = range(20, 33)

NONE = 0x0000
SELECTABLE, DEFAULT, EXIT, EDITABLE, RBUTTON, LASTOB, TOUCHEXIT = \
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040
FL3DBAK, FL3DACT, FLAG11 = 0x0400, 0x0600, 0x0800      # FLAG11: Undo key

NORMAL, SELECTED, DISABLED, OUTLINED, WHITEBAK = 0x00, 0x01, 0x08, 0x10, 0x40
SHADOWED = 0x20

# MagiC object types (CF-Lib emulates them where MagiC is missing); a
# shortcut is the position of the underlined letter
MX_TITLE = WHITEBAK | 0xFF00            # underlined title (G_STRING)
MX_FRAME = WHITEBAK | 0xFE00            # group frame (G_BUTTON)
def MX_SC(pos):                         # exit button / string with shortcut
    return WHITEBAK | (pos << 8)
def MX_CHECK(pos):                      # check box / radio with shortcut
    return WHITEBAK | 0x8000 | (pos << 8)

TEXT_TYPES = (G_BUTTON, G_STRING, G_TITLE)
TED_TYPES = (G_TEXT, G_BOXTEXT, G_FTEXT, G_FBOXTEXT)
IBM, TE_LEFT, TE_CNTR = 3, 0, 2


def ted(text, just=TE_LEFT, color=0x1180, thickness=-1, tmplt='', valid=''):
    return dict(text=text, just=just, color=color, thickness=thickness,
                tmplt=tmplt, valid=valid)


def obj(name, type_, flags, state, spec, x, y, w, h, children=()):
    return dict(name=name, type=type_, flags=flags, state=state, spec=spec,
                x=x, y=y, w=w, h=h, children=list(children))


def string(name, text, x, y, w, state=NORMAL, flags=NONE):
    return obj(name, G_STRING, flags, state, text, x, y, w, 1)



# ---------------------------------------------------------------- menu

def item(name, text, key, y, w, state=NORMAL):
    """A menu entry: two spaces for the check mark, the text, the
    shortcut at the right edge"""
    s = '  %-*s%s ' % (w - 3 - len(key), text, key)
    assert len(s) == w, s
    return string(name, s, 0, y, w, state)


def separator(y, w):
    return string(None, '-' * w, 0, y, w, DISABLED)


DESK_W, FILE_W, VIEW_W, OPT_W = 21, 22, 16, 15


def menu_tree():
    bar_h = (1, 2)
    title_h = (1, 3)
    acc = [item(None, 'Desk Accessory %d' % i, '', 1 + i, DESK_W)
           for i in range(1, 7)]
    return obj('T_MENU', G_IBOX, NONE, NORMAL, 0, 0, 0, 80, 25, [
        obj('M_BAR', G_BOX, NONE, NORMAL, 0x00001100, 0, 0, 80, bar_h, [
            obj('M_ACTIVE', G_IBOX, NONE, NORMAL, 0, 2, 0, 27, title_h, [
                obj('M_T_DESK', G_TITLE, NONE, NORMAL, ' sz81 ', 0, 0, 6, title_h),
                obj('M_T_FILE', G_TITLE, NONE, NORMAL, ' File ', 6, 0, 6, title_h),
                obj('M_T_VIEW', G_TITLE, NONE, NORMAL, ' View ', 12, 0, 6, title_h),
                obj('M_T_OPT', G_TITLE, NONE, NORMAL, ' Options ', 18, 0, 9, title_h),
            ]),
        ]),
        obj('M_SCREEN', G_IBOX, NONE, NORMAL, 0, 0, title_h, 80, 19, [
            obj('M_DESKBOX', G_BOX, NONE, NORMAL, 0x00FF1100, 2, 0, DESK_W, 8, [
                item('M_ABOUT', 'About sz81...', '', 0, DESK_W),
                separator(1, DESK_W),
            ] + acc),
            obj('M_FILEBOX', G_BOX, NONE, NORMAL, 0x00FF1100, 8, 0, FILE_W, 9, [
                item('M_OPEN', 'Open...', '^O', 0, FILE_W),
                separator(1, FILE_W),
                item('M_LOADSTATE', 'Load State...', '', 2, FILE_W),
                item('M_SAVESTATE', 'Save State...', '', 3, FILE_W),
                separator(4, FILE_W),
                item('M_SCREENSHOT', 'Save screenshot', '^T', 5, FILE_W),
                separator(6, FILE_W),
                item('M_RESET', 'Reset...', '', 7, FILE_W),
                item('M_QUIT', 'Quit', '^Q', 8, FILE_W),
            ]),
            obj('M_VIEWBOX', G_BOX, NONE, NORMAL, 0x00FF1100, 14, 0, VIEW_W, 2, [
                item('M_INVERT', 'Invert', '', 0, VIEW_W),
                item('M_KEYBOARD', 'Keyboard', '^K', 1, VIEW_W),
            ]),
            obj('M_OPTBOX', G_BOX, NONE, NORMAL, 0x00FF1100, 20, 0, OPT_W, 1, [
                item('M_HARDWARE', 'Hardware...', '', 0, OPT_W),
            ]),
        ]),
    ])


# ---------------------------------------------------------------- Hardware

RADIO = SELECTABLE | RBUTTON | FL3DBAK
CHECK = SELECTABLE | FL3DBAK
EXITBUT = SELECTABLE | EXIT | FL3DACT
POPBOX = TOUCHEXIT | FL3DBAK
LABEL = TOUCHEXIT                       # label that steps through its popup

# Popup texts: the box shows the chosen text and the program finds the
# entry by comparing, so all texts of a popup have the same length and its
# box is as wide. The entries must stay in this order.
RAM_TEXTS = tuple(' %2dK ' % n for n in (1, 2, 3, 4, 16, 24, 32, 40, 48, 56))
FSKIP_TEXTS = tuple(' %d ' % n for n in range(10))
SPEED_TEXTS = tuple(' %3d%% ' % n for n in (200, 100, 66, 50))
for texts in (RAM_TEXTS, FSKIP_TEXTS, SPEED_TEXTS):
    assert len(set(map(len, texts))) == 1


def hardware_tree():
    # Object 1 is a MagiC title: CF-Lib makes it the window name. The
    # radio buttons of a group follow each other (the program counts on it)
    return obj('T_HARDWARE', G_BOX, FL3DBAK, NORMAL, 0x00001100, 0, 0, 38, 14, [
        obj('HW_TITLE', G_STRING, FL3DBAK, MX_TITLE, 'Hardware', 1, 0, 36, (1, 2)),
        obj('HW_FMACH', G_BUTTON, NONE, MX_FRAME, ' Machine ', 1, 1, 36, 2, [
            obj('HW_ZX80', G_BUTTON, RADIO, MX_CHECK(5), 'ZX80 4K', 1, 1, 10, 1),
            obj('HW_ZX80_8K', G_BUTTON, RADIO, MX_CHECK(5), 'ZX80 8K', 12, 1, 10, 1),
            obj('HW_ZX81', G_BUTTON, RADIO, MX_CHECK(3), 'ZX81', 23, 1, 8, 1),
            obj('HW_RAMLBL', G_STRING, LABEL, MX_SC(0), 'RAM size', 1, 2, 8, 1),
            obj('HW_RAM', G_BOXTEXT, POPBOX, SHADOWED, ted(RAM_TEXTS[0]), 10, 2, len(RAM_TEXTS[0]), 1),
            obj('HW_M1NOT', G_BUTTON, CHECK, MX_CHECK(0), 'M1NOT', 23, 2, 9, 1),
        ]),
        obj('HW_FSPEED', G_BUTTON, NONE, MX_FRAME, ' Speed ', 1, 2, 36, 2, [
            obj('HW_FSKLBL', G_STRING, LABEL, MX_SC(0), 'Frameskip', 1, 1, 9, 1),
            obj('HW_FSKIP', G_BOXTEXT, POPBOX, SHADOWED, ted(FSKIP_TEXTS[0]), 11, 1, len(FSKIP_TEXTS[0]), 1),
            obj('HW_SPDLBL', G_STRING, LABEL, MX_SC(0), 'Emu speed', 18, 1, 9, 1),
            obj('HW_SPEED', G_BOXTEXT, POPBOX, SHADOWED, ted(SPEED_TEXTS[0]), 28, 1, len(SPEED_TEXTS[0]), 1),
        ]),
        obj('HW_FGFX', G_BUTTON, NONE, MX_FRAME, ' Graphics ', 1, 3, 36, 2, [
            obj('HW_WRX', G_BUTTON, CHECK, MX_CHECK(0), 'WRX hi-res', 1, 1, 13, 1),
            obj('HW_UDGNONE', G_BUTTON, RADIO, MX_CHECK(0), 'No UDG', 1, 2, 10, 1),
            obj('HW_UDG64', G_BUTTON, RADIO, MX_CHECK(4), 'UDG 64', 12, 2, 10, 1),
            obj('HW_UDG128', G_BUTTON, RADIO, MX_CHECK(5), 'UDG 128', 23, 2, 11, 1),
        ]),
        obj('HW_CANCEL', G_BUTTON, EXITBUT | FLAG11, MX_SC(0), 'Cancel', 17, 4, 9, 1),
        obj('HW_OK', G_BUTTON, EXITBUT | DEFAULT, MX_SC(0), 'OK', 27, 4, 9, 1),
    ])


def popup_tree(name, texts):
    w = len(texts[0])
    return obj(name, G_BOX, FL3DBAK, SHADOWED, 0x00FF1100, 0, 0, w, len(texts),
               [string(None, t, 0, i, w, flags=SELECTABLE) for i, t in enumerate(texts)])


# ---------------------------------------------------------------- free strings

# The About window's lines: FS_AB_LINE0 and 1 are followed by the version
# numbers, FS_AB_LINE4 follows the sz81 version; an empty line is half high.
FREE_STRINGS = [
    ('FS_APPNAME', '  sz81 Emulator'),
    ('FS_KB_ZX80', ' ZX80 Keyboard '),
    ('FS_KB_ZX81', ' ZX81 Keyboard '),
    ('FS_AB_TITLE', ' About sz81 '),
    ('FS_AB_OK', 'OK'),
    ('FS_AB_LINE0', 'Version '),
    ('FS_AB_LINE1', 'ST version '),
    ('FS_AB_LINE2', ''),
    ('FS_AB_LINE3', 'sz81 by Thunor and Chris Young,'),
    ('FS_AB_LINE4', ' updates by ikjordan,'),
    ('FS_AB_LINE5', 'based on z81 by Russell Marks.'),
    ('FS_AB_LINE6', 'ZX80 emulation and keyboard images'),
    ('FS_AB_LINE7', 'from EightyOne by Michael D Wynne.'),
    ('FS_AB_LINE8', ''),
    ('FS_AB_LINE9', 'ST port by Mia Jaap (mia-jaap.de)'),
    ('AL_NOVDI', '[3][Cannot open a VDI workstation.][  OK  ]'),
    ('AL_NOWINDOW', '[3][No more windows available.][  OK  ]'),
    ('AL_NOKBPIC', '[3][Cannot load the keyboard picture|data\\kbzx80.bmp or kbzx81.bmp.][  OK  ]'),
    ('AL_NOABPIC', '[3][Cannot load the picture|data\\zx81.bmp.][  OK  ]'),
    ('AL_NOMEM', '[3][Out of memory.][  OK  ]'),
    ('AL_RESET', '[2][Reset the emulated machine?|The program in memory is lost.][Cancel|Reset]'),
    ('AL_HWRESET', '[2][The new hardware resets the|emulated machine. The|program in memory is lost.][Cancel|Reset]'),
    ('AL_SZ81DIALOG', "[1][Please close sz81's own|dialog first.][  OK  ]"),
]

TREES = [menu_tree(), hardware_tree(),
         popup_tree('T_RAMPOP', RAM_TEXTS),
         popup_tree('T_FSKIPPOP', FSKIP_TEXTS),
         popup_tree('T_SPEEDPOP', SPEED_TEXTS)]


# ---------------------------------------------------------------- writer

def flatten(root):
    """Objects of a tree in resource order (depth first), with links."""
    out = []

    def add(o):
        i = len(out)
        out.append(o)
        o['next'] = o['head'] = o['tail'] = -1
        prev = None
        for c in o['children']:
            j = add(c)
            if prev is None:
                o['head'] = j
            else:
                out[prev]['next'] = j
            prev = j
            out[j]['_parent'] = i
        if prev is not None:
            o['tail'] = prev
            out[prev]['next'] = i
        return i

    add(root)
    root['next'] = -1
    out[-1]['flags'] |= LASTOB
    return out


def coord(v):
    if isinstance(v, tuple):
        chars, pixels = v
        return (pixels & 0xFF) << 8 | (chars & 0xFF)
    return v & 0xFFFF


class Strings:
    def __init__(self):
        self.data = bytearray()
        self.where = {}

    def add(self, s):
        b = s.encode('latin-1') + b'\0'
        off = len(self.data)
        self.data += b
        return off


def build():
    hdr = 36
    strings = Strings()
    trees = [flatten(t) for t in TREES]

    # strings of the objects and TEDINFOs, then the free strings
    teds = []
    for objs in trees:
        for o in objs:
            if o['type'] in TEXT_TYPES:
                o['_str'] = strings.add(o['spec'])
            elif o['type'] in TED_TYPES:
                t = dict(o['spec'])
                t['_text'] = strings.add(t['text'])
                t['_empty'] = strings.add('')
                t['_tmplt'] = strings.add(t.get('tmplt', ''))
                t['_valid'] = strings.add(t.get('valid', ''))
                o['_ted'] = len(teds)
                teds.append(t)
    free = [strings.add(s) for _, s in FREE_STRINGS]
    if len(strings.data) & 1:
        strings.data += b'\0'

    str_at = hdr
    frstr_at = str_at + len(strings.data)
    ted_at = frstr_at + 4 * len(free)
    obj_at = ted_at + 28 * len(teds)
    nobs = sum(len(t) for t in trees)
    trindex_at = obj_at + 24 * nobs
    size = trindex_at + 4 * len(trees)

    out = bytearray()
    out += struct.pack('>18H',
                       0,                   # rsh_vrsn
                       obj_at,              # rsh_object
                       ted_at,              # rsh_tedinfo
                       obj_at,              # rsh_iconblk (none)
                       obj_at,              # rsh_bitblk (none)
                       frstr_at,            # rsh_frstr
                       str_at,              # rsh_string
                       obj_at,              # rsh_imdata (none)
                       obj_at,              # rsh_frimg (none)
                       trindex_at,          # rsh_trindex
                       nobs,                # rsh_nobs
                       len(trees),          # rsh_ntree
                       len(teds), 0, 0,     # rsh_nted, rsh_nib, rsh_nbb
                       len(free),           # rsh_nstring
                       0,                   # rsh_nimages
                       size)                # rsh_rssize
    out += strings.data
    for f in free:
        out += struct.pack('>L', str_at + f)
    for t in teds:
        out += struct.pack('>LLLhhhHhhhh',
                           str_at + t['_text'], str_at + t['_tmplt'],
                           str_at + t['_valid'], IBM, 0, t['just'],
                           t['color'], 0, t['thickness'],
                           len(t['text']) + 1, len(t.get('tmplt', '')) + 1)
    roots = []
    for objs in trees:
        roots.append(len(out))
        for o in objs:
            if o['type'] in TEXT_TYPES:
                spec = str_at + o['_str']
            elif o['type'] in TED_TYPES:
                spec = ted_at + 28 * o['_ted']
            else:
                spec = o['spec']
            out += struct.pack('>hhhHHHLHHHH', o['next'], o['head'], o['tail'],
                               o['type'], o['flags'], o['state'] & 0xFFFF,
                               spec & 0xFFFFFFFF, coord(o['x']), coord(o['y']),
                               coord(o['w']), coord(o['h']))
    for r in roots:
        out += struct.pack('>L', r)
    assert len(out) == size
    return bytes(out), trees


def header(trees):
    lines = ['/* sz81_rsc.h - written by atari/mkrsc.py, do not edit */', '',
             '#ifndef SZ81_RSC_H', '#define SZ81_RSC_H', '']
    for t, objs in enumerate(trees):
        lines.append('#define %-16s %d    /* tree */' % (objs[0]['name'], t))
        for i, o in enumerate(objs[1:], 1):
            if o['name']:
                lines.append('#define %-16s %d' % (o['name'], i))
        lines.append('')
    lines.append('/* free strings */')
    for i, (name, _) in enumerate(FREE_STRINGS):
        lines.append('#define %-16s %d' % (name, i))
    lines += ['', '#endif', '']
    return '\n'.join(lines)


# ---------------------------------------------------------------- dump

def dump(path):
    d = open(path, 'rb').read()
    h = struct.unpack('>18H', d[:36])
    names = ('vrsn object tedinfo iconblk bitblk frstr string imdata frimg '
             'trindex nobs ntree nted nib nbb nstring nimages rssize').split()
    print(' '.join('%s=%d' % kv for kv in zip(names, h)))
    obj_at, frstr_at, trindex_at = h[1], h[5], h[9]

    def cstr(off):
        return d[off:d.index(b'\0', off)].decode('latin-1')

    for t in range(h[11]):
        root, = struct.unpack('>L', d[trindex_at + 4 * t:trindex_at + 4 * t + 4])
        print('tree %d' % t)
        i = 0
        while True:
            o = struct.unpack('>hhhHHHLHHHH', d[root + 24 * i:root + 24 * i + 24])
            nxt, head, tail, typ, flags, state, spec, x, y, w, hh = o
            if typ in TEXT_TYPES:
                text = repr(cstr(spec))
            elif typ in TED_TYPES:
                text = 'ted ' + repr(cstr(struct.unpack('>L', d[spec:spec + 4])[0]))
            else:
                text = '$%08X' % spec
            print('  %2d n%3d h%3d t%3d type %2d fl $%04X st $%04X %-24s %04X %04X %04X %04X'
                  % (i, nxt, head, tail, typ, flags, state, text, x, y, w, hh))
            if flags & LASTOB:
                break
            i += 1
    for i in range(h[15]):
        off, = struct.unpack('>L', d[frstr_at + 4 * i:frstr_at + 4 * i + 4])
        print('string %d: %r' % (i, cstr(off)))



def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--dump':
        dump(sys.argv[2])
    elif len(sys.argv) == 3:
        data, trees = build()
        open(sys.argv[1], 'wb').write(data)
        open(sys.argv[2], 'w').write(header(trees))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
