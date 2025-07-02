/**
 * @file SlowControlsMonCommand.cc
 * @brief Implementation of the slow controls "Mon" command.
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
#include "SlowControlsMonCommand.h"
#include "SlowControlsModuleCommand.h"
#include "SlowControlsDriver.h"

#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <sstream>


/**
 * constructor
 *    @param interp - interpreter the 'mon' command will be registered on.
 */
SlowControlsMonCommand::SlowControlsMonCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "mon", TCLPLUS::kfTRUE) {}

/**
 * destructor
 */
SlowControlsMonCommand::~SlowControlsMonCommand() {}

/** 
 * operator()
 *   - Ensure there are the right number of parameters.
 *   - Find the driver.  Error if it's not found.
 *   - Set the result to the result of the driver's getMonitor method.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - Words of the command.
 * @return int   - TCL_OK on success, TCL_ERROR if not.
 */
int
SlowControlsMonCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
     bindAll(interp, objv);
     requireExactly(objv, 2, "ERROR - mon command incorrect number of parameters");

     std::string moduleName = objv[1];
     auto pDriver = SlowControlsModuleIndex::getInstance()->findDriver(moduleName);

     if (!pDriver) {
        std::stringstream smsg;
        smsg << "ERROR - mon there is no module named: " << moduleName;
        std::string msg(smsg.str());
        interp.setResult(msg);

        return TCL_ERROR;
     }

     interp.setResult(pDriver->getMonitor());

     return TCL_OK;
}