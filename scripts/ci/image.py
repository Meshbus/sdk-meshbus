# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Resolve the public builder channel once; downstream jobs receive a digest."""

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request


def resolve(repository, reference="stable"):
    repository = repository.lower()
    if not re.fullmatch(r"[a-z0-9_.-]+/[a-z0-9_.-]+", repository):
        raise ValueError("expected a GitHub owner/repository name")
    if not re.fullmatch(r"[a-zA-Z0-9_.-]+|sha256:[0-9a-f]{64}", reference):
        raise ValueError("invalid image reference")
    package = repository + "-builder"
    query = urllib.parse.urlencode({"service": "ghcr.io", "scope": f"repository:{package}:pull"})
    # Always anonymous, including for maintainer runs. This verifies the same
    # registry-access boundary that a fork PR will have.
    with urllib.request.urlopen(f"https://ghcr.io/token?{query}", timeout=30) as response:
        token = json.load(response)["token"]
    request = urllib.request.Request(f"https://ghcr.io/v2/{package}/manifests/{reference}", headers={
        "Authorization": f"Bearer {token}",
        "Accept": ", ".join(("application/vnd.oci.image.index.v1+json",
                             "application/vnd.oci.image.manifest.v1+json",
                             "application/vnd.docker.distribution.manifest.list.v2+json",
                             "application/vnd.docker.distribution.manifest.v2+json")),
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        digest = response.headers.get("Docker-Content-Digest", "")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError("registry did not return an immutable image digest")
    return f"ghcr.io/{package}@{digest}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--reference", default="stable")
    args = parser.parse_args()
    try:
        image = resolve(args.repository, args.reference)
    except urllib.error.HTTPError as error:
        print(f"Public builder unavailable (HTTP {error.code}). Run the Builder image workflow "
              "and verify the GHCR package is public before starting CI.", file=sys.stderr)
        raise SystemExit(1) from None
    print(image)
    if output := os.environ.get("GITHUB_OUTPUT"):
        with open(output, "a") as stream:
            stream.write(f"image={image}\n")


if __name__ == "__main__":
    main()
