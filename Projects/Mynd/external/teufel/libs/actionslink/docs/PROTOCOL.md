# ActionsLink Protocol Specification

## Overview

ActionsLink is a communication protocol used for managing communication between an Actions module and a host processor (typically an MCU or host computer). The protocol provides reliable, frame-based communication over UART with HDLC-style framing, error detection, and acknowledgment mechanisms.

## Protocol Stack

The ActionsLink protocol consists of multiple layers:

```
┌─────────────────────────────────────┐
│   Application Layer (Protobuf)      │  ← ToMcu/FromMcu messages
├─────────────────────────────────────┤
│   Transport Layer                   │  ← ACK/NACK, retries, transaction management
├─────────────────────────────────────┤
│   Link Layer                        │  ← Frame building, CRC, transaction IDs
├─────────────────────────────────────┤
│   HDLC Framing Layer                │  ← HDLC-style framing with escape sequences
├─────────────────────────────────────┤
│   Physical Layer (UART)             │  ← Serial communication @ 115200 baud
└─────────────────────────────────────┘
```

## Physical Layer

- **Interface**: UART (Universal Asynchronous Receiver-Transmitter)
- **Baud Rate**: 115200 (hardcoded)
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Flow Control**: None (RTS/CTS disabled)

## HDLC Framing Layer

The HDLC (High-Level Data Link Control) framing layer provides frame delimiters and escape sequences to ensure reliable frame boundaries over the serial link.

### Frame Delimiters

- **Frame Delimiter**: `0x7E` - Marks the start and end of a frame
- **Escape Character**: `0x7D` - Used to escape special characters
- **Escape Mask**: `0x20` - XOR mask applied to escaped bytes

### Framing Rules

1. All frames start and end with the frame delimiter (`0x7E`)
2. If a byte in the payload equals the frame delimiter (`0x7E`) or escape character (`0x7D`), it must be escaped:
   - Send escape character (`0x7D`)
   - Send the byte XORed with escape mask (`byte ^ 0x20`)

### Example

**Original frame data**: `0x55 0x01 0x7E 0x02`

**HDLC-framed**:
```
0x7E                    // Start delimiter
0x55                    // Data byte
0x01                    // Data byte
0x7D 0x5E               // Escaped: 0x7E → 0x7D 0x5E (0x7E ^ 0x20)
0x02                    // Data byte
0x7E                    // End delimiter
```

## Link Layer (Frame Format)

The link layer adds a header with metadata and CRC checksums to each frame.

### Frame Structure

```
┌─────────────────────────────────────────────────────────┐
│                    Frame Header (8 bytes)               │
├──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┤
│ 0x55 │Type  │Tx ID │Len L │Len H │CRC8  │Resv  │HCRC8 │
│      │Value │      │      │      │Payload│      │Header│
└──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
│                    Payload (0-256 bytes)                │
└─────────────────────────────────────────────────────────┘
```

### Header Fields

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 byte | Start Byte | Always `0x55` |
| 1 | 1 byte | Type/Value | Bits 0-2: Packet type<br>Bits 3-7: Value (ACK status or reserved) |
| 2 | 1 byte | Transaction ID | Unique transaction identifier (0-255) |
| 3 | 1 byte | Payload Length LSB | Lower byte of payload length |
| 4 | 1 byte | Payload Length MSB | Upper byte of payload length |
| 5 | 1 byte | Payload CRC8 | CRC-8 checksum of payload (0 if no payload) |
| 6 | 1 byte | Reserved | Reserved, must be 0 |
| 7 | 1 byte | Header CRC8 | CRC-8 checksum of header bytes 0-6 |

### Packet Types

| Type | Value | Description |
|------|-------|-------------|
| 0 | `PACKET_TYPE_ACK` | Acknowledgment packet (no payload) |
| 1 | `PACKET_TYPE_PROTOBUF` | Protobuf message packet |

### Transaction IDs

- Transaction IDs are 8-bit values (0-255)
- Each new transmission increments the transaction ID
- Transaction IDs wrap around (255 → 0 → 1)
- All transaction IDs (0-255) are available for normal use

### Payload

- Maximum payload size: **256 bytes**
- For ACK packets: payload length is always 0
- For Protobuf packets: payload contains serialized protobuf message

### CRC-8 Checksum

The protocol uses a CRC-8 algorithm with a lookup table. The CRC is calculated over:
- **Header CRC**: Bytes 0-6 of the header
- **Payload CRC**: All bytes of the payload (if payload length > 0)

The CRC-8 polynomial and lookup table are defined in the transport layer implementation.

## Transport Layer

The transport layer provides reliable delivery with acknowledgments, retries, and error handling.

### Acknowledgment Mechanism

Every protobuf packet must be acknowledged by the receiver:

1. **Sender** transmits a protobuf packet with a transaction ID
2. **Receiver** validates the frame (CRC, length, format)
3. **Receiver** sends an ACK packet with:
   - Same transaction ID
   - ACK status value (see ACK Values below)
4. **Sender** waits for ACK before considering transmission complete

### ACK Values

| Value | Constant | Description |
|-------|----------|-------------|
| 0 | `ACK_OK` | Packet received and processed successfully |
| 1 | `ACK_ERROR_PACKET_TYPE` | Invalid packet type |
| 2 | `ACK_ERROR_PAYLOAD_CHECKSUM` | Payload CRC mismatch (transient, retry) |
| 3 | `ACK_ERROR_PAYLOAD_LENGTH` | Invalid payload length |
| 4 | `ACK_ERROR_BUSY` | Receiver busy, try again later (transient, retry) |

### Retry Logic

- **ACK Timeout**: 200ms (`ACK_TIMEOUT_SEC = 0.2`)
- **Max Retries**: 1 retry = 2 total attempts (initial + 1 retry)
- **Retry Delay**: 200ms between retry attempts (`ACK_RETRY_DELAY_SEC = 0.2`)

**Retry Behavior**:
- If ACK is not received within 200ms, the frame is retransmitted
- Transient errors (`ACK_ERROR_PAYLOAD_CHECKSUM`, `ACK_ERROR_BUSY`) trigger automatic retry
- After 2 total attempts (1 initial + 1 retry), the transmission is considered failed

### Transaction Management

- Only one transaction can be pending at a time
- The sender tracks the pending frame and waits for its ACK
- If a new frame is sent while a previous one is pending, it will be rejected with a "link busy" error

### System Ready Notification

There is **no handshake mechanism** at the transport/link layer. Instead, readiness is signaled using a protobuf message:

- **Message**: `ToMcuEvent.notify_system_ready` (from host to MCU) or `FromMcuEvent` notifications (from MCU to host)
- **Purpose**: Notifies the other side that the application has initialized successfully and is ready to receive requests
- **Usage**: This is a normal protobuf event message that follows the standard protocol (requires ACK like any other message)
- **When to send**: Typically sent after system initialization is complete

This is a regular protobuf message, not a special transport-layer handshake.

## Application Layer (Protobuf Messages)

The application layer uses Protocol Buffers (protobuf) for message serialization. Messages are defined in `.proto` files and vary by device type (RPI, ECO, Cinebar-Ultima).

### Message Structure

All messages follow a common structure:

#### From MCU (MCU → Host)

```protobuf
message FromMcu {
  oneof Payload {
    FromMcuRequest  request = 1;
    FromMcuResponse response = 2;
    FromMcuEvent    event = 3;
  };
}
```

#### To MCU (Host → MCU)

```protobuf
message ToMcu {
  oneof Payload {
    ToMcuRequest  request = 1;
    ToMcuResponse response = 2;
    ToMcuEvent    event = 3;
  };
}
```

### Message Types

1. **Request**: Command from sender to receiver (expects a response)
2. **Response**: Reply to a previous request (includes sequence number)
3. **Event**: Notification message (no response expected, but still requires ACK)

### Sequence Numbers

- Requests include a sequence number (`seq` field)
- Responses include the sequence number of the request they're responding to
- Sequence numbers are 32-bit values
- Events do not use sequence numbers

### Example Messages (RPI Variant)

#### ToMcuEvent (Host → MCU Events)

```protobuf
message ToMcuEvent {
  oneof Event {
    Common.Command        notify_system_ready = 10;
    System.PowerState     notify_power_state = 11;
    Audio.Volume          notify_volume = 21;
    bool                  notify_stream_state = 22;
  }
}
```

#### FromMcuRequest (MCU → Host Requests)

```protobuf
message FromMcuRequest {
  uint32 seq = 1;
  oneof Request {
    Common.Command        soft_reset = 10;
    Common.Command        get_firmware_version = 11;
    System.PowerState     set_power_state = 12;
    SetLed                set_led = 21;
    Audio.VolumeControl   set_volume = 22;
  }
}
```

#### FromMcuEvent (MCU → Host Events)

```protobuf
message FromMcuEvent {
  oneof Event {
    uint32                notify_battery_level = 12;
    Battery.ChargerStatus notify_charger_status = 13;
    bool                  notify_battery_friendly_charging = 15;
    ButtonEvent           notify_button_event = 40;
    bool                  notify_aux_connected = 41;
    bool                  notify_usb_connected = 42;
    bool                  notify_eco_mode = 30;
  }
}
```

## Protocol Flow Examples

### Example 1: Sending an Event (Host → MCU)

```
1. Host creates ToMcuEvent message
2. Host serializes to protobuf bytes
3. Host builds frame:
   - Header with PACKET_TYPE_PROTOBUF
   - Transaction ID (e.g., 1)
   - Payload: serialized protobuf
   - CRC calculations
4. Host applies HDLC framing
5. Host sends over UART
6. MCU receives and validates frame
7. MCU sends ACK (tx_id=1, value=ACK_OK)
8. Host receives ACK and completes transaction
```

### Example 2: Sending a Request (Host → MCU)

```
1. Host creates ToMcuRequest with seq=1
2. Host serializes and sends (same as event)
3. MCU processes request
4. MCU sends ACK for the request frame
5. MCU creates FromMcuResponse with seq=1
6. MCU sends response as new frame (new tx_id)
7. Host sends ACK for response frame
```

### Example 3: Retry on Timeout

```
1. Host sends frame (tx_id=1)
2. 200ms passes, no ACK received
3. Host retransmits frame (tx_id=1, retry=1)
4. MCU receives and sends ACK
5. Host receives ACK and completes
```

### Example 4: Retry on Transient Error

```
1. Host sends frame (tx_id=1)
2. MCU receives but detects CRC error
3. MCU sends NACK (tx_id=1, value=ACK_ERROR_PAYLOAD_CHECKSUM)
4. Host receives NACK, waits 200ms
5. Host retransmits frame (tx_id=1, retry=1)
6. MCU receives correctly and sends ACK_OK
7. Host receives ACK and completes
```

## Error Handling

### Frame Validation

The receiver validates each frame:

1. **Length Check**: Frame must be at least 8 bytes (header length)
2. **Start Byte**: First byte must be `0x55`
3. **Header CRC**: Header CRC must match calculated value
4. **Payload Length**: Payload length must be ≤ 256 bytes
5. **Frame Completeness**: Frame must contain full header + payload
6. **Payload CRC**: If payload exists, payload CRC must match

### Error Responses

- **Invalid frame format**: Frame is silently discarded (no ACK sent)
- **CRC mismatch**: NACK with `ACK_ERROR_PAYLOAD_CHECKSUM`
- **Invalid length**: NACK with `ACK_ERROR_PAYLOAD_LENGTH`
- **Invalid packet type**: NACK with `ACK_ERROR_PACKET_TYPE`
- **Receiver busy**: NACK with `ACK_ERROR_BUSY`

## Implementation Notes

### Python Implementation

The Python implementation (`actionslink/host/`) provides:

- **HDLC Layer** (`actionslink_hdlc.py`): HDLC framing and UART communication
- **Transport Layer** (`actionslink_transport.py`): Frame building, ACK handling, retries
- **API Layer** (`actionslink_api.py`): High-level protobuf message handling

### C Implementation

The C implementation (`actionslink/src/`) provides similar functionality for embedded systems.

### Protobuf Generation

Protobuf files are generated using the `generate_proto.sh` script, which:
1. Generates nanopb protobuf files (for C)
2. Generates standard protobuf files (for Python)
3. Handles imports from common proto definitions

## Constants Summary

| Constant | Value | Description |
|----------|-------|-------------|
| `FRAME_START_BYTE` | `0x55` | Frame header start byte |
| `FRAME_HEADER_LEN` | `8` | Frame header length in bytes |
| `FRAME_MAX_PAYLOAD` | `256` | Maximum payload size in bytes |
| `ACK_TIMEOUT_SEC` | `0.2` | ACK timeout in seconds |
| `ACK_MAX_RETRIES` | `1` | Maximum retry attempts |
| `ACK_RETRY_DELAY_SEC` | `0.2` | Delay between retries in seconds |
| `HDLC_FRAME_DELIMITER` | `0x7E` | HDLC frame delimiter |
| `HDLC_ESCAPE_CHARACTER` | `0x7D` | HDLC escape character |
| `HDLC_ESCAPE_MASK` | `0x20` | HDLC escape XOR mask |
| `BAUDRATE` | `115200` | UART baud rate |

## References

- Protocol Buffers: https://protobuf.dev/
- HDLC: https://en.wikipedia.org/wiki/High-Level_Data_Link_Control

