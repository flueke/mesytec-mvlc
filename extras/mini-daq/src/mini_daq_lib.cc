#include "mini_daq_lib.h"
#include <mesytec-mvlc/util/io_util.h>
#include <fmt/format.h>

using std::endl;

namespace mesytec
{
namespace mvlc
{
namespace mini_daq
{

readout_parser::ReadoutParserCallbacks make_mini_daq_callbacks(bool logReadoutData)
{
    readout_parser::ReadoutParserCallbacks callbacks;

    callbacks.beginEvent = [] (int eventIndex)
    {
    };

    callbacks.groupPrefix = [logReadoutData] (int ei, int mi, const u32 *data, u32 size)
    {
        if (logReadoutData)
            util::log_buffer(std::cout, basic_string_view<u32>(data, size), "module prefix");
    };

    callbacks.groupDynamic = [logReadoutData] (int ei, int mi, const u32 *data, u32 size)
    {
        if (logReadoutData)
            util::log_buffer(std::cout, basic_string_view<u32>(data, size), "module dynamic");
    };

    callbacks.groupSuffix = [logReadoutData] (int ei, int mi, const u32 *data, u32 size)
    {
        if (logReadoutData)
            util::log_buffer(std::cout, basic_string_view<u32>(data, size), "module suffix");
    };

    return callbacks;
}

} // end namespace mesytec
} // end namespace mvlc
} // end namespace mini_daq
