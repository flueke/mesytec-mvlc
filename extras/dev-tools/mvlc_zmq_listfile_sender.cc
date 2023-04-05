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
    std::array<mvlc::ReadoutBuffer, 2> buffers =
    {
        mvlc::ReadoutBuffer(1u << 20),
        mvlc::ReadoutBuffer(1u << 20),
    };

    mvlc::ReadoutBuffer *destBuf = &buffers[0];
    mvlc::ReadoutBuffer *tempBuf = &buffers[1];

    destBuf->setType(bufferFormat);
    tempBuf->setType(bufferFormat);

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
        // Note: taking into account that there can be unprocessed data from the
        // fixup_buffer() call in the current destBuffer (destBuf->used() offset).
        size_t bytesRead = listfileReadHandle->read(
            destBuf->data() + destBuf->used(), destBuf->free());

        if (bytesRead == 0)
            break;

        destBuf->use(bytesRead);
        totalBytesRead += bytesRead;

        // Ensures that destBuf contains only complete frames/packets. Can move
        // trailing data from destBuf into tempBuf.
        mvlc::fixup_buffer(bufferFormat, *destBuf, *tempBuf);

        zmq::message_t msg(destBuf->used());
        std::memcpy(msg.data(), destBuf->data(), destBuf->used());
        pub.send(msg, zmq::send_flags::none);
        totalBytesPublished += destBuf->used();
        ++messagesPublished;

        // Clear dest buffer and swap the two buffers => On next iteration will
        // read into tempBuf which may now contain temporary data from the fixup
        // step. The now empty destBuf will be used as the new temporary buffer.
        destBuf->clear();
        std::swap(destBuf, tempBuf);

    }

    std::cout << "Replay done, read " << totalBytesRead << " bytes from listfile"
        << ", sent " << totalBytesPublished << " bytes in " << messagesPublished << " messages\n";

    return 0;
}
