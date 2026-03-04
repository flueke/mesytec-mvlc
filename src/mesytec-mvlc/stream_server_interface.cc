#include "stream_server_interface.h"

namespace mesytec::mvlc
{

IStreamServer::~IStreamServer() = default;

size_t IStreamServer::sendToAllClients(const uint8_t *data, size_t size)
{
    IOV iov{data, size};
    return sendToAllClients(&iov, 1);
}

}
