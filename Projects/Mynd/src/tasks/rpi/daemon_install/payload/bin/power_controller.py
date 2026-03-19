#!/usr/bin/env python3
"""
Power handshake and shutdown orchestration for the daemon.
"""

from __future__ import annotations

import asyncio
import shlex
import subprocess

from actionslink_adapter import message_pb
from daemon_context import (
    POWER_STATE_OFF,
    POWER_STATE_ON,
    POWER_STATE_SHUTDOWN_REQUEST,
    get_enum_name,
)
from link_protocol import send_power_state


class PowerController:
    def __init__(self, daemon, playback, wifi):
        self.daemon = daemon
        self.logger = daemon.logger
        self.playback = playback
        self.wifi = wifi

    async def initialize_daemon_services(self, client):
        try:
            if await self.wifi.set_wifi_radio_state(True):
                self.wifi.bt_wifi_radio_forced_off = False
                self.wifi.bt_wifi_restore_connections = []
                self.logger.info("WiFi radio enabled during daemon initialization")
            self.daemon.power_state.initialized = True
            self.logger.info("Daemon services initialized")
            await self.playback.start_monitor(client)
        except Exception as exc:
            self.logger.error("Error during daemon initialization: %s", exc, exc_info=True)

    async def handle_power_state(self, client, power_state: int):
        self.daemon.power_state.mcu_power_state = power_state
        state_name = get_enum_name(system_pb.PowerState.SystemPowerMode.DESCRIPTOR, power_state)
        self.logger.info("Power state changed: %s (%s)", state_name, power_state)

        async with self.daemon.power_state.transition_lock:
            if power_state == POWER_STATE_ON:
                if not self.daemon.power_state.initialized:
                    self.logger.info("Initializing daemon services")
                    await self.initialize_daemon_services(client)
                self.logger.info("On - system fully operational")
                return

            if power_state not in (POWER_STATE_SHUTDOWN_REQUEST, POWER_STATE_OFF):
                self.logger.warning("Unknown power state received: %s", power_state)
                return

            self.daemon.power_state.initialized = False
            self.daemon.power_state.rpi_power_state = POWER_STATE_OFF
            self.logger.info("Power off requested - preparing for shutdown then poweroff")
            await self.pause_moode_playback()
            await self.playback.stop_monitor()

            shutdown_prep_task = self.daemon.power_state.shutdown_prep_task
            if shutdown_prep_task and not shutdown_prep_task.done():
                self.logger.debug("Shutdown already in progress")
                return

            self.daemon.power_state.shutdown_prep_task = asyncio.create_task(self.prepare_shutdown())
            await self.daemon.power_state.shutdown_prep_task
            self.daemon.power_state.poweroff_task = asyncio.create_task(self.execute_poweroff())

    async def pause_moode_playback(self) -> bool:
        if not self.daemon.moode:
            return False
        playback_state = await self.playback.mpd.query_state()
        if playback_state == "play":
            await self.daemon.moode.pause_playback()
            self.logger.info("Playback paused")
            return True
        self.logger.debug("Playback not active, skipping pause")
        return False

    async def prepare_shutdown(self):
        try:
            await self.daemon.command_runner.run(["sync"], timeout=5)
            self.logger.info("Filesystems synced")
            await asyncio.sleep(0.1)
            self.logger.info("Shutdown preparation complete")
        except Exception as exc:
            self.logger.error("Failed to prepare shutdown: %s", exc, exc_info=True)

    async def execute_poweroff(self):
        try:
            shutdown_prep_task = self.daemon.power_state.shutdown_prep_task
            if shutdown_prep_task and not shutdown_prep_task.done():
                self.logger.info("Waiting for shutdown preparation to complete...")
                await shutdown_prep_task
                self.logger.info("Shutdown preparation completed")

            self.logger.info("Executing poweroff")
            command = shlex.split(self.daemon.config["power"]["poweroff_command"])
            await self.daemon.command_runner.run(command, timeout=10)
        except subprocess.TimeoutExpired:
            self.logger.error("Poweroff command timed out")
        except Exception as exc:
            self.logger.error("Failed to execute poweroff: %s", exc, exc_info=True)

    async def ensure_startup_power_on(self, client, timeout_s: float = 5.0):
        power_state_event = self.daemon.power_state.power_state_event
        if not power_state_event or power_state_event.is_set():
            return

        try:
            await asyncio.wait_for(power_state_event.wait(), timeout=timeout_s)
            self.logger.debug("Initial MCU power state received during startup window")
            return
        except asyncio.TimeoutError:
            self.logger.warning(
                "No MCU power state received within %.1fs after startup; requesting MCU firmware version as fallback",
                timeout_s,
            )

        if power_state_event.is_set():
            return

        try:
            request = message_pb.ToMcuRequest()
            request.get_mcu_firmware_version.SetInParent()
            response = await client.send_request(
                request,
                expected_response="get_mcu_firmware_version",
                timeout=5000,
            )
        except Exception as exc:
            self.logger.error("Startup fallback MCU firmware request failed: %s", exc, exc_info=True)
            return

        if response is None:
            self.logger.warning(
                "Startup fallback MCU firmware request returned no response; waiting for MCU power state"
            )
            return

        self.logger.info(
            "MCU firmware version request succeeded during startup fallback; proceeding with synthetic power-on flow"
        )
        if not power_state_event.is_set():
            power_state_event.set()

        if self.daemon.power_state.rpi_power_state != POWER_STATE_ON:
            await send_power_state(client, self.daemon, POWER_STATE_ON, retry=True)
            self.daemon.power_state.rpi_power_state = POWER_STATE_ON
        else:
            self.logger.debug("Skipping duplicate startup fallback power ON notification")

        await asyncio.sleep(0.2)
        await self.handle_power_state(client, POWER_STATE_ON)


from actionslink_adapter import system_pb  # noqa: E402
