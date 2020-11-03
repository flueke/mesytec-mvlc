find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_EXCLUDE_PATTERNS
        *.test.cc
        */mesytec-mvlc/fmt/*
        */mesytec-mvlc/format.cc
        */mesytec-mvlc/os.cc)

    set(DOXYGEN_EXTRACT_ALL YES)
    #set(DOXYGEN_TOC_INCLUDE_HEADINGS 10)

    doxygen_add_docs(mesytec-mvlc-doc ALL
        ../README.md
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        )

    add_custom_command(TARGET mesytec-mvlc-doc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/images
            ${CMAKE_CURRENT_BINARY_DIR}/html/doc/images)

    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html
        DESTINATION share/doc)
endif (DOXYGEN_FOUND)
