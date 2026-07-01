cmake_minimum_required(VERSION 3.12)
project(mesytec-mvlc-cmake-example)

find_package(mesytec-mvlc REQUIRED)

add_executable(mvlc-connect-usb mvlc-connect-usb.cc)
target_link_libraries(mvlc-connect-usb PRIVATE mesytec-mvlc::mesytec-mvlc)

add_executable(mvlc-vme-reads mvlc-vme-reads.cc)
target_link_libraries(mvlc-vme-reads PRIVATE mesytec-mvlc::mesytec-mvlc)
