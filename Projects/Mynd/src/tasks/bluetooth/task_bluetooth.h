#pragma once

#include <variant>
#include <optional>

#include "ux/audio/audio.h"
#include "ux/bluetooth/bluetooth.h"
#include "ux/system/system.h"

namespace Teufel::Task::Bluetooth
{

// clang-format off
struct ActionsReady{};

using BluetoothMessage = std::variant<
    Teufel::Ux::System::SetPowerState,
    Teufel::Ux::System::BatteryLevel,
    Teufel::Ux::System::ChargerStatus,
    Teufel::Ux::System::ChargeType,
    Teufel::Ux::System::Color,
    ActionsReady,
    Teufel::Ux::Bluetooth::BtWakeUp,
    Teufel::Ux::Bluetooth::StartPairing,
#ifdef INCLUDE_TWS_MODE
    Teufel::Ux::Bluetooth::TwsPairing,
#endif // INCLUDE_TWS_MODE
    Teufel::Ux::Bluetooth::MultichainPairing,
    Teufel::Ux::Bluetooth::VolumeChange,
    Teufel::Ux::Bluetooth::StopPairingAndMultichain,
    Teufel::Ux::Bluetooth::ClearDeviceList,
    Teufel::Ux::Bluetooth::PlayPause,
    Teufel::Ux::Bluetooth::NextTrack,
    Teufel::Ux::Bluetooth::PreviousTrack,
    Teufel::Ux::Bluetooth::NotifyAuxConnectionChange,
    Teufel::Ux::Bluetooth::NotifyUsbConnectionChange,
    Teufel::Ux::Bluetooth::EnterDfuMode,
    Teufel::Ux::System::FactoryReset,
    Teufel::Ux::Audio::RequestSoundIcon,
    Teufel::Ux::Audio::StopPlayingSoundIcon,
    Teufel::Ux::Audio::EcoMode
#ifdef INCLUDE_FACTORY_TESTS
    ,
    Teufel::Ux::Bluetooth::FWVersionProdTest,
    Teufel::Ux::Bluetooth::DeviceNameProdTest,
    Teufel::Ux::Bluetooth::BtMacAddressProdTest,
    Teufel::Ux::Bluetooth::BleMacAddressProdTest,
    Teufel::Ux::Bluetooth::BtRssiProdTest,
    Teufel::Ux::Bluetooth::SetVolumeProdTest,
    Teufel::Ux::Bluetooth::AudioBypassProdTest
#endif // INCLUDE_FACTORY_TESTS
>;
// clang-format on

int start();

int postMessage(Teufel::Ux::System::Task source_task, BluetoothMessage msg);

}
