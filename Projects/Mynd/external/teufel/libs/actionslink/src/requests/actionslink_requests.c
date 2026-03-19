#include "actionslink_requests.h"
#include "actionslink_log.h"
#include "pb_decode.h"
#include "message.pb.h"

static const actionslink_request_handlers_t *mp_handlers;

static void on_request_get_mcu_firmware_version(uint8_t seq_id);
static void on_request_get_pdcontroler_firmware_version(uint8_t seq_id);
static void on_request_get_serial_number(uint8_t seq_id);
static void on_request_get_color(uint8_t seq_id);
static void on_request_set_off_timer(uint8_t seq_id, bool is_enabled, uint32_t value);
static void on_request_get_off_timer(uint8_t seq_id);
static void on_request_set_brightness(uint8_t seq_id, uint32_t brightness);
static void on_request_get_brightness(uint8_t seq_id);
static void on_request_set_bass(uint8_t seq_id, int32_t value);
static void on_request_get_bass(uint8_t seq_id);
static void on_request_set_treble(uint8_t seq_id, int32_t value);
static void on_request_get_treble(uint8_t seq_id);
static void on_request_set_eco_mode(uint8_t seq_id, bool is_enabled);
static void on_request_get_eco_mode(uint8_t seq_id);
static void on_request_set_sound_icons(uint8_t seq_id, bool is_enabled);
static void on_request_get_sound_icons(uint8_t seq_id);
static void on_request_set_battery_friendly_charging(uint8_t seq_id, bool is_enabled);
static void on_request_get_battery_friendly_charging(uint8_t seq_id);
static void on_request_get_battery_capacity(uint8_t seq_id);
static void on_request_get_battery_max_capacity(uint8_t seq_id);

void actionslink_requests_init(const actionslink_request_handlers_t *p_request_handlers)
{
    mp_handlers = p_request_handlers;
}

void actionslink_request_handler(const ActionsLink_ToMcuRequest *p_request, const uint8_t *p_data, uint16_t data_length)
{
    switch (p_request->which_Request)
    {
#ifdef ActionsLink_ToMcuRequest_get_mcu_firmware_version_tag
        case ActionsLink_ToMcuRequest_get_mcu_firmware_version_tag:
            on_request_get_mcu_firmware_version(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_pdcontroller_firmware_version_tag
        case ActionsLink_ToMcuRequest_get_pdcontroller_firmware_version_tag:
            on_request_get_pdcontroler_firmware_version(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_serial_number_tag
        case ActionsLink_ToMcuRequest_get_serial_number_tag:
            on_request_get_serial_number(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_color_tag
        case ActionsLink_ToMcuRequest_get_color_tag:
            on_request_get_color(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_off_timer_tag
        case ActionsLink_ToMcuRequest_set_off_timer_tag:
            on_request_set_off_timer(p_request->seq, p_request->Request.set_off_timer.state,
                                                     p_request->Request.set_off_timer.minutes);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_off_timer_tag
        case ActionsLink_ToMcuRequest_get_off_timer_tag:
            on_request_get_off_timer(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_brightness_tag
        case ActionsLink_ToMcuRequest_set_brightness_tag:
            on_request_set_brightness(p_request->seq, p_request->Request.set_brightness);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_brightness_tag
        case ActionsLink_ToMcuRequest_get_brightness_tag:
            on_request_get_brightness(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_bass_tag
        case ActionsLink_ToMcuRequest_set_bass_tag:
            on_request_set_bass(p_request->seq, p_request->Request.set_bass.value);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_bass_tag
        case ActionsLink_ToMcuRequest_get_bass_tag:
            on_request_get_bass(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_treble_tag
        case ActionsLink_ToMcuRequest_set_treble_tag:
            on_request_set_treble(p_request->seq, p_request->Request.set_treble.value);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_treble_tag
        case ActionsLink_ToMcuRequest_get_treble_tag:
            on_request_get_treble(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_eco_mode_tag
        case ActionsLink_ToMcuRequest_set_eco_mode_tag:
            on_request_set_eco_mode(p_request->seq, p_request->Request.set_eco_mode);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_eco_mode_tag
        case ActionsLink_ToMcuRequest_get_eco_mode_tag:
            on_request_get_eco_mode(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_sound_icons_tag
        case ActionsLink_ToMcuRequest_set_sound_icons_tag:
            on_request_set_sound_icons(p_request->seq, p_request->Request.set_sound_icons);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_sound_icons_tag
        case ActionsLink_ToMcuRequest_get_sound_icons_tag:
            on_request_get_sound_icons(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_set_battery_friendly_charging_tag
        case ActionsLink_ToMcuRequest_set_battery_friendly_charging_tag:
            on_request_set_battery_friendly_charging(p_request->seq, p_request->Request.set_battery_friendly_charging);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_battery_friendly_charging_tag
        case ActionsLink_ToMcuRequest_get_battery_friendly_charging_tag:
            on_request_get_battery_friendly_charging(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_battery_capacity_tag
        case ActionsLink_ToMcuRequest_get_battery_capacity_tag:
            on_request_get_battery_capacity(p_request->seq);
            break;
#endif
#ifdef ActionsLink_ToMcuRequest_get_battery_max_capacity_tag
        case ActionsLink_ToMcuRequest_get_battery_max_capacity_tag:
            on_request_get_battery_max_capacity(p_request->seq);
            break;
#endif
        default:
            log_warning("request: unknown request %d", p_request->which_Request);
            break;
    }
}

#ifdef ActionsLink_ToMcuRequest_get_mcu_firmware_version_tag
static void on_request_get_mcu_firmware_version(uint8_t seq_id)
{
    log_debug("request: get mcu firmware version");

    if (mp_handlers->on_request_get_mcu_firmware_version)
    {
        mp_handlers->on_request_get_mcu_firmware_version(seq_id);
    }
    else
    {
        log_warning("request: get mcu firmware version handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_pdcontroller_firmware_version_tag
static void on_request_get_pdcontroler_firmware_version(uint8_t seq_id)
{
    log_debug("request: get PD controller firmware version");

    if (mp_handlers->on_request_get_pdcontroller_firmware_version)
    {
        mp_handlers->on_request_get_pdcontroller_firmware_version(seq_id);
    }
    else
    {
        log_warning("request: get PD controller firmware version handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_serial_number_tag
static void on_request_get_serial_number(uint8_t seq_id)
{
    log_debug("request: get serial number");

    if (mp_handlers->on_request_get_serial_number)
    {
        mp_handlers->on_request_get_serial_number(seq_id);
    }
    else
    {
        log_warning("request: get serial number handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_color_tag
static void on_request_get_color(uint8_t seq_id)
{
    log_debug("request: get color");

    if (mp_handlers->on_request_get_color)
    {
        mp_handlers->on_request_get_color(seq_id);
    }
    else
    {
        log_warning("request: get color handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_off_timer_tag
static void on_request_set_off_timer(uint8_t seq_id, bool is_enabled, uint32_t value)
{
    log_debug("request: set off timer");

    if (mp_handlers->on_request_set_off_timer)
    {
        mp_handlers->on_request_set_off_timer(seq_id, is_enabled, value);
    }
    else
    {
        log_warning("request: set off timer handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_off_timer_tag
static void on_request_get_off_timer(uint8_t seq_id)
{
    log_debug("request: get off timer");

    if (mp_handlers->on_request_get_off_timer)
    {
        mp_handlers->on_request_get_off_timer(seq_id);
    }
    else
    {
        log_warning("request: get off timer handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_brightness_tag
static void on_request_set_brightness(uint8_t seq_id, uint32_t brightness)
{
    log_debug("request: set brightness");

    if (mp_handlers->on_request_set_brightness)
    {
        mp_handlers->on_request_set_brightness(seq_id, brightness);
    }
    else
    {
        log_warning("request: set brightness handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_brightness_tag
static void on_request_get_brightness(uint8_t seq_id)
{
    log_debug("request: get brightness");

    if (mp_handlers->on_request_get_brightness)
    {
        mp_handlers->on_request_get_brightness(seq_id);
    }
    else
    {
        log_warning("request: get  handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_bass_tag
static void on_request_set_bass(uint8_t seq_id, int32_t value)
{
    log_debug("request: set bass");

    if (mp_handlers->on_request_set_bass)
    {
        mp_handlers->on_request_set_bass(seq_id, value);
    }
    else
    {
        log_warning("request: set bass handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_bass_tag
static void on_request_get_bass(uint8_t seq_id)
{
    log_debug("request: get bass");

    if (mp_handlers->on_request_get_bass)
    {
        mp_handlers->on_request_get_bass(seq_id);
    }
    else
    {
        log_warning("request: get bass handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_treble_tag
static void on_request_set_treble(uint8_t seq_id, int32_t value)
{
    log_debug("request: set treble");

    if (mp_handlers->on_request_set_treble)
    {
        mp_handlers->on_request_set_treble(seq_id, value);
    }
    else
    {
        log_warning("request: set treble handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_treble_tag
static void on_request_get_treble(uint8_t seq_id)
{
    log_debug("request: get trebles");

    if (mp_handlers->on_request_get_treble)
    {
        mp_handlers->on_request_get_treble(seq_id);
    }
    else
    {
        log_warning("request: get treble handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_eco_mode_tag
static void on_request_set_eco_mode(uint8_t seq_id, bool is_enabled)
{
    log_debug("request: set eco mode");

    if (mp_handlers->on_request_set_eco_mode)
    {
        mp_handlers->on_request_set_eco_mode(seq_id, is_enabled);
    }
    else
    {
        log_warning("request: set eco mode handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_eco_mode_tag
static void on_request_get_eco_mode(uint8_t seq_id)
{
    log_debug("request: get eco mode");

    if (mp_handlers->on_request_get_eco_mode)
    {
        mp_handlers->on_request_get_eco_mode(seq_id);
    }
    else
    {
        log_warning("request: get eco mode handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_sound_icons_tag
static void on_request_set_sound_icons(uint8_t seq_id, bool is_enabled)
{
    log_debug("request: set sound icons");

    if (mp_handlers->on_request_set_sound_icons)
    {
        mp_handlers->on_request_set_sound_icons(seq_id, is_enabled);
    }
    else
    {
        log_warning("request: set sound icons handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_sound_icons_tag
static void on_request_get_sound_icons(uint8_t seq_id)
{
    log_debug("request: get sound icons");

    if (mp_handlers->on_request_get_sound_icons)
    {
        mp_handlers->on_request_get_sound_icons(seq_id);
    }
    else
    {
        log_warning("request: get sound icons handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_set_battery_friendly_charging_tag
static void on_request_set_battery_friendly_charging(uint8_t seq_id, bool is_enabled)
{
    log_debug("request: set battery friendly charge");

    if (mp_handlers->on_request_set_battery_friendly_charging)
    {
        mp_handlers->on_request_set_battery_friendly_charging(seq_id, is_enabled);
    }
    else
    {
        log_warning("request: set battery friendly charge handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_battery_friendly_charging_tag
static void on_request_get_battery_friendly_charging(uint8_t seq_id)
{
    log_debug("request: get battery friendly charge");

    if (mp_handlers->on_request_get_battery_friendly_charging)
    {
        mp_handlers->on_request_get_battery_friendly_charging(seq_id);
    }
    else
    {
        log_warning("request: get battery friendly charge handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_battery_capacity_tag
static void on_request_get_battery_capacity(uint8_t seq_id)
{
    log_debug("request: get battery capacity");

    if (mp_handlers->on_request_get_battery_capacity)
    {
        mp_handlers->on_request_get_battery_capacity(seq_id);
    }
    else
    {
        log_warning("request: get battery capacity handler not impl");
    }
}
#endif
#ifdef ActionsLink_ToMcuRequest_get_battery_max_capacity_tag
static void on_request_get_battery_max_capacity(uint8_t seq_id)
{
    log_debug("request: get battery MAX capacity");

    if (mp_handlers->on_request_get_battery_max_capacity)
    {
        mp_handlers->on_request_get_battery_max_capacity(seq_id);
    }
    else
    {
        log_warning("request: get battery capacity handler not impl");
    }
}
#endif

