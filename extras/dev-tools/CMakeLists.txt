add_executable(full_duplex_cmd_test full_duplex_cmd_test.cc)
target_link_libraries(full_duplex_cmd_test
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )
