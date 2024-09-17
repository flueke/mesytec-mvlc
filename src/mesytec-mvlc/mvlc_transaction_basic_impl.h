#ifndef B98531A7_898E_4810_BCBF_BEE8A6E7C8A8
#define B98531A7_898E_4810_BCBF_BEE8A6E7C8A8

#include "mvlc_transaction_interface.h"

namespace mesytec::mvlc
{

class MvlcTransactionBasicImpl: public MvlcTransactionInterface
{
    public:
        MvlcTransactionBasicImpl(MvlcBasicInterface *impl = nullptr)
            : impl_(impl)
        {}

        void setImpl(MvlcBasicInterface *impl) { impl_ = impl; }
        MvlcBasicInterface *getImpl() override { return impl_; }
        std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest) override;
        std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest) override;

    private:
        MvlcBasicInterface *impl_;
};

}

#endif /* B98531A7_898E_4810_BCBF_BEE8A6E7C8A8 */
