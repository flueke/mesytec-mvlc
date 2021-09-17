#include "logging.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace
{
    static const auto LoggerNames =
    {
        "cmd_pipe_reader",
        "listfile",
        "mvlc",
        "mvlc_eth",
        "mvlc_usb",
        "readout",
        "readout_parser",
    };
};

namespace mesytec
{
namespace mvlc
{

std::vector<std::shared_ptr<spdlog::logger>> setup_loggers(const std::vector<spdlog::sink_ptr> &sinks)
{
    std::vector<std::shared_ptr<spdlog::logger>> ret;
    ret.reserve(LoggerNames.size());

    for (auto loggerName: LoggerNames)
    {
        auto logger = spdlog::get(loggerName);

        if (!logger)
        {
            if (!sinks.empty())
            {
                logger = std::make_shared<spdlog::logger>(loggerName, std::begin(sinks), std::end(sinks));
                spdlog::register_logger(logger);
            }
            else
            {
                logger = spdlog::stdout_color_mt(loggerName);
            }
        }

        ret.emplace_back(logger);
    }

    return ret;
}

} // end namespace mvlc
} // end namespace mesytec
