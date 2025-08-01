# ftdi d3xx drivers
add_subdirectory(ftdi-d3xx)

# minizip options
option(MZ_COMPAT "Enables compatibility layer" ON)
option(MZ_ZLIB "Enables ZLIB compression" ON)
option(MZ_BZIP2 "Enables BZIP2 compression" OFF)
option(MZ_LZMA "Enables LZMA & XZ compression" ON)
option(MZ_ZSTD "Enables ZSTD compression" OFF)
option(MZ_PKCRYPT "Enables PKWARE traditional encryption" OFF)
option(MZ_WZAES "Enables WinZIP AES encryption" OFF)
option(MZ_LIBCOMP "Enables Apple compression" OFF)
option(MZ_OPENSSL "Enables OpenSSL for encryption" OFF)
option(MZ_BRG "Enables Brian Gladman's encryption library" OFF)
option(MZ_SIGNING "Enables zip signing support" OFF)
option(MZ_COMPRESS_ONLY "Only support compression" OFF)
option(MZ_DECOMPRESS_ONLY "Only support decompression" OFF)
option(MZ_BUILD_TEST "Builds minizip test executable" OFF)
option(MZ_BUILD_UNIT_TEST "Builds minizip unit test project" OFF)
option(MZ_BUILD_FUZZ_TEST "Builds minizip fuzzer executables" OFF)
option(MZ_CODE_COVERAGE "Builds with code coverage flags" OFF)
set(SKIP_INSTALL_ALL ON)

add_subdirectory(minizip-ng-3.0.10)

target_compile_options(minizip
    PRIVATE $<${not-msvc}:-Wno-unused-variable -Wno-unused-but-set-variable>
    PRIVATE $<${not-msvc}:-Wno-unused-parameter -Wno-cast-function-type -Wno-unused-local-typedefs>)

target_include_directories(minizip
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/minizip-ng-3.0.10>)

# pybind11
if (MVLC_ENABLE_PYTHON)
    add_subdirectory(pybind11-2.5.0)
endif(MVLC_ENABLE_PYTHON)

# yaml-cpp
option(YAML_CPP_BUILD_TESTS "Enable testing" OFF)
option(YAML_CPP_BUILD_TOOLS "Enable parse tools" OFF)
option(YAML_CPP_BUILD_CONTRIB "Enable contrib stuff in library" OFF)
option(YAML_CPP_INSTALL "Enable generation of install target" OFF)
option(YAML_BUILD_SHARED_LIBS "Build Shared Libraries" OFF)
add_subdirectory(yaml-cpp)
target_compile_options(yaml-cpp PRIVATE -Wno-shadow)

# nlohmann/json
set(JSON_BuildTests OFF CACHE INTERNAL "")

# If you only include this third party in PRIVATE source files, you do not
# need to install it when your main project gets installed.
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(nlohmann_json)

# Enable the PIC flag to fix a relocation error when linking against the
# static version of yaml-cpp.
set_target_properties(yaml-cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (NOT MESYTEC_MVLC_PLATFORM_WINDOWS) # windows uses lz4 from vcpkg
    # LZ4 TODO: get rid of this once the builders have lz4 installed
    # This builds the lz4_static target for linux (and macos).
    message(STATUS "${PROJECT_NAME}: Using lz4 from ${CMAKE_CURRENT_SOURCE_DIR}/lz4")
    add_subdirectory(lz4/contrib/cmake_unofficial)
    target_include_directories(lz4_static
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lz4/lib/>)
endif()

# Lyra command line parser
add_subdirectory(Lyra)

# spdlog
if (NOT TARGET spdlog)
    message(STATUS "${PROJECT_NAME}: Using spdlog from ${CMAKE_CURRENT_SOURCE_DIR}/spdlog")
    option(SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" OFF)
    option(SPDLOG_INSTALL "Generate the install target" ON)
    add_subdirectory(spdlog-1.14.0)
    set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# argh command line parser
add_subdirectory(argh)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/argh/argh.h" DESTINATION include/mesytec-mvlc)
target_include_directories(argh INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/argh> $<INSTALL_INTERFACE:include/mesytec-mvlc>)
