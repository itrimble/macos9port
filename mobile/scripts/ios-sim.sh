#!/bin/sh
# Build OS999 for the iOS Simulator and launch it. Run from the repository root.
#   sh mobile/scripts/ios-sim.sh ["iPhone Air"]
set -eu
DEVICE=${1:-"iPhone Air"}
sh mobile/scripts/fetch-deps.sh
[ "$(uname -s)" = Darwin ] && make -s wallpapers
cmake -S mobile -B build/mobile/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 >/dev/null
xcodebuild -project build/mobile/ios/OS999Mobile.xcodeproj -scheme OS999 -configuration Debug \
    -sdk iphonesimulator -destination "generic/platform=iOS Simulator" \
    -derivedDataPath build/mobile/ios/dd ARCHS=arm64 CODE_SIGNING_ALLOWED=NO -quiet build
APP=$(find build/mobile/ios/dd/Build/Products -name OS999.app -maxdepth 3 | head -1)
xcrun simctl boot "$DEVICE" 2>/dev/null || true
open -a Simulator
xcrun simctl install "$DEVICE" "$APP"
xcrun simctl launch "$DEVICE" org.remnantsecurity.os999.mobile
