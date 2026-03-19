#!/usr/bin/env python3
"""
Playback routing, source detection, and streaming monitor logic.
"""

from __future__ import annotations

import asyncio
import sqlite3
import subprocess
from dataclasses import dataclass
from typing import Optional

from actionslink_adapter import audio_pb, error_pb, host_pb
from daemon_context import get_enum_name
from link_protocol import send_notify_host_source, send_streaming_active_state
from mpd_client import MpdClient


@dataclass(slots=True)
class VolumeActionResult:
    success: bool
    status_code: int
    boundary: Optional[str] = None


class PlaybackController:
    def __init__(self, daemon, bluetooth, wifi):
        self.daemon = daemon
        self.logger = daemon.logger
        self.bluetooth = bluetooth
        self.wifi = wifi
        self.mpd = MpdClient(self.logger, daemon.mpd_port)

    async def start_monitor(self, client):
        task = self.daemon.playback_state.streaming_monitor_task
        if task and not task.done():
            return
        self.bluetooth.start_listener()
        self.daemon.playback_state.streaming_monitor_task = asyncio.create_task(
            self.streaming_state_loop(client)
        )

    async def stop_monitor(self):
        task = self.daemon.playback_state.streaming_monitor_task
        if task and not task.done():
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        self.bluetooth.stop_listener()
        self.daemon.playback_state.streaming_monitor_task = None
        self.daemon.playback_state.last_streaming_active = None
        self.daemon.playback_state.last_host_source = None
        self.mpd.close()

    async def handle_play_pause_for_active_source(self) -> bool:
        if self.daemon.playback_state.last_host_source == host_pb.SOURCE_BLUETOOTH:
            self.logger.debug("Play/Pause: toggling Bluetooth playback via BlueZ")
            if await self.bluetooth.toggle_playback():
                return True
            self.logger.warning("BlueZ play/pause failed; falling back to Moode toggle")

        if not self.daemon.moode:
            return False
        self.logger.debug("Play/Pause: toggling MPD playback via Moode API")
        return bool(await self.daemon.moode.toggle_playback())

    async def handle_next_track_for_active_source(self) -> bool:
        self.logger.debug("Next track action")
        if self.daemon.playback_state.last_host_source == host_pb.SOURCE_BLUETOOTH:
            if await self.bluetooth.send_next_track():
                return True
            self.logger.warning("BlueZ next-track failed; falling back to Moode next")

        if not self.daemon.moode:
            return False
        return bool(await self.daemon.moode.next_station())

    async def handle_previous_track_for_active_source(self) -> bool:
        self.logger.debug("Previous track action")
        if self.daemon.playback_state.last_host_source == host_pb.SOURCE_BLUETOOTH:
            if await self.bluetooth.send_previous_track():
                return True
            self.logger.warning("BlueZ previous-track failed; falling back to Moode previous")

        if not self.daemon.moode:
            return False
        return bool(await self.daemon.moode.previous_station())

    async def handle_volume_action(self, action: int) -> VolumeActionResult:
        if self.daemon.moode is None:
            self.logger.error("Moode client is not initialized")
            return VolumeActionResult(False, error_pb.Code.OperationFailed)

        active_source = self.daemon.playback_state.last_host_source
        if active_source not in (host_pb.SOURCE_MPD, host_pb.SOURCE_BLUETOOTH):
            self.logger.debug(
                "Ignoring volume press: active renderer is not MPD or Bluetooth (source=%s)",
                active_source,
            )
            return VolumeActionResult(True, error_pb.Code.Success)

        is_volume_up = action == audio_pb.VolumeControl.VolumeControlAction.VOLUME_UP
        is_volume_down = action == audio_pb.VolumeControl.VolumeControlAction.VOLUME_DOWN
        if not (is_volume_up or is_volume_down):
            self.logger.warning("Unknown volume action: %s", action)
            return VolumeActionResult(False, error_pb.Code.OperationFailed)

        if active_source == host_pb.SOURCE_BLUETOOTH:
            success = await self.bluetooth.send_volume_press(is_volume_up=is_volume_up)
            boundary = self.bluetooth.last_volume_boundary if success else None
            return VolumeActionResult(
                success=success,
                status_code=error_pb.Code.Success if success else error_pb.Code.OperationFailed,
                boundary=boundary,
            )

        success = await (
            self.daemon.moode.volume_up() if is_volume_up else self.daemon.moode.volume_down()
        )
        boundary = None
        if success:
            current = await self.daemon.moode.get_volume()
            if current is not None:
                mpd_max = self.daemon.moode.get_volume_mpd_max()
                if is_volume_up and current >= mpd_max:
                    boundary = "max"
                elif is_volume_down and current <= 0:
                    boundary = "min"

        return VolumeActionResult(
            success=bool(success),
            status_code=error_pb.Code.Success if success else error_pb.Code.OperationFailed,
            boundary=boundary,
        )

    async def begin_return_to_mpd(self) -> tuple[bool, bool, int]:
        current_source = self.detect_return_to_mpd_target()
        if current_source in (None, host_pb.SOURCE_UNKNOWN):
            mpd_state = await self.mpd.query_state()
            output_format = None
            if mpd_state is not None and self.daemon.moode:
                try:
                    output_format = await self.daemon.moode.get_output_format()
                except Exception as exc:
                    self.logger.debug("return_to_mpd: initial get_output_format failed: %s", exc)
            current_source = self.determine_host_source(mpd_state, output_format)

        if current_source == host_pb.SOURCE_MPD:
            self.logger.debug("Return-to-MPD requested while already on MPD")
            return True, False, current_source

        source_name = get_enum_name(host_pb.Source.DESCRIPTOR, current_source)
        self.logger.info("Returning host source from %s to MPD", source_name)

        if not await self.stop_active_renderer_for_mpd(current_source):
            return False, False, current_source

        return True, True, current_source

    async def complete_return_to_mpd(self, previous_source: int) -> bool:
        if previous_source == host_pb.SOURCE_MPD:
            return True

        if not await self.wait_for_mpd_return(previous_source):
            return False

        self.daemon.playback_state.last_host_source = host_pb.SOURCE_MPD
        return True

    async def streaming_state_loop(self, client):
        self.logger.info("Streaming monitor task started")
        poll_interval = max(0.3, self.daemon.streaming_poll_interval)
        consecutive_failures = 0

        while self.daemon.running:
            if not self.daemon.power_state.initialized:
                await asyncio.sleep(poll_interval)
                continue

            mpd_state = await self.mpd.query_state_for_monitor()
            if mpd_state == "play":
                host_source = host_pb.SOURCE_MPD
                is_streaming = True
            else:
                output_format = None
                if mpd_state is not None and self.daemon.moode:
                    try:
                        output_format = await self.daemon.moode.get_output_format()
                    except Exception as exc:
                        playback_state = self.daemon.playback_state
                        if (
                            asyncio.get_running_loop().time() - playback_state.last_moode_api_fail_log_time
                        ) >= playback_state.moode_api_fail_log_interval:
                            playback_state.last_moode_api_fail_log_time = asyncio.get_running_loop().time()
                            self.logger.warning("Failed to get output_format from Moode API: %s", exc)

                host_source = self.determine_host_source(mpd_state, output_format)
                is_streaming = self.determine_streaming_active(mpd_state, output_format)

            if host_source == host_pb.SOURCE_BLUETOOTH and self.bluetooth.playing_signal_state is not None:
                is_streaming = self.bluetooth.playing_signal_state

            await self.wifi.apply_bt_wifi_policy(host_source, is_streaming)

            if host_source != self.daemon.playback_state.last_host_source:
                self.daemon.playback_state.last_host_source = host_source
                source_name = get_enum_name(host_pb.Source.DESCRIPTOR, host_source)
                self.logger.info("Host source changed to %s (%s); notifying MCU", source_name, host_source)
                await send_notify_host_source(client, self.daemon, host_source)

            if is_streaming is not None and is_streaming != self.daemon.playback_state.last_streaming_active:
                self.daemon.playback_state.last_streaming_active = is_streaming
                consecutive_failures = 0
                self.logger.info("Streaming active changed to %s; notifying MCU", "true" if is_streaming else "false")
                success = await send_streaming_active_state(client, self.daemon, is_streaming)
                if not success:
                    self.logger.warning("Failed to notify MCU of streaming state change")
            elif is_streaming is None:
                consecutive_failures += 1
                if consecutive_failures == 3:
                    self.logger.warning(
                        "Streaming state unknown (unable to query Moode output format) - keeping last known state"
                    )
                elif consecutive_failures == 100:
                    self.logger.warning(
                        "Streaming state unavailable for 100 polls; check MPD and Moode API are running"
                    )

            await asyncio.sleep(poll_interval)

        self.logger.info("Streaming monitor task stopped")

    async def wait_for_mpd_return(self, previous_source: int, timeout_s: float = 8.0) -> bool:
        deadline = asyncio.get_running_loop().time() + timeout_s
        last_mpd_state: Optional[str] = None
        last_output_format: Optional[str] = None

        while asyncio.get_running_loop().time() < deadline:
            mpd_state = await self.mpd.query_state()
            output_format = None
            if mpd_state is not None and self.daemon.moode:
                try:
                    output_format = await self.daemon.moode.get_output_format()
                except Exception as exc:
                    self.logger.debug("return_to_mpd: get_output_format failed while waiting for MPD: %s", exc)

            last_mpd_state = mpd_state
            last_output_format = output_format
            output_idle = self.is_output_format_idle(output_format)
            db_source = self.detect_active_renderer_from_moode_db()

            if mpd_state == "play":
                return True
            if previous_source in (host_pb.SOURCE_SPOTIFY, host_pb.SOURCE_AIRPLAY):
                if mpd_state in ("pause", "stop") and output_idle:
                    return True
            elif previous_source == host_pb.SOURCE_BLUETOOTH:
                if (
                    db_source != host_pb.SOURCE_BLUETOOTH
                    and output_idle
                    and self.bluetooth.playing_signal_state is not True
                ):
                    return True

            await asyncio.sleep(0.5)

        self.logger.error(
            "Timed out waiting for MPD return after return_to_mpd (previous_source=%s, mpd_state=%s, output_format=%r)",
            previous_source,
            last_mpd_state,
            last_output_format,
        )
        return False

    async def stop_active_renderer_for_mpd(self, source: int) -> bool:
        try:
            if source == host_pb.SOURCE_SPOTIFY:
                result = await self.wifi.run_moode_util(
                    "restart-renderer.php",
                    ["--spotify"],
                    timeout=10,
                    use_sudo=True,
                )
            elif source == host_pb.SOURCE_AIRPLAY:
                result = await self.wifi.run_moode_util(
                    "restart-renderer.php",
                    ["--airplay"],
                    timeout=10,
                    use_sudo=True,
                )
            elif source == host_pb.SOURCE_BLUETOOTH:
                result = await self.wifi.run_moode_util(
                    "blu-control.sh",
                    ["-D"],
                    timeout=10,
                    use_sudo=True,
                )
            else:
                self.logger.warning("return_to_mpd is not implemented for source=%s", source)
                return False
        except subprocess.TimeoutExpired:
            self.logger.error("Renderer stop command timed out for source=%s", source)
            return False
        except Exception as exc:
            self.logger.error("Renderer stop command failed for source=%s: %s", source, exc, exc_info=True)
            return False

        if result.returncode != 0:
            self.logger.error(
                "Renderer stop command failed for source=%s (rc=%s, stdout=%r, stderr=%r)",
                source,
                result.returncode,
                (result.stdout or "").strip(),
                (result.stderr or "").strip(),
            )
            return False

        self.logger.info(
            "Renderer stop command succeeded for source=%s (stdout=%r)",
            source,
            (result.stdout or "").strip(),
        )
        return True

    def detect_return_to_mpd_target(self) -> int:
        last_host_source = self.daemon.playback_state.last_host_source
        if last_host_source not in (None, host_pb.SOURCE_UNKNOWN):
            return last_host_source
        return host_pb.SOURCE_UNKNOWN

    def determine_streaming_active(
        self,
        mpd_state: Optional[str],
        output_format: Optional[str],
    ) -> Optional[bool]:
        if mpd_state is None:
            return None
        if mpd_state == "play":
            return True
        if output_format is None:
            self.logger.error("Streaming detection: output_format unavailable, returning None")
            return None

        fmt = self.normalize_output_format(output_format)
        if fmt and fmt not in ("not playing", "n/a", ""):
            if any(indicator in fmt for indicator in ("pcm", "khz", "bit", "dsd", "mhz")):
                return True
        return False

    def determine_host_source(self, mpd_state: Optional[str], output_format: Optional[str]) -> int:
        if mpd_state is None:
            return host_pb.SOURCE_UNKNOWN
        if mpd_state == "play":
            return host_pb.SOURCE_MPD

        detected = self.detect_active_renderer_from_moode_db()
        if detected is not None:
            return detected

        if output_format is None or self.normalize_output_format(output_format) in ("not playing", ""):
            return host_pb.SOURCE_MPD

        fmt_str = output_format if isinstance(output_format, str) else ""
        detected = self.detect_other_renderer_from_format(fmt_str)
        if detected is not None:
            return detected
        return host_pb.SOURCE_MPD

    def detect_other_renderer_from_format(self, output_format: str) -> Optional[int]:
        fmt = output_format.strip().lower()
        if "airplay" in fmt or "shairport" in fmt:
            return host_pb.SOURCE_AIRPLAY
        if "spotify" in fmt or "librespot" in fmt:
            return host_pb.SOURCE_SPOTIFY
        if "bluetooth" in fmt or "bluealsa" in fmt or "a2dp" in fmt:
            return host_pb.SOURCE_BLUETOOTH
        return None

    def detect_active_renderer_from_moode_db(self) -> Optional[int]:
        try:
            conn = sqlite3.connect("/var/local/www/db/moode-sqlite3.db", timeout=1.0)
            cursor = conn.cursor()
            cursor.execute(
                "SELECT param, value FROM cfg_system WHERE param IN "
                "('spotactive', 'aplactive', 'btactive', 'slactive', 'rbactive')"
            )
            active_flags = dict(cursor.fetchall())
            conn.close()

            if active_flags.get("spotactive") == "1":
                return host_pb.SOURCE_SPOTIFY
            if active_flags.get("aplactive") == "1":
                return host_pb.SOURCE_AIRPLAY
            if active_flags.get("btactive") == "1":
                return host_pb.SOURCE_BLUETOOTH
            return None
        except Exception as exc:
            self.logger.error("Failed to query Moode database for active renderer: %s", exc)
            return None

    @staticmethod
    def normalize_output_format(output_format: Optional[str]) -> Optional[str]:
        if output_format is None:
            return None
        return str(output_format).strip().lower()

    def is_output_format_idle(self, output_format: Optional[str]) -> bool:
        return self.normalize_output_format(output_format) in ("not playing", "")
