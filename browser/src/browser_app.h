// OS999 Browser: browser-process CefApp. Sandbox stays on (checklist B03).
#ifndef OS999_BROWSER_APP_H_
#define OS999_BROWSER_APP_H_

#include <functional>

#include "include/cef_app.h"

class BrowserApp : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(std::function<void()> on_ready, bool test_autoplay)
      : on_ready_(std::move(on_ready)), test_autoplay_(test_autoplay) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnContextInitialized() override;
  void OnBeforeChildProcessLaunch(
      CefRefPtr<CefCommandLine> command_line) override;

 private:
  std::function<void()> on_ready_;
  bool test_autoplay_;
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

#endif
