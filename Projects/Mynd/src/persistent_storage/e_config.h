#pragma once

typedef enum
{
    ADDR_SPEAKER_COLOR      = 0x00,
    ADDR_LED_BRIGHTNESS     = 0x01,
    ADDR_BASS_LEVEL         = 0x02,
    ADDR_TREBLE_LEVEL       = 0x03,
    ADDR_VOLUME_LEVEL       = 0x04,
    ADDR_ECO_MODE           = 0x05,
    ADDR_SOUND_ICONS_ACTIVE = 0x06,
    ADDR_CHARGE_TYPE        = 0x07,

    ADDR_BATTERY_SOC_ALGO_STATE             = 0x70,
    ADDR_BATTERY_SOC_ACCUMULATED_CHARGE_MSB = 0x71,
    ADDR_BATTERY_SOC_ACCUMULATED_CHARGE_LSB = 0x72,
    ADDR_BATTERY_SOC_CAPACITY_MSB           = 0x73,
    ADDR_BATTERY_SOC_CAPACITY_LSB           = 0x74,
} virtAddress_t;
