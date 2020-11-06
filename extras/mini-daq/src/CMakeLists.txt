add_executable(mini-daq mini_daq_main.cc)
target_link_libraries(mini-daq
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )

add_executable(mini-daq-replay mini_daq_replay.cc)
target_link_libraries(mini-daq-replay
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )

install(TARGETS mini-daq mini-daq-replay
    RUNTIME DESTINATION bin)
