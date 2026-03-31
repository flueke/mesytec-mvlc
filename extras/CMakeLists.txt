#option(MVLC_ENABLE_PYTHON "Build the python wrapper module for mesytec-mvlc" OFF)
#
#if (MVLC_ENABLE_PYTHON)
#    add_subdirectory(python)
#endif(MVLC_ENABLE_PYTHON)

add_subdirectory(mini-daq)
add_subdirectory(mvlc-ctrl-tests)
add_subdirectory(dev-tools)
add_subdirectory(mvlc-cli)
add_subdirectory(fribdaq)

if (UNIX AND NOT APPLE)
    add_executable(netlink-test netlink-socket-mem-monitoring/netlink-test.cc)

    add_executable(netlink-monitor-rmem netlink-socket-mem-monitoring/netlink-monitor-rmem.cc)
    target_link_libraries(netlink-monitor-rmem
        PRIVATE mesytec-mvlc
        PRIVATE bfg::lyra
        PRIVATE spdlog::spdlog)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cmake-example
    DESTINATION share/mesytec-mvlc)

find_package(Boost CONFIG)

if (Boost_FOUND)
    find_package(Threads REQUIRED)
    add_executable(mvlc_stream_client_asio mvlc_stream_client_asio.cc)
    target_link_libraries(mvlc_stream_client_asio PRIVATE mesytec-mvlc Threads::Threads Boost::boost spdlog::spdlog)
    target_compile_definitions(mvlc_stream_client_asio PRIVATE ASIO_SEPARATE_COMPILATION)
    target_include_directories(mvlc_stream_client_asio SYSTEM PRIVATE ${MESYTEC_MVLC_SOURCE_DIR}/external/asio-1.36.0/include)
    target_sources(mvlc_stream_client_asio PRIVATE ${MESYTEC_MVLC_SOURCE_DIR}/external/asio-1.36.0/src/asio.cpp)
    # hide asio symbols to prevent clashes (perplexity.ai, check if actually required)
    set_target_properties(mvlc_stream_client_asio PROPERTIES CXX_VISIBILITY_PRESET hidden)
    install(TARGETS mvlc_stream_client_asio RUNTIME DESTINATION bin)
endif(Boost_FOUND)
