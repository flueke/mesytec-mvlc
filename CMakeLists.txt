cmake_minimum_required(VERSION 3.18)

project(mesytec-mvlc
    DESCRIPTION "User space driver library for the Mesytec MVLC VME controller"
    LANGUAGES C CXX)

# Check if mesytec-mvlc is being used directly or via add_subdirectory
set(MESYTEC_MVLC_MASTER_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MESYTEC_MVLC_MASTER_PROJECT ON)
endif()

# Taken from nng/CMakeLists.txt
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_definitions(-DMESYTEC_MVLC_PLATFORM_POSIX)
    add_definitions(-DMESYTEC_MVLC_PLATFORM_LINUX)
    # Windows subsystem for Linux -- smells like Linux, but it has
    # some differences (SO_REUSEADDR for one).
    if (CMAKE_SYSTEM_VERSION MATCHES "Microsoft")
        add_definitions(-DMESYTEC_MVLC_PLATFORM_WSL)
    endif ()
    set(MESYTEC_MVLC_PLATFORM_LINUX ON)
    set(MESYTEC_MVLC_PLATFORM_POSIX ON)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_definitions(-DMESYTEC_MVLC_PLATFORM_WINDOWS)
    set(MESYTEC_MVLC_PLATFORM_WINDOWS ON)
else ()
    message(AUTHOR_WARNING "WARNING: This platform may not be supported: ${CMAKE_SYSTEM_NAME}")
    # blithely hope for POSIX to work
    add_definitions(-DMESYTEC_MVLC_PLATFORM_POSIX)
    set(MESYTEC_MVLC_PLATFORM_POSIX ON)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")

# Add the local cmake directory module path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake")

# Pull information from git:
# GIT_SHA1 contains the complete git hash
#
# GIT_VERSION contains the name of the latest git tag, the number of commits
# since that tag was created and the start of the git hash if there have been
# changes since the tag was created.
# Example GIT_VERSION: 0.7-371-g12d9678

# GIT_VERSION_SHORT contains the same as GIT_VERSION but without the start of
# the last commits hash.
# Example GIT_VERSION_SHORT: 0.7-371

# GIT_VERSION_TAG contains only the tag part of the version
# Example GIT_VERSION_TAG: 0.7
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")
string(REGEX REPLACE "^v" "" GIT_VERSION ${GIT_VERSION})
string(REGEX MATCH "^[0-9.]+(-beta[0-9]*|-rc[0-9]*|-dev[0-9]*)?(-[0-9]+)?" GIT_VERSION_SHORT ${GIT_VERSION})
string(REGEX MATCH "^[0-9.]+" GIT_VERSION_TAG ${GIT_VERSION})

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

#message("-- GIT_VERSION=${GIT_VERSION}")
#message("-- GIT_VERSION_SHORT=${GIT_VERSION_SHORT}")
#message("-- GIT_VERSION_TAG=${GIT_VERSION_TAG}")
#message("-- GIT_BRANCH=${GIT_BRANCH}")

set(not-msvc $<NOT:$<CXX_COMPILER_ID:MSVC>>)

option(MVLC_BUILD_TESTS "Build test binaries" ${MESYTEC_MVLC_MASTER_PROJECT})
option(MVLC_BUILD_CONTROLLER_TESTS "Build online MVLC controller tests" OFF)
option(MVLC_BUILD_DEV_TOOLS "Build developer tools" ${MESYTEC_MVLC_MASTER_PROJECT})
option(MVLC_BUILD_TOOLS "Build MVLC related tools (vme-scan-bus)" ${MESYTEC_MVLC_MASTER_PROJECT})

if (MVLC_BUILD_TESTS OR MVLC_BUILD_CONTROLLER_TESTS)
    if (NOT TARGET gtest)
        option(INSTALL_GTEST "Enable installation of googletest." OFF)
        add_subdirectory(external/googletest)
    endif()

    message("-- Building tests")
    include(CTest)
    enable_testing()
endif ()

add_subdirectory(external)

# Create our binaries in the root of the build directory. It's just more convenient.
if (MESYTEC_MVLC_MASTER_PROJECT)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

option(MVLC_BUILD_DOCS "Build the documentation (requires sphinx, breathe and doxygen)"
    ${MESYTEC_MVLC_MASTER_PROJECT})

if (MVLC_BUILD_DOCS)
    add_subdirectory(doc)
endif (MVLC_BUILD_DOCS)

add_subdirectory(extras)
add_subdirectory(src)
