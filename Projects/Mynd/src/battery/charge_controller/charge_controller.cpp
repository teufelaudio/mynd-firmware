
// Due to the flash size limit, only the WARNING level is available
// for use in the complete firmware (including the bootloader).
#if defined(BOOTLOADER)
#define LOG_LEVEL LOG_LEVEL_WARNING
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
#include "logger.h"
#include "charge_controller.h"

#include "ux/system/system.h"

Teufel::Ux::System::ChargerStatus ChargeController::process(uint16_t battery_voltage_mv, int16_t battery_current_ma,
                                                            bool charger_ntc_allowed, bool ac_plugged, bool bfc_enabled)
{
    using enum Teufel::Ux::System::ChargerStatus;

    ChargerStatus new_charge_status = m_charger_status;

    switch (m_charger_status)
    {
        case NotConnected:
            if (ac_plugged && charger_ntc_allowed)
            {
                new_charge_status = Inactive;
                m_reset_counters();
            }
            break;
        case Inactive:
            if (not ac_plugged)
            {
                new_charge_status = NotConnected;
                break;
            }

            if (not charger_ntc_allowed)
            {
                break;
            }

            if (bfc_enabled && m_battery_below_7900mv_counter(battery_voltage_mv))
            {
                log_debug("Charger controller: Battery voltage below 7900mV for 5s");
                new_charge_status = Active;
            }

            if (not bfc_enabled && m_battery_below_8100mv_counter(battery_voltage_mv))
            {
                log_debug("Charger controller: Battery voltage below 8100mV for 5s");
                new_charge_status = Active;
            }
            break;
        case Active:
            if (not ac_plugged)
            {
                new_charge_status = NotConnected;
                break;
            }

            if (not charger_ntc_allowed)
            {
                log_debug("Charger controller: NTC not allowed (Active->Inactive)");
                new_charge_status = Inactive;
                m_reset_counters();
            }

            auto battery_voltager_full_charge_threshold = bfc_enabled ? 8150u : 8350u;
            if (m_battery_current_below_500ma_counter(battery_current_ma) &&
                battery_voltage_mv >= battery_voltager_full_charge_threshold)
            {
                log_debug("Charger controller: Battery current below 500mA for 5s");
                new_charge_status = Inactive;
                m_reset_counters();
                m_charger_ll_controller.on_full_charge();
            }
            break;
    }

    if (new_charge_status != m_charger_status)
    {
        log_highlight("Charger: %s -> %s", getDesc(m_charger_status), getDesc(new_charge_status));
        m_charger_status = new_charge_status;
        // Apply the charger status
        m_charger_status == Inactive ? m_charger_ll_controller.disable() : m_charger_ll_controller.enable(bfc_enabled);
    }

    return m_charger_status;
}
