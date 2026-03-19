#!/usr/bin/env python3
"""
MCU protocol helpers shared across daemon modules.
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import Callable, Tuple

from actionslink_adapter import error_pb, host_pb, leds_pb, message_pb, system_pb
from daemon_context import POWER_STATE_ON, get_enum_name


def build_status_response(seq: int, response_field: str, status_code: int) -> message_pb.ToMcuResponse:
    response = message_pb.ToMcuResponse()
    response.seq = seq
    getattr(response, response_field).status.code = status_code
    return response


async def send_status_response(client, seq: int, response_field: str, status_code: int) -> bool:
    response = build_status_response(seq, response_field, status_code)
    return await client.send_response(response)


@dataclass
class ResponseTracker:
    client: object
    seq: int
    response_field: str
    sent: bool = False

    async def send(self, status_code: int) -> bool:
        if self.sent:
            return False
        self.sent = True
        return await send_status_response(self.client, self.seq, self.response_field, status_code)


async def begin_request_handling(daemon, request_key: str, request_label: str, seq: int) -> bool:
    if daemon.last_seq.get(request_key) == seq:
        daemon.logger.debug("Ignoring duplicate %s request (seq=%s)", request_label, seq)
        return False
    daemon.last_seq[request_key] = seq
    await asyncio.sleep(0.05)
    return True


async def send_notify_system_ready(client, daemon) -> bool:
    event = message_pb.ToMcuEvent()
    event.notify_system_ready.SetInParent()
    timeout_ms = 1000
    daemon.logger.info("Sending notify_system_ready (timeout=%sms)...", timeout_ms)
    result = await client.send_event(event, timeout=timeout_ms)
    if result:
        daemon.logger.debug("notify_system_ready ACKed")
        return True
    daemon.logger.warning("notify_system_ready timed out - transport should have cleared pending frame")
    return False


async def send_event_with_retry(
    client,
    daemon,
    event_builder: Callable[[], message_pb.ToMcuEvent],
    timeout_ms: int,
    max_attempts: int,
    error_label: str,
    retry_delay_s: float = 0.2,
) -> Tuple[bool, int]:
    for attempt in range(1, max_attempts + 1):
        try:
            if await client.send_event(event_builder(), timeout=timeout_ms):
                return True, attempt
        except Exception as exc:
            daemon.logger.error("%s: %s", error_label, exc)
        if attempt < max_attempts:
            await asyncio.sleep(retry_delay_s)
    return False, max_attempts


async def send_power_state(client, daemon, power_state: int, retry: bool = False) -> bool:
    if not client.is_connected:
        daemon.logger.debug("Not connected, skipping RPi power state update")
        return False

    state_name = get_enum_name(system_pb.PowerState.SystemPowerMode.DESCRIPTOR, power_state)
    is_critical = power_state == POWER_STATE_ON or retry
    max_attempts = 5 if is_critical else 1
    timeout_ms = 3000
    daemon.logger.debug("Sending RPi power state to MCU: %s (%s)", state_name, power_state)

    def _event_builder() -> message_pb.ToMcuEvent:
        event = message_pb.ToMcuEvent()
        event.notify_power_state.mode = power_state
        return event

    success, attempt = await send_event_with_retry(
        client=client,
        daemon=daemon,
        event_builder=_event_builder,
        timeout_ms=timeout_ms,
        max_attempts=max_attempts,
        error_label=f"Error sending power state update {state_name}",
    )
    if success:
        if attempt > 1:
            daemon.logger.debug("Power state update %s succeeded on attempt %s", state_name, attempt)
        return True

    daemon.logger.error("Failed to send power state update %s after %s attempts", state_name, max_attempts)
    return False


async def send_notify_host_source(client, daemon, source: int) -> bool:
    if not client.is_connected:
        daemon.logger.debug("Not connected, skipping host_source update")
        return False

    def _event_builder() -> message_pb.ToMcuEvent:
        event = message_pb.ToMcuEvent()
        event.notify_host_source = source
        return event

    success, _ = await send_event_with_retry(
        client=client,
        daemon=daemon,
        event_builder=_event_builder,
        timeout_ms=3000,
        max_attempts=3,
        error_label="Error sending notify_host_source",
    )
    if success:
        return True
    daemon.logger.error("Failed to send notify_host_source after %s attempts", 3)
    return False


async def send_notify_wifi_info(client, daemon, ssid: str, ip_address: str, username: str) -> bool:
    if not client.is_connected:
        daemon.logger.debug("Not connected, skipping wifi info update")
        return False

    def _event_builder() -> message_pb.ToMcuEvent:
        event = message_pb.ToMcuEvent()
        event.notify_wifi_info.ssid = ssid
        event.notify_wifi_info.ip_address = ip_address
        event.notify_wifi_info.username = username
        return event

    success, _ = await send_event_with_retry(
        client=client,
        daemon=daemon,
        event_builder=_event_builder,
        timeout_ms=1000,
        max_attempts=2,
        error_label="Error sending notify_wifi_info",
    )
    if success:
        daemon.logger.info("Sent wifi info to MCU (ssid=%s, ip=%s, user=%s)", ssid, ip_address, username)
        return True
    daemon.logger.warning("Failed to send notify_wifi_info after %s attempts", 2)
    return False


async def send_notify_wifi_command_result(
    client,
    daemon,
    command_id: int,
    action_type: int,
    status_code: int,
    *,
    target_reached: bool = True,
    detail: int = host_pb.WiFiCommandResult.Detail.DETAIL_NONE,
) -> bool:
    if not client.is_connected:
        daemon.logger.debug("Not connected, skipping wifi command result")
        return False

    def _event_builder() -> message_pb.ToMcuEvent:
        event = message_pb.ToMcuEvent()
        event.notify_wifi_command_result.command_id = command_id
        event.notify_wifi_command_result.action_type = action_type
        event.notify_wifi_command_result.status.status.code = status_code
        event.notify_wifi_command_result.target_reached = target_reached
        event.notify_wifi_command_result.detail = detail
        return event

    success, _ = await send_event_with_retry(
        client=client,
        daemon=daemon,
        event_builder=_event_builder,
        timeout_ms=1000,
        max_attempts=2,
        error_label="Error sending notify_wifi_command_result",
    )
    if success:
        daemon.logger.info(
            "Sent wifi command result to MCU (command_id=%s, action_type=%s, status=%s, detail=%s, target_reached=%s)",
            command_id,
            action_type,
            status_code,
            detail,
            target_reached,
        )
        return True
    daemon.logger.warning("Failed to send notify_wifi_command_result after %s attempts", 2)
    return False


async def send_streaming_active_state(client, daemon, is_active: bool) -> bool:
    if not client.is_connected:
        daemon.logger.debug("Not connected, skipping streaming state update")
        return False

    def _event_builder() -> message_pb.ToMcuEvent:
        event = message_pb.ToMcuEvent()
        event.notify_stream_state = is_active
        return event

    success, attempt = await send_event_with_retry(
        client=client,
        daemon=daemon,
        event_builder=_event_builder,
        timeout_ms=3000,
        max_attempts=3,
        error_label="Error sending streaming state",
    )
    if success:
        if attempt > 1:
            daemon.logger.debug("Streaming state notification succeeded on attempt %s", attempt)
        return True
    daemon.logger.error("Failed to send streaming state after %s attempts", 3)
    return False


async def flash_result_status_led(client, daemon, success: bool):
    if not client.is_connected:
        daemon.logger.debug("Not connected, skipping play_led_pattern")
        return

    try:
        event = message_pb.ToMcuEvent()
        event.notify_play_led_pattern.pattern = (
            leds_pb.PlayLedPattern.Pattern.POSITIVE_FEEDBACK
            if success
            else leds_pb.PlayLedPattern.Pattern.NEGATIVE_FEEDBACK
        )
        await client.send_event(event)
        daemon.logger.info("LED pattern: %s feedback", "positive" if success else "negative")
    except Exception as exc:
        daemon.logger.error("Error in play_led_pattern: %s", exc)


__all__ = [
    "ResponseTracker",
    "begin_request_handling",
    "build_status_response",
    "flash_result_status_led",
    "send_event_with_retry",
    "send_notify_host_source",
    "send_notify_wifi_command_result",
    "send_notify_system_ready",
    "send_notify_wifi_info",
    "send_power_state",
    "send_status_response",
    "send_streaming_active_state",
]
