/**
 * @file RunStateCommand.h
 * @brief Defines the command that returns the current run staste.
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
#ifndef MVLC_RUNSTATECOMMAND_H
#define MVLC_RUNSTATECOMMAND_H

#include "TCLObjectProcessor.h"

/**
 *  @class RunStateCommand
 * 
 *    This is provided to support the ReST command server. It provides read acces to the
 * 'state variable'.  Command format is just:  ```runstate``` no additional parameters.
 */
class RunStateCommand : public CTCLObjectProcessor {
    // Exported canonicals:

public:
    RunStateCommand(CTCLInterpreter& interp);
    virtual ~RunStateCommand();

    // Forbidden canonicals:

private:
    RunStateCommand(const RunStateCommand&);
    RunStateCommand& operator=(const RunStateCommand&);
    int operator==(const RunStateCommand&);
    int operator!=(const RunStateCommand&);

    // Base class method overrides:

public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

};

#endif