#!/bin/sh
# Compile the C++ simulator to WebAssembly.
# SINGLE_FILE embeds the .wasm in the .js as base64, so the page stays one file.
set -e
cd "$(dirname "$0")/.."
em++ -std=c++20 -O2 -Iinclude \
    src/market.cpp src/agents/*.cpp web/bindings.cpp \
    --bind \
    -sSINGLE_FILE=1 -sMODULARIZE=1 -sEXPORT_NAME=createSim \
    -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,node \
    -o web/sim.js
echo "built web/sim.js ($(wc -c < web/sim.js) bytes)"
