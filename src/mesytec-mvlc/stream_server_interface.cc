#include "stream_server_interface.h"

namespace mesytec::mvlc
{

IStreamServer::~IStreamServer() = default;

size_t IStreamServer::listen(const std::vector<std::string> &uris)
{
    size_t res = 0;
    for (const auto &uri: uris)
    {
        if (listen(uri))
            ++res;
    }
    return res;
}

ssize_t IStreamServer::sendToAllClients(const uint8_t *data, size_t size)
{
    IOV iov{data, size};
    return sendToAllClients(&iov, 1);
}

}
