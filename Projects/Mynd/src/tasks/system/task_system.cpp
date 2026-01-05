// Due to the flash size limit, only the WARNING level is available
// for use in the complete firmware (including the bootloader).
#if defined(BOOTLOADER)
#define LOG_LEVEL LOG_LEVEL_WARNING
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "external/teufel/libs/GenericThread/GenericThread++.h"

#include "config.h"
#include "board.h"
#include "board_link.h"
#include "bsp_debug_uart.h"
#include "logger.h"

#include "task_audio.h"
#include "task_bluetooth.h"
#include "task_system.h"
#include "task_priorities.h"
#include "external/teufel/libs/property/property.h"
#include "external/teufel/libs/core_utils/overload.h"
#include "external/teufel/libs/core_utils/sync.h"
#include "external/teufel/libs/app_assert/app_assert.h"
#include "ux/system/system.h"
#include "persistent_storage/kvstorage.h"
#include "stm32f0xx_hal.h"

#include "external/teufel/libs/tshell/tshell.h"

#define TASK_SYSTEM_STACK_SIZE 384
#define QUEUE_SIZE             5

namespace Teufel::Task::System
{

namespace Tub = Teufel::Ux::Bluetooth;
namespace Tua = Teufel::Ux::Audio;
namespace Tus = Teufel::Ux::System;

static Tus::Task                                            ot_id        = Tus::Task::System;
static Teufel::GenericThread::GenericThread<SystemMessage> *task_handler = nullptr;

static PropertyNonOpt<decltype(Tus::OffTimer::value)> m_off_timer{"power off timer",
                                                                  CONFIG_MIN_IDLE_TIMEOUT_MINUTES,
                                                                  CONFIG_MAX_IDLE_TIMEOUT_MINUTES,
                                                                  1,
                                                                  CONFIG_STANDBY_TIMER_MINS_DEFAULT,
                                                                  CONFIG_STANDBY_TIMER_MINS_DEFAULT};
PROPERTY_SET(Tus::OffTimer, m_off_timer)

static PropertyNonOpt<decltype(Tus::OffTimerEnabled::value)> m_off_timer_enabled{
    "power off timer enabled", CONFIG_STANDBY_TIMER_MINS_DEFAULT > 0, CONFIG_STANDBY_TIMER_MINS_DEFAULT > 0};
PROPERTY_SET(Tus::OffTimerEnabled, m_off_timer_enabled)

static StaticTask_t system_task_buffer;
static StackType_t  system_task_stack[TASK_SYSTEM_STACK_SIZE];
/* The variable used to hold the queue's data structure. */
static StaticQueue_t queue_static;
static const size_t  queue_item_size = sizeof(GenericThread::QueueMessage<SystemMessage>);
static uint8_t       queue_static_buffer[QUEUE_SIZE * queue_item_size];

// We consider that the power state is "transitioning" when the system is first booting up
// This prevents other tasks from assuming that the system is in a normal stable state
// when in reality the system is still initializing
// The power state will be set to off once all tasks have finished initializing
static auto p_power_state =
    Power::GlobalPowerState<Tus::PowerState, Tus::PowerState::Transition>(Tus::PowerState::Transition);

typedef void *(*power_state_fn_t)(const Tus::PowerState &e, const Ux::System::PowerStateChangeReason &reason);

static power_state_fn_t power_state_on(const Tus::PowerState &p, const Ux::System::PowerStateChangeReason &reason = {});
static power_state_fn_t power_state_off(const Tus::PowerState                    &p,
                                        const Ux::System::PowerStateChangeReason &reason = {});

static bool power_on_after_update()
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    // Check for magic # written by bootloader on update
    uint32_t bkup_0 = RTC->BKP0R;
    bool     result = false;
    if (bkup_0 == 0xBEEFBEEF)
        result = true;
    RTC->BKP0R = 0;
    HAL_PWR_DisableBkUpAccess();
    return result;
}

static struct
{
    power_state_fn_t  power_state_fn            = reinterpret_cast<power_state_fn_t>(power_state_off);
    uint32_t          last_activity_timestamp   = 0;
    uint32_t          stream_inactive_timestamp = 0;
    SemaphoreHandle_t property_mutex            = nullptr;
} s_system;

static StaticSemaphore_t property_mutex_buffer;

static power_state_fn_t power_state_on(const Tus::PowerState &p, const Ux::System::PowerStateChangeReason &reason)
{
    switch (p)
    {
        case Tus::PowerState::Off:
        {
            log_highlight("Powering off (%s)", getDesc(reason));

            // MCU needs to explicitly exit active csb mode if charger is connected when unit is powered off
            // otherwise, the unit powers back on into CSB mode
            if (isPropertyOneOf(Tub::Status::CsbChainMaster, Tub::Status::ChainSlave))
            {
                Teufel::Task::Bluetooth::postMessage(
                    ot_id, Teufel::Ux::Bluetooth::StopPairingAndMultichain{Tub::MultichainExitReason::PowerOff});
                vTaskDelay(pdMS_TO_TICKS(800)); // BT module needs time (derived w/ testing) to handle request and play
                                                // CSB disconnected si, before power off si
            }

            auto set_power_msg =
                p_power_state.setTransition(Tus::PowerState::PreOff, reason, getDesc(Tus::PowerState::PreOff));

            // Tell the audio task to prepare for power off (start LED animations, etc.)
            Teufel::Task::Audio::postMessage(ot_id, set_power_msg);
            SyncPrimitive::await(Tus::Task::Audio, 6000, "power off prep done");

            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait once the battery low sound icon is played
            if (reason != Tus::PowerStateChangeReason::OffTimer)
            {
                Teufel::Task::Bluetooth::postMessage(
                    ot_id, Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_POWER_OFF,
                                                 ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, false});
            }

            // Tell the BT task to prepare for power off (play power off sound icon, etc.)
            // It should synchronize once the whole sound icon has played
            Teufel::Task::Bluetooth::postMessage(ot_id, set_power_msg);
            SyncPrimitive::await(Tus::Task::Bluetooth, 4000, "played off sound icon");

            set_power_msg = p_power_state.setTransition(Tus::PowerState::Off, reason, getDesc(Tus::PowerState::Off));

            // Once the power off sound icon has played, we can turn off the amps
            Teufel::Task::Audio::postMessage(ot_id, set_power_msg);
            SyncPrimitive::await(Tus::Task::Audio, 3000, "completed power off");

            // Once the amps are off, we can turn off the BT module
            Teufel::Task::Bluetooth::postMessage(ot_id, set_power_msg);
            SyncPrimitive::await(Tus::Task::Bluetooth, 5000, "completed power off");

            if (not isProperty(Tus::ChargerStatus::Active))
                board_link_charger_enable_low_power_mode(true);

            board_link_power_supply_hold_on(false);
            p_power_state.set(Tus::PowerState::Off, getDesc(Tus::PowerState::Off));

            return reinterpret_cast<power_state_fn_t>(power_state_off);
        }
        default:
            break;
    }

    return reinterpret_cast<power_state_fn_t>(power_state_on);
}

static power_state_fn_t power_state_off(const Tus::PowerState &p, const Ux::System::PowerStateChangeReason &reason)
{
    switch (p)
    {
        case Tus::PowerState::On:
        {
            log_highlight("Powering on");

            auto set_power_msg =
                p_power_state.setTransition(Tus::PowerState::PreOn, reason, getDesc(Tus::PowerState::PreOn));
            board_link_power_supply_hold_on(true);

            Teufel::Task::Audio::postMessage(ot_id, set_power_msg);
            SyncPrimitive::await(Tus::Task::Audio, 200, "enabled amps");

            // Bluetooth task does not need a PreOn state as of now, so we skip it

            set_power_msg = p_power_state.setTransition(Tus::PowerState::On, reason, getDesc(Tus::PowerState::On));

            Teufel::Task::Bluetooth::postMessage(ot_id, set_power_msg);
            SyncPrimitive::await(Tus::Task::Bluetooth, 4000, "enabled BT");

            // Delay enough for the BT module to power on and start I2S
            // The amps need the I2S BCLK to be stable before they can be configured
            // TODO: make it synchronous with BT ActionsReady
            // TODO: keep it until we clarify power on sequence with BT team
            // vTaskDelay(pdMS_TO_TICKS(1000));

            log_dbg("Assuming I2S active, configuring amps");
            Teufel::Task::Audio::postMessage(ot_id, set_power_msg);

            SyncPrimitive::await(Tus::Task::Audio, 3000, "completed power on");

            Teufel::Task::Bluetooth::postMessage(
                ot_id, Tua::RequestSoundIcon{ACTIONSLINK_SOUND_ICON_POWER_ON,
                                             ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY, false});

            p_power_state.set(Tus::PowerState::On, getDesc(Tus::PowerState::On));

            return reinterpret_cast<power_state_fn_t>(power_state_on);
        }
        default:
            break;
    }

    return reinterpret_cast<power_state_fn_t>(power_state_off);
}

const struct tshell_config tshell_conf = {
    .t_putchar = +[](char ch) -> int
    {
        putchar(ch);
        fflush(nullptr);
        return 0;
    },
};

void check_idle_timeout()
{
    auto is_streaming_active = getProperty<Tub::StreamingActive>().value;

    // Disable standby timer when the BT module in the DFU mode
    if (isProperty(Ux::Bluetooth::Status::DfuMode))
    {
        s_system.stream_inactive_timestamp = get_systick();
    }

    // If the stream just became inactive, record the current time
    if (!is_streaming_active && s_system.stream_inactive_timestamp == 0)
    {
        s_system.stream_inactive_timestamp = get_systick();
    }

    // If the stream is active, reset the stream_inactive_timestamp
    if (is_streaming_active)
    {
        s_system.stream_inactive_timestamp = 0;
    }

    if (isProperty(Tus::PowerState::On) && !is_streaming_active)
    {
        // If the stream is inactive and has been inactive for N seconds, and the standby timer has expired, power off
        // the system
        if (isProperty(Tus::OffTimerEnabled{true}) &&
            board_get_ms_since(s_system.last_activity_timestamp) >
                (getProperty<Tus::OffTimer>().value * CONFIG_IDLE_POWER_OFF_TIMEOUT_MS_FACTOR) &&
            board_get_ms_since(s_system.stream_inactive_timestamp) >
                (getProperty<Tus::OffTimer>().value * CONFIG_IDLE_POWER_OFF_TIMEOUT_MS_FACTOR))
        {
            s_system.power_state_fn = (power_state_fn_t) (*s_system.power_state_fn)(
                Tus::PowerState::Off, Tus::PowerStateChangeReason::OffTimer);
        }
    }
}

static const GenericThread::Config<SystemMessage> threadConfig = {
    .Name      = "System",
    .StackSize = TASK_SYSTEM_STACK_SIZE,
    .Priority  = TASK_SYSTEM_PRIORITY,
    .IdleMs    = 25,
    .Callback_Idle =
        []()
    {
        uint8_t uart_rx_data = 0;
        if (bsp_debug_uart_rx(&uart_rx_data, 1) == 0)
        {
            tshell_process_char(uart_rx_data);
        }

        check_idle_timeout();
    },
    .Callback_Init =
        []()
    {
        Storage::init();

        tshell_init(&tshell_conf, (char *) "MYND$ ");

        board_link_power_supply_init();

        // If the power button is pressed at this point in time then it's likely
        // that the system is being powered on due to the power button and not because of a USB connection
        // We do this as early as possible to prevent the system from reading the power button too late
        if (board_link_power_supply_button_is_pressed())
        {
            const auto tick = get_systick();
            if (tick < 500u)
                vTaskDelay(pdMS_TO_TICKS(500u - tick));
            // Check if button is still pressed in 500ms after system power on
            if (board_link_power_supply_button_is_pressed())
                board_link_power_supply_hold_on(true);
        }

        board_link_moisture_detection_init();

        Teufel::Task::Audio::start();
        SyncPrimitive::await(Tus::Task::Audio, 2000, "started");

        Teufel::Task::Bluetooth::start();
        SyncPrimitive::await(Tus::Task::Bluetooth, 2000, "started");

        // If the bootloader wrote the magic # to the RTC->BKP0R reg, then an update was performed and device must power
        // on or If the power supply is already held on that means that the speaker should be powered on because the
        // system task detected a power button press early during the boot up process
        if (power_on_after_update() || board_link_power_supply_is_held_on())
        {
            Task::System::postMessage(ot_id, Tus::SetPowerState{Tus::PowerState::On, Tus::PowerState::Off});
        }
        // If the power supply is not held on that means that we have power because of a USB connection,
        // so we start holding the power supply on until AC is lost
        else if (board_link_power_supply_is_ac_ok())
        {
            board_link_power_supply_hold_on(true);
        }

        // We consider that the power state is "transitioning" when the system is first booting up
        // This prevents other tasks from assuming that the system is in a normal stable state
        // when in reality the system is still initializing
        p_power_state.set(Tus::PowerState::Off, getDesc(Tus::PowerState::Off));
    },
    .QueueSize = QUEUE_SIZE,
    .Callback =
        [](uint8_t /*modid*/, SystemMessage msg)
    {
        std::visit(
            Teufel::Core::overload{
                [](const Tus::SetPowerState &p)
                {
                    s_system.power_state_fn = (power_state_fn_t) (*s_system.power_state_fn)(
                        p.to, p.reason.value_or(Tus::PowerStateChangeReason::UserRequest));
                },
                [](const Tus::UserActivity &) { s_system.last_activity_timestamp = get_systick(); },
                [](const Tus::OffTimer &p) { setProperty(p); },
                [](const Tus::OffTimerEnabled &p) { setProperty(p); },
                [](const Tus::FactoryResetRequest &)
                {
                    log_info("Initiating factory reset");

                    // Perform factory reset
                    setProperty(Tus::OffTimerEnabled{CONFIG_STANDBY_TIMER_MINS_DEFAULT > 0});
                    setProperty(Tus::OffTimer{CONFIG_STANDBY_TIMER_MINS_DEFAULT});
                    log_info("Power-off Timer: %d min", getProperty<Tus::OffTimer>().value);

                    Teufel::Task::Bluetooth::postMessage(ot_id, Tus::FactoryReset{});
                    Teufel::Task::Audio::postMessage(ot_id, Tus::FactoryReset{});
                    vTaskDelay(pdMS_TO_TICKS(1000)); // let factory reset finish before performing a hard reset
                    log_info("Factory reset complete");

                    Teufel::Task::Audio::postMessage(ot_id, Tus::HardReset{});
                },
            },
            msg);
    },
    .StackBuffer = system_task_stack,
    .StaticTask  = &system_task_buffer,
    .StaticQueue = &queue_static,
    .QueueBuffer = queue_static_buffer,
};

static IMutex p_mutex{
    .lock =
        []()
    {
        if (xSemaphoreTakeRecursive(s_system.property_mutex, pdMS_TO_TICKS(5000)) == pdFALSE)
        {
            char *task_name = pcTaskGetName(xSemaphoreGetMutexHolder(s_system.property_mutex));
            // log_fatal("Failed to lock property mutex from %s task - %s task is still holding it",
            //           pcTaskGetName(xTaskGetCurrentTaskHandle()), (task_name != nullptr) ? task_name : "Unknown");
            app_assertion_handler(LOG_MODULE_NAME, __LINE__);
        }
    },
    .unlock =
        []()
    {
        if (xSemaphoreGiveRecursive(s_system.property_mutex) == pdFALSE)
        {
            // log_fatal("Failed to unlock property mutex from %s task", pcTaskGetName(xTaskGetCurrentTaskHandle()));
            app_assertion_handler(LOG_MODULE_NAME, __LINE__);
        }
    },
};

int start()
{
    static_assert(sizeof(SystemMessage) <= 6, "Queue message size exceeded 6 bytes!");

    s_system.property_mutex = xSemaphoreCreateMutexStatic(&property_mutex_buffer);
    APP_ASSERT(s_system.property_mutex, "Mutex was NULL");

    PropertyMutex::mutex = &p_mutex;
    task_handler         = GenericThread::create(&threadConfig);
    APP_ASSERT(task_handler);

    return 0;
}

int postMessage(Tus::Task source_task, SystemMessage msg)
{
    return GenericThread::PostMsg(task_handler, static_cast<uint8_t>(source_task), msg);
}

#ifndef BOOTLOADER
SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_power,
    SHELL_CMD_NO_ARGS(off, "set power off",
                      []() { postMessage(ot_id, Ux::System::SetPowerState{Ux::System::PowerState::Off}); }),
    SHELL_CMD_NO_ARGS(on, "set power on",
                      []()
                      {
                          s_system.stream_inactive_timestamp = 0;
                          postMessage(ot_id, Ux::System::SetPowerState{Ux::System::PowerState::On});
                      }),
    SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_ARG_REGISTER(p, &sub_power, "power", NULL, 2, 0);
#endif

}

namespace Teufel::Ux::System
{

PowerState getProperty(PowerState *)
{
    return Teufel::Task::System::p_power_state.get();
}

TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::System, m_off_timer, OffTimer)
TS_GET_PROPERTY_NON_OPT_FN(Teufel::Task::System, m_off_timer_enabled, OffTimerEnabled)
}
