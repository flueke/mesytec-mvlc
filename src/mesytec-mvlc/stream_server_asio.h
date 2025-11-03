#ifndef STREAM_SERVER_ASIO_H
#define STREAM_SERVER_ASIO_H

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT StreamServerAsio
{
  public:
    struct IOV
    {
        const void *buf;
        size_t len;
    };

    static const size_t DefaultIoThreads = 1;

    StreamServerAsio();
    ~StreamServerAsio();

    bool listen(const std::string &uri);
    size_t listen(const std::vector<std::string> &uris);
    void stop();
    bool isListening() const;
    std::vector<std::string> listenUris() const;
    std::vector<std::string> clients() const;

    // Send data to all clients in a blocking fashion
    ssize_t sendToAllClients(const uint8_t *data, size_t size);
    ssize_t sendToAllClients(const IOV *iov, size_t n_iov);

  private:
    struct Private;
    std::unique_ptr<Private> d;
    friend struct TcpAcceptor;
    friend struct UnixAcceptor;
};

} // namespace mesytec::mvlc

#endif // STREAM_SERVER_ASIO_H
