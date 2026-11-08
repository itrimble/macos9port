// OS999 Browser: supported-version policy (checklist B06). Pure logic, no CEF,
// so it is unit-tested on its own (tests/version_policy_test.cc).
#ifndef OS999_VERSION_POLICY_H_
#define OS999_VERSION_POLICY_H_

#include <string>
#include <vector>

struct ChromeVersion {
  int part[4] = {0, 0, 0, 0};
  static bool Parse(const std::string& text, ChromeVersion* out);
  int Compare(const ChromeVersion& other) const;  // <0, 0, >0
  std::string ToString() const;
};

// A Chrome stable release Google is serving right now, with how many days
// ago that serving period started.
struct ServedRelease {
  ChromeVersion version;
  int days_serving = 0;
};

enum class VersionVerdict { kUnknown, kCurrent, kUpdateAvailable, kUnsupported };

struct VersionResult {
  VersionVerdict verdict = VersionVerdict::kUnknown;
  bool block = false;      // refuse to load web pages
  std::string message;     // shown to the person
};

constexpr int kUnsupportedGraceDays = 21;  // CEF trails Chrome majors by days
constexpr int kOfflineMaxAgeDays = 60;     // when the check cannot run

// |served| empty means the online check failed or has not finished.
VersionResult EvaluateVersion(const ChromeVersion& ours,
                              const std::vector<ServedRelease>& served,
                              int pin_age_days, bool allow_outdated);

// Days from an ISO date ("2026-10-31", optionally with a time) to |now|
// (seconds since the epoch). Returns -1 if unparsable.
int DaysSince(const std::string& iso_date, long long now);

#endif
