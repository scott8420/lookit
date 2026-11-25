#!/usr/bin/env bash
# Lookit build. Deps (Ubuntu/Debian): libgtkmm-4.0-dev libspdlog-dev cmake g++.
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -B build
cmake --build build -- -j"$(nproc)"
echo ""
echo "Built: ./build/lookit, ./build/lookit-peek  and  ./build/lookit_selftest"
echo "Smoke: ./build/lookit_selftest"
