/*
  @file SlowControlsTclDriver.h
  @brief Header for encapsulation of Tcl slow controls drivers.

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
#ifndef MVLC_SLOWCONTROLSTCLDRIVER_H
#define MVLC_SLOWCONTROLSTCLDRIVER_H

#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <string>
#include <vector>
#include <stdint.h>


namespace mesytec {
    namespace mvlc {
        class MVLC;
    }
}

class CTCLInterpreter;

/**
 * @class SlowControlsTclDriver
 *    This is a module that wraps Tcl slow controls drivers
 * so that we can import them from e.g. VMUSBReadout.alignas
 * Normally this is used in this way - in the ctlconfig.tcl:, suppose
 * you have a VMUSBReadout compatible slow controls driver in a package
 * named MyDriver which, provides a constructor named MyDriver that
 * creates the appropriate command ensemble with the required methods:
 * 
 * ```tcl
 * package require MyDriver
 * set driver [MyDriver %AUTO% <options>]
 * Module create tcl -ensemble $driver
 * ```
 * 
 * Is typically what you need to do where <options> are 
 * configuration options acceptable to MyDriver.  Note this example
 * assumes snit was used to create the MyDriver class (that's where
 * %AUTO% comes from to let Snit assign an unique name to the command
 * ensemble which is assigned to the variable driver).  itcl can also
 * be used to create ensembles in its own way.
 * 
 * Subcommands for the Tcl driver ensembles must be:
 * 
 * - Initialize - passed a controller ensemble 
 * - Update     - passed a controller ensemble.
 * - Set        - passed a controller ensemble, parameter name and value
 * - Get        - passed a controller ensemble, parameter name.
 * - AddMonitorList - passsed a readout list ensemble.
 * - processMonitorList - Passed the data lis the monitor list produced.
 *              The data are passed as a Tcl list of bytes in little endian
 *              order.
 * - getMonitorDta - return stringified monitor data from the data.
 */
class SlowControlsTclDriver : public SlowControlsDriver {
private:

    // canonical methods.
public:
    SlowControlsTclDriver(mesytec::mvlc::MVLC* controller);
    virtual ~SlowControlsTclDriver();

    // Disallowed canonicals:
private:
    SlowControlsTclDriver(const SlowControlsTclDriver&);
    SlowControlsTclDriver& operator=(const SlowControlsTclDriver&);
    int operator==(const SlowControlsTclDriver&);
    int operator!=(const SlowControlsTclDriver&);

    // Driver methods:
public:
    virtual void reconfigure();           // Trampoline to Initialize
    virtual void Update();                // Trampoline to Update.
    virtual std::string Set(
        const char* parameter, const char* value
    );                                    // Trampoline to Set
    virtual std::string Get(              // Trampoline to Get
        const char* parameter
    );
    virtual std::string getMonitor();     // AddMonitorList,
                                         //  processMonitorlist, getMonitorData
    // local utilities.

private:
    // Steps in the monitor data operations - Throw std::string for errors.
    void requestMonitorList(CTCLInterpreter& interp, std::string cmd);
    void requestProcessMonitorList(CTCLInterpreter& interp,  std::string cmd);
    std::string requestGetMonitorData(CTCLInterpreter& interp, std::string cmd);

    // Tiny utilities.
    std::string executeCommand(CTCLInterpreter& interp, std::string cmd);
    std::string getErrorInfo(CTCLInterpreter& interp);    
    std::pair<CTCLInterpreter*, std::string> getCommand();     
    std::string noEnsembleError(); 
                   
};
/**
 * @class TclDriverCreator
 *     Create a Tcl driver instance.
 */
class TclDriverCreator : public SlowControlsCreator {
public:
    SlowControlsDriver* create(mesytec::mvlc::MVLC* controller);
    class Register {
        public:
            Register();
    };
};
#endif
