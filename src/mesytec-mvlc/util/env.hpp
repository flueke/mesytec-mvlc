#ifndef D6F74ED9_1D7E_4EFB_BB36_AD960F6BB645
#define D6F74ED9_1D7E_4EFB_BB36_AD960F6BB645

#include <optional>
#include <string>

#include "mesytec-mvlc/mesytec-mvlc_export.h"

namespace mesytec::mvlc::util
{

std::optional<std::string> MESYTEC_MVLC_EXPORT get_env_variable(const char *name);

}

#endif /* D6F74ED9_1D7E_4EFB_BB36_AD960F6BB645 */
