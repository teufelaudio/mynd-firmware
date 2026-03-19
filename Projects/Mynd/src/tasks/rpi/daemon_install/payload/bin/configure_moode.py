#!/usr/bin/env python3
"""
Mynd RPi Boot Configuration Service

Runs once at boot (before nginx/moode) to ensure:
- UART enabled (for MCU communication)
- Serial console over UART disabled
- I2C enabled
- SSH enabled
- I2S DAC audio configured correctly in moode's database

This script follows moode's established sqlite3 conventions:
- cfg_system: i2sdevice, i2soverlay, adevname, cardnum, alsavolume,
              amixname, alsa_output_mode, mpdmixer
- cfg_mpd:    mixer_type, device (ALSA card number for output)
- cfg_outputdev: device output cache

It does NOT touch moode-managed files (_audioout.conf, constants.php).
Those are managed by moode's worker/audio pipeline.

The I2S overlay is placed inside moode's managed "# Audio overlays"
section of config.txt so moode's own overlay management stays consistent.
"""

import configparser
import os
import sqlite3
import subprocess
import sys
import syslog
from pathlib import Path

CONFIG_PATH = "/etc/mynd_rpi_link.conf"
MOODE_DB_PATH = "/var/local/www/db/moode-sqlite3.db"
BOOT_CONFIG_PATHS = ["/boot/firmware/config.txt", "/boot/config.txt"]
CMDLINE_PATHS = ["/boot/firmware/cmdline.txt", "/boot/cmdline.txt"]
SSH_ENABLE_PATHS = ["/boot/firmware/ssh", "/boot/ssh"]

MOODE_AUDIO_OVERLAYS_HEADER = "# Audio overlays"

TARGET_DEVICE = "Generic-I2S (hifiberry-dac)"
TARGET_OVERLAY = "hifiberry-dac"
TARGET_CARDNUM = "0"

# Actual default value defined by moode_boot_volume_percent in /etc/mynd_rpi_link.conf.
FALLBACK_MOODE_BOOT_VOLUME_PERCENT = 10

# cfg_mpd mixer_type controls MPD's audio output mixer mode.
# Default in moode's schema is "hardware"; we need "software".
CFG_MPD_MIXER_TYPE = "software"

# Correct moode cfg_system values for Generic-I2S DAC with software volume.
# These follow moode's own conventions from snd-config.php and audio.php.
CFG_SYSTEM_PARAMS = {
    "i2sdevice": TARGET_DEVICE,
    "i2soverlay": "None",          # Must be "None" when i2sdevice is set (mutually exclusive)
    "adevname": TARGET_DEVICE,
    "cardnum": TARGET_CARDNUM,
    "alsavolume": "none",           # No hardware mixer on generic I2S DAC
    "amixname": "none",             # No hardware mixer control name
    "alsa_output_mode": "plughw",   # NOT iec958 (that's for HDMI only)
    "mpdmixer": CFG_MPD_MIXER_TYPE, # Session mirror of cfg_mpd.mixer_type
}

# cfg_outputdev cache for the I2S device.
# moode's checkOutputDeviceCache() reads this and can override cfg_system.
CFG_OUTPUTDEV = {
    "device_name": TARGET_DEVICE,
    "mpd_volume_type": CFG_MPD_MIXER_TYPE,
    "alsa_output_mode": "plughw",
    "alsa_max_volume": "100",
}

def log(msg: str) -> None:
    syslog.syslog(syslog.LOG_INFO, f"configure-moode: {msg}")
    print(f"[INFO] {msg}")


def _load_config() -> dict:
    cfg = configparser.ConfigParser()
    if os.path.exists(CONFIG_PATH):
        cfg.read(CONFIG_PATH)
    try:
        moode_boot_volume_percent = cfg.getint(
            "configure_moode",
            "moode_boot_volume_percent",
            fallback=FALLBACK_MOODE_BOOT_VOLUME_PERCENT
        )
    except ValueError:
        log(
            "Invalid moode_boot_volume_percent in config, "
            f"using default {FALLBACK_MOODE_BOOT_VOLUME_PERCENT}"
        )
        moode_boot_volume_percent = FALLBACK_MOODE_BOOT_VOLUME_PERCENT
    if moode_boot_volume_percent < 0 or moode_boot_volume_percent > 100:
        clamped = max(0, min(100, moode_boot_volume_percent))
        log(
            "moode_boot_volume_percent out of range "
            f"({moode_boot_volume_percent}), clamping to {clamped}"
        )
        moode_boot_volume_percent = clamped
    return {
        "enabled": cfg.getboolean("configure_moode", "enabled", fallback=True),
        "boot_config_path": cfg.get(
            "configure_moode", "boot_config_path",
            fallback="/boot/firmware/config.txt"
        ),
        "moode_boot_volume_percent": moode_boot_volume_percent,
    }


def _resolve_path(paths: list[str]) -> str | None:
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def _get_boot_config_path(cfg: dict) -> str | None:
    path = cfg.get("boot_config_path")
    if path and os.path.exists(path):
        return path
    return _resolve_path(BOOT_CONFIG_PATHS)


# ---------------------------------------------------------------------------
# Boot config checks (changes here require a reboot)
# ---------------------------------------------------------------------------

def ensure_ssh_enabled() -> bool:
    changed = False
    for ssh_path in SSH_ENABLE_PATHS:
        parent = os.path.dirname(ssh_path)
        if os.path.exists(parent) and not os.path.exists(ssh_path):
            Path(ssh_path).touch()
            log(f"SSH enabled ({ssh_path})")
            changed = True
    return changed


def check_and_enable_uart(boot_config_path: str) -> bool:
    if not boot_config_path:
        return False
    lines = Path(boot_config_path).read_text().splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("enable_uart"):
            if stripped == "enable_uart=1":
                return False
            lines[i] = "enable_uart=1"
            Path(boot_config_path).write_text("\n".join(lines) + "\n")
            log("UART enabled")
            return True
    lines.append("enable_uart=1")
    Path(boot_config_path).write_text("\n".join(lines) + "\n")
    log("UART enabled")
    return True


def check_and_disable_serial_console() -> bool:
    path = _resolve_path(CMDLINE_PATHS)
    if not path:
        return False
    tokens = Path(path).read_text().split()
    serial_prefixes = ("console=serial0,", "console=ttyAMA0,", "console=ttyS0,")
    new_tokens = [t for t in tokens if not t.startswith(serial_prefixes)]
    if len(new_tokens) == len(tokens):
        return False
    Path(path).write_text(" ".join(new_tokens) + "\n")
    log("Serial console over UART disabled")
    return True


def check_and_enable_i2c(boot_config_path: str) -> bool:
    if not boot_config_path:
        return False
    lines = Path(boot_config_path).read_text().splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("dtparam=i2c_arm"):
            if "on" in stripped:
                return False
            lines[i] = "dtparam=i2c_arm=on"
            Path(boot_config_path).write_text("\n".join(lines) + "\n")
            log("I2C enabled")
            return True
    lines.append("dtparam=i2c_arm=on")
    Path(boot_config_path).write_text("\n".join(lines) + "\n")
    log("I2C enabled")
    return True


def ensure_i2s_overlay(boot_config_path: str) -> bool:
    """Ensure dtoverlay=hifiberry-dac is in moode's managed Audio overlays section.

    Moode manages audio overlays via the "# Audio overlays" section header
    in config.txt (see updBootConfigTxt in common.php). The line immediately
    after the header is the active overlay. We place our overlay there so
    moode's own overlay management stays consistent.
    """
    if not boot_config_path:
        return False

    content = Path(boot_config_path).read_text()
    overlay_line = f"dtoverlay={TARGET_OVERLAY}"
    lines = content.splitlines()
    changed = False

    # 1. Handle moode's managed "# Audio overlays" section
    header_idx = None
    for i, line in enumerate(lines):
        if line.strip() == MOODE_AUDIO_OVERLAYS_HEADER:
            header_idx = i
            break

    if header_idx is not None and header_idx + 1 < len(lines):
        next_line = lines[header_idx + 1].strip()
        if next_line != overlay_line:
            lines[header_idx + 1] = overlay_line
            changed = True
            log(f"Updated moode audio overlay section: {overlay_line}")
    elif header_idx is not None:
        lines.insert(header_idx + 1, overlay_line)
        changed = True
        log(f"Inserted overlay after moode header: {overlay_line}")

    # 2. Remove stale custom overlay sections from old script
    cleaned = []
    skip_section = False
    for line in lines:
        stripped = line.strip()
        if stripped in ("# I2S DAC", "# I2S DAC (auto-configured)"):
            skip_section = True
            changed = True
            continue
        if skip_section:
            if stripped.startswith("dtoverlay=") or stripped == "":
                continue
            skip_section = False
        cleaned.append(line)

    # Remove trailing blank lines that may have been left behind
    while cleaned and cleaned[-1].strip() == "":
        cleaned.pop()

    if changed:
        Path(boot_config_path).write_text("\n".join(cleaned) + "\n")
        log("Boot config overlay updated")

    return changed


# ---------------------------------------------------------------------------
# Moode database checks (no reboot needed; worker reads fresh values at start)
# ---------------------------------------------------------------------------

def _db_update_if_needed(cur, table: str, param: str, value: str) -> bool:
    """Update a param=value row in a moode key-value table if it differs."""
    cur.execute(f"SELECT value FROM {table} WHERE param=?", (param,))
    row = cur.fetchone()
    if row and row[0] == value:
        return False
    cur.execute(f"UPDATE {table} SET value=? WHERE param=?", (value, param))
    return True

def _set_volknob(percent: int) -> None:
    """Set moode's volknob in the database and software mixer level."""
    percent = max(0, min(100, percent))
    if not os.path.exists(MOODE_DB_PATH):
        return
    try:
        with sqlite3.connect(MOODE_DB_PATH) as conn:
            conn.execute("UPDATE cfg_system SET value=? WHERE param='volknob'", (str(percent),))
            conn.commit()
        log(f"volknob set to {percent}%")
    except Exception as e:
        log(f"Failed to set volknob: {e}")

def ensure_moode_audio_config() -> bool:
    """Ensure moode database has correct I2S DAC + software volume settings.

    mpdmixer/mixer_type is ALWAYS "software" regardless of hybrid volume control mode.
    """
    if not os.path.exists(MOODE_DB_PATH):
        log("Moode database not found, skipping audio config")
        return False

    changed = False
    with sqlite3.connect(MOODE_DB_PATH) as conn:
        cur = conn.cursor()

        # cfg_system params
        for param, value in CFG_SYSTEM_PARAMS.items():
            if _db_update_if_needed(cur, "cfg_system", param, value):
                log(f"cfg_system: {param} -> {value}")
                changed = True

        # cfg_mpd mixer_type and device (ALSA card number for output)
        if _db_update_if_needed(cur, "cfg_mpd", "mixer_type", CFG_MPD_MIXER_TYPE):
            log(f"cfg_mpd: mixer_type -> {CFG_MPD_MIXER_TYPE}")
            changed = True
        if _db_update_if_needed(cur, "cfg_mpd", "device", TARGET_CARDNUM):
            log(f"cfg_mpd: device -> {TARGET_CARDNUM} (I2S)")
            changed = True

        # cfg_outputdev cache (prevents stale cached values from overriding cfg_system)
        cur.execute(
            "SELECT alsa_output_mode, mpd_volume_type FROM cfg_outputdev WHERE device_name=?",
            (CFG_OUTPUTDEV["device_name"],)
        )
        row = cur.fetchone()
        if row:
            if row[0] != CFG_OUTPUTDEV["alsa_output_mode"] or row[1] != CFG_OUTPUTDEV["mpd_volume_type"]:
                cur.execute(
                    "UPDATE cfg_outputdev SET mpd_volume_type=?, alsa_output_mode=?, alsa_max_volume=? "
                    "WHERE device_name=?",
                    (CFG_OUTPUTDEV["mpd_volume_type"], CFG_OUTPUTDEV["alsa_output_mode"],
                     CFG_OUTPUTDEV["alsa_max_volume"], CFG_OUTPUTDEV["device_name"])
                )
                log(f"cfg_outputdev: updated cache for {CFG_OUTPUTDEV['device_name']}")
                changed = True
        else:
            cur.execute(
                "INSERT INTO cfg_outputdev (device_name, mpd_volume_type, alsa_output_mode, alsa_max_volume) "
                "VALUES (?, ?, ?, ?)",
                (CFG_OUTPUTDEV["device_name"], CFG_OUTPUTDEV["mpd_volume_type"],
                 CFG_OUTPUTDEV["alsa_output_mode"], CFG_OUTPUTDEV["alsa_max_volume"])
            )
            log(f"cfg_outputdev: inserted cache for {CFG_OUTPUTDEV['device_name']}")
            changed = True

        conn.commit()

    return changed


def run_upd_mpdconf() -> bool:
    """Run moode's upd-mpdconf.php to regenerate MPD config from the database."""
    path = "/var/www/util/upd-mpdconf.php"
    if not os.path.exists(path):
        log(f"upd-mpdconf.php not found ({path}), skipping")
        return False
    try:
        result = subprocess.run(
            ["/usr/bin/php", path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            log(f"upd-mpdconf.php failed (exit {result.returncode}): {result.stderr or result.stdout}")
            return False
        log("Regenerated MPD config (upd-mpdconf.php)")
        return True
    except subprocess.TimeoutExpired:
        log("upd-mpdconf.php timed out")
        return False
    except Exception as e:
        log(f"upd-mpdconf.php error: {e}")
        return False


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    syslog.openlog("configure-moode", syslog.LOG_PID, syslog.LOG_DAEMON)
    cfg = _load_config()

    if not cfg.get("enabled", True):
        log("Disabled via config, exiting")
        sys.exit(0)

    boot_config_path = _get_boot_config_path(cfg)

    # Collect all boot config changes in a single pass
    reboot_needed = False
    reboot_needed |= ensure_ssh_enabled()
    reboot_needed |= check_and_enable_uart(boot_config_path)
    reboot_needed |= check_and_disable_serial_console()
    reboot_needed |= check_and_enable_i2c(boot_config_path)
    reboot_needed |= ensure_i2s_overlay(boot_config_path)

    # Ensure moode database has correct audio config (mpdmixer stays "software"
    # in both SW and HW volume modes — see ensure_moode_audio_config docstring).
    db_changed = ensure_moode_audio_config()

    _set_volknob(cfg.get("moode_boot_volume_percent", FALLBACK_MOODE_BOOT_VOLUME_PERCENT))

    run_upd_mpdconf()

    if reboot_needed:
        log("Rebooting to apply boot config changes")
        subprocess.run(["/sbin/reboot"], check=False)
    elif db_changed:
        log("Database updated (moode will read fresh values at startup)")
    else:
        log("All configuration is correct")

    sys.exit(0)


if __name__ == "__main__":
    main()
