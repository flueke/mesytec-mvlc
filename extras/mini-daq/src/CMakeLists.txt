add_executable(mini-daq mini_daq_main.cc)
target_link_libraries(mini-daq
    PRIVATE mesytec-mvlc
    PRIVATE fmt::fmt-header-only # does add -DFMT_HEADER_ONLY=1
    )

add_executable(mini-daq-replay mini_daq_replay.cc)
target_link_libraries(mini-daq-replay
    PRIVATE mesytec-mvlc
    PRIVATE fmt::fmt-header-only # does add -DFMT_HEADER_ONLY=1
    )
