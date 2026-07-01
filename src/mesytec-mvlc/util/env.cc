#include "env.hpp"

namespace mesytec::mvlc::util
{

inline std::optional<std::string> get_env_variable(const char *name)
{
    char *pValue = nullptr;
    size_t len = 0;

    if (errno_t err = _dupenv_s( &pValue, &len, name))
        return std::nullopt;

    std::string ret(pValue);
    ::free(pValue);

    return ret;
}

}
