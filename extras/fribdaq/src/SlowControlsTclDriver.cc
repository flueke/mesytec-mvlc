/*
  @file SlowControlsTclDriver.cc
  @brief Implementation of encapsulation of Tcl slow controls drivers.

    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2005.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

#include "SlowControlsTclDriver.h"
#include <TCLObject.h>
#include <TCLInterpreter.h>
#include <Exception.h>
#include <TCLVariable.h>
#include <XXUSBConfigurableObject.h>

#include "TclServer.h"
#include <tcl.h>
#include <sstream>
#include <iostream>

/////////////////// Implementation of the driver.
/**
 * constructor
 */
SlowControlsTclDriver::SlowControlsTclDriver(mesytec::mvlc::MVLC* controller) :
    SlowControlsDriver(controller)
{}

/**
 * destructor
 */
SlowControlsTclDriver::~SlowControlsTclDriver() {}

/**
 *  reconfigure
 *    Called when the configuration has been updated.
 * We pull out the -ensmble config option.  If the
 * command exists we invoke it's Initialize subcommand, 
 * passing 'vmusb' as the command parameter.
 */
void
SlowControlsTclDriver::reconfigure() {
    auto info = getCommand();
    CTCLInterpreter* pInterp = info.first;
    auto command    = info.second;
    if (command != "") {
        CTCLObject fullCommand;
        fullCommand.Bind(pInterp);
        fullCommand += command;
        fullCommand += std::string("Initialize");
        fullCommand += std::string("vmusb");

        executeCommand(*pInterp, std::string(fullCommand));
    }
        
}
/**
 * Update
 *    Update the device from any internal shadow configuration.
 *    This invokes the Update method of the Tcl driver handing it
 *    the base command name of the vmusb command.
 */
void
SlowControlsTclDriver::Update() {
    auto info = getCommand();
    if(info.second != "") {
        CTCLObject fullCommand;
        fullCommand.Bind(info.first);
        fullCommand = info.second;
        fullCommand += "Update";
        fullCommand += "vmusb";
        executeCommand(*info.first, std::string(fullCommand));
    }
}
/**
 * Set
 *    Set a parameter.  In this case, we trampoline to the Set method of the Tcl driver,
 * we pass the vmusb command prefix, the parameter name and the parameter value.
 * 
 * @param parameter - name of the parameter the client wants to set.
 * @param value     - Value they want to set it to.
 * @return std::string - On successful execution of the Tcl script, we just return the result string.
 *    If the script throws an error, we return a string that consits of ERROR - Tcl Driver <basecommand> Failed Set command.
 *    in that case, stderr also gets that followed by the result and value of ErrorInfo to help the driver
 *    writer debug.  We don't send that back to the client because it will likely have \n's in it that will
 *    confuse client software...in any event well behaved drivers shoulid not do that.  
 */
std::string
SlowControlsTclDriver::Set(const char* parameter, const char* value) {
    auto info = getCommand();
    if (info.second != "") {
        CTCLObject fullCommand;
        fullCommand.Bind(info.first);
        fullCommand += info.second;               // Base command.
        fullCommand += "Set";                     // Subcommand.
        fullCommand += "vmusb";                   // VME command ensemble
        fullCommand += std::string(parameter);
        fullCommand += std::string(value);

        return executeCommand(*info.first, std::string(fullCommand));
    }
    return noEnsembleError();                // -ensemble not configured.
}
/**
 * Get
 *     Get the value of a parameter.  We trampoline to the Get method of the Tcl driver.
 * We pass the vme command prefix and parameter name.alignas
 * 
 * @param parameter - name of the parameter to be retrieved.
 * @return std::string - whatever the driver or executCommand gave us...executeCommand may
 *     output stuff to stderr - see Set's description of its return value.
 * 
 */
std::string
SlowControlsTclDriver::Get(const char* parameter) {
    auto info = getCommand();
    if (info.second != "") {
        CTCLObject fullCommand;
        fullCommand.Bind(info.first);
        fullCommand += info.second;                  // Driver base command.
        fullCommand += "Get";                        // Driver subcommand.
        fullCommand += "vmusb";                      // VME access subcommand.
        fullCommand += std::string(parameter);
        return executeCommand(*info.first, std::string(fullCommand));
    }
    return noEnsembleError();                // -ensemble not configured.
}
/**
 * getMonitor
 *     This is the most complex of the operations.  The Tcl driver for VMUSB expected to provide
 * a contribution to the VMUSBReadoutList that was periodically executed...on each list execution,
 * it was expected to distribute the data read by that list into some internal storage which was then 
 * retrieved by the client with getMonitoredData.  Since we don't have a problem executing
 * operations when the MVLC is in data taking, getMonitor will:
 *    - Invoke the Tcl driver's  addMonitorList with a vmusbreadoutlist ensemble.
 *    - Execute that list.
 *    - Pass the data (appropriately cooked) to the Tcl driver's processMonitorList
 *    - Invoke the Tcl drivers's 'getMonitoredData and return that to the caller/client.
 * 
 * 
 * @return std::string containing the monitored data as returned from the Tcl driver's getMonitordData
 * 
 */
std::string
SlowControlsTclDriver::getMonitor() {
    auto info = getCommand();
    CTCLInterpreter& interp(*info.first);
    std::string baseCommand = info.second;

    if (baseCommand != "") {
        try {
            requestMonitorList(interp, baseCommand);
            if (monitorListSize(interp) > 0) {
                requestProcessMonitorList(interp, baseCommand);
                return requestGetMonitorData(interp, baseCommand);
            } else {
                return "OK";
            }

        }
        catch (std::string msg) {
            std::stringstream smsg;
            smsg << "ERROR - Getting monitored data from " << baseCommand << " " << msg;
            std::string emsg(smsg.str());
            return emsg;
        }
    }
    return noEnsembleError();
    
 }
 ////////////////////////////////// Utilities for the monitor list:  /////////////////////////////

 /**
  * requestMonitorList
  *    Invokes the addMonitorList operation of the Tcl driver.  We just let vmusbreadoutlist
  * accumulate the operations list.  It's not going to be executed until requestProcessMonitorList
  * is invoked.  If there's an error in execution; detected by executeCommand, returning a string
  * beginning with "ERROR", we throw the value retunred from executeCommand as a sttring exception.
  * In that case, we reset the monitor list prior to throwing the exception.
  * 
  * 
  * @param interp - references the interpreter that will run the command(s).
  * @param cmd    - The command of the Tcl driver... assured to be non-empty.
  */
 void
 SlowControlsTclDriver::requestMonitorList(CTCLInterpreter& interp, std::string cmd) {
    CTCLObject fullcmd;
    fullcmd.Bind(interp);
    fullcmd = cmd;
    fullcmd += "addMonitorList";
    fullcmd += "vmusbReadoutList";     // the list manager.

    std::string scriptResult = executeCommand(interp, std::string(fullcmd));
    if (scriptResult.substr(0, 5) == "ERROR") {             // Error of some sort.
        throw scriptResult;
    }

 }

 /**
  * requestProcessMonitorList
  *    - Execute the monitorList.
  *    - Marshall the data it returned into a list of bytes
  *    - Invoke the Tcl driver's processMonitorList passing the data list.
  *    - If executeCommand returned a string like "ERROR", throw that.
  * 
  * @param interp - interpreter executing commands.
  * @param command- base command of he Tcl driver.
  */
 void
 SlowControlsTclDriver::requestProcessMonitorList(
    CTCLInterpreter& interp, std::string command
) {
    // Execut the list and get the raw data back.

    std::string rawData = executeCommand(interp, "vmusbreadoutlist execute");
    if (rawData.substr(0,5) == "ERROR") {
        throw rawData;
    }
    // Now convert the data to a byte list:

    CTCLObject cmd;
    cmd.Bind(interp);
    cmd = "vmusbreadoutlist";
    cmd += "tobytes";
    cmd += rawData;
    std::string bytes = executeCommand(interp, std::string(cmd));
    if (bytes.substr(0,5) == "ERROR") {
        throw bytes;
    }
    // Clear the monitor list for next time around:

    executeCommand(interp, "vmusbreadoutlist clear");             // "cant' fail".

    // Now we can get the  driver to process the data:

    cmd = command;
    cmd += "processMonitorList";
    cmd += bytes;
    std::string result = executeCommand(interp, std::string(cmd));
    if (result.substr(0, 5) == "ERROR") {
        throw result;
    }
 }
 /**
  * requestgetMonitorData
  *    Run the getMonitorData method of the Tcl driver and return
  * its response.   In this case, if there is an error we don't throw,
  * the caller is just going to relay our string to the caller/client.
  * 
  * @param interp  - interpreter on which to run the driver.,
  * @param cmd     - Tcl Driver base command.
  * @return std::string - result of running $cmd getMonitoedData
  */
 std::string
 SlowControlsTclDriver::requestGetMonitorData(CTCLInterpreter& interp, std::string cmd) {
    CTCLObject fullCommand;
    fullCommand.Bind(interp);
    fullCommand = cmd;
    fullCommand += "getMonitoredData";

    return executeCommand(interp, std::string(fullCommand));
 }
 /**
  * monitorListSize
  *     @param interp - interpreter to run the vmusbreadoutlist command on.
  *     @return int - size of current monitor list.
  */
 int
 SlowControlsTclDriver::monitorListSize(CTCLInterpreter& interp) {

    std::string strsize = executeCommand(interp,"vmusbreadoutlist size");
    std::stringstream s(strsize);
    int result;
    s >> result;

    return result;
 }

 /**
  * executeCommand
  * 
  *     Execute a Tcl command.  
  * @param interp - interpreter in which to run the command.
  * @param cmd    - The command string to run.
  * @return std::string - result of the command. See the note below.
  * @note If the command is successful, we grab the result and
  *    return it.  IF not:  We grab the result, which is an error messages string,
  *    prefix it with "ERROR - "".  Output to stderr that string followed
  * by the value of the errorInfo variable (traceback) and return the 
  * error string.  The idea is that drivers should detect and return
  * error strings and should  _not_ give error results.  Therefore, stderr
  * output can be used by the driver writer to troubleshoot/debug 
  * their driver.
  */
 std::string
 SlowControlsTclDriver::executeCommand(CTCLInterpreter& interp, std::string cmd) {
    if (cmd == "") {
        return noEnsembleError();      // -ensemble was not configured!
    }
    try {
        interp.GlobalEval(cmd);
        return std::string(Tcl_GetStringResult(interp.getInterpreter()));
    }
    catch (CException& e) {
        // The return value:
        std::string result = "ERROR - ";
        result += e.ReasonText();

        // Construct the output:

        std::stringstream smsg;
        smsg << result;
        smsg << std::endl << getErrorInfo(interp) << std::endl;
        std::cerr << "Error executing Tcl command: " << cmd << std::endl;
        std::cerr << result << std::endl << smsg.str() << std::endl;

        return result;
    }
 }
 /**
  * getErrorInfo
  *    If the errorInfo variable exists, we return its contents, 
  * normally an error traceback string.  If not we return the string
  * "No Traceback available".
  * 
  * @param interp - interpreter whose errorInfo var we'll return.
  */
 std::string
 SlowControlsTclDriver::getErrorInfo(CTCLInterpreter& interp) {
    CTCLVariable traceback(&interp, "errorInfo", TCLPLUS::kfFALSE);
    const char *t = traceback.Get();
    if (t) {
        return std::string(t);
    } else {
        return std::string("No Traceback available");
    }
 }
 /**
  * getCommand
  *    Return a pair consisting of the interpreter that runs the
  * slow controls server and our ensemble command.
  * 
  * @return std::pair<CTCLInterpreter*, std::string>
  */
 std::pair<CTCLInterpreter*, std::string>
 SlowControlsTclDriver::getCommand() {
    auto command = getConfiguration()->cget("-ensemble");
    auto interp  = ControlServer::getInstance()->getInterpreter();
    return std::make_pair(interp, command);
 }
 /**
  * noEnsembleError
  * 
  * @return std::string - message appropriate for -ensemble not configured.
  *   
  */
 std::string
 SlowControlsTclDriver::noEnsembleError() {
    return std::string("To use a Tcl slow controsl driver you must configure -ensembl");
 }
 ////////////////////////////////// TclDriverCreator and registration.

 /**
  * create - creat an instance of a wrapped Tcl driver.  The actual Tcl driver gets configured with
  *         the -ensemble option.
  * @param controller - the controller reference, actually unused.  We rely on the Tcl wrappings.
  */
 SlowControlsDriver*
 TclDriverCreator::create(mesytec::mvlc::MVLC* controller) {
   SlowControlsDriver* pDriver = new SlowControlsTclDriver(controller);
   pDriver->getConfiguration()->addParameter("-ensemble", nullptr, nullptr, "");
   return pDriver;
 }

 /**
  * Register::Register
  *     Register the driver wrapper  as type 'tcl'.
  */
 TclDriverCreator::Register::Register() {
    SlowControlsFactory::getInstance()->addCreator("tcl", new TclDriverCreator);
 }


 // tacky little trick to register the driver:

 static TclDriverCreator::Register registrar;   // Registers the driver creator with the factory singleton.
