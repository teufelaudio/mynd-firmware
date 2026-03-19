#!/usr/bin/env python3
"""
ActionsLink Unified Client

Provides a simple, unified interface for using ActionsLink protocol.
Supports both simple testing scenarios and full application usage.
"""

import asyncio
import logging
from typing import Optional, Callable, Any
from pathlib import Path
import sys

logger = logging.getLogger("ActionsLink.Client")

# Import internal layers
from actionslink_hdlc import HdlcFraming
from actionslink_transport import ActionsLinkTransport
from actionslink_api import ActionsLinkAPI

# Add generated directory to path for protobuf imports
SCRIPT_DIR = Path(__file__).resolve().parent
GENERATED_DIR = SCRIPT_DIR / "generated"
if str(GENERATED_DIR) not in sys.path:
    sys.path.insert(0, str(GENERATED_DIR))

try:
    from message_pb2 import FromMcu, ToMcu, ToMcuRequest, ToMcuResponse, ToMcuEvent, FromMcuRequest
except ImportError as e:
    logger.warning(f"Protobuf files not found: {e}. Some functionality may be limited.")
    FromMcu = None
    ToMcu = None
    ToMcuRequest = None
    ToMcuResponse = None
    ToMcuEvent = None
    FromMcuRequest = None


class ActionsLinkClient:
    """
    Unified client for ActionsLink protocol.
    
    This client wraps all protocol layers (HDLC, Transport, API) and provides
    a simple interface for both testing and full applications.
    
    **Parallel Event Handling:**
    Events from the MCU are handled automatically in parallel with your application
    code. Register an event handler with on_event(), and it will be called whenever
    an event arrives - no polling needed!
    
    Example for testing:
        async with ActionsLinkClient('/dev/serial0') as client:
            event = ToMcuEvent()
            event.notify_system_ready.SetInParent()
            success = await client.send_event(event)
    
    Example for full application with parallel event handling:
        async with ActionsLinkClient('/dev/serial0') as client:
            # Register event handler - called automatically when events arrive
            def on_event(event):
                event_type = event.WhichOneof('Event')
                print(f"Event received: {event_type}")
            
            client.on_event(on_event)
            
            # Your application code - events are handled in parallel
            while True:
                await send_some_message(client)
                await asyncio.sleep(1)
                # Events from MCU will be processed automatically during this time
    
    See examples/example_parallel_events.py and examples/example_simple_parallel.py for complete examples.
    """
    
    def __init__(self, port: str):
        """
        Initialize ActionsLink client.
        
        Args:
            port: Serial port path (e.g., '/dev/serial0' or 'COM3')
        """
        self.port = port
        self._hdlc: Optional[HdlcFraming] = None
        self._transport: Optional[ActionsLinkTransport] = None
        self._api: Optional[ActionsLinkAPI] = None
        self._connected = False
        
        # Internal handler for routing messages
        self._internal_handler = _InternalHandler(self)
        
        # Event and request callbacks
        self._event_callbacks: list[Callable] = []
        self._request_handlers: dict[str, Callable] = {}
    
    async def connect(self) -> bool:
        """
        Connect to the Actions module.
        
        Returns:
            True if connection was successful, False otherwise
        """
        if self._connected:
            logger.warning("Already connected")
            return True
        
        try:
            # Initialize transport layer first (HDLC will call it)
            self._transport = ActionsLinkTransport(self._internal_handler)
            
            # Initialize HDLC layer (HDLC calls transport, transport calls internal handler)
            self._hdlc = HdlcFraming(self.port, self._transport)
            if not await self._hdlc.open():
                logger.error("Failed to open serial port")
                return False
            
            # Set HDLC in transport layer
            self._transport.set_hdlc(self._hdlc)
            
            # Initialize API layer (client itself is the handler for events)
            self._api = ActionsLinkAPI(self._transport, self)
            await self._api.start()
            
            # Register any request handlers that were set before connection
            for request_type, handler in self._request_handlers.items():
                self._api.register_request_handler(request_type, handler)
            
            # Start HDLC RX task
            self._hdlc.start()
            
            # Small delay to let things settle
            await asyncio.sleep(0.1)
            
            self._connected = True
            logger.info(f"Connected to {self.port}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to connect: {e}", exc_info=True)
            await self._cleanup()
            return False
    
    async def disconnect(self):
        """Disconnect from the Actions module."""
        if not self._connected:
            return
        
        self._connected = False
        await self._cleanup()
        logger.info("Disconnected")
    
    async def _cleanup(self):
        """Internal cleanup method."""
        try:
            if self._api:
                await self._api.stop()
        except Exception as e:
            logger.debug(f"Error stopping API: {e}")
        
        try:
            if self._hdlc:
                await self._hdlc.stop()
        except Exception as e:
            logger.debug(f"Error stopping HDLC: {e}")
        
        try:
            if self._hdlc:
                await self._hdlc.close()
        except Exception as e:
            logger.debug(f"Error closing HDLC: {e}")
        
        self._hdlc = None
        self._transport = None
        self._api = None
    
    async def __aenter__(self):
        """Async context manager entry."""
        await self.connect()
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit."""
        await self.disconnect()
    
    async def send_event(self, event: ToMcuEvent, timeout: Optional[int] = None) -> bool:
        """
        Send an event (notify message) to the MCU.
        
        Args:
            event: ToMcuEvent message to send
            timeout: Timeout in milliseconds (default: transport default)
        
        Returns:
            True if message was sent and acknowledged, False otherwise
        """
        if not self._connected or not self._api:
            logger.error("Not connected")
            return False
        
        return await self._api.send_event(event, wait_for_ack=True, timeout=timeout)
    
    async def send_request(self, request: ToMcuRequest, expected_response: Optional[str] = None, timeout: Optional[int] = None) -> Optional[Any]:
        """
        Send a request to the MCU and optionally wait for response.
        
        Args:
            request: ToMcuRequest message to send
            expected_response: Name of expected response field (e.g., 'get_mcu_firmware_version')
            timeout: Timeout in milliseconds (default: 5000)
        
        Returns:
            Response value if expected_response is provided and response received, None otherwise
        """
        if not self._connected or not self._api:
            logger.error("Not connected")
            return None
        
        return await self._api.send_request(request, expected_response=expected_response, timeout=timeout)
    
    def on_event(self, callback: Callable):
        """
        Register a callback for incoming events from the MCU.
        
        Args:
            callback: Callable that takes an event object as argument
        """
        self._event_callbacks.append(callback)
    
    def on_request(self, request_type: str, handler: Callable):
        """
        Register a handler for incoming requests from the MCU.
        
        Args:
            request_type: Type of request (e.g., 'set_power_state')
            handler: Callable that takes (seq: int, request_field) as arguments
        """
        self._request_handlers[request_type] = handler
        # Also register with API layer
        if self._api:
            self._api.register_request_handler(request_type, handler)
    
    def handle_event(self, event):
        """
        Handle incoming event from API layer.
        This method is called by the API layer when an event is received.
        """
        # Route to registered callbacks
        for callback in self._event_callbacks:
            try:
                callback(event)
            except Exception as e:
                logger.error(f"Error in event callback: {e}", exc_info=True)
    
    async def send_response(self, response: ToMcuResponse):
        """
        Send a response to a request from the MCU.
        
        Args:
            response: ToMcuResponse message to send
        """
        if not self._connected or not self._api:
            logger.error("Not connected")
            return False
        
        if not FromMcu or not ToMcu:
            logger.error("Protobuf files not available")
            return False
        
        message = ToMcu()
        message.response.CopyFrom(response)
        
        payload = message.SerializeToString()
        
        # Send message - transport layer will wait for ACK and retry automatically
        return await self._api.transport.send_proto_message(payload, wait_for_ack=True)
    
    @property
    def is_connected(self) -> bool:
        """Check if client is connected."""
        return self._connected


class _InternalHandler:
    """
    Internal handler that routes frames from transport to API layer.
    """
    
    def __init__(self, client: ActionsLinkClient):
        self.client = client
    
    def handle_rx(self, payload: bytes):
        """Handle received protobuf message from transport layer."""
        # Route to API layer which will parse and handle appropriately
        if self.client._api:
            self.client._api.handle_rx(payload)

