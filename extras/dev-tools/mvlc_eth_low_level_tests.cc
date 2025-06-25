#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mesytec_vme_modules.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/udp_sockets.h>

// Command line utility for debugging the MVLC ethernet interface.
// This uses low level socket functions instead of the more complex high-level
// MVLC implementation.
// Response packets are checked for consistency (lengths, header pointer).
// Read timeouts are considered fatal, the MVLC should respond within 1s.

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

inline bool is_socket_timeout(const std::error_code &ec)
{
    return ec == std::error_code(EAGAIN, std::system_category()) ||
           ec == std::error_code(EWOULDBLOCK, std::system_category());
}

// Uses information from the two eth header words to check the packet consistency.
// Packet loss is not handled in here, only packet size, header pointer etc are checked.
bool check_packet_consistency(std::basic_string_view<u32> packet,
                              std::optional<eth::PacketChannel> expectedChannel = {},
                              u16 expectedControllerId = 0)
{
    if (packet.size() < 2)
    {
        spdlog::error("Packet too small: expected at least 2 words, got {}", packet.size());
        return false;
    }

    eth::PayloadHeaderInfo headerInfo{packet[0], packet[1]};

    if (expectedChannel.has_value())

        if (headerInfo.packetChannel() != static_cast<u16>(expectedChannel.value()))
        {
            spdlog::error("Unexpected packet channel: expected {}, got {}",
                        static_cast<u16>(expectedChannel.value()), headerInfo.packetChannel());
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

struct Context;
struct Command;

#define DEF_TEST_FUNC(name) int name(Context &ctx, const Command &self)
using TestFunction = std::function<DEF_TEST_FUNC()>;

struct Command
{
    std::string name;
    TestFunction exec;
};

struct Context
{
    argh::parser parser;
    std::vector<std::string> args;  // prepared cli pos args for the command.
    int cmdSock = -1;               // MVLC command socket
    std::string hostname;           // mvlc hostname or ip address
};

static const auto ReportInterval = std::chrono::milliseconds(500);

// Fills the outgoing packet with 1 to N reference words and checks the response.
DEF_TEST_FUNC(do_send_ref_words_test)
{
    auto args = ctx.args;
    auto sock = ctx.cmdSock;

    u16 refWord = 1;
    std::vector<u32> response;
    s32 lastPacketNumber = -1;
    size_t refWordsToSend = 1;
    size_t transactionCount = 0;
    size_t transactionsInInterval = 0;

    if (args.size() > 1)
        refWordsToSend = std::stoul(args[1]);

    // 2 eth headers, 1 0xF100 frame header, N ref words
    const size_t expectedResponseSize = 2 + 1 + refWordsToSend;

    spdlog::info("Sending {} reference words per packet", refWordsToSend);

    if (expectedResponseSize > 256)
    {
        spdlog::error(
            "Expected response size {} exceeds maximum of 256 words. MVLC would truncate.",
            expectedResponseSize);
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

        for (size_t i = 0; i < refWordsToSend; ++i)
        {
            if (auto ct = get_super_command_type(payloadView[i + 1]);
                ct != super_commands::SuperCommandType::ReferenceWord)
            {
                spdlog::error("Expected reference word command (0x0101) at payload[{}], found "
                              "{:#010x} instead",
                              i + 1, payloadView[i + 1]);
                return 1;
            }

            u16 expectedRefWord = (firstRefWord + i);

            if (auto refResponse = get_super_command_arg(payloadView[i + 1]);
                refResponse != expectedRefWord)
            {
                spdlog::error(
                    "Unexpected reference word in response: expected {:#06x}, got {:#06x}",
                    expectedRefWord, refResponse.value_or(0));
                return 1;
            }
        }

        ++transactionCount;
        ++transactionsInInterval;

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval)
        {
            auto totalElapsedSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed())
                    .count();
            auto totaltxPerSecond = transactionCount / totalElapsedSeconds;
            spdlog::info("Elapsed: {:.3f} s, Transactions: {}, {:.2f} tx/s", totalElapsedSeconds,
                         transactionCount, totaltxPerSecond);
            swReport.interval();
        }
    }
}

static const Command SendRefWordsCommand{
    .name = "send_ref_words",
    .exec = do_send_ref_words_test,
};

// Reads the hardware ID register from the MVLC repeatedly. Allows to specify
// the number of register reads per outgoing packet. This in turn affects the
// response size: 1 read -> 2 more response words. 0 reads also works, in which case
// only a reference word is transmitted and mirrored back by the MVLC.
DEF_TEST_FUNC(do_read_registers_test)
{
    auto args = ctx.args;
    auto sock = ctx.cmdSock;

    u16 refWord = 1;
    std::vector<u32> response;
    s32 lastPacketNumber = -1;
    size_t registersToRead = 1;
    size_t transactionCount = 0;
    size_t registersRead = 0;
    size_t transactionsInInterval = 0;

    if (args.size() > 1)
        registersToRead = std::stoul(args[1]);

    spdlog::info("Reading {} registers per transaction", registersToRead);

    // 2 eth headers, 1 0xF100 frame header, 1 ref word, N reads and results
    size_t expectedResponseSize = 2 + 1 + 1 + registersToRead * 2;

    if (expectedResponseSize > 256)
    {
        spdlog::error(
            "Expected response size {} exceeds maximum of 256 words. MVLC would truncate.",
            expectedResponseSize);
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
            // logging is done in check_packet_consistency() so just return here.
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
                // Not considered fatal: another client might have sent a
                // command in-between which will look like loss to us.
                spdlog::warn("Packet loss detected: {} packets lost between {} and {}", packetLoss,
                             lastPacketNumber, headerInfo.packetNumber());
            }
            lastPacketNumber = headerInfo.packetNumber();
        }

        // Check payload contents. This code is not ETH specific anymore but works for USB too.
        // Example response for two register reads:
        // 0xf1000005, 0x01010001, 0x01026008, 0x00005008, 0x01026008, 0x00005008
        // header      ref         read        contents    read        contents

        // 1 0xF100 frame header, 1 ref word, N*(read and result)
        std::basic_string_view<u32> payloadView(response.data() + 2, response.size() - 2);

        spdlog::trace("Response payload: {:#010x}", fmt::join(payloadView, ", "));

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

        if (auto refResponse = get_super_command_arg(payloadView[1]); refResponse != refWord)
        {
            spdlog::error("Unexpected reference word in response: expected {:#06x}, got {:#06x}",
                          refWord, refResponse.value_or(0));
            return 1;
        }

        // verify the rest of the response. all register reads must be present
        // and the read results must match the mvlc hardware ID.
        payloadView.remove_prefix(2); // remove the first two words (header and ref word)

        if (payloadView.size() != registersToRead * 2)
        {
            spdlog::error("Unexpected payload size: expected {} words, got {}", registersToRead * 2,
                          payloadView.size());
            return 1;
        }

        for (size_t i = 0; i < payloadView.size(); i += 2)
        {
            auto &cmd = payloadView[i];
            auto &result = payloadView[i + 1];

            if (auto ct = get_super_command_type(cmd);
                ct != super_commands::SuperCommandType::ReadLocal)
            {
                spdlog::error(
                    "Expected ReadLocal command (0x0102) at payload[{}], found {:#010x} instead", i,
                    cmd);
                return 1;
            }

            if (auto arg = get_super_command_arg(cmd); arg != registers::hardware_id)
            {
                spdlog::error("Expected ReadLocal command for hardware ID (0x{:04x}) at "
                              "payload[{}], got 0x{:04x} instead",
                              registers::hardware_id, i, arg.value_or(0));
                return 1;
            }

            if (result != vme_modules::HardwareIds::MVLC)
            {
                spdlog::error(
                    "Unexpected read result at payload[{}]: expected 0x{:08x}, got 0x{:08x}", i + 1,
                    vme_modules::HardwareIds::MVLC, result);
                return 1;
            }
        }

        ++refWord;
        ++transactionCount;
        ++transactionsInInterval;
        registersRead += registersToRead;

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval)
        {
            auto totalElapsedSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed())
                    .count();
            auto totaltxPerSecond = transactionCount / totalElapsedSeconds;
            auto totalRegistersPerSecond = registersRead / totalElapsedSeconds;
            spdlog::info("Elapsed: {:.3f} s, Transactions: {}, {:.2f} tx/s, {:.2f} registers/s",
                         totalElapsedSeconds, transactionCount, totaltxPerSecond,
                         totalRegistersPerSecond);
            swReport.interval();
        }
    }
}

static const Command ReadRegistersCommand{
    .name = "read_registers",
    .exec = do_read_registers_test,
};

DEF_TEST_FUNC(do_write_registers_test)
{
    auto args = ctx.args;
    auto sock = ctx.cmdSock;

    // write to somewhere in the middle of the stack mem.
    const u32 testRegister = stacks::StackMemoryBegin + 1024;
    u32 writePayload = 0x12345678;

    u16 refWord = 1;
    std::vector<u32> response;
    s32 lastPacketNumber = -1;
    size_t registersToWrite = 1;
    size_t transactionCount = 0;
    size_t registersWritten = 0;
    size_t transactionsInInterval = 0;

    if (args.size() > 1)
        registersToWrite = std::stoul(args[1]);

    spdlog::info("Writing {} registers per transaction", registersToWrite);

    // 2 eth headers, 1 0xF100 frame header, 1 ref word, N reads and results
    size_t expectedResponseSize = 2 + 1 + 1 + registersToWrite * 2;

    if (expectedResponseSize > 256)
    {
        spdlog::error(
            "Expected response size {} exceeds maximum of 256 words. MVLC would truncate.",
            expectedResponseSize);
        return 1;
    }

    util::Stopwatch swReport;

    while (true)
    {
        SuperCommandBuilder cmdList;
        cmdList.addReferenceWord(refWord);
        for (size_t i = 0; i < registersToWrite; ++i)
        {
            cmdList.addWriteLocal(testRegister, writePayload);
        }

        size_t bytesTransferred = 0u;

        if (auto ec = write_to_socket(sock, make_command_buffer(cmdList), bytesTransferred))
        {
            spdlog::error("Error writing to socket: {}", ec.message());
            return 1;
        }

        spdlog::debug("Sent {} bytes, {} words. writePayload={:#010x}", bytesTransferred, bytesTransferred / sizeof(u32), writePayload);

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
            // logging is done in check_packet_consistency() so just return here.
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
                // Not considered fatal: another client might have sent a
                // command in-between which will look like loss to us.
                spdlog::warn("Packet loss detected: {} packets lost between {} and {}", packetLoss,
                             lastPacketNumber, headerInfo.packetNumber());
            }
            lastPacketNumber = headerInfo.packetNumber();
        }

        // Check payload contents. This code is not ETH specific anymore but works for USB too.
        // Example response for two register reads:
        // 0xf1000005, 0x01010001, 0x01026008, 0x00005008, 0x01026008, 0x00005008
        // header      ref         read        contents    read        contents

        // 1 0xF100 frame header, 1 ref word, N*(read and result)
        std::basic_string_view<u32> payloadView(response.data() + 2, response.size() - 2);

        spdlog::trace("Response payload: {:#010x}", fmt::join(payloadView, ", "));

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

        if (auto refResponse = get_super_command_arg(payloadView[1]); refResponse != refWord)
        {
            spdlog::error("Unexpected reference word in response: expected {:#06x}, got {:#06x}",
                          refWord, refResponse.value_or(0));
            return 1;
        }

        // verify the rest of the response. all register reads must be present
        // and the read results must match the mvlc hardware ID.
        payloadView.remove_prefix(2); // remove the first two words (header and ref word)

        if (payloadView.size() != registersToWrite * 2)
        {
            spdlog::error("Unexpected payload size: expected {} words, got {}", registersToWrite * 2,
                          payloadView.size());
            return 1;
        }

        for (size_t i = 0; i < payloadView.size(); i += 2)
        {
            auto &cmd = payloadView[i];
            auto &result = payloadView[i + 1];

            if (auto ct = get_super_command_type(cmd);
                ct != super_commands::SuperCommandType::WriteLocal)
            {
                spdlog::error(
                    "Expected WriteLocal command (0x0204) at payload[{}], found {:#010x} instead", i,
                    cmd);
                return 1;
            }

            if (auto arg = get_super_command_arg(cmd); arg != testRegister)
            {
                spdlog::error("Expected WriteLocal command at "
                              "payload[{}], got 0x{:04x} instead",
                              registers::hardware_id, i, arg.value_or(0));
                return 1;
            }

            if (result != writePayload)
            {
                spdlog::error(
                    "Unexpected read result at payload[{}]: expected 0x{:08x}, got 0x{:08x}", i + 1,
                    writePayload, result);
                return 1;
            }
        }

        ++refWord;
        ++transactionCount;
        ++transactionsInInterval;
        registersWritten += registersToWrite;

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval)
        {
            auto totalElapsedSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed())
                    .count();
            auto totaltxPerSecond = transactionCount / totalElapsedSeconds;
            auto totalRegistersPerSecond = registersWritten / totalElapsedSeconds;
            spdlog::info("Elapsed: {:.3f} s, Transactions: {}, {:.2f} tx/s, {:.2f} registers/s",
                         totalElapsedSeconds, transactionCount, totaltxPerSecond,
                         totalRegistersPerSecond);
            swReport.interval();
        }

        ++writePayload;
    }
}

static const Command WriteRegistersCommand{
    .name = "write_registers",
    .exec = do_write_registers_test,
};

// Send eth throttle commands to the MVLC. This is send-only, no responses are read.
// The default throttle values is 0 which means unlimited.
// TODO: add ability to set different throttle values over time.
DEF_TEST_FUNC(send_eth_throttle)
{
    u16 delayValue = 0;

    if (ctx.args.size() > 1)
        delayValue = std::stoul(ctx.args[1], nullptr, 0);

    auto delaySock = eth::connect_udp_socket(ctx.hostname, eth::DelayPort);

    if (delaySock < 0)
    {
        spdlog::error("Error connecting to {}:{}", ctx.hostname, eth::DelayPort);
        return 1;
    }

    spdlog::info("Sending eth throttle command with delay={} cycles every 100 ms", delayValue);

    util::Stopwatch swReport;
    size_t delaysSent = 0;
    while (true)
    {
        if (auto ec = eth::send_delay_command(delaySock, delayValue))
        {
            spdlog::error("Error sending eth throttle command: {}", ec.message());
            return 1;
        }

        ++delaysSent;

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval)
        {
            auto totalElapsedSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed())
                    .count();
            spdlog::info("Elapsed: {:.3f} s, delay commands sent: {}, {:.2f} tx/s", totalElapsedSeconds,
                        delaysSent, delaysSent / totalElapsedSeconds);
            swReport.interval();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
};

static const Command SendEthThrottleCommand{
    .name = "send_eth_throttle",
    .exec = send_eth_throttle,
};

DEF_TEST_FUNC(read_write_vme_test)
{
    auto sock = ctx.cmdSock;

    // Write to the middle of the stack memory. This won't collide with the command stack itself.
    static const u32 testAddress = 0xffff0000 + stacks::StackMemoryBegin + 1024;
    u16 superRef = 0xdead;
    u32 stackRef = 0x1337cafe;
    u32 writePayload = 0x12345678;
    std::array<s32, 3> lastPacketNumbers = {-1, -1, -1}; // per PacketChannel sequential packet numbers
    size_t transactionCount = 0;
    size_t transactionsInInterval = 0;


    StackCommandBuilder cmdList;
    SuperCommandBuilder superCmdList;
    std::vector<u32> response;

    while (true)
    {
        spdlog::info("Transaction cycle {}, testAddress={:#010x}, writePayload={:#010x}, superRef={:#06x}, stackRef={:#010x} ===============",
             transactionCount, testAddress, writePayload, superRef, stackRef);
        cmdList.clear();
        cmdList.addWriteMarker(stackRef++);
        cmdList.addVMEWrite(testAddress, writePayload, vme_amods::A32, VMEDataWidth::D32);

        superCmdList.clear();
        superCmdList.addReferenceWord(superRef);

        // to increase the size of the command buffer and thus the mirror response size.
        //for (size_t i=0; i<200; ++i)
        //    superCmdList.addReferenceWord(superRef);

        ++superRef;
        superCmdList.addStackUpload(cmdList, static_cast<u8>(Pipe::Command), stacks::ImmediateStackStartOffsetBytes);
        // Write the stack offset and trigger registers. The latter triggers the
        // immediate execution of the stack.
        superCmdList.addWriteLocal(stacks::Stack0OffsetRegister, stacks::ImmediateStackStartOffsetBytes);
        superCmdList.addWriteLocal(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);
        // New: directly read both stack status registers
        superCmdList.addReadLocal(registers::stack_exec_status0);
        superCmdList.addReadLocal(registers::stack_exec_status1);
        auto cmdBuffer = make_command_buffer(superCmdList);
        spdlog::trace("cmdBuffer=\n{:#010x}", fmt::join(cmdBuffer, "\n"));

        size_t bytesTransferred = 0u;

        if (auto ec = write_to_socket(sock, cmdBuffer, bytesTransferred))
        {
            spdlog::error("Error writing to socket: {}", ec.message());
            return 1;
        }

        spdlog::debug("Sent {} bytes, {} words", bytesTransferred, bytesTransferred / sizeof(u32));

        // Try1: read to timeout
        std::error_code ec;

        do
        {
            response.clear();
            ec = read_packet(sock, response);

            if (!ec)
            {
                spdlog::info("Received response of size {}:\n{:#010x}", response.size(), fmt::join(response, "\n"));
                spdlog::debug("lastPacketNumbers={}", fmt::join(lastPacketNumbers, ", "));

                if (response.size() >= 2)
                {
                    auto ethHeader0 = response[0];
                    auto ethHeader1 = response[1];

                    eth::PayloadHeaderInfo headerInfo{ethHeader0, ethHeader1};
                    spdlog::debug("eth headers: {}", eth::eth_headers_to_string(headerInfo));

                    if (!check_packet_consistency(std::basic_string_view<u32>(response.data(), response.size())))
                    {
                        // logging is done in check_packet_consistency() so just return here.
                        return 1;
                    }

                    auto packetChannel = headerInfo.packetChannel();

                    if (lastPacketNumbers[packetChannel] >= 0)
                    {
                        if (s32 packetLoss = eth::calc_packet_loss(lastPacketNumbers[packetChannel], headerInfo.packetNumber());
                            packetLoss > 0)
                        {
                            spdlog::warn("Packet loss detected: {} packets lost between {} and {}", packetLoss,
                                        lastPacketNumbers[packetChannel], headerInfo.packetNumber());
                        }
                    }

                    lastPacketNumbers[packetChannel] = headerInfo.packetNumber();
                }
            }
        } while (!ec);

        spdlog::info("Left read loop with error code: {}", ec.message());

        if (ec && !is_socket_timeout(ec))
        {
            spdlog::error("Error reading packet: {}", ec.message());
            return 1;
        }
        else if (ec)
        {
            spdlog::info("Read timeout occurred, no more response data from mvlc.");
        }

        ++transactionCount;
        ++writePayload;
    }
}

static const Command ReadWriteVmeTest
{
    .name = "read_write_vme",
    .exec = read_write_vme_test,
};

static std::map<std::string, Command> tests;
static std::vector<std::string> testNames;

Context ctx;

void add_test(const std::string &name, const Command &cmd)
{
    tests[name] = cmd;
    testNames.push_back(name);
}

void add_test(const Command &cmd) { add_test(cmd.name, cmd); }

int main(int argc, char *argv[])
{

    add_test(ReadRegistersCommand);
    add_test(WriteRegistersCommand);
    add_test(SendEthThrottleCommand);
    add_test(SendRefWordsCommand);
    add_test(ReadWriteVmeTest);

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

    const auto timeout = std::chrono::milliseconds(100);

    spdlog::info("Setting socket timeouts to {} ms", timeout.count());

    if (auto ec = eth::set_socket_write_timeout(sock, timeout.count()))
    {
        spdlog::error("Error setting socket write timeout: {}", ec.message());
        return 1;
    }

    if (auto ec = eth::set_socket_read_timeout(sock, timeout.count()))
    {
        spdlog::error("Error setting socket read timeout: {}", ec.message());
        return 1;
    }

    ctx.hostname = hostname;
    ctx.parser = parser;
    ctx.args = std::vector<std::string>(pos_args.begin() + 2, pos_args.end());
    ctx.cmdSock = sock;

    auto &cmd = tests[testname];
    return cmd.exec(ctx, cmd);
}
