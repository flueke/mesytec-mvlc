function(add_mvlc_test exe_name exe_source)
    add_executable(${exe_name} ${exe_source})

    target_link_libraries(${exe_name}
        PRIVATE mesytec-mvlc
        PRIVATE gtest
        PRIVATE gtest_main
        )

    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvlc_test)

if (MVLC_BUILD_CONTROLLER_TESTS)
    #add_mvlc_test(mvlc_ctrl_test_vme_bus mvlc_ctrl_test_vme_bus.cc)
    #add_mvlc_test(mvlc_ctrl_test_stack_executor mvlc_ctrl_test_stack_executor.cc)
    add_mvlc_test(mvlc_ctrl_test_eth mvlc_ctrl_test_eth.cc)
    add_mvlc_test(mvlc_ctrl_test_usb mvlc_ctrl_test_usb.cc)
    add_mvlc_test(mvlc_ctrl_test_basic mvlc_ctrl_test_basic.cc)
    add_executable(mvlc_ctrl_test_usb_no_gtest mvlc_ctrl_test_usb_no_gtest.cc)
    target_link_libraries(mvlc_ctrl_test_usb_no_gtest
        PRIVATE mesytec-mvlc
        PRIVATE spdlog::spdlog
    )
endif (MVLC_BUILD_CONTROLLER_TESTS)
