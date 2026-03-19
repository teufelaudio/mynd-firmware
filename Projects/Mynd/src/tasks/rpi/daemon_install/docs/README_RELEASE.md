# Mynd RPi Link - Release Packaging

This document describes how the daemon install bundle is produced for URL-based installation.

## Build Release Package

Run from `Projects/Mynd/src/tasks/rpi/daemon_install`:

```bash
bash ./components/package/build.sh
```

The script creates:

```text
MYNDberry_v1.zip
|- MYNDberry_v1/
|  |- daemon_install/
|  |  |- install.sh
|  |  |- components/local/install.sh
|  |  |- components/local/uninstall.sh
|  |  |- ... daemon/config/service files ...
|  |- host/
|     |- actionslink_*.py
|     |- generated/*.py
```

The `host/` directory is included so the local installer can install ActionsLink Python modules without requiring a separate manual copy step.

## Install-From-URL Bootstrap

Use the bootstrap helper from the daemon install path and pass the release zip URL:

```bash
curl -sSL "https://raw.githubusercontent.com/teufelaudio/mcufirmware/main/Projects/Mynd/src/tasks/rpi/daemon_install/install_mynd_berry.sh" | \
sudo bash -s -- --url "https://github.com/teufelaudio/mcufirmware/releases/latest/download/MYNDberry_v1.zip"
```

Equivalent with `wget`:

```bash
wget -qO- "https://raw.githubusercontent.com/teufelaudio/mcufirmware/main/Projects/Mynd/src/tasks/rpi/daemon_install/install_mynd_berry.sh" | \
sudo bash -s -- --url "https://github.com/teufelaudio/mcufirmware/releases/latest/download/MYNDberry_v1.zip"
```
