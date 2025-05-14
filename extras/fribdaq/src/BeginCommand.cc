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
#include <tcl.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <chrono>
#include "parser_callbacks.h"
#include "StateUtils.h"
#include <stdexcept>
#include <sstream>

using mesytec::mvlc::MVLCReadout;

/**
 * constructor
 *    @param interp - encapsulated Tcl interpreter on which the command is registered.
 *    @param pState - Pointer to the extended DAQ state struct.
 *    @param pReadout - Pointer to the MVLC Readout object.
 *    @param configFilename - Path to the yaml config file.
 * 
 * @note we register as the ```begin``` command.
 */
BeginCommand::BeginCommand(
    CTCLInterpreter& interp, 
    FRIBDAQRunState* pState, MVLCReadout* pReadout,
    const std::string& configFilename
) :
    ReadoutCommand(interp, "begin", pState, pReadout),
    m_configFileName(configFilename)
     {}
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
        try {
            setConfiguration();                // Update the configuration.
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
            m_pRunState->s_runState = Active;
            setVar(interp, "state", "active");
            interp.setResult("");    // In case a variabl get set result.
        } catch (std::exception& e) {
            std::stringstream smsg;
            smsg << "Failed to start the run : " << e.what();
            std::string message(smsg.str());
            interp.setResult(message);
            return TCL_ERROR;
        }
    } else {
        // Begin with invalid state.

        interp.setResult("Run canot be started when in this state");
        return TCL_ERROR;
    }
    return TCL_OK;

}

///////////////////// private utils.

/**
 * setConfiguration
 *     Reprocess the configuration file and set the resulting
 * configuration in the readout:
 * 
 * @throw something derived from std::exception if processing the
 * configuration failed.
 */
void
BeginCommand::setConfiguration() {
    // Don't need to worry about scope since the confg is
    // copied inot the readout.

    mesytec::mvlc::CrateConfig config = 
         mesytec::mvlc::crate_config_from_yaml_file(m_configFileName);

    // If that parse failed, we don't get here -it throws std::runtime_error

    m_pReadout->setCrateConfig(config);
}
