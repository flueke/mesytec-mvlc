#include "mvlc_c.h"

#include <cassert>
#include <exception>
#include <memory>
#include <system_error>

#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

namespace
{
    mvlc_err_t make_mvlc_error(const std::error_code &ec)
    {
        mvlc_err_t res = { ec.value(), reinterpret_cast<const void *>(&ec.category()) };
        return res;
    }
}

bool mvlc_is_error(const mvlc_err_t err)
{
    return err.ec != 0;
}

char *mvlc_format_error(const mvlc_err_t err, char *buf, size_t bufsize)
{
    auto ec = std::error_code(err.ec, *reinterpret_cast<const std::error_category *>(err.cat));
    snprintf(buf, bufsize, "%s", ec.message().c_str());
    return buf;
}

struct mvlc_ctrl
{
    MVLC instance;
};

mvlc_ctrl_t *mvlc_ctrl_create_usb(void)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = make_mvlc_usb();
    return ret.release();
}

mvlc_ctrl_t *mvlc_ctrl_create_usb_index(unsigned index)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = make_mvlc_usb(index);
    return ret.release();
}

mvlc_ctrl_t *mvlc_ctrl_create_usb_serial(const char *serial)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = make_mvlc_usb(serial);
    return ret.release();
}

mvlc_ctrl_t *mvlc_ctrl_create_eth(const char *host)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = make_mvlc_eth(host);
    return ret.release();
}

void mvlc_ctrl_destroy(mvlc_ctrl_t *mvlc)
{
    delete mvlc;
}

mvlc_ctrl_t *mvlc_ctrl_copy(mvlc_ctrl_t *src)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = src->instance;
    return ret.release();
}

mvlc_err_t mvlc_ctrl_connect(mvlc_ctrl_t *mvlc)
{
    auto ec = mvlc->instance.connect();
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_disconnect(mvlc_ctrl_t *mvlc)
{
    auto ec = mvlc->instance.disconnect();
    return make_mvlc_error(ec);
}

bool mvlc_ctrl_is_connected(mvlc_ctrl_t *mvlc)
{
    return mvlc->instance.isConnected();
}

void mvlc_ctrl_set_disable_trigger_on_connect(mvlc_ctrl_t *mvlc, bool disableTriggers)
{
    mvlc->instance.setDisableTriggersOnConnect(disableTriggers);
}

MVLC_ConnectionType get_mvlc_ctrl_connection_type(const mvlc_ctrl_t *mvlc)
{
    auto ct = mvlc->instance.connectionType();
    switch (ct)
    {
        case ConnectionType::USB:
            return MVLC_ConnectionType_USB;
        case ConnectionType::ETH:
            return MVLC_ConnectionType_ETH;
    }

    return MVLC_ConnectionType_USB;
}

u32 get_mvlc_ctrl_hardware_id(mvlc_ctrl_t *mvlc)
{
    return mvlc->instance.hardwareId();
}

u32 get_mvlc_ctrl_firmware_revision(mvlc_ctrl_t *mvlc)
{
    return mvlc->instance.firmwareRevision();
}

char *get_mvlc_ctrl_connection_info(mvlc_ctrl_t *mvlc)
{
    return strdup(mvlc->instance.connectionInfo().c_str());
}

mvlc_err_t mvlc_ctrl_read_register(mvlc_ctrl_t *mvlc, u16 address, u32 *value)
{
    assert(value);
    auto ec = mvlc->instance.readRegister(address, *value);
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_write_register(mvlc_ctrl_t *mvlc, u16 address, u32 value)
{
    assert(value);
    auto ec = mvlc->instance.writeRegister(address, value);
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_vme_read(mvlc_ctrl_t *mvlc, u32 address, u32 *value, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    auto ec = mvlc->instance.vmeRead(address, *value, amod, static_cast<mesytec::mvlc::VMEDataWidth>(dataWidth));
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_vme_write(mvlc_ctrl_t *mvlc, u32 address, u32 value, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    auto ec = mvlc->instance.vmeWrite(address, value, amod, static_cast<mesytec::mvlc::VMEDataWidth>(dataWidth));
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_vme_block_read_alloc(mvlc_ctrl_t *mvlc, u32 address, u8 amod, u16 maxTransfers,
                                          u32 **buf, size_t *bufsize)
{
    assert(buf);
    assert(bufsize);

    std::vector<u32> dest;
    auto ec = mvlc->instance.vmeBlockRead(address, amod, maxTransfers, dest);

    *buf = reinterpret_cast<u32 *>(malloc(dest.size() * sizeof(u32)));
    *bufsize = dest.size();
    std::copy(dest.begin(), dest.end(), *buf);

    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_vme_block_read_buffer(mvlc_ctrl_t *mvlc, u32 address, u8 amod, u16 maxTransfers,
                                           u32 *buf, size_t *bufsize)
{
    assert(buf);
    assert(bufsize);

    std::vector<u32> dest;
    auto ec = mvlc->instance.vmeBlockRead(address, amod, maxTransfers, dest);

    size_t toCopy = std::min(dest.size(), *bufsize);
    std::copy_n(dest.begin(), toCopy, buf);
    *bufsize = toCopy;
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_vme_mblt_swapped_alloc(mvlc_ctrl_t *mvlc, u32 address, u16 maxTransfers,
                                            u32 **buf, size_t *bufsize)
{
    assert(buf);
    assert(bufsize);

    std::vector<u32> dest;
    auto ec = mvlc->instance.vmeMBLTSwapped(address, maxTransfers, dest);

    *buf = reinterpret_cast<u32 *>(malloc(dest.size() * sizeof(u32)));
    *bufsize = dest.size();
    std::copy(dest.begin(), dest.end(), *buf);

    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_vme_mblt_swapped_buffer(mvlc_ctrl_t *mvlc, u32 address, u16 maxTransfers,
                                             u32 *buf, size_t *bufsize)
{
    assert(buf);
    assert(bufsize);

    std::vector<u32> dest;
    auto ec = mvlc->instance.vmeMBLTSwapped(address, maxTransfers, dest);

    size_t toCopy = std::min(dest.size(), *bufsize);
    std::copy_n(dest.begin(), toCopy, buf);
    *bufsize = toCopy;
    return make_mvlc_error(ec);
}

struct mvlc_stackbuilder
{
    StackCommandBuilder sb;
};

mvlc_stackbuilder_t *mvlc_stackbuilder_create(const char *name)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = StackCommandBuilder(name);
    return ret.release();
}

void mvlc_stackbuilder_destroy(mvlc_stackbuilder_t *sb)
{
    delete sb;
}

mvlc_stackbuilder_t *mvlc_stackbuilder_copy(const mvlc_stackbuilder_t *sb)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = sb->sb;
    return ret.release();
}

bool mvlc_stackbuilder_equals(const mvlc_stackbuilder_t *sba, const mvlc_stackbuilder_t *sbb)
{
    return sba->sb == sbb->sb;
}

const char *mvlc_stackbuilder_get_name(const mvlc_stackbuilder_t *sb)
{
    return strdup(sb->sb.getName().c_str());
}

bool mvlc_stackbuilder_is_empty(const mvlc_stackbuilder_t *sb)
{
    return sb->sb.empty();
}

mvlc_stackbuilder_t *mvlc_stackbuilder_add_vme_read(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    sb->sb.addVMERead(address, amod, static_cast<VMEDataWidth>(dataWidth));
    return sb;
}

mvlc_stackbuilder_t *mvlc_stackbuilder_add_vme_block_read(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, u16 maxTransfers)
{
    sb->sb.addVMEBlockRead(address, amod, maxTransfers);
    return sb;
}

mvlc_stackbuilder_t *mvlc_stackbuilder_add_vme_mblt_swapped(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, u16 maxTransfers)
{
    sb->sb.addVMEMBLTSwapped(address, amod, maxTransfers);
    return sb;
}

mvlc_stackbuilder_t *mvlc_stackbuilder_add_vme_write(
    mvlc_stackbuilder_t *sb, u32 address, u32 value, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    sb->sb.addVMEWrite(address, value, amod, static_cast<VMEDataWidth>(dataWidth));
    return sb;
}

mvlc_stackbuilder_t *mvlc_stackbuilder_add_write_marker(
    mvlc_stackbuilder_t *sb, u32 value)
{
    sb->sb.addWriteMarker(value);
    return sb;
}

mvlc_stackbuilder_t *mvlc_stackbuilder_begin_group(
    mvlc_stackbuilder_t *sb, const char *name)
{
    sb->sb.beginGroup(name);
    return sb;
}

bool mvlc_stackbuilder_has_open_group(
    const mvlc_stackbuilder_t *sb)
{
    return sb->sb.hasOpenGroup();
}

size_t mvlc_stackbuilder_get_group_count(
    const mvlc_stackbuilder_t *sb)
{
    return sb->sb.getGroupCount();
}

// Note: uses strdup() interally so you have to free() the returned string after use.
const char *mvlc_stackbuilder_get_group_name(
    const mvlc_stackbuilder_t *sb,
    size_t groupIndex)
{
    if (groupIndex >= sb->sb.getGroupCount())
        return nullptr;

    return strdup(sb->sb.getGroup(groupIndex).name.c_str());
}

struct mvlc_crateconfig
{
    CrateConfig cfg;
};

mvlc_crateconfig_t *mvlc_read_crateconfig_from_file(const char *filename)
{
    std::ifstream in(filename);

    if (!in.is_open())
        return nullptr;

    auto ret = std::make_unique<mvlc_crateconfig_t>();
    ret->cfg = crate_config_from_yaml(in);
    return ret.release();
}

mvlc_crateconfig_t *mvlc_read_crateconfig_from_string(const char *str)
{
    std::string in(str);

    auto ret = std::make_unique<mvlc_crateconfig_t>();
    ret->cfg = crate_config_from_yaml(in);
    return ret.release();
}

void mvlc_crateconfig_destroy(mvlc_crateconfig_t *cfg)
{
    delete cfg;
}

const char *mvlc_crateconfig_to_string(const mvlc_crateconfig_t *cfg)
{
    return strdup(to_yaml(cfg->cfg).c_str());
}

mvlc_err_t mvlc_write_crateconfig_to_file(const mvlc_crateconfig_t *cfg, const char *filename)
{
    std::ofstream of;

    of.open(filename, std::ios_base::out);

    if (of.fail())
        return mvlc_err_t { errno, &std::system_category() };

    of << to_yaml(cfg->cfg);

    if (of.fail())
        return mvlc_err_t { errno, &std::system_category() };

    return {};
}

bool mvlc_crateconfig_equals(mvlc_crateconfig_t *ca, mvlc_crateconfig_t *cb)
{
    return ca->cfg == cb->cfg;
}

MVLC_ConnectionType mvlc_crateconfig_get_connection_type(const mvlc_crateconfig_t *cfg)
{
    return static_cast<MVLC_ConnectionType>(cfg->cfg.connectionType);
}

mvlc_stacktriggers_t mvlc_crateconfig_get_stack_triggers(const mvlc_crateconfig_t *cfg)
{
    mvlc_stacktriggers_t ret = {};

    std::copy_n(
        cfg->cfg.triggers.begin(),
        std::min(cfg->cfg.triggers.size(), static_cast<size_t>(MVLC_ReadoutStackCount)),
        ret.triggerValues);

    return ret;
}

mvlc_stackbuilder_t *mvlc_crateconfig_get_readout_stack(
    mvlc_crateconfig_t *cfg, unsigned stackIndex)
{
    if (stackIndex < cfg->cfg.stacks.size())
    {
        auto ret = std::make_unique<mvlc_stackbuilder_t>();
        ret->sb = cfg->cfg.stacks[stackIndex];
        return ret.release();
    }
    return nullptr;
}

mvlc_crateconfig_t *mvlc_crateconfig_set_readout_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack, unsigned stackIndex)
{
    if (stackIndex >= cfg->cfg.stacks.size())
        cfg->cfg.stacks.resize(stackIndex+1);
    cfg->cfg.stacks[stackIndex] = stack->sb;
    return cfg;
}

mvlc_stackbuilder_t *mvlc_crateconfig_get_trigger_io_stack(
    mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = cfg->cfg.initTriggerIO;
    return ret.release();
}
mvlc_crateconfig_t *mvlc_crateconfig_set_trigger_io_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack)
{
    cfg->cfg.initTriggerIO = stack->sb;
    return cfg;
}

mvlc_stackbuilder_t *mvlc_crateconfig_get_vme_init_stack(
    mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = cfg->cfg.initCommands;
    return ret.release();
}
mvlc_crateconfig_t *mvlc_crateconfig_set_vme_init_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack)
{
    cfg->cfg.initCommands = stack->sb;
    return cfg;
}

mvlc_stackbuilder_t *mvlc_crateconfig_get_vme_stop_stack(
    mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = cfg->cfg.stopCommands;
    return ret.release();
}
mvlc_crateconfig_t *mvlc_crateconfig_set_vme_stop_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack)
{
    cfg->cfg.stopCommands = stack->sb;
    return cfg;
}

mvlc_stackbuilder_t *mvlc_crateconfig_get_mcst_daq_start_stack(
    mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = cfg->cfg.mcstDaqStart;
    return ret.release();
}
mvlc_crateconfig_t *mvlc_crateconfig_set_mcst_daq_start_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack)
{
    cfg->cfg.mcstDaqStart = stack->sb;
    return cfg;
}

mvlc_stackbuilder_t *mvlc_crateconfig_get_mcst_daq_stop_stack(
    mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = cfg->cfg.mcstDaqStop;
    return ret.release();
}
mvlc_crateconfig_t *mvlc_crateconfig_set_mcst_daq_stop_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack)
{
    cfg->cfg.mcstDaqStop = stack->sb;
    return cfg;
}

mvlc_ctrl_t *mvlc_ctrl_create_from_crateconfig(mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = make_mvlc(cfg->cfg);
    return ret.release();
}

// Wraps the C-layer mvlc_listfile_write_handle function in the required C++
// WriteHandle subclass. Negative return values from the C-layer (indicating a write error) are
// thrown as std::runtime_errors.
struct ListfileWriteHandleWrapper: public listfile::WriteHandle
{
    ListfileWriteHandleWrapper(mvlc_listfile_write_handle listfile_handle)
        : listfile_handle(listfile_handle)
    {
    }

    size_t write(const u8 *data, size_t size) override
    {
        ssize_t res = listfile_handle(data, size);
        if (res < 0)
            throw std::runtime_error("wrapped listfile write failed: " + std::to_string(res));
        return res;
    }

    mvlc_listfile_write_handle listfile_handle;
};

mvlc_listfile_params_t make_default_listfile_params()
{
    mvlc_listfile_params_t ret;
    ret.writeListfile = true;
    ret.filepath = "./run_001.zip";
    ret.listfilename = "listfile";
    ret.overwrite = false;
    ret.compression = ListfileCompression_LZ4;
    ret.compressionLevel = 0;
    return ret;
}

struct mvlc_readout
{
    std::unique_ptr<MVLCReadout> rdo;
    std::unique_ptr<ListfileWriteHandleWrapper> lfWrap;
};

namespace
{
    // Creates C++ ReadoutParserCallbacks which internally call the C-style
    // parser_callbacks functions..
    readout_parser::ReadoutParserCallbacks wrap_parser_callbacks(
        readout_parser_callbacks_t parser_callbacks)
    {
        readout_parser::ReadoutParserCallbacks parserCallbacks;

        parserCallbacks.eventData = [parser_callbacks] (
            int eventIndex,
            const readout_parser::ModuleData *modulesDataCpp,
            unsigned moduleCount)
        {
            if (parser_callbacks.event_data)
            {
                static const size_t MaxVMEModulesPerEvent = 20;
                std::array<readout_moduledata, MaxVMEModulesPerEvent> modulesDataC;

                assert(moduleCount <= modulesDataC.size());

                for (size_t i=0; i<moduleCount; ++i)
                {
                    modulesDataC[i].prefix  = { modulesDataCpp[i].prefix.data,  modulesDataCpp[i].prefix.size };
                    modulesDataC[i].dynamic = { modulesDataCpp[i].dynamic.data, modulesDataCpp[i].dynamic.size };
                    modulesDataC[i].suffix  = { modulesDataCpp[i].suffix.data,  modulesDataCpp[i].suffix.size };
                }

                parser_callbacks.event_data(eventIndex, modulesDataC.begin(), moduleCount);
            }
        };

        parserCallbacks.systemEvent = [parser_callbacks] (const u32 *header, u32 size)
        {
            if (parser_callbacks.system_event)
                parser_callbacks.system_event(header, size);
        };

        return parserCallbacks;
    }
}

mvlc_readout_t *mvlc_readout_create(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_write_handle listfile_handle,
    readout_parser_callbacks_t parser_callbacks)
{
    auto lfWrap = std::make_unique<ListfileWriteHandleWrapper>(listfile_handle);

    auto rdo = make_mvlc_readout(
        mvlc->instance,
        crateconfig->cfg,
        lfWrap.get(),
        wrap_parser_callbacks(parser_callbacks));

    auto ret = std::make_unique<mvlc_readout>();
    ret->rdo = std::make_unique<MVLCReadout>(std::move(rdo));
    ret->lfWrap = std::move(lfWrap);

    return ret.release();
}

mvlc_readout_t *mvlc_readout_create2(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_params_t lfParamsC,
    readout_parser_callbacks_t parser_callbacks)
{
    ListfileParams lfParamsCpp;
    lfParamsCpp.writeListfile = lfParamsC.writeListfile;
    lfParamsCpp.filepath = lfParamsC.filepath;
    lfParamsCpp.listfilename = lfParamsC.listfilename;
    lfParamsCpp.overwrite = lfParamsC.overwrite;
    lfParamsCpp.compression = static_cast<ListfileParams::Compression>(lfParamsC.compression);
    lfParamsCpp.compressionLevel = lfParamsC.compressionLevel;

    auto rdo = make_mvlc_readout(
        mvlc->instance,
        crateconfig->cfg,
        lfParamsCpp,
        wrap_parser_callbacks(parser_callbacks));

    auto ret = std::make_unique<mvlc_readout>();
    ret->rdo = std::make_unique<MVLCReadout>(std::move(rdo));

    return ret.release();
}

void mvlc_readout_destroy(mvlc_readout_t *rdo)
{
    delete rdo;
}

mvlc_err_t mvlc_readout_start(mvlc_readout_t *rdo, int timeToRun_s)
{
    auto ec = rdo->rdo->start(std::chrono::seconds(timeToRun_s));
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_readout_stop(mvlc_readout_t *rdo)
{
    auto ec = rdo->rdo->stop();
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_readout_pause(mvlc_readout_t *rdo)
{
    auto ec = rdo->rdo->pause();
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_readout_resume(mvlc_readout_t *rdo)
{
    auto ec = rdo->rdo->resume();
    return make_mvlc_error(ec);
}

MVLC_ReadoutState get_readout_state(const mvlc_readout_t *rdo)
{
    auto cppState = rdo->rdo->state();
    return static_cast<MVLC_ReadoutState>(cppState);
}
