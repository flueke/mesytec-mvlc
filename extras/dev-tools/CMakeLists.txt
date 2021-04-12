add_executable(full_duplex_cmd_test full_duplex_cmd_test.cc)
target_link_libraries(full_duplex_cmd_test
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )

add_executable(request_queue_playground request_queue_playground.cc)
target_link_libraries(request_queue_playground
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog_header_only
    )
target_compile_definitions(request_queue_playground PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
