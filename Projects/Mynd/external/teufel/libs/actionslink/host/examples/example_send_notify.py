#!/usr/bin/env python3
"""
Example: Send notify messages via ActionsLink protocol and subscribe to events.

Usage:
    # Send notify messages
    python3 example_send_notify.py <port> <notify_type> [value] [--verbose]
    
    # Listen to events from MCU
    python3 example_send_notify.py <port> listen [--verbose]

Examples:
    # Send notify_system_ready
    python3 example_send_notify.py /dev/serial0 system_ready

    # Send notify_stream_state (true) with verbose logging
    python3 example_send_notify.py /dev/serial0 stream_state true --verbose

    # Send notify_stream_state (false)
    python3 example_send_notify.py /dev/serial0 stream_state false
    
    # Listen to events from MCU
    python3 example_send_notify.py /dev/serial0 listen
"""

import argparse
import asyncio
import logging
import sys
from pathlib import Path

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
    from message_pb2 import ToMcuEvent, FromMcuEvent
    import common_pb2
    import audio_pb2
    import system_pb2
    import battery_pb2
except ImportError as e:
    print(f"ERROR: Failed to import protobuf files: {e}", file=sys.stderr)
    print("Make sure protobuf files are generated in actionslink/host/generated/", file=sys.stderr)
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s: %(message)s'
)
logger = logging.getLogger("example_send_notify")


async def send_notify_system_ready(client: ActionsLinkClient) -> bool:
    """Send notify_system_ready event"""
    logger.info("Sending notify_system_ready...")
    
    event = ToMcuEvent()
    event.notify_system_ready.SetInParent()
    
    return await client.send_event(event)


async def send_notify_stream_state(client: ActionsLinkClient, state: bool) -> bool:
    """Send notify_stream_state event"""
    logger.info(f"Sending notify_stream_state: {state}...")
    
    event = ToMcuEvent()
    event.notify_stream_state = state
    
    return await client.send_event(event)


async def send_notify_volume(client: ActionsLinkClient, percent: int, is_muted: bool = False) -> bool:
    """Send notify_volume event"""
    logger.info(f"Sending notify_volume: {percent}% (muted={is_muted})...")
    
    if not audio_pb2:
        logger.error("audio_pb2 not available")
        return False
    
    volume = audio_pb2.Volume()
    volume.percent = percent
    volume.is_muted = is_muted
    
    event = ToMcuEvent()
    event.notify_volume.CopyFrom(volume)
    
    return await client.send_event(event)


async def send_notify_power_state(client: ActionsLinkClient, mode: str) -> bool:
    """Send notify_power_state event"""
    logger.info(f"Sending notify_power_state: {mode}...")
    
    if not system_pb2:
        logger.error("system_pb2 not available")
        return False
    
    # Map mode string to enum
    mode_map = {
        'off': system_pb2.PowerState.OFF,
        'on': system_pb2.PowerState.ON,
        'standby': system_pb2.PowerState.STANDBY,
        'shutdown_request': system_pb2.PowerState.SHUTDOWN_REQUEST,
    }
    
    if mode.lower() not in mode_map:
        logger.error(f"Invalid power mode: {mode}. Valid modes: {list(mode_map.keys())}")
        return False
    
    power_state = system_pb2.PowerState()
    power_state.mode = mode_map[mode.lower()]
    
    event = ToMcuEvent()
    event.notify_power_state.CopyFrom(power_state)
    
    return await client.send_event(event)


def format_event(event: FromMcuEvent) -> str:
    """
    Format a FromMcuEvent for display.
    
    Args:
        event: FromMcuEvent message
        
    Returns:
        Formatted string describing the event
    """
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
            # Try to get enum name from protobuf enum wrapper
            try:
                # Access the enum descriptor to get the name
                enum_descriptor = battery_pb2.ChargerStatus.DESCRIPTOR
                enum_value = enum_descriptor.values_by_number.get(charger_status_enum)
                if enum_value:
                    status_name = enum_value.name
                else:
                    status_name = f"Unknown({charger_status_enum})"
            except (AttributeError, KeyError):
                # Fallback to manual mapping
                status_map = {
                    0: "NotConnected",
                    1: "Active",
                    2: "Inactive",
                    3: "Fault"
                }
                status_name = status_map.get(charger_status_enum, f"Unknown({charger_status_enum})")
            return f"Charger Status: {status_name}"
        
        elif event_type == "notify_battery_friendly_charging":
            enabled = event.notify_battery_friendly_charging
            return f"Battery Friendly Charging: {'enabled' if enabled else 'disabled'}"
        
        elif event_type == "notify_eco_mode":
            enabled = event.notify_eco_mode
            return f"Eco Mode: {'enabled' if enabled else 'disabled'}"
        
        elif event_type.startswith("notify_battery_history_buffer_"):
            buffer = event.__getattribute__(event_type)
            period = event_type.replace("notify_battery_history_buffer_", "")
            return f"Battery History ({period}): {len(buffer)} bytes"
        
        else:
            # Generic handler for unknown event types
            value = getattr(event, event_type)
            return f"{event_type}: {value}"
    
    except Exception as e:
        logger.debug(f"Error formatting event {event_type}: {e}", exc_info=True)
        return f"{event_type}: (error formatting: {e})"


def create_event_handler():
    """
    Create an event handler callback that formats and logs incoming events.
    
    Returns:
        Callback function for handling events
    """
    def on_event(event: FromMcuEvent):
        """Handle incoming event from MCU"""
        event_type = event.WhichOneof("Event")
        if event_type:
            formatted = format_event(event)
            logger.info(f"📨 Event from MCU: {formatted}")
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug(f"  Raw event type: {event_type}")
                logger.debug(f"  Event object: {event}")
        else:
            logger.warning("Received empty event from MCU")
    
    return on_event


async def listen_mode(client: ActionsLinkClient):
    """
    Listen mode: subscribe to events and wait for incoming messages.
    
    Args:
        client: Connected ActionsLinkClient
    """
    logger.info("Entering listen mode. Waiting for events from MCU...")
    logger.info("Press Ctrl+C to exit")
    
    # Register event handler
    event_handler = create_event_handler()
    client.on_event(event_handler)
    
    try:
        # Keep running until interrupted
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        logger.info("Listen mode interrupted by user")


async def main():
    parser = argparse.ArgumentParser(
        description='Send notify messages via ActionsLink protocol or listen to events',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Send notify messages
  %(prog)s /dev/serial0 system_ready
  %(prog)s /dev/serial0 stream_state true
  %(prog)s /dev/serial0 stream_state false
  %(prog)s /dev/serial0 volume 50
  %(prog)s /dev/serial0 power_state on
  
  # Listen to events from MCU
  %(prog)s /dev/serial0 listen
        """
    )
    parser.add_argument('port', help='Serial port (e.g., /dev/serial0 or COM3)')
    parser.add_argument('notify_type', 
                       choices=['system_ready', 'stream_state', 'volume', 'power_state', 'listen'],
                       help='Type of notify message to send, or "listen" to subscribe to events')
    parser.add_argument('value', nargs='?', 
                       help='Value for the notify message (required for stream_state, volume, power_state)')
    parser.add_argument('--timeout', type=int, default=5000,
                       help='Timeout in milliseconds (default: 5000)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose logging (show raw data being sent)')
    
    args = parser.parse_args()
    
    # Set logging level based on verbose flag
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
        # Also set specific loggers to DEBUG
        logging.getLogger("ActionsLink.Client").setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.API").setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.Transport").setLevel(logging.DEBUG)
        logging.getLogger("ActionsLink.HDLC").setLevel(logging.DEBUG)
        logger.setLevel(logging.DEBUG)
    
    # Validate arguments
    if args.notify_type == 'listen':
        # Listen mode - no value needed
        pass
    elif args.notify_type in ['stream_state', 'volume', 'power_state'] and not args.value:
        parser.error(f"{args.notify_type} requires a value argument")
    
    # Use context manager for automatic connection/disconnection
    try:
        async with ActionsLinkClient(args.port) as client:
            # Register event handler for all modes (to see any incoming events)
            event_handler = create_event_handler()
            client.on_event(event_handler)
            
            # Handle listen mode
            if args.notify_type == 'listen':
                await listen_mode(client)
                return 0
            
            # Send notify message
            success = False
            if args.notify_type == 'system_ready':
                success = await send_notify_system_ready(client)
            elif args.notify_type == 'stream_state':
                state = args.value.lower() in ('true', '1', 'yes', 'on')
                success = await send_notify_stream_state(client, state)
            elif args.notify_type == 'volume':
                try:
                    percent = int(args.value)
                    if percent < 0 or percent > 100:
                        logger.error("Volume must be between 0 and 100")
                        return 1
                    success = await send_notify_volume(client, percent)
                except ValueError:
                    logger.error(f"Invalid volume value: {args.value}")
                    return 1
            elif args.notify_type == 'power_state':
                success = await send_notify_power_state(client, args.value)
            
            if success:
                logger.info("Notify message sent successfully!")
                # Small delay to ensure any final processing completes
                await asyncio.sleep(0.1)
                return 0
            else:
                logger.error("Failed to send notify message")
                return 1
        
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
        return 1
    except Exception as e:
        logger.error(f"Error: {e}", exc_info=True)
        return 1


if __name__ == '__main__':
    sys.exit(asyncio.run(main()))
