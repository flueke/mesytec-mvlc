#ifndef A603BEB6_BFB4_41CA_A8DF_A65F39E64A71
#define A603BEB6_BFB4_41CA_A8DF_A65F39E64A71

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>

namespace mesytec::mvlc::trigger_io
{

// Serialization of the TriggerIO structure to/from mvme VMEScript.
//
// The implementation does not really know VMEScript, there's no dependency on
// mvme. The generator just emits VMEScript formatted text, the parser contains
// a mini implementation doing just enough to parse the write commands and the
// meta info. No support for variable expansion or math expressions or any fancy
// VMEScript features as those are not used in the generated scripts.

MESYTEC_MVLC_EXPORT std::string generate_trigger_io_vmescript(const TriggerIO &ioCfg);
MESYTEC_MVLC_EXPORT TriggerIO parse_trigger_io_vmescript(const std::string &text);

}

#endif /* A603BEB6_BFB4_41CA_A8DF_A65F39E64A71 */
