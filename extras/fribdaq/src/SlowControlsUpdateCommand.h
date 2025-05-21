/**
 * @file SlowControlsUpdateCommand.h
 * @brief Header for the slow controls "Update" command.
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
#ifndef MVLC_SLOWCONTROLSUPDATECOMMAND_H
#define MVLC_SLOWCONTROLSUPDATECOMMAND_H
#include <TCLObjectProcessor.h>


/** 
 * @class SlowControlsUpdateCommand
 *    This implements a command that invokes the reconfigure method of a driver.
 * The Update operation is suppoed to send an internal state out to the physical device.
 * This can be necessary for devices that have write-only state.alignas
 * 
 * The form of this server request is:
 * 
 * ```tcl
 *    Update module-name
 * ```
 * 
 * Since the Update operation on the driver has nothing to return, success will return
 * "OK" as the result.
 */
class SlowControlsUpdateCommand : public CTCLObjectProcessor {
    // Canonicals that are exported:
public:
    SlowControlsUpdateCommand(CTCLInterpreter& interp);
    virtual ~SlowControlsUpdateCommand();

    // disallowed canonicals
private:
    SlowControlsUpdateCommand(const SlowControlsUpdateCommand&);
    SlowControlsUpdateCommand& operator=(const SlowControlsUpdateCommand&);
    int operator==(const SlowControlsUpdateCommand&);
    int operator!=(const SlowControlsUpdateCommand&);

    // operations:

public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};

#endif