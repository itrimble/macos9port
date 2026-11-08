#include "launch_policy.h"
#include "os999_browser_ipc.h"
#include <cassert>

static bool Allowed(const char* option) {
  char app[] = "browser";
  char* args[] = {app, const_cast<char*>(option)};
  return BrowserLaunchAllowed(2, args);
}
int main() {
  static_assert(sizeof(Os999BrowserMsg) == 1040, "IPC wire layout changed");
  static_assert(OS999B_SHUTDOWN == 23 && OS999B_TLS == 112, "IPC wire IDs changed");
  assert(Allowed("--os999-embed-fd=3"));
  assert(Allowed("--url=https://example.com"));
  assert(Allowed("--os999-profile=/tmp/private-profile"));
  assert(!Allowed("--ignore-certificate-errors"));
  assert(!Allowed("--ignore-certificate-errors-spki-list=key"));
  assert(!Allowed("--no-sandbox"));
  assert(!Allowed("--allow-running-insecure-content"));
  assert(!Allowed("--disable-web-security"));
  assert(!Allowed("--remote-debugging-port=9222"));
  assert(!Allowed("https://example.com"));
#ifndef OS999_ENABLE_TEST_HOOKS
  assert(!Allowed("--os999-test"));
  assert(!Allowed("--os999-quality=hd2160"));
  assert(!Allowed("--os999-exit-after=1"));
#endif
  puts("launch policy and IPC layout: ok");
}
