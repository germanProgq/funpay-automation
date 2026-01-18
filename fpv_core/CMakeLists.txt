# FunPay Vertex C core library target.

add_library(fpv_core STATIC
  src/fpv_audit.c
  src/fpv_cache.c
  src/fpv_chat_store.c
  src/fpv_core.c
  src/fpv_db.c
  src/fpv_event_bus.c
  src/fpv_feature_config.c
  src/fpv_features.c
  src/fpv_fs.c
  src/fpv_funpay.c
  src/fpv_funpay_http.c
  src/fpv_html.c
  src/fpv_identity.c
  src/fpv_ini.c
  src/fpv_json.c
  src/fpv_localization.c
  src/fpv_log.c
  src/fpv_models.c
  src/fpv_plugin.c
  src/fpv_products.c
  src/fpv_scheduler.c
  src/fpv_settings.c
  src/fpv_shared_lib.c
  src/fpv_string.c
  src/fpv_storage.c
  src/fpv_telegram.c
  src/fpv_time.c
)

target_include_directories(fpv_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_include_directories(fpv_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
