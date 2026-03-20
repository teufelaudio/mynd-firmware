# MYNDberry Initial Setup

This guide explains how to turn a [MYND](https://teufel.de/mynd-107002004) speaker into a self-contained music media library player with Bluetooth and WiFi internet radio streaming capability by replacing the removable bluetooth module with a Raspberry Pi Zero 2 W (RPi) running the [moOde](https://moodeaudio.org/) media player OS.

The goal of this project, dubbed "*MYNDberry*", is to demonstrate the potential of the MYND speaker and its open source audio capabilities. MYNDberry includes an adapted MCU firmware, a custom PCB adapter designed to directly connect the RPi to the MYND (thanks [jonamb](https://github.com/jonamb)), the "ActionsLink" communication protocol layer (thanks [toxxin](https://github.com/toxxin)), boot-time configuration, and a runtime service layer needed for the MYND MCU and the RPi to cooperate on power handling, playback control, volume behavior, WiFi actions, and moOde audio configuration.

[**moOde Audio Player**](https://moodeaudio.org/) is a free and open-source audiophile audio player for Raspberry Pi computers and the DIY audio community. It offers a browser-based interface and supports many streaming options, including Bluetooth. A special thanks to moOde Audio Player founder Tim Curtis, and all who have contributed, for their work.

>## What to Expect:
>
>This modification is aimed at hobbyists who are interested in open source software and in learning more about open source hardware and software. Since this is a community-driven hobby project and does **not** have the same engineering resources as a full Teufel product, the user experience may be less polished and may **allow — and sometimes require — you to dive into configuration settings** that would normally be hidden in a commercial product.
>
>By performing this modification, your MYND will **no longer be able to play audio from the AUX and USB-C ports**. While Bluetooth playback and other moOde renderers remain available via the Raspberry Pi, you may experience **audio dropouts or other quirks** that are not present in the stock MYND firmware.

>**❗Disclaimer❗**: This guide is a community-driven project and does not represent an official feature. It is intended for experienced hobbyists with basic knowledge of electronics. The steps described for installing a Raspberry Pi into the MYND speaker are fully reversible and do not require any soldering. While we have not encountered any issues during our own tests, we cannot provide any warranty or accept liability for any damage that may result from following these instructions—whether to the MYND speaker itself or to external components such as the Raspberry Pi. Please check all connections carefully. Incorrectly connected cables may cause hardware damage. You undertake this modification at your own risk. We also cannot guarantee that the modification will work in every case.

---

## Before You Start

You will need:

- Raspberry Pi Zero 2 W (approx. 20 €)
- microSD card (approx. 10 €)
- MYNDberry adapter PCB (approx. 15 €)
  - [look here for more information](https://github.com/teufelaudio/mynd-hardware/wiki/myndberry_pcb)
- 2.5 mm (M3) allen wrench / hex key
- PH-01 screwdriver
- Flathead screwdriver (small)

---

## 1. Update the MYND Speaker Firmware

- Download the MYNDberry firmware binary `myndberry-update-firmware-mcu.bin` from the *teufelaudio* GitHub *Releases* section
  - [Download MYNDberry MCU firmware](https://github.com/teufelaudio/mynd-firmware/releases/latest/download/myndberry-update-firmware-mcu.bin
)

- Update the production MYND unit using the drag-and-drop update procedure documented in the HowTo:
  - [Perform a Drag & Drop USB Firmware Update](https://github.com/teufelaudio/mynd-firmware/wiki/perform_drag&drop_update)

---

## 2. Flash the moOde OS to the microSD Card

- Use the Raspberry Pi Imager to configure and write the moOde OS image to your SD card (tested on moOde OS v10.1.1 2026-02-19)
  - Follow the [moOde Pi Imager Tutorial](https://github.com/moode-player/docs/blob/main/setup_guide.md#5-pi-imager-tutorial) for step-by-step instructions
  - Make note of the username, password, and hostname that you choose, you will need them later

- Boot the Raspberry Pi and make sure it can reach the network
- For more info, see the [After Startup](https://github.com/moode-player/docs/blob/main/setup_guide.md#4-after-startup) section of the official moOde Setup Guide
  - **Note:** No manual moOde I2S device selection is required, it is handled on first boot by `configure-moode.service`, which is installed in the next step

---

## 3. Install the MYNDberry Daemon Package

- With a laptop or computer on the same network, open a browser and go to the moOde WebUI webpage at:
  - `http://your_rpi_hostname`

  or
  - `http://your_rpi_hostname.local`

- **Important**: Enable the moOde's AP hotspot by: 
   - Navigating to the top right corner and clicking *m*-->*Configure*-->*Network*
   - Scrolling down to the *Hotspot* section
   - Choosing a password no less than 8 characters
   - Clicking `SAVE`

- Enable bluetooth connectivity by enabling the *Bluetooth* controller via *m*-->*Configure*-->*Renderers* under *Bluetooth* and clicking *Controller*. Here you can enable other renderers that you wish to use as well.

- Enable the *Web SSH* feature and open a terminal webpage by: 
  - Navigating to the top right corner and clicking *m*-->*Configure*-->*System*
  - Scrolling down to the *Security* section
    >**Hint**: Consider enabling moOde's ready sound icon under the *Ready Script* section as an indicator of the speaker's overall streaming ready state
  - Enabling *Web SSH* and clicking `OPEN`
  - In the newly opened web terminal, enter the RPi credentials you chose during the RPi SD imaging setup in step 2
    - Type your `username`, hit `enter`
    - Type your `password`, hit `enter`
  - Copy and paste this command into the web terminal to begin the RpiLink installation:
  - ```sh
      curl -sSL https://raw.githubusercontent.com/teufelaudio/mynd-firmware/MYNDberry/Projects/Mynd/src/tasks/rpi/daemon_install/install_mynd_berry.sh | sudo bash -s
      ```
      - **Note:** If you see an error like `SSL certificate problem: certificate is not yet valid` when running the command above, the Raspberry Pi's clock is probably too far in the past. You can dynamically fetch the current time from the internet (over plain HTTP, so it still works even when TLS is broken) and apply it with:
        - ```sh
          sudo date -s "$(curl -sI http://deb.debian.org | awk '/^Date:/ {print substr($0,6)}')"
          ```
          or apply it manually with:
        - ```sh 
          sudo date -s "2026-03-19 12:00:00"
          ```
  - Enter `y` when prompted
- Once completed, take a moment to explore moOde to get familiar with its WebUI
  - [Official moOde Quickhelp Guide](https://github.com/moode-player/docs/blob/main/Quickhelp.pdf)
    - "Quick help contains instructions for navigating moOde and using its features including Multiroom audio. Quick help is located on the Main Menu which is accessed via the m in the upper right of the WebUI." -moOde

>**Hints**:
>- If you want MYNDberry to auto-play internet radio after boot completes, enable moOde *Autoplay after start* under *MPD Options* via *m*-->*Configure*-->*Audio*
>    - Build a Favorites playlist from the internet radio library
>    - Add the Favorites or Default playlist to the queue by clicking the playlist icon.
>    - After reassembling and powering on the MYND in the next steps, it will begin streaming from the top of the queue. If the ready script is enabled, the ready chime will play before streaming begins.
>- Turn off the power status feedback LED0 of the RPi via *m*-->*Configure*-->*System* under *Power management* to reduce power consumption of the RPi

---

## <a id="step4"></a>4. Carefully Integrate MYNDberry into your MYND

Follow the [HowTo Install MYNDberry](https://github.com/teufelaudio/mynd-hardware/wiki/myndberry_pcb) guide for in-depth instructions on how to:

- Open the MYND speaker by completely unscrewing and removing the grill (4 screws), and then the speaker frame (7 screws)
  - **Hint:** In the top right corner, there is a notch available to easily lever the speaker frame out using a flathead screwdriver

- Remove the Bluetooth module from the MYND PCB
- Insert the RPi into the MYNDberry PCB
- Replace the BT module by carefully inserting the MYNDberry in its place
- Secure the MYND PCB and MYNDberry and close the unit

---

## 5. Power Cycle the MYND Speaker

- Power on the MYND speaker, wait **45-60 seconds** (patience is a virtue;) for the BT source led pattern to stop flashing, meaning moOde has completed its boot up process.

  - If you decided to enable the ready script and add a playlist to moOde's queue, you should hear the moOde ready chime and shortly after hear playback of the first internet radio station in the queue.
    - If you still do not hear anything, try power cycling the MYND once more and see the Troubleshooting section if issues persist.
  
  - If you chose not to enable the ready script or add a playlist to the queue, then you will not hear anything after the MYNDberry becomes 'ready', but you should see the BT LED begin a purple breathing pattern indicating that moOde is in a standby ready state.

  - If you enabled the BT renderer and/or other renderers, you should now see the MYNDberry (named using your RPi hostname) in the BT device list and/or other applicable lists of nearby devices.

---
Use the MYND hardware buttons to control playback, volume, WiFi internet radio as follows:

### Playback control

| Button (press) | MPD (Internet Radio) | Bluetooth | Other Renderers | Notes / BT LED Feedback |
|--------|----------------------|-----------|-----------------|-------|
| `Play`/`Pause` (single) | Play/Pause streaming | Play/Pause playback | Mute/Unmute amp | <span style="color:#a78bfa">purple flashing</span> → source unknown (moOde not ready)<br/><span style="color:#a78bfa">purple breathing</span> → [MPD](https://www.musicpd.org) paused (moOde ready)<br/><span style="color:#a78bfa">purple solid</span> → [MPD](https://www.musicpd.org) actively streaming (streaming continues while amp muted) |
| `Play`/`Pause` (double) | Next station | Next song | <span style="color:#6b7280">Not implemented </span> | Stops at queue end, press `Play`/`Pause` to restart |
| `Play`/`Pause` (triple) | Previous station | Previous song | <span style="color:#6b7280">Not implemented</span> | Stops at queue start, press `Play`/`Pause` to restart |

### Default volume control

| Button (press) | MPD (Internet Radio) | Bluetooth | Other Renderers | Notes / BT LED Feedback |
|--------|----------------------|-----------|-----------------|-------|
| `VOL+` / `VOL-` (single, hold) | <span style="color:#60a5fa">Adjust [MPD](https://www.musicpd.org) volume</span> <sup id="ref1">[1](#fn1)</sup> | <span style="color:#6b7280">Not applied</span> <sup id="ref2">[2](#fn2)</sup> | <span style="color:#6b7280">Not implemented</span> <sup id="ref3">[3](#fn3)</sup> | <span style="color:#4ade80">green flash</span> → <span style="color:#86efac">Max volume</span><br/><span style="color:#f87171">red flash</span> → <span style="color:#fca5a5">Min volume</span> |

### Internet Radio Source Selection & WiFi Connectivity

| Button (press) | MPD (Internet Radio), Bluetooth, Other Renderers | Notes / BT LED Feedback |
|--------|:------------------------:|-------|
| `BT` (single) |  Return to internet radio from another source/renderer | <span style="color:#a78bfa">purple flashing</span> → source unknown (moOde not ready)<br/><span style="color:#a78bfa">purple breathing</span> → [MPD](https://www.musicpd.org) paused (moOde ready)<br/><span style="color:#a78bfa">purple solid</span> → [MPD](https://www.musicpd.org) actively streaming |
| `BT` (double) | Cycle to next WiFi <sup id="ref4">[4](#fn4)</sup> | <span style="color:#4ade80">delayed green flash</span> → <span style="color:#86efac">wifi is connected</span><br/><span style="color:#f87171">delayed red flash</span> → <span style="color:#fca5a5">attempt failed</span> |
| `BT` (triple) | Enable AP hotspot <sup id="ref5">[5](#fn5)</sup> | <span style="color:#4ade80">delayed green flash</span> → <span style="color:#86efac">hotspot is up</span><br/><span style="color:#f87171">delayed red flash</span> → <span style="color:#fca5a5">attempt failed</span> |

### *Optional: Amp volume control* <sup id="ref6">[6](#fn6)</sup>

| Button (press) | MPD (Internet Radio), Bluetooth, Other Renderers | Notes / BT LED Feedback |
|--------|:--------------------:|-------|
| `VOL+` **&** `VOL-` (combo) | Toggle on/off | <span style="color:#4ade80">green flash</span> → <span style="color:#86efac">[MPD](https://www.musicpd.org) control enabled</span><br/><span style="color:#f87171">red flash</span> → <span style="color:#fca5a5">Amp control enabled</span>|
| `VOL+` / `VOL-` (single, hold) | Adjust amp volume (***if enabled***) | <span style="color:#4ade80">green flash</span> → <span style="color:#86efac">Max Volume</span><br/><span style="color:#f87171">red flash</span> → <span style="color:#fca5a5">Min Volume</span> |

***Footnotes***

<a id="fn1"></a>**1.** By default, volume change events are applied to moOde's software volume mixer via the Media Player Daemon ([MPD](https://www.musicpd.org)). [↩](#ref1)

<a id="fn2"></a>**2.** You can enable experimental Bluetooth volume control by setting `ENABLE_BT_RENDERER_VOLUME_CONTROL=1` in `mynd-firmware/Projects/Mynd/CMakeLists.txt`, recompiling, and then updating the MCU firmware. This will allow MYND volume buttons to adjust the RPi's bluez playback volume directly, but may exhibit volume jumping behavior due to volume levels between bluez and the connected BT device becoming unsynchronized. [↩](#ref2)

<a id="fn3"></a>**3.** Due to protocol limitations/deprecation with other renderers, volume event *forwarding* only works while streaming internet radio with the MPD renderer. This means that by default, volume presses do nothing when streaming through other renderers, but you can toggle to amp volume control if needed. [↩](#ref3)

<a id="fn4"></a>**4.** Cycles through the RPi's known WiFi network list of previously saved/configured WiFi networks (via WebUI or `nmcli`) in sequential order until it successfully connects. If a hotspot is active, it is turned off first. Playback is paused during the switch. Requires at least one known WiFi network. The MCU log will return the WiFi info upon success connection [↩](#ref4)

<a id="fn5"></a>**5.** Hotspot **must** be configured before use or this functionality will not work. Either configure the moOde hotspot (set a password in moOde web UI → Network), or set `ssid` and `password` in the daemon config under `[hotspot]` at `/etc/mynd_rpi_link.conf`. [↩](#ref5)

<a id="fn6"></a>**6.** This feature provides alternative volume control via hardware amplifier and works under all circumstances, but can only be changed using the MYND volume buttons.[↩](#ref6)

---

# Congratulations!

Your MYND is now fully open...source =D. If something does not work as expected, see **Troubleshooting** section below. For configuration details and developer-oriented options, see **Advanced**.

## 6. Revert your MYND Back to Production

- Obtain the **production** MYND firmware binary `mynd-update-firmware-mcu.bin`, included alongside the MYNDberry release artifacts, from the *teufelaudio* GitHub *Releases* section
  - [Download production MYND MCU firmware](https://github.com/teufelaudio/mynd-firmware/releases/latest/download/mynd-update-firmware-mcu.bin)

- Update the MYND unit using this **production** firmware and following the drag-and-drop update procedure documented in the HowTo:
  - [Perform a Drag & Drop USB Firmware Update](https://github.com/teufelaudio/mynd-firmware/wiki/perform_drag&drop_update)

- Perform [step 4](#step4) in reverse and carefully re-integrate the Bluetooth module hardware back into the MYND PCB.

---

## Useful References 

- How to [Perform a Drag & Drop USB Firmware Update](https://github.com/teufelaudio/mynd-firmware/wiki/perform_drag&drop_update)
- Learn more about [MCU Firmwares](https://github.com/teufelaudio/mynd-firmware/wiki/mcu_firmwares)
- Explore the [MYND Hardware GitHub](https://github.com/teufelaudio/mynd-hardware)
- Review [MYND BT Connector Component Schematic](https://github.com/teufelaudio/mynd-hardware/blob/main/PCB_Schematics/PDF/Mynd_BT_SCH.PDF)

### Additonal Documentation
- Mynd RpiLink Daemon: [mynd-firmware/Projects/Mynd/src/tasks/rpi/daemon_install/README.md](https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/src/tasks/rpi/daemon_install/README.md)
- ActionsLink protocol specification: [mynd-firmware/blob/MYNDberry/Projects/Mynd/external/teufel/libs/actionslink/docs/PROTOCOL.md](https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/external/teufel/libs/actionslink/docs/PROTOCOL.md)
- ActionsLink protobuf protocol internals and extension examples: [mynd-firmware/Projects/Mynd/docs/README_ACTIONSLINK_PROTOCOL.md](https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/src/tasks/rpi/daemon_install/docs/README_ACTIONSLINK_PROTOCOL.md)
- Configure moOde service behavior and defaults: [mynd-firmware/Projects/Mynd/docs/README_CONFIGURE.md](https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/src/tasks/rpi/daemon_install/docs/README_CONFIGURE.md)
- LED patterns and extension examples: [mynd-firmware/Projects/Mynd/src/leds/README.md](https://github.com/teufelaudio/mynd-firmware/blob/MYNDberry/Projects/Mynd/src/leds/README.md)

#### Production MYND Documentation
- [User Manual](https://cdn.teufelaudio.com/image/upload/w_100/v1/products/MYND/pdf/Teufel_MYND_UM_DE_V1.3_PD.pdf)
- [Quick Start Guide](https://cdn.teufelaudio.com/image/upload/w_100/v1/products/MYND/pdf/Teufel_MYND_QSG_V1.0_PD_web.pdf)
- [Battery Repair Guide](https://cdn.teufelaudio.com/products/MYND/pdf/Teufel_MYND_RM_DE_V1.0_PD.pdf)
- [Safety Booklet](https://cdn.teufelaudio.com/image/upload/w_100/v1/products/MOTIV_GO/Safety_Booklet_Portables_version_1.0_september22.pdf)
- [App Software Requirements](https://cdn.teufelaudio.com/image/upload/v1748589486/products/MYND/pdf/OA2302-MYND_OPEN_SOURCE-App-SW-Requirements-2025-05-23.pdf)

---

## Troubleshooting

### Verify the Installation

After installation and the first reboot, check both services.

```bash
sudo systemctl status configure-moode
sudo systemctl status mynd-rpi-link
```

Follow the daemon logs:

```bash
sudo journalctl -u mynd-rpi-link -f
```

Check boot-time configure logs:

```bash
sudo journalctl -b 0 | grep "configure-moode"
```

Run the configure script manually if needed:

```bash
sudo /usr/local/bin/configure_moode.py
```

What you want to confirm:

- `configure-moode` completed without error
- Your MYNDberry is reachable over the network
- `mynd-rpi-link.service` is active
- MYND button actions produce expected playback behavior
- Your MYNDberry hostname appears as a connectable device after moOde renderer activation
- Audio playback works as expected through enabled moOde renderers

### moOde boots and does not play anything
- Sometimes, especially after a fresh MCU firmware update or a new network is configured, moOde is unable to resolve any radio URLs. It is often resolved by power cycling the MYND speaker.

### `mynd-rpi-link.service` does not start
- Try to diagnois the issue by running the following commands on the RPi and reviewing the logs
  ```bash
  sudo systemctl status mynd-rpi-link
  sudo journalctl -u mynd-rpi-link -n 200 --no-pager
  ```

### Bluetooth Streaming Drops Out / Is Unstable
- Enabling other renderers alongside the Bluetooth renderer may degrade BT streaming performance, try disabling them
- This is a known issue and attempts have been made to mitigate it (see next)

### No WiFi Connectivity, WebUI Disconnects/Unresponsive When Streaming via Bluetooth Device
- In testing, disabling wifi improved bluetooth streaming stability. So by default, wifi is disabled whenever Bluetooth is actively streaming, then re-enabled after disconnecting from the BT device or after a BT button action
 - Disable this feature by setting `disable_wifi_when_bt_active = false` in `/etc/mynd_rpi_link.conf` under `[bt_wifi_coexistence]` and rebooting

### UART or serial device issues

- Confirm on the MYNDberry, the assigned serial device for MCU's ActionLink BT UART connection is present
```bash
ls -l /dev/serial*
ls -l /dev/ttyS0 /dev/ttyAMA0
```

### Boot configuration changes seem not to have applied

`configure-moode` is responsible for enabling UART and disabling the serial console at boot

- Ensure it is enabled in `/etc/mynd_rpi_link.conf` and reboot once more.
- Check:
  - `sudo journalctl -b 0 | grep "configure-moode"`
- Run:
  - `sudo /usr/local/bin/configure_moode.py`

### Perform a Hardware Reset of the MYNDberry Speaker

- If for any reason the MYNDberry gets into a 'hung' or 'frozen' state, you can perform a hardware reset by holding down both `VOL-` and `VOL+` buttons for 8-10 seconds.

### Uninstall RpiLink Daemon

To remove the MYNDberry Raspberry Pi integration:

```bash
sudo /usr/local/bin/uninstall_rpi_link.sh
```

### Adjusting the Configuration

The installed config file is:

```text
/etc/mynd_rpi_link.conf
```

### Main Config Sections

- `[configure_moode]`
  - enables or disables boot-time configuration
  - sets `boot_config_path`
  - sets `moode_boot_volume_percent`
  - sets `bt_sw_volume_step`
- `[uart]`
  - UART device path, baud rate, and timeout
- `[moode]`
  - moOde base URL, timeout, retry count, streaming poll interval, and MPD port
- `[power]`
  - poweroff command
- `[hotspot]`
  - h otspot name, password, and interface
  - Enable for quick and easy moOde and or RPi access (disabled by default)
- `[bt_wifi_coexistence]`
  - WiFi disable and restore behavior during Bluetooth usage
  - Enable to improve BT stability (disabled by default)
- `[logging]`
  - log level and syslog behavior

After editing the config, restart the runtime daemon:

```bash
sudo systemctl restart mynd-rpi-link
```
### Remote Un/Install Options for Iterative Development

- Replace `pi` and `raspberrypi` with your chosen username and hostname, respectively

Remote uninstall from a workstation:

```bash
ssh pi@raspberrypi "/usr/local/bin/uninstall_rpi_link.sh"
```

Remote install from a workstation:
- **Hint:** Ensure the daemon's protobuf files are up-to-date (regenerated if any changes made to ActionsLink protocol)
```bash
cd Projects/Mynd/src/tasks/rpi/daemon_install
bash ./components/remote/install.sh --host raspberrypi.local --user pi
```

---

## Advanced

### Power On MYND when Open
  - Mynd cannot power on when it is *open* (speaker frame has been removed)
  - If you wish to power on the MYND while it is *open* (for access to flash and uart debug pins during development and/or testing), [you must first short pins 4 and 5](https://github.com/teufelaudio/mynd-hardware/blob/main/PCB_Schematics/PDF/Mynd_AMP_CONN_SCH.PDF) of the `P1S` amp baffle connector component
