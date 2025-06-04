#include <argh.h>
#include <boost/format.hpp>
#include <boost/histogram.hpp>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mesytec_vme_modules.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/udp_sockets.h>

using namespace mesytec::mvlc;

std::error_code write_to_socket(int sock, const std::vector<u32> &data, size_t &bytesTransferred)
{
    return eth::write_to_socket(sock, reinterpret_cast<const u8 *>(data.data()),
                                data.size() * sizeof(u32), bytesTransferred);
}

std::error_code read_packet(int sock, std::vector<u32> &dest)
{
    dest.resize(eth::JumboFrameMaxSize / sizeof(u32));

    size_t bytesTransferred = 0u;

    if (auto ec = eth::receive_one_packet(sock, reinterpret_cast<u8 *>(dest.data()),
                                          dest.size() * sizeof(u32), bytesTransferred, 0, nullptr))
    {
        spdlog::error("Error reading packet from socket: {}", ec.message());
        return ec;
    }

    dest.resize(bytesTransferred / sizeof(u32));
    return {};
}

// Reads the hardware ID register from the MVLC repeatedly. Allows to specify
// the number of register reads per outgoing packet. This in turn affects the
// response size: 1 read -> 2 more response words. 0 reads also works, in which case
// only a reference word is transmitted and mirrored back by the MVLC.
int do_read_registers_test(int sock, const std::vector<std::string> &args)
{
    u16 refWord = 1;
    std::vector<u32> response;
    size_t registersToRead = 1;
    s32 lastPacketNumber = -1;

    if (args.size() > 1)
        registersToRead = std::stoul(args[1]);

    spdlog::info("Reading {} registers", registersToRead);

    while (true)
    {
        SuperCommandBuilder cmdList;
        cmdList.addReferenceWord(refWord++);
        for (size_t i = 0; i < registersToRead; ++i)
        {
            cmdList.addReadLocal(registers::hardware_id);
        }

        size_t bytesTransferred = 0u;

        if (auto ec = write_to_socket(sock, make_command_buffer(cmdList), bytesTransferred))
        {
            spdlog::error("Error writing to socket: {}", ec.message());
            return 1;
        }

        response.clear();
        if (auto ec = read_packet(sock, response))
        {
            spdlog::error("Error reading packet: {}", ec.message());
            return 1;
        }

        spdlog::info("Received {} bytes, {} words: {:#010x}", response.size() * sizeof(u32),
                     response.size(), fmt::join(response, ", "));

        if (response.empty())
        {
            spdlog::error("No response received from the MVLC.");
            return 1;
        }

        // 2 eth headers, 1 0xF1 frame header, 1 ref word, N reads and results
        size_t expectedResponseSize = 2 + 1 + 1 + registersToRead * 2;

        if (response.size() != expectedResponseSize)
        {
            spdlog::error("Unexpected response size: expected {}, got {}", expectedResponseSize,
                          response.size());
            return 1;
        }

        auto responseIter = response.begin();

        auto ethHeader0 = *responseIter++;
        auto ethHeader1 = *responseIter++;

        eth::PayloadHeaderInfo headerInfo{ethHeader0, ethHeader1};
        std::cout << fmt::format("header0 = 0x{:08x}, header1 = 0x{:08x}\n", ethHeader0,
                                 ethHeader1);
        std::cout << fmt::format(
            "header0: packetChannel={}, packetNumber={}, controllerId={}, dataWordCount={}\n",
            headerInfo.packetChannel(), headerInfo.packetNumber(), headerInfo.controllerId(),
            headerInfo.dataWordCount());
        std::cout << fmt::format(
            "header1: udpTimestamp={}, nextHeaderPointer=0x{:04x}, isHeaderPointerPresent={}\n",
            headerInfo.udpTimestamp(), headerInfo.nextHeaderPointer(),
            headerInfo.isNextHeaderPointerPresent());

        if (lastPacketNumber < 0)
        {
            lastPacketNumber = headerInfo.packetNumber();
        }
        else
        {
            if (s32 packetLoss = eth::calc_packet_loss(lastPacketNumber, headerInfo.packetNumber());
                packetLoss > 0)
            {
                spdlog::warn("Packet loss detected: {} packets lost between {} and {}", packetLoss,
                             lastPacketNumber, headerInfo.packetNumber());
            }
            lastPacketNumber = headerInfo.packetNumber();
        }

        auto dataWordCount = response.size() - 2; // 2 eth headers

        if (dataWordCount != headerInfo.dataWordCount())
        {
            spdlog::error("Data word count mismatch (ethHeaders): expected {}, got {}",
                          headerInfo.dataWordCount(), dataWordCount);
            return 1;
        }

        if (!headerInfo.isNextHeaderPointerPresent())
        {
            spdlog::error("Next header pointer is not present, expected it to be present.");
            return 1;
        }

        if (headerInfo.nextHeaderPointer() != 0)
        {
            spdlog::error("Next header pointer is not zero, expected it to be zero.");
            return 1;
        }

#if 0




        auto responseRef = response[0];

        if (responseRef != refWord - 1)
        {
            spdlog::error("Reference word mismatch: expected {}, got {}", refWord - 1, responseRef);
            return 1;
        }

        for (size_t i = 1; i < response.size(); ++i)
        {
            u32 registerValue = response[i];
            if (registerValue != vme_modules::HardwareIds::MVLC)
            {
                spdlog::error("Unexpected register value at index {}: expected {}, got {}",
                                 i - 1, vme_modules::HardwareIds::MVLC, registerValue);
            }
        }
#endif
    }
}

using TestFunction = int (*)(int sock, const std::vector<std::string> &args);

static std::map<std::string, TestFunction> tests;
static std::vector<std::string> testNames;

void add_test(const std::string &name, TestFunction func)
{
    tests[name] = func;
    testNames.push_back(name);
}

int main(int argc, char *argv[])
{
    add_test("read_registers", do_read_registers_test);

    argh::parser parser;
    parser.parse(argv);

    if (parser.pos_args().size() < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <mvlc hostname/ip> <testname> [<args>]\n";
        std::cerr << fmt::format("Available tests: {}\n", fmt::join(testNames, ", "));
        return 1;
    }

    auto pos_args = parser.pos_args();
    auto hostname = pos_args[1];
    auto testname = pos_args[2];

    if (tests.find(testname) == tests.end())
    {
        std::cerr << fmt::format("Unknown test '{}'. Available tests: {}\n", testname,
                                 fmt::join(testNames, ", "));
        return 1;
    }

    std::error_code ec;
    auto sock = eth::connect_udp_socket(hostname, eth::CommandPort, &ec);

    if (sock < 0)
    {
        spdlog::error("Error connecting to {}:{}: {}", hostname, eth::CommandPort, ec.message());
        return 1;
    }

    std::vector<std::string> cmdArgs(pos_args.begin() + 2, pos_args.end());
    return tests[testname](sock, cmdArgs);
}
