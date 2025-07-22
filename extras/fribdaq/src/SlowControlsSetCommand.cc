/**
 * @file SlowControlsSetCommand.cc
 * @brief Implementation of the slow controls server Set command.
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
#include "SlowControlsSetCommand.h"
#include "SlowControlsModuleCommand.h"    // For the module dir.
#include "SlowControlsDriver.h"

#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <sstream>


/**
 * constructor:
 * 
 * @param interp - references the interpreter this command is registered on.
 * 
 */
SlowControlsSetCommand::SlowControlsSetCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "Set", TCLPLUS::kfTRUE) 
{}

/**
 *  destructor
 */
SlowControlsSetCommand::~SlowControlsSetCommand() {}



/**
 *  operator()
 *     - Require 4 parameters exactly.
 *     - Find the driver module, and complain if it does not exist.
 *     - Invoke the modules Set command with the appropriate parameters,
 *     setting the result with what it kicks back.
 * 
 * @param interp - interpreter executing the command.
 * @param objv   - encapsulated command parameters.
 * @return int   - TCL_ERROR on failure, TCL_OK on success.
 */
int
SlowControlsSetCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);
    requireExactly(objv, 4, "ERROR Set command - incorrect number of command parameters");

    std::string moduleName = objv[1];
    std::string paramName  = objv[2];
    std::string value      = objv[3];

    auto pDriver = SlowControlsModuleIndex::getInstance()->findDriver(moduleName);
    if (!pDriver) {
        std::stringstream sResult;
        sResult << "ERROR - There is no slow controls module named: " << moduleName;
        std::string result = sResult.str();

        interp.setResult(result);
        return TCL_ERROR;
    }

    interp.setResult(pDriver->Set(paramName.c_str(), value.c_str()));

    return TCL_OK;
    
}