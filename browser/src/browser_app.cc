#include "browser_app.h"
#include "web_apps.h"

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  (void)test_autoplay_;  // Kept in the shared constructor ABI; unused in release.
  if (!process_type.empty()) return;
  // Never weaken isolation or TLS here: no --no-sandbox, no
  // --ignore-certificate-errors, no --disable-gpu (checklist B03/B09).
#if defined(OS_LINUX)
  // Off-screen only: the OS999 desktop presents the frames, so the helper
  // needs no X11/Wayland connection of its own (works on a bare Pi console).
  if (!command_line->HasSwitch("ozone-platform"))
    command_line->AppendSwitchWithValue("ozone-platform", "headless");
#endif
#ifdef OS999_ENABLE_TEST_HOOKS
  if (test_autoplay_ && !command_line->HasSwitch("autoplay-policy")) {
    // Measurement runs only (--os999-test): lets a scripted YouTube load start
    // audible playback without a click. Normal launches keep Chromium policy.
    command_line->AppendSwitchWithValue("autoplay-policy",
                                        "no-user-gesture-required");
  }
#endif
}

void BrowserApp::OnContextInitialized() {
  WebApps::Register();
  if (on_ready_) on_ready_();
}
