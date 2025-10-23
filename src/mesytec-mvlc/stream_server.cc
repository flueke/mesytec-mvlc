#include "stream_server.h"

#include <algorithm>
#include <mutex>
#include <mesytec-mvlc/util/nng_util.h>

#include <spdlog/spdlog.h>

namespace mesytec::mvlc
{

struct Acceptor
{
    StreamServer *ctx = nullptr;
    nng_stream_listener *listener = nullptr;
    nng_aio *accept_aio = nullptr;
};

struct Client: public StreamServer::IClient
{
    nng_stream *stream = nullptr;
    nng_aio *aio = nullptr;

    explicit Client(nng_stream *s)
        : stream(s)
        , aio(nullptr)
    {
        if (int rv = nng_aio_alloc(&aio, nullptr, nullptr))
        {
            spdlog::error("Failed to allocate client AIO: {}", nng_strerror(rv));
            throw NngException(rv);
        }
    }

    ~Client()
    {
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

struct StreamServer::Private
{
    std::vector<std::unique_ptr<Acceptor>> acceptors;
    std::vector<std::unique_ptr<Client>> clients;
    std::mutex clients_mutex;
};

void accept_callback(void *arg);

void start_accept(Acceptor *acceptor)
{
    if (!acceptor->accept_aio)
    {
        if (int rv = nng_aio_alloc(&acceptor->accept_aio, accept_callback, acceptor))
        {
            spdlog::error("Failed to allocate accept AIO: {}", nng_strerror(rv));
            return;
        }
    }

    nng_aio_set_timeout(acceptor->accept_aio, 100);
    nng_stream_listener_accept(acceptor->listener, acceptor->accept_aio);
}

// TODO: the shutdown flag has gone. shutdown is not handled. figure out if it's
// possible without the flag, otherwise add it back in.
void accept_callback(void *arg)
{
    auto acceptor = static_cast<Acceptor *>(arg);

    if (!acceptor->accept_aio)
        return;

    if (int rv = nng_aio_result(acceptor->accept_aio))
    {
        if (rv != NNG_ETIMEDOUT)
        {
            spdlog::error("Accept failed: {}", nng_strerror(rv));
        }

        if (rv != NNG_ECANCELED)
        {
            start_accept(acceptor);
        }

        return;
    }

    // Retrieve the nng stream object from the aio
    nng_stream *stream = static_cast<nng_stream *>(nng_aio_get_output(acceptor->accept_aio, 0));

    if (!stream)
    {
        spdlog::error("Accepted null stream");
        start_accept(acceptor);
        return;
    }

    try
    {
        auto client = std::make_unique<Client>(stream);

        // Add to client list
        {
            std::lock_guard<std::mutex> lock(acceptor->ctx->d->clients_mutex);
            acceptor->ctx->d->clients.emplace_back(std::move(client));
            spdlog::info("Accepted new connection from {}",
                         acceptor->ctx->d->clients.back()->remoteAddress());
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
    acceptor->ctx = this;

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
    for (auto &acceptor: d->acceptors)
    {
        if (acceptor->accept_aio)
        {
            // nng_aio_cancel(acceptor.accept_aio);
            // nng_aio_wait(acceptor.accept_aio);
            nng_aio_stop(acceptor->accept_aio);
            nng_aio_free(acceptor->accept_aio);
        }
        // XXX: this might be enough to detect 'stop' in accept_callback
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

ssize_t StreamServer::sendToAllClients(const u8 *data, size_t size)
{
    std::unique_lock<std::mutex> lock(d->clients_mutex);
    if (d->clients.empty())
    {
        return 0; // No clients to send to
    }

    auto &clients = d->clients;
    std::array<nng_iov, 1> iovs = {{{const_cast<u8 *>(data), size}}};

    // Setup sends for each client
    for (auto &client: clients)
    {
        assert(client->stream);
        assert(client->aio);

        [[maybe_unused]] int rv = nng_aio_set_iov(client->aio, iovs.size(), iovs.data());
        assert(rv == 0); // will only fail if iov is too large

        nng_stream_send(client->stream, client->aio);
    }

    std::vector<size_t> clientsToRemove;
    ssize_t ret = 0;

    for (auto it = clients.begin(); it != clients.end(); ++it)
    {
        auto &client = *it;
        nng_aio_wait(client->aio);
        assert(!nng_aio_busy(client->aio));

        if (int rv = nng_aio_result(client->aio))
        {
            spdlog::warn("Send to failed: {}", nng_strerror(rv));
            clientsToRemove.push_back(std::distance(clients.begin(), it));
        }
        else
        {
            ++ret;
        }
    }

    // reverse sort the indexes so we start removing from the end
    std::sort(std::begin(clientsToRemove), std::end(clientsToRemove), std::greater<size_t>());

    for (auto index: clientsToRemove)
    {
        spdlog::info("Removing client at index {}", index);
        clients.erase(clients.begin() + index);
    }

    return ret;
}

} // namespace mesytec::mvlc
