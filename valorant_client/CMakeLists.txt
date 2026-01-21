# Valorant client app.

add_executable(valorant_client
  src/login_only.c
)

target_compile_features(valorant_client PRIVATE c_std_11)

find_package(CURL REQUIRED)

target_include_directories(valorant_client PRIVATE
  ${CMAKE_SOURCE_DIR}/fpv_core/src
  ${CMAKE_SOURCE_DIR}/fpv_core/src/core
  ${CMAKE_SOURCE_DIR}/fpv_core/src/features
  ${CMAKE_SOURCE_DIR}/fpv_core/src/funpay
  ${CMAKE_SOURCE_DIR}/fpv_core/src/identity
  ${CMAKE_SOURCE_DIR}/fpv_core/src/telegram
  ${CMAKE_SOURCE_DIR}/fpv_core/include
)

target_link_libraries(valorant_client PRIVATE CURL::libcurl fpv_core)
