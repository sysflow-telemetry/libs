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

COPY ./driver /build/driver
COPY ./userspace /build/userspace
COPY ./cmake /build/cmake
COPY ./scripts /build/scripts
COPY ./CMakeLists.txt /build/

ENV PATH=$PATH:/usr/local/go/bin/
ENV GOPATH=/go/

ENV FALCOSECURITY_LIBS_LIB_PREFIX=/usr/lib/x86_64-linux-gnu/falcosecurity
ENV FALCOSECURITY_LIBS_INCLUDE_PREFIX=/usr/include/falcosecurity
ENV FALCOSECURITY_LIBS_CFLAGS='-I$(FALCOSECURITY_LIBS_INCLUDE_PREFIX)/ \
                               -I$(FALCOSECURITY_LIBS_INCLUDE_PREFIX)/userspace/libsinsp \
                               -I$(FALCOSECURITY_LIBS_INCLUDE_PREFIX)/userspace/libscap'
ENV FALCOSECURITY_LIBS_LDFLAGS='-L$(FALCOSECURITY_LIBS_LIB_PREFIX)/ \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libsinsp.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libscap.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libjq.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libonig.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libb64.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libcurl.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libtbb.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc++.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc++_alts.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc++_reflection.a  $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc++_error_details.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc++_unsecure.a  $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc_plugin_support.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpcpp_channelz.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgrpc.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libgpr.a -lcares -lprotobuf  \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libsinsp.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libscap.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libjq.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libonig.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_synchronization.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_graphcycles_internal.a  $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_status.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_statusor.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_strings.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_strings_internal.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_cord.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_cordz_functions.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_cord_internal.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_cordz_info.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_cordz_handle.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_hash.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_city.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_low_level_hash.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_raw_hash_set.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_hashtablez_sampler.a  $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_pool_urbg.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_randen.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_randen_hwaes.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_randen_hwaes_impl.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_platform.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_randen_slow.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_internal_seed_material.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_random_seed_gen_exception.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_exponential_biased.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_bad_variant_access.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_str_format_internal.a  \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_raw_logging_internal.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_malloc_internal.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_base.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_spinlock_wait.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_throw_delegate.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_log_severity.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_bad_optional_access.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_time.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_time_zone.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_int128.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_stacktrace.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_symbolize.a \
                                $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_debugging_internal.a $(FALCOSECURITY_LIBS_LIB_PREFIX)/libabsl_demangle_internal.a \
		                        -lelf -lstdc++ -lz -lrt -lanl -lssl -lcrypto -lpthread -lm -ldl -lupb -laddress_sorting -lre2'

RUN cd build && mkdir -p build && cd build && mkdir -p /usr/local/ && \
    apt update && apt install -y --no-install-recommends \
        git \
        cmake \
        build-essential \
        pkg-config \
        autoconf \
        libtool \
        libelf-dev \
        wget && \
    apt install -y --reinstall ca-certificates && \
    git config --global http.sslCAinfo /etc/ssl/certs/ca-certificates.crt && \
    cmake -DDRIVER_VERSION=${FALCOSECURITY_LIBS_DRIVER_VERSION} \
        -DDRIVER_NAME=${DRIVER_NAME} \
        -DDRIVER_PACKAGE_NAME=${DRIVER_PACKAGE_NAME} \
        -DSCAP_BPF_PROBE_ENV_VAR_NAME=${SCAP_BPF_PROBE_ENV_VAR_NAME} \
        -DCAP_HOST_ROOT_ENV_VAR_NAME=HOST_ROOT \
        -DFALCOSECURITY_LIBS_VERSION=${FALCOSECURITY_LIBS_VERSION} \
        -DFALCOSECURITY_LIBS_CHECKSUM=${FALCOSECURITY_LIBS_CHECKSUM} \
        -DCMAKE_INSTALL_PREFIX=/usr/ \
        -DCMAKE_BUILD_TYPE=Release \
        -DUSE_BUNDLED_GTEST=ON \
        -DUSE_BUNDLED_DEPS=ON \
        -DCREATE_TEST_TARGETS=OFF \
        -DBUILD_BPF=OFF  \
        -DBUILD_DRIVER=OFF ../. && \
    make install && \
    wget https://go.dev/dl/go1.17.7.linux-amd64.tar.gz && \
    tar -C /usr/local -xzf go1.17.7.linux-amd64.tar.gz && \
    rm -rf /build && rm -rf /var/lib/apt/lists/*
