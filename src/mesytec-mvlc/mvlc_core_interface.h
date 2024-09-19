#ifndef MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H
#define MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H

#include <system_error>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"

#include "mvlc_basic_interface.h"
#include "mvlc_constants.h"
#include "mvlc_transaction_interface.h"

namespace mesytec::mvlc
{

struct StackCommand;
class StackCommandBuilder;
class SuperCommandBuilder;

class MvlcCoreInterface: public MvlcTransactionInterface
{
public:
    virtual ~MvlcCoreInterface();

    // MvlcCoreInterface implementations should be thread-safe.
    virtual Locks &getLocks() = 0;
    virtual MvlcTransactionInterface *getTransactionImpl() = 0;
    MvlcBasicInterface *getImpl() override { return getTransactionImpl()->getImpl(); }
    u16 nextSuperReference() override { return getTransactionImpl()->nextSuperReference(); }
    u32 nextStackReference() override { return getTransactionImpl()->nextStackReference(); }

    // register / internal memory access
    virtual std::error_code readRegister(u16 address, u32 &value) = 0;
    virtual std::error_code writeRegister(u16 address, u32 value) = 0;
    virtual std::error_code uploadStack(u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents) = 0;

    virtual std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth) = 0;
    virtual std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth) = 0;

    // BLT, MBLT
    virtual std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true) = 0;

    // 2eSST
    virtual std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true) = 0;

    // Swaps the two 32-bit words for 64-bit reads. amod must be one of the MBLT amods!
    virtual std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers,
        std::vector<u32> &dest, bool fifo = true) = 0;

    virtual std::error_code vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers,
        std::vector<u32> &dest, bool fifo = true) = 0;

    // Part of MvlcBasicInterface
    virtual std::error_code connect() = 0;
    virtual std::error_code disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual ConnectionType connectionType() const = 0;
    virtual std::string connectionInfo() const = 0;

    // MvlcTransactionInterface
    std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest) override
    {
        return getTransactionImpl()->superTransaction(superBuilder, dest);
    }

    std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest) override
    {
        return getTransactionImpl()->stackTransaction(stackBuilder, dest);
    }

    // overloads for stack uploading
    std::error_code uploadStack(
        u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &commands);

    std::error_code uploadStack(
        u8 stackOutputPipe, u16 stackMemoryOffset, const StackCommandBuilder &stack);
};

}

#endif // MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H
