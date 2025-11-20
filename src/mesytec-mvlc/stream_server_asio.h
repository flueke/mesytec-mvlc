#ifndef STREAM_SERVER_ASIO_H
#define STREAM_SERVER_ASIO_H


#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/stream_server_interface.h"
#include <memory>

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT StreamServerAsio: public IStreamServer
{
  public:
    explicit StreamServerAsio(size_t io_threads=1);
    ~StreamServerAsio() override;

    using IStreamServer::listen;
    using IStreamServer::sendToAllClients;

    bool listen(const std::string &uri) override;

    //// Stop listening and disconnect all clients. Idempotent.
    //void stop() override;

    bool isRunning() const override;
    size_t clientCount() const override;

    std::vector<std::string> listenAddresses() const override;
    std::vector<std::string> clientAddresses() const override;

    ssize_t sendToAllClients(const IOV *iov, size_t n_iov) override;
    void setPreamble(const std::uint8_t *data, size_t size) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
    friend struct TcpAcceptor;
    friend struct UnixAcceptor;
};

} // namespace mesytec::mvlc

#endif // STREAM_SERVER_ASIO_H
