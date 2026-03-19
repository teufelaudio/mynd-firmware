#!/usr/bin/env python3
"""
MCU request and event handlers for the daemon.
"""

from __future__ import annotations

import asyncio

from actionslink_adapter import audio_pb, battery_pb, error_pb, host_pb, system_pb
from daemon_context import (
    POWER_STATE_OFF,
    POWER_STATE_ON,
    POWER_STATE_SHUTDOWN_REQUEST,
    POWER_STATE_STANDBY,
    get_enum_name,
)
from link_protocol import (
    ResponseTracker,
    begin_request_handling,
    flash_result_status_led,
    send_notify_host_source,
    send_notify_wifi_command_result,
    send_notify_wifi_info,
    send_power_state,
)


class RequestHandlers:
    def __init__(self, client, daemon):
        self.client = client
        self.daemon = daemon
        self.logger = daemon.logger
        self._tasks: set[asyncio.Task] = set()

    def schedule(self, coro, label: str):
        task = asyncio.create_task(coro)
        self._tasks.add(task)

        def _done_callback(done_task: asyncio.Task):
            self._tasks.discard(done_task)
            try:
                done_task.result()
            except asyncio.CancelledError:
                pass
            except Exception as exc:
                self.logger.error("Unhandled error in %s task: %s", label, exc, exc_info=True)

        task.add_done_callback(_done_callback)
        return task

    def handle_mcu_event(self, event):
        event_type = event.WhichOneof("Event")
        if event_type == "notify_battery_level":
            self.daemon.hardware_state.battery_level = event.notify_battery_level
            self.logger.info("Battery level: %s%%", self.daemon.hardware_state.battery_level)
        elif event_type == "notify_charger_status":
            old_state = self.daemon.hardware_state.charging_active
            self.daemon.hardware_state.charging_active = (
                event.notify_charger_status == battery_pb.ChargerStatus.Active
            )
            if old_state != self.daemon.hardware_state.charging_active:
                self.logger.info(
                    "Charger %s",
                    "connected" if self.daemon.hardware_state.charging_active else "disconnected",
                )
        elif event_type == "notify_battery_friendly_charging":
            self.daemon.hardware_state.battery_friendly_charging = event.notify_battery_friendly_charging
        elif event_type == "notify_configure_wifi_command":
            command = event.notify_configure_wifi_command
            self.logger.debug(
                "Received configure_wifi event (command_id=%s): %s",
                command.command_id,
                command.config.ssid,
            )
            self.schedule(
                self.handle_configure_wifi_event(command),
                f"configure_wifi:{command.command_id}",
            )
        elif event_type == "notify_enable_hotspot_command":
            command = event.notify_enable_hotspot_command
            self.logger.debug("Received enable_hotspot event (command_id=%s)", command.command_id)
            self.schedule(
                self.handle_enable_hotspot_event(command),
                f"enable_hotspot:{command.command_id}",
            )
        elif event_type == "notify_cycle_wifi_network_command":
            command = event.notify_cycle_wifi_network_command
            self.logger.debug("Received cycle_wifi_network event (command_id=%s)", command.command_id)
            self.schedule(
                self.handle_cycle_wifi_network_event(command),
                f"cycle_wifi_network:{command.command_id}",
            )
        else:
            self.logger.warning("Unknown event received: %s", event_type)

    async def handle_set_power_state(self, seq: int, power_state):
        state_value = power_state.mode
        state_name = get_enum_name(system_pb.PowerState.SystemPowerMode.DESCRIPTOR, state_value)
        self.logger.debug("Received setPowerState request (seq=%s): %s", seq, state_name)

        if not await begin_request_handling(self.daemon, "set_power_state", "setPowerState", seq):
            return

        responder = ResponseTracker(self.client, seq, "set_power_state")
        try:
            power_state_event = self.daemon.power_state.power_state_event
            if power_state_event and not power_state_event.is_set():
                power_state_event.set()

            if state_value == POWER_STATE_ON:
                await responder.send(error_pb.Code.Success)
                await asyncio.sleep(0.5)
                if self.daemon.power_state.rpi_power_state != POWER_STATE_ON:
                    await send_power_state(self.client, self.daemon, POWER_STATE_ON, retry=True)
                    self.daemon.power_state.rpi_power_state = POWER_STATE_ON
                else:
                    self.logger.debug("Skipping duplicate power ON notification")
                await asyncio.sleep(0.2)
                await self.daemon.power.handle_power_state(self.client, state_value)
                return

            if state_value in (POWER_STATE_OFF, POWER_STATE_SHUTDOWN_REQUEST):
                requested_shutdown_state = state_value
                await self.daemon.power.handle_power_state(self.client, POWER_STATE_OFF)
                await asyncio.sleep(0.2)
                await send_notify_host_source(self.client, self.daemon, host_pb.SOURCE_UNKNOWN)
                await send_power_state(self.client, self.daemon, requested_shutdown_state)
                await responder.send(error_pb.Code.Success)
                return

            if state_value != POWER_STATE_STANDBY:
                self.logger.warning("Unknown power state received: %s", state_value)
            await responder.send(error_pb.Code.OperationFailed)
        except Exception as exc:
            self.logger.error("Error handling setPowerState request: %s", exc, exc_info=True)
            await responder.send(error_pb.Code.OperationFailed)

    async def handle_send_playback_action(self, seq: int, avrcp_action):
        action = avrcp_action.action
        action_name = get_enum_name(host_pb.PlaybackAction.Action.DESCRIPTOR, action)
        self.logger.debug("Received send_playback_action request (seq=%s): %s", seq, action_name)

        if not await begin_request_handling(self.daemon, "send_playback_action", "send_playback_action", seq):
            return

        responder = ResponseTracker(self.client, seq, "send_playback_action")
        try:
            if action == host_pb.PlaybackAction.Action.TOGGLE_PLAY_PAUSE:
                success = await self.daemon.playback.handle_play_pause_for_active_source()
            elif action == host_pb.PlaybackAction.Action.NEXT_TRACK:
                success = await self.daemon.playback.handle_next_track_for_active_source()
            elif action == host_pb.PlaybackAction.Action.PREVIOUS_TRACK:
                success = await self.daemon.playback.handle_previous_track_for_active_source()
            else:
                self.logger.warning("Unknown media action: %s", action)
                await responder.send(error_pb.Code.OperationFailed)
                return

            await responder.send(error_pb.Code.Success if success else error_pb.Code.OperationFailed)
        except Exception as exc:
            self.logger.error("Error handling avrcp action request: %s", exc, exc_info=True)
            await responder.send(error_pb.Code.OperationFailed)

    async def handle_set_volume(self, seq: int, volume_control):
        action = volume_control.action
        self.logger.debug("Received set_volume request (seq=%s): action=%s", seq, action)

        if not await begin_request_handling(self.daemon, "set_volume", "set_volume", seq):
            return

        responder = ResponseTracker(self.client, seq, "set_volume")
        try:
            result = await self.daemon.playback.handle_volume_action(action)
            await responder.send(result.status_code)

            if not result.success:
                self.logger.error("Failed to apply volume action")
                return
            if result.boundary == "max":
                await flash_result_status_led(self.client, self.daemon, True)
            elif result.boundary == "min":
                await flash_result_status_led(self.client, self.daemon, False)
        except Exception as exc:
            self.logger.error("Error handling set_volume request: %s", exc, exc_info=True)
            await responder.send(error_pb.Code.OperationFailed)

    async def _handle_wifi_command(
        self,
        *,
        command_id: int,
        action_label: str,
        action_type: int,
        run_command,
        pause_moode: bool = False,
    ):
        start_state = await self.daemon.wifi.begin_command(command_id, action_label)
        if start_state == "duplicate":
            self.logger.info("Ignoring duplicate %s WiFi command (command_id=%s)", action_label, command_id)
            return
        if start_state == "busy":
            await send_notify_wifi_command_result(
                self.client,
                self.daemon,
                command_id,
                action_type,
                error_pb.Code.ResourceUnavailable,
                target_reached=False,
                detail=host_pb.WiFiCommandResult.Detail.DETAIL_BUSY,
            )
            return

        try:
            if pause_moode:
                await self.daemon.power.pause_moode_playback()

            result = await run_command()
            await send_notify_wifi_command_result(
                self.client,
                self.daemon,
                command_id,
                action_type,
                result.status_code,
                target_reached=result.target_reached,
                detail=result.detail,
            )
            if result.success:
                await send_notify_wifi_info(
                    self.client,
                    self.daemon,
                    result.ssid,
                    result.ip_address,
                    result.username,
                )
        except Exception as exc:
            self.logger.error("Error handling %s WiFi command: %s", action_label, exc, exc_info=True)
            await send_notify_wifi_command_result(
                self.client,
                self.daemon,
                command_id,
                action_type,
                error_pb.Code.OperationFailed,
                target_reached=False,
            )
        finally:
            await self.daemon.wifi.finish_command(command_id)

    async def handle_configure_wifi_event(self, command):
        await self._handle_wifi_command(
            command_id=command.command_id,
            action_label="configure_wifi",
            action_type=host_pb.WiFiCommandResult.ActionType.ACTION_TYPE_CONFIGURE_WIFI,
            run_command=lambda: self.daemon.wifi.configure_wifi(command.config.ssid, command.config.password),
        )

    async def handle_enable_hotspot_event(self, command):
        await self._handle_wifi_command(
            command_id=command.command_id,
            action_label="enable_hotspot",
            action_type=host_pb.WiFiCommandResult.ActionType.ACTION_TYPE_ENABLE_HOTSPOT,
            run_command=self.daemon.wifi.enable_hotspot,
            pause_moode=True,
        )

    async def handle_cycle_wifi_network_event(self, command):
        await self._handle_wifi_command(
            command_id=command.command_id,
            action_label="cycle_wifi_network",
            action_type=host_pb.WiFiCommandResult.ActionType.ACTION_TYPE_CYCLE_WIFI_NETWORK,
            run_command=self.daemon.wifi.cycle_wifi_network,
        )

    async def handle_return_to_mpd(self, seq: int, _req):
        self.logger.debug("Received cycle_source request (seq=%s)", seq)

        if not await begin_request_handling(self.daemon, "cycle_source", "cycle_source", seq):
            return

        responder = ResponseTracker(self.client, seq, "cycle_source")
        try:
            await self.daemon.power.pause_moode_playback()
            success, source_changed, previous_source = await self.daemon.playback.begin_return_to_mpd()
            if not success:
                await responder.send(error_pb.Code.OperationFailed)
                return
            await responder.send(error_pb.Code.Success)
            if source_changed:
                if not await self.daemon.playback.complete_return_to_mpd(previous_source):
                    self.logger.error("Return-to-MPD request did not reach MPD after stopping the active renderer")
                    return
                if not await send_notify_host_source(self.client, self.daemon, host_pb.SOURCE_MPD):
                    self.logger.error("Failed to notify MCU about MPD host source after cycle_source request")
                    return
        except Exception as exc:
            self.logger.error("Error returning host source to MPD: %s", exc, exc_info=True)
            await responder.send(error_pb.Code.OperationFailed)
