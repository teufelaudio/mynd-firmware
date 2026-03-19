#include "tas5825p.h"
#include "tasxxxx_volume_table.h"
#include <stddef.h>

#define LOG_MODULE_NAME "tas5825p.c"
#define LOG_LEVEL       LOG_LEVEL_INFO
#include "driver_logger.h"

#if defined(FreeRTOS) && defined(configSUPPORT_DYNAMIC_ALLOCATION) && (configSUPPORT_DYNAMIC_ALLOCATION == 1)
#include "FreeRTOS.h"
#else
#include <stdlib.h>
#endif

#define TAS5825P_REG_RESET_CTRL         (0x01)
#define TAS5825P_REG_DEVICE_CTRL_1      (0x02)
#define TAS5825P_REG_DEVICE_CTRL_2      (0x03)
#define TAS5825P_REG_I2C_PAGE_AUTO_INC  (0x0F)
#define TAS5825P_REG_SIG_CH_CTRL        (0x28)
#define TAS5825P_REG_CLOCK_DET_CTRL     (0x29)
#define TAS5825P_REG_SDOUT_SEL          (0x30)
#define TAS5825P_REG_I2S_CTRL           (0x31)
#define TAS5825P_REG_SAP_CTRL1          (0x33)
#define TAS5825P_REG_SAP_CTRL2          (0x34)
#define TAS5825P_REG_FS_MON             (0x37)
#define TAS5825P_REG_BCK_MON            (0x38)
#define TAS5825P_REG_CLKDET_STATUS      (0x39)
#define TAS5825P_REG_DSP_PGM_MODE       (0x40)
#define TAS5825P_REG_DSP_CTRL           (0x46)
#define TAS5825P_REG_DIG_VOL            (0x4C)
#define TAS5825P_REG_DIG_VOL_CTRL1      (0x4E)
#define TAS5825P_REG_DIG_VOL_CTRL2      (0x4F)
#define TAS5825P_REG_AUTO_MUTE_CTRL     (0x50)
#define TAS5825P_REG_AUTO_MUTE_TIME     (0x51)
#define TAS5825P_REG_ANA_CTRL           (0x53)
#define TAS5825P_REG_AGAIN              (0x54)
#define TAS5825P_REG_SPI_CLOCK          (0x55)
#define TAS5825P_REG_EEPROM_CTRL0       (0x56)
#define TAS5825P_REG_EEPROM_RD_CMD      (0x57)
#define TAS5825P_REG_EEPROM_ADDR_START0 (0x58)
#define TAS5825P_REG_EEPROM_ADDR_START1 (0x59)
#define TAS5825P_REG_EEPROM_ADDR_START2 (0x5A)
#define TAS5825P_REG_EEPROM_BOOT_STATUS (0x5B)
#define TAS5825P_REG_BQ_WR_CTRL1        (0x5C)
#define TAS5825P_REG_PVDD_ADC           (0x5E)
#define TAS5825P_REG_GPIO_CTRL          (0x60)
#define TAS5825P_REG_GPIO0_SEL          (0x61)
#define TAS5825P_REG_GPIO1_SEL          (0x62)
#define TAS5825P_REG_GPIO2_SEL          (0x63)
#define TAS5825P_REG_GPIO_INPUT_SEL     (0x64)
#define TAS5825P_REG_GPIO_OUT           (0x65)
#define TAS5825P_REG_GPIO_OUT_INV       (0x66)
#define TAS5825P_REG_DIE_ID             (0x67)
#define TAS5825P_REG_POWER_STATE        (0x68)
#define TAS5825P_REG_AUTOMUTE_STATE     (0x69)
#define TAS5825P_REG_PHASE_CTRL         (0x6A)
#define TAS5825P_REG_SS_CTRL0           (0x6B)
#define TAS5825P_REG_SS_CTRL1           (0x6C)
#define TAS5825P_REG_SS_CTRL2           (0x6D)
#define TAS5825P_REG_SS_CTRL3           (0x6E)
#define TAS5825P_REG_SS_CTRL4           (0x6F)
#define TAS5825P_REG_CHAN_FAULT         (0x70)
#define TAS5825P_REG_GLOBAL_FAULT1      (0x71)
#define TAS5825P_REG_GLOBAL_FAULT2      (0x72)
#define TAS5825P_REG_WARNING            (0x73)
#define TAS5825P_REG_PIN_CONTROL1       (0x74)
#define TAS5825P_REG_PIN_CONTROL2       (0x75)
#define TAS5825P_REG_MISC_CONTROL       (0x76)
#define TAS5825P_REG_CBC_CONTROL        (0x77)
#define TAS5825P_REG_FAULT_CLEAR        (0x78)

struct tas5825p_handler
{
    tas5825p_i2c_read_fn_t  i2c_read_fn;
    tas5825p_i2c_write_fn_t i2c_write_fn;
    tas5825p_delay_fn_t     delay_fn;
    uint8_t                 i2c_device_address;
};

static int           set_dsp_memory_to_book_and_page(const tas5825p_handler_t *h, uint8_t book, uint8_t page);
static int           tas5825p_read_register(const tas5825p_handler_t *h, uint8_t register_address, uint8_t *p_data);
static int           tas5825p_write_register(const tas5825p_handler_t *h, uint8_t register_address, uint8_t value);
static int           tas5825p_modify_register(const tas5825p_handler_t *h, uint8_t register_address, uint8_t bitmask,
                                              uint8_t value);
static tas5825p_fs_t tas5825p_decode_fs_code(uint8_t raw_fs_code);

tas5825p_handler_t *tas5825p_init(const tas5825p_config_t *p_config)
{
    if (p_config == NULL)
    {
        return NULL;
    }

    if (p_config->i2c_read_fn == NULL || p_config->i2c_write_fn == NULL || p_config->delay_fn == NULL)
    {
        return NULL;
    }

#if defined(FreeRTOS) && defined(configSUPPORT_DYNAMIC_ALLOCATION) && (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    struct tas5825p_handler *h = pvPortMalloc(sizeof(struct tas5825p_handler));
#else
    struct tas5825p_handler *h = malloc(sizeof(struct tas5825p_handler));
#endif

    if (h == NULL)
    {
        return NULL;
    }

    h->i2c_read_fn        = p_config->i2c_read_fn;
    h->i2c_write_fn       = p_config->i2c_write_fn;
    h->delay_fn           = p_config->delay_fn;
    h->i2c_device_address = p_config->i2c_device_address;
    return h;
}

int tas5825p_load_configuration(const tas5825p_handler_t *h, const tas5825p_cfg_reg_t *p_tasxxx_config,
                                uint32_t config_length)
{
    int i = 0;
    while (i < config_length)
    {
        switch (p_tasxxx_config[i].command)
        {
            case CFG_META_SWITCH:
                // Used in legacy applications, ignored here
                break;
            case CFG_META_DELAY:
                h->delay_fn(p_tasxxx_config[i].param);
                break;
            case CFG_META_BURST:
                // The write length is (param - 1) because the register address is included is part of the burst payload
                if (h->i2c_write_fn(h->i2c_device_address, p_tasxxx_config[i + 1].offset, &p_tasxxx_config[i + 1].value,
                                    p_tasxxx_config[i].param - 1) != 0)
                {
                    log_error("Failed to load configuration at register 0x%02X", p_tasxxx_config[i + 1].offset);
                    return -E_TAS5825P_IO;
                }
                i += (p_tasxxx_config[i].param / 2) + 1;
                break;
            default:
                if (tas5825p_write_register(h, p_tasxxx_config[i].command, p_tasxxx_config[i].param) != 0)
                {
                    log_error("Failed to load configuration at register 0x%02X", p_tasxxx_config[i].param);
                    return -E_TAS5825P_IO;
                }
                break;
        }
        i++;
    }
    return E_TAS5825P_OK;
}

int tas5825p_enable_dsp(const tas5825p_handler_t *h, bool enable)
{
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    uint8_t dis_dsp_bit = (enable ? 0 : (1 << 4));
    return tas5825p_modify_register(h, TAS5825P_REG_DEVICE_CTRL_2, 0x10, dis_dsp_bit);
}

int tas5825p_mute(const tas5825p_handler_t *h, bool enable)
{
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    uint8_t mute_bit = (enable ? (1 << 3) : 0);
    return tas5825p_modify_register(h, TAS5825P_REG_DEVICE_CTRL_2, 0x08, mute_bit);
}

int tas5825p_set_state(const tas5825p_handler_t *h, tas5825p_device_state_t state)
{
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    uint8_t ctrl_state_bits = (uint8_t) state;
    return tas5825p_modify_register(h, TAS5825P_REG_DEVICE_CTRL_2, 0x03, ctrl_state_bits);
}

int tas5825p_enable_eq(const tas5825p_handler_t *h, bool enable)
{
    if (set_dsp_memory_to_book_and_page(h, 0x8C, 0x0B) != 0)
    {
        return -E_TAS5825P_IO;
    }

    uint8_t data[4] = {0};
    data[3]         = (enable ? 0x00 : 0x01);
    if (h->i2c_write_fn(h->i2c_device_address, 0x2C, data, sizeof(data)) != 0)
    {
        return -E_TAS5825P_IO;
    }

    return E_TAS5825P_OK;
}

int tas5825p_set_volume(const tas5825p_handler_t *h, int8_t volume_db)
{
    // Book, page and registers were defined by EE by checking the I2C traffic
    // of the PPC3 tool
    if (set_dsp_memory_to_book_and_page(h, 0x8C, 0x0B) != 0)
    {
        return -E_TAS5825P_IO;
    }

    const uint8_t *p_volume_data = tasxxxx_volume_get_data_for_db(volume_db);

    if (h->i2c_write_fn(h->i2c_device_address, 0x0C, p_volume_data, 4) != 0)
    {
        return -E_TAS5825P_IO;
    }

    if (h->i2c_write_fn(h->i2c_device_address, 0x10, p_volume_data, 4) != 0)
    {
        return -E_TAS5825P_IO;
    }

    return E_TAS5825P_OK;
}

int tas5825p_set_gpio_mode(const tas5825p_handler_t *h, tas5825p_gpio_t gpio, tas5825p_gpio_mode_t mode)
{
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    uint8_t register_address = TAS5825P_REG_GPIO0_SEL + (uint8_t) gpio;
    return tas5825p_write_register(h, register_address, (uint8_t) mode);
}

int tas5825p_set_gpio_output_level(const tas5825p_handler_t *h, tas5825p_gpio_t gpio, bool high)
{
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    uint8_t bitmask = (1 << (uint8_t) gpio);
    uint8_t value   = (high ? bitmask : 0);
    return tas5825p_modify_register(h, TAS5825P_REG_GPIO_OUT, bitmask, value);
}

int tas5825p_clear_analog_fault(const tas5825p_handler_t *h)
{
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    return tas5825p_write_register(h, TAS5825P_REG_FAULT_CLEAR, 0x80);
}

int tas5825p_recover_dc_fake_fault(const tas5825p_handler_t *h)
{
    const uint8_t command_seq[][2] = {
        // clang-format off
        {0x00, 0x00},
        {0x7F, 0x00},
        {0x7E, 0xFF},
        {0x7D, 0x11},
        {0x00, 0x02},
        {0x16, 0x08},
        {0x00, 0x00},
        {0x7F, 0x00},
        {0x00, 0x00},
        {0x7E, 0x52},
        {0x7D, 0x00},
        // clang-format on
    };
    const uint8_t command_seq_len = sizeof(command_seq) / sizeof(command_seq[0]);

    for (uint8_t i = 0; i < command_seq_len; i++)
    {
        if (tas5825p_write_register(h, command_seq[i][0], command_seq[i][1]) != E_TAS5825P_OK)
        {
            return -E_TAS5825P_IO;
        }
    }

    return E_TAS5825P_OK;
}

static int tas5825p_read_register(const tas5825p_handler_t *h, uint8_t register_address, uint8_t *p_data)
{
    return (h->i2c_read_fn(h->i2c_device_address, register_address, p_data, 1) == 0) ? E_TAS5825P_OK : -E_TAS5825P_IO;
}

static int tas5825p_write_register(const tas5825p_handler_t *h, uint8_t register_address, uint8_t value)
{
    return (h->i2c_write_fn(h->i2c_device_address, register_address, &value, 1) == 0) ? E_TAS5825P_OK : -E_TAS5825P_IO;
}

static int tas5825p_modify_register(const tas5825p_handler_t *h, uint8_t register_address, uint8_t bitmask,
                                    uint8_t value)
{
    // Assert that the value does not write outside the provided bitmask
    if ((~bitmask & value) != 0)
    {
        return -E_TAS5825P_PARAM;
    }

    uint8_t data;
    if (tas5825p_read_register(h, register_address, &data) != 0)
    {
        return -E_TAS5825P_IO;
    }

    // Clear the bits to modify
    data &= ~(bitmask);

    // Set them to the new value
    data |= value;

    return tas5825p_write_register(h, register_address, data);
}

static int set_dsp_memory_to_book_and_page(const tas5825p_handler_t *h, uint8_t book, uint8_t page)
{
    // Register 0x00 of every page is used to change the page of the memory book
    if (tas5825p_write_register(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }

    // Register 0x7F of page 0x00 of every book is used to change the book
    // Change the book here
    if (tas5825p_write_register(h, 0x7F, book) != 0)
    {
        return -E_TAS5825P_IO;
    }

    // Now that we are in the right book, change to the wished page
    return tas5825p_write_register(h, 0x00, page);
}

int tas5825p_read_fs_mon(const tas5825p_handler_t *h, tas5825p_fs_t *p_fs)
{
    if (p_fs == NULL)
    {
        return -E_TAS5825P_PARAM;
    }
    if (set_dsp_memory_to_book_and_page(h, 0x00, 0x00) != 0)
    {
        return -E_TAS5825P_IO;
    }
    uint8_t raw_value;
    int     ret = tas5825p_read_register(h, TAS5825P_REG_FS_MON, &raw_value);
    if (ret != 0)
    {
        return ret;
    }

    *p_fs = tas5825p_decode_fs_code(raw_value & 0x0F);
    return 0;
}

static tas5825p_fs_t tas5825p_decode_fs_code(uint8_t raw_fs_code)
{
    switch (raw_fs_code)
    {
        case TAS5825P_FS_ERROR:
        case TAS5825P_FS_16KHZ:
        case TAS5825P_FS_32KHZ:
        case TAS5825P_FS_48KHZ:
        case TAS5825P_FS_96KHZ:
        case TAS5825P_FS_192KHZ:
            return (tas5825p_fs_t) raw_fs_code;
        default:
            // Map unknown and reserved FS codes to error
            return TAS5825P_FS_ERROR;
    }
}