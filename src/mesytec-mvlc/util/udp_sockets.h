#ifndef B89C2539_0D86_4151_8EEE_87B5BBB2FA52
#define B89C2539_0D86_4151_8EEE_87B5BBB2FA52

#include <system_error>

#ifndef __WIN32
#include <netinet/ip.h> // sockaddr_in
#else
#include <winsock2.h>
#endif

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/util/int_types.h"

namespace mesytec::mvlc::eth
{

// Creates, binds and connects a UDP socket. Uses an OS assigned local port
// number.
//
// Returns the socket on success or -1 on error. If ecp is non-null and an
// error occurs it will be stored in *ecp.
int MESYTEC_MVLC_EXPORT connect_udp_socket(const std::string &remoteHost, u16 remotePort, std::error_code *ecp = nullptr);

// Returns an unconnected UDP socket bound to the specified local port or -1 on
// error. If ecp is non-null and an error occurs it will be stored in *ecp.
int MESYTEC_MVLC_EXPORT bind_udp_socket(u16 localPort, std::error_code *ecp = nullptr);

// Returns the local port the socket is bound to or 0 on error. If ecp is
// non-null and an error occurs it will be stored in *ecp.
u16 MESYTEC_MVLC_EXPORT get_local_socket_port(int sock, std::error_code *ecp = nullptr);

std::error_code MESYTEC_MVLC_EXPORT set_socket_write_timeout(int sock, unsigned ms);
std::error_code MESYTEC_MVLC_EXPORT set_socket_read_timeout(int sock, unsigned ms);

std::error_code MESYTEC_MVLC_EXPORT close_socket(int sock);

// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code MESYTEC_MVLC_EXPORT lookup(const std::string &host, u16 port, sockaddr_in &dest);

std::error_code MESYTEC_MVLC_EXPORT receive_one_packet(
    int sockfd, u8 *dest, size_t maxSize, size_t &bytesTransferred,
    int timeout_ms, sockaddr_in *src_addr = nullptr);

std::error_code MESYTEC_MVLC_EXPORT write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred);
    //int timeout_ms);

std::string format_ipv4(u32 a);

std::error_code MESYTEC_MVLC_EXPORT set_socket_receive_buffer_size(
    int sockfd, int desiredBufferSize, int *actualBufferSize = 0);

}

#endif /* B89C2539_0D86_4151_8EEE_87B5BBB2FA52 */
