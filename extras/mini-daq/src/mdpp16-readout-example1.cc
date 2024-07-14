#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_eth_interface.h>
#include <mesytec-mvlc/mvlc_usb_interface.h>
#include <mesytec-mvlc/util/perf.h>

using namespace mesytec::mvlc;

static const std::chrono::milliseconds FlushBufferTimeout(500);

std::error_code readout_usb(MVLC &mvlc, ReadoutBuffer *destBuffer, ReadoutBuffer &previousData)
{
    assert(mvlc.connectionType() == ConnectionType::USB);
    auto usbImpl = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());

    if (previousData.used())
    {
        // move bytes from previousData into destBuffer
        destBuffer->ensureFreeSpace(previousData.used());
        std::memcpy(destBuffer->data() + destBuffer->used(),
                    previousData.data(), previousData.used());
        destBuffer->use(previousData.used());
        previousData.clear();
    }

    destBuffer->ensureFreeSpace(usb::USBStreamPipeReadSize);
    auto tStart = std::chrono::steady_clock::now();
    std::error_code ec;

    while (destBuffer->free() >= usb::USBStreamPipeReadSize)
    {
        const size_t bytesToRead = usb::USBStreamPipeReadSize;
        size_t bytesTransferred = 0u;

        auto dataGuard = mvlc.getLocks().lockData();
        ec = usbImpl->read_unbuffered(
            Pipe::Data,
            destBuffer->data() + destBuffer->used(),
            bytesToRead,
            bytesTransferred);
        dataGuard.unlock();

        destBuffer->use(bytesTransferred);

        if (ec == ErrorType::ConnectionError)
        {
            //logger->error("connection error from usb::Impl::read_unbuffered(): {}", ec.message());
            break;
        }

        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= FlushBufferTimeout)
        {
            //logger->trace("flush buffer timeout reached, leaving readout_usb()");
            break;
        }
    }

    // TODO: fixup_usb_buffer(*destBuffer, previousData);

    return ec;
}

std::error_code readout_eth(MVLC &mvlc, ReadoutBuffer *destBuffer)
{
    assert(mvlc.connectionType() == ConnectionType::ETH);
    auto ethImpl = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());

    auto tStart = std::chrono::steady_clock::now();
    std::error_code ec;
    std::array<size_t, stacks::StackCount> stackHits = {};

    {
        auto dataGuard = mvlc.getLocks().lockData();

        while (destBuffer->free() >= eth::JumboFrameMaxSize)
        {
            auto result = ethImpl->read_packet(
                Pipe::Data,
                destBuffer->data() + destBuffer->used(),
                destBuffer->free());

            ec = result.ec;
            destBuffer->use(result.bytesTransferred);

#if 0
            if (this->firstPacketDebugDump)
            {
                cout << "first received readout eth packet:" << endl;
                cout << fmt::format("header0=0x{:08x}", result.header0()) << endl;
                cout << fmt::format("header1=0x{:08x}", result.header1()) << endl;
                cout << "  packetNumber=" << result.packetNumber() << endl;
                cout << "  dataWordCount=" << result.dataWordCount() << endl;
                cout << "  lostPackets=" << result.lostPackets << endl;
                cout << "  nextHeaderPointer=" << result.nextHeaderPointer() << endl;
                this->firstPacketDebugDump = false;
            }
#endif

            if (result.ec == ErrorType::ConnectionError)
                return result.ec;

            // Record stack hits in the local counters array.
            count_stack_hits(result, stackHits);

            // A crude way of handling packets with residual bytes at the end. Just
            // subtract the residue from buffer->used which means the residual
            // bytes will be overwritten by the next packets data. This will at
            // least keep the structure somewhat intact assuming that the
            // dataWordCount in header0 is correct. Note that this case does not
            // happen, the MVLC never generates packets with residual bytes.
            if (unlikely(result.leftoverBytes()))
            {
                //std::cout << "Oi! There's residue here!" << std::endl; // TODO: log a warning instead of using cout
                destBuffer->setUsed(destBuffer->used() - result.leftoverBytes());
            }

            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= FlushBufferTimeout)
                break;
        }
    } // with dataGuard

    // Copy the ethernet pipe stats and the stack hits into the Counters
    // structure. The getPipeStats() access is thread-safe in the eth
    // implementation.
#if 0
    {
        auto c = counters.access();

        c->ethStats = mvlcETH->getPipeStats();

        for (size_t stack=0; stack<stackHits.size(); ++stack)
            c->stackHits[stack] += stackHits[stack];
    }
#endif

    return ec;
}

class ReadoutHelper
{
    public:
        ReadoutHelper(MVLC &mvlc)
            : mvlc_(mvlc)
            , outputBuffer_(util::Megabytes(1))
            , tempBuffer_(util::Megabytes(1))
        { }

        std::error_code readout()
        {
            outputBuffer_.clear();
            outputBuffer_.setBufferNumber(nextOutputBufferNumber++);
            outputBuffer_.setType(mvlc_.connectionType());

            switch (mvlc_.connectionType())
            {
                case ConnectionType::ETH:
                    return readout_eth(mvlc_, &outputBuffer_);
                case ConnectionType::USB:
                    return readout_usb(mvlc_, &outputBuffer_, tempBuffer_);
            }

            return {};
        }

        const ReadoutBuffer &outputBuffer() const { return outputBuffer_; }

    private:
        MVLC mvlc_;

        // Destination buffer to be filled with USB frames or UDP packets.
        ReadoutBuffer outputBuffer_;

        // Temporary storage for incomplete frames read from USB.
        ReadoutBuffer tempBuffer_;

        size_t nextOutputBufferNumber = 1u;
        Protected<ReadoutWorker::Counters> counters_;
};

void handle_event_data(void *userContext, int crateIndex, int eventIndex,
                       const ModuleData *moduleDataList, unsigned moduleCount)
{
    spdlog::info("handle_event_data: {} {}", fmt::ptr(moduleDataList), moduleCount);
    for (unsigned mi=0; mi<moduleCount; ++mi)
    {
        util::log_buffer(std::cout, moduleDataList[mi].data.data, moduleDataList[mi].data.size);
    }
}

void handle_system_event(void *userContext, int crateIndex, const u32 *header, u32 size)
{
    spdlog::info("handle_system_event: {} {}", fmt::ptr(header), size);
}

int main()
{
    spdlog::set_level(spdlog::level::debug);

    const u32 modBase = 0x02100000;

    // trigger setup for IRQ1
    stacks::Trigger trigger{};
    trigger.type = stacks::TriggerType::IRQNoIACK;
    trigger.subtype = stacks::TriggerSubtype::IRQ1;

    // mdpp-16 init values
    const u8 irqLevel = 1;
    const u16 pulserValue = 1;
    std::error_code ec;

    auto mvlc = make_mvlc_usb();

    ec = mvlc.connect();
    assert(!ec);

    // Module initialization using direct VME commands
    ec = mvlc.vmeWrite(modBase + 0x6008, 1, 0x09, VMEDataWidth::D16); // module reset
    assert(!ec);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // wait for the reset to complete
    ec = mvlc.vmeWrite(modBase + 0x6010, irqLevel, 0x09, VMEDataWidth::D16); // module signals IRQ1
    assert(!ec);
    ec = mvlc.vmeWrite(modBase + 0x6036, 0, 0x09, VMEDataWidth::D16); // single event, no module buffering
    assert(!ec);
    ec = mvlc.vmeWrite(modBase + 0x6070, pulserValue, 0x09, VMEDataWidth::D16); // enable the test pulser
    assert(!ec);

    // Prepare the readout command stack
    StackCommandBuilder readoutCommands;
    readoutCommands.addVMEBlockRead(modBase, 0x08, 65535); // MBLT module readout until BERR
    readoutCommands.addVMEWrite(modBase + 0x6034, 1, 0x09, VMEDataWidth::D16); // readout reset

    // Upload and setup the stack
    const u8 stackId = 1;

    ec = setup_readout_stack(mvlc, readoutCommands, stackId, trigger);
    assert(!ec);

    // Create a readout_parser
    auto parser = readout_parser::make_readout_parser({ readoutCommands });
    readout_parser::ReadoutParserCounters parserCounters;
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        handle_event_data,
        handle_system_event
    };

    // ConnectionType independent readout helper instance.
    ReadoutHelper rdoHelper(mvlc);

    // Enter DAQ mode. This will enable trigger processing.
    ec = enable_daq_mode(mvlc);
    assert(!ec);

    auto timeToRun = std::chrono::seconds(10);
    auto tStart = std::chrono::steady_clock::now();

    while (true)
    {
        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= timeToRun)
            break;

        ec = rdoHelper.readout();
        if (ec)
            spdlog::warn("errro_code from readout(): {}", ec.message());

        const auto &outputBuffer = rdoHelper.outputBuffer();

        if (outputBuffer.used())
        {
            spdlog::info("Got {} bytes of readout data", outputBuffer.used());
            readout_parser::parse_readout_buffer(
                outputBuffer.type(),
                parser,
                parserCallbacks,
                parserCounters,
                outputBuffer.bufferNumber(),
                outputBuffer.viewU32().data(),
                outputBuffer.viewU32().size());

        }
    }

    ec = disable_daq_mode(mvlc);
}
