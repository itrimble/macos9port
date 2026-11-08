#!/bin/sh
# Runs INSIDE a Debian arm64/x86_64 container (see linux-container.sh):
# builds ospi and OS999Browser for Linux, then a headless smoke test under Xvfb.
set -eu
ARCH="${1:-linuxarm64}"
PROJECT_ARCH=$([ "$ARCH" = linux64 ] && echo x86_64 || echo arm64)
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends cmake ninja-build g++ make pkg-config \
  libsdl2-dev libfreetype-dev xvfb x11-apps imagemagick ca-certificates curl perl \
  libnss3 libatk1.0-0 libatk-bridge2.0-0 libcups2 libdrm2 libxkbcommon0 libxcomposite1 \
  libxdamage1 libxfixes3 libxrandr2 libgbm1 libpango-1.0-0 libcairo2 libasound2 \
  libcurl4-openssl-dev python3 python3-cryptography libdbus-1-3 libexpat1 libxshmfence1 fonts-dejavu-core >/dev/null
# Build from a private copy so the Mac's build/ tree is never touched.
rm -rf /work && mkdir -p /work && cd /src && tar --exclude=./build -cf - . | (cd /work && tar xf -)
mkdir -p /work/build/deps
cp -a /src/build/deps/Musashi-* /work/build/deps/ 2>/dev/null || true
ln -s "/src/build/deps/cef-$ARCH" "/work/build/deps/cef-$ARCH"
cd /work
make -j2 build/ospi
make test >/work/test.log 2>&1 && echo "make test: ok" || { echo "make test: FAILED"; tail -20 /work/test.log; exit 1; }
cmake -S browser -B build/browser -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCEF_ROOT="/work/build/deps/cef-$ARCH" -DPROJECT_ARCH="$PROJECT_ARCH" >/work/cmake.log
cmake --build build/browser --target OS999Browser version_policy_test --parallel 2
"$(find build/browser -name version_policy_test -type f -perm -u+x | head -1)"  # B06 policy tests
OUT="/src/build/linux-$PROJECT_ARCH"
rm -rf "$OUT" && mkdir -p "$OUT"
cp build/ospi "$OUT/ospi"
cp -a build/browser/Release "$OUT/os999-browser"
chmod -R a+rX "$OUT"
echo "artifacts in $OUT"

# Transport fixtures and embedded IPC tests run without a desktop or focus.
make test-network >"$OUT/network-tests.log" 2>&1
id tester >/dev/null 2>&1 || useradd -m tester
chmod -R a+rX /work
chmod a+w /work/build
su tester -c "cd /work && python3 tests/test_embedded_browser_security.py --binary /work/build/browser/Release/OS999Browser" >"$OUT/security-tests.log" 2>&1
cp /work/build/embedded-security-results.json "$OUT/"
su tester -c "cd /work && python3 tests/test_web_games.py /work/build/browser/Release/OS999Browser" >"$OUT/web-games-tests.log" 2>&1
cp /work/build/web-games-results.json "$OUT/"
# ---- Headless smoke test (not a performance or hardware-decode test) -------
id tester >/dev/null 2>&1 || useradd -m tester
cp /src/browser/scripts/smoke.html /tmp/smoke.html
chmod 644 /tmp/smoke.html
Xvfb :99 -screen 0 1024x768x24 >/tmp/xvfb.log 2>&1 &
sleep 2
su tester -c "cd /tmp && DISPLAY=:99 SDL_RENDER_DRIVER=software OSPI_BOOT_ANIMATION=0 \
  OSPI_BROWSER_URL=https://example.com OSPI_BROWSER_LOG=/tmp/browser.log \
  timeout 40 /work/build/ospi --volume /tmp/vol >/tmp/ospi.log 2>&1 &"
sleep 30
DISPLAY=:99 import -window root "$OUT/smoke.png" || true
sleep 12
cp /tmp/browser.log /tmp/ospi.log "$OUT/" 2>/dev/null || true
ls /tmp/vol/Downloads 2>/dev/null | sed 's/^/download: /' || true
echo "smoke test done"
