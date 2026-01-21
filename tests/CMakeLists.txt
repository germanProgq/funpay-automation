# FunPay Vertex test targets.

add_executable(fpv_core_unit_tests
  fpv_core_unit_tests.c
)

target_include_directories(fpv_core_unit_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/fpv_core/include
  ${CMAKE_SOURCE_DIR}/fpv_core/src
  ${CMAKE_SOURCE_DIR}/fpv_core/src/core
  ${CMAKE_SOURCE_DIR}/fpv_core/src/features
  ${CMAKE_SOURCE_DIR}/fpv_core/src/funpay
  ${CMAKE_SOURCE_DIR}/fpv_core/src/identity
  ${CMAKE_SOURCE_DIR}/fpv_core/src/telegram
)

target_compile_definitions(fpv_core_unit_tests PRIVATE
  FPV_ENABLE_TEST_HOOKS
  FPV_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(fpv_core_unit_tests PRIVATE fpv_core)

add_test(NAME fpv_core_unit_tests COMMAND fpv_core_unit_tests)

add_executable(fpv_core_integration_tests
  fpv_core_integration_tests.c
)

target_include_directories(fpv_core_integration_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/fpv_core/include
  ${CMAKE_SOURCE_DIR}/fpv_core/src
  ${CMAKE_SOURCE_DIR}/fpv_core/src/core
  ${CMAKE_SOURCE_DIR}/fpv_core/src/features
  ${CMAKE_SOURCE_DIR}/fpv_core/src/funpay
  ${CMAKE_SOURCE_DIR}/fpv_core/src/identity
  ${CMAKE_SOURCE_DIR}/fpv_core/src/telegram
)

target_compile_definitions(fpv_core_integration_tests PRIVATE
  FPV_ENABLE_TEST_HOOKS
  FPV_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(fpv_core_integration_tests PRIVATE fpv_core)

add_test(NAME fpv_core_integration_tests COMMAND fpv_core_integration_tests)
