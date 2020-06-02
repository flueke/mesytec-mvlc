#ifndef __MESYTEC_MVLC_MVLC_READOUT_PARSER_UTIL_H__
#define __MESYTEC_MVLC_MVLC_READOUT_PARSER_UTIL_H__

#include "mvlc_readout_parser.h"
#include "util/readout_buffer_queues.h"

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

// Call like this:
//  parserThread = std::thread(
//      run_readout_parser,
//      std::ref(parserState),
//      std::ref(parserCounters),
//      std::ref(snoopQueues),
//      std::ref(parserCallbacks));
//
// To terminate enqueue an empty buffer onto snoopQueues.filledBufferQueue().
void run_readout_parser(
    readout_parser::ReadoutParserState &state,
    Protected<readout_parser::ReadoutParserCounters> &counters,
    ReadoutBufferQueues &snoopQueues,
    readout_parser::ReadoutParserCallbacks &parserCallbacks);

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_PARSER_UTIL_H__ */
