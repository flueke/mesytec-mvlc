#include <chrono>
#include <deque>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_apiv2.h>
#include <lyra/lyra.hpp>
#include <future>
#include <list>
#include <ratio>
#include <spdlog/spdlog.h>
#ifndef __WIN32
#include <sys/prctl.h>
#endif

using std::cout;
using std::endl;
using namespace mesytec::mvlc;

#if 1

// test some super commands
void super_rw_test(apiv2::MVLC &mvlc, std::atomic<size_t> &superTransactions, std::atomic<bool> &quit)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"super_rw_test",0,0,0);
#endif

    while (!quit)
    {

        if (auto ec = mvlc.writeRegister(stacks::StackMemoryBegin, 0x87654321u))
        {
            spdlog::warn("writeRegister, ec={}", ec.message());
            throw ec;
        }

        ++superTransactions;

        u32 value = 0;

        if (auto ec = mvlc.readRegister(stacks::StackMemoryBegin, value))
        {
            spdlog::warn("readRegister, ec={}", ec.message());
            throw ec;
        }

        if (value != 0x87654321u)
            spdlog::warn("writeRegister/readRegister, value=0x{0:x}", value);

        assert(value == 0x87654321u);

        ++superTransactions;

        spdlog::trace("writeRegister/readRegister, value=0x{0:x}", value);
    }
}

// Test a vme read
void vmeread_test(apiv2::MVLC &mvlc, std::atomic<size_t> &stackTransactions, std::atomic<bool> &quit)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"vmeread_test",0,0,0);
#endif

    while (!quit)
    {
        u32 value = 0;
        if (auto ec = mvlc.vmeRead(0x00006008, value, vme_amods::A32, VMEDataWidth::D16))
        {
            spdlog::warn("vmeRead, ec={}", ec.message());
            if (ec != ErrorType::VMEError)
                throw ec;
        }

        spdlog::trace("vmeRead, value=0x{0:x}", value);

        ++stackTransactions;
    }
}

// Test a vme write
void vmewrite_test(apiv2::MVLC &mvlc, std::atomic<size_t> &stackTransactions, std::atomic<bool> &quit)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"vmewrite_test",0,0,0);
#endif

    while (!quit)
    {
        if (auto ec = mvlc.vmeWrite(0x00006008, 1, vme_amods::A32, VMEDataWidth::D16))
        {
            spdlog::warn("vmeWrite, ec={}", ec.message());
            if (ec != ErrorType::VMEError)
                throw ec;
        }

        ++stackTransactions;
    }
}
#endif

#if 1
int main(int argc, char *argv[])
{
    //{
    //    auto logger = apiv2::setup_logger();
    //    spdlog::register_logger(logger);
    //}

    //SPDLOG_TRACE("SPDLOG_TRACE");
    //SPDLOG_DEBUG("SPDLOG_INFO");
    //SPDLOG_INFO("SPDLOG_INFO");

    std::string host;
    bool showHelp = false;
    bool logDebug = false;
    bool logTrace = false;
    unsigned secondsToRun = 2;

    auto cli
        = lyra::help(showHelp)
        | lyra::opt(host, "hostname")["--eth"]("mvlc hostname")
        | lyra::opt(logDebug)["--debug"]("enable debug logging")
        | lyra::opt(logTrace)["--trace"]("enable trace logging")
        | lyra::arg(secondsToRun, "secondsToRun")
        ;
    auto parseResult = cli.parse({ argc, argv});

    if (!parseResult)
        return 1;

    if (showHelp)
    {
        cout << cli << "\n";
        return 0;
    }

    if (logDebug)
        spdlog::set_level(spdlog::level::debug);

    if (logTrace)
        spdlog::set_level(spdlog::level::trace);

    std::unique_ptr<MVLCBasicInterface> impl;

    if (host.empty())
    {
        impl = std::make_unique<usb::Impl>();
    }
    else
    {
        impl = std::make_unique<eth::Impl>(host);
    }

    assert(impl);

    apiv2::MVLC mvlc(std::move(impl));


    // FIXME: disabling triggers via the old dialog does not work because the
    // pipes read timeout is now set to 0. This causes the dialog code to
    // return a timeout error immediately. The responses will still be
    // available on the next reads which will be done by the cmd_pipe_reader.
    // It would be good if the reader skipped over the ref mismatches buffers
    // without immediately fullfilling the pending response. Instead the caller
    // could use a timed wait for the future and cancel with a timeout.
    mvlc.setDisableTriggersOnConnect(true);

    try
    {
        if (auto ec = mvlc.connect())
            throw ec;

        spdlog::info("connected to mvlc");


        std::atomic<size_t> superTransactions(0);
        std::atomic<size_t> stackTransactions(0);

        auto tStart = std::chrono::steady_clock::now();

static constexpr int SECONDS_TO_RUN = 4;
static constexpr int PARALLEL_SUPER_TESTS = 2;
static constexpr int PARALLEL_VMEREAD_TESTS = 2;
static constexpr int PARALLEL_VMEWRITE_TESTS = 2;

        std::vector<std::thread> testThreads;
        std::atomic<bool> quitTests(false);

        for (auto i=0; i < PARALLEL_SUPER_TESTS; ++i)
        {
            testThreads.emplace_back(std::thread(
                    super_rw_test, std::ref(mvlc),
                    std::ref(superTransactions), std::ref(quitTests)));
        }

        for (auto i=0; i < PARALLEL_VMEREAD_TESTS; ++i)
        {
            testThreads.emplace_back(std::thread(
                    vmeread_test, std::ref(mvlc),
                    std::ref(stackTransactions), std::ref(quitTests)));
        }

        for (auto i=0; i < PARALLEL_VMEWRITE_TESTS; ++i)
        {
            testThreads.emplace_back(std::thread(
                    vmewrite_test, std::ref(mvlc),
                    std::ref(stackTransactions), std::ref(quitTests)));
        }

        u16 address = 0x4400;
        u32 value = 0;

        if (auto ec = mvlc.readRegister(address, value))
            throw ec;

        //
        // DSO
        //

        auto self_write_throw = [] (apiv2::MVLC &mvlc, u32 address, u16 value)
        {
            if (auto ec = mvlc.vmeWrite(
                    SelfVMEAddress + address, value,
                    vme_amods::A32, VMEDataWidth::D16))
                throw ec;
        };

        static const unsigned UnitNumber = 48;
        self_write_throw(mvlc, 0x0200, UnitNumber); // select DSO unit
        self_write_throw(mvlc, 0x0300, 1000); // pre trigger time
        self_write_throw(mvlc, 0x0302, 1000); // post trigger time
        self_write_throw(mvlc, 0x0304, 0xffff); // nim triggers
        self_write_throw(mvlc, 0x0308, 0xffff); // irq triggers
        self_write_throw(mvlc, 0x030A, 0xffff); // util triggers
        self_write_throw(mvlc, 0x0306, 1); // start capturing

        spdlog::info("own_ip_low={}", value);

        while (true)
        {
            auto tElapsed = std::chrono::steady_clock::now() - tStart;

            if (tElapsed >= std::chrono::seconds(SECONDS_TO_RUN))
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        quitTests = true;

        for (auto &t: testThreads)
            t.join();

        spdlog::info("all test threads joined");

        auto tElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tStart);

        // quit --------------------------
        if (auto ec = mvlc.disconnect())
            throw ec;

        // --------------------------------

        auto superRate = 1000.0 * superTransactions / tElapsed.count();
        auto stackRate = 1000.0 * stackTransactions / tElapsed.count();

        spdlog::info("loop done, elapsed={}ms, superTransactions={}, superRate={}",
                     tElapsed.count(), superTransactions, superRate);

        spdlog::info("loop done, elapsed={}ms, stackTransactions={}, stackRate={}",
                     tElapsed.count(), stackTransactions, stackRate);

        auto counters = mvlc.getCmdPipeCounters();

        spdlog::info("total reads={}, read timeouts={}, timeouts/reads={}",
                     counters.reads,
                     counters.timeouts,
                     (float)counters.timeouts / counters.reads
                    );

        spdlog::info("reader buffer counts: supers={}, stacks={}, dso={}, errors={}",
                     counters.superBuffers,
                     counters.stackBuffers,
                     counters.dsoBuffers,
                     counters.errorBuffers);

        spdlog::info("reader error counts: invalidHeaders={}, superFormatErrors={}"
                     ", superRefMismatches={}, stackRefMismatches={}, shortSuperBuffers={}",
                     counters.invalidHeaders,
                     counters.superFormatErrors,
                     counters.superRefMismatches,
                     counters.stackRefMismatches,
                     counters.shortSuperBuffers
                     );
    }
    catch (const std::error_code &ec)
    {
        cout << "Error: " << ec.message() << "\n";
        throw;
    }

        spdlog::info("end of main()");
}
#endif
