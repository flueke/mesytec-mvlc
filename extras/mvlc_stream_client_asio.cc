#include <argh.h>
#include <asio.hpp>
#include <boost/endian/conversion.hpp>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// TODO:
// - always handle systemframes with crateconfig info. the client has to update
//    the config as a different file/run might be streamed.
// - also reset state when detecting the endRun or endOfFile type system events.

using namespace mesytec;

// Frame header for framed format
struct FrameHeader
{
    uint32_t seqNum;
    uint32_t wordsInFrame;
};

struct MvlcParsingState
{
    std::vector<uint32_t> crateConfigBuffer;
    std::optional<mvlc::CrateConfig> crateConfig;
    std::optional<mvlc::readout_parser::ReadoutParserState> parserState;
    mvlc::readout_parser::ReadoutParserCounters parserCounters;
    // Local buffer sequence number for the raw format case.
    size_t rawFormatSequenceNumber = 1;
};

// Client state and statistics
struct ClientContext
{
    bool isRawFormat = false;
    std::vector<uint8_t> buffer;
    size_t bufferUsed = 0;

    int64_t lastSeqNum = -1;
    size_t totalBytesReceived = 0;
    // This counts the stream protocol frames if the framed format is used, not
    // inner MVLC data frames.
    size_t totalFramesReceived = 0;

    // Statistics for periodic reporting
    size_t bytesReceivedInInterval = 0;
    size_t framesReceivedInInterval = 0;
    std::chrono::steady_clock::time_point lastReportTime;

    MvlcParsingState mvlcState;

    ClientContext()
        : lastReportTime(std::chrono::steady_clock::now())
    {
    }
};

// Set socket timeouts for send and receive operations
template <typename Socket> void set_socket_timeout(Socket &sock, std::chrono::milliseconds timeout)
{
    struct timeval tv
    {
        static_cast<time_t>(timeout.count() / 1000),
            static_cast<suseconds_t>((timeout.count() % 1000) * 1000)
    };
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// TCP connection helper
std::error_code connect_tcp(asio::ip::tcp::socket &socket, const std::string &host,
                            const std::string &port)
{
    if (socket.is_open())
        socket.close();

    set_socket_timeout(socket, std::chrono::milliseconds(2000));

    spdlog::info("Connecting to TCP: {}:{}", host, port);

    auto &io_context = static_cast<asio::io_context &>(socket.get_executor().context());
    asio::ip::tcp::resolver resolver(io_context);

    std::error_code ec;
    auto endpoints = resolver.resolve(host, port, ec);
    if (ec)
        return ec;

    asio::connect(socket, endpoints, ec);
    return ec;
}

// Unix domain socket connection helper
std::error_code connect_unix(asio::local::stream_protocol::socket &socket, const std::string &path)
{
    if (socket.is_open())
        socket.close();

    set_socket_timeout(socket, std::chrono::milliseconds(2000));

    spdlog::info("Connecting to Unix socket: {}", path);

    asio::local::stream_protocol::endpoint endpoint(path);
    std::error_code ec;
    socket.connect(endpoint, ec);
    return ec;
}

// Read exactly the requested number of bytes from socket
template <typename Socket>
size_t read_exact(Socket &socket, uint8_t *dest, size_t bytesNeeded, std::error_code &ec)
{
    size_t bytesRead = 0;
    while (bytesRead < bytesNeeded && !ec)
    {
        auto asioDest = asio::buffer(dest + bytesRead, bytesNeeded - bytesRead);
        size_t n = asio::read(socket, asioDest, asio::transfer_at_least(asioDest.size()), ec);
        bytesRead += n;
    }
    return bytesRead;
}

void event_data_callback(void *userContext, int crateIndex, int triggerEventIndex,
                         const mvlc::readout_parser::ModuleData *moduleDataList,
                         unsigned moduleCount)
{
    spdlog::info("Event data callback: crateIndex={}, triggerEventIndex={}, moduleCount={}",
                 crateIndex, triggerEventIndex, moduleCount);

    for (unsigned i = 0; i < moduleCount; ++i)
    {
        const auto &modData = moduleDataList[i];
        spdlog::info("  Module {}: prefixSize={}, dynamicSize={}, suffixSize={}, hasDynamic={}", i,
                     modData.prefixSize, modData.dynamicSize, modData.suffixSize,
                     modData.hasDynamic);
    }
}

void system_event_callback(void *userContext, int crateIndex, const uint32_t *header, uint32_t size)
{
    spdlog::info("System event callback: crateIndex={}, size={}", crateIndex, size);
}

// Process raw unframed data. Returns the number of words processed or -1 on error.
ssize_t process_raw_data(ClientContext &ctx, uint32_t bufferNumber,
                         std::basic_string_view<uint32_t> data)
{
    if (data.empty())
        return 0;

    const auto origDataSize = data.size();
    auto &mvlcState = ctx.mvlcState;

    // If we directly land on a known header it must be MVLC USB formatted data
    // without the additional ETH header words.
    // TODO: move this into the parser code itself. This detection happens
    // everywhere and is annoying.
    const auto connectionType = mvlc::is_known_frame_header(data[0]) ? mvlc::ConnectionType::USB
                                                                     : mvlc::ConnectionType::ETH;

    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks = {event_data_callback,
                                                                    system_event_callback};

    bool done = false;

    while (!done && !data.empty())
    {
        if (mvlcState.parserState)
        {
            try
            {
                auto parseResult = mvlc::readout_parser::parse_readout_buffer(
                    connectionType, *mvlcState.parserState, parserCallbacks,
                    mvlcState.parserCounters, bufferNumber, data.data(), data.size());
                // TODO: handle parseResult. count it, print it, log errors, ...

                if (parseResult != mvlc::readout_parser::ParseResult::Ok)
                {
                    spdlog::error("Readout parsing error: {}",
                                  mvlc::readout_parser::get_parse_result_name(parseResult));
                }
            }
            catch (const std::exception &e)
            {
                spdlog::error("Exception during readout parsing: {}", e.what());
            }
            data.remove_prefix(data.size()); // consume it all
        }
        else
        {
            // Read the MVLCCrateConfig parts of the preamble if it's being
            // streamed.
            // Otherwise just follow the mvlc framing format/ eth packet headers and
            // print a bit of info.
            while (!data.empty())
            {
                assert(mvlc::is_known_frame_header(data[0]));
                auto headerInfo = mvlc::extract_frame_info(data[0]);
                assert(data.size() >= headerInfo.len + 1); // should be guaranteed by the caller

                if (data.size() >= headerInfo.len + 1)
                {
                    if (headerInfo.type == mvlc::frame_headers::SystemEvent &&
                        headerInfo.sysEventSubType == mvlc::system_event::subtype::MVLCCrateConfig)
                    {
                        std::copy(data.begin() + 1, data.begin() + 1 + headerInfo.len,
                                  std::back_inserter(mvlcState.crateConfigBuffer));

                        if (!(headerInfo.flags & mvlc::frame_flags::Continue))
                        {
                            try
                            {
                                mvlcState.crateConfig = mvlc::crate_config_from_yaml(std::string(
                                    reinterpret_cast<const char *>(
                                        mvlcState.crateConfigBuffer.data()),
                                    mvlcState.crateConfigBuffer.size() * sizeof(uint32_t)));

                                mvlcState.parserState = mvlc::readout_parser::make_readout_parser(
                                    mvlcState.crateConfig->stacks);
                            }
                            catch (const std::exception &e)
                            {
                                spdlog::error("Failed to parse crate config YAML: {}", e.what());
                            }
                        }
                    }
                    else
                    {
                        // TODO: print generic frame info, type, len, flags, start of contents,
                        // syseventsubtype if applicable
                    }

                    data.remove_prefix(headerInfo.len + 1);
                }
                else
                {
                    spdlog::error("Incomplete frame detected during preamble parsing");
                    done = true;
                    break;
                }
            }
        }
    }

    // Returns the number of words processed. original size - remaining size
    return origDataSize - data.size();
}

// Process one complete frame. Returns the number of words processed or -1 on error.
ssize_t process_frame(ClientContext &ctx, const FrameHeader &header,
                      std::basic_string_view<uint32_t> frameView)
{
    // Check for sequence number gaps and warn. The parser also handles this
    // internally, resets its state and resumes parsing when new data arrives.
    if (ctx.lastSeqNum != -1 && header.seqNum != static_cast<uint32_t>(ctx.lastSeqNum + 1))
    {
        spdlog::warn("Frame loss detected: last={}, current={}",
                     static_cast<long long>(ctx.lastSeqNum), header.seqNum);
    }

    ctx.lastSeqNum = header.seqNum;

    if (frameView.size() != header.wordsInFrame)
    {
        spdlog::error("Frame size mismatch: expected {} words, got {} words", header.wordsInFrame,
                      frameView.size());
        return -1;
    }

    // Process the frame payload
    return process_raw_data(ctx, header.seqNum, frameView);
}

// Print statistics periodically
void maybe_print_stats(ClientContext &ctx)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.lastReportTime);

    if (elapsed >= std::chrono::seconds(1))
    {
        double elapsedSec = elapsed.count() / 1000.0;
        double mbReceived = ctx.bytesReceivedInInterval / (1024.0 * 1024.0);
        double mbps = mbReceived / elapsedSec;
        double fps = ctx.framesReceivedInInterval / elapsedSec;

        spdlog::info("Stats: {:.2f} MB/s, {:.2f} frames/s (total: {} frames, {} bytes)", mbps, fps,
                     ctx.totalFramesReceived, ctx.totalBytesReceived);

        ctx.bytesReceivedInInterval = 0;
        ctx.framesReceivedInInterval = 0;
        ctx.lastReportTime = now;
    }
}

// Main client loop for framed protocol using bulk reading
template <typename Socket, typename ConnectFunc>
int run_client_framed(Socket &socket, ConnectFunc reconnect, ClientContext &ctx)
{
    enum class State
    {
        Connecting,
        Connected
    };
    State state = State::Connecting;

    std::error_code ec;
    const size_t readBufferSize = 1u << 20; // 1 MB
    ctx.buffer.resize(readBufferSize);

    while (true)
    {
        maybe_print_stats(ctx);

        switch (state)
        {
        case State::Connecting:
        {
            ec = reconnect();
            if (ec)
            {
                spdlog::warn("Failed to connect: {}", ec.message().c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            else
            {
                spdlog::info("Connected");
                state = State::Connected;
                ctx.lastSeqNum = -1;
                ctx.bufferUsed = 0; // Reset buffer on reconnect
            }
            break;
        }

        case State::Connected:
        {
            // Bulk read from socket into buffer after existing data
            size_t bytesToRead = ctx.buffer.size() - ctx.bufferUsed;
            spdlog::debug("Attempting to read up to {} bytes", bytesToRead);
            size_t bytesRead =
                socket.read_some(asio::buffer(ctx.buffer.data() + ctx.bufferUsed, bytesToRead), ec);

            if (ec && ec != asio::error::timed_out)
            {
                spdlog::warn("Read error: {}", ec.message().c_str());
                state = State::Connecting;
                ctx.bufferUsed = 0; // Discard partial data on error
                break;
            }
            else if (ec && ec == asio::error::timed_out)
            {
                spdlog::warn("Read timed out with {} bytes read, trying again", bytesRead);
            }

            ctx.bytesReceivedInInterval += bytesRead;
            ctx.totalBytesReceived += bytesRead;
            ctx.bufferUsed += bytesRead;

            // Parse all complete frames from the buffer
            const uint8_t *data = ctx.buffer.data();
            size_t consumed = 0;
            size_t remaining = ctx.bufferUsed;

            while (remaining >= sizeof(FrameHeader))
            {
                // Read frame header inline
                const FrameHeader *header = reinterpret_cast<const FrameHeader *>(data + consumed);
                size_t bytesInFrame = header->wordsInFrame * sizeof(uint32_t);
                size_t totalFrameBytes = sizeof(FrameHeader) + bytesInFrame;

                // Check if we have the complete frame
                if (remaining < totalFrameBytes)
                {
                    spdlog::debug("Incomplete frame: have {} bytes, need {} bytes.", remaining,
                                  totalFrameBytes);
                    if (ctx.buffer.size() < totalFrameBytes)
                    {
                        spdlog::debug("Resizing buffer to {} bytes", totalFrameBytes);
                        ctx.buffer.resize(totalFrameBytes);
                    }
                    break; // Incomplete frame - wait for more data
                }

                spdlog::debug("Complete frame received: seqNum={}, wordsInFrame={}", header->seqNum,
                              header->wordsInFrame);
                ++ctx.framesReceivedInInterval;
                ++ctx.totalFramesReceived;

                std::basic_string_view<uint32_t> frameView(
                    reinterpret_cast<const uint32_t *>(data + consumed + sizeof(FrameHeader)),
                    header->wordsInFrame);

                auto wordsProcessed = process_frame(ctx, *header, frameView);

                if (wordsProcessed < 0)
                {
                    spdlog::warn("Error processing frame, resetting connection.");
                    state = State::Connecting;
                    ctx.bufferUsed = 0;
                    break;
                }

                // Advance to next frame
                consumed += wordsProcessed * sizeof(uint32_t) + sizeof(FrameHeader);
                remaining -= wordsProcessed * sizeof(uint32_t) + sizeof(FrameHeader);
            }

            // Move any leftover partial data to the front of the buffer.
            if (remaining > 0 && consumed > 0)
            {
                spdlog::warn("Moving {} bytes of leftover data to front of buffer", remaining);
                std::memmove(ctx.buffer.data(), data + consumed, remaining);
            }
            ctx.bufferUsed = remaining;

            break;
        }
        }
    }

    return 0;
}

// Main client loop for raw (unframed) protocol
template <typename Socket, typename ConnectFunc>
int run_client_raw(Socket &socket, ConnectFunc reconnect, ClientContext &ctx)
{
    enum class State
    {
        Connecting,
        Connected
    };
    State state = State::Connecting;

    std::error_code ec;
    const size_t readBufferSize = 1u << 20; // 1 MB
    ctx.buffer.resize(readBufferSize);

    while (true)
    {
        maybe_print_stats(ctx);

        switch (state)
        {
        case State::Connecting:
        {
            ec = reconnect();
            if (ec)
            {
                spdlog::warn("Failed to connect: {}", ec.message().c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            else
            {
                spdlog::info("Connected");
                state = State::Connected;
                ctx.bufferUsed = 0; // Reset buffer on reconnect
                ctx.mvlcState.rawFormatSequenceNumber = 1;
            }
            break;
        }

        case State::Connected:
        {
            // Read whatever data is available
            auto bytesToRead = ctx.buffer.size() - ctx.bufferUsed;
            size_t bytesRead =
                socket.read_some(asio::buffer(ctx.buffer.data() + ctx.bufferUsed, bytesToRead), ec);

            if (ec && ec != asio::error::timed_out)
            {
                spdlog::warn("Read error: {}", ec.message().c_str());
                state = State::Connecting;
                ctx.bufferUsed = 0; // Discard partial data on error
                break;
            }
            else if (ec && ec == asio::error::timed_out)
            {
                spdlog::warn("Read timed out with {} bytes read, trying again", bytesRead);
            }

            ctx.bufferUsed += bytesRead;
            ctx.bytesReceivedInInterval += bytesRead;
            ctx.totalBytesReceived += bytesRead;

            std::basic_string_view<uint32_t> dataView(
                reinterpret_cast<const uint32_t *>(ctx.buffer.data()),
                ctx.bufferUsed / sizeof(uint32_t));

            auto wordsConsumed =
                process_raw_data(ctx, ctx.mvlcState.rawFormatSequenceNumber, dataView);

            if (wordsConsumed == 0)
            {
                spdlog::debug("No complete data processed yet, waiting for more data");
                break;
            }
            else
            {
                ++ctx.mvlcState.rawFormatSequenceNumber;
            }

            // Move leftover data to the front of the buffer
            size_t bytesConsumed = wordsConsumed * sizeof(uint32_t);
            if (bytesConsumed < ctx.bufferUsed)
            {
                size_t bytesLeftover = ctx.bufferUsed - bytesConsumed;
                spdlog::debug("Moving {} bytes of leftover data to front of buffer", bytesLeftover);
                std::memmove(ctx.buffer.data(), ctx.buffer.data() + bytesConsumed, bytesLeftover);
                ctx.bufferUsed = bytesLeftover;
            }
            else
            {
                ctx.bufferUsed = 0;
            }

            break;
        }
        }
    }

    return 0;
}

// Main entry point
int main(int argc, char *argv[])
{
    argh::parser parser({"-h", "--help", "--tcp", "--unix", "--raw"});
    parser.parse(argc, argv);

    if (parser[{"-h", "--help"}])
    {
        std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --tcp [host:port]    Connect via TCP (default: 127.0.0.1:42333)\n";
        std::cout << "  --unix [path]        Connect via Unix domain socket (default: "
                     "/tmp/mvme_stream_server.sock)\n";
        std::cout << "  --raw                Use raw (unframed) protocol\n";
        std::cout << "  -h, --help           Show this help\n";
        return 0;
    }

    // Parse connection parameters
    enum class Transport
    {
        TCP,
        Unix
    };
    Transport transport = Transport::TCP;

    std::string tcpHost = "127.0.0.1";
    std::string tcpPort = "42333";
    std::string unixPath = "/tmp/mvme_stream_server.sock";
    bool rawFormat = parser["--raw"];

    std::string tmp;

    if (parser["--unix"] || parser("--unix") >> tmp)
    {
        if (!tmp.empty())
            unixPath = tmp;
        transport = Transport::Unix;
    }
    else if (parser["--tcp"] || parser("--tcp") >> tmp)
    {
        if (!tmp.empty())
        {
            auto pos = tmp.find(':');
            if (pos != std::string::npos)
            {
                tcpHost = tmp.substr(0, pos);
                tcpPort = tmp.substr(pos + 1);
            }
            else
            {
                tcpHost = tmp;
            }
        }
        transport = Transport::TCP;
    }

    // Create client context
    ClientContext ctx;
    ctx.isRawFormat = rawFormat;

    try
    {
        asio::io_context io_context;
        int ret = 0;

        switch (transport)
        {
        case Transport::TCP:
        {
            asio::ip::tcp::socket socket(io_context);
            auto reconnect = [&]() { return connect_tcp(socket, tcpHost, tcpPort); };

            if (rawFormat)
                ret = run_client_raw(socket, reconnect, ctx);
            else
                ret = run_client_framed(socket, reconnect, ctx);
            break;
        }

        case Transport::Unix:
        {
            asio::local::stream_protocol::socket socket(io_context);
            auto reconnect = [&]() { return connect_unix(socket, unixPath); };

            if (rawFormat)
                ret = run_client_raw(socket, reconnect, ctx);
            else
                ret = run_client_framed(socket, reconnect, ctx);
            break;
        }
        }

        return ret;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception: {}", e.what());
        return 1;
    }
}
