find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_EXCLUDE_PATTERNS *.test.cc)

    doxygen_add_docs(doc
        ../README.md
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        )
endif (DOXYGEN_FOUND)
