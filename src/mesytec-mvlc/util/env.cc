#include "env.hpp"

namespace mesytec::mvlc::util
{

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
std::optional<std::string> get_env_variable(const char *name)
{
    char *pValue = nullptr;
    size_t len = 0;

    if (errno_t err = _dupenv_s( &pValue, &len, name))
        return std::nullopt;

    std::string ret(pValue);
    ::free(pValue);

    return ret;
}
#else
std::optional<std::string> get_env_variable(const char *name)
{
    if (const char *value = std::getenv(name))
        return std::string(value);

    return std::nullopt;
}
#endif

}
