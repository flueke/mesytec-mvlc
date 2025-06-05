/**
 * @file SlowControlsMonCommand.h
 * @brief Header for the slow controls "Mon" command.
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
#ifndef MVLC_SLOWCONTROLSMONCOMMAND_H
#define MVLC_SLOWCONTROLSMONCOMMAND_H

#include <TCLObjectProcessor.h>


/**
 * @class SlowControlsMonCommand
 * 
 * This class provides the slow controls 'mon' command.  The mon command fetches stuff that
 * should be monitored from a driver.  For example a driver for a high voltage unit might need
 * to monitor trips or currents or whatever.
 *  
 * In the VMUSBReadout, due to he autonomous mode of operation and the fact that it cannot
 * perform immediate mode operations when in autonomous mode, this had to be implemented with
 * each driver contributing to a list that was periodically software triggered (via the action reg), 
 * and whose data were then distributed amongst the drivers.
 * 
 * With the MVLC where executing immediate mode operations while data taking is possible, 
 * we can just have drivers read what's needed on demand and return it
 * 
 * In any event, the form of the mon command is
 * 
 * ```tcl
 *    mon module-name
 * ```
 * The result on a success is the textual form of the monitored data in a format determined by the
 * driver.
 */
class SlowControlsMonCommand : public CTCLObjectProcessor {
    // exported canonicals
public:
    SlowControlsMonCommand(CTCLInterpreter& interp);
    virtual ~SlowControlsMonCommand();

    // forbidden canoniocals:
private:
    SlowControlsMonCommand(const SlowControlsMonCommand&);
    SlowControlsMonCommand& operator=(const SlowControlsMonCommand&);
    int operator==(const SlowControlsMonCommand&);
    int operator!=(const SlowControlsMonCommand&);

    // operations:
public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};


#endif