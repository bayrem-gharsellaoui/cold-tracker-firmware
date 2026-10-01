# Copyright (c) 2026 Bayrem Gharsellaoui
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env bash

# Stop on a failed command (-e), an unset variable (-u), or a failed
# command within a pipeline (pipefail).
set -euo pipefail

# Allow the container user to create the Zephyr workspace in /workdir.
sudo chown user:user /workdir
cd /workdir

# Initialise west only if this workspace has not been initialised yet.
if [ ! -d .west ]; then
    west init -l application
fi

# Download the projects listed in west.yml, then register Zephyr with CMake.
west update
west zephyr-export
west blobs fetch hal_espressif
