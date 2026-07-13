#!/usr/bin/env python3
"""Canonical JSON and hashing helpers for local tuning profiles."""

from __future__ import annotations

import copy
import hashlib
import json
import math
from typing import Any


HASH_PREFIX = "sha256:"


class CanonicalJsonError(Exception):
    pass


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def pretty_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False, allow_nan=False) + "\n"


def without_canonical_hash(profile: dict[str, Any]) -> dict[str, Any]:
    clone = copy.deepcopy(profile)
    clone.pop("canonicalHash", None)
    return clone


def profile_hash(profile: dict[str, Any]) -> str:
    digest = hashlib.sha256(canonical_bytes(without_canonical_hash(profile))).hexdigest()
    return HASH_PREFIX + digest


def is_hash_string(value: Any) -> bool:
    return isinstance(value, str) and value.startswith(HASH_PREFIX) and len(value) == len(HASH_PREFIX) + 64 and all(
        c in "0123456789abcdef" for c in value[len(HASH_PREFIX):]
    )


def normalize_rational(value: dict[str, Any]) -> dict[str, int]:
    if set(value) != {"numerator", "denominator"}:
        raise CanonicalJsonError("rational values must contain exactly numerator and denominator")
    numerator = value["numerator"]
    denominator = value["denominator"]
    if isinstance(numerator, bool) or isinstance(denominator, bool):
        raise CanonicalJsonError("rational numerator and denominator must be integers")
    if not isinstance(numerator, int) or not isinstance(denominator, int):
        raise CanonicalJsonError("rational numerator and denominator must be integers")
    if denominator == 0:
        raise CanonicalJsonError("rational denominator cannot be zero")
    if denominator < 0:
        numerator = -numerator
        denominator = -denominator
    gcd = math.gcd(abs(numerator), denominator)
    return {"denominator": denominator // gcd, "numerator": numerator // gcd}


def require_canonical_rational(value: Any) -> None:
    if not isinstance(value, dict):
        raise CanonicalJsonError("expected rational object")
    normalized = normalize_rational(value)
    if value != normalized:
        raise CanonicalJsonError(f"rational must be reduced and normalized: expected {normalized}, got {value}")
