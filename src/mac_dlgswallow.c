#include "mac_runtime.h"
#include "mac_dlgswallow.h"

/* While a dialog is the current port, drawing the application does into it is not shown: our dialogs are drawn from their DITLs, and
 * nothing here gives a dialog a port to draw in. The small set of QuickDraw calls that only decorate (and DrawPicture, which is not shown; outlining the default button with
 * FrameRoundRect, a frame, an erase, a string) and the pen and text state calls that go with them are accepted and ignored, so they do
 * not land on the application's own window port; every other drawing call halts by name. */
int mac_dlg_swallowed_args(uint16_t op) {
    switch (op) {
    case 0xa89b: case 0xa893: case 0xa894: case 0xa891: case 0xa892: case 0xa862: case 0xa863: case 0xa87c: return 4;   /* PenSize, MoveTo, Move, LineTo, Line, ForeColor, BackColor, BackPat */
    case 0xa864: return 2;                                                                                  /* ColorBit */
    case 0xa896: case 0xa897: return 0;                                                                     /* HidePen, ShowPen */
    case 0xa89c: case 0xa887: case 0xa888: case 0xa889: case 0xa88a: return 2;                             /* PenMode, TextFont, TextFace, TextMode, TextSize */
    case 0xa89d: return 4;                                                                                  /* PenPat */
    case 0xa89e: return 0;                                                                                  /* PenNormal */
    case 0xa8a1: case 0xa8a2: case 0xa8a3: case 0xa8a4: case 0xa8b7: case 0xa8b8: case 0xa8b9: case 0xa8ba: case 0xa884: return 4;   /* Frame/Paint/Erase/Invert Rect and Oval, DrawString */
    case 0xa8a5: return 8;                                                                                  /* FillRect */
    case 0xa8b0: case 0xa8b1: case 0xa8b2: case 0xa8b3: return 8;                                           /* Frame/Paint/Erase/Invert RoundRect */
    case 0xa883: return 2;                                                                                  /* DrawChar */
    case 0xa8f6: return 8;                                                                                  /* DrawPicture: NOT shown (a picture is content, not decoration; accepted so the program can run on) */
    default: return -1;
    }
}
int mac_dlg_drawing_trap(uint16_t op) {
    return (op >= 0xa8a1 && op <= 0xa8a5) || (op >= 0xa8b0 && op <= 0xa8b4) || (op >= 0xa8b7 && op <= 0xa8bb) || (op >= 0xa8be && op <= 0xa8c2) ||
           (op >= 0xa8c6 && op <= 0xa8ca) || (op >= 0xa8d2 && op <= 0xa8d6) || (op >= 0xa883 && op <= 0xa885) || op == 0xa891 || op == 0xa892 ||
           op == 0xa8ec || op == 0xa8ef || op == 0xa8f6;
}
