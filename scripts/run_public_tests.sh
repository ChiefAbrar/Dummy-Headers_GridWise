#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gridwise_tests tests/fixtures_public_samples.json
