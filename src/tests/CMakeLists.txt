cmake_minimum_required(VERSION 3.14)

enable_testing()

find_package(GTest REQUIRED)
include(GoogleTest)

set(BUILD_TARGET tests)
set(RUN_TARGET run_unit_tests)

file(GLOB TESTS CONFIGURE_DEPENDS "*.h" "*.cpp")

add_executable(${BUILD_TARGET} ${TESTS} ${QMLAV_FILES})

target_link_libraries(${BUILD_TARGET} PRIVATE GTest::GTest GTest::Main)
foreach (ITEM ${QT_LINK_DEPENDENCIES})
    target_link_libraries(${BUILD_TARGET} PRIVATE Qt${QT_VERSION_MAJOR}::${ITEM})
endforeach()
target_link_libraries(${BUILD_TARGET} PRIVATE ${QMLAV_LINK_DEPENDENCIES})

gtest_add_tests(TARGET ${BUILD_TARGET})

add_custom_target(${RUN_TARGET} ALL
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${BUILD_TARGET})
