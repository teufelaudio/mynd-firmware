#!/usr/bin/env python3
"""
Example: Parallel Event Handling and Message Sending

This example demonstrates how to handle incoming events from the MCU
in parallel with sending messages from your application.

The key points:
1. Event callbacks are called asynchronously when events arrive
2. You can send messages at any time - events will be handled in parallel
3. Use asyncio tasks to run multiple operations concurrently
"""

import asyncio
import logging
import sys
from pathlib import Path
from typing import Optional

# Add parent directory (host/) to path for library imports
SCRIPT_DIR = Path(__file__).resolve().parent
HOST_DIR = SCRIPT_DIR.parent
if str(HOST_DIR) not in sys.path:
    sys.path.insert(0, str(HOST_DIR))

# Add generated directory to path for protobuf imports
GENERATED_DIR = HOST_DIR / "generated"
if str(GENERATED_DIR) not in sys.path:
    sys.path.insert(0, str(GENERATED_DIR))

from actionslink_client import ActionsLinkClient

# Import protobuf messages
try:
    from message_pb2 import ToMcuEvent, FromMcuEvent, ToMcuRequest
    import audio_pb2
    import system_pb2
    import battery_pb2
except ImportError as e:
    print(f"ERROR: Failed to import protobuf files: {e}", file=sys.stderr)
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s: %(message)s'
)
logger = logging.getLogger("example")


# ============================================================================
# Event Handler - Called automatically when events arrive from MCU
# ============================================================================

def format_event(event: FromMcuEvent) -> str:
    """Format a FromMcuEvent for display."""
    event_type = event.WhichOneof("Event")
    if not event_type:
        return "Unknown event (empty)"
    
    try:
        if event_type == "notify_battery_level":
            level = event.notify_battery_level
            return f"Battery Level: {level}%"
        
        elif event_type == "notify_charger_status":
            if not battery_pb2:
                return f"Charger Status: {event.notify_charger_status}"
            charger_status_enum = event.notify_charger_status
            try:
                enum_descriptor = battery_pb2.ChargerStatus.DESCRIPTOR
                enum_value = enum_descriptor.values_by_number.get(charger_status_enum)
                status_name = enum_value.name if enum_value else f"Unknown({charger_status_enum})"
            except (AttributeError, KeyError):
                status_map = {0: "NotConnected", 1: "Active", 2: "Inactive", 3: "Fault"}
                status_name = status_map.get(charger_status_enum, f"Unknown({charger_status_enum})")
            return f"Charger Status: {status_name}"
        
        elif event_type == "notify_battery_friendly_charging":
            enabled = event.notify_battery_friendly_charging
            return f"Battery Friendly Charging: {'enabled' if enabled else 'disabled'}"
        
        elif event_type == "notify_eco_mode":
            enabled = event.notify_eco_mode
            return f"Eco Mode: {'enabled' if enabled else 'disabled'}"
        
        elif event_type == "notify_button_event":
            button = event.notify_button_event
            return f"Button Event: bitfield={button.button_bitfield}, state={button.input_state}"
        
        elif event_type == "notify_aux_connected":
            connected = event.notify_aux_connected
            return f"Aux Connected: {connected}"
        
        elif event_type == "notify_usb_connected":
            connected = event.notify_usb_connected
            return f"USB Connected: {connected}"
        
        elif event_type.startswith("notify_battery_history_buffer_"):
            buffer = event.__getattribute__(event_type)
            period = event_type.replace("notify_battery_history_buffer_", "")
            return f"Battery History ({period}): {len(buffer)} bytes"
        
        else:
            value = getattr(event, event_type)
            return f"{event_type}: {value}"
    
    except Exception as e:
        logger.debug(f"Error formatting event {event_type}: {e}", exc_info=True)
        return f"{event_type}: (error formatting: {e})"


def on_event_received(event: FromMcuEvent):
    """
    Event handler callback - called automatically when an event arrives from MCU.
    
    This runs in the same event loop as your application, so it's safe to:
    - Access shared state (with proper synchronization)
    - Schedule async tasks
    - Update application state
    
    NOTE: This callback should be fast - don't block here!
    For long-running operations, use asyncio.create_task()
    """
    event_type = event.WhichOneof("Event")
    if event_type:
        formatted = format_event(event)
        logger.info(f"📨 Event from MCU: {formatted}")
        
        # Example: You can update application state here
        # Example: You can trigger actions based on events
        # Example: You can schedule async tasks for complex processing
        
        # For example, if battery is low, you might want to do something:
        if event_type == "notify_battery_level":
            level = event.notify_battery_level
            if level < 20:
                logger.warning(f"⚠️  Low battery warning: {level}%")
                # You could schedule a task here:
                # asyncio.create_task(handle_low_battery(level))
    else:
        logger.warning("Received empty event from MCU")


# ============================================================================
# Application Logic - Your code that sends messages
# ============================================================================

async def send_periodic_updates(client: ActionsLinkClient, interval: float = 5.0):
    """
    Example: Send periodic updates to MCU.
    
    This runs concurrently with event handling - events will be received
    and processed even while this function is running.
    """
    logger.info(f"Starting periodic updates (every {interval}s)")
    
    volume = 50
    while True:
        try:
            # Send volume update
            volume_msg = audio_pb2.Volume()
            volume_msg.percent = volume
            volume_msg.is_muted = False
            
            event = ToMcuEvent()
            event.notify_volume.CopyFrom(volume_msg)
            
            success = await client.send_event(event)
            if success:
                logger.info(f"✅ Sent volume update: {volume}%")
            else:
                logger.error("❌ Failed to send volume update")
            
            # Cycle volume for demo
            volume = (volume + 10) % 100
            
            await asyncio.sleep(interval)
            
        except asyncio.CancelledError:
            logger.info("Periodic updates cancelled")
            break
        except Exception as e:
            logger.error(f"Error in periodic updates: {e}", exc_info=True)
            await asyncio.sleep(interval)


async def send_system_ready(client: ActionsLinkClient):
    """Send system ready notification."""
    logger.info("Sending system ready notification...")
    
    event = ToMcuEvent()
    event.notify_system_ready.SetInParent()
    
    success = await client.send_event(event)
    if success:
        logger.info("✅ System ready notification sent")
    else:
        logger.error("❌ Failed to send system ready notification")
    
    return success


async def request_firmware_version(client: ActionsLinkClient):
    """Example: Send a request and wait for response."""
    logger.info("Requesting firmware version...")
    
    try:
        request = ToMcuRequest()
        request.get_mcu_firmware_version.SetInParent()
        
        response = await client.send_request(
            request,
            expected_response='get_mcu_firmware_version',
            timeout=5000
        )
        
        if response:
            logger.info(f"✅ Firmware version: {response}")
            return response
        else:
            logger.error("❌ Failed to get firmware version")
            return None
            
    except Exception as e:
        logger.error(f"Error requesting firmware version: {e}", exc_info=True)
        return None


# ============================================================================
# Main Application
# ============================================================================

async def main_application(client: ActionsLinkClient):
    """
    Main application logic.
    
    This function runs your application code while events are handled
    in parallel via the registered callback.
    """
    logger.info("=" * 60)
    logger.info("Application started - events will be handled in parallel")
    logger.info("=" * 60)
    
    # 1. Send initial system ready notification
    await send_system_ready(client)
    await asyncio.sleep(1)
    
    # 2. Request firmware version (this waits for response)
    await request_firmware_version(client)
    await asyncio.sleep(1)
    
    # 3. Start periodic updates task (runs concurrently)
    periodic_task = asyncio.create_task(
        send_periodic_updates(client, interval=3.0)
    )
    
    try:
        # 4. Your application logic here
        # Events will be received and handled via on_event_received() callback
        # while this code runs
        
        logger.info("Application running... Events are being handled in parallel")
        logger.info("Press Ctrl+C to exit")
        
        # Example: Run for 30 seconds, or until interrupted
        await asyncio.sleep(30)
        
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    finally:
        # Cleanup: Cancel periodic task
        periodic_task.cancel()
        try:
            await periodic_task
        except asyncio.CancelledError:
            pass


async def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Example: Parallel event handling and message sending',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('port', help='Serial port (e.g., /dev/serial0 or COM3)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose logging')
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.Client").setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.API").setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.Transport").setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.HDLC").setLevel(logging.DEBUG)
    
    # Connect to Actions module
    try:
        async with ActionsLinkClient(args.port) as client:
            # Register event handler - this will be called automatically
            # whenever an event arrives from the MCU
            client.on_event(on_event_received)
            
            logger.info("Event handler registered - events will be processed automatically")
            
            # Run your application logic
            # Events will be handled in parallel via the callback
            await main_application(client)
            
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    except Exception as e:
        logger.error(f"Error: {e}", exc_info=True)
        return 1
    
    return 0


if __name__ == '__main__':
    sys.exit(asyncio.run(main()))

