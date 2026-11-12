#include "mac_ppc.h"

/* Signature rows for the Resource Manager, Scrap, packages and the selector-dispatched managers. Written from memory
 * of the public API; see include/mac_ppc.h for the grammar. */
#define FSP(name, sig, selector) {name, 0xaa52, sig "/d" selector}
#define AE(name, sig, selector) {name, 0xa816, sig "/d" selector}
#define SF(name, sig, selector) {name, 0xa9ea, sig "/w" selector}

static const MacPpcSig sigs[] = {
    {"OpenResFile", 0xa997, "w:l"}, {"UseResFile", 0xa998, "-:w"}, {"UpdateResFile", 0xa999, "-:w"},
    {"CloseResFile", 0xa99a, "-:w"}, {"SetResLoad", 0xa99b, "-:b"}, {"CurResFile", 0xa994, "w:"},
    {"HomeResFile", 0xa9a4, "w:l"}, {"GetResource", 0xa9a0, "l:lw"}, {"Get1Resource", 0xa81f, "l:lw"},
    {"GetNamedResource", 0xa9a1, "l:ll"}, {"GetIndResource", 0xa99d, "l:lw"}, {"Get1IndResource", 0xa80e, "l:lw"},
    {"CountResources", 0xa99c, "w:l"}, {"Count1Resources", 0xa80d, "w:l"},
    {"LoadResource", 0xa9a2, "-:l"}, {"ReleaseResource", 0xa9a3, "-:l"}, {"DetachResource", 0xa992, "-:l"},
    {"AddResource", 0xa9ab, "-:llwl"}, {"ChangedResource", 0xa9aa, "-:l"}, {"WriteResource", 0xa9b0, "-:l"},
    {"RemoveResource", 0xa9ad, "-:l"}, {"GetResInfo", 0xa9a8, "-:llll"}, {"SetResInfo", 0xa9a9, "-:lwl"},
    {"GetResAttrs", 0xa9a6, "w:l"}, {"SetResAttrs", 0xa9a7, "-:lw"}, {"ResError", 0xa9af, "w:"},
    {"GetResourceSizeOnDisk", 0xa9a5, "l:l"}, {"CreateResFile", 0xa9b1, "-:l"},
    {"HOpenResFile", 0xa81a, "w:wllb"}, {"HCreateResFile", 0xa81b, "-:wll"},
    {"GetString", 0xa9ba, "l:w"},
    {"ZeroScrap", 0xa9fc, "l:"}, {"PutScrap", 0xa9fe, "l:lll"}, {"GetScrap", 0xa9fd, "l:lll"},
    {"InfoScrap", 0xa9f9, "l:"}, {"LoadScrap", 0xa9fb, "l:"}, {"UnloadScrap", 0xa9fa, "l:"},
    {"SysBeep", 0xa9c8, "-:w"}, {"ExitToShell", 0xa9f4, "-:"}, {"Munger", 0xa9e0, "l:llllll"},
    {"KeyTranslate", 0xa9c3, "l:lwl"},
    {"GetScriptManagerVariable", 0xa8b5, "l:w/l84020008"}, {"GetScriptVariable", 0xa8b5, "l:ww/l8404000C"},
    {"CharByte", 0xa8b5, "w:lw/l82060010"},
    {"IUMagString", 0xa9ed, "w:llww/w000A"},
    {"FindFolder", 0xa823, "w:wlbll/d0000"}, {"ResolveAliasFile", 0xa823, "w:lbll/d000C"},
    {"SetFrontProcess", 0xa88f, "w:l/w003B"}, {"GetCurrentProcess", 0xa88f, "w:l/w0037"},
    FSP("FSMakeFSSpec", "w:wlll", "0001"), FSP("FSpOpenDF", "w:lbl", "0002"), FSP("FSpOpenRF", "w:lbl", "0003"),
    FSP("FSpCreate", "w:lllw", "0004"), FSP("FSpDirCreate", "w:lwl", "0005"), FSP("FSpDelete", "w:l", "0006"),
    FSP("FSpGetFInfo", "w:ll", "0007"), FSP("FSpSetFInfo", "w:ll", "0008"), FSP("FSpRename", "w:ll", "000B"),
    FSP("FSpCatMove", "w:ll", "000C"), FSP("FSpOpenResFile", "w:lb", "000D"),
    FSP("FSpCreateResFile", "-:lllw", "000E"), FSP("FSpExchangeFiles", "w:ll", "000F"),
    AE("AEInstallEventHandler", "w:llllb", "091F"), AE("AEProcessAppleEvent", "w:l", "021B"),
    AE("AEInteractWithUser", "w:lll", "061C"), AE("AESetInteractionAllowed", "w:b", "011E"),
    AE("AEGetParamPtr", "w:llllll", "0E11"), AE("AEGetParamDesc", "w:llll", "0812"),
    AE("AEGetAttributePtr", "w:llllll", "0E15"), AE("AECountItems", "w:ll", "0407"),
    AE("AEDisposeDesc", "w:l", "0204"), AE("AECreateDesc", "w:llll", "0825"),
    SF("SFPutFile", "-:lllll", "0001"), SF("SFGetFile", "-:lllwlll", "0002"),
    SF("StandardPutFile", "-:lll", "0005"), SF("StandardGetFile", "-:lwll", "0006"),
    {"HMGetBalloons", 0xa830, "b:/w0003"},
};

const MacPpcSig *mac_ppc_sigs_managers(size_t *count) {
    *count = sizeof sigs / sizeof sigs[0];
    return sigs;
}
