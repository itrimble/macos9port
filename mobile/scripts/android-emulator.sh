#!/bin/sh
# Foldable emulators for OS999 (Apple silicon). Run from the repository root.
#   sh mobile/scripts/android-emulator.sh setup     install emulator + image, create AVDs
#   sh mobile/scripts/android-emulator.sh start [avd] [--headless]
#   sh mobile/scripts/android-emulator.sh install   build and install the debug APK, launch it
#   sh mobile/scripts/android-emulator.sh posture closed|half|open
# AVDs: os999_pixel_10_pro_fold (book-style), os999_flip (6.7in clamshell),
# os999_pixel_tablet (tablet; attach a keyboard and mouse from the emulator's controls).
set -eu
export PATH=/opt/homebrew/bin:$PATH
export JAVA_HOME=${JAVA_HOME:-$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home}
SDK=${ANDROID_HOME:-$HOME/Library/Android/sdk}
export ANDROID_HOME=$SDK ANDROID_SDK_ROOT=$SDK
IMAGE="system-images;android-36;google_apis;arm64-v8a"
ADB=$SDK/platform-tools/adb
EMU=$SDK/emulator/emulator

# Homebrew's avdmanager only sees its own SDK folder, so use the copy
# installed inside $SDK (setup puts it there).
AVDMANAGER=$SDK/cmdline-tools/latest/bin/avdmanager

case "${1:-}" in
setup)
    yes | sdkmanager --sdk_root="$SDK" --licenses >/dev/null
    sdkmanager --sdk_root="$SDK" --install "cmdline-tools;latest" emulator "$IMAGE"
    for pair in "os999_pixel_10_pro_fold:pixel_10_pro_fold" "os999_flip:6.7in Foldable" \
                "os999_pixel_tablet:pixel_tablet"; do
        name=${pair%%:*} device=${pair#*:}
        "$AVDMANAGER" list avd -c | grep -qx "$name" ||
            echo no | "$AVDMANAGER" create avd -n "$name" -k "$IMAGE" -d "$device"
    done
    ;;
start)
    avd=${2:-os999_pixel_10_pro_fold}
    extra=""
    [ "${3:-}" = "--headless" ] && extra="-no-window"
    nohup "$EMU" -avd "$avd" -no-snapshot-save -no-boot-anim -gpu host $extra >"build/mobile/emulator-$avd.log" 2>&1 &
    "$ADB" wait-for-device
    until [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 2; done
    echo "$avd booted"
    ;;
install)
    (cd mobile/android && ./gradlew -q :app:assembleDebug)
    "$ADB" install -r mobile/android/app/build/outputs/apk/debug/app-debug.apk
    "$ADB" shell am start -n org.remnantsecurity.os999/.OS999Activity
    ;;
posture)
    # Emulator console postures: 1 closed, 2 half-open, 3 open.
    case "${2:-}" in closed) p=1 ;; half) p=2 ;; open) p=3 ;; *) echo "closed|half|open"; exit 2 ;; esac
    "$ADB" emu posture "$p"
    ;;
*)
    sed -n '2,8p' "$0"
    exit 2
    ;;
esac
