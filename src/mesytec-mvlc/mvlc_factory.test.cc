#include <gtest/gtest.h>
#include <spdlog/spdlog.h> // for fmt::format()
#include "mvlc_factory.h"

using namespace mesytec::mvlc;

TEST(mvlc_factory, mvlc_parse_url)
{
    {
        auto parsed = mvlc_parse_url("scheme://host:port");
        ASSERT_EQ(parsed.rawUrl, "scheme://host:port");
        ASSERT_EQ(parsed.scheme, "scheme");
        ASSERT_EQ(parsed.host, "host:port");
    }

    {
        auto parsed = mvlc_parse_url("scheme://");
        ASSERT_EQ(parsed.rawUrl, "scheme://");
        ASSERT_EQ(parsed.scheme, "scheme");
        ASSERT_TRUE(parsed.host.empty());
    }

    {
        auto parsed = mvlc_parse_url("://host:port");
        ASSERT_EQ(parsed.rawUrl, "://host:port");
        ASSERT_TRUE(parsed.scheme.empty());
        ASSERT_EQ(parsed.host, "host:port");
    }
}

TEST(mvlc_factory, make_mvlc)
{
    {
        auto url = "mvlc.example.com";
        auto mvlc = make_mvlc(url);
        ASSERT_TRUE(mvlc);
        ASSERT_EQ(mvlc.connectionType(), ConnectionType::ETH);
    }

    {
        auto url = "eth://mvlc.example.com";
        auto mvlc = make_mvlc(url);
        ASSERT_TRUE(mvlc);
        ASSERT_EQ(mvlc.connectionType(), ConnectionType::ETH);
    }

    {
        auto url = "udp://mvlc.example.com";
        auto mvlc = make_mvlc(url);
        ASSERT_TRUE(mvlc);
        ASSERT_EQ(mvlc.connectionType(), ConnectionType::ETH);
    }

    {
        auto url = "eth://";
        auto mvlc = make_mvlc(url);
        ASSERT_FALSE(mvlc);
    }

    {
        auto url = "udp://";
        auto mvlc = make_mvlc(url);
        ASSERT_FALSE(mvlc);
    }

    {
        auto url = "foobar://blob";
        auto mvlc = make_mvlc(url);
        ASSERT_FALSE(mvlc);
    }

    {
        auto url = "usb://";
        auto mvlc = make_mvlc(url);
        ASSERT_TRUE(mvlc);
        ASSERT_EQ(mvlc.connectionType(), ConnectionType::USB);
        std::cout << fmt::format("url='{}', mvlc connection info=({})\n", url, mvlc.connectionInfo());
    }

    {
        auto url = "usb://@42";
        auto mvlc = make_mvlc(url);
        ASSERT_TRUE(mvlc);
        ASSERT_EQ(mvlc.connectionType(), ConnectionType::USB);
        std::cout << fmt::format("url='{}', mvlc connection info=({})\n", url, mvlc.connectionInfo());
    }

    {
        auto url = "usb://TheUsbSerial";
        auto mvlc = make_mvlc(url);
        ASSERT_TRUE(mvlc);
        ASSERT_EQ(mvlc.connectionType(), ConnectionType::USB);
        std::cout << fmt::format("url='{}', mvlc connection info=({})\n", url, mvlc.connectionInfo());
    }
}
