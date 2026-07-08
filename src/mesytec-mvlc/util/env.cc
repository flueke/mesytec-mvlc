#include "env.hpp"
#include <cstdlib>

namespace mesytec::mvlc::util
{

std::optional<std::string> get_env_variable(const char *name)
{
    if (const char *value = std::getenv(name))
        return std::string(value);

    return std::nullopt;
}

}
