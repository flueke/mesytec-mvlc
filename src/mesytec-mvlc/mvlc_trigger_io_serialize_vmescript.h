#ifndef A603BEB6_BFB4_41CA_A8DF_A65F39E64A71
#define A603BEB6_BFB4_41CA_A8DF_A65F39E64A71

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>

namespace mesytec::mvlc::trigger_io
{

MESYTEC_MVLC_EXPORT std::string generate_trigger_io_vmescript(const TriggerIO &ioCfg);
MESYTEC_MVLC_EXPORT TriggerIO parse_trigger_io_vmescript(const std::string &text);

}

#endif /* A603BEB6_BFB4_41CA_A8DF_A65F39E64A71 */
