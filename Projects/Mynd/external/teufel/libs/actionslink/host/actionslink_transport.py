#!/usr/bin/env python3
"""
ActionsLink Transport Layer

Implements the link layer (frame building/parsing, CRC) and transport layer
(ACK/NACK, retries, transaction management) for ActionsLink protocol.
"""

import asyncio
import logging
import time
from typing import Optional, Dict, Any

logger = logging.getLogger("ActionsLink.Transport")

PACKET_TYPE_ACK = 0
PACKET_TYPE_PROTOBUF = 1

ACK_OK = 0
ACK_ERROR_PACKET_TYPE = 1
ACK_ERROR_PAYLOAD_CHECKSUM = 2
ACK_ERROR_PAYLOAD_LENGTH = 3
ACK_ERROR_BUSY = 4

FRAME_HEADER_LEN = 8
FRAME_START_BYTE = 0x55
FRAME_MAX_PAYLOAD = 256

ACK_TIMEOUT_MS = 200  # ACK timeout in milliseconds
ACK_MAX_RETRIES = 2  # 1 retry = 2 total attempts (initial + 1 retry)
ACK_RETRY_DELAY_MS = 200  # Delay between retry attempts in milliseconds

CRC8_TABLE = (
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
    0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
    0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
    0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
    0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
    0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
    0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
    0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
    0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
    0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
    0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
    0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
    0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
    0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
    0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
    0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
    0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
    0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
    0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
    0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
    0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
    0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
    0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
    0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
    0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3,
)


def crc8(data: bytes) -> int:
    """Calculate CRC-8 checksum"""
    crc = 0
    for byte in data:
        crc = CRC8_TABLE[crc ^ byte]
    return crc


class ActionsLinkTransport:
    """ActionsLink transport layer with link protocol"""
    
    def __init__(self, handler):
        self.handler = handler
        self.hdlc = None
        self._tx_counter = 0
        self._pending_frame: Optional[Dict[str, Any]] = None
        self._pending_lock = asyncio.Lock()
        self._pending_cleared_event = asyncio.Event()
        self._pending_cleared_event.set()
        self._pending_success = False  # Track if ACK was successful
        self.running = False
        self._service_task = None
    
    def set_hdlc(self, hdlc):
        """Set HDLC framing layer"""
        self.hdlc = hdlc
    
    async def start(self):
        """Start transport layer"""
        self.running = True
        self._service_task = asyncio.create_task(self._service_loop())
    
    async def stop(self):
        """Stop transport layer"""
        self.running = False
        if self._service_task:
            self._service_task.cancel()
            try:
                await self._service_task
            except asyncio.CancelledError:
                pass
        async with self._pending_lock:
            self._pending_frame = None
    
    def _next_tx_id(self) -> int:
        """Get next transaction ID"""
        self._tx_counter = (self._tx_counter + 1) & 0xFF
        return self._tx_counter
    
    def _build_frame(self, packet_type: int, value: int, transaction_id: int, payload: bytes) -> bytes:
        """Build ActionsLink frame"""
        length = len(payload)
        header = bytearray(FRAME_HEADER_LEN)
        header[0] = FRAME_START_BYTE
        header[1] = ((value & 0x1F) << 3) | (packet_type & 0x07)
        header[2] = transaction_id & 0xFF
        header[3] = length & 0xFF
        header[4] = (length >> 8) & 0xFF
        header[5] = crc8(payload) if payload else 0
        header[6] = 0
        header[7] = crc8(header[:7])
        return bytes(header + payload)
    
    async def _send_raw_frame(self, frame: bytes) -> bool:
        """Send frame through HDLC layer"""
        if not self.hdlc:
            return False
        try:
            await self.hdlc.send(frame)
            return True
        except Exception as e:
            logger.error(f"Failed to send frame: {e}")
            return False
    
    async def _record_pending_frame(self, frame: bytes, tx_id: int):
        """Record pending frame for retry logic (assumes lock is already held)"""
        self._pending_frame = {
            'frame': frame,
            'tx_id': tx_id,
            'sent_at': time.monotonic(),
            'retries': 0,
        }
        self._pending_success = False
        self._pending_cleared_event.clear()
    
    async def _clear_pending_frame(self, success: bool = False, lock_held: bool = False):
        """Clear pending frame
        
        Args:
            success: Whether the frame was successfully acknowledged
            lock_held: If True, assumes lock is already held (for use within locked context)
        """
        if lock_held:
            # Lock is already held, just update state
            self._pending_frame = None
            self._pending_success = success
            self._pending_cleared_event.set()
        else:
            # Acquire lock first
            async with self._pending_lock:
                self._pending_frame = None
                self._pending_success = success
                self._pending_cleared_event.set()
    
    async def _handle_ack_frame(self, transaction_id: int, value: int):
        """Handle received ACK frame"""
        logger.debug(f"Processing ACK: tx_id={transaction_id}, value={value}")
        frame_to_retry = None
        try:
            logger.debug(f"Attempting to acquire pending lock for ACK tx_id={transaction_id}")
            async with self._pending_lock:
                logger.debug(f"Lock acquired, checking pending frame")
                pending = self._pending_frame
                
                if not pending:
                    logger.debug(f"ACK without pending frame (tx={transaction_id} value={value})")
                    return
                
                logger.debug(f"Pending frame found: tx_id={pending['tx_id']}, checking match")
                # Check transaction ID match
                if pending['tx_id'] != transaction_id:
                    logger.warning(f"ACK transaction mismatch (expected {pending['tx_id']}, got {transaction_id})")
                    return
                
                logger.debug(f"Transaction ID matches, value={value}")
                # Handle ACK_OK
                if value == ACK_OK:
                    logger.debug(f"ACK_OK received for transaction {transaction_id}, clearing pending frame")
                    await self._clear_pending_frame(success=True, lock_held=True)
                    logger.debug(f"Pending frame cleared, event set")
                    return
                
                # Handle transient errors (retry)
                transient = value in (ACK_ERROR_PAYLOAD_CHECKSUM, ACK_ERROR_BUSY)
                if transient and pending['retries'] < ACK_MAX_RETRIES:
                    pending['retries'] += 1
                    logger.warning(f"Transient ACK error {value} for transaction {transaction_id} (retry {pending['retries']}/{ACK_MAX_RETRIES})")
                    pending['sent_at'] = time.monotonic()
                    frame_to_retry = pending['frame']
                else:
                    # Permanent error or max retries exceeded
                    logger.error(f"Permanent ACK error {value} for transaction {transaction_id}")
                    await self._clear_pending_frame(success=False, lock_held=True)
        except Exception as e:
            logger.error(f"Error in _handle_ack_frame: {e}", exc_info=True)
        
        if frame_to_retry is not None:
            await asyncio.sleep(ACK_RETRY_DELAY_MS / 1000.0)
            await self._send_raw_frame(frame_to_retry)
    
    async def _service_loop(self):
        """Service pending frames and timeouts"""
        while self.running:
            frame_to_resend = None
            tx_id_to_resend = None
            needs_retry = False
            
            async with self._pending_lock:
                pending = self._pending_frame
                if pending:
                    elapsed_ms = (time.monotonic() - pending['sent_at']) * 1000
                    if elapsed_ms >= ACK_TIMEOUT_MS:
                        logger.debug(f"Timeout detected: elapsed={elapsed_ms}ms, timeout={ACK_TIMEOUT_MS}ms, retries={pending['retries']}, max={ACK_MAX_RETRIES}")
                        if pending['retries'] >= ACK_MAX_RETRIES:
                            logger.error(f"ACK timeout for transaction {pending['tx_id']} after {pending['retries'] + 1} attempts")
                            await self._clear_pending_frame(success=False, lock_held=True)
                        else:
                            # Don't increment retries yet - wait for delay first
                            # Store frame and tx_id before releasing lock
                            frame_to_resend = pending['frame']
                            tx_id_to_resend = pending['tx_id']
                            needs_retry = True
                            logger.debug(f"Setting needs_retry=True for tx_id={tx_id_to_resend}")
            
            # Release lock before sleeping and sending
            if needs_retry and frame_to_resend is not None:
                # Wait for retry delay before sending retry
                logger.info(f"ACK timeout detected for transaction {tx_id_to_resend}, waiting {ACK_RETRY_DELAY_MS}ms before retry...")
                await asyncio.sleep(ACK_RETRY_DELAY_MS / 1000.0)
                logger.debug(f"Retry delay completed for transaction {tx_id_to_resend}")
                async with self._pending_lock:
                    # Re-check pending frame still exists and matches (ACK might have arrived)
                    if self._pending_frame and self._pending_frame['tx_id'] == tx_id_to_resend:
                        # ACK hasn't arrived, increment retries and send
                        self._pending_frame['retries'] += 1
                        logger.warning(f"ACK timeout for transaction {tx_id_to_resend}, retrying ({self._pending_frame['retries']}/{ACK_MAX_RETRIES})")
                        self._pending_frame['sent_at'] = time.monotonic()
                        await self._send_raw_frame(frame_to_resend)
                    else:
                        logger.debug(f"ACK arrived during retry delay for transaction {tx_id_to_resend}, skipping retry")
                    # If frame doesn't exist or tx_id doesn't match, ACK arrived - do nothing
            elif needs_retry:
                logger.warning(f"needs_retry=True but frame_to_resend is None!")
            
            await asyncio.sleep(0.05)
    
    async def send_ack(self, transaction_id: int, value: int):
        """Send ACK frame (no waiting, no retries)"""
        frame = self._build_frame(PACKET_TYPE_ACK, value, transaction_id, b"")
        
        # Verbose logging: show ACK frame
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(f"ACK frame (hex): {frame.hex()}")
            logger.debug(f"ACK frame (tx={transaction_id}, value={value})")
        
        await self._send_raw_frame(frame)
    
    async def send_proto_message(self, payload: bytes, wait_for_ack: bool = True, timeout: Optional[int] = None) -> bool:
        """
        Send protobuf message with link-layer framing.
        
        Args:
            payload: Serialized protobuf message
            wait_for_ack: If True, wait for ACK before returning
            timeout: Timeout in milliseconds (default: ACK_TIMEOUT_MS * (retries + 1) + margin)
        
        Returns:
            True if message was sent and acknowledged (if wait_for_ack=True), False otherwise
        """
        if len(payload) > FRAME_MAX_PAYLOAD:
            logger.error(f"Proto payload too large ({len(payload)} bytes)")
            return False
        
        tx_id = self._next_tx_id()
        frame = self._build_frame(PACKET_TYPE_PROTOBUF, 0, tx_id, payload)
        
        # Verbose logging: show frame details
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(f"Frame header (hex): {frame[:FRAME_HEADER_LEN].hex()}")
            logger.debug(f"Frame payload (hex): {frame[FRAME_HEADER_LEN:].hex()}")
            logger.debug(f"Frame total (hex): {frame.hex()}")
            logger.debug(f"Frame total (len): {len(frame)} bytes")
        
        # Check and record pending frame atomically
        async with self._pending_lock:
            if self._pending_frame:
                logger.warning(f"Link busy - pending transaction {self._pending_frame['tx_id']} not yet ACKed")
                return False
            
            await self._record_pending_frame(frame, tx_id)
            logger.debug(f"Sent protobuf frame (tx={tx_id}, payload={len(payload)} bytes)")
        
        # Send frame after releasing lock to avoid blocking ACK handler
        if not await self._send_raw_frame(frame):
            async with self._pending_lock:
                await self._clear_pending_frame(success=False, lock_held=True)
            return False
        
        # Wait for ACK if requested
        if wait_for_ack:
            if timeout is None:
                # Calculate timeout: (initial + retries) * ACK_TIMEOUT + retry delays + margin
                # Each retry waits ACK_RETRY_DELAY_MS before sending, so account for that
                timeout_ms = ACK_TIMEOUT_MS * (ACK_MAX_RETRIES + 1) + (ACK_RETRY_DELAY_MS * ACK_MAX_RETRIES) + 500
            else:
                timeout_ms = timeout
            
            ack_received = await self.wait_for_pending_clear(timeout_ms=timeout_ms)
            if not ack_received:
                logger.error(f"Timeout waiting for ACK for transaction {tx_id}")
                async with self._pending_lock:
                    if self._pending_frame and self._pending_frame['tx_id'] == tx_id:
                        await self._clear_pending_frame(success=False, lock_held=True)
                return False
            
            # Check if ACK was successful
            async with self._pending_lock:
                success = self._pending_success
            
            if not success:
                logger.error(f"Transaction {tx_id} failed (NACK or timeout)")
                return False
            
            logger.debug(f"Transaction {tx_id} successfully acknowledged")
            return True
        
        return True
    
    async def wait_for_pending_clear(self, timeout_ms: int = 1000) -> bool:
        """Wait for pending frame to be cleared (ACK received or max retries exceeded)"""
        try:
            await asyncio.wait_for(self._pending_cleared_event.wait(), timeout_ms / 1000.0)
            return True
        except asyncio.TimeoutError:
            return False
    
    def handle_rx(self, buf: bytes):
        """Handle received frame from HDLC layer"""
        try:
            logger.debug(f"Transport received frame (len={len(buf)}): {buf.hex()}")
            
            if len(buf) < FRAME_HEADER_LEN:
                logger.error(f"Frame too short: {len(buf)} bytes")
                return
            
            if buf[0] != FRAME_START_BYTE:
                logger.error(f"Invalid start byte: 0x{buf[0]:02x}, expected 0x{FRAME_START_BYTE:02x}")
                return
            
            header = buf[:FRAME_HEADER_LEN]
            header_crc = crc8(header[:7])
            if header_crc != header[7]:
                logger.error(f"Header CRC mismatch (got 0x{header[7]:02X}, expected 0x{header_crc:02X})")
                return
            
            packet_type = header[1] & 0x07
            value = header[1] >> 3
            tx_id = header[2]
            payload_len = header[3] | (header[4] << 8)
            
            logger.debug(f"Received frame: type={packet_type}, tx_id={tx_id}, value={value}, payload_len={payload_len}")
            
            # Handle ACK packets (ACK frames have no payload, only header)
            if packet_type == PACKET_TYPE_ACK:
                if payload_len != 0:
                    logger.error(f"ACK packet with unexpected payload (tx={tx_id})")
                    return
                # For ACK frames, the entire frame is just the header (8 bytes)
                if len(buf) != FRAME_HEADER_LEN:
                    logger.error(f"ACK frame length mismatch: expected {FRAME_HEADER_LEN}, got {len(buf)}")
                    return
                logger.debug(f"Received ACK frame: tx_id={tx_id}, value={value}")
                logger.debug(f"Scheduling ACK handling task for tx_id={tx_id}, value={value}")
                asyncio.create_task(self._handle_ack_frame(tx_id, value))
                return
            
            # Handle protobuf packets (must have payload)
            if packet_type != PACKET_TYPE_PROTOBUF:
                logger.error(f"Unexpected packet type {packet_type}")
                asyncio.create_task(self.send_ack(tx_id, ACK_ERROR_PACKET_TYPE))
                return
            
            if payload_len == 0:
                logger.error(f"Proto packet without payload (tx={tx_id})")
                asyncio.create_task(self.send_ack(tx_id, ACK_ERROR_PACKET_TYPE))
                return
            
            if payload_len > FRAME_MAX_PAYLOAD:
                logger.error(f"Invalid payload length: {payload_len}")
                asyncio.create_task(self.send_ack(tx_id, ACK_ERROR_PAYLOAD_LENGTH))
                return
            
            total_len = FRAME_HEADER_LEN + payload_len
            if len(buf) < total_len:
                logger.error("Incomplete frame")
                return
            
            payload = buf[FRAME_HEADER_LEN:total_len]
            
            # Validate payload CRC
            payload_crc = crc8(payload)
            if payload_crc != header[5]:
                logger.error(f"Payload CRC mismatch (tx={tx_id})")
                asyncio.create_task(self.send_ack(tx_id, ACK_ERROR_PAYLOAD_CHECKSUM))
                return
            
            # Check if this might be an echo of our own transmission
            # Note: We check without lock since this is just a warning - the actual ACK handling
            # will properly check the transaction ID match
            if self._pending_frame and self._pending_frame['tx_id'] == tx_id:
                logger.warning(f"Received protobuf frame with same tx_id as pending frame ({tx_id}) - possible echo/loopback, ignoring")
                return
            
            # Send ACK before processing (as per protocol)
            asyncio.create_task(self.send_ack(tx_id, ACK_OK))
            
            # Log received protobuf payload
            logger.debug(f"Received protobuf payload (tx={tx_id}, len={len(payload)}): {payload.hex()}")
            
            # Process the message
            if self.handler:
                self.handler.handle_rx(payload)
            else:
                logger.warning("No handler set for transport layer")
        except Exception as e:
            logger.error(f"Error in transport handle_rx: {e}", exc_info=True)