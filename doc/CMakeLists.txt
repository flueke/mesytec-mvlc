find_package(Doxygen)
find_package(Sphinx)

if (NOT DOXYGEN_FOUND)
    message(STATUS "doxygen not found, documentation will not be built")
    return()
endif()

if (NOT SPHINX_EXECUTABLE)
    message(STATUS "sphinx not found, documentation will not be built")
    return()
endif()

file(REAL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../src DOXYGEN_STRIP_FROM_PATH)

configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
configure_file(source/conf.py.in ${CMAKE_CURRENT_BINARY_DIR}/conf.py)

add_custom_target(mesytec_mvlc_doc_doxygen
    COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    COMMENT "Running doxygen")

add_custom_target(mesytec_mvlc_doc_sphinx
    COMMAND ${SPHINX_EXECUTABLE}
        -E -b html
        -c ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/html
    COMMENT "Building documentation with Sphinx"
)

add_dependencies(mesytec_mvlc_doc_sphinx mesytec_mvlc_doc_doxygen)
add_custom_target(mesytec_mvlc_doc ALL)
add_dependencies(mesytec_mvlc_doc mesytec_mvlc_doc_sphinx)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html/ DESTINATION doc/mesytec-mvlc)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doxygen/html/ DESTINATION doc/mesytec-mvlc/doxygen)
