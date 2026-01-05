#include "external/teufel/libs/property/property.h"

#include "ux/system/system.h"
#include "ux/input/input.h"

#include "gitversion/version.h"
#include "external/teufel/libs/tshell/tshell.h"

#include "config.h"
#include "battery.h"
#include "leds.h"

#include "task_system.h"
#include "task_bluetooth.h"

#include "persistent_storage/kvstorage.h"

#include "board.h"
#include "board_link.h"
#include "button_handler.h"
#include "input_events.h"

#include "external/teufel/libs/core_utils/mapper.h"

#include "tests.h"

#ifndef BOOTLOADER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

SHELL_STATIC_SUBCMD_SET_CREATE(sub_si_play,
    SHELL_CMD_NO_ARGS(ok, "", []() { Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio,
                                     Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK}); }),
    SHELL_CMD_NO_ARGS(bf, "", []() { Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio,
                                     Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_BUTTON_FAILED}); }),
    SHELL_CMD_NO_ARGS(err, "", []() { Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio,
                                     Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_ERROR}); }),
    SHELL_CMD_NO_ARGS(fw, "", []() { Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio,
                                     Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_FW_ANNOUNCEMENT}); }),
    SHELL_CMD_NO_ARGS(bt, "", []() { Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio,
                                     Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_BT_PAIRING,
                                                           ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY,
                                                           true}); }),
    SHELL_CMD_NO_ARGS(low, "battery low", []() { Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio,
                                                 Teufel::Ux::Audio::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_BATTERY_LOW}); }),
    SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_si,
    SHELL_CMD_ARG(play, &sub_si_play, "", NULL, 2, 1),
    SHELL_CMD_NO_ARGS(stop, "", [](){
            Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Audio::StopPlayingSoundIcon{ACTIONSLINK_SOUND_ICON_NONE});
        }),
    SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_ARG_REGISTER(si, &sub_si, "sound icon", NULL, 2, 0);

SHELL_CMD_ARG_REGISTER(l10, NULL, "low10", +[](){ Teufel::Task::Audio::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::System::BatteryLowLevelState::Below10Percent); }, 2, 0);
SHELL_CMD_ARG_REGISTER(l5, NULL, "low5", +[](){ Teufel::Task::Audio::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::System::BatteryLowLevelState::Below5Percent); }, 2, 0);

SHELL_CMD_ARG_REGISTER(version, NULL, "Firmware version", +[]()
{
    printf("%d.%d.%d\r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
}, 0, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bass,
    SHELL_CMD_PARSED_INT8(set, "set bass[-6..6]", CONFIG_DSP_BASS_MIN, CONFIG_DSP_BASS_MAX, [](int8_t v) { board_link_amps_set_bass_level(v); }),
    SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_ARG_REGISTER(bass, &sub_bass, "bass", NULL, 2, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_treble,
    SHELL_CMD_PARSED_INT8(set, "set treble[-6..6]", CONFIG_DSP_TREBLE_MIN, CONFIG_DSP_TREBLE_MAX, [](int8_t v) { board_link_amps_set_treble_level(v); }),
    SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_ARG_REGISTER(treble, &sub_treble, "treble", NULL, 2, 0);

#pragma GCC diagnostic pop
#endif // BOOTLOADER NOT DEFINED

#ifdef INCLUDE_PRODUCTION_TESTS

static PropertyNonOpt<decltype(Teufel::Ux::System::ChargeLimitMode::value)> m_charge_limit_mode{"charge limit mode", false, false};
PROPERTY_SET(Teufel::Ux::System::ChargeLimitMode, m_charge_limit_mode)

static uint8_t test_keys_pressed = 0;
static uint32_t key_test_start_time = 0;
static bool power_on_with_prompt = false;


// Production Test Mode
static bool test_mode_activated = false;
static bool led_test_activated  = false;
static bool key_test_activated  = false;

typedef void (*button_event_handler_fn_t)(Teufel::Ux::InputState event);

bool is_test_mode_activated()
{
    return test_mode_activated;
}

bool is_led_test_activated()
{
    return led_test_activated;
}

bool is_key_test_activated()
{
    return key_test_activated;
}

void set_power_with_prompt(bool value)
{
    power_on_with_prompt = value;
}

void factory_test_key_process()
{
    if (is_key_test_activated() || power_on_with_prompt)
    {
        uint32_t time_elapsed = 0;
        if (key_test_start_time != 0)
        {
            time_elapsed = board_get_ms_since(key_test_start_time);
            uint8_t power_bt_play_plus_minus_btns_all_pressed = BUTTON_ID_POWER |
                                                                BUTTON_ID_BT    |
                                                                BUTTON_ID_PLAY  |
                                                                BUTTON_ID_PLUS  |
                                                                BUTTON_ID_MINUS; // 0b11111
            if (test_keys_pressed == power_bt_play_plus_minus_btns_all_pressed && !power_on_with_prompt)
            {
                printf("KEY TEST OK\r\n");
                key_test_activated = false;
            }
            else if (time_elapsed >= 30000 && !power_on_with_prompt) // wait 30 seconds
            {
                printf("KEY TEST FAILED\r\n");
                key_test_activated = false;
            }

            if (time_elapsed >= 8000 && power_on_with_prompt) // wait 8 seconds for power on logs to finish
            {
                uint8_t enter_key = 0x000D;
                tshell_process_char(enter_key);
                power_on_with_prompt = false;
            }
        }
        else
            key_test_start_time = get_systick();

        if (!is_key_test_activated() && !power_on_with_prompt)
        {
            key_test_start_time = 0;
            test_keys_pressed = 0;
        }
    }
}

// Prod Test Mode Helper Functions
static void reset_leds()
{
    // Reset LEDs
    Teufel::Task::Leds::indicate_battery_level(getProperty<Teufel::Ux::System::BatteryLevel>());
    auto bt_status = getProperty<Teufel::Ux::Bluetooth::Status>();
    Teufel::Task::Audio::postMessage(Teufel::Ux::System::Task::System, bt_status);
}

static void reset_prod_test_mode() {
    test_mode_activated = false;
    key_test_activated = false;
    led_test_activated = false;
    setProperty(Teufel::Ux::System::ChargeLimitMode {false});
    reset_leds();
    printf("Reset Test Mode\r\n");
}

static void print_pd_version()
{
    uint8_t pd_fw_version = 0;
    if (board_link_usb_pd_controller_fw_version(&pd_fw_version) != 0)
    {
        printf("get PD version failed.\r\n");
    }
    else
    {
        // PD FW version is written as hex number, e.g. 0x21 = v2.1
        printf("PD:%d.%d\r\n", pd_fw_version >> 4, pd_fw_version & 0x0F );
    }
}

template<typename T>
static void print_with_prompt(const char* text, T value)
{
    printf("MYND$ ");
    printf(text, value);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

//
// Prod Test Mode Shell Commands
//
SHELL_CMD_ARG_REGISTER(AZ, NULL, "t: Enter Test Mode", +[]()
{
    if( !test_mode_activated )
    {
        printf("Test Mode Start\r\n");
    }
    else
    {
        reset_prod_test_mode();
    }
    test_mode_activated = true;
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AX, NULL, "t: Exit Test Mode", +[]()
{
    if( test_mode_activated )
    {
        reset_prod_test_mode();
        printf("Exit Test Mode\r\n");
        board_link_usb_switch_to_bluetooth(); // Eastech will need to press PWR+BT combo again after exiting, to route uart 2 usb
    }
    else
    {
        printf("Test Mode Inactive\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A1, NULL, "t: Enter LED Test Mode", +[]()
{
    if( test_mode_activated )
    {
        printf("LED Test\r\n");
        led_test_activated = true;

        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Status, Teufel::Task::Leds::Color::White);
        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Source, Teufel::Task::Leds::Color::White);
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A2, NULL, "t: LED A Red, LED B Green", +[]()
{
    if( test_mode_activated )
    {
        printf("LED Test\r\n");
        led_test_activated = true;

        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Status, Teufel::Task::Leds::Color::Red);
        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Source, Teufel::Task::Leds::Color::Green);
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A3, NULL, "t: LED A Green, LED B Blue", +[]()
{
    if( test_mode_activated )
    {
        printf("LED Test\r\n");
        led_test_activated = true;

        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Status, Teufel::Task::Leds::Color::Green);
        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Source, Teufel::Task::Leds::Color::Blue);
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A4, NULL, "t: LED A Blue, LED B Red", +[]()
{
    if( test_mode_activated )
    {
        printf("LED Test\r\n");
        led_test_activated = true;

        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Status, Teufel::Task::Leds::Color::Blue);
        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Source, Teufel::Task::Leds::Color::Red);
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A5, NULL, "t: Exit LED Test Mode", +[]()
{
    if( test_mode_activated && led_test_activated )
    {
        printf("Exit LED Test\r\n");
        led_test_activated = false;

        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Status, Teufel::Task::Leds::Color::Off);
        Teufel::Task::Leds::set_solid_color(Teufel::Task::Leds::Led::Source, Teufel::Task::Leds::Color::Off);

        reset_leds();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A6, NULL, "t: Battery Level Detection", +[]()
{
    if( test_mode_activated )
    {
        print_with_prompt("%u\r\n", getProperty<Teufel::Ux::System::BatteryLevel>().value);
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A7, NULL, "t: Battery Voltage Detection", +[]()
{
    if( test_mode_activated )
    {
        print_with_prompt("%u\r\n", Teufel::Task::Battery::get_battery_voltage_mv());
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A8, NULL, "t: Key Test", +[]()
{
    if( test_mode_activated )
    {
        key_test_activated = true;
        printf("Start Key Test\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(A9, NULL, "t: Volume", +[](const struct shell *, size_t argc, char **argv)
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::SetVolumeProdTest {(uint8_t)atoi(argv[1])});
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AA, NULL, "t: Get BT MAC", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::BtMacAddressProdTest {});
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AY, NULL, "t: get BLE MAC", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::BleMacAddressProdTest {});
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AB, NULL, "t: Clear BT Paired Device List", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::ClearDeviceList {});
        printf("OK\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AC, NULL, "t: AUX Connection Detection", +[]()
{
    if( test_mode_activated )
    {
        printf("AUX test: %s\r\n", board_link_plug_detection_is_jack_connected() ? "connected" : "disconnected");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AD, NULL, "t: Version Detection", +[]()
{
    if( test_mode_activated )
    {
        printf("MCU:%d.%d.%d\r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::FWVersionProdTest {});

        print_pd_version();
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AE, NULL, "t: Battery Temperature Detection", +[]()
{
    if( test_mode_activated )
    {
        print_with_prompt("%d\r\n", Teufel::Task::Battery::get_battery_temperature());
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AF, NULL, "t: Enter Standby", +[]()
{
    if( test_mode_activated )
    {
        printf("Power Off\r\n");
        vTaskDelay(pdMS_TO_TICKS(10));
        Teufel::Task::System::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::System::SetPowerState { Teufel::Ux::System::PowerState::Off, Teufel::Ux::System::PowerState::On });
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AG, NULL, "t: Reset", +[]()
{
    if( test_mode_activated )
    {
        reset_prod_test_mode();
        Teufel::Task::System::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::System::FactoryResetRequest {});
        vTaskDelay(pdMS_TO_TICKS(1000)); // wait for factory reset to complete before powering off
        printf("Reset OK\r\n");
        Teufel::Task::System::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::System::SetPowerState { Teufel::Ux::System::PowerState::Off, Teufel::Ux::System::PowerState::On });
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(Ag, NULL, "t: Reset1", +[]()
{
    if( test_mode_activated )
    {
        reset_prod_test_mode();
        Teufel::Task::System::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::System::FactoryResetRequest {});
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("Reset1 OK\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AH, NULL, "t: Switch To BT Mode", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::StartPairing {});
        printf("BT Source\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AI, NULL, "t: Get PD version", +[]()
{
    if( test_mode_activated )
    {
        print_pd_version();
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AJ, NULL, "t: Get BT Version", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::FWVersionProdTest {});
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AK, NULL, "t: Get MCU Version", +[]()
{
    if( test_mode_activated )
    {
        printf("MCU:%d.%d.%d\r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AL, NULL, "t: Enter AUDIO BYPASS MODE", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::AudioBypassProdTest::Enter);
        printf("OK\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AM, NULL, "t: Exit AUDIO BYPASS MODE", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::AudioBypassProdTest::Exit);
        printf("OK\r\n");
    }
}, 2, 1);


SHELL_CMD_ARG_REGISTER(AT00, NULL, "t: Write Speaker Color To Black", +[]()
{
    if( test_mode_activated )
    {
        auto color = Teufel::Ux::System::Color{Teufel::Ux::System::Color::Black};
        Storage::save(color);
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, color);
        printf("Color=00\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AT01, NULL, "t: Write Speaker Color To White", +[]()
{
    if( test_mode_activated )
    {
        auto color = Teufel::Ux::System::Color{Teufel::Ux::System::Color::White};
        Storage::save(color);
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, color);
        printf("Color=01\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AT02, NULL, "t: Write Speaker Color To Wild Berry", +[]()
{
    if( test_mode_activated )
    {
        auto color = Teufel::Ux::System::Color{Teufel::Ux::System::Color::Berry};
        Storage::save(color);
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, color);
        printf("Color=02\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AT03, NULL, "t: Write Speaker Color To Light Mint", +[]()
{
    if( test_mode_activated )
    {
        auto color = Teufel::Ux::System::Color{Teufel::Ux::System::Color::Mint};
        Storage::save(color);
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, color);
        printf("Color=03\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AU, NULL, "t: Read Speaker Color", +[]()
{
    if( test_mode_activated )
    {
        auto color = Storage::load<Teufel::Ux::System::Color>();
        printf("Color=%s\r\n", color.has_value() ? getDesc(color.value()) : "Not defined");
    }
}, 2, 1);

// API Required
// SHELL_CMD_ARG_REGISTER(AS, NULL, "t: Write SN Number", +[]()
// {
//     if( test_mode_activated )
//     {
//         printf("OK\r\n");
//     }
// }, 2, 1);

// SHELL_CMD_ARG_REGISTER(AN, NULL, "t: Read SN Number", +[]()
// {
//     if( test_mode_activated )
//     {
//         printf("SN=%s\r\n", usbd_string_serial);
//     }
// }, 2, 1);

SHELL_CMD_ARG_REGISTER(AV, NULL, "t: Get BT Name", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::DeviceNameProdTest {});
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AW, NULL, "t: Get BT RSSI Value", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Task::Bluetooth::postMessage(Teufel::Ux::System::Task::Audio, Teufel::Ux::Bluetooth::BtRssiProdTest {});
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(Ap, NULL, "t: Set Battery Charge Limit", +[]()
{
    if( test_mode_activated )
    {
        setProperty(Teufel::Ux::System::ChargeLimitMode {true});
        vTaskDelay(pdMS_TO_TICKS(100));
        printf("OK\r\n");
    }
}, 2, 1);

#define CMD_HELP "t: Charge Limit Mode: Ap <0:Disable, 1:Enable>"
SHELL_CMD_ARG_REGISTER(AQ, NULL, "t: Close Battery Charge Function", +[]()
{
    if( test_mode_activated )
    {
        setProperty(Teufel::Ux::System::ChargeLimitMode {false});
        vTaskDelay(pdMS_TO_TICKS(100));
        printf("OK\r\n");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AR, NULL, "t: Get Battery Charge Status", +[]()
{
    if( test_mode_activated )
    {
        Teufel::Ux::System::ChargeType chargeType = getProperty<Teufel::Ux::System::ChargeType>();
        printf("Charge Status: %s\r\n", (chargeType == Teufel::Ux::System::ChargeType::FastCharge) ? "Fast Charge (1)" : "Battery Friendly (0)");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(ASBU, NULL, "t: Liquid Detection", +[]()
{
    if( test_mode_activated )
    {
        print_with_prompt("%s\r\n", board_link_moisture_detection_is_detected() ? "high" : "low");
    }
}, 2, 1);

SHELL_CMD_ARG_REGISTER(AHV, NULL, "t: Get Hardware Version", +[]()
{
    if( test_mode_activated )
    {
        printf("HW Rev:%02u\r\n", read_hw_revision());
        printf("BT Rev:%02u\r\n", read_bt_hw_revision());
        printf("AMP Rev:%02u\r\n", read_amp_hw_revision());
    }
}, 2, 1);

#pragma GCC diagnostic pop

TS_KEY_VALUE_CONST_MAP(EventHandlerProductionTestMapper, uint32_t, button_event_handler_fn_t,
    {BUTTON_ID_POWER, [](Teufel::Ux::InputState event) {
        if (is_key_test_activated() && event == Teufel::Ux::InputState::ShortPress) {
            printf("POWER\r\n");
            test_keys_pressed |= BUTTON_ID_POWER;
            return;
        }
    }},
    {BUTTON_ID_BT, [](Teufel::Ux::InputState event) {
        if (is_key_test_activated() && event == Teufel::Ux::InputState::ShortPress) {
            printf("BT\r\n");
            test_keys_pressed |= BUTTON_ID_BT;
            return;
        }
    }},
    {BUTTON_ID_PLAY, [](Teufel::Ux::InputState event) {
        if (is_key_test_activated() && event == Teufel::Ux::InputState::ShortPress) {
            printf("PLAY\r\n");
            test_keys_pressed |= BUTTON_ID_PLAY;
            return;
        }
    }},
    {BUTTON_ID_PLUS, [](Teufel::Ux::InputState event) {
        if (is_key_test_activated() && event ==Teufel:: Ux::InputState::ShortPress) {
            printf("VOL+\r\n");
            test_keys_pressed |= BUTTON_ID_PLUS;
            return;
        }
    }},
    {BUTTON_ID_MINUS, [](Teufel::Ux::InputState event) {
        if (is_key_test_activated() && event == Teufel::Ux::InputState::ShortPress) {
            printf("VOL-\r\n");
            test_keys_pressed |= BUTTON_ID_MINUS;
            return;
        }
    }},
)

const std::optional<button_event_handler_fn_t> get_handler_mapper(uint32_t button_state)
{
    return Teufel::Core::mapValue(EventHandlerProductionTestMapper, button_state);
}

#else

bool is_test_mode_activated()
{
    return false;
}

bool is_led_test_activated()
{
    return false;
}

bool is_key_test_activated()
{
    return false;
}

void factory_test_key_process() {}

#endif // INCLUDE_PRODUCTION_TESTS

namespace Teufel::Ux::System
{
#ifdef INCLUDE_PRODUCTION_TESTS
TS_GET_PROPERTY_NON_OPT_FN(, m_charge_limit_mode, ChargeLimitMode)
#endif // INCLUDE_PRODUCTION_TESTS
}