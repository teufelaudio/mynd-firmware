#!/usr/bin/env python3
"""
Shared daemon state and configuration helpers.
"""

from __future__ import annotations

import asyncio
import configparser
import logging
import logging.handlers
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Optional

from actionslink_adapter import system_pb
from command_runner import AsyncCommandRunner


POWER_STATE_OFF = system_pb.PowerState.SystemPowerMode.OFF
POWER_STATE_ON = system_pb.PowerState.SystemPowerMode.ON
POWER_STATE_STANDBY = system_pb.PowerState.SystemPowerMode.STANDBY
POWER_STATE_SHUTDOWN_REQUEST = system_pb.PowerState.SystemPowerMode.SHUTDOWN_REQUEST


def get_enum_name(enum_descriptor, value: int) -> str:
    """Resolve an enum numeric value to its protobuf enum name."""
    try:
        enum_value = enum_descriptor.values_by_number.get(value)
        return enum_value.name if enum_value else f"UNKNOWN({value})"
    except (AttributeError, KeyError):
        return f"UNKNOWN({value})"


@dataclass(slots=True)
class PowerContext:
    mcu_power_state: int = POWER_STATE_OFF
    rpi_power_state: int = POWER_STATE_OFF
    initialized: bool = False
    power_state_event: Optional[asyncio.Event] = None
    transition_lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    shutdown_prep_task: Optional[asyncio.Task] = None
    poweroff_task: Optional[asyncio.Task] = None


@dataclass(slots=True)
class PlaybackContext:
    last_streaming_active: Optional[bool] = None
    last_host_source: Optional[int] = None
    streaming_monitor_task: Optional[asyncio.Task] = None
    last_mpd_fail_log_time: float = 0.0
    mpd_fail_log_interval: float = 60.0
    last_moode_api_fail_log_time: float = 0.0
    moode_api_fail_log_interval: float = 60.0


@dataclass(slots=True)
class HardwareContext:
    battery_level: int = 0
    charging_active: bool = False
    battery_friendly_charging: Optional[bool] = None


class MyndRpiDaemon:
    """Composition root for the daemon runtime."""

    def __init__(self, config_path: str):
        self.config = self._load_config(config_path)
        self.logger = self._setup_logging()
        self.command_runner = AsyncCommandRunner()

        self.running = False
        self.moode = None

        self.streaming_poll_interval = float(self.config["moode"]["streaming_poll_interval"])
        self.mpd_port = int(self.config["moode"]["mpd_port"])

        self.power_state = PowerContext()
        self.playback_state = PlaybackContext()
        self.hardware_state = HardwareContext()
        self.last_seq: Dict[str, int] = {}

        self.bluetooth = None
        self.wifi = None
        self.playback = None
        self.power = None

    def _load_config(self, config_path: str) -> Dict[str, Any]:
        config = configparser.ConfigParser()
        config.read(config_path)

        def get(section: str, key: str, fallback: str) -> str:
            if config.has_section(section):
                return config.get(section, key, fallback=fallback)
            return fallback

        def get_int(section: str, key: str, fallback: int) -> int:
            try:
                return int(get(section, key, str(fallback)))
            except (TypeError, ValueError):
                return fallback

        return {
            "configure_moode": {
                "bt_sw_volume_step": max(1, min(10, get_int("configure_moode", "bt_sw_volume_step", 2))),
            },
            "uart": {
                "device": get("uart", "device", "/dev/serial0"),
                "baudrate": get("uart", "baudrate", "115200"),
                "timeout": get("uart", "timeout", "0.2"),
            },
            "moode": {
                "base_url": get("moode", "base_url", "http://localhost"),
                "api_timeout": get("moode", "api_timeout", "5.0"),
                "retry_count": get("moode", "retry_count", "3"),
                "streaming_poll_interval": get("moode", "streaming_poll_interval", "2.0"),
                "mpd_port": get("moode", "mpd_port", "6600"),
            },
            "power": {
                "poweroff_delay": get("power", "poweroff_delay", "0.0"),
                "poweroff_command": get("power", "poweroff_command", "sudo poweroff"),
            },
            "hotspot": {
                "ssid": get("hotspot", "ssid", ""),
                "password": get("hotspot", "password", ""),
                "interface": get("hotspot", "interface", "wlan0"),
                "connection_name": get("hotspot", "connection_name", ""),
            },
            "bt_wifi_coexistence": {
                "disable_wifi_when_bt_active": get(
                    "bt_wifi_coexistence",
                    "disable_wifi_when_bt_active",
                    "false",
                ).lower()
                == "true",
                "disable_only_while_streaming": get(
                    "bt_wifi_coexistence",
                    "disable_only_while_streaming",
                    "true",
                ).lower()
                == "true",
                "restore_previous_connection": get(
                    "bt_wifi_coexistence",
                    "restore_previous_connection",
                    "true",
                ).lower()
                == "true",
            },
            "logging": {
                "level": get("logging", "level", "INFO"),
                "use_syslog": get("logging", "use_syslog", "true"),
            },
        }

    def _setup_logging(self) -> logging.Logger:
        logger_obj = logging.getLogger("mynd_rpi")
        logger_obj.handlers.clear()

        use_syslog = str(self.config["logging"]["use_syslog"]).lower() == "true"
        level_str = str(self.config["logging"]["level"]).upper()
        level = getattr(logging, level_str, logging.INFO)

        logger_obj.setLevel(level)
        formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s")

        if use_syslog:
            try:
                handler = logging.handlers.SysLogHandler(address="/dev/log")
                handler.setFormatter(logging.Formatter("mynd_rpi[%(process)d]: %(message)s"))
            except Exception:
                handler = logging.StreamHandler()
                handler.setFormatter(formatter)
        else:
            handler = logging.StreamHandler()
            handler.setFormatter(formatter)

        logger_obj.addHandler(handler)
        return logger_obj

    def validate_serial_device(self, device_path: str) -> tuple[bool, Optional[str], str]:
        """Validate serial device path and suggest alternatives if not found."""
        if os.path.exists(device_path):
            if os.path.islink(device_path):
                actual_device = os.path.realpath(device_path)
                if os.path.exists(actual_device):
                    return True, actual_device, ""
                return (
                    False,
                    None,
                    f"Symlink {device_path} points to non-existent device: {actual_device}",
                )
            return True, device_path, ""

        alternatives: list[str] = []
        common_paths = ["/dev/ttyS0", "/dev/ttyAMA0", "/dev/ttyUSB0", "/dev/ttyACM0"]

        for alt_path in common_paths:
            if os.path.exists(alt_path):
                alternatives.append(alt_path)

        for serial_dir in (Path("/dev/serial/by-id"), Path("/dev/serial/by-path")):
            if not serial_dir.exists():
                continue
            try:
                for link in serial_dir.iterdir():
                    if link.is_symlink():
                        alternatives.append(str(link))
            except Exception:
                pass

        error_msg = f"Serial device '{device_path}' not found."
        if alternatives:
            error_msg += f" Found alternative devices: {', '.join(alternatives[:3])}"
            error_msg += (
                "\n  Update configuration file to use one of these devices, "
                "or enable UART in /boot/config.txt"
            )
        else:
            error_msg += " No serial devices found."
            error_msg += (
                "\n  Make sure UART is enabled: add 'enable_uart=1' to /boot/config.txt and reboot"
            )

        return False, None, error_msg

    def stop(self):
        self.running = False
