#include <array>
#include <span>
#include <algorithm>
#include <numeric>

#define LOG_LEVEL LOG_LEVEL_INFO
#include "logger.h"

#include "history.h"

namespace Battery
{

static inline auto record_system_power_into(uint32_t system_power_mw)
{
    // lsb = 2mW, max = 131W
    return static_cast<uint16_t>(system_power_mw >> 2);
}

static inline auto record_power_in_out_of_battery_into(uint16_t battery_voltage_mv, int16_t battery_in_out_current_ma)
{
    // lsb = 4mW, max +-131W
    return static_cast<int16_t>(battery_in_out_current_ma * battery_voltage_mv >> 4);
}

static inline auto record_system_power_from(uint16_t system_power)
{
    return static_cast<uint32_t>(system_power << 2);
}

static inline auto record_power_in_out_of_battery_from(uint16_t battery_voltage_mv, int16_t battery_in_out_current_ma)
{
    return static_cast<int16_t>(battery_in_out_current_ma * battery_voltage_mv >> 4);
}

static History::Record average_power(std::span<History::Record> records)
{
    History::Record average = {
        0, 0, 0
        // ,                  0
    };

    // Average all fields of Record and return the average
    average.system_power =
        std::accumulate(records.begin(), records.end(), 0,
                        [](int sum, const History::Record &record) { return sum + record.system_power; }) /
        records.size();

    average.power_in_out_of_battery =
        std::accumulate(records.begin(), records.end(), 0,
                        [](int sum, const History::Record &record) { return sum + record.power_in_out_of_battery; }) /
        records.size();

    average.battery_charge =
        std::accumulate(records.begin(), records.end(), 0,
                        [](int sum, const History::Record &record) { return sum + record.battery_charge; }) /
        records.size();

    // average.status = std::accumulate(records.begin(), records.end(), 0,
    //     [](int sum, const Record& record) { return sum + record.status; }) / records.size();

    return average;
}

void History::add_record(const Record &record)
{
    // System power is in W
    // auto = system_power * 32.25806;

    /*
     Record {
        record_system_power_into(system_power_mw),
        record_power_in_out_of_battery_into(battery_voltage_mv, battery_in_out_current_ma),
        battery_charge, // 0-100%
        is_charging
    };
    */

    // The first two buffers are volatile and used only during the runtime.
    static constexpr uint16_t                   c_buffer_1m_size = 120;
    static constexpr uint16_t                   c_buffer_5m_size = 150;
    static std::array<Record, c_buffer_1m_size> buffer1m;
    static std::array<Record, c_buffer_5m_size> buffer5m;

    constexpr auto c_buffer_1m_interval_ms  = 500u;    // 0.5 seconds interval
    constexpr auto c_buffer_5m_interval_ms  = 2000u;   // 2 seconds interval
    constexpr auto c_buffer_15m_interval_ms = 6000u;   // 6 seconds interval
    constexpr auto c_buffer_1h_interval_ms  = 30000u;  // 30 seconds interval
    constexpr auto c_buffer_5h_interval_ms  = 120000u; // 2 minutes interval
    constexpr auto c_buffer_24h_interval_ms = 600000u; // 10 minutes interval

    constexpr auto c_buffer_5m_modulo  = c_buffer_5m_interval_ms / c_buffer_1m_interval_ms;
    constexpr auto c_buffer_15m_modulo = c_buffer_15m_interval_ms / c_buffer_5m_interval_ms;
    constexpr auto c_buffer_1h_modulo  = c_buffer_1h_interval_ms / c_buffer_15m_interval_ms;
    constexpr auto c_buffer_5h_modulo  = c_buffer_5h_interval_ms / c_buffer_1h_interval_ms;
    constexpr auto c_buffer_24h_modulo = c_buffer_24h_interval_ms / c_buffer_5h_interval_ms;
    log_info("save battery history record");

    // add to the buffer with overflow check by module operation
    buffer1m[++m_buffer_1m_index % c_buffer_1m_size] = record;

    if (m_buffer_1m_index % c_buffer_5m_modulo == 0)
    {
        // fill 5 minutes buffer in 2s intervals
        // averaging values from buffer1m buffer (last 4 records)
        buffer5m[++m_buffer_5m_index % c_buffer_5m_size] =
            average_power(std::span<Record>{buffer1m.data() + m_buffer_1m_index - 3, 4});
    }

    if (m_buffer_5m_index % c_buffer_15m_modulo == 0)
    {
        // fill 15 minutes buffer
        // average values from buffer5m buffer (last 3 records)
        auto record = average_power(std::span<Record>{buffer5m.data() + m_buffer_5m_index - 2, 3});
        // write this record to persistent memory (EEPROM on PD controller)
    }

#if 0
    if (m_buffer_15m_index % c_buffer_1h_modulo == 0)
    {
        // fill 1 hour buffer in 30s intervals
        // average values from buffer15m buffer (last 5 records)
        // read last 5 values from EEPROM from buffer15m
        // auto record = average_power(std::span<Record>{buffer15m.data() + m_buffer_15m_index - 4, 5});
        // write this record to persistent memory (EEPROM on PD controller)
    }

    if (m_buffer_1h_index % c_buffer_5h_modulo == 0)
    {
        // TODO: fill 5 hours buffer in 2 minutes intervals
    }

    if (m_buffer_5h_index % c_buffer_24h_modulo == 0)
    {
        // TODO: fill 24 hours buffer in 10 minutes intervals
    }
#endif
}

} // namespace Battery
