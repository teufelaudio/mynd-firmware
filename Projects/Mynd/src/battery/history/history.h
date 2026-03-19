#pragma once

#include <cstdint>

namespace Battery
{

/*
 * Recording time:
 *   1 Minute in 0.5s intervals (120 steps)
 *   5 Minutes in 2s intervals (150 steps, 120 additional)
 *   15 Minutes in 6s intervals (150 steps, 100 additional )
 *   1 Hour in 30s intervals (120 steps, 90 additional)
 *   5 Hours in 2min intervals (150 steps, 120 additional)
 *   24 Hours in 10min intervals (144 steps, 114 additional)
 *
 *   1 - 120 x 6bytes = 720
 *   2 - 150 x 6bytes = 900 (total: 1620)
 *   3 - 150 x 6bytes = 900 (total: 2520)
 *   4 - 120 x 6bytes = 720 (total: 3240)
 *   5 - 150 x 6bytes = 900 (total: 4140)
 *   6 - 144 x 6bytes = 864 (total: 5004)
 */

class History
{
  public:
    struct Record
    {
        uint16_t system_power;            // Milliwatts (lsb = 2mW, max = 131W)
        int16_t  power_in_out_of_battery; // Milliwatts (lsb = 4mW, max +-131W)
        uint8_t  battery_charge;          // 0-100%
        uint8_t  status;
    } __attribute__((packed));

    static_assert(sizeof(Record) == 6, "Size of Battery::History::Record is not 6 bytes");

    void add_record(const Record &record);

  private:
    uint8_t m_buffer_1m_index = 0u;
    uint8_t m_buffer_5m_index = 0u;
    // uint8_t m_buffer_15m_index = 0u;
    // uint8_t m_buffer_1h_index = 0u;
    // uint8_t m_buffer_5h_index = 0u;
    // uint8_t m_buffer_24h_index = 0u;
};

} // namespace Battery