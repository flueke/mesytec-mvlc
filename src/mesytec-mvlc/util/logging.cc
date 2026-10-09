#include "logging.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include "string_util.h"

std::mutex g_mutex;

namespace mesytec
{
namespace mvlc
{

namespace
{

// Shared by all loggers created without explicit sinks. Guarded by g_mutex.
std::vector<spdlog::sink_ptr> &default_sinks_unlocked()
{
    static std::vector<spdlog::sink_ptr> sinks = { std::make_shared<spdlog::sinks::stderr_color_sink_mt>() };
    return sinks;
}

}

std::shared_ptr<spdlog::logger>
    create_logger(const std::string &name, const std::vector<spdlog::sink_ptr> &sinks)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    auto logger = spdlog::get(name);

    if (!logger)
    {
        const auto &loggerSinks = sinks.empty() ? default_sinks_unlocked() : sinks;
        logger = std::make_shared<spdlog::logger>(name, std::begin(loggerSinks), std::end(loggerSinks));

        try
        {
            // Registers the logger and applies the global level, pattern and flush settings.
            spdlog::initialize_logger(logger);
        }
        catch (const std::exception &e)
        { }
    }

    return logger;
}

std::vector<spdlog::sink_ptr> get_default_sinks()
{
    std::unique_lock<std::mutex> lock(g_mutex);
    return default_sinks_unlocked();
}

void set_default_sinks(const std::vector<spdlog::sink_ptr> &sinks)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    auto &defaultSinks = default_sinks_unlocked();
    const auto oldSinks = defaultSinks;
    defaultSinks = sinks;

    spdlog::apply_all(
        [&] (std::shared_ptr<spdlog::logger> logger)
        {
            if (logger->sinks() == oldSinks)
                logger->sinks() = sinks;
        });
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

std::optional<spdlog::level::level_enum>
    log_level_from_string(const std::string &levelName)
{
    if (auto result = spdlog::level::from_str(levelName); result != spdlog::level::off)
        return result;
    return {};
}

std::string log_level_to_string(const spdlog::level::level_enum &level)
{
    return spdlog::level::to_string_view(level).data();
}

void set_log_pattern(const std::string &pattern)
{
    spdlog::set_pattern(pattern);
}

} // end namespace mvlc
} // end namespace mesytec
