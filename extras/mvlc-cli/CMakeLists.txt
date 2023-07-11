add_executable(mvlc-cli mvlc-cli.cc)
target_link_libraries(mvlc-cli PRIVATE mesytec-mvlc argh)
install(TARGETS mvlc-cli RUNTIME DESTINATION bin)
