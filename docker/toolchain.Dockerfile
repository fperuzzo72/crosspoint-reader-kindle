# Cross toolchain for the jailbroken Kindle KT3 (8th gen, i.MX6SL).
#
# Target ABI was measured on the device, not assumed: the fbink that the
# WinterBreak2 jailbreak installs at /mnt/us/libkh/bin/fbink is
#   ELF 32-bit LSB, ARM EABI5, e_flags 0x05000200 (EF_ARM_ABI_FLOAT_SOFT)
#   interpreter /lib/ld-linux.so.3, for GNU/Linux 3.0.35
# so the toolchain must be SOFT-float (arm-*-linux-gnueabi). The usual
# gnueabihf default of every modern ARM cross-compiler produces binaries the
# loader rejects with a misleading "not found" (the file is there; its
# interpreter, /lib/ld-linux-armhf.so.3, is not).
#
# koxtoolchain's "kindlepw2" profile is the right one despite the name: it
# covers the i.MX6 generation from the Paperwhite 2 onward, which includes
# this KT3, and it emits arm-kindlepw2-linux-gnueabi.
#
# Upstream only tests koxtoolchain on Linux hosts ("when in doubt, use a
# Debian VM"), which is why this lives in a container instead of running on
# the Mac directly.
FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf automake bison build-essential ca-certificates curl file flex \
      gawk git gperf help2man libncurses-dev libtool libtool-bin patch \
      python3 rsync texinfo unzip wget xz-utils bzip2 \
    && rm -rf /var/lib/apt/lists/*

# crosstool-NG refuses to run as root.
RUN useradd -ms /bin/bash builder
USER builder
WORKDIR /home/builder

RUN git clone --depth 1 https://github.com/koreader/koxtoolchain.git
WORKDIR /home/builder/koxtoolchain

# ~1h. Lands in /home/builder/x-tools/arm-kindlepw2-linux-gnueabi.
RUN ./gen-tc.sh kindlepw2


# CMake for the target build (cmake/kindle/). Deliberately installed AFTER
# gen-tc.sh so the hour-long toolchain layer stays cached: appending here costs
# a minute, while adding cmake to the apt line above would rebuild everything.
USER root
RUN apt-get update && apt-get install -y --no-install-recommends cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
USER builder

ENV PATH="/home/builder/x-tools/arm-kindlepw2-linux-gnueabi/bin:${PATH}"
WORKDIR /src
