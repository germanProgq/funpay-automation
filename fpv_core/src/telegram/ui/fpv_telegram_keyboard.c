/* FunPay Vertex Telegram keyboard helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <stdlib.h>
#include <string.h>

void fpv_tg_keyboard_destroy(fpv_tg_keyboard_t* kb) {
  if (!kb) {
    return;
  }
  fpv_free(kb->buffer);
  fpv_free(kb);
}

fpv_tg_keyboard_t* fpv_tg_keyboard_create(bool inline_keyboard, bool resize) {
  fpv_tg_keyboard_t* kb = (fpv_tg_keyboard_t*)calloc(1, sizeof(*kb));
  if (!kb) {
    return NULL;
  }
  kb->inline_keyboard = inline_keyboard;
  kb->resize = resize;
  if (inline_keyboard) {
    const char* prefix = "{\"inline_keyboard\":[";
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                              prefix, strlen(prefix))) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
  } else {
    const char* prefix = "{\"keyboard\":[";
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                              prefix, strlen(prefix))) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
  }
  kb->row_open = false;
  kb->first_in_row = true;
  kb->has_rows = false;
  return kb;
}

bool fpv_tg_keyboard_row_begin(fpv_tg_keyboard_t* kb) {
  if (!kb) {
    return false;
  }
  if (kb->row_open) {
    return true;
  }
  if (kb->has_rows) {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, ",", 1)) {
      return false;
    }
  }
  if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "[", 1)) {
    return false;
  }
  kb->row_open = true;
  kb->first_in_row = true;
  return true;
}

bool fpv_tg_keyboard_row_end(fpv_tg_keyboard_t* kb) {
  if (!kb || !kb->row_open) {
    return false;
  }
  if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "]", 1)) {
    return false;
  }
  kb->row_open = false;
  kb->has_rows = true;
  return true;
}

bool fpv_tg_keyboard_add_button(
    fpv_tg_keyboard_t* kb,
    const char* text,
    const char* callback_data,
    const char* url) {
  if (!kb || !text) {
    return false;
  }
  if (!fpv_tg_keyboard_row_begin(kb)) {
    return false;
  }
  if (!kb->first_in_row) {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, ",", 1)) {
      return false;
    }
  }
  kb->first_in_row = false;
  if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "{\"text\":\"",
                            strlen("{\"text\":\""))) {
    return false;
  }
  if (!fpv_tg_json_escape_append(&kb->buffer, &kb->length, &kb->capacity, text)) {
    return false;
  }
  if (kb->inline_keyboard) {
    if (url && url[0]) {
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "\",\"url\":\"",
                                strlen("\",\"url\":\""))) {
        return false;
      }
      if (!fpv_tg_json_escape_append(&kb->buffer, &kb->length, &kb->capacity, url)) {
        return false;
      }
    } else {
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                                "\",\"callback_data\":\"",
                                strlen("\",\"callback_data\":\""))) {
        return false;
      }
      if (!fpv_tg_json_escape_append(&kb->buffer, &kb->length, &kb->capacity, callback_data ? callback_data : "")) {
        return false;
      }
    }
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "\"}",
                              strlen("\"}"))) {
      return false;
    }
  } else {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "\"}",
                              strlen("\"}"))) {
      return false;
    }
  }
  return true;
}

char* fpv_tg_keyboard_finalize(fpv_tg_keyboard_t* kb, bool remove_keyboard) {
  if (!kb) {
    return NULL;
  }
  if (kb->row_open) {
    fpv_tg_keyboard_row_end(kb);
  }
  if (kb->inline_keyboard) {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "]}", 2)) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
  } else {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "]", 1)) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
    if (remove_keyboard) {
      const char* suffix = ",\"remove_keyboard\":true}";
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                                suffix, strlen(suffix))) {
        fpv_tg_keyboard_destroy(kb);
        return NULL;
      }
    } else if (kb->resize) {
      const char* suffix = ",\"resize_keyboard\":true}";
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                                suffix, strlen(suffix))) {
        fpv_tg_keyboard_destroy(kb);
        return NULL;
      }
    } else {
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "}", 1)) {
        fpv_tg_keyboard_destroy(kb);
        return NULL;
      }
    }
  }
  char* result = kb->buffer;
  kb->buffer = NULL;
  fpv_tg_keyboard_destroy(kb);
  return result;
}

char* fpv_tg_build_clear_state_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_cancel"),
                             fpv_tg_cbt_clear_state, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_keyboard_remove(void) {
  return fpv_strdup("{\"remove_keyboard\":true}");
}
