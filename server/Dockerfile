# Build from the repo root: docker build -f server/Dockerfile -t generals-relay .
FROM rust:1-slim AS build
WORKDIR /src
COPY shared shared
COPY server server
# The full workspace includes the Tauri launcher; the server image only needs these two crates.
RUN printf '[workspace]\nresolver = "2"\nmembers = ["shared", "server"]\n' > Cargo.toml \
 && cargo build --release -p generals-server

FROM debian:stable-slim
RUN useradd --system --uid 10001 relay && mkdir /data && chown relay /data
COPY --from=build /src/target/release/generals-server /usr/local/bin/generals-server
USER relay
ENV GENERALS_SERIALS=/data/serials.txt GENERALS_BIND=0.0.0.0:7777
VOLUME /data
EXPOSE 7777/udp
ENTRYPOINT ["generals-server"]
CMD ["run"]
