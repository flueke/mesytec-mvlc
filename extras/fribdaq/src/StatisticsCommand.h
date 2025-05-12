/**
 * @file StatisticsCommand.h
 * @brief Defines the Tcl command that returns run statistics.
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
#ifndef MVLC_STATISTICSCOMMAND_H
#define MVLC_STATISTICSCOMMAND_H

#include "ReadoutCommand.h"


/**
 *  @class StatisticsCommand
 * 
 *  Implements the 'statistics' command.  This command takes no parameters.  It 
 * returns a two elemnt list.  The elements of the first list are cumulative counters
 * (over all runs).  The elements of the second list are counters for the current (or last run).
 * Each sub-list has, in order
 *   *   Number of triggers
 *   *   Number of accepted triggers - same as above for mvlc readout.
 *   *   Number of bytes of event data (this is exclusive of e.g. ring item wrapping and body headers).
 * 
 * @note statistics are maintained by the parser_callbacks.cc  file.
 */
class StatisticsCommand  : public ReadoutCommand {
    // Exported canonicals:
public:
    StatisticsCommand(
        CTCLInterpreter& interp, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
    );
    virtual ~StatisticsCommand();

    // Forbidden canonicals

private:
    StatisticsCommand(const StatisticsCommand&);
    StatisticsCommand& operator=(const StatisticsCommand&);
    int operator==(const StatisticsCommand&);
    int operator!=(const StatisticsCommand&);

    // Base class methods overrides and pure virtual implementations.
public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

private:
    void MarshallStats(
        CTCLInterpreter& interp, CTCLObject& result, 
        unsigned long triggers, unsigned long bytes
    );
};

#endif