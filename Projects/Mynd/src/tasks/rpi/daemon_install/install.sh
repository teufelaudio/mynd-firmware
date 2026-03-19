#!/bin/bash
#
# Top-level installer entrypoint.
# Run this on the Raspberry Pi as root (or with sudo).
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Run as root: sudo ./install.sh"
    exit 1
fi

bash "${SCRIPT_DIR}/components/local/install.sh"
