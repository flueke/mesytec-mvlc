#ifndef DDE5B269_586E_432D_8652_774580A87FAB
#define DDE5B269_586E_432D_8652_774580A87FAB

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <cstdint>
#include <string>
#include <vector>

// TODO: add network byte order handling?
// TODO: return size_t::max() in sendToAllClei

    // Note: this needs a 'using' declaration in subclasses.

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT IStreamServer
{
  public:
    // Note: for setPreamble() and sendToAllClients() to be visible in
    // subclasses they need the corresponding 'using' delcarations, e.g.  'using
    // IStreamServer::listen' in the subclass.

    // IO vector for gathered writes.
    struct IOV
    {
        const void *buf;
        std::size_t len;
    };

    explicit IStreamServer() = default;
    virtual ~IStreamServer();

    // True if the server is able to listen on the given url.
    virtual bool listen(const std::string &url) = 0;

    // True if the server i s listening on at least one address.
    virtual bool isListening() const = 0;

    // Stop listening and disconnect all clients.
    // Returns true if the server was listening and is now stopped, false
    // otherwise.
    virtual bool stop() = 0;

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

    // Set a preamble that is sent to newly connected clients before any other
    // data.
    virtual void setPreamble(const IOV *data, size_t n_iov) = 0;

    // Non-gather version of setPreamble.
    void setPreamble(const std::uint8_t *data, size_t size);

    IStreamServer(const IStreamServer &) = delete;
    IStreamServer &operator=(const IStreamServer &) = delete;
    IStreamServer(IStreamServer &&) = delete;
    IStreamServer &operator=(IStreamServer &&) = delete;
};

}

#endif /* DDE5B269_586E_432D_8652_774580A87FAB */
