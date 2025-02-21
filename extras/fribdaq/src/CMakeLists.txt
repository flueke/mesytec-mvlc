
set(NSCLDAQ_ROOT "_"  CACHE STRING "_")


# Only build any of this stuff if NSCLDAQ_ROOT has been defined.
# 

if (NSCLDAQ_ROOT STREQUAL "_")    # not defined.
    
else ()
    
    add_executable(fribdaq-readout 
    fribdaq_readout.cc
    command_parse.cc
    username.cc
    parser_callbacks.cc
    )
    target_include_directories(
        fribdaq-readout PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${NSCLDAQ_ROOT}/include
    )
    target_link_libraries(fribdaq-readout
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog
        PRIVATE ${NSCLDAQ_ROOT}/lib/libdataformat.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/liburl.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libdaqshm.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libDataFlow.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libException.so
    )    
    install(TARGETS fribdaq-readout)

    # Tests:

    add_executable(parse_test parse_test.cc command_parse.cc)
    target_link_libraries(parse_test
            PRIVATE gtest
            PRIVATE gtest_main
    )
    add_test(NAME parse_test COMMAND parse_test)
endif ()



