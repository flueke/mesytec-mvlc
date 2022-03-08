#include <zmq.hpp>
#include "mesytec-mvlc/mvlc_listfile_zmq.h"
#include "mesytec-mvlc/util/logging.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

static const std::string Port = "5575";
static const std::string Url = "tcp://*" + Port;
//static const int ZmqIoThreads = 1;

struct ZmqWriteHandle::Private
{
    std::shared_ptr<spdlog::logger> logger;
    zmq::context_t ctx;
    zmq::socket_t pub;
};

ZmqWriteHandle::ZmqWriteHandle()
    : d(std::make_unique<Private>())
{
    d->logger = get_logger("mvlc_listfile_zmq");
    //d->ctx = zmq::context_t(ZmqIoThreads);
    d->ctx = zmq::context_t();
    d->pub = zmq::socket_t(d->ctx, ZMQ_PUB);

    // linger equal to 0 for a fast socket shutdown
    int linger = 0;
    d->pub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

    try
    {
        d->pub.bind(Url.c_str());
    }
    catch (const zmq::error_t &e)
    {
        d->logger->error("Error binding zmq socket to {}: {}", Url, e.what());
        throw std::runtime_error(fmt::format("Error binding zmq socket to {}: {}", Url, e.what()));
    }

    d->logger->info("zmq server listening on {}", Url);
}

ZmqWriteHandle::~ZmqWriteHandle()
{
    d->logger->info("Closing zmq server");
}

size_t ZmqWriteHandle::write(const u8 *data, size_t size)
{
    try
    {
        // TODO: fix the depcrecation warning
        return d->pub.send(data, size);
    }
    catch (const zmq::error_t &)
    {
        throw std::runtime_error("Failed publishing listfile data on zmq socket.");
    }
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
