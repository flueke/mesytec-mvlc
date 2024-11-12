if (WIN32)
    add_library(ftd3xx SHARED IMPORTED GLOBAL)

    set_target_properties(ftd3xx PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.8/x64/DLL/FTD3XX.dll
        IMPORTED_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.8/x64/DLL/FTD3XX.lib)

    target_include_directories(ftd3xx
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.8)

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.8/x64/DLL/FTD3XX.dll
        DESTINATION bin)

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.8/x64/DLL/FTD3XX.dll
        DESTINATION ${CMAKE_BINARY_DIR})

    # Note: I cannot get this to link properly: ftdi_list_devices_main.cc:34: undefined reference to `__imp_FT_CreateDeviceInfoList'
    #add_library(ftd3xx-static STATIC IMPORTED GLOBAL)

    #set_target_properties(ftd3xx-static PROPERTIES
    #    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2/x64/Static/FTD3XX.lib
    #    IMPORTED_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2/x64/Static/FTD3XX.lib)

    #target_include_directories(ftd3xx-static
    #    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2)

elseif(UNIX AND NOT APPLE)
    #set_target_properties(ftd3xx PROPERTIES
    #    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/libftd3xx.so
    #    IMPORTED_NO_SONAME TRUE)

    #target_include_directories(ftd3xx
    #    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/)

    #install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/libftd3xx.so
    #    DESTINATION lib)

    #file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/libftd3xx.so
    #    DESTINATION ${CMAKE_BINARY_DIR})

    add_library(ftd3xx-static STATIC IMPORTED GLOBAL)

    if (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")

        set_target_properties(ftd3xx-static PROPERTIES
            IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/libftd3xx-static.a)

        target_include_directories(ftd3xx-static
            INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/)

    elseif (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")

        set_target_properties(ftd3xx-static PROPERTIES
            IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/linux-arm-v8/libftd3xx-static.a)

        target_include_directories(ftd3xx-static
            INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/linux-arm-v8/)

    endif()

    find_package(Threads)

    target_link_libraries(ftd3xx-static INTERFACE Threads::Threads)
elseif(APPLE)
    add_library(ftd3xx SHARED IMPORTED GLOBAL)

    set_target_properties(ftd3xx PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx/libftd3xx.dylib
        IMPORTED_NO_SONAME TRUE)

    target_include_directories(ftd3xx
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx)

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx/libftd3xx.dylib
        DESTINATION lib)

    #file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx/libftd3xx.dylib
    #    DESTINATION ${CMAKE_BINARY_DIR})
endif()
