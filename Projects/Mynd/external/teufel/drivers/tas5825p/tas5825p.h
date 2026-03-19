#pragma once

#include <stdint.h>
#include <stdbool.h>

#define E_TAS5825P_OK    0
#define E_TAS5825P_IO    1 // I/O operation failed
#define E_TAS5825P_PARAM 2 // Invalid param

typedef unsigned char cfg_u8;
typedef union
{
    struct
    {
        cfg_u8 offset;
        cfg_u8 value;
    };
    struct
    {
        cfg_u8 command;
        cfg_u8 param;
    };
} tas5825p_cfg_reg_t;

#define CFG_META_SWITCH (255)
#define CFG_META_DELAY  (254)
#define CFG_META_BURST  (253)

typedef int (*tas5825p_i2c_read_fn_t)(uint8_t i2c_address, uint8_t register_address, uint8_t *p_data, uint32_t length);
typedef int (*tas5825p_i2c_write_fn_t)(uint8_t i2c_address, uint8_t register_address, const uint8_t *p_data,
                                       uint32_t length);
typedef void (*tas5825p_delay_fn_t)(uint32_t ms);

typedef struct tas5825p_handler tas5825p_handler_t;

typedef struct
{
    tas5825p_i2c_read_fn_t  i2c_read_fn;
    tas5825p_i2c_write_fn_t i2c_write_fn;
    tas5825p_delay_fn_t     delay_fn;
    uint8_t                 i2c_device_address;
} tas5825p_config_t;

typedef enum
{
    TAS5825P_DEVICE_STATE_DEEP_SLEEP,
    TAS5825P_DEVICE_STATE_SLEEP,
    TAS5825P_DEVICE_STATE_HI_Z,
    TAS5825P_DEVICE_STATE_PLAY,
} tas5825p_device_state_t;

typedef enum
{
    TAS5825P_GPIO0,
    TAS5825P_GPIO1,
    TAS5825P_GPIO2,
} tas5825p_gpio_t;

typedef enum
{
    TAS5825P_GPIO_MODE_OFF,
    TAS5825P_GPIO_MODE_HYBRID_PRO_CLASS_G_WAVEFORM_CONTROL_OUTPUT,
    TAS5825P_GPIO_MODE_OUTPUT,
    TAS5825P_GPIO_MODE_AUTO_MUTE_FLAG_LR,
    TAS5825P_GPIO_MODE_AUTO_MUTE_FLAG_L,
    TAS5825P_GPIO_MODE_AUTO_MUTE_FLAG_R,
    TAS5825P_GPIO_MODE_CLOCK_INVALID_FLAG,
    TAS5825P_GPIO_MODE_RESERVED_0,
    TAS5825P_GPIO_MODE_WARNZ_OUTPUT,
    TAS5825P_GPIO_MODE_HYBRID_PRO_CLASS_H_WAVEFORM_CONTROL_OUTPUT,
    TAS5825P_GPIO_MODE_FAULTZ_OUTPUT,
    TAS5825P_GPIO_MODE_SPI_CLK,
    TAS5825P_GPIO_MODE_SPI_MOSI,
    TAS5825P_GPIO_MODE_RESERVED1,
    TAS5825P_GPIO_MODE_RESERVED2,
} tas5825p_gpio_mode_t;

/**
 * @brief Audio sampling rate values from FS_MON register (bits 3-0)
 */
typedef enum
{
    TAS5825P_FS_ERROR      = 0x0, ///< 0000: FS Error
    TAS5825P_FS_16KHZ      = 0x4, ///< 0100: 16 KHz
    TAS5825P_FS_32KHZ      = 0x6, ///< 0110: 32 KHz
    TAS5825P_FS_RESERVED_8 = 0x8, ///< 1000: Reserved
    TAS5825P_FS_48KHZ      = 0x9, ///< 1001: 48 KHz
    TAS5825P_FS_96KHZ      = 0xB, ///< 1011: 96 KHz
    TAS5825P_FS_192KHZ     = 0xD, ///< 1101: 192 KHz
} tas5825p_fs_t;

/**
 * @brief Initializes the TAS5825P driver.
 *
 * @param[in] p_config          pointer to configuration structure
 *
 * @return pointer to handler if successful, NULL otherwise
 */
tas5825p_handler_t *tas5825p_init(const tas5825p_config_t *p_config);

/**
 * @brief Loads the configuration for the TAS5825P amplifier.
 *
 * @param[in] h                     pointer to handler
 * @param[in] p_tasxxx_config       pointer to configuration array
 * @param[in] config_length         length of configuration array
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_load_configuration(const tas5825p_handler_t *h, const tas5825p_cfg_reg_t *p_tasxxx_config,
                                uint32_t config_length);

/**
 * @brief Enables/disables the DSP in the amplifier.
 *
 * @details The DSP needs to be enabled only after  all input clocks are
 *          settled so that DMA channels do not go out of sync.
 *
 * @param[in] h             pointer to handler
 * @param[in] enable        enable option
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_enable_dsp(const tas5825p_handler_t *h, bool enable);

/**
 * @brief Enables/disables the mute control for both left and right channels.
 *
 * @param[in] h             pointer to handler
 * @param[in] enable        enable option
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_mute(const tas5825p_handler_t *h, bool enable);

/**
 * @brief Sets the device to the given state.
 *
 * @param[in] h             pointer to handler
 * @param[in] state         device state
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_set_state(const tas5825p_handler_t *h, tas5825p_device_state_t state);

/**
 * @brief Enables/disables the EQ.
 *
 * @param[in] h             pointer to handler
 * @param[in] enable        enable option
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_enable_eq(const tas5825p_handler_t *h, bool enable);

/**
 * @brief Sets the digital volume control for both left and right channels.
 *
 * @details The volume range goes from -90 dB to +10 dB.
 *          Anything less than -90 dB gets written as -infinite dB.
 *
 * @param[in] h             pointer to handler
 * @param[in] volume_db     volume in dB
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_set_volume(const tas5825p_handler_t *h, int8_t volume_db);

/**
 * @brief Sets the mode of a given GPIO.
 *
 * @param[in] h             pointer to handler
 * @param[in] gpio          GPIO to set
 * @param[in] mode          mode to set
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_set_gpio_mode(const tas5825p_handler_t *h, tas5825p_gpio_t gpio, tas5825p_gpio_mode_t mode);

/**
 * @brief Sets the output level of a given GPIO.
 *
 * @param[in] h             pointer to handler
 * @param[in] gpio          GPIO to set
 * @param[in] high          high/low output
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_set_gpio_output_level(const tas5825p_handler_t *h, tas5825p_gpio_t gpio, bool high);

/**
 * @brief Clears any analog faults in the device.
 *
 * @param[in] h             pointer to handler
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_clear_analog_fault(const tas5825p_handler_t *h);

/**
 *
 * @brief Recover from fake DC protection fault.
 *
 * @details Use an "official" workaround provided by TI
 *
 * @param[in] h             pointer to handler
 *
 * @return 0 if successful, error code otherwise
 */
int tas5825p_recover_dc_fake_fault(const tas5825p_handler_t *h);

/**
 * @brief Read FS_MON register (0x37) and extract sampling rate.
 *
 * @param[in]  h         pointer to handler
 * @param[out] p_fs      pointer to detected word-select (frame) sampling rate (extracted from bits 3-0)
 *
 * @return 0 if successful, error code otherwise
 *
 * @details Table 9-18. FS_MON Register Field Descriptions
 * Bit  Field            Type  Reset  Description
 * 7-6  RESERVED         R/W   00     This bit is reserved
 * 5-4  SCLK_RATIO_HIGH  R     00     2 msbs of detected SCLK ratio
 * 3-0  FS               R     0000   These bits indicate the currently detected audio sampling rate.
 *                                    0000: FS Error
 *                                    0100: 16 KHz
 *                                    0110: 32 KHz
 *                                    1000: Reserved
 *                                    1001: 48 KHz
 *                                    1011: 96 KHz
 *                                    1101: 192 KHz
 *                                    Others: Reserved
 */
int tas5825p_read_fs_mon(const tas5825p_handler_t *h, tas5825p_fs_t *p_fs);
