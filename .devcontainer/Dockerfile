# Architecture-aware base selection. BuildKit sets TARGETARCH automatically.
FROM ubuntu:22.04 AS base-amd64
FROM nvcr.io/nvidia/deepstream-l4t:7.1-triton-multiarch AS base-arm64

ARG TARGETARCH
FROM base-${TARGETARCH}

ARG DEBIAN_FRONTEND=noninteractive

# Create user early — this layer is stable and rarely invalidated.
RUN apt-get update && apt-get install -y --no-install-recommends sudo \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash gandalf \
    && echo "gandalf ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/gandalf \
    && chmod 0440 /etc/sudoers.d/gandalf

# Register LLVM 18 and Kitware (cmake) apt repositories.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gpg \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
        | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null \
    && echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" \
        > /etc/apt/sources.list.d/llvm-18.list \
    && curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor - \
        | tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null \
    && echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main" \
        > /etc/apt/sources.list.d/kitware.list

# Dev toolchain: cmake, clang-18, ccache, doxygen, conan, pre-commit.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ccache \
        clang-18 \
        clang-format-18 \
        clang-tidy-18 \
        clangd-18 \
        cmake \
        doxygen \
        git \
        graphviz \
        libclang-rt-18-dev \
        libdw-dev \
        llvm-18 \
        make \
        ninja-build \
        openssh-client \
        python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir \
        breathe==4.36.0 \
        conan==2.28.1 \
        exhale==0.3.7 \
        furo==2025.12.19 \
        gersemi==0.27.5 \
        myst-parser==4.0.1 \
        pre-commit==4.6.0 \
        sphinx==8.1.3 \
        sphinxcontrib-mermaid==1.0.0 \
    && rm -rf /root/.cache/pip \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100 \
    && update-alternatives --install /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-18 100 \
    && update-alternatives --install /usr/bin/clangd clangd /usr/bin/clangd-18 100 \
    && curl -fsSL "https://github.com/bufbuild/buf/releases/download/v1.69.0/buf-Linux-$(uname -m)" \
           -o /usr/local/bin/buf \
    && chmod +x /usr/local/bin/buf

# /usr/lib/ccache first so compiler wrappers intercept calls before system binaries.
ENV PATH="/usr/lib/ccache:/usr/local/bin:${PATH}" \
    CCACHE_DIR=/ccache \
    CCACHE_BASEDIR=/workspace \
    CCACHE_SLOPPINESS=pch_defines,time_macros \
    CONAN_HOME=/opt/conan

# Bake Conan dependency packages into the image so CI jobs never cold-build them.
# /opt/conan is outside $HOME so GitHub Actions' home-directory mount won't shadow it.
# chown happens in the same layer as the writes to avoid Docker layer bloat.
ARG TARGETARCH
COPY conanfile.py conan.lock /workspace/
COPY conan/ /workspace/conan/
RUN mkdir -p /opt/conan && chmod 777 /opt/conan \
    && conan profile detect --force \
    && conan remote add orion-local /workspace/conan -t local-recipes-index --index 0 -f \
    && if [ "$TARGETARCH" = "amd64" ]; then \
         conan install /workspace --build=missing \
             -pr=/workspace/conan/profiles/x86_64/debug \
             --lockfile=/workspace/conan.lock; \
         conan install /workspace --build=missing \
             -pr=/workspace/conan/profiles/x86_64/release \
             --lockfile=/workspace/conan.lock; \
       elif [ "$TARGETARCH" = "arm64" ]; then \
         conan install /workspace --build=missing \
             -pr=/workspace/conan/profiles/arm64/debug \
             --lockfile=/workspace/conan.lock; \
       fi \
    && chown -R gandalf:gandalf /opt/conan \
    && rm -rf /workspace/*

WORKDIR /workspace
