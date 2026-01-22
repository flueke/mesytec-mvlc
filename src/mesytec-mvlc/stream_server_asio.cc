#include "stream_server_asio.h"

#include <asio.hpp>

// Enable local (unix domain) sockets on non-Windows platforms if the toolchain
// claims to support them.
#if defined(ASIO_HAS_LOCAL_SOCKETS) and (not defined(_WIN32))
#define ENABLE_UNIX_DOMAIN_SOCKETS
#include <asio/local/stream_protocol.hpp>
#endif

#include <list>
#include <memory>
#include <mutex>
#include <numeric>
#include <spdlog/spdlog.h>
#include <thread>

using asio::ip::tcp;
using asio::local::stream_protocol;
#define HANDLER_LOCATION ASIO_HANDLER_LOCATION((__FILE__, __LINE__, __func__))

namespace mesytec::mvlc
{

using GenericSocket = asio::generic::stream_protocol::socket;
using GenericEndpoint = asio::generic::stream_protocol::endpoint;

struct GenericSocketAddress
{
    std::string protocol, address, port;
};

GenericSocketAddress make_socket_address(const GenericEndpoint &endpoint)
{
    const auto *data = endpoint.data();
    socklen_t len = endpoint.size();

    switch (data->sa_family)
    {
    case AF_INET:
        return {"tcp4", inet_ntoa(((sockaddr_in *)data)->sin_addr),
                std::to_string(ntohs(((sockaddr_in *)data)->sin_port))};
    case AF_INET6:
        // inet_ntop + port...
        break;
    case AF_UNIX:
        return {"ipc",
                std::string(((sockaddr_un *)data)->sun_path, len - offsetof(sockaddr_un, sun_path)),
                ""};
    }
    return {"unknown", "", ""};
}

std::string to_string(const GenericSocketAddress &addr)
{
    return addr.protocol + "://" + addr.address + (addr.port.empty() ? "" : ":" + addr.port);
}

std::string to_string(const GenericEndpoint &endpoint)
{
    return to_string(make_socket_address(endpoint));
}

// Executor can be an io_context or a strand or similar
template <typename Func, typename Executor> void post_and_wait(Executor &executor, Func &&func)
{
    std::promise<void> promise;
    auto future = promise.get_future();

    asio::post(executor,
               [func = std::forward<Func>(func), &promise]() mutable
               {
                   func();
                   promise.set_value();
               });

    future.wait();
}

struct Client: std::enable_shared_from_this<Client>
{
    GenericSocket socket;

    explicit Client(GenericSocket &&s)
        : socket(std::move(s))
    {
    }
};

struct StreamServer::Private
{
    asio::io_context io_context;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard{
        asio::make_work_guard(io_context)};
    std::thread io_thread;

    std::mutex acceptors_mutex;
    std::unique_ptr<tcp::acceptor> tcp_acceptor;
    std::unique_ptr<stream_protocol::acceptor> ipc_acceptor;

    std::mutex clients_mutex;
    std::list<std::shared_ptr<Client>> clients;

    std::mutex preamble_mutex;
    std::vector<uint8_t> preamble;

    // IOV data is stored by sendToAllClients() to save on allocations.
    std::vector<asio::const_buffer> send_buffers;

    std::atomic<bool> accepting = true;

    void ensure_io_thread_running();
    void stop();

    bool listenTcp(const std::string &host, const std::string &port);
    void startAcceptTcp();
    void handleAcceptTcp(std::shared_ptr<Client> client, const asio::error_code &error);

    bool listenIpc(const std::string &path);
    void startAcceptIpc();
    void handleAcceptIpc(std::shared_ptr<Client> client, const asio::error_code &error);
};

StreamServer::StreamServer()
    : d(std::make_unique<Private>())
{
}

StreamServer::~StreamServer() { d->stop(); }

void StreamServer::Private::ensure_io_thread_running()
{
    if (!io_thread.joinable())
    {
        io_thread = std::thread(
            [this]()
            {
                spdlog::debug("StreamServer IO context thread started");
                io_context.run();
                spdlog::debug("StreamServer IO context thread stopped");
            });
    }
}

void StreamServer::Private::stop()
{
    HANDLER_LOCATION;

    accepting = false;

    spdlog::debug("posting acceptor close");
    post_and_wait(io_context,
                  [this]()
                  {
                      HANDLER_LOCATION;
                      spdlog::debug("in posted acceptor close");
                      asio::error_code ec;
                      std::lock_guard<std::mutex> lock(acceptors_mutex);
                      if (tcp_acceptor && tcp_acceptor->is_open())
                      {
                          tcp_acceptor->cancel(ec);
                          tcp_acceptor->close(ec);
                          tcp_acceptor.reset();
                      }

                      if (ipc_acceptor && ipc_acceptor->is_open())
                      {
                          ipc_acceptor->cancel(ec);
                          ipc_acceptor->close(ec);
                          ipc_acceptor.reset();
                      }
                      spdlog::debug("end of posted acceptor close");
                  });

    spdlog::debug("posting client close");
    post_and_wait(io_context,
                  [this]()
                  {
                      HANDLER_LOCATION;
                      spdlog::debug("in posted client close");
                      asio::error_code ec;
                      std::lock_guard<std::mutex> lock(clients_mutex);
                      for (auto &client: clients)
                      {
                          client->socket.close(ec);
                      }
                      clients.clear();
                      spdlog::debug("end of posted client close");
                  });

    spdlog::debug("stopping io_context");
    io_context.stop();
    spdlog::debug("resetting work guard");
    work_guard.reset();

    spdlog::debug("joining io_context thread");
    if (io_thread.joinable())
    {
        io_thread.join();
    }
}

bool StreamServer::listen(const std::string &url)
{
    if (url.find("tcp://") == 0 || url.find("tcp4://") == 0 || url.find("tcp6://") == 0)
    {
        // Parse url: tcp://host:port or tcp4://host:port or tcp6://host:port
        std::string scheme, host_port;
        size_t scheme_end = url.find("://");

        if (scheme_end == std::string::npos)
            return false;

        scheme = url.substr(0, scheme_end);
        host_port = url.substr(scheme_end + 3);

        size_t colon = host_port.rfind(':');
        if (colon == std::string::npos)
            return false;

        std::string host = host_port.substr(0, colon);
        std::string port = host_port.substr(colon + 1);

        if (host.empty() || host == "*")
            host = "0.0.0.0";

        return d->listenTcp(host, port);
    }
    else if (url.find("ipc://") == 0)
    {
        std::string path = url.substr(6); // Remove "ipc://" prefix
        return d->listenIpc(path);
    }
    else if (url.find("unix://") == 0)
    {
        std::string path = url.substr(7); // Remove "unix://" prefix
        return d->listenIpc(path);
    }
    else if (auto colon_pos = url.rfind(':'); colon_pos != std::string::npos)
    {
        // Assume host:port format
        std::string host = url.substr(0, colon_pos);
        std::string port = url.substr(colon_pos + 1);

        if (host.empty() || host == "*")
            host = "127.0.0.1";

        return d->listenTcp(host, port);
    }

    return false;
}

bool StreamServer::isListening() const
{
    std::lock_guard<std::mutex> lock(d->acceptors_mutex);
    return (d->tcp_acceptor && d->tcp_acceptor->is_open()) ||
           (d->ipc_acceptor && d->ipc_acceptor->is_open());
}

bool StreamServer::stop()
{
    if (isListening())
    {
        d->stop();
        return true;
    }
    return false;
}

size_t StreamServer::clientCount() const
{
    std::lock_guard<std::mutex> lock(d->clients_mutex);
    return d->clients.size();
}

std::vector<std::string> StreamServer::clientAddresses() const
{
    std::vector<std::string> addresses;
    std::lock_guard<std::mutex> lock(d->clients_mutex);

    for (const auto &client: d->clients)
    {
        try
        {
            auto addr_str = to_string(client->socket.remote_endpoint());
            // Unix domain sockets do not return a useful string for
            // remote_endpoint() so just use the local endpoint here.
            if (addr_str == "ipc://")
                addr_str = to_string(client->socket.local_endpoint());

            if (addr_str.empty())
                addr_str = "<unknown>";

            addresses.push_back(addr_str);
        }
        catch (...)
        {
            addresses.push_back("<unknown>");
        }
    }

    return addresses;
}

template <typename Acceptor> std::string get_local_address(Acceptor &acceptor)
{
    try
    {
        return to_string(acceptor.local_endpoint());
    }
    catch (...)
    {
        return "<unknown>";
    }
}

std::vector<std::string> StreamServer::listenAddresses() const
{
    std::lock_guard<std::mutex> lock(d->acceptors_mutex);
    std::vector<std::string> addresses;

    if (d->tcp_acceptor && d->tcp_acceptor->is_open())
    {
        addresses.push_back(get_local_address(*d->tcp_acceptor));
    }

    if (d->ipc_acceptor && d->ipc_acceptor->is_open())
    {
        addresses.push_back(get_local_address(*d->ipc_acceptor));
    }

    return addresses;
}

size_t StreamServer::sendToAllClients(const IOV *iov, size_t n_iov)
{
    HANDLER_LOCATION;

    // Shared state for tracking async operations
    struct SendState
    {
        std::mutex mutex;
        std::condition_variable cv;
        size_t pending = 0;
        size_t successful = 0;
        std::vector<std::shared_ptr<Client>> failed_clients;
    };

    auto state = std::make_shared<SendState>();

    // Start async writes to all clients
    {
        std::lock_guard<std::mutex> lock(d->clients_mutex);

        d->send_buffers.resize(n_iov);
        std::transform(iov, iov + n_iov, d->send_buffers.begin(),
                       [](const IOV &entry) { return asio::buffer(entry.buf, entry.len); });

        for (auto &client: d->clients)
        {
            state->pending++;

            // auto buffer = asio::buffer(data, size);
            asio::async_write(
                client->socket, d->send_buffers,
                [state, client](const asio::error_code &ec, std::size_t /*bytes_transferred*/)
                {
                    HANDLER_LOCATION;
                    std::lock_guard<std::mutex> lock(state->mutex);

                    if (ec)
                    {
                        // Mark client for removal
                        state->failed_clients.push_back(client);

                        try
                        {
                            spdlog::warn("Error sending to client '{}', will disconnect: {}",
                                         to_string(client->socket.remote_endpoint()), ec.message());
                        }
                        catch (const std::exception &e)
                        {
                            spdlog::warn("Error sending to unknown client, will disconnect: {}",
                                         e.what());
                        }
                        catch (...)
                        {
                            spdlog::warn("Error sending to unknown client, will disconnect: {}",
                                         ec.message());
                        }
                    }
                    else
                    {
                        state->successful++;
                    }

                    state->pending--;
                    if (state->pending == 0)
                    {
                        state->cv.notify_one();
                    }
                });
        }
    }

    // Wait for all async writes to complete
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait(lock, [&state]() { return state->pending == 0; });
    }

    // Clean up failed clients
    if (!state->failed_clients.empty())
    {
        std::lock_guard<std::mutex> lock(d->clients_mutex);

        for (auto &failed_client: state->failed_clients)
        {
            asio::error_code close_ec;
            failed_client->socket.close(close_ec);

            d->clients.remove(failed_client);
        }
    }

    return state->failed_clients.size();
}

void StreamServer::setPreamble(const IOV *iov, size_t n_iov)
{
    std::lock_guard<std::mutex> lock(d->preamble_mutex);

    auto totalSize =
        std::accumulate(iov, iov + n_iov, 0ul,
                        [](unsigned long accu, const IOV &entry) { return accu + entry.len; });

    d->preamble.resize(totalSize);
    for (size_t i = 0, offset = 0; i < n_iov; ++i)
    {
        std::memcpy(d->preamble.data() + offset, iov[i].buf, iov[i].len);
        offset += iov[i].len;
    }

    spdlog::debug("Preamble set, size: {} bytes", d->preamble.size());
}

bool StreamServer::Private::listenTcp(const std::string &host, const std::string &port)
{
    try
    {
        {
            std::lock_guard<std::mutex> lock(acceptors_mutex);
            if (tcp_acceptor && tcp_acceptor->is_open())
            {
                spdlog::warn("StreamServer is already listening on a TCP address");
                return false;
            }
            spdlog::debug("Attempting to listen on TCP {}:{}", host, port);
            auto address = asio::ip::make_address(host);
            unsigned short port_num = static_cast<unsigned short>(std::stoi(port));
            tcp::endpoint endpoint(address, port_num);

            tcp_acceptor = std::make_unique<tcp::acceptor>(io_context, endpoint);
        }

        HANDLER_LOCATION;
        startAcceptTcp();
        ensure_io_thread_running();
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to listen on TCP {}:{}: {}", host, port, e.what());
        return false;
    }
}

void StreamServer::Private::startAcceptTcp()
{
    HANDLER_LOCATION;

    if (!accepting)
        return;

    auto client = std::make_shared<Client>(GenericSocket(io_context));

    std::lock_guard<std::mutex> lock(acceptors_mutex);
    if (tcp_acceptor && tcp_acceptor->is_open())
    {
        tcp_acceptor->async_accept(client->socket, [this, client](const asio::error_code &error)
                                   { handleAcceptTcp(client, error); });
    }
}

void StreamServer::Private::handleAcceptTcp(std::shared_ptr<Client> client,
                                            const asio::error_code &error)
{
    HANDLER_LOCATION;
    if (!error)
    {
        try
        {
            spdlog::info("Client connected from {}, to {}",
                         to_string(client->socket.remote_endpoint()),
                         to_string(client->socket.local_endpoint()));

            // Send preamble if set (synchronously, before adding to client list)
            std::vector<uint8_t> preamble_copy;
            {
                std::lock_guard<std::mutex> lock(preamble_mutex);
                preamble_copy = preamble;
            }

            if (!preamble_copy.empty())
            {
                asio::error_code ec;
                asio::write(client->socket, asio::buffer(preamble_copy), ec);

                if (ec)
                {
                    spdlog::warn("Failed to send preamble to client {}, disconnecting: {}",
                                 to_string(client->socket.remote_endpoint()), ec.message());
                    asio::error_code close_ec;
                    client->socket.close(close_ec);

                    // Continue accepting new connections
                    startAcceptTcp();
                    return;
                }

                spdlog::debug("Sent preamble ({} bytes) to client {}", preamble_copy.size(),
                              to_string(client->socket.remote_endpoint()));
            }

            // Only add client to list after successful preamble send (or if no preamble)
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.emplace_back(client);
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("Error handling new client: {}", e.what());
        }

        // Continue accepting new connections
        startAcceptTcp();
    }
    else
    {
        if (error != asio::error::operation_aborted)
        {
            spdlog::error("Accept error: {}", error.message());

            // Continue accepting if acceptor is still valid
            if (tcp_acceptor && tcp_acceptor->is_open())
            {
                startAcceptTcp();
            }
        }
    }
}

bool StreamServer::Private::listenIpc(const std::string &path)
{
#ifndef ENABLE_UNIX_DOMAIN_SOCKETS
    spdlog::error("Unix domain sockets are not supported on this platform");
    return false;
#else
    try
    {
        {
            std::lock_guard<std::mutex> lock(acceptors_mutex);
            if (ipc_acceptor && ipc_acceptor->is_open())
            {
                spdlog::warn("StreamServer is already listening on a unix domain socket");
                return false;
            }

            struct stat st;
            if (stat(path.c_str(), &st) == 0)
            {
                if (S_ISSOCK(st.st_mode))
                {
                    try
                    {
                        asio::local::stream_protocol::socket tmp_socket(io_context);
                        tmp_socket.connect(asio::local::stream_protocol::endpoint(path));
                        spdlog::warn("StreamServer: socket file {} already exists and is in use",
                                     path);
                        return false;
                    }
                    catch (...)
                    {
                        spdlog::debug(
                            "StreamServer: socket file {} exists but is not in use, removing",
                            path);
                        ::unlink(path.c_str());
                    }
                }
            }

            spdlog::debug("Attempting to listen on unix domain socket {}", path);
            ipc_acceptor = std::make_unique<stream_protocol::acceptor>(
                io_context, stream_protocol::endpoint(path));
        }
        HANDLER_LOCATION;
        startAcceptIpc();
        ensure_io_thread_running();
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Error starting unix domain socket server on {}: {}", path, e.what());
        return false;
    }

    return false;
#endif
}

void StreamServer::Private::startAcceptIpc()
{
    HANDLER_LOCATION;

    if (!accepting)
        return;

    auto client = std::make_shared<Client>(GenericSocket(io_context));

    std::lock_guard<std::mutex> lock(acceptors_mutex);
    if (ipc_acceptor && ipc_acceptor->is_open())
    {
        ipc_acceptor->async_accept(client->socket, [this, client](const asio::error_code &error)
                                   { handleAcceptIpc(client, error); });
    }
}

void StreamServer::Private::handleAcceptIpc(std::shared_ptr<Client> client,
                                            const asio::error_code &error)
{
    HANDLER_LOCATION;
    if (!error)
    {
        try
        {
            spdlog::info("Client connected from {}, to {}",
                         to_string(client->socket.remote_endpoint()),
                         to_string(client->socket.local_endpoint()));

            // Send preamble if set (synchronously, before adding to client list)
            std::vector<uint8_t> preamble_copy;
            {
                std::lock_guard<std::mutex> lock(preamble_mutex);
                preamble_copy = preamble;
            }

            if (!preamble_copy.empty())
            {
                asio::error_code ec;
                asio::write(client->socket, asio::buffer(preamble_copy), ec);

                if (ec)
                {
                    spdlog::warn("Failed to send preamble to client {}, disconnecting: {}",
                                 to_string(client->socket.remote_endpoint()), ec.message());
                    asio::error_code close_ec;
                    client->socket.close(close_ec);

                    // Continue accepting new connections
                    startAcceptTcp();
                    return;
                }

                spdlog::debug("Sent preamble ({} bytes) to client {}", preamble_copy.size(),
                              to_string(client->socket.remote_endpoint()));
            }

            // Only add client to list after successful preamble send (or if no preamble)
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.emplace_back(client);
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("Error handling new client: {}", e.what());
        }

        // Continue accepting new connections
        startAcceptIpc();
    }
    else
    {
        if (error != asio::error::operation_aborted)
        {
            spdlog::error("Accept error: {}", error.message());

            // Continue accepting if acceptor is still valid
            if (ipc_acceptor && ipc_acceptor->is_open())
            {
                startAcceptIpc();
            }
        }
    }
}

} // namespace mesytec::mvlc
