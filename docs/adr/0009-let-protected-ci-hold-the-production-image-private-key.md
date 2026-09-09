---
status: superseded by ADR-0010
---

# Let protected CI hold the production image private key

Historical policy, superseded by [ADR 0010](0010-use-native-zephyr-build-signing.md).
The separate-job requirement below no longer governs current release builds.

Firmware 1.0.0 stores the exportable Ed25519 Production Image private key as a
PEM secret in a protected production-signing CI environment. Every successful
C2 application build triggered by a push to the protected `main` branch enters
a dedicated signing job without per-run human approval. This favors continuous
availability of trusted engineering images while explicitly accepting that a
malicious merge, authorized workflow compromise, or runner compromise can sign
an unauthorized image or exfiltrate the long-lived production key.

## Consequences

- The PEM is an environment-scoped production secret, not a repository file,
  general repository secret, organization-wide secret, cache, or artifact.
- Only a `push` to the protected `main` branch can enter the production-signing
  environment. Pull requests, forks, untrusted workflow inputs, tags, and other
  branches cannot directly trigger this job.
- Merging to `main` is production-image signing authorization. Branch protection
  therefore requires review, blocks force pushes and deletion, and gives
  trust-sensitive workflow, signing, and boot-policy paths explicit ownership.
- The production-signing job uses an ephemeral runner, minimum token
  permissions, and audited immutable actions and signing code. It does not run
  arbitrary build output as executable code.
- Build and signing are separate jobs. The signer checks the unsigned artifact
  digest, C2 target, version, source revision, protected ref, and workflow
  revision before exposing the PEM.
- The job writes the PEM only to a private temporary file, never prints or
  uploads it, and removes it before completion. Secret masking is not treated as
  protection against malicious job code.
- The resulting application is verified with the public-only Production Image
  Key before packaging. Its record includes the key identifier, public-key
  fingerprint, input digest, output digest, source revision, and workflow
  revision.
- An automatically signed `main` artifact remains an Engineering Candidate. It
  is not publishable, does not become a Release Baseline, and does not create a
  tag or public release until the separate GA gates pass.
- An encrypted offline backup and a tested rotation procedure remain required.
  Suspected disclosure immediately revokes CI access and blocks publication.
- A stolen key can authorize malicious applications on every stock C2 MCUboot
  that trusts it. Recovering that trust root requires distributing and
  programming replacement MCUboot firmware over SWD.
- The DFOTA Manifest private key remains separate and offline unless another
  decision explicitly changes its custody.
