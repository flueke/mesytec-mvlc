#ifndef D69B27F4_5AB0_4399_9786_26C3E3C4C08B
#define D69B27F4_5AB0_4399_9786_26C3E3C4C08B

// A client implementation for the StreamServer.
//
// Uses nng under the hood to handle networking. This is a blocking
// implementation, no async stuff is exposed.

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/util/int_types.h>
#include <memory>
#include <string>
#include <chrono>

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT StreamClient
{
  public:
    explicit StreamClient();
    ~StreamClient();

    // Connect to the given server URI. Supported URI schemes are:
    // tcp://, tcp4://, tcp6://, ipc:// and inproc://
    //
    // Returns true on success, false on failure.
    bool connect(const std::string &uri, const std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    // Disconnect from the server.
    void disconnect();

    // Returns true if connected to a server.
    bool isConnected() const;

    // Returns the address of the connected server.
    std::string remoteAddress() const;

    // Receive data from the server into the given buffer.
    // Blocks until size bytes have been received, the receive times out or an
    // error occurs.
    //
    // Returns the number of bytes received, or -1 on error.
    ssize_t receive(u8 *buffer, size_t size);

  private:
    struct Private;
    std::unique_ptr<Private> d;
    friend void dial_callback(void *arg);
    friend void recv_callback(void *arg);
};

}

#endif /* D69B27F4_5AB0_4399_9786_26C3E3C4C08B */
