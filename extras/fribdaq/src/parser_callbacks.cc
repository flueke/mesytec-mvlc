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
// we know that what we want to make are ring items for the version of
// the software we were built against; so using that set is the best - I think.
// Note that CDataFormatItems only came into being in NSCLDAQ-11.0.

#include <CRingStateChangeItem.h>
#include <CRingScalerItem.h>
#include <CPhysicsEventItem.h>
#include <CRingPhysicsEventCountItem.h>
#include <CDataFormatItem.h>       // Requires NSCLDAQ-11.0 and higher.
#include <stdint.h>
#include <iostream>
#include <vector>
#include <time.h>



static const int STACK_EVENT(1);
static const int STACK_SCALER(2);
static bool bad_stack_warning_given(false);

////////////////////////////// private utilities ////////////////////////////////////////
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

    CRingScalerItem item (
        0xffffffffffffffff, 0, 0,
        context->s_lastScalerStopTime, context->s_runtime, time(nullptr), 
        scalers, context->s_divisor
    );
    
    item.commitToRing(*(context->s_pRing));

    // Start/stop book keeping>

    context->s_lastScalerStopTime = context->s_runtime;    // Start of next interval


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

    CPhysicsEventItem event;
    for ( int i  = 0; i < moduleCount; i++) {
        uint32_t* pCursor = reinterpret_cast<uint32_t*>(event.getBodyCursor());
        auto size = pModuleDataList->data.size;
        memcpy(pCursor, pModuleDataList->data.data, size * sizeof(uint32_t));
        pCursor += size;
        event.setBodyCursor(pCursor);
    }
    event.updateSize();

    event.commitToRing(*(context->s_pRing));
    context->s_events++;                            // Update statistics.

}


/**
 *  reset_statistics
 *    Called on a begin run to reset some statistics in the run state:Active
 * @param context - pointer to the FRIBDAQRunState item.
 */
static void
reset_statistics(  FRIBDAQRunState* context )  {
    context->s_events = 0;
    context->s_lastScalerStopTime = 0;
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

    // Chose the right type of item:


    switch (mesytec::mvlc::system_event::extract_subtype(*header))
    {
        case mesytec::mvlc::system_event::subtype::BeginRun:
            itemType = BEGIN_RUN;
            reset_statistics(context);
            break;
        case mesytec::mvlc::system_event::subtype::EndRun:
            itemType= END_RUN;
            break;
        case mesytec::mvlc::system_event::subtype::Pause:
            itemType = PAUSE_RUN;
            break;
        case mesytec::mvlc::system_event::subtype::Resume:
            itemType = RESUME_RUN;  
            break;
        default:
            return;                                // Silently ignore all other types.
    }
    // Let's emit a format item prior to all of these...

    CDataFormatItem fmtItem;
    fmtItem.commitToRing(*context->s_pRing);

    CRingStateChangeItem item(
        0xffffffff, 0, 0, 
        itemType, context->s_runNumber, context->s_runtime, time(nullptr), context->s_runTitle, context->s_divisor
    );
    item.commitToRing(*context->s_pRing);
}