#ifndef FPV_APP_SRC_APP_SETUP_H_
#define FPV_APP_SRC_APP_SETUP_H_

#include "core/app_context.h"

gchar* find_resource_dir(
    const gchar* subdir,
    const gchar* base_dir,
    const gchar* marker);

gboolean has_config_files(const gchar* config_dir);
void sync_main_config(const gchar* config_dir);
void configure_database_from_config(
    const gchar* config_dir,
    const gchar* base_dir);
void show_setup_wizard(AppContext* context);

#endif  // FPV_APP_SRC_APP_SETUP_H_
