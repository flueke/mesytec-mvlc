#ifndef E1CA04AB_C0C5_4EF6_822E_4E63AD013010
#define E1CA04AB_C0C5_4EF6_822E_4E63AD013010

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_command_builders.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>

namespace mesytec::mvlc::trigger_io
{

// Serialization of the TriggerIO structure to/from mvlc script (aka StackCommands).

// To/from StackCommandBuilder structures containing mvlc vme write commands.
MESYTEC_MVLC_EXPORT StackCommandBuilder generate_trigger_io_command_builder(const TriggerIO &ioCfg);
MESYTEC_MVLC_EXPORT TriggerIO parse_trigger_io_command_builder(const StackCommandBuilder &builder);
MESYTEC_MVLC_EXPORT TriggerIO parse_trigger_io_command_builder(const std::string &yamlText);

// To/from flat mvlc script text. One write command per line. Comments start
// with '#'.  The TriggerIO meta block is stored in commented out yaml at the
// end of the script after the line '# TriggerIO Meta Info:'.
MESYTEC_MVLC_EXPORT std::string generate_trigger_io_mvlcscript(const TriggerIO &ioCfg);
MESYTEC_MVLC_EXPORT TriggerIO parse_trigger_io_mvlcscript(const std::string &text);


} // namespace mesytec::mvlc::trigger_io

#endif /* E1CA04AB_C0C5_4EF6_822E_4E63AD013010 */
