function(add_mvlc_test exe_name exe_source)
    add_executable(${exe_name} ${exe_source})

    target_link_libraries(${exe_name}
        PRIVATE mesytec-mvlc
        PRIVATE gtest
        PRIVATE gtest_main
        PRIVATE fmt::fmt-header-only # does add -DFMT_HEADER_ONLY=1
        )

    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvlc_test)

if (MVLC_BUILD_CONTROLLER_TESTS)
    add_mvlc_test(mvlc_test_vme_bus mvlc_test_vme_bus.cc)
endif (MVLC_BUILD_CONTROLLER_TESTS)
