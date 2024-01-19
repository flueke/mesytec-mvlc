#ifndef __MESYTEC_MVLC_LISTFILE_ZMQ_H__
#define __MESYTEC_MVLC_LISTFILE_ZMQ_H__

#include <memory>
#include <string>
#include "mesytec-mvlc/mvlc_listfile.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

class MESYTEC_MVLC_EXPORT ZmqGanilWriteHandle: public WriteHandle
{
    public:
        ZmqGanilWriteHandle(const std::string &zmqBindUrl = "tcp://*:5575");
        ~ZmqGanilWriteHandle() override;
        size_t write(const u8 *data, size_t size) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec


#endif /* __MESYTEC_MVLC_LISTFILE_ZMQ_H__ */
