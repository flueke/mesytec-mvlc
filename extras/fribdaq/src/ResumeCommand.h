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
#ifndef MVLC_RESUMECOMMAND_H
#define MVLC_RESUMECOMMAND_H

#include "ReadoutCommand.h"

/**
 *  @class ResumeCommand
 *     Class to register and implement the ```resume``` command.  The command
 * takes no additional parameters.  See the description of operator() in ResumeCommand.cc
 * for a description of the command itself.
 */
class ResumeCommand : public ReadoutCommand {
    // Exported canonicals:

    ResumeCommand(
        CTCLInterpreter& interp, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
    );
    virtual ~ResumeCommand();
    
    // forbidden canonicals:
private:
    ResumeCommand(const ResumeCommand&);
    ResumeCommand& operator=(const ResumeCommand&);
    int operator==(const ResumeCommand&);
    int operator!=(const ResumeCommand&);

    // Base class overrides:

public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};

#endif