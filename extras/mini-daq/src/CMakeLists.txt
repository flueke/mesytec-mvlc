add_executable(mini-daq mini_daq_main.cc)
target_link_libraries(mini-daq
    PRIVATE mesytec-mvlc
    PRIVATE fmt::fmt-header-only # does add -DFMT_HEADER_ONLY=1
    )
