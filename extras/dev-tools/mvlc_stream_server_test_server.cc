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
    "tcp4://localhost:42333",
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

        std::vector<u8> sendBuffer;
        size_t iteration = 0;
        size_t totalBytesSent = 0;
        size_t bytesSentInInterval = 0;
        size_t buffersSentInInterval = 0;
        util::Stopwatch swReport;

        //generate_test_data(sendBuffer, static_cast<u32>(iteration), 1u << 19);

        while (!mvlc::util::signal_received())
        {
            if (auto interval = swReport.get_interval(); interval >= std::chrono::seconds(1))
            {
                auto clients = server.clients();
                spdlog::info("Main loop iteration {}, {} clients connected: {}", iteration,
                            clients.size(), fmt::join(clients, ", "));
                spdlog::info("  Sent {:.2f} MB ({} buffers) in the last {} ms, rate={:.2f} MB/s ({:.2f} buffers/s) (total {} MB sent)",
                    bytesSentInInterval / util::Megabytes(1) * 1.0,
                    buffersSentInInterval,
                    std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(),
                    bytesSentInInterval / util::Megabytes(1) * 1.0 /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() * 1.0 / 1000.0),
                    buffersSentInInterval /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() * 1.0 / 1000.0),
                    totalBytesSent / util::Megabytes(1) * 1.0
                );
                swReport.interval();
                bytesSentInInterval = 0;
                buffersSentInInterval = 0;
            }

            generate_test_data(sendBuffer, static_cast<u32>(iteration), 0xffff);

            auto bufferView = std::basic_string_view<std::uint32_t>(
                reinterpret_cast<const std::uint32_t *>(sendBuffer.data()),
                std::min(sendBuffer.size(), static_cast<std::size_t>(10)));

            spdlog::debug("Generated test buffer {} of size {} words: {:#010x}", iteration, sendBuffer.size() / sizeof(u32),
                fmt::join(bufferView, ", "));

            auto res = server.sendToAllClients(reinterpret_cast<const u8 *>(sendBuffer.data()), sendBuffer.size() * sizeof(u32));
            spdlog::debug("Sent buffer {} of size {} words", iteration, sendBuffer.size());

            if (res < 0)
            {
                spdlog::error("Failed to send data to all clients");
                return 1;
            }
            else if (res > 0)
            {
                totalBytesSent += sendBuffer.size();
                bytesSentInInterval += sendBuffer.size();
                ++buffersSentInInterval;
                //spdlog::debug("Generated test buffer of size {} MB", data.size() / util::Megabytes(1) * 1.0);
            }

            ++iteration;
        }
    }

    spdlog::info("left main scope. StreamServer instance got destroyed");
    nng_fini(); // Note: don't do this in a real application. It destroys state that NNG needs to
                // operate. Helps valgrind though.
}
