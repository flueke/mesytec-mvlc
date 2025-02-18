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
        PRIVATE BFG::Lyra)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cmake-example
    DESTINATION share/mesytec-mvlc)
