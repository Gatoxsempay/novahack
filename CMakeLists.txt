cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(noclip VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED src/main.cpp)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK no definido. Ejecuta: $env:GEODE_SDK = 'C:\\Users\\fer91\\GeodeSDK'")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
target_link_libraries(${PROJECT_NAME} geode-sdk)
setup_geode_mod(${PROJECT_NAME})

# cmake 4.x NMake fix: add MSVC CRT paths when implicit link dirs are empty
if(MSVC AND "${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES}" STREQUAL "")
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _d)
    cmake_path(GET _d PARENT_PATH _d)
    cmake_path(GET _d PARENT_PATH _d)
    cmake_path(GET _d PARENT_PATH _msvc_root)
    file(GLOB _sdk_dirs LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/Lib/*")
    list(SORT _sdk_dirs ORDER DESCENDING)
    list(GET _sdk_dirs 0 _sdk)
    target_link_directories(${PROJECT_NAME} BEFORE PRIVATE
        "${_msvc_root}/lib/x64"
        "${_sdk}/ucrt/x64"
        "${_sdk}/um/x64")
endif()
