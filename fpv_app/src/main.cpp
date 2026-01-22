/* FunPay Vertex GTK application entry point. */

#include "auth/app_auth.h"
#include "core/app_context.h"
#include "events/app_events.h"
#include "helpers/app_helpers.h"
#include "setup/app_setup.h"
#include "settings/app_settings.h"
#include "ui/app_ui.h"

#include <gtk/gtk.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<sanitizer/lsan_interface.h>)
#include <sanitizer/lsan_interface.h>
#define FPV_HAVE_LSAN 1
#endif
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FPV_HAVE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define FPV_HAVE_ASAN 1
#endif

static void app_context_destroy(gpointer data) {
  AppContext* context = (AppContext*)data;
  if (!context) {
    return;
  }

  context->closing = TRUE;

  if (context->poll_id != 0) {
    g_source_remove(context->poll_id);
    context->poll_id = 0;
  }
  stop_chat_refresh(context);

  if (context->core && context->running) {
    fpv_core_stop(context->core, (uint64_t)(g_get_real_time() / 1000ULL));
  }

  fpv_core_destroy(context->core);
  fpv_event_bus_destroy(context->bus);
  fpv_identity_store_destroy(context->identity);
  fpv_chat_store_destroy(context->chat_store);
  fpv_user_destroy(context->current_user);
  fpv_organization_destroy(context->current_org);
  fpv_team_destroy(context->current_team);

  if (context->chats) {
    g_hash_table_destroy(context->chats);
  }
  if (context->orders) {
    g_hash_table_destroy(context->orders);
  }
  if (context->lots) {
    g_hash_table_destroy(context->lots);
  }
  if (context->plugins) {
    g_hash_table_destroy(context->plugins);
  }
  if (context->messages_by_chat) {
    g_hash_table_destroy(context->messages_by_chat);
  }

  g_free(context->active_chat_id);
  g_free(context->active_chat_name);
  g_free(context->base_dir);
  g_free(context->data_dir);
  g_free(context->config_dir);
  g_free(context->logs_dir);
  g_free(context->plugins_dir);
  g_free(context->locales_dir);
  g_free(context);
}

static void configure_sanitizers(void) {
#if defined(FPV_HAVE_ASAN)
  const char* asan = g_getenv("ASAN_OPTIONS");
  if (!asan || !asan[0]) {
    g_setenv(
        "ASAN_OPTIONS",
        "abort_on_error=1:halt_on_error=1:detect_leaks=1:strict_string_checks=1",
        TRUE);
  }
  const char* lsan = g_getenv("LSAN_OPTIONS");
  if (!lsan || !lsan[0]) {
    g_setenv(
        "LSAN_OPTIONS",
        "halt_on_error=1:exitcode=23:verbosity=1",
        TRUE);
  }
#if defined(FPV_HAVE_LSAN)
  atexit(__lsan_do_leak_check);
#endif
#endif
}

static gboolean on_window_close(GtkWindow* window, gpointer user_data) {
  (void)window;
  AppContext* context = (AppContext*)user_data;
  if (!context) {
    return FALSE;
  }
  context->closing = TRUE;
  if (context->poll_id != 0) {
    g_source_remove(context->poll_id);
    context->poll_id = 0;
  }
  stop_chat_refresh(context);
  return FALSE;
}

static void on_activate(GtkApplication* app, gpointer user_data) {
  AppContext* context = g_new0(AppContext, 1);

  context->bus = fpv_event_bus_create();
  if (!context->bus) {
    show_message_dialog(NULL, "Startup", "Event bus initialization failed.");
    g_free(context);
    return;
  }

  const gchar* data_root = g_get_user_data_dir();
  context->base_dir = g_build_filename(data_root, "funpay_vertex", NULL);
  context->data_dir = g_build_filename(context->base_dir, "storage", NULL);
  context->config_dir = g_build_filename(context->base_dir, "configs", NULL);
  context->logs_dir = g_build_filename(context->base_dir, "logs", NULL);
  context->plugins_dir = g_build_filename(context->base_dir, "plugins", NULL);
  context->locales_dir = find_resource_dir("locales", context->base_dir, "eng.loc");

  g_mkdir_with_parents(context->data_dir, 0755);
  g_mkdir_with_parents(context->config_dir, 0755);
  g_mkdir_with_parents(context->logs_dir, 0755);
  g_mkdir_with_parents(context->plugins_dir, 0755);

  sync_main_config(context->config_dir);
  configure_database_from_config(context->config_dir, context->base_dir);

  fpv_result_t identity_result = FPV_OK;
  context->identity = fpv_identity_store_open(context->data_dir, &identity_result);
  if (!context->identity) {
    show_message_dialog(NULL, "Startup", "Identity store initialization failed.");
    fpv_event_bus_destroy(context->bus);
    g_free(context);
    return;
  }

  fpv_result_t chat_store_result = FPV_OK;
  context->chat_store = fpv_chat_store_open(context->data_dir, NULL, &chat_store_result);
  if (!context->chat_store) {
    show_message_dialog(NULL, "Startup", "Chat store initialization failed.");
    fpv_identity_store_destroy(context->identity);
    fpv_event_bus_destroy(context->bus);
    g_free(context);
    return;
  }

  const char* lang = g_getenv("LANG");
  const char* locale = "eng";
  if (lang && (g_str_has_prefix(lang, "ru") || g_str_has_prefix(lang, "RU"))) {
    locale = "ru";
  }

  fpv_core_config_t config;
  config.data_dir = context->data_dir;
  config.config_dir = context->config_dir;
  config.logs_dir = context->logs_dir;
  config.plugins_dir = context->plugins_dir;
  config.locales_dir = context->locales_dir;
  config.locale = locale;

  context->core = fpv_core_create(&config, context->bus);
  if (!context->core) {
    show_message_dialog(NULL, "Startup", "Core initialization failed.");
    fpv_event_bus_destroy(context->bus);
    fpv_identity_store_destroy(context->identity);
    g_free(context);
    return;
  }

  context->chats = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_chat_destroy);
  context->orders = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_order_destroy);
  context->lots = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_lot_destroy);
  context->plugins = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_plugin_destroy);
  context->messages_by_chat = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)g_ptr_array_unref);

  context->window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(context->window), "FunPay Vertex");
  gtk_window_set_default_size(GTK_WINDOW(context->window), 1120, 720);
  g_signal_connect(
      context->window,
      "close-request",
      G_CALLBACK(on_window_close),
      context);

  GtkWidget* layout = build_main_layout(context);
  gtk_window_set_child(GTK_WINDOW(context->window), layout);
  gtk_widget_set_margin_top(layout, 12);
  gtk_widget_set_margin_bottom(layout, 12);
  gtk_widget_set_margin_start(layout, 12);
  gtk_widget_set_margin_end(layout, 12);

  update_status(context, FPV_CORE_STOPPED, NULL);
  apply_css(context->window);

  context->poll_id = g_timeout_add(250, poll_events, context);
  g_object_set_data_full(
      G_OBJECT(app),
      "fpv-context",
      context,
      app_context_destroy);

  if (!has_config_files(context->config_dir)) {
    gtk_widget_set_visible(context->window, FALSE);
    show_setup_wizard(context);
  } else {
    refresh_settings_from_file(context);
    begin_auth_flow(context);
  }
}

int main(int argc, char* argv[]) {
  configure_sanitizers();
  GtkApplication* app = gtk_application_new(
      "com.funpay.vertex",
      G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
