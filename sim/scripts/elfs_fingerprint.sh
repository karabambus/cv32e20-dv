#!/usr/bin/env bash
#
# Compute a 12-char SHA256 prefix identifying a prebuilt ELF bundle by
# the inputs that determine its content. Used as the GitHub Release tag
# (`elfs-<fingerprint>`) and recorded in sim/.act4-elfs-pin.
#
# Inputs hashed (in order):
#   1. ACT4_HASH from sim/ExternalRepos.mk
#   2. The full body of the `gen:` recipe in sim/core/Makefile
#   3. `sail_riscv_sim --version`
#
# Output: a single 12-char hex string on stdout.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

EXTERNAL_REPOS_MK="$REPO_ROOT/sim/ExternalRepos.mk"
CORE_MAKEFILE="$REPO_ROOT/sim/core/Makefile"

die() { echo "elfs_fingerprint: $*" >&2; exit 1; }

[ -f "$EXTERNAL_REPOS_MK" ] || die "missing $EXTERNAL_REPOS_MK"
[ -f "$CORE_MAKEFILE" ]     || die "missing $CORE_MAKEFILE"

# 1. ACT4_HASH (the value assigned in ExternalRepos.mk).
act4_hash="$(grep -E '^ACT4_HASH[[:space:]]*\?=' "$EXTERNAL_REPOS_MK" \
             | head -n1 | sed -E 's/^ACT4_HASH[[:space:]]*\?=[[:space:]]*//' \
             | tr -d '[:space:]')"
[ -n "$act4_hash" ] || die "could not parse ACT4_HASH from $EXTERNAL_REPOS_MK"

# 2. Full body of the `gen:` recipe (every line until the next blank line).
#    Captures EXTENSIONS / EXCLUDE_EXTENSIONS, CONFIG_FILES, and the clean step.
gen_recipe="$(awk '
    /^gen:/        { in_target = 1; next }
    in_target && /^$/ { exit }
    in_target      { print }
' "$CORE_MAKEFILE")"
[ -n "$gen_recipe" ] || die "could not extract gen: recipe from $CORE_MAKEFILE"

# 3. Sail version.
command -v sail_riscv_sim >/dev/null 2>&1 || die "sail_riscv_sim not on PATH"
sail_version="$(sail_riscv_sim --version 2>&1 | head -n1)"

{
    printf 'ACT4_HASH=%s\n' "$act4_hash"
    printf '=== gen recipe ===\n'
    printf '%s\n' "$gen_recipe"
    printf 'SAIL=%s\n' "$sail_version"
} | sha256sum | cut -c1-12
