# Build from the repo root: docker build -f server/Dockerfile -t rallypoint-relay .
FROM rust:1-slim AS build
# The release version, compiled into the server so it can report it.
ARG RALLYPOINT_VERSION
ENV RALLYPOINT_VERSION=$RALLYPOINT_VERSION
WORKDIR /src
COPY shared shared
COPY server server
# The full workspace includes the Tauri launcher; the server image only needs these two crates.
RUN printf '[workspace]\nresolver = "2"\nmembers = ["shared", "server"]\n' > Cargo.toml \
 && cargo build --release -p generals-server

FROM debian:stable-slim
RUN useradd --system --uid 10001 relay && mkdir /data && chown relay /data
COPY --from=build /src/target/release/rallypoint-server /usr/local/bin/rallypoint-server
# Links the published GHCR package to this repository (and its public visibility and permissions).
LABEL org.opencontainers.image.source="https://github.com/michaelmurfy/rallypoint" org.opencontainers.image.description="Rallypoint relay server" org.opencontainers.image.licenses="MIT"
USER relay
ENV GENERALS_SERIALS=/data/serials.txt GENERALS_BIND=0.0.0.0:7777
VOLUME /data
EXPOSE 7777/udp
ENTRYPOINT ["rallypoint-server"]
CMD ["run"]
