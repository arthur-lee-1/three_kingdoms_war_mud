add_library(tkw_core INTERFACE)

target_include_directories(tkw_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tkw_core INTERFACE pjh_result)

add_executable(tkw
    main.cpp
)

target_link_libraries(tkw PRIVATE tkw_core pjh_result pjh_json pjh_cli pjh_platform)
