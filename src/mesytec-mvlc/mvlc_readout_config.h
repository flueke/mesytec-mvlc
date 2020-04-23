#ifndef __MESYTEC_MVLC_MVLC_READOUT_CONFIG_H__
#define __MESYTEC_MVLC_MVLC_READOUT_CONFIG_H__

#include "mesytec-mvlc_export.h"

#include "mvlc_constants.h"
#include "mvlc_command_builders.h"

namespace mesytec
{
namespace mvlc
{

struct MESYTEC_MVLC_EXPORT CrateConfig
{
    ConnectionType connectionType;
    int usbIndex = -1;
    std::string usbSerial;
    std::string ethHost;

    std::vector<StackCommandBuilder> stacks;
    std::vector<u32> triggers;
    StackCommandBuilder initCommands;
    StackCommandBuilder stopCommands;
    StackCommandBuilder initTriggerIO;

    bool operator==(const CrateConfig &o) const;
    bool operator!=(const CrateConfig &o) const { return !(*this == o); }
};

std::string MESYTEC_MVLC_EXPORT to_yaml(const CrateConfig &crateConfig);
CrateConfig MESYTEC_MVLC_EXPORT crate_config_from_yaml(const std::string &yaml);
CrateConfig MESYTEC_MVLC_EXPORT crate_config_from_yaml(std::istream &input);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_CONFIG_H__ */
