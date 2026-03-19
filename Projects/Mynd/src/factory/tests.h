#pragma once

bool is_test_mode_activated();
bool is_led_test_activated();
bool is_key_test_activated();
void factory_test_key_process();

typedef void (*button_evt_handler_fn_t)(Teufel::Ux::InputState event);

const std::optional<button_evt_handler_fn_t> get_handler_mapper(uint32_t button_state);
void                                         set_power_with_prompt(bool value);
