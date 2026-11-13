// OS999 Browser: glue between the opt-in decoder diagnostics (decode_diag.h) and
// the standalone browser window. Everything is a no-op unless OS999_DECODE_DIAG=1.
#ifndef OS999_DECODE_DIAG_HOST_H_
#define OS999_DECODE_DIAG_HOST_H_
#import <Foundation/Foundation.h>

// Opens <support>/diagnostics/decode-diag.log (0600, inside the private folder)
// and notes how large chromium.log already is.
void DecodeDiagStart(NSString* support);
// True when a page script should scrape chrome://media-internals / chrome://gpu:
// diagnostics mode, or a developer build with OS999_ENABLE_TEST_HOOKS.
bool DecodeDiagWantsScrape();
// JS for the current page; logs OS999MEDIA / OS999GPU console lines. Runs only
// on chrome://media-internals and chrome://gpu; it never touches other pages.
NSString* DecodeDiagScript();
// Handles an "OS999MEDIA ..." or "OS999GPU ..." console message.
void DecodeDiagConsole(NSString* message);
// After CefShutdown: copies decoder-selection lines from this run's chromium.log
// into the diagnostics log and writes the final verdict.
void DecodeDiagFinish();
#endif
