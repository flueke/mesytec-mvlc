#ifndef DDE5B269_586E_432D_8652_774580A87FAB
#define DDE5B269_586E_432D_8652_774580A87FAB

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <cstdint>
#include <string>
#include <vector>

namespace mesytec::mvlc::stream
{

// Interface for a streaming (readout) data server. An asio based implementation
// can be found in stream_server_asio.{h,cc}.
//
// The interface is designed to allow for efficient "broadcasts" of data buffers
// to all connected clients.
//
// Supported URL formats:
// - TCP: tcp://<host>:<port>
//        tcp4:// and tcp6:// can be used to force a specific IP version.
//        Use '*' for the host part to listen on all interfaces, e.g. tcp://*:55333
// - IPC: ipc://<socket_path> or unix://<socket_path> for Unix domain sockets, e.g. ipc:///tmp/mvlc_stream_server.ipc
class MESYTEC_MVLC_EXPORT IStreamServer
{
  public:
    // Note: for setPreamble() and sendToAllClients() to be visible in
    // subclasses they need the corresponding 'using' delcarations, e.g. 'using
    // IStreamServer::listen' in the subclass.

    // IO vector for gathered writes.
    struct IOV
    {
        const void *buf;
        std::size_t len;
    };

    explicit IStreamServer() = default;
    virtual ~IStreamServer();

    // Returns true if the server is able to listen on the given url.
    virtual bool listen(const std::string &url) = 0;

    // True if the server is listening on at least one address.
    virtual bool isListening() const = 0;

    // Stop listening and disconnect all clients.
    // Returns true if the server was listening and is now stopped, false
    // otherwise.
    virtual bool stop() = 0;

    // The number of currently connected clients.
    virtual size_t clientCount() const = 0;

    // List of active listening addresses.
    virtual std::vector<std::string> listenAddresses() const = 0;

    // List of active client addresses.
    virtual std::vector<std::string> clientAddresses() const = 0;

    // Send data to all clients in a blocking fashion. Data is gathered from the
    // given IOV array.
    // Returns 0 on success or the number of sends that failed. size_t::max() is
    // returned on internal/fatal error, e.g. if the server is not listening.
    virtual size_t sendToAllClients(const IOV *iov, size_t n_iov) = 0;

    // Non-gather version of sendToAllClients.
    size_t sendToAllClients(const uint8_t *data, size_t size);
    size_t sendToAllClients(const std::vector<uint8_t> &data);

    // Set a preamble that is sent to newly connected clients before any other
    // data. Typically used to hold configuration data that clients can use to
    // correctly handle the incoming data stream.
    // Clients that are connected at the time setPreamble() is called will also
    // receive the new preamble. This ensures both new and existing clients see
    // the same state and state changes.
    virtual void setPreamble(const IOV *data, size_t n_iov) = 0;

    // Non-gather version of setPreamble.
    void setPreamble(const std::uint8_t *data, size_t size);
    void setPreamble(const std::vector<std::uint8_t> &data);

    IStreamServer(const IStreamServer &) = delete;
    IStreamServer &operator=(const IStreamServer &) = delete;
    IStreamServer(IStreamServer &&) = delete;
    IStreamServer &operator=(IStreamServer &&) = delete;
};

}

#endif /* DDE5B269_586E_432D_8652_774580A87FAB */
