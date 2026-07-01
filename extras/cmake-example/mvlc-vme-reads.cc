#include <iostream>
#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec;

int main(int argc, char *argv[])
{
    mvlc::set_global_log_level(spdlog::level::warn); // debug or trace for more verbose output

    const auto vmeDataWidth = mvlc::VMEDataWidth::D16;

    auto mvlcAddr = "usb://";
    std::uint32_t targetAddress = 0xF8000000;
    std::uint32_t registerStart = 0x14;
    std::uint32_t readCount = 16;
    std::uint8_t vmeAmod = 0x0e;
    std::uint32_t addressIncrement = 2;

    for (int i = 0; i < argc; ++i)
    {
        if (argv[i] == std::string("--help") || argv[i] == std::string("-h"))
        {
            // clang-format off
            std::cout << "This program performs VME read operations on the target VME device.\n";
            std::cout << "Read addresses are incremented by <address-increment> bytes for each read, starting from target-base-address + start-offset.\n\n";
            std::cout << "Usage: " << argv[0] << " [mvlc-url] [target-base-address] [start-offset] [read-count] [read-vme-amod] [address-increment\n";
            std::cout << "  mvlc-url: optional MVLC URL, e.g. <hostname>, eth://<hostname>, usb://, "
                         "usb://@<index>, usb://<serial>\n";
            std::cout << "  target-base-address: optional base address, e.g. 0xF8000000\n";
            std::cout << "  start-offset: optional start offset, e.g. 0x14\n";
            std::cout << "  read-count: optional number of reads, e.g. 16\n";
            std::cout << "  read-vme-amod: optional non-block VME address modifier, e.g. 0x0e\n";
            std::cout << "  address-increment: optional address increment, e.g. 2\n";
            // clang-format on
            return 0;
        }
    }

    if (argc > 1)
    {
        mvlcAddr = argv[1];
    }

    if (argc > 2)
    {
        targetAddress = std::stoul(argv[2], nullptr, 0);
    }

    if (argc > 3)
    {
        registerStart = std::stoul(argv[3], nullptr, 0);
    }

    if (argc > 4)
    {
        readCount = std::stoul(argv[4], nullptr, 0);
    }

    if (argc > 5)
    {
        vmeAmod = std::stoul(argv[5], nullptr, 0);
    }

    if (argc > 6)
    {
        addressIncrement = std::stoul(argv[6], nullptr, 0);
    }

    if (mvlc::vme_amods::is_block_mode(vmeAmod))
    {
        std::cerr << "Error: expected non-block vme amod value.\n";
        return 1;
    }

    fmt::println(
        "Connecting to '{}'. VME target address: 0x{:08x}, start offset: 0x{:08x}, read count: {}, VME amod: 0x{:02x}, address increment: {}",
        mvlcAddr, targetAddress, registerStart, readCount, vmeAmod, addressIncrement);

    auto mvlc = mvlc::make_mvlc(mvlcAddr);

    if (auto ec = mvlc.connect())
    {
        fmt::println("Could not connect to mvlc: {}", ec.message());
        return 1;
    }

    for (unsigned i = 0; i < readCount; ++i)
    {
        std::uint32_t targetOffset = registerStart + i * addressIncrement;
        std::uint32_t vmeAddress = targetAddress + targetOffset;
        std::uint32_t readValue = 0;

        if (auto ec = mvlc.vmeRead(vmeAddress, readValue, vmeAmod, vmeDataWidth))
        {
            fmt::println("Error reading from target address 0x{:08x}: {} (code={})", vmeAddress,
                         ec.message(), ec.value());
            return 1;
        }
        else
        {
            fmt::println("Read from 0x{:08x} = 0x{:08x} ({} decimal)", vmeAddress, readValue, readValue);
        }
    }

    return 0;
}
