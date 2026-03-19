#pragma once

#ifdef INCLUDE_FACTORY_TESTS
#include <unordered_map>
#endif // INCLUDE_FACTORY_TESTS
#include <cstdint>
#include <optional>

#include "external/teufel/libs/power/power.h"

#define PROPERTY_SET(_TYPE, _VARIABLE)                                                                                 \
    static void setProperty(_TYPE v)                                                                                   \
    {                                                                                                                  \
        _VARIABLE.set(v.value);                                                                                        \
    }

#define PROPERTY_ENUM_SET(_TYPE, _VARIABLE)                                                                            \
    static void setProperty(_TYPE v)                                                                                   \
    {                                                                                                                  \
        _VARIABLE.set(v, getDesc(v));                                                                                  \
    }

template <typename T>
auto getProperty()
{
    return getProperty(static_cast<T *>(nullptr));
}

template <typename T>
bool isProperty(T v)
{
    if constexpr (std::is_enum_v<T>)
    {
        return getProperty<T>() == v;
    }
    else
    {
        auto p = getProperty<T>();
        return p.value == v.value;
    }
}

template <typename... T>
bool isPropertyOneOf(T... v)
{
    if (((v == getProperty<T>()) || ...))
        return true;

    return false;
}

namespace Teufel::Ux::System
{

enum class Task : uint8_t
{
    System,
    Audio,
    Ui,
    Bluetooth,
#ifdef MYND_RPI_MODIFICATION
    RpiLink,
#endif // MYND_RPI_MODIFICATION
};

inline auto getDesc(const Task &value)
{
    switch (value)
    {
        case Task::System:
            return "System";
        case Task::Audio:
            return "Audio";
        case Task::Ui:
            return "Ui";
        case Task::Bluetooth:
            return "Bluetooth";
#ifdef MYND_RPI_MODIFICATION
        case Task::RpiLink:
            return "RpiLink";
#endif // MYND_RPI_MODIFICATION
        default:
            return "Unknown";
    }
}

enum class PowerState : uint8_t
{
    Off,
    On,
    PreOff,
    PreOn,
    Transition,
};

inline auto getDesc(const PowerState &value)
{
    switch (value)
    {
        case PowerState::Off:
            return "Off";
        case PowerState::On:
            return "On";
        case PowerState::PreOff:
            return "PreOff";
        case PowerState::PreOn:
            return "PreOn";
        case PowerState::Transition:
            return "Transition";
        default:
            return "Unknown";
    }
}

enum class Color : uint8_t
{
    Black,
    White,
    Berry,
    Mint,
};

inline auto getDesc(const Color &value)
{
    switch (value)
    {
        case Color::Black:
            return "Black";
        case Color::White:
            return "White";
        case Color::Berry:
            return "Berry";
        case Color::Mint:
            return "Mint";
        default:
            return "Unknown";
    }
}

enum class ChargerStatus : uint8_t
{
    Active,
    Inactive,
    NotConnected,
    Fault
};

inline auto getDesc(const ChargerStatus &value)
{
    switch (value)
    {
        case ChargerStatus::Active:
            return "Active";
        case ChargerStatus::Inactive:
            return "Inactive";
        case ChargerStatus::NotConnected:
            return "NotConnected";
        case ChargerStatus::Fault:
            return "Fault";
        default:
            return "Unknown";
    }
}

enum class ChargeType : uint8_t
{
    BatteryFriendly,
    FastCharge
};

inline auto getDesc(const ChargeType &value)
{
    switch (value)
    {
        case ChargeType::BatteryFriendly:
            return "BatteryFriendly";
        case ChargeType::FastCharge:
            return "FastCharge";
        default:
            return "Unknown";
    }
}

enum class BatteryCriticalTemperature : uint8_t
{
    TempOk,
    ChargeOverTemp,
    ChargeUnderTemp,
    DischargeOverTemp,
    DischargeUnderTemp,
    NTCLost
};

inline auto getDesc(const BatteryCriticalTemperature &value)
{
    switch (value)
    {
        case BatteryCriticalTemperature::TempOk:
            return "TempOk";
        case BatteryCriticalTemperature::ChargeOverTemp:
            return "ChargeOverTemp";
        case BatteryCriticalTemperature::ChargeUnderTemp:
            return "ChargeUnderTemp";
        case BatteryCriticalTemperature::DischargeOverTemp:
            return "DischargeOverTemp";
        case BatteryCriticalTemperature::DischargeUnderTemp:
            return "DischargeUnderTemp";
        case BatteryCriticalTemperature::NTCLost:
            return "NTCLost";
        default:
            return "Unknown";
    }
}

enum class BatteryLowLevelState : uint8_t
{
    Below10Percent,
    Below5Percent,
    Below1Percent,
};

inline auto getDesc(const BatteryLowLevelState &value)
{
    switch (value)
    {
        case BatteryLowLevelState::Below10Percent:
            return "Below10Percent";
        case BatteryLowLevelState::Below5Percent:
            return "Below5Percent";
        case BatteryLowLevelState::Below1Percent:
            return "Below1Percent";
        default:
            return "Unknown";
    }
}

// clang-format off
struct UserActivity {};
struct LedBrightness { uint8_t value; };
struct BatteryLevel { uint8_t value; };

struct OffTimer { uint8_t value; }; // value is represented in minutes, up to 4.2 hrs timeout possible...
struct OffTimerEnabled { bool value; };
struct FactoryResetRequest {};
struct FactoryReset {};
struct HardReset {};
#if defined(INCLUDE_FACTORY_TESTS) || defined(MYND_RPI_MODIFICATION)
struct ChargeLimitMode { bool value; };
#endif

struct BatterySocAccumulatedCharge { float value; };
struct BatterySocCapacity { float value; };

enum class BatterySoCAlgoState : uint8_t {
    Reset, // After the factory reset or new battery
    FirstStartOrBatteryReset,
    FirstFullChargeCompleted,
    FirstFullDischargeCompleted,
    NormalOperation,
};
// clang-format on

inline auto getDesc(const BatterySoCAlgoState &value)
{
    switch (value)
    {
        case BatterySoCAlgoState::Reset:
            return "Reset";
        case BatterySoCAlgoState::FirstStartOrBatteryReset:
            return "FirstStartOrBatteryReset";
        case BatterySoCAlgoState::FirstFullChargeCompleted:
            return "FirstFullChargeCompleted";
        case BatterySoCAlgoState::FirstFullDischargeCompleted:
            return "FirstFullDischargeCompleted";
        case BatterySoCAlgoState::NormalOperation:
            return "NormalOperation";
        default:
            return "Unknown";
    }
}

enum class PowerStateChangeReason : uint8_t
{
    UserRequest,
    OffTimer,
    BatteryLowLevel,
    BatteryLowLevelAfterBoot,
    BatteryCriticalTemperature,
    BroadcasterPowerOff,
};

inline auto getDesc(const PowerStateChangeReason &value)
{
    switch (value)
    {
        case PowerStateChangeReason::UserRequest:
            return "UserRequest";
        case PowerStateChangeReason::OffTimer:
            return "OffTimer";
        case PowerStateChangeReason::BatteryLowLevel:
            return "BatteryLowLevel";
        case PowerStateChangeReason::BatteryLowLevelAfterBoot:
            return "BatteryLowLevelAfterBoot";
        case PowerStateChangeReason::BatteryCriticalTemperature:
            return "BatteryCriticalTemperature";
        case PowerStateChangeReason::BroadcasterPowerOff:
            return "BroadcasterPowerOff";
        default:
            return "Unknown";
    }
}

using SetPowerState = Power::SetPowerState<Ux::System::PowerState, PowerStateChangeReason>;

// Public API
PowerState      getProperty(PowerState *);
LedBrightness   getProperty(LedBrightness *);
BatteryLevel    getProperty(BatteryLevel *);
OffTimer        getProperty(OffTimer *);
OffTimerEnabled getProperty(OffTimerEnabled *);
ChargerStatus   getProperty(ChargerStatus *);
ChargeType      getProperty(ChargeType *);
#if defined(INCLUDE_FACTORY_TESTS) || defined(MYND_RPI_MODIFICATION)
ChargeLimitMode getProperty(ChargeLimitMode *);
// BatteryTemperature getProperty(BatteryTemperature *);
#endif
}
