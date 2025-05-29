
set(NSCLDAQ_ROOT "_"  CACHE STRING "_")


# Only build any of this stuff if NSCLDAQ_ROOT has been defined.
# 

if (NSCLDAQ_ROOT STREQUAL "_")    # not defined.
    
else ()
    find_package(TCL REQUIRED)

    if (NOT ${TCL_FOUND})
        message(FATAL_ERROR, "TCl headers and libraries must be installed")
    endif()
    add_executable(fribdaq-readout 
    fribdaq_readout.cc
    username.cc
    parser_callbacks.cc
    ReadoutCommand.cc
    StateUtils.cc
    BeginCommand.cc
    EndCommand.cc
    PauseCommand.cc
    ResumeCommand.cc
    RunStateCommand.cc
    InitCommand.cc
    StatisticsCommand.cc
    RunVarCommand.cc
    TclServer.cc
    SlowControlsDriver.cc
    SlowControlsModuleCommand.cc
    SlowControlsSetCommand.cc
    SlowControlsGetCommand.cc
    SlowControlsUpdateCommand.cc
    SlowControlsMonCommand.cc
    SlowControlsTestDevice.cc
    CGDG.cc
    CV812.cc
    CMxDCRCBus.cc
    CMxDCReset.cc
    CV6533.cc
    CVMEModule.cc
    )
    target_include_directories(
        fribdaq-readout PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${NSCLDAQ_ROOT}/include
        ${TCL_INCLUDE_PATH}
    )
    target_link_libraries(fribdaq-readout
        PRIVATE mesytec-mvlc
        PRIVATE BFG::Lyra
        PRIVATE spdlog::spdlog
        PRIVATE ${NSCLDAQ_ROOT}/lib/libdataformat.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/liburl.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libdaqshm.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libDataFlow.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libtclPlus.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libException.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libConfigurableObject.so
        PRIVATE ${TCL_LIBRARY}
    )  
    target_link_options(fribdaq-readout PRIVATE
        -ldl -Wl,-rpath=${NSCLDAQ_ROOT}/lib -lm
    )  
    install(TARGETS fribdaq-readout)

    # Slow controls client library:

    add_library(slowControlsClient SHARED
        CVMEClient.cc
    )
    target_include_directories(slowControlsClient PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${NSCLDAQ_ROOT}/include
        ${TCL_INCLUDE_PATH}
    )
    target_link_libraries(slowControlsClient 
        PRIVATE ${NSCLDAQ_ROOT}/lib/libtclPlus.so
        PRIVATE ${NSCLDAQ_ROOT}/lib/libException.so
        PRIVATE ${TCL_LIBRARY}
    )
    target_link_options(slowControlsClient PRIVATE
        -Wl,-rpath=${NSCLDAQ_ROOT}/lib -lm
    )
    set_target_properties(slowControlsClient PROPERTIES PUBLIC_HEADER CVMEClient.h)
    # parser_callbacks.h must be installed.
    # We'll put it in ${CMAKE_INSTALL_INCLUDEDIR}/fribdaq

    set_target_properties(fribdaq-readout PROPERTIES PUBLIC_HEADER parser_callbacks.h)
    install(TARGETS fribdaq-readout slowControlsClient
        PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fribdaq"
    )

    

    # Tests:

    add_executable(parse_test parse_test.cc command_parse.cc)
    target_link_libraries(parse_test
            PRIVATE gtest
            PRIVATE gtest_main
    )
    add_test(NAME parse_test COMMAND parse_test)
endif ()



