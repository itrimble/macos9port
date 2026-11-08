#include "version_policy.h"

#include <cstdio>
#include <charconv>
#include <ctime>

bool ChromeVersion::Parse(const std::string& text, ChromeVersion* out) {
  if (!out) return false;
  ChromeVersion v;
  const char* cur = text.data();
  const char* end = cur + text.size();
  for (int i = 0; i < 4; ++i) {
    if (cur == end || *cur < '0' || *cur > '9') return false;
    auto parsed = std::from_chars(cur, end, v.part[i]);
    if (parsed.ec != std::errc()) return false;
    cur = parsed.ptr;
    if (i < 3) { if (cur == end || *cur++ != '.') return false; }
  }
  if (cur != end) return false;
  *out = v;
  return true;
}

int ChromeVersion::Compare(const ChromeVersion& o) const {
  for (int i = 0; i < 4; i++)
    if (part[i] != o.part[i]) return part[i] < o.part[i] ? -1 : 1;
  return 0;
}

std::string ChromeVersion::ToString() const {
  char buf[64];
  snprintf(buf, sizeof buf, "%d.%d.%d.%d", part[0], part[1], part[2], part[3]);
  return buf;
}

int DaysSince(const std::string& iso, long long now) {
  struct tm t = {};
  if (sscanf(iso.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return -1;
  t.tm_year -= 1900;
  t.tm_mon -= 1;
  long long then = (long long)timegm(&t);
  return then > now ? 0 : (int)((now - then) / 86400);
}

VersionResult EvaluateVersion(const ChromeVersion& ours, const std::vector<ServedRelease>& served,
                              int pin_age_days, bool allow_outdated) {
  VersionResult r;
  std::string me = "Chromium " + ours.ToString();
  if (served.empty()) {
    r.verdict = VersionVerdict::kUnknown;
    if (pin_age_days > kOfflineMaxAgeDays) {
      r.block = !allow_outdated;
      r.message = "Could not check for updates and this build is " + std::to_string(pin_age_days) +
                  " days old. Update OS999 Browser.";
    }
    return r;
  }
  const ServedRelease* newest = &served[0];
  int min_major = served[0].version.part[0];
  for (const auto& s : served) {
    if (s.version.Compare(ours) == 0) {
      r.verdict = VersionVerdict::kCurrent;
      return r;
    }
    if (s.version.Compare(newest->version) > 0) newest = &s;
    if (s.version.part[0] < min_major) min_major = s.version.part[0];
  }
  if (ours.Compare(newest->version) > 0) {  // newer than stable (dev build)
    r.verdict = VersionVerdict::kCurrent;
    return r;
  }
  if (ours.part[0] < min_major) {
    // Google no longer serves this major version: it stops getting fixes.
    int days = 0;
    for (const auto& s : served)
      if (s.version.part[0] > ours.part[0] && s.days_serving > days) days = s.days_serving;
    r.verdict = VersionVerdict::kUnsupported;
    // startTime restarts with each rollout step, so it understates age. Two or
    // more majors behind stable means a newer major shipped >= ~4 weeks ago.
    bool far_behind = newest->version.part[0] - ours.part[0] >= 2;
    r.block = !allow_outdated && (far_behind || days > kUnsupportedGraceDays);
    r.message = me + " is no longer supported; Chrome stable is " + newest->version.ToString() +
                ". Update OS999 Browser.";
    return r;
  }
  r.verdict = VersionVerdict::kUpdateAvailable;
  r.message = "Security update available: Chromium " + newest->version.ToString() +
              " (this is " + ours.ToString() + ").";
  return r;
}
