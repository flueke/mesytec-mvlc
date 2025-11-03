#include <algorithm>
#include <iomanip>
#include <iostream>

#include <asio.hpp>

#include "mesytec-mvlc/mvlc_stream_test_support.h"
#include <mesytec-mvlc/util/stopwatch.h>
#include <mesytec-mvlc/util/storage_sizes.h>

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
    // asio::ip::tcp::socket socket(socket.get_executor().context());
    asio::connect(socket, endpoints, ec);
    return ec;
}

struct ClientState
{
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

template <typename Sock, typename ReconnectFun>
int run_client(Sock &socket, ReconnectFun reconnect, ClientState &clientState);

int main(int argc, char *argv[])
{
    std::string tcpHost = "127.0.0.1";
    std::string tcpPort = "42333";
    std::string socketPath = "/tmp/mvlc_stream_test_server.ipc";
    enum class Method
    {
        TCP,
        IPC
    } method = Method::TCP;

    spdlog::set_level(spdlog::level::info);
    argh::parser parser({"-h", "--help", "--log-level", "--tcp", "--ipc"});
    parser.parse(argc, argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
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
        std::cout << "Usage: " << argv[0] << " [--tcp [host:port]|--ipc [socket_path]]"
                  << " [--log-level level][--trace][--debug][--info][--warn]\n";
        return 0;
    }

    std::string tmp;

    if (parser["--ipc"] || parser("--ipc") >> tmp)
    {
        if (!tmp.empty())
        {
            socketPath = tmp;
        }
        method = Method::IPC;
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
        method = Method::TCP;
    }

    ClientState clientState;
    int ret = 0;

    try
    {
        asio::io_context io_context;

        switch (method)
        {
        case Method::IPC:
        {
            asio::local::stream_protocol::socket socket(io_context);
            ret =
                run_client(socket, std::bind(reconnect_ipc, std::ref(socket), std::ref(socketPath)),
                           clientState);
        }
        break;
        case Method::TCP:
        {
            asio::ip::tcp::socket socket(io_context);
            ret = run_client(
                socket,
                std::bind(reconnect_tcp, std::ref(socket), std::ref(tcpHost), std::ref(tcpPort)),
                clientState);
        }
        break;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }
}

template <typename Sock, typename ReconnectFun>
int run_client(Sock &socket, ReconnectFun reconnect, ClientState &clientState)
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
            if (ec = reconnect())
            {
                spdlog::warn("Failed to connect to server: {}", ec.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            else
            {
                state = State::Connected;
                spdlog::info("Connected to server");
                clientState.lastSeqNum = -1;
                clientState.destBufferUsed = 0;
            }
            break;

        case State::Connected:
        {
            clientState.destBuffer.resize(util::Megabytes(2));
            size_t bytesRead = 0;

            while (clientState.destBufferUsed < sizeof(TestBuffer) && !ec)
            {
                auto dest =
                    asio::buffer(clientState.destBuffer.data() + clientState.destBufferUsed,
                                 clientState.destBuffer.size() - clientState.destBufferUsed);

                bytesRead = asio::read(socket, dest, ec);

                if (!ec)
                {
                    clientState.destBufferUsed += bytesRead;
                    clientState.totalBytesReceived += bytesRead;
                    clientState.bytesReceivedInInterval += bytesRead;
                    clientState.totalReads += 1;
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

            auto testBuffer = reinterpret_cast<const TestBuffer *>(clientState.destBuffer.data());

            spdlog::trace("Received TestBuffer header: magic={:#010x}, sequence_number={}, "
                          "buffer_size={} "
                          "words ({} bytes)",
                          testBuffer->magic, testBuffer->sequence_number, testBuffer->buffer_size,
                          testBuffer->buffer_size * sizeof(u32));

            if (testBuffer->magic != MAGIC_PATTERN)
            {
                spdlog::error("Invalid magic pattern in received buffer: {:#010x}",
                              testBuffer->magic);
                state = State::Connecting;
                break;
            }

            if (clientState.lastSeqNum != -1 &&
                testBuffer->sequence_number != static_cast<u32>(clientState.lastSeqNum + 1u))
            {
                spdlog::warn("Buffer loss detected: last seq num {}, current seq num {}",
                             clientState.lastSeqNum, testBuffer->sequence_number);
            }

            clientState.lastSeqNum = testBuffer->sequence_number;

            size_t totalBytesNeeded = sizeof(TestBuffer) + testBuffer->buffer_size * sizeof(u32);
            clientState.destBuffer.resize(
                std::max(totalBytesNeeded, clientState.destBuffer.size()));
            // have to update the pointer to the header after resizing!
            testBuffer = reinterpret_cast<const TestBuffer *>(clientState.destBuffer.data());

            while (clientState.destBufferUsed < totalBytesNeeded && !ec)
            {
                auto dest =
                    asio::buffer(clientState.destBuffer.data() + clientState.destBufferUsed,
                                 clientState.destBuffer.size() - clientState.destBufferUsed);

                bytesRead = asio::read(socket, dest, ec);

                if (!ec)
                {
                    clientState.destBufferUsed += bytesRead;
                    clientState.totalBytesReceived += bytesRead;
                    clientState.bytesReceivedInInterval += bytesRead;
                    clientState.totalReads += 1;
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

            ++clientState.buffersReceivedInInterval;
            ++clientState.totalBuffersReceived;

            std::basic_string_view<u8> bufferView(clientState.destBuffer.data(),
                                                  sizeof(TestBuffer) +
                                                      testBuffer->buffer_size * sizeof(u32));

#if 1
            if (!verify_test_data(bufferView, testBuffer->sequence_number))
            {
                spdlog::error("Data verification failed for buffer {}",
                              testBuffer->sequence_number);
            }
#endif

            // Move any remaining data to the front of the buffer
            if (clientState.destBufferUsed > totalBytesNeeded)
            {
                size_t remaining = clientState.destBufferUsed - totalBytesNeeded;
                std::memmove(clientState.destBuffer.data(),
                             clientState.destBuffer.data() + totalBytesNeeded, remaining);
                clientState.destBufferUsed = remaining;
            }
            else
            {
                clientState.destBufferUsed = 0;
            }

            if (auto interval = clientState.swReport.get_interval();
                interval >= std::chrono::seconds(1))
            {
                spdlog::info(
                    "Received in the last {} ms: {:.2f} MB ({} buffers), rate={:.2f} MB/s ({:.2f} "
                    "buffers/s) (total {} MB received), avg {:.2f} reads per buffer",
                    std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(),
                    clientState.bytesReceivedInInterval / util::Megabytes(1) * 1.0,
                    clientState.buffersReceivedInInterval,
                    clientState.bytesReceivedInInterval / util::Megabytes(1) * 1.0 /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),
                    clientState.buffersReceivedInInterval /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),
                    clientState.totalBytesReceived / util::Megabytes(1) * 1.0,
                    clientState.totalReads * 1.0 / clientState.totalBuffersReceived);

                clientState.swReport.interval();
                clientState.bytesReceivedInInterval = 0;
                clientState.buffersReceivedInInterval = 0;
            }
        }
        }
    }
    return 0;
}
