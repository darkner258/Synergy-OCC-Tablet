#!/bin/sh
# Sestaveni pluginu i aplikace (Linux/WSL, balicek mingw-w64).
# Vysledek: dist/SynergyTablet.exe (plugin i webove UI jsou uvnitr exe)
set -e
cd "$(dirname "$0")"
( cd plugin && i686-w64-mingw32-g++ -O2 -s -shared -static -o SynergyBridge.dll SynergyBridge.cpp -lws2_32 -Wl,--kill-at )
sh app/build.sh i686
mkdir -p dist
mv app/SynergyTablet.exe dist/
cp docs/CTI_MNE.txt dist/
echo "hotovo: dist/SynergyTablet.exe"
