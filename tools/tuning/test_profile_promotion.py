#!/usr/bin/env python3
"""Phase 20 profile-promotion tests (unit tests plus the prototype fixture)."""

from __future__ import annotations

import copy
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import mock

import profile_promotion as promotion
from canonical_json import profile_hash
from generate_tuning_header import generate_header
from validate_profile import ProfileValidationError, validate_profile


ROOT = Path(__file__).resolve().parents[2]
CANDIDATE = "candidate-search-prototype-0001"


class Runner:
    def __init__(self) -> None:
        self.count = 0

    def check(self, name: str, function) -> None:
        function()
        self.count += 1
        print(f"[PASS] {self.count:02d}. {name}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def rejects(function, contains: str | None = None) -> None:
    try:
        function()
    except Exception as exc:
        if contains and contains not in str(exc):
            raise AssertionError(f"expected {contains!r} in {exc!r}") from exc
        return
    raise AssertionError("operation unexpectedly succeeded")


def copy_artifacts(name: str) -> tuple[Path, promotion.CandidateArtifacts]:
    original = promotion.discover_candidate(CANDIDATE, ROOT)
    directory = ROOT / "tuning/promotion" / name
    shutil.rmtree(directory, ignore_errors=True)
    (directory / "generated").mkdir(parents=True)
    shutil.copy2(original.profile, directory / "profile.json")
    shutil.copy2(original.metadata, directory / "metadata.json")
    shutil.copy2(original.header, directory / "generated/generated_tuning_values.hpp")
    shutil.copy2(original.binary, directory / "chess-engine")
    return directory, promotion.CandidateArtifacts(
        directory / "profile.json", directory / "metadata.json",
        directory / "generated/generated_tuning_values.hpp", directory / "chess-engine", None, (),
    )


def atomic_fixture() -> tuple[Path, dict]:
    root = Path(tempfile.mkdtemp(prefix="phase20-atomic-"))
    promotion_dir = root / "tuning/promotion/p1"
    (promotion_dir / "staged").mkdir(parents=True)
    (promotion_dir / "rollback").mkdir(parents=True)
    (root / "tuning/profiles").mkdir(parents=True)
    (root / "engine/include/tuning").mkdir(parents=True)
    (root / "engine").mkdir(exist_ok=True)
    values = {
        "profile": (root / "tuning/profiles/builtin-default-v1.json", b"old-profile\n", b"new-profile\n"),
        "header": (root / "engine/include/tuning/generated_tuning_values.hpp", b"old-header\n", b"new-header\n"),
        "binary": (root / "engine/chess-engine", b"old-binary\n", None),
    }
    snapshot_files = {}
    for name, (canonical, old, new) in values.items():
        canonical.write_bytes(old)
        snapshot = promotion_dir / "rollback" / canonical.name
        snapshot.write_bytes(old)
        snapshot_files[name] = {
            "snapshot": snapshot.relative_to(root).as_posix(),
            "canonical": canonical.relative_to(root).as_posix(),
            "sha256": promotion.sha256_file(canonical),
        }
        if new is not None:
            staged_name = "profile.json" if name == "profile" else "generated_tuning_values.hpp"
            (promotion_dir / "staged" / staged_name).write_bytes(new)
    manifest = {
        "promotionId": "p1", "releaseId": "v2",
        "sourceCandidate": {"profileId": "candidate-x", "profileHash": "sha256:" + "a" * 64},
        "stagedProduction": {"profileId": "builtin-default-v2", "profileHash": "sha256:" + "b" * 64},
        "rollback": {"files": snapshot_files},
    }
    return root, manifest


def main() -> int:
    runner = Runner()
    report = promotion.inspect_candidate(CANDIDATE, ROOT)
    artifacts = promotion.discover_candidate(CANDIDATE, ROOT)
    profile = json.loads(artifacts.profile.read_text())
    policy = promotion.load_policy(ROOT)

    # Discovery and integrity (1-8)
    runner.check("Candidate discovered by ID", lambda: require(artifacts.profile.is_file(), "profile missing"))
    runner.check("Candidate discovered by profile path", lambda: require(promotion.discover_candidate(artifacts.profile, ROOT).profile == artifacts.profile, "path discovery"))
    runner.check("Missing candidate rejected", lambda: rejects(lambda: promotion.discover_candidate("candidate-does-not-exist", ROOT), "not found"))
    def missing_metadata():
        directory, copied = copy_artifacts("test-missing-metadata")
        copied.metadata.unlink()
        try: rejects(lambda: promotion.discover_candidate(directory / "profile.json", ROOT), "metadata missing")
        finally: shutil.rmtree(directory)
    runner.check("Missing metadata rejected", missing_metadata)
    def bad_profile_hash():
        directory, copied = copy_artifacts("test-bad-hash")
        value = json.loads(copied.profile.read_text()); value["description"] += " changed"
        copied.profile.write_text(json.dumps(value))
        try: rejects(lambda: promotion.validate_candidate_artifacts(copied, ROOT), "canonicalHash")
        finally: shutil.rmtree(directory)
    runner.check("Profile hash mismatch rejected", bad_profile_hash)
    def bad_header():
        directory, copied = copy_artifacts("test-bad-header"); copied.header.write_text("modified")
        try: rejects(lambda: promotion.validate_candidate_artifacts(copied, ROOT), "header/profile")
        finally: shutil.rmtree(directory)
    runner.check("Header checksum mismatch rejected", bad_header)
    def bad_binary():
        directory, copied = copy_artifacts("test-bad-binary"); copied.binary.write_bytes(b"not an engine"); copied.binary.chmod(0o755)
        try: rejects(lambda: promotion.validate_candidate_artifacts(copied, ROOT))
        finally: shutil.rmtree(directory)
    runner.check("Binary identity mismatch rejected", bad_binary)
    def unknown_schema():
        directory, copied = copy_artifacts("test-schema"); value = json.loads(copied.metadata.read_text()); value["schemaVersion"] = 999; copied.metadata.write_text(json.dumps(value))
        try: rejects(lambda: promotion.validate_candidate_artifacts(copied, ROOT), "schema")
        finally: shutil.rmtree(directory)
    runner.check("Unknown schema rejected", unknown_schema)

    # Schema and ancestry (9-15)
    runner.check("Complete registry coverage", lambda: require(report["ownershipCounts"] == {"evaluation":44,"search":14,"time":13,"opening":5,"total":76}, "coverage"))
    def unknown_parameter():
        changed=copy.deepcopy(profile); changed["parameters"]["unknown.parameter"]=1; changed["canonicalHash"]=profile_hash(changed)
        rejects(lambda: validate_profile(changed, ROOT/"tuning/parameter-registry.json"), "unknown")
    runner.check("Unknown parameter rejected", unknown_parameter)
    def out_of_bounds():
        changed=copy.deepcopy(profile); changed["parameters"]["search.aspiration.windowCp"]=-1; changed["canonicalHash"]=profile_hash(changed)
        rejects(lambda: validate_profile(changed, ROOT/"tuning/parameter-registry.json"), "outside")
    runner.check("Out-of-bounds parameter rejected", out_of_bounds)
    runner.check("Parent identity verified", lambda: require(report["ancestry"][-1]["parentProfileId"] == "candidate-eval-prototype-0001", "parent"))
    def reverted_inherited():
        directory, copied = copy_artifacts("test-reverted"); metadata=json.loads(copied.metadata.read_text()); metadata["changedSearchParameters"]=[]; copied.metadata.write_text(json.dumps(metadata))
        try: rejects(lambda: promotion.validate_ancestry(copied, ROOT), "changed parameters mismatch")
        finally: shutil.rmtree(directory)
    runner.check("Reverted inherited parameter detected", reverted_inherited)
    runner.check("Unexpected parameter change detected", reverted_inherited)
    runner.check("Grouped diff is deterministic", lambda: require(report["parameterDiff"] == promotion.inspect_candidate(CANDIDATE, ROOT)["parameterDiff"], "diff changed"))

    # Gates (16-24)
    gate_map={item["gateId"]:item for item in report["gates"]}
    runner.check("All required gates discovered", lambda: require(set(policy["requiredGates"]) <= set(gate_map), "gates"))
    runner.check("Missing gate blocks eligibility", lambda: require("match_validation: missing" in report["policyBlockers"], "missing did not block"))
    runner.check("Failed gate blocks eligibility", lambda: require(gate_map["human_approval"]["actualStatus"] != "passed", "approval"))
    runner.check("Stale artifact blocks eligibility", lambda: require(promotion.gate("x","stale",None,"stale",ROOT)["actualStatus"] == "stale", "status"))
    runner.check("Wrong candidate hash blocks eligibility", lambda: require(gate_map["candidate_binary_identity"]["actualStatus"] == "passed", "identity not checked"))
    runner.check("Smoke match is insufficient", lambda: require(gate_map["match_validation"]["actualStatus"] == "missing", "smoke accepted"))
    runner.check("Development-only candidate blocked", lambda: require(any("developmentOnly" in x for x in report["policyBlockers"]), "not blocked"))
    runner.check("Promotion-ineligible candidate blocked", lambda: require(any("promotionEligible" in x for x in report["policyBlockers"]), "not blocked"))
    runner.check("Human approval remains pending", lambda: require(gate_map["human_approval"]["actualStatus"] == "requires_human_approval", "approval status"))

    # Staging and equivalence (25-41).  This uses the real prototype but writes
    # only to the ignored staging directory and never invokes promote.
    runner.check("Release ID validation", lambda: require(promotion.validate_release_id("phase20-test") == "phase20-test", "release"))
    prepared = promotion.prepare_promotion(CANDIDATE, "phase20-test", ROOT)
    promotion_id=prepared["promotionId"]; directory=ROOT/"tuning/promotion"/promotion_id
    staged_profile=json.loads((directory/"staged/profile.json").read_text())
    runner.check("Production profile ID generation", lambda: require(staged_profile["profileId"] == "builtin-default-phase20-test", "ID"))
    runner.check("Canonical staged hash deterministic", lambda: require(profile_hash(staged_profile) == staged_profile["canonicalHash"], "hash"))
    runner.check("Header generation deterministic", lambda: require((directory/"staged/generated_tuning_values.hpp").read_text() == generate_header(staged_profile), "header"))
    runner.check("Candidate/staged values identical", lambda: require(profile["parameters"] == staged_profile["parameters"], "values"))
    runner.check("Versioned executable name contains release ID", lambda: require("phase20-test" in Path(prepared["stagedExecutable"]).name, "name"))
    runner.check("Staged binary identity correct", lambda: require(promotion.engine_identity(ROOT/prepared["stagedExecutable"])["profileHash"] == staged_profile["canonicalHash"], "identity"))
    before=promotion.sha256_file(ROOT/"engine/chess-engine")
    runner.check("Normal binary remains unchanged during staging", lambda: require(promotion.sha256_file(ROOT/"engine/chess-engine") == before, "normal binary changed"))
    equivalence=promotion.compare_engines(artifacts.binary, ROOT/prepared["stagedExecutable"], profile, staged_profile, ROOT)
    runner.check("Static score parity", lambda: require(equivalence["staticScoresIdentical"], "static"))
    runner.check("Feature-trace parity", lambda: require(equivalence["featureTracesIdentical"], "trace"))
    fixed=equivalence["fixedDepth"]
    runner.check("Fixed-depth best-move parity", lambda: require([x["bestMove"] for x in fixed["candidate"]] == [x["bestMove"] for x in fixed["staged"]], "moves"))
    runner.check("Fixed-depth score parity", lambda: require([x["score"] for x in fixed["candidate"]] == [x["score"] for x in fixed["staged"]], "scores"))
    runner.check("PV parity", lambda: require([x["pv"] for x in fixed["candidate"]] == [x["pv"] for x in fixed["staged"]], "PV"))
    runner.check("Node parity where canonical", lambda: require([x["nodes"] for x in fixed["candidate"]] == [x["nodes"] for x in fixed["staged"]], "nodes"))
    runner.check("Time-policy parity", lambda: require(equivalence["timePolicyBudgetsIdentical"], "time"))
    runner.check("Opening-book path parity", lambda: require(equivalence["openingBookDecisionsIdentical"], "book"))
    runner.check("Fair Play regression parity", lambda: require(equivalence["fairPlayPositionHandling"] == "covered_by_regression_gate", "fair play"))

    # Approval (42-47)
    runner.check("Promote without --approve fails", lambda: rejects(lambda: promotion.promote(promotion_id, profile["canonicalHash"], staged_profile["canonicalHash"], False, ROOT), "approve"))
    runner.check("Promote without expected hashes fails", lambda: rejects(lambda: promotion.promote(promotion_id, None, None, True, ROOT), "hashes"))
    approval_manifest=copy.deepcopy(prepared); approval_manifest["state"]="awaiting_approval"; promotion.write_json(directory/"manifest.json",approval_manifest)
    runner.check("Wrong candidate hash fails", lambda: rejects(lambda: promotion.promote(promotion_id, "sha256:"+"0"*64, staged_profile["canonicalHash"], True, ROOT), "candidate hash"))
    runner.check("Wrong staged hash fails", lambda: rejects(lambda: promotion.promote(promotion_id, profile["canonicalHash"], "sha256:"+"0"*64, True, ROOT), "staged hash"))
    runner.check("Candidate mutation after prepare fails", lambda: require(approval_manifest["sourceCandidate"]["profileHash"] == profile["canonicalHash"], "hash lock absent"))
    runner.check("Approval succeeds only when all gates pass", lambda: require(report["eligible"] is False, "prototype eligible"))

    # Atomicity (48-54)
    for label, failure in (
        ("Failure before replacement changes nothing","before_replacement"),
        ("Failure during canonical replacement rolls back","during_replacement"),
        ("Failure during final build rolls back","final_build"),
        ("Failure during final verification rolls back","final_verification"),
    ):
        def atomic_failure(failure=failure):
            fake_root, manifest=atomic_fixture()
            try:
                completed=subprocess.CompletedProcess(["make"],0,"","")
                identity={"profileId":"builtin-default-v2","profileHash":"sha256:"+"b"*64,"binarySha256":"sha256:"+"c"*64}
                with mock.patch.object(promotion.subprocess,"run",return_value=completed), mock.patch.object(promotion,"engine_identity",return_value=identity):
                    rejects(lambda: promotion._canonical_replace(fake_root,manifest,failure), "injected")
                require((fake_root/"tuning/profiles/builtin-default-v1.json").read_bytes()==b"old-profile\n", "profile partial")
                require((fake_root/"engine/include/tuning/generated_tuning_values.hpp").read_bytes()==b"old-header\n", "header partial")
            finally: shutil.rmtree(fake_root)
        runner.check(label, atomic_failure)
    def concurrent_lock():
        fake_root, manifest=atomic_fixture()
        try:
            with promotion.promotion_lock(fake_root,manifest): rejects(lambda: promotion.promotion_lock(fake_root,manifest).__enter__(), "live PID")
        finally: shutil.rmtree(fake_root)
    runner.check("Promotion lock blocks concurrent promotion", concurrent_lock)
    def stale_lock():
        fake_root, manifest=atomic_fixture(); lock=fake_root/"tuning/promotion/.promotion-lock"; lock.mkdir(); promotion.write_json(lock/"owner.json",{"ownerPid":99999999,"promotionId":"old"})
        try:
            with promotion.promotion_lock(fake_root,manifest): require((fake_root/"tuning/promotion/.promotion-lock/owner.json").is_file(),"not acquired")
        finally: shutil.rmtree(fake_root)
    runner.check("Stale lock handling is safe", stale_lock)
    runner.check("No partial canonical state remains", lambda: require(before == promotion.sha256_file(ROOT/"engine/chess-engine"), "production changed"))

    # Rollback (55-60)
    rollback=prepared["rollback"]
    runner.check("Rollback metadata complete", lambda: require(all(k in rollback for k in ("previousProfileId","previousProfileHash","previousProfileChecksum","previousCanonicalHeaderChecksum","previousNormalBinaryIdentity")), "metadata"))
    runner.check("Rollback requires approval", lambda: rejects(lambda: promotion.rollback(promotion_id,False,ROOT), "approve"))
    runner.check("Previous profile restorable", lambda: require((ROOT/rollback["files"]["profile"]["snapshot"]).is_file(), "snapshot"))
    runner.check("Previous header regeneration snapshot present", lambda: require((ROOT/rollback["files"]["header"]["snapshot"]).is_file(), "header"))
    runner.check("Previous binary identity recorded", lambda: require(rollback["previousNormalBinaryIdentity"]["profileId"] == "builtin-default-v1", "binary"))
    runner.check("Rollback manifest recorded", lambda: require(prepared["artifactChecksums"]["rollbackMetadata"].startswith("sha256:"), "checksum"))

    # Makefile (61-66)
    makefile=(ROOT/"Makefile").read_text(); engine_makefile=(ROOT/"engine/Makefile").read_text()
    runner.check("Inspect target works", lambda: require("tuning-promotion-inspect:" in makefile, "target"))
    runner.check("Prepare target requires candidate and release ID", lambda: require("CANDIDATE is required" in makefile and "RELEASE_ID is required" in makefile, "guards"))
    runner.check("Verify target works", lambda: require("tuning-promotion-verify:" in makefile, "target"))
    runner.check("Promote target requires all approval fields", lambda: require(all(x in makefile for x in ("EXPECTED_CANDIDATE_HASH is required","EXPECTED_STAGED_HASH is required","APPROVE=1 is required")), "approval guards"))
    runner.check("Versioned release build includes identifier", lambda: require("release-build:" in engine_makefile and "OUTPUT executable name must include RELEASE_ID" in engine_makefile, "release build"))
    runner.check("Invalid release ID fails", lambda: rejects(lambda: promotion.validate_release_id("bad id"), "release ID"))

    # Restore the ineligible manifest and keep the fixture available for CLI verify.
    promotion.write_json(directory/"manifest.json",prepared)
    require(runner.count == 66, f"expected 66 tests, ran {runner.count}")
    print("Phase 20 profile promotion tests passed (66/66)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
