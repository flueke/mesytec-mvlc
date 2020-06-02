#include "mvlc_readout_parser_util.h"

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include <iostream>

using std::cerr;
using std::endl;

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

void run_readout_parser(
    readout_parser::ReadoutParserState &state,
    Protected<readout_parser::ReadoutParserCounters> &counters,
    ReadoutBufferQueues &snoopQueues,
    readout_parser::ReadoutParserCallbacks &parserCallbacks)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"readout_parser",0,0,0);
#endif

    try
    {
        cerr << "run_readout_parser entering loop" << endl;

        auto &filled = snoopQueues.filledBufferQueue();
        auto &empty = snoopQueues.emptyBufferQueue();

        while (true)
        {
            auto buffer = filled.dequeue(std::chrono::milliseconds(100));

            if (buffer && buffer->empty()) // sentinel
                break;
            else if (!buffer)
                continue;

            try
            {
                //cout << "buffer #" << buffer->bufferNumber() << endl;

                auto bufferView = buffer->viewU32();

                readout_parser::parse_readout_buffer(
                    buffer->type(),
                    state,
                    parserCallbacks,
                    counters,
                    buffer->bufferNumber(),
                    bufferView.data(),
                    bufferView.size());

                empty.enqueue(buffer);
            }
            catch (...)
            {
                empty.enqueue(buffer);
                throw;
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        {
            //auto state = protectedState.access();
            //state->eptr = std::current_exception();
        }

        cerr << "readout_parser caught a std::runtime_error: " << e.what() << endl;
    }
    catch (...)
    {
        {
            //auto state = protectedState.access();
            //state->eptr = std::current_exception();
        }

        cerr << "readout_parser caught an unknown exception." << endl;
    }

    cerr << "run_readout_parser left loop" << endl;
}

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec
