#!/usr/bin/env python3
"""Controlled, deterministic promotion of validated tuning profiles.

Inspection is read-only.  Preparation writes only below the ignored private
promotion directory.  Canonical replacement is available solely through the
hash-locked, explicitly approved ``promote`` command.
"""

from __future__ import annotations

import argparse
import contextlib
import copy
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from canonical_json import pretty_json, profile_hash
from generate_tuning_header import generate_header
from profile_schema import load_registry
from validate_profile import ProfileValidationError, validate_profile


SCHEMA_VERSION = 1
OUTCOMES = {
    "promotion_pipeline_validated_candidate_ineligible",
    "promotion_ready_awaiting_approval",
    "profile_promoted",
    "promotion_failed",
    "profile_rolled_back",
}
STATES = {
    "discovered", "validated", "ineligible", "eligible", "staged", "verified",
    "awaiting_approval", "promoting", "promoted", "promotion_failed", "rolled_back",
}
ALLOWED_TRANSITIONS = {
    "discovered": {"validated", "ineligible"},
    "validated": {"eligible", "ineligible"},
    "eligible": {"staged"},
    "staged": {"verified", "promotion_failed"},
    "verified": {"awaiting_approval", "promotion_failed"},
    "awaiting_approval": {"promoting", "promotion_failed"},
    "promoting": {"promoted", "promotion_failed"},
    "promoted": {"rolled_back"},
    "ineligible": set(), "promotion_failed": set(), "rolled_back": set(),
}
GATE_STATUSES = {
    "passed", "failed", "missing", "stale", "not_applicable", "requires_human_approval"
}
RELEASE_RE = re.compile(r"[a-z0-9][a-z0-9.-]*")
HASH_RE = re.compile(r"sha256:[0-9a-f]{64}")


class PromotionError(RuntimeError):
    pass


@dataclass(frozen=True)
class CandidateArtifacts:
    profile: Path
    metadata: Path
    header: Path
    binary: Path
    manifest: Path | None
    related: tuple[Path, ...]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


@contextlib.contextmanager
def working_directory(path: Path):
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise PromotionError(f"cannot read JSON artifact {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PromotionError(f"JSON artifact must be an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def sha256_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def relative(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as exc:
        raise PromotionError(f"artifact is outside repository: {path}") from exc


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except Exception:
        with contextlib.suppress(OSError):
            os.unlink(temporary)
        raise


def write_json(path: Path, value: Any) -> None:
    atomic_write(path, pretty_json(value).encode("utf-8"))


def validate_release_id(value: str) -> str:
    if not value or not RELEASE_RE.fullmatch(value) or ".." in value or value.endswith("."):
        raise PromotionError("release ID must match [a-z0-9][a-z0-9.-]* without '..' or a trailing dot")
    if any(character in value for character in "/\\;|&$`'\" "):
        raise PromotionError("release ID contains a path separator, whitespace, or shell metacharacter")
    return value


def _contains_absolute_path(value: Any) -> bool:
    if isinstance(value, dict):
        return any(_contains_absolute_path(item) for item in value.values())
    if isinstance(value, list):
        return any(_contains_absolute_path(item) for item in value)
    if isinstance(value, str):
        return value.startswith("/") or bool(re.match(r"^[A-Za-z]:[\\/]", value))
    return False


def _candidate_profile_paths(root: Path) -> Iterable[Path]:
    patterns = (
        "tuning/candidates/*/profile.json", "tuning/builds/*/profile.json",
        "tuning/search/**/profile.json", "tuning/time/**/profile.json",
        "tuning/runs/**/profile.json",
    )
    seen: set[Path] = set()
    for pattern in patterns:
        for path in root.glob(pattern):
            if path not in seen:
                seen.add(path)
                yield path


def _profile_by_id(root: Path, candidate_id: str) -> Path:
    matches = []
    for path in _candidate_profile_paths(root):
        try:
            if read_json(path).get("profileId") == candidate_id:
                matches.append(path)
        except PromotionError:
            continue
    if not matches:
        raise PromotionError(f"candidate not found: {candidate_id}")
    preferred = sorted(matches, key=lambda p: ("/candidate/" not in p.as_posix(), len(p.parts), p.as_posix()))
    first_hash = read_json(preferred[0]).get("canonicalHash")
    if any(read_json(path).get("canonicalHash") != first_hash for path in matches):
        raise PromotionError(f"ambiguous candidate ID with different hashes: {candidate_id}")
    return preferred[0]


def discover_candidate(candidate: str | Path, root: Path | None = None) -> CandidateArtifacts:
    root = (root or repo_root()).resolve()
    supplied = Path(candidate)
    explicit_path = supplied.is_absolute() or supplied.exists() or (root / supplied).exists()
    if explicit_path:
        path = supplied if supplied.is_absolute() else root / supplied
        profile = path / "profile.json" if path.is_dir() else path
        if profile.name != "profile.json" or not profile.is_file():
            raise PromotionError(f"explicit candidate path is not a profile.json: {profile}")
    else:
        profile = _profile_by_id(root, str(candidate))
    profile = profile.resolve()
    candidate_id = read_json(profile).get("profileId")
    if not isinstance(candidate_id, str):
        raise PromotionError("candidate profile has no canonical profileId")
    metadata_candidates = [profile.parent / "metadata.json"]
    if not explicit_path:
        metadata_candidates.append(root / "tuning/candidates" / candidate_id / "metadata.json")
    metadata = next((path for path in metadata_candidates if path.is_file()), None)
    if metadata is None:
        raise PromotionError(f"candidate metadata missing for {candidate_id}")
    corrected_binaries: list[Path] = []
    corrected_headers: list[Path] = []
    profile_data_hash = read_json(profile).get("canonicalHash")
    time_root = root / "tuning/time"
    if time_root.exists():
        for evidence_path in time_root.rglob("*.json"):
            try:
                evidence = read_json(evidence_path)
                text = evidence_path.read_text(encoding="utf-8")
            except PromotionError:
                continue
            corrected = evidence.get("correctedEngine")
            if candidate_id in text and profile_data_hash:
                if profile_data_hash not in text:
                    continue
                if isinstance(corrected, str):
                    corrected_path = root / corrected
                    if corrected_path.is_file():
                        corrected_binaries.append(corrected_path)
                        generated = corrected_path.parent / "generated/generated_tuning_values.hpp"
                        if generated.is_file():
                            corrected_headers.append(generated)
    header_candidates = corrected_headers + [
        profile.parent / "generated/generated_tuning_values.hpp",
        root / "tuning/builds" / candidate_id / "generated/generated_tuning_values.hpp",
    ]
    header = next((path for path in header_candidates if path.is_file()), None)
    if header is None:
        raise PromotionError(f"candidate generated header missing for {candidate_id}")
    binary_candidates = corrected_binaries + [profile.parent / "chess-engine", root / "tuning/builds" / candidate_id / "chess-engine"]
    binary = next((path for path in binary_candidates if path.is_file()), None)
    if binary is None:
        raise PromotionError(f"candidate binary missing for {candidate_id}")
    manifest_candidates = [profile.parent / "manifest.json", profile.parent.parent / "manifest.json"]
    manifest = next((path for path in manifest_candidates if path.is_file()), None)
    related: list[Path] = []
    needles = {candidate_id, read_json(profile).get("canonicalHash")}
    for base in (root / "tuning/selections", root / "tuning/validation", root / "tuning/search", root / "tuning/time"):
        if not base.exists():
            continue
        for path in base.rglob("*.json"):
            if path in {profile, metadata, manifest}:
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except OSError:
                continue
            if any(isinstance(needle, str) and needle in text for needle in needles):
                related.append(path)
    runs_root = root / "tuning/runs"
    with contextlib.suppress(ValueError):
        relative_profile = profile.relative_to(runs_root)
        run_directory = runs_root / relative_profile.parts[0]
        for path in run_directory.rglob("*.json"):
            if path not in {profile, metadata, manifest}:
                related.append(path)
    return CandidateArtifacts(profile, metadata.resolve(), header.resolve(), binary.resolve(), manifest.resolve() if manifest else None, tuple(sorted(set(related))))


def engine_identity(path: Path, timeout: float = 15.0) -> dict[str, str]:
    if not path.is_file() or not os.access(path, os.X_OK):
        raise PromotionError(f"engine binary is missing or not executable: {path}")
    process = subprocess.run(
        [str(path.resolve()), "--mode=gui"], input="uci\nquit\n", text=True,
        capture_output=True, timeout=timeout,
    )
    if process.returncode:
        raise PromotionError(f"engine identity command failed: {process.stderr.strip()}")
    marker = "info string tuning profile="
    lines = [line for line in process.stdout.splitlines() if line.startswith(marker)]
    if len(lines) != 1:
        raise PromotionError(f"engine reported {len(lines)} tuning identities")
    tokens = dict(item.split("=", 1) for item in lines[0][len("info string tuning "):].split() if "=" in item)
    profile_id, canonical_hash = tokens.get("profile"), tokens.get("hash")
    if not profile_id or not canonical_hash:
        raise PromotionError("engine identity is incomplete")
    return {"profileId": profile_id, "profileHash": canonical_hash, "binarySha256": sha256_file(path)}


def _manifest_checks(manifest_path: Path | None, candidate_dir: Path) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    if manifest_path is None:
        return results
    manifest = read_json(manifest_path)
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise PromotionError("unknown candidate manifest schema version")
    for name, record in sorted(manifest.get("artifacts", {}).items()):
        expected = record.get("sha256") if isinstance(record, dict) else record
        artifact_root = candidate_dir if manifest_path.parent == candidate_dir else manifest_path.parent
        artifact = artifact_root / name
        if not artifact.is_file():
            raise PromotionError(f"candidate manifest artifact missing: {name}")
        actual = sha256_file(artifact)
        if actual != expected:
            raise PromotionError(f"candidate manifest checksum mismatch: {name}")
        results.append({"path": name, "sha256": actual, "status": "passed"})
    return results


def validate_candidate_artifacts(artifacts: CandidateArtifacts, root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    profile, metadata = read_json(artifacts.profile), read_json(artifacts.metadata)
    if metadata.get("schemaVersion") != SCHEMA_VERSION:
        raise PromotionError("unknown candidate metadata schema version")
    if _contains_absolute_path(metadata):
        raise PromotionError("candidate metadata contains an absolute path")
    with working_directory(root):
        stats = validate_profile(profile, root / "tuning/parameter-registry.json")
    if profile_hash(profile) != profile.get("canonicalHash"):
        raise PromotionError("candidate profile hash mismatch")
    identity_pairs = (
        ("candidateProfileId", "profileId"), ("candidateProfileHash", "canonicalHash"),
    )
    for metadata_key, profile_key in identity_pairs:
        if metadata.get(metadata_key) != profile.get(profile_key):
            raise PromotionError(f"candidate metadata identity mismatch: {metadata_key}")
    expected_header = generate_header(profile).encode("utf-8")
    actual_header = artifacts.header.read_bytes()
    if actual_header != expected_header:
        raise PromotionError("candidate header/profile mismatch")
    actual_header_hash = sha256_bytes(actual_header)
    for key in ("candidateHeaderSha256", "generatedHeaderSha256", "headerSha256"):
        if key in metadata and metadata[key] != actual_header_hash:
            raise PromotionError(f"candidate header checksum mismatch: {key}")
    identity = engine_identity(artifacts.binary)
    if (identity["profileId"], identity["profileHash"]) != (profile["profileId"], profile["canonicalHash"]):
        raise PromotionError("candidate binary/profile mismatch")
    for key in ("candidateBinarySha256", "binarySha256"):
        if key in metadata and metadata[key] != identity["binarySha256"]:
            raise PromotionError(f"candidate binary checksum mismatch: {key}")
    manifest_results = _manifest_checks(artifacts.manifest, artifacts.profile.parent)
    return {
        "profile": profile,
        "metadata": metadata,
        "schema": stats,
        "identity": identity,
        "artifacts": {
            "profile": {"path": relative(artifacts.profile, root), "sha256": sha256_file(artifacts.profile), "status": "passed"},
            "metadata": {"path": relative(artifacts.metadata, root), "sha256": sha256_file(artifacts.metadata), "status": "passed"},
            "header": {"path": relative(artifacts.header, root), "sha256": sha256_file(artifacts.header), "status": "passed", "matchesGenerated": True},
            "binary": {"path": relative(artifacts.binary, root), "sha256": identity["binarySha256"], "status": "passed", "identity": identity},
            "manifest": ({"path": relative(artifacts.manifest, root), "sha256": sha256_file(artifacts.manifest), "status": "passed", "entries": manifest_results} if artifacts.manifest else None),
        },
    }


def ownership_counts(root: Path) -> dict[str, int]:
    registry = load_registry(root / "tuning/parameter-registry.json")
    counts = {group: 0 for group in ("evaluation", "search", "time", "opening")}
    for parameter in registry["parameters"]:
        counts[parameter["name"].split(".", 1)[0]] += 1
    return {**counts, "total": sum(counts.values())}


def grouped_diff(base: dict[str, Any], candidate: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    result = {group: [] for group in ("evaluation", "search", "time", "opening")}
    names = sorted(set(base["parameters"]) | set(candidate["parameters"]))
    for name in names:
        before, after = base["parameters"].get(name), candidate["parameters"].get(name)
        if before != after:
            group = name.split(".", 1)[0]
            result[group].append({"parameter": name, "currentProduction": before, "sourceCandidate": after})
    return result


def _declared_changes(metadata: dict[str, Any]) -> set[str]:
    if isinstance(metadata.get("changedParameters"), list):
        return {item if isinstance(item, str) else item.get("registryName") for item in metadata["changedParameters"]}
    if isinstance(metadata.get("changedSearchParameters"), list):
        return {item.get("registryName") for item in metadata["changedSearchParameters"]}
    if isinstance(metadata.get("changedTimeParameters"), list):
        return {item.get("registryName") for item in metadata["changedTimeParameters"]}
    return set()


def validate_ancestry(source: CandidateArtifacts, root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    production_path = root / "tuning/profiles/builtin-default-v1.json"
    production = read_json(production_path)
    chain: list[dict[str, Any]] = []
    current_artifacts, seen = source, set()
    while True:
        checked = validate_candidate_artifacts(current_artifacts, root)
        child, metadata = checked["profile"], checked["metadata"]
        if child["profileId"] in seen:
            raise PromotionError("candidate ancestry cycle detected")
        seen.add(child["profileId"])
        parent_id = child.get("parentProfileId")
        if metadata.get("basedOnProfileId") != parent_id:
            raise PromotionError(f"ancestry parent ID mismatch for {child['profileId']}")
        if parent_id == production["profileId"]:
            parent = production
        else:
            parent_artifacts = discover_candidate(parent_id, root)
            parent = read_json(parent_artifacts.profile)
        if metadata.get("basedOnProfileHash") != parent.get("canonicalHash"):
            raise PromotionError(f"ancestry parent hash mismatch for {child['profileId']}")
        actual = {name for name in child["parameters"] if child["parameters"][name] != parent["parameters"].get(name)}
        declared = _declared_changes(metadata)
        if actual != declared:
            raise PromotionError(
                f"ancestry changed parameters mismatch for {child['profileId']}: "
                f"declared={sorted(declared)} actual={sorted(actual)}"
            )
        chain.append({
            "profileId": child["profileId"], "profileHash": child["canonicalHash"],
            "parentProfileId": parent["profileId"], "parentProfileHash": parent["canonicalHash"],
            "changedParameters": sorted(actual),
        })
        if parent_id == production["profileId"]:
            break
        current_artifacts = parent_artifacts
    chain.reverse()
    return {
        "currentProduction": {"profileId": production["profileId"], "profileHash": production["canonicalHash"], "path": relative(production_path, root)},
        "chain": chain,
        "parameterDiff": grouped_diff(production, read_json(source.profile)),
    }


def gate(gate_id: str, status: str, source: Path | None, reason: str, root: Path) -> dict[str, Any]:
    if status not in GATE_STATUSES:
        raise PromotionError(f"unknown gate status: {status}")
    return {
        "gateId": gate_id, "requiredStatus": "passed", "actualStatus": status,
        "artifactSource": relative(source, root) if source else None,
        "artifactChecksum": sha256_file(source) if source and source.is_file() else None,
        "reason": reason,
    }


def _json_has_identity(value: Any, profile_id: str, profile_hash_value: str) -> bool:
    if isinstance(value, dict):
        direct_id = value.get("profileId") or value.get("candidateId") or value.get("candidateProfileId")
        direct_hash = value.get("profileHash") or value.get("candidateHash") or value.get("candidateProfileHash")
        if direct_id == profile_id and direct_hash == profile_hash_value:
            return True
        return any(_json_has_identity(item, profile_id, profile_hash_value) for item in value.values())
    if isinstance(value, list):
        return any(_json_has_identity(item, profile_id, profile_hash_value) for item in value)
    return False


def _first_related(artifacts: CandidateArtifacts, predicate) -> Path | None:
    for path in artifacts.related:
        try:
            value = read_json(path)
        except PromotionError:
            continue
        if predicate(path, value):
            return path
    return None


def _verify_declared_artifacts(path: Path, fields: tuple[str, ...] = ("artifacts",)) -> tuple[bool, str]:
    value = read_json(path)
    for field in fields:
        records = value.get(field, {})
        if not isinstance(records, dict):
            return False, f"{field} is not an artifact map"
        for name, record in sorted(records.items()):
            expected = record.get("sha256") if isinstance(record, dict) else record
            artifact = path.parent / ("games" if field == "gameArtifacts" else "") / name
            if not artifact.is_file():
                return False, f"declared validation artifact missing: {name}"
            if sha256_file(artifact) != expected:
                return False, f"declared validation artifact checksum mismatch: {name}"
    return True, "all declared validation artifact checksums match"


def evaluate_match_gate(artifacts: CandidateArtifacts, profile: dict[str, Any], policy: dict[str, Any], root: Path) -> dict[str, Any]:
    candidates = [root / "tuning/validation" / profile["profileId"] / "match-validation.json"]
    match_path = next((path for path in candidates if path.is_file()), None)
    if match_path is None:
        match_path = _first_related(artifacts, lambda p, v: p.name == "match-validation.json" and _json_has_identity(v, profile["profileId"], profile["canonicalHash"]))
    if match_path is None:
        smoke = _first_related(artifacts, lambda p, v: "smoke-match" in p.name or "smokeMatch" in v)
        return gate("match_validation", "missing", smoke, "no strength-validation artifact; smoke matches are insufficient", root)
    value = read_json(match_path)
    requirements = policy["matchValidation"]
    required_fields = {
        "schemaVersion", "candidateId", "candidateHash", "baselineId", "baselineHash", "games",
        "timeControl", "startingPositionSuiteChecksum", "colorReversal", "wins", "losses", "draws",
        "flags", "illegalMoves", "crashes", "protocolFailures", "adjudications", "acceptanceDecision",
    }
    if set(value) != required_fields or value.get("schemaVersion") != requirements["schemaVersion"]:
        return gate("match_validation", "failed", match_path, "match artifact schema/fields invalid", root)
    if value["candidateId"] != profile["profileId"] or value["candidateHash"] != profile["canonicalHash"]:
        return gate("match_validation", "stale", match_path, "match artifact candidate identity is stale", root)
    failures = []
    if value["games"] < requirements["minimumGames"]: failures.append("insufficient games")
    if requirements["requireColorReversal"] and value["colorReversal"] is not True: failures.append("color reversal missing")
    if not value["timeControl"]: failures.append("time control missing")
    if not HASH_RE.fullmatch(value["startingPositionSuiteChecksum"]): failures.append("suite checksum missing")
    for key in ("flags", "illegalMoves", "crashes", "protocolFailures"):
        if value[key] > requirements["maximum" + key[0].upper() + key[1:]]: failures.append(key)
    if value["acceptanceDecision"] not in requirements["allowedAcceptanceDecisions"]: failures.append("not accepted")
    return gate("match_validation", "failed" if failures else "passed", match_path, ", ".join(failures) or "strength match policy satisfied", root)


def load_policy(root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    policy = read_json(root / "tuning/promotion-policy.json")
    if policy.get("schemaVersion") != SCHEMA_VERSION or policy.get("policyVersion") != 1:
        raise PromotionError("unsupported promotion policy version")
    required = set(policy.get("requiredGates", []))
    known = {
        "schema_validation", "artifact_integrity", "ancestry_validation", "deterministic_generation",
        "isolated_build", "candidate_binary_identity", "evaluation_validation", "search_validation",
        "time_policy_validation", "protocol_regression", "fair_play_regression",
        "opening_book_integration", "match_validation",
    }
    if not required or not required <= known:
        raise PromotionError("promotion policy contains an unknown or empty required gate set")
    return policy


def inspect_candidate(candidate: str | Path, root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    policy = load_policy(root)
    artifacts = discover_candidate(candidate, root)
    integrity = validate_candidate_artifacts(artifacts, root)
    ancestry = validate_ancestry(artifacts, root)
    profile, metadata = integrity["profile"], integrity["metadata"]
    header_first = generate_header(profile).encode("utf-8")
    header_second = generate_header(copy.deepcopy(profile)).encode("utf-8")
    gates: dict[str, dict[str, Any]] = {}
    gates["schema_validation"] = gate("schema_validation", "passed", artifacts.profile, "complete registry schema validated", root)
    gates["artifact_integrity"] = gate("artifact_integrity", "passed", artifacts.metadata, "profile, metadata, generated header, manifest, and binary are mutually consistent", root)
    gates["ancestry_validation"] = gate("ancestry_validation", "passed", artifacts.metadata, "complete declared ancestry and edge diffs validated", root)
    gates["deterministic_generation"] = gate("deterministic_generation", "passed" if header_first == header_second else "failed", artifacts.header, "two generated headers are byte-identical", root)
    gates["isolated_build"] = gate("isolated_build", "passed", artifacts.binary, "candidate binary exists outside canonical engine path", root)
    gates["candidate_binary_identity"] = gate("candidate_binary_identity", "passed", artifacts.binary, "binary reports exact candidate profile identity", root)
    eval_chain = next((item for item in ancestry["chain"] if item["profileId"].startswith("candidate-eval-")), None)
    eval_validation = None
    if eval_chain:
        expected_id, expected_hash = eval_chain["profileId"], eval_chain["profileHash"]
        path = root / "tuning/validation" / expected_id / "manifest.json"
        candidates = [path] if path.is_file() else []
        candidates.extend(item for item in artifacts.related if item.name == "manifest.json")
        for path in candidates:
            value = read_json(path)
            valid_checksums, _ = _verify_declared_artifacts(path, ("artifacts", "gameArtifacts"))
            baseline = value.get("baselineIdentity", {})
            if (value.get("schemaVersion") == 1 and value.get("validationStatus") in {"validated", "validated_with_warnings", "validated_for_experimental_use"}
                    and _json_has_identity(value, expected_id, expected_hash) and valid_checksums
                    and baseline.get("profileId") == ancestry["currentProduction"]["profileId"]
                    and baseline.get("profileHash") == ancestry["currentProduction"]["profileHash"]):
                eval_validation = path
                break
    gates["evaluation_validation"] = gate("evaluation_validation", "passed" if eval_validation else "missing", eval_validation, "exact evaluation ancestor, baseline, schema, suite, and artifact checksums independently validated" if eval_validation else "exact fresh evaluation validation artifact missing", root)
    search_validation = _first_related(artifacts, lambda p, v: p.name in {"summary.json", "promotion-summary.json"} and _json_has_identity(v, profile["profileId"], profile["canonicalHash"]))
    search_status, search_reason = "missing", "exact fresh search validation artifact missing"
    if search_validation:
        valid_checksums, checksum_reason = _verify_declared_artifacts(search_validation)
        search_status = "passed" if valid_checksums else "stale"
        search_reason = "exact search candidate, suite, and harness artifact checksums validated" if valid_checksums else checksum_reason
    gates["search_validation"] = gate("search_validation", search_status, search_validation, search_reason, root)
    time_validation = _first_related(artifacts, lambda p, v: v.get("status") == "time_policy_safe" and _json_has_identity(v, profile["profileId"], profile["canonicalHash"]))
    gates["time_policy_validation"] = gate("time_policy_validation", "passed" if time_validation else "missing", time_validation, "time-policy safety evidence matches exact candidate" if time_validation else "exact time-policy safety evidence missing", root)
    for gate_id, reason in (
        ("protocol_regression", "requires staged regression verification"),
        ("fair_play_regression", "requires staged website integration verification"),
        ("opening_book_integration", "requires staged opening-book integration verification"),
    ):
        gates[gate_id] = gate(gate_id, "missing", None, reason, root)
    gates["match_validation"] = evaluate_match_gate(artifacts, profile, policy, root)
    gates["human_approval"] = gate("human_approval", "requires_human_approval", None, "explicit --approve and both expected hashes are mandatory", root)
    policy_blockers = []
    if policy["blockDevelopmentOnly"] and metadata.get("developmentOnly") is True:
        policy_blockers.append("source candidate is developmentOnly")
    if policy["requirePromotionEligible"] and metadata.get("promotionEligible") is not True:
        policy_blockers.append("source candidate is not promotionEligible")
    for gate_id in policy["requiredGates"]:
        if gates[gate_id]["actualStatus"] != "passed":
            policy_blockers.append(f"{gate_id}: {gates[gate_id]['actualStatus']}")
    transitions = [{"from": None, "to": "discovered"}, {"from": "discovered", "to": "validated"}]
    transitions.append({"from": "validated", "to": "ineligible" if policy_blockers else "eligible"})
    outcome = "promotion_pipeline_validated_candidate_ineligible" if policy_blockers else "promotion_ready_awaiting_approval"
    return {
        "schemaVersion": SCHEMA_VERSION,
        "sourceCandidate": {"profileId": profile["profileId"], "profileHash": profile["canonicalHash"]},
        "currentProduction": ancestry["currentProduction"],
        "ownershipCounts": ownership_counts(root),
        "artifacts": integrity["artifacts"],
        "ancestry": ancestry["chain"],
        "parameterDiff": ancestry["parameterDiff"],
        "gates": [gates[name] for name in sorted(gates)],
        "policyVersion": policy["policyVersion"],
        "policyBlockers": policy_blockers,
        "eligible": not policy_blockers,
        "state": "ineligible" if policy_blockers else "eligible",
        "stateTransitions": transitions,
        "outcome": outcome,
    }


def _snapshot(root: Path, directory: Path) -> dict[str, Any]:
    canonical_profile = root / "tuning/profiles/builtin-default-v1.json"
    canonical_header = root / "engine/include/tuning/generated_tuning_values.hpp"
    normal_binary = root / "engine/chess-engine"
    paths = {"profile": canonical_profile, "header": canonical_header, "binary": normal_binary}
    for name, path in paths.items():
        if not path.is_file():
            raise PromotionError(f"cannot snapshot missing canonical {name}: {path}")
        shutil.copy2(path, directory / path.name)
    profile = read_json(canonical_profile)
    identity = engine_identity(normal_binary)
    return {
        "previousProfileId": profile["profileId"], "previousProfileHash": profile["canonicalHash"],
        "previousProfileChecksum": sha256_file(canonical_profile),
        "previousCanonicalHeaderChecksum": sha256_file(canonical_header),
        "previousNormalBinaryIdentity": identity,
        "previousReleaseIdentifier": profile["profileId"].removeprefix("builtin-default-"),
        "files": {name: {"snapshot": relative(directory / path.name, root), "canonical": relative(path, root), "sha256": sha256_file(path)} for name, path in paths.items()},
    }


def _promotion_dir(root: Path, promotion: str) -> Path:
    if not RELEASE_RE.fullmatch(promotion) or ".." in promotion:
        raise PromotionError("invalid promotion ID")
    directory = root / "tuning/promotion" / promotion
    if not directory.is_dir():
        raise PromotionError(f"promotion not found: {promotion}")
    return directory


def prepare_promotion(candidate: str | Path, release_id: str, root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    release_id = validate_release_id(release_id)
    report = inspect_candidate(candidate, root)
    technical = {"schema_validation", "artifact_integrity", "ancestry_validation", "deterministic_generation", "isolated_build", "candidate_binary_identity"}
    gate_map = {item["gateId"]: item for item in report["gates"]}
    bad = [name for name in technical if gate_map[name]["actualStatus"] != "passed"]
    if bad:
        raise PromotionError(f"technical staging prerequisites failed: {sorted(bad)}")
    artifacts = discover_candidate(candidate, root)
    source = read_json(artifacts.profile)
    production = read_json(root / "tuning/profiles/builtin-default-v1.json")
    policy = load_policy(root)
    staged_id = policy["canonicalProfileIdTemplate"].format(releaseId=release_id)
    staged = copy.deepcopy(source)
    staged.update({
        "profileId": staged_id,
        "parentProfileId": production["profileId"],
        "sourceBaseline": production["profileId"],
        "description": f"Canonical production profile prepared from {source['profileId']} for release {release_id}.",
        "canonicalHash": "",
    })
    staged["canonicalHash"] = profile_hash(staged)
    with working_directory(root):
        validate_profile(staged, root / "tuning/parameter-registry.json")
    promotion_id = f"{release_id}-{source['canonicalHash'].split(':', 1)[1][:12]}"
    directory = root / "tuning/promotion" / promotion_id
    if directory.exists():
        existing = read_json(directory / "manifest.json") if (directory / "manifest.json").is_file() else None
        if existing and existing.get("sourceCandidate", {}).get("profileHash") == source["canonicalHash"] and existing.get("stagedProduction", {}).get("profileHash") == staged["canonicalHash"]:
            return existing
        raise PromotionError(f"promotion directory already exists with different content: {promotion_id}")
    source_dir, staged_dir, rollback_dir = directory / "source", directory / "staged", directory / "rollback"
    source_dir.mkdir(parents=True)
    staged_dir.mkdir(parents=True)
    rollback_dir.mkdir(parents=True)
    shutil.copy2(artifacts.profile, source_dir / "profile.json")
    shutil.copy2(artifacts.metadata, source_dir / "metadata.json")
    write_json(staged_dir / "profile.json", staged)
    first = generate_header(staged).encode("utf-8")
    second = generate_header(copy.deepcopy(staged)).encode("utf-8")
    if first != second:
        raise PromotionError("staged header generation is nondeterministic")
    atomic_write(staged_dir / "generated_tuning_values.hpp", first)
    binary_name = f"chess-engine-{release_id}"
    binary_path = staged_dir / binary_name
    command = [
        "make", "-C", "engine", "release-build", f"RELEASE_ID={release_id}",
        f"PROFILE_HEADER={staged_dir / 'generated_tuning_values.hpp'}", f"OUTPUT={binary_path}",
    ]
    build = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if build.returncode:
        raise PromotionError(f"staged release build failed:\n{build.stdout}\n{build.stderr}")
    identity = engine_identity(binary_path)
    if (identity["profileId"], identity["profileHash"]) != (staged["profileId"], staged["canonicalHash"]):
        raise PromotionError("staged binary identity mismatch")
    rollback = _snapshot(root, rollback_dir)
    write_json(rollback_dir / "metadata.json", rollback)
    transitions = copy.deepcopy(report["stateTransitions"])
    state = report["state"]
    if state == "eligible":
        transitions.append({"from": "eligible", "to": "staged"})
        state = "staged"
    manifest = {
        "schemaVersion": SCHEMA_VERSION, "policyVersion": policy["policyVersion"],
        "promotionId": promotion_id, "releaseId": release_id,
        "sourceCandidate": report["sourceCandidate"], "sourceAncestry": report["ancestry"],
        "currentProduction": report["currentProduction"],
        "stagedProduction": {"profileId": staged["profileId"], "profileHash": staged["canonicalHash"]},
        "artifactChecksums": {
            "sourceProfile": sha256_file(source_dir / "profile.json"),
            "sourceMetadata": sha256_file(source_dir / "metadata.json"),
            "stagedProfile": sha256_file(staged_dir / "profile.json"),
            "stagedHeader": sha256_file(staged_dir / "generated_tuning_values.hpp"),
            "stagedEngine": sha256_file(binary_path),
            "rollbackMetadata": sha256_file(rollback_dir / "metadata.json"),
        },
        "stagedExecutable": relative(binary_path, root),
        "validationGates": report["gates"], "parameterDiff": report["parameterDiff"],
        "equivalenceResults": None,
        "approvalRequirements": policy["humanApproval"], "rollback": rollback,
        "state": state, "stateTransitions": transitions,
        "outcome": report["outcome"],
    }
    write_json(directory / "manifest.json", manifest)
    write_json(directory / "summary.json", {
        "promotionId": promotion_id, "releaseId": release_id, "state": state,
        "sourceCandidate": report["sourceCandidate"], "stagedProduction": manifest["stagedProduction"],
        "blockingReasons": report["policyBlockers"], "outcome": report["outcome"],
    })
    return manifest


def _run_json_mode(binary: Path, mode: str, lines: list[str]) -> list[dict[str, Any]]:
    process = subprocess.run([str(binary.resolve()), f"--mode={mode}"], input="\n".join(lines) + "\n", text=True, capture_output=True, timeout=30)
    if process.returncode:
        raise PromotionError(f"{mode} comparison failed: {process.stderr.strip()}")
    return [json.loads(line) for line in process.stdout.splitlines() if line.strip().startswith("{")]


def _strip_identity(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: _strip_identity(item) for key, item in value.items() if key not in {"profileId", "profileHash"}}
    if isinstance(value, list):
        return [_strip_identity(item) for item in value]
    return value


def _uci_search(binary: Path, fen: str, depth: int, book: Path | None = None) -> dict[str, Any]:
    position = "position startpos" if fen == "startpos" else f"position fen {fen}"
    args = [str(binary.resolve()), "--mode=gui"]
    if book:
        args.append(f"--book={book.resolve()}")
    commands = ["uci", "isready", "setoption name OwnBook value " + ("true" if book else "false"), position, f"go depth {depth}", "quit"]
    process = subprocess.run(args, input="\n".join(commands) + "\n", text=True, capture_output=True, timeout=60)
    if process.returncode:
        raise PromotionError(f"UCI comparison failed: {process.stderr.strip()}")
    info = [line for line in process.stdout.splitlines() if line.startswith("info depth ")]
    best = [line for line in process.stdout.splitlines() if line.startswith("bestmove ")]
    if not best:
        raise PromotionError("UCI comparison produced no bestmove")
    final = info[-1] if info else ""
    def token(name: str) -> Any:
        match = re.search(rf"(?:^| ){name} (-?[a-z0-9]+)", final)
        return match.group(1) if match else None
    pv_match = re.search(r"(?:^| )pv (.+)$", final)
    score_match = re.search(r" score (cp|mate) (-?\d+)", final)
    return {
        "bestMove": best[-1].split()[1], "depth": int(token("depth")) if token("depth") else None,
        "nodes": int(token("nodes")) if token("nodes") else None,
        "score": list(score_match.groups()) if score_match else None,
        "pv": pv_match.group(1).split() if pv_match else [],
        "book": next((line for line in process.stdout.splitlines() if line.startswith("info string book move ")), None),
    }


def compare_engines(candidate: Path, staged: Path, source_profile: dict[str, Any], staged_profile: dict[str, Any], root: Path) -> dict[str, Any]:
    mismatches: list[str] = []
    if source_profile["parameters"] != staged_profile["parameters"]:
        mismatches.append("parameter_values")
    fens = [
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bq1rk1/pp2nppp/2p1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 2 9",
        "7k/6pp/8/8/8/8/5PPP/6K1 w - - 0 1",
    ]
    candidate_features = _run_json_mode(candidate, "eval-features", fens)
    staged_features = _run_json_mode(staged, "eval-features", fens)
    if _strip_identity(candidate_features) != _strip_identity(staged_features):
        mismatches.append("evaluation_feature_trace")
    time_lines = [
        json.dumps({"remainingTimeMs": 10000, "incrementMs": 100, "fullmoveNumber": 10, "movesToGo": None, "bestMoveUnstable": False, "bestMoveChangeCount": 0, "scoreSwingCp": 0}),
        json.dumps({"remainingTimeMs": 39, "incrementMs": 0, "fullmoveNumber": 30, "movesToGo": None, "bestMoveUnstable": True, "bestMoveChangeCount": 2, "scoreSwingCp": 80}),
    ]
    candidate_time = _run_json_mode(candidate, "time-policy", time_lines)
    staged_time = _run_json_mode(staged, "time-policy", time_lines)
    if _strip_identity(candidate_time) != _strip_identity(staged_time):
        mismatches.append("time_policy")
    search_fens = ["startpos", fens[1], "7k/5Q2/7K/8/8/8/8/8 w - - 0 1"]
    candidate_search = [_uci_search(candidate, fen, 4) for fen in search_fens]
    staged_search = [_uci_search(staged, fen, 4) for fen in search_fens]
    if candidate_search != staged_search:
        for key in ("bestMove", "score", "pv", "nodes"):
            if [row[key] for row in candidate_search] != [row[key] for row in staged_search]:
                mismatches.append("fixed_depth_" + key.lower())
    book = root / "engine/openings/performance.bin"
    candidate_book = _uci_search(candidate, "startpos", 2, book)
    staged_book = _uci_search(staged, "startpos", 2, book)
    if candidate_book != staged_book:
        mismatches.append("opening_book_path")
    return {
        "parameterValuesIdentical": source_profile["parameters"] == staged_profile["parameters"],
        "staticScoresIdentical": _strip_identity(candidate_features) == _strip_identity(staged_features),
        "featureCoefficientsIdentical": _strip_identity(candidate_features) == _strip_identity(staged_features),
        "featureTracesIdentical": _strip_identity(candidate_features) == _strip_identity(staged_features),
        "fixedDepth": {"candidate": candidate_search, "staged": staged_search},
        "timePolicyBudgetsIdentical": _strip_identity(candidate_time) == _strip_identity(staged_time),
        "openingBookDecisionsIdentical": candidate_book == staged_book,
        "fairPlayPositionHandling": "covered_by_regression_gate",
        "mismatches": sorted(set(mismatches)), "mismatchCount": len(set(mismatches)),
    }


def _run_regressions(root: Path, staged: Path) -> dict[str, Any]:
    commands = {
        "engine_tests": ["make", "-C", "engine", "test"],
        "fair_play_regression": ["npm", "run", "test:fair-play-integration"],
        "website_server_tests": ["npm", "run", "test:server"],
        "website_lint": ["npm", "run", "lint"],
        "website_typecheck": ["npm", "run", "typecheck"],
        "website_production_build": ["npm", "run", "build"],
    }
    results = {}
    for name, command in commands.items():
        cwd = root / "website" if command[0] == "npm" else root
        process = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
        output = (process.stdout + process.stderr).encode("utf-8")
        results[name] = {"command": command, "exitCode": process.returncode, "outputChecksum": sha256_bytes(output)}
    identity = engine_identity(staged)
    results["staged_uci_identity"] = {"exitCode": 0, "identity": identity}
    return results


def _set_gate(manifest: dict[str, Any], gate_id: str, status: str, source: str | None, checksum: str | None, reason: str) -> None:
    for item in manifest["validationGates"]:
        if item["gateId"] == gate_id:
            item.update({"actualStatus": status, "artifactSource": source, "artifactChecksum": checksum, "reason": reason})
            return
    raise PromotionError(f"manifest lacks gate {gate_id}")


def verify_promotion(promotion: str, root: Path | None = None, run_regressions: bool = True) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    directory = _promotion_dir(root, promotion)
    manifest_path = directory / "manifest.json"
    manifest = read_json(manifest_path)
    source_profile = read_json(directory / "source/profile.json")
    staged_profile = read_json(directory / "staged/profile.json")
    source_artifacts = discover_candidate(source_profile["profileId"], root)
    if read_json(source_artifacts.profile)["canonicalHash"] != manifest["sourceCandidate"]["profileHash"]:
        raise PromotionError("source candidate changed after preparation")
    staged_header = directory / "staged/generated_tuning_values.hpp"
    if staged_header.read_bytes() != generate_header(staged_profile).encode("utf-8"):
        raise PromotionError("staged header changed after preparation")
    staged_binary = root / manifest["stagedExecutable"]
    identity = engine_identity(staged_binary)
    if (identity["profileId"], identity["profileHash"]) != (staged_profile["profileId"], staged_profile["canonicalHash"]):
        raise PromotionError("staged binary identity changed after preparation")
    equivalence = compare_engines(source_artifacts.binary, staged_binary, source_profile, staged_profile, root)
    regressions = _run_regressions(root, staged_binary) if run_regressions else {}
    regression_failed = [name for name, value in regressions.items() if value.get("exitCode") != 0]
    validation_path = directory / "validation/results.json"
    validation_path.parent.mkdir(parents=True, exist_ok=True)
    results = {"schemaVersion": 1, "equivalence": equivalence, "regressions": regressions}
    write_json(validation_path, results)
    status = "passed" if not equivalence["mismatches"] else "failed"
    _set_gate(manifest, "protocol_regression", status, relative(validation_path, root), sha256_file(validation_path), "UCI, fixed-depth, mate telemetry, and identity parity" if status == "passed" else "behavioral mismatch")
    fair_missing = not run_regressions
    fair_failed = any(name in regression_failed for name in ("fair_play_regression", "website_server_tests"))
    fair_status = "missing" if fair_missing else ("failed" if fair_failed else "passed")
    _set_gate(manifest, "fair_play_regression", fair_status, relative(validation_path, root), sha256_file(validation_path), "Fair Play integration regression not run" if fair_missing else ("Fair Play integration regression passed" if not fair_failed else "Fair Play regression failed"))
    opening_status = "missing" if not run_regressions else ("passed" if equivalence["openingBookDecisionsIdentical"] and "engine_tests" not in regression_failed else "failed")
    _set_gate(manifest, "opening_book_integration", opening_status, relative(validation_path, root), sha256_file(validation_path), "book load/legal move/miss/fallback path validated without changing book bytes" if opening_status == "passed" else "opening-book integration failed")
    manifest["equivalenceResults"] = equivalence
    manifest["regressionResults"] = regressions
    policy = load_policy(root)
    blockers = []
    source_metadata = read_json(directory / "source/metadata.json")
    if policy["blockDevelopmentOnly"] and source_metadata.get("developmentOnly") is True: blockers.append("source candidate is developmentOnly")
    if policy["requirePromotionEligible"] and source_metadata.get("promotionEligible") is not True: blockers.append("source candidate is not promotionEligible")
    gates = {item["gateId"]: item for item in manifest["validationGates"]}
    for gate_id in policy["requiredGates"]:
        if gates[gate_id]["actualStatus"] != "passed": blockers.append(f"{gate_id}: {gates[gate_id]['actualStatus']}")
    if regression_failed: blockers.extend(f"regression failed: {name}" for name in regression_failed)
    if blockers:
        manifest["state"] = "ineligible"
        manifest["outcome"] = "promotion_pipeline_validated_candidate_ineligible"
    else:
        current = manifest["state"]
        if current == "staged":
            manifest["stateTransitions"].extend([{"from": "staged", "to": "verified"}, {"from": "verified", "to": "awaiting_approval"}])
        manifest["state"] = "awaiting_approval"
        manifest["outcome"] = "promotion_ready_awaiting_approval"
    manifest["blockingReasons"] = sorted(set(blockers))
    write_json(manifest_path, manifest)
    write_json(directory / "summary.json", {
        "promotionId": manifest["promotionId"], "releaseId": manifest["releaseId"],
        "state": manifest["state"], "sourceCandidate": manifest["sourceCandidate"],
        "stagedProduction": manifest["stagedProduction"], "equivalenceMismatches": equivalence["mismatches"],
        "blockingReasons": manifest["blockingReasons"], "outcome": manifest["outcome"],
    })
    return manifest


@contextlib.contextmanager
def promotion_lock(root: Path, manifest: dict[str, Any]):
    lock = root / "tuning/promotion/.promotion-lock"
    try:
        lock.mkdir(parents=True)
    except FileExistsError:
        owner_path = lock / "owner.json"
        owner = read_json(owner_path) if owner_path.is_file() else {}
        pid = owner.get("ownerPid")
        alive = isinstance(pid, int) and (Path("/proc") / str(pid)).exists()
        if alive:
            raise PromotionError(f"promotion lock held by live PID {pid}")
        stale = lock.with_name(f".promotion-lock.stale-{owner.get('promotionId', 'unknown')}")
        with contextlib.suppress(OSError):
            os.replace(lock, stale)
        lock.mkdir(parents=True)
    write_json(lock / "owner.json", {"schemaVersion": 1, "promotionId": manifest["promotionId"], "candidateId": manifest["sourceCandidate"]["profileId"], "releaseId": manifest["releaseId"], "ownerPid": os.getpid()})
    try:
        yield
    finally:
        shutil.rmtree(lock, ignore_errors=True)


def _restore_snapshot(root: Path, manifest: dict[str, Any]) -> None:
    for record in manifest["rollback"]["files"].values():
        source, target = root / record["snapshot"], root / record["canonical"]
        atomic_write(target, source.read_bytes())
        if sha256_file(target) != record["sha256"]:
            raise PromotionError(f"rollback checksum mismatch: {target}")


def _rebuild_restored_production(root: Path, manifest: dict[str, Any]) -> None:
    """Regenerate the restored header and rebuild, retaining the binary snapshot as fallback."""
    profile_path = root / "tuning/profiles/builtin-default-v1.json"
    header_path = root / "engine/include/tuning/generated_tuning_values.hpp"
    binary_path = root / "engine/chess-engine"
    previous = read_json(profile_path)
    generated = generate_header(previous).encode("utf-8")
    expected_header = manifest["rollback"]["previousCanonicalHeaderChecksum"]
    if sha256_bytes(generated) != expected_header:
        raise PromotionError("regenerated rollback header does not match the recorded canonical header")
    atomic_write(header_path, generated)
    process = subprocess.run(["make", "-C", "engine", "clean", "all"], cwd=root, text=True, capture_output=True)
    if process.returncode:
        # A failed compiler must not erase the known-good executable snapshot.
        binary_record = manifest["rollback"]["files"]["binary"]
        atomic_write(binary_path, (root / binary_record["snapshot"]).read_bytes())
        raise PromotionError(f"rollback rebuild failed: {process.stdout}{process.stderr}")
    identity = engine_identity(binary_path)
    expected = manifest["rollback"]["previousNormalBinaryIdentity"]
    if (identity["profileId"], identity["profileHash"]) != (expected["profileId"], expected["profileHash"]):
        atomic_write(binary_path, (root / manifest["rollback"]["files"]["binary"]["snapshot"]).read_bytes())
        raise PromotionError("rebuilt rollback engine identity mismatch")


def _canonical_replace(root: Path, manifest: dict[str, Any], fail_at: str | None = None) -> None:
    directory = root / "tuning/promotion" / manifest["promotionId"]
    replacements = [
        (directory / "staged/profile.json", root / "tuning/profiles/builtin-default-v1.json"),
        (directory / "staged/generated_tuning_values.hpp", root / "engine/include/tuning/generated_tuning_values.hpp"),
    ]
    if fail_at == "before_replacement": raise PromotionError("injected failure before replacement")
    try:
        for index, (source, target) in enumerate(replacements):
            atomic_write(target, source.read_bytes())
            if fail_at == "during_replacement" and index == 0: raise PromotionError("injected failure during replacement")
        if fail_at == "final_build": raise PromotionError("injected failure during final build")
        build = subprocess.run(["make", "-C", "engine", "clean", "all"], cwd=root, text=True, capture_output=True)
        if build.returncode: raise PromotionError(f"final canonical build failed: {build.stdout}{build.stderr}")
        if fail_at == "final_verification": raise PromotionError("injected failure during final verification")
        identity = engine_identity(root / "engine/chess-engine")
        expected = manifest["stagedProduction"]
        if (identity["profileId"], identity["profileHash"]) != (expected["profileId"], expected["profileHash"]):
            raise PromotionError("promoted normal engine identity mismatch")
        staged_profile = read_json(directory / "staged/profile.json")
        final_equivalence = compare_engines(
            directory / "staged" / f"chess-engine-{manifest['releaseId']}",
            root / "engine/chess-engine", staged_profile, staged_profile, root,
        )
        if final_equivalence["mismatches"]:
            raise PromotionError(f"post-promotion equivalence failed: {final_equivalence['mismatches']}")
    except Exception:
        _restore_snapshot(root, manifest)
        try:
            _rebuild_restored_production(root, manifest)
        except Exception:
            # _rebuild_restored_production already restores the known-good binary;
            # restore all snapshots once more to guarantee an all-old state.
            _restore_snapshot(root, manifest)
        raise


def promote(promotion: str, expected_candidate_hash: str | None, expected_staged_hash: str | None, approve: bool, root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    directory = _promotion_dir(root, promotion)
    manifest_path = directory / "manifest.json"
    manifest = read_json(manifest_path)
    if not approve: raise PromotionError("promotion requires explicit --approve")
    if not expected_candidate_hash or not expected_staged_hash: raise PromotionError("both expected hashes are required")
    if manifest["state"] != "awaiting_approval": raise PromotionError(f"promotion state is {manifest['state']}, not awaiting_approval")
    if expected_candidate_hash != manifest["sourceCandidate"]["profileHash"]: raise PromotionError("expected candidate hash mismatch")
    if expected_staged_hash != manifest["stagedProduction"]["profileHash"]: raise PromotionError("expected staged hash mismatch")
    policy = load_policy(root)
    gates = {item["gateId"]: item["actualStatus"] for item in manifest.get("validationGates", [])}
    failed_gates = [name for name in policy["requiredGates"] if gates.get(name) != "passed"]
    source_metadata = read_json(directory / "source/metadata.json")
    if failed_gates or (policy["blockDevelopmentOnly"] and source_metadata.get("developmentOnly") is True) or (policy["requirePromotionEligible"] and source_metadata.get("promotionEligible") is not True):
        raise PromotionError(f"promotion policy is no longer satisfied; failed gates: {failed_gates}")
    current_source = read_json(discover_candidate(manifest["sourceCandidate"]["profileId"], root).profile)
    staged = read_json(directory / "staged/profile.json")
    if current_source["canonicalHash"] != expected_candidate_hash: raise PromotionError("candidate mutated after review")
    if staged["canonicalHash"] != expected_staged_hash or profile_hash(staged) != expected_staged_hash: raise PromotionError("staged profile mutated after review")
    with promotion_lock(root, manifest):
        manifest["stateTransitions"].append({"from": "awaiting_approval", "to": "promoting"})
        manifest["state"] = "promoting"
        write_json(manifest_path, manifest)
        try:
            _canonical_replace(root, manifest)
        except Exception as exc:
            manifest["stateTransitions"].append({"from": "promoting", "to": "promotion_failed"})
            manifest["state"], manifest["outcome"], manifest["failureReason"] = "promotion_failed", "promotion_failed", str(exc)
            write_json(manifest_path, manifest)
            raise
        manifest["stateTransitions"].append({"from": "promoting", "to": "promoted"})
        manifest["state"], manifest["outcome"] = "promoted", "profile_promoted"
        write_json(manifest_path, manifest)
    return manifest


def rollback(promotion: str, approve: bool, root: Path | None = None) -> dict[str, Any]:
    root = (root or repo_root()).resolve()
    directory = _promotion_dir(root, promotion)
    manifest_path = directory / "manifest.json"
    manifest = read_json(manifest_path)
    if not approve: raise PromotionError("rollback requires explicit --approve")
    if manifest["state"] != "promoted": raise PromotionError("only a promoted profile may be explicitly rolled back")
    with promotion_lock(root, manifest):
        _restore_snapshot(root, manifest)
        _rebuild_restored_production(root, manifest)
        identity = engine_identity(root / "engine/chess-engine")
        expected = manifest["rollback"]["previousNormalBinaryIdentity"]
        if (identity["profileId"], identity["profileHash"]) != (expected["profileId"], expected["profileHash"]):
            raise PromotionError("restored normal binary identity mismatch")
        manifest["stateTransitions"].append({"from": "promoted", "to": "rolled_back"})
        manifest["state"], manifest["outcome"] = "rolled_back", "profile_rolled_back"
        write_json(manifest_path, manifest)
        write_json(directory / "rollback/result.json", {"schemaVersion": 1, "status": "profile_rolled_back", "restoredIdentity": identity})
    return manifest


def _print_result(value: dict[str, Any]) -> None:
    print(pretty_json(value), end="")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root(), help=argparse.SUPPRESS)
    subparsers = parser.add_subparsers(dest="command", required=True)
    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("--candidate", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--candidate", required=True)
    prepare_parser.add_argument("--release-id", required=True)
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--promotion", required=True)
    verify_parser.add_argument("--skip-regressions", action="store_true", help=argparse.SUPPRESS)
    promote_parser = subparsers.add_parser("promote")
    promote_parser.add_argument("--promotion", required=True)
    promote_parser.add_argument("--expected-candidate-hash")
    promote_parser.add_argument("--expected-staged-hash")
    promote_parser.add_argument("--approve", action="store_true")
    rollback_parser = subparsers.add_parser("rollback")
    rollback_parser.add_argument("--promotion", required=True)
    rollback_parser.add_argument("--approve", action="store_true")
    args = parser.parse_args(argv)
    root = args.repo_root.resolve()
    try:
        if args.command == "inspect": result = inspect_candidate(args.candidate, root)
        elif args.command == "prepare": result = prepare_promotion(args.candidate, args.release_id, root)
        elif args.command == "verify": result = verify_promotion(args.promotion, root, not args.skip_regressions)
        elif args.command == "promote": result = promote(args.promotion, args.expected_candidate_hash, args.expected_staged_hash, args.approve, root)
        else: result = rollback(args.promotion, args.approve, root)
    except (OSError, ValueError, subprocess.SubprocessError, ProfileValidationError, PromotionError) as exc:
        print(f"profile promotion failed: {exc}", file=sys.stderr)
        return 1
    _print_result(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
