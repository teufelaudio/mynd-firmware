#pragma once

#include "actionslink_types.h"
#include "pb_decode.h"

bool actionslink_decode_to_mcu_message(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_to_mcu_response_message(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_to_mcu_event_message(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_string(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_device(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_paired_device_list(pb_istream_t *stream, const pb_field_t *field, void **arg);

// Unused code that could be useful for future reference
#if 0

typedef struct
{
    actionslink_bluetooth_device_status_t  *p_list;
    uint8_t                             number_of_items;
    const uint8_t                       list_size;
} actionslink_decoder_bluetooth_devices_t;

bool actionslink_decode_bluetooth_single_bd_addr(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_confirm_message(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_connection_status_event(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool actionslink_decode_device_list_confirmation(pb_istream_t *stream, const pb_field_t *field, void **arg);
#endif
