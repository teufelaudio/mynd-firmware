#!/bin/bash
#
# Remote install helper (run on laptop).
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MYND_ROOT="$(cd "${ROOT_DIR}/../../../.." && pwd)"
ACTIONSLINK_HOST_DIR="${MYND_ROOT}/external/teufel/libs/actionslink/host"
DAEMON_INSTALL_DIR="${ROOT_DIR}"

RPI_HOST=""
RPI_USER=""
SSH_KEY=""
RPI_PASSWORD=""

usage() {
    echo "Usage: $0 --host <host> [--user <user>] [--key <keyfile>] [--password]"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--host) RPI_HOST="$2"; shift 2 ;;
        -u|--user) RPI_USER="$2"; shift 2 ;;
        -k|--key) SSH_KEY="$2"; shift 2 ;;
        -p|--password) RPI_PASSWORD="PROMPT"; shift ;;
        --help) usage ;;
        *) usage ;;
    esac
done

[ -z "$RPI_HOST" ] && usage
[ -z "$RPI_USER" ] && RPI_USER="pi"

SSH_OPTS=()
SCP_OPTS=()
if [ -n "$SSH_KEY" ]; then
    SSH_OPTS+=(-i "$SSH_KEY")
    SCP_OPTS+=(-i "$SSH_KEY")
fi
SSH_OPTS+=(-o "StrictHostKeyChecking=no" -o "UserKnownHostsFile=/dev/null")
SCP_OPTS+=(-o "StrictHostKeyChecking=no" -o "UserKnownHostsFile=/dev/null")

SSH_TARGET="${RPI_USER}@${RPI_HOST}"
REMOTE_STAGING_DIR="/tmp/mynd_berry_install"

if [ ! -d "$ACTIONSLINK_HOST_DIR" ]; then
    echo "ERROR: host directory not found: ${ACTIONSLINK_HOST_DIR}"
    exit 1
fi

if [ "$RPI_PASSWORD" = "PROMPT" ] && command -v sshpass >/dev/null 2>&1; then
    read -rsp "Password for ${SSH_TARGET}: " PASSWORD
    echo ""
    sshpass -p "${PASSWORD}" ssh "${SSH_OPTS[@]}" "${SSH_TARGET}" "rm -rf ${REMOTE_STAGING_DIR}; mkdir -p ${REMOTE_STAGING_DIR}"
    sshpass -p "${PASSWORD}" scp -r "${SCP_OPTS[@]}" "${ACTIONSLINK_HOST_DIR}" "${SSH_TARGET}:${REMOTE_STAGING_DIR}/host"
    sshpass -p "${PASSWORD}" scp -r "${SCP_OPTS[@]}" "${DAEMON_INSTALL_DIR}" "${SSH_TARGET}:${REMOTE_STAGING_DIR}/daemon_install"
    sshpass -p "${PASSWORD}" ssh "${SSH_OPTS[@]}" "${SSH_TARGET}" "find ${REMOTE_STAGING_DIR} -type f -name '.*' -delete"
    sshpass -p "${PASSWORD}" ssh "${SSH_OPTS[@]}" -t "${SSH_TARGET}" "cd ${REMOTE_STAGING_DIR}/daemon_install && sudo bash ./install.sh"
else
    ssh "${SSH_OPTS[@]}" "${SSH_TARGET}" "rm -rf ${REMOTE_STAGING_DIR}; mkdir -p ${REMOTE_STAGING_DIR}"
    scp -r "${SCP_OPTS[@]}" "${ACTIONSLINK_HOST_DIR}" "${SSH_TARGET}:${REMOTE_STAGING_DIR}/host"
    scp -r "${SCP_OPTS[@]}" "${DAEMON_INSTALL_DIR}" "${SSH_TARGET}:${REMOTE_STAGING_DIR}/daemon_install"
    ssh "${SSH_OPTS[@]}" "${SSH_TARGET}" "find ${REMOTE_STAGING_DIR} -type f -name '.*' -delete"
    ssh "${SSH_OPTS[@]}" -t "${SSH_TARGET}" "cd ${REMOTE_STAGING_DIR}/daemon_install && sudo bash ./install.sh"
fi

echo "Remote install finished on ${SSH_TARGET}"
