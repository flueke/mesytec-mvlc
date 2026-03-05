#include <algorithm>
#include <iostream>

#include <argh.h>
#include <mesytec-mvlc/mvlc_stream_test_support.h>
#include <mesytec-mvlc/stream_server_asio.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <mesytec-mvlc/util/storage_sizes.h>

using namespace mesytec;
using namespace mesytec::mvlc;

static const std::vector<std::string> listenUris = {
    "tcp4://127.0.0.1:42333",
    "tcp4://127.0.0.1:42334",
    "ipc:///tmp/mvlc_stream_test_server_asio.ipc",
    "ipc:///tmp/mvlc_stream_test_server_asio.ipc2",
};

int main(int argc, char **argv)
{
    // mvlc::util::setup_signal_handlers();
    spdlog::set_level(spdlog::level::trace);
    mvlc::set_global_log_level(spdlog::level::trace);

    argh::parser parser({"-h", "--help", "--log-level", "--trace", "--debug", "--info", "--warn",
                         "--buffer-size", "--time-to-run"});
    parser.parse(argc, argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";
        else if (parser["--info"])
            logLevelName = "info";
        else if (parser["--warn"])
            logLevelName = "warn";

        if (!logLevelName.empty())
        {
            spdlog::set_level(spdlog::level::from_str(logLevelName));
            mvlc::set_global_log_level(spdlog::level::from_str(logLevelName));
        }
    }

    if (parser[{"-h", "--help"}])
    {
        std::cout << "Usage: " << argv[0]
                  << " [--log-level level][--trace][--debug][--info][--warn][--buffer-size "
                     "<words>][--time-to-run <seconds>]\n";
        return 0;
    }

    std::string arg;
    size_t bufferSizeWords = (1ul << 20) / sizeof(u32); // 1 MB default

    if (parser("--buffer-size") >> arg)
    {
        bufferSizeWords = std::stoul(arg);
    }

    std::chrono::seconds timeToRun(10);

    if (parser("--time-to-run") >> arg)
    {
        timeToRun = std::chrono::seconds(std::stoul(arg));
    }

    spdlog::info("Using test buffer size of {} words, {:0.2f} MB", bufferSizeWords,
                 bufferSizeWords * sizeof(u32) / (util::Megabytes(1) * 1.0));

    {
        stream::StreamServer server;

        for (const auto &uri: listenUris)
        {
            if (!server.listen(uri))
            {
                spdlog::error("Failed to listen on {}", uri);
                continue;
            }
            else
                spdlog::info("Listening on {}", uri);
        }

        std::vector<u8> sendBuffer;
        size_t iteration = 0;
        size_t totalBytesSent = 0;
        size_t bytesSentInInterval = 0;
        size_t buffersSentInInterval = 0;
        util::Stopwatch swReport;

        generate_test_data(sendBuffer, static_cast<u32>(iteration), bufferSizeWords);
        assert(
            verify_test_data({sendBuffer.data(), sendBuffer.size()}, static_cast<u32>(iteration)));

        util::Stopwatch swRunning;
        while (timeToRun.count() == 0 || swRunning.get_elapsed() < timeToRun)
        {
            if (auto interval = swReport.get_interval(); interval >= std::chrono::seconds(1))
            {
                auto clients = server.clientAddresses();
                spdlog::info("Main loop iteration {}, {} clients connected: {}", iteration,
                             clients.size(), fmt::join(clients, ", "));
                spdlog::info(
                    "  Sent {:.2f} MB ({} buffers) in the last {} ms, rate={:.2f} MB/s ({:.2f} "
                    "buffers/s) (total {} MB sent)",
                    bytesSentInInterval / util::Megabytes(1) * 1.0, buffersSentInInterval,
                    std::chrono::duration_cast<std::chrono::milliseconds>(interval).count(),
                    bytesSentInInterval / util::Megabytes(1) * 1.0 /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),
                    buffersSentInInterval /
                        (std::chrono::duration_cast<std::chrono::milliseconds>(interval).count() *
                         1.0 / 1000.0),
                    totalBytesSent / util::Megabytes(1) * 1.0);
                swReport.interval();
                bytesSentInInterval = 0;
                buffersSentInInterval = 0;
            }

            generate_test_data(sendBuffer, static_cast<u32>(iteration), bufferSizeWords, false);
            // assert(verify_test_data(sendBuffer, static_cast<u32>(iteration)));

#if 0
            auto bufferView = std::basic_string_view<std::uint32_t>(
                reinterpret_cast<const std::uint32_t *>(sendBuffer.data()),
                std::min(sendBuffer.size() / sizeof(u32), static_cast<std::size_t>(10)));

            spdlog::trace("Generated test buffer {} of size {} words, {} bytes: {:#010x} ...",
                          iteration, sendBuffer.size() / sizeof(u32), sendBuffer.size(),
                          fmt::join(bufferView, ", "));
#endif

            const auto clientCount = server.clientCount();
            auto res = server.sendToAllClients(reinterpret_cast<const u8 *>(sendBuffer.data()),
                                               sendBuffer.size());
            if (res > 0)
            {
                spdlog::warn("Failed to send data to all clients");
            }
            else if (clientCount > 0)
            {
                totalBytesSent += sendBuffer.size();
                bytesSentInInterval += sendBuffer.size();
                ++buffersSentInInterval;
            }
            else if (clientCount == 0)
            {
                // spdlog::trace("No clients connected, not sending data, sleeping a bit because I
                // can");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // spdlog::trace("Sent buffer {} of size {} bytes to {} clients", iteration,
            //               sendBuffer.size(), server.clientCount());

            ++iteration;
        }
    }

    spdlog::info("left main scope. StreamServerAsio instance got destroyed");
}
