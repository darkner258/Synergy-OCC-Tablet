#!/bin/sh
set -e
cd "$(dirname "$0")/src"
ARCH=${1:-i686}
${ARCH}-w64-mingw32-windres app.rc -O coff -o app_res.o
${ARCH}-w64-mingw32-gcc -O2 -c ../third/qrcodegen.c -o qrcodegen.o
${ARCH}-w64-mingw32-g++ -O2 -s -static -municode -mwindows -o ../SynergyTablet.exe main.cpp qrcodegen.o app_res.o \
  -lws2_32 -liphlpapi -lcomctl32 -lshell32 -lole32 -luser32 -lgdi32 -ladvapi32
echo built
