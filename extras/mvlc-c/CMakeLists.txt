if (MVLC_BUILD_C_WRAPPER)
    add_library(mesytec-mvlc-c SHARED mvlc_c.cc)

    target_include_directories(mesytec-mvlc-c
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/>
        PUBLIC $<INSTALL_INTERFACE:include>
        )

    target_link_libraries(mesytec-mvlc-c
        PRIVATE mesytec-mvlc
        PRIVATE spdlog::spdlog
        )

    target_compile_options(mesytec-mvlc-c PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_features(mesytec-mvlc-c PUBLIC c_std_11)

    include(GenerateExportHeader)
    generate_export_header(mesytec-mvlc-c)

    install(TARGETS mesytec-mvlc-c
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION include/mesytec-mvlc
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")


    add_executable(mvlc_c_test1 mvlc_c_test1.c)
    target_link_libraries(mvlc_c_test1 mesytec-mvlc-c)
    target_compile_options(mvlc_c_test1 PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(mvlc_c_test2 mvlc_c_test2.c)
    target_link_libraries(mvlc_c_test2 mesytec-mvlc-c)
    target_compile_options(mvlc_c_test2 PRIVATE -Wall -Wextra -Wpedantic)
endif(MVLC_BUILD_C_WRAPPER)
