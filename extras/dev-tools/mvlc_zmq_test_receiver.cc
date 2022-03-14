#include <mesytec-mvlc/mesytec-mvlc.h>
#include <zmq.hpp>
#include <iostream>
#include <signal.h>

using namespace mesytec::mvlc;

static volatile bool g_signal_received = false;

void signal_handler(int signum)
{
#ifndef _WIN32
    g_signal_received = true;
#endif
}

void setup_signal_handlers()
{
#ifndef _WIN32
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
#endif
}

int main(int argc, char *argv[])
{
    setup_signal_handlers();

    zmq::context_t ctx;
    zmq::socket_t sub(ctx, ZMQ_SUB);
    int timeout = 500; // milliseconds
    sub.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(int));
    std::string pubUrl = "tcp://localhost:5575";
    sub.setsockopt(ZMQ_SUBSCRIBE, "", 0);

    while (true)
    {
        try
        {
            spdlog::info("Connecting to publisher {}", pubUrl);
            sub.connect(pubUrl);
            break;
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Could not connect to publisher {}", pubUrl);
        }
    }

    size_t nMessages = 0;
    size_t nBytes = 0;
    auto tStart = std::chrono::steady_clock::now();

    while (true)
    {
        try
        {
            if (g_signal_received)
            {
                spdlog::info("Interrupted, leaving read loop");
                break;
            }

            spdlog::debug("Calling sub.recv()");
            zmq::message_t msg;
            if (sub.recv(msg))
            {
#if 0
                spdlog::info("Received message of size {}", msg.size());
                basic_string_view<u32> bufferView(
                    reinterpret_cast<const u32 *>(msg.data()),
                    msg.size() / sizeof(u32));

                util::log_buffer(std::cout, bufferView, fmt::format("Message {}", nMessages),
                                 10, 10);
#endif

                ++nMessages;
                nBytes += msg.size();

                if (nMessages == 1)
                {
                    spdlog::info("Received first message of size {}", msg.size());
                    basic_string_view<u32> bufferView(
                        reinterpret_cast<const u32 *>(msg.data()),
                        msg.size() / sizeof(u32));

                    util::log_buffer(std::cout, bufferView, fmt::format("Message {}", nMessages),
                                     20, 10);
                }
            }
            else
            {
                spdlog::debug("Timeout waiting for message from publisher");
            }
        } catch (const std::exception &e)
        {
            spdlog::warn("Error from sub.recv(): {}", e.what());
        }
    }

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
    double elapsed_s = elapsed_ms.count() / 1000.0;
    double messagesPerSecond = static_cast<double>(nMessages) / elapsed_s;
    double bytesPerSecond = static_cast<double>(nBytes) / elapsed_s;
    double mbPerSecond = bytesPerSecond / (1024 * 1024);

    spdlog::info("nMessages={}, nBytes={}, elapsed={}s, messageRate={}msg/s, byteRate={}B/s, byteRate={}MB/s",
                 nMessages, nBytes, elapsed_s, messagesPerSecond, bytesPerSecond, mbPerSecond);

    return 0;
}
