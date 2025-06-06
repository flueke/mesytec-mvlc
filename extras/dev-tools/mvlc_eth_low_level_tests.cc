#include <argh.h>
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

// Uses information from the two eth header words to check the packet consistency.
// Packet loss is not handled in here, only packet size, header pointer etc are checked.
bool check_packet_consistency(std::basic_string_view<u32> packet,
                              eth::PacketChannel expectedChannel = eth::PacketChannel::Command,
                              u16 expectedControllerId = 0)
{
    if (packet.size() < 2)
    {
        spdlog::error("Packet too small: expected at least 2 words, got {}", packet.size());
        return false;
    }

    eth::PayloadHeaderInfo headerInfo{packet[0], packet[1]};

    if (headerInfo.packetChannel() != static_cast<u16>(expectedChannel))
    {
        spdlog::error("Unexpected packet channel: expected {}, got {}",
                      static_cast<u16>(expectedChannel), headerInfo.packetChannel());
        return false;
    }

    if (headerInfo.controllerId() != expectedControllerId)
    {
        spdlog::error("Unexpected controller ID: expected {}, got {}", expectedControllerId,
                      headerInfo.controllerId());
        return false;
    }

    if (headerInfo.dataWordCount() != packet.size() - 2)
    {
        spdlog::error("Data word count mismatch: expected {}, got {}", packet.size() - 2,
                      headerInfo.dataWordCount());
        return false;
    }

    if (headerInfo.isNextHeaderPointerPresent())
    {
        if (!headerInfo.isNextHeaderPointerValid())
        {
            spdlog::error("Invalid next header pointer: {}, dataWordCount={}",
                          headerInfo.nextHeaderPointer(), headerInfo.dataWordCount());
            return false;
        }

        u32 header = packet[headerInfo.nextHeaderPointer() + 2];

        if (!is_known_frame_header(header))
        {
            spdlog::error("Next header pointer points to an unknown frame header: {:#010x}",
                          header);
            return false;
        }
    }

    return true;
}

// Fills the outgoing packet with 1 to N reference words and checks the responses.
int do_send_ref_words_test(int sock, const std::vector<std::string> &args)
{
    u16 refWord = 1;
    std::vector<u32> response;
    s32 lastPacketNumber = -1;
    size_t refWordsToSend = 1;
    size_t transactionCount = 0;
    const auto ReportInterval = std::chrono::milliseconds(500);
    size_t transactionsInInterval = 0;

    if (args.size() > 1)
        refWordsToSend = std::stoul(args[1]);

    // 2 eth headers, 1 0xF100 frame header, N ref words
    const size_t expectedResponseSize = 2 + 1 + refWordsToSend;

    spdlog::info("Sending {} reference words per packet", refWordsToSend);

    if (expectedResponseSize > 256)
    {
        spdlog::error("Expected response size {} exceeds maximum of 256 words. MVLC would truncate.", expectedResponseSize);
        return 1;
    }

    util::Stopwatch swReport;

    while (true)
    {
        SuperCommandBuilder cmdList;
        u16 firstRefWord = refWord; // the first ref word sent to the mvlc

        for (size_t i = 0; i < refWordsToSend; ++i)
            cmdList.addReferenceWord(refWord++);

        size_t bytesTransferred = 0u;

        if (auto ec = write_to_socket(sock, make_command_buffer(cmdList), bytesTransferred))
        {
            spdlog::error("Error writing to socket: {}", ec.message());
            return 1;
        }

        spdlog::debug("Sent {} bytes, {} words", bytesTransferred, bytesTransferred / sizeof(u32));

        response.clear();
        if (auto ec = read_packet(sock, response))
        {
            spdlog::error("Error reading packet: {}", ec.message());
            return 1;
        }

        if (response.empty())
        {
            spdlog::error("No response received from the MVLC.");
            return 1;
        }

        spdlog::debug("Received {} bytes, {} words: {:#010x}", response.size() * sizeof(u32),
                     response.size(), fmt::join(response, ", "));

        if (response.size() != expectedResponseSize)
        {
            spdlog::error("Unexpected response size: expected {}, got {}", expectedResponseSize,
                          response.size());
            return 1;
        }

        auto ethHeader0 = response[0];
        auto ethHeader1 = response[1];

        eth::PayloadHeaderInfo headerInfo{ethHeader0, ethHeader1};
        spdlog::debug("{}", eth::eth_headers_to_string(headerInfo));

        if (!check_packet_consistency(std::basic_string_view<u32>(response.data(), response.size()),
                                      eth::PacketChannel::Command, 0))
        {
            // logging is done in check_packet_consistency()
            return 1;
        }

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

        // Check payload contents. This code is not ETH specific anymore but works for USB too.
        // 1 0xF100 frame headers, N reference words
        std::basic_string_view<u32> payloadView(response.data() + 2, response.size() - 2);

        if (auto ct = get_super_command_type(payloadView[0]);
            ct != super_commands::SuperCommandType::CmdBufferStart)
        {
            spdlog::error("Payload does not start with CmdBufferStart (0xF100) command: {:#010x}",
                          payloadView[0]);
            return 1;
        }

        for (size_t i=0; i<refWordsToSend; ++i)
        {
            if (auto ct = get_super_command_type(payloadView[i + 1]);
                ct != super_commands::SuperCommandType::ReferenceWord)
            {
                spdlog::error(
                    "Expected reference word command (0x0101) at payload[{}], found {:#010x} instead",
                    i + 1, payloadView[i + 1]);
                return 1;
            }

            u16 expectedRefWord = (firstRefWord + i);

            if (auto refResponse = get_super_command_arg(payloadView[i + 1]);
                refResponse != expectedRefWord)
            {
                spdlog::error("Unexpected reference word in response: expected {:#06x}, got {:#06x}",
                              expectedRefWord, refResponse.value_or(0));
                return 1;
            }
        }

        ++transactionCount;
        ++transactionsInInterval;

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval)
        {
            auto totalElapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed()).count();
            auto totaltxPerSecond = transactionCount / totalElapsedSeconds;
            spdlog::info(
                "Elapsed: {:.3f} s, Transactions: {}, {:.2f} tx/s",
                totalElapsedSeconds,
                transactionCount, totaltxPerSecond);
            swReport.interval();
        }
    }
}

// Reads the hardware ID register from the MVLC repeatedly. Allows to specify
// the number of register reads per outgoing packet. This in turn affects the
// response size: 1 read -> 2 more response words. 0 reads also works, in which case
// only a reference word is transmitted and mirrored back by the MVLC.
int do_read_registers_test(int sock, const std::vector<std::string> &args)
{
    u16 refWord = 1;
    std::vector<u32> response;
    s32 lastPacketNumber = -1;
    size_t registersToRead = 1;
    size_t transactionCount = 0;
    const auto ReportInterval = std::chrono::milliseconds(500);
    size_t transactionsInInterval = 0;

    if (args.size() > 1)
        registersToRead = std::stoul(args[1]);

    spdlog::info("Reading {} registers per transaction", registersToRead);

    // 2 eth headers, 1 0xF100 frame header, 1 ref word, N reads and results
    size_t expectedResponseSize = 2 + 1 + 1 + registersToRead * 2;

    if (expectedResponseSize > 256)
    {
        spdlog::error("Expected response size {} exceeds maximum of 256 words. MVLC would truncate.", expectedResponseSize);
        return 1;
    }

    util::Stopwatch swReport;

    while (true)
    {
        SuperCommandBuilder cmdList;
        cmdList.addReferenceWord(refWord);
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

        spdlog::debug("Sent {} bytes, {} words", bytesTransferred, bytesTransferred / sizeof(u32));

        response.clear();
        if (auto ec = read_packet(sock, response))
        {
            spdlog::error("Error reading packet: {}", ec.message());
            return 1;
        }

        if (response.empty())
        {
            spdlog::error("No response received from the MVLC.");
            return 1;
        }

        spdlog::debug("Received {} bytes, {} words: {:#010x}", response.size() * sizeof(u32),
                     response.size(), fmt::join(response, ", "));

        if (response.size() != expectedResponseSize)
        {
            spdlog::error("Unexpected response size: expected {}, got {}", expectedResponseSize,
                          response.size());
            return 1;
        }

        auto ethHeader0 = response[0];
        auto ethHeader1 = response[1];

        eth::PayloadHeaderInfo headerInfo{ethHeader0, ethHeader1};
        spdlog::debug("{}", eth::eth_headers_to_string(headerInfo));

        if (!check_packet_consistency(std::basic_string_view<u32>(response.data(), response.size()),
                                      eth::PacketChannel::Command, 0))
        {
            // logging is done in check_packet_consistency()
            return 1;
        }

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

        // Check payload contents. This code is not ETH specific anymore but works for USB too.

        // 1 0xF100 frame header, 1 ref word, N*(read and result)
        std::basic_string_view<u32> payloadView(response.data() + 2, response.size() - 2);

        //spdlog::debug("Payload: {:#010x}", fmt::join(payloadView, ", "));

        if (auto ct = get_super_command_type(payloadView[0]);
            ct != super_commands::SuperCommandType::CmdBufferStart)
        {
            spdlog::error("Payload does not start with CmdBufferStart (0xF100) command: {:#010x}",
                          payloadView[0]);
            return 1;
        }

        if (auto ct = get_super_command_type(payloadView[1]);
            ct != super_commands::SuperCommandType::ReferenceWord)
        {
            spdlog::error(
                "Expected reference word command (0x0101) at payload[1], found {:#010x} instead",
                payloadView[1]);
            return 1;
        }

        if (auto refResponse = get_super_command_arg(payloadView[1]);
            refResponse != refWord)
        {
            spdlog::error("Unexpected reference word in response: expected {:#06x}, got {:#06x}",
                          refWord, refResponse.value_or(0));
            return 1;
        }

        // TODO: verify the rest of the response. all register reads must be present and the read results must match the mvlc hardware ID.

        ++refWord;
        ++transactionCount;
        ++transactionsInInterval;

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval)
        {
            auto totalElapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed()).count();
            auto totaltxPerSecond = transactionCount / totalElapsedSeconds;
            spdlog::info(
                "Elapsed: {:.3f} s, Transactions: {}, {:.2f} tx/s",
                totalElapsedSeconds,
                transactionCount, totaltxPerSecond);
            swReport.interval();
        }
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
    add_test("send_ref_words", do_send_ref_words_test);

    argh::parser parser;
    parser.parse(argv);

    if (parser["--trace"])
    {
        spdlog::set_level(spdlog::level::trace);
    }
    else if (parser["--debug"])
    {
        spdlog::set_level(spdlog::level::debug);
    }
    else
    {
        spdlog::set_level(spdlog::level::info);
    }

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
