#!/bin/sh
# Sestaveni pluginu i aplikace (Linux/WSL, balicek mingw-w64).
# Vysledek: dist/SynergyTablet.exe (plugin i webove UI jsou uvnitr exe)
#   ./build.sh         plugin + aplikace
#   ./build.sh plugin  jen plugin/SynergyBridge.dll
#   ./build.sh app     jen aplikace s pluginem, ktery uz lezi v plugin/ (v CI uz podepsany)
set -e
cd "$(dirname "$0")"
what="${1:-all}"
if [ "$what" = all ] || [ "$what" = plugin ]; then
  ( cd plugin && i686-w64-mingw32-g++ -O2 -s -shared -static -o SynergyBridge.dll SynergyBridge.cpp -lws2_32 -Wl,--kill-at )
  echo "hotovo: plugin/SynergyBridge.dll"
fi
if [ "$what" = all ] || [ "$what" = app ]; then
  sh app/build.sh i686
  mkdir -p dist
  mv app/SynergyTablet.exe dist/
  cp docs/CTI_MNE.txt dist/
  echo "hotovo: dist/SynergyTablet.exe"
fi
