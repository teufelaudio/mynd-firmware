# Mynd RPiLink Daemon

This daemon bridges the MYND MCU and a Raspberry Pi running Moode over ActionsLink (UART at 115200 baud). It handles power handshakes, playback actions, stream/source notifications, and optional WiFi/hotspot operations via `nmcli`.

## Features

- ActionsLink protocol bridge between MCU and RPi (`set_power_state`, playback actions, volume, and event-driven WiFi/hotspot commands).
- Coordinated MCU-RPi power-state handshake (`notify_system_ready`, `notify_power_state`, startup fallback handling).
- Host source and stream-state notifications back to MCU (`notify_host_source`, `notify_stream_state`).
- WiFi workflow hooks from speaker buttons (`configure_wifi`, hotspot enable, network cycling) with explicit completion telemetry and `notify_wifi_info` follow-up on success.
- Systemd integration for both daemon runtime and boot-time configure service.
- INI-based runtime configuration via `/etc/mynd_rpi_link.conf`.

## Installer Package Layout

```text
daemon_install/
|- install.sh
|- install_mynd_berry.sh
|- components/
|  |- local/install.sh
|  |- local/uninstall.sh
|  |- remote/install.sh
|  |- package/build.sh
|- payload/
|  |- bin/
|  |  |- mynd_rpi_link.py
|  |  |- actionslink_adapter.py
|  |  |- bluetooth_controller.py
|  |  |- command_runner.py
|  |  |- moode_client.py
|  |  |- daemon_context.py
|  |  |- link_protocol.py
|  |  |- mpd_client.py
|  |  |- playback_controller.py
|  |  |- power_controller.py
|  |  |- request_handlers.py
|  |  |- wifi_controller.py
|  |  |- configure_moode.py
|  |- config/
|  |  |- mynd_rpi_link.conf
|  |- systemd/
|  |  |- mynd-rpi-link.service
|  |  |- configure-moode.service
|  |- sudoers/
|     |- mynd-rpi-link.sudoers
|- docs/
|  |- README_ACTIONSLINK_PROTOCOL.md
|  |- README_CONFIGURE.md
|  |- README_RELEASE.md
```

## Components

- `payload/bin/mynd_rpi_link.py`: thin entrypoint that wires the daemon runtime, ActionsLink client, and controllers.
- `payload/bin/actionslink_adapter.py`: shared ActionsLink/protobuf imports for both repo and installed layouts.
- `payload/bin/daemon_context.py`: config loading, logging setup, shared daemon state, and power-state constants.
- `payload/bin/link_protocol.py`: MCU request/response/event helpers, retries, and the single-response guard used by request handlers.
- `payload/bin/request_handlers.py`: MCU-facing request handlers and event callbacks.
- `payload/bin/power_controller.py`: ON/OFF handshake flow, startup fallback handling, shutdown prep, and poweroff.
- `payload/bin/playback_controller.py`: source detection, volume/playback routing, cycle-source logic, and the streaming monitor loop.
- `payload/bin/mpd_client.py`: consolidated MPD socket access for one-shot and persistent monitor queries.
- `payload/bin/bluetooth_controller.py`: BlueZ signal listener, AVRCP play/pause and track control, plus optional BT software volume stepping.
- `payload/bin/wifi_controller.py`: hotspot, WiFi cycling/configuration, and BT/WiFi coexistence policy.
- `payload/bin/command_runner.py`: non-blocking subprocess wrapper used by the daemon and Moode helpers.
- `payload/bin/moode_client.py`: async Moode REST client and Moode utility wrappers.
- `payload/bin/configure_moode.py`: boot-time setup helper for UART/serial-console, I2C, I2S overlay, and Moode DB values.
- `install.sh`: local installer entrypoint (run on Raspberry Pi).
- `install_mynd_berry.sh`: URL bootstrap installer (run on Raspberry Pi).
- `components/remote/install.sh`: remote copy/install helper (run from laptop/workstation).
- `components/local/uninstall.sh`: uninstall source (installed as `/usr/local/bin/uninstall_rpi_link.sh`).

## Related Documentation

- ActionsLink internals and extension examples: `docs/README_ACTIONSLINK_PROTOCOL.md`
- Configure service behavior and defaults: `docs/README_CONFIGURE.md`
- Release packaging and URL bootstrap flow: `docs/README_RELEASE.md`

### Additonal Documentation
- LED pattern implementation: `mynd-firmware/Projects/Mynd/src/leds/README.md`

## Protocol and Runtime Behavior

### MCU -> RPi requests handled

- `set_power_state`
- `send_playback_action`
- `set_volume`
- `cycle_source`

### MCU -> RPi events handled

- `notify_battery_level`
- `notify_charger_status`
- `notify_battery_friendly_charging`
- `notify_configure_wifi_command`
- `notify_enable_hotspot_command`
- `notify_cycle_wifi_network_command`

### RPi -> MCU events sent

- `notify_system_ready`
- `notify_power_state`
- `notify_stream_state`
- `notify_host_source`
- `notify_play_led_pattern`
- `notify_wifi_command_result`
- `notify_wifi_info`

### Handler invariants

- Every MCU request handler sends exactly one status response on every exit path.
- Long-running system commands (`nmcli`, Moode utilities, `sync`, poweroff) execute off the asyncio event loop.
- WiFi reconfiguration and power/shutdown transitions are serialized to avoid overlapping state mutations.
- WiFi commands are event-driven, use `command_id` correlation, and always terminate with `notify_wifi_command_result`.
- Successful WiFi commands emit both `notify_wifi_command_result` and `notify_wifi_info`; failures emit only the result event.

### Power-state flow summary

- Startup: daemon connects, registers handlers, then sends `notify_system_ready`.
- On power-on request: daemon answers `set_power_state(ON)` and later sends `notify_power_state(ON)` after MCU tick processing delay.
- On daemon initialization / power-on handling: daemon restores WiFi radio on startup before starting its playback monitor, which clears any earlier BT/WiFi coexistence forced-off state.
- On shutdown request: daemon pauses playback, stops monitoring, syncs filesystems, sends `notify_power_state(SHUTDOWN_REQUEST)` or `notify_power_state(OFF)` as appropriate, then executes configured poweroff command.
- If daemon starts after MCU is already on, startup fallback requests MCU firmware version and performs synthetic ON initialization.

Detailed sequence and timing notes are documented in `Projects/Mynd/docs/mcu_rpilink_pwr_seq.puml` and `docs/README_ACTIONSLINK_PROTOCOL.md`.

## Button and Action Mapping (RPi firmware mode)

Runtime button behavior is implemented in `Projects/Mynd/src/tasks/audio_rpi/task_audio.cpp` and forwarded by `Projects/Mynd/src/tasks/rpi/task_rpi.cpp`.

- Play short press:
  - host source `MPD` or `Bluetooth`: forwarded as playback action
  - other renderer host sources: MCU handles amp mute toggle
- BT double press: sends `cycle_wifi_network`
- BT triple press: sends `enable_hotspot`
- Power+Minus long press: sends drag-and-drop update pre-shutdown request to RPi task

## Volume Control Modes

Volume behavior spans MCU and daemon:

- Compile-time controls in firmware:
  - `HYBRID_VOLUME_MODE`
  - `ENABLE_BT_RENDERER_VOLUME_CONTROL` (optional, off unless defined)
- Runtime toggle in hybrid mode:
  - single short `VOL+|VOL-` combo toggles whether `VOL+/-` controls renderer volume (`set_volume` requests) or local amp gain

In all cases, `configure_moode.py` sets Moode boot `volknob` using `[configure_moode] moode_boot_volume_percent`.

## Source and Status LEDs

The daemon can trigger source feedback patterns through `notify_play_led_pattern` and host-source updates. Final source/status LED rendering remains MCU-side (`audio_rpi` + `leds` tasks), including battery/status behavior.

## Configuration

Installed config: `/etc/mynd_rpi_link.conf`

Sections:

- `[configure_moode]`
- `[uart]`
- `[moode]`
- `[power]`
- `[hotspot]`
- `[bt_wifi_coexistence]`
- `[logging]`

Important shipped defaults:

- UART: `/dev/serial0`, `baudrate = 115200`
- Moode base URL: `http://localhost`
- Stream polling interval: `streaming_poll_interval = 2.0`
- Hotspot:
  - If `ssid` and `password` are set, daemon creates/starts that hotspot
  - Otherwise daemon brings up the existing Moode hotspot connection
  - `connection_name = ` (optional override; if empty, derives Moode hotspot name from hostname)
  - `ssid = `
  - `password = `
  - `interface = wlan0`
- BT/WiFi coexistence:
  - `disable_wifi_when_bt_active = false`
    - `disable_only_while_streaming = true`
    - `restore_previous_connection = true`

### Hotspot Setup

BT triple press sends `enable_hotspot`. That event is enabled in firmware by default and the daemon decides which hotspot mode to use from `mynd_rpi_link.conf`.

Mode 1: To use Moode hotspot, you must

- Leave `connection_name`, `ssid`, and `password` empty under `[hotspot]` in `mynd_rpi_link.conf`.
- In Moode web UI, open Network configuration and set a hotspot password. This is what enables Moode's hotspot profile in NetworkManager.

Mode 2: Use a daemon-defined hotspot

- Set `connection_name`, `ssid`, and `password` under `[hotspot]` in `mynd_rpi_link.conf`.
- The daemon will create/start this hotspot directly with `nmcli device wifi hotspot`.

Example:

```ini
[hotspot]
connection_name = MyndHotspot
ssid = MyndHotspot
password = changeme123
interface = wlan0
```

Conditions for BT triple press to work:

- Firmware must be built with `ENABLE_RPI_LINK_HOTSPOT=1` (current default in `Projects/Mynd/CMakeLists.txt`).
- `mynd-rpi-link` service must be installed and running.
- The RPi must be on when the event is sent.
- `interface` must match the WiFi interface used for AP mode, normally `wlan0`.
- For Moode mode: the hotspot must already be configured in Moode web UI so a matching NetworkManager connection exists.
- For daemon-defined mode: both `ssid` and `password` must be non-empty; if only one is set, the request fails.

What happens on failure:

- If Moode hotspot is not configured yet, the daemon emits `notify_wifi_command_result(status=ResourceUnavailable)` and logs that the user must configure hotspot SSID/password in Moode web UI first.
- If the Moode hotspot connection exists but its saved hotspot password secret is missing, activation fails with `ResourceUnavailable` and the daemon logs that the hotspot password must be set again in Moode web UI.
- If a daemon-defined hotspot is incomplete or `nmcli` activation fails, the daemon emits `notify_wifi_command_result(status=OperationFailed)`.

After config changes:

```bash
sudo systemctl restart mynd-rpi-link
```

### Optional aggressive BT/WiFi mode

Set `[bt_wifi_coexistence] disable_wifi_when_bt_active = true` to force WiFi radio off while Bluetooth source is active (or only while streaming, depending on `disable_only_while_streaming`). This can improve BT stability on Pi Zero 2 W but temporarily drops WiFi connectivity.

## Tests

Behavior-focused unit tests for the refactored daemon live in `tests/test_mynd_rpi_link.py`.

Run them from the repo root with:

```bash
python3 -m unittest discover -s "Projects/Mynd/src/tasks/rpi/daemon_install/tests" -p "test_*.py"
```

Recommended hardware validation after WiFi protocol changes:

- Connect to a known network with `configure_wifi` and confirm both `notify_wifi_command_result` and `notify_wifi_info`.
- Attempt `configure_wifi` with a wrong password and confirm only `notify_wifi_command_result`.
- Trigger `enable_hotspot` with a valid Moode or custom hotspot profile and confirm completion plus info telemetry.
- Trigger `enable_hotspot` with hotspot unavailable and confirm failure completion telemetry.
- Trigger `cycle_wifi_network` and confirm `command_id` correlation plus `target_reached` behavior.
- Repeat WiFi button presses during an active WiFi operation and confirm the MCU/daemon do not start overlapping jobs.

## Installation

### Prerequisites

- Raspberry Pi with Moode OS
- `sudo` access
- Python 3
- ActionsLink host files (`host/`) available next to `daemon_install/` for local/manual installer path

`install.sh` installs missing dependencies (`aiohttp`, `aioserial`, `protobuf`), copies all daemon Python modules from `payload/bin/` into `/usr/local/bin`, installs the ActionsLink host library under `/usr/local/bin/actionslink/`, and installs both systemd units.

### Option 1: Install current release from teufelaudio on Raspberry Pi

```bash
curl -sSL http://https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/src/tasks/rpi/daemon_install/install_mynd_berry.sh | sudo bash -s
```

Equivalent with `wget`:

```bash
wget -qO- "http://https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/src/tasks/rpi/daemon_install/install_mynd_berry.sh" | sudo bash -s
```

See `docs/README_RELEASE.md` for package details.

### Option 2: Remote install from laptop

```bash
cd Projects/Mynd/src/tasks/rpi/daemon_install
bash ./components/remote/install.sh --host raspberrypi.local --user pi
```

### Option 3: Manual local install on Raspberry Pi

Copy both directories to the Pi under the same parent:

```bash
scp -r Projects/Mynd/external/teufel/libs/actionslink/host pi@raspberrypi.local:/tmp/mynd_berry_install/
scp -r Projects/Mynd/src/tasks/rpi/daemon_install pi@raspberrypi.local:/tmp/mynd_berry_install/
```

Then install:

```bash
ssh pi@raspberrypi.local
cd /tmp/mynd_berry_install/daemon_install
sudo bash ./install.sh
```

## Service Management

```bash
sudo systemctl status mynd-rpi-link
sudo journalctl -u mynd-rpi-link -f
sudo systemctl restart mynd-rpi-link
sudo systemctl stop mynd-rpi-link
sudo systemctl start mynd-rpi-link
```

## Testing and Debugging

Run daemon in foreground:

```bash
sudo /usr/local/bin/mynd_rpi_link.py --config /etc/mynd_rpi_link.conf --foreground
```

Increase daemon log verbosity in `/etc/mynd_rpi_link.conf`:

```ini
[logging]
level = DEBUG
use_syslog = true
```

Then restart and follow logs:

```bash
sudo systemctl restart mynd-rpi-link
sudo journalctl -u mynd-rpi-link -f
```

## Troubleshooting

### Daemon does not start

```bash
sudo systemctl status mynd-rpi-link
sudo journalctl -u mynd-rpi-link -n 200 --no-pager
```

### Serial device errors

```bash
ls -l /dev/serial*
ls -l /dev/ttyS0 /dev/ttyAMA0
```

`configure-moode` is responsible for enabling UART and disabling serial console at boot.

### WiFi/hotspot requests fail

Check sudoers:

```bash
sudo visudo -c -f /etc/sudoers.d/mynd-rpi-link
```

Check NetworkManager state:

```bash
nmcli -t -f NAME,TYPE connection show
nmcli -t -f NAME,TYPE connection show --active
```

Check hotspot mode/config:

- Moode mode: verify `[hotspot] ssid` and `password` are empty, then confirm the Moode hotspot connection exists with `nmcli connection show "Moode-rpi-zero"` or with your hostname-derived/overridden connection name.
- Daemon-defined mode: verify both `[hotspot] ssid` and `password` are set and that password length is valid for WPA2.
- If you change `/etc/mynd_rpi_link.conf`, restart the daemon with `sudo systemctl restart mynd-rpi-link`.

## Security Notes

- Sudoers grants passwordless `poweroff` and selected `nmcli` commands.
- `configure_wifi` can pass SSID/password on command line (visible in local process list).
- If `ssid` and `password` are set for a config-defined hotspot, they are stored in plaintext in `/etc/mynd_rpi_link.conf`.
- UART transport is not encrypted.

## Uninstall

```bash
sudo /usr/local/bin/uninstall_rpi_link.sh
```

Uninstall removes service units, daemon scripts, ActionsLink install directory, config file, and sudoers file.