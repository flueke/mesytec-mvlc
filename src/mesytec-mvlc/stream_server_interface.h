#ifndef DDE5B269_586E_432D_8652_774580A87FAB
#define DDE5B269_586E_432D_8652_774580A87FAB

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <cstdint>
#include <string>
#include <vector>

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT IStreamServer
{
  public:
    // IO vector for gathered writes.
    struct IOV
    {
        const void *buf;
        std::size_t len;
    };

    IStreamServer(const IStreamServer &) = delete;
    IStreamServer &operator=(const IStreamServer &) = delete;
    IStreamServer(IStreamServer &&) = delete;
    IStreamServer &operator=(IStreamServer &&) = delete;

    explicit IStreamServer() = default;
    virtual ~IStreamServer();

    // True if the server is able to listen on the given uri.
    virtual bool listen(const std::string &uri) = 0;

    // Returns the number of successfully listened URIs.
    // Note: this is not visible in subclasses. They have to have a 'using
    // IStreamSever::listen' in their declaration.
    virtual size_t listen(const std::vector<std::string> &uris);

    // TODO: is this needed?
    //// Stop listening and disconnect all clients. Idempotent.
    //virtual void stop() = 0;

    virtual void stop() = 0;
    //virtual bool isRunning() const = 0;
    virtual size_t clientCount() const = 0;

    virtual bool isListening() const = 0;
    virtual std::vector<std::string> listenAddresses() const = 0;
    virtual std::vector<std::string> clientAddresses() const = 0;

    // Send data to all clients in a blocking fashion.
    // The senders network byte order is used, no swapping is done.
    // Data is gathered from the given IOV array.
    // Returns the number of clients the data was sent to or -1 on error.
    virtual ssize_t sendToAllClients(const IOV *iov, size_t n_iov) = 0;

    // Send data to all clients in a blocking fashion.
    // The senders network byte order is used, no swapping is done.
    // Returns the number of clients the data was sent to or -1 on error.
    // Note: this also needs a 'using' declaration in subclasses.
    ssize_t sendToAllClients(const uint8_t *data, size_t size);

    // Set a preamble that is sent to newly connected clients before any other data.
    virtual void setPreamble(const std::uint8_t *data, size_t size) = 0;
};

}

#endif /* DDE5B269_586E_432D_8652_774580A87FAB */
