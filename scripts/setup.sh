#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
if command -v python3 >/dev/null 2>&1; then
    python3 scripts/dev.py "$@"
else
    python scripts/dev.py "$@"
fi
