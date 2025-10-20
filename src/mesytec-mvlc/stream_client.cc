#include "stream_client.h"
#include <mesytec-mvlc/util/nng_util.h>
#include <spdlog/spdlog.h>

namespace mesytec::mvlc
{

struct StreamClient::Private
{
    nng_aio *dial_aio = nullptr;
    nng_stream_dialer *dialer = nullptr;
    nng_stream *stream = nullptr;
    nng_aio *recv_aio = nullptr;
};

StreamClient::StreamClient()
    : d(std::make_unique<Private>())
{
}

StreamClient::~StreamClient()
{
    disconnect();

    if (d->stream)
    {
        nng_stream_free(d->stream);
        d->stream = nullptr;
    }

    if (d->dialer)
    {
        nng_stream_dialer_free(d->dialer);
        d->dialer = nullptr;
    }

    if (d->dial_aio)
    {
        nng_aio_free(d->dial_aio);
        d->dial_aio = nullptr;
    }

    if (d->recv_aio)
    {
        nng_aio_free(d->recv_aio);
        d->recv_aio = nullptr;
    }
}

void dial_callback(void *arg)
{
    auto client = static_cast<StreamClient *>(arg);
    assert(!client->d->stream);

    if (int rv = nng_aio_result(client->d->dial_aio))
    {
        spdlog::error("StreamClient: Dial failed: {}", nng_strerror(rv));
        return;
    }

    client->d->stream = static_cast<nng_stream *>(nng_aio_get_output(client->d->dial_aio, 0));
}

bool StreamClient::connect(const std::string &uri)
{
    if (isConnected())
    {
        spdlog::warn("StreamClient::connect: already connected");
        return false;
    }

    if (!d->dial_aio)
    {
        if (int rv = nng_aio_alloc(&d->dial_aio, dial_callback, this))
        {
            spdlog::error("StreamClient: Failed to allocate AIO: {}", nng_strerror(rv));
            throw NngException(rv);
        }
    }

    if (!d->dialer)
    {
        if (int rv = nng_stream_dialer_alloc(&d->dialer, uri.c_str()))
        {
            spdlog::error("StreamClient: Failed to allocate dialer: {}", nng_strerror(rv));
            nng_aio_free(d->dial_aio);
            d->dial_aio = nullptr;
            throw NngException(rv);
        }
    }

    nng_stream_dialer_dial(d->dialer, d->dial_aio);
    nng_aio_wait(d->dial_aio);

    return isConnected();
}

void StreamClient::disconnect()
{
    if (d->dial_aio)
    {
        nng_aio_stop(d->dial_aio);
        nng_aio_free(d->dial_aio);
        d->dial_aio = nullptr;
    }

    if (d->dialer)
    {
        nng_stream_dialer_free(d->dialer);
        d->dialer = nullptr;
    }
}

bool StreamClient::isConnected() const
{
    return d->stream != nullptr;
}

std::string StreamClient::remoteAddress() const
{
    if (!d->stream)
        return {};

    nng_sockaddr addr{};
    nng_stream_get_addr(d->stream, NNG_OPT_REMADDR, &addr);
    return nng_sockaddr_to_string(addr);
}

void recv_callback(void *arg)
{
}

ssize_t StreamClient::receive(u8 *buffer, size_t size)
{
    if (!isConnected())
    {
        spdlog::error("StreamClient::receive: not connected");
        return -1;
    }

    std::array<nng_iov, 1> iovs = {{{buffer, size}}};

    if (!d->recv_aio)
    {
        if (int rv = nng_aio_alloc(&d->recv_aio, recv_callback, this))
        {
            spdlog::error("StreamClient::receive: Failed to allocate recv AIO: {}", nng_strerror(rv));
            return -1;
        }
    }

    if (int rv = nng_aio_set_iov(d->recv_aio, iovs.size(), iovs.data()))
    {
        spdlog::error("StreamClient::receive: Failed to set IOVs: {}", nng_strerror(rv));
        return -1;
    }

    nng_stream_recv(d->stream, d->recv_aio);
    nng_aio_wait(d->recv_aio);
    assert(!nng_aio_busy(d->recv_aio));

    if (int rv = nng_aio_result(d->recv_aio))
    {
        spdlog::error("StreamClient::receive: Receive failed: {}", nng_strerror(rv));
        return -1;
    }

    return size;
}

}
