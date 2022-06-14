#ifndef __MESYTEC_MVLC_LOGGING_H__
#define __MESYTEC_MVLC_LOGGING_H__

#include <string>
#include <spdlog/spdlog.h>

#include "mesytec-mvlc/mesytec-mvlc_export.h"

namespace mesytec
{
namespace mvlc
{

std::shared_ptr<spdlog::logger> MESYTEC_MVLC_EXPORT
    get_logger(const std::string &name);

std::shared_ptr<spdlog::logger> MESYTEC_MVLC_EXPORT
    create_logger(const std::string &name, const std::vector<spdlog::sink_ptr> &sinks = {});

void MESYTEC_MVLC_EXPORT
    set_global_log_level(spdlog::level::level_enum level);

MESYTEC_MVLC_EXPORT std::vector<std::string> list_logger_names();

template<typename View>
void log_buffer(const std::shared_ptr<spdlog::logger> &logger,
                const spdlog::level::level_enum &level,
                const View &buffer, const std::string &header)
{
    if (!logger->should_log(level))
        return;

    logger->log(level, "begin buffer '{}' (size={})", header, buffer.size());

    for (const auto &value: buffer)
        logger->log(level, "  0x{:008X}", value);

    logger->log(level, "end buffer '{}' (size={})", header, buffer.size());

}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_LOGGING_H__ */
