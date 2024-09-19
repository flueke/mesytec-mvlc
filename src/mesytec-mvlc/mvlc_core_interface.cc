#include "mvlc_core_interface.h"
#include "mvlc_command_builders.h"

namespace mesytec::mvlc
{

MvlcCoreInterface::~MvlcCoreInterface() { }

std::error_code MvlcCoreInterface::uploadStack(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &commands)
{
    return uploadStack(stackOutputPipe, stackMemoryOffset, make_stack_buffer(commands));
}

std::error_code MvlcCoreInterface::uploadStack(
    u8 stackOutputPipe, u16 stackMemoryOffset, const StackCommandBuilder &stack)
{
    return uploadStack(stackOutputPipe, stackMemoryOffset, stack.getCommands());
}

}
