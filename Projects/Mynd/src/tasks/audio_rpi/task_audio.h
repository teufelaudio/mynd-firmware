#pragma once

#include <variant>
#include <optional>

#include "ux/audio/audio.h"
#include "ux/bluetooth/bluetooth.h"
#include "ux/system/system.h"

namespace Teufel::Task::Audio
{

// clang-format off
struct IoExpanderInterrupt {};

using AudioMessage = std::variant<
    Teufel::Ux::System::SetPowerState,
    Teufel::Ux::System::LedBrightness,
    Teufel::Ux::Audio::UpdateVolume,
    IoExpanderInterrupt,
    Teufel::Ux::System::FactoryReset,
    Teufel::Ux::System::HardReset,
    Teufel::Ux::Audio::SoundIconsActive,
    Teufel::Ux::Audio::EcoMode,
    Teufel::Ux::Audio::BassLevel,
    Teufel::Ux::Audio::TrebleLevel,
    Teufel::Ux::System::BatteryCriticalTemperature,
    Teufel::Ux::System::ChargeType,
    Teufel::Ux::System::BatteryLowLevelState
>;
// clang-format on

int start();

int postMessage(Teufel::Ux::System::Task source_task, AudioMessage msg);

}
