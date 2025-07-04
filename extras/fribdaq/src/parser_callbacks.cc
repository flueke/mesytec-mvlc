/**
 * @file parser_callbacks.cc
 * @brief implementations of the event parser callbacks called in MVLCReadout objects.
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
#include "parser_callbacks.h"
#include <CRingBuffer.h>

// A note:  We could use the unified format library but then we'd need to 
// generate a ring item factory for a _specific_ FRIB/NSCLDAQ version but
// we know that what we want to make are ring items ::for the version of
// the software we were built against; so using that set is the best - I think.
// Note that CDataFormatItems only came into being in NSCLDAQ-11.0.

#include <CRingStateChangeItem.h>
#include <CRingScalerItem.h>
#include <CPhysicsEventItem.h>
#include <CRingPhysicsEventCountItem.h>
#include <CRingTextItem.h>
#include <CDataFormatItem.h>       // Requires NSCLDAQ-11.0 and higher.
#include <stdint.h>
#include <iostream>
#include <vector>
#include <time.h>
#include <chrono>



static const int STACK_EVENT(0);
static const int STACK_SCALER(1);
static bool bad_stack_warning_given(false);


////////////////////////////// private utilities ////////////////////////////////////////

/** get_milliseconds
 * 
 * @return unsigned int - the number of milliseconds into the run we are.
 */
static unsigned int get_milliseconds(FRIBDAQRunState* state) {
    auto now = state->s_timing.get_interval();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now);
    return ms.count();
}

/**
 * emit_statistics
 *    Emit a CPhysicsEventCountItem for the current statistics.
 * Note that if there's a timstamp extractor we use a timetamps of 0xffffffffffffffff
 * which asks the event builder to supply a timestamp.
 * 
 * @param context - pointer to the run context.  This supplies the ring buffer, the
 *      statistics, offset and all of the stuff we need.
 */
static void
emit_statistics(FRIBDAQRunState* context) {
    std::unique_ptr<CRingPhysicsEventCountItem> item;  // Ensure deletion.
    if (context->s_tsExtractor) {
        item.reset(new CRingPhysicsEventCountItem(
            0xffffffffffffffff, context->s_sourceid, 0,
            context->s_events, get_milliseconds(context), time(nullptr), context->s_divisor
        ));
    } else {
        item.reset(new CRingPhysicsEventCountItem(
            context->s_events, get_milliseconds(context), time(nullptr)
        ));
        item->setTimeDivisor(context->s_divisor);
    }
    std::lock_guard(context->s_serializer);
    item->commitToRing(*context->s_pRing);

}
/**
 *  submit_scaler
 * 
 *    Called to submit scaler data.  The assumption is that the payload for all
 * of the pModuleDataList are 32 bit wide scalers.  We get the run time offset from
 * the context.
 * 
 * @param context - the FRIBDAQRUnstate 
 * @param pData   -  Pointer to the module list
 * @param moduleCount - count of the number of modules in pData.
 */
static void
submit_scaler(
    FRIBDAQRunState* context, 
    const mesytec::mvlc::readout_parser::ModuleData* pModuleDataList,
    unsigned moduleCount
) {
    std::vector<uint32_t>  scalers;

    // Marshall the data into the scaler vector from the module data:


    for (int i = 0; i < moduleCount; i++) {
        auto data = pModuleDataList->data;

        // Size in u32s?

        for (int j = 0; j < data.size; j++) {
            scalers.push_back(data.data[j]);    // I'm to old fashioned to think of some fancy move.
        }

        pModuleDataList++;                   
                // Next module.
    }
    // For now use a source-id of zero.  We'll need to add that to the context and set it up from parameters:

    auto stop_time = get_milliseconds(context);
    CRingScalerItem item (
        0xffffffffffffffff, context->s_sourceid, 0,
        context->s_lastScalerStopTime, stop_time, time(nullptr), 
        scalers, context->s_divisor
    );
    std::lock_guard(context->s_serializer);
    item.commitToRing(*(context->s_pRing));

    // Start/stop book keeping>

    context->s_lastScalerStopTime = stop_time;    // Start of next interval


}
/**
 *  submit_event
 *     Called when a physics event has been received from the parser.
 *     The data from the modules are marshalled into CPhysicsEventItem which is
 *     submitted to the ringbuffer
 * 
 * @param context - Poitner to the FRIBDAQRunState.
 * @param pData   - Pointer to the module data
 * @param moduleCount - number of module data items to process.
 * 
 */
static void
submit_event(
    FRIBDAQRunState* context, const mesytec::mvlc::readout_parser::ModuleData* pModuleDataList,
    unsigned moduleCount
) {
    // Size the event:

    unsigned eventSize(0);                  // Will be in bytes:
    for (int i=0; i < moduleCount; i++) {
        eventSize += pModuleDataList[i].data.size * sizeof(uint32_t);
    }
    // Make the empty event and fill it.
    // The type of physics event we create will depend on s_tsExtractor in the context.
    // if not null, it wil be consulted to get  a timestamp and the
    // physics event  item will be created with a source id and a timestamp.
    //

    std::unique_ptr<CPhysicsEventItem> pEvent;
    if (context->s_tsExtractor) {
        pEvent.reset(new CPhysicsEventItem(
            context->s_tsExtractor(moduleCount, pModuleDataList), context->s_sourceid, 0,
            eventSize + 100
        ));
    } else {
        pEvent.reset(new CPhysicsEventItem(eventSize + 100));
    }
    CPhysicsEventItem& event(*pEvent);   

    for ( int i  = 0; i < moduleCount; i++) {
        uint32_t* pCursor = reinterpret_cast<uint32_t*>(event.getBodyCursor());
        auto size = pModuleDataList[i].data.size;
	if (size > 0) {		// In case memcpy is ill behaved for size==0.
	    memcpy(pCursor, pModuleDataList[i].data.data, size * sizeof(uint32_t));
	    pCursor += size;
	    event.setBodyCursor(pCursor);
	}
    }
    event.updateSize();
    std::lock_guard(context->s_serializer);
    event.commitToRing(*(context->s_pRing));

    // Update the statistics counters

    context->s_events++;                            // Update statistics.
    context->s_bytes += eventSize;
    context->s_cumulative_events++;
    context->s_cumulative_bytes += eventSize;

}


/**
 *  reset_statistics
 *    Called on a begin run to reset some statistics in the run state:Active
 * @param context - pointer to the FRIBDAQRunState item.
 */
static void
reset_statistics(  FRIBDAQRunState* context )  {
    context->s_events = 0;
    context->s_bytes = 0;
    context->s_lastScalerStopTime = 0;
    context->s_timing.start();
}
////////////////////////////// Public entries ///////////////////////////////////
/**
 * stack_callback
 *   What is done depends on the stack index.  If 1, dispatch to
 *   submit_event(static), if 2, dispatch to submit_scaler(static).
 *   For anything else emit a message and ignore the data.
 */
void
stack_callback(
    void* cd, 
    int crate, int stack, 
    const mesytec::mvlc::readout_parser::ModuleData* pModuleDataList,
    unsigned moduleCount
) {
    // cd is really a pointer to FRIBDAQRuntState:

    FRIBDAQRunState* context = reinterpret_cast<FRIBDAQRunState*>(cd);

    // Dispatch to the appropriate marshalling/submitting function:

    switch (stack) {
        case STACK_EVENT:
            submit_event(context, pModuleDataList, moduleCount);
            break;
        case STACK_SCALER:
            emit_statistics(context);
            submit_scaler(context, pModuleDataList, moduleCount);
            break;
        default:
            if (!bad_stack_warning_given) {
                std::cerr << "Unrecognized stack index: " << stack << std::endl;
                std::cerr << "The FRIB/NSCLDAQ parser call back only recognize: \n";
                std::cerr << "  " << STACK_EVENT << " - Physics trigger data\n";
                std::cerr << "  " << STACK_SCALER << " - Timed scaler readout\n";
                
                std::cerr << "Data from this stack will be ignored.  Check your crate configuration.\n";
                bad_stack_warning_given = true;      // Don't flook output/error.
            }
    }
}

/**
 *  Called on a system event.  Much of the stuff we need for this is in the context
 * struct.
 */
void system_event_callback(
    void* cd,
    int crateIndex, const mesytec::mvlc::u32* header, mesytec::mvlc::u32 size
) {
    FRIBDAQRunState* context = reinterpret_cast<FRIBDAQRunState*>(cd);

    // We only care about the run state transitions:
    uint16_t itemType;
    int barriertype(0);

    // Chose the right type of item:


    switch (mesytec::mvlc::system_event::extract_subtype(*header))
    {
        
        case mesytec::mvlc::system_event::subtype::BeginRun:
            itemType = BEGIN_RUN;
            reset_statistics(context);
            barriertype = 0;
            break;
        case mesytec::mvlc::system_event::subtype::EndRun:
            itemType= END_RUN;
            barriertype = 1;
            emit_statistics(context);
            break;
        case mesytec::mvlc::system_event::subtype::Pause:
            itemType = PAUSE_RUN;
            emit_statistics(context);
            break;
        case mesytec::mvlc::system_event::subtype::Resume:
            itemType = RESUME_RUN;  
            break;
        default:
            return;                                // Silently ignore all other types.
    }
    // Let's emit a format item prior to all of these...

    CDataFormatItem fmtItem;
    std::lock_guard(context->s_serializer);
    fmtItem.commitToRing(*context->s_pRing);

    // Note that the constructor for the state change item
    // only allows a divisor for most of NSCLDAQ if the contruction includes
    // a body header.
    
    CRingStateChangeItem item( 
        0xffffffffffffffff, context->s_sourceid, barriertype, 
        itemType, context->s_runNumber, get_milliseconds(context), 
        time(nullptr), context->s_runTitle, context->s_divisor);
    
    item.commitToRing(*context->s_pRing);
}

/**
 * dumpVariables
 * 
 *    Create and commit a MONITORED_VARIABLES CRingTextItem.
 *    a lock guard is used because this is likely called from the main thread
 * not the thread the normal parser callbacks runin.alignas
 * 
 * @param pState - context (has the ringbuffer and mutex)
 * @param strings - Strings to put in the ring item.
 *     
 */
void
dumpVariables(FRIBDAQRunState& state, const std::vector<std::string>& strings) {
    // Always put in a sid and dummy timestamp:

    CRingTextItem item(
        MONITORED_VARIABLES, 0xffffffffffffffff, state.s_sourceid, 0,
        strings, get_milliseconds(&state), time(nullptr), state.s_divisor
    );
    std::lock_guard(state.s_serializer);
    item.commitToRing(*state.s_pRing);



}