add_executable(mini-daq mini_daq_main.cc mini_daq_lib.cc)
target_link_libraries(mini-daq
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )

add_executable(mini-daq-replay mini_daq_replay.cc mini_daq_lib.cc)
target_link_libraries(mini-daq-replay
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )

add_executable(mini-daq-manual-readout mini_daq_manual_readout.cc mini_daq_lib.cc)
target_link_libraries(mini-daq-manual-readout
    PRIVATE mesytec-mvlc
    PRIVATE BFG::Lyra
    )

install(TARGETS mini-daq mini-daq-replay mini-daq-manual-readout
    RUNTIME DESTINATION bin)
