#include "logging.h"

#include <spdlog/sinks/stdout_color_sinks.h>

std::mutex g_mutex;

namespace mesytec
{
namespace mvlc
{

std::shared_ptr<spdlog::logger>
    create_logger(const std::string &name, const std::vector<spdlog::sink_ptr> &sinks)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    auto logger = spdlog::get(name);

    //fmt::print(stderr, "mvlc::create_logger: name={}, logger={}\n", name, fmt::ptr(logger.get()));

    if (!logger)
    {
        if (!sinks.empty())
        {
            logger = std::make_shared<spdlog::logger>(name, std::begin(sinks), std::end(sinks));
        }
        else
        {
            logger = spdlog::stdout_color_mt(name);
        }

        if (logger)
        {
            try
            {
                spdlog::register_logger(logger);
            }
            catch (const std::exception &e)
            { }
        }
    }

    return logger;
}

std::shared_ptr<spdlog::logger>
    get_logger(const std::string &name)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    auto logger = spdlog::get(name);
    lock.unlock();

    if (!logger)
        logger = create_logger(name);

    return logger;
}

std::shared_ptr<spdlog::logger> default_logger()
{
    return spdlog::default_logger();
}

void set_global_log_level(spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}

std::vector<std::string> list_logger_names()
{
    std::vector<std::string> result;

    spdlog::apply_all(
        [&] (std::shared_ptr<spdlog::logger> logger)
        {
            result.emplace_back(logger->name());
        });

    return result;
}

std::vector<std::string> get_logger_names()
{
    return list_logger_names();
}

} // end namespace mvlc
} // end namespace mesytec
