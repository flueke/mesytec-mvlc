#ifndef D75CA87C_B626_428E_BF93_EE4A6BB7CB6B
#define D75CA87C_B626_428E_BF93_EE4A6BB7CB6B

// Streaming server implementation for MVME/mesytec-mvlc using libnng to handle
// networking.
//
// Purpose is to stream raw MVLC buffers to multiple clients over TCP, IPC and
// inproc transports.
//
// Supports tcp://, ipc://, inproc://, tcp4:// and tcp6:// URIs.
// The acceptor runs asychronously in the background. No thread creation needed.
//
// Usage:
//
// Create a TCPStreamServer instance and call listen() with the desired URIs.
// Use send_to_all_clients() to send a buffer of data to all connected clients.
// This will internally queue up async sends, then wait for all of them to
// complete before returning.

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <memory>
#include <string>
#include <vector>

#include "util/int_types.h"

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT StreamServer
{
  public:
    struct IClient
    {
        virtual ~IClient() = default;
        virtual std::string remoteAddress() const = 0;
    };

    StreamServer();
    ~StreamServer();

    bool listen(const std::string &uri);
    bool listen(const std::vector<std::string> &uris);
    void stop();
    bool isListening() const;
    std::vector<std::string> clients() const;

  private:
    struct Private;
    std::unique_ptr<Private> d;
    friend void accept_callback(void *arg);
    friend bool send_to_all_clients(StreamServer *ctx, const u8 *data, size_t size);
};

// Send data to all clients in a blocking fashion.
// The senders network byte order is used, no swapping is done.
bool send_to_all_clients(StreamServer *ctx, const u8 *data, size_t size);

} // namespace mesytec::mvlc

#endif /* D75CA87C_B626_428E_BF93_EE4A6BB7CB6B */
