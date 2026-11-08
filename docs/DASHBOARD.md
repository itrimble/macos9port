# OS999 development dashboard

Open http://127.0.0.1:8080 on this Mac. The Python service listens only on loopback.
It reads project evidence every three seconds and sends changes through server-sent
events. The browser also retries HTTP reads after a connection interruption.

The default view shows tracked acceptance and component status. Navigation opens
milestones and their Mermaid dependency chart, application compatibility,
performance, System Folder anatomy, or numbered builds. The sidebar searches and
filters features; expanding one exposes its recorded evidence.

## Live sources

- `docs/DEVELOPMENT-CHECKLIST.md`: checked acceptance gates. An unchecked gate
  cannot become accepted merely through an implementation override.
- `config/dashboard.json`: extra parity tasks, implementation evidence, component
  groups, milestones, app observations, benchmarks and anatomy membership.
- Git: current branch, main revision, changed paths and source worktrees.
- `~/Library/Application Support/OS999/Integration`: queue entries, worker polling
  status and the latest passing integration receipt.

The current catalog contains 160 gates. It is an explicit working catalog, not a
claim that every Mac OS 9.2 behavior has been enumerated or implemented. Unknown
compatibility values and unmeasured benchmarks remain empty. Complexity scores
are planning estimates. The roadmap wave means monitoring is active; it does not
measure development speed. Undated Gantt slots express ordering, not forecasts.
System Folder rectangles represent tracked behavior counts, not installed bytes.

A benchmark measurement must include a nonnegative numeric `value`, `hardware`,
`date`, and `source`. Record equivalent workloads on each system before comparing
results. Example measurement shape (replace every placeholder with actual evidence):

```json
{"value": 0, "hardware": "record tested hardware", "date": "YYYY-MM-DD",
 "source": "path to measurement receipt"}
```

Do not use this illustrative zero as a measured result.

## Build automation

A milestone can declare an exact `ready_commit` and `depends_on_commits`. Once all
its task IDs are accepted, the dashboard submits that revision to the integration
watcher. The watcher allocates ascending build numbers, retains a descriptive
milestone title, creates an isolated candidate, runs the gate, and promotes only
with a matching passing receipt. A dirty main checkout blocks promotion; failed
candidates and logs remain available for repair. No public remote push is involved.

An online worker is not a passing build. Read its waiting/error status and the
actual numbered receipt. Build 8 and initial local-main promotion passed; the exact receipt remains
separate from dashboard service tests and GUI/gameplay acceptance.

## Running and maintenance

```sh
sh scripts/prepare-dashboard.sh
BUILDFLEET_LOCAL=1 python3 scripts/os999-dashboard.py --port 8080
BUILDFLEET_LOCAL=1 python3 -m unittest discover -s tests -p 'test_dashboard*.py'
```

The Mermaid bundle and build dependencies live under ignored `build/`. Its exact
versions are pinned in `config/dashboard-tools/`; fonts and chart code load locally,
with no runtime CDN. The installed login service is
`com.remnantsecurity.os999.dashboard`; its logs are in `build-logs/`.

Data/service tests cover live revision changes, event-stream delivery, invalid
configuration recovery, source allowlisting, traversal rejection and foreign Host
rejection. Ian approved the desktop appearance as an MVP on 7 November 2026.
SnapDog capture work runs on the Mac mini; mobile layout and exhaustive
interactive acceptance remain unverified. The service now reads canonical main.
