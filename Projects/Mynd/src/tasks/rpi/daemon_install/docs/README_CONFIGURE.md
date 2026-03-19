# Configure-Moode Boot Service

This document matches the current `configure_moode.py` implementation in this branch.

## Overview

`configure-moode.service` runs as a oneshot service at boot and executes:

`/usr/local/bin/configure_moode.py`

The script ensures required RPi and Moode settings for the MCU <-> RPi link:

1. SSH enable marker file exists (`/boot/firmware/ssh` or `/boot/ssh`)
2. UART enabled in boot config (`enable_uart=1`)
3. Linux serial console removed from cmdline (`console=serial0,...` etc)
4. I2C enabled (`dtparam=i2c_arm=on`)
5. I2S overlay in Moode managed `# Audio overlays` section (`dtoverlay=hifiberry-dac`)
6. Moode database audio settings (`cfg_system`, `cfg_mpd`, `cfg_outputdev`)
7. Boot volume (`volknob`) set from config key `moode_boot_volume_percent` via configure-moode service

## What It Does Not Configure

Current script does **not** configure Bluetooth renderer database keys (`btsvc`, `p3bt`, `btname`) and does not modify `dtoverlay=disable-bt`.
It also does not configure daemon-only runtime keys such as WiFi/hotspot logic, coexistence policy, or logging behavior.

## Configuration Source

Reads `/etc/mynd_rpi_link.conf`, section `[configure_moode]`:

- `enabled` (default true)
- `boot_config_path` (default `/boot/firmware/config.txt`)
- `moode_boot_volume_percent` (default 10, clamped to 0..100)

## Audio DB Values Enforced

### `cfg_system`

- `i2sdevice = Generic-I2S (hifiberry-dac)`
- `i2soverlay = None`
- `adevname = Generic-I2S (hifiberry-dac)`
- `cardnum = 0`
- `alsavolume = none`
- `amixname = none`
- `alsa_output_mode = plughw`
- `mpdmixer = software`

### `cfg_mpd`

- `mixer_type = software`
- `device = 0`

### `cfg_outputdev`

- `device_name = Generic-I2S (hifiberry-dac)`
- `mpd_volume_type = software`
- `alsa_output_mode = plughw`
- `alsa_max_volume = 100`

## Boot Volume Behavior

`volknob` is set every boot:

- `moode_boot_volume_percent` is applied regardless of MCU-selected volume mode

This is independent from MCU amp gain settings set by CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME via `config.h`.

If the key is missing or invalid, `configure_moode.py` falls back to `10`.

## Reboot Rules

Reboot is triggered only when boot files changed:

- SSH marker file created
- UART changed
- serial console removed
- I2C changed
- I2S overlay section changed

If only DB values changed, no reboot is forced.

## Service Ordering

From `configure-moode.service`:

- `DefaultDependencies=no`
- `Wants=NetworkManager.service`
- `After=local-fs.target NetworkManager.service`
- `Before=nginx.service`
- `Type=oneshot`
- `WantedBy=multi-user.target`

## Install / Remove

Installed by:

```bash
sudo bash ./install.sh
```

Removed by:

```bash
sudo /usr/local/bin/uninstall_rpi_link.sh
```

## Manual Run and Logs

Run once:

```bash
sudo /usr/local/bin/configure_moode.py
```

Inspect logs:

```bash
sudo journalctl -b 0 | grep "configure-moode"
```

Typical outputs:

- `configure-moode: All configuration is correct`
- `configure-moode: Database updated (moode will read fresh values at startup)`
- `configure-moode: Rebooting to apply boot config changes`
