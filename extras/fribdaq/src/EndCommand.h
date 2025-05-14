/**
 * @file EndCommand.h
 * @brief Defines the Tcl command that ends a run (if possible).
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
#ifndef MVLC_ENDRUNCOMMAND_H
#define MVLC_ENDRUNCOMMAND_H

#include "ReadoutCommand.h"


/**
 *  @class EndCommand
 *      This class implements the Tcl command that ends a run
 * The command name is ```end``` and does not take any parameters.
 * For implementation details, see the operator() implementation comments.
 */
class EndCommand : public ReadoutCommand {
    // Exported canonicals:

public:
    EndCommand(
        CTCLInterpreter& interp, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout 
    );
    virtual ~EndCommand();

    // Canonicals that are not allowed.

private:
    EndCommand(const EndCommand& );
    EndCommand& operator=(const EndCommand);
    int operator==(const EndCommand&);
    int operator!=(const EndCommand&);

    // Virtual member overrides:

    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    
};
#endif