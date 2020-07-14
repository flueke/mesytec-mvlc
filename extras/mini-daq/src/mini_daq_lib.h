#ifndef __MESYTEC_MVLC_EXTRAS_MINI_DAQ_LIB_H__
#define __MESYTEC_MVLC_EXTRAS_MINI_DAQ_LIB_H__

#include <limits>
#include <unordered_map>

#include <mesytec-mvlc/mvlc_readout_parser.h>

namespace mesytec
{
namespace mvlc
{
namespace mini_daq
{

// Creates a a set of readout parser callbacks which update the given
// MiniDAQStats when invoked by the parser.
readout_parser::ReadoutParserCallbacks make_mini_daq_callbacks(bool logReadoutData);

} // end namespace mesytec
} // end namespace mvlc
} // end namespace mini_daq

#endif /* __MESYTEC_MVLC_EXTRAS_MINI_DAQ_LIB_H__ */
