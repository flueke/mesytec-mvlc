/**
 * @file StatisticsCommand.cc
 * @brief Implements the Tcl command that returns run statistics.
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
#include "StatisticsCommand.h"
#include "parser_callbacks.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>


/**
 *  @constructor
 *     @param interp - interpreter to register the commadn on.
 *     @param pState - the run state struct.
 *     @param pReadout - The readouit object.
 */
StatisticsCommand::StatisticsCommand(
CTCLInterpreter& interp, 
FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
) : 
    ReadoutCommand(interp, "statistics", pState, pReadout) {}

    /** 
 * destructor
 */
StatisticsCommand::~StatisticsCommand() {}


/**
 * operator() 
 *    Implements the command:
 * ```
 *   Ensure there are no command parameters.
 *   Marshall the cumulative counters.
 *   Marshall the run counters.
 *   Set the result as [list cumulative run]
 * 
 * ```
 *    @param interp - interpreter running the command.
 *    @param objv   - The command words.
 *    @return int   - TCL_OK on success, or TCL_ERROR if not.
 * 
 * The only failure possible is  the user handing us some parameters.
 * 
 */
int
StatisticsCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    if (objv.size() > 1) {
        interp.setResult("statistics - too many command line parameters");
        return TCL_ERROR;
    }
    CTCLObject cumulative;
    CTCLObject run;

    MarshallStats(
        interp, cumulative, 
        m_pRunState->s_cumulative_events, m_pRunState->s_cumulative_events
    );
    MarshallStats(
        interp, run, m_pRunState->s_events, m_pRunState->s_bytes
    );

    CTCLObject result;
    result.Bind(interp);
    result += cumulative;
    result += run;

    interp.setResult(result);
    return TCL_OK;
}
/////////////////////////////////////////////// private utils. 

/**
 * MarshallStats
 *    Collect the statistics for triggers and bytes into a three eleme3nt list of
 *     triggers, accdepted trigggers and bytes.  Note that for the MVLC< triggers == accepted-triggers.
 * 
 * @param interp  - an interpreter that CTCLObjects can be bound into
 * @param[out] result - the object into which the list is made (not assumed to be bound).
 * @param triggers - number of triggers.
 * @param bytes   - number of bytes.
 */
void
StatisticsCommand::MarshallStats(
    CTCLInterpreter& interp, CTCLObject& result, 
    unsigned long triggers, unsigned long bytes
    
) {
    result.Bind(interp);

    CTCLObject trigs;
    trigs.Bind(interp);
    trigs = (double)(triggers);
    result += trigs;
    result += trigs;
    
    CTCLObject b;
    b.Bind(interp);
    b = (double)(bytes);
    result += b;
}