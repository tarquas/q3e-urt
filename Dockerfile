FROM ubuntu:23.10

WORKDIR /usr/src/urt

RUN apt-get update && apt-get install -y \
    curl \
    make \
    gcc \
    libcurl4-openssl-dev \
    mesa-common-dev \
    libxxf86dga-dev \
    libxrandr-dev \
    libxxf86vm-dev \
    libasound-dev \
    libsdl2-dev \
    aria2 \
    unzip \
    build-essential \
    gdb  \
    lcov  \
    pkg-config  \
    libbz2-dev  \
    libffi-dev  \
    libgdbm-dev  \
    libgdbm-compat-dev  \
    liblzma-dev  \
    libncurses5-dev  \
    libreadline6-dev  \
    libsqlite3-dev  \
    libssl-dev  \
    lzma  \
    lzma-dev  \
    tk-dev  \
    uuid-dev  \
    zlib1g-dev \
    lib32z1 \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*
