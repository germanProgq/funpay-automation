# FunPay Vertex C core library target.

add_library(fpv_core STATIC
  src/core/app/fpv_audit.c
  src/core/io/fpv_cache.c
  src/core/data/fpv_chat_store.c
  src/core/app/fpv_core.c
  src/core/data/fpv_db.c
  src/features/entitlements/fpv_entitlements.c
  src/core/infra/fpv_event_bus.c
  src/features/config/fpv_feature_config.c
  src/features/flow/fpv_features_background.c
  src/features/runtime/fpv_features.c
  src/features/config/fpv_features_config.c
  src/features/flow/fpv_features_flow.c
  src/features/lots/fpv_features_lots.c
  src/features/send/fpv_features_send.c
  src/features/text/fpv_features_text.c
  src/features/runtime/fpv_features_utils.c
  src/core/io/fpv_fs.c
  src/funpay/core/fpv_funpay.c
  src/funpay/chat/fpv_funpay_chat.c
  src/funpay/html/fpv_funpay_html.c
  src/funpay/http/fpv_funpay_http.c
  src/funpay/runner/fpv_funpay_runner.c
  src/funpay/utils/fpv_funpay_utils.c
  src/core/io/fpv_html.c
  src/identity/core/fpv_identity.c
  src/identity/access_reviews/fpv_identity_access_reviews.c
  src/identity/accounts/fpv_identity_accounts.c
  src/identity/core/fpv_identity_crypto.c
  src/identity/core/fpv_identity_db.c
  src/identity/invites/fpv_identity_invites.c
  src/identity/orgs/fpv_identity_orgs.c
  src/identity/core/fpv_identity_parse.c
  src/identity/price_requests/fpv_identity_price_requests.c
  src/identity/roles/fpv_identity_roles.c
  src/identity/scopes/fpv_identity_scopes.c
  src/identity/teams/fpv_identity_teams.c
  src/identity/users/fpv_identity_users.c
  src/core/data/fpv_ini.c
  src/core/data/fpv_json.c
  src/core/app/fpv_localization.c
  src/core/infra/fpv_log.c
  src/core/data/fpv_models.c
  src/core/infra/fpv_plugin.c
  src/core/data/fpv_products.c
  src/core/infra/fpv_scheduler.c
  src/core/app/fpv_settings.c
  src/core/infra/fpv_shared_lib.c
  src/core/base/fpv_string.c
  src/core/io/fpv_storage.c
  src/telegram/core/fpv_telegram.c
  src/telegram/admin/fpv_telegram_admin.c
  src/telegram/auth/fpv_telegram_auth.c
  src/telegram/auto/fpv_telegram_auto_delivery.c
  src/telegram/auto/fpv_telegram_auto_response.c
  src/telegram/chat/fpv_telegram_chat_actions.c
  src/telegram/core/fpv_telegram_constants.c
  src/telegram/io/fpv_telegram_helpers.c
  src/telegram/io/fpv_telegram_http.c
  src/telegram/ui/fpv_telegram_keyboard.c
  src/telegram/menu/fpv_telegram_menu.c
  src/telegram/io/fpv_telegram_parsing.c
  src/telegram/admin/fpv_telegram_profile.c
  src/telegram/service/fpv_telegram_service.c
  src/telegram/state/fpv_telegram_state.c
  src/telegram/ui/fpv_telegram_templates.c
  src/telegram/ui/fpv_telegram_ui.c
  src/telegram/io/fpv_telegram_utils.c
  src/core/base/fpv_time.c
)

target_include_directories(fpv_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_include_directories(fpv_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/features
  ${CMAKE_CURRENT_SOURCE_DIR}/src/funpay
  ${CMAKE_CURRENT_SOURCE_DIR}/src/identity
  ${CMAKE_CURRENT_SOURCE_DIR}/src/telegram
)

target_compile_features(fpv_core PUBLIC c_std_11)

target_compile_definitions(fpv_core PRIVATE
  FPV_CORE_BUILD
  $<$<BOOL:${FPV_BUILD_TESTS}>:FPV_ENABLE_TEST_HOOKS>)

option(FPV_ENABLE_POSTGRES "Enable PostgreSQL database backend." ON)
option(FPV_ENABLE_SQLITE "Enable SQLite database backend." ON)

set(FPV_WITH_POSTGRES OFF)
set(FPV_WITH_SQLITE OFF)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(Threads REQUIRED)

if (FPV_ENABLE_POSTGRES)
  find_package(PostgreSQL)
  if (PostgreSQL_FOUND)
    set(FPV_WITH_POSTGRES ON)
    target_compile_definitions(fpv_core PRIVATE FPV_HAVE_POSTGRES)
    target_include_directories(fpv_core PRIVATE ${PostgreSQL_INCLUDE_DIRS})
    target_link_libraries(fpv_core PRIVATE ${PostgreSQL_LIBRARIES})
  else()
    message(WARNING "PostgreSQL not found; PostgreSQL backend disabled.")
  endif()
endif()

if (FPV_ENABLE_SQLITE)
  find_package(SQLite3 REQUIRED)
  set(FPV_WITH_SQLITE ON)
  target_compile_definitions(fpv_core PRIVATE FPV_HAVE_SQLITE)
  if (TARGET SQLite::SQLite3)
    target_link_libraries(fpv_core PRIVATE SQLite::SQLite3)
  else()
    target_include_directories(fpv_core PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(fpv_core PRIVATE ${SQLite3_LIBRARIES})
  endif()
endif()

if (NOT FPV_WITH_POSTGRES AND NOT FPV_WITH_SQLITE)
  message(FATAL_ERROR "No database backend enabled. Enable SQLite or PostgreSQL.")
endif()

target_link_libraries(
  fpv_core
  PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
    CURL::libcurl
    LibXml2::LibXml2)

if (WIN32)
  target_link_libraries(fpv_core PRIVATE Bcrypt)
endif()

install(TARGETS fpv_core
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
