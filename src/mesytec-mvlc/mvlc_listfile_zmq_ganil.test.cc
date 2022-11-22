#include <chrono>
#include <thread>
#include <zmq.hpp>
#include "gtest/gtest.h"
#include "mvlc_listfile_zmq_ganil.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;

TEST(mvlc_listfile_zmq_ganil, TestListfileZmqGanil)
{
    // The write handle is the publisher side
    ZmqGanilWriteHandle pub;

    // Create and connect a subscriber socket
    zmq::context_t ctx;
    zmq::socket_t sub(ctx, ZMQ_SUB);

    int timeout=500; //milliseconds
#if CPPZMQ_VERSION >= ZMQ_MAKE_VERSION(4, 7, 0)
    sub.set(zmq::sockopt::rcvtimeo, timeout);
    EXPECT_NO_THROW(sub.set(zmq::sockopt::subscribe, ""));
#else
    sub.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(int));
    EXPECT_NO_THROW(sub.setsockopt(ZMQ_SUBSCRIBE, "", 0));
#endif

    EXPECT_NO_THROW(sub.connect("tcp://localhost:5575"));

    // Hack to give zmq time to connect. Removing this entirely makes the
    // receive tests below fail.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Publish N messages of increasing size.
    for (int i=1; i<=100; ++i)
    {
        std::vector<int> buffer(i, i);
        ASSERT_EQ(buffer.size(), i);
        pub.write(reinterpret_cast<const u8 *>(buffer.data()), buffer.size() * sizeof(int));
    }

    // Now attempt to receive and verify the messages on the subscriber.
    for (int i=1; i<=100; ++i)
    {
        zmq::message_t msg;
#ifndef CPPZMQ_VERSION
        ASSERT_NO_THROW(sub.recv(&msg));
#else
        ASSERT_NO_THROW(sub.recv(msg).value());
#endif
        ASSERT_EQ(msg.size(), i * sizeof(int));

        const int *data = reinterpret_cast<const int *>(msg.data());
        const int count = msg.size() / sizeof(int);

        for (auto it = data; it != data + count; ++it)
        {
            ASSERT_EQ(*it, i);
        }
    }
}
