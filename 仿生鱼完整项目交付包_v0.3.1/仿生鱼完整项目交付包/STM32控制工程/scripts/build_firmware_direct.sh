#!/usr/bin/env bash
# Compatibility entry point. CMake owns all sources, options, and dependencies.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
exec "$PYTHON" "$ROOT/scripts/build.py" firmware "$@"
