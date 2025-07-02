/**
 * @file SlowControlsGetCommand.h
 * @brief Header for the slow controls "Get" command.
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
#ifndef MVLC_SLOWCONTROLSGETCOMMAND_H
#define MVLC_SLOWCONTROLSGETCOMMAND_H

#include <TCLObjectProcessor.h>


/**
 * @class   SlowControlsGetCommand
 * 
 *    This class provides the "Get" command to the slow controls server.
 *  The form of the command is:
 * 
 * ```tcl
 *   Get module-name parameter-name
 * ```
 * 
 * To retrieve the value of parameter-name from the driver for 
 * the hardware module named module-name.
 * 
 * The result of this command will, on success be the string from the
 * driver without interpretation or modification.  When used from
 * a server client, the result string is passed back to the client.
 * 
 */
class SlowControlsGetCommand : public CTCLObjectProcessor {
    // Exported canonicals:
public:
    SlowControlsGetCommand(CTCLInterpreter& interp);
    virtual ~SlowControlsGetCommand();

    // Forbidden canonicals:
private:
    SlowControlsGetCommand(const SlowControlsGetCommand&);
    SlowControlsGetCommand& operator=(const SlowControlsGetCommand&);
    int operator==(const SlowControlsGetCommand&);
    int operator!=(const SlowControlsGetCommand&);

public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};

#endif