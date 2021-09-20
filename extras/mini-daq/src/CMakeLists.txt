add_executable(mvlc-mini-daq mini_daq_main.cc)
target_link_libraries(mvlc-mini-daq
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

add_executable(mvlc-mini-daq2 mini_daq_main2.cc)
target_link_libraries(mvlc-mini-daq2
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

add_executable(mvlc-mini-daq-replay2 mini_daq_replay2.cc)
target_link_libraries(mvlc-mini-daq-replay2
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    PRIVATE spdlog::spdlog
    )

install(TARGETS mvlc-mini-daq mvlc-mini-daq-replay mvlc-mini-daq2 mvlc-mini-daq-replay2
    RUNTIME DESTINATION bin)

add_executable(dev_mvlc_replay_simple_api replay_simple_api.cc)
target_link_libraries(dev_mvlc_replay_simple_api
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )
