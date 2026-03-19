#!/bin/bash
#
# Local installation script for Mynd RPi Link daemon.
# Intended to be called by ../../install.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PAYLOAD_DIR="${ROOT_DIR}/payload"
CONFIG_FILE="mynd_rpi_link.conf"
SERVICE_FILE="mynd-rpi-link.service"
SUDOERS_FILE="mynd-rpi-link.sudoers"
UNINSTALL_SCRIPT="uninstall.sh"

INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="/etc"
SYSTEMD_DIR="/etc/systemd/system"
SUDOERS_DIR="/etc/sudoers.d"

ACTIONSLINK_HOST_DIR="${INSTALL_DIR}/actionslink"
PYTHON_MODULE_DIR="${PAYLOAD_DIR}/bin"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "Mynd RPi Link Daemon Installation"
echo "=================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Sync system time (fixes SSL cert errors when RTC is wrong)
echo "Syncing system time..."
if command -v ntpdate &>/dev/null; then
    ntpdate -s pool.ntp.org 2>/dev/null || true
elif command -v timedatectl &>/dev/null; then
    timedatectl set-ntp true 2>/dev/null || true
    systemctl start systemd-timesyncd 2>/dev/null || true
    sleep 3
else
    HTTP_DATE=$(curl -sI --connect-timeout 3 https://google.com 2>/dev/null | grep -i '^[Dd]ate:' | cut -d' ' -f2-)
    if [ -n "$HTTP_DATE" ]; then
        date -s "$HTTP_DATE" 2>/dev/null || true
    fi
fi

# Detect the username (the user who invoked sudo, or fallback to current user)
if [ -n "$SUDO_USER" ]; then
    DETECTED_USER="$SUDO_USER"
elif [ -n "$USER" ]; then
    DETECTED_USER="$USER"
else
    DETECTED_USER=$(getent passwd | awk -F: '$3 >= 1000 && $1 != "nobody" {print $1; exit}')
fi

if [ -z "$DETECTED_USER" ]; then
    DETECTED_USER="pi"
    echo -e "${YELLOW}WARNING: Could not detect username, defaulting to 'pi'${NC}"
else
    echo "Detected username: ${DETECTED_USER}"
fi

if HOSTNAME_FQDN=$(hostname -f 2>/dev/null); then
    DETECTED_HOST="$HOSTNAME_FQDN"
elif HOSTNAME_SHORT=$(hostname 2>/dev/null); then
    DETECTED_HOST="$HOSTNAME_SHORT"
else
    DETECTED_HOST="raspberrypi"
fi

# Prefer the primary IP address for display, falling back to hostname
RPI_PRIMARY_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [ -n "${RPI_PRIMARY_IP}" ]; then
    SSH_TARGET_DISPLAY="${DETECTED_USER}@${RPI_PRIMARY_IP}"
else
    SSH_TARGET_DISPLAY="${DETECTED_USER}@${DETECTED_HOST}.local"
fi

if ! command -v python3 &> /dev/null; then
    echo -e "${RED}ERROR: Python 3 is not installed${NC}"
    exit 1
fi

echo "Checking Python dependencies..."
NEEDS_UPDATE=false
PACKAGES_TO_INSTALL=""

if ! python3 -c "import aiohttp" 2>/dev/null; then
    echo -e "${YELLOW}WARNING: aiohttp not found. Will install python3-aiohttp${NC}"
    PACKAGES_TO_INSTALL="${PACKAGES_TO_INSTALL} python3-aiohttp"
    NEEDS_UPDATE=true
fi

PROTOBUF_NEEDS_PIP=false
if ! python3 -c "import google.protobuf" 2>/dev/null; then
    echo -e "${YELLOW}WARNING: protobuf not found. Will install via pip${NC}"
    PROTOBUF_NEEDS_PIP=true
elif ! python3 -c "from google.protobuf import runtime_version" 2>/dev/null; then
    echo -e "${YELLOW}WARNING: protobuf version is too old (missing runtime_version). Will upgrade via pip${NC}"
    PROTOBUF_NEEDS_PIP=true
fi

PIP_PACKAGES=""
if ! python3 -c "import aioserial" 2>/dev/null; then
    echo -e "${YELLOW}WARNING: aioserial not found. Will install via pip${NC}"
    PIP_PACKAGES="${PIP_PACKAGES} aioserial"
fi

if [ "$PROTOBUF_NEEDS_PIP" = true ]; then
    PIP_PACKAGES="${PIP_PACKAGES} protobuf"
fi

if [ -n "$PIP_PACKAGES" ]; then
    if ! command -v pip3 &> /dev/null; then
        echo "Installing python3-pip..."
        if [ "$NEEDS_UPDATE" = false ]; then
            apt update -qq
            NEEDS_UPDATE=true
        fi
        apt install -y python3-pip || {
            echo -e "${RED}ERROR: Failed to install python3-pip${NC}"
            exit 1
        }
    fi
    echo "Installing Python packages via pip: ${PIP_PACKAGES}..."
    pip3 install --break-system-packages --upgrade --root-user-action=ignore \
        --trusted-host pypi.org --trusted-host pypi.python.org --trusted-host files.pythonhosted.org \
        --trusted-host www.piwheels.org \
        $PIP_PACKAGES || {
        echo -e "${RED}ERROR: Failed to install packages via pip: ${PIP_PACKAGES}${NC}"
        exit 1
    }
    echo -e "${GREEN}✓${NC} Installed packages via pip"
fi

if [ -n "$PACKAGES_TO_INSTALL" ]; then
    if command -v apt &> /dev/null; then
        if [ "$NEEDS_UPDATE" = true ]; then
            echo "Updating package list..."
            apt update -qq
        fi
        echo "Installing Python packages..."
        if ! apt install -y $PACKAGES_TO_INSTALL; then
            echo -e "${RED}ERROR: Failed to install Python packages: $PACKAGES_TO_INSTALL${NC}"
            exit 1
        fi
        echo -e "${GREEN}✓${NC} Installed Python dependencies"
    else
        echo -e "${RED}ERROR: apt not found. Please install packages manually: $PACKAGES_TO_INSTALL${NC}"
        exit 1
    fi
fi

echo "Verifying Python dependencies..."
MISSING_DEPS=false

if ! python3 -c "import aiohttp" 2>/dev/null; then
    echo -e "${RED}ERROR: aiohttp is not installed${NC}"
    MISSING_DEPS=true
fi
if ! python3 -c "import aioserial" 2>/dev/null; then
    echo -e "${RED}ERROR: aioserial is not installed${NC}"
    MISSING_DEPS=true
fi
if ! python3 -c "import google.protobuf" 2>/dev/null; then
    echo -e "${RED}ERROR: protobuf is not installed${NC}"
    MISSING_DEPS=true
elif ! python3 -c "from google.protobuf import runtime_version" 2>/dev/null; then
    echo -e "${RED}ERROR: protobuf version is too old (missing runtime_version)${NC}"
    MISSING_DEPS=true
fi

if [ "$MISSING_DEPS" = true ]; then
    echo -e "${RED}ERROR: Required Python dependencies are missing${NC}"
    exit 1
fi

echo -e "${GREEN}✓${NC} All Python dependencies are installed"

echo "Installing ActionsLink host library..."
mkdir -p "${ACTIONSLINK_HOST_DIR}"
mkdir -p "${ACTIONSLINK_HOST_DIR}/generated"

ACTIONSLINK_SOURCE_DIR="${ROOT_DIR}/host"
if [ ! -d "$ACTIONSLINK_SOURCE_DIR" ] || [ ! -f "${ACTIONSLINK_SOURCE_DIR}/actionslink_client.py" ]; then
    ACTIONSLINK_SOURCE_DIR="${ROOT_DIR}/../host"
fi
if [ ! -d "$ACTIONSLINK_SOURCE_DIR" ] || [ ! -f "${ACTIONSLINK_SOURCE_DIR}/actionslink_client.py" ]; then
    echo -e "${RED}ERROR: Could not find ActionsLink host library directory${NC}"
    echo "Expected location: ${ACTIONSLINK_SOURCE_DIR}"
    exit 1
fi

cp "${ACTIONSLINK_SOURCE_DIR}"/actionslink_*.py "${ACTIONSLINK_HOST_DIR}/" 2>/dev/null || {
    echo -e "${RED}ERROR: Failed to copy ActionsLink library files from ${ACTIONSLINK_SOURCE_DIR}${NC}"
    exit 1
}

if [ -d "${ACTIONSLINK_SOURCE_DIR}/generated" ]; then
    cp "${ACTIONSLINK_SOURCE_DIR}"/generated/*.py "${ACTIONSLINK_HOST_DIR}/generated/" 2>/dev/null || {
        echo -e "${YELLOW}WARNING: Could not copy generated protobuf files${NC}"
    }
else
    echo -e "${YELLOW}WARNING: Generated protobuf directory not found${NC}"
fi

chown -R "${DETECTED_USER}:${DETECTED_USER}" "${ACTIONSLINK_HOST_DIR}"
echo -e "${GREEN}✓${NC} Installed ActionsLink host library to ${ACTIONSLINK_HOST_DIR}"

echo "Installing daemon Python modules..."
if ls "${PYTHON_MODULE_DIR}"/*.py >/dev/null 2>&1; then
    for module_path in "${PYTHON_MODULE_DIR}"/*.py; do
        module_name="$(basename "${module_path}")"
        cp "${module_path}" "${INSTALL_DIR}/${module_name}"
        chmod +x "${INSTALL_DIR}/${module_name}"
    done
    echo -e "${GREEN}✓${NC} Installed Python modules to ${INSTALL_DIR}"
else
    echo -e "${RED}ERROR: No Python modules found in ${PYTHON_MODULE_DIR}${NC}"
    exit 1
fi

echo "Installing configuration file..."
if [ -f "${CONFIG_DIR}/${CONFIG_FILE}" ]; then
    echo -e "${YELLOW}WARNING: Configuration file already exists at ${CONFIG_DIR}/${CONFIG_FILE}${NC}"
    read -p "Overwrite? (y/N): " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cp "${PAYLOAD_DIR}/config/${CONFIG_FILE}" "${CONFIG_DIR}/${CONFIG_FILE}"
        echo -e "${GREEN}✓${NC} Updated ${CONFIG_DIR}/${CONFIG_FILE}"
    else
        echo -e "${YELLOW}  Skipped (keeping existing configuration)${NC}"
    fi
else
    cp "${PAYLOAD_DIR}/config/${CONFIG_FILE}" "${CONFIG_DIR}/${CONFIG_FILE}"
    echo -e "${GREEN}✓${NC} Installed ${CONFIG_DIR}/${CONFIG_FILE}"
fi

echo "Installing configure-moode service..."
CONFIGURE_SCRIPT="configure_moode.py"
CONFIGURE_SERVICE="configure-moode.service"

if [ -f "${PAYLOAD_DIR}/bin/${CONFIGURE_SCRIPT}" ]; then
    cp "${PAYLOAD_DIR}/bin/${CONFIGURE_SCRIPT}" "${INSTALL_DIR}/${CONFIGURE_SCRIPT}"
    chmod +x "${INSTALL_DIR}/${CONFIGURE_SCRIPT}"
    echo -e "${GREEN}✓${NC} Installed ${INSTALL_DIR}/${CONFIGURE_SCRIPT}"

    if [ -f "${PAYLOAD_DIR}/systemd/${CONFIGURE_SERVICE}" ]; then
        cp "${PAYLOAD_DIR}/systemd/${CONFIGURE_SERVICE}" "${SYSTEMD_DIR}/${CONFIGURE_SERVICE}"
        systemctl daemon-reload
        systemctl enable "${CONFIGURE_SERVICE}"
        echo -e "${GREEN}✓${NC} Installed and enabled ${CONFIGURE_SERVICE}"
    else
        echo -e "${YELLOW}WARNING: ${CONFIGURE_SERVICE} not found${NC}"
    fi
else
    echo -e "${YELLOW}WARNING: ${CONFIGURE_SCRIPT} not found${NC}"
fi

if [ -f "${SCRIPT_DIR}/${UNINSTALL_SCRIPT}" ]; then
    cp "${SCRIPT_DIR}/${UNINSTALL_SCRIPT}" "${INSTALL_DIR}/uninstall_rpi_link.sh"
    chmod +x "${INSTALL_DIR}/uninstall_rpi_link.sh"
    echo -e "${GREEN}✓${NC} Uninstall script placed at ${INSTALL_DIR}/uninstall_rpi_link.sh"
fi

echo "Installing systemd service..."
sed "s/^User=pi$/User=${DETECTED_USER}/" "${PAYLOAD_DIR}/systemd/${SERVICE_FILE}" | \
    sed "s/^Group=pi$/Group=${DETECTED_USER}/" > "${SYSTEMD_DIR}/${SERVICE_FILE}"
systemctl daemon-reload
echo -e "${GREEN}✓${NC} Installed systemd service (configured for user: ${DETECTED_USER})"

echo ""
echo "Sudoers Configuration"
echo "-------------------"
read -p "Configure sudoers? (Y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    SERVICE_USER="$DETECTED_USER"
    sed "s/^pi ALL=/${SERVICE_USER} ALL=/" "${PAYLOAD_DIR}/sudoers/${SUDOERS_FILE}" | \
        sed "s/^# pi ALL=/# ${SERVICE_USER} ALL=/" > "${SUDOERS_DIR}/mynd-rpi-link"
    chmod 0440 "${SUDOERS_DIR}/mynd-rpi-link"

    if visudo -c -f "${SUDOERS_DIR}/mynd-rpi-link" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Configured sudoers for user ${SERVICE_USER}"
    else
        echo -e "${RED}ERROR: Sudoers syntax check failed${NC}"
        rm -f "${SUDOERS_DIR}/mynd-rpi-link"
        exit 1
    fi
fi

echo ""
read -p "Enable and start the service now? (Y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    systemctl enable mynd-rpi-link.service
    systemctl start mynd-rpi-link.service
    echo -e "${GREEN}✓${NC} Service enabled and started"
    sleep 1
    systemctl status mynd-rpi-link.service --no-pager || true
fi

echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo "Useful local commands to check the status of the service and view logs directly on the Raspberry Pi:"
echo "  Check status:    sudo systemctl status mynd-rpi-link"
echo "  View logs:       sudo journalctl -u mynd-rpi-link -f"
echo "  Restart service: sudo systemctl restart mynd-rpi-link"
echo "  Stop service:    sudo systemctl stop mynd-rpi-link"
echo "  Start service:   sudo systemctl start mynd-rpi-link"
echo "  Disable service: sudo systemctl disable mynd-rpi-link"
echo "  Enable service:  sudo systemctl enable mynd-rpi-link"
echo ""
echo "Run the command remotely from a workstation by running 'ssh ${SSH_TARGET_DISPLAY} '<command>'"
echo ""