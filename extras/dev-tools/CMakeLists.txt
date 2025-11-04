if (MVLC_BUILD_DEV_TOOLS)

    function(add_mvlc_dev_tool exe_name exe_source)
        add_executable(${exe_name} ${exe_source})
        target_link_libraries(${exe_name}
            PRIVATE mesytec-mvlc
            PRIVATE BFG::Lyra
            PRIVATE spdlog::spdlog
        )
    endfunction()

    add_mvlc_dev_tool(full_duplex_cmd_test full_duplex_cmd_test.cc)
    add_mvlc_dev_tool(request_queue_playground request_queue_playground.cc)
    add_mvlc_dev_tool(mvlc-reconnect-test reconnect-test.cc)
    add_mvlc_dev_tool(decode-mvlc-frame-header decode_mvlc_frame_header.cc)
    add_mvlc_dev_tool(decode-mvlc-eth-headers decode_mvlc_eth_headers.cc)
    add_mvlc_dev_tool(decode-mvlc-eth-packets decode_mvlc_eth_packets.cc)
    add_mvlc_dev_tool(mvlc-eth-low-level-tests mvlc_eth_low_level_tests.cc)

    add_mvlc_dev_tool(mvlc_temp_debug mvlc_temp_debug.cc)
    install(TARGETS mvlc_temp_debug RUNTIME DESTINATION bin)

    add_mvlc_dev_tool(gsi-listfile-info gsi_listfile_info.cc)
    install(TARGETS gsi-listfile-info RUNTIME DESTINATION bin)

    if (NOT WIN32)
        add_mvlc_dev_tool(mvlc-register-rw-loop mvlc_register_rw_loop.cc)
        install(TARGETS mvlc-register-rw-loop RUNTIME DESTINATION bin)

        add_mvlc_dev_tool(mvlc-init-daq-repeat mvlc_init_daq_repeat.cc)
        install(TARGETS mvlc-init-daq-repeat RUNTIME DESTINATION bin)
    endif()

    add_mvlc_dev_tool(mvlc-vme-write-many mvlc_vme_write_many.cc)
    install(TARGETS mvlc-vme-write-many RUNTIME DESTINATION bin)

    # stream_server & test clients
    add_mvlc_dev_tool(mvlc_stream_test_client_asio mvlc_stream_test_client_asio.cc)
    target_link_libraries(mvlc_stream_test_client_asio PRIVATE asio pthread)

    add_mvlc_dev_tool(mvlc_stream_test_server_asio mvlc_stream_test_server_asio.cc)
    target_link_libraries(mvlc_stream_test_server_asio PRIVATE asio pthread)

    if (WIN32)
        target_link_libraries(mvlc_stream_test_client_asio PRIVATE ws2_32 winmm)
        target_link_libraries(mvlc_stream_test_server_asio PRIVATE ws2_32 winmm)
    endif(WIN32)
endif(MVLC_BUILD_DEV_TOOLS)

if (MVLC_BUILD_TOOLS)
    add_mvlc_dev_tool(mvlc-listfile-info mvlc_listfile_info.cc)
    install(TARGETS mvlc-listfile-info RUNTIME DESTINATION bin)

    if(MVLC_ENABLE_ZMQ)
        add_mvlc_dev_tool(mvlc-zmq-test-receiver mvlc_zmq_test_receiver.cc)
        target_link_libraries(mvlc-zmq-test-receiver PRIVATE mesytec-mvlc)

        add_mvlc_dev_tool(mvlc-zmq-listfile-sender mvlc_zmq_listfile_sender.cc)
        target_link_libraries(mvlc-zmq-listfile-sender PRIVATE mesytec-mvlc PRIVATE BFG::Lyra)
    endif(MVLC_ENABLE_ZMQ)

endif(MVLC_BUILD_TOOLS)
