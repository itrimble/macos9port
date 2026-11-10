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
#if defined(OS_MAC)
  // Chromium would otherwise create a "Chromium Safe Storage" login-keychain item and macOS
  // would ask for the user's password. The embedded browser must open without that prompt, so
  // the profile's cookie key stays in the profile. The trade-off: saved cookies are protected
  // by the profile folder's permissions, not by the keychain.
  if (!command_line->HasSwitch("use-mock-keychain"))
    command_line->AppendSwitch("use-mock-keychain");
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

void BrowserApp::OnBeforeChildProcessLaunch(
    CefRefPtr<CefCommandLine> command_line) {
#if defined(OS_MAC)
  // The network service and other helpers encrypt cookies and tokens too; give
  // them the same instruction so none of them opens the user's login keychain.
  if (!command_line->HasSwitch("use-mock-keychain"))
    command_line->AppendSwitch("use-mock-keychain");
#else
  (void)command_line;
#endif
}

void BrowserApp::OnContextInitialized() {
  WebApps::Register();
  if (on_ready_) on_ready_();
}
