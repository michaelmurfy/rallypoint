# syntax=docker/dockerfile:1
# Build from the repo root: docker build -f server/Dockerfile -t rallypoint-relay .
# One image for amd64, arm64 (Raspberry Pi 3/4/5 on a 64-bit OS, ARM cloud servers) and armv7 (32-bit Raspberry Pi OS):
#   docker buildx build --platform linux/amd64,linux/arm64,linux/arm/v7 -f server/Dockerfile .
# The server is compiled on the build machine for each target (no emulation), so every platform builds in a minute or two.
FROM --platform=$BUILDPLATFORM rust:1-slim AS build
ARG TARGETARCH
ARG TARGETVARIANT
# The release version, compiled into the server so it can report it.
ARG RALLYPOINT_VERSION
ENV RALLYPOINT_VERSION=$RALLYPOINT_VERSION
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu libc6-dev-arm64-cross gcc-arm-linux-gnueabihf libc6-dev-armhf-cross \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY shared shared
COPY server server
# The full workspace includes the Tauri launcher; the server image only needs these two crates.
RUN set -e; \
    case "${TARGETARCH}${TARGETVARIANT}" in \
      amd64) T=x86_64-unknown-linux-gnu ;; \
      arm64) T=aarch64-unknown-linux-gnu; export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc ;; \
      armv7) T=armv7-unknown-linux-gnueabihf; export CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER=arm-linux-gnueabihf-gcc ;; \
      *) echo "unsupported platform ${TARGETARCH}${TARGETVARIANT} (amd64, arm64 and armv7 are built)"; exit 1 ;; \
    esac; \
    rustup target add "$T"; \
    printf '[workspace]\nresolver = "2"\nmembers = ["shared", "server"]\n' > Cargo.toml; \
    cargo build --release -p generals-server --target "$T"; \
    mkdir -p /out /data; \
    cp "target/$T/release/rallypoint-server" /out/

# Just the binary, for `--target binary --output type=local,dest=out` (the release workflow packs these for download).
FROM scratch AS binary
COPY --from=build /out/rallypoint-server /rallypoint-server

# No RUN here, so the image for another CPU can be assembled without emulating it.
FROM debian:stable-slim
COPY --from=build /out/rallypoint-server /usr/local/bin/rallypoint-server
COPY --from=build --chown=10001:10001 /data /data
# Links the published GHCR package to this repository (and its public visibility and permissions).
LABEL org.opencontainers.image.source="https://github.com/michaelmurfy/rallypoint" org.opencontainers.image.description="Rallypoint relay server" org.opencontainers.image.licenses="MIT"
USER 10001:10001
ENV GENERALS_SERIALS=/data/serials.txt GENERALS_BIND=0.0.0.0:7777
VOLUME /data
EXPOSE 7777/udp
ENTRYPOINT ["rallypoint-server"]
CMD ["run"]
