if (MVLC_BUILD_C_WRAPPER)
    add_library(mesytec-mvlc-c SHARED mvlc_c.cc)

    target_include_directories(mesytec-mvlc-c
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/>
        PUBLIC $<INSTALL_INTERFACE:include>
        )

    target_link_libraries(mesytec-mvlc-c
        PUBLIC mesytec-mvlc
        PRIVATE spdlog::spdlog
        )

    target_compile_options(mesytec-mvlc-c PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_features(mesytec-mvlc-c PUBLIC c_std_11)

    include(GenerateExportHeader)
    generate_export_header(mesytec-mvlc-c)

    # Export and installation
    include(CMakePackageConfigHelpers)
    set(project_config ${PROJECT_BINARY_DIR}/mesytec-mvlc-c-config.cmake)
    set(targets_export_name mesytec-mvlc-c-targets)

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/tools/cmake/mesytec-mvlc-c-config.cmake.in"
        ${project_config}
        INSTALL_DESTINATION lib/cmake/mesytec-mvlc)

    set(INSTALL_TARGETS mesytec-mvlc-c)
    export(TARGETS ${INSTALL_TARGETS} NAMESPACE mesytec-mvlc::
        FILE ${PROJECT_BINARY_DIR}/${targets_export_name}.cmake)

    install(FILES ${project_config}
        DESTINATION lib/cmake/mesytec-mvlc-c)

    install(EXPORT ${targets_export_name}
        DESTINATION lib/cmake/mesytec-mvlc-c
        NAMESPACE mesytec-mvlc::
        )

    install(TARGETS ${INSTALL_TARGETS}
        EXPORT ${targets_export_name}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION include/mesytec-mvlc
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mesytec-mvlc-c_export.h
        DESTINATION include/mesytec-mvlc)

    add_executable(mvlc_c_test1 mvlc_c_test1.c)
    target_link_libraries(mvlc_c_test1 mesytec-mvlc-c)
    target_compile_options(mvlc_c_test1 PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(mvlc_c_test2 mvlc_c_test2.c)
    target_link_libraries(mvlc_c_test2 mesytec-mvlc-c)
    target_compile_options(mvlc_c_test2 PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(mvlc_c_test3 mvlc_c_test3.c)
    target_link_libraries(mvlc_c_test3 mesytec-mvlc-c)
    target_compile_options(mvlc_c_test3 PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(mvlc_c_replay mvlc_c_replay.c)
    target_link_libraries(mvlc_c_replay mesytec-mvlc-c)
    target_compile_options(mvlc_c_replay PRIVATE -Wall -Wextra -Wpedantic)

    install(TARGETS
        mvlc_c_test1
        mvlc_c_test2
        mvlc_c_test3
        RUNTIME DESTINATION bin)

    if (NOT WIN32)
        add_executable(mvlc_test_advanced mvlc_test_advance.c)
        target_link_libraries(mvlc_test_advanced mesytec-mvlc-c)
        target_compile_options(mvlc_test_advanced PRIVATE -Wall -Wextra -Wpedantic)
        install(TARGETS mvlc_test_advanced
        RUNTIME DESTINATION bin)
    endif()

endif(MVLC_BUILD_C_WRAPPER)
