#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2024-2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
# SPDX-License-Identifier: Apache-2.0


set -euxo pipefail

rm -rf build
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..
