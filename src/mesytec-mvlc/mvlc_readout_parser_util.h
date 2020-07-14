#ifndef __MESYTEC_MVLC_MVLC_READOUT_PARSER_UTIL_H__
#define __MESYTEC_MVLC_MVLC_READOUT_PARSER_UTIL_H__

#include <ostream>

#include "mesytec-mvlc_export.h"
#include "mvlc_readout_parser.h"
#include "util/readout_buffer_queues.h"

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

// Driver function intended to run the readout parser in its own thread.
// Readout buffers are taken from the snoopQueues and are passed to the readout
// parser. Once a buffer has been processed it is re-enqueued on the empty
// snoop queue.

// Call like this:
//  parserThread = std::thread(
//      run_readout_parser,
//      std::ref(parserState),
//      std::ref(parserCounters),
//      std::ref(snoopQueues),
//      std::ref(parserCallbacks));
//
// To terminate enqueue an empty buffer onto snoopQueues.filledBufferQueue().
void MESYTEC_MVLC_EXPORT run_readout_parser(
    readout_parser::ReadoutParserState &state,
    Protected<readout_parser::ReadoutParserCounters> &counters,
    ReadoutBufferQueues &snoopQueues,
    readout_parser::ReadoutParserCallbacks &parserCallbacks);

MESYTEC_MVLC_EXPORT std::ostream &dump_counters(
    std::ostream &out, const ReadoutParserCounters &counters);

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_PARSER_UTIL_H__ */
