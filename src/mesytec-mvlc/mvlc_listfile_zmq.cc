#include <zmq.hpp>
#include "mesytec-mvlc/mvlc_listfile_zmq.h"
#include "mesytec-mvlc/util/logging.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

static const std::string ZmqPort = "5575";
static const std::string ZmqUrl = "tcp://*:" + ZmqPort;
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
        d->pub.bind(ZmqUrl.c_str());
        d->logger->info("zmq server listening on {}", ZmqUrl);
    }
    catch (const zmq::error_t &e)
    {
        d->logger->error("Error binding zmq socket to {}: {}", ZmqUrl, e.what());
        throw std::runtime_error(
            fmt::format("Error binding zmq socket to {}: {}", ZmqUrl, e.what()));
    }
}

ZmqWriteHandle::~ZmqWriteHandle()
{
    d->logger->info("Closing zmq server");
}

size_t ZmqWriteHandle::write(const u8 *data, size_t size)
{
    try
    {
        d->logger->trace("Publishing message of size {}", size);
        // TODO: fix the depcrecation warning
        return d->pub.send(data, size);
    }
    catch (const zmq::error_t &e)
    {
        throw std::runtime_error(
            fmt::format("Failed publishing listfile data on zmq socket: {}", e.what()));
    }
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
