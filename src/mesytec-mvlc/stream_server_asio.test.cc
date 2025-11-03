#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "mesytec-mvlc/mvlc_stream_test_support.h"
#include "mesytec-mvlc/stream_server_asio.h"
#include "mesytec-mvlc/util/string_util.h"

#include "gtest/gtest.h"
#include <asio.hpp>

using namespace mesytec::mvlc;
using namespace std::chrono_literals;

class StreamServerTestBase: public ::testing::TestWithParam<std::vector<std::string>>
{
  protected:
    void SetUp() override
    {
        server = std::make_unique<StreamServerAsio>();
        ASSERT_TRUE(server->listen(GetParam()));
        ASSERT_TRUE(server->isListening());
    }

    void TearDown() override
    {
        if (server)
        {
            server->stop();
            ASSERT_FALSE(server->isListening());
        }
    }

    std::unique_ptr<StreamServerAsio> server;
    static constexpr size_t TEST_BUFFER_SIZE = 1024;
};

TEST(StreamServerTest, CanListen)
{
    StreamServerAsio server;
    EXPECT_FALSE(server.isListening());

    auto ipcPath =
        fmt::format("ipc://{}.ipc", (std::filesystem::temp_directory_path() /
                                     ("mvlc_test_stream_server_asio-" + std::to_string(getpid())))
                                        .string());

    for (size_t i = 0; i < 1; ++i)
    {
        // for tcp let the OS pick a port
        ASSERT_TRUE(server.listen("tcp://127.0.0.1:0"));
        ASSERT_TRUE(server.listen("tcp://127.0.0.1:0"));
        ASSERT_TRUE(server.listen(ipcPath));

        spdlog::info("Listening URIs: {}", fmt::join(server.listenUris(), ", "));

        EXPECT_TRUE(server.isListening());
        ASSERT_EQ(server.listenUris().size(), 3u);

        server.stop();

        ASSERT_FALSE(server.isListening());
        ASSERT_EQ(server.listenUris().size(), 0u);
    }
}

TEST_P(StreamServerTestBase, OneSenderOneClient)
{
    auto uri = server->listenUris().front();

    if (!util::startswith(uri, "tcp://"))
    {
        GTEST_SKIP() << "Skipping non-tcp transport test (for now)";
    }

    std::atomic<bool> quitClient = false;

    std::thread clientThread(
        [&uri, &quitClient]()
        {
            asio::io_context io_context;
            asio::ip::tcp::resolver resolver(io_context);
            asio::ip::tcp::resolver::query query("localhost", "42333");
            auto endpoints = resolver.resolve(query);
            ASSERT_FALSE(endpoints.empty());
            asio::ip::tcp::socket socket(io_context);
            asio::connect(socket, endpoints);
            ASSERT_TRUE(socket.is_open());

            while (!quitClient) {}
        });

    while (server->clients().empty())
    {
        std::this_thread::sleep_for(1ms);
    }

    ASSERT_TRUE(server->clients().size() == 1);
    quitClient = true;
    if (clientThread.joinable())
        clientThread.join();


    std::vector<u8> send_buffer;
    generate_test_data(send_buffer, 4711, TEST_BUFFER_SIZE);

    ASSERT_EQ(server->sendToAllClients(send_buffer.data(), send_buffer.size()), 1);
}

INSTANTIATE_TEST_SUITE_P(StreamServerTest, StreamServerTestBase,
                         ::testing::Values(std::vector<std::string>{"tcp://localhost:42333"},
                                           std::vector<std::string>{
                                               "ipc:///tmp/mvlc_test_stream_server_asio.ipc"}),
                         [](const auto &info)
                         {
                             if (info.index == 0)
                                 return "Tcp";
                             else
                                 return "Ipc";
                         } /* name generator */
);
