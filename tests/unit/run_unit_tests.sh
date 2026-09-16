#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${QUETZ_TEST_BUILD:-$root/tests/unit/_build}"
cmake -S "$root" -B "$build" -DQUETZ_BUILD_RUNTIME=OFF -DSST_ROOT="${SST_HOME:-/opt/sst}"
cmake --build "$build" --parallel "${JOBS:-2}"
ctest --test-dir "$build" --output-on-failure
