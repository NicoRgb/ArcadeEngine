#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
preset=${1:-debug}
if command -v python3 >/dev/null 2>&1; then
    python3 scripts/ci.py test --preset "$preset"
else
    python scripts/ci.py test --preset "$preset"
fi
