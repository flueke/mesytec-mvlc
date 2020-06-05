add_executable(ftdi-list-devices ftdi_list_devices_main.cc)
target_link_libraries(ftdi-list-devices ftd3xx-static)
