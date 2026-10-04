#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
if command -v python3 >/dev/null 2>&1; then
    python3 scripts/ci.py lint
else
    python scripts/ci.py lint
fi
