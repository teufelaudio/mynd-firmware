#pragma once

#include <cstdint>

#include "ux/system/system.h"

namespace Teufel::Task::Leds
{

enum class Led : uint8_t
{
    Status,
    Source,
};

enum class Color : uint8_t
{
    Off,
    Red,
    Green,
    Blue,
    Yellow,
    Orange,
    Purple,
    Cyan,
    White
};

#ifndef MYND_RPI_MODIFICATION
enum class SourcePattern : uint8_t
{
    Off,
    BluetoothDisconnected,
    BluetoothPairing,
    CsbMaster,
    CsbSlave,
#ifdef INCLUDE_TWS_MODE
    TwsMasterPairing,
#endif
    SlavePairing,
    OffFromBtDfu,
    OffFromBtSource,
    OffFromAuxSource,
    OffFromUsbSource,
    OffFromMstrChain,
    OffFromSlvChain,
    BluetoothConnected,
    AuxConnected,
    UsbConnected,
    PositiveFeedback,
    EcoModeOn,
    EcoModeOff,
    BtDfu,
};

inline auto getDesc(const SourcePattern &value)
{
    switch (value)
    {
        case SourcePattern::Off:
            return "Off";
        case SourcePattern::BluetoothDisconnected:
            return "BluetoothDisconnected";
        case SourcePattern::BluetoothPairing:
            return "BluetoothPairing";
        case SourcePattern::CsbMaster:
            return "CsbMaster";
        case SourcePattern::CsbSlave:
            return "CsbSlave";
        default:
            return "Unknown";
    }
}
#else
enum class SourcePattern : uint8_t
{
    Off,
    EcoModeOn,
    EcoModeOff,
    RpiPending,
    RpiMpdStreaming,
    RpiMpdIdle,
    RpiAirplayStreaming,
    RpiAirplayIdle,
    RpiBluetoothStreaming,
    RpiBluetoothIdle,
    RpiSpotifyStreaming,
    RpiSpotifyIdle,
    OffFromRpiMpd,
    OffFromRpiAirplay,
    OffFromRpiBt,
    OffFromRpiSpotify,
    PositiveFeedback,
    NegativeFeedback,
};

inline auto getDesc(const SourcePattern &value)
{
    switch (value)
    {
        case SourcePattern::Off:
            return "Off";
        case SourcePattern::PositiveFeedback:
            return "PositiveFeedback";
        case SourcePattern::RpiPending:
            return "RpiPending";
        case SourcePattern::RpiMpdStreaming:
            return "RpiMpdStreaming";
        case SourcePattern::RpiAirplayStreaming:
            return "RpiAirplayStreaming";
        case SourcePattern::RpiBluetoothStreaming:
            return "RpiBluetoothStreaming";
        case SourcePattern::RpiSpotifyStreaming:
            return "RpiSpotifyStreaming";
        default:
            return "Unknown";
    }
}
#endif // NOT MYND_RPI_MODIFICATION

void tick();
bool is_engine_running(Led led);
void run_engines();
void set_solid_color(Led led, Color color);
void set_solid_rgb(Led led, uint8_t r, uint8_t g, uint8_t b);
void set_source_pattern(SourcePattern pattern);
void set_brightness(uint8_t brightness);

void user_activity();

void indicate_battery_level(const Teufel::Ux::System::BatteryLevel &p);
void indicate_factory_reset(const Ux::System::FactoryReset &p);
void indicate_charge_type(const Ux::System::ChargeType &charge_type, const Ux::System::BatteryLevel &battery_level);
void indicate_low_battery_level(const Ux::System::BatteryLowLevelState &p);
void indicate_power_off(const Ux::System::BatteryLevel &battery_level);
void indicate_temperature_warning(const Ux::System::BatteryCriticalTemperature &p);

}
