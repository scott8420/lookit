#!/usr/bin/env bash
# Lookit build. Deps (Ubuntu/Debian): libgtkmm-4.0-dev libspdlog-dev cmake g++.
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -B build
cmake --build build -- -j"$(nproc)"
echo ""
echo "Built: ./build/lookit, ./build/lookit-peek, ./build/lookit_selftest  and  ./build/lookit_savecheck"
echo "Smoke: ./build/lookit_selftest   and   ./build/lookit_savecheck"
