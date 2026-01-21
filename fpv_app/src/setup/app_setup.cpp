#include "setup/app_setup.h"

#include "auth/app_auth.h"
#include "helpers/app_helpers.h"
#include "settings/app_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

gboolean dir_has_marker(const gchar* dir, const gchar* marker) {
  if (!dir || !marker || !marker[0]) {
    return FALSE;
  }
  gchar* path = g_build_filename(dir, marker, NULL);
  if (!path) {
    return FALSE;
  }
  gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR);
  g_free(path);
  return exists;
}

gchar* find_resource_dir(
    const gchar* subdir,
    const gchar* base_dir,
    const gchar* marker) {
  if (!subdir || !subdir[0]) {
    return NULL;
  }

  gchar* cwd = g_get_current_dir();
  if (cwd) {
    gchar* path = g_build_filename(cwd, subdir, NULL);
    if (path &&
        g_file_test(path, G_FILE_TEST_IS_DIR) &&
        (!marker || dir_has_marker(path, marker))) {
      g_free(cwd);
      return path;
    }
    g_free(path);
    g_free(cwd);
  }

  if (base_dir && base_dir[0]) {
    gchar* path = g_build_filename(base_dir, subdir, NULL);
    if (path &&
        g_file_test(path, G_FILE_TEST_IS_DIR) &&
        (!marker || dir_has_marker(path, marker))) {
      return path;
    }
    g_free(path);
  }

  const gchar* const* system_dirs = g_get_system_data_dirs();
  for (size_t i = 0; system_dirs && system_dirs[i]; i++) {
    gchar* path = g_build_filename(system_dirs[i], "funpay_vertex", subdir, NULL);
    if (path &&
        g_file_test(path, G_FILE_TEST_IS_DIR) &&
        (!marker || dir_has_marker(path, marker))) {
      return path;
    }
    g_free(path);
  }

  return NULL;
}

gboolean has_config_files(const gchar* config_dir) {
  if (!config_dir || !config_dir[0]) {
    return FALSE;
  }
  gchar* main_cfg = g_build_filename(config_dir, "_main.cfg", NULL);
  gboolean exists = g_file_test(main_cfg, G_FILE_TEST_IS_REGULAR);
  g_free(main_cfg);
  return exists;
}

gboolean copy_file_if_missing(
    const gchar* src,
    const gchar* dest,
    GError** error) {
  if (!src || !dest) {
    return FALSE;
  }
  if (g_file_test(dest, G_FILE_TEST_IS_REGULAR)) {
    return TRUE;
  }
  GFile* src_file = g_file_new_for_path(src);
  GFile* dest_file = g_file_new_for_path(dest);
  gboolean ok = g_file_copy(
      src_file,
      dest_file,
      G_FILE_COPY_OVERWRITE,
      NULL,
      NULL,
      NULL,
      error);
  g_object_unref(src_file);
  g_object_unref(dest_file);
  return ok;
}

gboolean copy_file_overwrite(
    const gchar* src,
    const gchar* dest,
    GError** error) {
  if (!src || !dest) {
    return FALSE;
  }
  GFile* src_file = g_file_new_for_path(src);
  GFile* dest_file = g_file_new_for_path(dest);
  gboolean ok = g_file_copy(
      src_file,
      dest_file,
      G_FILE_COPY_OVERWRITE,
      NULL,
      NULL,
      NULL,
      error);
  g_object_unref(src_file);
  g_object_unref(dest_file);
  return ok;
}

gboolean copy_directory_recursive(
    const gchar* src,
    const gchar* dest,
    GError** error) {
  if (!src || !dest) {
    return FALSE;
  }
  if (!g_file_test(src, G_FILE_TEST_IS_DIR)) {
    return FALSE;
  }
  if (g_mkdir_with_parents(dest, 0755) != 0) {
    return FALSE;
  }

  GDir* dir = g_dir_open(src, 0, error);
  if (!dir) {
    return FALSE;
  }

  const gchar* name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      continue;
    }
    gchar* src_path = g_build_filename(src, name, NULL);
    gchar* dest_path = g_build_filename(dest, name, NULL);
    if (g_file_test(src_path, G_FILE_TEST_IS_DIR)) {
      if (!copy_directory_recursive(src_path, dest_path, error)) {
        g_free(src_path);
        g_free(dest_path);
        g_dir_close(dir);
        return FALSE;
      }
    } else {
      GFile* src_file = g_file_new_for_path(src_path);
      GFile* dest_file = g_file_new_for_path(dest_path);
      gboolean ok = g_file_copy(
          src_file,
          dest_file,
          G_FILE_COPY_OVERWRITE,
          NULL,
          NULL,
          NULL,
          error);
      g_object_unref(src_file);
      g_object_unref(dest_file);
      if (!ok) {
        g_free(src_path);
        g_free(dest_path);
        g_dir_close(dir);
        return FALSE;
      }
    }
    g_free(src_path);
    g_free(dest_path);
  }

  g_dir_close(dir);
  return TRUE;
}

void sync_main_config(const gchar* config_dir) {
  if (!config_dir || !config_dir[0]) {
    return;
  }

  const gchar* filenames[] = {
      "_main.cfg",
      "auto_response.cfg",
      "auto_delivery.cfg"
  };
  const size_t file_count = sizeof(filenames) / sizeof(filenames[0]);

  gchar* base_dir = g_path_get_dirname(config_dir);
  gchar* template_dir = find_resource_dir("configs", base_dir, "_main.cfg");
  g_free(base_dir);
  if (!template_dir || !g_file_test(template_dir, G_FILE_TEST_IS_DIR)) {
    g_free(template_dir);
    return;
  }

#if defined(__APPLE__)
  gchar* mac_dir = NULL;
  const gchar* home_dir = g_get_home_dir();
  if (home_dir && home_dir[0]) {
    mac_dir = g_build_filename(
        home_dir,
        "Library",
        "Application Support",
        "funpay_vertex",
        "configs",
        NULL);
    g_mkdir_with_parents(mac_dir, 0755);
  }
#endif

  for (size_t i = 0; i < file_count; i++) {
    gchar* src = g_build_filename(template_dir, filenames[i], NULL);
    gchar* dest = g_build_filename(config_dir, filenames[i], NULL);
    gboolean has_src = g_file_test(src, G_FILE_TEST_IS_REGULAR);
    if (has_src) {
      copy_file_if_missing(src, dest, NULL);
    }
#if defined(__APPLE__)
    if (mac_dir && has_src) {
      gchar* mac_dest = g_build_filename(mac_dir, filenames[i], NULL);
      copy_file_if_missing(src, mac_dest, NULL);
      g_free(mac_dest);
    }
#endif
    g_free(src);
    g_free(dest);
  }

  g_free(template_dir);
#if defined(__APPLE__)
  g_free(mac_dir);
#endif
}

typedef struct DatabaseConfig {
  gchar* url;
  gboolean docker;
  gchar* docker_image;
  gchar* docker_name;
  gchar* docker_volume;
} DatabaseConfig;

typedef struct PostgresUrlParts {
  gchar* user;
  gchar* password;
  gchar* host;
  gchar* database;
  guint port;
} PostgresUrlParts;

void database_config_clear(DatabaseConfig* config) {
  if (!config) {
    return;
  }
  g_free(config->url);
  g_free(config->docker_image);
  g_free(config->docker_name);
  g_free(config->docker_volume);
  memset(config, 0, sizeof(*config));
}

void postgres_url_parts_clear(PostgresUrlParts* parts) {
  if (!parts) {
    return;
  }
  g_free(parts->user);
  g_free(parts->password);
  g_free(parts->host);
  g_free(parts->database);
  memset(parts, 0, sizeof(*parts));
}

gboolean parse_postgres_url(const char* url, PostgresUrlParts* parts) {
  if (!url || !parts) {
    return FALSE;
  }
  memset(parts, 0, sizeof(*parts));
  GError* error = NULL;
  GUri* uri = g_uri_parse(url, G_URI_FLAGS_NONE, &error);
  if (!uri) {
    if (error) {
      g_error_free(error);
    }
    return FALSE;
  }
  const char* scheme = g_uri_get_scheme(uri);
  if (!scheme ||
      (g_ascii_strcasecmp(scheme, "postgres") != 0 &&
       g_ascii_strcasecmp(scheme, "postgresql") != 0)) {
    g_uri_unref(uri);
    return FALSE;
  }
  const char* host = g_uri_get_host(uri);
  const char* path = g_uri_get_path(uri);
  if (!host || !host[0] || !path || !path[0] || strcmp(path, "/") == 0) {
    g_uri_unref(uri);
    return FALSE;
  }
  gchar* database = g_uri_unescape_string(path[0] == '/' ? path + 1 : path, NULL);
  if (!database || !database[0]) {
    g_free(database);
    g_uri_unref(uri);
    return FALSE;
  }
  parts->host = g_strdup(host);
  parts->database = database;
  gint port = g_uri_get_port(uri);
  parts->port = port > 0 ? (guint)port : 5433;

  const char* userinfo = g_uri_get_userinfo(uri);
  if (userinfo && userinfo[0]) {
    gchar* decoded = g_uri_unescape_string(userinfo, NULL);
    const char* info = decoded ? decoded : userinfo;
    const char* colon = strchr(info, ':');
    if (colon) {
      parts->user = g_strndup(info, (gsize)(colon - info));
      parts->password = g_strdup(colon + 1);
    } else {
      parts->user = g_strdup(info);
    }
    g_free(decoded);
  }

  g_uri_unref(uri);
  if (!parts->host || !parts->database) {
    postgres_url_parts_clear(parts);
    return FALSE;
  }
  return TRUE;
}

gboolean is_local_host(const char* host) {
  if (!host || !host[0]) {
    return FALSE;
  }
  return g_strcmp0(host, "localhost") == 0 ||
         g_strcmp0(host, "127.0.0.1") == 0 ||
         g_strcmp0(host, "::1") == 0;
}

gboolean spawn_and_capture(
    char* const argv[],
    gchar** out_stdout,
    gchar** out_stderr) {
  if (out_stdout) {
    *out_stdout = NULL;
  }
  if (out_stderr) {
    *out_stderr = NULL;
  }
  GError* error = NULL;
  int status = 0;
  gboolean ok = g_spawn_sync(
      NULL,
      const_cast<gchar**>(argv),
      NULL,
      G_SPAWN_SEARCH_PATH,
      NULL,
      NULL,
      out_stdout,
      out_stderr,
      &status,
      &error);
  if (!ok) {
    if (error) {
      g_error_free(error);
    }
    return FALSE;
  }
  if (!g_spawn_check_wait_status(status, &error)) {
    if (error) {
      g_error_free(error);
    }
    return FALSE;
  }
  return TRUE;
}

gboolean stdout_has_line(const gchar* output, const char* name) {
  if (!output || !name || !name[0]) {
    return FALSE;
  }
  gchar** lines = g_strsplit(output, "\n", -1);
  gboolean found = FALSE;
  for (size_t i = 0; lines && lines[i]; i++) {
    if (g_strcmp0(lines[i], name) == 0) {
      found = TRUE;
      break;
    }
  }
  g_strfreev(lines);
  return found;
}

gboolean docker_container_present(const char* name, gboolean running_only) {
  if (!name || !name[0]) {
    return FALSE;
  }
  char filter[256];
  snprintf(filter, sizeof(filter), "name=^%s$", name);
  gchar* stdout_text = NULL;
  gchar* stderr_text = NULL;
  gboolean ok = FALSE;
  if (running_only) {
    char* const argv[] = {
        (char*)"docker",
        (char*)"ps",
        (char*)"--filter",
        filter,
        (char*)"--format",
        (char*)"{{.Names}}",
        NULL};
    ok = spawn_and_capture(argv, &stdout_text, &stderr_text);
  } else {
    char* const argv[] = {
        (char*)"docker",
        (char*)"ps",
        (char*)"-a",
        (char*)"--filter",
        filter,
        (char*)"--format",
        (char*)"{{.Names}}",
        NULL};
    ok = spawn_and_capture(argv, &stdout_text, &stderr_text);
  }
  gboolean present = ok && stdout_has_line(stdout_text, name);
  g_free(stdout_text);
  g_free(stderr_text);
  return present;
}

gboolean docker_start_container(const char* name) {
  char* const argv[] = {(char*)"docker", (char*)"start", (char*)name, NULL};
  return spawn_and_capture(argv, NULL, NULL);
}

gboolean docker_run_postgres(
    const DatabaseConfig* config,
    const PostgresUrlParts* parts) {
  const char* name = config->docker_name ? config->docker_name : "fpv-postgres";
  const char* image = config->docker_image ? config->docker_image : "postgres:16";
  const char* volume =
      config->docker_volume ? config->docker_volume : "fpv-postgres-data";

  char port_arg[32];
  snprintf(port_arg, sizeof(port_arg), "%u:5433", parts->port);

  char user_arg[128];
  char pass_arg[128];
  char db_arg[128];
  snprintf(user_arg, sizeof(user_arg), "POSTGRES_USER=%s", parts->user);
  snprintf(pass_arg, sizeof(pass_arg), "POSTGRES_PASSWORD=%s", parts->password);
  snprintf(db_arg, sizeof(db_arg), "POSTGRES_DB=%s", parts->database);

  char volume_arg[256];
  snprintf(volume_arg, sizeof(volume_arg), "%s:/var/lib/postgresql/data", volume);

  char* const argv[] = {
      (char*)"docker",
      (char*)"run",
      (char*)"-d",
      (char*)"--name",
      (char*)name,
      (char*)"--restart",
      (char*)"unless-stopped",
      (char*)"-e",
      user_arg,
      (char*)"-e",
      pass_arg,
      (char*)"-e",
      db_arg,
      (char*)"-p",
      port_arg,
      (char*)"-v",
      volume_arg,
      (char*)image,
      NULL};

  return spawn_and_capture(argv, NULL, NULL);
}

gboolean docker_wait_postgres_ready(
    const char* name,
    const char* user,
    const char* database) {
  for (int attempt = 0; attempt < 60; attempt++) {
    char* const argv[] = {
        (char*)"docker",
        (char*)"exec",
        (char*)name,
        (char*)"pg_isready",
        (char*)"-U",
        (char*)user,
        (char*)"-d",
        (char*)database,
        NULL};
    if (spawn_and_capture(argv, NULL, NULL)) {
      return TRUE;
    }
    g_usleep(500000);
  }
  return FALSE;
}

void configure_database_from_config(
    const gchar* config_dir,
    const gchar* base_dir) {
  gchar* config_path = NULL;
  if (config_dir && config_dir[0]) {
    config_path = g_build_filename(config_dir, "_main.cfg", NULL);
    if (!g_file_test(config_path, G_FILE_TEST_IS_REGULAR)) {
      g_free(config_path);
      config_path = NULL;
    }
  }
  if (!config_path) {
    gchar* template_dir = find_resource_dir("configs", base_dir, "_main.cfg");
    if (template_dir && template_dir[0]) {
      config_path = g_build_filename(template_dir, "_main.cfg", NULL);
    }
    g_free(template_dir);
  }
  if (!config_path) {
    return;
  }

  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(config_path, &error);
  g_free(config_path);
  if (!ini) {
    return;
  }

  DatabaseConfig config;
  memset(&config, 0, sizeof(config));
  const char* url = fpv_ini_get(ini, "Database", "url");
  const char* docker = fpv_ini_get(ini, "Database", "docker");
  const char* docker_image = fpv_ini_get(ini, "Database", "dockerImage");
  const char* docker_name = fpv_ini_get(ini, "Database", "dockerName");
  const char* docker_volume = fpv_ini_get(ini, "Database", "dockerVolume");

  if (url && url[0]) {
    config.url = g_strdup(url);
  }
  config.docker = parse_ini_bool(docker, FALSE);
  if (docker_image && docker_image[0]) {
    config.docker_image = g_strdup(docker_image);
  }
  if (docker_name && docker_name[0]) {
    config.docker_name = g_strdup(docker_name);
  }
  if (docker_volume && docker_volume[0]) {
    config.docker_volume = g_strdup(docker_volume);
  }

  if (config.url && config.url[0]) {
    g_setenv("FPV_DB_URL", config.url, TRUE);
  }

  if (config.docker && config.url && config.url[0]) {
    PostgresUrlParts parts;
    if (parse_postgres_url(config.url, &parts) &&
        parts.user && parts.user[0] &&
        parts.password && parts.password[0] &&
        parts.database && parts.database[0] &&
        is_local_host(parts.host)) {
      if (g_find_program_in_path("docker")) {
        const char* name =
            config.docker_name ? config.docker_name : "fpv-postgres";
        gboolean exists = docker_container_present(name, FALSE);
        gboolean running = docker_container_present(name, TRUE);
        if (!exists) {
          docker_run_postgres(&config, &parts);
        } else if (!running) {
          docker_start_container(name);
        }
        docker_wait_postgres_ready(name, parts.user, parts.database);
      }
    }
    postgres_url_parts_clear(&parts);
  }

  database_config_clear(&config);
  fpv_ini_destroy(ini);
}

gchar* resolve_config_dir(const gchar* selection) {
  if (!selection) {
    return NULL;
  }

  gchar* direct = g_build_filename(selection, "_main.cfg", NULL);
  if (g_file_test(direct, G_FILE_TEST_IS_REGULAR)) {
    g_free(direct);
    return g_strdup(selection);
  }
  g_free(direct);

  gchar* nested = g_build_filename(selection, "configs", "_main.cfg", NULL);
  if (g_file_test(nested, G_FILE_TEST_IS_REGULAR)) {
    gchar* config_dir = g_build_filename(selection, "configs", NULL);
    g_free(nested);
    return config_dir;
  }
  g_free(nested);

  return NULL;
}

gboolean import_from_path(
    AppContext* context,
    const gchar* selection,
    gchar** out_error) {
  if (!context || !selection) {
    return FALSE;
  }

  gchar* config_src = resolve_config_dir(selection);
  if (!config_src) {
    if (out_error) {
      *out_error = g_strdup("No configs found in the selected folder.");
    }
    return FALSE;
  }

  const gchar* filenames[] = {
      "_main.cfg",
      "auto_response.cfg",
      "auto_delivery.cfg"
  };
  const size_t file_count = sizeof(filenames) / sizeof(filenames[0]);

  for (size_t i = 0; i < file_count; i++) {
    gchar* src = g_build_filename(config_src, filenames[i], NULL);
    gchar* dest = g_build_filename(context->config_dir, filenames[i], NULL);
    if (g_file_test(src, G_FILE_TEST_IS_REGULAR)) {
      if (!copy_file_overwrite(src, dest, NULL)) {
        if (out_error) {
          *out_error = g_strdup("Failed to copy config files.");
        }
        g_free(src);
        g_free(dest);
        g_free(config_src);
        return FALSE;
      }
    }
    g_free(src);
    g_free(dest);
  }

  gchar* root = NULL;
  if (g_strcmp0(config_src, selection) == 0) {
    root = g_strdup(selection);
  } else if (g_str_has_suffix(config_src, G_DIR_SEPARATOR_S "configs")) {
    root = g_path_get_dirname(config_src);
  } else {
    root = g_strdup(selection);
  }

  gchar* storage_src = g_build_filename(root, "storage", NULL);
  if (g_file_test(storage_src, G_FILE_TEST_IS_DIR)) {
    if (!copy_directory_recursive(storage_src, context->data_dir, NULL)) {
      if (out_error) {
        *out_error = g_strdup("Failed to import storage data.");
      }
      g_free(storage_src);
      g_free(root);
      g_free(config_src);
      return FALSE;
    }
  }

  g_free(storage_src);
  g_free(root);
  g_free(config_src);
  sync_main_config(context->config_dir);
  return TRUE;
}

typedef struct SetupWizard {
  AppContext* context;
  GtkWidget* window;
} SetupWizard;

void finish_setup(AppContext* context) {
  if (!context) {
    return;
  }
  refresh_settings_from_file(context);
  begin_auth_flow(context);
}

void on_setup_create(GtkButton* button, gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  if (!wizard || !wizard->context) {
    return;
  }
  sync_main_config(wizard->context->config_dir);
  if (!has_config_files(wizard->context->config_dir)) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        "Default configs not found in the app directory.");
    return;
  }
  gtk_window_close(GTK_WINDOW(wizard->window));
  finish_setup(wizard->context);
  g_free(wizard);
}

#if GTK_CHECK_VERSION(4, 10, 0)
void on_setup_import_finish(
    GObject* source,
    GAsyncResult* result,
    gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
  if (!wizard || !wizard->context) {
    g_object_unref(dialog);
    return;
  }

  GError* error = NULL;
  GFile* file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
  if (!file) {
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      show_message_dialog(
          GTK_WINDOW(wizard->window),
          "Setup",
          error->message ? error->message : "Import failed.");
    }
    g_clear_error(&error);
    g_object_unref(dialog);
    return;
  }

  gchar* selection = g_file_get_path(file);
  g_object_unref(file);
  if (!selection) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        "Selected folder is not available.");
    g_object_unref(dialog);
    return;
  }

  gchar* import_error = NULL;
  if (!import_from_path(wizard->context, selection, &import_error)) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        import_error ? import_error : "Import failed.");
    g_free(import_error);
    g_free(selection);
    g_object_unref(dialog);
    return;
  }

  g_free(selection);
  gtk_window_close(GTK_WINDOW(wizard->window));
  finish_setup(wizard->context);
  g_free(wizard);
  g_object_unref(dialog);
}

void on_setup_import(GtkButton* button, gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  if (!wizard || !wizard->context) {
    return;
  }

  GtkFileDialog* dialog = gtk_file_dialog_new();
  g_object_ref(dialog);
  gtk_file_dialog_select_folder(
      dialog,
      GTK_WINDOW(wizard->window),
      NULL,
      on_setup_import_finish,
      wizard);
}
#else
void on_setup_import_response(
    GtkNativeDialog* dialog,
    gint response,
    gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  if (!wizard || !wizard->context) {
    g_object_unref(dialog);
    return;
  }

  if (response != GTK_RESPONSE_ACCEPT) {
    g_object_unref(dialog);
    return;
  }

  GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
  if (!file) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        "Selected folder is not available.");
    g_object_unref(dialog);
    return;
  }

  gchar* selection = g_file_get_path(file);
  g_object_unref(file);
  if (!selection) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        "Selected folder is not available.");
    g_object_unref(dialog);
    return;
  }

  gchar* import_error = NULL;
  if (!import_from_path(wizard->context, selection, &import_error)) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        import_error ? import_error : "Import failed.");
    g_free(import_error);
    g_free(selection);
    g_object_unref(dialog);
    return;
  }

  g_free(selection);
  gtk_window_close(GTK_WINDOW(wizard->window));
  finish_setup(wizard->context);
  g_free(wizard);
  g_object_unref(dialog);
}

void on_setup_import(GtkButton* button, gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  if (!wizard || !wizard->context) {
    return;
  }

  GtkFileChooserNative* dialog = gtk_file_chooser_native_new(
      "Select Folder",
      GTK_WINDOW(wizard->window),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      "_Select",
      "_Cancel");
  g_signal_connect(
      dialog,
      "response",
      G_CALLBACK(on_setup_import_response),
      wizard);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
#endif

void show_setup_wizard(AppContext* context) {
  if (!context || !context->window) {
    return;
  }

  SetupWizard* wizard = (SetupWizard*)g_new0(SetupWizard, 1);
  wizard->context = context;

  GtkWidget* dialog = gtk_window_new();
  wizard->window = dialog;
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_title(GTK_WINDOW(dialog), "First Setup");
  gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 240);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 20);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);

  GtkWidget* title = gtk_label_new("Set up FunPay Vertex");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* detail = gtk_label_new(
      "Create new configuration files or import an existing setup "
      "from another FunPay Vertex installation.");
  gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
  gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* create_button = gtk_button_new_with_label("Create New");
  GtkWidget* import_button = gtk_button_new_with_label("Import Existing");

  g_signal_connect(create_button, "clicked", G_CALLBACK(on_setup_create), wizard);
  g_signal_connect(import_button, "clicked", G_CALLBACK(on_setup_import), wizard);

  gtk_box_append(GTK_BOX(actions), create_button);
  gtk_box_append(GTK_BOX(actions), import_button);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), detail);
  gtk_box_append(GTK_BOX(box), actions);

  gtk_window_set_child(GTK_WINDOW(dialog), box);
  gtk_window_present(GTK_WINDOW(dialog));
}
