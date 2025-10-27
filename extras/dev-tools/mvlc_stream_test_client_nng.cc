#include <iostream>
#include <vector>

#include <nng/nng.h> // for nng_fini() only

#include <mesytec-mvlc/stream_client.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/signal_handling.h>

#include "mvlc_stream_test_support.h"

using namespace mesytec;
using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    mvlc::set_global_log_level(spdlog::level::debug);
    //mvlc::util::setup_signal_handlers();

    {
        mesytec::mvlc::StreamClient client;
        std::string uri = "tcp://0.0.0.0:42333";

        if (argc > 1)
        {
            uri = argv[1];
        }

        if (!client.connect(uri.c_str()))
        {
            std::cerr << "Failed to connect to stream server\n";
            return 1;
        }

        std::vector<u8> readBuffer(2 * (1u << 20));

        while (!mvlc::util::signal_received())
        {
            ssize_t nBytes = client.receive(readBuffer.data(), readBuffer.size());

            if (nBytes < 0)
            {
                std::cerr << "Receive error\n";
                break;
            }

            //std::cout << "Received " << nBytes << " bytes from server\n";
        }
    }

    spdlog::info("left main scope. StreamClient instance got destroyed");
    nng_fini(); // Note: don't do this in a real application. It destroys state that NNG needs to
                // operate. Helps valgrind though.
}
