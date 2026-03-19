#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "message.pb.h"

typedef enum
{
    ACTIONSLINK_LOG_LEVEL_OFF,
    ACTIONSLINK_LOG_LEVEL_ERROR,
    ACTIONSLINK_LOG_LEVEL_WARN,
    ACTIONSLINK_LOG_LEVEL_INFO,
    ACTIONSLINK_LOG_LEVEL_DEBUG,
    ACTIONSLINK_LOG_LEVEL_TRACE,
} actionslink_log_level_t;

typedef enum
{
    ACTIONSLINK_ERROR_SUCCESS,
    ACTIONSLINK_ERROR_OPERATION_FAILED,
    ACTIONSLINK_ERROR_OPERATION_CANCELED,
    ACTIONSLINK_ERROR_OPERATION_NOT_SUPPORTED,
    ACTIONSLINK_ERROR_RESOURCE_UNAVAILABLE,
} actionslink_error_t;

typedef enum
{
    ACTIONSLINK_WIFI_COMMAND_ACTION_UNKNOWN,
    ACTIONSLINK_WIFI_COMMAND_ACTION_CONFIGURE_WIFI,
    ACTIONSLINK_WIFI_COMMAND_ACTION_ENABLE_HOTSPOT,
    ACTIONSLINK_WIFI_COMMAND_ACTION_CYCLE_WIFI_NETWORK,
} actionslink_wifi_command_action_t;

typedef enum
{
    ACTIONSLINK_WIFI_COMMAND_DETAIL_NONE,
    ACTIONSLINK_WIFI_COMMAND_DETAIL_BUSY,
} actionslink_wifi_command_detail_t;

/**
 * @brief Function to initialize the MCU support package necessary for the Actions Link driver to work.
 * @note  This is not a mandatory function and can be NULL if not needed.
 *
 * @return 0 if successful, -1 otherwise
 */
typedef int (*actionslink_msp_init_fn_t)(void);

/**
 * @brief Function to deinitialize the MCU support package necessary for the Actions Link driver to work.
 * @note  This is not a mandatory function and can be NULL if not needed.
 *
 * @return 0 if successful, -1 otherwise
 */
typedef int (*actionslink_msp_deinit_fn_t)(void);

/**
 * @brief Function to send data to the Actions Link module.
 *
 * @param[in] p_data        pointer to data to send
 * @param[in] length        length of data to send
 * @param[in] timeout       timeout value
 *
 * @return 0 if successful, -1 otherwise
 */
typedef int (*actionslink_write_buffer_fn_t)(const uint8_t *p_data, uint8_t length, uint32_t timeout);

/**
 * @brief Function to read data received from the Actions module.
 *
 * @param[out] p_data       pointer to where the data will be written
 * @param[in]  length       length of data to read
 * @param[in]  timeout      timeout value
 *
 * @return 0 if successful, -1 otherwise
 */
typedef int (*actionslink_read_buffer_fn_t)(uint8_t *p_data, uint8_t length, uint32_t timeout);

/**
 * @brief Function to yield the task while waiting for long running operations,
 *        e.g. waiting for a response from the Actions module.
 */
typedef void (*actionslink_task_yield_fn_t)(void);

/**
 * @brief Function to get the current system timestamp in milliseconds.
 *
 * @return timestamp in milliseconds
 */
typedef uint32_t (*actionslink_get_tick_ms_fn_t)(void);

/**
 * @brief Function to call when the Actions Link library logs something.
 *
 * @param[in] level         log level
 * @param[in] string        string to log
 */
typedef void (*actionslink_log_fn_t)(actionslink_log_level_t level, const char *string);

typedef struct actionslink_config
{
    actionslink_write_buffer_fn_t write_buffer_fn; // Mandatory function
    actionslink_read_buffer_fn_t  read_buffer_fn;  // Mandatory function
    actionslink_get_tick_ms_fn_t  get_tick_ms_fn;  // Mandatory function
    actionslink_msp_init_fn_t     msp_init_fn;     // Optional function
    actionslink_msp_deinit_fn_t   msp_deinit_fn;   // Optional function
    actionslink_task_yield_fn_t   task_yield_fn;   // Optional function
    actionslink_log_fn_t          log_fn;          // Optional function
    uint8_t                      *p_rx_buffer;
    uint8_t                      *p_tx_buffer;
    uint16_t                      rx_buffer_size;
    uint16_t                      tx_buffer_size;
} actionslink_config_t;

typedef enum
{
    ACTIONSLINK_POWER_STATE_OFF,
    ACTIONSLINK_POWER_STATE_STANDBY,
    ACTIONSLINK_POWER_STATE_ON,
    ACTIONSLINK_POWER_STATE_SHUTDOWN_REQUEST,
} actionslink_power_state_t;

typedef enum
{
    ACTIONSLINK_AUDIO_SOURCE_A2DP1,
    ACTIONSLINK_AUDIO_SOURCE_A2DP2,
    ACTIONSLINK_AUDIO_SOURCE_USB,
    ACTIONSLINK_AUDIO_SOURCE_ANALOG,
} actionslink_audio_source_t;

#ifdef ActionsLink_ToMcuEvent_notify_host_source_tag
/**
 * @brief Host audio source for LED indication.
 */
typedef enum
{
    ACTIONSLINK_HOST_SOURCE_UNKNOWN,
    ACTIONSLINK_HOST_SOURCE_MPD,
    ACTIONSLINK_HOST_SOURCE_AIRPLAY,
    ACTIONSLINK_HOST_SOURCE_BLUETOOTH,
    ACTIONSLINK_HOST_SOURCE_SPOTIFY,
} actionslink_host_source_t;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_play_led_pattern_tag
/**
 * @brief LED pattern for source LED indication.
 */
typedef enum
{
    ACTIONSLINK_LED_PATTERN_POSITIVE_FEEDBACK,
    ACTIONSLINK_LED_PATTERN_NEGATIVE_FEEDBACK,
} actionslink_led_pattern_t;
#endif
typedef enum
{
    ACTIONSLINK_VOLUME_KIND_PERCENT,
    ACTIONSLINK_VOLUME_KIND_ABSOLUTE_AVRCP,
    ACTIONSLINK_VOLUME_KIND_DB,
} actionslink_volume_kind_t;

typedef struct
{
    actionslink_volume_kind_t kind;
    union
    {
        uint8_t percent;
        uint8_t absolute_avrcp;
        int8_t  db;
    } volume;
    bool is_muted;
} actionslink_volume_t;

typedef enum
{
    ACTIONSLINK_A2DP_CHANNEL_MODE_MONO,
    ACTIONSLINK_A2DP_CHANNEL_MODE_STEREO,
} actionslink_a2dp_channel_mode_t;

typedef enum
{
    ACTIONSLINK_A2DP_CODEC_SBC,
    ACTIONSLINK_A2DP_CODEC_MP3,
    ACTIONSLINK_A2DP_CODEC_AAC,
} actionslink_a2dp_codec_t;

typedef struct
{
    actionslink_a2dp_channel_mode_t channel_mode;
    actionslink_a2dp_codec_t        codec;
    uint32_t                        sample_rate;
} actionslink_a2dp_data_t;

typedef enum
{
    ACTIONSLINK_AVRCP_STATE_PLAY,
    ACTIONSLINK_AVRCP_STATE_PAUSE,
    ACTIONSLINK_AVRCP_STATE_STOP,
} actionslink_avrcp_state_t;

typedef enum
{
    ACTIONSLINK_BT_DISCONNECTION_BY_USER_REQUEST,
    ACTIONSLINK_BT_DISCONNECTION_BY_LINK_LOSS,
} actionslink_bt_disconnection_t;

typedef enum
{
    ACTIONSLINK_BT_PAIRING_STATE_IDLE,
    ACTIONSLINK_BT_PAIRING_STATE_BT_PAIRING,
#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
    ACTIONSLINK_BT_PAIRING_STATE_TWS_MASTER_PAIRING,
    ACTIONSLINK_BT_PAIRING_STATE_TWS_SLAVE_PAIRING,
    ACTIONSLINK_BT_PAIRING_STATE_TWS_AUTO,
#endif
    ACTIONSLINK_BT_PAIRING_STATE_CSB_AUTO,
    ACTIONSLINK_BT_PAIRING_STATE_CSB_BROADCASTING,
    ACTIONSLINK_BT_PAIRING_STATE_CSB_RECEIVING,
} actionslink_bt_pairing_state_t;

#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
typedef enum
{
    ACTIONSLINK_TWS_CONNECTION_STATE_DISCONNECTED,
    ACTIONSLINK_TWS_CONNECTION_STATE_MASTER,
    ACTIONSLINK_TWS_CONNECTION_STATE_SLAVE,
} actionslink_tws_connection_state_t;
#endif

typedef enum
{
    ACTIONSLINK_CSB_STATE_DISABLED,
    ACTIONSLINK_CSB_STATE_BROADCASTING,
    ACTIONSLINK_CSB_STATE_RECEIVER_PAIRING,
    ACTIONSLINK_CSB_STATE_RECEIVER_CONNECTED,
} actionslink_csb_state_t;

typedef enum
{
    ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_UNKNOWN,
    ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_USER_REQUEST,
    ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_POWER_OFF,
    ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_LINK_LOSS,
} actionslink_csb_receiver_disconnect_reason_t;

typedef enum
{
    ACTIONSLINK_CSB_MASTER_EXIT_REASON_UNKNOWN,
    ACTIONSLINK_CSB_MASTER_EXIT_REASON_USER_REQUEST,
    ACTIONSLINK_CSB_MASTER_EXIT_REASON_POWER_OFF
} actionslink_csb_master_exit_reason_t;

typedef enum
{
    ACTIONSLINK_SOUND_ICON_NONE,
    ACTIONSLINK_SOUND_ICON_BATTERY_LOW,
    ACTIONSLINK_SOUND_ICON_BT_CONNECTED,
    ACTIONSLINK_SOUND_ICON_BT_DISCONNECTED,
    ACTIONSLINK_SOUND_ICON_BT_PAIRING,
    ACTIONSLINK_SOUND_ICON_CHARGING,
    ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_CONNECTED,
    ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_DISCONNECTED,
    ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_PAIRING,
    ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_MASTER_ENTERED,
    ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_SLAVE_PAIRING,
    ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK,
    ACTIONSLINK_SOUND_ICON_POWER_OFF,
    ACTIONSLINK_SOUND_ICON_POWER_ON,
    ACTIONSLINK_SOUND_ICON_BUTTON_FAILED,
    ACTIONSLINK_SOUND_ICON_ERROR,
    ACTIONSLINK_SOUND_ICON_FW_ANNOUNCEMENT,
} actionslink_sound_icon_t;

typedef enum
{
    ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY,
    ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT,
} actionslink_sound_icon_playback_mode_t;

typedef enum
{
    ACTIONSLINK_DEVICE_COLOR_BLACK,
    ACTIONSLINK_DEVICE_COLOR_WHITE,
    ACTIONSLINK_DEVICE_COLOR_BERRY,
    ACTIONSLINK_DEVICE_COLOR_MINT,
} actionslink_device_color_t;

typedef enum
{
    ACTIONSLINK_CHARGER_STATUS_NOT_CONNECTED,
    ACTIONSLINK_CHARGER_STATUS_ACTIVE,
    ACTIONSLINK_CHARGER_STATUS_INACTIVE,
    ACTIONSLINK_CHARGER_STATUS_FAULT
} actionslink_charger_status_t;

typedef struct
{
    uint8_t      *p_buffer;
    const uint8_t buffer_size;
} actionslink_buffer_dsc_t;

typedef struct
{
    uint64_t     *p_list;
    uint8_t       number_of_items;
    const uint8_t list_size;
} actionslink_bt_paired_device_list_t;

typedef struct
{
    uint32_t                  major;
    uint32_t                  minor;
    uint32_t                  patch;
    actionslink_buffer_dsc_t *p_build_string;
} actionslink_firmware_version_t;

/**
 * @brief Handler for system ready notifications.
 */
typedef void (*actionslink_on_notify_system_ready_fn_t)(void);

/**
 * @brief Handler for power state change notifications.
 */
typedef void (*actionslink_on_notify_power_state_fn_t)(actionslink_power_state_t power_state);

/**
 * @brief Handler for TWS/CSB master power off notifications.
 */
typedef void (*actionslink_on_notify_tws_csb_master_power_off_fn_t)(void);

/**
 * @brief Handler for audio source change notifications.
 */
typedef void (*actionslink_on_notify_audio_source_fn_t)(actionslink_audio_source_t audio_source);

/**
 * @brief Handler for volume change notifications.
 */
typedef void (*actionslink_on_notify_volume_fn_t)(const actionslink_volume_t *p_volume);

/**
 * @brief Handler for streaming state change notifications.
 */
typedef void (*actionslink_on_notify_stream_state_fn_t)(bool is_streaming);

#ifdef ActionsLink_ToMcuEvent_notify_host_source_tag
/**
 * @brief Handler for host source change notifications.
 */
typedef void (*actionslink_on_notify_host_source_fn_t)(actionslink_host_source_t source);
#endif

#ifdef ActionsLink_ToMcuEvent_notify_play_led_pattern_tag
/**
 * @brief Handler for LED pattern notifications.
 */
typedef void (*actionslink_on_notify_play_led_pattern_fn_t)(actionslink_led_pattern_t pattern);
#endif

#ifdef ActionsLink_ToMcuEvent_notify_wifi_info_tag
/**
 * @brief Handler for WiFi information notifications.
 */
typedef void (*actionslink_on_notify_wifi_info_fn_t)(const char *ssid, const char *ip_address, const char *username);
#endif

#ifdef ActionsLink_ToMcuEvent_notify_wifi_command_result_tag
/**
 * @brief Handler for WiFi command completion notifications.
 */
typedef void (*actionslink_on_notify_wifi_command_result_fn_t)(uint32_t command_id,
                                                               actionslink_wifi_command_action_t action,
                                                               actionslink_error_t status,
                                                               bool target_reached,
                                                               actionslink_wifi_command_detail_t detail);
#endif

/**
 * @brief Handler for A2DP data notifications.
 */
typedef void (*actionslink_on_notify_bt_a2dp_data_fn_t)(actionslink_a2dp_data_t *p_a2dp_data);

/**
 * @brief Handler for AVRCP state change notifications.
 */
typedef void (*actionslink_on_notify_bt_avrcp_state_fn_t)(actionslink_avrcp_state_t avrcp_state);

/**
 * @brief Handler for AVRCP track change notifications.
 */
typedef void (*actionslink_on_notify_bt_avrcp_track_changed_fn_t)(uint64_t track_id);

/**
 * @brief Handler for AVRCP track position change notifications.
 */
typedef void (*actionslink_on_notify_bt_avrcp_track_position_changed_fn_t)(uint32_t ms_since_start);

/**
 * @brief Handler for BT connection notifications.
 */
typedef void (*actionslink_on_notify_bt_connection_fn_t)(uint64_t address);

/**
 * @brief Handler for BT disconnection notifications.
 */
typedef void (*actionslink_on_notify_bt_disconnection_fn_t)(uint64_t                       address,
                                                            actionslink_bt_disconnection_t disconnection_type);

/**
 * @brief Handler for BT device paired notifications.
 */
typedef void (*actionslink_on_notify_bt_device_paired_fn_t)(uint64_t address);

/**
 * @brief Handler for BT pairing state change notifications.
 */
typedef void (*actionslink_on_notify_bt_pairing_state_fn_t)(actionslink_bt_pairing_state_t state);

/**
 * @brief Handler for BT connection state change notifications.
 */
typedef void (*actionslink_on_notify_bt_connection_state_fn_t)(bool is_connected);

#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
/**
 * @brief Handler for TWS connection state change notifications.
 */
typedef void (*actionslink_on_notify_tws_connection_state_fn_t)(actionslink_tws_connection_state_t state);
#endif

/**
 * @brief Handler for CSB state change notifications.
 */
typedef void (*actionslink_on_notify_csb_state_fn_t)(actionslink_csb_state_t                      state,
                                                     actionslink_csb_receiver_disconnect_reason_t disconnect_reason);

/**
 * @brief Handler for USB connection state change notifications.
 */
typedef void (*actionslink_on_notify_usb_connected_fn_t)(bool is_connected);

/**
 * @brief Handler for DFU mode state change notifications.
 */
typedef void (*actionslink_on_notify_dfu_mode_fn_t)(bool is_active);

/**
 * @brief Handler for app packet notifications.
 */
typedef void (*actionslink_on_app_packet_fn_t)(void);

typedef struct actionslink_event_handlers
{
    actionslink_on_notify_system_ready_fn_t on_notify_system_ready;
    actionslink_on_notify_power_state_fn_t  on_notify_power_state;
    actionslink_on_notify_audio_source_fn_t on_notify_audio_source;
    actionslink_on_notify_volume_fn_t       on_notify_volume;
    actionslink_on_notify_stream_state_fn_t on_notify_stream_state;
#ifdef ActionsLink_ToMcuEvent_notify_host_source_tag
    actionslink_on_notify_host_source_fn_t on_notify_host_source;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_play_led_pattern_tag
    actionslink_on_notify_play_led_pattern_fn_t on_notify_play_led_pattern;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_wifi_info_tag
    actionslink_on_notify_wifi_info_fn_t on_notify_wifi_info;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_wifi_command_result_tag
    actionslink_on_notify_wifi_command_result_fn_t on_notify_wifi_command_result;
#endif
    actionslink_on_notify_bt_a2dp_data_fn_t                    on_notify_bt_a2dp_data;
    actionslink_on_notify_bt_avrcp_state_fn_t                  on_notify_bt_avrcp_state;
    actionslink_on_notify_bt_avrcp_track_changed_fn_t          on_notify_bt_avrcp_track_changed;
    actionslink_on_notify_bt_avrcp_track_position_changed_fn_t on_notify_bt_avrcp_track_position_changed;
    actionslink_on_notify_bt_connection_fn_t                   on_notify_bt_connection;
    actionslink_on_notify_bt_disconnection_fn_t                on_notify_bt_disconnection;
    actionslink_on_notify_bt_device_paired_fn_t                on_notify_bt_device_paired;
    actionslink_on_notify_bt_pairing_state_fn_t                on_notify_bt_pairing_state;
    actionslink_on_notify_bt_connection_state_fn_t             on_notify_bt_connection_state;
#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
    actionslink_on_notify_tws_connection_state_fn_t on_notify_tws_connection_state;
#endif
    actionslink_on_notify_csb_state_fn_t     on_notify_csb_state;
    actionslink_on_notify_usb_connected_fn_t on_notify_usb_connected;
    actionslink_on_notify_dfu_mode_fn_t      on_notify_dfu_mode;
    actionslink_on_app_packet_fn_t           on_app_packet;
} actionslink_event_handlers_t;

/**
 * @brief Handler for get firmware request.
 */
typedef void (*actionslink_on_request_get_mcu_firmware_version_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for get firmware request.
 */
typedef void (*actionslink_on_request_get_pdcontroller_firmware_version_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for get serial number request.
 */
typedef void (*actionslink_on_request_get_serial_number_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for get color request.
 */
typedef void (*actionslink_on_request_get_color_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for set off timer request.
 */
typedef void (*actionslink_on_request_set_off_timer_fn_t)(uint8_t seq_id, bool is_enabled, uint32_t value);

/**
 * @brief Handler for get off timer request.
 */
typedef void (*actionslink_on_request_get_off_timer_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for set brightness request.
 */
typedef void (*actionslink_on_request_set_brightness_fn_t)(uint8_t seq_id, uint32_t value);

/**
 * @brief Handler for get brightness request.
 */
typedef void (*actionslink_on_request_get_brightness_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for set bass request.
 */
typedef void (*actionslink_on_request_set_bass_fn_t)(uint8_t seq_id, int32_t value);

/**
 * @brief Handler for get bass request.
 */
typedef void (*actionslink_on_request_get_bass_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for set treble request.
 */
typedef void (*actionslink_on_request_set_treble_fn_t)(uint8_t seq_id, int32_t value);

/**
 * @brief Handler for get treble request.
 */
typedef void (*actionslink_on_request_get_treble_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for eco mode request.
 */
typedef void (*actionslink_on_request_set_eco_mode_fn_t)(uint8_t seq_id, bool value);

/**
 * @brief Handler for get eco mode request.
 */
typedef void (*actionslink_on_request_get_eco_mode_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for set sound icons request.
 */
typedef void (*actionslink_on_request_set_sound_icons_fn_t)(uint8_t seq_id, bool value);

/**
 * @brief Handler for get sound icons request.
 */
typedef void (*actionslink_on_request_get_sound_icons_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for set battery friendly charge request.
 */
typedef void (*actionslink_on_request_set_battery_friendly_charging_fn_t)(uint8_t seq_id, bool value);

/**
 * @brief Handler for get battery friendly charge request.
 */
typedef void (*actionslink_on_request_get_battery_friendly_charging_fn_t)(uint8_t seq_id);

/**
 * @brief Handler for get battery capacity request.
 */
typedef void (*actionslink_on_request_get_battery_capacity_fn_t)(uint8_t seq_id);

typedef struct actionslink_request_handlers
{
    /** System requests. */
    actionslink_on_request_get_mcu_firmware_version_fn_t          on_request_get_mcu_firmware_version;
    actionslink_on_request_get_pdcontroller_firmware_version_fn_t on_request_get_pdcontroller_firmware_version;
    actionslink_on_request_get_serial_number_fn_t                 on_request_get_serial_number;
    actionslink_on_request_get_color_fn_t                         on_request_get_color;
    actionslink_on_request_set_off_timer_fn_t                     on_request_set_off_timer;
    actionslink_on_request_get_off_timer_fn_t                     on_request_get_off_timer;
    actionslink_on_request_set_brightness_fn_t                    on_request_set_brightness;
    actionslink_on_request_get_brightness_fn_t                    on_request_get_brightness;

    /** Audio related requests */
    actionslink_on_request_set_bass_fn_t        on_request_set_bass;
    actionslink_on_request_get_bass_fn_t        on_request_get_bass;
    actionslink_on_request_set_treble_fn_t      on_request_set_treble;
    actionslink_on_request_get_treble_fn_t      on_request_get_treble;
    actionslink_on_request_set_eco_mode_fn_t    on_request_set_eco_mode;
    actionslink_on_request_get_eco_mode_fn_t    on_request_get_eco_mode;
    actionslink_on_request_set_sound_icons_fn_t on_request_set_sound_icons;
    actionslink_on_request_get_sound_icons_fn_t on_request_get_sound_icons;

    /** Battery related requests */
    actionslink_on_request_set_battery_friendly_charging_fn_t on_request_set_battery_friendly_charging;
    actionslink_on_request_get_battery_friendly_charging_fn_t on_request_get_battery_friendly_charging;

    actionslink_on_request_get_battery_capacity_fn_t on_request_get_battery_capacity;
    actionslink_on_request_get_battery_capacity_fn_t on_request_get_battery_max_capacity;
} actionslink_request_handlers_t;
