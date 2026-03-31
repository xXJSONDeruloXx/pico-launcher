#!/usr/bin/env bash
set -euo pipefail

IMAGE="skylyrac/blocksds:slim-latest"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

exec docker run --rm \
  -v "$REPO_ROOT:/work" \
  -w /work \
  "$IMAGE" \
  make "$@"
