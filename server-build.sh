#!/bin/sh

mkdir -p test
make clean release BUILD_CLIENT=0 BUILD_SERVER=1 SERVER_CFLAGS="-DDUMMY_DEFINE"
