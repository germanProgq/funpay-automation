# FunPay Vertex GTK application target.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_executable(fpv_app
  src/main.cpp
)

target_include_directories(fpv_app PRIVATE ${CMAKE_SOURCE_DIR}/fpv_core/include)
target_link_libraries(fpv_app PRIVATE fpv_core PkgConfig::GTK4)
