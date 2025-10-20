#include <algorithm>
#include <nng/nng.h>

#include <mesytec-mvlc/stream_server.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <mesytec-mvlc/util/storage_sizes.h>

#include "mvlc_stream_test_support.h"

using namespace mesytec;
using namespace mesytec::mvlc;

static const std::vector<std::string> listenUris = {
    "tcp4://*:42333",
    "tcp4://*:42334",
    "ipc:///tmp/mvlc_stream_test_server.ipc",
    "ipc:///tmp/mvlc_stream_test_server2.ipc",
};

int main(int argc, char **argv)
{
    mvlc::set_global_log_level(spdlog::level::debug);
    mvlc::util::setup_signal_handlers();

    {
        StreamServer server;
        if (!server.listen(listenUris))
        {
            spdlog::error("Failed to start StreamServer");
            return 1;
        }

        util::Stopwatch swReport;
        size_t iteration = 0;
        size_t totalBytesSent = 0;
        size_t bytesSentInInterval = 0;

        while (!mvlc::util::signal_received())
        {
            if (auto interval = swReport.get_interval(); interval >= std::chrono::seconds(1))
            {
                auto clients = server.clients();
                spdlog::info("Main loop iteration {}, {} clients connected: {}", iteration,
                            clients.size(), fmt::join(clients, ", "));
                spdlog::info("  Sent {} MB in last {} ms, rate={} MB/s (total {} MB sent)",
                    bytesSentInInterval / util::Megabytes(1) * 1.0,
                    std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(),
                    bytesSentInInterval / util::Megabytes(1) * 1.0 /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() * 1.0 / 1000.0),
                    totalBytesSent / util::Megabytes(1) * 1.0
                );
                swReport.interval();
                bytesSentInInterval = 0;
            }

            auto data = generateTestBuffer(static_cast<uint32_t>(iteration), 1u << 19);
            auto res = send_to_all_clients(&server, reinterpret_cast<const u8 *>(data.data()), data.size() * sizeof(u32));

            if (res < 0)
            {
                spdlog::error("Failed to send data to all clients");
                return 1;
            }
            else if (res > 0)
            {
                totalBytesSent += data.size() * sizeof(u32);
                bytesSentInInterval += data.size() * sizeof(u32);
            }

            ++iteration;
        }
    }

    spdlog::info("left main scope. StreamServer instance got destroyed");
    nng_fini(); // Note: don't do this in a real application. It destroys state that NNG needs to
                // operate. Helps valgrind though.
}
