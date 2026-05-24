# Architecture-aware base selection. BuildKit sets TARGETARCH automatically
FROM ubuntu:22.04 AS base-amd64
FROM nvcr.io/nvidia/deepstream:7.1-triton-l4t AS base-arm64

ARG TARGETARCH
FROM base-${TARGETARCH}

ARG DEBIAN_FRONTEND=noninteractive

# Register LLVM 18 and Kitware (cmake) apt repositories.
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        ca-certificates \
        gpg \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
        | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc \
    && echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" \
        > /etc/apt/sources.list.d/llvm-18.list \
    && curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor - \
        | tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null \
    && echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main" \
        > /etc/apt/sources.list.d/kitware.list

# Dev toolchain: cmake, ninja, clang-18, ccache, doxygen, conan, pre-commit, protoc.
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ccache \
        make \
        ninja-build \
        git \
        python3-pip \
        clang-18 \
        clang-format-18 \
        clang-tidy-18 \
        clangd-18 \
        libclang-rt-18-dev \
        llvm-18 \
        doxygen \
        openssh-client \
        protobuf-compiler \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir pre-commit==4.6.0 gersemi==0.27.5 conan==2.28.1 \
    && rm -rf /root/.cache/pip \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100 \
    && update-alternatives --install /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-18 100 \
    && update-alternatives --install /usr/bin/clangd clangd /usr/bin/clangd-18 100

# /usr/lib/ccache first so compiler wrappers intercept calls before system binaries.
ENV PATH="/usr/lib/ccache:/usr/local/bin:${PATH}" \
    CCACHE_DIR=/ccache \
    CCACHE_BASEDIR=/workspace \
    CCACHE_SLOPPINESS=pch_defines,time_macros

WORKDIR /workspace