#include <algorithm>

#include "board.h"
#define LOG_LEVEL LOG_LEVEL_WARNING
#include "logger.h"
#include "soc_estimator.h"
#include "kvstorage.h"

static inline float integrate_charge(float previous_value, int16_t battery_in_out_current_ma, uint32_t time_delta_ms)
{
    return previous_value +
           static_cast<float>(battery_in_out_current_ma) * .001f * static_cast<float>(time_delta_ms) * .001f;
}

void SocEstimator::add_sample(uint16_t battery_voltage_mv, int16_t battery_in_out_current_ma)
{
#ifndef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
    static uint32_t s_last_sample_timestamp = get_systick() - 10u;

    // Integrate the charge
    m_integrated_charge =
        integrate_charge(m_integrated_charge, battery_in_out_current_ma, board_get_ms_since(s_last_sample_timestamp));
    s_last_sample_timestamp = get_systick();
#endif
    m_battery_voltage_mv = battery_voltage_mv;
}

#ifdef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
/**
 * @brief Primitive battery level estimation based on the battery voltage.
 * It uses a simple linear function to estimate the battery level.
 * Should not be used in production!
 * @param[in] voltage_mv
 * @return uint8_t battery level in percentage
 */
uint8_t SocEstimator::get_battery_level() const
{
    auto voltage_mv = std::clamp(m_battery_voltage_mv, c_battery_voltage_mv_min, c_battery_voltage_mv_max);
    return ((voltage_mv - c_battery_voltage_mv_min) * 100) / (c_battery_voltage_mv_max - c_battery_voltage_mv_min);
}
#else
uint8_t SocEstimator::get_battery_level() const
{
    if (m_algo_state == Teufel::Ux::System::BatterySoCAlgoState::Reset ||
        m_algo_state == Teufel::Ux::System::BatterySoCAlgoState::FirstStartOrBatteryReset)
    {
        auto charge = vbat_to_charge::convert(m_battery_voltage_mv);
        return charge_to_soc::convert(charge, m_battery_factory_capacity);
    }
    else
        return charge_to_soc::convert(m_integrated_charge, m_capacity);
}
#endif

void SocEstimator::stat() const
{
    // Convert m_integrated_charge from ampere-seconds to milli-ampere-hours
    auto charge_mah = m_integrated_charge * 1000.f / 3600.f;
    log_dbg_raw("soc: algo state: %s, CHG: %d (mAh), CAP: %d (As), BAT: %u (mV)\r\n", getDesc(m_algo_state),
                static_cast<int>(charge_mah), static_cast<int>(m_capacity), m_battery_voltage_mv);
}

void SocEstimator::init(uint16_t battery_voltage_mv)
{
#ifndef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
    m_algo_state = Storage::load<Teufel::Ux::System::BatterySoCAlgoState>().value_or(
        Teufel::Ux::System::BatterySoCAlgoState::Reset);

    // First initial algorithm state
    // After the factory reset, or full EEPROM erase
    if (m_algo_state == Teufel::Ux::System::BatterySoCAlgoState::Reset)
    {
        m_capacity          = m_battery_factory_capacity;
        m_integrated_charge = vbat_to_charge::convert(battery_voltage_mv);
        m_algo_state        = Teufel::Ux::System::BatterySoCAlgoState::FirstStartOrBatteryReset;

        save_persistent_parameters();
    }
    else
    {
        m_integrated_charge = Storage::load<Teufel::Ux::System::BatterySocAccumulatedCharge>()
                                  .value_or(Teufel::Ux::System::BatterySocAccumulatedCharge{0})
                                  .value;

        m_capacity = Storage::load<Teufel::Ux::System::BatterySocCapacity>()
                         .value_or(Teufel::Ux::System::BatterySocCapacity{m_battery_factory_capacity})
                         .value;
    }
#else
    Storage::save(Teufel::Ux::System::BatterySoCAlgoState::Reset);
#endif

    log_info("soc: initialized");
    stat();
}

void SocEstimator::reset(uint16_t battery_voltage_mv)
{
#ifndef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
    // TODO: set m_integrated_charge to 0 for the measurements
    m_integrated_charge = vbat_to_charge::convert(battery_voltage_mv);
    // m_integrated_charge = 0.f;
    m_capacity   = m_battery_factory_capacity;
    m_algo_state = Teufel::Ux::System::BatterySoCAlgoState::FirstStartOrBatteryReset;
    save_persistent_parameters();
    log_info("soc: reset...");
    stat();
#endif
}

void SocEstimator::save_persistent_parameters()
{
#ifndef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
    log_info("soc: saving persistent parameters...");
    stat();
    Storage::save(Teufel::Ux::System::BatterySocAccumulatedCharge{m_integrated_charge});
    Storage::save(Teufel::Ux::System::BatterySocCapacity{m_capacity});
    Storage::save(m_algo_state);
#endif
}

void SocEstimator::on_shutdown()
{
    save_persistent_parameters();
}

void SocEstimator::on_charge()
{
#ifndef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
    using namespace Teufel::Ux::System;
    switch (m_algo_state)
    {
        case BatterySoCAlgoState::FirstStartOrBatteryReset:
            m_integrated_charge = m_battery_factory_capacity;
            m_algo_state        = BatterySoCAlgoState::FirstFullChargeCompleted;
            break;

        case BatterySoCAlgoState::FirstFullChargeCompleted:
            m_integrated_charge = m_battery_factory_capacity;
            break;

        case BatterySoCAlgoState::FirstFullDischargeCompleted:
            m_capacity   = m_integrated_charge;
            m_algo_state = BatterySoCAlgoState::NormalOperation;
            break;

        case BatterySoCAlgoState::NormalOperation:
            m_capacity          = (m_capacity + m_integrated_charge) / 2.f;
            m_integrated_charge = m_capacity;
            break;
    }
    save_persistent_parameters();
#endif
}

void SocEstimator::on_discharge()
{
#ifndef BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE
    using namespace Teufel::Ux::System;
    switch (m_algo_state)
    {
        case BatterySoCAlgoState::FirstStartOrBatteryReset:
            m_integrated_charge = 0.f;
            m_algo_state        = BatterySoCAlgoState::FirstFullDischargeCompleted;
            break;

        case BatterySoCAlgoState::FirstFullChargeCompleted:
            m_capacity          = m_battery_factory_capacity - m_integrated_charge;
            m_integrated_charge = 0.f;
            m_algo_state        = BatterySoCAlgoState::NormalOperation;
            break;

        case BatterySoCAlgoState::FirstFullDischargeCompleted:
            m_integrated_charge = 0.f;
            break;

        case BatterySoCAlgoState::NormalOperation:
            m_capacity          = m_capacity - m_integrated_charge / 2.f;
            m_integrated_charge = 0.f;
            break;
    }
    save_persistent_parameters();
#endif
}
