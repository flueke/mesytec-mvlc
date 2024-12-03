#ifndef D9274010_ED5D_43E1_B6D0_B963F643A169
#define D9274010_ED5D_43E1_B6D0_B963F643A169

#include <string>
#include "mesytec-mvlc/mesytec-mvlc_export.h"

namespace mesytec::mvlc::util
{

std::string MESYTEC_MVLC_EXPORT yaml_to_json(const std::string &yamlText);
std::string MESYTEC_MVLC_EXPORT json_to_yaml(const std::string &jsonText);

}

#endif /* D9274010_ED5D_43E1_B6D0_B963F643A169 */
