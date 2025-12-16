/*
@file CVMEModule.cc
@brief Implementation of the generic VME operations slow control driver.
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2025.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

#include "CVMEModule.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <TCLInterpreter.h>                 // For list processing.
#include <TCLObject.h>
#include <Exception.h>
#include <stdexcept>
#include <sstream>


using mesytec::mvlc::MVLC;

//////////////////////////////////////////////////////////////
// Implementation of canonical operations.
//
/**
 * Construction is even easier than normal because we don't actually
 * need the configuration.
 * @param controller - the VME controller that wil lbe 
 */
CVMEModule::CVMEModule(MVLC* controller) :
  SlowControlsDriver(controller)
{}

/**
 * Destruction is literally a no-op.
 */
CVMEModule::~CVMEModule()
{
}

/** Set
 *    Called to execute a set operation.
 *    - Ensure the parameter is 'list'.
 *    - Translate the value list into an internal representation - yelling
 *      if the list had invalid elements.
 *    - Execute the list  - failing on any failing operations.
 *    - Marshall the reply from the read data.
 * 
 * @param pv - parameter name - must be "list"
 * @param value - List of operations to create.
 * @return On success OK {Read-data}  On failure ERROR - and the reason.
 */
std::string
CVMEModule::Set(const char* pv, const char* value) {
  // Note all of the utilities throw an stdexcept

  try {
    if (std::string("list") != pv) {
      return "ERROR - parameter name must be 'list'";
    }
    std::vector<VmeOperation> oplist = CompileList(value);
    std::vector<std::vector<uint32_t>> readData; // Results of each read operation.
    for (auto op : oplist) {
      if (op.s_op == Read) {
        uint32_t data = execRead(op);
        std::vector<uint32_t> singleRead;
        singleRead.push_back(data);
        readData.push_back(singleRead);
      } else if (op.s_op == Write) {
        execWrite(op);
      } else if (op.s_op == ReadBlock) {
        readData.push_back(execReadBlock(op));
      } else if (op.s_op == ReadFifo) {
        readData.push_back(execReadFifo(op));
      } else if (op.s_op == WriteBlock) {
        execWriteBlock(op);
      } else if (op.s_op == WriteFifo) {
        execWriteFifo(op);  
      } else {
        throw std::logic_error("BUG - compiled list has an unrecognzed operation type.");
      }
    }

    return createOkResponse(readData);
  }
  catch (std::exception& e) {
    std::stringstream smsg;
    smsg << "ERROR - " << e.what();
    std::string response(smsg.str());
    return response;
  }
  catch (CException& e) {                    // list processing may make CTCLException
    std::stringstream smsg;
    smsg << "ERROR - " << e.ReasonText();
    std::string response(smsg.str());
    return  response;
  }
}

/**
 * Get
 *     Get is illegal
 * 
 * @return std::string "ERROR - Get is not allowed for VME slow controls modules"
 */
std::string
CVMEModule::Get(const char* pv) {
  return "ERROR - Get is ot allowed for VME slow controls modules";
}
/**
 * Update
 *    This is a no-op for us.
 */
void
CVMEModule::Update() {}

////////////////////////////////////////  Private utilities

/*
   Methods to turn the operation list into internal form.  These throw exceptions
   on error.  The exception will include the sublist that failed.
 */

/**
* Compile the full parameter value into a vector of operations.
* 
* @parm ops - the value from Set.
* @return std::vector<VMEOperations> - internal from of the operations.
*/
std::vector<CVMEModule::VmeOperation>
CVMEModule::CompileList(const char* ops) {

    std::vector<VmeOperation> result;
    // Turn this thing ito a CTCLObject and process it as a list:

    CTCLInterpreter interp;
    CTCLObject      list;
    list.Bind(interp);
    list = ops; 

    // Process it as the list it must be:

    auto elements = list.getListElements();     // Vetor or CTCLObjects.
    for (auto& e : elements) {
      e.Bind(interp);
      result.push_back(CompileItem(e));        // Could throw.
    }

    return result;
}

/**
 *  CompileItem
 *     Compile a single item in to a VmeOperation:
 * 
 * @param op - List containing the operation see the header for the contents.
 * @return VmeOperation - the compiled operation.
 * @throw std::invalid_argument - on failure.
 */
CVMEModule::VmeOperation 
CVMEModule::CompileItem(CTCLObject& op) {
  VmeOperation result;

  // SHould be a list as well:

  auto definition = op.getListElements();
  for (auto& o : definition) {                     // Supply the elments wth an interp.
    o.Bind(op.getInterpreter());
  }
  // First item shoulid be a string containig either "r" or "w":

  if (std::string(definition[0]) == "r") {
    // Read - must be 4 items:

    if (definition.size() != 4) {
      std::stringstream smsg;
      smsg << std::string(op) << " must be a 4 element list for reads";
      std::string msg(smsg.str());
      throw std::invalid_argument(msg);
    }
    int amod = definition[1];
    uint32_t adr =  int(definition[2]);
    mesytec::mvlc::VMEDataWidth wid = decodeWidth(definition[3]);
    
    
    result.s_op = Read;
    result.s_modifier = amod;
    result.s_address = adr;
    result.s_width = wid;

  } else if (std::string(definition[0]) == "w") {
    // Write must be a 5 item list:
    if (definition.size() != 5) {
      std::stringstream smsg;
      smsg << std::string(op) << " must be a 5 element list for writes";
      std::string msg(smsg.str());
      throw std::invalid_argument(msg);
    }
    int amod = definition[1];
    uint32_t add = int(definition[2]);
    uint32_t data = int(definition[3]);
    mesytec::mvlc::VMEDataWidth wid = decodeWidth(definition[4]);
    
    result.s_op = Write;
    result.s_modifier = amod;
    result.s_address = add;
    result.s_data.push_back(data);
    result.s_width = wid;

  } else if (std::string(definition[0]) == "rb") {
    // Read block - must be 5 items:
    if (definition.size() != 5) {
      std::stringstream smsg;
      smsg << std::string(op) << " must be a 5 element list for read block operations";
      std::string msg(smsg.str());
      throw std::invalid_argument(msg);
    }
    int amod = definition[1];
    uint32_t adr =  int(definition[2]);
    uint32_t count = int(definition[3]);
    mesytec::mvlc::VMEDataWidth wid = decodeWidth(definition[4]);
    
    
    result.s_op = ReadBlock;
    result.s_modifier = amod;
    result.s_address = adr;
    result.s_count = count;
    result.s_width = wid;
  } else if (std::string(definition[0]) == "rf") {
    // Read fifo - must be 5 items:
    if (definition.size() != 5) {
      std::stringstream smsg;
      smsg << std::string(op) << " must be a 5 element list for read fifo operations";
      std::string msg(smsg.str());
      throw std::invalid_argument(msg);
    }
    int amod = definition[1];
    uint32_t adr =  int(definition[2]);
    uint32_t count = int(definition[3]);
    mesytec::mvlc::VMEDataWidth wid = decodeWidth(definition[4]);
    
    
    result.s_op = ReadFifo;
    result.s_modifier = amod;
    result.s_address = adr;
    result.s_count = count;
    result.s_width = wid;
  } else if (std::string(definition[0]) == "wb") {
    // Write block - must be 5 items:
    if (definition.size() != 5) {
      std::stringstream smsg;
      smsg << std::string(op) << " must be a 5 element list for write block operations";
      std::string msg(smsg.str());
      throw std::invalid_argument(msg);
    }
    int amod = definition[1];
    uint32_t adr =  int(definition[2]);
    mesytec::mvlc::VMEDataWidth wid = decodeWidth(definition[3]);
    // The last item is a list of data items:
    CTCLObject dataList = definition[4];
    dataList.Bind(op.getInterpreter());
    auto dataElements = dataList.getListElements();
    for (auto& de : dataElements) {
      de.Bind(op.getInterpreter());
      result.s_data.push_back(int(de));
    }
    result.s_op = WriteBlock;
    result.s_modifier = amod;
    result.s_address = adr;
    result.s_width = wid;

  } else if (std::string(definition[0]) == "wf") {
    // Write fifo - must be 5 items:
    if (definition.size() != 5) {
      std::stringstream smsg;
      smsg << std::string(op) << " must be a 5 element list for write fifo operations";
      std::string msg(smsg.str());
      throw std::invalid_argument(msg);
    }
    int amod = definition[1];
    uint32_t adr =  int(definition[2]);
    mesytec::mvlc::VMEDataWidth wid = decodeWidth(definition[3]);
    // The last item is a list of data items:
    CTCLObject dataList = definition[4];
    dataList.Bind(op.getInterpreter());
    auto dataElements = dataList.getListElements();
    for (auto& de : dataElements) {
      de.Bind(op.getInterpreter());
      result.s_data.push_back(int(de));
    }
    result.s_op = WriteFifo;
    result.s_modifier = amod;
    result.s_address = adr;
    result.s_width = wid;

  } else {
    // Invalid operation string:

    std::stringstream smsg;
    smsg << "Invalid operation code " << std::string(definition[0]) << " in " << std::string(op);
    std::string msg(smsg.str());

    throw std::invalid_argument(msg);
  }
  return result;
}

/**
 * decodeWidth
 *    Given a width specifier object (16 or 32), return the correct
 * meystec::mvlc:VMEDataWidth value.
 * 
 * @param wid - CTCLObject containing the width specifier (assumed bound).
 * @return mesytec::mvlc::VMEDataWidth 
 * @throw CTCLException if the wid is not an integer.
 * @throw std::invalid_argument if the width is not valid.
 */
mesytec::mvlc::VMEDataWidth
CVMEModule::decodeWidth(CTCLObject& wid) {
  int specifier = wid;                     // Can throw.

  if(specifier == 16) {
    return mesytec::mvlc::VMEDataWidth::D16;
  } else if (specifier == 32) {
    return mesytec::mvlc::VMEDataWidth::D32;
  } else {
    // invalid:

    std::stringstream smsg;
    smsg << "Width specify must be '16 or '32' buSt was " << std::string(wid);
    std::string msg(smsg.str());
    throw std::invalid_argument(msg);
  }
}

////////////////////////////// VME single operations:

/**
 * Write
 *    Perform the write operation specified:
 * 
 * @param op - references the operation to perform.
 * @throw runtime_error if the operation failed:
 */
void
CVMEModule::execWrite(const VmeOperation& op) {
  auto status = m_pVme->vmeWrite(op.s_address, op.s_data[0], op.s_modifier, op.s_width);

  if (status) {
    std::stringstream smsg;
    smsg << " VME write to 0x" << std::hex << op.s_address << " failed: " << status.message();
    std::string msg(smsg.str());
    throw std::runtime_error(msg);
  }
}

/**
 * Read
 * 
 *    Performs a VME read operation.
 * 
 * @param op - references the description of the read.
 * @return uint32_t - value read on success.
 * @throw std::runtime_error - if the read operation failed.
 */
uint32_t
CVMEModule::execRead(const VmeOperation& op) {
  uint32_t result;
  auto status = m_pVme->vmeRead(op.s_address, result, op.s_modifier, op.s_width);
  if(status) {
    std::stringstream smsg;
    smsg << " VME read from 0x" << std::hex << op.s_address << " failed: " << status.message();
    std::string msg(smsg.str());
    throw std::runtime_error(msg);
  }


  return result;
}

/**
 * execReadBlock
 *    Perform a VME block read operation.
 * 
 * @param op - references the operation to perform.
 * @return std::vector<uint32_t> - the data read.
 * @throw runtime_error if the operation failed:
 */
std::vector<uint32_t>
CVMEModule::execReadBlock(const VmeOperation& op) {
  std::vector<uint32_t> result;
  auto status = m_pVme->vmeBlockRead(
    op.s_address, op.s_modifier, op.s_count, result, false
  );
  if (status) {
    std::stringstream smsg;
    smsg << " VME block read from 0x" << std::hex << op.s_address << " failed: " << status.message();
    std::string msg(smsg.str());
    throw std::runtime_error(msg);
  }
  return result;
}
/**
 * execReadFifo
 *    Perform a VME fifo read operation.
 * 
 * @param op - references the operation to perform.
 * @return std::vector<uint32_t> - the data read.
 * @throw runtime_error if the operation failed:
 */
std::vector<uint32_t>
CVMEModule::execReadFifo(const VmeOperation& op) {
  std::vector<uint32_t> result;
  auto status = m_pVme->vmeBlockRead(
    op.s_address, op.s_modifier, op.s_count, result, true
  );
  if (status) {
    std::stringstream smsg;
    smsg << " VME fifo read from 0x" << std::hex << op.s_address << " failed: " << status.message();
    std::string msg(smsg.str());
    throw std::runtime_error(msg);
  }
  return result;
}
/**
 * execWriteBlock
 *    Perform a VME block write operation.
 * Note that these are simulated by multiple single writes.
 * We cook the address modifier to make it a single shot operation.
 * All block transfer amods have the bottom bit set...this
 * gets transparently cleared.
 * 
 * @param op - references the operation to perform.
 * @throw runtime_error if any of the write operations failed:
 * @note that this means that in the event of an exception, some of the
 * operations will have worked.
 */
void
CVMEModule::execWriteBlock(const VmeOperation& op) {
  uint8_t singleShotAmod = op.s_modifier & 0xFE;
  auto address = op.s_address;    // Since op is immutable.
  for (auto dataItem : op.s_data) {
    auto status = m_pVme->vmeWrite(address, dataItem, singleShotAmod, op.s_width);
    if (status) {
      std::stringstream smsg;
      smsg << " VME write to 0x" << std::hex << op.s_address << " failed: " << status.message();
      std::string msg(smsg.str());
      throw std::runtime_error(msg);
    }
    // Adjust to the next address.
    address += (op.s_width == mesytec::mvlc::VMEDataWidth::D16) ? 2 : 4;
  }
}
/**
 * execWriteFifo
 *   Perform a VME fifo write operation.  Note this must be simulated
 * since there is no such operation in the MVLC class.
 * Therefore, the address modifier is cooked to be a single shot operation
 * and multiple single writes are performed all to the same address.
 * 
 * @param op - references the operation to perform.
 * @throw runtime_error if any of the write operations failed:
 * @note that this means that in the event of an exception, some of the
 * operations will have worked.
 * 
 */
void
CVMEModule::execWriteFifo(const VmeOperation& op) {
  uint8_t singleShotAmod = op.s_modifier & 0xFE;
  for (auto dataItem : op.s_data) {
    auto status = m_pVme->vmeWrite(op.s_address, dataItem, singleShotAmod, op.s_width);
    if (status) {
      std::stringstream smsg;
      smsg << " VME write to 0x" << std::hex << op.s_address << " failed: " << status.message();
      std::string msg(smsg.str());
      throw std::runtime_error(msg);
    }
    // All addresses are the same in fifo writes.
  }
}

/**
 * createOkResponse
 * 
 *    Creates the return response for successsful list completion.
 * 
 * @param readData -vector of data read.
 * @return std::string the response to send to the caller.
 * @retval the string is a the text "OK" followed by a properly formatted
 * Tcl list (possibily empty).  The list contains either single items that
 * are the values read by single read operations, or sublists that are the
 * values read by block/fifo read operations.
 * 
 */
std::string
CVMEModule::createOkResponse(const std::vector<std::vector<uint32_t>>& readData) {
  CTCLInterpreter interp;   // Captive interpreter for building the result.
  CTCLObject      objResult;
  objResult.Bind(interp);
  
  // Outer list.

  for (auto d : readData) {
    // If d is a single element array, just add the element by itself:

    if (d.size() == 10) {
      std::stringstream s;
      s << std::hex << "0x" << d[0];
      std::string v(s.str());
      objResult += v;
      
    } else {
      // Make a sublist:
      CTCLObject sublist;
      sublist.Bind(interp);
      for (auto item : d) {
        std::stringstream s;
        s << std::hex << "0x" << item;
        std::string v(s.str());
        sublist += v;
      }
      objResult += sublist;
    }
    
  }

  std::stringstream sr;
  sr << "OK " << std::string(objResult);
  std::string result(sr.str());

  return result;

}

///////////////////////// Creator and registration.  We register both as vmusb and vme

SlowControlsDriver* 
VmeCreator::create(MVLC* controller) {
  return new CVMEModule(controller);                       // No config params.
}


VmeCreator::Register::Register() {
  SlowControlsFactory::getInstance()->addCreator("vmusb", new VmeCreator);
  SlowControlsFactory::getInstance()->addCreator("vme", new VmeCreator);
}

static VmeCreator::Register registrar;