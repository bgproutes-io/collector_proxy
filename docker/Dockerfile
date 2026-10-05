# syntax=docker/dockerfile:1.7

ARG DEBIAN_VERSION=bookworm-slim

FROM debian:${DEBIAN_VERSION} AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        build-essential \
        libssl-dev \
        libtool \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# libbmproutes is supplied as a BuildKit named build context. See
# build_image.sh; this keeps the private dependency out of this repository and
# avoids embedding Git credentials in an image layer.
WORKDIR /build/libbmproutes
COPY --from=libbmproutes . .
RUN autoreconf --force --install \
    && ./configure --prefix=/usr/local \
    && make -j"$(nproc)" \
    && make install \
    && ldconfig

WORKDIR /build/proxy
COPY proxy/ .
RUN autoreconf --force --install \
    && ./configure --prefix=/usr/local \
    && make -j"$(nproc)" \
    && make check


FROM debian:${DEBIAN_VERSION} AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        ca-certificates \
        libssl3 \
        openssl \
        python3 \
        python3-requests \
        tini \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 bgproutes \
    && useradd --uid 10001 --gid bgproutes --no-create-home \
        --shell /usr/sbin/nologin bgproutes \
    && install -d -o bgproutes -g bgproutes -m 0700 \
        /var/lib/bgproutes/tls /run/bgproutes /etc/bgproutes

COPY --from=builder /usr/local/lib/libbmproutes.so.1.0.0 /usr/local/lib/
COPY --from=builder /build/proxy/proxy /usr/local/bin/collector-proxy
COPY certificates/cert_generator.py /usr/local/libexec/bgproutes/cert_generator.py
COPY run.sh /usr/local/bin/run.sh
COPY proxy.conf /etc/bgproutes/proxy.conf

RUN chmod 0755 \
        /usr/local/bin/collector-proxy \
        /usr/local/bin/run.sh \
        /usr/local/libexec/bgproutes/cert_generator.py \
    && ln -s libbmproutes.so.1.0.0 /usr/local/lib/libbmproutes.so.1 \
    && ldconfig

ENV PROXY_CONFIG=/etc/bgproutes/proxy.conf

VOLUME ["/var/lib/bgproutes/tls"]

USER bgproutes:bgproutes

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/run.sh"]
