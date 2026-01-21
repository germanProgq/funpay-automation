/* FunPay Vertex Telegram service implementation. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fpv_tg_handle_message(fpv_telegram_service_t* service, fpv_tg_message_t* message);
static void fpv_tg_handle_callback(fpv_telegram_service_t* service, fpv_tg_callback_t* callback);
void* fpv_tg_thread_main(void* context);

static void fpv_tg_handle_message(fpv_telegram_service_t* service, fpv_tg_message_t* message) {
  if (!service || !message) {
    return;
  }
  bool is_private = message->chat_type && strcmp(message->chat_type, "private") == 0;
  bool is_authorized = fpv_tg_is_authorized(service, message->from_id);
  const char* session_user_id =
      fpv_tg_get_session_user_id(service, message->from_id);

  if (!message->reply_topic_created && (!is_private || is_authorized)) {
    fpv_tg_setup_default_notifications(service, message->chat_id);
  }

  if (is_private && !is_authorized) {
    if (fpv_tg_handle_auth_state(service, message)) {
      return;
    }
    if (!message->text || !message->text[0]) {
      return;
    }
    fpv_tg_send_login_menu(service, message->chat_id);
    return;
  }
  if (!is_authorized) {
    return;
  }

  if (fpv_tg_handle_auth_state(service, message)) {
    return;
  }

  if (is_private) {
    if (!service->identity) {
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_identity_unavailable"),
                          NULL, NULL);
      return;
    }
    fpv_organization_t* org = NULL;
    fpv_team_t* team = NULL;
    fpv_role_t role = FPV_ROLE_UNKNOWN;
    fpv_result_t ctx_result =
        fpv_tg_resolve_user_context(service, session_user_id, &org, &team, &role);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    if (ctx_result == FPV_ERR_NOT_FOUND) {
      if (!message->text || !message->text[0]) {
        return;
      }
      fpv_tg_send_workspace_menu(service, message->chat_id);
      return;
    }
    if (ctx_result != FPV_OK) {
      fpv_tg_send_auth_error(service, message->chat_id, ctx_result);
      return;
    }
  }

  if (fpv_tg_handle_upload_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_misc_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_ar_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_ad_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_template_state(service, message)) {
    return;
  }

  if (!message->text || !message->text[0]) {
    return;
  }

  const char* command = NULL;
  const char* args = NULL;
  char command_buf[64];
  if (fpv_tg_parse_command(message->text, command_buf, sizeof(command_buf), &args)) {
    fpv_tg_trim_lower(command_buf);
    command = command_buf;
  } else {
    if (strcmp(message->text,
               "\xF0\x9F\x93\x8B \xD0\x9B\xD0\xBE\xD0\xB3\xD0\xB8 \xF0\x9F\x93\x8B") == 0) {
      command = "logs";
    } else if (strcmp(message->text,
                      "\xE2\x9A\x99\xEF\xB8\x8F \xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xb9\xD0\xBA\xD0\xb8 \xE2\x9A\x99\xEF\xB8\x8F") == 0) {
      command = "menu";
    } else if (strcmp(message->text,
                      "\xF0\x9F\x93\x88 \xD0\xA1\xD0\xb8\xD1\x81\xD1\x82\xD0\xb5\xD0\xBC\xD0\xb0 \xF0\x9F\x93\x88") == 0) {
      command = "sys";
    } else if (strcmp(message->text,
                      "\xF0\x9F\x94\x84 \xD0\x9F\xD0\xb5\xD1\x80\xD0\xb5\xD0\xb7\xD0\xb0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA \xF0\x9F\x94\x84") == 0) {
      command = "restart";
    } else if (strcmp(message->text,
                      "\xF0\x9F\x94\x8C \xD0\x9E\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5 \xF0\x9F\x94\x8C") == 0) {
      command = "power_off";
    } else if (strcmp(message->text,
                      "\xE2\x9D\x8C \xD0\x97\xD0\xb0\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C \xE2\x9D\x8C") == 0) {
      fpv_tg_close_old_keyboard(service, message->chat_id);
      return;
    }
  }

  if (!command) {
    return;
  }

  if (strcmp(command, "menu") == 0) {
    fpv_tg_send_menu(service, message->chat_id);
    return;
  }
  if (strcmp(command, "all") == 0) {
    fpv_tg_send_all_settings(service, message->chat_id);
    return;
  }
  if (strcmp(command, "profile") == 0) {
    fpv_tg_send_profile(service, message->chat_id);
    return;
  }
  if (strcmp(command, "test_lot") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_MANUAL_AD_TEST,
                        "create_test_ad_key");
    return;
  }
  if (strcmp(command, "upload_img") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_UPLOAD_IMAGE,
                        "send_img");
    return;
  }
  if (strcmp(command, "ban") == 0) {
    if (!fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST)) {
      fpv_tg_send_feature_unavailable(service, message->chat_id);
      return;
    }
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_BAN,
                        "act_blacklist");
    return;
  }
  if (strcmp(command, "unban") == 0) {
    if (!fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST)) {
      fpv_tg_send_feature_unavailable(service, message->chat_id);
      return;
    }
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_UNBAN,
                        "act_unban");
    return;
  }
  if (strcmp(command, "black_list") == 0) {
    if (!fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST)) {
      fpv_tg_send_feature_unavailable(service, message->chat_id);
      return;
    }
    fpv_tg_send_blacklist(service, message->chat_id);
    return;
  }
  if (strcmp(command, "watermark") == 0) {
    if (!fpv_tg_has_entitlement(service, FPV_FEATURE_WATERMARK)) {
      fpv_tg_send_feature_unavailable(service, message->chat_id);
      return;
    }
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_EDIT_WATERMARK,
                        "act_edit_watermark");
    return;
  }
  if (strcmp(command, "logs") == 0) {
    fpv_tg_send_logs(service, message->chat_id);
    return;
  }
  if (strcmp(command, "del_logs") == 0) {
    fpv_tg_delete_logs(service, message->chat_id);
    return;
  }
  if (strcmp(command, "about") == 0) {
    fpv_tg_send_about(service, message->chat_id);
    return;
  }
  if (strcmp(command, "sys") == 0) {
    fpv_tg_send_sysinfo(service, message->chat_id);
    return;
  }
  if (strcmp(command, "old_orders") == 0) {
    if (!fpv_tg_has_entitlement(service, FPV_FEATURE_OLD_ORDERS_SCANNER)) {
      fpv_tg_send_feature_unavailable(service, message->chat_id);
      return;
    }
    fpv_tg_send_old_orders(service, message->chat_id);
    return;
  }
  if (strcmp(command, "keyboard") == 0) {
    fpv_tg_open_old_keyboard(service, message->chat_id);
    return;
  }
  if (strcmp(command, "change_cookie") == 0) {
    fpv_tg_change_cookie(service, message->chat_id, args);
    return;
  }
  if (strcmp(command, "restart") == 0) {
    fpv_tg_restart(service, message->chat_id);
    return;
  }
  if (strcmp(command, "power_off") == 0) {
    fpv_tg_power_off(service, message->chat_id);
    return;
  }
}

static void fpv_tg_handle_callback(fpv_telegram_service_t* service, fpv_tg_callback_t* callback) {
  if (!service || !callback) {
    return;
  }
  if (fpv_tg_handle_auth_callback(service, callback)) {
    return;
  }
  if (!fpv_tg_is_authorized(service, callback->from_id)) {
    char user_buf[32];
    char chat_buf[32];
    snprintf(user_buf, sizeof(user_buf), "%" PRId64, callback->from_id);
    snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, callback->chat_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    const char* chat_name = callback->chat_username ? callback->chat_username : "";
    fpv_tg_log_format(service, FPV_LOG_WARNING, "log_click_attempt",
                      (const char*[]){uname, user_buf, chat_name, chat_buf}, 4);
    return;
  }

  if (!service->identity) {
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "auth_identity_unavailable"),
                        NULL, NULL);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_role_t role = FPV_ROLE_UNKNOWN;
  fpv_result_t ctx_result =
      fpv_tg_resolve_user_context(
          service,
          fpv_tg_get_session_user_id(service, callback->from_id),
          &org,
          &team,
          &role);
  fpv_organization_destroy(org);
  fpv_team_destroy(team);
  if (ctx_result == FPV_ERR_NOT_FOUND) {
    fpv_tg_send_workspace_menu(service, callback->chat_id);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (ctx_result != FPV_OK) {
    fpv_tg_send_auth_error(service, callback->chat_id, ctx_result);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  if (!callback->data) {
    return;
  }
  if (strcmp(callback->data, fpv_tg_cbt_upload_products_file) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_PRODUCTS_FILE,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xBB \xD1\x81 \xD1\x82\xD0\xBE\xD0\xb2\xD0\xb0\xD1\x80\xD0\xb0\xD0\xBC\xD0\xb8.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cb_upload_main_config) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_MAIN_CONFIG,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD0\xBE\xD1\x81\xD0\xBD\xD0\xBE\xD0\xb2\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cb_upload_auto_response_config) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE\xD0\xBE\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD1\x87\xD0\xb8\xD0\xBA\xD0\xb0.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cb_upload_auto_delivery_config) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE-\xD0\xb2\xD1\x8B\xD0\xb4\xD0\xb0\xD1\x87\xD0\xb8.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cbt_upload_image) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_IMAGE,
        fpv_tg_loc(service, "send_img"));
    return;
  }

  char* data_copy = fpv_strdup(callback->data);
  if (!data_copy) {
    return;
  }
  char* tokens[10] = {0};
  size_t token_count = fpv_tg_split_tokens(data_copy, tokens, 10);
  if (token_count > 0) {
    if (strcmp(tokens[0], fpv_tg_cbt_main) == 0) {
      fpv_tg_open_main_sections(service, callback, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_main2) == 0) {
      fpv_tg_open_main_sections(service, callback, true);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_empty) == 0) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_param_disabled) == 0) {
      fpv_tg_answer_callback(service, callback->id,
                             fpv_tg_loc(service, "param_disabled"), true);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_clear_state) == 0) {
      fpv_tg_user_state_t* state =
          fpv_tg_get_state(service, callback->chat_id, callback->from_id);
      int prompt_id = state ? state->message_id : 0;
      fpv_tg_clear_state(service, callback->chat_id, callback->from_id, false);
      if (prompt_id > 0) {
        fpv_tg_delete_message(service, callback->chat_id, prompt_id);
      } else if (callback->message_id > 0) {
        fpv_tg_delete_message(service, callback->chat_id, callback->message_id);
      }
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_old_help) == 0) {
      fpv_tg_handle_old_help(service, callback);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_cancel_shutdown) == 0) {
      fpv_tg_cancel_shutdown_callback(service, callback);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_shutdown) == 0) {
      size_t state = 0;
      uint64_t instance_id = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &state) &&
          state <= (size_t)INT_MAX &&
          fpv_tg_parse_uint64_value(tokens[2], &instance_id)) {
        fpv_tg_handle_shutdown_callback(service, callback, (int)state, instance_id);
      } else {
        fpv_tg_answer_callback(service, callback->id,
                               fpv_tg_loc(service, "power_off_error"), true);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_update_profile) == 0 ||
        strcmp(tokens[0], fpv_tg_cbt_update_profile) == 0) {
      fpv_tg_update_profile_callback(service, callback, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_update_adv_profile) == 0) {
      fpv_tg_update_profile_callback(service, callback, true);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_config_loader) == 0) {
      fpv_tg_open_settings_category(service, callback, "configs");
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_category) == 0) {
      if (token_count > 1) {
        fpv_tg_open_settings_category(service, callback, tokens[1]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_template_list) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_templates_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_template_list_ans) == 0) {
      size_t offset = 0;
      size_t prev_page = 0;
      uint64_t chat_id = 0;
      if (token_count > 4 &&
          fpv_tg_parse_size_value(tokens[1], &offset) &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id) &&
          fpv_tg_parse_size_value(tokens[4], &prev_page)) {
        char extra_buf[256];
        extra_buf[0] = '\0';
        if (token_count > 5) {
          size_t used = 0;
          for (size_t i = 5; i < token_count; i++) {
            int written = snprintf(
                extra_buf + used,
                sizeof(extra_buf) - used,
                ":%s",
                tokens[i] ? tokens[i] : "");
            if (written < 0) {
              break;
            }
            used += (size_t)written;
            if (used >= sizeof(extra_buf)) {
              extra_buf[sizeof(extra_buf) - 1] = '\0';
              break;
            }
          }
        }
        const char* extra = extra_buf[0] ? extra_buf : NULL;
        fpv_tg_open_templates_list_ans(
            service,
            callback,
            offset,
            chat_id,
            tokens[3],
            (int)prev_page,
            extra);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_template) == 0) {
      size_t template_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &template_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_template_editor(service, callback, template_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_template) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_prompt_add_template(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_del_template) == 0) {
      size_t template_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &template_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_delete_template(service, callback, template_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_send_template) == 0) {
      size_t template_index = 0;
      uint64_t chat_id = 0;
      size_t prev_page = 0;
      if (token_count > 4 &&
          fpv_tg_parse_size_value(tokens[1], &template_index) &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id) &&
          fpv_tg_parse_size_value(tokens[4], &prev_page)) {
        const char* extra1 = token_count > 5 ? tokens[5] : NULL;
        const char* extra2 = token_count > 6 ? tokens[6] : NULL;
        fpv_tg_send_template(
            service,
            callback,
            template_index,
            chat_id,
            tokens[3],
            (int)prev_page,
            extra1,
            extra2);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_send_fp_message) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 1 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_state_data_t data;
        memset(&data, 0, sizeof(data));
        data.chat_id = chat_id;
        data.username = token_count > 2 ? tokens[2] : NULL;
        fpv_tg_prompt_state_with_data(
            service,
            callback,
            FPV_TG_STATE_SEND_FP_MESSAGE,
            fpv_tg_loc(service, "enter_msg_text"),
            &data);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_back_to_reply) == 0) {
      uint64_t chat_id = 0;
      bool again = false;
      bool extend = false;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id) &&
          fpv_tg_parse_bool(tokens[3], &again)) {
        if (token_count > 4) {
          fpv_tg_parse_bool(tokens[4], &extend);
        }
        fpv_tg_update_reply_keyboard(
            service,
            callback,
            chat_id,
            token_count > 2 ? tokens[2] : NULL,
            again,
            extend);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_back_to_order) == 0) {
      uint64_t chat_id = 0;
      bool no_refund = false;
      if (token_count > 4 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_parse_bool(tokens[4], &no_refund);
        fpv_tg_update_order_keyboard(
            service,
            callback,
            tokens[3],
            chat_id,
            tokens[2],
            false,
            no_refund);
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_extend_chat) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 1 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_extend_chat_history(
            service,
            callback,
            chat_id,
            token_count > 2 ? tokens[2] : NULL);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_request_refund) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id)) {
        fpv_tg_update_order_keyboard(
            service,
            callback,
            tokens[1],
            chat_id,
            tokens[3],
            true,
            false);
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_refund_cancelled) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id)) {
        fpv_tg_update_order_keyboard(
            service,
            callback,
            tokens[1],
            chat_id,
            tokens[3],
            false,
            false);
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_refund_confirmed) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id)) {
        fpv_tg_confirm_refund(
            service,
            callback,
            tokens[1],
            chat_id,
            tokens[3]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_switch) == 0) {
      if (token_count > 2) {
        fpv_tg_toggle_setting(service, callback, tokens[1], tokens[2]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_switch_tg) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 2 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_toggle_notification(service, callback, chat_id, tokens[2]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_lang) == 0) {
      if (token_count > 1) {
        fpv_tg_switch_language(service, callback, tokens[1]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_cmd_list) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_ar_commands_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_cmd) == 0) {
      fpv_tg_prompt_state_from_callback(
          service,
          callback,
          FPV_TG_STATE_ADD_CMD,
          fpv_tg_loc(service, "ar_enter_new_cmd"));
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_cmd) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_ar_command_editor(
            service, callback, command_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_cmd_response) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_prompt_ar_edit_text(
            service,
            callback,
            FPV_TG_STATE_EDIT_CMD_RESPONSE,
            "v_edit_response_text",
            command_index,
            offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_cmd_notification) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_prompt_ar_edit_text(
            service,
            callback,
            FPV_TG_STATE_EDIT_CMD_NOTIFICATION,
            "v_edit_notification_text",
            command_index,
            offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_switch_cmd_notification) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_toggle_ar_notification(
            service, callback, command_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_del_cmd) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_delete_ar_command(service, callback, command_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_ad_lots) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_ad_lots_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_fp_lots) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_funpay_lots_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_ad_lot_manual) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.offset = (int)offset;
      fpv_tg_prompt_state_with_data(
          service,
          callback,
          FPV_TG_STATE_ADD_AD_TO_LOT_MANUAL,
          fpv_tg_loc(service, "copy_lot_name"),
          &data);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_ad_lot) == 0) {
      size_t fp_lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &fp_lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_add_ad_lot_from_funpay(service, callback, fp_lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_ad_lot) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_edit_ad_lot(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_lot_text) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_prompt_ad_edit_text(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_bind_products) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_state_data_t data;
        memset(&data, 0, sizeof(data));
        data.lot_index = (int)lot_index;
        data.offset = (int)offset;
        fpv_tg_prompt_state_with_data(
            service,
            callback,
            FPV_TG_STATE_BIND_PRODUCTS_FILE,
            fpv_tg_loc(service, "ad_link_gf"),
            &data);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_switch_lot) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 3 &&
          fpv_tg_parse_size_value(tokens[2], &lot_index) &&
          fpv_tg_parse_size_value(tokens[3], &offset)) {
        fpv_tg_toggle_lot_setting(service, callback, tokens[1], lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_test_auto_delivery) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_create_delivery_test(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_del_ad_lot) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_delete_ad_lot(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_update_funpay_lots) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_update_funpay_lots_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_products_list) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_products_files_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_products_file) == 0) {
      size_t file_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_products_file_editor(service, callback, file_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_create_products_file) == 0) {
      fpv_tg_prompt_state_from_callback(
          service,
          callback,
          FPV_TG_STATE_CREATE_PRODUCTS_FILE,
          fpv_tg_loc(service, "act_create_gf"));
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_products) == 0) {
      size_t file_index = 0;
      size_t element_index = 0;
      size_t offset = 0;
      size_t prev_page = 0;
      if (token_count > 4 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &element_index) &&
          fpv_tg_parse_size_value(tokens[3], &offset) &&
          fpv_tg_parse_size_value(tokens[4], &prev_page)) {
        fpv_tg_state_data_t data;
        memset(&data, 0, sizeof(data));
        data.file_index = (int)file_index;
        data.element_index = (int)element_index;
        data.offset = (int)offset;
        data.previous_page = (int)prev_page;
        fpv_tg_prompt_state_with_data(
            service,
            callback,
            FPV_TG_STATE_ADD_PRODUCTS_TO_FILE,
            fpv_tg_loc(service, "gf_send_new_goods"),
            &data);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_download_products_file) == 0) {
      size_t file_index = 0;
      if (token_count > 1 && fpv_tg_parse_size_value(tokens[1], &file_index)) {
        fpv_tg_send_products_file(service, callback, file_index);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_delete_products_file) == 0) {
      size_t file_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_confirm_delete_products_file(service, callback, file_index, offset, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_confirm_delete_products_file) == 0) {
      size_t file_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_confirm_delete_products_file(service, callback, file_index, offset, true);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
  }
  if (callback->data && callback->data[0]) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Unhandled callback: %s", callback->data);
    fpv_tg_log(service, FPV_LOG_WARNING, msg);
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
  fpv_free(data_copy);
}

void* fpv_tg_thread_main(void* context) {
  fpv_telegram_service_t* service = (fpv_telegram_service_t*)context;
  if (!service) {
    return NULL;
  }

  fpv_tg_log(service, FPV_LOG_INFO, "Telegram polling started.");
  fpv_tg_send_notification(service, fpv_tg_loc(service, "bot_started"),
                           NULL, "1", NULL, 0, false, true);

  while (service->running) {
    char* body = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_tg_buffer_append(&body, &length, &capacity, "{\"timeout\":25", 13);
    if (service->update_offset > 0) {
      char offset_buf[32];
      snprintf(offset_buf, sizeof(offset_buf), ",\"offset\":%" PRIu64, service->update_offset);
      fpv_tg_buffer_append(&body, &length, &capacity, offset_buf, strlen(offset_buf));
    }
    fpv_tg_buffer_append(&body, &length, &capacity,
                         ",\"allowed_updates\":[\"message\",\"callback_query\"]}",
                         55);

    fpv_json_value_t* root = NULL;
    fpv_result_t req = fpv_tg_api_request(
        service, "POST", "getUpdates", "application/json",
        body, strlen(body), &root);
    fpv_free(body);
    if (req != FPV_OK || !root) {
      fpv_tg_log_poll_error(service, req != FPV_OK ? req : FPV_ERR_PARSE);
      fpv_json_destroy(root);
      fpv_tg_sleep_ms(1500);
      continue;
    }
    service->last_poll_error = FPV_OK;
    service->last_poll_error_ms = 0;
    const fpv_json_value_t* result = fpv_json_object_get(root, "result");
    if (!result || !fpv_json_is_type(result, FPV_JSON_ARRAY)) {
      fpv_json_destroy(root);
      continue;
    }
    size_t count = fpv_json_array_size(result);
    for (size_t i = 0; i < count; i++) {
      const fpv_json_value_t* update = fpv_json_array_get(result, i);
      if (!update) {
        continue;
      }
      int64_t update_id = 0;
      fpv_json_number_to_int64(fpv_json_object_get(update, "update_id"), &update_id);
      if (update_id >= 0) {
        service->update_offset = (uint64_t)update_id + 1;
      }
      const fpv_json_value_t* message_val = fpv_json_object_get(update, "message");
      if (message_val) {
        fpv_tg_message_t message;
        if (fpv_tg_parse_message(message_val, &message)) {
          fpv_tg_handle_message(service, &message);
          fpv_tg_message_clear(&message);
        }
        continue;
      }
      const fpv_json_value_t* cb_val = fpv_json_object_get(update, "callback_query");
      if (cb_val) {
        fpv_tg_callback_t callback;
        if (fpv_tg_parse_callback(cb_val, &callback)) {
          fpv_tg_handle_callback(service, &callback);
          fpv_tg_callback_clear(&callback);
        }
      }
    }
    fpv_json_destroy(root);
  }
  return NULL;
}
