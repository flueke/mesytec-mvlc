/**
 * @file TclVmeWrapper.cc
 * @brief Implement both VMUSB and VMUSBReadoutList command ensembles to support internal Tcl slow contol drivers.
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
#include "TclVMEWrapper.h"
#include "CVMEModule.h"
#include <mesytec-mvlc/mesytec-mvlc.h>

#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <Exception.h>
#include <stdexcept>
#include <sstream>
#include <algorithm>

using mesytec::mvlc::MVLC;
using mesytec::mvlc::VMEDataWidth;



/////////////////////////////// Implement TclVmeWrapper - vmusb command. ////////////////////////////


/**
 *  constructor:
 *     @param interp - interpreter on which the command is being registered
 *     @param controller - MVLC controller object which is used to execute operations.
 * 
 */
TclVmeWrapper::TclVmeWrapper(CTCLInterpreter& interp, MVLC* controller) :
    CTCLObjectProcessor(interp, "vmusb", TCLPLUS::kfTRUE),
    m_controller(controller)
{}

/** 
 * destructor
 *     We don't own the controller so we won't delete it.
 */
TclVmeWrapper::~TclVmeWrapper() {}

/**
 * operator()
 *     This executes the command itself.  We ensure there's a subcommand and dispatch to the
 * approrpriate subcommand handler with all of the command words bound to the interpreter.
 * We also setup to handle all failures via exceptions throw from both us and the
 * subcommand processors.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - The command words encapsulated in CTCLObject which in turn encapsulate Tcl_Obj*s.
 * @return int   - TCL_OK on success and TCL_ERROR if not.  Errors leave an instructive message in the
 *                 result  Successes may or may not leave results (e.g. reads will but writes won't).
 */
int
TclVmeWrapper::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv); 

    try {
        requireAtLeast(objv, 2, "vmusb: Missing subcommand");
        std::string sc = objv[1];    // Subcommand.
        if (sc == "vmeWrite32") {                   // Writes only set error results..
            vmeWrite(objv, VMEDataWidth::D32);
        } else if (sc == "VmeWrite16") {
            vmeWrite(objv, VMEDataWidth::D16);
        } else if (sc == "vmeRead32") {             // Reads need data marshalled into the result.
            vmeRead32(interp, objv);
        } else if (sc == "vmeRead16") {
            vmeRead16(interp, objv);
        } else if (sc == "vmeBlockRead") {
            vmeBlockRead(interp, objv);
        } else if (sc == "vmeFifoRead") {
            vmeFifoRead(interp, objv);
        } else {
            std::stringstream smsg;
            smsg << "vmusb: Invalid subcommand: " << sc;
            std::string msg(smsg.str());
            throw std::invalid_argument(msg);  
        }
    }
    catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    }
    catch (std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    }
    catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    }

    return TCL_OK;
}

/*    Private methods of the command */
/*    Subcommand processors          */

/**
 * vmeRead32
 *    Perform a VME read that is 32 bits wide.  This is really callling vmeRead and,
 * if it does not throw, marshalling its returned data into the result:
 * 
 * @param interp - interpreter into which we have to stuff the result.
 * @param objv   - The command words
 * 
 */
void
TclVmeWrapper::vmeRead32(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    uint32_t data = vmeRead(objv, VMEDataWidth::D32);
    intToResult(interp, data);
}
/**
 *  vmeRead16
 *    Same as above but for a 16 bit read.
 * 
 * @param interp - interpreter into which we have to stuff the result.
 * @param objv   - The command words
 * 
 */
void
TclVmeWrapper::vmeRead16(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    uint32_t data(vmeRead(objv, VMEDataWidth::D16));
    intToResult(interp, data);
}

/**
 * vmeBlockRead
 *    Perform a 32 bit block read  Addresses increment for each operation.
 * This is a call to blockRead with fifo false.alignas
 *
 * @param interp - interpreter into which we have to stuff the result.
 * @param objv   - The command words
*/
void
TclVmeWrapper::vmeBlockRead(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    std::vector<uint32_t> data = blockRead(objv, false);
    intVecToResult(interp, data);
}
/**
 * vmeFifoRead
 *     Just a call to block read with the fifo flag true:alignas
 * 
 * @param interp - interpreter into which we have to stuff the result.
 * @param objv   - The command words
 */
void
TclVmeWrapper::vmeFifoRead(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    std::vector<uint32_t> data = blockRead(objv, true);
    intVecToResult(interp, data);
}

/*  Utility methods  - intended to decrease code repitition  */

/**
 *  vmeWrite
 *    COmmon code for all VME writes:   
 * - We need 3 additional parameters, the address, address modifier and the data.
 * - These get converted to the appropraite types for the mvlc vmeWrite method.
 * -  The write is performed and any bad status converted into an std::runtime_error.
 * 
 * @param objv - the command words - all bound to an interpreter.
 * @param width - width of the operation.
 */
void
TclVmeWrapper::vmeWrite(std::vector<CTCLObject>& objv, VMEDataWidth width) {
    requireExactly(
        objv, 5, 
        "vmusb: Vme writes require an address, it's modifier and a datum to write"
    );

    uint32_t addr = int(objv[2]);
    uint8_t amod  = decodeAmod(objv[3]);
    uint32_t datum  = int(objv[4]);

    auto status = m_controller->vmeWrite(addr, datum, amod, width);
    if (status) {
        std::stringstream smsg;
        smsg << "vmusb:  Write operation failed: " << status.message();
        std::string msg(smsg.str());
        throw std::runtime_error(msg);
    }
}
/**
 * vmeRead
 *     Common code for  single shot VME read operations.
 * 
 * @param objv - command words.
 * @param width - width of the operation.
 * @return uint32_t - value read if successful.
 * 
 */
uint32_t
TclVmeWrapper::vmeRead(std::vector<CTCLObject>& objv, VMEDataWidth width) {
    requireExactly(
        objv, 4, 
        "vmusb: VME reads require an address, and its modifier"
    );
    uint32_t addr = int(objv[2]);
    uint8_t amod = decodeAmod(objv[3]);
    uint32_t datum;

    auto status = m_controller->vmeRead(addr, datum, amod, width);
    if(status) {
        std::stringstream smsg;
        smsg << "vmusb: Read operation failed: " << status.message();
        std::string msg(smsg.str());

        throw std::runtime_error(msg);
    }
    return datum;
}

/**
 *  blockRead
 *     Common code to perform a block read operation (fifo or otherwise).
 * 
 * @param objv - command words.
 * @param width - width of the operation.
 * @return std::vector<uint32_t> -Data that were read.
 */
std::vector<uint32_t>
TclVmeWrapper::blockRead(std::vector<CTCLObject>& objv, bool fifo) {
    requireExactly(objv, 5,
        "vmusb: block reads require a starting address its adresss modifier and a transfer count"
    );

    uint32_t addr = int(objv[2]);
    uint8_t  amod = decodeAmod(objv[3]);
    uint16_t xfers = int(objv[4]);
    std::vector<uint32_t> result;

    auto status = m_controller->vmeBlockRead(addr, amod, xfers, result, fifo);

    if(status) {
        std::stringstream smsg;
        smsg << "vmusb - Block transfer failed; " << status.message();
        std::string msg(smsg.str());

        throw std::runtime_error(msg);
    }
    return result;
}
/** 
 * intToResult 
 * 
 * Set the result from an integer:
 * 
 * @param interp - interp whose result is set.
 * @param data   - integer value to set the result as.
 */
void
TclVmeWrapper::intToResult(CTCLInterpreter& interp, uint32_t data) {
    CTCLObject result;
    result.Bind(interp);

    result = (int)data;

    interp.setResult(result);
}

/**
 * intVecToResult
 *     Takes an integer vector and converts it into a Tcl list which is
 * set as the interpreter result.
 * 
 * @param interp the interpreter whose result will be set.
 * @param data vector of data to use.
 */
void
TclVmeWrapper::intVecToResult(CTCLInterpreter& interp, const std::vector<uint32_t>& data) {
    CTCLObject result;
    result.Bind(interp);


    for (auto& d : data) {
        result += (int)d;
    }

    interp.setResult(result);
}

/**
 *  decodeAmod
 *     Decode an adress modifier:
 *     Address modifiers must be positive integers that fit in a byte.
 * 
 * @param obj - a bound CTCLObject that has the supposed modifier.
 * @return uint8_t - the decoded amod
 * 
 * @throw CTCLException if the obj is not convertible to an integer
 * @throw std::invalid_argument if the resulting integer does not fit in a byte.
 */
uint8_t
TclVmeWrapper::decodeAmod(CTCLObject& obj) {
    int value = obj;   // could throw CTCLException

    if ((value & 0xff) != value) {
        std::stringstream smsg;
        smsg << std::hex << "Invalid address modifier: 0x" << value  << " Must be smaller than 0x100";
        std::string msg(smsg.str());
        throw std::invalid_argument(msg);
    }
    return value;
}

//////////////////////////////////////////////////  TCLVmeListWrapper implementation //////////////////////

/**
 *  constructor:
 *     @param interp  - interpreter on which the 'vmusbreadoutlist' will be reistered.
 *     @param controller - Controller that will execute the list.
 * 
 */
TCLVmeListWrapper::TCLVmeListWrapper(CTCLInterpreter& interp, mesytec::mvlc::MVLC* controller) :
    CTCLObjectProcessor(interp, "vmusbreadoutlist", TCLPLUS::kfTRUE),
    m_pListProcessor(0)
{
    m_pListProcessor = new CVMEModule(controller);
}

/**
 *  destructor
 *      must destroy the VME module helper object.
 */
TCLVmeListWrapper::~TCLVmeListWrapper() {
    delete m_pListProcessor;
}
/**
 *  operator()
 *     Gets control to execute the command.  We just require ther be a subcommand and
 *     dispatch to the correct subcommand processor.  Note that what we're going to do
 *     is maintain a vector of valid command list entries accepted by CVMeModule.
 *     We'll set up to handle all errors by catching appropriate exception types and 
 *     converting them to result sets and TCL_ERROR returns.
 * 
 * @param interp - interpreter exeuting the command.
 * @param objv   - The command words.
 * @return int   - TCL_OK if the command executed correctly.  TCL_ERROR if not.
 * 
 * The intepreter result is set with an error message if we return TCL_ERROR, otherwise,
 * the result depends on the subcommand.
 * 
 */
int
TCLVmeListWrapper::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);
    try {
        requireAtLeast(objv, 2, "vmusbreadoutlist - requires a subcommand");
        std::string sc = objv[1];

        if (sc == "clear") {
            clear(interp, objv);
        } else if (sc == "size") {
            size(interp, objv);
        } else if (sc == "execute") {
            execute(interp, objv);
        } else if (sc == "addWrite16") {
            addWrite16(interp, objv);
        } else if (sc == "addWrite32") {
            addWrite32(interp, objv);
        } else if (sc == "addRead16") {
            addRead16(interp, objv);
        } else if (sc == "addRead32") {
            addRead32(interp, objv);
        } else if (sc == "tobytes") {
            toBytes(interp, objv);
        } else {
            std::stringstream smsg;
            smsg << "vmereadoutlist - " << sc << " is not a valid subcommand";
            std::string msg(smsg.str());
        
            throw std::invalid_argument(msg);
        }
    }
     catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    }
    catch (std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    }
    catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    }

    return TCL_OK;
}
/*   Subcommand processors: */

/**
 *  clear
 *    Clear the stored list of operations.
 * @param interp - interpreter exeuting the command.
 * @param objv   - The command words.
 */
void
TCLVmeListWrapper::clear(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 2, "vmusbreadoutlist clear- Requires no additional command words");

    m_list.clear();
}
/**
 *  size
 *     Return the number of entries in the command list
 *
 *  @param interp - the interpreter executing the command.
 *  @param objv   - COmmand words.
 * 
 * Sets the result to the size of the list.
 */
void
TCLVmeListWrapper::size(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 2, "vmusbreadoutlist size - requires no additional command words.");
    TclVmeWrapper::intToResult(interp, m_list.size());
}
/**
 * execute
 *    - Marshalls the list of VME operationss into a parameter value acceptable by the
 * CVMEModule::Set method and runs that method.  The resulting string is analyzed.
 * If it starts with "ERROR" an std::runtime_error is thrown with that string.
 * If not, thedata after the "OK" prefix is trimmed of are set as the result.  They are already a valid
 * Tcl list.
 */
void
TCLVmeListWrapper::execute(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(
        objv, 2, 
        "vmusdbreadoutlist execute - requires no additional command words"
    );

    std::string oplist = marshallRequest();
    std::string result = m_pListProcessor->Set("list", oplist.c_str());

    if (result.substr(0, 5) == "ERROR") {
        throw std::runtime_error(result);
    }

    // Trim off the OK and set the result:

    result = result.substr(2);
    interp.setResult(result);
}

/**
 * addWrite16
 *     This is just adding a write that is 16 bits long.  All error checking and parameter decoding
 * is done by addWrite which has the common code.
 * 
 * @param interp  - interpreter running the command.
 * @param obvjv   - command words.
 */
void
TCLVmeListWrapper::addWrite16(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    addWrite(objv, 16);
}
/**
 *  addWrite32
 *    Same as above but a 32 bit write.
 */
void
TCLVmeListWrapper::addWrite32(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    addWrite(objv, 32);
}

/**
 *  addRead16
 *     Add a 16 bit read to the list.  This is all done by addRead - -we just supply the width.
 */
void
TCLVmeListWrapper::addRead16(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    addRead(objv, 16);
}
/**
 *  addWrite32
 */
void
TCLVmeListWrapper::addRead32(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    addRead(objv, 32);
}

/**
 * toBytes
 *    executes the tobytes subcommand.  Expects a single extra parameter, the list of data
 * returned from an execute operation.  This takse that data and converts it into a list of bytes.
 * This requires processing the list and checking which list items are 16 and which are 32 bit reads
 * 
 * @param interp - interpreter that's running the command.
 * @param objv   - Command words.
 * @note on success, the result is set with a list of bytes.
 */
void
TCLVmeListWrapper::toBytes(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    
    requireExactly(objv, 3, "vmusbreadoutlist tobytes requires only the values from execute");
    std::vector<CTCLObject> dataList = objv[2].getListElements();
    bindAll(interp, dataList);

    auto pListItem = m_list.begin();
    CTCLObject result;
    result.Bind(interp);

    for (auto d : dataList) {
        pListItem = findNextRead(pListItem);
        if(pListItem == m_list.end()) {
            // Not enough reads in the listt

            throw std::string("vmusbreadoutlist - There are not enough reads for the amount of data you gave me");
        }
        int data = d;
        result += data & 0xff;   // bottom byte.
        result += (data >> 8) & 0xff; // Full bottom 16 bits.
        if (isLong(pListItem)) {
            // ah but it was a 32 bit read so we're not done yet:

            result += (data >> 16) & 0xff;
            result += (data >> 24) & 0xff;
        }

        ++pListItem;             // start search with next item.
    }
    // iterate over the dat items... find the next read and its width.

    interp.setResult(result);
}


/**  Utilities  */

/**
 *  addWrite
 *     Encode a write operation in the list.  This is a string of the form:
 * 
 * ```
 *   w amod addresss data width
 *```

 @param objv - the command words.  We need 5 of them containing in order address, amod, data
 @param bits - number of bits, 16 or 32.
 */
void
TCLVmeListWrapper::addWrite(std::vector<CTCLObject>& objv, int bits) {
    requireExactly(objv, 5, "VME Write operatinos need address, amod, and data");

    uint32_t addr = (int)objv[2];
    uint8_t amod = TclVmeWrapper::decodeAmod(objv[3]);
    uint32_t data  = (int)objv[4];

    std::stringstream encoder;
    encoder << std::hex << "w 0x" << (uint32_t)amod << " 0x" << addr << " 0x" << data;
    encoder << " " << bits;

    std::string op(encoder.str());
    m_list.push_back(op);
}
/**
 *  addRead
 *     Encode a read operation for the CVMEModule.  The form of a read
 * operation is:
 * ```
 *   r amod address width
 * ```
 * 
 * @param objv  - Command words.
 * @param bits  - number of bits (16 or 32).
 * 
 * An additional operation will be pushed into m_list.
 * 
 */
void
TCLVmeListWrapper::addRead(std::vector<CTCLObject>& objv, int bits) {
    requireExactly(objv, 4, "VME read operations need address and amod");

    uint32_t addr = (int)objv[2];
    uint8_t amod = TclVmeWrapper::decodeAmod(objv[3]);

    std::stringstream encoder;
    encoder << std::hex << "r 0x" 
        << (uint32_t)amod << " 0x" << addr << " " << std::dec << bits;
    std::string op(encoder.str());
    m_list.push_back(op);
}
/**
 *  marshallRequst
 *     Turn the list of operations into a request string.  This
 * is just a matter of making a Tcl list.
 * 
 * @return std::string - parameter value for CVMEModule::Set.
 * @note I was clever? lazy? enough so that the operations are already
 *       in the form expected by CVMEModule::Set() operations.
 */
std::string
TCLVmeListWrapper::marshallRequest() {
    CTCLInterpreter& interp(*(getInterpreter()));
    CTCLObject result;
    result.Bind(interp);

    for (auto& s : m_list) {
        result += s;
    }
    return std::string(result);
}
/**
 *  findNextRead
 *     Given a starting point in the list, find the next read operation and return an iterator to it.
 *     We can use find_if for that; witha lambda that wants the first character of the list item to be 
 *     "r"
 * 
 * @param start - iterator saying where to start looking.
 * @return std::vector<std::string>::iterator - pointer to next read or end() if not found.
 */
std::vector<std::string>::iterator
TCLVmeListWrapper::findNextRead(std::vector<std::string>::iterator& start) {
    
    return std::find_if(
        start, m_list.end(),
        [](const std::string& v) { 
            return v[0] == 'r';
        }
    );
}
/**
 * isLong
 *    Returns true if the opeation is a 32 bit operation.  
 * 
 * @param pItem -Itertor 'pointing' to the item to check.
 * @return bool - true if a 32 bit op.
 */
bool
TCLVmeListWrapper::isLong(std::vector<std::string>::iterator& pItem) {
    // Last 2 chara of the operation are the width:

    std::string op = *pItem;
    auto swidth= op.substr(op.size() - 3);  // Last two chars.
    return swidth == "32";
}