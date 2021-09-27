find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_EXCLUDE_PATTERNS
        *.test.cc)

    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_TOC_INCLUDE_HEADINGS 0)
    set(DOXYGEN_ALWAYS_DETAILED_SEC YES)
    set(DOXYGEN_TYPEDEF_HIDES_STRUCT NO)
    set(DOXYGEN_SOURCE_BROWSER YES)

    doxygen_add_docs(mesytec-mvlc-doc ALL
        ../README.md
        ../TODO.md
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extras/mvlc-c
        ${CMAKE_SOURCE_DIR}/extras/mini-daq
        )

    add_custom_command(TARGET mesytec-mvlc-doc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/images
            ${CMAKE_CURRENT_BINARY_DIR}/html/doc/images)

    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html
        DESTINATION share/doc/mesytec-mvlc)

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../README.md
        ${CMAKE_CURRENT_SOURCE_DIR}/usage-guide.md
        DESTINATION share/doc/mesytec-mvlc)
endif (DOXYGEN_FOUND)
