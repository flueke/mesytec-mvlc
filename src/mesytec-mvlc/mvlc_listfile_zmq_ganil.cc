#include <chrono>
#include <thread>
#include <zmq.hpp>
#include "mesytec-mvlc/mvlc_listfile_zmq_ganil.h"
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

struct ZmqGanilWriteHandle::Private
{
    std::shared_ptr<spdlog::logger> logger;
    zmq::context_t ctx;
    zmq::socket_t pub;
};

ZmqGanilWriteHandle::ZmqGanilWriteHandle()
    : d(std::make_unique<Private>())
{
    d->logger = get_logger("mvlc_listfile_zmq_ganil");
    //d->ctx = zmq::context_t(ZmqIoThreads);
    d->ctx = zmq::context_t();
    d->pub = zmq::socket_t(d->ctx, ZMQ_PUB);

    // linger equal to 0 for a fast socket shutdown
    int linger = 0;
#if CPPZMQ_VERSION > ZMQ_MAKE_VERSION(4, 7, 0)
    d->pub.set(zmq::sockopt::linger, linger);
#else
    d->pub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
#endif

    try
    {
        d->pub.bind(ZmqUrl.c_str());
        d->logger->info("zmq server listening on {}", ZmqUrl);
        // Hack to give clients time to connect. TODO: implement a real solution
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    catch (const zmq::error_t &e)
    {
        d->logger->error("Error binding zmq socket to {}: {}", ZmqUrl, e.what());
        throw std::runtime_error(
            fmt::format("Error binding zmq socket to {}: {}", ZmqUrl, e.what()));
    }
}

ZmqGanilWriteHandle::~ZmqGanilWriteHandle()
{
    d->logger->info("Closing zmq server");
}

size_t ZmqGanilWriteHandle::write(const u8 *data, size_t size)
{
    try
    {
        d->logger->trace("Publishing message of size {}", size);
        // TODO: fix the depcrecation warning
#if CPPZMQ_VERSION >= ZMQ_MAKE_VERSION(4, 3, 1)
        if (auto sendResult = d->pub.send(zmq::const_buffer{ data, size }))
            return sendResult.value();
        return {};
#else
        return d->pub.send(data, size);
#endif
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
