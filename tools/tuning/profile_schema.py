#!/usr/bin/env python3
"""Shared schema and value helpers for local tuning profiles."""

from __future__ import annotations

import copy
import json
import re
from pathlib import Path
from typing import Any

from validate_registry import (
    RegistryError,
    cpp_tuning_field_names,
    load_json,
    production_values,
    read_repo_file,
)


PROFILE_SCHEMA_VERSION = 1
PROFILE_ID = "builtin-default-v1"
ENGINE_VERSION = "1"
MODEL_VERSION = "phase-2-typed-model-v1"
SCHEMA_PATH = Path("tuning/schema/profile.schema.json")
DEFAULT_REGISTRY_PATH = Path("tuning/parameter-registry.json")
CANONICAL_DEFAULT_PROFILE_PATH = Path("tuning/profiles/builtin-default-v1.json")

RATIONAL_PARAMETERS = {
    "search.lmr.baseScaled100": {"numerator": 3, "denominator": 4},
    "time.hardStopFraction": {"numerator": 3, "denominator": 4},
    "time.instabilityMultiplierPermille": {"numerator": 13, "denominator": 10},
    "time.maxClockFractionDenominator": {"numerator": 1, "denominator": 4},
    "time.softStop.stablePercent": {"numerator": 1, "denominator": 2},
    "time.softStop.unstablePercent": {"numerator": 4, "denominator": 5},
}

ENUM_PARAMETERS = {
    "opening.selectionMode": {"best", "weighted", "top-n-weighted"},
}


class ProfileError(Exception):
    pass


def load_registry(path: Path = DEFAULT_REGISTRY_PATH) -> dict[str, Any]:
    return load_json(path)


def registry_parameter_names(registry: dict[str, Any]) -> list[str]:
    return [param["name"] for param in registry["parameters"]]


def production_mvv_lva_table() -> list[list[int]]:
    generated = read_repo_file("engine/include/tuning/generated_tuning_values.hpp")
    match = re.search(r"\.mvvLva\s*=\s*\{\{(?P<body>.*?)\}\},\n\s*\.seePieceValues", generated, re.DOTALL)
    if not match:
        raise ProfileError("Could not find generated MVV-LVA table")
    values = [int(item) for item in re.findall(r"-?\d+", match.group("body"))]
    if len(values) != 36:
        raise ProfileError(f"MVV_LVA has {len(values)} values, expected 36")
    return [values[index:index + 6] for index in range(0, 36, 6)]


def expected_profile_parameters(registry: dict[str, Any]) -> dict[str, Any]:
    values = production_values()
    values["search.ordering.mvvLva"] = production_mvv_lva_table()
    registry_defaults = {param["name"]: param["currentValue"] for param in registry["parameters"]}
    canonical_parameters = load_json(CANONICAL_DEFAULT_PROFILE_PATH)["parameters"]

    parameters: dict[str, Any] = {}
    for name in registry_parameter_names(registry):
        if name in RATIONAL_PARAMETERS:
            parameters[name] = dict(RATIONAL_PARAMETERS[name])
        elif name.startswith("evaluation."):
            value = registry_defaults[name]
            if isinstance(value, str) and value.startswith("source-table:"):
                value = canonical_parameters[name]
            parameters[name] = copy.deepcopy(value)
        elif name in values:
            parameters[name] = values[name]
        else:
            raise ProfileError(f"No default value source for {name}")
    return parameters


def validate_cpp_mapping_against_registry(registry: dict[str, Any]) -> None:
    registry_names = registry_parameter_names(registry)
    cpp_names = cpp_tuning_field_names()
    if cpp_names != registry_names:
        missing = sorted(set(registry_names) - set(cpp_names))
        unknown = sorted(set(cpp_names) - set(registry_names))
        raise ProfileError(f"C++ tuning metadata drift: missing={missing}, unknown={unknown}")


def load_schema(path: Path = SCHEMA_PATH) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)
