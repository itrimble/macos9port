// OS999 Browser: checks Google's currently served Chrome stable releases at
// startup and enforces version_policy (checklist B06). Browser process only.
#ifndef OS999_VERSION_GUARD_H_
#define OS999_VERSION_GUARD_H_

#include <functional>
#include <string>
#include <vector>

#include "include/cef_browser.h"

#include "version_policy.h"

class VersionGuard {
 public:
  static VersionGuard& Get();
  // UI thread, after CEF context initialization.
  void Start(std::function<void(const VersionResult&)> on_result);
  const VersionResult& result() const { return result_; }
  // True when |url| is a web page this outdated build must not load. While the
  // online check is pending, remembers |browser| so a late "block" verdict can
  // replace a page that already started loading.
  bool ShouldBlock(CefRefPtr<CefBrowser> browser, const std::string& url);
  std::string BlockPageURL() const;
  void Finish(const std::string& json, bool ok);  // internal

 private:
  VersionResult result_;
  std::function<void(const VersionResult&)> on_result_;
  bool allow_outdated_ = false;
  bool pending_ = false;
  std::vector<CefRefPtr<CefBrowser>> seen_while_pending_;
  int pin_age_ = 0;
};

#endif
