// Repeatable checks for the supported-version policy (checklist B06).
#include <cstdio>

#include "version_policy.h"

static int failures = 0;
#define EXPECT(cond)                                              \
  do {                                                            \
    if (!(cond)) {                                                \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);     \
      failures++;                                                 \
    }                                                             \
  } while (0)

static ChromeVersion V(const char* s) {
  ChromeVersion v;
  ChromeVersion::Parse(s, &v);
  return v;
}

int main() {
  ChromeVersion ours = V("156.0.8078.80");
  EXPECT(V("157.0.1.2").Compare(ours) > 0);
  ChromeVersion bad;
  EXPECT(!ChromeVersion::Parse("not a version", &bad));

  EXPECT(!ChromeVersion::Parse("156.0.8078.80junk", &bad));
  EXPECT(!ChromeVersion::Parse("-1.0.0.0", &bad));
  EXPECT(!ChromeVersion::Parse("156..0.0", &bad));
  EXPECT(!ChromeVersion::Parse("999999999999999.0.0.0", &bad));
  EXPECT(!ChromeVersion::Parse("156.0.8078.80.1", &bad));

  // Real data seen 6 Nov 2026: 156.0.8078.80 still served beside 157.
  auto r = EvaluateVersion(ours, {{V("157.0.8103.39"), 2}, {V("156.0.8078.80"), 0}}, 6, false);
  EXPECT(r.verdict == VersionVerdict::kCurrent && !r.block);

  // Same major, newer patch served: warn, keep working.
  r = EvaluateVersion(ours, {{V("156.0.8078.95"), 3}}, 10, false);
  EXPECT(r.verdict == VersionVerdict::kUpdateAvailable && !r.block);

  // Major no longer served, inside the grace period: warn only.
  r = EvaluateVersion(ours, {{V("157.0.8103.39"), 10}}, 20, false);
  EXPECT(r.verdict == VersionVerdict::kUnsupported && !r.block);

  // ...past the grace period: block web pages.
  r = EvaluateVersion(ours, {{V("157.0.8103.39"), 30}}, 40, false);
  EXPECT(r.verdict == VersionVerdict::kUnsupported && r.block);

  // Two or more majors behind stable blocks even when startTime looks recent
  // (it restarts with each rollout step; seen live on 6 Nov with stable 158).
  r = EvaluateVersion(V("140.0.8078.80"), {{V("158.0.8127.18"), 1}, {V("156.0.8078.80"), 0}}, 6,
                      false);
  EXPECT(r.verdict == VersionVerdict::kUnsupported && r.block);

  // Explicit developer override never blocks.
  r = EvaluateVersion(ours, {{V("157.0.8103.39"), 30}}, 40, true);
  EXPECT(!r.block);

  // Offline: allowed while young, blocked when the build is old.
  EXPECT(!EvaluateVersion(ours, {}, 30, false).block);
  EXPECT(EvaluateVersion(ours, {}, 61, false).block);

  // Dev build newer than stable is current.
  EXPECT(EvaluateVersion(V("158.0.1.1"), {{V("157.0.8103.39"), 2}}, 1, false).verdict ==
         VersionVerdict::kCurrent);

  EXPECT(DaysSince("2026-10-31", 1793923200LL) == 6);  // 2026-11-06T00:00Z
  printf(failures ? "version policy: %d FAILED\n" : "version policy: ok\n", failures);
  return failures ? 1 : 0;
}
