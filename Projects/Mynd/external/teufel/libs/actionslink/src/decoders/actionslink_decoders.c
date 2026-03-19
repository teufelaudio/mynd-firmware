#include "actionslink_decoders.h"
#include "actionslink_log.h"
#include "message.pb.h"

// Check the following link for a very nice encoding/decoding example with nested repeated messages
// https://github.com/BlockWorksCo/Playground/tree/c968e22cd923ee184fe9362905a4d58e9f69cb27/ProtobufTest

static void log_decoder_error(const char *fn, const char *error)
{
    log_error("decoder: %s failed [%s]", fn, error);
}

bool actionslink_decode_to_mcu_message(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    // The contents of the message can be accessed like this if needed
    // ActionsLink_ToMcu *message = field->message;
    void *actual_arg = *((void **) arg);

    log_info("decoder: decoding message");

    if (field->tag == ActionsLink_ToMcu_response_tag)
    {
        ActionsLink_ToMcuResponse *response = field->pData;
        response->cb_Response.funcs.decode = actionslink_decode_to_mcu_response_message;
        response->cb_Response.arg = actual_arg;
    }
    else if (field->tag == ActionsLink_ToMcu_event_tag)
    {
        ActionsLink_ToMcuEvent *event = field->pData;
        event->cb_Event.funcs.decode = actionslink_decode_to_mcu_event_message;
        event->cb_Event.arg = actual_arg;
    }
    return true;
}

bool actionslink_decode_to_mcu_response_message(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    void *actual_arg = *((void **) arg);

    log_info("decoder: decoding response: tag %d", field->tag);

    if (false)
    {
        // Placeholder
    }
#ifdef ActionsLink_ToMcuResponse_get_firmware_version_tag
    else if (field->tag == ActionsLink_ToMcuResponse_get_firmware_version_tag)
    {
        ActionsLink_System_FirmwareVersion *fw_version = field->pData;
        fw_version->build.funcs.decode = actionslink_decode_string;
        fw_version->build.arg = actual_arg;
    }
#endif
#ifdef ActionsLink_ToMcuResponse_get_this_device_name_tag
    else if (field->tag == ActionsLink_ToMcuResponse_get_this_device_name_tag)
    {
        // FIXME: We need to check if the response is ResponseDeviceName.name or ResponseDeviceName.error
        //        but the required .proto implementation is unclear and the check below does not work
        //
        // if (device_name->which_Result. == ActionsLink_Bluetooth_ResponseDeviceName_name_tag)
        // {
        //     device_name->Result.name.funcs.decode = actionslink_decode_string;
        //     device_name->Result.name.arg = actual_arg;
        // }
        // else if (device_name->which_Result == ActionsLink_Bluetooth_ResponseDeviceName_error_tag)
        // {
        //     // TODO: Decode error here
        //     log_error("decoder: decoding errors not implemented");
        //     return false;
        // }
        
        ActionsLink_Bluetooth_ResponseDeviceName *device_name = field->pData;
        device_name->Result.name.funcs.decode = actionslink_decode_string;
        device_name->Result.name.arg = actual_arg;
    }
#endif
#ifdef ActionsLink_ToMcuResponse_get_bt_paired_device_list_tag
    else if (field->tag == ActionsLink_ToMcuResponse_get_paired_device_list_tag)
    {
        ActionsLink_Bluetooth_ResponsePairedDeviceList *paired_device_list = field->pData;
        paired_device_list->cb_Result.funcs.decode = actionslink_decode_paired_device_list;
        paired_device_list->cb_Result.arg = actual_arg;
    }
#endif
    else
    {
        return false;
    }
    
    return true;
}

bool actionslink_decode_to_mcu_event_message(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    void *actual_arg = *((void **) arg);

#ifdef ActionsLink_ToMcuEvent_app_packet_tag
    if (field->tag == ActionsLink_ToMcuEvent_app_packet_tag)
    {
        ActionsLink_App_Packet *app_packet = field->pData;
        app_packet->payload.funcs.decode = actionslink_decode_string;
        app_packet->payload.arg = actual_arg;
    }
#endif
    return true;
}

bool actionslink_decode_string(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void) field;
    actionslink_buffer_dsc_t *p_buffer_dsc = *((actionslink_buffer_dsc_t **) arg);

    log_info("decoder: decoding string");

    if (p_buffer_dsc->buffer_size < stream->bytes_left + 1) {
        log_error("decoder: buffer too small: size is %d but %d bytes need to be stored", p_buffer_dsc->buffer_size, stream->bytes_left + 1);
        return false;
    }

    size_t size_to_read =
        (stream->bytes_left < p_buffer_dsc->buffer_size - 1) ? stream->bytes_left : p_buffer_dsc->buffer_size - 1;

    if (!pb_read(stream, p_buffer_dsc->p_buffer, size_to_read))
    {
        log_decoder_error(__FUNCTION__, stream->errmsg);
        return false;
    }

    // Null-terminate the string
    p_buffer_dsc->p_buffer[size_to_read] = 0;
    return true;
}

#ifdef ActionsLink_ToMcuResponse_get_bt_paired_device_list_tag
/* Decoder for paired devices list */
bool actionslink_decode_paired_device_list(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    void *actual_arg = *((void **) arg);

    ActionsLink_Bluetooth_PairedDeviceList *list = field->pData;
    list->devices.funcs.decode                   = actionslink_decode_device;
    list->devices.arg                            = actual_arg;

    return true;
}

/* Decoder for the repeated paired-device submessage */
bool actionslink_decode_device(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void) field;
    actionslink_bt_paired_device_list_t *p_devices = *((actionslink_bt_paired_device_list_t **) arg);
    if (p_devices->number_of_items >= p_devices->list_size)
    {
        log_decoder_error(__FUNCTION__, "list is not large enough");
        return false;
    }

    ActionsLink_Bluetooth_Device message = ActionsLink_Bluetooth_Device_init_zero;
    if (!pb_decode(stream, ActionsLink_Bluetooth_Device_fields, &message))
    {
        log_decoder_error(__FUNCTION__, stream->errmsg);
        return false;
    }

    p_devices->p_list[p_devices->number_of_items] = message.address;
    p_devices->number_of_items++;
    return true;
}
#endif

// Unused code that could be useful for future reference
#if 0

bool actionslink_decode_bluetooth_single_bd_addr(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void) field;
    actionslink_bluetooth_address_t *p_bd_addr = *((actionslink_bluetooth_address_t **) arg);
    if (!pb_read(stream, p_bd_addr->buffer, 6))
    {
        log_decoder_error(__FUNCTION__, stream->errmsg);
        return false;
    }
    return true;
}

bool actionslink_decode_connection_status_event(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void) field;
    actionslink_decoder_bluetooth_devices_t *p_devices = *((actionslink_decoder_bluetooth_devices_t **) arg);

    if (p_devices->number_of_items < p_devices->list_size)
    {
        BtConnectionStatusEvt_Device message = BtConnectionStatusEvt_Device_init_zero;
        message.bdaddr.funcs.decode          = actionslink_decode_bluetooth_single_bd_addr;
        message.bdaddr.arg                   = &p_devices->p_list[p_devices->number_of_items].bd_addr;

        if (!pb_decode(stream, BtConnectionStatusEvt_Device_fields, &message))
        {
            log_decoder_error(__FUNCTION__, stream->errmsg);
            return false;
        }
        p_devices->p_list[p_devices->number_of_items].a2dp_active = message.A2dpActive;
        p_devices->p_list[p_devices->number_of_items].hfp_active  = message.HfpActive;
        p_devices->number_of_items++;
    }
    else
    {
        log_decoder_error(__FUNCTION__, "list is not large enough");
        return false;
    }

    return true;
}

bool actionslink_decode_device_list_confirmation(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void) field;
    actionslink_bluetooth_address_list_t *p_devices = *((actionslink_bluetooth_address_list_t **) arg);

    if (p_devices->number_of_items < p_devices->list_size)
    {
        BtGetDeviceListCfm_Device message;
        message.bdaddr.funcs.decode = actionslink_decode_bluetooth_single_bd_addr;
        message.bdaddr.arg          = &p_devices->p_list[p_devices->number_of_items];

        if (!pb_decode(stream, BtGetDeviceListCfm_Device_fields, &message))
        {
            log_decoder_error(__FUNCTION__, stream->errmsg);
            return false;
        }
        p_devices->number_of_items++;
    }
    else
    {
        log_decoder_error(__FUNCTION__, "list is not large enough");
        return false;
    }
    return true;
}

// The callback below is a message-level callback which is called before each
// submessage is encoded. It is used to set the pb_callback_t callbacks inside
// the submessage. The reason we need this is that different submessages share
// storage inside oneof union, and before we know the message type we can't set
// the callbacks without overwriting each other.
bool actionslink_decode_get_device_name_response(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    switch (field->tag)
    {
        case Confirm_bt_get_remote_device_name_cfm_tag:
        {
            BtGetRemoteDeviceNameCfm *submessage          = field->pData;
            submessage->remote_friendly_name.funcs.decode = actionslink_decode_string;
            submessage->remote_friendly_name.arg          = *arg;
            break;
        }

        case Confirm_bt_get_device_list_cfm_tag:
        {
            BtGetDeviceListCfm *submessage   = field->pData;
            submessage->devices.funcs.decode = actionslink_decode_device_list_confirmation;
            submessage->devices.arg          = *arg;
            break;
        }

        default:
        {
            log_debug("decoder: %s ignored: no decoders set for tag %d", __FUNCTION__,
                        field->tag);
            break;
        }
    }

    // Once we return true, pb_dec_submessage() will go on to decode the
    // submessage contents. But if we want, we can also decode it ourselves
    // above and leave stream->bytes_left at 0 value, inhibiting automatic
    // decoding.
    return true;
}

#endif // if 0
