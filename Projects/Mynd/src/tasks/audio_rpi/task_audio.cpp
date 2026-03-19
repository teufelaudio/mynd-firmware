// Due to the flash size limit, use the WARNING or ERROR level to reduce flash usage.
// This will hide debug logs, but info, warning and/or error logs will still be printed.
#define LOG_LEVEL LOG_LEVEL_WARNING

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

#include "stm32f0xx_hal.h"
#include "persistent_storage/kvstorage.h"

#include "task_audio.h"
#include "task_rpi.h"
#include "task_system.h"
#include "task_priorities.h"

#include "logger.h"

#include "external/teufel/libs/GenericThread/GenericThread++.h"
#include "external/teufel/libs/property/property.h"
#include "external/teufel/libs/tshell/tshell.h"
#include "external/teufel/libs/core_utils/mapper.h"
#include "external/teufel/libs/core_utils/overload.h"
#include "external/teufel/libs/core_utils/sync.h"
#include "external/teufel/libs/core_utils/debouncer.h"
#include "external/teufel/libs/app_assert/app_assert.h"

#include "gitversion/version.h"

#define TASK_AUDIO_STACK_SIZE 384
#define QUEUE_SIZE            5

namespace Teufel::Task::Audio
{

namespace Tua = Teufel::Ux::Audio;
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
static void configure_eco_mode(bool enable);
static void setup_amps();
static void disable_amps();

static Tus::Task                                           ot_id                   = Tus::Task::Audio;
static Teufel::GenericThread::GenericThread<AudioMessage> *task_handler            = nullptr;
static button_handler_t                                   *s_button_handler        = nullptr;
static uint32_t                                            s_buttons_state         = 0;
static uint32_t                                            s_connection_poll_ts    = 0;
static bool                                                s_is_aux_jack_connected = false;

static struct
{
#ifdef BOARD_CONFIG_HAS_NO_I2C_MODE
    bool no_i2c_mode = false;
#endif
    bool ignore_power_input_until_release = false;
    bool bypass_mode                      = false;
    bool plug_connected                   = false;
    bool pending_amp_setup                = false;
#ifdef HYBRID_VOLUME_MODE
    // If renderer volume control mode is active, only forward the volume up/down commands to the MPD and/or Bluetooth
    // bluez. A Vol-/Vol+ single press btn combo toggles volume control between the amp and moode software gain
    // controls.
    bool renderer_vol_ctrl_mode_active = true;
#endif
} s_audio;

static const board_link_usb_pd_controller_callbacks_t usb_callbacks = {
    .plug_connection_change_cb =
        +[](bool connected)
        {
            log_info("Plug connection change: %s", connected ? "connected" : "disconnected");
            s_audio.plug_connected = connected;
        },
    .power_connection_change_cb =
        +[](bool connected) { log_info("Power connection change: %s", connected ? "connected" : "disconnected"); },
    .pd_port_role_change_cb = +[](bool source) { log_err("PD port role changed: %s", source ? "source" : "sink"); },
};

static Leds::SourcePattern get_connected_source_pattern()
{
    auto source = getProperty<Teufel::Ux::RpiLink::HostSource>();
    switch (source)
    {
        case Teufel::Ux::RpiLink::HostSource::Mpd:
            return Leds::SourcePattern::RpiMpdStreaming;
        case Teufel::Ux::RpiLink::HostSource::Airplay:
            return Leds::SourcePattern::RpiAirplayStreaming;
        case Teufel::Ux::RpiLink::HostSource::Bluetooth:
            return Leds::SourcePattern::RpiBluetoothStreaming;
        case Teufel::Ux::RpiLink::HostSource::Spotify:
            return Leds::SourcePattern::RpiSpotifyStreaming;
        default:
            return Leds::SourcePattern::RpiPending;
    }
}

static void load_persistent_parameters()
{
    log_debug("Loading persistent parameters (not yet implemented in MYNDberry)");
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

TS_KEY_VALUE_CONST_MAP(HostSourceToSourceOffMapper, Ux::RpiLink::HostSource, Leds::SourcePattern,
                       {Ux::RpiLink::HostSource::Mpd, Leds::SourcePattern::OffFromRpiMpd},
                       {Ux::RpiLink::HostSource::Airplay, Leds::SourcePattern::OffFromRpiAirplay},
                       {Ux::RpiLink::HostSource::Bluetooth, Leds::SourcePattern::OffFromRpiBt},
                       {Ux::RpiLink::HostSource::Spotify, Leds::SourcePattern::OffFromRpiSpotify}, )

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
                break;
            case Teufel::Ux::InputState::DoublePress: {
                auto charge_type = Battery::toggle_fast_charging();
                Leds::indicate_charge_type(charge_type, getProperty<Tus::BatteryLevel>());
                Task::RpiLink::postMessage(ot_id, Tus::ChargeType {charge_type});
                break;
            }
            case Teufel::Ux::InputState::MediumPress:
                if (isProperty(Tus::PowerState::Off)) {
                    s_audio.ignore_power_input_until_release = true;
                    Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::On, Tus::PowerState::Off });
                }
                else if (isProperty(Tus::PowerState::PreOn))
                {
                    Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::Off, Tus::PowerState::Off });
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
        if (isProperty(Tus::PowerState::Off)) {
            return;
        }

        log_debug("BT button: %s", getDesc(event));
        switch (event) {
            case Teufel::Ux::InputState::RawPress:
                set_source_pattern(get_connected_source_pattern());
                break;
            case Ux::InputState::ShortPress:
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::CycleSource{});
                break;
            case Ux::InputState::DoublePress:
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::CycleWifiNetwork{});
                break;
            case Ux::InputState::TriplePress:
#ifdef ENABLE_RPI_LINK_HOTSPOT
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::EnableHotspot{});
#endif // ENABLE_RPI_LINK_HOTSPOT
                break;
            default:
                break;
        }
    }},
    {BUTTON_ID_PLAY, [](Ux::InputState event) {
        log_debug("Play button: %s", getDesc(event));
        switch (event) {
            case Ux::InputState::ShortPress:
                {
                    auto source = getProperty<Teufel::Ux::RpiLink::HostSource>();
                    if (source == Teufel::Ux::RpiLink::HostSource::Mpd
                        || source == Teufel::Ux::RpiLink::HostSource::Bluetooth) {
                        // MPD/Bluetooth: forward play/pause to RPi transport control.
                        Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::PlayPause{});
                    } else if (source != Teufel::Ux::RpiLink::HostSource::Unknown) {
                        // Other renderers (e.g. Spotify/AirPlay): mute amps only.
                        board_link_amps_mute(!board_link_amps_is_muted());
                        log_info("Amp mute toggled: %s", board_link_amps_is_muted() ? "muted" : "unmuted");
                    }
                    // Unknown: no mute, no play/pause (RPi still booting)
                }
                break;
            case Ux::InputState::DoublePress:
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::NextTrack{});
                break;
            case Ux::InputState::TriplePress:
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::PreviousTrack{});
                break;
            default:
                break;
        }
    }},
    {BUTTON_ID_PLUS, [](Ux::InputState event) {
        log_debug("Plus button: %s", getDesc(event));
        if (event == Ux::InputState::ShortPress || event == Ux::InputState::Hold) {
            if (board_link_amps_is_muted()) {
                board_link_amps_mute(false);
                log_debug("Amp unmuted by volume up");
            }
#ifdef HYBRID_VOLUME_MODE
            // If renderer volume control mode is active, only forward the volume up command to the MPD and/or Bluetooth bluez.
            if (s_audio.renderer_vol_ctrl_mode_active) {
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::VolumeChange::Up);
            }
            else if (getProperty<Tua::VolumeLevel>().value >= CONFIG_MAX_AVRCP_VOLUME) {
                log_info("Max volume reached");
                Teufel::Task::Leds::set_source_pattern(Teufel::Task::Leds::SourcePattern::PositiveFeedback);
            }
            else
            {
                uint8_t new_vol = static_cast<uint8_t>(getProperty<Tua::VolumeLevel>().value + CONFIG_HW_VOL_STEP_AVRCP);
                postMessage(ot_id, Tua::UpdateVolume{new_vol});
            }
#else
            Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::VolumeChange::Up);
#endif // HYBRID_VOLUME_MODE
        }
    }},
    {BUTTON_ID_MINUS, [](Ux::InputState event) {
        log_debug("Minus button: %s", getDesc(event));
        if (event == Ux::InputState::ShortPress || event == Ux::InputState::Hold) {
            if (board_link_amps_is_muted()) {
                board_link_amps_mute(false);
                log_debug("Amp unmuted by volume down");
            }
#ifdef HYBRID_VOLUME_MODE
            // If renderer volume control mode is active, only forward the volume down command to the MPD and/or Bluetooth bluez.
            if (s_audio.renderer_vol_ctrl_mode_active) {
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::VolumeChange::Down);
            }
            else if (getProperty<Tua::VolumeLevel>().value == 0) {
                Teufel::Task::Leds::set_source_pattern(Teufel::Task::Leds::SourcePattern::NegativeFeedback);
            }
            else {
                uint8_t new_vol = static_cast<uint8_t>(getProperty<Tua::VolumeLevel>().value - CONFIG_HW_VOL_STEP_AVRCP);
                postMessage(ot_id, Tua::UpdateVolume{new_vol});
            }
#else
                Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::VolumeChange::Down);
#endif // HYBRID_VOLUME_MODE
        }
    }},
    {BUTTON_ID_PLUS | BUTTON_ID_MINUS, [](Ux::InputState event) {
        log_debug("Plus+Minus combo: %s", getDesc(event));
        if (event == Ux::InputState::ShortPress) {
#ifdef HYBRID_VOLUME_MODE
            s_audio.renderer_vol_ctrl_mode_active = !s_audio.renderer_vol_ctrl_mode_active;
            Teufel::Task::Leds::set_source_pattern(s_audio.renderer_vol_ctrl_mode_active
                ? Teufel::Task::Leds::SourcePattern::PositiveFeedback
                : Teufel::Task::Leds::SourcePattern::NegativeFeedback);
#endif // HYBRID_VOLUME_MODE
        }
    }},
    {BUTTON_ID_BT | BUTTON_ID_PLAY, [](Ux::InputState event) {
        log_debug("BT+Play combo: %s", getDesc(event));
        (void) event;
    }},
    {BUTTON_ID_BT | BUTTON_ID_PLUS, [](Ux::InputState event) {
         log_debug("BT+Plus combo: %s", getDesc(event));
         (void) event;
     }},
    {BUTTON_ID_BT | BUTTON_ID_MINUS, [](Ux::InputState event) {
         log_debug("BT+Minus combo: %s", getDesc(event));
         (void) event;
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
        if (event == Ux::InputState::LongPress) {
            // send shutdown message to Rpi because we are likely going to enter drag-drop update mode
            Teufel::Task::RpiLink::postMessage(ot_id, Teufel::Ux::RpiLink::DragAndDropUpdate{});
        }
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
        (void) event;
    }},
    {BUTTON_ID_POWER | BUTTON_ID_BT, [](Ux::InputState event) {
        log_debug("Power+BT combo: %s", getDesc(event));

        if (event == Ux::InputState::MediumPress)
        {
            if (isProperty(Ux::System::PowerState::Off))
                Teufel::Task::System::postMessage(ot_id, Tus::SetPowerState { Tus::PowerState::On, Tus::PowerState::Off });
#ifndef ENABLE_INTERNAL_FTDI
            log_warn("Rerouting UART to Bluetooth UART");
            board_link_usb_switch_to_bluetooth();
#endif
        }
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
        if (true)
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

        // Poll the USB PD controller/charger/plug detection every 500 ms
        // Only do it until the speaker is completely powered on, otherwise we will send events
        // before the Bluetooth task is ready to handle them
        if ((board_get_ms_since(s_connection_poll_ts) >= 500) &&
            (isProperty(Tus::PowerState::On))) {
            s_connection_poll_ts = get_systick();

            board_link_usb_pd_controller_poll_status(&usb_callbacks);

            if (board_link_plug_detection_is_jack_connected() != s_is_aux_jack_connected) {
                s_is_aux_jack_connected = board_link_plug_detection_is_jack_connected();
                log_debug("Audio jack %s", s_is_aux_jack_connected ? "connected" : "disconnected");

                board_link_amps_toggle_mute();
            }
        }

        if (isProperty(Tus::PowerState::On) && s_audio.pending_amp_setup)
        {
            if (board_link_amps_set_hi_z() == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(5));
                if (board_link_amps_fs_ready())
                {
                    log_warn("I2S clocks detected, performing deferred amp setup");
                    s_audio.pending_amp_setup = false;
                    setup_amps();
                }
            }
        }

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

    },
    .Callback_Init = []() {
        bsp_shared_i2c_init();
        bsp_usb_pd_i2c_init();

        log_info("System init");

        board_link_plug_detection_init();
        board_link_amps_init();
        board_link_boost_converter_init();

        s_button_handler = button_handler_init(&button_handler_config);

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

                            log_debug("Saving persistent parameters (not yet implemented in MYNDberry)");
                            Storage::save(getProperty<Tus::LedBrightness>());
                            Storage::save(getProperty<Tua::BassLevel>());
                            Storage::save(getProperty<Tua::TrebleLevel>());
                            Storage::save(getProperty<Tua::EcoMode>());
                            Storage::save(getProperty<Tua::SoundIconsActive>());
                            Storage::save(getProperty<Tua::VolumeLevel>());
                            Storage::save(getProperty<Tus::OffTimer>());
                            Storage::save(getProperty<Tus::OffTimerEnabled>());
                            Battery::save_persistent_parameters();


                            if (p.reason == Tus::PowerStateChangeReason::BatteryLowLevelAfterBoot) {
                                vTaskDelay(pdMS_TO_TICKS(2000)); // Wait once the power on sound icon is played
                                Leds::indicate_battery_level(Tus::BatteryLevel{0});
                            }
                            else
                            {
                                Leds::indicate_power_off(getProperty<Tus::BatteryLevel>());
                            }

                            {
                                auto host_source           = getProperty<Teufel::Ux::RpiLink::HostSource>();
                                auto mapped_source_pattern = Teufel::Core::mapValue(HostSourceToSourceOffMapper, host_source);
                                if (mapped_source_pattern.has_value())
                                {
                                    Leds::set_source_pattern(mapped_source_pattern.value());
                                }
                                else
                                {
                                    Leds::set_source_pattern(Leds::SourcePattern::Off);
                                }
                            }
                            break;
                        }

                        case Tus::PowerState::Off: {

                            Battery::set_power_state(p.to);

                            disable_amps();

                            s_audio.bypass_mode = false;
                            s_audio.pending_amp_setup = false;

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
                            if (board_link_amps_fs_ready())
                            {
                                log_warn("I2S clocks detected, performing amp setup");
                                setup_amps();
                            }
                            else
                            {
                                log_warn("I2S clocks not yet detected, deferring amp setup");
                                s_audio.pending_amp_setup = true;
                            }


                            s_is_aux_jack_connected = board_link_plug_detection_is_jack_connected();

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
                    board_link_amps_set_volume_avrcp(p.value);
                },
                [](const Tus::BatteryLowLevelState &p) {
                    Leds::indicate_low_battery_level(p);
                    log_debug("Battery low level: %s", getDesc(p));
                },
                [](const Tus::BatteryCriticalTemperature &s) {
                    log_warn("Critical temperature: %s", getDesc(s));
                    Leds::indicate_temperature_warning(s);
                },
                [](const Tus::ChargeType &p)
                {
                    Battery::set_charge_type(p);
                },
                [](const Tua::EcoMode &p)
                {
                    setProperty(p);
                    configure_eco_mode(p.value);
                    Leds::set_source_pattern(p.value ? Leds::SourcePattern::EcoModeOn : Leds::SourcePattern::EcoModeOff);
                },
                [](const Tua::SoundIconsActive &p)
                {
                    setProperty(p);
                },
                [](const Tua::BassLevel &p)
                {
                    setProperty(p);
                    board_link_amps_set_bass_level(p.value);
                },
                [](const Tua::TrebleLevel &p)
                {
                    setProperty(p);
                    board_link_amps_set_treble_level(p.value);
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

    if (uint8_t b = 0; board_link_io_expander_get_all_buttons(&b) == 0)
    {
        s_buttons_state = b;
    }
    log_trace("Buttons state: 0x%02X", s_buttons_state);

    button_handler_process(s_button_handler, s_buttons_state);
}

static void configure_eco_mode(bool enable)
{
    board_link_amps_enable_eco_mode(enable);

    auto bass   = enable ? 0 : getProperty<Tua::BassLevel>().value;
    auto treble = enable ? 0 : getProperty<Tua::TrebleLevel>().value;

    board_link_amps_set_bass_level(bass);
    board_link_amps_set_treble_level(treble);
}

static void setup_amps()
{
    board_link_amps_mode_t amp_mode = s_audio.bypass_mode ? AMP_MODE_BYPASS : AMP_MODE_NORMAL;
    board_link_amps_setup_woofer(amp_mode);
    board_link_amps_setup_tweeter(amp_mode);

    configure_eco_mode(isProperty(Tua::EcoMode{true}));
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
