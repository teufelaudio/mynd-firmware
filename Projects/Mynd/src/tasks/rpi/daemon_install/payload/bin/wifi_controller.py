#!/usr/bin/env python3
"""
NetworkManager and hotspot workflows for the RPi daemon.
"""

from __future__ import annotations

import asyncio
import pwd
import socket
import subprocess
from collections import deque
from dataclasses import dataclass
from typing import Optional

from actionslink_adapter import error_pb, host_pb
from command_runner import with_optional_sudo


@dataclass(slots=True)
class WifiActionResult:
    success: bool
    status_code: int
    ssid: str = ""
    ip_address: str = "unknown"
    username: str = "unknown"
    target_reached: bool = True
    detail: int = host_pb.WiFiCommandResult.Detail.DETAIL_NONE


class WifiController:
    def __init__(self, daemon):
        self.daemon = daemon
        self.logger = daemon.logger
        self.runner = daemon.command_runner
        self.operation_lock = asyncio.Lock()
        self.bt_wifi_last_active: Optional[bool] = None
        self.bt_wifi_radio_forced_off = False
        self.bt_wifi_restore_connections: list[str] = []
        self.wifi_action_override_active = False
        self.state_lock = asyncio.Lock()
        self.active_command_id: Optional[int] = None
        self.active_command_name: Optional[str] = None
        self.completed_command_ids: deque[int] = deque()
        self.completed_command_id_set: set[int] = set()

    async def begin_command(self, command_id: int, action_name: str) -> str:
        async with self.state_lock:
            if command_id == self.active_command_id or command_id in self.completed_command_id_set:
                return "duplicate"
            if self.active_command_id is not None:
                return "busy"
            self.active_command_id = command_id
            self.active_command_name = action_name
            return "started"

    async def finish_command(self, command_id: int) -> None:
        async with self.state_lock:
            if self.active_command_id == command_id:
                self.active_command_id = None
                self.active_command_name = None

            if command_id in self.completed_command_id_set:
                return

            if len(self.completed_command_ids) >= 32:
                expired_command_id = self.completed_command_ids.popleft()
                self.completed_command_id_set.discard(expired_command_id)

            self.completed_command_ids.append(command_id)
            self.completed_command_id_set.add(command_id)

    async def configure_wifi(self, ssid: str, password: str) -> WifiActionResult:
        async with self.operation_lock:
            try:
                if not ssid:
                    self.logger.error("SSID cannot be empty")
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                if not await self.prepare_wifi_action("configure_wifi"):
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                rescan = await self.run_nmcli(["device", "wifi", "rescan"], timeout=10, use_sudo=True)
                if rescan.returncode != 0:
                    self.logger.warning("WiFi rescan failed: %s", rescan.stderr or rescan.stdout)
                await asyncio.sleep(3)

                list_result = await self.run_nmcli(["-g", "NAME", "connection", "show"], timeout=5)
                if list_result.returncode == 0 and ssid in (list_result.stdout or "").splitlines():
                    up_result = await self.run_nmcli(["connection", "up", ssid], timeout=15, use_sudo=True)
                    if up_result.returncode == 0:
                        self.logger.info("WiFi network activated (existing): %s", ssid)
                        return WifiActionResult(
                            True,
                            error_pb.Code.Success,
                            ssid=ssid,
                            ip_address=await self.get_interface_ipv4_address(
                                self.daemon.config["hotspot"]["interface"]
                            ),
                            username=self.get_service_username(),
                        )

                if not password:
                    self.logger.error("Password required to connect to unknown WiFi network: %s", ssid)
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                connect_result = await self.run_nmcli(
                    ["device", "wifi", "connect", ssid, "password", password],
                    timeout=30,
                    use_sudo=True,
                )
                if connect_result.returncode != 0:
                    self.logger.error("nmcli failed: %s", connect_result.stderr or connect_result.stdout)
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                self.logger.debug("WiFi network configured successfully: %s", ssid)
                return WifiActionResult(
                    True,
                    error_pb.Code.Success,
                    ssid=ssid,
                    ip_address=await self.get_interface_ipv4_address(
                        self.daemon.config["hotspot"]["interface"]
                    ),
                    username=self.get_service_username(),
                )
            except subprocess.TimeoutExpired:
                self.logger.error("nmcli timed out")
                return WifiActionResult(False, error_pb.Code.OperationFailed)
            except Exception as exc:
                self.logger.error("Error during WiFi configuration: %s", exc, exc_info=True)
                return WifiActionResult(False, error_pb.Code.OperationFailed)

    async def enable_hotspot(self) -> WifiActionResult:
        async with self.operation_lock:
            try:
                interface = self.daemon.config["hotspot"]["interface"]
                connection_name = self.hotspot_connection_name()
                ssid, password = self.custom_hotspot_settings()

                if not await self.prepare_wifi_action("enable_hotspot"):
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                if ssid or password:
                    if not ssid or not password:
                        self.logger.error(
                            "Custom hotspot configuration is incomplete: both ssid and password must be set."
                        )
                        return WifiActionResult(False, error_pb.Code.OperationFailed)

                    self.logger.info(
                        "Enabling configured hotspot (con_name=%s, ssid=%s, ifname=%s)",
                        connection_name,
                        ssid,
                        interface,
                    )
                    result = await self.run_nmcli(
                        [
                            "device",
                            "wifi",
                            "hotspot",
                            "con-name",
                            connection_name,
                            "ifname",
                            interface,
                            "ssid",
                            ssid,
                            "password",
                            password,
                        ],
                        timeout=30,
                        use_sudo=True,
                    )
                else:
                    self.logger.info(
                        "Enabling Moode hotspot (con_name=%s, ifname=%s)",
                        connection_name,
                        interface,
                    )
                    exists_result = await self.run_nmcli(["connection", "show", connection_name], timeout=5)
                    if exists_result.returncode != 0:
                        self.logger.error(
                            "Moode hotspot not configured: connection '%s' not found; configure SSID/password in Moode web UI first.",
                            connection_name,
                        )
                        return WifiActionResult(False, error_pb.Code.ResourceUnavailable)

                    result = await self.run_nmcli(
                        ["connection", "up", connection_name, "ifname", interface],
                        timeout=30,
                        use_sudo=True,
                    )
                    if result.returncode != 0:
                        error_text = (result.stderr or result.stdout or "").strip()
                        if "Secrets were required, but not provided" in error_text:
                            self.logger.error(
                                "Moode hotspot connection '%s' exists but has no usable hotspot password secret; set the hotspot password in Moode web UI first.",
                                connection_name,
                            )
                            return WifiActionResult(False, error_pb.Code.ResourceUnavailable)

                if result.returncode != 0:
                    self.logger.error("nmcli hotspot failed: %s", result.stderr or result.stdout)
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                if not ssid:
                    ssid = await self.get_connection_ssid(connection_name)

                self.logger.info("WiFi hotspot enabled: %s", ssid)
                hotspot_ip = await self.get_interface_ipv4_address(interface)
                if hotspot_ip == "unknown":
                    self.logger.warning("Hotspot enabled but AP IPv4 for %s is not yet available", interface)

                return WifiActionResult(
                    True,
                    error_pb.Code.Success,
                    ssid=ssid,
                    ip_address=hotspot_ip,
                    username=self.get_service_username(),
                )
            except subprocess.TimeoutExpired:
                self.logger.error("nmcli hotspot timed out")
                return WifiActionResult(False, error_pb.Code.OperationFailed)
            except Exception as exc:
                self.logger.error("Error enabling hotspot: %s", exc, exc_info=True)
                return WifiActionResult(False, error_pb.Code.OperationFailed)

    async def cycle_wifi_network(self) -> WifiActionResult:
        async with self.operation_lock:
            try:
                if not await self.prepare_wifi_action("cycle_wifi_network"):
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                hotspot_connection_name = self.hotspot_connection_name()
                hotspot_active = hotspot_connection_name in await self.active_wifi_connections(
                    include_hotspot=True
                )

                if hotspot_active:
                    self.logger.info("Hotspot is active, deactivating before cycling WiFi")
                    try:
                        await self.run_nmcli(
                            ["connection", "down", hotspot_connection_name],
                            timeout=10,
                            use_sudo=True,
                        )
                    except Exception as exc:
                        self.logger.warning("Failed to deactivate hotspot: %s", exc)

                known_wifi = await self.known_wifi_connections()
                if not known_wifi:
                    self.logger.error("No known WiFi networks found to cycle to")
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                current_wifi = await self.get_active_wifi_connection_name()
                if current_wifi and current_wifi in known_wifi:
                    idx = known_wifi.index(current_wifi)
                    next_wifi = known_wifi[(idx + 1) % len(known_wifi)]
                else:
                    next_wifi = known_wifi[0]

                self.logger.info("Cycling WiFi: %s -> %s", current_wifi or "(none)", next_wifi)
                connect_result = await self.run_nmcli(
                    ["connection", "up", next_wifi],
                    timeout=30,
                    use_sudo=True,
                )
                activated, active_wifi, connectivity = await self.wait_for_wifi_activation(next_wifi)
                success = active_wifi is not None and connectivity == "full"
                if not success:
                    self.logger.error(
                        "WiFi cycle failed: no active internet WiFi (target=%s, active=%s, connectivity=%s, nmcli_rc=%s, nmcli_err=%s)",
                        next_wifi,
                        active_wifi or "(none)",
                        connectivity,
                        connect_result.returncode,
                        (connect_result.stderr or connect_result.stdout).strip(),
                    )
                    return WifiActionResult(False, error_pb.Code.OperationFailed)

                self.logger.info(
                    "WiFi cycle succeeded with active internet WiFi: %s (connectivity=%s, requested_target=%s, nmcli_rc=%s%s)",
                    active_wifi,
                    connectivity,
                    next_wifi,
                    connect_result.returncode,
                    ", target_reached=true" if activated else ", target_reached=false",
                )
                return WifiActionResult(
                    True,
                    error_pb.Code.Success,
                    ssid=active_wifi or next_wifi,
                    ip_address=await self.get_interface_ipv4_address(
                        self.daemon.config["hotspot"]["interface"]
                    ),
                    username=self.get_service_username(),
                    target_reached=activated,
                )
            except subprocess.TimeoutExpired:
                self.logger.error("nmcli cycle wifi timed out")
                return WifiActionResult(False, error_pb.Code.OperationFailed)
            except Exception as exc:
                self.logger.error("Error cycling WiFi network: %s", exc, exc_info=True)
                return WifiActionResult(False, error_pb.Code.OperationFailed)

    async def apply_bt_wifi_policy(self, host_source: int, is_streaming: Optional[bool]) -> None:
        cfg = self.daemon.config["bt_wifi_coexistence"]
        if not cfg["disable_wifi_when_bt_active"]:
            return

        bt_active = host_source == host_pb.SOURCE_BLUETOOTH
        if cfg["disable_only_while_streaming"]:
            bt_active = bt_active and bool(is_streaming)

        if self.wifi_action_override_active:
            if bt_active:
                self.bt_wifi_last_active = True
                self.logger.debug(
                    "Skipping BT/WiFi coexistence disable while explicit WiFi action override is active"
                )
                return
            self.logger.info("Clearing explicit WiFi override after Bluetooth activity ended")
            self.wifi_action_override_active = False

        if self.bt_wifi_last_active is bt_active:
            return
        self.bt_wifi_last_active = bt_active

        if bt_active:
            async with self.operation_lock:
                self.bt_wifi_restore_connections = await self.active_wifi_connections(include_hotspot=False)
                changed = await self.set_wifi_radio_state(False)
                if changed:
                    self.bt_wifi_radio_forced_off = True
            return

        if not self.bt_wifi_radio_forced_off:
            return

        async with self.operation_lock:
            changed = await self.set_wifi_radio_state(True)
            if not changed:
                return

            self.bt_wifi_radio_forced_off = False
            restore_previous = cfg["restore_previous_connection"]
            restore_targets = list(self.bt_wifi_restore_connections)
            self.bt_wifi_restore_connections = []

            if not restore_previous or not restore_targets:
                return

            for conn_name in restore_targets:
                result = await self.run_nmcli(["connection", "up", conn_name], timeout=15, use_sudo=True)
                if result.returncode == 0:
                    self.logger.info("Restored WiFi connection after BT session: %s", conn_name)
                    return
                self.logger.debug(
                    "Failed to restore WiFi connection '%s' (rc=%s): %s",
                    conn_name,
                    result.returncode,
                    (result.stderr or result.stdout or "").strip(),
                )

    async def set_wifi_radio_state(self, enabled: bool) -> bool:
        state_arg = "on" if enabled else "off"
        try:
            result = await self.run_nmcli(["radio", "wifi", state_arg], timeout=10, use_sudo=True)
        except Exception as exc:
            self.logger.warning("Failed to toggle WiFi radio %s: %s", state_arg, exc)
            return False

        if result.returncode != 0:
            self.logger.warning(
                "nmcli radio wifi %s failed (rc=%s, stderr=%s)",
                state_arg,
                result.returncode,
                (result.stderr or "").strip(),
            )
            return False

        self.logger.info("WiFi radio forced %s", state_arg)
        return True

    async def prepare_wifi_action(self, action_name: str) -> bool:
        self.wifi_action_override_active = True
        if not await self.set_wifi_radio_state(True):
            self.logger.error("Cannot run %s: failed to enable WiFi radio", action_name)
            return False

        if self.bt_wifi_radio_forced_off:
            self.logger.info("WiFi radio restored for explicit WiFi action: %s", action_name)
        self.bt_wifi_radio_forced_off = False
        return True

    async def restore_radio_after_stop(self):
        self.bt_wifi_last_active = None
        self.wifi_action_override_active = False
        if self.bt_wifi_radio_forced_off:
            self.logger.info("Restoring WiFi radio on streaming monitor stop")
            if await self.set_wifi_radio_state(True):
                self.bt_wifi_radio_forced_off = False
        self.bt_wifi_restore_connections = []

    async def active_wifi_connections(self, include_hotspot: bool = False) -> list[str]:
        hotspot_connection_name = self.hotspot_connection_name()
        try:
            result = await self.run_nmcli(
                ["-t", "-f", "NAME,TYPE", "connection", "show", "--active"],
                timeout=5,
            )
            names = []
            for name, conn_type in self.parse_name_type_rows(result.stdout):
                if conn_type != "802-11-wireless":
                    continue
                if not include_hotspot and name == hotspot_connection_name:
                    continue
                names.append(name)
            return names
        except Exception as exc:
            self.logger.debug("Could not query active WiFi connections: %s", exc)
            return []

    async def known_wifi_connections(self) -> list[str]:
        hotspot_connection_name = self.hotspot_connection_name()
        try:
            result = await self.run_nmcli(["-t", "-f", "NAME,TYPE", "connection", "show"], timeout=5)
            known_wifi = []
            for name, conn_type in self.parse_name_type_rows(result.stdout):
                if conn_type == "802-11-wireless" and name != hotspot_connection_name:
                    known_wifi.append(name)
            return known_wifi
        except Exception as exc:
            self.logger.error("Failed to list known WiFi connections: %s", exc)
            return []

    async def get_active_wifi_connection_name(self) -> Optional[str]:
        active_wifi = await self.active_wifi_connections(include_hotspot=False)
        return active_wifi[0] if active_wifi else None

    async def get_nm_connectivity_state(self) -> str:
        try:
            result = await self.run_nmcli(["-t", "-f", "CONNECTIVITY", "general"], timeout=5)
            state = (result.stdout or "").strip().lower()
            return state or "unknown"
        except Exception as exc:
            self.logger.debug("Could not query connectivity state: %s", exc)
            return "unknown"

    async def wait_for_wifi_activation(
        self,
        expected_ssid: str,
        timeout_s: float = 8.0,
        poll_interval_s: float = 1.0,
    ) -> tuple[bool, Optional[str], str]:
        deadline = asyncio.get_running_loop().time() + timeout_s
        last_active: Optional[str] = None
        last_connectivity = "unknown"
        while asyncio.get_running_loop().time() < deadline:
            last_active = await self.get_active_wifi_connection_name()
            last_connectivity = await self.get_nm_connectivity_state()
            if last_active == expected_ssid:
                return True, last_active, last_connectivity
            await asyncio.sleep(poll_interval_s)
        return False, last_active, last_connectivity

    async def get_connection_ssid(self, connection_name: str) -> str:
        try:
            result = await self.run_nmcli(
                ["-g", "802-11-wireless.ssid", "connection", "show", connection_name],
                timeout=5,
            )
            ssid = (result.stdout or "").strip()
            if result.returncode == 0 and ssid:
                return ssid
        except Exception as exc:
            self.logger.debug("Could not query SSID for connection %s: %s", connection_name, exc)
        return connection_name

    async def get_interface_ipv4_address(self, interface: str) -> str:
        try:
            result = await self.run_nmcli(["-g", "IP4.ADDRESS", "device", "show", interface], timeout=5)
            if result.returncode != 0:
                self.logger.debug(
                    "Could not query IPv4 for interface %s (rc=%s, stderr=%s)",
                    interface,
                    result.returncode,
                    (result.stderr or "").strip(),
                )
                return "unknown"

            for line in (result.stdout or "").splitlines():
                entry = line.strip()
                if not entry:
                    continue
                return entry.split("/", 1)[0].strip() or "unknown"
            return "unknown"
        except Exception as exc:
            self.logger.debug("Could not query IPv4 for interface %s: %s", interface, exc)
            return "unknown"

    def get_service_username(self) -> str:
        try:
            return pwd.getpwuid(__import__("os").geteuid()).pw_name
        except Exception as exc:
            self.logger.debug("Could not resolve daemon username: %s", exc)
            return __import__("os").environ.get("USER", "unknown")

    def custom_hotspot_settings(self) -> tuple[str, str]:
        ssid = (self.daemon.config["hotspot"].get("ssid") or "").strip()
        password = (self.daemon.config["hotspot"].get("password") or "").strip()
        return ssid, password

    def hotspot_connection_name(self) -> str:
        configured_name = (self.daemon.config["hotspot"].get("connection_name") or "").strip()
        if configured_name:
            return configured_name

        ssid, password = self.custom_hotspot_settings()
        if ssid or password:
            return "Hotspot"

        hostname = (socket.gethostname() or "").strip()
        if hostname:
            return hostname[:1].upper() + hostname[1:]
        return "Moode"

    async def run_nmcli(self, nmcli_args, timeout: int = 5, use_sudo: bool = False):
        command = with_optional_sudo(["nmcli", *nmcli_args], use_sudo=use_sudo)
        return await self.runner.run(command, timeout=timeout)

    async def run_moode_util(self, util_name: str, util_args=(), timeout: int = 10, use_sudo: bool = False):
        command = with_optional_sudo([f"/var/www/util/{util_name}", *util_args], use_sudo=use_sudo)
        return await self.runner.run(command, timeout=timeout)

    @staticmethod
    def parse_name_type_rows(stdout: str) -> list[tuple[str, str]]:
        rows: list[tuple[str, str]] = []
        for line in (stdout or "").strip().splitlines():
            if ":" not in line:
                continue
            name, conn_type = line.split(":", 1)
            rows.append((name, conn_type))
        return rows
