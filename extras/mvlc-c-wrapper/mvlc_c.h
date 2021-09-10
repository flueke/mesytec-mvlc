#ifndef __MESYTEC_MVLC_C_H__
#define __MESYTEC_MVLC_C_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mvlc_ctrl mvlc_ctrl_t;
typedef struct mvlc_err
{
    int ec;
    void *cat;
} mvlc_err_t;

// create, destroy, copy
mvlc_ctrl_t *mvlc_ctrl_create_usb();
mvlc_ctrl_t *mvlc_ctrl_create_usb_index(unsigned index);
mvlc_ctrl_t *mvlc_ctrl_create_usb_serial(const char *serial);
mvlc_ctrl_t *mvlc_ctrl_create_eth(const char *host);

void mvlc_ctrl_destroy(mvlc_ctrl_t *mvlc);

mvlc_ctrl_t *mvlc_ctrl_copy(mvlc_ctrl_t *src);

// connection releated
mvlc_err_t


#ifdef __cplusplus
}
#endif

#endif /* __MESYTEC_MVLC_C_H__ */
