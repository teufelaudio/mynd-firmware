// Due to the flash size limit, only the WARNING level is available
// for use in the complete firmware (including the bootloader).
#if defined(BOOTLOADER)
#define LOG_LEVEL LOG_LEVEL_WARNING
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#include "config.h"
#include "battery.h"
#include "board.h"
#include "board_link.h"
#include "bsp_shared_i2c.h"
#include "bsp_usb_pd_i2c.h"
#include "button_handler.h"
#include "input_events.h"
#include "leds.h"
#include "actionslink.h"

#include "external/teufel/libs/property/property.h"
#include "stm32f0xx_hal.h"
#include "persistent_storage/kvstorage.h"

#include "external/teufel/libs/GenericThread/GenericThread++.h"
#include "task_audio.h"
#include "task_bluetooth.h"
#include "task_system.h"
#include "task_priorities.h"
#include "tests.h"

#include "logger.h"
#include "src/tshell/tshell.h"
#include "external/teufel/libs/core_utils/mapper.h"
#include "external/teufel/libs/core_utils/overload.h"
#include "external/teufel/libs/core_utils/sync.h"
#include "external/teufel/libs/core_utils/debouncer.h"
#include "external/teufel/libs/app_assert/app_assert.h"

#ifdef INCLUDE_PRODUCTION_TESTS
#include "tests.h"
#endif

#include "gitversion/version.h"

#define TASK_AUDIO_STACK_SIZE 384
#define QUEUE_SIZE            5

namespace Teufel::Task::Audio
{

namespace Tu  = Teufel::Ux;
namespace Tua = Teufel::Ux::Audio;
namespace Tub = Teufel::Ux::Bluetooth;
namespace Tus = Teufel::Ux::System;

static PropertyNonOpt<decltype(Tua::EcoMode::value)> m_eco_mode{"eco mode", false, false};
PROPERTY_SET(Tua::EcoMode, m_eco_mode)

static PropertyNonOpt<decltype(Tus::LedBrightness::value)> m_led_brightness{
    "brightness", 0, 100, 1, CONFIG_BRIGHTNESS_DEFAULT, CONFIG_BRIGHTNESS_DEFAULT};
PROPERTY_SET(Tus::LedBrightness, m_led_brightness)

static PropertyNonOpt<decltype(Tua::SoundIconsActive::value)> m_sound_icons_active{"sound icons active", true, true};
PROPERTY_SET(Tua::SoundIconsActive, m_sound_icons_active)

static PropertyNonOpt<decltype(Tua::BassLevel::value)> m_bass_level{
    "bass", CONFIG_DSP_BASS_MIN, CONFIG_DSP_BASS_MAX, 1, CONFIG_DSP_BASS_DEFAULT, CONFIG_DSP_BASS_DEFAULT};
PROPERTY_SET(Tua::BassLevel, m_bass_level)

static PropertyNonOpt<decltype(Tua::TrebleLevel::value)> m_treble_level{
    "treble", CONFIG_DSP_TREBLE_MIN, CONFIG_DSP_TREBLE_MAX, 1, CONFIG_DSP_TREBLE_DEFAULT, CONFIG_DSP_TREBLE_DEFAULT};
PROPERTY_SET(Tua::TrebleLevel, m_treble_level)

static PropertyNonOpt<decltype(Tua::VolumeLevel::value)> m_current_avrcp_volume{"volume",
                                                                                0,
                                                                                CONFIG_MAX_AVRCP_VOLUME,
                                                                                1,
                                                                                CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME,
                                                                                CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME};
PROPERTY_SET(Tua::VolumeLevel, m_current_avrcp_volume)

static StaticTask_t audio_task_buffer;
static StackType_t  audio_task_stack[TASK_AUDIO_STACK_SIZE];
/* The variable used to hold the queue's data structure. */
static StaticQueue_t queue_static;
static const size_t  queue_item_size = sizeof(GenericThread::QueueMessage<AudioMessage>);
static uint8_t       queue_static_buffer[QUEUE_SIZE * queue_item_size];

// board_link_power_supply_is_ac_ok() briefly loses connection in certain cases, so check if it is "not ok" for > than
// 2000 ms before powering off
//
// Testing has shown that if the unit is in a completely off state and the charger is then connected,
// board_link_power_supply_is_ac_ok() consistantly reports false for approx. 1800 ms. It then recovers and reports "ok".
// Without this check, this event causes the unit to repeatedly perform a power-cycle reset into the pseudo off-state.
// This causes the off-state charge voltage to repeatedly oscillate btwn 0V-5V-19V.
static auto pwr_ac_debouncer = Debouncer<bool, 2000>{true, get_systick, board_get_ms_since};

// UX has requested to reduce the incr/decr speed when holding either of the volume btns, adjust accordingly
// static auto volume_debouncer = Debouncer<bool, 200>{false, get_systick, board_get_ms_since};

static void read_io_expander_inputs();
static void disable_amps();

static Tus::Task                                           ot_id                      = Tus::Task::Audio;
static Teufel::GenericThread::GenericThread<AudioMessage> *task_handler               = nullptr;
static button_handler_t                                   *s_button_handler           = nullptr;
static uint32_t                                            s_buttons_state            = 0;
static uint32_t                                            s_connection_poll_ts       = 0;
static bool                                                s_is_aux_jack_connected    = false;
static bool                                                s_max_volume_play_feedback = true;

static struct
{
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
    bool no_i2c_mode = false;
#endif
    bool ignore_power_input_until_release         = false;
    bool ignore_stop_pairing_inputs_until_release = false;
    bool ignore_hold_until_stop_pairing           = false;
    bool bypass_mode                              = false;
    bool plug_connected                           = false;
} s_audio;

static const board_link_usb_pd_controller_callbacks_t usb_callbacks = {
    .plug_connection_change_cb =
        +[](bool connected)
        {
            log_info("Plug connection change: %s", connected ? "connected" : "disconnected");
            Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::NotifyUsbConnectionChange{connected});
            s_audio.plug_connected = connected;
        },
    .power_connection_change_cb =
        +[](bool connected) { log_info("Power connection change: %s", connected ? "connected" : "disconnected"); },
    .pd_port_role_change_cb = +[](bool source) { log_err("PD port role changed: %s", source ? "source" : "sink"); },
};

static Leds::SourcePattern get_connected_source_pattern()
{
    // clang-format off
    if (isProperty(Tub::Status::BluetoothConnected)) return Leds::SourcePattern::BluetoothConnected;
    if (isProperty(Tub::Status::BluetoothDisconnected)) return Leds::SourcePattern::BluetoothDisconnected;
    if (isProperty(Tub::Status::AuxConnected)) return Leds::SourcePattern::AuxConnected;
    if (isProperty(Tub::Status::UsbConnected)) return Leds::SourcePattern::UsbConnected;

    return Leds::SourcePattern::Off; // Should never get returned
    // clang-format on
}

static bool is_pairing()
{
#ifdef INCLUDE_TWS_MODE
    return isPropertyOneOf(Tub::Status::BluetoothPairing, Tub::Status::TwsMasterPairing, Tub::Status::SlavePairing);
#else
    return isPropertyOneOf(Tub::Status::BluetoothPairing, Tub::Status::SlavePairing);
#endif
}

static bool stop_pairing()
{
    return is_pairing() && !s_audio.ignore_stop_pairing_inputs_until_release && !is_key_test_activated();
}

static void load_persistent_parameters()
{
    log_info("Loading persistent parameters");
    auto ledBrightness = Storage::load<Tus::LedBrightness>().value_or(Tus::LedBrightness{CONFIG_BRIGHTNESS_DEFAULT});
    setProperty(ledBrightness);

    auto volumeLevel =
        Storage::load<Tua::VolumeLevel>().value_or(Tua::VolumeLevel{CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME});
    if (volumeLevel.value > 0 && volumeLevel.value < CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME)
    {
        setProperty(volumeLevel);
    }
    else
    {
        setProperty(Tua::VolumeLevel{CONFIG_DEFAULT_ABSOLUTE_AVRCP_VOLUME});
    }
    auto bassLevel = Storage::load<Tua::BassLevel>().value_or(Tua::BassLevel{CONFIG_DSP_BASS_DEFAULT});
    setProperty(bassLevel);
    auto trebleLevel = Storage::load<Tua::TrebleLevel>().value_or(Tua::TrebleLevel{CONFIG_DSP_TREBLE_DEFAULT});
    setProperty(trebleLevel);
    auto ecoMode = Storage::load<Tua::EcoMode>().value_or(Tua::EcoMode{false});
    setProperty(ecoMode);
    auto soundIconsActive = Storage::load<Tua::SoundIconsActive>().value_or(Tua::SoundIconsActive{true});
    setProperty(soundIconsActive);
    auto offTimerEnabled =
        Storage::load<Tus::OffTimerEnabled>().value_or(Tus::OffTimerEnabled{CONFIG_STANDBY_TIMER_MINS_DEFAULT > 0});
    Teufel::Task::System::postMessage(ot_id, offTimerEnabled);
    auto offTimer = Storage::load<Tus::OffTimer>().value_or(Tus::OffTimer{CONFIG_STANDBY_TIMER_MINS_DEFAULT});
    Teufel::Task::System::postMessage(ot_id, offTimer);

    Battery::load_persistent_parameters();
}

TS_KEY_VALUE_CONST_MAP(StatusToSourceOffMapper, Ux::Bluetooth::Status, Leds::SourcePattern,
                       {Tub::Status::BluetoothDisconnected, Leds::SourcePattern::OffFromBtSource},
                       {Tub::Status::BluetoothConnected, Leds::SourcePattern::OffFromBtSource},
                       {Tub::Status::BluetoothPairing, Leds::SourcePattern::OffFromBtSource},
#ifdef INCLUDE_TWS_MODE
                       {Tub::Status::TwsMasterPairing, Leds::SourcePattern::OffFromMstrChain},
                       {Tub::Status::TwsChainMaster, Leds::SourcePattern::OffFromSlvChain},
#endif // INCLUDE_TWS_MODE
                       {Tub::Status::SlavePairing, Leds::SourcePattern::OffFromSlvChain},
                       {Tub::Status::CsbChainMaster, Leds::SourcePattern::OffFromMstrChain},
                       {Tub::Status::ChainSlave, Leds::SourcePattern::OffFromSlvChain},
                       {Tub::Status::AuxConnected, Leds::SourcePattern::OffFromAuxSource},
                       {Tub::Status::UsbConnected, Leds::SourcePattern::OffFromUsbSource},
                       {Tub::Status::DfuMode, Leds::SourcePattern::OffFromBtDfu}, )

TS_KEY_VALUE_CONST_MAP(StatusToSourceMapper, Ux::Bluetooth::Status, Leds::SourcePattern,
                       {Tub::Status::BluetoothDisconnected, Leds::SourcePattern::BluetoothDisconnected},
                       {Tub::Status::BluetoothConnected, Leds::SourcePattern::BluetoothConnected},
                       {Tub::Status::BluetoothPairing, Leds::SourcePattern::BluetoothPairing},
                       {Tub::Status::CsbChainMaster, Leds::SourcePattern::CsbMaster},
                       {Tub::Status::ChainSlave, Leds::SourcePattern::CsbSlave},
#ifdef INCLUDE_TWS_MODE
                       {Tub::Status::TwsMasterPairing, Leds::SourcePattern::TwsMasterPairing},
#endif
                       {Tub::Status::SlavePairing, Leds::SourcePattern::SlavePairing},
                       {Tub::Status::AuxConnected, Leds::SourcePattern::AuxConnected},
                       {Tub::Status::UsbConnected, Leds::SourcePattern::UsbConnected},
                       {Tub::Status::DfuMode, Leds::SourcePattern::BtDfu}, )

// clang-format off

TS_KEY_VALUE_CONST_MAP(EventMapper, input_event_id_t, Ux::InputState,
                       {INPUT_EVENT_ID_SINGLE_PRESS, Ux::InputState::ShortPress},
                       {INPUT_EVENT_ID_SINGLE_PRESS_RELEASE, Ux::InputState::ShortRelease},
                       {INPUT_EVENT_ID_SINGLE_MEDIUM_PRESS, Ux::InputState::MediumPress},
                       {INPUT_EVENT_ID_SINGLE_MEDIUM_PRESS_RELEASE, Ux::InputState::MediumRelease},
                       {INPUT_EVENT_ID_SINGLE_LONG_PRESS, Ux::InputState::LongPress},
                       {INPUT_EVENT_ID_SINGLE_LONG_PRESS_RELEASE, Ux::InputState::LongRelease},
                       {INPUT_EVENT_ID_SINGLE_VERY_LONG_PRESS, Ux::InputState::VeryLongPress},
                       {INPUT_EVENT_ID_SINGLE_VERY_LONG_PRESS_RELEASE, Ux::InputState::VeryLongRelease},
                       {INPUT_EVENT_ID_SINGLE_VERY_VERY_LONG_PRESS, Ux::InputState::VeryVeryLongPress},
                       {INPUT_EVENT_ID_SINGLE_VERY_VERY_LONG_PRESS_RELEASE, Ux::InputState::VeryVeryLongRelease},
                       {INPUT_EVENT_ID_DOUBLE_PRESS, Ux::InputState::DoublePress},
                       {INPUT_EVENT_ID_DOUBLE_PRESS_RELEASE, Ux::InputState::DoubleRelease},
                       {INPUT_EVENT_ID_TRIPLE_PRESS, Ux::InputState::TriplePress},
                       {INPUT_EVENT_ID_TRIPLE_PRESS_RELEASE, Ux::InputState::TripleRelease},
                       {INPUT_EVENT_ID_HOLD, Ux::InputState::Hold},
                       {INPUT_EVENT_ID_PRESS, Ux::InputState::RawPress},
                       {INPUT_EVENT_ID_RELEASE, Ux::InputState::RawRelease}, )

typedef void (*button_event_handler_fn_t)(Ux::InputState event);

TS_KEY_VALUE_CONST_MAP(EventHandlerMapper, uint32_t, button_event_handler_fn_t,
    {BUTTON_ID_POWER, [](Ux::InputState event) {
        log_debug("Power button: %s", getDesc(event));

        switch (event)
            case Teufel::Ux::InputState::ShortPress: {
                if (is_pairing())
                    Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
                break;
            case Teufel::Ux::InputState::DoublePress: {
                auto charge_type = Battery::toggle_fast_charging();
                Leds::indicate_charge_type(charge_type, getProperty<Tus::BatteryLevel>());
                Task::Bluetooth::postMessage(ot_id, Tus::ChargeType {charge_type});
                if (charge_type == Tus::ChargeType::FastCharge)
                    Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK});
                break;
            }
            case Teufel::Ux::InputState::MediumPress:
                if (isProperty(Tus::PowerState::Off)) {
                    s_audio.ignore_power_input_until_release = true;
                    Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::On, Tus::PowerState::Off });
                }
                break;
            case Teufel::Ux::InputState::LongPress:
                // This prevents a power off from happening immediately after a power on without releasing the button first
                if (s_audio.ignore_power_input_until_release) {
                    return;
                }

                Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::Off, Tus::PowerState::On });
                break;
            case Teufel::Ux::InputState::RawPress:
                if (isProperty(Tus::PowerState::On) && !s_audio.ignore_power_input_until_release /* allow factory reset pattern to play */) {
                    Leds::indicate_battery_level(getProperty<Tus::BatteryLevel>());
                }
                break;
            case Teufel::Ux::InputState::RawRelease:
                s_audio.ignore_power_input_until_release = false;
                break;
            default:
                // Nothing to do
                break;
        }
    }},
    {BUTTON_ID_BT, [](Ux::InputState event) {

        if (isPropertyOneOf(Tus::PowerState::Off, Ux::Bluetooth::Status::ChainSlave) ) {
            return;
        }

        log_debug("BT button: %s", getDesc(event));
        switch (event) {
            case Ux::InputState::ShortPress:
                if (stop_pairing()) {
                    Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
                    return;
                }
                if (!is_pairing())
                {
                    Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::BtWakeUp{});
                }
                break;
            case Ux::InputState::LongPress:
                s_audio.ignore_stop_pairing_inputs_until_release = true;
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StartPairing{});
                break;
            case Ux::InputState::VeryVeryLongPress:
                // Sound icon will not successfully execute without StopPairingAndMultichain request
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
                vTaskDelay(pdMS_TO_TICKS(300)); // positive feedback si does not play without this delay after StopPairing request
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::ClearDeviceList {});
                Leds::set_source_pattern(Leds::SourcePattern::PositiveFeedback);
                break;
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
            case Ux::InputState::TriplePress:
                s_audio.no_i2c_mode = true;

                if (!led_test_activated) {
                    Leds::set_solid_color(Leds::Led::Status, Leds::Color::Purple);
                }

                // We are no longer going to poll for the buttons on the IO expander,
                // so we clear their inputs from the state to avoid inputs getting stuck
                // generating hold events when I2C is disabled
                // This line clears the inputs of all buttons except the power button
                s_buttons_state &= BUTTON_ID_POWER;

                log_highlight("Disabling I2C communication");
                bsp_shared_i2c_deinit();
#endif
            case Teufel::Ux::InputState::RawPress:
                if (!is_pairing())
                {
                    set_source_pattern(get_connected_source_pattern());
                }
                break;
            default:
                break;
        }
    }},
    {BUTTON_ID_PLAY, [](Ux::InputState event) {
        log_debug("Play button: %s", getDesc(event));
        switch (event) {
            case Ux::InputState::ShortPress:
                if (stop_pairing()) {
                    Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
                    // Do not change play state in this case
                    return;
                }
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::PlayPause{});
                break;
            case Ux::InputState::DoublePress:
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::NextTrack{});
                break;
            case Ux::InputState::TriplePress:
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::PreviousTrack{});
                break;
            default:
                break;
        }
    }},
    {BUTTON_ID_PLUS, [](Ux::InputState event) {
        log_debug("Plus button: %s", getDesc(event));
        if (event == Ux::InputState::ShortPress || event == Ux::InputState::Hold) {
            if (s_audio.ignore_hold_until_stop_pairing)
                return;
            if (stop_pairing()) {
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
                s_audio.ignore_hold_until_stop_pairing = true;
                // Do not change volume in this case
            }
            else if (getProperty<Tua::VolumeLevel>().value >= CONFIG_MAX_AVRCP_VOLUME) {
                log_info("Max volume reached");
                if (s_max_volume_play_feedback) {
                    Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK,
                                                                                       ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY,
                                                                                       false});
                }
                s_max_volume_play_feedback = false;
            }
            else //if (volume_debouncer(true))
            {
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::VolumeChange::Up);
            }
        } else if (event == Ux::InputState::RawRelease) {
            s_audio.ignore_hold_until_stop_pairing = false;
        }
    }},
    {BUTTON_ID_MINUS, [](Ux::InputState event) {
        log_debug("Minus button: %s", getDesc(event));
        if (event == Ux::InputState::ShortPress || event == Ux::InputState::Hold) {
            if (s_audio.ignore_hold_until_stop_pairing)
                return;
            if (stop_pairing()) {
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
                s_audio.ignore_hold_until_stop_pairing = true;
                // Do not change volume in this case
                return;
            }

            // if (volume_debouncer(true))
            // {
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::VolumeChange::Down);
                s_max_volume_play_feedback = true;
            // }
        } else if (event == Ux::InputState::RawRelease) {
            s_audio.ignore_hold_until_stop_pairing = false;
        }
    }},
    {BUTTON_ID_BT | BUTTON_ID_PLAY, [](Ux::InputState event) {
        log_debug("BT+Play combo: %s", getDesc(event));
#ifdef INCLUDE_TWS_MODE
        if (event == Ux::InputState::LongPress && !isPropertyOneOf(Tub::Status::CsbChainMaster, Tub::Status::TwsChainMaster, Tub::Status::ChainSlave)) {
            s_audio.ignore_stop_pairing_inputs_until_release = true;
            Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::TwsPairing{});
        }
#endif // INCLUDE_TWS_MODE
    }},
    {BUTTON_ID_BT | BUTTON_ID_PLUS, [](Ux::InputState event) {
         log_debug("BT+Plus combo: %s", getDesc(event));
         if (event == Ux::InputState::ShortPress) {
             s_audio.ignore_stop_pairing_inputs_until_release = true;
             if (not isPropertyOneOf(Tub::Status::CsbChainMaster, Tub::Status::ChainSlave))
                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::MultichainPairing{});
         }
     }},
    {BUTTON_ID_BT | BUTTON_ID_MINUS, [](Ux::InputState event) {
         log_debug("BT+Minus combo: %s", getDesc(event));
         if (event == Ux::InputState::ShortPress) {
             Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
         }
     }},
    {BUTTON_ID_POWER | BUTTON_ID_PLUS, [](Ux::InputState event) {
        log_debug("Power+Plus combo: %s", getDesc(event));
        if (event == Ux::InputState::VeryVeryLongPress) {
            log_highlight("Factory reset requested");
            Task::System::postMessage(ot_id, Tus::FactoryResetRequest {});
        }
    }},
    {BUTTON_ID_POWER | BUTTON_ID_MINUS, [](Ux::InputState event) {
        log_debug("Power+Minus combo: %s", getDesc(event));
        if (event == Ux::InputState::VeryVeryLongPress) {
            __HAL_RCC_PWR_CLK_ENABLE();
            HAL_PWR_EnableBkUpAccess();

            // Set the backup register to a magic value that will trigger a bootloader jump
            RTC->BKP0R = 0xCAFEBEEF;

            NVIC_SystemReset();
        }
    }},
    {BUTTON_ID_POWER | BUTTON_ID_BT | BUTTON_ID_MINUS, [](Ux::InputState event) {
         log_debug("Power+BT+Minus combo: %s", getDesc(event));
        if (is_pairing())
            Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{});
            
        if (event == Ux::InputState::VeryVeryLongPress) {
            Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::EnterDfuMode{});
        }
    }},
    {BUTTON_ID_POWER | BUTTON_ID_BT, [](Ux::InputState event) {
        log_debug("Power+BT combo: %s", getDesc(event));

        if (event == Ux::InputState::MediumPress)
        {
            if (isProperty(Ux::System::PowerState::Off))
                Teufel::Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::On, Tus::PowerState::Off });
            log_err("start prod test!!!!");
#ifdef INCLUDE_PRODUCTION_TESTS
            // When test mode activated via POWER+BT, we need to pass UART lines to the USB port.
            board_link_usb_switch_to_uart_debug();
            set_power_with_prompt(true);

            if (isProperty(Ux::System::PowerState::Off))
                Teufel::Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::On, Tus::PowerState::Off });
        }
#else // Do not include FW announcement feature in Production Test Mode FW
        }
        else if (event == Ux::InputState::LongPress)
        {
            Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_FW_ANNOUNCEMENT,
                                                                               ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, 
                                                                               false});
        }
#endif // INCLUDE_PRODUCTION_TESTS
    }},
    {BUTTON_ID_PLAY | BUTTON_ID_PLUS, [](Ux::InputState event) {
         log_debug("Play+Plus combo: %s", getDesc(event));
         if (event == Ux::InputState::ShortPress) {
            postMessage(ot_id, Tua::EcoMode{true});
         }
     }},
    {BUTTON_ID_PLAY | BUTTON_ID_MINUS, [](Ux::InputState event) {
         log_debug("Play+Minus combo: %s", getDesc(event));
         if (event == Ux::InputState::ShortPress) {
            postMessage(ot_id, Tua::EcoMode{false});
         }
     }},
)

// Only these buttons should support repeated press detection
static const uint32_t list_of_buttons_with_repeated_press_support[] = {
    BUTTON_ID_POWER,
    BUTTON_ID_BT,
    BUTTON_ID_PLAY,
};

static const button_handler_config_t button_handler_config = {
    .buttons_num                          = 1,
    .short_press_duration_ms              = 50u,        // Defined as "press" in the UX spec
    .medium_press_duration_ms             = 500u,       // Defined as "short press" in the UX spec
    .long_press_duration_ms               = 1500u,      // Defined as "middle press" in the UX spec
    .very_long_press_duration_ms          = 4000u,      // Defined as "long press" in the UX spec
    .very_very_long_press_duration_ms     = 8000u,      // Defined as "very long press" in the UX spec
    .hold_event_interval_ms               = 100u,
    .repeated_press_threshold_duration_ms = 500u,
    .user_callback =
        +[](uint32_t button_state, input_event_id_t event, uint16_t repeat_count) {
            // Any raw press on any button combination should count as user activity
            if (event == INPUT_EVENT_ID_PRESS) {
                Task::System::postMessage(ot_id, Tus::UserActivity {});
                Leds::user_activity();
            }

            // Skip the first 5 repeats of a hold event, to send repeated press events
            // only after (hold_event_interval_ms x 5) milliseconds.
            if (event == INPUT_EVENT_ID_HOLD && (repeat_count < 5)) {
                return;
            }

            auto mapped_event = Teufel::Core::mapValue(EventMapper, event);
            if (mapped_event.has_value())
            {
                auto handler = Teufel::Core::mapValue(EventHandlerMapper, button_state);
#if defined(INCLUDE_PRODUCTION_TESTS)
                if (is_key_test_activated())
                    handler = get_handler_mapper(button_state);
#endif
                if (handler.has_value())
                {
                    handler.value()(mapped_event.value());
                }
            }
        },
    .get_tick_ms                                 = get_systick,
    .list_of_buttons_with_repeated_press_support = list_of_buttons_with_repeated_press_support,
    .number_of_buttons_with_repeated_press_support =
        sizeof(list_of_buttons_with_repeated_press_support) / sizeof(uint32_t),
    .repeated_press_mode             = BUTTON_HANDLER_REPEATED_PRESS_MODE_DEFERRED,
    .enable_raw_press_release_events = true,
    .enable_multitouch_support       = false,
};

static const GenericThread::Config<AudioMessage> threadConfig = {
    .Name      = "Audio",
    .StackSize = TASK_AUDIO_STACK_SIZE,
    .Priority  = TASK_AUDIO_PRIORITY,
    .IdleMs    = 25,
    .Callback_Idle = []() {
        // Checking if (not isProperty(Tus::PowerState::Off) is unnecessary here. It prevented charging indication from playing while in pseudo off state and
        // the s_source_led_engine has logic in update_infinite_patterns() to ensure it does not run while in a power-off state.
        if (not (is_test_mode_activated() && is_led_test_activated())
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
                && (not s_audio.no_i2c_mode)
#endif
        )
        {
            Leds::tick();
            Leds::run_engines();
        }

        if (board_link_power_supply_button_is_pressed()) {
            s_buttons_state |= BUTTON_ID_POWER;
        } else {
            s_buttons_state &= ~BUTTON_ID_POWER;
        }

        button_handler_process(s_button_handler, s_buttons_state);

        // TODO: Rework/de-duplicate conditions for polling USB PD controller and battery
        //       once we add support for polling them in off mode (with USB power supply connected)

        // Poll the USB PD controller/charger/plug detection every 500 ms
        // Only do it until the speaker is completely powered on, otherwise we will send events
        // before the Bluetooth task is ready to handle them
        if ((board_get_ms_since(s_connection_poll_ts) >= 500) &&
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
            (!s_audio.no_i2c_mode) &&
#endif
            (isProperty(Tus::PowerState::On))) {
            s_connection_poll_ts = get_systick();

            board_link_usb_pd_controller_poll_status(&usb_callbacks);

            if (board_link_plug_detection_is_jack_connected() != s_is_aux_jack_connected) {
                s_is_aux_jack_connected = board_link_plug_detection_is_jack_connected();
                log_info("Audio jack %s", s_is_aux_jack_connected ? "connected" : "disconnected");

                // Mute and unmute amps to prevent pop noise.
                // The delay amount of 200 ms is derived from testing
                board_link_amps_mute(true);
                vTaskDelay(pdMS_TO_TICKS(200));
                board_link_amps_mute(false);

                Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::NotifyAuxConnectionChange {s_is_aux_jack_connected});
            }
        }

#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
        if (not s_audio.no_i2c_mode)
#endif
            Battery::poll();

        // The unit is already off and running only because we're holding the power supply on
        if (isProperty(Tus::PowerState::Off) &&
            not pwr_ac_debouncer(board_link_power_supply_is_ac_ok()) &&
            board_link_power_supply_is_held_on()) 
        {
            log_highlight("Power supply lost");

            board_link_charger_enable_low_power_mode(true);

            // Give the system time to process logs, etc.
            vTaskDelay(pdMS_TO_TICKS(1000));

            // The MCU will lose power shortly after this
            board_link_power_supply_hold_on(false);
        }

        if (board_link_amps_woofer_fault_detected() && isProperty(Tus::PowerState::On))
        {
            board_link_amps_woofer_fault_recover();
        }

        factory_test_key_process();
    },
    .Callback_Init = []() {
        bsp_shared_i2c_init();
        bsp_usb_pd_i2c_init();

        log_info("System init");

        board_link_plug_detection_init();
        board_link_amps_init();
        board_link_boost_converter_init();

        s_button_handler = button_handler_init(&button_handler_config);
        // TODO: Check for nullptr

        board_link_io_expander_init();
        board_link_io_expander_attach_interrupt_handler(+[]() { postMessage(ot_id, IoExpanderInterrupt{}); });
        board_link_io_expander_reset(false);

        // Reset recovery time for IO expander is ~1 us
        vTaskDelay(pdMS_TO_TICKS(2));

        board_link_io_expander_setup_for_normal_operation();

        // If the power supply button is still pressed, wait for the release before processing new inputs
        // if it's not pressed anymore that means that it was already released and we can process inputs
        if (board_link_power_supply_button_is_pressed()) {
            s_audio.ignore_power_input_until_release = true;
            s_buttons_state |= BUTTON_ID_POWER;
        }

        button_handler_process(s_button_handler, s_buttons_state);

        auto brightness = getProperty<Tus::LedBrightness>();
        Leds::set_brightness(brightness.value);

        // PD controller needs some time to load firmware from the EEPROM.
        // It's 1s now, and might be increased in the future.
        vTaskDelay(pdMS_TO_TICKS(1000));

        log_info("Waiting for USB PD ready");
        board_link_usb_pd_controller_init();
        board_link_usb_pd_controller_poll_status(&usb_callbacks);

        // TODO: Figure out how to handle the case where the USB PD controller is never ready
        //       At the very least the user should be able to power cycle the speaker
        while (not board_link_usb_pd_controller_is_ready())
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            board_link_usb_pd_controller_poll_status(&usb_callbacks);
        }
        log_info("USB PD ready");

        // Battery management depends on the successful initialization of the USB PD controller
        Battery::init();
        load_persistent_parameters();

        SyncPrimitive::notify(ot_id);
    },
    .QueueSize = QUEUE_SIZE,
    .Callback  = [](uint8_t /*modid*/, AudioMessage msg) {
        std::visit(
            Teufel::Core::overload{
                [](const Tus::SetPowerState &p) {
                    log_info("Audio power state: %s", getDesc(p.to));
                    switch (p.to) {
                        case Tus::PowerState::PreOff: {

                            log_info("Saving persistent parameters");
                            Storage::save(getProperty<Tus::LedBrightness>());
                            Storage::save(getProperty<Tua::BassLevel>());
                            Storage::save(getProperty<Tua::TrebleLevel>());
                            Storage::save(getProperty<Tua::EcoMode>());
                            Storage::save(getProperty<Tua::SoundIconsActive>());
                            Storage::save(getProperty<Tua::VolumeLevel>());
                            Storage::save(getProperty<Tus::OffTimer>());
                            Storage::save(getProperty<Tus::OffTimerEnabled>());
                            Battery::save_persistent_parameters();

                            // Exit no I2C mode to prepare for shutdown
#ifdef BOARD_CONFIG_HAS_NO_i2C_MODE
                            if (s_audio.no_i2c_mode)
                            {
                                s_audio.no_i2c_mode = false;
                                bsp_shared_i2c_init();
                            }
#endif

                            if (p.reason == Tus::PowerStateChangeReason::BatteryLowLevelAfterBoot) {
                                vTaskDelay(pdMS_TO_TICKS(2000)); // Wait once the power on sound icon is played

                                Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_BATTERY_LOW,
                                                                                                   ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT,
                                                                                                   false});
                                Leds::indicate_battery_level(Tus::BatteryLevel{0});
                            }
                            else
                            {
                                Leds::indicate_power_off(getProperty<Tus::BatteryLevel>());
                            }

                            auto bt_status             = getProperty<Tub::Status>();
                            auto mapped_source_pattern = Teufel::Core::mapValue(StatusToSourceOffMapper, bt_status);
                            if (mapped_source_pattern.has_value())
                            {
                                Leds::set_source_pattern(mapped_source_pattern.value());
                            }
                            else
                            {
                                log_error("No source pattern for status %s", getDesc(bt_status));
                            }
                            break;
                        }

                        case Tus::PowerState::Off: {

                            Battery::set_power_state(p.to);

                            disable_amps();

                            s_audio.bypass_mode = false;
                            break;
                        }

                        case Tus::PowerState::PreOn: {
                            board_link_io_expander_reset(false);

                            // Startup sequence of the amplifiers requires power supplies to be stable
                            // before enabling the amplifiers (bringing PDN pin high)
                            // The boost converter provides PVDD (24V)
                            board_link_boost_converter_enable(true);

                            // Boost controller needs 50 us to turn on its internal regulator plus
                            // 120 us to perform its initial configuration after enabling the device
                            vTaskDelay(pdMS_TO_TICKS(10));

                            // Reset recovery time for IO expander is ~1 us
                            board_link_io_expander_setup_for_normal_operation();

                            // Both amps' datasheets specify that the PDN pin should be high for at least 5 ms
                            // before the I2S clocks start (provided by the BT module, synchronized by the system task)
                            board_link_amps_enable(true);

                            // Likely not necessary considering all the stuff that needs to happen
                            // before the amps are initialized when we get PowerState::On
                            vTaskDelay(pdMS_TO_TICKS(5));

                            if (s_audio.bypass_mode)
                            {
                                log_highlight("Starting in bypass mode");
                                Leds::set_solid_color(Leds::Led::Status, Leds::Color::White);
                            }
                            else
                            {
                                // When the system bootup with the battery level 0% then the unit
                                // turns off immediately with feedback indications which handles in on->off transition.
                                auto bl = getProperty<Tus::BatteryLevel>();
                                if (bl.value > 0)
                                    Leds::indicate_battery_level(getProperty<Tus::BatteryLevel>());
                            }
                            break;
                        }

                        case Tus::PowerState::On: {
                            // The I2S clocks should be stable by now (provided by BT module, synchronized by the system task)
                            // We should now be able to safely start configuring the amplifiers
                            board_link_amps_mode_t amp_mode = s_audio.bypass_mode ? AMP_MODE_BYPASS : AMP_MODE_NORMAL;
                            board_link_amps_setup_woofer(amp_mode);
                            board_link_amps_setup_tweeter(amp_mode);

                            if (isProperty(Tua::EcoMode{true}))
                            {
#ifndef INCLUDE_PRODUCTION_TESTS
                                board_link_amps_enable_eco_mode(true);
#endif
                            }

#ifndef INCLUDE_PRODUCTION_TESTS
                            auto bass = isProperty(Tua::EcoMode{true}) ? 0 : getProperty<Tua::BassLevel>().value;
                            auto treble = isProperty(Tua::EcoMode{true}) ? 0 : getProperty<Tua::TrebleLevel>().value;

                            board_link_amps_set_bass_level(bass);
                            board_link_amps_set_treble_level(treble);
#endif

#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
                            if (s_audio.no_i2c_mode)
                            {
                                log_highlight("Disabling I2C communication");
                                bsp_shared_i2c_deinit();
                            }
#endif

                            s_is_aux_jack_connected = board_link_plug_detection_is_jack_connected();
                            Teufel::Task::Bluetooth::postMessage(ot_id, Tub::NotifyAuxConnectionChange{s_is_aux_jack_connected});

                            Battery::set_power_state(p.to);

                            break;
                        }

                        default:
                            break;
                    }
                    SyncPrimitive::notify(ot_id);
                },
                [](const IoExpanderInterrupt &) {
                    log_debug("IO expander interrupt");
                    read_io_expander_inputs();
                },
                [](const Tus::LedBrightness &p) {
                    setProperty(p);
                    Leds::set_brightness(p.value);
                },
                [](const Tua::UpdateVolume &p)
                {
                    log_info("Updating current avrcp volume");
                    setProperty(Tua::VolumeLevel{ p.value });
                },
                [](const Tub::Status &s) {
                    log_info("Bluetooth status changed to %s", getDesc(s));
                    if (s == Tub::Status::BluetoothDisconnected && s_audio.plug_connected)
                    {
                        log_info("BT disconnected, USB connected, notify BT");
                        Teufel::Task::Bluetooth::postMessage(ot_id, Teufel::Ux::Bluetooth::NotifyUsbConnectionChange{true});
                    }

                    if (
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
                            s_audio.no_i2c_mode ||
#endif
                            is_led_test_activated()) {
                        return;
                    }
                },
                [](const Tus::BatteryLowLevelState &p) {
                    Leds::indicate_low_battery_level(p);
                    log_debug("Battery low level: %s", getDesc(p));
                    Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_BATTERY_LOW});
                },
                [](const Tus::BatteryCriticalTemperature &s) {
                    log_warn("Critical temperature: %s", getDesc(s));
                    if (
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
                        s_audio.no_i2c_mode ||
#endif
                        is_led_test_activated())
                    {
                        return;
                    }

                    Leds::indicate_temperature_warning(s);
                },
                [](const Tus::ChargeType &p)
                {
                    Battery::set_charge_type(p);
                },
                [](const Tua::EcoMode &p)
                {
                    setProperty(p);
#ifndef INCLUDE_PRODUCTION_TESTS
                    board_link_amps_enable_eco_mode(p.value);

                    auto bass = isProperty(Tua::EcoMode{true}) ? 0 : getProperty<Tua::BassLevel>().value;
                    auto treble = isProperty(Tua::EcoMode{true}) ? 0 : getProperty<Tua::TrebleLevel>().value;

                    board_link_amps_set_bass_level(bass);
                    board_link_amps_set_treble_level(treble);
#endif
                    Leds::set_source_pattern(p.value ? Leds::SourcePattern::EcoModeOn : Leds::SourcePattern::EcoModeOff);
                    Teufel::Task::Bluetooth::postMessage(ot_id, p);
                    Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK,
                                                                                   ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY,
                                                                                   false});
                    if (!p.value)
                        Teufel::Task::Bluetooth::postMessage(ot_id, Tua::RequestSoundIcon {ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK,
                                                                                       ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT,
                                                                                       false});
                },
                [](const Tua::SoundIconsActive &p)
                {
                    setProperty(p);
                },
                [](const Tua::BassLevel &p)
                {
                    setProperty(p);
#ifndef INCLUDE_PRODUCTION_TESTS
                    board_link_amps_set_bass_level(p.value);
#endif
                },
                [](const Tua::TrebleLevel &p)
                {
                    setProperty(p);
#ifndef INCLUDE_PRODUCTION_TESTS
                    board_link_amps_set_treble_level(p.value);
#endif
                },
                [](const Tus::FactoryReset &p)
                {
                    // Activate sound icons
                    setProperty(Tua::SoundIconsActive{ true });
                    log_debug("Sound Icons: %s", (getProperty<Tua::SoundIconsActive>().value) ? "active" : "inactive");

                    setProperty(Tua::EcoMode{ false });
                    log_debug("Eco Mode: %s", (getProperty<Tua::EcoMode>().value) ? "active" : "inactive");

                    // Unmute
                    board_link_amps_mute(false);
                    log_debug("Amps: %s", (board_link_amps_is_muted()) ? "muted" : "unmuted");

                    Battery::factory_reset();

                    // Reset brightness
                    Leds::set_brightness(CONFIG_BRIGHTNESS_DEFAULT);
                    log_debug("LED Brightness: %d", getProperty<Tus::LedBrightness>().value);

                    // Play factory reset indication
                    Leds::indicate_factory_reset(p);
                    s_audio.ignore_power_input_until_release = true; // do not allow batt pattern to override
                },
                [](const Tus::HardReset &) {
                    disable_amps();
                    vPortEnterCritical();
                    NVIC_DisableIRQ(SysTick_IRQn);
                    NVIC_SystemReset();
                },
            }, msg);
    },
    .StackBuffer = audio_task_stack,
    .StaticTask = &audio_task_buffer,
    .StaticQueue = &queue_static,
    .QueueBuffer = queue_static_buffer,
};

int start()
{
    static_assert(sizeof(AudioMessage) <= 6, "Queue message size exceeded 6 bytes!");

    task_handler = GenericThread::create(&threadConfig);
    APP_ASSERT(task_handler);

    return 0;
}

int postMessage(Tus::Task source_task, AudioMessage msg)
{
    return GenericThread::PostMsg(task_handler, static_cast<uint8_t>(source_task), msg);
}

static void read_io_expander_inputs() {
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
    if (s_audio.no_i2c_mode) {
        return;
    }
#endif

    if (uint8_t b = 0; board_link_io_expander_get_all_buttons(&b) == 0)
    {
        s_buttons_state = b;
    }
    log_trace("Buttons state: 0x%02X", s_buttons_state);

    // After initiating some kind of pairing, we need to make sure that all the buttons are released
    // before we can consider that the user wants to stop the pairing with a button input
    // This avoids situations like the user pressing BT+Play to start pairing and then releasing BT
    // just a little bit before the play button, which would stop the pairing immediately after starting it
    if (s_buttons_state == 0) {
        s_audio.ignore_stop_pairing_inputs_until_release = false;
    }

    button_handler_process(s_button_handler, s_buttons_state);
}

static void disable_amps()
{
    // Mute the amps and wait for them to mute before power down
    board_link_amps_mute(true);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Disable the amplifiers and wait for them to disable before power down
    // Datasheet specifies to wait at least 6 ms for this (apparently it depends on several things)
    board_link_amps_enable(false);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Bring down power supplies after disabling the amps
    board_link_boost_converter_enable(false);

    // Boost converter datasheet mentions a 40 us delay to shut down
    vTaskDelay(pdMS_TO_TICKS(10));
}

}

// Properties public API
namespace Teufel::Ux::Audio
{
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Audio, m_eco_mode, EcoMode)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Audio, m_current_avrcp_volume, VolumeLevel)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Audio, m_sound_icons_active, SoundIconsActive)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Audio, m_bass_level, BassLevel)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Audio, m_treble_level, TrebleLevel)
}

namespace Teufel::Ux::System
{
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Audio, m_led_brightness, LedBrightness)
}
