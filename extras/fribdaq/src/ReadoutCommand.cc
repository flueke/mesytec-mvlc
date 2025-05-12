/**
 * @file ReadoutCommand.cpp
 * @brief Implementation of  class for all Tcl command recongized by fribdaq-readout
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
#include "ReadoutCommand.h"
#include <TCLVariable.h>

using mesytec::mvlc::MVLCReadout;

/**
 *  constructor
 *     @param interp  - references the interpreter the command rusn on.
 *     @param command - command that triggers execution of this class's operator().
 *     @param pState  - pointer to the extended run state.
 *     @param pReadout - Pointer to the readout object.
 * 
 * We just initialize the base class and save the data our subclasses need.
 */
ReadoutCommand::ReadoutCommand(
    CTCLInterpreter& interp, const char* command, 
    FRIBDAQRunState* pState, MVLCReadout* pReadout
) :
    CTCLObjectProcessor(interp, command, TCLPLUS::kfTRUE),
    m_pRunState(pState), m_pReadout(pReadout)
{}

/**
 * destructor
 */
ReadoutCommand::~ReadoutCommand() {}


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
ReadoutCommand::getVar(CTCLInterpreter& interp, const char* name) {
    CTCLVariable var(name, TCLPLUS::kfFALSE);
    var.Bind(interp);
    return var.Get();
}
/**
 * setVar
 *    Set the value of a Tcl variable.
 * 
 * @param interp - inteprreter the variable is in.
 * @param name - name of the variable.
 * @param value  - New variable value.
 */
void
ReadoutCommand::setVar(CTCLInterpreter& interp, const char* name, const char* value) {
    CTCLVariable var(name, TCLPLUS::kfFALSE);
    var.Bind(interp);
    var.Set(nullptr, value);
}