# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

"""Deterministic FIRMWARE control-plane fault model.

This model proves retry/restart convergence only.  It is not firmware, RF,
flash-driver, MCUboot, or hardware evidence.
"""

from dataclasses import dataclass, field
from hashlib import sha256


CHUNK_SIZE = 448
PATCH_MAX = 24 * 1024


class Fault(RuntimeError):
    pass


@dataclass
class Manifest:
    transfer_id: bytes
    patch_hash: bytes
    patch_size: int
    source: bytes = b"source"
    role: str = "repeater"
    board: str = "devkit_nrf54l15"
    signature_valid: bool = True
    security_counter: int = 2


@dataclass
class Journal:
    generation: int
    state: str
    transfer_id: bytes
    durable: int
    valid: bool = True


@dataclass
class Target:
    source: bytes = b"source"
    role: str = "repeater"
    board: str = "devkit_nrf54l15"
    security_counter: int = 1
    safe_receive: bool = True
    safe_apply: bool = True
    shutdown_pending: bool = False
    state: str = "idle"
    manifest: Manifest | None = None
    staged: bytearray = field(default_factory=bytearray)
    durable: int = 0
    journals: list[Journal] = field(default_factory=list)
    apply_count: int = 0
    activate_count: int = 0

    def _commit(self) -> None:
        generation = self.journals[-1].generation + 1 if self.journals else 1
        self.journals.append(Journal(generation, self.state, self.transfer_id, self.durable))
        self.journals = self.journals[-4:]

    @property
    def transfer_id(self) -> bytes:
        return self.manifest.transfer_id if self.manifest else bytes(32)

    def status(self) -> dict:
        return {"state": self.state, "transfer_id": self.transfer_id, "durable": self.durable,
                "next": len(self.staged), "safe_receive": self.safe_receive,
                "safe_apply": self.safe_apply, "shutdown": self.shutdown_pending}

    def begin(self, manifest: Manifest) -> dict:
        if not manifest.signature_valid:
            raise Fault("bad_signature")
        if manifest.source != self.source:
            raise Fault("wrong_source")
        if manifest.role != self.role or manifest.board != self.board:
            raise Fault("wrong_target")
        if manifest.security_counter < self.security_counter:
            raise Fault("downgrade")
        if not 88 < manifest.patch_size <= PATCH_MAX:
            raise Fault("unsupported_size")
        if self.shutdown_pending or not self.safe_receive:
            raise Fault("unsafe_receive")
        if self.manifest and self.transfer_id == manifest.transfer_id and self.state not in {"failed", "aborted"}:
            return self.status()
        if self.state in {"receiving", "staged", "verified", "applying", "pending_reboot", "testing"}:
            raise Fault("conflict")
        self.manifest = manifest
        self.staged.clear()
        self.durable = 0
        self.state = "receiving"
        self._commit()
        return self.status()

    def write(self, transfer_id: bytes, offset: int, data: bytes) -> dict:
        if transfer_id != self.transfer_id or self.state != "receiving":
            raise Fault("conflict")
        if self.shutdown_pending or not self.safe_receive:
            raise Fault("unsafe_receive")
        expected = min(CHUNK_SIZE, self.manifest.patch_size - offset)
        if offset % CHUNK_SIZE or len(data) != expected:
            raise Fault("bad_offset")
        if offset < len(self.staged):
            if self.staged[offset : offset + len(data)] != data:
                raise Fault("duplicate_conflict")
            return self.status()
        if offset != len(self.staged):
            raise Fault("bad_offset")
        self.staged.extend(data)
        chunks = len(self.staged) // CHUNK_SIZE
        if chunks % 4 == 0 or len(self.staged) == self.manifest.patch_size:
            self.durable = len(self.staged)
            self._commit()
        return self.status()

    def finish(self, transfer_id: bytes) -> dict:
        if transfer_id != self.transfer_id:
            raise Fault("conflict")
        if self.state == "verified":
            return self.status()
        if self.state not in {"receiving", "staged"}:
            raise Fault("bad_state")
        if self.state == "receiving" and len(self.staged) != self.manifest.patch_size:
            raise Fault("incomplete")
        if self.state == "receiving":
            self.state = "staged"
            self.durable = len(self.staged)
            self._commit()
        if sha256(self.staged).digest() != self.manifest.patch_hash:
            self.state = "failed"
            self._commit()
            raise Fault("patch_hash")
        self.state = "verified"
        self._commit()
        return self.status()

    def apply(self, transfer_id: bytes) -> dict:
        if transfer_id != self.transfer_id:
            raise Fault("conflict")
        if self.state in {"applying", "pending_reboot"}:
            return self.status()
        if self.shutdown_pending or not self.safe_apply:
            raise Fault("unsafe_apply")
        if self.state != "verified":
            raise Fault("bad_state")
        self.state = "applying"
        self.apply_count += 1
        self._commit()
        self.state = "pending_reboot"
        self._commit()
        return self.status()

    def activate(self, transfer_id: bytes) -> dict:
        if transfer_id != self.transfer_id or self.state not in {"pending_reboot", "testing"}:
            raise Fault("bad_state")
        if self.shutdown_pending or not self.safe_apply:
            raise Fault("unsafe_apply")
        if self.state == "pending_reboot":
            self.activate_count += 1
            self.state = "testing"
            self._commit()
        return self.status()

    def health(self, healthy: bool) -> dict:
        if self.state != "testing":
            raise Fault("bad_state")
        self.state = "confirmed" if healthy else "rolled_back"
        if healthy:
            self.security_counter = self.manifest.security_counter
            self.source = b"target"
        self._commit()
        return self.status()

    def restart(self) -> None:
        valid = [journal for journal in self.journals if journal.valid]
        if not valid:
            self.state, self.manifest, self.staged, self.durable = "idle", None, bytearray(), 0
            return
        newest = max(valid, key=lambda journal: journal.generation)
        self.state = newest.state
        self.durable = newest.durable
        del self.staged[self.durable :]
        if self.state == "applying":
            self.state = "pending_reboot"
        elif self.state == "pending_reboot":
            self.state = "testing"


@dataclass
class Bearer:
    target: Target
    route: bool = True
    floods: int = 0
    lose_next_response: bool = False

    def call(self, method: str, *args):
        if not self.route:
            raise Fault("no_route")
        result = getattr(self.target, method)(*args)
        if self.lose_next_response:
            self.lose_next_response = False
            raise Fault("lost_response")
        return result


def send_patch(bearer: Bearer, manifest: Manifest, patch: bytes) -> dict:
    status = bearer.target.status()
    if status["state"] == "idle":
        bearer.call("begin", manifest)
    offset = bearer.target.status()["next"]
    while offset < len(patch):
        end = min(offset + CHUNK_SIZE, len(patch))
        try:
            status = bearer.call("write", manifest.transfer_id, offset, patch[offset:end])
        except Fault as error:
            if str(error) != "lost_response":
                raise
            status = bearer.target.status()
            if status["next"] < end:
                status = bearer.call("write", manifest.transfer_id, offset, patch[offset:end])
        offset = status["next"]
    return bearer.call("finish", manifest.transfer_id)
