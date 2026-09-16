#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${ROOT}/runtime/fetch-qemu.sh"
exec docker build --target "${1:-runtime}" -t "${QUETZ_IMAGE:-quetz-sim}" \
    --build-arg "QUETZ_REVISION=$(git -C "${ROOT}" rev-parse HEAD)" \
    --build-arg "BUILD_JOBS=${BUILD_JOBS:-2}" \
    -f "${ROOT}/runtime/Dockerfile" "${ROOT}"
