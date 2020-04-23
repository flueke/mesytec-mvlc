#include "mvlc_readout.h"

#include <iostream>

#include "mvlc_factory.h"
#include "mvlc_dialog_util.h"

using std::cerr;
using std::endl;

namespace mesytec
{
namespace mvlc
{

#if 0
struct ReadoutWorker::Private
{
    Private(MVLC &mvlc_)
        : mvlc(mvlc_)
    {}

    std::atomic<ReadoutWorker::State> state;
    std::atomic<ReadoutWorker::State> desiredState;
    MVLC mvlc;
    std::vector<StackTrigger> stackTriggers;
};

ReadoutWorker::ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers)
    : d(std::make_unique<Private>(mvlc))
{
    d->state = State::Idle;
    d->desiredState = State::Idle;
    d->stackTriggers = stackTriggers;
}

ReadoutWorker::State ReadoutWorker::state() const
{
    return d->state;
}

std::error_code ReadoutWorker::start(const std::chrono::seconds &duration)
{
}

std::error_code ReadoutWorker::stop()
{
}

std::error_code ReadoutWorker::pause()
{
}

std::error_code ReadoutWorker::resume()
{
}
#endif


MVLC MESYTEC_MVLC_EXPORT make_mvlc(const CrateConfig &crateConfig)
{
    switch (crateConfig.connectionType)
    {
        case ConnectionType::USB:
            if (crateConfig.usbIndex >= 0)
                return make_mvlc_usb(crateConfig.usbIndex);

            if (!crateConfig.usbSerial.empty())
                return make_mvlc_usb(crateConfig.usbSerial);

            return make_mvlc_usb();

        case ConnectionType::ETH:
            return make_mvlc_eth(crateConfig.ethHost);
    }

    throw std::runtime_error("unknown CrateConfig::connectionType");
}

ReadoutInitResults MESYTEC_MVLC_EXPORT init_readout(
    MVLC &mvlc, const CrateConfig &crateConfig,
    const CommandExecOptions stackExecOptions)
{
    ReadoutInitResults ret;

    // exec init_commands using all of the stack memory
    {
        std::vector<u32> response;

        ret.ec = execute_stack(
            mvlc, crateConfig.initCommands,
            stacks::StackMemoryWords, stackExecOptions,
            response);

        ret.init = parse_stack_exec_response(
            crateConfig.initCommands, response);

        if (ret.ec)
        {
            cerr << "Error running init_commands: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // upload the readout stacks
    {
        ret.ec = setup_readout_stacks(mvlc, crateConfig.stacks);

        if (ret.ec)
        {
            cerr << "Error uploading readout stacks: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // Run the trigger_io init sequence after the stacks are in place.
    // Uses only the immediate stack reserved stack memory to not overwrite the
    // readout stacks.
    {
        CommandExecOptions options;

        std::vector<u32> response;

        ret.ec = execute_stack(
            mvlc, crateConfig.initTriggerIO,
            stacks::ImmediateStackReservedWords, stackExecOptions,
            response);

        ret.triggerIO = parse_stack_exec_response(
            crateConfig.initTriggerIO, response);

        if (ret.ec)
        {
            cerr << "Error running init_trigger_io: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // Both readout stacks and the trigger io system are setup. Now activate the triggers.
    {
        ret.ec = setup_readout_triggers(mvlc, crateConfig.triggers);

        if (ret.ec)
        {
            cerr << "Error setting up readout triggers: " << ret.ec.message() << endl;
            return ret;
        }
    }

    return ret;
}

} // end namespace mvlc
} // end namespace mesytec
