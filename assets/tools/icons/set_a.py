"""Volumes, Trash, folders, and documents."""
from core import *  # noqa: F401,F403
import glyphs as gl

REG = {}


def icon(name, cat):
    def wrap(fn):
        REG[name] = (cat, fn)
        return fn
    return wrap


def stamp(ic, rows, cx, cy, col, accent=None):
    w, h = len(rows[0]), len(rows)
    cmap = {'#': col}
    if accent:
        cmap['+'] = accent
    cmap['W'] = W
    ic.glyph(rows, cx - w // 2, cy - h // 2, cmap)


# --- bases ----------------------------------------------------------------

def folder_base(ic, body=FOLD, hi=FOLD_HI, lo=FOLD_LO):
    ic.shadow(3, 28, 30, 28)
    ic.shape(lambda d: (d.rectangle((2, 7, 13, 12), fill=1), d.rectangle((1, 10, 30, 27), fill=1)),
             body, hi, lo)
    ic.line([(2, 12), (29, 12)], hi)
    ic.line([(14, 10), (29, 10)], lo)


def folder(emblem=None, body=FOLD, hi=FOLD_HI, lo=FOLD_LO, dk=FOLD_DK, accent=None):
    def fn(ic):
        folder_base(ic, body, hi, lo)
        if emblem:
            stamp(ic, emblem, 16, 20, dk, accent)
    return fn


def doc_base(ic, page=W, fold=G2):
    ic.shadow(8, 30, 27, 30)
    ic.pgon([(6, 2), (20, 2), (26, 8), (26, 29), (6, 29)], page, None, G3)
    ic.pgon([(20, 2), (20, 8), (26, 8)], fold, None, None)
    ic.line([(20, 2), (26, 8)], K)


def doc(emblem=None, col=G7, accent=None, cy=18):
    def fn(ic):
        doc_base(ic)
        if emblem:
            stamp(ic, emblem, 16, cy, col, accent)
    return fn


def text_lines(ic, x0, y0, rows, cols, col=G5):
    for i, n in enumerate(rows):
        ic.line([(x0, y0 + i * 3), (x0 + n, y0 + i * 3)], cols[i % len(cols)] if cols else col)


# --- volumes --------------------------------------------------------------

@icon("hard_disk", "volume")
def hard_disk(ic):
    ic.shadow(3, 26, 30, 26)
    ic.box(1, 9, 29, 24, G2, W, G5)
    ic.line([(3, 20), (27, 20)], G4)
    ic.line([(4, 22), (18, 22)], G7)
    ic.rect(24, 22, 25, 22, GREEN)
    ic.rect(4, 25, 7, 26, G6)
    ic.rect(23, 25, 26, 26, G6)


@icon("floppy", "volume")
def floppy(ic):
    ic.box(4, 3, 27, 28, G8, G6, G9)
    ic.box(10, 3, 21, 11, G3, W, G5)
    ic.rect(17, 5, 19, 9, G8)
    ic.box(7, 15, 24, 28, W, None, G2)
    text_lines(ic, 9, 18, [13, 10, 12], [BLUE, G4, G4])
    ic.rect(5, 26, 6, 27, K)


@icon("cd_rom", "volume")
def cd_rom(ic):
    ic.oval((2, 2, 29, 29), G1, W, G4)
    for (x, y), col in zip(((8, 8), (9, 7), (7, 10), (22, 22), (21, 23), (23, 20)),
                           (TEAL_HI, PINK, YELLOW, TEAL_HI, PINK, YELLOW)):
        ic.px(x, y, col)
    ic.oval((11, 11, 20, 20), G3, None, None)
    ic.oval((13, 13, 18, 18), W, None, None, outline=G6)


@icon("removable_drive", "volume")
def removable_drive(ic):
    ic.shadow(4, 23, 24, 23)
    ic.box(2, 10, 23, 22, BLUE, BLUE_HI, BLUE_DK)
    ic.box(23, 12, 30, 20, G2, W, G5)
    ic.rect(25, 14, 26, 15, G8)
    ic.rect(25, 17, 26, 18, G8)
    ic.box(5, 13, 17, 19, W, None, G3)


@icon("sd_card", "volume")
def sd_card(ic):
    ic.shadow(9, 30, 26, 30)
    ic.pgon([(7, 2), (21, 2), (25, 6), (25, 29), (7, 29)], G8, G6, G9)
    for x in range(10, 23, 3):
        ic.rect(x, 4, x + 1, 8, YELLOW)
    ic.box(9, 13, 23, 27, W, None, G2)
    stamp(ic, gl.PI, 16, 20, RED_DK)


@icon("network_volume", "volume")
def network_volume(ic):
    ic.shadow(10, 29, 25, 29)
    ic.box(8, 2, 23, 28, G2, W, G5)
    for y in (6, 9, 12):
        ic.line([(11, y), (20, y)], G6)
    ic.rect(19, 24, 20, 25, GREEN)
    stamp(ic, gl.GLOBE, 15, 19, BLUE)


@icon("disk_image", "volume")
def disk_image(ic):
    doc_base(ic)
    ic.box(9, 12, 23, 23, G2, W, G5)
    ic.line([(11, 20), (19, 20)], G7)
    ic.px(21, 20, GREEN)


# --- trash ----------------------------------------------------------------

def can(ic):
    ic.shadow(8, 30, 25, 30)
    ic.pgon([(7, 9), (25, 9), (23, 29), (9, 29)], G2, W, G5)
    for x in (11, 14, 17, 20):
        ic.line([(x, 12), (x + (1 if x > 16 else 0), 26)], G5)
    ic.box(5, 6, 27, 9, G1, W, G4)
    ic.box(13, 3, 19, 6, G3, W, G5)


@icon("trash_empty", "trash")
def trash_empty(ic):
    can(ic)


@icon("trash_full", "trash")
def trash_full(ic):
    ic.pgon([(4, 4), (11, 1), (14, 8), (6, 10)], W, None, G3)
    ic.pgon([(18, 2), (27, 3), (26, 10), (17, 9)], c('ffffcc'), None, YEL_DK)
    ic.pgon([(10, 3), (20, 0), (21, 8), (12, 9)], BLUE_HI, None, BLUE)
    ic.shadow(8, 30, 25, 30)
    ic.pgon([(7, 10), (25, 10), (23, 29), (9, 29)], G2, W, G5)
    for x in (11, 14, 17, 20):
        ic.line([(x, 13), (x + (1 if x > 16 else 0), 26)], G5)
    ic.pgon([(5, 8), (26, 6), (27, 9), (6, 11)], G1, W, G4)


# --- folders --------------------------------------------------------------

FOLDERS = [
    ("folder", None, {}),
    ("system_folder", gl.PI, {}),
    ("applications_folder", gl.APP, {}),
    ("documents_folder", gl.PAGE, {}),
    ("downloads_folder", gl.DOWN, {}),
    ("garden_folder", gl.SPROUT, dict(body=c('99cc99'), hi=c('ccffcc'), lo=c('669966'), dk=GREEN_DK)),
    ("preferences_folder", gl.SLIDERS, {}),
    ("extensions_folder", gl.PUZZLE, {}),
    ("control_panels_folder", gl.SLIDERS, dict(body=c('cccc99'), hi=c('ffffcc'), lo=c('999966'), dk=c('333300'))),
    ("fonts_folder", gl.LETTER_A, {}),
    ("startup_items_folder", gl.POWER, {}),
    ("home_folder", gl.HOUSE, {}),
    ("music_folder", gl.NOTE, {}),
    ("pictures_folder", gl.MOUNTAIN, {}),
    ("movies_folder", gl.FILM, {}),
    ("utilities_folder", gl.WRENCH, {}),
    ("shared_folder", gl.CABLE, {}),
]
for _n, _e, _kw in FOLDERS:
    REG[_n] = ("folder", folder(_e, **_kw))


@icon("folder_open", "folder")
def folder_open(ic):
    ic.shadow(3, 28, 30, 28)
    ic.shape(lambda d: (d.rectangle((2, 7, 13, 12), fill=1), d.rectangle((1, 10, 28, 27), fill=1)),
             FOLD_LO, FOLD, FOLD_DK)
    ic.pgon([(5, 15), (31, 15), (27, 27), (1, 27)], FOLD, FOLD_HI, FOLD_LO)


# --- documents ------------------------------------------------------------

@icon("document", "document")
def document(ic):
    doc_base(ic)


@icon("text_document", "document")
def text_document(ic):
    doc_base(ic)
    text_lines(ic, 9, 11, [13, 14, 11, 14, 12, 8], None)


@icon("rich_text_document", "document")
def rich_text(ic):
    doc_base(ic)
    ic.rect(9, 10, 18, 11, RED)
    text_lines(ic, 9, 14, [13, 14, 11, 14, 9], [BLUE, G5, G5, PURPLE, G5])


@icon("image_document", "document")
def image_document(ic):
    doc_base(ic)
    ic.box(8, 11, 24, 24, BLUE_HI, None, None)
    ic.pgon([(9, 23), (14, 16), (18, 20), (20, 18), (23, 23)], GREEN, GREEN_HI, GREEN_DK, outline=GREEN_DK)
    ic.oval((18, 12, 21, 15), YELLOW, None, None, outline=YEL_DK)


@icon("sound_document", "document")
def sound_document(ic):
    doc_base(ic)
    stamp(ic, gl.NOTE, 15, 16, PURPLE)
    for i, h in enumerate((2, 5, 3, 6, 4, 2, 5, 3)):
        ic.line([(9 + i * 2, 25 - h), (9 + i * 2, 25)], PURPLE_HI if i % 2 else PURPLE)


@icon("movie_document", "document")
def movie_document(ic):
    doc_base(ic)
    ic.box(8, 10, 24, 25, G8, None, None)
    for y in range(11, 25, 3):
        ic.rect(9, y, 10, y + 1, W)
        ic.rect(22, y, 23, y + 1, W)
    ic.box(12, 12, 20, 23, ORANGE, YELLOW, RED_DK)


@icon("portable_document", "document")
def portable_document(ic):
    doc_base(ic)
    text_lines(ic, 9, 10, [13, 14, 11], None)
    ic.box(8, 19, 24, 26, RED, RED_HI, RED_DK)
    ic.line([(11, 22), (21, 22)], W)
    ic.line([(11, 24), (17, 24)], W)


@icon("archive_sit", "document")
def archive_sit(ic):
    doc_base(ic)
    ic.box(8, 10, 24, 25, c('cc99cc'), c('ffccff'), c('996699'))
    ic.rect(15, 10, 17, 25, YELLOW)
    ic.line([(15, 10), (15, 25)], YEL_DK)
    ic.box(7, 15, 25, 19, G3, W, G6)


@icon("archive_zip", "document")
def archive_zip(ic):
    doc_base(ic)
    for y in range(4, 27, 2):
        ic.rect(14, y, 15, y, G6)
        ic.rect(16, y + 1, 17, y + 1, G6)
    ic.box(13, 20, 18, 26, G3, W, G6)


@icon("font_file", "document")
def font_file(ic):
    doc_base(ic)
    ic.glyph(["..###....", ".##.##...", "##...##..", "#######..", "##...##..", "##...##.."],
             9, 12, {'#': K})
    ic.glyph(["....", ".##.", "#..#", "#..#", ".###"], 18, 13, {'#': G7})
    ic.line([(9, 24), (23, 24)], G4)


@icon("preferences_file", "document")
def preferences_file(ic):
    doc_base(ic)
    for i, x in enumerate((10, 15, 20)):
        ic.line([(x + 1, 11), (x + 1, 25)], G5)
        y = (14, 21, 17)[i]
        ic.box(x, y, x + 2, y + 2, BLUE, BLUE_HI, BLUE_DK)


@icon("extension", "system")
def extension(ic):
    ic.shadow(5, 27, 27, 27)
    ic.shape(lambda d: (d.rectangle((3, 8, 25, 26), fill=1), d.ellipse((11, 3, 18, 10), fill=1),
                        d.ellipse((22, 13, 29, 20), fill=1)), PURPLE, PURPLE_HI, PURPLE_DK)
    stamp(ic, gl.PI, 14, 18, W)


@icon("control_panel", "system")
def control_panel(ic):
    ic.shadow(4, 28, 29, 28)
    ic.box(2, 4, 28, 27, G2, W, G5)
    for i, x in enumerate((7, 14, 21)):
        ic.line([(x + 1, 8), (x + 1, 23)], G8)
        y = (11, 18, 14)[i]
        ic.box(x - 1, y, x + 3, y + 3, (RED, GREEN, BLUE)[i], W, G6)


@icon("script_document", "document")
def script_document(ic):
    doc_base(ic, page=c('ffffcc'), fold=c('ffcc99'))
    text_lines(ic, 9, 11, [8, 12, 10, 13, 6], [BROWN, G5, G5, BROWN, G5])
    ic.glyph(["#.", ".#", "#."], 9, 25, {'#': K})
    ic.line([(12, 27), (16, 27)], K)


@icon("source_code", "document")
def source_code(ic):
    doc_base(ic)
    stamp(ic, gl.BRACES, 16, 18, BLUE)
    ic.line([(14, 18), (18, 18)], GREEN_DK)


@icon("web_document", "document")
def web_document(ic):
    doc_base(ic)
    ic.oval((9, 11, 23, 25), BLUE_HI, None, None, outline=BLUE)
    stamp(ic, gl.GLOBE, 16, 18, BLUE)


@icon("stationery", "document")
def stationery(ic):
    ic.pgon([(9, 4), (27, 4), (27, 30), (9, 30)], G2, None, G4)
    ic.pgon([(7, 3), (25, 3), (25, 29), (7, 29)], W, None, G3)
    ic.pgon([(5, 2), (19, 2), (23, 6), (23, 28), (5, 28)], W, None, G3)
    ic.pgon([(19, 2), (19, 6), (23, 6)], G2, None, None)
    ic.line([(19, 2), (23, 6)], K)
    text_lines(ic, 8, 11, [11, 12, 9, 12], None)
