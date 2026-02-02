#include "mesytec-mvlc/mvlc_listfile_stream_server.h"
#include "mesytec-mvlc/stream_server_asio.h"

namespace mesytec::mvlc::listfile
{

struct StreamServerListfileWriteHandle::Private
{
    mvlc::StreamServer streamServer_;
};

StreamServerListfileWriteHandle::StreamServerListfileWriteHandle(const std::string &bindAddress)
    : d(std::make_unique<Private>())
{
    if (!d->streamServer_.listen(bindAddress))
    {
        throw std::runtime_error(
            "StreamServerListfileWriteHandle: Failed to start stream server on " + bindAddress);
    }
}

StreamServerListfileWriteHandle::~StreamServerListfileWriteHandle() {}

size_t StreamServerListfileWriteHandle::write(const u8 *data, size_t size)
{
    auto res = d->streamServer_.sendToAllClients(data, size);
    if (res != 0 || res == std::numeric_limits<size_t>::max())
    {
        throw std::runtime_error(
            "StreamServerListfileWriteHandle::write(): failed to send data to all clients");
    }
    return size;
}
} // namespace mesytec::mvlc::listfile
