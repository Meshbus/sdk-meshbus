# syntax=docker/dockerfile:1
# SPDX-License-Identifier: Apache-2.0

# Reusable Meshbus build-tool image. It contains no Meshbus or west project
# checkout and therefore needs no source credential while it is being built.
#
# Build the tool image:
#   docker build -t meshbus-builder-base .
#
# Run it with the triggering sdk-meshbus checkout mounted read-only. Private
# west projects still need a short-lived credential while `west update` runs:
#   docker run --rm -it \
#     --mount type=bind,src=/absolute/path/to/sdk-meshbus,dst=/workspace/meshbus,readonly \
#     --mount type=bind,src=/absolute/path/to/github.netrc,dst=/root/.netrc,readonly \
#     --mount type=volume,src=meshbus-ccache,dst=/cache/ccache \
#     meshbus-builder-base
# The netrc file uses: machine github.com login x-access-token password <token>
FROM ubuntu:24.04

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG TARGETARCH
ARG CCACHE_VERSION=4.13.2
ARG CCACHE_SHA256_AMD64=e9e2bcec3cd816ba58ff1c331fdeaa3465760683eeec9f462c31b909e2651e35
ARG CCACHE_SHA256_ARM64=101bd788d5eba7db7dace0c346e0f0d68648bcf99716c2a9ce760a072af14bdd
ARG RUST_VERSION=1.98.1
ARG WEST_VERSION=1.5.0
ARG JSONSCHEMA_VERSION=4.26.0
ARG ZEPHYR_SDK_VERSION=1.0.1
ARG ZEPHYR_SDK_SHA256_AMD64=ca9bc0ff66fafca1dac9d592a36d953cf16d096a9d09b1c0357f021cf9f6a7eb
ARG ZEPHYR_SDK_SHA256_ARM64=d79c5bfc68e679488659bea289a4026e52a64f03338875c8c9c850fff13cee30
ARG ZEPHYR_SDK_TOOLCHAINS=arm-zephyr-eabi

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    VIRTUAL_ENV=/opt/venv \
    CARGO_HOME=/opt/cargo \
    RUSTUP_HOME=/opt/rustup \
    ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk \
    CCACHE_DIR=/cache/ccache \
    CCACHE_TEMPDIR=/tmp/ccache \
    PIP_CACHE_DIR=/cache/pip \
    GIT_TERMINAL_PROMPT=0
ENV PATH="/opt/venv/bin:/opt/cargo/bin:${PATH}"

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl device-tree-compiler \
        dfu-util file git gperf libmagic1 libsdl2-dev libssl-dev libudev-dev \
        ninja-build openssh-client pkg-config protobuf-compiler python3-dev \
        python3-pip python3-venv unzip wget xz-utils zip \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv "${VIRTUAL_ENV}" \
    && python3 -m pip install --no-cache-dir --upgrade pip \
    && python3 -m pip install --no-cache-dir \
        "west==${WEST_VERSION}" "jsonschema==${JSONSCHEMA_VERSION}"

# Ubuntu 24.04 ships ccache 4.9.1, below the version accepted by current
# Zephyr. Install an architecture-matched, checksum-verified upstream binary.
RUN case "${TARGETARCH}" in \
        amd64) ccache_arch=x86_64; ccache_sha="${CCACHE_SHA256_AMD64}" ;; \
        arm64) ccache_arch=aarch64; ccache_sha="${CCACHE_SHA256_ARM64}" ;; \
        *) echo "unsupported Docker target architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && ccache_archive="ccache-${CCACHE_VERSION}-linux-${ccache_arch}-glibc.tar.xz" \
    && curl --fail --location --retry 3 --show-error --silent \
        "https://github.com/ccache/ccache/releases/download/v${CCACHE_VERSION}/${ccache_archive}" \
        --output "/tmp/${ccache_archive}" \
    && echo "${ccache_sha}  /tmp/${ccache_archive}" | sha256sum --check - \
    && mkdir -p /tmp/ccache-dist \
    && tar --extract --xz --file "/tmp/${ccache_archive}" \
        --directory /tmp/ccache-dist --strip-components=1 \
    && install -m 0755 /tmp/ccache-dist/ccache /usr/local/bin/ccache \
    && rm -rf "/tmp/${ccache_archive}" /tmp/ccache-dist \
    && ccache --version | head -1

# Release packaging builds the Rust Meshbus CLI. Pin the tested toolchain so
# rebuilding the same Dockerfile does not silently select a newer compiler.
RUN curl --fail --silent --show-error --location https://sh.rustup.rs \
        --output /tmp/rustup-init.sh \
    && sh /tmp/rustup-init.sh -y --profile minimal \
        --default-toolchain "${RUST_VERSION}" --no-modify-path \
    && rm /tmp/rustup-init.sh \
    && rustc --version \
    && cargo --version

# Install only the ARM toolchain required by current Meshbus products. The
# source archive is verified here; setup.sh verifies downloaded components.
RUN case "${TARGETARCH}" in \
        amd64) sdk_arch=x86_64; sdk_sha="${ZEPHYR_SDK_SHA256_AMD64}" ;; \
        arm64) sdk_arch=aarch64; sdk_sha="${ZEPHYR_SDK_SHA256_ARM64}" ;; \
        *) echo "unsupported Docker target architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && sdk_archive="zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-${sdk_arch}_minimal.tar.xz" \
    && curl --fail --location --retry 3 --show-error --silent \
        "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/${sdk_archive}" \
        --output "/tmp/${sdk_archive}" \
    && echo "${sdk_sha}  /tmp/${sdk_archive}" | sha256sum --check - \
    && tar --extract --xz --file "/tmp/${sdk_archive}" --directory /opt \
    && mv "/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}" "${ZEPHYR_SDK_INSTALL_DIR}" \
    && rm "/tmp/${sdk_archive}" \
    && read -r -a toolchains <<< "${ZEPHYR_SDK_TOOLCHAINS}" \
    && test "${#toolchains[@]}" -gt 0 \
    && "${ZEPHYR_SDK_INSTALL_DIR}/setup.sh" -l -h -c \
        -t "${toolchains[@]}" \
    && "${ZEPHYR_SDK_INSTALL_DIR}/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" \
        --version | head -1 \
    && mkdir -p "${CCACHE_DIR}" "${PIP_CACHE_DIR}" "${CCACHE_TEMPDIR}"

WORKDIR /workspace

# At runtime, initialize the mounted manifest and install its exact Python set:
#   west init -l meshbus
#   west update --narrow -o=--depth=1
#   west zephyr-export
#   west packages pip --install -- --cache-dir /cache/pip
#   python3 -m pip install --cache-dir /cache/pip -r meshbus/scripts/requirements.txt
CMD ["/bin/bash"]
