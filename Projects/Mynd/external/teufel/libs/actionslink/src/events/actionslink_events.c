#include "actionslink_events.h"
#include "actionslink_decoders.h"
#include "actionslink_log.h"
#include "pb_decode.h"
#include "message.pb.h"

static const actionslink_event_handlers_t *mp_handlers;
static bool m_system_ready_event_received;

static void on_notify_system_ready(void);
static void on_notify_power_state(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_audio_source(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_volume(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_stream_state(const ActionsLink_ToMcuEvent *p_event);
#ifdef ActionsLink_ToMcuEvent_notify_host_source_tag
static void on_notify_host_source(const ActionsLink_ToMcuEvent *p_event);
#endif
#ifdef ActionsLink_ToMcuEvent_notify_play_led_pattern_tag
static void on_notify_play_led_pattern(const ActionsLink_ToMcuEvent *p_event);
#endif
#ifdef ActionsLink_ToMcuEvent_notify_wifi_info_tag
static void on_notify_wifi_info(const ActionsLink_ToMcuEvent *p_event);
#endif
#ifdef ActionsLink_ToMcuEvent_notify_wifi_command_result_tag
static void on_notify_wifi_command_result(const ActionsLink_ToMcuEvent *p_event);
#endif
static void on_notify_bt_a2dp_data(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_avrcp_state(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_avrcp_track_changed(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_avrcp_track_position_changed(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_connection(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_disconnection(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_device_paired(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_pairing_state(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_bt_connection_state(const ActionsLink_ToMcuEvent *p_event);
#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
static void on_notify_tws_connection_state(const ActionsLink_ToMcuEvent *p_event);
#endif
static void on_notify_csb_receiver_connection_state(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_usb_connection(const ActionsLink_ToMcuEvent *p_event);
static void on_notify_dfu_mode(const ActionsLink_ToMcuEvent *p_event);
static void on_app_packet(const ActionsLink_ToMcuEvent *p_event, const uint8_t *p_data, uint16_t data_length);

void actionslink_events_init(const actionslink_event_handlers_t *p_event_handlers)
{
    mp_handlers = p_event_handlers;
    m_system_ready_event_received = false;
}

bool actionslink_events_has_received_system_ready(void)
{
    return m_system_ready_event_received;
}

void actionslink_event_handler(const ActionsLink_ToMcuEvent *p_event, const uint8_t *p_data, uint16_t data_length)
{
    switch (p_event->which_Event)
    {
#ifdef ActionsLink_ToMcuEvent_notify_system_ready_tag
        case ActionsLink_ToMcuEvent_notify_system_ready_tag:
            on_notify_system_ready();
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_power_state_tag
        case ActionsLink_ToMcuEvent_notify_power_state_tag:
            on_notify_power_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_audio_source_tag
        case ActionsLink_ToMcuEvent_notify_audio_source_tag:
            on_notify_audio_source(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_volume_tag
        case ActionsLink_ToMcuEvent_notify_volume_tag:
            on_notify_volume(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_stream_state_tag
        case ActionsLink_ToMcuEvent_notify_stream_state_tag:
            on_notify_stream_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_host_source_tag
        case ActionsLink_ToMcuEvent_notify_host_source_tag:
            on_notify_host_source(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_play_led_pattern_tag
        case ActionsLink_ToMcuEvent_notify_play_led_pattern_tag:
            on_notify_play_led_pattern(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_wifi_info_tag
        case ActionsLink_ToMcuEvent_notify_wifi_info_tag:
            on_notify_wifi_info(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_wifi_command_result_tag
        case ActionsLink_ToMcuEvent_notify_wifi_command_result_tag:
            on_notify_wifi_command_result(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_a2dp_data_tag
        case ActionsLink_ToMcuEvent_notify_bt_a2dp_data_tag:
            on_notify_bt_a2dp_data(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_avrcp_state_tag
        case ActionsLink_ToMcuEvent_notify_bt_avrcp_state_tag:
            on_notify_bt_avrcp_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_avrcp_track_changed_tag
        case ActionsLink_ToMcuEvent_notify_bt_avrcp_track_changed_tag:
            on_notify_bt_avrcp_track_changed(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_avrcp_track_position_changed_tag
        case ActionsLink_ToMcuEvent_notify_bt_avrcp_track_position_changed_tag:
            on_notify_bt_avrcp_track_position_changed(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_connection_tag
        case ActionsLink_ToMcuEvent_notify_bt_connection_tag:
            on_notify_bt_connection(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_disconnection_tag
        case ActionsLink_ToMcuEvent_notify_bt_disconnection_tag:
            on_notify_bt_disconnection(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_device_paired_tag
        case ActionsLink_ToMcuEvent_notify_bt_device_paired_tag:
            on_notify_bt_device_paired(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_pairing_state_tag
        case ActionsLink_ToMcuEvent_notify_bt_pairing_state_tag:
            on_notify_bt_pairing_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_connection_state_tag
        case ActionsLink_ToMcuEvent_notify_bt_connection_state_tag:
            on_notify_bt_connection_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
        case ActionsLink_ToMcuEvent_notify_tws_connection_state_tag:
            on_notify_tws_connection_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_csb_state_tag
        case ActionsLink_ToMcuEvent_notify_csb_state_tag:
            on_notify_csb_receiver_connection_state(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_usb_connected_tag
        case ActionsLink_ToMcuEvent_notify_usb_connected_tag:
            on_notify_usb_connection(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_notify_dfu_mode_tag
        case ActionsLink_ToMcuEvent_notify_dfu_mode_tag:
            on_notify_dfu_mode(p_event);
            break;
#endif
#ifdef ActionsLink_ToMcuEvent_app_packet_tag
        case ActionsLink_ToMcuEvent_app_packet_tag:
            on_app_packet(p_event, p_data, data_length);
            break;
#endif
        default:
            log_warning("event: unknown event %d", p_event->which_Event);
            break;
    }
}

#ifdef ActionsLink_ToMcuEvent_notify_system_ready_tag
static void on_notify_system_ready(void)
{
    log_debug("event: system ready");
    m_system_ready_event_received = true;

    if (mp_handlers->on_notify_system_ready)
    {
        mp_handlers->on_notify_system_ready();
    }
}
#endif

#ifdef ActionsLink_ToMcuEvent_notify_power_state_tag
static void on_notify_power_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: power state");

    if (mp_handlers->on_notify_power_state)
    {
        const ActionsLink_System_PowerState *p_power_state_event = &p_event->Event.notify_power_state;
        switch (p_power_state_event->mode)
        {
            case ActionsLink_System_PowerState_SystemPowerMode_OFF:
                mp_handlers->on_notify_power_state(ACTIONSLINK_POWER_STATE_OFF);
                break;
            case ActionsLink_System_PowerState_SystemPowerMode_ON:
                mp_handlers->on_notify_power_state(ACTIONSLINK_POWER_STATE_ON);
                break;
            case ActionsLink_System_PowerState_SystemPowerMode_STANDBY:
                mp_handlers->on_notify_power_state(ACTIONSLINK_POWER_STATE_STANDBY);
                break;
            case ActionsLink_System_PowerState_SystemPowerMode_SHUTDOWN_REQUEST:
                mp_handlers->on_notify_power_state(ACTIONSLINK_POWER_STATE_SHUTDOWN_REQUEST);
                break;
            default:
                log_debug("invalid power state %d", p_power_state_event->mode);
                break;
        }
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_audio_source_tag
static void on_notify_audio_source(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: audio source");

    if (mp_handlers->on_notify_audio_source)
    {
        const ActionsLink_Audio_Source *p_audio_source_event = &p_event->Event.notify_audio_source;
        switch (p_audio_source_event->source)
        {
            case ActionsLink_Audio_AudioSourceType_A2DP1:
                mp_handlers->on_notify_audio_source(ACTIONSLINK_AUDIO_SOURCE_A2DP1);
                break;
            case ActionsLink_Audio_AudioSourceType_A2DP2:
                mp_handlers->on_notify_audio_source(ACTIONSLINK_AUDIO_SOURCE_A2DP2);
                break;
            case ActionsLink_Audio_AudioSourceType_USB:
                mp_handlers->on_notify_audio_source(ACTIONSLINK_AUDIO_SOURCE_USB);
                break;
            case ActionsLink_Audio_AudioSourceType_ANALOG:
                mp_handlers->on_notify_audio_source(ACTIONSLINK_AUDIO_SOURCE_ANALOG);
                break;
            default:
                log_debug("invalid audio source %d", p_audio_source_event->source);
                break;
        }
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_volume_tag
static void on_notify_volume(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: volume");

    if (mp_handlers->on_notify_volume)
    {
        const ActionsLink_Audio_Volume *p_volume_event = &p_event->Event.notify_volume;
        actionslink_volume_t volume;
        switch (p_volume_event->which_Volume)
        {
            case ActionsLink_Audio_Volume_percent_tag:
                volume.kind = ACTIONSLINK_VOLUME_KIND_PERCENT;
                volume.volume.percent = p_volume_event->Volume.percent;
                break;
            case ActionsLink_Audio_Volume_absolute_avrcp_tag:
                volume.kind = ACTIONSLINK_VOLUME_KIND_ABSOLUTE_AVRCP;
                volume.volume.absolute_avrcp = p_volume_event->Volume.absolute_avrcp;
                break;
            case ActionsLink_Audio_Volume_db_tag:
                volume.kind = ACTIONSLINK_VOLUME_KIND_DB;
                volume.volume.db = p_volume_event->Volume.db;
                break;
            default:
                log_debug("invalid volume kind %d", p_volume_event->which_Volume);
                return;
        }
        volume.is_muted = p_volume_event->is_muted;
        mp_handlers->on_notify_volume(&volume);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_stream_state_tag
static void on_notify_stream_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: stream state");

    if (mp_handlers->on_notify_stream_state)
    {
        bool is_streaming = p_event->Event.notify_stream_state;
        mp_handlers->on_notify_stream_state(is_streaming);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_host_source_tag
static void on_notify_host_source(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: host source");

    if (mp_handlers->on_notify_host_source)
    {
        actionslink_host_source_t source = (actionslink_host_source_t) p_event->Event.notify_host_source;
        mp_handlers->on_notify_host_source(source);
    }
}
#endif

#ifdef ActionsLink_ToMcuEvent_notify_play_led_pattern_tag
static void on_notify_play_led_pattern(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: play led pattern");

    if (mp_handlers->on_notify_play_led_pattern)
    {
        actionslink_led_pattern_t pattern = (actionslink_led_pattern_t) p_event->Event.notify_play_led_pattern.pattern;
        mp_handlers->on_notify_play_led_pattern(pattern);
    }
}
#endif

#ifdef ActionsLink_ToMcuEvent_notify_wifi_info_tag
static void on_notify_wifi_info(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: wifi info");

    if (mp_handlers->on_notify_wifi_info)
    {
        mp_handlers->on_notify_wifi_info(
            p_event->Event.notify_wifi_info.ssid,
            p_event->Event.notify_wifi_info.ip_address,
            p_event->Event.notify_wifi_info.username
        );
    }
}
#endif

#ifdef ActionsLink_ToMcuEvent_notify_wifi_command_result_tag
static void on_notify_wifi_command_result(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: wifi command result");

    if (mp_handlers->on_notify_wifi_command_result)
    {
        const ActionsLink_Rpi_Host_WiFiCommandResult *p_result =
            &p_event->Event.notify_wifi_command_result;
        actionslink_wifi_command_action_t action = ACTIONSLINK_WIFI_COMMAND_ACTION_UNKNOWN;
        actionslink_error_t               status = ACTIONSLINK_ERROR_OPERATION_FAILED;
        actionslink_wifi_command_detail_t detail = ACTIONSLINK_WIFI_COMMAND_DETAIL_NONE;

        switch (p_result->action_type)
        {
            case ActionsLink_Rpi_Host_WiFiCommandResult_ActionType_ACTION_TYPE_CONFIGURE_WIFI:
                action = ACTIONSLINK_WIFI_COMMAND_ACTION_CONFIGURE_WIFI;
                break;
            case ActionsLink_Rpi_Host_WiFiCommandResult_ActionType_ACTION_TYPE_ENABLE_HOTSPOT:
                action = ACTIONSLINK_WIFI_COMMAND_ACTION_ENABLE_HOTSPOT;
                break;
            case ActionsLink_Rpi_Host_WiFiCommandResult_ActionType_ACTION_TYPE_CYCLE_WIFI_NETWORK:
                action = ACTIONSLINK_WIFI_COMMAND_ACTION_CYCLE_WIFI_NETWORK;
                break;
            default:
                break;
        }

        switch (p_result->status.status.code)
        {
            case ActionsLink_Error_Code_Success:
                status = ACTIONSLINK_ERROR_SUCCESS;
                break;
            case ActionsLink_Error_Code_OperationFailed:
                status = ACTIONSLINK_ERROR_OPERATION_FAILED;
                break;
            case ActionsLink_Error_Code_OperationCanceled:
                status = ACTIONSLINK_ERROR_OPERATION_CANCELED;
                break;
            case ActionsLink_Error_Code_OperationNotSupported:
                status = ACTIONSLINK_ERROR_OPERATION_NOT_SUPPORTED;
                break;
            case ActionsLink_Error_Code_ResourceUnavailable:
                status = ACTIONSLINK_ERROR_RESOURCE_UNAVAILABLE;
                break;
            default:
                break;
        }

        switch (p_result->detail)
        {
            case ActionsLink_Rpi_Host_WiFiCommandResult_Detail_DETAIL_BUSY:
                detail = ACTIONSLINK_WIFI_COMMAND_DETAIL_BUSY;
                break;
            default:
                break;
        }

        mp_handlers->on_notify_wifi_command_result(
            p_result->command_id,
            action,
            status,
            p_result->target_reached,
            detail);
    }
}
#endif

#ifdef ActionsLink_ToMcuEvent_notify_bt_a2dp_data_tag
static void on_notify_bt_a2dp_data(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt a2dp data");

    if (mp_handlers->on_notify_bt_a2dp_data)
    {
        const ActionsLink_Bluetooth_A2dpData *p_a2dp_data_event = &p_event->Event.notify_bt_a2dp_data;
        actionslink_a2dp_data_t a2dp_data = {0};
        switch (p_a2dp_data_event->channel_mode)
        {
            case ActionsLink_Bluetooth_ChannelMode_Mono:
                a2dp_data.channel_mode = ACTIONSLINK_A2DP_CHANNEL_MODE_MONO;
                break;
            case ActionsLink_Bluetooth_ChannelMode_Stereo:
                a2dp_data.channel_mode = ACTIONSLINK_A2DP_CHANNEL_MODE_STEREO;
                break;
            default:
                log_debug("invalid channel mode %d", p_a2dp_data_event->channel_mode);
                return;
        }

        switch (p_a2dp_data_event->codec)
        {
            case ActionsLink_Bluetooth_CodecType_SBC:
                a2dp_data.codec = ACTIONSLINK_A2DP_CODEC_SBC;
                break;
            case ActionsLink_Bluetooth_CodecType_MP3:
                a2dp_data.codec = ACTIONSLINK_A2DP_CODEC_MP3;
                break;
            case ActionsLink_Bluetooth_CodecType_AAC:
                a2dp_data.codec = ACTIONSLINK_A2DP_CODEC_AAC;
                break;
            default:
                log_debug("invalid codec %d", p_a2dp_data_event->codec);
                return;
        }

        a2dp_data.sample_rate = p_a2dp_data_event->sample_rate;
        mp_handlers->on_notify_bt_a2dp_data(&a2dp_data);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_avrcp_state_tag
static void on_notify_bt_avrcp_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt avrcp state");

    if (mp_handlers->on_notify_bt_avrcp_state)
    {
        const ActionsLink_Bluetooth_AvrcpState *p_avrcp_state_event = &p_event->Event.notify_bt_avrcp_state;
        actionslink_avrcp_state_t avrcp_state;
        switch (p_avrcp_state_event->state)
        {
            case ActionsLink_Bluetooth_AvrcpState_AvrcpState_PLAY:
                avrcp_state = ACTIONSLINK_AVRCP_STATE_PLAY;
                break;
            case ActionsLink_Bluetooth_AvrcpState_AvrcpState_PAUSE:
                avrcp_state = ACTIONSLINK_AVRCP_STATE_PAUSE;
                break;
            case ActionsLink_Bluetooth_AvrcpState_AvrcpState_STOP:
                avrcp_state = ACTIONSLINK_AVRCP_STATE_STOP;
                break;
            default:
                log_debug("invalid avrcp state %d", p_avrcp_state_event->state);
                return;
        }

        mp_handlers->on_notify_bt_avrcp_state(avrcp_state);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_avrcp_track_changed_tag
static void on_notify_bt_avrcp_track_changed(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: avrcp track changed");

    if (mp_handlers->on_notify_bt_avrcp_track_changed)
    {
        const ActionsLink_Bluetooth_AvrcpTrackChangedEvt *p_avrcp_event = &p_event->Event.notify_bt_avrcp_track_changed;
        mp_handlers->on_notify_bt_avrcp_track_changed(p_avrcp_event->track_id);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_avrcp_track_position_changed_tag
static void on_notify_bt_avrcp_track_position_changed(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt avrcp track position changed");

    if (mp_handlers->on_notify_bt_avrcp_track_position_changed)
    {
        const ActionsLink_Bluetooth_AvrcpTrackPositionChangedEvt *p_avrcp_event = &p_event->Event.notify_bt_avrcp_track_position_changed;
        mp_handlers->on_notify_bt_avrcp_track_position_changed(p_avrcp_event->ms_since_start);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_connection_tag
static void on_notify_bt_connection(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt connection");

    if (mp_handlers->on_notify_bt_connection)
    {
        const ActionsLink_Bluetooth_ConnectionEvt *p_connection_event = &p_event->Event.notify_bt_connection;
        if (p_connection_event->has_device)
        {
            mp_handlers->on_notify_bt_connection(p_connection_event->device.address);
        }
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_disconnection_tag
static void on_notify_bt_disconnection(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt disconnection");

    if (mp_handlers->on_notify_bt_disconnection)
    {
        const ActionsLink_Bluetooth_DisconnectionEvt *p_disconnection_event = &p_event->Event.notify_bt_disconnection;
        if (p_disconnection_event->has_device)
        {
            actionslink_bt_disconnection_t disconnection_type;
            switch (p_disconnection_event->type)
            {
                case ActionsLink_Bluetooth_DisconnectionEvt_DisconnectionType_LINK_LOSS:
                    disconnection_type = ACTIONSLINK_BT_DISCONNECTION_BY_LINK_LOSS;
                    break;
                case ActionsLink_Bluetooth_DisconnectionEvt_DisconnectionType_USER_REQUEST:
                    disconnection_type = ACTIONSLINK_BT_DISCONNECTION_BY_USER_REQUEST;
                    break;
                default:
                    log_debug("invalid disconnection type %d", p_disconnection_event->type);
                    return;
            }

            mp_handlers->on_notify_bt_disconnection(p_disconnection_event->device.address, disconnection_type);
        }
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_device_paired_tag
static void on_notify_bt_device_paired(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt device paired");

    if (mp_handlers->on_notify_bt_device_paired)
    {
        const ActionsLink_Bluetooth_DevicePaired *p_paired_event = &p_event->Event.notify_bt_device_paired;
        if (p_paired_event->has_device)
        {
            mp_handlers->on_notify_bt_device_paired(p_paired_event->device.address);
        }
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_pairing_state_tag
static void on_notify_bt_pairing_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt pairing state");

    if (mp_handlers->on_notify_bt_pairing_state)
    {
        const ActionsLink_Bluetooth_PairingState *p_pairing_event = &p_event->Event.notify_bt_pairing_state;
        actionslink_bt_pairing_state_t pairing_state;
        switch (p_pairing_event->state) {
            case ActionsLink_Bluetooth_PairingState_PairingType_IDLE:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_IDLE;
                break;
            case ActionsLink_Bluetooth_PairingState_PairingType_BT_PAIRING:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_BT_PAIRING;
                break;
#ifdef ActionsLink_FromMcuRequest_notify_tws_connection_state_tag
            case ActionsLink_Bluetooth_PairingState_PairingType_TWS_MASTER_PAIRING:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_TWS_MASTER_PAIRING;
                break;
            case ActionsLink_Bluetooth_PairingState_PairingType_TWS_SLAVE_PAIRING:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_TWS_SLAVE_PAIRING;
                break;
            case ActionsLink_Bluetooth_PairingState_PairingType_TWS_AUTO :
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_TWS_AUTO;
                break;
#endif
            case ActionsLink_Bluetooth_PairingState_PairingType_CSB_AUTO:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_CSB_AUTO;
                break;
            case ActionsLink_Bluetooth_PairingState_PairingType_CSB_BROADCASTER:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_CSB_BROADCASTING;
                break;
            case ActionsLink_Bluetooth_PairingState_PairingType_CSB_RECEIVER:
                pairing_state = ACTIONSLINK_BT_PAIRING_STATE_CSB_RECEIVING;
                break;
            default:
                log_debug("invalid pairing state %d", p_pairing_event->state);
                return;
        }
        mp_handlers->on_notify_bt_pairing_state(pairing_state);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_bt_connection_state_tag
static void on_notify_bt_connection_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: bt connection state");

    if (mp_handlers->on_notify_bt_connection_state)
    {
        const ActionsLink_Bluetooth_ConnectionState *p_connection_event = &p_event->Event.notify_bt_connection_state;
        bool is_connected;
        switch (p_connection_event->state)
        {
            case ActionsLink_Bluetooth_ConnectionState_ConnectionType_DISCONNECTED:
                is_connected = false;
                break;
            case ActionsLink_Bluetooth_ConnectionState_ConnectionType_CONNECTED:
                is_connected = true;
                break;
            default:
                log_debug("invalid connection state %d", p_connection_event->state);
                return;
        }
        mp_handlers->on_notify_bt_connection_state(is_connected);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_tws_connection_state_tag
static void on_notify_tws_connection_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: tws connection state");

    if (mp_handlers->on_notify_tws_connection_state)
    {
        const ActionsLink_Bluetooth_TwsConnectionState *p_connection_event = &p_event->Event.notify_tws_connection_state;
        actionslink_tws_connection_state_t state;
        switch (p_connection_event->state)
        {
            case ActionsLink_Bluetooth_TwsConnectionState_TwsConnectionType_DISCONNECTED:
                state = ACTIONSLINK_TWS_CONNECTION_STATE_DISCONNECTED;
                break;
            case ActionsLink_Bluetooth_TwsConnectionState_TwsConnectionType_MASTER:
                state = ACTIONSLINK_TWS_CONNECTION_STATE_MASTER;
                break;
            case ActionsLink_Bluetooth_TwsConnectionState_TwsConnectionType_SLAVE:
                state = ACTIONSLINK_TWS_CONNECTION_STATE_SLAVE;
                break;
            default:
                log_debug("invalid connection state %d", p_connection_event->state);
                return;
        }
        mp_handlers->on_notify_tws_connection_state(state);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_csb_state_tag
static void on_notify_csb_receiver_connection_state(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: csb receiver connection state");

    if (mp_handlers->on_notify_csb_state)
    {
        const ActionsLink_Bluetooth_CsbState *p_connection_event = &p_event->Event.notify_csb_state;
        actionslink_csb_state_t csb_state;
        actionslink_csb_receiver_disconnect_reason_t csb_disconnect_reason = ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_UNKNOWN;
        switch (p_connection_event->state)
        {
            case ActionsLink_Bluetooth_CsbState_CsbStateType_DISABLED:
                csb_state = ACTIONSLINK_CSB_STATE_DISABLED;
                break;
            case ActionsLink_Bluetooth_CsbState_CsbStateType_BROADCASTING:
                csb_state = ACTIONSLINK_CSB_STATE_BROADCASTING;
                break;
            case ActionsLink_Bluetooth_CsbState_CsbStateType_RECEIVER_PAIRING:
                csb_state = ACTIONSLINK_CSB_STATE_RECEIVER_PAIRING;
                break;
            case ActionsLink_Bluetooth_CsbState_CsbStateType_RECEIVER_CONNECTED:
                csb_state = ACTIONSLINK_CSB_STATE_RECEIVER_CONNECTED;
                break;
            default:
                log_debug("invalid csb state %d", p_connection_event->state);
                return;
        }
        switch (p_connection_event->disconnect_reason)
        {
            case ActionsLink_Bluetooth_CsbState_CsbReceiverDisconnectReason_USER_REQUEST:
                csb_disconnect_reason = ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_USER_REQUEST;
                break;
            case ActionsLink_Bluetooth_CsbState_CsbReceiverDisconnectReason_POWER_OFF:
                csb_disconnect_reason = ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_POWER_OFF;
                break;
            case ActionsLink_Bluetooth_CsbState_CsbReceiverDisconnectReason_LINK_LOSS:
                csb_disconnect_reason = ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_LINK_LOSS;
                break;
            case ActionsLink_Bluetooth_CsbState_CsbReceiverDisconnectReason_UNKNOWN:
            default:
                csb_disconnect_reason = ACTIONSLINK_CSB_RECEIVER_DISCONNECT_REASON_UNKNOWN;
                break;
        }
        mp_handlers->on_notify_csb_state(csb_state, csb_disconnect_reason);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_usb_connected_tag
static void on_notify_usb_connection(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: usb connection");

    if (mp_handlers->on_notify_usb_connected)
    {
        mp_handlers->on_notify_usb_connected(p_event->Event.notify_usb_connected);
    }
}
#endif
#ifdef ActionsLink_ToMcuEvent_notify_dfu_mode_tag
static void on_notify_dfu_mode(const ActionsLink_ToMcuEvent *p_event)
{
    log_debug("event: dfu mode");

    if (mp_handlers->on_notify_dfu_mode)
    {
        mp_handlers->on_notify_dfu_mode(p_event->Event.notify_dfu_mode);
    }
}
#endif
static void on_app_packet(const ActionsLink_ToMcuEvent *p_event, const uint8_t *p_data, uint16_t data_length)
{
    log_debug("event: app packet");

    // TODO: Implement
    (void)p_event;
    (void)p_data;
    (void)data_length;
}
