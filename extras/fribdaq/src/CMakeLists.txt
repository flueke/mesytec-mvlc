
set(NSCLDAQ_ROOT "_"  CACHE STRING "_")
set(NSCLDAQ_INC ${NSCLDAQ_ROOT}/include CACHE STRING "${NSCLDAQ_ROOT}/include")
set(NSCLDAQ_LIB ${NSCLDAQ_ROOT}/lib CACHE STRING "${NSCLDAQ_ROOT}/lib")

# Only build any of this stuff if NSCLDAQ_ROOT has been defined.
# 

if (NSCLDAQ_ROOT STREQUAL "_")    # not defined.
    set(exclusion_patterns PATTERN fribdaq-readout EXCLUDE)
else ()
    add_executable(fribdaq-readout fribdaq_readout.cc)
    target_link_libraries(fribdaq-readout
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog
    )    
    install(TARGETS fribdaq-readout)
endif ()



