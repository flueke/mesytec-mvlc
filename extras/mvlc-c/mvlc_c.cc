#include "mvlc_c.h"
#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

struct mvlc_ctrl
{
    MVLC instance;
};

mvlc_ctrl_t *mvlc_ctrl_create_usb()
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
