include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.0)

set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

add_executable(tkw_tests test_main.cpp test_smoke.cpp)

target_link_libraries(tkw_tests PRIVATE tkw_core doctest)

target_compile_definitions(tkw_tests PRIVATE TKW_BUILD_TESTS)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(tkw_tests)

add_executable(entity_tests test_main.cpp entity/hp_test.cpp entity/event_test.cpp
                        entity/effect_test.cpp entity/manager_test.cpp)

target_link_libraries(entity_tests PRIVATE tkw_entity doctest)

doctest_discover_tests(entity_tests)

add_executable(config_tests test_main.cpp config/config_test.cpp)

target_link_libraries(config_tests PRIVATE tkw_config doctest)

doctest_discover_tests(config_tests)

add_executable(io_tests test_main.cpp io/file_test.cpp)

target_link_libraries(io_tests PRIVATE tkw_io doctest)

doctest_discover_tests(io_tests)

add_executable(event_tests test_main.cpp event/event_test.cpp)

target_link_libraries(event_tests PRIVATE tkw_event doctest)

doctest_discover_tests(event_tests)

add_executable(handler_tests test_main.cpp event/handler_test.cpp)

target_link_libraries(handler_tests PRIVATE tkw_event doctest)

doctest_discover_tests(handler_tests)

add_executable(card_tests test_main.cpp card/catalog_test.cpp card/manager_test.cpp)

target_link_libraries(card_tests PRIVATE tkw_card doctest)

target_compile_definitions(
    card_tests PRIVATE TKW_TEST_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")

doctest_discover_tests(card_tests)

add_executable(game_tests test_main.cpp game/game_test.cpp game/replay_test.cpp)

target_link_libraries(game_tests PRIVATE tkw_game doctest)

target_compile_definitions(
    game_tests PRIVATE TKW_TEST_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")

doctest_discover_tests(game_tests)
