/**
 * @file SlowControlsGetCommand.cc
 * @brief Implementation of the slow controls "Get" command.
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
#include "SlowControlsGetCommand.h"
#include "SlowControlsModuleCommand.h"
#include "SlowControlsDriver.h"

#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <sstream>

/**
 *  constructor
 *     @param interp - interpretr on which the "Set" command is registered.
 */
SlowControlsGetCommand::SlowControlsGetCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "Get", TCLPLUS::kfTRUE) {}

/**
 *  destructor
 */
SlowControlsGetCommand::~SlowControlsGetCommand() {
    
}

/**
 *  operator()
 *     - Ensure there are three command words.
 *     - Get the driver and return an error if that does not exist.
 *     - Get the result from the driver's Get method.
 * 
 * @param interp - interpreter executing the command.
 * @param objv   - the command words.
 * @return int   - TCL_ERROR on failure, TCL_OK on success.
 */
int
SlowControlsGetCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);
    requireExactly(objv, 3, "ERROR - Get command - incorrect number of parameters");

    std::string moduleName = objv[1];
    std::string paramName  = objv[2];

    auto pDriver = SlowControlsModuleIndex::getInstance()->findDriver(moduleName);
    if (!pDriver) {
        std::stringstream smsg;
        smsg << "ERROR - There is no slow controls module named: " << moduleName;
        std::string msg(smsg.str());
        interp.setResult(msg);
        return TCL_ERROR;
    }

    interp.setResult(pDriver->Get(paramName.c_str()));
    return TCL_OK;
}