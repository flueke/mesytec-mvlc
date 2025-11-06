#include <argh.h>
#include <asio.hpp>
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

std::error_code reconnect_ipc(asio::local::stream_protocol::socket &socket,
                              const std::string &socketPath)
{
    if (socket.is_open())
        socket.close();
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

struct ClientContext
{
    bool isRawFormat = false;
    std::vector<u8> destBuffer;
    size_t destBufferUsed = 0;
    ssize_t lastSeqNum = -1;
    size_t totalBytesReceived = 0;
    size_t bytesReceivedInInterval = 0;
    size_t buffersReceivedInInterval = 0;
    size_t totalReads = 0;
    size_t totalBuffersReceived = 0;
    util::Stopwatch swReport;
};

template <typename Sock>
size_t read_at_least(Sock &socket, u8 *dest, size_t bytesNeeded, std::error_code &ec)
{
    size_t bytesRead = 0;
    while (bytesRead < bytesNeeded && !ec)
    {
        auto asioDest = asio::buffer(dest + bytesRead, bytesNeeded - bytesRead);
        bytesRead += asio::read(socket, asioDest, ec);
    }
    return bytesRead;
}

// Must return the number of bytes consumed.
size_t process_data(ClientContext &ctx, std::basic_string_view<u32> buffer);

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
            }
            break;

        case State::Connected:
        {
            ctx.destBuffer.resize(util::Megabytes(1));
            const size_t minBytes = ctx.isRawFormat ? sizeof(u32) : 2 * sizeof(u32);
            size_t bytesRead = 0;

            while (ctx.destBufferUsed < minBytes && !ec)
            {
                auto dest = asio::buffer(ctx.destBuffer.data() + ctx.destBufferUsed,
                                         ctx.destBuffer.size() - ctx.destBufferUsed);
                bytesRead = asio::read(socket, dest, ec);

                if (!ec)
                {
                    ctx.destBufferUsed += bytesRead;
                    ctx.totalBytesReceived += bytesRead;
                    ctx.bytesReceivedInInterval += bytesRead;
                    ctx.totalReads += 1;
                }
            }

            if (ec)
            {
                if (ec == asio::error::eof)
                {
                    spdlog::warn("Connection closed by server");
                }
                else if (ec == asio::error::connection_reset || ec == asio::error::broken_pipe)
                {
                    spdlog::warn("Connection lost: {}", ec.message());
                }
                else
                {
                    spdlog::warn("Error while reading from server: {}", ec.message());
                }
                state = State::Connecting;
                break;
            }

            spdlog::trace("Buffer size after reading: {} bytes, {} words", ctx.destBufferUsed,
                          ctx.destBufferUsed / sizeof(u32));

            std::basic_string_view<u32> bufferView(reinterpret_cast<u32 *>(ctx.destBuffer.data()),
                                                   ctx.destBufferUsed / sizeof(u32));
            size_t bytesConsumed = process_data(ctx, bufferView);
            assert(bytesConsumed <= ctx.destBufferUsed);
            if (bytesConsumed < ctx.destBufferUsed)
            {
                // move remaining data to the front of the buffer
                size_t bytesRemaining = ctx.destBufferUsed - bytesConsumed;
                std::memmove(ctx.destBuffer.data(), ctx.destBuffer.data() + bytesConsumed,
                             bytesRemaining);
                ctx.destBufferUsed = bytesRemaining;
            }
            else
            {
                ctx.destBufferUsed = 0;
            }

            if (auto interval = ctx.swReport.get_interval(); interval >= std::chrono::seconds(1))
            {
                spdlog::info(
                    "Received in the last {} ms: {:.2f} MB ({} buffers), rate={:.2f} MB/s ({:.2f} "
                    "buffers/s) (total {} MB received), avg {:.2f} reads per buffer",
                    std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(),
                    ctx.bytesReceivedInInterval / util::Megabytes(1) * 1.0,
                    ctx.buffersReceivedInInterval,
                    ctx.bytesReceivedInInterval / util::Megabytes(1) * 1.0 /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),
                    ctx.buffersReceivedInInterval /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),
                    ctx.totalBytesReceived / util::Megabytes(1) * 1.0,
                    ctx.totalReads * 1.0 / ctx.totalBuffersReceived);

                ctx.swReport.interval();
                ctx.bytesReceivedInInterval = 0;
                ctx.buffersReceivedInInterval = 0;
            }
        }
        }
    }
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
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
size_t process_data_raw(ClientContext &ctx, std::basic_string_view<u32> data)
{
    if (data.empty())
        return 0;
    return 0;
}

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
size_t process_data_framed(ClientContext &ctx, std::basic_string_view<u32> data)
{
    if (data.size() < 2)
        return 0;

    u32 seqNum = data[0];
    u32 wordsInBuffer = data[1];

    if (ctx.lastSeqNum != -1 && data[0] != static_cast<u32>(ctx.lastSeqNum + 1u))
    {
        spdlog::warn("Buffer loss detected: last seq num {}, current seq num {}", ctx.lastSeqNum,
                     seqNum);
    }

    if (data.size() < 2 + wordsInBuffer)
    {
        // not enough data yet
        return 0;
    }

    std::basic_string_view<u32> rawView = data.substr(2, wordsInBuffer);
    size_t wordsProcessed = process_data_raw(ctx, rawView);

    if (wordsProcessed != wordsInBuffer)
    {
        // This should not happen. The server should guarantee that only complete frames are sent.
        spdlog::error("Format error: framed buffer with seq num {} contains {} words, but only {} "
                      "words could be processed.",
                      seqNum, wordsInBuffer, wordsProcessed);
    }

    return 2 + wordsProcessed;
}

size_t process_data(ClientContext &ctx, std::basic_string_view<u32> data)
{
    if (!ctx.isRawFormat)
        return process_data_framed(ctx, data);
    return process_data_raw(ctx, data);
}
