/**
 * @file EndCommand.cc
 * @brief Implements the Tcl command that ends a run (if possible).
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

 #include "EndCommand.h"
 #include <mesytec-mvlc/mesytec-mvlc.h>
 #include "parser_callbacks.h"
 #include "StateUtils.h"
 #include <tcl.h>
 #include <TCLInterpreter.h>
 
 using mesytec::mvlc::MVLCReadout;

 /** constructor
  * @param interp - intepreter on which the command will be registered.
  * @param pState - pointer to the extra state held by the FRIB part of the daq.
  * @param pReadout - POinter to the MVLC readout object that does the real work.
  */
 EndCommand::EndCommand(CTCLInterpreter& interp, FRIBDAQRunState* pState, MVLCReadout* pReadout) :
    ReadoutCommand(interp, "end", pState, pReadout) {}

/** destructor */
EndCommand::~EndCommand() {}


/**
 *  operator() - does the actual work:
 * ```
 *    Ensure there's only one command word. If not error.
 *    Ensure the state allows the run to stop  If not error.
 *    Submit stop run to the Readout and report any errors that throws.
 *    
 * ```
 * @param interp - interpreter executing the command.
 * @param objv   - Command words, shoulid only have the 'end' text.
 * @return int  - TCL_OK if the command succeeded else TCL_ERROR.
 * 
 */
int
EndCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() != 1) {
        interp.setResult("Too many command parameters");
        return TCL_ERROR;
    }

    if (canEnd(*m_pRunState)) {
        auto ec =  m_pReadout->stop();
        if (ec) {                    // Readout rejected stop...
            interp.setResult(ec.message());
            return TCL_ERROR;
        } else {
            setVar(interp, "state", "idle");
            m_pRunState->s_runState = Halted;
        }
    } else {
        // invalid state to stop..

        interp.setResult("Run cannot be halted when in this state");
        return TCL_ERROR;
    }
    return TCL_OK;        // Success falls through to here.
}