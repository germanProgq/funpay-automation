#include "events/app_events.h"

#include "helpers/app_helpers.h"
#include "lists/app_lists.h"
#include "settings/app_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

enum {
  CHAT_REFRESH_TICK_MS = 2000,
  CHAT_REFRESH_ACTIVE_INTERVAL_MS = 8000,
  CHAT_REFRESH_WARMUP_TARGET_CYCLE_MS = 120000,
  CHAT_REFRESH_WARMUP_MIN_INTERVAL_MS = 4000,
  CHAT_REFRESH_WARMUP_MAX_INTERVAL_MS = 20000,
  CHAT_REFRESH_BACKGROUND_TARGET_CYCLE_MS = 600000,
  CHAT_REFRESH_BACKGROUND_MIN_INTERVAL_MS = 15000,
  CHAT_REFRESH_BACKGROUND_MAX_INTERVAL_MS = 120000
};

void handle_event(AppContext* context, const fpv_event_t* event) {
  if (!context || !event) {
    return;
  }

  switch (event->type) {
    case FPV_EVENT_CORE_STATUS: {
      const fpv_core_status_event_t* payload =
          fpv_event_as_core_status(event);
      if (payload) {
        update_status(context, payload->status, payload->detail);
      }
      break;
    }
    case FPV_EVENT_LOG: {
      const fpv_log_entry_t* payload = fpv_event_as_log(event);
      if (payload) {
        append_log_row(
            context,
            log_level_label(payload->level),
            payload->message ? payload->message : "",
            event->timestamp_ms);
      }
      break;
    }
    case FPV_EVENT_NOTIFICATION: {
      const fpv_notification_t* payload = fpv_event_as_notification(event);
      if (payload) {
        append_log_row(
            context,
            notification_severity_label(payload->severity),
            payload->message ? payload->message : "",
            event->timestamp_ms);
      }
      break;
    }
    case FPV_EVENT_CHAT: {
      const fpv_chat_t* payload = fpv_event_as_chat(event);
      if (payload && payload->id) {
        fpv_chat_t* clone = fpv_chat_clone(payload);
        g_hash_table_replace(
            context->chats,
            g_strdup(payload->id),
            clone);
        refresh_chat_list(context);
        if (context->chat_store && context->current_org &&
            context->current_org->id) {
          fpv_chat_store_upsert_chat(
              context->chat_store,
              context->current_org->id,
              payload,
              event->timestamp_ms);
        }
      }
      break;
    }
    case FPV_EVENT_MESSAGE: {
      const fpv_message_t* payload = fpv_event_as_message(event);
      if (payload && payload->chat_id) {
        fpv_message_t* clone = fpv_message_clone(payload);
        GPtrArray* list = (GPtrArray*)g_hash_table_lookup(
            context->messages_by_chat,
            payload->chat_id);
        if (!list) {
          list = g_ptr_array_new_with_free_func(
              (GDestroyNotify)fpv_message_destroy);
          g_hash_table_replace(
              context->messages_by_chat,
              g_strdup(payload->chat_id),
              list);
        }
        g_ptr_array_add(list, clone);
        while (list->len > 200) {
          g_ptr_array_remove_index(list, 0);
        }
        if (context->active_chat_id &&
            strcmp(context->active_chat_id, payload->chat_id) == 0) {
          refresh_message_list(context);
        }
        if (context->chat_store && context->current_org &&
            context->current_org->id && payload->id) {
          fpv_chat_store_upsert_message(
              context->chat_store,
              context->current_org->id,
              payload);
        }
      }
      break;
    }
    case FPV_EVENT_ORDER: {
      const fpv_order_t* payload = fpv_event_as_order(event);
      if (payload && payload->id) {
        fpv_order_t* clone = fpv_order_clone(payload);
        g_hash_table_replace(
            context->orders,
            g_strdup(payload->id),
            clone);
        refresh_order_list(context);
      }
      break;
    }
    case FPV_EVENT_LOT: {
      const fpv_lot_t* payload = fpv_event_as_lot(event);
      if (payload && payload->id) {
        fpv_lot_t* clone = fpv_lot_clone(payload);
        g_hash_table_replace(
            context->lots,
            g_strdup(payload->id),
            clone);
        refresh_lot_list(context);
        refresh_auto_delivery_list(context);
      }
      break;
    }
    case FPV_EVENT_PLUGIN: {
      const fpv_plugin_t* payload = fpv_event_as_plugin(event);
      if (payload && payload->id) {
        fpv_plugin_t* clone = fpv_plugin_clone(payload);
        g_hash_table_replace(
            context->plugins,
            g_strdup(payload->id),
            clone);
        refresh_plugin_list(context);
      }
      break;
    }
    default:
      break;
  }

  refresh_metrics(context);
}

gboolean poll_events(gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || context->closing || !context->core || !context->bus) {
    return G_SOURCE_REMOVE;
  }

  if (fpv_core_status(context->core) == FPV_CORE_RUNNING) {
    fpv_core_tick(
        context->core,
        (uint64_t)(g_get_real_time() / 1000ULL));
  }

  fpv_event_batch_t batch = fpv_event_bus_drain(context->bus);
  if (!batch.events || batch.count == 0) {
    fpv_event_batch_destroy(&batch);
    return G_SOURCE_CONTINUE;
  }

  for (size_t i = 0; i < batch.count; i++) {
    handle_event(context, batch.events[i]);
  }

  fpv_event_batch_destroy(&batch);
  return G_SOURCE_CONTINUE;
}

void start_core(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->core) {
    return;
  }
  if (context->running) {
    return;
  }

  const uint64_t now_ms = (uint64_t)(g_get_real_time() / 1000ULL);
  fpv_result_t result = fpv_core_start(context->core, now_ms);
  if (result != FPV_OK) {
    append_log_row(context, "error", "Core start failed.", now_ms);
    update_status(context, FPV_CORE_ERROR, "start failed");
    return;
  }
  start_lots_refresh(context, FALSE);
}

void stop_core(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->core) {
    return;
  }
  if (!context->running) {
    return;
  }

  const uint64_t now_ms = (uint64_t)(g_get_real_time() / 1000ULL);
  fpv_result_t result = fpv_core_stop(context->core, now_ms);
  if (result != FPV_OK) {
    append_log_row(context, "error", "Core stop failed.", now_ms);
    update_status(context, FPV_CORE_ERROR, "stop failed");
  }
}

typedef struct ChatHistoryTask {
  AppContext* context;
  gchar* chat_id;
  gchar* chat_name;
  gchar* org_id;
  gboolean load_cached;
  gboolean update_memory;
  gboolean release_refresh;
} ChatHistoryTask;

typedef struct ChatCacheTask {
  AppContext* context;
  gchar* chat_id;
} ChatCacheTask;

gboolean chat_history_cache_complete(gpointer data) {
  ChatCacheTask* task = (ChatCacheTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->context->closing) {
    g_free(task->chat_id);
    g_free(task);
    return G_SOURCE_REMOVE;
  }
  if (task->chat_id && task->context->active_chat_id &&
      strcmp(task->context->active_chat_id, task->chat_id) == 0) {
    refresh_message_list(task->context);
  }
  refresh_metrics(task->context);
  g_free(task->chat_id);
  g_free(task);
  return G_SOURCE_REMOVE;
}

gboolean chat_history_complete(gpointer data) {
  ChatHistoryTask* task = (ChatHistoryTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->release_refresh) {
    task->context->chat_refresh_in_flight = FALSE;
  }
  if (task->context->closing) {
    g_free(task->chat_id);
    g_free(task->chat_name);
    g_free(task->org_id);
    g_free(task);
    return G_SOURCE_REMOVE;
  }
  if (task->chat_id && task->context->active_chat_id &&
      strcmp(task->context->active_chat_id, task->chat_id) == 0) {
    refresh_message_list(task->context);
  }
  refresh_metrics(task->context);
  g_free(task->chat_id);
  g_free(task->chat_name);
  g_free(task->org_id);
  g_free(task);
  return G_SOURCE_REMOVE;
}

gpointer chat_history_thread(gpointer data) {
  ChatHistoryTask* task = (ChatHistoryTask*)data;
  if (!task || !task->context) {
    if (task) {
      g_main_context_invoke(NULL, chat_history_complete, task);
    }
    return NULL;
  }

  if (task->load_cached && task->context->chat_store &&
      task->org_id && task->org_id[0] && task->chat_id && task->chat_id[0]) {
    fpv_message_t** cached = NULL;
    size_t cached_count = 0;
    fpv_result_t cache_result = fpv_chat_store_load_messages(
        task->context->chat_store,
        task->org_id,
        task->chat_id,
        200,
        &cached,
        &cached_count);
    if (cache_result == FPV_OK && cached_count > 0) {
      GPtrArray* list = g_ptr_array_new_with_free_func(
          (GDestroyNotify)fpv_message_destroy);
      if (list) {
        for (size_t i = 0; i < cached_count; i++) {
          g_ptr_array_add(list, cached[i]);
        }
        g_hash_table_replace(
            task->context->messages_by_chat,
            g_strdup(task->chat_id),
            list);
        ChatCacheTask* cache_task = (ChatCacheTask*)g_new0(ChatCacheTask, 1);
        if (cache_task) {
          cache_task->context = task->context;
          cache_task->chat_id = g_strdup(task->chat_id);
          if (cache_task->chat_id) {
            g_main_context_invoke(
                NULL, chat_history_cache_complete, cache_task);
          } else {
            g_free(cache_task);
          }
        }
      } else {
        for (size_t i = 0; i < cached_count; i++) {
          fpv_message_destroy(cached[i]);
        }
      }
    } else if (cached_count > 0) {
      for (size_t i = 0; i < cached_count; i++) {
        fpv_message_destroy(cached[i]);
      }
    }
    if (cached) {
      free(cached);
    }
  }

  if (!task->context->core) {
    g_main_context_invoke(NULL, chat_history_complete, task);
    return NULL;
  }

  if (!task->chat_id || !task->chat_id[0]) {
    g_main_context_invoke(NULL, chat_history_complete, task);
    return NULL;
  }

  guint64 chat_id = g_ascii_strtoull(task->chat_id, NULL, 10);
  if (chat_id == 0) {
    g_main_context_invoke(NULL, chat_history_complete, task);
    return NULL;
  }
  fpv_message_t** messages = NULL;
  size_t count = 0;
  fpv_result_t result = fpv_core_fetch_chat_history(
      task->context->core,
      chat_id,
      task->chat_name,
      &messages,
      &count);
  if (result == FPV_OK) {
    gboolean update_memory = task->update_memory;
    if (!update_memory && task->context->active_chat_id &&
        strcmp(task->context->active_chat_id, task->chat_id) == 0) {
      update_memory = TRUE;
    }
    if (task->context->chat_store && task->org_id && task->org_id[0] &&
        messages && count > 0) {
      fpv_chat_store_upsert_messages(
          task->context->chat_store,
          task->org_id,
          (const fpv_message_t* const*)messages,
          count);
    }
    if (update_memory) {
      GPtrArray* list = g_ptr_array_new_with_free_func(
          (GDestroyNotify)fpv_message_destroy);
      if (list) {
        for (size_t i = 0; i < count; i++) {
          g_ptr_array_add(list, messages[i]);
        }
        g_hash_table_replace(
            task->context->messages_by_chat,
            g_strdup(task->chat_id),
            list);
      } else {
        for (size_t i = 0; i < count; i++) {
          fpv_message_destroy(messages[i]);
        }
      }
    } else {
      for (size_t i = 0; i < count; i++) {
        fpv_message_destroy(messages[i]);
      }
    }
  }
  if (messages) {
    free(messages);
  }
  g_main_context_invoke(NULL, chat_history_complete, task);
  return NULL;
}

static void chat_refresh_queue_clear(AppContext* context) {
  if (!context) {
    return;
  }
  if (context->chat_refresh_queue) {
    g_ptr_array_free(context->chat_refresh_queue, TRUE);
    context->chat_refresh_queue = NULL;
  }
  context->chat_refresh_index = 0;
}

static void chat_refresh_queue_rebuild(AppContext* context) {
  if (!context || !context->chats) {
    return;
  }

  chat_refresh_queue_clear(context);

  GPtrArray* queue = g_ptr_array_new_with_free_func(g_free);
  if (!queue) {
    return;
  }

  GHashTableIter iter;
  gpointer key = NULL;
  g_hash_table_iter_init(&iter, context->chats);
  while (g_hash_table_iter_next(&iter, &key, NULL)) {
    const char* chat_id = (const char*)key;
    if (chat_id && chat_id[0]) {
      g_ptr_array_add(queue, g_strdup(chat_id));
    }
  }

  if (queue->len == 0) {
    g_ptr_array_free(queue, TRUE);
    return;
  }

  g_ptr_array_sort(queue, (GCompareFunc)g_strcmp0);
  context->chat_refresh_queue = queue;
  context->chat_refresh_index = 0;
}

static guint64 chat_refresh_interval_ms(
    const AppContext* context,
    gboolean warmup) {
  size_t chat_count = 0;
  if (context && context->chats) {
    chat_count = g_hash_table_size(context->chats);
  }
  guint64 target = warmup
      ? CHAT_REFRESH_WARMUP_TARGET_CYCLE_MS
      : CHAT_REFRESH_BACKGROUND_TARGET_CYCLE_MS;
  guint64 min_interval = warmup
      ? CHAT_REFRESH_WARMUP_MIN_INTERVAL_MS
      : CHAT_REFRESH_BACKGROUND_MIN_INTERVAL_MS;
  guint64 max_interval = warmup
      ? CHAT_REFRESH_WARMUP_MAX_INTERVAL_MS
      : CHAT_REFRESH_BACKGROUND_MAX_INTERVAL_MS;
  if (chat_count == 0) {
    return max_interval;
  }
  guint64 interval = target / chat_count;
  if (interval < min_interval) {
    interval = min_interval;
  }
  if (interval > max_interval) {
    interval = max_interval;
  }
  return interval;
}

static const gchar* chat_refresh_pick_next(AppContext* context) {
  if (!context || !context->chat_refresh_queue ||
      context->chat_refresh_queue->len == 0) {
    return NULL;
  }
  if (context->chat_refresh_index >= context->chat_refresh_queue->len) {
    return NULL;
  }
  const gchar* chat_id = (const gchar*)g_ptr_array_index(
      context->chat_refresh_queue,
      context->chat_refresh_index);
  context->chat_refresh_index++;
  return chat_id;
}

static const gchar* chat_refresh_pick_next_non_active(AppContext* context) {
  if (!context || !context->chat_refresh_queue ||
      context->chat_refresh_queue->len == 0) {
    return NULL;
  }
  guint attempts = 0;
  guint max_attempts = context->chat_refresh_queue->len;
  while (attempts < max_attempts) {
    const gchar* chat_id = chat_refresh_pick_next(context);
    if (!chat_id) {
      return NULL;
    }
    if (context->active_chat_id &&
        strcmp(context->active_chat_id, chat_id) == 0 &&
        context->chat_refresh_queue->len > 1) {
      attempts++;
      continue;
    }
    return chat_id;
  }
  return NULL;
}

static const gchar* chat_refresh_lookup_name(
    AppContext* context,
    const gchar* chat_id) {
  if (!context || !context->chats || !chat_id || !chat_id[0]) {
    return NULL;
  }
  fpv_chat_t* chat = (fpv_chat_t*)g_hash_table_lookup(
      context->chats,
      chat_id);
  return chat ? chat->title : NULL;
}

static gboolean chat_refresh_schedule(
    AppContext* context,
    const gchar* chat_id,
    const gchar* chat_name,
    gboolean update_memory) {
  if (!context || !context->core || !chat_id || !chat_id[0]) {
    return FALSE;
  }
  if (!update_memory) {
    if (!context->chat_store || !context->current_org ||
        !context->current_org->id) {
      return FALSE;
    }
  }

  ChatHistoryTask* task = (ChatHistoryTask*)g_new0(ChatHistoryTask, 1);
  if (!task) {
    return FALSE;
  }

  task->context = context;
  task->chat_id = g_strdup(chat_id);
  task->chat_name = chat_name ? g_strdup(chat_name) : NULL;
  task->org_id = context->current_org && context->current_org->id
      ? g_strdup(context->current_org->id)
      : NULL;
  task->load_cached = FALSE;
  task->update_memory = update_memory;
  task->release_refresh = TRUE;

  if (!task->chat_id) {
    g_free(task->chat_name);
    g_free(task->org_id);
    g_free(task);
    return FALSE;
  }

  context->chat_refresh_in_flight = TRUE;
  g_thread_new("fpv-chat-refresh", chat_history_thread, task);
  return TRUE;
}

static gboolean chat_refresh_tick(gpointer data) {
  AppContext* context = (AppContext*)data;
  if (!context || context->closing) {
    return G_SOURCE_REMOVE;
  }
  if (!context->running || !context->core) {
    return G_SOURCE_CONTINUE;
  }
  if (context->chat_refresh_in_flight) {
    return G_SOURCE_CONTINUE;
  }

  guint64 now_ms = (guint64)(g_get_real_time() / 1000ULL);

  if (context->active_chat_id &&
      now_ms - context->chat_refresh_last_active_ms >=
          CHAT_REFRESH_ACTIVE_INTERVAL_MS) {
    if (chat_refresh_schedule(
            context,
            context->active_chat_id,
            context->active_chat_name,
            TRUE)) {
      context->chat_refresh_last_active_ms = now_ms;
    }
    return G_SOURCE_CONTINUE;
  }

  if (!context->chat_refresh_queue) {
    chat_refresh_queue_rebuild(context);
  }

  if (context->chat_refresh_queue &&
      context->chat_refresh_index >= context->chat_refresh_queue->len) {
    if (context->chat_refresh_warmup) {
      context->chat_refresh_warmup = FALSE;
      context->chat_refresh_last_background_ms = now_ms;
      context->chat_refresh_index = 0;
      return G_SOURCE_CONTINUE;
    }
    chat_refresh_queue_rebuild(context);
  }

  if (context->chat_refresh_warmup) {
    guint64 warmup_interval = chat_refresh_interval_ms(context, TRUE);
    if (now_ms - context->chat_refresh_last_background_ms < warmup_interval) {
      return G_SOURCE_CONTINUE;
    }
    const gchar* chat_id = chat_refresh_pick_next_non_active(context);
    if (!chat_id) {
      return G_SOURCE_CONTINUE;
    }
    const gchar* chat_name = chat_refresh_lookup_name(context, chat_id);
    if (chat_refresh_schedule(context, chat_id, chat_name, FALSE)) {
      context->chat_refresh_last_background_ms = now_ms;
    }
    if (context->chat_refresh_queue &&
        context->chat_refresh_index >= context->chat_refresh_queue->len) {
      context->chat_refresh_warmup = FALSE;
      context->chat_refresh_index = 0;
    }
    return G_SOURCE_CONTINUE;
  }

  guint64 background_interval = chat_refresh_interval_ms(context, FALSE);
  if (now_ms - context->chat_refresh_last_background_ms <
      background_interval) {
    return G_SOURCE_CONTINUE;
  }

  const gchar* chat_id = chat_refresh_pick_next_non_active(context);
  if (!chat_id) {
    return G_SOURCE_CONTINUE;
  }
  const gchar* chat_name = chat_refresh_lookup_name(context, chat_id);
  if (chat_refresh_schedule(context, chat_id, chat_name, FALSE)) {
    context->chat_refresh_last_background_ms = now_ms;
  }
  return G_SOURCE_CONTINUE;
}

void start_chat_refresh(AppContext* context) {
  if (!context) {
    return;
  }
  if (context->chat_refresh_id != 0) {
    return;
  }
  context->chat_refresh_in_flight = FALSE;
  context->chat_refresh_warmup = TRUE;
  context->chat_refresh_index = 0;
  context->chat_refresh_last_active_ms = 0;
  context->chat_refresh_last_background_ms = 0;
  chat_refresh_queue_rebuild(context);
  context->chat_refresh_id = g_timeout_add(
      CHAT_REFRESH_TICK_MS,
      chat_refresh_tick,
      context);
}

void stop_chat_refresh(AppContext* context) {
  if (!context) {
    return;
  }
  if (context->chat_refresh_id != 0) {
    g_source_remove(context->chat_refresh_id);
    context->chat_refresh_id = 0;
  }
  context->chat_refresh_in_flight = FALSE;
  context->chat_refresh_warmup = FALSE;
  context->chat_refresh_index = 0;
  context->chat_refresh_last_active_ms = 0;
  context->chat_refresh_last_background_ms = 0;
  chat_refresh_queue_clear(context);
}

void on_chat_selected(
    GtkListBox* box,
    GtkListBoxRow* row,
    gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context) {
    return;
  }

  g_free(context->active_chat_id);
  g_free(context->active_chat_name);
  context->active_chat_id = NULL;
  context->active_chat_name = NULL;

  if (!row) {
    gtk_label_set_text(GTK_LABEL(context->message_header), "Messages");
    refresh_message_list(context);
    gtk_widget_set_sensitive(context->message_send_button, FALSE);
    return;
  }

  const gchar* chat_id = (const gchar*)g_object_get_data(
      G_OBJECT(row), "chat-id");
  const gchar* chat_name = (const gchar*)g_object_get_data(
      G_OBJECT(row), "chat-name");
  if (!chat_id || !chat_id[0]) {
    return;
  }

  context->active_chat_id = g_strdup(chat_id);
  if (chat_name) {
    context->active_chat_name = g_strdup(chat_name);
  }

  gchar* header = g_strdup_printf(
      "Messages · %s",
      chat_name ? chat_name : chat_id);
  gtk_label_set_text(GTK_LABEL(context->message_header), header);
  g_free(header);

  refresh_message_list(context);
  gtk_widget_set_sensitive(context->message_send_button, context->running);

  gboolean load_cached = TRUE;
  if (context->messages_by_chat) {
    GPtrArray* existing = (GPtrArray*)g_hash_table_lookup(
        context->messages_by_chat,
        chat_id);
    if (existing && existing->len > 0) {
      load_cached = FALSE;
    }
  }

  ChatHistoryTask* task = (ChatHistoryTask*)g_new0(ChatHistoryTask, 1);
  task->context = context;
  task->chat_id = g_strdup(chat_id);
  task->chat_name = chat_name ? g_strdup(chat_name) : NULL;
  task->org_id = context->current_org && context->current_org->id
      ? g_strdup(context->current_org->id)
      : NULL;
  task->load_cached = load_cached;
  task->update_memory = TRUE;
  task->release_refresh = FALSE;
  g_thread_new("fpv-chat-history", chat_history_thread, task);
}

typedef struct MessageSendTask {
  AppContext* context;
  gchar* chat_id;
  gchar* chat_name;
  gchar* text;
  fpv_result_t result;
} MessageSendTask;

gboolean message_send_complete(gpointer data) {
  MessageSendTask* task = (MessageSendTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->context->closing) {
    g_free(task->chat_id);
    g_free(task->chat_name);
    g_free(task->text);
    g_free(task);
    return G_SOURCE_REMOVE;
  }
  if (task->result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(task->context->window),
        "Messages",
        "Failed to send message.");
  }
  g_free(task->chat_id);
  g_free(task->chat_name);
  g_free(task->text);
  g_free(task);
  return G_SOURCE_REMOVE;
}

gpointer message_send_thread(gpointer data) {
  MessageSendTask* task = (MessageSendTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task) {
      task->result = FPV_ERR_INVALID_STATE;
      g_main_context_invoke(NULL, message_send_complete, task);
    }
    return NULL;
  }
  guint64 chat_id = g_ascii_strtoull(task->chat_id, NULL, 10);
  fpv_result_t result = fpv_core_send_message(
      task->context->core,
      chat_id,
      task->chat_name,
      task->text,
      true);
  task->result = result;
  g_main_context_invoke(NULL, message_send_complete, task);
  return NULL;
}

void on_send_message(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->message_entry) {
    return;
  }
  if (!context->active_chat_id) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Message",
        "Select a chat first.");
    return;
  }
  if (!context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Message",
        "Start the core before sending messages.");
    return;
  }
  const char* text = gtk_editable_get_text(
      GTK_EDITABLE(context->message_entry));
  if (!text || !text[0]) {
    return;
  }

  MessageSendTask* task = (MessageSendTask*)g_new0(MessageSendTask, 1);
  task->context = context;
  task->chat_id = g_strdup(context->active_chat_id);
  task->chat_name = context->active_chat_name
      ? g_strdup(context->active_chat_name)
      : NULL;
  task->text = g_strdup(text);

  gtk_editable_set_text(GTK_EDITABLE(context->message_entry), "");
  g_thread_new("fpv-send-message", message_send_thread, task);
}

void on_message_entry_activate(GtkEntry* entry, gpointer user_data) {
  on_send_message(NULL, user_data);
}

typedef struct LotsRefreshTask {
  AppContext* context;
  gboolean retry_until_ready;
} LotsRefreshTask;

gpointer lots_refresh_thread(gpointer data);

void start_lots_refresh(AppContext* context, gboolean check_running) {
  if (!context || !context->core) {
    return;
  }
  if (check_running && !context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Lots",
        "Start the core before refreshing lots.");
    return;
  }
  if (context->lot_refresh_button) {
    gtk_widget_set_sensitive(context->lot_refresh_button, FALSE);
  }
  LotsRefreshTask* task = (LotsRefreshTask*)g_new0(LotsRefreshTask, 1);
  if (!task) {
    if (context->lot_refresh_button) {
      gtk_widget_set_sensitive(context->lot_refresh_button, TRUE);
    }
    return;
  }
  task->context = context;
  task->retry_until_ready = check_running ? FALSE : TRUE;
  g_thread_new("fpv-refresh-lots", lots_refresh_thread, task);
}

gboolean lots_refresh_complete(gpointer data) {
  LotsRefreshTask* task = (LotsRefreshTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->context->closing) {
    g_free(task);
    return G_SOURCE_REMOVE;
  }
  gtk_widget_set_sensitive(task->context->lot_refresh_button, TRUE);
  g_free(task);
  return G_SOURCE_REMOVE;
}

gpointer lots_refresh_thread(gpointer data) {
  LotsRefreshTask* task = (LotsRefreshTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task && task->context) {
      g_main_context_invoke(NULL, lots_refresh_complete, task);
    } else if (task) {
      g_free(task);
    }
    return NULL;
  }
  const int max_attempts = task->retry_until_ready ? 5 : 1;
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    fpv_result_t result = fpv_core_refresh_lots(task->context->core);
    if (result == FPV_OK || !task->retry_until_ready ||
        result != FPV_ERR_INVALID_STATE) {
      break;
    }
    g_usleep(1000 * 1000);
  }
  g_main_context_invoke(NULL, lots_refresh_complete, task);
  return NULL;
}

void on_refresh_lots(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  (void)button;
  start_lots_refresh(context, TRUE);
}
