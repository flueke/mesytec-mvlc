/**
 * @file parser_callbacks.h
 * @brief interfaces for the event parser callbacks called in MVLCReadout objects.
 * @author Ron Fox <fox @ frib dot msu dot edu>
 * 
 *
 *   This software is Copyright by the Board of Trustees of Michigan
 *   State University (c) Copyright 2025.
 *
 *   You may use this software under the terms of the GNU public license
 *   (GPL).  The terms of this license are described at:
 *
 *    http://www.gnu.org/licenses/gpl.txt
 * 
 * @note - this file is private to fribdaq's readout and won't be installed.
 */

 #ifndef PARSER_CALLBACKS_H
 #define PARSER_CALLBACKS_H

 #include <mesytec-mvlc/mvlc_readout_parser.h>
 #include <mesytec-mvlc/util/stopwatch.h>
 #include <stdint.h>
 #include <mutex>
 #include <string>
 #include <vector>


 // Forward type definitions:

 class CRingBuffer;

 namespace mesytec {
    namespace mvlc {
        class MVLC;
        class MVLCReadout;
        struct CrateConfig;
    }
 }

 /**
 *  This block of stuff is passed around to the parsers to provide
 * run state information:
 */

 typedef enum {
    Active, Halted, Paused
} FRIBState;

/** 
 * This is a prototype for a timestamp extractor:  If provided, it 
 */ 

typedef uint64_t (*TimestampExtractor)(unsigned, const mesytec::mvlc::readout_parser::ModuleData*);


struct FRIBDAQRunState {
    std::mutex s_serializer;
    unsigned s_runNumber;
    std::string s_runTitle;
    FRIBState s_runState;
    CRingBuffer* s_pRing;
    // THe statistics get initialized by begin run state changes.
    unsigned     s_events;     // Number of accepted events this run.
    unsigned long    s_bytes;      // Event data bytes this run. COuld be TB.
    unsigned     s_cumulative_events; // total events over all time.  
    unsigned long    s_cumulative_bytes;  // total event bytes over all time. Couldb e TB
    mesytec::mvlc::util::Stopwatch     s_timing;    // Run offset.
    unsigned     s_lastScalerStopTime;              // ms.
    unsigned     s_divisor;    // Offset divisor. 
    int          s_sourceid;   // Source id for event built case. -1 if not.
    TimestampExtractor s_tsExtractor;
    mesytec::mvlc::MVLC*        s_interface;  // Pointer to the MVLC interface object.
    mesytec::mvlc::CrateConfig* s_config;     // Current configuration pointer.
    mesytec::mvlc::MVLCReadout* s_readout;    // The reaodut object.

    
FRIBDAQRunState() : 
    s_runNumber(0),            // If never set.
    s_runTitle("Change the title please"),
    s_runState(Halted), 
    s_pRing(nullptr),
    s_events(0), s_bytes(0), 
    s_cumulative_events(0), s_cumulative_bytes(0),
    s_divisor(1000),          // Timing in seconds.
    s_sourceid(0),
    s_tsExtractor(nullptr),
    s_interface(nullptr),
    s_config(nullptr)
    {}
    
};
 
/**
 *  This is called whenver stack data is processed by the parser.  We 
 * generatwe the appropriate ring item (see stack below) and insert it into
 * the ringbuffer.
 * @param cd - Actually a pointer to an FRIBDAQRunState object.
 * @param crate - Index of the VME crate being read out. We only support one for now.
 * @param stack - Index of the stack these data come from.  We support:
 *     - 1  - Data triggered by NIM input 1.  This is physics data.
 *     - 2  - Data triggered periodically.  This is scaler data.
 * @param pModuleDataList - pointer to the parsed data from the module.
 * @param moduleCount - number of parsed modules in the moduleDataList.
 * 
 */
void stack_callback(
    void* cd, 
    int crate, int stack, 
    const mesytec::mvlc::readout_parser::ModuleData* pModuleDataList,
    unsigned moduleCount
);

/**
 * This is called when a system event occurs.  We care about run transitions 
 * and then add the appropriate run transition ring item to the output ringbuffer.
 * 
 * @param cd - client data, this is actually a pointer to an FRIBDAQRunState object.
 * @param crateIndex - crate number from which this came, we only support one crate
 * @param header - pointer to data associated with the state transition.
 * @param size   - Size of the header.
 */
void system_event_callback(
    void* cd,
    int crateIndex, const mesytec::mvlc::u32* header, mesytec::mvlc::u32 size
);
/**
 * Create and submit a monitoredVariables item:
 * 
 * @param context - context pointer.
 * @param strings - Vector of strings to put in that item.
 */
void
dumpVariables(FRIBDAQRunState& pState, const std::vector<std::string>& strings);
#endif