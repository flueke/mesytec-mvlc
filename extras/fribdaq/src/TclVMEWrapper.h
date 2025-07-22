/**
 * @file TclVMEWrapper.h
 * @brief Header for VMUSB and VMUSBReadoutList command ensembles to support internal Tcl slow contol drivers.
 * @note The command ensembles are registered as VMUSB and VMUSBReadoutList for compatibility with
 *       Tcl drivers in the VMUSBReadout.  Typically, in fact, drivers won't directly see these commands
 *       but will be have the command instance name passed to them.
 * @note We use a captive instance of a CVMEModule to handle  list execution in order not to have
 *      to re-invent that wheel, but we do directly implement individual VME operations for the VMUSB command.
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
#ifndef MVLC_TCLVMEWRAPPER_H
#define MVLC_TCLVMEWRAPPER_H

#include <TCLObjectProcessor.h>
#include <stdint.h>
#include <string>

namespace mesytec {
    namespace mvlc {
        class MVLC;
        enum class VMEDataWidth;
    }
}

class CVMEModule;                     // For list processing.

/**
 * @class TCLVmeWrapper
 *    This class registers as the vmusb command and is a command ensemble that supports
 * individusl VME operations.  Its subcommands are based around the CVMUSB operations for compatiblity
 * with existing slow controls drivers for VMUSBReadout:
 * 
 *  - ```vmeWrite32 address amod data``` - Perform and VME write operation that is 32 bits wide.
 *  - ```vmeWrite16 address amod data``` - Performa 16 bit VME write operation.
 *  - ```vmeRead32 addresss amod``` Returns the results of a 32 bit vme read.
 *  - ```vmeRead16 address amod```  Returns the results ofr a 16 bit VME read.
 *  - ```vmeBlockRead address amod count``` returns the data from a block read.
 *  - ```vmeFifoRead address amod count``` Returns the data from a FIFO read.
 * 
 * Note that in the above  subcommand specifications:
 *   - addresss is a VME address the subcommand is directed at.
 *   - amod is an address modifier with which the operation's cycles will be performed.
 *     note that the addreess modifier must make sense for the operations (e.g. a block transfer
 *     operation must use a block transfer amod).
 *   - data is a datume to write.
 *   - count is the number *transer* to perform (not the number of bytes to transfer but number of
 *     32 bit items to read.
 * 
 * While these operations are performed via the MVLC controller, the command will be registered as
 * vmusb.  Typically this won't matter since the command base name is passed in to the Tcl driver
 * operations by the driver's C++ jacket.
 * 
 */
class TclVmeWrapper :  public CTCLObjectProcessor {
private:
    mesytec::mvlc::MVLC* m_controller;            // Controller used to do the operations.

    // Canonicals:
public:
    TclVmeWrapper(CTCLInterpreter& interp, mesytec::mvlc::MVLC* controller);
    virtual ~TclVmeWrapper();
    
    // forbidden canonicals:

private:
    TclVmeWrapper(const TclVmeWrapper&);
    TclVmeWrapper& operator=(const TclVmeWrapper&);
    int operator==(const TclVmeWrapper& ) const;
    int operator!=(const TclVmeWrapper&) const;


    // operations:
public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // Subcommand handlers:
private:

    
    void vmeRead32(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void vmeRead16(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void vmeBlockRead(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void vmeFifoRead(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // utilities:
private:
    // DRY methods

    void vmeWrite(std::vector<CTCLObject>& objv, mesytec::mvlc::VMEDataWidth width);
    uint32_t vmeRead(std::vector<CTCLObject>& objv, mesytec::mvlc::VMEDataWidth width);
    std::vector<uint32_t> blockRead(std::vector<CTCLObject>& objv, bool fifo);
    
    
public:
    static void intToResult(CTCLInterpreter& interp, uint32_t data);
    static void intVecToResult(CTCLInterpreter& interp, const std::vector<uint32_t>& data);
    static uint8_t decodeAmod(CTCLObject& obj); // TCLVMeListWrapper can use this too.

};

/**
 * @class TCLVmeListWrapper
 * 
 * This class wraps the creation and execution of list operations. While operations are done
 * vme a MVLC, it is, for compatibility with existing  modules, given the command
 * vmusbreadoutlist.  I don't think many slow controls Tcl drivers use lists but, in any
 * event, this capability is given here. The command is an esemble with the subcommands:
 * 
 *  - ```clear``` - clear the list.
 *  - ```size```  - Returns the number of operations in the list.
 *  - ```execute``` - execute the list leaving any read data as the result.
 *  - ```addWrite16``` address amod data```  - Adds a 16 bit write operation to the list.
 *  - ```addWrite32``` address amod data``` -  adds a 32 bit write to the list
 *  - ```addRead16```  address amod``` - Adds a 16 bit read to the list.
 *  - ```addRead32```  address amod``` - adds a 32 bit read to the list.
 *  - ```tobytes```  result-from-execute - convers to a list of bytes.
 * 
 * List parameters are have the same meaning as for the VME wrapper above.
 * 
 * @note adding block, operations requires additions to the CVMEModule which are possible
 * but will be done as needed rather than immediately.  One could imagine block reads
 * returning a list and block writes takinga  list of data etc.
 * 
 * @note TODO: in the future, we can implement this in terms of the mvlc stack command builder
 * and mvlc directly.  Just have to take a bit more time to understand that part of the API.
 */
class TCLVmeListWrapper : public CTCLObjectProcessor {
private:
    CVMEModule* m_pListProcessor;
    std::vector<std::string> m_list;                   // Stringified operation list.

    // Canonicals:
public:
    TCLVmeListWrapper(CTCLInterpreter& interp, mesytec::mvlc::MVLC* controller);
    virtual ~TCLVmeListWrapper();

    // Forbidden canonicals:

private:
    TCLVmeListWrapper(const TCLVmeListWrapper&);
    TCLVmeListWrapper& operator=(const TCLVmeListWrapper&);
    int operator==(const TCLVmeListWrapper&) const;
    int operator!=(const TCLVmeListWrapper&) const;

    // operations:
public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);


    // Subcommand handlers:

private:
    void clear(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void size(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void execute(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void addWrite16(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void addWrite32(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void addRead16(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void addRead32(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void toBytes(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // DRY utilities:
private:
    void addWrite(std::vector<CTCLObject>& objv, int bits);
    void addRead(std::vector<CTCLObject>& objv, int bits);
    std::string marshallRequest();
    std::vector<std::string>::iterator findNextRead(std::vector<std::string>::iterator& start);
    bool isLong(std::vector<std::string>::iterator& pItem);

    // we'll use TclVmeWrapper::decodeAmod.
};


#endif