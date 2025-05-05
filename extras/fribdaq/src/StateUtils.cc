/**
 * @file StateUtils.cc
 * @brief Implement functions that are useful for all state change commands.
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
 */

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "parser_callbacks.h"


 /** 
  * canBegin
 * We can begine a run if the following conditions obtain:
 * - The state in FRIBDAQRunState is Halted 
 * - the Readout is finished - that is the workers have all
 * rundown.
 *   @param rdo - the readout object.
 *   @param ExtraRunState - the run state struct.
 *   @return bool - true if the run can be begun.
 */
bool 
canBegin(mesytec::mvlc::MVLCReadout& rdo, FRIBDAQRunState& ExtraRunState) {
    return (
        ExtraRunState.s_runState == Halted &&
        rdo.finished()
    ); 
}

/**
 * canEnd
 *   We can end a run if the following conditions hold
 *    - We are paused and the readout is finished.
 *    - We are Active.
 * @param ExtraRunState -The extra state for the run:
 * @return bool - true if it's legal to end the run.
 */
bool
canEnd(FRIBDAQRunState& ExtraRunState) {
    return (
        (ExtraRunState.s_runState == Active) ||
        (ExtraRunState.s_runState == Paused)
    );
}

/**
 * canPause
 *     Determine if it is legal to pause the run.
 * The run must be active for this to be legal:
 * @param ExtraRunState - state of the run.
 * @return bool - true if the run can be paused.
 */

bool
canPause(FRIBDAQRunState& ExtraRunState) {
    return ExtraRunState.s_runState == Active;
}



/**
 * canResume
 *    Determine if resuming a run is legal.
 * This is the case if we are Paused and finished.
 * 
 * @param ExtraRunState - the run state struct.
 * @return bool -True if legal.
 */

 bool
 canResume(FRIBDAQRunState& ExtraRunState) {
     return 
         (ExtraRunState.s_runState == Paused);
     
 }