// OS999 Browser for Linux / Raspberry Pi: runs only embedded in the OS999
// desktop (--os999-embed-fd). Frames go through shared memory (CPU path).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "browser_app.h"
#include "launch_policy.h"
#include "browser_stats.h"
#include "embed/embed_frames.h"
#include "embed/embed_host.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"

static EmbedHost* g_host;  // embedded session (lives until exit)

int main(int argc, char* argv[]) {
  CefMainArgs args(argc, argv);
  int code = CefExecuteProcess(args, nullptr, nullptr);
  if (code >= 0) return code;  // a Chromium sub-process finished

  CefRefPtr<CefCommandLine> cl = CefCommandLine::CreateCommandLine();
  cl->InitFromArgv(argc, argv);
  if (!BrowserLaunchAllowed(argc, argv)) return 64;
  if (!cl->HasSwitch("os999-embed-fd")) {
    fprintf(stderr, "OS999 Browser runs inside the OS999 desktop: choose 999 > Chromium Browser.\n");
    return 2;
  }
  int fd = atoi(cl->GetSwitchValue("os999-embed-fd").ToString().c_str());
  std::string downloads = cl->GetSwitchValue("os999-download-dir").ToString();
  BrowserStats::Get().SetLogPath(cl->GetSwitchValue("os999-stats-log").ToString());

  const char* home = getenv("HOME");
  std::string root = std::string(home ? home : "/tmp") + "/.config/os999-browser-security";
  std::string profile = cl->GetSwitchValue("os999-profile").ToString();
  if (!profile.empty()) { if (profile[0] != '/') return 64; root = profile; }
  CefSettings settings;
  settings.windowless_rendering_enabled = true;
  settings.persist_session_cookies = true;
  CefString(&settings.root_cache_path) = root;
  CefString(&settings.cache_path) = root + "/Profile";
  CefString(&settings.log_file) = root + "/chromium.log";

  CefRefPtr<BrowserApp> app(new BrowserApp(
      [fd, downloads]() {
        FrameSink::Init("");
        g_host = new EmbedHost(fd, downloads);
        g_host->Start();
      },
      cl->HasSwitch("os999-test")));
  if (!CefInitialize(args, settings, app.get(), nullptr)) return CefGetExitCode();
  BrowserStats::Get().Log(std::string("start cef ") + CEF_VERSION + " linux");
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
