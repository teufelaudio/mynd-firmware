#pragma once

#include <cstdint>

namespace Teufel::Ux::Bluetooth
{
// clang-format off
struct BtWakeUp {};
struct StartPairing {};
#ifdef INCLUDE_TWS_MODE
struct TwsPairing {};
#endif // INCLUDE_TWS_MODE
struct MultichainPairing {};
enum class MultichainExitReason: uint8_t
{
    Unknown,
    UserRequest,
    PowerOff,
};

struct StopPairingAndMultichain
{
    MultichainExitReason reason = MultichainExitReason::Unknown;
};
struct ClearDeviceList {};
struct PlayPause {};
struct NextTrack {};
struct PreviousTrack {};

enum class VolumeChange: uint8_t {
    Up,
    Down,
};

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

// This represents the 9 different states that the source LED can be in
// which directly depend on the status of the Bluetooth module
enum class Status: uint8_t {
    BluetoothDisconnected,
    BluetoothConnected,
    BluetoothPairing,
#ifdef INCLUDE_TWS_MODE
    TwsMasterPairing,
    TwsChainMaster,
#endif // INCLUDE_TWS_MODE
    SlavePairing,
    CsbChainMaster,
    ChainSlave,
    AuxConnected,
    UsbConnected,
    DfuMode,
    None
};

inline auto getDesc(const Status &value)
{
    switch (value)
    {
        case Status::BluetoothDisconnected:
            return "BluetoothDisconnected";
        case Status::BluetoothConnected:
            return "BluetoothConnected";
        case Status::BluetoothPairing:
            return "BluetoothPairing";
#ifdef INCLUDE_TWS_MODE
        case Status::TwsMasterPairing:
            return "TwsMasterPairing";
        case Status::TwsChainMaster:
            return "TwsChainMaster";
#endif // INCLUDE_TWS_MODE
        case Status::SlavePairing:
            return "SlavePairing";
        case Status::CsbChainMaster:
            return "CsbChainMaster";
        case Status::ChainSlave:
            return "ChainSlave";
        case Status::AuxConnected:
            return "AuxConnected";
        case Status::UsbConnected:
            return "UsbConnected";
        case Status::DfuMode:
            return "DfuMode";
        case Status::None:
            return "None";
        default:
            return "Unknown";
    }
}

struct StreamingActive { bool value; };
struct NotifyAuxConnectionChange { bool connected; };
struct NotifyUsbConnectionChange { bool connected; };
struct EnterDfuMode {};
#ifdef INCLUDE_FACTORY_TESTS
struct FWVersionProdTest {};
struct DeviceNameProdTest {};
struct BtMacAddressProdTest {};
struct BleMacAddressProdTest {};
struct BtRssiProdTest {};
struct SetVolumeProdTest { uint8_t volume_req; };
enum class AudioBypassProdTest: uint8_t {
    Enter,
    Exit,
};
#endif // INCLUDE_FACTORY_TESTS
// clang-format on

// Public API
Status          getProperty(Status *);
StreamingActive getProperty(StreamingActive *);
}
