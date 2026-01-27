#include <argh.h>
#include <asio.hpp>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Simple logger - replace with spdlog if needed
namespace log
{
    template<typename... Args>
    void info(const char* fmt, Args&&... args) {
        printf("[INFO] ");
        if constexpr (sizeof...(Args) > 0) {
            printf(fmt, std::forward<Args>(args)...);
        } else {
            printf("%s", fmt);
        }
        printf("\n");
    }

    template<typename... Args>
    void warn(const char* fmt, Args&&... args) {
        printf("[WARN] ");
        if constexpr (sizeof...(Args) > 0) {
            printf(fmt, std::forward<Args>(args)...);
        } else {
            printf("%s", fmt);
        }
        printf("\n");
    }

    template<typename... Args>
    void error(const char* fmt, Args&&... args) {
        printf("[ERROR] ");
        if constexpr (sizeof...(Args) > 0) {
            printf(fmt, std::forward<Args>(args)...);
        } else {
            printf("%s", fmt);
        }
        printf("\n");
    }

    template<typename... Args>
    void debug(const char* fmt, Args&&... args) {
        #if 0
        printf("[DEBUG] ");
        if constexpr (sizeof...(Args) > 0) {
            printf(fmt, std::forward<Args>(args)...);
        } else {
            printf("%s", fmt);
        }
        printf("\n");
        #endif
    }
}

// Frame header for framed format
struct FrameHeader
{
    uint32_t seqNum;
    uint32_t wordsInFrame;
};

// Client state and statistics
struct ClientContext
{
    bool isRawFormat = false;
    std::vector<uint8_t> buffer;
    size_t bufferUsed = 0; // How many bytes in buffer are valid data

    int64_t lastSeqNum = -1;
    size_t totalBytesReceived = 0;
    size_t totalFramesReceived = 0;

    // Statistics for periodic reporting
    size_t bytesReceivedInInterval = 0;
    size_t framesReceivedInInterval = 0;
    std::chrono::steady_clock::time_point lastReportTime;

    ClientContext() : lastReportTime(std::chrono::steady_clock::now()) {}
};

// Set socket timeouts for send and receive operations
template <typename Socket>
void set_socket_timeout(Socket& sock, std::chrono::milliseconds timeout)
{
    struct timeval tv {
        static_cast<time_t>(timeout.count() / 1000),
        static_cast<suseconds_t>((timeout.count() % 1000) * 1000)
    };
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// TCP connection helper
std::error_code connect_tcp(
    asio::ip::tcp::socket& socket,
    const std::string& host,
    const std::string& port)
{
    if (socket.is_open())
        socket.close();

    set_socket_timeout(socket, std::chrono::milliseconds(2000));

    log::info("Connecting to TCP: %s:%s", host.c_str(), port.c_str());

    auto& io_context = static_cast<asio::io_context&>(socket.get_executor().context());
    asio::ip::tcp::resolver resolver(io_context);

    std::error_code ec;
    auto endpoints = resolver.resolve(host, port, ec);
    if (ec)
        return ec;

    asio::connect(socket, endpoints, ec);
    return ec;
}

// Unix domain socket connection helper
std::error_code connect_unix(
    asio::local::stream_protocol::socket& socket,
    const std::string& path)
{
    if (socket.is_open())
        socket.close();

    set_socket_timeout(socket, std::chrono::milliseconds(2000));

    log::info("Connecting to Unix socket: %s", path.c_str());

    asio::local::stream_protocol::endpoint endpoint(path);
    std::error_code ec;
    socket.connect(endpoint, ec);
    return ec;
}

// Read exactly the requested number of bytes from socket
template <typename Socket>
size_t read_exact(
    Socket& socket,
    uint8_t* dest,
    size_t bytesNeeded,
    std::error_code& ec)
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

// Read frame header (8 bytes: seqNum + wordsInFrame)
template <typename Socket>
std::optional<FrameHeader> read_frame_header(Socket& socket, std::error_code& ec)
{
    uint32_t header[2];
    size_t bytesRead = read_exact(socket, reinterpret_cast<uint8_t*>(header), sizeof(header), ec);

    if (ec || bytesRead < sizeof(header))
        return std::nullopt;

    // Convert from little-endian if needed
    // Note: Add endian conversion here if needed for your platform

    return FrameHeader{ header[0], header[1] };
}

// Process raw unframed data
size_t process_raw_data(ClientContext& ctx, const uint8_t* data, size_t size)
{
    // TODO: Implement actual data processing
    // For now, just acknowledge receipt
    ctx.bytesReceivedInInterval += size;
    return size;
}

// Process one complete frame
size_t process_frame(ClientContext& ctx, const FrameHeader& header, const uint8_t* data, size_t size)
{
    // Check for sequence number gaps
    if (ctx.lastSeqNum != -1 && header.seqNum != static_cast<uint32_t>(ctx.lastSeqNum + 1))
    {
        log::warn("Frame loss detected: last=%lld, current=%u",
            static_cast<long long>(ctx.lastSeqNum), header.seqNum);
    }

    ctx.lastSeqNum = header.seqNum;
    ctx.totalFramesReceived++;
    ctx.framesReceivedInInterval++;

    size_t expectedBytes = header.wordsInFrame * sizeof(uint32_t);
    if (size != expectedBytes)
    {
        log::error("Frame size mismatch: expected %zu bytes, got %zu bytes", expectedBytes, size);
        return 0;
    }

    // Process the frame payload
    return process_raw_data(ctx, data, size);
}

// Print statistics periodically
void maybe_print_stats(ClientContext& ctx)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.lastReportTime);

    if (elapsed >= std::chrono::seconds(1))
    {
        double elapsedSec = elapsed.count() / 1000.0;
        double mbReceived = ctx.bytesReceivedInInterval / (1024.0 * 1024.0);
        double mbps = mbReceived / elapsedSec;
        double fps = ctx.framesReceivedInInterval / elapsedSec;

        log::info("Stats: %.2f MB/s, %.2f frames/s (total: %zu frames, %zu bytes)",
            mbps, fps, ctx.totalFramesReceived, ctx.totalBytesReceived);

        ctx.bytesReceivedInInterval = 0;
        ctx.framesReceivedInInterval = 0;
        ctx.lastReportTime = now;
    }
}

// Main client loop for framed protocol using bulk reading
template <typename Socket, typename ConnectFunc>
int run_client_framed(Socket& socket, ConnectFunc reconnect, ClientContext& ctx)
{
    enum class State { Connecting, Connected };
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
                    log::warn("Failed to connect: %s", ec.message().c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                else
                {
                    log::info("Connected");
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
                log::debug("Attempting to read up to %zu bytes", bytesToRead);
                size_t bytesRead = socket.read_some(
                    asio::buffer(ctx.buffer.data() + ctx.bufferUsed, bytesToRead), ec);

                if (ec && ec != asio::error::timed_out)
                {
                    log::warn("Read error: %s", ec.message().c_str());
                    state = State::Connecting;
                    ctx.bufferUsed = 0; // Discard partial data on error
                    break;
                }
                else if (ec && ec == asio::error::timed_out)
                {
                    log::warn("Read timed out with {} bytes read, trying again", bytesRead);
                }

                ctx.totalBytesReceived += bytesRead;
                ctx.bufferUsed += bytesRead;

                // Parse all complete frames from the buffer
                const uint8_t* data = ctx.buffer.data();
                size_t consumed = 0;
                size_t remaining = ctx.bufferUsed;

                while (remaining >= sizeof(FrameHeader))
                {
                    // Read frame header inline
                    const FrameHeader* header = reinterpret_cast<const FrameHeader*>(data + consumed);
                    size_t framePayloadBytes = header->wordsInFrame * sizeof(uint32_t);
                    size_t totalFrameBytes = sizeof(FrameHeader) + framePayloadBytes;

                    // Check if we have the complete frame
                    if (remaining < totalFrameBytes)
                    {
                        log::debug("Incomplete frame: have %zu bytes, need %zu bytes.", remaining, totalFrameBytes);
                        if (ctx.buffer.size() < sizeof(FrameHeader) + totalFrameBytes)
                        {
                            log::debug("Resizing buffer to %zu bytes", sizeof(FrameHeader) + totalFrameBytes);
                            ctx.buffer.resize(sizeof(FrameHeader) + totalFrameBytes);
                        }
                        break; // Incomplete frame - wait for more data
                    }

                    log::debug("Complete frame received: seqNum=%u, wordsInFrame=%u",
                        header->seqNum, header->wordsInFrame);

                    // We have a complete frame - process it
                    const uint8_t* frameData = data + consumed + sizeof(FrameHeader);
                    process_frame(ctx, *header, frameData, framePayloadBytes);

                    // Advance to next frame
                    consumed += totalFrameBytes;
                    remaining -= totalFrameBytes;
                }

                // Move any leftover partial data to front of buffer
                if (remaining > 0 && consumed > 0)
                {
                    log::warn("Moving %zu bytes of leftover data to front of buffer", remaining);
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
int run_client_raw(Socket& socket, ConnectFunc reconnect, ClientContext& ctx)
{
    enum class State { Connecting, Connected };
    State state = State::Connecting;

    std::error_code ec;
    ctx.buffer.resize(1024 * 1024); // 1 MB buffer

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
                    log::warn("Failed to connect: %s", ec.message().c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                else
                {
                    log::info("Connected");
                    state = State::Connected;
                }
                break;
            }

            case State::Connected:
            {
                // Read whatever data is available
                size_t bytesRead = socket.read_some(asio::buffer(ctx.buffer), ec);

                if (ec || bytesRead == 0)
                {
                    if (bytesRead == 0)
                        log::warn("Disconnected (read returned 0 bytes)");
                    else
                        log::warn("Read error: %s", ec.message().c_str());
                    state = State::Connecting;
                    break;
                }

                ctx.totalBytesReceived += bytesRead;
                process_raw_data(ctx, ctx.buffer.data(), bytesRead);
                break;
            }
        }
    }

    return 0;
}

// Main entry point
int main(int argc, char* argv[])
{
    argh::parser parser({"-h", "--help", "--tcp", "--unix", "--raw"});
    parser.parse(argc, argv);

    if (parser[{"-h", "--help"}])
    {
        std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --tcp [host:port]    Connect via TCP (default: 127.0.0.1:42333)\n";
        std::cout << "  --unix [path]        Connect via Unix domain socket (default: /tmp/mvme_stream_server.sock)\n";
        std::cout << "  --raw                Use raw (unframed) protocol\n";
        std::cout << "  -h, --help           Show this help\n";
        return 0;
    }

    // Parse connection parameters
    enum class Transport { TCP, Unix };
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
    catch (const std::exception& e)
    {
        log::error("Exception: %s", e.what());
        return 1;
    }
}
