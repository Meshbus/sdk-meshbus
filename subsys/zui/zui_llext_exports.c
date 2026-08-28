/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Export list for the new public ZUI API exposed to LLEXT.
 * Keep this file in sync with <zephyr/zui/zui.h>.
 */

#include <zephyr/llext/symbol.h>
#include <zephyr/zui/zui.h>

EXPORT_GROUP_SYMBOL(ZUI, zui_init);
EXPORT_GROUP_SYMBOL(ZUI, zui_deinit);
EXPORT_GROUP_SYMBOL(ZUI, zui_get_default_host);
EXPORT_GROUP_SYMBOL(ZUI, zui_get_version);
EXPORT_GROUP_SYMBOL(ZUI, zui_get_runtime_stats);

EXPORT_GROUP_SYMBOL(ZUI, zui_input_code_name);
EXPORT_GROUP_SYMBOL(ZUI, zui_input_action_name);
EXPORT_GROUP_SYMBOL(ZUI, zui_input_from_zephyr);
EXPORT_GROUP_SYMBOL(ZUI, zui_input_keypad_value_from_zephyr);
EXPORT_GROUP_SYMBOL(ZUI, zui_action_state_reset);
EXPORT_GROUP_SYMBOL(ZUI, zui_action_state_clear_edges);
EXPORT_GROUP_SYMBOL(ZUI, zui_action_state_update);

#if defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
EXPORT_GROUP_SYMBOL(ZUI, zui_predictive_key_for_char);
EXPORT_GROUP_SYMBOL(ZUI, zui_predictive_sequence_for_word);
EXPORT_GROUP_SYMBOL(ZUI, zui_predictive_candidate_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_predictive_candidate);
EXPORT_GROUP_SYMBOL(ZUI, zui_predictive_has_sequence_prefix);
#endif

EXPORT_GROUP_SYMBOL(ZUI, zui_draw_ctx_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_ctx_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_present);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_reset);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_clear);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_width);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_height);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_font_height);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_font_metrics);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_set_color);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_set_font);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_set_font_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_invert_color);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_set_direction);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_set_clip);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_clear_clip);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_text_aligned);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_text_width);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_glyph_width);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_glyph);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_dot);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_line);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_rect);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_box);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_circle);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_disc);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_round_rect);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_round_rect_stroked);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_round_box);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_triangle);
EXPORT_GROUP_SYMBOL(ZUI, zui_bitmap_payload_size);
EXPORT_GROUP_SYMBOL(ZUI, zui_bitmap_bit_get);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_bitmap);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_bitmap_alpha);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_bitmap_transformed);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_framebuffer);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_icon);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_icon_frame);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_icon_transformed);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_icon_anim);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_progress_bar);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_progress_bar_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_scrollbar);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_button_hints);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_multiline_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_text_fit_width);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_text_line_scrolled);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_text_box);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_bubble_frame);
EXPORT_GROUP_SYMBOL(ZUI, zui_draw_bubble);

EXPORT_GROUP_SYMBOL(ZUI, zui_screen_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_get_user_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_is_entered);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_set_tick_period);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_tick_period);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_request_redraw);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_redraw_is_requested);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_clear_redraw);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_draw);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_submit_input);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_dispatch_event);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_enter);
EXPORT_GROUP_SYMBOL(ZUI, zui_screen_exit);

EXPORT_GROUP_SYMBOL(ZUI, zui_router_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_register_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_unregister_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_switch);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_current);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_screen_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_router_dispatch_event);

EXPORT_GROUP_SYMBOL(ZUI, zui_host_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_get_user_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_attach_router);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_detach_router);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_submit_input);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_request_redraw);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_set_redraw_callback);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_draw);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_run);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_stop);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_poll);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_next_timeout_ms);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_set_suspended);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_is_suspended);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_set_input_locked);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_is_input_locked);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_set_layer_enabled);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_layer_is_enabled);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_send_layer_to_front);
EXPORT_GROUP_SYMBOL(ZUI, zui_host_send_layer_to_back);

EXPORT_GROUP_SYMBOL(ZUI, zui_asset_pack_default);
EXPORT_GROUP_SYMBOL(ZUI, zui_asset_pack_icon_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_asset_pack_icon_id);
EXPORT_GROUP_SYMBOL(ZUI, zui_asset_pack_icon_name);
EXPORT_GROUP_SYMBOL(ZUI, zui_asset_pack_icon_by_id);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_width);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_height);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_frame_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_frame_rate);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_frame_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_frame_size);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_set_update_callback);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_set_frame_rate);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_start);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_stop);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_icon);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_width);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_height);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_frame);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_frame_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_icon_anim_is_last_frame);

EXPORT_GROUP_SYMBOL(ZUI, zui_list_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_reload);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_selected);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_select);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_move);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_activate);
EXPORT_GROUP_SYMBOL(ZUI, zui_list_set_item);

EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_reload);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_selected);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_select);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_move);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_activate);
EXPORT_GROUP_SYMBOL(ZUI, zui_sublist_set_item);

EXPORT_GROUP_SYMBOL(ZUI, zui_form_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_count);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_selected);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_select);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_move);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_option);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_set_option);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_move_option);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_value_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_set_value_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_item_user_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_activate);
EXPORT_GROUP_SYMBOL(ZUI, zui_form_set_item);

EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_set_text);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_set_validator);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_get_validator);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_submit);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_editor_validate_file);

EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_value);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_set_value);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_step);
EXPORT_GROUP_SYMBOL(ZUI, zui_number_editor_submit);

EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_size);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_payload_size);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_set_data);
EXPORT_GROUP_SYMBOL(ZUI, zui_hex_editor_submit);

EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_scroll);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_set_scroll);
EXPORT_GROUP_SYMBOL(ZUI, zui_text_view_scroll_by);

EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_path);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_open);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_stop);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_move);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_choose);
EXPORT_GROUP_SYMBOL(ZUI, zui_file_picker_set_work_q);

EXPORT_GROUP_SYMBOL(ZUI, zui_modal_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_modal_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_modal_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_modal_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_modal_submit);

EXPORT_GROUP_SYMBOL(ZUI, zui_progress_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_progress_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_progress_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_progress_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_progress_value);
EXPORT_GROUP_SYMBOL(ZUI, zui_progress_set);

EXPORT_GROUP_SYMBOL(ZUI, zui_actions_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_selected);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_select);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_move);
EXPORT_GROUP_SYMBOL(ZUI, zui_actions_activate);

EXPORT_GROUP_SYMBOL(ZUI, zui_composite_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_composite_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_composite_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_composite_update);
EXPORT_GROUP_SYMBOL(ZUI, zui_composite_request_redraw);

EXPORT_GROUP_SYMBOL(ZUI, zui_element_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_get_screen);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_reset);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_string);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_multiline_string);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_text_box);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_text_scroll);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_button);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_icon);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_rect);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_circle);
EXPORT_GROUP_SYMBOL(ZUI, zui_element_add_line);

EXPORT_GROUP_SYMBOL(ZUI, zui_blank_create);
EXPORT_GROUP_SYMBOL(ZUI, zui_blank_destroy);
EXPORT_GROUP_SYMBOL(ZUI, zui_blank_get_screen);

EXPORT_GROUP_SYMBOL(ZUI, zui_toast_show);
EXPORT_GROUP_SYMBOL(ZUI, zui_toast_dismiss);
EXPORT_GROUP_SYMBOL(ZUI, zui_toast_is_visible);
EXPORT_GROUP_SYMBOL(ZUI, zui_toast_dismiss_all);
