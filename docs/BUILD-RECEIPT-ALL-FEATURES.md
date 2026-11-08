# OS999 all-features build receipt

- Commit: 8313cf9 on integration/all-features (11 commits over release e66a061)
- Built: 2026-11-06 19:35 CST on macOS 26.6.2, arm64, Apple clang version 21.0.0 (clang-2100.4.1.1)
- Dirty files at build: 1
- Commands: make -j10 -k test all; make test-network test-tailnet; make app APP_BUNDLE=\"build/OS999 All Features.app\"; sh browser/scripts/build.sh (ad-hoc signed); make -f mobile/mobile.mk test; make -f keyboard/keyboard.mk test
- Results: all exit 0 (logs in build-logs/fulltest10.log, browser-build.log, mobile-test.log)
- Desktop test programs run: 70
- Smoke launch: app ran 8 s against build/smoke-volume and drew the desktop (build/smoke-desktop.png)

## Artifact hashes (sha256)
- 4d88afdb1a32914d  OS999 All Features.app/Contents/MacOS/ospi
- baee017972d57a4d  OS999 All Features.app/Contents/MacOS/ospi-runtime
- 46a0542c362d8bb5  OS999 All Features.app/Contents/MacOS/os999-tailnet
- 99060cc2c9d53607  OS999 All Features.app/Contents/Resources/Quake3/quake3
- 91e1d0d5683488d6  browser/Release/OS999Browser.app/Contents/MacOS/OS999Browser
