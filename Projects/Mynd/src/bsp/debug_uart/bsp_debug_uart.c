#include "bsp_debug_uart.h"
#include "board_hw.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.h"
#include "stm32f0xx_hal.h"
#include "logger.h"
#include <stdbool.h>

UART_HandleTypeDef          UART2_Handle;
static StreamBufferHandle_t sbuffer_handle_rx;
static uint8_t              irq_rx_data[1] = {};
static volatile bool        missed_rx_data = false;

#define STORAGE_SIZE_BYTES 32
static uint8_t              sbuffer_storage[STORAGE_SIZE_BYTES];
static StaticStreamBuffer_t StreamBufferStruct;

void bsp_debug_uart_init(void)
{
    UART2_Handle.Instance                    = DEBUG_UART;
    UART2_Handle.Init.BaudRate               = DEBUG_UART_BAUDRATE;
    UART2_Handle.Init.WordLength             = UART_WORDLENGTH_8B;
    UART2_Handle.Init.StopBits               = UART_STOPBITS_1;
    UART2_Handle.Init.Parity                 = UART_PARITY_NONE;
    UART2_Handle.Init.Mode                   = UART_MODE_TX_RX;
    UART2_Handle.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    UART2_Handle.Init.OverSampling           = UART_OVERSAMPLING_16;
    UART2_Handle.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    UART2_Handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    HAL_UART_Init(&UART2_Handle);

    sbuffer_handle_rx = xStreamBufferCreateStatic(sizeof(sbuffer_storage), 0u, sbuffer_storage, &StreamBufferStruct);
    if (sbuffer_handle_rx == NULL)
    {
        log_err("Failed to create stream buffer");
    }

    // Adding this line fixes the issue of Prod Test Mode needing AZ
    // to be entered 3 times
    // TODO: Investigate this issue further, this line is a temporary
    // workaround but is a duplicate, we should not need to call it here
    HAL_UART_Receive_IT(&UART2_Handle, (uint8_t *) irq_rx_data, 1);
}

void bsp_debug_uart_msp_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* Gpio clock enable */
    DEBUG_UART_TX_GPIO_CLK_ENABLE();
    DEBUG_UART_RX_GPIO_CLK_ENABLE();

    /* USART RX/TX GPIO Configuration */
    GPIO_InitStruct.Pin       = DEBUG_UART_TX_GPIO_PIN;
    GPIO_InitStruct.Mode      = DEBUG_UART_TX_GPIO_MODE;
    GPIO_InitStruct.Pull      = DEBUG_UART_TX_GPIO_PULL;
    GPIO_InitStruct.Speed     = DEBUG_UART_TX_GPIO_SPEED;
    GPIO_InitStruct.Alternate = DEBUG_UART_TX_GPIO_AF;
    HAL_GPIO_Init(DEBUG_UART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = DEBUG_UART_RX_GPIO_PIN;
    GPIO_InitStruct.Mode      = DEBUG_UART_RX_GPIO_MODE;
    GPIO_InitStruct.Pull      = DEBUG_UART_RX_GPIO_PULL;
    GPIO_InitStruct.Speed     = DEBUG_UART_RX_GPIO_SPEED;
    GPIO_InitStruct.Alternate = DEBUG_UART_RX_GPIO_AF;
    HAL_GPIO_Init(DEBUG_UART_RX_GPIO_PORT, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(DEBUG_UART_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DEBUG_UART_IRQn);

    DEBUG_UART_CLK_ENABLE();
}

int bsp_debug_uart_tx(const uint8_t *p_data, size_t length)
{
    HAL_UART_Transmit(&UART2_Handle, (uint8_t *) p_data, length, HAL_MAX_DELAY);
    return 0;
}

int bsp_debug_uart_rx(uint8_t *p_data, size_t length)
{
    if (xStreamBufferBytesAvailable(sbuffer_handle_rx) < length)
    {
        return -1;
    }

    if (missed_rx_data)
    {
        missed_rx_data = false;
        log_error("Lost RX data");
    }

    size_t bytes_received = xStreamBufferReceive(sbuffer_handle_rx, (void *) p_data, length, 0);
    if (bytes_received != length)
    {
        log_error("UART RX failed: expected %d, received %d", length, bytes_received);
        return -1;
    }

    return 0;
}

void bsp_debug_uart_isr_rx_complete_callback(void)
{
    if (xStreamBufferIsFull(sbuffer_handle_rx) == pdFALSE)
    {
        if (xStreamBufferSendFromISR(sbuffer_handle_rx, (uint8_t *) irq_rx_data, (size_t) 1, (BaseType_t *) pdFALSE) !=
            1)
        {
            missed_rx_data = true;
        }
    }
    else
    {
        missed_rx_data = true;
    }

    // Start receiving
    HAL_UART_Receive_IT(&UART2_Handle, (uint8_t *) irq_rx_data, 1);
}
