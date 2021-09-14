#include "mvlc_c.h"

#include <cassert>
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

mvlc_ctrl_t *mvlc_ctrl_create_from_crateconfig(mvlc_crateconfig_t *cfg)
{
    auto ret = std::make_unique<mvlc_ctrl_t>();
    ret->instance = make_mvlc(cfg->cfg);
    return ret.release();
}

struct mvlc_readout
{
    mvlc_ctrl_t *mvlc;
    mvlc_crateconfig *crateconfig;
    mvlc_listfile_write_handle *listfile_handle;
    readout_parser_callbacks_t parser_callbacks;
};

mvlc_readout_t *mvlc_readout_create(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *crateconfig,
    mvlc_listfile_write_handle *listfile_handle,
    readout_parser_callbacks_t parser_callbacks)
{
    auto ret = std::make_unique<mvlc_readout_t>();

    ret->mvlc = mvlc;
    ret->crateconfig = crateconfig;
    ret->listfile_handle = listfile_handle;
    ret->parser_callbacks = parser_callbacks;

    return ret.release();
}

void mvlc_readout_destroy(mvlc_readout_t *rdo)
{
    delete rdo;
}
