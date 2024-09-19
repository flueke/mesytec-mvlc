#ifndef F59B6136_6364_4ED8_9813_983E76CD9B25
#define F59B6136_6364_4ED8_9813_983E76CD9B25

#include "mvlc_transaction_interface.h"

namespace mesytec::mvlc
{

class MvlcTransactionLowLatencyImpl final: public MvlcTransactionInterface
{
    public:
        explicit MvlcTransactionLowLatencyImpl(std::unique_ptr<MvlcBasicInterface> &&mvlcImpl);
        ~MvlcTransactionLowLatencyImpl() override;

        MvlcBasicInterface *getImpl() override;
        std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest) override;
        std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest) override;
        u16 nextSuperReference() override;
        u32 nextStackReference() override;
        CmdPipeCounters getCmdPipeCounters() const override;
        StackErrorCounters getStackErrorCounters() const override;
        void resetStackErrorCounters() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* F59B6136_6364_4ED8_9813_983E76CD9B25 */
