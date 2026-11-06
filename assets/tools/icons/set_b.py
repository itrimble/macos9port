"""Applications, hardware, alerts, overlays, and logos."""
from core import *  # noqa: F401,F403
import glyphs as gl
from set_a import stamp, doc_base

REG = {}


def icon(name, cat):
    def wrap(fn):
        REG[name] = (cat, fn)
        return fn
    return wrap


def window(ic, x0, y0, x1, y1, body=W, title=G2):
    ic.box(x0, y0, x1, y1, body, None, G3)
    ic.rect(x0 + 1, y0 + 1, x1 - 1, y0 + 4, title)
    for y in (y0 + 2, y0 + 3):
        ic.line([(x0 + 2, y), (x1 - 2, y)], G4 if y % 2 else G1)
    ic.line([(x0, y0 + 5), (x1, y0 + 5)], K)


# --- applications ---------------------------------------------------------

@icon("application", "app")
def application(ic):
    ic.shadow(4, 29, 28, 29)
    ic.pgon([(16, 2), (29, 15), (16, 28), (3, 15)], FOLD, FOLD_HI, FOLD_LO)
    ic.pgon([(16, 8), (23, 15), (16, 22), (9, 15)], W, None, G3)
    stamp(ic, gl.PI, 16, 15, FOLD_DK)


@icon("ospi_desktop", "app")
def ospi_desktop(ic):
    ic.shadow(4, 29, 30, 29)
    window(ic, 7, 2, 29, 20, body=c('6699cc'))
    window(ic, 2, 9, 24, 28, body=c('6666cc'))
    ic.oval((7, 15, 19, 27), W, None, G3)
    stamp(ic, gl.PI, 13, 21, BLUE_DK)


@icon("text_editor", "app")
def text_editor(ic):
    doc_base(ic)
    for i, n in enumerate((13, 14, 10, 12)):
        ic.line([(9, 11 + i * 3), (9 + n, 11 + i * 3)], G5)
    ic.pgon([(14, 28), (27, 13), (30, 16), (17, 31)], YELLOW, c('ffff99'), YEL_DK)
    ic.pgon([(14, 28), (16, 26), (17, 31)], TAN, None, None)
    ic.px(14, 30, K)
    ic.pgon([(27, 13), (28, 12), (31, 15), (30, 16)], PINK, None, None)


@icon("calculator", "app")
def calculator(ic):
    ic.shadow(7, 30, 27, 30)
    ic.box(5, 2, 26, 29, G2, W, G5)
    ic.box(8, 5, 23, 10, c('99cc99'), None, None)
    ic.glyph(["###.#", "..#.#", "###.#"], 17, 6, {'#': K})
    for r in range(4):
        for k in range(3):
            col = ORANGE if (k == 2) else G4
            ic.box(8 + k * 6, 13 + r * 4, 11 + k * 6, 15 + r * 4, col, W, None)


@icon("clock", "app")
def clock(ic):
    ic.shadow(5, 29, 28, 29)
    ic.oval((2, 2, 29, 29), FOLD, FOLD_HI, FOLD_LO)
    ic.oval((5, 5, 26, 26), W, None, G2)
    for x, y in ((15, 6), (15, 24), (6, 15), (24, 15)):
        ic.rect(x, y, x + 1, y + 1, K)
    ic.line([(15, 15), (15, 9)], K, 2)
    ic.line([(16, 16), (21, 19)], K, 2)
    ic.px(15, 16, RED)


@icon("terminal", "app")
def terminal(ic):
    ic.shadow(4, 28, 30, 28)
    ic.box(2, 4, 29, 27, G2, W, G5)
    ic.box(4, 6, 27, 23, K, None, None)
    ic.glyph(["#..", ".#.", "#.."], 6, 9, {'#': GREEN})
    ic.line([(10, 11), (14, 11)], GREEN)
    ic.rect(6, 15, 8, 16, GREEN_HI)
    ic.rect(22, 25, 26, 25, G5)


@icon("settings", "app")
def settings(ic):
    ic.shadow(5, 29, 28, 29)
    teeth = [(14, 2, 17, 6), (14, 25, 17, 29), (2, 14, 6, 17), (25, 14, 29, 17),
             (5, 5, 9, 9), (22, 22, 26, 26), (22, 5, 26, 9), (5, 22, 9, 26)]

    def gear(d):
        d.ellipse((5, 5, 26, 26), fill=1)
        for b in teeth:
            d.rectangle(b, fill=1)
    ic.shape(gear, G3, W, G6)
    ic.oval((11, 11, 20, 20), FOLD, FOLD_HI, FOLD_LO)


@icon("help", "app")
def help_app(ic):
    ic.shadow(5, 29, 28, 29)
    ic.oval((2, 2, 29, 29), BLUE, BLUE_HI, BLUE_DK)
    ic.glyph([".####.", "##..##", "....##", "...##.", "..##..", "..##..", "......", "..##..",
              "..##.."], 13, 9, {'#': W})


@icon("garden_installer", "app")
def garden_installer(ic):
    ic.shadow(6, 29, 27, 29)
    ic.pgon([(5, 18), (27, 18), (24, 28), (8, 28)], BROWN_HI, TAN, BROWN)
    ic.box(4, 16, 28, 19, BROWN, BROWN_HI, None)
    stamp(ic, gl.SPROUT, 16, 10, GREEN_DK)
    ic.glyph(["..#..", ".###.", "#####"], 14, 21, {'#': W})


@icon("music_player", "app")
def music_player(ic):
    ic.shadow(5, 29, 28, 29)
    ic.oval((2, 2, 29, 29), PURPLE, PURPLE_HI, PURPLE_DK)
    ic.oval((7, 7, 24, 24), W, None, G3)
    stamp(ic, gl.NOTE, 16, 16, PURPLE_DK)


@icon("photo_viewer", "app")
def photo_viewer(ic):
    ic.pgon([(6, 3), (28, 6), (25, 27), (3, 24)], W, None, G3)
    ic.box(5, 6, 26, 25, W, None, G3)
    ic.box(7, 8, 24, 20, BLUE_HI, None, None, outline=G6)
    ic.pgon([(8, 19), (13, 12), (17, 17), (19, 15), (23, 19)], GREEN, GREEN_HI, GREEN_DK, outline=GREEN_DK)
    ic.oval((18, 9, 21, 12), YELLOW, None, None, outline=YEL_DK)


@icon("web_browser", "app")
def web_browser(ic):
    ic.shadow(5, 29, 28, 29)
    ic.oval((2, 2, 29, 29), BLUE_HI, W, BLUE)
    ic.line([(16, 3), (16, 28)], BLUE)
    ic.line([(3, 16), (28, 16)], BLUE)
    ic.ellipse((9, 3, 22, 28), None, outline=BLUE)
    ic.line([(5, 9), (26, 9)], BLUE)
    ic.line([(5, 22), (26, 22)], BLUE)
    ic.pgon([(17, 12), (27, 22), (22, 22), (24, 28), (22, 29), (20, 23), (17, 26)], W, None, G3)


@icon("mail", "app")
def mail(ic):
    ic.shadow(4, 26, 30, 26)
    ic.box(2, 7, 29, 24, W, None, G3)
    ic.line([(2, 7), (16, 17), (29, 7)], K)
    ic.line([(2, 24), (12, 15)], G4)
    ic.line([(29, 24), (20, 15)], G4)
    ic.box(23, 9, 27, 13, RED, RED_HI, RED_DK)


@icon("chat", "app")
def chat(ic):
    ic.box(2, 3, 21, 16, BLUE_HI, W, BLUE, r=3)
    ic.pgon([(6, 15), (6, 20), (11, 15)], BLUE_HI, None, None)
    ic.box(11, 12, 29, 24, G1, W, G4, r=3)
    ic.pgon([(24, 23), (26, 28), (20, 23)], G1, None, None)
    for x in (16, 20, 24):
        ic.rect(x - 1, 17, x, 18, G7)


@icon("calendar", "app")
def calendar(ic):
    ic.shadow(5, 29, 29, 29)
    ic.box(3, 4, 28, 28, W, None, G3)
    ic.rect(4, 5, 27, 10, RED)
    for x in (8, 23):
        ic.rect(x, 2, x + 1, 7, G7)
    for r in range(3):
        for k in range(5):
            ic.rect(6 + k * 4, 13 + r * 5, 8 + k * 4, 15 + r * 5, G4 if (r, k) != (1, 2) else RED)


@icon("notes", "app")
def notes(ic):
    ic.shadow(6, 29, 28, 29)
    ic.box(4, 3, 26, 28, c('ffff99'), W, YELLOW)
    ic.rect(5, 4, 25, 7, YELLOW)
    for y in (12, 16, 20, 24):
        ic.line([(7, y), (23, y)], YEL_DK)


@icon("puzzle_game", "app")
def puzzle_game(ic):
    cols = ((RED, RED_HI, RED_DK), (BLUE, BLUE_HI, BLUE_DK),
            (GREEN, GREEN_HI, GREEN_DK), (ORANGE, YELLOW, BROWN))
    for i, (x, y) in enumerate(((3, 3), (16, 3), (3, 16), (16, 16))):
        ic.box(x, y, x + 12, y + 12, *cols[i])
    stamp(ic, gl.PI, 16, 16, W)


@icon("paint", "app")
def paint(ic):
    ic.shadow(5, 27, 27, 27)
    ic.shape(lambda d: (d.ellipse((2, 5, 29, 26), fill=1)), TAN, c('ffffcc'), BROWN_HI)
    ic.oval((18, 17, 23, 22), (0, 0, 0, 0), None, None)
    for (x, y), col in zip(((7, 10), (13, 8), (19, 9), (8, 17)), (RED, BLUE, GREEN, YELLOW)):
        ic.oval((x, y, x + 3, y + 3), col, None, None)
    ic.line([(13, 25), (25, 13)], BROWN, 2)
    ic.line([(24, 14), (28, 10)], G7, 2)


@icon("scrapbook", "app")
def scrapbook(ic):
    ic.shadow(5, 29, 29, 29)
    ic.box(3, 3, 28, 28, c('996699'), c('cc99cc'), PURPLE_DK)
    ic.box(8, 7, 24, 20, W, None, G3)
    ic.pgon([(9, 19), (13, 13), (16, 17), (19, 14), (23, 19)], GREEN, None, None, outline=GREEN_DK)
    for x in (4, 6):
        ic.line([(x, 4), (x, 27)], PURPLE_DK)


@icon("archive_utility", "app")
def archive_utility(ic):
    ic.shadow(5, 29, 29, 29)
    ic.box(3, 9, 28, 28, BROWN_HI, TAN, BROWN)
    ic.pgon([(3, 9), (8, 3), (23, 3), (28, 9)], TAN, c('ffffcc'), BROWN_HI)
    ic.box(13, 9, 18, 28, YELLOW, None, YEL_DK)
    ic.box(11, 15, 20, 20, G3, W, G6)


@icon("disk_utility", "app")
def disk_utility(ic):
    ic.shadow(3, 24, 28, 24)
    ic.box(1, 10, 27, 22, G2, W, G5)
    ic.line([(4, 19), (16, 19)], G7)
    ic.px(23, 19, GREEN)
    ic.oval((17, 2, 29, 14), W, None, G3)
    ic.ellipse((19, 4, 27, 12), None, outline=G6)
    ic.line([(26, 13), (30, 18)], BROWN, 3)


@icon("system_profiler", "app")
def system_profiler(ic):
    doc_base(ic)
    ic.box(9, 10, 23, 18, G8, None, None)
    for x in (10, 13, 16, 19, 22):
        ic.line([(x, 9), (x, 19)], G5)
    ic.box(11, 12, 21, 16, G9, None, None, outline=G6)
    for i, n in enumerate((12, 9, 13)):
        ic.line([(9, 21 + i * 2), (9 + n, 21 + i * 2)], G5)


@icon("runtime_68k", "app")
def runtime_68k(ic):
    ic.shadow(6, 28, 28, 28)
    for i in range(5):
        for side in (2, 26):
            ic.rect(side, 7 + i * 4, side + 2, 8 + i * 4, G4)
    ic.box(5, 4, 26, 27, G8, G6, G9)
    ic.glyph(["###.###.#..#", "#...#.#.#.#.", "###.###.##..", "#.#.#.#.#.#.", "###.###.#..#"],
             10, 13, {'#': W})
    ic.px(7, 6, G4)


# --- hardware -------------------------------------------------------------

@icon("printer", "hardware")
def printer(ic):
    ic.box(8, 2, 23, 12, W, None, G3)
    ic.shadow(3, 28, 30, 28)
    ic.box(2, 10, 29, 26, G2, W, G5)
    ic.box(7, 19, 24, 24, W, None, G3)
    ic.rect(25, 13, 26, 14, GREEN)


@icon("monitor", "hardware")
def monitor(ic):
    ic.box(1, 2, 30, 23, G2, W, G5)
    ic.box(4, 5, 27, 20, c('6666cc'), None, None)
    ic.rect(5, 6, 26, 6, FOLD)
    ic.pgon([(11, 23), (20, 23), (23, 29), (8, 29)], G3, W, G6)


@icon("keyboard", "hardware")
def keyboard(ic):
    ic.shadow(2, 25, 31, 25)
    ic.box(0, 10, 30, 24, G2, W, G5)
    for r in range(3):
        for k in range(8):
            ic.rect(3 + k * 3 + (r % 2), 13 + r * 3, 4 + k * 3 + (r % 2), 14 + r * 3, G6)
    ic.line([(8, 22), (22, 22)], G6)


@icon("mouse", "hardware")
def mouse(ic):
    ic.line([(16, 2), (16, 6), (12, 3)], K)
    ic.shadow(10, 29, 23, 29)
    ic.box(8, 6, 24, 28, G2, W, G5, r=6)
    ic.line([(9, 14), (23, 14)], G5)
    ic.line([(16, 7), (16, 14)], G5)


@icon("speaker", "hardware")
def speaker(ic):
    ic.pgon([(3, 11), (9, 11), (17, 4), (17, 27), (9, 20), (3, 20)], G3, W, G6)
    for r, col in ((4, K), (8, G7)):
        ic.d.arc((14 - r, 15 - r - 4, 18 + r, 16 + r + 4), -55, 55, fill=col)


@icon("network", "hardware")
def network(ic):
    for x, y in ((12, 2), (2, 19), (22, 19)):
        ic.box(x, y, x + 8, y + 8, FOLD, FOLD_HI, FOLD_LO)
        ic.rect(x + 2, y + 2, x + 6, y + 5, W)
    ic.line([(16, 11), (16, 16), (6, 16), (6, 18)], K)
    ic.line([(16, 16), (26, 16), (26, 18)], K)


@icon("cpu", "hardware")
def cpu(ic):
    for i in range(6):
        for a in (1, 28):
            ic.rect(7 + i * 3, a, 8 + i * 3, a + 2, YELLOW)
            ic.rect(a, 7 + i * 3, a + 2, 8 + i * 3, YELLOW)
    ic.box(4, 4, 27, 27, G7, G5, G9)
    ic.box(10, 10, 21, 21, G3, W, G6)
    stamp(ic, gl.PI, 16, 16, G9)


@icon("battery", "hardware")
def battery(ic):
    ic.box(2, 9, 26, 22, G2, W, G5)
    ic.box(26, 13, 29, 18, G4, None, None)
    ic.rect(4, 11, 18, 20, GREEN)
    ic.rect(4, 11, 18, 12, GREEN_HI)


@icon("wireless", "hardware")
def wireless(ic):
    for r, col in ((14, FOLD_LO), (10, FOLD), (6, FOLD_LO)):
        ic.d.arc((16 - r, 24 - r, 16 + r, 24 + r), 220, 320, fill=col, width=3)
    ic.oval((13, 21, 18, 26), K, None, None)


# --- alerts ---------------------------------------------------------------

@icon("alert_stop", "alert")
def alert_stop(ic):
    ic.pgon([(10, 1), (21, 1), (30, 10), (30, 21), (21, 30), (10, 30), (1, 21), (1, 10)],
            RED, RED_HI, RED_DK)
    ic.rect(7, 13, 24, 18, W)


@icon("alert_caution", "alert")
def alert_caution(ic):
    ic.pgon([(15, 1), (16, 1), (30, 28), (1, 28)], YELLOW, c('ffff99'), YEL_DK)
    ic.rect(15, 9, 16, 20, K)
    ic.rect(15, 23, 16, 24, K)


@icon("alert_note", "alert")
def alert_note(ic):
    ic.box(3, 2, 28, 23, W, None, G3, r=8)
    ic.pgon([(8, 21), (6, 29), (15, 22)], W, None, None)
    ic.line([(8, 22), (6, 29), (14, 23)], K)
    ic.rect(15, 6, 16, 7, BLUE)
    ic.rect(14, 10, 16, 19, BLUE)
    ic.rect(13, 19, 18, 19, BLUE)


# --- overlays and logos ---------------------------------------------------

@icon("badge_alias", "overlay")
def badge_alias(ic):
    ic.box(0, 24, 7, 31, W, None, None)
    ic.line([(2, 30), (2, 28), (5, 27)], K)
    ic.poly([(4, 25), (6, 27), (4, 29)], K)


@icon("badge_locked", "overlay")
def badge_locked(ic):
    ic.d.arc((2, 23, 6, 29), 180, 360, fill=K)
    ic.line([(2, 26), (2, 27)], K)
    ic.line([(6, 26), (6, 27)], K)
    ic.box(1, 27, 7, 31, YELLOW, c('ffff99'), YEL_DK)


@icon("pi_logo", "logo")
def pi_logo(ic):
    ic.box(1, 1, 30, 30, c('6666cc'), FOLD, NAVY, r=6)
    ic.poly([(6, 10), (8, 8), (26, 8), (26, 12), (6, 12)], W)
    ic.poly([(10, 12), (14, 12), (13, 19), (11, 23), (8, 24), (7, 22), (9, 19)], W)
    ic.poly([(18, 12), (22, 12), (22, 20), (23, 21), (25, 20), (26, 21), (24, 24), (20, 24), (18, 21)], W)
