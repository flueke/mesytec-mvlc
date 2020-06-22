#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <fstream>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_usb_interface.h>
#include <mesytec-mvlc/mvlc_eth_interface.h>
#include <fmt/format.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

#if 0
void stack_error_notification_poller(
    MVLC mvlc,
    Protected<ReadoutWorker::Counters> &counters,
    std::atomic<bool> &quit)
{
    static constexpr auto Default_PollInterval = std::chrono::milliseconds(1000);

#ifndef __WIN32
    prctl(PR_SET_NAME,"error_poller",0,0,0);
#endif

    std::vector<u32> buffer;
    buffer.reserve(util::Megabytes(1));

    std::cout << "stack_error_notification_poller entering loop" << std::endl;

    while (!quit)
    {
        //std::cout << "stack_error_notification_poller begin read" << std::endl;
        auto ec = mvlc.readKnownBuffer(buffer);
        //std::cout << "stack_error_notification_poller read done" << std::endl;

        if (!buffer.empty())
        {
            std::cout << "stack_error_notification_poller updating counters" << std::endl;
            update_stack_error_counters(counters.access()->stackErrors, buffer);
        }

        if (ec == ErrorType::ConnectionError)
            break;

        if (buffer.empty())
        {
            //std::cout << "stack_error_notification_poller sleeping" << std::endl;
            std::this_thread::sleep_for(Default_PollInterval);
            //std::cout << "stack_error_notification_poller waking" << std::endl;
        }
    }

    std::cout << "stack_error_notification_poller exiting" << std::endl;
}
#endif

int main(int argc, char *argv[])
{
    // MVLC connection overrides
    std::string opt_mvlcEthHost;
    bool opt_mvlcUseFirstUSBDevice = false;
    int opt_mvlcUSBIndex = -1;
    std::string opt_mvlcUSBSerial;

    // listfile and run options
    std::string opt_crateConfig;
    unsigned opt_secondsToRun = 10;

    bool opt_showHelp = false;

    auto cli
        = lyra::help(opt_showHelp)

        | lyra::opt(opt_mvlcEthHost, "hostname")
            ["--mvlc-eth"] ("mvlc ethernet hostname (overrides CrateConfig)")

        | lyra::opt(opt_mvlcUseFirstUSBDevice)
            ["--mvlc-usb"] ("connect to the first mvlc usb device (overrides CrateConfig)")

        | lyra::opt(opt_mvlcUSBIndex, "index")
            ["--mvlc-usb-index"] ("connect to the mvlc with the given usb device index (overrides CrateConfig)")

        | lyra::opt(opt_mvlcUSBSerial, "serial")
            ["--mvlc-usb-serial"] ("connect to the mvlc with the given usb serial number (overrides CrateConfig)")

        | lyra::arg(opt_crateConfig, "crateConfig")
            ("crate config yaml file").required()

        | lyra::arg(opt_secondsToRun, "secondsToRun")
            ("duration the DAQ should run in seconds").required()
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << endl;
        return 1;
    }

    if (opt_showHelp)
    {
        cout << cli << endl;
        return 0;
    }

    std::ifstream inConfig(opt_crateConfig);

    if (!inConfig.is_open())
    {
        cerr << "Error opening crate config " << argv[1] << " for reading." << endl;
        return 1;
    }

    try
    {
        auto timeToRun = std::chrono::seconds(opt_secondsToRun);
        auto crateConfig = crate_config_from_yaml(inConfig);

        MVLC mvlc;

        if (!opt_mvlcEthHost.empty())
            mvlc = make_mvlc_eth(opt_mvlcEthHost);
        else if (opt_mvlcUseFirstUSBDevice)
            mvlc = make_mvlc_usb();
        else if (opt_mvlcUSBIndex >= 0)
            mvlc = make_mvlc_usb(opt_mvlcUSBIndex);
        else if (!opt_mvlcUSBSerial.empty())
            mvlc = make_mvlc_usb(opt_mvlcUSBSerial);
        else
            mvlc = make_mvlc(crateConfig);

        // Cancel any possibly running readout when connecting.
        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            cerr << "Error connecting to MVLC: " << ec.message() << endl;
            return 1;
        }

        cout << "Connected to MVLC " << mvlc.connectionInfo() << endl;

        //
        // init
        //
        {
            CommandExecOptions options = {};
            options.noBatching = true;
            auto initResults = init_readout(mvlc, crateConfig);

            cout << "Results from init_commands:" << endl << initResults.init << endl;
            // cout << "Result from init_trigger_io:" << endl << initResults.triggerIO << endl;

            if (initResults.ec)
            {
                cerr << "Error running readout init sequence: " << initResults.ec.message() << endl;
                return 1;
            }
        }

        // ConnectionType specifics
        eth::MVLC_ETH_Interface *mvlcETH = nullptr;
        usb::MVLC_USB_Interface *mvlcUSB = nullptr;

        switch (mvlc.connectionType())
        {
            case ConnectionType::ETH:
                mvlcETH = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
                mvlcETH->resetPipeAndChannelStats(); // reset packet loss counters
                assert(mvlcETH);

                // Send an initial empty frame to the UDP data pipe port so that
                // the MVLC knows where to send the readout data.
                {
                    static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
                    size_t bytesTransferred = 0;

                    if (auto ec = mvlc.write(
                            Pipe::Data,
                            reinterpret_cast<const u8 *>(EmptyRequest.data()),
                            EmptyRequest.size() * sizeof(u32),
                            bytesTransferred))
                    {
                        throw ec;
                    }
                }
                break;

            case ConnectionType::USB:
                mvlcUSB = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());
                assert(mvlcUSB);
                break;
        }

        assert(mvlcETH || mvlcUSB);

        cout << "data pipe read timeout: " << mvlc.readTimeout(Pipe::Data) << endl;

        // Enable MVLC trigger processing.
        if (auto ec = setup_readout_triggers(mvlc, crateConfig.triggers))
            throw ec;

#if 0
        // stack error polling thread
        std::atomic<bool> quitErrorPoller(false);

        auto pollerThread = std::thread(
            stack_error_notification_poller,
            mvlc,
            std::ref(counters),
            std::ref(quitErrorPoller));
#endif

        ReadoutBuffer destBuffer;
        destBuffer.ensureFreeSpace(usb::USBStreamPipeReadSize);

        std::error_code ec;
        auto tStart = std::chrono::steady_clock::now();
        size_t totalBytesTransferred = 0u;
        size_t totalReads = 0u;
        auto tReadMin = std::chrono::nanoseconds::max();
        auto tReadMax = std::chrono::nanoseconds::min();
        auto tReadTotal = std::chrono::nanoseconds::zero();
        size_t bytesReadMin = std::numeric_limits<size_t>::max();
        size_t bytesReadMax = 0u;

        std::vector<u32> dataPipeReadSizes;
        dataPipeReadSizes.reserve(util::Megabytes(1));
        std::vector<std::chrono::nanoseconds> dataPipeReadDurations;
        dataPipeReadDurations.reserve(util::Megabytes(1));

        while (ec != ErrorType::ConnectionError)
        {
            const auto now = std::chrono::steady_clock::now();

            // check if timeToRun has elapsed
            {
                auto totalElapsed = now - tStart;

                if (timeToRun.count() != 0 && totalElapsed >= timeToRun)
                {
                    std::cout << "MVLC readout timeToRun reached" << std::endl;
                    break;
                }
            }

            if (mvlcETH)
            {
                std::terminate();
            }
            else if (mvlcUSB)
            {
                // XXX: clears the buffer discarding the readout data
                destBuffer.clear();

                //while (destBuffer.free() >= usb::USBStreamPipeReadSize)
                if (destBuffer.free() >= usb::USBStreamPipeReadSize)
                {
                    const size_t bytesToRead = usb::USBStreamPipeReadSize;
                    size_t bytesTransferred = 0u;

                    auto tReadStart = std::chrono::steady_clock::now();

                    //auto dataGuard = mvlc.getLocks().lockData();
                    ec = mvlcUSB->read_unbuffered(
                        Pipe::Data,
                        destBuffer.data() + destBuffer.used(),
                        bytesToRead,
                        bytesTransferred);
                    //dataGuard.unlock();

                    auto tReadEnd = std::chrono::steady_clock::now();
                    auto readElapsed = tReadEnd - tReadStart;

                    {
                        tReadMin = std::min(tReadMin, readElapsed);
                        tReadMax = std::max(tReadMax, readElapsed);
                        tReadTotal += readElapsed;

                        bytesReadMin = std::min(bytesReadMin, bytesTransferred);
                        bytesReadMax = std::max(bytesReadMax, bytesTransferred);
                    }

                    destBuffer.use(bytesTransferred);
                    totalBytesTransferred += bytesTransferred;
                    ++totalReads;

                    dataPipeReadSizes.push_back(bytesTransferred);
                    dataPipeReadDurations.push_back(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(readElapsed));

                    if (ec == ErrorType::ConnectionError)
                    {
                        cerr << "connection error from usb::Impl::read_unbuffered(): "
                            << ec.message() << endl;
                        break;
                    }
                    else if (ec)
                    {
                        cerr << "Warning: usb read returned an error: "
                            << ec.message() << endl;
                    }
                }
            }
        }

        auto tEnd = std::chrono::steady_clock::now();
        auto runDuration = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
        double runSeconds = runDuration.count() / 1000.0;
        double megaBytes = totalBytesTransferred * 1.0 / util::Megabytes(1);
        double mbs = megaBytes / runSeconds;

        cout << "totalReads=" << totalReads << endl;
        cout << "totalBytesTransferred=" << totalBytesTransferred << endl;
        cout << "avg. read size=" << totalBytesTransferred / (totalReads * 1.0) << endl;
        cout << "duration=" << runDuration.count() << " ms" << endl;
        cout << "Ran for " << runSeconds << " seconds, transferred a total of " << megaBytes
            << " MB, resulting data rate: " << mbs << "MB/s"
            << endl;
        cout << endl;
        cout << "tReadMin=" << tReadMin.count() << endl;
        cout << "tReadMax=" << tReadMax.count() << endl;
        cout << "tReadTotal=" << tReadTotal.count() << endl;
        cout << "bytesReadMin=" << bytesReadMin << endl;
        cout << "bytesReadMax=" << bytesReadMax << endl;

        assert(dataPipeReadSizes.size() == dataPipeReadDurations.size());

        std::ofstream sizesLogfile("data_pipe_read_sizes_newlib.txt");
        for (size_t i=0; i<dataPipeReadSizes.size(); i++)
        {
            using Millis = std::chrono::duration<double, std::milli>;
            Millis millisElapsed = std::chrono::duration_cast<Millis>(dataPipeReadDurations[i]);

            sizesLogfile << millisElapsed.count()
                << " " << dataPipeReadSizes[i] << endl;
        }
    }
    catch (const std::runtime_error &e)
    {
        cerr << "mini-daq-manual-readout caught an exception: " << e.what() << endl;
        return 1;
    }
    catch (const std::error_code &ec)
    {
        cerr << "mini-daq-manual-readout caught a std::error_code: " << ec.message() << endl;
        return 1;
    }
    catch (...)
    {
        cerr << "mini-daq-manual-readout caught an unknown exception" << endl;
        return 1;
    }

    return 0;
}
