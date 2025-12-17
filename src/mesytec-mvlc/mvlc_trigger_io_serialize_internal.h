#ifndef D8EC5170_8820_44CA_97B1_A29F27F61406
#define D8EC5170_8820_44CA_97B1_A29F27F61406

#include <mesytec-mvlc/mvlc_trigger_io_serialize.h>
#include <string>

namespace mesytec::mvlc::trigger_io
{

BasicPart select_unit(int level, int unit, const std::string &unitName = {});
BasicPart write_unit_reg(u16 reg, u16 value, const std::string &comment, unsigned writeOpts = 0u);
BasicPart write_connection(u16 offset, u16 value, const std::string &sourceName = {});
BasicPart write_strobe_connection(u16 offset, u16 value, const std::string &sourceName = {});

namespace io_flags
{
    using Flags = u8;
    static const Flags HasDirection    = 1u << 0;
    static const Flags HasActivation   = 1u << 1;

    static const Flags None             = 0u;
    static const Flags NIM_IO_Flags     = HasDirection | HasActivation;
    static const Flags ECL_IO_Flags     = HasActivation;
}

BasicParts generate(const trigger_io::Timer &unit, int index);
BasicParts generate(const trigger_io::IO &io, const io_flags::Flags &ioFlags, u16 offset = 0);
BasicParts generate(const trigger_io::TriggerResource &unit, int index);
BasicParts generate(const trigger_io::StackBusy &unit, int index);
trigger_io::LUT_RAM make_lut_ram(const LUT &lut);
BasicParts write_lut_ram(const trigger_io::LUT_RAM &ram);
BasicParts write_lut(const LUT &lut);
BasicParts generate(const trigger_io::StackStart &unit, int index);
BasicParts generate(const trigger_io::MasterTrigger &unit, int index);
BasicParts generate(const trigger_io::Counter &unit, int index);

}

#endif /* D8EC5170_8820_44CA_97B1_A29F27F61406 */
