#include "actionslink.h"
#include "actionslink_bt_ul.h"
#include "actionslink_decoders.h"
#include "actionslink_encoders.h"
#include "actionslink_events.h"
#include "actionslink_requests.h"
#include "actionslink_log.h"
#include "actionslink_utils.h"
#include "actionslink_version.h"
#include "common.pb.h"

typedef struct
{
    bool                                  is_initialized;
    const actionslink_config_t           *p_config;
    const actionslink_event_handlers_t   *p_event_handlers;
    const actionslink_request_handlers_t *p_request_handlers;
    uint8_t                               next_sequence_id;
} actionslink_driver_t;

static actionslink_driver_t m_actionslink;

// Helper functions
static bool        is_driver_ready(void);
static const char *get_error_desc(ActionsLink_Error_Code error_code);
#ifdef ActionsLink_ToMcuRequest_get_color_tag
static ActionsLink_Eco_Device_Color to_pb_color(actionslink_device_color_t color);
#endif


int actionslink_init(const actionslink_config_t *p_config, const actionslink_event_handlers_t *p_event_handlers,
                     const actionslink_request_handlers_t *p_request_handlers)
{
    if ((p_config == NULL) || (p_event_handlers == NULL))
    {
        return -1;
    }

    if ((p_config->write_buffer_fn == NULL) || (p_config->read_buffer_fn == NULL) || (p_config->p_tx_buffer == NULL) ||
        (p_config->p_rx_buffer == NULL) || (p_config->get_tick_ms_fn == NULL))
    {
        return -1;
    }

    if (p_config->tx_buffer_size < 32)
    {
        log_error("tx buffer is too small (< 32 bytes)");
        return -1;
    }

    if (p_config->rx_buffer_size < 32)
    {
        log_error("rx buffer is too small (< 32 bytes)");
        return -1;
    }

    if (p_config->msp_init_fn)
    {
        if (p_config->msp_init_fn() != 0)
        {
            return -1;
        }
    }

    actionslink_log_register(p_config->log_fn);
    m_actionslink.p_config           = p_config;
    m_actionslink.p_request_handlers = p_request_handlers;
    m_actionslink.p_event_handlers   = p_event_handlers;

    actionslink_events_init(p_event_handlers);
    actionslink_requests_init(p_request_handlers);

    actionslink_utils_init(p_config);
    actionslink_bt_ul_init(p_config, actionslink_event_handler, actionslink_request_handler);

    m_actionslink.is_initialized   = true;
    m_actionslink.next_sequence_id = 0;
    log_info("driver initialized");
    return 0;
}

int actionslink_deinit(void)
{
    if (m_actionslink.is_initialized == false)
    {
        return -1;
    }

    if (m_actionslink.p_config->msp_init_fn)
    {
        if (m_actionslink.p_config->msp_init_fn() != 0)
        {
            return -1;
        }
    }

    m_actionslink.is_initialized   = false;
    m_actionslink.next_sequence_id = 0;
    log_info("driver deinitialized");
    return 0;
}

void actionslink_force_stop(void)
{
    actionslink_bt_ul_stop();
}

void actionslink_tick(void)
{
    if (m_actionslink.is_initialized == false)
    {
        log_error("actionslink driver is not initialized");
        return;
    }

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    // Nothing to do with the response, we just need to pass the buffer to the function
    actionslink_bt_ul_rx(&response);
}

bool actionslink_is_ready(void)
{
    return actionslink_events_has_received_system_ready();
}

bool actionslink_is_busy(void)
{
    return actionslink_bt_ul_is_busy();
}

int actionslink_get_firmware_version(actionslink_firmware_version_t *p_version)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending get fw version request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_get_firmware_version_tag;

    ActionsLink_ToMcu response       = ActionsLink_ToMcu_init_zero;
    response.cb_Payload.arg          = p_version->p_build_string;
    response.cb_Payload.funcs.decode = actionslink_decode_to_mcu_message;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to get firmware version");
        return -1;
    }

    p_version->major = response.Payload.response.Response.get_firmware_version.major;
    p_version->minor = response.Payload.response.Response.get_firmware_version.minor;
    p_version->patch = response.Payload.response.Response.get_firmware_version.patch;
    return 0;
}

int actionslink_set_power_state(actionslink_power_state_t power_state)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending set power mode command");

    ActionsLink_System_PowerState_SystemPowerMode power_mode;
    switch (power_state)
    {
        case ACTIONSLINK_POWER_STATE_OFF:
            power_mode = ActionsLink_System_PowerState_SystemPowerMode_OFF;
            break;
        case ACTIONSLINK_POWER_STATE_ON:
            power_mode = ActionsLink_System_PowerState_SystemPowerMode_ON;
            break;
        case ACTIONSLINK_POWER_STATE_STANDBY:
            power_mode = ActionsLink_System_PowerState_SystemPowerMode_STANDBY;
            break;
        case ACTIONSLINK_POWER_STATE_SHUTDOWN_REQUEST:
            power_mode = ActionsLink_System_PowerState_SystemPowerMode_SHUTDOWN_REQUEST;
            break;
        default:
            log_error("invalid power state %d", power_state);
            return -1;
    }

    ActionsLink_FromMcu message                          = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                          = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                = ActionsLink_FromMcuRequest_set_power_state_tag;
    message.Payload.request.Request.set_power_state.mode = power_mode;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.set_power_state.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to set power mode [%s]",
                        get_error_desc(response.Payload.response.Response.set_power_state.status.code));
        return -1;
    }
    return 0;
}

#ifdef ActionsLink_FromMcuRequest_enter_dfu_mode_tag
int actionslink_enter_dfu_mode(void)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending enter dfu mode command");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_enter_dfu_mode_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.enter_dfu_mode.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to enter dfu mode [%s]",
                        get_error_desc(response.Payload.response.Response.enter_dfu_mode.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_configure_wifi_command_tag
int actionslink_send_configure_wifi_command(uint32_t command_id, const char *ssid, const char *password)
{
    if (!is_driver_ready())
        return -1;

    if (!ssid || strlen(ssid) == 0)
    {
        log_error("ssid cannot be null or empty");
        return -1;
    }

    log_debug("send cfg WiFi cmd id=%lu ssid=%s",
              (unsigned long) command_id, ssid);

    ActionsLink_FromMcu message = ActionsLink_FromMcu_init_zero;
    message.which_Payload = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event = ActionsLink_FromMcuEvent_notify_configure_wifi_command_tag;
    message.Payload.event.Event.notify_configure_wifi_command.command_id = command_id;

    size_t ssid_len = strlen(ssid);
    if (ssid_len >= sizeof(message.Payload.event.Event.notify_configure_wifi_command.config.ssid))
    {
        ssid_len = sizeof(message.Payload.event.Event.notify_configure_wifi_command.config.ssid) - 1;
    }
    strncpy(message.Payload.event.Event.notify_configure_wifi_command.config.ssid, ssid, ssid_len);
    message.Payload.event.Event.notify_configure_wifi_command.config.ssid[ssid_len] = '\0';

    if (password && strlen(password) > 0)
    {
        size_t password_len = strlen(password);
        if (password_len >= sizeof(message.Payload.event.Event.notify_configure_wifi_command.config.password))
        {
            password_len = sizeof(message.Payload.event.Event.notify_configure_wifi_command.config.password) - 1;
        }
        strncpy(message.Payload.event.Event.notify_configure_wifi_command.config.password,
                password,
                password_len);
        message.Payload.event.Event.notify_configure_wifi_command.config.password[password_len] = '\0';
    }

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send cfg WiFi cmd");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_enable_hotspot_command_tag
int actionslink_send_enable_hotspot_command(uint32_t command_id)
{
    if (!is_driver_ready())
        return -1;

    log_debug("send hotspot cmd id=%lu", (unsigned long) command_id);

    ActionsLink_FromMcu message = ActionsLink_FromMcu_init_zero;
    message.which_Payload = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event = ActionsLink_FromMcuEvent_notify_enable_hotspot_command_tag;
    message.Payload.event.Event.notify_enable_hotspot_command.command_id = command_id;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send hotspot cmd");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_cycle_wifi_network_command_tag
int actionslink_send_cycle_wifi_network_command(uint32_t command_id)
{
    if (!is_driver_ready())
        return -1;

    log_debug("send cycle WiFi cmd id=%lu", (unsigned long) command_id);

    ActionsLink_FromMcu message = ActionsLink_FromMcu_init_zero;
    message.which_Payload = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event = ActionsLink_FromMcuEvent_notify_cycle_wifi_network_command_tag;
    message.Payload.event.Event.notify_cycle_wifi_network_command.command_id = command_id;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send cycle WiFi cmd");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_cycle_source_tag
int actionslink_send_cycle_source_request(void)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending cycle source request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_cycle_source_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0))
    {
        log_warning("Cycle source request sent, but response not received in time");
    }
    if (response.Payload.response.Response.cycle_source.status.code != ActionsLink_Error_Code_Success)
    {
        log_error("Cycle source unsuccessful: [%s]",
                    get_error_desc(response.Payload.response.Response.cycle_source.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_get_this_device_name_tag
int actionslink_get_this_device_name(actionslink_buffer_dsc_t *p_buffer_dsc)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending get this device name request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_get_this_device_name_tag;

    ActionsLink_ToMcu response       = ActionsLink_ToMcu_init_zero;
    response.cb_Payload.arg          = p_buffer_dsc;
    response.cb_Payload.funcs.decode = actionslink_decode_to_mcu_message;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to get this device name");
        return -1;
    }

    return 0;
}
#endif

int actionslink_get_bt_device_name(uint64_t address, actionslink_buffer_dsc_t *p_buffer_dsc)
{
    log_debug("get bt device name not implemented");
    return -1;
}

#ifdef ActionsLink_FromMcuRequest_get_bt_mac_address_tag
int actionslink_get_bt_mac_address(uint64_t *p_bt_mac_address)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending get bt mac addr request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_get_bt_mac_address_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to get bt mac addr");
        return -1;
    }

    *p_bt_mac_address = response.Payload.response.Response.get_bt_mac_address.address;
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_get_ble_mac_address_tag
int actionslink_get_ble_mac_address(uint64_t *p_ble_mac_address)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending get ble mac addr request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_get_ble_mac_address_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to get ble mac addr");
        return -1;
    }

    *p_ble_mac_address = response.Payload.response.Response.get_ble_mac_address.address;
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_get_bt_rssi_value_tag
int actionslink_get_bt_rssi_value(int8_t *p_bt_rssi_val)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending get bt rssi value request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_get_bt_rssi_value_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to get bt rssi value");
        return -1;
    }

    *p_bt_rssi_val = response.Payload.response.Response.get_bt_rssi_value.rssi;
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_get_bt_paired_device_list_tag
int actionslink_get_bt_paired_device_list(actionslink_bt_paired_device_list_t *p_list)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending get this device name request");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_get_paired_device_list_tag;

    ActionsLink_ToMcu response       = ActionsLink_ToMcu_init_zero;
    response.cb_Payload.arg          = p_list;
    response.cb_Payload.funcs.decode = actionslink_decode_to_mcu_message;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to get this device name");
        return -1;
    }

    return 0;
}
#endif

#ifdef  ActionsLink_FromMcuRequest_clear_bt_paired_device_list_tag
int actionslink_clear_bt_paired_device_list(void)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending clear paired device list command");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_clear_bt_paired_device_list_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.clear_bt_paired_device_list.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to clear paired device list [%s]",
                        get_error_desc(response.Payload.response.Response.clear_bt_paired_device_list.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_disconnect_all_bt_devices_tag
int actionslink_disconnect_all_bt_devices(void)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending disconnect all devices command");

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_disconnect_all_bt_devices_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.disconnect_all_bt_devices.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to disconnect all devices [%s]",
                        get_error_desc(response.Payload.response.Response.disconnect_all_bt_devices.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_set_volume_tag
static int control_volume(ActionsLink_Audio_VolumeControl_VolumeControlAction action)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending volume command: %d", action);

    ActionsLink_FromMcu message                       = ActionsLink_FromMcu_init_zero;
    message.which_Payload                             = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                       = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request             = ActionsLink_FromMcuRequest_set_volume_tag;
    message.Payload.request.Request.set_volume.action = action;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.set_volume.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to set volume [%s]",
                        get_error_desc(response.Payload.response.Response.set_volume.status.code));
        return -1;
    }
    return 0;
}

int actionslink_increase_volume(void)
{
    return control_volume(ActionsLink_Audio_VolumeControl_VolumeControlAction_VOLUME_UP);
}

int actionslink_decrease_volume(void)
{
    return control_volume(ActionsLink_Audio_VolumeControl_VolumeControlAction_VOLUME_DOWN);
}
#endif

#ifdef  ActionsLink_FromMcuRequest_set_absolute_avrcp_volume_tag
int actionslink_set_bt_absolute_avrcp_volume(uint8_t avrcp_volume)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending absolute avrcp volume command: %d", avrcp_volume);

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_set_absolute_avrcp_volume_tag;
    message.Payload.request.Request.set_absolute_avrcp_volume.volume = avrcp_volume;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.set_volume.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to set absolute avrcp volume [%s]",
                        get_error_desc(response.Payload.response.Response.set_absolute_avrcp_volume.status.code));
        return -1;
    }
    return 0;
}
#endif

#if defined(ActionsLink_FromMcuRequest_send_avrcp_action_tag)
#ifdef ActionsLink_Bluetooth_AvrcpAction_init_default
static int send_avrcp_command(ActionsLink_Bluetooth_AvrcpAction_Action action)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending avrcp action command: %d", action);

    ActionsLink_FromMcu message                              = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                    = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                              = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                    = ActionsLink_FromMcuRequest_send_avrcp_action_tag;
    message.Payload.request.Request.send_avrcp_action.action = action;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.send_avrcp_action.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to send avrcp action [%s]",
                        get_error_desc(response.Payload.response.Response.send_avrcp_action.status.code));
        return -1;
    }
    return 0;
}

int actionslink_bt_play_pause(void)
{
    return send_avrcp_command(ActionsLink_Bluetooth_AvrcpAction_Action_TOGGLE_PLAY_PAUSE);
}

int actionslink_bt_play(void)
{
    return send_avrcp_command(ActionsLink_Bluetooth_AvrcpAction_Action_PLAY);
}

int actionslink_bt_pause(void)
{
    return send_avrcp_command(ActionsLink_Bluetooth_AvrcpAction_Action_PAUSE);
}

int actionslink_bt_next_track(void)
{
    return send_avrcp_command(ActionsLink_Bluetooth_AvrcpAction_Action_NEXT_TRACK);
}

int actionslink_bt_previous_track(void)
{
    return send_avrcp_command(ActionsLink_Bluetooth_AvrcpAction_Action_PREVIOUS_TRACK);
}
#endif // ActionsLink_Bluetooth_AvrcpAction_init_default

#elif defined(ActionsLink_FromMcuRequest_send_playback_action_tag)
#ifdef ActionsLink_Rpi_Host_PlaybackAction_init_default
static int send_playback_command(ActionsLink_Rpi_Host_PlaybackAction_Action action)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending playback action command: %d", action);

    ActionsLink_FromMcu message                              = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                    = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                              = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                    = ActionsLink_FromMcuRequest_send_playback_action_tag;
    message.Payload.request.Request.send_playback_action.action = action;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.send_playback_action.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to send playback action [%s]",
                        get_error_desc(response.Payload.response.Response.send_playback_action.status.code));
        return -1;
    }
    return 0;
}

int actionslink_host_play_pause(void)
{
    return send_playback_command(ActionsLink_Rpi_Host_PlaybackAction_Action_TOGGLE_PLAY_PAUSE);
}

int actionslink_host_next_track(void)
{
    return send_playback_command(ActionsLink_Rpi_Host_PlaybackAction_Action_NEXT_TRACK);
}

int actionslink_host_previous_track(void)
{
    return send_playback_command(ActionsLink_Rpi_Host_PlaybackAction_Action_PREVIOUS_TRACK);
}
#endif // ActionsLink_Rpi_Host_PlaybackAction_init_default
#endif // send_avrcp_action_tag or send_playback_action_tag

#ifdef ActionsLink_FromMcuRequest_set_bt_pairing_state_tag
static int set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType pairing_state)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending set pairing state command: %d", pairing_state);

    ActionsLink_FromMcu message                                = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                      = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                                = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                      = ActionsLink_FromMcuRequest_set_bt_pairing_state_tag;
    message.Payload.request.Request.set_bt_pairing_state.state = pairing_state;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.set_bt_pairing_state.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to set bt pairing state [%s]",
                        get_error_desc(response.Payload.response.Response.set_bt_pairing_state.status.code));
        return -1;
    }
    return 0;
}

int actionslink_start_bt_pairing(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_BT_PAIRING);
}

int actionslink_start_multichain_pairing(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_CSB_AUTO);
}

#ifdef ActionsLink_FromMcuRequest_start_tws_pairing_tag
int actionslink_start_tws_pairing(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_TWS_AUTO);
}

int actionslink_start_tws_pairing_as_master(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_TWS_MASTER_PAIRING);
}

int actionslink_start_tws_pairing_as_slave(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_TWS_SLAVE_PAIRING);
}
#endif

int actionslink_start_csb_broadcaster(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_CSB_BROADCASTER);
}

int actionslink_start_csb_receiver(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_CSB_RECEIVER);
}

int actionslink_stop_pairing(void)
{
    return set_pairing_state(ActionsLink_Bluetooth_PairingState_PairingType_IDLE);
}
#endif // ActionsLink_FromMcuRequest_set_bt_pairing_state_tag

#ifdef ActionsLink_FromMcuRequest_exit_csb_mode_tag
int actionslink_exit_csb_mode(actionslink_csb_master_exit_reason_t reason)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending exit csb mode command with reason %d", reason);

    ActionsLink_Bluetooth_ExitCsbMode_ExitReason r;
    switch (reason)
    {
        case ACTIONSLINK_CSB_MASTER_EXIT_REASON_UNKNOWN:
            r = ActionsLink_Bluetooth_ExitCsbMode_ExitReason_UNKNOWN;
            break;
        case ACTIONSLINK_CSB_MASTER_EXIT_REASON_POWER_OFF:
            r = ActionsLink_Bluetooth_ExitCsbMode_ExitReason_POWER_OFF;
            break;
        case ACTIONSLINK_CSB_MASTER_EXIT_REASON_USER_REQUEST:
            r = ActionsLink_Bluetooth_ExitCsbMode_ExitReason_USER_REQUEST;
            break;
        default:
            r = ActionsLink_Bluetooth_ExitCsbMode_ExitReason_UNKNOWN;
            log_error("Unknown exit csb mode reason %d", reason);
            break;
    }

    ActionsLink_FromMcu message                               = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                     = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                               = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                     = ActionsLink_FromMcuRequest_exit_csb_mode_tag;
    message.Payload.request.Request.exit_csb_mode.exit_reason = r;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.exit_csb_mode.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to exit csb mode [%s]",
                        get_error_desc(response.Payload.response.Response.exit_csb_mode.status.code));
        return -1;
    }
    return 0;
}
#endif // ActionsLink_FromMcuRequest_exit_csb_mode_tag

#ifdef ActionsLink_FromMcuRequest_exit_tws_mode_tag
int actionslink_exit_tws_mode(void)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending exit tws mode command");

    ActionsLink_FromMcu message                            = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                  = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                            = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                  = ActionsLink_FromMcuRequest_exit_tws_mode_tag;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.exit_tws_mode.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to exit tws mode [%s]",
                        get_error_desc(response.Payload.response.Response.exit_tws_mode.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_enable_bt_reconnection_tag
int actionslink_enable_bt_reconnection(bool enable)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending enable reconnection command: %d", enable);

    ActionsLink_FromMcu message                            = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                  = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                            = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                  = ActionsLink_FromMcuRequest_enable_bt_reconnection_tag;
    message.Payload.request.Request.enable_bt_reconnection = enable;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.enable_bt_reconnection.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to enable/disable reconnection [%s]",
                        get_error_desc(response.Payload.response.Response.enable_bt_reconnection.status.code));
        return -1;
    }
    return 0;
}
#endif // ActionsLink_FromMcuRequest_enable_bt_reconnection_tag

#ifdef ActionsLink_FromMcuEvent_notify_aux_connected_tag
int actionslink_send_aux_connection_notification(bool is_connected)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending aux connection notification: %d", is_connected);

    ActionsLink_FromMcu message                      = ActionsLink_FromMcu_init_zero;
    message.which_Payload                            = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event                = ActionsLink_FromMcuEvent_notify_aux_connected_tag;
    message.Payload.event.Event.notify_aux_connected = is_connected;

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send aux connection notification");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_usb_connected_tag
int actionslink_send_usb_connection_notification(bool is_connected)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending usb connection notification: %d", is_connected);

    ActionsLink_FromMcu message                      = ActionsLink_FromMcu_init_zero;
    message.which_Payload                            = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event                = ActionsLink_FromMcuEvent_notify_usb_connected_tag;
    message.Payload.event.Event.notify_usb_connected = is_connected;

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send usb connection notification");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_battery_level_tag
int actionslink_send_battery_level(uint8_t battery_level)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending battery level: %d", battery_level);

    ActionsLink_FromMcu message                      = ActionsLink_FromMcu_init_zero;
    message.which_Payload                            = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event                = ActionsLink_FromMcuEvent_notify_battery_level_tag;
    message.Payload.event.Event.notify_battery_level = battery_level;

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send battery level");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_charger_status_tag
int actionslink_send_charger_status(actionslink_charger_status_t status)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending charger status: %d", status);

    ActionsLink_Battery_ChargerStatus charger_status;
    switch (status)
    {
        case ACTIONSLINK_CHARGER_STATUS_NOT_CONNECTED:
            charger_status = ActionsLink_Battery_ChargerStatus_NotConnected;
            break;
        case ACTIONSLINK_CHARGER_STATUS_ACTIVE:
            charger_status = ActionsLink_Battery_ChargerStatus_Active;
            break;
        case ACTIONSLINK_CHARGER_STATUS_INACTIVE:
            charger_status = ActionsLink_Battery_ChargerStatus_Inactive;
            break;
        case ACTIONSLINK_CHARGER_STATUS_FAULT:
            charger_status = ActionsLink_Battery_ChargerStatus_Fault;
            break;
        default:
            log_error("invalid charger status: %d", charger_status);
            return -1;
    }

    ActionsLink_FromMcu message                       = ActionsLink_FromMcu_init_zero;
    message.which_Payload                             = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event                 = ActionsLink_FromMcuEvent_notify_charger_status_tag;
    message.Payload.event.Event.notify_charger_status = charger_status;

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send battery level");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_battery_friendly_charging_tag
int actionslink_send_battery_friendly_charging_notification(bool status)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending battery friendly charging notification: %d", status);

    ActionsLink_FromMcu message                                  = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                        = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event                            = ActionsLink_FromMcuEvent_notify_battery_friendly_charging_tag;
    message.Payload.event.Event.notify_battery_friendly_charging = status;

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send battery friendly charging notification");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_eco_mode_tag
int actionslink_send_eco_mode_state(bool state)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending eco mode state: %d", state);

    ActionsLink_FromMcu message                 = ActionsLink_FromMcu_init_zero;
    message.which_Payload                       = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event           = ActionsLink_FromMcuEvent_notify_eco_mode_tag;
    message.Payload.event.Event.notify_eco_mode = state;

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send eco mode state");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuEvent_notify_color_tag
int actionslink_send_color_id(actionslink_device_color_t color)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending device color: %d", color);

    ActionsLink_FromMcu message              = ActionsLink_FromMcu_init_zero;
    message.which_Payload                    = ActionsLink_FromMcu_event_tag;
    message.Payload.event.which_Event        = ActionsLink_FromMcuEvent_notify_color_tag;
    message.Payload.event.Event.notify_color = to_pb_color(color);

    // Nothing to do with the response, we just need to pass the buffer to the function
    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if (actionslink_bt_ul_tx_rx(&message, &response) != 0)
    {
        log_error("failed to send device color");
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_send_usb_hid_action_tag
static int send_usb_hid_command(ActionsLink_Usb_HidAction_Action action)
{
    if (!is_driver_ready())
        return -1;

    log_debug("sending usb hid action command: %d", action);

    ActionsLink_FromMcu message                                = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                      = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                                = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                      = ActionsLink_FromMcuRequest_send_usb_hid_action_tag;
    message.Payload.request.Request.send_usb_hid_action.action = action;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.send_usb_hid_action.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to send usb hid action [%s]",
                        get_error_desc(response.Payload.response.Response.send_usb_hid_action.status.code));
        return -1;
    }
    return 0;
}

int actionslink_usb_play_pause(void)
{
    return send_usb_hid_command(ActionsLink_Usb_HidAction_Action_PLAY_PAUSE);
}

int actionslink_usb_next_track(void)
{
    return send_usb_hid_command(ActionsLink_Usb_HidAction_Action_NEXT_TRACK);
}

int actionslink_usb_previous_track(void)
{
    return send_usb_hid_command(ActionsLink_Usb_HidAction_Action_PREVIOUS_TRACK);
}
#endif // ActionsLink_FromMcuRequest_send_usb_hid_action_tag

#ifdef ActionsLink_FromMcuRequest_set_audio_source_tag
int actionslink_set_audio_source(actionslink_audio_source_t source)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_Audio_AudioSourceType source_type;
    switch (source)
    {
        case ACTIONSLINK_AUDIO_SOURCE_A2DP1:
            source_type = ActionsLink_Audio_AudioSourceType_A2DP1;
            break;
        case ACTIONSLINK_AUDIO_SOURCE_A2DP2:
            source_type = ActionsLink_Audio_AudioSourceType_A2DP2;
            break;
        case ACTIONSLINK_AUDIO_SOURCE_USB:
            source_type = ActionsLink_Audio_AudioSourceType_USB;
            break;
        case ACTIONSLINK_AUDIO_SOURCE_ANALOG:
            source_type = ActionsLink_Audio_AudioSourceType_ANALOG;
            break;
        default:
            log_error("invalid input source %d", source);
            return -1;
    }

    log_debug("sending set audio source command: %d", source);

    ActionsLink_FromMcu message                             = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                   = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                             = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                   = ActionsLink_FromMcuRequest_set_audio_source_tag;
    message.Payload.request.Request.set_audio_source.source = source_type;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.set_audio_source.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to set audio source [%s]",
                        get_error_desc(response.Payload.response.Response.set_audio_source.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_play_sound_icon_tag
static int get_actionslink_sound_icon_id(actionslink_sound_icon_t sound_icon)
{
    ActionsLink_Audio_SoundIcon sound_icon_id;
    switch (sound_icon)
    {
        case ACTIONSLINK_SOUND_ICON_NONE:
            sound_icon_id = ActionsLink_Audio_SoundIcon_NONE;
            break;
        case ACTIONSLINK_SOUND_ICON_BATTERY_LOW:
            sound_icon_id = ActionsLink_Audio_SoundIcon_BATTERY_LOW;
            break;
        case ACTIONSLINK_SOUND_ICON_BT_CONNECTED:
            sound_icon_id = ActionsLink_Audio_SoundIcon_BT_CONNECTED;
            break;
        case ACTIONSLINK_SOUND_ICON_BT_DISCONNECTED:
            sound_icon_id = ActionsLink_Audio_SoundIcon_BT_DISCONNECTED;
            break;
        case ACTIONSLINK_SOUND_ICON_BT_PAIRING:
            sound_icon_id = ActionsLink_Audio_SoundIcon_BT_PAIRING;
            break;
        case ACTIONSLINK_SOUND_ICON_CHARGING:
            sound_icon_id = ActionsLink_Audio_SoundIcon_CHARGING;
            break;
        case ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_CONNECTED:
            sound_icon_id = ActionsLink_Audio_SoundIcon_MULTISPEAKER_CHAIN_CONNECTED;
            break;
        case ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_DISCONNECTED:
            sound_icon_id = ActionsLink_Audio_SoundIcon_MULTISPEAKER_CHAIN_DISCONNECTED;
            break;
        case ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_PAIRING:
        case ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_MASTER_ENTERED:
        case ACTIONSLINK_SOUND_ICON_MULTISPEAKER_CHAIN_SLAVE_PAIRING:
            sound_icon_id = ActionsLink_Audio_SoundIcon_MULTISPEAKER_CHAIN_PAIRING;
            break;
        case ACTIONSLINK_SOUND_ICON_POSITIVE_FEEDBACK:
            sound_icon_id = ActionsLink_Audio_SoundIcon_POSITIVE_FEEDBACK;
            break;
        case ACTIONSLINK_SOUND_ICON_POWER_OFF:
            sound_icon_id = ActionsLink_Audio_SoundIcon_POWER_OFF;
            break;
        case ACTIONSLINK_SOUND_ICON_POWER_ON:
            sound_icon_id = ActionsLink_Audio_SoundIcon_POWER_ON;
            break;
        case ACTIONSLINK_SOUND_ICON_BUTTON_FAILED:
            sound_icon_id = ActionsLink_Audio_SoundIcon_BUTTON_FAILED;
            break;
        case ACTIONSLINK_SOUND_ICON_ERROR:
            sound_icon_id = ActionsLink_Audio_SoundIcon_ERROR;
            break;
        case ACTIONSLINK_SOUND_ICON_FW_ANNOUNCEMENT:
            sound_icon_id = ActionsLink_Audio_SoundIcon_FW_ANNOUNCEMENT;
            break;
        default:
            log_error("invalid sound icon %d", sound_icon);
            return -1;
    }
    return (int) sound_icon_id;
}

int actionslink_play_sound_icon(actionslink_sound_icon_t               sound_icon,
                                actionslink_sound_icon_playback_mode_t playback_mode, bool loop_forever)
{
    if (!is_driver_ready())
        return -1;

    int id = get_actionslink_sound_icon_id(sound_icon);
    if (id < 0)
        return -1;

    ActionsLink_Audio_SoundIcon sound_icon_id = (ActionsLink_Audio_SoundIcon) id;

    ActionsLink_Audio_SoundIconPlaybackMode mode;
    switch (playback_mode)
    {
        case ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_IMMEDIATELY:
            mode = ActionsLink_Audio_SoundIconPlaybackMode_PLAY_IMMEDIATELY;
            break;
        case ACTIONSLINK_SOUND_ICON_PLAYBACK_MODE_PLAY_AFTER_CURRENT:
            mode = ActionsLink_Audio_SoundIconPlaybackMode_PLAY_AFTER_CURRENT;
            break;
        default:
            log_error("invalid playback mode %d", playback_mode);
            return -1;
    }

    log_debug("sending play sound icon command: %d", sound_icon_id);

    ActionsLink_FromMcu message                                   = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                         = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                                   = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                         = ActionsLink_FromMcuRequest_play_sound_icon_tag;
    message.Payload.request.Request.play_sound_icon.sound_icon    = sound_icon_id;
    message.Payload.request.Request.play_sound_icon.playback_mode = mode;
    message.Payload.request.Request.play_sound_icon.loop_forever  = loop_forever;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.play_sound_icon.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to play sound icon [%s]",
                        get_error_desc(response.Payload.response.Response.play_sound_icon.status.code));
        return -1;
    }
    return 0;
}
#endif

#ifdef ActionsLink_FromMcuRequest_stop_sound_icon_tag
int actionslink_stop_sound_icon(actionslink_sound_icon_t sound_icon)
{
    if (!is_driver_ready())
        return -1;

    int id = get_actionslink_sound_icon_id(sound_icon);
    if (id < 0)
        return -1;

    ActionsLink_Audio_SoundIcon sound_icon_id = (ActionsLink_Audio_SoundIcon) id;

    log_debug("sending stop sound icon command: %d", sound_icon_id);

    ActionsLink_FromMcu message                                = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                      = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq                                = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request                      = ActionsLink_FromMcuRequest_stop_sound_icon_tag;
    message.Payload.request.Request.stop_sound_icon.sound_icon = sound_icon_id;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.stop_sound_icon.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to stop sound icon [%s]",
                        get_error_desc(response.Payload.response.Response.stop_sound_icon.status.code));
        return -1;
    }
    return 0;
}
#endif

static ActionsLink_Common_Result to_pb_result(actionslink_error_t error)
{
    ActionsLink_Common_Result result;

    result.has_status = true;

    switch (error)
    {
        case ACTIONSLINK_ERROR_SUCCESS:
            result.status.code = ActionsLink_Error_Code_Success;
            break;
        case ACTIONSLINK_ERROR_OPERATION_FAILED:
            result.status.code = ActionsLink_Error_Code_OperationFailed;
            break;
        case ACTIONSLINK_ERROR_OPERATION_CANCELED:
            result.status.code = ActionsLink_Error_Code_OperationCanceled;
            break;
        case ACTIONSLINK_ERROR_OPERATION_NOT_SUPPORTED:
            result.status.code = ActionsLink_Error_Code_OperationNotSupported;
            break;
        case ACTIONSLINK_ERROR_RESOURCE_UNAVAILABLE:
            result.status.code = ActionsLink_Error_Code_ResourceUnavailable;
            break;
        default:
            result.status.code = ActionsLink_Error_Code_OperationFailed;
            break;
    }

    return result;
}

#ifdef ActionsLink_ToMcuRequest_get_color_tag
static ActionsLink_Eco_Device_Color to_pb_color(actionslink_device_color_t color)
{
    switch (color)
    {
        case ACTIONSLINK_DEVICE_COLOR_BLACK:
            return ActionsLink_Eco_Device_Color_BLACK;
        case ACTIONSLINK_DEVICE_COLOR_WHITE:
            return ActionsLink_Eco_Device_Color_WHITE;
        case ACTIONSLINK_DEVICE_COLOR_BERRY:
            return ActionsLink_Eco_Device_Color_BERRY;
        case ACTIONSLINK_DEVICE_COLOR_MINT:
            return ActionsLink_Eco_Device_Color_MINT;
        default:
            return ActionsLink_Eco_Device_Color_BLACK;
    }
}
#endif

static int actionslink_send_set_common_response(uint32_t pb_tag, uint8_t sequence_id,
    actionslink_error_t a_error)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                     = ActionsLink_FromMcu_init_zero;
    message.which_Payload                           = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                    = sequence_id;
    message.Payload.response.which_Response         = pb_tag;

    ActionsLink_Common_Result result = to_pb_result(a_error);
#ifdef ActionsLink_FromMcuResponse_error_tag
    message.Payload.response.Response.error = result;
#elif defined(ActionsLink_FromMcuResponse_set_off_timer_tag)
    // TODO: Warning! In order to not duplicate the code we can use _any_ value with type Common.Result.
    // Set off timer response has such type therefore we can use it for all responses with Common.Result type.
    message.Payload.response.Response.set_off_timer = result;
#endif

    return actionslink_bt_ul_tx(&message);
}

#ifdef ActionsLink_FromMcuResponse_set_off_timer_tag
int actionslink_send_set_off_timer_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_off_timer_tag, sequence_id, result);
}
#endif

#ifdef ActionsLink_FromMcuResponse_set_brightness_tag
int actionslink_send_set_brightness_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_brightness_tag, sequence_id, result);
}
#endif


#ifdef ActionsLink_FromMcuResponse_set_bass_tag
int actionslink_send_set_bass_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_bass_tag, sequence_id, result);
}
#endif

#ifdef ActionsLink_FromMcuResponse_set_treble_tag
int actionslink_send_set_treble_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_treble_tag, sequence_id, result);
}
#endif

#ifdef ActionsLink_FromMcuResponse_set_eco_mode_tag
int actionslink_send_set_eco_mode_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_eco_mode_tag, sequence_id, result);
}
#endif

#ifdef ActionsLink_FromMcuResponse_set_sound_icons_tag
int actionslink_send_set_sound_icons_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_sound_icons_tag, sequence_id, result);
}
#endif

#ifdef ActionsLink_FromMcuResponse_set_battery_friendly_charging_tag
int actionslink_send_set_battery_friendly_charging_response(uint8_t sequence_id, actionslink_error_t result)
{
    return actionslink_send_set_common_response(ActionsLink_FromMcuResponse_set_battery_friendly_charging_tag, sequence_id, result);
}
#endif

int actionslink_send_get_mcu_firmware_version_response(uint8_t sequence_id,
    uint32_t major, uint32_t minor, uint32_t patch, const char *build)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                         = ActionsLink_FromMcu_init_zero;
    message.which_Payload                               = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                        = sequence_id;
    message.Payload.response.which_Response             = ActionsLink_FromMcuResponse_get_mcu_firmware_version_tag;
    message.Payload.response.Response.get_mcu_firmware_version.major = major;
    message.Payload.response.Response.get_mcu_firmware_version.minor = minor;
    message.Payload.response.Response.get_mcu_firmware_version.patch = patch;
    message.Payload.response.Response.get_mcu_firmware_version.build.funcs.encode = &actionslink_encode_string;
    message.Payload.response.Response.get_mcu_firmware_version.build.arg = (void*) build;

    return actionslink_bt_ul_tx(&message);
}

#ifdef ActionsLink_FromMcuResponse_get_pdcontroller_firmware_version_tag
int actionslink_send_get_pdcontroller_firmware_version_response(uint8_t sequence_id, uint32_t major, uint32_t minor)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                         = ActionsLink_FromMcu_init_zero;
    message.which_Payload                               = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                        = sequence_id;
    message.Payload.response.which_Response             = ActionsLink_FromMcuResponse_get_pdcontroller_firmware_version_tag;
    message.Payload.response.Response.get_pdcontroller_firmware_version.major = major;
    message.Payload.response.Response.get_pdcontroller_firmware_version.minor = minor;
    message.Payload.response.Response.get_mcu_firmware_version.build.funcs.encode = &actionslink_encode_string;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_serial_number_tag
int actionslink_send_get_serial_number_response(uint8_t sequence_id, const char *serial_number)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                         = ActionsLink_FromMcu_init_zero;
    message.which_Payload                               = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                        = sequence_id;
    message.Payload.response.which_Response             = ActionsLink_FromMcuResponse_get_serial_number_tag;
    message.Payload.response.Response.get_serial_number.funcs.encode = &actionslink_encode_string;
    message.Payload.response.Response.get_serial_number.arg = (void*) serial_number;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_color_tag
int actionslink_send_get_color_response(uint8_t sequence_id, actionslink_device_color_t color)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                 = ActionsLink_FromMcu_init_zero;
    message.which_Payload                       = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                = sequence_id;
    message.Payload.response.which_Response     = ActionsLink_FromMcuResponse_get_color_tag;
    message.Payload.response.Response.get_color = to_pb_color(color);

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_off_timer_tag
int actionslink_send_get_off_timer_response(uint8_t sequence_id, bool is_enabled, uint32_t value)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                 = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                          = sequence_id;
    message.Payload.response.which_Response               = ActionsLink_FromMcuResponse_get_off_timer_tag;
    message.Payload.response.Response.get_off_timer.state = is_enabled ?
                                    ActionsLink_System_OffTimer_State_ENABLED :
                                    ActionsLink_System_OffTimer_State_DISABLED;
    message.Payload.response.Response.get_off_timer.minutes = value;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_brightness_tag
int actionslink_send_get_brightness_response(uint8_t sequence_id, uint32_t value)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                      = ActionsLink_FromMcu_init_zero;
    message.which_Payload                            = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                     = sequence_id;
    message.Payload.response.which_Response          = ActionsLink_FromMcuResponse_get_brightness_tag;
    message.Payload.response.Response.get_brightness = value;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_bass_tag
int actionslink_send_get_bass_response(uint8_t sequence_id, int8_t value)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                      = ActionsLink_FromMcu_init_zero;
    message.which_Payload                            = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                     = sequence_id;
    message.Payload.response.which_Response          = ActionsLink_FromMcuResponse_get_bass_tag;
    message.Payload.response.Response.get_bass.value = value;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_treble_tag
int actionslink_send_get_treble_response(uint8_t sequence_id, int8_t value)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                        = ActionsLink_FromMcu_init_zero;
    message.which_Payload                              = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                       = sequence_id;
    message.Payload.response.which_Response            = ActionsLink_FromMcuResponse_get_treble_tag;
    message.Payload.response.Response.get_treble.value = value;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_eco_mode_tag
int actionslink_send_get_eco_mode_response(uint8_t sequence_id, bool is_enabled)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                    = ActionsLink_FromMcu_init_zero;
    message.which_Payload                          = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                   = sequence_id;
    message.Payload.response.which_Response        = ActionsLink_FromMcuResponse_get_eco_mode_tag;
    message.Payload.response.Response.get_eco_mode = is_enabled;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_eco_mode_tag
int actionslink_send_get_sound_icons_response(uint8_t sequence_id, bool is_enabled)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                       = ActionsLink_FromMcu_init_zero;
    message.which_Payload                             = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                      = sequence_id;
    message.Payload.response.which_Response           = ActionsLink_FromMcuResponse_get_eco_mode_tag;
    message.Payload.response.Response.get_sound_icons = is_enabled;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_battery_friendly_charging_tag
int actionslink_send_get_battery_friendly_charging_response(uint8_t sequence_id, bool is_enabled)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                                     = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                           = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                                    = sequence_id;
    message.Payload.response.which_Response                         = ActionsLink_FromMcuResponse_get_battery_friendly_charging_tag;
    message.Payload.response.Response.get_battery_friendly_charging = is_enabled;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_battery_capacity_tag
int actionslink_send_get_battery_capacity_response(uint8_t sequence_id, uint32_t value)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                            = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                  = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                           = sequence_id;
    message.Payload.response.which_Response                = ActionsLink_FromMcuResponse_get_battery_capacity_tag;
    message.Payload.response.Response.get_battery_capacity = value;

    return actionslink_bt_ul_tx(&message);
}
#endif

#ifdef ActionsLink_FromMcuResponse_get_battery_max_capacity_tag
int actionslink_send_get_battery_max_capacity_response(uint8_t sequence_id, uint32_t value)
{
    if (!is_driver_ready())
        return -1;

    ActionsLink_FromMcu message                                = ActionsLink_FromMcu_init_zero;
    message.which_Payload                                      = ActionsLink_FromMcu_response_tag;
    message.Payload.response.seq                               = sequence_id;
    message.Payload.response.which_Response                    = ActionsLink_FromMcuResponse_get_battery_max_capacity_tag;
    message.Payload.response.Response.get_battery_max_capacity = value;

    return actionslink_bt_ul_tx(&message);
}
#endif

const char *actionslink_get_version()
{
    return ACTIONSLINK_VERSION;
}

#if defined(ActionsLink_FromMcuRequest_write_key_value_tag)
int actionslink_write_key_value(uint32_t key, uint32_t value)
{
    if (!is_driver_ready())
        return -2;

    log_debug("sending kv element write request: key=%d value=%d", key, value);

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_write_key_value_tag;
    message.Payload.request.Request.write_key_value.el.key = key;
    message.Payload.request.Request.write_key_value.el.value = value;

    ActionsLink_ToMcu response = ActionsLink_ToMcu_init_zero;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.write_key_value.status.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to write kv element [%s]",
                        get_error_desc(response.Payload.response.Response.write_key_value.status.code));
        return -2;
    }
    return 0;
}
#endif

#if defined(ActionsLink_FromMcuRequest_read_key_value_tag)
int actionslink_read_key_value(uint32_t key, uint32_t *p_value)
{
    if (!is_driver_ready())
        return -2;

    log_debug("sending kv element read request: key=%d", key);

    ActionsLink_FromMcu message           = ActionsLink_FromMcu_init_zero;
    message.which_Payload                 = ActionsLink_FromMcu_request_tag;
    message.Payload.request.seq           = m_actionslink.next_sequence_id++;
    message.Payload.request.which_Request = ActionsLink_FromMcuRequest_read_key_value_tag;

    ActionsLink_ToMcu response       = ActionsLink_ToMcu_init_zero;
    message.Payload.request.Request.read_key_value.key = key;
    if ((actionslink_bt_ul_tx_rx(&message, &response) != 0) ||
        (response.Payload.response.Response.read_key_value.Result.error.code != ActionsLink_Error_Code_Success))
    {
        log_error("failed to read kv element [%s]",
                        get_error_desc(response.Payload.response.Response.read_key_value.Result.error.code));
        if (response.Payload.response.Response.read_key_value.Result.error.code == ActionsLink_Error_Code_ResourceUnavailable)
            return -1;
        else
            return -2;
    }

    *p_value = response.Payload.response.Response.read_key_value.Result.el.value;

    return 0;
}
#endif

static const char *get_error_desc(ActionsLink_Error_Code error_code)
{
    switch (error_code)
    {
        case ActionsLink_Error_Code_Success:
            return "success";
        case ActionsLink_Error_Code_OperationFailed:
            return "operation failed";
        case ActionsLink_Error_Code_OperationCanceled:
            return "operation canceled";
        case ActionsLink_Error_Code_OperationNotSupported:
            return "operation not supported";
        case ActionsLink_Error_Code_ResourceUnavailable:
            return "kv resource is unavailble";
        default:
            return "unknown error";
    }
}

static bool is_driver_ready(void)
{
    if (m_actionslink.is_initialized == false)
    {
        log_error("actionslink driver is not initialized yet");
        return false;
    }

    if (actionslink_events_has_received_system_ready() == false)
    {
        log_error("actionslink driver is not ready yet");
        return false;
    }

    return true;
}
