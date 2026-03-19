#!/bin/bash
#
# MYNDberry - Downloads and installs RPiLink daemon directly to RPi from teufelaudio's GitHub Releases
#

set -e

GITHUB_REPO="teufelaudio/mynd-firmware"
DEFAULT_ASSET_NAME="MYNDberry.zip"
INSTALL_ZIP_URL="https://github.com/${GITHUB_REPO}/releases/latest/download/${DEFAULT_ASSET_NAME}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "MYNDberry - Install from URL"
echo "==============================="
echo ""

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This script must be run as root (use sudo)${NC}"
    echo ""
    echo "Usage:"
    echo "  curl -sSL <url>/install_mynd_berry.sh | sudo bash"
    exit 1
fi

# Detect download tool
if command -v curl >/dev/null 2>&1; then
    DOWNLOAD_CMD="curl -sSL -o"
elif command -v wget >/dev/null 2>&1; then
    DOWNLOAD_CMD="wget -q -O"
else
    echo -e "${RED}ERROR: Neither curl nor wget found. Please install one of them.${NC}"
    exit 1
fi

# Check unzip
if ! command -v unzip >/dev/null 2>&1; then
    echo -e "${YELLOW}Installing unzip...${NC}"
    apt-get update -qq && apt-get install -y unzip
fi

INSTALL_DIR="/tmp/mynd_install_$$"
mkdir -p "$INSTALL_DIR"
trap 'rm -rf "$INSTALL_DIR"' EXIT

echo "Downloading from: ${INSTALL_ZIP_URL}"
cd "$INSTALL_DIR"

if [ "$DOWNLOAD_CMD" = "curl -sSL -o" ]; then
    curl -sSL -o install.zip "$INSTALL_ZIP_URL" || {
        echo -e "${RED}ERROR: Download failed${NC}"
        exit 1
    }
else
    wget -q -O install.zip "$INSTALL_ZIP_URL" || {
        echo -e "${RED}ERROR: Download failed${NC}"
        exit 1
    }
fi

echo "Extracting..."
unzip -q -o install.zip

# Find and run top-level install script in package
if [ -f "MYNDberry/daemon_install/install.sh" ]; then
    INSTALL_SCRIPT="MYNDberry/daemon_install/install.sh"
else
    echo -e "${RED}ERROR: install.sh not found at MYNDberry/daemon_install/install.sh in archive${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}Running installer...${NC}"
echo ""
/bin/bash "$INSTALL_SCRIPT" </dev/tty
