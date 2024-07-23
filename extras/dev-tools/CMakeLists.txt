if (MVLC_BUILD_DEV_TOOLS)
    add_executable(full_duplex_cmd_test full_duplex_cmd_test.cc)
    target_link_libraries(full_duplex_cmd_test
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        )

    add_executable(request_queue_playground request_queue_playground.cc)
    target_link_libraries(request_queue_playground
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog
        )
    add_executable(mvlc-reconnect-test reconnect-test.cc)
    target_link_libraries(mvlc-reconnect-test
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog
        )

    add_executable(decode-mvlc-frame-header decode_mvlc_frame_header.cc)
    target_link_libraries(decode-mvlc-frame-header
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(decode-mvlc-eth-headers decode_mvlc_eth_headers.cc)
    target_link_libraries(decode-mvlc-eth-headers
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)


    add_executable(mvlc_temp_debug mvlc_temp_debug.cc)
    target_link_libraries(mvlc_temp_debug
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)
    install(TARGETS mvlc_temp_debug RUNTIME DESTINATION bin)

    add_executable(gsi-listfile-info gsi_listfile_info.cc)
    target_link_libraries(gsi-listfile-info
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)
    install(TARGETS gsi-listfile-info RUNTIME DESTINATION bin)

if (NOT WIN32)
    add_executable(mvlc-register-rw-loop mvlc_register_rw_loop.cc)
    target_link_libraries(mvlc-register-rw-loop
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)
    install(TARGETS mvlc-register-rw-loop RUNTIME DESTINATION bin)
endif()
endif(MVLC_BUILD_DEV_TOOLS)

if (MVLC_BUILD_TOOLS)
    add_executable(mvlc-listfile-info mvlc_listfile_info.cc)
    target_link_libraries(mvlc-listfile-info
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)
    install(TARGETS mvlc-listfile-info RUNTIME DESTINATION bin)

    if(MVLC_ENABLE_ZMQ)
        add_executable(mvlc-zmq-test-receiver mvlc_zmq_test_receiver.cc)
        target_link_libraries(mvlc-zmq-test-receiver PRIVATE mesytec-mvlc)

        add_executable(mvlc-zmq-listfile-sender mvlc_zmq_listfile_sender.cc)
        target_link_libraries(mvlc-zmq-listfile-sender PRIVATE mesytec-mvlc PRIVATE BFG::Lyra)
    endif(MVLC_ENABLE_ZMQ)

endif(MVLC_BUILD_TOOLS)
