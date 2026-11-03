#!/usr/bin/env bash
set -euo pipefail
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo 'The initial Linux build requires an x86_64 Linux environment.' >&2
    exit 1
fi
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$project_dir"
cmake --preset linux-release
cmake --build --preset linux-release --parallel "${EXTRAPANE_BUILD_JOBS:-2}"
echo 'Built source target only. No tests, app launch, driver, or system configuration changes were requested by this script.'
