#include "stream_server_asio.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include <thread>

#include <asio.hpp>

#ifdef ASIO_HAS_LOCAL_SOCKETS
#include <asio/local/stream_protocol.hpp>
#endif

namespace mesytec::mvlc
{

struct ClientBase
{
    using WriteHandler = std::function<void(const asio::error_code &ec, size_t bytesTransferred)>;

    virtual ~ClientBase() = default;
    virtual std::string remoteAddress() const = 0;
    virtual std::future<size_t> asyncWrite(const std::vector<asio::const_buffer> &buffers) = 0;
    virtual void asyncWrite(const std::vector<asio::const_buffer> &buffers, WriteHandler handler) = 0;
    virtual void close() = 0;
};

struct TcpAcceptor;
#ifdef ASIO_HAS_LOCAL_SOCKETS
struct UnixAcceptor;
#endif

struct StreamServerAsio::Private
{
    void addClient(std::unique_ptr<ClientBase> client);
    bool listenTcp(const std::string &uri);
    bool listenIpc(const std::string &path);
    void start(size_t num_threads = 4);

    asio::io_context io_context_;
    std::unique_ptr<asio::io_context::work> work_guard_;
    std::vector<std::unique_ptr<TcpAcceptor>> tcp_acceptors_;
#ifdef ASIO_HAS_LOCAL_SOCKETS
    std::vector<std::unique_ptr<UnixAcceptor>> unix_acceptors_;
#endif
    std::vector<std::unique_ptr<ClientBase>> clients_;
    std::mutex mutable clients_mutex_;
    std::vector<std::thread> io_threads_;
    std::atomic<bool> running_{false};

    // Reusable buffers for sendToAllClients to avoid allocations
    std::vector<asio::const_buffer> send_buffers_;
    std::vector<ClientBase *> send_failed_;

    #if 0 // for the future based approach: one future per client
    std::vector<std::future<size_t>> send_futures_;
    std::vector<ClientBase *> send_snapshot_;
    #else
    struct WriteResult
    {
        asio::error_code ec = {};
        size_t bytes_transferred = 0;
    };
    std::vector<WriteResult> send_results_;
    std::mutex send_results_mutex_;
    std::condition_variable send_results_cv_;
    size_t send_results_pending_ = 0;
    #endif
};

struct TcpClient: ClientBase
{
    asio::ip::tcp::socket socket;

    explicit TcpClient(asio::ip::tcp::socket s)
        : socket(std::move(s))
    {
    }

    std::string remoteAddress() const override
    {
        try
        {
            return socket.remote_endpoint().address().to_string() + ":" +
                   std::to_string(socket.remote_endpoint().port());
        }
        catch (...)
        {
            return "unknown";
        }
    }

    std::future<size_t> asyncWrite(const std::vector<asio::const_buffer> &buffers) override
    {
        return asio::async_write(socket, buffers, asio::use_future);
    }

    void asyncWrite(const std::vector<asio::const_buffer> &buffers, WriteHandler handler)
    {
        asio::async_write(socket, buffers, handler);
    }

    void close() override
    {
        asio::error_code ec;
        socket.close(ec);
    }
};

#ifdef ASIO_HAS_LOCAL_SOCKETS
struct UnixClient: ClientBase
{
    asio::local::stream_protocol::socket socket;

    explicit UnixClient(asio::local::stream_protocol::socket s)
        : socket(std::move(s))
    {
    }

    std::string remoteAddress() const override { return "unix:local_client"; }

    std::future<size_t> asyncWrite(const std::vector<asio::const_buffer> &buffers) override
    {
        return asio::async_write(socket, buffers, asio::use_future);
    }

    void asyncWrite(const std::vector<asio::const_buffer> &buffers, WriteHandler handler)
    {
        asio::async_write(socket, buffers, handler);
    }

    void close() override
    {
        asio::error_code ec;
        socket.close(ec);
    }
};
#endif

struct TcpAcceptor
{
    asio::ip::tcp::acceptor acceptor;
    asio::ip::tcp::socket socket;
    StreamServerAsio::Private *server;

    TcpAcceptor(asio::io_context &io, const asio::ip::tcp::endpoint &ep,
                StreamServerAsio::Private *srv)
        : acceptor(io, ep)
        , socket(io)
        , server(srv)
    {
    }

    void startAccept()
    {
        acceptor.async_accept(
            socket,
            [this](const asio::error_code &ec)
            {
                if (!ec)
                {
                    // Set socket options for better performance
                    asio::error_code opt_ec;
                    socket.set_option(asio::ip::tcp::no_delay(true), opt_ec);
                    socket.set_option(asio::socket_base::send_buffer_size(2 * 1024 * 1024), opt_ec);
                    socket.set_option(asio::socket_base::receive_buffer_size(2 * 1024 * 1024),
                                      opt_ec);

                    auto client = std::make_unique<TcpClient>(std::move(socket));
                    server->addClient(std::move(client));
                }
                if (acceptor.is_open())
                    startAccept();
            });
    }
};

#ifdef ASIO_HAS_LOCAL_SOCKETS
struct UnixAcceptor
{
    asio::local::stream_protocol::acceptor acceptor;
    asio::local::stream_protocol::socket socket;
    StreamServerAsio::Private *server;
    std::string path;

    UnixAcceptor(asio::io_context &io, const std::string &path_, StreamServerAsio::Private *srv)
        : acceptor(io, asio::local::stream_protocol::endpoint(path_))
        , socket(io)
        , server(srv)
        , path(path_)
    {
    }

    ~UnixAcceptor()
    {
        // Clean up socket file
        ::unlink(path.c_str());
    }

    void startAccept()
    {
        acceptor.async_accept(socket,
                              [this](const asio::error_code &ec)
                              {
                                  if (!ec)
                                  {
                                      auto client = std::make_unique<UnixClient>(std::move(socket));
                                      server->addClient(std::move(client));
                                  }
                                  if (acceptor.is_open())
                                      startAccept();
                              });
    }
};
#endif

StreamServerAsio::StreamServerAsio()
    : d(std::make_unique<Private>())
{
}

StreamServerAsio::~StreamServerAsio() { stop(); }

void StreamServerAsio::Private::addClient(std::unique_ptr<ClientBase> client)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    spdlog::info("New client connected: {}", client->remoteAddress());
    clients_.push_back(std::move(client));
}

void StreamServerAsio::Private::start(size_t num_threads)
{
    if (running_)
        return;

    running_ = true;
    work_guard_ = std::make_unique<asio::io_context::work>(io_context_);

    for (size_t i = 0; i < num_threads; ++i)
    {
        io_threads_.emplace_back([this]() { io_context_.run(); });
    }
}

bool StreamServerAsio::Private::listenTcp(const std::string &uri)
{
    try
    {
        // Parse URI: tcp://host:port or tcp4://host:port or tcp6://host:port
        std::string scheme, host_port;
        size_t scheme_end = uri.find("://");

        if (scheme_end == std::string::npos)
            return false;

        scheme = uri.substr(0, scheme_end);
        host_port = uri.substr(scheme_end + 3);

        size_t colon = host_port.rfind(':');
        if (colon == std::string::npos)
            return false;

        std::string host = host_port.substr(0, colon);
        std::string port = host_port.substr(colon + 1);

        if (host.empty() || host == "*")
            host = "0.0.0.0";

        asio::ip::tcp::resolver resolver(io_context_);
        asio::ip::tcp::resolver::query query(host, port);
        auto endpoints = resolver.resolve(query);

        for (const auto &endpoint: endpoints)
        {
            // Skip IPv6 if scheme is tcp4, skip IPv4 if scheme is tcp6
            if (scheme == "tcp4" && endpoint.endpoint().address().is_v6())
                continue;
            if (scheme == "tcp6" && endpoint.endpoint().address().is_v4())
                continue;

            auto acceptor = std::make_unique<TcpAcceptor>(io_context_, endpoint.endpoint(), this);

            spdlog::info("Listening on TCP {}:{}", endpoint.endpoint().address().to_string(),
                         endpoint.endpoint().port());
            acceptor->startAccept();
            tcp_acceptors_.push_back(std::move(acceptor));

            if (scheme == "tcp4" || scheme == "tcp6")
                break; // Only bind once for specific protocol version
        }

        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to listen on TCP {}: {}", uri, e.what());
        return false;
    }
}

bool StreamServerAsio::Private::listenIpc(const std::string &path)
{
#ifdef ASIO_HAS_LOCAL_SOCKETS
    try
    {
        // Remove existing socket file if present
        ::unlink(path.c_str());

        auto acceptor = std::make_unique<UnixAcceptor>(io_context_, path, this);

        spdlog::info("Listening on IPC {}", path);
        acceptor->startAccept();
        unix_acceptors_.push_back(std::move(acceptor));

        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to listen on IPC {}: {}", path, e.what());
        return false;
    }
#else
    spdlog::error("Unix domain sockets not supported on this platform");
    return false;
#endif
}

bool StreamServerAsio::listen(const std::string &uri)
{
    if (uri.find("tcp://") == 0 || uri.find("tcp4://") == 0 || uri.find("tcp6://") == 0)
    {
        bool result = d->listenTcp(uri);

        if (!d->running_ && result)
            d->start();

        return result;
    }
    else if (uri.find("ipc://") == 0)
    {
        std::string path = uri.substr(6); // Remove "ipc://" prefix
        bool result = d->listenIpc(path);

        if (!d->running_ && result)
            d->start();

        return result;
    }
    else if (uri.find("unix://") == 0)
    {
        std::string path = uri.substr(7); // Remove "unix://" prefix
        bool result = d->listenIpc(path);

        if (!d->running_ && result)
            d->start();

        return result;
    }

    spdlog::error("Unsupported URI scheme: {}", uri);
    return false;
}

bool StreamServerAsio::listen(const std::vector<std::string> &uris)
{
    bool all_ok = true;
    for (const auto &uri: uris)
    {
        if (!listen(uri))
            all_ok = false;
    }
    return all_ok;
}

void StreamServerAsio::stop()
{
    if (!d->running_)
        return;

    d->running_ = false;

    // Close all acceptors
    for (auto &acceptor: d->tcp_acceptors_)
        acceptor->acceptor.close();

#ifdef ASIO_HAS_LOCAL_SOCKETS
    for (auto &acceptor: d->unix_acceptors_)
        acceptor->acceptor.close();
#endif

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(d->clients_mutex_);
        for (auto &client: d->clients_)
            client->close();
    }

    // Stop io_context and wait for thread
    d->work_guard_.reset();
    d->io_context_.stop();
    for (auto &thread: d->io_threads_)
    {
        if (thread.joinable())
            thread.join();
    }
    d->io_threads_.clear();

    // Clear everything
    d->tcp_acceptors_.clear();
#ifdef ASIO_HAS_LOCAL_SOCKETS
    d->unix_acceptors_.clear();
#endif
    d->clients_.clear();
}

bool StreamServerAsio::isListening() const
{
    return !d->tcp_acceptors_.empty()
#ifdef ASIO_HAS_LOCAL_SOCKETS
           || !d->unix_acceptors_.empty()
#endif
        ;
}

std::vector<std::string> StreamServerAsio::clients() const
{
    std::lock_guard<std::mutex> lock(d->clients_mutex_);
    std::vector<std::string> result;
    result.reserve(d->clients_.size());

    for (const auto &client: d->clients_)
        result.push_back(client->remoteAddress());

    return result;
}

ssize_t StreamServerAsio::sendToAllClients(const uint8_t *data, size_t size)
{
    IOV iov{data, size};
    return sendToAllClients(&iov, 1);
}

#if 0 // the future based approach
ssize_t StreamServerAsio::sendToAllClients(const IOV *iov, size_t n_iov)
{
    // Reuse vectors from Private to avoid allocations
    d->send_buffers_.clear();
    d->send_futures_.clear();
    d->send_snapshot_.clear();
    d->send_failed_.clear();

    d->send_buffers_.reserve(n_iov);
    for (size_t i = 0; i < n_iov; ++i)
        d->send_buffers_.push_back(asio::buffer(iov[i].buf, iov[i].len));

    {
        std::lock_guard<std::mutex> lock(d->clients_mutex_);

        if (d->clients_.empty())
            return 0;

        d->send_futures_.reserve(d->clients_.size());
        d->send_snapshot_.reserve(d->clients_.size());

        for (auto &client: d->clients_)
        {
            d->send_futures_.emplace_back(client->asyncWrite(d->send_buffers_));
            d->send_snapshot_.push_back(client.get());
        }
    }

    // Wait for ALL futures to complete (they execute concurrently on io_context)
    size_t completed = 0;
    for (size_t i = 0; i < d->send_futures_.size(); ++i)
    {
        try
        {
            d->send_futures_[i].get();
            completed++;
        }
        catch (...)
        {
            d->send_failed_.push_back(d->send_snapshot_[i]);
        }
    }

    // Remove failed clients
    if (!d->send_failed_.empty())
    {
        std::lock_guard<std::mutex> lock(d->clients_mutex_);
        spdlog::info("Removing {} clients due to send errors", d->send_failed_.size());
        auto new_end =
            std::remove_if(d->clients_.begin(), d->clients_.end(),
                           [this](const std::unique_ptr<ClientBase> &c)
                           {
                               return std::find(d->send_failed_.begin(), d->send_failed_.end(),
                                                c.get()) != d->send_failed_.end();
                           });
        d->clients_.erase(new_end, d->clients_.end());
    }

    return completed;
}
#else // the handler based approach
ssize_t StreamServerAsio::sendToAllClients(const IOV *iov, size_t n_iov)
{
    // Reuse vectors from Private to avoid allocations
    d->send_buffers_.clear();
    d->send_failed_.clear();
    d->send_results_.clear();

    d->send_buffers_.reserve(n_iov);
    for (size_t i = 0; i < n_iov; ++i)
        d->send_buffers_.push_back(asio::buffer(iov[i].buf, iov[i].len));

    {
        std::lock_guard<std::mutex> lock(d->clients_mutex_);

        if (d->clients_.empty())
            return 0;

        d->send_results_.resize(d->clients_.size());
        d->send_results_pending_ = d->clients_.size();

        for (size_t i=0; i<d->clients_.size(); ++i)
        {
            auto &client = d->clients_[i];

            auto handler = [this, i](const asio::error_code &ec, size_t bytes_transferred)
            {
                {
                    std::unique_lock<std::mutex> lock(d->send_results_mutex_);
                    d->send_results_[i].ec = ec;
                    d->send_results_[i].bytes_transferred = bytes_transferred;
                    d->send_results_pending_--;
                    lock.unlock();
                    d->send_results_cv_.notify_one();
                }
            };

            client->asyncWrite(d->send_buffers_, handler);
        }
    }

    std::unique_lock<std::mutex> lock(d->send_results_mutex_);
    d->send_results_cv_.wait(lock, [this]() { return d->send_results_pending_ == 0; });
    size_t completed = 0;

    for (size_t i = 0; i < d->send_results_.size(); ++i)
    {
        if (d->send_results_[i].ec)
        {
            d->send_failed_.push_back(d->clients_[i].get());
        }
        else
            ++completed;
    }

    // Remove failed clients
    if (!d->send_failed_.empty())
    {
        std::lock_guard<std::mutex> lock(d->clients_mutex_);
        spdlog::info("Removing {} clients due to send errors", d->send_failed_.size());
        auto new_end =
            std::remove_if(d->clients_.begin(), d->clients_.end(),
                           [this](const std::unique_ptr<ClientBase> &c)
                           {
                               return std::find(d->send_failed_.begin(), d->send_failed_.end(),
                                                c.get()) != d->send_failed_.end();
                           });
        d->clients_.erase(new_end, d->clients_.end());
    }

    return completed;
}
#endif

} // namespace mesytec::mvlc
