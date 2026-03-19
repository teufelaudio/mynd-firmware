#define LOG_LEVEL LOG_LEVEL_INFO
#include "leds.h"
#include "pattern/generic_rgb/generic_rgb.h"
#include "board_link_io_expander.h"
#include "board_link_moisture_detection.h"
#include "IndicationEngine.h"
#include "generic.h"
#include "config.h"
#include "logger.h"
#include "board.h"

#ifndef BOOTLOADER
#include "src/tshell/tshell.h"
#endif

#include "ux/rpi/rpi.h"

// The +1 is to ensure that the pattern is always at least 1 step long
// If you want a pattern to be 100 ms long with a tick of 100 ms, you need 2 steps
// The first step will be at 0 ms, the second at 100 ms
#define PATTERN_MS_TO_STEPS(x) (((x) / PATTERN_TICK_MS) + 1)
#define PATTERN_TICK_MS        (25)

#define BOARD_CONFIG_LED_RGB

namespace Teufel::Task::Leds
{

enum class PatternId : uint8_t
{
    NoPattern, /* Priority 0 */
    MoistureDetected,
    ChargerActiveBatteryLow,
    ChargerActiveBatteryMid,
    ChargerActiveBatteryFull,
    HostPending,
    HostMpdStreaming,
    HostMpdIdle,
    HostAirplayStreaming,
    HostAirplayIdle,
    HostBtStreaming,
    HostBtIdle,
    HostSpotifyStreaming,
    HostSpotifyIdle,
};

struct RgbColor
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// clang-format off
constexpr auto get_rgb(const Color &c)
{
    switch (c)
    {
        case Color::Off:    return RgbColor {0, 0, 0};
        case Color::Red:    return RgbColor {245, 0, 45};
        case Color::Green:  return RgbColor {40, 200, 95};
        // case Color::Blue:   return RgbColor {90, 90, 250};
        case Color::Blue:   return RgbColor {1, 1, 250};
        case Color::Yellow: return RgbColor {255, 255, 0};
        case Color::Orange: return RgbColor {255, 165, 0};
        case Color::Purple: return RgbColor {160, 32, 240};
        case Color::Cyan:   return RgbColor {0, 255, 128};
        case Color::White:  return RgbColor {255, 255, 255};

        default:            return RgbColor {0, 0, 0};
    }
}

float cubic_curve_up(float _x) { return _x * _x * _x; }
float cubic_curve_down(float _x) { return (1.0f - _x) * (1.0f - _x) * (1.0f - _x); }
float linear_up(float _x) { return _x; }
float linear_down(float _x) { return 1 - _x; }

// Look up tables
static std::array<uint8_t, PATTERN_MS_TO_STEPS(500)>  s_fast_ramp_up_table{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(500)>  s_fast_ramp_down_table{0};
#ifdef BOARD_CONFIG_LED_RGB
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_up_table_r{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_down_table_r{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_up_table_g{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_down_table_g{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_up_table_b{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_down_table_b{0};
#else
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_up_table{0};
static std::array<uint8_t, PATTERN_MS_TO_STEPS(2000)> s_pulse_down_table {0};
#endif

// Pattern segments/snippets
#ifdef BOARD_CONFIG_LED_RGB
static IndicationEngine::PatternFn    s_breathe_up_r(s_pulse_up_table_r);
static IndicationEngine::PatternFn    s_breathe_down_r(s_pulse_down_table_r);
static IndicationEngine::PatternFn    s_breathe_up_g(s_pulse_up_table_g);
static IndicationEngine::PatternFn    s_breathe_down_g(s_pulse_down_table_g);
static IndicationEngine::PatternFn    s_breathe_up_b(s_pulse_up_table_b);
static IndicationEngine::PatternFn    s_breathe_down_b(s_pulse_down_table_b);
#else
static IndicationEngine::PatternFn    s_breathe_up(s_pulse_up_table);
static IndicationEngine::PatternFn    s_breathe_down(s_pulse_down_table);
#endif
static IndicationEngine::PatternFn    s_fast_ramp_up(s_fast_ramp_up_table);
static IndicationEngine::PatternFn    s_fast_ramp_down(s_fast_ramp_down_table);

static IndicationEngine::PatternConst s_one_and_half_seconds_on(255, PATTERN_MS_TO_STEPS(1500));

static IndicationEngine::PatternConst s_one_second_on(255, PATTERN_MS_TO_STEPS(1000));
static IndicationEngine::PatternConst s_one_second_off(0, PATTERN_MS_TO_STEPS(1000));
static IndicationEngine::PatternConst s_one_second_half_on(165, PATTERN_MS_TO_STEPS(1000));

static IndicationEngine::PatternConst s_half_second_on(255, PATTERN_MS_TO_STEPS(500));
static IndicationEngine::PatternConst s_half_second_off(0, PATTERN_MS_TO_STEPS(500));

// Off for half a second
//
//
//
// ─────
static IndicationEngine::Pattern s_off_for_half_second {
    &s_half_second_off,
};

// Fast flashing
//
// ┌────┐    ┌────┐    ┌────┐    ┌────┐    ┌────┐
// │    │    │    │    │    │    │    │    │    │
// ┘    └────┘    └────┘    └────┘    └────┘    └────
static IndicationEngine::Pattern s_fast_flashing {
    &s_half_second_on,
    &s_half_second_off,
};

// Slow flashing to solid
//
// ┌────────┐        ┌─────────┐
// │        │        │         │
// ┘        └────────┘         └───────
static IndicationEngine::Pattern s_two_slow_flashes {
    &s_one_second_on,
    &s_one_second_off,
    &s_one_second_on,
    &s_one_second_off,
};

// Slow flashing
//
// ┌────────┐        ┌─────────┐        ┌────────┐
// │        │        │         │        │        │
// ┘        └────────┘         └────────┘        └────
static IndicationEngine::Pattern s_slow_flashing {
    &s_one_second_on,
    &s_one_second_off,
};

// Fast ramp to on
//
//         .───────
//        /
// ______/
static IndicationEngine::Pattern s_fast_ramp_to_on {
    &s_fast_ramp_up
};

// Fast ramp to off
//
// ────────.
//          \
//           \______
static IndicationEngine::Pattern s_fast_ramp_to_off {
    &s_fast_ramp_down
};

// One second solid
//
// ────────────────────────────────────────────────
//
//
static IndicationEngine::Pattern s_one_second_solid_indication {
    &s_one_second_on
};
static IndicationEngine::Pattern s_one_second_solid_indication_half {
    &s_one_second_half_on
};

// Breathing (sine wave-like)
//
//    .-.     .-.     .-.     .-.     .-.     .-.
//   /   \   /   \   /   \   /   \   /   \   /   \
// -'     '-'     '-'     '-'     '-'     '-'     '
#ifdef BOARD_CONFIG_LED_RGB
static IndicationEngine::Pattern s_breathing_r {
    &s_breathe_up_r,
    &s_breathe_down_r,
};
static IndicationEngine::Pattern s_breathing_g {
    &s_breathe_up_g,
    &s_breathe_down_g,
};
static IndicationEngine::Pattern s_breathing_b {
    &s_breathe_up_b,
    &s_breathe_down_b,
};
#else
static IndicationEngine::Pattern s_breathing {
    &s_breathe_up,
    &s_breathe_down,
};
#endif

// Status Short Flash
//
//       ┌─────┐    
//       │     │            
//...────┘     └────...        
static IndicationEngine::Pattern s_short_flash {
    &s_half_second_on,
    &s_half_second_off,
};

// Pattern definitions
static PatternGenericRgb<2, RGB_LED, PURPLE_LED>                     s_rpi_mpd_idle(s_breathing_r, s_breathing_g, s_breathing_b, static_cast<uint8_t>(PatternId::HostMpdIdle));
static PatternGenericRgb<2, RGB_LED, RED_LED | GREEN_LED | BLUE_LED> s_rpi_airplay_idle(s_breathing_r, s_breathing_g, s_breathing_b, static_cast<uint8_t>(PatternId::HostAirplayIdle));
static PatternGenericRgb<2, RGB_LED, BLUE_LED>                        s_rpi_bt_idle(s_breathing_r, s_breathing_g, s_breathing_b, static_cast<uint8_t>(PatternId::HostBtIdle));
static PatternGenericRgb<2, RGB_LED, GREEN_LED>                       s_rpi_spotify_idle(s_breathing_r, s_breathing_g, s_breathing_b, static_cast<uint8_t>(PatternId::HostSpotifyIdle));

static Color   s_rpi_idle_breathing_color = Color::Purple;
static uint8_t s_raw_brightness           = static_cast<uint8_t>(CONFIG_BRIGHTNESS_DEFAULT * UINT8_MAX / 100);

#ifdef BOARD_CONFIG_LED_RGB
static void fill_rgb_breathing_tables(uint8_t raw_brightness, Color c)
{
    auto [r, g, b]    = get_rgb(c);
    auto r_brightness = r * raw_brightness / UINT8_MAX;
    auto b_brightness = b * raw_brightness / UINT8_MAX;
    IndicationEngine::FillLookUpTable(cubic_curve_up, s_pulse_up_table_r, 0.f, 1.f, r_brightness);
    IndicationEngine::FillLookUpTable(cubic_curve_down, s_pulse_down_table_r, 0.f, 1.f, r_brightness);
    std::copy(s_pulse_up_table_r.begin(), s_pulse_up_table_r.end(), s_pulse_up_table_g.begin());
    std::copy(s_pulse_down_table_r.begin(), s_pulse_down_table_r.end(), s_pulse_down_table_g.begin());
    IndicationEngine::FillLookUpTable(cubic_curve_up, s_pulse_up_table_b, 0.f, 1.f, b_brightness);
    IndicationEngine::FillLookUpTable(cubic_curve_down, s_pulse_down_table_b, 0.f, 1.f, b_brightness);
}
#endif

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// Status /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static PatternGeneric<2, RGB_LED, GREEN_LED>                      s_factory_reset_confirmation(s_short_flash);
static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_off_from_battery_full(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED>            s_off_from_battery_half(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, RED_LED>                        s_off_from_battery_low(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED | BLUE_LED> s_off_before_blink(s_off_for_half_second);

// Set of patterns for battery-full level indication
static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_battery_full(s_one_second_solid_indication);
static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_battery_full_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_battery_full_ramp_down(s_fast_ramp_to_off);

// Set of patterns for battery-half level indication
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED>            s_battery_half(s_one_second_solid_indication);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED>            s_battery_half_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED>            s_battery_half_ramp_down(s_fast_ramp_to_off);

// Set of patterns for battery-low level indication
static PatternGeneric<1, RGB_LED, RED_LED>                        s_battery_low(s_one_second_solid_indication);
static PatternGeneric<1, RGB_LED, RED_LED>                        s_battery_low_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, RED_LED>                        s_battery_low_ramp_down(s_fast_ramp_to_off);

static PatternGeneric<2, RGB_LED, GREEN_LED>                      s_battery_full_blink(s_fast_flashing);
static PatternGeneric<2, RGB_LED, RED_LED | GREEN_LED>            s_battery_half_blink(s_fast_flashing);
static PatternGeneric<2, RGB_LED, RED_LED>                        s_battery_low_blink(s_fast_flashing);

static PatternGeneric<2, RGB_LED, RED_LED>                        s_battery_low_slow_blink(s_slow_flashing);

// Set of patterns for charge/discharge over/under temperature indication
static PatternGeneric<2, RGB_LED, BLUE_LED>                       s_charge_under_temp(s_short_flash);
static PatternGeneric<2, RGB_LED, RED_LED>                        s_charge_over_temp(s_short_flash);
static PatternGeneric<2, RGB_LED, RED_LED>                        s_discharge_over_temp(s_short_flash);
static PatternGeneric<2, RGB_LED, BLUE_LED>                       s_discharge_under_temp(s_short_flash);

static PatternGeneric<2, RGB_LED, BLUE_LED>                       s_moisture_detected(s_short_flash, static_cast<uint8_t>(PatternId::MoistureDetected));

static PatternGeneric<1, RGB_LED, RED_LED>                        s_status_charger_solid_battery_low(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::ChargerActiveBatteryLow));
static PatternGeneric<1, RGB_LED, RED_LED>                        s_status_charger_solid_battery_low_ramp_up(s_fast_ramp_to_on, static_cast<uint8_t>(PatternId::ChargerActiveBatteryLow));
static PatternGeneric<1, RGB_LED, RED_LED>                        s_status_charger_solid_battery_low_ramp_down(s_fast_ramp_to_off, static_cast<uint8_t>(PatternId::ChargerActiveBatteryLow));

static PatternGeneric<1, RGB_LED, YELLOW_LED>                     s_status_charger_solid_battery_mid(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::ChargerActiveBatteryMid));
static PatternGeneric<1, RGB_LED, YELLOW_LED>                     s_status_charger_solid_battery_mid_ramp_up(s_fast_ramp_to_on, static_cast<uint8_t>(PatternId::ChargerActiveBatteryMid));
static PatternGeneric<1, RGB_LED, YELLOW_LED>                     s_status_charger_solid_battery_mid_ramp_down(s_fast_ramp_to_off, static_cast<uint8_t>(PatternId::ChargerActiveBatteryMid));

static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_status_charger_solid_battery_full(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::ChargerActiveBatteryFull));
static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_status_charger_solid_battery_full_ramp_up(s_fast_ramp_to_on, static_cast<uint8_t>(PatternId::ChargerActiveBatteryFull));
static PatternGeneric<1, RGB_LED, GREEN_LED>                      s_status_charger_solid_battery_full_ramp_down(s_fast_ramp_to_off, static_cast<uint8_t>(PatternId::ChargerActiveBatteryFull));

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////// Source //////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static PatternGeneric<1, RGB_LED, BLUE_LED>                       s_off_from_bt_source(s_fast_ramp_to_off);
static PatternGeneric<2, RGB_LED, GREEN_LED>                      s_positive_feedback(s_short_flash);
static PatternGeneric<2, RGB_LED, RED_LED>                        s_negative_feedback(s_short_flash);
static PatternGeneric<2, RGB_LED, GREEN_LED>                      s_eco_mode(s_short_flash);

// RPi Moode host source patterns: short flash (pending), solid (streaming), breathing (idle) per source color
// Colors: MPD=purple, Spotify=green, AirPlay=white, Bluetooth=blue
static PatternGeneric<2, RGB_LED, PURPLE_LED>                        s_rpi_pending(s_short_flash, static_cast<uint8_t>(PatternId::HostPending));
static PatternGeneric<1, RGB_LED, PURPLE_LED>                        s_rpi_pending_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, PURPLE_LED>                        s_rpi_pending_ramp_down(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, PURPLE_LED>                        s_rpi_mpd_streaming(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::HostMpdStreaming));
static PatternGeneric<1, RGB_LED, PURPLE_LED>                        s_rpi_mpd_streaming_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, PURPLE_LED>                        s_rpi_mpd_streaming_ramp_down(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED | BLUE_LED>    s_rpi_airplay_streaming(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::HostAirplayStreaming));
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED | BLUE_LED>    s_rpi_airplay_streaming_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED | BLUE_LED>    s_rpi_airplay_streaming_ramp_down(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, BLUE_LED>                          s_rpi_bt_streaming(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::HostBtStreaming));
static PatternGeneric<1, RGB_LED, BLUE_LED>                          s_rpi_bt_streaming_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, BLUE_LED>                          s_rpi_bt_streaming_ramp_down(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, GREEN_LED>                         s_rpi_spotify_streaming(s_one_second_solid_indication, static_cast<uint8_t>(PatternId::HostSpotifyStreaming));
static PatternGeneric<1, RGB_LED, GREEN_LED>                         s_rpi_spotify_streaming_ramp_up(s_fast_ramp_to_on);
static PatternGeneric<1, RGB_LED, GREEN_LED>                         s_rpi_spotify_streaming_ramp_down(s_fast_ramp_to_off);

static PatternGeneric<1, RGB_LED, PURPLE_LED>                        s_off_from_rpi_mpd(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, RED_LED | GREEN_LED | BLUE_LED>    s_off_from_rpi_airplay(s_fast_ramp_to_off);
static PatternGeneric<1, RGB_LED, GREEN_LED>                         s_off_from_rpi_spotify(s_fast_ramp_to_off);

// clang-format on

// Engines
static auto s_status_led_engine = IndicationEngine::Engine<RGB_LED>();
static auto s_source_led_engine = IndicationEngine::Engine<RGB_LED>();

enum class BatteryIndicationLevel : uint8_t
{
    Full,
    Half,
    Low,
    VeryLow,
};

static auto get_battery_indication_level(const Ux::System::BatteryLevel &p)
{
    if (p.value >= CONFIG_BATTERY_LEVEL_FULL_MIN_THRESHOLD)
        return BatteryIndicationLevel::Full;
    if (p.value >= CONFIG_BATTERY_LEVEL_HALF_MIN_THRESHOLD)
        return BatteryIndicationLevel::Half;

    return BatteryIndicationLevel::Low;
}

static inline bool is_long_pattern(PatternId p)
{
    // TODO: extend it for _any_ pattern
    // clang-format off
    return p == PatternId::ChargerActiveBatteryLow ||
           p == PatternId::ChargerActiveBatteryMid ||
           p == PatternId::ChargerActiveBatteryFull ||
           p == PatternId::MoistureDetected ||
           p == PatternId::HostPending
           || p == PatternId::HostPending
           || p == PatternId::HostMpdStreaming
           || p == PatternId::HostMpdIdle
           || p == PatternId::HostAirplayStreaming
           || p == PatternId::HostAirplayIdle
           || p == PatternId::HostBtStreaming
           || p == PatternId::HostBtIdle
           || p == PatternId::HostSpotifyStreaming
           || p == PatternId::HostSpotifyIdle
           ;
    // clang-format on
}

class DimmingController
{
  public:
    DimmingController() = default;

    void start()
    {
        log_debug("dimming: start");
        m_cur_brightness = getProperty<Ux::System::LedBrightness>().value;
        if (m_cur_brightness <= m_dimmed_brightness)
            return;
    }

    void reset()
    {
        log_debug("dimming: reset");
        m_cur_brightness   = UINT8_MAX;
        m_last_activity_ts = get_systick();
        auto brightness    = getProperty<Ux::System::LedBrightness>().value;
        set_brightness(brightness);
    }

    void tick()
    {
        if (board_get_ms_since(m_last_activity_ts) > 60000u && m_cur_brightness == UINT8_MAX)
        {
            start();
        }

        if (m_cur_brightness != UINT8_MAX && m_cur_brightness > m_dimmed_brightness)
        {
            static int counter = 1;
            if (counter++ % 4 == 0)
            {
                m_cur_brightness -= m_step;
                set_brightness(m_cur_brightness);
            }
        }
    }

  private:
    static constexpr uint8_t m_dimmed_brightness = CONFIG_BRIGHTNESS_DIMMED;
    uint8_t                  m_cur_brightness    = UINT8_MAX;
    static constexpr uint8_t m_step              = 4;
    uint32_t                 m_last_activity_ts  = 0;
};

static DimmingController dimming_controller{};

void user_activity()
{
    dimming_controller.reset();
}

static void update_infinite_patterns()
{
    using namespace IndicationEngine;

    const std::tuple<LedPattern<RGB_LED> &, bool (*)(), void (*)()> infinite_patterns_status[] = {
        {s_moisture_detected, []() { return board_link_moisture_detection_is_detected(); },
         []()
         {
             log_debug("run moisture detected");
             s_status_led_engine.run_inf(s_moisture_detected);
         }},
        {s_status_charger_solid_battery_low,
         []()
         {
             return isProperty(Ux::System::ChargerStatus::Active) &&
                    get_battery_indication_level(getProperty<Ux::System::BatteryLevel>()) ==
                        BatteryIndicationLevel::Low;
         },
         []()
         {
             log_debug("run charger(red)");
             s_status_led_engine.run_inf_with_preload_and_postload(s_status_charger_solid_battery_low,
                                                                   s_status_charger_solid_battery_low_ramp_up,
                                                                   s_status_charger_solid_battery_low_ramp_down);
         }},
        {s_status_charger_solid_battery_mid,
         []()
         {
             return isProperty(Ux::System::ChargerStatus::Active) &&
                    get_battery_indication_level(getProperty<Ux::System::BatteryLevel>()) ==
                        BatteryIndicationLevel::Half;
         },
         []()
         {
             log_debug("run charger(yellow)");
             s_status_led_engine.run_inf_with_preload_and_postload(s_status_charger_solid_battery_mid,
                                                                   s_status_charger_solid_battery_mid_ramp_up,
                                                                   s_status_charger_solid_battery_mid_ramp_down);
         }},
        {s_status_charger_solid_battery_full,
         []()
         {
             return isProperty(Ux::System::ChargerStatus::Active) &&
                    get_battery_indication_level(getProperty<Ux::System::BatteryLevel>()) ==
                        BatteryIndicationLevel::Full;
         },
         []()
         {
             log_debug("run charger(green)");
             s_status_led_engine.run_inf_with_preload_and_postload(s_status_charger_solid_battery_full,
                                                                   s_status_charger_solid_battery_full_ramp_up,
                                                                   s_status_charger_solid_battery_full_ramp_down);
         }},
    };

    const std::tuple<LedPattern<RGB_LED> &, bool (*)(), void (*)()> infinite_patterns_source[] = {
        {s_rpi_pending,
         []()
         {
             return (isProperty(Ux::RpiLink::HostSource::Unknown) || isProperty(Ux::RpiLink::PowerState::Standby)) &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_pending));
         },
         []()
         {
             s_source_led_engine.run_inf_with_preload_and_postload(s_rpi_pending, s_rpi_pending_ramp_up,
                                                                   s_rpi_pending_ramp_down);
         }},
        {s_rpi_mpd_streaming,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Mpd) &&
                    getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_mpd_streaming));
         },
         []()
         {
             s_source_led_engine.run_inf_with_preload_and_postload(s_rpi_mpd_streaming, s_rpi_mpd_streaming_ramp_up,
                                                                   s_rpi_mpd_streaming_ramp_down);
         }},
        {s_rpi_mpd_idle,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Mpd) &&
                    !getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_mpd_idle));
         },
         []()
         {
             s_rpi_idle_breathing_color = Color::Purple;
             fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
             if (s_source_led_engine.is_running())
                 s_source_led_engine.run_inf_with_preload(s_rpi_mpd_idle, s_rpi_mpd_streaming_ramp_down);
             else
                 s_source_led_engine.run_inf(s_rpi_mpd_idle);
         }},
        {s_rpi_airplay_streaming,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Airplay) &&
                    getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_airplay_streaming));
         },
         []()
         {
             s_source_led_engine.run_inf_with_preload_and_postload(
                 s_rpi_airplay_streaming, s_rpi_airplay_streaming_ramp_up, s_rpi_airplay_streaming_ramp_down);
         }},
        {s_rpi_airplay_idle,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Airplay) &&
                    !getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_airplay_idle));
         },
         []()
         {
             s_rpi_idle_breathing_color = Color::White;
             fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
             if (s_source_led_engine.is_running())
                 s_source_led_engine.run_inf_with_preload(s_rpi_airplay_idle, s_rpi_airplay_streaming_ramp_down);
             else
                 s_source_led_engine.run_inf(s_rpi_airplay_idle);
         }},
        {s_rpi_bt_streaming,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Bluetooth) &&
                    getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_bt_streaming));
         },
         []()
         {
             s_source_led_engine.run_inf_with_preload_and_postload(s_rpi_bt_streaming, s_rpi_bt_streaming_ramp_up,
                                                                   s_rpi_bt_streaming_ramp_down);
         }},
        {s_rpi_bt_idle,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Bluetooth) &&
                    !getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_bt_idle));
         },
         []()
         {
             s_rpi_idle_breathing_color = Color::Blue;
             fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
             if (s_source_led_engine.is_running())
                 s_source_led_engine.run_inf_with_preload(s_rpi_bt_idle, s_rpi_bt_streaming_ramp_down);
             else
                 s_source_led_engine.run_inf(s_rpi_bt_idle);
         }},
        {s_rpi_spotify_streaming,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Spotify) &&
                    getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_spotify_streaming));
         },
         []()
         {
             s_source_led_engine.run_inf_with_preload_and_postload(
                 s_rpi_spotify_streaming, s_rpi_spotify_streaming_ramp_up, s_rpi_spotify_streaming_ramp_down);
         }},
        {s_rpi_spotify_idle,
         []()
         {
             return !isProperty(Ux::RpiLink::PowerState::Standby) && isProperty(Ux::RpiLink::HostSource::Spotify) &&
                    !getProperty<Ux::RpiLink::StreamingActive>().value &&
                    (!s_source_led_engine.is_running() || s_source_led_engine.is_running(s_rpi_spotify_idle));
         },
         []()
         {
             s_rpi_idle_breathing_color = Color::Green;
             fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
             if (s_source_led_engine.is_running())
                 s_source_led_engine.run_inf_with_preload(s_rpi_spotify_idle, s_rpi_spotify_streaming_ramp_down);
             else
                 s_source_led_engine.run_inf(s_rpi_spotify_idle);
         }},
    };

    if (auto cur_p = s_status_led_engine.getPatternId(); not(s_status_led_engine.is_running() && cur_p.has_value() &&
                                                             !is_long_pattern(static_cast<PatternId>(cur_p.value()))))
    {
        for (auto [pattern, condition, runner] : infinite_patterns_status)
        {
            if (!s_status_led_engine.is_running(pattern) && condition())
            {
                runner();
            }
            else if (s_status_led_engine.is_running(pattern) && !condition())
            {
                // If the pattern is running it must be terminated!
                // Let the engine finish the pattern gracefully
                s_status_led_engine.finish_gently();
            }
        }
    }

    if (auto cur_p = s_source_led_engine.getPatternId();
        not(s_source_led_engine.is_running() && cur_p.has_value() &&
            !is_long_pattern(static_cast<PatternId>(cur_p.value()))) &&
        // We want to show the pending pattern as soon as possible,
        // so don't wait for the power state to be on
        (isProperty(Teufel::Ux::System::PowerState::PreOn) || isProperty(Teufel::Ux::System::PowerState::On)))

    {
        for (auto [pattern, condition, runner] : infinite_patterns_source)
        {
            if (!s_source_led_engine.is_running(pattern) && condition())
            {
                runner();
            }
            else if (s_source_led_engine.is_running(pattern) && !condition())
            {
                // If the pattern is running it must be terminated!
                // Let the engine finish the pattern gracefully
                s_source_led_engine.finish_gently();
            }
        }
    }
}

void tick()
{
    dimming_controller.tick();

    update_infinite_patterns();
}

void run_engines()
{
    // Do not run the engines if no patterns are running
    // Otherwise the engines will override the values set manually by `set_solid_color()`

    auto status_led = s_status_led_engine.exec();
    board_link_io_expander_set_status_led(status_led[0], status_led[1], status_led[2]);

    if (s_source_led_engine.is_running())
    {
        auto source_led = s_source_led_engine.exec();
        board_link_io_expander_set_source_led(source_led[0], source_led[1], source_led[2]);
    }
}

void set_solid_color(Led led, Color color)
{
    const auto [r, g, b] = get_rgb(color);
    set_solid_rgb(led, r, g, b);
}

void set_solid_rgb(Led led, uint8_t r, uint8_t g, uint8_t b)
{
    log_debug("Set led %d: RGB(%d %d %d)", led, r, g, b);

    switch (led)
    {
        case Led::Status:
            s_status_led_engine.finish();
            board_link_io_expander_set_status_led(r, g, b);
            break;
        case Led::Source:
            s_source_led_engine.finish();
            board_link_io_expander_set_source_led(r, g, b);
            break;
    }
}

void set_source_pattern(SourcePattern pattern)
{
    log_debug("Source pattern: %s", getDesc(pattern));
    switch (pattern)
    {
        case SourcePattern::Off:
            s_source_led_engine.finish_gently();
            break;
        case SourcePattern::PositiveFeedback:
            s_source_led_engine.run_once_with_preload(s_positive_feedback, s_off_before_blink);
            break;
        case SourcePattern::NegativeFeedback:
            s_source_led_engine.run_once_with_preload(s_negative_feedback, s_off_before_blink);
            break;
        case SourcePattern::EcoModeOn:
            s_source_led_engine.run_once_transient(s_eco_mode);
            break;
        case SourcePattern::EcoModeOff:
            s_source_led_engine.run_few(s_eco_mode, 2);
            break;
        case SourcePattern::RpiPending:
            s_source_led_engine.run_inf_with_preload_and_postload(s_rpi_pending, s_rpi_pending_ramp_up,
                                                                  s_rpi_pending_ramp_down);
            break;
        case SourcePattern::RpiMpdStreaming:
            s_source_led_engine.run_inf_with_preload_and_postload(s_rpi_mpd_streaming, s_rpi_mpd_streaming_ramp_up,
                                                                  s_rpi_mpd_streaming_ramp_down);
            break;
        case SourcePattern::RpiMpdIdle:
            s_rpi_idle_breathing_color = Color::Purple;
            fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
            if (s_source_led_engine.is_running())
                s_source_led_engine.run_inf_with_preload(s_rpi_mpd_idle, s_rpi_mpd_streaming_ramp_down);
            else
                s_source_led_engine.run_inf(s_rpi_mpd_idle);
            break;
        case SourcePattern::RpiAirplayStreaming:
            s_source_led_engine.run_inf_with_preload_and_postload(
                s_rpi_airplay_streaming, s_rpi_airplay_streaming_ramp_up, s_rpi_airplay_streaming_ramp_down);
            break;
        case SourcePattern::RpiAirplayIdle:
            s_rpi_idle_breathing_color = Color::White;
            fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
            if (s_source_led_engine.is_running())
                s_source_led_engine.run_inf_with_preload(s_rpi_airplay_idle, s_rpi_airplay_streaming_ramp_down);
            else
                s_source_led_engine.run_inf(s_rpi_airplay_idle);
            break;
        case SourcePattern::RpiBluetoothStreaming:
            s_source_led_engine.run_inf_with_preload_and_postload(s_rpi_bt_streaming, s_rpi_bt_streaming_ramp_up,
                                                                  s_rpi_bt_streaming_ramp_down);
            break;
        case SourcePattern::RpiBluetoothIdle:
            s_rpi_idle_breathing_color = Color::Blue;
            fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
            if (s_source_led_engine.is_running())
                s_source_led_engine.run_inf_with_preload(s_rpi_bt_idle, s_rpi_bt_streaming_ramp_down);
            else
                s_source_led_engine.run_inf(s_rpi_bt_idle);
            break;
        case SourcePattern::RpiSpotifyStreaming:
            s_source_led_engine.run_inf_with_preload_and_postload(
                s_rpi_spotify_streaming, s_rpi_spotify_streaming_ramp_up, s_rpi_spotify_streaming_ramp_down);
            break;
        case SourcePattern::RpiSpotifyIdle:
            s_rpi_idle_breathing_color = Color::Green;
            fill_rgb_breathing_tables(s_raw_brightness, s_rpi_idle_breathing_color);
            if (s_source_led_engine.is_running())
                s_source_led_engine.run_inf_with_preload(s_rpi_spotify_idle, s_rpi_spotify_streaming_ramp_down);
            else
                s_source_led_engine.run_inf(s_rpi_spotify_idle);
            break;
        case SourcePattern::OffFromRpiMpd:
            s_source_led_engine.run_once(s_off_from_rpi_mpd);
            break;
        case SourcePattern::OffFromRpiAirplay:
            s_source_led_engine.run_once(s_off_from_rpi_airplay);
            break;
        case SourcePattern::OffFromRpiBt:
            s_source_led_engine.run_once(s_off_from_bt_source);
            break;
        case SourcePattern::OffFromRpiSpotify:
            s_source_led_engine.run_once(s_off_from_rpi_spotify);
            break;
    }
}

void indicate_battery_level(const Ux::System::BatteryLevel &p)
{
    auto battery_indication_level = get_battery_indication_level(p);

    switch (battery_indication_level)
    {
        case BatteryIndicationLevel::Full:
            if (s_status_led_engine.is_running())
                s_status_led_engine.run_few_with_postload(s_battery_full, s_battery_full_ramp_down, 6);
            else
                s_status_led_engine.run_few_with_preload_and_postload(s_battery_full, s_battery_full_ramp_up,
                                                                      s_battery_full_ramp_down, 6);
            break;
        case BatteryIndicationLevel::Half:
            if (s_status_led_engine.is_running())
                s_status_led_engine.run_few_with_postload(s_battery_half, s_battery_half_ramp_down, 6);
            else
                s_status_led_engine.run_few_with_preload_and_postload(s_battery_half, s_battery_half_ramp_up,
                                                                      s_battery_half_ramp_down, 6);
            break;
        case BatteryIndicationLevel::Low:
            if (s_status_led_engine.is_running())
                s_status_led_engine.run_few_with_postload(s_battery_low, s_battery_low_ramp_down, 6);
            else
                s_status_led_engine.run_few_with_preload_and_postload(s_battery_low, s_battery_low_ramp_up,
                                                                      s_battery_low_ramp_down, 6);
            break;
        case BatteryIndicationLevel::VeryLow:
            // Blink for 6 seconds
            s_status_led_engine.run_few(s_battery_low_blink, 6);
            break;
    }
}

void indicate_low_battery_level(const Ux::System::BatteryLowLevelState &p)
{
    switch (p)
    {
        case Ux::System::BatteryLowLevelState::Below5Percent:
            s_status_led_engine.run_few_with_preload(s_battery_low_blink, s_off_before_blink, 3);
            break;
        case Ux::System::BatteryLowLevelState::Below10Percent:
            s_status_led_engine.run_few_with_preload(s_battery_low_slow_blink, s_off_before_blink, 3);
            break;
        case Ux::System::BatteryLowLevelState::Below1Percent:
            // TODO: add indication for below 1%
            break;
    }
}

void indicate_charge_type(const Ux::System::ChargeType &charge_type, const Ux::System::BatteryLevel &battery_level)
{
    auto battery_indication_level = get_battery_indication_level(battery_level);

    if (charge_type == Ux::System::ChargeType::FastCharge)
    {
        switch (battery_indication_level)
        {
            case BatteryIndicationLevel::Full:
                s_status_led_engine.run_few_with_preload(s_battery_full_blink, s_off_before_blink, 2);
                break;
            case BatteryIndicationLevel::Half:
                s_status_led_engine.run_few_with_preload(s_battery_half_blink, s_off_before_blink, 2);
                break;
            case BatteryIndicationLevel::Low:
                s_status_led_engine.run_few_with_preload(s_battery_low_blink, s_off_before_blink, 2);
                break;
            default:
                break;
        }
    }
    else
    {
        switch (battery_indication_level)
        {
            case BatteryIndicationLevel::Full:
                s_status_led_engine.run_once_with_preload(s_battery_full_blink, s_off_before_blink);
                break;
            case BatteryIndicationLevel::Half:
                s_status_led_engine.run_once_with_preload(s_battery_half_blink, s_off_before_blink);
                break;
            case BatteryIndicationLevel::Low:
                s_status_led_engine.run_once_with_preload(s_battery_low_blink, s_off_before_blink);
                break;
            default:
                break;
        }
    }
}

void indicate_power_off(const Ux::System::BatteryLevel &battery_level)
{
    if (battery_level.value >= CONFIG_BATTERY_LEVEL_FULL_MIN_THRESHOLD)
        s_status_led_engine.run_once(s_off_from_battery_full);
    else if (battery_level.value >= CONFIG_BATTERY_LEVEL_HALF_MIN_THRESHOLD)
        s_status_led_engine.run_once(s_off_from_battery_half);
    else if (battery_level.value >= CONFIG_BATTERY_LEVEL_LOW_MIN_THRESHOLD)
        s_status_led_engine.run_once(s_off_from_battery_low);
}

void indicate_factory_reset(const Ux::System::FactoryReset &p)
{
    s_status_led_engine.run_once(s_factory_reset_confirmation);
}

void indicate_temperature_warning(const Ux::System::BatteryCriticalTemperature &p)
{
    switch (p)
    {
        case Ux::System::BatteryCriticalTemperature::ChargeUnderTemp:
            s_status_led_engine.run_few(s_charge_under_temp, 6);
            break;
        case Ux::System::BatteryCriticalTemperature::ChargeOverTemp:
            s_status_led_engine.run_few(s_charge_over_temp, 6);
            break;
        case Ux::System::BatteryCriticalTemperature::DischargeUnderTemp:
            s_status_led_engine.run_few(s_discharge_under_temp, 6);
            break;
        case Ux::System::BatteryCriticalTemperature::DischargeOverTemp:
            s_status_led_engine.run_few(s_discharge_over_temp, 6);
            break;
        case Ux::System::BatteryCriticalTemperature::TempOk:
        case Ux::System::BatteryCriticalTemperature::NTCLost:
            break;
    }
}

static void set_brightness_const_patterns(uint8_t brightness)
{
    const auto partial_brightness = static_cast<uint8_t>((get_rgb(Color::Cyan).b * 1.0f / UINT8_MAX) * brightness + 1);
    s_one_second_half_on.update(partial_brightness);
    s_one_second_on.update(brightness);
    s_half_second_on.update(brightness);
    s_one_and_half_seconds_on.update(brightness);
}

static void set_brightness_pattern(uint8_t brightness)
{
    IndicationEngine::FillLookUpTable(linear_up, s_fast_ramp_up_table, 0.f, 1.f, brightness);
    IndicationEngine::FillLookUpTable(linear_down, s_fast_ramp_down_table, 0.f, 1.f, brightness);

#ifdef BOARD_CONFIG_LED_RGB
    fill_rgb_breathing_tables(brightness, s_rpi_idle_breathing_color);
#else
    IndicationEngine::FillLookUpTable(cubic_curve_up, s_pulse_up_table, 0.f, 1.f, brightness);
    IndicationEngine::FillLookUpTable(cubic_curve_down, s_pulse_down_table, 0.f, 1.f, brightness);
#endif // BOARD_CONFIG_LED_RGB
}

void set_brightness(uint8_t brightness)
{
    // Here we receive 0..100 value and convert it to 0..255
    log_debug("Set brightness: %d", brightness);
    const auto raw_brightness = static_cast<uint8_t>(brightness * UINT8_MAX / 100);
    s_raw_brightness          = raw_brightness;
    set_brightness_const_patterns(raw_brightness);
    set_brightness_pattern(raw_brightness);
}

#ifndef BOOTLOADER

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// clang-format off
SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
    SHELL_CMD_PARSED_UINT8(b, "brightness", 0, 100, [](uint8_t v) { set_brightness(v); }),
    SHELL_SUBCMD_SET_END /* Array terminated. */
);
// clang-format on

SHELL_CMD_ARG_REGISTER(led, &sub_led, "led", NULL, 2, 0);

#pragma GCC diagnostic pop

#endif
}
