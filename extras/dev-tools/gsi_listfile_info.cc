#include <iostream>
#include <lyra/lyra.hpp>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <spdlog/spdlog.h>

using std::cerr;
using std::cout;
using namespace mesytec;
using mvlc::u32;

bool process_listfile(const std::string &listfile);

int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    std::vector<std::string> arg_listfiles;

    auto cli
        = lyra::help(opt_showHelp)
        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")
        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")
        | lyra::arg([&] (std::string arg) { arg_listfiles.emplace_back(arg); }, "listfiles")
            ("zip listfiles").cardinality(1, 1000)
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << "\n";
        return 1;
    }

    if (opt_showHelp)
    {
        cout << "mvlc-listfile-info: Collect and display info about MVLC listfiles.\n"
             << cli << "\n";
        return 0;
    }

    mvlc::set_global_log_level(spdlog::level::info);

    if (opt_logDebug)
        mvlc::set_global_log_level(spdlog::level::debug);

    if (opt_logTrace)
        mvlc::set_global_log_level(spdlog::level::trace);

    bool allGood = true;

    for (size_t i=0; i<arg_listfiles.size(); ++i)
    {
        const auto &listfile = arg_listfiles[i];
        cout << fmt::format("Processing listfile {}/{}: {}...\n", i+1, arg_listfiles.size(), listfile);
        allGood = allGood && process_listfile(listfile);
    }

    return allGood ? 0 : 1;
}

bool process_listfile(const std::string &listfile)
{
    mvlc::listfile::ZipReader zipReader;
    zipReader.openArchive(listfile);
    auto listfileEntryName = zipReader.firstListfileEntryName();

    if (listfileEntryName.empty())
    {
        std::cout << "Error: no listfile entry found in " << listfile << "\n";
        return false;
    }

    mvlc::listfile::ReadHandle *listfileReadHandle = {};

    try
    {
        listfileReadHandle = zipReader.openEntry(listfileEntryName);
    }
    catch (const std::exception &e)
    {
        std::cout << fmt::format("Error: could not open listfile entry {} for reading: {}\n", listfileEntryName, e.what());
        return false;
    }

    auto readerHelper = mvlc::listfile::make_listfile_reader_helper(listfileReadHandle);

    if (readerHelper.bufferFormat != mvlc::ConnectionType::USB)
    {
        std::cout << "Error: this tool expects listfiles in USB format!\n";
        return false;
    }

    using FrameParseState = mvlc::readout_parser::ReadoutParserState::FrameParseState;

    FrameParseState stackFrame;
    FrameParseState blockFrame;

    while (true)
    {
        auto buffer = read_next_buffer(readerHelper);
        auto bufferView = buffer->viewU32();

        if (buffer->empty())
            break;

        while (!bufferView.empty())
        {
            u32 header = bufferView.front();
            bufferView.remove_prefix(1);
            stackFrame = FrameParseState{ header };
            auto frameInfo = mvlc::extract_frame_info(header);

            if ((frameInfo.type == mvlc::frame_headers::StackFrame
                 || frameInfo.type == mvlc::frame_headers::StackContinuation)
                && frameInfo.stack == 4)
            {
                std::cout << fmt::format("Found StackFrame: {:#010x}, {}\n", header, mvlc::decode_frame_header(header));

                std::cout << fmt::format("  word[0]: {:#010x}\n", bufferView[0]);
                std::cout << fmt::format("  word[1]: {:#010x}\n", bufferView[1]);
                std::cout << fmt::format("  word[2]: {:#010x}\n", bufferView[2]);
                std::cout << fmt::format("  word[3]: {:#010x}\n", bufferView[3]);

                size_t i=0;

                while (i<frameInfo.len)
                {
                    // Note: very bad as there's guessing involved! Do not parse
                    // this way in real application code!
                    auto innerHeader = bufferView[i];
                    auto innerFrameInfo = mvlc::extract_frame_info(innerHeader);
                    if (innerFrameInfo.type == mvlc::frame_headers::BlockRead)
                    {
                        std::cout << fmt::format("  Found BlockFrame: {:#010x}, {}\n",
                            innerHeader, mvlc::decode_frame_header(innerHeader));
                        i += innerFrameInfo.len + 1;
                    }
                    else
                        ++i;
                }
            }

            bufferView.remove_prefix(frameInfo.len);
        }
    }

    return true;
}
