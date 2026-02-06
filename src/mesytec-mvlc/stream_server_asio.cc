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
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "util/logging.h"

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

bool is_tcp_socket(GenericSocket &socket)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(socket.native_handle(), (struct sockaddr *)&addr, &len) == 0)
    {
        return addr.ss_family == AF_INET || addr.ss_family == AF_INET6;
    }

    return false;
}

GenericEndpoint get_source_endpoint(GenericSocket &socket)
{
    if (is_tcp_socket(socket))
    {
        return socket.remote_endpoint();
    }

    // remote_endpoint does not contain anything useful for local ipc sockets
    return socket.local_endpoint();
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
    std::shared_ptr<spdlog::logger> logger;

    asio::io_context io_context;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard;
    std::thread io_thread;

    std::mutex acceptors_mutex;
    std::vector<std::unique_ptr<tcp::acceptor>> tcp_acceptors;
#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
    std::vector<std::unique_ptr<stream_protocol::acceptor>> ipc_acceptors;
#endif

    std::mutex clients_mutex;
    std::list<std::shared_ptr<Client>> clients;

    std::mutex preamble_mutex;
    std::vector<uint8_t> preamble;

    // IOV data is stored by sendToAllClients() to save on allocations.
    std::vector<asio::const_buffer> send_buffers;

    // Controls the acceptor loops and sendToAllClients(). If set to false no
    // new async accepts will be enqueued.
    std::atomic<bool> accepting = true;
    // Controls the shutdown sequence and state. If this is set to new acceptors
    // will be created, no new clients will be accepted and existing clients
    // will be disconnected.
    std::atomic<bool> in_shutdown = false;

    void ensure_asio_running();
    void stop();

    bool listenTcp(const std::string &host, const std::string &port);
    void startAcceptTcp(tcp::acceptor *acceptor);
    void handleAcceptTcp(std::shared_ptr<Client> client, tcp::acceptor *acceptor,
                         const asio::error_code &error);

    bool listenIpc(const std::string &path);
    void startAcceptIpc(stream_protocol::acceptor *acceptor);
    void handleAcceptIpc(std::shared_ptr<Client> client, stream_protocol::acceptor *acceptor,
                         const asio::error_code &error);
};

StreamServer::StreamServer()
    : d(std::make_unique<Private>())
{
    d->logger = get_logger("mvlc_stream_server_asio");
    d->logger->set_level(spdlog::level::trace);
}

StreamServer::~StreamServer()
{
    d->logger->trace("entering ~StreamServer()");
    d->stop();
    d->logger->trace("leaving ~StreamServer()");
}

void StreamServer::Private::ensure_asio_running()
{
    logger->trace("entering ensure_asio_running()");
    if (io_context.stopped())
    {
        logger->trace("  ensure_asio_running(): io_context was stopped, restarting");
        io_context.restart();
    }

    if (!work_guard.has_value())
    {
        logger->trace("  ensure_asio_running(): (re)creating work_guard");
        work_guard.emplace(asio::make_work_guard(io_context));
    }

    if (!io_thread.joinable())
    {
        logger->trace(" ensure_asio_running(): starting io_context thread");
        io_thread = std::thread(
            [this]()
            {
                #ifdef __linux__
                    prctl(PR_SET_NAME,"stream_server_asio_io",0,0,0);
                #endif
                logger->trace("StreamServer IO context thread started");
                io_context.run();
                logger->trace("StreamServer IO context thread stopped");
            });
    }
    logger->trace("leaving ensure_asio_running()");
}

void StreamServer::Private::stop()
{
    HANDLER_LOCATION;
    logger->trace("entering StreamServer::Private::stop()");
    accepting = false;
    in_shutdown = true;

    // At this point the io_context has to run otherwise the posts will not
    // return. Attempt to restart it in case it's stopped already, e.g. through
    // multiple calls to stop() in a row. This might cause useless thread
    // creation but I don't care right now.  An alternative would be to post
    // stuff, then run the io_context directly in this thread but then there are
    // two cases to handle: the io thread itself and this thread.
    ensure_asio_running();

    logger->trace("posting acceptor close");
    post_and_wait(io_context,
                  [this]()
                  {
                      HANDLER_LOCATION;
                      logger->trace("in posted acceptor close");
                      asio::error_code ec;
                      std::lock_guard<std::mutex> lock(acceptors_mutex);

                      for (auto &tcp_acceptor: tcp_acceptors)
                      {
                          if (tcp_acceptor && tcp_acceptor->is_open())
                          {
                              tcp_acceptor->cancel(ec);
                              tcp_acceptor->close(ec);
                          }
                      }
                      tcp_acceptors.clear();

#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
                      for (auto &ipc_acceptor: ipc_acceptors)
                      {
                          if (ipc_acceptor && ipc_acceptor->is_open())
                          {
                              ipc_acceptor->cancel(ec);
                              ipc_acceptor->close(ec);
                          }
                      }
                      ipc_acceptors.clear();
#endif
                      logger->trace("end of posted acceptor close");
                  });

    logger->trace("posting client close");
    post_and_wait(io_context,
                  [this]()
                  {
                      HANDLER_LOCATION;
                      logger->trace("in posted client close");
                      asio::error_code ec;
                      std::lock_guard<std::mutex> lock(clients_mutex);

                      for (auto &client: clients)
                      {
                          client->socket.close(ec);
                      }
                      clients.clear();
                      logger->trace("end of posted client close");
                  });

    logger->trace("stopping io_context");
    io_context.stop();
    logger->trace("resetting work guard");
    work_guard.reset();

    logger->trace("joining io_context thread");
    if (io_thread.joinable())
    {
        io_thread.join();
    }
    in_shutdown = false;
    logger->trace("leaving StreamServer::Private::stop()");
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
#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
    return !d->tcp_acceptors.empty() || !d->ipc_acceptors.empty();
#else
    return !d->tcp_acceptors.empty();
#endif
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

    for (auto &tcp_acceptor: d->tcp_acceptors)
    {
        if (tcp_acceptor && tcp_acceptor->is_open())
        {
            addresses.push_back(get_local_address(*tcp_acceptor));
        }
    }

#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
    for (auto &ipc_acceptor: d->ipc_acceptors)
    {
        if (ipc_acceptor && ipc_acceptor->is_open())
        {
            addresses.push_back(get_local_address(*ipc_acceptor));
        }
    }
#endif

    return addresses;
}

size_t StreamServer::sendToAllClients(const IOV *iov, size_t n_iov)
{
    HANDLER_LOCATION;

    if (!d->accepting)
    {
        d->logger->warn("sendToAllClients() called while not accepting, aborting send");
        return std::numeric_limits<size_t>::max();
    }

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
                [this, state, client](const asio::error_code &ec, std::size_t /*bytes_transferred*/)
                {
                    HANDLER_LOCATION;
                    std::lock_guard<std::mutex> lock(state->mutex);

                    if (ec)
                    {
                        // Mark client for removal
                        state->failed_clients.push_back(client);

                        try
                        {
                            d->logger->warn("Error sending to client '{}', will disconnect: {}",
                                            to_string(get_source_endpoint(client->socket)),
                                            ec.message());
                        }
                        catch (const std::exception &e)
                        {
                            d->logger->warn("Error sending to unknown client, will disconnect: {}",
                                            e.what());
                        }
                        catch (...)
                        {
                            d->logger->warn("Error sending to unknown client, will disconnect: {}",
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

    // TODO: could implement a timeout here and cancel pending operations if
    // needed. alternatively an asio deadline timer could be used i guess.

    // Wait for all async writes to complete. This is where we block.
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
    // Create a copy of the preamble data to be sent to new clients.
    // Then immediately send the new preamble to all existing clients.

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

    d->logger->trace("Preamble set, size: {} bytes, sending to {} clients", d->preamble.size(), clientCount());
    sendToAllClients(iov, n_iov);
}

std::vector<std::uint8_t> StreamServer::getPreamble() const
{
    std::lock_guard<std::mutex> lock(d->preamble_mutex);
    return d->preamble;
}

bool StreamServer::Private::listenTcp(const std::string &host, const std::string &port)
{
    if (in_shutdown)
        return false;

    try
    {
        tcp::acceptor *the_acceptor = nullptr;
        {
            std::lock_guard<std::mutex> lock(acceptors_mutex);
            logger->trace("Attempting to listen on TCP {}:{}", host, port);
            auto address = asio::ip::make_address(host);
            unsigned short port_num = static_cast<unsigned short>(std::stoi(port));
            tcp::endpoint endpoint(address, port_num);

            tcp_acceptors.emplace_back(std::make_unique<tcp::acceptor>(io_context, endpoint));
            the_acceptor = tcp_acceptors.back().get();
        }

        HANDLER_LOCATION;
        accepting = true;
        startAcceptTcp(the_acceptor);
        ensure_asio_running();
        return true;
    }
    catch (const std::exception &e)
    {
        logger->error("Failed to listen on TCP {}:{}: {}", host, port, e.what());
        return false;
    }
}

void StreamServer::Private::startAcceptTcp(tcp::acceptor *tcp_acceptor)
{
    HANDLER_LOCATION;

    if (!accepting)
        return;

    auto client = std::make_shared<Client>(GenericSocket(io_context));

    std::lock_guard<std::mutex> lock(acceptors_mutex);
    if (tcp_acceptor && tcp_acceptor->is_open())
    {
        tcp_acceptor->async_accept(client->socket,
                                   [this, client, tcp_acceptor](const asio::error_code &error)
                                   { handleAcceptTcp(client, tcp_acceptor, error); });
    }
}

void StreamServer::Private::handleAcceptTcp(std::shared_ptr<Client> client,
                                            tcp::acceptor *tcp_acceptor,
                                            const asio::error_code &error)
{
    HANDLER_LOCATION;
    if (!error)
    {
        try
        {
            spdlog::info("Client connected from {}, to {}",
                         to_string(get_source_endpoint(client->socket)),
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
                    logger->warn("Failed to send preamble to client {}, disconnecting: {}",
                                 to_string(get_source_endpoint(client->socket)), ec.message());
                    asio::error_code close_ec;
                    client->socket.close(close_ec);

                    // Continue accepting new connections
                    startAcceptTcp(tcp_acceptor);
                    return;
                }

                logger->trace("Sent preamble ({} bytes) to client {}", preamble_copy.size(),
                              to_string(get_source_endpoint(client->socket)));
            }

            // Only add client to list after successful preamble send (or if no preamble)
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.emplace_back(client);
            }
        }
        catch (const std::exception &e)
        {
            logger->error("Error handling new client: {}", e.what());
        }

        // Continue accepting new connections
        startAcceptTcp(tcp_acceptor);
    }
    else
    {
        if (error != asio::error::operation_aborted)
        {
            logger->error("Accept error: {}", error.message());
            startAcceptTcp(tcp_acceptor);
        }
    }
}

bool StreamServer::Private::listenIpc(const std::string &path)
{
#ifndef ENABLE_UNIX_DOMAIN_SOCKETS
    logger->error("Unix domain sockets are not supported on this platform");
    return false;
#else

    if (in_shutdown)
        return false;

    try
    {
        stream_protocol::acceptor *the_acceptor = nullptr;

        {
            std::lock_guard<std::mutex> lock(acceptors_mutex);
            logger->trace("Attempting to listen on unix domain socket {}", path);

            struct stat st;
            if (stat(path.c_str(), &st) == 0)
            {
                if (S_ISSOCK(st.st_mode))
                {
                    try
                    {
                        asio::local::stream_protocol::socket tmp_socket(io_context);
                        tmp_socket.connect(asio::local::stream_protocol::endpoint(path));
                        logger->warn("StreamServer: socket file {} already exists and is in use",
                                     path);
                        return false;
                    }
                    catch (...)
                    {
                        logger->trace(
                            "StreamServer: socket file {} exists but is not in use, removing",
                            path);
                        ::unlink(path.c_str());
                    }
                }
            }

            ipc_acceptors.emplace_back(std::make_unique<stream_protocol::acceptor>(
                io_context, stream_protocol::endpoint(path)));
            the_acceptor = ipc_acceptors.back().get();
        }

        HANDLER_LOCATION;
        accepting = true;
        startAcceptIpc(the_acceptor);
        ensure_asio_running();
        return true;
    }
    catch (const std::exception &e)
    {
        logger->error("Error starting unix domain socket server on {}: {}", path, e.what());
        return false;
    }

    return false;
#endif
}

#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
void StreamServer::Private::startAcceptIpc(stream_protocol::acceptor *ipc_acceptor)
{
    HANDLER_LOCATION;

    if (!accepting)
        return;

    auto client = std::make_shared<Client>(GenericSocket(io_context));

    std::lock_guard<std::mutex> lock(acceptors_mutex);
    if (ipc_acceptor && ipc_acceptor->is_open())
    {
        ipc_acceptor->async_accept(client->socket,
                                   [this, client, ipc_acceptor](const asio::error_code &error)
                                   { handleAcceptIpc(client, ipc_acceptor, error); });
    }
}

void StreamServer::Private::handleAcceptIpc(std::shared_ptr<Client> client,
                                            stream_protocol::acceptor *ipc_acceptor,
                                            const asio::error_code &error)
{
    HANDLER_LOCATION;
    if (!error)
    {
        try
        {
            spdlog::info("Client connected from {}, to {}",
                         to_string(get_source_endpoint(client->socket)),
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
                    logger->warn("Failed to send preamble to client {}, disconnecting: {}",
                                 to_string(get_source_endpoint(client->socket)), ec.message());
                    asio::error_code close_ec;
                    client->socket.close(close_ec);

                    // Continue accepting new connections
                    startAcceptIpc(ipc_acceptor);
                    return;
                }

                logger->trace("Sent preamble ({} bytes) to client {}", preamble_copy.size(),
                              to_string(get_source_endpoint(client->socket)));
            }

            // Only add client to list after successful preamble send (or if no preamble)
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.emplace_back(client);
            }
        }
        catch (const std::exception &e)
        {
            logger->error("Error handling new client: {}", e.what());
        }

        // Continue accepting new connections
        startAcceptIpc(ipc_acceptor);
    }
    else
    {
        if (error != asio::error::operation_aborted)
        {
            logger->error("Accept error: {}", error.message());

            // Continue accepting if acceptor is still valid
            startAcceptIpc(ipc_acceptor);
        }
    }
}
#endif

} // namespace mesytec::mvlc
