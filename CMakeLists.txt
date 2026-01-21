# FunPay Vertex native build configuration.

cmake_minimum_required(VERSION 3.24)

project(FunPayVertex VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(UNIX AND NOT APPLE)
  add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

option(FPV_BUILD_APP "Build the GTK application." ON)
option(FPV_BUILD_VALORANT_CLIENT "Build the Valorant client app." ON)
option(FPV_BUILD_TESTS "Build tests." ON)
option(FPV_BUILD_TOOLS "Build helper tools." ON)

include(GNUInstallDirs)

add_subdirectory(fpv_core)

if(FPV_BUILD_APP)
  add_subdirectory(fpv_app)
endif()

if(FPV_BUILD_VALORANT_CLIENT)
  add_subdirectory(valorant_client)
endif()

if(FPV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(FPV_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

install(DIRECTORY locales DESTINATION ${CMAKE_INSTALL_DATADIR}/funpay_vertex)
install(DIRECTORY configs DESTINATION ${CMAKE_INSTALL_DATADIR}/funpay_vertex)
if(UNIX AND NOT APPLE)
  install(FILES packaging/linux/funpay_vertex.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/linux/funpay_vertex.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

set(CPACK_PACKAGE_NAME "FunPay Vertex")
set(CPACK_PACKAGE_VENDOR "FunPay Vertex")
set(CPACK_PACKAGE_CONTACT "FunPay Vertex")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "FunPay Vertex desktop automation suite.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FunPay Vertex")

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_DISPLAY_NAME "FunPay Vertex")
  set(CPACK_NSIS_PACKAGE_NAME "FunPay Vertex")
  set(CPACK_NSIS_CONTACT "FunPay Vertex")
elseif(APPLE)
  set(CPACK_GENERATOR "TGZ")
else()
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "FunPay Vertex")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
