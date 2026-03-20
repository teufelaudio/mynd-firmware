# Mynd RPi Link - Release Packaging

This document describes how the daemon install bundle is produced for URL-based installation.

## Build Release Package

Run from `Projects/Mynd/src/tasks/rpi/daemon_install`:

```bash
bash ./components/package/build.sh
```

The script creates:

```text
MYNDberry.zip
|- daemon_install/
|  |- install.sh
|  |- components/local/install.sh
|  |- components/local/uninstall.sh
|  |- ... daemon/config/service files ...
|- host/
   |- actionslink_*.py
   |- generated/*.py
```

The `host/` directory is included so the local installer can install ActionsLink Python modules without requiring a separate manual copy step.