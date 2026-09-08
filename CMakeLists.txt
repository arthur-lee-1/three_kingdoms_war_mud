cmake_minimum_required(VERSION 3.21)

project(three_kingdoms_war VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TKW_ENABLE_TESTS "Compile three_kingdoms_war tests" ON)

add_subdirectory(thirdparty/pjh_result)
add_subdirectory(thirdparty/pjh_json)
add_subdirectory(thirdparty/pjh_cli)
add_subdirectory(thirdparty/pjh_platform)

add_subdirectory(src)

if(TKW_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
