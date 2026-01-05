#pragma once

#ifdef INCLUDE_PRODUCTION_TESTS
#include <unordered_map>
#include "external/teufel/libs/tshell/tshell.h"
#endif // INCLUDE_PRODUCTION_TEST
#include <optional>
#include <variant>
#include "logger.h"
#include "battery.h"
#include "ux/system/system.h"
#include "ux/audio/audio.h"
#include "ux/bluetooth/bluetooth.h"
#include "external/teufel/libs/app_assert/app_assert.h"

#include "virtual_eeprom.h"
#include "eeprom_config.h"

namespace Storage
{

/* WARNING! Don't reorder the enum values, since they are used as indices in the flash
 * of BT module. */
// clang-format off
using Persistable = std::variant<Teufel::Ux::System::Color,
                                 Teufel::Ux::System::LedBrightness,
                                 Teufel::Ux::Audio::BassLevel,
                                 Teufel::Ux::Audio::TrebleLevel,
                                 Teufel::Ux::Audio::VolumeLevel,
                                 Teufel::Ux::Audio::EcoMode,
                                 Teufel::Ux::Audio::SoundIconsActive,
                                 Teufel::Ux::System::ChargeType, /* not used */
                                 Teufel::Ux::System::OffTimer,
                                 Teufel::Ux::System::OffTimerEnabled,

                                 Teufel::Ux::System::BatterySoCAlgoState,
                                 Teufel::Ux::System::BatterySocAccumulatedCharge,
                                 Teufel::Ux::System::BatterySocCapacity
                                >;
// clang-format on

template <typename T, uint16_t Idx = 0>
constexpr uint16_t getTypeIdx()
{
    static_assert(Idx < std::variant_size_v<Persistable>, "invalid type");
    if constexpr (std::is_same_v<T, std::variant_alternative_t<Idx, Persistable>>)
        return Idx;
    else
        return getTypeIdx<T, Idx + 1>();
}

inline void init()
{
    vEEPROM_Init();
}

template <typename T>
constexpr std::optional<T> load()
{
    if constexpr (std::is_same_v<T, Teufel::Ux::System::ChargeType>)
    {
        /* According to the spec, the charge type should be always BatteryFriendly after reboot. */
        return Teufel::Ux::System::ChargeType::BatteryFriendly;
    }

    if constexpr (std::is_same_v<T, Teufel::Ux::System::BatterySoCAlgoState>)
    {
        uint16_t cell_value;

        if (vEEPROM_AddressRead(0x70, &cell_value) == 0)
        {
            return std::optional<T>(static_cast<T>(cell_value));
        }
    }
    else if constexpr (std::is_same_v<T, Teufel::Ux::System::BatterySocAccumulatedCharge>)
    {
        uint16_t cell_value_msb;
        uint16_t cell_value_lsb;
        if (vEEPROM_AddressRead(0x71, &cell_value_msb) == 0 && vEEPROM_AddressRead(0x72, &cell_value_lsb) == 0)
        {
            uint32_t cell_value = (cell_value_msb << 16) | cell_value_lsb;
            auto     c          = *(float *) &cell_value;
            return std::optional<T>(T{.value = c});
        }
    }
    else if constexpr (std::is_same_v<T, Teufel::Ux::System::BatterySocCapacity>)
    {
        uint16_t cell_value_msb;
        uint16_t cell_value_lsb;
        if (vEEPROM_AddressRead(0x73, &cell_value_msb) == 0 && vEEPROM_AddressRead(0x74, &cell_value_lsb) == 0)
        {
            uint32_t cell_value = (cell_value_msb << 16) | cell_value_lsb;
            auto     c          = *(float *) &cell_value;
            return std::optional<T>(T{.value = c});
        }
    }
    else
    {
        uint16_t cell_value;
        auto     key = getTypeIdx<T>();

        if (vEEPROM_AddressRead(key, &cell_value) == 0)
        {
            if constexpr (std::is_enum_v<T>)
                return std::optional<T>(static_cast<T>(cell_value));
            else
                return std::optional<T>(T{.value = static_cast<decltype(T::value)>(cell_value)});
        }
    }

    return std::nullopt;
}

template <typename T>
constexpr void save(T v)
{
    using BaseT = std::decay_t<T>;
    auto key    = getTypeIdx<BaseT>();

    if constexpr (std::is_same_v<T, Teufel::Ux::System::BatterySoCAlgoState>)
    {
        auto cell_value = static_cast<uint16_t>(v);
        vEEPROM_AddressWrite(0x70, cell_value);

        return;
    }

    if constexpr (std::is_same_v<T, Teufel::Ux::System::BatterySocAccumulatedCharge>)
    {
        uint32_t c              = *(uint32_t *) &v.value;
        uint16_t cell_value_msb = (c >> 16) & 0xFFFF;
        uint16_t cell_value_lsb = c & 0xFFFF;

        vEEPROM_AddressWrite(0x71, cell_value_msb);
        vEEPROM_AddressWrite(0x72, cell_value_lsb);

        return;
    }

    if constexpr (std::is_same_v<T, Teufel::Ux::System::BatterySocCapacity>)
    {
        uint32_t c              = *(uint32_t *) &v.value;
        uint16_t cell_value_msb = (c >> 16) & 0xFFFF;
        uint16_t cell_value_lsb = c & 0xFFFF;

        vEEPROM_AddressWrite(0x73, cell_value_msb);
        vEEPROM_AddressWrite(0x74, cell_value_lsb);
    }

    if constexpr (std::is_enum_v<BaseT>)
        vEEPROM_AddressWrite(key, static_cast<uint32_t>(v));
    else
        vEEPROM_AddressWrite(key, static_cast<uint32_t>(v.value));
}

static inline void test_helper(const Teufel::Ux::System::BatterySocAccumulatedCharge &v)
{
    Storage::save(v);

    auto l = Storage::load<Teufel::Ux::System::BatterySocAccumulatedCharge>();

    APP_ASSERT(l.has_value(), "Failed to load BatterySocAccumulatedCharge");
    APP_ASSERT(l.value().value == v.value, "Invalid BatterySocAccumulatedCharge value");
}

inline void test()
{
    test_helper({0});
    test_helper({42});
    test_helper({3012});
    test_helper({17512});
    test_helper({67232});

    test_helper({32.234123415f});
    // test_helper({INT32_MAX});

    test_helper({-1});
    test_helper({-17});
    test_helper({-130});
    test_helper({-17333});
    test_helper({-128333.777});
    // test_helper({INT16_MIN});
    // test_helper({INT32_MIN});
}
}
