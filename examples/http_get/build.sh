#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2019–2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024–2025 Knode.ai
# SPDX-License-Identifier: Apache-2.0
#
# Maintainer: Andy Curtis <contactandyc@gmail.com>


set -euxo pipefail

rm -rf build
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..
