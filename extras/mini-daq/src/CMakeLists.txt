add_executable(mvlc-mini-daq mini_daq_main.cc)
target_link_libraries(mvlc-mini-daq
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

add_executable(mvlc-mini-daq-replay mini_daq_replay.cc)
target_link_libraries(mvlc-mini-daq-replay
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

add_executable(mvlc-mini-daq-replay-blocking mini_daq_replay_blocking.cc)
target_link_libraries(mvlc-mini-daq-replay-blocking
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

add_executable(mvlc-multi-crate-replay multi_crate_replay.cc)
target_link_libraries(mvlc-multi-crate-replay
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

add_executable(mdpp16_readout_example1 mdpp16-readout-example1.cc)
target_link_libraries(mdpp16_readout_example1
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

install(TARGETS
    mvlc-mini-daq
    mvlc-mini-daq-replay
    mvlc-mini-daq-replay-blocking
    RUNTIME DESTINATION bin)
