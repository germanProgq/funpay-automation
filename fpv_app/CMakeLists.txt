# FunPay Vertex GTK application target.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_executable(fpv_app
  src/admin/app_admin.cpp
  src/auth/app_auth.cpp
  src/events/app_events.cpp
  src/helpers/app_helpers.cpp
  src/lists/app_lists.cpp
  src/settings/app_settings.cpp
  src/setup/app_setup.cpp
  src/ui/app_ui.cpp
  src/main.cpp
)

target_include_directories(fpv_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(fpv_app PRIVATE ${CMAKE_SOURCE_DIR}/fpv_core/include)
target_link_libraries(fpv_app PRIVATE fpv_core PkgConfig::GTK4)

option(FPV_APP_ENABLE_ASAN "Enable AddressSanitizer for fpv_app." OFF)
if(FPV_APP_ENABLE_ASAN)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(fpv_app PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(fpv_app PRIVATE -fsanitize=address)
  endif()
endif()

install(TARGETS fpv_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
