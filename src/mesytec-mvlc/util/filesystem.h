#ifndef __MESYTEC_MVLC_UTIL_FILESYSTEM_H__
#define __MESYTEC_MVLC_UTIL_FILESYSTEM_H__

#include <string>
#include "mesytec-mvlc_export.h"

namespace mesytec
{
namespace mvlc
{
namespace util
{

std::string MESYTEC_MVLC_EXPORT basename(const std::string &filepath);
std::string MESYTEC_MVLC_EXPORT dirname(const std::string &filepath);
bool MESYTEC_MVLC_EXPORT file_exists(const std::string &filepath);

}
}
}

#endif /* __MESYTEC_MVLC_UTIL_FILESYSTEM_H__ */
