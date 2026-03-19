# ActionsLink Protocol Guide (MYND RPi Mod)

This document covers protocol behavior that exists in the current branch.

## Where the Protocol Lives

- Protobuf schema: `Projects/Mynd/external/teufel/libs/actionslink/proto/rpi/message.proto`
- Protobuf generation script: `Projects/Mynd/external/teufel/libs/actionslink/scripts/generate_proto.sh`
- MCU-side protocol handling: `Projects/Mynd/src/tasks/rpi/task_rpi.cpp`
- RPi daemon protocol handling: `Projects/Mynd/src/tasks/rpi/daemon_install/payload/bin/mynd_rpi_link.py`
- Power sequence reference: `Projects/Mynd/docs/mcu_rpilink_pwr_seq.puml`

Transport is ActionsLink over the MYND's Bluetooth hardware UART (MCU_RX, MCU_TX, GND).
  - See connector component P2W at https://github.com/teufelaudio/mynd-hardware/blob/main/PCB_Schematics/PDF/Mynd_BT_SCH.PDF

## Runtime Hook Points

### Python daemon (`mynd_rpi_link.py`)

- Registers one MCU event callback using `client.on_event(...)`.
- Registers MCU request handlers using `client.on_request(...)` for non-WiFi RPC-style commands.
- Sends daemon-to-MCU events through generated protobuf classes (`message_pb.ToMcuEvent`).

### MCU firmware (`task_rpi.cpp`)

- Sends MCU-to-RPi requests via `actionslink_set_power_state` and playback/volume request helpers.
- Sends WiFi commands via MCU->RPi event helpers with `command_id` correlation.
- Handles daemon-to-MCU events in `actionslink_event_handlers_t`.
- Uses `actionslink_tick()` in active and wait loops to process transport.

## Message Matrix (Current Implementation)

### MCU -> RPi requests

- `set_power_state`
- `send_playback_action`
- `set_volume`

### MCU -> RPi events

- `notify_battery_level`
- `notify_charger_status`
- `notify_battery_friendly_charging`
- `notify_configure_wifi_command`
- `notify_enable_hotspot_command`
- `notify_cycle_wifi_network_command`

### RPi -> MCU events

- `notify_system_ready`
- `notify_power_state`
- `notify_stream_state`
- `notify_host_source`
- `notify_play_led_pattern`
- `notify_wifi_command_result`
- `notify_wifi_info`

## Power-State Behavior Notes

- Daemon startup sends `notify_system_ready` and expects MCU to proceed with power-state request flow.
- `set_power_state(ON)` request handling includes a short delay before response, then `notify_power_state(ON)` is sent after a processing delay.
- In MCU `PreOff`, shutdown is initiated early by sending `set_power_state(OFF)` from `task_rpi.cpp`.
- Drag-and-drop preparation uses `set_power_state(SHUTDOWN_REQUEST)` when RPi is on, then waits for `Off` or `ShutdownRequested`.
- Daemon startup fallback can request MCU firmware version and run synthetic ON initialization if no initial ON request arrives soon after connect.

For the step-by-step sequence with timing context, use `Projects/Mynd/docs/mcu_rpilink_pwr_seq.puml`.

## Example: MCU -> RPi request (`set_volume`)

`set_volume` is actively registered and handled.

### Daemon request registration

```python
client.on_request('set_volume',
                lambda seq, req: asyncio.create_task(handle_set_volume(client, daemon, seq, req)))
```

### Daemon response path

```python
await _send_status_response(client, seq, "set_volume", status_code)
```

## Example: MCU -> RPi event (`notify_configure_wifi_command`)

WiFi commands are event-driven because they can take significantly longer than the normal request/response timing budget.

### MCU send event (`task_rpi.cpp`)

```cpp
[](const Tur::ConfigureWifi &)
{
    if (isProperty(Tur::PowerState::On))
    {
        const auto command_id = begin_wifi_command(Tur::WifiCommandAction::ConfigureWifi);
        if (!command_id.has_value())
        {
            return;
        }
        if (actionslink_send_configure_wifi_command(command_id.value(), s_wifi.configure_wifi_ssid,
                                                    s_wifi.configure_wifi_password) != 0)
        {
            log_error("Failed to send cfg WiFi cmd");
            clear_pending_wifi_command();
        }
    }
    else
        log_error("RPi off, cannot cfg WiFi");
},
```

### Daemon receive via event handler

```python
def handle_mcu_event(event):
    event_type = event.WhichOneof("Event")
    if event_type == "notify_configure_wifi_command":
        handlers.schedule(
            handlers.handle_configure_wifi_event(event.notify_configure_wifi_command),
            "configure_wifi",
        )
```

### Daemon completion event

```python
await send_notify_wifi_command_result(
    client,
    daemon,
    command_id,
    host_pb.WiFiCommandResult.ActionType.ACTION_TYPE_CONFIGURE_WIFI,
    result.status_code,
    target_reached=result.target_reached,
    detail=result.detail,
)
```

## Example: RPi -> MCU event (`notify_play_led_pattern`)

### Daemon send

```python
event = message_pb.ToMcuEvent()
event.notify_play_led_pattern.pattern = pattern
await client.send_event(event)
```

### MCU receive (`task_rpi.cpp`)

```cpp
.on_notify_play_led_pattern =
    +[](actionslink_led_pattern_t pattern)
    {
        switch (pattern)
        {
            case ACTIONSLINK_LED_PATTERN_POSITIVE_FEEDBACK:
                Teufel::Task::Leds::set_source_pattern(Teufel::Task::Leds::SourcePattern::PositiveFeedback);
                break;
            case ACTIONSLINK_LED_PATTERN_NEGATIVE_FEEDBACK:
                Teufel::Task::Leds::set_source_pattern(Teufel::Task::Leds::SourcePattern::NegativeFeedback);
                break;
            default:
                log_warning("Unknown LED pattern: %d", pattern);
                break;
        }
    },
```

## Example: RPi -> MCU event (`notify_wifi_info`)

After successful WiFi actions (`configure_wifi`, `enable_hotspot`, `cycle_wifi_network`), daemon sends SSID/IP/username so MCU logs can show connection details and SSH hint. This is success-only follow-up telemetry; completion status is carried separately by `notify_wifi_command_result`.

### Daemon send

```python
event = message_pb.ToMcuEvent()
event.notify_wifi_info.ssid = ssid
event.notify_wifi_info.ip_address = ip_address
event.notify_wifi_info.username = username
await client.send_event(event)
```

### MCU receive (`task_rpi.cpp`)

```cpp
.on_notify_wifi_info =
    +[](const char *ssid, const char *ip_address, const char *username)
    {
        log_info("RPi WiFi connected: SSID=%s IP=%s USER=%s", ssid, ip_address, username);
        log_info("SSH (copy/paste): ssh %s@%s", username, ip_address);
    },
```

## Example: RPi -> MCU event (`notify_wifi_command_result`)

Every WiFi command reaches a terminal state through this event, correlated by `command_id`.

### Daemon send

```python
event = message_pb.ToMcuEvent()
event.notify_wifi_command_result.command_id = command_id
event.notify_wifi_command_result.action_type = action_type
event.notify_wifi_command_result.status.status.code = status_code
event.notify_wifi_command_result.target_reached = target_reached
event.notify_wifi_command_result.detail = detail
await client.send_event(event)
```

### MCU receive (`task_rpi.cpp`)

```cpp
.on_notify_wifi_command_result =
    +[](uint32_t command_id, actionslink_wifi_command_action_t action, actionslink_error_t status,
        bool target_reached, actionslink_wifi_command_detail_t detail)
    {
        log_info("WiFi command %lu (%s) completed with status=%d, detail=%d",
                 static_cast<unsigned long>(command_id), get_wifi_command_desc(action), status, detail);
    },
```

## Adding New Messages

1. Define a new message in `rpi/host.proto` and/or `rpi/led.proto`
2. Add the new message field to the correct message type in `rpi/message.proto`.
2. Regenerate protobuf artifacts:

```bash
bash Projects/Mynd/external/teufel/libs/actionslink/scripts/generate_proto.sh
```

3. Confirm generated symbol names in C and Python outputs.
4. Wire sender and handler in both MCU and daemon, following the existing request/response and event patterns.
5. For long-running event-driven jobs like WiFi, define an explicit completion event instead of overloading the request/response path.

## Extension Examples (Worked Templates)

These are implementation templates based on current project patterns. Field numbers and generated symbol names are examples; always use the exact names produced by protobuf generation.

### 1) Add a new MCU -> RPi request

Use this when MCU needs daemon-side work and a status response (success/fail).

#### Proto (`message.proto`)

```protobuf
message FromMcuRequest {
  // ... existing fields ...
  MyNewRequest my_new_request = 99; // choose next free field number
}

message MyNewRequest {
  uint32 value = 1;
}
```

#### Daemon (`mynd_rpi_link.py`)

```python
async def handle_my_new_request(client: ActionsLinkClient, daemon: MyndRpiDaemon, seq: int, req):
    try:
        value = req.my_new_request.value
        daemon.logger.info("my_new_request value=%d", value)

        # TODO: perform action
        status_code = error_pb.Code.Success
    except Exception:
        daemon.logger.exception("my_new_request failed")
        status_code = error_pb.Code.OperationFailed

    await _send_status_response(client, seq, "my_new_request", status_code)

# register handler
client.on_request(
    "my_new_request",
    lambda seq, req: asyncio.create_task(handle_my_new_request(client, daemon, seq, req)),
)
```

#### MCU sender (`task_rpi.cpp`)

```cpp
// Pattern follows existing actionslink_send_*_request usage
if (isProperty(Tur::PowerState::On))
{
    actionslink_send_my_new_request(/* args from generated API */);
}
```

### 2) Add a new MCU -> RPi event

Use this for one-way MCU notifications (no request/response status path).

#### Proto (`message.proto`)

```protobuf
message FromMcuEvent {
  // ... existing fields ...
  MyNewEvent notify_my_new_event = 99; // choose next free field number
}

message MyNewEvent {
  uint32 value = 1;
}
```

#### MCU sender (`task_rpi.cpp`)

```cpp
// Use generated actionslink_send_* event helper name
actionslink_send_my_new_event(/* args */);
```

#### Daemon receiver (`mynd_rpi_link.py`)

```python
def handle_mcu_event(daemon: MyndRpiDaemon, event: message_pb.FromMcuEvent):
    event_type = event.WhichOneof("Event")

    if event_type == "notify_my_new_event":
        value = event.notify_my_new_event.value
        daemon.logger.info("notify_my_new_event value=%d", value)
        # TODO: handle event
```

### 3) Add a new RPi -> MCU event

Use this when daemon needs to asynchronously notify MCU state.

#### Proto (`message.proto`)

```protobuf
message ToMcuEvent {
  // ... existing fields ...
  MyDaemonEvent notify_my_daemon_event = 99; // choose next free field number
}

message MyDaemonEvent {
  bool enabled = 1;
}
```

#### Daemon sender (`mynd_rpi_link.py`)

```python
async def send_notify_my_daemon_event(client: ActionsLinkClient, enabled: bool) -> bool:
    event = message_pb.ToMcuEvent()
    event.notify_my_daemon_event.enabled = enabled
    return await client.send_event(event)
```

#### MCU event handler (`task_rpi.cpp`)

```cpp
static const actionslink_event_handlers_t actionslink_event_handlers = {
    // ... existing handlers ...
    .on_notify_my_daemon_event =
        +[](bool enabled)
        {
            log_info("notify_my_daemon_event enabled=%d", enabled);
            // TODO: handle event
        },
};
```

### 4) Add a new RPi -> MCU request

Use this only when daemon needs a direct response payload from MCU.

#### Proto (`message.proto`)

```protobuf
message ToMcuRequest {
  // ... existing fields ...
  MyDaemonRequest my_daemon_request = 99; // choose next free field number
}

message MyDaemonRequest {
  uint32 mode = 1;
}
```

#### Daemon sender (`mynd_rpi_link.py`)

```python
request = message_pb.ToMcuRequest()
request.my_daemon_request.mode = 1
response = await client.send_request(request, expected_response="my_daemon_request")
```

#### MCU request handler (`task_rpi.cpp`)

```cpp
static const actionslink_request_handlers_t actionslink_request_handlers = {
    // ... existing handlers ...
    .on_request_my_daemon_request =
        +[](uint8_t seq_id, uint32_t mode)
        {
            log_info("my_daemon_request mode=%u", mode);
            // TODO: handle request
            actionslink_send_my_daemon_request_response(
                seq_id,
                ACTIONSLINK_ERROR_SUCCESS
                /* + optional response payload fields */
            );
        },
};
```
