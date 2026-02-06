#include "stream_server_interface.h"

namespace mesytec::mvlc
{

IStreamServer::~IStreamServer() = default;

size_t IStreamServer::sendToAllClients(const uint8_t *data, size_t size)
{
    IOV iov{data, size};
    return sendToAllClients(&iov, 1);
}

size_t IStreamServer::sendToAllClients(const std::vector<uint8_t> &data)
{
    return sendToAllClients(data.data(), data.size());
}

void IStreamServer::setPreamble(const std::uint8_t *data, size_t size)
{
    IOV iov{data, size};
    setPreamble(&iov, 1);
}

void IStreamServer::setPreamble(const std::vector<std::uint8_t> &data)
{
    setPreamble(data.data(), data.size());
}

}
