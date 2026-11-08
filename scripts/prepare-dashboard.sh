#!/bin/sh
# Pinned, local-only Mermaid bundle. Runtime does not contact a CDN.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/dashboard-tools build/dashboard-web
cp config/dashboard-tools/package.json config/dashboard-tools/package-lock.json build/dashboard-tools/
npm ci --prefix build/dashboard-tools --ignore-scripts --no-audit --no-fund
printf "export {default} from 'mermaid';\n" > build/dashboard-tools/entry.js
build/dashboard-tools/node_modules/.bin/esbuild build/dashboard-tools/entry.js \
    --bundle --format=esm --minify --outfile=build/dashboard-web/mermaid.js
