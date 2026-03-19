#pragma once

#include <variant>
#include <optional>

#include "ux/rpi/rpi.h"
#include "ux/system/system.h"
#include "actionslink_types.h"

namespace Teufel::Task::RpiLink
{

// clang-format off
struct ActionsReady{};

using RpiLinkMessage = std::variant<
    Teufel::Ux::System::SetPowerState,
    Teufel::Ux::RpiLink::PlayPause,
    Teufel::Ux::RpiLink::NextTrack,
    Teufel::Ux::RpiLink::PreviousTrack,
    Teufel::Ux::RpiLink::VolumeChange,
    Teufel::Ux::System::BatteryLevel,
    Teufel::Ux::System::ChargerStatus,
    Teufel::Ux::System::ChargeType,
    ActionsReady,
    Teufel::Ux::RpiLink::DragAndDropUpdate,
    Teufel::Ux::RpiLink::ConfigureWifi,
    Teufel::Ux::RpiLink::EnableHotspot,
    Teufel::Ux::RpiLink::CycleWifiNetwork,
    Teufel::Ux::RpiLink::CycleSource
>;
// clang-format on

int start();

int postMessage(Teufel::Ux::System::Task source_task, RpiLinkMessage msg);

}
