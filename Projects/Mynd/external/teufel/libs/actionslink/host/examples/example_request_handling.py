#!/usr/bin/env python3
"""
Example: Handling Requests from MCU

This example demonstrates how to handle incoming requests from the MCU
(such as set_power_state, set_led) and send responses.

Key concepts:
1. Register request handlers using client.on_request(request_type, handler)
2. Handler receives (seq, request_field) - send response using client.send_response()
3. Requests are handled automatically in parallel with your application code
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

# Import protobuf messages
try:
    from message_pb2 import FromMcuRequest, ToMcuResponse, ToMcuEvent
    import system_pb2
    import common_pb2
    import host_pb2 as host_pb
    import leds_pb2 as leds_pb
except ImportError as e:
    print(f"ERROR: Failed to import protobuf files: {e}", file=sys.stderr)
    sys.exit(1)

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("request_example")


# ============================================================================
# Request Handlers - Called automatically when requests arrive from MCU
# ============================================================================

async def handle_set_power_state(client: ActionsLinkClient, seq: int, power_state):
    """
    Handle set_power_state request from MCU.
    
    Args:
        client: ActionsLinkClient instance (for sending responses/events)
        seq: Sequence number of the request (must be included in response)
        power_state: System.PowerState message from MCU
    """
    power_mode = power_state.mode
    logger.info(f"📨 Received set_power_state request (seq={seq}): {power_mode}")
    
    # Process the power state change
    # In a real application, you would:
    # - Update your application state
    # - Perform actions based on the power state
    # - Send notify_power_state to report RPi power state to MCU
    
    if power_mode == system_pb2.SystemPowerMode.ON:
        logger.info("   → ON: System fully operational")
        event = ToMcuEvent()
        event.notify_power_state.mode = system_pb2.SystemPowerMode.ON
        await client.send_event(event)
        
    elif power_mode == system_pb2.SystemPowerMode.OFF:
        logger.info("   → OFF: Shutting down...")
        event = ToMcuEvent()
        event.notify_power_state.mode = system_pb2.SystemPowerMode.OFF
        await client.send_event(event)
    elif power_mode == system_pb2.PowerState.SystemPowerMode.STANDBY:
        logger.info("   → STANDBY")
        event = ToMcuEvent()
        event.notify_power_state.mode = system_pb2.PowerState.SystemPowerMode.STANDBY
        await client.send_event(event)
    else:
        logger.warning(f"   → Unknown power mode: {power_mode}")
    
    # Send response to MCU (required!)
    response = ToMcuResponse()
    response.seq = seq
    response.set_power_state.status.code = common_pb2.Error.Code.Success
    await client.send_response(response)
    
    logger.info(f"   ✅ Processed power state change and sent response")


async def handle_set_led(client: ActionsLinkClient, seq: int, set_led):
    """
    Handle set_led request from MCU.
    
    Args:
        client: ActionsLinkClient instance (for sending responses)
        seq: Sequence number of the request (must be included in response)
        set_led: SetLed message from MCU (contains led and color enum)
    """
    led = set_led.led
    color = set_led.color
    logger.info(f"📨 Received set_led request (seq={seq}): LED={led}, color={color}")
    
    # Process the LED change
    # In a real application, you would control the actual LED hardware
    # For example: control_led_hardware(led, color)
    
    # Send response to MCU (required!)
    response = ToMcuResponse()
    response.seq = seq
    response.set_led.status.code = common_pb2.Error.Code.Success
    await client.send_response(response)
    
    logger.info(f"   ✅ LED updated and response sent")


# ============================================================================
# Event Handler - Handle events from MCU
# ============================================================================

def handle_mcu_event(event):
    """Handle events from MCU."""
    event_type = event.WhichOneof("Event")
    if event_type:
        logger.info(f"📨 Received event: {event_type}")
        
        if event_type == "notify_button_event":
            button = event.notify_button_event
            logger.info(f"   Button: bitfield={button.button_bitfield}, state={button.input_state}")
        
        elif event_type == "notify_battery_level":
            level = event.notify_battery_level
            logger.info(f"   Battery level: {level}%")
        
        elif event_type == "notify_charger_status":
            status = event.notify_charger_status
            logger.info(f"   Charger status: {status}")
        
        elif event_type == "notify_aux_connected":
            connected = event.notify_aux_connected
            logger.info(f"   Aux connected: {connected}")
        
        elif event_type == "notify_usb_connected":
            connected = event.notify_usb_connected
            logger.info(f"   USB connected: {connected}")


# ============================================================================
# Application Code
# ============================================================================

async def my_application(client: ActionsLinkClient):
    """
    Your application logic - runs while requests and events are handled in parallel.
    """
    logger.info("Application started - waiting for requests from MCU...")
    logger.info("Requests will be handled automatically via registered handlers")
    
    # Your application code here
    # Requests and events from MCU will be handled automatically
    await asyncio.sleep(60)
    
    logger.info("Application finished")


# ============================================================================
# Main Entry Point
# ============================================================================

async def main():
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/serial0'
    
    # Connect to Actions module
    async with ActionsLinkClient(port) as client:
        # Register event handler
        client.on_event(handle_mcu_event)
        
        # Register request handlers
        # These will be called automatically when requests arrive from MCU
        # Note: We capture 'client' in the lambda closure to pass it to handlers
        client.on_request('set_power_state', 
                         lambda seq, req: asyncio.create_task(handle_set_power_state(client, seq, req)))
        client.on_request('set_led',
                         lambda seq, req: asyncio.create_task(handle_set_led(client, seq, req)))
        
        logger.info("Request handlers registered:")
        logger.info("  - set_power_state: Handle power state changes from MCU")
        logger.info("  - set_led: Handle LED control requests from MCU")
        logger.info("Event handler registered for MCU events")
        
        # Run your application
        await my_application(client)


if __name__ == '__main__':
    asyncio.run(main())











