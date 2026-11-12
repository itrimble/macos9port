#include "mac_ppc.h"

/* Signature rows for the Window, Event, Menu, Control, Dialog and TextEdit Managers (Pascal stack-based Toolbox
 * traps). Written from memory of the public API; see include/mac_ppc.h for the grammar. */
#define TE(name, sig, selector) {name, 0xa83d, sig "/w" selector}

static const MacPpcSig sigs[] = {
    {"InitFonts", 0xa8fe, "-:"}, {"GetFontName", 0xa8ff, "-:wl"}, {"GetFNum", 0xa900, "-:ll"},
    {"InitWindows", 0xa912, "-:"}, {"GetWMgrPort", 0xa910, "-:l"}, {"NewWindow", 0xa913, "l:lllbwlbl"},
    {"GetNewWindow", 0xa9bd, "l:wll"}, {"NewCWindow", 0xaa45, "l:lllbwlbl"}, {"GetNewCWindow", 0xaa46, "l:wll"},
    {"DisposeWindow", 0xa914, "-:l"}, {"CloseWindow", 0xa92d, "-:l"}, {"ShowWindow", 0xa915, "-:l"},
    {"HideWindow", 0xa916, "-:l"}, {"ShowHide", 0xa908, "-:lb"}, {"SelectWindow", 0xa91f, "-:l"},
    {"BringToFront", 0xa920, "-:l"}, {"SendBehind", 0xa921, "-:ll"}, {"FrontWindow", 0xa924, "l:"},
    {"HiliteWindow", 0xa91c, "-:lb"}, {"DrawGrowIcon", 0xa904, "-:l"}, {"MoveWindow", 0xa91b, "-:lwwb"},
    {"SizeWindow", 0xa91d, "-:lwwb"}, {"ZoomWindow", 0xa83a, "-:lwb"}, {"SetWTitle", 0xa91a, "-:ll"},
    {"GetWTitle", 0xa919, "-:ll"}, {"SetWRefCon", 0xa918, "-:ll"}, {"GetWRefCon", 0xa917, "l:l"},
    {"FindWindow", 0xa92c, "w:ll"}, {"TrackGoAway", 0xa91e, "b:ll"}, {"TrackBox", 0xa83b, "b:llw"},
    {"GrowWindow", 0xa92b, "l:lll"}, {"DragWindow", 0xa925, "-:lll"}, {"DragGrayRgn", 0xa905, "l:llllwl"},
    {"InvalRect", 0xa928, "-:l"}, {"InvalRgn", 0xa927, "-:l"}, {"ValidRect", 0xa92a, "-:l"}, {"ValidRgn", 0xa929, "-:l"},
    {"BeginUpdate", 0xa922, "-:l"}, {"EndUpdate", 0xa923, "-:l"}, {"PaintOne", 0xa90c, "-:ll"},
    {"PaintBehind", 0xa90d, "-:ll"}, {"CalcVisBehind", 0xa90a, "-:ll"},
    {"GetNextEvent", 0xa970, "b:wl"}, {"WaitNextEvent", 0xa860, "b:wlll"}, {"EventAvail", 0xa971, "b:wl"},
    {"GetMouse", 0xa972, "-:l"}, {"StillDown", 0xa973, "b:"}, {"Button", 0xa974, "b:"}, {"TickCount", 0xa975, "l:"},
    {"GetKeys", 0xa976, "-:l"}, {"WaitMouseUp", 0xa977, "b:"},
    {"SystemTask", 0xa9b4, "-:"}, {"SystemClick", 0xa9b3, "-:ll"}, {"SystemEdit", 0xa9c2, "b:w"},
    {"SystemEvent", 0xa9b2, "b:l"}, {"OpenDeskAcc", 0xa9b6, "w:l"}, {"CloseDeskAcc", 0xa9b7, "-:w"},
    {"InitMenus", 0xa930, "-:"}, {"NewMenu", 0xa931, "l:wl"}, {"GetMenu", 0xa9bf, "l:w"}, {"DisposeMenu", 0xa932, "-:l"},
    {"AppendMenu", 0xa933, "-:ll"}, {"AppendResMenu", 0xa94d, "-:ll"}, {"InsertMenu", 0xa935, "-:lw"},
    {"DeleteMenu", 0xa936, "-:w"}, {"DrawMenuBar", 0xa937, "-:"}, {"HiliteMenu", 0xa938, "-:w"},
    {"MenuSelect", 0xa93d, "l:l"}, {"MenuKey", 0xa93e, "l:w"}, {"EnableItem", 0xa939, "-:lw"},
    {"DisableItem", 0xa93a, "-:lw"}, {"CheckItem", 0xa945, "-:lwb"}, {"SetItemMark", 0xa944, "-:lww"},
    {"GetItemMark", 0xa943, "-:lwl"}, {"SetMenuItemText", 0xa947, "-:lwl"}, {"GetMenuItemText", 0xa946, "-:lwl"},
    {"SetItemCmd", 0xa84f, "-:lww"}, {"GetItemCmd", 0xa84e, "-:lwl"}, {"CountMItems", 0xa950, "w:l"},
    {"GetMenuHandle", 0xa949, "l:w"}, {"InsertMenuItem", 0xa826, "-:llw"}, {"DeleteMenuItem", 0xa952, "-:lw"},
    {"PopUpMenuSelect", 0xa80b, "l:lwww"}, {"CalcMenuSize", 0xa948, "-:l"},
    {"NewControl", 0xa954, "l:lllbwwwwl"}, {"GetNewControl", 0xa9be, "l:wl"}, {"DisposeControl", 0xa955, "-:l"},
    {"ShowControl", 0xa957, "-:l"}, {"HideControl", 0xa958, "-:l"}, {"MoveControl", 0xa959, "-:lww"},
    {"GetControlReference", 0xa95a, "l:l"}, {"SetControlReference", 0xa95b, "-:ll"}, {"SizeControl", 0xa95c, "-:lww"},
    {"HiliteControl", 0xa95d, "-:lw"}, {"GetControlTitle", 0xa95e, "-:ll"}, {"SetControlTitle", 0xa95f, "-:ll"},
    {"GetControlValue", 0xa960, "w:l"}, {"GetControlMinimum", 0xa961, "w:l"}, {"GetControlMaximum", 0xa962, "w:l"},
    {"SetControlValue", 0xa963, "-:lw"}, {"SetControlMinimum", 0xa964, "-:lw"}, {"SetControlMaximum", 0xa965, "-:lw"},
    {"TestControl", 0xa966, "w:ll"}, {"TrackControl", 0xa968, "w:lll"}, {"DrawControls", 0xa969, "-:l"},
    {"SetControlAction", 0xa96b, "-:ll"}, {"FindControl", 0xa96c, "w:lll"}, {"Draw1Control", 0xa96d, "-:l"},
    {"InitDialogs", 0xa97b, "-:l"}, {"GetNewDialog", 0xa97c, "l:wll"}, {"NewDialog", 0xa97d, "l:lllbwlbll"},
    {"DisposeDialog", 0xa983, "-:l"}, {"ModalDialog", 0xa991, "-:ll"}, {"Alert", 0xa985, "w:wl"},
    {"StopAlert", 0xa986, "w:wl"}, {"NoteAlert", 0xa987, "w:wl"}, {"CautionAlert", 0xa988, "w:wl"},
    {"ParamText", 0xa98b, "-:llll"}, {"GetDialogItem", 0xa98d, "-:lwlll"}, {"SetDialogItem", 0xa98e, "-:lwwll"},
    {"GetDialogItemText", 0xa990, "-:ll"}, {"SetDialogItemText", 0xa98f, "-:ll"},
    {"SelectDialogItemText", 0xa97e, "-:lwww"}, {"DrawDialog", 0xa981, "-:l"},
    {"TEInit", 0xa9cc, "-:"}, {"TENew", 0xa9d2, "l:ll"}, {"TEStyleNew", 0xa83e, "l:ll"}, {"TEDispose", 0xa9cd, "-:l"},
    {"TESetText", 0xa9cf, "-:lll"}, {"TEGetText", 0xa9cb, "l:l"}, {"TEIdle", 0xa9da, "-:l"}, {"TEClick", 0xa9d4, "-:lbl"},
    {"TESetSelect", 0xa9d1, "-:lll"}, {"TEActivate", 0xa9d8, "-:l"}, {"TEDeactivate", 0xa9d9, "-:l"},
    {"TEKey", 0xa9dc, "-:wl"}, {"TECut", 0xa9d6, "-:l"}, {"TECopy", 0xa9d5, "-:l"}, {"TEPaste", 0xa9db, "-:l"},
    {"TEDelete", 0xa9d7, "-:l"}, {"TEInsert", 0xa9de, "-:lll"}, {"TESetAlignment", 0xa9df, "-:wl"},
    {"TEUpdate", 0xa9d3, "-:ll"}, {"TECalText", 0xa9d0, "-:l"}, {"TEScroll", 0xa9dd, "-:wwl"},
    {"TESelView", 0xa811, "-:l"}, {"TEAutoView", 0xa813, "-:bl"}, {"TEGetOffset", 0xa83c, "w:ll"},
    TE("TEStylePaste", "-:l", "0000"), TE("TESetStyle", "-:wlbl", "0001"), TE("TEGetStyle", "-:llll", "0003"),
    TE("TEGetStyleScrapHandle", "l:l", "0006"), TE("TEStyleInsert", "-:llll", "0007"), TE("TEGetPoint", "l:wl", "0008"),
    TE("TEGetHeight", "l:lll", "0009"), TE("TEContinuousStyle", "b:lll", "000A"),
    TE("TEUseStyleScrap", "-:lllbl", "000B"), TE("TENumStyles", "l:lll", "000D"),
};

const MacPpcSig *mac_ppc_sigs_toolbox(size_t *count) {
    *count = sizeof sigs / sizeof sigs[0];
    return sigs;
}
