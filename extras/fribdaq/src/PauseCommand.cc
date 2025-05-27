/**
 * @file PauseCommand.cc
 * @brief Implements the Tcl command that pauses a run (if possible).
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
#include "PauseCommand.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <tcl.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <chrono>
#include "parser_callbacks.h"
#include "StateUtils.h"


using mesytec::mvlc::MVLCReadout;    // I'm too lazy to do all that typing.

/**
 *  constructor
 *     @param interp   - Interpreter on which we wil register the 'pause' command.
 *     @param pState   - Additional FRIB DAQ State of the system (pointer).
 *     @param pReadout - Pointer to the readout object that does all the work.
 */
PauseCommand::PauseCommand(
    CTCLInterpreter& interp, 
    FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
) :
    ReadoutCommand(interp, "pause", pState, pReadout) {}

/**
 *  destructor
 */
PauseCommand::~PauseCommand() {}



/**
 * operator()
 *    Performs the command:
 * ```
 *    Ensure there are no additional command parameters.
 *    Ensure the state allows the run to be paused.
 *    Attempt to pause the run, reporting any variables.
 *    Update the state variables.
 * ```
 * 
 * @param interp - interpreter that is executing the command.
 * @param objv   - The command words.
 * @return int   - TCL_OK on success and TCL_ERROR on error.
 * 
 */
int
PauseCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() > 1) {
        interp.setResult("Too many command line parameters");
        return TCL_ERROR;
    }
    if (canPause(*m_pRunState)) {
        auto ec = m_pReadout->pause();
        if (ec) {
            interp.setResult(ec.message());
            return TCL_ERROR;                     // Readout would not pause.
        } else {
            setVar(interp, "state", "paused");
            m_pRunState->s_runState = Paused;
        }
    } else {
        // Wrong state I guess.

        interp.setResult("Run cannot be paused when in this state");
        return TCL_ERROR;
    }
    return TCL_OK;
}