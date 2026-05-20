#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="${1:-./demo/client_data}"

mkdir -p "${TARGET_DIR}"

for i in $(seq 1 10); do
  FILE="${TARGET_DIR}/large_${i}.bin"
  if [[ ! -f "${FILE}" ]]; then
    echo "creating ${FILE}"
    dd if=/dev/urandom of="${FILE}" bs=1m count=100 status=progress
  fi
done
