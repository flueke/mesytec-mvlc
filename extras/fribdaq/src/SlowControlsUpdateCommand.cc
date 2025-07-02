/**
 * @file SlowControlsUpdateCommand.cc
 * @brief Implementation of the slow controls "Update" command.
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
#include "SlowControlsUpdateCommand.h"
#include "SlowControlsModuleCommand.h"
#include "SlowControlsDriver.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <sstream>

/** 
 * constructor
 *    @param interp - interpreter on which the command will be registered.
 */
SlowControlsUpdateCommand::SlowControlsUpdateCommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "Update", TCLPLUS::kfTRUE) {}

/**
 *  destructor
 */
SlowControlsUpdateCommand::~SlowControlsUpdateCommand() {}

/**
 * operator()
 *    Actually execute the command:
 * 
 *  - Validate the number of command words.
 *  - Find the driver and error if it does not exist.
 *  - Ask the driver to execute the command and set a result of OK.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - Command words.
 * @return int  - TCL_OK on success, TCL_ERROR on failure.
 */
int
SlowControlsUpdateCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 2, "ERROR - Update - incorrect number of command words");
    std::string moduleName = objv[1];

    auto pDriver = SlowControlsModuleIndex::getInstance()->findDriver(moduleName);
    if (!pDriver) {
        std::stringstream smsg;
        smsg << "ERROR - Update command; there is no module named: " << moduleName;
        std::string msg(smsg.str());

        interp.setResult(msg);
        return TCL_ERROR;
    }

    pDriver->Update();
    interp.setResult("OK");
    return TCL_OK;
}