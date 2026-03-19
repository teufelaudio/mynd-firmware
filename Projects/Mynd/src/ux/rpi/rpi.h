#pragma once

#include <cstdint>

namespace Teufel::Ux::RpiLink
{
// clang-format off
enum class PowerState : uint8_t
{
    Off,
    On,
    Standby,
    ShutdownRequested,
};
enum class HostSource: uint8_t {
    Unknown,
    Mpd,
    Airplay,
    Bluetooth,
    Spotify,
};
enum class VolumeChange: uint8_t {
    Up,
    Down,
};
enum class WifiCommandAction : uint8_t
{
    Unknown,
    ConfigureWifi,
    EnableHotspot,
    CycleWifiNetwork,
};
enum class WifiCommandDetail : uint8_t
{
    None,
    Busy,
};
struct PlayPause {};
struct NextTrack {};
struct PreviousTrack {};
struct StreamingActive { bool value; };
struct DragAndDropUpdate {};
/** Trigger to run configure_wifi on RPi task (credentials in task_rpi static buffers). */
struct ConfigureWifi {};
/** Trigger to enable WiFi hotspot on RPi (SSID/password in daemon .conf). */
struct EnableHotspot {};
/** Trigger to cycle to the next known WiFi network on RPi. */
struct CycleWifiNetwork {};
/** Trigger to switch the active host source back to MPD. */
struct CycleSource {};

inline auto getDesc(const PowerState &value)
{
    switch (value)
    {
        case PowerState::Off:
            return "Off";
        case PowerState::On:
            return "On";
        case PowerState::Standby:
            return "Standby";
        case PowerState::ShutdownRequested:
            return "ShutdownRequested";
        default:
            return "Unknown";
    }
}

inline auto getDesc(const HostSource &value)
{
    switch (value)
    {
        case HostSource::Unknown:
            return "Unknown";
        case HostSource::Mpd:
            return "Mpd";
        case HostSource::Airplay:
            return "Airplay";
        case HostSource::Bluetooth:
            return "Bluetooth";
        case HostSource::Spotify:
            return "Spotify";
        default:
            return "Unknown";
    }
}

inline auto getDesc(const VolumeChange &value)
{
    switch (value)
    {
    case VolumeChange::Up:
        return "Up";
    case VolumeChange::Down:
        return "Down";
    default:
        return "Unknown";
    }
}

inline auto getDesc(const WifiCommandAction &value)
{
    switch (value)
    {
        case WifiCommandAction::Unknown:
            return "Unknown";
        case WifiCommandAction::ConfigureWifi:
            return "ConfigureWifi";
        case WifiCommandAction::EnableHotspot:
            return "EnableHotspot";
        case WifiCommandAction::CycleWifiNetwork:
            return "CycleWifiNetwork";
        default:
            return "Unknown";
    }
}

inline auto getDesc(const WifiCommandDetail &value)
{
    switch (value)
    {
        case WifiCommandDetail::None:
            return "None";
        case WifiCommandDetail::Busy:
            return "Busy";
        default:
            return "Unknown";
    }
}
// clang-format on

// Public API
PowerState      getProperty(PowerState *);
StreamingActive getProperty(StreamingActive *);
HostSource      getProperty(HostSource *);
}
