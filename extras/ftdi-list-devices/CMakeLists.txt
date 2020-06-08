add_executable(ftdi-list-devices ftdi_list_devices_main.cc)

if(WIN32)
    target_link_libraries(ftdi-list-devices ftd3xx)
else(WIN32)
    target_link_libraries(ftdi-list-devices ftd3xx-static)
endif(WIN32)
