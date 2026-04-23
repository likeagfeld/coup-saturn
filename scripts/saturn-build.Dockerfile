# Baked Saturn build image: Ubuntu + Jo Engine + SH-2 toolchain.
#
# The joengine SHA is pinned so builds are reproducible. Bump by editing
# JOENGINE_SHA and rebuilding the image. Use --platform linux/amd64 on
# Apple Silicon (Jo Engine ships x86_64 Linux binaries).
#
# Build:   docker build --platform linux/amd64 \
#            -f scripts/saturn-build.Dockerfile \
#            -t coup-saturn-build:latest scripts/
# Rebuild: docker build --no-cache ... (or bump JOENGINE_SHA).
FROM --platform=linux/amd64 ubuntu:22.04

ARG JOENGINE_SHA=4dc19a30e971673a855eac6cf1d828c81f9cfbda

RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends \
      make mkisofs git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/johannes-fetz/joengine /joengine \
 && git -C /joengine checkout "$JOENGINE_SHA" \
 && rm -rf /joengine/.git \
 && chmod +x /joengine/Compiler/LINUX/bin/*

ENV JOENGINE_ROOT=/joengine
