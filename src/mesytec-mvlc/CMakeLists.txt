find_package(Threads)

configure_file("git_version.cc.in" "git_version.cc" @ONLY)

add_library(mesytec-mvlc SHARED
    "${CMAKE_CURRENT_BINARY_DIR}/git_version.cc"
    mvlc_basic_interface.cc
    mvlc.cc
    mvlc_command_builders.cc
    mvlc_constants.cc
    mvlc_dialog.cc
    mvlc_dialog_util.cc
    mvlc_error.cc
    mvlc_eth_interface.cc
    mvlc_factory.cc
    mvlc_impl_eth.cc
    mvlc_impl_support.cc
    mvlc_impl_usb.cc
    mvlc_listfile.cc
    mvlc_listfile_zip.cc
    mvlc_readout.cc
    mvlc_readout_config.cc
    mvlc_readout_parser.cc
    mvlc_readout_parser_util.cc
    mvlc_replay.cc
    mvlc_stack_errors.cc
    mvlc_stack_executor.cc
    mvlc_usb_interface.cc
    mvlc_util.cc
    readout_buffer.cc
    readout_buffer_queues.cc

    util/filesystem.cc
    util/protected.cc
    util/string_util.cc
    util/threadsafequeue.cc
    util/ticketmutex.cc
    )

#set_source_files_properties(mvlc_impl_eth.cc PROPERTIES
#    COMPILE_FLAGS "-Wno-missing-field-initializers")

target_include_directories(mesytec-mvlc
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC $<INSTALL_INTERFACE:include>
    PUBLIC $<INSTALL_INTERFACE:include/mesytec-mvlc>
    )

target_link_libraries(mesytec-mvlc
    PRIVATE lz4_static
    PRIVATE minizip
    PRIVATE yaml-cpp
    PUBLIC fmt::fmt
    PUBLIC Threads::Threads
    )


if (UNIX AND NOT APPLE)
    target_link_libraries(mesytec-mvlc PRIVATE ftd3xx-static)
else()
    target_link_libraries(mesytec-mvlc PRIVATE ftd3xx)
endif()

target_compile_options(mesytec-mvlc PRIVATE -Wall -Wextra)
target_compile_features(mesytec-mvlc PUBLIC cxx_std_14)

if (WIN32)
    target_link_libraries(mesytec-mvlc PRIVATE ws2_32 winmm)
    target_compile_options(mesytec-mvlc PRIVATE -Wno-format)
endif(WIN32)

include(GenerateExportHeader)
generate_export_header(mesytec-mvlc)

install(TARGETS mesytec-mvlc
    EXPORT libmesytec-mvlc
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    )

install(EXPORT libmesytec-mvlc
    DESTINATION lib/cmake/mesytec-mvlc
    NAMESPACE mesytec-mvlc::
    )

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/mesytec-mvlc
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mesytec-mvlc_export.h
    DESTINATION include/mesytec-mvlc)

#include(CMakePackageConfigHelpers)
set(project_config ${PROJECT_BINARY_DIR}/mesytec-mvlc-config.cmake)
set(targets_export_name libmesytec-mvlc)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/tools/cmake/mesytec-mvlc-config.cmake.in"
    ${project_config}
    INSTALL_DESTINATION lib/cmake/mesytec-mvlc)
install(FILES ${project_config}
    DESTINATION lib/cmake/mesytec-mvlc)

if (MVLC_BUILD_TESTS)
    function(add_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})

        target_link_libraries(${exe_name}
            PRIVATE mesytec-mvlc
            PRIVATE gtest
            PRIVATE gtest_main
            )

        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_gtest)

    add_gtest(test_mvlc_command_builders mvlc_command_builders.test.cc)
    add_gtest(test_mvlc_listfile_zip mvlc_listfile_zip.test.cc)
    target_link_libraries(test_mvlc_listfile_zip PRIVATE minizip)
    add_gtest(test_mvlc_stack_executor mvlc_stack_executor.test.cc)
    add_gtest(test_mvlc_readout_config mvlc_readout_config.test.cc)
    add_gtest(test_threadsafequeue util/threadsafequeue.test.cc)
    add_gtest(test_protected util/protected.test.cc)
    add_gtest(test_mvlc_error mvlc_error.test.cc)
    #target_link_libraries(test_mvlc_error PRIVATE ftd3xx)
endif(MVLC_BUILD_TESTS)
