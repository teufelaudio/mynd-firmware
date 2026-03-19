#!/bin/bash
#
# Build release zip with convention-style layout.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MYND_ROOT="$(cd "${ROOT_DIR}/../../../.." && pwd)"
HOST_DIR="${MYND_ROOT}/external/teufel/libs/actionslink/host"
GENERATE_PROTO="${MYND_ROOT}/external/teufel/libs/actionslink/scripts/generate_proto.sh"

if [ ! -d "${HOST_DIR}" ]; then
    echo "ERROR: host directory not found: ${HOST_DIR}"
    exit 1
fi

if [ -f "${GENERATE_PROTO}" ]; then
    bash "${GENERATE_PROTO}"
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT

PACKAGE_ROOT="${TEMP_DIR}/MYNDberry"
mkdir -p "${PACKAGE_ROOT}"

cp -r "${ROOT_DIR}" "${PACKAGE_ROOT}/daemon_install"
cp -r "${HOST_DIR}" "${PACKAGE_ROOT}/host"

(cd "${TEMP_DIR}" && zip -r MYNDberry.zip MYNDberry -x "*/.DS_Store" -x ".DS_Store" >/dev/null)
mv "${TEMP_DIR}/MYNDberry.zip" "${ROOT_DIR}/MYNDberry.zip"

echo "Created: ${ROOT_DIR}/MYNDberry.zip"
