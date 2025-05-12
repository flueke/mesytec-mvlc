/**
 * @file RunStateCommand.cc
 * @brief implements the command that returns the current run staste.
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
#include "RunStateCommand.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <TCLVariable.h>


/**
 *  constructor
 * 
 * @param interp - interpreter on which the 'runstate' command will be registered.
 */

 RunStateCommand::RunStateCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "runstate", TCLPLUS::kfTRUE) {}

/**
 * destructor
 */
RunStateCommand::~RunStateCommand() {}


/** 
 *  operator() 
 *    Note if the 'state' variable is not defined, then "-undefined-" is returned.
 * 
 * @param interp - interpreter that's running the command.
 * @param objv   - COmmand parameters.
 * @return int - TCL_OK
 */
int 
RunStateCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() != 1) {
        interp.setResult("Too many command parameters");
    }
    CTCLVariable state("state", TCLPLUS::kfFALSE);
    state.Bind(interp);

    const char* value = state.Get();
    if (value) {
        interp.setResult(value);
    } else {
        interp.setResult("-undefined-");
    }

    return TCL_OK;
}