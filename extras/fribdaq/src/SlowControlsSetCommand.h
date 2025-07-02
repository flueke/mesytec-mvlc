/**
 * @file SlowControlsSetCommand.h
 * @brief Header for the slow controls server Set command.
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
#ifndef MVLC_SLOWCONTROLSSETCOMMAND_H
#define MVLC_SLOWCONTROLSSETCOMMAND_H

#include <TCLObjectProcessor.h>

/**
 *  @class SlowControlsSetCommand
 * 
 * The Set command (capitalized S to avoid the Tcl set built in), tells a
 * driver to set a device parameter.    The form of the command is:
 * 
 * ```tcl
 * Set module-name parameter-name value
 * ```
 * 
 * The interpretation of the parameter name and value are entirely up to the driver
 * associated with the named module.
 */
class SlowControlsSetCommand : public CTCLObjectProcessor {
    // exported canonicals:
public:
    SlowControlsSetCommand(CTCLInterpreter& interp);
    virtual ~SlowControlsSetCommand();

    // forbidden canonicals:
private:
    SlowControlsSetCommand(const SlowControlsSetCommand&);
    SlowControlsSetCommand& operator=(const SlowControlsSetCommand &);
    int operator==(const SlowControlsSetCommand&);
    int operator!=(const SlowControlsSetCommand&);

public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};

#endif