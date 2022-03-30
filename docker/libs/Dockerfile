FROM debian:buster

ARG FALCOSECURITY_LIBS_DRIVER_VERSION
ARG FALCOSECURITY_LIBS_VERSION
ARG FALCOSECURITY_LIBS_CHECKSUM
ARG DRIVER_PACKAGE_NAME=falco
ARG DRIVER_NAME=falco
ARG SCAP_BPF_PROBE_ENV_VAR_NAME=FALCO_BPF_PROBE

LABEL "name"="Base image for Falco Libs"
LABEL "vendor"="Falco"
LABEL "version"="${FALCOSECURITY_LIBS_VERSION}"
LABEL "release"="${FALCOSECURITY_LIBS_VERSION}"
LABEL "summary"="Falco Libs implements userspace libraries and drivers for system event collection."
LABEL "description"="Falco Libs implements userspace libraries and drivers for system event collection."
LABEL "io.k8s.display-name"="Falco Libs"
LABEL "io.k8s.description"="Falco Libs implements userspace libraries and drivers for system event collection."
LABEL maintainer="cncf-falco-dev@lists.cncf.io"
LABEL usage="docker build --build-args FALCOSECURITY_LIBS_DRIVER_VERSION=${FALCOSECURITY_LIBS_DRIVER_VERSION} --build-args FALCOSECURITY_LIBS_VERSION=${FALCOSECURITY_LIBS_VERSION} --build-args=${FALCOSECURITY_LIBS_CHECKSUM} -f ./docker/libs/Dockerfile -t falcosecurity/libs ."

COPY ./common /build/common
COPY ./driver /build/driver
COPY ./userspace /build/userspace
COPY ./cmake /build/cmake
COPY ./scripts /build/scripts
COPY ./CMakeLists.txt /build/

RUN cd build && mkdir -p build && cd build && mkdir -p /usr/local/ && \
    apt-get update && apt install -y --no-install-recommends \
        git \
        cmake \
        build-essential \
        pkg-config \
        autoconf \
        libtool \
        libelf-dev && \
    cmake -DPROBE_VERSION=${FALCOSECURITY_LIBS_DRIVER_VERSION} \
        -DDRIVER_NAME=${DRIVER_NAME} \
        -DDRIVER_PACKAGE_NAME=${DRIVER_PACKAGE_NAME} \
        -DSCAP_BPF_PROBE_ENV_VAR_NAME=${SCAP_BPF_PROBE_ENV_VAR_NAME} \
        -DCAP_HOST_ROOT_ENV_VAR_NAME=HOST_ROOT \
        -DFALCOSECURITY_LIBS_VERSION=${FALCOSECURITY_LIBS_VERSION} \
        -DFALCOSECURITY_LIBS_CHECKSUM=${FALCOSECURITY_LIBS_CHECKSUM} \
        -DCMAKE_INSTALL_PREFIX=/usr/ \
        -DCMAKE_BUILD_TYPE=Release -DPROBE_NAME=${DRIVER_NAME} \
        -DBUILD_BPF=OFF  \
        -DBUILD_DRIVER=OFF ../. && \
    make install && rm -rf /build && rm -rf /var/lib/apt/lists/*
