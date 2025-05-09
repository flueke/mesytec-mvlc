/**
 * @file InitCommand.cc
 * @brief Implements the Tcl command that initializes crate hardware.
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
#include "InitCommand.h"
#include "StateUtils.h"
#include "parser_callbacks.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_stack_executor.h>
#include <TCLInterpreter.h>
#include <TCLObject.h>


using mesytec::mvlc::CommandExecOptions;

/** 
 * constructor
 *   @param interp - interpreter on which the command will be registered.
 *   @param pState - Pointer to the state structure (has what we need to execute commands).
 *   @param pReadout - Points to the readout object.
 * 
 * @note we register the command as "init"
 */

 InitCommand::InitCommand(
    CTCLInterpreter& interp, 
    FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
 ) :
    ReadoutCommand(interp, "init", pState, pReadout) {}

/**
 * destructor
 */
InitCommand::~InitCommand() {}

/**
 *  operator()
 *     Executes the command:
 * ```
 *     Ensure there are no additional parameters.
 *     Ensure the run is inactive (Halted or paused).
 *     Run the init section of the Config.
 *     Report errors.
 * ```
 * 
 *   @param interp - interpreter running the command.
 *   @param objv   - Commmand words.
 *   @return int - TCL_OK success, TCL_ERROR on errors.
 *   @retval TCL_OK - in this case, and non -normal results from the list execution are
 *      in the result as a list.
 */
int
InitCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() > 1) {
        interp.setResult("init -too many command line parameters");
        return TCL_ERROR;
    }

    if (canBegin(*m_pReadout, *m_pRunState) || canResume(*m_pRunState)) {
        CommandExecOptions runOpts = {
            ignoreDelays : false,
            continueOnVMEError  : true
        };
        auto statuses = mesytec::mvlc::run_commands(
            *m_pRunState->s_interface, 
            m_pRunState->s_config->initCommands,
            runOpts
        );
        CTCLObject resultList;
        resultList.Bind(interp);
        for (auto& status : statuses) {
            if (status.ec) {
                CTCLObject result;
                result.Bind(interp);
                result = status.ec.message();
                resultList += result;
            }
        }
        
        interp.setResult(resultList);
        
    }  else {
        interp.setResult("init - incorrect state to initialize the hardware");
        return TCL_ERROR;
    }
    return TCL_OK;
}
