/**
 * @file InitCommand.h
 * @brief Defines the Tcl command that initializes crate hardware.
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

#ifndef MVLC_INITCOMMAND_H
#define MVLC_INITCOMMAND_H
#include "ReadoutCommand.h"

struct FRIBDAQRunState;

namespace mesytec {
    namespace mvlc {
        class MVLCReadout;
    }
}
/**
 * @class InitCommand
 * 
 * 
 * This command, which takes no parameters, initializes the readout hardware specified
 * by the configuration in the extended state.
 */
class InitCommand : public ReadoutCommand {
    // Exported canonicals:

public:
    InitCommand(
        CTCLInterpreter& interp, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
    );
    virtual ~InitCommand();

    // forbidden canonicals:

private:
    InitCommand(const InitCommand&);
    InitCommand& operator=(const InitCommand&);
    int operator==(const InitCommand&);
    int operator!=(const InitCommand&);

    // Base class overrides.

public:
    operator()(CTCLInterpreter& interp, std::vector<CTCLObject>* objv);
};

#endif