#include "mesytec-mvlc/mvlc_listfile_stream_server.h"
#include <thread>

#include "mesytec-mvlc/stream_server_asio.h"
#include "mesytec-mvlc/util/fmt.h"
#include "mesytec-mvlc/util/logging.h"

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
    // Hack to give clients time to connect. TODO: implement a real solution
    // (flueke 2026: taken from the zmq listfile code).
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

StreamServerListfileWriteHandle::~StreamServerListfileWriteHandle() {}

size_t StreamServerListfileWriteHandle::write(const u8 *data, size_t size)
{
    auto res = d->streamServer_.sendToAllClients(data, size);

    if (res != 0)
    {
        auto logger = get_logger("mvlc_listfile_stream_server");

        if (res == std::numeric_limits<size_t>::max())
        {
            logger->error("StreamServerListfileWriteHandle::write(): internal server error "
                          "while trying to send listfile data");
        }
        else
        {
            logger->warn("Failed to send listfile data to {} clients", res);
        }
    }
    return size;
}
} // namespace mesytec::mvlc::listfile
