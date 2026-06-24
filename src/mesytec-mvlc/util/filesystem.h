#ifndef __MESYTEC_MVLC_UTIL_FILESYSTEM_H__
#define __MESYTEC_MVLC_UTIL_FILESYSTEM_H__

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <memory>
#include <string>
#include <unistd.h> // ::close()

namespace mesytec::mvlc::util
{

std::string MESYTEC_MVLC_EXPORT basename(const std::string &filepath);
std::string MESYTEC_MVLC_EXPORT dirname(const std::string &filepath);
bool MESYTEC_MVLC_EXPORT file_exists(const std::string &filepath);
bool MESYTEC_MVLC_EXPORT delete_file(const std::string &filepath);

// Scoped cleanup for file descriptors.
// clang-format off
static auto fd_deleter = [](int* fd) { if (fd && *fd >= 0) ::close(*fd); delete fd; };
static auto make_fd_deleter = [](int fd) { return std::unique_ptr<int, decltype(fd_deleter)>(new int(fd), fd_deleter); };
// clang-format on

// Example usage:
//    auto tmpFd = make_fd_deleter(::mkstemp(tmpNameTemplate.data()));
//    if (!tmpFd || *tmpFd == -1)
//      throw std::runtime_error("mkstemp failed");

} // namespace mesytec::mvlc::util

#endif /* __MESYTEC_MVLC_UTIL_FILESYSTEM_H__ */
