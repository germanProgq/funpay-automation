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

option(FPV_BUILD_APP "Build the GTK application." ON)
option(FPV_BUILD_VALORANT_CLIENT "Build the Valorant client app." ON)

add_subdirectory(fpv_core)

if(FPV_BUILD_APP)
  add_subdirectory(fpv_app)
endif()

if(FPV_BUILD_VALORANT_CLIENT)
  add_subdirectory(valorant_client)
endif()
