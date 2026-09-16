FROM ubuntu:24.04@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90 AS build
ENV DEBIAN_FRONTEND=noninteractive SST_PREFIX=/opt/sst QEMU_PREFIX=/opt/qemu
ENV PATH=/opt/sst/bin:/opt/qemu/bin:${PATH}
ENV LD_LIBRARY_PATH=/opt/sst/lib
ARG BUILD_JOBS=2
ARG SST_CORE_REVISION=380b62e9a798a409100f88129f9634e2317ddaeb
ARG SST_ELEMENTS_REVISION=a4f2a207861b41bfa4c6e6f57f6a12ac81782d7b
ARG QEMU_VERSION=9.2.1
ARG QEMU_SHA256=72874fe9c395ced0c7fd7c22c43744072697f7ee1926a72237bd81784b2faf62
ARG QEMU_TARGET_LIST=m68k-softmmu,riscv64-softmmu
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake bison build-essential ca-certificates cmake curl doctest-dev \
    flex git libcap-ng-dev libglib2.0-dev libopenmpi-dev libpixman-1-dev \
    libslirp-dev libtool libtool-bin ninja-build openmpi-bin pkg-config python3 \
    python3-dev python3-pip zlib1g-dev gcc-m68k-linux-gnu \
    && rm -rf /var/lib/apt/lists/*
COPY . /src/quetz
RUN echo "${QEMU_SHA256}  /src/quetz/runtime/cache/qemu-${QEMU_VERSION}.tar.xz" | sha256sum -c - \
    && tar xJf "/src/quetz/runtime/cache/qemu-${QEMU_VERSION}.tar.xz" -C /tmp \
    && sh /src/quetz/qemu-overlay/apply-qemu-overlay.sh "/tmp/qemu-${QEMU_VERSION}" \
    && sh /src/quetz/qemu-overlay/apply-coldfire-overlay.sh "/tmp/qemu-${QEMU_VERSION}" \
    && cd "/tmp/qemu-${QEMU_VERSION}" \
    && ./configure --prefix="${QEMU_PREFIX}" --target-list="${QEMU_TARGET_LIST}" --enable-plugins \
    && make -j"${BUILD_JOBS}" && make install \
    && mkdir -p "${QEMU_PREFIX}/include" \
    && cp include/plugins/qemu-plugin.h "${QEMU_PREFIX}/include/" \
    && rm -rf "/tmp/qemu-${QEMU_VERSION}" /src/quetz/runtime/cache
RUN git init /src/sst-core \
    && git -C /src/sst-core fetch --depth 1 https://github.com/sstsimulator/sst-core.git "${SST_CORE_REVISION}" \
    && git -C /src/sst-core checkout --detach FETCH_HEAD \
    && cd /src/sst-core && ./autogen.sh \
    && mkdir -p /build/sst-core && cd /build/sst-core \
    && /src/sst-core/configure --prefix="${SST_PREFIX}" \
    && make -j"${BUILD_JOBS}" install
RUN git init /src/sst-elements \
    && git -C /src/sst-elements fetch --depth 1 https://github.com/nab880/sst-elements.git "${SST_ELEMENTS_REVISION}" \
    && git -C /src/sst-elements checkout --detach FETCH_HEAD \
    && find /src/sst-elements/src/sst/elements -mindepth 1 -maxdepth 1 -type d \
        ! -name memHierarchy ! -name mmu -exec touch '{}/.ignore' \; \
    && cd /src/sst-elements && ./autogen.sh \
    && mkdir -p /build/sst-elements && cd /build/sst-elements \
    && /src/sst-elements/configure --prefix="${SST_PREFIX}" --with-sst-core="${SST_PREFIX}" --without-pin \
    && make -j"${BUILD_JOBS}" install
RUN cmake -S /src/quetz -B /build/quetz -DCMAKE_INSTALL_PREFIX="${SST_PREFIX}" \
        -DSST_ROOT="${SST_PREFIX}" -DQEMU_PLUGIN_INCLUDE_DIR="${QEMU_PREFIX}/include" \
    && cmake --build /build/quetz -j"${BUILD_JOBS}" \
    && cmake --install /build/quetz \
    && "${SST_PREFIX}/bin/quetz-register"
RUN cd /src/quetz/tests/sysmode/firmware \
    && m68k-linux-gnu-gcc -mcpu=5208 -O2 -nostdlib -nostartfiles -ffreestanding \
        -T link_m68k.ld -Wl,--build-id=none coldfire_startup.S coldfire_hello.c -o coldfire_hello \
    && cp coldfire_hello "${SST_PREFIX}/share/quetz/tests/sysmode/firmware/"
COPY runtime/quetz-run /usr/local/bin/quetz-run
RUN chmod +x /usr/local/bin/quetz-run
CMD ["quetz-run", "--no-docker"]

FROM ubuntu:24.04@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90 AS runtime
ARG QUETZ_REVISION=unknown
LABEL org.quetz.revision="${QUETZ_REVISION}" org.quetz.ipc.magic="0x515A4D04"
ENV DEBIAN_FRONTEND=noninteractive SST_PREFIX=/opt/sst QEMU_PREFIX=/opt/qemu
ENV PATH=/opt/sst/bin:/opt/qemu/bin:${PATH}
ENV LD_LIBRARY_PATH=/opt/sst/lib
ENV SST_HOME=/opt/sst
RUN apt-get update && apt-get install -y --no-install-recommends \
    libcap-ng0 libglib2.0-0t64 libgomp1 libltdl7 libopenmpi3 libpixman-1-0 libpython3.12 \
    libslirp0 libstdc++6 openmpi-bin python3 zlib1g \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/qemu /opt/qemu
COPY --from=build /opt/sst /opt/sst
COPY runtime/quetz-run /usr/local/bin/quetz-run
RUN chmod +x /usr/local/bin/quetz-run
WORKDIR /work
CMD ["quetz-run", "--no-docker"]
