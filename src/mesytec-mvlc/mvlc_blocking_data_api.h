#ifndef __MESYTEC_MVLC_MVLC_BLOCKING_DATA_API_H__
#define __MESYTEC_MVLC_MVLC_BLOCKING_DATA_API_H__

#include <condition_variable>
#include <mutex>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_readout.h"
#include "mesytec-mvlc/mvlc_replay.h"

namespace mesytec
{
namespace mvlc
{

struct MESYTEC_MVLC_EXPORT EventContainer
{
    enum class Type
    {
        None,
        Readout,
        System
    };

    struct Readout
    {
        int eventIndex;
        const readout_parser::ModuleData *moduleDataList;
        unsigned moduleCount;
    };

    struct System
    {
        const u32 *header;
        u32 size;
    };

    Type type;
    Readout readout;
    System system;
    u8 crateId = 0;

    inline operator bool() const
    {
        return type != Type::None;
    }
};

// readout

class MESYTEC_MVLC_EXPORT BlockingReadout
{
    public:
        ~BlockingReadout();

        BlockingReadout(BlockingReadout &&);
        BlockingReadout &operator=(BlockingReadout &&);

        BlockingReadout(const BlockingReadout &) = delete;
        BlockingReadout &operator=(const BlockingReadout &) = delete;

        std::error_code start(const std::chrono::seconds &timeToRun = {});

    private:
        BlockingReadout();

        struct Private;
        std::unique_ptr<Private> d;

        friend BlockingReadout make_mvlc_readout_blocking(
            const CrateConfig &crateConfig,
            const ListfileParams &listfileParams);

        friend BlockingReadout make_mvlc_readout_blocking(
            MVLC &mvlc,
            const CrateConfig &crateConfig,
            const ListfileParams &listfileParams);

        friend BlockingReadout make_mvlc_readout_blocking(
            const CrateConfig &crateConfig,
            const std::shared_ptr<listfile::WriteHandle> &listfileWriteHandle);

        friend BlockingReadout make_mvlc_readout_blocking(
            MVLC &mvlc,
            const CrateConfig &crateConfig,
            const std::shared_ptr<listfile::WriteHandle> &listfileWriteHandle);

        friend EventContainer next_event(BlockingReadout &br);
};

BlockingReadout MESYTEC_MVLC_EXPORT make_mvlc_readout_blocking(
    const CrateConfig &crateConfig,
    const ListfileParams &listfileParams);

BlockingReadout MESYTEC_MVLC_EXPORT make_mvlc_readout_blocking(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const ListfileParams &listfileParams);

BlockingReadout MESYTEC_MVLC_EXPORT make_mvlc_readout_blocking(
    const CrateConfig &crateConfig,
    const std::shared_ptr<listfile::WriteHandle> &listfileWriteHandle);

BlockingReadout MESYTEC_MVLC_EXPORT make_mvlc_readout_blocking(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const std::shared_ptr<listfile::WriteHandle> &listfileWriteHandle);

EventContainer MESYTEC_MVLC_EXPORT next_event(BlockingReadout &br);


// replay

class MESYTEC_MVLC_EXPORT BlockingReplay
{
    public:
        ~BlockingReplay();

        BlockingReplay(BlockingReplay &&);
        BlockingReplay &operator=(BlockingReplay &&);

        BlockingReplay(const BlockingReplay &) = delete;
        BlockingReplay &operator=(const BlockingReplay &) = delete;

        std::error_code start();

        const CrateConfig &crateConfig() const;

    private:
        BlockingReplay();

        struct Private;
        std::unique_ptr<Private> d;

        friend BlockingReplay make_mvlc_replay_blocking(
            const std::string &listfileArchiveName);

        friend BlockingReplay make_mvlc_replay_blocking(
            const std::string &listfileArchiveName,
            const std::string &listfileArchiveMemberName);

        friend BlockingReplay make_mvlc_replay_blocking(
            listfile::ReadHandle *lfh);

        friend EventContainer next_event(BlockingReplay &br);
};

BlockingReplay MESYTEC_MVLC_EXPORT make_mvlc_replay_blocking(
    const std::string &listfileArchiveName);

BlockingReplay MESYTEC_MVLC_EXPORT make_mvlc_replay_blocking(
    const std::string &listfileArchiveName,
    const std::string &listfileArchiveMemberName);

BlockingReplay MESYTEC_MVLC_EXPORT make_mvlc_replay_blocking(
    listfile::ReadHandle *lfh);

EventContainer MESYTEC_MVLC_EXPORT next_event(BlockingReplay &br);

}
}

#endif /* __MESYTEC_MVLC_MVLC_BLOCKING_DATA_API_H__ */
