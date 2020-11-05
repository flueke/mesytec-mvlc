# Disabled until it becomes useful
#option(MVLC_ENABLE_CLI "Build the mvlc-cli demo binary" OFF)
#
#if (MVLC_ENABLE_CLI)
#    add_subdirectory(mvlc-cli)
#endif(MVLC_ENABLE_CLI)

option(MVLC_ENABLE_PYTHON "Build the python wrapper module for mesytec-mvlc" OFF)

if (MVLC_ENABLE_PYTHON)
    add_subdirectory(python)
endif(MVLC_ENABLE_PYTHON)

add_subdirectory(zip-tests)
add_subdirectory(mini-daq)
add_subdirectory(mvlc-ctrl-tests)
add_subdirectory(ftdi-list-devices)

if (UNIX AND NOT APPLE)
    add_executable(netlink-test netlink-socket-mem-monitoring/netlink-test.cc)

    add_executable(netlink-monitor-rmem netlink-socket-mem-monitoring/netlink-monitor-rmem.cc)
    target_link_libraries(netlink-monitor-rmem
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/mesytec-mvlc-cmake-example
    DESTINATION share/mesytec-mvlc)
