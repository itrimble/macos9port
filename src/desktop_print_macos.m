#import <AppKit/AppKit.h>
/* AppKit imports Carbon Rect; keep the desktop C layout distinct here. */
#define Rect OspiRect
#include "finder_print.h"
#include "session_int.h"
#undef Rect
#include <math.h>

static NSPrintInfo *settings;
static NSPrintInfo *print_settings(void) {
    if (!settings) {
        settings = [[NSPrintInfo sharedPrintInfo] copy];
        [settings setHorizontalPagination:NSPrintingPaginationModeFit];
        [settings setVerticalPagination:NSPrintingPaginationModeAutomatic];
        [settings setHorizontallyCentered:NO]; [settings setVerticallyCentered:NO];
        [settings setLeftMargin:36]; [settings setRightMargin:36];
        [settings setTopMargin:36]; [settings setBottomMargin:36];
    }
    return settings;
}
static NSView *make_view(const Session *s, const uint32_t *frame, int kind) {
    NSPrintInfo *info = print_settings();
    if (kind == PRINT_TEXT) {
        NSString *text = [[NSString alloc] initWithUTF8String:s->print_text];
        if (!text) text = [[NSString alloc] initWithCString:s->print_text encoding:NSMacOSRomanStringEncoding];
        if (!text) return nil;
        CGFloat width = MAX(72, info.paperSize.width - info.leftMargin - info.rightMargin);
        NSTextView *view = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, width, 1)];
        [view setTextContainerInset:NSMakeSize(0, 0)];
        [[view textContainer] setLineFragmentPadding:0];
        [[view textContainer] setContainerSize:NSMakeSize(width, CGFLOAT_MAX)];
        [[view textContainer] setWidthTracksTextView:YES];
        [view setFont:[NSFont userFixedPitchFontOfSize:11]];
        [view setTextColor:[NSColor blackColor]];
        [view setBackgroundColor:[NSColor whiteColor]];
        [view setString:text]; [text release];
        [[view layoutManager] ensureLayoutForTextContainer:[view textContainer]];
        NSRect used = [[view layoutManager] usedRectForTextContainer:[view textContainer]];
        [view setFrameSize:NSMakeSize(width, MAX(20, ceil(used.size.height) + 2))];
        return view;
    }
    OspiRect r = s->print_rect;
    if (!frame || r.x < 0 || r.y < 0 || r.w < 1 || r.h < 1 || r.w > OSPI_W || r.h > OSPI_H ||
        r.x > OSPI_W - r.w || r.y > OSPI_H - r.h) return nil;
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
        pixelsWide:r.w pixelsHigh:r.h bitsPerSample:8 samplesPerPixel:3 hasAlpha:NO
        isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:r.w * 3 bitsPerPixel:24];
    if (!bitmap) return nil;
    unsigned char *pixels = bitmap.bitmapData;
    for (int y = 0; y < r.h; y++) for (int x = 0; x < r.w; x++) {
        uint32_t pixel = frame[(r.y + y) * OSPI_W + r.x + x];
        size_t index = (size_t)y * bitmap.bytesPerRow + x * 3;
        pixels[index] = (pixel >> 16) & 255; pixels[index + 1] = (pixel >> 8) & 255; pixels[index + 2] = pixel & 255;
    }
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(r.w, r.h)]; [image addRepresentation:bitmap];
    NSImageView *view = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, r.w, r.h)];
    [view setImage:image]; [view setImageScaling:NSImageScaleProportionallyUpOrDown];
    [image release]; [bitmap release]; return view;
}
int desktop_print_supported(void) { return 1; }
void desktop_print_service(Session *s, const uint32_t *frame) {
    int kind = s->print_request; if (!kind) return;
    s->print_request = 0;
    @autoreleasepool {
        @try {
            if (kind == PRINT_PAGE_SETUP) {
                NSPrintInfo *draft = [print_settings() copy];
                if ([[NSPageLayout pageLayout] runModalWithPrintInfo:draft] == NSModalResponseOK) {
                    [settings release]; settings = draft;
                } else [draft release];
                return;
            }
            NSView *view = make_view(s, frame, kind);
            if (!view) { dialog_info(s, "Print", "The print view could not be prepared. Nothing was sent to a printer."); return; }
            NSPrintOperation *operation = [NSPrintOperation printOperationWithView:view printInfo:print_settings()];
            [operation setJobTitle:[NSString stringWithUTF8String:s->print_title] ?: @"OS999"];
            [operation setShowsPrintPanel:YES]; [operation setShowsProgressPanel:YES];
            [operation runOperation]; [view release];
        } @catch (NSException *exception) {
            (void)exception; dialog_info(s, "Print", "The host printing service could not complete this operation.");
        }
    }
}
int desktop_print_export_pdf(Session *s, const uint32_t *frame, const char *path) {
    if (!path || !path[0] || (s->print_request != PRINT_TEXT && s->print_request != PRINT_WINDOW)) return -1;
    @autoreleasepool {
        NSView *view = nil;
        @try {
            view = make_view(s, frame, s->print_request); if (!view) return -1;
            NSPrintInfo *info = [print_settings() copy];
            [[info dictionary] setObject:NSPrintSaveJob forKey:NSPrintJobDisposition];
            [[info dictionary] setObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]] forKey:NSPrintJobSavingURL];
            NSPrintOperation *operation = [NSPrintOperation printOperationWithView:view printInfo:info];
            [operation setShowsPrintPanel:NO]; [operation setShowsProgressPanel:NO];
            BOOL result = [operation runOperation];
            [info release]; [view release]; return result ? 0 : -1;
        } @catch (NSException *exception) { (void)exception; [view release]; return -1; }
    }
}
