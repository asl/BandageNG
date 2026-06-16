FROM ubuntu:26.04 AS base
LABEL org.opencontainers.image.authors="Thomas Roder"

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=en_US.UTF-8 \
    LANGUAGE=en_US:en \
    LC_ALL=en_US.UTF-8

# Generate locale once in the base image
RUN apt-get update \
    && apt-get install -y --no-install-recommends locales \
    && locale-gen en_US.UTF-8

# ==========================================
FROM base AS build

RUN apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    ninja-build \
    pkg-config \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-svg-dev

WORKDIR /src

# Build from mounted source and keep build artifacts in a cache-mounted build dir.
RUN --mount=type=bind,source=.,target=/src \
    --mount=type=cache,target=/src/build \
    cmake -S . -B build -G Ninja \
    && cmake --build build --target BandageNG \
    && install -Dm755 /src/build/BandageNG /out/BandageNG

# ==========================================
FROM base AS runtime

RUN apt-get install -y --no-install-recommends \
    libqt6svg6 \
    libqt6widgets6

COPY --from=build /out/BandageNG /usr/local/bin/BandageNG

ENV QT_QPA_PLATFORM=offscreen \
    QT_X11_NO_MITSHM=1
