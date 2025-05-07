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
#include "ResumeCommand.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <tcl.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <chrono>
#include "parser_callbacks.h"
#include "StateUtils.h"

using mesytec::mvlc::MVLCReadout;


/**
 *  constructor
 *    @param interp  -- TCL Interpreter the command is registered on.
 *    @param pState   - POinter to the FRIBDAQ additional state struct.
 *    @param pReadout - POinter t othe MVLCReadout object doing the work.
 */
ResumeCommand::ResumeCommand(
    CTCLInterpreter& interp, 
    FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
) : ReadoutCommand(interp, "resume", pState, pReadout) {}

/**
 *  destructor
 */
ResumeCommand::~ResumeCommand() {}


/** 
 * operator()
 *     Actually perform the command:
 * 
 * ```
 *   Ensure there are no additional command line parameters.
 *   Ensure the state allows us to resume.
 *   Attempt the resume reporting any errors/failures.
 *   On success, update the state variables (Tcl and c++).
 * ```
 * @param interp - Interpreter running the command.
 * @param objv   - The command words.
 * @return int   - TCL_OK on success and TCL_ERROR on failure.
 */
int
ResumeCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() > 1) {
        interp.setResult("too many parameters for the 'resume' command.");
        return TCL_ERROR;
    }

    if (canResume(*m_pRunState)) {
        auto ec = m_pReadout->resume();
        if (ec) {
            interp.setResult(ec.message());
            return TCL_ERROR;
        } else {                             // Success.
            m_pRunState->s_runState = Active;
            setVar(interp, "state", "Active");
            
        }
    } else {
        interp.setResult("Run cannot be resumed when in this state.");
        return TCL_ERROR;
    }
    return TCL_OK;
}