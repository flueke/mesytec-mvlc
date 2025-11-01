#include "stream_server.h"

#include <algorithm>
#include <mesytec-mvlc/util/nng_util.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <condition_variable>
#include <mutex>
#include <numeric>

#include <spdlog/spdlog.h>

namespace mesytec::mvlc
{

static const size_t NngMaxIOVs = 4;
using IovArray = std::array<nng_iov, NngMaxIOVs>;

size_t nng_iov_total_size(const nng_iov *iovs, size_t n_iov)
{
    size_t total = 0;
    for (size_t i = 0; i < n_iov; ++i)
    {
        total += iovs[i].iov_len;
    }
    return total;
}

size_t subtract_from_iovs(IovArray &iovs, size_t n_iov, size_t bytes)
{
    assert(n_iov <= NngMaxIOVs);

    IovArray new_iovs;
    size_t new_n_iov = 0;
    size_t remaining = bytes;

    for (size_t i = 0; i < n_iov; ++i)
    {
        if (remaining == 0)
            break;

        if (iovs[i].iov_len <= remaining)
        {
            remaining -= iovs[i].iov_len;
        }
        else
        {
            iovs[i].iov_buf = static_cast<uint8_t *>(iovs[i].iov_buf) + remaining;
            iovs[i].iov_len -= remaining;
            remaining = 0;
            new_iovs[new_n_iov++] = iovs[i];
        }
    }

    std::swap(iovs, new_iovs);
    return new_n_iov;
}

struct Acceptor;
struct Client;

struct SendSyncContext
{
    std::mutex send_mutex;
    std::condition_variable send_cv;
    std::atomic<size_t> sends_pending = 0;

    void dec()
    {
        std::unique_lock<std::mutex> lock(send_mutex);
        assert(sends_pending > 0);
        if (sends_pending > 0)
        {
            --sends_pending;
        }

        //if (sends_pending == 0)
        //{
        //    lock.unlock();
        //    send_cv.notify_all();
        //}
        send_cv.notify_all();
    }
};

struct StreamServer::Private
{
    std::vector<std::unique_ptr<Acceptor>> acceptors;
    std::vector<std::unique_ptr<Client>> clients;
    std::mutex clients_mutex;
    SendSyncContext send_ctx;

    ssize_t sendToAllClients(const IovArray &iovs, size_t n_iov);
    //void sendToAllClients(const std::vector<Client *> &clients_);
};

struct Acceptor
{
    StreamServer::Private *ctx = nullptr;
    nng_stream_listener *listener = nullptr;
    nng_aio *accept_aio = nullptr;
    std::string uri;
};

void start_send(Client *client);
void send_callback(void *arg);

struct Client: public StreamServer::IClient
{
    nng_stream *stream = nullptr;
    SendSyncContext &send_ctx;
    nng_aio *aio = nullptr;
    IovArray send_iovs;
    size_t n_send_iov = 0;
    int nng_result = 0;

    explicit Client(nng_stream *s, SendSyncContext &ctx)
        : stream(s)
        , send_ctx(ctx)
    {
    }

    ~Client()
    {
        assert(!nng_aio_busy(aio));
        if (aio)
            nng_aio_free(aio);

        if (stream)
            nng_stream_free(stream);
    }

    nng_sockaddr nngRemoteAddress() const
    {
        nng_sockaddr addr{};
        nng_stream_get_addr(stream, NNG_OPT_REMADDR, &addr);
        return addr;
    }
    std::string remoteAddress() const override
    {
        return nng_sockaddr_to_string(nngRemoteAddress());
    }
};

void accept_callback(void *arg);

void start_accept(Acceptor *acceptor)
{
    spdlog::debug("Starting accept on {}", acceptor->uri);
    if (!acceptor->accept_aio)
    {
        if (int rv = nng_aio_alloc(&acceptor->accept_aio, accept_callback, acceptor))
        {
            spdlog::error("Failed to allocate accept AIO: {}", nng_strerror(rv));
            return;
        }
    }

    nng_aio_set_timeout(acceptor->accept_aio, 1000);
    nng_stream_listener_accept(acceptor->listener, acceptor->accept_aio);
}

void accept_callback(void *arg)
{
    spdlog::debug("Accept callback called");
    auto acceptor = static_cast<Acceptor *>(arg);

    if (!acceptor->accept_aio)
    {
        spdlog::warn("Accept AIO is null in accept callback");
        return;
    }

    if (int rv = nng_aio_result(acceptor->accept_aio))
    {
        spdlog::warn("Accept AIO result indicates failure: {}", nng_strerror(rv));
        if (rv != NNG_ETIMEDOUT && rv != NNG_ECANCELED)
        {
            spdlog::error("Accept failed: {}", nng_strerror(rv));
            return;
        }

        //if (rv != NNG_ECANCELED)
        //{
        //    spdlog::warn("Accept canceled: {}", nng_strerror(rv));
        //    start_accept(acceptor);
        //}

        //return;
    }

    // Retrieve the nng stream object from the aio
    nng_stream *stream = static_cast<nng_stream *>(nng_aio_get_output(acceptor->accept_aio, 0));

    if (!stream)
    {
        spdlog::error("Accepted null stream");
        start_accept(acceptor);
        return;
    }

    // Create and add the new client
    try
    {
        auto client = std::make_unique<Client>(stream, acceptor->ctx->send_ctx);

        {
            spdlog::info("Accepted new connection from {}", client->remoteAddress());
            std::lock_guard<std::mutex> lock(acceptor->ctx->clients_mutex);
            acceptor->ctx->clients.emplace_back(std::move(client));
        }
    }
    catch (const NngException &e)
    {
        spdlog::warn("Failed to handle new connection: {}", e.what());
    }

    // Continue accepting
    start_accept(acceptor);
}

StreamServer::StreamServer()
    : d(std::make_unique<Private>())
{
}

StreamServer::~StreamServer() { stop(); }

bool StreamServer::listen(const std::string &uri)
{
    auto acceptor = std::make_unique<Acceptor>();
    acceptor->ctx = d.get();
    acceptor->uri = uri;

    if (int rv = nng_stream_listener_alloc(&acceptor->listener, uri.c_str()))
    {
        spdlog::error("Failed to allocate listener for {}: {}", uri, nng_strerror(rv));
        return false;
    }

    if (int rv = nng_stream_listener_listen(acceptor->listener))
    {
        spdlog::error("Failed to listen on {}: {}", uri, nng_strerror(rv));
        nng_stream_listener_free(acceptor->listener);
        return false;
    }

    nng_sockaddr local_addr{};
    if (nng_stream_listener_get_addr(acceptor->listener, NNG_OPT_LOCADDR, &local_addr) == 0)
    {
        spdlog::info("Listening on {}", nng_sockaddr_to_string(local_addr));
    }

    auto &a = d->acceptors.emplace_back(std::move(acceptor));
    start_accept(a.get());
    return true;
}

bool StreamServer::listen(const std::vector<std::string> &uris)
{
    for (const auto &uri: uris)
    {
        if (!listen(uri))
            return false;
    }
    return true;
}

void StreamServer::stop()
{
    spdlog::debug("Stopping StreamServer");
    for (auto &acceptor: d->acceptors)
    {
        if (acceptor->accept_aio)
        {
            nng_aio_stop(acceptor->accept_aio);
            nng_aio_free(acceptor->accept_aio);
        }
        acceptor->accept_aio = nullptr;
        nng_stream_listener_free(acceptor->listener);
    }

    d->acceptors.clear();
}

bool StreamServer::isListening() const { return !d->acceptors.empty(); }

std::vector<std::string> StreamServer::clients() const
{
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(d->clients_mutex);
    result.reserve(d->clients.size());
    for (const auto &client: d->clients)
        result.emplace_back(client->remoteAddress());
    return result;
}

void start_send(Client *client)
{
    if (!client->aio)
    {
        if (int rv = nng_aio_alloc(&client->aio, send_callback, client))
        {
            spdlog::error("Failed to allocate client AIO: {}", nng_strerror(rv));
            client->nng_result = rv;
            client->send_ctx.dec();
            return;
        }
    }

    assert(client->aio);
    assert(!nng_aio_busy(client->aio));

    // Will only fail if iovs is too large (> 4) or nng runs OOM
    if (int rv = nng_aio_set_iov(client->aio, client->n_send_iov, client->send_iovs.data()); rv != 0)
    {
        spdlog::warn("Failed to set IOVs for client {}: {}", client->remoteAddress(), nng_strerror(rv));
        client->nng_result = rv;
        client->send_ctx.dec();
        return;
    }

    spdlog::debug("Starting send to client {} with {} IOVs totaling {} bytes",
                  client->remoteAddress(),
                  client->n_send_iov,
                  nng_iov_total_size(client->send_iovs.data(), client->n_send_iov));

    nng_stream_send(client->stream, client->aio);
}

void send_callback(void *arg)
{
    auto client = static_cast<Client *>(arg);
    spdlog::debug("Send callback called for Client instance @ {}, {}", fmt::ptr(client), client->remoteAddress());

    //assert(!nng_aio_busy(client->aio));

    if (int rv = nng_aio_result(client->aio))
    {
        spdlog::warn("Send to client {} failed: {}", client->remoteAddress(), nng_strerror(rv));
        assert(!nng_aio_busy(client->aio));
        client->nng_result = rv;
        client->send_ctx.dec();
        return;
    }

    size_t sentSize = nng_aio_count(client->aio);
    auto totalSize = nng_iov_total_size(client->send_iovs.data(), client->n_send_iov);
    client->n_send_iov = subtract_from_iovs(client->send_iovs, client->n_send_iov, sentSize);
    auto newTotalSize = nng_iov_total_size(client->send_iovs.data(), client->n_send_iov);
    assert(newTotalSize + sentSize == totalSize);

    if (client->n_send_iov > 0)
    {
        start_send(client);
    }
    else
    {
        spdlog::debug("Completed send to client {}", client->remoteAddress());
        client->send_ctx.dec();
    }
}


ssize_t StreamServer::Private::sendToAllClients(const IovArray &iovs, size_t n_iov)
{
    assert(n_iov <= NngMaxIOVs);

    if (nng_iov_total_size(iovs.data(), n_iov) == 0)
    {
        spdlog::warn("sendToAllClients called with zero total size");
        return 0;
    }

    std::unique_lock<std::mutex> clients_lock(clients_mutex);

    if (clients.empty())
        return 0;

    send_ctx.sends_pending = clients.size();

    for (auto &client: clients)
    {
        client->send_iovs = iovs;
        client->n_send_iov = n_iov;
        client->nng_result = 0;
        start_send(client.get());
    }

    clients_lock.unlock();

    std::unique_lock<std::mutex> send_lock(send_ctx.send_mutex);
    send_ctx.send_cv.wait(send_lock, [this]()
                 { return send_ctx.sends_pending == 0; });

    clients_lock.lock();

    size_t sends_completed = 0;

    for  (auto &client: clients)
    {
        spdlog::debug("Checking result for client @{}, {}", fmt::ptr(client.get()), client->remoteAddress());
        assert(!nng_aio_busy(client->aio));

        if (client->nng_result == 0)
        {
            ++sends_completed;
        }
        else
        {
            spdlog::info("TODO Removing client {} due to send error", client->remoteAddress());
        }

        client->send_iovs.fill(nng_iov{ nullptr, 0 });
        client->n_send_iov = 0;
    }

    return sends_completed;
}

#if 0
ssize_t StreamServer::Private::sendToAllClients(const nng_iov *iovs, size_t n_iov)
{
    assert(n_iov <= NngMaxIOVs);
    if (n_iov > NngMaxIOVs)
    {
        spdlog::error("sendToAllClients called with too many IOVs: {} (max {})", n_iov, NngMaxIOVs);
        return -1;
    }

    std::unique_lock<std::mutex> lock(clients_mutex);

    if (clients.empty())
    {
        return 0; // No clients to send to
    }

    util::Stopwatch sw;

    // Create a copy of the current client pointers, so we can release the lock asap.
    std::vector<Client *> clients_;
    clients_.reserve(clients.size());

    for (auto &client: clients)
    {
        std::copy(iovs, iovs + n_iov, client->send_iovs.data());
        clients_.emplace_back(client.get());
    }

    lock.unlock();

    sendToAllClients(clients_);

    std::vector<Client *> clientsToRemove;
    ssize_t ret = 0;

    for (auto it = clients_.begin(); it != clients_.end(); ++it)
    {
        auto &client = *it;

        if (nng_aio_result(client->aio) == 0)
        {
            ++ret;
            continue;
        }

        clientsToRemove.push_back(client);
    }

    if (!clientsToRemove.empty())
    {
        spdlog::info("Removing {} clients due to send errors", clientsToRemove.size());
        lock.lock();

        auto r = std::remove_if(clients.begin(), clients.end(),
                                [&clientsToRemove](const std::unique_ptr<Client> &c)
                                {
                                    return std::find(clientsToRemove.begin(), clientsToRemove.end(),
                                                     c.get()) != clientsToRemove.end();
                                });
        clients.erase(r, clients.end());
    }

    spdlog::debug("sendToAllClients to {} clients took {} ms",
                  clients_.size(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(sw.get_elapsed()).count());

    return ret;
}

void StreamServer::Private::sendToAllClients(const std::vector<Client *> &clients_)
{
    auto need_more_sends = [](auto &clients_) -> bool
    {
        return std::any_of(clients_.begin(), clients_.end(),
                           [](auto *c)
                           { return nng_iov_total_size(c->send_iovs.data(), c->n_send_iov) > 0; });
    };

    bool didScheduleASend = false;

    do
    {
        didScheduleASend = false;

        // Schedule a send for each client that still has data left to send
        for (auto &client: clients_)
        {
            if (nng_iov_total_size(client->send_iovs.data(), client->n_send_iov) == 0)
                continue;

            // Will only fail if iovs is too large (> 4) or nng runs OOM
            if (int rv = nng_aio_set_iov(client->aio, client->n_send_iov, client->send_iovs.data()); rv != 0)
            {
                spdlog::warn("Failed to set IOVs for client {}: {}", client->remoteAddress(), nng_strerror(rv));
                // Mark all data as sent to avoid scheduling further sends
                client->n_send_iov = 0;
                continue;
            }

            nng_stream_send(client->stream, client->aio);
            didScheduleASend = true;
        }

        if (!didScheduleASend)
            break;

        // Wait for all scheduled sends to complete
        for (auto &client: clients_)
        {
            nng_aio_wait(client->aio);
            assert(!nng_aio_busy(client->aio));

            if (int rv = nng_aio_result(client->aio))
            {
                spdlog::warn("Send to client {} failed: {}", client->remoteAddress(), nng_strerror(rv));
                // Mark all data as sent to avoid scheduling further sends
                client->n_send_iov = 0;
                continue;
            }

            size_t sentSize = nng_aio_count(client->aio);
            auto totalSize = nng_iov_total_size(client->send_iovs.data(), client->n_send_iov);
            client->n_send_iov = subtract_from_iovs(client->send_iovs, client->n_send_iov, sentSize);
            auto newTotalSize = nng_iov_total_size(client->send_iovs.data(), client->n_send_iov);
            assert(newTotalSize + sentSize == totalSize);
        }
    } while (didScheduleASend);

    assert(!need_more_sends(clients_));
}
#endif

ssize_t StreamServer::sendToAllClients(const u8 *data, size_t size)
{
    IovArray iov;
    iov[0] = nng_iov{ static_cast<void *>(const_cast<u8 *>(data)), size };

    return d->sendToAllClients(iov, 1);
}

ssize_t StreamServer::sendToAllClients(const std::vector<IOV> &iov)
{
    if (iov.size() > NngMaxIOVs)
    {
        spdlog::error("sendToAllClients called with too many IOVs: {} (max {})", iov.size(), NngMaxIOVs);
        return -1;
    }

    IovArray nng_iovs;
    std::transform(iov.begin(), iov.end(), nng_iovs.begin(),
                   [](const IOV &iov)
                   {
                       return nng_iov{iov.buf, iov.len};
                   });

    return d->sendToAllClients(nng_iovs, iov.size());
}

} // namespace mesytec::mvlc
