#include <argh.h>
#include <asio.hpp>
#include <boost/endian/conversion.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>

#include <mesytec-mvlc/util/int_types.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <mesytec-mvlc/util/string_util.h>

using namespace mesytec::mvlc;

template <typename Socket> void set_timeout(Socket &sock, std::chrono::milliseconds timeout)
{
    struct timeval tv
    {
        static_cast<time_t>(timeout.count() / 1000),
            static_cast<suseconds_t>((timeout.count() % 1000) * 1000)
    };
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

std::error_code reconnect_ipc(asio::local::stream_protocol::socket &socket,
                              const std::string &socketPath)
{
    if (socket.is_open())
        socket.close();
    set_timeout(socket, std::chrono::milliseconds(500));
    spdlog::info("Connecting to IPC socket: {}", socketPath);
    asio::local::stream_protocol::endpoint endpoint(socketPath);
    std::error_code ec;
    socket.connect(endpoint, ec);
    return ec;
}

std::error_code reconnect_tcp(asio::ip::tcp::socket &socket, const std::string &tcpHost,
                              const std::string &tcpPort)
{
    if (socket.is_open())
        socket.close();
    set_timeout(socket, std::chrono::milliseconds(500));
    spdlog::info("Connecting to TCP URL: {}:{}", tcpHost, tcpPort);
    auto &io_context = static_cast<asio::io_context &>(socket.get_executor().context());
    asio::ip::tcp::resolver resolver(io_context);
    std::error_code ec;
    auto endpoints = resolver.resolve(tcpHost, tcpPort, ec);
    if (ec)
        return ec;
    asio::connect(socket, endpoints, ec);
    return ec;
}

struct FrameHeader
{
    u32 seqNum;
    u32 wordsInFrame;
};

struct ClientContext
{
    bool isRawFormat = false;
    std::vector<u8> destBuffer;
    size_t destBufferUsed = 0;
    ssize_t lastSeqNum = -1;
    size_t totalBytesReceived = 0;
    size_t bytesReceivedInInterval = 0;
    size_t framesReceivedInInterval = 0;
    size_t totalReads = 0;
    size_t totalFramesReceived = 0;
    util::Stopwatch swReport;
    std::optional<FrameHeader> currentFrameHeader;
};

size_t process_data(ClientContext &ctx, std::basic_string_view<u32> data);
size_t process_raw_data(ClientContext &ctx, std::basic_string_view<u32> data);
size_t process_framed_data(ClientContext &ctx, std::basic_string_view<u32> frameBody);

template <typename Sock>
size_t read_at_least(Sock &socket, u8 *dest, size_t bytesNeeded, std::error_code &ec)
{
    size_t bytesRead = 0;
    while (bytesRead < bytesNeeded && !ec)
    {
        auto asioDest = asio::buffer(dest + bytesRead, bytesNeeded - bytesRead);
        bytesRead += asio::read(socket, asioDest, asio::transfer_at_least(asioDest.size()), ec);
    }
    return bytesRead;
}

template <typename Sock>
std::optional<FrameHeader> read_frame_header(Sock &socket, std::error_code &ec)
{
    std::array<u32, 2> dest;
    size_t bytesRead =
        read_at_least(socket, reinterpret_cast<u8 *>(dest.data()), dest.size() * sizeof(u32), ec);

    if (ec)
        return std::nullopt;

    if (bytesRead < sizeof(FrameHeader))
        return std::nullopt;

    if (bytesRead > sizeof(FrameHeader))
    {
        spdlog::warn("read_frame_header(): read {} bytes, expected {}", bytesRead,
                     sizeof(FrameHeader));
    }

    boost::endian::little_to_native_inplace(dest[0]);
    boost::endian::little_to_native_inplace(dest[1]);

    return FrameHeader{dest[0], dest[1]};
}

template <typename Sock, typename ReconnectFun>
int run_client(Sock &socket, ReconnectFun reconnect, ClientContext &ctx)
{
    enum class State
    {
        Connecting,
        Connected,
    } state = State::Connecting;

    std::error_code ec;

    while (true)
    {
        switch (state)
        {
        case State::Connecting:
            if ((ec = reconnect()))
            {
                spdlog::warn("Failed to connect to server: {}", ec.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            else
            {
                state = State::Connected;
                spdlog::info("Connected to server");
                ctx.lastSeqNum = -1;
                ctx.destBufferUsed = 0;
                ctx.framesReceivedInInterval = 0;
                ctx.bytesReceivedInInterval = 0;
            }
            break;

        case State::Connected:
        {
            ctx.destBuffer.resize(util::Megabytes(1));

            if (!ctx.isRawFormat)
            {
                if (!ctx.currentFrameHeader)
                {
                    ctx.currentFrameHeader = read_frame_header(socket, ec);
                    ctx.totalReads += 1;

                    if (ec)
                    {
                        assert(!ctx.currentFrameHeader);
                        spdlog::warn("Error while reading frame header: {}", ec.message());
                        state = State::Connecting;
                        ctx.destBufferUsed = 0;
                        break;
                    }
                    else
                    {
                        ctx.totalBytesReceived += sizeof(FrameHeader);
                    }
                }
                else if (ctx.currentFrameHeader)
                {
                    size_t totalBytesNeeded = ctx.currentFrameHeader->wordsInFrame * sizeof(u32);
                    ctx.destBuffer.resize(totalBytesNeeded);

                    while (ctx.destBufferUsed < totalBytesNeeded && !ec)
                    {
                        size_t bytesToRead = totalBytesNeeded - ctx.destBufferUsed;
                        size_t bytesRead = read_at_least(
                            socket, ctx.destBuffer.data() + ctx.destBufferUsed, bytesToRead, ec);

                        spdlog::debug(
                            "read_at_least() returned {} bytes, bytesToRead was {}, ec={}",
                            bytesRead, bytesToRead, ec.message());

                        if (ec)
                            break;

                        ctx.destBufferUsed += bytesRead;
                        ctx.totalBytesReceived += bytesRead;
                        ctx.totalReads += 1;
                    }

                    // FIXME: leftoff here. this returns "Bad address". no clue yet...
                    if (ctx.destBufferUsed < totalBytesNeeded || ec)
                    {
                        spdlog::warn("Error while reading frame data: {}", ec.message());
                        state = State::Connecting;
                        ctx.destBufferUsed = 0;
                        ctx.currentFrameHeader.reset();
                        break;
                    }
                    else if (!ec && ctx.destBufferUsed >= totalBytesNeeded)
                    {
                        spdlog::trace("Received full frame of size {} bytes, {} words",
                                      totalBytesNeeded, totalBytesNeeded / sizeof(u32));
                        ++ctx.totalFramesReceived;
                        ++ctx.framesReceivedInInterval;

                        size_t wordsConsumed =
                            process_data(ctx, std::basic_string_view<u32>(
                                                  reinterpret_cast<u32 *>(ctx.destBuffer.data()),
                                                  ctx.currentFrameHeader->wordsInFrame));

                        auto bytesConsumed = wordsConsumed * sizeof(u32);

                        if (wordsConsumed != ctx.currentFrameHeader->wordsInFrame)
                        {
                            spdlog::error("process_data() consumed {} words, expected {} words",
                                          wordsConsumed, ctx.currentFrameHeader->wordsInFrame);
                        }

                        ctx.currentFrameHeader.reset();

                        if (bytesConsumed < ctx.destBufferUsed)
                        {
                            auto bytesToMove = ctx.destBufferUsed - bytesConsumed;
                            spdlog::warn(
                                "moving {} trailing bytes ({} words) to front of destBuffer",
                                bytesToMove, bytesToMove / sizeof(u32));
                            ctx.destBufferUsed = ctx.destBufferUsed - bytesConsumed;
                            std::memmove(ctx.destBuffer.data(),
                                         ctx.destBuffer.data() + bytesConsumed, ctx.destBufferUsed);
                        }
                        else
                        {
                            ctx.destBufferUsed = 0;
                        }
                    }
                }
            }

            if (auto interval = ctx.swReport.get_interval(); interval >= std::chrono::seconds(1))
            {
                auto stateStr = (state == State::Connected) ? "connected" : "connecting";
                spdlog::info(
                    "State: {}, "
                    "Received in the last {} ms: {:.2f} MB ({} frames), rate={:.2f} MB/s "
                    ", {:.2f} frames/s"
                    "(total {} MB received), avg {:.2f} reads per frame, {} total "
                    "frames",

                    stateStr,

                    std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(),

                    ctx.bytesReceivedInInterval / util::Megabytes(1) * 1.0,

                    ctx.framesReceivedInInterval,

                    ctx.bytesReceivedInInterval / util::Megabytes(1) * 1.0 /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),

                    ctx.framesReceivedInInterval /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),

                    ctx.totalBytesReceived / util::Megabytes(1) * 1.0,

                    ctx.totalReads * 1.0 / ctx.totalFramesReceived, ctx.totalFramesReceived);

                ctx.swReport.interval();
                ctx.bytesReceivedInInterval = 0;
                ctx.framesReceivedInInterval = 0;
            }
            break;
        }
        }
    }
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);
    argh::parser parser({"-h", "--help", "--log-level", "--tcp", "--ipc", "--raw"});
    parser.parse(argc, argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = util::str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";
        else if (parser["--info"])
            logLevelName = "info";
        else if (parser["--warn"])
            logLevelName = "warn";

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
    }

    if (parser[{"-h", "--help"}])
    {
        std::cout << "Usage: " << argv[0] << " [--tcp [host:port]|--ipc [socket_path]] [--raw]"
                  << " [--log-level level][--trace][--debug][--info][--warn]\n\n";

        std::cout
            << " --raw: Expect raw data format without { bufferNumber, bufferSize } headers.\n";
        return 0;
    }

    std::string tcpHost = "127.0.0.1";
    std::string tcpPort = "42333";
    std::string socketPath = "/tmp/mvme_stream_server.sock";
    bool isRawFormat = false;
    enum class Transport
    {
        TCP,
        IPC
    } method = Transport::TCP;

    std::string tmp;

    if (parser["--ipc"] || parser("--ipc") >> tmp)
    {
        if (!tmp.empty())
        {
            socketPath = tmp;
        }
        method = Transport::IPC;
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
        method = Transport::TCP;
    }

    if (parser["--raw"])
    {
        isRawFormat = true;
    }

    ClientContext clientContext;
    clientContext.isRawFormat = isRawFormat;
    int ret = 0;

    try
    {
        asio::io_context io_context;

        switch (method)
        {
        case Transport::IPC:
        {
            asio::local::stream_protocol::socket socket(io_context);
            auto reconnectFun = std::bind(reconnect_ipc, std::ref(socket), std::ref(socketPath));
            ret = run_client(socket, reconnectFun, clientContext);
        }
        break;
        case Transport::TCP:
        {
            asio::ip::tcp::socket socket(io_context);
            auto reconnectFun =
                std::bind(reconnect_tcp, std::ref(socket), std::ref(tcpHost), std::ref(tcpPort));
            ret = run_client(socket, reconnectFun, clientContext);
        }
        break;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }
}

// Unframed readout data. Cannot distinguish server created loss from UDP packet
// loss that occured at the readout side.
//
// Returns the number of words consumed.
size_t process_raw_data(ClientContext &ctx, std::basic_string_view<u32> data)
{
    if (data.empty())
        return 0;
    return data.size(); // fake it and never make it
    // return 0;
}

size_t process_data(ClientContext &ctx, std::basic_string_view<u32> data)
{
    if (!ctx.isRawFormat)
        return process_framed_data(ctx, data);
    return process_raw_data(ctx, data);
}

// Process as much input data as possible. Must return the number of bytes
// consumed.
//
// Framed format: bufferNumber: u32, bufferSize: u32, buffer: u32[]
//
// Allows to detect buffers skipped by the server based on the sequence number.
// E.g. if the server operates on a best-effort basis and at least one connected
// client is too slow to keep up with the data volume the server will drop
// buffers.
// Also allows to distinguish between server created loss and UDP packet loss at
// the readout side.
//
// Returns the number of words consumed.
size_t process_framed_data(ClientContext &ctx, std::basic_string_view<u32> frameBody)
{
    if (!ctx.currentFrameHeader)
    {
        spdlog::error("process_framed_data() called without valid currentFrameHeader");
        return 0;
    }

    size_t totalBytesNeeded = ctx.currentFrameHeader->wordsInFrame * sizeof(u32);

    if (ctx.destBufferUsed < totalBytesNeeded)
    {
        spdlog::error(
            "process_framed_data(): not enough data in destBuffer, expected {} bytes, have "
            "{} bytes",
            totalBytesNeeded, ctx.destBufferUsed);
        return 0;
    }

    if (ctx.currentFrameHeader->seqNum != -1 && ctx.lastSeqNum != -1 &&
        ctx.currentFrameHeader->seqNum != static_cast<u32>(ctx.lastSeqNum + 1u))
    {
        spdlog::warn("Buffer loss detected: last seq num {}, current seq num {}", ctx.lastSeqNum,
                     ctx.currentFrameHeader->seqNum);
    }

    auto wordsProcessed = process_raw_data(ctx, frameBody);

    if (wordsProcessed != ctx.currentFrameHeader->wordsInFrame)
    {
        // This should not happen. The server should guarantee that only complete frames are
        // sent.
        spdlog::error("Format error: framed buffer with seq num {} contains {} words, but only {} "
                      "words could be processed.",
                      ctx.currentFrameHeader->seqNum, ctx.currentFrameHeader->wordsInFrame,
                      wordsProcessed);
    }

    return 2 + wordsProcessed;
}
