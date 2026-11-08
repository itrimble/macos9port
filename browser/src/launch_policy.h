#ifndef OS999_LAUNCH_POLICY_H
#define OS999_LAUNCH_POLICY_H
#include <cstdio>
#include <string>

inline bool BrowserLaunchAllowed(int argc, char* argv[]) {
  const char* keys[] = {"url", "os999-embed-fd", "os999-embed-port", "os999-download-dir",
      "os999-stats-log", "os999-profile", "os999-size", "os999-fps"};
  for (int i = 1; i < argc; ++i) {
    std::string name(argv[i]);
    name = name.substr(0, name.find('='));
    bool allowed = false;
    for (const char* key : keys) allowed |= name == std::string("--") + key;
#ifdef OS999_ENABLE_TEST_HOOKS
    const char* tests[] = {"os999-test", "os999-quality", "os999-quality-step",
                          "os999-report-at", "os999-exit-after"};
    for (const char* key : tests) allowed |= name == std::string("--") + key;
#endif
    if (!allowed) { fprintf(stderr, "unsupported launch option\n"); return false; }
  }
  return true;
}
#endif
