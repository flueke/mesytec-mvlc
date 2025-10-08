#ifndef D75CA87C_B626_428E_BF93_EE4A6BB7CB6B
#define D75CA87C_B626_428E_BF93_EE4A6BB7CB6B

// Streaming server implementation for MVME/mesytec-mvlc using libnng to handle
// networking.
//
// Purpose is to stream raw MVLC buffers to multiple clients over TCP or IPC.
// Optionally each buffer can be prefixed by a 32-bit sequence number and a
// 32-bit size prefix. This is required to detect internal buffer loss in case
// the server is fed data on a best-effort basis (e.g. if it's driven by the
// mvme analysis side).
//
// Supports tcp://, ipc://, inproc://, tcp4:// and tcp6:// URIs.
// The acceptor runs asychronously in the background. No thread creation needed.
// Message format is: u32 bufferNumber, u32 bufferSize, u32 data[bufferSize].
// Endianess is left as is.
//
// Usage:
// Create a TCPStreamServer instance, set the output format, then call start()
// with a list of URIs to listen on. Use stop() to stop the server, isRunning()
// to query the state.  Use send_to_all_clients() to do a blocking send to all
// connected clients.  This will internally queue up async sends, then wait for
// all of them to complete before returning.

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
// Message format is: u32 bufferNumber, u32 bufferSize, u32 data[bufferSize].
// Clients won't receive partial data, only complete messages.
// The senders network byte order is used, no reordering is done.
//bool MESYTEC_MVLC_EXPORT send_to_all_clients(StreamServer *ctx, u32 bufferNumber, const u32 *data,
//                                             u32 bufferElements);

bool send_to_all_clients(StreamServer *ctx, const u8 *data, size_t size);

} // namespace mesytec::mvlc

#endif /* D75CA87C_B626_428E_BF93_EE4A6BB7CB6B */
