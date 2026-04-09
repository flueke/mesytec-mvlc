#include <algorithm>
#include <argh.h>
#include <asio.hpp>
#include <boost/endian/conversion.hpp>
#include <cassert>
#include <cctype>
#include <chrono>
#include <iostream>
#include <optional>
#include <spdlog/spdlog.h>

inline std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline double floaty_seconds(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
}

struct ClientContext
{
    std::vector<std::uint8_t> buffer;
    size_t buffer_used = 0;
    size_t totalBytesReceived = 0;
    size_t totalBuffersReceived = 0;
    size_t bytesReceivedInInterval = 0;
    size_t buffersReceivedInInterval = 0;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastReportTime;

    ClientContext()
        : startTime(std::chrono::steady_clock::now()), lastReportTime(std::chrono::steady_clock::now())
    {
    }

    size_t free_space() const
    {
        assert(buffer_used <= buffer.size());
        return buffer.size() - buffer_used;
    }

    void ensure_free_space(size_t needed)
    {
        if (free_space() >= needed)
            return;

        auto missing = needed - free_space();
        buffer.resize(buffer.size() + missing);
        assert(free_space() >= needed);
    }

    std::uint8_t *write_ptr() { return buffer.data() + buffer_used; }
};

void reset_state(ClientContext &ctx)
{
    ctx.buffer.clear();
    ctx.buffer_used = 0;
    ctx.totalBytesReceived = 0;
    ctx.totalBuffersReceived = 0;
    ctx.bytesReceivedInInterval = 0;
    ctx.buffersReceivedInInterval = 0;
    ctx.lastReportTime = std::chrono::steady_clock::now();
}

void maybe_print_stats(ClientContext &ctx)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.lastReportTime);

    if (elapsed >= std::chrono::seconds(1))
    {

        auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.startTime);
        double dt_s = floaty_seconds(elapsed);
        double totalElapsed_s = floaty_seconds(totalElapsed);

        double totalBytesPerSecond = ctx.totalBytesReceived / totalElapsed_s;
        double intervalBytesPerSecond = ctx.bytesReceivedInInterval / dt_s;

        double totalBuffersPerSecond = ctx.totalBuffersReceived / totalElapsed_s;
        double intervalBuffersPerSecond = ctx.buffersReceivedInInterval / dt_s;

        spdlog::info("Stats over the last {:.2f}s: {:.2f} Bytes/s, {:.2f} Buffers/s", dt_s,
                     intervalBytesPerSecond, intervalBuffersPerSecond);
        spdlog::info("Stats over the run: {:.2f}s: {:.2f} Bytes/s, {:.2f} Buffers/s", totalElapsed_s,
                     totalBytesPerSecond, totalBuffersPerSecond);

        ctx.bytesReceivedInInterval = 0;
        ctx.buffersReceivedInInterval = 0;
        ctx.lastReportTime = now;
    }
}

inline struct timeval ms_to_timeval(unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = ms * 1000;

    return tv;
}

#ifdef WIN32
// Set socket timeouts for send and receive operations
template <typename Socket> void set_socket_timeout(Socket &sock, std::chrono::milliseconds timeout)
{
    DWORD optval = timeout.count();
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&optval), sizeof(optval));
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char *>(&optval), sizeof(optval));
}
#else
template <typename Socket> void set_socket_timeout(Socket &sock, std::chrono::milliseconds timeout)
{
    struct timeval tv = ms_to_timeval(timeout.count());
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
#endif

// TCP connection helper
inline std::error_code connect_tcp(asio::ip::tcp::socket &socket, const std::string &host,
                                   const std::string &port)
{
    if (socket.is_open())
        socket.close();

    set_socket_timeout(socket, std::chrono::milliseconds(2000));

    spdlog::info("Connecting to TCP: {}:{}", host, port);

    auto &io_context = static_cast<asio::io_context &>(socket.get_executor().context());
    asio::ip::tcp::resolver resolver(io_context);

    std::error_code ec;
    auto endpoints = resolver.resolve(host, port, ec);
    if (ec)
        return ec;

    asio::connect(socket, endpoints, ec);
    return ec;
}

// Unix domain socket connection helper
inline std::error_code connect_unix(asio::local::stream_protocol::socket &socket,
                                    const std::string &path)
{
    if (socket.is_open())
        socket.close();

    set_socket_timeout(socket, std::chrono::milliseconds(2000));

    spdlog::info("Connecting to Unix socket: {}", path);

    asio::local::stream_protocol::endpoint endpoint(path);
    std::error_code ec;
    socket.connect(endpoint, ec);
    return ec;
}

template <typename Socket>
size_t read_at_least(ClientContext &ctx, Socket &socket, size_t needed, std::error_code &ec)
{
    ctx.ensure_free_space(needed);
    auto dest = asio::buffer(ctx.write_ptr(), ctx.free_space());

    size_t bytesRead = asio::read(socket, dest, asio::transfer_at_least(needed), ec);

    spdlog::trace("read_at_least: requested {} bytes, read {} bytes, ec={}", needed, bytesRead,
                  ec.message());

    ctx.buffer_used += bytesRead;
    ctx.totalBytesReceived += bytesRead;
    ctx.bytesReceivedInInterval += bytesRead;
    ctx.totalBuffersReceived++;
    ctx.buffersReceivedInInterval++;

    return bytesRead;
}

namespace frame_constants
{
static const std::uint8_t TypeShift = 24;
static const std::uint8_t TypeMask = 0xff;

static const std::uint8_t FrameFlagsMask = 0xf;
static const std::uint8_t FrameFlagsShift = 20;

static const std::uint8_t StackNumShift = 16;
static const std::uint8_t StackNumMask = 0xf;

static const std::uint8_t CtrlIdShift = 13;
static const std::uint8_t CtrlIdMask = 0b111;

// This works for both raw (usb) headers and eth packet headers.
static const std::uint16_t LengthShift = 0;
static const std::uint16_t LengthMask = 0x1fff;

static const std::uint8_t EthHeaderShift = 30;
static const std::uint8_t EthHeaderMask = 0b11;
} // namespace frame_constants

// The atomic parts that make up an mvlc stream: either direct (usb formatted)
// frames or eth packets containing the former.
enum class AtomicPartType
{
    EthPacket = 0b00,
    ReadoutFrame = 0xF3,
    ReadoutContinuation = 0xF9,
    SystemFrame = 0xFA,
    SystemFrame2 = 0xFB,
};

struct AtomicPartView
{
    AtomicPartType type;
    std::basic_string_view<std::uint32_t> data;
};

inline uint32_t extract_value(std::uint32_t data, std::uint32_t mask, std::uint32_t shift)
{
    return (data >> shift) & mask;
}

inline std::uint8_t get_frame_type(std::uint32_t header)
{
    return extract_value(header, frame_constants::TypeMask, frame_constants::TypeShift);
}

inline std::optional<AtomicPartType> determine_atomic_part_type(std::uint32_t header)
{
    auto ethHeaderBits =
        extract_value(header, frame_constants::EthHeaderMask, frame_constants::EthHeaderShift);

    if (ethHeaderBits == 0b00)
        return AtomicPartType::EthPacket;

    auto frameType = static_cast<AtomicPartType>(get_frame_type(header));

    if (frameType == AtomicPartType::ReadoutFrame || frameType == AtomicPartType::ReadoutContinuation ||
        frameType == AtomicPartType::SystemFrame || frameType == AtomicPartType::SystemFrame2)
    {
        return frameType;
    }

    return std::nullopt;
}

// Returns the value of the 'data word count' field of frames and eth packets.
// This is the number of words _following_ the respective header (in the case of
// eth its the number of words following the 2nd eth header word).
inline size_t extract_atomic_part_length(AtomicPartType partType, std::uint32_t header)
{
    switch (partType)
    {
    case AtomicPartType::ReadoutFrame:
    case AtomicPartType::ReadoutContinuation:
    case AtomicPartType::SystemFrame:
    case AtomicPartType::SystemFrame2:
    case AtomicPartType::EthPacket:
        return extract_value(header, frame_constants::LengthMask, frame_constants::LengthShift);
    default:
        return 0;
    }
}

// Returns the number of bytes consumed or a negative number indicating the
// number of bytes missing for a complete frame.
// The second value holds an explanatory message in case of an error or if more data is needed.
std::pair<std::int64_t, std::string> analyze_buffer(const std::uint8_t *data, size_t buffer_used)
{
    // Assumption: we land on a mvlc-protocol-level frame header. Check if the
    // frame matches a known bit pattern, then check if the frame is fully
    // contained in the buffer.

    std::basic_string_view<std::uint32_t> view(reinterpret_cast<const std::uint32_t *>(data),
                                               buffer_used / sizeof(std::uint32_t));

    if (view.empty())
        return {0, "Buffer empty"};

    auto atomicPartOpt = determine_atomic_part_type(view[0]);

    if (!atomicPartOpt)
    {
        spdlog::error("Unknown frame header: {:#010x}", view[0]);
        return {0, "Unknown frame header"}; // do not 'consume' any data. we don't know what's up
    }

    auto partWordCount = extract_atomic_part_length(*atomicPartOpt, view[0]);

    size_t totalWordsNeeded = 1 + partWordCount; // header + payload

    if (*atomicPartOpt == AtomicPartType::EthPacket)
        totalWordsNeeded += 1; // eth packets have an additional header word

    if (view.size() < totalWordsNeeded)
    {
        size_t bytesNeeded = totalWordsNeeded * sizeof(std::uint32_t) - buffer_used;
        return {-static_cast<std::int64_t>(bytesNeeded),
                fmt::format("Need {} more bytes for a complete frame", bytesNeeded)};
    }

    return {static_cast<std::int64_t>(view.size() * sizeof(std::uint32_t)), "Frame complete"};
}

std::optional<AtomicPartView>

template <typename Socket, typename ConnectFunc>
int run_client(Socket &socket, ConnectFunc reconnect, ClientContext &ctx)
{
    enum class State
    {
        Connecting,
        Reading
    };

    State state = State::Connecting;
    std::error_code ec;
    ctx.buffer.resize(1u << 20);

    while (true)
    {
        switch (state)
        {
        case State::Connecting:
        {
            ec = reconnect();

            if (ec)
            {
                spdlog::warn("Failed to connect: {}", ec.message().c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            else
            {
                spdlog::info("Connected");
                reset_state(ctx);
                state = State::Reading;
            }
            break;
        }
        case State::Reading:
        {
            while (!ec && ctx.buffer_used < sizeof(std::uint32_t))
            {
                read_at_least(ctx, socket, sizeof(std::uint32_t), ec);
            }

            if (ec && ctx.buffer_used < sizeof(std::uint32_t))
            {
                spdlog::error("Error reading data: {}, reconnecting...", ec.message());
                state = State::Connecting;
            }

            auto [bytesConsumed, message] = analyze_buffer(ctx.buffer.data(), ctx.buffer_used);

            if (bytesConsumed == 0)
            {
            }
            else if (bytesConsumed < 0)
            {
            }
            else
            {
            }

            assert(bytesConsumed <= ctx.buffer_used);

            if (bytesConsumed)
            {
                if (bytesConsumed <= ctx.buffer_used)
                {
                    std::memmove(ctx.buffer.data(), ctx.buffer.data() + bytesConsumed,
                                 ctx.buffer_used - bytesConsumed);
                    ctx.buffer_used -= bytesConsumed;
                }
                else
                {
                    spdlog::error(
                        "analyze_buffer reported consuming more bytes than available: {} > {}",
                        bytesConsumed, ctx.buffer_used);
                    ctx.buffer_used = 0;
                    state = State::Connecting;
                }
            }

            break;
        }
        }
    }
}

int main(int argc, char *argv[])
{
    argh::parser parser({"-h", "--help", "--log-level", "--tcp", "--unix"});
    parser.parse(argc, argv);

    if (parser[{"-h", "--help"}])
    {
        std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --tcp [host:port]    Connect via TCP (default: 127.0.0.1:42333)\n";
        std::cout << "  --unix [path]        Connect via Unix domain socket (default: "
                     "/tmp/mvme_stream_server.sock)\n";
        std::cout << "  -h, --help           Show this help\n";
        std::cout << "  --log-level <level_name> "
                     "[--trace][--debug][--info][--warn][--error][--critical]\n";
        return 0;
    }

    spdlog::set_level(spdlog::level::info);

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
        else if (parser["--error"])
            logLevelName = "error";
        else if (parser["--critical"])
            logLevelName = "critical";

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
    }

    // Parse connection parameters
    enum class Transport
    {
        TCP,
        Unix
    };
    Transport transport = Transport::TCP;

    std::string tcpHost = "127.0.0.1";
    std::string tcpPort = "42333";
    std::string unixPath = "/tmp/mvme_stream_server.sock";
    ClientContext ctx;

    std::string tmp;

    if (parser["--unix"] || parser("--unix") >> tmp)
    {
        if (!tmp.empty())
            unixPath = tmp;
        transport = Transport::Unix;
    }
    else if (parser["--tcp"] || parser("--tcp") >> tmp)
    {
        if (!tmp.empty())
        {
            auto pos = tmp.find(':');
            if (pos != std::string::npos)
            {
                tcpHost = tmp.substr(0, pos);
                tcpPort = tmp.substr(pos + 1);
            }
            else
            {
                tcpHost = tmp;
            }
        }
        transport = Transport::TCP;
    }

    try
    {
        asio::io_context io_context;
        int ret = 0;

        switch (transport)
        {
        case Transport::TCP:
        {
            asio::ip::tcp::socket socket(io_context);
            auto reconnect = [&]() { return connect_tcp(socket, tcpHost, tcpPort); };
            ret = run_client(socket, reconnect, ctx);
            break;
        }

        case Transport::Unix:
        {
            asio::local::stream_protocol::socket socket(io_context);
            auto reconnect = [&]() { return connect_unix(socket, unixPath); };
            ret = run_client(socket, reconnect, ctx);
            break;
        }
        }

        return ret;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception: {}", e.what());
        return 1;
    }
}
