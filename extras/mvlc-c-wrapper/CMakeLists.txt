add_library(mesytec-mvlc-c SHARED mvlc_c.cc)

target_link_libraries(mesytec-mvlc-c
    PRIVATE mesytec-mvlc
    PRIVATE spdlog::spdlog
    )

install(TARGETS mesytec-mvlc-c
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)
