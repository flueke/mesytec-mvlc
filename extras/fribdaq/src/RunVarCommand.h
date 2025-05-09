/**
 * @file RunVarCommand.h
 * @brief Defines the TCL class that manages ruvars.
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
#ifndef MVLC_RUNVARCOMMAND_H
#define MVLC_RUNVARCOMMAND_H
#include "ReadoutCommand.h"
#included <TCLTimer.h>        // Eventually we'll have a timer class to make/dump buffers.
#include <set>
#include<string>

struct FRIBDAQRunState;
namespace mesytec {
    namespace mvlc {
        class MVLCReadout;
    }
}
/**
 *  @class RunVarCommand
 * 
 *   The purpose of this class is to 
 *   1. Maintain a set of variables and
 *   2. Periodically dump those variables to the ringbuffer s CRingTextItems with type MONITORED_VARIABLES
 * 
 * @note The following varibles are pre-created:
 *      -  title  - title of the run.
 *      -  run    - run number.
 * 
 * @note creating a runvar also creates the Tcl variable with an empty string.  Deleting one does not
 * destroty the variable itself.
 */
class RunVarCommand : public ReadoutCommand {
    // Internal classes

    class Dumper : public CTCLTimer {
    private:
        RunVarCommand m_owner;
    public:
        Dumper(RunVarCommand& owner)
        virtual void operator():
    };
    // Class private data.
private:
    std::set<std::string> m_names;  // names of variables that are runvars.
    Dumper                m_dumper; // Periodic dump method.

    // exported canonicals:

public:
    RunVarCommand(
        CTCLInterpreter& interp,
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
    );
    virtual ~RunVarCommand();

    // forbidden canonicals:

private:
    RunVarCommand(const RunVarCommand&);
    RunVarCommand& operator=(const RunVarCommand&);
    int operator==(const RunVarCommand&);
    int operator!=(const RunVarCommand&);

    // Base class overrides:
public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // Utility methods:
private:
    void create(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void remove(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void list(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    std::string makeSet(CTCLInterpreter& interp, const char* varname);
};


#endif