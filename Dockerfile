FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake zlib1g-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY source/ /star/source/

WORKDIR /star/source
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    zlib1g libgomp1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash star

COPY --from=builder /star/source/build/STAR /usr/local/bin/STAR

USER star
WORKDIR /home/star

HEALTHCHECK --interval=60s --timeout=5s \
    CMD ["STAR", "--version"] || exit 1

ENTRYPOINT ["STAR"]
