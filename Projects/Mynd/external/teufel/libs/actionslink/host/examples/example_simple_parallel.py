#!/usr/bin/env python3
"""
Simple Example: Parallel Event Handling

This is a minimal example showing how events are handled in parallel
with your application code.

Key concept: Register an event handler, then do your work.
Events will be received and processed automatically in the background.
"""

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
from message_pb2 import ToMcuEvent, FromMcuEvent
import audio_pb2

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("simple_example")


# ============================================================================
# Step 1: Define your event handler
# ============================================================================

def handle_mcu_event(event: FromMcuEvent):
    """
    This function is called automatically whenever an event arrives from the MCU.
    
    It runs in the same event loop as your application, so events are processed
    in parallel with your code - you don't need to poll or check for events!
    """
    event_type = event.WhichOneof("Event")
    if event_type:
        logger.info(f"📨 Received event: {event_type}")
        
        # Handle specific events
        if event_type == "notify_battery_level":
            level = event.notify_battery_level
            logger.info(f"   Battery level: {level}%")
        
        elif event_type == "notify_charger_status":
            status = event.notify_charger_status
            logger.info(f"   Charger status: {status}")
        
        elif event_type == "notify_button_event":
            button = event.notify_button_event
            logger.info(f"   Button: bitfield={button.button_bitfield}, state={button.input_state}")
        
        elif event_type == "notify_aux_connected":
            connected = event.notify_aux_connected
            logger.info(f"   Aux connected: {connected}")
        
        elif event_type == "notify_usb_connected":
            connected = event.notify_usb_connected
            logger.info(f"   USB connected: {connected}")
        
        elif event_type == "notify_eco_mode":
            enabled = event.notify_eco_mode
            logger.info(f"   Eco mode: {'ON' if enabled else 'OFF'}")


# ============================================================================
# Step 2: Your application code
# ============================================================================

async def my_application(client: ActionsLinkClient):
    """
    Your application logic - runs while events are handled in parallel.
    """
    logger.info("Application started")
    
    # Send some messages
    for i in range(5):
        # Create and send an event
        volume = audio_pb2.Volume()
        volume.percent = 50 + i * 10
        volume.is_muted = False
        
        event = ToMcuEvent()
        event.notify_volume.CopyFrom(volume)
        
        logger.info(f"Sending volume: {volume.percent}%")
        await client.send_event(event)
        
        # Wait a bit - during this time, events from MCU will be
        # automatically received and handled by handle_mcu_event()
        await asyncio.sleep(2)
    
    logger.info("Application finished")


# ============================================================================
# Step 3: Connect and run
# ============================================================================

async def main():
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/serial0'
    
    # Connect to Actions module
    async with ActionsLinkClient(port) as client:
        # Register event handler - events will be processed automatically
        client.on_event(handle_mcu_event)
        
        logger.info("Event handler registered - events will be processed in parallel")
        
        # Run your application
        # While this runs, events from MCU will be automatically handled
        await my_application(client)


if __name__ == '__main__':
    asyncio.run(main())

