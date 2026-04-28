#!/usr/bin/env bash
#
# Download a prebuilt ACT4 ELF bundle and extract it under
# vendor_lib/riscv-arch-test/act4/work/cv32e20/elfs/.
#
# The bundle to fetch is identified by sim/.act4-elfs-pin (one line,
# fingerprint hex). The pin is updated by the prebuild workflow whenever
# a new bundle is generated.
#
# Source resolution order:
#   1. URL=<path-or-url> override (file:// or https://;
#      manifest expected at $URL.manifest.json)
#   2. gh release download elfs-<pin> (needs `gh` and auth)
#
# Exits 2 if no release matches the pin.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEST="${DEST:-$REPO_ROOT/vendor_lib/riscv-arch-test/act4/work/cv32e20/elfs}"
PIN_FILE="$REPO_ROOT/sim/.act4-elfs-pin"
URL="${URL:-}"

die() { echo "download_prebuilt_elfs: $*" >&2; exit "${2:-1}"; }

[ -f "$PIN_FILE" ] || die "missing pin file $PIN_FILE"
FP="$(tr -d '[:space:]' < "$PIN_FILE")"
[ -n "$FP" ] || die "pin file $PIN_FILE is empty"

TAG="elfs-$FP"
TARBALL_NAME="act4-elfs-cv32e20-$FP.tar.zst"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

TARBALL="$WORKDIR/$TARBALL_NAME"
MANIFEST="$WORKDIR/$TARBALL_NAME.manifest.json"

fetch_one() {
    local src="$1" out="$2"
    case "$src" in
        file://*) cp "${src#file://}" "$out" ;;
        http://*|https://*) curl -fsSL "$src" -o "$out" ;;
        *) cp "$src" "$out" ;;
    esac
}

if [ -n "$URL" ]; then
    echo "Fetching bundle from $URL"
    fetch_one "$URL" "$TARBALL" || die "fetch failed: $URL"
    fetch_one "$URL.manifest.json" "$MANIFEST" || die "fetch failed: $URL.manifest.json"
else
    command -v gh >/dev/null 2>&1 || die "'gh' CLI not found and URL not set"
    if ! gh release view "$TAG" >/dev/null 2>&1; then
        die "no release '$TAG' found. Bump sim/.act4-elfs-pin or trigger prebuild." 2
    fi
    gh release download "$TAG" \
        --pattern "$TARBALL_NAME" \
        --pattern "$TARBALL_NAME.manifest.json" \
        --dir "$WORKDIR"
fi

[ -f "$TARBALL" ]  || die "missing tarball after fetch"
[ -f "$MANIFEST" ] || die "missing manifest after fetch"

EXPECTED_SHA="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sha256"])' "$MANIFEST")"
ACTUAL_SHA="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
if [ "$EXPECTED_SHA" != "$ACTUAL_SHA" ]; then
    die "sha256 mismatch: expected $EXPECTED_SHA, got $ACTUAL_SHA"
fi

# The tarball stores `work/cv32e20/elfs/...`. Extract into the directory
# three levels above $DEST so that layout lands at the canonical path.
EXTRACT_ROOT="${DEST%/work/cv32e20/elfs}"
if [ "$EXTRACT_ROOT" = "$DEST" ]; then
    die "DEST must end with /work/cv32e20/elfs, got: $DEST"
fi
mkdir -p "$EXTRACT_ROOT"
rm -rf "$DEST"
zstd -dcf "$TARBALL" | tar -xf - -C "$EXTRACT_ROOT"

echo "Extracted to: $DEST"
echo "Pin:          $FP"
