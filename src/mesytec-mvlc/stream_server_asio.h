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
    static const size_t DefaultIoThreads = 1;

    StreamServerAsio();
    ~StreamServerAsio() override;

    using IStreamServer::listen;
    using IStreamServer::sendToAllClients;

    // True if the server is able to listen on the given uri.
    bool listen(const std::string &uri) override;


    // Stop listening and disconnect all clients. Idempotent.
    void stop();

    bool isListening() const override;
    std::vector<std::string> listenUris() const override;
    std::vector<std::string> clients() const override;

    virtual ssize_t sendToAllClients(const IOV *iov, size_t n_iov) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
    friend struct TcpAcceptor;
    friend struct UnixAcceptor;
};

} // namespace mesytec::mvlc

#endif // STREAM_SERVER_ASIO_H
