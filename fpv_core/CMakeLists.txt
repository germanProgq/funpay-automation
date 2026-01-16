# FunPay Vertex C core library target.

add_library(fpv_core STATIC
  src/fpv_cache.c
  src/fpv_core.c
  src/fpv_event_bus.c
  src/fpv_feature_config.c
  src/fpv_features.c
  src/fpv_fs.c
  src/fpv_funpay.c
  src/fpv_funpay_http.c
  src/fpv_html.c
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

target_compile_definitions(fpv_core PRIVATE FPV_CORE_BUILD)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(
  fpv_core
  PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
    CURL::libcurl
    LibXml2::LibXml2)
