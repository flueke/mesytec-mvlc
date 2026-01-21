#ifndef STREAM_SERVER_ASIO_H
#define STREAM_SERVER_ASIO_H


#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/stream_server_interface.h"
#include <memory>

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT StreamServer: public IStreamServer
{
  public:
    explicit StreamServer();
    ~StreamServer() override;

    using IStreamServer::sendToAllClients;
    using IStreamServer::setPreamble;

    bool listen(const std::string &uri) override;
    bool isListening() const override;
    bool stop() override;

    size_t clientCount() const override;
    std::vector<std::string> listenAddresses() const override;
    std::vector<std::string> clientAddresses() const override;

    size_t sendToAllClients(const IOV *iov, size_t n_iov) override;
    void setPreamble(const IOV *data, size_t n_iov) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
    friend struct TcpAcceptor;
    friend struct UnixAcceptor;
};

} // namespace mesytec::mvlc

#endif // STREAM_SERVER_ASIO_H
