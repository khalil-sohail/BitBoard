#!/usr/bin/env python3
"""Print the canonical SHA-256 hash for a local tuning profile."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from canonical_json import profile_hash


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    args = parser.parse_args()

    try:
        with args.profile.open("r", encoding="utf-8") as handle:
            profile = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"profile hash failed: {exc}", file=sys.stderr)
        return 1

    print(profile_hash(profile))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
