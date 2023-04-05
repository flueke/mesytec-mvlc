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

    add_executable(mvlc-mvp-read-page mvlc_mvp_read_page.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-read-page
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc-mvp-write-page mvlc_mvp_write_page.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-write-page
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc-mvp-erase-section mvlc_mvp_erase_section.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-erase-section
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc-mvp-flash-write-test mvlc_mvp_flash_write_test.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-flash-write-test
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc-mvp-flash-write-test2 mvlc_mvp_flash_write_test2.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-flash-write-test2
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc-mvp-flash-check-test-pages mvlc_mvp_flash_check_test_pages.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-flash-check-test-pages
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc-mvp-erase-unprotect-23bytes mvlc_mvp_erase_unprotect_23bytes.cc mvlc_mvp_lib.cc)
    target_link_libraries(mvlc-mvp-erase-unprotect-23bytes
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)

    add_executable(mvlc_temp_debug mvlc_temp_debug.cc)
    target_link_libraries(mvlc_temp_debug
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)
    install(TARGETS mvlc_temp_debug RUNTIME DESTINATION bin)
endif(MVLC_BUILD_DEV_TOOLS)

if (MVLC_BUILD_TOOLS)
    add_executable(mvlc-vme-scan-bus mvlc_vme_scan_bus.cc)
    target_link_libraries(mvlc-vme-scan-bus
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog)
    install(TARGETS mvlc-vme-scan-bus RUNTIME DESTINATION bin)

