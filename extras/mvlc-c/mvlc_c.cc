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

char *mvlc_format_error_alloc(const mvlc_err_t err)
{
    auto ec = std::error_code(err.ec, *reinterpret_cast<const std::error_category *>(err.cat));
    return strdup(ec.message().c_str());
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

mvlc_err_t mvlc_ctrl_enable_jumbo_frames(mvlc_ctrl_t *mvlc, bool enableJumbos)
{
    auto ec = mvlc->instance.enableJumboFrames(enableJumbos);
    return make_mvlc_error(ec);
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

int mvlc_ctrl_eth_get_command_socket(mvlc_ctrl_t *mvlc)
{
    if (auto impl = dynamic_cast<eth::Impl *>(mvlc->instance.getImpl()))
        return impl->getSocket(Pipe::Command);
    return -1;
}

int mvlc_ctrl_eth_get_data_socket(mvlc_ctrl_t *mvlc)
{
    if (auto impl = dynamic_cast<eth::Impl *>(mvlc->instance.getImpl()))
        return impl->getSocket(Pipe::Data);
    return -1;
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
    assert(mvlc);
    auto ec = mvlc->instance.writeRegister(address, value);
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_ctrl_set_daq_mode(mvlc_ctrl_t *mvlc, bool enable)
{
    return mvlc_ctrl_write_register(mvlc, DAQModeEnableRegister, enable);
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

stack_error_collection_t mvlc_ctrl_get_stack_errors(mvlc_ctrl_t *mvlc)
{
    auto counters = mvlc->instance.getStackErrorCounters();
    size_t nErrors = 0;

    for (auto &errCounts: counters.stackErrors)
        nErrors += errCounts.size();

    auto mem = std::make_unique<stack_error_t[]>(nErrors);
    auto dest = mem.get();

    for (size_t stackId = 0; stackId < counters.stackErrors.size(); ++stackId)
    {
        auto &errorCounts = counters.stackErrors[stackId];

        for (auto errIt = errorCounts.begin(); errIt != errorCounts.end(); ++errIt)
        {
            stack_error_t err = {
                static_cast<u8>(stackId),
                errIt->first.line,
                errIt->first.flags,
                static_cast<u32>(errIt->second)
            };

            assert(dest < mem.get() + nErrors);

            *dest++ = err;
        }
    }

    stack_error_collection_t errors = { mem.release(), nErrors };

    return errors;
}

void mvlc_ctrl_stack_errors_destroy(stack_error_collection_t stackErrors)
{
    delete stackErrors.errors;
}

char *mvlc_format_frame_flags(u8 flags)
{
    return strdup(format_frame_flags(flags).c_str());
}

struct mvlc_stackbuilder
{
    StackCommandBuilder sb;
};

mvlc_stackbuilder_t *mvlc_stackbuilder_create()
{
    auto ret = std::make_unique<mvlc_stackbuilder_t>();
    ret->sb = StackCommandBuilder();
    return ret.release();
}

mvlc_stackbuilder_t *mvlc_stackbuilder_create2(const char *name)
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

void mvlc_stackbuilder_set_name(mvlc_stackbuilder_t *sb, const char *name)
{
    sb->sb.setName(name);
}

bool mvlc_stackbuilder_is_empty(const mvlc_stackbuilder_t *sb)
{
    return sb->sb.empty();
}

void mvlc_stackbuilder_add_vme_read(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    sb->sb.addVMERead(address, amod, static_cast<VMEDataWidth>(dataWidth));
}

void mvlc_stackbuilder_add_vme_block_read(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, u16 maxTransfers)
{
    sb->sb.addVMEBlockRead(address, amod, maxTransfers);
}

void mvlc_stackbuilder_add_vme_mblt_swapped(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, u16 maxTransfers)
{
    sb->sb.addVMEMBLTSwapped(address, amod, maxTransfers);
}

void mvlc_stackbuilder_add_vme_write(
    mvlc_stackbuilder_t *sb, u32 address, u32 value, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    sb->sb.addVMEWrite(address, value, amod, static_cast<VMEDataWidth>(dataWidth));
}

void mvlc_stackbuilder_add_write_marker(
    mvlc_stackbuilder_t *sb, u32 value)
{
    sb->sb.addWriteMarker(value);
}

void mvlc_stackbuilder_add_set_address_increment_mode(mvlc_stackbuilder_t *sb, MVLC_AddressIncrementMode mode)
{
    sb->sb.addSetAddressIncMode(static_cast<AddressIncrementMode>(mode));
}

void mvlc_stackbuilder_add_wait(mvlc_stackbuilder_t *sb, u32 clocks)
{
    sb->sb.addWait(clocks);
}

void mvlc_stackbuilder_add_signal_accu(mvlc_stackbuilder_t *sb)
{
    sb->sb.addSignalAccu();
}

void mvlc_stackbuilder_add_mask_shift_accu(mvlc_stackbuilder_t *sb, u32 mask, u8 shift)
{
    sb->sb.addMaskShiftAccu(mask, shift);
}

void mvlc_stackbuilder_add_set_accu(mvlc_stackbuilder_t *sb, u32 accuValue)
{
    sb->sb.addSetAccu(accuValue);
}

void mvlc_stackbuilder_add_read_to_accu(mvlc_stackbuilder_t *sb, u32 address, u8 amod, MVLC_VMEDataWidth dataWidth)
{
    sb->sb.addReadToAccu(address, amod, static_cast<VMEDataWidth>(dataWidth));
}

void mvlc_stackbuilder_add_writespecial(mvlc_stackbuilder_t *sb, u32 specialValue)
{
    sb->sb.addWriteSpecial(specialValue);
}

void mvlc_stackbuilder_begin_group(
    mvlc_stackbuilder_t *sb, const char *name)
{
    sb->sb.beginGroup(name);
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

u16 mvlc_get_stack_offset_register(u8 stackId)
{
    return stacks::get_offset_register(stackId);
}

u16 mvlc_get_stack_trigger_register(u8 stackId)
{
    return stacks::get_trigger_register(stackId);
}

size_t mvlc_get_stack_size_words(const mvlc_stackbuilder_t *sb)
{
    return make_stack_buffer(sb->sb).size();
}

mvlc_err_t mvlc_upload_stack(
    mvlc_ctrl_t *mvlc, u8 outputPipe, u16 stackMemoryOffset, const mvlc_stackbuilder_t *sb)
{
    assert(mvlc);
    assert(sb);

    auto ec = mvlc->instance.uploadStack(outputPipe, stackMemoryOffset, sb->sb);
    return make_mvlc_error(ec);
}

u16 mvlc_calculate_trigger_value(MVLC_StackTriggerType trigger, u8 irq)
{
    return trigger_value(static_cast<stacks::TriggerType>(trigger), irq);
}

mvlc_err_t mvlc_setup_readout_stack(
    mvlc_ctrl_t *mvlc, const mvlc_stackbuilder_t *sb,
    u8 stackId, u32 triggerValue)
{
    auto ec = setup_readout_stack(
        mvlc->instance,
        sb->sb,
        stackId,
        triggerValue);
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_setup_readout_stack2(
    mvlc_ctrl_t *mvlc, const mvlc_stackbuilder_t *sb,
    u8 stackId, MVLC_StackTriggerType trigger, u8 irq)
{
    auto triggerValue = mvlc_calculate_trigger_value(trigger, irq);
    return mvlc_setup_readout_stack(mvlc, sb, stackId, triggerValue);
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

// Wraps the C-layer mvlc_listfile_write_handle_t function in the required C++
// WriteHandle subclass. Negative return values from the C-layer (indicating a write error) are
// thrown as std::runtime_errors.
struct ListfileWriteHandleWrapper: public listfile::WriteHandle
{
    ListfileWriteHandleWrapper(mvlc_listfile_write_handle_t listfile_handle, void *userContext = nullptr)
        : listfile_handle(listfile_handle)
        , userContext(userContext)
    { }

    size_t write(const u8 *data, size_t size) override
    {
        ssize_t res = listfile_handle(userContext, data, size);
        if (res < 0)
            throw std::runtime_error("wrapped listfile write failed: " + std::to_string(res));
        return res;
    }

    mvlc_listfile_write_handle_t listfile_handle;
    void *userContext;
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
        readout_parser_callbacks_t parser_callbacks,
        void *userContext)
    {
        readout_parser::ReadoutParserCallbacks parserCallbacks;

        parserCallbacks.eventData = [parser_callbacks, userContext] (
            void *,
            int /*crateIndex*/,
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
                    modulesDataC[i].data = { modulesDataCpp[i].data.data,  modulesDataCpp[i].data.size };
                }

                parser_callbacks.event_data(userContext, eventIndex, modulesDataC.begin(), moduleCount);
            }
        };

        parserCallbacks.systemEvent = [parser_callbacks, userContext] (
            void *,
            int /*crateIndex*/,
            const u32 *header,
            u32 size)
        {
            if (parser_callbacks.system_event)
                parser_callbacks.system_event(userContext, header, size);
        };

        return parserCallbacks;
    }

    inline ListfileParams to_cpp_listfile_params(const mvlc_listfile_params_t lfParamsC)
    {
        ListfileParams lfParamsCpp = {};

        lfParamsCpp.writeListfile = lfParamsC.writeListfile;
        lfParamsCpp.filepath = lfParamsC.filepath;
        lfParamsCpp.listfilename = lfParamsC.listfilename;
        lfParamsCpp.overwrite = lfParamsC.overwrite;
        lfParamsCpp.compression = static_cast<ListfileParams::Compression>(lfParamsC.compression);
        lfParamsCpp.compressionLevel = lfParamsC.compressionLevel;

        return lfParamsCpp;
    }


} // end anon namespace

mvlc_readout_t *mvlc_readout_create(
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_write_handle_t lfh,
    readout_parser_callbacks_t parser_callbacks,
    void *userContext)
{
    auto lfWrap = std::make_unique<ListfileWriteHandleWrapper>(lfh, userContext);

    auto rdo = make_mvlc_readout(
        crateconfig->cfg,
        lfWrap.get(),
        wrap_parser_callbacks(parser_callbacks, userContext));

    auto ret = std::make_unique<mvlc_readout>();
    ret->rdo = std::make_unique<MVLCReadout>(std::move(rdo));
    ret->lfWrap = std::move(lfWrap);

    return ret.release();
}

mvlc_readout_t *mvlc_readout_create2(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_write_handle_t lfh,
    readout_parser_callbacks_t parser_callbacks,
    void *userContext)
{
    auto lfWrap = std::make_unique<ListfileWriteHandleWrapper>(lfh, userContext);

    auto rdo = make_mvlc_readout(
        mvlc->instance,
        crateconfig->cfg,
        lfWrap.get(),
        wrap_parser_callbacks(parser_callbacks, userContext));

    auto ret = std::make_unique<mvlc_readout>();
    ret->rdo = std::make_unique<MVLCReadout>(std::move(rdo));
    ret->lfWrap = std::move(lfWrap);

    return ret.release();
}

mvlc_readout_t *mvlc_readout_create3(
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_params_t lfParamsC,
    readout_parser_callbacks_t parser_callbacks,
    void *userContext)
{
    auto lfParamsCpp = to_cpp_listfile_params(lfParamsC);

    auto rdo = make_mvlc_readout(
        crateconfig->cfg,
        lfParamsCpp,
        wrap_parser_callbacks(parser_callbacks, userContext));

    auto ret = std::make_unique<mvlc_readout>();
    ret->rdo = std::make_unique<MVLCReadout>(std::move(rdo));

    return ret.release();
}

mvlc_readout_t *mvlc_readout_create4(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_params_t lfParamsC,
    readout_parser_callbacks_t parser_callbacks,
    void *userContext)
{
    auto lfParamsCpp = to_cpp_listfile_params(lfParamsC);

    auto rdo = make_mvlc_readout(
        mvlc->instance,
        crateconfig->cfg,
        lfParamsCpp,
        wrap_parser_callbacks(parser_callbacks, userContext));

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
    auto cppState = rdo->rdo->workerState();
    return static_cast<MVLC_ReadoutState>(cppState);
}

// Replay
// ---------------------------------------------------------------------
struct ListfileReadHandleWrapper: public listfile::ReadHandle
{
    ListfileReadHandleWrapper(mvlc_listfile_read_handle_t listfile_handle, void *userContext = nullptr)
        : listfile_handle(listfile_handle)
        , userContext(userContext)
    { }

    size_t read(u8 *dest, size_t maxSize) override
    {
        ssize_t res = listfile_handle.read_func(userContext, dest, maxSize);
        if (res < 0)
            throw std::runtime_error("wrapped listfile read failed: " + std::to_string(res));
        return res;
    }

    void seek(size_t pos) override
    {
        ssize_t res = listfile_handle.seek_func(userContext, pos);
        if (res < 0)
            throw std::runtime_error("wrapped listfile seek failed: " + std::to_string(res));
    }

    mvlc_listfile_read_handle_t listfile_handle;
    void *userContext;
};

struct mvlc_replay
{
    std::unique_ptr<MVLCReplay> replay;
    std::unique_ptr<ListfileReadHandleWrapper> rh;
};

mvlc_replay_t *mvlc_replay_create(
    const char *listfileFilename,
    readout_parser_callbacks_t event_callbacks,
    void *userContext)
{
    auto replay = make_mvlc_replay(
        listfileFilename,
        wrap_parser_callbacks(event_callbacks, userContext));

    auto ret = std::make_unique<mvlc_replay>();
    ret->replay = std::make_unique<MVLCReplay>(std::move(replay));

    return ret.release();
}

mvlc_replay_t *mvlc_replay_create2(
    mvlc_listfile_read_handle_t lfh,
    readout_parser_callbacks_t event_callbacks,
    void *userContext)
{
    auto rh = std::make_unique<ListfileReadHandleWrapper>(lfh, userContext);
    auto replay = make_mvlc_replay(rh.get(), wrap_parser_callbacks(event_callbacks, userContext));

    auto ret = std::make_unique<mvlc_replay>();
    ret->replay = std::make_unique<MVLCReplay>(std::move(replay));
    ret->rh = std::move(rh);

    return ret.release();
}

void mvlc_replay_destroy(mvlc_replay_t *replay)
{
    delete replay;
}

mvlc_err_t mvlc_replay_start(mvlc_replay_t *replay)
{
    auto ec = replay->replay->start();
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_replay_stop(mvlc_replay_t *replay)
{
    auto ec = replay->replay->stop();
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_replay_pause(mvlc_replay_t *replay)
{
    auto ec = replay->replay->pause();
    return make_mvlc_error(ec);
}

mvlc_err_t mvlc_replay_resume(mvlc_replay_t *replay)
{
    auto ec = replay->replay->resume();
    return make_mvlc_error(ec);
}

MVLC_ReadoutState get_replay_state(const mvlc_replay_t *replay)
{
    auto cppState = replay->replay->workerState();
    return static_cast<MVLC_ReadoutState>(cppState);
}

mvlc_crateconfig_t *mvlc_replay_get_crateconfig(const mvlc_replay_t *replay)
{
    auto cfg = replay->replay->crateConfig();
    auto ret = std::make_unique<mvlc_crateconfig_t>();
    ret->cfg = cfg;
    return ret.release();
}

// Blocking API

struct mvlc_blocking_readout
{
    std::unique_ptr<BlockingReadout> r;
    std::unique_ptr<ListfileWriteHandleWrapper> lfWrap;
};

struct mvlc_blocking_replay
{
    std::unique_ptr<BlockingReplay> r;
    std::unique_ptr<ListfileReadHandleWrapper> lfWrap;
};

namespace
{
    inline event_container_t to_c_container(const EventContainer &cppEvent)
    {
        event_container_t ret = {};

        switch (cppEvent.type)
        {
            case EventContainer::Type::None:
                ret.type = MVLC_EventType_None;
                break;

            case EventContainer::Type::Readout:
                ret.type = MVLC_EventType_Readout;
                ret.readout.eventIndex = cppEvent.readout.eventIndex;

                for (size_t i=0; i<cppEvent.readout.moduleCount; ++i)
                {
                    auto cppData = cppEvent.readout.moduleDataList[i];
                    ret.readout.moduleData[i].data  = { cppData.data.data,  cppData.data.size };
                }

                ret.readout.moduleCount = cppEvent.readout.moduleCount;
                break;

            case EventContainer::Type::System:
                ret.type = MVLC_EventType_System;
                ret.system.header = cppEvent.system.header;
                ret.system.size = cppEvent.system.size;
                break;
        }

        return ret;
    }
}

event_container_t next_readout_event(mvlc_blocking_readout_t *r)
{
    return to_c_container(next_event(*r->r));
}

event_container_t next_replay_event(mvlc_blocking_replay_t *r)
{
    return to_c_container(next_event(*r->r));
}

mvlc_blocking_readout_t *mvlc_blocking_readout_create(
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle_t lfh)
{
    auto lfWrap = std::make_unique<ListfileWriteHandleWrapper>(lfh);
    auto r = make_mvlc_readout_blocking(cfg->cfg, lfWrap.get());
    auto ret = std::make_unique<mvlc_blocking_readout_t>();
    ret->r = std::make_unique<BlockingReadout>(std::move(r));
    ret->lfWrap = std::move(lfWrap);
    return ret.release();
}

mvlc_blocking_readout_t *mvlc_blocking_readout_create2(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle_t lfh)
{
    auto lfWrap = std::make_unique<ListfileWriteHandleWrapper>(lfh);
    auto r = make_mvlc_readout_blocking(mvlc->instance, cfg->cfg, lfWrap.get());
    auto ret = std::make_unique<mvlc_blocking_readout_t>();
    ret->r = std::make_unique<BlockingReadout>(std::move(r));
    ret->lfWrap = std::move(lfWrap);
    return ret.release();
}

mvlc_blocking_readout_t *mvlc_blocking_readout_create3(
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_params_t lfParamsC)
{
    auto lfParamsCpp = to_cpp_listfile_params(lfParamsC);
    auto r = make_mvlc_readout_blocking(cfg->cfg, lfParamsCpp);
    auto ret = std::make_unique<mvlc_blocking_readout_t>();
    ret->r = std::make_unique<BlockingReadout>(std::move(r));
    return ret.release();
}

mvlc_blocking_readout_t *mvlc_blocking_readout_create4(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_params_t lfParamsC)
{
    auto lfParamsCpp = to_cpp_listfile_params(lfParamsC);
    auto r = make_mvlc_readout_blocking(mvlc->instance, cfg->cfg, lfParamsCpp);
    auto ret = std::make_unique<mvlc_blocking_readout_t>();
    ret->r = std::make_unique<BlockingReadout>(std::move(r));
    return ret.release();
}

mvlc_err_t mvlc_blocking_readout_start(mvlc_blocking_readout_t *r, int timeToRun_s)
{
    auto ec = r->r->start(std::chrono::seconds(timeToRun_s));
    return make_mvlc_error(ec);
}

void mvlc_blocking_readout_destroy(mvlc_blocking_readout_t *r)
{
    delete r;
}

mvlc_blocking_replay_t *mvlc_blocking_replay_create(
    const char *listfileArchiveFilename)
{
    auto r = make_mvlc_replay_blocking(listfileArchiveFilename);
    auto ret = std::make_unique<mvlc_blocking_replay_t>();
    ret->r = std::make_unique<BlockingReplay>(std::move(r));
    return ret.release();
}

mvlc_blocking_replay_t *mvlc_blocking_replay_create2(
    const char *listfileArchiveName,
    const char *listfileArchiveMemberName)
{
    auto r = make_mvlc_replay_blocking(listfileArchiveName, listfileArchiveMemberName);
    auto ret = std::make_unique<mvlc_blocking_replay_t>();
    ret->r = std::make_unique<BlockingReplay>(std::move(r));
    return ret.release();
}

mvlc_blocking_replay_t *mvlc_blocking_replay_create3(
    mvlc_listfile_read_handle_t lfh)
{
    auto lfWrap = std::make_unique<ListfileReadHandleWrapper>(lfh);
    auto r = make_mvlc_replay_blocking(lfWrap.get());
    auto ret = std::make_unique<mvlc_blocking_replay_t>();
    ret->r = std::make_unique<BlockingReplay>(std::move(r));
    ret->lfWrap = std::move(lfWrap);
    return ret.release();
}

mvlc_err_t mvlc_blocking_replay_start(mvlc_blocking_replay_t *r)
{
    auto ec = r->r->start();
    return make_mvlc_error(ec);
}

void mvlc_blocking_replay_destroy(mvlc_blocking_replay_t *r)
{
    delete r;
}
