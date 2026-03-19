#include <cstdio>

#include "board.h"
#include "board_hw.h"
#include "board_link.h"
#include "bsp_debug_uart.h"
#ifdef MYND_RPI_MODIFICATION
#include "bsp_bluetooth_uart.h"
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "task_system.h"

#if defined(SEGGER_RTT)
#include "SEGGER_RTT.h"
#endif
#include "logger.h"
#include "external/teufel/libs/greeting/greeting.h"
#include "external/teufel/libs/app_assert/app_assert.h"

static void SystemClock_Config();

#define TASK_LOGGER_STACK_SIZE 64
static StaticTask_t logger_task_buffer;
static StackType_t  logger_task_stack[TASK_LOGGER_STACK_SIZE];

#define LOGGER_STORAGE_SIZE_BYTES 512
static uint8_t              logger_sbuffer_storage[LOGGER_STORAGE_SIZE_BYTES];
static StaticStreamBuffer_t LoggerStreamBufferStruct;

#if defined(__cplusplus)
extern "C"
{
#endif

    extern UART_HandleTypeDef UART2_Handle;

    int __io_putchar(int ch)
    {
        HAL_UART_Transmit(&UART2_Handle, (uint8_t *) &ch, 1, HAL_MAX_DELAY);
        return ch;
    }

    void vApplicationIdleHook(void)
    {
        // "Low-power mode"
        __WFI();
    }

    void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
    {
        (void) xTask;
        log_fatal("Stack overflow on task %s", pcTaskName);
        APP_ASSERT(false);
    }

    void vApplicationDaemonTaskStartupHook(void)
    {
        char buffer[24];

        board_init();
        bsp_debug_uart_init();

        snprintf(buffer, sizeof(buffer), "MYND (rev%d)", read_hw_revision());

        printf("\r\n\r\n\n");
        PrintFirmwareInfo(buffer);
    }

    /* GetIdleTaskMemory prototype (linked to static allocation support) */
    void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                       uint32_t *pulIdleTaskStackSize);

    static StaticTask_t xIdleTaskTCBBuffer;
    static StackType_t  xIdleStack[configMINIMAL_STACK_SIZE];

    void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                       uint32_t *pulIdleTaskStackSize)
    {
        *ppxIdleTaskTCBBuffer   = &xIdleTaskTCBBuffer;
        *ppxIdleTaskStackBuffer = &xIdleStack[0];
        *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
    }

    /* GetIdleTaskMemory prototype (linked to static allocation support) */
    void vApplicationGetTimerTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                        uint32_t *pulIdleTaskStackSize);

    static StaticTask_t xTimerTaskTCBBuffer;
    static StackType_t  xTimerStack[configTIMER_TASK_STACK_DEPTH];

    void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer,
                                        uint32_t *pulTimerTaskStackSize)
    {
        *ppxTimerTaskTCBBuffer   = &xTimerTaskTCBBuffer;
        *ppxTimerTaskStackBuffer = &xTimerStack[0];
        *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
    }

    uint32_t logger_get_timestamp()
    {
        return get_systick();
    }

#if defined(__cplusplus)
}
#endif

int main()
{
#if defined(BOOTLOADER)
    /* Enable the SYSCFG peripheral clock*/
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    /* Remap SRAM at 0x00000000 */
    __HAL_SYSCFG_REMAPMEMORY_SRAM();
#endif

    HAL_Init();
    SystemClock_Config();
    // Call this function as early as possible (right after the clock configuration is done).
    // Later we can call this function again and it returns the cached value.
    read_hw_revision();

#if defined(SEGGER_RTT)
    SEGGER_RTT_Init();
#endif

#ifdef LOGGER_USE_EXTERNAL_THREAD

    TaskHandle_t status = nullptr;
    status              = xTaskCreateStatic(
                     +[](void *)
                     {
            static auto sbuffer_logger_h = xStreamBufferCreateStatic(sizeof(logger_sbuffer_storage), 1u,
                                                                                  logger_sbuffer_storage, &LoggerStreamBufferStruct);

            logger_init(sbuffer_logger_h);

            for (;;)
            {
                if (uint8_t data; xStreamBufferReceive(sbuffer_logger_h, &data, 1, portMAX_DELAY) > 0)
                {
#if defined(SEGGER_RTT)
                    SEGGER_RTT_PutChar(0, data);
#else
                    __io_putchar(data);
#endif
                }
            }
        },
        "Logger", TASK_LOGGER_STACK_SIZE, nullptr, 2, logger_task_stack, &logger_task_buffer);
    APP_ASSERT(status);

#endif // LOGGER_USE_EXTERNAL_THREAD

    Teufel::Task::System::start();
    vTaskStartScheduler();

    /* Should never reach this point. */
    while (true)
        ;

    return 0;
}

static void SystemClock_Config()
{
    RCC_OscInitTypeDef       RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef       RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit     = {0};
    HAL_StatusTypeDef        hal_stat;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSI48State     = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_NONE;

    hal_stat = HAL_RCC_OscConfig(&RCC_OscInitStruct);
    APP_ASSERT(hal_stat == HAL_OK);

    /* Initializes the CPU, AHB and APB busses clocks */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI48;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

    hal_stat = HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1);
    APP_ASSERT(hal_stat == HAL_OK);

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_USART2 | RCC_PERIPHCLK_I2C1;
    PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
    PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    PeriphClkInit.I2c1ClockSelection   = RCC_I2C1CLKSOURCE_SYSCLK;

    hal_stat = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
    APP_ASSERT(hal_stat == HAL_OK);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    switch (GPIO_Pin)
    {
        case IO_EXP_INT_GPIO_PIN:
        {
            board_link_io_expander_on_interrupt();
            break;
        }
    }
}

#define USE_FULL_ASSERT
#ifdef USE_FULL_ASSERT

/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(char *file, uint32_t line)
{
    /* User can add his own implementation to report the file name and line number,
     * ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    log_fatal("Assert failed: %s:%d)\n", file, line);
    __BKPT(0);
    while (1)
    {
    }
}
#endif
