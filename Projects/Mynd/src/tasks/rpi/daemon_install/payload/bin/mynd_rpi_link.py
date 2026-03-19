#!/usr/bin/env python3
"""
Mynd RPi Link Daemon

Receives messages from Mynd MCU via ActionsLink protobuf protocol and executes commands on the
Raspberry Pi. Receives action events (play/pause, track navigation) and forwards them to Moode
or BlueZ. Handles power state changes and WiFi workflows.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import signal
import sys

from actionslink_adapter import ActionsLinkClient, host_pb
from bluetooth_controller import BluetoothController
from daemon_context import MyndRpiDaemon, POWER_STATE_OFF, POWER_STATE_STANDBY
from link_protocol import send_notify_host_source, send_notify_system_ready, send_power_state
from moode_client import MoodeClient
from playback_controller import PlaybackController
from power_controller import PowerController
from request_handlers import RequestHandlers
from wifi_controller import WifiController


async def run_daemon(daemon: MyndRpiDaemon):
    """Main daemon loop."""
    daemon.logger.info("Starting Mynd RPi Link Daemon")
    daemon.power_state.power_state_event = asyncio.Event()

    device = daemon.config["uart"]["device"]
    is_valid, actual_device, error_msg = daemon.validate_serial_device(device)
    if not is_valid:
        daemon.logger.error("Serial device validation failed:")
        daemon.logger.error("  %s", error_msg)
        daemon.logger.error("")
        daemon.logger.error("Troubleshooting steps:")
        daemon.logger.error("  1. Check if UART is enabled: grep enable_uart /boot/config.txt")
        daemon.logger.error("  2. If not enabled, add 'enable_uart=1' to /boot/config.txt and reboot")
        daemon.logger.error("  3. Check available devices: ls -l /dev/serial* /dev/ttyS* /dev/ttyAMA*")
        daemon.logger.error("  4. Update device path in configuration file if needed")
        daemon.logger.error("  5. Ensure user is in dialout group: sudo usermod -a -G dialout $USER")
        return 1

    if actual_device != device:
        daemon.logger.debug("Using resolved device path: %s (from %s)", actual_device, device)
        device = actual_device

    daemon.moode = MoodeClient(
        daemon.config["moode"]["base_url"],
        float(daemon.config["moode"]["api_timeout"]),
        int(daemon.config["moode"]["retry_count"]),
        command_runner=daemon.command_runner,
    )
    await daemon.moode.start()
    await daemon.moode.refresh_volume_settings()

    daemon.bluetooth = BluetoothController(daemon.logger, daemon.config)
    daemon.wifi = WifiController(daemon)
    daemon.playback = PlaybackController(daemon, daemon.bluetooth, daemon.wifi)
    daemon.power = PowerController(daemon, daemon.playback, daemon.wifi)

    try:
        async with ActionsLinkClient(device) as client:
            handlers = RequestHandlers(client, daemon)
            client.on_event(handlers.handle_mcu_event)
            client.on_request(
                "set_power_state",
                lambda seq, req: handlers.schedule(
                    handlers.handle_set_power_state(seq, req),
                    "set_power_state",
                ),
            )
            client.on_request(
                "send_playback_action",
                lambda seq, req: handlers.schedule(
                    handlers.handle_send_playback_action(seq, req),
                    "send_playback_action",
                ),
            )
            client.on_request(
                "set_volume",
                lambda seq, req: handlers.schedule(
                    handlers.handle_set_volume(seq, req),
                    "set_volume",
                ),
            )
            client.on_request(
                "cycle_source",
                lambda seq, req: handlers.schedule(
                    handlers.handle_return_to_mpd(seq, req),
                    "cycle_source",
                ),
            )

            daemon.logger.info("Request handlers registered:")
            daemon.logger.info("  - set_power_state: Handle power state changes from MCU")
            daemon.logger.info("  - send_playback_action: Handle media control requests from MCU")
            daemon.logger.info("  - set_volume: Handle volume up/down requests from MCU")
            daemon.logger.info("  - cycle_source: Return active host source to MPD")
            daemon.logger.info("MCU event handler registered for battery and WiFi command events")

            daemon.logger.info("Connection established, waiting 200ms for stabilization...")
            await asyncio.sleep(0.2)

            daemon.running = True
            if not await send_notify_system_ready(client, daemon):
                daemon.logger.error(
                    "Failed to send notify_system_ready - System task will timeout after 30s"
                )

            daemon.logger.debug("notify_system_ready sent successfully")
            await daemon.power.ensure_startup_power_on(client, timeout_s=5.0)

            try:
                while daemon.running:
                    await asyncio.sleep(1)
            except KeyboardInterrupt:
                daemon.logger.info("Received interrupt signal")
            except Exception as exc:
                daemon.logger.error("Error in main loop: %s", exc, exc_info=True)
            finally:
                daemon.running = False
                try:
                    await daemon.playback.stop_monitor()
                except Exception as exc:
                    daemon.logger.debug("Failed to stop streaming monitor: %s", exc)

                if daemon.power_state.rpi_power_state != POWER_STATE_OFF:
                    try:
                        await send_notify_host_source(client, daemon, host_pb.SOURCE_UNKNOWN)
                        await send_power_state(client, daemon, POWER_STATE_STANDBY)
                        daemon.logger.info("Sent standby + unknown host source to MCU")
                    except Exception as exc:
                        daemon.logger.error("Failed to send shutdown notifications: %s", exc)

                daemon.logger.info("Mynd RPi Link Daemon stopped successfully")
    finally:
        try:
            if daemon.moode:
                await daemon.moode.stop()
        except Exception as exc:
            daemon.logger.error("Failed to stop Moode client: %s", exc)

    return 0


def main():
    parser = argparse.ArgumentParser(description="Mynd RPi Link Daemon")
    parser.add_argument(
        "--config",
        default="/etc/mynd_rpi.conf",
        help="Path to configuration file",
    )
    args = parser.parse_args()

    if not os.path.exists(args.config):
        print(f"ERROR: Configuration file not found: {args.config}", file=sys.stderr)
        print(
            "Please create a configuration file or use --config to specify a different path",
            file=sys.stderr,
        )
        sys.exit(1)

    daemon = MyndRpiDaemon(args.config)

    def signal_handler(_sig, _frame):
        daemon.stop()

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    return asyncio.run(run_daemon(daemon))


if __name__ == "__main__":
    sys.exit(main())
