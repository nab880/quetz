#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QEMU_VERSION=9.2.1
QEMU_SHA256=72874fe9c395ced0c7fd7c22c43744072697f7ee1926a72237bd81784b2faf62
CACHE_DIR="${SCRIPT_DIR}/cache"
ARCHIVE="${CACHE_DIR}/qemu-${QEMU_VERSION}.tar.xz"

file_sha256()
{
    if command -v sha256sum >/dev/null; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

mkdir -p "${CACHE_DIR}"
if [ -f "${ARCHIVE}" ] && [ "$(file_sha256 "${ARCHIVE}")" = "${QEMU_SHA256}" ]; then
    echo "Pinned QEMU source already cached: ${ARCHIVE}"
    exit 0
fi

PARTIAL="${ARCHIVE}.partial.$$"
trap 'rm -f "${PARTIAL}"' EXIT
curl --fail --location --retry 3 \
    "https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz" \
    --output "${PARTIAL}"
[ "$(file_sha256 "${PARTIAL}")" = "${QEMU_SHA256}" ] \
    || { echo "QEMU source checksum mismatch" >&2; exit 1; }
mv -f "${PARTIAL}" "${ARCHIVE}"
trap - EXIT
echo "Cached pinned QEMU source: ${ARCHIVE}"
