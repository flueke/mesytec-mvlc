#ifndef __MESYTEC_MVLC_C_H__
#define __MESYTEC_MVLC_C_H__

/**
 * \defgroup mvlc-c mvlc-c - C language interface for the mesytec-mvlc driver library
 * \{
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO:
// - maybe low level reads: usb: read_unbuffered(), eth: read_packet() (need a
//   PacketReadResult structure or similar)

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

/**
 * C version of the information contained in std::error_code objects.
 */
typedef struct mvlc_err
{
    int ec;             // std::error_code::value()
    const void *cat;    // &std::error_code::category()
} mvlc_err_t;

bool mvlc_is_error(const mvlc_err_t err);
char *mvlc_format_error(const mvlc_err_t err, char *buf, size_t bufsize);
// Note: uses strdup() interally so you have to free() the returned string after use.
char *mvlc_format_error_alloc(const mvlc_err_t err);

// mvlc_ctrl_t: create, destroy, copy
// =====================================================================

/**
 * Handle type representing an MVLC controller.
 */
typedef struct mvlc_ctrl mvlc_ctrl_t;

/** MVLC controller factory function using the first MVLC_USB found on the system. */
mvlc_ctrl_t *mvlc_ctrl_create_usb(void);

/** MVLC controller factory function using the MVLC_USB with the specified
 * zero-based index. */
mvlc_ctrl_t *mvlc_ctrl_create_usb_index(unsigned index);

/** MVLC controller factory function using the MVLC_USB with the specified
 * serial number string.
 * Example: mvlc_ctrl_create_usb_serial("02220066");
 */
mvlc_ctrl_t *mvlc_ctrl_create_usb_serial(const char *serial);

/** MVLC controller factory function for an MVLC_ETH with the given hostname/ip address. */
mvlc_ctrl_t *mvlc_ctrl_create_eth(const char *host);

/** Destroys a mvlc_ctrl_t instance. */
void mvlc_ctrl_destroy(mvlc_ctrl_t *mvlc);

/** Creates a copy of the underlying MVLC object. It is safe to use the copy
 * from a different thread. */
mvlc_ctrl_t *mvlc_ctrl_copy(mvlc_ctrl_t *src);

// Connection releated
// ---------------------------------------------------------------------
mvlc_err_t mvlc_ctrl_connect(mvlc_ctrl_t *mvlc);
mvlc_err_t mvlc_ctrl_disconnect(mvlc_ctrl_t *mvlc);
bool mvlc_ctrl_is_connected(mvlc_ctrl_t *mvlc);
void mvlc_ctrl_set_disable_trigger_on_connect(mvlc_ctrl_t *mvlc, bool disableTriggers);
mvlc_err_t mvlc_ctrl_enable_jumbo_frames(mvlc_ctrl_t *mvlc, bool enableJumbos);

typedef enum
{
    MVLC_ConnectionType_USB,
    MVLC_ConnectionType_ETH,
} MVLC_ConnectionType;

MVLC_ConnectionType get_mvlc_ctrl_connection_type(const mvlc_ctrl_t *mvlc);

// Ethernet specific
int mvlc_ctrl_eth_get_command_socket(mvlc_ctrl_t *mvlc);
int mvlc_ctrl_eth_get_data_socket(mvlc_ctrl_t *mvlc);

// Info
// ---------------------------------------------------------------------
u32 get_mvlc_ctrl_hardware_id(mvlc_ctrl_t *mvlc);
u32 get_mvlc_ctrl_firmware_revision(mvlc_ctrl_t *mvlc);
// Note: uses strdup() interally so you have to free() the returned string after use.
char *get_mvlc_ctrl_connection_info(mvlc_ctrl_t *mvlc);

// Access to internal registers
// ---------------------------------------------------------------------
mvlc_err_t mvlc_ctrl_read_register(mvlc_ctrl_t *mvlc, u16 address, u32 *value);
mvlc_err_t mvlc_ctrl_write_register(mvlc_ctrl_t *mvlc, u16 address, u32 value);

// Enable/disable DAQ mode (autonomous processing of triggers and stack command execution).
mvlc_err_t mvlc_ctrl_set_daq_mode(mvlc_ctrl_t *mvlc, bool enable);


// VME bus access
// ---------------------------------------------------------------------

// The slow bit for VME reads is required for modules that are not 100% VME
// conformant.
#define MVLC_SlowReadBit 2u

typedef enum
{
    MVLC_VMEDataWidth_D16 = 0x1,
    MVLC_VMEDataWidth_D32 = 0x2,
    MVLC_VMEDataWidth_D16_slow = 0x1 | (1u << MVLC_SlowReadBit),
    MVLC_VMEDataWidth_D32_slow = 0x2 | (1u << MVLC_SlowReadBit)
} MVLC_VMEDataWidth;

// Single register VME read
mvlc_err_t mvlc_ctrl_vme_read(
    mvlc_ctrl_t *mvlc, u32 address, u32 *value, u8 amod, MVLC_VMEDataWidth dataWidth);

// Single register VME write
mvlc_err_t mvlc_ctrl_vme_write(
    mvlc_ctrl_t *mvlc, u32 address, u32 value, u8 amod, MVLC_VMEDataWidth dataWidth);

// VME block read (BLT/MBLT)
// Allocates memory into *buf, stores the allocated size (in number of 32-bit
// words) into bufsize. The buffer needs to be free()d by the caller.
mvlc_err_t mvlc_ctrl_vme_block_read_alloc(
    mvlc_ctrl_t *mvlc, u32 address, u8 amod, u16 maxTransfers,
    u32 **buf, size_t *bufsize);

// VME block read (BLT/MBLT)
// Reads into the given buffer, storing a maximum of bufsize words. Any
// additional data from the VME block read is discarded. bufsize must contain
// the number of words available in the buffer. After the function returns
// bufsize will contain the actual number of 32-bit words stored in the buffer.
mvlc_err_t mvlc_ctrl_vme_block_read_buffer(
    mvlc_ctrl_t *mvlc, u32 address, u8 amod, u16 maxTransfers,
    u32 *buf, size_t *bufsize);

// Like the block read functions above but perform a 32-bit word swap on the
// 64-bit VME MBLT data. Required for some VME modules.
mvlc_err_t mvlc_ctrl_vme_mblt_swapped_alloc(
    mvlc_ctrl_t *mvlc, u32 address, u16 maxTransfers,
    u32 **buf, size_t *bufsize);

mvlc_err_t mvlc_ctrl_vme_mblt_swapped_buffer(
    mvlc_ctrl_t *mvlc, u32 address, u16 maxTransfers,
    u32 *buf, size_t *bufsize);

// Stack error counters
// ---------------------------------------------------------------------
typedef struct stack_error
{
    u8 stackId;
    u16 stackLine;
    u8 frameFlags;
    u32 count;
} stack_error_t;

typedef struct stack_error_collection
{
    stack_error_t *errors;
    size_t count;
} stack_error_collection_t;

stack_error_collection_t mvlc_ctrl_get_stack_errors(mvlc_ctrl_t *mvlc);
void mvlc_ctrl_stack_errors_destroy(stack_error_collection_t stackErrors);
// Note: uses strdup() internally so you have to free() the returned string after use.
char *mvlc_format_frame_flags(u8 flags);

// Command stack and readout abstractions
// =====================================================================

// StackCommandBuilder
// ---------------------------------------------------------------------
typedef struct mvlc_stackbuilder mvlc_stackbuilder_t;

#define MVLC_TotalStackCount 8u
#define MVLC_ReadoutStackCount 7u

mvlc_stackbuilder_t *mvlc_stackbuilder_create();
mvlc_stackbuilder_t *mvlc_stackbuilder_create2(const char *name);
void mvlc_stackbuilder_destroy(mvlc_stackbuilder_t *sb);
mvlc_stackbuilder_t *mvlc_stackbuilder_copy(const mvlc_stackbuilder_t *sb);
bool mvlc_stackbuilder_equals(const mvlc_stackbuilder_t *sba, const mvlc_stackbuilder_t *sbb);

mvlc_stackbuilder_t *mvlc_read_stackbuilder_from_file(const char *filename);
mvlc_err_t mvlc_write_stackbuilder_to_file(const mvlc_stackbuilder_t *sb, const char *filename);

// Note: uses strdup() interally so you have to free() the returned string after use.
const char *mvlc_stackbuilder_get_name(const mvlc_stackbuilder_t *sb);
void mvlc_stackbuilder_set_name(mvlc_stackbuilder_t *sb, const char *name);

bool mvlc_stackbuilder_is_empty(const mvlc_stackbuilder_t *sb);

void mvlc_stackbuilder_add_vme_read(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, MVLC_VMEDataWidth dataWidth);

void mvlc_stackbuilder_add_vme_block_read(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, u16 maxTransfers);

void mvlc_stackbuilder_add_vme_mblt_swapped(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, u16 maxTransfers);

void mvlc_stackbuilder_add_vme_write(
    mvlc_stackbuilder_t *sb, u32 address, u32 value, u8 amod, MVLC_VMEDataWidth dataWidth);

void mvlc_stackbuilder_add_write_marker(
    mvlc_stackbuilder_t *sb, u32 value);

// Set the address increment mode for subsequent VME block reads.
typedef enum
{
    MVLC_AddressIncrement_FIFO,
    MVLC_AddressIncrement_Memory
} MVLC_AddressIncrementMode;

void mvlc_stackbuilder_add_set_address_increment_mode(
    mvlc_stackbuilder_t *sb, MVLC_AddressIncrementMode mode);

void mvlc_stackbuilder_add_wait(mvlc_stackbuilder_t *sb, u32 clocks);
void mvlc_stackbuilder_add_signal_accu(mvlc_stackbuilder_t *sb);
void mvlc_stackbuilder_add_mask_shift_accu(mvlc_stackbuilder_t *sb, u32 mask, u8 shift);
void mvlc_stackbuilder_add_set_accu(mvlc_stackbuilder_t *sb, u32 accuValue);
void mvlc_stackbuilder_add_read_to_accu(
    mvlc_stackbuilder_t *sb, u32 address, u8 amod, MVLC_VMEDataWidth dataWidth);

typedef enum
{
    MVLC_AccuComparator_EQ, // ==
    MVLC_AccuComparator_LT, // <
    MVLC_AccuComparator_GT, // >
} MVLC_AccuComparator;

void mvlc_stackbuilder_add_compare_loop_accu(
    mvlc_stackbuilder_t *sb, MVLC_AccuComparator comparator, u32 compareValue);

typedef enum
{
    MVLC_SpecialWord_Timestamp,
    MVLC_SpecialWord_Accu
} MVLC_SpecialWord;

void mvlc_stackbuilder_add_writespecial(
    mvlc_stackbuilder_t *sb, u32 specialWord);

// Support for stack groups
void mvlc_stackbuilder_begin_group(
    mvlc_stackbuilder_t *sb, const char *name);

bool mvlc_stackbuilder_has_open_group(
    const mvlc_stackbuilder_t *sb);

size_t mvlc_stackbuilder_get_group_count(
    const mvlc_stackbuilder_t *sb);

// Note: uses strdup() interally so you have to free() the returned string after use.
const char *mvlc_stackbuilder_get_group_name(
    const mvlc_stackbuilder_t *sb,
    size_t groupIndex);

// Low level command stack uploading and setup
// ---------------------------------------------------------------------
//
// Note: stackId=0 is reserved for direct/immediate command execution.  Library
// convention: The immedate stack starts at word offset 1 from the beginning of
// the stack memory and a total of 127 words is reserved for the stack.
// The first word of the stack memory is left free so that unused stack offset
// registers, which default to 0, do not point to a valid StackStart command.
#define MVLC_ImmediateStackStartOffset 1
#define MVLC_ImmedateStackReservedWords 127

// Returns the address of the stack offset register for the given stackId.
u16 mvlc_get_stack_offset_register(u8 stackId);

// Returns the address of the stack trigger register for the given stackId.
u16 mvlc_get_stack_trigger_register(u8 stackId);

// Returns the number of 32-bit words the stack would occupy in MVLC stack
// memory.
size_t mvlc_get_stack_size_in_words(const mvlc_stackbuilder_t *sb);

// Output pipes are:
#define MVLC_CommandPipe 0
#define MVLC_DataPipe 1
#define MVLC_SuppressPipeOutput 2

// Creates a stack buffer from the stack builder and uploads the stack starting
// at the given memory offset.
mvlc_err_t mvlc_upload_stack(
    mvlc_ctrl_t *mvlc, u8 outputPipe, u16 stackMemoryOffset,
    const mvlc_stackbuilder_t *sb);

typedef enum
{
    StackTrigger_NoTrigger   = 0, // No autonomous execution of the stack.
    StackTrigger_IRQWithIACK = 1, // IRQ based; slow version for modules requiring the VME IACK
    StackTrigger_IRQNoIACK   = 2, // IRQ based; fast version without IACK, works with mesytec modules
    StackTrigger_External    = 3, // via the Trigger/IO system
} MVLC_StackTriggerType;

// Calculate the value for the stack trigger register. The 'irq' parameter is
// ignored for non-irq trigger types.
u16 mvlc_calculate_trigger_value(MVLC_StackTriggerType trigger, u8 irq);

// Higher level readout stack handling
// ---------------------------------------------------------------------

// Combines uploading the command stack, setting up the stack memory offset
// register and writing the stack trigger register.
// Assumes a memory layout where the stack memory is divided into equal sized
// parts of 128 words each. So stack1 is written to the memory starting at
// offset 128, stack2 at offset 256..
// This function is not intended to be used for stack0, the stack reserved for
// immediate command execution.
mvlc_err_t mvlc_setup_readout_stack(
    mvlc_ctrl_t *mvlc, const mvlc_stackbuilder_t *sb,
    u8 stackId, u32 triggerValue);

// Same as above but takes a triggerType and an irq value instead of a
// precalculated triggerValue.
mvlc_err_t mvlc_setup_readout_stack2(
    mvlc_ctrl_t *mvlc, const mvlc_stackbuilder_t *sb,
    u8 stackId, MVLC_StackTriggerType trigger, u8 irq);


// Crateconfig
// ---------------------------------------------------------------------
typedef struct mvlc_crateconfig mvlc_crateconfig_t;

mvlc_crateconfig_t *mvlc_read_crateconfig_from_file(const char *filename);
mvlc_crateconfig_t *mvlc_read_crateconfig_from_string(const char *str);
void mvlc_crateconfig_destroy(mvlc_crateconfig_t *cfg);

// Note: uses strdup() interally so you have to free() the returned string after use.
const char *mvlc_crateconfig_to_string(const mvlc_crateconfig_t *cfg);

mvlc_err_t mvlc_write_crateconfig_to_file(
    const mvlc_crateconfig_t *cfg,
    const char *filename);

bool mvlc_crateconfig_equals(mvlc_crateconfig_t *ca, mvlc_crateconfig_t *cb);

MVLC_ConnectionType mvlc_crateconfig_get_connection_type(const mvlc_crateconfig_t *cfg);

typedef struct mvlc_stacktriggers
{
    u32 triggerValues[MVLC_ReadoutStackCount];
} mvlc_stacktriggers_t;

mvlc_stacktriggers_t mvlc_crateconfig_get_stack_triggers(const mvlc_crateconfig_t *cfg);

// Note: a copy of the readout stack is returned. Modifying the copy won't
// affect the original stored in the stack builder. The usage pattern is to get
// the readout stack, modify it and set it again.
mvlc_stackbuilder_t *mvlc_crateconfig_get_readout_stack(
    mvlc_crateconfig_t *cfg, unsigned stackIndex);
mvlc_crateconfig_t *mvlc_crateconfig_set_readout_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack, unsigned stackIndex);

mvlc_stackbuilder_t *mvlc_crateconfig_get_trigger_io_stack(
    mvlc_crateconfig_t *cfg);
mvlc_crateconfig_t *mvlc_crateconfig_set_trigger_io_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack);

mvlc_stackbuilder_t *mvlc_crateconfig_get_vme_init_stack(
    mvlc_crateconfig_t *cfg);
mvlc_crateconfig_t *mvlc_crateconfig_set_vme_init_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack);

mvlc_stackbuilder_t *mvlc_crateconfig_get_vme_stop_stack(
    mvlc_crateconfig_t *cfg);
mvlc_crateconfig_t *mvlc_crateconfig_set_vme_stop_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack);

mvlc_stackbuilder_t *mvlc_crateconfig_get_mcst_daq_start_stack(
    mvlc_crateconfig_t *cfg);
mvlc_crateconfig_t *mvlc_crateconfig_set_mcst_daq_start_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack);

mvlc_stackbuilder_t *mvlc_crateconfig_get_mcst_daq_stop_stack(
    mvlc_crateconfig_t *cfg);
mvlc_crateconfig_t *mvlc_crateconfig_set_mcst_daq_stop_stack(
    mvlc_crateconfig_t *cfg, mvlc_stackbuilder_t *stack);

// MVLC controller from crateconfig
mvlc_ctrl_t *mvlc_ctrl_create_from_crateconfig(mvlc_crateconfig_t *cfg);

// Listfiles
// ---------------------------------------------------------------------

// Listfile write handle function. Must return the number of bytes written or a
// negative value in case of an error.
typedef ssize_t (*mvlc_listfile_write_handle_t) (void *userContext, const u8 *data, size_t size);

// Seek and read functions for listfiles. Must return a negative value on
// error.
typedef ssize_t (*mvlc_listfile_read_func) (void *userContext, const u8 *dest, size_t maxSize);
typedef ssize_t (*mvlc_listfile_seek_func) (void *userContext, size_t pos);

typedef struct mvlc_listfile_read_handle
{
    mvlc_listfile_read_func read_func;
    mvlc_listfile_seek_func seek_func;
} mvlc_listfile_read_handle_t;

// Listfile params
typedef enum
{
    ListfileCompression_LZ4,
    ListfileCompression_ZIP
} MVLC_ListfileCompression;

typedef struct mvlc_listfile_params
{
    bool writeListfile;
    const char *filepath;
    const char *listfilename;
    bool overwrite;
    MVLC_ListfileCompression compression;
    int compressionLevel;
} mvlc_listfile_params_t;

mvlc_listfile_params_t make_default_listfile_params();

// Readout data structures and parser callbacks
// ---------------------------------------------------------------------
typedef struct readout_datablock
{
    const u32 *data;    // pointer to the readout data
    u32 size;           // number of elements in the readout data
} readout_datablock_t;

typedef struct readout_moduledata
{
    readout_datablock_t data;
} readout_moduledata_t;

// Called for each readout event recorded by the DAQ.
typedef void (*rdo_event_data_callback)
    (void *userContext, int eventIndex, const readout_moduledata_t *moduleDataList, unsigned moduleCount);

// Called for each software generated system event.
typedef void (*rdo_system_event_callback)
    (void *userContext, const u32 *header, u32 size);

typedef struct readout_parser_callbacks
{
    rdo_event_data_callback event_data;
    rdo_system_event_callback system_event;
} readout_parser_callbacks_t;

// A readout object combining
// - mvlc
// - crateconfig
// - listfile write handle
// - readout parser callbacks
// ---------------------------------------------------------------------
typedef enum
{
    ReadoutState_Idle,
    ReadoutState_Starting,
    ReadoutState_Running,
    ReadoutState_Paused,
    ReadoutState_Stopping
} MVLC_ReadoutState;

typedef struct mvlc_readout mvlc_readout_t;

mvlc_readout_t *mvlc_readout_create(
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle_t lfh,
    readout_parser_callbacks_t event_callbacks,
    void *userContext);

mvlc_readout_t *mvlc_readout_create2(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle_t lfh,
    readout_parser_callbacks_t event_callbacks,
    void *userContext);

mvlc_readout_t *mvlc_readout_create3(
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_params_t listfileParams,
    readout_parser_callbacks_t event_callbacks,
    void *userContext);

mvlc_readout_t *mvlc_readout_create4(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_params_t listfileParams,
    readout_parser_callbacks_t event_callbacks,
    void *userContext);

void mvlc_readout_destroy(mvlc_readout_t *rdo);

mvlc_err_t mvlc_readout_start(mvlc_readout_t *rdo, int timeToRun_s);
mvlc_err_t mvlc_readout_stop(mvlc_readout_t *rdo);
mvlc_err_t mvlc_readout_pause(mvlc_readout_t *rdo);
mvlc_err_t mvlc_readout_resume(mvlc_readout_t *rdo);

MVLC_ReadoutState get_readout_state(const mvlc_readout_t *rdo);

// Replay similar to the readout object above.
// ---------------------------------------------------------------------
typedef struct mvlc_replay mvlc_replay_t;

mvlc_replay_t *mvlc_replay_create(
    const char *listfileFilename,
    readout_parser_callbacks_t event_callbacks,
    void *userContext);

mvlc_replay_t *mvlc_replay_create2(
    mvlc_listfile_read_handle_t lfh,
    readout_parser_callbacks_t event_callbacks,
    void *userContext);

void mvlc_replay_destroy(mvlc_replay_t *replay);

mvlc_err_t mvlc_replay_start(mvlc_replay_t *replay);
mvlc_err_t mvlc_replay_stop(mvlc_replay_t *replay);
mvlc_err_t mvlc_replay_pause(mvlc_replay_t *replay);
mvlc_err_t mvlc_replay_resume(mvlc_replay_t *replay);

MVLC_ReadoutState get_replay_state(const mvlc_replay_t *replay);

mvlc_crateconfig_t *mvlc_replay_get_crateconfig(const mvlc_replay_t *replay);

// "Blocking" data consumer API
// ---------------------------------------------------------------------
typedef enum
{
    MVLC_EventType_None,
    MVLC_EventType_Readout,
    MVLC_EventType_System
} event_type;

#define MVLC_MaxModulesPerEvent 20u

typedef struct readout_event
{
    int eventIndex;
    readout_moduledata_t moduleData[MVLC_MaxModulesPerEvent];
    unsigned moduleCount;
} readout_event_t;

typedef struct system_event
{
    const u32 *header;
    u32 size;
} system_event_t;

typedef struct event_container
{
    event_type type;
    readout_event_t readout;
    system_event_t system;
} event_container_t;

bool is_valid_event(const event_container_t *event);

typedef struct mvlc_blocking_readout mvlc_blocking_readout_t;
typedef struct mvlc_blocking_replay mvlc_blocking_replay_t;

event_container_t next_readout_event(mvlc_blocking_readout_t *r);
event_container_t next_replay_event(mvlc_blocking_replay_t *r);

mvlc_blocking_readout_t *mvlc_blocking_readout_create(
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle_t lfh);

mvlc_blocking_readout_t *mvlc_blocking_readout_create2(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_write_handle_t lfh);

mvlc_blocking_readout_t *mvlc_blocking_readout_create3(
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_params_t listfileParams);

mvlc_blocking_readout_t *mvlc_blocking_readout_create4(
    mvlc_ctrl_t *mvlc,
    mvlc_crateconfig_t *cfg,
    mvlc_listfile_params_t listfileParams);

mvlc_err_t mvlc_blocking_readout_start(mvlc_blocking_readout_t *r, int timeToRun_s);

void mvlc_blocking_readout_destroy(mvlc_blocking_readout_t *r);

mvlc_blocking_replay_t *mvlc_blocking_replay_create(
    const char *listfileFilename);

mvlc_blocking_replay_t *mvlc_blocking_replay_create2(
    const char *listfileArchiveName,
    const char *listfileArchiveMemberName);

mvlc_blocking_replay_t *mvlc_blocking_replay_create3(
    mvlc_listfile_read_handle_t lfh);

mvlc_err_t mvlc_blocking_replay_start(mvlc_blocking_replay_t *r);

void mvlc_blocking_replay_destroy(mvlc_blocking_replay_t *r);


#ifdef __cplusplus
}
#endif

/**\}*/

#endif /* __MESYTEC_MVLC_C_H__ */
