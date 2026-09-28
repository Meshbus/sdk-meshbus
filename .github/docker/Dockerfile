# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
FROM rust:1.97.1-slim-bookworm@sha256:2775a09d208ff0d7c1f50490c45b62db929e87ba1dcbc3f2132ac71a704bcdd3 AS rust
FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3
LABEL org.opencontainers.image.source="https://github.com/Meshbus/sdk-meshbus" \
      org.meshbus.builder.schema="1"
SHELL ["/bin/bash", "-euo", "pipefail", "-c"]
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8 \
    VIRTUAL_ENV=/opt/venv RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo \
    ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-1.0.1 \
    PATH=/opt/venv/bin:/opt/cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
# Only the tools image admits weekly OS patch refreshes. Each job updates
# preloaded dependencies directly from its source manifest.
RUN test "$(dpkg --print-architecture)" = amd64 && \
    sed -i '/^Types:/a Architectures: amd64 i386' /etc/apt/sources.list.d/ubuntu.sources && \
    dpkg --add-architecture arm64 && dpkg --add-architecture i386 && \
    printf '%s\n' 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble main universe' \
      'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-updates main universe' \
      'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-security main universe' \
      > /etc/apt/sources.list.d/arm64.list && \
    apt-get update && apt-get upgrade -y && apt-get install -y --no-install-recommends \
      ca-certificates curl wget git openssh-client xz-utils unzip zip file locales \
      build-essential cmake ninja-build gperf device-tree-compiler dfu-util \
      python3 python3-venv python3-dev pkg-config libudev-dev libudev-dev:arm64 \
      libusb-1.0-0-dev libssl-dev clang clang-format llvm lld shellcheck \
      gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libsdl2-dev libmagic1 \
      gcc-13-multilib g++-13-multilib linux-libc-dev:i386 \
      && rm -rf /var/lib/apt/lists/* && python3 -m venv /opt/venv
COPY --from=rust /usr/local/rustup /opt/rustup
COPY --from=rust /usr/local/cargo /opt/cargo
COPY requirements.txt requirements.lock /opt/builder/
RUN pip install --no-cache-dir --no-deps -r /opt/builder/requirements.lock && pip check
COPY install-tools.py retain-cargo-notices.py /opt/builder/
RUN python /opt/builder/install-tools.py && \
    rustup component add rustfmt clippy && \
    rustup target add aarch64-unknown-linux-gnu x86_64-pc-windows-msvc aarch64-pc-windows-msvc && \
    cargo install --locked cargo-xwin --version 0.23.1 && \
    python /opt/builder/retain-cargo-notices.py && \
    rm -rf /opt/cargo/registry /opt/cargo/git && \
    pip freeze > /opt/builder/python-freeze.txt && \
    dpkg-query -W > /opt/builder/debian-packages.txt && \
    rustc -vV > /opt/builder/rust.txt
# Microsoft SDK/CRT are acquired by cargo-xwin in the job, never in this image.
# Preload shallow repositories for west update --path-cache in consumer jobs.
COPY --from=source west.yml /opt/west-workspace/meshbus/west.yml
RUN cd /opt/west-workspace && west init -l meshbus && \
    west update --narrow -o=--depth=1 && \
    west manifest --freeze --active-only > west-frozen.yml && \
    cp west-frozen.yml /opt/builder/west-seed.yml
ENV MESHBUS_WEST_SEED=/opt/west-workspace
WORKDIR /work
