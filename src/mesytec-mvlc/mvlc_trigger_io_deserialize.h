#ifndef AFF40A97_3888_4C52_8E42_5CB2D754CF79
#define AFF40A97_3888_4C52_8E42_5CB2D754CF79

#include <unordered_map>

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>
#include <mesytec-mvlc/util/int_types.h>

namespace mesytec::mvlc::trigger_io
{

// Abstractions for deserializing/parsing the TriggerIO structure from any
// format.
//
// The deserializer works in terms of write commands to the unit selection and
// settings registers. These writes are analyzed and the settings are applied to
// the correct elements in the TriggerIO structure.
//
// A concrete deserializer needs to parse the input format (e.g. mvme VMEScript)
// and fill a flat list of write commands.
//
// Additionally the yaml meta block containing unit names and other info can be
// extracted from the concrete format and passed to the deserializer. This info
// will then be applied to the resulting TriggerIO structure.

struct MESYTEC_MVLC_EXPORT WriteCommand
{
    u32 address;
    u32 value;
};

using FlatWrites = std::vector<WriteCommand>;

// This goes from flat list of register writes + optional meta info to a fully
// initialized TriggerIO structure.
TriggerIO MESYTEC_MVLC_EXPORT deserialize_trigger_io(const FlatWrites &writes, const std::string &metaYaml = {});

// More detailed stuff for use by the implementation and concrete deserializers.
void MESYTEC_MVLC_EXPORT apply_meta_info_yaml(TriggerIO &ioCfg, const std::string &metaYaml);

using RegisterWrites = std::unordered_map<u16, u16>;
using UnitWrites = std::unordered_map<u16, RegisterWrites>;
using LevelWrites = std::array<UnitWrites, 4>; // write commands grouped by trigger io level

// Turns a flat list of writes into the nested LevelWrites structure.
LevelWrites MESYTEC_MVLC_EXPORT level_writes_from_flat_writes(const std::vector<WriteCommand> &writes);

// From nested per-level writes to TriggerIO.
TriggerIO MESYTEC_MVLC_EXPORT deserialize_trigger_io(const LevelWrites &levelWrites, const std::string &metaYaml = {});

// Helpers for certain unit types.
IO MESYTEC_MVLC_EXPORT parse_io(const RegisterWrites &writes, u16 offset = 0);
LUT_RAM MESYTEC_MVLC_EXPORT parse_lut_ram(const RegisterWrites &writes);
LUT MESYTEC_MVLC_EXPORT parse_lut(const RegisterWrites &writes);

}

#endif /* AFF40A97_3888_4C52_8E42_5CB2D754CF79 */
