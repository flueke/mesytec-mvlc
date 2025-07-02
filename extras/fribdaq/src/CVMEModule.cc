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
    std::vector<uint32_t> readData;
    for (auto op : oplist) {
      if (op.s_op == Read) {
        readData.push_back(execRead(op));
      } else if (op.s_op == Write) {
        execWrite(op);
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
    result.s_data = data;
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
    smsg << "Width specify must be '16 or '32' but was " << std::string(wid);
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
  auto status = m_pVme->vmeWrite(op.s_address, op.s_data, op.s_modifier, op.s_width);

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
 * createOkResponse
 * 
 *    Creates the return response for successsful list completion.
 * 
 * @param readData -vector of data read.
 * @return std::string the response to send to the caller.
 * 
 */
std::string
CVMEModule::createOkResponse(const std::vector<uint32_t> readData) {
  CTCLInterpreter interp;
  CTCLObject      objResult;
  objResult.Bind(interp);

  for (auto d : readData) {
    std::stringstream s;
    s << std::hex << "0x" << d;
    std::string v(s.str());
    objResult += v;
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