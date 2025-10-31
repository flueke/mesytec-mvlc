#include <algorithm>
#include <iomanip>
#include <iostream>

#include <asio.hpp>

#include "mvlc_stream_test_support.h"

template <typename Sock, typename ReconnectFun>
int run_client(Sock &socket, ReconnectFun reconnect);

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

int main(int argc, char *argv[])
{
    std::string tcpHost = "localhost";
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

    int ret = 0;

    try
    {
        asio::io_context io_context;

        switch (method)
        {
        case Method::IPC:
        {
            asio::local::stream_protocol::socket socket(io_context);
            ret = run_client(socket,
                             std::bind(reconnect_ipc, std::ref(socket), std::ref(socketPath)));
        }
        break;
        case Method::TCP:
        {
            asio::ip::tcp::socket socket(io_context);
            ret = run_client(socket, std::bind(reconnect_tcp, std::ref(socket), std::ref(tcpHost),
                                               std::ref(tcpPort)));
        }
        break;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }
}

template <typename Sock, typename ReconnectFun> int run_client(Sock &socket, ReconnectFun reconnect)
{
    enum class State
    {
        Connecting,
        Connected,
    } state = State::Connecting;

    std::vector<u8> destBuffer;
    ssize_t lastSeqNum = -1;
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
                lastSeqNum = -1;
            }
            break;

        case State::Connected:
        {

            destBuffer.resize(sizeof(TestBuffer));
            auto bytesRead =
                asio::read(socket, asio::buffer(destBuffer.data(), destBuffer.size()), ec);

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
                    spdlog::error("Error while reading from server: {}", ec.message());
                }
                state = State::Connecting;
            }
            else
            {
                if (bytesRead != destBuffer.size())
                {
                    spdlog::error("Connection closed while reading header");
                    return 1;
                }

                auto testBuffer = reinterpret_cast<const TestBuffer *>(destBuffer.data());

                spdlog::trace("Received TestBuffer header: magic={:#010x}, sequence_number={}, "
                              "buffer_size={} "
                              "words ({} bytes)",
                              testBuffer->magic, testBuffer->sequence_number,
                              testBuffer->buffer_size, testBuffer->buffer_size * sizeof(u32));

                if (lastSeqNum != -1 &&
                    testBuffer->sequence_number != static_cast<u32>(lastSeqNum + 1))
                {
                    spdlog::warn("Buffer loss detected: last seq num {}, current seq num {}",
                                 lastSeqNum, testBuffer->sequence_number);
                    return 1;
                }
                lastSeqNum = testBuffer->sequence_number;

                size_t bytesToRead = testBuffer->buffer_size * sizeof(u32);

                destBuffer.resize(sizeof(TestBuffer) + bytesToRead);
                // have to update the pointer after resize!
                testBuffer = reinterpret_cast<const TestBuffer *>(destBuffer.data());

                bytesRead = asio::read(
                    socket, asio::buffer(destBuffer.data() + sizeof(TestBuffer), bytesToRead));

                if (bytesRead != bytesToRead)
                {
                    spdlog::error("Connection closed while reading data");
                    return 1;
                }

                spdlog::trace("Read {} bytes of data for buffer {}", bytesRead,
                              testBuffer->sequence_number);

#if 1
                if (!verify_test_data(destBuffer, testBuffer->sequence_number))
                {
                    spdlog::error("Data verification failed for buffer {}",
                                  testBuffer->sequence_number);
                    break;
                }
#endif
            }
        }
        }
    }
    return 0;
}
