#include <iostream>
#include <lyra/lyra.hpp>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <zmq.hpp>

using std::cerr;
using std::cout;
using namespace mesytec;

int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    int opt_bindPort = 5575;
    std::string arg_listfile;

    auto cli
        = lyra::help(opt_showHelp)
        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")
        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")
        | lyra::opt(opt_bindPort, "port")["--bind-port"]("local port to bind the zmq pub socket to (default = 5575)")
        | lyra::arg(arg_listfile, "listfile")("listfile zip file").required()
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << "\n";
        return 1;
    }

    if (opt_showHelp)
    {
        cout << "mvlc-zmq-listfile-sender: Sends data buffers from an input listfile via a ZMQ PUB socket.\n"
             << cli << "\n";
        return 0;
    }

    mvlc::set_global_log_level(spdlog::level::info);

    if (opt_logDebug)
        mvlc::set_global_log_level(spdlog::level::debug);

    if (opt_logTrace)
        mvlc::set_global_log_level(spdlog::level::trace);

    std::string bindUrl = "tcp://*:" + std::to_string(opt_bindPort);

    mvlc::listfile::ZipReader zipReader;
    zipReader.openArchive(arg_listfile);
    auto listfileEntryName = zipReader.firstListfileEntryName();

    if (listfileEntryName.empty())
    {
        std::cerr << "Error: no listfile entry found in " << arg_listfile << "\n";
        return 1;
    }

    auto listfileReadHandle = zipReader.openEntry(listfileEntryName);
    auto listfilePreamble = mvlc::listfile::read_preamble(*listfileReadHandle);
    const auto bufferFormat = (listfilePreamble.magic == mvlc::listfile::get_filemagic_eth()
        ? mvlc::ConnectionType::ETH
        : mvlc::ConnectionType::USB);

    listfileReadHandle->seek(mvlc::listfile::get_filemagic_len());

    std::cout << "Found listfile entry " << listfileEntryName << ", filemagic=" << listfilePreamble.magic << "\n";

    // Two buffers, one to read data into, one to store temporary data after
    // fixing up the destination buffer.
    mvlc::ReadoutBuffer destBuf(1u << 20);
    std::vector<std::uint8_t> tempBuf;

    destBuf.setType(bufferFormat);

    size_t totalBytesRead = 0;
    size_t totalBytesPublished = 0;
    size_t messagesPublished = 0;

    zmq::context_t context;
    auto pub = zmq::socket_t(context, ZMQ_PUB);
    pub.bind(bindUrl);

    std::cout << "zmq socket bound to " << bindUrl << ". Press enter to start publishing listfile data...\n";
    std::getc(stdin);

    while (true)
    {
        // Copy data from the temp buffer into the dest buffer.
        destBuf.ensureFreeSpace(tempBuf.size());
        std::copy(std::begin(tempBuf), std::end(tempBuf), destBuf.data());
        destBuf.setUsed(tempBuf.size());
        tempBuf.clear();

        size_t bytesRead = listfileReadHandle->read(
            destBuf.data() + destBuf.used(), destBuf.free());

        if (bytesRead == 0)
            break;

        destBuf.use(bytesRead);
        totalBytesRead += bytesRead;

        // Ensures that destBuf contains only complete frames/packets. Can move
        // trailing data from destBuf into tempBuf.
        size_t bytesMoved = mvlc::fixup_buffer(bufferFormat, destBuf.data(), destBuf.used(), tempBuf);
        destBuf.setUsed(destBuf.used() - bytesMoved);

        zmq::message_t msg(destBuf.used());
        std::memcpy(msg.data(), destBuf.data(), destBuf.used());
        pub.send(msg, zmq::send_flags::none);
        totalBytesPublished += destBuf.used();
        ++messagesPublished;
        destBuf.clear();
    }

    std::cout << "Replay done, read " << totalBytesRead << " bytes from listfile"
        << ", sent " << totalBytesPublished << " bytes in " << messagesPublished << " messages\n";

    return 0;
}
