/**
 * @file PauseCommand.h
 * @brief Defines the Tcl command that pauses a run (if possible).
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
#ifndef MVLC_PAUSECOMMAND_H
#define MVLC_PAUSECOMMAND_H
#include "ReadoutCommand.h"

/**
 * @class PauseCOmmand
 *    Provides the command to pause the ru8n.  The command name is ```pause```
 * and takes no additional parameters. 
 * 
 * See the description of the implementation of operator() in PauseCommand.cc
 * for more information.
 */
class PauseCommand : public ReadoutCommand {
    // Exported canonicals:
public:
    PauseCommand(
        CTCLInterpreter& interp, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
    );
    virtual ~PauseCommand();

    // Forbidden canonicals:

private:
    PauseCommand(const PauseCommand&);
    PauseCommand& operator=(const PauseCommand&);
    int operator==(const PauseCommand&);
    int operator!=(const PauseCommand&);

    // Base class method overrides:
public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};
#endif