/**
 * @file StateUtils.h
 * @brief Define functions that are useful for all state change commands.
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
 */

 #ifndef MVLC_STATEUTILS_H
 #define MVLC_STATEUTILS_H

// Forward defs.
struct FRIBRunState;
namespace mesytec {
    namespace mvlc {
        class  MVLCReadout;        
    }
}

extern bool 
canBegin(mesytec::mvlc::MVLCReadout& rdo, FRIBDAQRunState& ExtraRunState);

extern bool
canEnd(FRIBDAQRunState& ExtraRunState);

extern bool 
canPause(FRIBDAQRunState& ExtraRunState);

extern bool
canResume(FRIBDAQRunState& ExtraRunState);

 #endif