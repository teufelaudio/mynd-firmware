// Due to the flash size limit, use the WARNING or ERROR level to reduce flash usage.
// This will hide debug logs, but info, warning and/or error logs will still be printed.
#define LOG_LEVEL LOG_LEVEL_INFO

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <optional>
#include <variant>
#include <functional>

#include "config.h"
#include "board.h"
#include "board_link.h"
#include "bsp_bluetooth_uart.h"
#include "actionslink_types.h"
#include "actionslink.h"
#include "task_priorities.h"
#include "include/logger_defs.h"
#include "logger.h"

#include "ux/system/system.h"
#include "ux/audio/audio.h"
#include "ux/rpi/rpi.h"

#include "task_rpi.h"
#include "task_audio.h"
#include "task_system.h"

#include "leds.h"
#include "board_link_bluetooth.h"
#include "board_link_plug_detection.h"
#include "board_link_usb_pd_controller.h"
#include "board_link_usb_switch.h"
#include "FreeRTOS.h"
#include "projdefs.h"
#include "task.h"

#include "external/teufel/libs/app_assert/app_assert.h"
#include "external/teufel/libs/core_utils/mapper.h"
#include "external/teufel/libs/GenericThread/GenericThread++.h"
#include "external/teufel/libs/core_utils/sync.h"
#include "external/teufel/libs/core_utils/overload.h"
#include "external/teufel/libs/property/property.h"
#include "external/teufel/libs/tshell/tshell.h"

#include "gitversion/version.h"

#define TASK_RPI_STACK_SIZE 448
#define QUEUE_SIZE          8

#define ACTIONSLINK_RX_BUFFER_SIZE 128u
uint8_t actionslink_rx_buffer[ACTIONSLINK_RX_BUFFER_SIZE] = {0};
#define ACTIONSLINK_TX_BUFFER_SIZE 64u
uint8_t actionslink_tx_buffer[ACTIONSLINK_TX_BUFFER_SIZE] = {0};

#define CONFIGURE_WIFI_SSID_MAX     33
#define CONFIGURE_WIFI_PASSWORD_MAX 64

namespace Teufel::Task::RpiLink
{

namespace Tus = Teufel::Ux::System;
namespace Tua = Teufel::Ux::Audio;
namespace Tur = Teufel::Ux::RpiLink;

static Tus::Task                                             ot_id        = Tus::Task::RpiLink;
static Teufel::GenericThread::GenericThread<RpiLinkMessage> *task_handler = nullptr;
static struct
{
    char     configure_wifi_ssid[CONFIGURE_WIFI_SSID_MAX];
    char     configure_wifi_password[CONFIGURE_WIFI_PASSWORD_MAX];
    uint32_t next_command_id = 1;
    struct
    {
        bool                   active     = false;
        uint32_t               command_id = 0;
        Tur::WifiCommandAction action     = Tur::WifiCommandAction::Unknown;
    } pending_command;
} s_wifi;

static PropertyNonOpt<decltype(Tur::StreamingActive::value)> m_streaming_active{"streaming active", false, false};
PROPERTY_SET(Tur::StreamingActive, m_streaming_active)

static PropertyNonOpt<Tur::HostSource> m_host_source{"host source", Tur::HostSource::Unknown, Tur::HostSource::Unknown};
PROPERTY_ENUM_SET(Tur::HostSource, m_host_source)

static PropertyNonOpt<Tur::PowerState> m_rpi_power_state{"rpi power state", Tur::PowerState::Off, Tur::PowerState::Off};
PROPERTY_ENUM_SET(Tur::PowerState, m_rpi_power_state)

TS_KEY_VALUE_CONST_MAP(HostSourceMapper, actionslink_host_source_t, Tur::HostSource,
                       {ACTIONSLINK_HOST_SOURCE_UNKNOWN, Tur::HostSource::Unknown},
                       {ACTIONSLINK_HOST_SOURCE_MPD, Tur::HostSource::Mpd},
                       {ACTIONSLINK_HOST_SOURCE_AIRPLAY, Tur::HostSource::Airplay},
                       {ACTIONSLINK_HOST_SOURCE_BLUETOOTH, Tur::HostSource::Bluetooth},
                       {ACTIONSLINK_HOST_SOURCE_SPOTIFY, Tur::HostSource::Spotify}, )
TS_KEY_VALUE_CONST_MAP(PowerStateMapper, actionslink_power_state_t, Tur::PowerState,
                       {ACTIONSLINK_POWER_STATE_OFF, Tur::PowerState::Off},
                       {ACTIONSLINK_POWER_STATE_ON, Tur::PowerState::On},
                       {ACTIONSLINK_POWER_STATE_STANDBY, Tur::PowerState::Standby},
                       {ACTIONSLINK_POWER_STATE_SHUTDOWN_REQUEST, Tur::PowerState::ShutdownRequested}, )
TS_KEY_VALUE_CONST_MAP(ChargerStatusMapper, Tus::ChargerStatus, actionslink_charger_status_t,
                       {Tus::ChargerStatus::NotConnected, ACTIONSLINK_CHARGER_STATUS_NOT_CONNECTED},
                       {Tus::ChargerStatus::Active, ACTIONSLINK_CHARGER_STATUS_ACTIVE},
                       {Tus::ChargerStatus::Inactive, ACTIONSLINK_CHARGER_STATUS_INACTIVE},
                       {Tus::ChargerStatus::Fault, ACTIONSLINK_CHARGER_STATUS_FAULT}, )
TS_KEY_VALUE_CONST_MAP(WifiCommandActionMapper, actionslink_wifi_command_action_t, Tur::WifiCommandAction,
                       {ACTIONSLINK_WIFI_COMMAND_ACTION_UNKNOWN, Tur::WifiCommandAction::Unknown},
                       {ACTIONSLINK_WIFI_COMMAND_ACTION_CONFIGURE_WIFI, Tur::WifiCommandAction::ConfigureWifi},
                       {ACTIONSLINK_WIFI_COMMAND_ACTION_ENABLE_HOTSPOT, Tur::WifiCommandAction::EnableHotspot},
                       {ACTIONSLINK_WIFI_COMMAND_ACTION_CYCLE_WIFI_NETWORK, Tur::WifiCommandAction::CycleWifiNetwork}, )
TS_KEY_VALUE_CONST_MAP(WifiCommandDetailMapper, actionslink_wifi_command_detail_t, Tur::WifiCommandDetail,
                       {ACTIONSLINK_WIFI_COMMAND_DETAIL_NONE, Tur::WifiCommandDetail::None},
                       {ACTIONSLINK_WIFI_COMMAND_DETAIL_BUSY, Tur::WifiCommandDetail::Busy}, )
static StaticTask_t rpi_task_buffer;
static StackType_t  rpi_task_stack[TASK_RPI_STACK_SIZE];
/* The variable used to hold the queue's data structure. */
static StaticQueue_t queue_static;
static const size_t  queue_item_size = sizeof(Teufel::GenericThread::QueueMessage<RpiLinkMessage>);
static uint8_t       queue_static_buffer[QUEUE_SIZE * queue_item_size];

static void actionslink_print_log(actionslink_log_level_t level, const char *dsc);
static int  actionslink_read_buffer(uint8_t *p_data, uint8_t length, uint32_t timeout);
static int  actionslink_write_buffer(const uint8_t *p_data, uint8_t length, uint32_t timeout);

inline static void clear_pending_wifi_command()
{
    s_wifi.pending_command = {};
}

inline static std::optional<uint32_t> begin_wifi_command(Tur::WifiCommandAction action)
{
    if (s_wifi.pending_command.active)
    {
        log_warning("Ignore %s, WiFi cmd %lu (%s) active", getDesc(action),
                    static_cast<unsigned long>(s_wifi.pending_command.command_id),
                    getDesc(s_wifi.pending_command.action));
        return std::nullopt;
    }

    if (s_wifi.next_command_id == 0)
    {
        s_wifi.next_command_id = 1;
    }

    const uint32_t command_id = s_wifi.next_command_id++;
    s_wifi.pending_command    = {.active = true, .command_id = command_id, .action = action};
    return command_id;
}

static void wait_for_rpi_off()
{
    while (not isPropertyOneOf(Tur::PowerState::Off, Tur::PowerState::ShutdownRequested))
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        actionslink_tick();
    }
}

static const actionslink_request_handlers_t actionslink_request_handlers = {
    .on_request_get_mcu_firmware_version =
        +[](uint8_t seq_id)
        {
            log_debug("Request MCU firmware version(seq_id: %d)", seq_id);
            vTaskDelay(pdMS_TO_TICKS(10));
            actionslink_send_get_mcu_firmware_version_response(seq_id, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
                                                               nullptr);
        },
    .on_request_set_bass =
        +[](uint8_t seq_id, int32_t bass)
        {
            log_debug("Request set bass(seq_id: %d, bass: %d)", seq_id, bass);
            Teufel::Task::Audio::postMessage(ot_id, Tua::BassLevel{.value = static_cast<int8_t>(bass)});
            actionslink_send_set_bass_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_bass =
        +[](uint8_t seq_id)
        {
            log_debug("Request get bass(seq_id: %d)", seq_id);
            actionslink_send_get_bass_response(seq_id, getProperty<Tua::BassLevel>().value);
        },
    .on_request_set_treble =
        +[](uint8_t seq_id, int32_t treble)
        {
            log_debug("Request set treble(seq_id: %d, treble: %d)", seq_id, treble);
            Teufel::Task::Audio::postMessage(ot_id, Tua::TrebleLevel{.value = static_cast<int8_t>(treble)});
            actionslink_send_set_treble_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_treble =
        +[](uint8_t seq_id)
        {
            log_debug("Request get treble(seq_id: %d)", seq_id);
            actionslink_send_get_treble_response(seq_id, getProperty<Tua::TrebleLevel>().value);
        },
    .on_request_set_battery_friendly_charging =
        +[](uint8_t seq_id, bool is_enabled)
        {
            log_debug("Request set battery friendly charging(seq_id: %d, state: %d)", seq_id, is_enabled);
            Teufel::Task::Audio::postMessage(ot_id, is_enabled ? Tus::ChargeType::BatteryFriendly
                                                               : Tus::ChargeType::FastCharge);
            actionslink_send_set_battery_friendly_charging_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_battery_friendly_charging =
        +[](uint8_t seq_id)
        {
            actionslink_send_get_battery_friendly_charging_response(
                seq_id, isProperty(Ux::System::ChargeType::BatteryFriendly));
        },
};

static const actionslink_event_handlers_t actionslink_event_handlers = {
    .on_notify_system_ready = +[]() { postMessage(ot_id, ActionsReady{}); },
    .on_notify_power_state =
        +[](actionslink_power_state_t power_state)
        {
            auto rpi_state = Teufel::Core::mapValue(PowerStateMapper, power_state);
            if (!rpi_state.has_value())
            {
                log_warning("Unknown power state: %d", power_state);
                return;
            }
            if (rpi_state.value() == Tur::PowerState::Off || rpi_state.value() == Tur::PowerState::ShutdownRequested)
            {
                clear_pending_wifi_command();
            }
            log_debug("RPi power: %s", getDesc(rpi_state.value()));
            setProperty(Tur::PowerState{rpi_state.value()});
        },
    .on_notify_stream_state =
        +[](bool is_streaming)
        {
            log_debug("Streaming active: %s", is_streaming ? "true" : "false");
            setProperty(Tur::StreamingActive{is_streaming});
        },
    .on_notify_host_source =
        +[](actionslink_host_source_t source_type)
        {
            Tur::HostSource src =
                Teufel::Core::mapValue(HostSourceMapper, source_type).value_or(Tur::HostSource::Unknown);
            log_debug("Host source: %s", getDesc(src));
            setProperty(Tur::HostSource{src});
        },
    .on_notify_play_led_pattern =
        +[](actionslink_led_pattern_t pattern)
        {
            log_debug("Received play_led_pattern notify (pattern: %d)", pattern);
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
    .on_notify_wifi_info =
        +[](const char *ssid, const char *ip_address, const char *username)
        {
            const char *resolved_ssid = (ssid != nullptr && ssid[0] != '\0') ? ssid : "unknown";
            const char *resolved_ip   = (ip_address != nullptr && ip_address[0] != '\0') ? ip_address : "unknown";
            const char *resolved_user = (username != nullptr && username[0] != '\0') ? username : "unknown";
            log_info("WiFi up:");
            log_info(" SSID: %s", resolved_ssid);
            log_info(" USER: %s", resolved_user);
            log_info(" IP:   %s", resolved_ip);
        },
    .on_notify_wifi_command_result =
        +[](uint32_t command_id, actionslink_wifi_command_action_t action, actionslink_error_t status,
            bool target_reached, actionslink_wifi_command_detail_t detail)
        {
            auto mapped_action =
                Teufel::Core::mapValue(WifiCommandActionMapper, action).value_or(Tur::WifiCommandAction::Unknown);
            auto mapped_detail =
                Teufel::Core::mapValue(WifiCommandDetailMapper, detail).value_or(Tur::WifiCommandDetail::None);
            if (!s_wifi.pending_command.active)
            {
                log_warning("WiFi cmd %lu unknown (%s, status=%d, detail=%s)", static_cast<unsigned long>(command_id),
                            getDesc(mapped_action), status, getDesc(mapped_detail));
                return;
            }

            if (s_wifi.pending_command.command_id != command_id)
            {
                log_warning("Stale WiFi cmd %lu, waiting for %lu", static_cast<unsigned long>(command_id),
                            static_cast<unsigned long>(s_wifi.pending_command.command_id));
                return;
            }

            log_info("WiFi cmd %lu (%s) done: status=%d detail=%s%s", static_cast<unsigned long>(command_id),
                     getDesc(mapped_action), status, getDesc(mapped_detail),
                     mapped_action == Tur::WifiCommandAction::CycleWifiNetwork
                         ? (target_reached ? ", target=true" : ", target=false")
                         : "");
            Teufel::Task::Leds::set_source_pattern(status == ACTIONSLINK_ERROR_SUCCESS
                                                       ? Teufel::Task::Leds::SourcePattern::PositiveFeedback
                                                       : Teufel::Task::Leds::SourcePattern::NegativeFeedback);
            clear_pending_wifi_command();
        },
};

static const actionslink_config_t actionslink_configuration = {
    .write_buffer_fn = actionslink_write_buffer,
    .read_buffer_fn  = actionslink_read_buffer,
    .get_tick_ms_fn  = get_systick,
    .msp_init_fn     = nullptr,
    .msp_deinit_fn   = nullptr,
    .task_yield_fn   = +[]() { vTaskDelay(pdMS_TO_TICKS(2)); },
    .log_fn          = actionslink_print_log,
    .p_rx_buffer     = actionslink_rx_buffer,
    .p_tx_buffer     = actionslink_tx_buffer,
    .rx_buffer_size  = ACTIONSLINK_RX_BUFFER_SIZE,
    .tx_buffer_size  = ACTIONSLINK_TX_BUFFER_SIZE,
};

static const Teufel::GenericThread::Config<RpiLinkMessage> threadConfig = {
    .Name      = "RPi",
    .StackSize = TASK_RPI_STACK_SIZE,
    .Priority  = TASK_BLUETOOTH_PRIORITY, // Same priority as Bluetooth task it replaces
    .IdleMs    = 10,
    .Callback_Idle =
        []()
    {
        if (isPropertyOneOf(Tus::PowerState::On, Tus::PowerState::PreOn) &&
            !isProperty(Tur::PowerState::ShutdownRequested))
        {
            actionslink_tick();
        }
    },
    .Callback_Init =
        []()
    {
        // Reroute UART to USB-C via D+, D-, and GND by default
        log_warn("Enabling CLI, rerouting UART log output to USB-C via its D+, D-, and GND pins");
        board_link_usb_switch_init();
        bsp_bluetooth_uart_init();
#ifdef ENABLE_INTERNAL_FTDI
        board_link_usb_switch_to_bluetooth();
#else
        board_link_usb_switch_to_uart_debug();
#endif // ENABLE_INTERNAL_FTDI
        board_link_bluetooth_init();
        board_link_bluetooth_set_power(false);
        board_link_bluetooth_reset(true);
        SyncPrimitive::notify(ot_id);
    },
    .QueueSize = QUEUE_SIZE,
    .Callback =
        [](uint8_t /*modid*/, RpiLinkMessage msg)
    {
        std::visit(
            Teufel::Core::overload{

                [](const Teufel::Ux::System::SetPowerState &p)
                {
                    log_info("Power state: %s", getDesc(p.to));
                    switch (p.to)
                    {
                        case Teufel::Ux::System::PowerState::PreOff:
                            log_debug("Sending shutdown request");
                            if (actionslink_set_power_state(ACTIONSLINK_POWER_STATE_OFF) != 0)
                            {
                                log_error("Failed to send OFF request");
                            }
                            SyncPrimitive::notify(ot_id);
                            break;
                        case Teufel::Ux::System::PowerState::Off:
                            wait_for_rpi_off();

                            clear_pending_wifi_command();
                            actionslink_deinit();
                            board_link_bluetooth_reset(true);
                            board_link_bluetooth_set_power(false);
                            log_info("RPi shutdown completed");

                            SyncPrimitive::notify(ot_id);
                            break;
                        case Teufel::Ux::System::PowerState::PreOn:
                            clear_pending_wifi_command();
                            board_link_bluetooth_reset(true);
                            board_link_bluetooth_set_power(false);
                            vTaskDelay(500);
                            board_link_bluetooth_reset(false);
                            board_link_bluetooth_set_power(true);
                            bsp_bluetooth_uart_clear_buffer();
                            actionslink_init(&actionslink_configuration, &actionslink_event_handlers,
                                             &actionslink_request_handlers);

                            SyncPrimitive::notify(ot_id);
                            break;
                        case Teufel::Ux::System::PowerState::On:
                            if (actionslink_set_power_state(ACTIONSLINK_POWER_STATE_ON) != 0)
                            {
                                log_error("Failed to send ON request");
                            }
                            SyncPrimitive::notify(ot_id);
                            break;
                        default:
                            log_warn("Received unexpected power state: %s", getDesc(p.to));
                            break;
                    }
                },
                [](const Tur::PlayPause &)
                {
                    log_info("Play/Pause");
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_host_play_pause();
                    }
                },
                [](const Tur::NextTrack &)
                {
                    log_info("Next track");
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_host_next_track();
                    }
                },
                [](const Tur::PreviousTrack &)
                {
                    log_info("Previous track");
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_host_previous_track();
                    }
                },
                [](const Tur::VolumeChange &p)
                {
#ifndef ENABLE_BT_RENDERER_VOLUME_CONTROL
                    auto source = getProperty<Teufel::Ux::RpiLink::HostSource>();
                    if (source == Teufel::Ux::RpiLink::HostSource::Bluetooth)
                    {
                        return;
                    }
#endif
                    log_info("Sending volume %s request to RPi", getDesc(p));
                    if (isProperty(Tur::PowerState::On))
                    {
                        switch (p)
                        {
                            case Tur::VolumeChange::Up:
                                actionslink_increase_volume();
                                break;
                            case Tur::VolumeChange::Down:
                                actionslink_decrease_volume();
                                break;
                        }
                    }
                },
                [](const Tus::BatteryLevel &p)
                {
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_send_battery_level(p.value);
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                },
                [](const Tus::ChargerStatus &p)
                {
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_send_charger_status(Teufel::Core::mapValue(ChargerStatusMapper, p)
                                                            .value_or(ACTIONSLINK_CHARGER_STATUS_NOT_CONNECTED));
                    }
                },
                [](const Tus::ChargeType &p)
                {
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_send_battery_friendly_charging_notification(
                            p == Ux::System::ChargeType::BatteryFriendly);
                    }
                },
                [](const Tur::DragAndDropUpdate &)
                {
                    log_info("Entering MCU's drag-and-drop upd mode, shutting down RPi.");

                    if (isProperty(Tur::PowerState::On))
                    {
                        log_debug("Sending shutdown request");
                        if (actionslink_set_power_state(ACTIONSLINK_POWER_STATE_SHUTDOWN_REQUEST) != 0)
                        {
                            log_error("Failed to send SHUTDOWN_REQUEST");
                        }

                        wait_for_rpi_off();

                        clear_pending_wifi_command();
                        actionslink_deinit();
                        board_link_bluetooth_reset(true);
                        board_link_bluetooth_set_power(false);
                        log_info("RPi shutdown completed");
                    }
                    else
                        log_debug("RPi is already in power-off state");

                    SyncPrimitive::notify(ot_id);
                },
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
#ifdef ENABLE_RPI_LINK_HOTSPOT
                [](const Tur::EnableHotspot &)
                {
                    if (isProperty(Tur::PowerState::On))
                    {
                        const auto command_id = begin_wifi_command(Tur::WifiCommandAction::EnableHotspot);
                        if (!command_id.has_value())
                        {
                            return;
                        }
                        if (actionslink_send_enable_hotspot_command(command_id.value()) != 0)
                        {
                            log_error("Failed to send hotspot cmd");
                            clear_pending_wifi_command();
                        }
                    }
                    else
                        log_error("RPi off, cannot enable hotspot");
                },
#endif // ENABLE_RPI_LINK_HOTSPOT
                [](const Tur::CycleWifiNetwork &)
                {
                    if (isProperty(Tur::PowerState::On))
                    {
                        const auto command_id = begin_wifi_command(Tur::WifiCommandAction::CycleWifiNetwork);
                        if (!command_id.has_value())
                        {
                            return;
                        }
                        if (actionslink_send_cycle_wifi_network_command(command_id.value()) != 0)
                        {
                            log_error("Failed to send cycle WiFi cmd");
                            clear_pending_wifi_command();
                        }
                    }
                    else
                        log_error("RPi off, cannot cycle WiFi");
                },
                [](const Tur::CycleSource &)
                {
                    if (isProperty(Tur::PowerState::On))
                    {
                        actionslink_send_cycle_source_request();
                    }
                    else
                        log_error("RPi not on, cannot cycle source");
                },
                [](const ActionsReady &)
                {
                    log_info("Actionslink is ready");

                    if (isProperty(Tus::PowerState::PreOn))
                    {
                        Task::System::postMessage(
                            ot_id, Tus::SetPowerState{.to     = Tus::PowerState::On,
                                                      .reason = Tus::PowerStateChangeReason::UserRequest});
                    }
                }},
            msg);
    },
    .StackBuffer = rpi_task_stack,
    .StaticTask  = &rpi_task_buffer,
    .StaticQueue = &queue_static,
    .QueueBuffer = queue_static_buffer,
};

int start()
{
    static_assert(sizeof(RpiLinkMessage) <= 16, "Queue message size exceeded 4 bytes!");

    task_handler = Teufel::GenericThread::create(&threadConfig);
    APP_ASSERT(task_handler);

    return 0;
}

int postMessage(Teufel::Ux::System::Task source_task, RpiLinkMessage msg)
{
    if (!task_handler)
    {
        return -1;
    }
    return Teufel::GenericThread::PostMsg(task_handler, static_cast<uint8_t>(static_cast<int>(source_task)), msg);
}

static int actionslink_read_buffer(uint8_t *p_data, uint8_t length, uint32_t timeout)
{
    (void) timeout;
    return bsp_bluetooth_uart_rx(p_data, (uint32_t) length);
}

static int actionslink_write_buffer(const uint8_t *p_data, uint8_t length, uint32_t timeout)
{
    (void) timeout;
    if (p_data != nullptr && length > 0)
    {
        return bsp_bluetooth_uart_tx(p_data, length);
    }
    return 0;
}

static void actionslink_print_log(actionslink_log_level_t level, const char *dsc)
{
    switch (level)
    {
        case ACTIONSLINK_LOG_LEVEL_ERROR:
            log_error("Actions: %s", dsc);
            break;
        case ACTIONSLINK_LOG_LEVEL_WARN:
            log_warning("Actions: %s", dsc);
            break;
        case ACTIONSLINK_LOG_LEVEL_INFO:
            log_info("Actions: %s", dsc);
            break;
        case ACTIONSLINK_LOG_LEVEL_DEBUG:
            log_debug("Actions: %s", dsc);
            break;
        case ACTIONSLINK_LOG_LEVEL_TRACE:
            log_debug("Actions: %s", dsc);
            break;
        default:
            break;
    }
}

}

// Properties public API
namespace Teufel::Ux::RpiLink
{
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::RpiLink, m_rpi_power_state, PowerState)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::RpiLink, m_streaming_active, StreamingActive)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::RpiLink, m_host_source, HostSource)
}

namespace Teufel::Task::Rpi
{
#ifdef ENABLE_CONFIGURE_WIFI_COMMAND
static int cmd_configure_wifi(const struct shell *, size_t argc, char **argv)
{
    const char *ssid     = argv[1];
    const char *password = (argc == 3) ? argv[2] : "";

    strncpy(Teufel::Task::RpiLink::s_wifi.configure_wifi_ssid, ssid, CONFIGURE_WIFI_SSID_MAX - 1);
    Teufel::Task::RpiLink::s_wifi.configure_wifi_ssid[CONFIGURE_WIFI_SSID_MAX - 1] = '\0';
    strncpy(Teufel::Task::RpiLink::s_wifi.configure_wifi_password, password, CONFIGURE_WIFI_PASSWORD_MAX - 1);
    Teufel::Task::RpiLink::s_wifi.configure_wifi_password[CONFIGURE_WIFI_PASSWORD_MAX - 1] = '\0';

    log_info("Configuring WiFi");
    int ret =
        Teufel::Task::RpiLink::postMessage(Teufel::Ux::System::Task::System, Teufel::Ux::RpiLink::ConfigureWifi{});
    if (ret != 0)
    {
        log_error("Failed to post configure_wifi to RPi task");
        return -1;
    }
    return 0;
}
SHELL_CMD_ARG_REGISTER(configure_wifi, NULL, "Configure WiFi on RPi (configure_wifi <ssid> [password])",
                       cmd_configure_wifi, 2, 1);
#endif // ENABLE_CONFIGURE_WIFI_COMMAND
}
