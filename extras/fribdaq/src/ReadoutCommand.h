/**
 * @file ReadoutCommand.h
 * @brief Base class for all Tcl command recongized by fribdaq-readout
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
 #ifndef MVLC_READOUTCOMMAND_H
 #define MVLC_READOUTCOMMAND_H

#include <TCLObjectProcessor.h>

// Forward data types:

struct FRIBDAQRunState;
namespace mesytec {
    namespace mvlc {
        class  MVLCReadout;        
    }
}


/**
 * @class ReadoutCommand
 * 
 * Command processing classes that are part of the fribdaq-readout program will need
 * to be able to access the additional daq context as well as the MVLCReadout object.
 * So this is just a CTCLOjbectProcessor that is constructed with pointers to that 
 * data and stores them in protected member data for subclasses to get:
 */
class ReadoutCommand : public CTCLObjectProcessor {
protected:
    FRIBDAQRunState*            m_pRunState;
    mesytec::mvlc::MVLCReadout* m_pReadout;
    // Canonicals are our only exports:
public:
    ReadoutCommand(
        CTCLInterpreter& interp, const char* command, 
        FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
    );
    virtual ~ReadoutCommand();

    // Disallowed canonicals:

private:
    ReadoutCommand(const ReadoutCommand&);  // don't support copy construction.
    ReadoutCommand& operator=(const ReadoutCommand&); // assignment
    int operator==(ReadoutCommand&);                  // equality compare.
    int operator!=(ReadoutCommand&);                  // inequality compare.
};

 #endif