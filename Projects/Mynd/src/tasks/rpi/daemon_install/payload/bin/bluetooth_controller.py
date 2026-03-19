#!/usr/bin/env python3
"""
BlueZ integration for renderer control and playback state monitoring.
"""

from __future__ import annotations

import asyncio
import subprocess
import threading
from typing import Any, Optional, Tuple


def _run_bluealsa_cli(args, timeout: int = 3) -> subprocess.CompletedProcess:
    """Run bluealsa-cli command with consistent subprocess options."""
    return subprocess.run(
        ["bluealsa-cli", *args],
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def _parse_bluealsa_volume(stdout: str) -> Optional[Tuple[int, int]]:
    """Parse `bluealsa-cli volume` output (e.g. 'Volume: L: 7 R: 7')."""
    tokens = (stdout or "").replace(":", " ").split()
    if "L" not in tokens or "R" not in tokens:
        return None
    try:
        l_idx = tokens.index("L") + 1
        r_idx = tokens.index("R") + 1
        return int(tokens[l_idx]), int(tokens[r_idx])
    except (ValueError, IndexError):
        return None


class BluetoothController:
    def __init__(self, logger, config):
        self.logger = logger
        self.config = config
        self.playing_signal_state: Optional[bool] = None
        self.last_volume_boundary: Optional[str] = None
        self._bluez_player_path: Optional[str] = None
        self._signal_thread: Optional[threading.Thread] = None
        self._glib_loop = None

    def _set_bt_playing_signal_state(self, status_value: Any):
        """Map BlueZ MediaPlayer1 Status -> cached bool state."""
        status = str(status_value).strip().lower()
        self.playing_signal_state = status == "playing"

    def _refresh_bt_state_from_managed_objects(self, system_bus):
        """Prime BT playback cache from current BlueZ objects at listener startup."""
        media_player_interface = "org.bluez.MediaPlayer1"
        try:
            dbus = __import__("dbus")
            object_manager = dbus.Interface(
                system_bus.get_object("org.bluez", "/"),
                "org.freedesktop.DBus.ObjectManager",
            )
            managed_objects = object_manager.GetManagedObjects()
            fallback_status = None
            fallback_path = None

            for path, interfaces in managed_objects.items():
                player_props = interfaces.get(media_player_interface)
                if not player_props:
                    continue

                status_value = player_props.get("Status")
                if status_value is None:
                    continue

                status_str = str(status_value).strip().lower()
                if status_str == "playing":
                    self._bluez_player_path = str(path)
                    self._set_bt_playing_signal_state(status_value)
                    return

                if fallback_status is None:
                    fallback_status = status_value
                    fallback_path = str(path)

            if fallback_status is not None:
                self._bluez_player_path = fallback_path
                self._set_bt_playing_signal_state(fallback_status)
        except Exception as e:
            self.logger.debug("BlueZ initial state load failed: %s", e)

    def _toggle_bt_playback_sync(self) -> bool:
        """Toggle Bluetooth playback via BlueZ MediaPlayer1 using cached player path.

        Uses Pause() when current status is playing, otherwise Play(). If the cached
        path is stale or missing, refreshes from managed objects and retries once.
        """
        media_player_interface = "org.bluez.MediaPlayer1"
        try:
            dbus = __import__("dbus")
            system_bus = dbus.SystemBus()
        except Exception as e:
            self.logger.warning("BlueZ control unavailable: %s", e)
            return False

        for _attempt in range(2):
            player_path = self._bluez_player_path
            if not player_path:
                self._refresh_bt_state_from_managed_objects(system_bus)
                player_path = self._bluez_player_path
            if not player_path:
                continue

            try:
                player_obj = system_bus.get_object("org.bluez", player_path)
                props = dbus.Interface(player_obj, "org.freedesktop.DBus.Properties")
                status_value = props.Get(media_player_interface, "Status")
                status = str(status_value).strip().lower()
                player = dbus.Interface(player_obj, media_player_interface)
            except Exception as e:
                self.logger.debug("BlueZ player path refresh needed (%s): %s", player_path, e)
                self._bluez_player_path = None
                self._refresh_bt_state_from_managed_objects(system_bus)
                continue

            try:
                if status == "playing":
                    player.Pause()
                    self.playing_signal_state = False
                    self.logger.info("Play/Pause: sent BlueZ Pause to %s", player_path)
                else:
                    player.Play()
                    self.playing_signal_state = True
                    self.logger.info("Play/Pause: sent BlueZ Play to %s (status was %s)", player_path, status or "unknown")
                return True
            except Exception as e:
                self.logger.warning("BlueZ play/pause command failed for %s: %s", player_path, e)
                return False

        self.logger.warning("BlueZ play/pause failed: no active MediaPlayer1 object")
        return False

    async def toggle_bt_playback(self) -> bool:
        """Async wrapper for BlueZ Bluetooth playback toggle."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, self._toggle_bt_playback_sync)

    def _send_bt_volume_press_sync(self, is_volume_up: bool) -> bool:
        """Send Bluetooth volume step via BlueALSA PCM volume."""
        self.last_volume_boundary = None
        volume_step = self.config["configure_moode"]["bt_sw_volume_step"]  # BlueALSA range: 0..127
        try:
            pcm_result = _run_bluealsa_cli(["list-pcms"], timeout=3)
            if pcm_result.returncode != 0:
                self.logger.warning("BlueALSA list-pcms failed: %s", pcm_result.stderr.strip())
                return False

            pcm_paths = [line.strip() for line in pcm_result.stdout.splitlines() if line.strip()]
            if not pcm_paths:
                self.logger.warning("BlueALSA volume command failed: no PCM paths")
                return False

            pcm_path = next((p for p in pcm_paths if "/a2dpsnk/" in p), pcm_paths[0])

            read_result = _run_bluealsa_cli(["volume", pcm_path], timeout=3)
            if read_result.returncode != 0:
                self.logger.warning("BlueALSA read volume failed for %s: %s", pcm_path, read_result.stderr.strip())
                return False

            parsed = _parse_bluealsa_volume(read_result.stdout)
            if parsed is None:
                self.logger.warning("BlueALSA read volume parse failed for %s: %s", pcm_path, read_result.stdout.strip())
                return False

            current_l, current_r = parsed
            delta = volume_step if is_volume_up else -volume_step
            target_l = max(0, min(127, current_l + delta))
            target_r = max(0, min(127, current_r + delta))
            if target_l >= 127 and target_r >= 127:
                self.last_volume_boundary = "max"
            elif target_l <= 0 and target_r <= 0:
                self.last_volume_boundary = "min"

            if target_l == current_l and target_r == current_r:
                self.logger.info("Volume: BlueALSA already at limit (%s/%s) on %s", current_l, current_r, pcm_path)
                return True

            write_result = _run_bluealsa_cli(["volume", pcm_path, str(target_l), str(target_r)], timeout=3)
            if write_result.returncode != 0:
                self.logger.warning("BlueALSA set volume failed for %s: %s", pcm_path, write_result.stderr.strip())
                return False

            self.logger.info(
                "Volume: set BlueALSA volume %s/%s -> %s/%s on %s",
                current_l, current_r, target_l, target_r, pcm_path
            )
            return True

        except Exception as e:
            self.logger.warning("BlueALSA volume command failed: %s", e)
            return False

    async def send_bt_volume_press(self, is_volume_up: bool) -> bool:
        """Async wrapper for BlueALSA Bluetooth volume step."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, self._send_bt_volume_press_sync, is_volume_up)

    def _send_bt_track_command_sync(self, command_name: str) -> bool:
        """Send Bluetooth track navigation command via BlueZ MediaPlayer1."""
        media_player_interface = "org.bluez.MediaPlayer1"
        if command_name not in ("Next", "Previous"):
            self.logger.warning("Invalid BlueZ track command: %s", command_name)
            return False
        try:
            dbus = __import__("dbus")
            system_bus = dbus.SystemBus()
        except Exception as e:
            self.logger.warning("BlueZ track control unavailable: %s", e)
            return False

        for _attempt in range(2):
            player_path = self._bluez_player_path
            if not player_path:
                self._refresh_bt_state_from_managed_objects(system_bus)
                player_path = self._bluez_player_path
            if not player_path:
                continue

            try:
                player_obj = system_bus.get_object("org.bluez", player_path)
                player = dbus.Interface(player_obj, media_player_interface)
                getattr(player, command_name)()
                self.logger.info("Track: sent BlueZ %s to %s", command_name, player_path)
                return True
            except Exception as e:
                self.logger.debug("BlueZ track path refresh needed (%s): %s", player_path, e)
                self._bluez_player_path = None
                self._refresh_bt_state_from_managed_objects(system_bus)

        self.logger.warning("BlueZ track command failed: no active MediaPlayer1 object")
        return False

    async def send_bt_next_track(self) -> bool:
        """Async wrapper for BlueZ Bluetooth next-track command."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, lambda: self._send_bt_track_command_sync("Next"))

    async def send_bt_previous_track(self) -> bool:
        """Async wrapper for BlueZ Bluetooth previous-track command."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, lambda: self._send_bt_track_command_sync("Previous"))

    def _run_bluez_signal_listener(self):
        try:
            dbus = __import__("dbus")
            __import__("dbus.mainloop.glib")
            GLib = __import__("gi.repository", fromlist=["GLib"]).GLib
        except Exception as e:
            self.logger.error("BlueZ signal listener unavailable: %s", e)
            return

        media_player_interface = "org.bluez.MediaPlayer1"

        try:
            dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
            system_bus = dbus.SystemBus()

            def _on_properties_changed(interface_name, changed_props, _invalidated_props, path=None):
                if interface_name != media_player_interface:
                    return
                status_value = changed_props.get("Status")
                if status_value is None:
                    return
                if path:
                    self._bluez_player_path = str(path)
                self._set_bt_playing_signal_state(status_value)

            def _on_interfaces_added(path, interfaces):
                player_props = interfaces.get(media_player_interface)
                if not player_props:
                    return
                self._bluez_player_path = str(path)
                status_value = player_props.get("Status")
                if status_value is not None:
                    self._set_bt_playing_signal_state(status_value)

            def _on_interfaces_removed(path, interfaces):
                removed_interfaces = {str(name) for name in interfaces}
                if media_player_interface in removed_interfaces:
                    if self._bluez_player_path is None or self._bluez_player_path == str(path):
                        self._bluez_player_path = None
                        self.playing_signal_state = False

            system_bus.add_signal_receiver(
                _on_properties_changed,
                dbus_interface="org.freedesktop.DBus.Properties",
                signal_name="PropertiesChanged",
                path_keyword="path",
            )
            system_bus.add_signal_receiver(
                _on_interfaces_added,
                dbus_interface="org.freedesktop.DBus.ObjectManager",
                signal_name="InterfacesAdded",
            )
            system_bus.add_signal_receiver(
                _on_interfaces_removed,
                dbus_interface="org.freedesktop.DBus.ObjectManager",
                signal_name="InterfacesRemoved",
            )

            self._refresh_bt_state_from_managed_objects(system_bus)
            self._glib_loop = GLib.MainLoop()
            self._glib_loop.run()
        except Exception as e:
            self.logger.error("BlueZ signal listener failed: %s", e)
        finally:
            self._glib_loop = None

    def _start_bluez_signal_listener(self):
        """Start BlueZ signal listener once per monitor lifecycle."""
        if self._signal_thread and self._signal_thread.is_alive():
            return
        self.playing_signal_state = None
        self._bluez_player_path = None
        self._signal_thread = threading.Thread(
            target=self._run_bluez_signal_listener,
            name="bluez-signal-listener",
            daemon=True,
        )
        self._signal_thread.start()

    def _stop_bluez_signal_listener(self):
        """Stop BlueZ signal listener thread."""
        thread = self._signal_thread
        loop = self._glib_loop

        if loop is not None:
            try:
                GLib = __import__("gi.repository", fromlist=["GLib"]).GLib
                GLib.idle_add(loop.quit)
            except Exception as e:
                self.logger.debug("BlueZ listener stop signal failed: %s", e)

        if thread and thread.is_alive():
            thread.join(timeout=2.0)

        self._signal_thread = None
        self._glib_loop = None
        self._bluez_player_path = None
        self.playing_signal_state = None

    async def toggle_playback(self) -> bool:
        return await self.toggle_bt_playback()

    async def send_volume_press(self, is_volume_up: bool) -> bool:
        return await self.send_bt_volume_press(is_volume_up)

    async def send_next_track(self) -> bool:
        return await self.send_bt_next_track()

    async def send_previous_track(self) -> bool:
        return await self.send_bt_previous_track()

    def start_listener(self):
        self._start_bluez_signal_listener()

    def stop_listener(self):
        self._stop_bluez_signal_listener()
