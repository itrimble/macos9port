#!/bin/sh
# Install what the OS999 Android build needs on a Mac, without Android Studio:
#   JDK 17 (Homebrew openjdk@17), Android command-line tools, Gradle,
#   SDK platform 36, build-tools 36.0.0, platform-tools, NDK r28c, CMake 3.22.1.
# Then writes mobile/android/local.properties and the Gradle wrapper.
# Run from the repository root: sh mobile/scripts/install-android-sdk.sh
# Accepts the Android SDK licences on your behalf (sdkmanager --licenses).
set -eu
export PATH=/opt/homebrew/bin:$PATH
SDK=${ANDROID_HOME:-$HOME/Library/Android/sdk}
NDK=28.2.13676358
GRADLE_WRAPPER=8.14.3

brew list --versions openjdk@17 >/dev/null || brew install openjdk@17
export JAVA_HOME=$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home
brew list --cask --versions android-commandlinetools >/dev/null || brew install --cask android-commandlinetools
brew list --versions gradle >/dev/null || brew install gradle

SDKMANAGER=$(brew --prefix)/bin/sdkmanager
mkdir -p "$SDK"
yes | "$SDKMANAGER" --sdk_root="$SDK" --licenses >/dev/null
"$SDKMANAGER" --sdk_root="$SDK" --install \
    "platform-tools" "platforms;android-36" "build-tools;36.0.0" "ndk;$NDK" "cmake;3.22.1"

printf 'sdk.dir=%s\n' "$SDK" > mobile/android/local.properties

# Generate the wrapper outside the project so the newer Homebrew Gradle never
# has to evaluate the Android build.
if [ ! -x mobile/android/gradlew ]; then
    tmp=$(mktemp -d)
    (cd "$tmp" && touch settings.gradle && gradle -q wrapper --gradle-version "$GRADLE_WRAPPER")
    cp -R "$tmp/gradlew" "$tmp/gradlew.bat" "$tmp/gradle" mobile/android/
    rm -rf "$tmp"
fi
echo "Android SDK ready at $SDK (JAVA_HOME=$JAVA_HOME)"
echo "Build: cd mobile/android && JAVA_HOME=$JAVA_HOME ./gradlew :app:assembleDebug"
