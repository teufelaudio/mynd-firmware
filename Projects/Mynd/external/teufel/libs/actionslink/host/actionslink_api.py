#!/usr/bin/env python3
"""
ActionsLink High-Level API

Provides async methods for sending commands and handling events/responses.
"""

import asyncio
import logging
from typing import Optional, Callable, Any

logger = logging.getLogger("ActionsLink.API")

import sys
import os
from pathlib import Path

# Add generated directory to path for protobuf imports
SCRIPT_DIR = Path(__file__).resolve().parent
GENERATED_DIR = SCRIPT_DIR / "generated"
if str(GENERATED_DIR) not in sys.path:
    sys.path.insert(0, str(GENERATED_DIR))

try:
    # Import generated protobuf files
    from message_pb2 import FromMcu, ToMcu, ToMcuRequest, ToMcuResponse, ToMcuEvent, FromMcuRequest
    import common_pb2
    import audio_pb2
    import system_pb2
except ImportError as e:
    logger.warning(f"Protobuf files not found: {e}. Some functionality may be limited.")
    FromMcu = None
    ToMcu = None
    ToMcuRequest = None
    ToMcuResponse = None
    ToMcuEvent = None
    FromMcuRequest = None
    common_pb2 = None
    audio_pb2 = None
    system_pb2 = None


class ActionsLinkAPI:
    """High-level API for ActionsLink protocol"""
    
    def __init__(self, transport, handler=None):
        self.transport = transport
        self.handler = handler
        self._seq = 1
        self._active_request: Optional[asyncio.Future] = None
        self._active_request_expected: Optional[str] = None
        self._event_callbacks: list[Callable] = []
        self._request_callbacks: dict[str, Callable] = {}
        self.running = False
    
    async def start(self):
        """Start API layer"""
        self.running = True
        await self.transport.start()
    
    async def stop(self):
        """Stop API layer"""
        self.running = False
        await self.transport.stop()
    
    def _next_seq(self) -> int:
        """Get next sequence number"""
        seq = self._seq
        self._seq = (self._seq + 1) & 0xFFFFFFFF
        if self._seq == 0:
            self._seq = 1
        return seq
    
    async def send_request(self, request: ToMcuRequest, expected_response: Optional[str] = None, timeout: Optional[int] = None) -> Optional[Any]:
        """
        Send a request and optionally wait for response.
        
        The transport layer will automatically wait for ACK and retry if needed.
        This method additionally waits for the protobuf response if expected_response is provided.
        """
        if self._active_request is not None:
            raise RuntimeError("Request already pending")
        
        if not FromMcu or not ToMcu:
            logger.error("Protobuf files not available")
            return None
        
        request.seq = self._next_seq()
        
        message = ToMcu()
        message.request.CopyFrom(request)
        
        payload = message.SerializeToString()
        
        if expected_response:
            self._active_request = asyncio.get_running_loop().create_future()
            self._active_request_expected = expected_response
        
        # Send message - transport layer will wait for ACK and retry automatically
        if not await self.transport.send_proto_message(payload, wait_for_ack=True, timeout=timeout):
            logger.error("Failed to send request (ACK timeout or error)")
            if self._active_request:
                self._active_request = None
                self._active_request_expected = None
            return None
        
        # If we're expecting a response, wait for it
        if expected_response:
            try:
                response_timeout_ms = timeout if timeout else 5000
                result = await asyncio.wait_for(self._active_request, timeout=response_timeout_ms / 1000.0)
                self._active_request = None
                self._active_request_expected = None
                return result
            except asyncio.TimeoutError:
                logger.error("Response timeout")
                self._active_request = None
                self._active_request_expected = None
                return None
    
    def handle_rx(self, payload: bytes):
        """Handle received protobuf message"""
        if not FromMcu:
            return
        
        if not payload:
            logger.warning("Received empty payload")
            return
        
        try:
            import warnings
            # Convert RuntimeWarnings to exceptions so we can catch them
            with warnings.catch_warnings():
                warnings.filterwarnings("error", category=RuntimeWarning)
                message = FromMcu()
                message.ParseFromString(payload)
        except Exception as e:
            # Catch both exceptions and RuntimeWarnings (converted to exceptions)
            logger.error(f"Failed to decode protobuf message: {e}")
            logger.error(f"Payload (hex): {payload.hex()}")
            logger.error(f"Payload (len): {len(payload)} bytes")
            logger.error(f"Payload (first 32 bytes): {payload[:32].hex() if len(payload) >= 32 else payload.hex()}")
            return
        
        if message.HasField('response'):
            self._handle_response(message.response)
        elif message.HasField('event'):
            self._handle_event(message.event)
        elif message.HasField('request'):
            self._handle_request(message.request)
    
    def _handle_response(self, response: ToMcuResponse):
        """Handle response message"""
        if self._active_request is None:
            logger.debug("Response received but no active request")
            return
        
        response_type = response.WhichOneof("Response")
        if response_type != self._active_request_expected:
            logger.warning(f"Response type mismatch: expected {self._active_request_expected}, got {response_type}")
            return
        
        if not self._active_request.done():
            response_value = getattr(response, response_type)
            self._active_request.set_result(response_value)
    
    def _handle_event(self, event):
        """Handle event message"""
        event_type = event.WhichOneof("Event")
        logger.debug(f"Event received: {event_type}")
        
        if self.handler:
            self.handler.handle_event(event)
        
        for callback in self._event_callbacks:
            try:
                callback(event)
            except Exception as e:
                logger.error(f"Error in event callback: {e}")
    
    def register_event_callback(self, callback: Callable):
        """Register callback for events"""
        self._event_callbacks.append(callback)
    
    def register_request_handler(self, request_type: str, handler: Callable):
        """Register handler for incoming requests from MCU"""
        self._request_callbacks[request_type] = handler
    
    def _handle_request(self, request: FromMcuRequest):
        """Handle incoming request from MCU"""
        request_type = request.WhichOneof("Request")
        if not request_type:
            logger.warning("Received request with no request type")
            return
        
        logger.debug(f"Request received: {request_type}")
        
        if request_type in self._request_callbacks:
            try:
                handler = self._request_callbacks[request_type]
                # Call handler with seq and the request field
                handler(request.seq, getattr(request, request_type))
            except Exception as e:
                logger.error(f"Error in request handler for {request_type}: {e}", exc_info=True)
        else:
            logger.warning(f"No handler registered for request type: {request_type}")
    
    async def get_firmware_version(self):
        """Get MCU firmware version"""
        if not ToMcuRequest:
            return None
        
        request = ToMcuRequest()
        request.get_mcu_firmware_version.SetInParent()
        return await self.send_request(request, 'get_mcu_firmware_version')
    
    async def set_power_state(self, power_state):
        """Set power state"""
        if not ToMcuRequest:
            return None
        
        request = ToMcuRequest()
        request.set_power_state.CopyFrom(power_state)
        return await self.send_request(request, 'set_power_state')
    
    async def set_volume(self, volume_control):
        """Set volume"""
        if not ToMcuRequest:
            return None
        
        request = ToMcuRequest()
        request.set_volume.CopyFrom(volume_control)
        return await self.send_request(request, 'set_volume')
    
    async def send_event(self, event: ToMcuEvent, wait_for_ack: bool = True, timeout: Optional[int] = None) -> bool:
        """
        Send an event (notify message) to the MCU.
        
        Args:
            event: ToMcuEvent message to send
            wait_for_ack: If True, wait for ACK before returning
            timeout: Timeout in milliseconds
        
        Returns:
            True if message was sent and acknowledged (if wait_for_ack=True), False otherwise
        """
        if not ToMcu or not ToMcuEvent:
            logger.error("Protobuf files not available")
            return False
        
        message = ToMcu()
        message.event.CopyFrom(event)
        
        payload = message.SerializeToString()
        
        # Verbose logging: show protobuf message details
        if logger.isEnabledFor(logging.DEBUG):
            event_type = event.WhichOneof("Event")
            logger.debug(f"Protobuf message: ToMcu.event.{event_type}")
            logger.debug(f"Protobuf payload (hex): {payload.hex()}")
            logger.debug(f"Protobuf payload (len): {len(payload)} bytes")
        
        # Send message - transport layer will wait for ACK and retry automatically
        return await self.transport.send_proto_message(payload, wait_for_ack=wait_for_ack, timeout=timeout)