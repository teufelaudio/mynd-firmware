// Due to the flash size limit, only the WARNING level is available
// for use in the complete firmware (including the bootloader).
#if defined(BOOTLOADER)
#define LOG_LEVEL LOG_LEVEL_WARNING
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#include <algorithm>
#include <utility>
#include <optional>
#include <functional>

#include "config.h"
#include "board.h"
#include "board_link.h"
#include "bsp_bluetooth_uart.h"
#include "actionslink.h"
#include "task_priorities.h"
#include "logger.h"

#include "ux/audio/audio.h"

#include "external/teufel/libs/GenericThread/GenericThread++.h"
#include "task_audio.h"
#include "task_bluetooth.h"
#include "task_system.h"

#include "external/teufel/libs/property/property.h"
#include "external/teufel/libs/core_utils/mapper.h"
#include "external/teufel/libs/core_utils/overload.h"
#include "external/teufel/libs/core_utils/sync.h"
#include "external/teufel/libs/app_assert/app_assert.h"
#include "gitversion/version.h"
#include "persistent_storage/kvstorage.h"

#ifdef INCLUDE_PRODUCTION_TESTS
#include "external/teufel/libs/tshell/tshell.h"
#endif // INCLUDE_PRODUCTION_TESTS

#define TASK_BLUETOOTH_STACK_SIZE 448
#define QUEUE_SIZE                8

namespace Teufel::Task::Bluetooth
{

namespace Tua = Teufel::Ux::Audio;
namespace Tub = Teufel::Ux::Bluetooth;
namespace Tus = Teufel::Ux::System;
namespace Tua = Teufel::Ux::Audio;

static Teufel::Ux::System::Task                                ot_id        = Teufel::Ux::System::Task::Bluetooth;
static Teufel::GenericThread::GenericThread<BluetoothMessage> *task_handler = nullptr;

static PropertyNonOpt<Tub::Status> m_bt_status{"bt status", Tub::Status::None, Tub::Status::None};
PROPERTY_ENUM_SET(Tub::Status, m_bt_status)

static PropertyNonOpt<decltype(Tub::StreamingActive::value)> m_streaming_active{"streaming active", false, false};
PROPERTY_SET(Tub::StreamingActive, m_streaming_active)

static StaticTask_t bluetooth_task_buffer;
static StackType_t  bluetooth_task_stack[TASK_BLUETOOTH_STACK_SIZE];
/* The variable used to hold the queue's data structure. */
static StaticQueue_t queue_static;
static const size_t  queue_item_size = sizeof(GenericThread::QueueMessage<BluetoothMessage>);
static uint8_t       queue_static_buffer[QUEUE_SIZE * queue_item_size];

static struct
{
    bool                                      is_connected            = false;
    bool                                      is_usb_source_available = false;
    bool                                      usb_plug_connected      = false; // USB plug detection (from Audio task)
    bool                                      dfu_mode_is_active      = false;
    bool                                      was_streaming           = false;
    bool                                      has_received_power_off_confirmation = false;
    bool                                      update_bt_state                     = false;
    uint32_t                                  update_bt_state_ts                  = 0U;
    uint8_t                                   number_of_connected_devices         = 0;
    actionslink_bt_pairing_state_t            pairing_state                       = ACTIONSLINK_BT_PAIRING_STATE_IDLE;
    actionslink_csb_state_t                   csb_state                           = ACTIONSLINK_CSB_STATE_DISABLED;
    std::optional<actionslink_audio_source_t> audio_source                        = std::nullopt;

    // Timestamp of the power on sound icon. This is used to prevent other sound icons from playing.
    // It is some sort of a simple lock mechanism to prevent other sound icons from playing while the power on sound
    // icon is playing.
    uint32_t                 power_on_sound_icon_ts   = 0u;
    actionslink_sound_icon_t curr_sound_icon          = ACTIONSLINK_SOUND_ICON_NONE;
    uint32_t                 curr_sound_icon_begin_ts = 0u;
} s_bluetooth;

// clang-format off
TS_KEY_VALUE_CONST_MAP(SoundIconToLengthMapper, actionslink_sound_icon_t, uint16_t,
                       {ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK, 180},
                       {ACTIONSLINK_SOUND_ICON_CHARGING, 1440},
                       {ACTIONSLINK_SOUND_ICON_BATTERY_LOW, 910},
                       {ACTIONSLINK_SOUND_ICON_BT_PAIRING, 4570},
                       {ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_MASTER_ENTERED, 4570},
                       {ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_SLAVE_PAIRING, 4570},
                       {ACTIONSLINK_SOUND_ICON_POWER_ON, 1670}, )
constexpr uint32_t c_update_bt_state_ts_duration = 200;
// clang-format on

static void actionslink_print_log(actionslink_log_level_t level, const char *dsc);
static int  actionslink_read_buffer(uint8_t *p_data, uint8_t length, uint32_t timeout);
static int  actionslink_write_buffer(const uint8_t *p_data, uint8_t length, uint32_t timeout);

#define ACTIONSLINK_RX_BUFFER_SIZE 64u
uint8_t actionslink_rx_buffer[ACTIONSLINK_RX_BUFFER_SIZE] = {0};

#define ACTIONSLINK_TX_BUFFER_SIZE 32u
uint8_t actionslink_tx_buffer[ACTIONSLINK_TX_BUFFER_SIZE] = {0};

TS_KEY_VALUE_CONST_MAP(CsbStateMapper, actionslink_csb_state_t, Tub::Status,
                       {ACTIONSLINK_CSB_STATE_BROADCASTING, Tub::Status::CsbChainMaster},
                       {ACTIONSLINK_CSB_STATE_RECEIVER_CONNECTED, Tub::Status::ChainSlave},
                       {ACTIONSLINK_CSB_STATE_RECEIVER_PAIRING, Tub::Status::SlavePairing}, )

TS_KEY_VALUE_CONST_MAP(PairingStateMapper, actionslink_bt_pairing_state_t, Tub::Status,
                       {ACTIONSLINK_BT_PAIRING_STATE_BT_PAIRING, Tub::Status::BluetoothPairing},
                       {ACTIONSLINK_BT_PAIRING_STATE_CSB_BROADCASTING, Tub::Status::CsbChainMaster},
                       {ACTIONSLINK_BT_PAIRING_STATE_CSB_RECEIVING, Tub::Status::SlavePairing})

TS_KEY_VALUE_CONST_MAP(SoundIconMapper, Tub::Status, actionslink_sound_icon_t,
                       {Tub::Status::CsbChainMaster, ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_MASTER_ENTERED},
                       {Tub::Status::ChainSlave, ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_CONNECTED},
                       {Tub::Status::SlavePairing, ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_SLAVE_PAIRING})

TS_KEY_VALUE_CONST_MAP(ColorMapper, Tus::Color, actionslink_device_color_t,
                       {Tus::Color::Black, ACTIONSLINK_DEVICE_COLOR_BLACK},
                       {Tus::Color::White, ACTIONSLINK_DEVICE_COLOR_WHITE},
                       {Tus::Color::Berry, ACTIONSLINK_DEVICE_COLOR_BERRY},
                       {Tus::Color::Mint, ACTIONSLINK_DEVICE_COLOR_MINT}, )

TS_KEY_VALUE_CONST_MAP(ChargerStatusMapper, Tus::ChargerStatus, actionslink_charger_status_t,
                       {Tus::ChargerStatus::NotConnected, ACTIONSLINK_CHARGER_STATUS_NOT_CONNECTED},
                       {Tus::ChargerStatus::Active, ACTIONSLINK_CHARGER_STATUS_ACTIVE},
                       {Tus::ChargerStatus::Inactive, ACTIONSLINK_CHARGER_STATUS_INACTIVE},
                       {Tus::ChargerStatus::Fault, ACTIONSLINK_CHARGER_STATUS_FAULT}, )

TS_KEY_VALUE_CONST_MAP(MultichainExitReasonMapper, Tub::MultichainExitReason, actionslink_csb_master_exit_reason_t,
                       {Tub::MultichainExitReason::Unknown, ACTIONSLINK_CSB_MASTER_EXIT_REASON_UNKNOWN},
                       {Tub::MultichainExitReason::UserRequest, ACTIONSLINK_CSB_MASTER_EXIT_REASON_USER_REQUEST},
                       {Tub::MultichainExitReason::PowerOff, ACTIONSLINK_CSB_MASTER_EXIT_REASON_POWER_OFF}, )

#ifdef INCLUDE_PRODUCTION_TESTS
static void hex_to_mac(uint64_t hexNumber, char *formattedMACAddr)
{
    // Define a mask for each byte in the 64-bit number
    uint64_t byteMask = 0xFFULL;

    // Iterate over each byte in the 64-bit number
    for (int i = 5; i >= 0; i--)
    {
        // Extract the current byte
        uint8_t currentByte = (hexNumber >> (i * 8)) & byteMask;

        // Print the byte in MAC address format
        if (i == 5)
            sprintf(formattedMACAddr, "%02X", currentByte);
        else
            sprintf(formattedMACAddr + strlen(formattedMACAddr), " %02X", currentByte);
    }
}
#endif // INCLUDE_PRODUCTION_TESTS

static int get_bt_fw_version(actionslink_firmware_version_t *p_version)
{
    uint8_t                  build_str_buffer[32] = {0};
    actionslink_buffer_dsc_t build_str            = {
                   .p_buffer    = build_str_buffer,
                   .buffer_size = sizeof(build_str_buffer),
    };
    p_version->p_build_string = &build_str;

    if (actionslink_get_firmware_version(p_version) == 0)
    {
        return 0;
    }
    return -1;
}

static void continue_streaming_check()
{
    if (s_bluetooth.was_streaming && not isPropertyOneOf(Tub::Status::BluetoothPairing, Tub::Status::SlavePairing))
    {
        postMessage(ot_id, Teufel::Ux::Bluetooth::PlayPause{});
        s_bluetooth.was_streaming = false;
    }
}

static bool power_on_sound_icon_played()
{
    // Once power_on sound icon is played, the timestamp is set to UINT32_MAX(even if sound icons disabled)
    return s_bluetooth.power_on_sound_icon_ts == UINT32_MAX;
}

static void handle_new_bt_state()
{
    // Can't make decisions about BT state until audio source is known
    if (not s_bluetooth.audio_source.has_value())
    {
        return;
    }

    // Do not trigger any indications until the power on sound icon is played
    if (not power_on_sound_icon_played() && not isProperty(Tua::SoundIconsActive{false}))
    {
        return;
    }

    auto mapped_csb_state     = Teufel::Core::mapValue(CsbStateMapper, s_bluetooth.csb_state);
    auto mapped_pairing_state = Teufel::Core::mapValue(PairingStateMapper, s_bluetooth.pairing_state);

    Tub::Status bt_state;
    if (mapped_csb_state.has_value())
    {
        bt_state = mapped_csb_state.value();
    }
    else if (mapped_pairing_state.has_value())
    {
        bt_state = mapped_pairing_state.value();
    }
    else if (s_bluetooth.audio_source.value() == ACTIONSLINK_AUDIO_SOURCE_ANALOG &&
             board_link_plug_detection_is_jack_connected())
    {
        bt_state = Tub::Status::AuxConnected;
    }
    else if (s_bluetooth.dfu_mode_is_active)
    {
        bt_state = Tub::Status::DfuMode;
    }
    else if (s_bluetooth.audio_source.value() == ACTIONSLINK_AUDIO_SOURCE_USB && s_bluetooth.is_usb_source_available)
    {
        bt_state = Tub::Status::UsbConnected;
    }
    else if (s_bluetooth.is_connected)
    {
        bt_state = Tub::Status::BluetoothConnected;
    }
    else
    {
        bt_state = Tub::Status::BluetoothDisconnected;
    }

    auto previous_bt_state = getProperty<Tub::Status>();
    // Ignore new CSB Master state if we are already in active CSB Master state,
    // avoids duplicate playing of CSB sound icon due to audio source change
    if ((previous_bt_state == Tub::Status::CsbChainMaster && bt_state == Tub::Status::CsbChainMaster) ||
        (previous_bt_state == Tub::Status::ChainSlave && bt_state == Tub::Status::ChainSlave))
        return;
    else
        setProperty(bt_state);

    auto mapped_sound_icon = Teufel::Core::mapValue(SoundIconMapper, bt_state);
    if (mapped_sound_icon.has_value())
    {
        bool repeat_forever = (bt_state == Tub::Status::BluetoothPairing || bt_state == Tub::Status::SlavePairing);
        postMessage(ot_id,
                    Tua::RequestSoundIcon{mapped_sound_icon.value(),
                                          ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, repeat_forever});
    }
    else
    {
        // No sound icon mapped, that means the BT module is not in a pairing mode and not in CSB mode either
        switch (previous_bt_state)
        {
            using enum Teufel::Ux::Bluetooth::Status;
            case BluetoothPairing:
            case SlavePairing:
                // Do nothing
                break;
            case CsbChainMaster:
            case ChainSlave:
                // The previous status was connected as part of a chain, and now we are not connected anymore,
                // so we need to play the chain disconnected sound icon
                postMessage(ot_id,
                            Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_DISCONNECTED,
                                                  ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT, false});
                break;
            default:
                break;
        }
    }
    Task::Audio::postMessage(ot_id, bt_state);

    continue_streaming_check();
}

static void update_infinite_sound_icons()
{
    const std::pair<actionslink_sound_icon_t, bool (*)()> infinite_sound_icons[] = {
        {ACTIONSLINK_SOUND_ICON_BT_PAIRING, []() { return isProperty(Ux::Bluetooth::Status::BluetoothPairing); }},
        {ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_SLAVE_PAIRING,
         []() { return isProperty(Ux::Bluetooth::Status::SlavePairing); }},
    };

    for (auto [icon, condition] : infinite_sound_icons)
    {
        if (s_bluetooth.curr_sound_icon != icon && condition())
        {
            if (board_get_ms_since(s_bluetooth.curr_sound_icon_begin_ts) >=
                Teufel::Core::mapValue(SoundIconToLengthMapper, s_bluetooth.curr_sound_icon).value_or(0))
            {
                postMessage(ot_id,
                            Tua::RequestSoundIcon{icon, ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, true});
            }
        }
        else if (s_bluetooth.curr_sound_icon == icon && not condition())
        {
            postMessage(ot_id, Tua::StopPlayingSoundIcon{icon});
        }
    }
}

static const actionslink_request_handlers_t actionslink_request_handlers = {
    .on_request_get_mcu_firmware_version =
        +[](uint8_t seq_id)
        {
            log_debug("Request MCU firmware version(seq_id: %d)", seq_id);
            actionslink_send_get_mcu_firmware_version_response(seq_id, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
                                                               nullptr);
        },
    .on_request_get_pdcontroller_firmware_version =
        +[](uint8_t seq_id)
        {
            log_debug("Request PD controller firmware version(seq_id: %d)", seq_id);
            uint8_t pd_version = 0x00;
            if (board_link_usb_pd_controller_fw_version(&pd_version))
            {
                // log_error("Error getting PD controller version");
                return;
            }
            actionslink_send_get_pdcontroller_firmware_version_response(seq_id, pd_version >> 4, pd_version & 0x0F);
        },
    .on_request_get_color =
        +[](uint8_t seq_id)
        {
            log_debug("Request color(seq_id: %d)", seq_id);
            auto color        = Storage::load<Tus::Color>().value_or(Tus::Color::Black);
            auto mapped_color = Teufel::Core::mapValue(ColorMapper, color).value_or(ACTIONSLINK_DEVICE_COLOR_BLACK);
            actionslink_send_get_color_response(seq_id, mapped_color);
        },
    .on_request_set_off_timer =
        +[](uint8_t seq_id, bool is_enabled, uint32_t value)
        {
            log_debug("Request set off timer(seq_id: %d, minutes: %d, state: %d)", seq_id, value, is_enabled);
            Teufel::Task::System::postMessage(ot_id, Teufel::Ux::System::OffTimerEnabled{.value = is_enabled});
            Teufel::Task::System::postMessage(ot_id,
                                              Teufel::Ux::System::OffTimer{.value = static_cast<uint8_t>(value)});
            actionslink_send_set_off_timer_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_off_timer =
        +[](uint8_t seq_id)
        {
            log_debug("Request get off timer(seq_id: %d)", seq_id);
            actionslink_send_get_off_timer_response(seq_id, getProperty<Ux::System::OffTimerEnabled>().value,
                                                    getProperty<Ux::System::OffTimer>().value);
        },
    .on_request_set_brightness =
        +[](uint8_t seq_id, uint32_t value)
        {
            log_debug("Request set brightness(seq_id: %d, brightness: %d)", seq_id, value);
            value = std::clamp<uint32_t>(value, 0u, 100u);
            Teufel::Task::Audio::postMessage(ot_id,
                                             Teufel::Ux::System::LedBrightness{.value = static_cast<uint8_t>(value)});
            actionslink_send_set_brightness_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_brightness =
        +[](uint8_t seq_id)
        {
            log_debug("Request get brightness(seq_id: %d)", seq_id);
            actionslink_send_get_brightness_response(seq_id, getProperty<Ux::System::LedBrightness>().value);
        },
    .on_request_set_bass =
        +[](uint8_t seq_id, int32_t bass)
        {
            log_debug("Request set bass(seq_id: %d, bass: %d)", seq_id, bass);
            // bass = std::clamp<int32_t>(bass, CONFIG_DSP_BASS_MIN, CONFIG_DSP_BASS_MAX);
            Teufel::Task::Audio::postMessage(ot_id, Teufel::Ux::Audio::BassLevel{.value = static_cast<int8_t>(bass)});
            actionslink_send_set_bass_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_bass =
        +[](uint8_t seq_id)
        {
            log_debug("Request get bass(seq_id: %d)", seq_id);
            actionslink_send_get_bass_response(seq_id, getProperty<Ux::Audio::BassLevel>().value);
        },
    .on_request_set_treble =
        +[](uint8_t seq_id, int32_t treble)
        {
            log_debug("Request set treble(seq_id: %d, treble: %d)", seq_id, treble);
            // treble = std::clamp<int32_t>(treble, CONFIG_DSP_TREBLE_MIN, CONFIG_DSP_TREBLE_MAX);
            Teufel::Task::Audio::postMessage(ot_id,
                                             Teufel::Ux::Audio::TrebleLevel{.value = static_cast<int8_t>(treble)});
            actionslink_send_set_treble_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_treble =
        +[](uint8_t seq_id)
        {
            log_debug("Request get treble(seq_id: %d)", seq_id);
            actionslink_send_get_treble_response(seq_id, getProperty<Ux::Audio::TrebleLevel>().value);
        },
    .on_request_set_eco_mode =
        +[](uint8_t seq_id, bool is_enabled)
        {
            log_debug("Request set eco mode(seq_id: %d, state: %d)", seq_id, is_enabled);
            Teufel::Task::Audio::postMessage(ot_id, Ux::Audio::EcoMode{.value = is_enabled});
            actionslink_send_set_eco_mode_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_eco_mode =
        +[](uint8_t seq_id)
        {
            log_debug("Request get eco mode(seq_id: %d)", seq_id);
            actionslink_send_get_eco_mode_response(seq_id, getProperty<Ux::Audio::EcoMode>().value);
        },
    .on_request_set_sound_icons =
        +[](uint8_t seq_id, bool is_enabled)
        {
            log_debug("Request set sound icons(seq_id: %d, state: %d)", seq_id, is_enabled);
            Teufel::Task::Audio::postMessage(ot_id, Ux::Audio::SoundIconsActive{.value = is_enabled});
            actionslink_send_set_sound_icons_response(seq_id, ACTIONSLINK_ERROR_SUCCESS);
        },
    .on_request_get_sound_icons =
        +[](uint8_t seq_id)
        {
            log_debug("Request get sound icons(seq_id: %d)", seq_id);
            actionslink_send_get_sound_icons_response(seq_id, getProperty<Ux::Audio::SoundIconsActive>().value);
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
    .on_request_get_battery_capacity =
        +[](uint8_t seq_id)
        {
            log_debug("Request get battery capacity(seq_id: %d)", seq_id);
            // TODO: send the actual battery capacity once it is implemented
            actionslink_send_get_battery_capacity_response(seq_id, 4900);
        },
    .on_request_get_battery_max_capacity =
        +[](uint8_t seq_id)
        {
            log_debug("Request get battery max capacity(seq_id: %d)", seq_id);
            actionslink_send_get_battery_max_capacity_response(seq_id, 4900);
        },
};

// WARNING: Do not call actionslink APIs within this handler, this explodes the stack usage of this task
static const actionslink_event_handlers_t actionslink_event_handlers = {
    .on_notify_system_ready =
        +[]()
        {
            postMessage(ot_id, ActionsReady{});
            postMessage(ot_id, Tus::BatteryLevel{getProperty<Ux::System::BatteryLevel>().value});
            postMessage(ot_id, Tub::NotifyAuxConnectionChange{board_link_plug_detection_is_jack_connected()});
            postMessage(ot_id, Tub::NotifyUsbConnectionChange{s_bluetooth.usb_plug_connected});
            postMessage(ot_id, getProperty<Tus::ChargerStatus>());
        },
    .on_notify_power_state =
        +[](actionslink_power_state_t power_state)
        {
            switch (power_state)
            {
                case ACTIONSLINK_POWER_STATE_OFF:
                    log_info("Actions power: OFF");
                    s_bluetooth.has_received_power_off_confirmation = true;
                    break;
                case ACTIONSLINK_POWER_STATE_ON:
                    log_info("Actions power: ON");
                    break;
                case ACTIONSLINK_POWER_STATE_STANDBY:
                    log_info("Actions power: STANDBY");
                    break;
                default:
                    break;
            }
        },
    .on_notify_audio_source =
        [](actionslink_audio_source_t audio_source)
    {
        if (s_bluetooth.audio_source == audio_source)
        {
            return;
        }

        log_info("Audio source changed to %d", audio_source);
        s_bluetooth.audio_source       = audio_source;
        s_bluetooth.update_bt_state    = true;
        s_bluetooth.update_bt_state_ts = get_systick();
    },
    .on_notify_volume =
        +[](const actionslink_volume_t *p_volume)
        {
            switch (p_volume->kind)
            {
                case ACTIONSLINK_VOLUME_KIND_PERCENT:
                    log_info("Volume changed to %d%%", p_volume->volume.percent);
                    break;
                case ACTIONSLINK_VOLUME_KIND_ABSOLUTE_AVRCP:
                    log_info("Volume changed to AVRCP %d", p_volume->volume.absolute_avrcp);
                    Teufel::Task::Audio::postMessage(ot_id, Tua::UpdateVolume{p_volume->volume.absolute_avrcp});
                    break;
                case ACTIONSLINK_VOLUME_KIND_DB:
                    log_info("Volume changed to %d dB", p_volume->volume.db);
                    break;
            }
        },
    .on_notify_stream_state =
        +[](bool is_streaming)
        {
            setProperty(Tub::StreamingActive{is_streaming});

            // Playback that is either played OR paused is user activity
            //
            // Cases where consistant loud music is being streamed causes is_streaming to be active for > IDLE_TIME.
            // Then, as soon as volume is turned down or a quite point is reached due to either a song change or quite
            // portion, the speaker powers down because is_streaming is updated to inactive and it has been > IDLE_TIME
            // since its been last updated.
            Task::System::postMessage(ot_id, Tus::UserActivity{});
        },
    .on_notify_bt_a2dp_data = +[](actionslink_a2dp_data_t *p_a2dp_data) {},
    .on_notify_bt_avrcp_state =
        +[](actionslink_avrcp_state_t avrcp_state) { log_info("BT AVRCP state: %d", avrcp_state); },
    .on_notify_bt_avrcp_track_changed          = +[](uint64_t track_id) {},
    .on_notify_bt_avrcp_track_position_changed = +[](uint32_t ms_since_start) {},
    .on_notify_bt_connection =
        +[](uint64_t address)
        {
            log_info("Got BT connection event: 0x%012X", address);
            postMessage(ot_id, Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_BT_CONNECTED,
                                                     ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, false});
            if (s_bluetooth.number_of_connected_devices < 2)
            {
                s_bluetooth.number_of_connected_devices++;
            }
            else
            {
#if !defined(BOOTLOADER)
                log_warn("Two devices are already connected, ignoring connection event");
#endif
            }
        },
    .on_notify_bt_disconnection =
        +[](uint64_t address, actionslink_bt_disconnection_t disconnection_type)
        {
            log_info("Got BT disconnection event: 0x%012X, type %d", address, disconnection_type);
            if (s_bluetooth.number_of_connected_devices > 0)
            {
                s_bluetooth.number_of_connected_devices--;
                postMessage(ot_id,
                            Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_BT_DISCONNECTED,
                                                  ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT, false});
            }
            else
            {
                log_warn("No devices are connected, ignoring disconnection event");
            }
        },
    .on_notify_bt_device_paired = +[](uint64_t address) { log_info("Got BT device paired event: 0x%012X", address); },
    .on_notify_bt_pairing_state =
        +[](actionslink_bt_pairing_state_t state)
        {
            log_info("BT pairing state: %d", state);
            s_bluetooth.pairing_state      = state;
            s_bluetooth.update_bt_state    = true;
            s_bluetooth.update_bt_state_ts = get_systick();
        },
    .on_notify_bt_connection_state =
        +[](bool is_bt_connected)
        {
            log_info("BT %s", is_bt_connected ? "connected" : "disconnected");
            s_bluetooth.is_connected = is_bt_connected;

            s_bluetooth.update_bt_state    = true;
            s_bluetooth.update_bt_state_ts = get_systick();
        },
    .on_notify_csb_state =
        +[](actionslink_csb_state_t csb_state, actionslink_csb_receiver_disconnect_reason_t disconnect_reason)
        {
            log_info("CSB state: %d, disconnect reason: %d", csb_state, disconnect_reason);
            s_bluetooth.csb_state          = csb_state;
            s_bluetooth.update_bt_state    = true;
            s_bluetooth.update_bt_state_ts = get_systick();
            if (disconnect_reason == ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_POWER_OFF)
            {
                postMessage(ot_id, Tus::SetPowerState{.to     = Tus::PowerState::Off,
                                                      .reason = Tus::PowerStateChangeReason::BroadcasterPowerOff});
            }
        },
    .on_notify_usb_connected =
        +[](bool is_usb_connected)
        {
            // BT module handles source switching automatically, no need to do anything here
            log_info("USB source %s", is_usb_connected ? "connected" : "disconnected");

            // Mute and unmute amps to prevent pop noise.
            // The delay amount of 200 ms is derived from testing
            if (!is_usb_connected && isProperty(Tub::Status::UsbConnected))
            {
                board_link_amps_mute(true);
                vTaskDelay(pdMS_TO_TICKS(200));
                board_link_amps_mute(false);
            }

            s_bluetooth.is_usb_source_available = is_usb_connected;

            if (is_usb_connected && not isPropertyOneOf(Tub::Status::CsbChainMaster, Tub::Status::ChainSlave))
            {
                log_info("USB detected, stop pairing");
                postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
            }
        },
    .on_notify_dfu_mode =
        +[](bool is_dfu_mode_active)
        {
            log_info("DFU mode %s", is_dfu_mode_active ? "active" : "inactive");
            s_bluetooth.dfu_mode_is_active = is_dfu_mode_active;
            s_bluetooth.update_bt_state    = true;
            s_bluetooth.update_bt_state_ts = get_systick();
        },
    .on_app_packet = +[]() {},
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

static const GenericThread::Config<BluetoothMessage> threadConfig = {
    .Name      = "Bluetooth",
    .StackSize = TASK_BLUETOOTH_STACK_SIZE,
    .Priority  = TASK_BLUETOOTH_PRIORITY,
    .IdleMs    = 10,
    .Callback_Idle =
        []()
    {
        // Once power_on sound icon is played, the timestamp is set to UINT32_MAX(even if sound icons disabled),
        // and we need to check the BT status (e.g. to check pairing state, and run the next sound icon).
        if (s_bluetooth.power_on_sound_icon_ts != 0u && s_bluetooth.power_on_sound_icon_ts != UINT32_MAX)
        {
            if (board_get_ms_since(s_bluetooth.power_on_sound_icon_ts) >
                Teufel::Core::mapValue(SoundIconToLengthMapper, ACTIONSLINK_SOUND_ICON_POWER_ON))
            {
                s_bluetooth.power_on_sound_icon_ts = UINT32_MAX;
                s_bluetooth.update_bt_state        = true;
                s_bluetooth.update_bt_state_ts     = get_systick();
            }
        }

        // Must be called before handle_new_bt_state changes bt status
        update_infinite_sound_icons();

        if (s_bluetooth.update_bt_state == true)
        {
            if (board_get_ms_since(s_bluetooth.update_bt_state_ts) > c_update_bt_state_ts_duration &&
                board_get_ms_since(s_bluetooth.power_on_sound_icon_ts) > 1000)
            {
                handle_new_bt_state();
                s_bluetooth.update_bt_state = false;
            }
        }

        if (isProperty(Tus::PowerState::On))
        {
            actionslink_tick();
        }
    },
    .Callback_Init =
        []()
    {
        bsp_bluetooth_uart_init();
        board_link_bluetooth_init();
        board_link_bluetooth_set_power(false);
        board_link_bluetooth_reset(true);

        board_link_usb_switch_init();
        board_link_usb_switch_to_bluetooth();
        SyncPrimitive::notify(ot_id);
    },
    .QueueSize = QUEUE_SIZE,
    .Callback =
        [](uint8_t /*modid*/, BluetoothMessage msg)
    {
        std::visit(
            Teufel::Core::overload{
                [](const Teufel::Ux::System::SetPowerState &p)
                {
                    log_info("Bluetooth power state: %s", getDesc(p.to));
                    switch (p.to)
                    {
                        case Tus::PowerState::PreOff:
                        {
                            // Wait until the sound icon is played completely
                            // UX spec says that the power off sound icon is 1.832 seconds long
                            // We need to mute the amps immediately after playing the power off sound icon to prevent
                            // music from playing after the sound icon is played (can't send a command to pause in AUX
                            // source) There is some delay between here and the audio task receiving the power off
                            // command, so we need consider that the sound icon is played completely a bit before it's
                            // actually done
                            // TODO: Investigate this delay, it seems suspiciously and unnecessarily long (50-70 ms)
                            vTaskDelay(pdMS_TO_TICKS(1780));
                            break;
                        }

                        case Tus::PowerState::Off:
                        {
                            s_bluetooth.has_received_power_off_confirmation = false;
                            s_bluetooth.power_on_sound_icon_ts =
                                0u; // IMPORTANT! needs to be reset when charger is connected
                            if (actionslink_set_power_state(ACTIONSLINK_POWER_STATE_OFF) != 0)
                            {
                                log_error("BT power off request failed");
                            }

                            // If power off is requested while in DFU mode with USB connected,
                            // we get stuck in this loop because BT module never confirms. So we need a timeout.
                            auto ts = get_systick();
                            while (board_get_ms_since(ts) < 1000 && not s_bluetooth.has_received_power_off_confirmation)
                            {
                                vTaskDelay(pdMS_TO_TICKS(10));
                                actionslink_tick();
                            }
                            if (not s_bluetooth.has_received_power_off_confirmation)
                                log_error("Confirmation from BT timed out");

                            actionslink_deinit();
                            board_link_bluetooth_reset(true);
                            board_link_bluetooth_set_power(false);

                            s_bluetooth.power_on_sound_icon_ts = 0u;

                            break;
                        }

                        case Tus::PowerState::On:
                        {
                            board_link_bluetooth_reset(false);
                            board_link_bluetooth_set_power(true);

                            bsp_bluetooth_uart_clear_buffer();
                            actionslink_init(&actionslink_configuration, &actionslink_event_handlers,
                                             &actionslink_request_handlers);

                            while (not actionslink_is_ready())
                            {
                                vTaskDelay(pdMS_TO_TICKS(10));
                                actionslink_tick();
                            }

                            actionslink_firmware_version_t version = {0};
                            if (get_bt_fw_version(&version) == 0)
                            {
                                log_warn("Actions FW version: %d.%d.%d%s", version.major, version.minor, version.patch,
                                         version.p_build_string->p_buffer);
                            }

                            uint8_t pd_version = 0x00;
                            if (board_link_usb_pd_controller_fw_version(&pd_version) == 0)
                            {
                                log_warn("PD controller FW version: %d.%d", pd_version >> 4, pd_version & 0x0F);
                            }

                            vTaskDelay(pdMS_TO_TICKS(200));

                            if (actionslink_set_power_state(ACTIONSLINK_POWER_STATE_ON) != 0)
                            {
                                log_error("Failed to request power on");
                            }

                            while (not s_bluetooth.audio_source.has_value())
                            {
                                vTaskDelay(pdMS_TO_TICKS(10));
                                actionslink_tick();
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    SyncPrimitive::notify(ot_id);
                },
                [](const Teufel::Ux::System::BatteryLevel &p)
                {
                    log_dbg("Report battery level: %u", p.value);
                    actionslink_send_battery_level(p.value);
                },
                [](const Teufel::Ux::System::ChargerStatus &p)
                {
                    if (!isProperty(Teufel::Ux::System::PowerState::Off))
                        actionslink_send_charger_status(Teufel::Core::mapValue(ChargerStatusMapper, p)
                                                            .value_or(ACTIONSLINK_CHARGER_STATUS_NOT_CONNECTED));
                },
                [](const Teufel::Ux::System::ChargeType &p)
                {
                    if (not isProperty(Tus::PowerState::Off))
                        actionslink_send_battery_friendly_charging_notification(
                            p == Ux::System::ChargeType::BatteryFriendly);
                },
                [](const Teufel::Ux::System::Color &p)
                {
                    auto color = Teufel::Core::mapValue(ColorMapper, p).value_or(ACTIONSLINK_DEVICE_COLOR_BLACK);
                    if (actionslink_send_color_id(color) != 0)
                    {
                        // log_error("Failed to send color");
                    }
                },
                [](const ActionsReady &)
                {
                    log_info("Actions is ready");
                    // Exit DFU mode if active
                    s_bluetooth.dfu_mode_is_active = false;
                },
                [](const Teufel::Ux::Bluetooth::BtWakeUp &)
                {
                    log_highlight("BT wakeup");

                    if (!s_bluetooth.audio_source.has_value())
                    {
#if !defined(BOOTLOADER)
                        log_error("Got wakeup but audio source is not set yet");
#endif
                        return;
                    }

                    // TODO: Confirm with UX on whether we want to do this regardless of audio source
                    log_debug("Enabling bluetooth reconnection");
                    if (actionslink_enable_bt_reconnection(true) != 0)
                    {
                        log_error("Failed to enable bluetooth reconnection");
                    }
                },
                [](const Teufel::Ux::Bluetooth::VolumeChange &p)
                {
                    log_info("Volume %s", getDesc(p));
                    switch (p)
                    {
                        case Tub::VolumeChange::Up:
                            actionslink_increase_volume();
                            break;
                        case Tub::VolumeChange::Down:
                            actionslink_decrease_volume();
                            break;
                    }
                },
                [](const Teufel::Ux::Bluetooth::StartPairing &p)
                {
                    log_highlight("Start pairing");
                    s_bluetooth.was_streaming = getProperty<Tub::StreamingActive>().value;
                    if (actionslink_start_bt_pairing() != 0)
                    {
                        // log_error("Failed to start pairing");
                    }
                },
                [](const Teufel::Ux::Bluetooth::MultichainPairing &p)
                {
                    log_highlight("Start multichain pairing");
                    s_bluetooth.was_streaming = getProperty<Tub::StreamingActive>().value;
                    if (actionslink_start_multichain_pairing() != 0)
                        log_error("Failed to start multichain pairing");
                },
                [](const Teufel::Ux::Bluetooth::StopPairingAndMultichain &p)
                {
                    log_highlight("Stopping pairing and multichain. Reason: %d", p.reason);
                    if (s_bluetooth.pairing_state == ACTIONSLINK_BT_PAIRING_STATE_BT_PAIRING)
                    {
                        if (actionslink_stop_pairing() != 0)
                        {
                            log_error("Failed to stop pairing");
                        }
                    }

                    if (s_bluetooth.csb_state != ACTIONSLINK_CSB_STATE_DISABLED)
                    {
                        if (actionslink_exit_csb_mode(Teufel::Core::mapValue(MultichainExitReasonMapper, p.reason)
                                                          .value_or(ACTIONSLINK_CSB_MASTER_EXIT_REASON_UNKNOWN)) != 0)
                        {
                            log_error("Failed to exit CSB mode");
                        }
                    }
                },
                [](const Teufel::Ux::Bluetooth::NotifyAuxConnectionChange &p)
                {
                    log_debug("Notifying aux connection change: %d", p.connected);
                    if (actionslink_send_aux_connection_notification(p.connected) != 0)
                    {
                        log_error("Failed to notify aux connection");
                    }
                },
                [](const Teufel::Ux::Bluetooth::NotifyUsbConnectionChange &p)
                {
                    log_debug("Notifying USB connection change: %d", p.connected);
                    s_bluetooth.usb_plug_connected = p.connected;
                    if (actionslink_send_usb_connection_notification(p.connected) != 0)
                    {
                        log_error("Failed to notify USB con");
                    }
                },
                [](const Teufel::Ux::Bluetooth::EnterDfuMode &p)
                {
                    log_highlight("Entering DFU mode");
                    if (actionslink_enter_dfu_mode() != 0)
                    {
#if !defined(BOOTLOADER)
                        log_error("Failed to enter DFU mode");
#endif
                    }
                },
                [](const Teufel::Ux::Bluetooth::ClearDeviceList &)
                {
                    log_highlight("Clearing paired device list");
                    // This disconnects all devices and deletes all paired devices
                    if (actionslink_clear_bt_paired_device_list() != 0)
                    {
                        log_error("Failed to clear device list");
                    }

                    postMessage(ot_id,
                                Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK,
                                                      ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, false});

                    postMessage(ot_id, Teufel::Ux::Bluetooth::StartPairing{});
                },
                [](Tus::FactoryReset &)
                {
                    // Clear paired device list
                    if (actionslink_clear_bt_paired_device_list() != 0)
#if !defined(BOOTLOADER)
                    {
                        log_error("Failed to clear paired device list");
                    }
                    else
                        log_info("Cleared paired device list");
#endif

                    // Set volume to 40%
                    if (actionslink_set_bt_absolute_avrcp_volume(CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME) != 0)
                    {
#if !defined(BOOTLOADER)
                        log_error("Failed to set default avrcp volume");
#endif
                    }
                    else
                        log_info("AVRCP Volume: %d", getProperty<Tua::VolumeLevel>().value);

                    s_bluetooth.number_of_connected_devices = 0;
                    log_info("Connected devices: %d", s_bluetooth.number_of_connected_devices);

#ifdef INCLUDE_PRODUCTION_TESTS
                    Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::AudioBypassProdTest::Exit);
#endif // INCLUDE_PRODUCTION_TESTS

                    // Play factory reset sound icon
                    postMessage(ot_id,
                                Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK,
                                                      ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, false});
                },
                [](const Teufel::Ux::Bluetooth::PlayPause &)
                {
                    log_info("Play/Pause");
                    if (!s_bluetooth.audio_source.has_value())
                    {
                        return;
                    }

                    switch (s_bluetooth.audio_source.value())
                    {
                        case ACTIONSLINK_AUDIO_SOURCE_A2DP1:
                        case ACTIONSLINK_AUDIO_SOURCE_A2DP2:
                            actionslink_bt_play_pause();
                            break;
                        case ACTIONSLINK_AUDIO_SOURCE_USB:
                            actionslink_usb_play_pause();
                            break;
                        default:
                            break;
                    }
                },
                [](const Teufel::Ux::Bluetooth::NextTrack &)
                {
                    log_info("Next track");
                    if (!s_bluetooth.audio_source.has_value())
                    {
                        return;
                    }

                    switch (s_bluetooth.audio_source.value())
                    {
                        case ACTIONSLINK_AUDIO_SOURCE_A2DP1:
                        case ACTIONSLINK_AUDIO_SOURCE_A2DP2:
                            actionslink_bt_next_track();
                            break;
                        case ACTIONSLINK_AUDIO_SOURCE_USB:
                            actionslink_usb_next_track();
                            break;
                        default:
                            break;
                    }
                },
                [](const Teufel::Ux::Bluetooth::PreviousTrack &)
                {
                    log_info("Previous track");
                    if (!s_bluetooth.audio_source.has_value())
                    {
                        return;
                    }

                    switch (s_bluetooth.audio_source.value())
                    {
                        case ACTIONSLINK_AUDIO_SOURCE_A2DP1:
                        case ACTIONSLINK_AUDIO_SOURCE_A2DP2:
                            actionslink_bt_previous_track();
                            break;
                        case ACTIONSLINK_AUDIO_SOURCE_USB:
                            actionslink_usb_previous_track();
                            break;
                        default:
                            break;
                    }
                },
                [](const Tua::RequestSoundIcon &p)
                {
                    if (getProperty<Ux::Audio::SoundIconsActive>().value)
                    {
                        // If the sound icon is BT connected, we need to play it immediately after the BT connection,
                        // except if the BT connection is established during(or before) the power on sound icon.
                        // In this case we need to wait for the power on sound icon to finish before playing the BT.
                        if (p.sound_icon == ACTIONSLINK_SOUND_ICON_BT_CONNECTED &&
                            (s_bluetooth.power_on_sound_icon_ts == 0 ||
                             board_get_ms_since(s_bluetooth.power_on_sound_icon_ts) < 500))
                        {
                            vTaskDelay(50);
                            postMessage(ot_id, Tua::RequestSoundIcon{
                                                   ACTIONSLINK_SOUND_ICON_BT_CONNECTED,
                                                   ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, false});
                        }

                        if (p.sound_icon == ACTIONSLINK_SOUND_ICON_POWER_ON)
                        {
                            s_bluetooth.power_on_sound_icon_ts = get_systick();
                        }
#if not defined(BOOTLOADER)
                        log_high("Sound icon (request) (si: %d)", p.sound_icon);
#endif
                        actionslink_play_sound_icon(p.sound_icon, p.playback_mode, p.loop_forever);

                        s_bluetooth.curr_sound_icon          = p.sound_icon;
                        s_bluetooth.curr_sound_icon_begin_ts = get_systick();
                    }
                    else
                    {
                        log_debug("Sound icon play (si: %d) requested. Sound icons inactive.", p.sound_icon);
                    }
                },
                [](const Tua::StopPlayingSoundIcon &p)
                {
                    if (getProperty<Ux::Audio::SoundIconsActive>().value)
                    {
#if not defined(BOOTLOADER)
                        log_high("Stop sound icon request (%u)", static_cast<uint8_t>(p.sound_icon));
#endif
                        int ret = actionslink_stop_sound_icon(p.sound_icon);
                        if (ret < 0)
                        {
                            log_err("stop sound icon status: %d", ret);
                        }
                        s_bluetooth.curr_sound_icon = ACTIONSLINK_SOUND_ICON_NONE;
                    }
                    else
                    {
                        log_debug("Sound icon stop requested. Sound icons inactive.");
                    }
                },
                [](const Tua::EcoMode &p)
                {
                    if (actionslink_send_eco_mode_state(p.value) != 0)
                    {
                        // log_error("Failed to notify eco mode state");
                    }
                },
#ifdef INCLUDE_PRODUCTION_TESTS
                [](Teufel::Ux::Bluetooth::FWVersionProdTest)
                {
                    actionslink_firmware_version_t version = {0};
                    if (get_bt_fw_version(&version) == 0)
                    {
                        printf("BT:%d.%d.%d\r\n", version.major, version.minor, version.patch);
                    }
                },
                [](Teufel::Ux::Bluetooth::DeviceNameProdTest)
                {
                    uint8_t                  device_name_str_buffer[32] = {0};
                    actionslink_buffer_dsc_t p_device_name_buf_dsc      = {
                             .p_buffer    = device_name_str_buffer,
                             .buffer_size = sizeof(device_name_str_buffer),
                    };

                    if (actionslink_get_this_device_name(&p_device_name_buf_dsc) == 0)
                    {
                        printf("NAME=%s\r\n", p_device_name_buf_dsc.p_buffer);
                    }
                },
                [](Teufel::Ux::Bluetooth::BtMacAddressProdTest)
                {
                    uint64_t bt_mac_address;
                    if (actionslink_get_bt_mac_address(&bt_mac_address) == 0)
                    {
                        char formattedMACAddr[18]; // 17 chars for the MAC address +1 for null char
                        hex_to_mac(bt_mac_address, formattedMACAddr);
                        printf("%s\r\n", formattedMACAddr);
                    }
                },
                [](Teufel::Ux::Bluetooth::BleMacAddressProdTest)
                {
                    uint64_t ble_mac_address;
                    if (actionslink_get_ble_mac_address(&ble_mac_address) == 0)
                    {
                        char formattedMACAddr[18]; // 17 chars for the MAC address +1 for null char
                        hex_to_mac(ble_mac_address, formattedMACAddr);
                        printf("%s\r\n", formattedMACAddr);
                    }
                },
                [](Teufel::Ux::Bluetooth::BtRssiProdTest)
                {
                    int8_t rssi_val;
                    if (actionslink_get_bt_rssi_value(&rssi_val) == 0)
                    {
                        printf("RSSI=%d\r\n", rssi_val);
                    }
                },
                [](Teufel::Ux::Bluetooth::SetVolumeProdTest &p)
                {
                    if (p.volume_req > 32)
                    {
                        log_error("Requested volume must be within 0-32");
                        return;
                    }
                    // The AVRCP volume range is 0-127, the requested Prod Test volume range is between 0-32
                    uint8_t avrcp_vol = (p.volume_req * 127) / 32;
                    log_highlight("Setting absolute AVRCP volume to: %d", avrcp_vol);
                    if (actionslink_set_bt_absolute_avrcp_volume(avrcp_vol) == 0)
                    {
                        printf("Vol Set=%.2d\r\n", p.volume_req);
                    }
                },
                [](Teufel::Ux::Bluetooth::AudioBypassProdTest &p)
                {
                    switch (p)
                    {
                        case Tub::AudioBypassProdTest::Enter:
                            if (board_link_amps_setup_woofer(AMP_MODE_BYPASS) != 0)
                            {
                                log_error("Woofer failed to enter Audio Bypass Mode");
                            }

                            if (board_link_amps_setup_tweeter(AMP_MODE_BYPASS) != 0)
                            {
                                log_error("Tweeter failed to enter Audio Bypass Mode");
                            }
                            break;
                        case Tub::AudioBypassProdTest::Exit:
                            if (board_link_amps_setup_woofer(AMP_MODE_NORMAL) != 0)
                            {
                                log_error("Woofer failed to exit Audio Bypass Mode");
                            }

                            if (board_link_amps_setup_tweeter(AMP_MODE_NORMAL) != 0)
                            {
                                log_error("Tweeter failed to exit Audio Bypass Mode");
                            }
                            break;
                    }
                },
#endif // INCLUDE_PRODUCTION_TESTS
            },
            msg);
    },
    .StackBuffer = bluetooth_task_stack,
    .StaticTask  = &bluetooth_task_buffer,
    .StaticQueue = &queue_static,
    .QueueBuffer = queue_static_buffer,
};

int start()
{
    static_assert(sizeof(BluetoothMessage) <= 16, "Queue message size exceeded 4 bytes!");

    task_handler = GenericThread::create(&threadConfig);
    APP_ASSERT(task_handler);

    return 0;
}

int postMessage(Teufel::Ux::System::Task source_task, BluetoothMessage msg)
{
    return GenericThread::PostMsg(task_handler, static_cast<uint8_t>(source_task), msg);
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
namespace Teufel::Ux::Bluetooth
{
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Bluetooth, m_bt_status, Status)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Bluetooth, m_streaming_active, StreamingActive)
}
