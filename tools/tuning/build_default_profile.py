#!/usr/bin/env python3
"""Build the immutable builtin-default-v1 local tuning profile."""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

from canonical_json import pretty_json, profile_hash
from profile_schema import (
    ENGINE_VERSION,
    MODEL_VERSION,
    PROFILE_ID,
    PROFILE_SCHEMA_VERSION,
    expected_profile_parameters,
    load_registry,
    validate_cpp_mapping_against_registry,
)
from validate_profile import validate_profile


def build_profile(registry_path: Path) -> dict:
    registry = load_registry(registry_path)
    validate_cpp_mapping_against_registry(registry)
    profile = {
        "schemaVersion": PROFILE_SCHEMA_VERSION,
        "profileId": PROFILE_ID,
        "parentProfileId": None,
        "description": "Canonical production-equivalent default profile.",
        "engineVersion": ENGINE_VERSION,
        "sourceBaseline": PROFILE_ID,
        "registryVersion": registry["schemaVersion"],
        "modelVersion": MODEL_VERSION,
        "parameters": expected_profile_parameters(registry),
        "canonicalHash": "",
    }
    profile["canonicalHash"] = profile_hash(profile)
    validate_profile(profile, registry_path)
    return profile


def write_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as handle:
        tmp_name = handle.name
        handle.write(text)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp_name, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--force-development-rebuild",
        action="store_true",
        help="Allow replacing builtin-default-v1 when the existing canonical content differs.",
    )
    args = parser.parse_args()

    try:
        profile = build_profile(args.registry)
        output_text = pretty_json(profile)

        if args.output.exists():
            existing_text = args.output.read_text(encoding="utf-8")
            existing = json.loads(existing_text)
            existing_hash = profile_hash(existing)
            if existing_hash == profile["canonicalHash"]:
                print(f"default profile already up to date: {args.output}")
                print(f"canonical hash: {profile['canonicalHash']}")
                return 0
            if not args.force_development_rebuild:
                print(
                    "refusing to overwrite immutable builtin-default-v1 with different canonical content; "
                    "use --force-development-rebuild only while intentionally rebuilding the baseline",
                    file=sys.stderr,
                )
                return 1

        write_atomic(args.output, output_text)
    except Exception as exc:
        print(f"default profile build failed: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {args.output}")
    print(f"canonical hash: {profile['canonicalHash']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
