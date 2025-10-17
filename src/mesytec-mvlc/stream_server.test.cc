// stream_server.test.cc
#include "gtest/gtest.h"
#include "mesytec-mvlc/stream_server.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <random>
#include <cstring>
#include <iostream>
#include <vector>
#include <cstdint>

using namespace mesytec::mvlc;
using namespace std::chrono_literals;

// Constants used by test utilities
static constexpr uint32_t MAGIC_PATTERN = 0xDEADBEEF;

class StreamServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<StreamServer>();
    }

    void TearDown() override {
        if (server) {
            server->stop();
        }
    }

    std::unique_ptr<StreamServer> server;
    static constexpr size_t TEST_BUFFER_SIZE = 1024;
};

// Test buffer structure
struct TestBuffer {
    uint32_t sequence_number;
    uint32_t magic;
    uint32_t buffer_size;  // Number of uint32_t values following
    uint32_t checksum;
    // Followed by buffer_size uint32_t values with pattern
};

// Generate test data with verifiable pattern
std::vector<uint8_t> generateTestBuffer(uint32_t seq_num, size_t data_words = 256) {
    TestBuffer header{
        .sequence_number = seq_num,
        .magic = MAGIC_PATTERN,
        .buffer_size = static_cast<uint32_t>(data_words),
        .checksum = 0
    };

    std::vector<uint32_t> data;
    data.resize(data_words);

    // Generate predictable pattern: seq_num + index
    for (size_t i = 0; i < data_words; ++i) {
        data[i] = seq_num + static_cast<uint32_t>(i);
    }

    // Calculate checksum
    header.checksum = seq_num ^ MAGIC_PATTERN ^ data_words;
    for (uint32_t val : data) {
        header.checksum ^= val;
    }

    // Pack into byte buffer
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(header) + data.size() * sizeof(uint32_t));

    memcpy(buffer.data(), &header, sizeof(header));
    memcpy(buffer.data() + sizeof(header), data.data(), data.size() * sizeof(uint32_t));

    return buffer;
}

// Verify received buffer
bool verifyTestBuffer(const std::vector<uint8_t>& buffer, uint32_t expected_seq) {
    if (buffer.size() < sizeof(TestBuffer)) return false;

    TestBuffer header;
    memcpy(&header, buffer.data(), sizeof(header));

    if (header.sequence_number != expected_seq) return false;
    if (header.magic != MAGIC_PATTERN) return false;

    size_t expected_size = sizeof(header) + header.buffer_size * sizeof(uint32_t);
    if (buffer.size() != expected_size) return false;

    // Verify data pattern and checksum
    const uint32_t* data = reinterpret_cast<const uint32_t*>(buffer.data() + sizeof(header));
    uint32_t calc_checksum = expected_seq ^ MAGIC_PATTERN ^ header.buffer_size;

    for (uint32_t i = 0; i < header.buffer_size; ++i) {
        if (data[i] != expected_seq + i) return false;
        calc_checksum ^= data[i];
    }

    return calc_checksum == header.checksum;
}

// Client stats collector
struct ClientStats {
    std::atomic<uint32_t> buffers_received{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint32_t> last_sequence{0};
    std::atomic<bool> sequence_error{false};
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_receive_time;

    void reset() {
        buffers_received = 0;
        bytes_received = 0;
        last_sequence = 0;
        sequence_error = false;
        start_time = std::chrono::steady_clock::now();
    }

    double getBuffersPerSecond() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - start_time).count();
        return elapsed > 0 ? buffers_received.load() / elapsed : 0.0;
    }

    double getDataRateMBps() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - start_time).count();
        return elapsed > 0 ? (bytes_received.load() / (1024.0 * 1024.0)) / elapsed : 0.0;
    }
};


TEST_F(StreamServerTest, BasicTcpTransport) {
    ASSERT_TRUE(server->listen("tcp://127.0.0.1:0")); // Use ephemeral port

    ClientStats stats;
    stats.reset();

    // Simple client thread
    std::atomic<bool> client_done{false};
    auto client_thread = std::thread([&]() {
        // Connect and receive data
        // Implementation depends on your client code
    });

    // Send test buffers
    const uint32_t num_buffers = 100;
    for (uint32_t i = 0; i < num_buffers; ++i) {
        auto buffer = generateTestBuffer(i);
        ASSERT_TRUE(send_to_all_clients(server.get(), buffer.data(), buffer.size()));
        std::this_thread::sleep_for(1ms);
    }

    client_done = true;
    client_thread.join();

    EXPECT_EQ(stats.buffers_received.load(), num_buffers);
    EXPECT_FALSE(stats.sequence_error.load());
}

TEST_F(StreamServerTest, IpcTransport) {
    const std::string ipc_path = "/tmp/stream_server_test.ipc";
    ASSERT_TRUE(server->listen("ipc://" + ipc_path));

    // Similar test structure as TCP
    // ...
}

TEST_F(StreamServerTest, InprocTransport) {
    ASSERT_TRUE(server->listen("inproc://test"));

    // Similar test structure but using inproc client
    // ...
}

TEST_F(StreamServerTest, MultipleClientsWithStats) {
    ASSERT_TRUE(server->listen("tcp://127.0.0.1:0"));

    const size_t num_clients = 5;
    const uint32_t num_buffers = 1000;

    std::vector<ClientStats> client_stats(num_clients);
    std::vector<std::thread> client_threads;
    std::atomic<bool> test_done{false};

    // Start multiple clients
    for (size_t i = 0; i < num_clients; ++i) {
        client_stats[i].reset();

        client_threads.emplace_back([&, i]() {
            // Client implementation
            uint32_t expected_seq = 0;

            while (!test_done) {
                // Receive and verify buffer
                // Update stats
            }
        });
    }

    // Send buffers
    auto send_start = std::chrono::steady_clock::now();
    for (uint32_t seq = 0; seq < num_buffers; ++seq) {
        auto buffer = generateTestBuffer(seq, 256);
        ASSERT_TRUE(send_to_all_clients(server.get(), buffer.data(), buffer.size()));
    }
    auto send_end = std::chrono::steady_clock::now();

    test_done = true;
    for (auto& t : client_threads) {
        t.join();
    }

    // Verify all clients received all data
    for (const auto& stats : client_stats) {
        EXPECT_EQ(stats.buffers_received.load(), num_buffers);
        EXPECT_FALSE(stats.sequence_error.load());

        std::cout << "Client stats: "
                  << stats.getBuffersPerSecond() << " buf/s, "
                  << stats.getDataRateMBps() << " MB/s" << std::endl;
    }

    // Calculate server-side stats
    auto send_duration = std::chrono::duration<double>(send_end - send_start).count();
    double server_buffers_per_sec = num_buffers / send_duration;

    std::cout << "Server sent: " << server_buffers_per_sec << " buf/s" << std::endl;
}

TEST_F(StreamServerTest, MixedSpeedClients) {
    ASSERT_TRUE(server->listen("tcp://127.0.0.1:0"));

    struct ClientConfig {
        std::string name;
        std::chrono::milliseconds processing_delay;
        ClientStats stats;
    };

    std::vector<ClientConfig> clients;
    clients.emplace_back(ClientConfig{"fast1", 0ms, {}});
    clients.emplace_back(ClientConfig{"fast2", 1ms, {}});
    clients.emplace_back(ClientConfig{"medium", 5ms, {}});
    clients.emplace_back(ClientConfig{"slow1", 20ms, {}});
    clients.emplace_back(ClientConfig{"slow2", 50ms, {}});

    const uint32_t num_buffers = 500;
    std::vector<std::thread> client_threads;
    std::atomic<bool> test_done{false};

    for (auto& client : clients) {
        client.stats.reset();

        client_threads.emplace_back([&]() {
            uint32_t expected_seq = 0;

            while (!test_done) {
                // Receive buffer
                // Simulate processing delay
                std::this_thread::sleep_for(client.processing_delay);
                // Verify and update stats
            }
        });
    }

    // Send at steady rate
    for (uint32_t seq = 0; seq < num_buffers; ++seq) {
        auto buffer = generateTestBuffer(seq, 128);
        ASSERT_TRUE(send_to_all_clients(server.get(), buffer.data(), buffer.size()));
        std::this_thread::sleep_for(10ms); // Steady 100 buf/s
    }

    // Allow slow clients to catch up
    std::this_thread::sleep_for(2s);
    test_done = true;

    for (auto& t : client_threads) {
        t.join();
    }

    // Verify and report stats
    for (const auto& client : clients) {
        EXPECT_EQ(client.stats.buffers_received.load(), num_buffers)
            << "Client " << client.name << " missed buffers";
        EXPECT_FALSE(client.stats.sequence_error.load())
            << "Client " << client.name << " had sequence errors";

        std::cout << client.name << ": "
                  << client.stats.getBuffersPerSecond() << " buf/s, "
                  << client.stats.getDataRateMBps() << " MB/s" << std::endl;
    }
}

TEST_F(StreamServerTest, DynamicClientConnection) {
    ASSERT_TRUE(server->listen("tcp://127.0.0.1:0"));

    const uint32_t buffers_before_client = 50;
    const uint32_t buffers_after_client = 100;

    // Start sending before any clients connect
    std::atomic<uint32_t> current_sequence{0};
    std::atomic<bool> sending_done{false};

    auto sender_thread = std::thread([&]() {
        while (!sending_done) {
            auto buffer = generateTestBuffer(current_sequence.load(), 64);
            send_to_all_clients(server.get(), buffer.data(), buffer.size());
            current_sequence++;
            std::this_thread::sleep_for(5ms);
        }
    });

    // Wait for some buffers to be sent
    while (current_sequence.load() < buffers_before_client) {
        std::this_thread::sleep_for(1ms);
    }

    // Now connect a client
    ClientStats client_stats;
    client_stats.reset();
    uint32_t client_start_sequence = current_sequence.load();

    auto client_thread = std::thread([&]() {
        uint32_t expected_seq = client_start_sequence;

        // Client should only see buffers sent after it connected
        while (current_sequence.load() < buffers_before_client + buffers_after_client) {
            // Receive and verify buffer
            // Should start from client_start_sequence
        }
    });

    // Send more buffers
    while (current_sequence.load() < buffers_before_client + buffers_after_client) {
        std::this_thread::sleep_for(1ms);
    }

    sending_done = true;
    sender_thread.join();
    client_thread.join();

    EXPECT_EQ(client_stats.buffers_received.load(), buffers_after_client);
    EXPECT_FALSE(client_stats.sequence_error.load());
    EXPECT_EQ(client_stats.last_sequence.load(),
              client_start_sequence + buffers_after_client - 1);
}

TEST_F(StreamServerTest, MultipleListenCalls) {
    // Start with TCP
    ASSERT_TRUE(server->listen("tcp://127.0.0.1:0"));
    EXPECT_TRUE(server->isListening());

    // Add IPC while running
    ASSERT_TRUE(server->listen("ipc:///tmp/test_additional.ipc"));

    // Start clients on both transports
    ClientStats tcp_stats, ipc_stats;
    tcp_stats.reset();
    ipc_stats.reset();

    std::atomic<bool> test_done{false};

    auto tcp_client = std::thread([&]() {
        // TCP client implementation
    });

    auto ipc_client = std::thread([&]() {
        // IPC client implementation
    });

    // Send buffers - both clients should receive them
    const uint32_t num_buffers = 200;
    for (uint32_t seq = 0; seq < num_buffers; ++seq) {
        auto buffer = generateTestBuffer(seq, 32);
        ASSERT_TRUE(send_to_all_clients(server.get(), buffer.data(), buffer.size()));
        std::this_thread::sleep_for(2ms);
    }

    test_done = true;
    tcp_client.join();
    ipc_client.join();

    EXPECT_EQ(tcp_stats.buffers_received.load(), num_buffers);
    EXPECT_EQ(ipc_stats.buffers_received.load(), num_buffers);
    EXPECT_FALSE(tcp_stats.sequence_error.load());
    EXPECT_FALSE(ipc_stats.sequence_error.load());
}
