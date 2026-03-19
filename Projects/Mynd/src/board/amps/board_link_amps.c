#define LOG_LEVEL LOG_LEVEL_ERROR
#include "logger.h"

#include "config.h"
#include "board_link_amps.h"
#include "board_hw.h"
#include "bsp_shared_i2c.h"
#include "tas5805m.h"
#include "tas5825p.h"
#include "FreeRTOS.h"
#include "task.h"

#include "eco_5805_config.h"
#include "eco_5805_treble_config.h"
#include "eco_5825_config.h"
#include "eco_5825_bass_config.h"
#include "eco_5805_eco_mode_config.h"
#include "eco_5825_eco_mode_config.h"
#include "eco_5805_patch_to_bypass_mode.h"
#include "eco_5825_patch_to_bypass_mode.h"

static void thread_sleep_ms(uint32_t ms);

static const tas5805m_config_t tas5805m_config = {
    .i2c_read_fn        = bsp_shared_i2c_read,
    .i2c_write_fn       = bsp_shared_i2c_write,
    .delay_fn           = thread_sleep_ms,
    .i2c_device_address = TAS5805M_I2C_ADDRESS,
};

static const tas5825p_config_t tas5825p_config = {
    .i2c_read_fn        = bsp_shared_i2c_read,
    .i2c_write_fn       = bsp_shared_i2c_write,
    .delay_fn           = thread_sleep_ms,
    .i2c_device_address = TAS5825P_I2C_ADDRESS,
};

static struct
{
    tas5805m_handler_t *tas5805m;
    tas5825p_handler_t *tas5825p;
    int8_t              tweeter_volume_db;
    int8_t              woofer_volume_db;
    bool                is_muted;
} s_amps;

void board_link_amps_init(void)
{
    int ret;

    GPIO_InitTypeDef GPIO_InitStruct;

    // Configure amps power down pin
    AMPS_POWER_DOWN_GPIO_CLK_ENABLE();
    board_link_amps_enable(false);
    GPIO_InitStruct.Pin   = AMPS_POWER_DOWN_GPIO_PIN;
    GPIO_InitStruct.Mode  = AMPS_POWER_DOWN_GPIO_MODE;
    GPIO_InitStruct.Pull  = AMPS_POWER_DOWN_GPIO_PULL;
    GPIO_InitStruct.Speed = AMPS_POWER_DOWN_GPIO_SPEED;
    HAL_GPIO_Init(AMPS_POWER_DOWN_GPIO_PORT, &GPIO_InitStruct);

    // Configure tweeter amp fault interrupt pin
    TWEETER_FAULT_INT_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin   = TWEETER_FAULT_INT_GPIO_PIN;
    GPIO_InitStruct.Mode  = TWEETER_FAULT_INT_GPIO_MODE;
    GPIO_InitStruct.Pull  = TWEETER_FAULT_INT_GPIO_PULL;
    GPIO_InitStruct.Speed = TWEETER_FAULT_INT_GPIO_SPEED;
    HAL_GPIO_Init(TWEETER_FAULT_INT_GPIO_PORT, &GPIO_InitStruct);

    // Configure woofer amp fault interrupt pin
    WOOFER_FAULT_INT_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin   = WOOFER_FAULT_INT_GPIO_PIN;
    GPIO_InitStruct.Mode  = WOOFER_FAULT_INT_GPIO_MODE;
    GPIO_InitStruct.Pull  = WOOFER_FAULT_INT_GPIO_PULL;
    GPIO_InitStruct.Speed = WOOFER_FAULT_INT_GPIO_SPEED;
    HAL_GPIO_Init(WOOFER_FAULT_INT_GPIO_PORT, &GPIO_InitStruct);

    s_amps.tas5805m = tas5805m_init(&tas5805m_config);
    if (s_amps.tas5805m == NULL)
    {
        log_error("Failed to initialize tas5805m");
    }

    s_amps.tas5825p = tas5825p_init(&tas5825p_config);
    if (s_amps.tas5825p == NULL)
    {
        log_error("Failed to initialize tas5825p");
    }
}

void board_link_amps_enable(bool enable)
{
    HAL_GPIO_WritePin(AMPS_POWER_DOWN_GPIO_PORT, AMPS_POWER_DOWN_GPIO_PIN, (enable) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    log_info("Amps power %s", enable ? "enabled" : "disabled");
}

int board_link_amps_set_hi_z(void)
{
    int result = 0;

    if (tas5825p_set_state(s_amps.tas5825p, TAS5825P_DEVICE_STATE_HI_Z) != 0)
    {
        log_error("Failed to set woofer amp to Hi-Z state");
        result = -1;
    }

    if (tas5805m_set_state(s_amps.tas5805m, TAS5805M_DEVICE_STATE_HI_Z) != 0)
    {
        log_error("Failed to set tweeter amp to Hi-Z state");
        result = -1;
    }

    return result;
}

int board_link_amps_setup_woofer(board_link_amps_mode_t mode)
{
    int result = 0;

    if (tas5825p_set_state(s_amps.tas5825p, TAS5825P_DEVICE_STATE_HI_Z) != 0)
    {
        log_error("Failed to set woofer amp to Hi-Z state");
        result = -1;
    }

    if (tas5825p_enable_dsp(s_amps.tas5825p, true) != 0)
    {
        log_error("Failed to enable DSP on woofer amp");
        result = -1;
    }

    // Datasheet specifies that we need to wait at least 5 ms to allow the device to settle down after enabling the DSP
    thread_sleep_ms(10);

    if (tas5825p_load_configuration(s_amps.tas5825p, tas5825p_config_registers, TAS5825P_CONFIG_REGISTERS_SIZE) == 0)
    {
        log_info("Woofer amp configuration loaded");
    }
    else
    {
        log_error("Failed to load configuration on woofer amp");
        result = -1;
    }

    if (mode == AMP_MODE_BYPASS)
    {
        if (tas5825p_load_configuration(s_amps.tas5825p, tas5825p_bypass_config_registers,
                                        TAS5825P_BYPASS_CONFIG_REGISTERS_SIZE) == 0)
        {
            log_info("Woofer amp bypass configuration loaded");
        }
        else
        {
            log_error("Failed to load bypass configuration on woofer amp");
            result = -1;
        }

        if (tas5825p_enable_eq(s_amps.tas5825p, false) != 0)
        {
            log_error("Failed to disable EQ on woofer amp");
            result = -1;
        }

        board_link_amps_set_envelope_tracking_mode(AMP_ENVELOPE_TRACKING_MODE_OFF_MAX_PVDD);
    }

    if (tas5825p_set_state(s_amps.tas5825p, TAS5825P_DEVICE_STATE_PLAY) != 0)
    {
        log_error("Failed to set woofer amp to Play state");
        result = -1;
    }

    return result;
}

int board_link_amps_setup_tweeter(board_link_amps_mode_t mode)
{
    int result = 0;
    if (tas5805m_set_state(s_amps.tas5805m, TAS5805M_DEVICE_STATE_HI_Z) != 0)
    {
        log_error("Failed to set tweeter amp to Hi-Z state");
        result = -1;
    }

    if (tas5805m_enable_dsp(s_amps.tas5805m, true) != 0)
    {
        log_error("Failed to enable DSP on tweeter amp");
        result = -1;
    }

    // Datasheet specifies that we need to wait at least 5 ms to allow the device to settle down after enabling the DSP
    thread_sleep_ms(10);

    if (tas5805m_load_configuration(s_amps.tas5805m, tas5805m_config_registers, TAS5805M_CONFIG_REGISTERS_SIZE) == 0)
    {
        log_info("Tweeter amp configuration loaded");
    }
    else
    {
        log_error("Failed to load configuration on tweeter amp");
        result = -1;
    }

    if (mode == AMP_MODE_BYPASS)
    {
        if (tas5805m_load_configuration(s_amps.tas5805m, tas5805m_bypass_config_registers,
                                        TAS5805M_BYPASS_CONFIG_REGISTERS_SIZE) == 0)
        {
            log_info("Tweeter amp bypass configuration loaded");
        }
        else
        {
            log_error("Failed to load bypass configuration on tweeter amp");
            result = -1;
        }

        if (tas5805m_enable_eq(s_amps.tas5805m, false) != 0)
        {
            log_error("Failed to disable EQ on tweeter amp");
            result = -1;
        }

        if (tas5805m_enable_drc(s_amps.tas5805m, false) != 0)
        {
            log_error("Failed to disable DRC on tweeter amp");
            result = -1;
        }
    }

    if (tas5805m_set_state(s_amps.tas5805m, TAS5805M_DEVICE_STATE_PLAY) != 0)
    {
        log_error("Failed to set tweeter amp to Play state");
        result = -1;
    }

    return result;
}

void board_link_amps_enable_eco_mode(bool enable)
{
    int result = 0;
    if (enable)
    {
        result += tas5825p_load_configuration(s_amps.tas5825p, tas5825p_normal_to_eco_mode_config_1,
                                              TAS5825P_NORMAL_TO_ECO_CONFIG1_REGISTERS_SIZE);
        result += tas5825p_load_configuration(s_amps.tas5825p, tas5825p_normal_to_eco_mode_config_2,
                                              TAS5825P_NORMAL_TO_ECO_CONFIG2_REGISTERS_SIZE);
        result += tas5825p_load_configuration(s_amps.tas5825p, tas5825p_normal_to_eco_mode_config_3,
                                              TAS5825P_NORMAL_TO_ECO_CONFIG3_REGISTERS_SIZE);

        if (result == 0)
        {
            log_info("Woofer amp EcoMode configuration loaded");
        }
        else
        {
            log_error("Failed to load EcoMode configuration on woofer amp");
        }

        result += tas5805m_load_configuration(s_amps.tas5805m, tas5805m_normal_to_eco_mode_config_1,
                                              TAS5805M_NORMAL_TO_ECO_MODE_CONFIG1_REGISTERS_SIZE);
        result += tas5805m_load_configuration(s_amps.tas5805m, tas5805m_normal_to_eco_mode_config_2,
                                              TAS5805M_NORMAL_TO_ECO_MODE_CONFIG2_REGISTERS_SIZE);

        if (result == 0)
        {
            log_info("Tweeter amp EcoMode configuration loaded");
        }
        else
        {
            log_error("Failed to load EcoMode configuration on tweeter amp");
        }
    }
    else
    {
        result += tas5825p_load_configuration(s_amps.tas5825p, tas5825p_eco_to_normal_mode_config_1,
                                              TAS5825P_ECO_TO_NORMAL_CONFIG1_REGISTERS_SIZE);
        result += tas5825p_load_configuration(s_amps.tas5825p, tas5825p_eco_to_normal_mode_config_2,
                                              TAS5825P_ECO_TO_NORMAL_CONFIG2_REGISTERS_SIZE);
        result += tas5825p_load_configuration(s_amps.tas5825p, tas5825p_eco_to_normal_mode_config_3,
                                              TAS5825P_ECO_TO_NORMAL_CONFIG3_REGISTERS_SIZE);

        if (result == 0)
        {
            log_info("Woofer amp non-EcoMode configuration loaded");
        }
        else
        {
            log_error("Failed to load non-EcoMode configuration on woofer amp");
        }

        result += tas5805m_load_configuration(s_amps.tas5805m, tas5805m_eco_to_normal_mode_config_1,
                                              TAS5805M_ECO_TO_NORMAL_MODE_CONFIG1_REGISTERS_SIZE);
        result += tas5805m_load_configuration(s_amps.tas5805m, tas5805m_eco_to_normal_mode_config_2,
                                              TAS5805M_ECO_TO_NORMAL_MODE_CONFIG2_REGISTERS_SIZE);

        if (result == 0)
        {
            log_info("Tweeter amp non-EcoMode configuration loaded");
        }
        else
        {
            log_error("Failed to load non-EcoMode configuration on tweeter amp");
        }
    }
}

void board_link_amps_set_envelope_tracking_mode(board_link_amps_envelope_tracking_mode_t mode)
{
    int result = 0;
    switch (mode)
    {
        case AMP_ENVELOPE_TRACKING_MODE_OFF_MIN_PVDD:
            result += tas5825p_set_gpio_output_level(s_amps.tas5825p, TAS5825P_GPIO1, true);
            result += tas5825p_set_gpio_mode(s_amps.tas5825p, TAS5825P_GPIO1, TAS5825P_GPIO_MODE_OUTPUT);
            break;
        case AMP_ENVELOPE_TRACKING_MODE_OFF_MAX_PVDD:
            result += tas5825p_set_gpio_output_level(s_amps.tas5825p, TAS5825P_GPIO1, false);
            result += tas5825p_set_gpio_mode(s_amps.tas5825p, TAS5825P_GPIO1, TAS5825P_GPIO_MODE_OUTPUT);
            break;
        case AMP_ENVELOPE_TRACKING_MODE_ON:
            result = tas5825p_set_gpio_mode(s_amps.tas5825p, TAS5825P_GPIO1,
                                            TAS5825P_GPIO_MODE_HYBRID_PRO_CLASS_H_WAVEFORM_CONTROL_OUTPUT);
            break;
        default:
            return;
    }

    if (result == 0)
    {
        log_info("Envelope tracking mode set to %d", mode);
    }
    else
    {
        log_error("Failed to set envelope tracking mode to %d", mode);
    }
}

void board_link_amps_enable_eq(bool enable)
{
    if (tas5825p_enable_eq(s_amps.tas5825p, enable) == 0)
    {
        log_info("Woofer amp EQ %s", enable ? "enabled" : "disabled");
    }
    else
    {
        log_error("Failed to %s woofer amp EQ", enable ? "enable" : "disable");
    }
}

void board_link_amps_set_bass_level(int8_t bass_db)
{
    if (bass_db < CONFIG_DSP_BASS_MIN || bass_db > CONFIG_DSP_BASS_MAX)
    {
        return;
    }

    if (tas5825p_load_configuration(s_amps.tas5825p, tas5825p_bass_preconfig, TAS5825P_BASS_PRECONFIG_SIZE) != 0)
    {
        log_error("Failed to load bass preconfig");
        return;
    }

    static const tas5825p_cfg_reg_t *bass_configs[] = {
        tas5825p_bass_minus_6db_config, tas5825p_bass_minus_5db_config, tas5825p_bass_minus_4db_config,
        tas5825p_bass_minus_3db_config, tas5825p_bass_minus_2db_config, tas5825p_bass_minus_1db_config,
        tas5825p_bass_0db_config,       tas5825p_bass_plus_1db_config,  tas5825p_bass_plus_2db_config,
        tas5825p_bass_plus_3db_config,  tas5825p_bass_plus_4db_config,  tas5825p_bass_plus_5db_config,
        tas5825p_bass_plus_6db_config,
    };

    uint8_t                   index         = bass_db + 6;
    const tas5825p_cfg_reg_t *p_bass_config = bass_configs[index];

    if (tas5825p_load_configuration(s_amps.tas5825p, p_bass_config, TAS5825P_BASS_CONFIG_SIZE) != 0)
    {
        log_error("Failed to load bass config");
    }
}

void board_link_amps_set_treble_level(int8_t treble_db)
{
    if (treble_db < CONFIG_DSP_TREBLE_MIN || treble_db > CONFIG_DSP_TREBLE_MAX)
    {
        return;
    }

    if (tas5805m_load_configuration(s_amps.tas5805m, tas5805m_treble_preconfig, TAS5805M_TREBLE_PRECONFIG_SIZE) != 0)
    {
        log_error("Failed to load treble preconfig");
        return;
    }

    static const tas5805m_cfg_reg_t *treble_configs[] = {
        tas5805m_treble_minus_6db_config, tas5805m_treble_minus_5db_config, tas5805m_treble_minus_4db_config,
        tas5805m_treble_minus_3db_config, tas5805m_treble_minus_2db_config, tas5805m_treble_minus_1db_config,
        tas5805m_treble_0db_config,       tas5805m_treble_plus_1db_config,  tas5805m_treble_plus_2db_config,
        tas5805m_treble_plus_3db_config,  tas5805m_treble_plus_4db_config,  tas5805m_treble_plus_5db_config,
        tas5805m_treble_plus_6db_config,
    };

    uint8_t                   index           = treble_db + 6;
    const tas5805m_cfg_reg_t *p_treble_config = treble_configs[index];

    if (tas5805m_load_configuration(s_amps.tas5805m, p_treble_config, TAS5805M_TREBLE_CONFIG_SIZE) != 0)
    {
        log_error("Failed to load treble config");
    }
}

void board_link_amps_set_volume(int8_t volume_db)
{
    board_link_amps_set_tweeter_volume(volume_db);
    board_link_amps_set_woofer_volume(volume_db);
}

#if defined(MYND_RPI_MODIFICATION) && defined(HYBRID_VOLUME_MODE)
int8_t board_link_amps_avrcp_to_db(uint8_t avrcp_volume)
{
    if (avrcp_volume == 0)
        return -90; // Mute
    // Map 1..127 to CONFIG_HW_VOL_MIN_DB..CONFIG_HW_VOL_MAX_DB
    return (int8_t)(CONFIG_HW_VOL_MIN_DB +
                    ((int)(avrcp_volume - 1) * (CONFIG_HW_VOL_MAX_DB - CONFIG_HW_VOL_MIN_DB)) /
                        (CONFIG_MAX_AVRCP_VOLUME - 1));
}

void board_link_amps_set_volume_avrcp(uint8_t avrcp_volume)
{
    board_link_amps_set_volume(board_link_amps_avrcp_to_db(avrcp_volume));
}
#endif

void board_link_amps_set_tweeter_volume(int8_t volume_db)
{
    if (tas5805m_set_volume(s_amps.tas5805m, volume_db) == 0)
    {
        log_debug("Tweeter amp volume set to %d dB", volume_db);
        s_amps.tweeter_volume_db = volume_db;
    }
    else
    {
        log_error("Failed to set tweeter amp volume");
    }
}

void board_link_amps_set_woofer_volume(int8_t volume_db)
{
    if (tas5825p_set_volume(s_amps.tas5825p, volume_db) == 0)
    {
        log_debug("Woofer amp volume set to %d dB", volume_db);
        s_amps.woofer_volume_db = volume_db;
    }
    else
    {
        log_error("Failed to set woofer amp volume");
    }
}

int8_t board_link_amps_get_tweeter_volume(void)
{
    return s_amps.tweeter_volume_db;
}

int8_t board_link_amps_get_woofer_volume(void)
{
    return s_amps.woofer_volume_db;
}

void board_link_amps_mute(bool enable)
{
    if (tas5805m_mute(s_amps.tas5805m, enable) == 0)
    {
        log_debug("Tweeter amp %s", enable ? "muted" : "unmuted");
    }
    else
    {
        log_error("Failed to %s tweeter amp", enable ? "mute" : "unmute");
    }

    if (tas5825p_mute(s_amps.tas5825p, enable) == 0)
    {
        log_debug("Woofer amp %s", enable ? "muted" : "unmuted");
    }
    else
    {
        log_error("Failed to %s woofer amp", enable ? "mute" : "unmute");
    }

    log_info("Amps %s", enable ? "muted" : "unmuted");
    s_amps.is_muted = enable;
}

bool board_link_amps_is_muted(void)
{
    return s_amps.is_muted;
}

void board_link_amps_toggle_mute(void)
{
    // Mute and unmute amps to prevent pop noise.
    // The delay amount of 200 ms is derived from testing
    board_link_amps_mute(true);
    vTaskDelay(pdMS_TO_TICKS(200));
    board_link_amps_mute(false);
}

int board_link_amps_read_fs_mon(tas5825p_fs_t *p_woofer_fs, tas5805m_fs_t *p_tweeter_fs)
{
    if (p_woofer_fs == NULL || p_tweeter_fs == NULL)
    {
        log_error("Null pointer passed to board_link_amps_read_fs_mon");
        return -1;
    }

    tas5825p_fs_t fs = TAS5825P_FS_ERROR;
    if (tas5825p_read_fs_mon(s_amps.tas5825p, &fs) != 0)
    {
        log_error("Failed to read woofer FS monitor");
        return -1;
    }
    *p_woofer_fs = fs;

    tas5805m_fs_t tweeter_fs = TAS5805M_FS_ERROR;
    if (tas5805m_read_fs_mon(s_amps.tas5805m, &tweeter_fs) != 0)
    {
        log_error("Failed to read tweeter FS monitor");
        return -1;
    }
    *p_tweeter_fs = tweeter_fs;

    return 0;
}

bool board_link_amps_fs_ready(void)
{
    tas5825p_fs_t woofer_fs  = TAS5825P_FS_ERROR;
    tas5805m_fs_t tweeter_fs = TAS5805M_FS_ERROR;
    if (board_link_amps_read_fs_mon(&woofer_fs, &tweeter_fs) != 0)
    {
        return false;
    }
    log_debug("Woofer FS: %u, Tweeter FS: %u", woofer_fs, tweeter_fs);
    return (woofer_fs != TAS5825P_FS_ERROR) && (tweeter_fs != TAS5805M_FS_ERROR);
}

bool board_link_amps_woofer_fault_detected(void)
{
    return HAL_GPIO_ReadPin(TWEETER_FAULT_INT_GPIO_PORT, TWEETER_FAULT_INT_GPIO_PIN) == GPIO_PIN_SET;
}

void board_link_amps_woofer_fault_recover(void)
{
    if (tas5825p_recover_dc_fake_fault(s_amps.tas5825p) != 0)
    {
        log_error("Failed to recover from woofer fault");
    }
}

static void thread_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
