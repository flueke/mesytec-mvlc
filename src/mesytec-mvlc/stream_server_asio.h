#ifndef C14948AC_3867_4F71_A5DA_E39765D9A95C
#define C14948AC_3867_4F71_A5DA_E39765D9A95C

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/stream_server_interface.h"
#include <memory>

namespace mesytec::mvlc::stream
{

class MESYTEC_MVLC_EXPORT StreamServer: public IStreamServer
{
  public:
    explicit StreamServer();
    ~StreamServer() override;

    bool listen(const std::string &uri) override;
    bool isListening() const override;
    bool stop() override;

    size_t clientCount() const override;
    std::vector<std::string> listenAddresses() const override;
    std::vector<std::string> clientAddresses() const override;

    size_t sendToAllClients(const IOV *iov, size_t n_iov) override;
    using IStreamServer::sendToAllClients;

    void setPreamble(const IOV *data, size_t n_iov) override;
    using IStreamServer::setPreamble;
    std::vector<std::uint8_t> getPreamble() const;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvlc

#endif /* C14948AC_3867_4F71_A5DA_E39765D9A95C */
