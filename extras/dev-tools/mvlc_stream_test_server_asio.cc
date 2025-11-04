#include <algorithm>
#include <iostream>

#include <argh.h>
#include <asio.hpp>
#include <mesytec-mvlc/stream_server_asio.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <mesytec-mvlc/mvlc_stream_test_support.h>

using namespace mesytec;
using namespace mesytec::mvlc;

static const std::vector<std::string> listenUris = {
    "tcp4://127.0.0.1:42333",
    "tcp4://0.0.0.0:42334",
//#if 0
#ifdef ASIO_HAS_LOCAL_SOCKETS
    "ipc:///tmp/mvlc_stream_test_server_asio.ipc",
    //"ipc:///tmp/mvlc_stream_test_server_asio2.ipc",
#endif
};

int main(int argc, char **argv)
{
    mvlc::util::setup_signal_handlers();
    spdlog::set_level(spdlog::level::info);
    mvlc::set_global_log_level(spdlog::level::info);

    argh::parser parser({"-h", "--help", "--log-level", "--trace", "--debug", "--info", "--warn", "--buffer-size"});
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
                  << " [--log-level level][--trace][--debug][--info][--warn][--buffer-size <words>]\n";
        return 0;
    }

    std::string arg;
    size_t bufferSizeWords = (1ul << 20) / sizeof(u32); // 1 MB default

    if (parser("--buffer-size") >> arg)
    {
        bufferSizeWords = std::stoul(arg);
    }

    spdlog::info("Using test buffer size of {} words, {:0.2f} MB", bufferSizeWords, bufferSizeWords * sizeof(u32) / (util::Megabytes(1) * 1.0));

    {
        StreamServerAsio server;
        if (!server.listen(listenUris))
        {
            spdlog::error("Failed to start StreamServerAsio");
            return 1;
        }

        std::vector<u8> sendBuffer;
        size_t iteration = 0;
        size_t totalBytesSent = 0;
        size_t bytesSentInInterval = 0;
        size_t buffersSentInInterval = 0;
        util::Stopwatch swReport;

        generate_test_data(sendBuffer, static_cast<u32>(iteration), bufferSizeWords);
        assert(verify_test_data({sendBuffer.data(), sendBuffer.size()}, static_cast<u32>(iteration)));

        while (!mvlc::util::signal_received())
        {
            if (auto interval = swReport.get_interval(); interval >= std::chrono::seconds(1))
            {
                auto clients = server.clients();
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
            //assert(verify_test_data(sendBuffer, static_cast<u32>(iteration)));

            auto bufferView = std::basic_string_view<std::uint32_t>(
                reinterpret_cast<const std::uint32_t *>(sendBuffer.data()),
                std::min(sendBuffer.size() / sizeof(u32), static_cast<std::size_t>(10)));

            spdlog::trace("Generated test buffer {} of size {} words, {} bytes: {:#010x} ...",
                          iteration, sendBuffer.size() / sizeof(u32), sendBuffer.size(),
                          fmt::join(bufferView, ", "));

            auto res = server.sendToAllClients(reinterpret_cast<const u8 *>(sendBuffer.data()),
                                               sendBuffer.size());
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
            }

            spdlog::trace("Sent buffer {} of size {} bytes to {} clients", iteration,
                          sendBuffer.size(), res);

            if (res == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

            ++iteration;
        }
    }

    spdlog::info("left main scope. StreamServerAsio instance got destroyed");
}
