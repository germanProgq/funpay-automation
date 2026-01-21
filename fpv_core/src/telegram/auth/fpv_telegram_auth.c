/* FunPay Vertex Telegram authentication helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* fpv_tg_auth_error_key(fpv_result_t result) {
  switch (result) {
    case FPV_ERR_INVALID_ARGUMENT:
      return "auth_error_invalid_input";
    case FPV_ERR_NOT_FOUND:
      return "auth_error_not_found";
    case FPV_ERR_INVALID_STATE:
      return "auth_error_invalid_state";
    case FPV_ERR_IO:
      return "auth_error_storage";
    default:
      return "auth_error_default";
  }
}

void fpv_tg_send_auth_error(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    fpv_result_t result) {
  if (!service || chat_id == 0) {
    return;
  }
  const char* key = fpv_tg_auth_error_key(result);
  fpv_tg_send_message(service, chat_id, fpv_tg_loc(service, key), NULL, NULL);
}

fpv_result_t fpv_tg_resolve_user_context(
    fpv_telegram_service_t* service,
    const char* user_id,
    fpv_organization_t** out_org,
    fpv_team_t** out_team,
    fpv_role_t* out_role) {
  if (!service || !service->identity || !user_id || !user_id[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  return fpv_identity_resolve_user_context(
      service->identity,
      user_id,
      out_org,
      out_team,
      out_role);
}

char* fpv_tg_build_auth_menu_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  fpv_tg_keyboard_add_button(
      kb,
      fpv_tg_loc(service, "auth_menu_login"),
      fpv_tg_cbt_auth_login,
      NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_workspace_menu_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  fpv_tg_keyboard_add_button(
      kb,
      fpv_tg_loc(service, "auth_workspace_create"),
      fpv_tg_cbt_auth_create,
      NULL);
  fpv_tg_keyboard_add_button(
      kb,
      fpv_tg_loc(service, "auth_workspace_join"),
      fpv_tg_cbt_auth_join,
      NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_timezone_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  const char* timezones[] = {
      "UTC",
      "Europe/Moscow",
      "America/New_York",
      "Asia/Tokyo"};
  char cb_buf[96];
  for (size_t i = 0; i < sizeof(timezones) / sizeof(timezones[0]); i++) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%s",
             fpv_tg_cbt_auth_timezone, timezones[i]);
    fpv_tg_keyboard_add_button(kb, timezones[i], cb_buf, NULL);
    if ((i + 1) % 2 == 0) {
      fpv_tg_keyboard_row_end(kb);
    }
  }
  if ((sizeof(timezones) / sizeof(timezones[0])) % 2 != 0) {
    fpv_tg_keyboard_row_end(kb);
  }
  fpv_tg_keyboard_add_button(
      kb,
      fpv_tg_loc(service, "auth_custom"),
      fpv_tg_cbt_auth_timezone_custom,
      NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_currency_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  const char* currencies[] = {"RUB", "USD", "EUR", "GBP"};
  char cb_buf[64];
  for (size_t i = 0; i < sizeof(currencies) / sizeof(currencies[0]); i++) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%s",
             fpv_tg_cbt_auth_currency, currencies[i]);
    fpv_tg_keyboard_add_button(kb, currencies[i], cb_buf, NULL);
    if ((i + 1) % 2 == 0) {
      fpv_tg_keyboard_row_end(kb);
    }
  }
  if ((sizeof(currencies) / sizeof(currencies[0])) % 2 != 0) {
    fpv_tg_keyboard_row_end(kb);
  }
  fpv_tg_keyboard_add_button(
      kb,
      fpv_tg_loc(service, "auth_custom"),
      fpv_tg_cbt_auth_currency_custom,
      NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

void fpv_tg_send_login_menu(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  if (!service->identity) {
    fpv_tg_send_message(service, chat_id,
                        fpv_tg_loc(service, "auth_identity_unavailable"),
                        NULL, NULL);
    return;
  }
  char* reply_markup = fpv_tg_build_auth_menu_keyboard(service);
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "auth_menu_title"),
                      reply_markup, NULL);
  fpv_free(reply_markup);
}

void fpv_tg_send_workspace_menu(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  if (!service->identity) {
    fpv_tg_send_message(service, chat_id,
                        fpv_tg_loc(service, "auth_identity_unavailable"),
                        NULL, NULL);
    return;
  }
  char* reply_markup = fpv_tg_build_workspace_menu_keyboard(service);
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "auth_workspace_missing"),
                      reply_markup, NULL);
  fpv_free(reply_markup);
}

void fpv_tg_finalize_workspace_create(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t tg_user_id,
    const char* org_name,
    const char* timezone,
    const char* currency) {
  if (!service || chat_id == 0 || !org_name || !org_name[0]) {
    return;
  }
  if (!service->identity) {
    fpv_tg_send_message(service, chat_id,
                        fpv_tg_loc(service, "auth_identity_unavailable"),
                        NULL, NULL);
    return;
  }
  const char* user_id = fpv_tg_get_session_user_id(service, tg_user_id);
  if (!user_id || !user_id[0]) {
    fpv_tg_send_login_menu(service, chat_id);
    return;
  }
  const char* tz = timezone && timezone[0] ? timezone : "UTC";
  const char* cur = currency && currency[0] ? currency : "RUB";
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_user_role_t* owner_role = NULL;
  fpv_result_t result = fpv_identity_create_organization(
      service->identity,
      org_name,
      tz,
      cur,
      NULL,
      user_id,
      &org,
      &team,
      &owner_role);
  fpv_organization_destroy(org);
  fpv_team_destroy(team);
  fpv_user_role_destroy(owner_role);
  if (result != FPV_OK) {
    fpv_tg_send_auth_error(service, chat_id, result);
    fpv_tg_send_workspace_menu(service, chat_id);
    return;
  }
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "auth_workspace_created"),
                      NULL, NULL);
  fpv_tg_send_menu(service, chat_id);
}

bool fpv_tg_handle_auth_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return false;
  }
  fpv_tg_user_state_t* state =
      fpv_tg_get_state(service, message->chat_id, message->from_id);
  if (!state) {
    return false;
  }
  switch (state->type) {
    case FPV_TG_STATE_AUTH_LOGIN_EMAIL:
    case FPV_TG_STATE_AUTH_LOGIN_PASSWORD:
    case FPV_TG_STATE_AUTH_JOIN_TOKEN:
    case FPV_TG_STATE_AUTH_CREATE_ORG_NAME:
    case FPV_TG_STATE_AUTH_CREATE_TIMEZONE:
    case FPV_TG_STATE_AUTH_CREATE_TIMEZONE_CUSTOM:
    case FPV_TG_STATE_AUTH_CREATE_CURRENCY:
    case FPV_TG_STATE_AUTH_CREATE_CURRENCY_CUSTOM:
      break;
    default:
      return false;
  }

  if (!message->text || !message->text[0]) {
    return true;
  }

  switch (state->type) {
    case FPV_TG_STATE_AUTH_LOGIN_EMAIL: {
      char* email = fpv_tg_normalize_email(message->text);
      if (!email || !email[0]) {
        fpv_free(email);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
      char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
      int prompt_id = 0;
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_enter_password"),
                          reply_markup, &prompt_id);
      fpv_free(reply_markup);
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.email = email;
      fpv_tg_set_state(service, message->chat_id, message->from_id,
                       prompt_id, FPV_TG_STATE_AUTH_LOGIN_PASSWORD, &data);
      fpv_free(email);
      return true;
    }
    case FPV_TG_STATE_AUTH_LOGIN_PASSWORD: {
      if (!service->identity) {
        fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
        fpv_tg_send_message(service, message->chat_id,
                            fpv_tg_loc(service, "auth_identity_unavailable"),
                            NULL, NULL);
        return true;
      }
      char* email = state->data.email ? fpv_strdup(state->data.email) : NULL;
      char* password = fpv_tg_trim_copy(message->text);
      if (!email || !email[0] || !password || !password[0]) {
        fpv_free(email);
        fpv_free(password);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
      fpv_user_t* user = NULL;
      fpv_result_t result =
          fpv_identity_authenticate(service->identity, email, password, &user);
      fpv_free(email);
      fpv_free(password);
      if (result != FPV_OK || !user) {
        fpv_tg_increment_attempt(service, message->from_id);
        fpv_tg_send_auth_error(service, message->chat_id, result);
        fpv_user_destroy(user);
        fpv_tg_send_login_menu(service, message->chat_id);
        return true;
      }
      if (!fpv_tg_set_session(service, message->from_id, user->id)) {
        fpv_user_destroy(user);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_OUT_OF_MEMORY);
        return true;
      }
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_login_success"),
                          NULL, NULL);
      fpv_organization_t* org = NULL;
      fpv_team_t* team = NULL;
      fpv_role_t role = FPV_ROLE_UNKNOWN;
      result = fpv_tg_resolve_user_context(service, user->id, &org, &team, &role);
      fpv_organization_destroy(org);
      fpv_team_destroy(team);
      fpv_user_destroy(user);
      if (result == FPV_OK) {
        fpv_tg_send_menu(service, message->chat_id);
      } else if (result == FPV_ERR_NOT_FOUND) {
        fpv_tg_send_workspace_menu(service, message->chat_id);
      } else {
        fpv_tg_send_auth_error(service, message->chat_id, result);
      }
      return true;
    }
    case FPV_TG_STATE_AUTH_JOIN_TOKEN: {
      if (!service->identity) {
        fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
        fpv_tg_send_message(service, message->chat_id,
                            fpv_tg_loc(service, "auth_identity_unavailable"),
                            NULL, NULL);
        return true;
      }
      char* token = fpv_tg_trim_copy(message->text);
      if (!token || !token[0]) {
        fpv_free(token);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
      const char* user_id = fpv_tg_get_session_user_id(
          service, message->from_id);
      if (!user_id || !user_id[0]) {
        fpv_free(token);
        fpv_tg_send_login_menu(service, message->chat_id);
        return true;
      }
      fpv_identity_invite_t* invite = NULL;
      fpv_result_t result =
          fpv_identity_validate_invite(service->identity, token, &invite);
      if (result != FPV_OK || !invite) {
        fpv_tg_send_auth_error(service, message->chat_id, result);
        fpv_identity_invite_destroy(invite);
        fpv_free(token);
        fpv_tg_send_workspace_menu(service, message->chat_id);
        return true;
      }
      fpv_user_t* user = NULL;
      result = fpv_identity_get_user(service->identity, user_id, &user);
      if (result != FPV_OK || !user) {
        fpv_tg_send_auth_error(service, message->chat_id, result);
        fpv_identity_invite_destroy(invite);
        fpv_user_destroy(user);
        fpv_free(token);
        return true;
      }
      char* invite_email = fpv_tg_normalize_email(invite->email);
      char* user_email = fpv_tg_normalize_email(user->email);
      bool email_ok = invite_email && user_email &&
          strcmp(invite_email, user_email) == 0;
      if (!email_ok) {
        char* text = fpv_tg_loc_format(
            service,
            "auth_invite_email_mismatch",
            (const char*[]){invite->email ? invite->email : "",
                            user->email ? user->email : ""},
            2);
        fpv_tg_send_message(service, message->chat_id,
                            text ? text : "", NULL, NULL);
        fpv_free(text);
        fpv_identity_invite_destroy(invite);
        fpv_user_destroy(user);
        fpv_free(invite_email);
        fpv_free(user_email);
        fpv_free(token);
        fpv_tg_send_workspace_menu(service, message->chat_id);
        return true;
      }
      fpv_identity_invite_t* accepted = NULL;
      fpv_user_role_t* assigned = NULL;
      result = fpv_identity_accept_invite(
          service->identity, token, user_id, &accepted, &assigned);
      fpv_user_role_destroy(assigned);
      fpv_identity_invite_destroy(accepted);
      fpv_identity_invite_destroy(invite);
      fpv_user_destroy(user);
      fpv_free(invite_email);
      fpv_free(user_email);
      fpv_free(token);
      if (result != FPV_OK) {
        fpv_tg_send_auth_error(service, message->chat_id, result);
        fpv_tg_send_workspace_menu(service, message->chat_id);
        return true;
      }
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_invite_accepted"),
                          NULL, NULL);
      fpv_tg_send_menu(service, message->chat_id);
      return true;
    }
    case FPV_TG_STATE_AUTH_CREATE_ORG_NAME: {
      char* org_name = fpv_tg_trim_copy(message->text);
      if (!org_name || !org_name[0]) {
        fpv_free(org_name);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
      char* reply_markup = fpv_tg_build_timezone_keyboard(service);
      int prompt_id = 0;
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_enter_timezone"),
                          reply_markup, &prompt_id);
      fpv_free(reply_markup);
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.org_name = org_name;
      fpv_tg_set_state(service, message->chat_id, message->from_id,
                       prompt_id, FPV_TG_STATE_AUTH_CREATE_TIMEZONE, &data);
      fpv_free(org_name);
      return true;
    }
    case FPV_TG_STATE_AUTH_CREATE_TIMEZONE:
    case FPV_TG_STATE_AUTH_CREATE_CURRENCY: {
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_use_buttons"),
                          NULL, NULL);
      return true;
    }
    case FPV_TG_STATE_AUTH_CREATE_TIMEZONE_CUSTOM: {
      char* timezone = fpv_tg_trim_copy(message->text);
      if (!timezone || !timezone[0]) {
        fpv_free(timezone);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      char* org_name = state->data.org_name ? fpv_strdup(state->data.org_name) : NULL;
      if (!org_name || !org_name[0]) {
        fpv_free(timezone);
        fpv_free(org_name);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
      char* reply_markup = fpv_tg_build_currency_keyboard(service);
      int prompt_id = 0;
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "auth_enter_currency"),
                          reply_markup, &prompt_id);
      fpv_free(reply_markup);
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.org_name = org_name;
      data.timezone = timezone;
      fpv_tg_set_state(service, message->chat_id, message->from_id,
                       prompt_id, FPV_TG_STATE_AUTH_CREATE_CURRENCY, &data);
      fpv_free(org_name);
      fpv_free(timezone);
      return true;
    }
    case FPV_TG_STATE_AUTH_CREATE_CURRENCY_CUSTOM: {
      char* currency = fpv_tg_trim_copy(message->text);
      if (!currency || !currency[0]) {
        fpv_free(currency);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_uppercase_ascii(currency);
      char* org_name = state->data.org_name ? fpv_strdup(state->data.org_name) : NULL;
      char* timezone = state->data.timezone ? fpv_strdup(state->data.timezone) : NULL;
      if (!org_name || !org_name[0]) {
        fpv_free(currency);
        fpv_free(org_name);
        fpv_free(timezone);
        fpv_tg_send_auth_error(service, message->chat_id, FPV_ERR_INVALID_ARGUMENT);
        return true;
      }
      fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
      fpv_tg_finalize_workspace_create(
          service,
          message->chat_id,
          message->from_id,
          org_name,
          timezone,
          currency);
      fpv_free(org_name);
      fpv_free(timezone);
      fpv_free(currency);
      return true;
    }
    default:
      break;
  }
  return false;
}

bool fpv_tg_handle_auth_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback) {
  if (!service || !callback || !callback->data) {
    return false;
  }
  if (strcmp(callback->data, fpv_tg_cbt_clear_state) == 0) {
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
    return true;
  }

  if (strcmp(callback->data, fpv_tg_cbt_auth_login) == 0) {
    if (!service->identity) {
      fpv_tg_send_message(service, callback->chat_id,
                          fpv_tg_loc(service, "auth_identity_unavailable"),
                          NULL, NULL);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_AUTH_LOGIN_EMAIL,
        fpv_tg_loc(service, "auth_enter_email"));
    return true;
  }

  const char* session_user_id =
      fpv_tg_get_session_user_id(service, callback->from_id);

  if (strcmp(callback->data, fpv_tg_cbt_auth_join) == 0 ||
      strcmp(callback->data, fpv_tg_cbt_auth_create) == 0) {
    if (!session_user_id || !session_user_id[0]) {
      fpv_tg_send_login_menu(service, callback->chat_id);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    if (!service->identity) {
      fpv_tg_send_message(service, callback->chat_id,
                          fpv_tg_loc(service, "auth_identity_unavailable"),
                          NULL, NULL);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    fpv_organization_t* org = NULL;
    fpv_team_t* team = NULL;
    fpv_role_t role = FPV_ROLE_UNKNOWN;
    fpv_result_t result =
        fpv_tg_resolve_user_context(service, session_user_id, &org, &team, &role);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    if (result == FPV_OK) {
      fpv_tg_send_menu(service, callback->chat_id);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    if (result != FPV_ERR_NOT_FOUND) {
      fpv_tg_send_auth_error(service, callback->chat_id, result);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    if (strcmp(callback->data, fpv_tg_cbt_auth_join) == 0) {
      fpv_tg_prompt_state_from_callback(
          service,
          callback,
          FPV_TG_STATE_AUTH_JOIN_TOKEN,
          fpv_tg_loc(service, "auth_enter_invite"));
    } else {
      fpv_tg_prompt_state_from_callback(
          service,
          callback,
          FPV_TG_STATE_AUTH_CREATE_ORG_NAME,
          fpv_tg_loc(service, "auth_enter_workspace_name"));
    }
    return true;
  }

  char* data_copy = fpv_strdup(callback->data);
  if (!data_copy) {
    return false;
  }
  char* tokens[4] = {0};
  size_t token_count = fpv_tg_split_tokens(data_copy, tokens, 4);
  if (token_count > 0 && strcmp(tokens[0], fpv_tg_cbt_auth_timezone) == 0) {
    if (!session_user_id || !session_user_id[0]) {
      fpv_tg_send_login_menu(service, callback->chat_id);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return true;
    }
    fpv_tg_user_state_t* state =
        fpv_tg_get_state(service, callback->chat_id, callback->from_id);
    if (!state || state->type != FPV_TG_STATE_AUTH_CREATE_TIMEZONE ||
        token_count < 2 || !tokens[1] || !tokens[1][0]) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return true;
    }
    char* org_name = state->data.org_name ? fpv_strdup(state->data.org_name) : NULL;
    char* timezone = fpv_strdup(tokens[1]);
    if (!org_name || !timezone) {
      fpv_free(org_name);
      fpv_free(timezone);
      fpv_tg_send_auth_error(service, callback->chat_id, FPV_ERR_OUT_OF_MEMORY);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return true;
    }
    fpv_tg_clear_state(service, callback->chat_id, callback->from_id, true);
    char* reply_markup = fpv_tg_build_currency_keyboard(service);
    int prompt_id = 0;
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "auth_enter_currency"),
                        reply_markup, &prompt_id);
    fpv_free(reply_markup);
    fpv_tg_state_data_t data;
    memset(&data, 0, sizeof(data));
    data.org_name = org_name;
    data.timezone = timezone;
    fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                     prompt_id, FPV_TG_STATE_AUTH_CREATE_CURRENCY, &data);
    fpv_free(org_name);
    fpv_free(timezone);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    fpv_free(data_copy);
    return true;
  }
  if (token_count > 0 &&
      strcmp(tokens[0], fpv_tg_cbt_auth_currency) == 0) {
    if (!session_user_id || !session_user_id[0]) {
      fpv_tg_send_login_menu(service, callback->chat_id);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return true;
    }
    fpv_tg_user_state_t* state =
        fpv_tg_get_state(service, callback->chat_id, callback->from_id);
    if (!state || state->type != FPV_TG_STATE_AUTH_CREATE_CURRENCY ||
        token_count < 2 || !tokens[1] || !tokens[1][0]) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return true;
    }
    char* org_name = state->data.org_name ? fpv_strdup(state->data.org_name) : NULL;
    char* timezone = state->data.timezone ? fpv_strdup(state->data.timezone) : NULL;
    char* currency = fpv_strdup(tokens[1]);
    if (!org_name || !currency) {
      fpv_free(org_name);
      fpv_free(timezone);
      fpv_free(currency);
      fpv_tg_send_auth_error(service, callback->chat_id, FPV_ERR_OUT_OF_MEMORY);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return true;
    }
    fpv_tg_uppercase_ascii(currency);
    fpv_tg_clear_state(service, callback->chat_id, callback->from_id, true);
    fpv_tg_finalize_workspace_create(
        service,
        callback->chat_id,
        callback->from_id,
        org_name,
        timezone,
        currency);
    fpv_free(org_name);
    fpv_free(timezone);
    fpv_free(currency);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    fpv_free(data_copy);
    return true;
  }
  fpv_free(data_copy);

  if (strcmp(callback->data, fpv_tg_cbt_auth_timezone_custom) == 0 ||
      strcmp(callback->data, fpv_tg_cbt_auth_currency_custom) == 0) {
    if (!session_user_id || !session_user_id[0]) {
      fpv_tg_send_login_menu(service, callback->chat_id);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    fpv_tg_user_state_t* state =
        fpv_tg_get_state(service, callback->chat_id, callback->from_id);
    if (!state ||
        (strcmp(callback->data, fpv_tg_cbt_auth_timezone_custom) == 0 &&
         state->type != FPV_TG_STATE_AUTH_CREATE_TIMEZONE) ||
        (strcmp(callback->data, fpv_tg_cbt_auth_currency_custom) == 0 &&
         state->type != FPV_TG_STATE_AUTH_CREATE_CURRENCY)) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    char* org_name = state->data.org_name ? fpv_strdup(state->data.org_name) : NULL;
    char* timezone = state->data.timezone ? fpv_strdup(state->data.timezone) : NULL;
    if (!org_name || !org_name[0]) {
      fpv_free(org_name);
      fpv_free(timezone);
      fpv_tg_send_auth_error(service, callback->chat_id, FPV_ERR_INVALID_ARGUMENT);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return true;
    }
    fpv_tg_clear_state(service, callback->chat_id, callback->from_id, true);
    char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
    int prompt_id = 0;
    if (strcmp(callback->data, fpv_tg_cbt_auth_timezone_custom) == 0) {
      fpv_tg_send_message(service, callback->chat_id,
                          fpv_tg_loc(service, "auth_enter_timezone_manual"),
                          reply_markup, &prompt_id);
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.org_name = org_name;
      fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                       prompt_id, FPV_TG_STATE_AUTH_CREATE_TIMEZONE_CUSTOM, &data);
    } else {
      fpv_tg_send_message(service, callback->chat_id,
                          fpv_tg_loc(service, "auth_enter_currency_manual"),
                          reply_markup, &prompt_id);
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.org_name = org_name;
      data.timezone = timezone;
      fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                       prompt_id, FPV_TG_STATE_AUTH_CREATE_CURRENCY_CUSTOM, &data);
    }
    fpv_free(reply_markup);
    fpv_free(org_name);
    fpv_free(timezone);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return true;
  }

  return false;
}
