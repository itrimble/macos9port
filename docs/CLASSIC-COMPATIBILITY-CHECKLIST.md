# Classic compatibility additions — 7 November 2026

Part of the [development checklist](DEVELOPMENT-CHECKLIST.md). These are
requirements, not claims of complete Mac OS 9.2 compatibility. Existing runtime
implementations must be audited and extended rather than duplicated. Record
source revision, tests, real application evidence, and remaining limitations
separately for each item. Preserve the project's clean-room boundaries.

- [ ] CC01 Cooperative scheduling and event dispatch: implement and verify
  WaitNextEvent yields, event ordering, sleep/wakeup, and background-task
  fairness. Bound guest execution even when an application does not yield;
  Chromium and other host components must remain responsive. Test busy guests,
  idle guests, timers, input latency, and multi-application scheduling.
- [ ] CC02 QuickDraw and GrafPort rendering: complete port state, BitMap and
  PixMap handling, region clipping, offscreen drawing, fonts, and framebuffer
  presentation. Preserve classic pixel semantics with explicit scaling options.
  Measure rendering and composition costs; distinguish software rendering from
  verified GPU acceleration. Test overlapping ports, complex regions, palette
  changes, and representative applications.
- [ ] CC03 Apple Events and inter-application communication: implement typed
  descriptors, application addressing, launch/open-document events, dispatch,
  replies, timeouts, and errors. Map guest messaging onto a bounded host IPC
  transport without losing descriptor semantics. Validate target APIs such as
  AESend and AEProcessAppleEvent against public interfaces; investigate the
  requested AEProcessMessage name before treating it as a supported API.
  AppleScript execution is a separate compatibility gate, not implied by IPC.
- [ ] CC04 Font suitcases: parse user-supplied FONT, NFNT, and sfnt resources
  with bounds checks, resource identity, metrics, styles, and fallback handling.
  Integrate bitmap and outline rendering with the existing font cache. Test
  malformed suitcases and legacy point sizes. User-supplied Chicago, Geneva,
  or Monaco resources are compatibility inputs only; do not bundle Apple fonts
  or claim Adobe Type Manager compatibility from font parsing alone.
- [ ] CC05 Extensions and INIT resources: audit the guest patching/runtime
  prerequisites, then implement an opt-in loader with version checks,
  deterministic ordering, dependency diagnostics, and failure recovery. Add a
  spacebar-accessible extension-management overlay and a disabled-extension
  startup path. Coordinate with the existing startup-sequence producer.
  Isolate faults from the host; arbitrary guest trap patches cannot be assumed
  independently safe. Test conflicting extensions, failed initialization, and
  recovery without modifying the user's working volume.
- [ ] CC06 Storage and installer integration: preserve data/resource forks and
  Finder metadata through StuffIt and MacBinary extraction; expose supported
  disk-image formats through contained disposable volumes; validate file type,
  structure, and execution architecture before dispatch. A classic type/creator
  signature identifies a file, not its authenticity. Test traversal, symlinks,
  malformed archives, quotas, rollback, fork persistence, and launch selection.
  Development owner: isolated `storageinstallers` worktree on
  `feature/storage-installers`; implementation and acceptance remain pending.
