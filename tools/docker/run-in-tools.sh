#!/bin/bash
# Run a command inside the tools image (ESP-IDF 6.0.2 + PowerShell 7).
#
#   run-in-tools.sh --build                      build the image
#   run-in-tools.sh pwsh tests/run_p4_host_tests.ps1
#   run-in-tools.sh python3 tools/check_board_build.py ...
#
# The repo is mounted at /work and the command runs as the host user, so no
# root-owned files appear. No device is exposed: serial capture stays on the
# host (tools/hil/hil.sh). IDF_PATH comes from the image (/opt/esp/idf).
set -euo pipefail

IMAGE=${PAJONIIIR_TOOLS_IMAGE:-pajoniiir-tools:idf6.0.2}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

if [ "${1:-}" = "--build" ]; then
  exec docker build -t "$IMAGE" -f "$ROOT/tools/docker/Dockerfile.tools" "$ROOT/tools/docker"
fi
[ $# -gt 0 ] || { sed -n 2,9p "$0"; exit 2; }

docker image inspect "$IMAGE" >/dev/null 2>&1 || {
  echo "image $IMAGE missing: run $0 --build" >&2; exit 1; }

# Bash-wrapped so the IDF python env is on PATH, as in the CI container.
exec docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
  -v "$ROOT":/work -w /work \
  "$IMAGE" bash -lc 'source /opt/esp/idf/export.sh >/dev/null 2>&1; exec "$@"' _ "$@"
