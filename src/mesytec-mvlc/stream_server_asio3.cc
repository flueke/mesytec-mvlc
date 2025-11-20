#include "stream_server_asio.h"

#ifndef NDEBUG
#define ASIO_ENABLE_BUFFER_DEBUGGING
#define ASIO_ENABLE_HANDLER_TRACKING
#endif
#include <asio.hpp>

// Enable local (unix domain) sockets on non-Windows platforms where the
// toolchain claims to support them.
#if defined(ASIO_HAS_LOCAL_SOCKETS) and (not defined(_WIN32))
#define ENABLE_UNIX_DOMAIN_SOCKETS
#include <asio/local/stream_protocol.hpp>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <spdlog/fmt/ranges.h>
#include <spdlog/fmt/std.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <variant>

#define HANDLER_LOCATION ASIO_HANDLER_LOCATION((__FILE__, __LINE__, __func__))

namespace mesytec::mvlc
{

void run_io_context(asio::io_context &io_context)
{
    spdlog::trace("Entering io_context.run(), thread={}", std::this_thread::get_id());
    io_context.run();
    spdlog::trace("Left io_context.run(), thread={}", std::this_thread::get_id());
}

#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
using Client = std::variant<asio::ip::tcp::socket, asio::local::stream_protocol::socket>;
#else
using Client = std::variant<asio::ip::tcp::socket>;
#endif

bool is_open(const Client &client)
{
    return std::visit([](const auto &sock) { return sock.is_open(); }, client);
}

void close(Client &client)
{
    return std::visit(
        [](auto &sock)
        {
            asio::error_code ec;
            sock.close(ec);
        },
        client);
}

struct SendResult
{
    asio::error_code ec;
    size_t bytesTransferred;
};

struct StreamServerAsio::Private
{
    StreamServerAsio *q = nullptr;

    size_t n_io_threads_ = 1;
    std::vector<std::thread> io_threads_;
    asio::io_context io_context_;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;

    asio::ip::tcp::acceptor tcp_acceptor_;
#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
    asio::local::stream_protocol::acceptor local_acceptor_;
#endif
    asio::strand<asio::io_context::executor_type> acceptors_strand_;
    //std::mutex acceptors_mutex_;

    std::vector<std::shared_ptr<Client>> clients_;
    mutable std::mutex clients_mutex_;

    std::vector<asio::const_buffer> send_buffers_;
    size_t sends_scheduled_ = 0;
    size_t sends_completed_ = 0;
    std::vector<SendResult> send_results_;
    std::mutex send_mutex_;
    std::condition_variable sends_cv_;

    std::vector<std::uint8_t> preamble_;
    std::mutex preamble_mutex_;

    std::atomic<bool> running_ = false;
    std::atomic<bool> listeningTcp_ = false;
    std::atomic<bool> listeningLocal_ = false;

    explicit Private(StreamServerAsio *q_, size_t n_io_threads)
        : q(q_)
        , n_io_threads_(n_io_threads)
        , work_guard_(asio::make_work_guard(io_context_))
        , tcp_acceptor_(io_context_)
#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
        , local_acceptor_(io_context_)
#endif
        , acceptors_strand_(asio::make_strand(io_context_))
    {
        assert(work_guard_.has_value());
    }

    ~Private()
    {
        spdlog::trace("~StreamServerAsio::Private() invoked");
    }

    void start();
    void stop();

    bool listen(const std::string &host, const std::string &service);
    bool listen(const std::string &path);

    void startAcceptTcp();
    void handleAcceptTcp(const asio::error_code &ec, asio::ip::tcp::socket socket);

#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
    void startAcceptLocal();
    void handleAcceptLocal(const asio::error_code &ec, asio::local::stream_protocol::socket socket);
#endif

    void startSendPreambleToClient(std::shared_ptr<Client> client);
    void handlePreambleSentToClient(const std::shared_ptr<Client> &client,
                                    const asio::error_code &ec, size_t bytes_transferred);

    void startSendToClient(const std::shared_ptr<Client> &client, size_t client_index,
                           const std::vector<asio::const_buffer> &buffers);
    void handleSendToClient(const std::shared_ptr<Client> &client, size_t client_index,
                            const asio::error_code &ec, size_t bytes_transferred);
};

StreamServerAsio::StreamServerAsio(size_t io_threads_)
    : d(std::make_unique<Private>(this, io_threads_))
{
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

StreamServerAsio::~StreamServerAsio()
{
    spdlog::trace("~StreamServerAsio() invoked");

    d->stop();

    spdlog::trace("~StreamServerAsio() completed");
}

void StreamServerAsio::Private::start()
{
    if (running_.exchange(true))
        return;

    spdlog::trace("Starting StreamServerAsio with {} IO threads", n_io_threads_);

    for (size_t i = 0; i < n_io_threads_; ++i)
    {
        io_threads_.emplace_back(run_io_context, std::ref(io_context_));
    }
}

void StreamServerAsio::Private::stop()
{
    if (!running_.exchange(false))
        return;

    spdlog::trace("Stopping StreamServerAsio");

#if 1
    spdlog::trace("stop(): closing acceptors");
    post_and_wait(acceptors_strand_,
                  [this]
                  {
                      //std::lock_guard<std::mutex> acceptors_lock(acceptors_mutex_);
                      asio::error_code ec;

                      tcp_acceptor_.close(ec);
                      spdlog::trace("TCP acceptor closed: {}", ec ? ec.message() : "success");

                      local_acceptor_.close(ec);
                      spdlog::trace("Local acceptor closed: {}", ec ? ec.message() : "success");
                  });

    spdlog::trace("stop(): closing client connections");
    post_and_wait(io_context_,
                  [this]
                  {
                      std::lock_guard<std::mutex> clients_lock(clients_mutex_);
                      for (auto &client: clients_)
                      {
                          close(*client);
                      }
                      clients_.clear();
                  });
#endif

    spdlog::trace("stop(): reseting work guard and stopping io_context");
    work_guard_.reset();
    //io_context_.stop();

    spdlog::trace("stop(): Waiting for IO threads to finish");
    for (auto &thread: io_threads_)
    {
        if (thread.joinable())
            thread.join();
    }
    io_threads_.clear();
}

bool StreamServerAsio::Private::listen(const std::string &host, const std::string &service)
{
    try
    {
        //std::unique_lock<std::mutex> lock(acceptors_mutex_);
        if (tcp_acceptor_.is_open())
        {
            spdlog::warn("StreamServerAsio is already listening on a TCP socket");
            return false;
        }

        asio::ip::tcp::resolver resolver(io_context_);
        auto results = resolver.resolve(host, service);

        for (const auto &entry: results)
        {
            spdlog::trace("Resolved {}:{} to {}", host, service,
                          entry.endpoint().address().to_string());
            tcp_acceptor_ = asio::ip::tcp::acceptor(io_context_, entry.endpoint());
            listeningTcp_ = true;
            //lock.unlock();
            startAcceptTcp();
            start();
            return true;
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Error starting TCP server on {}:{}: {}", host, service, e.what());
        return false;
    }
    return false;
}

bool StreamServerAsio::Private::listen(const std::string &path)
{
#ifndef ENABLE_UNIX_DOMAIN_SOCKETS
    spdlog::error("Unix domain sockets are not supported on this platform");
    return false;
#else
    try
    {
        //std::unique_lock<std::mutex> lock(acceptors_mutex_);
        if (local_acceptor_.is_open())
        {
            spdlog::warn("StreamServerAsio is already listening on a unix domain socket");
            return false;
        }

        local_acceptor_ = asio::local::stream_protocol::acceptor(
            io_context_, asio::local::stream_protocol::endpoint(path));
        listeningLocal_ = true;
        //lock.unlock();
        startAcceptLocal();
        start();
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

void StreamServerAsio::Private::startAcceptTcp()
{
    HANDLER_LOCATION;
    //std::lock_guard<std::mutex> lock(acceptors_mutex_);
    tcp_acceptor_.async_accept(asio::make_strand(io_context_),
                               std::bind(&StreamServerAsio::Private::handleAcceptTcp, this,
                                         std::placeholders::_1, std::placeholders::_2));
}

void StreamServerAsio::Private::handleAcceptTcp(const asio::error_code &ec,
                                                asio::ip::tcp::socket socket)
{
    HANDLER_LOCATION;
    {
        //std::lock_guard<std::mutex> lock(acceptors_mutex_);

        if (!tcp_acceptor_.is_open())
        {
            spdlog::trace("handleAccept called with ec={}, acceptor is closed, returning",
                          ec.message());
            return;
        }

        if (!ec)
        {
            spdlog::trace("Accepted connection from {}",
                          socket.remote_endpoint().address().to_string());
            auto client = std::make_shared<Client>(std::move(socket));
            startSendPreambleToClient(client);
        }
    }

    startAcceptTcp();
}

#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
void StreamServerAsio::Private::startAcceptLocal()
{
    HANDLER_LOCATION;
    //std::lock_guard<std::mutex> lock(acceptors_mutex_);
    local_acceptor_.async_accept(asio::make_strand(io_context_),
                                 std::bind(&StreamServerAsio::Private::handleAcceptLocal, this,
                                           std::placeholders::_1, std::placeholders::_2));
}

void StreamServerAsio::Private::handleAcceptLocal(const asio::error_code &ec,
                                                  asio::local::stream_protocol::socket socket)
{
    HANDLER_LOCATION;
    {
        //std::lock_guard<std::mutex> lock(acceptors_mutex_);

        if (!local_acceptor_.is_open())
        {
            spdlog::trace("handleAccept called with ec={}, acceptor is closed, returning",
                          ec.message());
            return;
        }

        if (!ec)
        {
            spdlog::trace("Accepted connection on unix domain socket");
            auto client = std::make_shared<Client>(std::move(socket));
            startSendPreambleToClient(client);
        }
    }

    startAcceptLocal();
}
#endif

void StreamServerAsio::Private::startSendPreambleToClient(std::shared_ptr<Client> client)
{
    auto preamble_copy = std::make_shared<std::vector<std::uint8_t>>();
    {
        std::lock_guard<std::mutex> preamble_lock(preamble_mutex_);
        *preamble_copy = preamble_;
    }

    HANDLER_LOCATION;
    std::visit(
        [this, client, preamble = preamble_copy](auto &socket)
        {
            auto handler =
                [this, client, preamble](const asio::error_code &ec, size_t bytes_transferred)
            {
                HANDLER_LOCATION;
                handlePreambleSentToClient(client, ec, bytes_transferred);
            };
            spdlog::trace("Sending preamble of {} bytes to new client: {}", preamble->size(),
                          fmt::join(*preamble, ", "));
            asio::async_write(socket, asio::buffer(*preamble), handler);
        },
        *client);
}

void StreamServerAsio::Private::handlePreambleSentToClient(const std::shared_ptr<Client> &client,
                                                           const asio::error_code &ec,
                                                           size_t bytes_transferred)
{
    HANDLER_LOCATION;
    if (ec)
    {
        spdlog::error("Error sending preamble to new client: {}, closing socket", ec.message());
        close(*client);
        return;
    }

    spdlog::trace("Preamble of {} bytes sent to new client, adding to client list",
                  bytes_transferred);

    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.emplace_back(std::move(client));
}

void StreamServerAsio::Private::startSendToClient(const std::shared_ptr<Client> &client,
                                                  size_t client_index,
                                                  const std::vector<asio::const_buffer> &buffers)
{
    HANDLER_LOCATION;
    std::visit(
        [this, client, client_index, buffers](auto &socket)
        {
            auto handler =
                [this, client, client_index](const asio::error_code &ec, size_t bytes_transferred)
            {
                HANDLER_LOCATION;
                handleSendToClient(client, client_index, ec, bytes_transferred);
            };
            asio::async_write(socket, buffers, handler);
        },
        *client);
}
void StreamServerAsio::Private::handleSendToClient(const std::shared_ptr<Client> &client,
                                                   size_t client_index, const asio::error_code &ec,
                                                   size_t bytes_transferred)
{
    HANDLER_LOCATION;
    std::unique_lock<std::mutex> lock(send_mutex_);
    ++sends_completed_;
    spdlog::trace("Send to client {}: ec={}, bytes_transferred={}, "
                  "sends_scheduled={}, sends_completed={}",
                  client_index, ec.message(), bytes_transferred, sends_scheduled_,
                  sends_completed_);
    send_results_[client_index].ec = ec;
    send_results_[client_index].bytesTransferred = bytes_transferred;
    if (ec)
    {
        spdlog::error("Error sending to client {}: {}, closing socket", client_index, ec.message());
        // Closing the socket is needed for the epoll backend to not deadlock us: when doing another
        // async_write() on a socket that previously had an error the callback is never invoked.
        // This is different to the select(), io_uring and iocp backends.
        close(*client);
    }
    lock.unlock();
    sends_cv_.notify_all();
}

bool StreamServerAsio::listen(const std::string &uri)
{
    if (uri.find("tcp://") == 0 || uri.find("tcp4://") == 0 || uri.find("tcp6://") == 0)
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

        return d->listen(host, port);
    }

    // TODO: implement ipc:// and unix:// schemes
#if 0
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
#endif

    spdlog::error("Unsupported URI scheme: {}", uri);
    return false;
}

void StreamServerAsio::setPreamble(const std::uint8_t *data, size_t size)
{
    std::lock_guard<std::mutex> preamble_lock(d->preamble_mutex_);
    d->preamble_.clear();
    std::copy(data, data + size, std::back_inserter(d->preamble_));
}

std::int64_t StreamServerAsio::sendToAllClients(const IOV *iov, size_t n_iov)
{
    std::unique_lock<std::mutex> send_lock(d->send_mutex_);
    std::unique_lock<std::mutex> clients_lock(d->clients_mutex_);

    assert(d->sends_scheduled_ == 0);
    d->sends_completed_ = 0;
    d->send_results_.clear();
    d->send_results_.resize(d->clients_.size());

    d->send_buffers_.clear();
    d->send_buffers_.reserve(n_iov);
    for (size_t i = 0; i < n_iov; ++i)
        d->send_buffers_.emplace_back(asio::const_buffer(iov[i].buf, iov[i].len));

    for (size_t i = 0; i < d->clients_.size(); ++i)
    {
        HANDLER_LOCATION;
        auto &client = d->clients_[i];

        if (is_open(*client))
        {
            d->startSendToClient(client, i, d->send_buffers_);
            ++d->sends_scheduled_;
        }
    }

    clients_lock.unlock(); // can accept new clients again
    spdlog::trace("Scheduled {} sends to clients, {} completed, waiting for completion now",
                  d->sends_scheduled_, d->sends_completed_);
    d->sends_cv_.wait(send_lock, [this]() { return d->sends_completed_ == d->sends_scheduled_; });
    auto ret = d->sends_completed_;
    d->sends_scheduled_ = 0;
    d->sends_completed_ = 0;

    clients_lock.lock();
    // Remove failed clients
    for (int i = static_cast<int>(d->clients_.size()) - 1; i >= 0; --i)
    {
        if (d->send_results_[i].ec || !is_open(*d->clients_[i]))
        {
            spdlog::debug("Removing client {} due to send error: {}", i,
                          d->send_results_[i].ec.message());
            d->clients_.erase(d->clients_.begin() + i);
        }
    }
    spdlog::debug("sendToAllClients: {} clients remaining after send", d->clients_.size());
    clients_lock.unlock();

    return ret;
}

size_t StreamServerAsio::clientCount() const
{
    std::lock_guard<std::mutex> lock(d->clients_mutex_);
    return d->clients_.size();
}

bool StreamServerAsio::isRunning() const { return d->running_; }

std::vector<std::string> StreamServerAsio::listenAddresses() const
{
    //std::lock_guard<std::mutex> acceptors_lock(d->acceptors_mutex_);
    std::vector<std::string> result;
    if (d->tcp_acceptor_.is_open())
    {
        result.emplace_back(d->tcp_acceptor_.local_endpoint().address().to_string());
    }
#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
    if (d->local_acceptor_.is_open())
    {
        result.emplace_back(d->local_acceptor_.local_endpoint().path());
    }
    return result;
#endif
}

// From: https://en.cppreference.com/w/cpp/utility/variant/visit

// helper type for the visitor #4
template <class... Ts> struct overloaded: Ts...
{
    using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

std::vector<std::string> StreamServerAsio::clientAddresses() const
{
    // clang-format off
    auto handler = overloaded
    {
        [] (const asio::ip::tcp::socket &sock) -> std::string
        {
            try
            {
                return sock.remote_endpoint().address().to_string();
            }
            catch (const std::exception &e)
            {
                spdlog::error("Error getting TCP client address: {}", e.what());
                return "<error>";
            }
        },
#ifdef ENABLE_UNIX_DOMAIN_SOCKETS
        [] (const asio::local::stream_protocol::socket &sock) -> std::string
        {
            try
            {
                return sock.remote_endpoint().path();
            }
            catch (const std::exception &e)
            {
                spdlog::error("Error getting local socket client address: {}", e.what());
                return "<error>";
            }
        },
#endif
    };
    // clang-format on

    std::lock_guard<std::mutex> clients_lock(d->clients_mutex_);
    std::vector<std::string> result;
    result.reserve(d->clients_.size());
    for (const auto &client: d->clients_)
    {
        result.emplace_back(std::visit(handler, *client));
    }
    return result;
}

} // namespace mesytec::mvlc
