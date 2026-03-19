
// Due to the flash size limit, only the WARNING level is available
// for use in the complete firmware (including the bootloader).
#if defined(BOOTLOADER)
#define LOG_LEVEL LOG_LEVEL_WARNING
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#include "FreeRTOS.h"
#include "semphr.h"
#include "timers.h"
#include "battery.h"

#include "stm32f0xx_ll_adc.h"

#include <array>
#include <utility>
#include <algorithm>

#include "charge_controller.h"
#include "battery_indicator.h"
#include "soc_estimator.h"
#include "history/history.h"
#include "board.h"
#include "board_link_charger.h"
#include "board_link_usb_pd_controller.h"
#include "board_link_eeprom.h"
#include "bsp_adc.h"

#include "logger.h"
#include "external/teufel/libs/property/property.h"
#include "ux/system/system.h"

#include "kvstorage.h"
#include "task_system.h"
#ifndef MYND_RPI_MODIFICATION
#include "task_bluetooth.h"
#else
#include "task_rpi.h"
#endif // NOT MYND_RPI_MODIFICATION
#include "config.h"
#include "external/teufel/libs/app_assert/app_assert.h"
#include "external/teufel/libs/tshell/tshell.h"
#include "external/teufel/libs/core_utils/ewma.h"
#include "external/teufel/libs/core_utils/hysteresis.h"
#include "external/teufel/libs/core_utils/misc.h"

#include "temperature/temperature.h"

// #define BATTERY_DEBUG 1

#define BATTERY_VOLTAGE_POWER_OFF_THRESHOLD_MV (6000u)

#define BATTERY_VOLTAGE_MAX_MV    (8400u)
#define BATTERY_VOLTAGE_MIN_MV    (6000u)
#define BATTERY_NTC_MAX_RAW_VALUE (3800u)

namespace Teufel::Task::Battery
{
namespace Tus = Teufel::Ux::System;

StaticTimer_t        soc_timer_static_buffer;
static TimerHandle_t soc_timer;

static PropertyNonOpt<decltype(Tus::BatteryLevel::value)> m_battery_level{"battery level", 0, 100, 1, 100, 100};
PROPERTY_SET(Tus::BatteryLevel, m_battery_level)

static PropertyNonOpt<Tus::ChargerStatus> m_charger_status{"charger", Tus::ChargerStatus::NotConnected,
                                                           Tus::ChargerStatus::NotConnected};
PROPERTY_ENUM_SET(Tus::ChargerStatus, m_charger_status)

static constexpr auto default_charge_type =
    CONFIG_FAST_CHARGE_DEFAULT ? Tus::ChargeType::FastCharge : Tus::ChargeType::BatteryFriendly;
static PropertyNonOpt<Tus::ChargeType> m_charge_type{"charge type", default_charge_type, default_charge_type};
PROPERTY_ENUM_SET(Tus::ChargeType, m_charge_type)

// The buffer of ADC values needs to be uint16_t because the DMA transfers 16 bits at a time
// because the ADC is configured to 12 bits resolution
static uint16_t __attribute__((aligned(4))) s_adc_buffer[12 * 6];

static float              calculate_battery_current_milliamps(uint32_t vcc_mv, int32_t isns_ref, int32_t isns);
static std::optional<int> get_battery_current();

static SemaphoreHandle_t sys_adc_buffer_mutex = nullptr;
static StaticSemaphore_t sys_adc_buffer_mutex_buffer;

static constexpr uint8_t c_bat_adc_voltage_divider_ratio = (10000 + 5000) / 5000;

#if defined(INCLUDE_FACTORY_TESTS)
static bool s_prev_charge_limit_state = false;
#endif

static struct
{
    bool                            is_charger_initialized    = false;
    Tus::BatteryCriticalTemperature critical_temp_status      = Tus::BatteryCriticalTemperature::TempOk;
    uint32_t                        last_poll_timestamp_ms    = 0;
    uint16_t                        last_battery_voltage_mv   = 0;
    bool                            is_battery_voltage_stable = false;
    int                             last_battery_current      = 0;
    int8_t                          last_battery_temperature  = 25;
    bool                            ntc_lost                  = false;
} s_battery;

#ifdef BATTERY_DEBUG
static uint8_t mocking_battery_level = UINT8_MAX;
#define BATTERY_MOCK_DEFAULT 1
// #define BATTERY_MOCK_FULLY_CHARGED  1
// #define BATTERY_MOCK_DISCHARGED 1
#if defined(BATTERY_MOCK_DEFAULT)
// Battery mock voltage and current (for testing purposes)
static uint16_t mocking_battery_voltage = UINT16_MAX;
static int      mocking_battery_current = INT32_MAX;
#elif defined(BATTERY_MOCK_FULLY_CHARGED)
// Battery fully charged
static uint16_t mocking_battery_voltage = 8200u;
static int      mocking_battery_current = 100;
#else
static uint16_t mocking_battery_voltage = 7000u;
static int      mocking_battery_current = 700;
#endif
static int8_t mocking_battery_temperature = INT8_MAX;
#endif // BATTERY_DEBUG

static EWMA<4, uint32_t> isens_ref_smoother{};
static EWMA<4, uint32_t> isens_smoother{};
static EWMA<4, uint32_t> psys_smoother{};
static EWMA<4, uint32_t> ntc_sens_smoother{};
static EWMA<4, uint32_t> bat_voltage_smoother{};
static EWMA<4, uint32_t> vrefint_smoother{};

static uint32_t adc_conv_time_ms;
static uint32_t battery_current_processing_time_ms;

static SocEstimator soc_estimator{};

static ::Battery::History battery_history{};

void init()
{
    soc_timer = xTimerCreateStatic(
        "SoC", pdMS_TO_TICKS(10), pdTRUE, 0,
        +[](TimerHandle_t /*xTimer*/)
        {
            static uint32_t last_soc_processing;
            auto            battery_current_ma = get_battery_current().value_or(0);
#if defined(BATTERY_DEBUG)
            if (mocking_battery_current != INT32_MAX)
            {
                battery_current_ma = mocking_battery_current;
            }
#endif
            soc_estimator.add_sample(s_battery.last_battery_voltage_mv, battery_current_ma);
            s_battery.last_battery_current = battery_current_ma;

            battery_current_processing_time_ms = get_systick() - last_soc_processing;
            last_soc_processing                = get_systick();
        },
        &soc_timer_static_buffer);

    xTimerStart(soc_timer, 0);

    constexpr auto c_adc_number_of_samples_per_conversion = 12u;
    static auto    adc_number_of_sampled_channels         = 6u;
    static auto    adc_buffer_size = c_adc_number_of_samples_per_conversion * adc_number_of_sampled_channels;

    APP_ASSERT(adc_buffer_size <= sizeof(s_adc_buffer) / sizeof(uint16_t),
               "s_adc_buffer is too small to hold the samples");

    sys_adc_buffer_mutex = xSemaphoreCreateMutexStatic(&sys_adc_buffer_mutex_buffer);

    APP_ASSERT(sys_adc_buffer_mutex != nullptr, "Failed to create mutex");
    xSemaphoreGive(sys_adc_buffer_mutex);

    bsp_bat_voltage_enable_init();
    bsp_bat_voltage_enable(true);

    bsp_adc_init();
    board_link_charger_init();
    if (board_link_charger_setup() == 0)
    {
        s_battery.is_charger_initialized = true;
        auto charge_type                 = getProperty<Tus::ChargeType>();
        set_charge_type(charge_type);
    }

    bsp_adc_start((uint32_t *) s_adc_buffer, adc_buffer_size,
                  +[]()
                  {
                      if (xSemaphoreTakeFromISR(sys_adc_buffer_mutex, NULL) == pdFALSE)
                          return;

                      static uint32_t s_sample_tick_last = 0;
                      auto            tick               = get_systick();
                      // adc_conv_time_ms = tick - s_sample_tick_last;
                      s_sample_tick_last = std::exchange(tick, get_systick());

                      // TODO: drop it, after PP samples are not used anymore
                      for (uint32_t i = 0; i < adc_buffer_size; i += adc_number_of_sampled_channels)
                      {
                          bat_voltage_smoother(s_adc_buffer[i]);
                          isens_ref_smoother(s_adc_buffer[i + 1]);
                          isens_smoother(s_adc_buffer[i + 2]);
                          psys_smoother(s_adc_buffer[i + 3]);
                          ntc_sens_smoother(s_adc_buffer[i + 4]);
                          vrefint_smoother(s_adc_buffer[i + 5]);
                      }

                      xSemaphoreGiveFromISR(sys_adc_buffer_mutex, NULL);
                  });
}

static BatteryIndicator battery_indicator{
    +[]() { Teufel::Task::Audio::postMessage(Tus::Task::Audio, Tus::BatteryLowLevelState::Below5Percent); },
    +[]() { Teufel::Task::Audio::postMessage(Tus::Task::Audio, Tus::BatteryLowLevelState::Below10Percent); }};

void set_power_state(const Tus::PowerState &state)
{
    if (state == Tus::PowerState::On)
    {
        // This call cover the case when the system starts while the phone is plugged. In this case, it keeps
        // drawing power from the phone. We want to switch to source mode to avoid this.
        board_link_usb_pd_controller_swap_to_srouce();

        // The previous call (swap_to_source) will "reset" the charger configuration, therefore
        // we need to re-enable the charger according to the current charger state.
        if (!isProperty(Tus::ChargerStatus::Active))
        {
            board_link_usb_pd_controller_set_max_source_voltage(USB_PD_MAX_SOURCE_VOLTAGE_5V);
            vTaskDelay(300); // Wait for the voltage to settle
            board_link_charger_disable();
        }
    }

    battery_indicator.update_power_state(state, get_systick());
}

static std::optional<int> get_battery_current()
{
    // Update battery current
    if (xSemaphoreTake(sys_adc_buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return {};

    auto vdda = __LL_ADC_CALC_VREFANALOG_VOLTAGE(vrefint_smoother.get(), LL_ADC_RESOLUTION_12B);

    auto raw_isens_ref_tmp = isens_ref_smoother.get();
    auto raw_isens_tmp     = isens_smoother.get();

    xSemaphoreGive(sys_adc_buffer_mutex);
    return static_cast<int>(calculate_battery_current_milliamps(vdda, raw_isens_ref_tmp, raw_isens_tmp));
}

static void monitor_battery_level()
{
    static std::array<uint16_t, 10> battery_voltage_buffer{};
    static uint8_t                  battery_voltage_buffer_index = 0;

    if (board_get_ms_since(s_battery.last_poll_timestamp_ms) < 200)
        return;

    s_battery.last_poll_timestamp_ms = get_systick();

    auto vdda = __LL_ADC_CALC_VREFANALOG_VOLTAGE(vrefint_smoother.get(), LL_ADC_RESOLUTION_12B);

    // Use the internal ADC for battery voltage measurement
    uint16_t battery_voltage_mv = (bat_voltage_smoother.get() * vdda * c_bat_adc_voltage_divider_ratio) >> 12;

    battery_voltage_buffer[battery_voltage_buffer_index] = battery_voltage_mv;
    battery_voltage_buffer_index = (battery_voltage_buffer_index + 1) % battery_voltage_buffer.size();
    if (battery_voltage_buffer_index == 0 && !s_battery.is_battery_voltage_stable)
    {
        s_battery.is_battery_voltage_stable = true;
    }

    battery_voltage_mv = *std::max_element(std::begin(battery_voltage_buffer), std::end(battery_voltage_buffer));

    if (not s_battery.is_battery_voltage_stable)
        return;

    if (not s_battery.ntc_lost)
        s_battery.last_battery_voltage_mv = battery_voltage_mv;

        // Fake battery voltage for testing low battery indication
        // auto t = get_systick() % 2;
        // battery_voltage_mv = 6200u + t;

#ifdef BATTERY_DEBUG
    if (mocking_battery_voltage != UINT16_MAX)
    {
        battery_voltage_mv                = mocking_battery_voltage;
        s_battery.last_battery_voltage_mv = mocking_battery_voltage;
    }
#endif

    // log_info_raw("Battery: %d mV(%u%%), %d mA (ADC samp: %lu ms)\r\n", battery_voltage_mv,
    // soc_estimator.get_battery_level(), s_battery.last_battery_current, battery_current_processing_time_ms);

    if (not s_battery.ntc_lost)
    {
        if (auto bl = soc_estimator.get_battery_level(); not isProperty(Tus::BatteryLevel{bl}))
        {
            setProperty(Tus::BatteryLevel{bl});
            battery_indicator.update_battery_level(bl, get_systick());
#ifndef MYND_RPI_MODIFICATION
            Teufel::Task::Bluetooth::postMessage(Tus::Task::Audio, Tus::BatteryLevel{bl});
#else
            if (isProperty(Teufel::Ux::RpiLink::PowerState::On))
                Teufel::Task::RpiLink::postMessage(Tus::Task::RpiLink, Tus::BatteryLevel{bl});
#endif // NOT MYND_RPI_MODIFICATION
        }

        if (isProperty(Ux::System::PowerState::Off))
        {
            static uint32_t last_save_persistent_ts = 0;
            if (board_get_ms_since(last_save_persistent_ts) > 60000)
            {
                last_save_persistent_ts = get_systick();
                soc_estimator.save_persistent_parameters();
            }
        }

        if (s_battery.last_battery_voltage_mv <= 6200)
        {
            log_warning("Bat v < 6200mV, power off");

            soc_estimator.on_discharge();

            // We need to check the battery level once after bootup, and power off the system
            // if the battery level is 0% with additional indication. Therefore, we need to distinguish
            // the power off reason.
            const auto power_state_change_reason = (get_systick() < 15000)
                                                       ? Tus::PowerStateChangeReason::BatteryLowLevelAfterBoot
                                                       : Tus::PowerStateChangeReason::BatteryLowLevel;
            Task::System::postMessage(
                Tus::Task::Audio, Tus::SetPowerState{.to = Tus::PowerState::Off, .reason = power_state_change_reason});
        }
    }
}

class ChargerLLController : public IChargerLLController
{
  public:
    void enable(bool bfc_enabled) override
    {
        /* To comply with regulations, the MCU must adjust the charge voltage to 5V
         * once the battery reaches full charge or disabled. When the charging is active
         * we can request 20V. */
        board_link_usb_pd_controller_set_max_source_voltage(USB_PD_MAX_SOURCE_VOLTAGE_20V);

        /* Select fast_charge/battery_friendly mode **after** PD controller applies the source voltage.
         * Otherwise the current value in the charger can be overwritten. */
        vTaskDelay(300);
        bool fast_charge = !bfc_enabled;
        board_link_charger_enable_fast_charge(fast_charge);
    }
    void disable() override
    {
        /* To comply with regulations, the MCU must adjust the charge voltage to 5V
         * once the battery reaches full charge or disabled. When the charging is active
         * we can request 20V. */
        board_link_usb_pd_controller_set_max_source_voltage(USB_PD_MAX_SOURCE_VOLTAGE_5V);
        vTaskDelay(300); // Wait for the voltage to settle
        board_link_charger_disable();
    }

    void on_full_charge() override
    {
        soc_estimator.on_charge();
    }
};

static void monitor_charger_status()
{
    static ChargerLLController charger_ll_controller{};
    static auto                charge_controller = ChargeController{charger_ll_controller};
    bool                       ac_plugged        = false;

    if (board_link_charger_get_ac_plugged_status(&ac_plugged) != 0)
    {
        log_err("Failed to get AC status");
        return;
    }

    const auto is_charging_allowed = !is_battery_temperature_in_critical_for_charge(s_battery.last_battery_temperature);

    auto charger_state =
        charge_controller.process(s_battery.last_battery_voltage_mv, s_battery.last_battery_current,
                                  is_charging_allowed, ac_plugged, isProperty(Tus::ChargeType::BatteryFriendly));

    if (not isProperty(charger_state))
    {
        setProperty(charger_state);
        battery_indicator.update_charger_status(charger_state);
#ifndef MYND_RPI_MODIFICATION
        Teufel::Task::Bluetooth::postMessage(Tus::Task::Bluetooth, Tus::ChargerStatus{charger_state});
#else
        if (isProperty(Teufel::Ux::RpiLink::PowerState::On))
            Teufel::Task::RpiLink::postMessage(Tus::Task::RpiLink, Tus::ChargerStatus{charger_state});
#endif // NOT MYND_RPI_MODIFICATION
       // log_info("charger status: %s", getDesc(charger_state));
    }
}

static void update_battery_temperature()
{
    static uint32_t last_battery_temperature_calculation_ts = 0u;
    if (board_get_ms_since(last_battery_temperature_calculation_ts) > 2000)
    {
        last_battery_temperature_calculation_ts = get_systick();

#ifdef BATTERY_DEBUG
        if (mocking_battery_temperature != INT8_MAX)
        {
            printf("Mocking battery temperature: %d\r\n", mocking_battery_temperature);
            s_battery.last_battery_temperature = mocking_battery_temperature;
            return;
        }
#endif

        if (xSemaphoreTake(sys_adc_buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
            return;
        auto raw_ntc_tmp = ntc_sens_smoother.get();
        auto vdda        = __LL_ADC_CALC_VREFANALOG_VOLTAGE(vrefint_smoother.get(), LL_ADC_RESOLUTION_12B);
        xSemaphoreGive(sys_adc_buffer_mutex);

        s_battery.ntc_lost = raw_ntc_tmp > BATTERY_NTC_MAX_RAW_VALUE;
        if (s_battery.ntc_lost)
            return;

        auto ntc_voltage     = (static_cast<float>(vdda) / 1000.f) * static_cast<float>(raw_ntc_tmp) / 4096.f;
        auto ntc_temperature = ntc_voltage_to_temperature(ntc_voltage);
        if (ntc_temperature != s_battery.last_battery_temperature)
        {
            log_dbg("NTC: %d deg.", ntc_temperature);
            s_battery.last_battery_temperature = ntc_temperature;
        }
    }
}

#ifndef MYND_RPI_MODIFICATION
static void check_charger_status_to_play_sound_icon()
{
    static bool     is_charger_connected          = false;
    static uint32_t last_charger_status_update_ts = 0u;
    if (board_get_ms_since(last_charger_status_update_ts) > 1000)
    {
        auto charger_connected_status = CHARGER_STATUS_UNDEFINED;
        auto ec                       = board_link_charger_get_status(&charger_connected_status);
        if (ec == 0)
        {
            if (charger_connected_status == CHARGER_STATUS_CONNECTED && !is_charger_connected)
            {
                Teufel::Task::Bluetooth::postMessage(
                    Teufel::Ux::System::Task::Audio,
                    Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_CHARGING,
                                                        ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT,
                                                        false});
            }
        }
        is_charger_connected          = (ec == 0) ? (charger_connected_status == CHARGER_STATUS_CONNECTED) : false;
        last_charger_status_update_ts = get_systick();
    }
}
#endif // NOT MYND_RPI_MODIFICATION

static auto power_bank_in_low_battery_mode =
    hysteresis<CONFIG_POWER_BANK_LOW_BATTERY_THRESHOLD, CONFIG_POWER_BANK_LOW_BATTERY_RECOVERY_THRESHOLD>(
        "PowerBankLowBatteryProtector");

static void monitor_power_bank_mode()
{
    static bool     is_power_bank                 = false;
    static uint32_t last_charger_status_update_ts = 0u;
    if (board_get_ms_since(last_charger_status_update_ts) > 2000)
    {
        last_charger_status_update_ts = get_systick();

        auto is_power_bank_allowed = !power_bank_in_low_battery_mode(m_battery_level.get().value());
        if (is_power_bank_allowed != is_power_bank)
        {
            is_power_bank = is_power_bank_allowed;
            log_debug("Power bank mode: %s", is_power_bank ? "on" : "off");
            auto current = is_power_bank ? USB_PD_MAX_SOURCE_CURRENT_1A : USB_PD_MAX_SOURCE_CURRENT_0A;
            board_link_usb_pd_controller_set_max_source_current(current);
        }
    }
}

void poll()
{
    // Calculate NTC temperature
    update_battery_temperature();

    auto battery_critical_temp_status = battery_temperature_state(s_battery.last_battery_temperature);
    if (s_battery.ntc_lost)
        battery_critical_temp_status = Tus::BatteryCriticalTemperature::NTCLost;
    if (battery_critical_temp_status != s_battery.critical_temp_status)
    {
        s_battery.critical_temp_status = battery_critical_temp_status;
        Teufel::Task::Audio::postMessage(Tus::Task::Audio,
                                         Tus::BatteryCriticalTemperature{battery_critical_temp_status});
    }

    auto is_discharging_allowed = !is_battery_temperature_in_critical_for_discharge(s_battery.last_battery_temperature);

    if (not is_discharging_allowed)
    {
        if (isProperty(Tus::PowerState::On) && !s_battery.ntc_lost)
        {
            log_warning("Temp protection: power off the system");
            Task::System::postMessage(Tus::Task::Audio, Tus::SetPowerState{Tus::PowerState::Off, Tus::PowerState::On});
        }
        return;
    }

    // Retry setup if it failed before
    if (not s_battery.is_charger_initialized)
    {
        s_battery.is_charger_initialized = board_link_charger_setup();

        // If it still fails, don't do anything and try again on next poll
        if (not s_battery.is_charger_initialized)
        {
            return;
        }

        auto charge_type = getProperty<Tus::ChargeType>();
        set_charge_type(charge_type);
    }

#ifndef MYND_RPI_MODIFICATION
    check_charger_status_to_play_sound_icon();
#endif // NOT MYND_RPI_MODIFICATION

    if (s_battery.is_battery_voltage_stable)
    {
        Teufel::Core::callOnce([]() { soc_estimator.init(s_battery.last_battery_voltage_mv); });
    }

#if defined(INCLUDE_FACTORY_TESTS)
    bool update_charge_limit_mode = getProperty<Tus::ChargeLimitMode>().value != s_prev_charge_limit_state;
    if (update_charge_limit_mode && getProperty<Tus::BatteryLevel>().value > 70 &&
        isProperty(Tus::ChargerStatus::Active))
    {
        board_link_charger_disable_charging(getProperty<Tus::ChargeLimitMode>().value);
        s_prev_charge_limit_state = getProperty<Tus::ChargeLimitMode>().value;
    }
#endif // INCLUDE_FACTORY_TESTS

    static int cnt = 1;
    if (cnt++ % 40 == 0)
        monitor_charger_status();

    monitor_battery_level();

    monitor_power_bank_mode();

#if 0
    // Update system power
    if (xSemaphoreTake(sys_adc_buffer_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return;
    auto raw_psys_tmp = psys_smoother.get();
    xSemaphoreGive(sys_adc_buffer_mutex);
    log_err("raw psys: %d", raw_psys_tmp);

    auto psys_voltage = 3.3f * static_cast<float>(raw_psys_tmp) / 4096.f;
    auto system_power = static_cast<uint16_t>(psys_voltage * 32.25806f);
#endif

#ifdef BOARD_CONFIG_BATTERY_HISTORY_ENABLED
    // Battery history record
    auto            is_charging_active          = isProperty(Tus::ChargerStatus::Active);
    auto            is_fast_charge_enabled      = isProperty(Tus::ChargeType::FastCharge);
    static uint32_t last_battery_history_record = 0u;
    if (board_get_ms_since(last_battery_history_record) > 500u)
    {
        last_battery_history_record = get_systick();
        auto r =
            ::Battery::History::Record{.system_power            = 42,
                                       .power_in_out_of_battery = static_cast<int16_t>(s_battery.last_battery_current),
                                       .battery_charge          = soc_estimator.get_battery_level(),
                                       .status                  = 0x77};
        battery_history.add_record(r);
    }
#endif
}

Ux::System::ChargeType toggle_fast_charging()
{
    auto charge_type = isProperty(Tus::ChargeType::FastCharge) ? Ux::System::ChargeType::BatteryFriendly
                                                               : Ux::System::ChargeType::FastCharge;

    set_charge_type(charge_type);
    return charge_type;
}

void set_charge_type(const Ux::System::ChargeType &type)
{
    setProperty(type);
    if (isProperty(Tus::ChargerStatus::Active))
    {
        board_link_charger_enable_fast_charge(type == Ux::System::ChargeType::FastCharge);
    }
    log_info("Fast charge %s", type == Ux::System::ChargeType::FastCharge ? "on" : "off");
}

#if defined(INCLUDE_FACTORY_TESTS)
int8_t get_battery_temperature()
{
    return s_battery.last_battery_temperature;
}

uint16_t get_battery_voltage_mv()
{
    return s_battery.last_battery_voltage_mv;
}
#endif // INCLUDE_FACTORY_TESTS

static float calculate_battery_current_milliamps(uint32_t vcc_mv, int32_t isns_ref, int32_t isns)
{
    constexpr float r_sns = 0.005f; // Ohms
    constexpr float gain  = 20.f;   // Gain of the INA

    int32_t         diff_adc                 = isns_ref - isns; // RAW ADC values
    constexpr float adc_mamp_lsb_resolution1 = (gain * r_sns * 4095.f);

    // There is current mismatch between the INA and the ADC, and it's about 15mA,
    // so we need to compensate it.
    return 15.f + static_cast<float>(vcc_mv) * static_cast<float>(diff_adc) / adc_mamp_lsb_resolution1;
    /*
     * This is simplified and slightly optimized version of the follwoing code:
     *
     * // adc_v_lsb calculated as follows:
     * // VCC / ((2 ^ ADC_bits) - 1),
     * // where VCC = 3.3V and ADC_bits = 12
     * float adc_v_lsb = (static_cast<float>(vcc_mv) * 0.001f) / 4095.f;
     *
     * // adc_amp_lsb_resolution calculated as follows:
     * // (adc_v_lsb / gain ) / r_sns,
     * // where gain = 20 and r_sns = 0.005
     * float adc_amp_lsb_resolution = adc_v_lsb / (gain * r_sns); // ampere/lsb
     *
     * auto check_vsns = (static_cast<float>(diff_adc) * adc_v_lsb) / gain;
     * auto check_iload = check_vsns / r_sns;
     * return check_iload * 1000.f; // convert to milliamps
     */
}

void load_persistent_parameters()
{
    auto charge_type = Storage::load<Tus::ChargeType>().value_or(Tus::ChargeType::BatteryFriendly);
    set_charge_type(charge_type);
}

void save_persistent_parameters()
{
    soc_estimator.save_persistent_parameters();
    Storage::save(getProperty<Tus::ChargeType>());
}

void factory_reset()
{
    constexpr auto default_charge_type =
        CONFIG_FAST_CHARGE_DEFAULT ? Tus::ChargeType::FastCharge : Tus::ChargeType::BatteryFriendly;
    set_charge_type(default_charge_type);
    soc_estimator.reset(s_battery.last_battery_voltage_mv);
}

#ifdef BATTERY_DEBUG
SHELL_STATIC_SUBCMD_SET_CREATE(sub_b_level,
                               SHELL_CMD_PARSED_UINT8(set, "set level", 0, UINT8_MAX,
                                                      [](uint16_t v) { mocking_battery_level = v; }),
                               SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_b_volt,
                               SHELL_CMD_PARSED_UINT32(set, "set volt", 0, 10000,
                                                       [](uint16_t v) { mocking_battery_voltage = v; }),
                               SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_b_current,
                               SHELL_CMD_PARSED_INT32(set, "set current", -10000, 10000,
                                                      [](int32_t v) { mocking_battery_current = v; }),
                               SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_b_temp,
                               SHELL_CMD_PARSED_INT8(set, "set temp", -100, 100,
                                                     [](int8_t v) { mocking_battery_temperature = v; }),
                               SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_b, SHELL_CMD_ARG(l, &sub_b_level, "battery level", NULL, 2, 1),
                               SHELL_CMD_ARG(v, &sub_b_volt, "battery voltage", NULL, 2, 1),
                               SHELL_CMD_ARG(c, &sub_b_current, "battery current", NULL, 2, 1),
                               SHELL_CMD_ARG(t, &sub_b_temp, "battery temperature", NULL, 2, 1),
                               SHELL_CMD_NO_ARGS(
                                   full, "battery full",
                                   +[]()
                                   {
                                       mocking_battery_current = 100;
                                       mocking_battery_voltage = 8200u;
                                   }),
                               SHELL_CMD_NO_ARGS(
                                   reset, "battery mock reset",
                                   +[]()
                                   {
                                       mocking_battery_current = INT32_MAX;
                                       mocking_battery_voltage = UINT16_MAX;
                                   }),

                               SHELL_CMD_PARSED_UINT8(source, "source current", 0, 3,
                                                      [](uint8_t value)
                                                      {
                                                          switch (value)
                                                          {
                                                              case 0:
                                                                  board_link_usb_pd_controller_set_max_source_current(
                                                                      USB_PD_MAX_SOURCE_CURRENT_0A);
                                                                  break;
                                                              case 1:
                                                                  board_link_usb_pd_controller_set_max_source_current(
                                                                      USB_PD_MAX_SOURCE_CURRENT_1A);
                                                                  break;
                                                          }

                                                          board_link_usb_pd_controller_select_mode(TYPEC_DISABLED_MODE);
                                                          board_link_usb_pd_controller_select_mode(TYPEC_DRP_MODE);
                                                      }),

                               SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_ARG_REGISTER(b, &sub_b, "battery", NULL, 2, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_soc,
                               SHELL_CMD_NO_ARGS(
                                   reset, "reset soc",
                                   +[]() { soc_estimator.reset(s_battery.last_battery_voltage_mv); }),
                               SHELL_CMD_NO_ARGS(
                                   show, "show", +[]() { soc_estimator.stat(); }),
                               SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_ARG_REGISTER(soc, &sub_soc, "soc", NULL, 2, 0);
#endif
}

namespace Teufel::Ux::System
{
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Battery, m_battery_level, BatteryLevel)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Battery, m_charger_status, ChargerStatus)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::Battery, m_charge_type, ChargeType)
}
