#!/usr/bin/env python3
"""
ActionsLink HDLC Framing Layer

Implements HDLC-style framing with escape sequences for UART communication.
Based on the ActionsLink protocol specification.
"""

import asyncio
import logging
import sys

try:
    import aioserial
except ImportError:
    print("ERROR: aioserial is required. Install with: pip3 install aioserial", file=sys.stderr)
    sys.exit(1)

logger = logging.getLogger("ActionsLink.HDLC")

HDLC_FRAME_DELIMITER = 0x7E
HDLC_ESCAPE_CHARACTER = 0x7D
HDLC_ESCAPE_MASK = 0x20


class HdlcFraming:
    """HDLC framing layer for ActionsLink protocol"""
    
    BAUDRATE = 115200  # Hardcoded baudrate
    
    def __init__(self, port: str, handler=None):
        self.port = port
        self.handler = handler
        self.ser: aioserial.AioSerial = None
        self.rx_buf = bytearray()
        self.escape = False
        self.running = False
        self.rx_task = None
    
    async def open(self):
        """Open serial port"""
        try:
            self.ser = aioserial.AioSerial(
                port=self.port,
                baudrate=self.BAUDRATE,
                timeout=0,
                rtscts=False,
                write_timeout=1.0,  # Set write timeout to ensure writes complete
                inter_byte_timeout=None  # No timeout between bytes
            )
            # Flush input buffer to clear any leftover data
            self.ser.reset_input_buffer()
            logger.info(f"Opened UART: {self.port} @ {self.BAUDRATE} baud")
            return True
        except Exception as e:
            logger.error(f"Failed to open UART: {e}")
            return False
    
    async def close(self):
        """Close serial port"""
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
                logger.info("Closed UART connection")
            except Exception as e:
                logger.error(f"Error closing UART: {e}")
    
    def start(self):
        """Start RX task"""
        if not self.ser:
            raise RuntimeError("Serial port not open")
        self.running = True
        self.rx_task = asyncio.create_task(self._rx_loop())
    
    async def stop(self):
        """Stop RX task"""
        self.running = False
        if self.rx_task:
            self.rx_task.cancel()
            try:
                await self.rx_task
            except asyncio.CancelledError:
                pass
    
    async def _rx_loop(self):
        """Receive and process incoming bytes"""
        while self.running:
            try:
                buf = await self.ser.read_async(1000)
                if buf:
                    self._process_incoming(buf)
                else:
                    await asyncio.sleep(0.05) # increasing from 0.01 to 0.05 reduces RPi CPU usage from ~9% to ~3%
            except Exception as e:
                logger.error(f"Error in RX loop: {e}")
                await asyncio.sleep(0.1)
    
    def _process_incoming(self, buf: bytes):
        """Process incoming bytes and extract frames"""
        for c in buf:
            if self.escape:
                c ^= HDLC_ESCAPE_MASK
                self.escape = False
            elif c == HDLC_ESCAPE_CHARACTER:
                self.escape = True
                continue
            elif c == HDLC_FRAME_DELIMITER:
                if self.rx_buf:
                    logger.debug(f"Frame received: {self.rx_buf.hex()}")
                    if self.handler:
                        self.handler.handle_rx(bytes(self.rx_buf))
                self.rx_buf = bytearray()
                continue
            
            if self.rx_buf is not None:
                self.rx_buf.append(c)
    
    async def send(self, buf: bytes):
        """Send data with HDLC framing"""
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial port not open")
        
        tx_buf = bytearray()
        tx_buf.append(HDLC_FRAME_DELIMITER)
        
        for b in buf:
            if b == HDLC_FRAME_DELIMITER or b == HDLC_ESCAPE_CHARACTER:
                tx_buf.append(HDLC_ESCAPE_CHARACTER)
                tx_buf.append(b ^ HDLC_ESCAPE_MASK)
            else:
                tx_buf.append(b)
        
        tx_buf.append(HDLC_FRAME_DELIMITER)
        
        # Convert to bytes for writing
        tx_bytes = bytes(tx_buf)
        
        # Verbose logging: show HDLC-framed data
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(f"HDLC raw tx (hex): {tx_bytes.hex()}")
            logger.debug(f"HDLC raw tx (len): {len(tx_bytes)} bytes")
            logger.debug(f"HDLC raw tx (raw bytes): {tx_bytes}")
        
        # Write frame byte-by-byte to prevent OS from fragmenting it
        # Some OS/drivers fragment large writes, so we write one byte at a time
        # with a small delay to ensure each byte is transmitted before the next
        def _write():
            import time
            # Write each byte individually with a small delay
            # At 115200 baud, 1 byte takes ~87 microseconds to transmit
            # We use 0.1ms (100 microseconds) delay between bytes to ensure
            # each byte is fully transmitted before sending the next
            byte_delay = 0.0001  # 100 microseconds per byte
            
            for i, byte_val in enumerate(tx_bytes):
                written = self.ser.write(bytes([byte_val]))
                if written != 1:
                    raise RuntimeError(f"Serial write failed at byte {i}: wrote {written} bytes instead of 1")
                
                # Small delay between bytes (except for the last byte)
                if i < len(tx_bytes) - 1:
                    time.sleep(byte_delay)
            
            # Force flush to hardware
            self.ser.flushOutput()
            if hasattr(self.ser, 'flush'):
                self.ser.flush()
        
        # Execute write in thread
        await asyncio.to_thread(_write)
        
        # Small delay after flush to ensure last byte is fully transmitted
        # At 115200 baud: 1 byte = ~87 microseconds, use 2ms to be safe
        await asyncio.sleep(0.002)