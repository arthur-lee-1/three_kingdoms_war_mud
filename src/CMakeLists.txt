add_library(tkw_io INTERFACE)

target_include_directories(tkw_io INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tkw_io INTERFACE pjh_result pjh_platform)

add_library(tkw_config INTERFACE)

target_include_directories(tkw_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tkw_config INTERFACE tkw_io pjh_json)

add_library(tkw_entity INTERFACE)

target_include_directories(tkw_entity INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tkw_entity INTERFACE tkw_event pjh_result)

add_library(tkw_event INTERFACE)

target_include_directories(tkw_event INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(tkw_core INTERFACE)

target_include_directories(tkw_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tkw_core INTERFACE tkw_entity tkw_config tkw_event tkw_io pjh_result)

add_executable(tkw
    main.cpp
)

target_link_libraries(tkw PRIVATE tkw_core pjh_json pjh_cli pjh_platform)
