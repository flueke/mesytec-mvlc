#include <iostream>

#define ASIO_ENABLE_BUFFER_DEBUGGING
//#define ASIO_ENABLE_HANDLER_TRACKING
#include <asio.hpp>
#define HANDLER_LOCATION ASIO_HANDLER_LOCATION((__FILE__, __LINE__, __func__))

#include <argh.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <mesytec-mvlc/util/string_util.h>

using namespace mesytec;
using namespace mesytec::mvlc;

struct Client: std::enable_shared_from_this<Client>
{
    asio::ip::tcp::socket socket_;
    std::string write_buffer_;

    Client(asio::ip::tcp::socket &&socket)
        : socket_(std::move(socket))
    {
        spdlog::debug(
            "New client: @{}, {}:{} -> {}:{}", fmt::ptr(this),
            socket_.remote_endpoint().address().to_string(), socket_.remote_endpoint().port(),
            socket_.local_endpoint().address().to_string(), socket_.local_endpoint().port());
        write_buffer_ = "Hello from server!\n";
    }

    ~Client()
    {
        spdlog::debug("Client destroyed: @{}", fmt::ptr(this));
        socket_.close();
    }

    void startWriteHello()
    {
        HANDLER_LOCATION;
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(write_buffer_),
                          [this, self](const asio::error_code &ec, size_t bytesTransferred)
                          {
                              HANDLER_LOCATION;
                              self->onWriteDone(ec, bytesTransferred);
                          });
    }

    void onWriteDone(const asio::error_code &ec, size_t bytesTransferred)
    {
        HANDLER_LOCATION;
        if (!ec)
        {
            spdlog::info("Sent {} bytes to client @{}", bytesTransferred, fmt::ptr(this));
            HANDLER_LOCATION;
            startWriteHello();
        }
        else
        {
            spdlog::error("Error sending to client @{}: {}", fmt::ptr(this), ec.message());
        }
    }
};

struct Server
{
    std::mutex clients_mutex;
    std::vector<std::weak_ptr<Client>> clients;
};

template<typename Func>
std::future<void> post(asio::io_context &io_context, Func &&func)
{
    HANDLER_LOCATION;
    std::packaged_task<void()> task(std::move(func));
    auto f = asio::post(io_context, asio::use_future(std::move(task)));
    return f;
}

template<typename Func>
void post_and_wait(asio::io_context &io_context, Func &&func)
{
    HANDLER_LOCATION;
    post(io_context, std::forward<Func>(func)).wait();
}

void start_accept(asio::ip::tcp::acceptor &acceptor, Server &server)
{
    HANDLER_LOCATION;
    acceptor.async_accept(
        [&](const asio::error_code &ec, asio::ip::tcp::socket socket)
        {
            if (!acceptor.is_open())
                return;

            HANDLER_LOCATION;
            if (!ec)
            {
                spdlog::info("Accepted connection from {}",
                             socket.remote_endpoint().address().to_string());
                auto client = std::make_shared<Client>(std::move(socket));
                client->startWriteHello();
                std::lock_guard<std::mutex> lock(server.clients_mutex);
                server.clients.push_back(client);
            }
            else
            {
                spdlog::error("Accept error: {}", ec.message());
            }

            start_accept(acceptor, server);
        });
}

void stop_accept(asio::io_context &io_context, asio::ip::tcp::acceptor &acceptor)
{
    HANDLER_LOCATION;
    post_and_wait(io_context, [&acceptor] { acceptor.close(); });
}

void await_stop_signal(asio::signal_set &signals, asio::io_context &io_context)
{
    HANDLER_LOCATION;
    signals.async_wait(
        [&io_context](const asio::error_code &ec, int signal_number)
        {
            HANDLER_LOCATION;
            if (!ec)
            {
                spdlog::info("Received signal {}, stopping...", signal_number);
                io_context.stop();
            }
            else
            {
                spdlog::error("Signal wait error: {}", ec.message());
            }
        });
}

int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::trace);
    mvlc::set_global_log_level(spdlog::level::trace);

    asio::io_context io_context;
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    await_stop_signal(signals, io_context);

    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 54321);
    asio::ip::tcp::acceptor acceptor(io_context, endpoint);

    Server server;
    start_accept(acceptor, server);

    std::vector<std::thread> io_threads;

    for (size_t i = 0; i < 4; ++i)
    {
        io_threads.emplace_back([&io_context]() { io_context.run(); });
    }

    spdlog::info("Spawned {} IO threads, entering REPL", io_threads.size());

    std::string line;

    std::cout << "> " << std::flush;
    while (std::getline(std::cin, line))
    {
        if (!line.empty())
            spdlog::info("REPL input: '{}'", line);
        line = util::trim(line);

        if (line == "quit" || line == "exit")
            break;

        if (line == "stop")
        {
            spdlog::info("Stop: closing acceptor");
            stop_accept(io_context, acceptor);

            spdlog::info("Stop: Closing {} client connections", server.clients.size());
            std::lock_guard<std::mutex> lock(server.clients_mutex);
            for (auto &weak_client: server.clients)
            {
                if (auto client = weak_client.lock())
                {
                    spdlog::info("Closing client @{}", fmt::ptr(client.get()));
                    post_and_wait(io_context, [&client]() { client->socket_.close(); });
                }
            }
            server.clients.clear();

            spdlog::info("Stop: stopping io_context");
            io_context.stop();

            spdlog::info("Stop: waiting for IO threads to finish");
            for (auto &t: io_threads)
            {
                t.join();
            }
            io_threads.clear();
            spdlog::info("server stopped");
        }

        if (line == "start")
        {
            if (io_context.stopped())
            {
                spdlog::info("Restarting io_context");
                acceptor = asio::ip::tcp::acceptor(io_context, endpoint);
                start_accept(acceptor, server);
                io_context.restart();
                for (size_t i = 0; i < 4; ++i)
                {
                    io_threads.emplace_back([&io_context]() { io_context.run(); });
                }
            }
            else
            {
                spdlog::info("io_context is already running");
            }
        }
        std::cout << "> " << std::flush;
    }

    spdlog::info("REPL exiting, stopping io_context");

    io_context.stop();

    for (auto &t: io_threads)
    {
        t.join();
    }
}
