/**
 * @file BeginCommand.cc
 * @brief Implements the  Tcl command that starts a run (if possible).
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
#include "BeginCommand.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <TCLVariable.h>
#include <tcl.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <chrono>
#include "parser_callbacks.h"
#include "StateUtils.h"

using mesytec::mvlc::MVLCReadout;

/**
 * constructor
 *    @param interp - encapsulated Tcl interpreter on which the command is registered.
 *    @param pState - Pointer to the extended DAQ state struct.
 *    @param pReadout - Pointer to the MVLC Readout object.
 * 
 * @note we register as the ```begin``` command.
 */
BeginCommand::BeginCommand(CTCLInterpreter& interp, FRIBDAQRunState* pState, MVLCReadout* pReadout) :
    ReadoutCommand(interp, "begin", pState, pReadout) {}
/**
 *  destructor
 *     provide a chain point.
 */
BeginCommand::~BeginCommand() {}


/**
 *  operator()
 *     Executes the actual command.
 *     Pseudo code:
 * ```
 *   If there is more than 1 command word, report an error.
 *   if a begin is allowed:
 *       If a title variable exists, copy it's value to the state's title.
 *       If a run variable exists and is an integer, copy its value to the state's run number.
 *       Start the run in the readout object.
 *       Set the 'state' variable to "Active"
 *       return TCL_OK
 *    else:
 *       result <-- Run cannot be started at this time.
 * 
 * @param interp - the interpreter the command is runnign on.
 * @param objv   - the encapsulated command words.
 * @return int   - TCL_OK on success, TCL_ERROR on failure.
 */
int
BeginCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() > 1) {
        interp.setResult("Too many command parameters");
        return TCL_ERROR;
    }
    if (canBegin(*m_pReadout, *m_pRunState)) {
    
        // Set title and runn um if can:

        auto titlesz = getVar(interp, "title");
        if(titlesz) m_pRunState->s_runTitle = titlesz;
        auto runsz   = getVar(interp, "run");    // Might not convert to int so:
        try {
            if (runsz) {
                CTCLObject run;
                run.Bind(interp);
                run = runsz;
                m_pRunState->s_runNumber = (int)run;     // Converts object -> int rep if possible.
            }
            

        }
        catch(...) {
            interp.setResult("***warning*** run number does not convert to an integer");
        }                            // non fatal exception.

        // Try to start the run:

        auto ec = m_pReadout->start(std::chrono::seconds(0));              // no limit to runtime.
        if (ec) {
            interp.setResult(ec.message());
            return TCL_ERROR;                                      // Readout object failed to start run.
        }
        interp.setResult("");    // In case a variabl get set result.
    } else {
        // Begin with invalid state.

        interp.setResult("Run canot be started at this time");
        return TCL_ERROR;
    }
    return TCL_OK;

}


/////////////////////////////////// Private utils ////////////////////////////////////////


/**
 * getVar
 *    Get the value of a global variable in the interpreter.
 * 
 * @param interp - references the interp holding the var.
 * @param name   - name of the variable.
 * @return const char* - Pointer to the string repreentation of the variable value.
 * @retval nullptr - if there is no such variable.
 */
const char*
BeginCommand::getVar(CTCLInterpreter& interp, const char* name) {
    CTCLVariable var(name, TCLPLUS::kfFALSE);
    var.Bind(interp);
    return var.Get();
}
