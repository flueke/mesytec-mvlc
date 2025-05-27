/**
 * @file BeginCommand.h
 * @brief Defines the Tcl command that starts a run (if possible).
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
#ifndef MVLC_BEGINCOMMAND_H
#define MVLC_BEGINCOMMAND_H
#include "ReadoutCommand.h"

#include <string>

/**
 * @class BeginCommand
 *     This class implements the Tcl command that begins a run.
 *   The command name is ```begin``` and does not take any parameters.
 *   See the implementation comments for operator().
 */
class BeginCommand : public ReadoutCommand {
private:
    std::string m_configFileName;
    // exported canonicals:
public:
    BeginCommand(
        CTCLInterpreter& interp, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout, 
        const std::string& configFilename
    );
    virtual ~BeginCommand();

    // Forbidden canonicals:
private:
    BeginCommand(const BeginCommand&);
    BeginCommand& operator=(const BeginCommand&);
    int operator==(const BeginCommand&);
    int operator!=(const BeginCommand&);

    // Interface methods:
public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
private:
    void setConfiguration();
};


#endif