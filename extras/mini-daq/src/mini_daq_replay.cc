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

    {
        auto magic = listfile::read_magic(rh);
        auto view = string_view(reinterpret_cast<const char *>(magic.data()), magic.size());

        if (!(view == get_filemagic_eth()
              || view == get_filemagic_usb()))
        {
            return 3;
        }
    }

    auto preamble = listfile::read_preamble(rh);

    {
        auto it = std::find_if(
            std::begin(preamble), std::end(preamble),
            [] (const auto &sysEvent)
            {
                return sysEvent.type == system_event::subtype::MVLCCrateConfig;
            });

        if (it == std::end(preamble))
            return 4;
    }
}
