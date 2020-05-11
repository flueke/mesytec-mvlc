#include <regex>
#include <mesytec-mvlc/mesytec_mvlc.h>
#include <mesytec-mvlc/mesytec_mvlc.h>
#include <mesytec-mvlc/mvlc_dialog_util.h>
#include <mesytec-mvlc/mvlc_eth_interface.h>
#include <mesytec-mvlc/mvlc_readout.h>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include <mesytec-mvlc/mvlc_stack_executor.h>
#include <mesytec-mvlc/mvlc_usb_interface.h>
#include <mesytec-mvlc/util/io_util.h>
#include <mesytec-mvlc/util/perf.h>
#include <mesytec-mvlc/util/readout_buffer_queues.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <mesytec-mvlc/util/string_view.hpp>
#include <fmt/format.h>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;
using namespace nonstd;

// Follows the framing structure inside the buffer until a partial frame at the
// end is detected. The partial data is moved over to the tempBuffer so that
// the readBuffer ends with a complete frame.
//
// The input buffer must start with a frame header (skip_count will be called
// with the first word of the input buffer on the first iteration).
//
// The SkipCountFunc must return the number of words to skip to get to the next
// frame header or 0 if there is not enough data left in the input iterator to
// determine the frames size.
// Signature of SkipCountFunc:  u32 skip_count(const BufferIterator &iter);
template<typename SkipCountFunc>
inline void fixup_buffer(ReadoutBuffer &readBuffer, ReadoutBuffer &tempBuffer, SkipCountFunc skip_count)
{
    auto view = readBuffer.viewU8();

    while (!view.empty())
    {
        if (view.size() >= sizeof(u32))
        {
            u32 wordsToSkip = skip_count(view);

            //cout << "wordsToSkip=" << wordsToSkip << ", view.size()=" << view.size() << ", in words:" << view.size() / sizeof(u32));

            if (wordsToSkip == 0 || wordsToSkip > view.size() / sizeof(u32))
            {
                // Move the trailing data into the temporary buffer. This will
                // truncate the readBuffer to the last complete frame or packet
                // boundary.


                tempBuffer.ensureFreeSpace(view.size());

                std::memcpy(tempBuffer.data() + tempBuffer.used(),
                            view.data(), view.size());
                tempBuffer.use(view.size());
                readBuffer.setUsed(readBuffer.used() - view.size());
                return;
            }

            // Skip over the SystemEvent frame or the ETH packet data.
            view.remove_prefix(wordsToSkip * sizeof(u32));
        }
    }
}

// The listfile contains two types of data:
// - System event sections identified by a header word with 0xFA in the highest
//   byte.
// - ETH packet data starting with the two ETH specific header words followed
//   by the packets payload
// The first ETH packet header can never have the value 0xFA because the
// highest two bits are always 0.
inline void fixup_buffer_eth(ReadoutBuffer &readBuffer, ReadoutBuffer &tempBuffer)
{
    auto skip_func = [](const basic_string_view<const u8> &view) -> u32
    {
        if (view.size() < sizeof(u32))
            return 0u;

        // Either a SystemEvent header or the first of the two ETH packet headers
        u32 header = *reinterpret_cast<const u32 *>(view.data());

        if (get_frame_type(header) == frame_headers::SystemEvent)
            return 1u + extract_frame_info(header).len;

        if (view.size() >= 2 * sizeof(u32))
        {
            u32 header1 = *reinterpret_cast<const u32 *>(view.data() + sizeof(u32));
            eth::PayloadHeaderInfo ethHdrs{ header, header1 };
            return eth::HeaderWords + ethHdrs.dataWordCount();
        }

        // Not enough data to get the 2nd ETH header word.
        return 0u;
    };

    fixup_buffer(readBuffer, tempBuffer, skip_func);
}

inline void fixup_buffer_usb(ReadoutBuffer &readBuffer, ReadoutBuffer &tempBuffer)
{
    auto skip_func = [] (const basic_string_view<const u8> &view) -> u32
    {
        if (view.size() < sizeof(u32))
            return 0u;

        u32 header = *reinterpret_cast<const u32 *>(view.data());
        return 1u + extract_frame_info(header).len;
    };

    fixup_buffer(readBuffer, tempBuffer, skip_func);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    ZipReader zr;
    zr.openArchive(argv[1]);

    std::string entryName;

    if (argc >= 3)
        entryName = argv[2];
    else
    {
        auto entryNames = zr.entryNameList();

        auto it = std::find_if(
            std::begin(entryNames), std::end(entryNames),
            [] (const std::string &entryName)
            {
                static const std::regex re(R"foo(.+\.mvlclst(\.lz4)?)foo");
                return std::regex_search(entryName, re);
            });

        if (it != std::end(entryNames))
            entryName = *it;
    }

    if (entryName.empty())
        return 2;

    auto &rh = *zr.openEntry(entryName);

    auto preamble = listfile::read_preamble(rh);

    if (!(preamble.magic == get_filemagic_eth()
          || preamble.magic == get_filemagic_usb()))
        return 3;

    CrateConfig crateConfig = {};

    {
        auto it = std::find_if(
            std::begin(preamble.systemEvents), std::end(preamble.systemEvents),
            [] (const auto &sysEvent)
            {
                return sysEvent.type == system_event::subtype::MVLCCrateConfig;
            });

        if (it == std::end(preamble.systemEvents))
            return 4;

        crateConfig = crate_config_from_yaml(it->contentsToString());
    }

    cout << "Read a CrateConfig from the listfile: " << endl;
    cout << to_yaml(crateConfig) << endl;

    cout << "Preamble SystemEvent types:" << endl;
    for (const auto &systemEvent: preamble.systemEvents)
    {
        cout << "  " << system_event_type_to_string(systemEvent.type) << endl;
    }

    cout << "Press the AnyKey to start the replay" << endl;
    std::getc(stdin);

    //const size_t BufferSize = util::Megabytes(1);
    //const size_t BufferCount = 10;
    //ReadoutBufferQueues snoopQueues(BufferSize, BufferCount);

    ReadoutBuffer destBuffer(util::Megabytes(1));
    ReadoutBuffer previousData(destBuffer.capacity());

    u32 nextOutputBufferNumber = 1u;
    destBuffer.setBufferNumber(nextOutputBufferNumber++);

    auto parserState = readout_parser::make_readout_parser(crateConfig.stacks);
    readout_parser::ReadoutParserCallbacks callbacks;

    callbacks.beginEvent = [] (int eventIndex)
    {
        //cout << "beginEvent " + std::to_string(eventIndex) << endl;
    };

    callbacks.moduleDynamic = [] (int ei, int mi,  const u32 *data, u32 size)
    {
        cout << "ei=" << ei << ", mi=" << mi << ", data=" << data << ", size=" << size << endl;
        //util::log_buffer(cout, basic_string_view<u32>(data, size));
    };

    // Read and fixup a buffer
    while (true)
    {
        if (previousData.used())
        {
            destBuffer.ensureFreeSpace(previousData.used());
            std::memcpy(destBuffer.data() + destBuffer.used(),
                        previousData.data(), previousData.used());
            destBuffer.use(previousData.used());
            previousData.clear();
        }

        size_t bytesRead = rh.read(destBuffer.data() + destBuffer.used(), destBuffer.free());
        destBuffer.use(bytesRead);
        destBuffer.setType(crateConfig.connectionType);

        if (bytesRead == 0)
            break;

        switch (crateConfig.connectionType)
        {
            case ConnectionType::ETH:
                fixup_buffer_eth(destBuffer, previousData);
                break;

            case ConnectionType::USB:
                fixup_buffer_usb(destBuffer, previousData);
                break;
        }

        auto bufferView = destBuffer.viewU32();

        readout_parser::parse_readout_buffer(
            destBuffer.type(),
            parserState,
            callbacks,
            destBuffer.bufferNumber(),
            bufferView.data(),
            bufferView.size());

        destBuffer.clear();
        destBuffer.setBufferNumber(nextOutputBufferNumber++);
    }


    return 0;
}
