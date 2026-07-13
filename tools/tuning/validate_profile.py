#!/usr/bin/env python3
"""Validate a local tuning profile against registry, schema, and defaults."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

from canonical_json import CanonicalJsonError, is_hash_string, profile_hash, require_canonical_rational
from profile_schema import (
    ENGINE_VERSION,
    ENUM_PARAMETERS,
    MODEL_VERSION,
    PROFILE_ID,
    PROFILE_SCHEMA_VERSION,
    ProfileError,
    RATIONAL_PARAMETERS,
    expected_profile_parameters,
    load_registry,
    load_schema,
    registry_parameter_names,
    validate_cpp_mapping_against_registry,
)


class ProfileValidationError(Exception):
    pass


def load_profile(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _validate_top_level(profile: dict[str, Any]) -> None:
    required = {
        "schemaVersion",
        "profileId",
        "parentProfileId",
        "description",
        "engineVersion",
        "sourceBaseline",
        "registryVersion",
        "modelVersion",
        "parameters",
        "canonicalHash",
    }
    if set(profile) != required:
        raise ProfileValidationError(f"top-level fields mismatch: got {sorted(profile)}, expected {sorted(required)}")
    if profile["schemaVersion"] != PROFILE_SCHEMA_VERSION:
        raise ProfileValidationError("schemaVersion must be 1")
    if not isinstance(profile["profileId"], str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]*", profile["profileId"]):
        raise ProfileValidationError("profileId must be a stable lowercase identifier")
    if profile["profileId"] == PROFILE_ID:
        if profile["parentProfileId"] is not None:
            raise ProfileValidationError("builtin-default-v1 parentProfileId must be null")
    elif not isinstance(profile["parentProfileId"], str) or not profile["parentProfileId"]:
        raise ProfileValidationError("candidate profiles must declare a parentProfileId")
    if profile["engineVersion"] != ENGINE_VERSION:
        raise ProfileValidationError("engineVersion must be deterministic compatibility version 1")
    if not isinstance(profile["sourceBaseline"], str) or not profile["sourceBaseline"]:
        raise ProfileValidationError("sourceBaseline must be a non-empty string")
    if profile["registryVersion"] != 1:
        raise ProfileValidationError("registryVersion must be 1")
    if profile["modelVersion"] != MODEL_VERSION:
        raise ProfileValidationError(f"modelVersion must be {MODEL_VERSION}")
    if not isinstance(profile["description"], str) or not profile["description"]:
        raise ProfileValidationError("description must be a non-empty string")
    if not isinstance(profile["parameters"], dict):
        raise ProfileValidationError("parameters must be an object")
    if not is_hash_string(profile["canonicalHash"]):
        raise ProfileValidationError("canonicalHash must be sha256:<64 lowercase hex chars>")


def _validate_value_shape(name: str, value: Any, registry_param: dict[str, Any]) -> None:
    if name in RATIONAL_PARAMETERS:
        try:
            require_canonical_rational(value)
        except CanonicalJsonError as exc:
            raise ProfileValidationError(f"{name}: {exc}") from exc
        return
    if name in ENUM_PARAMETERS:
        if value not in ENUM_PARAMETERS[name]:
            raise ProfileValidationError(f"{name}: unknown enum value {value!r}")
        return

    typ = registry_param["type"]
    if typ == "integer":
        if not _is_int(value):
            raise ProfileValidationError(f"{name}: expected integer")
    elif typ == "unsigned integer":
        if not _is_int(value) or value < 0:
            raise ProfileValidationError(f"{name}: expected unsigned integer")
    elif typ == "boolean":
        if not isinstance(value, bool):
            raise ProfileValidationError(f"{name}: expected boolean")
    elif typ == "integer array":
        if not _array_all_ints(value):
            raise ProfileValidationError(f"{name}: expected integer array")
        dims = registry_param.get("array", {}).get("dimensions")
        if dims and not _matches_dimensions(value, dims):
            raise ProfileValidationError(f"{name}: expected dimensions {dims}")
    else:
        raise ProfileValidationError(f"{name}: unsupported type {typ}")


def _array_all_ints(value: Any) -> bool:
    if not isinstance(value, list):
        return False
    for item in value:
        if isinstance(item, list):
            if not _array_all_ints(item):
                return False
        elif not _is_int(item):
            return False
    return True


def _matches_dimensions(value: Any, dims: list[int]) -> bool:
    if not dims:
        return not isinstance(value, list)
    if not isinstance(value, list) or len(value) != dims[0]:
        return False
    return all(_matches_dimensions(item, dims[1:]) for item in value)


def _flatten_array(value: Any) -> list[int]:
    if isinstance(value, list):
        flat: list[int] = []
        for item in value:
            flat.extend(_flatten_array(item))
        return flat
    return [value]


def _validate_bounds(name: str, value: Any, registry_param: dict[str, Any]) -> None:
    if registry_param["boundsStatus"] == "unknown":
        return
    if name in RATIONAL_PARAMETERS:
        return
    minimum = registry_param["minimum"]
    maximum = registry_param["maximum"]
    if isinstance(value, list):
        flat = _flatten_array(value)
        mins = minimum if isinstance(minimum, list) else [minimum] * len(flat)
        maxs = maximum if isinstance(maximum, list) else [maximum] * len(flat)
        if len(mins) != len(flat) or len(maxs) != len(flat):
            raise ProfileValidationError(f"{name}: bound dimensions do not match value")
        for idx, item in enumerate(flat):
            if item < mins[idx] or item > maxs[idx]:
                raise ProfileValidationError(f"{name}[{idx}]={item} outside [{mins[idx]}, {maxs[idx]}]")
    elif _is_int(value):
        if value < minimum or value > maximum:
            raise ProfileValidationError(f"{name}={value} outside [{minimum}, {maximum}]")


def validate_profile(profile: dict[str, Any], registry_path: Path = Path("tuning/parameter-registry.json")) -> dict[str, int]:
    schema = load_schema()
    if schema.get("properties", {}).get("schemaVersion", {}).get("const") != PROFILE_SCHEMA_VERSION:
        raise ProfileValidationError("schema file does not declare schemaVersion const 1")

    registry = load_registry(registry_path)
    validate_cpp_mapping_against_registry(registry)
    expected_names = registry_parameter_names(registry)
    registry_by_name = {param["name"]: param for param in registry["parameters"]}

    _validate_top_level(profile)
    if profile_hash(profile) != profile["canonicalHash"]:
        raise ProfileValidationError("canonicalHash is stale or incorrect")

    names = sorted(profile["parameters"].keys())
    missing = sorted(set(expected_names) - set(names))
    extra = sorted(set(names) - set(expected_names))
    if missing:
        raise ProfileValidationError(f"missing profile parameters: {missing}")
    if extra:
        raise ProfileValidationError(f"unknown profile parameters: {extra}")
    if "eval.tempo" in names:
        raise ProfileValidationError("eval.tempo must not appear in profile")

    expected_values = expected_profile_parameters(registry)
    value_mismatches = 0
    type_mismatches = 0
    dimension_mismatches = 0

    for name in expected_names:
        value = profile["parameters"][name]
        param = registry_by_name[name]
        try:
            _validate_value_shape(name, value, param)
        except ProfileValidationError:
            if param["type"] == "integer array":
                dimension_mismatches += 1
            else:
                type_mismatches += 1
            raise
        _validate_bounds(name, value, param)
        if value != expected_values[name]:
            value_mismatches += 1
            raise ProfileValidationError(f"{name}: value mismatch profile={value!r} expected={expected_values[name]!r}")

    if profile["parameters"]["opening.selectionTopN"] < 1:
        raise ProfileValidationError("opening.selectionTopN must be nonzero")
    if profile["parameters"]["evaluation.material.mg"][5] != 0 or profile["parameters"]["evaluation.material.eg"][5] != 0:
        raise ProfileValidationError("king material sentinels must remain 0")

    return {
        "registry_entries": len(expected_names),
        "typed_model_entries": len(expected_names),
        "profile_entries": len(names),
        "missing_profile_entries": len(missing),
        "extra_profile_entries": len(extra),
        "value_mismatches": value_mismatches,
        "type_mismatches": type_mismatches,
        "dimension_mismatches": dimension_mismatches,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("--registry", type=Path, default=Path("tuning/parameter-registry.json"))
    args = parser.parse_args()

    try:
        profile = load_profile(args.profile)
        stats = validate_profile(profile, args.registry)
    except (OSError, json.JSONDecodeError, ProfileError, ProfileValidationError) as exc:
        print(f"profile validation failed: {exc}", file=sys.stderr)
        return 1

    print("profile validation passed")
    print(
        "registry entries: {registry_entries}, typed model entries: {typed_model_entries}, "
        "profile entries: {profile_entries}, missing profile entries: {missing_profile_entries}, "
        "extra profile entries: {extra_profile_entries}, value mismatches: {value_mismatches}, "
        "type mismatches: {type_mismatches}, dimension mismatches: {dimension_mismatches}".format(**stats)
    )
    print(f"canonical hash: {profile['canonicalHash']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
