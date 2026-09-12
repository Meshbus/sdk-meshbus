# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

from hashlib import sha256
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from firmware_model import Bearer, Fault, Manifest, Target, send_patch


def package(size=9 * 1024):
    patch = bytes((index * 17) & 0xFF for index in range(size))
    manifest = Manifest(sha256(b"manifest" + size.to_bytes(4, "big")).digest(),
                        sha256(patch).digest(), len(patch))
    return manifest, patch


def test_lost_write_response_reconciles_without_duplicate_progress():
    manifest, patch = package()
    target, bearer = Target(), Bearer(Target())
    target = bearer.target
    target.begin(manifest)
    bearer.lose_next_response = True
    send_patch(bearer, manifest, patch)
    assert target.state == "verified"
    assert bytes(target.staged) == patch
    assert target.durable == len(patch)


def test_restart_discards_only_non_durable_tail_then_resumes():
    manifest, patch = package()
    target = Target()
    target.begin(manifest)
    for offset in range(0, 5 * 448, 448):
        target.write(manifest.transfer_id, offset, patch[offset : offset + 448])
    assert len(target.staged) == 5 * 448 and target.durable == 4 * 448
    target.restart()
    assert len(target.staged) == 4 * 448
    send_patch(Bearer(target), manifest, patch)
    assert target.state == "verified"


def test_missing_direct_route_never_floods():
    manifest, _ = package()
    bearer = Bearer(Target(), route=False)
    with pytest.raises(Fault, match="no_route"):
        bearer.call("begin", manifest)
    assert bearer.floods == 0
    assert bearer.target.state == "idle"


@pytest.mark.parametrize("field,value,error", [
    ("signature_valid", False, "bad_signature"),
    ("source", b"other", "wrong_source"),
    ("role", "sensor", "wrong_target"),
    ("board", "idea_mesh_tracker_c2", "wrong_target"),
    ("security_counter", 0, "downgrade"),
])
def test_manifest_admission_fails_before_patch_mutation(field, value, error):
    manifest, _ = package()
    setattr(manifest, field, value)
    target = Target()
    with pytest.raises(Fault, match=error):
        target.begin(manifest)
    assert target.state == "idle" and not target.staged and not target.journals


def test_power_gates_pause_receive_and_block_apply():
    manifest, patch = package(896)
    target = Target(safe_receive=False)
    with pytest.raises(Fault, match="unsafe_receive"):
        target.begin(manifest)
    target.safe_receive = True
    send_patch(Bearer(target), manifest, patch)
    target.safe_apply = False
    with pytest.raises(Fault, match="unsafe_apply"):
        target.apply(manifest.transfer_id)
    assert target.state == "verified"


def test_apply_and_activation_are_idempotent_after_lost_responses():
    manifest, patch = package(896)
    target, bearer = Target(), Bearer(Target())
    target = bearer.target
    send_patch(bearer, manifest, patch)
    bearer.lose_next_response = True
    with pytest.raises(Fault, match="lost_response"):
        bearer.call("apply", manifest.transfer_id)
    target.apply(manifest.transfer_id)
    assert target.apply_count == 1 and target.state == "pending_reboot"
    bearer.lose_next_response = True
    with pytest.raises(Fault, match="lost_response"):
        bearer.call("activate", manifest.transfer_id)
    target.activate(manifest.transfer_id)
    assert target.activate_count == 1 and target.state == "testing"
    assert target.health(True)["state"] == "confirmed"


def test_corrupt_newest_journal_falls_back_to_prior_checkpoint():
    manifest, patch = package(4 * 448)
    target = Target()
    target.begin(manifest)
    for offset in range(0, len(patch), 448):
        target.write(manifest.transfer_id, offset, patch[offset : offset + 448])
    assert target.durable == len(patch)
    target.journals[-1].valid = False
    target.restart()
    assert target.state == "receiving" and target.durable == 0


def test_restart_from_staged_can_repeat_finish_verification():
    manifest, patch = package(896)
    target = Target()
    target.begin(manifest)
    for offset in range(0, len(patch), 448):
        target.write(manifest.transfer_id, offset, patch[offset : offset + 448])
    target.state = "staged"
    target._commit()
    target.restart()
    assert target.state == "staged"
    assert target.finish(manifest.transfer_id)["state"] == "verified"


def test_manual_reboot_from_pending_uses_boot_state_not_activate_intent():
    manifest, patch = package(896)
    target = Target()
    send_patch(Bearer(target), manifest, patch)
    target.apply(manifest.transfer_id)
    assert target.state == "pending_reboot"
    target.restart()
    assert target.state == "testing"
    assert target.health(True)["state"] == "confirmed"


@pytest.mark.parametrize("healthy,expected", [(True, "confirmed"), (False, "rolled_back")])
def test_testing_health_controls_confirmation_or_rollback(healthy, expected):
    manifest, patch = package(896)
    target = Target()
    send_patch(Bearer(target), manifest, patch)
    target.apply(manifest.transfer_id)
    target.activate(manifest.transfer_id)
    assert target.health(healthy)["state"] == expected
