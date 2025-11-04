#include <argh.h>
#include <asio.hpp>
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>

inline std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

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
    std::vector<std::uint8_t> destBuffer;
    size_t destBufferUsed = 0;
    ssize_t lastSeqNum = -1;
    size_t totalBytesReceived = 0;
    size_t bytesReceivedInInterval = 0;
    size_t buffersReceivedInInterval = 0;
    size_t totalReads = 0;
    size_t totalBuffersReceived = 0;
};

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    argh::parser parser({"-h", "--help", "--log-level", "--tcp", "--ipc", "--raw"});
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
        std::cout << "Usage: " << argv[0] << " [--tcp [host:port]|--ipc [socket_path]] [--raw]"
                  << " [--log-level level][--trace][--debug][--info][--warn]\n";
        return 0;
    }

    std::string tcpHost = "127.0.0.1";
    std::string tcpPort = "42333";
    std::string socketPath = "/tmp/mvme_stream_server.sock";
    bool useRawFormat = false;
    enum class Method
    {
        TCP,
        IPC
    } method = Method::TCP;

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

    if (parser["--raw"])
    {
        useRawFormat = true;
    }

    ClientContext clientState;
    int ret = 0;

    #if 0
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
    #endif
}
