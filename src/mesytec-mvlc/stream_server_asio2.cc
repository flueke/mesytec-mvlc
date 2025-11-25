#include "stream_server_asio.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <spdlog/fmt/std.h>
#include <spdlog/spdlog.h>
#include <thread>

#define ASIO_ENABLE_BUFFER_DEBUGGING
#define ASIO_ENABLE_HANDLER_TRACKING
#include <asio.hpp>

#define HANDLER_LOCATION ASIO_HANDLER_LOCATION((__FILE__, __LINE__, __func__))

#ifdef ASIO_HAS_LOCAL_SOCKETS
#include <asio/local/stream_protocol.hpp>
#endif

namespace mesytec::mvlc
{

static const size_t DefaultIoThreads = 1;

template <typename Func> std::future<void> post(asio::io_context &io_context, Func &&func)
{
    std::packaged_task<void()> task(std::move(func));
    return asio::post(io_context, asio::use_future(std::move(task)));
}

template <typename Func> void post_and_wait(asio::io_context &io_context, Func &&func)
{
    post(io_context, std::forward<Func>(func)).wait();
}

class TcpClient: std::enable_shared_from_this<TcpClient>
{
  public:
    using Pointer = std::shared_ptr<TcpClient>;
    asio::ip::tcp::socket socket;

    static Pointer create(asio::ip::tcp::socket &&socket)
    {
        return Pointer(new TcpClient(std::move(socket)));
    }

    ~TcpClient() { spdlog::trace("TcpClient destroyed @{}", fmt::ptr(this)); }

  private:
    explicit TcpClient(asio::ip::tcp::socket &&socket_)
        : socket(std::move(socket_))
    {
        spdlog::trace("TcpClient created @{}", fmt::ptr(this));
    }
};

struct StreamServerAsio::Private
{
    asio::io_context io_context_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> io_threads_;
    // std::unique_ptr<asio::strand<asio::io_context::executor_type>> strand_;
    // std::unique_ptr<asio::io_context::work> work_guard_;

    std::vector<std::shared_ptr<asio::ip::tcp::acceptor>> tcp_acceptors_;
    std::mutex acceptors_mutex_;
    std::condition_variable acceptors_cv_;
    size_t pending_accepts_ = 0;

    void startAccept(std::shared_ptr<asio::ip::tcp::acceptor> acceptor)
    {
        HANDLER_LOCATION;
        std::unique_lock<std::mutex> lock(acceptors_mutex_);

        spdlog::trace("Starting async accept on acceptor @{}", fmt::ptr(acceptor.get()));

        acceptor->async_accept(
            asio::make_strand(io_context_),
            [this, acceptor](const asio::error_code &ec, asio::ip::tcp::socket socket)
            {
                HANDLER_LOCATION;

                std::unique_lock<std::mutex> lock(acceptors_mutex_);

                spdlog::trace("TcpAcceptor::async_accept handler called, ec={}, pending_accepts={}",
                              ec.message(), pending_accepts_);

                if (!acceptor->is_open())
                {
                    pending_accepts_--;
                    spdlog::trace("TcpAcceptor::async_accept handler called, acceptor is closed, "
                                  "pending_accepts={}",
                                  pending_accepts_);
                    lock.unlock();
                    acceptors_cv_.notify_all();
                    return;
                }

                if (!ec)
                {
                    spdlog::trace("Accepted TCP connection from {}",
                                  socket.remote_endpoint().address().to_string());
                    this->addClient(TcpClient::create(std::move(socket)));
                }
                else
                {
                    spdlog::error("Error accepting TCP connection: {}", ec.message());
                }
                pending_accepts_--;
                spdlog::trace("TcpAcceptor::async_accept handler done, pending_accepts={}",
                              pending_accepts_);
                lock.unlock();
                acceptors_cv_.notify_all();

                startAccept(acceptor);
            });

        pending_accepts_++;
        spdlog::trace("Async accept started, pending_accepts={}", pending_accepts_);
        lock.unlock();
        acceptors_cv_.notify_all();
    }

    std::vector<TcpClient::Pointer> tcp_clients_;
    std::mutex clients_mutex_;

    bool listenTcp(const std::string &uri);
    // bool listenIpc(const std::string &path);
    void start(size_t num_threads = DefaultIoThreads);
    void addClient(std::shared_ptr<TcpClient> client)
    {
        std::lock_guard<std::mutex> clients_lock(clients_mutex_);
        tcp_clients_.emplace_back(std::move(client));
    }
};

StreamServerAsio::StreamServerAsio()
    : IStreamServer()
    , d(std::make_unique<Private>())
{
    d->start();
}

StreamServerAsio::~StreamServerAsio()
{
    spdlog::trace("StreamServerAsio::~StreamServerAsio() called");
    stop();
}

void StreamServerAsio::Private::start(size_t num_threads)
{
    if (running_)
        return;

    spdlog::debug("Starting StreamServerAsio with {} IO threads", num_threads);
    running_ = true;
    // strand_ =
    //     std::make_unique<asio::strand<asio::io_context::executor_type>>(io_context_.get_executor());
    // work_guard_ = std::make_unique<asio::io_context::work>(io_context_);

    for (size_t i = 0; i < num_threads; ++i)
    {
        io_threads_.emplace_back(
            [this]()
            {
                spdlog::debug("Starting IO thread, id={}", std::this_thread::get_id());
                while (!io_context_.stopped())
                {
                    spdlog::trace("Running io_context_.run_for(1s) in IO thread id={}",
                                  std::this_thread::get_id());
                    auto handlersCount = io_context_.run_for(std::chrono::seconds(1));
                    spdlog::trace("io_context_.run_for(1s) returned, id={}, handlers={}",
                                  std::this_thread::get_id(), handlersCount);
                }
                spdlog::debug("IO thread exiting, id={}", std::this_thread::get_id());
            });
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

        // asio::ip::tcp::resolver resolver(io_context_);
        // asio::ip::tcp::resolver::query query(host, port);
        // auto endpoints = resolver.resolve(query);
        struct ResolveEntry
        {
            asio::ip::tcp::endpoint endpoint() const { return ep; }
            asio::ip::tcp::endpoint ep;
        };
        auto endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), std::stoi(port));
        auto endpoints = {ResolveEntry{endpoint}};

        for (const auto &resolveEntry: endpoints)
        {
            // Skip IPv6 if scheme is tcp4, skip IPv4 if scheme is tcp6
            if (scheme == "tcp4" && resolveEntry.endpoint().address().is_v6())
                continue;
            if (scheme == "tcp6" && resolveEntry.endpoint().address().is_v4())
                continue;

            auto acceptor =
                std::make_shared<asio::ip::tcp::acceptor>(io_context_, resolveEntry.endpoint());
            {
                std::lock_guard<std::mutex> lock(acceptors_mutex_);
                tcp_acceptors_.push_back(acceptor);
            }

            spdlog::info("Listening on TCP {}:{}", resolveEntry.endpoint().address().to_string(),
                         resolveEntry.endpoint().port());

            HANDLER_LOCATION;
            startAccept(acceptor);

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

#if 0
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
        std::lock_guard<std::mutex> lock(acceptors_mutex);
        unix_acceptors_.emplace_back(std::move(acceptor));

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
#endif

bool StreamServerAsio::listen(const std::string &uri)
{
    if (uri.find("tcp://") == 0 || uri.find("tcp4://") == 0 || uri.find("tcp6://") == 0)
    {
        bool result = d->listenTcp(uri);

        if (!d->running_ && result)
            d->start();

        return result;
    }
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

void StreamServerAsio::stop()
{
    if (!d->running_)
        return;

    {
        spdlog::trace("StreamServerAsio::stop(): Stopping acceptors");
        std::unique_lock<std::mutex> acceptors_lock(d->acceptors_mutex_);

#if 0
        for (auto &acceptor: d->tcp_acceptors_)
        {
            post_and_wait(d->io_context_,
                          [acceptor]()
                          {
                              asio::error_code ec;
                              acceptor->close(ec);
                              if (ec)
                              {
                                  spdlog::error("Error closing TCP acceptor: {}", ec.message());
                              }
                          });
        }
#else
        asio::error_code opt_ec;
        for (auto &acceptor: d->tcp_acceptors_)
            acceptor->close(opt_ec);

        d->acceptors_cv_.wait(acceptors_lock, [this]() { return d->pending_accepts_ == 0; });
#endif

        // TODO: clear acceptors

        spdlog::trace("StreamServerAsio::stop(): All acceptors stopped");
    }

#if 0
    {
        spdlog::trace("StreamServerAsio::stop(): Closing client connections");
        std::lock_guard<std::mutex> lock(d->clients_mutex_);

        for (auto &client: d->tcp_clients_)
        {
            post_and_wait(d->io_context_,
                          [client]()
                          {
                              asio::error_code ec;
                              client->socket.close(ec);
                              if (ec)
                              {
                                  spdlog::error("Error closing TCP client socket: {}",
                                                ec.message());
                              }
                          });
        }

        // TODO: clear clients

        spdlog::trace("StreamServerAsio::stop(): All client connections closed");
    }
#endif

    // d->work_guard_.reset();

    spdlog::trace("StreamServerAsio::stop() calling io_context_.stop()");
    if (!d->io_context_.stopped())
        d->io_context_.stop();
    spdlog::trace("StreamServerAsio::stop() io_context_.stop() returned");

    spdlog::trace("sleeping for a bit");
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    spdlog::trace("woke up from sleep, io_context.stopped()={}", d->io_context_.stopped());

    // assert(d->io_context_.stopped());

    spdlog::trace("StreamServerAsio::stop() joining IO threads");
    for (auto &thread: d->io_threads_)
    {
        if (thread.joinable())
            thread.join();
    }

    spdlog::trace("StreamServerAsio::stop() clearing all the things");

    d->io_threads_.clear();
    d->tcp_acceptors_.clear();
    d->tcp_clients_.clear();

    // Clear everything
}

bool StreamServerAsio::isListening() const { return false; }

std::vector<std::string> StreamServerAsio::listenUris() const
{
#if 0
    std::lock_guard<std::mutex> lock(d->acceptors_mutex);
    std::vector<std::string> result;
    for (auto &acceptor: d->tcp_acceptors_)
    {
        result.emplace_back(fmt::format("tcp://{}:{}",
                                        acceptor->acceptor.local_endpoint().address().to_string(),
                                        acceptor->acceptor.local_endpoint().port()));
    }
#ifdef ASIO_HAS_LOCAL_SOCKETS
    for (auto &acceptor: d->unix_acceptors_)
    {
        result.emplace_back(fmt::format("ipc://{}", acceptor->acceptor.local_endpoint().path()));
    }
#endif
    return result;
    ;
#else
    return {};
#endif
}

std::vector<std::string> StreamServerAsio::clients() const
{
#if 0
    std::lock_guard<std::mutex> lock(d->clients_mutex_);
    std::vector<std::string> result;
    result.reserve(d->clients_.size());

    for (const auto &client: d->clients_)
        result.emplace_back(client->remoteAddress());

    return result;
#else
    return {};
#endif
}

ssize_t StreamServerAsio::sendToAllClients(const IOV *iov, size_t n_iov)
{
#if 0
    std::lock_guard<std::mutex> send_lock(d->send_mutex);

    // Reuse vectors from Private to avoid allocations
    d->send_buffers_.clear();
    d->send_failed_.clear();
    d->send_results_.clear();

    d->send_buffers_.reserve(n_iov);
    for (size_t i = 0; i < n_iov; ++i)
        d->send_buffers_.emplace_back(asio::buffer(iov[i].buf, iov[i].len));

    {
        std::lock_guard<std::mutex> clients_lock(d->clients_mutex_);

        if (d->clients_.empty())
            return 0;

        std::unique_lock<std::mutex> send_results_lock(d->send_results_mutex_);
        d->send_results_.resize(d->clients_.size());
        d->send_results_pending_ = d->clients_.size();

        for (size_t i = 0; i < d->clients_.size(); ++i)
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

    std::unique_lock<std::mutex> send_results_lock(d->send_results_mutex_);
    d->send_results_cv_.wait(send_results_lock, [this]() { return d->send_results_pending_ == 0; });
    size_t completed = 0;

    for (size_t i = 0; i < d->send_results_.size(); ++i)
    {
        if (d->send_results_[i].ec)
        {
            d->send_failed_.emplace_back(d->clients_[i].get());
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
                           [this](const std::shared_ptr<ClientBase> &c)
                           {
                               return std::find(d->send_failed_.begin(), d->send_failed_.end(),
                                                c.get()) != d->send_failed_.end();
                           });
        d->clients_.erase(new_end, d->clients_.end());
    }

    return completed;
#else
    return -1;
#endif
}

} // namespace mesytec::mvlc
