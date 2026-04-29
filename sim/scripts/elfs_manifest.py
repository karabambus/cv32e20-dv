#!/usr/bin/env python3
"""Emit a manifest JSON for a prebuilt ELF tarball.

Usage: elfs_manifest.py <tarball-path> <fingerprint>
Stdout: JSON with sha256, size, file_count, fingerprint, generated_at, act4_hash.
"""
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys


def parse_act4_hash(repo_root: str) -> str:
    mk = os.path.join(repo_root, "sim", "ExternalRepos.mk")
    with open(mk) as f:
        for line in f:
            m = re.match(r"^ACT4_HASH\s*\?=\s*(\S+)", line)
            if m:
                return m.group(1)
    raise RuntimeError(f"could not parse ACT4_HASH from {mk}")


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: elfs_manifest.py <tarball-path> <fingerprint>", file=sys.stderr)
        sys.exit(2)

    tarball, fingerprint = sys.argv[1], sys.argv[2]
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

    h = hashlib.sha256()
    with open(tarball, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)

    listing = subprocess.run(
        ["tar", "-tzf", tarball],
        check=True, capture_output=True, text=True,
    ).stdout.splitlines()
    file_count = sum(1 for line in listing if not line.endswith("/"))

    manifest = {
        "fingerprint": fingerprint,
        "tarball": os.path.basename(tarball),
        "sha256": h.hexdigest(),
        "size": os.path.getsize(tarball),
        "file_count": file_count,
        "act4_hash": parse_act4_hash(repo_root),
        "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    }
    json.dump(manifest, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
