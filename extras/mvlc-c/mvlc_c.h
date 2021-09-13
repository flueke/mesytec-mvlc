#ifndef __MESYTEC_MVLC_C_H__
#define __MESYTEC_MVLC_C_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO:
// - access to stack error counters
// - enable/disable jumbo frames
// - maybe low level reads: usb: read_unbuffered(), eth: read_packet()
// - load crate config from yaml
// - create listfiles
// - readout worker
// - readout parser
// - queue between readout and parser

// Numeric types
// =====================================================================
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// Error handling
// =====================================================================
typedef struct mvlc_err
{
    int ec;
    const void *cat;
} mvlc_err_t;

bool mvlc_is_error(const mvlc_err_t err);
char *mvlc_format_error(const mvlc_err_t err, char *buf, size_t bufsize);

// mvlc_ctrl_t: create, destroy, copy
// =====================================================================
typedef struct mvlc_ctrl mvlc_ctrl_t;

mvlc_ctrl_t *mvlc_ctrl_create_usb(void);
mvlc_ctrl_t *mvlc_ctrl_create_usb_index(unsigned index);
mvlc_ctrl_t *mvlc_ctrl_create_usb_serial(const char *serial);
mvlc_ctrl_t *mvlc_ctrl_create_eth(const char *host);

void mvlc_ctrl_destroy(mvlc_ctrl_t *mvlc);

mvlc_ctrl_t *mvlc_ctrl_copy(mvlc_ctrl_t *src);

// Connection releated
// =====================================================================
mvlc_err_t mvlc_ctrl_connect(mvlc_ctrl_t *mvlc);
mvlc_err_t mvlc_ctrl_disconnect(mvlc_ctrl_t *mvlc);
bool mvlc_ctrl_is_connected(mvlc_ctrl_t *mvlc);
void mvlc_ctrl_set_disable_trigger_on_connect(mvlc_ctrl_t *mvlc, bool disableTriggers);

// Info
// =====================================================================
u32 get_mvlc_ctrl_hardware_id(mvlc_ctrl_t *mvlc);
u32 get_mvlc_ctrl_firmware_revision(mvlc_ctrl_t *mvlc);
// Note: uses strdup() interally so you have to free() the string after use.
char *get_mvlc_ctrl_connection_info(mvlc_ctrl_t *mvlc);

// Access to internal registers
// =====================================================================
mvlc_err_t mvlc_ctrl_read_register(mvlc_ctrl_t *mvlc, u16 address, u32 *value);
mvlc_err_t mvlc_ctrl_write_register(mvlc_ctrl_t *mvlc, u16 address, u32 value);

// VME bus access
// =====================================================================
typedef enum
{
    MVLC_VMEDataWidth_D16 = 0x1,
    MVLC_VMEDataWidth_D32 = 0x2
} MVLC_VMEDataWidth;

mvlc_err_t mvlc_ctrl_vme_read(mvlc_ctrl_t *mvlc, u32 address, u32 *value, u8 amod, MVLC_VMEDataWidth dataWidth);
mvlc_err_t mvlc_ctrl_vme_write(mvlc_ctrl_t *mvlc, u32 address, u32 value, u8 amod, MVLC_VMEDataWidth dataWidth);

// Allocates memory into *buf, stores the allocated size (in number of 32-bit
// words) into bufsize. The buffer needs to be free()d by the caller.
mvlc_err_t mvlc_ctrl_vme_block_read_alloc(mvlc_ctrl_t *mvlc, u32 address, u8 amod, u16 maxTransfers,
                                          u32 **buf, size_t *bufsize);

// Reads into the given buffer, storing a maximum of bufsize words. Any
// additional data from the VME block read is discarded. bufsize must contain
// the number of words available in the buffer. After the function return
// bufsize will contain the actual number of 32-bit words stored in the buffer.
mvlc_err_t mvlc_ctrl_vme_block_read_buffer(mvlc_ctrl_t *mvlc, u32 address, u8 amod, u16 maxTransfers,
                                           u32 *buf, size_t bufsize);

// Like the block read functions above but perform a 32-bit word swap on the 64-bit VME MBLT data.
mvlc_err_t mvlc_ctrl_vme_mblt_swapped_alloc(mvlc_ctrl_t *mvlc, u32 address, u16 maxTransfers,
                                            u32 **buf, size_t *bufsize);

mvlc_err_t mvlc_ctrl_vme_mblt_swapped_buffer(mvlc_ctrl_t *mvlc, u32 address, u16 maxTransfers,
                                             u32 *buf, size_t *bufsize);

// Readout abstractions
// =====================================================================

// Crateconfig
typedef struct mvlc_crateconfig mvlc_crateconfig_t;

// TODO: allow creating, inspecting and modifying createconfigs
mvlc_crateconfig_t *mvlc_read_crateconfig_from_file(const char *filename);
mvlc_crateconfig_t *mvlc_read_crateconfig_from_string(const char *str);
void mvlc_crateconfig_destroy(mvlc_crateconfig_t *cfg);
mvlc_ctrl_t *mvlc_ctrl_create_from_crateconfig(mvlc_crateconfig_t *cfg);

// Listfile write handle function. Must return the number of bytes written or a
// negative value in case of an error.
typedef ssize_t (mvlc_listfile_write_handle) (const u8 *data, size_t size);

// Readout object combining readout worker and readout parser.
typedef struct readout_datablock
{
    const u32 *data;
    u32 size;
} readout_datablock_t;

typedef struct readout_moduledata
{
    readout_datablock_t prefix;
    readout_datablock_t dynamic;
    readout_datablock_t suffix;
} readout_moduledata_t;

typedef struct readout_parser_callbacks
{
    typedef void (rdo_event_data_callback) (int eventIndex, const readout_moduledata_t *moduleDataList, unsigned moduleCount);
    typedef void (rdo_system_event_callback) (const u32 *header, u32 size);

    rdo_event_data_callback event_data;
    rdo_system_event_callback system_event;
} readout_parser_callbacks_t;

typedef struct mvlc_readout mvlc_readout_t;

mvlc_readout_t *mvlc_readout_create(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle lfh,
    readout_parser_callbacks_t callbacks);

void mvlc_readout_destroy(mvlc_readout_t *rdo);

mvlc_err_t readout_start(mvlc_readout_t *rdo);
mvlc_err_t readout_stop(mvlc_readout_t *rdo);
mvlc_err_t readout_pause(mvlc_readout_t *rdo);
mvlc_err_t readout_resume(mvlc_readout_t *rdo);
mvlc_err_t readout_state(mvlc_readout_t *rdo);

#ifdef __cplusplus
}
#endif

#endif /* __MESYTEC_MVLC_C_H__ */
